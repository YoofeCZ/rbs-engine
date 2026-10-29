-- Built-in point entities: spawn points, lights, sprites, sounds, cameras, props.

rbs.class("player_start", {
  editor = { category = "player", color = {40, 220, 90}, mins = {-16, -24, -16}, maxs = {16, 32, 16},
             desc = "Where the player starts. The arrow shows the view direction." },
})

rbs.class("info_null", {
  editor = { category = "misc", color = {160, 160, 160}, mins = {-6, -6, -6}, maxs = {6, 6, 6},
             desc = "Invisible marker (targets, waypoints, script positions).", props = { targetname = "" } },
})

-- Lights are baked into lightmaps. Set "dynamic" to 1 for a real-time light, "style"
-- for flickering ("flicker", "pulse", "strobe") - those are always dynamic.
rbs.class("light", {
  editor = { category = "lights", color = {255, 220, 90}, mins = {-8, -8, -8}, maxs = {8, 8, 8},
             desc = "Point light (baked). spot > 0 makes a spotlight (cone angle, aimed by angle + pitch).",
             props = { color = {255, 220, 180}, intensity = 1.0, radius = 300, spot = 0, pitch = -90, dynamic = 0, style = "" } },
  spawn = function(self)
    local p = self.props
    local style = p.style or ""
    if util.num(p.dynamic, 0) ~= 0 or style ~= "" then
      local c = util.color(p.color)
      local k = util.num(p.intensity, 1) / 255
      self.base_color = vec(c[1] * k, c[2] * k, c[3] * k)
      self.light_color = self.base_color
      self.light_radius = util.num(p.radius, 300)
      self.style = style
      self.phase = math.random() * 10
    end
  end,
  update = function(self, dt)
    if not self.style or self.style == "" then return end
    local t = rbs.time() + self.phase
    local f = 1
    if self.style == "flicker" then
      f = 0.75 + 0.25 * math.sin(t * 23) * math.sin(t * 7.3) + (math.random() < 0.03 and -0.5 or 0)
    elseif self.style == "pulse" then
      f = 0.6 + 0.4 * math.sin(t * 3)
    elseif self.style == "strobe" then
      f = (t * 4) % 1 < 0.5 and 1 or 0.1
    end
    self.light_color = self.base_color * math.max(0, f)
  end,
})
rbs.class("light_spot", { base = "light",
  editor = { category = "lights", color = {255, 200, 60}, mins = {-8, -8, -8}, maxs = {8, 8, 8},
             desc = "Spotlight (baked).", props = { color = {255, 230, 200}, intensity = 1.4, radius = 500, spot = 50, pitch = -90 } } })

rbs.class("sprite", {
  editor = { category = "effects", color = {255, 150, 220}, mins = {-8, -8, -8}, maxs = {8, 8, 8},
             desc = "Camera facing sprite (glows, fire, smoke). frames > 1 = horizontal sprite sheet.",
             props = { texture = "sprites/flare.png", size = 32, additive = 1, frames = 1, fps = 10, color = {255, 255, 255}, light = 0 } },
  spawn = function(self)
    local p = self.props
    self.sprite = p.texture or "sprites/flare.png"
    self.sprite_size = util.num(p.size, 32)
    self.sprite_additive = util.num(p.additive, 1) ~= 0
    self.fullbright = true
    local c = util.color(p.color)
    self.tint = {r = c[1], g = c[2], b = c[3], a = 255}
    if util.num(p.light, 0) > 0 then
      self.light_color = vec(c[1], c[2], c[3]) / 255
      self.light_radius = util.num(p.light, 0)
    end
  end,
})

rbs.class("fire", { base = "sprite",
  editor = { category = "effects", color = {255, 120, 30}, mins = {-12, 0, -12}, maxs = {12, 24, 12},
             desc = "Burning fire with light and crackle sound.", props = { size = 48 } },
  spawn = function(self)
    self.sprite = "sprites/fire_anim.png"
    self.sprite_size = util.num(self.props.size, 48)
    self.sprite_additive = true
    self.fullbright = true
    self.props = { frames = 8, fps = 14 }
    self.base_radius = self.sprite_size * 5
    self.light_color = vec(1.4, 0.7, 0.25)
    self.light_radius = self.base_radius
    self.loop_sound = "sounds/fire_loop.wav"
    self.loop_volume = 0.6
    self.loop_radius = 500
  end,
  update = function(self, dt)
    local t = rbs.time() * 9 + self.id
    self.light_radius = self.base_radius * (0.85 + 0.15 * math.sin(t) * math.sin(t * 1.7))
    if math.random() < dt * 6 then
      rbs.particles(self.pos + vec(0, self.sprite_size * 0.3, 0), vec(0, 1, 0),
        { count = 1, color = {255, 160, 60}, speed = 60, spread = 0.3, size = 2, life = 0.8, gravity = -0.3, additive = true, texture = "sprites/spark.png" })
    end
  end,
})

rbs.class("ambient_sound", {
  editor = { category = "sound", color = {90, 200, 255}, mins = {-8, -8, -8}, maxs = {8, 8, 8},
             desc = "Looping positional sound.", props = { sound = "sounds/hum_loop.wav", volume = 0.6, radius = 600 } },
  spawn = function(self)
    self.loop_sound = self.props.sound or ""
    self.loop_volume = util.num(self.props.volume, 0.6)
    self.loop_radius = util.num(self.props.radius, 600)
  end,
})

