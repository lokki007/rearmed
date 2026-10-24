"""Minimal Quake 3 MD3 writer + reader/validator (single frame, no tags).

Used by tools/blender/make_models.py (runs inside Blender's Python), and standalone:
    python tools/md3.py <file.md3> [...]        validate + print a summary
    python tools/md3.py --all                   validate every md3 under assets/models/rearmed

Conventions (Quake 3): +Z up, units ~ inches (a player is 56 tall), xyz stored as short(v * 64),
normals as two bytes (azimuth << 8 | polar), triangles wound clockwise seen from the front,
texcoord t runs downwards (t = 1 - v of a Blender UV).
"""
import math
import os
import struct
import sys

IDENT = b'IDP3'
VERSION = 15
MAX_VERTS = 999        # renderer limit per surface: numVerts < SHADER_MAX_VERTEXES (1000)
MAX_TRIS = 1999        # numTriangles * 3 < SHADER_MAX_INDEXES (6000)
MAX_SURFACES = 32
XYZ_SCALE = 64.0

HDR = struct.Struct('<4si64s9i')             # 108 bytes
FRAME = struct.Struct('<3f3f3ff16s')          # 56 bytes
SURF = struct.Struct('<4s64s10i')             # 108 bytes
SHADER = struct.Struct('<64si')               # 68 bytes


def encode_normal(n):
    x, y, z = n
    l = math.sqrt(x * x + y * y + z * z) or 1.0
    x, y, z = x / l, y / l, z / l
    if abs(z) > 0.99999 and abs(x) < 1e-5 and abs(y) < 1e-5:
        return 0 if z > 0 else 128          # lat 0, lng 0 or 128 (straight down)
    lng = int(round(math.acos(max(-1.0, min(1.0, z))) * 256.0 / (2 * math.pi))) & 0xff
    lat = int(round(math.atan2(y, x) * 256.0 / (2 * math.pi))) & 0xff
    return (lat << 8) | lng


def decode_normal(v):
    lat = ((v >> 8) & 0xff) * 2 * math.pi / 256.0
    lng = (v & 0xff) * 2 * math.pi / 256.0
    return (math.cos(lat) * math.sin(lng), math.sin(lat) * math.sin(lng), math.cos(lng))


def _name(s, n):
    b = s.encode('ascii')
    if len(b) >= n:
        raise ValueError(f'name too long ({len(b)} >= {n}): {s}')
    return b + b'\0' * (n - len(b))


def write_md3(path, surfaces, model_name='rearmed'):
    """surfaces: list of dicts {name, shader, verts:[(x,y,z)], normals:[(x,y,z)], uvs:[(s,t)], tris:[(a,b,c)]}
    tris are given in Q3 order already (clockwise from the front); uvs in Q3 convention."""
    if not surfaces or len(surfaces) > MAX_SURFACES:
        raise ValueError('bad surface count')
    allv = [v for s in surfaces for v in s['verts']]
    mins = [min(v[i] for v in allv) for i in range(3)]
    maxs = [max(v[i] for v in allv) for i in range(3)]
    radius = max(math.sqrt(sum(c * c for c in v)) for v in allv)
    for v in allv:
        for c in v:
            if abs(c) * XYZ_SCALE > 32767:
                raise ValueError('vertex out of md3 range (+-511 units)')

    surf_blobs = []
    for s in surfaces:
        nv, nt = len(s['verts']), len(s['tris'])
        if nv > MAX_VERTS or nt > MAX_TRIS:
            raise ValueError(f"surface {s['name']}: {nv} verts / {nt} tris over the limit")
        ofs_shaders = SURF.size
        ofs_tris = ofs_shaders + SHADER.size
        ofs_st = ofs_tris + 12 * nt
        ofs_xyz = ofs_st + 8 * nv
        ofs_end = ofs_xyz + 8 * nv
        out = bytearray()
        out += SURF.pack(IDENT, _name(s['name'], 64), 0, 1, 1, nv, nt,
                         ofs_tris, ofs_shaders, ofs_st, ofs_xyz, ofs_end)
        out += SHADER.pack(_name(s['shader'], 64), 0)
        for t in s['tris']:
            out += struct.pack('<3i', *t)
        for st in s['uvs']:
            out += struct.pack('<2f', *st)
        for v, n in zip(s['verts'], s['normals']):
            out += struct.pack('<3hH', *(int(round(c * XYZ_SCALE)) for c in v), encode_normal(n))
        assert len(out) == ofs_end
        surf_blobs.append(bytes(out))

    ofs_frames = HDR.size
    ofs_tags = ofs_frames + FRAME.size
    ofs_surfs = ofs_tags
    ofs_end = ofs_surfs + sum(len(b) for b in surf_blobs)
    data = bytearray()
    data += HDR.pack(IDENT, VERSION, _name(model_name, 64), 0, 1, 0, len(surfaces), 0,
                     ofs_frames, ofs_tags, ofs_surfs, ofs_end)
    data += FRAME.pack(*mins, *maxs, 0.0, 0.0, 0.0, radius, _name('frame0', 16))
    for b in surf_blobs:
        data += b
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)
    return len(data)


