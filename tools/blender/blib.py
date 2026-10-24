"""Blender-side helpers for the REARMED asset generator: mesh building, UVs, map baking,
numpy noise, TGA output and MD3 export. Imported by make_models.py (runs inside Blender)."""
import math
import os
import sys
import time

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
ROOT = os.path.dirname(TOOLS)
ASSETS = os.path.join(ROOT, 'assets')
sys.path.insert(0, TOOLS)
sys.path.insert(0, HERE)
import md3  # noqa: E402


# ---------------------------------------------------------------- scene
def setup_scene():
    for coll in (bpy.data.objects, bpy.data.meshes, bpy.data.materials, bpy.data.images,
                 bpy.data.cameras, bpy.data.lights):
        for item in list(coll):
            coll.remove(item)
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    try:
        prefs = bpy.context.preferences.addons['cycles'].preferences
        prefs.compute_device_type = 'OPTIX'
        prefs.get_devices()
        for d in prefs.devices:
            d.use = d.type == 'OPTIX'
        sc.cycles.device = 'GPU'
    except Exception as e:  # CPU fallback is fine, just slower
        print('GPU unavailable:', e)
        sc.cycles.device = 'CPU'
    sc.cycles.samples = 48
    sc.render.bake.margin = 16
    sc.render.bake.use_clear = True
    sc.view_settings.view_transform = 'Standard'


def clear_objects():
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    for me in list(bpy.data.meshes):
        bpy.data.meshes.remove(me)


def obj_from_bm(name, bm, mats=()):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    for m in mats:
        me.materials.append(get_mat(m))
    for p in me.polygons:
        p.use_smooth = True
    return ob


def get_mat(name):
    m = bpy.data.materials.get(name)
    if m is None:
        m = bpy.data.materials.new(name)
    return m


def auto_sharp(bm, angle_deg=40.0):
    lim = math.radians(angle_deg)
    for e in bm.edges:
        if len(e.link_faces) == 2:
            e.smooth = e.calc_face_angle(0) < lim
        else:
            e.smooth = False


def uv_layer(bm):
    return bm.loops.layers.uv.verify()


# ---------------------------------------------------------------- lathe / primitives
def lathe(bm, profile, segs, v_rect, u_rect=(0.0, 1.0), mat=0, cap_bottom=None, cap_top=None, phase=0.0,
          radial=None):
    """Revolve profile [(r, z), ...] (bottom to top, solid on the left) around Z.
    Side UVs: u around in u_rect, v by arc length in v_rect. Caps: (cx, cy, R, mat) disc in UV space.
    radial(ring_index, angle, r, z) -> (r, z) lets callers dent/perturb rings. Returns rings."""
    uvl = uv_layer(bm)
    L = [0.0]
    for i in range(1, len(profile)):
        (r0, z0), (r1, z1) = profile[i - 1], profile[i]
        L.append(L[-1] + math.hypot(r1 - r0, z1 - z0))
    vs = [v_rect[0] + (v_rect[1] - v_rect[0]) * l / L[-1] for l in L]
    rings = []
    for i, (r, z) in enumerate(profile):
        ring = []
        for j in range(segs):
            a = phase + 2 * math.pi * j / segs
            rr, zz = (r, z) if radial is None else radial(i, a, r, z)
            ring.append(bm.verts.new((rr * math.cos(a), rr * math.sin(a), zz)))
        rings.append(ring)
    for i in range(len(profile) - 1):
        for j in range(segs):
            j2 = (j + 1) % segs
            f = bm.faces.new((rings[i][j], rings[i][j2], rings[i + 1][j2], rings[i + 1][j]))
            f.material_index = mat
            u0 = u_rect[0] + (u_rect[1] - u_rect[0]) * j / segs
            u1 = u_rect[0] + (u_rect[1] - u_rect[0]) * (j + 1) / segs
            for loop, uv in zip(f.loops, ((u0, vs[i]), (u1, vs[i]), (u1, vs[i + 1]), (u0, vs[i + 1]))):
                loop[uvl].uv = uv
    for cap, ring, down in ((cap_bottom, rings[0], True), (cap_top, rings[-1], False)):
        if cap is None:
            continue
        cx, cy, R, cmat = cap[:4]
        z = sum(v.co.z for v in ring) / len(ring)
        ctr = bm.verts.new((0, 0, z + (cap[4] if len(cap) > 4 else 0.0)))
        rmax = max(v.co.xy.length for v in ring)
        for j in range(segs):
            j2 = (j + 1) % segs
            vv = (ring[j2], ring[j], ctr) if down else (ring[j], ring[j2], ctr)
            f = bm.faces.new(vv)
            f.material_index = cmat
            for loop in f.loops:
                co = loop.vert.co
                loop[uvl].uv = (cx + R * co.x / rmax, cy + R * co.y / rmax)
    return rings


