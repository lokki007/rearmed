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
// cg_mutators.c -- rearmed: random event visuals (incoming warning, timer, screen grades,
// blood rain, storm flashes, meteors, strike markers), slow motion FX, obituaries of the
// new deaths and the one-time tips

#include "cg_local.h"
#include "../client/keycodes.h"

#define ROULETTE_TIME		8000	// matches g_rearmed.c
#define TIP_TIME			6500

typedef struct {
	qhandle_t	filterShader;		// multiply grade
	qhandle_t	vignetteShader;		// dark radial vignette (alpha)
	qhandle_t	topGlowShader;		// additive glow from the top edge
	qhandle_t	frostShader;		// frosted screen edges
	qhandle_t	flashShader;		// additive full screen
	qhandle_t	meteorModel;
	qhandle_t	meteorSkin;			// fallback sprite
	sfxHandle_t	incomingSound;
	sfxHandle_t	tickSound;
	sfxHandle_t	rouletteSound;
	sfxHandle_t	meteorLoopSound;
	sfxHandle_t	meteorWarnSound;
	sfxHandle_t	strikeWarnSound;
	sfxHandle_t	corpseBeepSound;
	sfxHandle_t	slowmoSound;
	sfxHandle_t	tipSound;
	sfxHandle_t	voices[MUT_NUM_MUTATORS];
} evMedia_t;

static evMedia_t	ev;

static const char *evVoiceNames[MUT_NUM_MUTATORS] = {
	NULL, "low_gravity", "vampire", "rocket_frenzy", "speed_rush", "big_heads", "knockback_chaos",
	"quad_madness", "jetpack_party", "blood_moon", "meteor_shower", "overcharge", "bullet_time",
	"ice_rink", "barrel_bonanza", "weapon_roulette", "thunderstorm", "chain_reaction"
};

// event colours for the HUD
static const vec4_t evColors[MUT_NUM_MUTATORS] = {
	{ 1, 1, 1, 1 },
	{ 0.7f, 0.6f, 1.0f, 1 },		// low gravity
	{ 1.0f, 0.25f, 0.3f, 1 },		// vampire
	{ 1.0f, 0.55f, 0.15f, 1 },		// rocket frenzy
	{ 0.9f, 1.0f, 0.4f, 1 },		// speed rush
	{ 1.0f, 0.8f, 0.6f, 1 },		// big heads
	{ 1.0f, 0.7f, 0.2f, 1 },		// knockback chaos
	{ 0.4f, 0.55f, 1.0f, 1 },		// quad madness
	{ 0.4f, 0.85f, 1.0f, 1 },		// jetpack party
	{ 1.0f, 0.15f, 0.12f, 1 },		// blood moon
	{ 1.0f, 0.5f, 0.1f, 1 },		// meteor shower
	{ 0.45f, 0.75f, 1.0f, 1 },		// overcharge
	{ 0.7f, 0.85f, 1.0f, 1 },		// bullet time
	{ 0.65f, 0.95f, 1.0f, 1 },		// ice rink
	{ 1.0f, 0.45f, 0.2f, 1 },		// barrel bonanza
	{ 1.0f, 0.85f, 0.3f, 1 },		// weapon roulette
	{ 0.55f, 0.7f, 1.0f, 1 },		// thunderstorm
	{ 1.0f, 0.3f, 0.2f, 1 }			// chain reaction
};

static int		evLastNext;			// incoming event seen last frame
static int		evWarnTick;			// countdown second last ticked
static int		evRouletteTick;
static int		evStormFlash;		// cg.time of the last storm flash
static int		evNextStormFlash;
static int		evSlowmoTime;
static int		evNextRain;
static int		tipShowing = -1;
static int		tipTime;
static int		tipQueue;
static int		tipGroundTime, tipAirTime, tipSpawnTime;

/*
=================
CG_MutatorsRegister
=================
*/
void CG_MutatorsRegister( void ) {
	int		i;

	ev.filterShader = trap_R_RegisterShaderNoMip( "rearmed/evFilter" );
	ev.vignetteShader = trap_R_RegisterShaderNoMip( "rearmed/evVignette" );
	ev.topGlowShader = trap_R_RegisterShaderNoMip( "rearmed/evTopGlow" );
	ev.frostShader = trap_R_RegisterShaderNoMip( "rearmed/evFrost" );
	ev.flashShader = trap_R_RegisterShaderNoMip( "rearmed/evFlash" );
	ev.meteorModel = trap_R_RegisterModel( "models/rearmed/meteor.md3" );
	ev.incomingSound = trap_S_RegisterSound( "sound/rearmed/events/incoming.wav", qfalse );
	ev.tickSound = trap_S_RegisterSound( "sound/rearmed/events/tick.wav", qfalse );
	ev.rouletteSound = trap_S_RegisterSound( "sound/rearmed/events/roulette.wav", qfalse );
	ev.meteorLoopSound = trap_S_RegisterSound( "sound/rearmed/events/meteor_loop.wav", qfalse );
	ev.meteorWarnSound = trap_S_RegisterSound( "sound/rearmed/events/meteor_warn.wav", qfalse );
	ev.strikeWarnSound = trap_S_RegisterSound( "sound/rearmed/events/strike_warn.wav", qfalse );
	ev.corpseBeepSound = trap_S_RegisterSound( "sound/rearmed/events/corpse_beep.wav", qfalse );
	ev.slowmoSound = trap_S_RegisterSound( "sound/rearmed/events/slowmo.wav", qfalse );
	ev.tipSound = trap_S_RegisterSound( "sound/rearmed/events/tip.wav", qfalse );
	for ( i = 1 ; i < MUT_NUM_MUTATORS ; i++ ) {
		ev.voices[i] = trap_S_RegisterSound( va( "sound/rearmed/events/v_%s.wav", evVoiceNames[i] ), qfalse );
	}
	evLastNext = 0;
	evWarnTick = 0;
	evRouletteTick = 0;
	evStormFlash = 0;
	evNextStormFlash = 0;
	evSlowmoTime = 0;
	tipShowing = -1;
	tipQueue = 0;
	tipGroundTime = tipAirTime = 0;
	tipSpawnTime = 0;
}

