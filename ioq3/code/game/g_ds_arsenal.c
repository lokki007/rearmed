/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
//
// g_ds_arsenal.c -- DESCENT v2 ARSENAL: weapon levels 1..8 and evolutions.
//
// The human's growth lives in arsHuman (loaded from / saved to the run file by g_roguelike.c
// through G_DS_ArsenalLoad / G_DS_ArsenalSave), every bot has its own from floor and role.
// In DESCENT the primary fire of every levelling weapon except the gauntlet goes through
// G_DS_FireWeapon (stock functions plus the level and evolution changes). Fire rate travels
// to pmove in ps->stats[STAT_DSFIRE] (bg_descent.h), so prediction matches the server.
// Everything here returns at once outside DESCENT: normal modes are untouched.

#include "g_local.h"

int G_DS_PassiveLevel( gentity_t *ent, int passive );		// g_ds_loot.c

typedef struct {
	qboolean	assigned;
	int			level[DS_NUM_WEAPONS];		// 0 = never held (counts as 1)
	int			xp[DS_NUM_WEAPONS];			// xp gathered towards the next level
	int			evo;						// bit per dsEvolution_t
	int			shots[DS_NUM_WEAPONS];		// shots fired (every Nth shot effects)
	int			floor, role;				// bots: what their levels follow
	int			retrack;					// bots: level.time of the next catch-up with the player
} arsGrowth_t;

static arsGrowth_t	arsHuman;
static arsGrowth_t	arsBot[MAX_CLIENTS];
static int			arsSlowUntil[MAX_CLIENTS];
static float		arsSlowScale[MAX_CLIENTS];
static int			arsInBlast;			// arsenal blast damage in progress: no scaling, no on-hit effects

/*
-------------------------------------------------------------------------------
Per-level numbers. Level n adds dmg % damage, rate % shorter refire, speed % missile
speed, area % splash radius. Specials are the "lvl >= n" checks in the code.
Keep in sync with ds_weaponLevelDesc in bg_descent.c.
-------------------------------------------------------------------------------
*/
typedef struct {
	int		dmg, rate, speed, area;
} arsLvl_t;

static const arsLvl_t arsLvl[DS_NUM_WEAPONS][DS_MAX_WLEVEL + 1] = {
	{ { 0 } },
	// gauntlet: +8% dmg, swing 6% faster, knockback, +8% dmg, heal 3, swing 6% faster, gib + gore splash
	{ { 0 }, { 0 }, { 8, 0, 0, 0 }, { 0, 6, 0, 0 }, { 0 }, { 8, 0, 0, 0 }, { 0 }, { 0, 6, 0, 0 }, { 0 } },
	// machinegun: +6%, fire 5% faster, pierce 1, +6%, 1 in 12 free, fire 5% faster, every 5th explosive
	{ { 0 }, { 0 }, { 6, 0, 0, 0 }, { 0, 5, 0, 0 }, { 0 }, { 6, 0, 0, 0 }, { 0 }, { 0, 5, 0, 0 }, { 0 } },
	// shotgun: +1 pellet, +6%, tighter, +1 pellet, fire 5% faster, +6%, incendiary
	{ { 0 }, { 0 }, { 0 }, { 6, 0, 0, 0 }, { 0 }, { 0 }, { 0, 5, 0, 0 }, { 6, 0, 0, 0 }, { 0 } },
	// grenade launcher: +6%, +6% radius, fire 5% faster, lively bounce, +6%, +6% radius, bomblets
	{ { 0 }, { 0 }, { 6, 0, 0, 0 }, { 0, 0, 0, 6 }, { 0, 5, 0, 0 }, { 0 }, { 6, 0, 0, 0 }, { 0, 0, 0, 6 }, { 0 } },
	// rocket launcher: +6%, +8% speed, +6% radius, fire 5% faster, +6%, +8% speed, half rocket every 4th
	{ { 0 }, { 0 }, { 6, 0, 0, 0 }, { 0, 0, 8, 0 }, { 0, 0, 0, 6 }, { 0, 5, 0, 0 }, { 6, 0, 0, 0 }, { 0, 0, 8, 0 }, { 0 } },
	// lightning: +6%, slow, +6%, fire 5% faster, 1 in 12 free, +6%, arc
	{ { 0 }, { 0 }, { 6, 0, 0, 0 }, { 0 }, { 6, 0, 0, 0 }, { 0, 5, 0, 0 }, { 0 }, { 6, 0, 0, 0 }, { 0 } },
	// railgun: +6%, recharge 5% faster, ricochet, +6%, recharge 5% faster, +6%, kills explode
	{ { 0 }, { 0 }, { 6, 0, 0, 0 }, { 0, 5, 0, 0 }, { 0 }, { 6, 0, 0, 0 }, { 0, 5, 0, 0 }, { 6, 0, 0, 0 }, { 0 } },
	// plasma: +6%, +8% speed, fire 5% faster, +8% splash, +6%, fire 5% faster, growing bolts
	{ { 0 }, { 0 }, { 6, 0, 0, 0 }, { 0, 0, 8, 0 }, { 0, 5, 0, 0 }, { 0, 0, 0, 8 }, { 6, 0, 0, 0 }, { 0, 5, 0, 0 }, { 0 } },
	// bfg: +6%, +8% radius, recharge 6% faster, +6%, longer zaps, +8% radius, burning crater
	{ { 0 }, { 0 }, { 6, 0, 0, 0 }, { 0, 0, 0, 8 }, { 0, 6, 0, 0 }, { 6, 0, 0, 0 }, { 0 }, { 0, 0, 0, 8 }, { 0 } },
	{ { 0 } },
	// nailgun: +6%, +1 nail, ricochet, +6%, fire 5% faster, +1 nail, pin
	{ { 0 }, { 0 }, { 6, 0, 0, 0 }, { 0 }, { 0 }, { 6, 0, 0, 0 }, { 0, 5, 0, 0 }, { 0 }, { 0 } },
	// prox: +6%, +8% radius, arm faster, fire 5% faster, +6%, +8% radius, fire
	{ { 0 }, { 0 }, { 6, 0, 0, 0 }, { 0, 0, 0, 8 }, { 0 }, { 0, 5, 0, 0 }, { 6, 0, 0, 0 }, { 0, 0, 0, 8 }, { 0 } },
	// chaingun: +6%, spins up 9% faster, +6%, pierce 1, 1 in 12 free, fire 5% faster, no spin-down
	{ { 0 }, { 0 }, { 6, 0, 0, 0 }, { 0 }, { 6, 0, 0, 0 }, { 0 }, { 0 }, { 0, 5, 0, 0 }, { 0 } }
};

#define DS_STAT_DMG		0
#define DS_STAT_RATE	1
#define DS_STAT_SPEED	2
#define DS_STAT_AREA	3

static int Ars_LvlSum( int w, int lvl, int stat ) {
	int		i, s = 0;
	const arsLvl_t	*l;

	if ( w <= 0 || w >= DS_NUM_WEAPONS ) {
		return 0;
	}
	for ( i = 2 ; i <= lvl && i <= DS_MAX_WLEVEL ; i++ ) {
		l = &arsLvl[w][i];
		s += stat == DS_STAT_DMG ? l->dmg : stat == DS_STAT_RATE ? l->rate : stat == DS_STAT_SPEED ? l->speed : l->area;
	}
	return s;
}

static qboolean Ars_Debug( void ) {
	return trap_Cvar_VariableIntegerValue( "ds_debug" ) != 0;
}

/*
===============================================================================

GROWTH STATE

===============================================================================
*/

static arsGrowth_t *Ars_Growth( gentity_t *ent ) {
	int		n;

	if ( !G_RL_Active() || !ent || !ent->client ) {
		return NULL;
	}
	n = ent - g_entities;
	if ( n < 0 || n >= MAX_CLIENTS ) {
		return NULL;
	}
	if ( G_RL_IsPlayer( ent ) ) {		// (or the ACTS g_dsBossSim bot)
		return &arsHuman;
	}
	if ( !arsBot[n].assigned ) {
		G_DS_SetBotWeaponLevels( ent, G_RL_Floor(), G_RL_ClientRole( ent ) );
	}
	return &arsBot[n];
}

static qboolean Ars_IsHuman( gentity_t *ent ) {
	return ent && ent->client && G_RL_IsPlayer( ent );		// (or the ACTS g_dsBossSim bot)
}

static int Ars_Level( arsGrowth_t *g, int w ) {
	int		l;

	if ( !g || !DS_WEAPON_LEVELS( w ) ) {
		return 0;
	}
	l = g->level[w];
	if ( l < 1 ) {
		return 1;
	}
	return l > DS_MAX_WLEVEL ? DS_MAX_WLEVEL : l;
}

// the evolution active for a weapon, -1 none (ARMAGEDDON replaces HELLFIRE)
static int Ars_EvoFor( arsGrowth_t *g, int w ) {
	int		e;

	if ( !g || !DS_WEAPON_LEVELS( w ) ) {
		return -1;
	}
	if ( w == WP_ROCKET_LAUNCHER && ( g->evo & ( 1 << EVO_ARMAGEDDON ) ) ) {
		return EVO_ARMAGEDDON;
	}
	e = BG_DS_EvolutionForWeapon( w );
	if ( e >= 0 && ( g->evo & ( 1 << e ) ) ) {
		return e;
	}
	return -1;
}

static void Ars_HumanChanged( void ) {
	G_DS_GrowChanged();
}

static void Ars_Notify( gentity_t *ent, int w, int lvl, int gained ) {
	if ( !Ars_IsHuman( ent ) ) {
		return;
	}
	trap_SendServerCommand( ent - g_entities, va( "dslvl %i %i", w, lvl ) );
	if ( gained > 0 ) {
		G_DS_Stat( DSST_LEVELUPS, gained );
	}
	G_LogPrintf( "DSLevel: client %i weapon %i level %i\n", (int)( ent - g_entities ), w, lvl );
	Ars_HumanChanged();
}

/*
===============================================================================

PUBLIC GROWTH API

===============================================================================
*/

