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
// cg_physics.c -- rearmed: props, debris, corpse tumble, gibs kicked around by blasts
//
// - CG_Prop draws the server's physics props (model, or a flat-shaded fallback when the model is
//   missing) with their fire / gas jet / fuse effects.
// - A small debris pool (rock, wood and metal chunks) with its own bounce / slide / roll physics.
// - Gib and brass fragments (LE_FRAGMENT) are pushed by explosions, kicked by running players and
//   slide / roll after landing instead of stopping dead.
// - Airborne corpses tumble and settle on landing (CG_CorpseTumble, from CG_PlayerAngles).

#include "cg_local.h"

// cg_localents.c
extern localEntity_t	cg_localEntities[];
extern localEntity_t	cg_activeLocalEntities;

#define CGP_MAX_LE			2048		// fragment side table (the local entity pool size)
#define MAX_DEBRIS			160
#define MAX_POLY_DEBRIS		60			// fallback (no model) chunks are polys: fewer of them
#define DEBRIS_TRACES		160			// collision traces per frame for debris
#define FRAG_TRACES			96			// ... for sliding fragments

typedef enum {
	DEB_ROCK,
	DEB_WOOD,
	DEB_METAL,
	DEB_PAINT				// painted metal (barrel / tank shell)
} debrisKind_t;

typedef struct {
	qboolean	active;
	int			kind;
	qhandle_t	model;
	vec3_t		pos, vel;
	vec3_t		angles, avel;
	vec3_t		size;			// half extents (poly) / scale (model: size[0])
	float		color[3];
	int			start, end;
	int			nextTrail;
	int			kickTime;
	int			sounds;
	qboolean	onGround;
	qboolean	resting;
} debris_t;

typedef struct {
	int			startTime;		// matches le->startTime while this record belongs to that fragment
	qboolean	isGib;
	vec3_t		lastVel;
	qboolean	sliding;
	qboolean	slideDone;
	vec3_t		slideVel;
	int			kickTime;
	float		smear;			// distance since the last blood smear
} fragState_t;

typedef struct {
	int			time;
	float		pitch, roll;
	float		pitchRate, rollRate;
	qboolean	spinning;
} tumble_t;

static struct {
	qboolean	registered;
	qhandle_t	propModel[PROP_NUM_TYPES];
	qhandle_t	rock[3], wood[3], metal[2];
	int			numRock, numWood, numMetal;
	qhandle_t	flatShader;
	sfxHandle_t	barrelBoom, tankBoom, crateBreak, rupture, ignite;
	sfxHandle_t	hitMetal[2], hitWood;
	sfxHandle_t	splatSound[2];
	sfxHandle_t	rockTick, metalClink, woodKnock;
} cgp;

static vmCvar_t		cg_physicsFX;

static debris_t		cgpDebris[MAX_DEBRIS];
static int			debrisNext;
static fragState_t	frags[CGP_MAX_LE];
static tumble_t		tumbles[MAX_GENTITIES];
static int			propFxTime[MAX_GENTITIES];
static int			propPopTime[MAX_GENTITIES];
static int			debrisTraces, fragTraces, lastSoundTime;

// players that can kick gibs this frame
static vec3_t		kickOrg[MAX_CLIENTS], kickVel[MAX_CLIENTS];
static int			numKickers;

static qboolean CG_PhysOn( void ) {
	return cg_physicsFX.integer && ( cgs.rmSwitches & ( 1 << RS_CORPSEPHYSICS ) ) != 0;
}

static qboolean CG_FxOn( void ) {
	return cg_physicsFX.integer != 0;
}

/*
===============================================================================

SMALL FX HELPERS (the cg_fx.c ones are static)

===============================================================================
*/

static void CG_PhysSpark( const vec3_t org, const vec3_t vel, const vec3_t color, int life, qboolean ember, float size ) {
	localEntity_t	*le;

	le = CG_AllocLocalEntity();
	le->leType = LE_FX_SPARK;
	le->leFlags = ember ? LEF_FX_EMBER : 0;
	le->startTime = cg.time;
	le->endTime = cg.time + life;
	le->lifeRate = 1.0f / life;
	le->bounceFactor = 0.3f;
	le->radius = size;
	VectorCopy( color, le->color );
	le->color[3] = 1.0f;
	le->pos.trType = TR_GRAVITY;
	le->pos.trTime = cg.time;
	VectorCopy( org, le->pos.trBase );
	VectorCopy( vel, le->pos.trDelta );
	VectorCopy( org, le->refEntity.origin );
	le->refEntity.customShader = ember ? cgs.media.fxGlowShader : cgs.media.fxSparkShader;
}

static void CG_PhysFlash( const vec3_t org, const vec3_t color, float radius, float endRadius, int life ) {
	localEntity_t	*le;

	le = CG_AllocLocalEntity();
	le->leType = LE_FX_FLASH;
	le->startTime = cg.time;
	le->endTime = cg.time + life;
	le->lifeRate = 1.0f / life;
	le->radius = radius;
	le->endRadius = endRadius;
	VectorCopy( color, le->color );
	VectorCopy( org, le->refEntity.origin );
	le->refEntity.customShader = cgs.media.fxGlowShader;
	le->refEntity.reType = RT_SPRITE;
}

static void CG_PhysRing( const vec3_t org, const vec3_t normal, const vec3_t color, float endRadius, int life ) {
	localEntity_t	*le;

	le = CG_AllocLocalEntity();
	le->leType = LE_FX_RING;
	le->startTime = cg.time;
	le->endTime = cg.time + life;
	le->lifeRate = 1.0f / life;
	le->radius = endRadius * 0.15f;
	le->endRadius = endRadius;
	VectorCopy( color, le->color );
	VectorMA( org, 2, normal, le->refEntity.origin );
	VectorCopy( normal, le->refEntity.axis[0] );
	le->refEntity.customShader = cgs.media.fxRingShader;
}

static void CG_PhysShake( const vec3_t org, float amplitude, float radius ) {
	float	dist, amp, remaining;

	if ( !cg_screenShake.value ) {
		return;
	}
	dist = Distance( org, cg.refdef.vieworg );
	if ( dist >= radius ) {
		return;
	}
	amp = amplitude * ( 1.0f - dist / radius ) * CG_Clamp01( cg_screenShake.value );
	remaining = cg.shakeAmplitude * ( 1.0f - (float)( cg.time - cg.shakeTime ) / 450.0f );
	if ( amp > remaining ) {
		cg.shakeTime = cg.time;
		cg.shakeAmplitude = amp;
	}
}

static void CG_PhysRandomDir( const vec3_t normal, float focus, vec3_t out ) {
	vec3_t	r;

	do {
		VectorSet( r, crandom(), crandom(), crandom() );
	} while ( VectorLengthSquared( r ) > 1.0f || VectorLengthSquared( r ) < 0.01f );
	VectorNormalize( r );
	if ( DotProduct( r, normal ) < 0 ) {
		VectorNegate( r, r );
	}
	VectorMA( r, focus, normal, out );
	VectorNormalize( out );
}

static void CG_PhysSound( const vec3_t org, sfxHandle_t sfx, int minGap ) {
	if ( !sfx || cg.time - lastSoundTime < minGap ) {
		return;
	}
	lastSoundTime = cg.time;
	CG_FxSound( org, ENTITYNUM_WORLD, CHAN_AUTO, sfx );
}

/*
===============================================================================

REGISTRATION

===============================================================================
*/

