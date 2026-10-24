"""REARMED round 4 (en-feel) severed limb + helmet gibs: procedural geometry, baked + painted -> MD3/TGA.

  blender --background --factory-startup --python tools/blender/make_hitfeel.py
writes assets/models/rearmed/gibs/arm.md3/.tga, leg.md3/.tga, helmet.md3/.tga
(shaders: assets/scripts/rearmed_hitfeel.shader from tools/make_hitfeel_assets.py).
Generic enough for any Q3 body: a dark combat-suit sleeve / trouser with armour plates, a torn
bloody end with a bone stub. Units are Q3 units (a Q3 arm is ~22 long).
"""
import math
import os
import random
import sys

import bmesh
import numpy as np
from mathutils import Vector, noise

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blib  # noqa: E402
from blib import col, fbm3, lerp, smoothstep  # noqa: E402

SUIT, SKIN, MEAT, BONE, PLATE = 0, 1, 2, 3, 4
MATS = ('h_suit', 'h_skin', 'h_meat', 'h_bone', 'h_plate')


def tube(bm, path, radius, segs, mat=0, cap0=True, cap1=True, cap_mat=None, jag=0.0, rnd=None, mats=None):
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
            if jag and rnd and i == 0:
                q += tng * rnd.uniform(-jag, jag)
            ring.append(bm.verts.new(q))
        rings.append(ring)
    for i in range(n - 1):
        for j in range(segs):
            j2 = (j + 1) % segs
            f = bm.faces.new((rings[i][j], rings[i][j2], rings[i + 1][j2], rings[i + 1][j]))
            f.material_index = mats[i] if mats else mat
    for ring, flip, on in ((rings[0], True, cap0), (rings[-1], False, cap1)):
        if not on:
            continue
        f = bm.faces.new(list(reversed(ring)) if flip else ring)
        f.material_index = mat if cap_mat is None else cap_mat
    return rings


def lumpy(bm, center, radii, subdiv=2, seed=0, rough=0.2, freq=0.4, mat=0):
    nv, nf = len(bm.verts), len(bm.faces)
    bmesh.ops.create_icosphere(bm, subdivisions=subdiv, radius=1.0)
    bm.verts.ensure_lookup_table()
    bm.faces.ensure_lookup_table()
    off = Vector((seed * 13.1, seed * 7.7, seed * 3.3))
    for v in bm.verts[nv:]:
        d = v.co.normalized()
        p = Vector((d.x * radii[0], d.y * radii[1], d.z * radii[2]))
        k = 1 + rough * noise.noise(p * freq + off)
        v.co = Vector(center) + p * k
    for f in bm.faces[nf:]:
        f.material_index = mat


def torn_end(bm, ring, center, axis, rnd, depth=1.6):
    """Poke the open end of a limb into a ragged meat cap with a bone stub in the middle."""
    f = bm.faces.new(list(reversed(ring)))
    f.material_index = MEAT
    res = bmesh.ops.poke(bm, faces=[f])
    for v in res['verts']:
        v.co = Vector(center) - Vector(axis) * depth * 0.4
    for ff in res['faces']:
        ff.material_index = MEAT
    for v in ring:   # ragged flesh rim
        v.co += Vector(axis) * rnd.uniform(-depth, depth * 0.3)


def bone_stub(bm, base, axis, length, r, rnd):
    a = Vector(axis)
    p0 = Vector(base) + a * 0.5
    path = [p0 - a * (length * t) for t in (0.0, 0.5, 1.0)]
    tube(bm, path, r, 7, BONE, cap0=True, cap1=True, cap_mat=BONE, jag=r * 0.9, rnd=rnd)


def finish(ob, rel, shader):
    blib.triangulate(ob)
    return blib.export_md3(rel, [('body', ob, shader)])


def center_origin(ob):
    me = ob.data
    xs = [v.co for v in me.vertices]
    mn = Vector((min(c.x for c in xs), min(c.y for c in xs), min(c.z for c in xs)))
    mx = Vector((max(c.x for c in xs), max(c.y for c in xs), max(c.z for c in xs)))
    c = (mn + mx) / 2
    for v in me.vertices:
        v.co -= c


def uv_and_obj(name, bm):
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    ob = blib.obj_from_bm(name, bm, MATS)
    center_origin(ob)
    blib.smart_uv(ob, 60, 0.02)
    blib.uv_fit(ob, (0, 0, 1, 1), 0.01)
    return ob


