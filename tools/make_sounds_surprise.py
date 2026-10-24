"""Synthesized sounds for DESCENT round 3 surprises (assets/sound/rearmed/surprise/, 22 kHz 16-bit
mono): the ambush warning horn, the landing thump, a fodder portal, the golden meteor's whistle,
the ghost's chord, the "surpassed" fanfare, the wild event sting, a slain sting. ds-surprise;
our own generated content (helpers from make_sounds_acts.py)."""
import math, random, pathlib, sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import make_sounds_acts as A

A.OUT = pathlib.Path(__file__).resolve().parent.parent / 'assets' / 'sound' / 'rearmed' / 'surprise'
n, env, tone, mix, noise, sweep, boom = A.n, A.env, A.tone, A.mix, A.noise, A.sweep, A.boom
RATE = A.RATE


def warn():
    # a war horn: two low brassy blasts, the second higher (~1.6 s)
    out = [0.0] * n(1.8)
    for at, f, ln in ((0.0, 98, 0.6), (0.7, 131, 0.95)):
        ph = 0.0
        s = []
        for i in range(n(ln)):
            t = i / RATE
            ph += 2 * math.pi * f * (1.0 + 0.004 * math.sin(2 * math.pi * 5 * t)) / RATE
            v = sum(math.sin(k * ph) / k ** 0.8 for k in range(1, 9))
            s.append(math.tanh(v * 0.8) * min(1.0, t / 0.06) * min(1.0, (ln - t) / 0.15))
        mix(out, at, s, 0.55)
    return out


def land():
    # a heavy landing: a boom with a crunch of rubble
    out = [0.0] * n(1.3)
    mix(out, 0.0, boom(46, 1.2, 0.35, 3), 1.0)
    mix(out, 0.0, [v * env(i / RATE, 0.002, 0.12) for i, v in enumerate(noise(0.5, 9, 0.6))], 0.6)
    return out


def portal():
    # a short purple whoosh: filtered noise sweeping up, a soft pop
    out = [0.0] * n(0.9)
    nz = noise(0.8, 21, 0.15)
    for i in range(len(nz)):
        t = i / RATE
        nz[i] *= math.sin(math.pi * min(1.0, t / 0.8)) * (0.5 + 0.5 * math.sin(2 * math.pi * (6 + 20 * t) * t))
    mix(out, 0.0, nz, 0.8)
    mix(out, 0.75, [tone(320, i / RATE) * env(i / RATE, 0.002, 0.06) for i in range(n(0.15))], 0.4)
    return out


def gold():
    # a falling whistle that ends in a bright chime (the meteor loop also uses it)
    out = [0.0] * n(2.2)
    s = sweep(1500, 420, 1.6, (1.0, 0.3), 1.2)
    for i in range(len(s)):
        t = i / RATE
        s[i] *= min(1.0, t / 0.1) * 0.6
    mix(out, 0.0, s, 0.5)
    for k, f in enumerate((1318, 1760, 2093)):
        mix(out, 1.55 + 0.07 * k, [(tone(f, i / RATE) + 0.3 * tone(f * 2.01, i / RATE)) * env(i / RATE, 0.002, 0.35) for i in range(n(0.6))], 0.35)
    return out


def ghost():
    # a cold, breathy minor chord swelling in and out
    out = [0.0] * n(2.4)
    for f in (220.0, 261.6, 329.6, 440.0 * 1.003):
        s = []
        for i in range(n(2.3)):
            t = i / RATE
            s.append(tone(f * (1.0 + 0.003 * math.sin(2 * math.pi * 0.7 * t)), t) * math.sin(math.pi * t / 2.3) ** 1.5)
        mix(out, 0.0, s, 0.22)
    mix(out, 0.0, [v * math.sin(math.pi * min(1.0, i / RATE / 2.3)) for i, v in enumerate(noise(2.3, 33, 0.05))], 0.5)
    return out


def surpassed():
    # a rising major fanfare: C E G C, with a shimmer on top
    out = [0.0] * n(2.0)
    for k, f in enumerate((523.3, 659.3, 784.0, 1046.5)):
        s = [(math.tanh(2 * tone(f, i / RATE)) + 0.3 * tone(f * 2, i / RATE)) * env(i / RATE, 0.005, 0.5 if k == 3 else 0.22) for i in range(n(1.2))]
        mix(out, 0.11 * k, s, 0.35)
    mix(out, 0.33, [tone(2093 + 30 * math.sin(i / RATE * 40), i / RATE) * env(i / RATE, 0.01, 0.6) for i in range(n(1.4))], 0.12)
    return out


def event():
    # the wild event sting: a gong hit with a rising tail
    out = [0.0] * n(1.8)
    rnd = random.Random(4)
    parts = [(1.0, 1.0), (2.76, 0.5), (5.4, 0.25), (8.9, 0.12)]
    s = []
    for i in range(n(1.7)):
        t = i / RATE
        s.append(sum(a * tone(170 * r, t) * math.exp(-t / (0.9 / r ** 0.5)) for r, a in parts))
    mix(out, 0.0, s, 0.5)
    mix(out, 0.3, sweep(300, 900, 1.2, (1.0, 0.3), 2.0), 0.08)
    return out


def slain():
    # a short heavy "got it": low hit + bright ping
    out = [0.0] * n(1.0)
    mix(out, 0.0, boom(60, 0.6, 0.18, 7), 0.7)
    mix(out, 0.05, [(tone(1568, i / RATE) + 0.4 * tone(2349, i / RATE)) * env(i / RATE, 0.002, 0.25) for i in range(n(0.8))], 0.4)
    return out


if __name__ == '__main__':
    for name, fn in (('warn', warn), ('land', land), ('portal', portal), ('gold', gold), ('ghost', ghost),
                     ('surpassed', surpassed), ('event', event), ('slain', slain)):
        A.write(name + '.wav', fn())
    print('surprise sounds written to', A.OUT)
