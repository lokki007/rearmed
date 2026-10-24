"""DESCENT v2 LOOT models: xp gems (3 tiers), the vacuum magnet, treasure chests (3 qualities),
their textures (numpy) and assets/scripts/rearmed_loot.shader. Cheap on purpose: flat-shaded
crystals of 8-60 triangles with a fullbright gradient + an additive environment sparkle.

Run (from anywhere):
  blender --background --factory-startup --python tools/blender/make_loot.py
(also run by tools/blender/build_all.sh loot)
"""
import math
import os
import pathlib
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blib  # noqa: E402

SHADER = pathlib.Path(blib.ASSETS) / 'scripts' / 'rearmed_loot.shader'
GEM_RGB = [(0.25, 0.55, 1.0), (0.25, 1.0, 0.4), (1.0, 0.18, 0.15)]
TRIM_RGB = [(0.78, 0.52, 0.26), (0.95, 0.55, 0.16), (0.72, 0.42, 1.0)]


def new_obj(name, bm, flat=True):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    for p in me.polygons:
        p.use_smooth = not flat
    return ob


# ---------------------------------------------------------------- crystals
def crystal(bm, sides, r, mid, top, bot, xform=Matrix(), twist=0.0):
    """A bipyramid with an optional prism middle; UV v runs bottom to top, u across each facet."""
    uvl = blib.uv_layer(bm)
    lo = [bm.verts.new((r * math.cos(2 * math.pi * j / sides + twist), r * math.sin(2 * math.pi * j / sides + twist), -mid / 2))
          for j in range(sides)]
    hi = lo if mid <= 0 else [bm.verts.new((v.co.x, v.co.y, mid / 2)) for v in lo]
    apex = bm.verts.new((0, 0, mid / 2 + top))
    foot = bm.verts.new((0, 0, -mid / 2 - bot))
    zmin, zmax = -mid / 2 - bot, mid / 2 + top
    faces = []
    for j in range(sides):
        k = (j + 1) % sides
        if mid > 0:
            faces.append(bm.faces.new((lo[j], lo[k], hi[k], hi[j])))
        faces.append(bm.faces.new((hi[j], hi[k], apex)))
        faces.append(bm.faces.new((lo[k], lo[j], foot)))
    for f in faces:
        n = len(f.loops)
        for i, loop in enumerate(f.loops):
            loop[uvl].uv = (0.1 + 0.8 * i / max(1, n - 1), (loop.vert.co.z - zmin) / (zmax - zmin))
    for v in set(lo + hi + [apex, foot]):
        v.co = xform @ v.co
    return faces


def build_gem(tier):
    blib.clear_objects()
    bm = bmesh.new()
    if tier == 0:
        crystal(bm, 4, 2.6, 0.0, 4.2, 4.2)
    elif tier == 1:
        crystal(bm, 6, 2.9, 2.8, 3.4, 2.8, twist=0.3)
    else:
        crystal(bm, 6, 3.4, 5.0, 4.4, 2.8)
        crystal(bm, 5, 1.7, 2.2, 2.4, 1.2, Matrix.Translation((2.6, 0.6, -1.0)) @ Matrix.Rotation(0.65, 4, 'Y'))
        crystal(bm, 5, 1.5, 1.8, 2.2, 1.0, Matrix.Translation((-2.2, -1.4, -1.4)) @ Matrix.Rotation(-0.7, 4, 'X'))
    return new_obj('gem%d' % tier, bm)


def paint_gem(tier):
    h, w = 64, 64
    v = np.linspace(0, 1, h)[:, None] * np.ones((1, w))
    u = np.ones((h, 1)) * np.linspace(0, 1, w)[None, :]
    c = np.array(GEM_RGB[tier], np.float32)
    core = 0.35 + 0.65 * v ** 1.4
    edge = np.clip(1.0 - np.minimum(u, 1 - u) * 9, 0, 1) ** 2
    rgb = c[None, None, :] * core[..., None]
    rgb = rgb + (1 - rgb) * (0.55 * edge * v)[..., None] + (1 - rgb) * (0.35 * np.clip(v - 0.75, 0, 1) * 4)[..., None]
    blib.save_tga('models/rearmed/loot/gem%d.tga' % tier, np.clip(rgb, 0, 1))


