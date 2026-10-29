-- Game rules and global hooks. Mods can redefine any of these functions.

-- Called when the player spawns at the start of a map (not when carried over).
function rbs.on_player_spawn(player)
  player.health = 100
  player.max_health = 100
  player.armor = 0
  rbs.max_ammo("shells", 60)
  rbs.max_ammo("cells", 300)
  rbs.max_ammo("rockets", 30)
  rbs.give_weapon("pistol")
  rbs.give_weapon("shotgun", true)
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
