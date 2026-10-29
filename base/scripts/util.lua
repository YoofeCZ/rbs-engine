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
