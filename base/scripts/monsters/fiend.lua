-- Fiend: fast demon - claws up close, leaps from mid range, throws fireballs from afar.
-- Pure data on top of the built-in AI (monsters/base.lua).
rbs.monster("monster_fiend", {
  editor = { color = {200, 30, 30}, mins = {-16, -24, -16}, maxs = {16, 28, 16},
             model = "models/monsters/fiend.glb", desc = "Leaping demon, melee + fireballs.",
             props = { animator = "animators/fiend.json", health = 90, ai = "melee", speed = 230, attack_range = 700, keep_distance = 40,
                       attack_cooldown = 1.1, attack_delay = 0.25, attack_time = 0.6,
                       attack_type = "melee+projectile", melee_damage = 15, leap = true,
                       projectile = "fireball", projectile_speed = 700, projectile_damage = 15,
                       melee_sound = "sounds/fiend_claw.wav",
                       sight_sound = "sounds/fiend_sight.wav", pain_sound = "sounds/fiend_pain.wav",
                       death_sound = "sounds/fiend_death.wav" } },
  max_hp = 90,
})