int G_DS_WeaponForMod( int mod ) {
	switch ( mod ) {
	case MOD_GAUNTLET:			return WP_GAUNTLET;
	case MOD_MACHINEGUN:		return WP_MACHINEGUN;
	case MOD_SHOTGUN:			return WP_SHOTGUN;
	case MOD_GRENADE:
	case MOD_GRENADE_SPLASH:	return WP_GRENADE_LAUNCHER;
	case MOD_ROCKET:
	case MOD_ROCKET_SPLASH:		return WP_ROCKET_LAUNCHER;
	case MOD_LIGHTNING:
	case MOD_THUNDER:			return WP_LIGHTNING;
	case MOD_RAILGUN:			return WP_RAILGUN;
	case MOD_PLASMA:
	case MOD_PLASMA_SPLASH:		return WP_PLASMAGUN;
	case MOD_BFG:
	case MOD_BFG_SPLASH:		return WP_BFG;
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case MOD_NAIL:				return WP_NAILGUN;
	case MOD_PROXIMITY_MINE:	return WP_PROX_LAUNCHER;
	case MOD_CHAINGUN:			return WP_CHAINGUN;
#endif
	default:					return WP_NONE;
	}
}

int G_DS_WeaponLevel( gentity_t *ent, int weapon ) {
	arsGrowth_t	*g = Ars_Growth( ent );

	return g ? Ars_Level( g, weapon ) : 0;
}

void G_DS_AddWeaponLevel( gentity_t *ent, int weapon, int levels ) {
	arsGrowth_t	*g = Ars_Growth( ent );
	int			old;

	if ( !g || !DS_WEAPON_LEVELS( weapon ) || levels <= 0 ) {
		return;
	}
	old = Ars_Level( g, weapon );
	g->level[weapon] = old + levels;
	if ( g->level[weapon] > DS_MAX_WLEVEL ) {
		g->level[weapon] = DS_MAX_WLEVEL;
	}
	if ( g->level[weapon] >= DS_MAX_WLEVEL ) {
		g->xp[weapon] = 0;
	}
	if ( g->level[weapon] != old ) {
		Ars_Notify( ent, weapon, g->level[weapon], g->level[weapon] - old );
	} else if ( Ars_IsHuman( ent ) ) {
		Ars_HumanChanged();
	}
}

void G_DS_AddWeaponXP( gentity_t *ent, int weapon, int xp ) {
	arsGrowth_t	*g = Ars_Growth( ent );
	int			lvl, need, gained = 0;

	if ( !g || !DS_WEAPON_LEVELS( weapon ) || xp <= 0 ) {
		return;
	}
	lvl = Ars_Level( g, weapon );
	if ( lvl >= DS_MAX_WLEVEL ) {
		return;
	}
	g->xp[weapon] += xp;
	while ( lvl < DS_MAX_WLEVEL ) {
		need = ds_wlevelXP[lvl + 1] - ds_wlevelXP[lvl];
		if ( need < 1 ) {
			need = 1;
		}
		if ( g->xp[weapon] < need ) {
			break;
		}
		g->xp[weapon] -= need;
		lvl++;
		gained++;
	}
	if ( lvl >= DS_MAX_WLEVEL ) {
		g->xp[weapon] = 0;
	}
	g->level[weapon] = lvl;
	if ( gained ) {
		Ars_Notify( ent, weapon, lvl, gained );
	} else if ( Ars_IsHuman( ent ) ) {
		Ars_HumanChanged();
	}
}

qboolean G_DS_Evolved( gentity_t *ent, int evo ) {
	arsGrowth_t	*g = Ars_Growth( ent );

	return g && evo >= 0 && evo < EVO_NUM && ( g->evo & ( 1 << evo ) );
}

// a key card: passives by level (chests give levels too), perks and secrets by card
static qboolean Ars_HasKey( gentity_t *ent, int card ) {
	if ( card >= RLC_PASSIVE_FIRST && card < RLC_PASSIVE_FIRST + RLP_NUM ) {
		return G_DS_PassiveLevel( ent, card - RLC_PASSIVE_FIRST ) > 0;
	}
	return G_RL_HasCard( ent, card );
}

qboolean G_DS_CanEvolve( gentity_t *ent, int evo ) {
	arsGrowth_t	*g = Ars_Growth( ent );

	if ( !g || evo < 0 || evo >= EVO_NUM || ( g->evo & ( 1 << evo ) ) ) {
		return qfalse;
	}
	if ( evo == EVO_ARMAGEDDON ) {
		return ( g->evo & ( 1 << EVO_HELLFIRE ) ) && ( g->evo & ( 1 << EVO_CLUSTERBOMB ) )
			&& Ars_HasKey( ent, ds_evolutions[evo].keyCard );
	}
	return Ars_Level( g, ds_evolutions[evo].weapon ) >= DS_MAX_WLEVEL && Ars_HasKey( ent, ds_evolutions[evo].keyCard );
}

void G_DS_Evolve( gentity_t *ent, int evo ) {
	arsGrowth_t	*g = Ars_Growth( ent );
	gentity_t	*tent;
	int			w;

	if ( !g || evo < 0 || evo >= EVO_NUM || ( g->evo & ( 1 << evo ) ) ) {
		return;
	}
	w = ds_evolutions[evo].weapon;
	g->evo |= 1 << evo;
	g->level[w] = DS_MAX_WLEVEL;
	g->xp[w] = 0;
	if ( ent->client->pers.connected == CON_CONNECTED && ent->health > 0 ) {
		tent = G_TempEntity( ent->r.currentOrigin, EV_DS_FX );
		tent->s.eventParm = DSX_EVOLVE;
		tent->s.otherEntityNum = ent->s.number;
		tent->s.generic1 = evo;
		tent->s.weapon = w;
	}
	if ( Ars_IsHuman( ent ) ) {
		trap_SendServerCommand( ent - g_entities, va( "dsevo %i", evo ) );
		G_DS_Stat( DSST_EVOS, 1 );
		Ars_HumanChanged();
	}
	G_LogPrintf( "DSEvolve: client %i evo %i %s\n", (int)( ent - g_entities ), evo, ds_evolutions[evo].name );
}

/*
=================
Ars_BotTarget

Bots follow the player's real progress: the average level of the player's weapons,
-1 on the first half of an act for regular bots, +1 for elites and bosses (1..8).
=================
*/
static int Ars_BotTarget( int floor, int role ) {
	int		w, n = 0, sum = 0, lvl;

	for ( w = 1 ; w < DS_NUM_WEAPONS ; w++ ) {
		if ( DS_WEAPON_LEVELS( w ) && arsHuman.level[w] > 0 ) {
			sum += arsHuman.level[w] > DS_MAX_WLEVEL ? DS_MAX_WLEVEL : arsHuman.level[w];
			n++;
		}
	}
	lvl = n ? ( sum + n / 2 ) / n : 1;
	if ( role == 3 || role == 4 ) {
		lvl++;
	} else if ( ( ( floor - 1 ) % DS_FLOORS_PER_ACT ) < DS_FLOORS_PER_ACT / 2 ) {
		lvl--;
	}
	if ( lvl < 1 ) {
		lvl = 1;
	} else if ( lvl > DS_MAX_WLEVEL ) {
		lvl = DS_MAX_WLEVEL;
	}
	return lvl;
}

// ds-intro: the level a bot of this role will get (the floor's opponents before they join)
int G_DS_BotLevelPreview( int floor, int role ) {
	return Ars_BotTarget( floor < 1 ? 1 : floor, role );
}

/*
=================
G_DS_SetBotWeaponLevels

Levels follow the player (Ars_BotTarget). Elites from act 3 evolve three of their guns,
bosses evolve everything (the union too from act 4).
=================
*/
void G_DS_SetBotWeaponLevels( gentity_t *ent, int floor, int role ) {
	arsGrowth_t	*g;
	int			n, w, lvl, i, e, picks;
	unsigned	seed;

	if ( !G_RL_Active() || !ent || !ent->client || !( ent->r.svFlags & SVF_BOT ) ) {
		return;
	}
	n = ent - g_entities;
	if ( n < 0 || n >= MAX_CLIENTS ) {
		return;
	}
	g = &arsBot[n];
	memset( g, 0, sizeof( *g ) );
	g->assigned = qtrue;
	if ( floor < 1 ) {
		floor = 1;
	}
	g->floor = floor;
	g->role = role;
	g->retrack = level.time + 10000;
	lvl = Ars_BotTarget( floor, role ) + G_DS_RankWeaponBonus( ent );		// en-personality: veterans rank 3+, the nemesis's level
	if ( lvl > DS_MAX_WLEVEL ) {
		lvl = DS_MAX_WLEVEL;
	}
	for ( w = 1 ; w < DS_NUM_WEAPONS ; w++ ) {
		if ( DS_WEAPON_LEVELS( w ) ) {
			g->level[w] = lvl;
		}
	}
	if ( role == 4 ) {
		for ( e = 0 ; e < EVO_ARMAGEDDON ; e++ ) {
			g->evo |= 1 << e;
		}
		if ( DS_ActForFloor( floor ) >= 4 ) {
			g->evo |= 1 << EVO_ARMAGEDDON;
		}
	} else if ( role == 3 && DS_ActForFloor( floor ) >= 3 ) {
		seed = (unsigned)( floor * 7919 + n * 104729 );
		for ( picks = 0, i = 0 ; picks < 3 && i < 40 ; i++ ) {
			seed = seed * 1103515245u + 12345u;
			e = (int)( ( seed >> 16 ) % EVO_ARMAGEDDON );
			if ( !( g->evo & ( 1 << e ) ) ) {
				g->evo |= 1 << e;
				g->level[ds_evolutions[e].weapon] = DS_MAX_WLEVEL;
				picks++;
			}
		}
	}
	G_LogPrintf( "DSBot: client %i floor %i role %i level %i evo 0x%x\n", n, floor, role, lvl, g->evo );
}

/*
===============================================================================

RUN FILE / CONFIGSTRING

===============================================================================
*/

