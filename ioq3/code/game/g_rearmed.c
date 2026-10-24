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
// g_rearmed.c -- rearmed match rules: feature switches, game modes, mutators,
// killstreaks, bounties and medals

#include "g_local.h"

#define INFINITE_AMMO		999
#define JUGGERNAUT_HEALTH	300
#define JUGGERNAUT_ARMOR	150
#define BOUNTY_STREAK		5		// a streak this long puts a bounty on you
#define MULTIKILL_TIME		3000	// kills this close together chain into a multi kill
#define OITC_MAX_SLUGS		5

#define MUTATOR_WARN_TIME	3000	// "incoming" warning before a random event starts
#define ROULETTE_TIME		8000	// weapon roulette: a new weapon this often
#define METEOR_WARN_TIME	1300	// ground marker to impact
#define STORM_WARN_TIME		1000
#define SKY_DROP_HEIGHT		1800	// meteors fall from at most this high

// random event state (map start resets it)
static int	rmPendingMutator;		// picked, "incoming" warning running
static int	rmMutatorStart;
static qboolean	rmMutatorFloor;		// the running event lasts the whole floor / map (no timer)
static int	rmNextEventTick;		// next meteor / lightning strike / roulette spin
static int	rmSkySpots = -1;		// spawn points under open sky, -1 = not counted yet

void G_StyleInit( void );
void G_PerksThornsFrame( void );
void G_PerkGive( gentity_t *ent, int perk );
void G_PerkForceOffer( gentity_t *ent );
static qboolean G_SlowMoAllowed( void );
#define SLOWMO_GAP		2500	// game msec after a slow motion before the next one (the match winner ignores it)

/*
===============
Feature switches

Every rearmed addition can be turned off. The movement / weapon ones that pmove
needs are copied into stats[STAT_RMFLAGS]; the whole set goes to the clients in CS_REARMED.
===============
*/
typedef struct {
	vmCvar_t	cv;
	const char	*name;
	const char	*def;
	int			rmf;		// RMF_ bit for pmove, or 0
	int			rmf2;		// RMF2_ bit for pmove, or 0
} rmSwitchDef_t;

static rmSwitchDef_t rmSwitches[RS_NUM] = {
	{ { 0 }, "g_doubleJump",		"1", RMF_DOUBLEJUMP },
	{ { 0 }, "g_wallJump",			"1", RMF_WALLJUMP },
	{ { 0 }, "g_wallRun",			"1", RMF_WALLRUN },
	{ { 0 }, "g_airDash",			"1", RMF_DASH },
	{ { 0 }, "g_jumpAssist",		"1", RMF_JUMPASSIST },
	{ { 0 }, "g_altFire",			"1", RMF_ALTFIRE },
	{ { 0 }, "g_shotgunKick",		"1", RMF_SHOTGUNKICK },
	{ { 0 }, "g_chaingunSpin",		"1", RMF_SPINUP },
	{ { 0 }, "g_ricochet",			"1", 0 },
	{ { 0 }, "g_stickyGrenades",	"1", 0 },
	{ { 0 }, "g_singularity",		"1", 0 },
	{ { 0 }, "g_bfgLasers",			"1", 0 },
	{ { 0 }, "g_uppercut",			"1", 0 },
	{ { 0 }, "g_headshots",			"1", 0 },
	{ { 0 }, "g_airshots",			"1", 0 },
	{ { 0 }, "g_killstreaks",		"1", 0 },
	{ { 0 }, "g_bounty",			"1", 0 },
	{ { 0 }, "g_medals",			"1", 0 },
	{ { 0 }, "g_rearmPowerups",		"1", 0 },
	{ { 0 }, "g_elements",			"1", 0 },
	{ { 0 }, "g_chargedAlt",		"1", RMF_CHARGE },
	{ { 0 }, "g_style",				"1", 0 },
	{ { 0 }, "g_perks",				"1", 0 },
	{ { 0 }, "g_slide",				"1", 0, RMF2_SLIDE },
	{ { 0 }, "g_slam",				"1", 0, RMF2_SLAM },
	{ { 0 }, "g_props",				"1", 0 },
	{ { 0 }, "g_corpsePhysics",		"1", 0 },
	{ { 0 }, "g_slowmo",			"1", 0 },
	{ { 0 }, "g_zipLine",			"1", 0, RMF2_ZIPLINE }
};

static int	rmSwitchBits;		// bit per RS_ switch that is on
static int	rmMoveFlags;		// RMF_ bits
static int	rmMoveFlags2;		// RMF2_ bits
static int	rmFragTarget;		// ds-economy: the frag target last published in CS_REARMED

qboolean G_RearmOn( int sw ) {
	return ( rmSwitchBits >> sw ) & 1;
}

int G_RearmMoveFlags( void ) {
	return rmMoveFlags;
}

int G_RearmMoveFlags2( void ) {
	return rmMoveFlags2 | ( level.mutator == MUT_ICE_RINK ? RMF2_ICE : 0 )
		| ( level.mutator == MUT_OVERCHARGE ? RMF2_OVERCHARGE : 0 );
}

static void G_RearmReadSwitches( void ) {
	int		i;

	rmSwitchBits = 0;
	rmMoveFlags = 0;
	rmMoveFlags2 = 0;
	for ( i = 0 ; i < RS_NUM ; i++ ) {
		if ( rmSwitches[i].cv.integer ) {
			rmSwitchBits |= 1 << i;
			rmMoveFlags |= rmSwitches[i].rmf;
			rmMoveFlags2 |= rmSwitches[i].rmf2;
		}
	}
}

/*
===============
G_RearmUpdateConfigstring

Tells the clients about the mode, the running mutator, the juggernaut,
the bounties and the feature switches
===============
*/
static void G_RearmUpdateConfigstring( void ) {
	char	bounties[MAX_CLIENTS * 3 + 1];
	int		i;

	bounties[0] = 0;
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( level.clients[i].pers.connected == CON_CONNECTED && level.clients[i].bounty ) {
			Q_strcat( bounties, sizeof( bounties ), va( "%i.", i ) );
		}
	}
	rmFragTarget = G_FragLimit();		// ds-economy: the frag target with g_fragMult (cgame: scoreboard / HUD)
	// mutend -1: the event lasts the whole floor (the HUD shows THIS FLOOR, no countdown)
	trap_SetConfigstring( CS_REARMED, va( "\\mode\\%i\\mut\\%i\\mutend\\%i\\jug\\%i\\sw\\%i\\bty\\%s\\mutstart\\%i\\mutnext\\%i\\mutat\\%i\\fl\\%i",
		level.rmMode, level.mutator, ( level.mutator && rmMutatorFloor ) ? -1 : level.mutatorEnd, level.juggernaut, rmSwitchBits, bounties,
		rmMutatorStart, rmPendingMutator, rmPendingMutator ? level.nextMutator : 0, rmFragTarget ) );
}

/*
===============
G_RearmAnnounce

Banner + sound on every client (client -1) or one client.
kind: streak, mutator, juggernaut, gungame, bounty, medal, warn, info
===============
*/
static void QDECL G_RearmAnnounceTo( int client, const char *kind, const char *fmt, ... ) {
	va_list		argptr;
	char		text[256];

	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );
	trap_SendServerCommand( client, va( "rmann %s \"%s\"", kind, text ) );
}

static void QDECL G_RearmAnnounce( const char *kind, const char *fmt, ... ) {
	va_list		argptr;
	char		text[256];

	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );
	trap_SendServerCommand( -1, va( "rmann %s \"%s\"", kind, text ) );
}

