"""ds-surprise: ui hooks (F10 switches, run summary line, reset list, floor map). One-shot, skips present edits."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from patch import sub

U = pathlib.Path(__file__).resolve().parent.parent.parent / 'ioq3' / 'code' / 'q3_ui'


def edit(path, old, new, marker):
    s = open(path, newline='').read().replace('\r\n', '\n')
    if marker in s:
        print('skip (present):', path.name, marker)
        return
    sub(str(path), old, new)
    print('ok:', path.name, marker)


edit(U / 'ui_rearmed.c', '''	{ "g_dsNemesis", RMTAB_DESCENT,''', '''	{ "g_dsAmbush", RMTAB_DESCENT, "Mid-floor ambushes:", "Now and then a mini-boss crashes in partway through a floor (a warning and a pillar first). Kill it for loot.", NULL, NULL, 1, RMF_SERVER },
	{ "g_dsModifiers", RMTAB_DESCENT, "Floor modifiers:", "Some floors on the map offer an optional risk (ARMOURED, GLASS, SWARM ...) for more loot. Click the chip to take it.", NULL, NULL, 1, RMF_SERVER },
	{ "g_dsWildEvents", RMTAB_DESCENT, "Wild events:", "Rare floor events: a golden meteor, a bot uprising, a blood eclipse, a bounty.", NULL, NULL, 1, RMF_SERVER },
	{ "g_dsGhosts", RMTAB_DESCENT, "Ghosts of past runs:", "Your past self (that run's kit at this depth) may come hunting you. Beat it for a bonus.", NULL, NULL, 1, RMF_SERVER },
	{ "g_dsNemesis", RMTAB_DESCENT,''', 'g_dsAmbush')

E = U / 'ui_ds_econ.c'
edit(E, '''		Com_sprintf( line, sizeof( line ), "bonus & difficulty %s%i", bn >= 0 ? "+" : "", bn );
		RL_Text( x + 4, y, line, 5, 8, bn >= 0 ? rl_colBone : rl_colDim, 0 );
		y += 12;''', '''		Com_sprintf( line, sizeof( line ), "bonus & difficulty %s%i", bn >= 0 ? "+" : "", bn );
		RL_Text( x + 4, y, line, 5, 8, bn >= 0 ? rl_colBone : rl_colDim, 0 );
		y += 10;
		if ( RL_KVInt( &rl_run, "sk_md" ) > 0 ) {		// ds-surprise: the floor modifiers taken
			Com_sprintf( line, sizeof( line ), "floor modifiers %i", RL_KVInt( &rl_run, "sk_md" ) );
			RL_Text( x + 4, y, line, 5, 8, rl_colBone, 0 );
			y += 10;
		}
		y += 2;''', 'sk_md')
print('done')
