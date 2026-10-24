#!/usr/bin/env python3
"""DESCENT map audit (ds-maps): every .bsp the game can see, read straight from the pk3s (read only).

For each map: deathmatch spawn points, weapons / armor / health / powerups, team-only entities,
AAS present, levelshot present, arena-file types, and a rough size (playable bounds of the
spawn points and items). Used to pick the DESCENT act pools (bg_descent.c ds_acts[].maps).

  python tools/map_audit.py            # table
  python tools/map_audit.py --json f   # also write the data
"""
import io, json, os, re, struct, sys, zipfile, math

STEAM = r'H:\SteamLibrary\steamapps\common\Quake 3 Arena'
HERE = os.path.dirname(os.path.abspath(__file__))
HOME = os.path.join(HERE, '..', 'home', 'rearmed')

def pk3s():
    out = []
    dirs = [os.path.join(STEAM, 'baseq3'), HOME]
    if '--ta' in sys.argv:
        dirs.insert(1, os.path.join(STEAM, 'missionpack'))
    for d in dirs:
        for f in sorted(os.listdir(d)):
            if f.lower().endswith('.pk3'):
                out.append(os.path.join(d, f))
    return out

def entities(bsp):
    ident, ver = struct.unpack_from('<4si', bsp, 0)
    off, ln = struct.unpack_from('<ii', bsp, 8)
    txt = bsp[off:off + ln].decode('latin-1', 'replace')
    ents = []
    for block in re.findall(r'\{([^{}]*)\}', txt):
        ents.append(dict(re.findall(r'"([^"]*)"\s+"([^"]*)"', block)))
    return ents

def arenas(files):
    info = {}
    for name, data in files.items():
        if not (name.endswith('.arena') or name.endswith('arenas.txt')):
            continue
        txt = data.decode('latin-1', 'replace')
        for block in re.findall(r'\{([^{}]*)\}', txt):
            kv = dict((k.lower(), v) for k, v in re.findall(r'(\w+)\s+"([^"]*)"', block))
            if 'map' in kv:
                info[kv['map'].lower()] = kv
    return info

def main():
    files = {}
    maps = {}
    for p in pk3s():
        z = zipfile.ZipFile(p)
        for n in z.namelist():
            ln = n.lower()
            if ln.endswith('.bsp') or ln.endswith('.aas') or ln.endswith('.arena') or ln.endswith('arenas.txt') or ln.startswith('levelshots/'):
                files[ln] = (p, n)
    data = {}
    for ln, (p, n) in files.items():
        if ln.endswith('.arena') or ln.endswith('arenas.txt'):
            data[ln] = zipfile.ZipFile(p).read(n)
    ar = arenas(data)
    rows = []
    for ln, (p, n) in sorted(files.items()):
        if not ln.endswith('.bsp') or not ln.startswith('maps/'):
            continue
        m = ln[5:-4]
        bsp = zipfile.ZipFile(p).read(n)
        ents = entities(bsp)
        cls = [e.get('classname', '') for e in ents]
        dm = [e for e in ents if e.get('classname') == 'info_player_deathmatch']
        pts = []
        for e in ents:
            o = e.get('origin')
            c = e.get('classname', '')
            if o and (c.startswith('info_player') or c.startswith('weapon_') or c.startswith('item_') or c.startswith('ammo_')):
                try:
                    pts.append(tuple(float(x) for x in o.split()))
                except ValueError:
                    pass
        span = 0
        if pts:
            xs = [q[0] for q in pts]; ys = [q[1] for q in pts]; zs = [q[2] for q in pts]
            span = int(math.sqrt((max(xs) - min(xs)) * (max(ys) - min(ys))))
        rows.append({
            'map': m, 'pk3': os.path.basename(p), 'bsp_kb': len(bsp) // 1024,
            'dm_spawns': len(dm),
            'team_spawns': sum(1 for c in cls if c.lower().startswith('team_ctf_') and ('spawn' in c.lower() or 'player' in c.lower())),
            'flags': sum(1 for c in cls if c.lower() in ('team_ctf_redflag', 'team_ctf_blueflag')),
            'obelisks': sum(1 for c in cls if 'obelisk' in c),
            'weapons': sorted(set(c[7:] for c in cls if c.startswith('weapon_'))),
            'armor': sum(1 for c in cls if c.startswith('item_armor')),
            'health': sum(1 for c in cls if c.startswith('item_health')),
            'power': sorted(set(c for c in cls if c in ('item_quad', 'item_enviro', 'item_haste', 'item_invis', 'item_regen', 'item_flight'))),
            'notfree': sum(1 for e in ents if 'notfree' in e or e.get('gametype', '') and 'ffa' not in e.get('gametype', '')),
            'aas': ('maps/%s.aas' % m) in files,
            'shot': any(k.startswith('levelshots/%s.' % m) for k in files),
            'types': ar.get(m, {}).get('type', ''),
            'longname': ar.get(m, {}).get('longname', ''),
            'span': span,
        })
    if '--json' in sys.argv:
        json.dump(rows, open(sys.argv[sys.argv.index('--json') + 1], 'w'), indent=1)
    print('%-16s %-9s %5s %4s %4s %4s %5s %4s %3s %3s %-22s %s' % ('map', 'span', 'kb', 'dm', 'team', 'nfr', 'arm', 'hp', 'aas', 'sht', 'types', 'weapons'))
    for r in rows:
        print('%-16s %-9s %5d %4d %4d %4d %5d %4d %3s %3s %-22s %s' % (r['map'], r['span'], r['bsp_kb'], r['dm_spawns'], r['team_spawns'],
              r['notfree'], r['armor'], r['health'], 'y' if r['aas'] else '-', 'y' if r['shot'] else '-', r['types'][:22], ','.join(r['weapons'])))

if __name__ == '__main__':
    main()
