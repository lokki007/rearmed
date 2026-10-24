"""REARMED v4 models: procedural geometry + baked/painted textures -> MD3 + TGA under assets/.

Run (from anywhere):
  blender --background --factory-startup --python tools/blender/make_models.py -- [names...]
names: barrel crate tank rock metal wood gibs head meteor (default: all). See build_all.sh.
"""
import math
import os
import random
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector, noise

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blib  # noqa: E402
from blib import col, fbm3, lerp, smoothstep, vnoise3  # noqa: E402
import make_props  # noqa: E402

BUILD = os.path.join(blib.HERE, 'build')
STATS = {}


def finish(ob, rel_md3, shader, sharp=40.0):
    """Triangulate, export, record the triangle count."""
    blib.triangulate(ob)
    n = blib.export_md3(rel_md3, [('body', ob, shader)])
    STATS[rel_md3] = n
    return n


def shade_common(rgb, m, ao_k=0.65, top=0.12):
    ao = m['ao']
    rgb = rgb * (1 - ao_k + ao_k * ao)[..., None]
    rgb = rgb * (1 - top * 0.5 + top * m['nrm'][..., 2])[..., None]
    return np.clip(rgb, 0, 1)


def edge_wear(m, scale=0.6, seed=2, amount=0.7, lo=0.55, hi=0.62):
    """(chips, halo) masks: paint chipped off at convex edges."""
    convex = np.clip((1 - m['edge']) * 6, 0, 1) * smoothstep(0.55, 0.9, m['ao'])
    nz = fbm3(m['pos'] * scale, 4, seed)
    s = nz * 0.6 + convex * amount
    chips = smoothstep(lo, lo + 0.07, s)
    halo = np.clip(smoothstep(lo - 0.1, lo, s) - chips, 0, 1)
    return chips, halo


# =====================================================================================  PROPS
# barrel / tank / crate live in make_props.py (id-1999 style rebuild, ds-art 2026-10-02)


# =====================================================================================  generic shape helpers
def grid_verts(bm, xs, ys):
    """create_grid, normalised to x in [-1,1] * xs/2... returns verts spanning [-xs/2, xs/2] x [-ys/2, ys/2]."""
    res = bmesh.ops.create_grid(bm, x_segments=xs[0], y_segments=ys[0], size=1.0)
    vs = res['verts']
    mx = max(abs(v.co.x) for v in vs)
    my = max(abs(v.co.y) for v in vs)
    for v in vs:
        v.co.x = v.co.x / mx * xs[1] / 2
        v.co.y = v.co.y / my * ys[1] / 2
    return vs


def lumpy(bm, center, radii, subdiv=3, seed=0, rough=0.25, freq=0.35, mat=0):
    nv, nf = len(bm.verts), len(bm.faces)
    bmesh.ops.create_icosphere(bm, subdivisions=subdiv, radius=1.0)
    bm.verts.ensure_lookup_table()
    bm.faces.ensure_lookup_table()
    new = bm.verts[nv:]
    off = Vector((seed * 13.1, seed * 7.7, seed * 3.3))
    for v in new:
        d = v.co.normalized()
        p = Vector((d.x * radii[0], d.y * radii[1], d.z * radii[2]))
        k = 1 + rough * noise.noise(p * freq + off) + rough * 0.5 * noise.noise(p * freq * 2.3 + off)
        v.co = Vector(center) + p * k
    for f in bm.faces[nf:]:
        f.material_index = mat
    return new


def tube(bm, path, radius, segs, mat=0, cap0=True, cap1=True, cap_mat=None, jag=0.0, rnd=None):
    """Sweep a circle along path points; radius(t) or float."""
    rings = []
    n = len(path)
    for i, p in enumerate(path):
        p = Vector(p)
        tng = (Vector(path[min(i + 1, n - 1)]) - Vector(path[max(i - 1, 0)])).normalized()
        ref = Vector((0, 0, 1)) if abs(tng.z) < 0.9 else Vector((1, 0, 0))
        side = tng.cross(ref).normalized()
        up = side.cross(tng).normalized()
        r = radius(i / (n - 1)) if callable(radius) else radius
        ring = []
        for j in range(segs):
            a = 2 * math.pi * j / segs
            q = p + (side * math.cos(a) + up * math.sin(a)) * r
            if jag and rnd and (i == 0 or i == n - 1):
                q += tng * (rnd.uniform(-jag, jag))
            ring.append(bm.verts.new(q))
        rings.append(ring)
    for i in range(n - 1):
        for j in range(segs):
            j2 = (j + 1) % segs
            f = bm.faces.new((rings[i][j], rings[i][j2], rings[i + 1][j2], rings[i + 1][j]))
            f.material_index = mat
    for ring, flip in ((rings[0], True), (rings[-1], False)):
        if (flip and not cap0) or (not flip and not cap1):
            continue
        f = bm.faces.new(list(reversed(ring)) if flip else ring)
        f.material_index = mat if cap_mat is None else cap_mat
    return rings


