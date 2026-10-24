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
// cg_zipline.c -- rearmed: zip line effects. The taut cable from the hand to the anchor,
// the anchor spark, the cable whoosh, and the first person wind streaks / FOV push.
//
// Who is riding: our own player from the predicted playerState (RMZ_ZIPPING in loopSound,
// anchor in grapplePoint); everyone else from the entityState (RM_BODY_ZIP in generic1,
// anchor in origin2, set by BG_PlayerStateToEntityState).

#include "cg_local.h"

#define ZIP_SHOOT_TIME		70		// msec for the cable to fly out to the anchor
#define ZIP_FOV_PUNCH		4.0f	// degrees at the attach
#define ZIP_FOV_RIDE		2.5f	// degrees held while riding (on top of the speed FOV)

static sfxHandle_t	zipFireSound, zipAttachSound, zipMissSound, zipReleaseSound, zipLoopSound;

static int		zipFireTime[MAX_CLIENTS];	// cg.time of the last EV_ZIP_FIRE
static int		zipEndTime[MAX_CLIENTS];	// ... EV_ZIP_END
static vec3_t	zipLastAnchor[MAX_CLIENTS];
static vec3_t	zipLastHand[MAX_CLIENTS];
static int		zipWindNext;
static int		zipFovPunchTime;
static float	zipFovRide;

// our own presses: why the last one didn't hold (HUD hint), and the anchor marker
#define ZIP_HINT_TIME		1300
static int		zipMissTime;
static int		zipMissReason;
static qboolean	zipAnchorReady;			// this frame: a press would hold
static int		zipReadySince;			// cg.time the marker came on (fade in)

void CG_ZipRegister( void ) {
	zipFireSound = trap_S_RegisterSound( "sound/rearmed/zip/zip_fire.wav", qfalse );
	zipAttachSound = trap_S_RegisterSound( "sound/rearmed/zip/zip_attach.wav", qfalse );
	zipMissSound = trap_S_RegisterSound( "sound/rearmed/zip/zip_miss.wav", qfalse );
	zipReleaseSound = trap_S_RegisterSound( "sound/rearmed/zip/zip_release.wav", qfalse );
	zipLoopSound = trap_S_RegisterSound( "sound/rearmed/zip/zip_loop.wav", qfalse );
	memset( zipFireTime, 0, sizeof( zipFireTime ) );
	memset( zipEndTime, 0, sizeof( zipEndTime ) );
	zipWindNext = 0;
	zipFovPunchTime = 0;
	zipFovRide = 0;
	zipMissTime = 0;
	zipMissReason = 0;
	zipAnchorReady = qfalse;
	zipReadySince = 0;
}

/*
==================
CG_ZipIsView

The player whose eyes we look through (ourselves, or whoever we follow)
==================
*/
static qboolean CG_ZipIsView( int clientNum ) {
	return cg.snap && clientNum == cg.snap->ps.clientNum;
}

/*
==================
CG_ZipState

Is this client riding a zip line, and where is the anchor
==================
*/
static qboolean CG_ZipState( int clientNum, vec3_t anchor ) {
	centity_t	*cent;

	if ( CG_ZipIsView( clientNum ) ) {
		if ( !( cg.predictedPlayerState.loopSound & RMZ_ZIPPING ) || cg.predictedPlayerState.pm_type != PM_NORMAL ) {
			return qfalse;
		}
		VectorCopy( cg.predictedPlayerState.grapplePoint, anchor );
		return qtrue;
	}
	cent = &cg_entities[clientNum];
	if ( !cent->currentValid || cent->currentState.eType != ET_PLAYER || ( cent->currentState.eFlags & EF_DEAD )
		|| !( cent->currentState.generic1 & RM_BODY_ZIP ) ) {
		return qfalse;
	}
	VectorCopy( cent->currentState.origin2, anchor );
	return qtrue;
}

