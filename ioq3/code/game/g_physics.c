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
// g_physics.c -- rearmed: physics props (barrels, crates, gas tanks), corpse physics, wall splats
//
// Props are ET_GENERAL entities (s.generic1 RM_ENT_PROP, s.modelindex2 propType_t) with their own
// small rigid body mover: gravity, bounce, friction, rolling, tumbling, buoyancy. While a prop moves
// it thinks every server frame and sends TR_INTERPOLATE positions (the cgame lerps between
// snapshots); at rest it becomes a TR_STATIONARY physicsObject (movers can push it) and stops
// thinking. Per-entity state lives in props[] / bodies[] indexed by entity number.
//
// Corpses: dead clients take knockback (G_Damage skips them), bounce off walls and splat;
// body-queue corpses get the same mover as props until they come to rest and sink as usual.

#include "g_local.h"

void BodySink( gentity_t *ent );		// g_client.c

#define PROP_MAX_TOTAL		80			// every prop on the map, extras included
#define PROP_MAX_ANCHORS	256
#define PROP_MAX_QUEUE		64
#define PROP_STOP_SPEED		14
#define PROP_MAX_SPEED		1700
#define PROP_HIT_SPEED		320			// a flying prop hurts players above this impact speed
#define PROP_CREDIT_TIME	20000		// a launcher keeps the credit this long
#define PROP_MANAGER_MSEC	200
#define PROP_AIR_LIMIT		7000		// in flight this long: fell out of the world

#define TANK_THRUST			1500
#define TANK_MAX_SPEED		1050

#define SPLAT_SPEED			600			// wall impact speed for a splat
#define SPLAT_KNOCK_MSEC	1600		// a living victim must have been knocked by someone this recently
#define CORPSE_KNOCK_SCALE	1.5f		// corpses fly further than the living
#define CORPSE_BOUNCE		0.35f

typedef struct {
	const char	*classname;
	vec3_t		mins, maxs;				// upright box (origin at the bottom centre)
	vec3_t		lmins, lmaxs;			// lying box (cylinders)
	int			health;
	float		mass;
	float		bounce;					// restitution
	float		friction;				// sliding deceleration, u/s^2
	float		rollFriction;			// rolling deceleration when lying
	qboolean	cylinder;
} propInfo_t;

static const propInfo_t propInfo[PROP_NUM_TYPES] = {
	{ "prop_barrel", { -14, -14, 0 }, { 14, 14, 40 }, { -16, -16, 0 }, { 16, 16, 28 }, 40, 120, 0.30f, 520, 110, qtrue },
	{ "prop_crate",  { -16, -16, 0 }, { 16, 16, 32 }, { -16, -16, 0 }, { 16, 16, 32 }, 25,  90, 0.22f, 760, 760, qfalse },
	{ "prop_tank",   { -12, -12, 0 }, { 12, 12, 44 }, { -14, -14, 0 }, { 14, 14, 24 }, 20,  70, 0.42f, 480, 90, qtrue }
};

typedef struct {
	qboolean	active;
	int			type;
	qboolean	extra;					// Barrel Bonanza drop: removed when the mutator ends
	qboolean	moving;
	qboolean	onGround;
	qboolean	lying;
	qboolean	ruptured;
	qboolean	burning;
	qboolean	dying;					// fuse lit, will be destroyed at fuseTime
	qboolean	chain;					// set off by another prop's blast
	vec3_t		vel;
	vec3_t		avel;					// tumble, deg/s
	vec3_t		ang;
	vec3_t		groundNormal;
	int			groundEnt;
	int			lastThink;
	int			fuseTime;
	int			launcher;				// client who last hit / kicked it, -1 = nobody
	int			launchTime;
	int			airTime;				// when it left the ground
	int			hitEventTime;
	int			kickTime;
	int			hurtClient, hurtTime;	// last player it hit
	int			stuckTime;
} prop_t;

typedef struct {
	qboolean	active;					// a body-queue corpse moving under our mover
	vec3_t		vel;
	int			lastThink;
	int			airTime;
	qboolean	onGround;
} physBody_t;

typedef struct {
	int			type;
	int			time;
	qboolean	extra;
} propQueue_t;

static prop_t		props[MAX_GENTITIES];
static physBody_t	bodies[MAX_GENTITIES];

static vec3_t		anchors[PROP_MAX_ANCHORS];		// spots props are placed around (items, spawns)
static int			numAnchors;
static vec3_t		avoids[PROP_MAX_ANCHORS];		// spawn points, teleport and jump pad targets
static int			numAvoids;

static propQueue_t	queue[PROP_MAX_QUEUE];
static int			numQueued;

static gentity_t	*manager;
static qboolean		spawnedInitial;
static qboolean		propsWereOn;

// clients
static vec3_t		preVel[MAX_CLIENTS];			// velocity before this frame's pmove
static int			knocker[MAX_CLIENTS];
static int			knockTime[MAX_CLIENTS];
static int			splatTime[MAX_GENTITIES];

static int			burnLoopSound, gasLoopSound;

static vmCvar_t		g_propCount;
static vmCvar_t		g_propRespawn;
static vmCvar_t		g_propTest;

static void G_PropThink( gentity_t *ent );
static void G_PropDestroy( gentity_t *ent );
static void G_PropPain( gentity_t *self, gentity_t *attacker, int damage );
static void G_PropDie( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int mod );
static void G_PropTouch( gentity_t *self, gentity_t *other, trace_t *trace );

/*
===============================================================================

HELPERS

===============================================================================
*/

static qboolean G_PropsOn( void ) {
	return G_RearmOn( RS_PROPS );
}

static qboolean G_CorpsesOn( void ) {
	return G_RearmOn( RS_CORPSEPHYSICS );
}

// the entity is still one of our props (a door can delete one behind our back)
static qboolean G_IsProp( int n ) {
	gentity_t	*e = &g_entities[n];

	if ( !props[n].active ) {
		return qfalse;
	}
	if ( !e->inuse || e->s.eType != ET_GENERAL || e->s.generic1 != RM_ENT_PROP ) {
		props[n].active = qfalse;
		return qfalse;
	}
	return qtrue;
}

static float G_PhysGravity( void ) {
	return g_gravity.value * G_RearmGravityScale();
}

static gentity_t *G_PropCredit( prop_t *p ) {
	gentity_t	*who;

	if ( p->launcher >= 0 && p->launcher < MAX_CLIENTS && level.time - p->launchTime < PROP_CREDIT_TIME ) {
		who = &g_entities[p->launcher];
		if ( who->inuse && who->client && who->client->pers.connected == CON_CONNECTED ) {
			return who;
		}
	}
	return &g_entities[ENTITYNUM_WORLD];
}

static void G_PropSetLauncher( prop_t *p, gentity_t *who ) {
	if ( who && who->client ) {
		p->launcher = who->s.number;
		p->launchTime = level.time;
	}
}

static void G_PropCenter( gentity_t *ent, vec3_t out ) {
	VectorCopy( ent->r.currentOrigin, out );
	out[2] += ( ent->r.maxs[2] + ent->r.mins[2] ) * 0.5f;
}

// leaves the ground now: the "fell out of the world" clock (PROP_AIR_LIMIT) starts here.
// Every place that knocks a prop off the ground must come through here: a stale airTime
// (0 for a prop resting since it spawned) made a just-shot gas tank count as falling for
// minutes, and the first think removed it without a trace.
static void G_PropLeaveGround( prop_t *p ) {
	if ( p->onGround ) {
		p->airTime = level.time;
	}
	p->onGround = qfalse;
}

static void G_PropStateBits( gentity_t *ent, prop_t *p ) {
	int		f = 0;

	if ( p->burning ) {
		f |= PROPF_BURNING;
	}
	if ( p->ruptured ) {
		f |= PROPF_RUPTURED;
	}
	if ( p->lying ) {
		f |= PROPF_LYING;
	}
	if ( p->dying && !p->burning && !p->ruptured ) {
		f |= PROPF_FUSE;
	}
	if ( p->moving && !p->onGround ) {
		f |= PROPF_AIRBORNE;
	}
	ent->s.frame = f;
	ent->s.time2 = ( f & PROPF_DANGER ) ? p->fuseTime : 0;
	if ( p->ruptured ) {
		ent->s.loopSound = gasLoopSound;
	} else if ( p->burning ) {
		ent->s.loopSound = burnLoopSound;
	} else {
		ent->s.loopSound = 0;
	}
}

/*
=============
G_PropWake

Start simulating a prop (it may be resting)
=============
*/
static void G_PropWake( gentity_t *ent ) {
	prop_t	*p = &props[ent->s.number];

	if ( !p->moving ) {
		p->moving = qtrue;
		p->lastThink = level.time;
		p->airTime = level.time;		// it was resting: any flight starts now
		ent->physicsObject = qfalse;
		ent->s.pos.trType = TR_INTERPOLATE;
		ent->s.apos.trType = TR_INTERPOLATE;
		VectorCopy( ent->r.currentOrigin, ent->s.pos.trBase );
		VectorCopy( p->ang, ent->s.apos.trBase );
	}
	ent->think = G_PropThink;
	ent->nextthink = level.time + 1;
}

/*
=============
G_PropWakeRiders

Props resting on top of ent start moving (fall) too
=============
*/
static void G_PropWakeRiders( gentity_t *ent ) {
	int		i;

	for ( i = MAX_CLIENTS ; i < level.num_entities ; i++ ) {
		if ( G_IsProp( i ) && !props[i].moving && g_entities[i].s.groundEntityNum == ent->s.number ) {
			G_PropLeaveGround( &props[i] );
			G_PropWake( &g_entities[i] );
		}
	}
}

static void G_PropLightFuse( gentity_t *ent, int msec ) {
	prop_t	*p = &props[ent->s.number];

	if ( p->dying && p->fuseTime <= level.time + msec ) {
		return;		// already going off sooner
	}
	p->dying = qtrue;
	p->fuseTime = level.time + msec;
	G_PropStateBits( ent, p );
	if ( !p->moving ) {
		ent->think = G_PropThink;
		ent->nextthink = p->fuseTime;
	}
}