def cylinder(bm, r, z0, z1, segs, center=(0, 0), uv_rect=(0, 0, 1, 1), mat=0, top=True, bottom=False):
    """Small closed cylinder (for bungs, valves). Box-ish UVs packed into uv_rect (x0, y0, x1, y1)."""
    x0, y0, x1, y1 = uv_rect
    prof = [(r, z0), (r, z1)]
    capw = (x1 - x0) * 0.25
    cb = (x0 + capw, y0 + capw, capw * 0.95, mat) if bottom else None
    ct = (x1 - capw, y0 + capw, capw * 0.95, mat) if top else None
    nv = len(bm.verts)
    lathe(bm, prof, segs, (y0 + 2 * capw, y1), (x0, x1), mat, cb, ct)
    bm.verts.ensure_lookup_table()
    for v in bm.verts[nv:]:
        v.co.x += center[0]
        v.co.y += center[1]


def uv_fit(ob, rect, margin=0.0):
    """Linearly fit all UVs of `ob` into rect (x0, y0, x1, y1), keeping aspect."""
    uv = ob.data.uv_layers.active.data
    us = [d.uv.x for d in uv]
    vs = [d.uv.y for d in uv]
    u0, u1, v0, v1 = min(us), max(us), min(vs), max(vs)
    x0, y0, x1, y1 = rect
    s = min((x1 - x0 - 2 * margin) / max(u1 - u0, 1e-6), (y1 - y0 - 2 * margin) / max(v1 - v0, 1e-6))
    for d in uv:
        d.uv = ((d.uv.x - u0) * s + x0 + margin, (d.uv.y - v0) * s + y0 + margin)


def box_uv(bm, scale, long_axis=0, cells=None):
    """Planar projection per face by its dominant normal axis. `scale` = world units per UV unit.
    The long axis maps to v (texture grain runs along v)."""
    uvl = uv_layer(bm)
    for f in bm.faces:
        n = f.normal
        k = max(range(3), key=lambda i: abs(n[i]))
        a, b = [i for i in range(3) if i != k]
        if long_axis in (a, b) and b != long_axis:
            a, b = b, a
        for loop in f.loops:
            co = loop.vert.co
            loop[uvl].uv = (co[a] / scale, co[b] / scale)


def transform_new(bm, nv, mat):
    """Transform the verts created after the mesh had `nv` verts."""
    bm.verts.ensure_lookup_table()
    for v in bm.verts[nv:]:
        v.co = mat @ v.co


# ---------------------------------------------------------------- UV utilities
def smart_uv(ob, angle=66.0, margin=0.03):
    bpy.context.view_layer.objects.active = ob
    for o in bpy.context.view_layer.objects:
        o.select_set(o == ob)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(angle), island_margin=margin, scale_to_bounds=False)
    bpy.ops.object.mode_set(mode='OBJECT')


def uv_world_scale(ob, units_per_tile):
    """Rescale an object's UVs so one texture repeat covers `units_per_tile` world units."""
    me = ob.data
    uv = me.uv_layers.active.data
    area3 = sum(p.area for p in me.polygons)
    area2 = 0.0
    for p in me.polygons:
        pts = [uv[li].uv for li in p.loop_indices]
        a = 0.0
        for i in range(len(pts)):
            a += pts[i].x * pts[(i + 1) % len(pts)].y - pts[(i + 1) % len(pts)].x * pts[i].y
        area2 += abs(a) / 2
    s = math.sqrt(area3 / max(area2, 1e-9)) / units_per_tile
    for d in uv:
        d.uv = d.uv * s