/*
===============================================================================

WORLD: strike markers, meteors, corpse explosions, storm and blood rain

===============================================================================
*/

static void CG_EvShake( const vec3_t org, float amplitude, float radius ) {
	float	dist, amp;

	if ( !cg_screenShake.value ) {
		return;
	}
	dist = Distance( org, cg.refdef.vieworg );
	if ( dist >= radius ) {
		return;
	}
	amp = amplitude * ( 1.0f - dist / radius ) * CG_Clamp01( cg_screenShake.value );
	if ( amp > cg.shakeAmplitude * ( 1.0f - (float)( cg.time - cg.shakeTime ) / 450.0f ) ) {
		cg.shakeTime = cg.time;
		cg.shakeAmplitude = amp;
	}
}

/*
=================
CG_GroundQuad

A flat textured quad lying on the floor (rings, danger discs)
=================
*/
static void CG_GroundQuad( const vec3_t org, float half, const float *rgb, float strength, qhandle_t shader ) {
	polyVert_t	v[4];
	int			i, c[3];

	for ( i = 0 ; i < 3 ; i++ ) {
		c[i] = (int)( 255 * CG_Clamp01( rgb[i] * strength ) );
	}
	for ( i = 0 ; i < 4 ; i++ ) {
		v[i].xyz[0] = org[0] + ( ( i == 1 || i == 2 ) ? half : -half );
		v[i].xyz[1] = org[1] + ( ( i >= 2 ) ? half : -half );
		v[i].xyz[2] = org[2];
		v[i].st[0] = ( i == 1 || i == 2 ) ? 1 : 0;
		v[i].st[1] = ( i >= 2 ) ? 1 : 0;
		v[i].modulate[0] = c[0];
		v[i].modulate[1] = c[1];
		v[i].modulate[2] = c[2];
		v[i].modulate[3] = 255;
	}
	trap_R_AddPolyToScene( shader, 4, v );
}

static void CG_EvSprite( const vec3_t org, float radius, const float *rgb, float strength, qhandle_t shader ) {
	refEntity_t	re;

	memset( &re, 0, sizeof( re ) );
	re.reType = RT_SPRITE;
	VectorCopy( org, re.origin );
	re.radius = radius;
	re.customShader = shader;
	re.shaderRGBA[0] = (byte)( 255 * CG_Clamp01( rgb[0] * strength ) );
	re.shaderRGBA[1] = (byte)( 255 * CG_Clamp01( rgb[1] * strength ) );
	re.shaderRGBA[2] = (byte)( 255 * CG_Clamp01( rgb[2] * strength ) );
	re.shaderRGBA[3] = 255;
	trap_R_AddRefEntityToScene( &re );
}

/*
=================
CG_StrikeMarker

A glowing ring on the floor where a meteor, a lightning bolt or a corpse bomb will hit,
with a second ring closing in as the moment comes
=================
*/
void CG_StrikeMarker( centity_t *cent ) {
	static const vec3_t	colors[3] = { { 1.0f, 0.45f, 0.1f }, { 0.5f, 0.8f, 1.0f }, { 1.0f, 0.15f, 0.1f } };
	entityState_t	*es = &cent->currentState;
	vec3_t			org, glowOrg;
	const float		*rgb;
	float			frac, radius, pulse, total;
	int				kind;

	kind = es->modelindex2;
	if ( kind < 0 || kind > 2 ) {
		kind = 0;
	}
	rgb = colors[kind];
	total = es->time2 - es->time;
	frac = total > 0 ? CG_Clamp01( (float)( cg.time - es->time ) / total ) : 1.0f;
	radius = es->otherEntityNum2 > 0 ? es->otherEntityNum2 : 150;
	pulse = 0.6f + 0.4f * sin( cg.time * ( 0.012f + 0.03f * frac ) );

	VectorCopy( cent->lerpOrigin, org );
	org[2] += 1.5f;

	// first sight: a warning sound
	if ( cent->miscTime < es->time ) {
		cent->miscTime = es->time;
		cent->trailTime = cg.time;
		if ( kind == RM_STRIKE_METEOR ) {
			trap_S_StartSound( org, ENTITYNUM_WORLD, CHAN_AUTO, ev.meteorWarnSound );
		} else if ( kind == RM_STRIKE_LIGHTNING ) {
			trap_S_StartSound( org, ENTITYNUM_WORLD, CHAN_AUTO, ev.strikeWarnSound );
		}
	}
	// corpse bombs beep faster and faster
	if ( kind == RM_STRIKE_CORPSE && cg.time >= cent->trailTime ) {
		CG_FxSound( org, ENTITYNUM_WORLD, CHAN_AUTO, ev.corpseBeepSound );
		cent->trailTime = cg.time + 70 + (int)( 260 * ( 1.0f - frac ) );
	}

	if ( kind != RM_STRIKE_CORPSE ) {
		// danger disc, outer ring and the ring closing in
		CG_GroundQuad( org, radius * 0.9f, rgb, 0.05f + 0.1f * frac, cgs.media.fxGlowShader );
		CG_GroundQuad( org, radius / 0.8f, rgb, 0.3f + 0.3f * pulse, cgs.media.fxRingShader );
		CG_GroundQuad( org, radius * ( 1.0f - frac ) / 0.8f + 4, rgb, 0.45f + 0.4f * frac, cgs.media.fxRingShader );
		trap_R_AddLightToScene( org, 60 + 120 * frac, rgb[0], rgb[1], rgb[2] );
		VectorCopy( org, glowOrg );
		glowOrg[2] += 8;
		CG_EvSprite( glowOrg, 8 + 18 * frac * pulse, rgb, 1.0f, cgs.media.fxGlowShader );
	} else {
		// a pulsing red heart on the body
		VectorCopy( org, glowOrg );
		glowOrg[2] += 6;
		CG_EvSprite( glowOrg, 14 + 22 * frac * pulse, rgb, 0.8f + 0.2f * pulse, cgs.media.fxGlowShader );
		CG_GroundQuad( org, ( 30 + radius * 0.6f * frac ) / 0.8f, rgb, 0.4f + 0.5f * pulse, cgs.media.fxRingShader );
		trap_R_AddLightToScene( glowOrg, 80 + 140 * frac * pulse, 1.0f, 0.15f, 0.1f );
	}
}