/*
==================
CG_ZipHand

Where the cable leaves the player: under the gun in first person, the chest otherwise
==================
*/
static void CG_ZipHand( int clientNum, vec3_t hand ) {
	centity_t	*cent;
	vec3_t		fwd, right, up;

	if ( CG_ZipIsView( clientNum ) && !cg.renderingThirdPerson ) {
		VectorCopy( cg.refdef.vieworg, hand );
		VectorMA( hand, 18, cg.refdef.viewaxis[0], hand );
		VectorMA( hand, -7, cg.refdef.viewaxis[1], hand );		// axis[1] points left
		VectorMA( hand, -9, cg.refdef.viewaxis[2], hand );
		return;
	}
	if ( CG_ZipIsView( clientNum ) ) {
		VectorCopy( cg.predictedPlayerEntity.lerpOrigin, hand );
		AngleVectors( cg.predictedPlayerEntity.lerpAngles, fwd, right, up );
	} else {
		cent = &cg_entities[clientNum];
		VectorCopy( cent->lerpOrigin, hand );
		AngleVectors( cent->lerpAngles, fwd, right, up );
	}
	hand[2] += 18;
	VectorMA( hand, 12, fwd, hand );
	VectorMA( hand, 6, right, hand );
}

/*
==================
Small local entities
==================
*/
static void CG_ZipSpark( const vec3_t org, const vec3_t vel, float r, float g, float b, int life, qboolean ember,
		float size, qboolean straight ) {
	localEntity_t	*le;

	le = CG_AllocLocalEntity();
	le->leType = LE_FX_SPARK;
	le->leFlags = ember ? LEF_FX_EMBER : 0;
	le->startTime = cg.time;
	le->endTime = cg.time + life;
	le->lifeRate = 1.0f / life;
	le->bounceFactor = straight ? 0 : 0.3f;
	le->radius = size;
	VectorSet( le->color, r, g, b );
	le->color[3] = 1.0f;
	le->pos.trType = straight ? TR_LINEAR : TR_GRAVITY;
	le->pos.trTime = cg.time;
	VectorCopy( org, le->pos.trBase );
	VectorCopy( vel, le->pos.trDelta );
	VectorCopy( org, le->refEntity.origin );
	le->refEntity.customShader = ember ? cgs.media.fxGlowShader : cgs.media.fxSparkShader;
}

