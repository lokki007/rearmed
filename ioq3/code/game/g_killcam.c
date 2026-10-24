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
// g_killcam.c -- rearmed 0.5: tells the human player what made a kill notable (rmkc) and who
// killed them with how much health left (rmkiller). cgame (cg_killcam.c) records the scene and
// plays the side kill cam, the death cam and the end of floor / match replay from it.

#include "g_local.h"
#include "g_descent.h"
#include "bg_killcam.h"

#define KC_QUICK_TIME	1200		// kills this close together are a multi kill

static int	gkcLastKill[MAX_CLIENTS];
static int	gkcQuick[MAX_CLIENTS];

/*
===============
G_KC_MatchPoint

Did this kill just win the match? (the scores are already updated; same rules as g_style.c)
===============
*/
static qboolean G_KC_MatchPoint( gentity_t *attacker ) {
	gclient_t	*cl = attacker->client;

	if ( level.rmMode == RM_MODE_GUNGAME ) {
		return cl->pers.ggLevel >= GG_LEVELS;
	}
	if ( G_RL_Active() || g_fraglimit.integer <= 0 || G_RearmIgnoresFraglimit() || g_gametype.integer >= GT_CTF ) {
		return qfalse;		// descent floors: cgame marks the frag that ends the floor itself
	}
	if ( g_gametype.integer == GT_TEAM ) {
		return level.teamScores[cl->sess.sessionTeam] >= g_fraglimit.integer;
	}
	return cl->ps.persistant[PERS_SCORE] >= g_fraglimit.integer;
}

static int G_KC_WeaponForMod( int mod, gentity_t *attacker ) {
	switch ( mod ) {
	case MOD_SHOTGUN:			return WP_SHOTGUN;
	case MOD_GAUNTLET:			return WP_GAUNTLET;
	case MOD_MACHINEGUN:		return WP_MACHINEGUN;
	case MOD_GRENADE:
	case MOD_GRENADE_SPLASH:	return WP_GRENADE_LAUNCHER;
	case MOD_ROCKET:
	case MOD_ROCKET_SPLASH:		return WP_ROCKET_LAUNCHER;
	case MOD_PLASMA:
	case MOD_PLASMA_SPLASH:		return WP_PLASMAGUN;
	case MOD_RAILGUN:			return WP_RAILGUN;
	case MOD_LIGHTNING:			return WP_LIGHTNING;
	case MOD_BFG:
	case MOD_BFG_SPLASH:		return WP_BFG;
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case MOD_NAIL:				return WP_NAILGUN;
	case MOD_CHAINGUN:			return WP_CHAINGUN;
	case MOD_PROXIMITY_MINE:	return WP_PROX_LAUNCHER;
#endif
	case MOD_GRAPPLE:			return WP_GRAPPLING_HOOK;
	default:
		break;
	}
	if ( attacker && attacker->client ) {
		return attacker->client->ps.weapon;
	}
	return WP_NONE;
}

static qboolean G_KC_Environment( int mod ) {
	switch ( mod ) {
	case MOD_WATER:
	case MOD_SLIME:
	case MOD_LAVA:
	case MOD_CRUSH:
	case MOD_FALLING:
	case MOD_TRIGGER_HURT:
	case MOD_TARGET_LASER:
	case MOD_WALLSPLAT:
		return qtrue;
	default:
		return qfalse;
	}
}

