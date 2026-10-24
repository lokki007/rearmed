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
// g_weapon.c 
// perform the server side effects of a weapon firing

#include "g_local.h"

static	float	s_quadFactor;
static	vec3_t	forward, right, up;
static	vec3_t	muzzle;

#define NUM_NAILSHOTS 15

#define GAUNTLET_LAUNCH_UP		480
#define GAUNTLET_LAUNCH_PUSH	280
#define GAUNTLET_COMBO_TIME		700		// a hit this soon after a lunge is a combo

/*
================
G_BounceProjectile
================
*/
void G_BounceProjectile( vec3_t start, vec3_t impact, vec3_t dir, vec3_t endout ) {
	vec3_t v, newv;
	float dot;

	VectorSubtract( impact, start, v );
	dot = DotProduct( v, dir );
	VectorMA( v, -2*dot, dir, newv );

	VectorNormalize(newv);
	VectorMA(impact, 8192, newv, endout);
}


/*
======================================================================

GAUNTLET

======================================================================
*/

void Weapon_Gauntlet( gentity_t *ent ) {

}

/*
===============
CheckGauntletAttack
===============
*/
qboolean CheckGauntletAttack( gentity_t *ent ) {
	trace_t		tr;
	vec3_t		end;
	gentity_t	*tent;
	gentity_t	*traceEnt;
	int			damage;

	// set aiming directions
	AngleVectors (ent->client->ps.viewangles, forward, right, up);

	CalcMuzzlePoint ( ent, forward, right, up, muzzle );

	VectorMA (muzzle, G_DS_GauntletRange( ent ), forward, end);		// descent HEARTRIPPER reaches further

	trap_Trace (&tr, muzzle, NULL, NULL, end, ent->s.number, MASK_SHOT);
	if ( tr.surfaceFlags & SURF_NOIMPACT ) {
		return qfalse;
	}

	if ( ent->client->noclip ) {
		return qfalse;
	}

	traceEnt = &g_entities[ tr.entityNum ];

	// send blood impact
	if ( traceEnt->takedamage && traceEnt->client ) {
		tent = G_TempEntity( tr.endpos, EV_MISSILE_HIT );
		tent->s.otherEntityNum = traceEnt->s.number;
		tent->s.eventParm = DirToByte( tr.plane.normal );
		tent->s.weapon = ent->s.weapon;
	}

	if ( !traceEnt->takedamage) {
		return qfalse;
	}

	if (ent->client->ps.powerups[PW_QUAD] ) {
		G_AddEvent( ent, EV_POWERUP_QUAD, 0 );
		s_quadFactor = g_quadfactor.value;
	} else {
		s_quadFactor = 1;
	}
#ifdef MISSIONPACK
	if( ent->client->persistantPowerup && ent->client->persistantPowerup->item && ent->client->persistantPowerup->item->giTag == PW_DOUBLER ) {
		s_quadFactor *= 2;
	}
#endif

	damage = 50 * s_quadFactor;
	G_Damage( traceEnt, ent, ent, forward, tr.endpos,
		damage, 0, MOD_GAUNTLET );

	// uppercut: launch the victim, much higher right after a lunge
	if ( G_RearmOn( RS_UPPERCUT ) && traceEnt->client && traceEnt->health > 0 ) {
		float	scale = 1.0f;

		if ( level.time - ent->client->lungeTime < GAUNTLET_COMBO_TIME ) {
			scale = 1.6f + 0.8f * ent->client->lungeCharge;
			if ( ent->client->lungeCharge > 0 ) {
				G_Damage( traceEnt, ent, ent, forward, tr.endpos, 50 * ent->client->lungeCharge * s_quadFactor, 0, MOD_GAUNTLET );
			}
			ent->client->lungeTime = 0;
			ent->client->lungeCharge = 0;
			trap_SendServerCommand( ent - g_entities, "rmann medal \"LUNGE COMBO\"" );
		}
		VectorMA( traceEnt->client->ps.velocity, GAUNTLET_LAUNCH_PUSH * scale, forward, traceEnt->client->ps.velocity );
		if ( traceEnt->client->ps.velocity[2] < GAUNTLET_LAUNCH_UP * scale ) {
			traceEnt->client->ps.velocity[2] = GAUNTLET_LAUNCH_UP * scale;
		}
		traceEnt->client->ps.pm_time = 200;
		traceEnt->client->ps.pm_flags |= PMF_TIME_KNOCKBACK;
	}

	return qtrue;
}


/*
======================================================================

MACHINEGUN

======================================================================
*/

/*
======================
SnapVectorTowards

Round a vector to integers for more efficient network
transmission, but make sure that it rounds towards a given point
rather than blindly truncating.  This prevents it from truncating 
into a wall.
======================
*/
void SnapVectorTowards( vec3_t v, vec3_t to ) {
	int		i;

	for ( i = 0 ; i < 3 ; i++ ) {
		if ( to[i] <= v[i] ) {
			v[i] = floor(v[i]);
		} else {
			v[i] = ceil(v[i]);
		}
	}
}

#if defined(MISSIONPACK) || defined(TA_WEAPONS)
#define CHAINGUN_SPREAD		600
#define CHAINGUN_DAMAGE		7
#endif
#define MACHINEGUN_SPREAD	200
#define	MACHINEGUN_DAMAGE	7
#define	MACHINEGUN_TEAM_DAMAGE	5		// wimpier MG in teamplay

void Bullet_Fire (gentity_t *ent, float spread, int damage, int mod ) {
	trace_t		tr;
	vec3_t		end;
#ifdef MISSIONPACK
	vec3_t		impactpoint, bouncedir;
#endif
	float		r;
	float		u;
	gentity_t	*tent;
	gentity_t	*traceEnt;
	int			i, passent;

	damage *= s_quadFactor;

	r = random() * M_PI * 2.0f;
	u = sin(r) * crandom() * spread * 16;
	r = cos(r) * crandom() * spread * 16;
	VectorMA (muzzle, 8192*16, forward, end);
	VectorMA (end, r, right, end);
	VectorMA (end, u, up, end);

	passent = ent->s.number;
	for (i = 0; i < 10; i++) {

		trap_Trace (&tr, muzzle, NULL, NULL, end, passent, MASK_SHOT);
		if ( tr.surfaceFlags & SURF_NOIMPACT ) {
			return;
		}

		traceEnt = &g_entities[ tr.entityNum ];

		// snap the endpos to integers, but nudged towards the line
		SnapVectorTowards( tr.endpos, muzzle );

		// send bullet impact
		if ( traceEnt->takedamage && traceEnt->client ) {
			tent = G_TempEntity( tr.endpos, EV_BULLET_HIT_FLESH );
			tent->s.eventParm = traceEnt->s.number;
			if( LogAccuracyHit( traceEnt, ent ) ) {
				ent->client->accuracy_hits++;
			}
		} else {
			tent = G_TempEntity( tr.endpos, EV_BULLET_HIT_WALL );
			tent->s.eventParm = DirToByte( tr.plane.normal );
		}
		tent->s.otherEntityNum = ent->s.number;

		if ( traceEnt->takedamage) {
#ifdef MISSIONPACK
			if ( traceEnt->client && traceEnt->client->invulnerabilityTime > level.time ) {
				if (G_InvulnerabilityEffect( traceEnt, forward, tr.endpos, impactpoint, bouncedir )) {
					G_BounceProjectile( muzzle, impactpoint, bouncedir, end );
					VectorCopy( impactpoint, muzzle );
					// the player can hit him/herself with the bounced rail
					passent = ENTITYNUM_NONE;
				}
				else {
					VectorCopy( tr.endpos, muzzle );
					passent = traceEnt->s.number;
				}
				continue;
			}
			else {
#endif
				G_Damage( traceEnt, ent, ent, forward, tr.endpos,
					damage, 0, mod);
#ifdef MISSIONPACK
			}
#endif
		}
		break;
	}
}


