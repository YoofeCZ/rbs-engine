-- Fiend: fast melee demon that leaps and throws fireballs from range.
rbs.class("monster_fiend", {
  base = "monster_base",
  editor = { category = "monsters", color = {200, 30, 30}, mins = {-16, -24, -16}, maxs = {16, 28, 16},
             model = "models/monsters/fiend.glb", desc = "Leaping demon, melee + fireballs.", props = { health = 90 } },
  max_hp = 90,
  speed = 230,
  attack_range = 700,
  keep_distance = 40,
  attack_cooldown = 1.1,
  attack_delay = 0.25,
  attack_time = 0.6,
  sight_sound = "sounds/fiend_sight.wav",
  pain_sound = "sounds/fiend_pain.wav",
  death_sound = "sounds/fiend_death.wav",
  spawn = function(self)
    self.model = "models/monsters/fiend.glb"
    rbs.classes.monster_base.spawn(self)
    self.maxs = vec(16, 28, 16)
  end,
  attack = function(self, enemy)
    local d = self:distance(enemy)
    if d < 80 then
      rbs.damage(enemy, 15, self, (enemy.pos - self.pos):normalize())
      rbs.sound("sounds/fiend_claw.wav", self.pos, 1.0)
    elseif d < 380 and self.on_ground then
      local dir = (enemy.pos - self.pos):flat():normalize()
      self.vel = dir * 480 + vec(0, 260, 0)
      rbs.sound("sounds/fiend_sight.wav", self.pos, 0.8, 1.2)
    else
      local origin = self.pos + vec(0, 20, 0)
      rbs.projectile("fireball", self, origin, (enemy.center - origin):normalize(), 700)
    end
  end,
})
