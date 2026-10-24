"""
en-tune: the stand-in table from tools/tune_sweep.sh results (home_bt/results_<prefix>_s<skill>_t<0|1>.txt).

  python tools/tune_table.py pre [post ...]

Per skill and tactics off / on: the stand-in's (client 0, stock skill 3) kills per death, deaths per
minute, time-to-kill (in: the bots' time to kill it once they start hitting it, out: its time to kill a
bot it started hitting; median seconds, one-shot kills count as 0), and the on/off change of K/D
(negative = the tactics made the bots harder). +- is one bootstrap standard error of the pooled K/D.
"""
import pathlib, random, re, statistics, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent / 'home_bt'


def load(path):
    runs = []
    if not path.exists():
        return runs
    for m in re.finditer(r'STANDIN kills (\d+) deaths (\d+) minutes ([\d.]+) ttk_in ([\d,]*) ttk_out ([\d,]*)', path.read_text()):
        k, d, mins = int(m.group(1)), int(m.group(2)), float(m.group(3))
        tin = [int(x) for x in m.group(4).split(',') if x]
        tout = [int(x) for x in m.group(5).split(',') if x]
        if mins > 0:
            runs.append((k, d, mins, tin, tout))
    return runs


def summ(runs):
    if not runs:
        return None
    k = sum(r[0] for r in runs)
    d = sum(r[1] for r in runs)
    mins = sum(r[2] for r in runs)
    kd = k / max(d, 1)
    rng = random.Random(1)		# bootstrap over matches: the spread of the pooled K/D
    boots = []
    for _ in range(400):
        pick = [runs[rng.randrange(len(runs))] for _ in runs]
        boots.append(sum(r[0] for r in pick) / max(sum(r[1] for r in pick), 1))
    se = statistics.stdev(boots) if len(runs) > 1 else 0
    tin = [x for r in runs for x in r[3]]
    tout = [x for r in runs for x in r[4]]
    return dict(n=len(runs), kd=kd, se=se, dpm=d / mins, kpm=k / mins,
                tin=statistics.median(tin) / 1000 if tin else 0, tout=statistics.median(tout) / 1000 if tout else 0)


def main():
    for pfx in sys.argv[1:] or ['pre']:
        print('\n== %s ==' % pfx)
        print('skill | tactics | matches | K/D (+-se) | deaths/min | kills/min | TTK in s | TTK out s | K/D change on vs off')
        print('---|---|---|---|---|---|---|---|---')
        parts = pfx.split('+')		# "pre+pre2": both batches together
        skills = sorted({re.match(r'results_%s_s([\d.]+)_t' % re.escape(q), p.name).group(1)
                         for q in parts for p in ROOT.glob('results_%s_s*_t*.txt' % q)}, key=float)
        for s in skills:
            off = summ([r for q in parts for r in load(ROOT / ('results_%s_s%s_t0.txt' % (q, s)))])
            on = summ([r for q in parts for r in load(ROOT / ('results_%s_s%s_t1.txt' % (q, s)))])
            for name, r in (('off', off), ('on', on)):
                if not r:
                    continue
                ch = ''
                if name == 'on' and off:
                    ch = '%+.0f%%' % (100.0 * (r['kd'] / off['kd'] - 1))
                print('%s | %s | %d | %.2f (+-%.2f) | %.2f | %.2f | %.1f | %.1f | %s' % (
                    s, name, r['n'], r['kd'], r['se'], r['dpm'], r['kpm'], r['tin'], r['tout'], ch))


if __name__ == '__main__':
    main()


# en-tune: how much of the tactics the opponents use (BtStats, the 4 opponent bots), per bot-minute
CATS = [('dodge+juke', ('dodge', 'juke')), ('flank', ('flank',)), ('retreat', ('retreat',)),
        ('hear+track', ('hear', 'track')), ('item contest', ('contest',)), ('rj chase+hop', ('rjumpChase', 'strafeHop')),
        ('habits', ('preaim', 'habitWeapon', 'range')), ('leader', ('leader',)), ('grudge hunt', ('grudgeHunt',)), ('lure', ('lure',))]


def usage(path, bots=4):
    tot = {c: 0.0 for c, _ in CATS}
    mins = 0.0
    if not path.exists():
        return None
    for line in path.read_text().splitlines():
        m = re.match(r'\s+t=(\d+) (.*)', line)
        if not m:
            continue
        kv = dict(x.split('=') for x in m.group(2).split() if '=' in x)
        t = int(m.group(1)) / 60.0
        if t < 1:
            continue
        mins += t * bots
        for c, keys in CATS:
            tot[c] += sum(int(kv.get(k, 0)) for k in keys)
    if not mins:
        return None
    return {c: v / mins for c, v in tot.items()}


def usage_table(pfx):
    print('\n== %s: tactics used per opponent bot per minute (tactics on) ==' % pfx)
    print('skill | ' + ' | '.join(c for c, _ in CATS))
    print('---|' + '---|' * len(CATS))
    parts = pfx.split('+')
    skills = sorted({re.match(r'results_%s_s([\d.]+)_t' % re.escape(q), p.name).group(1)
                     for q in parts for p in ROOT.glob('results_%s_s*_t1.txt' % q)}, key=float)
    for s in skills:
        u = usage(ROOT / ('results_%s_s%s_t1.txt' % (parts[0], s)))
        if u:
            print('%s | ' % s + ' | '.join('%.2f' % u[c] for c, _ in CATS))


if __name__ == '__main__' and len(sys.argv) > 1:
    for pfx in sys.argv[1:]:
        usage_table(pfx)
