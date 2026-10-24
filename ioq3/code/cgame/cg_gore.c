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
// cg_gore.c -- rearmed: decapitation, neck fountains, wall splatter, heavier gibs
//
// Everything here lives in its own pools, so it can't starve the local entity
// pool or the stock marks:
//   particles  blood drops (stretched along their velocity, they splat where
//              they land), blood mist, fireballs, smoke. Drawn as camera
//              facing polys, so they don't use up refentities.
//   gibs       meat chunks with their own physics: they bleed while flying,
//              splat on impact and stay until the map restarts (a ring).
//   pools      blood pools that grow under a body, then become permanent marks.
//   drips      blood running down walls under a splatter, kept until restart.
//   bodies     per-entity death state: neck fountains, charred smoke.
//   screen     blood on the screen, kill flash, low-health heartbeat, speed lines.
// cg_gore (0 stock .. 3 absurd) scales it all.

#include "cg_local.h"

// cg_marks.c
void CG_GoreMark( qhandle_t shader, const vec3_t origin, const vec3_t normal, const vec3_t sdir,
				 float radius, const float *rgba, int mode, int cell );
// cg_blood.c
qhandle_t CG_BloodShader( qboolean *atlas );
float CG_BloodLightLevel( const vec3_t origin, const vec3_t normal );
void CG_BloodTintColor( vec3_t tint );

#define GMARK_TEMP			0
#define GMARK_FADING		1
#define GMARK_PERMANENT		2		// spray ring
#define GMARK_KEEP			3		// keep ring (deaths: pools, seeps, overkill splatter)

static qboolean	goreKeepMarks;		// CG_GoreSplatMark / CG_GoreWallSpatter: use the keep ring

#define	GORE_GRAVITY		800

// ------------------------------------------------------------------ media

static struct {
	qboolean	loaded;
	qhandle_t	drop, mist, fire[2], smoke;
	qhandle_t	spatter, streak, drip, footprint;
	qhandle_t	splat[4];
	int			numSplats;
	qhandle_t	charred, embers;
	qhandle_t	visor, screen[3], vignette, speed[2];
	int			numScreens;
	qhandle_t	meat[6];
	int			numMeat;
	qhandle_t	head;
	sfxHandle_t	splatSnd[3], decapSnd, gibBurstSnd, heartSnd, sizzleSnd, impactSnd;
} gm;

static qboolean CG_GoreFileExists( const char *path ) {
	fileHandle_t	f;
	int				len;

	len = trap_FS_FOpenFile( path, &f, FS_READ );
	if ( f ) {
		trap_FS_FCloseFile( f );
	}
	return len > 0;
}

/*
=================
CG_GoreRegister

Shaders of optional textures (the Blender worker's splats / screen blood /
drip) are only registered when the image exists, a missing image would draw
the default checker texture.
=================
*/
static void CG_GoreRegister( void ) {
	int		i;
	char	name[MAX_QPATH];

	if ( gm.loaded ) {
		return;
	}
	gm.loaded = qtrue;

	gm.drop = trap_R_RegisterShader( "rearmed/gore/drop" );
	gm.mist = trap_R_RegisterShader( "rearmed/gore/mist" );
	gm.fire[0] = trap_R_RegisterShader( "rearmed/fx/fireball1" );
	gm.fire[1] = trap_R_RegisterShader( "rearmed/fx/fireball2" );
	gm.smoke = trap_R_RegisterShader( "rearmed/fx/smoke" );
	gm.spatter = trap_R_RegisterShader( "rearmed/gore/spatter" );
	gm.streak = trap_R_RegisterShader( "rearmed/gore/streak" );
	gm.footprint = trap_R_RegisterShader( "rearmed/gore/footprint" );
	gm.charred = trap_R_RegisterShader( "rearmed/gore/charred" );
	gm.embers = trap_R_RegisterShader( "rearmed/gore/embers" );
	gm.visor = trap_R_RegisterShader( "rearmed/gore/visor" );
	gm.vignette = trap_R_RegisterShader( "rearmed/gore/vignette" );
	gm.speed[0] = trap_R_RegisterShader( "rearmed/gore/speed1" );
	gm.speed[1] = trap_R_RegisterShader( "rearmed/gore/speed2" );

	gm.drip = CG_GoreFileExists( "gfx/rearmed/gore/drip.tga" ) ? trap_R_RegisterShader( "gfx/rearmed/gore/drip" ) : 0;
	gm.numSplats = 0;
	for ( i = 1 ; i <= 4 ; i++ ) {
		if ( CG_GoreFileExists( va( "gfx/rearmed/gore/splat%i.tga", i ) ) ) {
			gm.splat[gm.numSplats++] = trap_R_RegisterShader( va( "gfx/rearmed/gore/splat%i", i ) );
		}
	}
	gm.numScreens = 0;
	for ( i = 1 ; i <= 3 ; i++ ) {
		if ( CG_GoreFileExists( va( "gfx/rearmed/gore/screen%i.tga", i ) ) ) {
			gm.screen[gm.numScreens++] = trap_R_RegisterShader( va( "gfx/rearmed/gore/screen%i", i ) );
		}
	}

	gm.numMeat = 0;
	for ( i = 1 ; i <= 6 ; i++ ) {
		Com_sprintf( name, sizeof( name ), "models/rearmed/gibs/meat%i.md3", i );
		gm.meat[gm.numMeat] = trap_R_RegisterModel( name );
		if ( gm.meat[gm.numMeat] ) {
			gm.numMeat++;
		}
	}
	gm.head = trap_R_RegisterModel( "models/rearmed/gibs/head.md3" );

	gm.splatSnd[0] = trap_S_RegisterSound( "sound/rearmed/gore/splat1.wav", qfalse );
	gm.splatSnd[1] = trap_S_RegisterSound( "sound/rearmed/gore/splat2.wav", qfalse );
	gm.splatSnd[2] = trap_S_RegisterSound( "sound/rearmed/gore/splat3.wav", qfalse );
	gm.decapSnd = trap_S_RegisterSound( "sound/rearmed/gore/decap.wav", qfalse );
	gm.gibBurstSnd = trap_S_RegisterSound( "sound/rearmed/gore/gibburst.wav", qfalse );
	gm.heartSnd = trap_S_RegisterSound( "sound/rearmed/gore/heartbeat.wav", qfalse );
	gm.sizzleSnd = trap_S_RegisterSound( "sound/rearmed/gore/sizzle.wav", qfalse );
	gm.impactSnd = trap_S_RegisterSound( "sound/rearmed/gore/impact.wav", qfalse );
}

/*
=================
CG_GoreLevel / CG_GoreScale

cg_gore 0 = stock, 1 = light, 2 = heavy (default), 3 = absurd. cg_blood 0 turns it all off.
=================
*/
int CG_GoreLevel( void ) {
	int		g;

	if ( !cg_blood.integer ) {
		return 0;
	}
	g = cg_gore.integer;
	if ( g < 0 ) {
		g = 0;
	} else if ( g > 3 ) {
		g = 3;
	}
	return g;
}

static float CG_GoreScale( void ) {
	static const float	scale[4] = { 0, 0.45f, 1.0f, 2.2f };
	return scale[CG_GoreLevel()];
}

static int CG_GoreCount( float n ) {
	n *= CG_GoreScale();
	return (int)( n + random() );
}

// e^x for x <= 0 (the QVM has no exp)
static float CG_GoreExp( float x ) {
	float	y;
	int		i;

	if ( x < -12 ) {
		return 0;
	}
	if ( x > 0 ) {
		x = 0;
	}
	y = 1.0f + x / 64.0f;
	for ( i = 0 ; i < 6 ; i++ ) {
		y *= y;
	}
	return y;
}

static void CG_GoreRandomDir( vec3_t out ) {
	do {
		VectorSet( out, crandom(), crandom(), crandom() );
	} while ( VectorLengthSquared( out ) > 1.0f || VectorLengthSquared( out ) < 0.01f );
	VectorNormalize( out );
}

// ------------------------------------------------------------------ particles

typedef enum {
	GP_DROP,		// stretched blood drop, collides and may splat
	GP_MIST,		// blood mist puff
	GP_FIRE,		// additive fire puff, color ramps from white-hot to dark
	GP_SMOKE		// alpha blended smoke / dust
} gpType_t;

#define GPF_COLLIDE		1
#define GPF_SPLAT		2		// leave a permanent mark where it lands
#define GPF_FADEIN		4
#define GPF_BIGSPLAT	8

typedef struct {
	byte		type;
	byte		flags;
	qhandle_t	shader;
	int			start, life;
	vec3_t		org, vel;
	float		gravity, drag;
	float		size, endSize;
	float		rot, rotSpeed;
	float		rgba[4];
} gpart_t;

#define MAX_GORE_PARTICLES	1600
#define GORE_TRACE_BUDGET	140

static gpart_t	gparts[MAX_GORE_PARTICLES];
static int		numGparts;

static gpart_t *CG_GoreAllocParticle( void ) {
	gpart_t	*p;
	int		limit;

	limit = CG_GoreLevel() >= 3 ? MAX_GORE_PARTICLES : MAX_GORE_PARTICLES * 2 / 3;
	if ( numGparts >= limit ) {
		// full: recycle a random old one rather than dropping the new effect
		p = &gparts[rand() % numGparts];
	} else {
		p = &gparts[numGparts++];
	}
	memset( p, 0, sizeof( *p ) );
	p->start = cg.time;
	return p;
}

/*
=================
CG_GoreDrop

One blood drop. color: NULL = blood.
=================
*/
static void CG_GoreDrop( const vec3_t org, const vec3_t vel, float size, int life, float splatChance, const float *color ) {
	gpart_t	*p;

	p = CG_GoreAllocParticle();
	p->type = GP_DROP;
	p->flags = GPF_COLLIDE;
	if ( random() < splatChance ) {
		p->flags |= GPF_SPLAT;
	}
	p->shader = gm.drop;
	p->life = life;
	VectorCopy( org, p->org );
	VectorCopy( vel, p->vel );
	p->gravity = GORE_GRAVITY;
	p->drag = 0.25f;
	p->size = size;
	if ( color ) {
		Vector4Copy( color, p->rgba );
	} else {
		p->rgba[0] = 0.42f + 0.2f * random();
		p->rgba[1] = 0.02f;
		p->rgba[2] = 0.015f;
		p->rgba[3] = 0.95f;
	}
}

// a colored drop for other effects (water splashes): no splat
void CG_GoreParticleDrop( const vec3_t org, const vec3_t vel, float size, int life, const float *color ) {
	CG_GoreRegister();
	CG_GoreDrop( org, vel, size, life, 0, color );
}

static void CG_GoreMist( const vec3_t org, const vec3_t vel, float size, float endSize, int life, float alpha ) {
	gpart_t	*p;
	float	dark;

	p = CG_GoreAllocParticle();
	p->type = GP_MIST;
	p->shader = gm.mist;
	p->life = life;
	VectorCopy( org, p->org );
	VectorCopy( vel, p->vel );
	p->gravity = 60;
	p->drag = 2.2f;
	p->size = size;
	p->endSize = endSize;
	p->rot = random() * 360;
	p->rotSpeed = crandom() * 90;
	dark = 0.75f + 0.25f * random();
	p->rgba[0] = 0.75f * dark;
	p->rgba[1] = 0.05f * dark;
	p->rgba[2] = 0.04f * dark;
	p->rgba[3] = alpha;
}

