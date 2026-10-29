-- Quests and dialogues as graphs (quests/<name>.json, dialogues/<name>.json), made in the
-- editor (Project > Quests / Dialogues). See docs/QUESTS.md.
--
-- A graph is a set of nodes; each node has output ports ("next", "yes"/"no", one per
-- choice...) linked to other nodes. When a node is entered it either finishes at once and
-- passes the flow on through a port, or waits (an objective waits for kills or for a target
-- to be used, "say" waits for a keypress...). A quest can have several nodes waiting at the
-- same time (links from one port to several nodes, joined again by a Join node).
--
--   start         the flow begins here
--   objectives    kill, pickup, use (target), var_wait      (quests)
--   logic         branch, set_var, join, delay, chance, end
--   actions       message, give, fire_targets, spawn, sound, start_quest, change_map,
--                 dialogue, log
--   dialogue      say, choice                               (dialogues)
--
-- Variables (flow.get / flow.set) are shared by all quests and dialogues and kept across
-- maps. The engine feeds these events to the running quests: kill (class), pickup (class),
-- fire (targetname), var (key), quest (name), dialogue_end (name).
--
-- Your own nodes:
--   rbs.flow_node{ id = "my_node", category = "action", title = "...", desc = "...",
--                  scopes = {"quest", "dialogue"},
--                  params = { {name = "text", type = "string", default = ""} },
--                  enter = function(inst, node, st) ... return "next" end }   -- nil = wait
-- Optional: event(inst, node, st, kind, a, b), update(inst, node, st, dt), hud(inst, node, st)
-- return a port name to finish the node.

local flow = {}
rbs.flow = flow

flow.library = {}  -- id -> metadata (listed in the editor)
flow.impl = {}     -- id -> {enter, event, update, hud, join}
flow.order = {}
flow.vars = {}     -- variables shared by every quest and dialogue
flow.quests = {}   -- name -> instance (running or finished)
flow.dialogue = nil
flow.no_save = false
local graphs = {}
local dirty = false

