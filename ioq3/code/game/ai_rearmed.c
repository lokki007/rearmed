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
// ai_rearmed.c -- teaches the bots the rearmed moves: dash, double jump, jetpack,
// offhand grapple and alt-fire. Works on top of the usercmd the stock bot AI produced.

#include "g_local.h"
#include "../botlib/botlib.h"
#include "../botlib/be_aas.h"
#include "../botlib/be_ea.h"
#include "../botlib/be_ai_char.h"
#include "../botlib/be_ai_chat.h"
#include "../botlib/be_ai_gen.h"
#include "../botlib/be_ai_goal.h"
#include "../botlib/be_ai_move.h"
#include "../botlib/be_ai_weap.h"
#include "ai_main.h"
#include "ai_dmq3.h"
#include "chars.h"

#define BOT_DETONATE_RANGE	110		// remote detonate when our rocket / mine is this close to the enemy
#define BOT_HOOK_MIN_DIST	700
#define BOT_HOOK_TIME		1500

#define PROP_SHOT_TIME		1000	// give up on a prop shot after this long
#define PROP_VIEW_DOT		0.8f	// only props within about 37 degrees of where we look
#define PROP_SAFE_MARGIN	64		// stay this much outside the blast ourselves
#define PROP_ENEMY_FRAC		0.8f	// the enemy must be inside this fraction of the blast radius

/*
===============================================================================

v4 switches: bot_rearmTricks, bot_useProps, bot_avoidDanger, bot_debugTricks

===============================================================================
*/
static vmCvar_t	bot_rearmTricks, bot_useProps, bot_avoidDanger, bot_debugTricks;
static int		botTrickCvarTime = -1;

static void BotTrickCvars( void ) {
	if ( botTrickCvarTime == -1 ) {
		trap_Cvar_Register( &bot_rearmTricks, "bot_rearmTricks", "1", CVAR_ARCHIVE );
		trap_Cvar_Register( &bot_useProps, "bot_useProps", "1", CVAR_ARCHIVE );
		trap_Cvar_Register( &bot_avoidDanger, "bot_avoidDanger", "1", CVAR_ARCHIVE );
		trap_Cvar_Register( &bot_debugTricks, "bot_debugTricks", "0", 0 );
		botTrickCvarTime = level.time;
	} else if ( botTrickCvarTime != level.time ) {
		trap_Cvar_Update( &bot_rearmTricks );
		trap_Cvar_Update( &bot_useProps );
		trap_Cvar_Update( &bot_avoidDanger );
		trap_Cvar_Update( &bot_debugTricks );
		botTrickCvarTime = level.time;
	}
}

qboolean BotTrickOn( int which ) {
	BotTrickCvars();
	switch ( which ) {
	case BOT_TRICK_MOVES:
		return bot_rearmTricks.integer != 0;
	case BOT_TRICK_PROPS:
		return bot_useProps.integer != 0 && G_RearmOn( RS_PROPS );
	case BOT_TRICK_DANGER:
		return bot_avoidDanger.integer != 0;
	default:
		return qfalse;
	}
}

int BotTrickDebug( void ) {
	BotTrickCvars();
	return bot_debugTricks.integer;
}

/*
===============================================================================

Explosive props (barrels, gas tanks)

===============================================================================
*/
qboolean BotRearmExplosiveProp( gentity_t *ent ) {
	return ent->inuse && ent->s.eType == ET_GENERAL && ent->s.generic1 == RM_ENT_PROP
		&& ( ent->s.modelindex2 == PROP_BARREL || ent->s.modelindex2 == PROP_TANK );
}

float BotRearmPropRadius( gentity_t *prop ) {
	return prop->s.modelindex2 == PROP_TANK ? PROP_TANK_BLAST_RADIUS : PROP_BLAST_RADIUS;
}

qboolean BotRearmPropBurning( gentity_t *prop ) {
	return ( prop->s.frame & PROPF_DANGER ) != 0;
}

