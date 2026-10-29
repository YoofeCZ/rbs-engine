#!/usr/bin/env python3
"""Generates the demo level base/maps/e1m1.json (courtyard -> tunnel -> office).

Open the result in the editor to see how it is built, or use it as a starting point.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from mapgen import MapBuilder, box, plane_brush, prism, hprism, fit_tex  # noqa: E402

OUT = os.path.join(os.path.dirname(__file__), "..", "base", "maps", "e1m1.json")

m = MapBuilder("E1M1: Brewery Yard")
m.settings.update({
    "author": "RBS Engine",
    "sky_zenith": [0.32, 0.05, 0.03], "sky_horizon": [0.9, 0.28, 0.1], "sky_clouds": [0.3, 0.07, 0.04],
    "sun_dir": [0.55, -0.42, 0.3], "sun_color": [1.0, 0.58, 0.32], "sun_intensity": 1.15,
    "ambient": [0.07, 0.055, 0.05], "fog_color": [0.22, 0.07, 0.04], "fog_density": 0.00022,
    "lightmap_scale": 8, "sky_light": 0.55, "bounce": 1.0, "ao": 0.6, "gravity": 800,
    "music": "", "next_map": "",
})

T = 16          # wall thickness
SKY = 576       # courtyard sky height
LOW = 112       # height of the lower wall band

# ----------------------------------------------------------------------------
# A) Courtyard: x -512..512, z -384..640
# ----------------------------------------------------------------------------
X0, X1, Z0, Z1 = -512, 512, -384, 640
m.add(box((X0 - T, -T, Z0 - 256), (X1 + T, 0, Z1 + T), "asphalt"))            # ground
m.add(box((X0 - T, SKY, Z0 - 256), (X1 + T, SKY + T, Z1 + T), "sky"))         # sky lid

def banded_wall(x0, x1, z0, z1, y0=0, y1=SKY, upper="brick_red", lower="wall_yellow"):
    m.add(box((x0, y0, z0), (x1, min(LOW, y1), z1), lower))
    if y1 > LOW:
        m.add(box((x0, max(LOW, y0), z0), (x1, y1, z1), upper))

# south wall
banded_wall(X0 - T, X1 + T, Z1, Z1 + T)
# east wall (building facade)
banded_wall(X1, X1 + T, Z0 - 256, Z1, upper="brick_dark")
# west wall with the tunnel opening (z 96..352, height 224)
banded_wall(X0 - T, X0, Z0 - 256, 96)
banded_wall(X0 - T, X0, 352, Z1)
m.add(box((X0 - T, 224, 96), (X0, SKY, 352), "brick_red"))

# north wall with the garage alcove (x -320..32, depth to z -576, height 176)
AX0, AX1, AZ, AH = -320, 32, -576, 176
banded_wall(X0, AX0, Z0 - T, Z0)
banded_wall(AX1, X1, Z0 - T, Z0)
m.add(box((AX0, AH, Z0 - T), (AX1, SKY, Z0), "brick_red"))
# alcove shell
m.add(box((AX0 - T, 0, AZ - T), (AX1 + T, AH + T, AZ), "concrete_panel"))          # back wall
m.add(box((AX0 - T, 0, AZ), (AX0, AH, Z0 - T), "concrete_panel"))                   # left
m.add(box((AX1, 0, AZ), (AX1 + T, AH, Z0 - T), "concrete_panel"))                   # right
m.add(box((AX0 - T, AH, AZ - T), (AX1 + T, AH + T, Z0), "concrete_panel"))          # ceiling
m.add(box((AX0 - T, 0, AZ - T - 16), (AX1 + T, AH + T, AZ - T), "concrete_dark"))   # backing
# chamfered ceiling edges (like a loading bay)
for side in (-1, 1):
    xw = AX0 if side < 0 else AX1
    xin = xw - side * 56
    planes = [((0, 1, 0), (0, AH, 0)), ((0, 0, 1), (0, 0, Z0)), ((0, 0, -1), (0, 0, AZ)),
              ((side, 0, 0), (xw, 0, 0)),
              ((-side * 1.0, -1.0, 0), (xin, AH, 0))]
    m.add(plane_brush(planes, "concrete_panel"))
# front chamfer along the alcove opening
m.add(plane_brush([((0, 1, 0), (0, AH, 0)), ((1, 0, 0), (AX1, 0, 0)), ((-1, 0, 0), (AX0, 0, 0)),
                   ((0, 0, 1), (0, 0, Z0)), ((0, -1.0, -1.0), (0, AH - 40, Z0))], "concrete_panel"))
# the big metal door on the back wall
door = box((-256, 0, AZ), (-32, 160, AZ + 6), "metal_plate")
fit_tex(door, "+z", "metal_door", 256, 256, -256, -160, 224, 160)
m.add(door)
# fluorescent light fixture
m.add(box((-176, AH - 6, -520), (-112, AH, -488), "metal_plate", ny="light_panel"))
fix = m.add  # noqa
m.light((-144, AH - 24, -500), (220, 235, 255), 1.5, 420)
m.light((-144, 60, -430), (255, 190, 120), 0.5, 260)

# raised sidewalk on the east side
SW = 256
m.add(box((SW, 0, Z0), (X1, 24, Z1), "concrete", py="sidewalk"))
# building windows on the east facade
for z in (-300, -60, 180, 420):
    for y in (210, 380):
        w = box((X1 - 4, y, z), (X1, y + 120, z + 160), "metal_plate")
        fit_tex(w, "-x", "window", 256, 256, z, -(y + 120), 160, 120)
        m.add(w)
# beer sign high up on the east facade
sign = box((X1 - 10, 430, -440), (X1, 546, -208), "rust")
fit_tex(sign, "-x", "sign_beer", 512, 256, -440, -546, 232, 116)
m.add(sign)
m.light((X1 - 70, 540, -324), (255, 200, 140), 1.2, 300, spot=70, pitch=-30, angle=0)
# pipes coming out of the facade onto the sidewalk
for z in (40, 136):
    m.add(prism((X1 - 60, z), 12, 24, 150, 8, "rust"))
    m.add(hprism("x", (z, 150), 12, X1 - 72, X1, 8, "rust"))
# wooden post
m.add(prism((SW + 160, 560), 9, 24, 160, 6, "wood_planks"))

# curbs / small details
m.add(box((X0, 0, Z1 - 48), (-160, 16, Z1), "concrete", py="caution"))

# crates in the south-west corner
def crate(x, y, z, s=64):
    b = box((x, y, z), (x + s, y + s, z + s), "crate")
    for side in ("px", "nx", "pz", "nz", "py", "ny"):
        pass
    sc = s / 256.0
    # align the crate texture to each face
    b.tex("+x", "crate", scale=(sc, sc), off=(z / sc, (y + s) / sc))
    b.tex("-x", "crate", scale=(sc, sc), off=(-z / sc, (y + s) / sc))
    b.tex("+z", "crate", scale=(sc, sc), off=(-x / sc, (y + s) / sc))
    b.tex("-z", "crate", scale=(sc, sc), off=(x / sc, (y + s) / sc))
    b.tex("+y", "crate", scale=(sc, sc), off=(-x / sc, -z / sc))
    b.tex("-y", "crate", scale=(sc, sc), off=(-x / sc, -z / sc))
    m.add(b)

crate(-480, 0, 520)
crate(-416, 0, 528)
crate(-470, 64, 530)
crate(-300, 0, 560, 48)

# courtyard lamps on the walls
for x in (-360, 160):
    m.add(box((x - 12, 300, Z1 - 12), (x + 12, 312, Z1), "metal_plate", ny="light_panel"))
    m.light((x, 280, Z1 - 40), (255, 170, 90), 1.3, 380)

# fire barrel
m.ent("fire", (200, 36, 330), size=44)
m.add(prism((200, 330), 14, 0, 30, 8, "rust"))

# ----------------------------------------------------------------------------
# B) Tunnel: x -1536..-512, z 96..352, arched
# ----------------------------------------------------------------------------
TX0, TX1, TZ0, TZ1, TH = -1536, X0 - T, 96, 352, 224
m.add(box((TX0, -T, TZ0 - T), (TX1, 0, TZ1 + T), "concrete_dark"))
m.add(box((TX0, 0, TZ0 - T), (TX1, 160, TZ0), "tiles_tunnel"))
m.add(box((TX0, 0, TZ1), (TX1, 160, TZ1 + T), "tiles_tunnel"))
m.add(box((TX0, 160, TZ0 - T), (TX1, TH + 96, TZ0), "tunnel_stone"))
m.add(box((TX0, 160, TZ1), (TX1, TH + 96, TZ1 + T), "tunnel_stone"))
m.add(box((TX0, TH + 80, TZ0), (TX1, TH + 96, TZ1), "tunnel_stone"))
# arch: two chamfer slabs per side between the wall (y 170) and the ceiling (y 304)
mid = (TZ0 + TZ1) / 2
TOP = TH + 80
for side in (-1, 1):
    zw = TZ0 if side < 0 else TZ1
    # (z offset from the wall, y) points of the arch outline
    for (a, b) in (((0, 170), (40, 250)), ((40, 250), (110, TOP))):
        z_a, y_a = zw - side * a[0], a[1]
        z_b, y_b = zw - side * b[0], b[1]
        dz, dy = z_b - z_a, y_b - y_a
        n = (0, -abs(dz), -side * abs(dy))      # points into the tunnel
        planes = [((0, 1, 0), (0, TOP, 0)), ((1, 0, 0), (TX1, 0, 0)), ((-1, 0, 0), (TX0, 0, 0)),
                  ((0, 0, side), (0, 0, zw)), (n, (0, y_a, z_a))]
        m.add(plane_brush(planes, "tunnel_stone"))
# ceiling light strip + lights along the tunnel
for x in range(TX0 + 128, TX1, 256):
    m.add(box((x - 24, TH + 72, mid - 6), (x + 24, TH + 80, mid + 6), "metal_plate", ny="light_panel"))
    m.light((x, TH + 40, mid), (200, 230, 255), 1.1, 300)
# side walkways
m.add(box((TX0, 0, TZ0), (TX1, 12, TZ0 + 40), "concrete", py="concrete_panel"))
m.add(box((TX0, 0, TZ1 - 40), (TX1, 12, TZ1), "concrete", py="concrete_panel"))

# ----------------------------------------------------------------------------
# C) Office: x -2304..-1536, z -96..544
# ----------------------------------------------------------------------------
OX0, OX1, OZ0, OZ1, OH = -2304, TX0, -96, 544, 160
m.add(box((OX0 - T, -T, OZ0 - T), (OX1, 0, OZ1 + T), "floor_checker"))
m.add(box((OX0 - T, OH, OZ0 - T), (OX1, OH + T, OZ1 + T), "ceiling_perforated"))
m.add(box((OX0 - T, 0, OZ0 - T), (OX0, OH, OZ1 + T), "office_wall"))
m.add(box((OX0, 0, OZ0 - T), (OX1, OH, OZ0), "office_wall"))
m.add(box((OX0, 0, OZ1), (OX1, OH, OZ1 + T), "office_wall"))
# east wall of the office with the tunnel doorway (z 96..352 -> keep 160 high)
m.add(box((OX1, 0, OZ0 - T), (OX1 + T, OH, TZ0 - T), "office_wall"))
m.add(box((OX1, 0, TZ1 + T), (OX1 + T, OH, OZ1 + T), "office_wall"))
m.add(box((OX1, OH - 8, TZ0 - T), (OX1 + T, OH, TZ1 + T), "office_wall"))
m.add(box((OX1, OH, TZ0 - T), (OX1 + T, TH + 96, TZ1 + T), "tunnel_stone"))
# interior window wall with blinds splitting the office (x = -1984)
WX = -1984
m.add(box((WX, 0, OZ0), (WX + 12, 48, 300), "office_wall"))
m.add(box((WX, 120, OZ0), (WX + 12, OH, 300), "blinds"))
glass = box((WX + 4, 48, OZ0), (WX + 8, 120, 300), "grate")
m.add(glass)
m.add(box((WX, 0, 400), (WX + 12, OH, OZ1), "office_wall"))
m.add(box((WX, 128, 300), (WX + 12, OH, 400), "office_wall"))
# desks
def desk(x, z, w=96, d=48):
    m.add(box((x, 28, z), (x + w, 32, z + d), "wood_dark"))
    for dx in (2, w - 8):
        for dz in (2, d - 8):
            m.add(box((x + dx, 0, z + dz), (x + dx + 6, 28, z + dz + 6), "wood_dark"))
desk(-1860, 120)
desk(-1860, 240)
desk(-1720, 180, 64, 96)
# filing cabinets
for i, z in enumerate((-80, -40, 0)):
    c = box((-1600, 0, z), (-1568, 72, z + 36), "metal_plate")
    fit_tex(c, "-x", "cabinet", 256, 256, z, -72, 36, 72)
    m.add(c)
# office lights
for x in (-2200, -1900, -1700):
    for z in (40, 360):
        m.add(box((x - 32, OH - 4, z - 10), (x + 32, OH, z + 10), "metal_plate", ny="light_panel"))
        m.light((x, OH - 20, z), (225, 240, 255), 1.0, 300)
# warehouse crates behind the window
crate(-2240, 0, 400)
crate(-2176, 0, 420)
crate(-2230, 64, 410)

# exit door at the far end of the office (trigger to end the level)
exitdoor = box((OX0, 0, 200), (OX0 + 6, 120, 296), "metal_plate")
fit_tex(exitdoor, "+x", "metal_door", 256, 256, -296, -120, 96, 120)
m.add(exitdoor)

# ----------------------------------------------------------------------------
# entities
# ----------------------------------------------------------------------------
m.ent("player_start", (300, 48, 560), angle=135)
m.ent("info_objective", (0, 32, 0), id="exit", text="Find the exit in the office", targetname="obj_exit")
m.ent("info_objective", (0, 32, 0), id="kill", text="Clear the brewery yard")
m.ent("trigger_changelevel", (OX0 + 40, 60, 248), brushes=[box((OX0 + 6, 0, 200), (OX0 + 40, 120, 296), "trigger")], map="")

# monsters
m.ent("monster_grunt", (-150, 48, -300), angle=270)
m.ent("monster_grunt", (-380, 48, 120), angle=0)
m.ent("monster_grunt", (100, 48, -100), angle=200)
m.ent("monster_fiend", (-900, 48, 220), angle=0)
m.ent("monster_fiend", (-1200, 48, 180), angle=0)
m.ent("monster_heavy", (-2100, 48, 150), angle=0)
m.ent("monster_grunt", (-1800, 48, 420), angle=0)

# items
m.ent("item_shells", (380, 44, 300))
m.ent("item_health", (-450, 32, 450))
m.ent("item_armor", (-100, 32, -520))
m.ent("weapon_plasma", (-1024, 32, 224))
m.ent("item_cells", (-1300, 32, 150))
m.ent("weapon_rocket", (-2200, 32, 0))
m.ent("item_rockets", (-2240, 32, -40))
m.ent("item_health", (-1700, 32, 480))
m.ent("prop_barrel", (-60, 0, 420))
m.ent("prop_barrel", (-30, 0, 470))

# atmosphere
m.ent("ambient_sound", (-1024, 120, 224), sound="sounds/hum_loop.wav", volume=0.4, radius=700)
m.ent("mission_camera", (420, 300, -320), angle=210, targetname="yardcam", pitch=-20)
m.ent("trigger_camera", (-560, 100, 224), brushes=[box((-600, 0, TZ0), (-580, 224, TZ1), "trigger")], camera="yardcam", time=4)
m.ent("sprite", (-144, AH - 10, -504), texture="sprites/flare.png", size=60, additive=1, color=[200, 220, 255])

m.save(os.path.abspath(OUT))
