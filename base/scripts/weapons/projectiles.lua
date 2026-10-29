-- Projectiles fired by weapons and monsters.

rbs.class("plasma_bolt", {
  spawn = function(self)
    self.movetype = "fly"
    self.solid = "not"
    self.mins = vec(-2, -2, -2)
    self.maxs = vec(2, 2, 2)
    self.sprite = "sprites/plasma.png"
    self.sprite_size = 18
    self.sprite_additive = true
    self.fullbright = true
    self.light_color = vec(0.3, 0.6, 1.6)
    self.light_radius = 160
    self.damage = 18
    self.die_at = rbs.time() + 4
  end,
  update = function(self)
    if rbs.time() > self.die_at then rbs.remove(self) end
  end,
  touch = function(self, other, normal)
    if other and other.takedamage then
      rbs.damage(other, self.damage, self.owner, self.vel:normalize())
      if other.monster then rbs.blood(self.pos, normal, 6) end
    else
      rbs.decal(self.pos + normal * 2, self.pos - normal * 8, "decals/scorch.png", 14, {r = 60, g = 120, b = 255, a = 200})
    end
    rbs.particles(self.pos + normal * 2, normal, { count = 10, color = {120, 190, 255}, speed = 160, size = 3, life = 0.35,
      gravity = 0.3, additive = true, texture = "sprites/spark.png" })
    rbs.light(self.pos + normal * 6, vec(0.4, 0.7, 1.8), 140, 0.15)
    rbs.sound("sounds/plasma_hit.wav", self.pos, 0.6, 0.9 + math.random() * 0.2)
    rbs.remove(self)
  end,
})

rbs.class("rocket", {
  spawn = function(self)
    self.movetype = "fly"
    self.solid = "not"
    self.mins = vec(-3, -3, -3)
    self.maxs = vec(3, 3, 3)
    self.model = "models/weapons/rocket_proj.glb"
    self.fullbright = true
    self.light_color = vec(1.5, 0.8, 0.3)
    self.light_radius = 180
    self.die_at = rbs.time() + 8
  end,
  update = function(self, dt)
    if rbs.time() > self.die_at then rbs.remove(self) return end
    rbs.particles(self.pos, self.vel * -0.001, { count = 1, color = {120, 120, 120, 180}, speed = 10, size = 8, size_var = 2,
      life = 0.9, gravity = -0.1, drag = 1, growth = 18, texture = "sprites/smoke.png" })
  end,
  touch = function(self, other, normal)
    if other and other.takedamage then rbs.damage(other, 80, self.owner, self.vel:normalize()) end
    rbs.explosion(self.pos + normal * 4, 150, 110, self.owner)
    rbs.remove(self)
  end,
})

-- Fireball thrown by fiends
rbs.class("fireball", {
  spawn = function(self)
    self.movetype = "fly"
    self.solid = "not"
    self.mins = vec(-3, -3, -3)
    self.maxs = vec(3, 3, 3)
    self.sprite = "sprites/fire.png"
    self.sprite_size = 26
    self.sprite_additive = true
    self.fullbright = true
    self.light_color = vec(1.6, 0.6, 0.2)
    self.light_radius = 150
    self.die_at = rbs.time() + 6
  end,
  update = function(self)
    if rbs.time() > self.die_at then rbs.remove(self) return end
    rbs.particles(self.pos, vec(0, 1, 0), { count = 1, color = {255, 140, 50}, speed = 20, size = 10, life = 0.3,
      gravity = -0.2, additive = true, growth = -10, texture = "sprites/fire.png" })
  end,
  touch = function(self, other, normal)
    if other and other.takedamage then rbs.damage(other, 15, self.owner, self.vel:normalize()) end
    rbs.particles(self.pos, normal, { count = 14, color = {255, 150, 50}, speed = 150, size = 6, life = 0.4,
      gravity = 0.2, additive = true, texture = "sprites/fire.png" })
    rbs.light(self.pos, vec(1.6, 0.7, 0.2), 160, 0.2)
    rbs.decal(self.pos + normal * 2, self.pos - normal * 8, "decals/scorch.png", 24)
    rbs.sound("sounds/fireball_hit.wav", self.pos, 0.8)
    rbs.remove(self)
  end,
})