# ---------------------------------------------------------------- baking data maps
PASSES = ('pos', 'nrm', 'ao', 'edge', 'id')


def _setup_bake_material(mat, slot, img, pass_name, ao_dist, bevel_r):
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    em = nt.nodes.new('ShaderNodeEmission')
    nt.links.new(em.outputs[0], out.inputs['Surface'])
    tex = nt.nodes.new('ShaderNodeTexImage')
    tex.image = img
    nt.nodes.active = tex
    geo = nt.nodes.new('ShaderNodeNewGeometry')
    if pass_name in ('pos', 'nrm'):
        ma = nt.nodes.new('ShaderNodeVectorMath')
        ma.operation = 'MULTIPLY_ADD'
        nt.links.new(geo.outputs['Position' if pass_name == 'pos' else 'Normal'], ma.inputs[0])
        s = 0.01 if pass_name == 'pos' else 0.5
        ma.inputs[1].default_value = (s, s, s)
        ma.inputs[2].default_value = (0.5, 0.5, 0.5)
        nt.links.new(ma.outputs[0], em.inputs['Color'])
    elif pass_name == 'ao':
        ao = nt.nodes.new('ShaderNodeAmbientOcclusion')
        ao.samples = 32
        ao.only_local = True
        ao.inputs['Distance'].default_value = ao_dist
        nt.links.new(ao.outputs['AO'], em.inputs['Color'])
    elif pass_name == 'edge':
        bev = nt.nodes.new('ShaderNodeBevel')
        bev.samples = 16
        bev.inputs['Radius'].default_value = bevel_r
        dot = nt.nodes.new('ShaderNodeVectorMath')
        dot.operation = 'DOT_PRODUCT'
        nt.links.new(bev.outputs['Normal'], dot.inputs[0])
        nt.links.new(geo.outputs['Normal'], dot.inputs[1])
        nt.links.new(dot.outputs['Value'], em.inputs['Color'])
    elif pass_name == 'id':
        em.inputs['Color'].default_value = ((slot + 0.5) / 16.0, 0, 0, 1)


def bake_maps(ob, size, ao_dist=6.0, bevel_r=0.6, passes=PASSES):
    """Bake object data maps into numpy arrays (H, W, 3), rows bottom-up (row 0 = v 0).
    size: int (square) or (w, h)."""
    w, h = (size, size) if isinstance(size, int) else size
    bpy.context.view_layer.objects.active = ob
    for o in bpy.context.view_layer.objects:
        o.select_set(o == ob)
    img = bpy.data.images.new('bake_' + ob.name, w, h, alpha=False, float_buffer=True)
    img.colorspace_settings.name = 'Non-Color'
    res = {}
    for p in passes:
        for slot, mat in enumerate(ob.data.materials):
            _setup_bake_material(mat, slot, img, p, ao_dist, bevel_r)
        bpy.context.scene.cycles.samples = 64 if p in ('ao', 'edge') else 1
        t0 = time.time()
        bpy.ops.object.bake(type='EMIT', margin=16, use_clear=True)
        print(f'  bake {ob.name}.{p}: {time.time() - t0:.1f}s')
        buf = np.empty(w * h * 4, np.float32)
        img.pixels.foreach_get(buf)
        a = buf.reshape(h, w, 4)[..., :3].copy()
        if p == 'pos':
            a = (a - 0.5) / 0.01
        elif p == 'nrm':
            a = (a - 0.5) / 0.5
            a /= np.linalg.norm(a, axis=-1, keepdims=True) + 1e-6
        elif p in ('ao', 'edge'):
            a = a[..., 0]
        elif p == 'id':
            a = np.floor(a[..., 0] * 16.0).astype(np.int32)
        res[p] = a
    bpy.data.images.remove(img)
    if os.environ.get('REARMED_DUMP'):
        np.savez(os.path.join(os.environ['REARMED_DUMP'], ob.name + '.npz'), **res)
    # uv grid (bottom-up)
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    res['u'] = (x + 0.5) / w
    res['v'] = (y + 0.5) / h
    return res


