-- Player weapons. view_offset = right, up, forward (camera space units).
-- muzzle = muzzle position in model space (for the flash sprite).

rbs.weapon{
  id = "pistol", slot = 1, name = "Pistol",
  model = "models/weapons/v_pistol.glb",
  fire_rate = 0.32, automatic = false, recoil = 1.0,
  view_offset = {7, -7.5, 13}, muzzle = {0, 2.6, 14},
  flash_color = {1.0, 0.75, 0.35}, flash_size = 10,
  fire = function(player)
    rbs.sound("sounds/pistol.wav", nil, 0.8, 0.95 + math.random() * 0.1)
    rbs.fire_bullets(player, { count = 1, spread = 0.012, damage = 14 })
  end,
}

rbs.weapon{
  id = "shotgun", slot = 2, name = "Double-barrel shotgun",
  model = "models/weapons/v_shotgun.glb",
  ammo = "shells", ammo_per_shot = 2, start_ammo = 24,
  fire_rate = 0.95, automatic = true, recoil = 2.2,
  view_offset = {6.5, -8, 12}, muzzle = {0, 2.5, 34},
  flash_color = {1.0, 0.7, 0.3}, flash_size = 26,
  fire = function(player)
    rbs.sound("sounds/shotgun.wav", nil, 1.0, 0.95 + math.random() * 0.1)
    rbs.fire_bullets(player, { count = 22, spread = 0.11, damage = 8 })
    -- a bit of smoke from the barrels
    local eye, dir = rbs.aim()
    rbs.particles(eye + dir * 30, dir, { count = 4, color = {200, 200, 200, 90}, speed = 40, size = 6, life = 0.7,
      gravity = -0.1, drag = 2, growth = 16, texture = "sprites/smoke.png" })
  end,
}

rbs.weapon{
  id = "plasma", slot = 3, name = "Plasma rifle",
  model = "models/weapons/v_plasma.glb",
  ammo = "cells", ammo_per_shot = 1, start_ammo = 60,
  fire_rate = 0.085, automatic = true, recoil = 0.35,
  view_offset = {6.5, -8, 13}, muzzle = {0, 2.8, 30},
  flash_color = {0.4, 0.7, 1.6}, flash_size = 14,
  fire = function(player)
    rbs.sound("sounds/plasma.wav", nil, 0.6, 0.95 + math.random() * 0.1)
    local eye, dir = rbs.aim()
    local right = dir:cross(vec(0, 1, 0)):normalize()
    rbs.projectile("plasma_bolt", player, eye + right * 4 - vec(0, 4, 0) + dir * 12, dir, 1900)
  end,
}

rbs.weapon{
  id = "rocket", slot = 4, name = "Rocket launcher",
  model = "models/weapons/v_rocket.glb",
  ammo = "rockets", ammo_per_shot = 1, start_ammo = 8,
  fire_rate = 0.8, automatic = true, recoil = 2.5,
  view_offset = {7, -8.5, 12}, muzzle = {0, 3, 30},
  flash_color = {1.2, 0.6, 0.2}, flash_size = 22,
  fire = function(player)
    rbs.sound("sounds/rocket_fire.wav", nil, 1.0)
    local eye, dir = rbs.aim()
    local right = dir:cross(vec(0, 1, 0)):normalize()
    rbs.projectile("rocket", player, eye + right * 5 - vec(0, 5, 0) + dir * 14, dir, 1000)
  end,
}
