"""Synthesized sounds for DESCENT round 4 affixes (assets/sound/rearmed/affix/, 22 kHz 16-bit
mono): the explosive corpse's fuse and blast, the split tear, the reflect ping, the frost crack,
the vampire's drain, the magnetic wind-up / yank / snap, the regen shimmer, the champion pack's
warning and landing. en-affix; our own generated content (helpers from make_sounds_acts.py)."""
import math, random, pathlib, sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import make_sounds_acts as A

A.OUT = pathlib.Path(__file__).resolve().parent.parent / 'assets' / 'sound' / 'rearmed' / 'affix'
n, env, tone, mix, noise, sweep, boom = A.n, A.env, A.tone, A.mix, A.noise, A.sweep, A.boom
RATE = A.RATE


def bomb_fuse():
    # a swelling hiss with a ticking pulse that speeds up (1.3 s)
    L = 1.3
    out = [0.0] * n(L + 0.05)
    nz = noise(L, 31, 0.5)
    ph = 0.0
    for i in range(len(nz)):
        t = i / RATE
        x = t / L
        rate = 5 + 22 * x * x
        ph += 2 * math.pi * rate / RATE
        tick = max(0.0, math.sin(ph)) ** 12
        nz[i] = nz[i] * (0.15 + 0.85 * x ** 1.5) + 0.6 * tick * tone(900 + 600 * x, t)
    mix(out, 0.0, nz, 0.9)
    mix(out, 0.0, sweep(180, 520, L, (1.0, 0.5, 0.25), 1.6), 0.18)
    return out


def bomb_blast():
    out = [0.0] * n(1.4)
    mix(out, 0.0, boom(42, 1.3, 0.4, 5), 1.0)
    mix(out, 0.0, [v * env(i / RATE, 0.001, 0.08) for i, v in enumerate(noise(0.4, 6, 0.9))], 0.8)
    return out


def split():
    # glass tearing: a fast downward screech, crystalline chirps, a soft violet pop
    out = [0.0] * n(0.95)
    s = sweep(2400, 700, 0.45, (1.0, 0.6, 0.3, 0.2), 0.6)
    for i in range(len(s)):
        t = i / RATE
        s[i] *= env(t, 0.005, 0.25) * (0.6 + 0.4 * math.sin(2 * math.pi * 37 * t))
    mix(out, 0.0, s, 0.4)
    rnd = random.Random(8)
    for k in range(7):
        f = rnd.uniform(2500, 4200)
        mix(out, 0.05 + 0.06 * k, [tone(f, i / RATE) * env(i / RATE, 0.001, 0.04) for i in range(n(0.12))], 0.25)
    mix(out, 0.55, [tone(220, i / RATE) * env(i / RATE, 0.003, 0.09) for i in range(n(0.3))], 0.5)
    return out


def reflect():
    # a bright metallic ping (inharmonic partials)
    out = [0.0] * n(0.45)
    for r, a in ((1.0, 1.0), (2.41, 0.5), (3.93, 0.3), (5.6, 0.15)):
        mix(out, 0.0, [tone(1650 * r, i / RATE) * env(i / RATE, 0.0008, 0.09 / r ** 0.5) for i in range(n(0.4))], 0.35 * a)
    return out


def frost():
    # ice cracking: sharp noisy clicks over a cold glassy ring
    out = [0.0] * n(0.55)
    rnd = random.Random(14)
    for k in range(5):
        at = 0.0 + 0.035 * k + rnd.uniform(0, 0.02)
        mix(out, at, [v * env(i / RATE, 0.0005, 0.012) for i, v in enumerate(noise(0.05, 40 + k, 0.95))], 0.8)
    mix(out, 0.02, [(tone(3100, i / RATE) + 0.5 * tone(4650, i / RATE)) * env(i / RATE, 0.002, 0.18) for i in range(n(0.5))], 0.22)
    return out