# ---------------------------------------------------------------- numpy noise
_TABLES = {}


def _table(seed):
    if seed not in _TABLES:
        _TABLES[seed] = np.random.default_rng(seed).random(4096).astype(np.float32)
    return _TABLES[seed]


def vnoise3(p, seed=0):
    """Value noise in [0,1] at points p (..., 3)."""
    t = _table(seed)
    pi = np.floor(p).astype(np.int64)
    f = (p - pi).astype(np.float32)
    f = f * f * (3 - 2 * f)
    ix, iy, iz = pi[..., 0], pi[..., 1], pi[..., 2]

    def h(dx, dy, dz):
        return t[((ix + dx) * 73856093 ^ (iy + dy) * 19349663 ^ (iz + dz) * 83492791) & 4095]
    fx, fy, fz = f[..., 0], f[..., 1], f[..., 2]
    x00 = h(0, 0, 0) * (1 - fx) + h(1, 0, 0) * fx
    x10 = h(0, 1, 0) * (1 - fx) + h(1, 1, 0) * fx
    x01 = h(0, 0, 1) * (1 - fx) + h(1, 0, 1) * fx
    x11 = h(0, 1, 1) * (1 - fx) + h(1, 1, 1) * fx
    y0 = x00 * (1 - fy) + x10 * fy
    y1 = x01 * (1 - fy) + x11 * fy
    return y0 * (1 - fz) + y1 * fz


def fbm3(p, octaves=4, seed=0, lac=2.03, gain=0.5):
    tot, amp, norm = 0.0, 1.0, 0.0
    q = p
    for o in range(octaves):
        tot = tot + vnoise3(q, seed + o * 17) * amp
        norm += amp
        amp *= gain
        q = q * lac + 3.17
    return tot / norm


def voronoi_f1f2(p, pts):
    """Distances to nearest and 2nd nearest feature points: p (..., 3), pts (N, 3)."""
    flat = p.reshape(-1, 3)
    f1 = np.full(flat.shape[0], 1e9, np.float32)
    f2 = np.full(flat.shape[0], 1e9, np.float32)
    for q in pts:
        d = np.sqrt(((flat - q) ** 2).sum(-1))
        f2 = np.where(d < f1, f1, np.minimum(f2, d))
        f1 = np.minimum(f1, d)
    return f1.reshape(p.shape[:-1]), f2.reshape(p.shape[:-1])


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def lerp(a, b, t):
    t = np.asarray(t)
    if t.ndim and t.shape[-1] != 3:
        t = t[..., None]
    return a * (1 - t) + b * t


def col(r, g, b):
    return np.array([r, g, b], np.float32)