void CG_PhysicsRegister( void ) {
	static const char *propNames[PROP_NUM_TYPES] = {
		"models/rearmed/props/barrel.md3", "models/rearmed/props/crate.md3", "models/rearmed/props/tank.md3" };
	int		i;
	qhandle_t	h;

	trap_Cvar_Register( &cg_physicsFX, "cg_physicsFX", "1", CVAR_ARCHIVE );

	memset( &cgp, 0, sizeof( cgp ) );
	for ( i = 0 ; i < PROP_NUM_TYPES ; i++ ) {
		cgp.propModel[i] = trap_R_RegisterModel( propNames[i] );
	}
	for ( i = 0 ; i < 3 ; i++ ) {
		h = trap_R_RegisterModel( va( "models/rearmed/debris/rock%i.md3", i + 1 ) );
		if ( h ) {
			cgp.rock[cgp.numRock++] = h;
		}
		h = trap_R_RegisterModel( va( "models/rearmed/debris/wood%i.md3", i + 1 ) );
		if ( h ) {
			cgp.wood[cgp.numWood++] = h;
		}
		if ( i < 2 ) {
			h = trap_R_RegisterModel( va( "models/rearmed/debris/metal%i.md3", i + 1 ) );
			if ( h ) {
				cgp.metal[cgp.numMetal++] = h;
			}
		}
	}
	cgp.flatShader = trap_R_RegisterShader( "rearmed/propflat" );
	if ( !cgp.flatShader ) {
		cgp.flatShader = cgs.media.whiteShader;
	}

	cgp.barrelBoom = trap_S_RegisterSound( "sound/rearmed/physics/barrel_boom.wav", qfalse );
	cgp.tankBoom = trap_S_RegisterSound( "sound/rearmed/physics/tank_boom.wav", qfalse );
	cgp.crateBreak = trap_S_RegisterSound( "sound/rearmed/physics/crate_break.wav", qfalse );
	cgp.rupture = trap_S_RegisterSound( "sound/rearmed/physics/tank_rupture.wav", qfalse );
	cgp.ignite = trap_S_RegisterSound( "sound/rearmed/physics/ignite.wav", qfalse );
	cgp.hitMetal[0] = trap_S_RegisterSound( "sound/rearmed/physics/hit_metal1.wav", qfalse );
	cgp.hitMetal[1] = trap_S_RegisterSound( "sound/rearmed/physics/hit_metal2.wav", qfalse );
	cgp.hitWood = trap_S_RegisterSound( "sound/rearmed/physics/hit_wood.wav", qfalse );
	cgp.splatSound[0] = trap_S_RegisterSound( "sound/rearmed/physics/splat1.wav", qfalse );
	cgp.splatSound[1] = trap_S_RegisterSound( "sound/rearmed/physics/splat2.wav", qfalse );
	cgp.rockTick = trap_S_RegisterSound( "sound/rearmed/physics/debris_rock.wav", qfalse );
	cgp.metalClink = trap_S_RegisterSound( "sound/rearmed/physics/debris_metal.wav", qfalse );
	cgp.woodKnock = trap_S_RegisterSound( "sound/rearmed/physics/debris_wood.wav", qfalse );

	memset( cgpDebris, 0, sizeof( cgpDebris ) );
	memset( frags, 0, sizeof( frags ) );
	memset( tumbles, 0, sizeof( tumbles ) );
	memset( propFxTime, 0, sizeof( propFxTime ) );
	memset( propPopTime, 0, sizeof( propPopTime ) );
	cgp.registered = qtrue;
}

/*
===============================================================================

FLAT-SHADED POLYS (fallback when a model is missing)

===============================================================================
*/

static void CG_FlatPoly( int n, vec3_t *pts, const float *color ) {
	polyVert_t	verts[12];
	vec3_t		e1, e2, normal;
	float		shade;
	int			i;
	static const vec3_t	light = { 0.36f, 0.24f, 0.90f };

	if ( n > 12 ) {
		n = 12;
	}
	VectorSubtract( pts[1], pts[0], e1 );
	VectorSubtract( pts[2], pts[0], e2 );
	CrossProduct( e1, e2, normal );
	VectorNormalize( normal );
	shade = 0.42f + 0.58f * ( 0.5f + 0.5f * DotProduct( normal, light ) );
	for ( i = 0 ; i < n ; i++ ) {
		VectorCopy( pts[i], verts[i].xyz );
		verts[i].st[0] = verts[i].st[1] = 0;
		verts[i].modulate[0] = 255 * color[0] * shade;
		verts[i].modulate[1] = 255 * color[1] * shade;
		verts[i].modulate[2] = 255 * color[2] * shade;
		verts[i].modulate[3] = 255;
	}
	trap_R_AddPolyToScene( cgp.flatShader, n, verts );
}

static void CG_LocalPoint( const vec3_t org, vec3_t axis[3], float x, float y, float z, vec3_t out ) {
	VectorMA( org, x, axis[0], out );
	VectorMA( out, y, axis[1], out );
	VectorMA( out, z, axis[2], out );
}

// box from -h to +h around org (z from 0 to 2*h[2] when fromBottom)
static void CG_FlatBox( const vec3_t org, vec3_t axis[3], const vec3_t h, qboolean fromBottom, const float *color ) {
	vec3_t	c[8], q[4];
	float	z0 = fromBottom ? 0 : -h[2], z1 = fromBottom ? 2 * h[2] : h[2];
	int		i;
	static const int faces[6][4] = {
		{ 0, 1, 3, 2 }, { 4, 6, 7, 5 },		// bottom, top
		{ 0, 4, 5, 1 }, { 2, 3, 7, 6 },		// -y, +y
		{ 0, 2, 6, 4 }, { 1, 5, 7, 3 }		// -x, +x
	};

	for ( i = 0 ; i < 8 ; i++ ) {
		CG_LocalPoint( org, axis, ( i & 1 ) ? h[0] : -h[0], ( i & 2 ) ? h[1] : -h[1], ( i & 4 ) ? z1 : z0, c[i] );
	}
	for ( i = 0 ; i < 6 ; i++ ) {
		VectorCopy( c[faces[i][0]], q[0] );
		VectorCopy( c[faces[i][1]], q[1] );
		VectorCopy( c[faces[i][2]], q[2] );
		VectorCopy( c[faces[i][3]], q[3] );
		CG_FlatPoly( 4, q, color );
	}
}

// upright cylinder, bands of color (z fractions)
static void CG_FlatCylinder( const vec3_t org, vec3_t axis[3], float r, float h, int nb, const float *bands, const float (*colors)[3] ) {
	vec3_t	q[4], cap[8];
	float	a0, a1, z0, z1;
	int		i, b;

	for ( i = 0 ; i < 8 ; i++ ) {
		a0 = i * M_PI / 4;
		a1 = ( i + 1 ) * M_PI / 4;
		for ( b = 0 ; b < nb ; b++ ) {
			z0 = h * bands[b];
			z1 = h * bands[b + 1];
			CG_LocalPoint( org, axis, cos( a0 ) * r, sin( a0 ) * r, z0, q[0] );
			CG_LocalPoint( org, axis, cos( a1 ) * r, sin( a1 ) * r, z0, q[1] );
			CG_LocalPoint( org, axis, cos( a1 ) * r, sin( a1 ) * r, z1, q[2] );
			CG_LocalPoint( org, axis, cos( a0 ) * r, sin( a0 ) * r, z1, q[3] );
			CG_FlatPoly( 4, q, colors[b] );
		}
	}
	for ( i = 0 ; i < 8 ; i++ ) {
		a0 = -i * M_PI / 4;
		CG_LocalPoint( org, axis, cos( a0 ) * r, sin( a0 ) * r, 0, cap[i] );
	}
	CG_FlatPoly( 8, cap, colors[0] );
	for ( i = 0 ; i < 8 ; i++ ) {
		a0 = i * M_PI / 4;
		CG_LocalPoint( org, axis, cos( a0 ) * r * 0.92f, sin( a0 ) * r * 0.92f, h, cap[i] );
	}
	CG_FlatPoly( 8, cap, colors[nb - 1] );
}

