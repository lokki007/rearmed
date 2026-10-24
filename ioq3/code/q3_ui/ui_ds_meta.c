/*
===========================================================================
REARMED -- DESCENT v2 META menus (owner META).

The hub, character select, the upgrade tree, the collection, the run summary
(with the damage breakdown, also the victory screen), records and the run
history, plus the lifetime counters and unlocks in rearmed_meta.txt.

The game writes each finished floor into rearmed_run.txt (fst/st/fseq, dmg,
wkills, wtime: game/g_ds_meta.c). The UI merges every finished floor once into
the meta file (DSM_ProcessRun: mg_seed/mg_seq mark what was merged, kept in the
meta file because the game rewrites the run file from memory), checks the
unlocks, and writes one line per finished run into rearmed_history.txt.

Meta keys added by 0.5 (the 0.4 keys keep their meaning):
	ds2				1 once the 0.4 meta was migrated (counters seeded from the old totals)
	cnt_<stat>		lifetime counters (ds_statKeys), cnt_acts, cnt_wins, cnt_hardwins,
					cnt_bestfloor, cnt_boss0..3
	chars / secrets	unlock masks (bit per dsCharacter_t / dsSecret_t)
	seen_cards		hex bit set of cards taken (bit per rlCard_t)
	seen_evo		evolutions found (bit per dsEvolution_t), seen_boss (bit per dsBoss_t)
	wmax			highest level reached per weapon (DS_NUM_WEAPONS csv)
	best_ftime / best_fkills / best_fgibs	single floor bests
	won_<d>			a descent won on difficulty d
	unl_new			unlocks not announced yet (csv of unlock ids), unl_seed + unl_run: this run's
	last_char, mg_seed, mg_seq, hist_seed
===========================================================================
*/
#include "ui_local.h"
#include "ui_ds_meta.h"
#include "../game/bg_ds_types.h"	// en-tune: type adaptation
#include "../game/bg_ds_bestiary.h"		// en-screens: DSB_NUM_TYPES (the bestiary grid's row length)

int trap_RealTime( qtime_t *qtime );

#define DSM_UNL_SECRET		100		// unlock ids: 0..CHR_NUM-1 characters, 100 + dsSecret_t secrets
#define DSM_MAX_UNL			24
#define DSM_HIST_MAX		30
#define DSM_HIST_LINE		200

static vec4_t dsm_colGold		= { 1.0f, 0.78f, 0.25f, 1.0f };
static vec4_t dsm_colLegend		= { 1.0f, 0.6f, 0.1f, 1.0f };		// round 3: legendary cards
static vec4_t dsm_colEvo		= { 1.0f, 0.55f, 0.1f, 1.0f };
static vec4_t dsm_colSilhouette	= { 0.05f, 0.03f, 0.03f, 1.0f };
static vec4_t dsm_colBar		= { 0.75f, 0.2f, 0.08f, 0.9f };
static vec4_t dsm_colBarBack	= { 0.15f, 0.05f, 0.04f, 0.9f };

static vec4_t dsm_passiveColors[RLP_NUM] = {
	{ 0.95f, 0.25f, 0.2f, 1 }, { 0.6f, 0.6f, 0.65f, 1 }, { 0.9f, 0.3f, 0.45f, 1 }, { 0.55f, 0.1f, 0.15f, 1 },
	{ 0.6f, 0.85f, 1.0f, 1 }, { 1.0f, 0.75f, 0.3f, 1 }, { 1.0f, 0.5f, 0.15f, 1 }, { 0.75f, 0.7f, 0.5f, 1 },
	{ 0.5f, 0.5f, 1.0f, 1 }, { 0.4f, 0.9f, 0.4f, 1 }, { 0.85f, 0.4f, 0.9f, 1 }, { 0.95f, 0.85f, 0.3f, 1 },
	{ 0.7f, 0.55f, 0.35f, 1 }
};
static vec4_t dsm_perkColors[4] = {
	{ 1.0f, 0.4f, 0.25f, 1 }, { 0.5f, 0.7f, 1.0f, 1 }, { 0.5f, 0.95f, 0.5f, 1 }, { 0.85f, 0.5f, 1.0f, 1 }
};

static qhandle_t	dsm_charIcons[CHR_NUM];
static qhandle_t	dsm_bossIcons[BOSS_NUM];
static qhandle_t	dsm_weaponIcons[DS_NUM_WEAPONS];
static sfxHandle_t	dsm_unlockSound;

static int			dsm_pendingChar;		// the character picked for the run being started


/*
=======================================================================

SMALL HELPERS

=======================================================================
*/

static int DSM_Popcount( int m ) {
	int		n = 0;

	while ( m ) {
		n += m & 1;
		m = (unsigned)m >> 1;
	}
	return n;
}

static int DSM_HexVal( char c ) {
	if ( c >= '0' && c <= '9' ) {
		return c - '0';
	}
	if ( c >= 'a' && c <= 'f' ) {
		return c - 'a' + 10;
	}
	if ( c >= 'A' && c <= 'F' ) {
		return c - 'A' + 10;
	}
	return 0;
}

// hex bit sets ("seen_cards"): character n holds bits 4n..4n+3
static int DSM_BitGet( const char *hex, int bit ) {
	int		n;

	n = bit >> 2;
	if ( bit < 0 || n >= (int)strlen( hex ) ) {
		return 0;
	}
	return ( DSM_HexVal( hex[n] ) >> ( bit & 3 ) ) & 1;
}

static qboolean DSM_MetaBitSet( const char *key, int bit ) {
	char	buf[64];
	int		n;
	int		len;
	int		v;

	n = bit >> 2;
	if ( bit < 0 || n >= (int)sizeof( buf ) - 2 ) {
		return qfalse;
	}
	Q_strncpyz( buf, RL_KVGet( &rl_meta, key ), sizeof( buf ) );
	len = strlen( buf );
	while ( len <= n ) {
		buf[len++] = '0';
		buf[len] = 0;
	}
	v = DSM_HexVal( buf[n] );
	if ( v & ( 1 << ( bit & 3 ) ) ) {
		return qfalse;
	}
	v |= 1 << ( bit & 3 );
	buf[n] = "0123456789abcdef"[v];
	RL_KVSet( &rl_meta, key, buf );
	return qtrue;
}

static int DSM_Meta( const char *key ) {
	return RL_KVInt( &rl_meta, key );
}

static void DSM_MetaAdd( const char *key, int n ) {
	if ( n ) {
		RL_KVSetInt( &rl_meta, key, RL_KVInt( &rl_meta, key ) + n );
	}
}

static void DSM_MetaMax( const char *key, int n ) {
	if ( n > RL_KVInt( &rl_meta, key ) ) {
		RL_KVSetInt( &rl_meta, key, n );
	}
}

static int DSM_Cnt( int stat ) {
	return RL_KVInt( &rl_meta, va( "cnt_%s", ds_statKeys[stat] ) );
}

static const char *DSM_Csv( const int *v, int n ) {
	static char	buf[2][256];
	static int	index;
	char		*b;
	int			i;

	b = buf[index++ & 1];
	b[0] = 0;
	for ( i = 0; i < n; i++ ) {
		Q_strcat( b, 256, va( i ? ",%i" : "%i", v[i] ) );
	}
	return b;
}

// a csv run / meta key into a fixed array (missing values 0)
static void DSM_ReadCsv( rlKV_t *kv, const char *key, int *out, int n ) {
	int		i;

	for ( i = 0; i < n; i++ ) {
		out[i] = 0;
	}
	RL_ParseInts( RL_KVGet( kv, key ), out, n );
}

static int DSM_BestFloorAny( void ) {
	int		i;
	int		best;

	best = DSM_Meta( "cnt_bestfloor" );
	for ( i = 1; i <= 5; i++ ) {
		if ( DSM_Meta( va( "best_floor_%i", i ) ) > best ) {
			best = DSM_Meta( va( "best_floor_%i", i ) );
		}
	}
	return best;
}

static const char *DSM_TimeShort( int msec ) {
	int		sec;

	sec = msec / 1000;
	if ( sec >= 3600 ) {
		return va( "%ih%02i", sec / 3600, ( sec / 60 ) % 60 );
	}
	return va( "%i:%02i", sec / 60, sec % 60 );
}

// "THICK SKIN" -> "TS", "PENTAGRAM" -> "PEN": a tile tag for cards without a good short tag
static const char *DSM_Initials( const char *name ) {
	static char	buf[4];
	int			n;
	const char	*p;

	buf[0] = 0;
	if ( !name || !name[0] ) {
		return buf;
	}
	n = 0;
	for ( p = name; *p && n < 3; p++ ) {
		if ( ( p == name || p[-1] == ' ' ) && *p != ' ' ) {
			buf[n++] = *p;
		}
	}
	if ( n == 1 ) {
		Q_strncpyz( buf, name, sizeof( buf ) );
		n = strlen( buf );
	}
	buf[n] = 0;
	return buf;
}

// item icons ignore the draw colour: a dark veil over them instead
static void DSM_PicDim( float x, float y, float w, float h, qhandle_t shader, qboolean dim ) {
	static vec4_t	veil = { 0.03f, 0.012f, 0.01f, 0.82f };

	RL_Pic( x, y, w, h, shader, NULL );
	if ( dim ) {
		UI_FillRect( x, y, w, h, veil );
	}
}

static qhandle_t DSM_WeaponIcon( int w ) {
	if ( w <= 0 || w >= DS_NUM_WEAPONS ) {
		return 0;
	}
	return dsm_weaponIcons[w];
}

// a coloured tile with a short tag (passives, perks, abilities: no icons of their own)
static void DSM_Tile( float x, float y, float sz, const char *tag, const float *color, qboolean found ) {
	vec4_t	c;
	float	cw;

	if ( found ) {
		RL_Scale( c, color, 0.45f, 1 );
		RL_Pic( x - sz * 0.25f, y - sz * 0.25f, sz * 1.5f, sz * 1.5f, rl_shGlow, c );
		RL_Scale( c, color, 0.3f, 1 );
		RL_Frame( x, y, sz, sz, c, color );
	}
	else {
		RL_Frame( x, y, sz, sz, dsm_colSilhouette, rl_colDark );
	}
	if ( !tag || !tag[0] ) {
		return;
	}
	cw = sz * 0.8f / strlen( tag );
	if ( cw > sz * 0.3f ) {
		cw = sz * 0.3f;
	}
	RL_TextS( x + sz / 2, y + sz / 2 - cw * 0.7f, tag, cw, cw * 1.4f, found ? rl_colBone : rl_colDark, 1 );
}


/*
=======================================================================

UNLOCKS

=======================================================================
*/

static qboolean DSM_CharOwned( int c ) {
	if ( c == CHR_RANGER ) {
		return qtrue;
	}
	if ( c < 0 || c >= CHR_NUM ) {
		return qfalse;
	}
	return ( DSM_Meta( "chars" ) & ( 1 << c ) ) != 0;
}

static qboolean DSM_SecretOwned( int s ) {
	return ( DSM_Meta( "secrets" ) & ( 1 << s ) ) != 0;
}

/*
=================
DSM_CharProgress

How far the lifetime counters are toward a character: returns the target
(0 = no counter), *have = the progress
=================
*/
static int DSM_CharProgress( int c, int *have ) {
	*have = 0;
	switch ( c ) {
	case CHR_SARGE:		*have = DSM_Meta( "cnt_acts" ); return 1;
	case CHR_VISOR:		*have = DSM_Cnt( DSST_HEADSHOTS ); return 300;
	case CHR_ANARKI:	*have = DSM_BestFloorAny(); return 10;
	case CHR_KLESK:		*have = DSM_Cnt( DSST_GKILLS ); return 200;
	case CHR_XAERO:		*have = DSM_Cnt( DSST_EVOS ); return 1;
	case CHR_DOOM:		*have = DSM_Cnt( DSST_KILLS ); return 1000;
	case CHR_HUNTER:	*have = DSM_Cnt( DSST_ZIPS ); return 100;
	case CHR_ORBB:
		*have = ( DSM_Meta( "cnt_hardwins" ) > 0 || ( DSM_Meta( "secrets" ) & ( ( 1 << RLS_NUM ) - 1 ) ) == ( 1 << RLS_NUM ) - 1 );
		return 1;
	default:
		return 0;
	}
}

static const char *DSM_UnlockName( int id ) {
	if ( id >= 0 && id < CHR_NUM ) {
		return ds_characters[id].name;
	}
	if ( id >= DSM_UNL_SECRET && id < DSM_UNL_SECRET + RLS_NUM ) {
		return bg_rlCardNames[RLC_SECRET_FIRST + id - DSM_UNL_SECRET];
	}
	return "";
}

// id unlocked now: meta bit, the announce list, this run's list
static void DSM_Unlock( int id ) {
	char	buf[RL_VAL_LEN];
	int		seed;

	if ( id >= 0 && id < CHR_NUM ) {
		RL_KVSetInt( &rl_meta, "chars", DSM_Meta( "chars" ) | ( 1 << id ) | 1 );
	}
	else if ( id >= DSM_UNL_SECRET && id < DSM_UNL_SECRET + RLS_NUM ) {
		RL_KVSetInt( &rl_meta, "secrets", DSM_Meta( "secrets" ) | ( 1 << ( id - DSM_UNL_SECRET ) ) );
	}
	else {
		return;
	}
	Q_strncpyz( buf, RL_KVGet( &rl_meta, "unl_new" ), sizeof( buf ) );
	Q_strcat( buf, sizeof( buf ), va( buf[0] ? ",%i" : "%i", id ) );
	RL_KVSet( &rl_meta, "unl_new", buf );

	seed = RL_KVInt( &rl_run, "seed" );
	if ( seed ) {
		if ( DSM_Meta( "unl_seed" ) != seed ) {
			RL_KVSetInt( &rl_meta, "unl_seed", seed );
			RL_KVSet( &rl_meta, "unl_run", "" );
		}
		Q_strncpyz( buf, RL_KVGet( &rl_meta, "unl_run" ), sizeof( buf ) );
		Q_strcat( buf, sizeof( buf ), va( buf[0] ? ",%i" : "%i", id ) );
		RL_KVSet( &rl_meta, "unl_run", buf );
	}
	Com_Printf( "DESCENT: unlocked %s\n", DSM_UnlockName( id ) );
}

/*
=================
DSM_CheckUnlocks

fst: the floor just merged (NULL: lifetime checks only); run growth from rl_run
=================
*/
static void DSM_CheckUnlocks( const int *fst, qboolean withRun ) {
	int		c;
	int		have;
	int		need;
	int		wl[DS_NUM_WEAPONS];
	int		i;
	int		n;
	int		evo;

	for ( c = 0; c < CHR_NUM; c++ ) {
		if ( c == CHR_RANGER || DSM_CharOwned( c ) || c == CHR_ORBB ) {
			continue;
		}
		need = DSM_CharProgress( c, &have );
		if ( need > 0 && have >= need ) {
			DSM_Unlock( c );
		}
	}

	if ( fst ) {
		if ( !DSM_SecretOwned( RLS_PENTAGRAM ) && fst[DSST_GIBS] >= 10 ) {
			DSM_Unlock( DSM_UNL_SECRET + RLS_PENTAGRAM );
		}
		if ( !DSM_SecretOwned( RLS_STOPWATCH ) && fst[DSST_WON] && fst[DSST_TIME] > 0 && fst[DSST_TIME] < 60000 ) {
			DSM_Unlock( DSM_UNL_SECRET + RLS_STOPWATCH );
		}
	}
	if ( withRun ) {
		DSM_ReadCsv( &rl_run, "wl", wl, DS_NUM_WEAPONS );
		n = 0;
		for ( i = 1; i < DS_NUM_WEAPONS; i++ ) {
			if ( wl[i] >= DS_MAX_WLEVEL ) {
				n++;
			}
		}
		if ( !DSM_SecretOwned( RLS_BLOODCROWN ) && n >= 3 ) {
			DSM_Unlock( DSM_UNL_SECRET + RLS_BLOODCROWN );
		}
		evo = RL_KVInt( &rl_run, "evo" ) & ( ( 1 << EVO_ARMAGEDDON ) - 1 );
		if ( !DSM_SecretOwned( RLS_UNION ) && DSM_Popcount( evo ) >= 2 ) {
			DSM_Unlock( DSM_UNL_SECRET + RLS_UNION );
		}
	}
	if ( !DSM_SecretOwned( RLS_GOLDENSKULL ) && DSM_Cnt( DSST_CHESTS ) >= 25 ) {
		DSM_Unlock( DSM_UNL_SECRET + RLS_GOLDENSKULL );
	}
	// ds-secrets: 5 secret stashes found (lifetime) -> the DOWSING ROD card
	if ( !DSM_SecretOwned( RLS_DOWSING ) && DSM_Cnt( DSST_STASHES ) >= 5 ) {
		DSM_Unlock( DSM_UNL_SECRET + RLS_DOWSING );
	}

	// ORBB last: finding the fifth secret above counts
	if ( !DSM_CharOwned( CHR_ORBB ) ) {
		DSM_CharProgress( CHR_ORBB, &have );
		if ( have ) {
			DSM_Unlock( CHR_ORBB );
		}
	}
}


/*
=======================================================================

MERGING THE GAME'S FLOORS INTO THE META FILE

=======================================================================
*/

/*
=================
DSM_Migrate

A meta file from 0.4: the lifetime counters start from its totals, so the
kills and floors already played count toward the unlocks. Only adds keys.
=================
*/
static void DSM_Migrate( void ) {
	int		best;

	if ( DSM_Meta( "ds2" ) ) {
		return;
	}
	best = DSM_BestFloorAny();
	if ( !RL_KVGet( &rl_meta, "cnt_kills" )[0] ) {
		RL_KVSetInt( &rl_meta, "cnt_kills", DSM_Meta( "total_kills" ) );
	}
	if ( !RL_KVGet( &rl_meta, "cnt_won" )[0] ) {
		RL_KVSetInt( &rl_meta, "cnt_won", DSM_Meta( "total_floors" ) );
	}
	DSM_MetaMax( "cnt_bestfloor", best );
	DSM_MetaMax( "cnt_acts", best / DS_FLOORS_PER_ACT );	// 0.4 floors: every sixth one cleared counts as an act
	RL_KVSetInt( &rl_meta, "chars", DSM_Meta( "chars" ) | 1 );
	RL_KVSetInt( &rl_meta, "ds2", 1 );
	DSM_CheckUnlocks( NULL, qfalse );
	RL_SaveMeta();
	Com_Printf( "DESCENT: meta file brought up to 0.5 (%i kills, best floor %i)\n", DSM_Meta( "cnt_kills" ), best );
}