static void Ars_ParseCsv( const char *s, int *out, int max ) {
	int		i = 0;

	if ( !s ) {
		return;
	}
	while ( *s && i < max ) {
		out[i++] = atoi( s );
		while ( *s && *s != ',' && *s != '.' ) {
			s++;
		}
		if ( *s ) {
			s++;
		}
	}
}

void G_DS_ArsenalLoad( const char *wl, const char *wx, int evoMask ) {
	int		w;

	memset( &arsHuman, 0, sizeof( arsHuman ) );
	arsHuman.assigned = qtrue;
	Ars_ParseCsv( wl, arsHuman.level, DS_NUM_WEAPONS );
	Ars_ParseCsv( wx, arsHuman.xp, DS_NUM_WEAPONS );
	for ( w = 0 ; w < DS_NUM_WEAPONS ; w++ ) {
		if ( !DS_WEAPON_LEVELS( w ) ) {
			arsHuman.level[w] = 0;
			arsHuman.xp[w] = 0;
		} else if ( arsHuman.level[w] > DS_MAX_WLEVEL ) {
			arsHuman.level[w] = DS_MAX_WLEVEL;
		} else if ( arsHuman.level[w] < 0 ) {
			arsHuman.level[w] = 0;
		}
		if ( arsHuman.xp[w] < 0 ) {
			arsHuman.xp[w] = 0;
		}
	}
	arsHuman.evo = evoMask & ( ( 1 << EVO_NUM ) - 1 );
	memset( arsBot, 0, sizeof( arsBot ) );
	memset( arsSlowUntil, 0, sizeof( arsSlowUntil ) );
}

void G_DS_ArsenalSave( char *wl, char *wx, int size, int *evoMask ) {
	int		w;

	wl[0] = wx[0] = 0;
	for ( w = 0 ; w < DS_NUM_WEAPONS ; w++ ) {
		Q_strcat( wl, size, va( w ? ",%i" : "%i", arsHuman.level[w] ) );
		Q_strcat( wx, size, va( w ? ",%i" : "%i", arsHuman.xp[w] ) );
	}
	*evoMask = arsHuman.evo;
}

void G_DS_ArsenalGrowInfo( char *buf, int size ) {
	int		w;

	Q_strcat( buf, size, "\\wl\\" );
	for ( w = 0 ; w < DS_NUM_WEAPONS ; w++ ) {
		Q_strcat( buf, size, va( w ? ".%i" : "%i", arsHuman.level[w] ) );
	}
	Q_strcat( buf, size, "\\wx\\" );
	for ( w = 0 ; w < DS_NUM_WEAPONS ; w++ ) {
		Q_strcat( buf, size, va( w ? ".%i" : "%i", arsHuman.xp[w] ) );
	}
	Q_strcat( buf, size, va( "\\ev\\%i", arsHuman.evo ) );
}

/*
===============================================================================

THINK (before every Pmove)

===============================================================================
*/

static int Ars_TempoPct( gentity_t *ent ) {
	int		pct;

	pct = (int)( 100.0f * ds_passivePerLevel[RLP_TEMPO] * G_DS_PassiveLevel( ent, RLP_TEMPO ) + 0.5f );
	if ( Ars_IsHuman( ent ) ) {
		pct += (int)( 100.0f * G_DS_UpValue( DSU_TEMPO ) + 0.5f );
	}
	return pct;
}

static float Ars_AreaScale( gentity_t *ent, int w, int lvl ) {
	float	s;

	s = 1.0f + Ars_LvlSum( w, lvl, DS_STAT_AREA ) / 100.0f
		+ ds_passivePerLevel[RLP_AREA] * G_DS_PassiveLevel( ent, RLP_AREA );
	if ( Ars_IsHuman( ent ) ) {
		s += G_DS_UpValue( DSU_AREA );
	}
	return s + G_DS_SkillAreaBonus( ent, w );		// SKILLS: BLAST RADIUS, SUPERHEATED
}

static float Ars_SpeedScale( gentity_t *ent, int w, int lvl ) {
	return 1.0f + Ars_LvlSum( w, lvl, DS_STAT_SPEED ) / 100.0f
		+ ds_passivePerLevel[RLP_VELOCITY] * G_DS_PassiveLevel( ent, RLP_VELOCITY )
		+ G_DS_SkillSpeedBonus( ent, w );		// SKILLS: PAYLOAD
}

void G_DS_ArsenalThink( gentity_t *ent ) {
	gclient_t	*cl = ent->client;
	arsGrowth_t	*g;
	int			w, lvl, pct, f, n, start;
	qboolean	changed = qfalse;

	if ( !( g = Ars_Growth( ent ) ) ) {
		return;
	}
	n = ent - g_entities;

	// a weapon held for the first time this run starts at level 1 (2 with the ARSENAL upgrade)
	if ( g == &arsHuman ) {
		start = G_DS_UpRank( DSU_ARSENAL ) > 0 ? 2 : 1;
		for ( w = 1 ; w < DS_NUM_WEAPONS ; w++ ) {
			if ( DS_WEAPON_LEVELS( w ) && !g->level[w] && ( cl->ps.stats[STAT_WEAPONS] & ( 1 << w ) ) ) {
				g->level[w] = start;
				changed = qtrue;
			}
		}
		if ( changed ) {
			Ars_HumanChanged();
		}
	}

	// the held weapon's refire scale and chaingun spin, predicted by the client
	w = cl->ps.weapon;
	pct = 100 - Ars_TempoPct( ent );
	f = 0;
	if ( DS_WEAPON_LEVELS( w ) ) {
		lvl = Ars_Level( g, w );
		pct -= Ars_LvlSum( w, lvl, DS_STAT_RATE );
		pct -= G_DS_SkillFirePct( ent, w );		// SKILLS: SWIFT FISTS, RAPID CYCLE, QUICK PUMP, STEADY, OVERCLOCK, FRENZY
		f |= G_DS_SkillFireFlags( ent, w );		// SKILLS: SPIN DOCTOR
		if ( w == WP_CHAINGUN ) {
			if ( lvl >= 3 ) {
				f |= DSF_SPINFAST;
			}
			if ( lvl >= 8 ) {
				f |= DSF_NOSPINDOWN;
			}
			if ( Ars_EvoFor( g, w ) == EVO_RIPSAW ) {
				f |= DSF_NOSPINUP;
			}
		}
	}
	if ( pct < 40 ) {
		pct = 40;
	} else if ( pct > 200 ) {
		pct = 200;
	}
	pct = G_DS_FrenzyFirePct( ent, pct );		// FRENZY: BERSERK (a few seconds, may pass the 40% floor)
	cl->ps.stats[STAT_DSFIRE] = DSF_ON | f | pct;

	// evolved weapon in hand: everyone sees it glow
	if ( Ars_EvoFor( g, w ) >= 0 ) {
		cl->ps.generic1 |= RM_BODY_EVOLVED;
	} else {
		cl->ps.generic1 &= ~RM_BODY_EVOLVED;
	}

	// bots catch up with the player's growth during the floor (never down)
	if ( g != &arsHuman && level.time >= g->retrack ) {
		g->retrack = level.time + 10000;
		lvl = Ars_BotTarget( g->floor, g->role ) + G_DS_RankWeaponBonus( ent );		// en-personality
		if ( lvl > DS_MAX_WLEVEL ) {
			lvl = DS_MAX_WLEVEL;
		}
		for ( w = 1 ; w < DS_NUM_WEAPONS ; w++ ) {
			if ( DS_WEAPON_LEVELS( w ) && g->level[w] < lvl ) {
				g->level[w] = lvl;
			}
		}
	}

	// slowed by lightning / pinned by nails
	if ( arsSlowUntil[n] > level.time ) {
		cl->ps.speed *= arsSlowScale[n];
	}
}

/*
=================
G_DS_SetWeaponLevels

ds-surprise: a bot gets exact weapon levels (levels[w] > 0; 0 keeps its own) and evolutions
(the ghost of a past run carries the past kit)
=================
*/
void G_DS_SetWeaponLevels( gentity_t *ent, const int *levels, int evoMask ) {
	arsGrowth_t	*g = Ars_Growth( ent );
	int			w;

	if ( !g || !levels || G_RL_IsPlayer( ent ) ) {
		return;
	}
	for ( w = 1 ; w < DS_NUM_WEAPONS ; w++ ) {
		if ( DS_WEAPON_LEVELS( w ) && levels[w] > 0 ) {
			g->level[w] = levels[w] > DS_MAX_WLEVEL ? DS_MAX_WLEVEL : levels[w];
			g->xp[w] = 0;
		}
	}
	g->evo = evoMask & ( ( 1 << EVO_NUM ) - 1 );
	g->retrack = level.time + 3600000;		// keeps the past kit (no catching up with the player)
	G_LogPrintf( "DSBot: client %i set levels (ghost) evo 0x%x\n", (int)( ent - g_entities ), g->evo );
}

void G_DS_ArsenalClientBegin( gentity_t *ent ) {
	int		n = ent - g_entities;

	if ( n >= 0 && n < MAX_CLIENTS ) {
		arsSlowUntil[n] = 0;
	}
}

void G_DS_ArsenalDisconnect( gentity_t *ent ) {
	int		n = ent - g_entities;

	if ( n >= 0 && n < MAX_CLIENTS ) {
		arsBot[n].assigned = qfalse;
		arsSlowUntil[n] = 0;
	}
}

static void Ars_Slow( gentity_t *targ, float scale, int msec ) {
	int		n = targ - g_entities;

	if ( n < 0 || n >= MAX_CLIENTS ) {
		return;
	}
	if ( arsSlowUntil[n] < level.time || scale < arsSlowScale[n] ) {
		arsSlowScale[n] = scale;
	}
	if ( arsSlowUntil[n] < level.time + msec ) {
		arsSlowUntil[n] = level.time + msec;
	}
}

/*
===============================================================================

SHARED FIRING HELPERS

===============================================================================
*/

static gentity_t *Ars_Fx( vec3_t origin, int kind, gentity_t *shooter, int weapon ) {
	gentity_t	*tent;

	tent = G_TempEntity( origin, EV_DS_FX );
	tent->s.eventParm = kind;
	tent->s.otherEntityNum = shooter ? shooter->s.number : ENTITYNUM_NONE;
	tent->s.weapon = weapon;
	return tent;
}

