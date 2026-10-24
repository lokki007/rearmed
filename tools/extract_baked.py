"""Recover misc_model geometry that id baked into a BSP (their source models never shipped).

Reads the triangle soup surfaces of the world model and writes them as one ASE model in world
coordinates, to be re-inserted into the decompiled .map with a single misc_model at the origin.
usage: extract_baked.py <map.bsp> <out.ase>"""
import struct, sys, collections, pathlib

LUMP_SHADERS, LUMP_MODELS, LUMP_DRAWVERTS, LUMP_DRAWINDEXES, LUMP_SURFACES = 1, 7, 10, 11, 13
MST_TRIANGLE_SOUP = 3


def lumps(data):
    ident, version = struct.unpack_from('<4si', data, 0)
    assert ident == b'IBSP' and version == 46, (ident, version)
    return [struct.unpack_from('<ii', data, 8 + i * 8) for i in range(17)]


def main(bsp_path, out_path):
    data = pathlib.Path(bsp_path).read_bytes()
    L = lumps(data)

    def lump(i, size):
        ofs, length = L[i]
        return [data[ofs + k * size: ofs + (k + 1) * size] for k in range(length // size)]

    shaders = [s[:64].split(b'\0')[0].decode('latin1') for s in lump(LUMP_SHADERS, 72)]
    first_surf, num_surfs = struct.unpack_from('<ii', lump(LUMP_MODELS, 40)[0], 24)
    verts = [struct.unpack('<3f2f2f3f4B', v) for v in lump(LUMP_DRAWVERTS, 44)]
    ofs, length = L[LUMP_DRAWINDEXES]
    indexes = struct.unpack_from(f'<{length // 4}i', data, ofs)

    by_shader = collections.defaultdict(list)   # shader -> list of (v0, v1, v2)
    for raw in lump(LUMP_SURFACES, 104)[first_surf:first_surf + num_surfs]:
        shader, fog, stype, fv, nv, fi, ni = struct.unpack_from('<7i', raw)
        if stype != MST_TRIANGLE_SOUP:
            continue
        idx = indexes[fi:fi + ni]
        for t in range(0, ni, 3):
            by_shader[shaders[shader]].append(tuple(verts[fv + idx[t + k]] for k in range(3)))

    out = ['*3DSMAX_ASCIIEXPORT\t200', '*COMMENT "baked models recovered from %s"' % pathlib.Path(bsp_path).name,
           '*MATERIAL_LIST {', f'\t*MATERIAL_COUNT {len(by_shader)}']
    names = sorted(by_shader)
    for m, name in enumerate(names):
        out += [f'\t*MATERIAL {m} {{', f'\t\t*MATERIAL_NAME "{name}"', '\t\t*MAP_DIFFUSE {',
                f'\t\t\t*BITMAP "{name}"', '\t\t}', '\t}']
    out.append('}')
    total = 0
    for m, name in enumerate(names):
        tris = by_shader[name]
        total += len(tris)
        n = len(tris) * 3
        out += ['*GEOMOBJECT {', f'\t*NODE_NAME "surf{m}"', '\t*MESH {',
                f'\t\t*MESH_NUMVERTEX {n}', f'\t\t*MESH_NUMFACES {len(tris)}', '\t\t*MESH_VERTEX_LIST {']
        k = 0
        for tri in tris:
            for v in tri:
                out.append(f'\t\t\t*MESH_VERTEX {k}\t{v[0]:.4f}\t{v[1]:.4f}\t{v[2]:.4f}')
                k += 1
        out.append('\t\t}')
        # ASE winding is the reverse of the BSP's
        out.append('\t\t*MESH_FACE_LIST {')
        for f in range(len(tris)):
            out.append(f'\t\t\t*MESH_FACE {f}:\tA: {3 * f}\tB: {3 * f + 2}\tC: {3 * f + 1}\tAB: 1\tBC: 1\tCA: 1\t*MESH_SMOOTHING 0\t*MESH_MTLID 0')
        out += ['\t\t}', f'\t\t*MESH_NUMTVERTEX {n}', '\t\t*MESH_TVERTLIST {']
        k = 0
        for tri in tris:
            for v in tri:
                out.append(f'\t\t\t*MESH_TVERT {k}\t{v[3]:.5f}\t{1.0 - v[4]:.5f}\t0.0000')
                k += 1
        out += ['\t\t}', f'\t\t*MESH_NUMTVFACES {len(tris)}', '\t\t*MESH_TFACELIST {']
        for f in range(len(tris)):
            out.append(f'\t\t\t*MESH_TFACE {f}\t{3 * f}\t{3 * f + 2}\t{3 * f + 1}')
        out += ['\t\t}', '\t}', f'\t*MATERIAL_REF {m}', '}']
    pathlib.Path(out_path).parent.mkdir(parents=True, exist_ok=True)
    pathlib.Path(out_path).write_text('\n'.join(out) + '\n')
    print(f'{out_path}: {total} triangles, {len(names)} shaders')


if __name__ == '__main__':
    main(*sys.argv[1:3])