# ---------------------------------------------------------------- output
def save_tga(rel, rgb, alpha=None):
    """rgb (H, W, 3) floats 0..1, rows bottom-up (TGA's native origin). Uncompressed 24/32-bit."""
    path = os.path.join(ASSETS, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    h, w = rgb.shape[:2]
    c = np.clip(rgb * 255 + 0.5, 0, 255).astype(np.uint8)
    chans = [c[..., 2], c[..., 1], c[..., 0]]
    if alpha is not None:
        chans.append(np.clip(alpha * 255 + 0.5, 0, 255).astype(np.uint8))
    data = np.stack(chans, -1)
    bpp = 8 * data.shape[-1]
    hdr = bytes([0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, w & 255, w >> 8, h & 255, h >> 8, bpp, 8 if alpha is not None else 0])
    with open(path, 'wb') as f:
        f.write(hdr)
        f.write(data.tobytes())
    print('wrote', rel)


def load_png_rgba(path):
    img = bpy.data.images.load(path)
    w, h = img.size
    buf = np.empty(w * h * 4, np.float32)
    img.pixels.foreach_get(buf)
    bpy.data.images.remove(img)
    return buf.reshape(h, w, 4)   # bottom-up


def sample(img, u, v):
    """Bilinear-free nearest sample of a bottom-up image at uv arrays (wrapping u, clamping v)."""
    h, w = img.shape[:2]
    x = (np.mod(u, 1.0) * w).astype(np.int32) % w
    y = np.clip((v * h).astype(np.int32), 0, h - 1)
    return img[y, x]


def split_surface(sname, shader, verts, norms, uvs, tris):
    """Split into several surfaces (same shader) so each stays under the renderer's vertex limit."""
    if len(verts) <= md3.MAX_VERTS and len(tris) <= md3.MAX_TRIS:
        return [{'name': sname, 'shader': shader, 'verts': verts, 'normals': norms, 'uvs': uvs, 'tris': tris}]
    # order triangles spatially (by centroid along the longest-spread axis) so chunks stay compact
    import numpy as _np
    cent = _np.array([[sum(verts[i][k] for i in t) / 3 for k in range(3)] for t in tris])
    ax = int(_np.argmax(cent.max(0) - cent.min(0)))
    order = _np.argsort(cent[:, ax])
    out, cur, remap = [], [], {}

    def flush():
        inv = sorted(remap, key=remap.get)
        out.append({'name': f'{sname}{len(out)}', 'shader': shader, 'verts': [verts[i] for i in inv],
                    'normals': [norms[i] for i in inv], 'uvs': [uvs[i] for i in inv], 'tris': list(cur)})
    for ti in order:
        t = tris[ti]
        need = sum(1 for i in t if i not in remap)
        if len(remap) + need > md3.MAX_VERTS - 2 or len(cur) >= md3.MAX_TRIS:
            flush()
            cur, remap = [], {}
        cur.append(tuple(remap.setdefault(i, len(remap)) for i in t))
    if cur:
        flush()
    return out


def export_md3(rel, parts, name='rearmed'):
    """parts: [(surface_name, object, shader)]. Objects exported in world space."""
    dg = bpy.context.evaluated_depsgraph_get()
    surfaces = []
    for sname, ob, shader in parts:
        ev = ob.evaluated_get(dg)
        me = ev.to_mesh()
        me.calc_loop_triangles()
        uv = me.uv_layers.active.data
        cn = me.corner_normals
        mw = ob.matrix_world
        nmat = mw.to_3x3().inverted().transposed()
        verts, norms, uvs, tris, index = [], [], [], [], {}
        for lt in me.loop_triangles:
            ids = []
            for li in lt.loops:
                vi = me.loops[li].vertex_index
                n = (nmat @ Vector(cn[li].vector)).normalized()
                u, v = uv[li].uv
                key = (vi, round(n.x, 3), round(n.y, 3), round(n.z, 3), round(u, 5), round(v, 5))
                k = index.get(key)
                if k is None:
                    k = len(verts)
                    index[key] = k
                    co = mw @ me.vertices[vi].co
                    verts.append(tuple(co))
                    norms.append(tuple(n))
                    uvs.append((u, 1.0 - v))
                ids.append(k)
            if len(set(ids)) == 3:
                tris.append((ids[0], ids[2], ids[1]))    # Blender CCW -> Q3 clockwise
        ev.to_mesh_clear()
        surfaces.extend(split_surface(sname, shader, verts, norms, uvs, tris))
    path = os.path.join(ASSETS, rel)
    md3.write_md3(path, surfaces, name)
    txt, _ = md3.summary(path)
    print(txt)
    return sum(len(s['tris']) for s in surfaces)


def join(objs, name):
    bpy.context.view_layer.objects.active = objs[0]
    for o in bpy.context.view_layer.objects:
        o.select_set(o in objs)
    bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    ob.name = name
    return ob


def triangulate(ob):
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bmesh.ops.triangulate(bm, faces=bm.faces[:], quad_method='BEAUTY', ngon_method='BEAUTY')
    bm.to_mesh(ob.data)
    bm.free()


def apply_modifiers(ob):
    bpy.context.view_layer.objects.active = ob
    for o in bpy.context.view_layer.objects:
        o.select_set(o == ob)
    for m in list(ob.modifiers):
        bpy.ops.object.modifier_apply(modifier=m.name)