/*
======================================================================

BFG

======================================================================
*/

void BFG_Fire ( gentity_t *ent ) {
	gentity_t	*m;

	m = fire_bfg (ent, muzzle, forward);
	m->damage *= s_quadFactor;
	m->splashDamage *= s_quadFactor;
	m->count = level.time + 10000;		// lifetime, the think zaps enemies meanwhile
	m->think = G_BFGThink;
	m->nextthink = level.time + 100;

//	VectorAdd( m->s.pos.trDelta, ent->client->ps.velocity, m->s.pos.trDelta );	// "real" physics
}


/*
======================================================================

SHOTGUN

======================================================================
*/

// DEFAULT_SHOTGUN_SPREAD and DEFAULT_SHOTGUN_COUNT	are in bg_public.h, because
// client predicts same spreads
#define	DEFAULT_SHOTGUN_DAMAGE	10

qboolean ShotgunPellet( vec3_t start, vec3_t end, gentity_t *ent ) {
	trace_t		tr;
	int			damage, i, passent;
	gentity_t	*traceEnt;
#ifdef MISSIONPACK
	vec3_t		impactpoint, bouncedir;
#endif
	vec3_t		tr_start, tr_end;
	qboolean	hitClient = qfalse;

	passent = ent->s.number;
	VectorCopy( start, tr_start );
	VectorCopy( end, tr_end );
	for (i = 0; i < 10; i++) {
		trap_Trace (&tr, tr_start, NULL, NULL, tr_end, passent, MASK_SHOT);
		traceEnt = &g_entities[ tr.entityNum ];

		// send bullet impact
		if (  tr.surfaceFlags & SURF_NOIMPACT ) {
			return qfalse;
		}

		if ( traceEnt->takedamage) {
			damage = DEFAULT_SHOTGUN_DAMAGE * s_quadFactor;
#ifdef MISSIONPACK
			if ( traceEnt->client && traceEnt->client->invulnerabilityTime > level.time ) {
				if (G_InvulnerabilityEffect( traceEnt, forward, tr.endpos, impactpoint, bouncedir )) {
					G_BounceProjectile( tr_start, impactpoint, bouncedir, tr_end );
					VectorCopy( impactpoint, tr_start );
					// the player can hit him/herself with the bounced rail
					passent = ENTITYNUM_NONE;
				}
				else {
					VectorCopy( tr.endpos, tr_start );
					passent = traceEnt->s.number;
				}
				continue;
			}
#endif
			if( LogAccuracyHit( traceEnt, ent ) ) {
				hitClient = qtrue;
			}
			G_Damage( traceEnt, ent, ent, forward, tr.endpos, damage, 0, MOD_SHOTGUN);
			return hitClient;
		}
		return qfalse;
	}
	return qfalse;
}

// this should match CG_ShotgunPattern
void ShotgunPattern( vec3_t origin, vec3_t origin2, int seed, gentity_t *ent ) {
	int			i;
	float		r, u;
	vec3_t		end;
	vec3_t		localForward, localRight, localUp;
	qboolean	hitClient = qfalse;

	// derive the right and up vectors from the forward vector, because
	// the client won't have any other information
	VectorNormalize2( origin2, localForward );
	PerpendicularVector( localRight, localForward );
	CrossProduct( localForward, localRight, localUp );

	// generate the "random" spread pattern
	for ( i = 0 ; i < DEFAULT_SHOTGUN_COUNT ; i++ ) {
		r = Q_crandom( &seed ) * DEFAULT_SHOTGUN_SPREAD * 16;
		u = Q_crandom( &seed ) * DEFAULT_SHOTGUN_SPREAD * 16;
		VectorMA( origin, 8192 * 16, localForward, end);
		VectorMA (end, r, localRight, end);
		VectorMA (end, u, localUp, end);
		if( ShotgunPellet( origin, end, ent ) && !hitClient ) {
			hitClient = qtrue;
			ent->client->accuracy_hits++;
		}
	}
}


void weapon_supershotgun_fire (gentity_t *ent) {
	gentity_t		*tent;

	// send shotgun blast
	tent = G_TempEntity( muzzle, EV_SHOTGUN );
	VectorScale( forward, 4096, tent->s.origin2 );
	SnapVector( tent->s.origin2 );
	tent->s.eventParm = rand() & 255;		// seed for spread pattern
	tent->s.otherEntityNum = ent->s.number;

	ShotgunPattern( tent->s.pos.trBase, tent->s.origin2, tent->s.eventParm, ent );
}


/*
======================================================================

GRENADE LAUNCHER

======================================================================
*/

void weapon_grenadelauncher_fire (gentity_t *ent) {
	gentity_t	*m;

	// extra vertical velocity
	forward[2] += 0.2f;
	VectorNormalize( forward );

	m = fire_grenade (ent, muzzle, forward);
	m->damage *= s_quadFactor;
	m->splashDamage *= s_quadFactor;

//	VectorAdd( m->s.pos.trDelta, ent->client->ps.velocity, m->s.pos.trDelta );	// "real" physics
}

/*
======================================================================

ROCKET

======================================================================
*/

void Weapon_RocketLauncher_Fire (gentity_t *ent) {
	gentity_t	*m;

	m = fire_rocket (ent, muzzle, forward);
	m->damage *= s_quadFactor;
	m->splashDamage *= s_quadFactor;

//	VectorAdd( m->s.pos.trDelta, ent->client->ps.velocity, m->s.pos.trDelta );	// "real" physics
}


/*
======================================================================

PLASMA GUN

======================================================================
*/

void Weapon_Plasmagun_Fire (gentity_t *ent) {
	gentity_t	*m;

	m = fire_plasma (ent, muzzle, forward);
	m->damage *= s_quadFactor;
	m->splashDamage *= s_quadFactor;

//	VectorAdd( m->s.pos.trDelta, ent->client->ps.velocity, m->s.pos.trDelta );	// "real" physics
}

/*
======================================================================

RAILGUN

======================================================================
*/


