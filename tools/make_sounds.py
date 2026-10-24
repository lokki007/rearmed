"""Synthesize the mod's own sound effects into assets/sound/rearmed/ (22 kHz, 16-bit mono WAV)."""
import math, random, struct, wave, pathlib

RATE = 22050
OUT = pathlib.Path(__file__).resolve().parent.parent / 'assets' / 'sound' / 'rearmed'


def write(name, samples):
    OUT.mkdir(parents=True, exist_ok=True)
    peak = max(1e-9, max(abs(s) for s in samples))
    with wave.open(str(OUT / name), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b''.join(struct.pack('<h', int(s / peak * 30000)) for s in samples))


def env(t, attack, decay):
    return min(1.0, t / attack) * math.exp(-t / decay) if attack > 0 else math.exp(-t / decay)


def headshot():
    # bright metallic ping (inharmonic partials) over a short noise crack
    rnd = random.Random(7)
    out = []
    for i in range(int(RATE * 0.45)):
        t = i / RATE
        ping = sum(a * math.sin(2 * math.pi * f * t) for f, a in ((2150, 1.0), (3410, 0.6), (5230, 0.35), (1180, 0.4)))
        crack = rnd.uniform(-1, 1) * env(t, 0.0005, 0.012)
        out.append(0.55 * ping * env(t, 0.001, 0.09) + 0.8 * crack)
    return out


def whoosh(duration, f0, f1, seed):
    # band-limited noise sweep, used for dash / double jump
    rnd = random.Random(seed)
    out, lp, bp = [], 0.0, 0.0
    n = int(RATE * duration)
    for i in range(n):
        t = i / RATE
        f = f0 + (f1 - f0) * (i / n)
        # state-variable filter as a simple resonant band-pass
        k = 2 * math.sin(math.pi * f / RATE)
        x = rnd.uniform(-1, 1)
        lp += k * bp
        hp = x - lp - 0.35 * bp
        bp += k * hp
        out.append(bp * math.sin(math.pi * min(1.0, t / duration)) ** 0.6)
    return out


def click_boost():
    # double jump: soft thump + short upward whoosh
    w = whoosh(0.28, 500, 2200, 3)
    return [s + 0.9 * math.sin(2 * math.pi * 90 * (i / RATE)) * env(i / RATE, 0.002, 0.03) for i, s in enumerate(w)]


def tick(freq, length=0.07):
    # short, clean hitmarker click: sine blip with a noise transient
    rnd = random.Random(int(freq))
    out = []
    for i in range(int(RATE * length)):
        t = i / RATE
        s = math.sin(2 * math.pi * freq * t) + 0.35 * math.sin(2 * math.pi * freq * 2.01 * t)
        out.append(s * env(t, 0.0008, 0.018) + 0.3 * rnd.uniform(-1, 1) * env(t, 0.0002, 0.002))
    return out


def kill_confirm():
    # two quick descending tones with body: "tk-THUNK"
    out = []
    for i in range(int(RATE * 0.32)):
        t = i / RATE
        a = math.sin(2 * math.pi * 1500 * t) * env(t, 0.001, 0.02)
        t2 = t - 0.06
        b = 0.0
        if t2 > 0:
            f = 420 - 180 * min(1.0, t2 / 0.2)
            b = (math.sin(2 * math.pi * f * t2) + 0.4 * math.sin(2 * math.pi * f * 2 * t2)) * env(t2, 0.002, 0.08)
        out.append(0.7 * a + b)
    return out


def airshot():
    # bright rising arpeggio sting
    out = []
    notes = (880, 1109, 1319, 1760)
    for i in range(int(RATE * 0.55)):
        t = i / RATE
        s = 0.0
        for k, f in enumerate(notes):
            tk = t - k * 0.06
            if tk > 0:
                s += (math.sin(2 * math.pi * f * tk) + 0.3 * math.sin(2 * math.pi * f * 3 * tk)) * env(tk, 0.003, 0.12 + 0.05 * k)
        out.append(s)
    return out


def singularity_loop():
    # seamless 1 s loop: low detuned drone with a slow wobble (all frequencies whole Hz)
    out = []
    for i in range(RATE):
        t = i / RATE
        wob = 1 + 0.35 * math.sin(2 * math.pi * 3 * t)
        s = (math.sin(2 * math.pi * 55 * t) + 0.7 * math.sin(2 * math.pi * 83 * t)
             + 0.35 * math.sin(2 * math.pi * 220 * t + 2 * math.sin(2 * math.pi * 5 * t)))
        out.append(s * wob)
    return out


def bfg_zap():
    # short buzzy green laser zap
    rnd = random.Random(11)
    out = []
    for i in range(int(RATE * 0.18)):
        t = i / RATE
        f = 1800 - 1200 * t / 0.18
        saw = 2 * ((f * t) % 1.0) - 1
        out.append((0.8 * saw + 0.3 * rnd.uniform(-1, 1)) * env(t, 0.002, 0.05))
    return out


def chord(freqs, length, decay, attack=0.004, detune=0.0, stagger=0.0):
    # layered saw-ish tones: brassy announcer sting
    out = []
    for i in range(int(RATE * length)):
        t = i / RATE
        s = 0.0
        for k, f in enumerate(freqs):
            tk = t - k * stagger
            if tk <= 0:
                continue
            for d in (-detune, detune):
                ph = (f * (1 + d)) * tk
                s += (2 * (ph % 1.0) - 1) * 0.5 * env(tk, attack, decay)
        out.append(s)
    return out


