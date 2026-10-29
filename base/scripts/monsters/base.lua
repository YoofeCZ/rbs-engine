-- Built-in monster AI.
--
-- Every tunable below can be changed without code: per placed monster in the editor's
-- Inspector, or for a whole enemy type made with Project > Entities > New type (saved as
-- entities/<name>.json). For your own AI write a class with base = "monster_base" (or any
-- monster) and override only what you need: attack(), think() or the whole update().
-- See docs/AI.md.
--
-- States: idle (waiting or patrolling) -> chase <-> attack, pain, flee, dead.
-- Animations used from the model: idle, walk, attack, pain, death.

local TUNABLES = {
  health = 60,
  -- behaviour
  ai = "chase",
  speed = 150,
  turn_speed = 540,
  sight_range = 1800,
  wake_on_sight = true,
  keep_distance = 96,
  patrol_path = "",
  flee_health = 0,
  -- attack
  attack_type = "hitscan",
  attack_range = 900,
  attack_cooldown = 1.5,
  attack_delay = 0.35,
  attack_time = 0.8,
  bullets = 1,
  spread = 0.05,
  bullet_damage = 5,
  projectile = "fireball",
  projectile_speed = 700,
  projectile_damage = 15,
  melee_range = 72,
  melee_damage = 15,
  leap = false,
  attack_sound = "",
  melee_sound = "",
  muzzle_flash = true,
  -- reactions and looks
  pain_chance = 0.6,
  gib_health = -40,
  sight_sound = "sounds/grunt_sight.wav",
  pain_sound = "sounds/grunt_pain.wav",
  death_sound = "sounds/grunt_death.wav",
  model = "",
  scale = 1,
  tint = {255, 255, 255},
}

local ENUMS = {
  ai = {"chase", "ranged", "melee", "turret", "patrol"},
  attack_type = {"hitscan", "projectile", "melee", "melee+projectile", "none", "custom"},
  projectile = {"fireball", "plasma_bolt", "rocket"},
}

local HELP = {
  ai = "chase: runs at you and attacks | ranged: keeps its distance and strafes | melee: closes in for hand-to-hand | turret: never moves | patrol: walks patrol_path until it sees you",
  speed = "walking speed (units per second, the player runs at 320)",
  turn_speed = "degrees per second",
  sight_range = "how far it notices the player",
  wake_on_sight = "off = ambush: waits until it is shot or triggered (point a trigger's target at it)",
  keep_distance = "chase: stops this close to you; ranged: the distance it tries to hold",
  patrol_path = "targetname of the first path_corner; corners link to the next one with target",
  flee_health = "runs away for a while when its health drops below this (0 = never)",
  attack_type = "hitscan = bullets | projectile = fireball, plasma or rocket | melee = claws | melee+projectile = claws up close, projectile further away | none | custom = the class's own Lua code",
  attack_range = "starts attacking from this distance",
  attack_cooldown = "seconds between attacks (randomised a bit)",
  attack_delay = "wind-up: seconds from the start of the attack animation to the shot/hit",
  attack_time = "how long the attack stops it",
  bullets = "hitscan: pellets per shot",
  spread = "hitscan: cone width (0.05 = tight, 0.2 = shotgun)",
  bullet_damage = "hitscan: damage per pellet",
  projectile = "entity class of the projectile",
  projectile_damage = "damage of a direct hit",
  melee_range = "reach of the melee hit",
  leap = "jumps at the player from mid range (melee attacks)",
  attack_sound = "played when it shoots",
  melee_sound = "played when it hits up close",
  muzzle_flash = "light flash when shooting",
  pain_chance = "0..1 chance to flinch when hit",
  gib_health = "below this health the body bursts into gibs",
  model = "glTF model with idle, walk, attack, pain and death animations",
  scale = "model size multiplier",
  tint = "model colour multiplier (255 255 255 = unchanged)",
}

-- Declares a monster: def.editor.props only lists what differs from the parent class;
-- the editor then shows every built-in AI option (with these defaults) for it.
function rbs.monster(name, def)
  def.base = def.base or "monster_base"
  local parent = rbs.classes[def.base]
  local ped = parent and parent.editor or {}
  local ed = def.editor or {}
  ed.category = ed.category or "monsters"
  ed.props = util.merge(ped.props, ed.props)
  if ed.model and (ed.props.model or "") == "" then ed.props.model = ed.model end
  ed.enums = util.merge(ped.enums, ed.enums)
  ed.help = util.merge(ped.help, ed.help)
  ed.mins = ed.mins or ped.mins
  ed.maxs = ed.maxs or ped.maxs
  ed.color = ed.color or ped.color
  def.editor = ed
  -- class fields follow the props, so code can read self.speed etc. even without the editor
  for k, v in pairs(ed.props) do
    if rawget(def, k) == nil then def[k] = v end
  end
  return rbs.class(name, def)