/*
===============================================================================

PROPS

===============================================================================
*/

static const float propHalfHeight[PROP_NUM_TYPES] = { 20, 16, 22 };
static const float propLyingHalf[PROP_NUM_TYPES] = { 14, 16, 12 };

static void CG_PropFallback( int type, const vec3_t org, vec3_t axis[3] ) {
	static const float barrelBands[4] = { 0, 0.40f, 0.58f, 1.0f };
	static const float barrelColors[3][3] = { { 0.62f, 0.10f, 0.06f }, { 0.95f, 0.75f, 0.10f }, { 0.62f, 0.10f, 0.06f } };
	static const float tankBands[3] = { 0, 0.78f, 1.0f };
	static const float tankColors[2][3] = { { 0.20f, 0.50f, 0.24f }, { 0.85f, 0.85f, 0.80f } };
	static const float crateColor[3] = { 0.58f, 0.40f, 0.20f };
	static const float strapColor[3] = { 0.30f, 0.20f, 0.10f };
	vec3_t	h;

	switch ( type ) {
	case PROP_CRATE:
		VectorSet( h, 16, 16, 16 );
		CG_FlatBox( org, axis, h, qtrue, crateColor );
		VectorSet( h, 16.4f, 4, 16.4f );
		CG_FlatBox( org, axis, h, qtrue, strapColor );
		break;
	case PROP_TANK:
		CG_FlatCylinder( org, axis, 12, 44, 2, tankBands, tankColors );
		break;
	default:
		CG_FlatCylinder( org, axis, 14, 40, 3, barrelBands, barrelColors );
		break;
	}
}

/*
=============
CG_Prop

RM_ENT_PROP entities (from CG_General)
=============
*/
void CG_Prop( centity_t *cent ) {
	static const vec3_t	fire = { 1.0f, 0.55f, 0.15f };
	static const vec3_t	gas = { 0.85f, 0.9f, 1.0f };
	static const vec3_t	red = { 1.0f, 0.2f, 0.1f };
	entityState_t	*s = &cent->currentState;
	refEntity_t		re;
	vec3_t			axis[3], center, org, top, vel, p;
	float			hz, cz, scale, f;
	int				type, age, i, num = s->number;

	type = s->modelindex2;
	if ( type < 0 || type >= PROP_NUM_TYPES ) {
		type = PROP_BARREL;
	}
	hz = propHalfHeight[type];
	cz = ( s->frame & PROPF_LYING ) ? propLyingHalf[type] : hz;

	// pop in when (re)spawned
	scale = 1.0f;
	age = cg.time - s->time;
	if ( age >= 0 && age < 420 ) {
		f = age / 420.0f;
		scale = 0.2f + 0.8f * ( 1.0f - ( 1.0f - f ) * ( 1.0f - f ) * ( 1.0f - f ) ) + 0.12f * sin( f * M_PI );
		if ( propPopTime[num] != s->time ) {
			propPopTime[num] = s->time;
			for ( i = 0 ; i < 6 ; i++ ) {
				VectorCopy( cent->lerpOrigin, p );
				p[0] += crandom() * 16;
				p[1] += crandom() * 16;
				p[2] += 8;
				VectorSet( vel, crandom() * 50, crandom() * 50, 20 + random() * 40 );
				CG_SmokePuff( p, vel, 18 + random() * 10, 0.8f, 0.8f, 0.8f, 0.4f, 600, cg.time, 0, 0, cgs.media.smokePuffShader );
			}
		}
	}

	AnglesToAxis( cent->lerpAngles, axis );
	VectorCopy( cent->lerpOrigin, center );
	center[2] += cz;
	// the model's origin is its bottom centre: rotate around the middle
	VectorMA( center, -hz * scale, axis[2], org );
	VectorMA( center, hz * scale, axis[2], top );

	if ( cgp.propModel[type] ) {
		memset( &re, 0, sizeof( re ) );
		re.hModel = cgp.propModel[type];
		VectorCopy( org, re.origin );
		VectorCopy( org, re.oldorigin );
		VectorCopy( center, re.lightingOrigin );
		re.renderfx = RF_LIGHTING_ORIGIN;
		for ( i = 0 ; i < 3 ; i++ ) {
			VectorScale( axis[i], scale, re.axis[i] );
		}
		re.nonNormalizedAxes = ( scale != 1.0f );
		trap_R_AddRefEntityToScene( &re );
	} else {
		for ( i = 0 ; i < 3 ; i++ ) {
			VectorScale( axis[i], scale, axis[i] );
		}
		CG_PropFallback( type, org, axis );
		AnglesToAxis( cent->lerpAngles, axis );
	}

	// burning: flames out of the top
	if ( s->frame & PROPF_BURNING ) {
		f = 0.6f + 0.4f * sin( cg.time * 0.031f ) * sin( cg.time * 0.017f );
		trap_R_AddLightToScene( top, 140 + 60 * f, 1.0f, 0.5f, 0.12f );
		if ( cg.time >= propFxTime[num] ) {
			propFxTime[num] = cg.time + ( cg_fx.integer ? 30 : 70 );
			for ( i = 0 ; i < 2 ; i++ ) {
				VectorCopy( top, p );
				p[0] += crandom() * 8;
				p[1] += crandom() * 8;
				VectorSet( vel, crandom() * 15, crandom() * 15, 90 + random() * 80 );
				CG_SmokePuff( p, vel, 10 + random() * 10, 1.0f, 0.55f + 0.25f * random(), 0.2f, 0.95f,
					420 + random() * 220, cg.time, 0, 0, cgs.media.flameShader );
			}
			if ( random() < 0.35f ) {
				VectorSet( vel, crandom() * 10, crandom() * 10, 60 + random() * 30 );
				VectorCopy( top, p );
				p[2] += 20;
				CG_SmokePuff( p, vel, 18 + random() * 12, 0.15f, 0.15f, 0.15f, 0.5f, 1300, cg.time, 0, 0, cgs.media.smokePuffShader );
			}
			if ( cg_fx.integer && random() < 0.2f ) {
				VectorSet( vel, crandom() * 60, crandom() * 60, 140 + random() * 120 );
				CG_PhysSpark( top, vel, fire, 800, qtrue, 3 );
			}
		}
	}

	// ruptured tank: a roaring gas jet from the valve (top, venting up and out along +x)
	if ( s->frame & PROPF_RUPTURED ) {
		vec3_t	jet, valve;

		VectorMA( axis[2], 0.6f, axis[0], jet );
		VectorNormalize( jet );
		VectorMA( center, hz * 0.95f * scale, axis[2], valve );
		trap_R_AddLightToScene( valve, 90, 0.7f, 0.8f, 1.0f );
		if ( cg.time >= propFxTime[num] ) {
			propFxTime[num] = cg.time + 16;
			for ( i = 0 ; i < 2 ; i++ ) {
				VectorScale( jet, 260 + random() * 160, vel );
				vel[0] += crandom() * 40;
				vel[1] += crandom() * 40;
				vel[2] += crandom() * 40;
				CG_SmokePuff( valve, vel, 10 + random() * 12, 0.9f, 0.92f, 0.95f, 0.55f, 420 + random() * 200,
					cg.time, 0, 0, cgs.media.smokePuffShader );
			}
			if ( cg_fx.integer && random() < 0.5f ) {
				VectorScale( jet, 380, vel );
				vel[0] += crandom() * 80;
				vel[1] += crandom() * 80;
				CG_PhysSpark( valve, vel, gas, 250, qfalse, 0 );
			}
		}
	}

	// fuse: about to go (chain reaction / blast)
	if ( ( s->frame & PROPF_FUSE ) || ( ( s->frame & PROPF_DANGER ) && s->time2 - cg.time < 500 ) ) {
		if ( ( cg.time / 60 ) & 1 ) {
			trap_R_AddLightToScene( center, 110, 1.0f, 0.15f, 0.05f );
		}
		if ( cg_fx.integer && random() < 0.3f ) {
			CG_PhysRandomDir( axis[2], 0.2f, vel );
			VectorScale( vel, 200 + random() * 150, vel );
			CG_PhysSpark( top, vel, red, 300, qfalse, 0 );
		}
	}
}