/*
=================
DSM_TypeAdaptMerge

en-tune (bg_ds_types.h, TYPE ADAPTATION): this floor's deaths by killer archetype (run key
tad_f) into the meta. A new run's first merge first rolls the last run into the weights:
tad_w (x10) = tad_w * decay + tad_cur, then tad_cur starts over for this run (tad_seed).
Never for the daily (it never adapts or counts).
=================
*/
static void DSM_TypeAdaptMerge( void ) {
	int		f[DSAR_NUM], cur[DSAR_NUM], wi[DSAR_NUM];
	float	w[DSAR_NUM];
	int		i, seed;

	if ( UI_DS_DailyOn() || !RL_KVGet( &rl_run, "tad_f" )[0] ) {
		return;
	}
	seed = RL_KVInt( &rl_run, "seed" );
	DSM_ReadCsv( &rl_run, "tad_f", f, DSAR_NUM );
	DSM_ReadCsv( &rl_meta, "tad_cur", cur, DSAR_NUM );
	if ( RL_KVInt( &rl_meta, "tad_seed" ) != seed ) {
		DSM_ReadCsv( &rl_meta, "tad_w", wi, DSAR_NUM );
		for ( i = 0; i < DSAR_NUM; i++ ) {
			w[i] = wi[i] * 0.1f;
		}
		BG_DS_TypeAdaptRoll( w, cur );
		for ( i = 0; i < DSAR_NUM; i++ ) {
			wi[i] = (int)( w[i] * 10.0f + 0.5f );
			cur[i] = 0;
		}
		RL_KVSet( &rl_meta, "tad_w", DSM_Csv( wi, DSAR_NUM ) );
		RL_KVSetInt( &rl_meta, "tad_seed", seed );
	}
	for ( i = 0; i < DSAR_NUM; i++ ) {
		cur[i] += f[i];
	}
	RL_KVSet( &rl_meta, "tad_cur", DSM_Csv( cur, DSAR_NUM ) );
}

/*
=================
DSM_MergeFloor

The last finished floor of rl_run into rl_meta (the caller saves)
=================
*/
static void DSM_MergeFloor( void ) {
	int		fst[DSST_NUM];
	int		wl[DS_NUM_WEAPONS];
	int		pv[RLP_NUM];
	int		cards[RL_MAX_CARDS];
	int		i;
	int		n;
	int		floor;
	int		evo;
	int		boss;
	int		diff;

	DSM_ReadCsv( &rl_run, "fst", fst, DSST_NUM );
	for ( i = 0; i < DSST_NUM; i++ ) {
		if ( i != DSST_FLOOR && i != DSST_BOSSID ) {
			DSM_MetaAdd( va( "cnt_%s", ds_statKeys[i] ), fst[i] );
		}
	}
	floor = fst[DSST_FLOOR];
	diff = UI_DS_RecordDifficulty();		// ds-economy: a mixed run counts as the easiest difficulty played

	boss = fst[DSST_BOSSID] - 1;
	if ( boss >= 0 && boss < BOSS_NUM ) {
		DSM_MetaAdd( va( "cnt_boss%i", boss ), 1 );
		RL_KVSetInt( &rl_meta, "seen_boss", DSM_Meta( "seen_boss" ) | ( 1 << boss ) );
	}
	if ( fst[DSST_WON] ) {
		if ( floor > 0 && floor <= DS_WIN_FLOOR && DS_IsBossFloor( floor ) ) {
			DSM_MetaAdd( "cnt_acts", 1 );
		}
		if ( floor == DS_WIN_FLOOR ) {
			DSM_MetaAdd( "cnt_wins", 1 );
			if ( diff >= 4 ) {
				DSM_MetaAdd( "cnt_hardwins", 1 );
			}
			RL_KVSetInt( &rl_meta, va( "won_%i", diff ), 1 );
		}
		if ( fst[DSST_TIME] > 0 && ( !DSM_Meta( "best_ftime" ) || fst[DSST_TIME] < DSM_Meta( "best_ftime" ) ) ) {
			RL_KVSetInt( &rl_meta, "best_ftime", fst[DSST_TIME] );
		}
	}
	DSM_MetaMax( "best_fkills", fst[DSST_KILLS] );
	DSM_MetaMax( "best_fgibs", fst[DSST_GIBS] );
	DSM_MetaMax( "cnt_bestfloor", RL_KVInt( &rl_run, "floor" ) );

	// what was seen this run
	n = RL_ParseInts( RL_KVGet( &rl_run, "cards" ), cards, RL_MAX_CARDS );
	for ( i = 0; i < n; i++ ) {
		if ( cards[i] >= 0 && cards[i] < RLC_NUM ) {
			DSM_MetaBitSet( "seen_cards", cards[i] );
		}
	}
	DSM_ReadCsv( &rl_run, "pv", pv, RLP_NUM );
	for ( i = 0; i < RLP_NUM; i++ ) {
		if ( pv[i] > 0 ) {
			DSM_MetaBitSet( "seen_cards", RLC_PASSIVE_FIRST + i );
		}
	}
	{
		int		wmax[DS_NUM_WEAPONS];

		DSM_ReadCsv( &rl_run, "wl", wl, DS_NUM_WEAPONS );
		DSM_ReadCsv( &rl_meta, "wmax", wmax, DS_NUM_WEAPONS );
		for ( i = 0; i < DS_NUM_WEAPONS; i++ ) {
			if ( wl[i] > wmax[i] ) {
				wmax[i] = wl[i];
			}
		}
		RL_KVSet( &rl_meta, "wmax", DSM_Csv( wmax, DS_NUM_WEAPONS ) );
	}
	evo = RL_KVInt( &rl_run, "evo" );
	RL_KVSetInt( &rl_meta, "seen_evo", DSM_Meta( "seen_evo" ) | evo );
	if ( DSM_Cnt( DSST_EVOS ) < DSM_Popcount( DSM_Meta( "seen_evo" ) ) ) {
		RL_KVSetInt( &rl_meta, "cnt_evos", DSM_Popcount( DSM_Meta( "seen_evo" ) ) );
	}

	UI_DS_BestiaryMerge();		// en-bestiary: who the player met on this floor, kills per enemy kind
	DSM_TypeAdaptMerge();		// en-tune: deaths by killer archetype (type adaptation next run)
	DSM_CheckUnlocks( fst, qtrue );
}

/*
=================
DSM_HistoryAdd

One line per finished run: date|difficulty|character|floor|result|kills|time|skulls|cause
=================
*/
static void DSM_HistoryAdd( const char *result ) {
	static char		buf[DSM_HIST_MAX * DSM_HIST_LINE + 512];
	static char		out[DSM_HIST_MAX * DSM_HIST_LINE + 512];
	char			*lines[DSM_HIST_MAX + 8];
	char			line[DSM_HIST_LINE];
	char			cause[96];
	char			*p;
	fileHandle_t	f;
	qtime_t			qt;
	int				len;
	int				n;
	int				i;
	int				chr;

	if ( UI_DS_DailyOn() ) {
		return;		// en-daily: a daily attempt never goes into the descent history
	}
	// what is there
	n = 0;
	buf[0] = 0;
	len = trap_FS_FOpenFile( RL_HISTORY_FILE, &f, FS_READ );
	if ( f ) {
		if ( len > (int)sizeof( buf ) - 1 ) {
			len = sizeof( buf ) - 1;
		}
		if ( len > 0 ) {
			trap_FS_Read( buf, len, f );
			buf[len] = 0;
		}
		trap_FS_FCloseFile( f );
	}
	p = buf;
	while ( *p && n < DSM_HIST_MAX + 4 ) {
		lines[n] = p;
		while ( *p && *p != '\n' ) {
			p++;
		}
		if ( *p ) {
			*p++ = 0;
		}
		len = strlen( lines[n] );
		while ( len > 0 && lines[n][len - 1] == '\r' ) {
			lines[n][--len] = 0;
		}
		if ( lines[n][0] ) {
			n++;
		}
	}

	// the new line
	Q_strncpyz( cause, RL_KVGet( &rl_run, "cause" ), sizeof( cause ) );
	for ( i = 0; cause[i]; i++ ) {
		if ( cause[i] == '|' || cause[i] == '\n' || cause[i] == '\r' ) {
			cause[i] = ' ';
		}
	}
	memset( &qt, 0, sizeof( qt ) );
	trap_RealTime( &qt );
	chr = RL_KVInt( &rl_run, "version" ) >= 2 ? RL_KVInt( &rl_run, "character" ) : -1;
	Com_sprintf( line, sizeof( line ), "%04i-%02i-%02i %02i:%02i|%s|%i|%i|%s|%i|%i|%i|%s",
		qt.tm_year + 1900, qt.tm_mon + 1, qt.tm_mday, qt.tm_hour, qt.tm_min,
		UI_DS_HistoryDifficulty(), chr, RL_KVInt( &rl_run, "floor" ), result,		// ds-economy: "2-4" = mixed
		RL_KVInt( &rl_run, "kills" ), RL_KVInt( &rl_run, "time" ),
		RL_KVGet( &rl_run, "banked" )[0] ? RL_KVInt( &rl_run, "banked" ) : 0, cause );

	out[0] = 0;
	for ( i = ( n >= DSM_HIST_MAX ? n - DSM_HIST_MAX + 1 : 0 ); i < n; i++ ) {
		Q_strcat( out, sizeof( out ), lines[i] );
		Q_strcat( out, sizeof( out ), "\n" );
	}
	Q_strcat( out, sizeof( out ), line );
	Q_strcat( out, sizeof( out ), "\n" );

	trap_FS_FOpenFile( RL_HISTORY_FILE, &f, FS_WRITE );
	if ( !f ) {
		Com_Printf( S_COLOR_YELLOW "DESCENT: can't write %s\n", RL_HISTORY_FILE );
		return;
	}
	trap_FS_Write( out, strlen( out ), f );
	trap_FS_FCloseFile( f );
	Com_Printf( "DESCENT: history: %s\n", line );
}

static qboolean DSM_RunWon( void ) {
	return RL_KVInt( &rl_run, "won" ) || ( RL_KVInt( &rl_run, "version" ) >= 2 && RL_KVInt( &rl_run, "floor" ) >= DS_WIN_FLOOR );
}

/*
=================
DSM_ProcessRun

Merges a finished floor not merged yet, logs a finished run into the history.
Safe to call any time (it only acts once per floor / run). Leaves rl_run and
rl_meta loaded.
=================
*/
static void DSM_ProcessRun( void ) {
	int			seed;
	int			seq;
	qboolean	metaDirty;
	const char	*result;

	RL_LoadMeta();
	DSM_Migrate();
	UI_DS_EconMigrate();		// ds-economy pass 2: refunds for retired upgrades (once)
	UI_DS_SkillsMerge();		// ds-skills: the game's pending skill xp, once per batch
	RL_LoadRun();
	seed = RL_KVInt( &rl_run, "seed" );
	if ( !rl_run.num || !seed ) {
		return;
	}
	metaDirty = qfalse;

	seq = RL_KVInt( &rl_run, "fseq" );
	if ( seq > 0 && ( DSM_Meta( "mg_seed" ) != seed || seq > DSM_Meta( "mg_seq" ) ) ) {
		DSM_MergeFloor();
		RL_KVSetInt( &rl_meta, "mg_seed", seed );
		RL_KVSetInt( &rl_meta, "mg_seq", seq );
		metaDirty = qtrue;
	}

	if ( !RL_KVInt( &rl_run, "active" ) && DSM_Meta( "hist_seed" ) != seed ) {
		if ( RL_KVInt( &rl_run, "retired" ) ) {
			result = "RETIRED";
		}
		else if ( DSM_RunWon() ) {
			result = RL_KVInt( &rl_run, "floor" ) > DS_WIN_FLOOR ? "ABYSS" : "WON";
		}
		else {
			result = "LOST";
		}
		DSM_HistoryAdd( result );
		RL_KVSetInt( &rl_meta, "hist_seed", seed );
		metaDirty = qtrue;
	}
	if ( metaDirty ) {
		RL_SaveMeta();
	}
}

/*
=================
DSM_Bank

Ends the loaded run from the UI exactly like the game's RL_EndRun: skulls,
descents, records. (A won run ended at the victory screen, an old 0.4 run.)
=================
*/
static void DSM_Bank( void ) {
	int		skulls;
	int		floor;
	int		kills;
	int		diff;
	int		newFloor;
	int		newKills;
	char	key[32];

	RL_LoadMeta();
	if ( !RL_KVInt( &rl_run, "active" ) ) {
		return;
	}
	skulls = RL_KVInt( &rl_run, "skulls" );
	floor = RL_KVInt( &rl_run, "floor" );
	kills = RL_KVInt( &rl_run, "kills" );
	diff = UI_DS_RecordDifficulty();		// ds-economy: the easiest difficulty played
	DSM_MetaAdd( "skulls", skulls );
	DSM_MetaAdd( "runs", 1 );
	Com_sprintf( key, sizeof( key ), "best_floor_%i", diff );
	newFloor = floor > DSM_Meta( key );
	if ( newFloor ) {
		RL_KVSetInt( &rl_meta, key, floor );
	}
	newKills = kills > DSM_Meta( "best_kills" );
	if ( newKills ) {
		RL_KVSetInt( &rl_meta, "best_kills", kills );
	}
	DSM_MetaAdd( "total_kills", kills );
	DSM_MetaAdd( "total_floors", floor );
	RL_SaveMeta();

	RL_KVSetInt( &rl_run, "active", 0 );
	RL_KVSetInt( &rl_run, "victory", 0 );
	RL_KVSetInt( &rl_run, "infloor", 0 );
	RL_KVSetInt( &rl_run, "banked", skulls );
	RL_KVSetInt( &rl_run, "new_best_floor", newFloor );
	RL_KVSetInt( &rl_run, "new_best_kills", newKills );
	RL_SaveRun();
	Com_Printf( "DESCENT: run banked at floor %i, %i skulls\n", floor, skulls );
}

// the unlock ids in a csv meta key
static int DSM_UnlockList( const char *key, int *out ) {
	int		n;

	n = RL_ParseInts( RL_KVGet( &rl_meta, key ), out, DSM_MAX_UNL );
	return n;
}

static void DSM_ClearAnnounced( void ) {
	RL_LoadMeta();
	if ( RL_KVGet( &rl_meta, "unl_new" )[0] ) {
		RL_KVSet( &rl_meta, "unl_new", "" );
		RL_SaveMeta();
	}
}


/*
=======================================================================

RUN START HOOKS (called from RL_StartRun)

=======================================================================
*/

void UI_DS_BeforeNewRun( void ) {
	int		seed;

	DSM_ProcessRun();
	seed = RL_KVInt( &rl_run, "seed" );
	if ( RL_KVInt( &rl_run, "active" ) && seed && DSM_Meta( "hist_seed" ) != seed ) {
		DSM_HistoryAdd( "ABANDONED" );
		RL_KVSetInt( &rl_meta, "hist_seed", seed );
		RL_SaveMeta();
	}
}

/*
=================
UI_DS_StartRunKeys

The new run file (being built in rl_run): version 2, the character and its kit,
every upgrade rank
=================
*/
void UI_DS_StartRunKeys( void ) {
	const dsCharInfo_t	*ch;
	int					cards[RL_MAX_CARDS];
	int					pv[RLP_NUM];
	int					wl[DS_NUM_WEAPONS];
	int					n;
	int					k;
	int					c;
	int					i;

	c = dsm_pendingChar;
	if ( !DSM_CharOwned( c ) ) {
		c = CHR_RANGER;
	}
	ch = &ds_characters[c];

	memset( pv, 0, sizeof( pv ) );
	memset( wl, 0, sizeof( wl ) );
	n = 0;
	for ( k = 0; k < 4 && ch->startCards[k] >= 0; k++ ) {
		if ( RLC_IS_PASSIVE( ch->startCards[k] ) ) {
			pv[ch->startCards[k] - RLC_PASSIVE_FIRST] = ( k == 0 && ch->startPassiveLevels > 0 ) ? ch->startPassiveLevels : 1;
		}
		else if ( ch->startCards[k] < RLC_NUM ) {
			cards[n++] = ch->startCards[k];
		}
	}
	if ( c == CHR_ORBB ) {
		for ( i = RLC_ALT_GAUNTLET; i <= RLC_ALT_CHAINGUN; i++ ) {
			if ( i != RLC_ALT_GRAPPLE_UNUSED ) {
				cards[n++] = i;
			}
		}
	}
	if ( ch->startWeapon > 0 && ch->startWeapon < DS_NUM_WEAPONS ) {
		wl[ch->startWeapon] = ch->startLevel > 0 ? ch->startLevel : 1;
	}

	RL_KVSetInt( &rl_run, "version", DS_RUN_VERSION );
	RL_KVSetInt( &rl_run, "character", c );
	RL_KVSet( &rl_run, "cards", DSM_Csv( cards, n ) );
	RL_KVSetInt( &rl_run, "startcards", n );
	RL_KVSet( &rl_run, "pv", DSM_Csv( pv, RLP_NUM ) );
	RL_KVSet( &rl_run, "wl", DSM_Csv( wl, DS_NUM_WEAPONS ) );
	for ( i = 0; i < DSU_NUM; i++ ) {
		RL_KVSetInt( &rl_run, ds_upgrades[i].key, RL_KVInt( &rl_meta, ds_upgrades[i].key ) );
	}

	RL_KVSetInt( &rl_meta, "last_char", c );
	RL_SaveMeta();
	Com_Printf( "DESCENT: character %s\n", ch->name );
}


/*
=======================================================================

HUB

=======================================================================
*/

typedef struct {
	menuframework_s	menu;
	rlButton_t		cont;
	rlButton_t		newrun;
	rlButton_t		upgrades;
	rlButton_t		collection;
	rlButton_t		records;
	rlButton_t		skills;			// ds-skills: SKILLS
	rlButton_t		back;
	rlButton_t		abandon;		// ds-economy: ABANDON DESCENT
	rlButton_t		practice;		// en-boss: BOSS PRACTICE
	rlButton_t		daily;			// en-daily: DAILY CHALLENGE
	int				mode;			// what CONTINUE does: 0 open the run, 1 end an old run
} dsHub_t;

static dsHub_t s_dsHub;

static int DSM_CollectionCount( int *total );

static void DSM_EndOldAction( qboolean result ) {
	if ( !result ) {
		return;
	}
	RL_LoadRun();
	RL_KVSetInt( &rl_run, "retired", 1 );
	DSM_Bank();
	UI_DS_SummaryMenu();
}

/*
=================
UI_DS_AbandonRun

ds-economy: ABANDON DESCENT (hub): the run ends as a loss, skulls and records banked
=================
*/
void UI_DS_AbandonRun( void ) {
	RL_LoadRun();
	if ( !RL_KVInt( &rl_run, "active" ) ) {
		return;
	}
	RL_KVSet( &rl_run, "cause", va( "Abandoned the descent on floor %i", RL_KVInt( &rl_run, "floor" ) + 1 ) );
	RL_KVSetInt( &rl_run, "abandoned", 1 );
	DSM_Bank();
	Com_Printf( "DESCENT: abandoned at floor %i, banked as a loss\n", RL_KVInt( &rl_run, "floor" ) );
	UI_DS_SummaryMenu();
}

static void DSM_EndOldDraw( void ) {
	UI_DrawProportionalString( 320, 230, "ITS SKULLS AND RECORDS ARE BANKED", UI_CENTER|UI_SMALLFONT, color_red );
}

static void DSM_HubEvent( void *ptr, int event ) {
	if ( event != QM_ACTIVATED ) {
		return;
	}
	switch ( ((menucommon_s *)ptr)->id ) {
	case ID_RL_CONTINUE:
		if ( s_dsHub.mode == 1 ) {
			UI_ConfirmMenu( "END THE OLD DESCENT?", DSM_EndOldDraw, DSM_EndOldAction );
		}
		else {
			RL_OpenRun();
		}
		break;
	case ID_RL_NEWRUN:
		RL_NewRunMenu();
		break;
	case ID_RL_UPGRADES:
		UI_DS_UpgradesMenu();
		break;
	case ID_RL_COLLECTION:
		UI_DS_CollectionMenu();
		break;
	case ID_RL_RECORDS:
		UI_DS_RecordsMenu();
		break;
	case ID_RL_SKILLS:
		UI_DS_SkillsMenu( -1 );		// ds-skills
		break;
	case ID_RL_BACK:
		UI_PopMenu();
		break;
	case ID_RL_ABANDON:
		UI_DS_AbandonMenu();		// ds-economy
		break;
	case ID_RL_DAILY:
		UI_DS_DailyMenu();		// en-daily
		break;
	case ID_RL_PRACTICE:
		UI_DS_BossPracMenu();		// en-boss
		break;
	}
}