def paint_env():
    h = w = 64
    y, x = np.mgrid[0:h, 0:w] / (h - 1.0)
    spots = np.zeros((h, w), np.float32)
    for (cx, cy, r) in ((0.3, 0.7, 0.12), (0.72, 0.35, 0.08), (0.55, 0.82, 0.05), (0.2, 0.25, 0.06)):
        spots += np.exp(-((x - cx) ** 2 + (y - cy) ** 2) / (r * r))
    rgb = np.clip(spots, 0, 1)[..., None] * np.ones(3)[None, None, :]
    blib.save_tga('models/rearmed/loot/gemenv.tga', rgb)


# ---------------------------------------------------------------- the vacuum magnet
def sweep(bm, path, radius, segs, uv_v):
    """Tube along a polyline (x/z plane); returns nothing, UV u around, v along."""
    uvl = blib.uv_layer(bm)
    rings = []
    for i, p in enumerate(path):
        a = Vector(path[max(0, i - 1)])
        b = Vector(path[min(len(path) - 1, i + 1)])
        t = (b - a).normalized()
        side = Vector((0, 1, 0))
        nrm = t.cross(side).normalized()
        ring = []
        for j in range(segs):
            ang = 2 * math.pi * j / segs
            off = nrm * math.cos(ang) * radius + side * math.sin(ang) * radius
            ring.append(bm.verts.new(Vector(p) + off))
        rings.append(ring)
    for i in range(len(rings) - 1):
        for j in range(segs):
            k = (j + 1) % segs
            f = bm.faces.new((rings[i][j], rings[i][k], rings[i + 1][k], rings[i + 1][j]))
            for loop, uv in zip(f.loops, ((j / segs, uv_v * i), ((j + 1) / segs, uv_v * i),
                                          ((j + 1) / segs, uv_v * (i + 1)), (j / segs, uv_v * (i + 1)))):
                loop[uvl].uv = uv
    return rings


def build_magnet():
    blib.clear_objects()
    R, leg = 4.2, 3.5
    path = [(-R, 0, -leg), (-R, 0, -leg / 2)]
    for i in range(11):
        a = math.pi - math.pi * i / 10
        path.append((R * math.cos(a), 0, R * math.sin(a)))
    path += [(R, 0, -leg / 2), (R, 0, -leg)]
    bm = bmesh.new()
    sweep(bm, path, 1.5, 8, 0.12)
    body = new_obj('magnet_body', bm, flat=False)
    bm = bmesh.new()
    for x in (-R, R):
        blib.cylinder(bm, 1.62, -leg - 2.6, -leg, 8, center=(x, 0), bottom=True, top=True)
    tips = new_obj('magnet_tips', bm, flat=False)
    for ob in (body, tips):
        blib.triangulate(ob)
    n = blib.export_md3('models/rearmed/loot/magnet.md3',
                        [('body', body, 'models/rearmed/loot/magnet'), ('tips', tips, 'models/rearmed/loot/magnettip')])
    print('magnet tris', n)
    rgb = np.ones((32, 32, 3)) * np.array([0.85, 0.12, 0.1])[None, None, :]
    rgb[:, :4] *= 0.6
    blib.save_tga('models/rearmed/loot/magnet.tga', rgb)
    blib.save_tga('models/rearmed/loot/magnettip.tga', np.ones((32, 32, 3)) * 0.82)


# ---------------------------------------------------------------- chests
def box(bm, x0, x1, y0, y1, z0, z1):
    vs = [bm.verts.new((x, y, z)) for z in (z0, z1) for y in (y0, y1) for x in (x0, x1)]
    for q in ((0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)):
        bm.faces.new([vs[i] for i in q])


def half_cyl(bm, x0, x1, r, zc, segs, y_scale=1.0):
    """A lid: half cylinder along x (top half), closed ends."""
    arc = [(r * math.cos(math.pi * i / segs) * y_scale, zc + r * math.sin(math.pi * i / segs)) for i in range(segs + 1)]
    a = [bm.verts.new((x0, y, z)) for y, z in arc]
    b = [bm.verts.new((x1, y, z)) for y, z in arc]
    for i in range(segs):
        bm.faces.new((a[i], a[i + 1], b[i + 1], b[i]))
    bm.faces.new(list(reversed(a)))
    bm.faces.new(b)
    bm.faces.new((a[0], b[0], b[-1], a[-1]))


