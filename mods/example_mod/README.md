# Example Lua mod

A mod is just a folder in `mods/`. Any file it contains overrides the file with the same
path in `base/`, new files are added. This one only adds content:

| File | What it does |
|---|---|
| `mod.json` | name/description shown in the Mods menu |
| `game.cfg` | console commands run when the mod is active (here: the map started by *New game*) |
| `scripts/autorun/example_mod.lua` | new projectile, weapon (slot 5), monster and a hook chained onto the base game |
| `maps/mod_arena.json` | a small arena (open it in the editor!) |

Run it with `rbs --mod example_mod` or pick it in the main menu under **Mods**.