# ------------------------------------------------------------------ arm
def build_arm():
    blib.clear_objects()
    rnd = random.Random(501)
    bm = bmesh.new()
    # shoulder (torn, at x=0) -> elbow (x=11) -> wrist (x=21, bent down a little)
    sh = [(0, 0, 0), (3, 0, 0.2), (7, 0, 0.1), (11, 0, -0.4)]
    fa = [(11, 0, -0.4), (14.5, 0.4, -1.4), (18, 0.7, -2.5), (20.5, 0.8, -3.2)]
    upper = tube(bm, sh, lambda t: 2.6 - 0.5 * t, 10, SUIT, cap0=False, cap1=False)
    torn_end(bm, upper[0], (0, 0, 0), (1, 0, 0), rnd, 1.4)
    lower = tube(bm, fa, lambda t: 2.15 - 0.5 * t, 10, SUIT, cap0=False, cap1=True, mats=[SUIT, SUIT, SKIN])
    # elbow knot hides the seam
    lumpy(bm, (11, 0, -0.4), (2.5, 2.4, 2.4), 2, 3, 0.12, 0.6, SUIT)
    # shoulder pad plate
    lumpy(bm, (2.2, 0, 1.6), (3.4, 2.9, 1.3), 2, 4, 0.08, 0.5, PLATE)
    # hand: a clenched lump of skin
    lumpy(bm, (22.3, 0.8, -3.6), (2.0, 1.6, 1.7), 2, 5, 0.18, 0.7, SKIN)
    bone_stub(bm, (0, 0, 0), (1, 0, 0), 2.6, 0.8, rnd)
    return uv_and_obj('arm', bm)


# ------------------------------------------------------------------ leg
def build_leg():
    blib.clear_objects()
    rnd = random.Random(502)
    bm = bmesh.new()
    th = [(0, 0, 0), (0.2, 0, -5), (0.5, 0, -10), (0.6, 0, -13)]
    sn = [(0.6, 0, -13), (0.2, 0, -18), (-0.2, 0, -23), (-0.3, 0, -25.5)]
    thigh = tube(bm, th, lambda t: 3.6 - 0.9 * t, 10, SUIT, cap0=False, cap1=False)
    torn_end(bm, thigh[0], (0, 0, 0), (0, 0, -1), rnd, 1.8)
    tube(bm, sn, lambda t: 2.6 - 0.5 * t, 10, SUIT, cap0=False, cap1=True)
    lumpy(bm, (0.9, 0, -13), (2.9, 2.8, 2.6), 2, 6, 0.1, 0.6, PLATE)        # knee pad
    # boot
    lumpy(bm, (1.6, 0, -27), (4.2, 2.3, 1.9), 2, 7, 0.08, 0.5, PLATE)
    bone_stub(bm, (0, 0, 0), (0, 0, -1), 3.2, 1.0, rnd)
    # lay it along +X like the arm: settled gibs lie flat along their X axis (cg_gore.c)
    for v in bm.verts:
        x, y, z = v.co
        v.co = Vector((-z, y, x))
    return uv_and_obj('leg', bm)


# ------------------------------------------------------------------ helmet
def build_helmet():
    blib.clear_objects()
    bm = bmesh.new()
    rx, ry, rz = 5.4, 4.8, 5.0
    segs, rings = 20, 9
    outer, inner = [], []
    for k in range(rings + 1):
        lat = (k / rings) * (math.pi / 2 + 0.25)          # top -> a little past the equator
        ro, ri = [], []
        for j in range(segs):
            a = 2 * math.pi * j / segs
            x, y, z = math.sin(lat) * math.cos(a), math.sin(lat) * math.sin(a), math.cos(lat)
            front = max(0.0, math.cos(a))                     # cut the face opening lower in front
            zz = z if not (k > rings * 0.55 and front > 0.5) else z * (1 - 0.6 * front) + 0.25 * front
            ro.append(bm.verts.new((x * rx, y * ry, zz * rz)))
            ri.append(bm.verts.new((x * (rx - 0.6), y * (ry - 0.6), zz * (rz - 0.6))))
        outer.append(ro)
        inner.append(ri)
    for k in range(rings):
        for j in range(segs):
            j2 = (j + 1) % segs
            f = bm.faces.new((outer[k][j], outer[k][j2], outer[k + 1][j2], outer[k + 1][j]))
            f.material_index = PLATE
            f = bm.faces.new((inner[k][j], inner[k + 1][j], inner[k + 1][j2], inner[k][j2]))
            f.material_index = SUIT
    for j in range(segs):   # rim
        j2 = (j + 1) % segs
        f = bm.faces.new((outer[rings][j], outer[rings][j2], inner[rings][j2], inner[rings][j]))
        f.material_index = BONE
    # visor band
    lumpy(bm, (4.6, 0, -0.6), (1.2, 3.8, 1.3), 2, 9, 0.03, 0.5, SKIN)
    bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=0.01)
    return uv_and_obj('helmet', bm)


# ------------------------------------------------------------------ paint
def shade(rgb, m, ao_k=0.7, top=0.12):
    rgb = rgb * (1 - ao_k + ao_k * m['ao'])[..., None]
    rgb = rgb * (1 - top * 0.5 + top * m['nrm'][..., 2])[..., None]
    return np.clip(rgb, 0, 1)