static qboolean Ars_Enemy( gentity_t *attacker, gentity_t *other ) {
	return other && other->inuse && other->client && other->health > 0 && other != attacker
		&& other->client->sess.sessionTeam != TEAM_SPECTATOR && !OnSameTeam( other, attacker );
}

/*
=================
Ars_Blast

Arsenal splash: no headshots, no arsenal scaling (the numbers already include it), no
chain of on-hit effects. fx < 0: no visual.
=================
*/
static void Ars_Blast( vec3_t origin, gentity_t *attacker, float damage, float radius, int mod, gentity_t *ignore, int fxWeapon ) {
	gentity_t	*ent, *tent;
	int			entityList[MAX_GENTITIES];
	int			num, i, e;
	vec3_t		mins, maxs, v, dir;
	float		dist, points;

	if ( fxWeapon >= 0 ) {
		tent = Ars_Fx( origin, DSX_BLAST, attacker, fxWeapon );
		tent->s.generic1 = (int)( radius / 4 ) > 255 ? 255 : (int)( radius / 4 );
	}
	for ( i = 0 ; i < 3 ; i++ ) {
		mins[i] = origin[i] - radius;
		maxs[i] = origin[i] + radius;
	}
	num = trap_EntitiesInBox( mins, maxs, entityList, MAX_GENTITIES );
	arsInBlast++;
	for ( e = 0 ; e < num ; e++ ) {
		ent = &g_entities[entityList[e]];
		if ( ent == ignore || !ent->takedamage || ent == attacker ) {
			continue;
		}
		for ( i = 0 ; i < 3 ; i++ ) {
			if ( origin[i] < ent->r.absmin[i] ) {
				v[i] = ent->r.absmin[i] - origin[i];
			} else if ( origin[i] > ent->r.absmax[i] ) {
				v[i] = origin[i] - ent->r.absmax[i];
			} else {
				v[i] = 0;
			}
		}
		dist = VectorLength( v );
		if ( dist >= radius ) {
			continue;
		}
		points = damage * ( 1.0f - 0.5f * dist / radius );
		if ( points < 1 || !CanDamage( ent, origin ) ) {
			continue;
		}
		VectorSubtract( ent->r.currentOrigin, origin, dir );
		dir[2] += 24;
		G_Damage( ent, NULL, attacker, dir, NULL, (int)points, DAMAGE_RADIUS, mod );
	}
	arsInBlast--;
}

static void Ars_DelayedBlastThink( gentity_t *ent ) {
	gentity_t	*attacker = NULL;

	if ( ent->r.ownerNum >= 0 && ent->r.ownerNum < MAX_CLIENTS && g_entities[ent->r.ownerNum].inuse ) {
		attacker = &g_entities[ent->r.ownerNum];
	}
	if ( attacker ) {
		if ( ent->count == DSX_GOREBURST ) {
			Ars_Fx( ent->s.origin, DSX_GOREBURST, attacker, WP_GAUNTLET );
			Ars_Blast( ent->s.origin, attacker, ent->damage, ent->splashRadius, ent->methodOfDeath, NULL, -1 );
		} else {
			Ars_Blast( ent->s.origin, attacker, ent->damage, ent->splashRadius, ent->methodOfDeath, NULL, ent->s.weapon );
		}
	}
	G_FreeEntity( ent );
}

// a blast a moment after a kill (never inside the G_Damage that killed)
static void Ars_DelayedBlast( vec3_t origin, gentity_t *attacker, int damage, int radius, int mod, int fx, int weapon ) {
	gentity_t	*ent;

	ent = G_Spawn();
	ent->classname = "ds_blast";
	ent->r.svFlags = SVF_NOCLIENT;
	ent->r.ownerNum = attacker->s.number;
	VectorCopy( origin, ent->s.origin );
	ent->damage = damage;
	ent->splashRadius = radius;
	ent->methodOfDeath = mod;
	ent->count = fx;
	ent->s.weapon = weapon;
	ent->think = Ars_DelayedBlastThink;
	ent->nextthink = level.time + 80;
}

static void Ars_Refund( gentity_t *ent, int w ) {
	if ( ent->client->ps.ammo[w] >= 0 && ent->client->ps.ammo[w] < 999 ) {
		ent->client->ps.ammo[w]++;
	}
}

// nearest visible enemy of attacker within range of org, not in skip[]
static gentity_t *Ars_NearestEnemy( gentity_t *attacker, vec3_t org, float range, gentity_t **skip, int numSkip, vec3_t cone, float minDot ) {
	gentity_t	*best = NULL, *other;
	trace_t		tr;
	vec3_t		center, d;
	float		dist, bestDist = range;
	int			i, j;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		other = &g_entities[i];
		if ( !Ars_Enemy( attacker, other ) ) {
			continue;
		}
		for ( j = 0 ; j < numSkip ; j++ ) {
			if ( skip[j] == other ) {
				break;
			}
		}
		if ( j < numSkip ) {
			continue;
		}
		VectorCopy( other->r.currentOrigin, center );
		center[2] += 8;
		VectorSubtract( center, org, d );
		dist = VectorNormalize( d );
		if ( dist >= bestDist ) {
			continue;
		}
		if ( cone && DotProduct( d, cone ) < minDot ) {
			continue;
		}
		trap_Trace( &tr, org, NULL, NULL, center, ENTITYNUM_NONE, MASK_SOLID );
		if ( tr.fraction < 1.0f ) {
			continue;
		}
		best = other;
		bestDist = dist;
	}
	return best;
}

static void Ars_Missile( gentity_t *ent, gentity_t *m, int w, int lvl, int evo, float quad ) {
	float	speed;

	m->damage *= quad;
	m->splashDamage *= quad;
	speed = Ars_SpeedScale( ent, w, lvl );
	VectorScale( m->s.pos.trDelta, speed, m->s.pos.trDelta );
	SnapVector( m->s.pos.trDelta );
	m->splashRadius *= Ars_AreaScale( ent, w, lvl );
	m->dsLevel = lvl;
	m->dsEvo = evo + 1;
	m->dsTime = level.time;
	if ( evo >= 0 ) {
		m->s.generic1 |= DSM_EVOLVED;
		if ( evo == EVO_ARMAGEDDON ) {
			m->s.generic1 |= DSM_UNION;
		}
	}
	if ( lvl >= DS_MAX_WLEVEL && evo < 0 && ( w == WP_GRENADE_LAUNCHER || w == WP_PLASMAGUN || w == WP_BFG
		|| w == WP_PROX_LAUNCHER ) ) {
		m->s.generic1 |= DSM_SPECIAL;
	}
}

/*
===============================================================================

HITSCAN

===============================================================================
*/

static void Ars_Bullet( gentity_t *ent, vec3_t start, vec3_t forward, vec3_t right, vec3_t up, float spread,
	int damage, int mod, int pierce, vec3_t endOut ) {
	trace_t		tr;
	vec3_t		end, from;
	float		r, u;
	gentity_t	*traceEnt, *tent;
	int			i, passent;

	r = random() * M_PI * 2.0f;
	u = sin( r ) * crandom() * spread * 16;
	r = cos( r ) * crandom() * spread * 16;
	VectorMA( start, 8192 * 16, forward, end );
	VectorMA( end, r, right, end );
	VectorMA( end, u, up, end );
	VectorCopy( start, from );
	VectorCopy( end, endOut );
	passent = ent->s.number;
	for ( i = 0 ; i < 10 ; i++ ) {
		trap_Trace( &tr, from, NULL, NULL, end, passent, MASK_SHOT );
		VectorCopy( tr.endpos, endOut );
		if ( tr.surfaceFlags & SURF_NOIMPACT ) {
			return;
		}
		traceEnt = &g_entities[tr.entityNum];
		SnapVectorTowards( tr.endpos, from );
		VectorCopy( tr.endpos, endOut );
		if ( traceEnt->takedamage && traceEnt->client ) {
			tent = G_TempEntity( tr.endpos, EV_BULLET_HIT_FLESH );
			tent->s.eventParm = traceEnt->s.number;
			if ( LogAccuracyHit( traceEnt, ent ) ) {
				ent->client->accuracy_hits++;
			}
		} else {
			tent = G_TempEntity( tr.endpos, EV_BULLET_HIT_WALL );
			tent->s.eventParm = DirToByte( tr.plane.normal );
		}
		tent->s.otherEntityNum = ent->s.number;
		if ( traceEnt->takedamage ) {
			G_Damage( traceEnt, ent, ent, forward, tr.endpos, damage, 0, mod );
		}
		if ( traceEnt->takedamage && traceEnt->client && pierce > 0 && tr.entityNum < MAX_CLIENTS ) {
			pierce--;
			VectorCopy( tr.endpos, from );
			passent = traceEnt->s.number;
			continue;
		}
		break;
	}
}

static void Ars_FireMachinegun( gentity_t *ent, arsGrowth_t *g, int lvl, int evo, float quad,
	vec3_t muzzle, vec3_t forward, vec3_t right, vec3_t up ) {
	vec3_t		start, end1, end2;
	gentity_t	*tent;
	int			damage = 7 * quad;

	if ( evo == EVO_BULLETHELL ) {
		// twin barrels, dead straight, through three bodies (each round 4 of 7 damage)
		damage = 4 * quad;
		VectorMA( muzzle, -5, right, start );
		Ars_Bullet( ent, start, forward, right, up, 0, damage, MOD_MACHINEGUN, 3, end1 );
		VectorMA( muzzle, 5, right, start );
		Ars_Bullet( ent, start, forward, right, up, 0, damage, MOD_MACHINEGUN, 3, end2 );
		tent = Ars_Fx( end1, DSX_TWIN, ent, WP_MACHINEGUN );
		VectorCopy( end2, tent->s.origin2 );
		SnapVector( tent->s.origin2 );
		tent->s.generic1 = evo;
		if ( g->shots[WP_MACHINEGUN] % 3 == 0 ) {
			Ars_Refund( ent, WP_MACHINEGUN );
		}
		return;
	}
	Ars_Bullet( ent, muzzle, forward, right, up, 200, damage, MOD_MACHINEGUN, lvl >= 4 ? 1 : 0, end1 );
	if ( lvl >= 8 && g->shots[WP_MACHINEGUN] % 5 == 0 ) {
		Ars_Blast( end1, ent, 12 * quad, 64, MOD_MACHINEGUN, NULL, WP_MACHINEGUN );
	}
	if ( lvl >= 6 && g->shots[WP_MACHINEGUN] % 12 == 0 ) {
		Ars_Refund( ent, WP_MACHINEGUN );
	}
}

