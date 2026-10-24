"""REARMED physics props in id's 1999 style: riveted iron cask (barrel), ochre pressure vessel (tank),
plank crate. Low poly, 256 px painterly textures with baked-in light, warm rust/brown/ochre palette.

Imported by make_models.py (runs inside Blender): do_barrel(), do_tank(), do_crate().
Maps are baked and painted at 2x (BAKE) and box-filtered down to TEX, so the final textures keep
Q3-era texel density (~3 px per unit) instead of crisp modern detail.

Outputs (paths and frame/surface layout unchanged, so cgame needs nothing):
  models/rearmed/props/{barrel,tank,crate}.md3 + .tga (rgb + alpha = 1 - shine)
  models/rearmed/props/barrel_glow.tga, tank_glow.tga (additive glow stages), props/propenv.tga
"""
import math
import os
import random
import sys

import bmesh
import numpy as np
from mathutils import Matrix, Vector, noise

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blib  # noqa: E402
from blib import col, fbm3, lerp, smoothstep, vnoise3  # noqa: E402

TEX = 256
BAKE = 512
STATS = {}


# =====================================================================================  shared
def finish(ob, rel_md3, shader):
    blib.triangulate(ob)
    n = blib.export_md3(rel_md3, [('body', ob, shader)])
    STATS[rel_md3] = n
    return n


