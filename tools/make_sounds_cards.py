"""Synthesized sounds for DESCENT round 3 CARDS (assets/sound/rearmed/cards/, 22 kHz 16-bit mono):
the card reveal (flip, rare shimmer, epic sting, legendary charge + roar/choir hit), the synergy ding
(chime + snap), the evolution-ready chime, the floor-end tally (tick, stamp, record stamp) and one
sound per legendary card effect. All generated here (no samples). ds-cards worker."""
import math, random, struct, wave, pathlib

RATE = 22050
OUT = pathlib.Path(__file__).resolve().parent.parent / 'assets' / 'sound' / 'rearmed' / 'cards'


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


def mix(out, at, samples, gain=1.0):
    j0 = n(at)
    for i, s in enumerate(samples):
        if 0 <= j0 + i < len(out):
            out[j0 + i] += gain * s


def bell(f, length, decay, bright=1.0):
    out = []
    for i in range(n(length)):
        t = i / RATE
        out.append((math.sin(2 * math.pi * f * t) + 0.45 * bright * math.sin(2 * math.pi * f * 2.76 * t)
                    + 0.2 * bright * math.sin(2 * math.pi * f * 5.4 * t)) * env(t, 0.002, decay))
    return out


def noise(length, seed, lp=0.5):
    rnd = random.Random(seed)
    y, out = 0.0, []
    for _ in range(n(length)):
        y += lp * (rnd.uniform(-1, 1) - y)
        out.append(y)
    return out


def thunk(f0, length, decay, seed=1, click=0.5):
    rnd = random.Random(seed)
    out, ph, y = [], 0.0, 0.0
    for i in range(n(length)):
        t = i / RATE
        f = f0 * (1.0 + 1.2 * math.exp(-t / 0.02))
        ph += 2 * math.pi * f / RATE
        y += 0.3 * (rnd.uniform(-1, 1) - y)
        out.append((math.sin(ph) + click * y * math.exp(-t / 0.015)) * env(t, 0.001, decay))
    return out


def sweep(f0, f1, length, decay, shape='sin', seed=3):
    out, ph = [], 0.0
    for i in range(n(length)):
        t = i / RATE
        k = t / length
        f = f0 * (f1 / f0) ** k
        ph += 2 * math.pi * f / RATE
        v = math.sin(ph) if shape == 'sin' else (2 * ((ph / (2 * math.pi)) % 1.0) - 1)
        out.append(v * env(t, 0.005, decay))
    return out


def saw(f, t):
    return 2 * ((f * t) % 1.0) - 1


# ---------------------------------------------------------------- reveal
def flip():
    out = [0.0] * n(0.14)
    w = noise(0.12, 7, 0.35)
    for i in range(len(w)):
        t = i / RATE
        w[i] *= math.sin(math.pi * min(1.0, t / 0.11)) * 0.8
    mix(out, 0, w)
    mix(out, 0.085, thunk(900, 0.05, 0.012, 2, 0.8), 0.5)
    return out


def rare():
    out = [0.0] * n(0.9)
    for k, f in enumerate([1568, 2093, 2637, 3136]):
        mix(out, 0.035 * k, bell(f, 0.8, 0.25, 0.6), 0.5)
    sh = noise(0.6, 11, 0.9)
    for i in range(len(sh)):
        sh[i] *= 0.12 * env(i / RATE, 0.05, 0.18) * math.sin(2 * math.pi * 23 * i / RATE)
    mix(out, 0.0, sh)
    return out


def epic():
    L = 1.1
    out = [0.0] * n(L)
    for f in (110, 165, 220, 277.2):          # a low brass stab (A minor-ish power chord)
        stab = []
        for i in range(n(0.9)):
            t = i / RATE
            v = saw(f * (1 + 0.003 * math.sin(2 * math.pi * 5 * t)), t)
            stab.append(v * env(t, 0.008, 0.28))
        mix(out, 0, stab, 0.3)
    mix(out, 0.02, bell(1760, 0.9, 0.3, 1.0), 0.55)
    mix(out, 0.0, thunk(70, 0.4, 0.15, 5, 0.4), 0.8)
    return out


def leg_charge():
    L = 0.75
    out = [0.0] * n(L)
    w = noise(L, 21, 0.2)
    for i in range(len(w)):
        t = i / RATE
        w[i] *= (t / L) ** 2 * 0.9
    mix(out, 0, w)
    s = sweep(180, 900, L, 10.0)
    for i in range(len(s)):
        t = i / RATE
        s[i] *= (t / L) ** 1.5 * (0.6 + 0.4 * math.sin(2 * math.pi * (8 + 20 * t / L) * t))
    mix(out, 0, s, 0.6)
    return out


