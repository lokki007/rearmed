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
// g_elements.c -- rearmed elements: fire (grenade launcher), water (plasma gun),
// electricity (lightning gun), wind (shotgun alt) and earth (rocket launcher).
// Statuses live in the playerState powerups array (PW_BURNING, PW_WET, PW_SHOCKED).

#include "g_local.h"

#ifndef MISSIONPACK

#define BURN_TIME			3000
#define BURN_TICK			400
#define BURN_DAMAGE			4
#define WET_TIME			4000
#define SHOCK_TIME			450
#define WET_SHOCK_SCALE		1.5f	// electricity on a soaked target
#define WET_ARC_RANGE		420		// shocks jump between soaked players this far apart

#define FIRE_PATCH_RADIUS	80
#define FIRE_PATCH_TICK		250
#define FIRE_PATCH_DAMAGE	3
#define MAX_FIRE_PATCHES	32

#define TREMOR_RADIUS		230
#define TREMOR_UP			340

static int	firePatchCount;

static qboolean G_ElementsOn( void ) {
	return G_RearmOn( RS_ELEMENTS );
}

static qboolean G_StatusTarget( gentity_t *ent ) {
	return ent && ent->inuse && ent->client && ent->health > 0
		&& ent->client->sess.sessionTeam != TEAM_SPECTATOR && ent->client->ps.pm_type == PM_NORMAL;
}

static void G_Steam( gentity_t *ent ) {
	gentity_t	*tent;

	tent = G_TempEntity( ent->r.currentOrigin, EV_STEAM );
	tent->s.otherEntityNum = ent->s.number;
}

/*
===============
Statuses
===============
*/
static void G_Ignite( gentity_t *ent, gentity_t *attacker, int msec ) {
	gclient_t	*cl = ent->client;

	if ( cl->ps.powerups[PW_WET] > level.time ) {
		// water beats fire: the target dries off instead
		cl->ps.powerups[PW_WET] = 0;
		G_Steam( ent );
		return;
	}
	if ( cl->ps.powerups[PW_BURNING] < level.time ) {
		cl->burnNextTick = level.time + BURN_TICK;
	}
	if ( cl->ps.powerups[PW_BURNING] < level.time + msec ) {
		cl->ps.powerups[PW_BURNING] = level.time + msec;
	}
	cl->burnAttacker = ( attacker && attacker->client ) ? attacker - g_entities : ENTITYNUM_WORLD;
}

// descent arsenal: incendiary pellets
void G_ElementsIgnite( gentity_t *ent, gentity_t *attacker, int msec ) {
	if ( G_StatusTarget( ent ) ) {
		G_Ignite( ent, attacker, msec );
	}
}

static void G_Soak( gentity_t *ent, int msec ) {
	gclient_t	*cl = ent->client;

	if ( cl->ps.powerups[PW_BURNING] > level.time ) {
		cl->ps.powerups[PW_BURNING] = 0;
		G_Steam( ent );
	}
	if ( cl->ps.powerups[PW_WET] < level.time + msec ) {
		cl->ps.powerups[PW_WET] = level.time + msec;
	}
}

static void G_Shock( gentity_t *ent, int msec ) {
	if ( ent->client->ps.powerups[PW_SHOCKED] < level.time + msec ) {
		ent->client->ps.powerups[PW_SHOCKED] = level.time + msec;
	}
}

/*
===============
G_WetArcs

A shock on a soaked player jumps to the other soaked players nearby
===============
*/
static void G_WetArcs( gentity_t *from, gentity_t *attacker, int damage ) {
	gentity_t	*other, *tent;
	trace_t		tr;
	int			i;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		other = &g_entities[i];
		if ( other == from || other == attacker || !G_StatusTarget( other )
			|| other->client->ps.powerups[PW_WET] <= level.time || OnSameTeam( other, attacker ) ) {
			continue;
		}
		if ( Distance( other->r.currentOrigin, from->r.currentOrigin ) > WET_ARC_RANGE ) {
			continue;
		}
		trap_Trace( &tr, from->r.currentOrigin, NULL, NULL, other->r.currentOrigin, ENTITYNUM_NONE, MASK_SOLID );
		if ( tr.fraction < 1.0f ) {
			continue;
		}
		tent = G_TempEntity( from->r.currentOrigin, EV_ARC );
		VectorCopy( other->r.currentOrigin, tent->s.origin2 );
		SnapVector( tent->s.origin2 );
		G_Damage( other, attacker, attacker, NULL, other->r.currentOrigin, damage, DAMAGE_NO_KNOCKBACK, MOD_THUNDER );
	}
}

