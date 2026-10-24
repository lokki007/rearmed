"""Round 4 enemy archetypes (en-types): the SHIELD bearer's riot shield, in the low-poly id 1999
prop style of make_props.py: a slightly curved steel frame (gunmetal, scuffed, hazard chevrons on
the lower edge, a viewport slit), a rubber grip on the back, and a separate energy face.

Two models, the shield's front = +x, centred on its own origin (cgame puts it in front of the
bot's torso, or swung down to its side):
  models/rearmed/types/shield.md3        frame + backing plate + grip (baked atlas, lightingDiffuse)
  models/rearmed/types/shield_face.md3   the energy face, one curved sheet just in front of the
                                         frame (shader models/rearmed/types/shield_face in
                                         scripts/rearmed_types.shader: additive hex lattice, tinted
                                         by the entity colour; tools/make_types_gfx.py)

Run: blender --background --factory-startup --python tools/blender/make_shield.py
(also: bash tools/blender/build_all.sh shield)
"""
import math
import os
import pathlib
import sys

import bmesh
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blib  # noqa: E402
import make_props as mp  # noqa: E402
from blib import col, fbm3, lerp, smoothstep  # noqa: E402

SHADER = pathlib.Path(blib.ASSETS) / 'scripts' / 'rearmed_types_models.shader'
HW, HH = 13.0, 22.0          # half width (y), half height (z)
BOW = 2.2                    # how far the edges curve back (x) at y = +-HW
FRAME, PLATE, GRIP = 0, 1, 2
MATS = ('sh_frame', 'sh_plate', 'sh_grip')
COLS = 6                     # curve segments across


def bow(y):
    return -BOW * (y / HW) ** 2


def build_frame():
    blib.clear_objects()
    bm = bmesh.new()
    solids = []

    def add(lo, hi, mat, solid=True):
        mp._box(bm, lo, hi, mat)
        if solid:
            solids.append((lo, hi))
    # the backing plate: curved slab of COLS boxes
    for i in range(COLS):
        y0 = -HW + 2 * HW * i / COLS
        y1 = -HW + 2 * HW * ( i + 1 ) / COLS
        x = bow(0.5 * (y0 + y1))
        add((x - 1.6, y0, -HH + 1.5), (x - 0.4, y1, HH - 1.5), PLATE)
    # the frame: rails along the top / bottom (per segment, following the curve) and both sides
    for i in range(COLS):
        y0 = -HW + 2 * HW * i / COLS
        y1 = -HW + 2 * HW * (i + 1) / COLS
        x = bow(0.5 * (y0 + y1))
        add((x - 1.8, y0, HH - 2.0), (x + 0.9, y1, HH), FRAME)
        add((x - 1.8, y0, -HH), (x + 0.9, y1, -HH + 2.6), FRAME)
        add((x - 1.8, y0, -1.0), (x + 0.5, y1, 1.0), FRAME)            # the middle brace
    for sy in (-1, 1):
        x = bow(sy * HW)
        lo = (x - 1.8, min(sy * (HW - 1.6), sy * HW), -HH)
        hi = (x + 0.9, max(sy * (HW - 1.6), sy * HW), HH)
        add(lo, hi, FRAME)
    # the viewport slit: two short bars above the brace near the top
    add((-0.6, -6.0, HH - 7.0), (1.2, 6.0, HH - 6.0), FRAME)
    add((-0.6, -6.0, HH - 4.2), (1.2, 6.0, HH - 3.2), FRAME)
    # the grip on the back
    add((-4.6, -1.2, -5.0), (-3.4, 1.2, 5.0), GRIP)
    add((-3.6, -1.0, 4.0), (-1.6, 1.0, 5.0), GRIP)
    add((-3.6, -1.0, -5.0), (-1.6, 1.0, -4.0), GRIP)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    bm.normal_update()
    mp.crate_uv(bm, solids)
    blib.auto_sharp(bm, 30)
    return blib.obj_from_bm('shield', bm, MATS)