void CG_GoreFire( const vec3_t org, const vec3_t vel, float size, float endSize, int life, const vec3_t color, float drag, float lift ) {
	gpart_t	*p;

	CG_GoreRegister();
	p = CG_GoreAllocParticle();
	p->type = GP_FIRE;
	p->shader = gm.fire[rand() & 1];
	p->life = life;
	VectorCopy( org, p->org );
	VectorCopy( vel, p->vel );
	p->gravity = -lift;
	p->drag = drag;
	p->size = size;
	p->endSize = endSize;
	p->rot = random() * 360;
	p->rotSpeed = crandom() * 120;
	VectorCopy( color, p->rgba );
	p->rgba[3] = 1;
}

void CG_GoreSmoke( const vec3_t org, const vec3_t vel, float size, float endSize, int life, float shade, float alpha, float lift, int delay ) {
	gpart_t	*p;

	CG_GoreRegister();
	p = CG_GoreAllocParticle();
	p->type = GP_SMOKE;
	p->flags = GPF_FADEIN;
	p->shader = gm.smoke;
	p->start = cg.time + delay;
	p->life = life;
	VectorCopy( org, p->org );
	VectorCopy( vel, p->vel );
	p->gravity = -lift;
	p->drag = 0.9f;
	p->size = size;
	p->endSize = endSize;
	p->rot = random() * 360;
	p->rotSpeed = crandom() * 30;
	p->rgba[0] = shade;
	p->rgba[1] = shade * 0.97f;
	p->rgba[2] = shade * 0.92f;
	p->rgba[3] = alpha;
}

// ------------------------------------------------------------------ dynamic light flashes

typedef struct {
	vec3_t	org, color;
	float	radius;
	int		start, life;
} glight_t;

#define MAX_GORE_LIGHTS	6
static glight_t	glights[MAX_GORE_LIGHTS];

void CG_GoreLight( const vec3_t org, float radius, float r, float g, float b, int life ) {
	int			i, best;
	glight_t	*l;

	best = 0;
	for ( i = 0 ; i < MAX_GORE_LIGHTS ; i++ ) {
		if ( cg.time >= glights[i].start + glights[i].life ) {
			best = i;
			break;
		}
		if ( glights[i].start < glights[best].start ) {
			best = i;
		}
	}
	l = &glights[best];
	VectorCopy( org, l->org );
	VectorSet( l->color, r, g, b );
	l->radius = radius;
	l->start = cg.time;
	l->life = life;
}

static void CG_GoreAddLights( void ) {
	int			i;
	float		f;
	glight_t	*l;

	for ( i = 0, l = glights ; i < MAX_GORE_LIGHTS ; i++, l++ ) {
		if ( !l->life || cg.time >= l->start + l->life || cg.time < l->start ) {
			continue;
		}
		f = 1.0f - (float)( cg.time - l->start ) / l->life;
		trap_R_AddLightToScene( l->org, l->radius * ( 0.4f + 0.6f * f ), l->color[0] * f, l->color[1] * f, l->color[2] * f );
	}
}

// ------------------------------------------------------------------ marks

static float CG_GoreShade( const vec3_t org, const vec3_t normal ) {
	return CG_BloodLightLevel( org, normal );
}