static void Ars_FireChaingun( gentity_t *ent, arsGrowth_t *g, int lvl, int evo, float quad,
	vec3_t muzzle, vec3_t forward, vec3_t right, vec3_t up ) {
	vec3_t		end;
	gentity_t	*tent;
	int			damage = 7 * quad;

	if ( evo == EVO_RIPSAW ) {
		Ars_Bullet( ent, muzzle, forward, right, up, 380, damage, MOD_CHAINGUN, 2, end );
		Ars_Refund( ent, WP_CHAINGUN );		// never runs dry
		if ( g->shots[WP_CHAINGUN] % 2 == 0 ) {
			tent = Ars_Fx( end, DSX_TRACER, ent, WP_CHAINGUN );
			tent->s.generic1 = evo;
		}
		return;
	}
	Ars_Bullet( ent, muzzle, forward, right, up, 600, damage, MOD_CHAINGUN, lvl >= 5 ? 1 : 0, end );
	if ( lvl >= 6 && g->shots[WP_CHAINGUN] % 12 == 0 ) {
		Ars_Refund( ent, WP_CHAINGUN );
	}
}

static qboolean Ars_Pellet( gentity_t *ent, vec3_t start, vec3_t end, vec3_t forward, int damage, qboolean fire,
	qboolean patch ) {
	trace_t		tr;
	gentity_t	*traceEnt;

	trap_Trace( &tr, start, NULL, NULL, end, ent->s.number, MASK_SHOT );
	if ( tr.surfaceFlags & SURF_NOIMPACT ) {
		return qfalse;
	}
	traceEnt = &g_entities[tr.entityNum];
	if ( traceEnt->takedamage ) {
		G_Damage( traceEnt, ent, ent, forward, tr.endpos, damage, 0, MOD_SHOTGUN );
		return LogAccuracyHit( traceEnt, ent );
	}
	if ( fire && patch && tr.fraction < 1.0f ) {
		G_SpawnFirePatch( tr.endpos, ent, 3500, 64 );
	}
	return qfalse;
}

static void Ars_FireShotgun( gentity_t *ent, arsGrowth_t *g, int lvl, int evo, float quad,
	vec3_t muzzle, vec3_t forward ) {
	gentity_t	*tent;
	vec3_t		dir, localRight, localUp, end;
	int			code, count, spread, seed, i, patches = 0;
	float		r, u;
	qboolean	hit = qfalse, fire;

	code = ( lvl >= 2 ? 1 : 0 ) + ( lvl >= 5 ? 1 : 0 ) + ( evo >= 0 ? 1 : 0 );
	if ( lvl >= 4 ) {
		code |= ( evo >= 0 ? 2 : 1 ) << 4;
	}
	if ( evo >= 0 ) {
		code |= DSSG_EVOLVED;
	}
	fire = lvl >= 8 || evo >= 0;
	if ( fire ) {
		code |= DSSG_FIRE;
	}

	tent = G_TempEntity( muzzle, EV_SHOTGUN );
	VectorScale( forward, 4096, tent->s.origin2 );
	SnapVector( tent->s.origin2 );
	tent->s.eventParm = rand() & 255;
	tent->s.otherEntityNum = ent->s.number;
	tent->s.generic1 = code;

	// the same pattern as CG_ShotgunPattern (DSSG_ macros)
	seed = tent->s.eventParm;
	count = DEFAULT_SHOTGUN_COUNT + DSSG_PELLETS( code );
	spread = DSSG_SPREAD( code );
	VectorNormalize2( tent->s.origin2, dir );
	PerpendicularVector( localRight, dir );
	CrossProduct( dir, localRight, localUp );
	for ( i = 0 ; i < count ; i++ ) {
		r = Q_crandom( &seed ) * spread * 16;
		u = Q_crandom( &seed ) * spread * 16;
		VectorMA( tent->s.pos.trBase, 8192 * 16, dir, end );
		VectorMA( end, r, localRight, end );
		VectorMA( end, u, localUp, end );
		if ( Ars_Pellet( ent, tent->s.pos.trBase, end, forward, 10 * quad, fire, evo >= 0 && ( i % 5 ) == 0 && patches < 2 ) ) {
			hit = qtrue;
		}
		if ( evo >= 0 && ( i % 5 ) == 0 ) {
			patches++;
		}
	}
	if ( hit ) {
		ent->client->accuracy_hits++;
	}
}

static void Ars_Arc( gentity_t *ent, gentity_t *from, int damage, int maxTargets, float range, qboolean evolved, qboolean visual ) {
	gentity_t	*hit[8];
	gentity_t	*t, *tent;
	vec3_t		org, to, dir;
	int			n = 0, i;

	hit[n++] = from;
	VectorCopy( from->r.currentOrigin, org );
	org[2] += 8;
	for ( i = 0 ; i < maxTargets && n < 8 ; i++ ) {
		t = Ars_NearestEnemy( ent, evolved ? org : org, range, hit, n, NULL, 0 );
		if ( !t ) {
			break;
		}
		hit[n++] = t;
		VectorCopy( t->r.currentOrigin, to );
		to[2] += 8;
		if ( visual ) {
			tent = Ars_Fx( org, DSX_ARC, ent, WP_LIGHTNING );
			VectorCopy( to, tent->s.origin2 );
			SnapVector( tent->s.origin2 );
			tent->s.generic1 = evolved ? 1 : 0;
		}
		VectorSubtract( to, org, dir );
		arsInBlast++;
		G_Damage( t, ent, ent, dir, NULL, damage, DAMAGE_NO_KNOCKBACK, MOD_LIGHTNING );
		arsInBlast--;
		if ( !evolved ) {
			VectorCopy( to, org );		// a chain: the next jump starts at this target
		}
	}
}

static void Ars_FireLightning( gentity_t *ent, arsGrowth_t *g, int lvl, int evo, float quad,
	vec3_t muzzle, vec3_t forward ) {
	trace_t		tr;
	vec3_t		end;
	gentity_t	*traceEnt, *tent;
	int			damage = 8 * quad;
	int			shots = g->shots[WP_LIGHTNING];

	VectorMA( muzzle, LIGHTNING_RANGE, forward, end );
	trap_Trace( &tr, muzzle, NULL, NULL, end, ent->s.number, MASK_SHOT );
	if ( lvl >= 6 && shots % 12 == 0 ) {
		Ars_Refund( ent, WP_LIGHTNING );
	}
	if ( tr.entityNum == ENTITYNUM_NONE ) {
		return;
	}
	traceEnt = &g_entities[tr.entityNum];
	if ( traceEnt->takedamage ) {
		if ( LogAccuracyHit( traceEnt, ent ) ) {
			ent->client->accuracy_hits++;
		}
		G_Damage( traceEnt, ent, ent, forward, tr.endpos, damage, 0, MOD_LIGHTNING );
	}
	if ( traceEnt->takedamage && traceEnt->client ) {
		tent = G_TempEntity( tr.endpos, EV_MISSILE_HIT );
		tent->s.otherEntityNum = traceEnt->s.number;
		tent->s.eventParm = DirToByte( tr.plane.normal );
		tent->s.weapon = ent->s.weapon;
		if ( evo == EVO_STORMCALLER ) {
			Ars_Arc( ent, traceEnt, 3 * quad, 4, 450, qtrue, shots % 2 == 0 );
		} else if ( lvl >= 8 ) {
			Ars_Arc( ent, traceEnt, 3 * quad, 1, 350, qfalse, shots % 3 == 0 );
		}
	} else if ( !( tr.surfaceFlags & SURF_NOIMPACT ) ) {
		tent = G_TempEntity( tr.endpos, EV_MISSILE_MISS );
		tent->s.eventParm = DirToByte( tr.plane.normal );
	}
}

static void Ars_FireRailgun( gentity_t *ent, int lvl, int evo, float quad, vec3_t muzzle, vec3_t forward ) {
	trace_t		tr;
	vec3_t		start, dir, end, p;
	gentity_t	*tent;
	float		len, d;
	int			n;

	VectorCopy( muzzle, start );
	VectorCopy( forward, dir );
	if ( evo == EVO_JUDGEMENT ) {
		G_RailShot( ent, 100, 1, 2 );
		// the path detonates behind the slug (first segment, up to the first wall)
		VectorMA( start, 8192, dir, end );
		trap_Trace( &tr, start, NULL, NULL, end, ent->s.number, MASK_SOLID );
		len = Distance( start, tr.endpos );
		tent = Ars_Fx( start, DSX_JUDGEMENT, ent, WP_RAILGUN );
		VectorCopy( tr.endpos, tent->s.origin2 );
		SnapVector( tent->s.origin2 );
		for ( n = 0, d = 96 ; d < len && n < 8 ; d += 160, n++ ) {
			VectorMA( start, d, dir, p );
			Ars_Blast( p, ent, 15 * quad, 90, MOD_RAILGUN, NULL, -1 );
		}
		return;
	}
	G_RailShot( ent, 100, lvl >= 4 ? 1 : 0, 0 );
}

/*
===============================================================================

MISSILES

===============================================================================
*/

// HELLFIRE / ARMAGEDDON rockets: turn towards the nearest enemy ahead; armageddon drops bomblets
static void Ars_SeekThink( gentity_t *ent );

