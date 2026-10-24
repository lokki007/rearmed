"""
en-tune: juggernaut hit box tests on hidden dedicated servers (DESCENT fight floor, every bot a juggernaut).

  python tools/tune_jugg.py move --maps q3dm6,q3dm7 --minutes 3 --par 8 --tag m1
      each map twice: g_dsJuggBox 1 (the big hit box) and 0 (plain); JuggMove lines every 5 s
      -> mean distance moved per 5 s, share of "stuck" samples (< 40 units on the ground), kills,
         share of frames the big box had to fall back to the plain one
  python tools/tune_jugg.py trace --map q3dm17 --spot "x y z"
      pauses the bots, puts client 0 at the spot, logs dsjugtrace with the plain box then the big one

Homes: home_tune/j<slot> (fresh), engine dist_bt/ (bt_sim.py's copy), pk3s from build_pk3/.
"""
import argparse, pathlib, re, shutil, subprocess, time

ROOT = pathlib.Path(__file__).resolve().parent.parent
DIST = ROOT / 'dist_bt'
PK3 = ROOT / 'build_pk3'
HOMES = ROOT / 'home_tune'
Q3BASE = r'H:\SteamLibrary\steamapps\common\Quake 3 Arena'

RUN = """version 2
active 1
seed {seed}
difficulty 3
diff_start 3
diff_min 3
diff_max 3
lives 3
floor 7
skulls 120
cards 0,3,7
kills 30
time 0
result none
offer
map 0.0.0.0.0.0.0.0
rerolls 1
character 0
startcards 0
act 2
infloor 1
node_type fight
node_map {map}
node_bots {bots}
node_frags 200
node_mutator 0
node_skill 3.00
node_tier 1
node_floor 8
node_layer 7
node_index 0
"""


def prepare(slot, mp, sets, extra_cfg, bots, seed):
    home = HOMES / ('j%d' % slot)
    if home.exists():
        shutil.rmtree(home, ignore_errors=True)
    mod = home / 'rearmed'
    mod.mkdir(parents=True, exist_ok=True)
    for p in PK3.glob('*.pk3'):
        shutil.copy2(p, mod / p.name)
    (mod / 'rearmed_run.txt').write_text(RUN.format(map=mp, bots=bots, seed=seed), newline='\n')
    cfg = ['set %s "%s"' % kv for kv in sets.items()]
    (mod / 'jt_setup.cfg').write_text('\n'.join(cfg) + '\n', newline='\n')
    (mod / 'jt_test.cfg').write_text('\n'.join(extra_cfg) + '\n', newline='\n')
    return home, mod


def launch(slot, mp, sets, extra, bots, seed=4242):
    home, mod = prepare(slot, mp, sets, extra, bots, seed)
    cmd = [str(DIST / 'ioq3ded.exe'), '+set', 'fs_basepath', Q3BASE, '+set', 'fs_homepath', str(home),
           '+set', 'fs_game', 'rearmed', '+set', 'dedicated', '1', '+set', 'net_port', str(28400 + slot),
           '+set', 'com_hunkMegs', '256', '+exec', 'jt_setup.cfg', '+devmap', mp, '+exec', 'jt_test.cfg']
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = 0
    p = subprocess.Popen(cmd, cwd=str(DIST), creationflags=subprocess.CREATE_NEW_CONSOLE, startupinfo=si)
    return dict(p=p, mod=mod, map=mp, slot=slot, start=time.time())


def base_sets(box, ts):
    return {
        'g_log': 'games.log', 'g_logsync': 1, 'sv_pure': 0, 'g_gametype': 0, 'g_rearmMode': 5, 'bot_minplayers': 0,
        'sv_maxclients': 12, 'timescale': ts, 'g_dsTypes': 1, 'g_dsTypesForce': 'juggernaut,juggernaut,juggernaut,juggernaut',
        'g_dsTypesDebug': 2, 'g_dsTypesShots': 0, 'g_dsAmbush': 0, 'g_dsWildEvents': 0, 'g_dsGhosts': 0, 'g_dsNemesis': 0,
        'g_dsStagger': 0, 'g_dsAffixes': 0, 'g_dsChampions': 0, 'g_dsModifiers': 0, 'g_dsJuggBox': box, 'g_dsTypeAdapt': 0,
        'timelimit': 0, 'fraglimit': 0, 'g_doWarmup': 0, 'bot_pause': 0,
    }


