#!/usr/bin/env python3
"""RBS Engine - synthesizes the placeholder sound effects of the base game (WAV, 22 kHz mono).

Requires numpy. Replace any file in base/sounds/ with real recordings (WAV, OGG, MP3, FLAC).
"""
import os
import wave

import numpy as np

SR = 22050
OUT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "base", "sounds"))
rng = np.random.default_rng(42)


def t_axis(dur):
    return np.arange(int(SR * dur)) / SR


def env(dur, attack=0.005, decay=None, curve=4.0):
    t = t_axis(dur)
    a = np.clip(t / max(attack, 1e-4), 0, 1)
    d = np.exp(-t * curve / (decay or dur))
    return a * d


def noise(dur):
    return rng.uniform(-1, 1, int(SR * dur))


def lowpass(x, cutoff):
    # one-pole low pass, cutoff may be an array
    a = np.exp(-2 * np.pi * np.asarray(cutoff) / SR) * np.ones_like(x)
    y = np.zeros_like(x)
    prev = 0.0
    for i in range(len(x)):
        prev = (1 - a[i]) * x[i] + a[i] * prev
        y[i] = prev
    return y


def highpass(x, cutoff):
    return x - lowpass(x, cutoff)


def sine(freq, dur, phase=0.0):
    f = np.asarray(freq) * np.ones(int(SR * dur))
    return np.sin(2 * np.pi * np.cumsum(f) / SR + phase)


def saw(freq, dur):
    f = np.asarray(freq) * np.ones(int(SR * dur))
    ph = np.cumsum(f) / SR
    return 2 * (ph - np.floor(ph + 0.5))


def resonator(x, freq, q=8.0):
    """two-pole band pass (formants)"""
    w = 2 * np.pi * freq / SR
    r = np.exp(-w / (2 * q))
    b1, b2 = 2 * r * np.cos(w), -r * r
    y = np.zeros_like(x)
    y1 = y2 = 0.0
    for i in range(len(x)):
        v = x[i] * (1 - r) + b1 * y1 + b2 * y2
        y[i] = v
        y2, y1 = y1, v
    return y


def norm(x, peak=0.9):
    m = np.max(np.abs(x)) or 1.0
    return x / m * peak


def write(name, x, peak=0.9):
    x = norm(x, peak)
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name + ".wav")
    data = (np.clip(x, -1, 1) * 32767).astype(np.int16)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(data.tobytes())
    print("  wrote", path)


def pad(x, dur):
    n = int(SR * dur)
    return np.concatenate([x, np.zeros(max(0, n - len(x)))])[:n]


def mix(*parts):
    n = max(len(p) for p in parts)
    out = np.zeros(n)
    for p in parts:
        out[:len(p)] += p
    return out


def gunshot(dur, body_freq, cutoff0, cutoff1, crack=0.6):
    t = t_axis(dur)
    n = noise(dur)
    lp = lowpass(n, np.linspace(cutoff0, cutoff1, len(n)))
    boom = sine(body_freq * np.exp(-t * 6), dur) * env(dur, 0.001, dur * 0.5, 5)
    crackle = highpass(noise(dur), 2500) * env(dur, 0.0005, 0.05, 6) * crack
    return lp * env(dur, 0.001, dur, 5) + boom * 0.8 + crackle


def vocal(dur, f0, formants, vibrato=0.0, rough=0.0, curve=None):
    t = t_axis(dur)
    pitch = f0 * (curve(t) if curve else 1.0) * (1 + vibrato * np.sin(2 * np.pi * 6 * t))
    src = saw(pitch, dur) + rough * noise(dur)
    out = sum(resonator(src, f, 6) * g for f, g in formants)
    return out * env(dur, 0.02, dur, 3)