static void CG_ZipFlash( const vec3_t org, float r, float g, float b, float radius, float endRadius, int life ) {
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

/*
==================
CG_ZipCable

The taut cable: a bright core with a soft glow, and a glow knot at each end
==================
*/
static void CG_ZipCable( const vec3_t hand, const vec3_t anchor, float alpha ) {
	refEntity_t	beam, glow;
	float		pulse;

	if ( alpha <= 0 ) {
		return;
	}
	memset( &beam, 0, sizeof( beam ) );
	beam.reType = RT_RAIL_CORE;
	beam.customShader = cgs.media.railCoreShader;
	VectorCopy( hand, beam.origin );
	VectorCopy( anchor, beam.oldorigin );
	AxisClear( beam.axis );
	beam.shaderTime = cg.time / 1000.0f;
	beam.shaderRGBA[0] = 90 * alpha;
	beam.shaderRGBA[1] = 210 * alpha;
	beam.shaderRGBA[2] = 255 * alpha;
	beam.shaderRGBA[3] = 255 * alpha;
	trap_R_AddRefEntityToScene( &beam );

	// the anchor knot pulses a little
	pulse = 0.8f + 0.2f * sin( cg.time * 0.025f );
	memset( &glow, 0, sizeof( glow ) );
	glow.reType = RT_SPRITE;
	glow.customShader = cgs.media.fxGlowShader;
	VectorCopy( anchor, glow.origin );
	glow.radius = 9 * pulse;
	glow.shaderRGBA[0] = 120 * alpha;
	glow.shaderRGBA[1] = 220 * alpha;
	glow.shaderRGBA[2] = 255 * alpha;
	glow.shaderRGBA[3] = 255;
	trap_R_AddRefEntityToScene( &glow );
	VectorCopy( hand, glow.origin );
	glow.radius = 3.5f;
	trap_R_AddRefEntityToScene( &glow );
}

/*
==================
CG_ZipEvent

EV_ZIP_FIRE / EV_ZIP_MISS / EV_ZIP_END (cg_event.c)
==================
*/
void CG_ZipEvent( centity_t *cent, int event, int parm ) {
	entityState_t	*es = &cent->currentState;
	int				clientNum = es->clientNum;
	vec3_t			anchor, hand, dir, vel, pos;
	int				i;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}
	switch ( event ) {
	case EV_ZIP_FIRE:
		zipFireTime[clientNum] = cg.time;
		trap_S_StartSound( NULL, es->number, CHAN_AUTO, zipFireSound );
		if ( !CG_ZipState( clientNum, anchor ) ) {
			// not in this snapshot's state (yet): rebuild the anchor from the view and the distance
			if ( CG_ZipIsView( clientNum ) ) {
				VectorCopy( cg.predictedPlayerState.origin, pos );
				pos[2] += cg.predictedPlayerState.viewheight;
				AngleVectors( cg.predictedPlayerState.viewangles, dir, NULL, NULL );
			} else {
				VectorCopy( cent->lerpOrigin, pos );
				pos[2] += DEFAULT_VIEWHEIGHT;
				AngleVectors( cent->lerpAngles, dir, NULL, NULL );
			}
			VectorMA( pos, parm * 16 + 8, dir, anchor );
		}
		VectorCopy( anchor, zipLastAnchor[clientNum] );
		trap_S_StartSound( anchor, ENTITYNUM_WORLD, CHAN_AUTO, zipAttachSound );
		// sparks off the anchor, back towards the shooter
		CG_ZipHand( clientNum, hand );
		VectorSubtract( hand, anchor, dir );
		VectorNormalize( dir );
		VectorMA( anchor, 2, dir, pos );
		CG_ZipFlash( pos, 0.5f, 0.85f, 1.0f, 26, 4, 260 );
		for ( i = 0 ; i < 10 ; i++ ) {
			VectorScale( dir, 120 + random() * 160, vel );
			vel[0] += crandom() * 140;
			vel[1] += crandom() * 140;
			vel[2] += crandom() * 140 + 60;
			CG_ZipSpark( pos, vel, 0.7f + random() * 0.3f, 0.85f, 1.0f, 300 + rand() % 300, ( i & 3 ) == 0, 1.6f, qfalse );
		}
		if ( CG_ZipIsView( clientNum ) && !cg.renderingThirdPerson ) {
			zipFovPunchTime = cg.time;
		}
		break;

	case EV_ZIP_MISS:
		trap_S_StartSound( NULL, es->number, CHAN_AUTO, zipMissSound );
		if ( CG_ZipIsView( clientNum ) ) {
			// say why (CG_ZipDrawHud), and a little fizzle at the hand
			zipMissTime = cg.time;
			zipMissReason = parm;
			if ( !cg.renderingThirdPerson && parm != ZIPR_LOCKED ) {
				CG_ZipHand( clientNum, hand );
				for ( i = 0 ; i < 5 ; i++ ) {
					VectorScale( cg.refdef.viewaxis[0], 60 + random() * 60, vel );
					vel[0] += crandom() * 50;
					vel[1] += crandom() * 50;
					vel[2] += crandom() * 50 + 30;
					CG_ZipSpark( hand, vel, 0.6f, 0.75f, 0.85f, 160 + rand() % 120, qfalse, 0.8f, qfalse );
				}
			}
		}
		break;

	case EV_ZIP_END:
		zipEndTime[clientNum] = cg.time;
		trap_S_StartSound( NULL, es->number, CHAN_AUTO, zipReleaseSound );
		break;
	}
}

/*
==================
CG_ZipWind

First person wind streaks rushing past while riding
==================
*/
static void CG_ZipWind( const vec3_t anchor ) {
	vec3_t	dir, side, up, pos, vel;
	float	speed, ang, rad;
	int		i;

	if ( cg.time < zipWindNext || cg.renderingThirdPerson ) {
		return;
	}
	zipWindNext = cg.time + 18;
	speed = VectorLength( cg.predictedPlayerState.velocity );
	if ( speed < 300 ) {
		return;
	}
	VectorSubtract( anchor, cg.refdef.vieworg, dir );
	if ( VectorNormalize( dir ) < 160 ) {
		return;
	}
	PerpendicularVector( side, dir );
	CrossProduct( dir, side, up );
	for ( i = 0 ; i < 2 ; i++ ) {
		ang = random() * M_PI * 2;
		rad = 45 + random() * 110;
		VectorMA( cg.refdef.vieworg, 140 + random() * 220, dir, pos );
		VectorMA( pos, cos( ang ) * rad, side, pos );
		VectorMA( pos, sin( ang ) * rad, up, pos );
		VectorScale( dir, -( 700 + random() * 300 ), vel );
		CG_ZipSpark( pos, vel, 0.42f, 0.5f, 0.58f, 160 + rand() % 120, qfalse, 0.7f, qtrue );
	}
}

