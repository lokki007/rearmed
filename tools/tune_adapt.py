"""
en-tune: type adaptation test, game side, on a hidden dedicated server (DESCENT fight floor 8, act 2).

  python tools/tune_adapt.py [--adapt 1] [--meta "tad_w=0,80,0,0,0,40,0,0,0,0;tad_cur=...;tad_seed=1"] [--tag a1]

Writes a meta file (the keys given), a run file (seed 777), then: dstadapt (state), dstypesim 8 4 300
(the floor mix over 300 seeds with this run's adaptation), dstadapt sniper 2 + dstadapt kamikaze 1
(the player "died" to them), rllose (the floor ends: run key tad_f). Prints the DSTypeAdapt / DSTypesSim
lines, the serverinfo g_dsTypeAdaptNow and the run file's tad_f. Home: home_tune/a<slot>.
"""
import argparse, pathlib, re, shutil, subprocess, time, sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import tune_jugg as tj


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--adapt', type=int, default=1)
    ap.add_argument('--meta', default='tad_w=0,80,0,0,0,40,0,0,0,0;tad_cur=0,0,0,0,0,0,0,0,0,0;tad_seed=1')
    ap.add_argument('--daily', type=int, default=0)
    ap.add_argument('--tag', default='a1')
    ap.add_argument('--slot', type=int, default=20)
    args = ap.parse_args()
    sets = tj.base_sets(1, 1)
    sets['g_dsTypesForce'] = ''
    sets['g_dsTypeAdapt'] = args.adapt
    sets['g_dsTypesDebug'] = 1
    if args.daily:
        sets['ds_daily'] = 1
    seq = ['wait 10'] * 30 + ['serverinfo', 'dstadapt', 'dstypesim 8 4 300', 'dstadapt sniper 2', 'dstadapt kamikaze 1',
                              'wait 10', 'rllose'] + ['wait 10'] * 30 + ['quit']
    home, mod = tj.prepare(args.slot, 'q3dm6', sets, seq, 4, 777)
    meta = '\n'.join(kv.replace('=', ' ', 1) for kv in args.meta.split(';') if kv) + '\n'
    (mod / 'rearmed_meta.txt').write_text(meta, newline='\n')
    cmd = [str(tj.DIST / 'ioq3ded.exe'), '+set', 'fs_basepath', tj.Q3BASE, '+set', 'fs_homepath', str(home),
           '+set', 'fs_game', 'rearmed', '+set', 'dedicated', '1', '+set', 'net_port', str(28400 + args.slot),
           '+set', 'com_hunkMegs', '256', '+exec', 'jt_setup.cfg', '+devmap', 'q3dm6', '+exec', 'jt_test.cfg']
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = 0
    con = mod / 'qconsole.log'
    cmd[1:1] = ['+set', 'logfile', '2']
    p = subprocess.Popen(cmd, cwd=str(tj.DIST), creationflags=subprocess.CREATE_NEW_CONSOLE, startupinfo=si)
    t0 = time.time()
    while p.poll() is None and time.time() - t0 < 120:
        time.sleep(2)
    p.kill()
    log = (mod / 'games.log').read_text(errors='replace') if (mod / 'games.log').exists() else ''
    out = [l for l in log.splitlines() if re.search(r'DSTypeAdapt|DSTypesSim: floor 8 act 2 bots 4, |DSTypes: floor', l)]
    cl = con.read_text(errors='replace') if con.exists() else ''
    out += [l for l in cl.splitlines() if 'g_dsTypeAdaptNow' in l]
    run = (mod / 'rearmed_run.txt').read_text() if (mod / 'rearmed_run.txt').exists() else ''
    out += ['run file: ' + l for l in run.splitlines() if l.startswith('tad_f') or l.startswith('lives')]
    txt = '\n'.join(out)
    (tj.HOMES / ('adapt_%s.txt' % args.tag)).write_text(txt + '\n')
    print(txt)


if __name__ == '__main__':
    main()