def legendary():
    # a roar (filtered noise + growl) and a choir hit (stacked vowel-ish voices, D major) with a bell
    L = 2.6
    out = [0.0] * n(L)
    rnd = random.Random(31)
    y1 = y2 = 0.0
    roar = []
    for i in range(n(1.6)):
        t = i / RATE
        y1 += 0.12 * (rnd.uniform(-1, 1) - y1)
        y2 += 0.05 * (y1 - y2)
        growl = math.sin(2 * math.pi * (62 + 8 * math.sin(2 * math.pi * 3 * t)) * t)
        roar.append((y2 * 6 + 0.5 * growl * (1 + y1)) * env(t, 0.03, 0.55))
    mix(out, 0.0, roar, 0.9)
    chord = [146.8, 220.0, 293.7, 370.0, 440.0, 587.3]
    for k, f in enumerate(chord):
        v = []
        for i in range(n(2.3)):
            t = i / RATE
            vib = 1 + 0.006 * math.sin(2 * math.pi * (5.2 + 0.3 * k) * t + k)
            ph = 2 * math.pi * f * vib * t
            # a voice: fundamental + formant-ish partials (an "ah")
            s = math.sin(ph) + 0.6 * math.sin(2 * ph) + 0.45 * math.sin(3 * ph) + 0.25 * math.sin(5 * ph)
            v.append(s * env(t, 0.06, 0.9) * (1.0 if t < 1.6 else math.exp(-(t - 1.6) / 0.3)))
        mix(out, 0.08, v, 0.22)
    mix(out, 0.1, bell(1174.7, 2.0, 0.7, 1.0), 0.6)
    mix(out, 0.1, bell(1760, 2.0, 0.6, 0.8), 0.35)
    mix(out, 0.0, thunk(48, 0.7, 0.3, 9, 0.6), 1.0)
    return out


# ---------------------------------------------------------------- synergy / evolution ready
def synergy():
    out = [0.0] * n(1.4)
    for k, f in enumerate([1046.5, 1318.5, 1568, 2093]):
        mix(out, 0.06 * k, bell(f, 1.1, 0.35, 1.0), 0.7)
    mix(out, 0.24, bell(2637, 1.0, 0.5, 0.7), 0.6)
    mix(out, 0.0, thunk(180, 0.15, 0.04, 4, 1.0), 0.6)
    sh = noise(1.0, 13, 0.95)
    for i in range(len(sh)):
        sh[i] *= 0.1 * env(i / RATE, 0.2, 0.3)
    mix(out, 0.2, sh)
    return out


def snap():
    out = [0.0] * n(0.25)
    mix(out, 0, thunk(420, 0.2, 0.03, 6, 1.4), 1.0)
    mix(out, 0.004, thunk(1300, 0.08, 0.01, 8, 1.0), 0.6)
    return out


def evoready():
    out = [0.0] * n(0.9)
    mix(out, 0, bell(1318.5, 0.8, 0.22, 0.8), 0.7)
    mix(out, 0.09, bell(1975.5, 0.8, 0.3, 0.8), 0.7)
    return out


# ---------------------------------------------------------------- tally
def tick():
    out = [0.0] * n(0.05)
    mix(out, 0, thunk(2200, 0.04, 0.006, 3, 0.7), 1.0)
    return out


def stamp():
    out = [0.0] * n(0.35)
    mix(out, 0, thunk(85, 0.35, 0.09, 12, 1.6), 1.0)
    return out


def record():
    out = [0.0] * n(1.0)
    mix(out, 0, thunk(80, 0.35, 0.1, 14, 1.6), 1.0)
    for k, f in enumerate([1568, 2093, 2637]):
        mix(out, 0.05 + 0.05 * k, bell(f, 0.8, 0.25, 0.9), 0.5)
    return out


# ---------------------------------------------------------------- legendary effects
def lg_chain():
    out = [0.0] * n(0.6)
    rnd = random.Random(41)
    crack = []
    for i in range(n(0.5)):
        t = i / RATE
        crack.append(rnd.uniform(-1, 1) * env(t, 0.001, 0.05) * (1 if rnd.random() < 0.6 else 0.2))
    mix(out, 0, crack, 1.0)
    mix(out, 0.0, thunk(60, 0.5, 0.18, 2, 0.5), 0.7)
    return out


def lg_echo():
    out = [0.0] * n(0.7)
    base = sweep(900, 500, 0.3, 0.08)
    for k in range(4):
        mix(out, 0.09 * k, base, 0.8 * (0.55 ** k))
    return out


def lg_vampire():
    out = [0.0] * n(0.7)
    s = sweep(300, 900, 0.5, 0.25)
    mix(out, 0, s, 0.5)
    w = noise(0.6, 17, 0.25)
    for i in range(len(w)):
        w[i] *= env(i / RATE, 0.1, 0.2)
    mix(out, 0, w, 0.6)
    mix(out, 0.25, bell(660, 0.4, 0.15, 0.5), 0.4)
    return out


def lg_time():
    out = [0.0] * n(0.5)
    mix(out, 0, thunk(2600, 0.05, 0.01, 4, 0.6), 0.8)
    mix(out, 0.12, thunk(2000, 0.05, 0.01, 5, 0.6), 0.8)
    mix(out, 0.18, sweep(400, 1600, 0.25, 0.2), 0.4)
    return out