static void G_RearmSelectWeapon( gentity_t *ent, int weapon ) {
	ent->client->ps.weapon = weapon;
	ent->client->ps.weaponstate = WEAPON_READY;
	trap_SendServerCommand( ent - g_entities, va( "rmweapon %i", weapon ) );
}

static qboolean G_RearmAlive( gentity_t *ent ) {
	return ent->inuse && ent->client && ent->client->pers.connected == CON_CONNECTED
		&& ent->client->sess.sessionTeam != TEAM_SPECTATOR && ent->health > 0;
}

// best weapon the player still owns, for when one is taken away
static int G_RearmBestWeapon( gclient_t *cl ) {
	int		i;

	for ( i = WP_NUM_WEAPONS - 1 ; i > WP_GAUNTLET ; i-- ) {
		if ( i != WP_GRAPPLING_HOOK && ( cl->ps.stats[STAT_WEAPONS] & ( 1 << i ) ) && cl->ps.ammo[i] ) {
			return i;
		}
	}
	return WP_GAUNTLET;
}

/*
===============
G_RearmInit
===============
*/
void G_RearmInit( void ) {
	int		i;

	for ( i = 0 ; i < RS_NUM ; i++ ) {
		trap_Cvar_Register( &rmSwitches[i].cv, rmSwitches[i].name, rmSwitches[i].def, CVAR_ARCHIVE );
	}
	G_RearmReadSwitches();
	trap_Cvar_Register( NULL, "rearmed_version", REARMED_VERSION, CVAR_SERVERINFO | CVAR_ROM );
	G_SlowMoReset();

	level.rmMode = g_rearmMode.integer;
	if ( level.rmMode < 0 || level.rmMode >= RM_NUM_MODES ) {
		level.rmMode = RM_MODE_NORMAL;
	}
	// gun game, juggernaut and one in the chamber are free for all modes,
	// and a juggernaut duel is just a beating
	if ( ( level.rmMode == RM_MODE_GUNGAME || level.rmMode == RM_MODE_JUGGERNAUT || level.rmMode == RM_MODE_OITC )
		&& g_gametype.integer >= GT_TEAM ) {
		G_Printf( "%s is free for all only, playing Normal\n", bg_rmModeNames[level.rmMode] );
		level.rmMode = RM_MODE_NORMAL;
	}
	if ( level.rmMode == RM_MODE_JUGGERNAUT && g_gametype.integer == GT_TOURNAMENT ) {
		level.rmMode = RM_MODE_NORMAL;
	}
	G_RL_Init();		// descent: reads the run (no active run: Normal)
	G_DS_SkillsInit();	// SKILLS: permanent skill xp + perk ranks (after G_RL_Init: DESCENT or not)
	level.juggernaut = -1;
	level.mutator = MUT_NONE;
	level.mutatorEnd = 0;
	level.nextMutator = level.time + g_mutatorInterval.integer * 1000;
	level.firstBlood = qfalse;
	rmPendingMutator = MUT_NONE;
	rmMutatorStart = 0;
	rmMutatorFloor = qfalse;
	rmNextEventTick = 0;
	rmSkySpots = -1;
	G_StyleInit();
	G_ElementsInit();
	// a new level starts every ladder and medal counter over
	for ( i = 0 ; i < MAX_CLIENTS ; i++ ) {
		level.clients[i].pers.ggLevel = 0;
		level.clients[i].pers.lastKiller = -1;
		level.clients[i].pers.deathStreak = 0;
		level.clients[i].pers.multiKills = 0;
		level.clients[i].pers.multiKillTime = 0;
		level.clients[i].pers.perks = 0;
		level.clients[i].pers.perkKills = 0;
		level.clients[i].pers.perkOffer[0] = -1;
	}
	G_RearmUpdateConfigstring();
}

/*
===============
Mode queries
===============
*/
qboolean G_RearmHandlesScore( void ) {
	return level.rmMode == RM_MODE_GUNGAME || level.rmMode == RM_MODE_JUGGERNAUT;
}

static qboolean G_RearmFixedWeapons( void ) {
	return level.rmMode == RM_MODE_GUNGAME || level.rmMode == RM_MODE_INSTAGIB || level.rmMode == RM_MODE_OITC;
}

qboolean G_RearmItemAllowed( gitem_t *item ) {
	if ( !G_DS_RmItemAllowed( item ) ) {
		return qfalse;		// en-rematch: NO HEALTH
	}
	if ( G_RearmFixedWeapons() ) {
		// fixed weapons; the other items stay (and give bots somewhere to go)
		return item->giType != IT_WEAPON && item->giType != IT_AMMO;
	}
	return qtrue;
}

qboolean G_RearmDropsWeapons( void ) {
	return !G_RearmFixedWeapons();
}

qboolean G_RearmIgnoresFraglimit( void ) {
	return level.rmMode == RM_MODE_GUNGAME;		// the ladder ends the match
}

int G_RearmRailDamage( int damage ) {
	return ( level.rmMode == RM_MODE_INSTAGIB || level.rmMode == RM_MODE_OITC ) ? 1000 : damage;
}

float G_RearmGravityScale( void ) {
	return level.mutator == MUT_LOW_GRAVITY ? 0.35f : 1.0f;
}

float G_RearmSpeedScale( void ) {
	return level.mutator == MUT_SPEED_RUSH ? 1.4f : 1.0f;
}

float G_RearmKnockbackScale( void ) {
	return level.mutator == MUT_KNOCKBACK_CHAOS ? 3.0f : 1.0f;
}

int G_RearmHeadshotBelowEyes( void ) {
	return level.mutator == MUT_BIG_HEADS ? 26 : 6;
}

/*
===============
G_RearmOnDamage

Vampirism: part of the damage dealt heals the attacker
===============
*/
void G_RearmOnDamage( gentity_t *targ, gentity_t *attacker, int damage ) {
	gclient_t	*cl = attacker->client;
	int			max;

	G_PerkDamageTaken( targ, attacker, damage );		// thorns
	if ( !cl || !targ->client || targ == attacker || attacker->health <= 0 || OnSameTeam( targ, attacker ) ) {
		return;
	}
	// blood moon: a quarter of the damage dealt heals you
	if ( level.mutator == MUT_BLOOD_MOON && attacker->health < cl->ps.stats[STAT_MAX_HEALTH] ) {
		attacker->health += ( damage + 2 ) / 4;
		if ( attacker->health > cl->ps.stats[STAT_MAX_HEALTH] ) {
			attacker->health = cl->ps.stats[STAT_MAX_HEALTH];
		}
		cl->ps.stats[STAT_HEALTH] = attacker->health;
	}
	if ( !cl->ps.powerups[PW_VAMPIRE] && level.mutator != MUT_VAMPIRE ) {
		if ( G_HasPerk( attacker, PERK_BLOODTHIRST ) && attacker->health < cl->ps.stats[STAT_MAX_HEALTH] ) {
			attacker->health += ( damage * ( G_RL_Active() ? DSP_BLOODTHIRST_PCT : 15 ) + 50 ) / 100;	// DESCENT: single digits
			if ( attacker->health > cl->ps.stats[STAT_MAX_HEALTH] ) {
				attacker->health = cl->ps.stats[STAT_MAX_HEALTH];
			}
			cl->ps.stats[STAT_HEALTH] = attacker->health;
		}
		return;
	}
	max = cl->ps.stats[STAT_MAX_HEALTH] * 2;
	attacker->health += damage / 2;
	if ( attacker->health > max ) {
		attacker->health = max;
	}
	cl->ps.stats[STAT_HEALTH] = attacker->health;
}

/*
===============
G_RearmGivePowerup
===============
*/
static void G_RearmGivePowerup( gentity_t *ent, int powerup, int msec ) {
	int		*pw = &ent->client->ps.powerups[powerup];

	if ( *pw == INT_MAX ) {
		return;		// already unlimited (quad madness, juggernaut)
	}
	if ( *pw < level.time ) {
		*pw = level.time;
	}
	*pw += msec;
}

