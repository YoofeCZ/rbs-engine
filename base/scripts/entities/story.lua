-- Entities for quests and dialogues (see docs/QUESTS.md): a character to talk to, a switch
-- that starts a quest and one that sets a variable.

rbs.class("npc", {
  editor = { category = "story", color = {90, 200, 120}, mins = {-16, 0, -16}, maxs = {16, 64, 16},
             model = "models/monsters/grunt.glb",
             desc = "A character the player talks to: look at it and press the use key. dialogue = a dialogues/<name>.json made in Project > Dialogues.",
             props = { model = "models/monsters/grunt.glb", scale = 1, tint = {120, 200, 255}, npc_name = "", dialogue = "", animator = "", anim = "idle", solid = 1 },
             help = { npc_name = "name shown for the character (the dialogue's speakers are set in the dialogue itself)",
                      dialogue = "dialogue asset (dialogues/<name>.json) started when the player uses it",
                      animator = "animator asset (animators/<name>.json), see docs/ANIMATION.md" } },
  spawn = function(self)
    local p = self.props
    self.model = p.model or ""
    self.scale = util.num(p.scale, 1)
    if p.tint then self.tint = util.color(p.tint, {255, 255, 255}) end
    self.mins, self.maxs = vec(-16, 0, -16), vec(16, 64, 16)
    if util.num(p.solid, 1) ~= 0 then self.solid = "bbox" end
    self.takedamage = false
    if p.anim and p.anim ~= "" then self:play(p.anim, true) end
  end,
  use = function(self, activator)
    local d = self.props.dialogue
    if d and d ~= "" then rbs.flow.start_dialogue(d, false, self) end
  end,
})

rbs.class("quest_start", {
  editor = { category = "story", color = {255, 200, 80}, mins = {-8, -8, -8}, maxs = {8, 8, 8},
             desc = "Starts a quest (quests/<name>.json): when the map starts (auto = 1) or when it is used / triggered (give it a targetname).",
             props = { quest = "", auto = 1, targetname = "" },
             help = { quest = "quest asset (quests/<name>.json) made in Project > Quests", auto = "1 = start when the map starts" } },
  spawn = function(self)
    self.solid = "not"
    self.visible = false
    if util.num(self.props.auto, 1) ~= 0 and (self.props.quest or "") ~= "" then rbs.flow.start_quest(self.props.quest) end
  end,
  use = function(self)
    if (self.props.quest or "") ~= "" then rbs.flow.start_quest(self.props.quest) end
  end,
})

rbs.class("set_var", {
  editor = { category = "story", color = {200, 160, 255}, mins = {-8, -8, -8}, maxs = {8, 8, 8},
             desc = "Sets a quest variable when it is used / triggered (give it a targetname). Quests and dialogues read variables.",
             props = { key = "", value = "true", op = "set", targetname = "" },
             enums = { op = {"set", "add", "toggle"} } },
  spawn = function(self)
    self.solid = "not"
    self.visible = false
  end,
  use = function(self)
    local p, key = self.props, self.props.key or ""
    if key == "" then return end
    if p.op == "add" then rbs.flow.set(key, (tonumber(rbs.flow.vars[key]) or 0) + (tonumber(p.value) or 0))
    elseif p.op == "toggle" then rbs.flow.set(key, not rbs.flow.vars[key])
    else rbs.flow.set(key, rbs.flow.parse_value(p.value)) end
  end,
})

rbs.class("cutscene_start", {
  editor = { category = "story", color = {255, 120, 200}, mins = {-8, -8, -8}, maxs = {8, 8, 8},
             desc = "Plays a cutscene (cutscenes/<name>.json): when the map starts (auto = 1) or when it is used / triggered (give it a targetname).",
             props = { cutscene = "", auto = 0, once = 1, targetname = "" },
             help = { cutscene = "cutscene asset (cutscenes/<name>.json) made in Project > Cutscenes",
                      auto = "1 = play when the map starts",
                      once = "1 = play only the first time (remembered in the save game)" } },
  spawn = function(self)
    self.solid = "not"
    self.visible = false
    if util.num(self.props.auto, 0) ~= 0 then self:play_it() end
  end,
  use = function(self) self:play_it() end,
  play_it = function(self)
    local name = self.props.cutscene or ""
    if name == "" then return end
    local seen = "cutscene." .. name .. ".played"
    if util.num(self.props.once, 1) ~= 0 and rbs.flow.vars[seen] then return end
    if rbs.flow.start_cutscene(name) then rbs.flow.set(seen, true) end
  end,
})