def read_md3(path):
    """Parse + validate. Returns a dict; raises ValueError on any structural problem."""
    with open(path, 'rb') as f:
        d = f.read()
    if len(d) < HDR.size:
        raise ValueError('file too small')
    (ident, ver, name, flags, nfr, ntag, nsurf, nskin, ofr, otag, osurf, oend) = HDR.unpack_from(d, 0)
    if ident != IDENT or ver != VERSION:
        raise ValueError(f'bad ident/version {ident} {ver}')
    if oend != len(d):
        raise ValueError(f'ofsEnd {oend} != file size {len(d)}')
    if nfr < 1 or nsurf < 1 or nsurf > MAX_SURFACES:
        raise ValueError('bad frame/surface counts')
    fr = FRAME.unpack_from(d, ofr)
    mins, maxs, radius = fr[0:3], fr[3:6], fr[9]
    out = {'name': name.split(b'\0')[0].decode(), 'frames': nfr, 'tags': ntag, 'mins': mins,
           'maxs': maxs, 'radius': radius, 'surfaces': []}
    o = osurf
    for i in range(nsurf):
        (sid, sname, sflags, snfr, nsh, nv, nt, otri, osh, ost, oxyz, send) = SURF.unpack_from(d, o)
        if sid != IDENT:
            raise ValueError(f'surface {i}: bad ident')
        if snfr != nfr or nsh < 1 or nv < 3 or nt < 1 or nv > MAX_VERTS or nt > MAX_TRIS:
            raise ValueError(f'surface {i}: bad counts fr={snfr} sh={nsh} v={nv} t={nt}')
        if o + send > len(d):
            raise ValueError(f'surface {i}: runs past end of file')
        shader = SHADER.unpack_from(d, o + osh)[0].split(b'\0')[0].decode()
        tris = [struct.unpack_from('<3i', d, o + otri + 12 * k) for k in range(nt)]
        for t in tris:
            if min(t) < 0 or max(t) >= nv:
                raise ValueError(f'surface {i}: triangle index out of range')
            if len(set(t)) != 3:
                raise ValueError(f'surface {i}: degenerate triangle {t}')
        st = [struct.unpack_from('<2f', d, o + ost + 8 * k) for k in range(nv)]
        xyz = []
        nrm = []
        for k in range(nv):
            x, y, z, n = struct.unpack_from('<3hH', d, o + oxyz + 8 * k)
            xyz.append((x / XYZ_SCALE, y / XYZ_SCALE, z / XYZ_SCALE))
            nrm.append(decode_normal(n))
        for p in xyz:
            for c in range(3):
                if p[c] < mins[c] - 0.05 or p[c] > maxs[c] + 0.05:
                    raise ValueError(f'surface {i}: vertex {p} outside frame bounds')
        for s, t in st:
            if not (math.isfinite(s) and math.isfinite(t)):
                raise ValueError(f'surface {i}: non-finite texcoord')
        # facing check: winding (clockwise in Q3) should agree with the vertex normals
        agree = 0
        for a, b, c in tris:
            pa, pb, pc = xyz[a], xyz[b], xyz[c]
            u = [pb[j] - pa[j] for j in range(3)]
            v = [pc[j] - pa[j] for j in range(3)]
            fn = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
            vn = [nrm[a][j] + nrm[b][j] + nrm[c][j] for j in range(3)]
            agree += 1 if sum(fn[j] * vn[j] for j in range(3)) < 0 else 0   # CW => geometric normal flipped
        out['surfaces'].append({'name': sname.split(b'\0')[0].decode(), 'shader': shader, 'verts': nv,
                                'tris': nt, 'facing_ok': agree / nt, 'xyz': xyz, 'st': st,
                                'normals': nrm, 'triangles': tris})
        o += send
    if o != oend:
        raise ValueError('surfaces do not end at ofsEnd')
    return out


def summary(path):
    m = read_md3(path)
    size = [m['maxs'][i] - m['mins'][i] for i in range(3)]
    lines = [f"{path}: size {size[0]:.1f} x {size[1]:.1f} x {size[2]:.1f}  z {m['mins'][2]:.1f}..{m['maxs'][2]:.1f}  r {m['radius']:.1f}"]
    for s in m['surfaces']:
        lines.append(f"   {s['name']:<10} {s['shader']:<40} {s['verts']:4d} v {s['tris']:4d} t  facing {s['facing_ok'] * 100:.0f}%")
    return '\n'.join(lines), m


if __name__ == '__main__':
    args = sys.argv[1:]
    if args == ['--all']:
        root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'assets', 'models', 'rearmed')
        args = sorted(os.path.join(dp, f) for dp, _, fs in os.walk(root) for f in fs if f.endswith('.md3'))
    bad = 0
    for p in args:
        try:
            txt, m = summary(p)
            print(txt)
            for s in m['surfaces']:
                if s['facing_ok'] < 0.9:
                    print('   WARNING: winding disagrees with normals on', s['name'])
                    bad += 1
        except (ValueError, struct.error) as e:
            print(f'{p}: INVALID: {e}')
            bad += 1
    sys.exit(1 if bad else 0)