/*
===============
Gun game
===============
*/
static void G_RearmGunGameLoadout( gentity_t *ent ) {
	gclient_t	*cl = ent->client;
	int			weapon;

	if ( cl->pers.ggLevel >= GG_LEVELS ) {
		cl->pers.ggLevel = GG_LEVELS - 1;
	}
	weapon = bg_gunGameLadder[cl->pers.ggLevel];
	cl->ps.stats[STAT_WEAPONS] = 1 << weapon;
	cl->ps.ammo[weapon] = weapon == WP_GAUNTLET ? -1 : INFINITE_AMMO;
	cl->ps.stats[STAT_GGLEVEL] = cl->pers.ggLevel;
	cl->ps.persistant[PERS_SCORE] = cl->pers.ggLevel;
}

static void G_RearmGunGameKill( gentity_t *self, gentity_t *attacker, int mod ) {
	gclient_t	*cl = attacker->client;

	// humiliation: the gauntlet knocks the victim down a level
	if ( mod == MOD_GAUNTLET && self->client->pers.ggLevel > 0 ) {
		self->client->pers.ggLevel--;
		self->client->ps.stats[STAT_GGLEVEL] = self->client->pers.ggLevel;
		self->client->ps.persistant[PERS_SCORE] = self->client->pers.ggLevel;
		G_RearmAnnounce( "info", "%s^7 knocked %s^7 down a level", cl->pers.netname, self->client->pers.netname );
	}

	cl->pers.ggLevel++;
	if ( cl->pers.ggLevel >= GG_LEVELS ) {
		cl->ps.persistant[PERS_SCORE] = GG_LEVELS;
		CalculateRanks();
		G_RearmAnnounce( "gungame", "%s^7 WINS THE GUN GAME", cl->pers.netname );
		LogExit( "Gun game finished." );
		return;
	}
	G_RearmGunGameLoadout( attacker );
	G_RearmSelectWeapon( attacker, bg_gunGameLadder[cl->pers.ggLevel] );
	if ( cl->pers.ggLevel == GG_LEVELS - 1 ) {
		G_RearmAnnounce( "gungame", "%s^7 IS ON GAUNTLET - FINAL KILL", cl->pers.netname );
	}
	CalculateRanks();
}

/*
===============
Juggernaut
===============
*/
static void G_RearmMakeJuggernaut( gentity_t *ent ) {
	gentity_t	*old;

	if ( level.juggernaut >= 0 && ( !ent || level.juggernaut != ent - g_entities ) ) {
		old = &g_entities[level.juggernaut];
		if ( old->client ) {
			old->client->ps.stats[STAT_MAX_HEALTH] = old->client->pers.maxHealth;
			old->client->ps.powerups[PW_QUAD] = 0;
		}
	}
	level.juggernaut = ent ? ent - g_entities : -1;
	if ( ent && ent->client ) {
		ent->client->ps.stats[STAT_MAX_HEALTH] = JUGGERNAUT_HEALTH;
		ent->health = ent->client->ps.stats[STAT_HEALTH] = JUGGERNAUT_HEALTH;
		ent->client->ps.stats[STAT_ARMOR] = JUGGERNAUT_ARMOR;
		G_RearmAnnounce( "juggernaut", "%s^7 IS THE JUGGERNAUT", ent->client->pers.netname );
	}
	G_RearmUpdateConfigstring();
}

static void G_RearmJuggernautKill( gentity_t *self, gentity_t *attacker ) {
	int		victim = self - g_entities;
	int		killer = attacker - g_entities;
	qboolean	realKill = attacker->client && attacker != self;

	if ( realKill && ( killer == level.juggernaut || victim == level.juggernaut ) ) {
		attacker->client->ps.persistant[PERS_SCORE]++;
		CalculateRanks();
	}
	if ( victim == level.juggernaut || level.juggernaut < 0 ) {
		if ( realKill ) {
			G_RearmMakeJuggernaut( attacker );
		} else {
			G_RearmMakeJuggernaut( NULL );		// suicide: the next frag takes the crown
		}
	}
}

/*
===============
One in the chamber: one slug, a kill reloads one, a miss leaves you with the gauntlet
===============
*/
static void G_RearmOITCKill( gentity_t *attacker ) {
	gclient_t	*cl = attacker->client;

	if ( cl->ps.ammo[WP_RAILGUN] < OITC_MAX_SLUGS ) {
		cl->ps.ammo[WP_RAILGUN]++;
	}
	if ( cl->ps.weapon == WP_GAUNTLET && attacker->health > 0 ) {
		G_RearmSelectWeapon( attacker, WP_RAILGUN );
	}
}

/*
===============
Medals: short personal callouts for notable kills
===============
*/
static void G_RearmMedal( gentity_t *ent, const char *name ) {
	if ( G_RearmOn( RS_MEDALS ) ) {
		G_RearmAnnounceTo( ent - g_entities, "medal", "%s", name );
	}
}

static void G_RearmMedals( gentity_t *self, gentity_t *attacker, int mod ) {
	gclient_t	*cl = attacker->client;

	if ( !G_RearmOn( RS_MEDALS ) ) {
		return;
	}
	if ( !level.firstBlood ) {
		level.firstBlood = qtrue;
		G_RearmAnnounce( "medal", "%s^7 draws FIRST BLOOD", cl->pers.netname );
	}
	if ( cl->pers.lastKiller == self - g_entities ) {
		cl->pers.lastKiller = -1;
		G_RearmMedal( attacker, "REVENGE" );
	}
	if ( attacker->health <= 0 ) {
		G_RearmMedal( attacker, "FROM THE GRAVE" );
	}
	if ( self->client->streak >= BOUNTY_STREAK ) {
		G_RearmMedal( attacker, "SHUTDOWN" );
	}
	if ( cl->pers.deathStreak >= 3 ) {
		G_RearmMedal( attacker, "COMEBACK" );
	}
	if ( self->client->ps.groundEntityNum == ENTITYNUM_NONE && attacker->client->ps.groundEntityNum == ENTITYNUM_NONE
		&& mod != MOD_FALLING ) {
		G_RearmMedal( attacker, "MID-AIR" );
	}
	if ( level.time - cl->pers.multiKillTime < MULTIKILL_TIME ) {
		cl->pers.multiKills++;
	} else {
		cl->pers.multiKills = 1;
	}
	cl->pers.multiKillTime = level.time;
	if ( cl->pers.multiKills == 3 ) {
		G_RearmMedal( attacker, "TRIPLE KILL" );
	} else if ( cl->pers.multiKills == 4 ) {
		G_RearmMedal( attacker, "QUAD KILL" );
	} else if ( cl->pers.multiKills >= 5 ) {
		G_RearmMedal( attacker, "RAMPAGE KILL" );
	}
}

/*
===============
Bounties: a long streak puts a price on your head, ending it pays out
===============
*/
static void G_RearmClaimBounty( gentity_t *self, gentity_t *attacker ) {
	gclient_t	*cl;

	if ( !self->client->bounty ) {
		return;
	}
	self->client->bounty = qfalse;
	if ( attacker && attacker->client && attacker != self && !OnSameTeam( self, attacker ) && attacker->health > 0 ) {
		cl = attacker->client;
		G_RearmAnnounce( "bounty", "%s^7 CLAIMED THE BOUNTY ON %s", cl->pers.netname, self->client->pers.netname );
		if ( !G_RearmHandlesScore() ) {
			AddScore( attacker, self->r.currentOrigin, 1 );
		}
		if ( attacker->health < cl->ps.stats[STAT_MAX_HEALTH] ) {
			attacker->health = cl->ps.stats[STAT_HEALTH] = cl->ps.stats[STAT_MAX_HEALTH];
		}
		cl->ps.stats[STAT_ARMOR] += 50;
		if ( cl->ps.stats[STAT_ARMOR] > cl->ps.stats[STAT_MAX_HEALTH] * 2 ) {
			cl->ps.stats[STAT_ARMOR] = cl->ps.stats[STAT_MAX_HEALTH] * 2;
		}
	}
	G_RearmUpdateConfigstring();
}

