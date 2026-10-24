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
// cg_moves.c -- rearmed: the feel of the moves. Slide / ground pound / blink effects and
// sounds, and the camera motion (landing spring, slide dip and roll, wall run roll, FOV).

#include "cg_local.h"

static sfxHandle_t	slideSound, slamStartSound, slamLandSound, slamBigSound, blinkSound;

static int		slideStart[MAX_CLIENTS];	// cg.time of the last EV_SLIDE per client
static int		slamStart[MAX_CLIENTS];		// ... EV_SLAM
static int		slamLand[MAX_CLIENTS];		// ... EV_SLAM_LAND
static int		fxNext[MAX_CLIENTS];		// next trail puff time

// camera state
static int		camTime;
static float	springPos, springVel;		// landing dip (units, units/s)
static float	lastVz;
static qboolean	wasOnGround;
static float	slideAmt;					// 0..1 eased slide pose
static float	slideRoll;
static float	fovSpeed;					// eased speed FOV widening (degrees)
static int		fovPunchTime;
static float	fovPunch;

/*
==================
CG_MovesRegister

Called from CG_RegisterSounds
==================
*/
void CG_MovesRegister( void ) {
	slideSound = trap_S_RegisterSound( "sound/rearmed/move/slide.wav", qfalse );
	slamStartSound = trap_S_RegisterSound( "sound/rearmed/move/slam_start.wav", qfalse );
	slamLandSound = trap_S_RegisterSound( "sound/rearmed/move/slam_land.wav", qfalse );
	slamBigSound = trap_S_RegisterSound( "sound/rearmed/move/slam_big.wav", qfalse );
	blinkSound = trap_S_RegisterSound( "sound/rearmed/move/blink.wav", qfalse );
	memset( slideStart, 0, sizeof( slideStart ) );
	memset( slamStart, 0, sizeof( slamStart ) );
	memset( slamLand, 0, sizeof( slamLand ) );
	camTime = 0;
	springPos = springVel = 0;
}

static float CG_MoveCamScale( void ) {
	if ( cg_moveCamera.value <= 0 ) {
		return 0;
	}
	return cg_moveCamera.value > 2 ? 2 : cg_moveCamera.value;
}

/*
==================
Small local-entity helpers (drawn by the LE_FX_ / LE_FADE_RGB code in cg_localents.c / cg_fx.c)
==================
*/
static void CG_MoveSpark( const vec3_t org, const vec3_t vel, float r, float g, float b, int life, qboolean ember, float size ) {
	localEntity_t	*le;

	le = CG_AllocLocalEntity();
	le->leType = LE_FX_SPARK;
	le->leFlags = ember ? LEF_FX_EMBER : 0;
	le->startTime = cg.time;
	le->endTime = cg.time + life;
	le->lifeRate = 1.0f / life;
	le->bounceFactor = 0.35f;
	le->radius = size;
	VectorSet( le->color, r, g, b );
	le->color[3] = 1.0f;
	le->pos.trType = TR_GRAVITY;
	le->pos.trTime = cg.time;
	VectorCopy( org, le->pos.trBase );
	VectorCopy( vel, le->pos.trDelta );
	VectorCopy( org, le->refEntity.origin );
	le->refEntity.customShader = ember ? cgs.media.fxGlowShader : cgs.media.fxSparkShader;
}

static void CG_MoveFlash( const vec3_t org, float r, float g, float b, float radius, float endRadius, int life ) {
	localEntity_t	*le;

	le = CG_AllocLocalEntity();
	le->leType = LE_FX_FLASH;
	le->startTime = cg.time;
	le->endTime = cg.time + life;
	le->lifeRate = 1.0f / life;
	le->radius = radius;
	le->endRadius = endRadius;
	VectorSet( le->color, r, g, b );
	VectorCopy( org, le->refEntity.origin );
	le->refEntity.customShader = cgs.media.fxGlowShader;
	le->refEntity.reType = RT_SPRITE;
}

