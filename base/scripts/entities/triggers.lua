-- Brush triggers (invisible volumes). Create them in the editor: select brushes,
-- "Tie to entity" -> trigger_once etc. Their brushes use the "trigger" texture.

local function trigger_spawn(self)
  self.solid = "trigger"
  self.visible = false
  self.wait = util.num(self.props.wait, 0.5)
  self.next_ok = 0
end

local function trigger_fire(self, other)
  if self.props.message and self.props.message ~= "" then rbs.message(self.props.message, 3) end
  local delay = util.num(self.props.delay, 0)
  if delay > 0 then
    self.pending = rbs.time() + delay
    self.pending_who = other
  else
    rbs.fire_targets(self.target, other)
  end
end

local function trigger_update(self)
  if self.pending and rbs.time() >= self.pending then
    self.pending = nil
    rbs.fire_targets(self.target, self.pending_who)
    if self.remove_after then rbs.remove(self) end
  end
end

rbs.class("trigger_once", {
  editor = { brush = true, category = "triggers", color = {255, 140, 0}, desc = "Fires its target once when the player enters.",
             props = { target = "", message = "", delay = 0 } },
  spawn = trigger_spawn,
  update = trigger_update,
  touch = function(self, other)
    if not util.is_player(other) or self.fired then return end
    self.fired = true
    trigger_fire(self, other)
    if util.num(self.props.delay, 0) <= 0 then rbs.remove(self) else self.remove_after = true end
  end,
})

rbs.class("trigger_multiple", {
  editor = { brush = true, category = "triggers", color = {255, 170, 0}, desc = "Fires its target every `wait` seconds while touched.",
             props = { target = "", message = "", wait = 1, delay = 0 } },
  spawn = trigger_spawn,
  update = trigger_update,
  touch = function(self, other)
    if not util.is_player(other) or rbs.time() < self.next_ok then return end
    self.next_ok = rbs.time() + self.wait
    trigger_fire(self, other)
  end,
})

rbs.class("trigger_hurt", {
  editor = { brush = true, category = "triggers", color = {255, 40, 40}, desc = "Damages anything inside (lava, crushers...).",
             props = { damage = 10, wait = 0.5 } },
  spawn = trigger_spawn,
  touch = function(self, other)
    if rbs.time() < (self.next_hurt or 0) then return end
    self.next_hurt = rbs.time() + self.wait
    rbs.damage(other, util.num(self.props.damage, 10), self)
  end,
})

rbs.class("trigger_push", {
  editor = { brush = true, category = "triggers", color = {120, 255, 120}, desc = "Jump pad: sets the velocity of the player.",
             props = { push = "0 600 0" } },
  spawn = trigger_spawn,
  touch = function(self, other)
    if rbs.time() < (self.next_ok or 0) then return end
    self.next_ok = rbs.time() + 0.2
    other.vel = util.vec(self.props.push, vec(0, 600, 0))
    rbs.sound("sounds/jumppad.wav", other.pos, 0.8)
  end,
})

rbs.class("trigger_secret", {
  editor = { brush = true, category = "triggers", color = {255, 0, 255}, desc = "Secret area.", props = { message = "You found a secret area!" } },
  spawn = function(self) trigger_spawn(self); rbs.add_stat("secrets_total") end,
  touch = function(self, other)
    if not util.is_player(other) or self.fired then return end
    self.fired = true
    rbs.add_stat("secrets")
    rbs.message(self.props.message or "You found a secret area!", 3)
    rbs.sound("sounds/secret.wav", nil, 0.9)
    rbs.fire_targets(self.target, other)
    rbs.remove(self)
  end,
})

rbs.class("trigger_changelevel", {
  editor = { brush = true, category = "triggers", color = {255, 255, 255}, desc = "Ends the level. Empty map = map setting next_map / back to menu.",
             props = { map = "" } },
  spawn = trigger_spawn,
  touch = function(self, other)
    if not util.is_player(other) or self.fired then return end
    self.fired = true
    rbs.change_map(self.props.map or "")
  end,
})

rbs.class("trigger_camera", {
  editor = { brush = true, category = "triggers", color = {120, 255, 255}, desc = "Shows a mission_camera picture-in-picture.",
             props = { camera = "cam1", time = 5 } },
  spawn = trigger_spawn,
  touch = function(self, other)
    if not util.is_player(other) or self.fired then return end
    self.fired = true
    rbs.show_camera(self.props.camera or "cam1", util.num(self.props.time, 5))
  end,
})