def center_origin(ob):
    me = ob.data
    xs = [v.co for v in me.vertices]
    mn = Vector((min(c.x for c in xs), min(c.y for c in xs), min(c.z for c in xs)))
    mx = Vector((max(c.x for c in xs), max(c.y for c in xs), max(c.z for c in xs)))
    c = (mn + mx) / 2
    for v in me.vertices:
        v.co -= c


def fix_normals(bm):
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])


# =====================================================================================  GIBS
MEAT, BONE, FAT, MARROW = 0, 1, 2, 3
GIB_MATS = ('g_meat', 'g_bone', 'g_fat', 'g_marrow')


def gib_bone(bm, p0, p1, r, knob=True, rnd=None, mat_cap=MARROW):
    """Bone shaft from p0 (broken end) to p1 (knob end)."""
    p0, p1 = Vector(p0), Vector(p1)
    path = [p0.lerp(p1, t) for t in (0, 0.25, 0.5, 0.75, 0.9, 1.0)]

    def rad(t):
        if not knob:
            return r
        return r * (1.0 + 0.9 * smooth01((t - 0.62) / 0.3)) if t < 0.99 else r * 1.3
    rings = tube(bm, path, rad, 8, BONE, cap0=True, cap1=True, cap_mat=mat_cap, jag=r * 0.8, rnd=rnd)
    if knob:
        # rounded knob end: pull last ring's cap center outward
        last = rings[-1]
        ctr = sum((v.co for v in last), Vector()) / len(last)
        d = (p1 - p0).normalized()
        f = [f for f in bm.faces if set(f.verts) == set(last)]
        if f:
            res = bmesh.ops.poke(bm, faces=f)
            for v in res['verts']:
                v.co = ctr + d * r * 1.0
            for ff in res['faces']:
                ff.material_index = BONE
    return rings


def smooth01(x):
    x = min(max(x, 0.0), 1.0)
    return x * x * (3 - 2 * x)


