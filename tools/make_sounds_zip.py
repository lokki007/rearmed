"""Synthesize the zip line sounds into assets/sound/rearmed/zip/ (22 kHz, 16-bit mono WAV).
Everything is procedural (our own content): cable shot, anchor clank, dry fire, release snap, ride loop."""
import math, random, struct, wave, pathlib

RATE = 22050
OUT = pathlib.Path(__file__).resolve().parent.parent / 'assets' / 'sound' / 'rearmed' / 'zip'


def write(name, samples, level=30000):
    OUT.mkdir(parents=True, exist_ok=True)
    peak = max(1e-9, max(abs(s) for s in samples))
    with wave.open(str(OUT / name), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b''.join(struct.pack('<h', int(s / peak * level)) for s in samples))


def env(t, attack, decay):
    return min(1.0, t / attack) * math.exp(-t / decay) if attack > 0 else math.exp(-t / decay)


class SVF:
    """State-variable filter: returns (low, band, high) per sample."""
    def __init__(self, q=0.5):
        self.lp = self.bp = 0.0
        self.q = q

    def run(self, x, f):
        k = 2 * math.sin(math.pi * min(f, RATE * 0.45) / RATE)
        self.lp += k * self.bp
        hp = x - self.lp - self.q * self.bp
        self.bp += k * hp
        return self.lp, self.bp, hp


def zip_fire():
    # pneumatic launch: a sharp low thump, then the cable paying out (a rising, rattling hiss)
    rnd = random.Random(101)
    f = SVF(0.35)
    out = []
    n = int(RATE * 0.42)
    for i in range(n):
        t = i / RATE
        thump = math.sin(2 * math.pi * (140 - 80 * min(1.0, t / 0.06)) * t) * env(t, 0.001, 0.035)
        _, hiss, _ = f.run(rnd.uniform(-1, 1), 1800 + 4200 * min(1.0, t / 0.3))
        rattle = 0.6 + 0.4 * math.sin(2 * math.pi * 95 * t)
        out.append(1.3 * thump + 0.55 * hiss * env(t, 0.01, 0.12) * rattle)
    return out


def zip_attach():
    # the hook bites: a bright metallic clank with ringing partials
    rnd = random.Random(111)
    hp = SVF(0.5)
    out = []
    n = int(RATE * 0.55)
    partials = [(1180, 1.0, 0.09), (2730, 0.6, 0.06), (4110, 0.35, 0.04), (620, 0.5, 0.14)]
    for i in range(n):
        t = i / RATE
        ring = sum(a * math.sin(2 * math.pi * fr * t) * env(t, 0.0005, d) for fr, a, d in partials)
        _, _, click = hp.run(rnd.uniform(-1, 1), 3000)
        out.append(ring + 1.2 * click * env(t, 0.0003, 0.006))
    return out


def zip_miss():
    # dry fire: a small click and a short fizzle of air
    rnd = random.Random(121)
    f = SVF(0.6)
    out = []
    n = int(RATE * 0.22)
    for i in range(n):
        t = i / RATE
        click = math.sin(2 * math.pi * 2400 * t) * env(t, 0.0003, 0.004)
        _, air, _ = f.run(rnd.uniform(-1, 1), 2600 - 1800 * min(1.0, t / 0.2))
        out.append(click + 0.35 * air * env(t, 0.004, 0.05))
    return out


def zip_release():
    # letting go: a quick whip snap and a falling twang of the slack cable
    rnd = random.Random(131)
    hp = SVF(0.4)
    out = []
    n = int(RATE * 0.38)
    ph = 0.0
    for i in range(n):
        t = i / RATE
        _, _, snap = hp.run(rnd.uniform(-1, 1), 2500)
        freq = 520 * math.exp(-t / 0.12) + 140
        ph += 2 * math.pi * freq / RATE
        twang = (math.sin(ph) + 0.3 * math.sin(2.01 * ph)) * env(t, 0.002, 0.1)
        out.append(1.4 * snap * env(t, 0.0005, 0.012) + 0.7 * twang)
    return out


def zip_loop():
    # riding: rushing air plus the cable's pulley whine; exactly periodic so it loops cleanly
    n = int(RATE * 1.0)
    rnd = random.Random(141)
    noise = [rnd.uniform(-1, 1) for _ in range(n)]
    f1, f2 = SVF(0.5), SVF(0.25)
    out = []
    # two passes so the filters are settled when the kept pass starts (seamless wrap)
    for p in range(2):
        out = []
        for i in range(n):
            t = i / RATE
            wob = 0.5 + 0.5 * math.sin(2 * math.pi * 3 * t)          # 3 cycles per loop
            _, air, _ = f1.run(noise[i], 700 + 500 * wob)
            _, hi, _ = f2.run(noise[i], 3200 + 600 * wob)
            whine = math.sin(2 * math.pi * 880 * t) + 0.4 * math.sin(2 * math.pi * 1320 * t)
            out.append(0.9 * air + 0.25 * hi + 0.12 * whine * (0.7 + 0.3 * wob))
    return out


if __name__ == '__main__':
    write('zip_fire.wav', zip_fire(), 26000)
    write('zip_attach.wav', zip_attach(), 24000)
    write('zip_miss.wav', zip_miss(), 18000)
    write('zip_release.wav', zip_release(), 22000)
    write('zip_loop.wav', zip_loop(), 16000)
    print('wrote', sorted(p.name for p in OUT.glob('*.wav')))