/*
=================
CG_MeteorMissile

A burning rock falling out of the sky with a fire and smoke trail
=================
*/
void CG_MeteorMissile( centity_t *cent ) {
	static const vec3_t	fire = { 1.0f, 0.5f, 0.15f };
	static const vec3_t	core = { 1.0f, 0.85f, 0.5f };
	entityState_t	*es = &cent->currentState;
	refEntity_t		re;
	vec3_t			pos, vel, angles;
	int				t;

	// trail puffs along the path, frame rate independent
	if ( cent->trailTime < es->pos.trTime || cent->trailTime > cg.time ) {
		cent->trailTime = cg.time - 30;
	}
	for ( t = cent->trailTime + 25 ; t <= cg.time ; t += 25 ) {
		BG_EvaluateTrajectory( &es->pos, t, pos );
		VectorSet( vel, crandom() * 30, crandom() * 30, 20 + random() * 30 );
		CG_SmokePuff( pos, vel, 26 + random() * 14, 1.0f, 0.5f + 0.2f * random(), 0.15f, 0.9f, 350 + rand() % 150,
			t, 0, 0, cgs.media.flameShader );
		if ( ( t / 25 ) & 1 ) {
			CG_SmokePuff( pos, vel, 22 + random() * 12, 0.25f, 0.22f, 0.2f, 0.55f, 1400 + rand() % 400,
				t, 0, 0, cgs.media.smokePuffShader );
		}
		cent->trailTime = t;
	}

	memset( &re, 0, sizeof( re ) );
	VectorCopy( cent->lerpOrigin, re.origin );
	VectorCopy( cent->lerpOrigin, re.oldorigin );
	if ( ev.meteorModel ) {
		angles[0] = cg.time * 0.31f;
		angles[1] = cg.time * 0.17f;
		angles[2] = cg.time * 0.23f;
		AnglesToAxis( angles, re.axis );
		VectorScale( re.axis[0], 1.5f, re.axis[0] );
		VectorScale( re.axis[1], 1.5f, re.axis[1] );
		VectorScale( re.axis[2], 1.5f, re.axis[2] );
		re.nonNormalizedAxes = qtrue;
		re.hModel = ev.meteorModel;
		trap_R_AddRefEntityToScene( &re );
		CG_EvSprite( cent->lerpOrigin, 70, fire, 0.55f, cgs.media.fxGlowShader );
		CG_EvSprite( cent->lerpOrigin, 30, core, 0.5f, cgs.media.fxGlowShader );
	} else {
		CG_EvSprite( cent->lerpOrigin, 60, fire, 1.0f, cgs.media.fxGlowShader );
		CG_EvSprite( cent->lerpOrigin, 22, core, 1.0f, cgs.media.fxGlowShader );
	}
	trap_R_AddLightToScene( cent->lerpOrigin, 320, 1.0f, 0.5f, 0.15f );
	trap_S_AddLoopingSound( es->number, cent->lerpOrigin, vec3_origin, ev.meteorLoopSound );
}

/*
=================
CG_MeteorImpact

On top of the rocket explosion: rock chunks, a heat flash and the ground shaking
=================
*/
void CG_MeteorImpact( vec3_t origin, vec3_t dir ) {
	localEntity_t	*le;
	vec3_t			org, vel;
	int				i;

	le = CG_MakeExplosion( origin, dir, cgs.media.dishFlashModel, cgs.media.rocketExplosionShader, 900, qtrue );
	le->light = 450;
	VectorSet( le->lightColor, 1.0f, 0.55f, 0.2f );
	trap_S_StartSound( origin, ENTITYNUM_WORLD, CHAN_AUTO, cgs.media.sfx_rockexp );
	for ( i = 0 ; i < 10 ; i++ ) {
		VectorMA( origin, 8, dir, org );
		VectorSet( vel, crandom() * 120, crandom() * 120, 80 + random() * 120 );
		CG_SmokePuff( org, vel, 30 + random() * 20, 0.35f, 0.3f, 0.26f, 0.7f, 1600 + rand() % 800,
			cg.time, 0, 0, cgs.media.smokePuffShader );
	}
	CG_EvShake( origin, 9, 1100 );
}

/*
=================
CG_CorpseExplode

Chain reaction: the body goes off like a bomb
=================
*/
void CG_CorpseExplode( entityState_t *es ) {
	static const vec3_t	up = { 0, 0, 1 };
	localEntity_t	*le;
	vec3_t			org, vel;
	int				i;

	VectorCopy( es->pos.trBase, org );
	org[2] += 8;
	le = CG_MakeExplosion( org, (float *)up, cgs.media.dishFlashModel, cgs.media.rocketExplosionShader, 800, qtrue );
	le->light = 380;
	VectorSet( le->lightColor, 1.0f, 0.3f, 0.15f );
	trap_S_StartSound( org, ENTITYNUM_WORLD, CHAN_AUTO, cgs.media.sfx_rockexp );
	CG_GoreCorpseBurst( org );		// meat, mist and wall splatter (cg_gore.c, respects cg_blood)
	if ( cg_blood.integer ) {
		for ( i = 0 ; i < 6 ; i++ ) {
			VectorSet( vel, crandom() * 220, crandom() * 220, 60 + random() * 220 );
			CG_SmokePuff( org, vel, 18 + random() * 16, 0.55f, 0.02f, 0.02f, 0.85f, 900 + rand() % 500,
				cg.time, 0, 0, cgs.media.smokePuffShader );
		}
	}
	CG_ImpactFX( WP_ROCKET_LAUNCHER, org, up, qfalse );
	CG_EvShake( org, 6, 800 );
}

