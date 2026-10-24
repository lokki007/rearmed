"""REARMED v4 gore / FX textures (procedural, our own) -> assets/gfx/rearmed/gore + assets/gfx/rearmed/fx.

Used by cgame/cg_gore.c through assets/scripts/rearmed_gore.shader (written by hand, not by this script).
The Blender worker's splat1-4 / screen1-3 / drip textures live next to these and are never overwritten.
"""
import math, pathlib, random
import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent / 'assets' / 'gfx' / 'rearmed'
GORE = ROOT / 'gore'
FX = ROOT / 'fx'
rng = random.Random(4242)
nrng = np.random.default_rng(4242)


def grid(w, h):
    yy, xx = np.mgrid[0:h, 0:w]
    return (xx + 0.5) / w, (yy + 0.5) / h


def save(folder, name, rgb, alpha):
    folder.mkdir(parents=True, exist_ok=True)
    h, w = alpha.shape
    out = np.zeros((h, w, 4), dtype=np.uint8)
    out[..., :3] = np.clip(rgb, 0, 255).astype(np.uint8)
    out[..., 3] = np.clip(alpha * 255 + 0.5, 0, 255).astype(np.uint8)
    Image.fromarray(out, 'RGBA').save(folder / name)


def value_noise(w, h, cells, seed, octaves=4):
    """Tileable-ish fractal value noise in 0..1."""
    r = np.random.default_rng(seed)
    total = np.zeros((h, w))
    amp, norm = 1.0, 0.0
    for o in range(octaves):
        c = cells * (2 ** o)
        g = r.random((c + 1, c + 1))
        g[-1, :] = g[0, :]
        g[:, -1] = g[:, 0]
        ys = np.linspace(0, c, h, endpoint=False)
        xs = np.linspace(0, c, w, endpoint=False)
        y0 = np.floor(ys).astype(int)
        x0 = np.floor(xs).astype(int)
        fy = (ys - y0)[:, None]
        fx = (xs - x0)[None, :]
        fy = fy * fy * (3 - 2 * fy)
        fx = fx * fx * (3 - 2 * fx)
        a = g[y0][:, x0]
        b = g[y0][:, x0 + 1]
        cc = g[y0 + 1][:, x0]
        d = g[y0 + 1][:, x0 + 1]
        total += amp * ((a * (1 - fx) + b * fx) * (1 - fy) + (cc * (1 - fx) + d * fx) * fy)
        norm += amp
        amp *= 0.5
    return total / norm


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


BLOOD_EDGE = np.array([175, 22, 16])
BLOOD_DEEP = np.array([105, 8, 6])

# ---------------------------------------------------------------- droplet
# t = 0 is the leading end: a round head near the front, a tail tapering back to t = 1
u, v = grid(32, 64)
x = (u - 0.5) * 2                   # -1..1 across
head_c = 0.26
r_head = np.hypot(x * 0.5, (v - head_c) * 1.0) / 0.2
tail_w = np.clip(1 - (v - head_c) / (1 - head_c), 0, 1) ** 1.7 * 0.3
tail = np.where(v > head_c, np.abs(x) / np.maximum(tail_w, 1e-3), 9)
d = np.minimum(r_head, tail)
alpha = smoothstep(1.0, 0.6, d) * (0.55 + 0.45 * np.clip(1 - (v - head_c) / (1 - head_c), 0, 1))
hl = np.exp(-((x + 0.25) ** 2 / 0.03 + (v - head_c + 0.05) ** 2 / 0.004))
rgb = np.full(d.shape + (3,), 200.0) * (0.85 + 0.15 * (1 - d.clip(0, 1)))[..., None] + 80 * hl[..., None]  # tinted by vertex color
save(GORE, 'drop.tga', rgb, alpha)