/*
===============
Killstreaks
===============
*/
static void G_RearmStreak( gentity_t *attacker ) {
	gclient_t	*cl = attacker->client;
	qboolean	rewards;

	// no rewards in the fixed-weapon modes, nor for the juggernaut
	rewards = G_RearmOn( RS_KILLSTREAKS ) && !G_RearmFixedWeapons() && level.juggernaut != attacker - g_entities;

	cl->streak++;
	switch ( cl->streak ) {
	case 3:
		G_RearmAnnounce( "streak", "%s^7 is on a RAMPAGE", cl->pers.netname );
		if ( rewards ) {
			G_RearmGivePowerup( attacker, PW_HASTE, 10000 );
		}
		break;
	case 5:
		G_RearmAnnounce( "streak", "%s^7 is UNSTOPPABLE", cl->pers.netname );
		if ( rewards ) {
			G_RearmGivePowerup( attacker, PW_VAMPIRE, 15000 );
		}
		break;
	case 8:
		G_RearmAnnounce( "streak", "%s^7 is GODLIKE", cl->pers.netname );
		if ( rewards ) {
			G_RearmGivePowerup( attacker, PW_RAPIDFIRE, 15000 );
		}
		break;
	case 12:
		G_RearmAnnounce( "streak", "%s^7 is LEGENDARY", cl->pers.netname );
		if ( rewards ) {
			G_RearmGivePowerup( attacker, PW_QUAD, 10000 );
		}
		break;
	default:
		break;
	}
	if ( cl->streak == BOUNTY_STREAK && !cl->bounty && G_RearmOn( RS_BOUNTY ) && level.rmMode != RM_MODE_GUNGAME
		&& level.juggernaut != attacker - g_entities ) {
		cl->bounty = qtrue;
		G_RearmAnnounce( "bounty", "BOUNTY ON %s^7 - END THE STREAK", cl->pers.netname );
		G_RearmUpdateConfigstring();
	}
}

/*
===============
G_RearmPlayerDie

Called from player_die before scoring
===============
*/
void G_RearmPlayerDie( gentity_t *self, gentity_t *attacker, int mod ) {
	qboolean	realKill = attacker && attacker->client && attacker != self && !OnSameTeam( self, attacker );

	G_RL_PlayerDie( self, attacker );
	G_DS_LootPlayerDie( self, attacker, mod );		// DESCENT LOOT: gems, chests, the vacuum
	// warmup kills count for nothing
	if ( level.warmupTime ) {
		self->client->streak = 0;
		self->client->bounty = qfalse;
		return;
	}

	if ( realKill ) {
		G_RearmMedals( self, attacker, mod );
		self->client->pers.lastKiller = attacker - g_entities;
		attacker->client->pers.deathStreak = 0;
	}
	self->client->pers.deathStreak++;

	if ( self->client->streak >= 3 && realKill ) {
		G_RearmAnnounce( "info", "%s^7 ended %s^7's %i kill streak", attacker->client->pers.netname,
			self->client->pers.netname, self->client->streak );
	}
	G_RearmClaimBounty( self, attacker );
	self->client->streak = 0;

	if ( realKill ) {
		G_RearmStreak( attacker );
	}

	switch ( level.rmMode ) {
	case RM_MODE_GUNGAME:
		if ( realKill ) {
			G_RearmGunGameKill( self, attacker, mod );
		}
		break;
	case RM_MODE_JUGGERNAUT:
		G_RearmJuggernautKill( self, attacker ? attacker : self );
		break;
	case RM_MODE_OITC:
		if ( realKill ) {
			G_RearmOITCKill( attacker );
		}
		break;
	default:
		break;
	}
}

/*
===============
G_RearmClientDisconnect
===============
*/
void G_RearmClientDisconnect( gentity_t *ent ) {
	int		i;

	G_RL_ClientDisconnect( ent );
	G_DS_ArsenalDisconnect( ent );
	if ( ent->client->bounty ) {
		ent->client->bounty = qfalse;
		G_RearmUpdateConfigstring();
	}
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( level.clients[i].pers.lastKiller == ent - g_entities ) {
			level.clients[i].pers.lastKiller = -1;
		}
	}
}

/*
===============
G_RearmSpawnLoadout

Called at the end of ClientSpawn
===============
*/
void G_RearmSpawnLoadout( gentity_t *ent ) {
	gclient_t	*cl = ent->client;

	cl->streak = 0;
	if ( cl->bounty ) {
		cl->bounty = qfalse;
		G_RearmUpdateConfigstring();
	}
	if ( cl->sess.sessionTeam == TEAM_SPECTATOR ) {
		return;
	}
	G_RL_SpawnLoadout( ent );				// descent: run perks, armor, elite / boss
	G_PerkApplyMaxHealth( ent, qtrue );		// thick skin / glass cannon
	switch ( level.rmMode ) {
	case RM_MODE_GUNGAME:
		G_RearmGunGameLoadout( ent );
		cl->ps.weapon = bg_gunGameLadder[cl->pers.ggLevel];
		break;
	case RM_MODE_INSTAGIB:
		cl->ps.stats[STAT_WEAPONS] = ( 1 << WP_RAILGUN ) | ( 1 << WP_GAUNTLET );
		cl->ps.ammo[WP_RAILGUN] = INFINITE_AMMO;
		cl->ps.weapon = WP_RAILGUN;
		break;
	case RM_MODE_OITC:
		cl->ps.stats[STAT_WEAPONS] = ( 1 << WP_RAILGUN ) | ( 1 << WP_GAUNTLET );
		cl->ps.ammo[WP_RAILGUN] = 1;
		cl->ps.weapon = WP_RAILGUN;
		break;
	default:
		break;
	}
	cl->frenzyHadLauncher = ( cl->ps.stats[STAT_WEAPONS] & ( 1 << WP_ROCKET_LAUNCHER ) ) != 0;
	cl->frenzyAmmo = cl->ps.ammo[WP_ROCKET_LAUNCHER];
	if ( level.mutator == MUT_ROCKET_FRENZY ) {
		cl->ps.stats[STAT_WEAPONS] |= 1 << WP_ROCKET_LAUNCHER;
		cl->ps.ammo[WP_ROCKET_LAUNCHER] = INFINITE_AMMO;
		cl->ps.weapon = WP_ROCKET_LAUNCHER;
	}
}

/*
===============
Mutators
===============
*/
static const int rmMutatorWeight[MUT_NUM_MUTATORS] = {
	0,		// none
	3,		// low gravity
	2,		// vampire
	2,		// rocket frenzy
	3,		// speed rush
	2,		// big heads
	2,		// knockback chaos
	1,		// quad madness
	3,		// jetpack party
	3,		// blood moon
	3,		// meteor shower
	2,		// overcharge
	2,		// bullet time
	2,		// ice rink
	2,		// barrel bonanza
	3,		// weapon roulette
	3,		// thunderstorm
	3		// chain reaction
};

