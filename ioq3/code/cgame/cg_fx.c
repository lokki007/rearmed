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
// cg_fx.c -- rearmed particle effects: sparks, embers, flashes, shockwaves, screen shake

#include "cg_local.h"

#define SPARK_MAX_TAIL		20		// longest spark streak
#define SPARK_TAIL_TIME		0.018f	// streak length in seconds of travel
#define SPARK_STOP_SPEED	40		// slower than this after a bounce, the spark dies

/*
==================
CG_FxRandomDir

Random direction in the hemisphere around normal, biased towards it by focus (0..1)
==================
*/
static void CG_FxRandomDir( const vec3_t normal, float focus, vec3_t out ) {
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

/*
==================
CG_FxSpark

Glowing particle with gravity that bounces off the world.
Streaks are drawn along the velocity, embers as round sprites.
==================
*/
static void CG_FxSpark( const vec3_t org, const vec3_t vel, const vec3_t color, int life, float bounce, qboolean ember, float size ) {
	localEntity_t	*le;

	le = CG_AllocLocalEntity();
	le->leType = LE_FX_SPARK;
	le->leFlags = ember ? LEF_FX_EMBER : 0;
	le->startTime = cg.time;
	le->endTime = cg.time + life;
	le->lifeRate = 1.0f / life;
	le->bounceFactor = bounce;
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

#define SPARK_TRACE_BUDGET	160

void CG_AddFxSpark( localEntity_t *le ) {
	static int	traceFrame, tracesLeft;
	refEntity_t	*re = &le->refEntity;
	vec3_t		newOrigin, vel;
	trace_t		trace;
	float		fade, speed, dot;

	BG_EvaluateTrajectory( &le->pos, cg.time, newOrigin );
	// a budget of collision traces per frame; past it sparks just fly (they are short lived)
	if ( traceFrame != cg.clientFrame ) {
		traceFrame = cg.clientFrame;
		tracesLeft = SPARK_TRACE_BUDGET;
	}
	if ( tracesLeft > 0 ) {
		tracesLeft--;
		CG_Trace( &trace, re->origin, NULL, NULL, newOrigin, -1, CONTENTS_SOLID );
	} else {
		trace.fraction = 1.0f;
	}
	if ( trace.fraction < 1.0f ) {
		if ( trace.allsolid || le->bounceFactor <= 0 ) {
			CG_FreeLocalEntity( le );
			return;
		}
		// reflect off the surface and lose some energy
		BG_EvaluateTrajectoryDelta( &le->pos, cg.time, vel );
		dot = DotProduct( vel, trace.plane.normal );
		VectorMA( vel, -2 * dot, trace.plane.normal, le->pos.trDelta );
		VectorScale( le->pos.trDelta, le->bounceFactor, le->pos.trDelta );
		VectorMA( trace.endpos, 0.5f, trace.plane.normal, le->pos.trBase );
		le->pos.trTime = cg.time;
		VectorCopy( le->pos.trBase, newOrigin );
		if ( VectorLength( le->pos.trDelta ) < SPARK_STOP_SPEED ) {
			le->bounceFactor = 0;		// next hit kills it
		}
	}
	VectorCopy( newOrigin, re->origin );

	fade = 1.0f - ( cg.time - le->startTime ) * le->lifeRate;
	if ( fade <= 0 ) {
		return;
	}
	fade *= fade;
	re->shaderRGBA[0] = 255 * le->color[0] * fade;
	re->shaderRGBA[1] = 255 * le->color[1] * fade;
	re->shaderRGBA[2] = 255 * le->color[2] * fade;
	re->shaderRGBA[3] = 255;

	if ( le->leFlags & LEF_FX_EMBER ) {
		re->reType = RT_SPRITE;
		re->radius = le->radius * ( 0.5f + 0.5f * fade );
		re->rotation = 0;
	} else {
		// streak from the current position back along the velocity
		BG_EvaluateTrajectoryDelta( &le->pos, cg.time, vel );
		speed = VectorNormalize( vel );
		speed *= SPARK_TAIL_TIME;
		if ( speed > SPARK_MAX_TAIL ) {
			speed = SPARK_MAX_TAIL;
		} else if ( speed < 1 ) {
			speed = 1;
		}
		re->reType = RT_RAIL_CORE;
		VectorMA( re->origin, -speed, vel, re->oldorigin );
	}
	trap_R_AddRefEntityToScene( re );
}

/*
==================
CG_FxFlash

Camera facing glow that grows from radius to endRadius while fading out
==================
*/
static void CG_FxFlash( const vec3_t org, const vec3_t color, float radius, float endRadius, int life ) {
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

/*
==================
CG_FxRing

Flat shockwave ring on the impact surface
==================
*/
static void CG_FxRing( const vec3_t org, const vec3_t normal, const vec3_t color, float endRadius, int life ) {
	localEntity_t	*le;

	le = CG_AllocLocalEntity();
	le->leType = LE_FX_RING;
	le->startTime = cg.time;
	le->endTime = cg.time + life;
	le->lifeRate = 1.0f / life;
	le->radius = endRadius * 0.15f;
	le->endRadius = endRadius;
	VectorCopy( color, le->color );
	VectorMA( org, 2, normal, le->refEntity.origin );		// keep it off the surface
	VectorCopy( normal, le->refEntity.axis[0] );
	le->refEntity.customShader = cgs.media.fxRingShader;
}

void CG_AddFxFlash( localEntity_t *le ) {
	refEntity_t	*re = &le->refEntity;
	float		frac, fade;

	frac = ( cg.time - le->startTime ) * le->lifeRate;
	fade = 1.0f - frac;
	if ( fade <= 0 ) {
		return;
	}
	re->radius = le->radius + ( le->endRadius - le->radius ) * frac;
	re->shaderRGBA[0] = 255 * le->color[0] * fade;
	re->shaderRGBA[1] = 255 * le->color[1] * fade;
	re->shaderRGBA[2] = 255 * le->color[2] * fade;
	re->shaderRGBA[3] = 255;
	trap_R_AddRefEntityToScene( re );
}

void CG_AddFxRing( localEntity_t *le ) {
	refEntity_t	*re = &le->refEntity;
	polyVert_t	verts[4];
	vec3_t		right, up;
	float		frac, fade, radius;
	int			i;
	byte		c[3];

	frac = ( cg.time - le->startTime ) * le->lifeRate;
	fade = 1.0f - frac;
	if ( fade <= 0 ) {
		return;
	}
	// ease out: fast expansion that slows down
	radius = le->radius + ( le->endRadius - le->radius ) * ( 1.0f - fade * fade );

	PerpendicularVector( right, re->axis[0] );
	CrossProduct( re->axis[0], right, up );

	for ( i = 0 ; i < 3 ; i++ ) {
		c[i] = 255 * le->color[i] * fade;
	}
	for ( i = 0 ; i < 4 ; i++ ) {
		float s = ( i == 0 || i == 3 ) ? -1 : 1;
		float t = ( i < 2 ) ? -1 : 1;
		VectorMA( re->origin, s * radius, right, verts[i].xyz );
		VectorMA( verts[i].xyz, t * radius, up, verts[i].xyz );
		verts[i].st[0] = ( s + 1 ) * 0.5f;
		verts[i].st[1] = ( t + 1 ) * 0.5f;
		verts[i].modulate[0] = c[0];
		verts[i].modulate[1] = c[1];
		verts[i].modulate[2] = c[2];
		verts[i].modulate[3] = 255;
	}
	trap_R_AddPolyToScene( re->customShader, 4, verts );
}

/*
==================
CG_AddScreenShake

Shake the view for explosions near the camera
==================
*/
#define SHAKE_TIME	450
#define SHAKE_MAX	9.0f

static void CG_ShakeAdd( float amp ) {
	float	remaining, f;

	if ( amp <= 0 ) {
		return;
	}
	f = cg.shakeTime ? (float)( cg.time - cg.shakeTime ) / SHAKE_TIME : 1.0f;
	remaining = f < 1.0f ? cg.shakeAmplitude * ( 1.0f - f ) * ( 1.0f - f ) : 0;
	// shakes stack a little (a rocket barrage builds up), but the strongest dominates
	amp = amp > remaining ? amp + remaining * 0.35f : remaining + amp * 0.35f;
	if ( amp > SHAKE_MAX ) {
		amp = SHAKE_MAX;
	}
	cg.shakeTime = cg.time;
	cg.shakeAmplitude = amp;
}

static void CG_AddScreenShake( const vec3_t org, float amplitude, float radius ) {
	float	dist, amp, f;

	if ( !cg_screenShake.value ) {
		return;
	}
	dist = Distance( org, cg.refdef.vieworg );
	if ( dist >= radius ) {
		return;
	}
	// strong up close, a long soft tail: 1 at the center, 0 at the radius
	f = 1.0f - dist / radius;
	amp = amplitude * ( 0.25f * f + 0.75f * f * f ) * CG_Clamp01( cg_screenShake.value );
	CG_ShakeAdd( amp );
}

/*
==================
CG_FxShake / CG_FxPunch

Shakes for other files: from a point (distance scaled), or a direct camera punch.
==================
*/
void CG_FxShake( const vec3_t org, float amplitude, float radius ) {
	CG_AddScreenShake( org, amplitude, radius );
}

void CG_FxPunch( float amplitude ) {
	if ( !cg_screenShake.value ) {
		return;
	}
	CG_ShakeAdd( amplitude * CG_Clamp01( cg_screenShake.value ) );
}

/*
==================
CG_FxSound

The effect one-shots (gore, debris, prop knocks, event beeps) go through here: at most
FX_SOUND_BURST of them in any FX_SOUND_WINDOW msec, and the same sound at most every
FX_SOUND_SAME msec, so a pile of gibs or a barrel chain can't flood the mixer's channels and
cut off the weapon, pain and announcer sounds. Returns qfalse when the sound was dropped.
==================
*/
#define FX_SOUND_BURST		8
#define FX_SOUND_WINDOW		200
#define FX_SOUND_SAME		60

qboolean CG_FxSound( const vec3_t origin, int entityNum, int channel, sfxHandle_t sfx ) {
	static int			times[FX_SOUND_BURST];
	static sfxHandle_t	sounds[FX_SOUND_BURST];
	static int			next;		// the oldest of the last FX_SOUND_BURST starts
	int					i;

	if ( !sfx ) {
		return qfalse;
	}
	for ( i = 0 ; i < FX_SOUND_BURST ; i++ ) {
		if ( times[i] > cg.time ) {
			times[i] = 0;		// the clock went back (map restart)
		}
		if ( times[i] && sounds[i] == sfx && cg.time - times[i] < FX_SOUND_SAME ) {
			return qfalse;
		}
	}
	if ( times[next] && cg.time - times[next] < FX_SOUND_WINDOW ) {
		return qfalse;
	}
	times[next] = cg.time;
	sounds[next] = sfx;
	next = ( next + 1 ) % FX_SOUND_BURST;
	trap_S_StartSound( (float *)origin, entityNum, channel, sfx );
	return qtrue;
}

void CG_ApplyScreenShake( void ) {
	float	f, amp;

	if ( !cg.shakeTime ) {
		return;
	}
	if ( cg.snap && cg.snap->ps.stats[STAT_HEALTH] <= 0 ) {
		cg.shakeTime = 0;
		return;
	}
	f = (float)( cg.time - cg.shakeTime ) / SHAKE_TIME;
	if ( f >= 1.0f ) {
		cg.shakeTime = 0;
		return;
	}
	amp = cg.shakeAmplitude * ( 1.0f - f ) * ( 1.0f - f );
	cg.refdefViewAngles[PITCH] += amp * sin( cg.time * 0.061f );
	cg.refdefViewAngles[YAW] += amp * sin( cg.time * 0.047f + 1.3f ) * 0.8f;
	cg.refdefViewAngles[ROLL] += amp * sin( cg.time * 0.039f + 2.1f ) * 0.5f;
}

/*
==================
CG_FxBurst

count sparks + embers around normal
==================
*/
static void CG_FxBurst( const vec3_t org, const vec3_t normal, const vec3_t color, int count, float speed,
		int life, float focus, int embers ) {
	vec3_t	dir, vel;
	int		i;

	for ( i = 0 ; i < count ; i++ ) {
		CG_FxRandomDir( normal, focus, dir );
		VectorScale( dir, speed * ( 0.4f + 0.6f * random() ), vel );
		CG_FxSpark( org, vel, color, life * ( 0.5f + 0.5f * random() ), 0.35f, qfalse, 0 );
	}
	for ( i = 0 ; i < embers ; i++ ) {
		CG_FxRandomDir( normal, 1.0f, dir );
		VectorScale( dir, speed * ( 0.15f + 0.3f * random() ), vel );
		vel[2] += 60;
		CG_FxSpark( org, vel, color, life * ( 1.2f + random() ), 0.2f, qtrue, 3 + random() * 3 );
	}
}

/*
==================
CG_FxSparks

CG_FxBurst for other files
==================
*/
void CG_FxSparks( const vec3_t org, const vec3_t normal, const vec3_t color, int count, float speed,
		int life, float focus, int embers ) {
	CG_FxBurst( org, normal, color, count, speed, life, focus, embers );
}

/*
==================
CG_FxExplosion

A layered explosion: a white-hot core that blooms into rolling fire puffs,
a dark smoke column that lingers, a bright light flash and a wide faint
scorch around the stock burn mark. scale 1 = rocket.
==================
*/
void CG_FxExplosion( const vec3_t origin, const vec3_t dir, float scale, const vec3_t color, qboolean smoke ) {
	vec3_t	org, vel, d;
	int		i, n;
	float	s;

	VectorMA( origin, 10 * scale, dir, org );
	// core flash
	for ( i = 0 ; i < 2 ; i++ ) {
		CG_GoreFire( org, vec3_origin, 18 * scale, 46 * scale, 140 + i * 60, color, 0, 0 );
	}
	// rolling fireball
	n = 7 + (int)( 4 * scale );
	for ( i = 0 ; i < n ; i++ ) {
		do {
			VectorSet( d, crandom(), crandom(), crandom() );
		} while ( VectorLengthSquared( d ) > 1.0f || VectorLengthSquared( d ) < 0.05f );
		VectorNormalize( d );
		VectorMA( d, 0.7f, dir, d );
		VectorNormalize( d );
		s = 70 + 230 * random();
		VectorScale( d, s * scale, vel );
		CG_GoreFire( org, vel, ( 9 + 8 * random() ) * scale, ( 34 + 26 * random() ) * scale,
			350 + (int)( 350 * random() ), color, 3.2f, 60 );
	}
	if ( smoke ) {
		// smoke column: starts a little late, rises and spreads
		n = 6 + (int)( 3 * scale );
		for ( i = 0 ; i < n ; i++ ) {
			VectorCopy( org, d );
			d[0] += crandom() * 14 * scale;
			d[1] += crandom() * 14 * scale;
			d[2] += crandom() * 8 * scale;
			VectorScale( dir, 30 + 40 * random(), vel );
			vel[0] += crandom() * 25;
			vel[1] += crandom() * 25;
			vel[2] += 20 + 40 * random();
			CG_GoreSmoke( d, vel, ( 14 + 8 * random() ) * scale, ( 70 + 50 * random() ) * scale,
				2600 + (int)( 1600 * random() ), 0.18f + 0.12f * random(), 0.55f, 22, 120 + (int)( 250 * random() ) );
		}
	}
	CG_GoreLight( org, 520 * scale, color[0], color[1] * 0.9f, color[2] * 0.8f, 200 );
}

/*
==================
CG_FxDust

A small dust puff where a bullet hits the world
==================
*/
static void CG_FxDust( const vec3_t org, const vec3_t dir, float size ) {
	vec3_t	vel;

	VectorScale( dir, 25 + 30 * random(), vel );
	vel[2] += 8;
	CG_GoreSmoke( org, vel, size * 0.3f, size, 500 + (int)( 300 * random() ), 0.55f, 0.35f, 8, 0 );
}

/*
==================
CG_ImpactFX

Extra effects for a weapon hitting the world (called from CG_MissileHitWall)
==================
*/
void CG_ImpactFX( int weapon, const vec3_t origin, const vec3_t dir, qboolean alt ) {
	static const vec3_t	fire = { 1.0f, 0.62f, 0.25f };
	static const vec3_t	hot = { 1.0f, 0.85f, 0.55f };
	static const vec3_t	blue = { 0.45f, 0.65f, 1.0f };
	static const vec3_t	green = { 0.4f, 1.0f, 0.45f };
	static const vec3_t	white = { 0.85f, 0.9f, 1.0f };
	static const vec3_t	fireball = { 1.0f, 0.45f, 0.12f };
	static const vec4_t	water = { 0.45f, 0.62f, 1.0f, 0.7f };
	float	drop[4];
	vec3_t	org, vel;
	int		i;

	if ( !cg_fx.integer ) {
		return;
	}
	VectorMA( origin, 2, dir, org );

	switch ( weapon ) {
	case WP_MACHINEGUN:
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case WP_CHAINGUN:
	case WP_NAILGUN:
#endif
		CG_FxBurst( org, dir, hot, 4, 320, 350, 0.6f, 0 );
		CG_FxFlash( org, hot, 6, 10, 70 );
		CG_FxDust( org, dir, 10 );
		break;
	case WP_SHOTGUN:
		CG_FxBurst( org, dir, hot, alt ? 8 : 2, 300, 300, 0.6f, 0 );
		if ( random() < 0.5f ) {
			CG_FxDust( org, dir, 8 );
		}
		break;
	case WP_GRENADE_LAUNCHER:
	case WP_ROCKET_LAUNCHER:
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case WP_PROX_LAUNCHER:
#endif
		CG_FxBurst( org, dir, fire, 26, 600, 1100, 0.3f, 8 );
		CG_FxFlash( org, hot, 40, 150, 180 );
		CG_FxRing( origin, dir, fire, 190, 320 );
		CG_FxExplosion( origin, dir, alt ? 1.3f : 1.0f, fireball, qtrue );
		// a wide faint scorch around the stock burn mark
		CG_ImpactMark( cgs.media.burnMarkShader, origin, dir, random() * 360, 0.5f, 0.5f, 0.5f, 1, qfalse, 110, qfalse );
		CG_AddScreenShake( origin, 4.5f, 950 );
		break;
	case WP_PLASMAGUN:
		if ( alt ) {
			CG_FxBurst( org, dir, blue, 18, 450, 700, 0.3f, 6 );
			CG_FxFlash( org, blue, 30, 110, 200 );
			CG_FxRing( origin, dir, blue, 160, 300 );
			CG_FxExplosion( origin, dir, 0.8f, blue, qfalse );
			CG_AddScreenShake( origin, 2.0f, 600 );
		} else {
			CG_FxBurst( org, dir, blue, 3, 250, 300, 0.5f, 0 );
		}
		// water element: a splash of droplets
		Vector4Copy( water, drop );
		for ( i = 0 ; i < ( alt ? 16 : 5 ) ; i++ ) {
			VectorScale( dir, 90 + 160 * random(), vel );
			vel[0] += crandom() * 110;
			vel[1] += crandom() * 110;
			vel[2] += 60 + 90 * random();
			CG_GoreParticleDrop( org, vel, 0.9f + 0.8f * random(), 700, drop );
		}
		break;
	case WP_BFG:
		CG_FxBurst( org, dir, green, 30, 650, 1200, 0.3f, 10 );
		CG_FxFlash( org, green, 50, 200, 250 );
		CG_FxRing( origin, dir, green, 260, 380 );
		CG_FxExplosion( origin, dir, 1.2f, green, qtrue );
		CG_AddScreenShake( origin, 5.5f, 1100 );
		break;
	case WP_RAILGUN:
		CG_FxBurst( org, dir, white, 12, 420, 500, 0.5f, 2 );
		CG_FxFlash( org, white, 12, 40, 120 );
		CG_FxRing( origin, dir, white, 40, 250 );
		CG_FxDust( org, dir, 16 );
		break;
	case WP_LIGHTNING:
		if ( random() < 0.4f ) {
			CG_FxBurst( org, dir, blue, 2, 250, 250, 0.5f, 0 );
		}
		if ( random() < 0.3f ) {
			CG_GoreLight( org, 120, 0.5f, 0.75f, 1.0f, 60 );
		}
		break;
	default:
		break;
	}
}

/*
==================
CG_SingularitySwirl

Particles spiralling into the plasma singularity
==================
*/
#define SWIRL_RADIUS	220
#define SWIRL_TIME		450

static void CG_SingularitySwirl( const vec3_t center ) {
	static const vec3_t	violet = { 0.55f, 0.4f, 1.0f };
	localEntity_t	*le;
	vec3_t			dir, start, tangent, up = { 0, 0, 1 };
	int				i;

	if ( !cg_fx.integer ) {
		return;
	}
	for ( i = 0 ; i < 3 ; i++ ) {
		do {
			VectorSet( dir, crandom(), crandom(), crandom() );
		} while ( VectorLengthSquared( dir ) > 1.0f || VectorLengthSquared( dir ) < 0.05f );
		VectorNormalize( dir );
		VectorMA( center, SWIRL_RADIUS * ( 0.6f + 0.4f * random() ), dir, start );

		le = CG_AllocLocalEntity();
		le->leType = LE_FX_SPARK;
		le->leFlags = 0;
		le->startTime = cg.time;
		le->endTime = cg.time + SWIRL_TIME;
		le->lifeRate = 1.0f / SWIRL_TIME;
		le->bounceFactor = 0;
		VectorCopy( violet, le->color );
		le->pos.trType = TR_LINEAR;
		le->pos.trTime = cg.time;
		VectorCopy( start, le->pos.trBase );
		VectorCopy( start, le->refEntity.origin );
		// inward plus a sideways component so it spirals
		CrossProduct( dir, up, tangent );
		VectorScale( dir, -( Distance( start, center ) / ( SWIRL_TIME / 1000.0f ) ), le->pos.trDelta );
		VectorMA( le->pos.trDelta, 180, tangent, le->pos.trDelta );
		le->refEntity.customShader = cgs.media.fxSparkShader;
	}
}

/*
==================
CG_HitConfirm

EV_HITCONFIRM: hitmarker, hit sound, damage number, airshot callout
==================
*/
void CG_HitConfirm( centity_t *cent ) {
	entityState_t	*es = &cent->currentState;
	int				damage = es->eventParm;
	int				tier;

	if ( es->generic1 & HIT_KILL ) {
		trap_S_StartLocalSound( cgs.media.killConfirmSound, CHAN_LOCAL_SOUND );
	} else if ( cg_hitMarkers.integer ) {
		tier = damage < 25 ? 0 : damage < 70 ? 1 : 2;
		trap_S_StartLocalSound( ( rand() & 1 ) ? cgs.media.hitSounds2[tier] : cgs.media.hitSounds[tier], CHAN_LOCAL_SOUND );
	}
	if ( es->generic1 & HIT_ARMORBREAK ) {
		trap_S_StartLocalSound( cgs.media.armorBreakSound, CHAN_AUTO );
	}
	cg.hitMarkerTime = cg.time;
	cg.hitMarkerDamage = damage;
	cg.hitMarkerKill = ( es->generic1 & HIT_KILL ) != 0;
	cg.hitMarkerHeadshot = ( es->generic1 & HIT_HEADSHOT ) != 0;
	CG_DS_FrenzyHitConfirm( es->generic1 );	// FRENZY: a crit turns the marker gold

	if ( es->generic1 & HIT_AIRSHOT ) {
		cg.airshotTime = cg.time;
		trap_S_StartLocalSound( cgs.media.airshotSound, CHAN_ANNOUNCER );
	}

}

/*
==================
CG_OverchargeTrail

Overcharged rail: hot embers along the beam
==================
*/
void CG_OverchargeTrail( vec3_t start, vec3_t end ) {
	static const vec3_t	hot = { 1.0f, 0.55f, 0.15f };
	vec3_t	dir, pos, vel;
	float	len, d, step;

	if ( !cg_fx.integer ) {
		return;
	}
	VectorSubtract( end, start, dir );
	len = VectorNormalize( dir );
	step = len / 40.0f;
	if ( step < 24 ) {
		step = 24;
	}
	for ( d = 16 ; d < len ; d += step ) {
		VectorMA( start, d, dir, pos );
		VectorSet( vel, crandom() * 40, crandom() * 40, 20 + random() * 40 );
		CG_FxSpark( pos, vel, hot, 500 + random() * 400, 0, qtrue, 5 + random() * 3 );
	}
}

/*
==================
CG_BFGLaser

Green laser from the BFG ball to someone it zaps
==================
*/
void CG_BFGLaser( vec3_t start, vec3_t end ) {
	static const vec3_t	green = { 0.3f, 1.0f, 0.35f };
	localEntity_t	*le;
	vec3_t			dir;

	le = CG_AllocLocalEntity();
	le->leType = LE_FADE_RGB;
	le->startTime = cg.time;
	le->endTime = cg.time + 160;
	le->lifeRate = 1.0f / 160;
	VectorCopy( green, le->color );
	le->color[3] = 1.0f;
	le->refEntity.reType = RT_RAIL_CORE;
	le->refEntity.customShader = cgs.media.fxSparkShader;
	VectorCopy( start, le->refEntity.origin );
	VectorCopy( end, le->refEntity.oldorigin );
	le->refEntity.shaderRGBA[0] = 80;
	le->refEntity.shaderRGBA[1] = 255;
	le->refEntity.shaderRGBA[2] = 90;
	le->refEntity.shaderRGBA[3] = 255;

	VectorSubtract( start, end, dir );
	VectorNormalize( dir );
	CG_FxBurst( end, dir, green, 4, 200, 250, 0.3f, 0 );
	if ( random() < 0.35f ) {
		trap_S_StartSound( end, ENTITYNUM_WORLD, CHAN_AUTO, cgs.media.bfgZapSound );
	}
}

/*
==================
CG_RicochetFX

A nail bouncing off a wall
==================
*/
void CG_RicochetFX( vec3_t origin ) {
	static const vec3_t	hot = { 1.0f, 0.85f, 0.55f };
	static const vec3_t	up = { 0, 0, 1 };
	int		r = rand() % 3;

	trap_S_StartSound( origin, ENTITYNUM_WORLD, CHAN_AUTO,
		r == 0 ? cgs.media.sfx_ric1 : r == 1 ? cgs.media.sfx_ric2 : cgs.media.sfx_ric3 );
	if ( cg_fx.integer ) {
		CG_FxBurst( origin, up, hot, 5, 300, 300, 0.0f, 0 );
		CG_FxFlash( origin, hot, 5, 12, 80 );
	}
}

/*
==================
CG_GibFX

Blood burst and flash when a player is gibbed
==================
*/
void CG_GibFX( vec3_t origin ) {
	static const vec3_t	blood = { 0.8f, 0.05f, 0.03f };
	static const vec3_t	up = { 0, 0, 1 };
	vec3_t	org;
	int		i;

	if ( !cg_fx.integer ) {
		return;
	}
	CG_FxBurst( origin, up, blood, 24, 420, 700, 0.1f, 0 );
	for ( i = 0 ; i < 5 ; i++ ) {
		VectorCopy( origin, org );
		org[0] += crandom() * 20;
		org[1] += crandom() * 20;
		org[2] += crandom() * 24;
		CG_SmokePuff( org, vec3_origin, 26 + random() * 14, 0.55f, 0.05f, 0.05f, 0.7f, 700 + random() * 300,
			cg.time, 0, 0, cgs.media.bloodTrailShader );
	}
}

/*
==================
CG_WeaponKick

View kick and gun recoil of the local player's shot, per weapon
==================
*/
void CG_WeaponKick( int weapon ) {
	static const float	kick[] = {
		0,		// none
		1.5f,	// gauntlet
		0.7f,	// machinegun
		4.0f,	// shotgun
		2.2f,	// grenade launcher
		3.2f,	// rocket launcher
		0.25f,	// lightning
		4.5f,	// railgun
		0.6f,	// plasma
		5.5f,	// BFG
		1.0f,	// grapple
		2.4f,	// nailgun
		2.2f,	// prox launcher
		0.8f,	// chaingun
	};

	if ( weapon <= 0 || weapon >= ARRAY_LEN( kick ) ) {
		return;
	}
	cg.kickTime = cg.time;
	cg.kickPitch = kick[weapon] * ( 0.85f + 0.3f * random() );
	cg.kickYaw = crandom() * kick[weapon] * 0.25f;
}

/*
==================
CG_MissileGlow

Bright core sprite on rockets and alt-fire orbs, fed to the bloom
==================
*/
void CG_MissileGlow( centity_t *cent ) {
	refEntity_t	glow;
	float		radius;
	vec3_t		color, vel;
	qboolean	charged;

	if ( !cg_fx.integer ) {
		return;
	}
	switch ( cent->currentState.weapon ) {
	case WP_ROCKET_LAUNCHER:
		radius = 14;
		VectorSet( color, 1.0f, 0.6f, 0.25f );
		break;
	case WP_PLASMAGUN:
		if ( !( cent->currentState.generic1 & ALTFIRE_PROJECTILE ) ) {
			return;
		}
		radius = 40;
		VectorSet( color, 0.5f, 0.35f, 1.0f );
		CG_SingularitySwirl( cent->lerpOrigin );
		break;
	case WP_BFG:
		radius = 30;
		VectorSet( color, 0.35f, 1.0f, 0.4f );
		break;
	case WP_GRENADE_LAUNCHER:
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case WP_NAILGUN:
	case WP_PROX_LAUNCHER:
#endif
		if ( !( cent->currentState.generic1 & ALTFIRE_CHARGED ) ) {
			return;
		}
		radius = 12;
		VectorSet( color, 1.0f, 0.55f, 0.2f );
		break;
	default:
		return;
	}
	charged = ( cent->currentState.generic1 & ALTFIRE_CHARGED ) != 0;
	if ( charged ) {
		// charged shots burn brighter, pulse, and leave a trail of fire
		radius *= 1.6f + 0.25f * sin( cg.time * 0.025f );
		trap_R_AddLightToScene( cent->lerpOrigin, 160 + radius * 3, color[0], color[1], color[2] );
		if ( cg.time >= cent->rmFxTime ) {
			cent->rmFxTime = cg.time + 25;
			VectorSet( vel, crandom() * 20, crandom() * 20, 10 + crandom() * 20 );
			CG_GoreFire( cent->lerpOrigin, vel, radius * 0.35f, radius * 0.9f, 260 + (int)( random() * 160 ), color, 2, 30 );
		}
	}
	memset( &glow, 0, sizeof( glow ) );
	glow.reType = RT_SPRITE;
	VectorCopy( cent->lerpOrigin, glow.origin );
	glow.radius = radius * ( 0.9f + 0.2f * random() );
	glow.customShader = cgs.media.fxGlowShader;
	glow.shaderRGBA[0] = 255 * color[0];
	glow.shaderRGBA[1] = 255 * color[1];
	glow.shaderRGBA[2] = 255 * color[2];
	glow.shaderRGBA[3] = 255;
	trap_R_AddRefEntityToScene( &glow );
}

/*
==================
CG_FxChargeGlow

The local player's view weapon glows at the muzzle while an alt-fire charges,
brighter as it fills, pulsing when full. Called from CG_AddPlayerWeapon.
==================
*/
void CG_FxChargeGlow( refEntity_t *gun, qhandle_t weaponModel, playerState_t *ps ) {
	refEntity_t	glow;
	float		f, pulse;
	vec3_t		color;

	if ( !cg_fx.integer || !ps || ps->stats[STAT_ALTCHARGE] < ALT_TAP_TIME || ps->stats[STAT_HEALTH] <= 0 ) {
		return;
	}
	f = (float)( ps->stats[STAT_ALTCHARGE] - ALT_TAP_TIME ) / ( ALT_CHARGE_TIME - ALT_TAP_TIME );
	f = CG_Clamp01( f );
	switch ( ps->weapon ) {
	case WP_LIGHTNING:
	case WP_PLASMAGUN:
		VectorSet( color, 0.45f, 0.65f, 1.0f );
		break;
	case WP_BFG:
		VectorSet( color, 0.4f, 1.0f, 0.45f );
		break;
	case WP_RAILGUN:
		VectorSet( color, 0.85f, 0.9f, 1.0f );
		break;
	case WP_SHOTGUN:
		VectorSet( color, 0.75f, 0.9f, 1.0f );
		break;
	default:
		VectorSet( color, 1.0f, 0.55f, 0.18f );
		break;
	}
	pulse = f >= 1.0f ? 0.75f + 0.25f * sin( cg.time * 0.03f ) : 0.6f + 0.4f * f;
	memset( &glow, 0, sizeof( glow ) );
	CG_PositionEntityOnTag( &glow, gun, weaponModel, "tag_flash" );
	glow.reType = RT_SPRITE;
	glow.renderfx = gun->renderfx;
	glow.radius = ( 2.5f + 7.0f * f ) * ( 0.9f + 0.2f * random() );
	glow.customShader = cgs.media.fxGlowShader;
	glow.shaderRGBA[0] = 255 * color[0] * pulse;
	glow.shaderRGBA[1] = 255 * color[1] * pulse;
	glow.shaderRGBA[2] = 255 * color[2] * pulse;
	glow.shaderRGBA[3] = 255;
	trap_R_AddRefEntityToScene( &glow );
	if ( f > 0.3f ) {
		trap_R_AddLightToScene( glow.origin, 60 + 90 * f * pulse, color[0], color[1], color[2] );
	}
}

/*
==================
CG_ElementsPlayerFX

Burning players trail flames, soaked ones drip, shocked ones crackle
==================
*/
void CG_ElementsPlayerFX( centity_t *cent ) {
#ifndef MISSIONPACK
	static const vec3_t	blue = { 0.3f, 0.6f, 1.0f };
	static const vec3_t	cyan = { 0.55f, 0.9f, 1.0f };
	int		powerups = cent->currentState.powerups;
	vec3_t	org, vel;

	if ( !( powerups & ( ( 1 << PW_BURNING ) | ( 1 << PW_WET ) | ( 1 << PW_SHOCKED ) ) ) ) {
		return;
	}
	if ( powerups & ( 1 << PW_BURNING ) ) {
		trap_R_AddLightToScene( cent->lerpOrigin, 140 + 20 * sin( cg.time * 0.03f ), 1.0f, 0.5f, 0.1f );
		trap_S_AddLoopingSound( cent->currentState.number, cent->lerpOrigin, vec3_origin, cgs.media.fireLoopSound );
	}
	if ( powerups & ( 1 << PW_SHOCKED ) ) {
		trap_R_AddLightToScene( cent->lerpOrigin, 90 + 60 * random(), 0.4f, 0.8f, 1.0f );
	}
	if ( !cg_fx.integer || cg.time < cent->rmFxTime ) {
		return;
	}
	cent->rmFxTime = cg.time + 35;

	VectorCopy( cent->lerpOrigin, org );
	org[0] += crandom() * 12;
	org[1] += crandom() * 12;
	org[2] += -16 + random() * 44;
	if ( powerups & ( 1 << PW_BURNING ) ) {
		static const vec3_t	flame = { 1.0f, 0.42f, 0.1f };
		VectorSet( vel, crandom() * 15, crandom() * 15, 60 + random() * 50 );
		CG_SmokePuff( org, vel, 8 + random() * 6, 1.0f, 0.6f + 0.2f * random(), 0.25f, 0.9f, 380,
			cg.time, 0, 0, cgs.media.flameShader );
		// licking fire puffs and a thin smoke
		CG_GoreFire( org, vel, 4 + 3 * random(), 12 + 6 * random(), 300 + (int)( random() * 200 ), flame, 1.5f, 90 );
		if ( random() < 0.2f ) {
			VectorSet( vel, crandom() * 10, crandom() * 10, 40 + random() * 30 );
			CG_GoreSmoke( org, vel, 6, 24, 1200, 0.15f, 0.35f, 20, 150 );
		}
	}
	if ( powerups & ( 1 << PW_WET ) ) {
		VectorSet( vel, crandom() * 20, crandom() * 20, -40 );
		CG_FxSpark( org, vel, blue, 450, 0, qtrue, 2.5f );
	}
	if ( powerups & ( 1 << PW_SHOCKED ) ) {
		VectorSet( vel, crandom() * 160, crandom() * 160, crandom() * 160 );
		CG_FxSpark( org, vel, cyan, 180, 0, qfalse, 1.5f );
	}
#endif
}

/*
==================
CG_FirePatch

Burning ground from grenades and napalm: flames that fade in and out
==================
*/
void CG_FirePatch( centity_t *cent ) {
	static const vec3_t	ember = { 1.0f, 0.5f, 0.1f };
	entityState_t	*s = &cent->currentState;
	float	life, radius, a;
	vec3_t	org, vel;
	int		i;

	radius = s->otherEntityNum2 > 0 ? s->otherEntityNum2 : 80;
	life = 1.0f;
	if ( cg.time - s->time < 250 ) {
		life = ( cg.time - s->time ) / 250.0f;
	} else if ( s->time2 - cg.time < 600 ) {
		life = ( s->time2 - cg.time ) / 600.0f;
	}
	if ( life <= 0 ) {
		return;
	}
	trap_R_AddLightToScene( cent->lerpOrigin, radius * ( 1.6f + 0.2f * sin( cg.time * 0.02f ) ) * life, 1.0f, 0.45f, 0.08f );
	trap_S_AddLoopingSound( s->number, cent->lerpOrigin, vec3_origin, cgs.media.fireLoopSound );

	if ( cg.time < cent->rmFxTime ) {
		return;
	}
	cent->rmFxTime = cg.time + ( cg_fx.integer ? 28 : 70 );
	for ( i = 0 ; i < 2 ; i++ ) {
		a = random() * 2 * M_PI;
		VectorCopy( cent->lerpOrigin, org );
		org[0] += cos( a ) * radius * 0.8f * random();
		org[1] += sin( a ) * radius * 0.8f * random();
		org[2] += 4;
		VectorSet( vel, crandom() * 10, crandom() * 10, 70 + random() * 70 );
		CG_SmokePuff( org, vel, 12 + random() * 12 * life, 1.0f, 0.55f + 0.2f * random(), 0.2f, 0.95f * life,
			500 + random() * 300, cg.time, 0, 0, cgs.media.flameShader );
	}
	if ( cg_fx.integer ) {
		static const vec3_t	flame = { 1.0f, 0.42f, 0.1f };
		// fire puffs over the whole patch, and now and then a curl of dark smoke
		a = random() * 2 * M_PI;
		VectorCopy( cent->lerpOrigin, org );
		org[0] += cos( a ) * radius * 0.75f * sqrt( random() );
		org[1] += sin( a ) * radius * 0.75f * sqrt( random() );
		org[2] += 6;
		VectorSet( vel, crandom() * 12, crandom() * 12, 40 + random() * 50 );
		CG_GoreFire( org, vel, ( 7 + 6 * random() ) * life, ( 18 + 14 * random() ) * life, 420 + (int)( random() * 300 ), flame, 1.2f, 110 );
		if ( random() < 0.12f ) {
			CG_GoreSmoke( org, vel, 10, 40 + radius * 0.2f, 1800 + (int)( random() * 900 ), 0.12f, 0.4f * life, 30, 200 );
		}
		if ( random() < 0.15f ) {
			VectorSet( vel, crandom() * 40, crandom() * 40, 120 + random() * 120 );
			CG_FxSpark( org, vel, ember, 900, 0.3f, qtrue, 3 );
		}
	}
}

/*
==================
CG_WindBlastFX

Shotgun air blast: a cone of rushing air
==================
*/
void CG_WindBlastFX( entityState_t *es ) {
	vec3_t	dir, right, up, org, vel;
	float	charge = es->eventParm / 255.0f, speed;
	int		i, n;

	VectorCopy( es->angles2, dir );
	PerpendicularVector( right, dir );
	CrossProduct( dir, right, up );
	trap_S_StartSound( es->pos.trBase, es->otherEntityNum, CHAN_WEAPON, cgs.media.windSound );
	n = 10 + 10 * charge;
	speed = 700 + 500 * charge;
	for ( i = 0 ; i < n ; i++ ) {
		VectorMA( es->pos.trBase, 8 + random() * 24, dir, org );
		VectorScale( dir, speed * ( 0.6f + 0.4f * random() ), vel );
		VectorMA( vel, crandom() * speed * 0.35f, right, vel );
		VectorMA( vel, crandom() * speed * 0.35f, up, vel );
		CG_SmokePuff( org, vel, 10 + random() * 10, 0.85f, 0.9f, 0.95f, 0.35f, 350 + random() * 200,
			cg.time, 0, 0, cgs.media.smokePuffShader );
	}
	CG_AddScreenShake( es->pos.trBase, 1.5f + 2 * charge, 200 );
}

/*
==================
CG_ThunderFX

Thunderstrike: a jagged bolt from the sky, a flash and a boom
==================
*/
void CG_ThunderFX( vec3_t ground, vec3_t sky ) {
	static const vec3_t	white = { 0.85f, 0.95f, 1.0f };
	static const vec3_t	up = { 0, 0, 1 };
	vec3_t	a, b;
	int		i;

	VectorCopy( sky, a );
	for ( i = 1 ; i <= 5 ; i++ ) {
		VectorScale( sky, 1.0f - i / 5.0f, b );
		VectorMA( b, i / 5.0f, ground, b );
		if ( i < 5 ) {
			b[0] += crandom() * 40;
			b[1] += crandom() * 40;
		}
		CG_ArcBeam( a, b );
		VectorCopy( b, a );
	}
	CG_ArcBeam( sky, ground );
	CG_FxFlash( ground, white, 40, 320, 350 );
	CG_FxRing( ground, up, white, 300, 450 );
	CG_FxBurst( ground, up, white, 30, 500, 600, 0.3f, 8 );
	if ( cg_fx.integer ) {
		static const vec3_t	bolt = { 0.55f, 0.72f, 1.0f };
		CG_FxExplosion( ground, up, 0.7f, bolt, qtrue );
		CG_ImpactMark( cgs.media.burnMarkShader, ground, up, random() * 360, 1, 1, 1, 1, qfalse, 72, qfalse );
	}
	trap_S_StartSound( ground, ENTITYNUM_WORLD, CHAN_AUTO, cgs.media.thunderSound );
	CG_AddScreenShake( ground, 7, 900 );
}

/*
==================
CG_SteamFX

Fire meets water
==================
*/
void CG_SteamFX( vec3_t origin ) {
	vec3_t	org, vel;
	int		i;

	trap_S_StartSound( origin, ENTITYNUM_WORLD, CHAN_AUTO, cgs.media.steamSound );
	for ( i = 0 ; i < 8 ; i++ ) {
		VectorCopy( origin, org );
		org[0] += crandom() * 14;
		org[1] += crandom() * 14;
		org[2] += random() * 30;
		VectorSet( vel, crandom() * 30, crandom() * 30, 50 + random() * 60 );
		CG_SmokePuff( org, vel, 16 + random() * 10, 0.9f, 0.9f, 0.9f, 0.55f, 900 + random() * 400,
			cg.time, 0, 0, cgs.media.smokePuffShader );
	}
}
