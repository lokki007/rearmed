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
// cg_marks.c -- wall marks

#include "cg_local.h"

/*
===================================================================

MARK POLYS

===================================================================
*/


markPoly_t	cg_activeMarkPolys;			// double linked list
markPoly_t	*cg_freeMarkPolys;			// single linked list
markPoly_t	cg_markPolys[MAX_MARK_POLYS];
static		int	markTotal;

/*
REARMED: permanent marks (cg_gorePersist, cg_gorekeep.c)

Blood never fades and lives in its own pool, so it can't push out the normal
marks. The pool is a ring buffer of polys: when it is full the oldest
permanent mark is recycled (all of its fragments), so a long match keeps the
most recent permSize polys (cg_gorePersistMax x 128, read at map load), split
into a keep ring (deaths) and a spray ring (hits).
Blood dries: it darkens and browns over a few minutes. Cleared with the other
marks (new map, map_restart, vid_restart).
*/
#define	MAX_PERM_POLYS		8192
#define	MIN_PERM_POLYS		1024
#define	PERM_POLYS_PER_DEATH	128		// x cg_gorePersistMax
#define	MAX_PERM_FRAGMENTS	24		// per mark, a pool on stairs or a patch can split a lot
#define	PERM_RESERVE_POLYS	3400	// renderer polys left for stock marks (1024 kept), shadows, particles, gore (cg_gore.c)
#define	PERM_RESERVE_VERTS	14000
#define	PERM_DRY_SLICES		32		// drying updates 1/32 of the ring per frame
#define	PERM_DRY_HALF		75000	// msec until blood is half dried

// stock marks (bullet holes, burns, stock blood) kept with cg_gorePersist
#define	KEEP_MARKS_PER_DEATH	16		// x cg_gorePersistMax, up to MAX_MARK_POLYS
#define	STOCK_MARK_CAP			256		// the stock pool size, used when cg_gorePersist is off

#define	MARKMODE_TEMPORARY	0
#define	MARKMODE_FADING		1
#define	MARKMODE_PERMANENT	2		// spray ring: hit spray, droplets, footprints, gib bounces
#define	MARKMODE_KEEP		3		// keep ring: pools and splats where someone died

// The pool is split in two rings so a long fight's hit spray can't recycle the
// pools under the corpses: the keep ring gets PERM_KEEP_SHARE of the polys.
#define	PERM_KEEP_SHARE		0.4f

static markPoly_t	cg_permPolys[MAX_PERM_POLYS];	// markPoly_t.time holds the mark's serial
static byte		cg_permVisible[MAX_PERM_POLYS];
static int		cg_permBirth[MAX_PERM_POLYS];	// cg.time the poly was made (drying)
static float	cg_permSphere[MAX_PERM_POLYS][4];	// bounding sphere (frustum culling)
static byte		cg_permNumVerts[MAX_PERM_POLYS];	// poly.numVerts, compact: the per-frame scan stays in cache
static int		permSize;		// ring size in use (cg_gorePersistMax, at map load)
static int		ringStart[2], ringSize[2];	// 0 = keep, 1 = spray
static int		ringNext[2];	// next slot to write, the oldest slot once the ring has wrapped
static int		dryNext;		// drying: next slot to update
static int		activeMarks;	// stock marks in use
static int		permSerial;		// serial of the last permanent mark
static int		permOverwritten;	// serial of the last mark whose slot was just reused
static int		permPolyBudget, permVertBudget;
static qboolean	permBudgetSet;

void CG_InitBlood( void );		// cg_blood.c