/*
===============
G_SkyAbove

Open sky straight up from a point: returns the sky point (a little under the sky brush)
===============
*/
static qboolean G_SkyAbove( const vec3_t point, vec3_t sky ) {
	trace_t	tr;
	vec3_t	start, end;

	VectorCopy( point, start );
	start[2] += 8;
	VectorCopy( start, end );
	end[2] += 16384;
	trap_Trace( &tr, start, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID );
	if ( tr.startsolid ) {
		return qfalse;
	}
	if ( tr.fraction < 1.0f && !( tr.surfaceFlags & SURF_SKY ) ) {
		return qfalse;		// a roof
	}
	// open to the sky (or to nothing at all): the rocks come from at most this high
	VectorCopy( tr.endpos, sky );
	sky[2] -= 16;
	if ( sky[2] > point[2] + SKY_DROP_HEIGHT ) {
		sky[2] = point[2] + SKY_DROP_HEIGHT;
	}
	return sky[2] - point[2] > 128;
}

/*
===============
G_RearmCountSky

How many deathmatch spawns are under open sky (meteors and lightning need some)
===============
*/
static int G_RearmCountSky( void ) {
	gentity_t	*spot = NULL;
	vec3_t		sky;
	int			n = 0;

	while ( ( spot = G_Find( spot, FOFS( classname ), "info_player_deathmatch" ) ) != NULL ) {
		if ( G_SkyAbove( spot->s.origin, sky ) ) {
			n++;
		}
	}
	return n;
}

static qboolean G_RearmMutatorAllowed( int mut ) {
	qboolean	fixed = G_RearmFixedWeapons();

	if ( mut <= MUT_NONE || mut >= MUT_NUM_MUTATORS || mut == level.lastMutator ) {
		return qfalse;
	}
	switch ( mut ) {
	case MUT_METEOR_SHOWER:
	case MUT_THUNDERSTORM:
		if ( rmSkySpots < 0 ) {
			rmSkySpots = G_RearmCountSky();
		}
		if ( rmSkySpots < 2 ) {
			return qfalse;		// indoor map
		}
		break;
	case MUT_BULLET_TIME:
		if ( !G_SlowMoAllowed() ) {
			return qfalse;
		}
		break;
	case MUT_BARREL_BONANZA:
		if ( !G_RearmOn( RS_PROPS ) ) {
			return qfalse;
		}
		break;
	case MUT_OVERCHARGE:
		if ( !G_RearmOn( RS_ALTFIRE ) || !G_RearmOn( RS_CHARGE ) ) {
			return qfalse;
		}
		break;
	case MUT_WEAPON_ROULETTE:
		if ( fixed ) {
			return qfalse;
		}
		break;
	default:
		break;
	}
	if ( level.rmMode == RM_MODE_INSTAGIB || level.rmMode == RM_MODE_OITC ) {
		return mut != MUT_ROCKET_FRENZY && mut != MUT_VAMPIRE && mut != MUT_QUAD_MADNESS && mut != MUT_BIG_HEADS
			&& mut != MUT_BLOOD_MOON;
	}
	if ( level.rmMode == RM_MODE_GUNGAME ) {
		return mut != MUT_ROCKET_FRENZY;
	}
	return qtrue;
}

static int G_RearmPickMutator( void ) {
	int		i, total, r;

	total = 0;
	for ( i = 1 ; i < MUT_NUM_MUTATORS ; i++ ) {
		if ( G_RearmMutatorAllowed( i ) ) {
			total += rmMutatorWeight[i];
		}
	}
	if ( total <= 0 ) {
		return MUT_NONE;
	}
	r = rand() % total;
	for ( i = 1 ; i < MUT_NUM_MUTATORS ; i++ ) {
		if ( G_RearmMutatorAllowed( i ) ) {
			r -= rmMutatorWeight[i];
			if ( r < 0 ) {
				return i;
			}
		}
	}
	return MUT_NONE;
}

/*
===============
Weapon roulette
===============
*/
typedef struct {
	int		weapon;
	int		ammo;
} rmRouletteWeapon_t;

static const rmRouletteWeapon_t rmRoulette[] = {
	{ WP_MACHINEGUN, 100 },
	{ WP_SHOTGUN, 20 },
	{ WP_GRENADE_LAUNCHER, 15 },
	{ WP_ROCKET_LAUNCHER, 15 },
	{ WP_LIGHTNING, 120 },
	{ WP_RAILGUN, 15 },
	{ WP_PLASMAGUN, 80 },
	{ WP_BFG, 12 },
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	{ WP_NAILGUN, 30 },
	{ WP_PROX_LAUNCHER, 12 },
	{ WP_CHAINGUN, 150 },
#endif
};
#define NUM_ROULETTE	( (int)( sizeof( rmRoulette ) / sizeof( rmRoulette[0] ) ) )

static void G_RearmRouletteSpin( void ) {
	gentity_t	*ent;
	gclient_t	*cl;
	int			i, k, tries;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		ent = &g_entities[i];
		if ( !G_RearmAlive( ent ) ) {
			continue;
		}
		cl = ent->client;
		tries = 0;
		do {
			k = rand() % NUM_ROULETTE;
		} while ( rmRoulette[k].weapon == cl->ps.weapon && ++tries < 8 );
		cl->ps.stats[STAT_WEAPONS] |= 1 << rmRoulette[k].weapon;
		if ( cl->ps.ammo[rmRoulette[k].weapon] < rmRoulette[k].ammo ) {
			cl->ps.ammo[rmRoulette[k].weapon] = rmRoulette[k].ammo;
		}
		G_RearmSelectWeapon( ent, rmRoulette[k].weapon );
	}
}

/*
===============
Strike markers: a glowing ring on the ground where a meteor or a lightning bolt will land
===============
*/
static gentity_t *G_StrikeMarker( vec3_t point, int kind, int radius, int msec ) {
	gentity_t	*ent;

	ent = G_Spawn();
	ent->classname = "strike_marker";
	ent->s.eType = ET_GENERAL;
	ent->s.generic1 = RM_ENT_STRIKE;
	ent->s.modelindex2 = kind;
	ent->s.time = level.time;
	ent->s.time2 = level.time + msec;
	ent->s.otherEntityNum2 = radius;
	ent->random = radius;
	ent->think = G_FreeEntity;
	ent->nextthink = level.time + msec;
	G_SetOrigin( ent, point );
	trap_LinkEntity( ent );
	return ent;
}

