/*
===========================================================================
REARMED -- DAILY DESCENT CHALLENGE, UI side (round 4, en-daily). Contract: game/bg_ds_daily.h.

DESCENT > DAILY CHALLENGE: one fixed descent per local date (seed = hash of YYYYMMDD from
trap_RealTime; dev cvar ds_dailyDate fakes the date). Act 1, 6 floors, the act boss as a timed
1v1, Hurt Me Plenty, 2 lives, the day's character + a starting ability from the seed, no
upgrades / perks / skulls. Unlimited attempts, the best one counts.

While cvar ds_daily is 1 (this screen, the daily's floor map and floors):
- RL_LoadRun / RL_SaveRun use rearmed_daily_run.txt, never rearmed_run.txt;
- RL_LoadMeta gives a blank profile (UI_DS_DailyBlankMeta) and RL_SaveMeta writes nothing, so
  lifetime map counts, unlocks, skills and records never move; the history isn't written;
- the floor map hides the difficulty switch; rlover / a finished run opens the result here.
The flag is cleared by the main menu (UI_RL_MainMenuOpened, which reopens this screen when
a daily floor was quit), by BACK here and by the "descent" command.

rearmed_daily.txt (personal bests, only this file writes it): version, d<YYYYMMDD> <best>,
today <date> <best> <attempts>, today_list <scores csv>, today_bd <floors kills style speed
nodeath> (today's best), streak / streak_best / streak_last, attempts, top0..top9
"<score> <date> <floors>".

Dev: dsdaily (this screen), dsdaily start (a new attempt now, then the floor map),
dsdailyinfo [date] (prints the day's plan: seed, character, floors, maps, modifiers),
dsdailypb (prints the table), dsdailyscore (the loaded daily run's score).
===========================================================================
*/
#include "ui_local.h"
#include "ui_ds_meta.h"
#include "ui_ds_maps.h"
#include "../game/bg_ds_daily.h"
#include "../game/bg_ds_surprise.h"

int trap_RealTime( qtime_t *qtime );

#define DSD_TOP				10
#define DSD_MAX_DATES		120		// d<date> keys kept in the PB file
#define DSD_PREVIEW_LAYERS	8		// = RL_ComputeReach on floor 1 (the window the first map shows)

#define ID_DSD_START		300
#define ID_DSD_RESTART		301
#define ID_DSD_AGAIN		302
#define ID_DSD_DONE			303

typedef struct {
	int		floors;			// floors won
	int		kills;
	int		deaths;
	int		bossLeft;		// msec
	int		ptsFloors, ptsKills, ptsStyle, ptsSpeed, ptsNoDeath;
	int		styleSum;		// style ranks summed over won floors
	int		total;
} dsdScore_t;

typedef struct {
	int		date;
	int		seed;
	int		character;
	int		ability;
	int		n[DSD_FLOORS];
	int		type[DSD_FLOORS][RL_MAX_NODES];
	char	map[DSD_FLOORS][RL_MAX_NODES][32];
	int		mod[DSD_FLOORS][RL_MAX_NODES];
} dsdPlan_t;

typedef struct {
	menuframework_s	menu;
	rlButton_t		start;
	rlButton_t		restart;
	rlButton_t		back;
	int				active;			// an attempt of today's daily is running
} dsdMenu_t;

typedef struct {
	menuframework_s	menu;
	rlButton_t		again;
	rlButton_t		done;
} dsdResult_t;

static dsdMenu_t	s_dsd;
static dsdResult_t	s_dsdRes;
static dsdPlan_t	dsd_plan;
static rlKV_t		dsd_pb;

// the last recorded attempt (the result screen)
static dsdScore_t	dsd_last;
static int			dsd_lastDate;
static int			dsd_lastTopRank;		// 1..10, 0 = not in the top 10
static qboolean		dsd_lastBestToday;
static int			dsd_lastPrevBest;

static qhandle_t	dsd_charIcons[CHR_NUM];

static const char *dsd_months[12] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
static const char *dsd_typeNames[RLN_NUM] = { "FIGHT", "ELITE", "EVENT", "BOSS", "???" };
static vec4_t dsd_typeColors[RLN_NUM] = {
	{ 0.86f, 0.78f, 0.62f, 1.0f },
	{ 1.0f, 0.35f, 0.12f, 1.0f },
	{ 0.35f, 0.72f, 1.0f, 1.0f },
	{ 1.0f, 0.2f, 0.15f, 1.0f },
	{ 1.0f, 0.82f, 0.3f, 1.0f }
};
static vec4_t dsd_colGold = { 1.0f, 0.8f, 0.3f, 1.0f };

// abilities with no prerequisite (= ui_roguelike.c rl_startCards): the day's free ability
static const int dsd_abilities[] = { RLC_DOUBLEJUMP, RLC_WALLJUMP, RLC_WALLRUN, RLC_DASH, RLC_GRAPPLE, RLC_ZIPLINE, RLC_SLIDE, RLC_SLAM };


/*
=======================================================================

FLAG, DATE, SEED

=======================================================================
*/

qboolean UI_DS_DailyOn( void ) {
	return trap_Cvar_VariableValue( DSD_FLAG_CVAR ) != 0;
}

void UI_DS_DailySet( qboolean on ) {
	if ( on != UI_DS_DailyOn() ) {
		trap_Cvar_Set( DSD_FLAG_CVAR, on ? "1" : "0" );
		rl_hubDirty = qtrue;
	}
}

// RL_LoadMeta: while the daily is on, a blank profile (no upgrades, unlocks, lifetime map counts)
qboolean UI_DS_DailyBlankMeta( void ) {
	if ( !UI_DS_DailyOn() ) {
		return qfalse;
	}
	rl_meta.num = 0;
	RL_KVSet( &rl_meta, "version", "1" );
	RL_KVSet( &rl_meta, "ds2", "1" );			// (no 0.4 migration)
	RL_KVSet( &rl_meta, "econ_v", "2" );		// (no economy refund)
	RL_KVSet( &rl_meta, "dsm_v", "1" );			// (ds-maps: no play counts seeded from the game log)
	return qtrue;
}

static unsigned DSD_Hash( unsigned a, unsigned b ) {
	unsigned	h;

	h = a * 0x9E3779B1u + b * 0x85EBCA77u + 0x27D4EB2Fu;
	h ^= h >> 15;
	h *= 0x2C1B3C6Du;
	h ^= h >> 12;
	h *= 0x297A2D39u;
	h ^= h >> 15;
	return h;
}

