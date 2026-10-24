"""Synthesized sounds for DESCENT v2 LOOT (assets/sound/rearmed/loot/, 22 kHz 16-bit mono):
gem pickup ticks on a rising pentatonic scale (tick0..tick11), the vacuum whoosh, chest pickup,
the jackpot screen (reel click, reel stop, chest open, jackpot, evolution). ds-loot worker."""
import math, random, struct, wave, pathlib

RATE = 22050
OUT = pathlib.Path(__file__).resolve().parent.parent / 'assets' / 'sound' / 'rearmed' / 'loot'


def write(name, samples, level=0.9):
    OUT.mkdir(parents=True, exist_ok=True)
    peak = max(1e-9, max(abs(s) for s in samples))
    with wave.open(str(OUT / name), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b''.join(struct.pack('<h', int(s / peak * 32767 * level)) for s in samples))


def n(sec):
    return int(RATE * sec)


def env(t, attack, decay):
    return min(1.0, t / attack if attack > 0 else 1.0) * math.exp(-t / decay)


def tone(f, t):
    return math.sin(2 * math.pi * f * t)


def mix(out, at, samples, gain=1.0):
    j0 = n(at)
    for i, s in enumerate(samples):
        if 0 <= j0 + i < len(out):
            out[j0 + i] += gain * s


def bell(f, length, decay, bright=1.0):
    return [(tone(f, i / RATE) + 0.45 * bright * tone(f * 2.76, i / RATE) + 0.2 * bright * tone(f * 5.4, i / RATE))
            * env(i / RATE, 0.002, decay) for i in range(n(length))]


def noise(length, seed, lp=0.5):
    rnd = random.Random(seed)
    y, out = 0.0, []
    for _ in range(n(length)):
        y += lp * (rnd.uniform(-1, 1) - y)
        out.append(y)
    return out


def thunk(f0, length, decay, seed=1):
    rnd = random.Random(seed)
    out, ph, y = [], 0.0, 0.0
    for i in range(n(length)):
        t = i / RATE
        f = f0 * (1.0 + 1.2 * math.exp(-t / 0.02))
        ph += 2 * math.pi * f / RATE
        y += 0.3 * (rnd.uniform(-1, 1) - y)
        out.append((math.sin(ph) + 0.5 * y * math.exp(-t / 0.015)) * env(t, 0.001, decay))
    return out


# ---------------------------------------------------------------- gems
SCALE = [0, 2, 4, 7, 9, 12, 14, 16, 19, 21, 24, 26]   # major pentatonic, two octaves


def tick(step):
    f = 1046.5 * 2 ** (SCALE[step] / 12.0)
    out = [0.0] * n(0.16)
    mix(out, 0.0, bell(f, 0.16, 0.035 + 0.002 * step, 0.7), 1.0)
    mix(out, 0.0, [s * 0.25 for s in noise(0.01, step, 0.9)], 1.0)   # a tiny glassy click
    return out


def vacuum():
    # a rising magnetic hum + a whoosh sweeping up, sparkles at the end
    L = 1.6
    out = [0.0] * n(L)
    rnd = random.Random(7)
    y1 = y2 = 0.0
    ph = 0.0
    for i in range(n(L)):
        t = i / RATE
        k = t / L
        # band-swept noise: one-pole lowpass whose cutoff climbs
        a = 0.02 + 0.5 * k * k
        y1 += a * (rnd.uniform(-1, 1) - y1)
        y2 += a * (y1 - y2)
        whoosh = (y1 - y2) * 2.5 * math.sin(math.pi * min(1.0, k * 1.15)) ** 1.5
        f = 70 + 260 * k * k
        ph += 2 * math.pi * f / RATE
        hum = (math.sin(ph) + 0.4 * math.sin(2 * ph) + 0.2 * math.sin(3.01 * ph)) * min(1.0, t / 0.15) * (1 - k) ** 0.5
        out[i] = 0.9 * whoosh + 0.35 * hum
    for j in range(14):
        f = 1600 * 2 ** (rnd.choice(SCALE) / 12.0)
        mix(out, 0.55 + j * 0.06 + rnd.uniform(0, 0.04), bell(f, 0.25, 0.06), 0.12)
    return out