/*
===================
CG_InitPermanentMarks

The renderer drops polys past r_maxpolys / r_maxpolyverts (read at renderer
init), so permanent marks only use what is left after a reserve for the stock
effects. Measured once per cgame load, before CG_InitBlood may raise the cvars
for the next renderer start.
===================
*/
static void CG_InitPermanentMarks( void ) {
	int		polys, verts;
	char	buf[32];

	memset( cg_permPolys, 0, sizeof( cg_permPolys ) );
	memset( cg_permNumVerts, 0, sizeof( cg_permNumVerts ) );
	CG_GoreKeepInit();
	permSize = CG_GoreKeepCap() * PERM_POLYS_PER_DEATH;
	if ( permSize < MIN_PERM_POLYS ) {
		permSize = MIN_PERM_POLYS;
	} else if ( permSize > MAX_PERM_POLYS ) {
		permSize = MAX_PERM_POLYS;
	}
	ringStart[0] = 0;
	ringSize[0] = (int)( permSize * PERM_KEEP_SHARE );
	ringStart[1] = ringSize[0];
	ringSize[1] = permSize - ringSize[0];
	ringNext[0] = ringStart[0];
	ringNext[1] = ringStart[1];
	dryNext = 0;
	permSerial = 0;
	permOverwritten = 0;

	if ( !permBudgetSet ) {
		trap_Cvar_VariableStringBuffer( "r_maxpolys", buf, sizeof( buf ) );
		polys = atoi( buf );
		trap_Cvar_VariableStringBuffer( "r_maxpolyverts", buf, sizeof( buf ) );
		verts = atoi( buf );
		if ( polys < 600 ) {
			polys = 600;		// renderer minimum (MAX_POLYS)
		}
		if ( verts < 3000 ) {
			verts = 3000;		// renderer minimum (MAX_POLYVERTS)
		}
		permPolyBudget = polys - PERM_RESERVE_POLYS;
		permVertBudget = verts - PERM_RESERVE_VERTS;
		if ( permPolyBudget < 64 ) {
			permPolyBudget = 64;
		}
		if ( permVertBudget < 256 ) {
			permVertBudget = 256;
		}
		permBudgetSet = qtrue;
	}

	CG_InitBlood();
}

/*
===================
CG_AllocPermanentMark

Takes the next ring slot. If it still holds an older mark, that mark is being
recycled: remember it so its remaining fragments get removed too.
===================
*/
static markPoly_t *CG_AllocPermanentMark( int ring ) {
	markPoly_t	*mp;
	int			n = ringNext[ring];

	mp = &cg_permPolys[n];
	if ( mp->poly.numVerts && mp->time != permSerial ) {
		permOverwritten = mp->time;
	}
	cg_permBirth[n] = cg.time;
	cg_permNumVerts[n] = 0;
	if ( ++n >= ringStart[ring] + ringSize[ring] ) {
		n = ringStart[ring];
	}
	ringNext[ring] = n;

	memset( mp, 0, sizeof( *mp ) );
	return mp;
}

/*
===================
CG_TrimPermanentMarks

After a mark is written, drop the rest of a mark it partly overwrote. A mark's
fragments are contiguous in the ring, so they follow the write position.
===================
*/
static void CG_TrimPermanentMarks( int ring );

/*
===================
CG_PrunePermanentMarks

Blood piles up where many people die: dozens of alpha-blended layers on the
same floor cost a lot of fill rate (3.5 ms at 3840x1080 for one pile of 60
bodies). After a new mark, the older polys centred near it are summed as
layers (a poly's coverage of the disk around the new mark, at most 1 each);
past PERM_MAX_LAYERS the oldest buried ones are dropped. The top layers look
the same, the overdraw stays bounded, and the ring keeps room for elsewhere.
===================
*/
#define	PERM_MAX_LAYERS		5.0f
#define	PERM_PRUNE_MAX		128

static void CG_PrunePermanentMarks( const vec3_t origin, float radius ) {
	int			i, n, best, cand[PERM_PRUNE_MAX];
	float		R, r, d2, layers, cover[PERM_PRUNE_MAX];
	float		*sp;
	vec3_t		delta;

	R = radius;
	if ( R < 20 ) {
		R = 20;
	} else if ( R > 64 ) {
		R = 64;
	}
	n = 0;
	layers = 0;
	for ( i = 0 ; i < permSize && n < PERM_PRUNE_MAX ; i++ ) {
		if ( !cg_permNumVerts[i] ) {
			continue;
		}
		sp = cg_permSphere[i];
		VectorSubtract( sp, origin, delta );
		d2 = DotProduct( delta, delta );
		if ( d2 > R * R || cg_permPolys[i].time == permSerial ) {
			continue;		// too far, or the mark just made
		}
		r = sp[3] < R ? sp[3] : R;
		cand[n] = i;
		cover[n] = ( r * r ) / ( R * R );
		layers += cover[n];
		n++;
	}
	while ( layers > PERM_MAX_LAYERS && n > 0 ) {
		// drop the oldest
		best = 0;
		for ( i = 1 ; i < n ; i++ ) {
			if ( cg_permBirth[cand[i]] < cg_permBirth[cand[best]] ) {
				best = i;
			}
		}
		cg_permPolys[cand[best]].poly.numVerts = 0;
		cg_permNumVerts[cand[best]] = 0;
		layers -= cover[best];
		n--;
		cand[best] = cand[n];
		cover[best] = cover[n];
	}
}