/*
=================
CG_SlowMoEvent
=================
*/
void CG_SlowMoEvent( int strength ) {
	evSlowmoTime = cg.time;
	if ( strength > 40 ) {
		trap_S_StartLocalSound( ev.slowmoSound, CHAN_LOCAL_SOUND );
	}
}

/*
=================
CG_EvSkyAbove
=================
*/
static qboolean CG_EvSkyAbove( const vec3_t point, vec3_t sky ) {
	trace_t	tr;
	vec3_t	end;

	VectorCopy( point, end );
	end[2] += 16384;
	CG_Trace( &tr, point, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID );
	if ( tr.startsolid || ( tr.fraction < 1.0f && !( tr.surfaceFlags & SURF_SKY ) ) ) {
		return qfalse;
	}
	VectorCopy( tr.endpos, sky );
	if ( sky[2] > point[2] + 1800 ) {
		sky[2] = point[2] + 1800;
	}
	return qtrue;
}

/*
=================
CG_EvWorldFX

Blood rain, distant lightning
=================
*/
static void CG_EvWorldFX( void ) {
	vec3_t	org, sky, vel, ground, end;
	trace_t	tr;
	float	ang, r;
	int		i;

	if ( !cg_eventFX.integer || !cg.snap ) {
		return;
	}
	// blood moon: red rain around the viewer where the sky is open
	if ( cgs.rmMutator == MUT_BLOOD_MOON && cg.time >= evNextRain ) {
		evNextRain = cg.time + 16;
		for ( i = 0 ; i < 4 ; i++ ) {
			ang = random() * 2 * M_PI;
			r = 40 + random() * 520;
			VectorCopy( cg.refdef.vieworg, org );
			org[0] += cos( ang ) * r;
			org[1] += sin( ang ) * r;
			org[2] += 40;
			if ( !CG_EvSkyAbove( org, sky ) ) {
				continue;
			}
			org[2] += 260 < sky[2] - org[2] - 8 ? 260 : sky[2] - org[2] - 8;
			VectorSet( vel, crandom() * 10, crandom() * 10, -950 );
			CG_SmokePuff( org, vel, 1.6f, 0.6f, 0.0f, 0.0f, 0.9f, 650, cg.time, 0, LEF_PUFF_DONT_SCALE,
				cgs.media.fxGlowShader );
		}
	}
	// thunderstorm: sheet lightning and far bolts
	if ( cgs.rmMutator == MUT_THUNDERSTORM ) {
		if ( !evNextStormFlash || evNextStormFlash - cg.time > 20000 ) {
			evNextStormFlash = cg.time + 2500 + rand() % 3000;
		}
		if ( cg.time >= evNextStormFlash ) {
			evNextStormFlash = cg.time + 3500 + rand() % 5000;
			evStormFlash = cg.time;
			ang = random() * 2 * M_PI;
			r = 700 + random() * 900;
			VectorCopy( cg.refdef.vieworg, org );
			org[0] += cos( ang ) * r;
			org[1] += sin( ang ) * r;
			if ( ( rand() & 1 ) && CG_EvSkyAbove( org, sky ) ) {
				VectorCopy( org, end );
				end[2] -= 2000;
				CG_Trace( &tr, org, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID );
				VectorCopy( tr.endpos, ground );
				CG_ThunderFX( ground, sky );
			} else {
				trap_S_StartSound( org, ENTITYNUM_WORLD, CHAN_AUTO, cgs.media.thunderSound );
			}
		}
	} else {
		evNextStormFlash = 0;
	}
}

/*
===============================================================================

HUD: incoming warning, event timer, roulette countdown

===============================================================================
*/

static float CG_EvSlowmo( void ) {
	char	buf[32];
	float	s;

	trap_Cvar_VariableStringBuffer( "rm_slowmoNow", buf, sizeof( buf ) );	// the eased scale (rm_slowmo is the target)
	s = buf[0] ? atof( buf ) : 1.0f;
	if ( s <= 0.0f || s > 1.0f ) {
		s = 1.0f;
	}
	return s;
}

/*
=================
CG_MutatorsFrame

Every frame (before the scene): sounds for the schedule, world FX, tips
=================
*/
void CG_MutatorsFrame( void ) {
	int		secs, t, spin, i;

	if ( !cg.snap ) {
		return;
	}
	// an event was announced
	if ( cgs.rmMutatorNext != evLastNext ) {
		if ( cgs.rmMutatorNext > 0 && cgs.rmMutatorNext < MUT_NUM_MUTATORS ) {
			trap_S_StartLocalSound( ev.incomingSound, CHAN_LOCAL_SOUND );
			if ( cg_rearmAnnouncer.integer ) {
				trap_S_StartLocalSound( ev.voices[cgs.rmMutatorNext], CHAN_ANNOUNCER );
			}
			evWarnTick = 0;
			if ( cgs.rmMutatorNext == MUT_WEAPON_ROULETTE ) {
				for ( i = WP_MACHINEGUN ; i < WP_NUM_WEAPONS ; i++ ) {
					if ( i != WP_GRAPPLING_HOOK ) {
						CG_RegisterWeapon( i );
					}
				}
			}
			CG_Tip( TIP_EVENT );
		}
		evLastNext = cgs.rmMutatorNext;
	}
	if ( cgs.rmMutatorNext ) {
		secs = ( cgs.rmMutatorNextTime - cg.time + 999 ) / 1000;
		if ( secs >= 1 && secs <= 3 && secs != evWarnTick ) {
			evWarnTick = secs;
			trap_S_StartLocalSound( ev.tickSound, CHAN_LOCAL_SOUND );
		}
	}
	// weapon roulette: tick before each spin
	if ( cgs.rmMutator == MUT_WEAPON_ROULETTE && cgs.rmMutatorStart ) {
		t = cg.time - cgs.rmMutatorStart;
		spin = t / ROULETTE_TIME;
		if ( t % ROULETTE_TIME >= ROULETTE_TIME - 1000 && evRouletteTick != spin + 1 ) {
			evRouletteTick = spin + 1;
			trap_S_StartLocalSound( ev.rouletteSound, CHAN_LOCAL_SOUND );
		}
	}
	CG_EvWorldFX();
	CG_TipsFrame();
}

