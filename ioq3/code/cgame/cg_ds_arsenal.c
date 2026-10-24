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
// cg_ds_arsenal.c -- DESCENT v2 ARSENAL visuals: evolved / level-8 missiles, impacts,
// EV_DS_FX effects, the evolved weapon glow, layered fire sounds, and the weapon level
// pips beside the ammo (level-up flash off-centre, never mid-screen).
// The player's levels come from CS_DESCENTGROW (parsed by cg_ds_loot.c).

#include "cg_local.h"

static struct {
	qhandle_t	shell;			// evolved weapon shell (rgbGen entity)
	qhandle_t	voidShader;		// darkening sprite (black hole core)
	sfxHandle_t	levelup, evolve, bullet, flame, rocket, storm, holy, star, voidSnd, claw, blast, ping;

	int			flashTime;		// last level-up / evolution (HUD)
	int			flashWeapon;
	int			flashLevel;
	int			flashEvo;		// -1 = a level-up
} arsc;

static const float arscEvoColors[EVO_NUM][3] = {
	{ 1.0f, 0.12f, 0.16f },		// HEARTRIPPER
	{ 1.0f, 0.82f, 0.25f },		// BULLET HELL
	{ 1.0f, 0.45f, 0.10f },		// DRAGON'S BREATH
	{ 1.0f, 0.75f, 0.15f },		// CLUSTER BOMB
	{ 1.0f, 0.22f, 0.06f },		// HELLFIRE
	{ 0.70f, 0.45f, 1.0f },		// STORM CALLER
	{ 1.0f, 0.93f, 0.60f },		// JUDGEMENT
	{ 0.65f, 0.95f, 1.0f },		// STAR FURNACE
	{ 0.62f, 0.15f, 1.0f },		// DOOMSDAY
	{ 0.55f, 0.75f, 1.0f },		// IRON MAIDEN
	{ 1.0f, 0.28f, 0.18f },		// MINEFIELD
	{ 1.0f, 0.62f, 0.25f },		// RIPSAW
	{ 0.75f, 1.0f, 0.22f }		// ARMAGEDDON
};
static const vec3_t arscLevel8 = { 1.0f, 0.72f, 0.28f };
static const vec3_t arscUp = { 0, 0, 1 };

void CG_DS_ArsenalRegister( void ) {
	memset( &arsc, 0, sizeof( arsc ) );
	arsc.flashEvo = -1;
	arsc.shell = trap_R_RegisterShader( "rearmed/dsEvolvedShell" );
	arsc.voidShader = trap_R_RegisterShader( "rearmed/dsVoid" );
	arsc.levelup = trap_S_RegisterSound( "sound/rearmed/ds/ds_levelup.wav", qfalse );
	arsc.evolve = trap_S_RegisterSound( "sound/rearmed/ds/ds_evolve.wav", qfalse );
	arsc.bullet = trap_S_RegisterSound( "sound/rearmed/ds/ds_bullet.wav", qfalse );
	arsc.flame = trap_S_RegisterSound( "sound/rearmed/ds/ds_flame.wav", qfalse );
	arsc.rocket = trap_S_RegisterSound( "sound/rearmed/ds/ds_rocket.wav", qfalse );
	arsc.storm = trap_S_RegisterSound( "sound/rearmed/ds/ds_storm.wav", qfalse );
	arsc.holy = trap_S_RegisterSound( "sound/rearmed/ds/ds_holy.wav", qfalse );
	arsc.star = trap_S_RegisterSound( "sound/rearmed/ds/ds_star.wav", qfalse );
	arsc.voidSnd = trap_S_RegisterSound( "sound/rearmed/ds/ds_void.wav", qfalse );
	arsc.claw = trap_S_RegisterSound( "sound/rearmed/ds/ds_claw.wav", qfalse );
	arsc.blast = trap_S_RegisterSound( "sound/rearmed/ds/ds_blast.wav", qfalse );
	arsc.ping = trap_S_RegisterSound( "sound/rearmed/ds/ds_ping.wav", qfalse );
}

/*
===============================================================================

HELPERS

===============================================================================
*/

// the evolution of a weapon for the local player (from the grow configstring), -1 none
static int Arsc_LocalEvo( int w ) {
	int		mask = CG_DS_EvolvedMask(), e;

	if ( w == WP_ROCKET_LAUNCHER && ( mask & ( 1 << EVO_ARMAGEDDON ) ) ) {
		return EVO_ARMAGEDDON;
	}
	e = BG_DS_EvolutionForWeapon( w );
	return ( e >= 0 && ( mask & ( 1 << e ) ) ) ? e : -1;
}

// the evolution shown on a player's weapon: the local player knows which one, others by the glow bit
static int Arsc_PlayerEvo( int clientNum, int w, int generic1 ) {
	if ( !( generic1 & RM_BODY_EVOLVED ) ) {
		return -1;
	}
	if ( cg.snap && clientNum == cg.snap->ps.clientNum ) {
		return Arsc_LocalEvo( w );
	}
	return BG_DS_EvolutionForWeapon( w );
}

