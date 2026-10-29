-- Shared monster AI. Subclasses set the tunables and override attack().
--
-- States: idle -> chase -> attack -> chase ... pain, dead.
-- Animation names expected in the model: idle, walk, attack, pain, death.

rbs.class("monster_base", {
  -- tunables
  max_hp = 60,
  speed = 150,
  turn_speed = 540,
  attack_range = 900,
  keep_distance = 96,
  attack_cooldown = 1.5,
  attack_delay = 0.35,        -- time from attack start to the actual shot/hit
  attack_time = 0.8,          -- total attack state duration
  pain_chance = 0.6,
  gib_health = -40,
  sight_sound = "sounds/grunt_sight.wav",
  pain_sound = "sounds/grunt_pain.wav",
  death_sound = "sounds/grunt_death.wav",

  spawn = function(self)
    self.health = util.num(self.props.health, self.max_hp)
    self.max_health = self.health
    self.takedamage = true
    self.monster = true
    self.solid = "bbox"
    self.movetype = "walk"
    self.state = "idle"
    self.next_attack = 0
    self.next_look = math.random() * 0.3
    self.avoid_until = 0
    self:play("idle", true)
    rbs.add_stat("monsters")
  end,

  wake = function(self, enemy)
    if self.state ~= "idle" then return end
    self.enemy = enemy
    self.state = "chase"
    self.next_attack = rbs.time() + 0.5 + math.random() * 0.8
    rbs.sound(self.sight_sound, self.pos, 1.0, 0.9 + math.random() * 0.2)
    -- wake up the neighbours too
    for _, m in ipairs(rbs.find_radius(self.pos, 500)) do
      if m.monster and not m.dead and m.state == "idle" and m ~= self then m.enemy = enemy; m.state = "chase" end
    end
  end,

  update = function(self, dt)
    if self.dead then return end
    local p = rbs.player()
    local t = rbs.time()

    if self.state == "idle" then
      if t > self.next_look then
        self.next_look = t + 0.3
        if p and not p.dead and self:distance(p) < 1800 and rbs.visible(self, p) then self:wake(p) end
      end
      return
    end

    if not p or p.dead then
      self.state = "idle"
      self:play("idle", true)
      return
    end

    if self.state == "pain" then
      self:walk(vec(0, 0, 0), 0)
      if t > self.state_until then self.state = "chase" end
      return
    end

    if self.state == "attack" then
      self:face(p.pos, self.turn_speed, dt)
      self:walk(vec(0, 0, 0), 0)
      if not self.attacked and t > self.attack_at then
        self.attacked = true
        self:attack(p)
      end
      if t > self.state_until then
        self.state = "chase"
        self.next_attack = t + self.attack_cooldown * (0.7 + math.random() * 0.6)
      end
      return
    end

    -- chase
    local d = self:distance(p)
    local visible = rbs.visible(self, p)
    if visible and d < self.attack_range and t > self.next_attack then
      self.state = "attack"
      self.attacked = false
      self.attack_at = t + self.attack_delay
      self.state_until = t + self.attack_time
      self:play("attack", false)
      return
    end
    local dir = (p.pos - self.pos):flat():normalize()
    if t < self.avoid_until then
      dir = self.avoid_dir
    elseif self.blocked then
      -- side step around obstacles
      local side = dir:cross(vec(0, 1, 0))
      if math.random() < 0.5 then side = -side end
      self.avoid_dir = (side + dir * 0.3):normalize()
      self.avoid_until = t + 0.4 + math.random() * 0.4
      dir = self.avoid_dir
    end
    self:face(self.pos + dir, self.turn_speed, dt)
    if d > self.keep_distance or not visible then
      self:walk(dir, self.speed)
      self:play("walk", true, self.speed / 150)
    else
      self:walk(dir, 0)
      self:play("idle", true)
    end
  end,

  attack = function(self, enemy) end,

  damaged = function(self, amount, attacker)
    if self.dead then
      -- shooting corpses gibs them
      if self.health - amount < self.gib_health then
        rbs.gib(self, 9)
      end
      return amount
    end
    if attacker and attacker.classname == "player" and self.state == "idle" then
      self.enemy = attacker
      self:wake(attacker)
    end
    return amount
  end,

  pain = function(self, amount)
    rbs.sound(self.pain_sound, self.pos, 0.9, 0.9 + math.random() * 0.2)
    if self.state ~= "attack" and math.random() < self.pain_chance then
      self.state = "pain"
      self.state_until = rbs.time() + 0.35
      self:play("pain", false)
    end
  end,

  die = function(self, attacker, amount)
    self.solid = "corpse"  -- no longer blocks movement, but bullets still hit (gibbing)
    self.takedamage = true  -- corpses can still be gibbed
    if self.health < self.gib_health then
      rbs.gib(self, 9)
      return
    end
    rbs.sound(self.death_sound, self.pos, 1.0, 0.9 + math.random() * 0.2)
    self:play("death", false)
    self.state = "dead"
    self:walk(vec(0, 0, 0), 0)
  end,
})