/*
==================
BotRearmPropNearEnemy

The explosive prop closest to the enemy with the enemy well inside its blast and us well
outside it (NULL if none). propDist gets our distance to it.
==================
*/
gentity_t *BotRearmPropNearEnemy( bot_state_t *bs, gentity_t *enemy, float *propDist ) {
	gentity_t	*ent, *best = NULL;
	float		d, bestD = 99999, r, mine;
	int			i;

	for ( i = MAX_CLIENTS, ent = &g_entities[MAX_CLIENTS] ; i < level.num_entities ; i++, ent++ ) {
		if ( !BotRearmExplosiveProp( ent ) || ( ent->s.frame & PROPF_RUPTURED ) ) {
			continue;		// a flying tank is no aim target
		}
		r = BotRearmPropRadius( ent );
		d = Distance( ent->r.currentOrigin, enemy->r.currentOrigin );
		if ( d > r * PROP_ENEMY_FRAC || d >= bestD ) {
			continue;
		}
		mine = Distance( ent->r.currentOrigin, bs->cur_ps.origin );
		if ( mine < r + PROP_SAFE_MARGIN ) {
			continue;
		}
		best = ent;
		bestD = d;
		if ( propDist ) {
			*propDist = mine;
		}
	}
	return best;
}

/*
==================
BotRearmPropWeapon

Can this weapon set a prop off from here? Hitscan, or a projectile that gets there before the
enemy has walked off (barrels have 40 health: a rail, rocket, close shotgun blast, two plasma
bolts or a short burst of bullets / nails / lightning)
==================
*/
static qboolean BotRearmPropWeapon( int weapon, float dist ) {
	switch ( weapon ) {
	case WP_RAILGUN:
		return qtrue;
	case WP_MACHINEGUN:
		return dist < 900;		// a burst (held until it blows)
	case WP_SHOTGUN:
		return dist < 600;
	case WP_LIGHTNING:
		return dist < 750;
	case WP_ROCKET_LAUNCHER:
		return dist < 900;
	case WP_PLASMAGUN:
		return dist < 800;
	case WP_BFG:
		return dist < 1000;
	case WP_GRENADE_LAUNCHER:
		return dist < 450;		// lobbed: only when it's close to a straight shot
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case WP_CHAINGUN:
		return dist < 800;
	case WP_NAILGUN:
		return dist < 800;
#endif
	default:
		return qfalse;
	}
}

/*
==================
BotRearmPropAuto

Automatic weapons hold the trigger on the prop until it blows
==================
*/
static qboolean BotRearmPropAuto( int weapon ) {
	return weapon == WP_MACHINEGUN || weapon == WP_LIGHTNING || weapon == WP_PLASMAGUN
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
		|| weapon == WP_CHAINGUN || weapon == WP_NAILGUN
#endif
		;
}

/*
==================
BotRearmPropSlack

How far off the prop's centre the line of fire may pass and still set it off: the prop's own
half width, the spread of the gun, and the splash of explosives landing next to it
==================
*/
static float BotRearmPropSlack( int weapon ) {
	switch ( weapon ) {
	case WP_SHOTGUN:
		return 40;
	case WP_ROCKET_LAUNCHER:
	case WP_BFG:
		return 44;		// splash: a rocket into the floor beside it does it
	case WP_GRENADE_LAUNCHER:
		return 36;
	case WP_MACHINEGUN:
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case WP_CHAINGUN:
#endif
		return 22;
	default:
		return 16;
	}
}

/*
==================
BotRearmPropPoint

The middle of the prop (the origin is at the bottom centre; lying barrels are lower)
==================
*/
static void BotRearmPropPoint( gentity_t *prop, vec3_t point ) {
	VectorCopy( prop->r.currentOrigin, point );
	point[2] += ( prop->r.mins[2] + prop->r.maxs[2] ) * 0.5f;
}

/*
==================
BotRearmPropAim

Called at the top of BotAimAtEnemy: while a prop shot is on, look at the prop instead of the
enemy. The view still turns at the bot's normal speed (BotChangeViewAngles): no snap aim.
==================
*/
qboolean BotRearmPropAim( bot_state_t *bs ) {
	gentity_t	*prop;
	vec3_t		point, dir;

	if ( !bs->rmPropEnt ) {
		return qfalse;
	}
	prop = &g_entities[bs->rmPropEnt];
	if ( !BotRearmExplosiveProp( prop ) || level.time > bs->rmPropUntil || bs->enemy != bs->rmPropEnemy ) {
		return qfalse;		// BotRearmPropShot ends it (and logs why)
	}
	BotRearmPropPoint( prop, point );
	VectorSubtract( point, bs->eye, dir );
	vectoangles( dir, bs->ideal_viewangles );
	bs->ideal_viewangles[PITCH] = AngleMod( bs->ideal_viewangles[PITCH] );
	VectorCopy( point, bs->aimtarget );
	return qtrue;
}