/*
===============================================================================

DEBRIS POOL

===============================================================================
*/

static int CG_ActivePolyDebris( void ) {
	int		i, n = 0;

	for ( i = 0 ; i < MAX_DEBRIS ; i++ ) {
		if ( cgpDebris[i].active && !cgpDebris[i].model ) {
			n++;
		}
	}
	return n;
}

static debris_t *CG_AllocDebris( void ) {
	debris_t	*d, *oldest = NULL;
	int			i;

	for ( i = 0 ; i < MAX_DEBRIS ; i++ ) {
		d = &cgpDebris[( debrisNext + i ) % MAX_DEBRIS];
		if ( !d->active ) {
			debrisNext = ( d - cgpDebris + 1 ) % MAX_DEBRIS;
			return d;
		}
		if ( !oldest || d->start < oldest->start ) {
			oldest = d;
		}
	}
	return oldest;
}

/*
=============
CG_SpawnDebris

kind: DEB_*; paint: color for DEB_PAINT
=============
*/
static void CG_SpawnDebris( int kind, const vec3_t org, const vec3_t vel, const float *paint ) {
	debris_t	*d;
	qhandle_t	model = 0;
	float		s;

	switch ( kind ) {
	case DEB_ROCK:
		if ( cgp.numRock ) {
			model = cgp.rock[rand() % cgp.numRock];
		}
		break;
	case DEB_WOOD:
		if ( cgp.numWood ) {
			model = cgp.wood[rand() % cgp.numWood];
		}
		break;
	default:
		if ( cgp.numMetal ) {
			model = cgp.metal[rand() % cgp.numMetal];
		}
		break;
	}
	if ( !model && CG_ActivePolyDebris() >= MAX_POLY_DEBRIS ) {
		return;
	}
	d = CG_AllocDebris();
	memset( d, 0, sizeof( *d ) );
	d->active = qtrue;
	d->kind = kind;
	d->model = model;
	VectorCopy( org, d->pos );
	VectorCopy( vel, d->vel );
	VectorSet( d->angles, random() * 360, random() * 360, random() * 360 );
	VectorSet( d->avel, crandom() * 720, crandom() * 720, crandom() * 720 );
	d->start = cg.time;
	d->end = cg.time + 3500 + rand() % 2500;
	s = 0.7f + random() * 0.6f;
	switch ( kind ) {
	case DEB_ROCK:
		VectorSet( d->size, 2.5f * s, 2.0f * s, 1.8f * s );
		VectorSet( d->color, 0.42f, 0.40f, 0.37f );
		d->color[0] += crandom() * 0.06f;
		break;
	case DEB_WOOD:
		VectorSet( d->size, 7.0f * s, 1.6f * s, 1.0f * s );
		VectorSet( d->color, 0.55f, 0.37f, 0.18f );
		break;
	case DEB_PAINT:
		VectorSet( d->size, 4.5f * s, 3.5f * s, 0.6f );
		if ( paint ) {
			VectorCopy( paint, d->color );
		} else {
			VectorSet( d->color, 0.6f, 0.1f, 0.06f );
		}
		break;
	default:
		VectorSet( d->size, 3.5f * s, 2.5f * s, 0.8f );
		VectorSet( d->color, 0.45f, 0.46f, 0.48f );
		break;
	}
	if ( model ) {
		d->size[0] = s;
	}
}

static void CG_DebrisBounceSound( debris_t *d, float impact ) {
	sfxHandle_t	sfx;

	if ( d->sounds >= 2 || impact < 140 ) {
		return;
	}
	d->sounds++;
	switch ( d->kind ) {
	case DEB_ROCK:
		sfx = cgp.rockTick;
		break;
	case DEB_WOOD:
		sfx = cgp.woodKnock;
		break;
	default:
		sfx = cgp.metalClink;
		break;
	}
	CG_PhysSound( d->pos, sfx, 35 );
}

static void CG_DrawDebris( debris_t *d ) {
	refEntity_t	re;
	vec3_t		axis[3], org;
	float		sink = 0;
	int			left = d->end - cg.time, i;

	if ( left < 700 ) {
		sink = ( 1.0f - left / 700.0f ) * ( d->kind == DEB_WOOD ? 4 : 8 );
	}
	VectorCopy( d->pos, org );
	org[2] -= sink;
	AnglesToAxis( d->angles, axis );
	if ( d->model ) {
		memset( &re, 0, sizeof( re ) );
		re.hModel = d->model;
		VectorCopy( org, re.origin );
		VectorCopy( org, re.oldorigin );
		VectorCopy( d->pos, re.lightingOrigin );
		re.renderfx = RF_LIGHTING_ORIGIN;
		for ( i = 0 ; i < 3 ; i++ ) {
			VectorScale( axis[i], d->size[0], re.axis[i] );
		}
		re.nonNormalizedAxes = ( d->size[0] != 1.0f );
		if ( d->kind == DEB_PAINT ) {
			re.shaderRGBA[0] = 255 * d->color[0];
			re.shaderRGBA[1] = 255 * d->color[1];
			re.shaderRGBA[2] = 255 * d->color[2];
			re.shaderRGBA[3] = 255;
		}
		trap_R_AddRefEntityToScene( &re );
	} else {
		CG_FlatBox( org, axis, d->size, qfalse, d->color );
	}
}

