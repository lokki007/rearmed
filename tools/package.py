"""Build a shareable REARMED release into release/:
  REARMED-<ver>-win64.zip   engine + the mod pk3 + launcher/setup, no id Software data
  REARMED-<ver>-src.zip     the GPL source (ioq3 tree, build tools, release files)
The Team Arena weapon files are NOT included: setup.ps1 copies them from each player's
own Team Arena install, using ta_manifest.txt (a list of names, generated here).
Fails if any file in the release pk3 has the same path as a file in the id paks."""
import os, re, shutil, subprocess, sys, zipfile, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
IOQ3 = ROOT / 'ioq3'
Q3 = pathlib.Path(os.environ.get('Q3_BASE_WIN', r'H:\SteamLibrary\steamapps\common\Quake 3 Arena'))
VERSION = re.search(r'#define\s+REARMED_VERSION\s+"([^"]+)"', (IOQ3 / 'code/game/bg_public.h').read_text()).group(1)
NAME = f'REARMED-{VERSION}'
OUT = ROOT / 'release'
STAGE = OUT / NAME
FIXED_TIME = (2000, 1, 1, 0, 0, 0)
GPL_HEADER = (IOQ3 / 'code/game/inv.h').read_text().split('*/', 1)[0] + '*/\n'
ALLOWED_OVERRIDES = {'botfiles/inv.h', 'vm/cgame.qvm', 'vm/qagame.qvm', 'vm/ui.qvm'}	# our GPL builds replace the game code		# GPL source file (code/game/inv.h), renumbered for the TA items


def crlf(text):
	return text.replace('\r\n', '\n').replace('\n', '\r\n')


def write_pk3(path, entries):
	"""entries: {arcname: bytes}. Sorted, fixed timestamps: the same input gives the same pk3."""
	with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
		for arc in sorted(entries):
			info = zipfile.ZipInfo(arc, FIXED_TIME)
			info.compress_type = zipfile.ZIP_DEFLATED
			z.writestr(info, entries[arc])


def id_names():
	names = set()
	for pak in list((Q3 / 'baseq3').glob('pak*.pk3')) + list((Q3 / 'missionpack').glob('pak*.pk3')):
		with zipfile.ZipFile(pak) as z:
			names.update(n.lower() for n in z.namelist())
	return names