def build_gib(i):
    blib.clear_objects()
    rnd = random.Random(1000 + i)
    bm = bmesh.new()
    if i == 1:     # big lumpy chunk with a fat seam
        lumpy(bm, (0, 0, 0), (5.2, 3.6, 3.0), 3, 1, 0.3, 0.45, MEAT)
    elif i == 2:   # chunk with a bone sticking out
        lumpy(bm, (0, 0, 0), (3.8, 3.0, 2.8), 3, 2, 0.3, 0.5, MEAT)
        gib_bone(bm, (0.5, 0.2, 0.0), (7.5, 1.0, 1.2), 0.75, knob=True, rnd=rnd)
    elif i == 3:   # rib rack: two curved ribs joined by a torn sheet of intercostal meat
        ts = np.linspace(0.15, 1.75, 10)

        def rib(t, off, k):
            return Vector((math.cos(t) * 6.5 - 3.5, math.sin(t) * 6.5 - 3.0, off + 0.25 * math.sin(t * 4 + k)))
        for k, off in enumerate((-1.7, 1.7)):
            tube(bm, [rib(t, off, k) for t in ts], lambda t: 0.5 + 0.08 * math.sin(t * 9), 7, BONE,
                 cap_mat=MARROW, jag=0.4, rnd=rnd)
        # meat sheet: rows across (between the ribs), columns along; thick in the middle, ragged ends
        cols = [t for t in np.linspace(0.3, 1.6, 8)]
        nv = len(bm.verts)
        grid = []
        for ci, t in enumerate(cols):
            row = []
            for rj, w in enumerate(np.linspace(-1.0, 1.0, 5)):
                a_ = rib(t, -1.7, 0).lerp(rib(t, 1.7, 1), (w + 1) / 2)
                inward = Vector((-math.cos(t), -math.sin(t), 0)) * (0.45 + 0.5 * (1 - w * w))
                bulge = Vector((math.cos(t), math.sin(t), 0)) * 0.25 * noise.noise(a_ * 0.9)
                row.append(bm.verts.new(a_ + inward * 0.4 + bulge))
            grid.append(row)
        for ci in range(len(cols) - 1):
            for rj in range(4):
                f = bm.faces.new((grid[ci][rj], grid[ci + 1][rj], grid[ci + 1][rj + 1], grid[ci][rj + 1]))
                f.material_index = MEAT
        bm.faces.ensure_lookup_table()
        sheet = [f for f in bm.faces if f.material_index == MEAT]
        ext = bmesh.ops.extrude_face_region(bm, geom=sheet, use_keep_orig=True)
        for e in ext['geom']:
            if isinstance(e, bmesh.types.BMVert):
                d = Vector((e.co.x + 3.5, e.co.y + 3.0, 0)).normalized()
                e.co -= d * (0.9 + rnd.uniform(0, 0.4))
        for f in bm.faces:
            if f.material_index != BONE and f.material_index != MARROW:
                f.material_index = MEAT
    elif i == 4:   # torn flap of skin + meat
        top = set(grid_verts(bm, (7, 8.0), (5, 6.0)))
        for v in top:
            v.co.z = 0.9 * math.sin(v.co.x * 0.5) + 0.5 * math.cos(v.co.y * 0.8)
            bnd = abs(v.co.x) > 3.9 or abs(v.co.y) > 2.9
            if bnd:
                v.co.x += rnd.uniform(-0.5, 0.5)
                v.co.y += rnd.uniform(-0.5, 0.5)
        ext = bmesh.ops.extrude_face_region(bm, geom=bm.faces[:], use_keep_orig=True)
        newv = [e for e in ext['geom'] if isinstance(e, bmesh.types.BMVert)]
        for v in newv:
            v.co.z -= 0.7 + rnd.uniform(-0.15, 0.2)
        for f in bm.faces:
            f.material_index = MEAT
        bm.normal_update()
        fix_normals(bm)
        for f in bm.faces:
            if f.normal.z > 0.5 and all(v in top for v in f.verts):
                f.material_index = FAT     # skin/fat side up
    elif i == 5:   # small chunk, half fat
        vs = lumpy(bm, (0, 0, 0), (2.6, 2.1, 2.0), 3, 5, 0.35, 0.6, MEAT)
        for f in bm.faces:
            if f.calc_center_median().z > 0.8 + 0.4 * math.sin(f.calc_center_median().x):
                f.material_index = FAT
    elif i == 6:   # bone end (joint knob) wrapped in meat
        gib_bone(bm, (-4.0, 0, 0), (4.2, 0.3, 0.4), 1.0, knob=True, rnd=rnd)
        lumpy(bm, (-1.5, 0, 0.1), (2.6, 1.9, 1.8), 3, 6, 0.35, 0.6, MEAT)
    fix_normals(bm)
    blib.auto_sharp(bm, 75)
    ob = blib.obj_from_bm('meat%d' % i, bm, GIB_MATS)
    center_origin(ob)
    blib.smart_uv(ob, 60, 0.02)
    blib.uv_fit(ob, (0, 0, 1, 1), 0.01)
    return ob