static void G_PropIgnite( gentity_t *ent, gentity_t *by ) {
	prop_t	*p = &props[ent->s.number];

	if ( !p->active || p->burning || p->ruptured ) {
		return;
	}
	G_PropSetLauncher( p, by );
	if ( p->type == PROP_TANK ) {
		// heat bursts the valve
		p->ruptured = qtrue;
		G_PropLightFuse( ent, 1300 + rand() % 700 );
		G_PropLeaveGround( p );
		p->ang[PITCH] = 115 + rand() % 50;		// bottom up: it takes off
		p->ang[ROLL] = crandom() * 20;
		VectorSet( p->avel, crandom() * 120, ( rand() & 1 ) ? 650 : -650, crandom() * 120 );
		p->vel[2] += 380;
		G_AddEvent( ent, EV_PROP_HIT, 255 );
		G_PropWake( ent );
		G_PropStateBits( ent, p );
		return;
	}
	p->burning = qtrue;
	G_PropLightFuse( ent, ( p->type == PROP_CRATE ? 2800 : 1700 ) + rand() % 900 );
	G_PropStateBits( ent, p );
}

/*
=============
G_PropRupture

A shot gas tank vents and flies off like a rocket
=============
*/
static void G_PropRupture( gentity_t *ent, gentity_t *by ) {
	G_PropIgnite( ent, by );
}

/*
===============================================================================

PLACEMENT

===============================================================================
*/

