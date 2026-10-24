import pathlib, sys
p = pathlib.Path(r'G:\_Games\q3mod\ioq3\code\game\bg_misc.c')
s = p.read_text()
subs = [
    ('"PENTAGRAM", "BLOOD CROWN", "GOLDEN SKULL", "STOPWATCH", "UNION", "DOWSING ROD"\n};',
     '"PENTAGRAM", "BLOOD CROWN", "GOLDEN SKULL", "STOPWATCH", "UNION", "DOWSING ROD",\n'
     '\t// legendaries (dsLegend_t order, ds-cards)\n'
     '\t"CHAIN LIGHTNING SOUL", "ECHO", "VAMPIRE LORD", "TIME THIEF", "BLOOD ENGINE", "MIRROR", "PHOENIX",\n'
     '\t"MAGNETAR", "DOUBLE AGENT", "GRAVEDIGGER", "BLACK MASS", "DEADEYE"\n};'),
    ('\t"secret stashes hum louder, a faint compass on the HUD"\n};',
     '\t"secret stashes hum louder, a faint compass on the HUD",\n'
     '\t// legendaries: what they DO (single-digit numbers at most)\n'
     '\t"every kill arcs lightning into the nearest enemy; an arc that kills arcs on",\n'
     '\t"every 5th shot echoes: a free copy fires a moment later",\n'
     '\t"overkill damage becomes armor, up to your max health",\n'
     '\t"kills add time to your power spikes and powerups",\n'
     '\t"gibbing an enemy refunds a dash charge and a few shots",\n'
     '\t"the first hit you take each life is thrown back at the attacker",\n'
     '\t"once a floor a killing blow instead revives you in a fire nova",\n'
     '\t"3 gems orbit you and burn enemies they touch; gems you pick up join for a while",\n'
     '\t"the next enemy you hit fights for you for 8 s; once every half minute",\n'
     '\t"corpses you walk over give up a gem; every 5th grave a big one",\n'
     '\t"every 9th kill pulls every gem on the map to you",\n'
     '\t"every 5th hit you land is a headshot"\n};'),
    ('\t6, 6, 6, 6, 6, 6\n};',
     '\t6, 6, 6, 6, 6, 6,\n\t7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7\n};'),
    ('\t2, 2, 2, 2, 2, 1\t\t\t\t\t\t\t// secrets\n};',
     '\t2, 2, 2, 2, 2, 1,\t\t\t\t\t\t\t// secrets\n\t3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3\t\t\t// legendaries (ds-cards)\n};'),
    ('\t"S", "S", "S", "S", "S", "S"\n};',
     '\t"S", "S", "S", "S", "S", "S",\n'
     '\t"CLS", "ECH", "VMP", "TTH", "BEN", "MIR", "PHX", "MGT", "DAG", "GRV", "BMS", "DEY"\n};'),
    ('// 0 common, 1 rare, 2 epic (elite / boss offers lean to the higher tiers)',
     '// 0 common, 1 rare, 2 epic, 3 legendary (elite / boss offers lean to the higher tiers; legendaries: g_ds_cards.c)'),
]
for a, b in subs:
    if s.count(a) != 1:
        print('MISSING or ambiguous:', a[:60]); sys.exit(1)
    s = s.replace(a, b)
p.write_text(s)
print('ok')
