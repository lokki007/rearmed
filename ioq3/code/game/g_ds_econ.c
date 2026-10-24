/*
===========================================================================
REARMED -- DESCENT round 2 ECONOMY, game side (owner ds-economy).

- Difficulty reward multiplier (I Can Win 0.5x .. Nightmare 1.10x, bg_descent.c
  ds_rewardMult): skulls (floor rewards, chest skulls, nemesis), gem xp (g_ds_loot.c)
  and skill xp (ds-skills) use G_DS_RewardMult(). The run's difficulty can change
  between floors (UI floor map); the multiplier of the floor being played applies.
- Floor skulls (G_DS_FloorSkulls, called by RL_Finish): won floors pay floor + kills +
  style + the node bonus (+ the share of the boss bar you took on a boss floor; the final
  blow pays its own skulls in g_ds_boss.c);
  lost floors pay their kills, and a boss floor pays the share of the boss's 10 lives the
  player took (G_DS_BossProgress, ACTS; round 2b) + a little for trying. Numbers: bg_descent.h
  DS_SK_*, docs/DESCENT_V2.md "Economy".
- Records difficulty: the lowest difficulty a floor was played on (run key diff_min,
  set by the UI when a floor launches).
- Standard modes: the frag target multiplier g_fragMult (1..3, archived) applies to
  the fraglimit of every non-DESCENT match (G_FragLimit), skirmish and the single
  player arenas included; CS_REARMED "fl" carries the target to the clients.
===========================================================================
*/
#include "g_local.h"

#define ECON_RLN_ELITE		1		// g_roguelike.c RLN_* node types
#define ECON_RLN_BOSS		2
#define ECON_RLN_EVENT		3

static vmCvar_t		econFragMult;
static qboolean		econCvarsOn;

static struct {
	int		mult100;		// the last floor's reward multiplier x100
	int		bossPct;		// boss bar removed on the last floor, percent
	int		bossSkulls;		// skulls the boss bar / kill paid (before % and the multiplier)
	int		modSkulls;		// ds-surprise: skulls the floor modifier added (after the multiplier)
	int		timeSkulls;		// en-boss: a timed boss killed with time left (after the multiplier)
} econ;

static void Econ_Cvars( void ) {
	if ( !econCvarsOn ) {
		trap_Cvar_Register( &econFragMult, "g_fragMult", "1", CVAR_ARCHIVE );
		econCvarsOn = qtrue;
	}
	trap_Cvar_Update( &econFragMult );
}

/*
=================
G_FragLimit

fraglimit x g_fragMult (1..3) outside DESCENT; DESCENT floors keep their own frag race
=================
*/
int G_FragLimit( void ) {
	int		m;

	Econ_Cvars();
	if ( g_fraglimit.integer <= 0 || G_RL_Active() ) {
		return g_fraglimit.integer;
	}
	m = econFragMult.integer;
	if ( m < 1 ) {
		m = 1;
	} else if ( m > 3 ) {
		m = 3;
	}
	return g_fraglimit.integer * m;
}

/*
=================
G_DS_Difficulty / G_DS_RewardMult / G_DS_Skulls / G_DS_RecordDifficulty
=================
*/
int G_DS_Difficulty( void ) {
	int		d;

	if ( !G_RL_Active() ) {
		return 3;
	}
	d = atoi( G_RL_RunGet( "difficulty" ) );
	if ( d < 1 || d > 5 ) {
		d = 3;
	}
	return d;
}

float G_DS_RewardMult( void ) {
	if ( !G_RL_Active() ) {
		return 1.0f;
	}
	return BG_DS_RewardMult( G_DS_Difficulty() );
}

static int Econ_Scale( int n, float mult ) {
	int		r;

	if ( n <= 0 ) {
		return n;
	}
	r = (int)( n * mult + 0.5f );
	return r < 1 ? 1 : r;
}

int G_DS_Skulls( int n ) {
	return Econ_Scale( n, G_DS_RewardMult() );
}

int G_DS_RecordDifficulty( void ) {
	int		d;

	d = atoi( G_RL_RunGet( "diff_min" ) );
	if ( d < 1 || d > 5 ) {
		d = G_DS_Difficulty();
	}
	return d;
}

static int Econ_StylePart( int styleRank ) {
	return styleRank >= 5 ? 3 : styleRank >= 3 ? 2 : styleRank >= 1 ? 1 : 0;
}

// run key += n (the summary's skull breakdown; saved with the run at the floor end)
static void Econ_Tally( const char *key, int n ) {
	if ( n && G_RL_Active() ) {
		G_RL_RunSet( key, va( "%i", atoi( G_RL_RunGet( key ) ) + n ) );
	}
}