def drain():
    # a low wet pulse with a sucking rise
    out = [0.0] * n(0.5)
    s = sweep(90, 260, 0.4, (1.0, 0.7, 0.3), 2.0)
    for i in range(len(s)):
        t = i / RATE
        s[i] *= env(t, 0.03, 0.18)
    mix(out, 0.0, s, 0.7)
    nz = noise(0.4, 51, 0.08)
    mix(out, 0.0, [v * env(i / RATE, 0.02, 0.12) for i, v in enumerate(nz)], 0.5)
    return out


def pull_wind():
    # a magnetic whine winding up (0.9 s), tremolo speeding up
    L = 0.9
    out = [0.0] * n(L + 0.05)
    s = sweep(160, 900, L, (1.0, 0.0, 0.35, 0.0, 0.2), 1.5)
    for i in range(len(s)):
        t = i / RATE
        x = t / L
        s[i] *= min(1.0, t / 0.05) * (0.5 + 0.5 * math.sin(2 * math.pi * (8 + 30 * x) * t)) * (0.4 + 0.6 * x)
    mix(out, 0.0, s, 0.6)
    return out


def pull_yank():
    # a chain snapping taut: a clank and a whoosh
    out = [0.0] * n(0.6)
    for r, a in ((1.0, 1.0), (2.7, 0.6), (4.1, 0.4)):
        mix(out, 0.0, [tone(420 * r, i / RATE) * env(i / RATE, 0.0005, 0.06) for i in range(n(0.25))], 0.35 * a)
    nz = noise(0.45, 61, 0.2)
    for i in range(len(nz)):
        t = i / RATE
        nz[i] *= math.sin(math.pi * min(1.0, t / 0.45))
    mix(out, 0.05, nz, 0.5)
    return out


def pull_snap():
    # the tether breaks: a quick fizzing crackle
    out = [0.0] * n(0.35)
    nz = noise(0.3, 71, 0.9)
    mix(out, 0.0, [v * env(i / RATE, 0.001, 0.07) * (0.5 + 0.5 * math.sin(2 * math.pi * 60 * i / RATE)) for i, v in enumerate(nz)], 0.7)
    mix(out, 0.0, sweep(900, 200, 0.2, (1.0, 0.3), 1.0), 0.15)
    return out


def regen():
    # a soft rising shimmer (three bell tones, mint)
    out = [0.0] * n(0.9)
    for k, f in enumerate((784, 988, 1175)):
        mix(out, 0.08 * k, [(tone(f, i / RATE) + 0.3 * tone(f * 2, i / RATE)) * env(i / RATE, 0.01, 0.3) for i in range(n(0.7))], 0.25)
    return out


def champ_warn():
    # three stacked horn blasts rising, a gold shimmer on top (~1.8 s)
    out = [0.0] * n(2.0)
    for k, (f, at) in enumerate(((110, 0.0), (139, 0.35), (165, 0.7))):
        ph = 0.0
        s = []
        ln = 1.0
        for i in range(n(ln)):
            t = i / RATE
            ph += 2 * math.pi * f / RATE
            v = sum(math.sin(h * ph) / h ** 0.9 for h in range(1, 8))
            s.append(math.tanh(v * 0.7) * min(1.0, t / 0.05) * min(1.0, (ln - t) / 0.25))
        mix(out, at, s, 0.35)
    for k, f in enumerate((1568, 1976, 2349)):
        mix(out, 0.75 + 0.06 * k, [tone(f, i / RATE) * env(i / RATE, 0.002, 0.35) for i in range(n(0.8))], 0.12)
    return out


def champ_land():
    # three heavy landings close together
    out = [0.0] * n(1.4)
    for k in range(3):
        mix(out, 0.07 * k, boom(50 + 6 * k, 1.1, 0.3, 20 + k), 0.6)
    return out


if __name__ == '__main__':
    for name, fn in (('bomb_fuse', bomb_fuse), ('bomb_blast', bomb_blast), ('split', split), ('reflect', reflect),
                     ('frost', frost), ('drain', drain), ('pull_wind', pull_wind), ('pull_yank', pull_yank),
                     ('pull_snap', pull_snap), ('regen', regen), ('champ_warn', champ_warn), ('champ_land', champ_land)):
        A.write(name + '.wav', fn())
    print('affix sounds written to', A.OUT)