static void CG_EvCentered( float y, float cw, float ch, const char *s, const float *color ) {
	float	w = CG_DrawStrlen( s ) * cw;

	CG_DrawStringExt( 320 - w * 0.5f, y, s, color, qtrue, qtrue, (int)cw, (int)ch, 0 );
}

/*
=================
CG_DrawEventHud

Under the announcement banner: the incoming countdown, or the running event with a timer bar
=================
*/
void CG_DrawEventHud( float y ) {
	vec4_t		col, bg;
	float		f, w, pop, t;
	int			secs, mut, left;
	const char	*s;

	// incoming
	mut = cgs.rmMutatorNext;
	if ( mut > 0 && mut < MUT_NUM_MUTATORS ) {
		left = cgs.rmMutatorNextTime - cg.time;
		secs = ( left + 999 ) / 1000;
		if ( secs < 1 ) {
			secs = 1;
		}
		t = 1.0f - ( left % 1000 ) / 1000.0f;		// 0..1 inside each second
		Vector4Copy( evColors[mut], col );
		col[3] = 0.75f + 0.25f * sin( cg.time * 0.02f );
		bg[0] = bg[1] = bg[2] = 0; bg[3] = 0.55f;
		CG_FillRect( 320 - 110, y - 3, 220, 50, bg );
		col[3] = 1.0f;
		CG_FillRect( 320 - 110, y - 3, 220 * CG_Clamp01( left / 3000.0f ), 2, col );
		CG_EvCentered( y, 7, 10, "INCOMING EVENT", col );
		pop = 1.0f + 0.35f * ( 1.0f - CG_Clamp01( t * 4 ) );
		CG_EvCentered( y + 12 - 4 * ( pop - 1 ), 11 * pop, 16 * pop, bg_rmMutatorNames[mut], col );
		col[0] = col[1] = col[2] = 1; col[3] = 0.85f;
		CG_EvCentered( y + 32, 7, 10, va( "%i", secs ), col );
		return;
	}

	// running
	mut = cgs.rmMutator;
	if ( mut <= 0 || mut >= MUT_NUM_MUTATORS ) {
		return;
	}
	t = cgs.rmMutatorStart ? (float)( cg.time - cgs.rmMutatorStart ) : 0;
	pop = t < 6000 ? 1.0f : t < 6600 ? 1.0f - ( t - 6000 ) / 600.0f : 0;
	if ( cgs.rmMutatorEnd < 0 ) {
		// ds-fixes: a floor-long event (DESCENT event floors): no countdown, it ends with the floor
		Vector4Copy( evColors[mut], col );
		bg[0] = bg[1] = bg[2] = 0; bg[3] = 0.35f;
		CG_FillRect( 320 - 100, y - 3, 200, pop > 0 ? 40 : 28, bg );
		CG_EvCentered( y, 10, 15, bg_rmMutatorNames[mut], col );
		col[3] = 0.8f;
		CG_EvCentered( y + 16, 6, 9, "THIS FLOOR", col );
		if ( pop > 0 ) {
			col[0] = col[1] = col[2] = 1.0f; col[3] = 0.85f * pop;
			CG_EvCentered( y + 27, 6, 9, bg_rmMutatorHints[mut], col );
		}
		left = 0;
	} else {
		left = cgs.rmMutatorEnd - cg.time;
		if ( left < 0 ) {
			left = 0;
		}
		Vector4Copy( evColors[mut], col );
		if ( left < 5000 ) {
			col[3] = 0.55f + 0.45f * ( ( cg.time / 200 ) & 1 );
		}
		// a dark plate keeps the name and hint readable over bright maps and event grades
		// the hint line only for the first seconds, then just the name and the timer
		bg[0] = bg[1] = bg[2] = 0; bg[3] = 0.35f;
		CG_FillRect( 320 - 100, y - 3, 200, pop > 0 ? 38 : 26, bg );
		CG_EvCentered( y, 10, 15, bg_rmMutatorNames[mut], col );
		// timer bar
		w = 180;
		f = cgs.rmMutatorStart && cgs.rmMutatorEnd > cgs.rmMutatorStart
			? CG_Clamp01( (float)left / ( cgs.rmMutatorEnd - cgs.rmMutatorStart ) ) : 0;
		bg[3] = 0.6f;
		CG_FillRect( 320 - w / 2 - 1, y + 17, w + 2, 4, bg );
		col[3] = 0.95f;
		CG_FillRect( 320 - w / 2, y + 18, w * f, 2, col );
		if ( pop > 0 ) {
			col[0] = col[1] = col[2] = 1.0f; col[3] = 0.85f * pop;
			CG_EvCentered( y + 24, 6, 9, bg_rmMutatorHints[mut], col );
		}
	}

	// roulette countdown
	if ( mut == MUT_WEAPON_ROULETTE && cgs.rmMutatorStart ) {
		left = ROULETTE_TIME - ( cg.time - cgs.rmMutatorStart ) % ROULETTE_TIME;
		if ( left <= 3000 ) {
			secs = ( left + 999 ) / 1000;
			pop = left <= 1000 ? 1.0f + 0.3f * sin( cg.time * 0.04f ) : 1.0f;
			col[0] = 1.0f; col[1] = left <= 1000 ? 0.4f : 0.85f; col[2] = 0.3f; col[3] = 1.0f;
			s = va( "NEW WEAPON IN %i", secs );
			CG_EvCentered( y + 38, 8 * pop, 12 * pop, s, col );
		}
	}
}

/*
===============================================================================

SCREEN: event grades, vignettes, slow motion (drawn under the HUD)

===============================================================================
*/

static void CG_EvFull( qhandle_t shader, float r, float g, float b, float a ) {
	vec4_t	c;

	c[0] = r; c[1] = g; c[2] = b; c[3] = a;
	trap_R_SetColor( c );
	trap_R_DrawStretchPic( 0, 0, cgs.glconfig.vidWidth, cgs.glconfig.vidHeight, 0, 0, 1, 1, shader );
	trap_R_SetColor( NULL );
}