def summarize_move(text):
    moved = [(float(m.group(1)), m.group(2) == ' (air)') for m in
             re.finditer(r'JuggMove: \d+ at \(.*?\) moved (-?[\d.]+) in 5 s( \(air\))?', text)]
    moved = [(d, air) for d, air in moved if d >= 0]
    big = re.findall(r'frames big (\d+) tall (\d+) plain (\d+)', text)
    kills = len(re.findall(r'Kill: ', text))
    if not moved:
        return None
    n = len(moved)
    mean = sum(d for d, a in moved) / n
    stuck = sum(1 for d, a in moved if d < 40 and not a) / n
    b, t, p = (int(big[-1][0]), int(big[-1][1]), int(big[-1][2])) if big else (0, 0, 0)
    return dict(n=n, mean=mean, stuck=stuck, kills=kills, plain=p / max(b + t + p, 1), tall=t / max(b + t + p, 1))


def cmd_move(args):
    maps = args.maps.split(',')
    todo = [(mp, box) for mp in maps for box in (1, 0)]
    running, free = [], list(range(args.par))
    res = {}
    waits = ['wait 10'] * 20
    while todo or running:
        while todo and free:
            mp, box = todo.pop(0)
            r = launch(free.pop(0), mp, base_sets(box, 2), waits, 4)
            r['box'] = box
            running.append(r)
            time.sleep(2)
        time.sleep(3)
        for r in running[:]:
            if time.time() - r['start'] < args.minutes * 30 + 25 and r['p'].poll() is None:
                continue
            r['p'].kill()
            running.remove(r)
            free.append(r['slot'])
            log = r['mod'] / 'games.log'
            text = log.read_text(errors='replace') if log.exists() else ''
            s = summarize_move(text)
            res[(r['map'], r['box'])] = s
            shutil.copy2(log, HOMES / ('jugg_%s_%s_box%d.log' % (args.tag, r['map'], r['box']))) if log.exists() else None
            print(r['map'], 'box', r['box'], s, flush=True)
    out = HOMES / ('jugg_%s.txt' % args.tag)
    lines = ['map | box | samples | mean moved per 5 s | stuck share | kills | frames tall-only / plain']
    for mp in maps:
        for box in (1, 0):
            s = res.get((mp, box))
            if s:
                lines.append('%s | %s | %d | %.0f | %.0f%% | %d | %s' % (mp, 'big' if box else 'plain', s['n'], s['mean'],
                             100 * s['stuck'], s['kills'], '%.0f%% / %.0f%%' % (100 * s['tall'], 100 * s['plain']) if box else '-'))
            else:
                lines.append('%s | %s | NO DATA' % (mp, 'big' if box else 'plain'))
    out.write_text('\n'.join(lines) + '\n')
    print('\n'.join(lines))


def cmd_trace(args):
    x, y, z = args.spot.split()
    seq = ['wait 10'] * 40 + ['bot_pause 1'] + ['wait 10'] * 4
    seq += ['teleport 0 %s %s %s 180' % (x, y, z)] + ['wait 10'] * 6
    seq += ['set g_dsJuggBox 0'] + ['wait 10'] * 4 + ['dsjugtrace 0'] + ['wait 10'] * 2
    seq += ['set g_dsJuggBox 1'] + ['wait 10'] * 4 + ['dsjugtrace 0'] + ['wait 10'] * 2
    # the corpse: kill it (kchurt), wait for the dead box, trace it plain then big
    seq += ['kchurt 0 1 999'] + ['wait 10'] * 6
    seq += ['set g_dsJuggBox 0'] + ['wait 10'] * 4 + ['dsjugtrace 0'] + ['wait 10'] * 2
    seq += ['set g_dsJuggBox 1'] + ['wait 10'] * 4 + ['dsjugtrace 0'] + ['wait 10'] * 2
    seq += ['quit']
    sets = base_sets(1, 1)
    r = launch(9, args.map, sets, seq, 4)
    t0 = time.time()
    while r['p'].poll() is None and time.time() - t0 < 120:
        time.sleep(2)
    r['p'].kill()
    log = r['mod'] / 'games.log'
    text = log.read_text(errors='replace') if log.exists() else ''
    out = [l for l in text.splitlines() if 'JuggTrace' in l or 'teleport' in l.lower()]
    (HOMES / ('jugg_trace_%s.txt' % args.tag)).write_text('\n'.join(out) + '\n')
    print('\n'.join(out))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('mode', choices=['move', 'trace'])
    ap.add_argument('--maps', default='q3dm6,q3dm7,q3dm8,q3dm12,q3dm13,q3dm17,q3tourney2,pro-q3dm6')
    ap.add_argument('--map', default='q3dm17')
    ap.add_argument('--spot', default='-288 -376 24')
    ap.add_argument('--minutes', type=float, default=3)
    ap.add_argument('--par', type=int, default=8)
    ap.add_argument('--tag', default='t')
    args = ap.parse_args()
    HOMES.mkdir(exist_ok=True)
    (cmd_move if args.mode == 'move' else cmd_trace)(args)


if __name__ == '__main__':
    main()
