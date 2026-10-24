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
// cg_blood.c -- REARMED: blood that stays where players died (cg_gorePersist)
//
// Every trace happens once, when the death / gib event arrives. The marks go
// into the permanent mark pool in cg_marks.c, which never fades them and
// keeps the most recent ones until the level ends.

#include "cg_local.h"

// cg_marks.c
void CG_PermanentMark( qhandle_t markShader, const vec3_t origin, const vec3_t dir,
				   float orientation, float red, float green, float blue, float alpha,
				   float radius, int cell );

#define	BLOOD_FLOOR_DROP	128		// how far below the body to look for the floor
#define	BLOOD_WALL_REACH	90		// horizontal reach of the wall splatter
#define	BLOOD_BODY_HEIGHT	24		// player origin above the feet

// renderer poly limits the pool wants (the stock 600 / 3000 fit ~200 blood polys)
#define	BLOOD_WANT_POLYS	16384
#define	BLOOD_WANT_VERTS	131072

// cells of the 2x2 atlas gfx/rearmed/blood.tga (shader rearmed/blood)
#define	CELL_POOL			0		// 0-1: big pools
#define	CELL_SPLAT			2		// 2-3: splatter

static qhandle_t	bloodShader;
static qboolean		bloodAtlas;		// qfalse: stock "bloodMark", which ignores vertex color

void CG_BloodClearRemembered( void );

// cg_gore.c
void CG_GoreClear( void );
void CG_GorePool( qhandle_t shader, const vec3_t org, const vec3_t normal, float radius, int cell,
				 const float *rgba, int delay, int dur );

/*
=================
CG_InitBlood

Called from CG_InitMarkPolys (cgame load and map_restart).
=================
*/
void CG_InitBlood( void ) {
	int		polys, verts;
	char	buf[32];

	bloodShader = trap_R_RegisterShader( "rearmed/blood" );
	bloodAtlas = ( bloodShader != 0 );
	if ( !bloodAtlas ) {
		bloodShader = cgs.media.bloodMarkShader;
	}
	CG_GoreClear();
	CG_BloodClearRemembered();

	// The renderer sizes its per-frame poly buffers from r_maxpolys and
	// r_maxpolyverts at startup. Raise them (archived) so the pools have room;
	// cg_marks.c already measured this session's limits and stays inside them.
	if ( !CG_GorePersist() ) {
		return;
	}
	trap_Cvar_VariableStringBuffer( "r_maxpolys", buf, sizeof( buf ) );
	polys = atoi( buf );
	trap_Cvar_VariableStringBuffer( "r_maxpolyverts", buf, sizeof( buf ) );
	verts = atoi( buf );
	if ( polys >= BLOOD_WANT_POLYS && verts >= BLOOD_WANT_VERTS ) {
		return;
	}
	trap_Cvar_Register( NULL, "r_maxpolys", "600", CVAR_ARCHIVE );
	trap_Cvar_Register( NULL, "r_maxpolyverts", "3000", CVAR_ARCHIVE );
	if ( polys < BLOOD_WANT_POLYS ) {
		trap_Cvar_Set( "r_maxpolys", va( "%i", BLOOD_WANT_POLYS ) );
	}
	if ( verts < BLOOD_WANT_VERTS ) {
		trap_Cvar_Set( "r_maxpolyverts", va( "%i", BLOOD_WANT_VERTS ) );
	}
	CG_Printf( "cg_gorePersist: raised r_maxpolys / r_maxpolyverts, takes effect after vid_restart\n" );
}

/*
=================
CG_BloodTint

One tint per death, from fresh red to dark, brown-red old-looking blood.
The atlas texture is a bright red with some green in it, so scaling green
down less than red turns it browner.
=================
*/
void CG_BloodTintColor( vec3_t tint ) {
	float	age, dark;

	age = random();
	dark = 0.75f + 0.25f * random();
	tint[0] = dark * ( 1.00f - 0.38f * age );
	tint[1] = dark * ( 0.55f + 0.17f * age );
	tint[2] = dark * ( 0.60f + 0.08f * age );
}

/*
=================
CG_BloodLight

Marks aren't lit by the lightmap, so darken blood in dark places once, from
the light grid at creation time.
=================
*/
float CG_BloodLightLevel( const vec3_t origin, const vec3_t normal ) {
	vec3_t	p, ambient, directed, dir;
	float	level;

	VectorMA( origin, 8, normal, p );
	if ( !trap_R_LightForPoint( p, ambient, directed, dir ) ) {
		return 1.0f;
	}
	level = ( ambient[0] + ambient[1] + ambient[2] + directed[0] + directed[1] + directed[2] )
		/ ( 3.0f * 255.0f * 0.6f );
	if ( level > 1.0f ) {
		level = 1.0f;
	} else if ( level < 0 ) {
		level = 0;
	}
	return 0.45f + 0.55f * level;
}