static void CG_TrimPermanentMarks( int ring ) {
	int		i, k;

	if ( !permOverwritten ) {
		return;
	}
	k = ringNext[ring];
	for ( i = 0 ; i < MAX_PERM_FRAGMENTS ; i++ ) {
		if ( !cg_permPolys[k].poly.numVerts || cg_permPolys[k].time != permOverwritten ) {
			break;
		}
		cg_permPolys[k].poly.numVerts = 0;
		cg_permNumVerts[k] = 0;
		if ( ++k >= ringStart[ring] + ringSize[ring] ) {
			k = ringStart[ring];
		}
	}
	permOverwritten = 0;
}

/*
===================
CG_InitMarkPolys

This is called at startup and for tournement restarts
===================
*/
void	CG_InitMarkPolys( void ) {
	int		i;

	memset( cg_markPolys, 0, sizeof(cg_markPolys) );
	activeMarks = 0;

	cg_activeMarkPolys.nextMark = &cg_activeMarkPolys;
	cg_activeMarkPolys.prevMark = &cg_activeMarkPolys;
	cg_freeMarkPolys = cg_markPolys;
	for ( i = 0 ; i < MAX_MARK_POLYS - 1 ; i++ ) {
		cg_markPolys[i].nextMark = &cg_markPolys[i+1];
	}

	CG_InitPermanentMarks();
}


/*
==================
CG_FreeMarkPoly
==================
*/
void CG_FreeMarkPoly( markPoly_t *le ) {
	if ( !le->prevMark || !le->nextMark ) {
		CG_Error( "CG_FreeLocalEntity: not active" );
	}

	// remove from the doubly linked active list
	le->prevMark->nextMark = le->nextMark;
	le->nextMark->prevMark = le->prevMark;

	// the free list is only singly linked
	le->nextMark = cg_freeMarkPolys;
	cg_freeMarkPolys = le;
	activeMarks--;
}

/*
===================
CG_AllocMark

Will allways succeed, even if it requires freeing an old active mark
===================
*/
markPoly_t	*CG_AllocMark( void ) {
	markPoly_t	*le;
	int time, cap;

	// REARMED: kept marks have a cap of their own, the stock 256 otherwise
	if ( CG_GorePersist() ) {
		cap = CG_GoreKeepCap() * KEEP_MARKS_PER_DEATH;
		if ( cap > MAX_MARK_POLYS ) {
			cap = MAX_MARK_POLYS;
		}
		if ( cap < STOCK_MARK_CAP ) {
			cap = STOCK_MARK_CAP;
		}
	} else {
		cap = STOCK_MARK_CAP;
	}
	if ( cg_freeMarkPolys && activeMarks >= cap && cg_activeMarkPolys.prevMark != &cg_activeMarkPolys ) {
		CG_FreeMarkPoly( cg_activeMarkPolys.prevMark );		// the oldest one
	}

	if ( !cg_freeMarkPolys ) {
		// no free entities, so free the one at the end of the chain
		// remove the oldest active entity
		time = cg_activeMarkPolys.prevMark->time;
		while (cg_activeMarkPolys.prevMark && time == cg_activeMarkPolys.prevMark->time) {
			CG_FreeMarkPoly( cg_activeMarkPolys.prevMark );
		}
	}

	le = cg_freeMarkPolys;
	cg_freeMarkPolys = cg_freeMarkPolys->nextMark;

	memset( le, 0, sizeof( *le ) );

	// link into the active list
	le->nextMark = cg_activeMarkPolys.nextMark;
	le->prevMark = &cg_activeMarkPolys;
	cg_activeMarkPolys.nextMark->prevMark = le;
	cg_activeMarkPolys.nextMark = le;
	activeMarks++;
	return le;
}


