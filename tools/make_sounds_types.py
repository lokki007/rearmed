"""Synthesized sounds for the round 4 enemy archetypes (assets/sound/rearmed/types/, 22 kHz 16-bit
mono): the sniper's charge whine (the glint), the kamikaze beep / fuse / blast, the medic's heal
hum (loop), the blinker's zap, the summoner's ritual drone, the shield's block clang and power-up,
the juggernaut's heavy step. en-types; our own generated content (helpers from make_sounds_acts.py).
Run: python tools/make_sounds_types.py"""
import math, random, pathlib, sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import make_sounds_acts as A

A.OUT = pathlib.Path(__file__).resolve().parent.parent / 'assets' / 'sound' / 'rearmed' / 'types'
n, env, tone, mix, noise, sweep, boom = A.n, A.env, A.tone, A.mix, A.noise, A.sweep, A.boom
RATE = A.RATE


def snipe_charge():
    # a thin rising whine over the 0.7 s wind-up, a tiny click when it locks
    out = [0.0] * n(0.8)
    s = sweep(900, 3200, 0.72, (1.0, 0.25, 0.1), 1.8)
    for i in range(len(s)):
        t = i / RATE
        s[i] *= min(1.0, t / 0.05) * (0.35 + 0.65 * t / 0.72) * (0.85 + 0.15 * math.sin(2 * math.pi * 30 * t))
    mix(out, 0.0, s, 0.45)
    mix(out, 0.68, [tone(4200, i / RATE) * env(i / RATE, 0.001, 0.012) for i in range(n(0.06))], 0.5)
    return out


def beep():
    # one short electronic beep
    return [(tone(1850, i / RATE) + 0.3 * tone(3700, i / RATE)) * min(1.0, i / 40.0) * env(i / RATE, 0.0, 0.035)
            for i in range(n(0.07))]


def fuse():
    # the fuse: beeps speeding up into a whine (1 s)
    out = [0.0] * n(1.05)
    t, gap = 0.0, 0.16
    while t < 0.9:
        mix(out, t, beep(), 0.7)
        t += gap
        gap = max(0.045, gap * 0.78)
    s = sweep(1200, 2600, 0.5, (1.0, 0.2), 1.0)
    for i in range(len(s)):
        s[i] *= min(1.0, i / RATE / 0.3)
    mix(out, 0.5, s, 0.25)
    return out


def blast():
    # the kamikaze blast: a deep boom, crackle and debris
    out = [0.0] * n(1.8)
    mix(out, 0.0, boom(38, 1.6, 0.45, 11), 1.0)
    mix(out, 0.0, boom(80, 0.6, 0.12, 12), 0.6)
    crack = noise(1.2, 13, 0.7)
    mix(out, 0.0, [v * env(i / RATE, 0.001, 0.2) for i, v in enumerate(crack)], 0.6)
    rnd = random.Random(14)
    for _ in range(9):
        at = 0.25 + rnd.random() * 1.1
        mix(out, at, [v * env(i / RATE, 0.001, 0.02) for i, v in enumerate(noise(0.08, rnd.randint(0, 999), 0.9))], 0.25)
    return out


def heal_loop():
    # a soft shimmering hum, loops seamlessly (1 s, whole cycles)
    out = []
    for i in range(n(1.0)):
        t = i / RATE
        v = 0.5 * tone(220, t) + 0.3 * tone(330, t) + 0.15 * tone(660, t) * (0.5 + 0.5 * tone(4, t))
        v += 0.08 * tone(1320, t) * (0.5 + 0.5 * tone(6, t))
        out.append(v * (0.85 + 0.15 * tone(2, t)))
    return out


def blink():
    # a zap: a quick downward chirp with a hiss, then a pop
    out = [0.0] * n(0.45)
    s = sweep(2600, 300, 0.22, (1.0, 0.5, 0.3), 0.6)
    for i in range(len(s)):
        s[i] *= env(i / RATE, 0.002, 0.12)
    mix(out, 0.0, s, 0.5)
    nz = noise(0.3, 21, 0.9)
    mix(out, 0.0, [v * env(i / RATE, 0.001, 0.07) for i, v in enumerate(nz)], 0.4)
    mix(out, 0.2, [tone(180, i / RATE) * env(i / RATE, 0.002, 0.05) for i in range(n(0.2))], 0.5)
    return out


def ritual():
    # the raise: a low chanting drone that swells for 1.5 s, a crack of bone at the end
    out = [0.0] * n(1.9)
    for i in range(n(1.6)):
        t = i / RATE
        sw = min(1.0, t / 1.2)
        v = sum(a * math.sin(2 * math.pi * f * t + 0.3 * math.sin(2 * math.pi * 3 * t)) for f, a in ((73, 1.0), (110, 0.6), (146, 0.35), (220, 0.15)))
        v *= sw * (0.75 + 0.25 * math.sin(2 * math.pi * 1.5 * t))
        out[i] += 0.5 * math.tanh(v)
    mix(out, 1.5, boom(120, 0.35, 0.06, 31), 0.6)
    mix(out, 1.5, [v * env(i / RATE, 0.001, 0.05) for i, v in enumerate(noise(0.3, 32, 0.95))], 0.4)
    return out


def shield_block():
    # a bright metallic clang with an electric fizz
    out = [0.0] * n(0.5)
    parts = [(1.0, 1.0), (2.41, 0.6), (3.9, 0.35), (5.3, 0.2)]
    mix(out, 0.0, [sum(a * tone(620 * r, i / RATE) * math.exp(-i / RATE / (0.18 / r ** 0.5)) for r, a in parts)
                   for i in range(n(0.45))], 0.5)
    mix(out, 0.0, [v * env(i / RATE, 0.001, 0.06) for i, v in enumerate(noise(0.2, 41, 0.95))], 0.35)
    return out


def shield_up():
    # the shield powers up: a short rising hum
    out = [0.0] * n(0.35)
    s = sweep(180, 520, 0.3, (1.0, 0.5, 0.25), 0.8)
    for i in range(len(s)):
        s[i] *= min(1.0, i / RATE / 0.03) * env(i / RATE, 0.0, 0.2)
    mix(out, 0.0, s, 0.5)
    return out


def jugg_step():
    # a heavy metal-shod footfall: a low thud and a clank
    out = [0.0] * n(0.5)
    mix(out, 0.0, boom(42, 0.45, 0.11, 51), 1.0)
    mix(out, 0.0, [v * env(i / RATE, 0.001, 0.03) for i, v in enumerate(noise(0.1, 52, 0.8))], 0.35)
    mix(out, 0.01, [tone(310, i / RATE) * env(i / RATE, 0.001, 0.04) for i in range(n(0.12))], 0.2)
    return out


if __name__ == '__main__':
    for name, fn, lvl in (('snipe_charge', snipe_charge, 0.8), ('beep', beep, 0.7), ('fuse', fuse, 0.85),
                          ('boom', blast, 0.95), ('heal_loop', heal_loop, 0.5), ('blink', blink, 0.8),
                          ('ritual', ritual, 0.8), ('shield_block', shield_block, 0.75), ('shield_up', shield_up, 0.5),
                          ('jugg_step', jugg_step, 0.9)):
        A.write(name + '.wav', fn(), lvl)
    print('type sounds written to', A.OUT)
