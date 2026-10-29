#!/usr/bin/env python3
"""Generates mods/example_mod/maps/mod_arena.json."""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from mapgen import MapBuilder, box, prism  # noqa: E402

m = MapBuilder("Imp arena (example mod)")
m.settings.update({"sky_zenith": [0.12, 0.02, 0.15], "sky_horizon": [0.6, 0.15, 0.4], "sky_clouds": [0.25, 0.05, 0.2],
                   "sun_dir": [0.3, -0.5, -0.6], "sun_color": [1.0, 0.6, 0.8], "sun_intensity": 0.9,
                   "ambient": [0.1, 0.07, 0.1], "sky_light": 0.6, "fog_color": [0.2, 0.05, 0.18], "fog_density": 0.0003})
S, H, T = 1024, 448, 16
m.add(box((-S - T, -T, -S - T), (S + T, 0, S + T), "dirt"))
m.add(box((-S - T, H, -S - T), (S + T, H + T, S + T), "sky"))
for b in ((-S - T, 0, -S - T, -S, H, S + T), (S, 0, -S - T, S + T, H, S + T), (-S, 0, -S - T, S, H, -S), (-S, 0, S, S, H, S + T)):
    m.add(box(b[:3], b[3:], "brick_dark"))
for i in range(8):
    import math
    a = i * math.pi / 4
    m.add(prism((math.cos(a) * 600, math.sin(a) * 600), 48, 0, 220, 8, "tunnel_stone"))
m.add(box((-128, 0, -128), (128, 64, 128), "concrete", py="metal_plate"))
m.ent("player_start", (0, 104, 0), angle=90)
for i in range(8):
    a = i * math.pi / 4 + math.pi / 8
    m.ent("monster_imp", (math.cos(a) * 800, 48, math.sin(a) * 800), angle=0)
m.ent("item_rockets", (300, 32, 0))
m.ent("item_rockets", (-300, 32, 0))
m.ent("item_health", (0, 32, 400))
m.ent("fire", (0, 80, 0), size=60)
for x, z in ((-700, -700), (700, -700), (-700, 700), (700, 700)):
    m.light((x, 300, z), (255, 140, 220), 1.2, 900)
m.save(os.path.join(os.path.dirname(__file__), "..", "mods", "example_mod", "maps", "mod_arena.json"))
