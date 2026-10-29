-- Game rules and global hooks. Mods can redefine any of these functions.

-- Called when the player spawns at the start of a map (not when carried over).
-- The numbers come from the project settings (mod.json "player", editor: Project >
-- Settings), so a game can change them without code.
function rbs.on_player_spawn(player)
  local function tbl(v) return type(v) == "table" and v or nil end
  local cfg = tbl(rbs.project().player) or {}
  local health = util.num(cfg.health, 100)
  player.max_health = util.num(cfg.max_health, health)
  player.health = health
  player.armor = util.num(cfg.armor, 0)
  -- ammo limits: from the weapons (max_ammo), then the project's own list
  local max = { shells = 60, cells = 300, rockets = 30 }
  for _, w in pairs(rbs.weapons) do
    local n = tonumber(w.max_ammo)
    if type(w.ammo) == "string" and n then max[w.ammo] = math.max(max[w.ammo] or 0, n) end
  end
  for kind, n in pairs(tbl(cfg.max_ammo) or {}) do max[kind] = util.num(n, max[kind]) end
  for kind, n in pairs(max) do rbs.max_ammo(kind, n) end
  local start = tbl(cfg.start_weapons) or {"pistol", "shotgun"}
  local select = cfg.select or start[#start]
  for _, id in ipairs(start) do
    if rbs.weapons[id] then rbs.give_weapon(id, id == select) end
  end
  for kind, n in pairs(tbl(cfg.start_ammo) or {}) do rbs.give_ammo(kind, util.num(n, 0)) end
end

function rbs.on_player_death(player)
  rbs.message("You died. Press fire to restart.", 100)
end

function rbs.on_map_start()
end

-- The player entity itself (pain/death sounds, falling damage reactions...)
rbs.class("player", {
  pain = function(self, amount)
    if rbs.time() > (self.next_pain or 0) then
      self.next_pain = rbs.time() + 0.4
      rbs.sound("sounds/player_pain.wav", nil, 0.9, 0.95 + math.random() * 0.1)
    end
  end,
  die = function(self)
    rbs.sound("sounds/player_death.wav", nil, 1.0)
    if self.health < -50 then rbs.gib(self, 6) end
  end,
})