static int Arsc_MissileEvo( entityState_t *s ) {
	if ( !( s->generic1 & DSM_EVOLVED ) ) {
		return -1;
	}
	if ( s->weapon == WP_ROCKET_LAUNCHER && ( s->generic1 & DSM_UNION ) ) {
		return EVO_ARMAGEDDON;
	}
	return BG_DS_EvolutionForWeapon( s->weapon );
}

static void Arsc_Color( int evo, vec3_t out ) {
	if ( evo >= 0 && evo < EVO_NUM ) {
		VectorCopy( arscEvoColors[evo], out );
	} else {
		VectorCopy( arscLevel8, out );
	}
}

static void Arsc_Sprite( const vec3_t org, float radius, const vec3_t color, float alpha, qhandle_t shader ) {
	refEntity_t	re;

	memset( &re, 0, sizeof( re ) );
	re.reType = RT_SPRITE;
	VectorCopy( org, re.origin );
	VectorCopy( org, re.oldorigin );
	re.radius = radius;
	re.customShader = shader ? shader : cgs.media.fxGlowShader;
	re.shaderRGBA[0] = 255 * CG_Clamp01( color[0] * alpha );
	re.shaderRGBA[1] = 255 * CG_Clamp01( color[1] * alpha );
	re.shaderRGBA[2] = 255 * CG_Clamp01( color[2] * alpha );
	re.shaderRGBA[3] = 255;
	trap_R_AddRefEntityToScene( &re );
}

