"""DESCENT (ds-maps): copy the Team Arena maps that work as free-for-all arenas out of the
player's OWN missionpack/pak0.pk3 into assets_ta/ (-> zz-rearmed-ta.pk3; personal use only, the
release rebuilds it from each player's install with setup.ps1 / ta_manifest.txt).

For each map: the .bsp, the bot .aas, the levelshot, and everything the map needs that baseq3
doesn't have: the Team Arena shader blocks (into scripts/rearmed_tamaps.shader), their images
(textures, sky boxes, animMaps), entity models (+ their shaders), entity sounds and music.
A shader that baseq3 also defines keeps the baseq3 version (no overrides of id files).

  python tools/extract_ta_maps.py            # extract
  python tools/extract_ta_maps.py --dry      # list only
"""
import os, pathlib, re, struct, sys, zipfile

Q3 = pathlib.Path(os.environ.get('Q3_BASE_WIN', r'H:\SteamLibrary\steamapps\common\Quake 3 Arena'))
OUT = pathlib.Path(__file__).resolve().parent.parent / 'assets_ta'
MAPS = ['mptourney1', 'mptourney2', 'mptourney3', 'mptourney4',
        'mpteam1', 'mpteam2', 'mpteam4', 'mpteam6', 'mpteam7']
# not used: mpteam3 + mpteam5 (too big for a frag race of <= 6: half the kill pace of q3ctf), mpteam8 (botlib
# AAS_LinkEntity stack overflows), mpterra1-3 (8-12k unit terrain), mpq3ctf1-4 / mpq3tourney6 (the q3ctf / tourney6 layouts again)
DRY = '--dry' in sys.argv

ta = zipfile.ZipFile(Q3 / 'missionpack' / 'pak0.pk3')
base = [zipfile.ZipFile(p) for p in sorted((Q3 / 'baseq3').glob('pak*.pk3'))]
base_names = {n.lower() for z in base for n in z.namelist()}
ta_names = {n.lower(): n for n in ta.namelist() if not n.endswith('/')}


def shader_blocks(z):
	blocks = {}
	for n in z.namelist():
		if not n.lower().endswith('.shader'):
			continue
		txt = re.sub(r'//[^\n]*', '', z.read(n).decode('latin1'))
		i = 0
		pat = re.compile(r'\s*([^\s{}]+)\s*\{')
		while True:
			m = pat.match(txt, i)
			if not m:
				break
			j, depth = m.end() - 1, 0
			while j < len(txt):
				if txt[j] == '{':
					depth += 1
				elif txt[j] == '}':
					depth -= 1
					if depth == 0:
						break
				j += 1
			blocks.setdefault(m.group(1).lower(), txt[m.start(1):j + 1])
			i = j + 1
	return blocks


ta_shaders = shader_blocks(ta)
base_blocks = {}
for z in base:
	for k, v in shader_blocks(z).items():
		base_blocks.setdefault(k, v)
base_shaders = set(base_blocks.keys())

files, shaders, missing = set(), {}, []


def want_file(path):
	"""a file the map needs: from TA when baseq3 lacks it. True when it exists somewhere."""
	p = path.lower().replace('\\', '/')
	if p in base_names:
		return True
	if p in ta_names:
		files.add(ta_names[p])
		return True
	return False


def want_image(name):
	stem = re.sub(r'\.(tga|jpg)$', '', name.lower().replace('\\', '/'))
	return want_file(stem + '.tga') or want_file(stem + '.jpg')


