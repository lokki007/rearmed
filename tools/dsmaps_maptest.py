"""ds-maps: every DESCENT map loads as a free-for-all floor and the bots play on it.

Hidden dedicated servers (no windows, no human), timescale 2, each map as a DESCENT fight floor
(a fixture run file, node_variant SCRAMBLED so the item shuffle and the bots' item re-typing run
too). After --secs wall seconds the server is stopped and its games.log checked:
  - the map loaded, the bot library has the AAS (no "AAS not loaded" / "no spawn point")
  - DSMaps line: deathmatch spawns (+ the ones made from team spawns), items moved
  - kills by at least 2 different bots (the bots move, find each other, fight)
Results: home_dsmtest/results.txt (+ one games_<map>.log each).

  python tools/dsmaps_maptest.py [--maps a,b] [--par 4] [--secs 75] [--bots 4]
"""
import argparse, pathlib, re, shutil, subprocess, time

ROOT = pathlib.Path(__file__).resolve().parent.parent
DIST_SRC = ROOT / 'dist_test'
DIST = ROOT / 'dist_dsmtest'
PK3_TEST = ROOT / 'home_test' / 'rearmed'
OUT = ROOT / 'home_dsmtest'
Q3BASE = r'H:\SteamLibrary\steamapps\common\Quake 3 Arena'

NEW_MAPS = ['q3dm0', 'q3tourney1', 'q3tourney2', 'q3tourney3', 'q3tourney4', 'q3tourney5', 'q3tourney6',
            'q3ctf1', 'q3ctf2', 'q3ctf3', 'q3ctf4', 'q3tourney6_ctf', 'pro-q3tourney2', 'pro-q3tourney4',
            'mptourney1', 'mptourney2', 'mptourney3', 'mptourney4',
            'mpteam1', 'mpteam2', 'mpteam3', 'mpteam4', 'mpteam5', 'mpteam6', 'mpteam7', 'mpteam8']


def run_file(mp, bots, seed):
    lines = {
        'version': 2, 'active': 1, 'seed': seed, 'difficulty': 3, 'lives': 3, 'floor': 8, 'skulls': 0, 'cards': '',
        'kills': 0, 'time': 0, 'result': 'none', 'offer': '', 'map': '0.0.0.0.0.0.0.0', 'rerolls': 0,
        'character': 0, 'act': 2, 'infloor': 1, 'node_type': 'fight', 'node_map': mp, 'node_bots': bots,
        'node_frags': 99, 'node_mutator': 0, 'node_skill': '3.00', 'node_tier': 1, 'node_floor': 9,
        'node_layer': 8, 'node_index': 0, 'node_variant': 1 | (3 << 4),
    }
    return ''.join('%s %s\n' % (a, b) for a, b in lines.items())


def launch(slot, mp, args):
    home = OUT / ('s%d' % slot)
    mod = home / 'rearmed'
    mod.mkdir(parents=True, exist_ok=True)
    for pk3 in PK3_TEST.glob('*.pk3'):
        dst = mod / pk3.name
        if not dst.exists() or dst.stat().st_mtime < pk3.stat().st_mtime or dst.stat().st_size != pk3.stat().st_size:
            shutil.copy2(pk3, dst)
    for f in ('q3config_server.cfg', 'games.log', 'rearmed_meta.txt', 'ioq3_server.pid', 'qconsole.log'):
        try:
            (mod / f).unlink()
        except FileNotFoundError:
            pass
    (mod / 'rearmed_run.txt').write_text(run_file(mp, args.bots, 1000 + slot), newline='\n')
    cmd = [str(DIST / 'ioq3ded.exe'), '+set', 'fs_basepath', Q3BASE, '+set', 'fs_homepath', str(home),
           '+set', 'fs_game', 'rearmed', '+set', 'dedicated', '1', '+set', 'net_port', str(28300 + slot),
           '+set', 'sv_pure', '0', '+set', 'timescale', '2', '+set', 'g_gametype', '0', '+set', 'g_rearmMode', '5',
           '+set', 'g_log', 'games.log', '+set', 'g_logsync', '1', '+set', 'logfile', '2', '+set', 'timelimit', '0',
           '+set', 'fraglimit', '0', '+set', 'g_dsVariants', '1', '+set', 'g_mutators', '0', '+set', 'bot_rearm', '1',
           '+set', 'sv_maxclients', '12', '+set', 'bot_minplayers', '0', '+set', 'g_dsNemesis', '0',
           '+set', 'developer', '0', '+map', mp]
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = 0
    p = subprocess.Popen(cmd, cwd=str(DIST), creationflags=subprocess.CREATE_NEW_CONSOLE, startupinfo=si)
    return dict(p=p, slot=slot, mod=mod, map=mp, start=time.time())


def check(r):
    log = r['mod'] / 'games.log'
    con = r['mod'] / 'qconsole.log'
    text = log.read_text(errors='replace') if log.exists() else ''
    ctext = con.read_text(errors='replace') if con.exists() else ''
    shutil.copy2(log, OUT / ('games_%s.log' % r['map'])) if log.exists() else None
    shutil.copy2(con, OUT / ('console_%s.log' % r['map'])) if con.exists() else None
    kills = re.findall(r'Kill: (\d+) (\d+) \d+: (.*?) killed (.*?) by (\S+)', text)
    killers = set(k[2] for k in kills if k[0] != '1022' and k[0] != k[1])
    dsm = re.findall(r'DSMaps: .*', text)
    problems = []
    for pat in ('AAS not loaded', 'no spawn point', "Couldn't find a spawn point", 'ERROR', 'BotAISetupClient failed',
                'aas file .* not found', 'Couldn\'t load', 'could not load'):
        for m in re.findall(r'[^\n]*%s[^\n]*' % pat, ctext + text, re.I):
            problems.append(m.strip()[:120])
    ok = len(killers) >= 2 and not problems and 'InitGame' in text
    return ok, len(kills), len(killers), dsm[0][8:] if dsm else '(no DSMaps line)', problems[:3]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--maps', default='')
    ap.add_argument('--par', type=int, default=4)
    ap.add_argument('--secs', type=int, default=75)
    ap.add_argument('--bots', type=int, default=4)
    args = ap.parse_args()
    maps = args.maps.split(',') if args.maps else NEW_MAPS
    OUT.mkdir(exist_ok=True)
    DIST.mkdir(exist_ok=True)
    for f in ('ioq3ded.exe', 'SDL2.dll'):
        shutil.copy2(DIST_SRC / f, DIST / f)
    todo = list(maps)
    running, results = [], []
    free = list(range(args.par))
    while todo or running:
        while todo and free:
            running.append(launch(free.pop(0), todo.pop(0), args))
        time.sleep(2)
        for r in list(running):
            if r['p'].poll() is not None or time.time() - r['start'] > args.secs:
                if r['p'].poll() is None:
                    r['p'].kill()
                    r['p'].wait()
                time.sleep(0.5)
                ok, nk, nkillers, dsm, problems = check(r)
                line = '%-16s %s  kills %3d by %d bots  %s%s' % (r['map'], 'OK  ' if ok else 'FAIL', nk, nkillers, dsm,
                                                               ('  PROBLEMS: ' + ' | '.join(problems)) if problems else '')
                print(line, flush=True)
                results.append(line)
                running.remove(r)
                free.append(r['slot'])
    (OUT / 'results.txt').write_text('\n'.join(sorted(results)) + '\n')


if __name__ == '__main__':
    main()