/*
==================
BotRearmPropLog
==================
*/
static void BotRearmPropLog( bot_state_t *bs, const char *what, float dist ) {
	if ( BotTrickDebug() || trap_Cvar_VariableIntegerValue( "g_debugAlt" ) ) {
		G_LogPrintf( "PropShot: %i %s prop %i enemy %i weapon %i dist %.0f\n", bs->client, what, bs->rmPropEnt,
			bs->rmPropEnemy, g_entities[bs->client].client->ps.weapon, dist );
	}
}

/*
==================
BotRearmPropEnd
==================
*/
static void BotRearmPropEnd( bot_state_t *bs, const char *why ) {
	gentity_t	*prop = &g_entities[bs->rmPropEnt];

	BotRearmPropLog( bs, why, BotRearmExplosiveProp( prop ) ? Distance( prop->r.currentOrigin, bs->origin ) : 0 );
	bs->rmPropEnt = 0;
	bs->rmPropFireUntil = 0;
	bs->rmPropNext = level.time + 1500 + random() * 1500;
}

/*
==================
BotRearmPropShot

Per frame: start a prop shot when an enemy stands next to an explosive prop we can see
(skill scaled, not every time), turn onto it, and pull the trigger once the line of fire would
set it off (automatic weapons hold it until it blows). Returns qtrue while it owns the fire buttons.
==================
*/
static qboolean BotRearmPropShot( bot_state_t *bs, usercmd_t *ucmd, gentity_t *enemy, qboolean visible, float skill ) {
	playerState_t	*ps = &g_entities[bs->client].client->ps;
	gentity_t		*prop;
	trace_t			tr;
	vec3_t			eye, point, angles, fwd, end, to;
	float			propDist = 0, chance, along, off, slack;
	int				j;
	qboolean		onTarget;

	if ( !BotTrickOn( BOT_TRICK_PROPS ) || !enemy ) {
		if ( bs->rmPropEnt ) {
			BotRearmPropEnd( bs, "drops (no enemy)" );
		}
		return qfalse;
	}
	VectorCopy( ps->origin, eye );
	eye[2] += ps->viewheight;

	if ( bs->rmPropEnt ) {
		prop = &g_entities[bs->rmPropEnt];
		if ( !BotRearmExplosiveProp( prop ) || ( prop->s.frame & ( PROPF_FUSE | PROPF_RUPTURED ) )
			|| ( ( prop->s.frame & PROPF_BURNING ) && !( bs->rmPropFireUntil && BotRearmPropAuto( ps->weapon ) ) ) ) {
			// blown, venting, or lit (a burst keeps going until a lit barrel blows)
			BotRearmPropEnd( bs, bs->rmPropFireUntil ? "set off" : "lost (someone else set it off)" );
			return qfalse;
		}
		propDist = Distance( prop->r.currentOrigin, ps->origin );
		if ( level.time > bs->rmPropUntil ) {
			BotRearmPropEnd( bs, bs->rmPropFireUntil ? "missed" : "gives up (never on target)" );
			return qfalse;
		}
		if ( bs->enemy != bs->rmPropEnemy || propDist < BotRearmPropRadius( prop ) + 16
			|| ( !bs->rmPropFireUntil && Distance( prop->r.currentOrigin, enemy->r.currentOrigin ) > BotRearmPropRadius( prop ) ) ) {
			BotRearmPropEnd( bs, "drops (enemy left / too close)" );
			return qfalse;
		}
		if ( ps->weapon != bs->rmAltWeapon ) {
			// switched (often to a launcher for the barrel): carry on if this gun does it too
			if ( !BotRearmPropWeapon( ps->weapon, propDist ) || ps->ammo[ps->weapon] == 0 ) {
				BotRearmPropEnd( bs, "drops (weapon)" );
				return qfalse;
			}
			bs->rmAltWeapon = ps->weapon;
		}
		// keep looking at it whatever the AI node does with the view
		BotRearmPropPoint( prop, point );
		VectorSubtract( point, bs->eye, to );
		vectoangles( to, bs->ideal_viewangles );
		ucmd->buttons &= ~( BUTTON_ATTACK | BUTTON_ALTATTACK );

		// does the line of fire of this command pass close enough?
		for ( j = 0 ; j < 3 ; j++ ) {
			angles[j] = SHORT2ANGLE( ( ucmd->angles[j] + ps->delta_angles[j] ) & 65535 );
		}
		AngleVectors( angles, fwd, NULL, NULL );
		VectorSubtract( point, eye, to );
		along = DotProduct( to, fwd );
		onTarget = qfalse;
		if ( along > 0 ) {
			VectorMA( to, -along, fwd, end );
			off = VectorLength( end );
			slack = BotRearmPropSlack( ps->weapon ) * ( bs->rmPropFireUntil ? 1.5f : 1.0f );
			if ( off < slack ) {
				// and nothing in the way: a hit, or for spread and splash guns, nothing short of it
				VectorMA( eye, along + 32, fwd, end );
				trap_Trace( &tr, eye, NULL, NULL, end, bs->client, MASK_SHOT );
				onTarget = tr.entityNum == bs->rmPropEnt
					|| ( BotRearmPropSlack( ps->weapon ) > 20 && tr.fraction * ( along + 32 ) > along - 28 );
			}
		}
		if ( bs->rmPropFireUntil ) {
			// firing: automatic guns keep at it while on target
			if ( level.time < bs->rmPropFireUntil || ( BotRearmPropAuto( ps->weapon ) && onTarget ) ) {
				ucmd->buttons |= BUTTON_ATTACK;
				return qtrue;
			}
			if ( !BotRearmPropAuto( ps->weapon ) && ps->weaponTime <= 0 && level.time > bs->rmPropFireUntil + 300 ) {
				BotRearmPropEnd( bs, "missed" );
				return qfalse;
			}
			return qtrue;
		}
		if ( onTarget && ps->weaponTime <= 0 ) {
			ucmd->buttons |= BUTTON_ATTACK;
			bs->rmPropFireUntil = level.time + ( BotRearmPropAuto( ps->weapon ) ? 250 : 150 );
			bs->rmPropUntil = level.time + 1200;		// time for the burst / the rocket to land
			bs->rmAltHoldUntil = 0;
			BotRearmPropLog( bs, "fires at", propDist );
		}
		return qtrue;
	}

	if ( level.time < bs->rmPropNext || bs->rmAltHoldUntil || bs->enemy < 0 ) {
		return qfalse;
	}
	bs->rmPropNext = level.time + 300 + (int)( 400 * ( 1 - skill ) );		// slower bots notice later
	// the enemy must be in sight or just ducked out of it
	if ( !visible && level.time - bs->rmEnemySeen > 1000 ) {
		return qfalse;
	}
	// one-shot modes: shooting them beats shooting the barrel
	if ( visible && ( level.rmMode == RM_MODE_INSTAGIB || level.rmMode == RM_MODE_OITC ) ) {
		return qfalse;
	}
	prop = BotRearmPropNearEnemy( bs, enemy, &propDist );
	if ( !prop || ( prop->s.frame & PROPF_DANGER ) || !BotRearmPropWeapon( ps->weapon, propDist ) || ps->ammo[ps->weapon] == 0 ) {
		return qfalse;
	}
	// where they'll be when it goes off (better bots lead them): still well inside the blast?
	VectorMA( enemy->r.currentOrigin, 0.35f * skill, enemy->client->ps.velocity, to );
	off = Distance( prop->r.currentOrigin, to );
	if ( off > BotRearmPropRadius( prop ) * 0.7f ) {
		return qfalse;
	}
	chance = ( 0.3f + 0.45f * skill ) * BotPersona( bs )->props;
	if ( !visible ) {
		chance *= 1.5f;		// can't shoot them, but we can shoot what they're hiding next to
	}
	if ( !BotRearmPropAuto( ps->weapon ) && BotRearmPropSlack( ps->weapon ) < 20 ) {
		chance *= 1.3f;		// a rail: the barrel blows the instant it's hit
	} else if ( ps->weapon == WP_ROCKET_LAUNCHER || ps->weapon == WP_GRENADE_LAUNCHER || ps->weapon == WP_PLASMAGUN ) {
		chance *= 0.8f;		// projectiles: they get a moment to step away
	}
	if ( enemy->health < PROP_BLAST_DAMAGE * ( 1 - off / BotRearmPropRadius( prop ) ) + 20 ) {
		chance *= 1.5f;		// the blast would finish them
	}
	if ( random() >= chance ) {
		return qfalse;
	}
	BotRearmPropPoint( prop, point );
	// roughly in view already: no spinning around for a barrel
	for ( j = 0 ; j < 3 ; j++ ) {
		angles[j] = SHORT2ANGLE( ( ucmd->angles[j] + ps->delta_angles[j] ) & 65535 );
	}
	AngleVectors( angles, fwd, NULL, NULL );
	VectorSubtract( point, eye, end );
	VectorNormalize( end );
	if ( DotProduct( end, fwd ) < PROP_VIEW_DOT ) {
		return qfalse;
	}
	trap_Trace( &tr, eye, NULL, NULL, point, bs->client, MASK_SHOT );
	if ( tr.entityNum != prop->s.number && tr.fraction < 1.0f ) {
		return qfalse;
	}
	bs->rmPropEnt = prop->s.number;
	bs->rmPropEnemy = bs->enemy;
	bs->rmPropUntil = level.time + PROP_SHOT_TIME;
	bs->rmPropFireUntil = 0;
	bs->rmAltWeapon = ps->weapon;
	BotRearmPropLog( bs, "aims at", propDist );
	if ( BotTrickDebug() ) {
		G_Printf( "bot_tricks: %i aims at prop %i (type %i) next to enemy %i, dist %.0f\n",
			bs->client, bs->rmPropEnt, prop->s.modelindex2, bs->enemy, propDist );
	}
	return qfalse;
}