#define	MAX_MARK_FRAGMENTS	128
#define	MAX_MARK_POINTS		384

/*
=================
CG_ProjectMark

Clips a mark quad against the world and hands out the fragments: drawn once
(MARKMODE_TEMPORARY), stored with fade-out (MARKMODE_FADING) or stored in the
permanent pool (MARKMODE_PERMANENT). cell 0-3 maps the mark onto one quarter
of a 2x2 texture atlas, -1 uses the whole texture.
=================
*/
static void CG_ProjectMark( qhandle_t markShader, const vec3_t origin, const vec3_t dir,
				   float orientation, float red, float green, float blue, float alpha,
				   qboolean alphaFade, float radius, int mode, int cell, const vec3_t sdir ) {
	vec3_t			axis[3];
	float			texCoordScale;
	float			stScale, s0, t0;
	vec3_t			originalPoints[4];
	byte			colors[4];
	int				i, j;
	int				numFragments, maxFragments;
	markFragment_t	markFragments[MAX_MARK_FRAGMENTS], *mf;
	vec3_t			markPoints[MAX_MARK_POINTS];
	vec3_t			projection;
	qboolean		perm;
	int				ring;

	if ( !cg_addMarks.integer ) {
		return;
	}

	if ( radius <= 0 ) {
		CG_Error( "CG_ImpactMark called with <= 0 radius" );
	}

	if ( cell >= 0 ) {
		stScale = 0.5f;
		s0 = ( cell & 1 ) * 0.5f;
		t0 = ( ( cell >> 1 ) & 1 ) * 0.5f;
	} else {
		stScale = 1.0f;
		s0 = 0;
		t0 = 0;
	}
	perm = ( mode == MARKMODE_PERMANENT || mode == MARKMODE_KEEP );
	ring = ( mode == MARKMODE_KEEP ) ? 0 : 1;
	maxFragments = perm ? MAX_PERM_FRAGMENTS : MAX_MARK_FRAGMENTS;

	//if ( markTotal >= MAX_MARK_POLYS ) {
	//	return;
	//}

	// create the texture axis
	VectorNormalize2( dir, axis[0] );
	if ( sdir ) {
		// REARMED: the texture's s axis along sdir (projected onto the surface)
		VectorMA( sdir, -DotProduct( sdir, axis[0] ), axis[0], axis[1] );
		if ( VectorNormalize( axis[1] ) < 0.001f ) {
			PerpendicularVector( axis[1], axis[0] );
		}
		CrossProduct( axis[1], axis[0], axis[2] );	// same handedness as the stock axes (MarkFragments needs the winding)
	} else {
		PerpendicularVector( axis[1], axis[0] );
		RotatePointAroundVector( axis[2], axis[0], axis[1], orientation );
		CrossProduct( axis[0], axis[2], axis[1] );
	}

	texCoordScale = 0.5 * 1.0 / radius;

	// create the full polygon
	for ( i = 0 ; i < 3 ; i++ ) {
		originalPoints[0][i] = origin[i] - radius * axis[1][i] - radius * axis[2][i];
		originalPoints[1][i] = origin[i] + radius * axis[1][i] - radius * axis[2][i];
		originalPoints[2][i] = origin[i] + radius * axis[1][i] + radius * axis[2][i];
		originalPoints[3][i] = origin[i] - radius * axis[1][i] + radius * axis[2][i];
	}

	// get the fragments
	VectorScale( dir, -20, projection );
	numFragments = trap_CM_MarkFragments( 4, (void *)originalPoints,
					projection, MAX_MARK_POINTS, markPoints[0],
					maxFragments, markFragments );

	colors[0] = red * 255;
	colors[1] = green * 255;
	colors[2] = blue * 255;
	colors[3] = alpha * 255;

	if ( perm && numFragments > 0 ) {
		permSerial++;
	}

	for ( i = 0, mf = markFragments ; i < numFragments ; i++, mf++ ) {
		polyVert_t	*v;
		polyVert_t	verts[MAX_VERTS_ON_POLY];
		markPoly_t	*mark;

		// we have an upper limit on the complexity of polygons
		// that we store persistantly
		if ( mf->numPoints > MAX_VERTS_ON_POLY ) {
			mf->numPoints = MAX_VERTS_ON_POLY;
		}
		for ( j = 0, v = verts ; j < mf->numPoints ; j++, v++ ) {
			vec3_t		delta;

			VectorCopy( markPoints[mf->firstPoint + j], v->xyz );

			VectorSubtract( v->xyz, origin, delta );
			v->st[0] = s0 + stScale * ( 0.5 + DotProduct( delta, axis[1] ) * texCoordScale );
			v->st[1] = t0 + stScale * ( 0.5 + DotProduct( delta, axis[2] ) * texCoordScale );
			*(int *)v->modulate = *(int *)colors;
		}

		// if it is a temporary (shadow) mark, add it immediately and forget about it
		if ( mode == MARKMODE_TEMPORARY ) {
			trap_R_AddPolyToScene( markShader, mf->numPoints, verts );
			continue;
		}

		// otherwise save it persistantly
		if ( perm ) {
			mark = CG_AllocPermanentMark( ring );
			mark->time = permSerial;
		} else {
			mark = CG_AllocMark();
			mark->time = cg.time;
			markTotal++;
		}
		mark->alphaFade = alphaFade;
		mark->markShader = markShader;
		mark->poly.numVerts = mf->numPoints;
		mark->color[0] = red;
		mark->color[1] = green;
		mark->color[2] = blue;
		mark->color[3] = alpha;
		memcpy( mark->verts, verts, mf->numPoints * sizeof( verts[0] ) );
		if ( perm ) {
			// REARMED: a bounding sphere for culling
			float	*sp = cg_permSphere[mark - cg_permPolys];
			float	d, r = 0;
			vec3_t	c;

			VectorClear( c );
			for ( j = 0 ; j < mf->numPoints ; j++ ) {
				VectorAdd( c, verts[j].xyz, c );
			}
			VectorScale( c, 1.0f / mf->numPoints, c );
			for ( j = 0 ; j < mf->numPoints ; j++ ) {
				d = Distance( c, verts[j].xyz );
				if ( d > r ) {
					r = d;
				}
			}
			VectorCopy( c, sp );
			sp[3] = r + 1;
			cg_permNumVerts[mark - cg_permPolys] = (byte)mf->numPoints;
		}
	}

	if ( perm ) {
		CG_TrimPermanentMarks( ring );
		if ( numFragments > 0 ) {
			CG_PrunePermanentMarks( origin, radius );
		}
	}
}