static gentity_t *Ars_Bomblet( gentity_t *owner, vec3_t origin, vec3_t dir, float speed, int fuse, qboolean evolved ) {
	gentity_t	*b;

	b = fire_grenade( owner, origin, dir );
	b->classname = "ds_bomblet";
	VectorScale( dir, speed, b->s.pos.trDelta );
	SnapVector( b->s.pos.trDelta );
	b->s.pos.trTime = level.time;
	b->damage = 12;
	b->splashDamage = 18;
	b->splashRadius = 72;
	b->nextthink = level.time + fuse;
	b->s.generic1 |= DSM_SMALL | ( evolved ? DSM_EVOLVED : 0 );
	b->dsLevel = DS_MAX_WLEVEL;
	b->dsTime = level.time;
	return b;
}

static void Ars_Burst( gentity_t *owner, vec3_t origin, int n, qboolean evolved ) {
	vec3_t	dir;
	int		i;
	float	a;

	for ( i = 0 ; i < n ; i++ ) {
		a = ( i + random() * 0.6f ) * 2 * M_PI / n;
		dir[0] = cos( a ) * 0.6f;
		dir[1] = sin( a ) * 0.6f;
		dir[2] = 0.8f;
		VectorNormalize( dir );
		Ars_Bomblet( owner, origin, dir, 260 + random() * 180, 500 + rand() % 450, evolved );
	}
}

static void Ars_SeekThink( gentity_t *ent ) {
	gentity_t	*owner, *target;
	vec3_t		dir, want, org, down;
	float		speed;

	if ( level.time >= ent->count ) {
		G_ExplodeMissile( ent );
		return;
	}
	ent->nextthink = level.time + FRAMETIME;
	if ( ent->r.ownerNum < 0 || ent->r.ownerNum >= MAX_CLIENTS || !g_entities[ent->r.ownerNum].inuse ) {
		return;
	}
	owner = &g_entities[ent->r.ownerNum];
	BG_EvaluateTrajectory( &ent->s.pos, level.time, org );
	VectorCopy( ent->s.pos.trDelta, dir );
	speed = VectorNormalize( dir );

	// armageddon: a bomblet every 220 ms of flight
	if ( ( ent->s.generic1 & DSM_UNION ) && level.time >= ent->dsTime ) {
		ent->dsTime = level.time + 220;
		VectorSet( down, crandom() * 0.3f, crandom() * 0.3f, -1 );
		VectorNormalize( down );
		Ars_Bomblet( owner, org, down, 120, 700 + rand() % 300, qtrue );
	}

	if ( level.time - ent->s.pos.trTime < 0 ) {
		return;
	}
	target = Ars_NearestEnemy( owner, org, 1400, NULL, 0, dir, 0.35f );
	if ( !target ) {
		return;
	}
	VectorSubtract( target->r.currentOrigin, org, want );
	want[2] += 10;
	VectorNormalize( want );
	VectorMA( dir, 0.22f, want, dir );
	VectorNormalize( dir );
	VectorCopy( org, ent->s.pos.trBase );
	SnapVector( ent->s.pos.trBase );
	VectorScale( dir, speed, ent->s.pos.trDelta );
	SnapVector( ent->s.pos.trDelta );
	ent->s.pos.trTime = level.time;
	VectorCopy( org, ent->r.currentOrigin );
}

static void Ars_FireRockets( gentity_t *ent, arsGrowth_t *g, int lvl, int evo, float quad, vec3_t muzzle, vec3_t forward ) {
	gentity_t	*m;
	vec3_t		angles, dir;
	int			i;

	if ( evo == EVO_HELLFIRE || evo == EVO_ARMAGEDDON ) {
		// a salvo of three seeking rockets
		for ( i = -1 ; i <= 1 ; i++ ) {
			vectoangles( forward, angles );
			angles[YAW] += i * 7;
			angles[PITCH] -= i ? 3 : 0;
			AngleVectors( angles, dir, NULL, NULL );
			m = fire_rocket( ent, muzzle, dir );
			Ars_Missile( ent, m, WP_ROCKET_LAUNCHER, lvl, evo, quad );
			m->damage = m->damage * 36 / 100;
			m->splashDamage = m->splashDamage * 36 / 100;
			m->count = level.time + 6000;
			m->think = Ars_SeekThink;
			m->nextthink = level.time + 150;
			m->dsTime = level.time + 200;
		}
		return;
	}
	m = fire_rocket( ent, muzzle, forward );
	Ars_Missile( ent, m, WP_ROCKET_LAUNCHER, lvl, evo, quad );
	if ( lvl >= 8 && g->shots[WP_ROCKET_LAUNCHER] % 4 == 0 ) {
		vectoangles( forward, angles );
		angles[YAW] += 5;
		AngleVectors( angles, dir, NULL, NULL );
		m = fire_rocket( ent, muzzle, dir );
		Ars_Missile( ent, m, WP_ROCKET_LAUNCHER, lvl, evo, quad );
		m->damage /= 2;
		m->splashDamage /= 2;
	}
}

// DOOMSDAY: a slow black hole that drags enemies in and zaps them, then collapses
static void Ars_BlackHoleThink( gentity_t *ent ) {
	gentity_t	*other, *owner;
	vec3_t		dir;
	trace_t		tr;
	float		dist, pull;
	int			i;

	if ( level.time >= ent->count ) {
		G_ExplodeMissile( ent );
		return;
	}
	ent->nextthink = level.time + FRAMETIME;
	if ( ent->r.ownerNum < 0 || ent->r.ownerNum >= MAX_CLIENTS ) {
		return;
	}
	owner = &g_entities[ent->r.ownerNum];
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		other = &g_entities[i];
		if ( !Ars_Enemy( owner, other ) || other->client->noclip ) {
			continue;
		}
		VectorSubtract( ent->r.currentOrigin, other->r.currentOrigin, dir );
		dist = VectorNormalize( dir );
		if ( dist > 560 || dist < 1 ) {
			continue;
		}
		trap_Trace( &tr, ent->r.currentOrigin, NULL, NULL, other->r.currentOrigin, ENTITYNUM_NONE, MASK_SOLID );
		if ( tr.fraction < 1.0f ) {
			continue;
		}
		pull = 3600 * ( 1.0f - dist / 560 ) * FRAMETIME / 1000.0f;
		VectorMA( other->client->ps.velocity, pull, dir, other->client->ps.velocity );
		if ( other->client->ps.groundEntityNum != ENTITYNUM_NONE && other->client->ps.velocity[2] < 80 ) {
			other->client->ps.velocity[2] = 80;
		}
		other->client->ps.pm_time = 100;
		other->client->ps.pm_flags |= PMF_TIME_KNOCKBACK;
		if ( dist < 160 && ( level.time / FRAMETIME ) % 4 == 0 ) {
			arsInBlast++;
			G_Damage( other, ent, owner, NULL, NULL, 3, DAMAGE_NO_KNOCKBACK, MOD_BFG );
			arsInBlast--;
		}
	}
}

static void Ars_FireProjectile( gentity_t *ent, arsGrowth_t *g, int w, int lvl, int evo, float quad,
	vec3_t muzzle, vec3_t forward, vec3_t right, vec3_t up ) {
	gentity_t	*m;
	vec3_t		dir, angles;
	int			i, n;

	switch ( w ) {
	case WP_GRENADE_LAUNCHER:
		VectorCopy( forward, dir );
		dir[2] += 0.2f;
		VectorNormalize( dir );
		m = fire_grenade( ent, muzzle, dir );
		Ars_Missile( ent, m, w, lvl, evo, quad );
		if ( lvl >= 5 ) {
			m->s.eFlags = ( m->s.eFlags & ~EF_BOUNCE_HALF ) | EF_BOUNCE;		// lively
		}
		break;
	case WP_PLASMAGUN:
		m = fire_plasma( ent, muzzle, forward );
		Ars_Missile( ent, m, w, lvl, evo, quad );
		if ( lvl >= 5 ) {
			m->splashDamage = m->splashDamage * 108 / 100;
		}
		if ( evo == EVO_STARFURNACE ) {
			VectorScale( forward, 1500 * Ars_SpeedScale( ent, w, lvl ), m->s.pos.trDelta );
			SnapVector( m->s.pos.trDelta );
			m->damage = 17 * quad;
			m->splashDamage = 13 * quad;
			m->splashRadius = 56 * Ars_AreaScale( ent, w, lvl );
			m->dsCount = 3;
		}
		break;
	case WP_BFG:
		m = fire_bfg( ent, muzzle, forward );
		Ars_Missile( ent, m, w, lvl, evo, quad );
		m->count = level.time + 10000;
		if ( evo == EVO_DOOMSDAY ) {
			VectorScale( forward, 650, m->s.pos.trDelta );
			SnapVector( m->s.pos.trDelta );
			m->count = level.time + 3200;
			m->think = Ars_BlackHoleThink;
			m->nextthink = level.time + FRAMETIME;
		} else {
			m->think = G_BFGThink;
			m->nextthink = level.time + 100;
		}
		break;
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case WP_NAILGUN:
		n = 15 + ( lvl >= 3 ? 1 : 0 ) + ( lvl >= 7 ? 1 : 0 );
		for ( i = 0 ; i < n ; i++ ) {
			m = fire_nail( ent, muzzle, forward, right, up );
			Ars_Missile( ent, m, w, lvl, evo, quad );
			if ( lvl >= 4 ) {
				m->s.eFlags |= EF_BOUNCE;
				m->count++;
			}
			if ( evo == EVO_IRONMAIDEN ) {
				m->dsCount = 3;
			}
		}
		break;
	case WP_PROX_LAUNCHER:
		n = 1;
		for ( i = 0 ; i < n ; i++ ) {
			vectoangles( forward, angles );
			angles[YAW] += i * 8;
			angles[PITCH] -= 4;
			AngleVectors( angles, dir, NULL, NULL );
			dir[2] += 0.2f;
			VectorNormalize( dir );
			m = fire_prox( ent, muzzle, dir );
			Ars_Missile( ent, m, w, lvl, evo, quad );
		}
		break;
#endif
	default:
		break;
	}
}

