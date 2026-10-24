"""REARMED v4 2D textures (system Python: numpy + PIL). Re-runnable, deterministic (fixed seeds).

Writes:
  assets/gfx/rearmed/gore/splat1..4.tga    wall/floor blood decals (RGBA)
  assets/gfx/rearmed/gore/drip.tga         vertical drip streaks (RGBA)
  assets/gfx/rearmed/gore/screen1..3.tga   on-screen blood (RGBA, glossy)
  assets/models/rearmed/gibs/wetenv.tga    environment map for the wet sheen on gibs
  assets/models/rearmed/debris/rock.tga, metal.tga, wood.tga   tileable 128 px debris textures (match the props)
"""
import math
import pathlib

import sys

import numpy as np
from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
ROOT = HERE.parent.parent
ASSETS = ROOT / 'assets'
GORE = ASSETS / 'gfx' / 'rearmed' / 'gore'
BUILD = HERE / 'build'


# ---------------------------------------------------------------- helpers
def save_tga(path, rgba):
    path.parent.mkdir(parents=True, exist_ok=True)
    a = np.clip(rgba * 255 + 0.5, 0, 255).astype(np.uint8)
    mode = 'RGBA' if a.shape[2] == 4 else 'RGB'
    Image.fromarray(a, mode).save(path)
    print('wrote', path.relative_to(ROOT))


def grid(w, h):
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    return (x + 0.5) / w, (y + 0.5) / h


def fbm(shape, rng, beta=2.0, lo=2.0):
    """Tileable fractal noise in [0,1] (spectral synthesis)."""
    h, w = shape
    wn = rng.standard_normal((h, w))
    fy = np.fft.fftfreq(h)[:, None] * h
    fx = np.fft.fftfreq(w)[None, :] * w
    f = np.sqrt(fx * fx + fy * fy)
    f[0, 0] = 1
    filt = f ** (-beta / 2)
    filt[0, 0] = 0
    n = np.real(np.fft.ifft2(np.fft.fft2(wn) * filt))
    n = (n - n.min()) / (n.max() - n.min() + 1e-9)
    return n.astype(np.float32)


def blur(img, r):
    """Cheap gaussian-ish blur via FFT (wraps; fine for padded decals)."""
    h, w = img.shape
    fy = np.fft.fftfreq(h)[:, None]
    fx = np.fft.fftfreq(w)[None, :]
    g = np.exp(-(fx * fx + fy * fy) * (2 * math.pi * r) ** 2 / 2)
    return np.real(np.fft.ifft2(np.fft.fft2(img) * g)).astype(np.float32)


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def aniso_blob(x, y, cx, cy, r, stretch=1.0, ang=0.0):
    """Metaball contribution: gaussian elongated `stretch` times along angle `ang`."""
    dx, dy = x - cx, y - cy
    ca, sa = math.cos(ang), math.sin(ang)
    u = (dx * ca + dy * sa) / stretch
    v = -dx * sa + dy * ca
    return np.exp(-(u * u + v * v) / (r * r))


