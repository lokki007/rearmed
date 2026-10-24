"""Synthesize the v4 movement sounds into assets/sound/rearmed/move/ (22 kHz, 16-bit mono WAV).
Everything is procedural (our own content): slide scrape, ground pound whoosh / impact, blink zip."""
import math, random, struct, wave, pathlib

RATE = 22050
OUT = pathlib.Path(__file__).resolve().parent.parent / 'assets' / 'sound' / 'rearmed' / 'move'


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


def slide():
    # gritty scrape: band-passed noise with random grain clicks, bright at first, duller as it slows
    rnd = random.Random(11)
    f1, f2 = SVF(0.6), SVF(0.9)
    out = []
    n = int(RATE * 0.95)
    for i in range(n):
        t = i / RATE
        x = rnd.uniform(-1, 1)
        grain = rnd.uniform(-1, 1) * 3.0 if rnd.random() < 0.012 else 0.0
        _, body, _ = f1.run(x, 1900 - 900 * t)
        _, grit, _ = f2.run(x + grain, 4200 - 1500 * t)
        a = min(1.0, t / 0.02) * (1.0 - t / 0.95) ** 1.3
        wobble = 0.8 + 0.2 * math.sin(2 * math.pi * 23 * t)
        out.append((0.9 * body + 0.5 * grit) * a * wobble)
    return out


def slam_start():
    # sharp inhale then a falling whoosh
    rnd = random.Random(21)
    f = SVF(0.4)
    out = []
    n = int(RATE * 0.45)
    for i in range(n):
        t = i / RATE
        freq = 900 + 1800 * min(1.0, t / 0.12) if t < 0.12 else 2700 - 2300 * min(1.0, (t - 0.12) / 0.33)
        _, bp, _ = f.run(rnd.uniform(-1, 1), freq)
        a = math.sin(math.pi * min(1.0, t / 0.45)) ** 0.7
        out.append(bp * a)
    return out


def slam_land():
    # heavy body thump + crunchy noise + a few debris ticks
    rnd = random.Random(31)
    lp = SVF(0.7)
    out = []
    n = int(RATE * 0.9)
    ticks = [(rnd.uniform(0.05, 0.5), rnd.uniform(1500, 3500)) for _ in range(9)]
    for i in range(n):
        t = i / RATE
        f = 90 - 45 * min(1.0, t / 0.25)
        thump = math.sin(2 * math.pi * f * t) * env(t, 0.002, 0.16)
        low, _, _ = lp.run(rnd.uniform(-1, 1), 700)
        crunch = low * env(t, 0.001, 0.12) * 2.2
        deb = 0.0
        for t0, fr in ticks:
            dt = t - t0
            if 0 <= dt < 0.03:
                deb += math.sin(2 * math.pi * fr * dt) * env(dt, 0.0005, 0.006) * 0.35
        out.append(1.2 * thump + crunch + deb)
    return out


def slam_big():
    # deep rumble tail for strong pounds (plays on top of slam_land)
    rnd = random.Random(41)
    lp = SVF(0.8)
    out = []
    n = int(RATE * 1.5)
    for i in range(n):
        t = i / RATE
        low, _, _ = lp.run(rnd.uniform(-1, 1), 160 + 120 * math.exp(-t / 0.2))
        boom = math.sin(2 * math.pi * (48 - 14 * min(1.0, t)) * t) * env(t, 0.004, 0.35)
        out.append((1.6 * low + boom) * env(t, 0.01, 0.5))
    return out


def blink():
    # phasey zip: a fast downward sweep with shimmering partials and a soft air tail
    rnd = random.Random(51)
    f = SVF(0.3)
    out = []
    n = int(RATE * 0.4)
    ph1 = ph2 = 0.0
    for i in range(n):
        t = i / RATE
        freq = 2400 * math.exp(-t / 0.06) + 300
        ph1 += 2 * math.pi * freq / RATE
        ph2 += 2 * math.pi * freq * 1.507 / RATE
        tone = (math.sin(ph1) + 0.5 * math.sin(ph2)) * env(t, 0.001, 0.07)
        _, air, _ = f.run(rnd.uniform(-1, 1), 3000 - 2000 * min(1.0, t / 0.4))
        out.append(0.8 * tone + 0.45 * air * env(t, 0.003, 0.12) * (0.6 + 0.4 * math.sin(2 * math.pi * 60 * t)))
    return out


if __name__ == '__main__':
    write('slide.wav', slide(), 22000)
    write('slam_start.wav', slam_start(), 24000)
    write('slam_land.wav', slam_land())
    write('slam_big.wav', slam_big(), 26000)
    write('blink.wav', blink(), 24000)
    print('wrote', sorted(p.name for p in OUT.glob('*.wav')))
