-- Player weapons.
--
-- A weapon is plain data, so new ones can be made without code in the editor (Project >
-- Weapons > New weapon, saved as weapons/<id>.json). Fields:
--   slot, name, model (in-hand model), pickup_model, ammo (type name), ammo_per_shot,
--   start_ammo, max_ammo, fire_rate (seconds between shots), automatic, recoil,
--   shot = "hitscan" (bullets, spread, damage per bullet) or "projectile" (projectile
--   class, projectile_speed, projectile_damage), sound, volume, smoke,
--   view_offset = right, up, forward (camera space units), view_rotation, view_scale,
--   muzzle (flash position in model space), flash_color, flash_size.
-- For anything else give the weapon your own fire(player, weapon) function in Lua.

-- What the editor shows for weapon fields (Project > Weapons).
rbs.weapon_fields = {
  enums = { shot = {"hitscan", "projectile"}, projectile = {"plasma_bolt", "rocket", "fireball"} },
  help = {
    slot = "number key that selects it (weapons in one slot cycle)",
    name = "shown in the HUD and pickup message",
    model = "in-hand model (glTF, barrel along +Z)",
    pickup_model = "model of the pickup lying in the level",
    ammo = "ammo type name (shells, cells, rockets or your own); empty = unlimited",
    ammo_per_shot = "ammo used per shot",
    start_ammo = "ammo given with the weapon",
    max_ammo = "most ammo of this type the player can carry",
    fire_rate = "seconds between shots",
    automatic = "keeps firing while the button is held",
    recoil = "camera kick",
    shot = "hitscan = instant bullets, projectile = flying projectile",
    bullets = "hitscan: pellets per shot",
    spread = "hitscan: cone width (0.01 = precise, 0.12 = shotgun)",
    damage = "hitscan: damage per pellet",
    projectile = "projectile: entity class it fires",
    projectile_speed = "projectile: speed",
    projectile_damage = "projectile: damage of a direct hit",
    sound = "sound played when firing",
    volume = "volume of the sound",
    smoke = "puff of smoke at the muzzle",
    view_offset = "position in view: right, up, forward",
    view_rotation = "extra rotation of the in-hand model (degrees)",
    view_scale = "size of the in-hand model",
    muzzle = "muzzle flash position in model space",
    flash_color = "muzzle flash light colour",
    flash_size = "muzzle flash size",
  },
}

-- The built-in shot, driven by the weapon's fields.
function rbs.fire_weapon(player, w)
  if w.sound and w.sound ~= "" then rbs.sound(w.sound, nil, w.volume or 1.0, 0.95 + math.random() * 0.1) end
  local eye, dir = rbs.aim()
  if w.shot == "projectile" then
    local right = dir:cross(vec(0, 1, 0)):normalize()
    local p = rbs.projectile(w.projectile or "plasma_bolt", player, eye + right * 4 - vec(0, 4, 0) + dir * 12, dir,
                             w.projectile_speed or 1200)
    if p and w.projectile_damage then p.hit_damage = w.projectile_damage end
  else
    rbs.fire_bullets(player, { count = w.bullets or 1, spread = w.spread or 0.02, damage = w.damage or 10 })
  end
  if w.smoke then
    rbs.particles(eye + dir * 30, dir, { count = 4, color = {200, 200, 200, 90}, speed = 40, size = 6, life = 0.7,
      gravity = -0.1, drag = 2, growth = 16, texture = "sprites/smoke.png" })
  end
end

rbs.weapon{
  id = "pistol", slot = 1, name = "Pistol",
  model = "models/weapons/v_pistol.glb", pickup_model = "models/weapons/w_pistol.glb",
  fire_rate = 0.32, automatic = false, recoil = 1.0,
  shot = "hitscan", bullets = 1, spread = 0.012, damage = 14, sound = "sounds/pistol.wav", volume = 0.8,
  view_offset = {6, -6.5, 15}, view_rotation = {0, 4, 0}, view_scale = 1.0, muzzle = {0, 2.6, 14},
  flash_color = {1.0, 0.75, 0.35}, flash_size = 10,
}

rbs.weapon{
  id = "shotgun", slot = 2, name = "Double-barrel shotgun",
  model = "models/weapons/v_shotgun.glb", pickup_model = "models/weapons/w_shotgun.glb",
  ammo = "shells", ammo_per_shot = 2, start_ammo = 24, max_ammo = 60,
  fire_rate = 0.95, automatic = true, recoil = 2.2,
  shot = "hitscan", bullets = 22, spread = 0.11, damage = 8, sound = "sounds/shotgun.wav", smoke = true,
  view_offset = {6.5, -8, 12}, view_rotation = {0, 2.5, 0}, muzzle = {0, 2.5, 34},
  flash_color = {1.0, 0.7, 0.3}, flash_size = 26,
}

rbs.weapon{
  id = "plasma", slot = 3, name = "Plasma rifle",
  model = "models/weapons/v_plasma.glb", pickup_model = "models/weapons/w_plasma.glb",
  ammo = "cells", ammo_per_shot = 1, start_ammo = 60, max_ammo = 300,
  fire_rate = 0.085, automatic = true, recoil = 0.35,
  shot = "projectile", projectile = "plasma_bolt", projectile_speed = 1900, projectile_damage = 18,
  sound = "sounds/plasma.wav", volume = 0.6,
  view_offset = {8, -8.5, 12}, view_rotation = {0, 3, 0}, view_scale = 0.9, muzzle = {0, 2.8, 30},
  flash_color = {0.4, 0.7, 1.6}, flash_size = 14,
}

rbs.weapon{
  id = "rocket", slot = 4, name = "Rocket launcher",
  model = "models/weapons/v_rocket.glb", pickup_model = "models/weapons/w_rocket.glb",
  ammo = "rockets", ammo_per_shot = 1, start_ammo = 8, max_ammo = 30,
  fire_rate = 0.8, automatic = true, recoil = 2.5,
  shot = "projectile", projectile = "rocket", projectile_speed = 1000, projectile_damage = 80,
  sound = "sounds/rocket_fire.wav",
  view_offset = {8.5, -9, 11}, view_rotation = {0, 3, 0}, view_scale = 0.9, muzzle = {0, 3, 30},
  flash_color = {1.2, 0.6, 0.2}, flash_size = 22,
}
