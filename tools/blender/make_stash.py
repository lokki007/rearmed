"""DESCENT round 3 SECRETS: the old reliquary strongbox (ds-secrets), in the id 1999 prop style of
make_props.py (ds-art): low poly, a 256 px painterly atlas baked at 2x with AO and edge light,
dark oak, black forged iron straps and corner caps, a tarnished brass lock plate with a scratched
skull, and a dark interior with a few coins glinting (seen once the lid opens).

Two models, front = +x, sitting on z = 0:
  models/rearmed/secrets/stash.md3       the box (body, plinth, straps, lock plate)
  models/rearmed/secrets/stash_lid.md3   the lid, its origin on the hinge (back top edge,
                                         x = -12, z = 15 on the box), extending along +x
cgame/cg_ds_secrets.c puts the lid at the hinge and pitches it open (CSEC_HINGE_X / _Z).

Run: blender --background --factory-startup --python tools/blender/make_stash.py
(also: bash tools/blender/build_all.sh stash)
"""
import os
import pathlib
import random
import sys

import bmesh
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blib  # noqa: E402
import make_props as mp  # noqa: E402
from blib import col, fbm3, lerp, smoothstep, vnoise3  # noqa: E402

SHADER = pathlib.Path(blib.ASSETS) / 'scripts' / 'rearmed_secrets.shader'
HX, HY, HZ = 12.0, 8.5, 15.0          # body half length / half depth / height (top of the walls)
WOOD, IRON, BRASS, INNER = 0, 1, 2, 3
MATS = ('s_wood', 's_iron', 's_brass', 's_inner')


def build_body():
    blib.clear_objects()
    bm = bmesh.new()
    rnd = random.Random(31)
    solids = []

    def add(lo, hi, mat, solid=True):
        mp._box(bm, lo, hi, mat)
        if solid:
            solids.append((lo, hi))
    add((-HX - 1.0, -HY - 1.0, 0.0), (HX + 1.0, HY + 1.0, 2.2), IRON)              # plinth
    add((-HX, -HY, 2.2), (HX, HY, HZ - 0.6), WOOD)                                # the oak walls
    add((-HX + 1.2, -HY + 1.2, HZ - 0.6), (HX - 1.2, HY - 1.2, HZ - 0.2), INNER)    # the dark inside (top face)
    add((-HX, -HY, HZ - 0.6), (HX, -HY + 1.2, HZ), WOOD)                          # the rim around it
    add((-HX, HY - 1.2, HZ - 0.6), (HX, HY, HZ), WOOD)
    add((-HX, -HY + 1.2, HZ - 0.6), (-HX + 1.2, HY - 1.2, HZ), WOOD)
    add((HX - 1.2, -HY + 1.2, HZ - 0.6), (HX, HY - 1.2, HZ), WOOD)
    for x in (-7.0, 7.0):                                                          # two iron straps around
        w = 1.7 + rnd.uniform(-0.15, 0.15)
        add((x - w / 2, -HY - 0.45, 2.0), (x + w / 2, HY + 0.45, HZ + 0.05), IRON)
    for sx in (-1, 1):                                                             # forged corner caps
        for sy in (-1, 1):
            lo = (min(sx * (HX - 2.6), sx * (HX + 0.5)), min(sy * (HY - 2.6), sy * (HY + 0.5)), 2.0)
            hi = (max(sx * (HX - 2.6), sx * (HX + 0.5)), max(sy * (HY - 2.6), sy * (HY + 0.5)), 6.0)
            add(lo, hi, IRON)
            lo = (lo[0], lo[1], HZ - 4.0)
            hi = (hi[0], hi[1], HZ + 0.1)
            add(lo, hi, IRON)
    add((HX, -3.2, 5.4), (HX + 0.7, 3.2, 13.2), BRASS)                              # the lock plate (front)
    add((HX + 0.7, -0.6, 8.6), (HX + 1.3, 0.6, 10.6), IRON)                         # the keyhole boss
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    bm.normal_update()
    mp.crate_uv(bm, solids)
    blib.auto_sharp(bm, 30)
    return blib.obj_from_bm('stash', bm, MATS)


def build_lid():
    blib.clear_objects()
    bm = bmesh.new()
    solids = []
    L = 2 * HX + 0.6

    def add(lo, hi, mat, solid=True):
        mp._box(bm, lo, hi, mat)
        if solid:
            solids.append((lo, hi))
    # local frame: hinge at the origin, the lid runs along +x, its underside at z = 0
    add((-0.3, -HY - 0.6, 0.0), (L, HY + 0.6, 3.2), WOOD)                         # the slab
    add((1.5, -HY + 1.5, 3.2), (L - 1.8, HY - 1.5, 5.0), WOOD)                    # the raised panel
    add((1.2, -HY + 1.0, -0.6), (L - 1.5, HY - 1.0, 0.0), INNER)                  # the underside lining
    for x in (HX - 7.0, HX + 7.0):                                                # straps over the top
        add((x - 0.9, -HY - 1.0, -0.2), (x + 0.9, HY + 1.0, 5.4), IRON)
    add((L - 0.2, -1.4, -3.4), (L + 0.6, 1.4, 2.6), IRON)                          # the hasp down the front
    add((-0.9, -HY - 0.2, -0.4), (0.9, -HY + 2.6, 1.6), IRON)                      # hinge knuckles
    add((-0.9, HY - 2.6, -0.4), (0.9, HY + 0.2, 1.6), IRON)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    bm.normal_update()
    mp.crate_uv(bm, solids)
    blib.auto_sharp(bm, 30)
    return blib.obj_from_bm('stash_lid', bm, MATS)