def lg_blood():
    out = [0.0] * n(0.5)
    mix(out, 0, thunk(55, 0.3, 0.08, 7, 0.3), 1.0)
    mix(out, 0.16, thunk(50, 0.3, 0.07, 8, 0.3), 0.8)
    return out


def lg_mirror():
    out = [0.0] * n(0.9)
    rnd = random.Random(51)
    for k in range(14):
        f = rnd.uniform(2500, 6500)
        mix(out, rnd.uniform(0, 0.12), bell(f, 0.4, 0.06 + rnd.random() * 0.08, 0.5), 0.25)
    w = noise(0.3, 52, 0.95)
    for i in range(len(w)):
        w[i] *= env(i / RATE, 0.001, 0.06)
    mix(out, 0, w, 0.7)
    mix(out, 0, thunk(140, 0.3, 0.06, 53, 0.5), 0.5)
    return out


def lg_phoenix():
    L = 1.6
    out = [0.0] * n(L)
    w = noise(L, 61, 0.15)
    for i in range(len(w)):
        t = i / RATE
        w[i] *= env(t, 0.15, 0.5) * 3
    mix(out, 0, w, 0.9)
    mix(out, 0.0, sweep(120, 700, 0.9, 0.5, 'saw'), 0.35)
    mix(out, 0.15, bell(880, 1.2, 0.5, 1.0), 0.5)
    mix(out, 0.0, thunk(50, 0.6, 0.25, 62, 0.8), 1.0)
    return out


def lg_orb():
    out = [0.0] * n(0.2)
    mix(out, 0, sweep(1800, 600, 0.15, 0.05), 0.7)
    w = noise(0.1, 71, 0.9)
    for i in range(len(w)):
        w[i] *= env(i / RATE, 0.001, 0.02)
    mix(out, 0, w, 0.5)
    return out


def lg_agent():
    out = [0.0] * n(0.9)
    v = []
    for i in range(n(0.8)):
        t = i / RATE
        f = 520 + 160 * math.sin(2 * math.pi * 7 * t)
        v.append(math.sin(2 * math.pi * f * t) * env(t, 0.02, 0.3))
    mix(out, 0, v, 0.7)
    mix(out, 0.1, bell(784, 0.6, 0.2, 0.6), 0.4)
    return out


def lg_grave():
    out = [0.0] * n(0.6)
    w = noise(0.35, 81, 0.3)
    for i in range(len(w)):
        w[i] *= env(i / RATE, 0.01, 0.1)
    mix(out, 0, w, 0.8)
    mix(out, 0.12, bell(1396.9, 0.4, 0.12, 0.6), 0.5)
    return out


def lg_blackmass():
    L = 1.2
    out = [0.0] * n(L)
    v = []
    for i in range(n(L)):
        t = i / RATE
        v.append(math.sin(2 * math.pi * (40 + 25 * t) * t) * env(t, 0.05, 0.5) * (1 + 0.5 * math.sin(2 * math.pi * 6 * t)))
    mix(out, 0, v, 1.0)
    mix(out, 0.0, sweep(2000, 200, 1.0, 0.4), 0.25)
    return out


def lg_deadeye():
    out = [0.0] * n(0.5)
    mix(out, 0, bell(2793, 0.45, 0.12, 1.0), 0.8)
    mix(out, 0, thunk(1500, 0.05, 0.008, 9, 0.8), 0.5)
    return out


def main():
    write('flip.wav', flip(), 0.6)
    write('rare.wav', rare(), 0.7)
    write('epic.wav', epic(), 0.85)
    write('leg_charge.wav', leg_charge(), 0.7)
    write('legendary.wav', legendary(), 0.95)
    write('synergy.wav', synergy(), 0.95)
    write('snap.wav', snap(), 0.9)
    write('evoready.wav', evoready(), 0.7)
    write('tick.wav', tick(), 0.45)
    write('stamp.wav', stamp(), 0.85)
    write('record.wav', record(), 0.9)
    write('lg_chain.wav', lg_chain(), 0.8)
    write('lg_echo.wav', lg_echo(), 0.7)
    write('lg_vampire.wav', lg_vampire(), 0.7)
    write('lg_time.wav', lg_time(), 0.7)
    write('lg_blood.wav', lg_blood(), 0.8)
    write('lg_mirror.wav', lg_mirror(), 0.85)
    write('lg_phoenix.wav', lg_phoenix(), 0.95)
    write('lg_orb.wav', lg_orb(), 0.5)
    write('lg_agent.wav', lg_agent(), 0.7)
    write('lg_grave.wav', lg_grave(), 0.7)
    write('lg_blackmass.wav', lg_blackmass(), 0.9)
    write('lg_deadeye.wav', lg_deadeye(), 0.7)
    print('wrote', OUT)


if __name__ == '__main__':
    main()