/*
=================
CG_ImpactMark

origin should be a point within a unit of the plane
dir should be the plane normal

temporary marks will not be stored or randomly oriented, but immediately
passed to the renderer.
=================
*/
void CG_ImpactMark( qhandle_t markShader, const vec3_t origin, const vec3_t dir,
				   float orientation, float red, float green, float blue, float alpha,
				   qboolean alphaFade, float radius, qboolean temporary ) {
	CG_ProjectMark( markShader, origin, dir, orientation, red, green, blue, alpha,
		alphaFade, radius, temporary ? MARKMODE_TEMPORARY : MARKMODE_FADING, -1, NULL );
}

/*
=================
CG_PermanentMark

REARMED: a mark that never fades (blood pools). Same arguments as
CG_ImpactMark, plus the texture atlas cell (0-3, or -1 for the whole texture).
=================
*/
void CG_PermanentMark( qhandle_t markShader, const vec3_t origin, const vec3_t dir,
				   float orientation, float red, float green, float blue, float alpha,
				   float radius, int cell ) {
	CG_ProjectMark( markShader, origin, dir, orientation, red, green, blue, alpha,
		qfalse, radius, MARKMODE_KEEP, cell, NULL );
}

/*
=================
CG_GoreMark

REARMED gore (cg_gore.c): mode 0 = drawn this frame only, 1 = fades like a
stock mark, 2 = permanent (spray ring), 3 = permanent (keep ring: deaths). sdir (optional) aligns the texture's s axis, NULL
picks a random rotation.
=================
*/
void CG_GoreMark( qhandle_t shader, const vec3_t origin, const vec3_t normal, const vec3_t sdir,
				 float radius, const float *rgba, int mode, int cell ) {
	if ( radius <= 0 || !shader ) {
		return;
	}
	// all gore marks are blood: kept with cg_gorePersist (footprints too), stock fade without
	if ( CG_GorePersist() ) {
		if ( mode == MARKMODE_FADING ) {
			mode = MARKMODE_PERMANENT;
		}
	} else if ( mode == MARKMODE_PERMANENT || mode == MARKMODE_KEEP ) {
		mode = MARKMODE_FADING;
	}
	CG_ProjectMark( shader, origin, normal, random() * 360, rgba[0], rgba[1], rgba[2], rgba[3],
		qtrue, radius, mode, cell, sdir );
}