static void DSM_HubRefresh( void ) {
	int		active;
	int		version;
	int		lives;
	int		floor;
	int		diff;
	int		found;
	int		total;
	int		c;

	DSM_ProcessRun();
	rl_hubDirty = qfalse;

	active = RL_KVInt( &rl_run, "active" );
	version = RL_KVInt( &rl_run, "version" );
	lives = RL_KVInt( &rl_run, "lives" );
	floor = RL_KVInt( &rl_run, "floor" );
	diff = RL_KVInt( &rl_run, "difficulty" );
	if ( diff < 1 || diff > 5 ) {
		diff = 3;
	}
	c = RL_KVInt( &rl_run, "character" );
	if ( c < 0 || c >= CHR_NUM ) {
		c = 0;
	}

	s_dsHub.mode = 0;
	s_dsHub.cont.icon = 0;
	if ( !active ) {
		s_dsHub.cont.bm.generic.flags |= QMF_GRAYED;
		Q_strncpyz( s_dsHub.cont.label, "CONTINUE", sizeof( s_dsHub.cont.label ) );
		Q_strncpyz( s_dsHub.cont.sub, "No descent in progress", sizeof( s_dsHub.cont.sub ) );
	}
	else if ( version < 2 ) {
		s_dsHub.mode = 1;
		s_dsHub.cont.bm.generic.flags &= ~QMF_GRAYED;
		Q_strncpyz( s_dsHub.cont.label, "END THE OLD DESCENT", sizeof( s_dsHub.cont.label ) );
		Com_sprintf( s_dsHub.cont.sub, sizeof( s_dsHub.cont.sub ), "From before 0.5, floor %i: bank its skulls and records", floor );
	}
	else {
		s_dsHub.cont.bm.generic.flags &= ~QMF_GRAYED;
		s_dsHub.cont.icon = dsm_charIcons[c];
		Q_strncpyz( s_dsHub.cont.label, "CONTINUE", sizeof( s_dsHub.cont.label ) );
		if ( RL_KVInt( &rl_run, "victory" ) ) {
			Com_sprintf( s_dsHub.cont.sub, sizeof( s_dsHub.cont.sub ), "VICTORY! The Abyss, or the end?  |  %s", rl_skillNames[diff - 1] );
		}
		else if ( RL_KVInt( &rl_run, "infloor" ) ) {
			Com_sprintf( s_dsHub.cont.sub, sizeof( s_dsHub.cont.sub ), "Floor %i restarts, no life lost  |  %i %s  |  %s",
				floor + 1, lives, lives == 1 ? "life" : "lives", ds_characters[c].name );
		}
		else {
			Com_sprintf( s_dsHub.cont.sub, sizeof( s_dsHub.cont.sub ), "Floor %i next  |  %i %s  |  %s  |  %s",
				floor + 1, lives, lives == 1 ? "life" : "lives", ds_characters[c].name, rl_skillNames[diff - 1] );
		}
	}
	if ( active ) {
		Q_strncpyz( s_dsHub.newrun.sub, "Abandons the current descent (nothing is banked)", sizeof( s_dsHub.newrun.sub ) );
	}
	else {
		Q_strncpyz( s_dsHub.newrun.sub, "Pick a difficulty and a character", sizeof( s_dsHub.newrun.sub ) );
	}
	found = DSM_CollectionCount( &total );
	Com_sprintf( s_dsHub.collection.sub, sizeof( s_dsHub.collection.sub ), "Weapons, evolutions, characters, secrets: %i of %i found", found, total );
	Q_strncpyz( s_dsHub.skills.sub, UI_DS_SkillsHubLine(), sizeof( s_dsHub.skills.sub ) );	// ds-skills
	Q_strncpyz( s_dsHub.daily.sub, UI_DS_DailyHubLine(), sizeof( s_dsHub.daily.sub ) );	// en-daily
	// ds-economy: ABANDON only for a running 0.5 descent, from the main menu (an in-game floor would save it again)
	if ( active && version >= 2 && !RL_InGame() ) {
		s_dsHub.abandon.bm.generic.flags &= ~QMF_GRAYED;
	}
	else {
		s_dsHub.abandon.bm.generic.flags |= QMF_GRAYED;
	}
}

static void DSM_HubDraw( void ) {
	int		ids[DSM_MAX_UNL];
	int		n;
	int		i;
	char	line[256];
	vec4_t	c;

	if ( rl_hubDirty ) {
		DSM_HubRefresh();
	}
	RL_Backdrop( !RL_InGame() );
	RL_Header( "DESCENT", "Four acts of six floors, a boss below each. Beat the fourth and the descent is won." );
	RL_DrawSkulls( DSM_Meta( "skulls" ) );
	Menu_Draw( &s_dsHub.menu );

	RL_Text( 320, 384, "Win a floor: pick 1 of 3 cards, level your weapons, open the chests you found.", 6, 10, rl_colDim, 1 );
	RL_Text( 320, 396, "Lose a floor: lose a life. Out of lives: the descent ends and your skulls are banked.", 6, 10, rl_colDim, 1 );

	n = DSM_UnlockList( "unl_new", ids );
	if ( n > 0 ) {
		line[0] = 0;
		for ( i = 0; i < n; i++ ) {
			Q_strcat( line, sizeof( line ), va( i ? ", %s" : "%s", DSM_UnlockName( ids[i] ) ) );
		}
		Vector4Copy( dsm_colGold, c );
		c[3] = 0.6f + 0.4f * RL_Pulse( 0.005f );
		RL_TextS( 320, 409, va( "NEW: %s", line ), 7, 11, c, 1 );
		RL_Text( 320, 422, "See them in the Collection, pick them in New Descent", 5, 8, rl_colDim, 1 );
	}
	RL_Footer( "MOUSE, SPACE / TAB, WASD OR GAMEPAD" );
}

/*
=================
UI_RL_HubMenu
=================
*/
void UI_RL_HubMenu( void ) {
	float	x;
	float	w;
	float	y;

	memset( &s_dsHub, 0, sizeof( s_dsHub ) );
	RL_SetupMenu( &s_dsHub.menu, DSM_HubDraw, RL_InGame() );

	w = 360;
	x = 320 - w / 2;
	y = 72;		// seven buttons (ds-skills added SKILLS, en-daily DAILY CHALLENGE): 40 high, 44 apart
	RL_InitButton( &s_dsHub.cont, ID_RL_CONTINUE, x, y, w, 40, "CONTINUE", DSM_HubEvent );
	y += 44;
	RL_InitButton( &s_dsHub.newrun, ID_RL_NEWRUN, x, y, w, 40, "NEW DESCENT", DSM_HubEvent );
	y += 44;
	RL_InitButton( &s_dsHub.daily, ID_RL_DAILY, x, y, w, 40, "DAILY CHALLENGE", DSM_HubEvent );	// en-daily
	y += 44;
	RL_InitButton( &s_dsHub.upgrades, ID_RL_UPGRADES, x, y, w, 40, "UPGRADES", DSM_HubEvent );
	Q_strncpyz( s_dsHub.upgrades.sub, "Spend skulls: 14 upgrades - body, cards, spoils, death", sizeof( s_dsHub.upgrades.sub ) );
	y += 44;
	RL_InitButton( &s_dsHub.skills, ID_RL_SKILLS, x, y, w, 40, "SKILLS", DSM_HubEvent );	// ds-skills
	y += 44;
	RL_InitButton( &s_dsHub.collection, ID_RL_COLLECTION, x, y, w, 40, "COLLECTION", DSM_HubEvent );
	y += 44;
	RL_InitButton( &s_dsHub.records, ID_RL_RECORDS, x, y, w, 40, "RECORDS", DSM_HubEvent );
	Q_strncpyz( s_dsHub.records.sub, "Deepest floors, lifetime totals, your last 30 descents", sizeof( s_dsHub.records.sub ) );
	RL_InitButton( &s_dsHub.back, ID_RL_BACK, 8 - rl_ext, 440, 120, 28, "BACK", DSM_HubEvent );
	s_dsHub.back.center = qtrue;
	RL_InitButton( &s_dsHub.abandon, ID_RL_ABANDON, 632 + rl_ext - 170, 440, 170, 28, "ABANDON DESCENT", DSM_HubEvent );
	UI_DS_SmallButton( &s_dsHub.abandon );
	RL_InitButton( &s_dsHub.practice, ID_RL_PRACTICE, 320 - 85, 440, 170, 28, "PRACTICE / REMATCH", DSM_HubEvent );	// en-boss (en-rematch: + REMATCH)
	UI_DS_SmallButton( &s_dsHub.practice );

	Menu_AddItem( &s_dsHub.menu, &s_dsHub.cont );
	Menu_AddItem( &s_dsHub.menu, &s_dsHub.newrun );
	Menu_AddItem( &s_dsHub.menu, &s_dsHub.daily );		// en-daily
	Menu_AddItem( &s_dsHub.menu, &s_dsHub.upgrades );
	Menu_AddItem( &s_dsHub.menu, &s_dsHub.skills );
	Menu_AddItem( &s_dsHub.menu, &s_dsHub.collection );
	Menu_AddItem( &s_dsHub.menu, &s_dsHub.records );
	Menu_AddItem( &s_dsHub.menu, &s_dsHub.back );
	Menu_AddItem( &s_dsHub.menu, &s_dsHub.abandon );
	Menu_AddItem( &s_dsHub.menu, &s_dsHub.practice );

	DSM_HubRefresh();

	if ( RL_InGame() ) {
		uis.menusp = 0;
	}
	UI_PushMenu( &s_dsHub.menu );
}


/*
=======================================================================

CHARACTER SELECT (after the difficulty)

=======================================================================
*/

typedef struct {
	menuframework_s	menu;
	rlButton_t		chars[CHR_NUM];
	rlButton_t		back;
	int				difficulty;
	char			msg[64];
	int				msgTime;
} dsChar_t;

static dsChar_t s_dsChar;

static int DSM_FocusedChar( void ) {
	menucommon_s	*item;

	item = (menucommon_s *)Menu_ItemAtCursor( &s_dsChar.menu );
	if ( item && item->id >= ID_RL_ITEM0 && item->id < ID_RL_ITEM0 + CHR_NUM ) {
		return item->id - ID_RL_ITEM0;
	}
	return -1;
}

static void DSM_CharCardDraw( void *self ) {
	rlButton_t		*b;
	menucommon_s	*g;
	float			x;
	float			y;
	float			w;
	float			h;
	float			ps;
	int				c;
	qboolean		focus;
	qboolean		owned;
	vec4_t			col;

	b = (rlButton_t *)self;
	g = &b->bm.generic;
	x = g->left;
	y = g->top;
	w = g->right - g->left;
	h = g->bottom - g->top;
	c = b->data;
	focus = ( Menu_ItemAtCursor( g->parent ) == self );
	owned = DSM_CharOwned( c );

	if ( focus ) {
		RL_Scale( col, owned ? rl_colOrange : rl_colDark, 0.5f, 1 );
		RL_Pic( x - 24, y - 24, w + 48, h + 48, rl_shGlow, col );
	}
	RL_Frame( x, y, w, h, focus ? rl_colPanelHi : rl_colPanel, focus ? rl_colOrange : rl_colLine );

	ps = w - 16;
	RL_Frame( x + 8, y + 8, ps, ps, rl_colBlack, NULL );
	if ( owned ) {
		RL_Pic( x + 8, y + 8, ps, ps, dsm_charIcons[c], NULL );
	}
	else {
		RL_Pic( x + 8, y + 8, ps, ps, dsm_charIcons[c], dsm_colSilhouette );
		RL_TextS( x + w / 2, y + 8 + ps / 2 - 10, c == CHR_ORBB ? "?" : "LOCKED", c == CHR_ORBB ? 16 : 7, c == CHR_ORBB ? 22 : 10, rl_colDim, 1 );
	}
	RL_TextS( x + w / 2, y + ps + 14, ( owned || c != CHR_ORBB ) ? ds_characters[c].name : "???", 8, 12,
		owned ? ( focus ? rl_colOrange : rl_colBone ) : rl_colDark, 1 );
	if ( c == DSM_Meta( "last_char" ) && owned ) {
		RL_Text( x + w / 2, y + h - 11, "LAST PLAYED", 4, 7, rl_colDim, 1 );
	}
}

static void DSM_CharStart( int c ) {
	int		diff;

	diff = s_dsChar.difficulty;
	dsm_pendingChar = c;
	RL_PopTop();	// this menu
	RL_PopTop();	// the difficulty menu
	RL_StartRun( diff );
}

static void DSM_CharEvent( void *ptr, int event ) {
	int		id;
	int		c;

	if ( event != QM_ACTIVATED ) {
		return;
	}
	id = ((menucommon_s *)ptr)->id;
	if ( id == ID_RL_BACK ) {
		UI_PopMenu();
		return;
	}
	c = id - ID_RL_ITEM0;
	if ( c < 0 || c >= CHR_NUM ) {
		return;
	}
	if ( !DSM_CharOwned( c ) ) {
		Com_sprintf( s_dsChar.msg, sizeof( s_dsChar.msg ), "%s IS LOCKED", c == CHR_ORBB ? "???" : ds_characters[c].name );
		s_dsChar.msgTime = uis.realtime;
		trap_S_StartLocalSound( menu_buzz_sound, CHAN_LOCAL_SOUND );
		return;
	}
	DSM_CharStart( c );
}

static void DSM_CharDetails( int c, float x, float y, float w, float h ) {
	const dsCharInfo_t	*ch;
	char				line[256];
	int					k;
	int					have;
	int					need;
	float				ty;
	float				f;
	qboolean			owned;
	qboolean			hidden;

	ch = &ds_characters[c];
	owned = DSM_CharOwned( c );
	hidden = ( c == CHR_ORBB && !owned );

	RL_Frame( x, y, w, h, rl_colPanel, rl_colLine );
	RL_Pic( x + 8, y + 8, h - 16, h - 16, dsm_charIcons[c], owned ? NULL : dsm_colSilhouette );
	x += h;
	w -= h + 8;
	UI_DrawProportionalString( (int)x, (int)y + 6, hidden ? "???" : ch->name, UI_LEFT|UI_SMALLFONT, owned ? rl_colOrange : rl_colDim );
	ty = y + 30;
	if ( hidden ) {
		ty = RL_TextWrap( x, ty, w, ds_orbbHint, 7, 11, rl_colDim, 3 );
		return;
	}
	ty = RL_TextWrap( x, ty, w, ch->desc, 7, 11, rl_colBone, 2 );
	ty += 3;

	line[0] = 0;
	if ( ch->startWeapon > 0 && ch->startWeapon < DS_NUM_WEAPONS ) {
		Q_strcat( line, sizeof( line ), va( "STARTS WITH %s LV%i", ds_weaponNames[ch->startWeapon], ch->startLevel > 0 ? ch->startLevel : 1 ) );
	}
	for ( k = 0; k < 4 && ch->startCards[k] >= 0; k++ ) {
		if ( ch->startCards[k] >= RLC_NUM ) {
			continue;
		}
		Q_strcat( line, sizeof( line ), line[0] ? "  +  " : "STARTS WITH " );
		if ( RLC_IS_PASSIVE( ch->startCards[k] ) && k == 0 && ch->startPassiveLevels > 1 ) {
			Q_strcat( line, sizeof( line ), va( "%s %i", bg_rlCardNames[ch->startCards[k]], ch->startPassiveLevels ) );
		}
		else {
			Q_strcat( line, sizeof( line ), bg_rlCardNames[ch->startCards[k]] );
		}
	}
	if ( c == CHR_ORBB ) {
		Q_strcat( line, sizeof( line ), line[0] ? "  +  EVERY ALT-FIRE" : "STARTS WITH EVERY ALT-FIRE" );
	}
	if ( line[0] ) {
		ty = RL_TextWrap( x, ty, w, line, 6, 10, rl_colDim, 2 );
	}
	line[0] = 0;
	if ( ch->healthDelta ) {
		Q_strcat( line, sizeof( line ), va( "%+i MAX HEALTH   ", ch->healthDelta ) );
	}
	if ( ch->speedPct ) {
		Q_strcat( line, sizeof( line ), va( "%+i%% MOVE SPEED", ch->speedPct ) );
	}
	if ( line[0] ) {
		RL_Text( x, ty, line, 6, 10, rl_colBad, 0 );
		ty += 13;
	}

	if ( !owned ) {
		ty += 2;
		RL_Text( x, ty, va( "UNLOCK: %s", ch->unlock ), 7, 11, dsm_colGold, 0 );
		need = DSM_CharProgress( c, &have );
		if ( need > 1 ) {
			if ( have > need ) {
				have = need;
			}
			f = (float)have / need;
			UI_FillRect( x, ty + 15, w * 0.6f, 6, dsm_colBarBack );
			UI_FillRect( x, ty + 15, w * 0.6f * f, 6, dsm_colGold );
			RL_Text( x + w * 0.6f + 8, ty + 14, va( "%i / %i", have, need ), 6, 9, rl_colDim, 0 );
		}
	}
}

static void DSM_CharDraw( void ) {
	int		c;
	float	t;
	vec4_t	col;

	RL_Backdrop( !RL_InGame() );
	RL_Header( "CHARACTER", va( "%s  |  their model, their starting kit, their weakness", rl_skillNames[s_dsChar.difficulty - 1] ) );
	RL_DrawSkulls( DSM_Meta( "skulls" ) );
	Menu_Draw( &s_dsChar.menu );

	c = DSM_FocusedChar();
	if ( c < 0 ) {
		c = DSM_Meta( "last_char" );
		if ( c < 0 || c >= CHR_NUM ) {
			c = 0;
		}
	}
	DSM_CharDetails( c, 320 - 230 - rl_ext * 0.5f, 324, 460 + rl_ext, 104 );

	if ( s_dsChar.msg[0] ) {
		t = ( uis.realtime - s_dsChar.msgTime ) / 2000.0f;
		if ( t < 1 ) {
			Vector4Copy( rl_colBad, col );
			col[3] = 1 - t * t;
			RL_TextS( 320, 432, s_dsChar.msg, 8, 12, col, 1 );
		}
	}
	RL_Footer( "MOUSE, SPACE / TAB, WASD OR GAMEPAD  |  ESC: BACK TO THE DIFFICULTY" );
}