def build_chest():
    blib.clear_objects()
    W, D, H, R = 11.0, 7.5, 10.0, 7.5
    bm = bmesh.new()
    box(bm, -W, W, -D, D, 0, H)
    half_cyl(bm, -W, W, R, H, 8, D / R)
    blib.box_uv(bm, 16.0, long_axis=0)
    wood = new_obj('chest_wood', bm)
    bm = bmesh.new()
    for x in (-W + 2.2, W - 3.8):
        box(bm, x, x + 1.6, -D - 0.35, D + 0.35, -0.2, H + 0.2)
        half_cyl(bm, x, x + 1.6, R + 0.4, H, 8, (D + 0.35) / (R + 0.4))
    box(bm, -1.8, 1.8, -D - 0.7, -D + 0.1, H - 3.5, H + 1.5)     # the lock
    box(bm, -W - 0.3, W + 0.3, -D - 0.3, D + 0.3, -0.3, 1.2)     # base rim
    blib.box_uv(bm, 6.0)
    trim = new_obj('chest_trim', bm)
    for ob in (wood, trim):
        blib.triangulate(ob)
    for q in range(3):
        n = blib.export_md3('models/rearmed/loot/chest%d.md3' % q,
                            [('wood', wood, 'models/rearmed/loot/chestwood'), ('trim', trim, 'models/rearmed/loot/trim%d' % q)])
        print('chest tris', n)


def paint_chest():
    h, w = 128, 128
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    rng = np.random.default_rng(5)
    plank = (y // 32).astype(int)
    tone = 0.85 + 0.15 * rng.random(4)[plank % 4]
    grain = 0.5 + 0.5 * np.sin(x * 0.11 + np.sin(y * 0.21 + plank * 1.7) * 2.5 + plank * 3.0)
    base = np.array([0.42, 0.25, 0.13])
    rgb = base[None, None, :] * (tone * (0.8 + 0.2 * grain))[..., None]
    seam = (y % 32) < 2
    rgb[seam] *= 0.4
    blib.save_tga('models/rearmed/loot/chestwood.tga', np.clip(rgb, 0, 1))
    for q in range(3):
        c = np.array(TRIM_RGB[q])
        hh = ww = 64
        yy, xx = np.mgrid[0:hh, 0:ww].astype(np.float32)
        metal = 0.75 + 0.25 * np.sin(xx * 0.3 + yy * 0.05)
        rivet = (((xx % 16) - 8) ** 2 + ((yy % 16) - 8) ** 2) < 5
        rgb = c[None, None, :] * metal[..., None]
        rgb[rivet] = np.minimum(1, c * 1.5 + 0.2)
        blib.save_tga('models/rearmed/loot/trim%d.tga' % q, np.clip(rgb, 0, 1))


# ---------------------------------------------------------------- shader
def write_shader():
    parts = ['// REARMED DESCENT v2 loot: gems, the vacuum magnet, chests (generated by tools/blender/make_loot.py)\n']
    for t in range(3):
        parts.append(f"""models/rearmed/loot/gem{t}
{{
	nopicmip
	{{
		map models/rearmed/loot/gem{t}.tga
		rgbGen identity
	}}
	{{
		map models/rearmed/loot/gemenv.tga
		blendFunc GL_ONE GL_ONE
		tcGen environment
		rgbGen wave sin 0.7 0.3 {t * 0.3:.1f} 0.9
	}}
}}
""")
    for name in ('magnet', 'magnettip'):
        parts.append(f"""models/rearmed/loot/{name}
{{
	{{
		map models/rearmed/loot/{name}.tga
		rgbGen identity
	}}
	{{
		map models/rearmed/loot/gemenv.tga
		blendFunc GL_ONE GL_ONE
		tcGen environment
	}}
}}
""")
    parts.append("""models/rearmed/loot/chestwood
{
	{
		map models/rearmed/loot/chestwood.tga
		rgbGen lightingDiffuse
	}
}
""")
    for q in range(3):
        parts.append(f"""models/rearmed/loot/trim{q}
{{
	{{
		map models/rearmed/loot/trim{q}.tga
		rgbGen lightingDiffuse
	}}
	{{
		map models/rearmed/loot/gemenv.tga
		blendFunc GL_ONE GL_ONE
		tcGen environment
		rgbGen wave sin 0.35 0.15 0 0.5
	}}
}}
""")
    SHADER.parent.mkdir(parents=True, exist_ok=True)
    SHADER.write_bytes('\n'.join(parts).replace('\n', '\r\n').encode('ascii'))
    print('wrote', SHADER)


def main():
    blib.setup_scene()
    for t in range(3):
        ob = build_gem(t)
        blib.triangulate(ob)
        n = blib.export_md3('models/rearmed/loot/gem%d.md3' % t, [('gem', ob, 'models/rearmed/loot/gem%d' % t)])
        print('gem%d tris' % t, n)
        paint_gem(t)
    paint_env()
    build_magnet()
    build_chest()
    paint_chest()
    write_shader()


main()