def shade_liquid(field, thr, rng, *, light=(-0.45, -0.6, 0.66), base_thin=(0.64, 0.045, 0.04),
                 base_thick=(0.34, 0.012, 0.014), gloss=0.9, spec_pow=60, alpha_thin=0.78,
                 rim=0.45, edge_soft=0.012, height_gain=6.0):
    """Turn a metaball field into a glossy blood RGBA image."""
    cov = smoothstep(thr - edge_soft, thr + edge_soft, field)
    lo = fbm(field.shape, rng, beta=3.2)
    thick = smoothstep(thr, thr + 1.8, field * (0.75 + 0.5 * lo)) ** 0.8
    # the height used for the normals: rounded at the edges, flat-ish on top
    hgt = blur(smoothstep(thr, thr + 0.35, field) * (0.8 + 0.2 * thick) + 0.04 * blur(fbm(field.shape, rng, beta=2.4), 2.0), 1.5) * 0.35
    gy, gx = np.gradient(hgt)
    k = 70.0
    n = np.stack([-gx * k, -gy * k, np.ones_like(gx)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    L = np.array(light, np.float32)
    L /= np.linalg.norm(L)
    H = L + np.array([0, 0, 1.0], np.float32)
    H /= np.linalg.norm(H)
    diff = np.clip((n * L).sum(-1), 0, 1)
    spec = np.clip((n * H).sum(-1), 0, 1) ** spec_pow
    spec2 = np.clip((n * H).sum(-1), 0, 1) ** (spec_pow / 6)
    tb, tk = np.array(base_thin), np.array(base_thick)
    col = tb[None, None] * (1 - thick[..., None]) + tk[None, None] * thick[..., None]
    grain = fbm(field.shape, rng, beta=1.6)
    col *= (0.93 + 0.14 * blur(grain, 1.5))[..., None]
    # dried darker ring at the rim (coffee ring)
    ring = np.clip(cov - smoothstep(thr, thr + 0.09, field), 0, 1)
    col *= (1 - rim * ring)[..., None]
    col *= (0.85 + 0.25 * diff)[..., None]
    hl = gloss * (spec * 1.0 + spec2 * 0.12) * cov
    col = col + hl[..., None] * np.array([1.0, 0.82, 0.8])
    alpha = cov * (alpha_thin + (1 - alpha_thin) * thick)
    alpha = np.clip(alpha + hl * 0.5, 0, 1) * cov
    return np.concatenate([np.clip(col, 0, 1), alpha[..., None]], -1)


# ---------------------------------------------------------------- blood splats
def splat(seed, size=512, rays=18, core=1.0, satellites=90, tadpoles=True, flow=None):
    rng = np.random.default_rng(seed)
    x, y = grid(size, size)
    x -= 0.5
    y -= 0.5
    f = np.zeros((size, size), np.float32)
    # central mass: several blobs
    nb = rng.integers(5, 9)
    for _ in range(nb):
        a = rng.uniform(0, 2 * math.pi)
        d = rng.uniform(0, 0.07) * core
        f += aniso_blob(x, y, math.cos(a) * d, math.sin(a) * d, rng.uniform(0.06, 0.11) * core,
                        rng.uniform(1, 1.6), rng.uniform(0, math.pi)) * rng.uniform(0.8, 1.2)
    # radial spatter: arms of elongated droplets, shrinking outwards
    for i in range(rays):
        a = rng.uniform(0, 2 * math.pi)
        dist = rng.uniform(0.10, 0.17) * core
        r = rng.uniform(0.022, 0.04) * core
        steps = rng.integers(2, 6)
        for s in range(steps):
            if dist > 0.43:
                break
            f += aniso_blob(x, y, math.cos(a) * dist, math.sin(a) * dist, r, rng.uniform(1.6, 3.2), a) * 1.0
            dist += r * rng.uniform(1.6, 3.2)
            r *= rng.uniform(0.55, 0.8)
            a += rng.normal(0, 0.05)
        if tadpoles and rng.random() < 0.6 and dist < 0.40:
            # detached tadpole: round head, tail pointing back at the centre
            hr = rng.uniform(0.008, 0.016)
            dd = dist + rng.uniform(0.02, 0.06)
            dd = min(dd, 0.43)
            f += aniso_blob(x, y, math.cos(a) * dd, math.sin(a) * dd, hr, 1.0) * 1.1
            f += aniso_blob(x, y, math.cos(a) * (dd - hr * 2.2), math.sin(a) * (dd - hr * 2.2), hr * 0.45, 4.0, a) * 0.9
    # satellite droplets
    for _ in range(satellites):
        a = rng.uniform(0, 2 * math.pi)
        d = 0.12 + abs(rng.normal(0, 0.12))
        if d > 0.44:
            continue
        r = rng.uniform(0.003, 0.010) * (1.3 - d)
        f += aniso_blob(x, y, math.cos(a) * d, math.sin(a) * d, r, rng.uniform(1, 1.8), a) * 1.2
    if flow is not None:
        # gravity runs: a few streaks flowing down from the central mass
        for _ in range(flow):
            cx = rng.uniform(-0.12, 0.12)
            ln = rng.uniform(0.15, 0.32)
            w = rng.uniform(0.012, 0.02)
            for t in np.linspace(0, 1, 24):
                f += aniso_blob(x, y, cx + math.sin(t * 5 + cx * 30) * 0.006, 0.05 + t * ln, w * (1 - 0.5 * t), 1.0) * 0.9
            f += aniso_blob(x, y, cx, 0.05 + ln + w * 0.6, w * 1.2, 1.0) * 1.0   # bead at the end
    # irregular edges
    n = fbm((size, size), rng, beta=3.0)
    n2 = fbm((size, size), rng, beta=2.0)
    f *= 0.78 + 0.4 * n + 0.1 * n2
    # keep off the border (clampmap)
    r = np.sqrt(x * x + y * y)
    f *= smoothstep(0.495, 0.44, r)
    return shade_liquid(f, 0.5, rng)


def drip(seed=7, w=128, h=512):
    rng = np.random.default_rng(seed)
    x, y = grid(w, h)
    f = np.zeros((h, w), np.float32)
    # three streaks of different length, one dominant; coordinates in pixels for sane gaussians
    X, Y = x * w, y * h
    for cx, top, ln, wid in ((0.5, 0.02, 0.84, 9.0), (0.25, 0.05, 0.45, 5.5), (0.76, 0.03, 0.6, 6.5)):
        cx += rng.normal(0, 0.02)
        # source smear at the top
        f += aniso_blob(X, Y, cx * w, (top + 0.04) * h, wid * 2.0, 1.8, 0.0) * 1.1
        n = 90
        wob = rng.uniform(0, 6)
        for i in range(n):
            t = i / (n - 1)
            yy = (top + 0.04 + t * ln) * h
            ww = wid * (1 - 0.55 * t) * (0.9 + 0.2 * math.sin(t * 17 + wob))
            xx = cx * w + math.sin(t * 6 + wob) * 2.5
            f += aniso_blob(X, Y, xx, yy, ww, 1.0) * 0.55
        # bead at the bottom
        f += aniso_blob(X, Y, cx * w + math.sin(6 + wob) * 2.5, (top + 0.04 + ln) * h + wid * 0.9, wid * 1.05, 1.0) * 1.3
    n = fbm((h, w), rng, beta=2.0)
    f *= 0.9 + 0.2 * n
    f *= smoothstep(0.0, 0.02, x) * smoothstep(1.0, 0.98, x) * smoothstep(1.0, 0.985, y)
    return shade_liquid(f, 0.5, rng, edge_soft=0.03, height_gain=4.0)


def screen(seed, kind):
    """On-screen blood: glossy, fairly opaque, with drips; drawn over the view."""
    size = 512
    rng = np.random.default_rng(seed)
    x, y = grid(size, size)
    x -= 0.5
    y -= 0.5
    f = np.zeros((size, size), np.float32)
    if kind == 0:      # big corner-style splash: heavy mass + runs
        for _ in range(12):
            bx = rng.uniform(-0.28, 0.28)
            f += aniso_blob(x, y, bx, -0.2 + 0.08 * math.cos(bx * 6) + rng.normal(0, 0.03), rng.uniform(0.035, 0.075),
                            rng.uniform(1.3, 2.4), rng.uniform(-0.4, 0.4)) * 1.0
        for _ in range(60):
            a = rng.uniform(math.pi * 0.05, math.pi * 0.95)
            d = rng.uniform(0.1, 0.33)
            f += aniso_blob(x, y, math.cos(a) * d * 1.2, -0.2 - math.sin(a) * d * 0.6, rng.uniform(0.004, 0.014), 1.3, a) * 1.2
        for _ in range(5):
            cx = rng.uniform(-0.2, 0.2)
            ln = rng.uniform(0.18, 0.38)
            wd = rng.uniform(0.018, 0.03)
            for t in np.linspace(0, 1, 30):
                f += aniso_blob(x, y, cx + math.sin(t * 4 + cx * 20) * 0.01, -0.17 + t * ln, wd * (1 - 0.5 * t), 1.0) * 0.8
            f += aniso_blob(x, y, cx, -0.17 + ln + wd, wd * 1.2, 1.0) * 1.1
    elif kind == 1:    # spray of droplets
        for _ in range(140):
            a = rng.uniform(0, 2 * math.pi)
            d = abs(rng.normal(0, 0.18))
            if d > 0.42:
                continue
            r = rng.uniform(0.006, 0.035) * (1.1 - d)
            f += aniso_blob(x, y, math.cos(a) * d, math.sin(a) * d, r, rng.uniform(1, 1.4), rng.uniform(0, 3)) * 1.2
    else:              # splat with a torn edge and satellite drops
        return_splat = splat(seed + 100, size, rays=24, core=1.15, satellites=140, flow=3)
        a = return_splat
        return boost_screen(a)
    n = fbm((size, size), rng, beta=2.2)
    f *= 0.8 + 0.4 * n
    r = np.sqrt(x * x + y * y)
    f *= smoothstep(0.495, 0.44, r)
    return boost_screen(shade_liquid(f, 0.5, rng, alpha_thin=0.85, gloss=1.2, spec_pow=45, rim=0.2))


def boost_screen(a):
    # screen blood: a bit more saturated and opaque so it reads over the HUD scene
    out = a.copy()
    out[..., 3] = np.clip(a[..., 3] * 1.05, 0, 1)
    return out


# ---------------------------------------------------------------- misc model textures
def wetenv(size=128):
    """Fake studio environment for tcGen environment: black with soft bright windows."""
    rng = np.random.default_rng(3)
    x, y = grid(size, size)
    img = np.zeros((size, size), np.float32)
    for cx, cy, r, s in ((0.3, 0.28, 0.07, 1.0), (0.7, 0.35, 0.05, 0.7), (0.55, 0.75, 0.09, 0.45), (0.18, 0.7, 0.04, 0.5)):
        img += s * np.exp(-(((x - cx) / r) ** 2 + ((y - cy) / (r * 0.7)) ** 2) ** 1.5)
    img += 0.04 * fbm((size, size), rng)
    rgb = np.stack([img, img * 0.9, img * 0.88], -1)
    return np.clip(rgb, 0, 1)


def _speckle(rng, size, thr=0.82):
    """Pitted-metal / grit speckle (tileable): thresholded fine noise."""
    return smoothstep(thr, thr + 0.06, fbm((size, size), rng, beta=0.6))


def rock_tex(size=128):
    """Chips of gothic block: warm grey-brown stone, grit, darker cracks (id gothic_block palette)."""
    rng = np.random.default_rng(11)
    n1 = fbm((size, size), rng, beta=2.4)
    n2 = fbm((size, size), rng, beta=1.4)
    n3 = fbm((size, size), rng, beta=3.0)
    lo, hi = np.array([0.20, 0.17, 0.15]), np.array([0.44, 0.37, 0.31])
    v = np.clip(0.5 + (n1 - 0.5) * 1.8 + 0.3 * (n2 - 0.5), 0, 1)[..., None]
    rgb = lo * (1 - v) + hi * v
    cracks = smoothstep(0.035, 0.0, np.abs(n3 - 0.5)) * 0.5
    rgb = rgb * (1 - cracks[..., None]) * (1 - 0.3 * _speckle(rng, size)[..., None])
    return np.clip(rgb, 0, 1)


def metal_tex(size=128):
    """Barrel / tank shrapnel: dark forged iron, rust blooms, soot, a few flakes of ochre paint."""
    rng = np.random.default_rng(12)
    n1 = fbm((size, size), rng, beta=2.2)
    n2 = fbm((size, size), rng, beta=1.2)
    n3 = fbm((size, size), rng, beta=2.6)
    iron = np.array([0.13, 0.11, 0.095]) * (0.8 + 0.6 * n2[..., None])
    rust = np.array([0.36, 0.17, 0.07]) * (0.7 + 0.5 * n2[..., None])
    rgb = iron + (rust - iron) * smoothstep(0.5, 0.72, n1)[..., None]
    paint = smoothstep(0.7, 0.74, n3)[..., None]
    rgb = rgb + (np.array([0.50, 0.37, 0.14]) * (0.8 + 0.3 * n2[..., None]) - rgb) * paint * 0.9
    rgb *= 1 - 0.35 * _speckle(rng, size)[..., None]
    rgb *= 1 - 0.5 * smoothstep(0.4, 0.15, n1)[..., None]          # scorch
    return np.clip(rgb, 0, 1)


def wood_tex(size=128):
    """Crate splinters: dark weathered plank wood (matches the crate), grain along v, torn fibres."""
    rng = np.random.default_rng(13)
    x, y = grid(size, size)
    n1 = fbm((size, size), rng, beta=2.0)
    warp = n1 * 1.6
    rings = 0.5 + 0.5 * np.sin((x * 9 + warp) * 2 * math.pi)
    fine = fbm((size, size), rng, beta=1.0)
    grain = np.repeat(fine.mean(0, keepdims=True), size, 0)
    grain = (grain - grain.min()) / (np.ptp(grain) + 1e-9)
    dark, light = np.array([0.15, 0.10, 0.06]), np.array([0.36, 0.25, 0.15])
    t = np.clip(0.45 * rings ** 2 + 0.55 * grain, 0, 1)[..., None]
    rgb = dark * t + light * (1 - t)
    grey = np.array([0.33, 0.29, 0.24])
    rgb = rgb + (grey - rgb) * (smoothstep(0.55, 0.8, n1) * 0.45)[..., None]   # weathered
    rgb *= (0.85 + 0.3 * n1)[..., None]
    return np.clip(rgb, 0, 1)


def main():
    for i, (seed, kw) in enumerate([(101, dict(rays=18)), (202, dict(rays=26, core=0.8, satellites=160)),
                                     (303, dict(rays=12, core=1.1, satellites=60, flow=4)),
                                     (404, dict(rays=34, core=0.65, satellites=220, tadpoles=True))], 1):
        save_tga(GORE / f'splat{i}.tga', splat(seed, **kw))
    save_tga(GORE / 'drip.tga', drip())
    for i in range(3):
        save_tga(GORE / f'screen{i + 1}.tga', screen(500 + i, i))
    save_tga(ASSETS / 'models' / 'rearmed' / 'gibs' / 'wetenv.tga', wetenv())
    save_tga(ASSETS / 'models' / 'rearmed' / 'gibs' / 'wetenv_dim.tga', wetenv() * 0.45)   # skin: subtler sheen
    deb = ASSETS / 'models' / 'rearmed' / 'debris'
    save_tga(deb / 'rock.tga', rock_tex())
    save_tga(deb / 'metal.tga', metal_tex())
    save_tga(deb / 'wood.tga', wood_tex())


if __name__ == '__main__':
    main()