# ---------------------------------------------------------------- mist puff
u, v = grid(128, 128)
rho = np.hypot(u - 0.5, v - 0.5) / 0.5
n = value_noise(128, 128, 4, 11, 5)
alpha = np.clip((1 - rho) * 1.6, 0, 1) ** 1.4 * np.clip(n * 1.9 - 0.45, 0, 1)
rgb = np.full(u.shape + (3,), 170.0) * (0.7 + 0.5 * n)[..., None]  # tinted by vertex color
save(GORE, 'mist.tga', rgb, np.clip(alpha, 0, 1) * 0.95)

# ---------------------------------------------------------------- directional spatter atlas (2x2, 256 px cells)
# impact at u ~ 0.3 of the cell; droplets fly towards +u (s axis) and stretch along it


def spatter_cell(C, seed):
    r = random.Random(seed)
    uu, vv = grid(C, C)
    du, dv = uu - 0.3, vv - 0.5
    d = np.full_like(uu, -1.0)
    # the main hit: a wobbly blob stretched along +u
    tt = np.arctan2(dv, du * 0.7)
    rr = np.hypot(du * 0.7, dv)
    rad = np.full_like(rr, 0.12)
    for k in range(2, 9):
        rad += 0.12 * r.uniform(0.2, 0.55) / k * np.sin(k * tt + r.uniform(0, 6.3))
    d = np.maximum(d, (rad - rr) / 0.12)
    # streaks: long thin ellipses radiating forward from the hit
    for _ in range(r.randint(16, 26)):
        ang = r.gauss(0, 0.45)
        dist = r.uniform(0.08, 0.62)
        cx, cy = 0.3 + dist * math.cos(ang), 0.5 + dist * math.sin(ang)
        if not (0.04 < cx < 0.96 and 0.04 < cy < 0.96):
            continue
        size = r.uniform(0.012, 0.032) * (1.2 - 0.6 * dist)
        length = size * r.uniform(2.5, 6.0) * (0.6 + dist)
        p = (uu - cx) * math.cos(ang) + (vv - cy) * math.sin(ang)
        q = -(uu - cx) * math.sin(ang) + (vv - cy) * math.cos(ang)
        # teardrop: the head is at the far end
        wid = size * np.clip(0.35 + 0.65 * (p / length + 1) / 2, 0.2, 1.0)
        e = 1 - np.hypot(p / length, q / np.maximum(wid, 1e-4) * size / size)
        e = 1 - np.hypot(p / length, q / wid)
        d = np.maximum(d, e * 0.9)
    # fine mist of dots
    for _ in range(r.randint(40, 70)):
        ang = r.gauss(0, 0.7)
        dist = r.uniform(0.05, 0.62)
        cx, cy = 0.3 + dist * math.cos(ang), 0.5 + dist * math.sin(ang)
        if not (0.03 < cx < 0.97 and 0.03 < cy < 0.97):
            continue
        s = r.uniform(0.004, 0.011)
        d = np.maximum(d, 1 - np.hypot(uu - cx, vv - cy) / s)
    edge = np.minimum(np.minimum(uu, 1 - uu), np.minimum(vv, 1 - vv))
    d = np.minimum(d, (edge - 0.02) * 40)
    alpha = np.clip(d * 6, 0, 1)
    thick = np.clip(d, 0, 1) ** 0.5
    n = value_noise(C, C, 6, seed, 3)
    rgb = (BLOOD_EDGE + (BLOOD_DEEP - BLOOD_EDGE) * thick[..., None]) * (0.9 + 0.2 * n)[..., None]
    return rgb, alpha * (0.85 + 0.15 * thick)


C = 256
atlas_rgb = np.zeros((2 * C, 2 * C, 3))
atlas_a = np.zeros((2 * C, 2 * C))
for cell in range(4):
    rgb, a = spatter_cell(C, 100 + cell)
    y0, x0 = (cell >> 1) * C, (cell & 1) * C
    atlas_rgb[y0:y0 + C, x0:x0 + C] = rgb
    atlas_a[y0:y0 + C, x0:x0 + C] = a
save(GORE, 'spatter.tga', atlas_rgb, atlas_a)