static void CG_UpdateDebris( debris_t *d, float dt ) {
	vec3_t		end, t;
	trace_t		tr;
	float		vn, speed, decel, grav;

	if ( cg.time >= d->end ) {
		d->active = qfalse;
		return;
	}
	if ( d->resting ) {
		CG_DrawDebris( d );
		return;
	}
	grav = cgs.rmMutator == MUT_LOW_GRAVITY ? 280 : 800;

	if ( d->onGround ) {
		// slide / roll with friction
		speed = sqrt( d->vel[0] * d->vel[0] + d->vel[1] * d->vel[1] );
		decel = ( d->kind == DEB_ROCK ? 520 : 700 ) * dt;
		if ( speed <= decel ) {
			d->resting = qtrue;
			CG_DrawDebris( d );
			return;
		}
		VectorScale( d->vel, ( speed - decel ) / speed, d->vel );
		d->vel[2] = 0;
		// roll: spin about the axis across the motion
		d->avel[PITCH] = speed * 6.0f;
		d->avel[ROLL] *= 0.9f;
		d->avel[YAW] *= 0.9f;
	} else {
		d->vel[2] -= grav * dt;
	}

	VectorMA( d->pos, dt, d->vel, end );
	if ( debrisTraces <= 0 ) {
		CG_DrawDebris( d );		// out of budget this frame: hold still
		return;
	}
	debrisTraces--;
	CG_Trace( &tr, d->pos, NULL, NULL, end, -1, CONTENTS_SOLID );
	if ( tr.allsolid ) {
		d->resting = qtrue;
		CG_DrawDebris( d );
		return;
	}
	if ( tr.fraction < 1.0f ) {
		VectorMA( tr.endpos, 0.5f, tr.plane.normal, d->pos );
		vn = DotProduct( d->vel, tr.plane.normal );
		CG_DebrisBounceSound( d, -vn );
		VectorMA( d->vel, -( d->kind == DEB_METAL || d->kind == DEB_PAINT ? 1.4f : 1.3f ) * vn, tr.plane.normal, d->vel );
		// scrape the sideways motion
		VectorMA( d->vel, -DotProduct( d->vel, tr.plane.normal ), tr.plane.normal, t );
		VectorMA( d->vel, -0.3f, t, d->vel );
		VectorScale( d->avel, 0.6f, d->avel );
		if ( tr.plane.normal[2] > 0.7f && fabs( d->vel[2] ) < 70 ) {
			d->onGround = qtrue;
			d->vel[2] = 0;
		}
		if ( CG_PointContents( d->pos, 0 ) & CONTENTS_NODROP ) {
			d->active = qfalse;
			return;
		}
	} else {
		VectorCopy( end, d->pos );
		if ( d->onGround ) {
			// ran off a ledge?
			VectorCopy( d->pos, end );
			end[2] -= 4;
			if ( debrisTraces > 0 ) {
				debrisTraces--;
				CG_Trace( &tr, d->pos, NULL, NULL, end, -1, CONTENTS_SOLID );
				if ( tr.fraction == 1.0f ) {
					d->onGround = qfalse;
				}
			}
		}
	}
	VectorMA( d->angles, dt, d->avel, d->angles );

	// dust trail off flying rocks, smoke off hot metal
	if ( !d->onGround && cg.time >= d->nextTrail && cg.time - d->start < 800 && CG_FxOn() ) {
		d->nextTrail = cg.time + 45;
		if ( d->kind == DEB_ROCK ) {
			CG_SmokePuff( d->pos, vec3_origin, 5 + random() * 4, 0.55f, 0.52f, 0.48f, 0.35f, 500,
				cg.time, 0, 0, cgs.media.smokePuffShader );
		} else if ( d->kind == DEB_PAINT ) {
			CG_SmokePuff( d->pos, vec3_origin, 5 + random() * 4, 0.2f, 0.2f, 0.2f, 0.4f, 600,
				cg.time, 0, 0, cgs.media.smokePuffShader );
		}
	}
	CG_DrawDebris( d );
}

/*
===============================================================================

PUSHING THINGS AROUND

===============================================================================
*/

static fragState_t *CG_FragState( localEntity_t *le ) {
	int			idx = le - cg_localEntities;
	fragState_t	*st;

	if ( idx < 0 || idx >= CGP_MAX_LE ) {
		return NULL;
	}
	st = &frags[idx];
	if ( st->startTime != le->startTime ) {
		memset( st, 0, sizeof( *st ) );
		st->startTime = le->startTime;
		st->isGib = ( le->leBounceSoundType == LEBS_BLOOD || le->leMarkType == LEMT_BLOOD );
	}
	return st;
}

static void CG_PushFragment( localEntity_t *le, const vec3_t push ) {
	fragState_t	*st = CG_FragState( le );
	vec3_t		vel, ang;

	if ( le->pos.trType == TR_STATIONARY ) {
		VectorClear( vel );
		if ( st && st->sliding ) {
			VectorCopy( st->slideVel, vel );
		}
	} else {
		BG_EvaluateTrajectoryDelta( &le->pos, cg.time, vel );
	}
	VectorAdd( vel, push, vel );
	VectorCopy( le->refEntity.origin, le->pos.trBase );
	le->pos.trBase[2] += 1;
	le->pos.trType = TR_GRAVITY;
	le->pos.trTime = cg.time;
	VectorCopy( vel, le->pos.trDelta );

	// spin
	if ( le->leFlags & LEF_TUMBLE ) {
		BG_EvaluateTrajectory( &le->angles, cg.time, ang );
	} else {
		VectorSet( ang, random() * 360, random() * 360, random() * 360 );
		le->leFlags |= LEF_TUMBLE;
	}
	VectorCopy( ang, le->angles.trBase );
	le->angles.trType = TR_LINEAR;
	le->angles.trTime = cg.time;
	VectorSet( le->angles.trDelta, crandom() * 700, crandom() * 700, crandom() * 700 );

	if ( st ) {
		st->sliding = qfalse;
		st->slideDone = qfalse;
		if ( st->isGib ) {
			// thrown gibs splat again where they land
			le->leMarkType = LEMT_BLOOD;
			le->leBounceSoundType = LEBS_BLOOD;
		}
	}
	if ( le->endTime - cg.time < 3000 ) {
		le->endTime = cg.time + 3000;
	}
}

static void CG_PushDebris( debris_t *d, const vec3_t push ) {
	VectorAdd( d->vel, push, d->vel );
	d->onGround = qfalse;
	d->resting = qfalse;
	d->pos[2] += 1;
	VectorSet( d->avel, crandom() * 800, crandom() * 800, crandom() * 800 );
	d->sounds = 0;
	if ( d->end - cg.time < 2500 ) {
		d->end = cg.time + 2500;
	}
}

/*
=============
CG_PhysicsBlast

An explosion throws nearby gibs, brass and debris outward
=============
*/
void CG_PhysicsBlast( const vec3_t origin, float radius, float force ) {
	localEntity_t	*le;
	debris_t		*d;
	vec3_t			dir, push;
	float			dist, f;
	int				i;

	if ( !CG_PhysOn() ) {
		return;
	}
	for ( le = cg_activeLocalEntities.next ; le != &cg_activeLocalEntities ; le = le->next ) {
		if ( le->leType != LE_FRAGMENT || cg.time - le->startTime < 60 ) {
			continue;		// (gibs from this very blast already fly)
		}
		VectorSubtract( le->refEntity.origin, origin, dir );
		dist = VectorNormalize( dir );
		if ( dist >= radius ) {
			continue;
		}
		f = force * ( 1.0f - dist / radius );
		dir[2] += 0.45f;
		VectorNormalize( dir );
		VectorScale( dir, f, push );
		CG_PushFragment( le, push );
	}
	for ( i = 0 ; i < MAX_DEBRIS ; i++ ) {
		d = &cgpDebris[i];
		if ( !d->active || cg.time - d->start < 30 ) {
			continue;
		}
		VectorSubtract( d->pos, origin, dir );
		dist = VectorNormalize( dir );
		if ( dist >= radius ) {
			continue;
		}
		f = force * ( 1.0f - dist / radius );
		dir[2] += 0.45f;
		VectorNormalize( dir );
		VectorScale( dir, f, push );
		CG_PushDebris( d, push );
	}
}