def build_face():
    """one curved sheet just in front of the plate, UV 0..1 over it"""
    blib.clear_objects()
    bm = bmesh.new()
    uvl = blib.uv_layer(bm)
    rows = 2
    grid = []
    for j in range(rows + 1):
        z = -HH + 2.8 + (2 * HH - 5.4) * j / rows
        line = []
        for i in range(COLS + 1):
            y = -HW + 1.8 + (2 * HW - 3.6) * i / COLS
            line.append(bm.verts.new((bow(y) + 0.3, y, z)))
        grid.append(line)
    for j in range(rows):
        for i in range(COLS):
            # winding so the normal points +x (out of the face)
            f = bm.faces.new([grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j + 1][i]])
            for loop in f.loops:
                v = loop.vert.co
                loop[uvl].uv = ((v.y + HW) / (2 * HW), (v.z + HH) / (2 * HH))
    bm.normal_update()
    return blib.obj_from_bm('shield_face', bm, ('sh_face',))


def paint(m):
    pos, idm, nrm = m['pos'], m['id'], m['nrm']
    x, y, z = pos[..., 0], pos[..., 1], pos[..., 2]
    frame, plate, grip = idm == FRAME, idm == PLATE, idm == GRIP
    # gunmetal frame: blue-grey steel, scuffs and chips, hazard chevrons along the bottom rail
    steel = mp.mottle(pos, col(0.16, 0.18, 0.21), col(0.36, 0.39, 0.44), 601, 0.5)
    steel = steel * (1 - 0.35 * mp.pits(pos, 602))[..., None]
    scuff = smoothstep(0.62, 0.8, fbm3(pos * 0.9, 3, 603))
    steel = lerp(steel, col(0.55, 0.57, 0.6), scuff * 0.6)
    chevron = (np.mod((y + z * 0.9) * 0.35, 1.0) < 0.5) & (z < -HH + 2.7) & (x > -1.0)
    steel = np.where(chevron[..., None], lerp(steel, col(0.85, 0.62, 0.08), 0.85), steel)
    out = steel
    # backing plate: dark polymer, faint cell grid (the energy lattice sits in front of it)
    plt = mp.mottle(pos, col(0.04, 0.05, 0.06), col(0.10, 0.12, 0.14), 604, 0.4)
    cells = smoothstep(0.08, 0.0, np.minimum(np.abs(np.mod(y, 4.0) - 2.0), np.abs(np.mod(z, 4.0) - 2.0)))
    plt = lerp(plt, col(0.12, 0.30, 0.38), cells * 0.5)
    out = np.where(plate[..., None], plt, out)
    # grip: worn black rubber
    rub = mp.mottle(pos, col(0.03, 0.03, 0.03), col(0.09, 0.08, 0.08), 605, 0.6)
    out = np.where(grip[..., None], rub, out)
    out = out * (0.84 + 0.32 * mp.grit(pos, 606))[..., None]
    return mp.q3_light(out, m, 0.7, 0.22, 0.5)


def write_shader():
    txt = '// REARMED round 4 shield bearer (generated by tools/blender/make_shield.py)\n'
    txt += """models/rearmed/types/shield
{
	cull none
	{
		map models/rearmed/types/shield.tga
		rgbGen lightingDiffuse
	}
}
"""
    SHADER.parent.mkdir(parents=True, exist_ok=True)
    SHADER.write_bytes(txt.replace('\n', '\r\n').encode('ascii'))
    print('wrote', SHADER)


def main():
    blib.setup_scene()
    ob = build_frame()
    m = blib.bake_maps(ob, mp.BAKE, ao_dist=4.0, bevel_r=0.3)
    mp.save('models/rearmed/types/shield.tga', paint(m))
    print('shield tris', mp.finish(ob, 'models/rearmed/types/shield.md3', 'models/rearmed/types/shield'))
    ob = build_face()
    print('shield face tris', mp.finish(ob, 'models/rearmed/types/shield_face.md3', 'models/rearmed/types/shield_face'))
    write_shader()


main()