# ---------------------------------------------------------------- drip streak (t = 0 top, the bulb at the bottom)
u, v = grid(32, 128)
x = (u - 0.5) * 2
wob = 0.03 * np.sin(v * 11.0) + 0.015 * np.sin(v * 29.0 + 1.0)
width = 0.13 + 0.09 * v + 0.12 * np.exp(-((v - 0.03) / 0.04) ** 2)
line = 1 - np.abs(x - wob) / width
bulb = 1 - np.hypot((x - wob[-1:, :]) / 0.3, (v - 0.93) / 0.045)
d = np.maximum(line * (v < 0.93), bulb)
alpha = np.clip(d * 3, 0, 1) * smoothstep(0.0, 0.04, v)
rgb = BLOOD_EDGE + (BLOOD_DEEP - BLOOD_EDGE) * np.clip(d, 0, 1)[..., None]
save(GORE, 'streak.tga', rgb, alpha)

# ---------------------------------------------------------------- boot print (toe at t = 0)
u, v = grid(64, 128)
x = (u - 0.5) * 2.6
sole = 1 - np.hypot(x / 0.72, (v - 0.33) / 0.3)
heel = 1 - np.hypot(x / 0.62, (v - 0.8) / 0.15)
d = np.maximum(sole, heel)
tread = 0.6 + 0.4 * (value_noise(64, 128, 16, 78, 1) > 0.4)
n = value_noise(64, 128, 4, 77, 4)
alpha = np.clip(d * 5, 0, 1) * (0.55 + 0.45 * tread) * np.clip(0.55 + n * 0.9, 0, 1)
rgb = BLOOD_DEEP[None, None, :] * np.ones_like(u)[..., None] * (0.9 + 0.3 * n)[..., None]
save(GORE, 'footprint.tga', rgb, alpha)

# ---------------------------------------------------------------- charred skin (multiplied over the body: dark = burnt)
u, v = grid(128, 128)
n = value_noise(128, 128, 4, 5, 5)
n2 = value_noise(128, 128, 8, 6, 3)
burn = 0.13 + 0.22 * n ** 1.5 + 0.08 * (n2 > 0.62)
rgb = np.stack([burn * 255 * 1.05, burn * 255 * 0.95, burn * 255 * 0.85], -1)
save(GORE, 'charred.tga', rgb, np.ones_like(u))

# glowing cracks (additive): thin bright lines along the noise iso-contours
n = value_noise(128, 128, 3, 21, 4)
cracks = np.exp(-((np.abs(np.sin(n * 26.0))) / 0.09) ** 2) * smoothstep(0.35, 0.65, value_noise(128, 128, 3, 22, 3))
rgb = np.stack([cracks * 255, cracks * 120, cracks * 25], -1)
save(GORE, 'embers.tga', rgb, np.ones_like(u))

# ---------------------------------------------------------------- screen blood (fallback when the Blender screen1-3 are missing)
# 2x2 atlas: glossy splats with runs going down (t increases downwards)


def screen_cell(C, seed):
    r = random.Random(seed)
    uu, vv = grid(C, C)
    d = np.full_like(uu, -1.0)
    cx, cy = 0.5, 0.36
    tt = np.arctan2(vv - cy, uu - cx)
    rr = np.hypot(uu - cx, vv - cy)
    rad = np.full_like(rr, 0.18)
    for k in range(2, 10):
        rad += 0.18 * r.uniform(0.2, 0.6) / k * np.sin(k * tt + r.uniform(0, 6.3))
    d = np.maximum(d, (rad - rr) / 0.18)
    for _ in range(r.randint(2, 4)):          # runs
        rx = cx + r.uniform(-0.14, 0.14)
        length = r.uniform(0.25, 0.55)
        w = r.uniform(0.012, 0.03)
        seg = (vv > cy) & (vv < cy + length)
        line = 1 - np.abs(uu - rx) / (w * (1 - 0.4 * (vv - cy) / length))
        d = np.maximum(d, np.where(seg, line, -1))
        d = np.maximum(d, 1 - np.hypot(uu - rx, vv - cy - length) / (w * 1.5))
    for _ in range(r.randint(10, 20)):        # droplets
        a = r.uniform(0, 2 * math.pi)
        dist = r.uniform(0.2, 0.44)
        s = r.uniform(0.006, 0.02)
        d = np.maximum(d, 1 - np.hypot(uu - cx - dist * math.cos(a), vv - cy - dist * math.sin(a) * 0.9) / s)
    edge = np.minimum(np.minimum(uu, 1 - uu), np.minimum(vv, 1 - vv))
    d = np.minimum(d, (edge - 0.02) * 30)
    alpha = np.clip(d * 5, 0, 1)
    thick = np.clip(d, 0, 1) ** 0.6
    gloss = np.exp(-(((uu - cx + 0.05) ** 2 + (vv - cy + 0.06) ** 2) / 0.003))
    rgb = (np.array([150, 10, 8]) + (np.array([70, 3, 3]) - np.array([150, 10, 8])) * thick[..., None]) \
        + 90 * gloss[..., None] * (d > 0.3)[..., None]
    return rgb, alpha * (0.75 + 0.25 * thick)