static void CG_MoveRing( const vec3_t org, float r, float g, float b, float endRadius, int life ) {
	localEntity_t	*le;

	le = CG_AllocLocalEntity();
	le->leType = LE_FX_RING;
	le->startTime = cg.time;
	le->endTime = cg.time + life;
	le->lifeRate = 1.0f / life;
	le->radius = endRadius * 0.15f;
	le->endRadius = endRadius;
	VectorSet( le->color, r, g, b );
	VectorCopy( org, le->refEntity.origin );
	le->refEntity.origin[2] += 2;
	VectorSet( le->refEntity.axis[0], 0, 0, 1 );
	le->refEntity.customShader = cgs.media.fxRingShader;
}

static void CG_MoveBeam( const vec3_t start, const vec3_t end, float r, float g, float b, int life ) {
	localEntity_t	*le;
	refEntity_t		*re;

	le = CG_AllocLocalEntity();
	re = &le->refEntity;
	le->leType = LE_FADE_RGB;
	le->startTime = cg.time;
	le->endTime = cg.time + life;
	le->lifeRate = 1.0f / life;
	re->shaderTime = cg.time / 1000.0f;
	re->reType = RT_RAIL_CORE;
	re->customShader = cgs.media.railCoreShader;
	VectorCopy( start, re->origin );
	VectorCopy( end, re->oldorigin );
	le->color[0] = r;
	le->color[1] = g;
	le->color[2] = b;
	le->color[3] = 1.0f;
	AxisClear( re->axis );
}

/*
==================
CG_MoveShake

Screen shake from a point (same rules as the explosion shake in cg_fx.c)
==================
*/
#define MOVE_SHAKE_TIME	450

static void CG_MoveShake( const vec3_t org, float amplitude, float radius ) {
	float	dist, amp, remaining;

	if ( !cg_screenShake.value ) {
		return;
	}
	dist = Distance( org, cg.refdef.vieworg );
	if ( dist >= radius ) {
		return;
	}
	amp = amplitude * ( 1.0f - dist / radius ) * CG_Clamp01( cg_screenShake.value );
	remaining = cg.shakeAmplitude * ( 1.0f - (float)( cg.time - cg.shakeTime ) / MOVE_SHAKE_TIME );
	if ( !cg.shakeTime || amp > remaining ) {
		cg.shakeTime = cg.time;
		cg.shakeAmplitude = amp;
	}
}

/*
==================
CG_MovePlayerPos

Where a player is right now: the predicted position for us, the interpolated one for others
==================
*/
static qboolean CG_MoveIsLocal( int clientNum ) {
	return cg.snap && clientNum == cg.snap->ps.clientNum && !( cg.snap->ps.pm_flags & PMF_FOLLOW )
		&& !cg.demoPlayback;
}

static void CG_MovePlayerPos( centity_t *cent, vec3_t origin, vec3_t velocity ) {
	if ( CG_MoveIsLocal( cent->currentState.number ) ) {
		VectorCopy( cg.predictedPlayerState.origin, origin );
		VectorCopy( cg.predictedPlayerState.velocity, velocity );
	} else {
		VectorCopy( cent->lerpOrigin, origin );
		VectorCopy( cent->currentState.pos.trDelta, velocity );
	}
}

