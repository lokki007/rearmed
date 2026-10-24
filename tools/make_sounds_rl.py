"""Synthesized sounds for DESCENT (the roguelike mode): level intro, floor cleared / failed,
run over, card flip, card taken, ability unlocked, skull count
(assets/sound/rearmed/rl/, 22 kHz 16-bit mono). rlcore worker."""
import math, random, struct, wave, pathlib

RATE = 22050
OUT = pathlib.Path(__file__).resolve().parent.parent / 'assets' / 'sound' / 'rearmed' / 'rl'


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
        if j0 + i < len(out):
            out[j0 + i] += gain * s


def bell(f, length, decay):
    return [(tone(f, i / RATE) + 0.5 * tone(f * 2.01, i / RATE) + 0.25 * tone(f * 3.98, i / RATE))
            * env(i / RATE, 0.003, decay) for i in range(n(length))]


def boom(f0, length, decay, seed=1):
    rnd = random.Random(seed)
    out, y, ph = [], 0.0, 0.0
    for i in range(n(length)):
        t = i / RATE
        f = f0 * (1.0 + 1.5 * math.exp(-t / 0.05))
        ph += 2 * math.pi * f / RATE
        y += 0.08 * (rnd.uniform(-1, 1) - y)
        out.append((math.sin(ph) + 0.6 * y) * env(t, 0.004, decay))
    return out


def intro():
    # low drum hit, then a dark rising two-note horn: "a new floor"
    out = [0.0] * n(1.8)
    mix(out, 0.0, boom(48, 1.2, 0.35), 1.0)
    for k, (f, at) in enumerate(((110, 0.18), (165, 0.55))):
        seg = []
        for i in range(n(1.1)):
            t = i / RATE
            v = 1 + 0.01 * math.sin(2 * math.pi * 5 * t)
            s = sum(tone(f * h * v, t) / h for h in range(1, 7))
            seg.append(s * min(1.0, t / 0.08) * math.exp(-t / 0.6))
        mix(out, at, seg, 0.35)
    return out


def cleared():
    # bright rising arpeggio with a cymbal-ish shimmer
    out = [0.0] * n(1.6)
    for k, f in enumerate((523, 659, 784, 1047)):
        mix(out, k * 0.09, bell(f, 1.0, 0.35), 0.6)
    rnd = random.Random(3)
    sh = [rnd.uniform(-1, 1) * env(i / RATE, 0.01, 0.4) for i in range(n(1.2))]
    y, f = 0.0, []
    for s in sh:
        y += 0.5 * (s - y)
        f.append(s - y)
    mix(out, 0.27, f, 0.25)
    return out


def failed():
    # falling minor phrase over a soft thud
    out = [0.0] * n(1.7)
    mix(out, 0.0, boom(60, 0.8, 0.25, 4), 0.7)
    for k, f in enumerate((392, 349, 311, 233)):
        mix(out, 0.05 + k * 0.17, bell(f, 1.0, 0.3), 0.5)
    return out


def runover():
    # a deep gong and a long descending drone
    out = [0.0] * n(3.0)
    g = [(tone(73, i / RATE) + 0.7 * tone(73 * 2.76, i / RATE) + 0.4 * tone(73 * 5.4, i / RATE))
         * env(i / RATE, 0.005, 1.1) for i in range(n(3.0))]
    mix(out, 0.0, g, 0.8)
    ph = 0.0
    dr = []
    for i in range(n(2.6)):
        t = i / RATE
        f = 110 * math.exp(-t * 0.35)
        ph += 2 * math.pi * f / RATE
        dr.append((math.sin(ph) + 0.4 * math.sin(2 * ph) + 0.2 * math.sin(3 * ph)) * min(1.0, t / 0.3) * math.exp(-t / 1.4))
    mix(out, 0.25, dr, 0.4)
    return out


def card():
    # a quick paper flick: filtered noise sweep
    rnd = random.Random(7)
    out, y = [], 0.0
    for i in range(n(0.22)):
        t = i / RATE
        k = 0.05 + 0.6 * (t / 0.22)
        y += k * (rnd.uniform(-1, 1) - y)
        out.append(y * env(t, 0.01, 0.06))
    return out


def take():
    # punchy confirm: thump + two bright notes
    out = [0.0] * n(0.7)
    mix(out, 0.0, boom(90, 0.4, 0.1, 9), 0.7)
    mix(out, 0.02, bell(880, 0.6, 0.18), 0.5)
    mix(out, 0.09, bell(1320, 0.6, 0.22), 0.5)
    return out


def unlock():
    # rising shimmer sweep with a chord on top
    out = [0.0] * n(1.5)
    ph = 0.0
    sw = []
    for i in range(n(0.7)):
        t = i / RATE
        f = 300 * math.exp(t * 2.6)
        ph += 2 * math.pi * f / RATE
        sw.append((math.sin(ph) + 0.3 * math.sin(3 * ph)) * min(1.0, t / 0.05) * (1 - t / 0.7))
    mix(out, 0.0, sw, 0.35)
    for f in (784, 988, 1175, 1568):
        mix(out, 0.62, bell(f, 0.85, 0.3), 0.32)
    return out


def skull():
    # a hollow wooden tick for the skull counter
    return [(tone(620, i / RATE) + 0.6 * tone(1490, i / RATE)) * env(i / RATE, 0.0008, 0.025) for i in range(n(0.09))]


if __name__ == '__main__':
    write('rl_intro.wav', intro())
    write('rl_cleared.wav', cleared())
    write('rl_failed.wav', failed())
    write('rl_runover.wav', runover())
    write('rl_card.wav', card(), 0.7)
    write('rl_take.wav', take())
    write('rl_unlock.wav', unlock())
    write('rl_skull.wav', skull(), 0.6)
    print('wrote', OUT)