/*
=============
CG_GatherKickers

Players on foot moving fast enough to kick gibs
=============
*/
static void CG_GatherKickers( void ) {
	centity_t	*cent;
	int			i;
	float		speed;

	numKickers = 0;
	if ( !cg.snap ) {
		return;
	}
	if ( cg.predictedPlayerState.stats[STAT_HEALTH] > 0 && cg.predictedPlayerState.pm_type == PM_NORMAL ) {
		speed = VectorLength( cg.predictedPlayerState.velocity );
		if ( speed > 150 ) {
			VectorCopy( cg.predictedPlayerState.origin, kickOrg[numKickers] );
			VectorCopy( cg.predictedPlayerState.velocity, kickVel[numKickers] );
			numKickers++;
		}
	}
	for ( i = 0 ; i < cg.snap->numEntities && numKickers < MAX_CLIENTS ; i++ ) {
		cent = &cg_entities[cg.snap->entities[i].number];
		if ( cent->currentState.eType != ET_PLAYER || cent->currentState.number >= MAX_CLIENTS
			|| cent->currentState.number == cg.snap->ps.clientNum || ( cent->currentState.eFlags & EF_DEAD ) ) {
			continue;
		}
		if ( VectorLength( cent->currentState.pos.trDelta ) < 150 ) {
			continue;
		}
		VectorCopy( cent->lerpOrigin, kickOrg[numKickers] );
		VectorCopy( cent->currentState.pos.trDelta, kickVel[numKickers] );
		numKickers++;
	}
}

// is p at the feet of a kicker? fills the kick velocity
static qboolean CG_KickAt( const vec3_t p, vec3_t kick ) {
	vec3_t	d;
	float	speed;
	int		i;

	for ( i = 0 ; i < numKickers ; i++ ) {
		VectorSubtract( p, kickOrg[i], d );
		if ( d[2] < -30 || d[2] > 4 || d[0] * d[0] + d[1] * d[1] > 24 * 24 ) {
			continue;
		}
		speed = sqrt( kickVel[i][0] * kickVel[i][0] + kickVel[i][1] * kickVel[i][1] );
		VectorScale( kickVel[i], 0.9f, kick );
		d[2] = 0;
		VectorNormalize( d );
		VectorMA( kick, 60, d, kick );
		kick[0] += crandom() * 40;
		kick[1] += crandom() * 40;
		kick[2] = 90 + speed * 0.3f;
		return qtrue;
	}
	return qfalse;
}

/*
=============
CG_PhysFragment

Called at the top of CG_AddFragment. Keeps landed fragments sliding / rolling for a
moment. Returns qtrue if it drew the fragment itself (never at the moment: the stock
code draws, we only move it).
=============
*/
qboolean CG_PhysFragment( localEntity_t *le ) {
	fragState_t	*st;
	vec3_t		end, axis[3], rotAxis, up = { 0, 0, 1 }, t;
	trace_t		tr;
	float		dt, speed, decel, ang;
	int			i;

	if ( !CG_PhysOn() ) {
		return qfalse;
	}
	st = CG_FragState( le );
	if ( !st ) {
		return qfalse;
	}
	if ( le->pos.trType != TR_STATIONARY ) {
		BG_EvaluateTrajectoryDelta( &le->pos, cg.time, st->lastVel );
		return qfalse;
	}
	if ( !st->sliding ) {
		if ( st->slideDone ) {
			return qfalse;
		}
		st->slideDone = qtrue;
		speed = sqrt( st->lastVel[0] * st->lastVel[0] + st->lastVel[1] * st->lastVel[1] );
		if ( speed < 60 ) {
			return qfalse;
		}
		VectorSet( st->slideVel, st->lastVel[0], st->lastVel[1], 0 );
		VectorScale( st->slideVel, st->isGib ? 0.6f : 0.5f, st->slideVel );
		st->sliding = qtrue;
	}

	dt = cg.frametime * 0.001f;
	if ( dt <= 0 ) {
		return qfalse;
	}
	if ( dt > 0.05f ) {
		dt = 0.05f;
	}
	speed = VectorLength( st->slideVel );
	decel = ( st->isGib ? 420 : 560 ) * dt;
	if ( speed <= decel || fragTraces <= 0 ) {
		st->sliding = qfalse;
		return qfalse;
	}
	VectorScale( st->slideVel, ( speed - decel ) / speed, st->slideVel );
	speed -= decel;

	VectorMA( le->refEntity.origin, dt, st->slideVel, end );
	fragTraces--;
	CG_Trace( &tr, le->refEntity.origin, NULL, NULL, end, -1, CONTENTS_SOLID );
	if ( tr.fraction < 1.0f ) {
		// off a wall
		VectorMA( st->slideVel, -1.4f * DotProduct( st->slideVel, tr.plane.normal ), tr.plane.normal, st->slideVel );
		st->slideVel[2] = 0;
	}
	VectorCopy( tr.endpos, le->refEntity.origin );

	// still on the floor?
	VectorCopy( le->refEntity.origin, end );
	end[2] -= 6;
	if ( fragTraces > 0 ) {
		fragTraces--;
		CG_Trace( &tr, le->refEntity.origin, NULL, NULL, end, -1, CONTENTS_SOLID );
		if ( tr.fraction == 1.0f ) {
			// over the edge: fall
			VectorCopy( le->refEntity.origin, le->pos.trBase );
			VectorCopy( st->slideVel, le->pos.trDelta );
			le->pos.trType = TR_GRAVITY;
			le->pos.trTime = cg.time;
			st->sliding = qfalse;
			st->slideDone = qfalse;
			return qfalse;
		}
	}

	// roll it
	CrossProduct( up, st->slideVel, rotAxis );
	if ( VectorNormalize( rotAxis ) > 0 ) {
		ang = RAD2DEG( speed * dt / 4.0f );
		for ( i = 0 ; i < 3 ; i++ ) {
			RotatePointAroundVector( axis[i], rotAxis, le->refEntity.axis[i], ang );
		}
		for ( i = 0 ; i < 3 ; i++ ) {
			VectorCopy( axis[i], le->refEntity.axis[i] );
		}
		le->leFlags &= ~LEF_TUMBLE;
	}

	// gibs smear blood as they slide
	if ( st->isGib && cg_blood.integer && cg_addMarks.integer ) {
		st->smear += speed * dt;
		if ( st->smear > 20 ) {
			st->smear = 0;
			VectorSet( t, 0, 0, 1 );
			CG_ImpactMark( cgs.media.bloodMarkShader, le->refEntity.origin, t, random() * 360,
				1, 1, 1, 1, qtrue, 6 + random() * 5, qfalse );
		}
	}
	return qfalse;
}

/*
=============
CG_KickFragments

Running players kick gibs, brass and debris along
=============
*/
static void CG_KickFragments( void ) {
	localEntity_t	*le;
	fragState_t		*st;
	vec3_t			kick;
	int				i;

	if ( !numKickers ) {
		return;
	}
	for ( le = cg_activeLocalEntities.next ; le != &cg_activeLocalEntities ; le = le->next ) {
		if ( le->leType != LE_FRAGMENT ) {
			continue;
		}
		if ( le->pos.trType != TR_STATIONARY ) {
			continue;		// in flight already
		}
		st = CG_FragState( le );
		if ( !st || cg.time - st->kickTime < 250 ) {
			continue;
		}
		if ( CG_KickAt( le->refEntity.origin, kick ) ) {
			st->kickTime = cg.time;
			CG_PushFragment( le, kick );
			if ( st->isGib && random() < 0.5f ) {
				CG_PhysSound( le->refEntity.origin, cgs.media.gibBounce1Sound, 80 );
			}
		}
	}
	for ( i = 0 ; i < MAX_DEBRIS ; i++ ) {
		if ( !cgpDebris[i].active || !( cgpDebris[i].onGround || cgpDebris[i].resting ) || cg.time - cgpDebris[i].kickTime < 250 ) {
			continue;
		}
		if ( CG_KickAt( cgpDebris[i].pos, kick ) ) {
			cgpDebris[i].kickTime = cg.time;
			CG_PushDebris( &cgpDebris[i], kick );
		}
	}
}