def paint_limb(m, seed):
    pos, idm = m['pos'], m['id']
    n1 = fbm3(pos * 0.6 + seed, 4, 61)
    suit = lerp(col(0.13, 0.13, 0.12), col(0.22, 0.21, 0.18), n1)
    weave = 0.9 + 0.1 * np.sin(pos[..., 0] * 6) * np.sin(pos[..., 1] * 6 + pos[..., 2] * 6)
    suit = suit * weave[..., None]
    plate = lerp(col(0.28, 0.3, 0.31), col(0.45, 0.46, 0.44), fbm3(pos * 0.9 + seed, 3, 62))
    scratch = smoothstep(0.02, 0.0, np.abs(fbm3(pos * np.array([3.0, 0.4, 0.4]), 2, 63) - 0.5))
    plate = lerp(plate, col(0.7, 0.7, 0.66), scratch * 0.3)
    skin = lerp(col(0.62, 0.48, 0.4), col(0.5, 0.36, 0.3), fbm3(pos * 1.2, 3, 64))
    meat = lerp(col(0.45, 0.03, 0.04), col(0.75, 0.13, 0.11), smoothstep(0.35, 0.7, fbm3(pos * 2.2, 3, 65)))
    meat = lerp(meat, col(0.15, 0.0, 0.01), smoothstep(0.55, 0.75, n1) * 0.7)
    bone = lerp(col(0.9, 0.85, 0.72), col(0.68, 0.6, 0.46), fbm3(pos * 2, 3, 66))
    out = suit
    out = np.where((idm == PLATE)[..., None], plate, out)
    out = np.where((idm == SKIN)[..., None], skin, out)
    out = np.where((idm == MEAT)[..., None], meat, out)
    out = np.where((idm == BONE)[..., None], bone, out)
    # blood soaked toward the torn end (origin side of the limb), runs + spatter elsewhere
    dist = np.linalg.norm(pos, axis=-1)
    soak = smoothstep(9.0, 2.0, dist + 3 * fbm3(pos * 0.5, 3, 67))
    runs = smoothstep(0.55, 0.7, fbm3(pos * np.array([0.15, 1.2, 1.2]) + seed, 3, 68)) * smoothstep(16, 4, dist)
    spots = smoothstep(0.7, 0.76, fbm3(pos * 1.1 + 9 + seed, 3, 69))
    blood = np.clip(soak + runs * 0.8 + spots * 0.7, 0, 1) * (idm != MEAT)
    out = lerp(out, lerp(col(0.5, 0.02, 0.03), col(0.22, 0.0, 0.01), n1), blood * 0.9)
    return shade(out, m)


def paint_helmet(m):
    pos, idm = m['pos'], m['id']
    n1 = fbm3(pos * 0.7, 4, 71)
    shell = lerp(col(0.3, 0.33, 0.3), col(0.42, 0.45, 0.4), n1)
    dents = smoothstep(0.6, 0.75, fbm3(pos * 1.6, 3, 72))
    shell = lerp(shell, col(0.2, 0.21, 0.2), dents * 0.5)
    scratch = smoothstep(0.025, 0.0, np.abs(fbm3(pos * np.array([0.5, 2.5, 0.5]), 2, 73) - 0.5))
    shell = lerp(shell, col(0.6, 0.6, 0.58), scratch * 0.25)
    # the hole the shot punched + scorch
    hole = smoothstep(2.4, 0.8, np.linalg.norm(pos - np.array([2.5, 2.0, 4.0]), axis=-1))
    shell = lerp(shell, col(0.06, 0.05, 0.05), hole)
    visor = lerp(col(0.05, 0.08, 0.1), col(0.15, 0.3, 0.35), smoothstep(0.3, 0.8, fbm3(pos * 2, 2, 74)))
    lining = col(0.12, 0.1, 0.09)
    rim = col(0.18, 0.18, 0.17)
    out = shell
    out = np.where((idm == SUIT)[..., None], lining, out)
    out = np.where((idm == SKIN)[..., None], visor, out)
    out = np.where((idm == BONE)[..., None], rim, out)
    blood = smoothstep(0.66, 0.74, fbm3(pos * 0.9 + 5, 3, 75)) * 0.8 + (idm == SUIT) * 0.6
    out = lerp(out, col(0.4, 0.02, 0.03), np.clip(blood, 0, 1) * 0.8)
    return shade(out, m, 0.6, 0.15)


def bake(ob, size, painter, rel):
    maps = blib.bake_maps(ob, size, ao_dist=3.0, bevel_r=0.25)
    blib.save_tga(rel, painter(maps))


def main():
    blib.setup_scene()
    stats = {}
    ob = build_arm()
    bake(ob, 256, lambda m: paint_limb(m, 1.0), 'models/rearmed/gibs/arm.tga')
    stats['arm'] = finish(ob, 'models/rearmed/gibs/arm.md3', 'models/rearmed/gibs/arm')
    ob = build_leg()
    bake(ob, 256, lambda m: paint_limb(m, 2.0), 'models/rearmed/gibs/leg.tga')
    stats['leg'] = finish(ob, 'models/rearmed/gibs/leg.md3', 'models/rearmed/gibs/leg')
    ob = build_helmet()
    bake(ob, 256, paint_helmet, 'models/rearmed/gibs/helmet.tga')
    stats['helmet'] = finish(ob, 'models/rearmed/gibs/helmet.md3', 'models/rearmed/gibs/helmet')
    print('==== triangle counts', stats)


main()
