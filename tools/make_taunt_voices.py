"""Round 4 (en-personality): bot voice barks for taunts and telegraph shouts.
Windows' built-in speech voices (David = male bots, Zira = female bots), processed into a short,
gritty shout: slight pitch drop, drive, a tight slap echo (no arena reverb: these come from a body).
Output: assets/sound/rearmed/taunt/m_<name>.wav and f_<name>.wav (22.05 kHz mono).
The names must match prBarkFiles in cgame/cg_personality.c (order of prBark_t in
game/bg_ds_personality.h). Personal-use voices, like tools/make_voices.py.
Usage: python tools/make_taunt_voices.py [name ...]
"""
import math, pathlib, struct, subprocess, sys, tempfile, wave

OUT = pathlib.Path(__file__).resolve().parent.parent / 'assets' / 'sound' / 'rearmed' / 'taunt'
RATE = 22050

BARKS = {
	'gotcha': 'Got you!',
	'easy': 'Too easy.',
	'oops': 'Oops!',
	'stop': 'Stop that one!',
	'payback': 'Payback!',
	'remember': 'Remember me?',
	'mine': "You're mine!",
	'retreat': 'Fall back!',
	'again': 'We meet again.',
	'nemagain': 'Again.',
	'found': 'Found you.',
	'stronger': 'I grow stronger.',
	'incoming': 'Here it comes!',
	'eat': 'Eat this!',
	'glory': 'For glory!',
	'rise': 'Rise!',
	'getover': 'Get over here!',
}

VOICES = {
	'm': ('Microsoft David Desktop', 0.90, 1),		# name, playback rate factor (lower = deeper), SAPI rate
	'f': ('Microsoft Zira Desktop', 0.97, 1),
}


def speak(text, voice, rate, path):
	text = text.replace("'", "''")
	ps = (
		"Add-Type -AssemblyName System.Speech;"
		"$s = New-Object System.Speech.Synthesis.SpeechSynthesizer;"
		f"try {{ $s.SelectVoice('{voice}') }} catch {{}};"
		f"$s.Rate = {rate};"
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


def process(samples, factor):
	step = 44100 / RATE * factor
	out, pos = [], 0.0
	while pos < len(samples) - 1:
		i = int(pos)
		f = pos - i
		out.append(samples[i] * (1 - f) + samples[i + 1] * f)
		pos += step
	thr = 0.02
	start = next((i for i, s in enumerate(out) if abs(s) > thr), 0)
	end = len(out) - next((i for i, s in enumerate(reversed(out)) if abs(s) > thr), 0)
	out = out[max(0, start - 100):end + 600]
	# a gentle high-pass (removes boom) then drive: a shouted, slightly radio-ish voice
	hp, prev_in, prev_out = [], 0.0, 0.0
	a = 0.97
	for s in out:
		y = a * (prev_out + s - prev_in)
		hp.append(y)
		prev_in, prev_out = s, y
	out = [math.tanh(s * 2.6) for s in hp]
	res = out + [0.0] * int(RATE * 0.12)
	d = int(RATE * 0.028)
	for i, s in enumerate(out):
		res[i + d] += s * 0.25
	# short fade out
	n = min(len(res), 400)
	for i in range(n):
		res[-1 - i] *= i / n
	return res


def write(path, samples):
	peak = max(1e-9, max(abs(s) for s in samples))
	with wave.open(str(path), 'wb') as w:
		w.setnchannels(1)
		w.setsampwidth(2)
		w.setframerate(RATE)
		w.writeframes(b''.join(struct.pack('<h', int(s / peak * 29000)) for s in samples))


if __name__ == '__main__':
	OUT.mkdir(parents=True, exist_ok=True)
	only = sys.argv[1:]
	with tempfile.TemporaryDirectory() as tmp:
		for key, text in BARKS.items():
			if only and key not in only:
				continue
			for g, (voice, factor, rate) in VOICES.items():
				raw = pathlib.Path(tmp) / f'{g}_{key}.wav'
				speak(text, voice, rate, str(raw))
				write(OUT / f'{g}_{key}.wav', process(read(raw), factor))
	print('barks:', len(BARKS), 'x', len(VOICES), '->', OUT)