void UI_DS_CharacterMenu( int difficulty ) {
	int		i;
	int		perRow;
	int		row;
	int		col;
	int		inRow;
	float	cw;
	float	ch;
	float	gap;
	float	x0;
	float	y0;
	float	span;
	int		last;

	memset( &s_dsChar, 0, sizeof( s_dsChar ) );
	if ( difficulty < 1 || difficulty > 5 ) {
		difficulty = 3;
	}
	s_dsChar.difficulty = difficulty;
	RL_SetupMenu( &s_dsChar.menu, DSM_CharDraw, RL_InGame() );
	DSM_ProcessRun();
	DSM_ClearAnnounced();

	gap = 10;
	span = 624 + 2 * rl_ext;
	cw = 92;
	ch = 118;
	perRow = ( span >= CHR_NUM * ( cw + gap ) ) ? CHR_NUM : 5;
	for ( i = 0; i < CHR_NUM; i++ ) {
		row = i / perRow;
		col = i % perRow;
		inRow = ( row == 0 ) ? ( CHR_NUM < perRow ? CHR_NUM : perRow ) : CHR_NUM - perRow;
		x0 = 320 - ( inRow * cw + ( inRow - 1 ) * gap ) / 2;
		y0 = ( perRow == CHR_NUM ) ? 120 : 72 + row * ( ch + 8 );
		RL_InitButton( &s_dsChar.chars[i], ID_RL_ITEM0 + i, x0 + col * ( cw + gap ), y0, cw, ch, ds_characters[i].name, DSM_CharEvent );
		s_dsChar.chars[i].bm.generic.ownerdraw = DSM_CharCardDraw;
		s_dsChar.chars[i].data = i;
		Menu_AddItem( &s_dsChar.menu, &s_dsChar.chars[i] );
	}
	RL_InitButton( &s_dsChar.back, ID_RL_BACK, 8 - rl_ext, 440, 120, 28, "BACK", DSM_CharEvent );
	s_dsChar.back.center = qtrue;
	Menu_AddItem( &s_dsChar.menu, &s_dsChar.back );

	UI_PushMenu( &s_dsChar.menu );
	last = DSM_Meta( "last_char" );
	if ( last < 0 || last >= CHR_NUM || !DSM_CharOwned( last ) ) {
		last = 0;
	}
	Menu_SetCursorToItem( &s_dsChar.menu, &s_dsChar.chars[last] );
}


/*
=======================================================================

UPGRADE TREE

=======================================================================
*/

typedef struct {
	menuframework_s	menu;
	rlButton_t		rows[DSU_NUM];
	rlButton_t		back;
	int				itemTier[DSU_NUM + 1];	// per menu item (cursor): its tier and row (ds-economy)
	int				itemRow[DSU_NUM + 1];
	int				numItems;
	char			msg[64];
	int				msgTime;
	qboolean		msgGood;
	float			colX[DS_UP_TIERS];
	float			colW;
} dsUp_t;

static dsUp_t s_dsUp;

static int DSM_TierRanks( int tier ) {
	int		u;
	int		n;

	n = 0;
	for ( u = 0; u < DSU_NUM; u++ ) {
		if ( ds_upgrades[u].tier == tier ) {
			n += DSM_Meta( ds_upgrades[u].key );
		}
	}
	return n;
}

static qboolean DSM_TierOpen( int tier ) {
	int		t;

	for ( t = 1; t <= tier && t < DS_UP_TIERS; t++ ) {
		if ( DSM_TierRanks( t - 1 ) < ds_upTierNeed[t] ) {
			return qfalse;
		}
	}
	return qtrue;
}

static qboolean DSM_Buy( int u, char *msg, int msgSize ) {
	const dsUpgradeInfo_t	*up;
	int						rank;
	int						cost;
	int						skulls;

	if ( u < 0 || u >= DSU_NUM ) {
		Com_sprintf( msg, msgSize, "NO SUCH UPGRADE" );
		return qfalse;
	}
	up = &ds_upgrades[u];
	RL_LoadMeta();		// fresh: the game banks into the same file
	rank = DSM_Meta( up->key );
	if ( !DSM_TierOpen( up->tier ) ) {
		Com_sprintf( msg, msgSize, "TIER %s IS LOCKED", ds_upTierNames[up->tier] );
		return qfalse;
	}
	if ( DS_UP_RETIRED( u ) ) {
		Com_sprintf( msg, msgSize, "%s IS RETIRED", up->name );
		return qfalse;
	}
	if ( rank >= up->maxRank ) {
		Com_sprintf( msg, msgSize, "%s IS MAXED", up->name );
		return qfalse;
	}
	cost = up->cost[rank];
	skulls = DSM_Meta( "skulls" );
	if ( skulls < cost ) {
		Com_sprintf( msg, msgSize, "NEED %i MORE SKULLS", cost - skulls );
		return qfalse;
	}
	RL_KVSetInt( &rl_meta, "skulls", skulls - cost );
	RL_KVSetInt( &rl_meta, up->key, rank + 1 );
	RL_SaveMeta();
	Com_sprintf( msg, msgSize, "%s RANK %i", up->name, rank + 1 );
	Com_Printf( "DESCENT: bought %s rank %i for %i skulls\n", up->key, rank + 1, cost );
	return qtrue;
}

qboolean UI_DS_BuyKey( const char *key, char *msg, int msgSize ) {
	int		u;

	for ( u = 0; u < DSU_NUM; u++ ) {
		if ( !Q_stricmp( key, ds_upgrades[u].key ) ) {
			return DSM_Buy( u, msg, msgSize );
		}
	}
	Com_sprintf( msg, msgSize, "unknown upgrade '%s'", key );
	return qfalse;
}

static const char *DSM_UpEffect( int u, int ranks );

static void DSM_UpgradeDraw( void *self ) {
	rlButton_t				*b;
	menucommon_s			*g;
	const dsUpgradeInfo_t	*up;
	float					x;
	float					y;
	float					w;
	float					h;
	float					px;
	int						rank;
	int						i;
	qboolean				focus;
	qboolean				open;
	vec4_t					c;

	b = (rlButton_t *)self;
	g = &b->bm.generic;
	up = &ds_upgrades[b->data];
	x = g->left;
	y = g->top;
	w = g->right - g->left;
	h = g->bottom - g->top;
	focus = ( Menu_ItemAtCursor( g->parent ) == self );
	rank = DSM_Meta( up->key );
	open = DSM_TierOpen( up->tier );

	RL_Frame( x, y, w, h, focus ? rl_colPanelHi : ( open ? rl_colPanel : rl_colBlack ), focus ? rl_colOrange : ( open ? rl_colLine : rl_colDark ) );
	if ( focus ) {
		UI_FillRect( x, y, 3, h, rl_colOrange );
	}
	if ( rank >= up->maxRank ) {
		Vector4Copy( rl_colGood, c );
	}
	else if ( open ) {
		Vector4Copy( rl_colBone, c );
	}
	else {
		Vector4Copy( rl_colDark, c );
	}
	RL_TextS( x + 8, y + 6, up->name, 7, 11, c, 0 );

	// rank pips
	px = x + 8;
	for ( i = 0; i < up->maxRank; i++ ) {
		if ( i < rank ) {
			RL_Frame( px, y + h - 15, 9, 9, rl_colOrange, rl_colOrange );
		}
		else {
			RL_Frame( px, y + h - 15, 9, 9, rl_colBlack, open ? rl_colLine : rl_colDark );
		}
		px += 12;
	}

	// ds-economy: what you have and what the next rank gives, on the row itself
	if ( rank >= up->maxRank ) {
		RL_Text( x + 8, y + 20, DSM_UpEffect( b->data, rank ), 5, 8, rl_colGood, 0 );
	}
	else {
		RL_Text( x + 8, y + 20, va( "%s%s", rank ? "next: " : "", DSM_UpEffect( b->data, rank + 1 ) ), 5, 8, open ? rl_colDim : rl_colDark, 0 );
	}
	if ( rank >= up->maxRank ) {
		RL_Text( x + w - 8, y + h - 16, "MAX", 7, 11, rl_colGood, 2 );
	}
	else if ( !open ) {
		RL_Text( x + w - 8, y + h - 16, "LOCKED", 5, 9, rl_colDark, 2 );
	}
	else {
		Vector4Copy( DSM_Meta( "skulls" ) >= up->cost[rank] ? rl_colOrange : rl_colDark, c );
		RL_Text( x + w - 8, y + h - 16, va( "%i", up->cost[rank] ), 7, 11, c, 2 );
	}
}

static void DSM_UpgradesEvent( void *ptr, int event ) {
	int		id;

	if ( event != QM_ACTIVATED ) {
		return;
	}
	id = ((menucommon_s *)ptr)->id;
	if ( id == ID_RL_BACK ) {
		UI_PopMenu();
		return;
	}
	if ( id >= ID_RL_ITEM0 && id < ID_RL_ITEM0 + DSU_NUM ) {
		s_dsUp.msgGood = DSM_Buy( id - ID_RL_ITEM0, s_dsUp.msg, sizeof( s_dsUp.msg ) );
		s_dsUp.msgTime = uis.realtime;
		trap_S_StartLocalSound( s_dsUp.msgGood ? rl_buySound : menu_buzz_sound, CHAN_LOCAL_SOUND );
	}
}

// the effect of n ranks, in words
static const char *DSM_UpEffect( int u, int ranks ) {
	const dsUpgradeInfo_t	*up;
	float					v;

	up = &ds_upgrades[u];
	v = ranks * up->perRank;
	switch ( u ) {
	case DSU_HEALTH:	return va( "+%i max health", (int)v );
	case DSU_ARMOR:		return va( "+%i armor at spawn", (int)v );
	case DSU_REROLL:	return va( "%i reroll%s per descent", (int)v, (int)v == 1 ? "" : "s" );
	case DSU_IRONHIDE:	return va( "-%i%% damage taken", (int)( v * 100 + 0.5f ) );
	case DSU_TEMPO:		return va( "fire %i%% faster", (int)( v * 100 + 0.5f ) );
	case DSU_TREASURE:	return va( "%i%% chance of one more chest reward", (int)( v * 100 + 0.5f ) );
	case DSU_VACUUM:	return va( "+%i%% vacuum drops", (int)( v * 100 + 0.5f ) );
	case DSU_SPEED:		return va( "+%i%% move speed", (int)( v * 100 + 0.5f ) );
	case DSU_DAMAGE:	return va( "+%i%% damage", (int)( v * 100 + 0.5f ) );
	case DSU_SKULLS:	return va( "+%i%% skulls", (int)( v * 100 + 0.5f ) );
	case DSU_MAGNET:	return va( "+%i%% gem pull radius", (int)( v * 100 + 0.5f ) );
	case DSU_GROWTH:	return va( "+%i%% gem xp", (int)( v * 100 + 0.5f ) );
	case DSU_LUCK:		return va( "+%i%% luck", (int)( v * 100 + 0.5f ) );
	case DSU_AREA:		return va( "+%i%% splash radius", (int)( v * 100 + 0.5f ) );
	case DSU_LASTBREATH:	return va( "+%i life on boss floors", (int)v );
	case DSU_STARTCARD:	return "a free ability at the start";
	case DSU_CARDS:		return "4 cards in every offer";
	case DSU_FIRSTPICK:	return "5 cards in an act's first offer";
	case DSU_ARSENAL:	return "weapons start at level 2";
	case DSU_QUARTER:	return "a chest at every act's start";
	case DSU_LIFE:		return "+1 life every descent";
	case DSU_REVIVAL:	return "one loss forgiven per descent";
	default:			return ranks ? "owned" : "not owned";
	}
}

static void DSM_UpgradesDraw( void ) {
	menucommon_s			*item;
	const dsUpgradeInfo_t	*up;
	vec4_t					c;
	float					t;
	float					x;
	float					y;
	float					w;
	int						tier;
	int						u;
	int						rank;
	int						have;
	qboolean				open;

	RL_Backdrop( !RL_InGame() );
	RL_Header( "UPGRADES", "Permanent, bought with skulls, applied from your next descent. Each tier opens the next." );
	RL_DrawSkulls( DSM_Meta( "skulls" ) );

	// tier headers and the links between them
	for ( tier = 0; tier < DS_UP_TIERS; tier++ ) {
		x = s_dsUp.colX[tier];
		w = s_dsUp.colW;
		open = DSM_TierOpen( tier );
		RL_Frame( x, 66, w, 30, open ? rl_colPanelHi : rl_colBlack, open ? rl_colOrange : rl_colDark );
		RL_TextS( x + 8, 70, va( "%s  %s", tier == 0 ? "I" : tier == 1 ? "II" : tier == 2 ? "III" : "IV", ds_upTierNames[tier] ), 8, 12, open ? rl_colOrange : rl_colDark, 0 );
		RL_Text( x + w - 8, 72, ds_upTierDescs[tier], 5, 8, open ? rl_colDim : rl_colDark, 2 );		// ds-economy
		have = DSM_TierRanks( tier );
		if ( open ) {
			RL_Text( x + 8, 84, va( "%i RANKS BOUGHT", have ), 5, 8, rl_colDim, 0 );
		}
		else {
			have = DSM_TierRanks( tier - 1 );
			RL_Text( x + 8, 84, va( "OPENS AT %i RANKS IN %s (%i)", ds_upTierNeed[tier], ds_upTierNames[tier - 1], have ), 5, 8, rl_colBad, 0 );
		}
		if ( tier > 0 ) {
			// a chevron from the tier before
			Vector4Copy( open ? rl_colOrange : rl_colDark, c );
			RL_Text( x - ( x - ( s_dsUp.colX[tier - 1] + w ) ) / 2, 75, ">", 8, 12, c, 1 );
		}
	}

	Menu_Draw( &s_dsUp.menu );
	UI_DS_EconTreeLine( s_dsUp.colX[0], 366, s_dsUp.colX[DS_UP_TIERS - 1] + s_dsUp.colW - s_dsUp.colX[0] );	// ds-economy

	// the focused upgrade
	item = (menucommon_s *)Menu_ItemAtCursor( &s_dsUp.menu );
	x = s_dsUp.colX[0];
	w = s_dsUp.colX[DS_UP_TIERS - 1] + s_dsUp.colW - x;
	y = 386;
	RL_Frame( x, y, w, 42, rl_colPanel, rl_colLine );
	if ( item && item->id >= ID_RL_ITEM0 && item->id < ID_RL_ITEM0 + DSU_NUM ) {
		u = item->id - ID_RL_ITEM0;
		up = &ds_upgrades[u];
		rank = DSM_Meta( up->key );
		RL_TextS( x + 10, y + 6, up->name, 8, 12, rl_colOrange, 0 );
		RL_Text( x + 10 + ( strlen( up->name ) + 2 ) * 8, y + 8, up->desc, 6, 10, rl_colBone, 0 );
		if ( rank > 0 ) {
			RL_Text( x + 10, y + 26, va( "NOW: %s", DSM_UpEffect( u, rank ) ), 6, 10, rl_colGood, 0 );
		}
		if ( rank < up->maxRank ) {
			RL_Text( x + w - 10, y + 26, va( "RANK %i: %s  -  %i SKULLS", rank + 1, DSM_UpEffect( u, rank + 1 ), up->cost[rank] ), 6, 10,
				DSM_TierOpen( up->tier ) ? rl_colDim : rl_colDark, 2 );
		}
		else {
			RL_Text( x + w - 10, y + 26, "MAXED", 6, 10, rl_colGood, 2 );
		}
	}
	else {
		RL_Text( x + 10, y + 16, "Point at an upgrade. Click or SPACE buys the next rank.", 6, 10, rl_colDim, 0 );
	}

	if ( s_dsUp.msg[0] ) {
		t = ( uis.realtime - s_dsUp.msgTime ) / 2000.0f;
		if ( t < 1 ) {
			Vector4Copy( s_dsUp.msgGood ? rl_colGood : rl_colBad, c );
			c[3] = 1 - t * t;
			RL_TextS( 320 + rl_ext * 0.3f, 444, s_dsUp.msg, 9, 13, c, 1 );
		}
	}
	RL_Footer( "W / S: UP AND DOWN   A / D: NEXT TIER   CLICK OR SPACE: BUY" );
}

static sfxHandle_t DSM_UpgradesKey( int key ) {
	int		cur;
	int		step;

	step = 0;
	switch ( key ) {
	case 'a':
	case K_LEFTARROW:
	case K_PAD0_DPAD_LEFT:
	case K_PAD0_LEFTSTICK_LEFT:
		step = -5;
		break;
	case 'd':
	case K_RIGHTARROW:
	case K_PAD0_DPAD_RIGHT:
	case K_PAD0_LEFTSTICK_RIGHT:
		step = 5;
		break;
	}
	if ( step ) {
		int		i;
		int		best;
		int		tier;

		cur = s_dsUp.menu.cursor;
		if ( cur < 0 || cur >= s_dsUp.numItems ) {
			return 0;
		}
		tier = s_dsUp.itemTier[cur] + ( step > 0 ? 1 : -1 );
		best = -1;
		for ( i = 0; i < s_dsUp.numItems; i++ ) {
			if ( s_dsUp.itemTier[i] == tier && ( best < 0 || abs( s_dsUp.itemRow[i] - s_dsUp.itemRow[cur] ) < abs( s_dsUp.itemRow[best] - s_dsUp.itemRow[cur] ) ) ) {
				best = i;
			}
		}
		if ( best >= 0 ) {
			Menu_SetCursor( &s_dsUp.menu, best );
			return menu_move_sound;
		}
		return 0;
	}
	return Menu_DefaultKey( &s_dsUp.menu, RL_KeyRemap( key ) );
}

void UI_DS_UpgradesMenu( void ) {
	int		u;
	int		tier;
	int		row[DS_UP_TIERS];
	float	gap;
	float	left;
	float	span;

	memset( &s_dsUp, 0, sizeof( s_dsUp ) );
	RL_SetupMenu( &s_dsUp.menu, DSM_UpgradesDraw, RL_InGame() );
	s_dsUp.menu.key = DSM_UpgradesKey;
	RL_LoadMeta();
	UI_DS_EconMigrate();		// ds-economy: refunds for retired upgrades (once)

	gap = 22;
	left = 8 - rl_ext;
	span = 624 + 2 * rl_ext;
	s_dsUp.colW = ( span - gap * ( DS_UP_TIERS - 1 ) ) / DS_UP_TIERS;
	for ( tier = 0; tier < DS_UP_TIERS; tier++ ) {
		s_dsUp.colX[tier] = left + tier * ( s_dsUp.colW + gap );
		row[tier] = 0;
	}
	// tier by tier (ds-economy: retired upgrades are not shown)
	s_dsUp.numItems = 0;
	for ( tier = 0; tier < DS_UP_TIERS; tier++ ) {
		for ( u = 0; u < DSU_NUM; u++ ) {
			if ( ds_upgrades[u].tier != tier || DS_UP_RETIRED( u ) ) {
				continue;
			}
			RL_InitButton( &s_dsUp.rows[u], ID_RL_ITEM0 + u, s_dsUp.colX[tier], 102 + row[tier] * 56, s_dsUp.colW, 48, ds_upgrades[u].name, DSM_UpgradesEvent );
			s_dsUp.rows[u].bm.generic.ownerdraw = DSM_UpgradeDraw;
			s_dsUp.rows[u].data = u;
			Menu_AddItem( &s_dsUp.menu, &s_dsUp.rows[u] );
			s_dsUp.itemTier[s_dsUp.numItems] = tier;
			s_dsUp.itemRow[s_dsUp.numItems] = row[tier];
			s_dsUp.numItems++;
			row[tier]++;
		}
	}
	s_dsUp.itemTier[s_dsUp.numItems] = -1;		// BACK
	RL_InitButton( &s_dsUp.back, ID_RL_BACK, 8 - rl_ext, 440, 120, 28, "BACK", DSM_UpgradesEvent );
	s_dsUp.back.center = qtrue;
	Menu_AddItem( &s_dsUp.menu, &s_dsUp.back );

	UI_PushMenu( &s_dsUp.menu );
}


/*
=======================================================================

COLLECTION

=======================================================================
*/

