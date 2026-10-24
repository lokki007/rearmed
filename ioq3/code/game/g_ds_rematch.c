/*
===========================================================================
REARMED -- DESCENT BOSS REMATCH (round 4, en-rematch), game side.

A rematch is a BOSS PRACTICE floor (G_RL_Practice: nothing is saved) whose practice file says
"rematch 1" and carries the modifier mask (rm_mods, game/bg_ds_rematch.h). This file answers
the boss code's questions (g_ds_boss.c: health, clock, attack pace, enrage, one life), keeps
health items off the map (G_RearmItemAllowed), halves the player's health limit
(G_RL_MaxHealthBonus), publishes the mask on CS_DESCENTBOSS for the HUD line and, at the end,
hands the result to the UI ("dsrmend ..."), which keeps the best times. It writes no file.
===========================================================================
*/
#include "g_local.h"
#include "bg_ds_rematch.h"

static int		rm_limit;			// the clock this rematch got (msec), for the kill time
static int		rm_fallenLogged;
static int		rm_glassSpawn;		// the last respawn time logged for GLASS

// the active modifier mask (-1 = not a rematch)
static int G_DS_RmMask( void ) {
	if ( !G_RL_Practice() || atoi( G_RL_RunGet( "rematch" ) ) != 1 ) {
		return -1;
	}
	return atoi( G_RL_RunGet( "rm_mods" ) ) & DSRM_ALL;
}

qboolean G_DS_Rematch( void ) {
	return G_DS_RmMask() >= 0;
}

static qboolean G_DS_RmHas( int bit ) {
	int		m = G_DS_RmMask();

	return m > 0 && ( m & bit ) != 0;
}

// IRON: the boss pool (G_DS_ActsInit, per phase segment)
float G_DS_RmHealthScale( void ) {
	return G_DS_RmHas( DSRM_IRON ) ? DSRM_IRON_HP : 1.0f;
}

// HALF CLOCK (G_DS_ActsInit, before the clock starts); logs the whole rematch setup once
int G_DS_RmTimeLimit( int limit ) {
	int		m = G_DS_RmMask(), i;
	char	names[128];

	if ( m < 0 ) {
		rm_limit = 0;
		return limit;
	}
	if ( m & DSRM_HALFCLOCK ) {
		limit /= 2;
		if ( limit < 30000 ) {
			limit = 30000;
		}
	}
	rm_limit = limit;
	rm_fallenLogged = 0;
	rm_glassSpawn = 0;
	names[0] = 0;
	for ( i = 0 ; i < DSRM_NUM ; i++ ) {
		if ( m & dsrm_mods[i].bit ) {
			Q_strcat( names, sizeof( names ), va( " %s", dsrm_mods[i].name ) );
		}
	}
	G_LogPrintf( "Rematch: boss %i mods %i heat %i (%s ) hp x%.2f clock %i ms pace x%.2f enraged %i onelife %i nohealth %i glass %i prev %i\n",
		atoi( G_RL_RunGet( "practice" ) ), m, BG_DSRM_Heat( m ), names[0] ? names : " none", G_DS_RmHealthScale(), limit,
		G_DS_RmPace(), ( m & DSRM_ENRAGED ) != 0, ( m & DSRM_ONELIFE ) != 0, ( m & DSRM_NOHEALTH ) != 0, ( m & DSRM_GLASS ) != 0,
		atoi( G_RL_RunGet( "rm_prev" ) ) );
	return limit;
}

// FURY: the gap between scripted attacks and their cooldowns
float G_DS_RmPace( void ) {
	return G_DS_RmHas( DSRM_FURY ) ? DSRM_FURY_PACE : 1.0f;
}

// ENRAGED: the last-minute enrage from the start of the clock
qboolean G_DS_RmEnraged( void ) {
	return G_DS_RmHas( DSRM_ENRAGED );
}

// ONE LIFE: the player died once: the clock's time-up path ends the floor (a loss)
qboolean G_DS_RmFallen( int playerDeaths ) {
	if ( playerDeaths <= 0 || !G_DS_RmHas( DSRM_ONELIFE ) ) {
		return qfalse;
	}
	if ( !rm_fallenLogged ) {
		rm_fallenLogged = 1;
		G_LogPrintf( "Rematch: ONE LIFE - the player fell (deaths %i), the rematch is lost\n", playerDeaths );
	}
	return qtrue;
}