/*
===============
G_ElementsDamage

Called from G_Damage before armor: element statuses and combos.
Returns the (possibly scaled) damage.
===============
*/
int G_ElementsDamage( gentity_t *targ, gentity_t *attacker, int damage, int mod ) {
	qboolean	wet;

	if ( !G_ElementsOn() || !G_StatusTarget( targ ) ) {
		return damage;
	}
	if ( attacker && attacker->client && attacker != targ && OnSameTeam( targ, attacker ) ) {
		return damage;
	}
	wet = targ->client->ps.powerups[PW_WET] > level.time;
	if ( !wet && ( mod == MOD_LIGHTNING || mod == MOD_THUNDER ) && G_HasPerk( attacker, PERK_CONDUCTOR ) ) {
		wet = qtrue;		// conductor: everyone counts as soaked
	}

	switch ( mod ) {
	case MOD_PLASMA:
	case MOD_PLASMA_SPLASH:
		G_Soak( targ, WET_TIME );
		break;
	case MOD_GRENADE:
	case MOD_GRENADE_SPLASH:
		G_Ignite( targ, attacker, BURN_TIME );
		break;
	case MOD_LIGHTNING:
		G_Shock( targ, SHOCK_TIME );
		if ( wet ) {
			damage = damage * WET_SHOCK_SCALE;
			G_WetArcs( targ, attacker, damage / 2 );
			if ( attacker && attacker->client && attacker != targ && level.time - attacker->client->styleTime > 500 ) {
				G_StyleAdd( attacker, 30, "CONDUCTED" );
			}
		}
		break;
	case MOD_THUNDER:
		G_Shock( targ, SHOCK_TIME * 2 );
		if ( wet ) {
			damage = damage * WET_SHOCK_SCALE;
		}
		break;
	default:
		break;
	}
	return damage;
}

/*
===============
G_ElementsClientFrame

Burning damage over time, water puts fire out
===============
*/
void G_ElementsClientFrame( gentity_t *ent ) {
	gclient_t	*cl = ent->client;
	gentity_t	*burner;

	if ( !G_StatusTarget( ent ) ) {
		return;
	}
	if ( ent->waterlevel >= 2 ) {
		if ( cl->ps.powerups[PW_BURNING] > level.time ) {
			cl->ps.powerups[PW_BURNING] = 0;
			G_Steam( ent );
		}
		if ( G_ElementsOn() && cl->ps.powerups[PW_WET] < level.time + 3000 ) {
			cl->ps.powerups[PW_WET] = level.time + 3000;
		}
	}
	if ( cl->ps.powerups[PW_BURNING] > level.time && level.time >= cl->burnNextTick ) {
		cl->burnNextTick = level.time + BURN_TICK;
		burner = NULL;
		if ( cl->burnAttacker >= 0 && cl->burnAttacker < MAX_CLIENTS && g_entities[cl->burnAttacker].inuse
			&& g_entities[cl->burnAttacker].client ) {
			burner = &g_entities[cl->burnAttacker];
		}
		G_Damage( ent, burner, burner, NULL, NULL, BURN_DAMAGE, DAMAGE_NO_KNOCKBACK | DAMAGE_NO_ARMOR, MOD_BURN );
	}
}