#define DSC_WEAPONS		0
#define DSC_EVOS		1
#define DSC_PASSIVES	2
#define DSC_PERKS		3
#define DSC_ABILITIES	4
#define DSC_CHARS		5
#define DSC_BOSSES		6
#define DSC_SECRETS		7
#define DSC_LEGENDS		8		// round 3 (ds-cards): legendary cards
#define DSC_BESTIARY	9		// round 4 (en-bestiary): enemy kinds met (q3_ui/ui_ds_bestiary.c)
#define DSC_NUM			10
#define DSC_MAXCELLS	40

static const char *dsc_tabNames[DSC_NUM] = { "WEAPONS", "EVOLUTIONS", "PASSIVES", "PERKS", "ABILITIES", "CHARACTERS", "BOSSES", "SECRETS", "LEGENDS", "BESTIARY" };

typedef struct {
	menuframework_s	menu;
	rlButton_t		back;
	int				tab;
	int				cell;
	int				cells[DSC_MAXCELLS];	// what each cell shows (weapon, evolution, card, character, boss, secret)
	int				num;
	int				cols;
	int				slot[DSC_MAXCELLS];		// en-screens: grid position of each cell (a group can start a new row)
	int				slots;					// grid positions used (rows = slots / cols, rounded up)
	float			gx;
	float			gy;
	float			cw;
	float			ch;
	int				lastMouseX;
	int				lastMouseY;
} dsColl_t;

static dsColl_t s_dsColl;

// the abilities tab: the movement abilities, the charged alt, then the alt-fires
static int DSC_Items( int tab, int *out ) {
	int		n;
	int		i;

	n = 0;
	switch ( tab ) {
	case DSC_WEAPONS:
		for ( i = 1; i < DS_NUM_WEAPONS; i++ ) {
			if ( DS_WEAPON_LEVELS( i ) ) {
				out[n++] = i;
			}
		}
		break;
	case DSC_EVOS:
		for ( i = 0; i < EVO_NUM; i++ ) {
			out[n++] = i;
		}
		break;
	case DSC_PASSIVES:
		for ( i = 0; i < RLP_NUM; i++ ) {
			out[n++] = RLC_PASSIVE_FIRST + i;
		}
		break;
	case DSC_PERKS:
		for ( i = 0; i < PERK_NUM && n < DSC_MAXCELLS; i++ ) {
			out[n++] = RLC_PERK_FIRST + i;
		}
		break;
	case DSC_ABILITIES:
		for ( i = RLC_DOUBLEJUMP; i <= RLC_ALT_CHAINGUN && n < DSC_MAXCELLS; i++ ) {
			if ( i != RLC_ALT_GRAPPLE_UNUSED ) {
				out[n++] = i;
			}
		}
		break;
	case DSC_CHARS:
		for ( i = 0; i < CHR_NUM; i++ ) {
			out[n++] = i;
		}
		break;
	case DSC_BOSSES:
		for ( i = 0; i < BOSS_NUM; i++ ) {
			out[n++] = i;
		}
		break;
	case DSC_SECRETS:
		for ( i = 0; i < RLS_NUM; i++ ) {
			out[n++] = i;
		}
		break;
	case DSC_LEGENDS:
		for ( i = 0; i < LG_NUM; i++ ) {
			out[n++] = RLC_LEGEND_FIRST + i;
		}
		break;
	case DSC_BESTIARY:
		n = UI_DS_BestiaryItems( out );
		break;
	}
	return n;
}

static qboolean DSC_Found( int tab, int item ) {
	int		wmax[DS_NUM_WEAPONS];

	switch ( tab ) {
	case DSC_WEAPONS:
		DSM_ReadCsv( &rl_meta, "wmax", wmax, DS_NUM_WEAPONS );
		return wmax[item] > 0;
	case DSC_EVOS:
		return ( DSM_Meta( "seen_evo" ) & ( 1 << item ) ) != 0;
	case DSC_PASSIVES:
	case DSC_PERKS:
	case DSC_ABILITIES:
	case DSC_LEGENDS:
		return DSM_BitGet( RL_KVGet( &rl_meta, "seen_cards" ), item );
	case DSC_CHARS:
		return DSM_CharOwned( item );
	case DSC_BOSSES:
		return ( DSM_Meta( "seen_boss" ) & ( 1 << item ) ) != 0;
	case DSC_BESTIARY:
		return UI_DS_BestiaryFound( item );
	case DSC_SECRETS:
		return DSM_SecretOwned( item );
	}
	return qfalse;
}

static int DSC_TabCount( int tab, int *total ) {
	int		items[DSC_MAXCELLS];
	int		n;
	int		i;
	int		found;

	n = DSC_Items( tab, items );
	found = 0;
	for ( i = 0; i < n; i++ ) {
		if ( DSC_Found( tab, items[i] ) ) {
			found++;
		}
	}
	*total = n;
	return found;
}

static int DSM_CollectionCount( int *total ) {
	int		t;
	int		n;
	int		found;

	found = 0;
	*total = 0;
	for ( t = 0; t < DSC_NUM; t++ ) {
		found += DSC_TabCount( t, &n );
		*total += n;
	}
	return found;
}

/*
en-screens: the grid position of every cell. The bestiary starts each group (archetypes, affixes, the champion
pack, mini-bosses, bosses) on a new row; the other tabs fill rows in order.
*/
static void DSC_Slots( void ) {
	int		i, s, g, prev;

	s = 0;
	prev = -1;
	for ( i = 0; i < s_dsColl.num; i++ ) {
		g = s_dsColl.tab == DSC_BESTIARY ? UI_DS_BestiaryGroup( s_dsColl.cells[i] ) : 0;
		if ( i > 0 && g != prev && ( s % s_dsColl.cols ) != 0 ) {
			s += s_dsColl.cols - ( s % s_dsColl.cols );
		}
		prev = g;
		s_dsColl.slot[i] = s++;
	}
	s_dsColl.slots = s;
}

static void DSC_Layout( void ) {
	float	left;
	float	right;
	float	w;

	s_dsColl.num = DSC_Items( s_dsColl.tab, s_dsColl.cells );
	if ( s_dsColl.cell >= s_dsColl.num ) {
		s_dsColl.cell = s_dsColl.num - 1;
	}
	if ( s_dsColl.cell < 0 ) {
		s_dsColl.cell = 0;
	}
	left = 8 - rl_ext;
	right = 632 + rl_ext - 190;		// the detail panel
	w = right - left - 10;
	s_dsColl.cw = ( s_dsColl.tab == DSC_CHARS || s_dsColl.tab == DSC_BOSSES ) ? 76 : 66;
	s_dsColl.ch = s_dsColl.cw + 16;
	s_dsColl.cols = (int)( w / ( s_dsColl.cw + 6 ) );
	if ( s_dsColl.cols < 1 ) {
		s_dsColl.cols = 1;
	}
	if ( s_dsColl.cols > s_dsColl.num ) {
		s_dsColl.cols = s_dsColl.num;
	}
	if ( s_dsColl.tab == DSC_BESTIARY && s_dsColl.cols > DSB_NUM_TYPES ) {
		s_dsColl.cols = DSB_NUM_TYPES;		// en-screens: the longest group fills one row
	}
	s_dsColl.gx = left + ( w - s_dsColl.cols * ( s_dsColl.cw + 6 ) ) / 2;
	s_dsColl.gy = 104;
	DSC_Slots();
	// many cells: shrink to fit above the footer
	while ( ( ( s_dsColl.slots + s_dsColl.cols - 1 ) / s_dsColl.cols ) * ( s_dsColl.ch + 6 ) > 330 && s_dsColl.cw > 40 ) {
		s_dsColl.cw -= 4;
		s_dsColl.ch = s_dsColl.cw + 16;
		s_dsColl.cols = (int)( w / ( s_dsColl.cw + 6 ) );
		if ( s_dsColl.cols > s_dsColl.num ) {
			s_dsColl.cols = s_dsColl.num;
		}
		if ( s_dsColl.tab == DSC_BESTIARY && s_dsColl.cols > DSB_NUM_TYPES ) {
			s_dsColl.cols = DSB_NUM_TYPES;
		}
		s_dsColl.gx = left + ( w - s_dsColl.cols * ( s_dsColl.cw + 6 ) ) / 2;
		DSC_Slots();
	}
}

// en-screens: W / S: the cell in the row above / below nearest to this one's column
static int DSC_VerticalCell( int cell, int dir ) {
	int		i, row, col, best, bestD, d;

	if ( cell < 0 || cell >= s_dsColl.num || s_dsColl.cols < 1 ) {
		return cell;
	}
	row = s_dsColl.slot[cell] / s_dsColl.cols + dir;
	col = s_dsColl.slot[cell] % s_dsColl.cols;
	best = cell;
	bestD = 9999;
	for ( i = 0; i < s_dsColl.num; i++ ) {
		if ( s_dsColl.slot[i] / s_dsColl.cols != row ) {
			continue;
		}
		d = s_dsColl.slot[i] % s_dsColl.cols - col;
		if ( d < 0 ) {
			d = -d;
		}
		if ( d < bestD ) {
			bestD = d;
			best = i;
		}
	}
	return best;
}

static void DSC_CellRect( int i, float *x, float *y ) {
	*x = s_dsColl.gx + ( s_dsColl.slot[i] % s_dsColl.cols ) * ( s_dsColl.cw + 6 );
	*y = s_dsColl.gy + ( s_dsColl.slot[i] / s_dsColl.cols ) * ( s_dsColl.ch + 6 );
}

static float DSC_TabW( void ) {
	return ( 624 + 2 * rl_ext ) / DSC_NUM;
}

static const char *DSC_Name( int tab, int item, qboolean found ) {
	switch ( tab ) {
	case DSC_WEAPONS:	return ds_weaponNames[item];
	case DSC_EVOS:		return found ? ds_evolutions[item].name : "???";
	case DSC_PASSIVES:
	case DSC_PERKS:
	case DSC_ABILITIES:	return bg_rlCardNames[item];
	case DSC_CHARS:		return ( found || item != CHR_ORBB ) ? ds_characters[item].name : "???";
	case DSC_BOSSES:	return found ? ds_bosses[item].name : "???";
	case DSC_SECRETS:	return found ? bg_rlCardNames[RLC_SECRET_FIRST + item] : "???";
	case DSC_LEGENDS:	return found ? bg_rlCardNames[item] : "???";
	case DSC_BESTIARY:	return UI_DS_BestiaryName( item, found );
	}
	return "";
}

static void DSC_DrawIcon( int tab, int item, qboolean found, float x, float y, float sz ) {
	vec4_t	c;
	int		w;

	switch ( tab ) {
	case DSC_WEAPONS:
		DSM_PicDim( x, y, sz, sz, DSM_WeaponIcon( item ), !found );
		break;
	case DSC_EVOS:
		w = ds_evolutions[item].weapon;
		if ( found ) {
			RL_Scale( c, dsm_colEvo, 0.6f, 1 );
			RL_Pic( x - sz * 0.2f, y - sz * 0.2f, sz * 1.4f, sz * 1.4f, rl_shGlow, c );
			RL_Pic( x, y, sz, sz, rl_shRing, dsm_colEvo );
			RL_Pic( x + sz * 0.15f, y + sz * 0.15f, sz * 0.7f, sz * 0.7f, DSM_WeaponIcon( w ), NULL );
		}
		else {
			DSM_PicDim( x + sz * 0.15f, y + sz * 0.15f, sz * 0.7f, sz * 0.7f, DSM_WeaponIcon( w ), qtrue );
			RL_TextS( x + sz / 2, y + sz / 2 - 7, "?", 10, 14, rl_colDim, 1 );
		}
		break;
	case DSC_PASSIVES:
		DSM_Tile( x, y, sz, ds_passiveShort[item - RLC_PASSIVE_FIRST], dsm_passiveColors[item - RLC_PASSIVE_FIRST], found );
		break;
	case DSC_PERKS:
		w = bg_perkCategory[item - RLC_PERK_FIRST];
		if ( w < 0 || w > 3 ) {
			w = 0;
		}
		DSM_Tile( x, y, sz, DSM_Initials( bg_rlCardNames[item] ), dsm_perkColors[w], found );
		break;
	case DSC_ABILITIES:
		if ( RLC_IS_ALT( item ) ) {
			RL_Frame( x, y, sz, sz, found ? rl_colPanelHi : dsm_colSilhouette, found ? rl_colLine : rl_colDark );
			DSM_PicDim( x + sz * 0.2f, y + sz * 0.12f, sz * 0.6f, sz * 0.6f, DSM_WeaponIcon( item - RLC_ALT_FIRST + 1 ), !found );
			RL_Text( x + sz / 2, y + sz * 0.75f, "ALT", sz * 0.12f, sz * 0.17f, found ? rl_colOrange : rl_colDark, 1 );
		}
		else {
			DSM_Tile( x, y, sz, bg_rlCardShort[item], dsm_perkColors[2], found );
		}
		break;
	case DSC_CHARS:
		RL_Frame( x, y, sz, sz, rl_colBlack, NULL );
		RL_Pic( x, y, sz, sz, dsm_charIcons[item], found ? NULL : dsm_colSilhouette );
		break;
	case DSC_BOSSES:
		RL_Frame( x, y, sz, sz, rl_colBlack, NULL );
		RL_Pic( x, y, sz, sz, dsm_bossIcons[item], found ? NULL : dsm_colSilhouette );
		if ( !found ) {
			RL_TextS( x + sz / 2, y + sz / 2 - 8, "?", 12, 16, rl_colDim, 1 );
		}
		break;
	case DSC_SECRETS:
		DSM_Tile( x, y, sz, found ? DSM_Initials( bg_rlCardNames[RLC_SECRET_FIRST + item] ) : "?", dsm_colGold, found );
		break;
	case DSC_LEGENDS:
		if ( found ) {
			RL_Scale( c, dsm_colLegend, 0.55f, 1 );
			RL_Pic( x - sz * 0.15f, y - sz * 0.15f, sz * 1.3f, sz * 1.3f, rl_shGlow, c );
		}
		DSM_Tile( x, y, sz, found ? bg_rlCardShort[item] : "?", dsm_colLegend, found );
		break;
	case DSC_BESTIARY:
		UI_DS_BestiaryIcon( item, found, x, y, sz );
		break;
	}
}

static void DSC_Details( float x, float y, float w, float h ) {
	char		line[160];
	int			tab;
	int			item;
	int			wmax[DS_NUM_WEAPONS];
	int			lv;
	int			e;
	int			have;
	int			need;
	float		ty;
	qboolean	found;

	RL_Frame( x, y, w, h, rl_colPanel, rl_colLine );
	if ( s_dsColl.cell < 0 || s_dsColl.cell >= s_dsColl.num ) {
		return;
	}
	tab = s_dsColl.tab;
	item = s_dsColl.cells[s_dsColl.cell];
	found = DSC_Found( tab, item );

	DSC_DrawIcon( tab, item, found, x + w / 2 - 32, y + 10, 64 );
	ty = RL_TextWrap( x + 8, y + 84, w - 16, DSC_Name( tab, item, found ), 8, 12, found ? rl_colOrange : rl_colDim, 2 );
	ty += 4;

	switch ( tab ) {
	case DSC_WEAPONS:
		DSM_ReadCsv( &rl_meta, "wmax", wmax, DS_NUM_WEAPONS );
		RL_Text( x + 8, ty, wmax[item] > 0 ? va( "HIGHEST LEVEL %i", wmax[item] ) : "NEVER LEVELLED", 6, 10, wmax[item] >= DS_MAX_WLEVEL ? dsm_colGold : rl_colBone, 0 );
		ty += 14;
		for ( lv = 2; lv <= DS_MAX_WLEVEL && ty < y + h - 10; lv++ ) {
			ty = RL_TextWrap( x + 8, ty, w - 16, va( "%i: %s", lv, ds_weaponLevelDesc[item][lv] ), 5, 8, lv <= wmax[item] ? rl_colBone : rl_colDark, 2 );
		}
		e = BG_DS_EvolutionForWeapon( item );
		if ( e >= 0 && ty < y + h - 20 ) {
			ty += 4;
			RL_Text( x + 8, ty, va( "EVOLVES INTO %s", ( DSM_Meta( "seen_evo" ) & ( 1 << e ) ) ? ds_evolutions[e].name : "???" ), 5, 8, dsm_colEvo, 0 );
		}
		break;
	case DSC_EVOS:
		if ( found ) {
			ty = RL_TextWrap( x + 8, ty, w - 16, ds_evolutions[item].desc, 6, 10, rl_colBone, 6 );
			ty += 6;
		}
		Com_sprintf( line, sizeof( line ), "%s AT LEVEL 8 + %s", ds_weaponNames[ds_evolutions[item].weapon],
			found || item != EVO_ARMAGEDDON ? bg_rlCardNames[ds_evolutions[item].keyCard] : "???" );
		if ( item == EVO_ARMAGEDDON ) {
			Q_strncpyz( line, found ? "HELLFIRE + CLUSTER BOMB + the UNION secret card" : "Two evolutions, one secret.", sizeof( line ) );
		}
		ty = RL_TextWrap( x + 8, ty, w - 16, line, 5, 8, rl_colDim, 4 );
		ty += 4;
		RL_TextWrap( x + 8, ty, w - 16, "The next chest you open evolves it.", 5, 8, rl_colDark, 2 );
		break;
	case DSC_PASSIVES:
		ty = RL_TextWrap( x + 8, ty, w - 16, va( "%s per level, up to 5 levels.", ds_passiveDescs[item - RLC_PASSIVE_FIRST] ), 6, 10, rl_colBone, 4 );
		ty += 4;
		RL_TextWrap( x + 8, ty, w - 16, found ? "Taken in a descent." : "Never taken yet.", 5, 8, found ? rl_colGood : rl_colDark, 2 );
		break;
	case DSC_PERKS:
	case DSC_ABILITIES:
		ty = RL_TextWrap( x + 8, ty, w - 16, bg_rlCardDescs[item], 6, 10, rl_colBone, 8 );
		ty += 4;
		RL_TextWrap( x + 8, ty, w - 16, found ? "Taken in a descent." : "Never taken yet.", 5, 8, found ? rl_colGood : rl_colDark, 2 );
		break;
	case DSC_CHARS:
		if ( !found && item == CHR_ORBB ) {
			RL_TextWrap( x + 8, ty, w - 16, ds_orbbHint, 6, 10, rl_colDim, 6 );
			break;
		}
		ty = RL_TextWrap( x + 8, ty, w - 16, ds_characters[item].desc, 6, 10, rl_colBone, 4 );
		ty += 4;
		if ( found ) {
			RL_Text( x + 8, ty, "UNLOCKED", 6, 10, rl_colGood, 0 );
		}
		else {
			ty = RL_TextWrap( x + 8, ty, w - 16, va( "UNLOCK: %s", ds_characters[item].unlock ), 6, 10, dsm_colGold, 3 );
			need = DSM_CharProgress( item, &have );
			if ( need > 1 ) {
				RL_Text( x + 8, ty + 2, va( "%i / %i", have < need ? have : need, need ), 6, 10, rl_colDim, 0 );
			}
		}
		break;
	case DSC_BESTIARY:
		UI_DS_BestiaryDetails( item, found, x + 8, ty, w - 16, y + h - 6 );
		break;
	case DSC_BOSSES:
		RL_Text( x + 8, ty, found ? ds_bosses[item].title : "Not met yet.", 6, 10, rl_colBone, 0 );
		ty += 14;
		RL_Text( x + 8, ty, va( "THE BOSS OF ACT %i", item + 1 ), 5, 8, rl_colDim, 0 );
		ty += 12;
		RL_Text( x + 8, ty, va( "DEFEATED %i TIME%s", DSM_Meta( va( "cnt_boss%i", item ) ), DSM_Meta( va( "cnt_boss%i", item ) ) == 1 ? "" : "S" ), 6, 10,
			found ? rl_colGood : rl_colDark, 0 );
		break;
	case DSC_LEGENDS:
		if ( found ) {
			ty = RL_TextWrap( x + 8, ty, w - 16, bg_rlCardDescs[item], 6, 10, rl_colBone, 6 );
			ty += 4;
			RL_TextWrap( x + 8, ty, w - 16, "Taken in a descent.", 5, 8, rl_colGood, 2 );
		}
		else {
			ty = RL_TextWrap( x + 8, ty, w - 16, "A legendary card. Rare in any offer, likelier deeper down, from elites and bosses, and with luck.", 6, 10, dsm_colLegend, 6 );
		}
		break;
	case DSC_SECRETS:
		if ( found ) {
			ty = RL_TextWrap( x + 8, ty, w - 16, bg_rlCardDescs[RLC_SECRET_FIRST + item], 6, 10, rl_colBone, 6 );
			ty += 4;
			ty = RL_TextWrap( x + 8, ty, w - 16, va( "FOUND: %s", ds_secretConds[item] ), 5, 8, rl_colGood, 3 );
			RL_TextWrap( x + 8, ty + 2, w - 16, "Now in the card pool.", 5, 8, rl_colDim, 2 );
		}
		else {
			ty = RL_TextWrap( x + 8, ty, w - 16, ds_secretHints[item], 7, 11, dsm_colGold, 4 );
		}
		break;
	}
}

