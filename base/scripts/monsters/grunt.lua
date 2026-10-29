-- Grunt: armoured soldier with a machine gun (hitscan). Pure data on top of the
-- built-in AI - see monsters/base.lua for every option.
rbs.monster("monster_grunt", {
  editor = { color = {255, 130, 30}, mins = {-16, -24, -16}, maxs = {16, 32, 16},
             model = "models/monsters/grunt.glb", desc = "Soldier with a machine gun.",
             props = { health = 60, speed = 140, attack_range = 1100, attack_delay = 0.3, attack_time = 0.9,
                       attack_type = "hitscan", bullets = 4, spread = 0.07, bullet_damage = 3,
                       attack_sound = "sounds/grunt_fire.wav" } },
  max_hp = 60,
})

-- Heavy: bigger, slower, much more health, three bursts at once.
rbs.monster("monster_heavy", {
  base = "monster_grunt",
  editor = { color = {200, 80, 20}, mins = {-20, -24, -20}, maxs = {20, 44, 20},
             model = "models/monsters/grunt.glb", desc = "Heavy gunner (tough).",
             props = { health = 220, speed = 100, attack_time = 1.4, pain_chance = 0.25, gib_health = -80, bullets = 12,
                       scale = 1.3, tint = {150, 160, 140} } },
  max_hp = 220,
})
