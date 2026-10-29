-- Grunt: armoured soldier with a machine gun (hitscan).
rbs.class("monster_grunt", {
  base = "monster_base",
  editor = { category = "monsters", color = {255, 130, 30}, mins = {-16, -24, -16}, maxs = {16, 32, 16},
             model = "models/monsters/grunt.glb", desc = "Soldier with a machine gun.", props = { health = 60 } },
  max_hp = 60,
  speed = 140,
  attack_range = 1100,
  attack_delay = 0.3,
  attack_time = 0.9,
  spawn = function(self)
    self.model = "models/monsters/grunt.glb"
    rbs.classes.monster_base.spawn(self)
  end,
  attack = function(self, enemy)
    local origin = self.pos + vec(0, 18, 0) + self:forward() * 16
    local dir = (enemy.center + vec(0, 8, 0) - origin):normalize()
    rbs.sound("sounds/grunt_fire.wav", self.pos, 1.0, 0.95 + math.random() * 0.1)
    rbs.fire_bullets(self, { origin = origin, dir = dir, count = 4, spread = 0.07, damage = 3 })
    rbs.light(origin, vec(1.4, 0.9, 0.4), 220, 0.08)
  end,
})

-- Heavy: bigger, slower, much more health.
rbs.class("monster_heavy", {
  base = "monster_grunt",
  editor = { category = "monsters", color = {200, 80, 20}, mins = {-20, -24, -20}, maxs = {20, 44, 20},
             model = "models/monsters/grunt.glb", desc = "Heavy gunner (tough).", props = { health = 220 } },
  max_hp = 220,
  speed = 100,
  attack_time = 1.4,
  pain_chance = 0.25,
  gib_health = -80,
  spawn = function(self)
    rbs.classes.monster_grunt.spawn(self)
    self.mins = vec(-20, -24, -20)
    self.maxs = vec(20, 44, 20)
    self.scale = 1.3
    self.tint = {r = 150, g = 160, b = 140, a = 255}
  end,
  attack = function(self, enemy)
    for i = 0, 2 do
      rbs.classes.monster_grunt.attack(self, enemy)
    end
  end,
})
