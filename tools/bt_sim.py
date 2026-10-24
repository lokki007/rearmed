"""
en-brains (round 4) bot-tactics test harness: hidden bot-only FFA matches on a dedicated server.

  python tools/bt_sim.py --runs 4 --par 2 --split 1 --tag split1
  python tools/bt_sim.py --runs 2 --tactics 0 --tag base

Each match: 6 bots (skill --skill) on --maps, timescale 2, timelimit --minutes, bt_debug 1.
--split 1: only even client numbers use the tactics (odd = stock), 2: only odd. 0: all.
--tactics 0 turns g_botTactics off (baseline). --thief: client 0 plays "the human" for the
weapon thief + habits (bt_thiefTarget / bt_habitsTarget 0).
Homes: home_bt/s<slot> (fresh config each run), engine copy dist_bt/. Results: home_bt/results_<tag>.txt
"""
import argparse, pathlib, re, shutil, subprocess, time, collections

ROOT = pathlib.Path(__file__).resolve().parent.parent
DIST_SRC = ROOT / 'dist_test'
DIST = ROOT / 'dist_bt'
PK3_TEST = ROOT / 'home_test' / 'rearmed'
SIMROOT = ROOT / 'home_bt'
PK3_SRC = SIMROOT / 'pk3'
import os
if os.environ.get('BT_PK3'):
    PK3_SRC = pathlib.Path(os.environ['BT_PK3'])		# en-tune: a fixed pk3 snapshot (no refresh from home_test)
Q3BASE = r'H:\SteamLibrary\steamapps\common\Quake 3 Arena'
BOTS = ['sarge', 'grunt', 'major', 'visor', 'bitterman', 'anarki', 'slash', 'ranger']


def copy_engine():
    PK3_SRC.mkdir(parents=True, exist_ok=True)
    if not os.environ.get('BT_PK3'):
        for pk3 in PK3_TEST.glob('*.pk3'):
            shutil.copy2(pk3, PK3_SRC / pk3.name)
    DIST.mkdir(exist_ok=True)
    for f in ('ioq3ded.exe', 'SDL2.dll'):
        src, dst = DIST_SRC / f, DIST / f
        if src.exists() and (not dst.exists() or dst.stat().st_mtime < src.stat().st_mtime):
            try:
                shutil.copy2(src, dst)
            except PermissionError:
                print('note: %s in use, keeping the old copy' % dst)


def prepare_slot(slot):
    home = SIMROOT / ('s%d' % slot)
    mod = home / 'rearmed'
    mod.mkdir(parents=True, exist_ok=True)
    for old in mod.glob('*.pk3'):
        old.unlink()
    for pk3 in PK3_SRC.glob('*.pk3'):
        shutil.copy2(pk3, mod / pk3.name)
    for f in ('q3config_server.cfg', 'games.log', 'ioq3_server.pid', 'bt.cfg'):
        try:
            (mod / f).unlink()
        except FileNotFoundError:
            pass
    return home, mod


def launch(slot, args, mp, idx):
    home, mod = prepare_slot(slot)
    sets = {
        'dedicated': 1, 'net_port': 28300 + slot, 'sv_pure': 0, 'timescale': 2, 'g_gametype': 0,
        'g_rearmMode': 0, 'g_log': 'games.log', 'g_logsync': 1, 'timelimit': args.minutes, 'fraglimit': 0,
        'g_mutators': 0, 'bot_rearm': 1, 'bot_rearmTricks': 1, 'bot_useProps': 1, 'bot_avoidDanger': 1,
        'g_props': 1, 'g_propCount': -1, 'sv_maxclients': 8, 'bot_minplayers': 0, 'g_doWarmup': 0,
        'g_botTactics': args.tactics, 'g_botFlank': 1, 'g_botRetreat': 1, 'g_botDodge': 1,
        'g_botRocketJump': 1, 'g_botItemTiming': 1, 'g_botHearing': 1, 'g_botHabits': 1, 'g_botLeader': 1,
        'g_botLure': 1, 'g_botWeaponThief': 1 if args.thief else 0, 'bt_debug': args.debug, 'bt_split': args.split,
        'bt_habitsTarget': 0 if args.thief else -1, 'bt_thiefTarget': 0 if args.thief else -1,
        'g_botTaunts': 0, 'g_botGrudges': 1, 'g_debugAlt': 0, 'g_rearmPowerups': 1, 'g_killstreaks': 1,
    }
    if args.standin:
        # en-tune: client 0 = a fixed stock "player" (skill --skill0, tactics off), the rest at --skill
        sets['bt_split'] = 3 if args.tactics else 0
        sets['bt_bench'] = 1
        sets['bt_habitsTarget'] = 0
        sets['bt_thiefTarget'] = 0 if args.thief else -1
    cfg = ['set %s "%s"' % (k, v) for k, v in sets.items()]
    cfg.append('map %s' % mp)
    if args.standin:
        cfg.append('addbot %s %s' % (BOTS[0], args.skill0 or '3'))
        for i in range(args.bots - 1):
            cfg.append('addbot %s %s' % (BOTS[1 + (i + idx) % (len(BOTS) - 1)], args.skill))
    else:
        for i in range(args.bots):
            cfg.append('addbot %s %s' % (BOTS[(i + idx) % len(BOTS)], args.skill0 if (i == 0 and args.skill0) else args.skill))
    (mod / 'bt.cfg').write_text('\n'.join(cfg) + '\n', newline='\n')
    cmd = [str(DIST / 'ioq3ded.exe'), '+set', 'fs_basepath', Q3BASE, '+set', 'fs_homepath', str(home),
           '+set', 'fs_game', 'rearmed', '+set', 'dedicated', '1', '+set', 'net_port', str(28300 + slot),
           '+exec', 'bt.cfg']
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = 0
    p = subprocess.Popen(cmd, cwd=str(DIST), creationflags=subprocess.CREATE_NEW_CONSOLE, startupinfo=si)
    return dict(p=p, slot=slot, mod=mod, start=time.time(), map=mp, idx=idx)


