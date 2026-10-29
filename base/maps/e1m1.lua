-- Map script for e1m1 (loaded automatically after the map's entities spawned).
-- Map scripts can react to targets (rbs.on), add objectives and run per-frame logic.

local yard_cleared = false

function rbs.on_map_start()
  rbs.message("E1M1: Brewery Yard", 3)
end

-- complete "Clear the brewery yard" once every monster in the yard is dead
function rbs.on_update(dt)
  if yard_cleared then return end
  for _, m in ipairs(rbs.find_radius(vec(0, 0, 100), 700)) do
    if m.monster and not m.dead then return end
  end
  yard_cleared = true
  rbs.objective("kill", "", true)
  rbs.message("The yard is clear. Find the exit in the office!", 3)
end
