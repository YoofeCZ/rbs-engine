-- Reusable components (attach them to any entity in the editor Inspector -> Add Component).
-- A component gets callbacks with (component, entity, ...) and its props as fields.

rbs.component("rotator", {
  editor = { desc = "Spins the entity around the vertical axis.", props = { speed = 90 } },
  update = function(c, ent, dt) ent.yaw = ent.yaw + c.speed * dt end,
})

rbs.component("bobber", {
  editor = { desc = "Moves the entity up and down.", props = { height = 8, speed = 2 } },
  start = function(c, ent) c.base_y = ent.pos.y; c.phase = math.random() * 6 end,
  update = function(c, ent, dt)
    local p = ent.pos
    p.y = c.base_y + math.sin(rbs.time() * c.speed + c.phase) * c.height
    ent.pos = p
  end,
})

rbs.component("flicker_light", {
  editor = { desc = "Attaches a flickering dynamic light.", props = { color = {255, 180, 90}, radius = 250, intensity = 1.2 } },
  start = function(c, ent)
    local col = util.color(c.color)
    c.base = vec(col[1], col[2], col[3]) * (c.intensity / 255)
    ent.light_radius = c.radius
  end,
  update = function(c, ent)
    local t = rbs.time() * 17 + ent.id
    ent.light_color = c.base * (0.8 + 0.2 * math.sin(t) * math.sin(t * 2.3))
  end,
})

rbs.component("destructible", {
  editor = { desc = "Gives the entity health; explodes or breaks when destroyed and fires `target`.",
             props = { health = 30, explode = 1, radius = 140, damage = 80, target = "" } },
  start = function(c, ent)
    ent.takedamage = true
    ent.health = c.health
    if ent.solid == "not" then ent.solid = "bbox" end
  end,
  die = function(c, ent, attacker)
    if c.explode ~= 0 then rbs.explosion(ent.center, c.radius, c.damage, attacker) end
    rbs.fire_targets(c.target, attacker)
    rbs.remove(ent)
  end,
})

rbs.component("fire_on_death", {
  editor = { desc = "Fires a target when this entity dies (monster closets, arenas...).", props = { target = "" } },
  die = function(c, ent, attacker) rbs.fire_targets(c.target, attacker) end,
})

rbs.component("hurt_on_touch", {
  editor = { desc = "Damages the player on contact.", props = { damage = 10, interval = 0.5 } },
  update = function(c, ent)
    local p = rbs.player()
    if not p or p.dead or rbs.time() < (c.next or 0) then return end
    if (p.pos - ent.pos):len() < 40 then
      c.next = rbs.time() + c.interval
      rbs.damage(p, c.damage, ent)
    end
  end,
})

rbs.component("look_at_player", {
  editor = { desc = "Keeps facing the player (turrets, statues, cameras).", props = { turn_speed = 180 } },
  update = function(c, ent, dt)
    local p = rbs.player()
    if p then ent:face(p.pos, c.turn_speed, dt) end
  end,
})

rbs.component("spawner", {
  editor = { desc = "When used, spawns `class` here (count times, every `interval` seconds).",
             props = { class = "monster_grunt", count = 3, interval = 1.5 } },
  use = function(c, ent) c.left = c.count; c.next = rbs.time() end,
  update = function(c, ent)
    if (c.left or 0) > 0 and rbs.time() >= c.next then
      c.left = c.left - 1
      c.next = rbs.time() + c.interval
      local m = rbs.spawn(c.class, ent.pos, {}, ent.yaw)
      if m and m.wake then m:wake(rbs.player()) end
      rbs.particles(ent.pos, vec(0, 1, 0), { count = 20, color = {255, 255, 255}, speed = 150, size = 4, life = 0.5, additive = true })
      rbs.sound("sounds/teleport.wav", ent.pos, 1.0)
    end
  end,
})
