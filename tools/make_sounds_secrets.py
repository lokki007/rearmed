"""Synthesized sounds for DESCENT round 3 SECRETS (assets/sound/rearmed/secrets/, 22 kHz 16-bit mono):
the strongbox hum (a seamless 2 s loop in four loudness bands, hum0 = right next to it), the lid
creaking open, the SECRET FOUND fanfare (found / found_rare) and the DOWSING ROD compass tick.
ds-secrets worker. Run: python tools/make_sounds_secrets.py"""
import math
import pathlib
import random
import struct
import wave

RATE = 22050
OUT = pathlib.Path(__file__).resolve().parent.parent / 'assets' / 'sound' / 'rearmed' / 'secrets'


def write(name, samples, level=0.9):
    OUT.mkdir(parents=True, exist_ok=True)
    peak = max(1e-9, max(abs(s) for s in samples))
    with wave.open(str(OUT / name), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b''.join(struct.pack('<h', int(s / peak * 32767 * level)) for s in samples))
    print('wrote', OUT / name, '%.2f s' % (len(samples) / RATE))


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
            * env(i / RATE, 0.003, decay) for i in range(n(length))]


def noise(length, seed, lp=0.5):
    rnd = random.Random(seed)
    y, out = 0.0, []
    for _ in range(n(length)):
        y += lp * (rnd.uniform(-1, 1) - y)
        out.append(y)
    return out


# ---------------------------------------------------------------- the hum (seamless loop)
def hum():
    """A low reliquary drone with a slow beat and a breathy whisper on top. Every partial has a
    whole number of cycles in the loop, the whisper is cross-faded end into start."""
    L = 2.0
    N = n(L)
    out = []
    for i in range(N):
        t = i / RATE
        beat = 0.75 + 0.25 * math.sin(2 * math.pi * 1.0 * t)             # 1 Hz swell
        s = (0.55 * tone(55.0, t) + 0.35 * tone(110.5, t) + 0.18 * tone(165.0, t)
             + 0.08 * tone(330.0, t) * (0.5 + 0.5 * math.sin(2 * math.pi * 0.5 * t)))
        out.append(s * beat)
    # whisper: band-limited noise, swelling twice per loop, cross-faded for the seam
    w = noise(L + 0.35, 11, 0.18)
    x = 0.0
    hp = []
    for s in w:                      # a crude high-pass (remove the rumble of the low-pass noise)
        x = 0.92 * x + 0.08 * s
        hp.append(s - x)
    F = n(0.3)
    for i in range(N):
        s = hp[i]
        if i < F:
            a = i / F
            s = s * a + hp[N + i] * (1 - a)
        sw = 0.5 + 0.5 * math.sin(2 * math.pi * 1.0 * (i / RATE) + 1.2)
        out[i] += 1.6 * s * sw * sw
    return out


# ---------------------------------------------------------------- the lid
def lid_open():
    L = 1.1
    out = [0.0] * n(L)
    rnd = random.Random(3)
    # the hinge: a squeak gliding up, a little rough
    ph = 0.0
    sq = []
    for i in range(n(0.42)):
        t = i / RATE
        f = 520 + 380 * t / 0.42 + 40 * math.sin(2 * math.pi * 23 * t)
        ph += 2 * math.pi * f / RATE
        a = math.sin(math.pi * min(1.0, t / 0.42)) * (0.7 + 0.3 * rnd.random())
        sq.append((math.sin(ph) + 0.4 * math.sin(2 * ph) + 0.2 * math.sin(3.1 * ph)) * a)
    mix(out, 0.02, sq, 0.35)
    # the lid falls back on its stop: a wooden knock
    knock = []
    for i in range(n(0.25)):
        t = i / RATE
        knock.append((math.sin(2 * math.pi * 140 * t) + 0.6 * rnd.uniform(-1, 1) * math.exp(-t / 0.01)) * env(t, 0.001, 0.05))
    mix(out, 0.42, knock, 0.9)
    # a little shimmer out of the box
    for k, f in enumerate((1568.0, 2093.0, 2637.0)):
        mix(out, 0.45 + 0.07 * k, bell(f, 0.5, 0.18, 0.6), 0.22)
    return out


# ---------------------------------------------------------------- the fanfare
def fanfare(rare):
    L = 2.2 if rare else 1.5
    out = [0.0] * n(L)
    notes = [523.25, 659.25, 783.99, 1046.5]                 # C E G C
    if rare:
        notes += [1318.5, 1568.0]                            # on up to E G
    for k, f in enumerate(notes):
        mix(out, 0.09 * k, bell(f, 0.9, 0.35, 0.9), 0.7)
        mix(out, 0.09 * k, bell(f * 2.0, 0.4, 0.12, 0.5), 0.18)
    # the held chord under it
    pad = []
    base = notes[-1] / 2
    for i in range(n(L - 0.3)):
        t = i / RATE
        a = min(1.0, t / 0.25) * math.exp(-t / (0.9 if rare else 0.6))
        s = tone(base, t) + 0.6 * tone(base * 1.2599, t) + 0.5 * tone(base * 1.4983, t)
        if rare:
            s += 0.5 * tone(base / 2, t) + 0.3 * tone(base * 0.5 * 1.004, t)     # a slow beating low
        pad.append(s * a)
    mix(out, 0.3, pad, 0.22)
    # sparkle
    rnd = random.Random(7 if rare else 5)
    for k in range(10 if rare else 6):
        mix(out, 0.35 + rnd.random() * (L - 0.8), bell(2600 + rnd.random() * 1800, 0.25, 0.06, 0.3), 0.12)
    return out


def tick():
    out = []
    for i in range(n(0.06)):
        t = i / RATE
        out.append((math.sin(2 * math.pi * 1250 * t) + 0.5 * math.sin(2 * math.pi * 2600 * t)) * env(t, 0.001, 0.012))
    return out


def main():
    h = hum()
    for band, level in enumerate((0.34, 0.21, 0.13, 0.075)):
        write('hum%d.wav' % band, h, level)
    write('open.wav', lid_open(), 0.85)
    write('found.wav', fanfare(False), 0.8)
    write('found_rare.wav', fanfare(True), 0.85)
    write('tick.wav', tick(), 0.3)


main()