# ---------------------------------------------------------------- chests
def chest_get():
    out = [0.0] * n(0.9)
    mix(out, 0.0, thunk(70, 0.4, 0.12, 3), 1.0)
    for k, f in enumerate((784, 988, 1175)):
        mix(out, 0.05 + k * 0.06, bell(f, 0.6, 0.18), 0.35)
    return out


def chest_open():
    out = [0.0] * n(0.8)
    # creak: a slow wobbling saw-ish tone
    ph = 0.0
    for i in range(n(0.35)):
        t = i / RATE
        f = 180 + 60 * math.sin(2 * math.pi * 7 * t) + 120 * t
        ph += 2 * math.pi * f / RATE
        s = sum(math.sin(ph * h) / h for h in range(1, 6))
        out[i] += 0.25 * s * env(t, 0.03, 0.2)
    mix(out, 0.3, thunk(90, 0.3, 0.08, 5), 0.8)
    mix(out, 0.33, bell(1319, 0.45, 0.15), 0.35)
    return out


def reel():
    return [s * env(i / RATE, 0.0005, 0.006) for i, s in enumerate(noise(0.03, 11, 0.7))]


def reel_stop():
    out = [0.0] * n(0.45)
    mix(out, 0.0, thunk(110, 0.3, 0.06, 9), 1.0)
    mix(out, 0.01, bell(1568, 0.4, 0.12), 0.3)
    return out


def jackpot():
    out = [0.0] * n(2.2)
    notes = (523, 659, 784, 1047, 784, 1047, 1319, 1568)
    for k, f in enumerate(notes):
        mix(out, k * 0.085, bell(f, 0.8, 0.3), 0.45)
    # a held chord + a coin shower
    for f in (523, 659, 784, 1047):
        mix(out, 0.7, bell(f, 1.4, 0.55, 0.5), 0.25)
    rnd = random.Random(21)
    for j in range(40):
        f = 2000 + rnd.uniform(-300, 900)
        mix(out, 0.65 + rnd.uniform(0, 1.2), bell(f, 0.12, 0.03), 0.08)
    return out


def evolve():
    out = [0.0] * n(2.0)
    # deep power swell
    ph1 = ph2 = 0.0
    for i in range(n(1.6)):
        t = i / RATE
        ph1 += 2 * math.pi * 55 / RATE
        ph2 += 2 * math.pi * 82.5 / RATE
        s = sum(math.sin(ph1 * h) / h for h in range(1, 8)) + 0.7 * sum(math.sin(ph2 * h) / h for h in range(1, 6))
        out[i] += 0.3 * s * min(1.0, t / 0.25) * math.exp(-max(0.0, t - 0.4) / 0.5)
    # a shimmering rise
    for k in range(10):
        f = 660 * 2 ** (SCALE[k] / 12.0)
        mix(out, 0.1 + k * 0.07, bell(f, 0.6, 0.2), 0.22)
    mix(out, 0.0, thunk(50, 0.5, 0.2, 13), 0.8)
    return out


def main():
    for s in range(12):
        write('tick%d.wav' % s, tick(s), 0.55)
    write('vacuum.wav', vacuum(), 0.85)
    write('chest_get.wav', chest_get(), 0.8)
    write('chest_open.wav', chest_open(), 0.8)
    write('reel.wav', reel(), 0.35)
    write('reel_stop.wav', reel_stop(), 0.75)
    write('jackpot.wav', jackpot(), 0.85)
    write('evolve.wav', evolve(), 0.9)
    print('wrote', OUT)


if __name__ == '__main__':
    main()
