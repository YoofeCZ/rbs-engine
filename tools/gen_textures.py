#!/usr/bin/env python3
"""RBS Engine - procedural texture generator for the base game.

Generates all placeholder textures, decals, sprites and HUD images into base/.
Every texture is tileable. Requires numpy and Pillow:  pip install numpy pillow

Usage:  python3 tools/gen_textures.py [--out base]

Feel free to replace any generated file with hand made art - the engine just
loads PNG files from textures/, sprites/, decals/ and hud/.
"""
import argparse
import json
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

RNG = np.random.default_rng(1337)
S = 256


# ----------------------------------------------------------------------------
# noise helpers (all tileable)
# ----------------------------------------------------------------------------
def value_noise(size, freq, rng):
    grid = rng.random((freq, freq))
    xs = np.arange(size) * freq / size
    i0 = np.floor(xs).astype(int)
    t = xs - i0
    t = t * t * (3 - 2 * t)
    i1 = (i0 + 1) % freq
    i0 = i0 % freq
    # interpolate rows then columns (wrapping => tileable)
    a = grid[i0][:, i0] * (1 - t)[None, :] + grid[i0][:, i1] * t[None, :]
    b = grid[i1][:, i0] * (1 - t)[None, :] + grid[i1][:, i1] * t[None, :]
    return a * (1 - t)[:, None] + b * t[:, None]


def fbm(size=S, base=4, octaves=5, persistence=0.5, rng=RNG):
    total = np.zeros((size, size))
    amp, norm = 1.0, 0.0
    f = base
    for _ in range(octaves):
        if f > size:
            break
        total += value_noise(size, f, rng) * amp
        norm += amp
        amp *= persistence
        f *= 2
    return total / norm


def ridged(size=S, base=4, octaves=5, rng=RNG):
    n = fbm(size, base, octaves, rng=rng)
    return 1.0 - np.abs(n * 2 - 1)


def normalize(a):
    lo, hi = a.min(), a.max()
    return (a - lo) / (hi - lo + 1e-9)


def shade(height, strength=2.0, light=(-1, -1)):
    """Fake lighting from a height map (emboss)."""
    gx = np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)
    gy = np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)
    l = np.array(light, dtype=float)
    l /= np.linalg.norm(l)
    s = (gx * l[0] + gy * l[1]) * strength
    return np.clip(1.0 + s, 0.4, 1.6)


def colorize(t, stops):
    """t in [0,1] -> rgb using gradient stops [(pos, (r,g,b)), ...]"""
    t = np.clip(t, 0, 1)
    out = np.zeros(t.shape + (3,))
    for i in range(len(stops) - 1):
        p0, c0 = stops[i]
        p1, c1 = stops[i + 1]
        m = (t >= p0) & (t <= p1)
        k = ((t - p0) / max(p1 - p0, 1e-6))[m][:, None]
        out[m] = np.array(c0) * (1 - k) + np.array(c1) * k
    return out


def to_img(rgb, alpha=None):
    rgb = np.clip(rgb, 0, 255).astype(np.uint8)
    if alpha is not None:
        a = np.clip(alpha, 0, 255).astype(np.uint8)
        return Image.fromarray(np.dstack([rgb, a]), "RGBA")
    return Image.fromarray(rgb, "RGB")


def grime(size=S, amount=0.35, rng=RNG):
    g = fbm(size, 3, 6, rng=rng)
    streak = value_noise(size, 16, rng)
    streak = np.repeat(streak.mean(axis=0, keepdims=True), size, axis=0)  # vertical drips
    return 1.0 - amount * np.clip((g - 0.45) * 2.5 + (streak - 0.5) * 0.8, 0, 1)


