"""ds-maps test fixtures: run_fixture.txt (a run on floor 3) and meta_fixture.txt (the player's
meta + dsm_plays counted from a READ of home/rearmed/games.log, as the game's first-load seeding does)."""
import collections, pathlib, re

ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = pathlib.Path(__file__).resolve().parent
names = [l.split('"')[1] for l in open(ROOT / 'ioq3/code/game/bg_ds_maps.c') if l.strip().startswith('{ "') and 'DSMAP_SRC' in l]
c = collections.Counter()
for l in open(ROOT / 'home/rearmed/games.log', encoding='latin-1'):
	if 'InitGame' in l and '\\g_rearmMode\\5\\' in l:
		m = re.search(r'\\mapname\\([^\\]*)', l)
		c[m.group(1)] += 1
meta = open(ROOT / 'home/rearmed/rearmed_meta.txt').read()
meta = '\n'.join(x for x in meta.splitlines() if not x.startswith('dsm_')) + '\n'
meta += 'dsm_v 1\ndsm_plays %s\n' % ','.join(str(c[n]) for n in names)
(HERE / 'meta_fixture.txt').write_text(meta, newline='\n')
run = """version 2
active 1
seed SEED
difficulty 3
diff_start 3
diff_min 3
diff_max 3
lives 3
floor FLOOR
skulls 20
cards 2,57
kills 21
time 400000
result win
offer
map PATH
rerolls 0
character 5
act 1
infloor 0
ds_played PLAYED
"""
for name, seed, floor, path, played in (('run_fixture.txt', 4242, 2, '0.1', '1,2'), ('run_fixture2.txt', 777, 4, '0.0.1.0', '0,21,5,3'),
										('run_fixture_abyss.txt', 99, 70, '.'.join(['0'] * 70), ','.join(str(i % 50) for i in range(64)))):
	(HERE / name).write_text(run.replace('SEED', str(seed)).replace('FLOOR', str(floor)).replace('PATH', path).replace('PLAYED', played), newline='\n')
print(dict(c))