/*
==================
CG_ZipCheckAnchor

Would a press hold right now (the same aim test pmove makes, BG_ZipAim)
==================
*/
static void CG_ZipCheckAnchor( void ) {
	playerState_t	*ps = &cg.predictedPlayerState;
	vec3_t			eye, fwd, anchor;
	float			dist;
	qboolean		ready = qfalse;

	if ( ps->pm_type == PM_NORMAL && ps->stats[STAT_HEALTH] > 0 && !( ps->pm_flags & PMF_FOLLOW )
		&& ps->persistant[PERS_TEAM] != TEAM_SPECTATOR && ( RM_FLAGS2( ps ) & RMF2_ZIPLINE )
		&& !( ps->loopSound & ( RMZ_ZIPPING | RMZ_USED ) ) && !ps->powerups[PW_FLIGHT] ) {
		VectorCopy( ps->origin, eye );
		eye[2] += ps->viewheight;
		AngleVectors( ps->viewangles, fwd, NULL, NULL );
		ready = BG_ZipAim( CG_Trace, eye, fwd, ps->clientNum, anchor, &dist ) == ZIPR_OK;
	}
	if ( ready && !zipAnchorReady ) {
		zipReadySince = cg.time;
	}
	zipAnchorReady = ready;
}

qboolean CG_ZipAnchorReady( void ) {
	return zipAnchorReady;
}

/*
==================
CG_ZipDrawHud

Under the crosshair (screen pixels, cg_rearmed.c): two small cyan brackets while a zip
press would hold, and after a press that didn't, a one-line reason. cg_zipHud 0 hides both.
==================
*/
void CG_ZipDrawHud( float cx, float cy ) {
	vec4_t		col;
	float		vh = cgs.glconfig.vidHeight, r, h, t, tick, a, x;
	const char	*s, *key;
	int			i, dt;

	if ( !cg_zipHud.integer || cg.predictedPlayerState.stats[STAT_HEALTH] <= 0 ) {
		return;
	}
	if ( zipAnchorReady ) {
		a = CG_Clamp01( ( cg.time - zipReadySince ) / 120.0f ) * 0.75f;
		r = vh * 0.03f;
		h = vh * 0.018f;
		t = vh * 0.0022f;
		if ( t < 1 ) {
			t = 1;
		}
		tick = vh * 0.006f;
		col[0] = 0.45f; col[1] = 0.9f; col[2] = 1.0f; col[3] = a;
		trap_R_SetColor( col );
		for ( i = -1 ; i <= 1 ; i += 2 ) {
			x = cx + i * r;
			trap_R_DrawStretchPic( x - t * 0.5f, cy - h * 0.5f, t, h, 0, 0, 0, 0, cgs.media.whiteShader );
			// the ticks point inwards: [ ]
			trap_R_DrawStretchPic( i < 0 ? x : x - tick, cy - h * 0.5f, tick, t, 0, 0, 0, 0, cgs.media.whiteShader );
			trap_R_DrawStretchPic( i < 0 ? x : x - tick, cy + h * 0.5f - t, tick, t, 0, 0, 0, 0, cgs.media.whiteShader );
		}
		trap_R_SetColor( NULL );
	}

	dt = cg.time - zipMissTime;
	if ( !zipMissTime || dt < 0 || dt >= ZIP_HINT_TIME ) {
		return;
	}
	switch ( zipMissReason ) {
	case ZIPR_CLOSE:
		s = "ZIP: TOO CLOSE";
		break;
	case ZIPR_USED:
		s = "ZIP: LAND TO RELOAD";
		break;
	case ZIPR_LOCKED:
		if ( cgs.rmMode == RM_MODE_ROGUELIKE ) {
			s = "ZIP LINE: GET THE CARD FIRST";
		} else {
			key = CG_KeyForCommand( "rearmed" );
			s = va( "ZIP LINE IS OFF (%s > MOVEMENT)", key ? key : "F10" );
		}
		break;
	default:
		s = "ZIP: NO WALL IN RANGE";
		break;
	}
	a = dt < ZIP_HINT_TIME - 300 ? 1.0f : ( ZIP_HINT_TIME - dt ) / 300.0f;
	col[0] = 0.75f; col[1] = 0.88f; col[2] = 1.0f; col[3] = 0.85f * a;
	CG_HudAnchor( 0 );
	CG_DrawStringExt( 320 - CG_DrawStrlen( s ) * 5 * 0.5f, 240 + 30, s, col, qtrue, qfalse, 5, 8, 0 );
}