def want_shader(name):
	key = re.sub(r'\.(tga|jpg)$', '', name.lower().replace('\\', '/'))
	if not key or key.startswith('$') or key == 'noshader':
		return
	if key in base_shaders:
		# baseq3's own shader, but some of them name Team Arena images (textures/sfx/swirl_*)
		if '__base__' + key not in shaders:
			shaders.setdefault('__base__' + key, None)
			for img in re.findall(r'(?:^|\s)(?:map|clampmap)\s+(\S+)', base_blocks[key], re.I):
				if not img.startswith('$'):
					want_image(img)
		return
	if key in ta_shaders:
		if key in shaders:
			return
		block = ta_shaders[key]
		shaders[key] = block
		for img in re.findall(r'(?:^|\s)(?:map|clampmap)\s+(\S+)', block, re.I):
			if not img.startswith('$') and not want_image(img):
				missing.append('%s: image %s' % (key, img))
		for frames in re.findall(r'animmap\s+\S+\s+([^\n]+)', block, re.I):
			for img in frames.split():
				if not want_image(img):
					missing.append('%s: anim %s' % (key, img))
		for env in re.findall(r'skyparms\s+(\S+)', block, re.I):
			if env != '-' and not env.isdigit():
				for side in ('rt', 'lf', 'bk', 'ft', 'up', 'dn'):
					if not want_image('%s_%s' % (env, side)):
						missing.append('%s: sky %s_%s' % (key, env, side))
		return
	if not want_image(key):
		missing.append('shader/image %s' % key)


def md3_shaders(data):
	ident, ver, name, flags, nf, nt, ns, nskin, of, ot, osurf, oe = struct.unpack('<4si64siiiiiiiii', data[:108])
	res, o = [], osurf
	for _ in range(ns):
		sid, sname, sflags, snf, nsh, nv, ntri, otri, osh, ost, oxyz, oend = struct.unpack('<4s64s10i', data[o:o + 108])
		for k in range(nsh):
			res.append(data[o + osh + k * 68:o + osh + k * 68 + 64].split(b'\0')[0].decode('latin1'))
		o += oend
	return res


def bsp_lump(bsp, n):
	off, ln = struct.unpack_from('<ii', bsp, 8 + n * 8)
	return bsp[off:off + ln]


for m in MAPS:
	for f in ('maps/%s.bsp' % m, 'maps/%s.aas' % m):
		if not want_file(f):
			missing.append(f)
	want_image('levelshots/%s' % m)
	bsp = ta.read(ta_names['maps/%s.bsp' % m])
	sh = bsp_lump(bsp, 1)
	for k in range(len(sh) // 72):
		want_shader(sh[k * 72:k * 72 + 64].split(b'\0')[0].decode('latin1'))
	fog = bsp_lump(bsp, 12)
	for k in range(len(fog) // 72):
		want_shader(fog[k * 72:k * 72 + 64].split(b'\0')[0].decode('latin1'))
	ents = bsp_lump(bsp, 0).decode('latin1', 'replace')
	pairs = []
	for block in re.findall(r'\{([^{}]*)\}', ents):
		kv = re.findall(r'"([^"]+)"\s+"([^"]*)"', block)
		cls = dict((k.lower(), v) for k, v in kv).get('classname', '').lower()
		for k, v in kv:
			if k.lower() == 'model' and cls == 'misc_model':
				continue		# baked into the bsp by q3map
			pairs.append((k, v))
	for key, val in pairs:
		key = key.lower()
		if key in ('model', 'model2') and val.lower().endswith('.md3'):
			if want_file(val):
				p = val.lower().replace('\\', '/')
				if p in ta_names and p not in base_names:
					for s in md3_shaders(ta.read(ta_names[p])):
						want_shader(s)
			else:
				missing.append('%s model %s' % (m, val))
		elif key in ('noise', 'music') and '.' in val and not val.startswith('*'):
			if not want_file(val):
				missing.append('%s sound %s' % (m, val))

size = sum(ta.getinfo(f).file_size for f in files)
print('%d files (%.1f MB), %d shader blocks, %d missing' % (len(files), size / 1048576.0, len(shaders), len(missing)))
for x in sorted(set(missing))[:60]:
	print('  missing', x)
if DRY:
	sys.exit(0)
for n in sorted(files):
	dst = OUT / n
	dst.parent.mkdir(parents=True, exist_ok=True)
	dst.write_bytes(ta.read(n))
(OUT / 'scripts').mkdir(parents=True, exist_ok=True)
(OUT / 'scripts' / 'rearmed_tamaps.shader').write_text(
	'// Team Arena map shaders (extracted by tools/extract_ta_maps.py from your own install)\n\n' + '\n\n'.join(shaders[k] for k in sorted(shaders) if shaders[k]) + '\n',
	encoding='latin1')
print('written to', OUT)
