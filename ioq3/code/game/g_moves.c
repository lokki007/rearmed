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
// g_moves.c -- rearmed: server side of the new moves (ground pound shockwave, blink)

#include "g_local.h"

#define SLAM_RADIUS			170		// shockwave radius at the weakest ...
#define SLAM_RADIUS_ADD		130		// ... plus this much at full strength
#define SLAM_DAMAGE			18		// damage at the centre at the weakest ...
#define SLAM_DAMAGE_ADD		42		// ... plus this much at full strength
#define SLAM_LAUNCH			380		// upward speed given to the victims ...
#define SLAM_LAUNCH_ADD		260		// ... plus this much at full strength
#define SLAM_PUSH			140		// outward speed given to the victims
#define STOMP_DAMAGE		45		// landing right on someone's head ...
#define STOMP_DAMAGE_ADD	55		// ... plus this much at full strength
#define GROUNDZERO_SCALE	1.5f	// Ground Zero perk: radius, damage and launch

/*
===============
G_SlamImpact

A ground pound hit the floor (EV_SLAM_LAND, strength 0..255): a shockwave that hurts and
launches everyone around (never the slammer), and a stomp on whoever was landed on.
===============
*/
void G_SlamImpact( gentity_t *ent, int strength ) {
	gentity_t	*targ, *stomped;
	gclient_t	*cl;
	vec3_t		origin, mins, maxs, v, dir;
	float		s, radius, damage, launch, dist, f, scale;
	int			entityList[MAX_GENTITIES];
	int			num, i, e, hits, points;

	if ( !ent || !ent->client || ent->health <= 0 || !G_RearmOn( RS_SLAM ) ) {
		return;
	}
	cl = ent->client;
	s = strength / 255.0f;
	scale = G_HasPerk( ent, PERK_GROUNDZERO ) ? ( G_RL_Active() ? DSP_GROUNDZERO : GROUNDZERO_SCALE ) : 1.0f;	// DESCENT: +9%
	radius = ( SLAM_RADIUS + SLAM_RADIUS_ADD * s ) * scale;
	damage = ( SLAM_DAMAGE + SLAM_DAMAGE_ADD * s ) * scale;
	launch = ( SLAM_LAUNCH + SLAM_LAUNCH_ADD * s ) * ( scale > 1.0f ? ( G_RL_Active() ? DSP_GROUNDZERO : 1.2f ) : 1.0f );

	// the impact point: the slammer's feet
	VectorCopy( cl->ps.origin, origin );
	origin[2] += MINS_Z + 4;

	// stomp: landed right on top of someone
	stomped = NULL;
	if ( cl->ps.groundEntityNum >= 0 && cl->ps.groundEntityNum < MAX_CLIENTS ) {
		stomped = &g_entities[cl->ps.groundEntityNum];
		if ( !stomped->inuse || !stomped->client || stomped->health <= 0 || OnSameTeam( stomped, ent ) ) {
			stomped = NULL;
		}
	}
	if ( stomped ) {
		VectorSet( dir, 0, 0, -1 );
		G_Damage( stomped, ent, ent, dir, origin, (int)( ( STOMP_DAMAGE + STOMP_DAMAGE_ADD * s ) * scale ),
			DAMAGE_NO_KNOCKBACK, MOD_SLAM );
		G_StyleAdd( ent, 40, "STOMP" );		// (the bounce off the head is in pmove)
	}

	// the shockwave
	for ( i = 0 ; i < 3 ; i++ ) {
		mins[i] = origin[i] - radius;
		maxs[i] = origin[i] + radius;
	}
	num = trap_EntitiesInBox( mins, maxs, entityList, MAX_GENTITIES );
	hits = 0;
	for ( e = 0 ; e < num ; e++ ) {
		targ = &g_entities[entityList[e]];
		if ( targ == ent || !targ->takedamage || targ->health <= 0 ) {
			continue;
		}
		// distance from the edge of the bounding box
		for ( i = 0 ; i < 3 ; i++ ) {
			if ( origin[i] < targ->r.absmin[i] ) {
				v[i] = targ->r.absmin[i] - origin[i];
			} else if ( origin[i] > targ->r.absmax[i] ) {
				v[i] = origin[i] - targ->r.absmax[i];
			} else {
				v[i] = 0;
			}
		}
		v[2] *= 1.5f;		// a ground wave: height counts more than distance
		dist = VectorLength( v );
		if ( dist >= radius || !CanDamage( targ, origin ) ) {
			continue;
		}
		f = 1.0f - 0.6f * dist / radius;

		VectorSubtract( targ->r.currentOrigin, origin, dir );
		dir[2] = 0;
		VectorNormalize( dir );

		if ( targ->client ) {
			if ( targ != stomped ) {
				G_Damage( targ, ent, ent, dir, origin, (int)( damage * f ), DAMAGE_RADIUS | DAMAGE_NO_KNOCKBACK, MOD_SLAM );
			}
			if ( targ->health <= 0 || OnSameTeam( targ, ent ) || targ->client->ps.pm_type != PM_NORMAL ) {
				continue;
			}
			if ( targ == stomped ) {
				hits++;
				continue;	// crushed, not launched (and never thrown up into the stomper's feet)
			}
			// launch them: a juggle setup
			VectorMA( targ->client->ps.velocity, SLAM_PUSH * f, dir, targ->client->ps.velocity );
			if ( targ->client->ps.velocity[2] < launch * f ) {
				targ->client->ps.velocity[2] = launch * f;
			}
			targ->client->ps.groundEntityNum = ENTITYNUM_NONE;
			targ->client->ps.pm_time = 200;
			targ->client->ps.pm_flags |= PMF_TIME_KNOCKBACK;
			hits++;
		} else {
			// props and the like: thrown up and away
			dir[2] = 1.2f;
			G_Damage( targ, ent, ent, dir, origin, (int)( damage * f ), DAMAGE_RADIUS, MOD_SLAM );
		}
	}

	// rocks crack: the earth element throws the grounded players further out
	if ( G_RearmOn( RS_ELEMENTS ) ) {
		G_EarthTremor( origin, ent, radius * 1.4f, launch * 0.6f );
	}

	if ( hits > 0 ) {
		points = 12 + 8 * ( hits - 1 );
		G_StyleAdd( ent, points, hits > 1 ? "MULTI SHOCKWAVE" : "SHOCKWAVE" );
	}
	if ( g_debugMove.integer ) {
		G_Printf( "slam: client %i strength %i radius %.0f damage %.0f launched %i stomp %i\n",
			(int)( ent - g_entities ), strength, radius, damage, hits, stomped ? (int)( stomped - g_entities ) : -1 );
	}
}

/*
===============
G_BlinkDash

The Blink perk replaced a dash (EV_BLINK): the dash perks still apply
===============
*/
void G_BlinkDash( gentity_t *ent ) {
	if ( !ent || !ent->client ) {
		return;
	}
	G_PerkOnDash( ent );
}
