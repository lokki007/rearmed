"""Synthesized sounds for the random events, perk cards, weapon wheel, tips and slow motion
(assets/sound/rearmed/events/, 22 kHz 16-bit mono). Events worker, v4."""
import math, random, struct, wave, pathlib

RATE = 22050
OUT = pathlib.Path(__file__).resolve().parent.parent / 'assets' / 'sound' / 'rearmed' / 'events'


def write(name, samples, level=0.92):
    OUT.mkdir(parents=True, exist_ok=True)
    peak = max(1e-9, max(abs(s) for s in samples))
    with wave.open(str(OUT / name), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b''.join(struct.pack('<h', int(s / peak * 32767 * level)) for s in samples))


def env(t, attack, decay):
    return min(1.0, t / attack if attack > 0 else 1.0) * math.exp(-t / decay)


def n(sec):
    return int(RATE * sec)


def tone(f, t):
    return math.sin(2 * math.pi * f * t)


def lowpass(samples, k):
    out, y = [], 0.0
    for s in samples:
        y += k * (s - y)
        out.append(y)
    return out


def incoming():
    # two-tone alarm swell: "wee-oo wee-oo" with a sub hit at the start
    out = []
    for i in range(n(1.3)):
        t = i / RATE
        seg = int(t / 0.3)
        f = 880 if seg % 2 == 0 else 620
        f *= 1 + 0.03 * math.sin(2 * math.pi * 6 * t)
        s = 0.6 * tone(f, t) + 0.25 * tone(f * 2, t) + 0.15 * tone(f * 3.01, t)
        a = min(1.0, t / 0.05) * (1.0 if t < 1.0 else max(0.0, 1 - (t - 1.0) / 0.3))
        sub = tone(55, t) * env(t, 0.005, 0.25)
        out.append(0.55 * s * a + 0.8 * sub)
    return out


def tick(f=1500, length=0.09):
    rnd = random.Random(f)
    out = []
    for i in range(n(length)):
        t = i / RATE
        out.append((tone(f, t) + 0.4 * tone(f * 2.7, t)) * env(t, 0.001, 0.02) + 0.4 * rnd.uniform(-1, 1) * env(t, 0.0002, 0.003))
    return out


def roulette():
    # a slot machine rattle speeding up into a ding
    out = [0.0] * n(1.05)
    t0, gap = 0.0, 0.11
    while t0 < 0.8:
        for i in range(n(0.03)):
            j = n(t0) + i
            if j < len(out):
                t = i / RATE
                out[j] += (tone(2400, t) + 0.5 * tone(3700, t)) * env(t, 0.0005, 0.008)
        t0 += gap
        gap = max(0.035, gap * 0.82)
    for i in range(n(0.25)):
        j = n(0.8) + i
        t = i / RATE
        if j < len(out):
            out[j] += 0.8 * (tone(1320, t) + 0.6 * tone(1980, t) + 0.3 * tone(2640, t)) * env(t, 0.002, 0.08)
    return out


def meteor_loop():
    # burning roar: filtered noise with a slow flutter, loopable (1 s, seam faded)
    rnd = random.Random(3)
    raw = [rnd.uniform(-1, 1) for _ in range(n(1.0))]
    lp = lowpass(raw, 0.08)
    lp2 = lowpass(raw, 0.3)
    out = []
    L = len(lp)
    for i in range(L):
        t = i / RATE
        s = lp[i] * (0.8 + 0.2 * math.sin(2 * math.pi * 7 * t)) + 0.25 * lp2[i]
        out.append(s)
    # crossfade the ends so the loop has no click
    fade = n(0.08)
    for i in range(fade):
        a = i / fade
        out[i] = out[i] * a + out[L - fade + i] * (1 - a)
    return out[:L - fade]


def meteor_warn():
    # falling whistle: a descending sine with air
    rnd = random.Random(9)
    out, ph = [], 0.0
    dur = 1.3
    for i in range(n(dur)):
        t = i / RATE
        f = 1800 * (1 - 0.65 * (t / dur) ** 1.4)
        ph += 2 * math.pi * f / RATE
        a = min(1.0, t / 0.2) * (0.4 + 0.6 * t / dur)
        out.append(a * (0.7 * math.sin(ph) + 0.12 * rnd.uniform(-1, 1)))
    return out


def strike_warn():
    # electric charge building up: crackles getting denser over a rising hum
    rnd = random.Random(5)
    out = []
    dur = 1.0
    for i in range(n(dur)):
        t = i / RATE
        hum = 0.3 * tone(120 + 200 * t, t) * (t / dur)
        crack = 0.0
        if rnd.random() < 0.004 + 0.03 * t:
            crack = rnd.uniform(-1, 1)
        out.append(hum + crack)
    return lowpass(out, 0.6)


