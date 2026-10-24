"""REARMED 0.6.1 trailer: cuts 1080p segments out of the clip AVIs (tools/clip_play.sh 1920 1080 ...),
captions each, adds a title and an end card, and concatenates -> promo/0.6.1/rearmed_0.6.1_trailer.mp4.
Stills (the vault modes) get a slow push-in. Silent: the engine's video capture has no usable audio.
Run: python tools/trailer_061.py"""
import os, shutil, subprocess, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
FF = pathlib.Path(os.environ.get('FFMPEG_DIR', r'C:\Users\Ian\AppData\Local\Microsoft\WinGet\Packages\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe\ffmpeg-8.1.1-full_build\bin')) / 'ffmpeg.exe'
VID = ROOT / 'home_clips' / 'rearmed' / 'videos'
OUT = ROOT / 'promo' / '0.6.1'
WORK = ROOT / 'home_clips' / 'trailer_work'
FPS = 30

# (source, first frame, frames, caption); source = an AVI name, or a still under promo/0.6.1
SHOTS = [
	('alt_e', 80, 60, 'ALT-FIRE ON EVERY GUN'),
	('hero', 48, 50, 'HEADSHOTS'),
	('mv1', 100, 60, 'WALL RUNS, DASHES, A ZIP LINE'),
	('alt_c', 48, 45, 'CHAIN LIGHTNING'),
	('gore', 90, 45, 'GORE AND PHYSICS'),
	('gore', 172, 42, ''),
	('loot_a', 45, 60, 'DESCENT: A ROGUELIKE RUN'),
	('evo_b', 55, 55, 'WEAPON EVOLUTIONS'),
	('tf_champ', 10, 65, 'ELITES AND CHAMPION PACKS'),
	('boss3', 255, 45, 'FOUR ACT BOSSES'),
	('boss1', 160, 45, ''),
	('tb_all', 380, 60, 'TIMED 1V1 BOSS FIGHTS'),
	('still:07_vault_shooting_gallery.jpg', 0, 60, 'SECRET VAULTS'),
	('still:08_vault_sky.jpg', 0, 45, ''),
	('loot_c', 240, 60, 'CHESTS, CARDS, LOOT'),
]


def run(args):
	subprocess.run([str(FF), '-nostdin', '-v', 'error', '-y'] + args, check=True, cwd=WORK)


def caption(text):
	if not text:
		return ''
	text = text.replace(':', r'\:')
	return (f",drawtext=fontfile=impact.ttf:text='{text}':x=72:y=h-250:fontsize=64:fontcolor=white"
			f":borderw=4:bordercolor=black@0.85:shadowx=0:shadowy=0")


def main():
	if WORK.exists():
		shutil.rmtree(WORK)
	WORK.mkdir(parents=True)
	shutil.copy2(r'C:\Windows\Fonts\impact.ttf', WORK / 'impact.ttf')
	shutil.copy2(r'C:\Windows\Fonts\arial.ttf', WORK / 'arial.ttf')
	enc = ['-c:v', 'libx264', '-preset', 'slow', '-crf', '20', '-pix_fmt', 'yuv420p', '-r', str(FPS)]
	parts = []

	# title card
	run(['-f', 'lavfi', '-i', f'color=c=0x0d0b0a:s=1920x1080:d=2.6:r={FPS}', '-vf',
		"drawtext=fontfile=impact.ttf:text='REARMED':x=(w-tw)/2:y=(h-th)/2-50:fontsize=200:fontcolor=0xd9622b,"
		"drawtext=fontfile=arial.ttf:text='EVERY GUN GETS A SECOND TRIGGER':x=(w-tw)/2:y=h/2+90:fontsize=40:fontcolor=white,"
		"fade=t=in:st=0:d=0.4,fade=t=out:st=2.2:d=0.4"] + enc + ['p00.mp4'])
	parts.append('p00.mp4')

	for i, (src, start, n, text) in enumerate(SHOTS, 1):
		dur = n / FPS
		fade = f',fade=t=in:st=0:d=0.12,fade=t=out:st={dur - 0.12:.3f}:d=0.12'
		name = f'p{i:02d}.mp4'
		if src.startswith('still:'):
			img = OUT / src[6:]
			vf = (f"scale=2400:1350,zoompan=z='1+0.0016*on':x='iw/2-(iw/zoom/2)':y='ih/2-(ih/zoom/2)':d={n}:s=1920x1080:fps={FPS}"
				  + caption(text) + fade)
			run(['-loop', '1', '-i', str(img), '-vf', vf, '-frames:v', str(n)] + enc + [name])
		else:
			vf = (f"trim=start_frame={start}:end_frame={start + n},setpts=PTS-STARTPTS,fps={FPS},scale=1920:1080"
				  + caption(text) + fade)
			run(['-i', str(VID / f'{src}.avi'), '-vf', vf, '-an'] + enc + [name])
		parts.append(name)

	# end card
	run(['-f', 'lavfi', '-i', f'color=c=0x0d0b0a:s=1920x1080:d=3.4:r={FPS}', '-vf',
		"drawtext=fontfile=impact.ttf:text='REARMED 0.6.1 BETA':x=(w-tw)/2:y=h/2-130:fontsize=120:fontcolor=0xd9622b,"
		"drawtext=fontfile=arial.ttf:text='A free mod for Quake III Arena':x=(w-tw)/2:y=h/2+20:fontsize=46:fontcolor=white,"
		"drawtext=fontfile=arial.ttf:text='Requires your own copy of Quake III Arena (Steam or GOG)':x=(w-tw)/2:y=h/2+90:fontsize=30:fontcolor=0xb8b0a8,"
		"fade=t=in:st=0:d=0.4"] + enc + ['p99.mp4'])
	parts.append('p99.mp4')

	(WORK / 'list.txt').write_text(''.join(f"file '{p}'\n" for p in parts))
	OUT.mkdir(parents=True, exist_ok=True)
	run(['-f', 'concat', '-safe', '0', '-i', 'list.txt', '-c:v', 'libx264', '-preset', 'slow', '-crf', '21',
		 '-pix_fmt', 'yuv420p', '-movflags', '+faststart', str(OUT / 'rearmed_0.6.1_trailer.mp4')])
	print(OUT / 'rearmed_0.6.1_trailer.mp4')


if __name__ == '__main__':
	main()