/*
=================
weapon_railgun_fire
=================
*/
#define	MAX_RAIL_HITS	4
#define RAIL_PIERCE_DEPTH	80
static int	s_railDamage = 100;		// the alt-fire overcharge raises this for one shot
static int	s_railBounces = 0;		// ricochets left for the current overcharged shot
static int	s_railDepth = 0;		// ricochet segment of the current shot
static int	s_railPierce = 0;		// walls the current shot may still go through (charged lance)
static gentity_t	*s_railHit[MAX_RAIL_HITS];	// targets already hit by this shot, across ricochets
static int	s_railNumHit = 0;

void weapon_railgun_fire (gentity_t *ent) {
	vec3_t		end;
#ifdef MISSIONPACK
	vec3_t impactpoint, bouncedir;
#endif
	trace_t		trace;
	gentity_t	*tent;
	gentity_t	*traceEnt;
	int			damage;
	int			i;
	int			hits;
	int			unlinked;
	int			passent;
	gentity_t	*unlinkedEntities[MAX_RAIL_HITS];

	damage = G_RearmRailDamage( s_railDamage ) * s_quadFactor;
	if ( s_railDepth == 0 ) {
		s_railNumHit = 0;
	} else {
		damage = damage * ( 10 - 3 * s_railDepth ) / 10;	// 100%, 70%, 40%
	}

	VectorMA (muzzle, 8192, forward, end);

	// trace only against the solids, so the railgun will go through people
	unlinked = 0;
	hits = 0;
	passent = ent->s.number;
	do {
		trap_Trace (&trace, muzzle, NULL, NULL, end, passent, MASK_SHOT );
		if ( trace.entityNum >= ENTITYNUM_MAX_NORMAL ) {
			break;
		}
		traceEnt = &g_entities[ trace.entityNum ];
		if ( traceEnt->takedamage ) {
#ifdef MISSIONPACK
			if ( traceEnt->client && traceEnt->client->invulnerabilityTime > level.time ) {
				if ( G_InvulnerabilityEffect( traceEnt, forward, trace.endpos, impactpoint, bouncedir ) ) {
					G_BounceProjectile( muzzle, impactpoint, bouncedir, end );
					// snap the endpos to integers to save net bandwidth, but nudged towards the line
					SnapVectorTowards( trace.endpos, muzzle );
					// send railgun beam effect
					tent = G_TempEntity( trace.endpos, EV_RAILTRAIL );
					// set player number for custom colors on the railtrail
					tent->s.clientNum = ent->s.clientNum;
					VectorCopy( muzzle, tent->s.origin2 );
					// move origin a bit to come closer to the drawn gun muzzle
					VectorMA( tent->s.origin2, 4, right, tent->s.origin2 );
					VectorMA( tent->s.origin2, -1, up, tent->s.origin2 );
					tent->s.eventParm = 255;	// don't make the explosion at the end
					//
					VectorCopy( impactpoint, muzzle );
					// the player can hit him/herself with the bounced rail
					passent = ENTITYNUM_NONE;
				}
			}
			else {
				if( LogAccuracyHit( traceEnt, ent ) ) {
					hits++;
				}
				G_Damage (traceEnt, ent, ent, forward, trace.endpos, damage, 0, MOD_RAILGUN);
			}
#else
			for ( i = 0 ; i < s_railNumHit && s_railHit[i] != traceEnt ; i++ ) {
			}
			if ( i == s_railNumHit && s_railNumHit < MAX_RAIL_HITS ) {
				s_railHit[s_railNumHit++] = traceEnt;
				if( LogAccuracyHit( traceEnt, ent ) ) {
					hits++;
				}
				G_Damage (traceEnt, ent, ent, forward, trace.endpos, damage, 0, MOD_RAILGUN);
			}
#endif
		}
		if ( trace.contents & CONTENTS_SOLID ) {
			break;		// we hit something solid enough to stop the beam
		}
		// unlink this entity, so the next trace will go past it
		trap_UnlinkEntity( traceEnt );
		unlinkedEntities[unlinked] = traceEnt;
		unlinked++;
	} while ( unlinked < MAX_RAIL_HITS );

	// link back in any entities we unlinked
	for ( i = 0 ; i < unlinked ; i++ ) {
		trap_LinkEntity( unlinkedEntities[i] );
	}

	// the final trace endpos will be the terminal point of the rail trail

	// snap the endpos to integers to save net bandwidth, but nudged towards the line
	SnapVectorTowards( trace.endpos, muzzle );

	// send railgun beam effect
	tent = G_TempEntity( trace.endpos, EV_RAILTRAIL );

	// set player number for custom colors on the railtrail
	tent->s.clientNum = ent->s.clientNum;
	tent->s.generic1 = s_railDamage > 100 ? 1 : 0;		// overcharged: the cgame draws a heavier beam

	VectorCopy( muzzle, tent->s.origin2 );
	// move origin a bit to come closer to the drawn gun muzzle
	VectorMA( tent->s.origin2, 4, right, tent->s.origin2 );
	VectorMA( tent->s.origin2, -1, up, tent->s.origin2 );

	// no explosion at end if SURF_NOIMPACT, but still make the trail
	if ( trace.surfaceFlags & SURF_NOIMPACT ) {
		tent->s.eventParm = 255;	// don't make the explosion at the end
	} else {
		tent->s.eventParm = DirToByte( trace.plane.normal );
	}
	tent->s.clientNum = ent->s.clientNum;

	// the lance punches through a wall up to RAIL_PIERCE_DEPTH thick
	if ( s_railPierce > 0 && trace.fraction < 1.0f && !( trace.surfaceFlags & SURF_SKY ) ) {
		vec3_t	exitPoint;
		int		k;

		for ( k = 8 ; k <= RAIL_PIERCE_DEPTH ; k += 8 ) {
			VectorMA( trace.endpos, k, forward, exitPoint );
			if ( !( trap_PointContents( exitPoint, -1 ) & CONTENTS_SOLID ) ) {
				s_railPierce--;
				VectorCopy( exitPoint, muzzle );
				s_railDepth++;
				weapon_railgun_fire( ent );
				s_railDepth--;
				return;
			}
		}
	}
	// overcharged slugs ricochet off the world
	if ( s_railBounces > 0 && trace.fraction < 1.0f && !( trace.surfaceFlags & ( SURF_NOIMPACT | SURF_SKY ) ) ) {
		float	dot;

		s_railBounces--;
		dot = DotProduct( forward, trace.plane.normal );
		VectorMA( forward, -2 * dot, trace.plane.normal, forward );
		VectorNormalize( forward );
		VectorMA( trace.endpos, 2, trace.plane.normal, muzzle );
		PerpendicularVector( right, forward );
		CrossProduct( forward, right, up );
		s_railDepth++;
		weapon_railgun_fire( ent );
		s_railDepth--;
		return;
	}

	// give the shooter a reward sound if they have made two railgun hits in a row
	if ( hits == 0 ) {
		// complete miss
		ent->client->accurateCount = 0;
	} else {
		// check for "impressive" reward sound
		ent->client->accurateCount += hits;
		if ( ent->client->accurateCount >= 2 ) {
			ent->client->accurateCount -= 2;
			ent->client->ps.persistant[PERS_IMPRESSIVE_COUNT]++;
			// add the sprite over the player's head
			ent->client->ps.eFlags &= ~(EF_AWARD_IMPRESSIVE | EF_AWARD_EXCELLENT | EF_AWARD_GAUNTLET | EF_AWARD_ASSIST | EF_AWARD_DEFEND | EF_AWARD_CAP );
			ent->client->ps.eFlags |= EF_AWARD_IMPRESSIVE;
			ent->client->rewardTime = level.time + REWARD_SPRITE_TIME;
		}
		ent->client->accuracy_hits++;
	}

}