static qboolean CG_GoreWorldTrace( trace_t *tr, const vec3_t start, const vec3_t end ) {
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
CG_GoreSplatCrowded

REARMED (cg_gorePersist): droplet splats pile up by the hundred on the same
few spots and fill the permanent mark ring. A coarse grid (16 units) lets at
most SPLAT_PER_CELL small splats land in a cell per SPLAT_CELL_MSEC; the rest
are skipped (they would only cover blood that is already there).
=================
*/
#define SPLAT_GRID			16
#define SPLAT_HASH			4096
#define SPLAT_PER_CELL		2
#define SPLAT_CELL_MSEC		60000
#define SPLAT_SMALL			12.0f

static struct {
	int		key, count, time;
} splatCells[SPLAT_HASH];

static qboolean CG_GoreSplatCrowded( const vec3_t org, float radius ) {
	int		x, y, z, key, h;

	if ( radius >= SPLAT_SMALL ) {
		return qfalse;
	}
	x = (int)floor( org[0] / SPLAT_GRID );
	y = (int)floor( org[1] / SPLAT_GRID );
	z = (int)floor( org[2] / SPLAT_GRID );
	key = ( x * 73856093 ) ^ ( y * 19349663 ) ^ ( z * 83492791 );
	if ( !key ) {
		key = 1;
	}
	h = key & ( SPLAT_HASH - 1 );
	if ( splatCells[h].key != key || cg.time - splatCells[h].time > SPLAT_CELL_MSEC || cg.time < splatCells[h].time ) {
		splatCells[h].key = key;
		splatCells[h].count = 0;
		splatCells[h].time = cg.time;
	}
	if ( splatCells[h].count >= SPLAT_PER_CELL ) {
		return qtrue;
	}
	splatCells[h].count++;
	return qfalse;
}

/*
=================
CG_GoreSplatMark

A permanent blood splat. Uses the Blender splats when present, mixed with the
pool atlas splatter cells.
=================
*/
static void CG_GoreSplatMark( const vec3_t org, const vec3_t normal, float radius, float shadeMul ) {
	float		rgba[4], shade;
	vec3_t		tint;
	qhandle_t	shader;
	qboolean	atlas;
	int			cell;

	if ( !cg_addMarks.integer ) {
		return;
	}
	if ( CG_GorePersist() && CG_GoreSplatCrowded( org, radius ) ) {
		return;
	}
	CG_BloodTintColor( tint );
	shade = CG_GoreShade( org, normal ) * shadeMul;
	cell = -1;
	if ( gm.numSplats && random() < 0.6f ) {
		shader = gm.splat[rand() % gm.numSplats];
		// the Blender splats are painted dark: lift them a little less
		rgba[0] = 1.0f * shade;
		rgba[1] = 0.85f * shade;
		rgba[2] = 0.85f * shade;
		radius *= 1.25f;
	} else {
		shader = CG_BloodShader( &atlas );
		cell = atlas ? 2 + ( rand() & 1 ) : -1;
		rgba[0] = tint[0] * shade;
		rgba[1] = tint[1] * shade;
		rgba[2] = tint[2] * shade;
	}
	rgba[3] = 0.9f + 0.1f * random();
	CG_GoreMark( shader, org, normal, NULL, radius, rgba, goreKeepMarks ? GMARK_KEEP : GMARK_PERMANENT, cell );
}

// ------------------------------------------------------------------ drips

typedef struct {
	vec3_t		top, down, side;
	float		maxLen, width;
	int			start, dur;
	float		rgba[4];
	qhandle_t	shader;
} gdrip_t;

#define MAX_GORE_DRIPS	256
static gdrip_t	gdrips[MAX_GORE_DRIPS];
static int		dripNext, numDrips;

/*
=================
CG_GoreDrip

Blood running down a wall from start: the length is cut where the wall ends.
=================
*/
static void CG_GoreDrip( const vec3_t start, const vec3_t normal, float maxLen ) {
	gdrip_t	*d;
	vec3_t	down, side, a, b, end;
	trace_t	tr;
	float	len, shade;
	vec3_t	tint;
	int		step;

	if ( fabs( normal[2] ) > 0.6f ) {
		return;		// floors and ceilings don't drip
	}
	// "down" along the wall surface
	VectorSet( down, 0, 0, -1 );
	VectorMA( down, -DotProduct( down, normal ), normal, down );
	if ( VectorNormalize( down ) < 0.1f ) {
		return;
	}
	CrossProduct( normal, down, side );

	// walk down the wall in steps, stop where it ends or something is in the way
	len = 0;
	for ( step = 8 ; step <= maxLen ; step += 8 ) {
		VectorMA( start, step, down, a );
		VectorMA( a, 1.5f, normal, a );
		VectorMA( a, -4.0f, normal, b );
		trap_CM_BoxTrace( &tr, a, b, NULL, NULL, 0, MASK_SOLID );
		if ( tr.fraction >= 1.0f || tr.startsolid ) {
			break;
		}
		VectorMA( start, len, down, end );
		VectorMA( end, 1.5f, normal, end );
		trap_CM_BoxTrace( &tr, end, a, NULL, NULL, 0, MASK_SOLID );
		if ( tr.fraction < 1.0f ) {
			break;
		}
		len = step;
	}
	if ( len < 8 ) {
		return;
	}

	d = &gdrips[dripNext];
	dripNext = ( dripNext + 1 ) % MAX_GORE_DRIPS;
	if ( numDrips < MAX_GORE_DRIPS ) {
		numDrips++;
	}
	VectorMA( start, 0.4f, normal, d->top );
	VectorCopy( down, d->down );
	VectorCopy( side, d->side );
	d->maxLen = len;
	d->width = 0.9f + 1.4f * random();
	d->start = cg.time + (int)( random() * 250 );
	d->dur = 1800 + (int)( len * 45 * ( 0.7f + 0.6f * random() ) );
	CG_BloodTintColor( tint );
	shade = CG_GoreShade( start, normal );
	d->rgba[0] = tint[0] * shade;
	d->rgba[1] = tint[1] * shade;
	d->rgba[2] = tint[2] * shade;
	d->rgba[3] = 0.95f;
	d->shader = ( gm.drip && random() < 0.35f ) ? gm.drip : gm.streak;
	if ( d->shader == gm.drip ) {
		d->width *= 3.5f;		// the Blender strip has three runs across it
	}
}

static void CG_GoreAddDrips( void ) {
	int			i, j;
	gdrip_t		*d;
	polyVert_t	v[4];
	float		f, len;
	vec3_t		bottom, delta;
	byte		c[4];

	for ( i = 0, d = gdrips ; i < numDrips ; i++, d++ ) {
		if ( cg.time < d->start ) {
			continue;
		}
		f = (float)( cg.time - d->start ) / d->dur;
		if ( !CG_GorePersist() && f > 3.0f ) {
			continue;
		}
		if ( f > 1.0f ) {
			f = 1.0f;
		}
		// fast at first, then crawling
		f = 1.0f - ( 1.0f - f ) * ( 1.0f - f );
		len = 3 + ( d->maxLen - 3 ) * f;

		VectorSubtract( d->top, cg.refdef.vieworg, delta );
		if ( DotProduct( delta, cg.refdef.viewaxis[0] ) < -d->maxLen - 16 ) {
			continue;
		}
		VectorMA( d->top, len, d->down, bottom );
		for ( j = 0 ; j < 4 ; j++ ) {
			c[j] = (byte)( 255 * d->rgba[j] );
		}
		VectorMA( d->top, -d->width, d->side, v[0].xyz );
		VectorMA( d->top, d->width, d->side, v[1].xyz );
		VectorMA( bottom, d->width, d->side, v[2].xyz );
		VectorMA( bottom, -d->width, d->side, v[3].xyz );
		v[0].st[0] = 0; v[0].st[1] = 0;
		v[1].st[0] = 1; v[1].st[1] = 0;
		v[2].st[0] = 1; v[2].st[1] = 1;
		v[3].st[0] = 0; v[3].st[1] = 1;
		for ( j = 0 ; j < 4 ; j++ ) {
			v[j].modulate[0] = c[0];
			v[j].modulate[1] = c[1];
			v[j].modulate[2] = c[2];
			v[j].modulate[3] = c[3];
		}
		trap_R_AddPolyToScene( d->shader, 4, v );
	}
}

// ------------------------------------------------------------------ growing pools, footprints

typedef struct {
	vec3_t		org, normal;
	float		radius;
	int			start, dur;
	int			cell;
	float		rgba[4];
	qhandle_t	shader;
	qboolean	active;
} gpool_t;

#define MAX_GROW_POOLS		24
#define MAX_RECENT_POOLS	32

static gpool_t	gpools[MAX_GROW_POOLS];
static gpool_t	recentPools[MAX_RECENT_POOLS];		// for bloody footprints
static int		recentNext;

// REARMED (cg_gorePersist): a finished pool keeps seeping out twice, wider and darker
typedef struct {
	gpool_t		pool;
	int			time;			// next step, 0 = free
	int			stage;
} gseep_t;

#define MAX_SEEPS			48
static gseep_t	gseeps[MAX_SEEPS];

static void CG_GoreSeepStart( const gpool_t *p ) {
	int		i;

	if ( !CG_GorePersist() ) {
		return;
	}
	for ( i = 0 ; i < MAX_SEEPS ; i++ ) {
		if ( !gseeps[i].time ) {
			gseeps[i].pool = *p;
			gseeps[i].stage = 0;
			gseeps[i].time = cg.time + 6000 + (int)( 4000 * random() );
			return;
		}
	}
}

static void CG_GoreAddSeeps( void ) {
	int		i;
	gseep_t	*sp;
	float	rgba[4], scale;

	for ( i = 0, sp = gseeps ; i < MAX_SEEPS ; i++, sp++ ) {
		if ( !sp->time || cg.time < sp->time ) {
			continue;
		}
		if ( !CG_GorePersist() ) {
			sp->time = 0;
			continue;
		}
		scale = sp->stage ? 1.55f : 1.28f;
		rgba[0] = sp->pool.rgba[0] * 0.8f;
		rgba[1] = sp->pool.rgba[1] * 0.8f;
		rgba[2] = sp->pool.rgba[2] * 0.8f;
		rgba[3] = sp->pool.rgba[3] * ( sp->stage ? 0.32f : 0.45f );
		CG_GoreMark( sp->pool.shader, sp->pool.org, sp->pool.normal, NULL, sp->pool.radius * scale, rgba,
			GMARK_KEEP, sp->pool.cell );
		sp->stage++;
		sp->time = sp->stage >= 2 ? 0 : cg.time + 18000 + (int)( 12000 * random() );
	}
}

/*
=================
CG_GorePool

A pool that spreads over dur msec, then turns into a permanent mark.
Called by cg_blood.c for the pools under dead bodies.
=================
*/
void CG_GorePool( qhandle_t shader, const vec3_t org, const vec3_t normal, float radius, int cell,
				 const float *rgba, int delay, int dur ) {
	int		i;
	gpool_t	*p;

	p = NULL;
	for ( i = 0 ; i < MAX_GROW_POOLS ; i++ ) {
		if ( !gpools[i].active ) {
			p = &gpools[i];
			break;
		}
	}
	if ( !p ) {
		CG_GoreMark( shader, org, normal, NULL, radius, rgba, GMARK_KEEP, cell );
		return;
	}
	p->active = qtrue;
	VectorCopy( org, p->org );
	VectorCopy( normal, p->normal );
	p->radius = radius;
	p->cell = cell;
	p->shader = shader;
	Vector4Copy( rgba, p->rgba );
	p->start = cg.time + delay;
	p->dur = dur;

	recentPools[recentNext] = *p;
	recentNext = ( recentNext + 1 ) % MAX_RECENT_POOLS;
}

static void CG_GoreAddPools( void ) {
	int		i;
	gpool_t	*p;
	float	f, r;

	for ( i = 0, p = gpools ; i < MAX_GROW_POOLS ; i++, p++ ) {
		if ( !p->active || cg.time < p->start ) {
			continue;
		}
		f = (float)( cg.time - p->start ) / p->dur;
		if ( f >= 1.0f ) {
			p->active = qfalse;
			CG_GoreMark( p->shader, p->org, p->normal, NULL, p->radius, p->rgba, GMARK_KEEP, p->cell );
			CG_GoreSeepStart( p );
			continue;
		}
		r = p->radius * ( 0.12f + 0.88f * ( 1.0f - ( 1.0f - f ) * ( 1.0f - f ) * ( 1.0f - f ) ) );
		CG_GoreMark( p->shader, p->org, p->normal, NULL, r, p->rgba, GMARK_TEMP, p->cell );
	}
	CG_GoreAddSeeps();
}

// per client: bloody boots
typedef struct {
	int		steps;			// prints left
	vec3_t	last;
	int		side;
	qboolean	valid;
} gfeet_t;

static gfeet_t	gfeet[MAX_CLIENTS];

static void CG_GoreFootprints( centity_t *cent ) {
	gfeet_t		*ft;
	vec3_t		feet, delta, fwd, p, end, sdir;
	trace_t		tr;
	int			i;
	float		d, rgba[4], shade;
	gpool_t		*pool;

	if ( cent->currentState.clientNum < 0 || cent->currentState.clientNum >= MAX_CLIENTS ) {
		return;
	}
	ft = &gfeet[cent->currentState.clientNum];
	VectorCopy( cent->lerpOrigin, feet );
	feet[2] -= 23;

	if ( cent->currentState.groundEntityNum == ENTITYNUM_NONE ) {
		ft->valid = qfalse;
		return;
	}

	// stepping in a pool soaks the boots
	for ( i = 0, pool = recentPools ; i < MAX_RECENT_POOLS ; i++, pool++ ) {
		if ( !pool->radius || cg.time < pool->start + 400 ) {
			continue;
		}
		if ( fabs( pool->org[2] - feet[2] ) > 18 ) {
			continue;
		}
		delta[0] = pool->org[0] - feet[0];
		delta[1] = pool->org[1] - feet[1];
		if ( delta[0] * delta[0] + delta[1] * delta[1] < pool->radius * pool->radius * 0.5f ) {
			ft->steps = 6 + 4 * CG_GoreLevel();
			break;
		}
	}

	if ( !ft->valid ) {
		VectorCopy( feet, ft->last );
		ft->valid = qtrue;
		return;
	}
	VectorSubtract( feet, ft->last, delta );
	delta[2] = 0;
	d = VectorLength( delta );
	if ( d < 30 ) {
		return;
	}
	VectorCopy( feet, ft->last );
	if ( ft->steps <= 0 || d > 120 ) {
		return;		// teleports and respawns aren't steps
	}
	VectorScale( delta, 1.0f / d, fwd );
	ft->side ^= 1;
	VectorCopy( feet, p );
	p[0] += -fwd[1] * ( ft->side ? 5 : -5 );
	p[1] += fwd[0] * ( ft->side ? 5 : -5 );
	p[2] += 8;
	VectorCopy( p, end );
	end[2] -= 32;
	if ( !CG_GoreWorldTrace( &tr, p, end ) || tr.plane.normal[2] < 0.7f ) {
		return;
	}
	// texture t runs toe -> heel, so the s axis is sideways
	sdir[0] = -fwd[1];
	sdir[1] = fwd[0];
	sdir[2] = 0;
	shade = CG_GoreShade( tr.endpos, tr.plane.normal );
	rgba[0] = shade;
	rgba[1] = shade;
	rgba[2] = shade;
	rgba[3] = 0.3f + 0.07f * ft->steps;
	if ( rgba[3] > 0.9f ) {
		rgba[3] = 0.9f;
	}
	CG_GoreMark( gm.footprint, tr.endpos, tr.plane.normal, sdir, 10.0f, rgba, GMARK_FADING, -1 );
	ft->steps--;
}

// ------------------------------------------------------------------ gibs

#define GIBF_HEAD		1
#define GIBF_SETTLED	2
#define GIBF_CHARRED	4

typedef struct {
	qhandle_t	model;
	int			flags;
	int			start;
	vec3_t		org, vel, angles, avel;
	float		scale;
	float		bleed;
	int			nextTrail, lastHit, settleTime;
	qboolean	active;
} ggib_t;

#define MAX_GORE_GIBS		256
#define GIB_STOCK_MAX		200			// the pool before cg_gorePersist
#define GIB_PER_DEATH		4			// x cg_gorePersistMax with cg_gorePersist
#define GIB_PERSIST_TIME	12000		// without cg_gorePersist
#define GIB_DRAW_MAX		200

static ggib_t	ggibs[MAX_GORE_GIBS];
static int		gibNext;
static int		gibSoundTime;

static int CG_GoreGibLimit( void ) {
	static const int	limit[4] = { 0, 48, 128, GIB_STOCK_MAX };
	int		n, cap;

	n = limit[CG_GoreLevel()];
	if ( CG_GorePersist() && n > 0 ) {
		// kept until the map ends: the cap follows cg_gorePersistMax (heavy gore gets the full pool)
		cap = CG_GoreKeepCap() * GIB_PER_DEATH;
		if ( CG_GoreLevel() >= 2 ) {
			n = cap;
		} else if ( n > cap ) {
			n = cap;
		}
		if ( n > MAX_GORE_GIBS ) {
			n = MAX_GORE_GIBS;
		}
	}
	return n;
}

static ggib_t *CG_GoreLaunchGib( qhandle_t model, const vec3_t org, const vec3_t vel, float scale, float bleed, int flags ) {
	ggib_t	*g;
	int		limit, tries;

	if ( !model ) {
		return NULL;
	}
	limit = CG_GoreGibLimit();
	if ( limit <= 0 ) {
		return NULL;
	}
	if ( gibNext >= limit ) {
		gibNext = 0;
	}
	// REARMED (cg_gorePersist): oldest first, but skip a settled chunk the player is looking at
	for ( tries = 0 ; tries < 8 ; tries++ ) {
		g = &ggibs[( gibNext + tries ) % limit];
		if ( !g->active || !( g->flags & GIBF_SETTLED ) || !CG_GoreInView( g->org, 12 ) ) {
			break;
		}
	}
	if ( tries == 8 ) {
		tries = 0;
	}
	if ( tries ) {
		// swap the chosen old chunk into the ring slot, so the ring order stays oldest first
		ggib_t	tmp;

		tmp = ggibs[gibNext];
		ggibs[gibNext] = ggibs[( gibNext + tries ) % limit];
		ggibs[( gibNext + tries ) % limit] = tmp;
	}
	g = &ggibs[gibNext];
	gibNext++;
	memset( g, 0, sizeof( *g ) );
	g->active = qtrue;
	g->model = model;
	g->flags = flags;
	g->start = cg.time;
	VectorCopy( org, g->org );
	VectorCopy( vel, g->vel );
	VectorSet( g->angles, random() * 360, random() * 360, random() * 360 );
	VectorSet( g->avel, crandom() * 540, crandom() * 540, crandom() * 360 );
	g->scale = scale;
	g->bleed = bleed;
	g->nextTrail = cg.time;
	return g;
}

static qhandle_t CG_GoreMeat( void ) {
	if ( gm.numMeat ) {
		return gm.meat[rand() % gm.numMeat];
	}
	return ( rand() & 1 ) ? cgs.media.gibIntestine : cgs.media.gibAbdomen;
}

static void CG_GoreGibHit( ggib_t *g, trace_t *tr, float impact ) {
	vec3_t	vel;
	int		i, n;

	if ( cg.time - g->lastHit < 90 ) {
		return;
	}
	g->lastHit = cg.time;
	if ( impact > 110 && CG_GorePersist() ) {
		if ( tr->plane.normal[2] > 0.6f || random() < 0.7f ) {
			CG_GoreSplatMark( tr->endpos, tr->plane.normal, 6 + impact * 0.018f * g->scale, 1.0f );
		}
		if ( tr->plane.normal[2] < 0.3f && tr->plane.normal[2] > -0.3f && random() < 0.5f ) {
			CG_GoreDrip( tr->endpos, tr->plane.normal, 24 + 40 * random() );
		}
	}
	if ( impact > 160 ) {
		n = 2 + (int)( impact / 120 );
		for ( i = 0 ; i < n ; i++ ) {
			VectorScale( tr->plane.normal, 60 + random() * 90, vel );
			vel[0] += crandom() * 80;
			vel[1] += crandom() * 80;
			vel[2] += 40;
			CG_GoreDrop( tr->endpos, vel, 1.0f + random(), 800, 0.15f, NULL );
		}
	}
	if ( impact > 220 && cg.time - gibSoundTime > 60 ) {
		gibSoundTime = cg.time;
		n = rand() % 3;
		CG_FxSound( tr->endpos, ENTITYNUM_WORLD, CHAN_AUTO,
			random() < 0.5f ? gm.splatSnd[n] :
			n == 0 ? cgs.media.gibBounce1Sound : n == 1 ? cgs.media.gibBounce2Sound : cgs.media.gibBounce3Sound );
	}
}

static void CG_GoreMoveGib( ggib_t *g, float dt ) {
	vec3_t	end, vel;
	trace_t	tr;
	float	impact, speed;
	static const vec3_t	mins = { -3, -3, -3 };
	static const vec3_t	maxs = { 3, 3, 3 };

	g->vel[2] -= GORE_GRAVITY * dt;
	VectorMA( g->org, dt, g->vel, end );
	trap_CM_BoxTrace( &tr, g->org, end, mins, maxs, 0, MASK_SOLID );
	if ( tr.startsolid || tr.allsolid ) {
		g->flags |= GIBF_SETTLED;
		g->settleTime = cg.time;
		return;
	}
	if ( tr.fraction < 1.0f ) {
		impact = -DotProduct( g->vel, tr.plane.normal );
		CG_GoreGibHit( g, &tr, impact );
		// bounce: lose most of the normal speed, rub off some tangent speed
		VectorCopy( g->vel, vel );
		VectorMA( vel, impact * 1.3f, tr.plane.normal, g->vel );
		VectorScale( g->vel, 0.62f, g->vel );
		VectorScale( g->avel, 0.55f, g->avel );
		VectorMA( tr.endpos, 0.25f, tr.plane.normal, g->org );
		speed = VectorLength( g->vel );
		if ( tr.plane.normal[2] > 0.6f && speed < 70 ) {
			g->flags |= GIBF_SETTLED;
			g->settleTime = cg.time;
			VectorClear( g->vel );
			// lie flat-ish
			g->angles[PITCH] = ( rand() & 1 ) ? 0 : 180;
			g->angles[ROLL] = crandom() * 20;
		}
		return;
	}
	VectorCopy( end, g->org );
	VectorMA( g->angles, dt, g->avel, g->angles );
}

static void CG_GoreAddGibs( float dt ) {
	int			i, drawn;
	ggib_t		*g;
	refEntity_t	re;
	vec3_t		delta, vel, axis[3];
	float		dist;

	drawn = 0;
	cg_goreStatGibs = 0;
	for ( i = 0, g = ggibs ; i < MAX_GORE_GIBS ; i++, g++ ) {
		if ( !g->active ) {
			continue;
		}
		cg_goreStatGibs++;
		if ( !( g->flags & GIBF_SETTLED ) ) {
			CG_GoreMoveGib( g, dt );
			// blood trail while flying
			if ( g->bleed > 0 && cg.time >= g->nextTrail ) {
				g->nextTrail = cg.time + 22 + (int)( 30 * random() );
				VectorScale( g->vel, 0.15f, vel );
				vel[0] += crandom() * 30;
				vel[1] += crandom() * 30;
				vel[2] += crandom() * 30;
				CG_GoreDrop( g->org, vel, 1.2f + 1.2f * g->bleed, 1200, 0.35f * g->bleed, NULL );
				if ( ( g->flags & GIBF_HEAD ) && random() < 0.4f ) {
					CG_GoreMist( g->org, vec3_origin, 3, 10, 450, 0.35f );
				}
				g->bleed -= 0.012f;
			}
		} else if ( !CG_GorePersist() && cg.time - g->settleTime > GIB_PERSIST_TIME ) {
			// sink into the floor, then go
			g->org[2] -= dt * 6;
			if ( cg.time - g->settleTime > GIB_PERSIST_TIME + 2500 ) {
				g->active = qfalse;
				continue;
			}
		}

		VectorSubtract( g->org, cg.refdef.vieworg, delta );
		dist = DotProduct( delta, cg.refdef.viewaxis[0] );
		if ( ( dist < -24 && !CG_KillCamWantsArea( g->org ) ) || drawn >= GIB_DRAW_MAX ) {
			continue;
		}
		if ( ( g->flags & GIBF_SETTLED ) && !CG_GoreInView( g->org, 16 ) && !CG_KillCamWantsArea( g->org ) ) {
			continue;		// resting chunks outside the view cost nothing
		}
		if ( VectorLengthSquared( delta ) > 3500.0f * 3500.0f ) {
			continue;
		}
		drawn++;
		memset( &re, 0, sizeof( re ) );
		re.reType = RT_MODEL;
		re.hModel = g->model;
		VectorCopy( g->org, re.origin );
		VectorCopy( g->org, re.oldorigin );
		VectorCopy( g->org, re.lightingOrigin );
		AnglesToAxis( g->angles, axis );
		VectorScale( axis[0], g->scale, re.axis[0] );
		VectorScale( axis[1], g->scale, re.axis[1] );
		VectorScale( axis[2], g->scale, re.axis[2] );
		re.nonNormalizedAxes = ( g->scale != 1.0f );
		trap_R_AddRefEntityToScene( &re );
		if ( g->flags & GIBF_CHARRED ) {
			re.customShader = gm.charred;
			re.shaderRGBA[0] = re.shaderRGBA[1] = re.shaderRGBA[2] = re.shaderRGBA[3] = 255;
			trap_R_AddRefEntityToScene( &re );
		}
	}
	cg_goreStatGibsDrawn = drawn;
}

// ------------------------------------------------------------------ screen blood

typedef struct {
	float		x, y, w, h;
	float		s0, t0, s1, t1;
	float		run;
	int			start, life;
	qhandle_t	shader;
	float		shade;
} gscreen_t;

#define MAX_SCREEN_BLOOD	10
static gscreen_t	gscreen[MAX_SCREEN_BLOOD];
static int			screenNext;

/*
=================
CG_GoreScreenBlood

amount 0..1: how much blood hits the screen. Splats land away from the
center, then run down and fade.
=================
*/
void CG_GoreScreenBlood( float amount ) {
	int			i, n, cell;
	float		W, H, size;
	gscreen_t	*s;

	if ( !cg_screenFX.integer || !cg_bloodScreen.integer || CG_GoreLevel() <= 0 ) {
		return;
	}
	CG_GoreRegister();
	if ( amount > 1 ) {
		amount = 1;
	}
	W = cgs.glconfig.vidWidth;
	H = cgs.glconfig.vidHeight;
	n = 1 + (int)( amount * ( 0.5f + CG_GoreLevel() ) + random() * 0.6f );
	for ( i = 0 ; i < n ; i++ ) {
		s = &gscreen[screenNext];
		screenNext = ( screenNext + 1 ) % MAX_SCREEN_BLOOD;
		size = H * ( 0.16f + 0.2f * amount ) * ( 0.7f + 0.6f * random() );
		s->w = size;
		s->h = size;
		// keep the middle third of the screen clear
		do {
			s->x = W * random();
			s->y = H * ( -0.05f + 0.85f * random() );
		} while ( fabs( s->x - W * 0.5f ) < H * 0.3f && fabs( s->y - H * 0.5f ) < H * 0.22f );
		if ( gm.numScreens && random() < 0.75f ) {
			s->shader = gm.screen[rand() % gm.numScreens];
			s->s0 = 0; s->t0 = 0; s->s1 = 1; s->t1 = 1;
			s->shade = 0.85f + 0.15f * amount;
		} else {
			s->shader = gm.visor;
			cell = rand() & 3;
			s->s0 = ( cell & 1 ) * 0.5f;
			s->t0 = ( cell >> 1 ) * 0.5f;
			s->s1 = s->s0 + 0.5f;
			s->t1 = s->t0 + 0.5f;
			s->shade = 0.7f + 0.2f * amount;
		}
		if ( rand() & 1 ) {		// mirror
			float t = s->s0;
			s->s0 = s->s1;
			s->s1 = t;
		}
		s->run = 0.08f + 0.18f * random();
		s->start = cg.time + i * 25;
		s->life = 2200 + (int)( 1600 * random() + 1200 * amount );
	}
}

static void CG_GoreDrawScreenBlood( void ) {
	int			i;
	gscreen_t	*s;
	float		t, a, y, h, col[4];

	for ( i = 0, s = gscreen ; i < MAX_SCREEN_BLOOD ; i++, s++ ) {
		if ( !s->life || cg.time < s->start || cg.time >= s->start + s->life ) {
			continue;
		}
		t = (float)( cg.time - s->start ) / s->life;
		if ( t < 0.04f ) {
			a = t / 0.04f;
		} else if ( t < 0.45f ) {
			a = 1.0f;
		} else {
			a = 1.0f - ( t - 0.45f ) / 0.55f;
		}
		a = a * a * s->shade * 0.75f;
		h = s->h * ( 1.0f + 0.35f * t );
		y = s->y + s->run * t * t * cgs.glconfig.vidHeight;
		col[0] = col[1] = col[2] = 0.8f;
		col[3] = a;
		trap_R_SetColor( col );
		trap_R_DrawStretchPic( s->x - s->w * 0.5f, y - s->h * 0.5f, s->w, h, s->s0, s->t0, s->s1, s->t1, s->shader );
	}
	trap_R_SetColor( NULL );
}

// ------------------------------------------------------------------ kill flash, low health, speed lines

static int		killFlashTime;
static float	killFlashAmount;
static int		heartTime, heartNext;

/*
=================
CG_GoreKillFlash

The local player made a big kill: a white/red flash for a few frames, a heavy
thump and a camera punch.
=================
*/
void CG_GoreKillFlash( float amount ) {
	if ( !cg_screenFX.integer || !cg_killFlash.integer ) {
		return;
	}
	CG_GoreRegister();
	if ( cg.time - killFlashTime < 120 && amount <= killFlashAmount ) {
		return;
	}
	killFlashTime = cg.time;
	killFlashAmount = amount;
	trap_S_StartLocalSound( gm.impactSnd, CHAN_LOCAL_SOUND );
	CG_FxPunch( 1.5f + 2.5f * amount );
}

static void CG_GoreDrawKillFlash( void ) {
	float	t, a, col[4];

	t = cg.time - killFlashTime;
	if ( !killFlashTime || t < 0 || t > 180 ) {
		return;
	}
	t /= 180.0f;
	a = ( 1.0f - t ) * ( 1.0f - t );
	// a bright white frame that turns into a red rim
	col[0] = 1.0f;
	col[1] = 0.95f - 0.6f * t;
	col[2] = 0.9f - 0.7f * t;
	col[3] = t < 0.3f ? a * 0.22f * killFlashAmount : 0;
	trap_R_SetColor( col );
	trap_R_DrawStretchPic( 0, 0, cgs.glconfig.vidWidth, cgs.glconfig.vidHeight, 0, 0, 1, 1, cgs.media.whiteShader );
	col[0] = 0.8f; col[1] = 0.02f; col[2] = 0.0f;
	col[3] = a * 0.3f * killFlashAmount;
	trap_R_SetColor( col );
	trap_R_DrawStretchPic( 0, 0, cgs.glconfig.vidWidth, cgs.glconfig.vidHeight, 0, 0, 1, 1, gm.vignette );
	trap_R_SetColor( NULL );
}

static void CG_GoreDrawLowHealth( void ) {
	playerState_t	*ps = &cg.snap->ps;
	int				health, interval, t;
	float			f, pulse, col[4];

	health = ps->stats[STAT_HEALTH];
	if ( !cg_lowHealthFX.integer || health <= 0 || health > 35 || ps->persistant[PERS_TEAM] == TEAM_SPECTATOR
		|| ps->pm_type == PM_INTERMISSION || ( ps->pm_flags & PMF_FOLLOW ) ) {
		heartNext = 0;
		return;
	}
	f = 1.0f - health / 35.0f;
	interval = 1050 - (int)( 500 * f );
	if ( !heartNext || cg.time >= heartNext || heartNext - cg.time > 2000 ) {
		if ( heartNext && cg.time >= heartNext ) {
			trap_S_StartLocalSound( gm.heartSnd, CHAN_LOCAL_SOUND );
			heartTime = cg.time;
		}
		heartNext = cg.time + interval;
	}
	t = cg.time - heartTime;
	pulse = CG_GoreExp( -t / 110.0f );
	if ( t > 220 ) {
		pulse += 0.65f * CG_GoreExp( -( t - 220 ) / 130.0f );
	}
	col[0] = 0.45f; col[1] = 0.0f; col[2] = 0.0f;
	col[3] = ( 0.3f + 0.4f * f ) * ( 0.55f + 0.45f * pulse );
	trap_R_SetColor( col );
	trap_R_DrawStretchPic( 0, 0, cgs.glconfig.vidWidth, cgs.glconfig.vidHeight, 0, 0, 1, 1, gm.vignette );
	trap_R_SetColor( NULL );
}

static void CG_GoreDrawSpeedLines( void ) {
	float	speed, a, col[4];

	if ( !cg_speedLines.integer || cg.snap->ps.pm_type != PM_NORMAL || cg.renderingThirdPerson ) {
		return;
	}
	speed = VectorLength( cg.predictedPlayerState.velocity );
	a = ( speed - 600 ) / 700.0f;
	if ( a <= 0 ) {
		return;
	}
	if ( a > 1 ) {
		a = 1;
	}
	col[0] = col[1] = col[2] = 1.0f;
	col[3] = a * 0.15f;		// hud pass: calmer (was 0.22)
	trap_R_SetColor( col );
	trap_R_DrawStretchPic( 0, 0, cgs.glconfig.vidWidth, cgs.glconfig.vidHeight, 0, 0, 1, 1,
		gm.speed[( cg.time / 45 ) & 1] );
	trap_R_SetColor( NULL );
}

/*
=================
CG_GoreDrawScreen

2D overlays, drawn under the HUD (cg_draw.c).
=================
*/
void CG_GoreDrawScreen( void ) {
	CG_GoreRegister();
	if ( !cg_screenFX.integer ) {
		return;
	}
	CG_GoreDrawScreenBlood();
	CG_GoreDrawLowHealth();
	CG_GoreDrawSpeedLines();
	CG_GoreDrawKillFlash();
}

// ------------------------------------------------------------------ per body state

typedef struct {
	int		deathTime;
	int		flags;
	int		lastSeen;
	int		fxTime;
	int		killer;
} gbody_t;

static gbody_t	gbodies[MAX_GENTITIES];
static vec3_t	headPos[MAX_CLIENTS];
static int		headTime[MAX_CLIENTS];

// the last blood hit on each entity, so a death in the same frame knows the shot direction
typedef struct {
	int		time;
	vec3_t	point, dir;
	int		damage, flags, mod, attacker;
} ghit_t;

static ghit_t	lastHit[MAX_GENTITIES];

typedef struct {
	int		entnum, killer, flags;
	qboolean	gib;
	vec3_t	origin;
} gdeath_t;

#define MAX_PENDING_DEATHS	16
static gdeath_t	pendingDeaths[MAX_PENDING_DEATHS];
static int		numPendingDeaths;

static gbody_t *CG_GoreBody( centity_t *cent ) {
	gbody_t	*b, *owner;
	int		num, flags;

	num = cent->currentState.number;
	b = &gbodies[num];
	flags = cent->currentState.generic1 & RM_BODY_ALL;
	if ( b->flags != flags || cg.time - b->lastSeen > 1500 ) {
		// a new body in this slot (the body queue reuses entities)
		b->flags = flags;
		b->fxTime = 0;
		b->deathTime = cg.time - 20000;
		if ( num >= MAX_CLIENTS ) {
			owner = &gbodies[cent->currentState.clientNum & ( MAX_CLIENTS - 1 )];
			if ( owner->flags == flags && cg.time - owner->deathTime < 15000 ) {
				b->deathTime = owner->deathTime;
			}
		}
	}
	b->lastSeen = cg.time;
	return b;
}

// ------------------------------------------------------------------ hits

static qboolean CG_GoreModBleeds( int mod ) {
	switch ( mod ) {
	case MOD_WATER:
	case MOD_SLIME:
	case MOD_LAVA:
	case MOD_BURN:
	case MOD_TRIGGER_HURT:
	case MOD_SUICIDE:
		return qfalse;
	default:
		return qtrue;
	}
}

static qboolean CG_GoreModExplosive( int mod ) {
	switch ( mod ) {
	case MOD_GRENADE:
	case MOD_GRENADE_SPLASH:
	case MOD_ROCKET:
	case MOD_ROCKET_SPLASH:
	case MOD_BFG:
	case MOD_BFG_SPLASH:
	case MOD_PLASMA_SPLASH:
	case MOD_PROXIMITY_MINE:
	case MOD_PROP:
	case MOD_METEOR:
	case MOD_CORPSE:
	case MOD_SLAM:
		return qtrue;
	default:
		return qfalse;
	}
}

/*
=================
CG_GoreWallSpatter

Blood thrown against a surface along dir: a directional spatter, and on walls
some runs of blood down from it.
=================
*/
void CG_GoreWallSpatter( const vec3_t org, const vec3_t normal, const vec3_t dir, float radius, int drips ) {
	vec3_t		sdir, tint, start;
	float		rgba[4], shade, along;
	int			i;
	qboolean	atlas;

	if ( !cg_addMarks.integer || CG_GoreLevel() <= 0 ) {
		return;
	}
	CG_GoreRegister();
	CG_BloodTintColor( tint );
	shade = CG_GoreShade( org, normal );
	rgba[0] = tint[0] * shade;
	rgba[1] = tint[1] * shade;
	rgba[2] = tint[2] * shade;
	rgba[3] = 0.95f;

	// the streaks run along the part of the shot that slides along the surface
	VectorMA( dir, -DotProduct( dir, normal ), normal, sdir );
	along = VectorNormalize( sdir );
	if ( along > 0.25f ) {
		// the impact sits at s = 0.3 of the cell: shift the mark forward so it lands on org
		VectorMA( org, radius * 0.4f, sdir, start );
		CG_GoreMark( gm.spatter, start, normal, sdir, radius, rgba, goreKeepMarks ? GMARK_KEEP : GMARK_PERMANENT, rand() & 3 );
	} else if ( gm.numSplats ) {
		CG_GoreMark( gm.splat[rand() % gm.numSplats], org, normal, NULL, radius * 0.8f, rgba, goreKeepMarks ? GMARK_KEEP : GMARK_PERMANENT, -1 );
	} else {
		CG_GoreMark( CG_BloodShader( &atlas ), org, normal, NULL, radius * 0.8f, rgba, goreKeepMarks ? GMARK_KEEP : GMARK_PERMANENT, atlas ? 2 + ( rand() & 1 ) : -1 );
	}

	for ( i = 0 ; i < drips ; i++ ) {
		VectorCopy( org, start );
		start[0] += crandom() * radius * 0.35f;
		start[1] += crandom() * radius * 0.35f;
		start[2] += crandom() * radius * 0.25f;
		CG_GoreDrip( start, normal, 20 + random() * ( 40 + radius * 1.5f ) );
	}
}

/*
=================
CG_GoreSpray

Blood out of a wound: drops in a cone around dir, a little mist, and blood on
the wall behind.
=================
*/
static void CG_GoreSpray( const vec3_t point, const vec3_t dir, int damage, qboolean headshot, qboolean explosive ) {
	int		i, n;
	vec3_t	vel, d, end;
	float	speed, cone, size, dist;
	trace_t	tr;

	n = CG_GoreCount( 4 + damage / 7.0f );
	if ( headshot ) {
		n = n * 3 / 2 + 2;
	}
	if ( n > 50 ) {
		n = 50;
	}
	cone = explosive ? 0.9f : 0.35f;
	for ( i = 0 ; i < n ; i++ ) {
		CG_GoreRandomDir( d );
		VectorMA( dir, cone, d, vel );
		VectorNormalize( vel );
		speed = 120 + random() * 260 + damage * 1.6f;
		VectorScale( vel, speed, vel );
		vel[2] += 40 + 60 * random();
		size = 1.0f + random() * 1.4f + damage * 0.008f;
		CG_GoreDrop( point, vel, size, 1400, 0.22f, NULL );
	}
	// a few slow fat drops falling off the wound
	for ( i = 0 ; i < 2 ; i++ ) {
		VectorSet( vel, crandom() * 40, crandom() * 40, 20 + random() * 40 );
		CG_GoreDrop( point, vel, 2.0f + random(), 1400, 0.4f, NULL );
	}
	// mist
	n = 1 + CG_GoreCount( damage / 35.0f + ( explosive ? 2 : 0 ) );
	for ( i = 0 ; i < n ; i++ ) {
		VectorScale( dir, 40 + random() * 90, vel );
		vel[0] += crandom() * 25;
		vel[1] += crandom() * 25;
		vel[2] += crandom() * 25;
		size = 5 + damage * 0.08f;
		CG_GoreMist( point, vel, size, size * 3.2f, 450 + (int)( random() * 350 ), 0.45f + 0.15f * random() );
	}

	// blood on the wall behind the victim
	if ( !CG_GorePersist() || CG_GoreLevel() <= 0 || explosive ) {
		return;
	}
	dist = 160 + damage * 1.2f;
	if ( dist > 340 ) {
		dist = 340;
	}
	n = headshot ? 2 : 1;
	if ( damage < 12 && random() < 0.5f ) {
		n = 0;
	}
	for ( i = 0 ; i < n ; i++ ) {
		CG_GoreRandomDir( d );
		VectorMA( dir, 0.12f + 0.1f * i, d, d );
		VectorNormalize( d );
		VectorMA( point, dist, d, end );
		if ( !CG_GoreWorldTrace( &tr, point, end ) ) {
			continue;
		}
		size = ( 12 + damage * 0.25f ) * ( 1.0f - 0.45f * tr.fraction ) * ( 0.8f + 0.4f * random() );
		if ( size > 42 ) {
			size = 42;
		}
		CG_GoreWallSpatter( tr.endpos, tr.plane.normal, d, size,
			tr.fraction < 0.6f ? CG_GoreCount( 1.2f + damage / 45.0f ) : ( random() < 0.5f ? 1 : 0 ) );
	}
}

/*
=================
CG_GoreHit

EV_GORE_HIT: a player (or corpse) took damage. origin = where, angles2 = the
shot direction, eventParm = damage, generic1 = HIT_* flags (0x80: splash),
otherEntityNum = the target, otherEntityNum2 = the attacker, weapon = MOD.
=================
*/
void CG_GoreHit( entityState_t *es, const vec3_t position ) {
	vec3_t		dir, point;
	int			damage, target, attacker, mod, flags;
	float		dist, amount;
	ghit_t		*h;
	qboolean	explosive, local;

	CG_GoreRegister();
	damage = es->eventParm;
	target = es->otherEntityNum;
	attacker = es->otherEntityNum2;
	mod = es->weapon;
	flags = es->generic1;
	VectorCopy( position, point );
	VectorCopy( es->angles2, dir );
	if ( VectorNormalize( dir ) < 0.1f ) {
		CG_GoreRandomDir( dir );
		dir[2] = fabs( dir[2] );
	}
	if ( target >= 0 && target < MAX_GENTITIES ) {
		h = &lastHit[target];
		h->time = cg.time;
		VectorCopy( point, h->point );
		VectorCopy( dir, h->dir );
		h->damage = damage;
		h->flags = flags;
		h->mod = mod;
		h->attacker = attacker;
	}
	if ( CG_GoreLevel() <= 0 || !CG_GoreModBleeds( mod ) ) {
		return;
	}
	explosive = CG_GoreModExplosive( mod ) || ( flags & 0x80 );
	local = ( target == cg.snap->ps.clientNum );

	// your own wounds would spray right into the camera
	if ( !local || cg.renderingThirdPerson ) {
		CG_GoreSpray( point, dir, damage, ( flags & HIT_HEADSHOT ) != 0, explosive );
	}

	// blood on the screen: big hits close to the camera, or a big hit on you
	if ( local ) {
		if ( damage >= 30 ) {
			CG_GoreScreenBlood( ( damage - 20 ) / 100.0f );
		}
	} else {
		dist = Distance( point, cg.refdef.vieworg );
		if ( damage >= 25 && dist < 70 + damage * 0.9f ) {
			amount = ( damage / 110.0f ) * ( 1.0f - dist / ( 70 + damage * 0.9f ) ) * 1.6f;
			if ( amount > 0.12f ) {
				CG_GoreScreenBlood( amount );
			}
		}
	}
}

// ------------------------------------------------------------------ deaths

/*
=================
CG_GoreDeath

EV_DEATHx (gib = qfalse) and EV_GIB_PLAYER (gib = qtrue). Queued and handled
after all of this frame's events, so the killing blow's EV_GORE_HIT (which
comes on a later entity number) is known. Returns qfalse when cg_gore is 0 and
the stock effects should play.
=================
*/
qboolean CG_GoreDeath( centity_t *cent, int killer, qboolean gib ) {
	gdeath_t	*d;
	gbody_t		*b;

	CG_GoreRegister();
	if ( CG_GoreLevel() <= 0 ) {
		return qfalse;
	}
	b = &gbodies[cent->currentState.number];
	b->deathTime = cg.time;
	b->flags = cent->currentState.generic1 & RM_BODY_ALL;
	b->lastSeen = cg.time;
	b->fxTime = 0;
	b->killer = killer;

	if ( numPendingDeaths >= MAX_PENDING_DEATHS ) {
		return qtrue;
	}
	d = &pendingDeaths[numPendingDeaths++];
	d->entnum = cent->currentState.number;
	d->killer = killer;
	d->flags = cent->currentState.generic1 & RM_BODY_ALL;
	d->gib = gib;
	VectorCopy( cent->lerpOrigin, d->origin );
	return qtrue;
}

static void CG_GoreDeathDir( gdeath_t *d, vec3_t dir, vec3_t point ) {
	ghit_t	*h;

	h = &lastHit[d->entnum];
	VectorCopy( d->origin, point );
	if ( h->time && cg.time - h->time < 150 ) {
		VectorCopy( h->dir, dir );
		VectorCopy( h->point, point );
		return;
	}
	if ( d->killer >= 0 && d->killer < MAX_CLIENTS && d->killer != d->entnum ) {
		VectorSubtract( d->origin, cg_entities[d->killer].lerpOrigin, dir );
		if ( VectorNormalize( dir ) > 1 ) {
			return;
		}
	}
	CG_GoreRandomDir( dir );
	dir[2] = fabs( dir[2] ) * 0.5f;
	VectorNormalize( dir );
}

static void CG_GoreScreenIfClose( const vec3_t org, float range, float amount ) {
	float	dist;

	dist = Distance( org, cg.refdef.vieworg );
	if ( dist < range ) {
		CG_GoreScreenBlood( amount * ( 1.0f - dist / range ) + 0.1f );
	}
}

static void CG_GoreBurst( const vec3_t org, const vec3_t dir, int drops, float speed, int mists, float mistSize ) {
	int		i;
	vec3_t	vel, d;

	for ( i = 0 ; i < drops ; i++ ) {
		CG_GoreRandomDir( d );
		VectorMA( d, 0.6f, dir, vel );
		VectorNormalize( vel );
		VectorScale( vel, speed * ( 0.3f + 0.7f * random() ), vel );
		vel[2] += 80 + 120 * random();
		CG_GoreDrop( org, vel, 1.2f + 1.8f * random(), 1600, 0.3f, NULL );
	}
	for ( i = 0 ; i < mists ; i++ ) {
		CG_GoreRandomDir( d );
		VectorScale( d, 40 + 120 * random(), vel );
		VectorMA( vel, 60, dir, vel );
		vel[2] += 20;
		CG_GoreMist( org, vel, mistSize, mistSize * 3.5f, 600 + (int)( 500 * random() ), 0.55f );
	}
}

/*
=================
CG_GoreDecapitate
=================
*/
static void CG_GoreDecapitate( gdeath_t *d, const vec3_t dir ) {
	vec3_t		head, vel, up = { 0, 0, 1 };
	ggib_t		*g;
	int			n;
	qhandle_t	model;

	if ( d->entnum < MAX_CLIENTS && cg.time - headTime[d->entnum] < 300 ) {
		VectorCopy( headPos[d->entnum], head );
	} else {
		VectorCopy( d->origin, head );
		head[2] += 28;
	}
	VectorScale( dir, 200 + 160 * random(), vel );
	vel[2] += 230 + 90 * random();
	vel[0] += crandom() * 50;
	vel[1] += crandom() * 50;
	model = gm.head ? gm.head : cgs.media.gibSkull;
	g = CG_GoreLaunchGib( model, head, vel, gm.head ? 1.0f : 1.0f, 1.0f, GIBF_HEAD );
	if ( g ) {
		VectorSet( g->avel, crandom() * 720, 400 + random() * 600, crandom() * 720 );
	}
	// the neck bursts
	n = CG_GoreCount( 26 );
	CG_GoreBurst( head, up, n, 300, 1 + CG_GoreCount( 3 ), 7 );
	CG_GoreBurst( head, dir, CG_GoreCount( 12 ), 380, 1, 6 );
	// bits of skull and brain
	if ( CG_GoreLevel() >= 2 ) {
		VectorScale( dir, 150, vel );
		vel[2] += 200;
		CG_GoreLaunchGib( cgs.media.gibBrain, head, vel, 0.8f, 0.5f, 0 );
	}
	CG_FxSound( head, ENTITYNUM_WORLD, CHAN_AUTO, gm.decapSnd );
	CG_GoreScreenIfClose( head, 140, 0.5f );
}

/*
=================
CG_GoreSplit

Charged rail / slug: the upper body is blown away, the legs stay.
=================
*/
static void CG_GoreSplit( gdeath_t *d, const vec3_t dir ) {
	vec3_t		waist, vel, up = { 0, 0, 1 };
	int			i, n;
	static qhandle_t	*parts[6];

	parts[0] = &cgs.media.gibChest;
	parts[1] = &cgs.media.gibAbdomen;
	parts[2] = &cgs.media.gibIntestine;
	parts[3] = &cgs.media.gibArm;
	parts[4] = &cgs.media.gibForearm;
	parts[5] = &cgs.media.gibFist;

	VectorCopy( d->origin, waist );
	waist[2] += 6;
	for ( i = 0 ; i < 6 ; i++ ) {
		VectorScale( dir, 220 + 200 * random(), vel );
		vel[0] += crandom() * 120;
		vel[1] += crandom() * 120;
		vel[2] += 150 + 200 * random();
		CG_GoreLaunchGib( *parts[i], waist, vel, 1.0f, 0.8f, 0 );
	}
	VectorScale( dir, 300, vel );
	vel[2] += 260;
	CG_GoreLaunchGib( gm.head ? gm.head : cgs.media.gibSkull, waist, vel, 1.0f, 1.0f, GIBF_HEAD );
	n = CG_GoreCount( 4 );
	for ( i = 0 ; i < n ; i++ ) {
		VectorScale( dir, 200 + 250 * random(), vel );
		vel[0] += crandom() * 150;
		vel[1] += crandom() * 150;
		vel[2] += 120 + 200 * random();
		CG_GoreLaunchGib( CG_GoreMeat(), waist, vel, 0.9f + 0.5f * random(), 0.7f, 0 );
	}
	CG_GoreBurst( waist, dir, CG_GoreCount( 40 ), 420, 2 + CG_GoreCount( 4 ), 10 );
	CG_GoreBurst( waist, up, CG_GoreCount( 16 ), 240, 0, 0 );
	CG_FxSound( waist, ENTITYNUM_WORLD, CHAN_AUTO, gm.gibBurstSnd );
	CG_GoreScreenIfClose( waist, 170, 0.6f );
}

/*
=================
CG_GoreGib

A full gib. Overkill throws more and bigger chunks faster, a mist cloud and a
ring of splatter on the floor and walls around.
=================
*/
static void CG_GoreGib( gdeath_t *d, const vec3_t dir ) {
	vec3_t		org, vel, d2, end, up = { 0, 0, 1 };
	int			i, n, level;
	float		speed, scale;
	qboolean	overkill, charred;
	trace_t		tr;
	static qhandle_t	*stock[10];

	level = CG_GoreLevel();
	overkill = ( d->flags & RM_BODY_OVERKILL ) != 0;
	charred = ( d->flags & RM_BODY_CHARRED ) != 0;
	VectorCopy( d->origin, org );

	stock[0] = &cgs.media.gibAbdomen;
	stock[1] = &cgs.media.gibArm;
	stock[2] = &cgs.media.gibChest;
	stock[3] = &cgs.media.gibFist;
	stock[4] = &cgs.media.gibFoot;
	stock[5] = &cgs.media.gibForearm;
	stock[6] = &cgs.media.gibIntestine;
	stock[7] = &cgs.media.gibLeg;
	stock[8] = &cgs.media.gibLeg;
	stock[9] = &cgs.media.gibBrain;

	speed = overkill ? 480 : 300;
	// the id pieces (fewer on light gore)
	n = level >= 2 ? 10 : 6;
	if ( !cg_gibs.integer ) {
		n = 0;
	}
	for ( i = 0 ; i < n ; i++ ) {
		CG_GoreRandomDir( d2 );
		VectorMA( d2, overkill ? 0.8f : 0.4f, dir, vel );
		VectorScale( vel, speed * ( 0.5f + 0.6f * random() ), vel );
		vel[2] += 200 + 150 * random();
		CG_GoreLaunchGib( *stock[i], org, vel, 1.0f, 0.8f, charred ? GIBF_CHARRED : 0 );
	}
	if ( !( d->flags & RM_BODY_HEADLESS ) ) {
		VectorScale( dir, speed * 0.6f, vel );
		vel[2] += 280;
		CG_GoreLaunchGib( gm.head && ( rand() & 1 ) ? gm.head : cgs.media.gibSkull, org, vel, 1.0f, 1.0f,
			GIBF_HEAD | ( charred ? GIBF_CHARRED : 0 ) );
	}
	// meat chunks
	n = CG_GoreCount( overkill ? 14 : 6 );
	if ( !cg_gibs.integer ) {
		n = 0;
	}
	for ( i = 0 ; i < n ; i++ ) {
		CG_GoreRandomDir( d2 );
		VectorMA( d2, overkill ? 0.7f : 0.3f, dir, vel );
		VectorScale( vel, speed * ( 0.4f + 0.8f * random() ), vel );
		vel[2] += 180 + 200 * random();
		scale = overkill ? 1.1f + 0.7f * random() : 0.8f + 0.5f * random();
		CG_GoreLaunchGib( CG_GoreMeat(), org, vel, scale, 0.9f, charred ? GIBF_CHARRED : 0 );
	}

	// blood
	CG_GoreBurst( org, dir, CG_GoreCount( overkill ? 90 : 45 ), overkill ? 560 : 380,
		CG_GoreCount( overkill ? 12 : 5 ), overkill ? 12 : 9 );
	CG_GoreBurst( org, up, CG_GoreCount( 20 ), 300, 0, 0 );
	CG_GibFX( org );

	if ( overkill && CG_GorePersist() && cg_addMarks.integer ) {
		goreKeepMarks = qtrue;		// death splatter stays with the pools (keep ring)
		// a ring of splatter on the walls around, at a few heights
		n = CG_GoreCount( 12 );
		for ( i = 0 ; i < n ; i++ ) {
			float a = ( i + random() * 0.7f ) * 2 * M_PI / ( n > 0 ? n : 1 );
			VectorSet( d2, cos( a ), sin( a ), crandom() * 0.35f + 0.1f );
			VectorNormalize( d2 );
			VectorMA( org, 220, d2, end );
			if ( CG_GoreWorldTrace( &tr, org, end ) ) {
				CG_GoreWallSpatter( tr.endpos, tr.plane.normal, d2, 16 + 20 * ( 1.0f - tr.fraction ) + 8 * random(),
					tr.fraction < 0.7f ? 1 + ( rand() & 1 ) : 0 );
			}
		}
		// and splats flung across the floor
		n = CG_GoreCount( 8 );
		for ( i = 0 ; i < n ; i++ ) {
			float a = random() * 2 * M_PI;
			float r = 60 + 150 * random();
			VectorSet( end, org[0] + cos( a ) * r, org[1] + sin( a ) * r, org[2] );
			trap_CM_BoxTrace( &tr, org, end, NULL, NULL, 0, MASK_SOLID );
			VectorCopy( tr.endpos, end );
			VectorCopy( end, d2 );
			d2[2] -= 120;
			if ( CG_GoreWorldTrace( &tr, end, d2 ) && tr.plane.normal[2] > 0.6f ) {
				CG_GoreSplatMark( tr.endpos, tr.plane.normal, 14 + 16 * random(), 1.0f );
			}
		}
		goreKeepMarks = qfalse;
	}
	CG_FxSound( org, ENTITYNUM_WORLD, CHAN_AUTO, overkill ? gm.gibBurstSnd : gm.splatSnd[rand() % 3] );
	CG_GoreScreenIfClose( org, overkill ? 230 : 150, overkill ? 0.8f : 0.5f );
}

static void CG_GoreCharredDeath( gdeath_t *d ) {
	vec3_t	org, vel;
	int		i;
	static const vec3_t	ember = { 1.0f, 0.5f, 0.1f };
	static const vec3_t	up = { 0, 0, 1 };

	for ( i = 0 ; i < 6 ; i++ ) {
		VectorCopy( d->origin, org );
		org[0] += crandom() * 12;
		org[1] += crandom() * 12;
		org[2] += -10 + random() * 30;
		VectorSet( vel, crandom() * 20, crandom() * 20, 30 + random() * 40 );
		CG_GoreSmoke( org, vel, 10, 40, 1600 + (int)( random() * 900 ), 0.12f, 0.6f, 25, 0 );
	}
	CG_FxSparks( d->origin, up, ember, 0, 200, 900, 0.5f, 14 );
	CG_FxSound( d->origin, ENTITYNUM_WORLD, CHAN_AUTO, gm.sizzleSnd );
}

static void CG_GoreProcessDeaths( void ) {
	int			i;
	gdeath_t	*d;
	vec3_t		dir, point;
	float		flash;
	qboolean	mine;

	for ( i = 0, d = pendingDeaths ; i < numPendingDeaths ; i++, d++ ) {
		CG_GoreDeathDir( d, dir, point );
		mine = ( d->killer == cg.snap->ps.clientNum && d->entnum != cg.snap->ps.clientNum );
		flash = 0;
		if ( d->gib ) {
			CG_GoreGib( d, dir );
			flash = ( d->flags & RM_BODY_OVERKILL ) ? 1.0f : 0.5f;
		} else if ( d->flags & RM_BODY_SPLIT ) {
			CG_GoreSplit( d, dir );
			flash = 1.0f;
		} else if ( d->flags & RM_BODY_HEADLESS ) {
			CG_GoreDecapitate( d, dir );
			flash = 0.8f;
		}
		if ( ( d->flags & RM_BODY_CHARRED ) && !d->gib ) {
			CG_GoreCharredDeath( d );
		}
		if ( mine ) {
			if ( cg.snap->ps.stats[STAT_STYLE] >= STYLE_RANK_SSS && flash < 0.7f ) {
				flash = 0.7f;
			}
			if ( flash > 0 ) {
				CG_GoreKillFlash( flash );
			}
		}
	}
	numPendingDeaths = 0;
}

// ------------------------------------------------------------------ bodies (called while drawing players)

/*
=================
CG_GoreHead

Called for every player / corpse from CG_Player, after the head is placed.
Returns qtrue if the head must not be drawn (decapitated or split corpse).
=================
*/
qboolean CG_GoreHead( centity_t *cent, refEntity_t *torso, refEntity_t *head ) {
	gbody_t		*b;
	int			num, t, flags;
	vec3_t		neck, up, vel, org;
	float		beat, life, strength;
	int			i, n;
	qboolean	firstPerson;
	refEntity_t	stump;

	num = cent->currentState.number;
	if ( num < MAX_CLIENTS ) {
		VectorCopy( head->origin, headPos[num] );
		headTime[num] = cg.time;
	}
	if ( CG_GoreLevel() <= 0 ) {
		return qfalse;
	}
	if ( !( cent->currentState.eFlags & EF_DEAD ) ) {
		CG_GoreFootprints( cent );
		return qfalse;
	}
	flags = cent->currentState.generic1 & RM_BODY_ALL;
	if ( !flags ) {
		return qfalse;
	}
	b = CG_GoreBody( cent );
	t = cg.time - b->deathTime;
	firstPerson = ( num == cg.snap->ps.clientNum && !cg.renderingThirdPerson );

	if ( flags & ( RM_BODY_HEADLESS | RM_BODY_SPLIT ) ) {
		if ( flags & RM_BODY_SPLIT ) {
			VectorCopy( torso->origin, neck );
			VectorCopy( torso->axis[2], up );
			// a torn stump on the legs
			memset( &stump, 0, sizeof( stump ) );
			stump.reType = RT_MODEL;
			stump.hModel = gm.numMeat ? gm.meat[num % gm.numMeat] : cgs.media.gibAbdomen;
			VectorCopy( torso->origin, stump.origin );
			VectorCopy( torso->origin, stump.lightingOrigin );
			AxisCopy( torso->axis, stump.axis );
			stump.renderfx = torso->renderfx;
			trap_R_AddRefEntityToScene( &stump );
		} else {
			VectorCopy( head->origin, neck );
			VectorCopy( head->axis[2], up );
		}
		// the neck pumps blood for a few seconds, in heartbeats
		if ( !firstPerson && t >= 0 && t < 6500 && cg.time >= b->fxTime ) {
			b->fxTime = cg.time + 30;
			beat = CG_GoreExp( -( t % 650 ) / 170.0f );
			life = 1.0f - t / 6500.0f;
			strength = beat * life;
			n = CG_GoreCount( 1 + 3 * strength );
			for ( i = 0 ; i < n ; i++ ) {
				VectorScale( up, 60 + 260 * strength * ( 0.6f + 0.4f * random() ), vel );
				vel[0] += crandom() * 35;
				vel[1] += crandom() * 35;
				vel[2] += crandom() * 25;
				CG_GoreDrop( neck, vel, 1.3f + 1.2f * strength, 1400, 0.25f, NULL );
			}
			if ( strength > 0.6f && random() < 0.3f ) {
				VectorScale( up, 40, vel );
				CG_GoreMist( neck, vel, 3, 11, 420, 0.4f );
			}
		}
		return qtrue;
	}

	if ( flags & RM_BODY_CHARRED ) {
		if ( t >= 0 && t < 14000 && cg.time >= b->fxTime ) {
			b->fxTime = cg.time + 90 + t / 60;
			VectorCopy( cent->lerpOrigin, org );
			org[0] += crandom() * 14;
			org[1] += crandom() * 14;
			org[2] += -18 + random() * 20;
			VectorSet( vel, crandom() * 8, crandom() * 8, 20 + random() * 25 );
			CG_GoreSmoke( org, vel, 5, 22 + 10 * random(), 1400 + (int)( random() * 1200 ), 0.1f,
				0.45f * ( 1.0f - t / 14000.0f ), 18, 0 );
		}
	}
	return qfalse;
}

/*
=================
CG_GoreSkipTorso

Split corpses only draw their legs.
=================
*/
qboolean CG_GoreSkipTorso( centity_t *cent ) {
	return CG_GoreLevel() > 0 && ( cent->currentState.eFlags & EF_DEAD )
		&& ( cent->currentState.generic1 & RM_BODY_SPLIT );
}

/*
=================
CG_GoreBodyShell

Called for every body part added (CG_AddRefEntityWithPowerups): charred
corpses get a burnt skin and glowing cracks that cool down.
=================
*/
void CG_GoreBodyShell( refEntity_t *ent, entityState_t *state ) {
	gbody_t		*b;
	refEntity_t	shell;
	int			t;
	float		f, glow;

	if ( state->eType != ET_PLAYER || !( state->eFlags & EF_DEAD ) || !( state->generic1 & RM_BODY_CHARRED )
		|| CG_GoreLevel() <= 0 || ( state->powerups & ( 1 << PW_INVIS ) ) ) {
		return;
	}
	CG_GoreRegister();
	b = &gbodies[state->number];
	t = ( b->flags & RM_BODY_CHARRED ) ? cg.time - b->deathTime : 20000;
	if ( t < 0 ) {
		t = 0;
	}
	f = t / 350.0f;
	if ( f > 1 ) {
		f = 1;
	}
	shell = *ent;
	shell.customSkin = 0;
	shell.customShader = gm.charred;
	shell.shaderRGBA[0] = shell.shaderRGBA[1] = shell.shaderRGBA[2] = shell.shaderRGBA[3] = (byte)( 255 * f );
	trap_R_AddRefEntityToScene( &shell );

	glow = 1.0f - t / 9000.0f;
	if ( glow > 0 ) {
		glow *= 0.65f + 0.35f * sin( cg.time * 0.006f + state->number );
		shell.customShader = gm.embers;
		shell.shaderRGBA[0] = shell.shaderRGBA[1] = shell.shaderRGBA[2] = (byte)( 255 * glow );
		shell.shaderRGBA[3] = 255;
		trap_R_AddRefEntityToScene( &shell );
	}
}

// ------------------------------------------------------------------ other gore entry points

/*
=================
CG_GoreWallSplat

A body slammed into a wall (EV_WALLSPLAT from the physics worker): a big
splat with runs of blood down from it.
=================
*/
void CG_GoreWallSplat( const vec3_t org, const vec3_t normal, int speed ) {
	vec3_t	dir, p;
	float	size;

	CG_GoreRegister();
	if ( CG_GoreLevel() <= 0 ) {
		return;
	}
	VectorNegate( normal, dir );
	VectorMA( org, 2, normal, p );
	size = 20 + speed * 0.03f;
	if ( size > 48 ) {
		size = 48;
	}
	goreKeepMarks = qtrue;		// a body slammed into the wall: keep it with the death marks
	CG_GoreWallSpatter( org, normal, dir, size, CG_GoreCount( 3 ) );
	goreKeepMarks = qfalse;
	CG_GoreBurst( p, normal, CG_GoreCount( 18 ), 200, 1, 8 );
	CG_FxSound( org, ENTITYNUM_WORLD, CHAN_AUTO, gm.splatSnd[rand() % 3] );
}

/*
=================
CG_GoreExplosionMist

Chain reaction corpse explosions and the like: a burst of blood and chunks.
=================
*/
void CG_GoreCorpseBurst( const vec3_t org ) {
	gdeath_t	d;
	vec3_t		dir = { 0, 0, 1 };

	CG_GoreRegister();
	if ( CG_GoreLevel() <= 0 ) {
		return;
	}
	memset( &d, 0, sizeof( d ) );
	VectorCopy( org, d.origin );
	d.flags = RM_BODY_OVERKILL;
	d.gib = qtrue;
	d.killer = ENTITYNUM_WORLD;
	CG_GoreGib( &d, dir );
}

// ------------------------------------------------------------------ frame

static void CG_GoreAddParticles( float dt ) {
	int			i, j, traces;
	gpart_t		*p;
	float		f, size, alpha, speed, len, hot, s, c;
	vec3_t		end, dir, side, view, right, up, rgb;
	trace_t		tr;
	polyVert_t	v[4];
	byte		col[4];

	traces = GORE_TRACE_BUDGET;
	for ( i = 0 ; i < numGparts ; ) {
		p = &gparts[i];
		if ( cg.time < p->start ) {
			i++;
			continue;
		}
		f = (float)( cg.time - p->start ) / p->life;
		if ( f >= 1.0f ) {
			gparts[i] = gparts[--numGparts];
			continue;
		}

		// move
		p->vel[2] -= p->gravity * dt;
		if ( p->drag > 0 ) {
			s = 1.0f - p->drag * dt;
			if ( s < 0 ) {
				s = 0;
			}
			VectorScale( p->vel, s, p->vel );
		}
		VectorMA( p->org, dt, p->vel, end );
		if ( ( p->flags & GPF_COLLIDE ) && traces > 0 ) {
			traces--;
			trap_CM_BoxTrace( &tr, p->org, end, NULL, NULL, 0, MASK_SOLID );
			if ( tr.fraction < 1.0f ) {
				if ( ( p->flags & GPF_SPLAT ) && !tr.startsolid && !( tr.surfaceFlags & ( SURF_SKY | SURF_NOIMPACT | SURF_NOMARKS ) )
					&& CG_GorePersist() ) {
					CG_GoreSplatMark( tr.endpos, tr.plane.normal, 2.5f + p->size * 1.8f, 1.0f );
				}
				gparts[i] = gparts[--numGparts];
				continue;
			}
		}
		VectorCopy( end, p->org );
		p->rot += p->rotSpeed * dt;
		i++;

		// cull behind the view
		VectorSubtract( p->org, cg.refdef.vieworg, view );
		if ( DotProduct( view, cg.refdef.viewaxis[0] ) < -p->endSize - p->size - 8 ) {
			continue;
		}

		// draw
		alpha = p->rgba[3];
		VectorCopy( p->rgba, rgb );
		size = p->size + ( p->endSize - p->size ) * f;
		switch ( p->type ) {
		case GP_DROP:
			alpha *= f > 0.7f ? ( 1.0f - f ) / 0.3f : 1.0f;
			size = p->size;
			break;
		case GP_MIST:
			alpha *= ( 1.0f - f ) * ( 1.0f - f );
			size = p->size + ( p->endSize - p->size ) * ( 1.0f - ( 1.0f - f ) * ( 1.0f - f ) );
			break;
		case GP_FIRE:
			hot = ( 1.0f - f );
			hot = hot * hot * hot * hot;
			for ( j = 0 ; j < 3 ; j++ ) {
				rgb[j] = ( p->rgba[j] + ( 1.0f - p->rgba[j] ) * hot * 0.8f ) * ( 1.0f - f ) * ( 1.0f - f * 0.3f );
			}
			alpha = 1.0f;
			size = p->size + ( p->endSize - p->size ) * ( 1.0f - ( 1.0f - f ) * ( 1.0f - f ) );
			break;
		case GP_SMOKE:
		default:
			if ( f < 0.12f ) {
				alpha *= f / 0.12f;
			} else {
				alpha *= 1.0f - ( f - 0.12f ) / 0.88f;
			}
			size = p->size + ( p->endSize - p->size ) * ( 1.0f - ( 1.0f - f ) * ( 1.0f - f ) );
			break;
		}
		col[0] = (byte)( 255 * ( rgb[0] > 1 ? 1 : rgb[0] ) );
		col[1] = (byte)( 255 * ( rgb[1] > 1 ? 1 : rgb[1] ) );
		col[2] = (byte)( 255 * ( rgb[2] > 1 ? 1 : rgb[2] ) );
		col[3] = (byte)( 255 * ( alpha > 1 ? 1 : alpha < 0 ? 0 : alpha ) );

		if ( p->type == GP_DROP ) {
			// a streak along the velocity, the round head in front
			VectorCopy( p->vel, dir );
			speed = VectorNormalize( dir );
			if ( speed < 1 ) {
				VectorSet( dir, 0, 0, -1 );
			}
			size *= 1.35f;
			len = size * ( 2.0f + speed * 0.008f );
			if ( len > size * 4.5f ) {
				len = size * 4.5f;
			}
			CrossProduct( dir, view, side );
			if ( VectorNormalize( side ) < 0.001f ) {
				VectorCopy( cg.refdef.viewaxis[1], side );
			}
			VectorMA( p->org, len * 0.3f, dir, v[0].xyz );
			VectorMA( v[0].xyz, -size, side, v[0].xyz );
			VectorMA( v[0].xyz, 2 * size, side, v[1].xyz );
			VectorMA( v[1].xyz, -len, dir, v[2].xyz );
			VectorMA( v[2].xyz, -2 * size, side, v[3].xyz );
		} else {
			s = sin( DEG2RAD( p->rot ) );
			c = cos( DEG2RAD( p->rot ) );
			VectorScale( cg.refdef.viewaxis[1], c * size, right );
			VectorMA( right, s * size, cg.refdef.viewaxis[2], right );
			VectorScale( cg.refdef.viewaxis[2], c * size, up );
			VectorMA( up, -s * size, cg.refdef.viewaxis[1], up );
			VectorAdd( p->org, up, v[0].xyz );
			VectorAdd( v[0].xyz, right, v[0].xyz );
			VectorAdd( p->org, up, v[1].xyz );
			VectorSubtract( v[1].xyz, right, v[1].xyz );
			VectorSubtract( p->org, up, v[2].xyz );
			VectorSubtract( v[2].xyz, right, v[2].xyz );
			VectorSubtract( p->org, up, v[3].xyz );
			VectorAdd( v[3].xyz, right, v[3].xyz );
		}
		v[0].st[0] = 0; v[0].st[1] = 0;
		v[1].st[0] = 1; v[1].st[1] = 0;
		v[2].st[0] = 1; v[2].st[1] = 1;
		v[3].st[0] = 0; v[3].st[1] = 1;
		for ( j = 0 ; j < 4 ; j++ ) {
			v[j].modulate[0] = col[0];
			v[j].modulate[1] = col[1];
			v[j].modulate[2] = col[2];
			v[j].modulate[3] = col[3];
		}
		trap_R_AddPolyToScene( p->shader, 4, v );
	}
}

/*
=================
CG_GoreAddEntities

Once per frame after the packet entities (their events included), from CG_AddMarks.
=================
*/
void CG_GoreAddEntities( void ) {
	float	dt;

	CG_GoreRegister();
	dt = cg.frametime * 0.001f;
	if ( dt < 0 ) {
		dt = 0;
	} else if ( dt > 0.1f ) {
		dt = 0.1f;
	}
	CG_GoreProcessDeaths();
	CG_GoreAddParticles( dt );
	CG_GoreAddGibs( dt );
	CG_GoreAddLights();
	if ( cg_addMarks.integer ) {
		CG_GoreAddPools();
		CG_GoreAddDrips();
	}
}

/*
=================
CG_GoreClear

New map, map_restart, vid_restart: everything goes with the marks.
=================
*/
void CG_GoreClear( void ) {
	numGparts = 0;
	memset( ggibs, 0, sizeof( ggibs ) );
	gibNext = 0;
	memset( gpools, 0, sizeof( gpools ) );
	memset( recentPools, 0, sizeof( recentPools ) );
	memset( gseeps, 0, sizeof( gseeps ) );
	memset( splatCells, 0, sizeof( splatCells ) );
	numDrips = 0;
	dripNext = 0;
	memset( gbodies, 0, sizeof( gbodies ) );
	memset( lastHit, 0, sizeof( lastHit ) );
	memset( gscreen, 0, sizeof( gscreen ) );
	memset( gfeet, 0, sizeof( gfeet ) );
	memset( glights, 0, sizeof( glights ) );
	numPendingDeaths = 0;
	killFlashTime = 0;
	CG_GoreRegister();
	CG_HF_Register();		// en-feel: hit-feel sounds / models up front (cg_hitfeel.c)
}

// ------------------------------------------------------------------ round 4 hit feel (cg_hitfeel.c)

/*
=================
CG_GoreLimbGib

A severed limb / head / helmet: one more chunk in the persistent gib ring (it bounces, bleeds
and stays like the rest). head = GIBF_HEAD trail. Returns qfalse if gore is off.
=================
*/
qboolean CG_GoreLimbGib( qhandle_t model, const vec3_t org, const vec3_t vel, float scale, float bleed, qboolean head ) {
	ggib_t	*g;

	CG_GoreRegister();
	if ( CG_GoreLevel() <= 0 ) {
		return qfalse;
	}
	g = CG_GoreLaunchGib( model, org, vel, scale, bleed, head ? GIBF_HEAD : 0 );
	if ( g ) {
		VectorSet( g->avel, crandom() * 520, crandom() * 520, crandom() * 400 );
	}
	return g != NULL;
}

// a gush of blood drops + mist (the neck / shoulder / hip of a torn-off limb)
void CG_GoreBloodBurst( const vec3_t org, const vec3_t dir, int drops, float speed, int mists, float mistSize ) {
	CG_GoreRegister();
	if ( CG_GoreLevel() <= 0 ) {
		return;
	}
	CG_GoreBurst( org, dir, CG_GoreCount( drops ), speed, mists, mistSize );
}

// one permanent blood mark on the floor (a crawler's trail): skips crowded spots
void CG_GoreTrailMark( const vec3_t org, const vec3_t normal, float radius ) {
	CG_GoreRegister();
	if ( CG_GoreLevel() <= 0 ) {
		return;
	}
	CG_GoreSplatMark( org, normal, radius, 1.0f );
}

