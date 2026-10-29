-- Behavior trees for monsters (monster property ai = "behavior", behavior = "behaviors/<name>.json").
--
-- A tree is a graph of nodes made in the editor (Project > Behaviors) and stored in
-- behaviors/<name>.json. Every frame the root is ticked and each node answers "success",
-- "failure" or "running". See docs/BEHAVIOR.md.
--
--   composites   sequence (all in order), selector (first that works, re-evaluated every
--                tick so higher priorities interrupt), parallel
--   decorators   inverter, succeeder, failer, repeat, cooldown, timeout
--   conditions   can_see, has_target, distance, health, compare, chance, heard, lost_target
--   actions      wait, idle, move_to, flee_from, strafe, face, attack, patrol, set_bb,
--                trigger_anim, sound, alert_neighbors, log
--
-- Blackboard: every monster has a table of values (self.bb) the tree reads and writes by
-- key; a shared one is reachable with the prefix "global.". The engine keeps these keys up to
-- date: target, can_see, distance, last_seen_pos, last_seen_time, heard_pos, heard_time,
-- health_pct, alerted.
--
-- Your own nodes (Lua):
--   rbs.bt_node{ id = "shout", kind = "action", title = "Shout", desc = "...",
--                params = { {name = "text", type = "string", default = "!"} },
--                run = function(self, p, ctx) ... return "success" end }
-- They show up in the editor's node menu. ctx.dt, ctx.mem (this node's own table, cleared
-- when the node stops running) and ctx.bb (helpers) are available.

local bt = {}
rbs.bt = bt

bt.library = {}  -- id -> metadata (what the editor lists)
bt.impl = {}     -- id -> run function
bt.reset = {}    -- id -> function called when a running node is interrupted
bt.order = {}
bt.global = {}   -- the shared blackboard ("global.<key>")

-- keys the engine maintains for every behavior monster (shown in the editor)
bt.builtin_keys = {
  {name = "target", type = "entity", help = "the enemy the monster is aware of (the player)"},
  {name = "can_see", type = "bool", help = "the target is in line of sight and within sight_range"},
  {name = "distance", type = "number", help = "distance to the player"},
  {name = "last_seen_pos", type = "vector", help = "where the target was last seen"},
  {name = "last_seen_time", type = "number", help = "game time when it was last seen"},
  {name = "heard_pos", type = "vector", help = "where a noise (shot) was last heard"},
  {name = "heard_time", type = "number", help = "game time of the last noise"},
  {name = "health_pct", type = "number", help = "health in percent (0-100)"},
  {name = "alerted", type = "bool", help = "has noticed the player (saw, heard or was hit)"},
}

