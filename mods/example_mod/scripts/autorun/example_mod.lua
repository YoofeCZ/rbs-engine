-- Example mod: everything here is added on top of the base game.
-- Files in scripts/autorun/ of every layer (base + active mod) run after scripts/init.lua.

-- 1) A new projectile: bouncing grenade -----------------------------------------
rbs.class("grenade", {
  spawn = function(self)
    self.movetype = "bounce"
    self.bounce = 0.55
    self.solid = "not"
    self.mins = vec(-3, -3, -3)
    self.maxs = vec(3, 3, 3)
    self.model = "models/items/rockets.glb"
    self.scale = 0.5
    self.fuse = rbs.time() + 2.2
    self.light_color = vec(1.2, 0.3, 0.1)
    self.light_radius = 60
  end,
  update = function(self, dt)
    self.angles = self.angles + vec(360 * dt, 200 * dt, 0)
    if rbs.time() > self.fuse then
      rbs.explosion(self.pos, 160, 120, self.owner)
      rbs.remove(self)
    end
  end,
  touch = function(self, other, normal)
    if other and other.monster then
      self.fuse = 0  -- direct hits explode immediately
    else
      rbs.sound("sounds/gib_bounce.wav", self.pos, 0.5, 1.6)
    end
  end,
})

-- 2) A new weapon using it ------------------------------------------------------------
rbs.weapon{
  id = "grenade", slot = 5, name = "Grenade launcher",
  model = "models/weapons/v_rocket.glb",
  ammo = "rockets", ammo_per_shot = 1, start_ammo = 10,
  fire_rate = 0.6, recoil = 1.8,
  view_offset = {8.5, -9, 11}, view_rotation = {0, 3, 0}, view_scale = 0.9, muzzle = {0, 3, 30},
  flash_color = {1.0, 0.5, 0.2}, flash_size = 16,
  fire = function(player)
    rbs.sound("sounds/rocket_fire.wav", nil, 0.7, 1.4)
    local eye, dir = rbs.aim()
    local g = rbs.projectile("grenade", player, eye + dir * 16 - vec(0, 6, 0), (dir + vec(0, 0.22, 0)):normalize(), 750)
  end,
}

-- 3) A new monster derived from the fiend: small, fast, purple, keeps its distance and
--    throws fireballs. The built-in AI options come from rbs.monster (they show up in the
--    editor), the attack is its own Lua code - attack_type "custom" says so.
rbs.monster("monster_imp", {
  base = "monster_fiend",
  editor = { color = {170, 60, 255}, mins = {-12, -24, -12}, maxs = {12, 16, 12},
             model = "models/monsters/fiend.glb", desc = "Imp (example mod): small and fast, throws fireballs.",
             props = { health = 40, ai = "ranged", speed = 300, attack_range = 900, keep_distance = 300,
                       attack_cooldown = 0.9, attack_type = "custom", leap = false,
                       scale = 0.75, tint = {190, 120, 255} } },
  max_hp = 40,
  attack = function(self, enemy)
    local origin = self.pos + vec(0, 10, 0)
    rbs.projectile("fireball", self, origin, (enemy.center - origin):normalize(), 800)
  end,
})

-- 4) Chain onto the base game's hook instead of replacing it --------------------------
local base_spawn = rbs.on_player_spawn
function rbs.on_player_spawn(player)
  base_spawn(player)
  rbs.give_weapon("grenade")
end

rbs.log("example_mod loaded: grenade launcher (slot 5) + monster_imp")