C = 256
atlas_rgb = np.zeros((2 * C, 2 * C, 3))
atlas_a = np.zeros((2 * C, 2 * C))
for cell in range(4):
    rgb, a = screen_cell(C, 300 + cell)
    y0, x0 = (cell >> 1) * C, (cell & 1) * C
    atlas_rgb[y0:y0 + C, x0:x0 + C] = rgb
    atlas_a[y0:y0 + C, x0:x0 + C] = a
save(GORE, 'visor.tga', atlas_rgb, atlas_a)

# ---------------------------------------------------------------- screen overlays
# vignette: alpha grows towards the edges (white, tinted by the color the code sets)
u, v = grid(256, 128)
rho = np.hypot((u - 0.5) * 2, (v - 0.5) * 2) / math.sqrt(2)
alpha = smoothstep(0.35, 1.0, rho) ** 1.5
save(GORE, 'vignette.tga', np.full(u.shape + (3,), 255.0), alpha)

# speed lines: thin radial streaks, nothing in the middle
for k in range(2):
    W, H = 512, 256
    u, v = grid(W, H)
    x, y = (u - 0.5) * 2 * (W / H), (v - 0.5) * 2
    ang = np.arctan2(y, x)
    rad = np.hypot(x, y)
    a = np.zeros_like(u)
    r = random.Random(900 + k)
    for _ in range(70):
        th = r.uniform(-math.pi, math.pi)
        wid = r.uniform(0.002, 0.007)
        start = r.uniform(0.55, 1.1)
        dth = np.angle(np.exp(1j * (ang - th)))
        line = np.exp(-(dth / wid) ** 2) * smoothstep(start, start + 0.5, rad)
        a = np.maximum(a, line * r.uniform(0.4, 1.0))
    save(GORE, 'speed%d.tga' % (k + 1), np.full(u.shape + (3,), 255.0), np.clip(a, 0, 1))

# ---------------------------------------------------------------- fire and smoke puffs
for k in range(2):
    u, v = grid(128, 128)
    rho = np.hypot(u - 0.5, v - 0.5) / 0.5
    n = value_noise(128, 128, 3, 40 + k, 5)
    body = np.clip(1.15 - rho - 1.1 * (n - 0.5), 0, 1)
    core = body ** 1.6 * (0.55 + 0.6 * n)
    i = np.clip(core * 1.4, 0, 1) * 255
    save(FX, 'fireball%d.tga' % (k + 1), np.stack([i, i, i], -1), np.clip(core * 1.5, 0, 1))

u, v = grid(128, 128)
rho = np.hypot(u - 0.5, v - 0.5) / 0.5
n = value_noise(128, 128, 3, 60, 5)
alpha = np.clip((1 - rho) * 1.5, 0, 1) ** 1.2 * np.clip(0.25 + n * 1.1, 0, 1)
shade = 170 + 85 * n
save(FX, 'smoke.tga', np.stack([shade, shade, shade], -1), alpha * 0.9)

print('gore/fx textures written to', GORE, FX)