def main():
    write("shotgun", mix(gunshot(0.9, 70, 3500, 300, 0.8), pad(np.zeros(1), 0.9)))
    write("pistol", gunshot(0.35, 120, 5000, 500, 0.9))
    write("grunt_fire", mix(*[np.concatenate([np.zeros(int(SR * 0.07 * i)), gunshot(0.18, 140, 6000, 900, 0.7)]) for i in range(3)]))
    t = t_axis(0.25)
    zap = sine(1800 * np.exp(-t * 9) + 200, 0.25) * env(0.25, 0.001, 0.25, 4)
    write("plasma", zap + 0.3 * sine(900 * np.exp(-t * 6), 0.25) * env(0.25, 0.001, 0.2, 5) + 0.15 * highpass(noise(0.25), 3000) * env(0.25, 0.001, 0.05, 5))
    write("plasma_hit", highpass(noise(0.3), 1200) * env(0.3, 0.001, 0.3, 6) + sine(300, 0.3) * env(0.3, 0.001, 0.2, 8) * 0.5)
    whoosh = lowpass(noise(0.9), np.linspace(400, 2500, int(SR * 0.9))) * env(0.9, 0.05, 0.9, 3)
    write("rocket_fire", whoosh + gunshot(0.9, 60, 2000, 200, 0.3) * 0.6)
    t = t_axis(1.8)
    rumble = lowpass(noise(1.8), np.linspace(2500, 120, len(t))) * env(1.8, 0.002, 1.8, 4)
    write("explode", rumble + sine(45 * np.exp(-t * 1.5), 1.8) * env(1.8, 0.002, 1.2, 4) * 0.8 + highpass(noise(1.8), 2000) * env(1.8, 0.001, 0.15, 6) * 0.5)
    write("fireball_hit", lowpass(noise(0.5), 1500) * env(0.5, 0.002, 0.5, 5))
    for i in range(1, 4):
        t = t_axis(0.5)
        f = 2400 + i * 500
        write("ric%d" % i, sine(f * np.exp(-t * 2), 0.5) * env(0.5, 0.001, 0.5, 5) * 0.5 + highpass(noise(0.5), 3000) * env(0.5, 0.001, 0.03, 6))
    # monsters / player voices
    write("grunt_sight", vocal(0.6, 110, [(600, 1), (1100, 0.6), (2400, 0.2)], 0.05, 0.3, lambda t: 1 + 0.3 * np.exp(-t * 4)))
    write("grunt_pain", vocal(0.3, 150, [(700, 1), (1200, 0.5)], 0.0, 0.4, lambda t: 1.2 - t))
    write("grunt_death", vocal(0.9, 130, [(650, 1), (1000, 0.5), (2300, 0.2)], 0.08, 0.3, lambda t: 1.1 - 0.6 * t))
    write("fiend_sight", vocal(0.8, 60, [(400, 1), (900, 0.8), (1800, 0.3)], 0.15, 0.9, lambda t: 1 + 0.4 * np.sin(t * 9)))
    write("fiend_pain", vocal(0.3, 80, [(500, 1), (1300, 0.6)], 0.1, 0.8))
    write("fiend_death", vocal(1.1, 70, [(450, 1), (900, 0.7)], 0.2, 0.9, lambda t: 1.2 - 0.7 * t))
    write("fiend_claw", highpass(noise(0.25), 1500) * env(0.25, 0.01, 0.25, 6) + lowpass(noise(0.25), 400) * env(0.25, 0.001, 0.1, 6))
    write("player_pain", vocal(0.35, 140, [(750, 1), (1250, 0.6), (2600, 0.2)], 0.05, 0.3, lambda t: 1.15 - 0.5 * t))
    write("player_death", vocal(1.2, 120, [(700, 1), (1150, 0.6)], 0.1, 0.4, lambda t: 1.2 - 0.6 * t))
    # gore
    sq = lowpass(noise(0.6), np.linspace(1800, 200, int(SR * 0.6))) * env(0.6, 0.002, 0.6, 4)
    write("gib", sq + resonator(noise(0.6), 300, 3) * env(0.6, 0.002, 0.4, 5) * 0.5)
    write("gib_bounce", lowpass(noise(0.15), 600) * env(0.15, 0.001, 0.15, 6))
    # movement
    for i in range(1, 5):
        d = 0.16
        thud = lowpass(noise(d), 300 + i * 60) * env(d, 0.002, d, 7) + highpass(noise(d), 2500) * env(d, 0.001, 0.02, 8) * 0.2
        write("step%d" % i, thud, 0.6)
    write("jump", lowpass(noise(0.2), 500) * env(0.2, 0.002, 0.2, 7), 0.5)
    write("land", lowpass(noise(0.3), 250) * env(0.3, 0.002, 0.3, 6) + sine(70, 0.3) * env(0.3, 0.001, 0.2, 7) * 0.5)
    # items / ui
    t = t_axis(0.35)
    write("pickup", sine(np.where(t < 0.1, 880, np.where(t < 0.2, 1320, 1760)), 0.35) * env(0.35, 0.002, 0.35, 3), 0.6)
    write("weapon_pickup", mix(sine(440, 0.6) * env(0.6, 0.002, 0.6, 3), sine(660, 0.6) * env(0.6, 0.05, 0.6, 3) * 0.7,
                               highpass(noise(0.6), 2000) * env(0.6, 0.001, 0.05, 6) * 0.4), 0.7)
    write("weapon_switch", highpass(noise(0.12), 1500) * env(0.12, 0.001, 0.12, 8), 0.5)
    write("empty", highpass(noise(0.08), 2500) * env(0.08, 0.001, 0.08, 9), 0.5)
    write("use_fail", sine(180, 0.15) * env(0.15, 0.005, 0.15, 5), 0.4)
    write("objective", mix(sine(523, 0.9) * env(0.9, 0.01, 0.9, 3), sine(659, 0.9)[::] * env(0.9, 0.15, 0.9, 3), sine(784, 0.9) * env(0.9, 0.3, 0.9, 3)), 0.6)
    write("secret", mix(*[np.concatenate([np.zeros(int(SR * 0.09 * i)), sine(600 + 150 * i, 0.4) * env(0.4, 0.005, 0.4, 4)]) for i in range(5)]), 0.6)
    write("teleport", sine(np.linspace(200, 1600, int(SR * 0.7)), 0.7) * env(0.7, 0.05, 0.7, 3) + highpass(noise(0.7), 3000) * env(0.7, 0.05, 0.7, 3) * 0.3)
    write("jumppad", sine(np.linspace(150, 600, int(SR * 0.4)), 0.4) * env(0.4, 0.01, 0.4, 4))
    # doors / mechanics
    t = t_axis(1.2)
    slide = lowpass(noise(1.2), 500) * (0.6 + 0.4 * np.sin(2 * np.pi * 14 * t)) * env(1.2, 0.05, 1.2, 2)
    clunk = lowpass(noise(0.3), 200) * env(0.3, 0.001, 0.3, 6)
    write("door_open", mix(clunk, slide * 0.7, np.concatenate([np.zeros(int(SR * 1.0)), clunk])))
    write("button", mix(highpass(noise(0.1), 1000) * env(0.1, 0.001, 0.1, 8), sine(800, 0.1) * env(0.1, 0.001, 0.1, 8) * 0.5), 0.6)
    write("locked", saw(110, 0.35) * env(0.35, 0.01, 0.35, 2) * 0.6, 0.5)
    write("plat_start", mix(clunk, lowpass(noise(0.8), 300) * env(0.8, 0.1, 0.8, 2) * 0.6))
    # loops (start/end smoothly so they loop cleanly)
    t = t_axis(2.0)
    hum = sine(60, 2.0) * 0.6 + sine(120, 2.0) * 0.3 + sine(180, 2.0) * 0.1 + lowpass(noise(2.0), 200) * 0.2
    write("hum_loop", hum, 0.5)
    crackle = lowpass(noise(2.0), 900) * 0.4
    pops = np.zeros(len(t))
    for _ in range(40):
        p = rng.integers(0, len(t) - 400)
        pops[p:p + 400] += highpass(noise(400 / SR), 1500)[:400] * np.exp(-np.arange(400) / 60) * rng.uniform(0.3, 1)
    write("fire_loop", crackle + pops, 0.6)


if __name__ == "__main__":
    main()