// G_RL_AddSkulls: chests, the nemesis, the boss's final blow (already x the multiplier)
void G_DS_EconOtherSkulls( int n ) {
	Econ_Tally( "sk_ot", n );
}

// ACTS: the player's lives on a boss floor (g_dsBossLives + LAST BREATH)
int G_DS_PlayerBossLives( int base ) {
	return base + G_DS_UpRank( DSU_LASTBREATH );
}

/*
=================
Econ_FloorSkulls

The whole formula (also used by the dsecon simulation). parts: floor, kills, style rank,
node bonus, bonus percent (CS_DESCENT rb). Returns the skulls after % and the multiplier.
=================
*/
static int Econ_FloorSkulls( qboolean won, int levelKills, int styleRank, int nodeType, int nodeFloor,
	int bonusPct, float bossProgress, float mult, int *parts, int *bossOut ) {
	int		base, kills, style, node, boss, act, bar, total;

	act = DS_ActForFloor( nodeFloor );
	bar = DS_SK_BOSS_BAR + DS_SK_BOSS_BAR_ACT * act;
	kills = levelKills / DS_SK_KILLS_PER;
	if ( kills > DS_SK_KILLS_MAX ) {
		kills = DS_SK_KILLS_MAX;
	}
	base = style = node = boss = 0;
	if ( won ) {
		base = 3 + nodeFloor / 3;
		style = Econ_StylePart( styleRank );
		total = base + kills + style;
		// the node map's promise: elite x2, event x1.5, boss x3
		node = nodeType == ECON_RLN_BOSS ? total * 2 : nodeType == ECON_RLN_ELITE ? total : nodeType == ECON_RLN_EVENT ? total / 2 : 0;
		if ( nodeType == ECON_RLN_BOSS ) {
			// the boss lives YOU took (share of the bar); the kill itself is the floor's x3 and the
			// final-blow skulls (g_ds_boss.c): economy pass 2 dropped the separate kill bonus
			if ( bossProgress > 1.0f ) {
				bossProgress = 1.0f;
			}
			boss = (int)( bar * bossProgress + 0.5f );
		}
	} else if ( nodeType == ECON_RLN_BOSS && bossProgress > 0.0f ) {
		if ( bossProgress > 1.0f ) {
			bossProgress = 1.0f;
		}
		boss = DS_SK_BOSS_TRY + (int)( bar * bossProgress + 0.5f );		// every attempt pays a little
	}
	total = base + kills + style + node + boss;
	total = ( total * ( 100 + bonusPct ) + 50 ) / 100;
	total = Econ_Scale( total, mult );
	if ( parts ) {
		parts[0] = base;
		parts[1] = kills;
		parts[2] = won ? styleRank : 0;
		parts[3] = node;
		parts[4] = bonusPct;
	}
	if ( bossOut ) {
		*bossOut = boss;
	}
	return total;
}

/*
=================
G_DS_FloorSkulls

RL_Finish: this floor's skulls
=================
*/
int G_DS_FloorSkulls( qboolean won, int levelKills, int styleRank, int nodeType, int nodeFloor, int bonusPct, int *parts ) {
	float	prog, mult;
	int		total, raw;

	prog = nodeType == ECON_RLN_BOSS ? G_DS_BossProgress() : 0.0f;		// boss lives taken by the player / 10
	mult = G_DS_RewardMult();
	total = Econ_FloorSkulls( won, levelKills, styleRank, nodeType, nodeFloor, bonusPct, prog, mult, parts, &econ.bossSkulls );
	// ds-surprise: the floor modifier's reward (ARMOURED / GLASS +25%) on a won floor, its own summary line
	econ.modSkulls = won ? G_DS_SurpriseModSkulls( total ) : 0;
	if ( econ.modSkulls > 0 ) {
		Econ_Tally( "sk_md", econ.modSkulls );
		total += econ.modSkulls;
	}
	// en-boss: the timed boss fight's time bonus (a skull per 15 s left), the BOSS line of the summary
	econ.timeSkulls = won && nodeType == ECON_RLN_BOSS ? Econ_Scale( G_DS_BossTimeBonus(), mult ) : 0;
	if ( econ.timeSkulls > 0 ) {
		Econ_Tally( "sk_bo", econ.timeSkulls );
		total += econ.timeSkulls;
	}
	econ.mult100 = (int)( mult * 100.0f + 0.5f );
	econ.bossPct = nodeType == ECON_RLN_BOSS ? (int)( prog * 100.0f + 0.5f ) : -1;
	// where the skulls came from (run summary): floor + style + node, kills, boss, bonus % and difficulty
	if ( parts ) {
		raw = parts[0] + parts[1] + ( won ? Econ_StylePart( styleRank ) : 0 ) + parts[3] + econ.bossSkulls;
		Econ_Tally( "sk_fl", parts[0] + ( won ? Econ_StylePart( styleRank ) : 0 ) + parts[3] );
		Econ_Tally( "sk_ki", parts[1] );
		Econ_Tally( "sk_bo", econ.bossSkulls );
		Econ_Tally( "sk_bn", total - econ.modSkulls - econ.timeSkulls - raw );
	}
	G_LogPrintf( "DSEcon: floor %i %s type %i difficulty %i mult %.2f kills %i style %i boss %i%% (%i) bonus %i%% -> %i skulls\n",
		nodeFloor, won ? "WON" : "LOST", nodeType, G_DS_Difficulty(), mult, levelKills, styleRank,
		econ.bossPct, econ.bossSkulls, bonusPct, total );
	return total;
}