static qboolean G_PropNearAvoid( const vec3_t spot, float dist ) {
	int		i;
	vec3_t	d;

	for ( i = 0 ; i < numAvoids ; i++ ) {
		VectorSubtract( avoids[i], spot, d );
		if ( fabs( d[2] ) < 96 && d[0] * d[0] + d[1] * d[1] < dist * dist ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
=============
G_PropFloorSpot

Try to find a flat, clear floor spot for a prop near 'near'. Returns qtrue and the floor point.
=============
*/
static qboolean G_PropCheckSpot( int type, vec3_t floor ) {
	const propInfo_t	*pi = &propInfo[type];
	vec3_t		mins, maxs, start, end;
	trace_t		tr;
	int			touch[16], n, i, c;
	gentity_t	*hit;

	// the box itself, lifted a unit
	VectorCopy( floor, start );
	start[2] += 1;
	trap_Trace( &tr, start, pi->mins, pi->maxs, start, ENTITYNUM_NONE, MASK_PLAYERSOLID );
	if ( tr.startsolid || tr.allsolid ) {
		return qfalse;
	}
	// breathing room around it, and headroom
	VectorSet( mins, pi->mins[0] - 10, pi->mins[1] - 10, 4 );
	VectorSet( maxs, pi->maxs[0] + 10, pi->maxs[1] + 10, pi->maxs[2] + 24 );
	trap_Trace( &tr, start, mins, maxs, start, ENTITYNUM_NONE, MASK_PLAYERSOLID );
	if ( tr.startsolid || tr.allsolid ) {
		return qfalse;
	}
	// no ledges: floor under every corner
	for ( i = 0 ; i < 4 ; i++ ) {
		VectorCopy( floor, start );
		start[0] += ( i & 1 ) ? pi->maxs[0] + 4 : pi->mins[0] - 4;
		start[1] += ( i & 2 ) ? pi->maxs[1] + 4 : pi->mins[1] - 4;
		start[2] += 4;
		VectorCopy( start, end );
		end[2] -= 14;
		trap_Trace( &tr, start, NULL, NULL, end, ENTITYNUM_NONE, MASK_PLAYERSOLID );
		if ( tr.fraction == 1.0f || tr.startsolid ) {
			return qfalse;
		}
	}
	// no liquids, no death zones
	VectorCopy( floor, start );
	start[2] += 12;
	c = trap_PointContents( start, -1 );
	if ( c & ( MASK_WATER | CONTENTS_NODROP | CONTENTS_TRIGGER ) ) {
		return qfalse;
	}
	// nothing else here: triggers (teleporters, jump pads, hurt), movers, items, other props, players
	VectorSet( mins, floor[0] + pi->mins[0] - 40, floor[1] + pi->mins[1] - 40, floor[2] - 8 );
	VectorSet( maxs, floor[0] + pi->maxs[0] + 40, floor[1] + pi->maxs[1] + 40, floor[2] + pi->maxs[2] + 64 );
	n = trap_EntitiesInBox( mins, maxs, touch, 16 );
	for ( i = 0 ; i < n ; i++ ) {
		hit = &g_entities[touch[i]];
		if ( hit->s.eType == ET_MOVER || hit->s.eType == ET_PUSH_TRIGGER || hit->s.eType == ET_TELEPORT_TRIGGER
			|| hit->s.eType == ET_ITEM || hit->s.eType == ET_PLAYER || hit->s.eType == ET_MISSILE
			|| ( hit->r.contents & CONTENTS_TRIGGER ) || G_IsProp( touch[i] ) ) {
			return qfalse;
		}
	}
	// keep off spawn points, teleport exits and jump pad targets
	if ( G_PropNearAvoid( floor, 100 ) ) {
		return qfalse;
	}
	return qtrue;
}

static qboolean G_PropFindSpot( int type, const vec3_t near, float minDist, float maxDist, vec3_t floor ) {
	const propInfo_t	*pi = &propInfo[type];
	vec3_t		start, end, eye, look;
	trace_t		tr;
	float		a, d;

	a = random() * 2 * M_PI;
	d = minDist + random() * ( maxDist - minDist );
	VectorCopy( near, start );
	start[0] += cos( a ) * d;
	start[1] += sin( a ) * d;
	start[2] += 24;

	// in sight of the anchor (same room, not through a wall)
	VectorCopy( near, eye );
	eye[2] += 24;
	trap_Trace( &tr, eye, NULL, NULL, start, ENTITYNUM_NONE, MASK_SOLID );
	if ( tr.fraction < 1.0f ) {
		return qfalse;
	}
	VectorCopy( start, end );
	end[2] -= 200;
	trap_Trace( &tr, start, pi->mins, pi->maxs, end, ENTITYNUM_NONE, MASK_PLAYERSOLID );
	if ( tr.startsolid || tr.allsolid || tr.fraction == 1.0f || tr.plane.normal[2] < 0.93f
		|| tr.entityNum != ENTITYNUM_WORLD || ( tr.surfaceFlags & ( SURF_SKY | SURF_NOIMPACT ) ) ) {
		return qfalse;
	}
	VectorCopy( tr.endpos, floor );
	floor[2] += 0.25f;
	// the spot must be visible from above the anchor's floor too (not under a low ceiling ledge)
	VectorCopy( floor, look );
	look[2] += 20;
	trap_Trace( &tr, eye, NULL, NULL, look, ENTITYNUM_NONE, MASK_SOLID );
	if ( tr.fraction < 1.0f ) {
		return qfalse;
	}
	return G_PropCheckSpot( type, floor );
}

static void G_PropsCollectAnchors( void ) {
	gentity_t	*ent;
	int			i;

	numAnchors = 0;
	numAvoids = 0;
	for ( i = MAX_CLIENTS ; i < level.num_entities ; i++ ) {
		ent = &g_entities[i];
		if ( !ent->inuse || !ent->classname ) {
			continue;
		}
		if ( !Q_stricmp( ent->classname, "info_player_deathmatch" ) || !Q_stricmp( ent->classname, "info_player_start" )
			|| !Q_stricmp( ent->classname, "team_CTF_redspawn" ) || !Q_stricmp( ent->classname, "team_CTF_bluespawn" )
			|| !Q_stricmp( ent->classname, "team_CTF_redplayer" ) || !Q_stricmp( ent->classname, "team_CTF_blueplayer" ) ) {
			if ( numAnchors < PROP_MAX_ANCHORS ) {
				VectorCopy( ent->s.origin, anchors[numAnchors] );
				numAnchors++;
			}
			if ( numAvoids < PROP_MAX_ANCHORS ) {
				VectorCopy( ent->s.origin, avoids[numAvoids] );
				numAvoids++;
			}
		} else if ( !Q_stricmp( ent->classname, "misc_teleporter_dest" ) || !Q_stricmp( ent->classname, "target_position" )
			|| !Q_stricmp( ent->classname, "info_notnull" ) || !Q_stricmp( ent->classname, "target_teleporter" ) ) {
			if ( numAvoids < PROP_MAX_ANCHORS ) {
				VectorCopy( ent->s.origin, avoids[numAvoids] );
				numAvoids++;
			}
		} else if ( ent->item && ent->s.eType == ET_ITEM && !( ent->flags & FL_DROPPED_ITEM ) ) {
			if ( numAnchors < PROP_MAX_ANCHORS ) {
				VectorCopy( ent->s.origin, anchors[numAnchors] );
				numAnchors++;
			}
		}
	}
}

/*
===============================================================================

SPAWNING

===============================================================================
*/

static int G_PropCount( qboolean extrasToo ) {
	int		i, n = 0;

	for ( i = MAX_CLIENTS ; i < level.num_entities ; i++ ) {
		if ( G_IsProp( i ) && ( extrasToo || !props[i].extra ) ) {
			n++;
		}
	}
	return n;
}

static int G_PropRandomType( void ) {
	int		r = rand() % 100;

	if ( r < 30 ) {
		return PROP_BARREL;
	}
	if ( r < 82 ) {
		return PROP_CRATE;
	}
	return PROP_TANK;
}

static gentity_t *G_PropSpawn( int type, const vec3_t origin, qboolean extra ) {
	const propInfo_t	*pi = &propInfo[type];
	gentity_t	*ent;
	prop_t		*p;

	if ( G_PropCount( qtrue ) >= PROP_MAX_TOTAL || level.num_entities >= ENTITYNUM_MAX_NORMAL - 64 ) {
		return NULL;
	}
	ent = G_Spawn();
	p = &props[ent->s.number];
	memset( p, 0, sizeof( *p ) );
	p->active = qtrue;
	p->type = type;
	p->extra = extra;
	p->launcher = -1;
	p->hurtClient = -1;
	p->onGround = qtrue;
	p->groundEnt = ENTITYNUM_WORLD;
	p->ang[YAW] = rand() % 360;

	ent->classname = (char *)pi->classname;
	ent->s.eType = ET_GENERAL;
	ent->s.generic1 = RM_ENT_PROP;
	ent->s.modelindex2 = type;
	ent->s.time = level.time;			// the cgame pops it in
	VectorCopy( pi->mins, ent->r.mins );
	VectorCopy( pi->maxs, ent->r.maxs );
	ent->r.contents = CONTENTS_SOLID;
	ent->clipmask = MASK_PLAYERSOLID;
	ent->takedamage = qtrue;
	ent->health = pi->health;
	ent->pain = G_PropPain;
	ent->die = G_PropDie;
	ent->touch = G_PropTouch;
	ent->think = G_PropThink;
	ent->s.groundEntityNum = ENTITYNUM_WORLD;

	G_SetOrigin( ent, (float *)origin );
	VectorCopy( p->ang, ent->s.angles );
	VectorCopy( p->ang, ent->s.apos.trBase );
	ent->s.apos.trType = TR_STATIONARY;
	ent->physicsObject = qtrue;
	ent->physicsBounce = 0;
	G_PropStateBits( ent, p );
	trap_LinkEntity( ent );
	return ent;
}

/*
=============
G_PropPlaceRandom

A new prop at a random spot near an item or spawn point
=============
*/
static gentity_t *G_PropPlaceRandom( int type, qboolean fromSky ) {
	vec3_t		floor, top;
	trace_t		tr;
	gentity_t	*ent;
	prop_t		*p;
	int			tries;

	if ( !numAnchors ) {
		return NULL;
	}
	for ( tries = 0 ; tries < 40 ; tries++ ) {
		if ( !G_PropFindSpot( type, anchors[rand() % numAnchors], 56, 240, floor ) ) {
			continue;
		}
		if ( fromSky ) {
			// drop it from up high (below the ceiling)
			VectorCopy( floor, top );
			top[2] += 520;
			floor[2] += 2;
			trap_Trace( &tr, floor, propInfo[type].mins, propInfo[type].maxs, top, ENTITYNUM_NONE, MASK_PLAYERSOLID );
			if ( tr.startsolid ) {
				continue;
			}
			VectorCopy( tr.endpos, floor );
			floor[2] -= 8;
			if ( tr.surfaceFlags & SURF_SKY ) {
				floor[2] -= 40;
			}
		}
		ent = G_PropSpawn( type, floor, qfalse );
		if ( !ent ) {
			return NULL;
		}
		if ( fromSky ) {
			p = &props[ent->s.number];
			p->onGround = qfalse;
			p->airTime = level.time;
			VectorSet( p->vel, crandom() * 60, crandom() * 60, -80 );
			VectorSet( p->avel, crandom() * 200, crandom() * 90, crandom() * 200 );
			G_PropWake( ent );
		}
		return ent;
	}
	return NULL;
}

static int	wantedCached = -1, wantedAnchors = -1;

static int G_PropsWanted( void ) {
	int		n, spawns = 0, items = 0, i;

	if ( g_propCount.integer >= 0 ) {
		return g_propCount.integer > 40 ? 40 : g_propCount.integer;
	}
	if ( wantedCached >= 0 && wantedAnchors == numAnchors ) {
		return wantedCached;
	}
	for ( i = 0 ; i < numAnchors ; i++ ) {
		if ( G_PropNearAvoid( anchors[i], 1 ) ) {
			spawns++;
		} else {
			items++;
		}
	}
	n = 3 + ( spawns + items ) / 8;
	if ( n < 4 ) {
		n = 4;
	}
	if ( n > 8 ) {
		n = 8;
	}
	wantedCached = n;
	wantedAnchors = numAnchors;
	return n;
}

static void G_PropsSpawnInitial( void ) {
	int			want, i, k, tries, type, barrels, maxBarrels;
	gentity_t	*ent, *mate;
	vec3_t		floor;

	want = G_PropsWanted();
	maxBarrels = want / 3 > 2 ? want / 3 : 2;		// explosive barrels are the spice, not the meal
	barrels = 0;
	for ( i = 0, tries = 0 ; i < want && tries < want * 3 ; tries++ ) {
		type = G_PropRandomType();
		if ( type == PROP_BARREL && barrels >= maxBarrels ) {
			type = PROP_CRATE;
		}
		ent = G_PropPlaceRandom( type, qfalse );
		if ( !ent ) {
			continue;
		}
		i++;
		if ( type == PROP_BARREL ) {
			barrels++;
		}
		// barrels like company: small clusters make chain reactions
		if ( type == PROP_BARREL && i < want && barrels < maxBarrels && random() < 0.2f ) {
			for ( k = 0 ; k < 3 && i < want ; k++ ) {
				if ( G_PropFindSpot( PROP_BARREL, ent->r.currentOrigin, 34, 60, floor ) ) {
					mate = G_PropSpawn( random() < 0.8f ? PROP_BARREL : PROP_TANK, floor, qfalse );
					if ( mate ) {
						i++;
						if ( mate->s.modelindex2 == PROP_BARREL ) {
							barrels++;
						}
					}
					break;		// pairs at most
				}
			}
		}
	}
	G_Printf( "props: %i placed (%i anchors)\n", i, numAnchors );
	if ( trap_Cvar_VariableIntegerValue( "developer" ) ) {
		for ( k = MAX_CLIENTS ; k < level.num_entities ; k++ ) {
			if ( G_IsProp( k ) ) {
				G_Printf( "  prop %i %s at %s\n", k, g_entities[k].classname, vtos( g_entities[k].r.currentOrigin ) );
			}
		}
	}
}

static void G_PropQueue( int type, int msec, qboolean extra ) {
	if ( numQueued >= PROP_MAX_QUEUE ) {
		return;
	}
	queue[numQueued].type = type;
	queue[numQueued].time = level.time + msec;
	queue[numQueued].extra = extra;
	numQueued++;
}

static void G_PropRemove( gentity_t *ent, qboolean vanishFx ) {
	prop_t		*p = &props[ent->s.number];
	gentity_t	*tent;
	vec3_t		c;

	if ( trap_Cvar_VariableIntegerValue( "developer" ) ) {
		G_LogPrintf( "PropRemove: %i %s state %i fx %i\n", ent->s.number, ent->classname, ent->s.frame, vanishFx );
	}
	if ( vanishFx ) {
		G_PropCenter( ent, c );
		tent = G_TempEntity( c, EV_PROP_BREAK );
		tent->s.eventParm = p->type | 0x80;
	}
	G_PropWakeRiders( ent );
	p->active = qfalse;
	G_FreeEntity( ent );
}

/*
===============================================================================

DAMAGE

===============================================================================
*/

static void G_PropPain( gentity_t *self, gentity_t *attacker, int damage ) {
	prop_t	*p = &props[self->s.number];

	if ( !p->active ) {
		return;
	}
	G_PropSetLauncher( p, attacker );
	if ( level.time - p->hitEventTime > 120 ) {
		p->hitEventTime = level.time;
		G_AddEvent( self, EV_PROP_HIT, damage * 6 > 255 ? 255 : damage * 6 );
	}
	switch ( p->type ) {
	case PROP_BARREL:
		if ( self->health <= 25 ) {
			G_PropIgnite( self, attacker );		// holed: it catches fire and blows in a moment
		}
		break;
	case PROP_TANK:
		G_PropRupture( self, attacker );
		break;
	default:
		break;
	}
}

static void G_PropDie( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int mod ) {
	prop_t	*p = &props[self->s.number];

	if ( !p->active ) {
		return;
	}
	G_PropSetLauncher( p, attacker );
	if ( mod == MOD_PROP ) {
		p->chain = qtrue;
	}
	if ( p->type == PROP_TANK && !p->ruptured && self->health > -30 ) {
		G_PropRupture( self, attacker );
		return;
	}
	if ( p->type == PROP_CRATE ) {
		G_PropLightFuse( self, 1 );
		return;
	}
	if ( mod == MOD_PROP || ( inflictor && inflictor->s.eType == ET_MISSILE && inflictor->splashDamage && !( inflictor->s.number == self->s.number ) ) ) {
		// set off by a blast: a short fuse while the knockback throws it
		G_PropLightFuse( self, ( mod == MOD_PROP ? 140 : 260 ) + rand() % 220 );
	} else {
		G_PropLightFuse( self, 50 );
	}
}

/*
=============
G_PropDropItem

Crates sometimes hold a small health or some ammo
=============
*/
static void G_PropDropItem( gentity_t *ent, gentity_t *breaker ) {
	static const char *ammoNames[] = { "Bullets", "Shells", "Grenades", "Rockets", "Cells", "Lightning", "Slugs" };
	gitem_t		*item = NULL;
	vec3_t		org, vel;
	int			i, w;

	if ( level.rmMode == RM_MODE_INSTAGIB || level.rmMode == RM_MODE_OITC || random() > 0.4f ) {
		return;
	}
	if ( level.rmMode != RM_MODE_NORMAL || random() < 0.5f ) {
		item = BG_FindItem( "25 Health" );
	} else {
		// ammo for the breaker's gun if we can
		if ( breaker && breaker->client ) {
			w = breaker->client->ps.weapon;
			for ( i = 1 ; i < bg_numItems ; i++ ) {
				if ( bg_itemlist[i].giType == IT_AMMO && bg_itemlist[i].giTag == w ) {
					item = &bg_itemlist[i];
					break;
				}
			}
		}
		if ( !item ) {
			item = BG_FindItem( ammoNames[rand() % 7] );
		}
	}
	if ( !item ) {
		return;
	}
	G_PropCenter( ent, org );
	VectorSet( vel, crandom() * 80, crandom() * 80, 220 );
	LaunchItem( item, org, vel );
}

/*
=============
G_PropDestroy

Barrels and tanks explode, crates break
=============
*/
static void G_PropDestroy( gentity_t *ent ) {
	prop_t		*p = &props[ent->s.number];
	gentity_t	*attacker, *tent, *other;
	vec3_t		center, pos;
	qboolean	wasAlive[MAX_CLIENTS];
	int			i, kills, type = p->type;
	float		a;

	if ( !p->active ) {
		return;
	}
	attacker = G_PropCredit( p );
	G_PropCenter( ent, center );
	if ( trap_Cvar_VariableIntegerValue( "developer" ) ) {
		G_Printf( "prop %i %s destroyed at %s, credit %i%s\n", ent->s.number, ent->classname, vtos( center ),
			attacker->s.number, p->chain ? " (chain)" : "" );
		G_LogPrintf( "PropDestroy: %i %s state %i at %s\n", ent->s.number, ent->classname, ent->s.frame, vtos( center ) );
	}

	// out of the world first, so it doesn't block or take its own blast
	ent->takedamage = qfalse;
	ent->r.contents = 0;
	trap_UnlinkEntity( ent );
	G_PropWakeRiders( ent );
	p->active = qfalse;

	tent = G_TempEntity( center, EV_PROP_BREAK );
	tent->s.eventParm = type;
	VectorCopy( p->vel, tent->s.angles2 );
	SnapVector( tent->s.angles2 );

	if ( type == PROP_BARREL || type == PROP_TANK ) {
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			other = &g_entities[i];
			wasAlive[i] = other->inuse && other->client && other->health > 0
				&& other->client->sess.sessionTeam != TEAM_SPECTATOR;
		}
		if ( type == PROP_BARREL ) {
			G_RadiusDamage( center, attacker, PROP_BLAST_DAMAGE, PROP_BLAST_RADIUS, NULL, MOD_PROP );
		} else {
			G_RadiusDamage( center, attacker, PROP_TANK_BLAST_DAMAGE, PROP_TANK_BLAST_RADIUS, NULL, MOD_PROP );
		}
		// style for environmental kills
		kills = 0;
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			other = &g_entities[i];
			if ( wasAlive[i] && other != attacker && other->health <= 0 ) {
				kills++;
			}
		}
		if ( kills && attacker->client ) {
			G_StyleAdd( attacker, 50 * kills + ( p->chain ? 40 : 0 ),
				p->chain ? "CHAIN REACTION" : ( kills > 1 ? "BARREL BLOWOUT" : ( type == PROP_TANK ? "TANK KILL" : "BARREL KILL" ) ) );
		}
		// burning fuel
		if ( type == PROP_BARREL && G_RearmOn( RS_ELEMENTS ) ) {
			G_SpawnFirePatch( center, attacker->client ? attacker : NULL, 4000, 90 );
			a = random() * 2 * M_PI;
			for ( i = 0 ; i < 2 ; i++ ) {
				VectorCopy( center, pos );
				pos[0] += cos( a + i * M_PI ) * 70;
				pos[1] += sin( a + i * M_PI ) * 70;
				G_SpawnFirePatch( pos, attacker->client ? attacker : NULL, 3000, 70 );
			}
		}
	} else {
		G_PropDropItem( ent, attacker->client ? attacker : NULL );
	}

	if ( !p->extra ) {
		G_PropQueue( type, ( g_propRespawn.integer > 0 ? g_propRespawn.integer : 30 ) * 1000 + rand() % 5000, qfalse );
	}
	G_FreeEntity( ent );
}

/*
=============
G_PropFireCheck

Fire patches set props alight
=============
*/
static void G_PropFireCheck( void ) {
	int			fires[64], numFires = 0, i, j;
	gentity_t	*f, *ent;
	prop_t		*p;
	vec3_t		d;
	float		r;

	for ( i = MAX_CLIENTS ; i < level.num_entities && numFires < 64 ; i++ ) {
		f = &g_entities[i];
		if ( f->inuse && f->s.eType == ET_GENERAL && f->s.generic1 == RM_ENT_FIRE && f->count > level.time ) {
			fires[numFires++] = i;
		}
	}
	if ( !numFires ) {
		return;
	}
	for ( i = MAX_CLIENTS ; i < level.num_entities ; i++ ) {
		p = &props[i];
		if ( !G_IsProp( i ) || p->burning || p->ruptured || p->dying ) {
			continue;		// (G_IsProp: a door may have freed it and the slot been reused)
		}
		ent = &g_entities[i];
		for ( j = 0 ; j < numFires ; j++ ) {
			f = &g_entities[fires[j]];
			VectorSubtract( ent->r.currentOrigin, f->r.currentOrigin, d );
			r = f->random + 12;
			if ( fabs( d[2] ) < 56 && d[0] * d[0] + d[1] * d[1] < r * r ) {
				G_PropIgnite( ent, ( f->r.ownerNum >= 0 && f->r.ownerNum < MAX_CLIENTS ) ? &g_entities[f->r.ownerNum] : NULL );
				break;
			}
		}
	}
}

/*
===============================================================================

SIMULATION

===============================================================================
*/

static void G_PropSetBox( gentity_t *ent, prop_t *p, qboolean lying ) {
	const propInfo_t	*pi = &propInfo[p->type];

	p->lying = lying;
	if ( lying ) {
		VectorCopy( pi->lmins, ent->r.mins );
		VectorCopy( pi->lmaxs, ent->r.maxs );
	} else {
		VectorCopy( pi->mins, ent->r.mins );
		VectorCopy( pi->maxs, ent->r.maxs );
	}
}

static qboolean G_PropBoxFits( gentity_t *ent, prop_t *p, qboolean lying ) {
	const propInfo_t	*pi = &propInfo[p->type];
	trace_t		tr;
	vec3_t		start;

	VectorCopy( ent->r.currentOrigin, start );
	start[2] += 0.5f;
	if ( lying ) {
		trap_Trace( &tr, start, pi->lmins, pi->lmaxs, start, ent->s.number, ent->clipmask );
	} else {
		trap_Trace( &tr, start, pi->mins, pi->maxs, start, ent->s.number, ent->clipmask );
	}
	return !tr.startsolid && !tr.allsolid;
}

/*
=============
G_PropLand

Just touched down: settle upright or on the side (cylinders), squares for crates
=============
*/
static void G_PropLand( gentity_t *ent, prop_t *p ) {
	vec3_t	fwd, right, up;
	qboolean	lie;

	if ( !propInfo[p->type].cylinder || p->ruptured ) {
		return;
	}
	AngleVectors( p->ang, fwd, right, up );
	lie = fabs( up[2] ) < 0.55f;
	if ( lie != p->lying && G_PropBoxFits( ent, p, lie ) ) {
		G_PropSetBox( ent, p, lie );
		if ( lie ) {
			// the same axis direction, as yaw + roll 90 (pitch is the rolling angle)
			p->ang[YAW] = RAD2DEG( atan2( up[0], -up[1] ) );
			p->ang[PITCH] = 0;
			p->ang[ROLL] = 90;
		}
	}
}

static void G_PropApproachAngle( float *a, float target, float step ) {
	float	d = AngleSubtract( target, *a );

	if ( d > step ) {
		d = step;
	} else if ( d < -step ) {
		d = -step;
	}
	*a = AngleMod( *a + d );
}

/*
=============
G_PropOrient

Angles: tumble in the air, settle / roll on the ground
=============
*/
static qboolean G_PropOrient( gentity_t *ent, prop_t *p, float dt ) {
	float		speed, heading, d, step;
	qboolean	settled = qtrue;
	int			i;

	if ( !p->onGround || p->ruptured ) {
		VectorMA( p->ang, dt, p->avel, p->ang );
		for ( i = 0 ; i < 3 ; i++ ) {
			p->ang[i] = AngleMod( p->ang[i] );
		}
		if ( p->ruptured ) {
			// the valve makes it wobble and spin
			p->avel[PITCH] += crandom() * 1100 * dt;
			p->avel[ROLL] += crandom() * 1100 * dt;
			for ( i = 0 ; i < 3 ; i += 2 ) {
				if ( p->avel[i] > 260 ) {
					p->avel[i] = 260;
				} else if ( p->avel[i] < -260 ) {
					p->avel[i] = -260;
				}
			}
		}
		return qfalse;
	}

	step = 540 * dt;
	speed = sqrt( p->vel[0] * p->vel[0] + p->vel[1] * p->vel[1] );
	// yaw spin dies down
	p->avel[YAW] *= ( 1.0f - 4.0f * dt );
	if ( fabs( p->avel[YAW] ) > 5 ) {
		p->ang[YAW] = AngleMod( p->ang[YAW] + p->avel[YAW] * dt );
		settled = qfalse;
	}
	p->avel[PITCH] = p->avel[ROLL] = 0;

	if ( p->lying ) {
		// roll along the ground: the axis turns across the motion, pitch is the rolling angle
		if ( speed > 8 ) {
			heading = vectoyaw( p->vel );
			d = AngleSubtract( heading, p->ang[YAW] );
			if ( d > 90 || d < -90 ) {
				heading = AngleMod( heading + 180 );
				d = AngleSubtract( heading, p->ang[YAW] );
			}
			G_PropApproachAngle( &p->ang[YAW], heading, 240 * dt );
			// rolling forward along our heading turns pitch up
			d = ( p->vel[0] * cos( DEG2RAD( p->ang[YAW] ) ) + p->vel[1] * sin( DEG2RAD( p->ang[YAW] ) ) );
			p->ang[PITCH] = AngleMod( p->ang[PITCH] + RAD2DEG( d * dt / ( ent->r.maxs[2] * 0.5f ) ) );
			settled = qfalse;
		}
		if ( fabs( AngleSubtract( 90, p->ang[ROLL] ) ) > 1 ) {
			G_PropApproachAngle( &p->ang[ROLL], 90, step );
			settled = qfalse;
		}
		return settled;
	}
	if ( propInfo[p->type].cylinder ) {
		// upright (or upside down)
		d = fabs( AngleSubtract( p->ang[ROLL], 0 ) ) > 90 ? 180 : 0;
		if ( fabs( AngleSubtract( p->ang[PITCH], 0 ) ) > 0.5f || fabs( AngleSubtract( p->ang[ROLL], d ) ) > 0.5f ) {
			G_PropApproachAngle( &p->ang[PITCH], 0, step );
			G_PropApproachAngle( &p->ang[ROLL], d, step );
			settled = qfalse;
		}
		return settled;
	}
	// crates: flat on a face
	for ( i = 0 ; i < 3 ; i += 2 ) {
		d = 90.0f * (int)floor( p->ang[i] / 90.0f + 0.5f );
		if ( fabs( AngleSubtract( p->ang[i], d ) ) > 0.5f ) {
			G_PropApproachAngle( &p->ang[i], d, step );
			settled = qfalse;
		}
	}
	return settled;
}

/*
=============
G_PropImpact

The prop's move hit something
=============
*/
static void G_PropImpact( gentity_t *ent, prop_t *p, trace_t *tr, float impact ) {
	gentity_t	*other, *credit;
	prop_t		*op;
	vec3_t		dir, rel;
	float		f, m1, m2;
	int			dmg;

	if ( tr->entityNum < 0 || tr->entityNum >= ENTITYNUM_MAX_NORMAL ) {
		return;
	}
	other = &g_entities[tr->entityNum];

	// a flying prop slams into a player
	if ( other->client && other->health > 0 ) {
		VectorSubtract( p->vel, other->client->ps.velocity, rel );
		f = -DotProduct( rel, tr->plane.normal );
		if ( f < PROP_HIT_SPEED ) {
			return;
		}
		if ( p->launcher == other->s.number && level.time - p->launchTime < 350 ) {
			return;		// the kicker running after his own kick
		}
		if ( p->hurtClient == other->s.number && level.time - p->hurtTime < 400 ) {
			return;
		}
		p->hurtClient = other->s.number;
		p->hurtTime = level.time;
		dmg = (int)( ( f - 250 ) * propInfo[p->type].mass / 1500.0f );
		if ( dmg < 8 ) {
			dmg = 8;
		}
		if ( dmg > 70 ) {
			dmg = 70;
		}
		VectorCopy( p->vel, dir );
		VectorNormalize( dir );
		credit = G_PropCredit( p );
		G_Damage( other, ent, credit, dir, tr->endpos, dmg, 0, MOD_PROP );
		G_AddEvent( ent, EV_PROP_HIT, 255 );
		p->hitEventTime = level.time;
		if ( credit->client && credit != other && dmg >= 20 ) {
			G_StyleAdd( credit, 25, "PROP HIT" );
		}
		// hot ones go off on contact
		if ( ( p->burning || p->ruptured || p->dying ) && p->type != PROP_CRATE
			&& ( !p->ruptured || level.time - p->airTime > 350 ) ) {
			G_PropLightFuse( ent, 1 );
		}
		return;
	}

	// prop on prop: share the momentum
	if ( G_IsProp( other->s.number ) && other != ent ) {
		op = &props[other->s.number];
		m1 = propInfo[p->type].mass;
		m2 = propInfo[op->type].mass;
		f = impact * 2 * m1 / ( m1 + m2 ) * 0.7f;
		VectorMA( op->vel, -f, tr->plane.normal, op->vel );
		G_PropLeaveGround( op );
		if ( op->launcher < 0 || level.time - op->launchTime > 2000 ) {
			op->launcher = p->launcher;
			op->launchTime = p->launchTime;
		}
		G_PropWake( other );
		if ( impact > 650 ) {
			G_Damage( other, ent, G_PropCredit( p ), NULL, NULL, 12, DAMAGE_NO_KNOCKBACK, MOD_PROP );
		}
	}
}

/*
=============
G_PhysMove

Box move with bounces. Returns the hardest impact speed, fills its normal / trace.
=============
*/
static float G_PhysMove( gentity_t *ent, vec3_t vel, float dt, float bounce, qboolean *onGround,
		vec3_t groundNormal, int *groundEnt, trace_t *hardest, prop_t *p ) {
	vec3_t		pos, end, t;
	trace_t		tr;
	float		remaining = dt, vn, best = 0, imp;
	int			bump, mask = ent->clipmask;

	VectorCopy( ent->r.currentOrigin, pos );
	for ( bump = 0 ; bump < 3 && remaining > 0 ; bump++ ) {
		VectorMA( pos, remaining, vel, end );
		trap_Trace( &tr, pos, ent->r.mins, ent->r.maxs, end, ent->s.number, mask );
		if ( tr.startsolid ) {
			// overlapping a player (spawned into it, or pushed): ignore bodies for this move
			trap_Trace( &tr, pos, ent->r.mins, ent->r.maxs, end, ent->s.number, mask & ~CONTENTS_BODY );
			if ( tr.startsolid ) {
				// really stuck in the world: pop upwards
				VectorCopy( pos, t );
				t[2] += 10;
				trap_Trace( &tr, t, ent->r.mins, ent->r.maxs, t, ent->s.number, mask );
				if ( !tr.startsolid ) {
					VectorCopy( t, pos );
				}
				if ( p && !p->stuckTime ) {
					p->stuckTime = level.time;
				}
				break;
			}
		}
		if ( p ) {
			p->stuckTime = 0;
		}
		VectorCopy( tr.endpos, pos );
		if ( tr.fraction == 1.0f ) {
			break;
		}
		remaining *= ( 1.0f - tr.fraction );
		vn = DotProduct( vel, tr.plane.normal );
		if ( vn >= 0 ) {
			continue;
		}
		imp = -vn;
		if ( p ) {
			G_PropImpact( ent, p, &tr, imp );
		}
		if ( imp > best ) {
			best = imp;
			*hardest = tr;
		}
		if ( tr.plane.normal[2] > 0.7f && imp * bounce < 70 ) {
			// land
			VectorMA( vel, -vn, tr.plane.normal, vel );
			*onGround = qtrue;
			VectorCopy( tr.plane.normal, groundNormal );
			*groundEnt = tr.entityNum;
		} else {
			VectorMA( vel, -( 1.0f + bounce ) * vn, tr.plane.normal, vel );
			// scrape
			VectorMA( vel, -DotProduct( vel, tr.plane.normal ), tr.plane.normal, t );
			VectorMA( vel, -0.12f, t, vel );
		}
	}
	G_SetOrigin( ent, pos );
	return best;
}

/*
=============
G_PropSimulate
=============
*/
static void G_PropSimulate( gentity_t *ent, prop_t *p, float dt ) {
	const propInfo_t	*pi = &propInfo[p->type];
	vec3_t		center, down, fwd, right, up, gt, pos;
	trace_t		tr, hardest;
	float		grav, speed, decel, impact;
	int			contents;
	qboolean	wasOnGround = p->onGround;

	grav = G_PhysGravity();
	G_PropCenter( ent, center );

	contents = trap_PointContents( center, ent->s.number );
	if ( contents & ( CONTENTS_LAVA | CONTENTS_SLIME ) ) {
		G_PropDestroy( ent );
		return;
	}
	if ( contents & CONTENTS_NODROP ) {
		p->extra = qtrue;		// don't count it twice
		G_PropQueue( p->type, 10000, qfalse );
		G_PropRemove( ent, qfalse );
		return;
	}
	if ( contents & CONTENTS_WATER ) {
		// barrels and crates float, everything is slowed, fire goes out
		// (floating is not falling: the out-of-the-world clock waits)
		p->onGround = qfalse;
		p->airTime = level.time;
		p->vel[2] += grav * ( p->type == PROP_TANK ? 0.8f : 1.35f ) * dt;
		VectorScale( p->vel, 1.0f - 2.5f * dt, p->vel );
		VectorScale( p->avel, 1.0f - 2.0f * dt, p->avel );
		if ( p->burning ) {
			p->burning = qfalse;
			p->dying = qfalse;
			p->fuseTime = 0;
		}
	}

	if ( p->ruptured ) {
		// the valve at the top vents up and out (+x): the tank flies bottom first
		AngleVectors( p->ang, fwd, right, up );
		VectorMA( up, 0.6f, fwd, gt );
		VectorNormalize( gt );
		VectorMA( p->vel, -TANK_THRUST * dt, gt, p->vel );
		p->vel[2] += grav * 0.45f * dt;
		speed = VectorLength( p->vel );
		if ( speed > TANK_MAX_SPEED ) {
			VectorScale( p->vel, TANK_MAX_SPEED / speed, p->vel );
		}
		G_PropLeaveGround( p );
	}

	if ( p->onGround ) {
		// still supported?
		VectorCopy( ent->r.currentOrigin, down );
		down[2] -= 3;
		trap_Trace( &tr, ent->r.currentOrigin, ent->r.mins, ent->r.maxs, down, ent->s.number, ent->clipmask & ~CONTENTS_BODY );
		if ( tr.fraction == 1.0f || tr.plane.normal[2] < 0.7f ) {
			p->onGround = qfalse;
			p->airTime = level.time;
		} else {
			VectorCopy( tr.plane.normal, p->groundNormal );
			p->groundEnt = tr.entityNum;
		}
	}

	if ( p->onGround ) {
		// stay on the plane, slide down slopes, friction
		speed = DotProduct( p->vel, p->groundNormal );
		if ( speed < 0 ) {
			VectorMA( p->vel, -speed, p->groundNormal, p->vel );
		}
		VectorSet( gt, 0, 0, -grav );
		VectorMA( gt, -DotProduct( gt, p->groundNormal ), p->groundNormal, gt );
		VectorMA( p->vel, dt * ( p->lying ? 0.8f : 0.35f ), gt, p->vel );
		speed = VectorLength( p->vel );
		decel = ( p->lying ? pi->rollFriction : pi->friction ) * dt;
		if ( speed <= decel ) {
			VectorClear( p->vel );
		} else {
			VectorScale( p->vel, ( speed - decel ) / speed, p->vel );
		}
	} else {
		p->vel[2] -= grav * dt;
	}

	speed = VectorLength( p->vel );
	if ( speed > PROP_MAX_SPEED ) {
		VectorScale( p->vel, PROP_MAX_SPEED / speed, p->vel );
	}

	impact = 0;
	if ( VectorLengthSquared( p->vel ) > 0.01f ) {
		qboolean	landed = qfalse;

		impact = G_PhysMove( ent, p->vel, dt, p->ruptured ? 0.55f : pi->bounce, &landed, p->groundNormal, &p->groundEnt, &hardest, p );
		if ( !props[ent->s.number].active ) {
			return;		// (destroyed by an impact)
		}
		if ( landed && !p->ruptured ) {
			p->onGround = qtrue;
		}
		if ( impact > 120 ) {
			// a knock sound; a tumbling hit also spins it
			if ( level.time - p->hitEventTime > 150 ) {
				p->hitEventTime = level.time;
				G_AddEvent( ent, EV_PROP_HIT, impact / 5 > 255 ? 255 : (int)( impact / 5 ) );
			}
			if ( !p->onGround ) {
				p->avel[PITCH] += crandom() * impact * 0.6f;
				p->avel[ROLL] += crandom() * impact * 0.6f;
			}
			// hard landings shake burning barrels loose
			if ( impact > 700 && ( p->type == PROP_BARREL ) && !p->dying ) {
				ent->health -= 12;
				if ( ent->health <= 25 ) {
					G_PropIgnite( ent, G_PropCredit( p )->client ? G_PropCredit( p ) : NULL );
				}
			}
		}
	}
	if ( p->onGround && !wasOnGround ) {
		G_PropLand( ent, p );
	}
	if ( !p->onGround && wasOnGround ) {
		p->airTime = level.time;
	}

	// stuck for good: give up on this one
	if ( p->stuckTime && level.time - p->stuckTime > 2500 ) {
		if ( p->type == PROP_CRATE || p->burning || p->ruptured || p->dying ) {
			// a lit one goes off where it is, it never just vanishes
			G_PropLightFuse( ent, 1 );
		} else {
			p->extra = qtrue;
			G_PropQueue( p->type, 5000, qfalse );
			G_PropRemove( ent, qtrue );
		}
		return;
	}
	// fell out of the world (a lit one blows before this: its fuse is shorter)
	if ( !p->onGround && level.time - p->airTime > PROP_AIR_LIMIT ) {
		if ( p->burning || p->ruptured || p->dying ) {
			G_PropLightFuse( ent, 1 );
			return;
		}
		p->extra = qtrue;
		G_PropQueue( p->type, 5000, qfalse );
		G_PropRemove( ent, qfalse );
		return;
	}
}

/*
=============
G_PropThink
=============
*/
static void G_PropThink( gentity_t *ent ) {
	prop_t		*p = &props[ent->s.number];
	float		dt;
	qboolean	settled;

	if ( !p->active ) {
		return;
	}
	if ( p->dying && level.time >= p->fuseTime ) {
		G_PropDestroy( ent );
		return;
	}
	if ( !p->moving ) {
		// resting with a lit fuse
		ent->nextthink = p->dying ? p->fuseTime : 0;
		return;
	}

	dt = ( level.time - p->lastThink ) * 0.001f;
	p->lastThink = level.time;
	if ( dt > 0.1f ) {
		dt = 0.1f;
	}
	if ( dt > 0 ) {
		G_PropSimulate( ent, p, dt );
		if ( !p->active ) {
			return;
		}
		settled = G_PropOrient( ent, p, dt );
	} else {
		settled = qfalse;
	}

	VectorCopy( p->ang, ent->s.apos.trBase );
	VectorCopy( p->ang, ent->s.angles );
	VectorCopy( p->vel, ent->s.pos.trDelta );
	ent->s.groundEntityNum = p->onGround ? p->groundEnt : ENTITYNUM_NONE;

	if ( p->onGround && settled && !p->ruptured && VectorLength( p->vel ) < PROP_STOP_SPEED ) {
		// rest: stop thinking, let movers push us
		p->moving = qfalse;
		VectorClear( p->vel );
		VectorClear( p->avel );
		VectorClear( ent->s.pos.trDelta );
		ent->s.pos.trType = TR_STATIONARY;
		ent->s.apos.trType = TR_STATIONARY;
		ent->physicsObject = qtrue;
		if ( ent->s.groundEntityNum == ENTITYNUM_NONE ) {
			ent->s.groundEntityNum = ENTITYNUM_WORLD;
		}
		ent->nextthink = p->dying ? p->fuseTime : 0;
	} else {
		ent->s.pos.trType = TR_INTERPOLATE;
		ent->s.apos.trType = TR_INTERPOLATE;
		ent->nextthink = level.time + 1;
	}
	G_PropStateBits( ent, p );
	trap_LinkEntity( ent );
}

/*
=============
G_PropTouch

A player ran into the prop: kick it
=============
*/
static void G_PropTouch( gentity_t *self, gentity_t *other, trace_t *trace ) {
	prop_t	*p = &props[self->s.number];
	vec3_t	dir, pv;
	float	push, scale, tip;

	if ( !p->active || !other->client || other->health <= 0 || level.time - p->kickTime < 120 ) {
		return;
	}
	if ( other->client->ps.groundEntityNum == self->s.number ) {
		return;		// standing on it
	}
	VectorCopy( preVel[other->s.number], pv );
	VectorSubtract( self->r.currentOrigin, other->r.currentOrigin, dir );
	dir[2] = 0;
	if ( VectorNormalize( dir ) < 1 ) {
		return;
	}
	push = DotProduct( pv, dir ) - DotProduct( p->vel, dir );
	if ( push < 90 ) {
		return;
	}
	p->kickTime = level.time;
	scale = 1.35f * 100.0f / propInfo[p->type].mass;
	if ( other->client->ps.ammo[AMMO_RMFLAGS2] & RMS2_SLIDING ) {
		scale *= 1.6f;		// slide kick
	}
	VectorMA( p->vel, push * scale, dir, p->vel );
	p->vel[2] += 40 + push * 0.12f;
	G_PropSetLauncher( p, other );
	// hard kicks knock cylinders over
	tip = push * scale;
	if ( propInfo[p->type].cylinder && !p->lying && tip > 380 ) {
		p->vel[2] += 90;
		p->avel[PITCH] = crandom() * 420;
		p->avel[ROLL] = crandom() * 420;
	}
	p->avel[YAW] += crandom() * tip * 0.8f;
	p->onGround = qfalse;
	p->airTime = level.time;
	G_AddEvent( self, EV_PROP_HIT, push / 4 > 255 ? 255 : (int)( push / 4 ) );
	p->hitEventTime = level.time;
	G_PropWake( self );
	G_PropWakeRiders( self );
}

/*
===============================================================================

MANAGER

===============================================================================
*/

static void G_PropTestCommand( void ) {
	char		buf[256], *s, *tok;
	vec3_t		org;
	int			type, i;
	gentity_t	*ent;

	Q_strncpyz( buf, g_propTest.string, sizeof( buf ) );
	trap_Cvar_Set( "g_propTest", "" );
	s = buf;
	tok = COM_Parse( &s );
	if ( !tok[0] ) {
		return;
	}
	if ( !Q_stricmp( tok, "list" ) ) {
		for ( i = MAX_CLIENTS ; i < level.num_entities ; i++ ) {
			if ( G_IsProp( i ) ) {
				G_Printf( "prop %i %s at %s vel %s hp %i state %i%s%s launcher %i\n", i, g_entities[i].classname,
					vtos( g_entities[i].r.currentOrigin ), vtos( props[i].vel ), g_entities[i].health, g_entities[i].s.frame,
					props[i].moving ? " moving" : "", props[i].extra ? " extra" : "", props[i].launcher );
			}
		}
		return;
	}
	if ( !Q_stricmp( tok, "hit" ) ) {
		// dev: "hit <type|all> <damage>" shoots every resting prop of that type (barrel / crate /
		// tank / all) as player 0 with a machinegun round, like a player's shot would
		int		want = -1, dmg, k = 0;
		vec3_t	dir;

		tok = COM_Parse( &s );
		if ( !Q_stricmp( tok, "barrel" ) ) {
			want = PROP_BARREL;
		} else if ( !Q_stricmp( tok, "crate" ) ) {
			want = PROP_CRATE;
		} else if ( !Q_stricmp( tok, "tank" ) ) {
			want = PROP_TANK;
		}
		dmg = atoi( COM_Parse( &s ) );
		if ( dmg <= 0 ) {
			dmg = 7;
		}
		VectorSet( dir, 1, 0, 0 );
		for ( i = MAX_CLIENTS ; i < level.num_entities ; i++ ) {
			if ( !G_IsProp( i ) || props[i].moving || ( want >= 0 && props[i].type != want ) ) {
				continue;
			}
			ent = &g_entities[i];
			G_LogPrintf( "PropHit: %i %s hp %i dmg %i at %s\n", i, ent->classname, ent->health, dmg, vtos( ent->r.currentOrigin ) );
			G_Damage( ent, &g_entities[0], &g_entities[0], dir, ent->r.currentOrigin, dmg, 0, MOD_MACHINEGUN );
			k++;
		}
		G_Printf( "hit %i props\n", k );
		return;
	}
	if ( !Q_stricmp( tok, "bonanza" ) ) {
		tok = COM_Parse( &s );
		G_PropsBonanza( atoi( tok ) != 0 );
		return;
	}
	if ( !Q_stricmp( tok, "view" ) ) {
		// dev: "view n" puts player 0 at a clear spot looking at the n-th prop (screenshots)
		int			n, k, best = -1;
		float		bestFrac = 0, yaw;
		vec3_t		c, end, ang, dir, spot;
		trace_t		tr;
		static vec3_t	pmins = { -15, -15, -24 }, pmaxs = { 15, 15, 32 };

		n = atoi( COM_Parse( &s ) );
		for ( i = MAX_CLIENTS, k = 0 ; i < level.num_entities ; i++ ) {
			if ( G_IsProp( i ) && k++ == n ) {
				break;
			}
		}
		if ( i >= level.num_entities || !g_entities[0].client ) {
			return;
		}
		G_PropCenter( &g_entities[i], c );
		c[2] += 12;
		for ( k = 0 ; k < 8 ; k++ ) {
			VectorSet( ang, 0, k * 45, 0 );
			AngleVectors( ang, dir, NULL, NULL );
			VectorMA( c, 40, dir, spot );
			VectorMA( c, 230, dir, end );
			trap_Trace( &tr, spot, pmins, pmaxs, end, ENTITYNUM_NONE, MASK_PLAYERSOLID );
			if ( !tr.startsolid && tr.fraction > bestFrac ) {
				bestFrac = tr.fraction;
				best = k;
			}
		}
		if ( best < 0 ) {
			return;
		}
		VectorSet( ang, 0, best * 45, 0 );
		AngleVectors( ang, dir, NULL, NULL );
		VectorMA( c, 40 + 190 * bestFrac - 8, dir, spot );
		yaw = AngleMod( best * 45 + 180 );
		VectorSet( ang, 12, yaw, 0 );
		{
			gentity_t	*pl = &g_entities[0];

			VectorCopy( spot, pl->client->ps.origin );
			VectorClear( pl->client->ps.velocity );
			SetClientViewAngle( pl, ang );
			pl->client->ps.eFlags ^= EF_TELEPORT_BIT;
			BG_PlayerStateToEntityState( &pl->client->ps, &pl->s, qtrue );
			VectorCopy( pl->client->ps.origin, pl->r.currentOrigin );
			trap_LinkEntity( pl );
		}
		G_Printf( "view prop %i %s from %s\n", i, g_entities[i].classname, vtos( spot ) );
		return;
	}
	if ( !Q_stricmp( tok, "probe" ) ) {
		// dev: "probe x y z yaw" prints where a line in that direction hits a wall
		vec3_t	end, ang;
		trace_t	tr;

		for ( i = 0 ; i < 3 ; i++ ) {
			tok = COM_Parse( &s );
			org[i] = atof( tok );
		}
		tok = COM_Parse( &s );
		VectorSet( ang, 0, atof( tok ), 0 );
		AngleVectors( ang, end, NULL, NULL );
		VectorMA( org, 4000, end, end );
		trap_Trace( &tr, org, NULL, NULL, end, ENTITYNUM_NONE, MASK_PLAYERSOLID );
		G_Printf( "probe from %s yaw %s: hit %s (%.0f units)\n", vtos( org ), tok, vtos( tr.endpos ), tr.fraction * 4000 );
		return;
	}
	if ( !Q_stricmp( tok, "crate" ) ) {
		type = PROP_CRATE;
	} else if ( !Q_stricmp( tok, "tank" ) ) {
		type = PROP_TANK;
	} else {
		type = PROP_BARREL;
	}
	for ( i = 0 ; i < 3 ; i++ ) {
		tok = COM_Parse( &s );
		org[i] = atof( tok );
	}
	ent = G_PropSpawn( type, org, qtrue );
	if ( ent ) {
		G_PropLeaveGround( &props[ent->s.number] );
		G_PropWake( ent );
		G_Printf( "prop %i (%s) at %s\n", ent->s.number, ent->classname, vtos( org ) );
	}
}

static void G_PropsCheckResting( void ) {
	static int	next;
	int			i, touch[4], n, j;
	gentity_t	*ent, *hit;
	vec3_t		down, mins, maxs;
	trace_t		tr;
	prop_t		*p;

	if ( level.time < next ) {
		return;
	}
	next = level.time + 1000;
	for ( i = MAX_CLIENTS ; i < level.num_entities ; i++ ) {
		p = &props[i];
		if ( !G_IsProp( i ) || p->moving ) {
			continue;
		}
		ent = &g_entities[i];
		// support gone (a prop underneath flew away, a platform moved)
		VectorCopy( ent->r.currentOrigin, down );
		down[2] -= 3;
		trap_Trace( &tr, ent->r.currentOrigin, ent->r.mins, ent->r.maxs, down, i, ent->clipmask & ~CONTENTS_BODY );
		if ( tr.fraction == 1.0f && !tr.startsolid ) {
			p->onGround = qfalse;
			p->airTime = level.time;
			G_PropWake( ent );
			continue;
		}
		// a player spawned into it: shove it out
		VectorAdd( ent->r.currentOrigin, ent->r.mins, mins );
		VectorAdd( ent->r.currentOrigin, ent->r.maxs, maxs );
		n = trap_EntitiesInBox( mins, maxs, touch, 4 );
		for ( j = 0 ; j < n ; j++ ) {
			hit = &g_entities[touch[j]];
			if ( hit->client && hit->health > 0 ) {
				VectorSubtract( ent->r.currentOrigin, hit->r.currentOrigin, down );
				down[2] = 0;
				VectorNormalize( down );
				VectorMA( p->vel, 250, down, p->vel );
				p->vel[2] = 120;
				G_PropLeaveGround( p );
				G_PropWake( ent );
				break;
			}
		}
	}
}

static void G_PropManagerThink( gentity_t *self ) {
	int			i, want, have;
	gentity_t	*ent;
	qboolean	on;

	self->nextthink = level.time + PROP_MANAGER_MSEC;
	trap_Cvar_Update( &g_propCount );
	trap_Cvar_Update( &g_propRespawn );
	trap_Cvar_Update( &g_propTest );

	if ( g_propTest.string[0] ) {
		G_PropTestCommand();
	}

	on = G_PropsOn();
	if ( !on && propsWereOn ) {
		// switched off: clear the map (Barrel Bonanza extras stay until their mutator ends)
		for ( i = MAX_CLIENTS ; i < level.num_entities ; i++ ) {
			if ( G_IsProp( i ) && !props[i].extra ) {
				G_PropRemove( &g_entities[i], qtrue );
			}
		}
		for ( i = 0 ; i < numQueued ; ) {
			if ( !queue[i].extra ) {
				queue[i] = queue[numQueued - 1];
				numQueued--;
				continue;
			}
			i++;
		}
		spawnedInitial = qfalse;
	}
	propsWereOn = on;
	if ( !numAnchors ) {
		G_PropsCollectAnchors();
	}
	if ( on && !spawnedInitial ) {
		spawnedInitial = qtrue;
		G_PropsSpawnInitial();
	}

	// respawns and sky drops
	for ( i = 0 ; i < numQueued ; ) {
		if ( level.time >= queue[i].time ) {
			ent = G_PropPlaceRandom( queue[i].type, queue[i].extra );
			if ( ent || level.time - queue[i].time > 10000 ) {
				if ( ent && queue[i].extra ) {
					props[ent->s.number].extra = qtrue;
				}
				queue[i] = queue[numQueued - 1];
				numQueued--;
				continue;
			}
			queue[i].time = level.time + 700;		// no room right now, try again soon
		}
		i++;
	}
	// keep the count up if something got lost
	want = on ? G_PropsWanted() : 0;
	have = G_PropCount( qfalse );
	for ( i = 0 ; i < numQueued ; i++ ) {
		if ( !queue[i].extra ) {
			have++;
		}
	}
	if ( have < want ) {
		G_PropQueue( G_PropRandomType(), 30000, qfalse );
	}

	G_PropFireCheck();
	G_PropsCheckResting();
}

/*
===============================================================================

PUBLIC

===============================================================================
*/

/*
=============
G_PropsInit

Map start (after the entities spawned): the manager places the props once the items dropped
=============
*/
void G_PropsInit( void ) {
	static const char *drops[] = { "25 Health", "Bullets", "Shells", "Grenades", "Rockets", "Cells", "Lightning", "Slugs" };
	gitem_t	*item;
	int		i;

	memset( props, 0, sizeof( props ) );
	memset( bodies, 0, sizeof( bodies ) );
	memset( splatTime, 0, sizeof( splatTime ) );
	memset( knockTime, 0, sizeof( knockTime ) );
	numQueued = 0;
	numAnchors = 0;
	numAvoids = 0;
	spawnedInitial = qfalse;
	propsWereOn = qfalse;
	wantedCached = -1;

	trap_Cvar_Register( &g_propCount, "g_propCount", "-1", CVAR_ARCHIVE );
	trap_Cvar_Register( &g_propRespawn, "g_propRespawn", "30", CVAR_ARCHIVE );
	trap_Cvar_Register( &g_propTest, "g_propTest", "", 0 );
	trap_Cvar_Set( "g_propTest", "" );

	burnLoopSound = G_SoundIndex( "sound/rearmed/physics/burn_loop.wav" );
	gasLoopSound = G_SoundIndex( "sound/rearmed/physics/gas_loop.wav" );

	// crates can drop these; register them so the clients load the models
	for ( i = 0 ; i < 8 ; i++ ) {
		item = BG_FindItem( drops[i] );
		if ( item ) {
			RegisterItem( item );
		}
	}

	manager = G_Spawn();
	manager->classname = "prop_manager";
	manager->think = G_PropManagerThink;
	manager->nextthink = level.time + 500;		// items drop to the floor first
	manager->r.svFlags = SVF_NOCLIENT;
}

/*
=============
G_PropsBonanza

Barrel Bonanza: 12-18 barrels (and some tanks) rain down; qfalse clears what is left of them
=============
*/
void G_PropsBonanza( qboolean start ) {
	int		i, n, t;

	if ( start ) {
		n = 12 + rand() % 7;
		for ( i = 0 ; i < n ; i++ ) {
			t = ( rand() % 100 ) < 82 ? PROP_BARREL : PROP_TANK;
			G_PropQueue( t, 200 + i * ( 2600 / n ) + rand() % 150, qtrue );
		}
		return;
	}
	// the mutator ended: the leftovers vanish
	for ( i = 0 ; i < numQueued ; ) {
		if ( queue[i].extra ) {
			queue[i] = queue[numQueued - 1];
			numQueued--;
			continue;
		}
		i++;
	}
	for ( i = MAX_CLIENTS ; i < level.num_entities ; i++ ) {
		if ( G_IsProp( i ) && props[i].extra && !props[i].dying ) {
			G_PropRemove( &g_entities[i], qtrue );
		}
	}
}

/*
=============
G_PropNear

Closest live prop within radius (bots: shoot the barrel next to the enemy)
=============
*/
gentity_t *G_PropNear( const vec3_t origin, float radius, qboolean explosiveOnly ) {
	gentity_t	*best = NULL;
	float		bestDist = radius * radius, d;
	vec3_t		c, delta;
	int			i;

	for ( i = MAX_CLIENTS ; i < level.num_entities ; i++ ) {
		if ( !G_IsProp( i ) || props[i].dying ) {
			continue;
		}
		if ( explosiveOnly && props[i].type == PROP_CRATE ) {
			continue;
		}
		G_PropCenter( &g_entities[i], c );
		VectorSubtract( c, origin, delta );
		d = VectorLengthSquared( delta );
		if ( d < bestDist ) {
			bestDist = d;
			best = &g_entities[i];
		}
	}
	return best;
}

/*
=============
G_BodyThink

A body-queue corpse sliding / flying until it rests, then the stock sink
=============
*/
static void G_WallSplat( gentity_t *victim, float impact, const vec3_t normal, const vec3_t at );

static void G_BodyRest( gentity_t *ent ) {
	physBody_t	*b = &bodies[ent->s.number];

	b->active = qfalse;
	VectorClear( ent->s.pos.trDelta );
	ent->s.pos.trType = TR_STATIONARY;
	VectorCopy( ent->r.currentOrigin, ent->s.pos.trBase );
	ent->physicsObject = qtrue;
	if ( ent->s.groundEntityNum == ENTITYNUM_NONE ) {
		ent->s.groundEntityNum = ENTITYNUM_WORLD;
	}
	ent->think = BodySink;
	ent->nextthink = ent->timestamp + 5000 > level.time ? ent->timestamp + 5000 : level.time + 1;
}

static void G_BodyThink( gentity_t *ent ) {
	physBody_t	*b = &bodies[ent->s.number];
	trace_t		tr, hardest;
	vec3_t		down, contact;
	float		dt, speed, decel, impact;
	qboolean	landed = qfalse;

	if ( !b->active || ent->s.eType != ET_PLAYER || !ent->r.linked ) {
		b->active = qfalse;
		ent->think = BodySink;
		ent->nextthink = level.time + 1;
		return;
	}
	dt = ( level.time - b->lastThink ) * 0.001f;
	b->lastThink = level.time;
	if ( dt > 0.1f ) {
		dt = 0.1f;
	}
	ent->nextthink = level.time + 1;
	if ( dt <= 0 ) {
		return;
	}

	if ( b->onGround ) {
		VectorCopy( ent->r.currentOrigin, down );
		down[2] -= 3;
		trap_Trace( &tr, ent->r.currentOrigin, ent->r.mins, ent->r.maxs, down, ent->s.number, ent->clipmask );
		if ( tr.fraction == 1.0f || tr.plane.normal[2] < 0.7f ) {
			b->onGround = qfalse;
			b->airTime = level.time;
		}
	}
	if ( b->onGround ) {
		b->vel[2] = 0;
		speed = VectorLength( b->vel );
		decel = 650 * dt;
		if ( speed <= decel ) {
			VectorClear( b->vel );
		} else {
			VectorScale( b->vel, ( speed - decel ) / speed, b->vel );
		}
	} else {
		b->vel[2] -= G_PhysGravity() * dt;
	}

	if ( VectorLengthSquared( b->vel ) > 0.01f ) {
		vec3_t	gn;
		int		ge;

		impact = G_PhysMove( ent, b->vel, dt, CORPSE_BOUNCE, &landed, gn, &ge, &hardest, NULL );
		if ( landed ) {
			b->onGround = qtrue;
		}
		if ( impact >= SPLAT_SPEED - 50 && hardest.plane.normal[2] < 0.7f ) {
			VectorMA( hardest.endpos, -16, hardest.plane.normal, contact );
			G_WallSplat( ent, impact, hardest.plane.normal, contact );
		}
	}
	VectorCopy( b->vel, ent->s.pos.trDelta );
	ent->s.pos.trType = TR_INTERPOLATE;
	ent->s.groundEntityNum = b->onGround ? ENTITYNUM_WORLD : ENTITYNUM_NONE;
	trap_LinkEntity( ent );

	if ( ( b->onGround && VectorLength( b->vel ) < 10 ) || level.time - b->airTime > PROP_AIR_LIMIT ) {
		G_BodyRest( ent );
	}
}

static void G_BodyStart( gentity_t *body ) {
	physBody_t	*b = &bodies[body->s.number];

	if ( !b->active ) {
		b->active = qtrue;
		b->lastThink = level.time;
		b->airTime = level.time;
	}
	body->physicsObject = qfalse;
	body->s.pos.trType = TR_INTERPOLATE;
	VectorCopy( body->r.currentOrigin, body->s.pos.trBase );
	body->think = G_BodyThink;
	body->nextthink = level.time + 1;
}

/*
=============
G_PhysicsBodyInit

CopyToBodyQue: the corpse keeps the dead player's momentum
=============
*/
void G_PhysicsBodyInit( gentity_t *body, gentity_t *ent ) {
	physBody_t	*b = &bodies[body->s.number];

	b->active = qfalse;
	if ( !G_CorpsesOn() || !ent->client ) {
		return;
	}
	VectorCopy( ent->client->ps.velocity, b->vel );
	b->onGround = ( ent->client->ps.groundEntityNum != ENTITYNUM_NONE );
	if ( b->onGround && VectorLength( b->vel ) < 20 ) {
		return;		// at rest already: the stock body
	}
	G_BodyStart( body );
}

/*
=============
G_PhysicsKnockback

Called from G_Damage for every target hit with a direction. Props and corpses are
pushed here (the living get their knockback in G_Damage); living players remember
who knocked them, for wall splats.
=============
*/
/*
=============
G_PhysicsEnvironmentKiller

A player who dies to the map (a pit, lava, a crusher) within a few seconds of being
knocked by someone was knocked into it: that someone gets the kill
=============
*/
gentity_t *G_PhysicsEnvironmentKiller( gentity_t *self, gentity_t *attacker, int mod ) {
	gentity_t	*k;
	int			n = self->s.number, t;

	if ( n >= MAX_CLIENTS ) {
		return attacker;
	}
	t = knockTime[n];
	knockTime[n] = 0;		// a death ends the knock window (no credit carried into the next life)
	if ( attacker && attacker->client ) {
		return attacker;
	}
	if ( mod != MOD_TRIGGER_HURT && mod != MOD_FALLING && mod != MOD_LAVA && mod != MOD_SLIME
		&& mod != MOD_WATER && mod != MOD_CRUSH ) {
		return attacker;
	}
	if ( !t || level.time - t > 4000 ) {
		return attacker;
	}
	k = &g_entities[knocker[n]];
	if ( !k->inuse || !k->client || k == self || OnSameTeam( k, self ) ) {
		return attacker;
	}
	G_StyleAdd( k, 60, "VOID KILL" );
	return k;
}

void G_PhysicsKnockback( gentity_t *targ, gentity_t *attacker, const vec3_t dir, int knockback, int mod ) {
	prop_t		*p;
	physBody_t	*b;
	float		kick;
	int			n = targ->s.number;

	if ( n < MAX_CLIENTS && targ->client ) {
		if ( attacker && attacker->client && attacker != targ ) {
			knocker[n] = attacker->s.number;
			knockTime[n] = level.time;
		}
		if ( targ->health <= 0 && knockback > 0 && G_CorpsesOn() && targ->client->ps.pm_type == PM_DEAD ) {
			kick = g_knockback.value * knockback / 200.0f * CORPSE_KNOCK_SCALE;
			VectorMA( targ->client->ps.velocity, kick, dir, targ->client->ps.velocity );
			if ( targ->client->ps.velocity[2] > 40 ) {
				targ->client->ps.groundEntityNum = ENTITYNUM_NONE;
			}
			if ( VectorLength( targ->client->ps.velocity ) > PROP_MAX_SPEED ) {
				VectorNormalize( targ->client->ps.velocity );
				VectorScale( targ->client->ps.velocity, PROP_MAX_SPEED, targ->client->ps.velocity );
			}
		}
		return;
	}
	if ( knockback <= 0 ) {
		return;
	}

	p = &props[n];
	if ( G_IsProp( n ) ) {
		kick = g_knockback.value * knockback / propInfo[p->type].mass;
		VectorMA( p->vel, kick, dir, p->vel );
		if ( dir[2] > -0.2f && kick > 120 ) {
			p->vel[2] += kick * 0.15f;		// blasts lift things off the floor
		}
		if ( p->vel[2] > 30 || kick > 200 ) {
			p->onGround = qfalse;
			p->airTime = level.time;
		}
		// tumble with the hit
		p->avel[PITCH] += crandom() * kick * 1.1f;
		p->avel[YAW] += crandom() * kick * 0.6f;
		p->avel[ROLL] += crandom() * kick * 1.1f;
		G_PropSetLauncher( p, attacker );
		G_PropWake( targ );
		G_PropWakeRiders( targ );
		return;
	}

	// body-queue corpse
	if ( targ->s.eType == ET_PLAYER && targ->classname && !strcmp( targ->classname, "bodyque" ) && G_CorpsesOn()
		&& targ->r.linked && G_CorpseKickable( targ ) ) {		// g_corpses.c: kept corpses stay kickable
		b = &bodies[n];
		kick = g_knockback.value * knockback / 200.0f * CORPSE_KNOCK_SCALE;
		if ( !b->active ) {
			VectorClear( b->vel );
			b->onGround = qtrue;
		}
		VectorMA( b->vel, kick, dir, b->vel );
		if ( b->vel[2] > 40 ) {
			b->onGround = qfalse;
			b->airTime = level.time;
		}
		G_BodyStart( targ );
	}
}

/*
=============
G_WallSplat

A body hit a wall hard. The living take damage credited to whoever knocked them.
=============
*/
static void G_WallSplat( gentity_t *victim, float impact, const vec3_t normal, const vec3_t at ) {
	gentity_t	*tent, *attacker = NULL;
	int			n = victim->s.number, dmg;
	qboolean	alive = victim->client && victim->health > 0;

	if ( !G_CorpsesOn() || level.time - splatTime[n] < 450 ) {
		return;
	}
	if ( alive ) {
		if ( impact < SPLAT_SPEED || level.time - knockTime[n] > SPLAT_KNOCK_MSEC ) {
			return;
		}
		attacker = &g_entities[knocker[n]];
		if ( !attacker->inuse || !attacker->client ) {
			attacker = NULL;
		}
	} else {
		if ( impact < SPLAT_SPEED - 150 ) {
			return;		// corpses splat a little easier
		}
		if ( victim->client && victim->client->lasthurt_client >= 0 && victim->client->lasthurt_client < MAX_CLIENTS ) {
			attacker = &g_entities[victim->client->lasthurt_client];
		}
	}
	if ( !G_HF_WallSplat( victim, attacker, impact ) ) {
		return;		// en-feel: g_wallSplat 0, or a Juggernaut that wasn't slammed hard enough
	}
	splatTime[n] = level.time;

	tent = G_TempEntity( (float *)at, EV_WALLSPLAT );
	tent->s.eventParm = impact / 8 > 255 ? 255 : (int)( impact / 8 );
	VectorCopy( normal, tent->s.angles2 );
	tent->s.otherEntityNum = n;

	if ( alive ) {
		dmg = (int)( ( impact - 450 ) / 10 );
		if ( dmg < 10 ) {
			dmg = 10;
		}
		if ( dmg > 70 ) {
			dmg = 70;
		}
		if ( attacker && attacker != victim ) {
			G_StyleAdd( attacker, 40 + dmg, "WALL SPLAT" );
		}
		G_Damage( victim, NULL, attacker, NULL, (float *)at, dmg, DAMAGE_NO_KNOCKBACK, MOD_WALLSPLAT );
	} else if ( impact > 1000 && victim->takedamage ) {
		// splattered
		G_Damage( victim, NULL, attacker, NULL, (float *)at, 300, DAMAGE_NO_KNOCKBACK | DAMAGE_NO_PROTECTION, MOD_WALLSPLAT );
	}
}

/*
=============
G_PhysicsClientMoved

After each pmove: remember the velocity (prop kicks), bounce corpses off walls, detect wall splats
=============
*/
void G_PhysicsClientMoved( gentity_t *ent, const vec3_t oldVelocity ) {
	gclient_t	*cl = ent->client;
	vec3_t		dir, end, contact, v;
	trace_t		tr;
	float		speed, impact, vn;
	int			n = ent->s.number;

	if ( !cl || n >= MAX_CLIENTS ) {
		return;
	}
	VectorCopy( oldVelocity, preVel[n] );
	if ( !G_CorpsesOn() || ( cl->ps.pm_type != PM_NORMAL && cl->ps.pm_type != PM_DEAD ) ) {
		return;
	}
	speed = VectorLength( oldVelocity );
	if ( speed < 350 ) {
		return;
	}
	VectorScale( oldVelocity, 1.0f / speed, dir );
	VectorMA( cl->ps.origin, 14, dir, end );
	trap_Trace( &tr, cl->ps.origin, ent->r.mins, ent->r.maxs, end, n, MASK_PLAYERSOLID & ~CONTENTS_BODY );
	if ( tr.fraction == 1.0f || tr.startsolid ) {
		return;
	}
	impact = -DotProduct( oldVelocity, tr.plane.normal );
	if ( impact < 250 ) {
		return;
	}
	// did the move actually stop us against it?
	vn = DotProduct( cl->ps.velocity, tr.plane.normal );
	if ( vn < -0.5f * impact ) {
		return;
	}
	if ( cl->ps.pm_type == PM_DEAD ) {
		if ( tr.plane.normal[2] > 0.7f ) {
			// a hard landing bounces a little
			if ( impact > 450 ) {
				cl->ps.velocity[2] = impact * 0.22f;
				cl->ps.groundEntityNum = ENTITYNUM_NONE;
			}
			return;
		}
		// ragdoll-ish bounce off the wall
		VectorCopy( oldVelocity, v );
		VectorMA( v, -( 1.0f + CORPSE_BOUNCE ) * DotProduct( v, tr.plane.normal ), tr.plane.normal, v );
		VectorCopy( v, cl->ps.velocity );
	} else if ( tr.plane.normal[2] > 0.7f ) {
		return;		// the living land normally
	}
	VectorMA( tr.endpos, -16, tr.plane.normal, contact );
	if ( cl->ps.pm_type != PM_DEAD ) {
		contact[2] += 8;
	}
	G_WallSplat( ent, impact, tr.plane.normal, contact );
}