def corpse_beep():
    out = []
    for i in range(n(0.07)):
        t = i / RATE
        out.append((tone(2000, t) + 0.3 * tone(4000, t)) * min(1.0, t / 0.002) * (1 if t < 0.05 else max(0, 1 - (t - 0.05) / 0.02)))
    return out


def slowmo():
    # a deep "whoom" dropping in pitch with a reverse-swell feel
    out, ph = [], 0.0
    dur = 1.1
    rnd = random.Random(2)
    noise = lowpass([rnd.uniform(-1, 1) for _ in range(n(dur))], 0.05)
    for i in range(n(dur)):
        t = i / RATE
        f = 180 * (1 - 0.7 * min(1.0, t / 0.8))
        ph += 2 * math.pi * f / RATE
        a = min(1.0, t / 0.04) * math.exp(-t / 0.5)
        out.append(a * (0.8 * math.sin(ph) + 0.3 * math.sin(ph * 2.02)) + 0.6 * noise[i] * math.exp(-t / 0.3))
    return out


def tip():
    out = []
    for i in range(n(0.35)):
        t = i / RATE
        a = tone(988, t) * env(t, 0.003, 0.08)
        t2 = t - 0.09
        b = tone(1319, t2) * env(t2, 0.003, 0.12) if t2 > 0 else 0.0
        out.append(0.6 * a + 0.6 * b)
    return out


def perk_pick():
    # bright upward arpeggio + shimmer: the "you got it" sound
    out = [0.0] * n(0.7)
    for k, f in enumerate((523, 659, 784, 1047)):
        st = n(0.055 * k)
        for i in range(n(0.5)):
            j = st + i
            if j >= len(out):
                break
            t = i / RATE
            out[j] += (tone(f, t) + 0.35 * tone(f * 2, t) + 0.15 * tone(f * 3, t)) * env(t, 0.002, 0.18)
    rnd = random.Random(4)
    for i in range(len(out)):
        t = i / RATE
        out[i] += 0.15 * rnd.uniform(-1, 1) * env(t, 0.001, 0.05)
    return out


def perk_cards():
    # three quick card whooshes
    rnd = random.Random(8)
    out = [0.0] * n(0.5)
    for k in range(3):
        st = n(0.07 * k)
        bp = lp = 0.0
        for i in range(n(0.2)):
            j = st + i
            t = i / RATE
            f = 800 + 3000 * (t / 0.2)
            kk = 2 * math.sin(math.pi * f / RATE)
            x = rnd.uniform(-1, 1)
            lp += kk * bp
            hp = x - lp - 0.4 * bp
            bp += kk * hp
            out[j] += bp * math.sin(math.pi * t / 0.2) * 0.6
    return out


def wheel_open():
    rnd = random.Random(12)
    out, bp, lp = [], 0.0, 0.0
    for i in range(n(0.16)):
        t = i / RATE
        f = 600 + 2400 * (t / 0.16)
        kk = 2 * math.sin(math.pi * f / RATE)
        lp += kk * bp
        hp = rnd.uniform(-1, 1) - lp - 0.4 * bp
        bp += kk * hp
        out.append(bp * math.sin(math.pi * t / 0.16) + 0.3 * tone(330, t) * env(t, 0.002, 0.04))
    return out


def wheel_close():
    return list(reversed(wheel_open()))


def wheel_select():
    out = []
    for i in range(n(0.18)):
        t = i / RATE
        out.append((tone(660, t) + 0.5 * tone(1320, t)) * env(t, 0.001, 0.05) + 0.6 * tone(90, t) * env(t, 0.001, 0.03))
    return out


if __name__ == '__main__':
    write('incoming.wav', incoming())
    write('tick.wav', tick(1500), 0.7)
    write('roulette.wav', roulette(), 0.8)
    write('meteor_loop.wav', meteor_loop(), 0.8)
    write('meteor_warn.wav', meteor_warn(), 0.8)
    write('strike_warn.wav', strike_warn(), 0.8)
    write('corpse_beep.wav', corpse_beep(), 0.7)
    write('slowmo.wav', slowmo())
    write('tip.wav', tip(), 0.6)
    write('perk_pick.wav', perk_pick(), 0.85)
    write('perk_cards.wav', perk_cards(), 0.7)
    write('wheel_open.wav', wheel_open(), 0.55)
    write('wheel_close.wav', wheel_close(), 0.45)
    write('wheel_tick.wav', tick(2600, 0.04), 0.45)
    write('wheel_select.wav', wheel_select(), 0.7)
    print('event sounds written to', OUT)
