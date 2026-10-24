"""Extract Team Arena weapon assets (from the user's own install) into assets_ta/.
Parses MD3 surface shaders, copies referenced textures and shader blocks. Personal use only."""
import zipfile, struct, re, pathlib, os
Q3 = pathlib.Path(os.environ.get('Q3_BASE_WIN', r'H:\SteamLibrary\steamapps\common\Quake 3 Arena'))
ta = zipfile.ZipFile(Q3 / 'missionpack' / 'pak0.pk3')
base = [zipfile.ZipFile(p) for p in sorted((Q3 / 'baseq3').glob('pak*.pk3'))]
base_names = {n.lower() for z in base for n in z.namelist()}
ta_names = {n.lower(): n for n in ta.namelist()}
out = pathlib.Path(__file__).resolve().parent.parent / 'assets_ta'

def shader_blocks(z):
    blocks = {}
    for n in z.namelist():
        if not n.endswith('.shader'): continue
        txt = re.sub(r'//[^\n]*', '', z.read(n).decode('latin1'))
        i = 0
        while True:
            m = re.compile(r'\s*([^\s{}]+)\s*\{').match(txt, i)
            if not m: break
            j, depth = m.end() - 1, 0
            while True:
                if txt[j] == '{': depth += 1
                elif txt[j] == '}':
                    depth -= 1
                    if depth == 0: break
                j += 1
            blocks[m.group(1).lower()] = txt[m.start(1):j + 1]
            i = j + 1
    return blocks
ta_shaders = shader_blocks(ta)
base_shaders = set().union(*(shader_blocks(z).keys() for z in base))

def md3_shaders(data):
    ident, ver, name, flags, nf, nt, ns, nskin, of, ot, osurf, oe = struct.unpack('<4si64siiiiiiiii', data[:108])
    res, o = [], osurf
    for _ in range(ns):
        sid, sname, sflags, snf, nsh, nv, ntri, otri, osh, ost, oxyz, oend = struct.unpack('<4s64s10i', data[o:o + 108])
        for k in range(nsh):
            res.append(data[o + osh + k * 68:o + osh + k * 68 + 64].split(b'\0')[0].decode('latin1'))
        o += oend
    return res

files, shaders = set(), {}
def want(path):
    p = path.lower().replace(chr(92), '/')
    if p in ta_names: files.add(ta_names[p]); return True
    return p in base_names

def want_image(name):
    stem = re.sub(r'\.(tga|jpg)$', '', name.lower())
    return want(stem + '.tga') or want(stem + '.jpg')

def want_shader(name):
    key = re.sub(r'\.(tga|jpg)$', '', name.lower())
    if key in ta_shaders and key not in base_shaders:
        shaders[key] = ta_shaders[key]
        for img in re.findall(r'(?:map|clampmap)\s+(\S+)', ta_shaders[key], re.I):
            if not img.startswith('$') and not want_image(img):
                print('missing image', img)
    elif key not in base_shaders and not want_image(key):
        print('missing', name)

prefixes = ('models/weapons/nailgun/', 'models/weapons/vulcan/', 'models/weapons/proxmine/',
            'models/weaphits/nail', 'models/weaphits/proxmine', 'models/powerups/ammo/nailgunam',
            'models/powerups/ammo/chaingunam', 'models/powerups/ammo/proxmineam',
            'sound/weapons/nailgun/', 'sound/weapons/vulcan/', 'sound/weapons/proxmine/',
            'sound/weapons/weapon_hover', 'icons/nailgun128', 'icons/chaingun128', 'icons/proxmine',
            'icons/ammo_nailgun', 'icons/ammo_chaingun', 'icons/ammo_proxmine')
for n in ta.namelist():
    if n.lower().startswith(prefixes) and not n.endswith('/'):
        files.add(n)
        if n.endswith('.md3'):
            for s in md3_shaders(ta.read(n)):
                want_shader(s)
for s in ['icons/iconw_nailgun', 'icons/iconw_chaingun', 'icons/iconw_proxlauncher', 'icons/icona_nailgun',
          'icons/icona_chaingun', 'icons/icona_proxlauncher', 'nailtrail']:
    want_shader(s)

for n in sorted(files):
    dst = out / n
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(ta.read(n))
(out / 'scripts').mkdir(parents=True, exist_ok=True)
(out / 'scripts' / 'rearmed_ta.shader').write_text('// Team Arena weapon shaders (extracted)\n\n' + '\n\n'.join(shaders.values()) + '\n')
print(len(files), 'files,', len(shaders), 'shaders')