/*
=================
G_DS_EconConfigstring

CS_DESCENT keys: \rm\<multiplier x100 of this floor>\bd\<boss bar % removed, -1 not a boss floor>
\bs\<boss skulls before % and the multiplier>\rd\<records difficulty>
=================
*/
void G_DS_EconConfigstring( char *buf, int size ) {
	if ( !econ.mult100 ) {
		econ.mult100 = (int)( G_DS_RewardMult() * 100.0f + 0.5f );
		econ.bossPct = -1;
	}
	Q_strcat( buf, size, va( "\\rm\\%i\\bd\\%i\\bs\\%i\\rd\\%i\\md\\%i\\tb\\%i", econ.mult100, econ.bossPct, econ.bossSkulls,
		G_DS_RecordDifficulty(), econ.modSkulls, econ.timeSkulls ) );		// md: ds-surprise floor modifier skulls, tb: en-boss time bonus
}

/*
=================
G_DS_EconCommand

Dev: dsecon (state), dsecon sim (a simulated run that dies at the first boss, every difficulty)
=================
*/
/*
=================
Econ_SimRun

One modelled descent (econ_model.py in docs): every act's floors 1-5 go fight, fight, elite,
event, fight; a won floor has frags + 1 kills and style B, a lost one half the frags; you take
5 of the boss's 10 lives and land the final blow on a won boss floor, 3 / 4 / 4 on lost tries.
Dying on floor f = every life lost there. Returns the skulls, *minutes the play time
(4 min a floor, 9 a boss floor).
=================
*/
#define ECON_SIM_OUTCOMES	9
static const char *econSimNames[ECON_SIM_OUTCOMES] = {
	"dies on floor 3", "dies at boss 1 (took 3-4 of 10)", "beats boss 1, dies on floor 9",
	"dies at boss 2", "dies on floor 15 (act 3)", "dies at boss 3", "dies on floor 21 (act 4)",
	"dies at boss 4", "wins (boss 4 beaten, banks)"
};
static const int econSimLast[ECON_SIM_OUTCOMES] = { 3, 6, 9, 12, 15, 18, 21, 24, 0 };

static int Econ_SimType( int f ) {
	static const int	path[5] = { 0, 0, ECON_RLN_ELITE, ECON_RLN_EVENT, 0 };

	return ( f % DS_FLOORS_PER_ACT ) == 0 ? ECON_RLN_BOSS : path[( f - 1 ) % DS_FLOORS_PER_ACT];
}

static int Econ_SimFrags( int f ) {
	int		fr = 8 + ( f - 1 ) / 3;

	return fr > 16 ? 16 : fr;
}

static int Econ_SimRun( int outcome, int d, int *minutes ) {
	static const float	tries[3] = { 0.3f, 0.4f, 0.4f };
	int		f, last, lives, i, total, t;
	float	mult = ds_rewardMult[d];

	lives = d <= 2 ? 3 : d == 3 ? 2 : 1;		// RL_BaseLives, no SPARE SOUL
	last = econSimLast[outcome];
	total = 0;
	*minutes = 0;
	for ( f = 1 ; f <= ( last ? last - 1 : DS_WIN_FLOOR ) ; f++ ) {
		t = Econ_SimType( f );
		total += Econ_FloorSkulls( qtrue, Econ_SimFrags( f ) + 1, 3, t, f, 0, 0.5f, mult, NULL, NULL );
		if ( t == ECON_RLN_BOSS ) {
			total += Econ_Scale( 5 + f / 3, mult );		// the final blow (g_ds_boss.c)
		}
		*minutes += t == ECON_RLN_BOSS ? 9 : 4;
	}
	for ( i = 0 ; last && i < lives ; i++ ) {
		t = Econ_SimType( last );
		total += Econ_FloorSkulls( qfalse, Econ_SimFrags( last ) / 2, 0, t, last, 0, tries[i], mult, NULL, NULL );
		*minutes += t == ECON_RLN_BOSS ? 9 : 4;
	}
	return total;
}