/*
===============
G_FindStrikeSpot

A floor spot near a point with open sky above it
===============
*/
static qboolean G_FindStrikeSpot( const vec3_t near, float minR, float maxR, float yaw, vec3_t ground, vec3_t sky ) {
	trace_t	tr;
	vec3_t	from, to;
	float	ang, r;
	int		tries;

	VectorCopy( near, from );
	from[2] += 24;
	for ( tries = 0 ; tries < 10 ; tries++ ) {
		ang = yaw >= 0 ? DEG2RAD( yaw ) + crandom() * 0.3f : random() * 2 * M_PI;
		r = minR + random() * ( maxR - minR );
		to[0] = from[0] + cos( ang ) * r;
		to[1] = from[1] + sin( ang ) * r;
		to[2] = from[2];
		// stay on our side of the walls
		trap_Trace( &tr, from, NULL, NULL, to, ENTITYNUM_NONE, MASK_SOLID );
		if ( tr.startsolid ) {
			continue;
		}
		VectorCopy( tr.endpos, to );
		if ( tr.fraction < 1.0f ) {
			VectorMA( tr.endpos, 16, tr.plane.normal, to );
		}
		VectorCopy( to, ground );
		ground[2] -= 600;
		trap_Trace( &tr, to, NULL, NULL, ground, ENTITYNUM_NONE, MASK_SOLID );
		if ( tr.startsolid || tr.fraction == 1.0f || tr.plane.normal[2] < 0.6f ) {
			continue;
		}
		VectorCopy( tr.endpos, ground );
		ground[2] += 2;
		if ( G_SkyAbove( ground, sky ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

static gentity_t *G_RearmRandomVictim( qboolean needSky ) {
	gentity_t	*list[MAX_CLIENTS], *ent;
	vec3_t		sky;
	int			i, n = 0;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		ent = &g_entities[i];
		if ( !G_RearmAlive( ent ) || ent->client->ps.pm_type != PM_NORMAL ) {
			continue;
		}
		if ( needSky && !G_SkyAbove( ent->r.currentOrigin, sky ) ) {
			continue;
		}
		list[n++] = ent;
	}
	return n ? list[rand() % n] : NULL;
}

/*
===============
Meteor shower
===============
*/
static qboolean G_RearmMeteor( gentity_t *target, float minR, float maxR, float yaw ) {
	gentity_t	*bolt;
	vec3_t		ground, sky, start, dir, side;
	trace_t		tr;
	float		dist, speed;
	int			msec;

	if ( !target ) {
		return qfalse;
	}
	if ( !G_FindStrikeSpot( target->r.currentOrigin, minR, maxR, yaw, ground, sky ) ) {
		return qfalse;
	}
	// come in at an angle when the sky allows it
	VectorCopy( sky, start );
	side[0] = crandom();
	side[1] = crandom();
	side[2] = 0;
	VectorNormalize( side );
	VectorMA( sky, ( sky[2] - ground[2] ) * 0.45f, side, start );
	trap_Trace( &tr, ground, NULL, NULL, start, ENTITYNUM_NONE, MASK_SOLID );
	if ( tr.fraction < 1.0f && ( tr.surfaceFlags & SURF_SKY ) ) {
		VectorSubtract( ground, tr.endpos, dir );
		VectorNormalize( dir );
		VectorMA( tr.endpos, 12, dir, start );
	} else if ( tr.fraction < 1.0f ) {
		VectorCopy( sky, start );
	}
	VectorSubtract( ground, start, dir );
	dist = VectorNormalize( dir );
	speed = dist / ( METEOR_WARN_TIME * 0.001f );
	if ( speed < 600 ) {
		speed = 600;
	} else if ( speed > 2200 ) {
		speed = 2200;
	}
	msec = (int)( dist / speed * 1000 );

	G_StrikeMarker( ground, RM_STRIKE_METEOR, 200, msec );
	if ( trap_Cvar_VariableIntegerValue( "g_debugAlt" ) ) {
		G_LogPrintf( "Meteor: %i %i %i in %i ms\n", (int)ground[0], (int)ground[1], (int)ground[2], msec );
	}

	bolt = G_Spawn();
	bolt->classname = "meteor";
	bolt->nextthink = level.time + msec + 3000;
	bolt->think = G_FreeEntity;
	bolt->s.eType = ET_MISSILE;
	bolt->r.svFlags = SVF_USE_CURRENT_ORIGIN | SVF_BROADCAST;
	bolt->s.weapon = WP_ROCKET_LAUNCHER;
	bolt->s.generic1 = RM_MISSILE_METEOR;
	bolt->r.ownerNum = ENTITYNUM_WORLD;
	bolt->parent = NULL;
	bolt->methodOfDeath = MOD_METEOR;
	bolt->splashMethodOfDeath = MOD_METEOR;
	bolt->clipmask = MASK_SHOT;
	bolt->s.pos.trType = TR_LINEAR;
	bolt->s.pos.trTime = level.time;
	VectorCopy( start, bolt->s.pos.trBase );
	VectorScale( dir, speed, bolt->s.pos.trDelta );
	SnapVector( bolt->s.pos.trDelta );
	VectorCopy( start, bolt->r.currentOrigin );
	vectoangles( dir, bolt->s.angles );
	trap_LinkEntity( bolt );
	return qtrue;
}

/*
===============
G_MeteorImpact

Called from G_RearmMissileImpact for meteors
===============
*/
void G_MeteorImpact( gentity_t *ent, trace_t *trace ) {
	gentity_t	*world = &g_entities[ENTITYNUM_WORLD];
	gentity_t	*other = &g_entities[trace->entityNum];
	vec3_t		dir;

	VectorCopy( ent->s.pos.trDelta, dir );
	VectorNormalize( dir );
	if ( other->takedamage && other->client ) {
		G_Damage( other, ent, world, dir, trace->endpos, 50, 0, MOD_METEOR );
	}
	G_AddEvent( ent, EV_MISSILE_MISS, DirToByte( trace->plane.normal ) );
	ent->freeAfterEvent = qtrue;
	ent->s.eType = ET_GENERAL;
	SnapVectorTowards( trace->endpos, ent->s.pos.trBase );
	G_SetOrigin( ent, trace->endpos );
	G_RadiusDamage( trace->endpos, world, 100, 210, ( other->takedamage && other->client ) ? other : NULL, MOD_METEOR );
	if ( G_RearmOn( RS_ELEMENTS ) ) {
		G_EarthTremor( trace->endpos, NULL, 300, 380 );
		G_SpawnFirePatch( trace->endpos, NULL, 2500, 70 );
	}
	trap_LinkEntity( ent );
}

/*
===============
Thunderstorm
===============
*/
static void G_StormStrikeThink( gentity_t *ent ) {
	G_StormStrike( ent->r.currentOrigin );
	G_FreeEntity( ent );
}

static void G_RearmStormStrike( gentity_t *target, float minR, float maxR, float yaw ) {
	gentity_t	*marker;
	vec3_t		ground, sky;

	if ( !target ) {
		return;
	}
	if ( !G_FindStrikeSpot( target->r.currentOrigin, minR, maxR, yaw, ground, sky ) ) {
		return;
	}
	marker = G_StrikeMarker( ground, RM_STRIKE_LIGHTNING, 170, STORM_WARN_TIME );
	if ( trap_Cvar_VariableIntegerValue( "g_debugAlt" ) ) {
		G_LogPrintf( "Strike: %i %i %i\n", (int)ground[0], (int)ground[1], (int)ground[2] );
	}
	marker->think = G_StormStrikeThink;
}

/*
===============
Start / end
===============
*/
static void G_RearmStartMutator( int mut ) {
	gentity_t	*ent;
	gclient_t	*cl;
	int			i;

	level.mutator = mut;
	level.lastMutator = mut;
	level.mutatorEnd = level.time + g_mutatorDuration.integer * 1000;
	rmMutatorFloor = qfalse;
	rmPendingMutator = MUT_NONE;
	rmMutatorStart = level.time;
	rmNextEventTick = level.time + 1500;
	G_RearmAnnounce( "mutator", "%s", bg_rmMutatorNames[mut] );
	trap_SendServerCommand( -1, va( "print \"Event: %s - %s\n\"", bg_rmMutatorNames[mut], bg_rmMutatorHints[mut] ) );
	G_LogPrintf( "Mutator: %i %s\n", mut, bg_rmMutatorNames[mut] );

	if ( mut == MUT_ROCKET_FRENZY ) {
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			ent = &g_entities[i];
			if ( !G_RearmAlive( ent ) ) {
				continue;
			}
			// remember the real loadout, it comes back afterwards
			cl = ent->client;
			cl->frenzyHadLauncher = ( cl->ps.stats[STAT_WEAPONS] & ( 1 << WP_ROCKET_LAUNCHER ) ) != 0;
			cl->frenzyAmmo = cl->ps.ammo[WP_ROCKET_LAUNCHER];
			cl->ps.stats[STAT_WEAPONS] |= 1 << WP_ROCKET_LAUNCHER;
			cl->ps.ammo[WP_ROCKET_LAUNCHER] = INFINITE_AMMO;
			G_RearmSelectWeapon( ent, WP_ROCKET_LAUNCHER );
		}
	}
	if ( mut == MUT_BARREL_BONANZA ) {
		G_PropsBonanza( qtrue );
	}
	if ( mut == MUT_WEAPON_ROULETTE ) {
		G_RearmRouletteSpin();
		rmNextEventTick = level.time + ROULETTE_TIME;
	}
	G_RearmUpdateConfigstring();
}

static void G_RearmEndMutator( void ) {
	gentity_t	*ent;
	gclient_t	*cl;
	int			i;

	if ( level.mutator == MUT_ROCKET_FRENZY ) {
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			ent = &g_entities[i];
			if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED ) {
				continue;
			}
			cl = ent->client;
			if ( cl->frenzyHadLauncher ) {
				cl->ps.ammo[WP_ROCKET_LAUNCHER] = cl->frenzyAmmo;
			} else {
				cl->ps.stats[STAT_WEAPONS] &= ~( 1 << WP_ROCKET_LAUNCHER );
				cl->ps.ammo[WP_ROCKET_LAUNCHER] = 0;
				if ( cl->ps.weapon == WP_ROCKET_LAUNCHER && ent->health > 0 ) {
					G_RearmSelectWeapon( ent, G_RearmBestWeapon( cl ) );
				}
			}
		}
	}
	if ( level.mutator == MUT_JETPACKS ) {
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			if ( g_entities[i].inuse && g_entities[i].client ) {
				g_entities[i].client->ps.powerups[PW_JETPACK] = 0;
			}
		}
	}
	if ( level.mutator == MUT_QUAD_MADNESS ) {
		// the event's unlimited quad runs out now (the juggernaut keeps his)
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			if ( g_entities[i].inuse && g_entities[i].client && i != level.juggernaut
				&& g_entities[i].client->ps.powerups[PW_QUAD] == INT_MAX ) {
				g_entities[i].client->ps.powerups[PW_QUAD] = level.time;
			}
		}
	}
	if ( level.mutator == MUT_BARREL_BONANZA ) {
		G_PropsBonanza( qfalse );
	}
	if ( !level.intermissiontime ) {
		G_RearmAnnounce( "info", "%s is over", bg_rmMutatorNames[level.mutator] );
	}
	level.mutator = MUT_NONE;
	level.mutatorEnd = 0;
	level.nextMutator = level.time + g_mutatorInterval.integer * 1000;
	rmMutatorStart = 0;
	rmMutatorFloor = qfalse;
	G_RearmUpdateConfigstring();
}