/*
==================
BotRearmOwnMissileNear

Is one of our missiles of this weapon close to the enemy?
==================
*/
static qboolean BotRearmOwnMissileNear( bot_state_t *bs, int weapon, vec3_t target ) {
	gentity_t	*m;
	int			i;

	for ( i = MAX_CLIENTS, m = &g_entities[MAX_CLIENTS] ; i < level.num_entities ; i++, m++ ) {
		if ( m->inuse && m->s.eType == ET_MISSILE && m->r.ownerNum == bs->client && m->s.weapon == weapon
			&& Distance( m->r.currentOrigin, target ) < BOT_DETONATE_RANGE ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
==================
BotRearmGroundBelow

Is there floor (not a pit or a hurt trigger's void) within dist below the bot?
==================
*/
static qboolean BotRearmGroundBelow( bot_state_t *bs, float dist ) {
	trace_t	tr;
	vec3_t	end;

	VectorCopy( bs->cur_ps.origin, end );
	end[2] -= dist;
	trap_Trace( &tr, bs->cur_ps.origin, NULL, NULL, end, bs->client, MASK_PLAYERSOLID );
	return tr.fraction < 1.0f && !( tr.surfaceFlags & SURF_SKY );
}

/*
==================
BotRearmPriorityTarget

The juggernaut and players with a bounty are worth chasing across the map
==================
*/
qboolean BotRearmPriorityTarget( bot_state_t *bs, int client ) {
	if ( client < 0 || client >= MAX_CLIENTS || client == bs->client ) {
		return qfalse;
	}
	return client == level.juggernaut || level.clients[client].bounty || G_DS_BotPriority( bs->client, client )
		|| G_DS_SurprisePriority( bs->client, client );	// ds-surprise: the ghost of a past run wants the player	// descent boss floors: the boss, most of the time
}

/*
==================
BotRearmHuntGoal

A DESCENT boss with nobody in sight walks to the nearest fighter: an honest path (jump pads,
its own moves), never a teleport. qtrue = goal set
==================
*/
qboolean BotRearmHuntGoal( bot_state_t *bs, bot_goal_t *goal ) {
	vec3_t	pos;
	int		area;

	if ( !G_DS_BossHuntSpot( bs->client, pos ) && !G_DS_SurpriseHuntSpot( bs->client, pos )
		&& !G_DS_AffixHuntSpot( bs->client, pos ) ) {		// ds-surprise: mini-boss / ghost / fodder hunt, the gold race; en-affix: champions / copies
		return qfalse;
	}
	area = BotPointAreaNum( pos );
	if ( !area || !trap_AAS_AreaReachability( area ) ) {
		return qfalse;
	}
	memset( goal, 0, sizeof( *goal ) );
	VectorCopy( pos, goal->origin );
	goal->areanum = area;
	VectorSet( goal->mins, -8, -8, -8 );
	VectorSet( goal->maxs, 8, 8, 8 );
	goal->entitynum = -1;
	return qtrue;
}

/*
==================
BotRearmAggressive

Modes where running away for health and weapons doesn't make sense
==================
*/
qboolean BotRearmAggressive( bot_state_t *bs ) {
	if ( !trap_Cvar_VariableIntegerValue( "bot_rearm" ) ) {
		return qfalse;
	}
	return level.rmMode == RM_MODE_GUNGAME || level.rmMode == RM_MODE_INSTAGIB || level.rmMode == RM_MODE_OITC
		|| bs->client == level.juggernaut || bs->client == G_DS_BossClient()
		|| G_DS_SurpriseAggressive( bs->client );	// a descent boss never runs for health
}

/*
==================
BotRearmEnemiesNear

Other living clients near the enemy (for the lightning chain arc)
==================
*/
static int BotRearmEnemiesNear( bot_state_t *bs, gentity_t *enemy ) {
	gentity_t	*other;
	int			i, count = 0;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		other = &g_entities[i];
		if ( other == enemy || i == bs->client || !other->inuse || !other->client || other->health <= 0
			|| other->client->sess.sessionTeam == TEAM_SPECTATOR || BotSameTeam( bs, i ) ) {
			continue;
		}
		if ( Distance( other->r.currentOrigin, enemy->r.currentOrigin ) < 350 ) {
			count++;
		}
	}
	return count;
}

/*
==================
BotRearmWantsAlt

Decide once per shot whether this shot should be the alt-fire
==================
*/
#define ALT_PRIMARY		0
#define ALT_TAP			1
#define ALT_CHARGED		2
#define ALT_TAP_HOLD	130		// msec the button is held for a quick alt

static int BotRearmHold( int lo, int hi ) {
	return lo + (int)( random() * ( hi - lo ) );
}

static qboolean BotRearmEnemyWet( gentity_t *enemy ) {
#ifndef MISSIONPACK
	return enemy->client && enemy->client->ps.powerups[PW_WET] > level.time;
#else
	return qfalse;
#endif
}

/*
==================
BotRearmAltPlan

Decide what the next shot is: primary, the quick alt, or a charged alt held for *hold msec
==================
*/
static int BotRearmAltPlan( bot_state_t *bs, gentity_t *enemy, float dist, int *hold ) {
	playerState_t	*ps = &bs->cur_ps;
	const altFire_t	*alt = &bg_altFire[ps->weapon];
	const rmPersona_t	*persona = BotPersona( bs );
	int				ammo = ps->ammo[ps->weapon];
	qboolean		canCharge;
	float			roll = random();
	float			r = roll / persona->alt;		// quick alt rolls
	float			rc = roll / persona->charge;	// charged alt rolls

	// v4: charging is cheap under the overcharge mutator / overcharger perk: do it more
	if ( BotTrickOn( BOT_TRICK_MOVES ) && ( ( RM_FLAGS2( ps ) & RMF2_OVERCHARGE ) || ( ps->stats[STAT_RMFLAGS] & RMP_OVERCHARGER ) ) ) {
		rc /= 1.5f;
	}

	canCharge = ( ps->stats[STAT_RMFLAGS] & RMF_CHARGE ) && alt->chargeAmmo > 0 && ( ammo < 0 || ammo >= alt->chargeAmmo );
	*hold = ALT_TAP_HOLD;

	switch ( ps->weapon ) {
	case WP_MACHINEGUN:
		if ( dist > 800 && canCharge && rc < 0.6f ) {
			*hold = BotRearmHold( 500, 1100 );
			return ALT_CHARGED;			// marksman shot
		}
		return ( dist > 500 || r < 0.25f ) ? ALT_TAP : ALT_PRIMARY;
	case WP_SHOTGUN:
		if ( dist < 260 && r < 0.35f ) {
			if ( canCharge && rc < 0.15f ) {
				*hold = BotRearmHold( 400, 800 );
				return ALT_CHARGED;		// gale
			}
			return ALT_TAP;				// air blast them off a ledge
		}
		return ALT_PRIMARY;
	case WP_GRENADE_LAUNCHER:
		if ( canCharge && dist > 250 && dist < 1000 && rc < 0.4f ) {
			*hold = BotRearmHold( 700, 1200 );
			return ALT_CHARGED;			// napalm
		}
		return r < 0.7f ? ALT_TAP : ALT_PRIMARY;
	case WP_ROCKET_LAUNCHER:
		if ( canCharge && dist > 250 && dist < 1200 && rc < 0.35f ) {
			*hold = BotRearmHold( 800, 1200 );
			return ALT_CHARGED;			// boulder
		}
		return ALT_PRIMARY;
	case WP_LIGHTNING:
		if ( canCharge && dist > 500 && rc < 0.5f ) {
			*hold = BotRearmHold( 800, 1200 );
			return ALT_CHARGED;			// thunderstrike
		}
		if ( ammo > 20 && ( BotRearmEnemiesNear( bs, enemy ) > 0 || BotRearmEnemyWet( enemy ) ) && r < 0.8f ) {
			return ALT_TAP;				// chain arc
		}
		return ALT_PRIMARY;
	case WP_RAILGUN:
		if ( dist > 500 && canCharge && rc < 0.5f ) {
			*hold = BotRearmHold( 900, 1200 );
			return ALT_CHARGED;			// lance
		}
		return ( dist > 500 && ammo >= 2 && r < 0.5f ) ? ALT_TAP : ALT_PRIMARY;
	case WP_PLASMAGUN:
		if ( dist > 250 && dist < 1200 && r < 0.5f ) {
			if ( canCharge && rc < 0.3f ) {
				*hold = BotRearmHold( 600, 1200 );
				return ALT_CHARGED;		// big whirlpool
			}
			return ammo > 30 ? ALT_TAP : ALT_PRIMARY;
		}
		return ALT_PRIMARY;
	case WP_BFG:
		if ( canCharge && rc < 0.35f ) {
			*hold = BotRearmHold( 700, 1200 );
			return ALT_CHARGED;			// overload
		}
		return ( ammo >= 3 && r < 0.45f ) ? ALT_TAP : ALT_PRIMARY;
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case WP_NAILGUN:
		if ( dist > 350 && canCharge && rc < 0.5f ) {
			*hold = BotRearmHold( 500, 1100 );
			return ALT_CHARGED;			// pinning spike
		}
		return dist > 450 ? ALT_TAP : ALT_PRIMARY;
	case WP_PROX_LAUNCHER:
		if ( canCharge && dist > 200 && dist < 900 && rc < 0.5f ) {
			*hold = BotRearmHold( 600, 900 );
			return ALT_CHARGED;			// minefield
		}
		return ALT_PRIMARY;
	case WP_CHAINGUN:
		return dist > 900 ? ALT_TAP : ALT_PRIMARY;	// single accurate shots at range
#endif
	default:
		return ALT_PRIMARY;
	}
}

/*
==================
BotRearmIncoming

An enemy projectile about to hit the bot (for the shotgun air blast)
==================
*/
static qboolean BotRearmIncoming( bot_state_t *bs ) {
	gentity_t	*m;
	vec3_t		to, vel;
	float		speed, along, dist;
	int			i;

	for ( i = MAX_CLIENTS, m = &g_entities[MAX_CLIENTS] ; i < level.num_entities ; i++, m++ ) {
		if ( !m->inuse || m->s.eType != ET_MISSILE || m->r.ownerNum == bs->client || m->s.pos.trType == TR_STATIONARY ) {
			continue;
		}
		VectorSubtract( bs->cur_ps.origin, m->r.currentOrigin, to );
		dist = VectorLength( to );
		if ( dist > 320 ) {
			continue;
		}
		VectorCopy( m->s.pos.trDelta, vel );
		speed = VectorNormalize( vel );
		along = DotProduct( to, vel );
		if ( speed > 100 && along > 0 && dist * dist - along * along < 70 * 70 ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
==================
BotRearmInput

Called after the stock AI converted its input to a usercmd
==================
*/
void BotRearmInput( bot_state_t *bs, usercmd_t *ucmd ) {
	playerState_t	*ps = &bs->cur_ps;
	gentity_t		*enemy = NULL;
	vec3_t			dir;
	float			dist = 0, skill;
	qboolean		visible = qfalse;

	// weapon roulette: the server swaps the gun, never ask for the old one back
	if ( level.mutator == MUT_WEAPON_ROULETTE && g_entities[bs->client].client ) {
		ucmd->weapon = g_entities[bs->client].client->ps.weapon;
	}
	// what led up to a fall (logged for stock bots too)
	BotRearmFallTrack( bs, ucmd );
	if ( !trap_Cvar_VariableIntegerValue( "bot_rearm" ) ) {
		return;
	}
	BotPersonaFrame( bs );
	// kills (adrenaline: dash more right after one)
	if ( bs->num_kills != bs->rmKillCount ) {
		if ( bs->num_kills > bs->rmKillCount ) {
			bs->rmKillTime = level.time;
		}
		bs->rmKillCount = bs->num_kills;
	}		// personality: taunts on kills / deaths / airshots (runs while dead too)
	if ( ps->pm_type != PM_NORMAL || ps->stats[STAT_HEALTH] <= 0 ) {
		return;
	}
	skill = trap_Characteristic_BFloat( bs->character, CHARACTERISTIC_AIM_SKILL, 0, 1 );

	if ( bs->enemy >= 0 && bs->enemy < MAX_CLIENTS ) {
		enemy = &g_entities[bs->enemy];
		if ( !enemy->inuse || !enemy->client || enemy->health <= 0 ) {
			enemy = NULL;
		} else {
			VectorSubtract( enemy->r.currentOrigin, ps->origin, dir );
			dist = VectorLength( dir );
			visible = BotEntityVisible( bs->entitynum, bs->eye, bs->viewangles, 360, bs->enemy ) > 0;
			if ( visible ) {
				bs->rmEnemySeen = level.time;
			}
		}
	}

	BotRearmMovement( bs, ucmd, enemy, dist, visible );

	// v4: blow up the barrel they're standing next to
	if ( BotRearmPropShot( bs, ucmd, enemy, visible, skill ) ) {
		return;
	}

	// --- alt-fire ------------------------------------------------------------------
	if ( !enemy ) {
		return;
	}
	// descent: a locked alt does nothing, so never hold it (that would also stop the primary)
	if ( !( ps->stats[STAT_RMFLAGS] & RMF_ALTFIRE ) || !RM_ALT_USABLE( ps, ps->weapon ) ) {
		bs->rmAltHoldUntil = 0;
		ucmd->buttons &= ~BUTTON_ALTATTACK;
		return;
	}
	bs->rmAltToggle ^= 1;

	// remote detonation (press on alternate frames so the trigger re-arms)
	if ( bs->rmAltToggle && ( ( ps->weapon == WP_ROCKET_LAUNCHER && BotRearmOwnMissileNear( bs, WP_ROCKET_LAUNCHER, enemy->r.currentOrigin ) )
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
		|| ( ps->weapon == WP_PROX_LAUNCHER && BotRearmOwnMissileNear( bs, WP_PROX_LAUNCHER, enemy->r.currentOrigin ) )
#endif
		) ) {
		ucmd->buttons |= BUTTON_ALTATTACK;
		return;
	}

	// holding alt-fire: a quick tap or a charge, then one frame released so it fires
	if ( bs->rmAltHoldUntil ) {
		ucmd->buttons &= ~BUTTON_ATTACK;
		if ( level.time < bs->rmAltHoldUntil && ps->weapon == bs->rmAltWeapon && ps->stats[STAT_HEALTH] > 0 ) {
			ucmd->buttons |= BUTTON_ALTATTACK;
		} else {
			ucmd->buttons &= ~BUTTON_ALTATTACK;
			bs->rmAltHoldUntil = 0;
		}
		return;
	}
	if ( ps->weaponTime > 0 ) {
		return;
	}

	// shotgun: blow incoming projectiles back at their owner
	if ( ps->weapon == WP_SHOTGUN && ps->ammo[WP_SHOTGUN] > 0 && random() < ( 0.35f + 0.5f * skill ) * BotPersona( bs )->reflect && BotRearmIncoming( bs ) ) {
		bs->rmAltHoldUntil = level.time + ALT_TAP_HOLD;
		bs->rmAltWeapon = ps->weapon;
		ucmd->buttons &= ~BUTTON_ATTACK;
		ucmd->buttons |= BUTTON_ALTATTACK;
		return;
	}

	// gauntlet lunge to close the gap, a charged rocket punch from further out
	if ( ps->weapon == WP_GAUNTLET && visible && dist > 90 && dist < 520 ) {
		bs->rmAltHoldUntil = level.time + ( ( dist > 300 && ( ps->stats[STAT_RMFLAGS] & RMF_CHARGE ) ) ? BotRearmHold( 450, 900 ) : ALT_TAP_HOLD );
		bs->rmAltWeapon = ps->weapon;
		ucmd->buttons |= BUTTON_ALTATTACK;
		return;
	}

#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	// keep the chaingun barrel spinning between bursts while an enemy is in sight
	if ( ps->weapon == WP_CHAINGUN && visible && ( ucmd->buttons & BUTTON_ATTACK ) ) {
		ucmd->buttons |= BUTTON_ALTATTACK;
		return;
	}
#endif

	if ( !( ucmd->buttons & BUTTON_ATTACK ) || !visible ) {
		return;
	}
	// choose primary, quick alt or charged alt once per shot
	if ( level.time != bs->rmAltDecided ) {
		int		hold, plan;

		bs->rmAltDecided = level.time;
		plan = BotRearmAltPlan( bs, enemy, dist, &hold );
		if ( plan != ALT_PRIMARY ) {
			bs->rmAltHoldUntil = level.time + hold;
			bs->rmAltWeapon = ps->weapon;
			ucmd->buttons &= ~BUTTON_ATTACK;
			ucmd->buttons |= BUTTON_ALTATTACK;
		}
	}
}