/*
===============
G_ElementsSpeedScale

Shocked players stagger, soaked ones are a little slower, pinned ones barely move
===============
*/
float G_ElementsSpeedScale( gclient_t *cl ) {
	float	scale = 1.0f;

	if ( cl->ps.powerups[PW_SHOCKED] > level.time ) {
		scale *= 0.7f;
	}
	if ( cl->ps.powerups[PW_WET] > level.time ) {
		scale *= 0.93f;
	}
	if ( cl->pinnedUntil > level.time ) {
		scale *= 0.2f;
	}
	return scale;
}

/*
===============
Fire patches: burning ground left by grenades and napalm
===============
*/
static void G_FirePatchThink( gentity_t *ent ) {
	gentity_t	*other, *owner;
	vec3_t		d;
	int			i;

	if ( level.time >= ent->count ) {
		firePatchCount--;
		G_FreeEntity( ent );
		return;
	}
	owner = ( ent->r.ownerNum >= 0 && ent->r.ownerNum < MAX_CLIENTS && g_entities[ent->r.ownerNum].inuse )
		? &g_entities[ent->r.ownerNum] : NULL;
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		other = &g_entities[i];
		if ( !G_StatusTarget( other ) ) {
			continue;
		}
		VectorSubtract( other->r.currentOrigin, ent->r.currentOrigin, d );
		if ( fabs( d[2] ) > 64 || d[0] * d[0] + d[1] * d[1] > ent->random * ent->random ) {
			continue;
		}
		if ( owner && other != owner && OnSameTeam( other, owner ) ) {
			continue;
		}
		if ( G_DS_AffixNoSelfBurn( owner, other ) ) {
			continue;		// en-affix: a BLAZING bot walks through its own trail
		}
		if ( other->client->ps.powerups[PW_WET] > level.time ) {
			other->client->ps.powerups[PW_WET] = 0;		// the fire dries them off
			G_Steam( other );
			continue;
		}
		G_Ignite( other, owner, 1500 );
		G_Damage( other, ent, owner, NULL, NULL, FIRE_PATCH_DAMAGE, DAMAGE_NO_KNOCKBACK, MOD_BURN );
	}
	ent->nextthink = level.time + FIRE_PATCH_TICK;
}

void G_SpawnFirePatch( vec3_t origin, gentity_t *owner, int msec, float radius ) {
	gentity_t	*ent;
	trace_t		tr;
	vec3_t		down;

	if ( firePatchCount >= MAX_FIRE_PATCHES ) {
		return;
	}
	// settle on the floor
	VectorCopy( origin, down );
	down[2] -= 96;
	trap_Trace( &tr, origin, NULL, NULL, down, ENTITYNUM_NONE, MASK_SOLID );
	if ( tr.startsolid || tr.fraction == 1.0f ) {
		return;
	}
	ent = G_Spawn();
	ent->classname = "fire_patch";
	ent->s.eType = ET_GENERAL;
	ent->s.generic1 = RM_ENT_FIRE;
	ent->s.time = level.time;			// the cgame fades it in and out
	ent->s.time2 = level.time + msec;
	ent->s.otherEntityNum2 = (int)radius;
	ent->r.ownerNum = owner ? owner->s.number : ENTITYNUM_NONE;
	ent->random = radius;
	ent->count = level.time + msec;
	ent->think = G_FirePatchThink;
	ent->nextthink = level.time + 100;
	VectorMA( tr.endpos, 2, tr.plane.normal, ent->s.origin );
	G_SetOrigin( ent, ent->s.origin );
	trap_LinkEntity( ent );
	firePatchCount++;
}

void G_ElementsInit( void ) {
	firePatchCount = 0;
}

/*
===============
G_EarthTremor

Rocket blasts shake the ground: everyone standing nearby is thrown up (juggle setups)
===============
*/
void G_EarthTremor( vec3_t origin, gentity_t *attacker, float radius, float up ) {
	gentity_t	*other;
	float		dist, f;
	int			i;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		other = &g_entities[i];
		if ( !G_StatusTarget( other ) || other == attacker || other->client->ps.groundEntityNum == ENTITYNUM_NONE ) {
			continue;
		}
		if ( attacker && attacker->client && OnSameTeam( other, attacker ) ) {
			continue;
		}
		dist = Distance( origin, other->r.currentOrigin );
		if ( dist > radius || fabs( origin[2] - other->r.currentOrigin[2] ) > 80 ) {
			continue;
		}
		f = 1.0f - 0.5f * dist / radius;
		if ( other->client->ps.velocity[2] < up * f ) {
			other->client->ps.velocity[2] = up * f;
		}
		other->client->ps.groundEntityNum = ENTITYNUM_NONE;
		other->client->ps.pm_time = 150;
		other->client->ps.pm_flags |= PMF_TIME_KNOCKBACK;
	}
}