def down2(a):
    """Box-filter a bottom-up (H, W[, C]) map down by 2 (the painterly Q3 softness)."""
    h, w = a.shape[:2]
    if a.ndim == 2:
        return a.reshape(h // 2, 2, w // 2, 2).mean((1, 3))
    return a.reshape(h // 2, 2, w // 2, 2, a.shape[2]).mean((1, 3))


def wrap(a):
    return np.arctan2(np.sin(a), np.cos(a))


def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    t = np.clip(((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy + 1e-9), 0, 1)
    return np.hypot(px - ax - t * dx, py - ay - t * dy)


def wobble(px, py, seed, amt=0.35, freq=0.7):
    """Hand-drawn jitter: displace a local 2D frame with low-frequency noise."""
    p = np.stack([px * freq, py * freq, np.full_like(px, seed * 1.37)], -1)
    return px + amt * (vnoise3(p, seed) - 0.5) * 2, py + amt * (vnoise3(p + 9.1, seed + 1) - 0.5) * 2


def strokes(px, py, segs, width, seed, soft=0.22, wob=0.35):
    """Brush strokes (list of (ax, ay, bx, by)) with a wobbly edge and uneven width -> mask 0..1."""
    qx, qy = wobble(px, py, seed, wob)
    d = np.full(px.shape, 1e9, np.float32)
    for s in segs:
        d = np.minimum(d, seg_dist(qx, qy, *s))
    w = width * (0.8 + 0.45 * vnoise3(np.stack([qx * 0.5, qy * 0.5, np.full_like(qx, seed)], -1), seed + 2))
    return smoothstep(w + soft, w - soft, d)


def disc(px, py, cx, cy, r, seed, soft=0.2):
    qx, qy = wobble(px, py, seed, 0.3, 0.9)
    return smoothstep(r + soft, r - soft, np.hypot(qx - cx, qy - cy))


def drips(mask, region, rng, frac=0.35, lmin=8, lmax=40, decay_mul=1.0):
    """Paint/oil runs: let `mask` (bottom-up image rows: row 0 = lowest) run downwards in a random
    subset of columns, inside `region`. Returns the extended mask."""
    h, w = mask.shape
    cols = np.zeros(w, np.float32)
    k = 0
    while k < w:
        width = rng.integers(1, 4)
        if rng.random() < frac:
            cols[k:k + width] = 1.0 - 1.0 / (rng.uniform(lmin, lmax) * decay_mul)
        k += width + rng.integers(1, 5)
    run = mask.copy()
    for r in range(h - 2, -1, -1):
        run[r] = np.maximum(run[r], run[r + 1] * cols * region[r])
    return np.clip(run, 0, 1)


def pits(pos, seed, scale=2.6, thr=0.8):
    """Pitted-metal speckle (the id pitted_rust look)."""
    n = vnoise3(pos * scale, seed) * 0.6 + vnoise3(pos * scale * 2.3, seed + 1) * 0.4
    return smoothstep(thr, thr + 0.08, n)


def q3_light(rgb, m, ao_k=0.7, top=0.22, hi=0.55):
    """id-style baked light: AO in the creases, a soft top light and painted highlights on convex edges."""
    ao = m['ao']
    nz = m['nrm'][..., 2]
    convex = np.clip((1 - m['edge']) * 5, 0, 1) * smoothstep(0.6, 0.9, ao)
    rgb = rgb * (1 - ao_k + ao_k * ao)[..., None]
    rgb = rgb * (1 - top * 0.5 + top * nz)[..., None]
    lit = convex * (0.45 + 0.55 * np.clip(nz * 0.5 + 0.6, 0, 1))
    rgb = rgb + (rgb * 0.9 + 0.035) * (hi * lit)[..., None]
    return np.clip(rgb, 0, 1)


def grit(pos, seed):
    """Texel-scale grain (the crunchy photo-sourced noise every id texture has), 0..1."""
    return np.clip(0.45 * vnoise3(pos * 3.3, seed) + 0.35 * vnoise3(pos * 7.1, seed + 1)
                   + 0.2 * vnoise3(pos * 13.0, seed + 2), 0, 1)


def rusty(out, pos, seed, amount=1.0, lo=0.5, hi=0.66, streak=0.3):
    """Granular, streaky rust (pitted_rust look): blotches broken up by grain, runs pulled downwards."""
    n = (fbm3(pos * 0.2 + seed, 4, seed) + 0.28 * (grit(pos, seed + 3) - 0.5)
         + streak * (fbm3(pos * np.array([0.7, 0.7, 0.07]), 3, seed + 4) - 0.5))
    mask = smoothstep(lo, hi, n) * amount
    g = grit(pos, seed + 7)
    rc = lerp(col(0.23, 0.12, 0.07), col(0.42, 0.22, 0.11), g)
    rc = lerp(rc, col(0.50, 0.32, 0.16), smoothstep(0.75, 0.9, g) * 0.6)      # bright flecks
    return lerp(out, rc, mask), mask


def mottle(pos, c0, c1, seed, scale=0.18, contrast=1.0):
    """Painterly base: low-frequency blotches between two colours plus a mid-frequency grain."""
    n = fbm3(pos * scale, 4, seed)
    n = np.clip(0.5 + (n - 0.5) * 1.8 * contrast, 0, 1)
    g = fbm3(pos * scale * 4.0, 3, seed + 5)
    out = lerp(c0, c1, n)
    return out * (0.86 + 0.28 * g)[..., None]


def save(rel, rgb, alpha=None):
    blib.save_tga(rel, down2(rgb), None if alpha is None else down2(alpha))


def propenv():
    """Dull warm environment map for the shiny stages (oil, brass, glass): blurry torch-lit room."""
    s = 64
    y, x = np.mgrid[0:s, 0:s].astype(np.float32) / s
    sky = lerp(col(0.10, 0.08, 0.07), col(0.42, 0.33, 0.24), smoothstep(0.2, 0.9, y))
    p = np.stack([x * 5, y * 5, np.zeros_like(x)], -1)
    blob = smoothstep(0.55, 0.8, fbm3(p, 3, 77))
    sky = lerp(sky, col(0.75, 0.5, 0.25), blob * smoothstep(0.4, 0.9, y))
    blib.save_tga('models/rearmed/props/propenv.tga', np.clip(sky, 0, 1))


# =====================================================================================  BARREL
# Riveted iron cask: bulged body, four hoops (the middle two hand-fitted and tilted), recessed lid
# with a bung, hand-painted skull and fire rune, a glowing crack bleeding dark oil.
B_HOOPS = [  # (z0, z1, tilt, phase)  raised iron hoops
    (0.0, 4.4, 0.0, 0.0), (11.6, 14.4, 0.9, 1.1), (25.4, 28.6, -0.7, 2.6), (36.2, 40.0, 0.0, 0.0)]
B_SEG = 14
B_SEAMS = (0.95, 3.05, 5.2)              # plate seams (angles)
B_SKULL = (0.0, 20.0)                    # (angle, z) of the painted skull
B_RUNE = (math.pi, 20.0)                 # fire rune on the far side
B_CRACK = (2.15, 5.0, 11.4)              # glowing crack: angle, z0, z1


def b_body_r(z):
    return 12.5 + 1.0 * math.sin(math.pi * min(max(z, 0), 40) / 40.0)


def build_barrel():
    blib.clear_objects()
    bm = bmesh.new()
    prof, ring_tilt = [], []
    # bottom chime (lid recess is mirrored at the top)
    prof += [(11.2, 1.3), (11.9, 0.0), (13.0, 0.0), (13.3, 0.8)]
    ring_tilt += [None, None, None, None]
    zs = [4.2, 4.4, 8.0, 11.4, 11.6, 14.4, 14.6, 17.5, 20.5, 23.5, 25.2, 25.4, 28.6, 28.8, 32.5, 36.0, 36.2]
    for z in zs:
        hoop = None
        for k, (z0, z1, t, ph) in enumerate(B_HOOPS):
            if z0 - 0.01 <= z <= z1 + 0.01:
                hoop = k
        r = b_body_r(z)
        if hoop is not None and z not in (11.4, 14.6, 25.2, 28.8, 4.4, 36.0):
            r += 0.65
        if z in (4.2,):
            r = 13.3
        if z == 36.2:
            r = 13.3
        prof.append((r, z))
        k2 = None
        for k, (z0, z1, t, ph) in enumerate(B_HOOPS):
            if t and z0 - 0.25 <= z <= z1 + 0.25:
                k2 = k
        ring_tilt.append(k2)
    prof += [(13.3, 39.2), (13.0, 40.0), (12.0, 40.0), (11.4, 38.7)]
    ring_tilt += [None, None, None, None]
    dents = [(0.6, 19.0, 4.5, 0.9), (2.4, 8.0, 3.5, 0.7), (4.4, 31.5, 4.0, 0.8), (5.6, 21.0, 3.0, 0.5)]
    rnd = random.Random(7)
    jitter = [rnd.uniform(-0.12, 0.12) for _ in range(len(prof) * B_SEG)]

    def radial(i, a, r, z):
        k = ring_tilt[i]
        if k is not None:
            _, _, t, ph = B_HOOPS[k]
            z += t * math.cos(a - ph)
        if 4 <= i < len(prof) - 4:
            for da, dz, rad, depth in dents:
                d_ang = math.atan2(math.sin(a - da), math.cos(a - da)) * r
                d = math.hypot(d_ang, z - dz)
                r -= depth * math.exp(-(d / rad) ** 2)
            j = int(round(a / (2 * math.pi) * B_SEG)) % B_SEG
            r += jitter[i * B_SEG + j]
        return r, z
    blib.lathe(bm, prof, B_SEG, (0.36, 1.0), mat=0, cap_bottom=(0.5, 0.18, 0.155, 1, 0.4),
               cap_top=(0.18, 0.18, 0.155, 1, -0.5), radial=radial, phase=0.11)
    # bung + a smaller vent plug on the lid
    blib.cylinder(bm, 2.0, 38.4, 39.6, 8, center=(6.8, 2.2), uv_rect=(0.70, 0.02, 0.84, 0.34), mat=2)
    blib.cylinder(bm, 1.2, 38.4, 39.1, 6, center=(-6.2, -4.0), uv_rect=(0.85, 0.02, 0.99, 0.34), mat=2)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    blib.auto_sharp(bm, 34)
    return blib.obj_from_bm('barrel', bm, ('b_side', 'b_cap', 'b_bung'))


def b_hoop_masks(ang, z):
    """Per-hoop soft membership on the side (matches the tilted geometry)."""
    out = []
    for z0, z1, t, ph in B_HOOPS:
        zz = z - t * np.cos(ang - ph)
        out.append(smoothstep(z0 - 0.3, z0 + 0.05, zz) * smoothstep(z1 + 0.3, z1 - 0.05, zz))
    return out


def rivet_ring(ang, z, r, n, z0, phase, rad=0.5):
    """Rivet heads around the cask at height z0: (mask, light) where light > 0 on the top-left."""
    f = np.mod(ang / (2 * np.pi) * n + phase, 1.0) - 0.5
    dx = f * 2 * np.pi * r / n
    dy = z - z0
    d = np.hypot(dx, dy)
    m = smoothstep(rad + 0.15, rad - 0.15, d)
    light = np.clip((-dx * 0.5 + dy) / rad, -1, 1)
    return m, light


def paint_barrel(m):
    pos, idm = m['pos'], m['id']
    h, w = idm.shape
    x, y, z = pos[..., 0], pos[..., 1], pos[..., 2]
    ang = np.arctan2(y, x)
    r = np.hypot(x, y)
    side = (idm == 0)
    lid = (idm == 1) & (z > 20)
    bottom = (idm == 1) & (z <= 20)
    bung = (idm == 2)
    rng = np.random.default_rng(31)

    iron_dk, iron_lt = col(0.19, 0.15, 0.12), col(0.42, 0.33, 0.25)
    rust_dk, rust_lt = col(0.30, 0.13, 0.06), col(0.55, 0.27, 0.11)
    out = mottle(pos, iron_dk, iron_lt, 1, 0.16, 1.2)
    # blotchy rust over the plates
    out, rust = rusty(out, pos, 2, 0.85, 0.53, 0.7, 0.55)
    streak = fbm3(pos * np.array([0.35, 0.35, 0.04]), 3, 9)
    out = out * (1 - 0.3 * smoothstep(0.55, 0.78, streak))[..., None]
    out = out * (1 - 0.35 * pits(pos, 4))[..., None]

    # hoops: darker forged iron, hammered, rust bleeding from the lower edge
    hoops = b_hoop_masks(ang, z)
    hoop = np.clip(sum(hoops), 0, 1) * side
    hammer = 0.8 + 0.4 * vnoise3(pos * 0.9, 5)
    hoop_col = mottle(pos, col(0.10, 0.085, 0.075), col(0.22, 0.18, 0.15), 6, 0.3) * hammer[..., None]
    out = lerp(out, hoop_col, hoop)
    # plate seams: dark overlap line + a highlight lip + a column of rivets
    seam_m = np.zeros_like(z)
    seam_lit = np.zeros_like(z)
    riv_m = np.zeros_like(z)
    riv_l = np.zeros_like(z)
    for a0 in B_SEAMS:
        dx = wrap(ang - a0) * 13.3
        seam_m = np.maximum(seam_m, smoothstep(0.35, 0.0, np.abs(dx)))
        seam_lit = np.maximum(seam_lit, smoothstep(0.9, 0.35, np.abs(dx + 0.55)) * (dx < 0))
        fz = np.mod(z / 3.4, 1.0) - 0.5
        d = np.hypot(dx + 1.0, fz * 3.4)
        mm = smoothstep(0.55, 0.35, d) * (z > 5) * (z < 35.5)
        riv_m = np.maximum(riv_m, mm)
        riv_l = np.where(mm > 0, np.clip(((-(dx + 1.0)) * 0.5 + fz * 3.4) / 0.5, -1, 1), riv_l)
    out = lerp(out, col(0.05, 0.04, 0.035), seam_m * 0.8 * side * (1 - hoop))
    out = lerp(out, col(0.36, 0.30, 0.25), seam_lit * 0.35 * side * (1 - hoop))
    # rivets along the hoops
    for k, (z0, z1, t, ph) in enumerate(B_HOOPS):
        zc = (z0 + z1) / 2 + t * np.cos(ang - ph)
        mm, ll = rivet_ring(ang, z, 13.6, 12 if k in (0, 3) else 9, zc, 0.13 * k, 0.55)
        riv_m = np.maximum(riv_m, mm * side)
        riv_l = np.where(mm > 0.01, ll, riv_l)
    riv_col = col(0.20, 0.16, 0.13) * (1.0 + 0.9 * np.clip(riv_l, 0, 1))[..., None] * (1 - 0.5 * np.clip(-riv_l, 0, 1))[..., None]
    out = lerp(out, riv_col, riv_m)
    # rust tears running down from every rivet and hoop
    tear_src = np.clip(riv_m + hoop * smoothstep(0.55, 0.75, fbm3(pos * 0.6, 3, 7)), 0, 1) * side
    tears = drips(tear_src, side.astype(np.float32), rng, 0.4, 6, 30) - tear_src
    tears = np.clip(tears, 0, 1) * (0.5 + 0.5 * fbm3(pos * 0.8, 2, 8))
    out = lerp(out, col(0.36, 0.15, 0.06), tears * 0.75)

    # ---- hand-painted skull (bone white, faded and flaking) + fire rune (rust red)
    lx = wrap(ang - B_SKULL[0]) * 13.4
    ly = z - B_SKULL[1]
    skull = np.clip(disc(lx, ly, 0, 1.4, 3.7, 11) + strokes(lx, ly, [(-1.6, -2.4, 1.6, -2.4)], 1.25, 12), 0, 1)
    holes = np.clip(disc(lx, ly, -1.45, 1.1, 1.05, 13) + disc(lx, ly, 1.5, 1.0, 1.0, 14)
                    + strokes(lx, ly, [(0, -0.2, 0.0, -0.6)], 0.45, 15)
                    + strokes(lx, ly, [(-0.9, -3.2, -0.9, -1.9), (0.1, -3.3, 0.1, -1.9), (1.0, -3.2, 1.0, -1.9)], 0.18, 16, 0.12), 0, 1)
    skull = np.clip(skull - holes, 0, 1) * side
    rx = wrap(ang - B_RUNE[0]) * 13.4
    ry = z - B_RUNE[1]
    rune = strokes(rx, ry, [(-4.0, -3.6, 4.0, -3.6), (4.0, -3.6, 0.1, 4.4), (0.1, 4.4, -4.0, -3.6),
                            (-0.2, -1.4, 0.2, 1.3)], 0.55, 21) * side
    region = (side & (z > 4.6)).astype(np.float32)
    skull_run = drips(skull * smoothstep(-1.5, -3.0, ly), region, rng, 0.55, 5, 18)
    rune_run = drips(rune, region, rng, 0.45, 5, 22)
    wear = smoothstep(0.32, 0.55, fbm3(pos * 0.45 + 2, 4, 22)) * (1 - 0.6 * pits(pos, 23, 3.2, 0.72))
    paint_skull = np.clip(np.maximum(skull, skull_run * 0.8) * wear, 0, 1)
    paint_rune = np.clip(np.maximum(rune, rune_run * 0.8) * wear, 0, 1)
    out = lerp(out, mottle(pos, col(0.46, 0.40, 0.30), col(0.62, 0.55, 0.42), 24, 0.6), paint_skull * 0.9)
    out = lerp(out, mottle(pos, col(0.34, 0.08, 0.04), col(0.52, 0.16, 0.06), 25, 0.6), paint_rune * 0.9)

    # ---- the crack: a jagged split between the lower hoops, glowing; dark oil weeping from it
    cx = wrap(ang - B_CRACK[0]) * 13.4
    pts = [(0.0, B_CRACK[1])]
    crng = random.Random(5)
    zc = B_CRACK[1]
    while zc < B_CRACK[2]:
        zc += crng.uniform(0.9, 1.6)
        pts.append((crng.uniform(-0.7, 0.7), min(zc, B_CRACK[2])))
    cseg = [(pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1]) for i in range(len(pts) - 1)]
    cd = np.full(z.shape, 1e9, np.float32)
    for s in cseg:
        cd = np.minimum(cd, seg_dist(cx, z, *s))
    cd = np.where(side, cd, 1e9)
    core = smoothstep(0.5, 0.12, cd)
    lip = smoothstep(1.4, 0.3, cd)
    out = lerp(out, col(0.05, 0.03, 0.02), lip * 0.7)
    out = lerp(out, col(0.30, 0.07, 0.02), core)
    oil_src = smoothstep(1.8, 0.4, cd) * (z < B_CRACK[2] - 1.5)
    oil = drips(oil_src, side.astype(np.float32), rng, 0.85, 40, 220)
    oil = (oil + np.roll(oil, 1, 1) + np.roll(oil, -1, 1) + np.roll(oil, 2, 1) + np.roll(oil, -2, 1)) / 3.2
    oil = np.clip(oil * (0.7 + 0.3 * fbm3(pos * 1.2, 2, 26)), 0, 1)
    # a soot plume rising from the crack
    plume = smoothstep(3.5, 0.0, np.abs(cx + 0.15 * (z - B_CRACK[2]))) * smoothstep(B_CRACK[1], B_CRACK[2] + 2, z) \
        * smoothstep(B_CRACK[2] + 16, B_CRACK[2], z) * side
    out = lerp(out, col(0.04, 0.035, 0.03), plume * 0.55 * (0.6 + 0.4 * fbm3(pos * 0.7, 3, 27)))

    # ---- lid + bottom
    rl = np.hypot(x, y)
    lid_col = mottle(pos, col(0.13, 0.11, 0.09), col(0.27, 0.22, 0.17), 30, 0.25)
    lid_col = lid_col * (1 - 0.12 * (0.5 + 0.5 * np.cos(rl * 1.1)))[..., None]
    lid_col = lerp(lid_col, mottle(pos, rust_dk, rust_lt, 31, 0.6), smoothstep(0.5, 0.7, fbm3(pos * 0.3, 3, 32)) * 0.8)
    soot_ring = smoothstep(4.5, 1.8, np.hypot(x - 6.8, y - 2.2))
    lid_col = lerp(lid_col, col(0.03, 0.025, 0.02), soot_ring * 0.85)
    out = np.where(lid[..., None], lid_col, out)
    out = np.where(bottom[..., None], col(0.07, 0.06, 0.05) * (0.8 + 0.4 * fbm3(pos * 0.3, 3, 33))[..., None], out)
    bung_col = mottle(pos, col(0.20, 0.14, 0.08), col(0.36, 0.26, 0.14), 34, 0.8)
    out = np.where(bung[..., None], bung_col, out)

    # ---- grime from the floor, soot from fires, scratches
    g = smoothstep(8, 0, z) * (0.6 + 0.4 * fbm3(pos * 0.4, 3, 35)) * (idm != 1)
    out = lerp(out, col(0.06, 0.05, 0.04), g * 0.6)
    soot = smoothstep(0.58, 0.8, fbm3(pos * 0.1 + 10, 3, 36)) * smoothstep(10, 40, z)
    out = lerp(out, col(0.035, 0.03, 0.025), soot * 0.55)
    sc = vnoise3(pos * np.array([0.3, 0.3, 5.0]), 37)
    out = lerp(out, col(0.40, 0.34, 0.28), smoothstep(0.88, 0.95, sc) * 0.35 * side)
    # oil on top of everything: near black, slightly brown at the thin edges
    out = out * (0.8 + 0.4 * grit(pos, 38))[..., None]
    out = lerp(out, lerp(col(0.10, 0.06, 0.03), col(0.015, 0.012, 0.01), smoothstep(0.2, 0.8, oil)), np.clip(oil * 1.2, 0, 0.95))
    out = q3_light(out, m)

    shine = 0.05 + 0.10 * hoop + 0.2 * riv_m + 0.55 * np.clip(oil * 1.3, 0, 1) + 0.25 * bung
    glow = col(1.0, 0.42, 0.08) * core[..., None] + col(0.55, 0.14, 0.02) * (smoothstep(2.2, 0.0, cd) * 0.45)[..., None]
    return out, 1 - np.clip(shine, 0, 0.8), np.clip(glow, 0, 1)


# =====================================================================================  TANK
# Old pressure vessel: riveted iron body in chipped ochre paint, iron straps, a domed shoulder,
# a bronze valve gone green with verdigris, an oily rag tied round the neck, a sickly yellow-green
# sight glass, hand-stencilled alchemical "air" rune and upward chevrons. The outlet points +X
# (cgame vents the rupture jet along +X at the top).
T_STRAPS = [(8.8, 11.0), (20.8, 23.0)]
T_RUNE = (0.0, 16.0)
T_GLASS = (2.2, 11.4, 20.4)        # angle, z0, z1
T_CHEV = (-2.1, 16.0)


def build_tank():
    blib.clear_objects()
    bm = bmesh.new()
    prof = [(10.4, 1.2), (11.0, 0.0), (11.9, 0.0), (11.9, 3.0), (11.3, 3.6), (11.3, 8.6), (11.85, 8.8),
            (11.85, 11.0), (11.3, 11.2), (11.3, 16.0), (11.3, 20.6), (11.85, 20.8), (11.85, 23.0), (11.3, 23.2),
            (11.3, 29.4), (11.0, 31.6), (10.2, 33.5), (8.8, 35.2), (6.9, 36.5), (4.6, 37.3), (3.2, 37.6)]
    rnd = random.Random(11)
    jit = {}

    def radial(i, a, r, z):
        if 5 <= i <= 17:
            k = (i, int(round(a / (2 * math.pi) * 14)) % 14)
            jit.setdefault(k, rnd.uniform(-0.1, 0.1))
            r += jit[k]
            d = math.hypot(math.atan2(math.sin(a - 4.0), math.cos(a - 4.0)) * r, z - 26.0)
            r -= 0.7 * math.exp(-(d / 3.5) ** 2)       # one dent
        return r, z
    blib.lathe(bm, prof, 14, (0.36, 1.0), mat=0, cap_bottom=(0.10, 0.10, 0.09, 1, 0.8), radial=radial)
    # bronze neck, valve body and spindle
    neck = [(3.3, 37.4), (2.4, 37.7), (2.4, 40.8), (3.3, 41.0), (3.3, 41.9), (2.2, 42.1), (0.8, 42.2)]
    blib.lathe(bm, neck, 8, (0.02, 0.34), (0.21, 0.45), mat=2, cap_top=(0.33, 0.18, 0.0001, 2))
    nv = len(bm.verts)
    blib.cylinder(bm, 0.6, 42.0, 43.9, 6, uv_rect=(0.47, 0.02, 0.55, 0.16), mat=2, top=True)
    # spoked hand wheel: a square-section rim + two crossed spokes + hub
    rim = [(3.7, 42.9), (4.6, 42.9), (4.6, 43.7), (3.7, 43.7), (3.7, 42.9)]
    blib.lathe(bm, rim, 12, (0.18, 0.34), (0.47, 0.64), mat=2, phase=0.2)
    for k in range(2):
        nv = len(bm.verts)
        _box(bm, (-3.9, -0.35, 43.0), (3.9, 0.35, 43.55), 2)
        blib.transform_new(bm, nv, Matrix.Rotation(math.pi / 4 + k * math.pi / 2, 4, 'Z'))
        uvl = blib.uv_layer(bm)
        bm.verts.ensure_lookup_table()
        for f in {f for v in bm.verts[nv:] for f in v.link_faces}:
            for loop in f.loops:
                loop[uvl].uv = (0.565 + 0.07 * (loop.vert.co.x + 4) / 8, 0.03 + 0.12 * (loop.vert.co.z - 43.0) / 0.55)
    # oily rag knotted round the neck: a lumpy wrap + a hanging tail
    nv = len(bm.verts)
    rag = [(2.4, 37.9), (3.7, 38.1), (4.1, 39.0), (3.8, 40.1), (2.4, 40.4)]
    rr = random.Random(3)
    bumps = [rr.uniform(-0.35, 0.35) for _ in range(len(rag) * 8)]
    blib.lathe(bm, rag, 8, (0.02, 0.20), (0.82, 0.99), mat=3, phase=0.3,
               radial=lambda i, a, r, z: (r + (bumps[i * 8 + int(round(a / (2 * math.pi) * 8)) % 8] if 0 < i < 4 else 0), z))
    tail = [Vector((-2.6, -2.6, 38.9)), Vector((-3.6, -3.6, 37.6)), Vector((-4.6, -4.4, 36.0)), Vector((-5.4, -4.9, 34.6))]
    rings = []
    for i, p in enumerate(tail):
        wdt, th = 1.3 - 0.15 * i, 0.35
        side = Vector((0.7, -0.7, 0)) * wdt
        rings.append([bm.verts.new(p + side + Vector((0.3, 0.3, 0)) * th), bm.verts.new(p - side + Vector((0.3, 0.3, 0)) * th),
                      bm.verts.new(p - side - Vector((0.3, 0.3, 0)) * th), bm.verts.new(p + side - Vector((0.3, 0.3, 0)) * th)])
    uvl = blib.uv_layer(bm)
    for i in range(len(rings) - 1):
        for j in range(4):
            j2 = (j + 1) % 4
            f = bm.faces.new((rings[i][j], rings[i][j2], rings[i + 1][j2], rings[i + 1][j]))
            f.material_index = 3
            for loop, (a, b) in zip(f.loops, ((j, i), (j + 1, i), (j + 1, i + 1), (j, i + 1))):
                loop[uvl].uv = (0.825 + 0.16 * a / 4, 0.225 + 0.11 * b / 3)
    f = bm.faces.new(list(reversed(rings[-1])))
    f.material_index = 3
    for loop in f.loops:
        loop[uvl].uv = (0.99, 0.34)
    bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=0.001)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    blib.auto_sharp(bm, 36)
    return blib.obj_from_bm('tank', bm, ('t_body', 't_cap', 't_valve', 't_rag'))


def paint_tank(m):
    pos, idm = m['pos'], m['id']
    x, y, z = pos[..., 0], pos[..., 1], pos[..., 2]
    ang = np.arctan2(y, x)
    body = (idm == 0)
    valve = (idm == 2)
    rag = (idm == 3)
    rng = np.random.default_rng(41)

    iron = mottle(pos, col(0.12, 0.10, 0.09), col(0.24, 0.20, 0.16), 41, 0.25)
    iron, _ = rusty(iron, pos, 42, 0.85, 0.45, 0.62)
    iron = iron * (1 - 0.3 * pits(pos, 44))[..., None]
    ochre = mottle(pos, col(0.40, 0.30, 0.13), col(0.60, 0.47, 0.22), 45, 0.14)
    # brush-stroke streaks in the paint (painted by hand, vertically)
    brush = fbm3(pos * np.array([0.9, 0.9, 0.12]), 3, 46)
    ochre = ochre * (0.88 + 0.24 * brush)[..., None]
    # chipping: edges, straps, foot, plus random flakes
    straps = np.zeros_like(z)
    for z0, z1 in T_STRAPS:
        straps = np.maximum(straps, smoothstep(z0 - 0.2, z0 + 0.1, z) * smoothstep(z1 + 0.2, z1 - 0.1, z))
    foot = smoothstep(3.8, 3.3, z)
    convex = np.clip((1 - m['edge']) * 5, 0, 1)
    flake_n = fbm3(pos * 0.35, 4, 47)
    chip = smoothstep(0.62, 0.68, flake_n * 0.75 + convex * 0.45 + 0.1 * pits(pos, 48, 1.5, 0.6))
    halo = np.clip(smoothstep(0.55, 0.62, flake_n * 0.75 + convex * 0.45) - chip, 0, 1)
    patch = smoothstep(0.58, 0.62, fbm3(pos * 0.12 + 5, 3, 53))
    ochre = lerp(ochre, ochre * col(0.78, 0.74, 0.62), patch * 0.8)          # patched over in a duller batch
    paint = body * (1 - straps) * (1 - foot)
    out = lerp(iron, ochre * (1 - 0.35 * halo)[..., None], paint * (1 - chip))
    # straps: dark iron with rivets
    strap_col = mottle(pos, col(0.10, 0.085, 0.07), col(0.21, 0.17, 0.14), 49, 0.35)
    out = lerp(out, strap_col, straps * body)
    riv_m = np.zeros_like(z)
    riv_l = np.zeros_like(z)
    for z0, z1 in T_STRAPS:
        mm, ll = rivet_ring(ang, z, 11.85, 14, (z0 + z1) / 2, 0.25, 0.5)
        riv_m = np.maximum(riv_m, mm * body)
        riv_l = np.where(mm > 0.01, ll, riv_l)
    # body seam with rivets (one vertical plate seam, + a ring of rivets on the shoulder)
    dx = wrap(ang - 3.6) * 11.3
    fz = np.mod(z / 2.4, 1.0) - 0.5
    mm = smoothstep(0.5, 0.3, np.hypot(dx, fz * 2.4)) * (z > 3.8) * (z < 29) * (1 - straps)
    riv_m = np.maximum(riv_m, mm * body)
    riv_l = np.where(mm > 0.01, np.clip((-dx * 0.5 + fz * 2.4) / 0.5, -1, 1), riv_l)
    out = lerp(out, col(0.06, 0.05, 0.04), smoothstep(0.3, 0.0, np.abs(dx - 0.9)) * 0.7 * body * (z < 29))
    mm, ll = rivet_ring(ang, z, 11.1, 18, 30.6, 0.0, 0.45)
    riv_m = np.maximum(riv_m, mm * body)
    riv_l = np.where(mm > 0.01, ll, riv_l)
    riv_col = col(0.22, 0.17, 0.12) * (1.0 + 0.9 * np.clip(riv_l, 0, 1))[..., None] * (1 - 0.5 * np.clip(-riv_l, 0, 1))[..., None]
    out = lerp(out, riv_col, riv_m)
    strap_edge = np.zeros_like(z)
    for z0, z1 in T_STRAPS:
        strap_edge = np.maximum(strap_edge, smoothstep(z0 + 0.6, z0, z) * smoothstep(z0 - 0.4, z0, z))
    src = np.clip(riv_m + strap_edge * smoothstep(0.45, 0.7, fbm3(pos * 0.7, 2, 54)), 0, 1) * body
    tears = np.clip(drips(src, body.astype(np.float32), rng, 0.5, 8, 45) - src, 0, 1)
    out = lerp(out, col(0.34, 0.14, 0.05), tears * 0.7 * (0.5 + 0.5 * fbm3(pos * 0.8, 2, 50)))

    # ---- sight glass: iron frame, riveted, murky yellow-green glass with a liquid line
    gx = wrap(ang - T_GLASS[0]) * 11.3
    gz0, gz1 = T_GLASS[1], T_GLASS[2]
    inner = smoothstep(2.2, 2.0, np.abs(gx)) * smoothstep(gz0 + 0.6, gz0 + 0.8, z) * smoothstep(gz1 - 0.6, gz1 - 0.8, z) * body
    frame = smoothstep(3.2, 3.0, np.abs(gx)) * smoothstep(gz0 - 0.4, gz0 - 0.2, z) * smoothstep(gz1 + 0.4, gz1 + 0.2, z) * body
    frame = np.clip(frame - inner, 0, 1)
    out = lerp(out, strap_col * 1.1, frame)
    for zz in (gz0 + 0.1, gz1 - 0.1, (gz0 + gz1) / 2):
        for sx in (-2.65, 2.65):
            mm = smoothstep(0.45, 0.25, np.hypot(gx - sx, z - zz)) * body
            out = lerp(out, col(0.32, 0.27, 0.2), mm)
    level = gz0 + (gz1 - gz0) * 0.62 + 0.25 * np.sin(gx * 2.0)
    liquid = smoothstep(level + 0.15, level - 0.15, z)
    glass = lerp(col(0.22, 0.25, 0.09), col(0.62, 0.70, 0.20), liquid * (0.8 + 0.2 * fbm3(pos * 1.5, 2, 51)))
    glass = lerp(glass, col(0.70, 0.75, 0.30), smoothstep(0.5, 0.0, np.abs(z - level)) * 0.8)        # meniscus
    glass = glass * (0.75 + 0.35 * smoothstep(1.6, 0.2, np.abs(gx + 0.7)))[..., None]
    glass = lerp(glass, col(0.10, 0.09, 0.05), smoothstep(0.55, 0.8, fbm3(pos * 0.9, 3, 52)) * 0.7)  # grime
    out = lerp(out, glass, inner)

    # ---- hand-stencilled marks: alchemical air (triangle + bar), three upward chevrons
    lx = wrap(ang - T_RUNE[0]) * 11.3
    ly = z - T_RUNE[1]
    rune = strokes(lx, ly, [(-3.6, -3.4, 3.6, -3.4), (3.6, -3.4, 0.0, 3.8), (0.0, 3.8, -3.6, -3.4),
                            (-4.3, 0.6, 4.3, 0.6)], 0.55, 61, 0.15, 0.12)
    # stencil bridges
    bridges = (np.abs(lx - 2.6) < 0.25) & (ly > 0) & (ly < 1.5) | (np.abs(lx + 1.2) < 0.25) & (ly < -2.5)
    rune = rune * (1 - bridges) * body
    cx = wrap(ang - T_CHEV[0]) * 11.3
    cy = z - T_CHEV[1]
    chev = np.zeros_like(z)
    for k in range(3):
        oy = -3.3 + k * 3.0
        chev = np.maximum(chev, strokes(cx, cy, [(-2.6, oy, 0.0, oy + 2.0), (0.0, oy + 2.0, 2.6, oy)], 0.55, 62 + k, 0.15, 0.12))
    chev = chev * body
    marks = np.clip(rune + chev, 0, 1)
    overspray = np.clip(blur_like(marks, pos, 0.9) * 0.35, 0, 1)
    wear = smoothstep(0.3, 0.5, fbm3(pos * 0.5 + 7, 4, 63)) * (1 - chip)
    mark_col = col(0.09, 0.07, 0.05)
    out = lerp(out, mark_col, np.clip((marks * 0.9 + overspray * (1 - marks)) * wear, 0, 1))

    # ---- shoulder: soot from the valve, rust tears
    soot = smoothstep(31.0, 37.5, z) * body * (0.6 + 0.4 * fbm3(pos * 0.5, 3, 64))
    out = lerp(out, col(0.05, 0.04, 0.03), soot * 0.65)
    streak = fbm3(pos * np.array([0.35, 0.35, 0.04]), 3, 55)
    out = out * (1 - 0.3 * smoothstep(0.55, 0.78, streak) * body)[..., None]
    # ---- bronze with verdigris in the crevices
    bronze = mottle(pos, col(0.30, 0.20, 0.09), col(0.50, 0.36, 0.16), 65, 0.6)
    verd = smoothstep(0.9, 0.55, m['ao']) * 0.7 + smoothstep(0.55, 0.75, fbm3(pos * 0.8, 3, 66)) * 0.6
    bronze = lerp(bronze, mottle(pos, col(0.20, 0.26, 0.20), col(0.30, 0.36, 0.28), 67, 0.9), np.clip(verd * 0.7, 0, 1))
    out = np.where(valve[..., None], bronze, out)
    # ---- rag: dirty linen, oil soaked in blotches
    ragc = mottle(pos, col(0.30, 0.26, 0.19), col(0.46, 0.40, 0.30), 68, 1.0)
    ragc = lerp(ragc, col(0.08, 0.06, 0.04), smoothstep(0.45, 0.7, fbm3(pos * 0.9, 3, 69)) * 0.8)
    weave = 0.85 + 0.15 * np.sin(pos[..., 2] * 9.0) * np.sin(ang * 40)
    out = np.where(rag[..., None], ragc * weave[..., None], out)
    out = np.where((idm == 1)[..., None], col(0.06, 0.05, 0.04), out)

    # ---- floor grime + scratches
    g = smoothstep(14, 0, z) * (0.5 + 0.5 * fbm3(pos * 0.4, 3, 70)) * body
    out = lerp(out, col(0.08, 0.06, 0.04), g * 0.65)
    sc = vnoise3(pos * np.array([0.3, 0.3, 5.0]), 71)
    out = lerp(out, col(0.30, 0.26, 0.2), smoothstep(0.88, 0.95, sc) * 0.4 * body)
    out = out * (0.82 + 0.36 * grit(pos, 72))[..., None]
    out = q3_light(out, m)

    shine = 0.04 + 0.4 * valve + 0.65 * inner + 0.15 * paint * (1 - chip) + 0.25 * riv_m
    glow = col(0.36, 0.48, 0.07) * (inner * (0.3 + 0.7 * liquid))[..., None]
    return out, 1 - np.clip(shine, 0, 0.85), np.clip(glow, 0, 1)


def blur_like(mask, pos, r):
    """Soft halo around a mask (overspray): a few shifted max samples in image space."""
    acc = mask.copy()
    for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1), (2, 1), (-2, -1), (1, -2), (-1, 2)):
        acc = acc + np.roll(np.roll(mask, dy * 2, 0), dx * 2, 1)
    return acc / 9.0


