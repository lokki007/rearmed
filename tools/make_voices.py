"""Announcer voice lines for the style ranks and records, spoken by Windows' built-in
speech voice and processed into a deeper, punchier announcer (assets/sound/rearmed/*.wav)."""
import math, pathlib, struct, subprocess, sys, tempfile, wave

OUT = pathlib.Path(__file__).resolve().parent.parent / 'assets' / 'sound' / 'rearmed'
RATE = 22050

LINES = {
	'rank_d': 'Decent.',
	'rank_c': 'Cruel.',
	'rank_b': 'Brutal!',
	'rank_a': 'Anarchic!',
	'rank_s': 'Savage!',
	'rank_ss': 'Sadistic!',
	'rank_sss': 'Slaughterhouse!',
	'new_record': 'New record!',
	'perk_offer': 'Choose your perk.',
	'mastery_up': 'Mastery up!',
	# random events (v4): played with the "incoming" warning
	'events/v_low_gravity': 'Low gravity!',
	'events/v_vampire': 'Vampire!',
	'events/v_rocket_frenzy': 'Rocket frenzy!',
	'events/v_speed_rush': 'Speed rush!',
	'events/v_big_heads': 'Big heads!',
	'events/v_knockback_chaos': 'Knockback chaos!',
	'events/v_quad_madness': 'Quad madness!',
	'events/v_jetpack_party': 'Jetpack party!',
	'events/v_blood_moon': 'Blood moon rising!',
	'events/v_meteor_shower': 'Meteor shower! Watch the sky!',
	'events/v_overcharge': 'Overcharge!',
	'events/v_bullet_time': 'Bullet time!',
	'events/v_ice_rink': 'Ice rink!',
	'events/v_barrel_bonanza': 'Barrel bonanza!',
	'events/v_weapon_roulette': 'Weapon roulette!',
	'events/v_thunderstorm': 'Thunderstorm incoming!',
	'events/v_chain_reaction': 'Chain reaction!',
	# ds-intro: the DESCENT versus intro (the bots' own names are the player's Q3 announce/*.wav)
	'dsintro/boss_butcher': 'The Butcher!',
	'dsintro/boss_overseer': 'The Overseer!',
	'dsintro/boss_hollowking': 'The Hollow King!',
	'dsintro/boss_archdemon': 'The Arch-Demon!',
	'dsintro/elite': 'Elite!',
	'dsintro/nemesis': 'Your nemesis!',
	'dsintro/rivals': 'Rivals!',
	'dsintro/rival': 'Rival!',		# ds-arrivals: a rival joining a boss floor on its own
	# ds-surprise: ambushes, wild events, the ghost of a past run
	'surprise/v_ambush': 'Ambush!',
	'surprise/v_gold_meteor': 'Gold meteor incoming!',
	'surprise/v_uprising': 'Bot uprising!',
	'surprise/v_eclipse': 'Blood eclipse!',
	'surprise/v_bounty': 'Bounty!',
	'surprise/v_ghost': 'Your ghost walks.',
	'surprise/v_surpassed': 'You surpassed yourself!',
}


def speak(text, path):
	ps = (
		"Add-Type -AssemblyName System.Speech;"
		"$s = New-Object System.Speech.Synthesis.SpeechSynthesizer;"
		"try { $s.SelectVoice('Microsoft David Desktop') } catch {};"
		"$s.Rate = -1;"
		"$f = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(44100, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, [System.Speech.AudioFormat.AudioChannel]::Mono);"
		f"$s.SetOutputToWaveFile('{path}', $f);"
		f"$s.Speak('{text}');"
		"$s.Dispose()"
	)
	subprocess.run(['powershell', '-NoProfile', '-Command', ps], check=True)


def read(path):
	with wave.open(str(path)) as w:
		n = w.getnframes()
		data = struct.unpack('<%dh' % n, w.readframes(n))
	return [d / 32768.0 for d in data]


def process(samples):
	# pitch down ~15% by resampling 44.1k -> slower playback at 22.05k
	step = 44100 / RATE * 0.85
	out, pos = [], 0.0
	while pos < len(samples) - 1:
		i = int(pos)
		f = pos - i
		out.append(samples[i] * (1 - f) + samples[i + 1] * f)
		pos += step
	# trim silence
	thr = 0.02
	start = next((i for i, s in enumerate(out) if abs(s) > thr), 0)
	end = len(out) - next((i for i, s in enumerate(reversed(out)) if abs(s) > thr), 0)
	out = out[max(0, start - 200):end + 1500]
	# drive + soft clip for punch
	out = [math.tanh(s * 3.0) for s in out]
	# short slap echo and a longer arena echo
	res = out + [0.0] * int(RATE * 0.5)
	for delay, gain in ((int(RATE * 0.045), 0.35), (int(RATE * 0.21), 0.22), (int(RATE * 0.38), 0.12)):
		for i, s in enumerate(out):
			res[i + delay] += s * gain
	return res


def write(name, samples):
	peak = max(1e-9, max(abs(s) for s in samples))
	with wave.open(str(OUT / name), 'wb') as w:
		w.setnchannels(1)
		w.setsampwidth(2)
		w.setframerate(RATE)
		w.writeframes(b''.join(struct.pack('<h', int(s / peak * 30000)) for s in samples))


if __name__ == '__main__':
	OUT.mkdir(parents=True, exist_ok=True)
	with tempfile.TemporaryDirectory() as tmp:
		only = sys.argv[1:]		# optional key prefixes, e.g. "events/"
		for key, text in LINES.items():
			if only and not any(key.startswith(o) for o in only):
				continue
			(OUT / key).parent.mkdir(parents=True, exist_ok=True)
			raw = pathlib.Path(tmp) / (key.replace('/', '_') + '.wav')
			speak(text, str(raw))
			write(key + '.wav', process(read(raw)))
	print('voices:', ', '.join(LINES))
