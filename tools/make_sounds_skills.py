"""Synthesized sounds for the DESCENT skills (assets/sound/rearmed/skills/, 22 kHz 16-bit mono):
a soft skill level-up chime (HUD), a perk taken on the skills screen, a respec whoosh, a locked
click. All generated here from sine / noise, no samples. SKILLS worker."""
import math, random, struct, wave, pathlib

RATE = 22050
OUT = pathlib.Path(__file__).resolve().parent.parent / 'assets' / 'sound' / 'rearmed' / 'skills'


def write(name, samples, level=0.85):
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
    return [(tone(f, i / RATE) + 0.4 * tone(f * 2.0, i / RATE) + 0.15 * tone(f * 3.01, i / RATE))
            * env(i / RATE, 0.004, decay) for i in range(n(length))]


def noise(length, seed, smooth=0.5):
    rnd = random.Random(seed)
    y, out = 0.0, []
    for i in range(n(length)):
        y += smooth * (rnd.uniform(-1, 1) - y)
        out.append(y)
    return out


# skill level-up: two soft rising glassy notes (quieter than the weapon level-up)
out = [0.0] * n(0.9)
mix(out, 0.0, bell(880, 0.6, 0.18), 0.6)
mix(out, 0.09, bell(1318.5, 0.75, 0.25), 0.5)
write('sk_levelup.wav', out, 0.55)

# perk taken: a star lighting up, a bright three-note sparkle over a low swell
out = [0.0] * n(1.1)
for k, f in enumerate((659.3, 987.8, 1318.5)):
    mix(out, 0.05 * k, bell(f, 0.8, 0.3), 0.5)
swell = [tone(220, i / RATE) * env(i / RATE, 0.08, 0.35) for i in range(n(1.0))]
mix(out, 0.0, swell, 0.35)
write('sk_perk.wav', out, 0.8)

# respec: a falling airy whoosh
out = [0.0] * n(0.9)
ns = noise(0.9, 7, 0.2)
for i in range(len(out)):
    t = i / RATE
    out[i] = ns[i] * env(t, 0.15, 0.3) + 0.3 * tone(600 - 400 * min(1.0, t / 0.9), t) * env(t, 0.05, 0.3)
write('sk_respec.wav', out, 0.7)

# locked: a dull short click
out = [0.0] * n(0.12)
ns = noise(0.12, 3, 0.6)
for i in range(len(out)):
    t = i / RATE
    out[i] = (0.6 * ns[i] + tone(140, t)) * env(t, 0.001, 0.025)
write('sk_locked.wav', out, 0.6)
print('wrote', OUT)
