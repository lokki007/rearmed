"""ds-surprise: loot gem xp, economy modifier skulls, ai hooks (one-shot, skips present edits)."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from patch import sub

G = pathlib.Path(__file__).resolve().parent.parent.parent / 'ioq3' / 'code' / 'game'


def edit(path, old, new, marker):
    s = open(path, newline='').read().replace('\r\n', '\n')
    if marker in s:
        print('skip (present):', path.name, marker)
        return
    sub(str(path), old, new)
    print('ok:', path.name, marker)


edit(G / 'g_ds_loot.c', '''		if ( G_RL_HasCard( h, RLC_SECRET_FIRST + RLS_BLOODCROWN ) ) {
			xp *= 2;
		}''', '''		if ( G_RL_HasCard( h, RLC_SECRET_FIRST + RLS_BLOODCROWN ) ) {
			xp *= 2;
		}
		xp *= G_DS_SurpriseGemXpMult();		// ds-surprise: BLACKOUT''', 'G_DS_SurpriseGemXpMult')

E = G / 'g_ds_econ.c'
edit(E, '''	int		bossSkulls;		// skulls the boss bar / kill paid (before % and the multiplier)
} econ;''', '''	int		bossSkulls;		// skulls the boss bar / kill paid (before % and the multiplier)
	int		modSkulls;		// ds-surprise: skulls the floor modifier added (after the multiplier)
} econ;''', 'modSkulls;')
edit(E, '''	total = Econ_FloorSkulls( won, levelKills, styleRank, nodeType, nodeFloor, bonusPct, prog, mult, parts, &econ.bossSkulls );
''', '''	total = Econ_FloorSkulls( won, levelKills, styleRank, nodeType, nodeFloor, bonusPct, prog, mult, parts, &econ.bossSkulls );
	// ds-surprise: the floor modifier's reward (ARMOURED / GLASS +25%) on a won floor, its own summary line
	econ.modSkulls = won ? G_DS_SurpriseModSkulls( total ) : 0;
	if ( econ.modSkulls > 0 ) {
		Econ_Tally( "sk_md", econ.modSkulls );
		total += econ.modSkulls;
	}
''', 'G_DS_SurpriseModSkulls')
edit(E, '''		Econ_Tally( "sk_bn", total - raw );''', '''		Econ_Tally( "sk_bn", total - econ.modSkulls - raw );''', 'total - econ.modSkulls - raw')
edit(E, '''	Q_strcat( buf, size, va( "\\\\rm\\\\%i\\\\bd\\\\%i\\\\bs\\\\%i\\\\rd\\\\%i", econ.mult100, econ.bossPct, econ.bossSkulls,
		G_DS_RecordDifficulty() ) );''', '''	Q_strcat( buf, size, va( "\\\\rm\\\\%i\\\\bd\\\\%i\\\\bs\\\\%i\\\\rd\\\\%i\\\\md\\\\%i", econ.mult100, econ.bossPct, econ.bossSkulls,
		G_DS_RecordDifficulty(), econ.modSkulls ) );		// md: ds-surprise floor modifier skulls''', 'econ.modSkulls ) );')

A = G / 'ai_rearmed.c'
edit(A, '''G_DS_BotPriority( bs->client, client );''', '''G_DS_BotPriority( bs->client, client )
		|| G_DS_SurprisePriority( bs->client, client );	// ds-surprise: the ghost of a past run wants the player''', 'G_DS_SurprisePriority')
edit(A, '''	if ( !G_DS_BossHuntSpot( bs->client, pos ) ) {''', '''	if ( !G_DS_BossHuntSpot( bs->client, pos ) && !G_DS_SurpriseHuntSpot( bs->client, pos ) ) {		// ds-surprise: mini-boss / ghost / fodder hunt, the gold race''', 'G_DS_SurpriseHuntSpot')
edit(A, '''|| bs->client == G_DS_BossClient();''', '''|| bs->client == G_DS_BossClient()
		|| G_DS_SurpriseAggressive( bs->client );''', 'G_DS_SurpriseAggressive')
edit(G / 'ai_main.c', '''	G_DS_BotInput(bs->client, &bs->lastucmd);''', '''	G_DS_BotInput(bs->client, &bs->lastucmd);
	G_DS_SurpriseBotInput(bs->client, &bs->lastucmd);	// ds-surprise: a casting mini-boss stands, dead extras stay down''', 'G_DS_SurpriseBotInput')
print('done')