def paint_gib(m, seed=0):
    pos, idm = m['pos'], m['id']
    n1 = fbm3(pos * 0.9 + seed, 4, 41)
    fib = fbm3(pos * np.array([0.4, 2.5, 2.5]) + seed, 3, 42)       # muscle fibres
    meat = lerp(col(0.44, 0.04, 0.045), col(0.7, 0.11, 0.1), smoothstep(0.35, 0.7, fib))
    meat = lerp(meat, col(0.16, 0.0, 0.01), smoothstep(0.55, 0.75, n1) * 0.8)    # clots
    fatspots = smoothstep(0.66, 0.72, fbm3(pos * 1.4 + 7 + seed, 3, 43))
    meat = lerp(meat, col(0.86, 0.74, 0.5), fatspots * 0.85)
    veins = smoothstep(0.03, 0.0, np.abs(fbm3(pos * 0.7 + seed, 3, 44) - 0.5))
    meat = lerp(meat, col(0.25, 0.02, 0.06), veins * 0.6)
    bone = lerp(col(0.88, 0.83, 0.7), col(0.7, 0.62, 0.48), fbm3(pos * 1.5, 3, 45))
    bone = lerp(bone, col(0.55, 0.05, 0.05), smoothstep(0.55, 0.8, fbm3(pos * 0.8 + 3, 3, 46)) * 0.7)
    fat = lerp(col(0.9, 0.78, 0.55), col(0.8, 0.35, 0.3), smoothstep(0.3, 0.7, n1))
    marrow = lerp(col(0.5, 0.12, 0.08), col(0.75, 0.55, 0.35), smoothstep(0.4, 0.6, fbm3(pos * 4, 2, 47)))
    out = meat
    out = np.where((idm == BONE)[..., None], bone, out)
    out = np.where((idm == FAT)[..., None], fat, out)
    out = np.where((idm == MARROW)[..., None], marrow, out)
    # everything is smeared with blood
    smear = smoothstep(0.45, 0.7, fbm3(pos * 0.5 + 11, 3, 48))
    out = lerp(out, col(0.35, 0.01, 0.02), smear * 0.5 * (idm != MEAT))
    return shade_common(out, m, 0.75, 0.1)


# =====================================================================================  HEAD
HEAD_SCALE = (5.0, 4.3, 6.4)
# features as unit-sphere directions (x forward, y left, z up), shared by the builder and painter
H_EYES = [(0.82, 0.36, 0.14), (0.82, -0.36, 0.14)]
H_BROW = [(0.8, 0.36, 0.36), (0.8, -0.36, 0.36)]
H_CHEEK = [(0.62, 0.62, -0.12), (0.62, -0.62, -0.12)]
H_NOSE = (0.97, 0.0, -0.1)
H_EARS = [(0.0, 1.0, 0.02), (0.0, -1.0, 0.02)]
H_CHIN = (0.78, 0.0, -0.6)
H_MOUTH = (0.86, 0.0, -0.44)
H_CUT = -0.66


def build_head():
    blib.clear_objects()
    rnd = random.Random(77)
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=24, v_segments=18, radius=1.0)

    def g(d, c, w):
        return math.exp(-((d - Vector(c).normalized()).length_squared) / (w * w))
    for v in bm.verts:
        d = v.co.normalized()
        x, y, z = d
        sx, sy, sz = HEAD_SCALE
        if x > 0:
            sx *= 1.0 - 0.1 * x
        if z < -0.1:                       # jaw narrows
            sy *= 1.0 - 0.45 * (-z - 0.1)
            sx *= 1.0 + 0.12 * max(x, 0)
        if x < 0 and z > -0.2:             # back of the skull bulges
            sx *= 1.08
        p = Vector((x * sx, y * sy, z * sz))
        fwd = Vector((1, 0, 0))
        for e in H_EYES:
            p -= fwd * 1.5 * g(d, e, 0.22)
        for e in H_BROW:
            p += fwd * 0.35 * g(d, e, 0.18)
        for c in H_CHEEK:
            p += Vector((0.2, 0.3 * (1 if c[1] > 0 else -1), 0)) * 0.6 * g(d, c, 0.2)
        p += fwd * 0.75 * g(d, H_NOSE, 0.17)
        p -= fwd * 0.45 * g(d, H_MOUTH, 0.12)
        p += fwd * 0.5 * g(d, H_CHIN, 0.16)
        for e in H_EARS:
            p += Vector((-0.3, e[1], 0)) * 0.7 * g(d, e, 0.14)
        v.co = p
    # neck: chop the bottom off and make a ragged stump
    cut = [v for v in bm.verts if v.co.z < H_CUT * HEAD_SCALE[2]]
    bmesh.ops.delete(bm, geom=cut, context='VERTS')
    edges = [e for e in bm.edges if e.is_boundary]
    res = bmesh.ops.holes_fill(bm, edges=edges, sides=0)
    pk = bmesh.ops.poke(bm, faces=res['faces'])
    for v in pk['verts']:
        v.co.z += 0.9
        v.co.x -= 0.4
    bnd = set(v for f in pk['faces'] for v in f.verts) - set(pk['verts'])
    for v in bnd:
        v.co.z += rnd.uniform(-0.8, 0.5)
        v.co.x *= 0.95
        v.co.y *= 0.95
    for f in bm.faces:
        f.material_index = 0
    for f in pk['faces']:
        f.material_index = 1
    fix_normals(bm)
    blib.auto_sharp(bm, 85)
    ob = blib.obj_from_bm('head', bm, ('h_skin', 'h_stump'))
    blib.smart_uv(ob, 50, 0.02)
    blib.uv_fit(ob, (0, 0, 1, 1), 0.01)
    return ob