def main():
	if STAGE.exists():
		shutil.rmtree(STAGE)
	(STAGE / 'engine').mkdir(parents=True)
	(STAGE / 'home' / 'rearmed').mkdir(parents=True)

	# engine
	rel = IOQ3 / 'build' / 'Release'
	for f in ('ioquake3.exe', 'ioq3ded.exe', 'renderer_opengl1.dll', 'renderer_opengl2.dll', 'SDL2.dll'):
		shutil.copy2(rel / f, STAGE / 'engine' / f)

	# the mod pk3: game code, our own generated assets, configs
	entries = {}
	for q in sorted((rel / 'baseq3' / 'vm').glob('*.qvm')):
		entries[f'vm/{q.name}'] = q.read_bytes()
	for f in sorted((ROOT / 'assets').rglob('*')):
		if f.is_file():
			arc = f.relative_to(ROOT / 'assets').as_posix()
			# hit-feel arm-less skins are made from the player's own installed skins: never ship them
			# (a release install shows the stump without hiding the arm)
			if arc.startswith('models/players/') and f.suffix == '.skin' and '_hf' in f.stem:
				continue
			entries[arc] = f.read_bytes()
	entries['rearmed_gfx.cfg'] = (ROOT / 'home' / 'rearmed' / 'rearmed_gfx.cfg').read_bytes()
	entries['rearmed_firstrun.cfg'] = (ROOT / 'release_files' / 'rearmed_firstrun.cfg').read_bytes()
	entries['botfiles/inv.h'] = (GPL_HEADER + (ROOT / 'assets_ta' / 'botfiles' / 'inv.h').read_text()).encode()

	clash = sorted(n for n in entries if n.lower() in id_names() and n.lower() not in ALLOWED_OVERRIDES)
	if clash:
		sys.exit('release pk3 would contain id Software paths:\n  ' + '\n  '.join(clash))
	write_pk3(STAGE / 'home' / 'rearmed' / f'zz-rearmed-{VERSION}.pk3', entries)

	# Team Arena manifest: names only, the files come from the player's own install
	ta = ROOT / 'assets_ta'
	lines = ['# Team Arena files REARMED copies from your own missionpack/pak0.pk3']
	for f in sorted(ta.rglob('*')):
		arc = f.relative_to(ta).as_posix()
		if f.is_file() and arc not in ('botfiles/inv.h', 'scripts/rearmed_ta.shader', 'scripts/rearmed_tamaps.shader'):
			lines.append('file ' + arc)
	shader_text = (ta / 'scripts' / 'rearmed_ta.shader').read_text(encoding='latin1')
	if (ta / 'scripts' / 'rearmed_tamaps.shader').is_file():	# ds-maps: the Team Arena maps' shaders (tools/extract_ta_maps.py)
		shader_text += '\n' + (ta / 'scripts' / 'rearmed_tamaps.shader').read_text(encoding='latin1')
	for name in re.findall(r'^(\S+)\s*\n\{', shader_text, re.M):
		lines.append('shader ' + name)
	(STAGE / 'ta_manifest.txt').write_text(crlf('\n'.join(lines) + '\n'))

	# launcher, setup, docs
	for f in ('Play REARMED.bat', 'Start Server.bat', 'Host 1v1.bat', 'Join 1v1.bat', 'README.txt', 'TESTERS.txt'):
		(STAGE / f).write_text(crlf((ROOT / 'release_files' / f).read_text()), newline='')
	shutil.copy2(ROOT / 'release_files' / 'setup.ps1', STAGE / 'setup.ps1')
	for f in ('AGENTS.md', 'CLAUDE.md'):		# for AI agents on a player's PC (CLAUDE.md imports AGENTS.md)
		shutil.copy2(ROOT / 'release_files' / f, STAGE / f)
	(STAGE / 'home' / 'rearmed' / 'server.cfg').write_text(crlf((ROOT / 'release_files' / 'server.cfg').read_text()), newline='')
	(STAGE / 'home' / 'rearmed' / 'server_duel.cfg').write_text(crlf((ROOT / 'release_files' / 'server_duel.cfg').read_text()), newline='')
	(STAGE / 'CHANGELOG.txt').write_text(crlf((ROOT / 'CHANGELOG.md').read_text()), newline='')
	shutil.copy2(IOQ3 / 'COPYING.txt', STAGE / 'COPYING.txt')

	# zips
	binzip = OUT / f'{NAME}-win64.zip'
	with zipfile.ZipFile(binzip, 'w', zipfile.ZIP_DEFLATED) as z:
		for f in sorted(STAGE.rglob('*')):
			if f.is_file():
				z.write(f, (pathlib.Path(NAME) / f.relative_to(STAGE)).as_posix())

	src = subprocess.run(['git', '-C', str(IOQ3), 'ls-files', '-co', '--exclude-standard'],
		capture_output=True, text=True, check=True).stdout.split('\n')
	srczip = OUT / f'{NAME}-src.zip'
	with zipfile.ZipFile(srczip, 'w', zipfile.ZIP_DEFLATED) as z:
		for f in src:
			if f and (IOQ3 / f).is_file():
				z.write(IOQ3 / f, f'{NAME}-src/ioq3/{f}')
		for f in sorted((ROOT / 'tools').rglob('*')):
			if f.is_file() and '__pycache__' not in f.parts:
				z.write(f, f'{NAME}-src/{f.relative_to(ROOT).as_posix()}')
		for f in sorted((ROOT / 'release_files').iterdir()):
			z.write(f, f'{NAME}-src/release_files/{f.name}')
		for f in ('README.md', 'CHANGELOG.md'):
			z.write(ROOT / f, f'{NAME}-src/{f}')

	pk3 = STAGE / 'home' / 'rearmed' / f'zz-rearmed-{VERSION}.pk3'
	print(f'{binzip.name}: {binzip.stat().st_size // 1024} KB  ({len(entries)} files in the pk3, {len(lines) - 1} TA manifest lines)')
	print(f'{srczip.name}: {srczip.stat().st_size // 1024} KB')
	print(f'staged in {STAGE}')


if __name__ == '__main__':
	main()
