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
// cg_gorekeep.c -- rearmed: corpses and blood stay until the map ends
//
//   cg_gorePersist 1     (archived) blood marks, splats, pools, drips,
//                        footprints, gibs and bullet holes stay until the map
//                        ends; pools dry darker over a few minutes and seep
//                        wider. The server keeps the corpses (g_corpses.c
//                        reads the same cvar). 0: everything fades as in
//                        stock Q3.
//   cg_gorePersistMax N  (archived, 8-64, default 64) roughly the carnage of
//                        N deaths: N corpses, N x 128 blood polys, N x 4 gib
//                        chunks, N x 16 bullet holes / burn marks.
//   cg_goreStats 1       prints fps, snapshot entities and pool use every 2 s.
//
// Pools recycle oldest first. Old corpses outside the view skip all their
// client work (CG_GoreKeepCullCorpse).

#include "cg_local.h"

static vmCvar_t		cg_gorePersist;
static vmCvar_t		cg_gorePersistMax;
static vmCvar_t		cg_goreStats;
static vmCvar_t		cg_goreKeepDebug;		// dev: 1 no blood poly submit, 2 no drying, 4 no permanent marks, 8 no corpses
static qboolean		keepRegistered;

// pool use, filled in by cg_marks.c / cg_gore.c each frame
int		cg_goreStatPermMade, cg_goreStatPerm, cg_goreStatPermDrawn, cg_goreStatMarks;
int		cg_goreStatGibs, cg_goreStatGibsDrawn;
static int	statCorpses, statCorpsesDrawn;

// the corpse birth (entityState.time, set by CopyToBodyQue) last seen in each slot
static int	corpseBirth[MAX_GENTITIES];

// fps window
static int	statStart, statFrames, statLast, statWorst;

/*
=================
CG_GoreKeepInit

From CG_InitMarkPolys (cgame load, map_restart, vid_restart).
=================
*/
void CG_GoreKeepInit( void ) {
	if ( !keepRegistered ) {
		trap_Cvar_Register( &cg_gorePersist, "cg_gorePersist", "1", CVAR_ARCHIVE );
		trap_Cvar_Register( &cg_gorePersistMax, "cg_gorePersistMax", "64", CVAR_ARCHIVE );
		trap_Cvar_Register( &cg_goreStats, "cg_goreStats", "0", 0 );
		trap_Cvar_Register( &cg_goreKeepDebug, "cg_goreKeepDebug", "0", 0 );
		keepRegistered = qtrue;
	}
	statStart = 0;
	memset( corpseBirth, 0, sizeof( corpseBirth ) );
}

int CG_GoreKeepDebug( void ) {
	return cg_goreKeepDebug.integer;
}

int CG_GorePersist( void ) {
	if ( !keepRegistered ) {
		CG_GoreKeepInit();
	}
	return cg_gorePersist.integer != 0;
}

/*
=================
CG_GoreKeepCap

cg_gorePersistMax clamped to 8-64.
=================
*/
int CG_GoreKeepCap( void ) {
	int		n;

	if ( !keepRegistered ) {
		CG_GoreKeepInit();
	}
	n = cg_gorePersistMax.integer;
	if ( n < 8 ) {
		n = 8;
	} else if ( n > 64 ) {
		n = 64;
	}
	return n;
}

/*
=================
CG_GoreInView

Is a sphere at least partly inside the view frustum of cg.refdef?
=================
*/
qboolean CG_GoreInView( const vec3_t p, float radius ) {
	vec3_t	delta;
	float	fwd, side, up, tx, ty;

	VectorSubtract( p, cg.refdef.vieworg, delta );
	fwd = DotProduct( delta, cg.refdef.viewaxis[0] );
	if ( fwd < -radius ) {
		return qfalse;
	}
	if ( fwd < radius ) {
		return qtrue;
	}
	side = DotProduct( delta, cg.refdef.viewaxis[1] );
	up = DotProduct( delta, cg.refdef.viewaxis[2] );
	tx = tan( cg.refdef.fov_x * ( M_PI / 360.0f ) );
	ty = tan( cg.refdef.fov_y * ( M_PI / 360.0f ) );
	// the sphere radius widens the cone a little (generous: never pop visible things)
	if ( fabs( side ) > fwd * tx + radius * 1.5f ) {
		return qfalse;
	}
	if ( fabs( up ) > fwd * ty + radius * 1.5f ) {
		return qfalse;
	}
	return qtrue;
}

/*
=================
CG_GoreKeepCullCorpse

CG_Player: qtrue skips a body-queue corpse outside the view (no animation,
shadow trace, gore or refentities). The renderer would cull it anyway, this
saves the client work for a map full of bodies.
=================
*/
qboolean CG_GoreKeepCullCorpse( centity_t *cent ) {
	int		num;

	if ( cent->currentState.number < MAX_CLIENTS || !( cent->currentState.eFlags & EF_DEAD ) ) {
		return qfalse;
	}
	statCorpses++;
	// a new corpse in this slot: bleed it if its death wasn't seen (cg_blood.c)
	num = cent->currentState.number;
	if ( corpseBirth[num] != cent->currentState.time ) {
		corpseBirth[num] = cent->currentState.time;
		if ( CG_GorePersist() && cent->currentState.time ) {
			CG_BloodUnseenCorpse( cent->lerpOrigin );
		}
	}
	if ( cg_goreKeepDebug.integer & 8 ) {
		return qtrue;		// dev: profile without corpses
	}
	if ( CG_GorePersist() && !CG_GoreInView( cent->lerpOrigin, 64 ) && !CG_KillCamWantsArea( cent->lerpOrigin ) ) {
		return qtrue;
	}
	statCorpsesDrawn++;
	return qfalse;
}

/*
=================
CG_GoreKeepFrame

Once per frame (CG_AddMarks): the cg_goreStats line.
=================
*/
void CG_GoreKeepFrame( void ) {
	int		now, frame;
	float	secs;

	if ( !keepRegistered ) {
		CG_GoreKeepInit();
	}
	trap_Cvar_Update( &cg_gorePersist );
	trap_Cvar_Update( &cg_gorePersistMax );
	trap_Cvar_Update( &cg_goreStats );
	trap_Cvar_Update( &cg_goreKeepDebug );

	now = trap_Milliseconds();
	if ( !cg_goreStats.integer ) {
		statStart = 0;
	} else if ( !statStart ) {
		statStart = now;
		statLast = now;
		statFrames = 0;
		statWorst = 0;
	} else {
		frame = now - statLast;
		statLast = now;
		if ( frame > statWorst ) {
			statWorst = frame;
		}
		statFrames++;
		if ( now - statStart >= 2000 ) {
			secs = ( now - statStart ) * 0.001f;
			CG_Printf( "gorestats: t %i fps %.1f worst %i ms | snap %i corpses %i drawn %i | perm %i drawn %i made %i | marks %i | gibs %i drawn %i\n",
				cg.time / 1000, statFrames / secs, statWorst, cg.snap ? cg.snap->numEntities : 0,
				statCorpses, statCorpsesDrawn, cg_goreStatPerm, cg_goreStatPermDrawn, cg_goreStatPermMade, cg_goreStatMarks,
				cg_goreStatGibs, cg_goreStatGibsDrawn );
			statStart = now;
			statFrames = 0;
			statWorst = 0;
		}
	}
	statCorpses = 0;
	statCorpsesDrawn = 0;
}