static void DSC_SetTab( int tab ) {
	s_dsColl.tab = ( tab + DSC_NUM ) % DSC_NUM;
	s_dsColl.cell = 0;
	DSC_Layout();
}

static void DSM_CollectionDraw( void ) {
	float	x;
	float	y;
	float	tw;
	float	sz;
	int		t;
	int		i;
	int		n;
	int		found;
	int		total;
	qboolean	hit;
	qboolean	f;
	vec4_t	c;

	DSC_Layout();
	RL_Backdrop( !RL_InGame() );
	found = DSM_CollectionCount( &total );
	RL_Header( "COLLECTION", va( "%i of %i found", found, total ) );
	RL_DrawSkulls( DSM_Meta( "skulls" ) );

	// tabs
	tw = DSC_TabW();
	for ( t = 0; t < DSC_NUM; t++ ) {
		x = 8 - rl_ext + t * tw;
		hit = UI_CursorInRect( (int)x, 66, (int)tw - 4, 28 );
		RL_Frame( x, 66, tw - 4, 28, t == s_dsColl.tab ? rl_colPanelHi : rl_colPanel,
			t == s_dsColl.tab ? rl_colOrange : ( hit ? rl_colBone : rl_colLine ) );
		RL_Text( x + ( tw - 4 ) / 2, 70, dsc_tabNames[t], ( tw - 8 ) / strlen( dsc_tabNames[t] ) < 6 ? ( tw - 8 ) / strlen( dsc_tabNames[t] ) : 6, 10,
			t == s_dsColl.tab ? rl_colOrange : rl_colBone, 1 );		// (10 tabs: long names shrink on 4:3)
		i = DSC_TabCount( t, &n );
		RL_Text( x + ( tw - 4 ) / 2, 83, va( "%i/%i", i, n ), 5, 8, i == n ? dsm_colGold : rl_colDim, 1 );
	}

	// mouse hover picks the cell (only when the mouse moved: the keys keep theirs)
	if ( uis.cursorx != s_dsColl.lastMouseX || uis.cursory != s_dsColl.lastMouseY ) {
		s_dsColl.lastMouseX = uis.cursorx;
		s_dsColl.lastMouseY = uis.cursory;
		for ( i = 0; i < s_dsColl.num; i++ ) {
			DSC_CellRect( i, &x, &y );
			if ( UI_CursorInRect( (int)x, (int)y, (int)s_dsColl.cw, (int)s_dsColl.ch ) ) {
				s_dsColl.cell = i;
			}
		}
	}

	// cells
	for ( i = 0; i < s_dsColl.num; i++ ) {
		DSC_CellRect( i, &x, &y );
		f = DSC_Found( s_dsColl.tab, s_dsColl.cells[i] );
		RL_Frame( x, y, s_dsColl.cw, s_dsColl.ch, i == s_dsColl.cell ? rl_colPanelHi : rl_colPanel,
			i == s_dsColl.cell ? rl_colOrange : rl_colLine );
		sz = s_dsColl.cw - 16;
		DSC_DrawIcon( s_dsColl.tab, s_dsColl.cells[i], f, x + 8, y + 5, sz );
		Vector4Copy( f ? rl_colBone : rl_colDark, c );
		// en-screens: small tiles (the bestiary's 5 group rows) get narrower letters so a 10-letter word
		// ("ARCH-DEMON") isn't cut in two
		tw = ( s_dsColl.cw - 6 ) / 10.0f;
		RL_TextWrap( x + 3, y + sz + 8, s_dsColl.cw - 6, DSC_Name( s_dsColl.tab, s_dsColl.cells[i], f ), tw < 4 ? tw : 4, 6, c, 2 );
	}

	DSC_Details( 632 + rl_ext - 186, 104, 186, 330 );
	Menu_Draw( &s_dsColl.menu );
	RL_Footer( "TAB / SHIFT-TAB OR SHOULDER BUTTONS: NEXT LIST   WASD OR MOUSE: PICK   ESC: BACK" );
}

static sfxHandle_t DSM_CollectionKey( int key ) {
	float	x;
	float	tw;
	int		t;
	int		i;
	int		cell;

	cell = s_dsColl.cell;
	switch ( key ) {
	case K_TAB:
		DSC_SetTab( s_dsColl.tab + ( trap_Key_IsDown( K_SHIFT ) ? -1 : 1 ) );
		return menu_move_sound;
	case K_PAD0_RIGHTSHOULDER:
	case 'e':
		DSC_SetTab( s_dsColl.tab + 1 );
		return menu_move_sound;
	case K_PAD0_LEFTSHOULDER:
	case 'q':
		DSC_SetTab( s_dsColl.tab - 1 );
		return menu_move_sound;
	case 'a':
	case K_LEFTARROW:
	case K_PAD0_DPAD_LEFT:
	case K_PAD0_LEFTSTICK_LEFT:
	case K_MWHEELUP:
		cell--;
		break;
	case 'd':
	case K_RIGHTARROW:
	case K_PAD0_DPAD_RIGHT:
	case K_PAD0_LEFTSTICK_RIGHT:
	case K_MWHEELDOWN:
		cell++;
		break;
	case 'w':
	case K_UPARROW:
	case K_PAD0_DPAD_UP:
	case K_PAD0_LEFTSTICK_UP:
		cell = DSC_VerticalCell( cell, -1 );
		break;
	case 's':
	case K_DOWNARROW:
	case K_PAD0_DPAD_DOWN:
	case K_PAD0_LEFTSTICK_DOWN:
		cell = DSC_VerticalCell( cell, 1 );
		break;
	case K_MOUSE1:
		tw = DSC_TabW();
		for ( t = 0; t < DSC_NUM; t++ ) {
			x = 8 - rl_ext + t * tw;
			if ( UI_CursorInRect( (int)x, 66, (int)tw - 4, 28 ) ) {
				DSC_SetTab( t );
				return menu_move_sound;
			}
		}
		for ( i = 0; i < s_dsColl.num; i++ ) {
			float	cx;
			float	cy;

			DSC_CellRect( i, &cx, &cy );
			if ( UI_CursorInRect( (int)cx, (int)cy, (int)s_dsColl.cw, (int)s_dsColl.ch ) ) {
				s_dsColl.cell = i;
				return menu_move_sound;
			}
		}
		return Menu_DefaultKey( &s_dsColl.menu, key );
	case K_ESCAPE:
	case K_MOUSE2:
	case K_PAD0_B:
	case K_PAD0_BACK:
		UI_PopMenu();
		return menu_out_sound;
	default:
		return Menu_DefaultKey( &s_dsColl.menu, RL_KeyRemap( key ) );
	}
	if ( cell >= 0 && cell < s_dsColl.num && cell != s_dsColl.cell ) {
		s_dsColl.cell = cell;
		return menu_move_sound;
	}
	return 0;
}

static void DSM_CollectionEvent( void *ptr, int event ) {
	if ( event == QM_ACTIVATED ) {
		UI_PopMenu();
	}
}

void UI_DS_CollectionMenu( void ) {
	memset( &s_dsColl, 0, sizeof( s_dsColl ) );
	RL_SetupMenu( &s_dsColl.menu, DSM_CollectionDraw, RL_InGame() );
	s_dsColl.menu.key = DSM_CollectionKey;
	DSM_ProcessRun();
	DSM_ClearAnnounced();
	s_dsColl.lastMouseX = uis.cursorx;
	s_dsColl.lastMouseY = uis.cursory;

	RL_InitButton( &s_dsColl.back, ID_RL_BACK, 8 - rl_ext, 440, 120, 28, "BACK", DSM_CollectionEvent );
	s_dsColl.back.center = qtrue;
	Menu_AddItem( &s_dsColl.menu, &s_dsColl.back );
	DSC_SetTab( 0 );
	UI_PushMenu( &s_dsColl.menu );
}


/*
=======================================================================

RUN SUMMARY (run over, victory)

=======================================================================
*/

typedef struct {
	menuframework_s	menu;
	rlButton_t		cont;
	rlButton_t		abyss;
	qboolean		ingame;
	qboolean		victory;		// the act 4 boss fell: INTO THE ABYSS or END
	int				order[DS_NUM_WEAPONS];
	int				numRows;
	int				dmg[DS_NUM_WEAPONS];
	int				wkills[DS_NUM_WEAPONS];
	int				wtime[DS_NUM_WEAPONS];
	int				wl[DS_NUM_WEAPONS];
	int				pv[RLP_NUM];
	int				st[DSST_NUM];
	int				evo;
	int				popups[DSM_MAX_UNL];
	int				numPopups;
	int				popup;			// the unlock popup showing (numPopups = done)
	int				popupTime;
	int				runUnl[DSM_MAX_UNL];
	int				numRunUnl;
} dsSum_t;

static dsSum_t s_dsSum;

static qboolean DSM_WeaponEvolved( int w, int evo ) {
	int		e;

	e = BG_DS_EvolutionForWeapon( w );
	if ( e >= 0 && ( evo & ( 1 << e ) ) ) {
		return qtrue;
	}
	return ( w == ds_evolutions[EVO_ARMAGEDDON].weapon && ( evo & ( 1 << EVO_ARMAGEDDON ) ) );
}

static void DSM_SumLoad( void ) {
	int		i;
	int		j;
	int		t;
	int		n;
	int		seed;

	DSM_ReadCsv( &rl_run, "dmg", s_dsSum.dmg, DS_NUM_WEAPONS );
	DSM_ReadCsv( &rl_run, "wkills", s_dsSum.wkills, DS_NUM_WEAPONS );
	DSM_ReadCsv( &rl_run, "wtime", s_dsSum.wtime, DS_NUM_WEAPONS );
	DSM_ReadCsv( &rl_run, "wl", s_dsSum.wl, DS_NUM_WEAPONS );
	DSM_ReadCsv( &rl_run, "pv", s_dsSum.pv, RLP_NUM );
	DSM_ReadCsv( &rl_run, "st", s_dsSum.st, DSST_NUM );
	s_dsSum.evo = RL_KVInt( &rl_run, "evo" );

	n = 0;
	for ( i = 1; i < DS_NUM_WEAPONS; i++ ) {
		if ( s_dsSum.dmg[i] > 0 || s_dsSum.wkills[i] > 0 || s_dsSum.wtime[i] >= 1000 ) {
			s_dsSum.order[n++] = i;
		}
	}
	// most damage first
	for ( i = 1; i < n; i++ ) {
		for ( j = i; j > 0 && s_dsSum.dmg[s_dsSum.order[j]] > s_dsSum.dmg[s_dsSum.order[j - 1]]; j-- ) {
			t = s_dsSum.order[j];
			s_dsSum.order[j] = s_dsSum.order[j - 1];
			s_dsSum.order[j - 1] = t;
		}
	}
	s_dsSum.numRows = n;

	s_dsSum.numPopups = DSM_UnlockList( "unl_new", s_dsSum.popups );
	s_dsSum.popup = 0;
	s_dsSum.popupTime = uis.realtime;
	seed = RL_KVInt( &rl_run, "seed" );
	s_dsSum.numRunUnl = 0;
	if ( seed && DSM_Meta( "unl_seed" ) == seed ) {
		s_dsSum.numRunUnl = DSM_UnlockList( "unl_run", s_dsSum.runUnl );
	}
	if ( s_dsSum.numPopups > 0 ) {
		trap_S_StartLocalSound( dsm_unlockSound, CHAN_LOCAL_SOUND );
	}
}

static void DSM_SumLeave( void ) {
	if ( s_dsSum.ingame ) {
		RL_RegisterCvars();
		trap_Cvar_Set( "ui_rlOpenHub", "1" );
		UI_ForceMenuOff();
		trap_Cmd_ExecuteText( EXEC_APPEND, "disconnect\n" );
		return;
	}
	UI_PopMenu();
	if ( !uis.menusp ) {
		UI_RL_HubMenu();
	}
	rl_hubDirty = qtrue;
}

static void DSM_EndRunAction( qboolean result ) {
	if ( !result ) {
		return;
	}
	RL_LoadRun();
	DSM_Bank();
	RL_PopTop();		// the victory screen
	UI_DS_SummaryMenu();
}

static void DSM_EndRunDraw( void ) {
	UI_DrawProportionalString( 320, 230, "YOUR SKULLS ARE BANKED", UI_CENTER|UI_SMALLFONT, color_red );
}

static void DSM_SumEvent( void *ptr, int event ) {
	if ( event != QM_ACTIVATED ) {
		return;
	}
	if ( s_dsSum.popup < s_dsSum.numPopups ) {
		return;		// the key closes the popup first (DSM_SumKey)
	}
	switch ( ((menucommon_s *)ptr)->id ) {
	case ID_RL_ABYSS:
		RL_LoadRun();
		RL_KVSetInt( &rl_run, "victory", 0 );
		RL_SaveRun();
		RL_PopTop();
		if ( s_dsSum.ingame ) {
			uis.menusp = 0;
		}
		RL_OpenRun();
		break;
	case ID_RL_CONTINUE:
		if ( s_dsSum.victory ) {
			UI_ConfirmMenu( "END THE DESCENT HERE?", DSM_EndRunDraw, DSM_EndRunAction );
			return;
		}
		DSM_SumLeave();
		break;
	}
}

static sfxHandle_t DSM_SumKey( int key ) {
	int		k;

	k = RL_KeyRemap( key );
	if ( s_dsSum.popup < s_dsSum.numPopups ) {
		if ( uis.realtime - s_dsSum.popupTime < 400 ) {
			return 0;		// don't skip a popup by accident
		}
		if ( k == K_ENTER || k == K_ESCAPE || key == K_MOUSE1 || key == K_MOUSE2 ) {
			s_dsSum.popup++;
			s_dsSum.popupTime = uis.realtime;
			if ( s_dsSum.popup >= s_dsSum.numPopups ) {
				DSM_ClearAnnounced();
			}
			else {
				trap_S_StartLocalSound( dsm_unlockSound, CHAN_LOCAL_SOUND );
			}
			return menu_move_sound;
		}
		return 0;
	}
	if ( ( k == K_ESCAPE || key == K_MOUSE2 ) && !s_dsSum.victory ) {
		DSM_SumLeave();
		return menu_out_sound;
	}
	if ( k == K_ESCAPE || key == K_MOUSE2 ) {
		return menu_buzz_sound;		// victory: choose
	}
	return Menu_DefaultKey( &s_dsSum.menu, k );
}

static void DSM_DrawUnlockPopup( void ) {
	int		id;
	float	t;
	float	x;
	float	y;
	float	w;
	float	h;
	vec4_t	c;
	const char	*kind;
	const char	*cond;

	id = s_dsSum.popups[s_dsSum.popup];
	t = ( uis.realtime - s_dsSum.popupTime ) / 250.0f;
	if ( t > 1 ) {
		t = 1;
	}
	c[0] = c[1] = c[2] = 0;
	c[3] = 0.75f * t;
	UI_FillRect( -rl_ext - 100, 0, 840 + 2 * rl_ext, 480, c );

	w = 300;
	h = 190;
	x = 320 - w / 2;
	y = 120 + ( 1 - t ) * 30;
	RL_Scale( c, dsm_colGold, 0.5f + 0.2f * RL_Pulse( 0.004f ), 1 );
	RL_Pic( x - 60, y - 60, w + 120, h + 120, rl_shGlow, c );
	RL_Frame( x, y, w, h, rl_colPanelHi, dsm_colGold );

	if ( id < CHR_NUM ) {
		kind = "NEW CHARACTER";
		cond = id == CHR_ORBB ? "The eye has opened." : va( "%s.", ds_characters[id].unlock );
		RL_Pic( x + w / 2 - 40, y + 34, 80, 80, dsm_charIcons[id], NULL );
	}
	else {
		kind = "SECRET CARD FOUND";
		cond = ds_secretConds[id - DSM_UNL_SECRET];
		DSM_Tile( x + w / 2 - 36, y + 38, 72, DSM_Initials( bg_rlCardNames[RLC_SECRET_FIRST + id - DSM_UNL_SECRET] ), dsm_colGold, qtrue );
	}
	RL_TextS( x + w / 2, y + 10, kind, 8, 12, dsm_colGold, 1 );
	UI_DrawProportionalString( (int)( x + w / 2 ), (int)( y + 122 ), DSM_UnlockName( id ), UI_CENTER|UI_SMALLFONT, rl_colBone );
	RL_TextWrap( x + 14, y + 148, w - 28, cond, 6, 10, rl_colDim, 2 );
	if ( s_dsSum.numPopups > 1 ) {
		RL_Text( x + w - 8, y + h - 12, va( "%i / %i", s_dsSum.popup + 1, s_dsSum.numPopups ), 5, 8, rl_colDim, 2 );
	}
	RL_Text( 320, y + h + 10, "SPACE, CLICK OR A: CONTINUE", 6, 9, rl_colDim, 1 );
}

