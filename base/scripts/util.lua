-- Small helpers shared by the base scripts.
util = {}

-- Parses "x y z", {x=..}, {1,2,3} into a vec.
function util.vec(v, default)
  if type(v) == "table" then
    if v.x then return vec(v.x, v.y, v.z) end
    return vec(v[1] or 0, v[2] or 0, v[3] or 0)
  elseif type(v) == "string" then
    local x, y, z = v:match("([%-%d%.]+)%s+([%-%d%.]+)%s+([%-%d%.]+)")
    if x then return vec(tonumber(x), tonumber(y), tonumber(z)) end
  end
  return default or vec(0, 0, 0)
end

function util.num(v, default)
  local n = tonumber(v)
  if n == nil then return default end
  return n
end

function util.color(v, default)
  if type(v) == "table" then return v end
  if type(v) == "string" then
    local r, g, b = v:match("(%d+)%s+(%d+)%s+(%d+)")
    if r then return {tonumber(r), tonumber(g), tonumber(b)} end
  end
  return default or {255, 255, 255}
end

function util.is_player(e) return e ~= nil and e.classname == "player" end

-- true if point p is inside the entity's brush bounds grown by `pad`
function util.near_brush(ent, p, pad)
  local mn, mx = ent.bmins, ent.bmaxs
  return p.x > mn.x - pad and p.x < mx.x + pad and p.y > mn.y - pad and p.y < mx.y + pad and p.z > mn.z - pad and p.z < mx.z + pad
end

function util.choose(list) return list[math.random(1, #list)] end

-- Copies properties (editor values + type defaults, from self.props) onto the entity so
-- behaviour code can read them as self.<name>. `keys` is a list or a set of names.
-- Appearance keys (model, scale, tint) go straight to the engine.
function util.apply_props(self, keys)
  local props = self.props
  local function apply(k)
    local v = props[k]
    if v == nil then return end
    if k == "model" then
      if v ~= "" then self.model = v end
    else
      self[k] = v
    end
  end
  if keys[1] ~= nil then
    for _, k in ipairs(keys) do apply(k) end
  else
    for k in pairs(keys) do apply(k) end
  end
end

-- Merges tables into a new one (later ones win). Used to build editor property lists.
function util.merge(...)
  local out = {}
  for i = 1, select("#", ...) do
    local t = select(i, ...)
    if t then for k, v in pairs(t) do out[k] = v end end
  end
  return out
end