/*
=============
CG_PhysicsFrame

Once per frame, from CG_AddLocalEntities
=============
*/
void CG_PhysicsFrame( void ) {
	float	dt;
	int		i;

	if ( !cgp.registered ) {
		return;
	}
	trap_Cvar_Update( &cg_physicsFX );
	debrisTraces = DEBRIS_TRACES;
	fragTraces = FRAG_TRACES;
	dt = cg.frametime * 0.001f;
	if ( dt < 0 ) {
		dt = 0;
	}
	if ( dt > 0.05f ) {
		dt = 0.05f;
	}
	if ( CG_PhysOn() ) {
		CG_GatherKickers();
		CG_KickFragments();
	}
	for ( i = 0 ; i < MAX_DEBRIS ; i++ ) {
		if ( cgpDebris[i].active ) {
			CG_UpdateDebris( &cgpDebris[i], dt );
		}
	}
}

/*
===============================================================================

EVENTS

===============================================================================
*/

/*
=============
CG_PhysicsExplosion

A weapon impact (EV_MISSILE_HIT / MISS / MISS_METAL): blasts push things and chip the walls
=============
*/
void CG_PhysicsExplosion( int weapon, const vec3_t origin, const vec3_t dir, int event ) {
	static const vec3_t	hot = { 1.0f, 0.8f, 0.45f };
	float		radius, force;
	int			rocks, i;
	vec3_t		vel, p, n;
	qboolean	metal = ( event == EV_MISSILE_MISS_METAL );

	switch ( weapon ) {
	case WP_ROCKET_LAUNCHER:
		radius = 220;
		force = 850;
		rocks = 6;
		break;
	case WP_GRENADE_LAUNCHER:
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case WP_PROX_LAUNCHER:
#endif
		radius = 190;
		force = 720;
		rocks = 5;
		break;
	case WP_BFG:
		radius = 280;
		force = 1000;
		rocks = 8;
		break;
	case WP_PLASMAGUN:
		if ( !cg.impactAlt ) {
			return;
		}
		radius = 150;
		force = 380;
		rocks = 0;
		break;
	default:
		return;
	}
	CG_PhysicsBlast( origin, radius, force );

	if ( !CG_FxOn() || event == EV_MISSILE_HIT || !rocks ) {
		return;
	}
	VectorCopy( dir, n );
	if ( VectorNormalize( n ) < 0.5f ) {
		VectorSet( n, 0, 0, 1 );
	}
	if ( cg_physicsFX.integer > 1 ) {
		rocks = rocks * 2;
	}
	VectorMA( origin, 3, n, p );
	for ( i = 0 ; i < rocks ; i++ ) {
		CG_PhysRandomDir( n, 0.6f, vel );
		VectorScale( vel, 220 + random() * 380, vel );
		if ( metal ) {
			if ( i < 2 ) {
				CG_SpawnDebris( DEB_METAL, p, vel, NULL );
			}
			VectorScale( vel, 1.6f, vel );
			CG_PhysSpark( p, vel, hot, 500 + rand() % 300, qfalse, 0 );
		} else {
			CG_SpawnDebris( DEB_ROCK, p, vel, NULL );
		}
	}
	if ( !metal ) {
		// a puff of dust off the wall
		for ( i = 0 ; i < 3 ; i++ ) {
			CG_PhysRandomDir( n, 1.0f, vel );
			VectorScale( vel, 40 + random() * 50, vel );
			CG_SmokePuff( p, vel, 26 + random() * 14, 0.5f, 0.47f, 0.42f, 0.4f, 1100 + random() * 500,
				cg.time, 0, 0, cgs.media.smokePuffShader );
		}
	}
}

static void CG_Fireball( const vec3_t org, float size, int count ) {
	vec3_t	vel, p;
	int		i;

	for ( i = 0 ; i < count ; i++ ) {
		VectorSet( vel, crandom(), crandom(), crandom() * 0.6f + 0.35f );
		VectorNormalize( vel );
		VectorScale( vel, size * ( 2.5f + random() * 3.0f ), vel );
		VectorCopy( org, p );
		CG_SmokePuff( p, vel, size + random() * size * 0.6f, 1.0f, 0.5f + 0.3f * random(), 0.15f, 1.0f,
			380 + random() * 260, cg.time, 0, 0, cgs.media.flameShader );
	}
	for ( i = 0 ; i < count / 2 ; i++ ) {
		VectorSet( vel, crandom() * 40, crandom() * 40, 50 + random() * 60 );
		VectorCopy( org, p );
		p[2] += 10 + random() * 20;
		CG_SmokePuff( p, vel, size * 1.3f + random() * size, 0.12f, 0.11f, 0.1f, 0.6f,
			1600 + random() * 1200, cg.time, cg.time + 250, 0, cgs.media.smokePuffShader );
	}
}

/*
=============
CG_PropBreakFX

EV_PROP_BREAK: parm = propType_t, | 0x80 = just vanish
=============
*/
void CG_PropBreakFX( entityState_t *es, const vec3_t origin ) {
	static const vec3_t	fire = { 1.0f, 0.62f, 0.25f };
	static const vec3_t	hot = { 1.0f, 0.85f, 0.55f };
	static const vec3_t	up = { 0, 0, 1 };
	static const float	barrelPaint[3] = { 0.62f, 0.10f, 0.06f };
	static const float	tankPaint[3] = { 0.20f, 0.50f, 0.24f };
	vec3_t		vel, p, inherit;
	int			type = es->eventParm & 0x7f, i, n;

	if ( es->eventParm & 0x80 ) {
		for ( i = 0 ; i < 8 ; i++ ) {
			VectorSet( vel, crandom() * 60, crandom() * 60, 20 + random() * 50 );
			CG_SmokePuff( origin, vel, 20 + random() * 12, 0.85f, 0.85f, 0.85f, 0.5f, 700,
				cg.time, 0, 0, cgs.media.smokePuffShader );
		}
		return;
	}
	VectorScale( es->angles2, 0.4f, inherit );

	if ( type == PROP_CRATE ) {
		CG_FxSound( origin, ENTITYNUM_WORLD, CHAN_AUTO, cgp.crateBreak );
		n = cg_physicsFX.integer > 1 ? 18 : 12;
		for ( i = 0 ; i < n ; i++ ) {
			VectorSet( vel, crandom() * 260, crandom() * 260, 120 + random() * 260 );
			VectorAdd( vel, inherit, vel );
			VectorCopy( origin, p );
			p[0] += crandom() * 12;
			p[1] += crandom() * 12;
			p[2] += crandom() * 12;
			CG_SpawnDebris( DEB_WOOD, p, vel, NULL );
		}
		for ( i = 0 ; i < 6 ; i++ ) {
			VectorSet( vel, crandom() * 70, crandom() * 70, 20 + random() * 50 );
			CG_SmokePuff( origin, vel, 24 + random() * 14, 0.55f, 0.45f, 0.35f, 0.45f, 1000 + random() * 500,
				cg.time, 0, 0, cgs.media.smokePuffShader );
		}
		CG_PhysicsBlast( origin, 90, 260 );
		return;
	}

	// barrel / tank: a big fireball
	CG_MissileHitWall( WP_ROCKET_LAUNCHER, 0, (float *)origin, (float *)up, IMPACTSOUND_DEFAULT );
	trap_S_StartSound( (float *)origin, ENTITYNUM_WORLD, CHAN_AUTO, type == PROP_TANK ? cgp.tankBoom : cgp.barrelBoom );
	if ( CG_FxOn() ) {
		CG_Fireball( origin, type == PROP_TANK ? 22 : 30, type == PROP_TANK ? 10 : 16 );
		CG_PhysFlash( origin, fire, 60, type == PROP_TANK ? 240 : 320, 300 );
		CG_PhysRing( origin, up, fire, type == PROP_TANK ? 220 : 280, 380 );
		for ( i = 0 ; i < 24 ; i++ ) {
			CG_PhysRandomDir( up, 0.2f, vel );
			VectorScale( vel, 350 + random() * 450, vel );
			CG_PhysSpark( origin, vel, i & 1 ? fire : hot, 700 + rand() % 700, ( i % 3 ) == 0, 4 );
		}
		n = type == PROP_TANK ? 7 : 8;
		if ( cg_physicsFX.integer > 1 ) {
			n += 6;
		}
		for ( i = 0 ; i < n ; i++ ) {
			CG_PhysRandomDir( up, 0.3f, vel );
			VectorScale( vel, 300 + random() * 450, vel );
			VectorAdd( vel, inherit, vel );
			CG_SpawnDebris( ( i & 1 ) ? DEB_METAL : DEB_PAINT, origin, vel, type == PROP_TANK ? tankPaint : barrelPaint );
		}
	}
	CG_PhysShake( origin, type == PROP_TANK ? 5.0f : 7.0f, 900 );
	CG_PhysicsBlast( origin, 260, 950 );
}