static void DSM_SumBreakdown( float x, float y, float w, float bottom ) {
	int		i;
	int		wp;
	int		maxDmg;
	int		totDmg;
	int		totKills;
	int		totTime;
	float	rh;
	float	cx;
	float	nameW;
	float	barX;
	float	barW;
	float	colDmg;
	float	colKills;
	float	colTime;
	float	colDps;
	qboolean	evolved;
	vec4_t	c;

	RL_Frame( x, y, w, bottom - y, rl_colPanel, rl_colLine );
	UI_DrawProportionalString( (int)( x + w / 2 ), (int)y + 4, "DAMAGE BY WEAPON", UI_CENTER|UI_SMALLFONT, rl_colOrange );
	if ( !s_dsSum.numRows ) {
		RL_TextWrap( x + 12, y + 40, w - 24, RL_KVInt( &rl_run, "version" ) < 2 ? "This descent is from before 0.5: no weapon records."
			: "No weapon damage recorded.", 6, 10, rl_colDim, 3 );
		return;
	}

	colDps = x + w - 10;
	colTime = colDps - 46;
	colKills = colTime - 52;
	colDmg = colKills - 44;
	nameW = 118;
	barX = x + 30 + nameW;
	barW = colDmg - 52 - barX;

	cx = y + 28;
	RL_Text( x + 30, cx, "WEAPON", 5, 8, rl_colDim, 0 );
	RL_Text( colDmg, cx, "DAMAGE", 5, 8, rl_colDim, 2 );
	RL_Text( colKills, cx, "KILLS", 5, 8, rl_colDim, 2 );
	RL_Text( colTime, cx, "HELD", 5, 8, rl_colDim, 2 );
	RL_Text( colDps, cx, "DPS", 5, 8, rl_colDim, 2 );
	UI_FillRect( x + 8, cx + 11, w - 16, 1, rl_colLine );

	maxDmg = 1;
	totDmg = totKills = totTime = 0;
	for ( i = 0; i < s_dsSum.numRows; i++ ) {
		wp = s_dsSum.order[i];
		if ( s_dsSum.dmg[wp] > maxDmg ) {
			maxDmg = s_dsSum.dmg[wp];
		}
		totDmg += s_dsSum.dmg[wp];
		totKills += s_dsSum.wkills[wp];
		totTime += s_dsSum.wtime[wp];
	}

	rh = ( bottom - 30 - ( cx + 16 ) ) / s_dsSum.numRows;
	if ( rh > 26 ) {
		rh = 26;
	}
	y = cx + 16;
	for ( i = 0; i < s_dsSum.numRows; i++ ) {
		wp = s_dsSum.order[i];
		evolved = DSM_WeaponEvolved( wp, s_dsSum.evo );
		if ( evolved ) {
			RL_Scale( c, dsm_colEvo, 0.35f, 1 );
			RL_Pic( x + 2, y - 6, rh + 8, rh + 8, rl_shGlow, c );
		}
		RL_Pic( x + 8, y + 1, rh - 4, rh - 4, DSM_WeaponIcon( wp ), NULL );
		RL_Text( x + 30, y + 1, ds_weaponNames[wp], 6, 9, i == 0 ? rl_colOrange : rl_colBone, 0 );
		if ( s_dsSum.wl[wp] > 0 ) {
			RL_Text( x + 30, y + 11, va( "LV %i", s_dsSum.wl[wp] ), 5, 8, s_dsSum.wl[wp] >= DS_MAX_WLEVEL ? dsm_colGold : rl_colDim, 0 );
		}
		if ( evolved ) {
			RL_Text( x + 62, y + 11, "EVOLVED", 5, 8, dsm_colEvo, 0 );
		}
		if ( barW > 30 ) {
			UI_FillRect( barX, y + 4, barW, rh - 10, dsm_colBarBack );
			UI_FillRect( barX, y + 4, barW * s_dsSum.dmg[wp] / maxDmg, rh - 10, evolved ? dsm_colEvo : dsm_colBar );
			if ( totDmg > 0 ) {
				RL_Text( barX + 3, y + 5, va( "%i%%", ( s_dsSum.dmg[wp] * 100 + totDmg / 2 ) / totDmg ), 4, 7, rl_colBone, 0 );
			}
		}
		RL_Text( colDmg, y + 4, va( "%i", s_dsSum.dmg[wp] ), 6, 9, rl_colBone, 2 );
		RL_Text( colKills, y + 4, va( "%i", s_dsSum.wkills[wp] ), 6, 9, rl_colBone, 2 );
		RL_Text( colTime, y + 4, DSM_TimeShort( s_dsSum.wtime[wp] ), 6, 9, rl_colDim, 2 );
		RL_Text( colDps, y + 4, s_dsSum.wtime[wp] >= 1000 ? va( "%i", s_dsSum.dmg[wp] * 1000 / s_dsSum.wtime[wp] ) : "-", 6, 9, rl_colOrange, 2 );
		y += rh;
	}
	UI_FillRect( x + 8, bottom - 24, w - 16, 1, rl_colLine );
	RL_Text( x + 30, bottom - 18, "TOTAL", 6, 9, rl_colOrange, 0 );
	RL_Text( colDmg, bottom - 18, va( "%i", totDmg ), 6, 9, rl_colOrange, 2 );
	RL_Text( colKills, bottom - 18, va( "%i", totKills ), 6, 9, rl_colOrange, 2 );
	RL_Text( colTime, bottom - 18, DSM_TimeShort( totTime ), 6, 9, rl_colDim, 2 );
	RL_Text( colDps, bottom - 18, totTime >= 1000 ? va( "%i", totDmg * 1000 / totTime ) : "-", 6, 9, rl_colOrange, 2 );
}

static float DSM_StatLine( float x, float y, float w, const char *label, const char *value, const float *color ) {
	RL_Text( x, y, label, 6, 9, rl_colDim, 0 );
	RL_Text( x + w, y, value, 6, 9, color, 2 );
	return y + 12;
}

static void DSM_SumDraw( void ) {
	float		x;
	float		y;
	float		w;
	float		left;
	float		right;
	float		mid;
	float		midW;
	int			diff;
	int			chr;
	int			floor;
	int			i;
	int			n;
	int			banked;
	qboolean	won;
	const char	*title;
	const char	*s;

	RL_Backdrop( !s_dsSum.ingame );
	diff = RL_KVInt( &rl_run, "difficulty" );
	if ( diff < 1 || diff > 5 ) {
		diff = 3;
	}
	chr = RL_KVInt( &rl_run, "character" );
	if ( chr < 0 || chr >= CHR_NUM || RL_KVInt( &rl_run, "version" ) < 2 ) {
		chr = -1;
	}
	floor = RL_KVInt( &rl_run, "floor" );
	won = DSM_RunWon();

	if ( s_dsSum.victory ) {
		title = "VICTORY";
	}
	else if ( won ) {
		title = "THE DESCENT IS WON";
	}
	else {
		title = "THE DESCENT ENDS";
	}
	UI_DrawBannerString( 320, 10, title, UI_CENTER, won ? dsm_colGold : rl_colBanner );
	RL_TextS( 320, 50, va( "%s  |  %s  |  %s", rl_skillNames[diff - 1], chr >= 0 ? ds_characters[chr].name : "BEFORE 0.5",
		s_dsSum.victory ? "THE ARCH-DEMON IS DEAD. THE ABYSS WAITS BELOW." : floor > DS_WIN_FLOOR ? va( "THE ABYSS, %i FLOORS DOWN", floor - DS_WIN_FLOOR )
		: va( "ACT %i", DS_ActForFloor( floor + 1 ) > DS_NUM_ACTS ? DS_NUM_ACTS : DS_ActForFloor( floor + 1 ) ) ), 7, 11, rl_colDim, 1 );

	left = 8 - rl_ext;
	right = 632 + rl_ext;
	w = 172;

	// left: the run
	x = left;
	RL_Frame( x, 66, w, 364, rl_colPanel, rl_colLine );
	if ( chr >= 0 ) {
		RL_Pic( x + 8, 74, 44, 44, dsm_charIcons[chr], NULL );
	}
	RL_TextS( x + ( chr >= 0 ? 60 : 10 ), 76, va( "FLOOR %i", floor ), 12, 18, won ? dsm_colGold : rl_colBone, 0 );
	RL_Text( x + ( chr >= 0 ? 60 : 10 ), 98, floor == 1 ? "FLOOR CLEARED" : "FLOORS CLEARED", 5, 8, rl_colDim, 0 );
	y = 128;
	y = DSM_StatLine( x + 10, y, w - 20, "KILLS", va( "%i", RL_KVInt( &rl_run, "kills" ) ), rl_colBone );
	y = DSM_StatLine( x + 10, y, w - 20, "TIME", RL_TimeString( RL_KVInt( &rl_run, "time" ) ), rl_colBone );
	y = DSM_StatLine( x + 10, y, w - 20, "HEADSHOTS", va( "%i", s_dsSum.st[DSST_HEADSHOTS] ), rl_colBone );
	y = DSM_StatLine( x + 10, y, w - 20, "GIBS", va( "%i", s_dsSum.st[DSST_GIBS] ), rl_colBone );
	y = DSM_StatLine( x + 10, y, w - 20, "ELITES / BOSSES", va( "%i / %i", s_dsSum.st[DSST_ELITES], s_dsSum.st[DSST_BOSSES] ), rl_colBone );
	y = DSM_StatLine( x + 10, y, w - 20, "CHESTS / GEMS", va( "%i / %i", s_dsSum.st[DSST_CHESTS], s_dsSum.st[DSST_GEMS] ), rl_colBone );
	y = DSM_StatLine( x + 10, y, w - 20, "ZIP RIDES", va( "%i", s_dsSum.st[DSST_ZIPS] ), rl_colBone );
	if ( RL_KVInt( &rl_run, "fastest" ) > 0 ) {
		y = DSM_StatLine( x + 10, y, w - 20, "FASTEST FLOOR", RL_TimeString( RL_KVInt( &rl_run, "fastest" ) ), rl_colBone );
	}
	y += 4;
	if ( s_dsSum.victory ) {
		y = DSM_StatLine( x + 10, y, w - 20, "SKULLS SO FAR", va( "%i", RL_KVInt( &rl_run, "skulls" ) ), rl_colOrange );
	}
	else {
		banked = RL_KVGet( &rl_run, "banked" )[0] ? RL_KVInt( &rl_run, "banked" ) : RL_KVInt( &rl_run, "skulls" );
		y = DSM_StatLine( x + 10, y, w - 20, "SKULLS BANKED", va( "%i", banked ), rl_colOrange );
		y = DSM_StatLine( x + 10, y, w - 20, "IN THE VAULT", va( "%i", DSM_Meta( "skulls" ) ), rl_colDim );
	}
	y = UI_DS_EconSummary( x + 10, y, w - 20 );		// ds-economy: rewards multiplier, records difficulty
	y += 4;
	if ( RL_KVInt( &rl_run, "new_best_floor" ) ) {
		RL_TextS( x + 10, y, "NEW RECORD: DEEPEST FLOOR", 5, 8, rl_colGood, 0 );
		y += 11;
	}
	if ( RL_KVInt( &rl_run, "new_best_kills" ) ) {
		RL_TextS( x + 10, y, "NEW RECORD: MOST KILLS", 5, 8, rl_colGood, 0 );
		y += 11;
	}
	if ( !s_dsSum.victory && RL_KVGet( &rl_run, "cause" )[0] ) {
		y += 6;
		RL_Text( x + 10, y, won ? "LOST IN THE ABYSS" : "CAUSE OF DEATH", 5, 8, rl_colBad, 0 );
		y = RL_TextWrap( x + 10, y + 11, w - 20, RL_KVGet( &rl_run, "cause" ), 5, 8, rl_colBone, 3 );
		if ( RL_KVGet( &rl_run, "lastdeath" )[0] ) {
			RL_TextWrap( x + 10, y + 2, w - 20, va( "Last killed by %s", RL_KVGet( &rl_run, "lastdeath" ) ), 5, 8, rl_colDim, 2 );
		}
	}

	// right: passives, evolutions, unlocks
	x = right - w;
	RL_Frame( x, 66, w, 364, rl_colPanel, rl_colLine );
	y = 72;
	RL_Text( x + 10, y, "PASSIVES", 6, 9, rl_colOrange, 0 );
	y += 13;
	n = 0;
	for ( i = 0; i < RLP_NUM; i++ ) {
		int		k;

		if ( s_dsSum.pv[i] <= 0 ) {
			continue;
		}
		UI_FillRect( x + 10, y + 2, 5, 5, dsm_passiveColors[i] );
		RL_Text( x + 18, y, ds_passiveNames[i], 5, 8, rl_colBone, 0 );
		for ( k = 0; k < DS_MAX_PLEVEL; k++ ) {
			RL_Frame( x + w - 10 - ( DS_MAX_PLEVEL - k ) * 8, y + 1, 6, 6, k < s_dsSum.pv[i] ? dsm_passiveColors[i] : rl_colBlack, rl_colLine );
		}
		y += 11;
		n++;
	}
	if ( !n ) {
		RL_Text( x + 10, y, "none", 5, 8, rl_colDark, 0 );
		y += 11;
	}
	y += 6;
	RL_Text( x + 10, y, "EVOLUTIONS", 6, 9, dsm_colEvo, 0 );
	y += 13;
	n = 0;
	for ( i = 0; i < EVO_NUM; i++ ) {
		if ( s_dsSum.evo & ( 1 << i ) ) {
			RL_Text( x + 10, y, ds_evolutions[i].name, 5, 8, rl_colBone, 0 );
			y += 11;
			n++;
		}
	}
	if ( !n ) {
		RL_Text( x + 10, y, "none", 5, 8, rl_colDark, 0 );
		y += 11;
	}
	y += 6;
	{
		int		cards[RL_MAX_CARDS];

		RL_Text( x + 10, y, va( "CARDS %i", RL_ParseInts( RL_KVGet( &rl_run, "cards" ), cards, RL_MAX_CARDS ) ), 6, 9, rl_colOrange, 0 );
	}
	y += 18;
	y = UI_DS_SkillsSummary( x + 10, y, w - 20 );		// ds-skills: skills trained, +N PERK POINTS
	if ( s_dsSum.numRunUnl > 0 ) {
		RL_Text( x + 10, y, "UNLOCKED", 6, 9, dsm_colGold, 0 );
		y += 13;
		for ( i = 0; i < s_dsSum.numRunUnl && y < 420; i++ ) {
			s = s_dsSum.runUnl[i] < CHR_NUM ? "character" : "secret card";
			RL_TextS( x + 10, y, DSM_UnlockName( s_dsSum.runUnl[i] ), 6, 9, dsm_colGold, 0 );
			RL_Text( x + w - 10, y + 1, s, 4, 7, rl_colDim, 2 );
			y += 12;
		}
	}

	// middle: the damage breakdown
	mid = left + w + 8;
	midW = right - w - 8 - mid;
	DSM_SumBreakdown( mid, 66, midW, 430 );

	Menu_Draw( &s_dsSum.menu );
	if ( s_dsSum.popup < s_dsSum.numPopups ) {
		DSM_DrawUnlockPopup();
	}
}

static void DSM_SumMenu( qboolean victory ) {
	memset( &s_dsSum, 0, sizeof( s_dsSum ) );
	s_dsSum.ingame = RL_InGame();
	s_dsSum.victory = victory;
	RL_SetupMenu( &s_dsSum.menu, DSM_SumDraw, s_dsSum.ingame );
	s_dsSum.menu.key = DSM_SumKey;

	DSM_ProcessRun();
	DSM_SumLoad();

	if ( victory ) {
		RL_InitButton( &s_dsSum.abyss, ID_RL_ABYSS, 320 - 256, 438, 250, 32, "INTO THE ABYSS", DSM_SumEvent );
		s_dsSum.abyss.center = qtrue;
		Menu_AddItem( &s_dsSum.menu, &s_dsSum.abyss );
		RL_InitButton( &s_dsSum.cont, ID_RL_CONTINUE, 320 + 6, 438, 250, 32, "END THE DESCENT", DSM_SumEvent );
	}
	else {
		RL_InitButton( &s_dsSum.cont, ID_RL_CONTINUE, 320 - 90, 438, 180, 32, "CONTINUE", DSM_SumEvent );
	}
	s_dsSum.cont.center = qtrue;
	Menu_AddItem( &s_dsSum.menu, &s_dsSum.cont );

	if ( s_dsSum.ingame ) {
		uis.menusp = 0;
	}
	UI_PushMenu( &s_dsSum.menu );
	trap_Cmd_ExecuteText( EXEC_APPEND, DSM_RunWon() ? "music music/win\n" : "music music/loss\n" );
}

void UI_DS_SummaryMenu( void ) {
	DSM_SumMenu( qfalse );
}

void UI_DS_VictoryMenu( void ) {
	DSM_SumMenu( qtrue );
}

/*
=================
UI_DS_OpenRun

RL_OpenRun, the run file loaded: what the META screens take over
=================
*/
static void DSM_OpenOldAction( qboolean result ) {
	DSM_EndOldAction( result );
}

qboolean UI_DS_OpenRun( void ) {
	int		L;
	int		i;

	if ( UI_DS_DailyOn() ) {
		return UI_DS_DailyOpenRun();		// en-daily: its own result screen, no summary / history / merge
	}
	DSM_ProcessRun();
	if ( !RL_KVInt( &rl_run, "active" ) ) {
		UI_DS_SummaryMenu();
		return qtrue;
	}
	if ( RL_KVInt( &rl_run, "version" ) < 2 ) {
		UI_ConfirmMenu( "END THE OLD DESCENT?", DSM_EndOldDraw, DSM_OpenOldAction );
		return qtrue;
	}
	if ( RL_KVInt( &rl_run, "victory" ) ) {
		UI_DS_VictoryMenu();
		return qtrue;
	}
	if ( RL_KVInt( &rl_run, "infloor" ) && !RL_KVGet( &rl_run, "offer" )[0] && RL_KVGet( &rl_run, "node_layer" )[0] ) {
		L = RL_KVInt( &rl_run, "node_layer" );
		i = RL_KVInt( &rl_run, "node_index" );
		Com_Printf( "DESCENT: resuming floor %i (node %i) from its start\n", L + 1, i );
		RL_LaunchNode( L, i );
		return qtrue;
	}
	return qfalse;
}


/*
=======================================================================

RECORDS + HISTORY

=======================================================================
*/

typedef struct {
	menuframework_s	menu;
	rlButton_t		history;
	rlButton_t		hall;		// en-memory: NEMESIS HALL (ui_ds_hall.c)
	rlButton_t		back;
	rlButton_t		reset;		// ds-economy: RESET ALL PROGRESS
} dsRec_t;

static dsRec_t s_dsRec;

static void DSM_RecordsEvent( void *ptr, int event ) {
	if ( event != QM_ACTIVATED ) {
		return;
	}
	if ( ((menucommon_s *)ptr)->id == ID_RL_HISTORY ) {
		UI_DS_HistoryMenu();
		return;
	}
	if ( ((menucommon_s *)ptr)->id == ID_RL_HALL ) {
		UI_DS_HallMenu( 0 );		// en-memory: nemesis hall of fame + rivals
		return;
	}
	if ( ((menucommon_s *)ptr)->id == ID_RL_RESET ) {
		UI_DS_ResetMenu();		// ds-economy: two confirm screens, backups first
		return;
	}
	UI_PopMenu();
}

static float DSM_RecLine( float x, float y, float w, const char *label, const char *value ) {
	RL_TextS( x + 12, y, label, 7, 11, rl_colBone, 0 );
	RL_TextS( x + w - 12, y, value, 7, 11, rl_colOrange, 2 );
	return y + 16;
}