-- ---------------------------------------------------------------------------------
-- node library
-- ---------------------------------------------------------------------------------
local function refresh_library()
  local nodes = {}
  for _, id in ipairs(flow.order) do nodes[#nodes + 1] = flow.library[id] end
  rbs.flow_library = {nodes = nodes}
end

-- category: flow | objective | action | dialogue. ports: list of names (default {"next"}),
-- {} for a node that ends the flow, "options" for a choice (one port per option).
function rbs.flow_node(def)
  assert(type(def) == "table" and def.id, "rbs.flow_node needs an id")
  local meta = {id = def.id, category = def.category or "action", title = def.title or def.id, desc = def.desc or "",
                params = def.params or {}, ports = def.ports or {"next"}, scopes = def.scopes or {"quest", "dialogue"}}
  if not flow.library[def.id] then flow.order[#flow.order + 1] = def.id end
  flow.library[def.id] = meta
  flow.impl[def.id] = {enter = def.enter, event = def.event, update = def.update, hud = def.hud, join = def.join}
  refresh_library()
end

function flow.unregister(id)
  flow.library[id], flow.impl[id] = nil, nil
  for i, v in ipairs(flow.order) do if v == id then table.remove(flow.order, i); break end end
  refresh_library()
end

-- ---------------------------------------------------------------------------------
-- variables
-- ---------------------------------------------------------------------------------
local function parse_value(v)
  if type(v) ~= "string" then return v end
  if v == "true" then return true end
  if v == "false" then return false end
  local n = tonumber(v)
  if n then return n end
  return v
end
flow.parse_value = parse_value

local function compare(a, op, b)
  -- a variable nobody set equals 0 / false / ""
  if a == nil then
    if type(b) == "number" then a = 0 elseif type(b) == "boolean" then a = false else a = "" end
  end
  if op == "==" then return a == b end
  if op == "!=" then return a ~= b end
  if type(a) ~= "number" or type(b) ~= "number" then return false end
  if op == "<" then return a < b elseif op == "<=" then return a <= b
  elseif op == ">" then return a > b elseif op == ">=" then return a >= b end
  return false
end

function flow.get(key) return flow.vars[key] end

local function save() dirty = true end

function flow.set(key, value)
  if type(key) ~= "string" or key == "" then return end
  if flow.vars[key] == value then return end
  flow.vars[key] = value
  save()
  flow.event("var", key, value)
end

-- "{gold}" in a text becomes the value of the variable
function flow.format(text)
  return (tostring(text or ""):gsub("{([%w_%.]+)}", function(k)
    local v = flow.vars[k]
    if v == nil then return "" end
    return tostring(v)
  end))
end

local function say_text(t) return flow.format(rbs.tr(t or "")) end
flow.say_text = say_text

-- ---------------------------------------------------------------------------------
-- compiling a graph
-- ---------------------------------------------------------------------------------
local function fill_params(meta, params)
  local p = {}
  for _, def in ipairs(meta and meta.params or {}) do
    if params and params[def.name] ~= nil then p[def.name] = params[def.name] else p[def.name] = def.default end
  end
  for k, v in pairs(params or {}) do if p[k] == nil then p[k] = v end end
  return p
end

-- Turns a definition (the table of a quests/*.json or dialogues/*.json file) into nodes
-- with their links resolved. nil + message when there is no start.
function flow.build(def, kind, name)
  if type(def) ~= "table" or type(def.nodes) ~= "table" then return nil, "no nodes" end
  local by_id, list = {}, {}
  for _, n in ipairs(def.nodes) do
    if type(n) == "table" and n.id then
      local meta = flow.library[n.type]
      if not meta then rbs.log((name or "(inline)") .. ": unknown node type '" .. tostring(n.type) .. "'") end
      local node = {id = n.id, type = n.type, meta = meta, params = fill_params(meta, n.params), out = {}, incoming = 0, label = n.label}
      by_id[n.id] = node
      list[#list + 1] = node
    end
  end
  for _, n in ipairs(def.nodes) do
    local node = type(n) == "table" and by_id[n.id]
    if node and type(n.out) == "table" then
      for port, targets in pairs(n.out) do
        node.out[port] = {}
        if type(targets) == "table" then
          for _, tid in ipairs(targets) do
            local t = by_id[tid]
            if t then
              node.out[port][#node.out[port] + 1] = t
              t.incoming = t.incoming + 1
            end
          end
        end
      end
    end
  end
  local starts = {}
  for _, node in ipairs(list) do if node.type == "start" then starts[#starts + 1] = node end end
  if #starts == 0 then return nil, "no start node" end
  return {kind = kind, name = name, title = type(def.title) == "string" and def.title or name, nodes = list, starts = starts,
          vars = type(def.vars) == "table" and def.vars or {}}
end

local function path_of(kind, name)
  if name:find("/", 1, true) then return name end
  return (kind == "quest" and "quests/" or "dialogues/") .. name .. (name:match("%.json$") and "" or ".json")
end

local function short_name(name) return (name:match("([^/]+)%.json$")) or name end

function flow.load(kind, name)
  name = short_name(name)
  local key = kind .. ":" .. name
  if graphs[key] ~= nil then return graphs[key] or nil end
  local path = path_of(kind, name)
  local text = rbs.read_file(path)
  if not text then
    rbs.log("Missing " .. kind .. ": " .. path)
    graphs[key] = false
    return nil
  end
  local def, err = rbs.json_decode(text)
  local g
  if type(def) == "table" then g, err = flow.build(def, kind, name) end
  if not g then
    rbs.log("Invalid " .. kind .. " " .. path .. ": " .. tostring(err or "not a JSON object"))
    graphs[key] = false
    return nil
  end
  graphs[key] = g
  return g
end

-- ---------------------------------------------------------------------------------
-- running a graph
-- ---------------------------------------------------------------------------------
local enter, leave

local function active_count(inst)
  local n = 0
  for _ in pairs(inst.active) do n = n + 1 end
  return n
end

local function finish(inst, result)
  if inst.done then return end
  inst.done, inst.result = true, result or "success"
  inst.active = {}
  if inst.kind == "quest" then
    local title = say_text(inst.graph.title)
    if inst.result == "success" then
      rbs.message("Quest completed: " .. title, 3)
      rbs.sound("sounds/objective.wav", nil, 0.8)
    else
      rbs.message("Quest failed: " .. title, 3)
    end
    save()
    flow.event("quest", inst.name, inst.result)
  else
    if flow.dialogue == inst then
      flow.dialogue = nil
      rbs.freeze(false)
      if flow.view then flow.view.on_finish(inst) end
    end
    flow.event("dialogue_end", inst.name)
  end
end

local function check_idle(inst)
  -- nothing is waiting any more: the flow ran out of links
  if not inst.done and active_count(inst) == 0 then finish(inst, "success") end
end

enter = function(inst, node)
  if inst.done then return end
  local impl = flow.impl[node.type]
  if not impl or not impl.enter then
    rbs.log(inst.name .. ": node '" .. tostring(node.type) .. "' does nothing")
    return
  end
  local st = inst.active[node.id]
  if st and not impl.join then return end  -- already waiting here
  if rbs.cvar("quest_debug") ~= 0 then rbs.log(string.format("[quest] %s -> %s (%s)", inst.name, node.id, node.type)) end
  if not st then st = {}; inst.active[node.id] = st end
  local ok, port = pcall(impl.enter, inst, node, st)
  if not ok then
    rbs.log(inst.name .. " / " .. tostring(node.type) .. ": " .. tostring(port))
    inst.active[node.id] = nil
    return
  end
  if port ~= nil then leave(inst, node, port) end
  save()
end

leave = function(inst, node, port)
  if inst.done then return end
  inst.active[node.id] = nil
  for _, target in ipairs(node.out[port] or {}) do enter(inst, target) end
  check_idle(inst)
end

flow.enter, flow.leave = enter, leave  -- scripts/dialogue.lua moves its dialogue through the graph with them

-- Tells the running quests something happened.
function flow.event(kind, a, b)
  if rbs.cvar("quest_debug") ~= 0 and kind ~= "var" then rbs.log(string.format("[quest] event %s %s", tostring(kind), tostring(a))) end
  for _, inst in pairs(flow.quests) do
    if not inst.done then
      local ids = {}
      for id in pairs(inst.active) do ids[#ids + 1] = id end
      for _, id in ipairs(ids) do
        local st = inst.active[id]
        local node = st and inst.nodes[id]
        local impl = node and flow.impl[node.type]
        if impl and impl.event then
          local ok, port = pcall(impl.event, inst, node, st, kind, a, b)
          if not ok then rbs.log(inst.name .. " / " .. tostring(node.type) .. " event: " .. tostring(port))
          elseif port ~= nil then leave(inst, node, port) end
        end
      end
    end
  end
end

local function new_instance(kind, graph, name)
  local inst = {kind = kind, name = name, graph = graph, active = {}, done = false, nodes = {}}
  for _, n in ipairs(graph.nodes) do inst.nodes[n.id] = n end
  return inst
end

-- ---------------------------------------------------------------------------------
-- quests
-- ---------------------------------------------------------------------------------
function flow.start_quest(name)
  name = short_name(name)
  if flow.quests[name] then return false end  -- running or done already
  local g = flow.load("quest", name)
  if not g then return false end
  return flow.run_quest(g, name)
end

-- starts a compiled quest graph
function flow.run_quest(g, name)
  if flow.quests[name] then return false end
  for _, v in ipairs(g.vars) do
    if type(v) == "table" and v.name and flow.vars[v.name] == nil then flow.vars[v.name] = parse_value(v.value) end
  end
  local inst = new_instance("quest", g, name)
  flow.quests[name] = inst
  rbs.message("New quest: " .. say_text(g.title), 3)
  rbs.sound("sounds/objective.wav", nil, 0.8)
  save()
  for _, s in ipairs(g.starts) do enter(inst, s) end
  check_idle(inst)
  return true
end

function flow.quest_state(name)
  local q = flow.quests[short_name(name)]
  if not q then return "none" end
  if not q.done then return "active" end
  return q.result == "fail" and "failed" or "done"
end

-- ---------------------------------------------------------------------------------
-- dialogues
-- ---------------------------------------------------------------------------------
function flow.start_dialogue(name, replace, actor)
  name = short_name(name)
  if flow.dialogue and not flow.dialogue.done and not replace then return false end
  local g = flow.load("dialogue", name)
  if not g then return false end
  return flow.run_dialogue(g, name, replace, actor)
end

-- starts a compiled dialogue graph
function flow.run_dialogue(g, name, replace, actor)
  if flow.dialogue and not flow.dialogue.done then
    if not replace then return false end
    local old = flow.dialogue
    flow.dialogue = nil
    old.done = true
  end
  for _, v in ipairs(g.vars) do
    if type(v) == "table" and v.name and flow.vars[v.name] == nil then flow.vars[v.name] = parse_value(v.value) end
  end
  local inst = new_instance("dialogue", g, name)
  inst.actor = actor  -- the character that started it (an npc), the default speaker
  flow.dialogue = inst
  inst.block_until = rbs.time() + 0.25  -- the keypress that started it must not skip the first line
  rbs.freeze(true)
  for _, s in ipairs(g.starts) do enter(inst, s) end
  check_idle(inst)
  return true
end

-- flow.advance() / flow.pick(i) (the player pressed "continue" / picked an answer) live in
-- scripts/dialogue.lua with the rest of the dialogue presentation

-- ---------------------------------------------------------------------------------
-- saving across maps
-- ---------------------------------------------------------------------------------
function flow.save()
  if flow.no_save then return end
  local data = {vars = flow.vars, quests = {}}
  for name, q in pairs(flow.quests) do
    data.quests[name] = {done = q.done, result = q.result, active = q.active}
  end
  pcall(rbs.persist_set, "flow", rbs.json_encode(data))
end

function flow.restore()
  local ok, text = pcall(rbs.persist_get, "flow")
  if not ok or type(text) ~= "string" then return end
  local data = rbs.json_decode(text)
  if type(data) ~= "table" then return end
  flow.vars = type(data.vars) == "table" and data.vars or {}
  for name, q in pairs(type(data.quests) == "table" and data.quests or {}) do
    local g = flow.load("quest", name)
    if g then
      local inst = new_instance("quest", g, name)
      inst.done, inst.result = q.done and true or false, q.result
      for id, st in pairs(type(q.active) == "table" and q.active or {}) do
        if inst.nodes[id] then inst.active[id] = st end
      end
      flow.quests[name] = inst
      if rbs.cvar("quest_debug") ~= 0 then rbs.log("[quest] restored " .. name .. (inst.done and " (finished)" or "")) end
    end
  end
end

-- forgets everything (a new game)
function flow.reset()
  flow.vars, flow.quests, flow.dialogue = {}, {}, nil
  save()
end

-- ---------------------------------------------------------------------------------
-- hooks from the engine
-- ---------------------------------------------------------------------------------
local function is_a(classname, base)
  if base == nil or base == "" then return true end
  local c = classname
  for _ = 1, 32 do
    if c == base then return true end
    local def = rbs.classes and rbs.classes[c]
    c = def and def.base
    if not c then return false end
  end
  return false
end
flow.is_a = is_a

function rbs.on_kill(ent) flow.event("kill", ent.classname, ent) end

-- a target was fired (a button, a trigger, a script): "use" objectives listen for it
local orig_fire = rbs._fire
if orig_fire then
  rbs._fire = function(target, activator)
    orig_fire(target, activator)
    flow.event("fire", target)
  end
end

local poll = 0
function rbs._update(dt)
  for _, inst in pairs(flow.quests) do
    if not inst.done then
      local ids = {}
      for id in pairs(inst.active) do ids[#ids + 1] = id end
      for _, id in ipairs(ids) do
        local st, node = inst.active[id], inst.nodes[id]
        local impl = st and node and flow.impl[node.type]
        if impl and impl.update then
          local ok, port = pcall(impl.update, inst, node, st, dt)
          if not ok then rbs.log(inst.name .. " / " .. tostring(node.type) .. " update: " .. tostring(port))
          elseif port ~= nil then leave(inst, node, port) end
        end
      end
    end
  end
  local d = flow.dialogue
  if d and not d.done then
    local ids = {}
    for id in pairs(d.active) do ids[#ids + 1] = id end
    for _, id in ipairs(ids) do
      local st, node = d.active[id], d.nodes[id]
      local impl = st and node and flow.impl[node.type]
      if impl and impl.update then
        local port = impl.update(d, node, st, dt)
        if port ~= nil then leave(d, node, port) end
      end
    end
    if flow.view then flow.view.update(d, dt) end
  end
  poll = poll + dt
  if dirty and poll > 0.2 then
    poll = 0
    dirty = false
    flow.save()
  end
end

-- ---------------------------------------------------------------------------------
-- HUD: active quests and the dialogue box
-- ---------------------------------------------------------------------------------
local GOLD = {r = 255, g = 214, b = 120, a = 255}
local WHITE = {r = 235, g = 240, b = 248, a = 255}
local DIM = {r = 170, g = 178, b = 192, a = 255}

local function objective_lines(inst)
  local out = {}
  local ids = {}
  for id in pairs(inst.active) do ids[#ids + 1] = id end
  table.sort(ids)
  for _, id in ipairs(ids) do
    local node, st = inst.nodes[id], inst.active[id]
    local impl = node and flow.impl[node.type]
    if impl and impl.hud then
      local ok, text = pcall(impl.hud, inst, node, st)
      if ok and type(text) == "string" and text ~= "" then out[#out + 1] = text end
    end
  end
  return out
end

local function draw_quests(w, h)
  local s = h / 900
  local x, y = w - 14 * s, 14 * s
  local names = {}
  for name, q in pairs(flow.quests) do if not q.done then names[#names + 1] = name end end
  table.sort(names)
  for _, name in ipairs(names) do
    local q = flow.quests[name]
    local lines = objective_lines(q)
    if #lines > 0 then
      rbs.draw_text(say_text(q.graph.title), x, y, math.floor(16 * s), GOLD, "right", true, "ui_bold")
      y = y + 22 * s
      for _, l in ipairs(lines) do
        rbs.draw_text("- " .. l, x, y, math.floor(14 * s), WHITE, "right", true, "ui")
        y = y + 19 * s
      end
      y = y + 8 * s
    end
  end
end

function rbs.draw_story(w, h)
  if not flow.cinematic then draw_quests(w, h) end
  if flow.view then flow.view.draw_game(w, h) end
end

-- ---------------------------------------------------------------------------------
-- built-in nodes
-- ---------------------------------------------------------------------------------
local OPS = {"==", "!=", "<", "<=", ">", ">="}
local BOTH = {"quest", "dialogue"}
local QUEST = {"quest"}
local DIALOGUE = {"dialogue"}

local function node(def) rbs.flow_node(def) end

node({id = "start", category = "flow", title = "Start", desc = "The flow begins here.", scopes = BOTH, params = {},
      enter = function() return "next" end})

node({id = "end", category = "flow", title = "End", ports = {}, scopes = BOTH,
      desc = "Ends the quest (success or failure) or the dialogue.",
      params = {{name = "result", type = "enum", values = {"success", "fail"}, default = "success", help = "quests only"}},
      enter = function(inst, n, st)
        if inst.kind == "quest" then finish(inst, n.params.result == "fail" and "fail" or "success")
        else finish(inst, "success") end
      end})

local function evaluate(p)
  local c = p.check
  if c == "quest active" then return flow.quest_state(p.key) == "active" end
  if c == "quest done" then return flow.quest_state(p.key) == "done" end
  if c == "quest failed" then return flow.quest_state(p.key) == "failed" end
  if c == "has key" then return rbs.has_key(p.key) and true or false end
  if c == "has weapon" then return rbs.has_weapon(p.key) and true or false end
  if c == "health %" then
    local pl = rbs.player()
    if not pl then return false end
    return compare(pl.health / math.max(1, pl.max_health or 100) * 100, p.op, parse_value(p.value))
  end
  return compare(flow.vars[p.key], p.op, parse_value(p.value))
end
flow.evaluate = evaluate

node({id = "branch", category = "flow", title = "Branch", ports = {"yes", "no"}, scopes = BOTH,
      desc = "Goes to yes or no depending on a condition.",
      params = {
        {name = "check", type = "enum", values = {"variable", "quest active", "quest done", "quest failed", "has key", "has weapon", "health %"}, default = "variable"},
        {name = "key", type = "var", default = "", help = "variable name, quest name, key colour or weapon id (by the check)"},
        {name = "op", type = "enum", values = OPS, default = "=="},
        {name = "value", type = "string", default = "true"},
      },
      enter = function(inst, n) return evaluate(n.params) and "yes" or "no" end})

node({id = "chance", category = "flow", title = "Chance", ports = {"yes", "no"}, scopes = BOTH,
      desc = "Goes to yes with this probability.",
      params = {{name = "percent", type = "number", default = 50}},
      enter = function(inst, n) return math.random() * 100 < n.params.percent and "yes" or "no" end})

node({id = "set_var", category = "flow", title = "Set variable", scopes = BOTH,
      desc = "Writes a variable (shared by all quests and dialogues, kept across maps).",
      params = {
        {name = "key", type = "var", default = ""},
        {name = "op", type = "enum", values = {"set", "add", "toggle"}, default = "set"},
        {name = "value", type = "string", default = "true"},
      },
      enter = function(inst, n)
        local p = n.params
        if p.key == "" then return "next" end
        if p.op == "add" then
          flow.set(p.key, (tonumber(flow.vars[p.key]) or 0) + (tonumber(p.value) or 0))
        elseif p.op == "toggle" then
          flow.set(p.key, not flow.vars[p.key])
        else
          flow.set(p.key, parse_value(p.value))
        end
        return "next"
      end})

node({id = "join", category = "flow", title = "Join", scopes = BOTH, join = true,
      desc = "Waits until all the links leading into it (or this many) have arrived, then goes on.",
      params = {{name = "count", type = "number", default = 0, help = "0 = every incoming link"}},
      enter = function(inst, n, st)
        st.n = (st.n or 0) + 1
        local need = n.params.count > 0 and n.params.count or math.max(1, n.incoming)
        if st.n >= need then return "next" end
      end})

node({id = "delay", category = "flow", title = "Delay", scopes = BOTH,
      desc = "Waits some seconds.",
      params = {{name = "seconds", type = "number", default = 2}},
      enter = function(inst, n, st) st.t = 0 end,
      update = function(inst, n, st, dt)
        st.t = st.t + dt
        if st.t >= n.params.seconds then return "next" end
      end})

-- objectives ----------------------------------------------------------------------
local function count_node(kind, id, title, desc)
  node({id = id, category = "objective", title = title, scopes = QUEST, desc = desc,
        params = {
          {name = "class", type = "class", default = "", help = "entity class (a parent class counts too); empty = any"},
          {name = "count", type = "number", default = 1},
          {name = "text", type = "text", default = ""},
        },
        enter = function(inst, n, st) st.n = 0 end,
        event = function(inst, n, st, k, a)
          if k == kind and is_a(a, n.params.class) then
            st.n = (st.n or 0) + 1
            save()
            if st.n >= n.params.count then return "next" end
          end
        end,
        hud = function(inst, n, st)
          local text = say_text(n.params.text ~= "" and n.params.text or title .. " " .. n.params.class)
          if n.params.count > 1 then text = text .. " (" .. (st.n or 0) .. "/" .. n.params.count .. ")" end
          return text
        end})
end
count_node("kill", "kill", "Kill", "Waits until this many monsters of a class died.")
count_node("pickup", "pickup", "Pick up", "Waits until this many items of a class were picked up.")

node({id = "use", category = "objective", title = "Use / reach target", scopes = QUEST,
      desc = "Waits until a target name is fired: by a button or a trigger whose target is this name, or by the Fire targets node.",
      params = {{name = "target", type = "string", default = ""}, {name = "text", type = "text", default = ""}},
      enter = function() end,
      event = function(inst, n, st, k, a) if k == "fire" and a == n.params.target then return "next" end end,
      hud = function(inst, n) return say_text(n.params.text ~= "" and n.params.text or "Reach " .. n.params.target) end})

node({id = "var_wait", category = "objective", title = "Wait for variable", scopes = QUEST,
      desc = "Waits until a variable has this value (set by another quest, a dialogue or a set_var entity).",
      params = {
        {name = "key", type = "var", default = ""}, {name = "op", type = "enum", values = OPS, default = "=="},
        {name = "value", type = "string", default = "true"}, {name = "text", type = "text", default = ""},
      },
      enter = function(inst, n) if evaluate({check = "variable", key = n.params.key, op = n.params.op, value = n.params.value}) then return "next" end end,
      event = function(inst, n, st, k, a)
        if k == "var" and a == n.params.key and evaluate({check = "variable", key = n.params.key, op = n.params.op, value = n.params.value}) then return "next" end
      end,
      hud = function(inst, n) return n.params.text ~= "" and say_text(n.params.text) or nil end})

-- actions -------------------------------------------------------------------------
node({id = "message", category = "action", title = "Message", scopes = BOTH, desc = "Shows a text on the screen.",
      params = {{name = "text", type = "text", default = ""}, {name = "seconds", type = "number", default = 3}},
      enter = function(inst, n) rbs.message(say_text(n.params.text), n.params.seconds); return "next" end})

node({id = "give", category = "action", title = "Give", scopes = BOTH, desc = "Gives the player something.",
      params = {
        {name = "what", type = "enum", values = {"weapon", "ammo", "key", "health", "armor"}, default = "health"},
        {name = "name", type = "string", default = "", help = "weapon id, ammo type or key colour"},
        {name = "amount", type = "number", default = 25},
      },
      enter = function(inst, n)
        local p, pl = n.params, rbs.player()
        if p.what == "weapon" then rbs.give_weapon(p.name)
        elseif p.what == "ammo" then rbs.give_ammo(p.name, math.floor(p.amount))
        elseif p.what == "key" then rbs.set_key(p.name, true)
        elseif p.what == "health" and pl then pl.health = math.min(pl.max_health or 100, pl.health + p.amount)
        elseif p.what == "armor" and pl then pl.armor = math.min(100, (pl.armor or 0) + p.amount) end
        return "next"
      end})

node({id = "fire_targets", category = "action", title = "Fire targets", scopes = BOTH,
      desc = "Activates everything with this target name (opens a door, starts a trap...).",
      params = {{name = "target", type = "string", default = ""}},
      enter = function(inst, n) if n.params.target ~= "" then rbs.fire_targets(n.params.target, rbs.player()) end; return "next" end})

node({id = "spawn", category = "action", title = "Spawn", scopes = BOTH,
      desc = "Creates an entity at the position of another entity (by target name), or next to the player.",
      params = {{name = "class", type = "class", default = "monster_grunt"}, {name = "at", type = "string", default = ""},
                {name = "count", type = "number", default = 1}},
      enter = function(inst, n)
        local p = n.params
        local pos
        local at = p.at ~= "" and rbs.find(p.at)[1] or nil
        if at then pos = at.pos else local pl = rbs.player(); pos = pl and pl.pos + pl:forward() * 160 or vec(0, 64, 0) end
        for i = 1, math.max(1, math.floor(p.count)) do rbs.spawn(p.class, pos + vec((i - 1) * 40, 0, 0)) end
        return "next"
      end})

node({id = "sound", category = "action", title = "Play sound", scopes = BOTH,
      params = {{name = "path", type = "sound", default = ""}, {name = "volume", type = "number", default = 1}},
      enter = function(inst, n) if n.params.path ~= "" then rbs.sound(n.params.path, nil, n.params.volume) end; return "next" end})

node({id = "start_quest", category = "action", title = "Start quest", scopes = BOTH,
      desc = "Starts another quest (nothing happens if it was started before).",
      params = {{name = "quest", type = "quest", default = ""}},
      enter = function(inst, n) if n.params.quest ~= "" then flow.start_quest(n.params.quest) end; return "next" end})

node({id = "change_map", category = "action", title = "Change map", scopes = BOTH,
      params = {{name = "map", type = "map", default = ""}},
      enter = function(inst, n) flow.save(); rbs.change_map(n.params.map); return "next" end})

node({id = "dialogue", category = "action", title = "Dialogue", scopes = BOTH,
      desc = "Starts a dialogue. In a quest the flow goes on when the dialogue ended; inside a dialogue it jumps to the other one.",
      params = {{name = "dialogue", type = "dialogue", default = ""}},
      enter = function(inst, n, st)
        local name = n.params.dialogue
        if name == "" then return "next" end
        if inst.kind == "dialogue" then
          flow.start_dialogue(name, true, inst.actor)
          return
        end
        st.name = short_name(name)
        if not flow.start_dialogue(name) then return "next" end
      end,
      event = function(inst, n, st, k, a) if k == "dialogue_end" and a == st.name then return "next" end end})

node({id = "log", category = "action", title = "Log", scopes = BOTH, desc = "Writes a line to the console (debugging).",
      params = {{name = "text", type = "string", default = ""}},
      enter = function(inst, n) rbs.log("[" .. inst.name .. "] " .. flow.format(n.params.text)); return "next" end})

-- what a node keeps next to its params: the answers of a choice ({"options": [{"text":..., "require": "var"}]}),
-- and for dialogues the speakers and the style of the dialogue box (see scripts/dialogue.lua)
local build = flow.build
function flow.build(def, kind, name)
  local g, err = build(def, kind, name)
  if not g then return g, err end
  local by_id = {}
  for _, n in ipairs(def.nodes) do if type(n) == "table" and n.id then by_id[n.id] = n end end
  for _, n in ipairs(g.nodes) do
    local src = by_id[n.id]
    if src and type(src.options) == "table" then n.options = src.options end
  end
  g.speakers = type(def.speakers) == "table" and def.speakers or {}
  g.style = type(def.style) == "table" and def.style or {}
  return g
end

refresh_library()

-- ---------------------------------------------------------------------------------
-- console: quest <command> (tests and debugging)
-- ---------------------------------------------------------------------------------
function rbs.quest_console(line)
  local args = {}
  for w in tostring(line):gmatch("%S+") do args[#args + 1] = w end
  local cmd = args[1]
  if cmd == "start" then rbs.log("quest start " .. tostring(args[2]) .. ": " .. tostring(flow.start_quest(args[2] or "")))
  elseif cmd == "event" then flow.event(args[2], args[3], args[4]); rbs.log("event " .. tostring(args[2]) .. " " .. tostring(args[3]))
  elseif cmd == "var" then
    if args[3] then flow.set(args[2], parse_value(args[3])) end
    rbs.log("var " .. tostring(args[2]) .. " = " .. tostring(flow.vars[args[2]]))
  elseif cmd == "say" then rbs.log("dialogue " .. tostring(args[2]) .. ": " .. tostring(flow.start_dialogue(args[2] or "")))
  elseif cmd == "next" then rbs.log("next: " .. tostring(flow.advance(true)))
  elseif cmd == "pick" then rbs.log("pick: " .. tostring(flow.pick(tonumber(args[2]) or 1, true)))
  elseif cmd == "reset" then flow.reset(); rbs.log("quests reset")
  elseif cmd == "map" then flow.save(); rbs.change_map(args[2] or "")
  elseif cmd == "selftest" then rbs.flow_selftest()
  elseif cmd == "dump" or cmd == "list" then
    local names = {}
    for name in pairs(flow.quests) do names[#names + 1] = name end
    table.sort(names)
    for _, name in ipairs(names) do
      local q = flow.quests[name]
      local ids = {}
      for id, st in pairs(q.active) do ids[#ids + 1] = id .. ":" .. q.nodes[id].type .. (st.n and "(" .. st.n .. ")" or "") end
      table.sort(ids)
      rbs.log(string.format("QUEST %s [%s] %s", name, flow.quest_state(name), table.concat(ids, " ")))
    end
    local d = flow.dialogue
    if d and d.ui then
      local line = "DIALOGUE " .. d.name .. " " .. d.ui.kind .. " " .. tostring(d.ui.speaker) .. ": " .. tostring(d.ui.text)
      for i, o in ipairs(d.ui.options or {}) do line = line .. " | " .. i .. "=" .. o.text end
      rbs.log(line)
    elseif d then rbs.log("DIALOGUE " .. d.name .. " (running)") else rbs.log("DIALOGUE none") end
    local keys = {}
    for k, v in pairs(flow.vars) do keys[#keys + 1] = k .. "=" .. tostring(v) end
    table.sort(keys)
    rbs.log("VARS " .. table.concat(keys, " "))
  else
    rbs.log("quest start|event|var|say|next|pick|dump|reset|selftest")
  end
end

function rbs.flow_selftest()
  local ok, run = pcall(require, "tests.flow_test")
  if not ok then rbs.log("FLOW SELFTEST FAIL: " .. tostring(run)); return end
  run()
end

flow.restore()