/*
===============
G_ElementsExplode

A missile exploded at origin: fire and earth
===============
*/
void G_ElementsExplode( gentity_t *missile, vec3_t origin ) {
	gentity_t	*owner = &g_entities[missile->r.ownerNum];
	float		f;
	int			i, n;
	vec3_t		pos;

	if ( missile->s.weapon == WP_GRENADE_LAUNCHER && ( missile->s.generic1 & ALTFIRE_CHARGED ) ) {
		// napalm: a ring of fire
		f = missile->random;
		n = 3 + (int)( 4 * f );
		G_SpawnFirePatch( origin, owner, 3500 + 1500 * f, FIRE_PATCH_RADIUS );
		for ( i = 0 ; i < n ; i++ ) {
			pos[0] = origin[0] + cos( 2 * M_PI * i / n ) * ( 70 + 70 * f );
			pos[1] = origin[1] + sin( 2 * M_PI * i / n ) * ( 70 + 70 * f );
			pos[2] = origin[2] + 16;
			G_SpawnFirePatch( pos, owner, 3000 + 1500 * f, FIRE_PATCH_RADIUS );
		}
		return;
	}
	if ( missile->s.weapon == WP_ROCKET_LAUNCHER && ( missile->s.generic1 & ALTFIRE_CHARGED ) ) {
		// boulder: a big quake
		f = missile->random;
		G_EarthTremor( origin, owner, 320 + 200 * f, 460 + 160 * f );
		return;
	}
	if ( !G_ElementsOn() ) {
		return;
	}
	if ( missile->s.weapon == WP_GRENADE_LAUNCHER ) {
		G_SpawnFirePatch( origin, owner, 3000, FIRE_PATCH_RADIUS * 0.8f );
	} else if ( missile->s.weapon == WP_ROCKET_LAUNCHER ) {
		G_EarthTremor( origin, owner, TREMOR_RADIUS, TREMOR_UP );
	}
}

/*
===============
G_Whirlpool

Plasma alt: soak everyone the whirlpool reaches
===============
*/
void G_WhirlpoolSoak( gentity_t *other ) {
	if ( G_ElementsOn() && G_StatusTarget( other ) ) {
		G_Soak( other, WET_TIME );
	}
}

/*
===============
G_AirBlast

Shotgun alt: a cone of wind that throws players back and turns projectiles around.
charge 0..1 makes it reach further and hit harder.
===============
*/
#define AIRBLAST_RANGE		260
#define AIRBLAST_CONE		0.62f	// cos of the half angle
#define AIRBLAST_PUSH		700