local function refresh_library()
  local nodes = {}
  for _, id in ipairs(bt.order) do nodes[#nodes + 1] = bt.library[id] end
  local keys = {}
  for _, k in ipairs(bt.builtin_keys) do keys[#keys + 1] = k end
  rbs.bt_library = {nodes = nodes, keys = keys}
end

-- Declares a node type. kind: composite | decorator | condition | action
function rbs.bt_node(def)
  assert(type(def) == "table" and def.id, "rbs.bt_node needs an id")
  local kind = def.kind or "action"
  local meta = {id = def.id, kind = kind, title = def.title or def.id, desc = def.desc or "", params = def.params or {}}
  if not bt.library[def.id] then bt.order[#bt.order + 1] = def.id end
  bt.library[def.id] = meta
  bt.impl[def.id] = def.run
  bt.reset[def.id] = def.reset
  refresh_library()
end

-- ---------------------------------------------------------------------------------
-- blackboard
-- ---------------------------------------------------------------------------------
local function parse_value(v)
  if type(v) ~= "string" then return v end
  if v == "true" then return true end
  if v == "false" then return false end
  local n = tonumber(v)
  if n then return n end
  return v
end

local function bb_get(self, key)
  if type(key) ~= "string" or key == "" then return nil end
  local g = key:match("^global%.(.+)$")
  if g then return bt.global[g] end
  return self.bb and self.bb[key]
end

local function bb_set(self, key, value)
  if type(key) ~= "string" or key == "" then return end
  local g = key:match("^global%.(.+)$")
  if g then bt.global[g] = value else self.bb[key] = value end
end
bt.get, bt.set = bb_get, bb_set

-- an entity or a point out of a blackboard value
local function position_of(v)
  if type(v) ~= "table" and type(v) ~= "userdata" then return nil end
  if v.removed then return nil end
  if v.pos then return v.pos, v end
  if v.x and v.z then return v end
  return nil
end

local function compare(a, op, b)
  if op == "==" then return a == b end
  if op == "!=" then return a ~= b end
  if type(a) ~= "number" or type(b) ~= "number" then return false end
  if op == "<" then return a < b elseif op == "<=" then return a <= b
  elseif op == ">" then return a > b elseif op == ">=" then return a >= b end
  return false
end

-- ---------------------------------------------------------------------------------
-- loading trees
-- ---------------------------------------------------------------------------------
local cache = {}

local function fill_params(meta, params)
  local p = {}
  for _, def in ipairs(meta and meta.params or {}) do
    if params and params[def.name] ~= nil then p[def.name] = params[def.name] else p[def.name] = def.default end
  end
  for k, v in pairs(params or {}) do if p[k] == nil then p[k] = v end end
  return p
end

-- Turns a behavior definition (the table of a behaviors/*.json file) into nodes with their
-- children resolved. nil + message if it has no nodes or no root.
function bt.build(def, path)
  if type(def) ~= "table" or type(def.nodes) ~= "table" then return nil, "no nodes" end
  path = path or "(inline)"
  local by_id, list = {}, {}
  for _, n in ipairs(def.nodes) do
    if type(n) == "table" and n.id then
      local meta = bt.library[n.type]
      if not meta then rbs.log(path .. ": unknown node type '" .. tostring(n.type) .. "'") end
      local node = {id = n.id, type = n.type, meta = meta, kind = meta and meta.kind or "action", label = n.label, children = {},
                    params = fill_params(meta, n.params), child_ids = n.children or {}}
      by_id[n.id] = node
      list[#list + 1] = node
    end
  end
  local has_parent = {}
  for _, node in ipairs(list) do
    for _, cid in ipairs(node.child_ids) do
      local c = by_id[cid]
      if c then node.children[#node.children + 1] = c; has_parent[cid] = true end
    end
  end
  local root = by_id[def.root]
  if not root then
    for _, node in ipairs(list) do if not has_parent[node.id] then root = node; break end end
  end
  if not root then return nil, "no root node" end
  return {path = path, root = root, nodes = list, blackboard = type(def.blackboard) == "table" and def.blackboard or {}}
end

-- Reads behaviors/<name>.json; nil (with a log line) if it is missing or broken.
function bt.load(path)
  if cache[path] ~= nil then return cache[path] or nil end
  local text = rbs.read_file(path)
  if not text then
    rbs.log("Missing behavior: " .. path)
    cache[path] = false
    return nil
  end
  local def, err = rbs.json_decode(text)
  local tree
  if type(def) == "table" then tree, err = bt.build(def, path) end
  if not tree then
    rbs.log("Invalid behavior " .. path .. ": " .. tostring(err or "not a JSON object"))
    cache[path] = false
    return nil
  end
  cache[path] = tree
  return tree
end

-- ---------------------------------------------------------------------------------
-- running a tree
-- ---------------------------------------------------------------------------------
local function normalize(r)
  if r == true then return "success" end
  if r == false then return "failure" end
  if r == "success" or r == "failure" or r == "running" then return r end
  return "success"
end

local function node_mem(inst, node)
  local m = inst.mem[node.id]
  if not m then m = {}; inst.mem[node.id] = m end
  return m
end

local run_node

local function tick(inst, node, self, dt)
  inst.visited[node.id] = true
  local r = run_node(inst, node, self, dt)
  if r == "running" then inst.running[node.id] = true end
  inst.last[node.id] = r
  return r
end

-- composites and decorators
local flow = {}

flow.sequence = function(inst, node, self, dt)
  local m = node_mem(inst, node)
  local memory = node.params.memory ~= false
  local i = memory and m.i or 1
  if not i then i = 1 end
  if i > 1 and node.params.recheck ~= false then
    -- while a later child runs, the conditions before it must keep holding
    for j = 1, i - 1 do
      if node.children[j].kind == "condition" and tick(inst, node.children[j], self, dt) == "failure" then
        m.i = nil
        return "failure"
      end
    end
  end
  while i <= #node.children do
    local r = tick(inst, node.children[i], self, dt)
    if r == "running" then m.i = i; return "running" end
    if r == "failure" then m.i = nil; return "failure" end
    i = i + 1
  end
  m.i = nil
  return "success"
end

flow.selector = function(inst, node, self, dt)
  local m = node_mem(inst, node)
  local i = node.params.memory and m.i or 1
  if not i then i = 1 end
  while i <= #node.children do
    local r = tick(inst, node.children[i], self, dt)
    if r == "running" then m.i = i; return "running" end
    if r == "success" then m.i = nil; return "success" end
    i = i + 1
  end
  m.i = nil
  return "failure"
end

flow.parallel = function(inst, node, self, dt)
  local m = node_mem(inst, node)
  m.done = m.done or {}
  local any_success, all_success, any_failure, running = false, true, false, false
  for i, c in ipairs(node.children) do
    local r = m.done[i]
    if not r then
      r = tick(inst, c, self, dt)
      if r ~= "running" then m.done[i] = r end
    end
    if r == "running" then running = true; all_success = false
    elseif r == "success" then any_success = true
    else any_failure = true; all_success = false end
  end
  local result
  if node.params.policy == "one" then
    if any_success then result = "success" elseif not running then result = "failure" end
  else
    if any_failure then result = "failure" elseif all_success then result = "success" end
  end
  if result then m.done = nil; return result end
  return "running"
end

flow.inverter = function(inst, node, self, dt)
  local c = node.children[1]
  if not c then return "failure" end
  local r = tick(inst, c, self, dt)
  if r == "success" then return "failure" elseif r == "failure" then return "success" end
  return "running"
end

flow.succeeder = function(inst, node, self, dt)
  local c = node.children[1]
  if not c then return "success" end
  local r = tick(inst, c, self, dt)
  return r == "running" and "running" or "success"
end

flow.failer = function(inst, node, self, dt)
  local c = node.children[1]
  if not c then return "failure" end
  local r = tick(inst, c, self, dt)
  return r == "running" and "running" or "failure"
end

flow["repeat"] = function(inst, node, self, dt)
  local c = node.children[1]
  if not c then return "failure" end
  local m = node_mem(inst, node)
  local r = tick(inst, c, self, dt)
  if r == "failure" then m.n = nil; return "failure" end
  if r == "success" then
    m.n = (m.n or 0) + 1
    local count = node.params.count or 0
    if count > 0 and m.n >= count then m.n = nil; return "success" end
  end
  return "running"
end

flow.cooldown = function(inst, node, self, dt)
  local c = node.children[1]
  if not c then return "failure" end
  local m = node_mem(inst, node)
  local t = rbs.time()
  if m.ready and t < m.ready then return "failure" end
  local r = tick(inst, c, self, dt)
  if r ~= "running" then m.ready = t + (node.params.seconds or 1) end
  return r
end

flow.timeout = function(inst, node, self, dt)
  local c = node.children[1]
  if not c then return "failure" end
  local m = node_mem(inst, node)
  local t = rbs.time()
  m.start = m.start or t
  if t - m.start > (node.params.seconds or 5) then
    m.start = nil
    return "failure"
  end
  local r = tick(inst, c, self, dt)
  if r ~= "running" then m.start = nil end
  return r
end

run_node = function(inst, node, self, dt)
  local f = flow[node.type]
  if f then return normalize(f(inst, node, self, dt)) end
  local impl = bt.impl[node.type]
  if not impl then return "failure" end
  local ctx = {dt = dt, mem = node_mem(inst, node), node = node, tree = inst.tree}
  local ok, r = pcall(impl, self, node.params, ctx)
  if not ok then
    if not inst.warned then rbs.log("behavior node '" .. tostring(node.type) .. "': " .. tostring(r)); inst.warned = true end
    return "failure"
  end
  return normalize(r)
end

-- Gives `self` a tree and a blackboard (with the tree's own keys and their start values).
function bt.attach(self, tree)
  self.bb = {alerted = false, can_see = false, health_pct = 100}
  for _, k in ipairs(tree.blackboard) do
    if type(k) == "table" and k.name then self.bb[k.name] = parse_value(k.value) end
  end
  self.bt = {tree = tree, mem = {}, visited = {}, running = {}, last = {}, tick = 0, perceive_at = math.random() * 0.15}
end

-- Sets up the tree and the blackboard of a monster.
function bt.init(self)
  local path = self.behavior
  local tree = path and path ~= "" and bt.load(path) or nil
  if not tree then
    -- a missing or broken file must not freeze the monster: fall back to the built-in chase AI
    self.bb = {}
    self.ai = "chase"
    return
  end
  bt.attach(self, tree)
end

-- removes a node type (tests register throwaway ones)
function bt.unregister(id)
  bt.library[id], bt.impl[id], bt.reset[id] = nil, nil, nil
  for i, v in ipairs(bt.order) do if v == id then table.remove(bt.order, i); break end end
  refresh_library()
end

-- Perception: what the monster sees and hears, kept in the blackboard.
function bt.perceive(self, dt)
  local inst = self.bt
  local t = rbs.time()
  self.bb.health_pct = (self.health / math.max(1, self.max_health or self.health)) * 100
  if t < inst.perceive_at then return end
  inst.perceive_at = t + 0.12
  local p = rbs.player()
  if not p or p.dead then
    self.bb.can_see = false
    self.bb.target = nil
    return
  end
  local d = self:distance(p)
  self.bb.distance = d
  local sees = d < (self.sight_range or 1800) and rbs.visible(self, p)
  self.bb.can_see = sees and true or false
  if sees then
    self.bb.target = p
    self.bb.alerted = true
    self.bb.last_seen_pos = p.pos:copy()
    self.bb.last_seen_time = t
  end
end

-- The monster learns where the enemy is (damage, trigger, shots nearby).
function bt.alert(self, enemy)
  if not self.bb then return end
  self.bb.alerted = true
  if enemy and not enemy.removed then
    self.bb.target = enemy
    self.bb.last_seen_pos = enemy.pos:copy()
    self.bb.last_seen_time = rbs.time()
  end
end

-- Runs one tick of the monster's tree (call every frame).
function bt.update(self, dt, no_perception)
  local inst = self.bt
  if not inst then return end
  if not no_perception then bt.perceive(self, dt) end
  inst.tick = inst.tick + 1
  inst.visited, inst.running = {}, {}
  local prev = inst.last
  inst.last = {}
  tick(inst, inst.tree.root, self, dt)
  -- nodes that were running last time but were not reached now were interrupted: clean up
  -- (nodes that merely finished keep their memory, e.g. a cooldown's timestamp)
  for _, node in ipairs(inst.tree.nodes) do
    if prev[node.id] == "running" and not inst.visited[node.id] then
      local reset = bt.reset[node.type]
      if reset then pcall(reset, self, node.params, {mem = inst.mem[node.id] or {}}) end
      inst.mem[node.id] = nil
    end
  end
  -- debug: path from the root to the running leaf
  inst.path = {}
  local node = inst.tree.root
  while node do
    inst.path[#inst.path + 1] = node.label or node.meta and node.meta.title or node.type
    local nxt
    for _, c in ipairs(node.children) do if inst.running[c.id] then nxt = c; break end end
    node = nxt
  end
  if rbs.cvar("bt_debug") == 2 then
    -- bt_debug 2: every change of the active branch goes to the console
    local d = table.concat(inst.path, " > ")
    if d ~= inst.logged then
      inst.logged = d
      rbs.log(string.format("[bt %.2f] %s #%s: %s", rbs.time(), self.classname, tostring(self.id), d))
    end
  end
end

-- "Selector > Sequence > Attack" for the debug display
function bt.describe(self)
  local inst = self.bt
  if not inst or not inst.path then return "" end
  return table.concat(inst.path, " > ")
end

-- A noise at `pos` (the player firing): monsters within hearing range notice it.
function rbs.noise(source, radius)
  if not source or not source.pos then return end
  for _, m in ipairs(rbs.find_radius(source.pos, radius or 1400)) do
    if m.monster and not m.dead and m.bb then
      m.bb.heard_pos = source.pos:copy()
      m.bb.heard_time = rbs.time()
      m.bb.alerted = true
      if rbs.cvar("bt_debug") == 2 then rbs.log(string.format("[bt %.2f] %s #%s heard a noise", rbs.time(), m.classname, tostring(m.id))) end
    end
  end
end

-- Self-test of the node semantics (console: bt_selftest, needs a running map).
function rbs.bt_selftest()
  local ok, run = pcall(require, "tests.behavior_test")
  if not ok then
    rbs.log("BT SELFTEST FAIL: " .. tostring(run))
    return
  end
  run()
end

-- Draws the state of nearby behavior monsters (cvar bt_debug 1).
function rbs.draw_bt_debug(w, h)
  if rbs.cvar("bt_debug") == 0 then return end
  local p = rbs.player()
  if not p then return end
  local y = 70
  rbs.draw_text("BEHAVIOR TREES", 12, y, 14, {r = 255, g = 200, b = 90, a = 255}, "left", true)
  y = y + 18
  for _, m in ipairs(rbs.find_radius(p.pos, 1800)) do
    if m.monster and not m.dead and m.bt then
      local line = string.format("%s #%s  %s  [%s]", m.classname, tostring(m.id), m.state, bt.describe(m))
      rbs.draw_text(line, 12, y, 13, {r = 230, g = 235, b = 245, a = 255}, "left", true)
      y = y + 16
      if y > h - 60 then break end
    end
  end
end

-- ---------------------------------------------------------------------------------
-- built-in nodes
-- ---------------------------------------------------------------------------------
local function composite(id, title, desc, params) rbs.bt_node({id = id, kind = "composite", title = title, desc = desc, params = params}) end
local function decorator(id, title, desc, params) rbs.bt_node({id = id, kind = "decorator", title = title, desc = desc, params = params}) end

composite("sequence", "Sequence", "Runs its children in order; fails as soon as one fails, succeeds when all did.", {
  {name = "memory", type = "bool", default = true, help = "continue with the child that was running instead of starting over"},
  {name = "recheck", type = "bool", default = true, help = "while a later child runs, keep checking the conditions before it"},
})
composite("selector", "Selector", "Tries its children from the top; the first that works wins. Re-evaluated every tick, so a higher child interrupts a lower one.", {
  {name = "memory", type = "bool", default = false, help = "keep running the same child instead of re-checking the ones above it"},
})
composite("parallel", "Parallel", "Runs all children at the same time.", {
  {name = "policy", type = "enum", values = {"all", "one"}, default = "all", help = "all: succeeds when every child did, fails when one fails | one: succeeds as soon as one succeeds"},
})
decorator("inverter", "Inverter", "Success becomes failure and the other way round.", {})
decorator("succeeder", "Succeeder", "Always succeeds once its child is done.", {})
decorator("failer", "Failer", "Always fails once its child is done.", {})
decorator("repeat", "Repeat", "Runs its child again and again.", {
  {name = "count", type = "number", default = 0, help = "how many times (0 = forever); stops with failure if the child fails"},
})
decorator("cooldown", "Cooldown", "After its child is done it fails for a while (so the action cannot be used again too soon).", {
  {name = "seconds", type = "number", default = 2},
})
decorator("timeout", "Timeout", "Fails when its child runs longer than this.", {
  {name = "seconds", type = "number", default = 5},
})

local OPS = {"<", "<=", ">", ">=", "==", "!="}

local function cond(id, title, desc, params, fn) rbs.bt_node({id = id, kind = "condition", title = title, desc = desc, params = params, run = fn}) end
local function action(id, title, desc, params, fn, reset) rbs.bt_node({id = id, kind = "action", title = title, desc = desc, params = params, run = fn, reset = reset}) end

cond("can_see", "Can see", "The monster has a line of sight to the entity in the key.", {
  {name = "target", type = "key", default = "target"},
}, function(self, p)
  local pos, ent = position_of(bb_get(self, p.target))
  if not ent then return false end
  return rbs.visible(self, ent) and self:distance(ent) < (self.sight_range or 1800)
end)

cond("has_target", "Has target", "The key holds an entity (by default the enemy it knows about).", {
  {name = "target", type = "key", default = "target"},
}, function(self, p)
  local _, ent = position_of(bb_get(self, p.target))
  return ent ~= nil and not ent.dead
end)

cond("distance", "Distance", "Compares the distance to an entity or point.", {
  {name = "target", type = "key", default = "target"},
  {name = "op", type = "enum", values = OPS, default = "<"},
  {name = "value", type = "number", default = 400},
}, function(self, p)
  local pos = position_of(bb_get(self, p.target))
  if not pos then return false end
  return compare((pos - self.pos):len(), p.op, p.value)
end)

cond("health", "Health", "Compares the monster's health in percent.", {
  {name = "op", type = "enum", values = OPS, default = "<"},
  {name = "percent", type = "number", default = 30},
}, function(self, p)
  return compare((self.health / math.max(1, self.max_health or self.health)) * 100, p.op, p.percent)
end)

cond("compare", "Compare", "Compares a blackboard value with a number, true/false or text.", {
  {name = "key", type = "key", default = ""},
  {name = "op", type = "enum", values = OPS, default = "=="},
  {name = "value", type = "string", default = "true"},
}, function(self, p)
  return compare(bb_get(self, p.key), p.op, parse_value(p.value))
end)

cond("chance", "Chance", "Succeeds with this probability each time it is checked.", {
  {name = "percent", type = "number", default = 50},
}, function(self, p, ctx)
  -- decided once while the parent keeps asking, so a sequence does not flicker
  local m = ctx.mem
  local t = rbs.time()
  if m.until_t and t < m.until_t then return m.result end
  m.result = math.random() * 100 < p.percent
  m.until_t = t + 0.5
  return m.result
end)

cond("heard", "Heard something", "A noise (the player firing) was heard recently.", {
  {name = "seconds", type = "number", default = 4, help = "how recent"},
}, function(self, p)
  local ht = self.bb.heard_time
  return ht ~= nil and rbs.time() - ht <= p.seconds
end)

cond("lost_target", "Lost target", "It saw the enemy before but has not seen it for a while.", {
  {name = "seconds", type = "number", default = 1},
}, function(self, p)
  local lt = self.bb.last_seen_time
  return lt ~= nil and not self.bb.can_see and rbs.time() - lt >= p.seconds
end)

local function stop(self)
  self:walk(vec(0, 0, 0), 0)
end

action("wait", "Wait", "Does nothing for a while.", {
  {name = "seconds", type = "number", default = 1},
  {name = "extra", type = "number", default = 0, help = "plus a random 0..extra seconds"},
}, function(self, p, ctx)
  local m = ctx.mem
  local t = rbs.time()
  if not m.done_at then m.done_at = t + p.seconds + math.random() * (p.extra or 0) end
  stop(self)
  self:play("idle", true)
  if t >= m.done_at then
    ctx.mem.done_at = nil
    return "success"
  end
  return "running"
end)

action("idle", "Idle", "Stands still (one tick).", {}, function(self)
  stop(self)
  self:play("idle", true)
  return "success"
end)

action("move_to", "Move to", "Walks to an entity or point from the blackboard.", {
  {name = "target", type = "key", default = "target"},
  {name = "range", type = "number", default = 96, help = "success when this close"},
  {name = "speed", type = "number", default = 1, help = "multiplier of the monster's speed"},
  {name = "timeout", type = "number", default = 0, help = "give up (failure) after this many seconds, 0 = never"},
}, function(self, p, ctx)
  local pos = position_of(bb_get(self, p.target))
  if not pos then return "failure" end
  local flat = (pos - self.pos):flat()
  if flat:len() <= p.range then
    stop(self)
    ctx.mem.start = nil
    return "success"
  end
  local t = rbs.time()
  ctx.mem.start = ctx.mem.start or t
  if p.timeout > 0 and t - ctx.mem.start > p.timeout then
    ctx.mem.start = nil
    return "failure"
  end
  self:move(ctx.dt, flat:normalize(), self.speed * p.speed, true)
  return "running"
end)

action("flee_from", "Flee from", "Runs away from an entity or point.", {
  {name = "target", type = "key", default = "target"},
  {name = "seconds", type = "number", default = 2.5},
  {name = "speed", type = "number", default = 1.2},
}, function(self, p, ctx)
  local pos = position_of(bb_get(self, p.target))
  if not pos then return "failure" end
  local m = ctx.mem
  local t = rbs.time()
  m.until_t = m.until_t or (t + p.seconds)
  if t >= m.until_t then
    m.until_t = nil
    stop(self)
    return "success"
  end
  self:move(ctx.dt, (self.pos - pos):flat():normalize(), self.speed * p.speed, true)
  return "running"
end, function(self) self:walk(vec(0, 0, 0), 0) end)

action("strafe", "Strafe", "Side-steps around the target while facing it.", {
  {name = "target", type = "key", default = "target"},
  {name = "seconds", type = "number", default = 1},
  {name = "speed", type = "number", default = 0.7},
}, function(self, p, ctx)
  local pos, ent = position_of(bb_get(self, p.target))
  if not pos then return "failure" end
  local m = ctx.mem
  local t = rbs.time()
  if not m.until_t then
    m.until_t = t + p.seconds
    m.dir = math.random() < 0.5 and -1 or 1
  end
  if t >= m.until_t then
    m.until_t = nil
    stop(self)
    return "success"
  end
  local dir = (pos - self.pos):flat():normalize()
  self:move(ctx.dt, dir:cross(vec(0, 1, 0)):normalize() * m.dir, self.speed * p.speed, false, ent)
  return "running"
end, function(self) self:walk(vec(0, 0, 0), 0) end)

action("face", "Face", "Turns towards an entity or point.", {
  {name = "target", type = "key", default = "target"},
}, function(self, p, ctx)
  local pos = position_of(bb_get(self, p.target))
  if not pos then return "failure" end
  stop(self)
  self:face(pos, self.turn_speed, ctx.dt)
  local to = (pos - self.pos):flat():normalize()
  local fwd = self:forward():flat():normalize()
  if to.x * fwd.x + to.z * fwd.z > 0.97 then return "success" end
  return "running"
end)

action("attack", "Attack", "Uses the monster's attack (attack_type, damage... from its properties). Fails while it is cooling down or the target is not in reach.", {
  {name = "target", type = "key", default = "target"},
}, function(self, p, ctx)
  if self.state == "attack" then return "running" end
  local _, ent = position_of(bb_get(self, p.target))
  if not ent or ent.dead then return "failure" end
  local t = rbs.time()
  if t < self.next_attack or self:distance(ent) >= self:reach() or not rbs.visible(self, ent) then return "failure" end
  self:start_attack()
  return "running"
end)

action("patrol", "Patrol", "Walks the path_corner route from the monster's patrol_path property.", {}, function(self, p, ctx)
  if (self.patrol_path or "") == "" then return "failure" end
  self:patrol(ctx.dt)
  return "running"
end)

action("set_bb", "Set value", "Writes a blackboard value (a number, true/false or text).", {
  {name = "key", type = "key", default = ""},
  {name = "value", type = "string", default = "true"},
}, function(self, p)
  if p.key == "" then return "failure" end
  bb_set(self, p.key, parse_value(p.value))
  return "success"
end)

action("trigger_anim", "Animation trigger", "Fires a trigger of the monster's animator (or plays that animation).", {
  {name = "name", type = "string", default = "attack"},
}, function(self, p)
  self:play(p.name, false)
  return "success"
end)

action("sound", "Play sound", "Plays a sound at the monster.", {
  {name = "path", type = "string", default = "sounds/grunt_sight.wav"},
  {name = "volume", type = "number", default = 1},
}, function(self, p)
  if p.path ~= "" then rbs.sound(p.path, self.pos, p.volume, 0.9 + math.random() * 0.2) end
  return "success"
end)

action("alert_neighbors", "Alert neighbors", "Tells nearby monsters where the target is.", {
  {name = "radius", type = "number", default = 500},
}, function(self, p)
  local target = self.bb.target
  for _, m in ipairs(rbs.find_radius(self.pos, p.radius)) do
    if m.monster and not m.dead and m ~= self and m.bb then bt.alert(m, target) end
  end
  return "success"
end)

action("log", "Log", "Writes a line to the console (for debugging a tree).", {
  {name = "text", type = "string", default = "reached"},
}, function(self, p)
  rbs.log(string.format("[%s #%s] %s", self.classname, tostring(self.id), p.text))
  return "success"
end)

refresh_library()