rbs.class("mission_camera", {
  editor = { category = "misc", color = {120, 255, 255}, mins = {-8, -8, -8}, maxs = {8, 8, 8},
             desc = "Security camera shown picture-in-picture when triggered (rbs.show_camera or a target).",
             props = { targetname = "cam1", pitch = -15, time = 5 } },
  spawn = function(self) self.pitch = util.num(self.props.pitch, -15) end,
  use = function(self) rbs.show_camera(self.targetname, util.num(self.props.time, 5)) end,
})

-- Generic 3D model in the level (props, decoration, furniture...).
rbs.class("prop_model", {
  editor = { category = "props", color = {200, 160, 255}, mins = {-16, 0, -16}, maxs = {16, 32, 16},
             desc = "A 3D model placed in the level. Drag a model from the Project panel to create one.",
             props = { model = "models/props/barrel.glb", scale = 1, solid = 1, anim = "", animator = "" } },
  spawn = function(self)
    local p = self.props
    self.model = p.model or ""
    self.scale = util.num(p.scale, 1)
    local mn, mx = rbs.model_bounds(self.model)
    if mn then
      self.mins = vec(mn.x * self.scale, 0, mn.z * self.scale)
      self.maxs = vec(mx.x * self.scale, (mx.y - mn.y) * self.scale, mx.z * self.scale)
    end
    if util.num(p.solid, 1) ~= 0 then self.solid = "bbox" end
    if p.anim and p.anim ~= "" then self.anim = p.anim end
  end,
})

-- Exploding barrel: shoot it!
rbs.class("prop_barrel", {
  editor = { category = "props", color = {255, 90, 40}, mins = {-14, 0, -14}, maxs = {14, 40, 14},
             model = "models/props/barrel.glb", desc = "Explosive barrel.", props = { health = 20 } },
  spawn = function(self)
    self.model = "models/props/barrel.glb"
    self.solid = "bbox"
    self.takedamage = true
    self.health = util.num(self.props.health, 20)
    self.movetype = "walk"
  end,
  die = function(self, attacker)
    self.takedamage = false
    self.visible = false
    self.solid = "not"
    local pos = self.center
    rbs.explosion(pos, 160, 120, attacker)
    rbs.remove(self)
  end,
})

-- Removes itself after `delay` seconds and fires its target (timed events).
rbs.class("target_delay", {
  editor = { category = "logic", color = {200, 200, 90}, desc = "When used, waits `delay` seconds then fires `target`.",
             props = { targetname = "", target = "", delay = 1 } },
  use = function(self, activator)
    self.fire_at = rbs.time() + util.num(self.props.delay, 1)
    self.activator = activator
  end,
  update = function(self)
    if self.fire_at and rbs.time() >= self.fire_at then
      self.fire_at = nil
      rbs.fire_targets(self.target, self.activator)
    end
  end,
})

rbs.class("target_message", {
  editor = { category = "logic", color = {200, 200, 90}, desc = "Shows a message when used.",
             props = { targetname = "", message = "Hello!", time = 3 } },
  use = function(self) rbs.message(self.props.message or "", util.num(self.props.time, 3)) end,
})

rbs.class("target_sound", {
  editor = { category = "sound", color = {90, 200, 255}, desc = "Plays a sound when used.",
             props = { targetname = "", sound = "sounds/objective.wav", volume = 1, global = 0 } },
  use = function(self)
    if util.num(self.props.global, 0) ~= 0 then rbs.sound(self.props.sound, nil, util.num(self.props.volume, 1))
    else rbs.sound(self.props.sound, self.pos, util.num(self.props.volume, 1)) end
  end,
})

rbs.class("info_objective", {
  editor = { category = "logic", color = {255, 255, 120}, desc = "Mission goal. Added at map start, completed when used.",
             props = { targetname = "", id = "goal1", text = "Find the exit" } },
  spawn = function(self) rbs.objective(self.props.id or self.targetname, self.props.text or "", false) end,
  use = function(self)
    rbs.objective(self.props.id or self.targetname, "", true)
    rbs.message("Objective complete", 2)
  end,
})

-- Gibs spawned by rbs.gib()
rbs.class("gib", {
  spawn = function(self)
    local kind = self.props.kind or "small"
    if kind == "head" then self.model = "models/gibs/head.glb"
    elseif kind == "chunk" then self.model = "models/gibs/chunk.glb"
    else self.model = util.choose({"models/gibs/small1.glb", "models/gibs/small2.glb"}) end
    self.mins = vec(-4, -2, -4)
    self.maxs = vec(4, 6, 4)
    self.movetype = "bounce"
    self.bounce = 0.35
    self.solid = "not"
    self.angles = vec(math.random(0, 360), math.random(0, 360), math.random(0, 360))
    self.spin_speed = math.random(-500, 500)
    self.die_at = rbs.time() + 12 + math.random() * 6
    self.splats = 0
  end,
  update = function(self, dt)
    if not self.on_ground then
      self.angles = self.angles + vec(self.spin_speed * dt, self.spin_speed * 0.7 * dt, 0)
      if math.random() < dt * 25 then
        rbs.particles(self.pos, vec(0, 0, 0), { count = 1, color = {120, 0, 0, 255}, speed = 10, size = 2, life = 0.8,
          gravity = 1, blood = true, texture = "sprites/blood_drop.png" })
      end
    end
    if rbs.time() > self.die_at then rbs.remove(self) end
  end,
  touch = function(self, other, normal)
    if self.splats < 2 and other == nil then
      self.splats = self.splats + 1
      rbs.decal(self.pos + normal * 4, self.pos - normal * 16, "decals/blood" .. math.random(1, 4) .. ".png", 18 + math.random() * 16)
      rbs.sound("sounds/gib_bounce.wav", self.pos, 0.4, 0.9 + math.random() * 0.2)
    end
  end,
})
