-- Enemy/item types and weapons made without code.
--
--   entities/<name>.json   a new entity type based on an existing class:
--     { "base": "monster_grunt", "description": "...", "category": "monsters",
--       "properties": { "health": 150, "speed": 90, "model": "models/monsters/zombie.glb", ... } }
--   weapons/<id>.json      a new weapon (all fields of weapons/weapons.lua):
--     { "base": "shotgun", "name": "Sawed-off", "slot": 5, "bullets": 12, "damage": 9, ... }
--
-- The editor writes these files (Project > Entities > New type, Project > Weapons > New
-- weapon) and edits them in the Inspector. The engine calls rbs._load_definitions() after
-- all scripts (scripts/init.lua and scripts/autorun/*.lua) are loaded, so a definition can
-- be based on any class or weapon, including ones written in Lua by a project.

local function read_json(path)
  local text = rbs.read_file(path)
  if not text then return nil end
  local def, err = rbs.json_decode(text)
  if type(def) ~= "table" or def[1] ~= nil then
    rbs.log(path .. ": " .. (err or "not a JSON object"))
    return nil
  end
  return def
end

local function name_of(path) return path:match("([^/]+)%.json$") end

local function load_entity(path, def)
  local name = name_of(path)
  local base = rbs.classes[def.base]
  local bed = base.editor or {}
  if type(def.properties) ~= "table" then def.properties = {} end
  local props = util.merge(bed.props, def.properties)
  local editor = {
    category = def.category or bed.category, color = def.color or bed.color,
    mins = bed.mins, maxs = bed.maxs, brush = bed.brush, sprite = bed.sprite,
    model = (props.model ~= nil and props.model ~= "") and props.model or bed.model,
    desc = def.description or bed.desc, props = props, enums = bed.enums, help = bed.help,
  }
  local cls = { base = def.base, editor = editor, definition = path }
  -- class fields follow the properties, so code reading self.<name> sees them too
  for k, v in pairs(def.properties) do cls[k] = v end
  -- the base behaviour might not apply every property itself (looks, sounds...)
  local keys = def.properties
  cls.spawn = function(self)
    local spawn = base.spawn
    if spawn then spawn(self) end
    util.apply_props(self, keys)
  end
  rbs.class(name, cls)
end

local function load_weapon(path, def)
  local id = name_of(path)
  local w = {}
  local base = def.base and rbs.weapons[def.base]
  if base then
    for k, v in pairs(base) do w[k] = v end
    -- Lua weapons with their own fire() keep it unless the file picks a built-in shot
    if def.shot then w.fire = nil end
  end
  for k, v in pairs(def) do w[k] = v end
  w.id = id
  w.base = nil
  w.definition = path
  rbs.weapon(w)
end

-- Loads every file whose base exists, repeating while that makes progress, so a type can
-- be based on another type made without code.
local function load_all(folder, exists, load, what)
  local pending = {}
  for _, path in ipairs(rbs.list_files(folder, ".json")) do
    local def = name_of(path) and read_json(path)
    if def then pending[#pending + 1] = { path = path, def = def } end
  end
  local progress = true
  while progress and #pending > 0 do
    progress = false
    for i = #pending, 1, -1 do
      local p = pending[i]
      local base = p.def.base
      if base ~= name_of(p.path) and exists(base) then
        load(p.path, p.def)
        table.remove(pending, i)
        progress = true
      end
    end
  end
  for _, p in ipairs(pending) do
    rbs.log(p.path .. ": unknown base " .. what .. " '" .. tostring(p.def.base) .. "'")
  end
end

function rbs._load_definitions()
  -- a weapon may start from scratch; an entity type always extends a class
  load_all("weapons", function(b) return b == nil or (type(b) == "string" and rbs.weapons[b] ~= nil) end, load_weapon, "weapon")
  -- every weapon can be placed in a map as weapon_<id>, also ones a project wrote in Lua
  for id, w in pairs(rbs.weapons) do
    if not rbs.classes["weapon_" .. id] then
      rbs.weapon_pickup(id, w.pickup_model or "models/weapons/w_shotgun.glb", w.name or id)
    end
  end
  load_all("entities", function(b) return type(b) == "string" and rbs.classes[b] ~= nil end, load_entity, "class")
end
