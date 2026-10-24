"""ds-secrets: scan stock maps for secret stash spots with a hidden dedicated server.

  python tools/secrets_scan.py q3dm1 q3dm6 q3dm7 ...   [--force] [--par 4] [--timeout 300]

Each map runs in its own home (home_sec/<map>/rearmed, pk3s copied from build_pk3) from a copy of
the engine in dist_sec/ (so tools/build.sh can still redeploy dist_test). g_dsSecretsDebug 3 makes
the game scan (or load the cache) as soon as the AAS is up and quit when done; --force (4)
ignores the cache. Prints the "Secrets:" log lines and leaves rearmed_secrets_<map>.txt there.
"""
import argparse
import os
import pathlib
import shutil
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
DIST_SRC = ROOT / 'dist_test'
DIST = ROOT / 'dist_sec'
PK3 = ROOT / 'build_pk3'
HOMES = ROOT / 'home_sec'
Q3BASE = r'H:\SteamLibrary\steamapps\common\Quake 3 Arena'


def copy_engine():
    DIST.mkdir(exist_ok=True)
    for f in ('ioq3ded.exe', 'SDL2.dll'):
        src, dst = DIST_SRC / f, DIST / f
        if not dst.exists() or dst.stat().st_mtime < src.stat().st_mtime:
            shutil.copy2(src, dst)


def prepare(mp):
    mod = HOMES / mp / 'rearmed'
    mod.mkdir(parents=True, exist_ok=True)
    for pk3 in PK3.glob('*.pk3'):
        dst = mod / pk3.name
        if 'zz-rearmed-ta' in pk3.name and not mp.startswith('mp'):
            if dst.exists():
                dst.unlink()               # 110 MB: only the Team Arena maps need it
            continue
        if not dst.exists() or dst.stat().st_size != pk3.stat().st_size or dst.stat().st_mtime < pk3.stat().st_mtime:
            shutil.copy2(pk3, dst)
    for f in ('games.log', 'q3config_server.cfg', 'ioq3_server.pid'):
        try:
            (mod / f).unlink()
        except FileNotFoundError:
            pass
    return mod


def launch(mp, slot, force):
    mod = prepare(mp)
    cmd = [str(DIST / 'ioq3ded.exe'), '+set', 'fs_basepath', Q3BASE, '+set', 'fs_homepath', str(HOMES / mp),
           '+set', 'fs_game', 'rearmed', '+set', 'dedicated', '1', '+set', 'net_port', str(28300 + slot),
           '+set', 'sv_pure', '0', '+set', 'g_gametype', '0', '+set', 'g_rearmMode', '0', '+set', 'bot_enable', '1',
           '+set', 'g_log', 'games.log', '+set', 'g_logsync', '1', '+set', 'g_dsSecretsDebug', '4' if force else '3',
           '+set', 'g_props', '1', '+set', 'bot_minplayers', '0', '+map', mp]
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = 0
    p = subprocess.Popen(cmd, cwd=str(DIST), creationflags=subprocess.CREATE_NEW_CONSOLE, startupinfo=si)
    return dict(p=p, map=mp, mod=mod, start=time.time())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('maps', nargs='+')
    ap.add_argument('--force', action='store_true')
    ap.add_argument('--par', type=int, default=3)
    ap.add_argument('--timeout', type=int, default=400)
    args = ap.parse_args()
    copy_engine()
    todo = list(args.maps)
    running = []
    slot = 0
    while todo or running:
        while todo and len(running) < args.par:
            running.append(launch(todo.pop(0), slot, args.force))
            slot += 1
        time.sleep(1)
        for r in running[:]:
            if r['p'].poll() is None and time.time() - r['start'] < args.timeout:
                continue
            if r['p'].poll() is None:
                r['p'].kill()
                print('== %s: TIMEOUT' % r['map'])
            running.remove(r)
            log = r['mod'] / 'games.log'
            lines = [l for l in log.read_text(errors='replace').splitlines() if 'Secrets:' in l] if log.exists() else []
            print('== %s (%.0f s)' % (r['map'], time.time() - r['start']))
            for l in lines:
                print('  ' + l.split('Secrets: ', 1)[1])


main()