def paint_head(m):
    pos, idm, nrm = m['pos'], m['id'], m['nrm']
    x, y, z = pos[..., 0], pos[..., 1], pos[..., 2]
    d = pos / np.array(HEAD_SCALE, np.float32)
    d /= np.linalg.norm(d, axis=-1, keepdims=True) + 1e-6

    def near(c, w):
        c = np.array(c, np.float32)
        c /= np.linalg.norm(c)
        return np.exp(-((d - c) ** 2).sum(-1) / (w * w))
    n1 = fbm3(pos * 0.8, 4, 51)
    skin = lerp(col(0.74, 0.6, 0.5), col(0.56, 0.42, 0.37), n1)
    skin = lerp(skin, col(0.55, 0.62, 0.55), smoothstep(0.55, 0.8, fbm3(pos * 0.35, 3, 52)) * 0.35)   # pallor
    # cropped dark hair on the scalp
    hair = smoothstep(0.35, 0.55, d[..., 2] - 0.35 * np.clip(d[..., 0], 0, 1) + 0.15 * np.clip(-d[..., 0], 0, 1))
    hair *= 0.75 + 0.25 * fbm3(pos * 3, 2, 53)
    skin = lerp(skin, col(0.1, 0.08, 0.07), np.clip(hair, 0, 1) * 0.92)
    # dark sockets, a grim mouth with teeth
    sock = np.maximum(near(H_EYES[0], 0.2), near(H_EYES[1], 0.2))
    skin = lerp(skin, col(0.3, 0.12, 0.12), smoothstep(0.1, 0.4, sock) * 0.7)     # bruised rims
    skin = lerp(skin, col(0.06, 0.01, 0.01), smoothstep(0.35, 0.65, sock))
    glint = np.maximum(near((0.86, 0.33, 0.13), 0.05), near((0.86, -0.39, 0.13), 0.05))
    skin = lerp(skin, col(0.85, 0.8, 0.7), smoothstep(0.5, 0.9, glint) * 0.8)     # dead eyes catch the light
    mo = np.abs(d[..., 2] - H_MOUTH[2] + 0.25 * d[..., 1] ** 2)
    mouth = smoothstep(0.07, 0.03, mo) * smoothstep(0.42, 0.3, np.abs(d[..., 1])) * (d[..., 0] > 0.5)
    skin = lerp(skin, col(0.06, 0.0, 0.0), mouth)
    teeth = mouth * smoothstep(0.035, 0.015, mo) * (np.mod(d[..., 1] * 14.0, 1) > 0.2)
    skin = lerp(skin, col(0.85, 0.8, 0.62), teeth * 0.85)
    # torn open skull at the top-back: bone + dark cracks, ringed by torn scalp
    hole = near((-0.3, 0.3, 0.9), 0.38) * (0.8 + 0.4 * fbm3(pos * 1.3, 2, 60))
    bonec = lerp(col(0.86, 0.8, 0.66), col(0.62, 0.52, 0.4), fbm3(pos * 2.0, 3, 58))
    crk = smoothstep(0.035, 0.0, np.abs(fbm3(pos * 0.9, 3, 59) - 0.5)) * smoothstep(0.55, 0.8, hole)
    bonec = lerp(bonec, col(0.25, 0.02, 0.02), crk)
    rim = smoothstep(0.3, 0.45, hole) * smoothstep(0.7, 0.5, hole)
    # blood: runs streaming down from the wound + the gash, the neck soaked from below
    gash = smoothstep(0.06, 0.02, np.abs(d[..., 1] + 0.25 * np.sin(d[..., 0] * 4) + 0.15)) * smoothstep(0.35, 0.6, d[..., 2]) * (d[..., 0] > -0.1)
    streak = smoothstep(0.5, 0.66, fbm3(pos * np.array([1.4, 1.4, 0.06]), 3, 54))
    src = np.clip(near((-0.3, 0.3, 0.9), 0.6) * 1.5 + near((0.6, -0.15, 0.6), 0.45), 0, 1)
    runs = streak * src * smoothstep(-0.5, 0.3, d[..., 2])
    soak = smoothstep(-0.25, -0.62, d[..., 2]) * (0.55 + 0.45 * fbm3(pos * 0.8, 3, 55))
    spots = smoothstep(0.68, 0.74, fbm3(pos * 0.9 + 4, 3, 57))
    blood = np.clip(gash + runs + soak + spots * 0.8 + rim, 0, 1)
    skin = lerp(skin, lerp(col(0.55, 0.02, 0.03), col(0.24, 0.0, 0.01), n1), blood * 0.93)
    skin = lerp(skin, bonec, smoothstep(0.5, 0.62, hole))
    # stump: raw meat, spine + windpipe cross-sections
    rr = np.hypot(x + 1.0, y)
    stump = lerp(col(0.5, 0.04, 0.05), col(0.75, 0.15, 0.13), smoothstep(0.3, 0.7, fbm3(pos * 2, 3, 56)))
    stump = lerp(stump, col(0.88, 0.82, 0.66), smoothstep(1.0, 0.8, rr))                  # vertebra
    stump = lerp(stump, col(0.3, 0.02, 0.03), smoothstep(0.4, 0.25, rr))                  # cord
    wp = np.hypot(x - 1.5, y)
    stump = lerp(stump, col(0.85, 0.6, 0.55), smoothstep(0.75, 0.55, wp) * smoothstep(0.25, 0.4, wp))
    stump = lerp(stump, col(0.12, 0.0, 0.0), smoothstep(0.32, 0.22, wp))
    out = np.where((idm == 1)[..., None], stump, skin)
    return shade_common(out, m, 0.7, 0.14)


