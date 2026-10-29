-- Pickups: health, armor, ammo, weapons, keys.
--
-- Every pickup has these properties, editable per placed item or for a whole item type
-- made with Project > Entities > New type: model, scale, tint, message (shown when taken),
-- pickup_sound and respawn. The kinds below add their own (amount, ammo, weapon...).

local ITEM_PROPS = { model = "", scale = 1, tint = {255, 255, 255}, message = "", pickup_sound = "sounds/pickup.wav", respawn = 0 }
local ITEM_HELP = {
  model = "glTF model shown spinning in the level",
  scale = "model size multiplier",
  tint = "model colour multiplier (255 255 255 = unchanged)",
  message = "text shown when it is picked up",
  pickup_sound = "sound played when it is picked up",
  respawn = "seconds until it comes back (0 = never)",
  amount = "how much it gives",
  max = "it never raises the value above this",
  ammo = "ammo type: shells, cells, rockets or any name your weapons use",
  weapon = "id of the weapon it gives (see Project > Weapons)",
}

-- Declares a pickup: def.model / def.message become defaults of the editable properties.
local function item(name, def)
  def.base = def.base or "item_base"
  local parent = rbs.classes[def.base]
  local ped = parent and parent.editor or {}
  local ed = def.editor or {}
  ed.category = ed.category or "items"
  ed.mins = ed.mins or {-16, -16, -16}
  ed.maxs = ed.maxs or {16, 16, 16}
  ed.model = ed.model or def.model
  ed.props = util.merge(ped.props, { model = def.model, message = def.message, pickup_sound = def.pickup_sound }, ed.props)
  ed.help = util.merge(ITEM_HELP, ed.help)
  ed.enums = util.merge(ped.enums, ed.enums)
  def.editor = ed
  return rbs.class(name, def)
end
rbs.item = item

rbs.class("item_base", {
  editor = { category = "items", props = ITEM_PROPS, help = ITEM_HELP },
  spawn = function(self)
    self.solid = "trigger"
    self.mins = vec(-16, -16, -16)
    self.maxs = vec(16, 16, 16)
    util.apply_props(self, ITEM_PROPS)
    self.spin = 90
    self.bob = 4
    rbs.add_stat("items_total")
  end,
  touch = function(self, other)
    if not util.is_player(other) or other.dead or self.taken then return end
    if self:pickup(other) == false then return end
    self.taken = true
    rbs.sound(self.pickup_sound ~= "" and self.pickup_sound or "sounds/pickup.wav", nil, 0.8)
    rbs.flash(0.35)
    if self.message ~= "" then rbs.message(self.message, 2) end
    rbs.add_stat("items")
    rbs.flow.event("pickup", self.classname, self)  -- quests count pickups (scripts/quests.lua)
    if util.num(self.respawn, 0) > 0 then
      self.visible = false
      self.respawn_at = rbs.time() + self.respawn
    else
      rbs.remove(self)
    end
  end,
  update = function(self)
    if self.respawn_at and rbs.time() > self.respawn_at then
      self.respawn_at = nil
      self.taken = false
      self.visible = true
      rbs.particles(self.pos, vec(0, 1, 0), { count = 12, color = {255, 230, 150}, speed = 80, size = 3, life = 0.5, additive = true })
    end
  end,
  pickup = function(self, p) end,
})

item("item_health", {
  model = "models/items/medkit.glb", message = "Medkit +25",
  editor = { color = {80, 255, 80}, desc = "Health: adds amount, up to max", props = { amount = 25, max = 100 } },
  pickup = function(self, p)
    local max = util.num(self.props.max, p.max_health)
    if p.health >= max then return false end
    p.health = math.min(max, p.health + util.num(self.props.amount, 25))
  end,
})

item("item_health_mega", {
  base = "item_health", model = "models/items/medkit.glb", message = "MEGA HEALTH!",
  editor = { color = {80, 255, 200}, desc = "Health +100, can go over 100",
             props = { amount = 100, max = 200, scale = 1.5, tint = {120, 200, 255} } },
})

item("item_armor", {
  model = "models/items/armor.glb", message = "Armor",
  editor = { color = {80, 160, 255}, desc = "Armor: adds amount, up to max", props = { amount = 50, max = 200 } },
  pickup = function(self, p)
    local max = util.num(self.props.max, 200)
    if p.armor >= max then return false end
    p.armor = math.min(max, p.armor + util.num(self.props.amount, 50))
  end,
})

-- Ammo of any type: the type is just a name shared with the weapons that use it.
item("item_ammo", {
  model = "models/items/shells.glb", message = "Ammo",
  editor = { color = {255, 160, 60}, desc = "Ammo of any type (ammo, amount)", props = { ammo = "shells", amount = 12 } },
  pickup = function(self)
    local kind = self.props.ammo or "shells"
    if rbs.ammo(kind) >= rbs.max_ammo(kind) then return false end
    rbs.give_ammo(kind, util.num(self.props.amount, 12))
  end,
})
item("item_shells", { base = "item_ammo", model = "models/items/shells.glb", message = "Shells",
  editor = { color = {255, 160, 60}, desc = "Shotgun shells", props = { ammo = "shells", amount = 12 } } })
item("item_cells", { base = "item_ammo", model = "models/items/cells.glb", message = "Plasma cells",
  editor = { color = {80, 200, 255}, desc = "Plasma cells", props = { ammo = "cells", amount = 40 } } })
item("item_rockets", { base = "item_ammo", model = "models/items/rockets.glb", message = "Rockets",
  editor = { color = {255, 100, 60}, desc = "Rockets", props = { ammo = "rockets", amount = 5 } } })

-- Any weapon: weapon = its id. The message defaults to the weapon's name.
item("item_weapon", {
  model = "models/weapons/w_shotgun.glb", pickup_sound = "sounds/weapon_pickup.wav",
  editor = { color = {255, 220, 60}, desc = "Weapon pickup (weapon = id)", props = { weapon = "shotgun" } },
  spawn = function(self)
    rbs.classes.item_base.spawn(self)
    local w = rbs.weapons[self.props.weapon or ""]
    if self.message == "" and w then self.message = w.name end
  end,
  pickup = function(self)
    local id = self.props.weapon or ""
    rbs.give_weapon(id, not rbs.has_weapon(id))
  end,
})
-- a pickup class per weapon, so mappers can simply place weapon_<id> (weapons that have
-- none get one automatically once all scripts are loaded, see definitions.lua)
function rbs.weapon_pickup(id, model, label)
  return item("weapon_" .. id, { base = "item_weapon", model = model, message = label, pickup_sound = "sounds/weapon_pickup.wav",
    editor = { color = {255, 220, 60}, desc = "Weapon pickup: " .. label, props = { weapon = id } } })
end
rbs.weapon_pickup("pistol", "models/weapons/w_pistol.glb", "Pistol")
rbs.weapon_pickup("shotgun", "models/weapons/w_shotgun.glb", "Double-barrel shotgun")
rbs.weapon_pickup("plasma", "models/weapons/w_plasma.glb", "Plasma rifle")
rbs.weapon_pickup("rocket", "models/weapons/w_rocket.glb", "Rocket launcher")

local function key_item(name, key, color, label)
  item(name, {
    model = "models/items/keycard.glb", message = label,
    editor = { color = color, desc = label, props = { tint = color } },
    spawn = function(self)
      rbs.classes.item_base.spawn(self)
      local c = self.tint
      self.fullbright = true
      self.light_color = vec(c.r, c.g, c.b) / 400
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
