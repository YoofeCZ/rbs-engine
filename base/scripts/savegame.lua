-- Saving the fields scripts keep on their entities (a door's state, a trigger that fired, a
-- monster's timers). The engine calls these for every entity when it saves and loads a game;
-- positions, health and the like are saved by the engine itself, not here.
--
-- Only plain values are kept: numbers, text, true / false, vectors and tables of those.
-- References to other entities cannot be saved and are dropped (a monster forgets whom it
-- was chasing). A class that needs more can set the fields again in its `loaded(self)` function,
-- which is called after the saved fields were put back.

-- props / classname / id are the engine's; keys starting with "__" are the engine's own bookkeeping
-- (the link to the class): putting those back would break the entity
local SKIP = {props = true, classname = true, id = true}
local function skipped(k) return SKIP[k] or k:sub(1, 2) == "__" end

local function encode(v, depth)
  local t = type(v)
  if t == "number" then
    if v ~= v or v == math.huge or v == -math.huge then return nil end
    return v
  end
  if t == "string" or t == "boolean" then return v end
  if t ~= "table" or depth > 4 then return nil end
  if getmetatable(v) == vec.meta then return {__vec = {v.x, v.y, v.z}} end
  local ok, removed = pcall(function() return v.removed end)
  if ok and removed ~= nil then return nil end  -- an entity
  local out = {}
  if #v > 0 then
    for i = 1, #v do
      local e = encode(v[i], depth + 1)
      if e == nil then return nil end  -- an array with a hole cannot be stored
      out[i] = e
    end
  else
    for k, x in pairs(v) do
      if type(k) == "string" then
        local e = encode(x, depth + 1)
        if e ~= nil then out[k] = e end
      end
    end
  end
  return out
end

local function decode(v)
  if type(v) ~= "table" then return v end
  if v.__vec then return vec(v.__vec[1], v.__vec[2], v.__vec[3]) end
  local out = {}
  for k, x in pairs(v) do out[k] = decode(x) end
  return out
end

function rbs.save_fields(ent)
  local out = {}
  for k, v in pairs(ent) do
    if type(k) == "string" and not skipped(k) then
      local e = encode(v, 0)
      if e ~= nil then out[k] = e end
    end
  end
  return out
end

function rbs.load_fields(ent, data)
  if type(data) ~= "table" then return end
  for k, v in pairs(data) do
    if type(k) == "string" and not skipped(k) then rawset(ent, k, decode(v)) end
  end
  if type(ent.loaded) == "function" then ent:loaded() end
end