/*
==================
CG_SlamImpactFX

Shockwave ring, dust, debris and a shake scaled by distance
==================
*/
static void CG_SlamImpactFX( const vec3_t feet, float s ) {
	vec3_t	pos, vel, up;
	float	ang, radius;
	int		i, n;

	radius = 170 + 130 * s;
	VectorSet( up, 0, 0, 1 );

	CG_MoveRing( feet, 1.0f, 0.75f, 0.45f, radius, 380 );
	CG_MoveRing( feet, 0.55f, 0.45f, 0.35f, radius * 0.7f, 650 );
	VectorCopy( feet, pos );
	pos[2] += 8;
	CG_MoveFlash( pos, 1.0f, 0.7f, 0.4f, 24, 90 + 60 * s, 220 );

	// a ring of dust thrown outwards
	n = 12 + (int)( 8 * s );
	for ( i = 0 ; i < n ; i++ ) {
		ang = ( i + random() * 0.5f ) * ( M_PI * 2 / n );
		VectorSet( pos, feet[0] + cos( ang ) * 14, feet[1] + sin( ang ) * 14, feet[2] + 6 );
		VectorSet( vel, cos( ang ) * ( 200 + 180 * s ), sin( ang ) * ( 200 + 180 * s ), 20 + random() * 50 );
		CG_SmokePuff( pos, vel, 16 + 10 * s, 0.55f, 0.5f, 0.45f, 0.55f, 700 + random() * 500,
			cg.time, 0, 0, cgs.media.smokePuffShader );
	}
	// debris and embers kicked up
	n = 8 + (int)( 12 * s );
	for ( i = 0 ; i < n ; i++ ) {
		ang = random() * M_PI * 2;
		VectorSet( vel, cos( ang ) * ( 80 + random() * 180 ), sin( ang ) * ( 80 + random() * 180 ), 200 + random() * 300 * ( 0.5f + s ) );
		VectorSet( pos, feet[0], feet[1], feet[2] + 4 );
		CG_MoveSpark( pos, vel, 1.0f, 0.6f + random() * 0.3f, 0.3f, 500 + rand() % 500, ( i & 1 ), 2.5f );
	}
	// cracked floor
	if ( cg_addMarks.integer ) {
		CG_ImpactMark( cgs.media.burnMarkShader, feet, up, random() * 360, 0.6f, 0.6f, 0.6f, 1.0f,
			qtrue, 28 + 26 * s, qfalse );
	}
	CG_MoveShake( feet, 3.0f + 5.0f * s, 500 + 500 * s );
}

