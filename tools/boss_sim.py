"""ds-boss / en-boss: DESCENT boss-floor simulations on dedicated servers (no windows, no human).

A skill-5 bot named "Sim" plays the human's part (g_dsBossSim 1): it gets the run file's
cards, passives and weapon levels, and a modelled human read of the telegraphs
(g_dsSimDodge chance per attack, g_dsSimReact msec). The server quits after the result.

  python tools/boss_sim.py --boss 1 --runs 12 --par 4 [--scale 1.0] [--dodge 0.75] [--lives 10]
       [--maps q3dm6,q3dm7] [--tag before] [--diff 3] [--mode timed|lives] [--time 300]
  python tools/boss_sim.py --model results_<tag>.txt[,more]  [--time 300]   (no servers: the timer model)

Round 4 (en-boss): --mode timed (default) = g_dsBossMode 0, the sim alone vs the boss with a
--time second clock and unlimited deaths. Each run also logs a DSSimT line (deaths, kill time,
fight time, bar left). The TIMER MODEL turns one batch into a calibration curve: a run's
"capacity" = the share of the whole boss pool the sim removes in --time seconds at the rate it
really managed (pool removed / fight time, so the time lost to deaths, 2.3 s respawn + the walk
back each, is in it). P(win) with the pool x k = the share of runs with capacity >= k. Tune
actsCalTimed (g_ds_boss.c) by the k that gives the target win rate.

Each run gets its own home (home_sim/s<slot>, the test pk3s copied in), a fresh run file and a
fresh config; results go to home_sim/results_<tag>.txt (one DSSim line per run) and a summary
is printed. Timescale 2 (the rule for bot matches).
"""
import argparse, os, pathlib, random, re, shutil, subprocess, sys, time

ROOT = pathlib.Path(__file__).resolve().parent.parent
DIST_SRC = ROOT / 'dist_test'
DIST = ROOT / 'dist_sim'      # a copy: build.sh can redeploy dist_test while sims run
PK3_TEST = ROOT / 'home_test' / 'rearmed'
PK3_SRC = ROOT / 'home_sim' / 'pk3'     # snapshot at batch start (a build mid-batch doesn't change the code)
SIM = ROOT / 'home_sim'
Q3BASE = r'H:\SteamLibrary\steamapps\common\Quake 3 Arena'
CREATE_NO_WINDOW = 0x08000000

# a typical kit for the floor before each boss (difficulty 3): cards, passives (13), weapon levels (14)
KITS = {
    1: dict(floor=5, cards='0,3,14', pv='2,1,1,0,0,0,0,0,0,0,0,0,0',
            wl='0,2,3,3,1,3,2,2,2,0,0,0,0,0', wused=(1 << 2) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 8),
            maps=['q3dm6', 'q3dm7', 'q3dm3', 'q3dm5'], skill=3.0),
    2: dict(floor=11, cards='0,3,14,7,4,12', pv='3,2,2,1,1,0,0,0,0,0,0,0,0',
            wl='0,3,4,5,3,5,4,4,4,0,0,2,0,2', wused=0x29fc, maps=['q3dm8', 'q3dm9', 'q3dm10', 'q3dm12'], skill=3.6),
    3: dict(floor=17, cards='0,3,14,7,4,12,1,8,15', pv='4,3,3,2,2,1,0,0,0,0,0,0,0',
            wl='0,4,6,6,4,7,5,6,6,2,0,3,0,3', wused=0x2bfc, maps=['q3dm16', 'q3dm18', 'q3dm19', 'q3dm17'], skill=4.3),
    4: dict(floor=23, cards='0,3,14,7,4,12,1,8,15,4,17', pv='5,4,4,3,3,2,0,0,0,0,0,0,0',
            wl='0,6,8,7,5,8,7,8,7,3,0,4,0,4', wused=0x2bfc, maps=['q3dm13', 'q3dm14', 'q3dm15'], skill=5.0),
}


