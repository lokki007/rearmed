"""Synthesized sounds for DESCENT v2 acts and bosses (assets/sound/rearmed/acts/, 22 kHz 16-bit
mono): the boss attack warning, meat hook, rail charge, roar, blink, gravity well hum, shield,
fire nova, the act intro sting and the vault chime. ACTS worker; our own generated content."""
import math, random, struct, wave, pathlib

RATE = 22050
OUT = pathlib.Path(__file__).resolve().parent.parent / 'assets' / 'sound' / 'rearmed' / 'acts'


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


def noise(length, seed, k=0.3):
    rnd = random.Random(seed)
    y, out = 0.0, []
    for _ in range(n(length)):
        y += k * (rnd.uniform(-1, 1) - y)
        out.append(y)
    return out


def sweep(f0, f1, length, harm=(1.0, 0.4, 0.2), curve=1.0):
    ph, out = 0.0, []
    for i in range(n(length)):
        t = i / RATE
        x = (t / length) ** curve
        f = f0 + (f1 - f0) * x
        ph += 2 * math.pi * f / RATE
        out.append(sum(a * math.sin((h + 1) * ph) for h, a in enumerate(harm)))
    return out


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


def warn():
    # a short rising two-tone siren: "something is coming at you" (~0.9 s, the telegraph length)
    out = [0.0] * n(0.95)
    s = sweep(330, 700, 0.85, (1.0, 0.0, 0.35), 1.6)
    for i in range(len(s)):
        t = i / RATE
        s[i] *= min(1.0, t / 0.03) * (0.6 + 0.4 * math.sin(2 * math.pi * 9 * t)) * (1.0 - 0.3 * t)
    mix(out, 0.0, s, 0.5)
    return out


def hook():
    # chain rattle: rapid metallic clicks rising, ends in a snap
    out = [0.0] * n(1.2)
    rnd = random.Random(11)
    t = 0.0
    k = 0
    while t < 0.95:
        f = 1800 + rnd.uniform(-300, 300) + 900 * t
        click = [(tone(f, i / RATE) + 0.5 * tone(f * 1.53, i / RATE)) * env(i / RATE, 0.0005, 0.012) for i in range(n(0.04))]
        mix(out, t, click, 0.35 + 0.4 * t)
        t += 0.07 - 0.04 * t
        k += 1
    snap = [(rnd.uniform(-1, 1) * 0.7 + tone(420, i / RATE)) * env(i / RATE, 0.001, 0.05) for i in range(n(0.2))]
    mix(out, 0.98, snap, 0.9)
    return out


def charge():
    # rail lance charge: a whine climbing to a held note
    out = [0.0] * n(1.2)
    s = sweep(220, 1400, 1.1, (1.0, 0.5, 0.25, 0.12), 0.7)
    for i in range(len(s)):
        t = i / RATE
        s[i] *= min(1.0, t / 0.05) * (0.5 + 0.5 * t)
    mix(out, 0.0, s, 0.45)
    return out


def roar():
    # a beast's roar: low growl (detuned saws + noise), swelling then falling
    out = [0.0] * n(1.9)
    rnd = random.Random(5)
    ph1 = ph2 = 0.0
    y = 0.0
    for i in range(n(1.8)):
        t = i / RATE
        f = 70 + 30 * math.sin(math.pi * min(1.0, t / 1.4)) - 15 * t
        ph1 += 2 * math.pi * f / RATE
        ph2 += 2 * math.pi * f * 1.51 / RATE
        saw = ((ph1 / math.pi) % 2 - 1) + 0.6 * ((ph2 / math.pi) % 2 - 1)
        y += 0.25 * (rnd.uniform(-1, 1) - y)
        a = min(1.0, t / 0.15) * (1.0 if t < 1.1 else math.exp(-(t - 1.1) / 0.25))
        out[i] += (0.7 * saw + 0.8 * y) * a * (0.8 + 0.2 * math.sin(2 * math.pi * 23 * t))
    return out


def blink():
    # a reversed whoosh into a soft pop
    out = [0.0] * n(0.9)
    w = noise(0.7, 21, 0.5)
    for i in range(len(w)):
        t = i / RATE
        w[i] *= (t / 0.7) ** 2
    mix(out, 0.0, w, 0.7)
    mix(out, 0.7, sweep(900, 200, 0.18, (1.0, 0.3)), 0.4)
    return out


def well():
    # a deep throbbing hum that pulls downwards
    out = [0.0] * n(1.4)
    s = sweep(110, 55, 1.4, (1.0, 0.6, 0.3, 0.15), 1.0)
    for i in range(len(s)):
        t = i / RATE
        s[i] *= min(1.0, t / 0.2) * (0.6 + 0.4 * math.sin(2 * math.pi * 6 * t)) * math.exp(-t / 1.2)
    mix(out, 0.0, s, 0.7)
    return out


def shield():
    # an electric hum coming up
    out = [0.0] * n(1.0)
    s = sweep(180, 240, 1.0, (1.0, 0.8, 0.6, 0.4, 0.3), 1.0)
    for i in range(len(s)):
        t = i / RATE
        s[i] *= min(1.0, t / 0.25) * math.exp(-t / 0.7) * (0.7 + 0.3 * math.sin(2 * math.pi * 50 * t))
    mix(out, 0.0, s, 0.5)
    return out