// NO HEALTH: G_RearmItemAllowed (map items; G_RL_Init has read the practice file by then)
qboolean G_DS_RmItemAllowed( gitem_t *item ) {
	if ( item && item->giType == IT_HEALTH && G_DS_RmHas( DSRM_NOHEALTH ) ) {
		G_LogPrintf( "Rematch: NO HEALTH - %s not spawned\n", item->classname );
		return qfalse;
	}
	return qtrue;
}

// GLASS: G_RL_MaxHealthBonus for the human: half of the whole limit
int G_DS_RmHealthBonus( gentity_t *ent, int bonus ) {
	int		full, cut;

	if ( !ent || !ent->client || ( ent->r.svFlags & SVF_BOT ) || !G_DS_RmHas( DSRM_GLASS ) ) {
		return 0;
	}
	full = ent->client->pers.maxHealth + bonus;
	cut = full / 2;
	if ( rm_glassSpawn != ent->client->respawnTime ) {
		rm_glassSpawn = ent->client->respawnTime;
		G_LogPrintf( "Rematch: GLASS - max health %i -> %i\n", full, full - cut );
	}
	return -cut;
}

// Acts_PowerScale: the player's power as the boss sees it. GLASS halves the health limit, which
// G_DS_PowerScore counts as half the staying power: the boss must not shrink for it (that would
// cancel IRON), so the score is taken with the full limit
float G_DS_RmPowerScore( gentity_t *ent ) {
	int		keep;
	float	p;

	if ( !ent || !ent->client || !G_DS_RmHas( DSRM_GLASS ) ) {
		return G_DS_PowerScore( ent );
	}
	keep = ent->client->ps.stats[STAT_MAX_HEALTH];
	if ( keep > 0 ) {
		ent->client->ps.stats[STAT_MAX_HEALTH] = keep * 2;
	}
	p = G_DS_PowerScore( ent );
	ent->client->ps.stats[STAT_MAX_HEALTH] = keep;
	G_LogPrintf( "Rematch: GLASS - power taken at max health %i (not %i): %.2f\n", keep * 2, keep, p );
	return p;
}

// Acts_UpdateConfigstring (timed): rm mask, rmh heat, rmp the previous best at this heat (ms)
void G_DS_RematchInfo( char *s, int size ) {
	int		m = G_DS_RmMask();

	if ( m < 0 ) {
		return;
	}
	Q_strcat( s, size, va( "\\rm\\%i\\rmh\\%i\\rmp\\%i", m, BG_DSRM_Heat( m ), atoi( G_RL_RunGet( "rm_prev" ) ) ) );
}

/*
=================
G_DS_RematchEnd

g_roguelike.c, RLP_DONE on a practice floor: a rematch reports to the UI with "dsrmend" (qtrue);
anything else goes on to "dspracend"
=================
*/
qboolean G_DS_RematchEnd( int result ) {
	char	cs[MAX_STRING_CHARS];
	int		m = G_DS_RmMask(), kill = 0, left, deaths, bar;

	if ( m < 0 ) {
		return qfalse;
	}
	trap_GetConfigstring( CS_DESCENTBOSS, cs, sizeof( cs ) );
	deaths = atoi( Info_ValueForKey( cs, "pd" ) );
	if ( result == 1 ) {
		kill = atoi( Info_ValueForKey( cs, "kt" ) );
		if ( kill <= 0 && rm_limit > 0 ) {
			left = G_DS_BossTimeLeft();
			kill = left >= 0 ? rm_limit - left : 0;
		}
	}
	bar = (int)( G_DS_BossProgress() * 100.0f + 0.5f );
	if ( result == 1 ) {
		bar = 100;
	}
	trap_SendConsoleCommand( EXEC_INSERT, va( "dsrmend %i %i %i %i %i %i\n", atoi( G_RL_RunGet( "practice" ) ), m, result, kill, bar, deaths ) );
	G_LogPrintf( "Rematch: over: boss %i mods %i heat %i result %s kill %i ms bar %i%% deaths %i\n", atoi( G_RL_RunGet( "practice" ) ), m,
		BG_DSRM_Heat( m ), result == 1 ? "WIN" : "LOSS", kill, bar, deaths );
	return qtrue;
}