/*
===============
CG_DryPermanentMarks

Blood darkens and turns brown as it dries (half way after PERM_DRY_HALF).
Every frame one slice of the ring gets its vertex colours from the colour it
was made with and its age.
===============
*/
static void CG_DryPermanentMarks( void ) {
	int			i, j, k, n, age;
	float		f, r, g, b, a;
	markPoly_t	*mp;
	byte		c[4];

	n = permSize / PERM_DRY_SLICES + 1;
	for ( i = 0 ; i < n ; i++ ) {
		k = dryNext;
		if ( ++dryNext >= permSize ) {
			dryNext = 0;
		}
		age = cg.time - cg_permBirth[k];
		if ( !cg_permNumVerts[k] || age < 4000 ) {
			continue;
		}
		mp = &cg_permPolys[k];
		f = (float)( age - 4000 ) / (float)( age - 4000 + PERM_DRY_HALF );		// 0 .. 1
		r = mp->color[0] * ( 1.0f - 0.55f * f );
		g = mp->color[1] * ( 1.0f - 0.38f * f );
		b = mp->color[2] * ( 1.0f - 0.42f * f );
		a = mp->color[3] * ( 1.0f + 0.12f * f );
		if ( a > 1.0f ) {
			a = 1.0f;
		}
		c[0] = (byte)( r * 255 );
		c[1] = (byte)( g * 255 );
		c[2] = (byte)( b * 255 );
		c[3] = (byte)( a * 255 );
		for ( j = 0 ; j < mp->poly.numVerts ; j++ ) {
			mp->verts[j].modulate[0] = c[0];
			mp->verts[j].modulate[1] = c[1];
			mp->verts[j].modulate[2] = c[2];
			mp->verts[j].modulate[3] = c[3];
		}
	}
}

/*
===============
CG_AddPermanentMarks

Oldest first, so newer blood blends over older (polys with the same shader
keep their submission order in the renderer). Polys outside the view frustum
are skipped (8192 polys cost ~1.4 ms a frame otherwise); if the renderer's
poly budget is still too small, the oldest go.
===============
*/
static void CG_AddPermanentMarks( void ) {
	int			i, k;
	int			polys, verts, frontPolys, frontVerts;
	markPoly_t	*mp;
	vec3_t		delta;
	float		*sp, fwd, tx, ty;
	int			dbg, r;

	cg_goreStatPerm = 0;
	cg_goreStatPermDrawn = 0;
	cg_goreStatPermMade = permSerial;
	if ( !CG_GorePersist() || !cg_blood.integer ) {
		return;
	}
	dbg = (int)CG_GoreKeepDebug();
	if ( dbg & 4 ) {
		return;
	}
	if ( !( dbg & 2 ) ) {
		CG_DryPermanentMarks();
	}

	// frustum cull with the bounding spheres (kill cam focus areas are kept for its side view)
	tx = tan( cg.refdef.fov_x * ( M_PI / 360.0f ) );
	ty = tan( cg.refdef.fov_y * ( M_PI / 360.0f ) );
	polys = verts = frontPolys = frontVerts = 0;
	for ( i = 0 ; i < permSize ; i++ ) {
		cg_permVisible[i] = 0;
		if ( !cg_permNumVerts[i] ) {
			continue;
		}
		polys++;
		sp = cg_permSphere[i];
		VectorSubtract( sp, cg.refdef.vieworg, delta );
		fwd = DotProduct( delta, cg.refdef.viewaxis[0] );
		if ( fwd < -sp[3]
			|| ( fwd > sp[3] && ( fabs( DotProduct( delta, cg.refdef.viewaxis[1] ) ) > fwd * tx + sp[3] * 1.5f
				|| fabs( DotProduct( delta, cg.refdef.viewaxis[2] ) ) > fwd * ty + sp[3] * 1.5f ) ) ) {
			if ( !CG_KillCamWantsArea( sp ) ) {
				continue;
			}
		}
		cg_permVisible[i] = 1;
		frontPolys++;
		frontVerts += cg_permNumVerts[i];
	}
	cg_goreStatPerm = polys;
	if ( !frontPolys ) {
		return;
	}

	// oldest first; over the renderer budget the oldest visible ones go
	// keep ring first (pools are the bottom layer), each ring oldest first
	r = 0;
	k = ringNext[0];
	for ( i = 0 ; i < permSize ; i++ ) {
		if ( i == ringSize[0] ) {
			r = 1;
			k = ringNext[1];
		}
		if ( cg_permVisible[k] ) {
			mp = &cg_permPolys[k];
			if ( frontPolys > permPolyBudget || frontVerts > permVertBudget ) {
				frontPolys--;
				frontVerts -= mp->poly.numVerts;
			} else {
				if ( !( dbg & 1 ) ) {
					trap_R_AddPolyToScene( mp->markShader, mp->poly.numVerts, mp->verts );
				}
				cg_goreStatPermDrawn++;
			}
		}
		if ( ++k >= ringStart[r] + ringSize[r] ) {
			k = ringStart[r];
		}
	}
}