/*
==================
CG_MoveEvent

Player events of the new moves (cg_event.c)
==================
*/
void CG_MoveEvent( centity_t *cent, int event, int parm ) {
	entityState_t	*es = &cent->currentState;
	int				clientNum = es->clientNum;
	qboolean		local;
	vec3_t			origin, vel, feet, start, end, dir;
	float			s, yaw, dist;
	int				i;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}
	local = CG_MoveIsLocal( clientNum );
	CG_MovePlayerPos( cent, origin, vel );
	VectorCopy( origin, feet );
	feet[2] += MINS_Z;

	switch ( event ) {
	case EV_DASH:
		if ( local ) {
			fovPunchTime = cg.time;
			fovPunch = 6;
		}
		break;

	case EV_SLIDE:
		slideStart[clientNum] = cg.time;
		fxNext[clientNum] = 0;
		trap_S_StartSound( NULL, es->number, CHAN_BODY, slideSound );
		for ( i = 0 ; i < 6 ; i++ ) {
			VectorSet( start, feet[0] + crandom() * 12, feet[1] + crandom() * 12, feet[2] + 4 );
			VectorSet( dir, -vel[0] * 0.15f + crandom() * 40, -vel[1] * 0.15f + crandom() * 40, 20 + random() * 30 );
			CG_SmokePuff( start, dir, 12, 0.6f, 0.55f, 0.5f, 0.5f, 600, cg.time, 0, 0, cgs.media.smokePuffShader );
		}
		if ( local ) {
			fovPunchTime = cg.time;
			fovPunch = 3;
		}
		break;

	case EV_SLAM:
		slamStart[clientNum] = cg.time;
		fxNext[clientNum] = 0;
		trap_S_StartSound( NULL, es->number, CHAN_BODY, slamStartSound );
		CG_MoveTechPuff( cent, 0 );
		break;

	case EV_SLAM_LAND:
		slamLand[clientNum] = cg.time;
		s = parm / 255.0f;
		trap_S_StartSound( feet, es->number, CHAN_BODY, slamLandSound );
		if ( s > 0.6f ) {
			trap_S_StartSound( feet, ENTITYNUM_WORLD, CHAN_AUTO, slamBigSound );
		}
		CG_SlamImpactFX( feet, s );
		if ( local ) {
			fovPunchTime = cg.time;
			fovPunch = -4 - 4 * s;		// a squeeze on impact
		}
		break;

	case EV_BLINK:
		// parm: direction in 16ths of a turn, distance / 15
		yaw = ( ( parm >> 4 ) & 15 ) * 22.5f;
		dist = ( parm & 15 ) * 15.0f;
		VectorSet( dir, cos( DEG2RAD( yaw ) ), sin( DEG2RAD( yaw ) ), 0 );
		VectorCopy( origin, end );
		VectorMA( end, -dist, dir, start );
		trap_S_StartSound( start, ENTITYNUM_WORLD, CHAN_AUTO, blinkSound );
		trap_S_StartSound( NULL, es->number, CHAN_BODY, blinkSound );
		// the streak: three beams at feet, waist and head height, glows at both ends
		for ( i = 0 ; i < 3 ; i++ ) {
			vec3_t	a, b;

			VectorCopy( start, a );
			VectorCopy( end, b );
			a[2] += -16 + i * 18;
			b[2] += -16 + i * 18;
			CG_MoveBeam( a, b, 0.35f, 0.75f + 0.1f * i, 1.0f, 260 + 60 * i );
		}
		CG_MoveFlash( start, 0.3f, 0.7f, 1.0f, 30, 6, 350 );
		CG_MoveFlash( end, 0.4f, 0.8f, 1.0f, 12, 46, 250 );
		for ( i = 0 ; i < 10 ; i++ ) {
			VectorMA( start, dist * i / 10.0f, dir, end );
			end[2] += crandom() * 20;
			VectorSet( vel, crandom() * 30, crandom() * 30, crandom() * 30 );
			CG_SmokePuff( end, vel, 10, 0.4f, 0.7f, 1.0f, 0.35f, 300 + i * 25, cg.time, 0, 0, cgs.media.smokePuffShader );
		}
		if ( local ) {
			fovPunchTime = cg.time;
			fovPunch = 10;
		}
		break;
	}
}