void G_AirBlast( gentity_t *ent, vec3_t muzzle, vec3_t forward, float charge ) {
	gentity_t	*other, *tent, *owner;
	vec3_t		dir, center, vel;
	trace_t		tr;
	float		range, push, dist, speed;
	int			i;

	range = AIRBLAST_RANGE + 240 * charge;
	push = AIRBLAST_PUSH + 600 * charge;

	tent = G_TempEntity( muzzle, EV_WINDBLAST );
	VectorCopy( forward, tent->s.angles2 );
	tent->s.eventParm = (int)( 255 * charge );
	tent->s.otherEntityNum = ent->s.number;

	// players
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		other = &g_entities[i];
		if ( other == ent || !G_StatusTarget( other ) || OnSameTeam( other, ent ) ) {
			continue;
		}
		VectorCopy( other->r.currentOrigin, center );
		center[2] += 8;
		VectorSubtract( center, muzzle, dir );
		dist = VectorNormalize( dir );
		if ( dist > range || DotProduct( dir, forward ) < AIRBLAST_CONE ) {
			continue;
		}
		trap_Trace( &tr, muzzle, NULL, NULL, center, ent->s.number, MASK_SOLID );
		if ( tr.fraction < 1.0f ) {
			continue;
		}
		VectorScale( forward, push * ( 1.0f - 0.4f * dist / range ), vel );
		vel[2] += 180 + 160 * charge;
		VectorAdd( other->client->ps.velocity, vel, other->client->ps.velocity );
		other->client->ps.groundEntityNum = ENTITYNUM_NONE;
		other->client->ps.pm_time = 250;
		other->client->ps.pm_flags |= PMF_TIME_KNOCKBACK;
		// wind fans the flames away
		other->client->ps.powerups[PW_BURNING] = 0;
		G_Damage( other, ent, ent, forward, center, 6 + (int)( 10 * charge ), DAMAGE_NO_KNOCKBACK, MOD_SHOTGUN );
	}

	// projectiles: turned around and handed to the blaster
	for ( i = MAX_CLIENTS ; i < level.num_entities ; i++ ) {
		other = &g_entities[i];
		if ( !other->inuse || other->s.eType != ET_MISSILE || other->r.ownerNum == ent->s.number
			|| other->s.pos.trType == TR_STATIONARY || other->s.weapon == WP_GRAPPLING_HOOK
			|| ( other->r.svFlags & SVF_NOCLIENT )				// prox mine stuck in a player
			|| ( other->enemy && other->classname && !strcmp( other->classname, "sticky_grenade" ) ) ) {
			continue;		// (and sticky grenades riding a player)
		}
		owner = &g_entities[other->r.ownerNum];
		if ( owner->client && owner != ent && OnSameTeam( owner, ent ) ) {
			continue;
		}
		VectorSubtract( other->r.currentOrigin, muzzle, dir );
		dist = VectorNormalize( dir );
		if ( dist > range + 60 || DotProduct( dir, forward ) < AIRBLAST_CONE - 0.1f ) {
			continue;
		}
		speed = VectorLength( other->s.pos.trDelta );
		if ( speed < 300 ) {
			speed = 300;
		}
		VectorCopy( other->r.currentOrigin, other->s.pos.trBase );
		VectorScale( forward, speed * 1.15f, other->s.pos.trDelta );
		SnapVector( other->s.pos.trDelta );
		other->s.pos.trTime = level.time;
		other->r.ownerNum = ent->s.number;
		other->parent = ent;
		other->s.generic1 |= ALTFIRE_REFLECTED;
		other->target_ent = NULL;
		if ( other->s.weapon == WP_ROCKET_LAUNCHER ) {
			vectoangles( forward, other->s.angles );
		}
		trap_LinkEntity( other );
	}

	// and blow out fires in front of us
	for ( i = MAX_CLIENTS ; i < level.num_entities ; i++ ) {
		other = &g_entities[i];
		if ( !other->inuse || other->s.eType != ET_GENERAL || other->s.generic1 != RM_ENT_FIRE ) {
			continue;
		}
		VectorSubtract( other->r.currentOrigin, muzzle, dir );
		dist = VectorNormalize( dir );
		if ( dist < range && DotProduct( dir, forward ) > AIRBLAST_CONE - 0.1f ) {
			other->count = level.time;
		}
	}
}

/*
===============
G_Thunderstrike

Charged lightning: a bolt from the sky where you aim
===============
*/
#define THUNDER_RANGE		2400