/*
===============
G_KillCamOnDeath

From player_die, after gore and gib decisions. gibbed: the body is about to burst.
===============
*/
void G_KillCamOnDeath( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int mod, qboolean gibbed ) {
	gclient_t	*cl;
	int			n, flags, role;

	if ( !self || !self->client || level.warmupTime ) {
		return;
	}

	// the victim (a human): who did it and what they have left
	if ( !( self->r.svFlags & SVF_BOT ) && attacker && attacker->client && attacker != self ) {
		trap_SendServerCommand( self - g_entities, va( "rmkiller %i %i %i %i", (int)( attacker - g_entities ),
			attacker->health > 0 ? attacker->health : 0, attacker->client->ps.stats[STAT_ARMOR],
			G_KC_WeaponForMod( mod, attacker ) ) );
	}

	if ( !attacker || !attacker->client || attacker == self || OnSameTeam( self, attacker ) ) {
		return;
	}
	cl = attacker->client;
	n = attacker - g_entities;

	if ( gkcLastKill[n] && level.time >= gkcLastKill[n] && level.time - gkcLastKill[n] < KC_QUICK_TIME ) {
		gkcQuick[n]++;
	} else {
		gkcQuick[n] = 1;
	}
	gkcLastKill[n] = level.time;

	if ( attacker->r.svFlags & SVF_BOT ) {
		return;
	}

	flags = 0;
	if ( gibbed ) {
		flags |= KCF_GIB;
	}
	if ( self->s.generic1 & RM_BODY_OVERKILL ) {
		flags |= KCF_OVERKILL | KCF_GIB;
	}
	if ( ( self->s.generic1 & RM_BODY_HEADLESS )
		|| ( cl->hitTarget == self->s.number && ( cl->hitFlags & HIT_HEADSHOT ) && !G_KC_Environment( mod ) ) ) {
		flags |= KCF_HEADSHOT;
	}
	if ( cl->hitTarget == self->s.number && ( cl->hitFlags & HIT_AIRSHOT ) ) {
		flags |= KCF_AIRSHOT;
	}
	if ( self->client->ps.groundEntityNum == ENTITYNUM_NONE && cl->ps.groundEntityNum == ENTITYNUM_NONE
		&& !G_KC_Environment( mod ) ) {
		flags |= KCF_MIDAIR;
	}
	if ( G_KC_Environment( mod ) ) {
		flags |= KCF_ENV;
	}
	if ( mod == MOD_GAUNTLET || mod == MOD_SLAM ) {
		flags |= KCF_MELEE;
	}
	if ( inflictor && inflictor->s.eType == ET_MISSILE && ( inflictor->s.generic1 & ALTFIRE_CHARGED ) ) {
		flags |= KCF_CHARGED;
	}
	role = G_RL_ClientRole( self );
	if ( role == 4 ) {
		flags |= KCF_BOSS;
	} else if ( role == 3 ) {
		flags |= KCF_ELITE;
	}
	if ( G_KC_MatchPoint( attacker ) ) {
		flags |= KCF_FINAL;
	}

	trap_SendServerCommand( n, va( "rmkc %i %i %i %i %i", self->s.number, flags, G_KC_WeaponForMod( mod, attacker ),
		gkcQuick[n], mod ) );
}

/*
===============
G_KillCamConsoleCommand

Dev (tests): kchurt <victim> <attacker> <damage> [mod] -- damage as if the attacker hit them
===============
*/
qboolean G_KillCamConsoleCommand( const char *cmd ) {
	char		buf[16];
	gentity_t	*targ, *att;
	int			v, a, dmg, mod;

	if ( Q_stricmp( cmd, "kchurt" ) ) {
		return qfalse;
	}
	trap_Argv( 1, buf, sizeof( buf ) );
	v = atoi( buf );
	trap_Argv( 2, buf, sizeof( buf ) );
	a = atoi( buf );
	trap_Argv( 3, buf, sizeof( buf ) );
	dmg = atoi( buf );
	trap_Argv( 4, buf, sizeof( buf ) );
	mod = buf[0] ? atoi( buf ) : MOD_RAILGUN;
	if ( v < 0 || v >= level.maxclients || a < 0 || a >= level.maxclients ) {
		return qtrue;
	}
	targ = &g_entities[v];
	att = &g_entities[a];
	if ( !targ->inuse || !targ->client || !att->inuse || !att->client || targ->health <= 0 ) {
		G_Printf( "kchurt: bad clients\n" );
		return qtrue;
	}
	G_Damage( targ, att, att, NULL, NULL, dmg, DAMAGE_NO_PROTECTION, mod );
	G_Printf( "kchurt: %i hit %i for %i, health now %i (attacker health %i)\n", a, v, dmg, targ->health, att->health );
	return qtrue;
}