/*
=================
G_RailShot

Descent arsenal: one slug with ricochets / wall pierces (muzzle and forward are set by FireWeapon)
=================
*/
void G_RailShot( gentity_t *ent, int damage, int bounces, int pierce ) {
	s_railDamage = damage;
	s_railBounces = bounces;
	s_railPierce = pierce;
	weapon_railgun_fire( ent );
	s_railDamage = 100;
	s_railBounces = 0;
	s_railPierce = 0;
}


/*
======================================================================

GRAPPLING HOOK

======================================================================
*/

void Weapon_GrapplingHook_Fire (gentity_t *ent)
{
	if (!ent->client->fireHeld && !ent->client->hook)
		fire_grapple (ent, muzzle, forward);

	ent->client->fireHeld = qtrue;
}

void Weapon_HookFree (gentity_t *ent)
{
	ent->parent->client->hook = NULL;
	ent->parent->client->ps.pm_flags &= ~PMF_GRAPPLE_PULL;
	G_FreeEntity( ent );
}

/*
================
Weapon_OffhandHook

Fire the grapple from the eyes without switching weapons
================
*/
#define OFFHAND_HOOK_SPEED	2000

void Weapon_OffhandHook( gentity_t *ent ) {
	vec3_t	fwd, rt, u, start;
	gentity_t	*hook;

	if ( ent->client->hook ) {
		return;
	}
	AngleVectors( ent->client->ps.viewangles, fwd, rt, u );
	CalcMuzzlePointOrigin( ent, ent->client->oldOrigin, fwd, rt, u, start );
	hook = fire_grapple( ent, start, fwd );
	VectorScale( fwd, OFFHAND_HOOK_SPEED, hook->s.pos.trDelta );
	SnapVector( hook->s.pos.trDelta );
	G_AddEvent( ent, EV_FIRE_HOOK, 0 );
	if ( trap_Cvar_VariableIntegerValue( "g_debugAlt" ) ) {
		G_LogPrintf( "Move: %i EV_FIRE_HOOK\n", ent->s.number );
	}
}

void Weapon_HookThink (gentity_t *ent)
{
	if (ent->enemy) {
		vec3_t v, oldorigin;

		VectorCopy(ent->r.currentOrigin, oldorigin);
		v[0] = ent->enemy->r.currentOrigin[0] + (ent->enemy->r.mins[0] + ent->enemy->r.maxs[0]) * 0.5;
		v[1] = ent->enemy->r.currentOrigin[1] + (ent->enemy->r.mins[1] + ent->enemy->r.maxs[1]) * 0.5;
		v[2] = ent->enemy->r.currentOrigin[2] + (ent->enemy->r.mins[2] + ent->enemy->r.maxs[2]) * 0.5;
		SnapVectorTowards( v, oldorigin );	// save net bandwidth

		G_SetOrigin( ent, v );
	}

	VectorCopy( ent->r.currentOrigin, ent->parent->client->ps.grapplePoint);
}

/*
======================================================================

LIGHTNING GUN

======================================================================
*/

void Weapon_LightningFire( gentity_t *ent ) {
	trace_t		tr;
	vec3_t		end;
#ifdef MISSIONPACK
	vec3_t impactpoint, bouncedir;
#endif
	gentity_t	*traceEnt, *tent;
	int			damage, i, passent;

	damage = 8 * s_quadFactor;

	passent = ent->s.number;
	for (i = 0; i < 10; i++) {
		VectorMA( muzzle, LIGHTNING_RANGE, forward, end );

		trap_Trace( &tr, muzzle, NULL, NULL, end, passent, MASK_SHOT );

#ifdef MISSIONPACK
		// if not the first trace (the lightning bounced of an invulnerability sphere)
		if (i) {
			// add bounced off lightning bolt temp entity
			// the first lightning bolt is a cgame only visual
			//
			tent = G_TempEntity( muzzle, EV_LIGHTNINGBOLT );
			VectorCopy( tr.endpos, end );
			SnapVector( end );
			VectorCopy( end, tent->s.origin2 );
		}
#endif
		if ( tr.entityNum == ENTITYNUM_NONE ) {
			return;
		}

		traceEnt = &g_entities[ tr.entityNum ];

		if ( traceEnt->takedamage) {
#ifdef MISSIONPACK
			if ( traceEnt->client && traceEnt->client->invulnerabilityTime > level.time ) {
				if (G_InvulnerabilityEffect( traceEnt, forward, tr.endpos, impactpoint, bouncedir )) {
					G_BounceProjectile( muzzle, impactpoint, bouncedir, end );
					VectorCopy( impactpoint, muzzle );
					VectorSubtract( end, impactpoint, forward );
					VectorNormalize(forward);
					// the player can hit him/herself with the bounced lightning
					passent = ENTITYNUM_NONE;
				}
				else {
					VectorCopy( tr.endpos, muzzle );
					passent = traceEnt->s.number;
				}
				continue;
			}
#endif
			if( LogAccuracyHit( traceEnt, ent ) ) {
				ent->client->accuracy_hits++;
			}
			G_Damage( traceEnt, ent, ent, forward, tr.endpos, damage, 0, MOD_LIGHTNING);
		}

		if ( traceEnt->takedamage && traceEnt->client ) {
			tent = G_TempEntity( tr.endpos, EV_MISSILE_HIT );
			tent->s.otherEntityNum = traceEnt->s.number;
			tent->s.eventParm = DirToByte( tr.plane.normal );
			tent->s.weapon = ent->s.weapon;
		} else if ( !( tr.surfaceFlags & SURF_NOIMPACT ) ) {
			tent = G_TempEntity( tr.endpos, EV_MISSILE_MISS );
			tent->s.eventParm = DirToByte( tr.plane.normal );
		}

		break;
	}
}

#if defined(MISSIONPACK) || defined(TA_WEAPONS)
/*
======================================================================

NAILGUN

======================================================================
*/