def siren(length=0.9):
    # two-tone alarm for a new mutator
    out = []
    for i in range(int(RATE * length)):
        t = i / RATE
        f = 660 if int(t / 0.15) % 2 == 0 else 880
        out.append((math.sin(2 * math.pi * f * t) + 0.5 * math.sin(2 * math.pi * f * 2 * t)) * env(t, 0.01, 0.5))
    return out


def armor_break():
    # glassy crack: noise burst through bright inharmonic ringing
    rnd = random.Random(11)
    out = []
    for i in range(int(RATE * 0.5)):
        t = i / RATE
        ring = sum(a * math.sin(2 * math.pi * f * t) for f, a in ((2890, 1.0), (4120, 0.7), (6370, 0.45), (1730, 0.5)))
        crack = rnd.uniform(-1, 1) * env(t, 0.0003, 0.03)
        out.append(0.35 * ring * env(t, 0.001, 0.12) + crack)
    return out


def beeps(freq, count, gap, length):
    # urgent short beeps
    out = []
    for i in range(int(RATE * length)):
        t = i / RATE
        k = int(t / gap)
        tk = t - k * gap
        on = k < count and tk < gap * 0.55
        out.append(math.sin(2 * math.pi * freq * t) * (env(tk, 0.002, 0.05) if on else 0.0))
    return out


def crackle_loop(length=1.0):
    # fire: low roar plus random pops, loops cleanly (fade the pops at the seam)
    rnd = random.Random(21)
    out, lp = [], 0.0
    for i in range(int(RATE * length)):
        n = rnd.uniform(-1, 1)
        lp += (n - lp) * 0.05
        pop = rnd.uniform(-1, 1) * (1.0 if rnd.random() < 0.004 else 0.0)
        out.append(0.8 * lp + 0.6 * pop)
    return out


def thunder():
    # sharp crack into a long rumble
    rnd = random.Random(33)
    out, lp = [], 0.0
    for i in range(int(RATE * 1.8)):
        t = i / RATE
        n = rnd.uniform(-1, 1)
        lp += (n - lp) * (0.35 if t < 0.05 else 0.03)
        out.append((n * env(t, 0.001, 0.04) * 1.2) + lp * 3.0 * env(t, 0.02, 0.6))
    return out


def noise_burst(length, cut, decay, seed):
    # filtered noise with an envelope (steam hiss, wind)
    rnd = random.Random(seed)
    out, lp = [], 0.0
    for i in range(int(RATE * length)):
        t = i / RATE
        lp += (rnd.uniform(-1, 1) - lp) * cut
        out.append(lp * env(t, 0.01, decay))
    return out


def zap():
    # electric buzz
    rnd = random.Random(44)
    out = []
    for i in range(int(RATE * 0.25)):
        t = i / RATE
        s = (1 if math.sin(2 * math.pi * 110 * t) > 0 else -1) * 0.5 + rnd.uniform(-1, 1) * 0.5
        out.append(s * env(t, 0.002, 0.08))
    return out


def rising_blip():
    # charge full: quick rising two-tone
    out = []
    for i in range(int(RATE * 0.22)):
        t = i / RATE
        f = 900 + 2200 * t
        out.append(math.sin(2 * math.pi * f * t) * env(t, 0.003, 0.09))
    return out


if __name__ == '__main__':
    write('fire_loop.wav', crackle_loop())
    write('thunder.wav', thunder())
    write('wind.wav', noise_burst(0.5, 0.08, 0.18, 55))
    write('steam.wav', noise_burst(0.7, 0.6, 0.25, 66))
    write('zap.wav', zap())
    write('charge_ready.wav', rising_blip())
    write('ann_medal.wav', chord((784, 1175), 0.5, 0.12, stagger=0.06))
    write('ann_bounty.wav', chord((110, 165, 220, 330), 1.2, 0.5, attack=0.01, detune=0.003, stagger=0.09))
    write('ann_warn.wav', beeps(1320, 3, 0.11, 0.4))
    write('armorbreak.wav', armor_break())
    write('wallrun.wav', [s * 0.6 for s in whoosh(0.3, 400, 1400, 9)])
    write('hit_low2.wav', tick(2050))
    write('hit_mid2.wav', tick(1620))
    write('hit_high2.wav', tick(1060, 0.09))
    write('ann_streak.wav', chord((196, 247, 294, 392), 1.1, 0.35, detune=0.004, stagger=0.07))
    write('ann_mutator.wav', siren())
    write('ann_juggernaut.wav', chord((55, 82.5, 110), 1.6, 0.6, attack=0.02, detune=0.006))
    write('ann_gungame.wav', chord((523, 659, 784, 1047), 0.8, 0.18, stagger=0.05))
    write('ann_info.wav', tick(1000, 0.12))
    write('hit_low.wav', tick(1900))
    write('hit_mid.wav', tick(1500))
    write('hit_high.wav', tick(1150, 0.09))
    write('kill.wav', kill_confirm())
    write('airshot.wav', airshot())
    write('singularity.wav', singularity_loop())
    write('bfgzap.wav', bfg_zap())
    write('headshot.wav', headshot())
    write('dash.wav', whoosh(0.35, 2600, 500, 1))
    write('doublejump.wav', click_boost())
    write('walljump.wav', [s * 0.8 for s in whoosh(0.22, 900, 3000, 5)])
    print('wrote', sorted(p.name for p in OUT.iterdir()))
