"""Remix an id map: decompile, restore baked models, apply edits, compile.

usage: remix.py <remix name> [fast]
Remixes are defined in maps/remixes.py as REMIXES[name] = dict(source=..., title=..., edit=fn)."""
import os, re, subprocess, sys, zipfile, pathlib, importlib.util

Q3MOD = pathlib.Path(__file__).resolve().parent.parent
Q3_BASE = pathlib.Path(r'H:\SteamLibrary\steamapps\common\Quake 3 Arena')
NR = pathlib.Path(r'G:\_Games\_toolchain\netradiant')
GIT_BASH = str(pathlib.Path("C:/Program Files/Git/bin/bash.exe"))
SRC = Q3MOD / 'maps' / 'src'
BAKED = Q3MOD / 'home' / 'rearmed' / 'models' / 'rearmed'   # q3map2 finds it through -fs_homepath
sys.path.insert(0, str(Q3MOD / 'tools'))
from extract_baked import main as extract_baked


class MapFile:
    """Minimal .map entity editor (keeps brush text verbatim)."""
    ENT = re.compile(r'// entity \d+\n\{\n(.*?)\n\}\n', re.S)

    def __init__(self, text):
        self.header = text.split('// entity 0', 1)[0]
        self.ents = []
        for m in self.ENT.finditer(text):
            body = m.group(1)
            keys = dict(re.findall(r'^\s*"([^"]+)" "([^"]*)"\s*$', body, re.M))
            brushes = body[body.find('\n\t//') + 1:] if '\n\t//' in body else ''
            self.ents.append({'keys': keys, 'brushes': brushes})

    def find(self, **match):
        return [e for e in self.ents if all(e['keys'].get(k) == v for k, v in match.items())]

    def add(self, keys, brushes=''):
        self.ents.append({'keys': dict(keys), 'brushes': brushes})

    def remove(self, ent):
        self.ents.remove(ent)

    def text(self):
        out = [self.header.rstrip() + '\n']
        for i, e in enumerate(self.ents):
            out.append(f'// entity {i}\n{{\n')
            out += [f'"{k}" "{v}"\n' for k, v in e['keys'].items()]
            if e['brushes']:
                out.append('\n' + e['brushes'].rstrip('\n') + '\n')
            out.append('}\n')
        return ''.join(out)


def box_brush(mins, maxs, shader, detail=False):
    """Axis aligned brush in q3map2 'map' format."""
    (x0, y0, z0), (x1, y1, z1) = mins, maxs
    flags = '134217728 0 0' if detail else '0 0 0'
    planes = [
        ((x0, y1, z1), (x0, y0, z1), (x0, y0, z0)),   # -x
        ((x1, y0, z1), (x1, y1, z1), (x1, y1, z0)),   # +x
        ((x0, y0, z1), (x1, y0, z1), (x1, y0, z0)),   # -y
        ((x1, y1, z1), (x0, y1, z1), (x0, y1, z0)),   # +y
        ((x0, y0, z0), (x1, y0, z0), (x1, y1, z0)),   # -z
        ((x1, y0, z1), (x0, y0, z1), (x0, y1, z1)),   # +z
    ]
    lines = ['\t{']
    for p in planes:
        pts = ' '.join('( %d %d %d )' % q for q in p)
        lines.append(f'\t\t{pts} {shader} 0 0 0 0.5 0.5 {flags}')
    lines.append('\t}')
    return '\n'.join(lines) + '\n'


def prepare(name, source, edit):
    SRC.mkdir(parents=True, exist_ok=True)
    bsp = SRC / f'{source}.bsp'
    with zipfile.ZipFile(Q3_BASE / 'baseq3' / 'pak0.pk3') as z:
        bsp.write_bytes(z.read(f'maps/{source}.bsp'))
    subprocess.run([str(NR / 'q3map2.exe'), '-game', 'quake3', '-fs_basepath', str(Q3_BASE), '-convert', '-format', 'map', str(bsp)],
                   check=True, capture_output=True)
    m = MapFile((SRC / f'{source}_converted.map').read_text(encoding='latin1'))

    # the source models of misc_models never shipped: re-add their baked triangles as one model
    for e in m.find(classname='misc_model'):
        m.remove(e)
    ase = BAKED / f'{name}_baked.ase'
    extract_baked(str(bsp), str(ase))
    m.add({'classname': 'misc_model', 'origin': '0 0 0', 'model': f'models/rearmed/{name}_baked.ase'})

    edit(m)
    out = SRC / f'{name}.map'
    out.write_text(m.text(), encoding='latin1')
    return out


if __name__ == '__main__':
    spec = importlib.util.spec_from_file_location('remixes', Q3MOD / 'maps' / 'remixes.py')
    remixes = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(remixes)
    name = sys.argv[1]
    r = remixes.REMIXES[name]
    path = prepare(name, r['source'], r['edit'])
    mode = sys.argv[2] if len(sys.argv) > 2 else 'final'
    subprocess.run([GIT_BASH, str(Q3MOD / 'tools' / 'buildmap.sh'), path.as_posix(), name, mode], check=True)