# =====================================================================================  METEOR
def meteor_points():
    # evenly spread cells (Fibonacci sphere + jitter) so no tiny cell turns into a solid glowing blob
    r = np.random.default_rng(99)
    n = 26
    i = np.arange(n) + 0.5
    phi = np.arccos(1 - 2 * i / n)
    th = math.pi * (1 + 5 ** 0.5) * i
    p = np.stack([np.cos(th) * np.sin(phi), np.sin(th) * np.sin(phi), np.cos(phi)], -1)
    p += r.normal(0, 0.2, p.shape)
    p /= np.linalg.norm(p, axis=1, keepdims=True)
    return p * 14.0


def build_meteor():
    blib.clear_objects()
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=4, radius=14.0)
    pts = meteor_points()
    co = np.array([v.co[:] for v in bm.verts], np.float32)
    f1, f2 = blib.voronoi_f1f2(co, pts)
    crack = smoothstep(1.6, 0.3, f2 - f1)
    big = np.array([noise.noise(Vector(c) * 0.07) for c in co])
    small = np.array([noise.noise(Vector(c) * 0.22 + Vector((5, 5, 5))) for c in co])
    rad = 14.0 * (1 + 0.22 * big + 0.08 * small) - 1.4 * crack
    rad *= 1.0 + 0.05 * np.clip(f1 / 7.0, 0, 1)     # plates bulge
    for v, rr, c in zip(bm.verts, rad, co):
        d = Vector(c).normalized()
        v.co = d * rr
        v.co.z *= 0.88
    fix_normals(bm)
    blib.auto_sharp(bm, 55)
    ob = blib.obj_from_bm('meteor', bm, ('m_rock',))
    blib.smart_uv(ob, 45, 0.015)
    blib.uv_fit(ob, (0, 0, 1, 1), 0.005)
    return ob