# ----------------------------------------------------------------------------
# textures
# ----------------------------------------------------------------------------
def bricks(base_col, mortar_col, rows=16, cols=8, var=0.18, seed=1, dark=0.0):
    rng = np.random.default_rng(seed)
    h = np.zeros((S, S))
    col = np.zeros((S, S, 3))
    bh = S // rows
    bw = S // cols
    mortar = 2
    yy, xx = np.mgrid[0:S, 0:S]
    row = yy // bh
    off = (row % 2) * (bw // 2)
    bx = ((xx + off) // bw) % cols
    brick_id = row * cols + bx
    tints = rng.normal(1.0, var, rows * cols)
    tint2 = rng.normal(0.0, 0.06, (rows * cols, 3))
    lx = (xx + off) % bw
    ly = yy % bh
    is_mortar = (lx < mortar) | (ly < mortar)
    edge = np.minimum(np.minimum(lx, bw - 1 - lx), np.minimum(ly, bh - 1 - ly)).astype(float)
    bevel = np.clip(edge / 3.0, 0, 1)
    n = fbm(S, 16, 5, rng=rng)
    chips = (fbm(S, 32, 3, rng=rng) > 0.68) & ~is_mortar
    base = np.array(base_col, dtype=float)
    col[:] = base * tints[brick_id][..., None] * (1 + tint2[brick_id])
    col *= (0.8 + 0.4 * n)[..., None]
    col[is_mortar] = np.array(mortar_col) * (0.85 + 0.3 * n[is_mortar])[..., None]
    col[chips] *= 0.7
    h = np.where(is_mortar, 0.0, bevel * 0.8 + n * 0.3)
    col *= shade(h, 1.6)[..., None]
    col *= grime(S, 0.35 + dark, rng)[..., None]
    return to_img(col)


def concrete(c0, c1, seed=2, panels=False, stains=0.4):
    rng = np.random.default_rng(seed)
    n = fbm(S, 4, 7, rng=rng)
    fine = fbm(S, 64, 3, rng=rng)
    t = n * 0.75 + fine * 0.25
    col = colorize(t, [(0, c0), (1, c1)])
    pits = fbm(S, 64, 2, rng=rng) > 0.72
    col[pits] *= 0.75
    h = t
    if panels:
        yy, xx = np.mgrid[0:S, 0:S]
        seam = (yy % 128 < 2) | (xx % 128 < 2)
        col[seam] *= 0.55
        h = np.where(seam, 0, h)
        holes = ((yy % 128 - 64) ** 2 + (xx % 128 - 64) ** 2) < 0  # placeholder
    col *= shade(h, 1.2)[..., None]
    col *= grime(S, stains, rng)[..., None]
    return to_img(col)


def wall_panel(c_top, c_bot, seed=3):
    """Yellow-ish concrete with a horizontal line pattern (lower walls in the screenshots)."""
    rng = np.random.default_rng(seed)
    n = fbm(S, 4, 6, rng=rng)
    col = colorize(n, [(0, c_bot), (1, c_top)])
    yy, xx = np.mgrid[0:S, 0:S]
    lines = (yy % 64 < 2)
    vert = (xx % 128 < 2)
    col[lines | vert] *= 0.6
    drips = value_noise(S, 32, rng)
    drips = np.repeat(drips[:1], S, axis=0) * np.linspace(1.0, 0.3, S)[:, None]
    col *= (1 - 0.35 * np.clip(drips - 0.4, 0, 1) * 2)[..., None]
    col *= grime(S, 0.4, rng)[..., None]
    return to_img(col)


def metal_door(seed=4):
    rng = np.random.default_rng(seed)
    base = np.array([196, 182, 150], dtype=float)
    n = fbm(S, 8, 6, rng=rng)
    col = base * (0.8 + 0.35 * n)[..., None]
    img = to_img(col)
    d = ImageDraw.Draw(img)
    # double door: two leaves with 3x2 panels each
    for leaf in range(2):
        x0 = leaf * 128
        d.rectangle([x0 + 2, 2, x0 + 125, 253], outline=(110, 100, 85), width=3)
        for r in range(3):
            for c in range(2):
                px = x0 + 12 + c * 56
                py = 14 + r * 80
                d.rectangle([px, py, px + 48, py + 70], outline=(130, 118, 98), width=3)
                d.line([px + 2, py + 72, px + 50, py + 72], fill=(225, 214, 188), width=1)
        # rivets
        for yv in range(8, 250, 24):
            d.ellipse([x0 + 5, yv, x0 + 8, yv + 3], fill=(120, 110, 95))
            d.ellipse([x0 + 119, yv, x0 + 122, yv + 3], fill=(120, 110, 95))
    # handle bars
    d.rectangle([116, 110, 120, 160], fill=(90, 85, 80))
    d.rectangle([136, 110, 140, 160], fill=(90, 85, 80))
    arr = np.asarray(img).astype(float)
    rust = normalize(fbm(S, 6, 6, rng=rng))
    rust_mask = np.clip((rust - 0.6) * 4, 0, 1)
    arr = arr * (1 - rust_mask[..., None]) + np.array([120, 70, 35]) * rust_mask[..., None] * (0.7 + 0.5 * n[..., None])
    arr *= grime(S, 0.3, rng)[..., None]
    return to_img(arr)


def metal_plate(seed=5):
    rng = np.random.default_rng(seed)
    n = fbm(S, 8, 6, rng=rng)
    col = colorize(n, [(0, (70, 72, 74)), (1, (130, 132, 130))])
    yy, xx = np.mgrid[0:S, 0:S]
    # diamond tread
    u = (xx + yy) % 32
    v = (xx - yy) % 32
    dia = (np.abs(u - 16) < 3) & ((v % 32) < 12)
    h = dia.astype(float)
    col *= shade(h, 1.5)[..., None]
    seam = (xx % 128 < 2) | (yy % 128 < 2)
    col[seam] *= 0.5
    col *= grime(S, 0.3, rng)[..., None]
    return to_img(col)


def rust_tex(seed=6):
    rng = np.random.default_rng(seed)
    n = fbm(S, 4, 7, rng=rng)
    r = ridged(S, 8, 5, rng=rng)
    col = colorize(n * 0.7 + r * 0.3, [(0, (60, 28, 14)), (0.4, (130, 60, 25)), (0.7, (170, 95, 45)), (1, (110, 80, 60))])
    col *= shade(r, 1.2)[..., None]
    return to_img(col)


def tiles(c_tile, c_grout, size=32, seed=7, dirt=0.25, hframe=None):
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:S, 0:S]
    lx = xx % size
    ly = yy % size
    grout = (lx < 2) | (ly < 2)
    tid = (yy // size) * 64 + (xx // size)
    var = rng.normal(1.0, 0.04, 64 * 64)[tid]
    n = fbm(S, 16, 4, rng=rng)
    col = np.array(c_tile, float) * (var * (0.92 + 0.12 * n))[..., None]
    col[grout] = np.array(c_grout) * (0.9 + 0.2 * n[grout])[..., None]
    edge = np.minimum(np.minimum(lx, size - 1 - lx), np.minimum(ly, size - 1 - ly)).astype(float)
    h = np.where(grout, 0, np.clip(edge / 3, 0, 1))
    col *= shade(h, 0.8)[..., None]
    col *= grime(S, dirt, rng)[..., None]
    return to_img(col)


def wall_tiles_two_tone(seed=8):
    """White tiles on top, green tiles at the bottom (subway tunnel)."""
    a = np.asarray(tiles((214, 222, 226), (150, 155, 158), 32, seed)).astype(float)
    b = np.asarray(tiles((70, 110, 98), (40, 60, 55), 32, seed + 1)).astype(float)
    yy = np.mgrid[0:S, 0:S][0]
    out = np.where((yy < 128)[..., None], b, a)
    return to_img(out)


def checker(c0, c1, size=32, seed=9):
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:S, 0:S]
    chk = ((xx // size) + (yy // size)) % 2
    n = fbm(S, 16, 5, rng=rng)
    col = np.where(chk[..., None] == 0, np.array(c0, float), np.array(c1, float)) * (0.9 + 0.18 * n)[..., None]
    specks = fbm(S, 128, 1, rng=rng) > 0.8
    col[specks] *= 0.85
    grout = ((xx % size) < 1) | ((yy % size) < 1)
    col[grout] *= 0.7
    col *= grime(S, 0.2, rng)[..., None]
    return to_img(col)


def perforated(c, seed=10):
    rng = np.random.default_rng(seed)
    n = fbm(S, 8, 5, rng=rng)
    col = np.array(c, float) * (0.9 + 0.15 * n)[..., None]
    yy, xx = np.mgrid[0:S, 0:S]
    sp = 12
    off = ((yy // sp) % 2) * (sp // 2)
    dx = (xx + off) % sp - sp / 2
    dy = yy % sp - sp / 2
    hole = dx * dx + dy * dy < 7
    col[hole] *= 0.35
    h = np.where(hole, 0.0, 1.0)
    col *= shade(h, 0.4)[..., None]
    return to_img(col)


def asphalt(seed=11):
    rng = np.random.default_rng(seed)
    n = fbm(S, 8, 6, rng=rng)
    g = fbm(S, 128, 1, rng=rng)
    t = n * 0.5 + g * 0.5
    col = colorize(t, [(0, (28, 28, 30)), (0.5, (52, 50, 50)), (1, (78, 76, 74))])
    cracks = ridged(S, 4, 4, rng=rng) > 0.94
    col[cracks] *= 0.5
    return to_img(col)


def sidewalk(seed=12):
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:S, 0:S]
    size = 64
    lx, ly = xx % size, yy % size
    seam = (lx < 3) | (ly < 3)
    n = fbm(S, 16, 6, rng=rng)
    tid = (yy // size) * 8 + xx // size
    var = rng.normal(1, 0.07, 64)[tid]
    col = np.array([118, 112, 104], float) * (var * (0.82 + 0.3 * n))[..., None]
    col[seam] = np.array([60, 56, 52]) * (0.8 + 0.3 * n[seam])[..., None]
    h = np.where(seam, 0, 1.0) + n * 0.3
    col *= shade(h, 0.8)[..., None]
    col *= grime(S, 0.3, rng)[..., None]
    return to_img(col)


def wood(c0, c1, planks=4, seed=13, vertical=False):
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:S, 0:S]
    if vertical:
        xx, yy = yy, xx
    pw = S // planks
    pid = yy // pw
    grain = value_noise(S, 64, rng)
    stretch = np.repeat(value_noise(S, 8, rng)[::8], 8, axis=0)[:S]
    g = np.sin((xx * 0.05 + grain * 6 + pid * 13) * 3) * 0.5 + 0.5
    g = g * 0.6 + fbm(S, 32, 4, rng=rng) * 0.4
    col = colorize(g, [(0, c0), (1, c1)]) * rng.normal(1, 0.08, planks)[pid][..., None]
    seam = (yy % pw) < 2
    col[seam] *= 0.45
    col *= grime(S, 0.2, rng)[..., None]
    if vertical:
        col = np.transpose(col, (1, 0, 2))
    return to_img(col)


def crate(seed=14):
    img = wood((120, 82, 45), (176, 128, 74), 4, seed)
    d = ImageDraw.Draw(img)
    frame = (92, 62, 34)
    d.rectangle([0, 0, 255, 255], outline=frame, width=18)
    d.line([18, 18, 237, 237], fill=frame, width=20)
    arr = np.asarray(img).astype(float)
    arr *= shade(np.asarray(img.convert("L")).astype(float) / 255.0, 0.6)[..., None]
    for x, y in [(9, 9), (246, 9), (9, 246), (246, 246)]:
        d.ellipse([x - 3, y - 3, x + 3, y + 3], fill=(60, 55, 50))
    return to_img(arr)


def window(seed=15, lit=False):
    rng = np.random.default_rng(seed)
    frame_col = np.array([70, 64, 56], float)
    n = fbm(S, 8, 5, rng=rng)
    col = frame_col * (0.8 + 0.3 * n)[..., None]
    yy, xx = np.mgrid[0:S, 0:S]
    cell = 64
    lx, ly = xx % cell, yy % cell
    glass = (lx > 6) & (ly > 6)
    refl = np.clip(1 - (xx + yy) / 512, 0, 1)
    gcol = np.array([150, 170, 170], float) if not lit else np.array([255, 210, 140], float)
    gl = gcol * (0.35 + 0.4 * refl + 0.25 * fbm(S, 4, 4, rng=rng))[..., None]
    broken = (fbm(S, 16, 3, rng=rng) > 0.66)
    gl[broken] = np.array([20, 18, 16])
    col[glass] = gl[glass]
    col *= grime(S, 0.25, rng)[..., None]
    return to_img(col)


def sign_beer():
    img = Image.new("RGB", (512, 256), (190, 160, 110))
    arr = np.asarray(img).astype(float)
    n = fbm(256, 4, 6)
    n = np.repeat(np.repeat(n, 2, axis=1), 1, axis=0)[:, :512][:256]
    arr *= (0.75 + 0.35 * n[..., None])
    img = to_img(arr)
    d = ImageDraw.Draw(img)
    try:
        f1 = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf", 110)
        f2 = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf", 30)
    except OSError:
        f1 = f2 = ImageFont.load_default()
    d.rectangle([8, 8, 503, 247], outline=(120, 40, 25), width=6)
    d.text((256, 105), "PIVO", font=f1, fill=(170, 40, 25), anchor="mm")
    d.text((256, 200), "BOHEMIAN LAGER", font=f2, fill=(90, 50, 30), anchor="mm")
    arr = np.asarray(img).astype(float)
    peel = fbm(256, 8, 5)
    peel = np.repeat(peel, 2, axis=1)[:, :512]
    m = np.clip((peel - 0.62) * 5, 0, 1)
    arr = arr * (1 - m[..., None]) + np.array([90, 80, 70]) * m[..., None]
    return to_img(arr)


def light_panel():
    img = Image.new("RGB", (128, 64), (60, 62, 64))
    d = ImageDraw.Draw(img)
    d.rectangle([4, 6, 123, 57], fill=(150, 152, 150))
    for i in range(3):
        y = 14 + i * 14
        d.rectangle([10, y, 117, y + 8], fill=(255, 255, 240))
    return img


def stripes(c0, c1, seed=16):
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:S, 0:S]
    st = ((xx + yy) // 32) % 2
    n = fbm(S, 8, 5, rng=rng)
    col = np.where(st[..., None] == 0, np.array(c0, float), np.array(c1, float)) * (0.85 + 0.2 * n)[..., None]
    col *= grime(S, 0.35, rng)[..., None]
    return to_img(col)


def roof(seed=17):
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:S, 0:S]
    rows = 16
    rh = S // rows
    off = ((yy // rh) % 2) * 16
    lx = (xx + off) % 32
    ly = yy % rh
    n = fbm(S, 16, 5, rng=rng)
    tid = (yy // rh) * 8 + (xx + off) // 32
    var = rng.normal(1, 0.1, 2000)[tid % 2000]
    col = np.array([150, 62, 40], float) * (var * (0.8 + 0.3 * n))[..., None]
    h = ly / rh + np.where(lx < 2, -1, 0)
    col *= shade(h, 1.4)[..., None]
    col *= grime(S, 0.3, rng)[..., None]
    return to_img(col)


def grate():
    img = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for i in range(0, 128, 16):
        d.rectangle([i, 0, i + 3, 127], fill=(90, 92, 95, 255))
        d.rectangle([0, i, 127, i + 3], fill=(80, 82, 85, 255))
    d.rectangle([0, 0, 127, 127], outline=(60, 60, 62, 255), width=4)
    return img


def fence():
    img = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for i in range(-128, 256, 16):
        d.line([i, 0, i + 128, 128], fill=(150, 150, 150, 255), width=2)
        d.line([i, 128, i + 128, 0], fill=(130, 130, 130, 255), width=2)
    return img


def tunnel_stone(seed=18):
    rng = np.random.default_rng(seed)
    n = fbm(S, 4, 7, rng=rng)
    r = ridged(S, 6, 5, rng=rng)
    col = colorize(n * 0.6 + r * 0.4, [(0, (30, 30, 32)), (0.6, (64, 62, 60)), (1, (92, 88, 84))])
    col *= shade(r, 1.2)[..., None]
    col *= grime(S, 0.3, rng)[..., None]
    return to_img(col)


def office_wall(seed=19):
    rng = np.random.default_rng(seed)
    n = fbm(S, 4, 6, rng=rng)
    col = colorize(n, [(0, (150, 168, 172)), (1, (190, 206, 208))])
    yy = np.mgrid[0:S, 0:S][0]
    col[yy > 200] = np.array([60, 105, 92]) * (0.9 + 0.2 * n[yy > 200])[..., None]
    col[(yy > 196) & (yy <= 200)] *= 0.5
    col *= grime(S, 0.15, rng)[..., None]
    return to_img(col)


def cabinet(seed=20):
    rng = np.random.default_rng(seed)
    n = fbm(S, 8, 5, rng=rng)
    col = np.array([70, 100, 84], float) * (0.85 + 0.25 * n)[..., None]
    img = to_img(col)
    d = ImageDraw.Draw(img)
    for i in range(4):
        y0 = 4 + i * 63
        d.rectangle([6, y0, 249, y0 + 58], outline=(45, 64, 54), width=3)
        d.rectangle([108, y0 + 22, 148, y0 + 30], fill=(170, 170, 160))
        d.rectangle([116, y0 + 8, 140, y0 + 16], fill=(210, 205, 190))
    return img


def blinds(seed=21):
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:S, 0:S]
    sl = yy % 16
    t = np.clip(sl / 15.0, 0, 1)
    col = colorize(t, [(0, (90, 100, 105)), (0.5, (200, 208, 210)), (1, (130, 138, 140))])
    col *= (0.9 + 0.15 * fbm(S, 16, 3, rng=rng))[..., None]
    return to_img(col)


def tool_tex(color, text):
    img = Image.new("RGBA", (64, 64), color + (255,))
    d = ImageDraw.Draw(img)
    for i in range(-64, 64, 16):
        d.line([i, 64, i + 64, 0], fill=tuple(int(c * 0.7) for c in color) + (255,), width=4)
    d.text((4, 26), text, fill=(255, 255, 255, 255))
    return img


# ----------------------------------------------------------------------------
# decals / sprites / hud
# ----------------------------------------------------------------------------
def blood_decal(seed, size=128):
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:size, 0:size] - size / 2
    r = np.sqrt(xx ** 2 + yy ** 2) / (size / 2)
    ang = np.arctan2(yy, xx)
    wob = sum(rng.random() * 0.25 * np.sin(ang * k + rng.random() * 6) for k in (3, 5, 7, 11))
    blob = r < (0.45 + wob * 0.6)
    a = blob.astype(float)
    # droplets + streaks
    img = Image.fromarray((a * 255).astype(np.uint8), "L")
    d = ImageDraw.Draw(img)
    for _ in range(rng.integers(8, 18)):
        an = rng.random() * 6.28
        dist = rng.uniform(0.35, 0.9) * size / 2
        rad = rng.uniform(1.5, 6)
        cx, cy = size / 2 + math.cos(an) * dist, size / 2 + math.sin(an) * dist
        d.ellipse([cx - rad, cy - rad, cx + rad, cy + rad], fill=255)
        d.line([size / 2, size / 2, cx, cy], fill=255, width=int(rad))
    img = img.filter(ImageFilter.GaussianBlur(1.2))
    a = np.asarray(img).astype(float) / 255.0
    a = np.clip((a - 0.35) * 3, 0, 1)
    shade_n = value_noise(size, 8, rng)
    col = np.zeros((size, size, 3))
    col[..., 0] = 110 + 50 * shade_n
    col[..., 1] = 6 + 8 * shade_n
    col[..., 2] = 6 + 8 * shade_n
    return to_img(col, a * 235)


def soft_dot(size=64, color=(255, 255, 255), power=2.0, core=0.0):
    yy, xx = np.mgrid[0:size, 0:size] - (size - 1) / 2
    r = np.sqrt(xx ** 2 + yy ** 2) / (size / 2)
    a = np.clip(1 - r, 0, 1) ** power
    if core > 0:
        a = np.maximum(a, np.clip(1 - r / core, 0, 1))
    col = np.zeros((size, size, 3)) + np.array(color, float)
    return to_img(col, a * 255)


def smoke_sprite(seed=30, size=64):
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:size, 0:size] - (size - 1) / 2
    r = np.sqrt(xx ** 2 + yy ** 2) / (size / 2)
    n = fbm(size, 4, 4, rng=rng)
    a = np.clip(1 - r, 0, 1) ** 1.5 * (0.5 + 0.8 * n)
    col = np.zeros((size, size, 3)) + 255
    return to_img(col, np.clip(a, 0, 1) * 255)


def fire_sprite(seed=31, size=64):
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:size, 0:size] - (size - 1) / 2
    r = np.sqrt(xx ** 2 + yy ** 2) / (size / 2)
    n = fbm(size, 4, 4, rng=rng)
    a = np.clip(1 - r, 0, 1) ** 1.2 * (0.6 + 0.6 * n)
    t = np.clip(a, 0, 1)
    col = colorize(t, [(0, (120, 20, 0)), (0.4, (255, 110, 20)), (0.8, (255, 210, 120)), (1, (255, 255, 230))])
    return to_img(col, np.clip(a * 1.4, 0, 1) * 255)


def fire_sheet(frames=8, size=64, seed=40):
    rng = np.random.default_rng(seed)
    sheet = Image.new("RGBA", (size * frames, size), (0, 0, 0, 0))
    for f in range(frames):
        yy, xx = np.mgrid[0:size, 0:size].astype(float)
        nx = xx / size - 0.5
        ny = 1 - yy / size
        n = fbm(size, 4, 4, rng=rng)
        width = 0.35 * (1 - ny) ** 0.7 + 0.02
        flame = np.clip(1 - np.abs(nx + (n - 0.5) * 0.25 * ny) / width, 0, 1) * np.clip(1.2 - ny * 1.1 + (n - 0.5) * 0.6, 0, 1)
        col = colorize(flame, [(0, (90, 10, 0)), (0.3, (230, 80, 10)), (0.7, (255, 190, 80)), (1, (255, 250, 220))])
        sheet.paste(to_img(col, np.clip(flame * 1.6, 0, 1) * 255), (f * size, 0))
    return sheet


def muzzle(size=64, seed=32):
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:size, 0:size] - (size - 1) / 2
    r = np.sqrt(xx ** 2 + yy ** 2) / (size / 2)
    ang = np.arctan2(yy, xx)
    spikes = np.clip(np.cos(ang * 5 + 0.3) * 0.5 + 0.5, 0, 1) ** 3
    a = np.clip(1 - r / (0.35 + 0.65 * spikes), 0, 1) ** 1.5
    col = colorize(a, [(0, (255, 120, 20)), (0.6, (255, 220, 140)), (1, (255, 255, 255))])
    return to_img(col, a * 255)


def face(level, size=64):
    """Crude pixel-art marine face; level 0 = healthy ... 4 = almost dead."""
    img = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    skin = (196, 140, 104)
    dark = (120, 80, 56)
    helmet = (86, 92, 62)
    d.rectangle([7, 9, 24, 29], fill=skin)            # face
    d.rectangle([5, 3, 26, 11], fill=helmet)          # helmet
    d.rectangle([4, 9, 7, 16], fill=helmet)
    d.rectangle([24, 9, 27, 16], fill=helmet)
    d.rectangle([9, 13, 14, 15], fill=(255, 255, 255))  # eyes
    d.rectangle([17, 13, 22, 15], fill=(255, 255, 255))
    d.rectangle([11, 13, 13, 15], fill=(40, 60, 90))
    d.rectangle([18, 13, 20, 15], fill=(40, 60, 90))
    d.rectangle([9, 11, 14, 12], fill=dark)            # brows
    d.rectangle([17, 11, 22, 12], fill=dark)
    d.rectangle([14, 16, 17, 21], fill=dark)           # nose
    d.rectangle([11, 24, 20, 25], fill=(110, 50, 40))  # mouth
    d.rectangle([8, 26, 23, 29], fill=(150, 104, 76))  # jaw/stubble
    blood = (150, 10, 10)
    if level >= 1:
        d.rectangle([20, 18, 22, 22], fill=blood)
    if level >= 2:
        d.rectangle([8, 16, 10, 24], fill=blood)
        d.rectangle([11, 24, 20, 26], fill=(90, 20, 20))
    if level >= 3:
        d.rectangle([17, 13, 22, 15], fill=(120, 20, 20))
        d.rectangle([12, 18, 14, 28], fill=blood)
    if level >= 4:
        d.rectangle([7, 9, 24, 29], outline=blood)
        d.rectangle([9, 13, 22, 15], fill=(60, 10, 10))
    return img.resize((size, size), Image.NEAREST)


def scorch(size=128, seed=33):
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:size, 0:size] - size / 2
    r = np.sqrt(xx ** 2 + yy ** 2) / (size / 2)
    n = fbm(size, 8, 4, rng=rng)
    a = np.clip(1 - r * (0.8 + 0.5 * n), 0, 1) ** 0.8
    col = np.zeros((size, size, 3)) + 12
    return to_img(col, a * 230)