/*
==================
CG_MovesFrame

Continuous effects: slide scrape dust and sparks, ground pound trails. Once per frame.
==================
*/
void CG_MovesFrame( void ) {
	centity_t	*cent;
	vec3_t		origin, vel, pos, v, fwd;
	float		speed;
	qboolean	sliding, slamming;
	int			i, rm2;

	if ( !cg.snap ) {
		return;
	}
	for ( i = 0 ; i < MAX_CLIENTS ; i++ ) {
		sliding = slamming = qfalse;
		if ( cg.time - slideStart[i] > SLIDE_TIME * 2 + 100 && cg.time - slamStart[i] > 3000 ) {
			continue;
		}
		if ( slideStart[i] > cg.time || slamStart[i] > cg.time ) {
			slideStart[i] = slamStart[i] = 0;	// a new level / time went back
			continue;
		}
		cent = &cg_entities[i];
		if ( CG_MoveIsLocal( i ) ) {
			rm2 = RM_FLAGS2( &cg.predictedPlayerState );
			sliding = ( rm2 & RMS2_SLIDING ) && cg.predictedPlayerState.groundEntityNum != ENTITYNUM_NONE;
			slamming = ( rm2 & RMS2_SLAMMING ) && cg.predictedPlayerState.velocity[2] < -600;
		} else {
			if ( !cent->currentValid || cent->currentState.eType != ET_PLAYER ) {
				continue;
			}
			speed = sqrt( cent->currentState.pos.trDelta[0] * cent->currentState.pos.trDelta[0]
				+ cent->currentState.pos.trDelta[1] * cent->currentState.pos.trDelta[1] );
			sliding = cg.time - slideStart[i] < SLIDE_TIME * 2 && speed > 200
				&& cent->currentState.groundEntityNum != ENTITYNUM_NONE
				&& ( cent->currentState.legsAnim & ~ANIM_TOGGLEBIT ) == LEGS_IDLECR;
			slamming = slamLand[i] < slamStart[i] && cg.time - slamStart[i] < 3000
				&& cent->currentState.pos.trDelta[2] < -600;
		}
		if ( ( !sliding && !slamming ) || cg.time < fxNext[i] ) {
			continue;
		}
		CG_MovePlayerPos( cent, origin, vel );

		if ( sliding ) {
			fxNext[i] = cg.time + 35;
			speed = sqrt( vel[0] * vel[0] + vel[1] * vel[1] );
			if ( speed < 1 ) {
				continue;
			}
			VectorSet( fwd, vel[0] / speed, vel[1] / speed, 0 );
			// just ahead of the feet, so the slider sees the dust kick up at the bottom of the view
			VectorMA( origin, 18, fwd, pos );
			pos[0] += fwd[1] * crandom() * 12;
			pos[1] -= fwd[0] * crandom() * 12;
			pos[2] += MINS_Z + 3;
			VectorSet( v, -fwd[0] * 60 + crandom() * 25, -fwd[1] * 60 + crandom() * 25, 18 + random() * 30 );
			CG_SmokePuff( pos, v, 11 + random() * 6, 0.75f, 0.7f, 0.62f, 0.55f, 550 + random() * 250,
				cg.time, 0, 0, cgs.media.smokePuffShader );
			if ( rand() & 1 ) {
				VectorSet( v, -fwd[0] * ( 120 + random() * 120 ) + crandom() * 60,
					-fwd[1] * ( 120 + random() * 120 ) + crandom() * 60, 60 + random() * 120 );
				CG_MoveSpark( pos, v, 1.0f, 0.65f, 0.3f, 220 + rand() % 180, qfalse, 1.5f );
			}
		} else {
			fxNext[i] = cg.time + 25;
			// plunge trail: a column of pale air
			VectorCopy( origin, pos );
			pos[2] += 10;
			VectorSet( v, crandom() * 15, crandom() * 15, 60 );
			CG_SmokePuff( pos, v, 14, 0.75f, 0.85f, 1.0f, 0.3f, 350, cg.time, 0, 0, cgs.media.smokePuffShader );
		}
	}
}