/*
=================
CG_BloodTrace

World-only point trace; qfalse unless it hit a surface that takes marks.
=================
*/
static qboolean CG_BloodTrace( trace_t *tr, const vec3_t start, const vec3_t end ) {
	trap_CM_BoxTrace( tr, start, end, NULL, NULL, 0, MASK_SOLID );
	if ( tr->allsolid || tr->startsolid || tr->fraction >= 1.0f ) {
		return qfalse;
	}
	if ( tr->surfaceFlags & ( SURF_SKY | SURF_NOIMPACT | SURF_NOMARKS ) ) {
		return qfalse;
	}
	return qtrue;
}

/*
=================
CG_BloodFloor
=================
*/
static qboolean CG_BloodFloor( const vec3_t start, vec3_t floor, vec3_t normal ) {
	trace_t	tr;
	vec3_t	end;

	VectorCopy( start, end );
	end[2] -= BLOOD_FLOOR_DROP;
	if ( !CG_BloodTrace( &tr, start, end ) ) {
		return qfalse;
	}
	if ( tr.plane.normal[2] < 0.5f ) {
		return qfalse;		// too steep to hold a pool
	}
	VectorCopy( tr.endpos, floor );
	VectorCopy( tr.plane.normal, normal );
	return qtrue;
}

/*
=================
CG_BloodMark
=================
*/
static void CG_BloodMark( const vec3_t origin, const vec3_t normal, float radius, int cell, const vec3_t tint ) {
	float	shade, alpha;

	shade = CG_BloodLightLevel( origin, normal ) * ( 0.9f + 0.1f * random() );
	alpha = 0.85f + 0.15f * random();
	CG_PermanentMark( bloodShader, origin, normal, random() * 360,
		tint[0] * shade, tint[1] * shade, tint[2] * shade, alpha,
		radius, bloodAtlas ? cell : -1 );
}

/*
=================
CG_BloodShader

For cg_gore.c: the pool / splatter atlas (atlas = qfalse: the stock mark).
=================
*/
qhandle_t CG_BloodShader( qboolean *atlas ) {
	*atlas = bloodAtlas;
	return bloodShader;
}

/*
=================
CG_BloodGrowPool

With cg_gore, a pool spreads out under the body over a few seconds.
=================
*/
static void CG_BloodGrowPool( const vec3_t origin, const vec3_t normal, float radius, const vec3_t tint, int delay, int dur ) {
	float	shade, rgba[4];

	if ( CG_GoreLevel() <= 0 ) {
		CG_BloodMark( origin, normal, radius, CELL_POOL + ( rand() & 1 ), tint );
		return;
	}
	shade = CG_BloodLightLevel( origin, normal ) * ( 0.9f + 0.1f * random() );
	rgba[0] = tint[0] * shade;
	rgba[1] = tint[1] * shade;
	rgba[2] = tint[2] * shade;
	rgba[3] = 0.9f + 0.1f * random();
	CG_GorePool( bloodShader, origin, normal, radius, bloodAtlas ? CELL_POOL + ( rand() & 1 ) : -1, rgba, delay, dur );
}

/*
=================
CG_BloodFloorSplats

Splats on the floor around the body. A wall between the body and a splat
cuts it short, so blood doesn't show up in the next room.
=================
*/
static void CG_BloodFloorSplats( const vec3_t center, int count, float spread, const vec3_t tint ) {
	int		i;
	float	yaw, dist;
	vec3_t	dir, target, p, floor, normal;
	trace_t	tr;

	for ( i = 0 ; i < count ; i++ ) {
		yaw = random() * 2 * M_PI;
		dist = spread * ( 0.2f + 0.8f * sqrt( random() ) );
		dir[0] = cos( yaw );
		dir[1] = sin( yaw );
		dir[2] = 0;
		VectorMA( center, dist, dir, target );

		trap_CM_BoxTrace( &tr, center, target, NULL, NULL, 0, MASK_SOLID );
		if ( tr.allsolid || tr.startsolid ) {
			continue;
		}
		VectorCopy( tr.endpos, p );
		if ( tr.fraction < 1.0f ) {
			VectorMA( p, -4, dir, p );
		}
		if ( !CG_BloodFloor( p, floor, normal ) ) {
			continue;
		}
		CG_BloodMark( floor, normal, 14 + 16 * random(), CELL_SPLAT + ( rand() & 1 ), tint );
	}
}

