"""Contact sheet of the exported MD3s (reads the .md3 files back, so it also checks the writer).
blender --background --factory-startup --python tools/blender/preview.py -- <out.png> [tile_px]"""
import math
import os
import sys

import bpy
import numpy as np
from mathutils import Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blib  # noqa: E402
import md3  # noqa: E402

MODELS = ['props/barrel', 'props/crate', 'props/tank', 'meteor', 'gibs/head',
          'gibs/meat1', 'gibs/meat2', 'gibs/meat3', 'gibs/meat4', 'gibs/meat5', 'gibs/meat6',
          'debris/rock1', 'debris/rock2', 'debris/rock3', 'debris/metal1', 'debris/metal2',
          'debris/wood1', 'debris/wood2', 'debris/wood3']
GLOW = {'models/rearmed/meteor': 'models/rearmed/meteor_glow.tga'}


def load_image(rel):
    p = os.path.join(blib.ASSETS, rel)
    return bpy.data.images.load(p) if os.path.exists(p) else None


def make_material(shader):
    m = bpy.data.materials.new(shader)
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes['Principled BSDF']
    bsdf.inputs['Roughness'].default_value = 0.55
    img = load_image(shader + '.tga')
    if img:
        tex = nt.nodes.new('ShaderNodeTexImage')
        tex.image = img
        nt.links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
    if shader in GLOW:
        g = nt.nodes.new('ShaderNodeTexImage')
        g.image = load_image(GLOW[shader])
        nt.links.new(g.outputs['Color'], bsdf.inputs['Emission Color'])
        bsdf.inputs['Emission Strength'].default_value = 2.0
    if 'gibs' in shader:
        bsdf.inputs['Roughness'].default_value = 0.25
    return m


def import_md3(path, name):
    m = md3.read_md3(path)
    obs = []
    for s in m['surfaces']:
        me = bpy.data.meshes.new(name)
        tris = [(a, c, b) for a, b, c in s['triangles']]          # Q3 clockwise -> Blender CCW
        me.from_pydata(s['xyz'], [], tris)
        uvl = me.uv_layers.new()
        for poly in me.polygons:
            for li in poly.loop_indices:
                vi = me.loops[li].vertex_index
                st = s['st'][vi]
                uvl.data[li].uv = (st[0], 1.0 - st[1])
        me.normals_split_custom_set_from_vertices([Vector(n) for n in s['normals']])
        for p in me.polygons:
            p.use_smooth = True
        me.materials.append(make_material(s['shader']))
        ob = bpy.data.objects.new(name, me)
        bpy.context.scene.collection.objects.link(ob)
        obs.append(ob)
    return obs, m


def main():
    argv = sys.argv[sys.argv.index('--') + 1:]
    out = argv[0]
    tile = int(argv[1]) if len(argv) > 1 else 320
    models = argv[2].split(',') if len(argv) > 2 and argv[2] else MODELS
    dirs = [Vector([float(c) for c in d.split(',')]).normalized() for d in argv[3].split(';')] if len(argv) > 3 else [Vector((1.0, -0.75, 0.55)).normalized()]
    sc = bpy.context.scene
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob)
    sc.render.engine = 'CYCLES'
    try:
        prefs = bpy.context.preferences.addons['cycles'].preferences
        prefs.compute_device_type = 'OPTIX'
        prefs.get_devices()
        for d in prefs.devices:
            d.use = d.type == 'OPTIX'
        sc.cycles.device = 'GPU'
    except Exception:
        pass
    sc.cycles.samples = 24
    sc.cycles.use_denoising = True
    sc.render.resolution_x = sc.render.resolution_y = tile
    sc.render.film_transparent = False
    sc.view_settings.view_transform = 'Standard'
    world = bpy.data.worlds.new('w')
    world.use_nodes = True
    world.node_tree.nodes['Background'].inputs['Color'].default_value = (0.16, 0.17, 0.19, 1)
    world.node_tree.nodes['Background'].inputs['Strength'].default_value = 0.8
    sc.world = world
    sun = bpy.data.objects.new('sun', bpy.data.lights.new('sun', 'SUN'))
    sun.data.energy = 3.5
    sun.rotation_euler = (math.radians(50), 0, math.radians(35))
    sc.collection.objects.link(sun)
    cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
    cam.data.lens = 50
    sc.collection.objects.link(cam)
    sc.camera = cam
    tiles = []
    for name, direc in [(n, d) for n in models for d in dirs]:
        path = os.path.join(blib.ASSETS, 'models', 'rearmed', name + '.md3')
        if not os.path.exists(path):
            tiles.append(np.zeros((tile, tile, 4), np.float32))
            continue
        obs, m = import_md3(path, name)
        mn, mx = Vector(m['mins']), Vector(m['maxs'])
        c = (mn + mx) / 2
        r = (mx - mn).length / 2
        d = r / math.tan(cam.data.angle / 2) * 1.05
        cam.location = c + direc * d
        cam.rotation_euler = (-direc).to_track_quat('-Z', 'Y').to_euler()
        tmp = os.path.join(os.path.dirname(out), '_tile.png')
        sc.render.filepath = tmp
        bpy.ops.render.render(write_still=True)
        img = bpy.data.images.load(tmp)
        buf = np.empty(tile * tile * 4, np.float32)
        img.pixels.foreach_get(buf)
        tiles.append(buf.reshape(tile, tile, 4))
        bpy.data.images.remove(img)
        for ob in obs:
            bpy.data.objects.remove(ob)
    cols = min(5, len(tiles))
    rows = (len(tiles) + cols - 1) // cols
    sheet = np.zeros((rows * tile, cols * tile, 4), np.float32)
    sheet[..., 3] = 1
    for i, t in enumerate(tiles):
        rr, cc = i // cols, i % cols
        y0 = (rows - 1 - rr) * tile        # images are bottom-up
        sheet[y0:y0 + tile, cc * tile:(cc + 1) * tile] = t
        sheet[y0:y0 + tile, cc * tile:cc * tile + 1, :3] = 0
        sheet[y0:y0 + 1, cc * tile:(cc + 1) * tile, :3] = 0
    img = bpy.data.images.new('sheet', cols * tile, rows * tile, alpha=True)
    img.pixels.foreach_set(sheet.ravel())
    img.filepath_raw = out
    img.file_format = 'PNG'
    img.save()
    print('wrote', out)


main()