static qboolean DSD_ValidDate( int d ) {
	int		y, m, day;

	y = d / 10000;
	m = ( d / 100 ) % 100;
	day = d % 100;
	return y >= 2000 && y <= 2999 && m >= 1 && m <= 12 && day >= 1 && day <= 31;
}

// today, YYYYMMDD (the local clock, or ds_dailyDate for tests)
int UI_DS_DailyToday( void ) {
	char	buf[32];
	qtime_t	qt;
	int		d;

	trap_Cvar_VariableStringBuffer( DSD_DATE_CVAR, buf, sizeof( buf ) );
	d = atoi( buf );
	if ( DSD_ValidDate( d ) ) {
		return d;
	}
	memset( &qt, 0, sizeof( qt ) );
	trap_RealTime( &qt );
	return ( qt.tm_year + 1900 ) * 10000 + ( qt.tm_mon + 1 ) * 100 + qt.tm_mday;
}

static int DSD_Seed( int date ) {
	int		s;

	s = (int)( DSD_Hash( (unsigned)date, 0xDA117u ) & 0x7fffffff );
	return s ? s : 1;
}

// days since 1970-01-01 (civil calendar), for the streak
static int DSD_DayNumber( int date ) {
	int		y, m, d, era, yoe, doy, doe;

	y = date / 10000;
	m = ( date / 100 ) % 100;
	d = date % 100;
	if ( m <= 2 ) {
		y--;
	}
	era = y / 400;
	yoe = y - era * 400;
	doy = ( 153 * ( m + ( m > 2 ? -3 : 9 ) ) + 2 ) / 5 + d - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

static const char *DSD_DateString( int date ) {
	static char	bufs[4][24];
	static int	idx;
	char		*b;
	int			m;

	b = bufs[idx++ & 3];
	m = ( date / 100 ) % 100;
	if ( m < 1 || m > 12 ) {
		return "?";
	}
	Com_sprintf( b, 24, "%i %s %i", date % 100, dsd_months[m - 1], date / 10000 );
	return b;
}

static const char *DSD_Num( int n ) {
	static char	bufs[4][16];
	static int	idx;
	char		*b;

	b = bufs[idx++ & 3];
	if ( n >= 1000000 ) {
		Com_sprintf( b, 16, "%i,%03i,%03i", n / 1000000, ( n / 1000 ) % 1000, n % 1000 );
	}
	else if ( n >= 1000 ) {
		Com_sprintf( b, 16, "%i,%03i", n / 1000, n % 1000 );
	}
	else {
		Com_sprintf( b, 16, "%i", n );
	}
	return b;
}


/*
=======================================================================

THE DAY'S PLAN

=======================================================================
*/

static int DSD_Character( int seed ) {
	return (int)( DSD_Hash( (unsigned)seed, 11u ) % CHR_ORBB );		// every character but the secret one
}

static int DSD_Ability( int seed, int character ) {
	const dsCharInfo_t	*ch;
	int					n, k, j, card, dup;

	ch = &ds_characters[character];
	n = (int)ARRAY_LEN( dsd_abilities );
	k = (int)( DSD_Hash( (unsigned)seed, 12u ) % (unsigned)n );
	for ( j = 0; j < n; j++ ) {
		card = dsd_abilities[( k + j ) % n];
		for ( dup = 0; dup < 4 && ch->startCards[dup] >= 0; dup++ ) {
			if ( ch->startCards[dup] == card ) {
				break;
			}
		}
		if ( dup >= 4 || ch->startCards[dup] < 0 ) {
			return card;		// one the character doesn't start with
		}
	}
	return dsd_abilities[k];
}

/*
=================
DSD_BuildPlan

The floors of the day as its first floor map shows them (types, maps, modifiers). Needs the
daily flag on (a blank lifetime profile); writes nothing.
=================
*/
static void DSD_BuildPlan( int date ) {
	rlLayer_t	*ly;
	qboolean	was;
	int			L, i;

	memset( &dsd_plan, 0, sizeof( dsd_plan ) );
	dsd_plan.date = date;
	dsd_plan.seed = DSD_Seed( date );
	dsd_plan.character = DSD_Character( dsd_plan.seed );
	dsd_plan.ability = DSD_Ability( dsd_plan.seed, dsd_plan.character );

	was = UI_DS_DailyOn();
	UI_DS_DailySet( qtrue );
	ly = UI_DSM_Preview( dsd_plan.seed, DSD_PREVIEW_LAYERS );
	for ( L = 0; L < DSD_FLOORS; L++ ) {
		dsd_plan.n[L] = ly[L].n;
		for ( i = 0; i < ly[L].n && i < RL_MAX_NODES; i++ ) {
			dsd_plan.type[L][i] = ly[L].type[i];
			Q_strncpyz( dsd_plan.map[L][i], UI_DSM_MapName( ly[L].map[i] ), sizeof( dsd_plan.map[0][0] ) );
			dsd_plan.mod[L][i] = UI_DS_NodeMod( dsd_plan.seed, L, i, ly[L].type[i] );
		}
	}
	if ( !was ) {
		UI_DS_DailySet( qfalse );
	}
}


/*
=======================================================================

SCORE

=======================================================================
*/

static void DSD_ScoreRun( dsdScore_t *sc ) {
	int		k, n, rec[DSD_REC_NUM], secs, nodeath;

	memset( sc, 0, sizeof( *sc ) );
	n = RL_KVInt( &rl_run, "dly_n" );
	if ( n > DSD_MAX_RECS ) {
		n = DSD_MAX_RECS;
	}
	nodeath = 0;
	for ( k = 0; k < n; k++ ) {
		memset( rec, 0, sizeof( rec ) );
		if ( RL_ParseInts( RL_KVGet( &rl_run, va( "dly_r%i", k ) ), rec, DSD_REC_NUM ) < DSD_REC_NUM - 1 ) {
			continue;
		}
		sc->kills += rec[DSD_REC_KILLS];
		sc->deaths += rec[DSD_REC_DEATHS];
		sc->ptsKills += rec[DSD_REC_KILLS] * DSD_PTS_KILL;
		if ( !rec[DSD_REC_WON] ) {
			continue;
		}
		sc->floors++;
		sc->styleSum += rec[DSD_REC_STYLE];
		sc->ptsStyle += rec[DSD_REC_STYLE] * DSD_PTS_STYLE;
		if ( rec[DSD_REC_FLOOR] >= DSD_FLOORS ) {
			sc->ptsFloors += DSD_PTS_BOSS;
			sc->bossLeft = rec[DSD_REC_BOSSLEFT];
			sc->ptsSpeed += ( rec[DSD_REC_BOSSLEFT] / 1000 ) * DSD_PTS_BOSSSEC;
		}
		else {
			sc->ptsFloors += DSD_PTS_FLOOR;
			secs = DSD_PAR_SECS - rec[DSD_REC_SECS];
			if ( secs > 0 ) {
				sc->ptsSpeed += secs * DSD_PTS_SEC;
			}
		}
		if ( !rec[DSD_REC_DEATHS] ) {
			nodeath += DSD_PTS_DEATHLESS;
		}
	}
	sc->ptsNoDeath = nodeath;
	if ( sc->floors >= DSD_FLOORS && sc->deaths == 0 ) {
		sc->ptsNoDeath += DSD_PTS_FLAWLESS;
	}
	sc->total = sc->ptsFloors + sc->ptsKills + sc->ptsStyle + sc->ptsSpeed + sc->ptsNoDeath;
}


/*
=======================================================================

PERSONAL BESTS (rearmed_daily.txt)

=======================================================================
*/

static void DSD_LoadPB( void ) {
	RL_KVLoad( &dsd_pb, DSD_PB_FILE );
}

static void DSD_SavePB( void ) {
	RL_KVSet( &dsd_pb, "version", "1" );
	RL_KVSave( &dsd_pb, DSD_PB_FILE );
}

static int DSD_PBInt( const char *key ) {
	return RL_KVInt( &dsd_pb, key );
}

// today's best and attempts (0 0 when today has none)
static void DSD_Today( int date, int *best, int *attempts ) {
	int		v[3];

	*best = *attempts = 0;
	if ( RL_ParseInts( RL_KVGet( &dsd_pb, "today" ), v, 3 ) == 3 && v[0] == date ) {
		*best = v[1];
		*attempts = v[2];
	}
}

// the streak as it stands today: days in a row ending today or yesterday with a finished attempt
static int DSD_Streak( int today ) {
	int		last;

	last = DSD_PBInt( "streak_last" );
	if ( !DSD_ValidDate( last ) ) {
		return 0;
	}
	if ( DSD_DayNumber( today ) - DSD_DayNumber( last ) > 1 ) {
		return 0;
	}
	return DSD_PBInt( "streak" );
}

static void DSD_TopGet( int i, int *score, int *date, int *floors ) {
	int		v[3];

	*score = *date = *floors = 0;
	if ( RL_ParseInts( RL_KVGet( &dsd_pb, va( "top%i", i ) ), v, 3 ) == 3 ) {
		*score = v[0];
		*date = v[1];
		*floors = v[2];
	}
}

// drops the oldest d<date> keys beyond DSD_MAX_DATES
static void DSD_PruneDates( void ) {
	int		i, count, oldest, oldIdx, d;

	while ( 1 ) {
		count = 0;
		oldest = 0x7fffffff;
		oldIdx = -1;
		for ( i = 0; i < dsd_pb.num; i++ ) {
			if ( dsd_pb.key[i][0] == 'd' && dsd_pb.key[i][1] >= '0' && dsd_pb.key[i][1] <= '9' ) {
				count++;
				d = atoi( dsd_pb.key[i] + 1 );
				if ( d < oldest ) {
					oldest = d;
					oldIdx = i;
				}
			}
		}
		if ( count <= DSD_MAX_DATES || oldIdx < 0 ) {
			return;
		}
		for ( i = oldIdx; i < dsd_pb.num - 1; i++ ) {
			Q_strncpyz( dsd_pb.key[i], dsd_pb.key[i + 1], RL_KEY_LEN );
			Q_strncpyz( dsd_pb.val[i], dsd_pb.val[i + 1], RL_VAL_LEN );
		}
		dsd_pb.num--;
	}
}

/*
=================
DSD_Record

The loaded daily run (rl_run) is over or abandoned: its score goes into the table, once
=================
*/
static void DSD_Record( void ) {
	dsdScore_t	sc;
	int			date, best, attempts, i, j, s, d, f, rank, last, days;
	char		list[RL_VAL_LEN];
	char		key[16];

	date = RL_KVInt( &rl_run, "daily" );
	if ( !DSD_ValidDate( date ) || RL_KVInt( &rl_run, "dly_rec" ) ) {
		return;
	}
	DSD_ScoreRun( &sc );
	DSD_LoadPB();

	// the date's best
	dsd_lastPrevBest = DSD_PBInt( va( "d%i", date ) );
	if ( !RL_KVGet( &dsd_pb, va( "d%i", date ) )[0] || sc.total > dsd_lastPrevBest ) {
		RL_KVSetInt( &dsd_pb, va( "d%i", date ), sc.total );
	}
	DSD_PruneDates();

	// today: best, attempts, the attempt list, the best one's breakdown
	DSD_Today( date, &best, &attempts );
	dsd_lastBestToday = ( attempts == 0 || sc.total > best );
	d = 0;
	RL_ParseInts( RL_KVGet( &dsd_pb, "today" ), &d, 1 );
	if ( date >= d ) {		// (an older day's attempt scored late leaves today alone)
		if ( !attempts ) {
			RL_KVSet( &dsd_pb, "today_list", "" );
		}
		if ( dsd_lastBestToday ) {
			best = sc.total;
			RL_KVSet( &dsd_pb, "today_bd", va( "%i %i %i %i %i %i", sc.floors, sc.ptsFloors, sc.ptsKills, sc.ptsStyle, sc.ptsSpeed, sc.ptsNoDeath ) );
		}
		RL_KVSet( &dsd_pb, "today", va( "%i %i %i", date, best, attempts + 1 ) );
		Q_strncpyz( list, RL_KVGet( &dsd_pb, "today_list" ), sizeof( list ) );
		if ( strlen( list ) < sizeof( list ) - 16 ) {
			Q_strcat( list, sizeof( list ), va( list[0] ? ",%i" : "%i", sc.total ) );
			RL_KVSet( &dsd_pb, "today_list", list );
		}
	}

	// the all-time top 10 (every attempt can enter)
	rank = 0;
	for ( i = 0; i < DSD_TOP; i++ ) {
		DSD_TopGet( i, &s, &d, &f );
		if ( !s || sc.total > s ) {
			rank = i + 1;
			break;
		}
	}
	if ( rank && sc.total > 0 ) {
		for ( j = DSD_TOP - 1; j >= rank; j-- ) {
			Com_sprintf( key, sizeof( key ), "top%i", j );		// (one live va() per call: it has two buffers)
			RL_KVSet( &dsd_pb, key, RL_KVGet( &dsd_pb, va( "top%i", j - 1 ) ) );
		}
		Com_sprintf( key, sizeof( key ), "top%i", rank - 1 );
		RL_KVSet( &dsd_pb, key, va( "%i %i %i", sc.total, date, sc.floors ) );
	}
	else {
		rank = 0;
	}

	// the streak: days in a row with a finished attempt
	last = DSD_PBInt( "streak_last" );
	if ( !DSD_ValidDate( last ) ) {
		RL_KVSetInt( &dsd_pb, "streak", 1 );
		RL_KVSetInt( &dsd_pb, "streak_last", date );
	}
	else if ( date > last ) {
		days = DSD_DayNumber( date ) - DSD_DayNumber( last );
		RL_KVSetInt( &dsd_pb, "streak", days == 1 ? DSD_PBInt( "streak" ) + 1 : 1 );
		RL_KVSetInt( &dsd_pb, "streak_last", date );
	}
	if ( DSD_PBInt( "streak" ) > DSD_PBInt( "streak_best" ) ) {
		RL_KVSetInt( &dsd_pb, "streak_best", DSD_PBInt( "streak" ) );
	}
	RL_KVSetInt( &dsd_pb, "attempts", DSD_PBInt( "attempts" ) + 1 );
	DSD_SavePB();

	dsd_last = sc;
	dsd_lastDate = date;
	dsd_lastTopRank = rank;

	RL_KVSetInt( &rl_run, "dly_rec", 1 );
	RL_KVSetInt( &rl_run, "dly_score", sc.total );
	RL_SaveRun();
	Com_Printf( "DAILY: %s attempt over: %i points (%i floors, %i kills, %i deaths)%s, top 10 rank %i\n", DSD_DateString( date ),
		sc.total, sc.floors, sc.kills, sc.deaths, dsd_lastBestToday ? ", best today" : "", rank );
}


/*
=======================================================================

STARTING AN ATTEMPT

=======================================================================
*/

static void DSD_Csv( char *out, int size, const int *v, int n ) {
	int		i, len;

	out[0] = 0;
	len = 0;
	for ( i = 0; i < n; i++ ) {
		Com_sprintf( out + len, size - len, i ? ",%i" : "%i", v[i] );
		len += strlen( out + len );
	}
}

/*
=================
DSD_Start

A fresh attempt of today's daily in rearmed_daily_run.txt (the old one, if any, scored first)
=================
*/
static void DSD_Start( void ) {
	const dsCharInfo_t	*ch;
	int					cards[RL_MAX_CARDS], pv[RLP_NUM], wl[DS_NUM_WEAPONS];
	int					n, k, i, today, seed, c;
	char				buf[RL_VAL_LEN];

	UI_DS_DailySet( qtrue );
	RL_LoadRun();
	if ( RL_KVInt( &rl_run, "active" ) && RL_KVInt( &rl_run, "dly_n" ) > 0 ) {
		DSD_Record();		// restarting: the attempt counts as it stands
	}
	today = UI_DS_DailyToday();
	seed = DSD_Seed( today );
	c = DSD_Character( seed );
	ch = &ds_characters[c];

	// the character's kit (= UI_DS_StartRunKeys, without the unlock check) + the day's ability
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
	cards[n++] = DSD_Ability( seed, c );
	if ( ch->startWeapon > 0 && ch->startWeapon < DS_NUM_WEAPONS ) {
		wl[ch->startWeapon] = ch->startLevel > 0 ? ch->startLevel : 1;
	}

	rl_run.num = 0;
	RL_KVSetInt( &rl_run, "version", DS_RUN_VERSION );
	RL_KVSetInt( &rl_run, "active", 1 );
	RL_KVSetInt( &rl_run, "seed", seed );
	RL_KVSetInt( &rl_run, "daily", today );
	RL_KVSetInt( &rl_run, "difficulty", DSD_DIFFICULTY );
	UI_DS_EconStartRun( DSD_DIFFICULTY );
	RL_KVSetInt( &rl_run, "lives", RL_BaseLives( DSD_DIFFICULTY ) );
	RL_KVSetInt( &rl_run, "floor", 0 );
	RL_KVSetInt( &rl_run, "skulls", 0 );
	RL_KVSetInt( &rl_run, "kills", 0 );
	RL_KVSetInt( &rl_run, "time", 0 );
	RL_KVSet( &rl_run, "result", "none" );
	RL_KVSet( &rl_run, "offer", "" );
	RL_KVSet( &rl_run, "map", "" );
	RL_KVSetInt( &rl_run, "rerolls", 0 );
	RL_KVSetInt( &rl_run, "character", c );
	DSD_Csv( buf, sizeof( buf ), cards, n );
	RL_KVSet( &rl_run, "cards", buf );
	RL_KVSetInt( &rl_run, "startcards", n );
	DSD_Csv( buf, sizeof( buf ), pv, RLP_NUM );
	RL_KVSet( &rl_run, "pv", buf );
	DSD_Csv( buf, sizeof( buf ), wl, DS_NUM_WEAPONS );
	RL_KVSet( &rl_run, "wl", buf );
	for ( i = 0; i < DSU_NUM; i++ ) {
		RL_KVSetInt( &rl_run, ds_upgrades[i].key, 0 );		// no permanent upgrades: the same for everyone
	}
	RL_KVSetInt( &rl_run, "dly_n", 0 );
	RL_SaveRun();
	RL_LoadRun();
	Com_Printf( "DAILY: new attempt, %s, seed %i, %s + %s\n", DSD_DateString( today ), seed, ch->name, bg_rlCardNames[cards[n - 1]] );
	RL_MapMenu();
}


/*
=======================================================================

RESULT SCREEN

=======================================================================
*/

static void DSD_ResultEvent( void *ptr, int event ) {
	if ( event != QM_ACTIVATED ) {
		return;
	}
	switch ( ((menucommon_s *)ptr)->id ) {
	case ID_DSD_AGAIN:
		if ( !RL_InGame() ) {
			UI_PopMenu();
		}
		DSD_Start();
		break;
	case ID_DSD_DONE:
		if ( RL_InGame() ) {
			// leave the server: the main menu brings the hub and this screen back
			UI_DS_DailySet( qtrue );
			trap_Cvar_Set( "ui_rlOpenHub", "1" );
			UI_ForceMenuOff();
			trap_Cmd_ExecuteText( EXEC_APPEND, "disconnect\n" );
		}
		else {
			UI_PopMenu();
			if ( uis.activemenu == &s_dsd.menu ) {
				rl_hubDirty = qtrue;		// the daily screen below: it refreshes itself
			}
			else {
				UI_DS_DailyMenu();
			}
		}
		break;
	}
}

static void DSD_BreakdownLine( float y, const char *label, const char *detail, int pts, const float *col ) {
	RL_Text( 320 - 230, y, label, 8, 13, col, 0 );
	RL_Text( 320 - 40, y + 2, detail, 6, 10, rl_colDim, 0 );
	RL_TextS( 320 + 230, y, DSD_Num( pts ), 8, 13, col, 2 );
}

static void DSD_ResultDraw( void ) {
	dsdScore_t	*sc;
	int			best, attempts, today, left;
	float		y;
	vec4_t		c;

	sc = &dsd_last;
	RL_Backdrop( qtrue );		// (opaque: the floor's own end banner is behind it in game)
	RL_Header( "DAILY RESULT", va( "The daily descent of %s", DSD_DateString( dsd_lastDate ) ) );

	RL_TextS( 320, 76, DSD_Num( sc->total ), 26, 40, rl_colOrange, 1 );
	Vector4Copy( dsd_colGold, c );
	c[3] = 0.65f + 0.35f * RL_Pulse( 0.006f );
	if ( dsd_lastBestToday && dsd_lastPrevBest > 0 ) {
		RL_TextS( 320, 124, va( "NEW BEST TODAY  -  BEAT %s", DSD_Num( dsd_lastPrevBest ) ), 9, 14, c, 1 );
	}
	else if ( dsd_lastBestToday ) {
		RL_TextS( 320, 124, "TODAY'S FIRST SCORE", 9, 14, c, 1 );
	}
	else {
		DSD_LoadPB();
		DSD_Today( dsd_lastDate, &best, &attempts );
		RL_TextS( 320, 124, va( "TODAY'S BEST STAYS %s  -  %s SHORT", DSD_Num( best ), DSD_Num( best - sc->total ) ), 8, 13, rl_colDim, 1 );
	}
	if ( dsd_lastTopRank ) {
		RL_TextS( 320, 142, va( "ALL-TIME TOP 10: #%i", dsd_lastTopRank ), 8, 12, rl_colGood, 1 );
	}

	RL_Frame( 320 - 250, 162, 500, 176, rl_colPanel, rl_colLine );
	y = 172;
	DSD_BreakdownLine( y, "FLOORS CLEARED", va( "%i of %i%s", sc->floors, DSD_FLOORS, sc->floors >= DSD_FLOORS ? "  -  the boss fell" : "" ),
		sc->ptsFloors, rl_colBone );
	y += 24;
	DSD_BreakdownLine( y, "KILLS", va( "%i x %i", sc->kills, DSD_PTS_KILL ), sc->ptsKills, rl_colBone );
	y += 24;
	DSD_BreakdownLine( y, "STYLE", va( "rank sum %i x %i", sc->styleSum, DSD_PTS_STYLE ), sc->ptsStyle, rl_colBone );
	y += 24;
	left = sc->bossLeft / 1000;
	DSD_BreakdownLine( y, "SPEED", sc->floors >= DSD_FLOORS ? va( "boss clock %i:%02i left", left / 60, left % 60 ) : "under 4:00 a floor",
		sc->ptsSpeed, rl_colBone );
	y += 24;
	DSD_BreakdownLine( y, "NO-DEATH", sc->deaths ? va( "%i death%s", sc->deaths, sc->deaths == 1 ? "" : "s" ) : "never died",
		sc->ptsNoDeath, sc->ptsNoDeath ? rl_colGood : rl_colBone );
	y += 28;
	UI_FillRect( 320 - 230, y - 4, 460, 1, rl_colLine );
	DSD_BreakdownLine( y, "TOTAL", "", sc->total, rl_colOrange );

	DSD_LoadPB();
	today = UI_DS_DailyToday();
	DSD_Today( today, &best, &attempts );
	if ( attempts ) {
		RL_Text( 320, 346, va( "TODAY: BEST %s IN %i ATTEMPT%s  |  STREAK %i DAY%s", DSD_Num( best ), attempts, attempts == 1 ? "" : "S",
			DSD_Streak( today ), DSD_Streak( today ) == 1 ? "" : "S" ), 7, 11, rl_colDim, 1 );
	}
	Menu_Draw( &s_dsdRes.menu );
	RL_Footer( "MOUSE, SPACE / TAB, WASD OR GAMEPAD" );
}

static void DSD_ResultMenu( void ) {
	memset( &s_dsdRes, 0, sizeof( s_dsdRes ) );
	RL_SetupMenu( &s_dsdRes.menu, DSD_ResultDraw, RL_InGame() );
	RL_InitButton( &s_dsdRes.again, ID_DSD_AGAIN, 320 - 250, 372, 240, 44, "TRY AGAIN", DSD_ResultEvent );
	Q_strncpyz( s_dsdRes.again.sub, "Same floors, same draws: beat it", sizeof( s_dsdRes.again.sub ) );
	RL_InitButton( &s_dsdRes.done, ID_DSD_DONE, 320 + 10, 372, 240, 44, "DONE", DSD_ResultEvent );
	Q_strncpyz( s_dsdRes.done.sub, "Back to the daily screen", sizeof( s_dsdRes.done.sub ) );
	Menu_AddItem( &s_dsdRes.menu, &s_dsdRes.again );
	Menu_AddItem( &s_dsdRes.menu, &s_dsdRes.done );
	if ( RL_InGame() ) {
		uis.menusp = 0;
	}
	UI_PushMenu( &s_dsdRes.menu );
}

// the loaded daily run is over: score it (once) and show the result
static void DSD_Finish( void ) {
	if ( !RL_KVInt( &rl_run, "dly_rec" ) ) {
		DSD_Record();
	}
	else {
		DSD_ScoreRun( &dsd_last );		// (already recorded: show it again)
		dsd_lastDate = RL_KVInt( &rl_run, "daily" );
		dsd_lastTopRank = 0;
		dsd_lastBestToday = qfalse;
	}
	DSD_ResultMenu();
}


/*
=======================================================================

THE DAILY SCREEN

=======================================================================
*/

static void DSD_RestartAction( qboolean result ) {
	if ( result ) {
		DSD_Start();
	}
}

static void DSD_RestartDraw( void ) {
	UI_DrawProportionalString( 320, 230, "THE RUNNING ATTEMPT IS SCORED AS IT STANDS", UI_CENTER|UI_SMALLFONT, color_red );
}

static void DSD_Event( void *ptr, int event ) {
	if ( event != QM_ACTIVATED ) {
		return;
	}
	switch ( ((menucommon_s *)ptr)->id ) {
	case ID_DSD_START:
		if ( s_dsd.active ) {
			RL_OpenRun();		// (the daily's run: the floor to resume, or the map)
		}
		else {
			DSD_Start();
		}
		break;
	case ID_DSD_RESTART:
		UI_ConfirmMenu( "START A NEW ATTEMPT?", DSD_RestartDraw, DSD_RestartAction );
		break;
	case ID_RL_BACK:
		UI_DS_DailySet( qfalse );		// the hub shows the real descent again
		UI_PopMenu();
		break;
	}
}

static void DSD_PanelTitle( float x, float y, float w, const char *title ) {
	RL_TextS( x + w / 2, y, title, 8, 12, rl_colOrange, 1 );
	UI_FillRect( x + 10, y + 15, w - 20, 1, rl_colLine );
}

static void DSD_DrawLeft( float x, float y, float w, float h ) {
	const dsCharInfo_t	*ch;
	float				ty;

	ch = &ds_characters[dsd_plan.character];
	RL_Frame( x, y, w, h, rl_colPanel, rl_colLine );
	DSD_PanelTitle( x, y + 6, w, "TODAY'S DESCENT" );
	ty = y + 30;
	RL_Pic( x + 10, ty, 48, 48, dsd_charIcons[dsd_plan.character], NULL );
	UI_DrawProportionalString( (int)( x + 66 ), (int)ty, ch->name, UI_LEFT|UI_SMALLFONT, rl_colBone );
	RL_TextWrap( x + 66, ty + 22, w - 74, ch->desc, 6, 10, rl_colDim, 3 );
	ty += 58;
	RL_Text( x + 10, ty, "FREE ABILITY", 6, 9, rl_colDim, 0 );
	RL_TextS( x + 10, ty + 11, bg_rlCardNames[dsd_plan.ability], 8, 12, dsd_colGold, 0 );
	ty += 32;
	RL_Text( x + 10, ty, "HURT ME PLENTY  -  2 LIVES", 7, 11, rl_colBone, 0 );
	ty += 15;
	RL_Text( x + 10, ty, va( "ACT I: %i FLOORS", DSD_FLOORS ), 7, 11, rl_colBone, 0 );
	ty += 15;
	RL_Text( x + 10, ty, va( "FLOOR 6: %s", ds_bosses[0].name ), 7, 11, dsd_typeColors[RLN_BOSS], 0 );
	ty += 13;
	RL_Text( x + 10, ty, va( "TIMED 1V1, %i:%02i ON THE CLOCK", DSD_BOSS_TIME / 60, DSD_BOSS_TIME % 60 ), 6, 10, dsd_typeColors[RLN_BOSS], 0 );
	ty += 18;
	ty = RL_TextWrap( x + 10, ty, w - 20, "No upgrades, perks or skulls. Your own descent is untouched.", 6, 10, rl_colDim, 3 );
	ty += 6;
	RL_Text( x + 10, ty, "SCORE", 6, 9, rl_colOrange, 0 );
	RL_TextWrap( x + 10, ty + 12, w - 20, va( "%i a floor (boss %i), %i a kill, style, speed, no deaths",
		DSD_PTS_FLOOR, DSD_PTS_BOSS, DSD_PTS_KILL ), 6, 10, rl_colDim, 3 );
}

static void DSD_DrawFloors( float x, float y, float w, float h ) {
	float		rowH, cellW, cx, ty;
	int			L, i, t, maxChars;
	const char	*s;
	char		name[64];
	vec4_t		c;

	RL_Frame( x, y, w, h, rl_colPanel, rl_colLine );
	DSD_PanelTitle( x, y + 6, w, va( "THE FLOORS  -  SEED %i", dsd_plan.seed ) );
	rowH = ( h - 32 ) / DSD_FLOORS;
	for ( L = 0; L < DSD_FLOORS; L++ ) {
		ty = y + 28 + L * rowH;
		if ( L ) {
			UI_FillRect( x + 8, ty - 2, w - 16, 1, rl_colShade );
		}
		RL_TextS( x + 10, ty + rowH * 0.5f - 9, va( "%i", L + 1 ), 12, 18, L == DSD_FLOORS - 1 ? dsd_typeColors[RLN_BOSS] : rl_colBone, 0 );
		cellW = ( w - 40 ) / ( dsd_plan.n[L] > 0 ? dsd_plan.n[L] : 1 );
		maxChars = (int)( ( cellW - 6 ) / 6 );
		for ( i = 0; i < dsd_plan.n[L]; i++ ) {
			cx = x + 36 + i * cellW;
			t = dsd_plan.type[L][i];
			if ( t < 0 || t >= RLN_NUM ) {
				t = 0;
			}
			RL_Text( cx, ty + 2, dsd_typeNames[t], 7, 11, dsd_typeColors[t], 0 );
			if ( t == RLN_BOSS ) {
				RL_Text( cx + 7 * 5 + 6, ty + 3, ds_bosses[0].name, 6, 10, dsd_typeColors[t], 0 );
			}
			s = t == RLN_VAULT ? "a hidden vault" : UI_DSM_LongName( dsd_plan.map[L][i] );
			if ( !s || !s[0] ) {
				s = dsd_plan.map[L][i];
			}
			Q_strncpyz( name, s, sizeof( name ) );
			if ( maxChars > 3 && maxChars < (int)sizeof( name ) ) {
				name[maxChars] = 0;
			}
			RL_Text( cx, ty + 15, name, 6, 9, rl_colBone, 0 );
			if ( dsd_plan.mod[L][i] > 0 && dsd_plan.mod[L][i] < DSMOD_NUM ) {
				Vector4Copy( dsd_colGold, c );
				RL_Text( cx, ty + 26, va( "+ %s", ds_modifiers[dsd_plan.mod[L][i]].name ), 5, 8, c, 0 );
			}
		}
	}
}

static void DSD_DrawRight( float x, float y, float w, float h, int today ) {
	int		best, attempts, i, s, d, f, streak, target;
	float	ty;
	vec4_t	c;

	DSD_LoadPB();
	RL_Frame( x, y, w, h, rl_colPanel, rl_colLine );
	DSD_PanelTitle( x, y + 6, w, "PERSONAL BESTS" );
	DSD_Today( today, &best, &attempts );
	ty = y + 28;
	RL_Text( x + 10, ty, "TODAY", 6, 9, rl_colDim, 0 );
	if ( attempts ) {
		RL_TextS( x + 10, ty + 11, DSD_Num( best ), 14, 20, rl_colOrange, 0 );
		RL_Text( x + w - 10, ty + 16, va( "%i attempt%s", attempts, attempts == 1 ? "" : "s" ), 6, 10, rl_colDim, 2 );
	}
	else {
		RL_TextS( x + 10, ty + 14, "NOT PLAYED YET", 8, 12, rl_colBone, 0 );
	}
	ty += 38;

	// today's target: your own best today, else the score that enters your top 10
	DSD_TopGet( DSD_TOP - 1, &target, &d, &f );
	Vector4Copy( dsd_colGold, c );
	if ( attempts ) {
		RL_Text( x + 10, ty, va( "TARGET: BEAT %s", DSD_Num( best ) ), 7, 11, c, 0 );
	}
	else if ( target > 0 ) {
		RL_Text( x + 10, ty, va( "TARGET: %s ENTERS YOUR TOP 10", DSD_Num( target + 1 ) ), 7, 11, c, 0 );
	}
	else {
		RL_Text( x + 10, ty, "TARGET: CLEAR THE ACT", 7, 11, c, 0 );
	}
	ty += 18;
	streak = DSD_Streak( today );
	RL_Text( x + 10, ty, va( "STREAK %i DAY%s  (BEST %i)", streak, streak == 1 ? "" : "S", DSD_PBInt( "streak_best" ) ), 7, 11,
		streak > 0 ? rl_colGood : rl_colBone, 0 );
	ty += 22;

	RL_Text( x + 10, ty, "ALL-TIME TOP 10", 6, 9, rl_colOrange, 0 );
	ty += 13;
	for ( i = 0; i < DSD_TOP; i++ ) {
		DSD_TopGet( i, &s, &d, &f );
		if ( !s ) {
			RL_Text( x + 10, ty, va( "%2i  -", i + 1 ), 6, 10, rl_colDark, 0 );
		}
		else {
			RL_Text( x + 10, ty, va( "%2i  %s", i + 1, DSD_Num( s ) ), 6, 10, d == today ? rl_colOrange : rl_colBone, 0 );
			RL_Text( x + w - 10, ty, va( "%s  %i/%i", DSD_DateString( d ), f, DSD_FLOORS ), 6, 10, rl_colDim, 2 );
		}
		ty += 12;
		if ( ty > y + h - 12 ) {
			break;
		}
	}
}

// the buttons for the daily run as it stands (start / continue + new attempt)
static void DSD_Refresh( void ) {
	int		today, floor, lives;

	rl_hubDirty = qfalse;
	today = UI_DS_DailyToday();
	RL_LoadRun();
	s_dsd.active = RL_KVInt( &rl_run, "active" ) && RL_KVInt( &rl_run, "daily" ) == today;
	s_dsd.start.icon = dsd_charIcons[dsd_plan.character];
	if ( s_dsd.active ) {
		floor = RL_KVInt( &rl_run, "floor" );
		lives = RL_KVInt( &rl_run, "lives" );
		s_dsd.start.bm.generic.x = 320 - 250;
		s_dsd.start.bm.width = 240;
		Q_strncpyz( s_dsd.start.label, "CONTINUE", sizeof( s_dsd.start.label ) );
		Com_sprintf( s_dsd.start.sub, sizeof( s_dsd.start.sub ), "Floor %i next  |  %i %s left", floor + 1, lives, lives == 1 ? "life" : "lives" );
		Q_strncpyz( s_dsd.restart.sub, "Scores this one, starts over", sizeof( s_dsd.restart.sub ) );
		s_dsd.restart.bm.generic.flags &= ~( QMF_HIDDEN | QMF_INACTIVE );
	}
	else {
		s_dsd.start.bm.generic.x = 320 - 160;
		s_dsd.start.bm.width = 320;
		Q_strncpyz( s_dsd.start.label, "START", sizeof( s_dsd.start.label ) );
		Q_strncpyz( s_dsd.start.sub, "Floor 1 of today's descent", sizeof( s_dsd.start.sub ) );
		s_dsd.restart.bm.generic.flags |= QMF_HIDDEN | QMF_INACTIVE;
	}
	// (Menu_AddItem sized the hit box: follow the new geometry)
	s_dsd.start.bm.generic.left = s_dsd.start.bm.generic.x;
	s_dsd.start.bm.generic.right = s_dsd.start.bm.generic.x + s_dsd.start.bm.width;
}

static void DSD_Draw( void ) {
	float	side, gap, left, center, top, h;
	int		today;

	if ( rl_hubDirty ) {
		DSD_Refresh();		// back from the floor map / a confirm screen
	}
	today = UI_DS_DailyToday();
	if ( today != dsd_plan.date ) {
		DSD_BuildPlan( today );		// the clock passed midnight
	}
	RL_Backdrop( !RL_InGame() );
	RL_Header( "DAILY CHALLENGE", va( "%s  -  the same descent all day, as often as you like: the best attempt counts", DSD_DateString( today ) ) );

	side = 150 + ( rl_ext < 300 ? rl_ext : 300 ) * 0.5f;
	gap = 10;
	left = -rl_ext + 8;
	center = 640 + 2 * rl_ext - 16 - 2 * side - 2 * gap;
	top = 70;
	h = 284;
	DSD_DrawLeft( left, top, side, h );
	DSD_DrawFloors( left + side + gap, top, center, h );
	DSD_DrawRight( left + side + gap + center + gap, top, side, h, today );

	Menu_Draw( &s_dsd.menu );
	RL_Text( 320, 420, "Unlimited attempts. No skulls, no unlocks, no records outside this screen.", 6, 10, rl_colDim, 1 );
	RL_Footer( "MOUSE, SPACE / TAB, WASD OR GAMEPAD" );
}

/*
=================
UI_DS_DailyMenu
=================
*/
void UI_DS_DailyMenu( void ) {
	int		today, i;

	UI_DS_DailySet( qtrue );
	memset( &s_dsd, 0, sizeof( s_dsd ) );
	RL_SetupMenu( &s_dsd.menu, DSD_Draw, RL_InGame() );
	for ( i = 0; i < CHR_NUM; i++ ) {
		dsd_charIcons[i] = trap_R_RegisterShaderNoMip( va( "models/players/%s/icon_default", ds_characters[i].model ) );
	}
	today = UI_DS_DailyToday();

	// an attempt of an older day: scored for its own date, then gone
	RL_LoadRun();
	if ( RL_KVInt( &rl_run, "active" ) && RL_KVInt( &rl_run, "daily" ) != today ) {
		if ( RL_KVInt( &rl_run, "dly_n" ) > 0 ) {
			DSD_Record();
		}
		RL_KVSetInt( &rl_run, "active", 0 );
		RL_SaveRun();
	}
	else if ( !RL_KVInt( &rl_run, "active" ) && RL_KVInt( &rl_run, "daily" ) && !RL_KVInt( &rl_run, "dly_rec" ) && RL_KVInt( &rl_run, "dly_n" ) > 0 ) {
		DSD_Record();		// a finished attempt that never reached its result screen
	}
	DSD_BuildPlan( today );

	RL_InitButton( &s_dsd.start, ID_DSD_START, 320 - 250, 362, 240, 46, "START", DSD_Event );
	RL_InitButton( &s_dsd.restart, ID_DSD_RESTART, 320 + 10, 362, 240, 46, "NEW ATTEMPT", DSD_Event );
	RL_InitButton( &s_dsd.back, ID_RL_BACK, 8 - rl_ext, 440, 120, 28, "BACK", DSD_Event );
	s_dsd.back.center = qtrue;
	Menu_AddItem( &s_dsd.menu, &s_dsd.start );
	Menu_AddItem( &s_dsd.menu, &s_dsd.restart );
	Menu_AddItem( &s_dsd.menu, &s_dsd.back );
	DSD_Refresh();
	UI_PushMenu( &s_dsd.menu );
	Menu_SetCursorToItem( &s_dsd.menu, &s_dsd.start );
}


/*
=======================================================================

HOOKS

=======================================================================
*/

/*
=================
UI_DS_DailyOpenRun

UI_DS_OpenRun while the daily is on: a finished attempt gets its result, an interrupted
floor resumes; qfalse = the offer / floor map as usual
=================
*/
qboolean UI_DS_DailyOpenRun( void ) {
	RL_LoadRun();
	if ( !RL_KVInt( &rl_run, "active" ) || RL_KVInt( &rl_run, "dly_done" ) ) {
		DSD_Finish();
		return qtrue;
	}
	if ( RL_KVInt( &rl_run, "infloor" ) && !RL_KVGet( &rl_run, "offer" )[0] && RL_KVGet( &rl_run, "node_layer" )[0] ) {
		RL_LaunchNode( RL_KVInt( &rl_run, "node_layer" ), RL_KVInt( &rl_run, "node_index" ) );
		return qtrue;
	}
	return qfalse;
}

/*
=================
UI_DS_DailyMainMenu

UI_RL_MainMenuOpened: the flag goes off; returns whether a daily was on (the caller reopens
this screen over the hub when a daily floor was quit)
=================
*/
qboolean UI_DS_DailyMainMenu( void ) {
	qboolean	was;

	trap_Cvar_Register( NULL, DSD_FLAG_CVAR, "0", 0 );
	trap_Cvar_Register( NULL, DSD_DATE_CVAR, "", 0 );
	was = UI_DS_DailyOn();
	UI_DS_DailySet( qfalse );
	return was;
}

// the hub button's line (reads only rearmed_daily.txt)
const char *UI_DS_DailyHubLine( void ) {
	int		today, best, attempts, streak;

	today = UI_DS_DailyToday();
	DSD_LoadPB();
	DSD_Today( today, &best, &attempts );
	streak = DSD_Streak( today );
	if ( attempts ) {
		return va( "%s: best %s in %i %s  |  streak %i", DSD_DateString( today ), DSD_Num( best ), attempts,
			attempts == 1 ? "try" : "tries", streak );
	}
	return va( "%s: a new descent for everyone  |  streak %i", DSD_DateString( today ), streak );
}

/*
=================
UI_DS_DailyCommand

First in UI_RL_ConsoleCommand
=================
*/
qboolean UI_DS_DailyCommand( const char *cmd ) {
	char		arg[32];
	int			L, i, d, s, f, best, attempts;
	dsdScore_t	sc;

	if ( !Q_stricmp( cmd, "descent" ) || !Q_stricmp( cmd, "roguelike" ) ) {
		UI_DS_DailySet( qfalse );		// the real descent (never the daily's run file)
		return qfalse;
	}
	if ( !Q_stricmp( cmd, "rlover" ) && UI_DS_DailyOn() ) {
		if ( RL_InGame() ) {
			uis.menusp = 0;
		}
		RL_LoadRun();
		DSD_Finish();
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsdaily" ) ) {
		Q_strncpyz( arg, UI_Argv( 1 ), sizeof( arg ) );
		if ( RL_InGame() ) {
			uis.menusp = 0;
		}
		if ( !Q_stricmp( arg, "start" ) ) {
			DSD_Start();
		}
		else {
			UI_DS_DailyMenu();
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsdailyinfo" ) ) {
		d = atoi( UI_Argv( 1 ) );
		if ( !DSD_ValidDate( d ) ) {
			d = UI_DS_DailyToday();
		}
		DSD_BuildPlan( d );
		Com_Printf( "DAILY %i seed %i character %s ability %s\n", d, dsd_plan.seed, ds_characters[dsd_plan.character].name,
			bg_rlCardNames[dsd_plan.ability] );
		for ( L = 0; L < DSD_FLOORS; L++ ) {
			Com_Printf( "DAILY floor %i:", L + 1 );
			for ( i = 0; i < dsd_plan.n[L]; i++ ) {
				Com_Printf( " %s/%s%s%s", dsd_typeNames[dsd_plan.type[L][i]], dsd_plan.map[L][i], dsd_plan.mod[L][i] ? "/" : "",
					dsd_plan.mod[L][i] ? ds_modifiers[dsd_plan.mod[L][i]].key : "" );
			}
			Com_Printf( "\n" );
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsdailypb" ) ) {
		DSD_LoadPB();
		d = UI_DS_DailyToday();
		DSD_Today( d, &best, &attempts );
		Com_Printf( "DAILYPB today %i best %i attempts %i list %s streak %i (stored %i last %i best %i) total %i\n", d, best, attempts,
			RL_KVGet( &dsd_pb, "today_list" ), DSD_Streak( d ), DSD_PBInt( "streak" ), DSD_PBInt( "streak_last" ),
			DSD_PBInt( "streak_best" ), DSD_PBInt( "attempts" ) );
		for ( i = 0; i < DSD_TOP; i++ ) {
			DSD_TopGet( i, &s, &d, &f );
			if ( s ) {
				Com_Printf( "DAILYPB top %i: %i %i floors %i\n", i + 1, s, d, f );
			}
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsdailyscore" ) ) {
		if ( !UI_DS_DailyOn() ) {
			Com_Printf( "dsdailyscore: the daily isn't on (dsdaily first)\n" );
			return qtrue;
		}
		RL_LoadRun();
		DSD_ScoreRun( &sc );
		Com_Printf( "DAILYSCORE %i: floors %i (%i) kills %i (%i) style %i (%i) speed %i nodeath %i deaths %i\n", sc.total, sc.floors,
			sc.ptsFloors, sc.kills, sc.ptsKills, sc.styleSum, sc.ptsStyle, sc.ptsSpeed, sc.ptsNoDeath, sc.deaths );
		return qtrue;
	}
	return qfalse;
}