/*
=================
G_DS_FireWeapon

From FireWeapon once the muzzle is known. qtrue = the shot was fired here.
=================
*/
qboolean G_DS_FireWeapon( gentity_t *ent, vec3_t muzzleIn, vec3_t forwardIn, vec3_t rightIn, vec3_t upIn ) {
	arsGrowth_t	*g;
	vec3_t		muzzle, forward, right, up;
	int			w, lvl, evo;
	float		quad;

	if ( !( g = Ars_Growth( ent ) ) ) {
		return qfalse;
	}
	w = ent->s.weapon;
	if ( !DS_WEAPON_LEVELS( w ) || w == WP_GAUNTLET ) {
		return qfalse;
	}
	VectorCopy( muzzleIn, muzzle );
	VectorCopy( forwardIn, forward );
	VectorCopy( rightIn, right );
	VectorCopy( upIn, up );
	lvl = Ars_Level( g, w );
	evo = Ars_EvoFor( g, w );
	quad = ent->client->ps.powerups[PW_QUAD] ? g_quadfactor.value : 1;
	g->shots[w]++;
	if ( Ars_Debug() ) {
		G_LogPrintf( "DSFire: client %i weapon %i level %i evo %i time %i stat 0x%x ammo %i\n", ent->s.number, w, lvl,
			evo, level.time, ent->client->ps.stats[STAT_DSFIRE], ent->client->ps.ammo[w] );
	}

	switch ( w ) {
	case WP_MACHINEGUN:
		Ars_FireMachinegun( ent, g, lvl, evo, quad, muzzle, forward, right, up );
		break;
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case WP_CHAINGUN:
		Ars_FireChaingun( ent, g, lvl, evo, quad, muzzle, forward, right, up );
		break;
#endif
	case WP_SHOTGUN:
		Ars_FireShotgun( ent, g, lvl, evo, quad, muzzle, forward );
		break;
	case WP_LIGHTNING:
		Ars_FireLightning( ent, g, lvl, evo, quad, muzzle, forward );
		break;
	case WP_RAILGUN:
		Ars_FireRailgun( ent, lvl, evo, quad, muzzle, forward );
		break;
	case WP_ROCKET_LAUNCHER:
		Ars_FireRockets( ent, g, lvl, evo, quad, muzzle, forward );
		break;
	default:
		Ars_FireProjectile( ent, g, w, lvl, evo, quad, muzzle, forward, right, up );
		break;
	}
	return qtrue;
}

float G_DS_GauntletRange( gentity_t *ent ) {
	arsGrowth_t	*g = Ars_Growth( ent );

	if ( g && Ars_EvoFor( g, WP_GAUNTLET ) == EVO_HEARTRIPPER ) {
		return 72 * G_DS_SkillGauntletScale( ent );		// SKILLS: LONG ARM
	}
	return 32 * G_DS_SkillGauntletScale( ent );
}

int G_DS_ProxArmTime( gentity_t *mine ) {
	if ( !mine->dsLevel ) {
		return 2000;
	}
	if ( mine->dsEvo - 1 == EVO_MINEFIELD ) {
		return 600;
	}
	return mine->dsLevel >= 4 ? 1000 : 2000;
}

float G_DS_BFGLaserScale( gentity_t *ent ) {
	return ent->dsLevel >= 6 ? 1.35f : 1.0f;
}

/*
===============================================================================

MISSILE IMPACT / EXPLOSION

===============================================================================
*/

static gentity_t *Ars_MissileOwner( gentity_t *ent ) {
	if ( ent->r.ownerNum < 0 || ent->r.ownerNum >= MAX_CLIENTS ) {
		return NULL;
	}
	if ( !g_entities[ent->r.ownerNum].inuse || !g_entities[ent->r.ownerNum].client ) {
		return NULL;
	}
	return &g_entities[ent->r.ownerNum];
}

static void Ars_RedirectNail( gentity_t *ent, vec3_t from, gentity_t *target, float speed ) {
	vec3_t	dir;

	VectorSubtract( target->r.currentOrigin, from, dir );
	dir[2] += 12;
	VectorNormalize( dir );
	VectorCopy( from, ent->s.pos.trBase );
	SnapVector( ent->s.pos.trBase );
	VectorScale( dir, speed, ent->s.pos.trDelta );
	SnapVector( ent->s.pos.trDelta );
	ent->s.pos.trType = TR_LINEAR;
	ent->s.pos.trTime = level.time;
	VectorCopy( ent->s.pos.trBase, ent->r.currentOrigin );
	ent->dsCount--;
	trap_LinkEntity( ent );
}

qboolean G_DS_MissileImpact( gentity_t *ent, trace_t *trace ) {
	gentity_t	*other, *owner, *target, *tent, *m;
	vec3_t		velocity, from, dir, n;
	float		age, f, speed;
	int			i, evo;

	if ( !ent->dsLevel || !( owner = Ars_MissileOwner( ent ) ) ) {
		return qfalse;
	}
	other = &g_entities[trace->entityNum];
	evo = ent->dsEvo - 1;

	// level 8 plasma: bolts grow (up to double) as they fly
	if ( ent->s.weapon == WP_PLASMAGUN && ( ent->s.generic1 & DSM_SPECIAL ) ) {
		age = level.time - ent->dsTime;
		f = 1.0f + 0.35f * ( age > 700 ? 700 : age ) / 700.0f;
		ent->damage *= f;
		ent->splashDamage *= f;
		ent->splashRadius *= f;
		ent->s.generic1 &= ~DSM_SPECIAL;
	}

	// STAR FURNACE: the orb bursts into smaller orbs off the surface it hit
	if ( evo == EVO_STARFURNACE && ent->s.weapon == WP_PLASMAGUN && ent->dsCount > 0 && !( ent->s.generic1 & DSM_SMALL ) ) {
		VectorCopy( trace->plane.normal, n );
		if ( VectorLength( n ) < 0.5f ) {
			VectorSet( n, 0, 0, 1 );
		}
		VectorMA( trace->endpos, 6, n, from );
		for ( i = 0 ; i < ent->dsCount ; i++ ) {
			dir[0] = n[0] + crandom() * 0.9f;
			dir[1] = n[1] + crandom() * 0.9f;
			dir[2] = n[2] + crandom() * 0.9f;
			VectorNormalize( dir );
			m = fire_plasma( owner, from, dir );
			VectorScale( dir, 900, m->s.pos.trDelta );
			SnapVector( m->s.pos.trDelta );
			m->s.pos.trTime = level.time;
			m->damage = 5;
			m->splashDamage = 4;
			m->splashRadius = 40;
			m->nextthink = level.time + 420;
			m->s.generic1 |= DSM_SMALL | DSM_EVOLVED;
			m->dsLevel = DS_MAX_WLEVEL;
			m->dsEvo = EVO_STARFURNACE + 1;
			m->dsTime = level.time;
			if ( other->takedamage ) {
				m->target_ent = other;		// don't hit the same body at once
			}
		}
		ent->dsCount = 0;
		return qfalse;		// the orb itself explodes normally
	}

#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	// IRON MAIDEN: nails ricochet from enemy to enemy
	if ( evo == EVO_IRONMAIDEN && ent->s.weapon == WP_NAILGUN && ent->dsCount > 0 ) {
		BG_EvaluateTrajectoryDelta( &ent->s.pos, level.time, velocity );
		speed = VectorLength( velocity );
		if ( speed < 1400 ) {
			speed = 1400;
		}
		if ( other->takedamage ) {
			G_Damage( other, ent, owner, velocity, trace->endpos, ent->damage, 0, MOD_NAIL );
			if ( other->client ) {
				tent = G_TempEntity( trace->endpos, EV_MISSILE_HIT );
				tent->s.otherEntityNum = other->s.number;
				tent->s.eventParm = DirToByte( trace->plane.normal );
				tent->s.weapon = WP_NAILGUN;
			}
			ent->dsLast = other;
			VectorCopy( trace->endpos, from );
			target = Ars_NearestEnemy( owner, from, 700, &ent->dsLast, 1, NULL, 0 );
			if ( !target ) {
				G_FreeEntity( ent );
				return qtrue;
			}
			ent->target_ent = other;		// G_RunMissile passes it on the next trace
		} else {
			VectorMA( trace->endpos, 2, trace->plane.normal, from );
			target = Ars_NearestEnemy( owner, from, 700, &ent->dsLast, ent->dsLast ? 1 : 0, NULL, 0 );
			if ( !target ) {
				return qfalse;		// stock bounce / stop
			}
		}
		tent = Ars_Fx( from, DSX_RICOCHET, owner, WP_NAILGUN );
		Ars_RedirectNail( ent, from, target, speed );
		return qtrue;
	}
#endif
	return qfalse;
}

static void Ars_ChainExplode( gentity_t *mine ) {
	if ( mine->activator ) {
		G_FreeEntity( mine->activator );
		mine->activator = NULL;
	}
	G_ExplodeMissile( mine );
}

void G_DS_MissileExplode( gentity_t *ent, vec3_t origin ) {
	gentity_t	*owner, *m, *tent;
	int			evo, i;

	if ( !ent->dsLevel || !( owner = Ars_MissileOwner( ent ) ) ) {
		return;
	}
	evo = ent->dsEvo - 1;
	if ( ent->s.generic1 & DSM_SMALL ) {
		return;
	}
	switch ( ent->s.weapon ) {
	case WP_GRENADE_LAUNCHER:
		if ( evo == EVO_CLUSTERBOMB ) {
			Ars_Burst( owner, origin, 6, qtrue );
		} else if ( ent->dsLevel >= DS_MAX_WLEVEL ) {
			Ars_Burst( owner, origin, 3, qfalse );
		}
		break;
	case WP_ROCKET_LAUNCHER:
		if ( evo == EVO_ARMAGEDDON ) {
			Ars_Burst( owner, origin, 4, qtrue );
		}
		break;
	case WP_BFG:
		if ( evo == EVO_DOOMSDAY ) {
			tent = Ars_Fx( origin, DSX_BLACKHOLE, owner, WP_BFG );
			Ars_Blast( origin, owner, 55, 260, MOD_BFG_SPLASH, NULL, -1 );
		} else if ( ent->dsLevel >= DS_MAX_WLEVEL ) {
			G_SpawnFirePatch( origin, owner, 5000, 110 );
		}
		break;
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case WP_PROX_LAUNCHER:
		if ( evo == EVO_MINEFIELD || ent->dsLevel >= DS_MAX_WLEVEL ) {
			G_SpawnFirePatch( origin, owner, evo == EVO_MINEFIELD ? 5000 : 3500, evo == EVO_MINEFIELD ? 110 : 80 );
		}
		if ( evo == EVO_MINEFIELD ) {
			// the other mines nearby go off in a chain
			for ( i = MAX_CLIENTS, m = &g_entities[MAX_CLIENTS] ; i < level.num_entities ; i++, m++ ) {
				if ( !m->inuse || m == ent || m->s.eType != ET_MISSILE || m->s.weapon != WP_PROX_LAUNCHER
					|| m->r.ownerNum != ent->r.ownerNum || ( m->r.svFlags & SVF_NOCLIENT ) || m->think == Ars_ChainExplode ) {
					continue;
				}
				if ( Distance( m->r.currentOrigin, origin ) > 380 ) {
					continue;
				}
				m->think = Ars_ChainExplode;
				m->nextthink = level.time + 140;
			}
		}
		break;
#endif
	default:
		break;
	}
}

