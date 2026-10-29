-- Moving brush entities: doors, buttons, platforms, solid walls.

rbs.class("func_wall", {
  editor = { brush = true, category = "brush", color = {150, 150, 255}, desc = "Solid brush entity (can be removed/hidden by scripts)." },
  spawn = function(self) self.solid = "bsp" end,
  use = function(self) self.visible = not self.visible; self.solid = self.visible and "bsp" or "not" end,
})

rbs.class("func_illusionary", {
  editor = { brush = true, category = "brush", color = {150, 255, 150}, desc = "Visible but not solid (fake walls for secrets)." },
  spawn = function(self) self.solid = "not" end,
})

local function move_toward(self, target, dt)
  local cur = self.offset
  local d = target - cur
  local dist = d:len()
  local step = self.speed * dt
  if dist <= step then
    self.offset = target
    return true
  end
  self.offset = cur + d * (step / dist)
  return false
end

rbs.class("func_door", {
  editor = { brush = true, category = "brush", color = {80, 160, 255},
             desc = "Sliding door. Opens when the player comes close (auto=1) or when used/targeted. key = red/blue/yellow.",
             props = { move = "0 96 0", speed = 140, wait = 3, auto = 1, key = "", sound = "sounds/door_open.wav", targetname = "" } },
  spawn = function(self)
    self.solid = "bsp"
    self.move = util.vec(self.props.move, vec(0, 96, 0))
    self.speed = util.num(self.props.speed, 140)
    self.wait = util.num(self.props.wait, 3)
    self.state = "closed"
    self.key = self.props.key or ""
  end,
  open = function(self, activator)
    if self.key ~= "" and not rbs.has_key(self.key) then
      if util.is_player(activator) and rbs.time() > (self.next_msg or 0) then
        self.next_msg = rbs.time() + 2
        rbs.message("You need the " .. self.key .. " keycard", 2)
        rbs.sound("sounds/locked.wav", nil, 0.8)
      end
      return
    end
    if self.state == "closed" or self.state == "closing" then
      self.state = "opening"
      rbs.sound(self.props.sound or "sounds/door_open.wav", self.center, 0.9)
    elseif self.state == "open" then
      self.timer = self.wait
    end
  end,
  update = function(self, dt)
    local auto = util.num(self.props.auto, 1) ~= 0 and self.targetname == ""
    if auto and (self.state == "closed" or self.state == "open") then
      local p = rbs.player()
      if p and not p.dead and util.near_brush(self, p.pos, 72) then self:open(p) end
    end
    if self.state == "opening" then
      if move_toward(self, self.move, dt) then
        self.state = "open"
        self.timer = self.wait
      end
    elseif self.state == "open" then
      if self.wait >= 0 then
        self.timer = self.timer - dt
        if self.timer <= 0 then
          self.state = "closing"
          rbs.sound(self.props.sound or "sounds/door_open.wav", self.center, 0.7, 0.9)
        end
      end
    elseif self.state == "closing" then
      if move_toward(self, vec(0, 0, 0), dt) then self.state = "closed" end
    end
  end,
  use = function(self, activator) self:open(activator) end,
  blocked = function(self, other)
    if self.state == "closing" then self.state = "opening" end
    rbs.damage(other, 1, self)
  end,
})

rbs.class("func_button", {
  editor = { brush = true, category = "brush", color = {255, 80, 80}, desc = "Button: press E (use) or shoot it to fire its target.",
             props = { target = "", move = "0 0 -4", wait = 1, shootable = 0 } },
  spawn = function(self)
    self.solid = "bsp"
    self.move = util.vec(self.props.move, vec(0, 0, -4))
    if util.num(self.props.shootable, 0) ~= 0 then
      self.takedamage = true
      self.health = 1
    end
  end,
  use = function(self, activator)
    if rbs.time() < (self.ready_at or 0) then return end
    self.ready_at = rbs.time() + util.num(self.props.wait, 1)
    self.offset = self.move
    self.release_at = rbs.time() + 0.5
    rbs.sound("sounds/button.wav", self.center, 0.8)
    rbs.fire_targets(self.target, activator)
  end,
  damaged = function(self, amount, attacker)
    self:use(attacker)
    return 0
  end,
  update = function(self)
    if self.release_at and rbs.time() > self.release_at then
      self.release_at = nil
      self.offset = vec(0, 0, 0)
    end
  end,
})

rbs.class("func_plat", {
  editor = { brush = true, category = "brush", color = {255, 200, 80}, desc = "Elevator platform: moves by `move` when the player steps on it or when used.",
             props = { move = "0 128 0", speed = 120, wait = 2, targetname = "" } },
  spawn = function(self)
    self.solid = "bsp"
    self.move = util.vec(self.props.move, vec(0, 128, 0))
    self.speed = util.num(self.props.speed, 120)
    self.state = "bottom"
  end,
  update = function(self, dt)
    local p = rbs.player()
    if self.state == "bottom" and self.targetname == "" and p and p.on_ground and util.near_brush(self, p.pos, 30) then
      self.state = "up"
      rbs.sound("sounds/plat_start.wav", self.center, 0.8)
    end
    if self.state == "up" then
      if move_toward(self, self.move, dt) then self.state = "top"; self.timer = util.num(self.props.wait, 2) end
    elseif self.state == "top" then
      self.timer = self.timer - dt
      if self.timer <= 0 and not (p and util.near_brush(self, p.pos, 30)) then self.state = "down" end
    elseif self.state == "down" then
      if move_toward(self, vec(0, 0, 0), dt) then self.state = "bottom" end
    end
  end,
  use = function(self) if self.state == "bottom" then self.state = "up" end end,
  blocked = function(self, other)
    if self.state == "down" then self.state = "up" end
  end,
})