/*
===============
G_RearmStartFloorMutator

DESCENT event floors: event mut runs from now until the floor ends (intermission / map change),
no timer. The HUD gets mutend -1 and shows THIS FLOOR instead of a countdown.
===============
*/
void G_RearmStartFloorMutator( int mut ) {
	if ( mut <= MUT_NONE || mut >= MUT_NUM_MUTATORS ) {
		return;
	}
	if ( level.mutator ) {
		G_RearmEndMutator();
	}
	G_RearmStartMutator( mut );
	rmMutatorFloor = qtrue;
	level.mutatorEnd = 0;
	G_LogPrintf( "Mutator: %i floor-long\n", mut );
	G_RearmUpdateConfigstring();
}

qboolean G_RearmMutatorIsFloorLong( void ) {
	return level.mutator && rmMutatorFloor;
}

/*
===============
G_RearmMutatorFrame

The schedule (warning, start, end) and what the running event does over time
===============
*/
static void G_RearmMutatorFrame( void ) {
	int		mut, i;

	if ( level.mutator && !rmMutatorFloor && level.time >= level.mutatorEnd ) {
		G_RearmEndMutator();
		return;
	}
	if ( !level.mutator ) {
		if ( ( !g_mutators.integer || level.warmupTime ) && !rmPendingMutator ) {
			level.nextMutator = level.time + g_mutatorInterval.integer * 1000;
			return;
		}
		if ( !rmPendingMutator && level.time >= level.nextMutator - MUTATOR_WARN_TIME ) {
			mut = G_RearmPickMutator();
			if ( mut == MUT_NONE ) {
				level.nextMutator = level.time + g_mutatorInterval.integer * 1000;
				return;
			}
			rmPendingMutator = mut;
			if ( level.nextMutator < level.time + MUTATOR_WARN_TIME ) {
				level.nextMutator = level.time + MUTATOR_WARN_TIME;
			}
			G_RearmUpdateConfigstring();
		}
		if ( rmPendingMutator && level.time >= level.nextMutator ) {
			G_RearmStartMutator( rmPendingMutator );
		}
		return;
	}

	// running event
	if ( level.time < rmNextEventTick ) {
		return;
	}
	switch ( level.mutator ) {
	case MUT_METEOR_SHOWER:
		// the victim may be indoors: try a few
		for ( i = 0 ; i < 4 && !G_RearmMeteor( G_RearmRandomVictim( qfalse ), 40, 480, -1 ) ; i++ ) {
		}
		rmNextEventTick = level.time + 1000 + rand() % 900;
		break;
	case MUT_THUNDERSTORM:
		G_RearmStormStrike( G_RearmRandomVictim( qtrue ), 0, 140, -1 );
		rmNextEventTick = level.time + 1800 + rand() % 1700;
		break;
	case MUT_WEAPON_ROULETTE:
		G_RearmRouletteSpin();
		rmNextEventTick = level.time + ROULETTE_TIME;
		break;
	default:
		rmNextEventTick = level.time + 100000;
		break;
	}
}

/*
===============
G_RearmConsoleCommand

Dev server commands:
  mutator <n> [now|floor] start random event n (0 ends the running one), with the warning unless "now";
                         "floor": runs until the map ends, no timer (DESCENT event floors)
  perkoffer <client>     offer perks right away
  perkgive <client> <n>  give perk n
  slowmo <scale> <msec>  slow motion
===============
*/
qboolean G_RearmConsoleCommand( const char *cmd ) {
	char	arg[MAX_TOKEN_CHARS];
	int		n, c;

	if ( !Q_stricmp( cmd, "mutator" ) ) {
		if ( trap_Argc() < 2 ) {
			for ( n = 1 ; n < MUT_NUM_MUTATORS ; n++ ) {
				G_Printf( "%2i %-16s %s\n", n, bg_rmMutatorNames[n], G_RearmMutatorAllowed( n ) ? "" : "(not allowed now)" );
			}
			return qtrue;
		}
		trap_Argv( 1, arg, sizeof( arg ) );
		n = atoi( arg );
		if ( level.mutator ) {
			G_RearmEndMutator();
		}
		rmPendingMutator = MUT_NONE;
		if ( n <= MUT_NONE || n >= MUT_NUM_MUTATORS ) {
			G_RearmUpdateConfigstring();
			return qtrue;
		}
		trap_Argv( 2, arg, sizeof( arg ) );
		if ( !Q_stricmp( arg, "floor" ) ) {
			G_RearmStartFloorMutator( n );		// until the map ends, no timer
		} else if ( !Q_stricmp( arg, "now" ) ) {
			G_RearmStartMutator( n );
		} else {
			rmPendingMutator = n;
			level.nextMutator = level.time + MUTATOR_WARN_TIME;
			G_RearmUpdateConfigstring();
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "perkoffer" ) || !Q_stricmp( cmd, "perkgive" ) ) {
		trap_Argv( 1, arg, sizeof( arg ) );
		c = atoi( arg );
		if ( c < 0 ) {		// -1: the first human
			for ( c = 0 ; c < level.maxclients ; c++ ) {
				if ( g_entities[c].inuse && g_entities[c].client && !( g_entities[c].r.svFlags & SVF_BOT ) ) {
					break;
				}
			}
		}
		if ( c < 0 || c >= level.maxclients || !g_entities[c].inuse || !g_entities[c].client ) {
			G_Printf( "bad client\n" );
			return qtrue;
		}
		if ( !Q_stricmp( cmd, "perkoffer" ) ) {
			g_entities[c].client->pers.perkOffer[0] = -1;
			G_PerkForceOffer( &g_entities[c] );
		} else {
			trap_Argv( 2, arg, sizeof( arg ) );
			G_PerkGive( &g_entities[c], atoi( arg ) );
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "meteor" ) ) {		// a meteor and a lightning strike in front of the first human
		for ( c = 0 ; c < level.maxclients ; c++ ) {
			if ( g_entities[c].inuse && g_entities[c].client && !( g_entities[c].r.svFlags & SVF_BOT ) ) {
				G_RearmMeteor( &g_entities[c], 160, 220, g_entities[c].client->ps.viewangles[YAW] );
				G_RearmStormStrike( &g_entities[c], 380, 420, g_entities[c].client->ps.viewangles[YAW] + 20 );
				break;
			}
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "slowmo" ) ) {
		trap_Argv( 2, arg, sizeof( arg ) );
		n = trap_Argc() > 2 ? atoi( arg ) : 1500;
		trap_Argv( 1, arg, sizeof( arg ) );
		G_SlowMoEx( atof( arg ) > 0 ? atof( arg ) : 0.3f, n > 0 ? n : 1500, qtrue );
		return qtrue;
	}
	return qfalse;
}

