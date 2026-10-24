/*
===========================================================================
REARMED -- DAILY DESCENT CHALLENGE, game side (round 4, en-daily). See bg_ds_daily.h.

- G_RL_Daily(): this floor belongs to the daily (cvar ds_daily at G_RL_Init, DESCENT, not
  a boss practice). g_roguelike.c then reads / writes rearmed_daily_run.txt and nothing
  else (RL_Save refuses other files, meta reads come back empty); skills, tally and ghost
  saves check G_RL_Daily() too; skill perks are off; the boss floor is always the timed 1v1.
- Seeded streams (independent of what the player does): the chest rolls (Loot_RollRewards,
  Loot_ChestPick) draw from a stream of run seed + floor; the card offers, the floor plan,
  the types / affixes / ambushes / modifiers were already seeded by run seed + floor.
- G_DS_DailyFloorEnd: the floor's record (run keys dly_r<n>, dly_n) for the UI's score; the
  6th floor won ends the run (no card offer).
===========================================================================
*/
#include "g_local.h"
#include "bg_ds_daily.h"

static qboolean	dlyOn;
static qboolean	dlyChestSeeded;
static unsigned	dlyChestState;

/*
=================
G_DS_DailyInit

Top of G_RL_Init (every map load)
=================
*/
void G_DS_DailyInit( void ) {
	dlyOn = level.rmMode == RM_MODE_ROGUELIKE && trap_Cvar_VariableIntegerValue( DSD_FLAG_CVAR ) != 0;
	dlyChestSeeded = qfalse;
	if ( dlyOn ) {
		G_LogPrintf( "Descent: DAILY CHALLENGE floor (%s, no meta / skills / tally / ghost written)\n", DSD_RUN_FILE );
	}
}

qboolean G_RL_Daily( void ) {
	return dlyOn && !G_RL_Practice();
}

// en-review: a floor that keeps nothing of the player's (boss practice / rematch, or the daily)
qboolean G_RL_NoSave( void ) {
	return G_RL_Practice() || G_RL_Daily();
}

/*
=================
G_DS_DailyRand

0..0x7fff. On a daily floor: the chest stream (run seed + floor), so the chests a
player finds roll the same for everyone that day. Elsewhere: rand().
=================
*/
int G_DS_DailyRand( void ) {
	if ( !G_RL_Daily() ) {
		return rand() & 0x7fff;
	}
	if ( !dlyChestSeeded ) {
		dlyChestState = (unsigned)atoi( G_RL_RunGet( "seed" ) ) * 2654435761u + (unsigned)G_RL_Floor() * 362437u + 0xC4E57u;
		dlyChestSeeded = qtrue;
	}
	dlyChestState = dlyChestState * 1103515245u + 12345u;
	return (int)( ( dlyChestState >> 16 ) & 0x7fff );
}

float G_DS_DailyRandom( void ) {
	return G_DS_DailyRand() / (float)0x7fff;
}

/*
=================
G_DS_DailyFloorEnd

RL_Finish, after G_DS_MetaFloorEnd (fst holds this floor's stats). Returns qtrue
when the daily is complete (its last floor won): the run ends, no cards.
=================
*/
qboolean G_DS_DailyFloorEnd( qboolean won, int styleRank, int kills, int msec, int floorNum, qboolean boss ) {
	int		fst[DSST_NUM];
	int		n, deaths, left;
	const char	*s;
	char	key[16];

	if ( !G_RL_Daily() ) {
		return qfalse;
	}
	memset( fst, 0, sizeof( fst ) );
	s = G_RL_RunGet( "fst" );
	for ( n = 0 ; *s && n < DSST_NUM ; n++ ) {
		fst[n] = atoi( s );
		while ( *s && *s != ',' ) {
			s++;
		}
		if ( *s == ',' ) {
			s++;
		}
	}
	deaths = fst[DSST_DEATHS];
	left = 0;
	if ( won && boss ) {
		left = G_DS_BossTimeLeft();
		if ( left < 0 ) {
			left = 0;
		}
	}
	n = atoi( G_RL_RunGet( "dly_n" ) );
	if ( n < 0 ) {
		n = 0;
	}
	if ( n < DSD_MAX_RECS ) {
		Com_sprintf( key, sizeof( key ), "dly_r%i", n );		// (one live va() per call: it has two buffers)
		G_RL_RunSet( key, va( "%i %i %i %i %i %i %i", floorNum, won ? 1 : 0, kills, won ? styleRank : 0,
			msec / 1000, deaths, left ) );
		G_RL_RunSet( "dly_n", va( "%i", n + 1 ) );
	}
	G_LogPrintf( "Daily: floor %i %s kills %i style %i secs %i deaths %i bossleft %i\n", floorNum, won ? "WON" : "LOST",
		kills, won ? styleRank : 0, msec / 1000, deaths, left );
	if ( won && floorNum >= DSD_FLOORS ) {
		G_RL_RunSet( "dly_done", "1" );
		G_LogPrintf( "Daily: challenge complete\n" );
		return qtrue;
	}
	return qfalse;
}