void Weapon_Nailgun_Fire (gentity_t *ent) {
	gentity_t	*m;
	int			count;

	for( count = 0; count < NUM_NAILSHOTS; count++ ) {
		m = fire_nail (ent, muzzle, forward, right, up );
		m->damage *= s_quadFactor;
		m->splashDamage *= s_quadFactor;
	}

//	VectorAdd( m->s.pos.trDelta, ent->client->ps.velocity, m->s.pos.trDelta );	// "real" physics
}


/*
======================================================================

PROXIMITY MINE LAUNCHER

======================================================================
*/

void weapon_proxlauncher_fire (gentity_t *ent) {
	gentity_t	*m;

	// extra vertical velocity
	forward[2] += 0.2f;
	VectorNormalize( forward );

	m = fire_prox (ent, muzzle, forward);
	m->damage *= s_quadFactor;
	m->splashDamage *= s_quadFactor;

//	VectorAdd( m->s.pos.trDelta, ent->client->ps.velocity, m->s.pos.trDelta );	// "real" physics
}

#endif

//======================================================================


/*
===============
LogAccuracyHit
===============
*/
qboolean LogAccuracyHit( gentity_t *target, gentity_t *attacker ) {
	if( !target->takedamage ) {
		return qfalse;
	}

	if ( target == attacker ) {
		return qfalse;
	}

	if( !target->client ) {
		return qfalse;
	}

	if( !attacker->client ) {
		return qfalse;
	}

	if( target->client->ps.stats[STAT_HEALTH] <= 0 ) {
		return qfalse;
	}

	if ( OnSameTeam( target, attacker ) ) {
		return qfalse;
	}

	return qtrue;
}


/*
===============
CalcMuzzlePoint

set muzzle location relative to pivoting eye
===============
*/
void CalcMuzzlePoint ( gentity_t *ent, vec3_t localForward, vec3_t localRight, vec3_t localUp, vec3_t muzzlePoint ) {
	VectorCopy( ent->s.pos.trBase, muzzlePoint );
	muzzlePoint[2] += ent->client->ps.viewheight;
	VectorMA( muzzlePoint, 14, localForward, muzzlePoint );
	// snap to integer coordinates for more efficient network bandwidth usage
	SnapVector( muzzlePoint );
}

/*
===============
CalcMuzzlePointOrigin

set muzzle location relative to pivoting eye
===============
*/
void CalcMuzzlePointOrigin ( gentity_t *ent, vec3_t origin, vec3_t localForward, vec3_t localRight, vec3_t localUp, vec3_t muzzlePoint ) {
	VectorCopy( ent->s.pos.trBase, muzzlePoint );
	muzzlePoint[2] += ent->client->ps.viewheight;
	VectorMA( muzzlePoint, 14, localForward, muzzlePoint );
	// snap to integer coordinates for more efficient network bandwidth usage
	SnapVector( muzzlePoint );
}



/*
======================================================================

ALT-FIRE

======================================================================
*/

#define MG_BURST_SPREAD		40
#define MG_BURST_DAMAGE		8
#define SLUG_DAMAGE			55
#define CHAINGUN_ALT_SPREAD	120
#define CHAINGUN_ALT_DAMAGE	9
#define RAIL_OVERCHARGE_DAMAGE	150
#define ARC_RANGE			768
#define ARC_DAMAGE			30
#define ARC_JUMP_RANGE		350
#define ARC_JUMPS			2
#define STAKE_SPEED			2400
#define STAKE_DAMAGE		45

void Bullet_Fire (gentity_t *ent, float spread, int damage, int mod );

