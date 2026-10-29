-- Self-test of the quest / dialogue flow: console command `quest selftest` (a map must be
-- running). Prints "FLOW SELFTEST PASS n" or "FLOW SELFTEST FAIL: ..." lines. It builds small
-- graphs in memory and feeds them events, so it checks the semantics, not the game content.

local flow = rbs.flow

return function()
  local failures, passed = 0, 0
  local function check(name, cond)
    if cond then passed = passed + 1 else failures = failures + 1; rbs.log("FLOW SELFTEST FAIL: " .. name) end
  end

  local saved_vars, saved_quests, saved_dialogue = flow.vars, flow.quests, flow.dialogue
  flow.vars, flow.quests, flow.dialogue = {}, {}, nil
  flow.no_save = true
  local real_message, real_sound = rbs.message, rbs.sound
  rbs.message, rbs.sound = function() end, function() end  -- the tests must not flash messages at the player
  local created = {}

  -- n(id, type, params, out): out = {next = {"id", ...}}
  local function N(id, type_, params, out, extra)
    local n = {id = id, type = type_, params = params or {}, out = out or {}}
    for k, v in pairs(extra or {}) do n[k] = v end
    return n
  end
  local function graph(kind, nodes, vars)
    local g, err = flow.build({title = "T", nodes = nodes, vars = vars}, kind, "__t")
    assert(g, err)
    return g
  end
  local function quest(name, nodes, vars)
    assert(flow.run_quest(graph("quest", nodes, vars), name))
    return flow.quests[name]
  end
  local function active_types(inst)
    local t = {}
    for id in pairs(inst.active) do t[#t + 1] = inst.nodes[id].type end
    table.sort(t)
    return table.concat(t, ",")
  end

  -- ---- a simple quest: kill 2 grunts, then done ----
  local q = quest("t1", {
    N("a", "start", nil, {next = {"b"}}),
    N("b", "kill", {class = "monster_grunt", count = 2}, {next = {"c"}}),
    N("c", "end", {result = "success"}),
  })
  check("a quest waits at its objective", active_types(q) == "kill" and not q.done)
  flow.event("kill", "monster_fiend")
  check("another class does not count", q.active.b.n == 0)
  flow.event("kill", "monster_grunt")
  check("a kill counts", q.active.b.n == 1)
  flow.event("kill", "monster_heavy")  -- monster_heavy is a monster_grunt
  check("a child class counts for its parent class", q.done and q.result == "success")
  check("quest_state reports done", flow.quest_state("t1") == "done")
  check("a finished quest cannot be started again", flow.run_quest(graph("quest", {N("a", "start")}), "t1") == false)

  -- ---- hud text with progress ----
  q = quest("t2", {
    N("a", "start", nil, {next = {"b"}}),
    N("b", "pickup", {class = "item_health", count = 3, text = "Find medkits"}),
  })
  flow.event("pickup", "item_health")
  local hud = flow.impl.pickup.hud(q, q.nodes.b, q.active.b)
  check("the objective text shows its progress", hud == "Find medkits (1/3)")

  -- ---- branch on a variable, set_var ----
  q = quest("t3", {
    N("a", "start", nil, {next = {"b"}}),
    N("b", "set_var", {key = "gold", op = "add", value = "5"}, {next = {"c"}}),
    N("c", "branch", {check = "variable", key = "gold", op = ">=", value = "5"}, {yes = {"d"}, no = {"e"}}),
    N("d", "set_var", {key = "rich", value = "true"}),
    N("e", "set_var", {key = "rich", value = "false"}),
  })
  check("set_var add and branch yes", flow.vars.gold == 5 and flow.vars.rich == true)
  check("a quest with no links left ends with success", q.done and q.result == "success")
  flow.vars.rich = nil
  quest("t3b", {
    N("a", "start", nil, {next = {"c"}}),
    N("c", "branch", {check = "variable", key = "poor", op = "==", value = "false"}, {yes = {"d"}, no = {"e"}}),  -- unset == false
    N("d", "set_var", {key = "rich", value = "1"}),
    N("e", "set_var", {key = "rich", value = "2"}),
  })
  check("an unset variable equals false", flow.vars.rich == 1)
  check("toggle", (function()
    quest("t3c", {N("a", "start", nil, {next = {"b"}}), N("b", "set_var", {key = "flag", op = "toggle"})})
    return flow.vars.flag == true
  end)())

  -- ---- two objectives in parallel, joined ----
  q = quest("t4", {
    N("a", "start", nil, {next = {"b", "c"}}),
    N("b", "kill", {class = "monster_grunt", count = 1}, {next = {"j"}}),
    N("c", "use", {target = "lever"}, {next = {"j"}}),
    N("j", "join", nil, {next = {"e"}}),
    N("e", "end", {result = "success"}),
  })
  check("one link into two nodes runs both", active_types(q) == "kill,use")
  flow.event("kill", "monster_grunt")
  check("the join waits for the other branch", not q.done and active_types(q) == "join,use")
  flow.event("fire", "other")
  check("an unrelated target does nothing", not q.done)
  flow.event("fire", "lever")
  check("the join goes on when both arrived", q.done and q.result == "success")

  -- ---- variable objective ----
  q = quest("t5", {
    N("a", "start", nil, {next = {"b"}}),
    N("b", "var_wait", {key = "door_open", op = "==", value = "true"}, {next = {"c"}}),
    N("c", "end", {result = "fail"}),
  })
  check("var_wait waits", not q.done)
  flow.set("door_open", true)
  check("var_wait finishes when the variable matches (and fail ends failed)", q.done and q.result == "fail" and flow.quest_state("t5") == "failed")

  -- ---- delay ----
  q = quest("t6", {N("a", "start", nil, {next = {"b"}}), N("b", "delay", {seconds = 1}, {next = {"c"}}), N("c", "set_var", {key = "late", value = "true"})})
  rbs._update(0.4)
  check("delay waits", flow.vars.late == nil)
  rbs._update(0.7)
  check("delay passes after its time", flow.vars.late == true)

  -- ---- quests starting quests, quest state in a branch ----
  q = quest("t7", {
    N("a", "start", nil, {next = {"b"}}),
    N("b", "branch", {check = "quest done", key = "t1"}, {yes = {"c"}, no = {"d"}}),
    N("c", "set_var", {key = "saw_t1", value = "done"}),
    N("d", "set_var", {key = "saw_t1", value = "missing"}),
  })
  check("branch can ask for a quest state", flow.vars.saw_t1 == "done")

  -- ---- dialogue: say, choice with a hidden option, branch ----
  flow.vars.vip = nil
  local d = graph("dialogue", {
    N("a", "start", nil, {next = {"s1"}}),
    N("s1", "say", {speaker = "Guard", text = "Halt, {name}!"}, {next = {"c1"}}),
    N("c1", "choice", {text = "Well?"}, {o1 = {"v1"}, o2 = {"v2"}, o3 = {"v3"}},
      {options = {{text = "Friend."}, {text = "Enemy."}, {text = "VIP pass", require = "vip"}}}),
    N("v1", "set_var", {key = "answer", value = "friend"}, {next = {"e"}}),
    N("v2", "set_var", {key = "answer", value = "enemy"}, {next = {"e"}}),
    N("v3", "set_var", {key = "answer", value = "vip"}, {next = {"e"}}),
    N("e", "end"),
  })
  flow.vars.name = "Bob"
  flow.dialogue = nil
  check("a dialogue starts", flow.run_dialogue(d, "t_d"))
  check("say shows its text with variables", flow.dialogue.ui.kind == "say" and flow.dialogue.ui.text == "Halt, Bob!" and flow.dialogue.ui.speaker == "Guard")
  check("a second dialogue is refused while one runs", flow.run_dialogue(d, "t_d2") == false)
  check("advance goes to the choice", flow.advance(true) and flow.dialogue.ui.kind == "choice")
  check("an option with an unmet requirement is hidden", #flow.dialogue.ui.options == 2)
  check("pick follows the chosen option's link", flow.pick(2, true) and flow.vars.answer == "enemy")
  check("the dialogue ended", flow.dialogue == nil)
  flow.vars.vip = true
  flow.run_dialogue(d, "t_d")
  flow.advance(true)
  check("the option shows once its requirement holds", #flow.dialogue.ui.options == 3)
  flow.pick(3, true)
  check("picking a later option keeps its original port", flow.vars.answer == "vip")

  -- ---- how a dialogue plays: typing, shots, preview ----
  local view = flow.view
  local typed = graph("dialogue", {
    N("a", "start", nil, {next = {"s"}}),
    N("s", "say", {speaker = "Guard", text = "Hello there, friend."}, {next = {"e"}}),
    N("e", "end"),
  })
  typed.style = {text_speed = 10}
  typed.speakers = {{id = "Guard", name = "Captain Guard", color = {1, 2, 3}, portrait = "sprites/x.png", emotions = {angry = "sprites/y.png"}}}
  flow.dialogue = nil
  flow.run_dialogue(typed, "t_type")
  local ui = flow.dialogue.ui
  check("a line starts with no text typed", ui.shown == 0 and not ui.done and ui.len == 20)
  check("the speaker is looked up by id (name, portrait)", ui.speaker == "Captain Guard" and ui.portrait == "sprites/x.png")
  view.update(flow.dialogue, 0.5)
  check("the text types with time (10 characters per second)", ui.shown == 5)
  check("the first press completes the typing, not the line", flow.advance() == true and ui.done and flow.dialogue ~= nil)
  check("the second press goes on", flow.advance() == true and flow.dialogue == nil)
  typed.style = {text_speed = 0}
  flow.run_dialogue(typed, "t_type")
  check("text speed 0 shows the whole line at once", flow.dialogue.ui.done and flow.dialogue.ui.shown == 20)
  flow.advance(true)

  -- camera shots
  local h = 64
  local subject = {feet = vec(0, 0, 0), height = h, head = vec(0, h * 0.9, 0), yaw = 0}
  local pos, look, fov = view.shot("close-up", "none", 0, subject, nil)
  check("a close-up is close to the face", math.abs((pos - look):len() - h * 0.75) < 1 and fov == 42)
  check("a close-up looks at the face", math.abs(look.y - subject.head.y) < h * 0.2)
  local p6 = view.shot("close-up", "push-in", 6, subject, nil)
  check("push-in comes closer by a fifth over six seconds", math.abs((p6 - look):len() - (pos - look):len() * 0.8) < 0.5)
  local po = view.shot("close-up", "pull-out", 6, subject, nil)
  check("pull-out moves away", (po - look):len() > (pos - look):len())
  local pr = view.shot("close-up", "orbit", 5, subject, nil)
  check("orbit turns around the face at the same distance", math.abs((pr - look):len() - (pos - look):len()) < 0.01 and (pr - pos):len() > 5)
  local far = view.shot("over-shoulder", "none", 0, subject, {head = vec(1000, 50, 0)})
  local near = view.shot("over-shoulder", "none", 0, subject, {head = vec(240, 50, 0)})
  check("over-the-shoulder does not depend on how far the player is", (far - near):len() < 0.01)
  local pm, lm = view.shot("medium", "none", 0, subject, nil)
  local pw = view.shot("wide", "none", 0, subject, nil)
  check("wide is further away than medium", (pw - lm):len() > (pm - lm):len())

  -- the editor preview asks for exactly this
  local def = {title = "p", speakers = {{id = "G", name = "Guard"}}, style = {default_shot = "close-up"},
               nodes = {{id = "a", type = "start", out = {next = {"s"}}},
                        {id = "s", type = "say", params = {speaker = "G", text = "Hello there, friend.", shot = "auto"}, out = {}}}}
  local st = rbs.flow_preview_state({def = def, node = "s", t = 0.2})
  check("the preview state has a camera shot", st.camera and #st.camera.pos == 3 and st.camera.fov == 42 and st.cinematic == true)
  check("the preview types the text by time (45 characters per second)", st.ui.shown == 9 and not st.ui.done)
  def.nodes[2].params.shot = "none"
  st = rbs.flow_preview_state({def = def, node = "s", t = 99})
  check("a line without a shot previews the player's own view", st.cinematic == false and st.ui.done)
  check("an empty dialogue previews nothing", rbs.flow_preview_state({def = {nodes = {{id = "a", type = "start", out = {}}}}, node = ""}).empty == true)

  -- ---- a quest waits for its dialogue ----
  local dlg = graph("dialogue", {N("a", "start", nil, {next = {"s"}}), N("s", "say", {text = "Hi"}, {next = {"e"}}), N("e", "end")})
  local realload = flow.load
  flow.load = function(kind, name) if kind == "dialogue" and name == "talk" then return dlg end return realload(kind, name) end
  q = quest("t8", {
    N("a", "start", nil, {next = {"b"}}),
    N("b", "dialogue", {dialogue = "talk"}, {next = {"c"}}),
    N("c", "set_var", {key = "talked", value = "true"}),
  })
  check("the quest waits while the dialogue runs", flow.vars.talked == nil and flow.dialogue ~= nil)
  flow.advance(true)
  check("the quest goes on when the dialogue ended", flow.vars.talked == true)
  flow.load = realload

  -- ---- saving and restoring across maps ----
  flow.no_save = false
  flow.quests = {}  -- only the quest under test goes into the save
  q = quest("t9", {N("a", "start", nil, {next = {"b"}}), N("b", "kill", {class = "monster_grunt", count = 4})})
  flow.event("kill", "monster_grunt")
  flow.event("kill", "monster_grunt")
  flow.set("gold", 7)
  flow.save()
  local live_vars = flow.vars
  flow.vars, flow.quests = {}, {}
  -- the restore reloads quests from files: give it this one in memory
  local g9 = q.graph
  flow.load = function(kind, name) if kind == "quest" and name == "t9" then return g9 end return realload(kind, name) end
  flow.restore()
  flow.load = realload
  check("progress survives a save / restore", flow.quests.t9 and flow.quests.t9.active.b and flow.quests.t9.active.b.n == 2)
  check("variables survive a save / restore", flow.vars.gold == live_vars.gold)
  flow.no_save = true
  pcall(rbs.persist_set, "flow", "{}")  -- leave nothing of the test in the real save

  -- ---- unknown node types and custom nodes ----
  local before = #flow.order
  local ran = false
  rbs.flow_node({id = "t_custom", category = "action", title = "T", enter = function(inst, n) ran = true; return "next" end})
  created[#created + 1] = "t_custom"
  quest("t10", {N("a", "start", nil, {next = {"b"}}), N("b", "t_custom", nil, {next = {"c"}}), N("c", "set_var", {key = "custom_done", value = "true"})})
  check("a custom node runs", ran and flow.vars.custom_done == true and #flow.order == before + 1)
  local real_log = rbs.log
  rbs.log = function() end
  q = quest("t11", {N("a", "start", nil, {next = {"b"}}), N("b", "no_such_node", nil, {next = {"c"}}), N("c", "set_var", {key = "never", value = "1"})})
  rbs.log = real_log
  check("an unknown node type stops its branch instead of crashing", flow.vars.never == nil)
  check("a graph without a start is refused", flow.build({nodes = {{id = "x", type = "message"}}}, "quest", "t") == nil)

  for _, id in ipairs(created) do flow.unregister(id) end
  flow.vars, flow.quests, flow.dialogue = saved_vars, saved_quests, saved_dialogue
  flow.no_save = false
  rbs.freeze(false)
  rbs.message, rbs.sound = real_message, real_sound
  if failures == 0 then rbs.log("FLOW SELFTEST PASS " .. passed) else rbs.log("FLOW SELFTEST FAILED " .. failures .. " of " .. (passed + failures)) end
end