def paint(m, lid):
    pos, idm, nrm = m['pos'], m['id'], m['nrm']
    x, y, z = pos[..., 0], pos[..., 1], pos[..., 2]
    wood, iron, brass, inner = idm == WOOD, idm == IRON, idm == BRASS, idm == INNER
    ax = np.argmax(np.abs(nrm), -1)
    # dark old oak: planks along x, heavy grain, near-black in the seams
    along = np.where(ax == 0, y, x)
    across = np.where(ax == 2, y, z)
    pw = 3.4
    k = np.floor((across + 20) / pw)
    fr = np.mod(across + 20, pw)
    gap = smoothstep(0.35, 0.0, np.minimum(fr, pw - fr))
    tone = 0.75 + 0.4 * vnoise3(np.stack([k * 1.7, k * 0.3, ax * 2.0], -1), 501)
    grain = fbm3(np.stack([along * 0.2, across * 2.6 + k * 4.0, ax * 3.0], -1), 4, 502)
    oak = lerp(col(0.10, 0.065, 0.04), col(0.30, 0.20, 0.12), np.clip(0.5 + (grain - 0.5) * 2.4, 0, 1)) * tone[..., None]
    oak = oak * (1 - 0.75 * gap)[..., None]
    oak = lerp(oak, col(0.05, 0.035, 0.025), smoothstep(0.55, 0.78, fbm3(pos * 0.25, 3, 503)) * 0.5)   # grime
    out = oak
    # black forged iron, hammered, rust bleeding at the edges, rivets
    ironc = mp.mottle(pos, col(0.07, 0.065, 0.06), col(0.20, 0.17, 0.14), 504, 0.45)
    ironc, _ = mp.rusty(ironc, pos, 505, 0.85, 0.52, 0.68)
    ironc = ironc * (1 - 0.3 * mp.pits(pos, 506))[..., None]
    rv = np.zeros_like(x)
    for rx in (-7.0, 7.0, HX - 7.0, HX + 7.0):
        for rz in (4.0, 8.5, 13.0):
            rv = np.maximum(rv, smoothstep(0.55, 0.25, np.hypot(x - rx, z - rz)))
    ironc = lerp(ironc, col(0.30, 0.27, 0.22), rv * 0.8)
    out = np.where(iron[..., None], ironc, out)
    # tarnished brass plate with a scratched skull
    br = mp.mottle(pos, col(0.36, 0.25, 0.08), col(0.62, 0.46, 0.17), 507, 0.6)
    br = lerp(br, col(0.16, 0.20, 0.10), smoothstep(0.5, 0.75, fbm3(pos * 0.8, 3, 508)) * 0.6)    # verdigris
    py, pz = y, z - 9.4
    skull = mp.disc(py, pz, 0.0, 0.6, 2.0, 509, 0.25)
    eyes = mp.disc(py, pz, -0.8, 0.8, 0.55, 510, 0.15) + mp.disc(py, pz, 0.8, 0.8, 0.55, 511, 0.15)
    jaw = mp.strokes(py, pz, [(-1.0, -1.6, 1.0, -1.6)], 0.25, 512)
    cut = np.clip(skull * 0.55 - eyes * 0.6, 0, 1) + jaw * 0.5
    br = lerp(br, col(0.10, 0.06, 0.02), np.clip(cut, 0, 1) * 0.8)
    out = np.where(brass[..., None], br, out)
    # the inside: near black velvet with a few coins catching the light
    rng = np.random.default_rng(513 if lid else 514)
    innerc = mp.mottle(pos, col(0.03, 0.02, 0.025), col(0.09, 0.05, 0.06), 515, 0.5)
    if not lid:
        coins = np.zeros_like(x)
        for _ in range(9):
            cx, cy = rng.uniform(-HX + 2.5, HX - 2.5), rng.uniform(-HY + 2.5, HY - 2.5)
            coins = np.maximum(coins, smoothstep(1.3, 0.9, np.hypot(x - cx, y - cy)))
        innerc = lerp(innerc, col(0.85, 0.62, 0.20), coins * 0.9)
    out = np.where(inner[..., None], innerc, out)
    out = out * (0.82 + 0.36 * mp.grit(pos, 516))[..., None]
    out = mp.q3_light(out, m, 0.7, 0.22, 0.5)
    return out


def write_shader():
    txt = '// REARMED DESCENT secret stash (generated by tools/blender/make_stash.py)\n'
    for name in ('stash', 'stash_lid'):
        txt += f"""models/rearmed/secrets/{name}
{{
	{{
		map models/rearmed/secrets/{name}.tga
		rgbGen lightingDiffuse
	}}
}}
"""
    SHADER.parent.mkdir(parents=True, exist_ok=True)
    SHADER.write_bytes(txt.replace('\n', '\r\n').encode('ascii'))
    print('wrote', SHADER)


def main():
    blib.setup_scene()
    ob = build_body()
    m = blib.bake_maps(ob, mp.BAKE, ao_dist=5.0, bevel_r=0.35)
    mp.save('models/rearmed/secrets/stash.tga', paint(m, False))
    print('stash tris', mp.finish(ob, 'models/rearmed/secrets/stash.md3', 'models/rearmed/secrets/stash'))
    ob = build_lid()
    m = blib.bake_maps(ob, mp.BAKE, ao_dist=5.0, bevel_r=0.35)
    mp.save('models/rearmed/secrets/stash_lid.tga', paint(m, True))
    print('lid tris', mp.finish(ob, 'models/rearmed/secrets/stash_lid.md3', 'models/rearmed/secrets/stash_lid'))
    write_shader()


main()