end

local function play_sound(path, pos, pitch)
  if path and path ~= "" then rbs.sound(path, pos, 1.0, pitch or (0.9 + math.random() * 0.2)) end
end

rbs.class("monster_base", {
  editor = { category = "monsters", color = {255, 130, 30}, mins = {-16, -24, -16}, maxs = {16, 32, 16},
             desc = "Built-in monster AI (base of all monsters).", props = TUNABLES, enums = ENUMS, help = HELP },
  max_hp = 60,

  spawn = function(self)
    util.apply_props(self, TUNABLES)
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

  -- triggered by a trigger/button pointing at it: wakes up (ambushes)
  use = function(self, activator)
    local p = rbs.player()
    if p and not self.dead then self:wake(p) end
  end,

  wake = function(self, enemy)
    if self.state ~= "idle" then return end
    self.enemy = enemy
    self.state = "chase"
    self.next_attack = rbs.time() + 0.5 + math.random() * 0.8
    play_sound(self.sight_sound, self.pos)
    -- wake up the neighbours too
    for _, m in ipairs(rbs.find_radius(self.pos, 500)) do
      if m.monster and not m.dead and m.state == "idle" and m ~= self then m.enemy = enemy; m.state = "chase" end
    end
  end,

  -- how close it must be to start an attack
  reach = function(self)
    local t = self.attack_type
    if t == "none" then return 0 end
    if t == "melee" then return self.leap and 380 or self.melee_range + 16 end
    return self.attack_range
  end,

  update = function(self, dt)
    if self.dead then return end
    local p = rbs.player()
    local t = rbs.time()

    if self.state == "idle" then
      if self.patrol_path ~= "" then self:patrol(dt) end
      if self.wake_on_sight and t > self.next_look then
        self.next_look = t + 0.3
        if p and not p.dead and self:distance(p) < self.sight_range and rbs.visible(self, p) then self:wake(p) end
      end
      return
    end

    if not p or p.dead then
      self.state = "idle"
      self:walk(vec(0, 0, 0), 0)
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

    if self.state == "flee" then
      if t > self.state_until then self.state = "chase" end
      self:move(dt, (self.pos - p.pos):flat():normalize(), self.speed * 1.2, true)
      return
    end

    self:think(dt, p)
  end,

  -- chase state: decides between attacking and moving. Override for your own AI.
  think = function(self, dt, p)
    local t = rbs.time()
    local d = self:distance(p)
    local visible = rbs.visible(self, p)
    if self.flee_health > 0 and self.health < self.flee_health and t > (self.flee_again or 0) then
      self.state = "flee"
      self.state_until = t + 2.5 + math.random()
      self.flee_again = t + 7
      return
    end
    if visible and d < self:reach() and t > self.next_attack then
      self.state = "attack"
      self.attacked = false
      self.attack_at = t + self.attack_delay
      self.state_until = t + self.attack_time
      self:play("attack", false)
      return
    end
    if self.ai == "turret" then
      self:face(p.pos, self.turn_speed, dt)
      self:walk(vec(0, 0, 0), 0)
      self:play("idle", true)
      return
    end
    local dir = (p.pos - self.pos):flat():normalize()
    if self.ai == "ranged" and visible then
      -- hold the distance: back off when too close, strafe while in the sweet spot
      if d < self.keep_distance * 0.7 then
        self:move(dt, -dir, self.speed, false, p)
        return
      elseif d < self.keep_distance * 1.15 then
        if t > (self.strafe_until or 0) then
          self.strafe_dir = math.random() < 0.5 and -1 or 1
          self.strafe_until = t + 0.8 + math.random()
        end
        self:move(dt, dir:cross(vec(0, 1, 0)):normalize() * self.strafe_dir, self.speed * 0.7, false, p)
        return
      end
    end
    local stop = self.ai == "melee" and self.melee_range * 0.8 or self.keep_distance
    if d > stop or not visible then
      self:move(dt, dir, self.speed, true)
    else
      self:walk(dir, 0)
      self:face(p.pos, self.turn_speed, dt)
      self:play("idle", true)
    end
  end,

  -- walks in `dir`, side-stepping obstacles; faces the movement (or `look_at`)
  move = function(self, dt, dir, speed, face_dir, look_at)
    local t = rbs.time()
    if t < self.avoid_until then
      dir = self.avoid_dir
    elseif self.blocked then
      local side = dir:cross(vec(0, 1, 0))
      if math.random() < 0.5 then side = -side end
      self.avoid_dir = (side + dir * 0.3):normalize()
      self.avoid_until = t + 0.4 + math.random() * 0.4
      dir = self.avoid_dir
    end
    if look_at then self:face(look_at.pos, self.turn_speed, dt)
    elseif face_dir then self:face(self.pos + dir, self.turn_speed, dt) end
    self:walk(dir, speed)
    self:play("walk", true, speed / 150)
  end,

  -- idle: follows path_corner waypoints from patrol_path
  patrol = function(self, dt)
    local t = rbs.time()
    if t < (self.patrol_wait or 0) then
      self:walk(vec(0, 0, 0), 0)
      self:play("idle", true)
      return
    end
    if not self.goal or self.goal.removed then
      local name = self.next_corner or self.patrol_path
      self.goal = rbs.find(name)[1]
      if not self.goal then
        self.patrol_path = ""  -- broken path: stand still
        return
      end
    end
    local to = (self.goal.pos - self.pos):flat()
    if to:len() < 24 then
      self.patrol_wait = t + util.num(self.goal.props.wait, 0)
      self.next_corner = self.goal.target ~= "" and self.goal.target or self.patrol_path
      self.goal = nil
      return
    end
    self:move(dt, to:normalize(), self.speed * 0.5, true)
  end,

  -- built-in attacks chosen by attack_type; override attack() for your own
  attack = function(self, enemy)
    local kind = self.attack_type
    local d = self:distance(enemy)
    local close = d <= self.melee_range + 16
    if kind == "melee" or kind == "melee+projectile" then
      if close then
        rbs.damage(enemy, self.melee_damage, self, (enemy.pos - self.pos):normalize())
        play_sound(self.melee_sound, self.pos, 1.0)
        return
      end
      if self.leap and self.on_ground and d < 380 then
        local dir = (enemy.pos - self.pos):flat():normalize()
        self.vel = dir * 480 + vec(0, 260, 0)
        play_sound(self.sight_sound, self.pos, 1.2)
        return
      end
      if kind == "melee" then return end
      kind = "projectile"
    end
    if kind == "hitscan" then
      local origin = self.pos + vec(0, self.maxs.y * 0.55, 0) + self:forward() * 16
      local dir = (enemy.center + vec(0, 8, 0) - origin):normalize()
      play_sound(self.attack_sound, self.pos, 0.95 + math.random() * 0.1)
      rbs.fire_bullets(self, { origin = origin, dir = dir, count = self.bullets, spread = self.spread, damage = self.bullet_damage })
      if self.muzzle_flash then rbs.light(origin, vec(1.4, 0.9, 0.4), 220, 0.08) end
    elseif kind == "projectile" then
      local origin = self.pos + vec(0, self.maxs.y * 0.6, 0) + self:forward() * 12
      play_sound(self.attack_sound, self.pos)
      local p = rbs.projectile(self.projectile, self, origin, (enemy.center - origin):normalize(), self.projectile_speed)
      if p then p.hit_damage = self.projectile_damage end
    end
  end,

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
    play_sound(self.pain_sound, self.pos)
    if self.state ~= "attack" and self.state ~= "flee" and math.random() < self.pain_chance then
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
    play_sound(self.death_sound, self.pos)
    self:play("death", false)
    self.state = "dead"
    self:walk(vec(0, 0, 0), 0)
  end,
})

-- Waypoint for patrolling monsters: give it a targetname, point the monster's patrol_path
-- at it and link the next corner with target. wait = seconds to stand there.
rbs.class("path_corner", {
  editor = { category = "ai", color = {120, 220, 255}, mins = {-8, -8, -8}, maxs = {8, 8, 8},
             desc = "Patrol waypoint. targetname = its name, target = next corner, wait = pause in seconds.",
             props = { targetname = "", target = "", wait = 0 } },
  spawn = function(self)
    self.solid = "not"
    self.visible = false
  end,
})