void G_Thunderstrike( gentity_t *ent, vec3_t muzzle, vec3_t forward, float charge, float quad ) {
	gentity_t	*other, *tent;
	trace_t		tr;
	vec3_t		end, point, sky, center;
	float		radius, dist;
	int			i, damage;

	VectorMA( muzzle, THUNDER_RANGE, forward, end );
	trap_Trace( &tr, muzzle, NULL, NULL, end, ent->s.number, MASK_SHOT );
	VectorCopy( tr.endpos, point );
	if ( tr.fraction < 1.0f ) {
		VectorMA( point, 4, tr.plane.normal, point );
	}
	VectorCopy( point, end );
	end[2] += 1400;
	trap_Trace( &tr, point, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID );
	VectorCopy( tr.endpos, sky );

	tent = G_TempEntity( point, EV_THUNDER );
	VectorCopy( sky, tent->s.origin2 );
	SnapVector( tent->s.origin2 );
	tent->r.svFlags |= SVF_BROADCAST;

	radius = 150 + 110 * charge;
	damage = ( 40 + 60 * charge ) * quad;
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		other = &g_entities[i];
		if ( other == ent || !G_StatusTarget( other ) || OnSameTeam( other, ent ) ) {
			continue;
		}
		VectorCopy( other->r.currentOrigin, center );
		dist = Distance( center, point );
		if ( dist > radius ) {
			continue;
		}
		trap_Trace( &tr, point, NULL, NULL, center, ENTITYNUM_NONE, MASK_SOLID );
		if ( tr.fraction < 1.0f ) {
			continue;
		}
		G_Damage( other, ent, ent, NULL, center, damage * ( 1.0f - 0.5f * dist / radius ), DAMAGE_NO_KNOCKBACK, MOD_THUNDER );
		if ( other->client ) {
			G_Shock( other, 900 );
		}
	}
}

/*
===============
G_StormStrike

Thunderstorm event: a bolt from the sky onto a floor point, nobody's kill
===============
*/
#define STORM_RADIUS		170
#define STORM_DAMAGE		60

void G_StormStrike( vec3_t point ) {
	gentity_t	*other, *tent;
	trace_t		tr;
	vec3_t		sky, center;
	float		dist;
	int			i;

	VectorCopy( point, sky );
	sky[2] += 3000;
	trap_Trace( &tr, point, NULL, NULL, sky, ENTITYNUM_NONE, MASK_SOLID );
	VectorCopy( tr.endpos, sky );

	tent = G_TempEntity( point, EV_THUNDER );
	VectorCopy( sky, tent->s.origin2 );
	SnapVector( tent->s.origin2 );
	tent->r.svFlags |= SVF_BROADCAST;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		other = &g_entities[i];
		if ( !G_StatusTarget( other ) ) {
			continue;
		}
		VectorCopy( other->r.currentOrigin, center );
		dist = Distance( center, point );
		if ( dist > STORM_RADIUS ) {
			continue;
		}
		trap_Trace( &tr, point, NULL, NULL, center, ENTITYNUM_NONE, MASK_SOLID );
		if ( tr.fraction < 1.0f ) {
			continue;
		}
		G_Damage( other, NULL, NULL, NULL, center, STORM_DAMAGE * ( 1.0f - 0.5f * dist / STORM_RADIUS ),
			DAMAGE_NO_KNOCKBACK, MOD_THUNDER );
		if ( other->client ) {
			G_Shock( other, 900 );
		}
	}
}

#else	// MISSIONPACK: no element statuses

int G_ElementsDamage( gentity_t *targ, gentity_t *attacker, int damage, int mod ) { return damage; }
void G_ElementsClientFrame( gentity_t *ent ) { }
float G_ElementsSpeedScale( gclient_t *cl ) { return 1.0f; }
void G_SpawnFirePatch( vec3_t origin, gentity_t *owner, int msec, float radius ) { }
void G_ElementsIgnite( gentity_t *ent, gentity_t *attacker, int msec ) { }
void G_ElementsInit( void ) { }
void G_EarthTremor( vec3_t origin, gentity_t *attacker, float radius, float up ) { }
void G_ElementsExplode( gentity_t *missile, vec3_t origin ) { }
void G_WhirlpoolSoak( gentity_t *other ) { }
void G_AirBlast( gentity_t *ent, vec3_t muzzle, vec3_t forward, float charge ) { }
void G_Thunderstrike( gentity_t *ent, vec3_t muzzle, vec3_t forward, float charge, float quad ) { }
void G_StormStrike( vec3_t point ) { }

#endif