def paint_meteor(m):
    pos = m['pos']
    q = pos.copy()
    q[..., 2] /= 0.88
    d = q / (np.linalg.norm(q, axis=-1, keepdims=True) + 1e-6) * 14.0
    f1, f2 = blib.voronoi_f1f2(d, meteor_points())
    e = f2 - f1
    wob = fbm3(pos * 0.5, 3, 61)
    crack = smoothstep(1.1 + 0.6 * wob, 0.15, e)
    core = smoothstep(0.55, 0.05, e)
    fine = smoothstep(0.02, 0.0, np.abs(fbm3(pos * 0.35, 3, 62) - 0.5)) * 0.8   # thin secondary veins
    glowmask = np.clip(np.maximum(crack, fine * (1 - smoothstep(4, 6, e))), 0, 1)
    rock = lerp(col(0.2, 0.18, 0.17), col(0.09, 0.08, 0.08), fbm3(pos * 0.6, 4, 63))
    rock = lerp(rock, col(0.32, 0.28, 0.25), smoothstep(0.6, 0.75, fbm3(pos * 1.7, 3, 64)) * 0.6)
    rock = lerp(rock, col(0.35, 0.12, 0.04), smoothstep(3.0, 1.0, e) * 0.5)     # heat tint near cracks
    rock = lerp(rock, col(0.06, 0.02, 0.01), glowmask)
    albedo = shade_common(rock, m, 0.6, 0.1)
    glow = lerp(col(0.55, 0.08, 0.0), col(1.0, 0.55, 0.1), core)
    glow = lerp(glow, col(1.0, 0.9, 0.55), smoothstep(0.2, 0.0, e))
    glow = glow * glowmask[..., None] * (0.75 + 0.25 * m['ao'])[..., None]
    return albedo, glow


# =====================================================================================  DEBRIS
def build_rock(i):
    blib.clear_objects()
    rnd = random.Random(300 + i)
    bm = bmesh.new()
    dims = {1: (4.5, 3.4, 2.8), 2: (2.8, 2.3, 2.0), 3: (5.0, 3.6, 1.8)}[i]
    pts = []
    for _ in range(22):
        v = Vector((rnd.gauss(0, 1), rnd.gauss(0, 1), rnd.gauss(0, 1))).normalized()
        k = rnd.uniform(0.75, 1.0)
        pts.append(bm.verts.new((v.x * dims[0] * k, v.y * dims[1] * k, v.z * dims[2] * k)))
    res = bmesh.ops.convex_hull(bm, input=pts)
    bmesh.ops.delete(bm, geom=list({g for g in res['geom_interior'] + res['geom_unused'] if isinstance(g, bmesh.types.BMVert)}), context='VERTS')
    bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=1, use_grid_fill=True)
    for v in bm.verts:
        v.co += v.co.normalized() * 0.25 * noise.noise(v.co * 0.6 + Vector((i, i, i)))
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    fix_normals(bm)
    blib.auto_sharp(bm, 35)
    blib.box_uv(bm, 16.0)
    return blib.obj_from_bm('rock%d' % i, bm, ('d_rock',))


def build_metal(i):
    blib.clear_objects()
    rnd = random.Random(400 + i)
    bm = bmesh.new()
    if i == 1:     # twisted strip of shrapnel
        for v in grid_verts(bm, (9, 9.0), (2, 2.6)):
            x, y = v.co.x, v.co.y
            t = (x + 4.5) / 9.0
            a = t * 1.6 + 0.3 * math.sin(t * 7)
            y2, z2 = y * math.cos(a), y * math.sin(a)
            v.co = Vector((x, y2, z2 + 1.2 * math.sin(t * 3.1)))
            if abs(x) > 4.2:
                v.co.x += rnd.uniform(-0.9, 0.5) * (1 if x > 0 else -1)
    else:          # torn, folded plate
        for v in grid_verts(bm, (5, 6.4), (5, 6.0)):
            x, y = v.co.x, v.co.y
            fold = max(0.0, x + 0.4) * 0.9
            v.co = Vector((x - fold * 0.3, y, fold + 0.3 * math.sin(y * 1.5)))
            if abs(v.co.y) > 2.9 or abs(x) > 3.1:
                v.co += Vector((rnd.uniform(-0.6, 0.6), rnd.uniform(-0.6, 0.6), rnd.uniform(-0.3, 0.3)))
    ext = bmesh.ops.extrude_face_region(bm, geom=bm.faces[:], use_keep_orig=True)
    for e in ext['geom']:
        if isinstance(e, bmesh.types.BMVert):
            e.co.z -= 0.35
    fix_normals(bm)
    blib.auto_sharp(bm, 40)
    blib.box_uv(bm, 16.0)
    ob = blib.obj_from_bm('metal%d' % i, bm, ('d_metal',))
    center_origin(ob)
    return ob


