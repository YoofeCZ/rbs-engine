#!/usr/bin/env python3
"""Generates mods/plugin_demo/maps/plugin_test.json - an arena that uses the native example plugins."""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from mapgen import MapBuilder, box, prism  # noqa: E402

m = MapBuilder("Plugin test arena")
m.settings.update({"sky_zenith": [0.05, 0.08, 0.2], "sky_horizon": [0.3, 0.4, 0.7], "sky_clouds": [0.2, 0.25, 0.4],
                   "sun_dir": [-0.4, -0.7, 0.3], "sun_color": [0.9, 0.9, 1.0], "sun_intensity": 0.9,
                   "ambient": [0.12, 0.12, 0.15], "sky_light": 0.6, "fog_density": 0.0})
S, H, T = 768, 384, 16
m.add(box((-S - T, -T, -S - T), (S + T, 0, S + T), "metal_plate"))
m.add(box((-S - T, H, -S - T), (S + T, H + T, S + T), "sky"))
for b in ((-S - T, 0, -S - T, -S, H, S + T), (S, 0, -S - T, S + T, H, S + T), (-S, 0, -S - T, S, H, -S), (-S, 0, S, S, H, S + T)):
    m.add(box(b[:3], b[3:], "concrete_panel"))
# a few pillars and platforms
for x, z in ((-300, -300), (300, -300), (-300, 300), (300, 300)):
    m.add(prism((x, z), 40, 0, 160, 8, "concrete"))
m.add(box((-160, 0, -620), (160, 96, -480), "concrete", py="caution"))
for x in (-500, 0, 500):
    m.light((x, 300, 0), (220, 230, 255), 1.0, 700)

m.ent("player_start", (0, 40, 600), angle=90)
m.ent("info_objective", (0, 0, 0), id="test", text="Try the C, C++, Rust and C# entities")
# native entities
m.ent("cpp_turret", (-300, 168, -300), angle=270, range=900)
m.ent("cpp_turret", (300, 168, -300), angle=270, range=900)
m.ent("rust_heal_station", (-450, 0, 450))
m.ent("csharp_launch_pad", (0, 4, 200), power=800)
m.ent("c_beacon", (450, 24, 450))
# components from different languages mixed on one entity
m.ent("prop_model", (0, 96, -550), angle=0, model="models/props/barrel.glb",
      components=[{"type": "cpp_spinner", "props": {"speed": 90}}, {"type": "rust_hover", "props": {"height": 12}},
                  {"type": "csharp_pulse_light", "props": {"radius": 260}}])
m.ent("prop_model", (-200, 0, 0), model="models/weapons/w_rocket.glb", scale=2,
      components=[{"type": "rotator", "props": {"speed": 60}}, {"type": "bobber", "props": {"height": 6}}])
m.ent("monster_grunt", (0, 48, -200), angle=270)
m.ent("item_health", (450, 32, -450))
m.ent("weapon_plasma", (-450, 32, -450))
m.save(os.path.join(os.path.dirname(__file__), "..", "mods", "plugin_demo", "maps", "plugin_test.json"))