/*
=================
CG_BloodWallSplats

Horizontal traces at 0-40 units above the floor; splat where they hit a wall.
=================
*/
static void CG_BloodWallSplats( const vec3_t base, int count, const vec3_t tint ) {
	int		i;
	float	yaw;
	vec3_t	start, end;
	trace_t	tr;

	for ( i = 0 ; i < count ; i++ ) {
		yaw = random() * 2 * M_PI;
		VectorCopy( base, start );
		start[2] += 2 + 38 * random();
		end[0] = start[0] + cos( yaw ) * BLOOD_WALL_REACH;
		end[1] = start[1] + sin( yaw ) * BLOOD_WALL_REACH;
		end[2] = start[2];

		if ( !CG_BloodTrace( &tr, start, end ) ) {
			continue;
		}
		if ( fabs( tr.plane.normal[2] ) > 0.7f ) {
			continue;		// floor or ceiling, not a wall
		}
		CG_BloodMark( tr.endpos, tr.plane.normal, 16 + 12 * random(), CELL_SPLAT + ( rand() & 1 ), tint );
	}
}

/*
=================
CG_BloodBurst
=================
*/
// REARMED (cg_gorePersist): where deaths bled, so a corpse found later whose death
// happened out of sight (no death event reached this client) can bleed then
#define	MAX_BLED			96
#define	BLED_RANGE			160
static vec3_t	bled[MAX_BLED];
static int		numBled, bledNext;

static void CG_BloodRemember( const vec3_t origin ) {
	VectorCopy( origin, bled[bledNext] );
	bledNext = ( bledNext + 1 ) % MAX_BLED;
	if ( numBled < MAX_BLED ) {
		numBled++;
	}
}

static void CG_BloodBurst( const vec3_t origin, int splats, float spread, int walls ) {
	vec3_t	tint, floor, normal, base;

	if ( !CG_GorePersist() || !cg_blood.integer || !cg_addMarks.integer ) {
		return;
	}
	CG_BloodRemember( origin );

	CG_BloodTintColor( tint );

	VectorCopy( origin, base );
	base[2] -= BLOOD_BODY_HEIGHT;
	if ( CG_BloodFloor( origin, floor, normal ) ) {
		// the pool first, so the splats land on top of it (with cg_gore it grows over a few seconds)
		CG_BloodGrowPool( floor, normal, ( 36 + 20 * random() ) * ( CG_GoreLevel() >= 3 ? 1.35f : 1.0f ), tint,
			400, 2600 + (int)( 1600 * random() ) );
		VectorCopy( floor, base );
	}
	CG_BloodFloorSplats( origin, splats, spread, tint );
	CG_BloodWallSplats( base, walls, tint );
}

/*
=================
CG_BloodPoolDeath

EV_DEATH1-3: a pool under the body, 3-6 splats around it, 1-3 wall splats.
=================
*/
void CG_BloodPoolDeath( const vec3_t origin ) {
	CG_BloodBurst( origin, 3 + rand() % 4, 70, 1 + rand() % 3 );
}

/*
=================
CG_BloodPoolGib

EV_GIB_PLAYER (a gibbing death sends this instead of EV_DEATHx): a pool plus
8-12 splats over a wider area and 3-5 wall splats.
=================
*/
void CG_BloodPoolGib( const vec3_t origin ) {
	CG_BloodBurst( origin, 8 + rand() % 5, 140, 3 + rand() % 3 );
}

/*
=================
CG_BloodUnseenCorpse

cg_gorekeep.c, the first time a kept corpse shows up: if its death bled
nowhere near (it died out of this client's sight), it gets its pool and
splats now, fully spread, so a room full of bodies is never clean.
=================
*/
void CG_BloodUnseenCorpse( const vec3_t origin ) {
	int		i;
	vec3_t	tint, floor, normal, base;

	if ( !CG_GorePersist() || !cg_blood.integer || !cg_addMarks.integer ) {
		return;
	}
	for ( i = 0 ; i < numBled ; i++ ) {
		if ( DistanceSquared( bled[i], origin ) < BLED_RANGE * BLED_RANGE ) {
			return;
		}
	}
	CG_BloodRemember( origin );
	CG_BloodTintColor( tint );
	VectorCopy( origin, base );
	base[2] -= BLOOD_BODY_HEIGHT;
	if ( CG_BloodFloor( origin, floor, normal ) ) {
		CG_BloodMark( floor, normal, 40 + 20 * random(), CELL_POOL + ( rand() & 1 ), tint );
		VectorCopy( floor, base );
	}
	CG_BloodFloorSplats( origin, 3 + rand() % 3, 70, tint );
	CG_BloodWallSplats( base, rand() % 3, tint );
}

/*
=================
CG_BloodClearRemembered

New map / map_restart.
=================
*/
void CG_BloodClearRemembered( void ) {
	numBled = 0;
	bledNext = 0;
}