/*
=============
CG_PropHitFX

EV_PROP_HIT on the prop: a knock, parm = strength 0..255
=============
*/
void CG_PropHitFX( centity_t *cent, int strength ) {
	static const vec3_t	hot = { 1.0f, 0.85f, 0.55f };
	entityState_t	*s = &cent->currentState;
	vec3_t			p, vel;
	int				i;

	if ( strength < 16 ) {
		return;
	}
	if ( s->modelindex2 == PROP_CRATE ) {
		CG_FxSound( NULL, s->number, CHAN_AUTO, cgp.hitWood );
		return;
	}
	if ( ( s->frame & PROPF_RUPTURED ) && strength == 255 && cg.time - s->time > 0 ) {
		CG_FxSound( NULL, s->number, CHAN_BODY, cgp.rupture );
	}
	CG_FxSound( NULL, s->number, CHAN_AUTO, cgp.hitMetal[rand() & 1] );
	if ( strength > 120 && CG_FxOn() ) {
		VectorCopy( cent->lerpOrigin, p );
		p[2] += 16;
		for ( i = 0 ; i < 4 ; i++ ) {
			VectorSet( vel, crandom() * 250, crandom() * 250, 80 + random() * 200 );
			CG_PhysSpark( p, vel, hot, 350, qfalse, 0 );
		}
	}
}

/*
=============
CG_WallSplatFX

EV_WALLSPLAT: a body hit a wall hard. parm = speed / 8, angles2 = the wall normal
=============
*/
void CG_WallSplatFX( entityState_t *es, const vec3_t origin ) {
	vec3_t		normal, p, end;
	float		speed = es->eventParm * 8;
	trace_t		tr;

	VectorCopy( es->angles2, normal );
	if ( VectorNormalize( normal ) < 0.5f ) {
		VectorSet( normal, 0, 0, 1 );
	}
	CG_PhysShake( origin, 2.0f + speed / 400.0f, 500 );
	if ( es->otherEntityNum == cg.snap->ps.clientNum ) {
		CG_PhysShake( cg.refdef.vieworg, 6.0f, 10 );
	}

	// find the wall surface
	VectorMA( origin, 12, normal, p );
	VectorMA( origin, -20, normal, end );
	CG_Trace( &tr, p, NULL, NULL, end, -1, CONTENTS_SOLID );
	if ( tr.fraction < 1.0f ) {
		VectorCopy( tr.endpos, p );
		VectorCopy( tr.plane.normal, normal );
	} else {
		VectorCopy( origin, p );
	}

	if ( cg_blood.integer ) {
		// the gore worker's splat: a big directional splat with drips, plus its sound
		CG_GoreWallSplat( p, normal, (int)speed );
	} else {
		CG_FxSound( origin, ENTITYNUM_WORLD, CHAN_AUTO, cgp.splatSound[rand() & 1] );
	}
	CG_PhysicsBlast( p, 80, 200 );
	CG_HF_WallSplat( p, (int)speed );		// en-feel: the body's thud
}

/*
===============================================================================

CORPSE TUMBLE

===============================================================================
*/

/*
=============
CG_CorpseTumble

From CG_PlayerAngles for EF_DEAD bodies: an airborne corpse flips over along its flight,
and settles flat when it lands. Rotates around the body's middle.
=============
*/
void CG_CorpseTumble( centity_t *cent, vec3_t legsAngles ) {
	tumble_t	*t;
	vec3_t		vel, fwd, axis[3], ang, off;
	float		dt, speed, step, sign;
	qboolean	airborne;
	int			n = cent->currentState.number;

	if ( n < 0 || n >= MAX_GENTITIES || !CG_PhysOn() ) {
		return;
	}
	t = &tumbles[n];
	dt = ( cg.time - t->time ) * 0.001f;
	if ( !t->time || dt < 0 || dt > 0.5f ) {
		memset( t, 0, sizeof( *t ) );
		dt = 0;
	}
	t->time = cg.time;

	VectorCopy( cent->currentState.pos.trDelta, vel );
	speed = VectorLength( vel );
	airborne = ( cent->currentState.groundEntityNum == ENTITYNUM_NONE );

	if ( airborne && ( speed > 260 || t->spinning ) ) {
		if ( !t->spinning ) {
			t->spinning = qtrue;
			AngleVectors( legsAngles, fwd, NULL, NULL );
			sign = ( fwd[0] * vel[0] + fwd[1] * vel[1] ) >= 0 ? 1.0f : -1.0f;
			t->pitchRate = sign * ( 280 + speed * 0.55f );
			t->rollRate = crandom() * speed * 0.35f;
		}
		t->pitch = AngleNormalize180( t->pitch + t->pitchRate * dt );
		t->roll = AngleNormalize180( t->roll + t->rollRate * dt );
	} else {
		// land: settle flat quickly
		t->spinning = qfalse;
		step = 720 * dt;
		if ( t->pitch > step ) {
			t->pitch -= step;
		} else if ( t->pitch < -step ) {
			t->pitch += step;
		} else {
			t->pitch = 0;
		}
		if ( t->roll > step ) {
			t->roll -= step;
		} else if ( t->roll < -step ) {
			t->roll += step;
		} else {
			t->roll = 0;
		}
	}
	if ( !t->pitch && !t->roll ) {
		return;
	}
	legsAngles[PITCH] += t->pitch;
	legsAngles[ROLL] += t->roll;

	// pivot around the body (the corpse lies about 18 units under the origin)
	VectorSet( ang, t->pitch, legsAngles[YAW], t->roll );
	AnglesToAxis( ang, axis );
	VectorScale( axis[2], 18, off );
	off[2] -= 18;
	VectorAdd( cent->lerpOrigin, off, cent->lerpOrigin );
}