def build_wood(i):
    blib.clear_objects()
    rnd = random.Random(500 + i)
    bm = bmesh.new()
    L, W, T = {1: (10.0, 3.0, 1.0), 2: (9.0, 0.9, 0.7), 3: (5.0, 3.2, 2.4)}[i]
    segs = 5 if i != 3 else 3
    # box along X with jagged splintered ends
    xs = [-L / 2 + L * k / segs for k in range(segs + 1)]
    prof = []
    for k, x in enumerate(xs):
        taper = 1.0 if i != 2 else (1.0 - 0.8 * k / segs)
        prof.append([Vector((x, sy * W / 2 * taper, sz * T / 2 * taper)) for sy, sz in ((-1, -1), (1, -1), (1, 1), (-1, 1))])
    rings = [[bm.verts.new(p) for p in ring] for ring in prof]
    for k in range(segs):
        for j in range(4):
            j2 = (j + 1) % 4
            bm.faces.new((rings[k][j], rings[k][j2], rings[k + 1][j2], rings[k + 1][j]))
    for ring, flip in ((rings[0], True), (rings[-1], False)):
        f = bm.faces.new(list(reversed(ring)) if flip else ring)
        pk = bmesh.ops.poke(bm, faces=[f])
        for v in pk['verts']:     # splinter spike
            v.co.x += (-1 if flip else 1) * rnd.uniform(0.8, 2.2)
            v.co.y += rnd.uniform(-W, W) * 0.3
        for v in ring:
            v.co.x += (-1 if flip else 1) * rnd.uniform(-0.6, 0.9)
    for v in bm.verts:
        v.co.z += 0.15 * math.sin(v.co.x * 0.7 + i)
    fix_normals(bm)
    blib.auto_sharp(bm, 35)
    blib.box_uv(bm, 12.0, long_axis=0)
    ob = blib.obj_from_bm('wood%d' % i, bm, ('d_wood',))
    center_origin(ob)
    return ob


# =====================================================================================  driver
def bake_and_paint(ob, size, painter, tex_rel, **kw):
    maps = blib.bake_maps(ob, size, **kw)
    res = painter(maps)
    if isinstance(res, tuple):
        return res
    blib.save_tga(tex_rel, res)
    return res


def _props(fn):
    def run():
        fn()
        STATS.update(make_props.STATS)
    return run


do_barrel = _props(make_props.do_barrel)
do_crate = _props(make_props.do_crate)
do_tank = _props(make_props.do_tank)


def do_gibs():
    for i in range(1, 7):
        ob = build_gib(i)
        bake_and_paint(ob, 256, lambda m: paint_gib(m, i * 3.7), 'models/rearmed/gibs/meat%d.tga' % i, ao_dist=2.5, bevel_r=0.25)
        finish(ob, 'models/rearmed/gibs/meat%d.md3' % i, 'models/rearmed/gibs/meat%d' % i)


def do_head():
    ob = build_head()
    bake_and_paint(ob, 512, paint_head, 'models/rearmed/gibs/head.tga', ao_dist=3.0, bevel_r=0.3)
    finish(ob, 'models/rearmed/gibs/head.md3', 'models/rearmed/gibs/head')


def do_meteor():
    ob = build_meteor()
    maps = blib.bake_maps(ob, 512, ao_dist=6.0, bevel_r=0.6)
    albedo, glow = paint_meteor(maps)
    blib.save_tga('models/rearmed/meteor.tga', albedo)
    blib.save_tga('models/rearmed/meteor_glow.tga', glow)
    finish(ob, 'models/rearmed/meteor.md3', 'models/rearmed/meteor')


def do_debris():
    for i in (1, 2, 3):
        finish(build_rock(i), 'models/rearmed/debris/rock%d.md3' % i, 'models/rearmed/debris/rock')
        finish(build_wood(i), 'models/rearmed/debris/wood%d.md3' % i, 'models/rearmed/debris/wood')
    for i in (1, 2):
        finish(build_metal(i), 'models/rearmed/debris/metal%d.md3' % i, 'models/rearmed/debris/metal')


JOBS = {'barrel': do_barrel, 'crate': do_crate, 'tank': do_tank, 'gibs': do_gibs, 'head': do_head,
        'meteor': do_meteor, 'debris': do_debris}


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    names = [n for n in argv if n in JOBS] if argv else list(JOBS)   # other names (loot) belong to other scripts
    blib.setup_scene()
    for n in names:
        print('==== building', n)
        JOBS[n]()
    print('==== triangle counts')
    for k, v in STATS.items():
        print(f'{k:<45} {v:5d} tris')


main()