# =====================================================================================  CRATE
# Rough plank crate: plank walls, chunky edge battens, diagonal braces, forged iron corner caps.
def _box(bm, lo, hi, mat):
    verts = [bm.verts.new((x, y, z)) for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])]
    idx = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    for q in idx:
        bm.faces.new([verts[i] for i in q]).material_index = mat
    return verts


def build_crate():
    blib.clear_objects()
    bm = bmesh.new()
    rnd = random.Random(23)
    solids = []      # boxes (lo, hi) for the hidden-face test

    def add(lo, hi, mat, rot=None):
        nv = len(bm.verts)
        _box(bm, lo, hi, mat)
        if rot is not None:
            blib.transform_new(bm, nv, rot)
        else:
            solids.append((lo, hi))
    C = 14.6
    add((-C, -C, 16 - C), (C, C, 16 + C), 0)              # plank walls
    E = 15.75
    for k in range(3):          # 12 edge battens, slightly uneven
        for s1 in (-1, 1):
            for s2 in (-1, 1):
                a, b = [i for i in range(3) if i != k]
                lo, hi = [0, 0, 0], [0, 0, 0]
                lo[k], hi[k] = -12.0, 12.0
                for ax, s in ((a, s1), (b, s2)):
                    inner = 12.2 + rnd.uniform(-0.3, 0.3)
                    outer = E - rnd.uniform(0.0, 0.25)
                    lo[ax], hi[ax] = (inner, outer) if s > 0 else (-outer, -inner)
                lo[2] += 16
                hi[2] += 16
                add(tuple(lo), tuple(hi), 1)
    for k, (ax, s) in enumerate(((0, 1), (0, -1), (1, 1), (1, -1))):    # diagonal braces on the walls
        L, W, T = 17.4, 1.5, 0.7
        lo, hi = (-L, -W, 0.0), (L, W, T)
        ang = math.radians(45 if k % 2 else -45)
        if ax == 0:
            place = Matrix.Translation((s * C, 0, 16)) @ Matrix.Rotation(math.pi / 2 * s, 4, 'Y') @ Matrix.Rotation(ang, 4, 'Z')
        else:
            place = Matrix.Translation((0, s * C, 16)) @ Matrix.Rotation(-math.pi / 2 * s, 4, 'X') @ Matrix.Rotation(ang, 4, 'Z')
        add(lo, hi, 2, place)
    for sx in (-1, 1):          # forged corner caps
        for sy in (-1, 1):
            for sz in (-1, 1):
                c = 11.4 + rnd.uniform(-0.2, 0.2)
                o = 16.0
                lo = tuple(min(s * c, s * o) for s in (sx, sy, sz))
                hi = tuple(max(s * c, s * o) for s in (sx, sy, sz))
                add((lo[0], lo[1], lo[2] + 16), (hi[0], hi[1], hi[2] + 16), 3)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    bm.normal_update()
    crate_uv(bm, solids)
    blib.auto_sharp(bm, 30)
    return blib.obj_from_bm('crate', bm, ('c_plank', 'c_batten', 'c_brace', 'c_iron'))


