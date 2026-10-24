"""Synthesize the physics sounds (props, debris, wall splats) into assets/sound/rearmed/physics/.

All procedural, our own: 22 kHz, 16-bit mono WAV. Run: python tools/make_sounds_physics.py
"""
import math, random, struct, wave, pathlib

RATE = 22050
OUT = pathlib.Path(__file__).resolve().parent.parent / 'assets' / 'sound' / 'rearmed' / 'physics'


def write(name, samples, level=0.92):
    OUT.mkdir(parents=True, exist_ok=True)
    peak = max(1e-9, max(abs(s) for s in samples))
    with wave.open(str(OUT / name), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b''.join(struct.pack('<h', int(s / peak * 32767 * level)) for s in samples))


def env(t, attack, decay):
    return min(1.0, t / attack) * math.exp(-t / decay) if attack > 0 else math.exp(-t / decay)


class SVF:
    """state variable filter: band-pass (bp) / low-pass (lp) / high-pass (hp) of the input"""
    def __init__(self, freq, damp=0.35):
        self.lp = self.bp = 0.0
        self.set(freq, damp)

    def set(self, freq, damp=None):
        self.k = 2 * math.sin(math.pi * min(freq, RATE / 6) / RATE)
        if damp is not None:
            self.damp = damp

    def run(self, x):
        self.lp += self.k * self.bp
        hp = x - self.lp - self.damp * self.bp
        self.bp += self.k * hp
        return self.lp, self.bp, hp


def silence(seconds):
    return [0.0] * int(RATE * seconds)


def mix(dst, src, at=0.0, gain=1.0):
    o = int(at * RATE)
    if o + len(src) > len(dst):
        dst.extend([0.0] * (o + len(src) - len(dst)))
    for i, s in enumerate(src):
        dst[o + i] += s * gain
    return dst


def partials(freqs, length, decay, amp=1.0, seed=0):
    rnd = random.Random(seed)
    ph = [rnd.random() * 6.28 for _ in freqs]
    out = []
    for i in range(int(RATE * length)):
        t = i / RATE
        s = sum(a * math.sin(2 * math.pi * f * t + p) * math.exp(-t / (decay * d))
                for (f, a, d), p in zip(freqs, ph))
        out.append(s * amp)
    return out


def noise(length, seed, cut=None, band=None, damp=0.4, attack=0.0005, decay=0.1):
    rnd = random.Random(seed)
    f = SVF(band or cut or 1000, damp)
    out = []
    for i in range(int(RATE * length)):
        t = i / RATE
        lp, bp, hp = f.run(rnd.uniform(-1, 1))
        s = bp if band else (lp if cut else rnd.uniform(-1, 1))
        out.append(s * env(t, attack, decay))
    return out


def thump(f0, f1, length, decay, attack=0.002):
    out, ph = [], 0.0
    for i in range(int(RATE * length)):
        t = i / RATE
        f = f1 + (f0 - f1) * math.exp(-t / (length * 0.25))
        ph += 2 * math.pi * f / RATE
        out.append(math.sin(ph) * env(t, attack, decay))
    return out


def rattle(start, length, count, seed, band=3000, gain=0.25):
    """little debris clicks scattered in time"""
    rnd = random.Random(seed)
    out = silence(start + length + 0.1)
    for _ in range(count):
        at = start + rnd.random() ** 1.6 * length
        mix(out, noise(0.03, rnd.randint(0, 9999), band=band * rnd.uniform(0.6, 1.4), decay=0.008),
            at, gain * rnd.uniform(0.3, 1.0) * (1.0 - (at - start) / (length + 0.2)))
    return out


def loopify(samples, fade):
    """crossfade the tail into the head so the loop has no seam"""
    n = int(RATE * fade)
    body = samples[:len(samples) - n]
    for i in range(n):
        a = i / n
        body[i] = body[i] * a + samples[len(samples) - n + i] * (1 - a)
    return body


# ---------------------------------------------------------------------------------------------

def barrel_boom():
    out = silence(2.2)
    mix(out, noise(0.25, 1, decay=0.035), 0, 1.3)                       # the crack
    mix(out, thump(95, 32, 1.2, 0.30), 0, 1.4)                          # the body blow
    mix(out, noise(2.0, 2, cut=160, attack=0.01, decay=0.55), 0, 3.2)   # rumble
    mix(out, noise(1.0, 3, cut=700, attack=0.004, decay=0.16), 0, 1.6)  # fireball roar
    mix(out, partials([(430, 1, 1), (1135, .6, .7), (1870, .45, .5), (2710, .3, .35)], 1.2, 0.35, 0.22, 4), 0.004)
    mix(out, rattle(0.25, 1.2, 22, 5, band=2600, gain=0.35), 0)
    return out


def tank_boom():
    out = silence(1.7)
    mix(out, noise(0.2, 11, decay=0.025), 0, 1.4)
    mix(out, thump(130, 45, 0.9, 0.2), 0, 1.1)
    mix(out, noise(1.5, 12, cut=240, attack=0.008, decay=0.4), 0, 2.6)
    mix(out, noise(0.6, 13, band=3200, damp=0.8, attack=0.002, decay=0.12), 0, 0.9)    # gas flash hiss
    mix(out, partials([(610, 1, 1), (1580, .7, .6), (2440, .5, .45), (3900, .3, .3)], 1.0, 0.3, 0.25, 14), 0.004)
    mix(out, rattle(0.2, 0.9, 16, 15, band=3400, gain=0.3), 0)
    return out


