-- Pickups: health, armor, ammo, weapons, keys.

local function item(name, def)
  def.base = "item_base"
  def.editor = def.editor or {}
  def.editor.category = def.editor.category or "items"
  def.editor.mins = def.editor.mins or {-16, -16, -16}
  def.editor.maxs = def.editor.maxs or {16, 16, 16}
  def.editor.model = def.model
  return rbs.class(name, def)
end

rbs.class("item_base", {
  spawn = function(self)
    self.solid = "trigger"
    self.mins = vec(-16, -16, -16)
    self.maxs = vec(16, 16, 16)
    self.model = self.model or rawget(self.__class, "model") or ""
    self.spin = 90
    self.bob = 4
    rbs.add_stat("items_total")
  end,
  touch = function(self, other)
    if not util.is_player(other) or other.dead or self.taken then return end
    if self:pickup(other) == false then return end
    self.taken = true
    rbs.sound(self.sound or "sounds/pickup.wav", nil, 0.8)
    rbs.flash(0.35)
    if self.message then rbs.message(self.message, 2) end
    rbs.add_stat("items")
    rbs.remove(self)
  end,
})

item("item_health", {
  model = "models/items/medkit.glb", message = "Medkit +25",
  editor = { color = {80, 255, 80}, desc = "Health +25 (amount prop)", props = { amount = 25 } },
  pickup = function(self, p)
    if p.health >= p.max_health then return false end
    p.health = math.min(p.max_health, p.health + util.num(self.props.amount, 25))
  end,
})

item("item_health_mega", {
  model = "models/items/medkit.glb", message = "MEGA HEALTH!",
  editor = { color = {80, 255, 200}, desc = "Health +100, can go over 100" },
  spawn = function(self) item_base_spawn(self); self.scale = 1.5; self.tint = {r = 120, g = 200, b = 255, a = 255} end,
  pickup = function(self, p) p.health = math.min(200, p.health + 100) end,
})

item("item_armor", {
  model = "models/items/armor.glb", message = "Armor",
  editor = { color = {80, 160, 255}, desc = "Armor +50", props = { amount = 50 } },
  pickup = function(self, p)
    if p.armor >= 200 then return false end
    p.armor = math.min(200, p.armor + util.num(self.props.amount, 50))
  end,
})

item("item_shells", {
  model = "models/items/shells.glb", message = "Shells",
  editor = { color = {255, 160, 60}, desc = "Shotgun shells", props = { amount = 12 } },
  pickup = function(self)
    if rbs.ammo("shells") >= rbs.max_ammo("shells") then return false end
    rbs.give_ammo("shells", util.num(self.props.amount, 12))
  end,
})

item("item_cells", {
  model = "models/items/cells.glb", message = "Plasma cells",
  editor = { color = {80, 200, 255}, desc = "Plasma cells", props = { amount = 40 } },
  pickup = function(self)
    if rbs.ammo("cells") >= rbs.max_ammo("cells") then return false end
    rbs.give_ammo("cells", util.num(self.props.amount, 40))
  end,
})

item("item_rockets", {
  model = "models/items/rockets.glb", message = "Rockets",
  editor = { color = {255, 100, 60}, desc = "Rockets", props = { amount = 5 } },
  pickup = function(self)
    if rbs.ammo("rockets") >= rbs.max_ammo("rockets") then return false end
    rbs.give_ammo("rockets", util.num(self.props.amount, 5))
  end,
})

local function weapon_item(name, weapon, model, label)
  item(name, {
    model = model, message = label, sound = "sounds/weapon_pickup.wav",
    editor = { color = {255, 220, 60}, desc = "Weapon pickup: " .. label },
    pickup = function(self) rbs.give_weapon(weapon, not rbs.has_weapon(weapon)) end,
  })
end
weapon_item("weapon_shotgun", "shotgun", "models/weapons/w_shotgun.glb", "Double-barrel shotgun")
weapon_item("weapon_plasma", "plasma", "models/weapons/w_plasma.glb", "Plasma rifle")
weapon_item("weapon_rocket", "rocket", "models/weapons/w_rocket.glb", "Rocket launcher")

local function key_item(name, key, color, label)
  item(name, {
    model = "models/items/keycard.glb", message = label,
    editor = { color = color, desc = label },
    spawn = function(self)
      item_base_spawn(self)
      self.tint = {r = color[1], g = color[2], b = color[3], a = 255}
      self.fullbright = true
      self.light_color = vec(color[1], color[2], color[3]) / 400
      self.light_radius = 120
    end,
    pickup = function(self) rbs.set_key(key) end,
  })
end
key_item("key_red", "red", {255, 60, 50}, "Red keycard")
key_item("key_blue", "blue", {60, 120, 255}, "Blue keycard")
key_item("key_yellow", "yellow", {255, 220, 40}, "Yellow keycard")

-- helper so subclasses can call the base spawn explicitly
function item_base_spawn(self) rbs.classes.item_base.spawn(self) end
