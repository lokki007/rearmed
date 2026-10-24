"""Economy model of DESCENT skulls (mirrors game/g_ds_econ.c Econ_FloorSkulls / Econ_SimRun, economy pass 2).
Run: python tools/econ_model.py  (the table in docs/DESCENT_V2.md "Economy"). Before pass 2: BAR 18, BAR_ACT 4, KILL 10, KILL_ACT 5."""
import sys

FIGHT, ELITE, BOSS, EVENT = 0, 1, 2, 3
MULT = {1: 0.5, 2: 0.75, 3: 1.0, 4: 1.05, 5: 1.10}
LIVES = {1: 3, 2: 3, 3: 2, 4: 1, 5: 1}

P = dict(BAR=24, BAR_ACT=6, TRY=2, KILL=0, KILL_ACT=0, KPER=3, KMAX=4, FINAL=5,
         NODE_ELITE=1.0, NODE_EVENT=0.5, NODE_BOSS=2.0, LOSTKILLS=True)

def act(f):
    return 5 if f > 24 else (f - 1) // 6 + 1

def floor_skulls(won, kills, style, ntype, f, pct=0, prog=0.0, mult=1.0, final=False, p=P):
    a = act(f)
    bar = p['BAR'] + p['BAR_ACT'] * a
    k = min(kills // p['KPER'], p['KMAX'])
    base = st = node = boss = 0
    if won:
        base = 3 + f // 3
        st = 3 if style >= 5 else 2 if style >= 3 else 1 if style >= 1 else 0
        t = base + k + st
        node = int(t * p['NODE_BOSS']) if ntype == BOSS else int(t * p['NODE_ELITE']) if ntype == ELITE else int(t * p['NODE_EVENT']) if ntype == EVENT else 0
        if ntype == BOSS:
            boss = round(bar * min(prog, 1)) + p['KILL'] + p['KILL_ACT'] * a
    else:
        if not p['LOSTKILLS']:
            k = 0
        if ntype == BOSS and prog > 0:
            boss = p['TRY'] + round(bar * min(prog, 1))
    t = base + k + st + node + boss
    t = (t * (100 + pct) + 50) // 100
    t = max(1, int(t * mult + 0.5)) if t > 0 else t
    extra = 0
    if won and ntype == BOSS and final:
        extra = max(1, int((p['FINAL'] + f // 3) * mult + 0.5))
    return t + extra

PATH = [FIGHT, FIGHT, ELITE, EVENT, FIGHT]          # floors 1-5 of an act

def frags(f):                                     # node frags rise 8 -> 16 over the acts
    return min(16, 8 + (f - 1) // 3)

def run(outcome, d=3, p=P, pct=0):
    """outcome: ('floor', f) die on floor f; ('boss', a, shares) die at boss a; ('win',)"""
    mult = MULT[d]
    lives = LIVES[d]
    total = 0
    minutes = 0
    log = []
    def won(f, ntype, prog=0.0, final=False):
        nonlocal total, minutes
        s = floor_skulls(True, frags(f) + 1, 3, ntype, f, pct, prog, mult, final, p)
        total += s; minutes += 9 if ntype == BOSS else 4
        log.append((f, 'W', s))
    def lost(f, ntype, prog=0.0, kills=None):
        nonlocal total, minutes
        s = floor_skulls(False, frags(f) // 2 if kills is None else kills, 0, ntype, f, pct, prog, mult, False, p)
        total += s; minutes += 9 if ntype == BOSS else 4
        log.append((f, 'L', s))
    kind = outcome[0]
    if kind == 'floor':
        last = outcome[1]
        for f in range(1, last):
            won(f, BOSS if f % 6 == 0 else PATH[(f - 1) % 6], prog=0.5, final=True)
        for i in range(lives):
            lost(last, PATH[(last - 1) % 6] if last % 6 else BOSS)
    elif kind == 'boss':
        a = outcome[1]
        shares = outcome[2]
        last = a * 6
        for f in range(1, last):
            won(f, BOSS if f % 6 == 0 else PATH[(f - 1) % 6], prog=0.5, final=True)
        for i in range(lives):
            lost(last, BOSS, prog=shares[min(i, len(shares) - 1)])
    else:
        for f in range(1, 25):
            won(f, BOSS if f % 6 == 0 else PATH[(f - 1) % 6], prog=0.5, final=True)
    return total, minutes, log

OUTCOMES = [
    ('dies on floor 3 (2 lost tries)', ('floor', 3)),
    ('dies at boss 1 (took 3, 4 of 10 lives)', ('boss', 1, [0.3, 0.4, 0.4])),
    ('beats boss 1, dies on floor 9', ('floor', 9)),
    ('dies at boss 2 (took 3, 4 lives)', ('boss', 2, [0.3, 0.4, 0.4])),
    ('dies on floor 15 (act 3)', ('floor', 15)),
    ('dies at boss 3', ('boss', 3, [0.3, 0.4, 0.4])),
    ('dies on floor 21 (act 4)', ('floor', 21)),
    ('dies at boss 4', ('boss', 4, [0.3, 0.4, 0.4])),
    ('wins (beats boss 4, banks)', ('win',)),
]

def table(p=P, pct=0):
    print('%-42s' % 'outcome', ' '.join('%7s' % n for n in ['ICW', 'BIO', 'HMP', 'HC', 'NM']), '   min  HMP/h')
    for name, oc in OUTCOMES:
        vals = [run(oc, d, p, pct) for d in range(1, 6)]
        print('%-42s' % name, ' '.join('%7d' % v[0] for v in vals), '%5d %6d' % (vals[2][1], vals[2][0] * 60 / vals[2][1]))

if __name__ == '__main__':
    table()