def nova():
    # fire gathering: a crackle rising into a whoomp
    out = [0.0] * n(1.4)
    c = noise(0.9, 31, 0.6)
    for i in range(len(c)):
        t = i / RATE
        c[i] *= (t / 0.9) ** 1.5 * (0.5 + 0.5 * (random.Random(i // 200).random()))
    mix(out, 0.0, c, 0.5)
    mix(out, 0.88, boom(55, 0.5, 0.18, 33), 0.9)
    return out


def act():
    # the act sting: a deep drum, a low choir-ish fifth, a distant bell
    out = [0.0] * n(2.6)
    mix(out, 0.0, boom(42, 1.4, 0.45, 41), 1.0)
    for f in (73.4, 110.0, 146.8):
        seg = []
        for i in range(n(2.2)):
            t = i / RATE
            v = 1 + 0.004 * math.sin(2 * math.pi * 4.5 * t + f)
            seg.append(sum(tone(f * h * v, t) / (h * h) for h in range(1, 6)) * min(1.0, t / 0.4) * math.exp(-t / 1.1))
        mix(out, 0.12, seg, 0.28)
    bell = [(tone(587, i / RATE) + 0.4 * tone(587 * 2.76, i / RATE)) * env(i / RATE, 0.003, 0.9) for i in range(n(1.6))]
    mix(out, 0.5, bell, 0.18)
    return out


def vault():
    # a glittering chime: quick high arpeggio over a soft pad
    out = [0.0] * n(2.0)
    for k, f in enumerate((1047, 1319, 1568, 2093, 1568, 2637)):
        b = [(tone(f, i / RATE) + 0.3 * tone(f * 2.01, i / RATE)) * env(i / RATE, 0.002, 0.35) for i in range(n(0.9))]
        mix(out, k * 0.07, b, 0.28)
    pad = [sum(tone(f, i / RATE) for f in (523, 659, 784)) * min(1.0, (i / RATE) / 0.3) * math.exp(-(i / RATE) / 0.9) for i in range(n(1.8))]
    mix(out, 0.1, pad, 0.12)
    return out


def clock_tick():
    # round 4 (en-boss): the boss clock's last 30 s, one a second: a dry wooden tick
    out = [0.0] * n(0.16)
    click = [(tone(1850, i / RATE) + 0.5 * tone(3100, i / RATE)) * env(i / RATE, 0.0008, 0.018) for i in range(n(0.08))]
    mix(out, 0.0, click, 0.8)
    mix(out, 0.0, [s * env(i / RATE, 0.0005, 0.01) for i, s in enumerate(noise(0.05, 11, 0.6))], 0.5)
    return out


def clock_tock():
    # the last 10 s: lower, heavier, with a heartbeat thump under it
    out = [0.0] * n(0.3)
    click = [(tone(1100, i / RATE) + 0.6 * tone(2250, i / RATE)) * env(i / RATE, 0.0008, 0.03) for i in range(n(0.12))]
    mix(out, 0.0, click, 0.75)
    mix(out, 0.0, boom(55, 0.25, 0.07, 5), 0.9)
    return out


def lastminute():
    # the last minute: a low brass-like swell with a rising siren on top
    out = [0.0] * n(1.6)
    for f in (98, 147, 196):
        seg = [sum(tone(f * h, i / RATE) / h for h in range(1, 5)) * min(1.0, (i / RATE) / 0.25) * math.exp(-(i / RATE) / 0.9)
               for i in range(n(1.5))]
        mix(out, 0.0, seg, 0.22)
    s = sweep(300, 820, 1.1, (1.0, 0.0, 0.3), 1.4)
    for i in range(len(s)):
        t = i / RATE
        s[i] *= min(1.0, t / 0.05) * (0.6 + 0.4 * math.sin(2 * math.pi * 7 * t)) * (1.0 - 0.5 * t)
    mix(out, 0.2, s, 0.35)
    return out


def timeup():
    # time up: a falling buzzer and a deep gong
    out = [0.0] * n(2.2)
    s = sweep(520, 140, 0.9, (1.0, 0.6, 0.4), 0.8)
    for i in range(len(s)):
        s[i] *= min(1.0, (i / RATE) / 0.02) * (1.0 - 0.6 * (i / RATE) / 0.9)
    mix(out, 0.0, s, 0.45)
    gong = [sum(tone(f, i / RATE) for f in (73, 110.5, 164.7)) * env(i / RATE, 0.005, 0.8) for i in range(n(1.8))]
    mix(out, 0.25, gong, 0.35)
    mix(out, 0.25, boom(45, 0.6, 0.2, 9), 0.6)
    return out


if __name__ == '__main__':
    write('clock_tick.wav', clock_tick(), 0.55)
    write('clock_tock.wav', clock_tock(), 0.7)
    write('lastminute.wav', lastminute(), 0.8)
    write('timeup.wav', timeup(), 0.85)
    write('warn.wav', warn(), 0.7)
    write('hook.wav', hook(), 0.8)
    write('charge.wav', charge(), 0.7)
    write('roar.wav', roar(), 0.9)
    write('blink.wav', blink(), 0.7)
    write('well.wav', well(), 0.8)
    write('shield.wav', shield(), 0.6)
    write('nova.wav', nova(), 0.85)
    write('act.wav', act(), 0.85)
    write('vault.wav', vault(), 0.7)
    print('wrote', OUT)