def crate_uv(bm, solids, size=TEX):
    """Shelf-pack every visible face into the atlas at one texel density; hidden faces collapse."""
    uvl = blib.uv_layer(bm)

    def inside(p):
        for lo, hi in solids:
            if all(lo[i] + 0.01 < p[i] < hi[i] - 0.01 for i in range(3)):
                return True
        return False
    items = []
    for f in bm.faces:
        c = f.calc_center_median()
        if inside(c + f.normal * 0.05):
            for loop in f.loops:
                loop[uvl].uv = (0.998, 0.998)
            continue
        e = max(f.edges, key=lambda e: e.calc_length())
        t = (e.verts[1].co - e.verts[0].co).normalized()
        b = f.normal.cross(t).normalized()
        loc = [((v.co - c).dot(t), (v.co - c).dot(b)) for v in f.verts]
        u0, v0 = min(p[0] for p in loc), min(p[1] for p in loc)
        u1, v1 = max(p[0] for p in loc), max(p[1] for p in loc)
        items.append((f, loc, u0, v0, u1 - u0, v1 - v0))
    items.sort(key=lambda it: -it[5])
    for dens in [d / 10 for d in range(40, 10, -1)]:          # px per unit, highest that fits
        pad = 3
        placed, x, y, row = [], pad, pad, 0
        ok = True
        for it in items:
            wpx, hpx = math.ceil(it[4] * dens), math.ceil(it[5] * dens)
            if x + wpx + pad > size:
                x, y, row = pad, y + row + pad, 0
            if y + hpx + pad > size - 4:
                ok = False
                break
            placed.append((it, x, y, dens))
            x += wpx + pad
            row = max(row, hpx)
        if ok:
            break
    print(f'  crate atlas: {dens:.1f} px/unit')
    for (f, loc, u0, v0, w, h), px, py, d in placed:
        for loop, (a, b) in zip(f.loops, loc):
            loop[uvl].uv = ((px + (a - u0) * d) / size, (py + (b - v0) * d) / size)