def summarize(text):
    out = {}
    stats = re.findall(r'BtStats: (.*)', text)
    out['stats'] = stats[-1] if stats else ''
    scores = re.findall(r'score: (-?\d+)\s+ping: \d+\s+client: (\d+) (.*)', text)
    out['scores'] = [(int(c), int(s), n.strip()) for s, c, n in scores]
    kills = re.findall(r'Kill: (\d+) (\d+) (\d+): .* by (\w+)', text)
    out['mods'] = collections.Counter(k[3] for k in kills)
    out['kills'] = len(kills)
    out['thief'] = re.findall(r'(Thief: .*|Reclaim: .*|ThiefDrop: .*|ThiefDown: .*)', text)
    out['habits'] = re.findall(r'BtHabits: .*', text)
    # en-tune stand-in (client 0)
    out['s_kills'] = sum(1 for k in kills if k[0] == '0' and k[1] != '0')
    out['s_deaths'] = sum(1 for k in kills if k[1] == '0')
    out['s_ttk_in'] = [int(x) for x in re.findall(r'BtBench: in (\d+)', text)]
    out['s_ttk_out'] = [int(x) for x in re.findall(r'BtBench: out (\d+)', text)]
    ex = re.findall(r'^\s*(\d+):(\d+)\s+Exit:', text, re.M)
    out['minutes'] = (int(ex[-1][0]) + int(ex[-1][1]) / 60.0) if ex else 0
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--runs', type=int, default=2)
    ap.add_argument('--par', type=int, default=2)
    ap.add_argument('--bots', type=int, default=6)
    ap.add_argument('--skill', default='4')
    ap.add_argument('--skill0', default='', help='skill of client 0 (the stand-in human with --thief)')
    ap.add_argument('--minutes', type=int, default=6)
    ap.add_argument('--maps', default='q3dm6,q3dm7')
    ap.add_argument('--tactics', type=int, default=1)
    ap.add_argument('--split', type=int, default=0)
    ap.add_argument('--thief', action='store_true')
    ap.add_argument('--debug', type=int, default=1)
    ap.add_argument('--tag', default='run')
    ap.add_argument('--slotbase', type=int, default=0)
    ap.add_argument('--standin', action='store_true', help='client 0 = stock stand-in player (en-tune)')
    args = ap.parse_args()
    copy_engine()
    maps = args.maps.split(',')
    todo = [(i, maps[i % len(maps)]) for i in range(args.runs)]
    free = list(range(args.slotbase, args.slotbase + args.par))
    running = []
    res_path = SIMROOT / ('results_%s.txt' % args.tag)
    tot_even = tot_odd = 0
    while todo or running:
        while todo and free:
            idx, mp = todo.pop(0)
            running.append(launch(free.pop(0), args, mp, idx))
            time.sleep(1.5)
        time.sleep(3)
        for r in running[:]:
            log = r['mod'] / 'games.log'
            text = log.read_text(errors='replace') if log.exists() else ''
            ended = 'Exit: ' in text and re.search(r'BtStats: .*', text.split('Exit: ', 1)[-1]) is not None
            timeout = time.time() - r['start'] > args.minutes * 30 + 240
            if not ended and not timeout and r['p'].poll() is None:
                continue
            if 'Exit: ' in text and not ended:
                time.sleep(3)
                text = log.read_text(errors='replace')
            r['p'].kill()
            running.remove(r)
            free.append(r['slot'])
            s = summarize(text)
            even = sum(sc for c, sc, n in s['scores'] if c % 2 == 0)
            odd = sum(sc for c, sc, n in s['scores'] if c % 2 == 1)
            tot_even += even
            tot_odd += odd
            line = 'run %d map %s wall %ds kills %d even %d odd %d scores %s' % (
                r['idx'], r['map'], int(time.time() - r['start']), s['kills'], even, odd,
                ' '.join('%d:%d' % (c, sc) for c, sc, n in sorted(s['scores'])))
            with open(res_path, 'a') as f:
                f.write(line + '\n')
                f.write('   mods ' + ' '.join('%s=%d' % kv for kv in s['mods'].most_common()) + '\n')
                f.write('   ' + s['stats'] + '\n')
                for h in s['habits'][-1:]:
                    f.write('   ' + h + '\n')
                for t in s['thief']:
                    f.write('   ' + t + '\n')
            if args.standin:
                f2 = open(res_path, 'a')
                f2.write('   STANDIN kills %d deaths %d minutes %.2f ttk_in %s ttk_out %s\n' % (
                    s['s_kills'], s['s_deaths'], s['minutes'], ','.join(map(str, s['s_ttk_in'])), ','.join(map(str, s['s_ttk_out']))))
                f2.close()
            print(line, flush=True)
            print('   ' + s['stats'], flush=True)
            shutil.copy2(log, r['mod'] / ('games_%s_%d.log' % (args.tag, r['idx']))) if log.exists() else None
    print('TOTAL %s: even %d odd %d' % (args.tag, tot_even, tot_odd))
    with open(res_path, 'a') as f:
        f.write('TOTAL even %d odd %d\n' % (tot_even, tot_odd))


if __name__ == '__main__':
    main()