static void CG_EvEdges( float r, float g, float b, float a, float width ) {
	vec4_t	c;
	float	w = cgs.glconfig.vidHeight * width;

	c[0] = r; c[1] = g; c[2] = b; c[3] = a;
	trap_R_SetColor( c );
	trap_R_DrawStretchPic( 0, 0, w, cgs.glconfig.vidHeight, 0, 0, 1, 1, cgs.media.edgeShader );
	trap_R_DrawStretchPic( cgs.glconfig.vidWidth - w, 0, w, cgs.glconfig.vidHeight, 1, 0, 0, 1, cgs.media.edgeShader );
	trap_R_SetColor( NULL );
}

/*
=================
CG_DrawMutatorFX
=================
*/
void CG_DrawMutatorFX( void ) {
	float	pulse, k, f;

	if ( !cg.snap ) {
		return;
	}
	if ( cg_eventFX.integer ) {
		pulse = 0.5f + 0.5f * sin( cg.time * 0.004f );
		switch ( cgs.rmMutator ) {
		case MUT_BLOOD_MOON:
			CG_EvFull( ev.filterShader, 1.0f, 0.58f, 0.52f, 1 );
			CG_EvFull( ev.vignetteShader, 0.35f, 0.0f, 0.0f, 0.55f + 0.15f * pulse );
			break;
		case MUT_THUNDERSTORM:
			CG_EvFull( ev.filterShader, 0.62f, 0.66f, 0.8f, 1 );
			CG_EvFull( ev.vignetteShader, 0, 0, 0.05f, 0.45f );
			f = cg.time - evStormFlash;
			if ( evStormFlash && f < 260 ) {
				// double flicker
				k = f < 60 ? 1.0f - f / 60.0f : ( f > 110 && f < 260 ? 0.6f * ( 1.0f - ( f - 110 ) / 150.0f ) : 0 );
				CG_EvFull( ev.flashShader, 0.55f * k, 0.6f * k, 0.75f * k, 1 );
			}
			break;
		case MUT_ICE_RINK:
			CG_EvFull( ev.filterShader, 0.88f, 0.96f, 1.0f, 1 );
			CG_EvFull( ev.frostShader, 0.8f, 0.94f, 1.0f, 0.6f );
			break;
		case MUT_METEOR_SHOWER:
			CG_EvFull( ev.filterShader, 1.0f, 0.88f, 0.78f, 1 );
			CG_EvFull( ev.topGlowShader, 0.35f + 0.1f * pulse, 0.12f, 0.02f, 1 );
			break;
		case MUT_BULLET_TIME:
			CG_EvFull( ev.vignetteShader, 0.0f, 0.03f, 0.08f, 0.35f );
			break;
		case MUT_OVERCHARGE:
			CG_EvEdges( 0.3f, 0.65f, 1.0f, 0.18f + 0.12f * pulse, 0.14f );
			break;
		case MUT_VAMPIRE:
			CG_EvEdges( 0.7f, 0.0f, 0.1f, 0.15f + 0.1f * pulse, 0.12f );
			break;
		case MUT_QUAD_MADNESS:
			CG_EvEdges( 0.25f, 0.35f, 1.0f, 0.18f + 0.1f * pulse, 0.14f );
			break;
		case MUT_ROCKET_FRENZY:
		case MUT_BARREL_BONANZA:
			CG_EvEdges( 1.0f, 0.45f, 0.1f, 0.12f + 0.08f * pulse, 0.12f );
			break;
		case MUT_SPEED_RUSH:
			CG_EvEdges( 0.9f, 1.0f, 0.6f, 0.1f + 0.1f * ( 0.5f + 0.5f * sin( cg.time * 0.02f ) ), 0.1f );
			break;
		case MUT_LOW_GRAVITY:
		case MUT_JETPACKS:
			CG_EvEdges( 0.55f, 0.45f, 1.0f, 0.1f + 0.06f * pulse, 0.12f );
			break;
		case MUT_CHAIN_REACTION:
			CG_EvFull( ev.vignetteShader, 0.25f, 0.02f, 0.0f, 0.35f + 0.1f * pulse );
			break;
		default:
			break;
		}
	}

	// slow motion: cold grade and a dark vignette following the engine's clock scale
	k = 1.0f - CG_EvSlowmo();
	if ( k > 0.02f ) {
		k = CG_Clamp01( k * 1.4f );
		CG_EvFull( ev.filterShader, 1.0f - 0.3f * k, 1.0f - 0.2f * k, 1.0f - 0.02f * k, 1 );
		CG_EvFull( ev.vignetteShader, 0, 0, 0.02f, 0.7f * k );
	}
}

/*
===============================================================================

OBITUARIES of the rearmed deaths

===============================================================================
*/

char *CG_RearmObituary( int mod, qboolean self, qboolean byPlayer, char **message2 ) {
	if ( byPlayer ) {
		switch ( mod ) {
		case MOD_SLAM:		*message2 = "'s ground pound"; return "was flattened by";
		case MOD_PROP:		*message2 = "'s barrel"; return "was blown sky high by";
		case MOD_WALLSPLAT:	*message2 = " into the wall"; return "was splattered by";
		case MOD_METEOR:	*message2 = "'s meteor"; return "was crushed by";
		case MOD_THORNS:	*message2 = "'s thorns"; return "bled out on";
		case MOD_CORPSE:	*message2 = "'s chain reaction"; return "was caught in";
		default:			return NULL;
		}
	}
	if ( self ) {
		return NULL;		// the suicide texts stay generic
	}
	switch ( mod ) {
	case MOD_SLAM:		return "was pounded into the floor";
	case MOD_PROP:		return "hugged an exploding barrel";
	case MOD_WALLSPLAT:	return "met the wall at full speed";
	case MOD_METEOR:	return "was flattened by a meteor";
	case MOD_THUNDER:	return "was struck by lightning";
	case MOD_CORPSE:	return "was caught in a chain reaction";
	case MOD_BURN:		return "burned to a crisp";
	default:			return NULL;
	}
}