qboolean G_DS_EconCommand( const char *cmd ) {
	char	arg[32];
	int		d, f, i, run, n, minutes;

	if ( !Q_stricmp( cmd, "dsscore" ) ) {
		// dev (frag target tests): dsscore <client> <score>
		// (client "me" = the first human)
		trap_Argv( 1, arg, sizeof( arg ) );
		i = atoi( arg );
		if ( !Q_stricmp( arg, "me" ) ) {
			for ( i = 0 ; i < level.maxclients ; i++ ) {
				if ( level.clients[i].pers.connected == CON_CONNECTED && !( g_entities[i].r.svFlags & SVF_BOT ) ) {
					break;
				}
			}
		}
		trap_Argv( 2, arg, sizeof( arg ) );
		if ( i >= 0 && i < level.maxclients && level.clients[i].pers.connected == CON_CONNECTED ) {
			level.clients[i].ps.persistant[PERS_SCORE] = atoi( arg );
			CalculateRanks();
			G_Printf( "dsscore: %s = %i (gametype %i fraglimit %i, target %i)\n", level.clients[i].pers.netname, atoi( arg ),
				g_gametype.integer, g_fraglimit.integer, G_FragLimit() );
			G_LogPrintf( "DSEcon: score %s = %i (gametype %i fraglimit %i, target %i)\n", level.clients[i].pers.netname, atoi( arg ),
				g_gametype.integer, g_fraglimit.integer, G_FragLimit() );
		}
		return qtrue;
	}
	if ( Q_stricmp( cmd, "dsecon" ) ) {
		return qfalse;
	}
	trap_Argv( 1, arg, sizeof( arg ) );
	if ( !Q_stricmp( arg, "perks" ) ) {
		// dev: client 0's perk numbers (DESCENT single digits vs normal): max health, damage scale, headshot scale
		gentity_t	*p = &g_entities[0];

		if ( p->inuse && p->client ) {
			G_LogPrintf( "DSEcon: perks descent %i perks 0x%x maxhealth %i scale %.4f headshot %.4f\n", G_RL_Active(),
				p->client->pers.perks, p->client->ps.stats[STAT_MAX_HEALTH],
				G_PerkDamageScale( &g_entities[ENTITYNUM_WORLD], p, 0 ), G_PerkDamageScale( &g_entities[ENTITYNUM_WORLD], p, HIT_HEADSHOT ) );
		}
		return qtrue;
	}
	if ( Q_stricmp( arg, "sim" ) ) {
		Econ_Cvars();
		if ( G_RL_Active() ) {
			char	wl[128], wx[128];
			int		evo;

			G_DS_ArsenalSave( wl, wx, sizeof( wl ), &evo );		// (read only) the player's weapon levels / xp
			G_LogPrintf( "DSEcon: growth difficulty %i mult %.2f levels %s xp %s skulls %i\n", G_DS_Difficulty(),
				G_DS_RewardMult(), wl, wx, G_RL_AddSkulls( 0 ) );
		}
		G_Printf( "dsecon: active %i difficulty %i (start %s, min %s, max %s) mult %.2f records %i | g_fragMult %i fraglimit %i target %i\n",
			G_RL_Active(), G_DS_Difficulty(), G_RL_RunGet( "diff_start" ), G_RL_RunGet( "diff_min" ), G_RL_RunGet( "diff_max" ),
			G_DS_RewardMult(), G_DS_RecordDifficulty(), econFragMult.integer, g_fraglimit.integer, G_FragLimit() );
		G_LogPrintf( "DSEcon: state difficulty %i mult %.2f records %i fragmult %i fraglimit %i target %i\n",
			G_DS_Difficulty(), G_DS_RewardMult(), G_DS_RecordDifficulty(), econFragMult.integer, g_fraglimit.integer, G_FragLimit() );
		return qtrue;
	}
	// the outcome table (docs/DESCENT_V2.md "Economy"): every difficulty, no skull bonus
	for ( i = 0 ; i < ECON_SIM_OUTCOMES ; i++ ) {
		char	line[160];

		Com_sprintf( line, sizeof( line ), "%-34s", econSimNames[i] );
		for ( d = 1 ; d <= 5 ; d++ ) {
			run = Econ_SimRun( i, d, &f );
			Q_strcat( line, sizeof( line ), va( " %5i", run ) );
			if ( d == 3 ) {
				minutes = f;
				n = run;
			}
		}
		Q_strcat( line, sizeof( line ), va( "   %3i min  %3i/h", minutes, minutes ? n * 60 / minutes : 0 ) );
		G_Printf( "dsecon sim: %s\n", line );
		G_LogPrintf( "DSEcon: sim %s\n", line );
	}
	return qtrue;
}