def run_file(boss, diff, seed, mp):
    k = KITS[boss]
    lines = {
        'version': 2, 'active': 1, 'seed': seed, 'difficulty': diff, 'lives': 2, 'floor': k['floor'],
        'skulls': 0, 'cards': k['cards'], 'kills': 0, 'time': 0, 'result': 'none', 'offer': '',
        'map': '.'.join(['0'] * k['floor']), 'rerolls': 0,
        'up_health': 3, 'up_speed': 1, 'up_damage': 2, 'up_armor': 0, 'up_skulls': 0, 'up_cards': 0,
        'character': 0, 'startcards': 0, 'pv': k['pv'], 'wl': k['wl'], 'wused': k['wused'],
        'act': (k['floor']) // 6 + 1, 'infloor': 1,
        'node_type': 'boss', 'node_map': mp, 'node_bots': 1, 'node_frags': 16, 'node_mutator': 0,
        'node_skill': '%.2f' % k['skill'], 'node_tier': min(2, (k['floor'] + 1) // 8),
        'node_floor': k['floor'] + 1, 'node_layer': k['floor'], 'node_index': 0,
    }
    return ''.join('%s %s\n' % (a, b) for a, b in lines.items())


def prepare_slot(slot):
    home = SIM / ('s%d' % slot)
    mod = home / 'rearmed'
    mod.mkdir(parents=True, exist_ok=True)
    for pk3 in PK3_SRC.glob('*.pk3'):
        dst = mod / pk3.name
        if not dst.exists() or dst.stat().st_mtime < pk3.stat().st_mtime or dst.stat().st_size != pk3.stat().st_size:
            shutil.copy2(pk3, dst)
    for f in ('q3config_server.cfg', 'games.log', 'rearmed_run.txt', 'rearmed_meta.txt', 'ioq3_server.pid'):
        try:
            (mod / f).unlink()
        except FileNotFoundError:
            pass
    return home, mod


def launch(slot, boss, args, seed, mp):
    home, mod = prepare_slot(slot)
    (mod / 'rearmed_run.txt').write_text(run_file(boss, args.diff, seed, mp), newline='\n')
    cmd = [str(DIST / 'ioq3ded.exe'), '+set', 'fs_basepath', Q3BASE, '+set', 'fs_homepath', str(home),
           '+set', 'fs_game', 'rearmed', '+set', 'dedicated', '1', '+set', 'net_port', str(28100 + slot),
           '+set', 'sv_pure', '0', '+set', 'timescale', '2', '+set', 'g_gametype', '0', '+set', 'g_rearmMode', '5',
           '+set', 'g_log', 'games.log', '+set', 'g_logsync', '1', '+set', 'timelimit', '0',
           '+set', 'g_dsBossSim', '1', '+set', 'g_dsBossLives', str(args.lives), '+set', 'g_dsBossScale', str(args.scale),
           '+set', 'g_dsBossMode', '0' if args.mode == 'timed' else '1', '+set', 'g_dsBossTime', str(args.time),
           '+set', 'g_dsPractice', '0', '+set', 'g_dsStagger', '1',
           '+set', 'g_dsSimDodge', str(args.dodge), '+set', 'g_dsSimReact', str(args.react), '+set', 'g_dsNemesis', '1',
           '+set', 'g_mutators', '0', '+set', 'bot_rearm', '1', '+set', 'bot_rearmTricks', '1', '+set', 'g_debugAlt', '0',
           '+set', 'sv_maxclients', '8', '+set', 'bot_minplayers', '0', '+map', mp]
    # the dedicated console needs real console handles (no console = a crash in its input poll):
    # a new console, hidden
    out = open(mod / 'stdout.txt', 'w')
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = 0  # SW_HIDE
    p = subprocess.Popen(cmd, cwd=str(DIST), creationflags=subprocess.CREATE_NEW_CONSOLE, startupinfo=si)
    return dict(p=p, slot=slot, mod=mod, start=time.time(), seed=seed, map=mp, out=out)


def copy_engine():
    PK3_SRC.mkdir(parents=True, exist_ok=True)
    for pk3 in PK3_TEST.glob('*.pk3'):
        shutil.copy2(pk3, PK3_SRC / pk3.name)
    DIST.mkdir(exist_ok=True)
    for f in ('ioq3ded.exe', 'SDL2.dll'):
        src, dst = DIST_SRC / f, DIST / f
        if src.exists() and (not dst.exists() or dst.stat().st_mtime < src.stat().st_mtime):
            try:
                shutil.copy2(src, dst)
            except PermissionError:
                print('note: %s is in use by running sims, keeping the old copy' % dst)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--boss', type=int, default=1)
    ap.add_argument('--runs', type=int, default=8)
    ap.add_argument('--par', type=int, default=4)
    ap.add_argument('--diff', type=int, default=3)
    ap.add_argument('--lives', type=int, default=10)
    ap.add_argument('--scale', type=float, default=1.0)
    ap.add_argument('--dodge', type=float, default=0.75)
    ap.add_argument('--react', type=int, default=350)
    ap.add_argument('--maps', default='')
    ap.add_argument('--tag', default='run')
    ap.add_argument('--seed', type=int, default=1000)
    ap.add_argument('--timeout', type=int, default=1100)
    ap.add_argument('--slotbase', type=int, default=0, help='first home_sim/sN slot (run batches side by side)')
    ap.add_argument('--mode', default='timed', choices=('timed', 'lives'))
    ap.add_argument('--time', type=int, default=300, help='timed: g_dsBossTime seconds')
    ap.add_argument('--model', default='', help='no sims: the timer model over these results files (comma list, in home_sim/)')
    args = ap.parse_args()
    if args.model:
        timer_model([SIM / f if not os.path.isabs(f) else pathlib.Path(f) for f in args.model.split(',')], args.time)
        return
    maps = args.maps.split(',') if args.maps else KITS[args.boss]['maps']
    SIM.mkdir(exist_ok=True)
    copy_engine()
    res_path = SIM / ('results_%s.txt' % args.tag)
    todo = [(args.seed + i * 7919, maps[i % len(maps)]) for i in range(args.runs)]
    running, results = [], []
    free = list(range(args.slotbase, args.slotbase + args.par))
    while todo or running:
        while todo and free:
            seed, mp = todo.pop(0)
            running.append(launch(free.pop(0), args.boss, args, seed, mp))
            time.sleep(1.5)
        time.sleep(2)
        for r in running[:]:
            done = r['p'].poll() is not None
            if not done and time.time() - r['start'] > args.timeout:
                r['p'].kill()
                done = True
            if not done:
                continue
            r['out'].close()
            running.remove(r)
            free.append(r['slot'])
            log = r['mod'] / 'games.log'
            text = log.read_text(errors='replace') if log.exists() else ''
            m = re.search(r'DSSim: result .*', text)
            line = m.group(0) if m else 'DSSim: result NONE (no result; exit %s)' % r['p'].returncode
            mt = re.search(r'DSSimT: (.*)', text)
            if mt:
                line += ' | ' + mt.group(1)
            line += ' map %s seed %d wall %ds' % (r['map'], r['seed'], int(time.time() - r['start']))
            deaths = re.findall(r'DSBoss: player died .*', text)
            results.append(line)
            with open(res_path, 'a') as f:
                f.write(line + '\n')
                for d in deaths:
                    f.write('   ' + d + '\n')
            print(line, flush=True)
            # keep the last log per slot for reading
            shutil.copy2(log, r['mod'] / ('games_%s_%d.log' % (args.tag, r['seed']))) if log.exists() else None
    if args.mode == 'timed':
        timer_model([res_path], args.time, lines=results)
        return
    wins = [l for l in results if 'result WIN' in l]
    used = [int(re.search(r'lives_used (\d+)', l).group(1)) for l in wins]
    bars = [float(re.search(r'bar_left ([\d.]+)', l).group(1)) for l in results if 'bar_left' in l]
    print('SUMMARY tag %s boss %d scale %.2f dodge %.2f: %d runs, %d wins (%.0f%%), lives used per win %s, mean bar left %.2f'
          % (args.tag, args.boss, args.scale, args.dodge, len(results), len(wins), 100.0 * len(wins) / max(1, len(results)),
             ('%.1f' % (sum(used) / len(used))) if used else '-', sum(bars) / len(bars) if bars else -1))


def parse_timed(line):
    """one result line -> dict (timed runs only), None otherwise"""
    m = re.search(r'mode timed deaths (\d+) kill_ms (\d+) limit_ms (\d+) fight_ms (\d+) bar_left ([\d.]+) phase (\d+)', line)
    if not m:
        return None
    d = dict(win='result WIN' in line, deaths=int(m.group(1)), kill=int(m.group(2)), limit=int(m.group(3)),
             fight=int(m.group(4)), bar=float(m.group(5)), phase=int(m.group(6)))
    h = re.search(r' hp (\d+) ', line)
    d['seg'] = int(h.group(1)) if h else 0
    removed = 1.0 - (0.0 if d['win'] else d['bar'])
    span = d['kill'] if d['win'] and d['kill'] > 0 else d['fight']
    d['span'] = span / 1000.0
    d['rate'] = removed / max(1.0, span / 1000.0)            # share of the pool per second
    d['cap'] = d['rate'] * d['limit'] / 1000.0 if d['limit'] else 0   # share of the pool in the whole clock
    return d


def timer_model(paths, limit_s, lines=None):
    """The timer model: P(win) vs the pool, from the runs' real removal rates (deaths included)"""
    if lines is None:
        lines = []
        for p in paths:
            if p.exists():
                lines += [l for l in p.read_text(errors='replace').splitlines() if l.startswith('DSSim: result')]
    runs = [d for d in (parse_timed(l) for l in lines) if d]
    if not runs:
        print('timer model: no timed results')
        return
    n = len(runs)
    wins = [r for r in runs if r['win']]
    losses = [r for r in runs if not r['win']]

    def mean(xs):
        return sum(xs) / len(xs) if xs else float('nan')

    print('TIMER MODEL: %d runs, %d wins (%.0f%%), clock %d s, segment hp mean %.0f'
          % (n, len(wins), 100.0 * len(wins) / n, limit_s, mean([r['seg'] for r in runs])))
    if wins:
        print('  wins: kill time mean %.0f s (min %.0f, max %.0f), deaths mean %.1f, time left mean %.0f s'
              % (mean([r['kill'] / 1000 for r in wins]), min(r['kill'] for r in wins) / 1000, max(r['kill'] for r in wins) / 1000,
                 mean([r['deaths'] for r in wins]), mean([(r['limit'] - r['kill']) / 1000 for r in wins])))
    if losses:
        print('  losses: bar left mean %.2f (min %.2f, max %.2f), deaths mean %.1f, phase reached mean %.1f'
              % (mean([r['bar'] for r in losses]), min(r['bar'] for r in losses), max(r['bar'] for r in losses),
                 mean([r['deaths'] for r in losses]), mean([r['phase'] for r in losses])))
    deaths = sum(r['deaths'] for r in runs)
    fight = sum(r['span'] for r in runs)
    print('  deaths per minute %.2f -> about %.0f s of every %d s spent dead or walking back (2.3 s + ~5 s a death)'
          % (60.0 * deaths / max(1, fight), 7.3 * deaths / max(1, fight) * limit_s, limit_s))
    print('  pool removed per second (whole pool = 1): mean %.4f -> %.0f%% of the pool in %d s'
          % (mean([r['rate'] for r in runs]), 100 * mean([r['cap'] for r in runs]), limit_s))
    caps = sorted(r['cap'] for r in runs)
    print('  P(win) if the pool were x k (actsCalTimed x k):')
    for k in (0.5, 0.7, 0.85, 1.0, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0, 3.5, 4.0, 4.5, 5.0, 6.0):
        print('    k %.2f -> %3.0f%%' % (k, 100.0 * sum(1 for c in caps if c >= k) / n))


if __name__ == '__main__':
    main()