def paint_crate(m):
    pos, idm, nrm = m['pos'], m['id'], m['nrm']
    x, y, z = pos[..., 0], pos[..., 1], pos[..., 2] - 16
    plank, batten, brace, iron = idm == 0, idm == 1, idm == 2, idm == 3
    rng = np.random.default_rng(81)
    ax = np.argmax(np.abs(nrm), -1)              # face axis
    # along/across for the walls: sides have horizontal planks, the lid runs along y
    along = np.where(ax == 2, y, np.where(ax == 0, y, x))
    across = np.where(ax == 2, x, z)
    face_id = ax * 2 + (np.take_along_axis(nrm, ax[..., None], -1)[..., 0] > 0)
    pw = 25.0 / 4
    k = np.floor((across + 12.5) / pw)
    fr = np.mod(across + 12.5, pw)
    gap = smoothstep(0.45, 0.0, np.minimum(fr, pw - fr))
    seed = k * 7.3 + face_id * 3.1
    tone = 0.78 + 0.4 * vnoise3(np.stack([seed, seed * 0.3, seed * 0.7], -1), 82)
    grain_p = np.stack([along * 0.16, across * 2.4 + seed * 5, face_id * 3.0], -1)
    grain = fbm3(grain_p, 4, 83)
    wood_dk, wood_lt = col(0.19, 0.14, 0.09), col(0.42, 0.32, 0.21)
    fib = vnoise3(np.stack([along * 0.35, across * 9.0 + seed * 3, face_id * 7.0], -1), 87)
    wood = lerp(wood_dk, wood_lt, np.clip(0.5 + (grain - 0.5) * 2.6 + (fib - 0.5) * 0.7, 0, 1)) * tone[..., None]
    # weathering: grey, sun-bleached towards the top of each plank and on the lid
    weather = smoothstep(0.45, 0.75, fbm3(pos * 0.2, 3, 84)) * 0.5 + (ax == 2) * 0.2
    wood = lerp(wood, col(0.34, 0.30, 0.25), weather * 0.5)
    knots = smoothstep(0.78, 0.84, fbm3(np.stack([along * 0.3, across * 1.0, face_id * 5.0], -1), 2, 85))
    wood = lerp(wood, col(0.08, 0.05, 0.03), knots * 0.8)
    wood = wood * (1 - 0.8 * gap)[..., None]
    # nails at the plank ends (iron heads with rust halos)
    nail = np.zeros_like(z)
    halo = np.zeros_like(z)
    for e in (-11.2, 11.2):
        for o in (-1.5, 1.5):
            d = np.hypot(along - e, fr - pw / 2 - o * 0.9)
            nail = np.maximum(nail, smoothstep(0.5, 0.3, d))
            halo = np.maximum(halo, smoothstep(1.4, 0.4, d))
    out = lerp(wood, col(0.25, 0.10, 0.04), halo * 0.6 * plank)
    out = lerp(out, col(0.12, 0.10, 0.09), nail * plank)
    # burnt brand (circle + cross) on two walls, crude, charred
    for fid, ca, cb in ((1, -3.0, 2.0), (2, 4.0, -1.0)):
        bx, by = along - ca, across - cb
        brand = strokes(bx, by, [(-3.0, 0, 3.0, 0), (0, -3.0, 0, 3.0)], 0.45, 86 + fid)
        ring = smoothstep(0.6, 0.2, np.abs(np.hypot(*wobble(bx, by, 88 + fid, 0.3)) - 3.6))
        bmask = np.clip(brand + ring, 0, 1) * (face_id == fid) * plank
        bmask = bmask * (0.6 + 0.4 * fbm3(pos * 1.2, 2, 90))
        out = lerp(out, col(0.04, 0.03, 0.02), bmask * 0.85)
    # battens and braces: darker, grain along their length
    b_along = np.where(np.abs(x) < np.minimum(np.abs(y), np.abs(z)), x, np.where(np.abs(y) < np.abs(z), y, z))
    bg = fbm3(np.stack([b_along * 0.18, (x + y + z - b_along) * 1.7, np.zeros_like(x)], -1), 4, 91)
    bwood = lerp(col(0.14, 0.10, 0.065), col(0.33, 0.24, 0.16), np.clip(0.5 + (bg - 0.5) * 2.2, 0, 1))
    out = np.where(batten[..., None], bwood, out)
    diag = (along + across) * 0.7071
    dg = fbm3(np.stack([diag * 0.18, (along - across) * 1.7, face_id * 2.0], -1), 4, 92)
    dwood = lerp(col(0.16, 0.115, 0.075), col(0.36, 0.27, 0.17), np.clip(0.5 + (dg - 0.5) * 2.2, 0, 1))
    out = np.where(brace[..., None], dwood, out)
    # forged iron caps: dark, hammered, rusty, with two nail heads per face
    ironc = mottle(pos, col(0.10, 0.085, 0.075), col(0.24, 0.19, 0.15), 93, 0.4)
    ironc = lerp(ironc, mottle(pos, col(0.26, 0.11, 0.05), col(0.42, 0.20, 0.08), 94, 0.7),
                 smoothstep(0.45, 0.7, fbm3(pos * 0.5, 3, 95)) * 0.8)
    ironc = ironc * (1 - 0.3 * pits(pos, 96))[..., None]
    out = np.where(iron[..., None], ironc, out)
    # stains: water/oil, darker towards the bottom; drips from the iron caps
    st = smoothstep(0.55, 0.78, fbm3(pos * 0.14 + 3, 4, 97))
    out = lerp(out, col(0.06, 0.045, 0.03), st * 0.55 * (1 - iron))
    g = smoothstep(-8, -16, z) * (0.6 + 0.4 * fbm3(pos * 0.4, 3, 98))
    out = lerp(out, col(0.06, 0.05, 0.04), g * 0.5)
    streak = fbm3(pos * np.array([0.4, 0.4, 0.05]), 3, 100)
    out = out * (1 - 0.3 * smoothstep(0.5, 0.75, streak) * (ax != 2))[..., None]
    out = lerp(out, col(0.30, 0.28, 0.25), smoothstep(0.6, 0.8, fbm3(pos * 0.25 + 9, 3, 101)) * 0.25 * (1 - iron))
    out = out * (0.8 + 0.4 * grit(pos, 99))[..., None]
    out = q3_light(out, m, 0.75, 0.2, 0.5)
    return out


# =====================================================================================  drivers
def bake(ob, **kw):
    return blib.bake_maps(ob, BAKE, **kw)


def do_barrel():
    propenv()
    ob = build_barrel()
    rgb, alpha, glow = paint_barrel(bake(ob, ao_dist=8.0, bevel_r=0.5))
    save('models/rearmed/props/barrel.tga', rgb, alpha)
    save('models/rearmed/props/barrel_glow.tga', glow)
    finish(ob, 'models/rearmed/props/barrel.md3', 'models/rearmed/props/barrel')


def do_tank():
    propenv()
    ob = build_tank()
    rgb, alpha, glow = paint_tank(bake(ob, ao_dist=6.0, bevel_r=0.4))
    save('models/rearmed/props/tank.tga', rgb, alpha)
    save('models/rearmed/props/tank_glow.tga', glow)
    finish(ob, 'models/rearmed/props/tank.md3', 'models/rearmed/props/tank')


def do_crate():
    ob = build_crate()
    save('models/rearmed/props/crate.tga', paint_crate(bake(ob, ao_dist=6.0, bevel_r=0.5)))
    finish(ob, 'models/rearmed/props/crate.md3', 'models/rearmed/props/crate')