def crate_break():
    rnd = random.Random(21)
    out = silence(0.9)
    mix(out, thump(210, 120, 0.25, 0.05), 0, 0.9)                      # the knock
    for k in range(7):                                                  # cracking fibres
        at = rnd.uniform(0.0, 0.16)
        mix(out, noise(0.08, 100 + k, band=rnd.uniform(700, 1700), damp=0.25, decay=0.018), at, rnd.uniform(0.6, 1.2))
    mix(out, rattle(0.12, 0.6, 18, 22, band=1400, gain=0.4), 0)         # splinters landing
    return out


def tank_rupture():
    out = silence(0.8)
    mix(out, partials([(520, 1, 1), (1310, .8, .6), (2270, .5, .4)], 0.5, 0.18, 0.8, 31), 0)
    mix(out, noise(0.8, 32, band=3800, damp=0.9, attack=0.02, decay=0.5), 0.01, 1.1)
    return out


def gas_loop():
    rnd = random.Random(41)
    hiss = SVF(4200, 0.9)
    whistle = []
    ph = 0.0
    for i in range(int(RATE * 1.25)):
        t = i / RATE
        f = 1750 + 90 * math.sin(2 * math.pi * 5 * t) + 40 * math.sin(2 * math.pi * 13 * t)
        ph += 2 * math.pi * f / RATE
        lp, bp, hp = hiss.run(rnd.uniform(-1, 1))
        whistle.append(0.9 * bp + 0.5 * hp + 0.22 * math.sin(ph) * (0.7 + 0.3 * math.sin(2 * math.pi * 3 * t)))
    return loopify(whistle, 0.25)


def burn_loop():
    rnd = random.Random(51)
    roar = SVF(260, 0.6)
    fuse = SVF(5200, 0.7)
    out = []
    for i in range(int(RATE * 1.3)):
        t = i / RATE
        n = rnd.uniform(-1, 1)
        lp, bp, hp = roar.run(n)
        _, fb, _ = fuse.run(rnd.uniform(-1, 1))
        pop = rnd.uniform(-1, 1) if rnd.random() < 0.006 else 0.0
        out.append(1.3 * lp + 0.35 * fb * (0.75 + 0.25 * math.sin(2 * math.pi * 7 * t)) + 0.8 * pop)
    return loopify(out, 0.3)


def ignite():
    out = silence(0.9)
    mix(out, noise(0.9, 61, cut=300, attack=0.12, decay=0.25), 0, 2.5)
    mix(out, noise(0.5, 62, band=1800, damp=0.8, attack=0.06, decay=0.12), 0, 0.5)
    return out


def hit_metal(seed, base):
    out = silence(0.7)
    mix(out, thump(base * 1.4, base, 0.4, 0.09), 0, 1.0)
    mix(out, partials([(base * 2.8, 1, 1), (base * 5.1, .7, .7), (base * 8.3, .45, .45)], 0.7, 0.2, 0.35, seed), 0)
    mix(out, noise(0.05, seed + 1, band=2600, decay=0.006), 0, 0.6)
    return out


def hit_wood():
    out = silence(0.3)
    mix(out, thump(260, 170, 0.2, 0.035), 0, 1.0)
    mix(out, noise(0.12, 71, band=900, damp=0.2, decay=0.03), 0, 0.8)
    return out


def splat(seed):
    rnd = random.Random(seed)
    out = silence(0.8)
    mix(out, thump(110, 45, 0.4, 0.09), 0, 1.3)                         # body blow
    mix(out, noise(0.25, seed + 1, decay=0.012), 0, 0.7)                # the crunch
    for k in range(5):                                                  # bones
        mix(out, noise(0.03, seed + 10 + k, band=rnd.uniform(1500, 3000), damp=0.3, decay=0.006),
            rnd.uniform(0.005, 0.06), rnd.uniform(0.4, 0.8))
    # wet squelch: band-passed noise with a fluttering envelope and falling centre
    f = SVF(1100, 0.25)
    wet = []
    for i in range(int(RATE * 0.45)):
        t = i / RATE
        f.set(1100 - 700 * min(1.0, t / 0.4))
        lp, bp, hp = f.run(rnd.uniform(-1, 1))
        flutter = 0.6 + 0.4 * math.sin(2 * math.pi * (23 + 9 * t) * t)
        wet.append(bp * flutter * env(t, 0.004, 0.12))
    mix(out, wet, 0.01, 1.4)
    return out


def debris_rock():
    out = silence(0.12)
    mix(out, noise(0.08, 81, band=2600, damp=0.35, decay=0.01), 0, 1.0)
    mix(out, thump(900, 700, 0.05, 0.008), 0, 0.5)
    return out


def debris_metal():
    return partials([(2420, 1, 1), (3970, .7, .7), (5610, .5, .5)], 0.3, 0.07, 1.0, 91) + silence(0.02)


def debris_wood():
    out = silence(0.15)
    mix(out, thump(560, 460, 0.1, 0.02), 0, 1.0)
    mix(out, noise(0.06, 95, band=1300, damp=0.3, decay=0.01), 0, 0.5)
    return out


if __name__ == '__main__':
    write('barrel_boom.wav', barrel_boom())
    write('tank_boom.wav', tank_boom())
    write('crate_break.wav', crate_break())
    write('tank_rupture.wav', tank_rupture())
    write('gas_loop.wav', gas_loop(), 0.6)
    write('burn_loop.wav', burn_loop(), 0.6)
    write('ignite.wav', ignite())
    write('hit_metal1.wav', hit_metal(101, 118), 0.8)
    write('hit_metal2.wav', hit_metal(202, 96), 0.8)
    write('hit_wood.wav', hit_wood(), 0.8)
    write('splat1.wav', splat(301))
    write('splat2.wav', splat(402))
    write('debris_rock.wav', debris_rock(), 0.55)
    write('debris_metal.wav', debris_metal(), 0.5)
    write('debris_wood.wav', debris_wood(), 0.55)
    print('wrote', sorted(p.name for p in OUT.iterdir()))