/*
===============
CG_AddMarks
===============
*/
#define	MARK_TOTAL_TIME		10000
#define	MARK_FADE_TIME		1000

void CG_AddMarks( void ) {
	int			j;
	markPoly_t	*mp, *next;
	int			t;
	int			fade;

	// REARMED: gore particles, gibs, pools, drips (after the packet entities and their events)
	CG_GoreAddEntities();

	cg_goreStatMarks = activeMarks;
	if ( !cg_addMarks.integer ) {
		CG_GoreKeepFrame();
		return;
	}

	mp = cg_activeMarkPolys.nextMark;
	for ( ; mp != &cg_activeMarkPolys ; mp = next ) {
		// grab next now, so if the local entity is freed we
		// still have it
		next = mp->nextMark;

		// see if it is time to completely remove it
		// REARMED: cg_gorePersist keeps them (CG_AllocMark recycles the oldest at the cap);
		// the plasma glow still burns out the stock way
		if ( CG_GorePersist() && mp->markShader != cgs.media.energyMarkShader ) {
			trap_R_AddPolyToScene( mp->markShader, mp->poly.numVerts, mp->verts );
			continue;
		}
		if ( cg.time > mp->time + MARK_TOTAL_TIME ) {
			CG_FreeMarkPoly( mp );
			continue;
		}

		// fade out the energy bursts
		if ( mp->markShader == cgs.media.energyMarkShader ) {

			fade = 450 - 450 * ( (cg.time - mp->time ) / 3000.0 );
			if ( fade < 255 ) {
				if ( fade < 0 ) {
					fade = 0;
				}
				if ( mp->verts[0].modulate[0] != 0 ) {
					for ( j = 0 ; j < mp->poly.numVerts ; j++ ) {
						mp->verts[j].modulate[0] = mp->color[0] * fade;
						mp->verts[j].modulate[1] = mp->color[1] * fade;
						mp->verts[j].modulate[2] = mp->color[2] * fade;
					}
				}
			}
		}

		// fade all marks out with time
		t = mp->time + MARK_TOTAL_TIME - cg.time;
		if ( t < MARK_FADE_TIME ) {
			fade = 255 * t / MARK_FADE_TIME;
			if ( mp->alphaFade ) {
				for ( j = 0 ; j < mp->poly.numVerts ; j++ ) {
					mp->verts[j].modulate[3] = fade;
				}
			} else {
				for ( j = 0 ; j < mp->poly.numVerts ; j++ ) {
					mp->verts[j].modulate[0] = mp->color[0] * fade;
					mp->verts[j].modulate[1] = mp->color[1] * fade;
					mp->verts[j].modulate[2] = mp->color[2] * fade;
				}
			}
		}


		trap_R_AddPolyToScene( mp->markShader, mp->poly.numVerts, mp->verts );
	}

	// permanent blood goes last so the stock marks get the renderer's polys first
	CG_AddPermanentMarks();
	CG_GoreKeepFrame();
}