static void Arsc_Flash( const vec3_t org, const vec3_t color, float radius, float endRadius, int life ) {
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

static void Arsc_Ring( const vec3_t org, const vec3_t normal, const vec3_t color, float endRadius, int life ) {
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

// a weightless glowing particle on a straight line (inward swirl, rising motes)
static void Arsc_Mote( const vec3_t org, const vec3_t vel, const vec3_t color, int life, float size ) {
	localEntity_t	*le;

	le = CG_AllocLocalEntity();
	le->leType = LE_FX_SPARK;
	le->leFlags = LEF_FX_EMBER;
	le->startTime = cg.time;
	le->endTime = cg.time + life;
	le->lifeRate = 1.0f / life;
	le->bounceFactor = 0;
	le->radius = size;
	VectorCopy( color, le->color );
	le->color[3] = 1.0f;
	le->pos.trType = TR_LINEAR;
	le->pos.trTime = cg.time;
	VectorCopy( org, le->pos.trBase );
	VectorCopy( vel, le->pos.trDelta );
	VectorCopy( org, le->refEntity.origin );
	le->refEntity.customShader = cgs.media.fxGlowShader;
}

static void Arsc_Beam( const vec3_t a, const vec3_t b, const vec3_t color, int life ) {
	localEntity_t	*le;

	le = CG_AllocLocalEntity();
	le->leType = LE_FADE_RGB;
	le->startTime = cg.time;
	le->endTime = cg.time + life;
	le->lifeRate = 1.0f / life;
	VectorCopy( color, le->color );
	le->color[3] = 1.0f;
	le->refEntity.reType = RT_RAIL_CORE;
	le->refEntity.customShader = cgs.media.fxSparkShader;
	VectorCopy( a, le->refEntity.origin );
	VectorCopy( b, le->refEntity.oldorigin );
}

// a jagged bolt a -> b: segs pieces bent sideways by up to jitter units (life 0 = this frame only)
static void Arsc_Bolt( const vec3_t a, const vec3_t b, const vec3_t color, int segs, float jitter, int life ) {
	vec3_t		dir, right, up, p0, p1;
	float		len, f;
	int			i;
	refEntity_t	re;

	VectorSubtract( b, a, dir );
	len = VectorNormalize( dir );
	if ( len < 1 ) {
		return;
	}
	PerpendicularVector( right, dir );
	CrossProduct( dir, right, up );
	VectorCopy( a, p0 );
	for ( i = 1 ; i <= segs ; i++ ) {
		f = (float)i / segs;
		VectorMA( a, len * f, dir, p1 );
		if ( i < segs ) {
			VectorMA( p1, crandom() * jitter, right, p1 );
			VectorMA( p1, crandom() * jitter, up, p1 );
		}
		if ( life > 0 ) {
			Arsc_Beam( p0, p1, color, life );
		} else {
			memset( &re, 0, sizeof( re ) );
			re.reType = RT_RAIL_CORE;
			re.customShader = cgs.media.fxSparkShader;
			VectorCopy( p0, re.origin );
			VectorCopy( p1, re.oldorigin );
			re.shaderRGBA[0] = 255 * color[0];
			re.shaderRGBA[1] = 255 * color[1];
			re.shaderRGBA[2] = 255 * color[2];
			re.shaderRGBA[3] = 255;
			trap_R_AddRefEntityToScene( &re );
		}
		VectorCopy( p1, p0 );
	}
}

// where a player's shots start (the local player: a little right of and below the eye)
static qboolean Arsc_Muzzle( int num, vec3_t out ) {
	centity_t	*cent;
	vec3_t		fwd;

	if ( cg.snap && num == cg.snap->ps.clientNum ) {
		VectorCopy( cg.predictedPlayerState.origin, out );
		out[2] += cg.predictedPlayerState.viewheight;
		AngleVectors( cg.predictedPlayerState.viewangles, fwd, NULL, NULL );
		VectorMA( out, 14, fwd, out );
		if ( !cg.renderingThirdPerson ) {
			VectorMA( out, -6, cg.refdef.viewaxis[1], out );
			VectorMA( out, -5, cg.refdef.viewaxis[2], out );
		}
		return qtrue;
	}
	if ( num < 0 || num >= MAX_CLIENTS ) {
		return qfalse;
	}
	cent = &cg_entities[num];
	if ( !cent->currentValid ) {
		return qfalse;
	}
	VectorCopy( cent->lerpOrigin, out );
	out[2] += DEFAULT_VIEWHEIGHT;
	AngleVectors( cent->lerpAngles, fwd, NULL, NULL );
	VectorMA( out, 14, fwd, out );
	return qtrue;
}

/*
===============================================================================

MISSILES

===============================================================================
*/

static void Arsc_SmallModel( centity_t *cent, int weapon, float scale ) {
	refEntity_t	ent;
	int			i;

	memset( &ent, 0, sizeof( ent ) );
	VectorCopy( cent->lerpOrigin, ent.origin );
	VectorCopy( cent->lerpOrigin, ent.oldorigin );
	ent.hModel = cg_weapons[weapon].missileModel;
	ent.renderfx = RF_NOSHADOW;
	if ( VectorNormalize2( cent->currentState.pos.trDelta, ent.axis[0] ) == 0 ) {
		ent.axis[0][2] = 1;
	}
	RotateAroundDirection( ent.axis, cg.time / 3 );
	for ( i = 0 ; i < 3 ; i++ ) {
		VectorScale( ent.axis[i], scale, ent.axis[i] );
	}
	ent.nonNormalizedAxes = qtrue;
	trap_R_AddRefEntityToScene( &ent );
}

/*
=================
CG_DS_Missile

From CG_Missile after the stock glow. qtrue = the missile is fully drawn here.
=================
*/
qboolean CG_DS_Missile( centity_t *cent ) {
	entityState_t	*s1 = &cent->currentState;
	int				g = s1->generic1, evo;
	vec3_t			col, vel, org, d;
	float			pulse, age, f;
	int				i;

	if ( !( g & ( DSM_EVOLVED | DSM_SPECIAL | DSM_SMALL ) ) ) {
		return qfalse;
	}
	evo = Arsc_MissileEvo( s1 );
	Arsc_Color( evo, col );
	pulse = 0.8f + 0.2f * sin( cg.time * 0.02f + s1->number );
	BG_EvaluateTrajectoryDelta( &s1->pos, cg.time, vel );

	switch ( s1->weapon ) {
	case WP_PLASMAGUN:
		if ( g & DSM_SMALL ) {
			Arsc_Sprite( cent->lerpOrigin, 7, col, 1.0f, cgs.media.plasmaBallShader );
			Arsc_Sprite( cent->lerpOrigin, 14, col, 0.8f, 0 );
			return qtrue;
		}
		if ( evo >= 0 ) {
			// STAR FURNACE: a white-hot star with a glittering tail
			Arsc_Sprite( cent->lerpOrigin, 15, col, 1.0f, cgs.media.plasmaBallShader );
			Arsc_Sprite( cent->lerpOrigin, 30 * pulse, col, 0.9f, 0 );
			trap_R_AddLightToScene( cent->lerpOrigin, 140, col[0], col[1], col[2] );
			if ( cg_fx.integer && cg.time >= cent->dsFxTime ) {
				cent->dsFxTime = cg.time + 35;
				VectorScale( vel, -0.08f, d );
				d[0] += crandom() * 30;
				d[1] += crandom() * 30;
				d[2] += crandom() * 30;
				Arsc_Mote( cent->lerpOrigin, d, col, 260, 3 );
			}
			return qtrue;
		}
		// level 8: the bolt swells as it flies
		age = cg.time - s1->pos.trTime;
		f = 1.0f + 0.35f * CG_Clamp01( age / 700.0f );
		Arsc_Sprite( cent->lerpOrigin, 16 * f, col, 1.0f, cgs.media.plasmaBallShader );
		Arsc_Sprite( cent->lerpOrigin, 18 * f, col, 0.5f, 0 );
		return qtrue;

	case WP_GRENADE_LAUNCHER:
		if ( g & DSM_SMALL ) {
			Arsc_SmallModel( cent, WP_GRENADE_LAUNCHER, 0.55f );
			Arsc_Sprite( cent->lerpOrigin, 9 * pulse, col, 1.0f, 0 );
			return qtrue;
		}
		Arsc_Sprite( cent->lerpOrigin, ( evo >= 0 ? 16 : 10 ) * pulse, col, 1.0f, 0 );
		if ( evo >= 0 ) {
			trap_R_AddLightToScene( cent->lerpOrigin, 120 * pulse, col[0], col[1], col[2] );
		}
		return qfalse;

	case WP_ROCKET_LAUNCHER:
		if ( evo < 0 ) {
			return qfalse;
		}
		// HELLFIRE / ARMAGEDDON: a coloured fire tail
		Arsc_Sprite( cent->lerpOrigin, 22 * pulse, col, 1.0f, 0 );
		trap_R_AddLightToScene( cent->lerpOrigin, 220, col[0], col[1], col[2] );
		if ( cg_fx.integer && cg.time >= cent->dsFxTime ) {
			cent->dsFxTime = cg.time + 30;
			VectorScale( vel, -0.1f, d );
			d[2] += 15;
			CG_GoreFire( cent->lerpOrigin, d, 6, 18, 320, col, 2, 25 );
		}
		return qfalse;

	case WP_BFG:
		if ( evo < 0 ) {
			Arsc_Sprite( cent->lerpOrigin, 28 * pulse, col, 0.9f, 0 );
			return qfalse;
		}
		// DOOMSDAY: a hole in the world with light falling into it
		Arsc_Sprite( cent->lerpOrigin, 34, col, 1.0f, arsc.voidShader );
		Arsc_Sprite( cent->lerpOrigin, 22, col, 1.0f, arsc.voidShader );
		Arsc_Sprite( cent->lerpOrigin, 64 * pulse, col, 0.75f, 0 );
		trap_R_AddLightToScene( cent->lerpOrigin, 260, col[0], col[1], col[2] );
		if ( cg_fx.integer && cg.time >= cent->dsFxTime ) {
			cent->dsFxTime = cg.time + 25;
			for ( i = 0 ; i < 2 ; i++ ) {
				do {
					VectorSet( d, crandom(), crandom(), crandom() );
				} while ( VectorLengthSquared( d ) > 1.0f || VectorLengthSquared( d ) < 0.1f );
				VectorNormalize( d );
				VectorMA( cent->lerpOrigin, 140, d, org );
				VectorScale( d, -420, d );
				VectorMA( d, 0.9f, vel, d );
				Arsc_Mote( org, d, col, 320, 4 );
			}
		}
		return qtrue;

#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case WP_NAILGUN:
		Arsc_Sprite( cent->lerpOrigin, 7, col, 1.0f, 0 );
		return qfalse;
	case WP_PROX_LAUNCHER:
		pulse = 0.6f + 0.4f * sin( cg.time * 0.012f + s1->number );
		Arsc_Sprite( cent->lerpOrigin, ( evo >= 0 ? 12 : 8 ) * pulse, col, 1.0f, 0 );
		return qfalse;
#endif
	default:
		return qfalse;
	}
}

/*
=================
CG_DS_MissileImpact

EV_MISSILE_HIT / MISS of an arsenal missile, after the stock explosion
=================
*/
void CG_DS_MissileImpact( entityState_t *es, vec3_t origin, vec3_t dir ) {
	int		g = es->generic1, evo;
	vec3_t	col;

	if ( !( g & ( DSM_EVOLVED | DSM_SPECIAL | DSM_SMALL ) ) ) {
		return;
	}
	evo = Arsc_MissileEvo( es );
	Arsc_Color( evo, col );
	if ( g & DSM_SMALL ) {
		if ( es->weapon == WP_PLASMAGUN ) {
			CG_FxSparks( origin, dir, col, 5, 260, 280, 0.3f, 1 );
		} else if ( cg_fx.integer ) {
			CG_FxExplosion( origin, dir, 0.4f, col, qfalse );
			CG_FxSound( origin, ENTITYNUM_WORLD, CHAN_AUTO, arsc.blast );
		}
		return;
	}
	if ( !cg_fx.integer ) {
		return;
	}
	switch ( es->weapon ) {
	case WP_ROCKET_LAUNCHER:
		if ( evo >= 0 ) {
			CG_FxExplosion( origin, dir, 0.8f, col, qtrue );
			Arsc_Ring( origin, dir, col, 150, 380 );
			CG_FxShake( origin, 3, 600 );
		}
		break;
	case WP_GRENADE_LAUNCHER:
		Arsc_Ring( origin, dir, col, evo >= 0 ? 170 : 110, 400 );
		CG_FxSparks( origin, dir, col, evo >= 0 ? 14 : 8, 420, 500, 0.3f, 3 );
		break;
	case WP_PLASMAGUN:
		Arsc_Flash( origin, col, 12, evo >= 0 ? 70 : 40, 220 );
		CG_FxSparks( origin, dir, col, evo >= 0 ? 10 : 4, 300, 300, 0.3f, 2 );
		break;
	case WP_BFG:
		if ( evo >= 0 ) {
			Arsc_Ring( origin, dir, col, 260, 500 );
		} else {
			CG_FxExplosion( origin, dir, 1.0f, col, qtrue );
		}
		break;
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case WP_PROX_LAUNCHER:
		Arsc_Ring( origin, dir, col, evo >= 0 ? 160 : 110, 420 );
		if ( evo >= 0 ) {
			CG_FxExplosion( origin, dir, 0.6f, col, qfalse );
		}
		break;
	case WP_NAILGUN:
		CG_FxSparks( origin, dir, col, 2, 200, 200, 0.4f, 0 );
		break;
#endif
	default:
		break;
	}
}

/*
===============================================================================

EV_DS_FX

===============================================================================
*/

void CG_DS_ArsenalEvent( centity_t *cent, vec3_t position ) {
	entityState_t	*es = &cent->currentState;
	vec3_t			col, start, a, b, dir, p, right;
	float			len, d, scale;
	int				evo, i, n;

	switch ( es->eventParm ) {
	case DSX_TRACER:
		Arsc_Color( es->generic1, col );
		if ( Arsc_Muzzle( es->otherEntityNum, start ) ) {
			Arsc_Beam( start, position, col, 70 );
		}
		break;
	case DSX_TWIN:
		Arsc_Color( EVO_BULLETHELL, col );
		if ( Arsc_Muzzle( es->otherEntityNum, start ) ) {
			VectorSubtract( position, start, dir );
			VectorNormalize( dir );
			PerpendicularVector( right, dir );
			CrossProduct( dir, arscUp, right );
			VectorNormalize( right );
			VectorMA( start, -4, right, a );
			VectorMA( start, 4, right, b );
			Arsc_Beam( a, position, col, 80 );
			Arsc_Beam( b, es->origin2, col, 80 );
		}
		Arsc_Flash( position, col, 3, 9, 90 );
		Arsc_Flash( es->origin2, col, 3, 9, 90 );
		break;
	case DSX_BLAST:
		switch ( es->weapon ) {
		case WP_RAILGUN:		VectorSet( col, 0.6f, 0.85f, 1.0f ); break;
		case WP_GAUNTLET:		VectorSet( col, 1.0f, 0.15f, 0.1f ); break;
		default:				VectorCopy( arscLevel8, col ); break;
		}
		scale = es->generic1 * 4 / 150.0f;
		if ( scale < 0.3f ) {
			scale = 0.3f;
		}
		if ( cg_fx.integer ) {
			CG_FxExplosion( position, arscUp, scale, col, scale > 0.6f );
			CG_FxShake( position, 2 * scale, 500 );
		}
		CG_FxSound( position, ENTITYNUM_WORLD, CHAN_AUTO, arsc.blast );
		break;
	case DSX_ARC:
		if ( es->generic1 ) {
			Arsc_Color( EVO_STORMCALLER, col );
			Arsc_Bolt( position, es->origin2, col, 6, 14, 140 );
			Arsc_Bolt( position, es->origin2, col, 4, 8, 90 );
			CG_FxSound( es->origin2, ENTITYNUM_WORLD, CHAN_AUTO, arsc.storm );
		} else {
			VectorSet( col, 0.5f, 0.75f, 1.0f );
			Arsc_Bolt( position, es->origin2, col, 5, 10, 110 );
		}
		VectorSubtract( position, es->origin2, dir );
		VectorNormalize( dir );
		CG_FxSparks( es->origin2, dir, col, 4, 220, 220, 0.2f, 0 );
		break;
	case DSX_CLAW:
		Arsc_Color( EVO_HEARTRIPPER, col );
		VectorSubtract( es->origin2, position, dir );
		VectorNormalize( dir );
		CrossProduct( dir, arscUp, right );
		VectorNormalize( right );
		for ( i = -1 ; i <= 1 ; i++ ) {
			VectorMA( position, 22, right, a );
			VectorMA( a, 10 + i * 9, arscUp, a );
			VectorMA( a, -6 * i, dir, a );
			VectorMA( position, -22, right, b );
			VectorMA( b, -10 + i * 9, arscUp, b );
			Arsc_Beam( a, b, col, 160 );
		}
		Arsc_Flash( position, col, 6, 26, 160 );
		CG_FxSparks( position, dir, col, 10, 320, 450, 0.4f, 0 );
		CG_FxSound( position, ENTITYNUM_WORLD, CHAN_AUTO, arsc.claw );
		break;
	case DSX_GOREBURST:
		Arsc_Color( EVO_HEARTRIPPER, col );
		CG_GibFX( position );
		Arsc_Flash( position, col, 20, 120, 280 );
		Arsc_Ring( position, arscUp, col, 140, 350 );
		CG_FxSound( position, ENTITYNUM_WORLD, CHAN_AUTO, arsc.blast );
		break;
	case DSX_JUDGEMENT:
		Arsc_Color( EVO_JUDGEMENT, col );
		Arsc_Beam( position, es->origin2, col, 420 );
		VectorSubtract( es->origin2, position, dir );
		len = VectorNormalize( dir );
		for ( n = 0, d = 96 ; d < len && n < 8 ; d += 160, n++ ) {
			VectorMA( position, d, dir, p );
			Arsc_Flash( p, col, 10, 70, 300 + n * 40 );
			if ( cg_fx.integer ) {
				CG_FxSparks( p, arscUp, col, 6, 300, 450, 0.2f, 2 );
				Arsc_Ring( p, dir, col, 60, 300 );
			}
		}
		CG_FxShake( position, 2.5f, 500 );
		CG_FxSound( position, ENTITYNUM_WORLD, CHAN_AUTO, arsc.holy );
		break;
	case DSX_RICOCHET:
		Arsc_Color( EVO_IRONMAIDEN, col );
		Arsc_Flash( position, col, 4, 16, 110 );
		CG_FxSparks( position, arscUp, col, 5, 300, 260, 0.0f, 0 );
		CG_FxSound( position, ENTITYNUM_WORLD, CHAN_AUTO, arsc.ping );
		break;
	case DSX_BLACKHOLE:
		Arsc_Color( EVO_DOOMSDAY, col );
		Arsc_Flash( position, col, 90, 10, 420 );		// collapses inward
		Arsc_Ring( position, arscUp, col, 300, 600 );
		if ( cg_fx.integer ) {
			CG_FxExplosion( position, arscUp, 1.2f, col, qtrue );
		}
		CG_FxShake( position, 5, 900 );
		trap_S_StartSound( position, ENTITYNUM_WORLD, CHAN_AUTO, arsc.voidSnd );
		break;
	case DSX_EVOLVE:
		evo = es->generic1;
		Arsc_Color( evo, col );
		if ( es->otherEntityNum >= 0 && es->otherEntityNum < MAX_CLIENTS ) {
			VectorCopy( cg_entities[es->otherEntityNum].lerpOrigin, p );
		} else {
			VectorCopy( position, p );
		}
		Arsc_Flash( p, col, 30, 160, 600 );
		Arsc_Ring( p, arscUp, col, 220, 700 );
		for ( i = 0 ; i < 24 ; i++ ) {
			VectorSet( a, p[0] + crandom() * 24, p[1] + crandom() * 24, p[2] - 20 + random() * 30 );
			VectorSet( b, crandom() * 30, crandom() * 30, 120 + random() * 160 );
			Arsc_Mote( a, b, col, 900 + rand() % 500, 4 );
		}
		if ( !cg.snap || es->otherEntityNum != cg.snap->ps.clientNum ) {
			trap_S_StartSound( p, ENTITYNUM_WORLD, CHAN_AUTO, arsc.evolve );
		}
		break;
	default:
		break;
	}
}

/*
===============================================================================

WEAPON IN HAND

===============================================================================
*/

/*
=================
CG_DS_WeaponFX

From CG_AddPlayerWeapon: an evolved weapon wears a coloured shell and a muzzle glow
=================
*/
void CG_DS_WeaponFX( refEntity_t *gun, qhandle_t weaponModel, playerState_t *ps, centity_t *cent ) {
	refEntity_t	shell, glow;
	int			evo, w = cent->currentState.weapon;
	vec3_t		col;
	float		pulse;

	evo = Arsc_PlayerEvo( ps ? ps->clientNum : cent->currentState.clientNum, w,
		ps ? ps->generic1 : cent->currentState.generic1 );
	if ( evo < 0 ) {
		return;
	}
	Arsc_Color( evo, col );
	pulse = 0.65f + 0.35f * sin( cg.time * 0.006f );
	shell = *gun;
	shell.customShader = arsc.shell;
	shell.shaderRGBA[0] = 255 * col[0] * pulse;
	shell.shaderRGBA[1] = 255 * col[1] * pulse;
	shell.shaderRGBA[2] = 255 * col[2] * pulse;
	shell.shaderRGBA[3] = 255;
	trap_R_AddRefEntityToScene( &shell );

	if ( !cg_fx.integer ) {
		return;
	}
	memset( &glow, 0, sizeof( glow ) );
	CG_PositionEntityOnTag( &glow, gun, weaponModel, "tag_flash" );
	glow.reType = RT_SPRITE;
	glow.renderfx = gun->renderfx;
	glow.radius = ( ps ? 3.0f : 7.0f ) * ( 0.85f + 0.3f * pulse );
	glow.customShader = cgs.media.fxGlowShader;
	glow.shaderRGBA[0] = 255 * col[0] * pulse;
	glow.shaderRGBA[1] = 255 * col[1] * pulse;
	glow.shaderRGBA[2] = 255 * col[2] * pulse;
	glow.shaderRGBA[3] = 255;
	trap_R_AddRefEntityToScene( &glow );
}

// STORM CALLER: violet forks crawl along the lightning beam
void CG_DS_LightningBeam( centity_t *cent, vec3_t start, vec3_t end ) {
	vec3_t	col;
	int		evo;

	evo = Arsc_PlayerEvo( cent->currentState.clientNum, WP_LIGHTNING,
		( cg.snap && cent->currentState.clientNum == cg.snap->ps.clientNum ) ? cg.predictedPlayerState.generic1
		: cent->currentState.generic1 );
	if ( evo != EVO_STORMCALLER ) {
		return;
	}
	Arsc_Color( evo, col );
	Arsc_Bolt( start, end, col, 7, 9, 0 );
	Arsc_Bolt( start, end, col, 5, 14, 0 );
}

/*
=================
CG_DS_FireWeapon

From CG_FireWeapon: evolved weapons layer their own sound on the shot
=================
*/
void CG_DS_FireWeapon( centity_t *cent ) {
	entityState_t	*es = &cent->currentState;
	sfxHandle_t		sfx;
	int				evo, gap = 0;

	evo = Arsc_PlayerEvo( es->clientNum, es->weapon,
		( cg.snap && es->clientNum == cg.snap->ps.clientNum ) ? cg.predictedPlayerState.generic1 : es->generic1 );
	if ( evo < 0 ) {
		return;
	}
	switch ( evo ) {
	case EVO_BULLETHELL:
	case EVO_RIPSAW:		sfx = arsc.bullet; gap = 170; break;
	case EVO_DRAGONBREATH:	sfx = arsc.flame; break;
	case EVO_CLUSTERBOMB:	sfx = arsc.blast; break;
	case EVO_HELLFIRE:
	case EVO_ARMAGEDDON:	sfx = arsc.rocket; break;
	case EVO_STORMCALLER:	sfx = arsc.storm; gap = 250; break;
	case EVO_JUDGEMENT:		sfx = arsc.holy; break;
	case EVO_STARFURNACE:	sfx = arsc.star; gap = 140; break;
	case EVO_DOOMSDAY:		sfx = arsc.voidSnd; gap = 600; break;
	case EVO_IRONMAIDEN:	sfx = arsc.ping; break;
	case EVO_MINEFIELD:		sfx = arsc.blast; break;
	default:				return;
	}
	if ( gap && cg.time < cent->dsFxTime ) {
		return;
	}
	cent->dsFxTime = cg.time + gap;
	trap_S_StartSound( NULL, es->number, CHAN_AUTO, sfx );
}

/*
=================
CG_DS_ShotgunPellet

DRAGON'S BREATH / level 8: a burning streak along a pellet
=================
*/
void CG_DS_ShotgunPellet( vec3_t start, vec3_t end, int skipNum, int code ) {
	trace_t	tr;
	vec3_t	col, dir, from;
	float	len;

	if ( !( code & DSSG_FIRE ) || !cg_fx.integer ) {
		return;
	}
	CG_Trace( &tr, start, NULL, NULL, end, skipNum, MASK_SHOT );
	if ( code & DSSG_EVOLVED ) {
		Arsc_Color( EVO_DRAGONBREATH, col );
	} else {
		VectorCopy( arscLevel8, col );
	}
	VectorSubtract( tr.endpos, start, dir );
	len = VectorNormalize( dir );
	VectorMA( start, len > 60 ? 40 : 0, dir, from );
	Arsc_Beam( from, tr.endpos, col, ( code & DSSG_EVOLVED ) ? 160 : 90 );
	if ( ( code & DSSG_EVOLVED ) && tr.fraction < 1.0f ) {
		VectorSet( dir, crandom() * 20, crandom() * 20, 30 );
		CG_GoreFire( tr.endpos, dir, 5, 16, 420, col, 2, 30 );
	}
}

/*
===============================================================================

SERVER COMMANDS

===============================================================================
*/

qboolean CG_DS_ArsenalServerCommand( const char *cmd ) {
	if ( !Q_stricmp( cmd, "dslvl" ) ) {
		arsc.flashWeapon = atoi( CG_Argv( 1 ) );
		arsc.flashLevel = atoi( CG_Argv( 2 ) );
		arsc.flashEvo = -1;
		arsc.flashTime = cg.time;
		trap_S_StartLocalSound( arsc.levelup, CHAN_LOCAL_SOUND );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsevo" ) ) {
		arsc.flashEvo = atoi( CG_Argv( 1 ) );
		if ( arsc.flashEvo < 0 || arsc.flashEvo >= EVO_NUM ) {
			arsc.flashEvo = -1;
			return qtrue;
		}
		arsc.flashWeapon = ds_evolutions[arsc.flashEvo].weapon;
		arsc.flashLevel = DS_MAX_WLEVEL;
		arsc.flashTime = cg.time;
		trap_S_StartLocalSound( arsc.evolve, CHAN_LOCAL_SOUND );
		return qtrue;
	}
	return qfalse;
}

/*
===============================================================================

HUD

===============================================================================
*/

int CG_DS_ArsenalLevel( int w ) {
	if ( !CG_RL_Active() || !DS_WEAPON_LEVELS( w ) ) {
		return 0;
	}
	return CG_DS_WeaponLevel( w );
}

qboolean CG_DS_ArsenalEvoColor( int w, vec4_t out ) {
	int		evo;

	if ( !CG_RL_Active() || ( evo = Arsc_LocalEvo( w ) ) < 0 ) {
		return qfalse;
	}
	Arsc_Color( evo, out );
	out[3] = 1.0f;
	return qtrue;
}

/*
=================
CG_DS_DrawWeaponLevel

Status bar: 8 level pips above the ammo number, a frame round the ammo icon when the
weapon is evolved, a short flash + label beside the pips on a level-up (640 space).
=================
*/
void CG_DS_DrawWeaponLevel( float x, float y, float iconX, float iconSize ) {
	int			w, lvl, i, evo, t;
	vec4_t		col, dim, txt;
	float		px, f, flash;
	const char	*s;

	if ( !CG_RL_Active() || !cg.snap || cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
		return;
	}
	w = cg.predictedPlayerState.weapon;
	lvl = CG_DS_ArsenalLevel( w );
	t = cg.time - arsc.flashTime;
	flash = ( arsc.flashTime && t >= 0 && t < 1400 && arsc.flashWeapon == w ) ? 1.0f - t / 1400.0f : 0;

	if ( lvl > 0 ) {
		evo = Arsc_LocalEvo( w );
		if ( evo >= 0 ) {
			Arsc_Color( evo, col );
			col[3] = 0.7f + 0.3f * sin( cg.time * 0.006f );
			// evolved: a frame round the ammo icon
			CG_DrawRect( iconX - 2, y - 2, iconSize + 4, iconSize + 4, 1.5f, col );
		} else if ( lvl >= DS_MAX_WLEVEL ) {
			VectorCopy( arscLevel8, col );
			col[3] = 1.0f;
		} else {
			col[0] = 0.95f; col[1] = 0.85f; col[2] = 0.6f; col[3] = 0.9f;
		}
		dim[0] = dim[1] = dim[2] = 0.0f; dim[3] = 0.45f;
		for ( i = 0 ; i < DS_MAX_WLEVEL ; i++ ) {
			px = x + 6 + i * 11;
			CG_FillRect( px - 1, y - 9, 10, 6, dim );
			if ( i < lvl ) {
				Vector4Copy( col, txt );
				if ( flash > 0 && i == lvl - 1 ) {
					f = flash * ( 0.6f + 0.4f * sin( cg.time * 0.04f ) );
					txt[0] += ( 1 - txt[0] ) * f;
					txt[1] += ( 1 - txt[1] ) * f;
					txt[2] += ( 1 - txt[2] ) * f;
				}
				CG_FillRect( px, y - 8, 8, 4, txt );
			}
		}
	}

	// level-up / evolution label beside the pips (any weapon), short and off-centre
	if ( arsc.flashTime && t >= 0 && t < ( arsc.flashEvo >= 0 ? 2600 : 1600 ) ) {
		f = CG_Clamp01( ( ( arsc.flashEvo >= 0 ? 2600 : 1600 ) - t ) / 400.0f );
		if ( arsc.flashEvo >= 0 ) {
			Arsc_Color( arsc.flashEvo, txt );
			s = va( "EVOLVED: %s", ds_evolutions[arsc.flashEvo].name );
		} else {
			txt[0] = 1.0f; txt[1] = 0.9f; txt[2] = 0.55f;
			s = arsc.flashWeapon == w ? va( "LEVEL %i", arsc.flashLevel )
				: va( "%s LV %i", ( arsc.flashWeapon > 0 && arsc.flashWeapon < DS_NUM_WEAPONS ) ? ds_weaponNames[arsc.flashWeapon] : "", arsc.flashLevel );
		}
		txt[3] = f;
		CG_DrawStringExt( x + 6 + DS_MAX_WLEVEL * 11 + 4, y - 11, s, txt, qtrue, qtrue, 6, 9, 0 );
	}
}
