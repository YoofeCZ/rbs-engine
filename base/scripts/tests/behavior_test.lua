-- Self-test of the behavior tree nodes: console command `bt_selftest` (a map must be running).
-- Prints "BT SELFTEST PASS n" or "BT SELFTEST FAIL: ..." lines. Uses a fake clock and fake
-- monsters, so it checks the node semantics, not the game.

local bt = rbs.bt

return function()
  local failures, passed = 0, 0
  local function check(name, cond)
    if cond then passed = passed + 1 else failures = failures + 1; rbs.log("BT SELFTEST FAIL: " .. name) end
  end

  local real_time = rbs.time
  local now = 0
  rbs.time = function() return now end

  local created = {}
  -- a throwaway node type whose result is whatever `script` returns each tick
  local function leaf(id, kind, fn, reset)
    created[#created + 1] = id
    rbs.bt_node({id = id, kind = kind or "action", run = fn, reset = reset})
  end

  local function monster()
    return {bb = {}, health = 100, max_health = 100, state = "idle", next_attack = 0, id = 1, classname = "test", pos = vec(0, 0, 0),
            play = function() end, walk = function() end}
  end

  -- builds a tree from nested {type, params, children...}: {"sequence", {memory = true}, {...}, {...}}
  local function tree(spec)
    local nodes, n = {}, 0
    local function add(s)
      n = n + 1
      local id = "t" .. n
      local node = {id = id, type = s[1], params = s[2], children = {}}
      nodes[#nodes + 1] = node
      for i = 3, #s do node.children[#node.children + 1] = add(s[i]) end
      return id
    end
    local root = add(spec)
    local t, err = bt.build({root = root, nodes = nodes})
    assert(t, err)
    return t
  end

  local function run(spec)
    local self = monster()
    bt.attach(self, tree(spec))
    return self, function(dt) now = now + (dt or 0.1); bt.update(self, dt or 0.1, true); return self.bt.last[self.bt.tree.root.id] end
  end

  -- ---- sequence ----
  local order = {}
  leaf("t_ok", "action", function(self, p) order[#order + 1] = p.name or "ok"; return "success" end)
  leaf("t_fail", "action", function(self, p) order[#order + 1] = p.name or "fail"; return "failure" end)

  local _, step = run({"sequence", nil, {"t_ok", {name = "a"}}, {"t_ok", {name = "b"}}})
  check("sequence of successes succeeds", step() == "success")
  check("sequence runs children in order", table.concat(order, ",") == "a,b")

  order = {}
  _, step = run({"sequence", nil, {"t_ok", {name = "a"}}, {"t_fail", {name = "x"}}, {"t_ok", {name = "c"}}})
  check("sequence fails at the first failure", step() == "failure")
  check("sequence stops after the failure", table.concat(order, ",") == "a,x")

  -- memory: the running child is resumed, earlier actions are not repeated
  local runs = 0
  leaf("t_run3", "action", function(self, p, ctx)
    ctx.mem.n = (ctx.mem.n or 0) + 1
    runs = runs + 1
    if ctx.mem.n >= 3 then ctx.mem.n = nil; return "success" end
    return "running"
  end)
  order = {}
  local s1, step1 = run({"sequence", nil, {"t_ok", {name = "first"}}, {"t_run3"}, {"t_ok", {name = "last"}}})
  local r1, r2, r3 = step1(), step1(), step1()
  check("sequence runs while a child runs", r1 == "running" and r2 == "running")
  check("sequence finishes with the running child", r3 == "success")
  check("sequence with memory does not repeat finished children", table.concat(order, ",") == "first,last")

  -- recheck: a condition before the running child failing stops it (and resets it)
  local reset_called = false
  local flag = true
  leaf("t_flag", "condition", function() return flag end)
  leaf("t_forever", "action", function() return "running" end, function() reset_called = true end)
  local _, step2 = run({"sequence", nil, {"t_flag"}, {"t_forever"}})
  check("recheck: running while the condition holds", step2() == "running" and step2() == "running")
  flag = false
  check("recheck: fails when the condition no longer holds", step2() == "failure")
  check("recheck: the interrupted child was reset", reset_called)
  flag = true

  -- ---- selector ----
  order = {}
  _, step = run({"selector", nil, {"t_fail", {name = "a"}}, {"t_ok", {name = "b"}}, {"t_ok", {name = "c"}}})
  check("selector succeeds with the first success", step() == "success")
  check("selector stops at the first success", table.concat(order, ",") == "a,b")
  _, step = run({"selector", nil, {"t_fail"}, {"t_fail"}})
  check("selector fails when all fail", step() == "failure")

  -- reactive: a higher priority child interrupts a running lower one
  local high = false
  reset_called = false
  leaf("t_high", "condition", function() return high end)
  local _, step3 = run({"selector", nil, {"sequence", nil, {"t_high"}, {"t_ok", {name = "high"}}}, {"t_forever"}})
  check("selector runs the lower child first", step3() == "running")
  high = true
  order = {}
  check("selector switches to the higher priority child", step3() == "success" and order[1] == "high")
  check("selector reset the interrupted lower child", reset_called)

  -- ---- parallel ----
  runs = 0
  local _, stepp = run({"parallel", {policy = "all"}, {"t_ok"}, {"t_run3"}})
  check("parallel(all) waits for every child", stepp() == "running" and stepp() == "running" and stepp() == "success")
  _, stepp = run({"parallel", {policy = "one"}, {"t_forever"}, {"t_ok"}})
  check("parallel(one) succeeds when one child does", stepp() == "success")
  _, stepp = run({"parallel", {policy = "all"}, {"t_forever"}, {"t_fail"}})
  check("parallel(all) fails when one child fails", stepp() == "failure")

  -- ---- decorators ----
  _, step = run({"inverter", nil, {"t_ok"}})
  check("inverter turns success into failure", step() == "failure")
  _, step = run({"inverter", nil, {"t_fail"}})
  check("inverter turns failure into success", step() == "success")
  _, step = run({"succeeder", nil, {"t_fail"}})
  check("succeeder", step() == "success")
  _, step = run({"failer", nil, {"t_ok"}})
  check("failer", step() == "failure")

  local count = 0
  leaf("t_count", "action", function() count = count + 1; return "success" end)
  _, step = run({"repeat", {count = 3}, {"t_count"}})
  check("repeat runs until its count", step() == "running" and step() == "running" and step() == "success" and count == 3)
  _, step = run({"repeat", {count = 0}, {"t_count"}})
  check("repeat(0) never finishes", step() == "running" and step() == "running")
  _, step = run({"repeat", {count = 5}, {"t_fail"}})
  check("repeat stops with the child's failure", step() == "failure")

  count = 0
  local _, stepc = run({"cooldown", {seconds = 1}, {"t_count"}})
  check("cooldown lets the first run through", stepc(0.1) == "success" and count == 1)
  check("cooldown fails while cooling down", stepc(0.1) == "failure" and count == 1)
  now = now + 1.0  -- time passes without anyone asking
  check("cooldown allows it again afterwards", stepc(0.2) == "success" and count == 2)

  local _, stept = run({"timeout", {seconds = 0.5}, {"t_forever"}})
  local first = stept(0.1)
  for _ = 1, 3 do stept(0.1) end
  check("timeout keeps running at first", first == "running")
  check("timeout fails when the child runs too long", stept(0.3) == "failure")

  -- ---- blackboard ----
  local bbm = monster()
  bt.attach(bbm, tree({"sequence", nil, {"set_bb", {key = "mood", value = "angry"}}, {"compare", {key = "mood", op = "==", value = "angry"}},
                       {"set_bb", {key = "global.alarm", value = "true"}}, {"compare", {key = "global.alarm", op = "==", value = "true"}}}))
  now = now + 0.1
  bt.update(bbm, 0.1, true)
  check("blackboard set and compare (local and global)", bbm.bt.last[bbm.bt.tree.root.id] == "success" and bbm.bb.mood == "angry" and bt.global.alarm == true)
  bt.global.alarm = nil

  local cm = monster()
  cm.health = 20
  bt.attach(cm, tree({"health", {op = "<", percent = 25}}))
  now = now + 0.1
  bt.update(cm, 0.1, true)
  check("health condition compares in percent", cm.bt.last[cm.bt.tree.root.id] == "success")

  -- ---- loading ----
  for _, name in ipairs({"soldier", "ambusher"}) do
    local t = bt.load("behaviors/" .. name .. ".json")
    check("behaviors/" .. name .. ".json loads", t ~= nil)
    if t then
      local known = true
      for _, node in ipairs(t.nodes) do if not node.meta then known = false end end
      check(name .. ": every node type exists in the library", known)
    end
  end
  check("an unknown node type fails instead of crashing", (function()
    local real_log = rbs.log
    rbs.log = function() end  -- building the tree logs the unknown type on purpose
    local t = tree({"selector", nil, {"no_such_node"}, {"t_ok"}})
    rbs.log = real_log
    local m = monster()
    bt.attach(m, t)
    now = now + 0.1
    bt.update(m, 0.1, true)
    return m.bt.last[m.bt.tree.root.id] == "success"
  end)())
  check("a definition without nodes is refused", bt.build({}) == nil)

  for _, id in ipairs(created) do bt.unregister(id) end
  rbs.time = real_time
  if failures == 0 then rbs.log("BT SELFTEST PASS " .. passed) else rbs.log("BT SELFTEST FAILED " .. failures .. " of " .. (passed + failures)) end
end