/*
===============
Weapon_ArcTarget

Nearest living client within ARC_JUMP_RANGE of org, visible from it, not yet hit
===============
*/
static gentity_t *Weapon_ArcTarget( gentity_t *attacker, vec3_t org, gentity_t **hit, int numHit ) {
	gentity_t	*best, *other;
	trace_t		tr;
	vec3_t		center;
	float		dist, bestDist;
	int			i, j;

	best = NULL;
	bestDist = ARC_JUMP_RANGE;
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		other = &g_entities[i];
		if ( !other->inuse || !other->client || other->health <= 0 || other == attacker
			|| other->client->sess.sessionTeam == TEAM_SPECTATOR || OnSameTeam( other, attacker ) ) {
			continue;
		}
		for ( j = 0 ; j < numHit ; j++ ) {
			if ( hit[j] == other ) {
				break;
			}
		}
		if ( j < numHit ) {
			continue;
		}
		VectorCopy( other->r.currentOrigin, center );
		dist = Distance( org, center );
		if ( dist >= bestDist ) {
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

static void Weapon_ArcBolt( vec3_t start, vec3_t end ) {
	gentity_t	*tent;

	tent = G_TempEntity( start, EV_ARC );
	VectorCopy( end, tent->s.origin2 );
	SnapVector( tent->s.origin2 );
}

/*
===============
Weapon_LightningArc

Lightning alt-fire: a bolt that jumps from the target to nearby enemies
===============
*/
static void Weapon_LightningArc( gentity_t *ent ) {
	gentity_t	*hit[ARC_JUMPS + 1];
	gentity_t	*target, *tent;
	trace_t		tr;
	vec3_t		end, from, to, dir;
	int			numHit, damage;

	VectorMA( muzzle, ARC_RANGE, forward, end );
	trap_Trace( &tr, muzzle, NULL, NULL, end, ent->s.number, MASK_SHOT );
	Weapon_ArcBolt( muzzle, tr.endpos );

	if ( tr.entityNum == ENTITYNUM_NONE ) {
		return;
	}
	target = &g_entities[tr.entityNum];
	if ( !target->takedamage ) {
		if ( !( tr.surfaceFlags & SURF_NOIMPACT ) ) {
			tent = G_TempEntity( tr.endpos, EV_MISSILE_MISS );
			tent->s.eventParm = DirToByte( tr.plane.normal );
			tent->s.weapon = WP_LIGHTNING;
		}
		return;
	}

	damage = ARC_DAMAGE * s_quadFactor;
	numHit = 0;
	VectorCopy( tr.endpos, to );
	while ( target ) {
		if ( LogAccuracyHit( target, ent ) ) {
			ent->client->accuracy_hits++;
		}
		VectorSubtract( to, muzzle, dir );
		G_Damage( target, ent, ent, dir, to, damage, 0, MOD_LIGHTNING );
		hit[numHit++] = target;
		if ( numHit > ARC_JUMPS || !target->client ) {
			break;
		}
		VectorCopy( to, from );
		target = Weapon_ArcTarget( ent, from, hit, numHit );
		if ( target ) {
			VectorCopy( target->r.currentOrigin, to );
			Weapon_ArcBolt( from, to );
			damage = damage * 2 / 3;
		}
	}
}

/*
===============
FireWeaponAlt
===============
*/
void FireWeaponAlt( gentity_t *ent ) {
	gentity_t	*m;
	vec3_t		angles, dir;
	int			i;

	s_quadFactor = ent->client->ps.powerups[PW_QUAD] ? g_quadfactor.value : 1;

	AngleVectors( ent->client->ps.viewangles, forward, right, up );
	CalcMuzzlePointOrigin( ent, ent->client->oldOrigin, forward, right, up, muzzle );

	switch ( ent->s.weapon ) {
	case WP_GAUNTLET:
		ent->client->lungeTime = level.time;
		ent->client->lungeCharge = 0;
		break;
	case WP_MACHINEGUN:
		for ( i = 0 ; i < 3 ; i++ ) {
			Bullet_Fire( ent, MG_BURST_SPREAD, MG_BURST_DAMAGE, MOD_MACHINEGUN );
		}
		ent->client->accuracy_shots += 3;
		break;
	case WP_SHOTGUN:
		G_AirBlast( ent, muzzle, forward, 0 );
		break;
	case WP_GRENADE_LAUNCHER:
		forward[2] += 0.2f;
		VectorNormalize( forward );
		m = fire_grenade( ent, muzzle, forward );
		m->classname = "sticky_grenade";
		m->s.eFlags &= ~EF_BOUNCE_HALF;
		m->s.generic1 |= ALTFIRE_PROJECTILE;
		m->damage *= s_quadFactor;
		m->splashDamage *= s_quadFactor;
		break;
	case WP_LIGHTNING:
		Weapon_LightningArc( ent );
		ent->client->accuracy_shots++;
		break;
	case WP_RAILGUN:
		s_railDamage = RAIL_OVERCHARGE_DAMAGE;
		s_railBounces = G_RearmOn( RS_RICOCHET ) ? 2 : 0;
		weapon_railgun_fire( ent );
		s_railDamage = 100;
		s_railBounces = 0;
		ent->client->accuracy_shots++;
		break;
	case WP_PLASMAGUN:
		// singularity: slow orb that drags players in, then detonates
		m = fire_plasma( ent, muzzle, forward );
		VectorScale( forward, 380, m->s.pos.trDelta );
		SnapVector( m->s.pos.trDelta );
		m->s.generic1 |= ALTFIRE_PROJECTILE;
		m->damage = 60 * s_quadFactor;
		m->splashDamage = 90 * s_quadFactor;
		m->splashRadius = 200;
		m->count = level.time + 1300;
		m->random = 300;		// pull radius
		m->speed = 2400;		// pull strength
		m->think = G_SingularityThink;
		m->nextthink = level.time + 50;
		break;
	case WP_BFG:
		for ( i = -1 ; i <= 1 ; i++ ) {
			vectoangles( forward, angles );
			angles[YAW] += i * 6;
			AngleVectors( angles, dir, NULL, NULL );
			m = fire_bfg( ent, muzzle, dir );
			m->damage *= s_quadFactor;
			m->splashDamage *= s_quadFactor;
		}
		break;
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case WP_NAILGUN:
		m = fire_nail( ent, muzzle, forward, right, up );
		VectorScale( forward, STAKE_SPEED, m->s.pos.trDelta );
		SnapVector( m->s.pos.trDelta );
		m->s.generic1 |= ALTFIRE_PROJECTILE;
		m->damage = STAKE_DAMAGE * s_quadFactor;
		m->count = 4;		// the stake ricochets even more
		if ( !G_RearmOn( RS_RICOCHET ) ) {
			m->s.eFlags &= ~EF_BOUNCE;
		}
		ent->client->accuracy_shots++;
		break;
	case WP_CHAINGUN:
		Bullet_Fire( ent, CHAINGUN_ALT_SPREAD, CHAINGUN_ALT_DAMAGE, MOD_CHAINGUN );
		ent->client->accuracy_shots++;
		break;
#endif
	default:
		break;
	}
}

/*
===============
FireWeaponAltCharged

The held (charged) alt-fire. charge is 1..255 from the event, frac 0..1
===============
*/
void FireWeaponAltCharged( gentity_t *ent, int charge ) {
	gentity_t	*m;
	vec3_t		angles, dir;
	float		frac;
	int			i;

	frac = ALT_CHARGE_FRAC( charge );
	ent->client->chargedShotTime = level.time;
	s_quadFactor = ent->client->ps.powerups[PW_QUAD] ? g_quadfactor.value : 1;
	AngleVectors( ent->client->ps.viewangles, forward, right, up );
	CalcMuzzlePointOrigin( ent, ent->client->oldOrigin, forward, right, up, muzzle );

	switch ( ent->s.weapon ) {
	case WP_GAUNTLET:
		// rocket punch: the lunge itself happened in pmove
		ent->client->lungeTime = level.time;
		ent->client->lungeCharge = frac;
		break;
	case WP_MACHINEGUN:
		// marksman: one dead-accurate heavy round
		Bullet_Fire( ent, 0, 20 + 40 * frac, MOD_MACHINEGUN );
		ent->client->accuracy_shots++;
		break;
	case WP_SHOTGUN:
		G_AirBlast( ent, muzzle, forward, frac );
		break;
	case WP_GRENADE_LAUNCHER:
		// napalm: bursts into a ring of fire
		forward[2] += 0.15f;
		VectorNormalize( forward );
		m = fire_grenade( ent, muzzle, forward );
		m->s.generic1 |= ALTFIRE_PROJECTILE | ALTFIRE_CHARGED;
		m->random = frac;
		m->damage *= s_quadFactor;
		m->splashDamage *= s_quadFactor;
		m->nextthink = level.time + 1400;
		break;
	case WP_ROCKET_LAUNCHER:
		// boulder: slow, heavy, shakes the whole area
		m = fire_rocket( ent, muzzle, forward );
		VectorScale( forward, 520, m->s.pos.trDelta );
		SnapVector( m->s.pos.trDelta );
		m->s.generic1 |= ALTFIRE_PROJECTILE | ALTFIRE_CHARGED;
		m->random = frac;
		m->damage = ( 100 + 60 * frac ) * s_quadFactor;
		m->splashDamage = ( 110 + 60 * frac ) * s_quadFactor;
		m->splashRadius = 150 + 150 * frac;
		break;
	case WP_LIGHTNING:
		G_Thunderstrike( ent, muzzle, forward, frac, s_quadFactor );
		ent->client->accuracy_shots++;
		break;
	case WP_RAILGUN:
		// lance: harder the longer you hold, goes through a wall
		s_railDamage = 100 + 100 * frac;
		s_railPierce = 1;
		weapon_railgun_fire( ent );
		s_railDamage = 100;
		s_railPierce = 0;
		ent->client->accuracy_shots++;
		break;
	case WP_PLASMAGUN:
		// a big whirlpool
		m = fire_plasma( ent, muzzle, forward );
		VectorScale( forward, 340, m->s.pos.trDelta );
		SnapVector( m->s.pos.trDelta );
		m->s.generic1 |= ALTFIRE_PROJECTILE | ALTFIRE_CHARGED;
		m->damage = ( 60 + 40 * frac ) * s_quadFactor;
		m->splashDamage = ( 90 + 60 * frac ) * s_quadFactor;
		m->splashRadius = 200 + 80 * frac;
		m->count = level.time + 1500 + 1300 * frac;
		m->random = 300 + 220 * frac;
		m->speed = 2400 + 1800 * frac;
		m->think = G_SingularityThink;
		m->nextthink = level.time + 50;
		break;
	case WP_BFG:
		// overload: one huge ball with twice the laser reach
		m = fire_bfg( ent, muzzle, forward );
		m->s.generic1 |= ALTFIRE_PROJECTILE | ALTFIRE_CHARGED;
		m->damage = m->damage * ( 2 + frac ) * s_quadFactor;
		m->splashDamage = m->splashDamage * ( 1.5f + frac ) * s_quadFactor;
		m->splashRadius = 180 + 100 * frac;
		break;
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case WP_NAILGUN:
		// pinning spike: fast, heavy, holds the target in place
		m = fire_nail( ent, muzzle, forward, right, up );
		VectorScale( forward, 3400, m->s.pos.trDelta );
		SnapVector( m->s.pos.trDelta );
		m->s.generic1 |= ALTFIRE_PROJECTILE | ALTFIRE_CHARGED;
		m->s.eFlags &= ~EF_BOUNCE;
		m->random = frac;
		m->damage = ( 45 + 45 * frac ) * s_quadFactor;
		ent->client->accuracy_shots++;
		break;
	case WP_PROX_LAUNCHER:
		// minefield: three mines in a fan
		for ( i = -1 ; i <= 1 ; i++ ) {
			vectoangles( forward, angles );
			angles[YAW] += i * 9;
			angles[PITCH] -= 4;
			AngleVectors( angles, dir, NULL, NULL );
			m = fire_prox( ent, muzzle, dir );
			m->damage *= s_quadFactor;
			m->splashDamage *= s_quadFactor;
		}
		break;
#endif
	default:
		break;
	}
}

/*
===============
Weapon_AltTrigger

Remote detonation of the player's rockets / prox mines
===============
*/
void Weapon_AltTrigger( gentity_t *ent ) {
	gentity_t	*m;
	int			i;

	for ( i = MAX_CLIENTS, m = &g_entities[MAX_CLIENTS] ; i < level.num_entities ; i++, m++ ) {
		if ( !m->inuse || m->s.eType != ET_MISSILE || m->r.ownerNum != ent->s.number ) {
			continue;
		}
		if ( ent->s.weapon == WP_ROCKET_LAUNCHER && m->s.weapon == WP_ROCKET_LAUNCHER ) {
			G_ExplodeMissile( m );
		}
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
		// mines stuck to players go off right away
		if ( ent->s.weapon == WP_PROX_LAUNCHER && m->s.weapon == WP_PROX_LAUNCHER && ( m->r.svFlags & SVF_NOCLIENT ) ) {
			m->nextthink = level.time;
			continue;
		}
		if ( ent->s.weapon == WP_PROX_LAUNCHER && m->s.weapon == WP_PROX_LAUNCHER ) {
			if ( m->activator ) {
				G_FreeEntity( m->activator );
				m->activator = NULL;
			}
			G_ExplodeMissile( m );
		}
#endif
	}
}

/*
===============
FireWeapon
===============
*/
void FireWeapon( gentity_t *ent ) {
	if (ent->client->ps.powerups[PW_QUAD] ) {
		s_quadFactor = g_quadfactor.value;
	} else {
		s_quadFactor = 1;
	}
#ifdef MISSIONPACK
	if( ent->client->persistantPowerup && ent->client->persistantPowerup->item && ent->client->persistantPowerup->item->giTag == PW_DOUBLER ) {
		s_quadFactor *= 2;
	}
#endif

	// track shots taken for accuracy tracking.  Grapple is not a weapon and gauntet is just not tracked
	if( ent->s.weapon != WP_GRAPPLING_HOOK && ent->s.weapon != WP_GAUNTLET ) {
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
		if( ent->s.weapon == WP_NAILGUN ) {
			ent->client->accuracy_shots += NUM_NAILSHOTS;
		} else {
			ent->client->accuracy_shots++;
		}
#else
		ent->client->accuracy_shots++;
#endif
	}

	// set aiming directions
	AngleVectors (ent->client->ps.viewangles, forward, right, up);

	CalcMuzzlePointOrigin ( ent, ent->client->oldOrigin, forward, right, up, muzzle );

	G_DS_SkillOnFire( ent );		// SKILLS: free shots (DESCENT)

	// descent: levelled / evolved weapons fire from g_ds_arsenal.c (qfalse outside DESCENT)
	if ( G_DS_FireWeapon( ent, muzzle, forward, right, up ) ) {
		return;
	}

	// fire the specific weapon
	switch( ent->s.weapon ) {
	case WP_GAUNTLET:
		Weapon_Gauntlet( ent );
		break;
	case WP_LIGHTNING:
		Weapon_LightningFire( ent );
		break;
	case WP_SHOTGUN:
		weapon_supershotgun_fire( ent );
		break;
	case WP_MACHINEGUN:
		if ( g_gametype.integer != GT_TEAM ) {
			Bullet_Fire( ent, MACHINEGUN_SPREAD, MACHINEGUN_DAMAGE, MOD_MACHINEGUN );
		} else {
			Bullet_Fire( ent, MACHINEGUN_SPREAD, MACHINEGUN_TEAM_DAMAGE, MOD_MACHINEGUN );
		}
		break;
	case WP_GRENADE_LAUNCHER:
		weapon_grenadelauncher_fire( ent );
		break;
	case WP_ROCKET_LAUNCHER:
		Weapon_RocketLauncher_Fire( ent );
		break;
	case WP_PLASMAGUN:
		Weapon_Plasmagun_Fire( ent );
		break;
	case WP_RAILGUN:
		weapon_railgun_fire( ent );
		break;
	case WP_BFG:
		BFG_Fire( ent );
		break;
	case WP_GRAPPLING_HOOK:
		Weapon_GrapplingHook_Fire( ent );
		break;
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case WP_NAILGUN:
		Weapon_Nailgun_Fire( ent );
		break;
	case WP_PROX_LAUNCHER:
		weapon_proxlauncher_fire( ent );
		break;
	case WP_CHAINGUN:
		Bullet_Fire( ent, CHAINGUN_SPREAD, CHAINGUN_DAMAGE, MOD_CHAINGUN );
		break;
#endif
	default:
// FIXME		G_Error( "Bad ent->s.weapon" );
		break;
	}
}


#ifdef MISSIONPACK

/*
===============
KamikazeRadiusDamage
===============
*/
static void KamikazeRadiusDamage( vec3_t origin, gentity_t *attacker, float damage, float radius ) {
	float		dist;
	gentity_t	*ent;
	int			entityList[MAX_GENTITIES];
	int			numListedEntities;
	vec3_t		mins, maxs;
	vec3_t		v;
	vec3_t		dir;
	int			i, e;

	if ( radius < 1 ) {
		radius = 1;
	}

	for ( i = 0 ; i < 3 ; i++ ) {
		mins[i] = origin[i] - radius;
		maxs[i] = origin[i] + radius;
	}

	numListedEntities = trap_EntitiesInBox( mins, maxs, entityList, MAX_GENTITIES );

	for ( e = 0 ; e < numListedEntities ; e++ ) {
		ent = &g_entities[entityList[ e ]];

		if (!ent->takedamage) {
			continue;
		}

		// don't hit things we have already hit
		if( ent->kamikazeTime > level.time ) {
			continue;
		}

		// find the distance from the edge of the bounding box
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

//		if( CanDamage (ent, origin) ) {
			VectorSubtract (ent->r.currentOrigin, origin, dir);
			// push the center of mass higher than the origin so players
			// get knocked into the air more
			dir[2] += 24;
			G_Damage( ent, NULL, attacker, dir, origin, damage, DAMAGE_RADIUS|DAMAGE_NO_TEAM_PROTECTION, MOD_KAMIKAZE );
			ent->kamikazeTime = level.time + 3000;
//		}
	}
}

/*
===============
KamikazeShockWave
===============
*/
static void KamikazeShockWave( vec3_t origin, gentity_t *attacker, float damage, float push, float radius ) {
	float		dist;
	gentity_t	*ent;
	int			entityList[MAX_GENTITIES];
	int			numListedEntities;
	vec3_t		mins, maxs;
	vec3_t		v;
	vec3_t		dir;
	int			i, e;

	if ( radius < 1 )
		radius = 1;

	for ( i = 0 ; i < 3 ; i++ ) {
		mins[i] = origin[i] - radius;
		maxs[i] = origin[i] + radius;
	}

	numListedEntities = trap_EntitiesInBox( mins, maxs, entityList, MAX_GENTITIES );

	for ( e = 0 ; e < numListedEntities ; e++ ) {
		ent = &g_entities[entityList[ e ]];

		// don't hit things we have already hit
		if( ent->kamikazeShockTime > level.time ) {
			continue;
		}

		// find the distance from the edge of the bounding box
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

//		if( CanDamage (ent, origin) ) {
			VectorSubtract (ent->r.currentOrigin, origin, dir);
			dir[2] += 24;
			G_Damage( ent, NULL, attacker, dir, origin, damage, DAMAGE_RADIUS|DAMAGE_NO_TEAM_PROTECTION, MOD_KAMIKAZE );
			//
			dir[2] = 0;
			VectorNormalize(dir);
			if ( ent->client ) {
				ent->client->ps.velocity[0] = dir[0] * push;
				ent->client->ps.velocity[1] = dir[1] * push;
				ent->client->ps.velocity[2] = 100;
			}
			ent->kamikazeShockTime = level.time + 3000;
//		}
	}
}

/*
===============
KamikazeDamage
===============
*/
static void KamikazeDamage( gentity_t *self ) {
	int i;
	float t;
	gentity_t *ent;
	vec3_t newangles;

	self->count += 100;

	if (self->count >= KAMI_SHOCKWAVE_STARTTIME) {
		// shockwave push back
		t = self->count - KAMI_SHOCKWAVE_STARTTIME;
		KamikazeShockWave(self->s.pos.trBase, self->activator, 25, 400,	(int) (float) t * KAMI_SHOCKWAVE_MAXRADIUS / (KAMI_SHOCKWAVE_ENDTIME - KAMI_SHOCKWAVE_STARTTIME) );
	}
	//
	if (self->count >= KAMI_EXPLODE_STARTTIME) {
		// do our damage
		t = self->count - KAMI_EXPLODE_STARTTIME;
		KamikazeRadiusDamage( self->s.pos.trBase, self->activator, 400,	(int) (float) t * KAMI_BOOMSPHERE_MAXRADIUS / (KAMI_IMPLODE_STARTTIME - KAMI_EXPLODE_STARTTIME) );
	}

	// either cycle or kill self
	if( self->count >= KAMI_SHOCKWAVE_ENDTIME ) {
		G_FreeEntity( self );
		return;
	}
	self->nextthink = level.time + 100;

	// add earth quake effect
	newangles[0] = crandom() * 2;
	newangles[1] = crandom() * 2;
	newangles[2] = 0;
	for (i = 0; i < MAX_CLIENTS; i++)
	{
		ent = &g_entities[i];
		if (!ent->inuse)
			continue;
		if (!ent->client)
			continue;

		if (ent->client->ps.groundEntityNum != ENTITYNUM_NONE) {
			ent->client->ps.velocity[0] += crandom() * 120;
			ent->client->ps.velocity[1] += crandom() * 120;
			ent->client->ps.velocity[2] = 30 + random() * 25;
		}

		ent->client->ps.delta_angles[0] += ANGLE2SHORT(newangles[0] - self->movedir[0]);
		ent->client->ps.delta_angles[1] += ANGLE2SHORT(newangles[1] - self->movedir[1]);
		ent->client->ps.delta_angles[2] += ANGLE2SHORT(newangles[2] - self->movedir[2]);
	}
	VectorCopy(newangles, self->movedir);
}

/*
===============
G_StartKamikaze
===============
*/
void G_StartKamikaze( gentity_t *ent ) {
	gentity_t	*explosion;
	gentity_t	*te;
	vec3_t		snapped;

	// start up the explosion logic
	explosion = G_Spawn();

	explosion->s.eType = ET_EVENTS + EV_KAMIKAZE;
	explosion->eventTime = level.time;

	if ( ent->client ) {
		VectorCopy( ent->s.pos.trBase, snapped );
	}
	else {
		VectorCopy( ent->activator->s.pos.trBase, snapped );
	}
	SnapVector( snapped );		// save network bandwidth
	G_SetOrigin( explosion, snapped );

	explosion->classname = "kamikaze";
	explosion->s.pos.trType = TR_STATIONARY;

	explosion->kamikazeTime = level.time;

	explosion->think = KamikazeDamage;
	explosion->nextthink = level.time + 100;
	explosion->count = 0;
	VectorClear(explosion->movedir);

	trap_LinkEntity( explosion );

	if (ent->client) {
		//
		explosion->activator = ent;
		//
		ent->s.eFlags &= ~EF_KAMIKAZE;
		// nuke the guy that used it
		G_Damage( ent, ent, ent, NULL, NULL, 100000, DAMAGE_NO_PROTECTION, MOD_KAMIKAZE );
	}
	else {
		if ( !strcmp(ent->activator->classname, "bodyque") ) {
			explosion->activator = &g_entities[ent->activator->r.ownerNum];
		}
		else {
			explosion->activator = ent->activator;
		}
	}

	// play global sound at all clients
	te = G_TempEntity(snapped, EV_GLOBAL_TEAM_SOUND );
	te->r.svFlags |= SVF_BROADCAST;
	te->s.eventParm = GTS_KAMIKAZE;
}
#endif