/*
===============================================================================

DAMAGE

===============================================================================
*/

int G_DS_ArsenalDamage( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, int damage, int mod ) {
	arsGrowth_t	*g;
	int			w, lvl, evo, base = damage;
	float		s;

	if ( arsInBlast || !attacker || !attacker->client || attacker == targ || !( g = Ars_Growth( attacker ) ) ) {
		return damage;
	}
	w = G_DS_WeaponForMod( mod );
	if ( !DS_WEAPON_LEVELS( w ) ) {
		return damage;
	}
	lvl = Ars_Level( g, w );
	evo = Ars_EvoFor( g, w );
	s = 1.0f + Ars_LvlSum( w, lvl, DS_STAT_DMG ) / 100.0f;
	if ( evo == EVO_HEARTRIPPER ) {
		s += 0.08f;
	} else if ( evo == EVO_RIPSAW ) {
		s += 0.05f;
	}
	damage = (int)( damage * s + 0.5f );
	// gib kills: gauntlet 8 / heartripper / ripsaw
	if ( targ->client && targ->health > 0 && damage >= targ->health
		&& ( ( w == WP_GAUNTLET && lvl >= 8 ) || evo == EVO_HEARTRIPPER || evo == EVO_RIPSAW ) ) {
		damage = targ->health - GIB_HEALTH + 10;
	}
	if ( Ars_Debug() && targ->client ) {
		G_LogPrintf( "DSDamage: attacker %i weapon %i level %i evo %i mod %i base %i scaled %i\n",
			attacker->s.number, w, lvl, evo, mod, base, damage );
	}
	return damage;
}

void G_DS_ArsenalHit( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, int take, int mod, vec3_t point, vec3_t dir ) {
	arsGrowth_t	*g;
	gentity_t	*tent;
	vec3_t		fwd, org;
	int			w, lvl, evo, max;
	qboolean	kill;

	if ( arsInBlast || take <= 0 || !attacker || !attacker->client || attacker == targ || !targ->client
		|| targ->health <= 0 || OnSameTeam( targ, attacker ) || !( g = Ars_Growth( attacker ) ) ) {
		return;
	}
	w = G_DS_WeaponForMod( mod );
	if ( !DS_WEAPON_LEVELS( w ) ) {
		return;
	}
	lvl = Ars_Level( g, w );
	evo = Ars_EvoFor( g, w );
	kill = targ->health - take <= 0;
	VectorCopy( targ->r.currentOrigin, org );

	switch ( w ) {
	case WP_GAUNTLET:
		if ( lvl >= 4 && !kill ) {
			AngleVectors( attacker->client->ps.viewangles, fwd, NULL, NULL );
			VectorMA( targ->client->ps.velocity, 380, fwd, targ->client->ps.velocity );
			if ( targ->client->ps.velocity[2] < 220 ) {
				targ->client->ps.velocity[2] = 220;
			}
			targ->client->ps.pm_time = 200;
			targ->client->ps.pm_flags |= PMF_TIME_KNOCKBACK;
		}
		if ( lvl >= 6 || evo == EVO_HEARTRIPPER ) {
			max = attacker->client->ps.stats[STAT_MAX_HEALTH];
			if ( attacker->health < max ) {
				attacker->health += evo == EVO_HEARTRIPPER ? 8 : 3;
				if ( attacker->health > max ) {
					attacker->health = max;
				}
				attacker->client->ps.stats[STAT_HEALTH] = attacker->health;
			}
		}
		if ( evo == EVO_HEARTRIPPER && point ) {
			tent = Ars_Fx( point, DSX_CLAW, attacker, WP_GAUNTLET );
			AngleVectors( attacker->client->ps.viewangles, fwd, NULL, NULL );
			VectorMA( point, 64, fwd, tent->s.origin2 );
			SnapVector( tent->s.origin2 );
		}
		if ( kill && ( lvl >= 8 || evo == EVO_HEARTRIPPER ) ) {
			Ars_DelayedBlast( org, attacker, evo == EVO_HEARTRIPPER ? 40 : 28, evo == EVO_HEARTRIPPER ? 160 : 120,
				MOD_GAUNTLET, DSX_GOREBURST, WP_GAUNTLET );
		}
		break;
	case WP_LIGHTNING:
		if ( lvl >= 3 ) {
			Ars_Slow( targ, evo == EVO_STORMCALLER ? 0.65f : 0.8f, 400 );
		}
		break;
	case WP_RAILGUN:
		if ( kill && lvl >= 8 ) {
			Ars_DelayedBlast( org, attacker, evo == EVO_JUDGEMENT ? 50 : 40, evo == EVO_JUDGEMENT ? 180 : 150,
				MOD_RAILGUN, DSX_BLAST, WP_RAILGUN );
		}
		break;
	case WP_SHOTGUN:
		if ( lvl >= 8 || evo >= 0 ) {
			G_ElementsIgnite( targ, attacker, evo >= 0 ? 3000 : 2000 );
		}
		break;
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case WP_NAILGUN:
		if ( lvl >= 8 ) {
			Ars_Slow( targ, 0.55f, 600 );
		}
		break;
#endif
	default:
		break;
	}
}

/*
===============================================================================

BOTS

===============================================================================
*/

int G_DS_BotWeaponBonus( gentity_t *ent, int weapon ) {
	arsGrowth_t	*g = Ars_Growth( ent );

	if ( !g || !DS_WEAPON_LEVELS( weapon ) ) {
		return 0;
	}
	return Ars_Level( g, weapon ) + ( Ars_EvoFor( g, weapon ) >= 0 ? 4 : 0 );
}

/*
===============================================================================

DEV COMMANDS

dslevel <weapon> <level> [client]	set a weapon level (default: the human)
dsxp <weapon> <xp> [client]			feed gem xp
dsevolve <evo> [client]				evolve (no checks)
dsstate								log every client's levels and evolutions
===============================================================================
*/

static gentity_t *Ars_ArgClient( int argn ) {
	char	arg[16];
	int		n;

	if ( trap_Argc() > argn ) {
		trap_Argv( argn, arg, sizeof( arg ) );
		n = atoi( arg );
	} else {
		n = G_RL_HumanClient();
	}
	if ( n < 0 || n >= level.maxclients || !g_entities[n].inuse || !g_entities[n].client ) {
		return NULL;
	}
	return &g_entities[n];
}

qboolean G_DS_ArsenalCommand( const char *cmd ) {
	char		arg[16];
	gentity_t	*ent;
	arsGrowth_t	*g;
	int			w, v, i, e;

	if ( !Q_stricmp( cmd, "dslevel" ) || !Q_stricmp( cmd, "dsxp" ) ) {
		trap_Argv( 1, arg, sizeof( arg ) );
		w = atoi( arg );
		trap_Argv( 2, arg, sizeof( arg ) );
		v = atoi( arg );
		ent = Ars_ArgClient( 3 );
		if ( !ent || !( g = Ars_Growth( ent ) ) || !DS_WEAPON_LEVELS( w ) ) {
			G_Printf( "dslevel/dsxp: no descent run, bad client or weapon\n" );
			return qtrue;
		}
		if ( !Q_stricmp( cmd, "dsxp" ) ) {
			G_DS_AddWeaponXP( ent, w, v );
		} else {
			if ( v < 1 ) {
				v = 1;
			} else if ( v > DS_MAX_WLEVEL ) {
				v = DS_MAX_WLEVEL;
			}
			g->level[w] = v;
			g->xp[w] = 0;
			Ars_Notify( ent, w, v, 0 );
		}
		G_Printf( "client %i weapon %i level %i xp %i\n", ent->s.number, w, Ars_Level( g, w ), g->xp[w] );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsevolve" ) ) {
		trap_Argv( 1, arg, sizeof( arg ) );
		e = atoi( arg );
		ent = Ars_ArgClient( 2 );
		if ( !ent || !Ars_Growth( ent ) || e < 0 || e >= EVO_NUM ) {
			G_Printf( "dsevolve: no descent run, bad client or evolution\n" );
			return qtrue;
		}
		G_DS_Evolve( ent, e );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsstate" ) ) {
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			ent = &g_entities[i];
			if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED || !( g = Ars_Growth( ent ) ) ) {
				continue;
			}
			G_LogPrintf( "DSState: client %i %s bot %i role %i levels %i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i evo 0x%x stat 0x%x\n",
				i, ent->client->pers.netname, ( ent->r.svFlags & SVF_BOT ) ? 1 : 0, G_RL_ClientRole( ent ),
				Ars_Level( g, 1 ), Ars_Level( g, 2 ), Ars_Level( g, 3 ), Ars_Level( g, 4 ), Ars_Level( g, 5 ), Ars_Level( g, 6 ),
				Ars_Level( g, 7 ), Ars_Level( g, 8 ), Ars_Level( g, 9 ), Ars_Level( g, 11 ), Ars_Level( g, 12 ), Ars_Level( g, 13 ),
				g->evo, ent->client->ps.stats[STAT_DSFIRE] );
		}
		return qtrue;
	}
	return qfalse;
}