/*
==================
CG_MoveCamera

First person camera motion of the moves (cg_view.c CG_OffsetFirstPersonView):
landing spring, slide dip and roll, wall run roll. Returns qtrue when the landing
dip was handled here (so the old linear landing offset is skipped).
==================
*/
qboolean CG_MoveCamera( vec3_t origin, vec3_t angles ) {
	playerState_t	*ps = &cg.predictedPlayerState;
	float			scale, dt, step, target, side, speed;
	qboolean		onGround;
	int				msec, rm, rm2;

	scale = CG_MoveCamScale();
	msec = cg.time - camTime;
	camTime = cg.time;
	if ( msec < 0 || msec > 200 ) {
		msec = 0;		// a pause, a new level, or time went back
		springPos = springVel = 0;
	}

	onGround = ps->groundEntityNum != ENTITYNUM_NONE;
	rm = ps->stats[STAT_RMFLAGS];
	rm2 = RM_FLAGS2( ps );

	// landing spring: the eye dips with the impact and settles back with a little overshoot
	if ( ps->pm_type != PM_NORMAL ) {
		springPos = springVel = 0;
	} else if ( onGround && !wasOnGround && lastVz < -200 ) {		// (stair step-downs land at ~-160)
		float	impact = -lastVz;

		if ( impact > 1300 ) {
			impact = 1300;
		}
		springVel -= impact * 0.2f;
	}
	wasOnGround = onGround;
	if ( !onGround ) {
		lastVz = ps->velocity[2];
	}
	while ( msec > 0 ) {
		// underdamped spring, w = 15 rad/s, damping 0.5, 4 msec steps
		int		n = msec > 4 ? 4 : msec;

		dt = n * 0.001f;
		springVel += ( -225.0f * springPos - 15.0f * springVel ) * dt;
		springPos += springVel * dt;
		if ( springPos < -14 ) {
			springPos = -14;
			if ( springVel < 0 ) {
				springVel = 0;
			}
		}
		msec -= n;
	}
	dt = cg.frametime > 0 && cg.frametime < 200 ? (float)cg.frametime : 0;

	// slide: sink a little lower than a crouch, and lean into the slide
	target = ( rm2 & RMS2_SLIDING ) ? 1.0f : 0.0f;
	step = dt * ( target > slideAmt ? 0.012f : 0.006f );
	if ( step > 1 ) {
		step = 1;
	}
	slideAmt += ( target - slideAmt ) * step;
	side = 0;
	speed = sqrt( ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1] );
	if ( speed > 50 ) {
		vec3_t	right;

		AngleVectors( ps->viewangles, NULL, right, NULL );
		side = ( ps->velocity[0] * right[0] + ps->velocity[1] * right[1] ) / speed;
	}
	target = slideAmt * ( 2.5f + 3.0f * side );
	step = dt * 0.008f;
	if ( step > 1 ) {
		step = 1;
	}
	slideRoll += ( target - slideRoll ) * step;

	// wall run: lean away from the wall (eased)
	target = 0;
	if ( rm & RMS_WALLRUN ) {
		target = ( rm & RMS_WALLRUN_LEFT ) ? 7.0f : -7.0f;
	}
	step = dt * ( target != 0 ? 0.010f : 0.007f );
	if ( step > 1.0f ) {
		step = 1.0f;
	}
	cg.rmWallRoll += ( target - cg.rmWallRoll ) * step;

	if ( scale <= 0 ) {
		angles[ROLL] += cg.rmWallRoll;
		return qfalse;
	}
	angles[ROLL] += ( cg.rmWallRoll + slideRoll ) * scale;
	origin[2] += ( springPos - 5.0f * slideAmt ) * scale;
	angles[PITCH] -= springPos * 0.25f * scale;		// the head nods with the dip
	return qtrue;
}

/*
==================
CG_MoveFov

Speed FOV widening and the dash / blink / slam punches (cg_view.c CG_CalcFov)
==================
*/
float CG_MoveFov( float fov ) {
	playerState_t	*ps = &cg.predictedPlayerState;
	float			speed, target, step, f, scale;
	int				t;

	scale = cg_speedFov.value;
	if ( scale <= 0 || ps->pm_type != PM_NORMAL ) {
		fovSpeed = 0;
		return fov;
	}
	if ( scale > 2 ) {
		scale = 2;
	}
	speed = VectorLength( ps->velocity );
	target = ( speed - 380 ) / 600.0f;
	if ( target < 0 ) {
		target = 0;
	} else if ( target > 1 ) {
		target = 1;
	}
	target *= 8.0f;
	step = cg.frametime * 0.004f;
	if ( step > 1 ) {
		step = 1;
	} else if ( step < 0 ) {
		step = 0;
	}
	fovSpeed += ( target - fovSpeed ) * step;

	f = 0;
	t = cg.time - fovPunchTime;
	if ( fovPunchTime && t >= 0 && t < 450 ) {
		if ( t < 60 ) {
			f = t / 60.0f;
		} else {
			f = 1.0f - ( t - 60 ) / 390.0f;
			f *= f;
		}
	}
	if ( cg.zoomed ) {
		return fov;
	}
	fov += ( fovSpeed + fovPunch * f ) * scale;
	if ( fov > 170 ) {
		fov = 170;
	}
	return fov;
}
