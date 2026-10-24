"""Synthesized sounds for the DESCENT v2 ARSENAL: weapon level-up, evolution, and the layered
fire / impact sounds of the evolved weapons (assets/sound/rearmed/ds/, 22 kHz 16-bit mono).
All generated here from sine / noise, no samples. ARSENAL worker."""
import math, random, struct, wave, pathlib

RATE = 22050
OUT = pathlib.Path(__file__).resolve().parent.parent / 'assets' / 'sound' / 'rearmed' / 'ds'


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


def noise(length, seed, smooth=0.5):
    rnd = random.Random(seed)
    y, out = 0.0, []
    for i in range(n(length)):
        y += smooth * (rnd.uniform(-1, 1) - y)
        out.append(y)
    return out


def sweep(f0, f1, length, decay, harm=3, attack=0.003):
    out, ph = [], 0.0
    for i in range(n(length)):
        t = i / RATE
        f = f0 + (f1 - f0) * min(1.0, t / length)
        ph += 2 * math.pi * f / RATE
        out.append(sum(math.sin(ph * h) / h for h in range(1, harm + 1)) * env(t, attack, decay))
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


def levelup():
    # short bright two-note rise with a sparkle: "this gun got better"
    out = [0.0] * n(0.7)
    mix(out, 0.0, bell(880, 0.5, 0.18), 0.55)
    mix(out, 0.07, bell(1320, 0.55, 0.22), 0.6)
    sp = noise(0.35, 7, 0.9)
    mix(out, 0.07, [s * env(i / RATE, 0.01, 0.08) * tone(6000, i / RATE) for i, s in enumerate(sp)], 0.25)
    return out


def evolve():
    # a swelling shimmer that snaps into a big major chord and a low hit
    out = [0.0] * n(2.2)
    sh = noise(1.0, 11, 0.95)
    mix(out, 0.0, [s * min(1.0, (i / RATE) / 0.8) * tone(3000 + 2500 * (i / RATE), i / RATE)
                   for i, s in enumerate(sh)], 0.25)
    mix(out, 0.0, sweep(180, 720, 0.9, 2.0, 4, 0.6), 0.35)
    mix(out, 0.9, boom(55, 1.2, 0.45, 3), 0.9)
    for k, f in enumerate((392, 494, 587, 784)):
        mix(out, 0.9 + 0.02 * k, bell(f, 1.2, 0.5), 0.45)
    return out


def bullet():
    # BULLET HELL / RIPSAW: a sharp metallic zing layered on the gunshot
    out = [0.0] * n(0.18)
    mix(out, 0.0, sweep(3400, 1800, 0.15, 0.04, 2), 0.6)
    mix(out, 0.0, [s * env(i / RATE, 0.001, 0.025) for i, s in enumerate(noise(0.1, 21, 0.7))], 0.7)
    return out


def flame():
    # DRAGON'S BREATH: a breathy roar
    out = [0.0] * n(0.6)
    nz = noise(0.6, 31, 0.15)
    mix(out, 0.0, [s * env(i / RATE, 0.02, 0.2) for i, s in enumerate(nz)], 1.0)
    mix(out, 0.0, boom(70, 0.4, 0.12, 5), 0.4)
    return out


def rocket():
    # HELLFIRE / ARMAGEDDON salvo: a rising scream
    out = [0.0] * n(0.7)
    mix(out, 0.0, sweep(300, 900, 0.6, 0.3, 5, 0.01), 0.4)
    nz = noise(0.7, 41, 0.3)
    mix(out, 0.0, [s * env(i / RATE, 0.005, 0.25) for i, s in enumerate(nz)], 0.7)
    return out


def storm():
    # STORM CALLER: crackling fork
    rnd = random.Random(51)
    out = [0.0] * n(0.35)
    for k in range(9):
        at = rnd.uniform(0, 0.22)
        mix(out, at, [s * env(i / RATE, 0.0005, 0.012) for i, s in enumerate(noise(0.06, 60 + k, 0.95))], 0.8)
    mix(out, 0.0, sweep(1200, 400, 0.3, 0.1, 3), 0.25)
    return out


def holy():
    # JUDGEMENT: a choir-like shimmering chord and a crack
    out = [0.0] * n(1.0)
    for f in (523, 659, 784, 1047):
        seg = [sum(tone(f * h * (1 + 0.004 * math.sin(2 * math.pi * 6 * i / RATE)), i / RATE) / h
                   for h in (1, 2, 3)) * env(i / RATE, 0.05, 0.35) for i in range(n(0.9))]
        mix(out, 0.0, seg, 0.18)
    mix(out, 0.0, [s * env(i / RATE, 0.001, 0.03) for i, s in enumerate(noise(0.1, 71, 0.9))], 0.8)
    return out


def star():
    # STAR FURNACE: a glassy chime with a fizz
    out = [0.0] * n(0.4)
    mix(out, 0.0, bell(1568, 0.35, 0.09), 0.5)
    mix(out, 0.0, bell(2093, 0.35, 0.07), 0.35)
    mix(out, 0.0, [s * env(i / RATE, 0.002, 0.05) for i, s in enumerate(noise(0.2, 81, 0.9))], 0.3)
    return out


def void():
    # DOOMSDAY: a deep downward suck and a sub thump
    out = [0.0] * n(1.4)
    mix(out, 0.0, sweep(220, 35, 1.0, 0.6, 4, 0.2), 0.6)
    mix(out, 0.9, boom(40, 0.5, 0.25, 9), 0.9)
    return out


def claw():
    # HEARTRIPPER: a wet slash
    out = [0.0] * n(0.3)
    nz = noise(0.3, 91, 0.6)
    mix(out, 0.0, [s * env(i / RATE, 0.004, 0.06) * (0.5 + 0.5 * tone(90, i / RATE)) for i, s in enumerate(nz)], 1.0)
    mix(out, 0.0, sweep(900, 300, 0.12, 0.05, 2), 0.3)
    return out


def blast():
    # small arsenal explosion (explosive rounds, bomblets, kill blasts)
    out = [0.0] * n(0.6)
    mix(out, 0.0, boom(80, 0.6, 0.15, 13), 1.0)
    mix(out, 0.0, [s * env(i / RATE, 0.001, 0.05) for i, s in enumerate(noise(0.2, 14, 0.8))], 0.5)
    return out


def ping():
    # IRON MAIDEN: a bright ricochet ping
    out = [0.0] * n(0.3)
    mix(out, 0.0, sweep(4200, 2600, 0.25, 0.07, 2), 0.7)
    return out


if __name__ == '__main__':
    write('ds_levelup.wav', levelup(), 0.8)
    write('ds_evolve.wav', evolve())
    write('ds_bullet.wav', bullet(), 0.6)
    write('ds_flame.wav', flame(), 0.7)
    write('ds_rocket.wav', rocket(), 0.7)
    write('ds_storm.wav', storm(), 0.6)
    write('ds_holy.wav', holy(), 0.8)
    write('ds_star.wav', star(), 0.6)
    write('ds_void.wav', void())
    write('ds_claw.wav', claw(), 0.8)
    write('ds_blast.wav', blast(), 0.8)
    write('ds_ping.wav', ping(), 0.6)
    print('wrote', OUT)
