"""REARMED round 4 (en-feel) hit-feel sounds, synthesized (our own) -> assets/sound/rearmed/hitfeel/.

22 kHz 16-bit mono WAV. No recorded or TTS voices: the screams are a formant synthesizer
(glottal pulse train + rasp noise through moving vowel formants), so they are free to ship.

death_<cat>_<voice><var>.wav  cat: gen head gib burn fall boom   voice 0..3   var a/b
panic_<voice><var>.wav        a frightened wail when morale breaks
groan_<voice>.wav             dropping to a crawl
thud1/2, shatter1/2, clang, tear1/2, crack1/2
voices: 0 low male, 1 higher male, 2 female, 3 creature (growl, sub-harmonic)
"""
import math
import pathlib
import random
import struct
import wave

RATE = 22050
OUT = pathlib.Path(__file__).resolve().parent.parent / 'assets' / 'sound' / 'rearmed' / 'hitfeel'


def write(name, samples, gain=1.0):
    OUT.mkdir(parents=True, exist_ok=True)
    peak = max(1e-9, max(abs(s) for s in samples))
    # 4 ms fade at both ends: no clicks
    n = len(samples)
    f = int(RATE * 0.004)
    with wave.open(str(OUT / name), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        data = []
        for i, s in enumerate(samples):
            k = min(1.0, i / f, (n - 1 - i) / f) if f else 1.0
            data.append(struct.pack('<h', int(max(-1.0, min(1.0, s / peak * gain * k)) * 30000)))
        w.writeframes(b''.join(data))


def env(t, attack, decay):
    return (min(1.0, t / attack) if attack > 0 else 1.0) * math.exp(-t / decay)


class SVF:
    def __init__(self, q=0.5):
        self.lp = self.bp = 0.0
        self.q = q

    def run(self, x, f):
        k = 2 * math.sin(math.pi * min(f, RATE * 0.45) / RATE)
        self.lp += k * self.bp
        hp = x - self.lp - self.q * self.bp
        self.bp += k * hp
        return self.lp, self.bp, hp


class Reson:
    """Two-pole resonator with a per-sample centre frequency and bandwidth (a formant)."""
    def __init__(self):
        self.y1 = self.y2 = 0.0

    def run(self, x, f, bw):
        r = math.exp(-math.pi * bw / RATE)
        c = 2 * r * math.cos(2 * math.pi * min(f, RATE * 0.45) / RATE)
        y = (1 - r) * x + c * self.y1 - r * r * self.y2
        self.y2, self.y1 = self.y1, y
        return y


# vowel formants (F1..F4, Hz) for an adult male tract; voices scale them
VOWELS = {
    'aa': (800, 1200, 2500, 3500),
    'ah': (700, 1250, 2550, 3500),
    'ae': (660, 1700, 2400, 3400),
    'eh': (550, 1800, 2500, 3500),
    'oh': (500, 900, 2400, 3300),
    'uh': (550, 1100, 2400, 3300),
    'oo': (320, 800, 2300, 3200),
    'ee': (300, 2300, 3000, 3700),
}
FORMANT_GAIN = (1.0, 0.6, 0.3, 0.15)
FORMANT_BW = (90, 110, 160, 220)

# base f0, formant scale, rasp, sub-harmonic, jitter
VOICES = [
    (105, 0.95, 0.35, 0.0, 0.010),
    (145, 1.02, 0.30, 0.0, 0.012),
    (235, 1.18, 0.22, 0.0, 0.008),
    (78, 0.82, 0.55, 0.45, 0.020),
]


def voice(dur, f0_curve, vowel_curve, amp_curve, v, seed, rasp_mul=1.0, breath=0.0, gurgle=0.0, vib=(5.5, 0.03)):
    """f0_curve(t01) -> multiplier of the voice's f0; vowel_curve(t01) -> (vowelA, vowelB, mix);
    amp_curve(t01) -> 0..1. gurgle adds a bubbling AM + wet noise."""
    base, fscale, rasp, sub, jit = VOICES[v]
    rnd = random.Random(seed)
    res = [Reson() for _ in range(4)]
    noise_f = SVF(0.4)
    bub = SVF(0.3)
    n = int(RATE * dur)
    out = []
    ph = 0.0
    sub_ph = 0.0
    jitter = 0.0
    bubble = 0.0
    bubble_t = 0.0
    for i in range(n):
        t = i / RATE
        u = i / max(1, n - 1)
        if i % 64 == 0:
            jitter = rnd.uniform(-1, 1) * jit
        f0 = base * f0_curve(u) * (1 + jitter + vib[1] * math.sin(2 * math.pi * vib[0] * t + seed))
        ph += f0 / RATE
        if ph >= 1.0:
            ph -= 1.0
        # glottal pulse: a skewed saw (Rosenberg-ish), brighter as it gets louder
        a = amp_curve(u)
        g = (ph / 0.6) if ph < 0.6 else (1.0 - (ph - 0.6) / 0.4)
        src = (g - 0.5) * 2
        if sub > 0:
            sub_ph += f0 * 0.5 / RATE
            src += sub * math.sin(2 * math.pi * sub_ph) * (0.5 + 0.5 * math.sin(2 * math.pi * 31 * t))
        # rasp: noise gated by the glottal phase (a torn, screamed voice)
        nz = rnd.uniform(-1, 1)
        src += rasp * rasp_mul * nz * (1.2 if ph < 0.3 else 0.4) * (0.4 + 0.8 * a)
        src += breath * nz
        va, vb, mix = vowel_curve(u)
        fa, fb = VOWELS[va], VOWELS[vb]
        s = 0.0
        for k in range(4):
            fk = (fa[k] * (1 - mix) + fb[k] * mix) * fscale
            # screams spread F1 up with effort
            if k == 0:
                fk *= 1.0 + 0.25 * a
            s += FORMANT_GAIN[k] * res[k].run(src, fk, FORMANT_BW[k] * (1.0 + 0.6 * a))
        s *= a
        if gurgle > 0:
            # bubbles: random wet pops in a liquid band
            if t >= bubble_t:
                bubble_t = t + rnd.uniform(0.025, 0.09)
                bubble = 1.0
            bubble *= 0.993
            lp, bp, _ = bub.run(nz, 350 + 900 * bubble)
            s = s * (0.55 + 0.45 * bubble) + gurgle * bp * bubble * (0.3 + a)
        lp, bp, hp = noise_f.run(nz, 2500)
        out.append(s + 0.02 * a * hp)
    return out


def lin(points):
    """Piecewise linear curve over 0..1 from [(u, value), ...]."""
    def f(u):
        if u <= points[0][0]:
            return points[0][1]
        for (u0, v0), (u1, v1) in zip(points, points[1:]):
            if u <= u1:
                return v0 + (v1 - v0) * (u - u0) / max(1e-9, u1 - u0)
        return points[-1][1]
    return f


def vow(seq):
    """[(u, 'aa'), ...] -> blends neighbouring vowels."""
    def f(u):
        if u <= seq[0][0]:
            return seq[0][1], seq[0][1], 0.0
        for (u0, a), (u1, b) in zip(seq, seq[1:]):
            if u <= u1:
                return a, b, (u - u0) / max(1e-9, u1 - u0)
        return seq[-1][1], seq[-1][1], 0.0
    return f


def death(cat, v, var):
    seed = sum(ord(c) * 31 ** j for j, c in enumerate(cat)) % 9973 + v * 101 + var * 7   # deterministic
    rnd = random.Random(seed)
    k = 1.0 + (0.08 if var else 0.0) * (1 if rnd.random() < 0.5 else -1)
    if cat == 'gen':
        dur = 0.85 + 0.2 * var
        return voice(dur, lin([(0, 1.1 * k), (0.15, 1.7 * k), (0.6, 1.45 * k), (1, 0.75)]),
                     vow([(0, 'ah'), (0.2, 'aa'), (0.75, 'ah'), (1, 'uh')]),
                     lin([(0, 0), (0.05, 1), (0.6, 0.85), (1, 0)]), v, seed)
    if cat == 'head':
        # a choked cut-off "uh-" then a wet, bubbling gurgle that sinks away
        dur = 1.1 + 0.2 * var
        return voice(dur, lin([(0, 1.3 * k), (0.12, 0.9), (0.5, 0.65), (1, 0.45)]),
                     vow([(0, 'uh'), (0.15, 'oh'), (1, 'oo')]),
                     lin([(0, 0), (0.03, 0.9), (0.12, 0.25), (0.2, 0.55), (0.7, 0.35), (1, 0)]), v, seed,
                     rasp_mul=0.6, gurgle=0.9, vib=(9.0, 0.06))
    if cat == 'gib':
        # a sharp shriek, torn off mid-breath
        dur = 0.34 + 0.06 * var
        s = voice(dur, lin([(0, 1.5 * k), (0.4, 2.1 * k), (1, 2.2 * k)]),
                  vow([(0, 'ae'), (1, 'aa')]),
                  lin([(0, 0), (0.08, 1), (0.9, 1), (1, 0.0)]), v, seed, rasp_mul=1.4)
        return s + [0.0] * int(RATE * 0.05)
    if cat == 'burn':
        # a long wavering agonised shriek, breaking into sobs
        dur = 1.6 + 0.25 * var
        sob = lambda u: (0.55 + 0.45 * abs(math.sin(u * 22))) if u > 0.6 else 1.0
        amp = lin([(0, 0), (0.06, 1), (0.6, 0.9), (1, 0)])
        return voice(dur, lin([(0, 1.6 * k), (0.2, 2.2 * k), (0.6, 2.0 * k), (1, 1.1)]),
                     vow([(0, 'ae'), (0.3, 'ee'), (0.6, 'aa'), (1, 'uh')]),
                     lambda u: amp(u) * sob(u), v, seed, rasp_mul=1.3, vib=(7.5, 0.07))
    if cat == 'fall':
        # a long falling "aaaaah" that recedes
        dur = 1.8 + 0.2 * var
        return voice(dur, lin([(0, 1.6 * k), (0.2, 1.7 * k), (1, 0.85)]),
                     vow([(0, 'aa'), (0.7, 'ah'), (1, 'oh')]),
                     lin([(0, 0), (0.05, 1), (0.5, 0.75), (1, 0)]), v, seed, rasp_mul=0.8, vib=(4.5, 0.025))
    if cat == 'boom':
        # a shocked breathy "huh-" into a short blown-out yell
        dur = 0.62 + 0.1 * var
        return voice(dur, lin([(0, 1.0 * k), (0.15, 1.2 * k), (0.3, 1.8 * k), (1, 1.0)]),
                     vow([(0, 'uh'), (0.25, 'ah'), (1, 'aa')]),
                     lin([(0, 0), (0.05, 0.6), (0.18, 0.2), (0.3, 1), (1, 0)]), v, seed,
                     rasp_mul=1.2, breath=0.25)
    raise ValueError(cat)


def panic(v, var):
    seed = 900 + v * 7 + var
    # two rising wails: "aah! AAAH!"
    dur = 0.95
    return voice(dur, lin([(0, 1.4), (0.2, 1.9), (0.3, 1.3), (0.45, 1.6), (0.75, 2.1), (1, 1.4)]),
                 vow([(0, 'ae'), (0.3, 'ah'), (0.45, 'ae'), (1, 'aa')]),
                 lin([(0, 0), (0.05, 0.8), (0.28, 0.6), (0.33, 0.05), (0.42, 0.9), (0.85, 1), (1, 0)]),
                 v, seed + (40 if var else 0), rasp_mul=1.1, vib=(6.5, 0.05))


def groan(v):
    return voice(0.9, lin([(0, 0.9), (0.5, 0.8), (1, 0.6)]), vow([(0, 'uh'), (1, 'oh')]),
                 lin([(0, 0), (0.15, 0.8), (0.7, 0.6), (1, 0)]), v, 1300 + v, rasp_mul=0.9, breath=0.08)


def thud(seed):
    # a body hitting a wall: low chest thump + cloth/gear rattle
    rnd = random.Random(seed)
    flt = SVF(0.5)
    out = []
    for i in range(int(RATE * 0.45)):
        t = i / RATE
        f = 55 + 70 * math.exp(-t / 0.025)
        s = math.sin(2 * math.pi * f * t) * env(t, 0.002, 0.09)
        lp, bp, hp = flt.run(rnd.uniform(-1, 1), 900 * math.exp(-t / 0.05) + 150)
        s += 0.6 * bp * env(t, 0.001, 0.035)
        if 0.04 < t < 0.2 and rnd.random() < 0.02:
            s += rnd.uniform(-0.4, 0.4)
        out.append(s)
    return out


def shatter(seed):
    # armour plates cracking apart: a hard crack, then ceramic / metal shards ringing as they scatter
    rnd = random.Random(seed)
    flt = SVF(0.3)
    pings = [(rnd.uniform(0.0, 0.45), rnd.uniform(2200, 6500), rnd.uniform(0.02, 0.07), rnd.uniform(0.3, 1.0))
             for _ in range(26)]
    out = []
    for i in range(int(RATE * 0.75)):
        t = i / RATE
        _, bp, hp = flt.run(rnd.uniform(-1, 1), 3500)
        s = (hp * 0.8 + bp) * env(t, 0.0005, 0.03) * 1.4
        s += 0.8 * math.sin(2 * math.pi * (140 + 120 * math.exp(-t / 0.01)) * t) * env(t, 0.001, 0.04)
        for (t0, f, d, a) in pings:
            if t >= t0:
                u = t - t0
                s += 0.25 * a * math.sin(2 * math.pi * f * u) * math.exp(-u / d)
        out.append(s)
    return out


def clang():
    # a helmet knocked off: inharmonic metal ring
    parts = [(410, 0.5, 1.0), (1023, 0.35, 0.6), (1712, 0.25, 0.45), (2630, 0.15, 0.3), (3390, 0.1, 0.2)]
    rnd = random.Random(77)
    out = []
    for i in range(int(RATE * 0.9)):
        t = i / RATE
        s = sum(a * math.sin(2 * math.pi * f * t) * math.exp(-t / d) for f, d, a in parts)
        s += 0.5 * rnd.uniform(-1, 1) * math.exp(-t / 0.004)
        out.append(s)
    return out


def tear(seed):
    # a limb torn off: wet ripping crackle + a squelch + a crunch of bone
    rnd = random.Random(seed)
    flt, sq = SVF(0.35), SVF(0.5)
    out = []
    for i in range(int(RATE * 0.55)):
        t = i / RATE
        grain = rnd.uniform(-1, 1) if rnd.random() < 0.25 + 0.5 * math.exp(-t / 0.1) else 0.0
        _, bp, hp = flt.run(grain, 1800 + 1500 * math.sin(t * 30))
        s = (bp + 0.4 * hp) * env(t, 0.003, 0.18)
        _, sb, _ = sq.run(rnd.uniform(-1, 1), 500 + 400 * math.exp(-t / 0.05))
        s += 0.9 * sb * env(t, 0.01, 0.12)
        s += 0.8 * math.sin(2 * math.pi * (180 * math.exp(-t / 0.02) + 60) * t) * env(t, 0.001, 0.05)
        out.append(s)
    return out


def crack(seed):
    # a hit that cracks armour: a dry snap with a short glassy tail
    rnd = random.Random(seed)
    flt = SVF(0.25)
    out = []
    for i in range(int(RATE * 0.25)):
        t = i / RATE
        _, bp, hp = flt.run(rnd.uniform(-1, 1), 4200)
        s = (hp + 0.5 * bp) * env(t, 0.0003, 0.012)
        s += 0.3 * math.sin(2 * math.pi * (3100 + 400 * seed) * t) * math.exp(-t / 0.05)
        out.append(s)
    return out


def main():
    for cat in ('gen', 'head', 'gib', 'burn', 'fall', 'boom'):
        for v in range(4):
            for var in (0, 1):
                write('death_%s_%d%s.wav' % (cat, v, 'ab'[var]), death(cat, v, var), 0.9)
    for v in range(4):
        for var in (0, 1):
            write('panic_%d%s.wav' % (v, 'ab'[var]), panic(v, var), 0.85)
        write('groan_%d.wav' % v, groan(v), 0.7)
    write('thud1.wav', thud(1), 0.95)
    write('thud2.wav', thud(2), 0.95)
    write('shatter1.wav', shatter(1), 0.9)
    write('shatter2.wav', shatter(2), 0.9)
    write('clang.wav', clang(), 0.8)
    write('tear1.wav', tear(1), 0.9)
    write('tear2.wav', tear(2), 0.9)
    write('crack1.wav', crack(1), 0.6)
    write('crack2.wav', crack(2), 0.6)
    print('hit-feel sounds written to', OUT)


main()