def bullet_hole(size=32):
    yy, xx = np.mgrid[0:size, 0:size] - (size - 1) / 2
    r = np.sqrt(xx ** 2 + yy ** 2) / (size / 2)
    a = np.clip(1 - r, 0, 1) ** 0.7
    core = r < 0.3
    col = np.zeros((size, size, 3)) + 40
    col[core] = 5
    return to_img(col, a * 220)


def plasma_ball(size=64):
    yy, xx = np.mgrid[0:size, 0:size] - (size - 1) / 2
    r = np.sqrt(xx ** 2 + yy ** 2) / (size / 2)
    a = np.clip(1 - r, 0, 1) ** 1.6
    col = colorize(a, [(0, (40, 120, 255)), (0.6, (140, 210, 255)), (1, (255, 255, 255))])
    return to_img(col, a * 255)


def save(img, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path, optimize=True)
    print("  wrote", path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "base"))
    args = ap.parse_args()
    out = os.path.abspath(args.out)
    T = lambda n: os.path.join(out, "textures", n + ".png")

    textures = {
        "brick_red": lambda: bricks((158, 72, 40), (120, 110, 96), seed=1),
        "brick_dark": lambda: bricks((96, 56, 40), (80, 74, 66), seed=2, dark=0.1),
        "brick_yellow": lambda: bricks((178, 140, 82), (110, 100, 86), seed=3),
        "concrete": lambda: concrete((88, 86, 82), (150, 146, 138), seed=4),
        "concrete_dark": lambda: concrete((44, 44, 44), (96, 94, 90), seed=5, stains=0.5),
        "concrete_panel": lambda: concrete((120, 112, 96), (176, 164, 138), seed=6, panels=True),
        "wall_yellow": lambda: wall_panel((212, 170, 92), (150, 112, 60), seed=7),
        "metal_door": lambda: metal_door(8),
        "metal_plate": lambda: metal_plate(9),
        "rust": lambda: rust_tex(10),
        "tiles_white": lambda: tiles((214, 222, 226), (150, 155, 158), 32, 11),
        "tiles_green": lambda: tiles((70, 110, 98), (40, 60, 55), 32, 12),
        "tiles_tunnel": lambda: wall_tiles_two_tone(13),
        "floor_checker": lambda: checker((60, 104, 100), (210, 214, 200), 32, 14),
        "floor_checker_brown": lambda: checker((120, 90, 70), (190, 160, 130), 32, 15),
        "ceiling_perforated": lambda: perforated((84, 118, 122), 16),
        "asphalt": lambda: asphalt(17),
        "sidewalk": lambda: sidewalk(18),
        "wood_planks": lambda: wood((92, 60, 34), (150, 104, 60), 4, 19),
        "wood_dark": lambda: wood((50, 34, 24), (86, 60, 40), 4, 20),
        "crate": lambda: crate(21),
        "window": lambda: window(22),
        "window_lit": lambda: window(23, lit=True),
        "caution": lambda: stripes((220, 180, 30), (30, 30, 30), 24),
        "roof_tiles": lambda: roof(25),
        "tunnel_stone": lambda: tunnel_stone(26),
        "office_wall": lambda: office_wall(27),
        "cabinet": lambda: cabinet(28),
        "blinds": lambda: blinds(29),
        "dirt": lambda: concrete((70, 52, 36), (120, 94, 66), seed=30, stains=0.2),
    }
    for name, fn in textures.items():
        save(fn(), T(name))
    save(sign_beer(), T("sign_beer"))
    save(light_panel(), T("light_panel"))
    save(grate(), T("grate"))
    save(fence(), T("fence"))
    save(tool_tex((150, 60, 200), "CLIP"), T("clip"))
    save(tool_tex((230, 120, 20), "TRIGGER"), T("trigger"))
    save(tool_tex((80, 80, 80), "NODRAW"), T("nodraw"))
    save(tool_tex((60, 120, 220), "SKY"), T("sky"))
    meta = {
        "light_panel": {"fullbright": True, "surface": "metal"},
        "window_lit": {"fullbright": True, "surface": "glass"},
        "metal_door": {"surface": "metal"},
        "metal_plate": {"surface": "metal"},
        "grate": {"surface": "metal"},
        "wood_planks": {"surface": "wood"},
        "crate": {"surface": "wood"},
    }
    for name, m in meta.items():
        with open(os.path.join(out, "textures", name + ".json"), "w") as f:
            json.dump(m, f, indent=1)

    D = lambda n: os.path.join(out, "decals", n + ".png")
    for i in range(1, 5):
        save(blood_decal(100 + i), D("blood%d" % i))
    save(scorch(), D("scorch"))
    save(bullet_hole(), D("bullet"))

    P = lambda n: os.path.join(out, "sprites", n + ".png")
    save(soft_dot(32), P("particle"))
    save(soft_dot(16, (255, 255, 255), 0.6), P("blood_drop"))
    save(smoke_sprite(34), P("blood_mist"))
    save(smoke_sprite(35), P("smoke"))
    save(soft_dot(16, (255, 255, 255), 1.0, 0.3), P("spark"))
    save(fire_sprite(), P("fire"))
    save(fire_sheet(), P("fire_anim"))
    save(muzzle(), P("muzzle"))
    save(plasma_ball(), P("plasma"))
    save(soft_dot(64, (255, 255, 255), 2.5, 0.08), P("flare"))

    H = lambda n: os.path.join(out, "hud", n + ".png")
    for lv in range(5):
        save(face(lv), H("face_%d" % lv))


if __name__ == "__main__":
    main()
