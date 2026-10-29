#!/usr/bin/env python3
"""RBS Engine - procedural low-poly model generator for the base game.

Builds animated monsters (skinned glTF with idle/walk/attack/pain/death), first person
weapons, pickups, props and gibs from boxes and cylinders, all sharing one texture
atlas (models/atlas.png). Requires numpy + Pillow.

Replace any of these with real art: export glTF (.glb) from Blender with the model
facing +Z, Y up, origin at the feet. Animation names the base scripts use:
idle, walk, attack, pain, death.
"""
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from gltf_writer import write_glb, euler  # noqa: E402
from gen_textures import fbm, value_noise, colorize, shade, to_img  # noqa: E402

OUT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "base", "models"))

# ----------------------------------------------------------------------------
# texture atlas: 4x4 swatches of 128px in a 512 image
# ----------------------------------------------------------------------------
ATLAS = 512
CELL = 128
SWATCHES = ["armor", "armor_dark", "skin", "face", "helmet", "gunmetal", "stripes", "gold",
            "glow_blue", "wood", "flesh", "white", "medkit", "olive", "barrel", "demon"]


def swatch_rect(name):
    i = SWATCHES.index(name)
    return (i % 4) * CELL, (i // 4) * CELL


def make_atlas():
    rng = np.random.default_rng(7)
    img = Image.new("RGB", (ATLAS, ATLAS))

    def noise_fill(c0, c1, base=4, oct=5):
        n = fbm(CELL, base, oct, rng=rng)
        return to_img(colorize(n, [(0, c0), (1, c1)]))

    def put(name, im):
        x, y = swatch_rect(name)
        img.paste(im.resize((CELL, CELL)), (x, y))

    # orange armour with panel lines and scratches (like the screenshot grunt)
    a = noise_fill((150, 70, 20), (225, 130, 50), 4, 6)
    d = ImageDraw.Draw(a)
    for i in range(0, CELL, 32):
        d.line([0, i, CELL, i], fill=(110, 50, 15), width=2)
    for _ in range(30):
        x, y = rng.integers(0, CELL, 2)
        d.line([x, y, x + rng.integers(-8, 8), y + rng.integers(-8, 8)], fill=(240, 180, 110), width=1)
    put("armor", a)
    put("armor_dark", noise_fill((40, 32, 26), (88, 70, 54)))
    put("skin", noise_fill((150, 100, 70), (205, 150, 110), 8, 5))
    f = noise_fill((150, 100, 70), (205, 150, 110), 8, 5)
    d = ImageDraw.Draw(f)
    d.rectangle([24, 44, 54, 58], fill=(240, 240, 230))
    d.rectangle([74, 44, 104, 58], fill=(240, 240, 230))
    d.rectangle([34, 46, 46, 56], fill=(40, 60, 90))
    d.rectangle([84, 46, 96, 56], fill=(40, 60, 90))
    d.rectangle([22, 36, 56, 41], fill=(80, 50, 30))
    d.rectangle([72, 36, 106, 41], fill=(80, 50, 30))
    d.polygon([(64, 58), (56, 84), (72, 84)], fill=(160, 105, 75))
    d.rectangle([44, 96, 84, 104], fill=(110, 40, 35))
    put("face", f)
    put("helmet", noise_fill((60, 64, 40), (110, 112, 72), 4, 5))
    put("gunmetal", noise_fill((22, 22, 24), (62, 62, 66), 8, 5))
    s = Image.new("RGB", (CELL, CELL), (24, 24, 24))
    d = ImageDraw.Draw(s)
    for i in range(-CELL, CELL * 2, 32):
        d.polygon([(i, 0), (i + 16, 0), (i + 16 + CELL, CELL), (i + CELL, CELL)], fill=(220, 175, 30))
    put("stripes", s)
    g = noise_fill((150, 110, 30), (235, 190, 80), 8, 5)
    d = ImageDraw.Draw(g)
    for yy in range(8, CELL, 16):
        for xx in range(8 + (yy // 16 % 2) * 8, CELL, 16):
            d.ellipse([xx - 3, yy - 3, xx + 3, yy + 3], fill=(60, 40, 10))
    put("gold", g)
    gl = noise_fill((40, 160, 255), (200, 240, 255), 4, 4)
    put("glow_blue", gl)
    put("wood", noise_fill((80, 50, 25), (150, 100, 55), 16, 5))
    fl = noise_fill((90, 5, 5), (190, 40, 30), 8, 6)
    put("flesh", fl)
    put("white", noise_fill((190, 190, 185), (240, 240, 235), 8, 4))
    mk = noise_fill((200, 200, 195), (245, 245, 240), 8, 4)
    d = ImageDraw.Draw(mk)
    d.rectangle([52, 20, 76, 108], fill=(200, 20, 20))
    d.rectangle([20, 52, 108, 76], fill=(200, 20, 20))
    put("medkit", mk)
    put("olive", noise_fill((50, 60, 30), (100, 110, 60), 8, 5))
    b = noise_fill((120, 30, 20), (180, 60, 40), 4, 6)
    d = ImageDraw.Draw(b)
    d.rectangle([0, 50, CELL, 78], fill=(220, 180, 30))
    for i in range(0, CELL, 24):
        d.polygon([(i, 50), (i + 12, 50), (i + 24, 78), (i + 12, 78)], fill=(30, 30, 30))
    put("barrel", b)
    dm = noise_fill((70, 10, 10), (150, 40, 30), 8, 6)
    d = ImageDraw.Draw(dm)
    for _ in range(14):
        x, y = rng.integers(0, CELL, 2)
        d.line([x, y, x + rng.integers(-30, 30), y + rng.integers(-30, 30)], fill=(200, 80, 40), width=1)
    put("demon", dm)
    return img


# ----------------------------------------------------------------------------
# mesh builder
# ----------------------------------------------------------------------------
class Mesh:
    def __init__(self):
        self.pos, self.nrm, self.uv, self.joint, self.idx = [], [], [], [], []

    def quad(self, p, n, mat, joint, uvrect=(0, 0, 1, 1)):
        """p: 4 corners CCW seen from outside"""
        sx, sy = swatch_rect(mat)
        pad = 1.5
        u0 = (sx + pad + uvrect[0] * (CELL - 2 * pad)) / ATLAS
        v0 = (sy + pad + uvrect[1] * (CELL - 2 * pad)) / ATLAS
        u1 = (sx + pad + uvrect[2] * (CELL - 2 * pad)) / ATLAS
        v1 = (sy + pad + uvrect[3] * (CELL - 2 * pad)) / ATLAS
        uvs = [(u0, v1), (u1, v1), (u1, v0), (u0, v0)]
        base = len(self.pos)
        for i in range(4):
            self.pos.append(tuple(float(c) for c in p[i]))
            self.nrm.append(tuple(float(c) for c in n))
            self.uv.append(uvs[i])
            self.joint.append(joint)
        self.idx += [base, base + 1, base + 2, base, base + 2, base + 3]

    def tri(self, p, n, mat, joint):
        sx, sy = swatch_rect(mat)
        uvs = [((sx + 4) / ATLAS, (sy + CELL - 4) / ATLAS), ((sx + CELL - 4) / ATLAS, (sy + CELL - 4) / ATLAS), ((sx + CELL / 2) / ATLAS, (sy + 4) / ATLAS)]
        base = len(self.pos)
        for i in range(3):
            self.pos.append(tuple(float(c) for c in p[i]))
            self.nrm.append(tuple(float(c) for c in n))
            self.uv.append(uvs[i])
            self.joint.append(joint)
        self.idx += [base, base + 1, base + 2]

    def box(self, center, size, mat, joint=0, rot=None, mats=None):
        """mats: optional per side material {'+z': 'face', ...}"""
        c = np.array(center, float)
        h = np.array(size, float) / 2
        R = np.eye(3) if rot is None else rot
        sides = {
            "+x": ((1, 0, 0), [(1, -1, 1), (1, -1, -1), (1, 1, -1), (1, 1, 1)]),
            "-x": ((-1, 0, 0), [(-1, -1, -1), (-1, -1, 1), (-1, 1, 1), (-1, 1, -1)]),
            "+y": ((0, 1, 0), [(-1, 1, 1), (1, 1, 1), (1, 1, -1), (-1, 1, -1)]),
            "-y": ((0, -1, 0), [(-1, -1, -1), (1, -1, -1), (1, -1, 1), (-1, -1, 1)]),
            "+z": ((0, 0, 1), [(-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)]),
            "-z": ((0, 0, -1), [(1, -1, -1), (-1, -1, -1), (-1, 1, -1), (1, 1, -1)]),
        }
        for key, (n, corners) in sides.items():
            m = (mats or {}).get(key, mat)
            pts = [c + R @ (np.array(k) * h) for k in corners]
            self.quad(pts, R @ np.array(n, float), m, joint)

    def cylinder(self, p0, p1, r, sides, mat, joint=0, caps=True, cap_mat=None, r1=None):
        p0, p1 = np.array(p0, float), np.array(p1, float)
        axis = p1 - p0
        L = np.linalg.norm(axis)
        a = axis / L
        up = np.array([0, 1, 0]) if abs(a[1]) < 0.9 else np.array([1, 0, 0])
        u = np.cross(up, a)
        u /= np.linalg.norm(u)
        v = np.cross(a, u)
        r1 = r if r1 is None else r1
        ring0, ring1 = [], []
        for i in range(sides):
            ang = 2 * math.pi * i / sides
            d = u * math.cos(ang) + v * math.sin(ang)
            ring0.append(p0 + d * r)
            ring1.append(p1 + d * r1)
        for i in range(sides):
            j = (i + 1) % sides
            ang = 2 * math.pi * (i + 0.5) / sides
            n = u * math.cos(ang) + v * math.sin(ang)
            self.quad([ring0[i], ring0[j], ring1[j], ring1[i]][::-1], n, mat, joint, (i / sides, 0, (i + 1) / sides, 1))
        if caps:
            cm = cap_mat or mat
            for i in range(1, sides - 1):
                self.tri([ring1[0], ring1[i], ring1[i + 1]], a, cm, joint)
                self.tri([ring0[0], ring0[i + 1], ring0[i]], -a, cm, joint)

    def fix_winding(self):
        """Make every triangle wind CCW around its stored normal (robust to builder mistakes)."""
        P = self.pos
        for t in range(0, len(self.idx), 3):
            a, b, c = self.idx[t:t + 3]
            pa, pb, pc = np.array(P[a]), np.array(P[b]), np.array(P[c])
            n = np.cross(pb - pa, pc - pa)
            if np.dot(n, np.array(self.nrm[a])) < 0:
                self.idx[t + 1], self.idx[t + 2] = c, b

    def data(self):
        self.fix_winding()
        return {"pos": self.pos, "nrm": self.nrm, "uv": self.uv, "joint": self.joint, "idx": self.idx}


def rot_x(deg):
    a = math.radians(deg)
    return np.array([[1, 0, 0], [0, math.cos(a), -math.sin(a)], [0, math.sin(a), math.cos(a)]])


def rot_y(deg):
    a = math.radians(deg)
    return np.array([[math.cos(a), 0, math.sin(a)], [0, 1, 0], [-math.sin(a), 0, math.cos(a)]])


def rot_z(deg):
    a = math.radians(deg)
    return np.array([[math.cos(a), -math.sin(a), 0], [math.sin(a), math.cos(a), 0], [0, 0, 1]])


# ----------------------------------------------------------------------------
# animation helpers
# ----------------------------------------------------------------------------
def track(fn, duration, fps=15, loop=True):
    """fn(t) -> {bone: (x,y,z) degrees}; returns {bone: [(time, quat)]}"""
    n = max(2, int(duration * fps) + 1)
    out = {}
    for i in range(n):
        t = duration * i / (n - 1)
        for bone, ang in fn(t).items():
            out.setdefault(bone, []).append((t, euler(*ang)))
    return out


def trans_track(fn, duration, fps=15):
    n = max(2, int(duration * fps) + 1)
    out = {}
    for i in range(n):
        t = duration * i / (n - 1)
        for bone, v in fn(t).items():
            out.setdefault(bone, []).append((t, v))
    return out


# ----------------------------------------------------------------------------
# characters
# ----------------------------------------------------------------------------
def humanoid_bones(scale=1.0, hunch=0.0):
    s = scale
    B = [
        ("pelvis", -1, (0, 26 * s, 0)),
        ("spine", 0, (0, 31 * s, 0)),
        ("chest", 1, (0, 38 * s, hunch)),
        ("head", 2, (0, 47 * s, hunch * 1.5)),
        ("arm_l", 2, (8 * s, 45 * s, hunch)),
        ("fore_l", 4, (8 * s, 37 * s, hunch)),
        ("arm_r", 2, (-8 * s, 45 * s, hunch)),
        ("fore_r", 6, (-8 * s, 37 * s, hunch)),
        ("thigh_l", 0, (3.6 * s, 25 * s, 0)),
        ("shin_l", 8, (3.6 * s, 13 * s, 0)),
        ("thigh_r", 0, (-3.6 * s, 25 * s, 0)),
        ("shin_r", 10, (-3.6 * s, 13 * s, 0)),
    ]
    return B


PELVIS, SPINE, CHEST, HEAD, ARM_L, FORE_L, ARM_R, FORE_R, THIGH_L, SHIN_L, THIGH_R, SHIN_R = range(12)


def build_grunt():
    m = Mesh()
    A, D, H = "armor", "armor_dark", "helmet"
    # legs
    for side, th, sh in ((1, THIGH_L, SHIN_L), (-1, THIGH_R, SHIN_R)):
        x = 3.6 * side
        m.box((x, 19.5, 0), (4.6, 11, 5.2), D, th)
        m.box((x, 21, 0.6), (5.0, 6, 5.6), A, th)          # thigh armour
        m.box((x, 8, 0), (4.2, 11, 4.6), D, sh)
        m.box((x, 10, 1.4), (4.6, 7, 2.4), A, sh)          # shin guard
        m.box((x, 1.6, 1.0), (5.0, 3.2, 7.4), "gunmetal", sh)  # boot
    # pelvis / belt
    m.box((0, 25.5, 0), (11.5, 5, 7), D, PELVIS)
    m.box((0, 26.5, 3.7), (6, 3, 1), "gunmetal", PELVIS)
    # torso
    m.box((0, 34, 0), (12.5, 8, 7.5), D, SPINE)
    m.box((0, 41, 0.3), (15, 9, 9.5), A, CHEST)
    m.box((0, 40.5, -5.3), (9, 8, 2), D, CHEST)           # backpack
    m.box((0, 45.6, 0), (6, 1.5, 6), D, CHEST)            # collar
    # shoulder pads
    for side in (1, -1):
        m.box((8.7 * side, 45.3, 0), (5, 3.6, 7), A, CHEST, rot=rot_z(-12 * side))
    # head + helmet
    m.box((0, 50.5, 0.4), (6.4, 7, 6.6), "skin", HEAD, mats={"+z": "face"})
    m.box((0, 54.6, 0), (7.6, 2.6, 8.2), H, HEAD)
    m.box((0, 53, -3.4), (7.4, 4.6, 1.4), H, HEAD)
    # arms
    for side, ua, fa in ((1, ARM_L, FORE_L), (-1, ARM_R, FORE_R)):
        x = 8 * side
        m.box((x, 41, 0), (4, 8.5, 4.4), D, ua)
        m.box((x, 33, 0), (3.6, 8, 4.0), A, fa)
        m.box((x, 28.3, 0), (3.2, 2.6, 3.4), "skin", fa)   # hand
    # gun (held by the right forearm, pointing forward along +Z after the arm pose)
    g = FORE_R
    gx = -8
    # in rest pose the forearm hangs down; we model the gun along -Y so the 'hold' pose
    # (forearm rotated -90 deg about X) points it forward.
    m.box((gx + 1.5, 26, 0), (2.6, 14, 3.2), "gunmetal", g)
    m.cylinder((gx + 1.5, 19, 0.4), (gx + 1.5, 13, 0.4), 0.9, 6, "gunmetal", g)
    m.box((gx + 1.5, 28.5, -2.6), (2.2, 3, 3), "gunmetal", g)   # magazine
    return m


def grunt_anims():
    hold_arm = (-70, 0, 0)
    hold_fore = (-25, 0, 0)

    def idle(t):
        b = math.sin(t / 2.0 * 2 * math.pi)
        return {CHEST: (b * 2, 0, 0), HEAD: (0, b * 6, 0), ARM_R: hold_arm, FORE_R: hold_fore,
                ARM_L: (-60, 0, 10), FORE_L: (-35, 0, 0), THIGH_L: (0, 0, 0), THIGH_R: (0, 0, 0)}

    def walk(t):
        p = t / 0.8 * 2 * math.pi
        s = math.sin(p)
        return {THIGH_L: (-s * 32, 0, 0), THIGH_R: (s * 32, 0, 0),
                SHIN_L: (max(0, math.sin(p + 1.4)) * 40, 0, 0), SHIN_R: (max(0, math.sin(p + 1.4 + math.pi)) * 40, 0, 0),
                CHEST: (4, s * 6, 0), ARM_R: hold_arm, FORE_R: hold_fore, ARM_L: (-60, 0, 10), FORE_L: (-35, 0, 0),
                HEAD: (0, -s * 4, 0)}

    def walk_tr(t):
        p = t / 0.8 * 2 * math.pi
        return {PELVIS: (0, 26 + abs(math.sin(p)) * 0.8, 0)}

    def attack(t):
        k = math.exp(-t * 14) if t < 0.5 else 0
        kick = math.sin(t * 60) * 2 * k
        return {ARM_R: (hold_arm[0] - 8 * k, 0, 0), FORE_R: (hold_fore[0] - 6 * k, 0, 0), ARM_L: (-60, 0, 10),
                FORE_L: (-35, 0, 0), CHEST: (-4 * k + kick, 0, 0), HEAD: (-3 * k, 0, 0)}

    def pain(t):
        k = math.sin(min(1, t / 0.35) * math.pi)
        return {CHEST: (14 * k, 10 * k, 0), HEAD: (12 * k, -10 * k, 0), ARM_R: (hold_arm[0] + 30 * k, 0, 0),
                FORE_R: hold_fore, ARM_L: (-30, 0, 30 * k), FORE_L: (-35, 0, 0)}

    def death(t):
        k = min(1.0, t / 0.9)
        e = 1 - (1 - k) ** 2
        return {PELVIS: (-88 * e, 0, 8 * e), CHEST: (-10 * e, 0, 0), HEAD: (-30 * e, 20 * e, 0),
                ARM_R: (-150 * e, 0, -30 * e), FORE_R: (-20, 0, 0), ARM_L: (-160 * e, 0, 40 * e), FORE_L: (-30 * e, 0, 0),
                THIGH_L: (-60 * e, 0, 10 * e), SHIN_L: (70 * e, 0, 0), THIGH_R: (-20 * e, 0, -6 * e), SHIN_R: (30 * e, 0, 0)}

    def death_tr(t):
        k = min(1.0, t / 0.9)
        e = 1 - (1 - k) ** 2
        return {PELVIS: (0, 26 - 21 * e, -10 * e)}

    return [
        ("idle", 2.0, track(idle, 2.0), None),
        ("walk", 0.8, track(walk, 0.8), trans_track(walk_tr, 0.8)),
        ("attack", 0.5, track(attack, 0.5, 30), None),
        ("pain", 0.35, track(pain, 0.35, 30), None),
        ("death", 1.2, track(death, 1.2), trans_track(death_tr, 1.2)),
    ]


def build_fiend():
    m = Mesh()
    S = "demon"
    for side, th, sh in ((1, THIGH_L, SHIN_L), (-1, THIGH_R, SHIN_R)):
        x = 4 * side
        m.box((x, 19.5, 0), (5.4, 11, 6), S, th)
        m.box((x, 8, -1), (4.4, 11, 4.6), S, sh, rot=rot_x(12))
        m.box((x, 1.6, 2.0), (5.4, 3.2, 8), "armor_dark", sh)
        for k in (-1.5, 0, 1.5):
            m.box((x + k, 1.2, 6.6), (0.8, 1.6, 2.4), "white", sh)   # toe claws
    m.box((0, 25.5, 0), (12, 6, 8), S, PELVIS)
    m.box((0, 33, 1), (13, 9, 9), S, SPINE)
    m.box((0, 41, 3), (17, 10, 11), S, CHEST)
    m.box((0, 42, -2.8), (12, 6, 4), "flesh", CHEST)        # exposed back
    # spikes on the back
    for i, y in enumerate((44, 39, 34)):
        m.cylinder((0, y, -4.5), (0, y + 3, -10), 1.6, 4, "white", CHEST if i < 2 else SPINE, r1=0.1)
    # head with horns and glowing eyes
    m.box((0, 49.5, 5), (8, 7, 8), S, HEAD, mats={"+z": "flesh"})
    m.box((0, 46.6, 8.6), (6, 2.2, 3), "white", HEAD)          # teeth
    m.box((-2, 51, 9.1), (1.6, 1.2, 0.4), "glow_blue", HEAD)
    m.box((2, 51, 9.1), (1.6, 1.2, 0.4), "glow_blue", HEAD)
    for side in (1, -1):
        m.cylinder((3.5 * side, 53, 4), (7.5 * side, 59, 1), 1.4, 5, "white", HEAD, r1=0.2)
    # long arms with claws
    for side, ua, fa in ((1, ARM_L, FORE_L), (-1, ARM_R, FORE_R)):
        x = 9.5 * side
        m.box((x, 41, 3), (4.6, 9.5, 5), S, ua)
        m.box((x, 31.5, 3), (4.2, 10, 4.6), S, fa)
        for k in (-1.4, 0, 1.4):
            m.cylinder((x + k, 26.5, 3), (x + k * 1.3, 21.5, 4.5), 0.7, 4, "white", fa, r1=0.1)
    return m


def fiend_anims():
    def idle(t):
        b = math.sin(t / 1.6 * 2 * math.pi)
        return {CHEST: (14 + b * 3, 0, 0), HEAD: (-12, b * 10, 0), ARM_L: (-15, 0, 15), ARM_R: (-15, 0, -15),
                FORE_L: (-25, 0, 0), FORE_R: (-25, 0, 0)}

    def walk(t):
        p = t / 0.55 * 2 * math.pi
        s = math.sin(p)
        return {THIGH_L: (-s * 38, 0, 0), THIGH_R: (s * 38, 0, 0),
                SHIN_L: (max(0, math.sin(p + 1.4)) * 50, 0, 0), SHIN_R: (max(0, math.sin(p + 1.4 + math.pi)) * 50, 0, 0),
                CHEST: (22, s * 8, 0), HEAD: (-18, 0, 0),
                ARM_L: (s * 40 - 20, 0, 15), ARM_R: (-s * 40 - 20, 0, -15), FORE_L: (-30, 0, 0), FORE_R: (-30, 0, 0)}

    def walk_tr(t):
        p = t / 0.55 * 2 * math.pi
        return {PELVIS: (0, 25 + abs(math.sin(p)) * 1.5, 0)}

    def attack(t):
        k = math.sin(min(1, t / 0.5) * math.pi)
        return {ARM_R: (-130 * k, 0, -15), FORE_R: (-20 - 40 * k, 0, 0), ARM_L: (-100 * k, 0, 15), FORE_L: (-20 - 50 * k, 0, 0),
                CHEST: (20 + 20 * k, 0, 0), HEAD: (-10 - 15 * k, 0, 0)}

    def pain(t):
        k = math.sin(min(1, t / 0.35) * math.pi)
        return {CHEST: (-15 * k, 15 * k, 0), HEAD: (20 * k, 0, 0)}

    def death(t):
        k = min(1.0, t / 0.8)
        e = 1 - (1 - k) ** 2
        return {PELVIS: (80 * e, 0, -10 * e), CHEST: (20 * e, 0, 0), HEAD: (30 * e, 0, 0), ARM_L: (-90 * e, 0, 50 * e),
                ARM_R: (-80 * e, 0, -60 * e), THIGH_L: (-80 * e, 0, 0), SHIN_L: (60 * e, 0, 0), THIGH_R: (-40 * e, 0, 0)}

    def death_tr(t):
        k = min(1.0, t / 0.8)
        e = 1 - (1 - k) ** 2
        return {PELVIS: (0, 26 - 21 * e, 8 * e)}

    return [
        ("idle", 1.6, track(idle, 1.6), None),
        ("walk", 0.55, track(walk, 0.55, 20), trans_track(walk_tr, 0.55, 20)),
        ("attack", 0.5, track(attack, 0.5, 30), None),
        ("pain", 0.35, track(pain, 0.35, 30), None),
        ("death", 1.0, track(death, 1.0), trans_track(death_tr, 1.0)),
    ]


# ----------------------------------------------------------------------------
# weapons (first person + world pickups). Barrel points +Z, origin at the grip.
# ----------------------------------------------------------------------------
def build_shotgun():
    m = Mesh()
    for x in (-1.45, 1.45):
        m.cylinder((x, 2.6, 4), (x, 2.6, 34), 1.45, 8, "gunmetal")
        m.cylinder((x, 2.6, 33.2), (x, 2.6, 34.2), 1.6, 8, "stripes")
    m.box((0, 1.5, 6), (5.4, 3.6, 12), "gunmetal")            # receiver
    m.box((0, 1.0, 3.5), (5.6, 2, 5), "stripes")               # yellow detail
    m.box((0, 0.4, 18), (4.6, 2.2, 10), "stripes", mats={"+y": "gunmetal"})  # fore grip
    m.box((0, -0.6, -4), (4, 4, 8), "gunmetal", rot=rot_x(18))  # grip/stock
    m.box((0, -3.4, -11), (3.8, 5, 8), "gunmetal", rot=rot_x(25))
    m.box((0, 4.6, 6), (1, 0.8, 2), "stripes")                 # sight
    return m


def build_plasma():
    m = Mesh()
    m.cylinder((0, 2.8, 0), (0, 2.8, 24), 3.2, 8, "gold")
    m.cylinder((0, 2.8, 24), (0, 2.8, 30), 2.2, 8, "gunmetal")
    for z in range(2, 24, 4):
        m.cylinder((0, 2.8, z), (0, 2.8, z + 1.4), 3.6, 8, "gold")       # rings
    m.box((-3.4, 2.8, 12), (0.8, 2.4, 22), "glow_blue")                   # coil window
    m.box((3.4, 2.8, 12), (0.8, 2.4, 22), "glow_blue")
    m.box((0, 6.6, 8), (2, 1.4, 14), "gold")                              # top rail
    for z in range(2, 22, 3):
        m.box((0, 7.6, z), (2.6, 0.8, 1), "gold")
    m.box((0, -1.6, 2), (2.6, 6, 3.2), "gunmetal", rot=rot_x(12))         # grip
    m.box((0, 0.6, -3), (3, 3, 5), "gold")
    return m


def build_pistol():
    m = Mesh()
    m.box((0, 2.6, 6), (2.8, 2.6, 13), "gunmetal")
    m.cylinder((0, 2.4, 12), (0, 2.4, 14), 0.8, 6, "gunmetal")
    m.box((0, -1.2, 1.5), (2.4, 6, 3.4), "wood", rot=rot_x(14))
    m.box((0, 0.6, 5), (2.2, 1.4, 4), "gunmetal")
    return m


def build_rocket():
    m = Mesh()
    m.cylinder((0, 3.2, -8), (0, 3.2, 28), 3.6, 8, "olive", cap_mat="gunmetal")
    m.cylinder((0, 3.2, 26), (0, 3.2, 30), 4.0, 8, "gunmetal")
    m.box((0, -1.4, 4), (2.6, 6, 3.4), "gunmetal", rot=rot_x(10))
    m.box((0, 7.6, 6), (1.6, 1.6, 8), "gunmetal")
    m.box((0, 3.2, 14), (7.8, 1.2, 3), "stripes")
    return m


def build_rocket_proj():
    m = Mesh()
    m.cylinder((0, 0, -6), (0, 0, 4), 1.4, 6, "olive")
    m.cylinder((0, 0, 4), (0, 0, 8), 1.4, 6, "stripes", r1=0.1)
    for a in (0, 90, 180, 270):
        m.box((0, 0, -5), (0.3, 5, 3), "gunmetal", rot=rot_z(a))
    return m


# ----------------------------------------------------------------------------
# items / props / gibs  (origin at the bottom centre, ~32 unit pickups)
# ----------------------------------------------------------------------------
def build_medkit():
    m = Mesh()
    m.box((0, 6, 0), (18, 12, 12), "white", mats={"+y": "medkit", "+z": "medkit", "-z": "medkit"})
    m.box((0, 12.8, 0), (6, 1.6, 2), "gunmetal")
    return m


def build_armor():
    m = Mesh()
    m.box((0, 10, 0), (16, 18, 6), "olive")
    m.box((0, 13, 3.4), (12, 10, 1.2), "armor")
    m.box((-6, 20, 0), (4, 3, 6), "olive")
    m.box((6, 20, 0), (4, 3, 6), "olive")
    return m


def build_shells():
    m = Mesh()
    m.box((0, 4, 0), (14, 8, 9), "barrel", mats={"+y": "armor_dark"})
    for x in (-4, 0, 4):
        m.cylinder((x, 8, 0), (x, 12, 0), 1.6, 6, "flesh", cap_mat="gold")
    return m


def build_cells():
    m = Mesh()
    m.box((0, 7, 0), (10, 14, 10), "gunmetal")
    m.box((0, 7, 0), (10.6, 9, 7), "glow_blue")
    m.box((0, 14.6, 0), (4, 1.2, 4), "gold")
    return m


def build_rockets_ammo():
    m = Mesh()
    m.box((0, 3, 0), (16, 6, 10), "olive")
    for x in (-4, 0, 4):
        m.cylinder((x, 6, -4), (x, 6, 6), 1.6, 6, "olive", cap_mat="stripes")
    return m


def build_keycard():
    m = Mesh()
    m.box((0, 8, 0), (10, 14, 1), "white", mats={"+z": "white", "-z": "white"})
    m.box((0, 11, 0.6), (8, 3, 0.4), "gunmetal")
    return m


def build_barrel():
    m = Mesh()
    m.cylinder((0, 0, 0), (0, 40, 0), 13, 10, "barrel", cap_mat="gunmetal")
    m.cylinder((0, 12, 0), (0, 14, 0), 13.6, 10, "gunmetal", caps=False)
    m.cylinder((0, 28, 0), (0, 30, 0), 13.6, 10, "gunmetal", caps=False)
    return m


def build_gib(kind):
    m = Mesh()
    if kind == "head":
        m.box((0, 3.5, 0), (6.4, 7, 6.6), "skin", mats={"+z": "face", "-y": "flesh"})
        m.box((0, 7.6, 0), (7.6, 2.4, 8.2), "helmet")
    elif kind == "chunk":
        m.box((0, 3, 0), (8, 6, 7), "flesh", rot=rot_y(20))
        m.box((2, 4, 2), (5, 5, 4), "armor", rot=rot_x(30))
    elif kind == "small1":
        m.box((0, 2, 0), (5, 4, 4), "flesh", rot=rot_z(25))
    else:
        m.cylinder((0, 1, -4), (0, 2, 4), 1.6, 5, "flesh", cap_mat="white")
    return m


def save_static(name, mesh):
    path = os.path.join(OUT, name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    depth = name.count("/")
    uri = "../" * depth + "atlas.png"
    write_glb(path, mesh.data(), uri)
    print("  wrote", path, len(mesh.pos), "verts")


def save_character(name, mesh, bones, anims):
    path = os.path.join(OUT, name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    uri = "../" * name.count("/") + "atlas.png"
    write_glb(path, mesh.data(), uri, bones=bones, animations=anims)
    print("  wrote", path, len(mesh.pos), "verts,", len(anims), "animations")


def main():
    os.makedirs(OUT, exist_ok=True)
    make_atlas().save(os.path.join(OUT, "atlas.png"), optimize=True)
    print("  wrote atlas.png")
    save_character("monsters/grunt.glb", build_grunt(), humanoid_bones(), grunt_anims())
    save_character("monsters/fiend.glb", build_fiend(), humanoid_bones(1.0, 3.0), fiend_anims())
    for prefix in ("v", "w"):
        save_static("weapons/%s_shotgun.glb" % prefix, build_shotgun())
        save_static("weapons/%s_plasma.glb" % prefix, build_plasma())
        save_static("weapons/%s_pistol.glb" % prefix, build_pistol())
        save_static("weapons/%s_rocket.glb" % prefix, build_rocket())
    save_static("weapons/rocket_proj.glb", build_rocket_proj())
    save_static("items/medkit.glb", build_medkit())
    save_static("items/armor.glb", build_armor())
    save_static("items/shells.glb", build_shells())
    save_static("items/cells.glb", build_cells())
    save_static("items/rockets.glb", build_rockets_ammo())
    save_static("items/keycard.glb", build_keycard())
    save_static("props/barrel.glb", build_barrel())
    for g in ("head", "chunk", "small1", "small2"):
        save_static("gibs/%s.glb" % g, build_gib(g))


if __name__ == "__main__":
    main()