/*
===============================================================================

TIPS: one line, once ever, when a mechanic first matters

===============================================================================
*/

/*
=================
CG_KeyName
=================
*/
static const char *CG_KeyName( int k ) {
	static char	buf[8];

	if ( k >= 'a' && k <= 'z' ) {
		buf[0] = k - 'a' + 'A';
		buf[1] = 0;
		return buf;
	}
	if ( k > 32 && k < 127 ) {
		buf[0] = k;
		buf[1] = 0;
		return buf;
	}
	if ( k >= K_F1 && k <= K_F12 ) {
		return va( "F%i", k - K_F1 + 1 );
	}
	if ( k >= K_MOUSE1 && k <= K_MOUSE5 ) {
		return va( "MOUSE%i", k - K_MOUSE1 + 1 );
	}
	switch ( k ) {
	case K_TAB:				return "TAB";
	case K_ENTER:			return "ENTER";
	case K_SPACE:			return "SPACE";
	case K_ALT:				return "ALT";
	case K_CTRL:			return "CTRL";
	case K_SHIFT:			return "SHIFT";
	case K_CAPSLOCK:		return "CAPSLOCK";
	case K_MWHEELUP:		return "WHEEL UP";
	case K_MWHEELDOWN:		return "WHEEL DOWN";
	case K_UPARROW:			return "UP";
	case K_DOWNARROW:		return "DOWN";
	case K_LEFTARROW:		return "LEFT";
	case K_RIGHTARROW:		return "RIGHT";
	case K_INS:				return "INS";
	case K_DEL:				return "DEL";
	case K_PAD0_A:			return "PAD A";
	case K_PAD0_B:			return "PAD B";
	case K_PAD0_X:			return "PAD X";
	case K_PAD0_Y:			return "PAD Y";
	case K_PAD0_LEFTSHOULDER:	return "LB";
	case K_PAD0_RIGHTSHOULDER:	return "RB";
	case K_PAD0_LEFTTRIGGER:	return "LT";
	case K_PAD0_RIGHTTRIGGER:	return "RT";
	case K_PAD0_LEFTSTICK_CLICK:	return "L3";
	case K_PAD0_RIGHTSTICK_CLICK:	return "R3";
	default:				return "KEY";
	}
}

/*
=================
CG_KeyForCommand

The key the player bound to a command, as a short name ("UNBOUND" when none)
=================
*/
const char *CG_KeyForCommand( const char *binding ) {
	int		k = trap_Key_GetKey( binding );

	if ( k <= 0 ) {
		return NULL;
	}
	return CG_KeyName( k );
}

static const char *CG_TipKey( const char *binding, const char *fallback ) {
	const char	*k = CG_KeyForCommand( binding );

	return va( "[%s]", k ? k : fallback );
}

static const char *CG_TipText( int tip ) {
	static char	buf[160];
	char		a[24], b[24], c[24];

	switch ( tip ) {
	case TIP_PERK:
		Q_strncpyz( a, CG_TipKey( "weapon 1", "1" ), sizeof( a ) );
		Q_strncpyz( b, CG_TipKey( "weapon 2", "2" ), sizeof( b ) );
		Q_strncpyz( c, CG_TipKey( "weapon 3", "3" ), sizeof( c ) );
		Com_sprintf( buf, sizeof( buf ), "PICK A PERK WITH %s %s %s  (KILLS EARN MORE)", a, b, c );
		break;
	case TIP_CHARGE:
		Com_sprintf( buf, sizeof( buf ), "HOLD %s TO CHARGE THE ALT-FIRE, TAP IT FOR A QUICK SHOT",
			CG_TipKey( "+button12", "ALT-FIRE" ) );
		break;
	case TIP_SLIDE:
		Com_sprintf( buf, sizeof( buf ), "CROUCH %s WHILE RUNNING TO SLIDE", CG_TipKey( "+movedown", "CROUCH" ) );
		break;
	case TIP_SLAM:
		Com_sprintf( buf, sizeof( buf ), "CROUCH %s IN THE AIR TO GROUND POUND", CG_TipKey( "+movedown", "CROUCH" ) );
		break;
	case TIP_EVENT:
		Com_sprintf( buf, sizeof( buf ), "RANDOM EVENT INCOMING: THE RULES CHANGE FOR A WHILE" );
		break;
	case TIP_WHEEL:
		Q_strncpyz( a, CG_TipKey( "+wheel", "WHEEL" ), sizeof( a ) );
		Com_sprintf( buf, sizeof( buf ), "HOLD %s FOR THE WEAPON WHEEL, TAP IT FOR YOUR LAST WEAPON", a );
		break;
	case TIP_WHEELUSE:
		Com_sprintf( buf, sizeof( buf ), "POINT AT A WEAPON, THEN LET GO OF %s", CG_TipKey( "+wheel", "THE KEY" ) );
		break;
	case TIP_DASH:
		Q_strncpyz( a, CG_TipKey( "+button14", "DASH" ), sizeof( a ) );
		Q_strncpyz( b, CG_TipKey( "+moveup", "JUMP" ), sizeof( b ) );
		Com_sprintf( buf, sizeof( buf ), "%s DASHES, %s AGAIN IN THE AIR DOUBLE JUMPS", a, b );
		break;
	case TIP_HOOK:
		Com_sprintf( buf, sizeof( buf ), "%s FIRES THE GRAPPLE, HOLD IT TO SWING", CG_TipKey( "+button13", "HOOK" ) );
		break;
	case TIP_ZIP:
		Com_sprintf( buf, sizeof( buf ), "[ ] = ZIP LINE READY: PRESS %s TO RIDE A CABLE THERE, JUMP LETS GO",
			CG_TipKey( "+button15", "ZIP" ) );
		break;
	default:
		buf[0] = 0;
		break;
	}
	return buf;
}

