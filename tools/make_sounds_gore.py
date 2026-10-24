"""REARMED v4 gore sounds (synthesized, our own) -> assets/sound/rearmed/gore/ (22 kHz 16-bit mono WAV)."""
import math, random, struct, wave, pathlib

RATE = 22050
OUT = pathlib.Path(__file__).resolve().parent.parent / 'assets' / 'sound' / 'rearmed' / 'gore'


def write(name, samples, gain=1.0):
    OUT.mkdir(parents=True, exist_ok=True)
    peak = max(1e-9, max(abs(s) for s in samples))
    with wave.open(str(OUT / name), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b''.join(struct.pack('<h', int(s / peak * 30000 * gain)) for s in samples))


def env(t, attack, decay):
    return (min(1.0, t / attack) if attack > 0 else 1.0) * math.exp(-t / decay)


class SVF:
    """State-variable filter, cutoff can change per sample."""
    def __init__(self, q=0.5):
        self.lp = self.bp = 0.0
        self.q = q

    def run(self, x, f):
        k = 2 * math.sin(math.pi * min(f, RATE * 0.45) / RATE)
        self.lp += k * self.bp
        hp = x - self.lp - self.q * self.bp
        self.bp += k * hp
        return self.lp, self.bp, hp


def thump(t, f0, f1, decay):
    f = f1 + (f0 - f1) * math.exp(-t / 0.03)
    return math.sin(2 * math.pi * f * t) * env(t, 0.002, decay)


def splat(seed, length=0.35, body=1.0):
    # wet slap: a filtered noise burst with a falling cutoff, a low thud and a few sticky clicks after it
    rnd = random.Random(seed)
    flt = SVF(0.35)
    out = []
    clicks = [rnd.uniform(0.03, length * 0.8) for _ in range(rnd.randint(3, 6))]
    ph = 0.0
    for i in range(int(RATE * length)):
        t = i / RATE
        x = rnd.uniform(-1, 1)
        lp, bp, hp = flt.run(x, 300 + 2600 * math.exp(-t / 0.05))
        s = 0.9 * bp * env(t, 0.002, 0.07)
        ph += 2 * math.pi * (70 + 60 * math.exp(-t / 0.02)) / RATE
        s += 0.7 * body * math.sin(ph) * env(t, 0.003, 0.06)
        for c in clicks:
            if 0 <= t - c < 0.012:
                s += 0.35 * rnd.uniform(-1, 1) * math.exp(-(t - c) / 0.003)
        out.append(s)
    return out


def decap():
    # crunch (bone) + wet pop + a short gush
    rnd = random.Random(31)
    flt, gush = SVF(0.3), SVF(0.6)
    out = []
    for i in range(int(RATE * 0.9)):
        t = i / RATE
        crunch = 0.0
        if t < 0.12:
            grain = rnd.uniform(-1, 1) if rnd.random() < 0.35 else 0.0
            _, bp, hp = flt.run(grain, 2500)
            crunch = (hp * 0.6 + bp) * env(t, 0.001, 0.05)
        pop = thump(t, 220, 60, 0.07)
        _, gb, _ = gush.run(rnd.uniform(-1, 1), 700 + 300 * math.sin(t * 40))
        g = gb * (min(1, t / 0.08)) * math.exp(-max(0, t - 0.1) / 0.3) * (0.6 + 0.4 * math.sin(t * 55))
        out.append(1.2 * crunch + 0.9 * pop + 0.8 * g)
    return out


def gibburst():
    # a big wet explosion: several overlapping splats and a low boom
    parts = [splat(50 + k, 0.5, 0.6) for k in range(4)]
    out = [0.0] * int(RATE * 0.9)
    for k, p in enumerate(parts):
        off = int(RATE * [0.0, 0.035, 0.07, 0.12][k])
        for i, s in enumerate(p):
            if off + i < len(out):
                out[off + i] += s * (1.0 - 0.15 * k)
    for i in range(len(out)):
        t = i / RATE
        out[i] += 1.4 * thump(t, 110, 38, 0.18)
    return out


def heartbeat():
    # lub-dub: two low thumps, the second softer
    out = []
    for i in range(int(RATE * 0.62)):
        t = i / RATE
        s = thump(t, 95, 48, 0.07) * 1.0
        if t > 0.22:
            s += thump(t - 0.22, 85, 45, 0.08) * 0.7
        out.append(s)
    return out


def sizzle():
    # charred corpse: crackle over a hiss
    rnd = random.Random(77)
    flt = SVF(0.7)
    out = []
    for i in range(int(RATE * 1.2)):
        t = i / RATE
        _, _, hp = flt.run(rnd.uniform(-1, 1), 4000)
        hiss = hp * 0.25 * env(t, 0.05, 0.5)
        crack = rnd.uniform(-1, 1) * 0.9 if rnd.random() < 0.004 * math.exp(-t / 0.6) else 0.0
        out.append(hiss + crack)
    # smear the crackles into short pops
    sm, a = [], 0.0
    for s in out:
        a = a * 0.8 + s
        sm.append(a * 0.5 + s * 0.5)
    return sm


def impact():
    # kill impact frame: a heavy punch with a sub drop and a short noise crack
    rnd = random.Random(99)
    out = []
    for i in range(int(RATE * 0.5)):
        t = i / RATE
        s = 1.3 * thump(t, 160, 36, 0.12)
        s += 0.5 * rnd.uniform(-1, 1) * env(t, 0.0005, 0.01)
        out.append(s)
    return out


write('splat1.wav', splat(1), 0.8)
write('splat2.wav', splat(2, 0.3, 0.7), 0.8)
write('splat3.wav', splat(3, 0.4, 1.2), 0.8)
write('decap.wav', decap())
write('gibburst.wav', gibburst())
write('heartbeat.wav', heartbeat())
write('sizzle.wav', sizzle(), 0.7)
write('impact.wav', impact())
print('gore sounds written to', OUT)