static void DSM_RecordsDraw( void ) {
	int		i;
	int		best;
	float	y;
	float	x;
	float	w;
	float	gap;
	float	x2;

	RL_Backdrop( !RL_InGame() );
	RL_Header( "RECORDS", "How deep you have gone, and everything you did on the way." );
	RL_DrawSkulls( DSM_Meta( "skulls" ) );

	gap = 12;
	w = ( 624 + 2 * rl_ext - gap ) / 2;
	if ( w > 380 ) {
		w = 380;
	}
	x = 320 - w - gap / 2;
	x2 = 320 + gap / 2;

	// deepest floor per difficulty
	RL_Frame( x, 66, w, 132, rl_colPanel, rl_colLine );
	UI_DrawProportionalString( (int)( x + w / 2 ), 70, "DEEPEST FLOOR", UI_CENTER|UI_SMALLFONT, rl_colOrange );
	y = 96;
	for ( i = 0; i < 5; i++ ) {
		best = DSM_Meta( va( "best_floor_%i", i + 1 ) );
		RL_Pic( x + 12, y - 3, 16, 16, rl_skillIcons[i], NULL );
		RL_TextS( x + 34, y, rl_skillNames[i], 7, 11, rl_colBone, 0 );
		if ( DSM_Meta( va( "won_%i", i + 1 ) ) ) {
			RL_Text( x + w - 60, y + 1, "WON", 6, 9, dsm_colGold, 2 );
		}
		RL_TextS( x + w - 12, y, best > 0 ? va( "%i", best ) : "-", 7, 11, best > 0 ? rl_colOrange : rl_colDark, 2 );
		y += 19;
	}

	// totals
	RL_Frame( x, 206, w, 224, rl_colPanel, rl_colLine );
	UI_DrawProportionalString( (int)( x + w / 2 ), 210, "TOTALS", UI_CENTER|UI_SMALLFONT, rl_colOrange );
	y = 236;
	y = DSM_RecLine( x, y, w, "DESCENTS", va( "%i", DSM_Meta( "runs" ) ) );
	y = DSM_RecLine( x, y, w, "DESCENTS WON", va( "%i", DSM_Meta( "cnt_wins" ) ) );
	y = DSM_RecLine( x, y, w, "FLOORS CLEARED", va( "%i", DSM_Meta( "total_floors" ) ) );
	y = DSM_RecLine( x, y, w, "ACTS CLEARED", va( "%i", DSM_Meta( "cnt_acts" ) ) );
	y = DSM_RecLine( x, y, w, "KILLS", va( "%i", DSM_Meta( "total_kills" ) ) );
	y = DSM_RecLine( x, y, w, "MOST KILLS IN ONE DESCENT", va( "%i", DSM_Meta( "best_kills" ) ) );
	y = DSM_RecLine( x, y, w, "SKULLS IN THE VAULT", va( "%i", DSM_Meta( "skulls" ) ) );
	y = DSM_RecLine( x, y, w, "TIME IN THE DESCENT", RL_TimeString( DSM_Cnt( DSST_TIME ) ) );
	DSM_RecLine( x, y, w, "COLLECTION", va( "%i / %i", DSM_CollectionCount( &i ), i ) );

	// lifetime counters (they drive the unlocks)
	RL_Frame( x2, 66, w, 236, rl_colPanel, rl_colLine );
	UI_DrawProportionalString( (int)( x2 + w / 2 ), 70, "LIFETIME", UI_CENTER|UI_SMALLFONT, rl_colOrange );
	y = 96;
	y = DSM_RecLine( x2, y, w, "ENEMIES KILLED", va( "%i", DSM_Cnt( DSST_KILLS ) ) );
	y = DSM_RecLine( x2, y, w, "HEADSHOTS", va( "%i", DSM_Cnt( DSST_HEADSHOTS ) ) );
	y = DSM_RecLine( x2, y, w, "GAUNTLET KILLS", va( "%i", DSM_Cnt( DSST_GKILLS ) ) );
	y = DSM_RecLine( x2, y, w, "GIBS", va( "%i", DSM_Cnt( DSST_GIBS ) ) );
	y = DSM_RecLine( x2, y, w, "ZIP LINE RIDES", va( "%i", DSM_Cnt( DSST_ZIPS ) ) );
	y = DSM_RecLine( x2, y, w, "CHESTS OPENED", va( "%i", DSM_Cnt( DSST_CHESTS ) ) );
	y = DSM_RecLine( x2, y, w, "EVOLUTIONS", va( "%i", DSM_Cnt( DSST_EVOS ) ) );
	y = DSM_RecLine( x2, y, w, "ELITES KILLED", va( "%i", DSM_Cnt( DSST_ELITES ) ) );
	y = DSM_RecLine( x2, y, w, "BOSSES DEFEATED", va( "%i", DSM_Cnt( DSST_BOSSES ) ) );
	y = DSM_RecLine( x2, y, w, "GEMS COLLECTED", va( "%i", DSM_Cnt( DSST_GEMS ) ) );
	y = DSM_RecLine( x2, y, w, "DAMAGE DEALT", va( "%i", DSM_Cnt( DSST_DAMAGE ) ) );
	DSM_RecLine( x2, y, w, "SECRET STASHES FOUND", va( "%i", DSM_Cnt( DSST_STASHES ) ) );	// ds-secrets

	// single floor bests
	RL_Frame( x2, 310, w, 120, rl_colPanel, rl_colLine );
	UI_DrawProportionalString( (int)( x2 + w / 2 ), 314, "ONE FLOOR", UI_CENTER|UI_SMALLFONT, rl_colOrange );
	y = 340;
	y = DSM_RecLine( x2, y, w, "FASTEST FLOOR WON", DSM_Meta( "best_ftime" ) > 0 ? RL_TimeString( DSM_Meta( "best_ftime" ) ) : "-" );
	y = DSM_RecLine( x2, y, w, "MOST KILLS", va( "%i", DSM_Meta( "best_fkills" ) ) );
	DSM_RecLine( x2, y, w, "MOST GIBS", va( "%i", DSM_Meta( "best_fgibs" ) ) );

	if ( UI_DS_EconMessage()[0] ) {		// ds-economy: the last reset
		RL_Text( 320, 426, UI_DS_EconMessage(), 5, 8, rl_colGood, 1 );
	}
	Menu_Draw( &s_dsRec.menu );
}

void UI_DS_RecordsMenu( void ) {
	memset( &s_dsRec, 0, sizeof( s_dsRec ) );
	RL_SetupMenu( &s_dsRec.menu, DSM_RecordsDraw, RL_InGame() );
	DSM_ProcessRun();
	RL_InitButton( &s_dsRec.history, ID_RL_HISTORY, 320 - 90, 438, 180, 30, "RUN HISTORY", DSM_RecordsEvent );
	s_dsRec.history.center = qtrue;
	RL_InitButton( &s_dsRec.hall, ID_RL_HALL, 320 - 90 - 98, 440, 92, 28, "NEMESIS HALL", DSM_RecordsEvent );	// en-memory
	UI_DS_HallButton( &s_dsRec.hall );
	RL_InitButton( &s_dsRec.back, ID_RL_BACK, 8 - rl_ext, 440, 120, 28, "BACK", DSM_RecordsEvent );
	s_dsRec.back.center = qtrue;
	Menu_AddItem( &s_dsRec.menu, &s_dsRec.history );
	Menu_AddItem( &s_dsRec.menu, &s_dsRec.hall );
	Menu_AddItem( &s_dsRec.menu, &s_dsRec.back );
	RL_InitButton( &s_dsRec.reset, ID_RL_RESET, 632 + rl_ext - 170, 440, 170, 28, "RESET ALL PROGRESS", DSM_RecordsEvent );
	UI_DS_SmallButton( &s_dsRec.reset );
	if ( RL_InGame() ) {
		s_dsRec.reset.bm.generic.flags |= QMF_GRAYED;		// main menu only
	}
	Menu_AddItem( &s_dsRec.menu, &s_dsRec.reset );
	UI_PushMenu( &s_dsRec.menu );
}

typedef struct {
	menuframework_s	menu;
	rlButton_t		back;
	char			buf[DSM_HIST_MAX * DSM_HIST_LINE + 512];
	char			*lines[DSM_HIST_MAX + 8];
	int				num;
} dsHist_t;

static dsHist_t s_dsHist;

static void DSM_HistoryLoad( void ) {
	fileHandle_t	f;
	int				len;
	char			*p;

	s_dsHist.num = 0;
	s_dsHist.buf[0] = 0;
	len = trap_FS_FOpenFile( RL_HISTORY_FILE, &f, FS_READ );
	if ( !f ) {
		return;
	}
	if ( len > (int)sizeof( s_dsHist.buf ) - 1 ) {
		len = sizeof( s_dsHist.buf ) - 1;
	}
	if ( len > 0 ) {
		trap_FS_Read( s_dsHist.buf, len, f );
		s_dsHist.buf[len] = 0;
	}
	trap_FS_FCloseFile( f );
	p = s_dsHist.buf;
	while ( *p && s_dsHist.num < DSM_HIST_MAX + 4 ) {
		s_dsHist.lines[s_dsHist.num] = p;
		while ( *p && *p != '\n' ) {
			p++;
		}
		if ( *p ) {
			*p++ = 0;
		}
		len = strlen( s_dsHist.lines[s_dsHist.num] );
		while ( len > 0 && s_dsHist.lines[s_dsHist.num][len - 1] == '\r' ) {
			s_dsHist.lines[s_dsHist.num][--len] = 0;
		}
		if ( len ) {
			s_dsHist.num++;
		}
	}
}

// field n of a history line ('|' separated) into out
static void DSM_HistField( const char *line, int n, char *out, int size ) {
	int		i;

	while ( n > 0 && *line ) {
		if ( *line == '|' ) {
			n--;
		}
		line++;
	}
	for ( i = 0; i < size - 1 && line[i] && line[i] != '|'; i++ ) {
		out[i] = line[i];
	}
	out[i] = 0;
}

static void DSM_HistoryDraw( void ) {
	char		f[10][128];
	float		x;
	float		y;
	float		w;
	float		cx[9];
	int			i;
	int			k;
	int			d;
	int			c;
	int			maxCause;
	const float	*col;

	RL_Backdrop( !RL_InGame() );
	RL_Header( "RUN HISTORY", va( "Your last %i descents, newest first.", DSM_HIST_MAX ) );
	RL_DrawSkulls( DSM_Meta( "skulls" ) );

	x = 8 - rl_ext;
	w = 624 + 2 * rl_ext;
	RL_Frame( x, 66, w, 368, rl_colPanel, rl_colLine );
	cx[0] = x + 10;			// date
	cx[1] = cx[0] + 100;	// character
	cx[2] = cx[1] + 50;		// difficulty
	cx[3] = cx[2] + 84;		// result
	cx[4] = cx[3] + 70;		// floor (right)
	cx[5] = cx[4] + 44;		// kills (right)
	cx[6] = cx[5] + 52;		// time (right)
	cx[7] = cx[6] + 48;		// skulls (right)
	cx[8] = cx[7] + 12;		// cause
	maxCause = (int)( ( x + w - 10 - cx[8] ) / 5 );

	y = 72;
	RL_Text( cx[0], y, "DATE", 5, 8, rl_colDim, 0 );
	RL_Text( cx[1], y, "WHO", 5, 8, rl_colDim, 0 );
	RL_Text( cx[2], y, "DIFFICULTY", 5, 8, rl_colDim, 0 );
	RL_Text( cx[3], y, "RESULT", 5, 8, rl_colDim, 0 );
	RL_Text( cx[4], y, "FLOOR", 5, 8, rl_colDim, 2 );
	RL_Text( cx[5], y, "KILLS", 5, 8, rl_colDim, 2 );
	RL_Text( cx[6], y, "TIME", 5, 8, rl_colDim, 2 );
	RL_Text( cx[7], y, "SKULLS", 5, 8, rl_colDim, 2 );
	if ( maxCause > 6 ) {
		RL_Text( cx[8], y, "HOW IT ENDED", 5, 8, rl_colDim, 0 );
	}
	UI_FillRect( x + 6, y + 10, w - 12, 1, rl_colLine );
	y += 15;

	if ( !s_dsHist.num ) {
		RL_Text( 320, 200, "No finished descents yet.", 7, 11, rl_colDim, 1 );
	}
	for ( i = s_dsHist.num - 1; i >= 0 && y < 428; i-- ) {
		for ( k = 0; k < 9; k++ ) {
			DSM_HistField( s_dsHist.lines[i], k, f[k], sizeof( f[k] ) );
		}
		d = atoi( f[1] );
		c = f[2][0] ? atoi( f[2] ) : -1;
		col = rl_colBone;
		if ( !Q_stricmp( f[4], "WON" ) || !Q_stricmp( f[4], "ABYSS" ) ) {
			col = dsm_colGold;
		}
		else if ( !Q_stricmp( f[4], "ABANDONED" ) ) {
			col = rl_colDark;
		}
		if ( i == s_dsHist.num - 1 ) {
			UI_FillRect( x + 4, y - 2, w - 8, 11, rl_colPanelHi );
		}
		RL_Text( cx[0], y, f[0], 5, 8, rl_colDim, 0 );
		RL_Text( cx[1], y, ( c >= 0 && c < CHR_NUM ) ? ds_characters[c].name : "-", 5, 8, col, 0 );
		RL_Text( cx[2], y, ( d >= 1 && d <= 5 ) ? va( "%s%s", rl_skillNames[d - 1], strchr( f[1], '-' ) ? "+" : "" ) : "-", 5, 8, rl_colDim, 0 );
		RL_Text( cx[3], y, f[4], 5, 8, col, 0 );
		RL_Text( cx[4], y, f[3], 5, 8, rl_colOrange, 2 );
		RL_Text( cx[5], y, f[5], 5, 8, rl_colBone, 2 );
		RL_Text( cx[6], y, DSM_TimeShort( atoi( f[6] ) ), 5, 8, rl_colBone, 2 );
		RL_Text( cx[7], y, f[7], 5, 8, rl_colOrange, 2 );
		if ( maxCause > 6 ) {
			// the cause, cut to the space left
			DSM_HistField( s_dsHist.lines[i], 8, f[9], sizeof( f[9] ) );
			if ( (int)strlen( f[9] ) > maxCause && maxCause < (int)sizeof( f[9] ) ) {
				f[9][maxCause] = 0;
			}
			RL_Text( cx[8], y, f[9], 5, 8, rl_colDim, 0 );
		}
		y += 11;
	}

	Menu_Draw( &s_dsHist.menu );
	RL_Footer( "ESC, RIGHT MOUSE OR BACK TO RETURN" );
}

static void DSM_HistoryEvent( void *ptr, int event ) {
	if ( event == QM_ACTIVATED ) {
		UI_PopMenu();
	}
}

void UI_DS_HistoryMenu( void ) {
	memset( &s_dsHist, 0, sizeof( s_dsHist ) );
	RL_SetupMenu( &s_dsHist.menu, DSM_HistoryDraw, RL_InGame() );
	RL_LoadMeta();
	DSM_HistoryLoad();
	RL_InitButton( &s_dsHist.back, ID_RL_BACK, 8 - rl_ext, 440, 120, 28, "BACK", DSM_HistoryEvent );
	s_dsHist.back.center = qtrue;
	Menu_AddItem( &s_dsHist.menu, &s_dsHist.back );
	UI_PushMenu( &s_dsHist.menu );
}


/*
=======================================================================

HOOKS

=======================================================================
*/

void UI_DS_Cache( void ) {
	int		i;
	gitem_t	*it;

	for ( i = 0; i < CHR_NUM; i++ ) {
		dsm_charIcons[i] = trap_R_RegisterShaderNoMip( va( "models/players/%s/icon_default", ds_characters[i].model ) );
	}
	for ( i = 0; i < BOSS_NUM; i++ ) {
		dsm_bossIcons[i] = trap_R_RegisterShaderNoMip( va( "models/players/%s/icon_default", ds_bosses[i].model ) );
	}
	for ( i = 1; i < DS_NUM_WEAPONS; i++ ) {
		dsm_weaponIcons[i] = 0;
		for ( it = bg_itemlist + 1; it->classname; it++ ) {
			if ( it->giType == IT_WEAPON && it->giTag == i ) {
				dsm_weaponIcons[i] = trap_R_RegisterShaderNoMip( it->icon );
				break;
			}
		}
	}
	dsm_unlockSound = trap_S_RegisterSound( "sound/rearmed/mastery_up.wav", qfalse );	// (voc_newhighscore.wav is Team Arena only: a missing-sound warning)
}

/*
=================
UI_DS_ConsoleCommand

descent newrun / characters <difficulty> / collection / history, rlvictory,
dev: dsui [process | set <key> <value> | unlock <id> | char <n>]
=================
*/
qboolean UI_DS_ConsoleCommand( const char *cmd ) {
	const char	*arg;
	int			n;

	if ( UI_DS_SkillsCommand( cmd ) ) {		// ds-skills dev: dsskillmerge
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "descent" ) ) {
		arg = UI_Argv( 1 );
		if ( !Q_stricmp( arg, "skills" ) ) {		// ds-skills: descent skills [skill]
			n = UI_Argv( 2 )[0] ? atoi( UI_Argv( 2 ) ) : -1;
			UI_RL_HubMenu();
			UI_DS_SkillsMenu( n );
			return qtrue;
		}
		if ( !Q_stricmp( arg, "collection" ) ) {
			UI_RL_HubMenu();
			UI_DS_CollectionMenu();
			if ( UI_Argv( 2 )[0] ) {
				DSC_SetTab( atoi( UI_Argv( 2 ) ) );
			}
			return qtrue;
		}
		if ( !Q_stricmp( arg, "history" ) ) {
			UI_RL_HubMenu();
			UI_DS_RecordsMenu();
			UI_DS_HistoryMenu();
			return qtrue;
		}
		if ( !Q_stricmp( arg, "characters" ) ) {
			n = atoi( UI_Argv( 2 ) );
			UI_RL_HubMenu();
			RL_NewRunMenu();
			UI_DS_CharacterMenu( n );
			return qtrue;
		}
		if ( !Q_stricmp( arg, "newrun" ) ) {
			UI_RL_HubMenu();
			RL_NewRunMenu();
			return qtrue;
		}
		if ( !Q_stricmp( arg, "summary" ) ) {
			UI_RL_HubMenu();
			UI_DS_SummaryMenu();
			return qtrue;
		}
		return qfalse;
	}

	if ( !Q_stricmp( cmd, "rlvictory" ) ) {
		if ( RL_InGame() ) {
			uis.menusp = 0;
		}
		UI_DS_VictoryMenu();
		return qtrue;
	}

	// dev (UI side; the game has its own dsmeta)
	if ( !Q_stricmp( cmd, "dsui" ) ) {
		arg = UI_Argv( 1 );
		if ( !Q_stricmp( arg, "process" ) ) {
			DSM_ProcessRun();
			return qtrue;
		}
		if ( !Q_stricmp( arg, "set" ) ) {
			char	key[RL_KEY_LEN];
			char	val[RL_VAL_LEN];

			// UI_Argv returns one static buffer: copy each argument
			Q_strncpyz( key, UI_Argv( 2 ), sizeof( key ) );
			Q_strncpyz( val, UI_Argv( 3 ), sizeof( val ) );
			RL_LoadMeta();
			RL_KVSet( &rl_meta, key, val );
			RL_SaveMeta();
			Com_Printf( "dsui: %s = %s\n", key, val );
			return qtrue;
		}
		if ( !Q_stricmp( arg, "char" ) ) {		// the character the next "descent new" run uses
			dsm_pendingChar = atoi( UI_Argv( 2 ) );
			return qtrue;
		}
		if ( !Q_stricmp( arg, "unlock" ) ) {
			RL_LoadMeta();
			RL_LoadRun();
			DSM_Unlock( atoi( UI_Argv( 2 ) ) );
			RL_SaveMeta();
			return qtrue;
		}
		RL_LoadMeta();
		DSM_Migrate();
		for ( n = 0; n < rl_meta.num; n++ ) {
			Com_Printf( "  %s %s\n", rl_meta.key[n], rl_meta.val[n] );
		}
		Com_Printf( "dsui: %i keys\n", rl_meta.num );
		return qtrue;
	}
	return qfalse;
}
