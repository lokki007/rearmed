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
// bg_pmove.c -- both games player movement code
// takes a playerstate and a usercmd as input and returns a modifed playerstate

#include "../qcommon/q_shared.h"
#include "bg_public.h"
#include "bg_local.h"

pmove_t		*pm;
pml_t		pml;

// movement parameters
float	pm_stopspeed = 100.0f;
float	pm_duckScale = 0.25f;
float	pm_swimScale = 0.50f;

float	pm_accelerate = 10.0f;
float	pm_airaccelerate = 1.0f;
float	pm_wateraccelerate = 4.0f;
float	pm_flyaccelerate = 8.0f;

float	pm_friction = 6.0f;
float	pm_waterfriction = 1.0f;
float	pm_flightfriction = 3.0f;
float	pm_spectatorfriction = 5.0f;

int		c_pmove = 0;


/*
===============
PM_AddEvent

===============
*/
void PM_AddEvent( int newEvent ) {
	BG_AddPredictableEventToPlayerstate( newEvent, 0, pm->ps );
}

/*
===============
PM_AddTouchEnt
===============
*/
void PM_AddTouchEnt( int entityNum ) {
	int		i;

	if ( entityNum == ENTITYNUM_WORLD ) {
		return;
	}
	if ( pm->numtouch == MAXTOUCH ) {
		return;
	}

	// see if it is already added
	for ( i = 0 ; i < pm->numtouch ; i++ ) {
		if ( pm->touchents[ i ] == entityNum ) {
			return;
		}
	}

	// add it
	pm->touchents[pm->numtouch] = entityNum;
	pm->numtouch++;
}

/*
===================
PM_StartTorsoAnim
===================
*/
static void PM_StartTorsoAnim( int anim ) {
	if ( pm->ps->pm_type >= PM_DEAD ) {
		return;
	}
	pm->ps->torsoAnim = ( ( pm->ps->torsoAnim & ANIM_TOGGLEBIT ) ^ ANIM_TOGGLEBIT )
		| anim;
}
static void PM_StartLegsAnim( int anim ) {
	if ( pm->ps->pm_type >= PM_DEAD ) {
		return;
	}
	if ( pm->ps->legsTimer > 0 ) {
		return;		// a high priority animation is running
	}
	pm->ps->legsAnim = ( ( pm->ps->legsAnim & ANIM_TOGGLEBIT ) ^ ANIM_TOGGLEBIT )
		| anim;
}

static void PM_ContinueLegsAnim( int anim ) {
	if ( ( pm->ps->legsAnim & ~ANIM_TOGGLEBIT ) == anim ) {
		return;
	}
	if ( pm->ps->legsTimer > 0 ) {
		return;		// a high priority animation is running
	}
	PM_StartLegsAnim( anim );
}

static void PM_ContinueTorsoAnim( int anim ) {
	if ( ( pm->ps->torsoAnim & ~ANIM_TOGGLEBIT ) == anim ) {
		return;
	}
	if ( pm->ps->torsoTimer > 0 ) {
		return;		// a high priority animation is running
	}
	PM_StartTorsoAnim( anim );
}

static void PM_ForceLegsAnim( int anim ) {
	pm->ps->legsTimer = 0;
	PM_StartLegsAnim( anim );
}


/*
==================
PM_ClipVelocity

Slide off of the impacting surface
==================
*/
void PM_ClipVelocity( vec3_t in, vec3_t normal, vec3_t out, float overbounce ) {
	float	backoff;
	float	change;
	int		i;

	backoff = DotProduct (in, normal);

	if ( backoff < 0 ) {
		backoff *= overbounce;
	} else {
		backoff /= overbounce;
	}

	for ( i=0 ; i<3 ; i++ ) {
		change = normal[i]*backoff;
		out[i] = in[i] - change;
	}
}


/*
==================
PM_Friction

Handles both ground friction and water friction
==================
*/
static void PM_Friction( void ) {
	vec3_t	vec;
	float	*vel;
	float	speed, newspeed, control;
	float	drop;

	vel = pm->ps->velocity;

	VectorCopy( vel, vec );
	if ( pml.walking ) {
		vec[2] = 0;	// ignore slope movement
	}

	speed = VectorLength(vec);
	if (speed < 1) {
		vel[0] = 0;
		vel[1] = 0;		// allow sinking underwater
		// FIXME: still have z friction underwater?
		return;
	}

	drop = 0;

	// apply ground friction
	if ( pm->waterlevel <= 1 ) {
		if ( pml.walking && !(pml.groundTrace.surfaceFlags & SURF_SLICK) ) {
			// if getting knocked back, no friction
			if ( ! (pm->ps->pm_flags & PMF_TIME_KNOCKBACK) ) {
				control = speed < pm_stopspeed ? pm_stopspeed : speed;
				drop += control*pm_friction*pml.frametime
					* ( ( RM_FLAGS2( pm->ps ) & RMF2_ICE ) ? ICE_FRICTION : 1.0f );
			}
		}
	}

	// apply water friction even if just wading
	if ( pm->waterlevel ) {
		drop += speed*pm_waterfriction*pm->waterlevel*pml.frametime;
	}

	// apply flying friction
	if ( pm->ps->powerups[PW_FLIGHT]) {
		drop += speed*pm_flightfriction*pml.frametime;
	}

	if ( pm->ps->pm_type == PM_SPECTATOR) {
		drop += speed*pm_spectatorfriction*pml.frametime;
	}

	// scale the velocity
	newspeed = speed - drop;
	if (newspeed < 0) {
		newspeed = 0;
	}
	newspeed /= speed;

	vel[0] = vel[0] * newspeed;
	vel[1] = vel[1] * newspeed;
	vel[2] = vel[2] * newspeed;
}


/*
==============
PM_Accelerate

Handles user intended acceleration
==============
*/
static void PM_Accelerate( vec3_t wishdir, float wishspeed, float accel ) {
#if 1
	// q2 style
	int			i;
	float		addspeed, accelspeed, currentspeed;

	currentspeed = DotProduct (pm->ps->velocity, wishdir);
	addspeed = wishspeed - currentspeed;
	if (addspeed <= 0) {
		return;
	}
	accelspeed = accel*pml.frametime*wishspeed;
	if (accelspeed > addspeed) {
		accelspeed = addspeed;
	}

	for (i=0 ; i<3 ; i++) {
		pm->ps->velocity[i] += accelspeed*wishdir[i];
	}
#else
	// proper way (avoids strafe jump maxspeed bug), but feels bad
	vec3_t		wishVelocity;
	vec3_t		pushDir;
	float		pushLen;
	float		canPush;

	VectorScale( wishdir, wishspeed, wishVelocity );
	VectorSubtract( wishVelocity, pm->ps->velocity, pushDir );
	pushLen = VectorNormalize( pushDir );

	canPush = accel*pml.frametime*wishspeed;
	if (canPush > pushLen) {
		canPush = pushLen;
	}

	VectorMA( pm->ps->velocity, canPush, pushDir, pm->ps->velocity );
#endif
}



/*
============
PM_CmdScale

Returns the scale factor to apply to cmd movements
This allows the clients to use axial -127 to 127 values for all directions
without getting a sqrt(2) distortion in speed.
============
*/
static float PM_CmdScale( usercmd_t *cmd ) {
	int		max;
	float	total;
	float	scale;

	max = abs( cmd->forwardmove );
	if ( abs( cmd->rightmove ) > max ) {
		max = abs( cmd->rightmove );
	}
	if ( abs( cmd->upmove ) > max ) {
		max = abs( cmd->upmove );
	}
	if ( !max ) {
		return 0;
	}

	total = sqrt( cmd->forwardmove * cmd->forwardmove
		+ cmd->rightmove * cmd->rightmove + cmd->upmove * cmd->upmove );
	scale = (float)pm->ps->speed * max / ( 127.0 * total );

	return scale;
}


/*
================
PM_SetMovementDir

Determine the rotation of the legs relative
to the facing dir
================
*/
static void PM_SetMovementDir( void ) {
	if ( pm->cmd.forwardmove || pm->cmd.rightmove ) {
		if ( pm->cmd.rightmove == 0 && pm->cmd.forwardmove > 0 ) {
			pm->ps->movementDir = 0;
		} else if ( pm->cmd.rightmove < 0 && pm->cmd.forwardmove > 0 ) {
			pm->ps->movementDir = 1;
		} else if ( pm->cmd.rightmove < 0 && pm->cmd.forwardmove == 0 ) {
			pm->ps->movementDir = 2;
		} else if ( pm->cmd.rightmove < 0 && pm->cmd.forwardmove < 0 ) {
			pm->ps->movementDir = 3;
		} else if ( pm->cmd.rightmove == 0 && pm->cmd.forwardmove < 0 ) {
			pm->ps->movementDir = 4;
		} else if ( pm->cmd.rightmove > 0 && pm->cmd.forwardmove < 0 ) {
			pm->ps->movementDir = 5;
		} else if ( pm->cmd.rightmove > 0 && pm->cmd.forwardmove == 0 ) {
			pm->ps->movementDir = 6;
		} else if ( pm->cmd.rightmove > 0 && pm->cmd.forwardmove > 0 ) {
			pm->ps->movementDir = 7;
		}
	} else {
		// if they aren't actively going directly sideways,
		// change the animation to the diagonal so they
		// don't stop too crooked
		if ( pm->ps->movementDir == 2 ) {
			pm->ps->movementDir = 1;
		} else if ( pm->ps->movementDir == 6 ) {
			pm->ps->movementDir = 7;
		}
	}
}


/*
=============
PM_CheckJump
=============
*/
static qboolean PM_CheckJump( void ) {
	if ( pm->ps->pm_flags & PMF_RESPAWNED ) {
		return qfalse;		// don't allow jump until all buttons are up
	}

	if ( pm->cmd.upmove < 10 ) {
		// not holding jump
		return qfalse;
	}

	// must wait for jump to be released
	if ( pm->ps->pm_flags & PMF_JUMP_HELD ) {
		// clear upmove so cmdscale doesn't lower running speed
		pm->cmd.upmove = 0;
		return qfalse;
	}

	pml.groundPlane = qfalse;		// jumping away
	pml.walking = qfalse;
	pm->ps->pm_flags |= PMF_JUMP_HELD;

	pm->ps->groundEntityNum = ENTITYNUM_NONE;
	pm->ps->velocity[2] = JUMP_VELOCITY;
	PM_AddEvent( EV_JUMP );

	if ( pm->cmd.forwardmove >= 0 ) {
		PM_ForceLegsAnim( LEGS_JUMP );
		pm->ps->pm_flags &= ~PMF_BACKWARDS_JUMP;
	} else {
		PM_ForceLegsAnim( LEGS_JUMPB );
		pm->ps->pm_flags |= PMF_BACKWARDS_JUMP;
	}

	return qtrue;
}

#define RMF(x)	( pm->ps->stats[STAT_RMFLAGS] & (x) )