/*
=================
CG_Tip

Show a tip if it was never shown (cg_tipsSeen bits persist in the config)
=================
*/
void CG_Tip( int tip ) {
	if ( !cg_tips.integer || tip < 0 || tip >= TIP_NUM || ( cg_tipsSeen.integer & ( 1 << tip ) ) ) {
		return;
	}
	if ( tipShowing >= 0 && cg.time - tipTime < TIP_TIME && tipShowing != tip ) {
		tipQueue |= 1 << tip;
		return;
	}
	if ( tipShowing == tip && cg.time - tipTime < TIP_TIME ) {
		return;
	}
	tipShowing = tip;
	tipTime = cg.time;
	trap_Cvar_Set( "cg_tipsSeen", va( "%i", cg_tipsSeen.integer | ( 1 << tip ) ) );
	cg_tipsSeen.integer |= 1 << tip;
	trap_S_StartLocalSound( ev.tipSound, CHAN_LOCAL_SOUND );
}

/*
=================
CG_TipsFrame

Watch for the moments a mechanic first matters
=================
*/
void CG_TipsFrame( void ) {
	playerState_t	*ps;
	float			speed;
	int				i, n;

	if ( !cg_tips.integer || !cg.snap ) {
		return;
	}
	// next queued tip
	if ( ( tipShowing < 0 || cg.time - tipTime >= TIP_TIME ) && tipQueue ) {
		for ( i = 0 ; i < TIP_NUM ; i++ ) {
			if ( tipQueue & ( 1 << i ) ) {
				tipQueue &= ~( 1 << i );
				tipShowing = -1;
				CG_Tip( i );
				break;
			}
		}
	}
	ps = &cg.snap->ps;
	if ( ps->persistant[PERS_TEAM] == TEAM_SPECTATOR || ps->stats[STAT_HEALTH] <= 0 || ps->pm_type != PM_NORMAL
		|| ( ps->pm_flags & PMF_FOLLOW ) ) {
		tipSpawnTime = 0;
		return;
	}
	if ( !tipSpawnTime ) {
		tipSpawnTime = cg.time;
	}
	if ( cg.time - tipSpawnTime < 2500 ) {
		return;
	}
	// charged alt-fire
	if ( ( cgs.rmSwitches & ( 1 << RS_ALTFIRE ) ) && ( cgs.rmSwitches & ( 1 << RS_CHARGE ) )
		&& ps->weapon > WP_NONE && ps->weapon < WP_NUM_WEAPONS && bg_altFire[ps->weapon].chargeAmmo > 0 ) {
		CG_Tip( TIP_CHARGE );
	}
	// dash / double jump early on
	if ( ( cgs.rmSwitches & ( 1 << RS_DASH ) ) && cg.time - tipSpawnTime > 6000 ) {
		CG_Tip( TIP_DASH );
	}
	if ( cg.time - tipSpawnTime > 40000 && atoi( Info_ValueForKey( CG_ConfigString( CS_SERVERINFO ), "g_offhandHook" ) ) ) {
		CG_Tip( TIP_HOOK );
	}
	// zip line: the first time the anchor brackets come on
	if ( cg.time - tipSpawnTime > 4000 && CG_ZipAnchorReady() ) {
		CG_Tip( TIP_ZIP );
	}
	// slide: running fast on the ground for a moment
	speed = sqrt( ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1] );
	if ( ps->groundEntityNum != ENTITYNUM_NONE && speed > 300 ) {
		if ( !tipGroundTime ) {
			tipGroundTime = cg.time;
		}
		if ( cg.time - tipGroundTime > 700 && ( cgs.rmSwitches & ( 1 << RS_SLIDE ) ) ) {
			CG_Tip( TIP_SLIDE );
		}
	} else {
		tipGroundTime = 0;
	}
	// ground pound: a long time in the air
	if ( ps->groundEntityNum == ENTITYNUM_NONE ) {
		if ( !tipAirTime ) {
			tipAirTime = cg.time;
		}
		if ( cg.time - tipAirTime > 800 && ( cgs.rmSwitches & ( 1 << RS_SLAM ) ) ) {
			CG_Tip( TIP_SLAM );
		}
	} else {
		tipAirTime = 0;
	}
	// weapon wheel once there is something to choose from
	for ( i = WP_MACHINEGUN, n = 0 ; i < WP_NUM_WEAPONS ; i++ ) {
		if ( i != WP_GRAPPLING_HOOK && ( ps->stats[STAT_WEAPONS] & ( 1 << i ) ) ) {
			n++;
		}
	}
	if ( n >= 3 && cg_wheel.integer && CG_KeyForCommand( "+wheel" ) ) {
		CG_Tip( TIP_WHEEL );
	}
}

/*
=================
CG_DrawTip
=================
*/
void CG_DrawTip( void ) {
	vec4_t		col, bg;
	const char	*s;
	float		t, a, slide, w;

	if ( tipShowing < 0 || !cg_tips.integer ) {
		return;
	}
	t = cg.time - tipTime;
	if ( t < 0 || t >= TIP_TIME ) {
		return;
	}
	a = t < 200 ? t / 200.0f : ( t > TIP_TIME - 500 ? ( TIP_TIME - t ) / 500.0f : 1.0f );
	slide = t < 250 ? 12 * ( 1.0f - t / 250.0f ) * ( 1.0f - t / 250.0f ) : 0;
	s = CG_TipText( tipShowing );
	w = CG_DrawStrlen( s ) * 6 + 40;
	// rearmed: low in the view, clear of the crosshair, the dash pips and the hit callouts
	bg[0] = 0.02f; bg[1] = 0.03f; bg[2] = 0.05f; bg[3] = 0.55f * a;
	CG_FillRect( 320 - w / 2, 340 + slide, w, 16, bg );
	col[0] = 1.0f; col[1] = 0.8f; col[2] = 0.25f; col[3] = a;
	CG_FillRect( 320 - w / 2, 340 + slide, 2, 16, col );
	CG_DrawStringExt( 320 - w / 2 + 8, 344 + slide, "TIP", col, qtrue, qfalse, 6, 9, 0 );
	col[0] = col[1] = col[2] = 1.0f; col[3] = 0.9f * a;
	CG_DrawStringExt( 320 - w / 2 + 34, 344 + slide, s, col, qtrue, qfalse, 6, 9, 0 );
}