/*
==================
CG_ZipFrame

Every frame: the cables of everyone riding, the whoosh loop, our wind. After CG_CalcViewValues.
==================
*/
void CG_ZipFrame( void ) {
	vec3_t	anchor, hand, end, vel;
	float	f, alpha;
	int		i, t;

	if ( !cg.snap ) {
		return;
	}
	CG_ZipCheckAnchor();
	for ( i = 0 ; i < MAX_CLIENTS ; i++ ) {
		if ( zipFireTime[i] > cg.time || zipEndTime[i] > cg.time ) {
			zipFireTime[i] = zipEndTime[i] = 0;		// a new level / time went back
		}
		if ( !CG_ZipState( i, anchor ) ) {
			// a quick fade of the cable after letting go
			t = cg.time - zipEndTime[i];
			if ( zipEndTime[i] && t < 120 && zipEndTime[i] >= zipFireTime[i] ) {
				alpha = 1.0f - t / 120.0f;
				CG_ZipCable( zipLastHand[i], zipLastAnchor[i], alpha * 0.7f );
			}
			continue;
		}
		CG_ZipHand( i, hand );
		VectorCopy( anchor, zipLastAnchor[i] );
		VectorCopy( hand, zipLastHand[i] );

		// the cable flies out over the first few frames
		t = cg.time - zipFireTime[i];
		f = ( zipFireTime[i] && t >= 0 && t < ZIP_SHOOT_TIME ) ? (float)t / ZIP_SHOOT_TIME : 1.0f;
		if ( f < 0.15f ) {
			f = 0.15f;
		}
		VectorSubtract( anchor, hand, end );
		VectorMA( hand, f, end, end );
		CG_ZipCable( hand, end, 1.0f );

		// whoosh along the cable
		if ( CG_ZipIsView( i ) ) {
			VectorCopy( cg.predictedPlayerState.velocity, vel );
			trap_S_AddLoopingSound( i, cg.predictedPlayerState.origin, vel, zipLoopSound );
			CG_ZipWind( anchor );
		} else {
			VectorCopy( cg_entities[i].currentState.pos.trDelta, vel );
			trap_S_AddLoopingSound( i, cg_entities[i].lerpOrigin, vel, zipLoopSound );
		}
	}
}

/*
==================
CG_ZipFov

A small push at the attach, a little extra width while riding (cg_view.c CG_CalcFov,
scaled by cg_speedFov like the other movement FOV effects)
==================
*/
float CG_ZipFov( float fov ) {
	float	scale, target, step, f;
	int		t;

	scale = cg_speedFov.value;
	if ( scale <= 0 || cg.zoomed || cg.renderingThirdPerson ) {
		zipFovRide = 0;
		return fov;
	}
	if ( scale > 2 ) {
		scale = 2;
	}
	target = ( ( cg.predictedPlayerState.loopSound & RMZ_ZIPPING ) && cg.predictedPlayerState.pm_type == PM_NORMAL )
		? ZIP_FOV_RIDE : 0;
	step = cg.frametime * 0.005f;
	if ( step > 1 ) {
		step = 1;
	} else if ( step < 0 ) {
		step = 0;
	}
	zipFovRide += ( target - zipFovRide ) * step;

	f = 0;
	t = cg.time - zipFovPunchTime;
	if ( zipFovPunchTime && t >= 0 && t < 400 ) {
		if ( t < 50 ) {
			f = t / 50.0f;
		} else {
			f = 1.0f - ( t - 50 ) / 350.0f;
			f *= f;
		}
	}
	fov += ( zipFovRide + ZIP_FOV_PUNCH * f ) * scale;
	if ( fov > 170 ) {
		fov = 170;
	}
	return fov;
}