/*
=============
PM_FindWall

Looks for a wall close to the player's sides to kick off from
=============
*/
static qboolean PM_FindWall( vec3_t normal ) {
	static const float	dirs[8][2] = {
		{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
		{ 0.7071f, 0.7071f }, { 0.7071f, -0.7071f }, { -0.7071f, 0.7071f }, { -0.7071f, -0.7071f }
	};
	trace_t		trace;
	vec3_t		end;
	float		best, dist;
	int			i;

	// kicking off the wall we're running on can reach a bit further
	dist = ( pm->ps->stats[STAT_RMFLAGS] & RMS_WALLRUN ) ? WALLRUN_DIST : WALL_JUMP_DIST;
	best = 1.0f;
	for ( i = 0 ; i < 8 ; i++ ) {
		end[0] = pm->ps->origin[0] + dirs[i][0] * dist;
		end[1] = pm->ps->origin[1] + dirs[i][1] * dist;
		end[2] = pm->ps->origin[2];
		pm->trace( &trace, pm->ps->origin, pm->mins, pm->maxs, end, pm->ps->clientNum, pm->tracemask );
		if ( trace.allsolid || trace.fraction >= best ) {
			continue;
		}
		// only mostly vertical world surfaces, never other players or the sky
		if ( fabs( trace.plane.normal[2] ) > 0.7f || trace.entityNum < MAX_CLIENTS
			|| ( trace.surfaceFlags & SURF_SKY ) ) {
			continue;
		}
		best = trace.fraction;
		VectorCopy( trace.plane.normal, normal );
	}
	return best < 1.0f;
}

/*
=============
PM_CheckAirJump

Wall jump when next to a wall, otherwise a single double jump per air time
=============
*/
static void PM_CheckAirJump( void ) {
	vec3_t	normal;
	float	into;

	if ( pm->ps->pm_type != PM_NORMAL || pm->waterlevel > 1 ) {
		return;
	}
#ifndef MISSIONPACK
	if ( pm->ps->powerups[PW_JETPACK] ) {
		return;		// holding jump flies instead
	}
#endif
	if ( pm->ps->pm_flags & ( PMF_RESPAWNED | PMF_JUMP_HELD | PMF_TIME_WATERJUMP ) ) {
		return;
	}
	if ( pm->cmd.upmove < 10 ) {
		return;
	}
	if ( RM_FLAGS2( pm->ps ) & RMS2_SLAMMING ) {
		return;		// a ground pound is committed (a dash cancels it)
	}

	if ( RMF( RMF_JUMPASSIST ) ) {
		trace_t	trace;
		vec3_t	end;

		// walked off a ledge a moment ago: still a normal ground jump
		if ( pm->ps->stats[STAT_JUMPTIMER] > 0 ) {
			pm->ps->stats[STAT_JUMPTIMER] = 0;
			pm->ps->pm_flags |= PMF_JUMP_HELD;
			if ( pm->ps->velocity[2] < JUMP_VELOCITY ) {
				pm->ps->velocity[2] = JUMP_VELOCITY;
			}
			PM_AddEvent( EV_JUMP );
			PM_ForceLegsAnim( pm->cmd.forwardmove >= 0 ? LEGS_JUMP : LEGS_JUMPB );
			return;
		}
		// about to land: keep the press for the ground jump instead of burning the air jump
		if ( pm->ps->velocity[2] < 0 ) {
			VectorCopy( pm->ps->origin, end );
			end[2] += pm->ps->velocity[2] * JUMP_BUFFER_TIME * 0.001f - 2;
			pm->trace( &trace, pm->ps->origin, pm->mins, pm->maxs, end, pm->ps->clientNum, pm->tracemask );
			if ( trace.fraction < 1.0f && trace.plane.normal[2] >= MIN_WALK_NORMAL ) {
				return;
			}
		}
	}

	if ( RMF( RMF_WALLJUMP ) && pm->ps->stats[STAT_WALLJUMP_TIME] <= 0 && PM_FindWall( normal ) ) {
		pm->ps->pm_flags |= PMF_JUMP_HELD;
		// drop the velocity into the wall, then kick away from it
		into = DotProduct( pm->ps->velocity, normal );
		if ( into < 0 ) {
			VectorMA( pm->ps->velocity, -into, normal, pm->ps->velocity );
		}
		VectorMA( pm->ps->velocity, WALL_JUMP_PUSH, normal, pm->ps->velocity );
		pm->ps->velocity[2] = WALL_JUMP_VELOCITY;
		pm->ps->stats[STAT_WALLJUMP_TIME] = WALL_JUMP_COOLDOWN;
		// really leave the wall: the run's pull must not eat the kick
		pm->ps->stats[STAT_RMFLAGS] &= ~( RMS_WALLRUN | RMS_WALLRUN_LEFT );
		if ( pm->debugLevel > 1 ) {
			Com_Printf( "%i:walljump: vel %.0f %.0f %.0f normal %.2f %.2f\n", c_pmove, pm->ps->velocity[0],
				pm->ps->velocity[1], pm->ps->velocity[2], normal[0], normal[1] );
		}
		PM_AddEvent( EV_WALL_JUMP );
		PM_ForceLegsAnim( LEGS_JUMP );
		return;
	}

	if ( !RMF( RMF_DOUBLEJUMP ) ) {
		return;
	}
	if ( pm->ps->pm_flags & PMF_DOUBLE_JUMPED ) {
		// Featherweight: one more air jump
		if ( !( RM_FLAGS2( pm->ps ) & RMP2_FEATHER ) || ( pm->ps->pm_flags & PMF_EXTRA_JUMPED ) ) {
			return;		// the press stays free, so holding it jumps again on landing
		}
		pm->ps->pm_flags |= PMF_EXTRA_JUMPED | PMF_JUMP_HELD;
		if ( pm->ps->velocity[2] < 0 ) {
			pm->ps->velocity[2] = 0;
		}
		pm->ps->velocity[2] = pm->ps->velocity[2] * 0.5f + FEATHER_JUMP_VELOCITY;
		PM_AddEvent( EV_DOUBLE_JUMP );
		PM_ForceLegsAnim( pm->cmd.forwardmove >= 0 ? LEGS_JUMP : LEGS_JUMPB );
		if ( pm->debugLevel > 1 ) {
			Com_Printf( "%i:feather jump vz %.0f\n", c_pmove, pm->ps->velocity[2] );
		}
		return;
	}
	pm->ps->pm_flags |= PMF_DOUBLE_JUMPED | PMF_JUMP_HELD;
	if ( pm->ps->velocity[2] < 0 ) {
		pm->ps->velocity[2] = 0;
	}
	pm->ps->velocity[2] = pm->ps->velocity[2] * 0.5f + DOUBLE_JUMP_VELOCITY;
	PM_AddEvent( EV_DOUBLE_JUMP );
	if ( pm->cmd.forwardmove >= 0 ) {
		PM_ForceLegsAnim( LEGS_JUMP );
		pm->ps->pm_flags &= ~PMF_BACKWARDS_JUMP;
	} else {
		PM_ForceLegsAnim( LEGS_JUMPB );
		pm->ps->pm_flags |= PMF_BACKWARDS_JUMP;
	}
}

/*
=============
PM_Blink

Blink perk: move up to BLINK_DIST along dir, never into anything solid (players included).
A blocked blink tries again a step higher, so stairs don't stop it. Returns the distance.
=============
*/
static int PM_Blink( const vec3_t dir ) {
	trace_t		trace, t2;
	vec3_t		start, end, best;
	float		bestFrac, dist;

	VectorMA( pm->ps->origin, BLINK_DIST, dir, end );
	pm->trace( &trace, pm->ps->origin, pm->mins, pm->maxs, end, pm->ps->clientNum, pm->tracemask );
	if ( trace.allsolid || trace.startsolid ) {
		return 0;
	}
	bestFrac = trace.fraction;
	VectorCopy( trace.endpos, best );

	if ( trace.fraction < 1.0f ) {
		VectorCopy( pm->ps->origin, end );
		end[2] += STEPSIZE;
		pm->trace( &t2, pm->ps->origin, pm->mins, pm->maxs, end, pm->ps->clientNum, pm->tracemask );
		if ( !t2.allsolid ) {
			VectorCopy( t2.endpos, start );
			VectorMA( start, BLINK_DIST, dir, end );
			pm->trace( &t2, start, pm->mins, pm->maxs, end, pm->ps->clientNum, pm->tracemask );
			if ( !t2.allsolid && t2.fraction > bestFrac + 0.05f ) {
				// settle back down onto the step
				VectorCopy( t2.endpos, start );
				VectorCopy( start, end );
				end[2] -= STEPSIZE;
				bestFrac = t2.fraction;
				pm->trace( &t2, start, pm->mins, pm->maxs, end, pm->ps->clientNum, pm->tracemask );
				if ( t2.allsolid ) {
					VectorCopy( start, best );
				} else {
					VectorCopy( t2.endpos, best );
				}
			}
		}
	}
	dist = sqrt( ( best[0] - pm->ps->origin[0] ) * ( best[0] - pm->ps->origin[0] )
		+ ( best[1] - pm->ps->origin[1] ) * ( best[1] - pm->ps->origin[1] ) );
	if ( pm->debugLevel > 1 ) {
		Com_Printf( "%i:blink %.0f units (frac %.2f)\n", c_pmove, dist, bestFrac );
	}
	VectorCopy( best, pm->ps->origin );
	if ( dist > 255 ) {
		dist = 255;
	}
	return (int)dist;
}

/*
=============
PM_CheckDash

Burst of horizontal speed in the movement direction (or view direction),
on the ground or once per air time
=============
*/
static void PM_CheckDash( void ) {
	vec3_t	angles, forward, right, dir;
	float	speed;

	if ( !( pm->cmd.buttons & BUTTON_DASH ) ) {
		pm->ps->pm_flags &= ~PMF_DASH_HELD;
		return;
	}
	if ( pm->debugLevel > 1 ) {
		Com_Printf( "%i:dash: flags %x ready %i rmf %x\n", c_pmove, pm->ps->pm_flags, DASH_READY( pm->ps ), pm->ps->stats[STAT_RMFLAGS] );
	}
	if ( pm->ps->pm_flags & ( PMF_DASH_HELD | PMF_RESPAWNED ) ) {
		return;
	}
	pm->ps->pm_flags |= PMF_DASH_HELD;

	if ( !RMF( RMF_DASH ) || pm->ps->pm_type != PM_NORMAL || pm->waterlevel > 1 || !DASH_READY( pm->ps ) ) {
		return;
	}
	if ( pm->ps->loopSound & RMZ_ZIPPING ) {
		// a dash lets go of the zip line (no refresh: the dash is the air move)
		pm->ps->loopSound &= ~RMZ_ZIPPING;
		PM_AddEvent( EV_ZIP_END );
	}

	VectorSet( angles, 0, pm->ps->viewangles[YAW], 0 );
	AngleVectors( angles, forward, right, NULL );
	VectorScale( forward, pm->cmd.forwardmove, dir );
	VectorMA( dir, pm->cmd.rightmove, right, dir );
	if ( VectorNormalize( dir ) == 0 ) {
		VectorCopy( forward, dir );
	}

	// a dash cancels a ground pound
	RM_FLAGS2( pm->ps ) &= ~RMS2_SLAMMING;
	pm->ps->stats[STAT_RMFLAGS] &= ~( RMS_WALLRUN | RMS_WALLRUN_LEFT );
	pm->ps->stats[STAT_DASH_TIME] += DASH_COOLDOWN;

	if ( RM_FLAGS2( pm->ps ) & RMP2_BLINK ) {
		// Blink: a short teleport that keeps the velocity.
		// eventParm: high 4 bits = direction (16ths of a turn), low 4 bits = distance / 15
		int		dist, dirIdx;

		dist = PM_Blink( dir ) / 15;
		if ( dist > 15 ) {
			dist = 15;
		}
		dirIdx = (int)( ( RAD2DEG( atan2( dir[1], dir[0] ) ) + 360.0f + 11.25f ) / 22.5f ) & 15;
		BG_AddPredictableEventToPlayerstate( EV_BLINK, ( dirIdx << 4 ) | dist, pm->ps );
		return;
	}

	// never slower than the current horizontal speed
	speed = sqrt( pm->ps->velocity[0] * pm->ps->velocity[0] + pm->ps->velocity[1] * pm->ps->velocity[1] );
	if ( speed < DASH_SPEED ) {
		speed = DASH_SPEED;
	}
	pm->ps->velocity[0] = dir[0] * speed;
	pm->ps->velocity[1] = dir[1] * speed;
	if ( pm->ps->velocity[2] < DASH_LIFT ) {
		pm->ps->velocity[2] = DASH_LIFT;
	}

	if ( pml.walking ) {
		// hop off the ground so friction doesn't eat the dash
		pml.walking = qfalse;
		pml.groundPlane = qfalse;
		pm->ps->groundEntityNum = ENTITYNUM_NONE;
		RM_FLAGS2( pm->ps ) &= ~RMS2_SLIDING;
	}
	PM_AddEvent( EV_DASH );
}

/*
=============
PM_CheckWallRun

Holding forward at speed along a wall runs on it for a while:
most of gravity is cancelled and the speed is kept. Jumping kicks off (wall jump).
=============
*/
static void PM_CheckWallRun( void ) {
	vec3_t		flat, side, end, normal;
	trace_t		trace;
	float		speed, into, sign;
	int			i, wasLeft;
	qboolean	was, found;

	was = ( pm->ps->stats[STAT_RMFLAGS] & RMS_WALLRUN ) != 0;
	wasLeft = pm->ps->stats[STAT_RMFLAGS] & RMS_WALLRUN_LEFT;
	pm->ps->stats[STAT_RMFLAGS] &= ~( RMS_WALLRUN | RMS_WALLRUN_LEFT );

	if ( !RMF( RMF_WALLRUN ) || pml.walking || pm->waterlevel > 1 || pm->ps->pm_type != PM_NORMAL
		|| pm->cmd.forwardmove <= 0 || ( pm->ps->pm_flags & ( PMF_GRAPPLE_PULL | PMF_TIME_WATERJUMP ) )
		|| pm->ps->powerups[PW_FLIGHT] || pm->ps->stats[STAT_WALLRUN_TIME] >= WALLRUN_TIME
		|| ( RM_FLAGS2( pm->ps ) & RMS2_SLAMMING ) || ( pm->ps->loopSound & RMZ_ZIPPING ) ) {
		return;
	}
	if ( !was && pm->ps->stats[STAT_WALLJUMP_TIME] > 0 ) {
		return;		// just kicked off a wall: don't get caught by it again
	}
#ifndef MISSIONPACK
	if ( pm->ps->powerups[PW_JETPACK] ) {
		return;
	}
#endif
	flat[0] = pm->ps->velocity[0];
	flat[1] = pm->ps->velocity[1];
	flat[2] = 0;
	speed = VectorNormalize( flat );
	if ( speed < WALLRUN_MIN_SPEED || ( !was && pm->ps->velocity[2] < -250 ) ) {
		if ( pm->debugLevel > 1 ) {
			Com_Printf( "%i:wallrun: speed %.0f vz %.0f\n", c_pmove, speed, pm->ps->velocity[2] );
		}
		return;
	}

	// a wall beside the direction of travel, the side we were running on first
	side[0] = flat[1];
	side[1] = -flat[0];
	side[2] = 0;
	found = qfalse;
	sign = 1;
	for ( i = 0 ; i < 2 && !found ; i++ ) {
		sign = ( ( i == 0 ) == ( wasLeft != 0 ) ) ? -1 : 1;
		VectorMA( pm->ps->origin, sign * WALLRUN_DIST, side, end );
		pm->trace( &trace, pm->ps->origin, pm->mins, pm->maxs, end, pm->ps->clientNum, pm->tracemask );
		if ( trace.fraction < 1.0f && !trace.allsolid && fabs( trace.plane.normal[2] ) < 0.3f
			&& trace.entityNum >= MAX_CLIENTS && !( trace.surfaceFlags & SURF_SKY ) ) {
			VectorCopy( trace.plane.normal, normal );
			found = qtrue;
		}
	}
	// only on purpose: a new run needs jump held and a path along the wall, not into it
	if ( found && !was && ( pm->cmd.upmove < 10 || !( pm->ps->pm_flags & PMF_JUMP_HELD )
		|| fabs( DotProduct( flat, normal ) ) > 0.6f ) ) {
		return;
	}
	if ( !found ) {
		if ( pm->debugLevel > 1 ) {
			Com_Printf( "%i:wallrun: no wall (frac %.2f n %.2f ent %i)\n", c_pmove, trace.fraction, trace.plane.normal[2], trace.entityNum );
		}
		return;
	}

	// steering clearly away from the wall lets go of it
	{
		vec3_t	wish;
		float	len;

		wish[0] = pml.forward[0] * pm->cmd.forwardmove + pml.right[0] * pm->cmd.rightmove;
		wish[1] = pml.forward[1] * pm->cmd.forwardmove + pml.right[1] * pm->cmd.rightmove;
		wish[2] = 0;
		len = VectorNormalize( wish );
		if ( len > 0 && DotProduct( wish, normal ) > 0.85f ) {
			if ( pm->debugLevel > 1 ) {
				Com_Printf( "%i:wallrun: steered away\n", c_pmove );
			}
			return;
		}
	}

	if ( !was ) {
		// catching a wall gives the air jumps (and the zip line) back
		pm->ps->pm_flags &= ~( PMF_DOUBLE_JUMPED | PMF_EXTRA_JUMPED );
		pm->ps->loopSound &= ~RMZ_USED;
		if ( pm->ps->velocity[2] < 0 ) {
			pm->ps->velocity[2] *= 0.3f;
		} else if ( pm->ps->velocity[2] > 180 ) {
			pm->ps->velocity[2] = 180;	// a small rise, not a climb up the wall
		}
		PM_AddEvent( EV_WALLRUN );
	}
	// run along the wall, drifting onto it at a fixed speed so the run holds
	// (setting it, not adding it every frame, keeps the run framerate independent)
	into = DotProduct( pm->ps->velocity, normal );
	VectorMA( pm->ps->velocity, -( into + WALLRUN_STICK ), normal, pm->ps->velocity );
	// cancel most of gravity (the air move applies all of it); less of it while still rising
	pm->ps->velocity[2] += pm->ps->gravity * pml.frametime
		* ( 1.0f - ( pm->ps->velocity[2] > 0 ? 0.55f : WALLRUN_GRAVITY ) );
	if ( pm->ps->velocity[2] < -WALLRUN_MAX_FALL ) {
		pm->ps->velocity[2] = -WALLRUN_MAX_FALL;
	}
	pm->ps->stats[STAT_WALLRUN_TIME] += pml.msec;
	pm->ps->stats[STAT_RMFLAGS] |= RMS_WALLRUN | ( sign < 0 ? RMS_WALLRUN_LEFT : 0 );
	PM_ContinueLegsAnim( LEGS_RUN );
}

/*
=============
PM_CrouchMoves

Slide (crouch while running on the ground) and ground pound (a fresh crouch press in the air).
All the state lives in RM_FLAGS2 (RMS2_ bits) and ammo[AMMO_RMSTATE2] (slide msec left,
or msec since the ground pound started), so client prediction runs exactly the same.
=============
*/
#define RMF2(x)	( RM_FLAGS2( pm->ps ) & (x) )

static void PM_CrouchMoves( void ) {
	int			*flags = &RM_FLAGS2( pm->ps );
	int			*timer = &pm->ps->ammo[AMMO_RMSTATE2];
	qboolean	crouch, pressed;
	float		speed, boost, f;
	trace_t		trace;
	vec3_t		end;

	crouch = pm->cmd.upmove < 0;
	pressed = crouch && !( *flags & RMS2_CROUCH_LAST );
	if ( crouch ) {
		*flags |= RMS2_CROUCH_LAST;
	} else {
		*flags &= ~( RMS2_CROUCH_LAST | RMS2_CROUCH_HELD );
	}

	if ( pm->ps->pm_type != PM_NORMAL || pm->waterlevel > 1 || pm->ps->powerups[PW_FLIGHT]
		|| ( pm->ps->pm_flags & ( PMF_GRAPPLE_PULL | PMF_TIME_WATERJUMP ) ) || ( pm->ps->loopSound & RMZ_ZIPPING ) ) {
		*flags &= ~( RMS2_SLIDING | RMS2_SLAMMING );
		*timer = 0;
		return;
	}

	//
	// ground pound in progress
	//
	if ( *flags & RMS2_SLAMMING ) {
		if ( !RMF2( RMF2_SLAM ) || pml.walking
			|| ( *timer >= SLAM_HANG_TIME && pm->ps->velocity[2] > 0 ) ) {
			// switched off, landed without the crash (steep slope), or thrown up by a jump pad / blast
			*flags &= ~RMS2_SLAMMING;
			*timer = 0;
			return;
		}
		if ( *timer < SLAM_HANG_TIME ) {
			// a short hang at the top: most of the horizontal speed bleeds off
			f = 1.0f - ( 1.0f - SLAM_KEEP_SPEED ) * pml.msec / (float)SLAM_HANG_TIME;
			if ( f < 0 ) {
				f = 0;
			}
			pm->ps->velocity[0] *= f;
			pm->ps->velocity[1] *= f;
			pm->ps->velocity[2] = 0;
		} else {
			pm->ps->velocity[2] = -SLAM_SPEED;
		}
		*timer += pml.msec;
		if ( *timer > 30000 ) {
			*timer = 30000;
		}
		return;
	}

	//
	// start a ground pound: a fresh crouch press with enough air below
	//
	if ( pressed && RMF2( RMF2_SLAM ) && !pml.walking && !( pm->ps->pm_flags & PMF_RESPAWNED ) ) {
		VectorCopy( pm->ps->origin, end );
		end[2] -= SLAM_MIN_HEIGHT;
		pm->trace( &trace, pm->ps->origin, pm->mins, pm->maxs, end, pm->ps->clientNum, pm->tracemask );
		if ( pm->debugLevel > 1 ) {
			Com_Printf( "%i:slam? frac %.2f\n", c_pmove, trace.fraction );
		}
		if ( trace.fraction == 1.0f && !trace.allsolid ) {
			*flags = ( *flags & ~RMS2_SLIDING ) | RMS2_SLAMMING | RMS2_CROUCH_HELD;
			*timer = 0;
			pm->ps->stats[STAT_RMFLAGS] &= ~( RMS_WALLRUN | RMS_WALLRUN_LEFT );
			pm->ps->velocity[2] = 0;
			PM_AddEvent( EV_SLAM );
			PM_ForceLegsAnim( LEGS_JUMP );
			return;
		}
	}

	//
	// slide in progress
	//
	if ( *flags & RMS2_SLIDING ) {
		*timer -= pml.msec;
		if ( !crouch && *timer > SLIDE_RELEASE_GRACE ) {
			*timer = SLIDE_RELEASE_GRACE;	// let go: stand up soon, but there's time to slide-jump
		}
		speed = sqrt( pm->ps->velocity[0] * pm->ps->velocity[0] + pm->ps->velocity[1] * pm->ps->velocity[1] );
		if ( pm->debugLevel > 2 ) {
			Com_Printf( "%i:sliding: left %i speed %.0f walking %i n.z %.2f\n", c_pmove, *timer, speed,
				pml.walking, pml.groundTrace.plane.normal[2] );
		}
		if ( !RMF2( RMF2_SLIDE ) || *timer <= 0
			|| ( pml.walking && speed < SLIDE_END_SPEED && !RMF2( RMP2_SLIPSTREAM ) ) ) {
			if ( pm->debugLevel > 1 ) {
				Com_Printf( "%i:slide end: left %i speed %.0f\n", c_pmove, *timer, speed );
			}
			*flags &= ~RMS2_SLIDING;
			*timer = 0;
		}
		return;
	}

	//
	// start a slide: crouch on the ground at running speed
	//
	if ( crouch && !( *flags & RMS2_CROUCH_HELD ) && pml.walking && RMF2( RMF2_SLIDE )
		&& !( pm->ps->pm_flags & PMF_RESPAWNED ) ) {
		speed = sqrt( pm->ps->velocity[0] * pm->ps->velocity[0] + pm->ps->velocity[1] * pm->ps->velocity[1] );
		if ( speed >= SLIDE_MIN_SPEED ) {
			boost = SLIDE_BOOST;
			if ( speed + boost > SLIDE_BOOST_CAP ) {
				boost = SLIDE_BOOST_CAP - speed;
			}
			if ( boost > 0 ) {
				pm->ps->velocity[0] *= ( speed + boost ) / speed;
				pm->ps->velocity[1] *= ( speed + boost ) / speed;
			}
			*flags |= RMS2_SLIDING | RMS2_CROUCH_HELD;
			*timer = RMF2( RMP2_SLIPSTREAM ) ? SLIDE_TIME * 2 : SLIDE_TIME;
			PM_AddEvent( EV_SLIDE );
			if ( pm->debugLevel > 1 ) {
				Com_Printf( "%i:slide start %.0f -> %.0f\n", c_pmove, speed, speed + ( boost > 0 ? boost : 0 ) );
			}
		}
	}
}

/*
=============
Zip line (RMF2_ZIPLINE, BUTTON_ZIP)

A press fires a hitscan cable along the view (ZIP_RANGE) that holds only on world
surfaces (not the sky, players or movers). Attached, you ride a straight line to the
anchor (ps->grapplePoint) with gravity off, free to look and shoot; jump or the zip button
lets go keeping the momentum, arriving drops you off with a hop, an obstacle knocks you off.
One zip per air time. All the state is in ps->loopSound (RMZ_ bits), so it predicts.
=============
*/
#define RMZ(x)	( pm->ps->loopSound & (x) )

static void PM_AirMove( void );

static void PM_ZipEnd( qboolean refresh ) {
	pm->ps->loopSound &= ~RMZ_ZIPPING;
	if ( refresh ) {
		// letting go gives the air jumps back (like a grapple pull)
		pm->ps->pm_flags &= ~( PMF_DOUBLE_JUMPED | PMF_EXTRA_JUMPED );
		pm->ps->stats[STAT_WALLRUN_TIME] = 0;
	}
	PM_AddEvent( EV_ZIP_END );
}

/*
=============
BG_ZipAim

Where a zip press from the eye along forward would hold (pmove, the cgame's anchor
indicator). The cable flies past players, bodies and props (a human aims at the enemy,
the cable holds on the wall behind them); the world and movers hold it, the sky doesn't.
Box entities trace as CONTENTS_BODY on the server and in the cgame alike.
=============
*/
int BG_ZipAim( void (*trace)( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs,
		const vec3_t end, int passEntityNum, int contentMask ), const vec3_t eye, const vec3_t forward,
		int passEntityNum, vec3_t anchor, float *dist ) {
	trace_t	tr;
	vec3_t	start, end;
	float	d;
	int		i;

	VectorCopy( eye, start );
	VectorMA( eye, ZIP_RANGE, forward, end );
	for ( i = 0 ; i < 4 ; i++ ) {
		trace( &tr, start, vec3_origin, vec3_origin, end, passEntityNum, MASK_SHOT );
		if ( tr.startsolid && i == 0 ) {
			return ZIPR_CLOSE;		// the eye is in the wall
		}
		if ( tr.fraction >= 1.0f || tr.entityNum == ENTITYNUM_NONE ) {
			return ZIPR_NOTHING;
		}
		if ( tr.entityNum == ENTITYNUM_WORLD || !( tr.contents & ( CONTENTS_BODY | CONTENTS_CORPSE ) ) ) {
			break;
		}
		passEntityNum = tr.entityNum;
		VectorCopy( tr.endpos, start );
	}
	if ( i == 4 || ( tr.surfaceFlags & ( SURF_SKY | SURF_NOIMPACT ) ) ) {
		return ZIPR_NOTHING;
	}
	d = Distance( eye, tr.endpos );
	if ( d < ZIP_MIN_DIST ) {
		return ZIPR_CLOSE;
	}
	VectorCopy( tr.endpos, anchor );
	*dist = d;
	return ZIPR_OK;
}

static void PM_ZipMiss( int reason ) {
	if ( pm->debugLevel > 1 ) {
		Com_Printf( "%i:zip miss: reason %i\n", c_pmove, reason );
	}
	BG_AddPredictableEventToPlayerstate( EV_ZIP_MISS, reason, pm->ps );
}

static void PM_ZipFire( void ) {
	vec3_t		start, anchor, dir;
	float		dist, speed;
	int			reason;

	VectorCopy( pm->ps->origin, start );
	start[2] += pm->ps->viewheight;
	reason = BG_ZipAim( pm->trace, start, pml.forward, pm->ps->clientNum, anchor, &dist );
	if ( reason != ZIPR_OK ) {
		PM_ZipMiss( reason );
		return;
	}

	VectorCopy( anchor, pm->ps->grapplePoint );
	pm->ps->loopSound |= RMZ_ZIPPING | RMZ_USED;
	// a zip cancels the grapple, a ground pound, a slide and a wall run
	pm->ps->pm_flags &= ~PMF_GRAPPLE_PULL;
	RM_FLAGS2( pm->ps ) &= ~( RMS2_SLIDING | RMS2_SLAMMING );
	pm->ps->ammo[AMMO_RMSTATE2] = 0;
	pm->ps->stats[STAT_RMFLAGS] &= ~( RMS_WALLRUN | RMS_WALLRUN_LEFT );
	pm->ps->groundEntityNum = ENTITYNUM_NONE;
	pml.walking = qfalse;
	pml.groundPlane = qfalse;

	// start moving along the line right away
	VectorSubtract( pm->ps->grapplePoint, pm->ps->origin, dir );
	VectorNormalize( dir );
	speed = DotProduct( pm->ps->velocity, dir );
	if ( speed < ZIP_START_SPEED ) {
		speed = ZIP_START_SPEED;
	}
	VectorScale( dir, speed, pm->ps->velocity );

	if ( pm->debugLevel > 1 ) {
		Com_Printf( "%i:zip attach: dist %.0f anchor %.0f %.0f %.0f speed %.0f\n", c_pmove, dist,
			anchor[0], anchor[1], anchor[2], speed );
	}
	BG_AddPredictableEventToPlayerstate( EV_ZIP_FIRE, (int)( dist / 16 ), pm->ps );
}

/*
=============
PM_CheckZipLine

The button and the start / let go rules. Returns qtrue while riding.
=============
*/
static qboolean PM_CheckZipLine( void ) {
	qboolean	down, pressed;

	down = ( pm->cmd.buttons & BUTTON_ZIP ) != 0;
	pressed = down && !RMZ( RMZ_HELD );
	if ( down ) {
		pm->ps->loopSound |= RMZ_HELD;
	} else {
		pm->ps->loopSound &= ~RMZ_HELD;
	}

	if ( RMZ( RMZ_ZIPPING ) ) {
		// forced off: switched off, dead, swimming, flying, a grapple pull took over
		if ( !RMF2( RMF2_ZIPLINE ) || pm->ps->pm_type != PM_NORMAL || pm->waterlevel > 1
			|| pm->ps->powerups[PW_FLIGHT] || ( pm->ps->pm_flags & ( PMF_GRAPPLE_PULL | PMF_TIME_WATERJUMP ) ) ) {
			if ( pm->debugLevel > 1 ) {
				Com_Printf( "%i:zip forced off\n", c_pmove );
			}
			PM_ZipEnd( qfalse );
			return qfalse;
		}
		// let go: a fresh jump or zip press keeps the momentum, plus a small kick up
		if ( pressed || ( pm->cmd.upmove >= 10 && !( pm->ps->pm_flags & ( PMF_JUMP_HELD | PMF_RESPAWNED ) ) ) ) {
			if ( !pressed ) {
				pm->ps->pm_flags |= PMF_JUMP_HELD;	// this press doesn't also double jump
			}
			if ( pm->ps->velocity[2] < 0 ) {
				pm->ps->velocity[2] *= 0.5f;
			}
			pm->ps->velocity[2] += ZIP_RELEASE_KICK;
			if ( pm->debugLevel > 1 ) {
				Com_Printf( "%i:zip let go: vel %.0f %.0f %.0f\n", c_pmove, pm->ps->velocity[0], pm->ps->velocity[1], pm->ps->velocity[2] );
			}
			PM_ZipEnd( qtrue );
			PM_ForceLegsAnim( LEGS_JUMP );
			return qfalse;
		}
		return qtrue;
	}

	if ( !pressed || pm->ps->pm_type != PM_NORMAL || pm->ps->stats[STAT_HEALTH] <= 0 ) {
		return qfalse;
	}
	if ( !RMF2( RMF2_ZIPLINE ) ) {
		PM_ZipMiss( ZIPR_LOCKED );		// say so: switched off, or the descent card isn't owned
		return qfalse;
	}
	if ( pm->waterlevel > 1 || pm->ps->powerups[PW_FLIGHT] || ( pm->ps->pm_flags & PMF_RESPAWNED ) ) {
		return qfalse;
	}
	if ( RMZ( RMZ_USED ) ) {
		// one zip per air time: land, swim or grapple to reload it
		PM_ZipMiss( ZIPR_USED );
		return qfalse;
	}
	PM_ZipFire();
	return RMZ( RMZ_ZIPPING ) != 0;
}

/*
=============
PM_ZipMove

Riding: a straight line to the anchor, accelerating to ZIP_SPEED, no gravity
=============
*/
#define ZIP_STEP	52		// a lip the body catches on is stepped over up to this high (about eye height)

/*
=============
PM_ZipStepOver

The eye line to the anchor is clear but the body caught on something lower down: from
where we were this frame, go up (at most ZIP_STEP), forward, and settle back down onto it.
Fails on anything taller, so it never climbs a wall.
=============
*/
static qboolean PM_ZipStepOver( const vec3_t before, const vec3_t dir, float move ) {
	trace_t		tr;
	vec3_t		flat, up, fwd, down;
	float		len;

	flat[0] = dir[0];
	flat[1] = dir[1];
	flat[2] = 0;
	len = VectorNormalize( flat );
	if ( len < 0.3f || move < 1 ) {
		return qfalse;		// a steep line: nothing to step over
	}
	VectorCopy( before, up );
	up[2] += ZIP_STEP;
	pm->trace( &tr, before, pm->mins, pm->maxs, up, pm->ps->clientNum, pm->tracemask );
	if ( tr.allsolid || tr.endpos[2] - before[2] < 4 ) {
		return qfalse;
	}
	VectorCopy( tr.endpos, up );
	VectorMA( up, move * len, flat, fwd );
	pm->trace( &tr, up, pm->mins, pm->maxs, fwd, pm->ps->clientNum, pm->tracemask );
	if ( tr.allsolid || tr.fraction < 0.3f ) {
		return qfalse;
	}
	// back down towards the line (no lower than where we were): onto the top of what we passed
	VectorCopy( tr.endpos, fwd );
	VectorCopy( fwd, down );
	down[2] = before[2] + dir[2] * move;
	if ( down[2] < fwd[2] ) {
		pm->trace( &tr, fwd, pm->mins, pm->maxs, down, pm->ps->clientNum, pm->tracemask );
		if ( !tr.allsolid ) {
			VectorCopy( tr.endpos, fwd );
		}
	}
	VectorCopy( fwd, pm->ps->origin );
	return qtrue;
}

static void PM_ZipMove( void ) {
	vec3_t		dir, before, moved;
	float		dist, speed, expect, along;

	VectorSubtract( pm->ps->grapplePoint, pm->ps->origin, dir );
	dist = VectorNormalize( dir );

	// the cable needs a clear line (a teleporter, or a corner we swung past, cuts it). From the
	// eye like the aim: from the feet, a lip the eye sees over cut the line at once
	if ( dist > ZIP_ARRIVE_DIST ) {
		trace_t	los;
		vec3_t	eye;

		VectorCopy( pm->ps->origin, eye );
		eye[2] += pm->ps->viewheight;
		pm->trace( &los, eye, vec3_origin, vec3_origin, pm->ps->grapplePoint, pm->ps->clientNum, MASK_SOLID );
		// (near the anchor the line grazes the ledge we're riding up to: the body handles that)
		if ( los.fraction < 1.0f && Distance( los.endpos, pm->ps->grapplePoint ) > 256 ) {
			if ( pm->debugLevel > 1 ) {
				Com_Printf( "%i:zip line cut: %.0f from the anchor\n", c_pmove, Distance( los.endpos, pm->ps->grapplePoint ) );
			}
			PM_ZipEnd( qtrue );
			PM_AirMove();
			return;
		}
	}

	speed = DotProduct( pm->ps->velocity, dir );
	if ( speed < ZIP_START_SPEED ) {
		speed = ZIP_START_SPEED;
	}
	if ( speed < ZIP_SPEED ) {
		speed += ZIP_ACCEL * pml.frametime;
		if ( speed > ZIP_SPEED ) {
			speed = ZIP_SPEED;
		}
	}

	if ( dist > ZIP_ARRIVE_DIST ) {
		VectorScale( dir, speed, pm->ps->velocity );
		pml.groundPlane = qfalse;
		pml.walking = qfalse;
		pm->ps->groundEntityNum = ENTITYNUM_NONE;
		VectorCopy( pm->ps->origin, before );
		PM_StepSlideMove( qfalse );
		// riding into the floor at speed is not a fall
		VectorCopy( pm->ps->velocity, pml.previous_velocity );
		pml.previous_velocity[2] = 0;
		pml.previous_origin[2] = pm->ps->origin[2];

		VectorSubtract( pm->ps->origin, before, moved );
		along = DotProduct( moved, dir );
		expect = speed * pml.frametime;
		VectorSubtract( pm->ps->grapplePoint, pm->ps->origin, moved );
		dist = VectorLength( moved );
		if ( dist > ZIP_ARRIVE_DIST && dist < ZIP_ARRIVE_DIST * 2 && along < expect * 0.5f ) {
			dist = ZIP_ARRIVE_DIST;		// pressed against the anchor's own surface: that's arriving
		} else if ( dist > ZIP_ARRIVE_DIST && along < expect * 0.3f ) {
			// the body caught on something the eye line clears (a ledge lip, a railing, a low
			// wall, a player): step over it instead of falling off
			if ( PM_ZipStepOver( before, dir, expect ) ) {
				VectorScale( dir, speed, pm->ps->velocity );
				if ( pm->debugLevel > 1 ) {
					Com_Printf( "%i:zip step over: up %.0f at %.0f %.0f %.0f\n", c_pmove, pm->ps->origin[2] - before[2],
						pm->ps->origin[0], pm->ps->origin[1], pm->ps->origin[2] );
				}
				PM_ContinueLegsAnim( LEGS_JUMP );
				return;
			}
		}
		if ( dist > ZIP_ARRIVE_DIST && along < expect * 0.3f && dist < ZIP_ARRIVE_DIST * 4 ) {
			dist = ZIP_ARRIVE_DIST;		// stuck just short of the anchor (its ledge, its corner): drop off there
		}
		if ( dist > ZIP_ARRIVE_DIST && along < expect * 0.3f ) {
			// knocked off by something in the way
			if ( pm->debugLevel > 1 ) {
				Com_Printf( "%i:zip blocked: moved %.1f of %.1f, %.0f left at %.0f %.0f %.0f\n", c_pmove, along, expect, dist,
					pm->ps->origin[0], pm->ps->origin[1], pm->ps->origin[2] );
			}
			PM_ZipEnd( qtrue );
			return;
		}
		if ( pm->debugLevel > 2 ) {
			Com_Printf( "%i:zipping: speed %.0f left %.0f\n", c_pmove, speed, dist );
		}
	}

	if ( dist <= ZIP_ARRIVE_DIST ) {
		// arrived: drop off with a small hop, a little momentum carries onto the ledge
		pm->ps->velocity[0] = dir[0] * 280;
		pm->ps->velocity[1] = dir[1] * 280;
		pm->ps->velocity[2] = ZIP_HOP;
		if ( pm->debugLevel > 1 ) {
			Com_Printf( "%i:zip arrived\n", c_pmove );
		}
		PM_ZipEnd( qtrue );
		PM_ForceLegsAnim( LEGS_JUMP );
		return;
	}
	PM_ContinueLegsAnim( LEGS_JUMP );
}

static void PM_AirMove( void );

/*
=============
PM_SlideGroundMove

Walking while sliding: low friction (none with Slipstream), gravity pulls along slopes,
the input only turns the slide a little, and jumping keeps all the speed.
=============
*/
#define SLIDE_MAX_SPEED		1000

static void PM_SlideGroundMove( void ) {
	float	*vel = pm->ps->velocity;
	float	speed, newspeed, drop, len, diff, turn, s, c, x, y;
	float	*n;
	vec3_t	wish;

	if ( PM_CheckJump() ) {
		// slide jump: the slide's speed carries into the air
		RM_FLAGS2( pm->ps ) &= ~RMS2_SLIDING;
		pm->ps->ammo[AMMO_RMSTATE2] = 0;
		if ( pm->debugLevel > 1 ) {
			Com_Printf( "%i:slide jump %.0f\n", c_pmove, sqrt( vel[0] * vel[0] + vel[1] * vel[1] ) );
		}
		PM_AirMove();
		return;
	}

	PM_SetMovementDir();

	speed = sqrt( vel[0] * vel[0] + vel[1] * vel[1] );

	// steering: turn the velocity towards the wish direction at a limited rate (no speed gain);
	// pulling back brakes instead
	wish[0] = pml.forward[0] * pm->cmd.forwardmove + pml.right[0] * pm->cmd.rightmove;
	wish[1] = pml.forward[1] * pm->cmd.forwardmove + pml.right[1] * pm->cmd.rightmove;
	wish[2] = 0;
	len = VectorNormalize( wish );
	drop = 0;
	if ( len > 0 && speed > 1 ) {
		diff = AngleNormalize180( RAD2DEG( atan2( wish[1], wish[0] ) ) - RAD2DEG( atan2( vel[1], vel[0] ) ) );
		if ( diff > 135 || diff < -135 ) {
			drop = speed * 1.5f * pml.frametime;		// braking
		} else {
			turn = SLIDE_TURN_RATE * pml.frametime;
			if ( turn > fabs( diff ) ) {
				turn = fabs( diff );
			}
			if ( diff < 0 ) {
				turn = -turn;
			}
			s = sin( DEG2RAD( turn ) );
			c = cos( DEG2RAD( turn ) );
			x = vel[0] * c - vel[1] * s;
			y = vel[0] * s + vel[1] * c;
			vel[0] = x;
			vel[1] = y;
		}
	}

	// friction
	if ( !( RM_FLAGS2( pm->ps ) & RMP2_SLIPSTREAM ) ) {
		drop += ( speed < pm_stopspeed ? pm_stopspeed : speed ) * SLIDE_FRICTION * pml.frametime;
	}
	if ( drop > 0 && speed > 0 ) {
		newspeed = speed - drop;
		if ( newspeed < 0 ) {
			newspeed = 0;
		}
		vel[0] *= newspeed / speed;
		vel[1] *= newspeed / speed;
	}

	// gravity along the slope: downhill speeds the slide up, uphill slows it down
	n = pml.groundTrace.plane.normal;
	vel[0] += n[0] * n[2] * pm->ps->gravity * pml.frametime;
	vel[1] += n[1] * n[2] * pm->ps->gravity * pml.frametime;

	speed = sqrt( vel[0] * vel[0] + vel[1] * vel[1] );
	if ( speed > SLIDE_MAX_SPEED ) {
		vel[0] *= SLIDE_MAX_SPEED / speed;
		vel[1] *= SLIDE_MAX_SPEED / speed;
	}

	// slide along the ground plane without losing speed on slopes
	len = VectorLength( vel );
	PM_ClipVelocity( vel, n, vel, OVERCLIP );
	VectorNormalize( vel );
	VectorScale( vel, len, vel );

	if ( !vel[0] && !vel[1] ) {
		return;
	}
	PM_StepSlideMove( qfalse );
}

/*
=============
PM_CheckWaterJump
=============
*/
static qboolean	PM_CheckWaterJump( void ) {
	vec3_t	spot;
	int		cont;
	vec3_t	flatforward;

	if (pm->ps->pm_time) {
		return qfalse;
	}

	// check for water jump
	if ( pm->waterlevel != 2 ) {
		return qfalse;
	}

	flatforward[0] = pml.forward[0];
	flatforward[1] = pml.forward[1];
	flatforward[2] = 0;
	VectorNormalize (flatforward);

	VectorMA (pm->ps->origin, 30, flatforward, spot);
	spot[2] += 4;
	cont = pm->pointcontents (spot, pm->ps->clientNum );
	if ( !(cont & CONTENTS_SOLID) ) {
		return qfalse;
	}

	spot[2] += 16;
	cont = pm->pointcontents (spot, pm->ps->clientNum );
	if ( cont & (CONTENTS_SOLID|CONTENTS_PLAYERCLIP|CONTENTS_BODY) ) {
		return qfalse;
	}

	// jump out of water
	VectorScale (pml.forward, 200, pm->ps->velocity);
	pm->ps->velocity[2] = 350;

	pm->ps->pm_flags |= PMF_TIME_WATERJUMP;
	pm->ps->pm_time = 2000;

	return qtrue;
}

//============================================================================


/*
===================
PM_WaterJumpMove

Flying out of the water
===================
*/
static void PM_WaterJumpMove( void ) {
	// waterjump has no control, but falls

	PM_StepSlideMove( qtrue );

	pm->ps->velocity[2] -= pm->ps->gravity * pml.frametime;
	if (pm->ps->velocity[2] < 0) {
		// cancel as soon as we are falling down again
		pm->ps->pm_flags &= ~PMF_ALL_TIMES;
		pm->ps->pm_time = 0;
	}
}

/*
===================
PM_WaterMove

===================
*/
static void PM_WaterMove( void ) {
	int		i;
	vec3_t	wishvel;
	float	wishspeed;
	vec3_t	wishdir;
	float	scale;
	float	vel;

	if ( PM_CheckWaterJump() ) {
		PM_WaterJumpMove();
		return;
	}
#if 0
	// jump = head for surface
	if ( pm->cmd.upmove >= 10 ) {
		if (pm->ps->velocity[2] > -300) {
			if ( pm->watertype & CONTENTS_WATER ) {
				pm->ps->velocity[2] = 100;
			} else if ( pm->watertype & CONTENTS_SLIME ) {
				pm->ps->velocity[2] = 80;
			} else {
				pm->ps->velocity[2] = 50;
			}
		}
	}
#endif
	PM_Friction ();

	scale = PM_CmdScale( &pm->cmd );
	//
	// user intentions
	//
	if ( !scale ) {
		wishvel[0] = 0;
		wishvel[1] = 0;
		wishvel[2] = -60;		// sink towards bottom
	} else {
		for (i=0 ; i<3 ; i++)
			wishvel[i] = scale * pml.forward[i]*pm->cmd.forwardmove + scale * pml.right[i]*pm->cmd.rightmove;

		wishvel[2] += scale * pm->cmd.upmove;
	}

	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize(wishdir);

	if ( wishspeed > pm->ps->speed * pm_swimScale ) {
		wishspeed = pm->ps->speed * pm_swimScale;
	}

	PM_Accelerate (wishdir, wishspeed, pm_wateraccelerate);

	// make sure we can go up slopes easily under water
	if ( pml.groundPlane && DotProduct( pm->ps->velocity, pml.groundTrace.plane.normal ) < 0 ) {
		vel = VectorLength(pm->ps->velocity);
		// slide along the ground plane
		PM_ClipVelocity (pm->ps->velocity, pml.groundTrace.plane.normal,
			pm->ps->velocity, OVERCLIP );

		VectorNormalize(pm->ps->velocity);
		VectorScale(pm->ps->velocity, vel, pm->ps->velocity);
	}

	PM_SlideMove( qfalse );
}

#ifdef MISSIONPACK
/*
===================
PM_InvulnerabilityMove

Only with the invulnerability powerup
===================
*/
static void PM_InvulnerabilityMove( void ) {
	pm->cmd.forwardmove = 0;
	pm->cmd.rightmove = 0;
	pm->cmd.upmove = 0;
	VectorClear(pm->ps->velocity);
}
#endif

/*
===================
PM_FlyMove

Only with the flight powerup
===================
*/
static void PM_FlyMove( void ) {
	int		i;
	vec3_t	wishvel;
	float	wishspeed;
	vec3_t	wishdir;
	float	scale;

	// normal slowdown
	PM_Friction ();

	scale = PM_CmdScale( &pm->cmd );
	//
	// user intentions
	//
	if ( !scale ) {
		wishvel[0] = 0;
		wishvel[1] = 0;
		wishvel[2] = 0;
	} else {
		for (i=0 ; i<3 ; i++) {
			wishvel[i] = scale * pml.forward[i]*pm->cmd.forwardmove + scale * pml.right[i]*pm->cmd.rightmove;
		}

		wishvel[2] += scale * pm->cmd.upmove;
	}

	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize(wishdir);

	PM_Accelerate (wishdir, wishspeed, pm_flyaccelerate);

	PM_StepSlideMove( qfalse );
}


/*
===================
PM_AirMove

===================
*/
static void PM_AirMove( void ) {
	int			i;
	vec3_t		wishvel;
	float		fmove, smove;
	vec3_t		wishdir;
	float		wishspeed;
	float		scale;
	usercmd_t	cmd;

	PM_CheckAirJump();

	PM_Friction();

	fmove = pm->cmd.forwardmove;
	smove = pm->cmd.rightmove;

	cmd = pm->cmd;
	scale = PM_CmdScale( &cmd );

	// set the movementDir so clients can rotate the legs for strafing
	PM_SetMovementDir();

	// project moves down to flat plane
	pml.forward[2] = 0;
	pml.right[2] = 0;
	VectorNormalize (pml.forward);
	VectorNormalize (pml.right);

	for ( i = 0 ; i < 2 ; i++ ) {
		wishvel[i] = pml.forward[i]*fmove + pml.right[i]*smove;
	}
	wishvel[2] = 0;

	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize(wishdir);
	wishspeed *= scale;

	// not on ground, so little effect on velocity
	PM_Accelerate (wishdir, wishspeed, pm_airaccelerate);

	// we may have a ground plane that is very steep, even
	// though we don't have a groundentity
	// slide along the steep plane
	if ( pml.groundPlane ) {
		PM_ClipVelocity (pm->ps->velocity, pml.groundTrace.plane.normal,
			pm->ps->velocity, OVERCLIP );
	}

#if 0
	//ZOID:  If we are on the grapple, try stair-stepping
	//this allows a player to use the grapple to pull himself
	//over a ledge
	if (pm->ps->pm_flags & PMF_GRAPPLE_PULL)
		PM_StepSlideMove ( qtrue );
	else
		PM_SlideMove ( qtrue );
#endif

	PM_StepSlideMove ( qtrue );
}

/*
===================
PM_GrappleMove

===================
*/
static void PM_GrappleMove( void ) {
	vec3_t vel, v;
	float vlen;

	VectorScale(pml.forward, -16, v);
	VectorAdd(pm->ps->grapplePoint, v, v);
	VectorSubtract(v, pm->ps->origin, vel);
	vlen = VectorLength(vel);
	VectorNormalize( vel );

	if (vlen <= 100)
		VectorScale(vel, 10 * vlen, vel);
	else
		VectorScale(vel, 800, vel);

	VectorCopy(vel, pm->ps->velocity);

	pml.groundPlane = qfalse;
}

/*
===================
PM_WalkMove

===================
*/
static void PM_WalkMove( void ) {
	int			i;
	vec3_t		wishvel;
	float		fmove, smove;
	vec3_t		wishdir;
	float		wishspeed;
	float		scale;
	usercmd_t	cmd;
	float		accelerate;
	float		vel;

	if ( pm->waterlevel > 2 && DotProduct( pml.forward, pml.groundTrace.plane.normal ) > 0 ) {
		// begin swimming
		PM_WaterMove();
		return;
	}


	if ( PM_CheckJump () ) {
		// jumped away
		if ( pm->waterlevel > 1 ) {
			PM_WaterMove();
		} else {
			PM_AirMove();
		}
		return;
	}

	PM_Friction ();

	fmove = pm->cmd.forwardmove;
	smove = pm->cmd.rightmove;

	cmd = pm->cmd;
	scale = PM_CmdScale( &cmd );

	// set the movementDir so clients can rotate the legs for strafing
	PM_SetMovementDir();

	// project moves down to flat plane
	pml.forward[2] = 0;
	pml.right[2] = 0;

	// project the forward and right directions onto the ground plane
	PM_ClipVelocity (pml.forward, pml.groundTrace.plane.normal, pml.forward, OVERCLIP );
	PM_ClipVelocity (pml.right, pml.groundTrace.plane.normal, pml.right, OVERCLIP );
	//
	VectorNormalize (pml.forward);
	VectorNormalize (pml.right);

	for ( i = 0 ; i < 3 ; i++ ) {
		wishvel[i] = pml.forward[i]*fmove + pml.right[i]*smove;
	}
	// when going up or down slopes the wish velocity should Not be zero
//	wishvel[2] = 0;

	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize(wishdir);
	wishspeed *= scale;

	// clamp the speed lower if ducking
	if ( pm->ps->pm_flags & PMF_DUCKED ) {
		if ( wishspeed > pm->ps->speed * pm_duckScale ) {
			wishspeed = pm->ps->speed * pm_duckScale;
		}
	}

	// clamp the speed lower if wading or walking on the bottom
	if ( pm->waterlevel ) {
		float	waterScale;

		waterScale = pm->waterlevel / 3.0;
		waterScale = 1.0 - ( 1.0 - pm_swimScale ) * waterScale;
		if ( wishspeed > pm->ps->speed * waterScale ) {
			wishspeed = pm->ps->speed * waterScale;
		}
	}

	// when a player gets hit, they temporarily lose
	// full control, which allows them to be moved a bit
	if ( ( pml.groundTrace.surfaceFlags & SURF_SLICK ) || pm->ps->pm_flags & PMF_TIME_KNOCKBACK ) {
		accelerate = pm_airaccelerate;
	} else {
		accelerate = pm_accelerate;
	}
	if ( RM_FLAGS2( pm->ps ) & RMF2_ICE ) {
		accelerate *= ICE_ACCEL;	// ice rink: slidey but still controllable
	}

	PM_Accelerate (wishdir, wishspeed, accelerate);

	//Com_Printf("velocity = %1.1f %1.1f %1.1f\n", pm->ps->velocity[0], pm->ps->velocity[1], pm->ps->velocity[2]);
	//Com_Printf("velocity1 = %1.1f\n", VectorLength(pm->ps->velocity));

	if ( ( pml.groundTrace.surfaceFlags & SURF_SLICK ) || pm->ps->pm_flags & PMF_TIME_KNOCKBACK ) {
		pm->ps->velocity[2] -= pm->ps->gravity * pml.frametime;
	} else {
		// don't reset the z velocity for slopes
//		pm->ps->velocity[2] = 0;
	}

	vel = VectorLength(pm->ps->velocity);

	// slide along the ground plane
	PM_ClipVelocity (pm->ps->velocity, pml.groundTrace.plane.normal,
		pm->ps->velocity, OVERCLIP );

	// don't decrease velocity when going up or down a slope
	VectorNormalize(pm->ps->velocity);
	VectorScale(pm->ps->velocity, vel, pm->ps->velocity);

	// don't do anything if standing still
	if (!pm->ps->velocity[0] && !pm->ps->velocity[1]) {
		return;
	}

	PM_StepSlideMove( qfalse );

	//Com_Printf("velocity2 = %1.1f\n", VectorLength(pm->ps->velocity));

}


/*
==============
PM_DeadMove
==============
*/
static void PM_DeadMove( void ) {
	float	forward;

	if ( !pml.walking ) {
		return;
	}

	// extra friction

	forward = VectorLength (pm->ps->velocity);
	forward -= 20;
	if ( forward <= 0 ) {
		VectorClear (pm->ps->velocity);
	} else {
		VectorNormalize (pm->ps->velocity);
		VectorScale (pm->ps->velocity, forward, pm->ps->velocity);
	}
}


/*
===============
PM_NoclipMove
===============
*/
static void PM_NoclipMove( void ) {
	float	speed, drop, friction, control, newspeed;
	int			i;
	vec3_t		wishvel;
	float		fmove, smove;
	vec3_t		wishdir;
	float		wishspeed;
	float		scale;

	pm->ps->viewheight = DEFAULT_VIEWHEIGHT;

	// friction

	speed = VectorLength (pm->ps->velocity);
	if (speed < 1)
	{
		VectorCopy (vec3_origin, pm->ps->velocity);
	}
	else
	{
		drop = 0;

		friction = pm_friction*1.5;	// extra friction
		control = speed < pm_stopspeed ? pm_stopspeed : speed;
		drop += control*friction*pml.frametime;

		// scale the velocity
		newspeed = speed - drop;
		if (newspeed < 0)
			newspeed = 0;
		newspeed /= speed;

		VectorScale (pm->ps->velocity, newspeed, pm->ps->velocity);
	}

	// accelerate
	scale = PM_CmdScale( &pm->cmd );

	fmove = pm->cmd.forwardmove;
	smove = pm->cmd.rightmove;

	for (i=0 ; i<3 ; i++)
		wishvel[i] = pml.forward[i]*fmove + pml.right[i]*smove;
	wishvel[2] += pm->cmd.upmove;

	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize(wishdir);
	wishspeed *= scale;

	PM_Accelerate( wishdir, wishspeed, pm_accelerate );

	// move
	VectorMA (pm->ps->origin, pml.frametime, pm->ps->velocity, pm->ps->origin);
}

//============================================================================

/*
================
PM_FootstepForSurface

Returns an event number appropriate for the groundsurface
================
*/
static int PM_FootstepForSurface( void ) {
	if ( pml.groundTrace.surfaceFlags & SURF_NOSTEPS ) {
		return 0;
	}
	if ( pml.groundTrace.surfaceFlags & SURF_METALSTEPS ) {
		return EV_FOOTSTEP_METAL;
	}
	return EV_FOOTSTEP;
}


/*
=================
PM_CrashLand

Check for hard landings that generate sound events
=================
*/
static void PM_CrashLand( void ) {
	float		delta;
	float		dist;
	float		vel, acc;
	float		t;
	float		a, b, c, den;

	if ( RM_FLAGS2( pm->ps ) & RMS2_SLAMMING ) {
		// ground pound impact: strength from the plunge distance, never fall damage
		float	fall;
		int		strength;

		fall = ( pm->ps->ammo[AMMO_RMSTATE2] - SLAM_HANG_TIME ) * 0.001f * SLAM_SPEED;
		strength = (int)( fall * 255 / SLAM_FULL_HEIGHT );
		if ( strength < 40 ) {
			strength = 40;
		} else if ( strength > 255 ) {
			strength = 255;
		}
		RM_FLAGS2( pm->ps ) &= ~RMS2_SLAMMING;
		pm->ps->ammo[AMMO_RMSTATE2] = 0;
		if ( pm->debugLevel > 1 ) {
			Com_Printf( "%i:slam land: fall %.0f strength %i ground %i\n", c_pmove, fall, strength, pml.groundTrace.entityNum );
		}
		BG_AddPredictableEventToPlayerstate( EV_SLAM_LAND, strength, pm->ps );
		if ( pml.groundTrace.entityNum < MAX_CLIENTS ) {
			// landed on someone's head: bounce off it (the ground trace kicks us off next frame)
			pm->ps->velocity[2] = STOMP_BOUNCE;
		}
		PM_ForceLegsAnim( LEGS_LAND );
		pm->ps->legsTimer = TIMER_LAND;
		pm->ps->bobCycle = 0;
		return;
	}

	// decide which landing animation to use
	if ( pm->ps->pm_flags & PMF_BACKWARDS_JUMP ) {
		PM_ForceLegsAnim( LEGS_LANDB );
	} else {
		PM_ForceLegsAnim( LEGS_LAND );
	}

	pm->ps->legsTimer = TIMER_LAND;

	// calculate the exact velocity on landing
	dist = pm->ps->origin[2] - pml.previous_origin[2];
	vel = pml.previous_velocity[2];
	acc = -pm->ps->gravity;

	a = acc / 2;
	b = vel;
	c = -dist;

	den =  b * b - 4 * a * c;
	if ( den < 0 ) {
		return;
	}
	t = (-b - sqrt( den ) ) / ( 2 * a );

	delta = vel + t * acc;
	delta = delta*delta * 0.0001;

	// ducking while falling doubles damage
	if ( pm->ps->pm_flags & PMF_DUCKED ) {
		delta *= 2;
	}

	// never take falling damage if completely underwater
	if ( pm->waterlevel == 3 ) {
		return;
	}

	// reduce falling damage if there is standing water
	if ( pm->waterlevel == 2 ) {
		delta *= 0.25;
	}
	if ( pm->waterlevel == 1 ) {
		delta *= 0.5;
	}

	if ( delta < 1 ) {
		return;
	}

	// create a local entity event to play the sound

	// SURF_NODAMAGE is used for bounce pads where you don't ever
	// want to take damage or play a crunch sound
	if ( ( RM_FLAGS2( pm->ps ) & RMP2_FEATHER ) && delta > 7 ) {
		// Featherweight: a soft landing, never fall damage
		PM_AddEvent( EV_FALL_SHORT );
	} else if ( !(pml.groundTrace.surfaceFlags & SURF_NODAMAGE) )  {
		if ( delta > 60 ) {
			PM_AddEvent( EV_FALL_FAR );
		} else if ( delta > 40 ) {
			// this is a pain grunt, so don't play it if dead
			if ( pm->ps->stats[STAT_HEALTH] > 0 ) {
				PM_AddEvent( EV_FALL_MEDIUM );
			}
		} else if ( delta > 7 ) {
			PM_AddEvent( EV_FALL_SHORT );
		} else {
			PM_AddEvent( PM_FootstepForSurface() );
		}
	}

	// start footstep cycle over
	pm->ps->bobCycle = 0;
}

/*
=============
PM_CheckStuck
=============
*/
/*
void PM_CheckStuck(void) {
	trace_t trace;

	pm->trace (&trace, pm->ps->origin, pm->mins, pm->maxs, pm->ps->origin, pm->ps->clientNum, pm->tracemask);
	if (trace.allsolid) {
		//int shit = qtrue;
	}
}
*/

/*
=============
PM_CorrectAllSolid
=============
*/
static int PM_CorrectAllSolid( trace_t *trace ) {
	int			i, j, k;
	vec3_t		point;

	if ( pm->debugLevel ) {
		Com_Printf("%i:allsolid\n", c_pmove);
	}

	// jitter around
	for (i = -1; i <= 1; i++) {
		for (j = -1; j <= 1; j++) {
			for (k = -1; k <= 1; k++) {
				VectorCopy(pm->ps->origin, point);
				point[0] += (float) i;
				point[1] += (float) j;
				point[2] += (float) k;
				pm->trace (trace, point, pm->mins, pm->maxs, point, pm->ps->clientNum, pm->tracemask);
				if ( !trace->allsolid ) {
					point[0] = pm->ps->origin[0];
					point[1] = pm->ps->origin[1];
					point[2] = pm->ps->origin[2] - 0.25;

					pm->trace (trace, pm->ps->origin, pm->mins, pm->maxs, point, pm->ps->clientNum, pm->tracemask);
					pml.groundTrace = *trace;
					return qtrue;
				}
			}
		}
	}

	pm->ps->groundEntityNum = ENTITYNUM_NONE;
	pml.groundPlane = qfalse;
	pml.walking = qfalse;

	return qfalse;
}


/*
=============
PM_GroundTraceMissed

The ground trace didn't hit a surface, so we are in freefall
=============
*/
static void PM_GroundTraceMissed( void ) {
	trace_t		trace;
	vec3_t		point;

	if ( pm->ps->groundEntityNum != ENTITYNUM_NONE ) {
		// we just transitioned into freefall
		if ( pm->debugLevel ) {
			Com_Printf("%i:lift\n", c_pmove);
		}

		// if they aren't in a jumping animation and the ground is a ways away, force into it
		// if we didn't do the trace, the player would be backflipping down staircases
		VectorCopy( pm->ps->origin, point );
		point[2] -= 64;

		pm->trace (&trace, pm->ps->origin, pm->mins, pm->maxs, point, pm->ps->clientNum, pm->tracemask);
		if ( trace.fraction == 1.0 ) {
			if ( pm->cmd.forwardmove >= 0 ) {
				PM_ForceLegsAnim( LEGS_JUMP );
				pm->ps->pm_flags &= ~PMF_BACKWARDS_JUMP;
			} else {
				PM_ForceLegsAnim( LEGS_JUMPB );
				pm->ps->pm_flags |= PMF_BACKWARDS_JUMP;
			}
		}
	}

	pm->ps->groundEntityNum = ENTITYNUM_NONE;
	pml.groundPlane = qfalse;
	pml.walking = qfalse;
}


/*
=============
PM_GroundTrace
=============
*/
static void PM_GroundTrace( void ) {
	vec3_t		point;
	trace_t		trace;

	point[0] = pm->ps->origin[0];
	point[1] = pm->ps->origin[1];
	point[2] = pm->ps->origin[2] - 0.25;

	pm->trace (&trace, pm->ps->origin, pm->mins, pm->maxs, point, pm->ps->clientNum, pm->tracemask);
	pml.groundTrace = trace;

	// do something corrective if the trace starts in a solid...
	if ( trace.allsolid ) {
		if ( !PM_CorrectAllSolid(&trace) )
			return;
	}

	// if the trace didn't hit anything, we are in free fall
	if ( trace.fraction == 1.0 ) {
		PM_GroundTraceMissed();
		pml.groundPlane = qfalse;
		pml.walking = qfalse;
		return;
	}

	// check if getting thrown off the ground
	if ( pm->ps->velocity[2] > 0 && DotProduct( pm->ps->velocity, trace.plane.normal ) > 10 ) {
		if ( pm->debugLevel ) {
			Com_Printf("%i:kickoff\n", c_pmove);
		}
		// go into jump animation
		if ( pm->cmd.forwardmove >= 0 ) {
			PM_ForceLegsAnim( LEGS_JUMP );
			pm->ps->pm_flags &= ~PMF_BACKWARDS_JUMP;
		} else {
			PM_ForceLegsAnim( LEGS_JUMPB );
			pm->ps->pm_flags |= PMF_BACKWARDS_JUMP;
		}

		pm->ps->groundEntityNum = ENTITYNUM_NONE;
		pml.groundPlane = qfalse;
		pml.walking = qfalse;
		return;
	}

	// slopes that are too steep will not be considered onground
	if ( trace.plane.normal[2] < MIN_WALK_NORMAL ) {
		if ( pm->debugLevel ) {
			Com_Printf("%i:steep\n", c_pmove);
		}
		// FIXME: if they can't slide down the slope, let them
		// walk (sharp crevices)
		pm->ps->groundEntityNum = ENTITYNUM_NONE;
		pml.groundPlane = qtrue;
		pml.walking = qfalse;
		return;
	}

	pml.groundPlane = qtrue;
	pml.walking = qtrue;

	// hitting solid ground will end a waterjump
	if (pm->ps->pm_flags & PMF_TIME_WATERJUMP)
	{
		pm->ps->pm_flags &= ~(PMF_TIME_WATERJUMP | PMF_TIME_LAND);
		pm->ps->pm_time = 0;
	}

	if ( pm->ps->groundEntityNum == ENTITYNUM_NONE ) {
		// just hit the ground
		if ( pm->debugLevel ) {
			Com_Printf("%i:Land\n", c_pmove);
		}

		PM_CrashLand();

		// don't do landing time if we were just going down a slope
		if ( pml.previous_velocity[2] < -200 ) {
			// don't allow another jump for a little while
			pm->ps->pm_flags |= PMF_TIME_LAND;
			pm->ps->pm_time = 250;
		}
	}

	pm->ps->groundEntityNum = trace.entityNum;

	// don't reset the z velocity for slopes
//	pm->ps->velocity[2] = 0;

	PM_AddTouchEnt( trace.entityNum );
}


/*
=============
PM_SetWaterLevel	FIXME: avoid this twice?  certainly if not moving
=============
*/
static void PM_SetWaterLevel( void ) {
	vec3_t		point;
	int			cont;
	int			sample1;
	int			sample2;

	//
	// get waterlevel, accounting for ducking
	//
	pm->waterlevel = 0;
	pm->watertype = 0;

	point[0] = pm->ps->origin[0];
	point[1] = pm->ps->origin[1];
	point[2] = pm->ps->origin[2] + MINS_Z + 1;
	cont = pm->pointcontents( point, pm->ps->clientNum );

	if ( cont & MASK_WATER ) {
		sample2 = pm->ps->viewheight - MINS_Z;
		sample1 = sample2 / 2;

		pm->watertype = cont;
		pm->waterlevel = 1;
		point[2] = pm->ps->origin[2] + MINS_Z + sample1;
		cont = pm->pointcontents (point, pm->ps->clientNum );
		if ( cont & MASK_WATER ) {
			pm->waterlevel = 2;
			point[2] = pm->ps->origin[2] + MINS_Z + sample2;
			cont = pm->pointcontents (point, pm->ps->clientNum );
			if ( cont & MASK_WATER ){
				pm->waterlevel = 3;
			}
		}
	}

}

/*
==============
PM_CheckDuck

Sets mins, maxs, and pm->ps->viewheight
==============
*/
static void PM_CheckDuck (void)
{
	trace_t	trace;

#ifdef MISSIONPACK
	if ( pm->ps->powerups[PW_INVULNERABILITY] ) {
		if ( pm->ps->pm_flags & PMF_INVULEXPAND ) {
			// invulnerability sphere has a 42 units radius
			VectorSet( pm->mins, -INVUL_RADIUS, -INVUL_RADIUS, -INVUL_RADIUS );
			VectorSet( pm->maxs, INVUL_RADIUS, INVUL_RADIUS, INVUL_RADIUS );
		}
		else {
			VectorSet( pm->mins, -PLAYER_WIDTH, -PLAYER_WIDTH, MINS_Z );
			VectorSet( pm->maxs, PLAYER_WIDTH, PLAYER_WIDTH, 16 );
		}
		pm->ps->pm_flags |= PMF_DUCKED;
		pm->ps->viewheight = CROUCH_VIEWHEIGHT;
		return;
	}
#endif
	pm->ps->pm_flags &= ~PMF_INVULEXPAND;

	pm->mins[0] = -PLAYER_WIDTH;
	pm->mins[1] = -PLAYER_WIDTH;

	pm->maxs[0] = PLAYER_WIDTH;
	pm->maxs[1] = PLAYER_WIDTH;

	pm->mins[2] = MINS_Z;

	if (pm->ps->pm_type == PM_DEAD)
	{
		pm->maxs[2] = DEAD_HEIGHT;
		pm->ps->viewheight = DEAD_VIEWHEIGHT;
		return;
	}

	if ( pm->cmd.upmove < 0 || ( RM_FLAGS2( pm->ps ) & RMS2_SLIDING ) )
	{	// duck (a slide keeps the crouched hull)
		pm->ps->pm_flags |= PMF_DUCKED;
	}
	else
	{	// stand up if possible
		if (pm->ps->pm_flags & PMF_DUCKED)
		{
			// try to stand up
			pm->maxs[2] = DEFAULT_HEIGHT;
			pm->trace (&trace, pm->ps->origin, pm->mins, pm->maxs, pm->ps->origin, pm->ps->clientNum, pm->tracemask );
			if (!trace.allsolid)
				pm->ps->pm_flags &= ~PMF_DUCKED;
		}
	}

	if (pm->ps->pm_flags & PMF_DUCKED)
	{
		pm->maxs[2] = CROUCH_HEIGHT;
		pm->ps->viewheight = CROUCH_VIEWHEIGHT;
	}
	else
	{
		pm->maxs[2] = DEFAULT_HEIGHT;
		pm->ps->viewheight = DEFAULT_VIEWHEIGHT;
	}
}



//===================================================================


/*
===============
PM_Footsteps
===============
*/
static void PM_Footsteps( void ) {
	float		bobmove;
	int			old;
	qboolean	footstep;

	//
	// calculate speed and cycle to be used for
	// all cyclic walking effects
	//
	pm->xyspeed = sqrt( pm->ps->velocity[0] * pm->ps->velocity[0]
		+  pm->ps->velocity[1] * pm->ps->velocity[1] );

	if ( pm->ps->groundEntityNum == ENTITYNUM_NONE ) {

#ifdef MISSIONPACK
		if ( pm->ps->powerups[PW_INVULNERABILITY] ) {
			PM_ContinueLegsAnim( LEGS_IDLECR );
		}
#endif
		// airborne leaves position in cycle intact, but doesn't advance
		if ( pm->waterlevel > 1 ) {
			PM_ContinueLegsAnim( LEGS_SWIM );
		}
		return;
	}

	// sliding: a low crouched pose, no bob and no footsteps
	if ( RM_FLAGS2( pm->ps ) & RMS2_SLIDING ) {
		pm->ps->bobCycle = 0;
		PM_ContinueLegsAnim( LEGS_IDLECR );
		return;
	}

	// if not trying to move
	if ( !pm->cmd.forwardmove && !pm->cmd.rightmove ) {
		if (  pm->xyspeed < 5 ) {
			pm->ps->bobCycle = 0;	// start at beginning of cycle again
			if ( pm->ps->pm_flags & PMF_DUCKED ) {
				PM_ContinueLegsAnim( LEGS_IDLECR );
			} else {
				PM_ContinueLegsAnim( LEGS_IDLE );
			}
		}
		return;
	}


	footstep = qfalse;

	if ( pm->ps->pm_flags & PMF_DUCKED ) {
		bobmove = 0.5;	// ducked characters bob much faster
		if ( pm->ps->pm_flags & PMF_BACKWARDS_RUN ) {
			PM_ContinueLegsAnim( LEGS_BACKCR );
		}
		else {
			PM_ContinueLegsAnim( LEGS_WALKCR );
		}
		// ducked characters never play footsteps
	/*
	} else 	if ( pm->ps->pm_flags & PMF_BACKWARDS_RUN ) {
		if ( !( pm->cmd.buttons & BUTTON_WALKING ) ) {
			bobmove = 0.4;	// faster speeds bob faster
			footstep = qtrue;
		} else {
			bobmove = 0.3;
		}
		PM_ContinueLegsAnim( LEGS_BACK );
	*/
	} else {
		if ( !( pm->cmd.buttons & BUTTON_WALKING ) ) {
			bobmove = 0.4f;	// faster speeds bob faster
			if ( pm->ps->pm_flags & PMF_BACKWARDS_RUN ) {
				PM_ContinueLegsAnim( LEGS_BACK );
			}
			else {
				PM_ContinueLegsAnim( LEGS_RUN );
			}
			footstep = qtrue;
		} else {
			bobmove = 0.3f;	// walking bobs slow
			if ( pm->ps->pm_flags & PMF_BACKWARDS_RUN ) {
				PM_ContinueLegsAnim( LEGS_BACKWALK );
			}
			else {
				PM_ContinueLegsAnim( LEGS_WALK );
			}
		}
	}

	// check for footstep / splash sounds
	old = pm->ps->bobCycle;
	pm->ps->bobCycle = (int)( old + bobmove * pml.msec ) & 255;

	// if we just crossed a cycle boundary, play an appropriate footstep event
	if ( ( ( old + 64 ) ^ ( pm->ps->bobCycle + 64 ) ) & 128 ) {
		if ( pm->waterlevel == 0 ) {
			// on ground will only play sounds if running
			if ( footstep && !pm->noFootsteps ) {
				PM_AddEvent( PM_FootstepForSurface() );
			}
		} else if ( pm->waterlevel == 1 ) {
			// splashing
			PM_AddEvent( EV_FOOTSPLASH );
		} else if ( pm->waterlevel == 2 ) {
			// wading / swimming at surface
			PM_AddEvent( EV_SWIM );
		} else if ( pm->waterlevel == 3 ) {
			// no sound when completely underwater

		}
	}
}

/*
==============
PM_WaterEvents

Generate sound events for entering and leaving water
==============
*/
static void PM_WaterEvents( void ) {		// FIXME?
	//
	// if just entered a water volume, play a sound
	//
	if (!pml.previous_waterlevel && pm->waterlevel) {
		PM_AddEvent( EV_WATER_TOUCH );
	}

	//
	// if just completely exited a water volume, play a sound
	//
	if (pml.previous_waterlevel && !pm->waterlevel) {
		PM_AddEvent( EV_WATER_LEAVE );
	}

	//
	// check for head just going under water
	//
	if (pml.previous_waterlevel != 3 && pm->waterlevel == 3) {
		PM_AddEvent( EV_WATER_UNDER );
	}

	//
	// check for head just coming out of water
	//
	if (pml.previous_waterlevel == 3 && pm->waterlevel != 3) {
		PM_AddEvent( EV_WATER_CLEAR );
	}
}


/*
===============
PM_BeginWeaponChange
===============
*/
static void PM_BeginWeaponChange( int weapon ) {
	if ( weapon <= WP_NONE || weapon >= WP_NUM_WEAPONS ) {
		return;
	}

	if ( !( pm->ps->stats[STAT_WEAPONS] & ( 1 << weapon ) ) ) {
		return;
	}

	if ( pm->ps->weaponstate == WEAPON_DROPPING ) {
		return;
	}

	PM_AddEvent( EV_CHANGE_WEAPON );
	pm->ps->stats[STAT_ALTCHARGE] = 0;		// switching drops a charge
	pm->ps->weaponstate = WEAPON_DROPPING;
	pm->ps->weaponTime += 200;
	PM_StartTorsoAnim( TORSO_DROP );
}


/*
===============
PM_FinishWeaponChange
===============
*/
static void PM_FinishWeaponChange( void ) {
	int		weapon;

	weapon = pm->cmd.weapon;
	if ( weapon < WP_NONE || weapon >= WP_NUM_WEAPONS ) {
		weapon = WP_NONE;
	}

	if ( !( pm->ps->stats[STAT_WEAPONS] & ( 1 << weapon ) ) ) {
		weapon = WP_NONE;
	}

	pm->ps->weapon = weapon;
	pm->ps->weaponstate = WEAPON_RAISING;
	pm->ps->weaponTime += 250;
	PM_StartTorsoAnim( TORSO_RAISE );
}


/*
==============
PM_TorsoAnimation

==============
*/
static void PM_TorsoAnimation( void ) {
	if ( pm->ps->weaponstate == WEAPON_READY ) {
		if ( pm->ps->weapon == WP_GAUNTLET ) {
			PM_ContinueTorsoAnim( TORSO_STAND2 );
		} else {
			PM_ContinueTorsoAnim( TORSO_STAND );
		}
		return;
	}
}


/*
==============
PM_AltFire

Alt-fire shot: ammo, event, refire time
==============
*/
#define GAUNTLET_LUNGE_SPEED	700
#define GAUNTLET_PUNCH_SPEED	600		// extra lunge speed at full charge
#define GAUNTLET_LUNGE_LIFT		150

/*
==============
PM_ChargeAlt

Charged alt-fire: holding alt-fire charges it, releasing fires. A short press is the quick alt.
Returns qtrue when the alt button owns the weapon this frame.
==============
*/
static void PM_AltFire( int charge );

static qboolean PM_ChargeAlt( void ) {
	const altFire_t	*alt = &bg_altFire[pm->ps->weapon];
	int				*charge = &pm->ps->stats[STAT_ALTCHARGE];
	int				held, parm;

	if ( alt->mode == ALTFIRE_NONE ) {
		*charge = 0;
		return qfalse;
	}
	if ( ( pm->cmd.buttons & BUTTON_ALTATTACK ) && !( pm->cmd.buttons & BUTTON_ATTACK ) ) {
		// charging (starts only with the weapon ready)
		if ( *charge < ALT_CHARGE_TIME + 1 ) {
			int	add;

			add = ( pm->ps->stats[STAT_RMFLAGS] & RMP_OVERCHARGER ) ? pml.msec * 7 / 5 : pml.msec;
			if ( ( pm->ps->stats[STAT_RMFLAGS] & RMP_OVERCHARGER ) && DS_PM_ON( pm->ps ) ) {
				// DESCENT: 9% faster, dithered on the command time so small frames keep it (predicted)
				add = ( pm->cmd.serverTime & 0xfffff ) + 0x100000;
				add = pml.msec + add * DSP_OVERCHARGER_PCT / 100 - ( add - pml.msec ) * DSP_OVERCHARGER_PCT / 100;
			}
			if ( RM_FLAGS2( pm->ps ) & RMF2_OVERCHARGE ) {
				add *= OVERCHARGE_RATE;		// Overcharge mutator
			}
			if ( *charge < ALT_CHARGE_TIME && *charge + add >= ALT_CHARGE_TIME
				&& ( alt->chargeAmmo > 0 || pm->ps->weapon == WP_CHAINGUN ) ) {
				PM_AddEvent( EV_CHARGE_READY );
			}
			*charge += add;
			if ( *charge > ALT_CHARGE_TIME + 1 ) {
				*charge = ALT_CHARGE_TIME + 1;
			}
		}
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
		// the chaingun alt spins the barrel up without firing
		if ( pm->ps->weapon == WP_CHAINGUN && *charge >= ALT_TAP_TIME ) {
			pm->ps->stats[STAT_SPINUP] += pml.msec;
			if ( pm->ps->stats[STAT_SPINUP] > CHAINGUN_SPIN_TIME ) {
				pm->ps->stats[STAT_SPINUP] = CHAINGUN_SPIN_TIME;
			}
		}
#endif
		pm->ps->weaponstate = WEAPON_READY;
		return qtrue;
	}
	if ( *charge <= 0 ) {
		return qfalse;
	}

	// released
	held = *charge;
	*charge = 0;
	if ( pm->cmd.buttons & BUTTON_ATTACK ) {
		return qfalse;		// the primary trigger cancels the charge
	}
	if ( held < ALT_TAP_TIME ) {
		if ( alt->mode != ALTFIRE_SHOT ) {
			return qtrue;	// trigger alts acted on the press (server side)
		}
		parm = 0;
	} else {
		if ( alt->chargeAmmo <= 0 ) {
			return qtrue;	// nothing to fire (chaingun spin-up)
		}
		if ( held > ALT_CHARGE_TIME ) {
			held = ALT_CHARGE_TIME;
		}
		parm = 1 + ( held - ALT_TAP_TIME ) * 254 / ( ALT_CHARGE_TIME - ALT_TAP_TIME );
	}
	PM_StartTorsoAnim( pm->ps->weapon == WP_GAUNTLET ? TORSO_ATTACK2 : TORSO_ATTACK );
	pm->ps->weaponstate = WEAPON_FIRING;
	PM_AltFire( parm );
	return qtrue;
}

/*
==============
PM_ShotgunKick

Shotgun recoil pushes the shooter back: aim down to shotgun jump
==============
*/
static void PM_ShotgunKick( void ) {
	if ( pm->ps->weapon != WP_SHOTGUN || !RMF( RMF_SHOTGUNKICK ) ) {
		return;
	}
	VectorMA( pm->ps->velocity, -SHOTGUN_KICK, pml.forward, pm->ps->velocity );
	if ( pml.forward[2] < -0.3f ) {
		pm->ps->groundEntityNum = ENTITYNUM_NONE;
	}
}

static void PM_AltFire( int charge ) {
	const altFire_t	*alt = &bg_altFire[pm->ps->weapon];
	int				addTime, ammo;

	ammo = charge ? alt->chargeAmmo : alt->ammo;
	if ( pm->ps->ammo[pm->ps->weapon] != -1 && pm->ps->ammo[pm->ps->weapon] < ammo ) {
		PM_AddEvent( EV_NOAMMO );
		pm->ps->weaponTime += 500;
		return;
	}
	if ( pm->ps->ammo[pm->ps->weapon] != -1 ) {
		pm->ps->ammo[pm->ps->weapon] -= ammo;
	}

	BG_AddPredictableEventToPlayerstate( EV_FIRE_WEAPON_ALT, charge, pm->ps );
	PM_ShotgunKick();

	if ( pm->ps->weapon == WP_GAUNTLET ) {
		// lunge along the view direction, much further when charged (rocket punch)
		VectorScale( pml.forward, GAUNTLET_LUNGE_SPEED + GAUNTLET_PUNCH_SPEED * ALT_CHARGE_FRAC( charge ), pm->ps->velocity );
		if ( pm->ps->velocity[2] < GAUNTLET_LUNGE_LIFT ) {
			pm->ps->velocity[2] = GAUNTLET_LUNGE_LIFT;
		}
		pm->ps->groundEntityNum = ENTITYNUM_NONE;
	}

	addTime = charge ? alt->chargeTime : alt->time;
	if ( pm->ps->stats[STAT_RMFLAGS] & RMP_QUICKHANDS ) {
		addTime = DS_PM_ON( pm->ps ) ? addTime * DSP_QUICKHANDS_PCT / 100 : addTime * 4 / 5;	// DESCENT: 6% faster
	}
	if ( pm->ps->powerups[PW_HASTE] ) {
		addTime /= 1.3;
	}
#ifndef MISSIONPACK
	if ( pm->ps->powerups[PW_RAPIDFIRE] ) {
		addTime /= 2;
	}
#endif
	addTime = DS_FIRETIME( pm->ps, addTime );		// descent refire scale
	pm->ps->weaponTime += addTime;
}

/*
==============
PM_Weapon

Generates weapon events and modifes the weapon counter
==============
*/
static void PM_Weapon( void ) {
	int		addTime;
	qboolean	altFire, altOk;

	// don't allow attack until all buttons are up
	if ( pm->ps->pm_flags & PMF_RESPAWNED ) {
		return;
	}

	// ignore if spectator
	if ( pm->ps->persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
		return;
	}

	// check for dead player
	if ( pm->ps->stats[STAT_HEALTH] <= 0 ) {
		pm->ps->weapon = WP_NONE;
		return;
	}

	// check for item using
	if ( pm->cmd.buttons & BUTTON_USE_HOLDABLE ) {
		if ( ! ( pm->ps->pm_flags & PMF_USE_ITEM_HELD ) ) {
			if ( bg_itemlist[pm->ps->stats[STAT_HOLDABLE_ITEM]].giTag == HI_MEDKIT
				&& pm->ps->stats[STAT_HEALTH] >= (pm->ps->stats[STAT_MAX_HEALTH] + 25) ) {
				// don't use medkit if at max health
			} else {
				pm->ps->pm_flags |= PMF_USE_ITEM_HELD;
				PM_AddEvent( EV_USE_ITEM0 + bg_itemlist[pm->ps->stats[STAT_HOLDABLE_ITEM]].giTag );
				pm->ps->stats[STAT_HOLDABLE_ITEM] = 0;
			}
			return;
		}
	} else {
		pm->ps->pm_flags &= ~PMF_USE_ITEM_HELD;
	}


	// make weapon function
	if ( pm->ps->weaponTime > 0 ) {
		pm->ps->weaponTime -= pml.msec;
	}

	// check for weapon change
	// can't change if weapon is firing, but can change
	// again if lowering or raising
	if ( pm->ps->weaponTime <= 0 || pm->ps->weaponstate != WEAPON_FIRING ) {
		if ( pm->ps->weapon != pm->cmd.weapon ) {
			PM_BeginWeaponChange( pm->cmd.weapon );
		}
	}

	if ( pm->ps->weaponTime > 0 ) {
		return;
	}

	// change weapon if time
	if ( pm->ps->weaponstate == WEAPON_DROPPING ) {
		PM_FinishWeaponChange();
		return;
	}

	if ( pm->ps->weaponstate == WEAPON_RAISING ) {
		pm->ps->weaponstate = WEAPON_READY;
		if ( pm->ps->weapon == WP_GAUNTLET ) {
			PM_StartTorsoAnim( TORSO_STAND2 );
		} else {
			PM_StartTorsoAnim( TORSO_STAND );
		}
		return;
	}

	// check for fire
	altFire = qfalse;
	// a locked alt-fire (roguelike: no card for this weapon) does nothing at all
	altOk = RMF( RMF_ALTFIRE ) && RM_ALT_USABLE( pm->ps, pm->ps->weapon );
	if ( !altOk ) {
		pm->cmd.buttons &= ~BUTTON_ALTATTACK;
	}
	if ( pm->ps->weapon != WP_CHAINGUN ) {
		pm->ps->stats[STAT_SPINUP] = 0;
	} else if ( ( pm->ps->stats[STAT_DSFIRE] & ( DSF_ON | DSF_NOSPINUP ) ) == ( DSF_ON | DSF_NOSPINUP ) ) {
		pm->ps->stats[STAT_SPINUP] = CHAINGUN_SPIN_TIME;		// descent RIPSAW: always at full speed
	} else if ( !( pm->cmd.buttons & ( BUTTON_ATTACK | BUTTON_ALTATTACK ) )
		&& ( pm->ps->stats[STAT_DSFIRE] & ( DSF_ON | DSF_NOSPINDOWN ) ) != ( DSF_ON | DSF_NOSPINDOWN ) ) {
		// the chaingun spins down quickly once both triggers are let go
		pm->ps->stats[STAT_SPINUP] -= pml.msec * 3;
		if ( pm->ps->stats[STAT_SPINUP] < 0 ) {
			pm->ps->stats[STAT_SPINUP] = 0;
		}
	}
	if ( altOk && RMF( RMF_CHARGE ) ) {
		if ( PM_ChargeAlt() ) {
			return;
		}
	} else {
		pm->ps->stats[STAT_ALTCHARGE] = 0;
	}
	if ( ! (pm->cmd.buttons & BUTTON_ATTACK) ) {
		if ( ( pm->cmd.buttons & BUTTON_ALTATTACK ) && bg_altFire[pm->ps->weapon].mode == ALTFIRE_SHOT && altOk
			&& !RMF( RMF_CHARGE ) ) {
			altFire = qtrue;
		} else {
			pm->ps->weaponTime = 0;
			pm->ps->weaponstate = WEAPON_READY;
			return;
		}
	}

	if ( altFire ) {
		PM_StartTorsoAnim( pm->ps->weapon == WP_GAUNTLET ? TORSO_ATTACK2 : TORSO_ATTACK );
		pm->ps->weaponstate = WEAPON_FIRING;
		PM_AltFire( 0 );
		return;
	}

	// start the animation even if out of ammo
	if ( pm->ps->weapon == WP_GAUNTLET ) {
		// the guantlet only "fires" when it actually hits something
		if ( !pm->gauntletHit ) {
			pm->ps->weaponTime = 0;
			pm->ps->weaponstate = WEAPON_READY;
			return;
		}
		PM_StartTorsoAnim( TORSO_ATTACK2 );
	} else {
		PM_StartTorsoAnim( TORSO_ATTACK );
	}

	pm->ps->weaponstate = WEAPON_FIRING;

	// check for out of ammo
	if ( ! pm->ps->ammo[ pm->ps->weapon ] ) {
		PM_AddEvent( EV_NOAMMO );
		pm->ps->weaponTime += 500;
		return;
	}

	// take an ammo away if not infinite
	if ( pm->ps->ammo[ pm->ps->weapon ] != -1 ) {
		pm->ps->ammo[ pm->ps->weapon ]--;
	}

	// fire weapon
	PM_AddEvent( EV_FIRE_WEAPON );

	PM_ShotgunKick();

	switch( pm->ps->weapon ) {
	default:
	case WP_GAUNTLET:
		addTime = 400;
		break;
	case WP_LIGHTNING:
		addTime = 50;
		break;
	case WP_SHOTGUN:
		addTime = 1000;
		break;
	case WP_MACHINEGUN:
		addTime = 100;
		break;
	case WP_GRENADE_LAUNCHER:
		addTime = 800;
		break;
	case WP_ROCKET_LAUNCHER:
		addTime = 800;
		break;
	case WP_PLASMAGUN:
		addTime = 100;
		break;
	case WP_RAILGUN:
		addTime = 1500;
		break;
	case WP_BFG:
		addTime = 200;
		break;
	case WP_GRAPPLING_HOOK:
		addTime = 400;
		break;
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case WP_NAILGUN:
		addTime = 1000;
		break;
	case WP_PROX_LAUNCHER:
		addTime = 800;
		break;
	case WP_CHAINGUN:
		if ( !RMF( RMF_SPINUP ) ) {
			addTime = 30;		// Team Arena's fixed rate
			break;
		}
		// spin up from slow to very fast while the trigger is held
		addTime = CHAINGUN_SLOW_TIME - ( CHAINGUN_SLOW_TIME - CHAINGUN_FAST_TIME )
			* pm->ps->stats[STAT_SPINUP] / CHAINGUN_SPIN_TIME;
		pm->ps->stats[STAT_SPINUP] += ( ( pm->ps->stats[STAT_DSFIRE] & ( DSF_ON | DSF_SPINFAST ) ) == ( DSF_ON | DSF_SPINFAST ) )
			? addTime * 109 / 100 : addTime;		// descent chaingun level 3: spins up 9% faster
		if ( pm->ps->stats[STAT_SPINUP] > CHAINGUN_SPIN_TIME ) {
			pm->ps->stats[STAT_SPINUP] = CHAINGUN_SPIN_TIME;
		}
		break;
#endif
	}

#ifdef MISSIONPACK
	if( bg_itemlist[pm->ps->stats[STAT_PERSISTANT_POWERUP]].giTag == PW_SCOUT ) {
		addTime /= 1.5;
	}
	else
	if( bg_itemlist[pm->ps->stats[STAT_PERSISTANT_POWERUP]].giTag == PW_AMMOREGEN ) {
		addTime /= 1.3;
	}
	else
#endif
	if ( pm->ps->powerups[PW_HASTE] ) {
		addTime /= 1.3;
	}
#ifndef MISSIONPACK
	if ( pm->ps->powerups[PW_RAPIDFIRE] ) {
		addTime /= 2;
	}
#endif
	if ( pm->ps->stats[STAT_RMFLAGS] & RMP_QUICKHANDS ) {
		addTime = DS_PM_ON( pm->ps ) ? addTime * DSP_QUICKHANDS_PCT / 100 : addTime * 4 / 5;	// DESCENT: 6% faster
	}
	addTime = DS_FIRETIME( pm->ps, addTime );		// descent: weapon level / HAIR TRIGGER refire scale

	pm->ps->weaponTime += addTime;
}

/*
================
PM_Animate
================
*/

static void PM_Animate( void ) {
	if ( pm->cmd.buttons & BUTTON_GESTURE ) {
		if ( pm->ps->torsoTimer == 0 ) {
			PM_StartTorsoAnim( TORSO_GESTURE );
			pm->ps->torsoTimer = TIMER_GESTURE;
			PM_AddEvent( EV_TAUNT );
		}
#ifdef MISSIONPACK
	} else if ( pm->cmd.buttons & BUTTON_GETFLAG ) {
		if ( pm->ps->torsoTimer == 0 ) {
			PM_StartTorsoAnim( TORSO_GETFLAG );
			pm->ps->torsoTimer = 600;	//TIMER_GESTURE;
		}
	} else if ( pm->cmd.buttons & BUTTON_GUARDBASE ) {
		if ( pm->ps->torsoTimer == 0 ) {
			PM_StartTorsoAnim( TORSO_GUARDBASE );
			pm->ps->torsoTimer = 600;	//TIMER_GESTURE;
		}
	} else if ( pm->cmd.buttons & BUTTON_PATROL ) {
		if ( pm->ps->torsoTimer == 0 ) {
			PM_StartTorsoAnim( TORSO_PATROL );
			pm->ps->torsoTimer = 600;	//TIMER_GESTURE;
		}
	} else if ( pm->cmd.buttons & BUTTON_FOLLOWME ) {
		if ( pm->ps->torsoTimer == 0 ) {
			PM_StartTorsoAnim( TORSO_FOLLOWME );
			pm->ps->torsoTimer = 600;	//TIMER_GESTURE;
		}
	} else if ( pm->cmd.buttons & BUTTON_AFFIRMATIVE ) {
		if ( pm->ps->torsoTimer == 0 ) {
			PM_StartTorsoAnim( TORSO_AFFIRMATIVE);
			pm->ps->torsoTimer = 600;	//TIMER_GESTURE;
		}
	} else if ( pm->cmd.buttons & BUTTON_NEGATIVE ) {
		if ( pm->ps->torsoTimer == 0 ) {
			PM_StartTorsoAnim( TORSO_NEGATIVE );
			pm->ps->torsoTimer = 600;	//TIMER_GESTURE;
		}
#endif
	}
}


/*
================
PM_DropTimers
================
*/
static void PM_DropTimers( void ) {
	// drop misc timing counter
	if ( pm->ps->pm_time ) {
		if ( pml.msec >= pm->ps->pm_time ) {
			pm->ps->pm_flags &= ~PMF_ALL_TIMES;
			pm->ps->pm_time = 0;
		} else {
			pm->ps->pm_time -= pml.msec;
		}
	}

	// drop animation counter
	if ( pm->ps->legsTimer > 0 ) {
		pm->ps->legsTimer -= pml.msec;
		if ( pm->ps->legsTimer < 0 ) {
			pm->ps->legsTimer = 0;
		}
	}

	if ( pm->ps->torsoTimer > 0 ) {
		pm->ps->torsoTimer -= pml.msec;
		if ( pm->ps->torsoTimer < 0 ) {
			pm->ps->torsoTimer = 0;
		}
	}

	// movement tech cooldowns
	if ( pm->ps->stats[STAT_DASH_TIME] > 0 ) {
		pm->ps->stats[STAT_DASH_TIME] -= pml.msec;
		if ( pm->ps->stats[STAT_DASH_TIME] < 0 ) {
			pm->ps->stats[STAT_DASH_TIME] = 0;
		}
	}
	if ( pm->ps->stats[STAT_WALLJUMP_TIME] > 0 ) {
		pm->ps->stats[STAT_WALLJUMP_TIME] -= pml.msec;
		if ( pm->ps->stats[STAT_WALLJUMP_TIME] < 0 ) {
			pm->ps->stats[STAT_WALLJUMP_TIME] = 0;
		}
	}
	if ( pm->ps->stats[STAT_JUMPTIMER] > 0 ) {
		pm->ps->stats[STAT_JUMPTIMER] -= pml.msec;
		if ( pm->ps->stats[STAT_JUMPTIMER] < 0 ) {
			pm->ps->stats[STAT_JUMPTIMER] = 0;
		}
	}
}

/*
================
PM_UpdateViewAngles

This can be used as another entry point when only the viewangles
are being updated instead of a full move
================
*/
void PM_UpdateViewAngles( playerState_t *ps, const usercmd_t *cmd ) {
	short		temp;
	int		i;

	if ( ps->pm_type == PM_INTERMISSION || ps->pm_type == PM_SPINTERMISSION) {
		return;		// no view changes at all
	}

	if ( ps->pm_type != PM_SPECTATOR && ps->stats[STAT_HEALTH] <= 0 ) {
		return;		// no view changes at all
	}

	// circularly clamp the angles with deltas
	for (i=0 ; i<3 ; i++) {
		temp = cmd->angles[i] + ps->delta_angles[i];
		if ( i == PITCH ) {
			// don't let the player look up or down more than 90 degrees
			if ( temp > 16000 ) {
				ps->delta_angles[i] = 16000 - cmd->angles[i];
				temp = 16000;
			} else if ( temp < -16000 ) {
				ps->delta_angles[i] = -16000 - cmd->angles[i];
				temp = -16000;
			}
		}
		ps->viewangles[i] = SHORT2ANGLE(temp);
	}

}


/*
================
PmoveSingle

================
*/
void trap_SnapVector( float *v );

void PmoveSingle (pmove_t *pmove) {
	qboolean	wasOnGround;
	qboolean	zipping;

	pm = pmove;

	// this counter lets us debug movement problems with a journal
	// by setting a conditional breakpoint fot the previous frame
	c_pmove++;

	// clear results
	pm->numtouch = 0;
	pm->watertype = 0;
	pm->waterlevel = 0;

	if ( pm->ps->stats[STAT_HEALTH] <= 0 ) {
		pm->tracemask &= ~CONTENTS_BODY;	// corpses can fly through bodies
	}

	// make sure walking button is clear if they are running, to avoid
	// proxy no-footsteps cheats
	if ( abs( pm->cmd.forwardmove ) > 64 || abs( pm->cmd.rightmove ) > 64 ) {
		pm->cmd.buttons &= ~BUTTON_WALKING;
	}

	// set the talk balloon flag
	if ( pm->cmd.buttons & BUTTON_TALK ) {
		pm->ps->eFlags |= EF_TALK;
	} else {
		pm->ps->eFlags &= ~EF_TALK;
	}

	// set the firing flag for continuous beam weapons
	if ( !(pm->ps->pm_flags & PMF_RESPAWNED) && pm->ps->pm_type != PM_INTERMISSION && pm->ps->pm_type != PM_NOCLIP
		&& ( pm->cmd.buttons & BUTTON_ATTACK ) && pm->ps->ammo[ pm->ps->weapon ] ) {
		pm->ps->eFlags |= EF_FIRING;
	} else {
		pm->ps->eFlags &= ~EF_FIRING;
	}

	// clear the respawned flag if attack and use are cleared
	if ( pm->ps->stats[STAT_HEALTH] > 0 &&
		!( pm->cmd.buttons & (BUTTON_ATTACK | BUTTON_USE_HOLDABLE) ) ) {
		pm->ps->pm_flags &= ~PMF_RESPAWNED;
	}

	// if talk button is down, dissallow all other input
	// this is to prevent any possible intercept proxy from
	// adding fake talk balloons
	if ( pmove->cmd.buttons & BUTTON_TALK ) {
		// keep the talk button set tho for when the cmd.serverTime > 66 msec
		// and the same cmd is used multiple times in Pmove
		pmove->cmd.buttons = BUTTON_TALK;
		pmove->cmd.forwardmove = 0;
		pmove->cmd.rightmove = 0;
		pmove->cmd.upmove = 0;
	}

	// clear all pmove local vars
	memset (&pml, 0, sizeof(pml));

	// determine the time
	pml.msec = pmove->cmd.serverTime - pm->ps->commandTime;
	if ( pml.msec < 1 ) {
		pml.msec = 1;
	} else if ( pml.msec > 200 ) {
		pml.msec = 200;
	}
	pm->ps->commandTime = pmove->cmd.serverTime;

	// save old org in case we get stuck
	VectorCopy (pm->ps->origin, pml.previous_origin);

	// save old velocity for crashlanding
	VectorCopy (pm->ps->velocity, pml.previous_velocity);

	pml.frametime = pml.msec * 0.001;

	// update the viewangles
	PM_UpdateViewAngles( pm->ps, &pm->cmd );

	AngleVectors (pm->ps->viewangles, pml.forward, pml.right, pml.up);

	if ( pm->cmd.upmove < 10 ) {
		// not holding jump
		pm->ps->pm_flags &= ~PMF_JUMP_HELD;
	}

	// decide if backpedaling animations should be used
	if ( pm->cmd.forwardmove < 0 ) {
		pm->ps->pm_flags |= PMF_BACKWARDS_RUN;
	} else if ( pm->cmd.forwardmove > 0 || ( pm->cmd.forwardmove == 0 && pm->cmd.rightmove ) ) {
		pm->ps->pm_flags &= ~PMF_BACKWARDS_RUN;
	}

	if ( pm->ps->pm_type >= PM_DEAD ) {
		pm->cmd.forwardmove = 0;
		pm->cmd.rightmove = 0;
		pm->cmd.upmove = 0;
	}

	if ( pm->ps->pm_type == PM_SPECTATOR ) {
		PM_CheckDuck ();
		PM_FlyMove ();
		PM_DropTimers ();
		return;
	}

	if ( pm->ps->pm_type == PM_NOCLIP ) {
		PM_NoclipMove ();
		PM_DropTimers ();
		return;
	}

	if (pm->ps->pm_type == PM_FREEZE) {
		return;		// no movement at all
	}

	if ( pm->ps->pm_type == PM_INTERMISSION || pm->ps->pm_type == PM_SPINTERMISSION) {
		return;		// no movement at all
	}

	// set watertype, and waterlevel
	PM_SetWaterLevel();
	pml.previous_waterlevel = pmove->waterlevel;

	// set mins, maxs, and viewheight
	PM_CheckDuck ();

	// set groundentity
	wasOnGround = pm->ps->groundEntityNum != ENTITYNUM_NONE;
	PM_GroundTrace();

	if ( pm->ps->pm_type == PM_DEAD ) {
		PM_DeadMove ();
	}

	PM_DropTimers();

	// touching the ground or swimming restores the air moves
	if ( pml.walking || pm->waterlevel > 1 ) {
		pm->ps->pm_flags &= ~( PMF_DOUBLE_JUMPED | PMF_AIR_DASHED );
		pm->ps->stats[STAT_WALLRUN_TIME] = 0;
		pm->ps->stats[STAT_JUMPTIMER] = 0;
		if ( !( pm->ps->loopSound & RMZ_ZIPPING ) ) {
			pm->ps->loopSound &= ~RMZ_USED;
		}
	} else if ( wasOnGround && pm->ps->velocity[2] <= 10 && RMF( RMF_JUMPASSIST ) ) {
		pm->ps->stats[STAT_JUMPTIMER] = COYOTE_TIME;	// walked off a ledge
	}
	// a grapple pull restores them once, when it starts
	if ( pm->ps->pm_flags & PMF_GRAPPLE_PULL ) {
		if ( !( pm->ps->stats[STAT_RMFLAGS] & RMS_HOOKREFRESH ) ) {
			pm->ps->stats[STAT_RMFLAGS] |= RMS_HOOKREFRESH;
			pm->ps->pm_flags &= ~( PMF_DOUBLE_JUMPED | PMF_AIR_DASHED );
			pm->ps->stats[STAT_WALLRUN_TIME] = 0;
			pm->ps->loopSound &= ~RMZ_USED;
		}
	} else {
		pm->ps->stats[STAT_RMFLAGS] &= ~RMS_HOOKREFRESH;
	}

	zipping = PM_CheckZipLine();
	PM_CrouchMoves();
	if ( !pm->ps->powerups[PW_FLIGHT] ) {
		PM_CheckDash();
	}
	if ( zipping && !( pm->ps->loopSound & RMZ_ZIPPING ) ) {
		zipping = qfalse;	// the dash let go of the line
	}
	PM_CheckWallRun();

#ifndef MISSIONPACK
	// jetpack: holding jump thrusts upwards
	pm->ps->eFlags &= ~EF_JETTING;
	if ( pm->ps->powerups[PW_JETPACK] && pm->cmd.upmove >= 10 && pm->ps->pm_type == PM_NORMAL
		&& pm->waterlevel <= 1 && !( pm->ps->pm_flags & PMF_RESPAWNED ) ) {
		if ( pml.walking ) {
			pml.walking = qfalse;
			pml.groundPlane = qfalse;
			pm->ps->groundEntityNum = ENTITYNUM_NONE;
		}
		if ( pm->ps->velocity[2] < JETPACK_MAX_UP ) {
			pm->ps->velocity[2] += JETPACK_THRUST * pml.frametime;
			if ( pm->ps->velocity[2] > JETPACK_MAX_UP ) {
				pm->ps->velocity[2] = JETPACK_MAX_UP;
			}
		}
		pm->ps->eFlags |= EF_JETTING;
	}
#endif

#ifdef MISSIONPACK
	if ( pm->ps->powerups[PW_INVULNERABILITY] ) {
		PM_InvulnerabilityMove();
	} else
#endif
	if ( pm->ps->powerups[PW_FLIGHT] ) {
		// flight powerup doesn't allow jump and has different friction
		PM_FlyMove();
	} else if ( zipping ) {
		PM_ZipMove();
	} else if (pm->ps->pm_flags & PMF_GRAPPLE_PULL) {
		PM_GrappleMove();
		// We can wiggle a bit
		PM_AirMove();
	} else if (pm->ps->pm_flags & PMF_TIME_WATERJUMP) {
		PM_WaterJumpMove();
	} else if ( pm->waterlevel > 1 ) {
		// swimming
		PM_WaterMove();
	} else if ( pml.walking ) {
		// walking on ground
		if ( RM_FLAGS2( pm->ps ) & RMS2_SLIDING ) {
			PM_SlideGroundMove();
		} else {
			PM_WalkMove();
		}
	} else {
		// airborne
		PM_AirMove();
	}

	PM_Animate();

	// set groundentity, watertype, and waterlevel
	PM_GroundTrace();
	PM_SetWaterLevel();

	// weapons
	PM_Weapon();

	// torso animation
	PM_TorsoAnimation();

	// footstep events / legs animations
	PM_Footsteps();

	// entering / leaving water splashes
	PM_WaterEvents();

	// snap some parts of playerstate to save network bandwidth
	trap_SnapVector( pm->ps->velocity );
}


/*
================
Pmove

Can be called by either the server or the client
================
*/
void Pmove (pmove_t *pmove) {
	int			finalTime;

	finalTime = pmove->cmd.serverTime;

	if ( finalTime < pmove->ps->commandTime ) {
		return;	// should not happen
	}

	if ( finalTime > pmove->ps->commandTime + 1000 ) {
		pmove->ps->commandTime = finalTime - 1000;
	}

	pmove->ps->pmove_framecount = (pmove->ps->pmove_framecount+1) & ((1<<PS_PMOVEFRAMECOUNTBITS)-1);

	// chop the move up if it is too long, to prevent framerate
	// dependent behavior
	while ( pmove->ps->commandTime != finalTime ) {
		int		msec;

		msec = finalTime - pmove->ps->commandTime;

		if ( pmove->pmove_fixed ) {
			if ( msec > pmove->pmove_msec ) {
				msec = pmove->pmove_msec;
			}
		}
		else {
			if ( msec > 66 ) {
				msec = 66;
			}
		}
		pmove->cmd.serverTime = pmove->ps->commandTime + msec;
		PmoveSingle( pmove );

		if ( pmove->ps->pm_flags & PMF_JUMP_HELD ) {
			pmove->cmd.upmove = 20;
		}
	}

	//PM_CheckStuck();

}