/*
===============
G_SlowMo

Slow the whole game down to scale for msec of game time, easing back at the end.
Only in a local game with one human: the engine slows the shared server/client clock.
===============
*/
static qboolean G_SlowMoAllowed( void ) {
	int		i, humans;

	if ( !G_RearmOn( RS_SLOWMO ) || level.intermissiontime || trap_Cvar_VariableIntegerValue( "dedicated" ) ) {
		return qfalse;
	}
	for ( i = humans = 0 ; i < level.maxclients ; i++ ) {
		if ( level.clients[i].pers.connected != CON_DISCONNECTED && !( g_entities[i].r.svFlags & SVF_BOT ) ) {
			humans++;
		}
	}
	return humans == 1;
}

void G_SlowMoEx( float scale, int msec, qboolean force ) {
	gentity_t	*te;

	if ( trap_Cvar_VariableIntegerValue( "g_debugAlt" ) ) {
		G_LogPrintf( "SlowMo: %.2f %i ms%s%s\n", scale, msec, force ? " forced" : "",
			!G_SlowMoAllowed() ? " (not allowed)" : level.time < level.slowmoEnd ? " (running)"
			: !force && level.slowmoEnd && level.time - level.slowmoEnd < SLOWMO_GAP ? " (too soon)" : "" );
	}
	if ( !G_SlowMoAllowed() ) {
		return;
	}
	if ( scale < 0.2f ) {
		scale = 0.2f;
	} else if ( scale > 0.95f ) {
		return;
	}
	// msec is game time: keep the real length short (the engine also gives up on a
	// target held longer than 5 s)
	if ( msec > 3000 * scale ) {
		msec = 3000 * scale;
	}
	// one at a time: a deeper one deepens the running one, but only the match winner may
	// stretch it, so they can't stack up
	if ( level.time < level.slowmoEnd ) {
		if ( scale < level.slowmoScale ) {
			level.slowmoScale = scale;
		}
		if ( force && level.time + msec > level.slowmoEnd ) {
			level.slowmoEnd = level.time + msec;
		}
		return;
	}
	if ( !force && level.slowmoEnd && level.time - level.slowmoEnd < SLOWMO_GAP ) {
		return;
	}
	level.slowmoStart = level.time;
	level.slowmoEnd = level.time + msec;
	level.slowmoScale = scale;
	te = G_TempEntity( vec3_origin, EV_SLOWMO );
	te->s.eventParm = (int)( ( 1.0f - scale ) * 255 );
	te->r.svFlags |= SVF_BROADCAST;
}

void G_SlowMo( float scale, int msec ) {
	G_SlowMoEx( scale, msec, qfalse );
}

/*
The game only sets the target scale (rm_slowmo): the engine eases the clock into it and
back out to 1 every frame (Com_ModifyMsec), which is smooth even though this runs at sv_fps.
*/
static void G_SlowMoFrame( void ) {
	float	s = 1.0f;

	if ( level.time < level.slowmoEnd && G_SlowMoAllowed() ) {
		s = level.slowmoScale;
	}
	if ( s != level.slowmoCurrent ) {
		level.slowmoCurrent = s;
		trap_Cvar_Set( "rm_slowmo", va( "%.3f", s ) );
	}
}

void G_SlowMoReset( void ) {
	level.slowmoEnd = 0;
	level.slowmoCurrent = 1.0f;
	trap_Cvar_Set( "rm_slowmo", "1" );
}

/*
===============
G_RearmRunFrame
===============
*/
void G_RearmRunFrame( void ) {
	gentity_t	*ent;
	int			i, oldBits;

	// feature switches can change at any time
	for ( i = 0 ; i < RS_NUM ; i++ ) {
		trap_Cvar_Update( &rmSwitches[i].cv );
	}
	oldBits = rmSwitchBits;
	G_RearmReadSwitches();
	if ( rmSwitchBits != oldBits || G_FragLimit() != rmFragTarget ) {	// (fraglimit / g_fragMult changed)
		G_RearmUpdateConfigstring();
	}
	G_SlowMoFrame();

	if ( level.intermissiontime ) {
		if ( level.mutator ) {
			G_RearmEndMutator();
		}
		if ( rmPendingMutator ) {
			rmPendingMutator = MUT_NONE;
			G_RearmUpdateConfigstring();
		}
		return;
	}

	// random events: warning, schedule, meteors / lightning / roulette
	G_RearmMutatorFrame();
	G_PerksThornsFrame();

	// the juggernaut must still be playing
	if ( level.juggernaut >= 0 ) {
		ent = &g_entities[level.juggernaut];
		if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED
			|| ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
			level.juggernaut = -1;
			G_RearmUpdateConfigstring();
		}
	}

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		ent = &g_entities[i];
		if ( !G_RearmAlive( ent ) ) {
			continue;
		}
		// modes with fixed weapons never run dry
		if ( level.rmMode == RM_MODE_GUNGAME ) {
			int w = bg_gunGameLadder[ent->client->pers.ggLevel];
			if ( w != WP_GAUNTLET ) {
				ent->client->ps.ammo[w] = INFINITE_AMMO;
			}
		} else if ( level.rmMode == RM_MODE_INSTAGIB ) {
			ent->client->ps.ammo[WP_RAILGUN] = INFINITE_AMMO;
		}
		if ( level.mutator == MUT_ROCKET_FRENZY ) {
			ent->client->ps.ammo[WP_ROCKET_LAUNCHER] = INFINITE_AMMO;
		}
		// juggernaut and quad madness keep quad running. INT_MAX = no time limit, like the CTF
		// flags: a timer refreshed to "1 s left" every frame played the wear-off sound 10-20
		// times a second for as long as the event ran (a whole DESCENT event floor)
		if ( i == level.juggernaut || level.mutator == MUT_QUAD_MADNESS ) {
			ent->client->ps.powerups[PW_QUAD] = INT_MAX;
		}
		G_ElementsClientFrame( ent );
		G_StyleFrame( ent );
		G_PerksFrame( ent );
		if ( level.mutator == MUT_JETPACKS ) {
			ent->client->ps.powerups[PW_JETPACK] = INT_MAX;
		}
	}
}
