/*
===========================================================================
REARMED -- DESCENT round 3 (ds-frenzy, cgame side): the kill-chain counter + cash-out tally
under the corner block, the frenzy edge glow, tier stings and the hot gem-tick layer, crit
glints / crunches / the gold hitmarker, the power spike wash + hum + countdown ring, loot beams
(drawn a little through walls, a shimmer when one comes into view) and spike pickups.
Game side: game/g_ds_frenzy.c. Nothing here draws in the middle of the screen except the small
countdown ring around the crosshair while a spike runs.
===========================================================================
*/
#include "cg_local.h"

#define FZC_POP_TIME		380
#define FZC_TALLY_TIME		2800
#define FZC_TALLY_COUNT		700		// the xp counts up this long
#define FZC_X				8
#define FZC_Y				142		// under the corner block (cg_roguelike.c CG_RL_DrawHud, y 100)
#define FZC_RING_R			30		// countdown ring radius around the crosshair (virtual px)
#define FZC_RING_DOTS		36
#define FZC_SHIMMER_GAP		350

typedef struct {
	// chain
	int		count, tier, expire, window, mult;
	int		popTime, tierTime, lastTier;
	// cash-out
	int		coCount, coXP, coTier, coTime;
	// spike
	int		spike, spikeEnd, spikeDur, spikeStart, spikeEndedTime, spikeEndedKind;
	// crit marker
	int		critTime, critConfirm;
	// beams: the s.time of the beam a shimmer was played for (per entity)
	int		beamShim[MAX_GENTITIES];
	int		lastShimmer;
} fzcState_t;

static fzcState_t	fzc;

static qhandle_t	fzc_beamShader, fzc_glintShader, fzc_ringShader, fzc_edgeH, fzc_edgeV, fzc_glow;
static sfxHandle_t	fzc_sndTier[DS_FRENZY_TIERS + 1], fzc_sndCash, fzc_sndCrit[2], fzc_sndBerserk, fzc_sndOverdrive;
static sfxHandle_t	fzc_sndHum[DS_SPIKE_NUM], fzc_sndSpikeEnd, fzc_sndShimmer[DS_BEAM_NUM], fzc_sndTickHot[12];

static const vec4_t fzc_tierColors[DS_FRENZY_TIERS + 1] = {
	{ 0.9f, 0.9f, 0.9f, 1 },
	{ 1.0f, 0.82f, 0.25f, 1 },		// FRENZY: gold
	{ 1.0f, 0.52f, 0.12f, 1 },		// RAMPAGE: orange
	{ 1.0f, 0.2f, 0.12f, 1 },		// MASSACRE: red
	{ 1.0f, 0.15f, 0.55f, 1 }		// BERSERK: hot pink
};
static const vec4_t fzc_beamColors[DS_BEAM_NUM] = {
	{ 0.85f, 0.9f, 1.0f, 1 },		// common white
	{ 0.2f, 0.5f, 1.0f, 1 },		// rare blue
	{ 0.68f, 0.25f, 1.0f, 1 },		// epic purple
	{ 1.0f, 0.6f, 0.1f, 1 }			// legendary orange / gold
};
static const vec4_t fzc_spikeColors[DS_SPIKE_NUM] = {
	{ 1, 1, 1, 1 },
	{ 1.0f, 0.12f, 0.06f, 1 },		// BERSERK: blood red
	{ 0.2f, 0.85f, 1.0f, 1 }		// OVERDRIVE: electric cyan
};
static const char *fzc_spikeNames[DS_SPIKE_NUM] = { "", "BERSERK", "OVERDRIVE" };
static const float fzc_beamHeight[DS_BEAM_NUM] = { 200, 280, 360, 460 };
static const float fzc_beamWidth[DS_BEAM_NUM] = { 6, 8, 10, 13 };

void CG_DS_FrenzyRegister( void ) {
	int		i;

	memset( &fzc, 0, sizeof( fzc ) );
	fzc_beamShader = trap_R_RegisterShader( "rearmed/fz_beam" );
	fzc_glintShader = trap_R_RegisterShader( "rearmed/fz_glint" );
	fzc_ringShader = trap_R_RegisterShader( "rearmed/fz_ring" );
	fzc_edgeH = trap_R_RegisterShaderNoMip( "rearmed/fz_edgeh" );
	fzc_edgeV = trap_R_RegisterShaderNoMip( "rearmed/fz_edgev" );
	fzc_glow = trap_R_RegisterShader( "rearmed/glow" );
	for ( i = 1 ; i <= DS_FRENZY_TIERS ; i++ ) {
		fzc_sndTier[i] = trap_S_RegisterSound( va( "sound/rearmed/frenzy/tier%i.wav", i ), qfalse );
	}
	fzc_sndCash = trap_S_RegisterSound( "sound/rearmed/frenzy/cashout.wav", qfalse );
	fzc_sndCrit[0] = trap_S_RegisterSound( "sound/rearmed/frenzy/crit1.wav", qfalse );
	fzc_sndCrit[1] = trap_S_RegisterSound( "sound/rearmed/frenzy/crit2.wav", qfalse );
	fzc_sndBerserk = trap_S_RegisterSound( "sound/rearmed/frenzy/berserk.wav", qfalse );
	fzc_sndOverdrive = trap_S_RegisterSound( "sound/rearmed/frenzy/overdrive.wav", qfalse );
	fzc_sndHum[DS_SPIKE_BERSERK] = trap_S_RegisterSound( "sound/rearmed/frenzy/hum_berserk.wav", qfalse );
	fzc_sndHum[DS_SPIKE_OVERDRIVE] = trap_S_RegisterSound( "sound/rearmed/frenzy/hum_overdrive.wav", qfalse );
	fzc_sndSpikeEnd = trap_S_RegisterSound( "sound/rearmed/frenzy/spike_end.wav", qfalse );
	for ( i = 0 ; i < DS_BEAM_NUM ; i++ ) {
		fzc_sndShimmer[i] = trap_S_RegisterSound( va( "sound/rearmed/frenzy/shimmer%i.wav", i ), qfalse );
	}
	for ( i = 0 ; i < 12 ; i++ ) {
		fzc_sndTickHot[i] = trap_S_RegisterSound( va( "sound/rearmed/frenzy/tickhot%i.wav", i ), qfalse );
	}
}

/*
===============================================================================

SERVER COMMANDS

===============================================================================
*/

qboolean CG_DS_FrenzyServerCommand( const char *cmd ) {
	int		count, tier, kind;

	if ( !Q_stricmp( cmd, "dsfz" ) ) {
		count = atoi( CG_Argv( 1 ) );
		tier = atoi( CG_Argv( 2 ) );
		if ( tier < 0 ) {
			tier = 0;
		} else if ( tier > DS_FRENZY_TIERS ) {
			tier = DS_FRENZY_TIERS;
		}
		if ( count > fzc.count ) {
			fzc.popTime = cg.time;
		}
		if ( tier > fzc.tier && tier > 0 ) {
			fzc.tierTime = cg.time;
			trap_S_StartLocalSound( fzc_sndTier[tier], CHAN_LOCAL_SOUND );
		}
		fzc.count = count;
		fzc.tier = tier;
		fzc.expire = atoi( CG_Argv( 3 ) );
		fzc.window = atoi( CG_Argv( 4 ) );
		fzc.mult = atoi( CG_Argv( 5 ) );
		fzc.coTime = 0;		// a new chain hides the old tally
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsfzend" ) ) {
		count = atoi( CG_Argv( 1 ) );
		if ( count > 0 ) {
			fzc.coCount = count;
			fzc.coXP = atoi( CG_Argv( 2 ) );
			fzc.coTier = atoi( CG_Argv( 4 ) );
			fzc.coTime = cg.time;
			trap_S_StartLocalSound( fzc_sndCash, CHAN_LOCAL_SOUND );
		}
		fzc.count = 0;
		fzc.tier = 0;
		fzc.expire = 0;
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsfzspk" ) ) {
		kind = atoi( CG_Argv( 1 ) );
		if ( kind < 0 || kind >= DS_SPIKE_NUM ) {
			kind = 0;
		}
		if ( kind && kind != fzc.spike ) {
			fzc.spikeStart = cg.time;
			trap_S_StartLocalSound( kind == DS_SPIKE_BERSERK ? fzc_sndBerserk : fzc_sndOverdrive, CHAN_ANNOUNCER );
		} else if ( !kind && fzc.spike ) {
			fzc.spikeEndedTime = cg.time;
			fzc.spikeEndedKind = fzc.spike;
			trap_S_StartLocalSound( fzc_sndSpikeEnd, CHAN_LOCAL_SOUND );
		}
		fzc.spike = kind;
		fzc.spikeEnd = atoi( CG_Argv( 2 ) );
		fzc.spikeDur = atoi( CG_Argv( 3 ) );
		if ( fzc.spikeDur <= 0 ) {
			fzc.spikeDur = 1;
		}
		return qtrue;
	}
	return qfalse;
}

/*
===============================================================================

HIT FEEDBACK: crits

===============================================================================
*/

// CG_HitConfirm: the latest hit confirm's flags
void CG_DS_FrenzyHitConfirm( int flags ) {
	if ( flags & HIT_CRIT ) {
		fzc.critTime = cg.time;
		fzc.critConfirm = cg.time;
	} else {
		fzc.critConfirm = 0;
	}
}

// CG_DrawHitFeedback: the hitmarker turns gold for a crit
qboolean CG_DS_FrenzyCritMarker( void ) {
	return fzc.critConfirm && fzc.critConfirm == cg.hitMarkerTime;
}

// the gem tick layer: louder and brighter while a chain runs
void CG_DS_FrenzyTick( int note ) {
	if ( fzc.tier <= 0 || fzc.count <= 0 ) {
		return;
	}
	if ( note < 0 ) {
		note = 0;
	}
	note += fzc.tier - 1;
	if ( note > 11 ) {
		note = 11;
	}
	trap_S_StartLocalSound( fzc_sndTickHot[note], CHAN_AUTO );
}

static void CGFZ_Glint( const vec3_t org, float size, const float *color, int life ) {
	localEntity_t	*le;

	le = CG_AllocLocalEntity();
	le->leType = LE_FX_FLASH;
	le->startTime = cg.time;
	le->endTime = cg.time + life;
	le->lifeRate = 1.0f / life;
	le->radius = size;
	le->endRadius = size * 1.8f;
	VectorCopy( color, le->color );
	VectorCopy( org, le->refEntity.origin );
	le->refEntity.customShader = fzc_glintShader;
	le->refEntity.reType = RT_SPRITE;
	le->refEntity.rotation = random() * 40 - 20;
}

/*
=================
CG_DS_FrenzyEvent

EV_DS_FRENZY: a crit glint + crunch at the impact; a spike burst around the player
=================
*/
void CG_DS_FrenzyEvent( centity_t *cent, vec3_t position ) {
	entityState_t	*es = &cent->currentState;
	static const vec3_t	gold = { 1.0f, 0.82f, 0.3f };
	static const vec3_t	white = { 1.0f, 0.95f, 0.8f };
	vec3_t			up = { 0, 0, 1 }, dir, col;
	qboolean		mine, onMe;

	switch ( es->eventParm ) {
	case DSFZ_CRIT:
		mine = cg.snap && es->otherEntityNum == cg.snap->ps.clientNum;
		onMe = cg.snap && es->otherEntityNum2 == cg.snap->ps.clientNum;
		VectorSubtract( cg.refdef.vieworg, position, dir );
		VectorNormalize( dir );
		CGFZ_Glint( position, es->generic1 ? 16 : 11, es->generic1 ? white : gold, es->generic1 ? 260 : 200 );
		CGFZ_Glint( position, 5, white, 120 );
		if ( cg_fx.integer ) {
			CG_FxSparks( position, dir, gold, es->generic1 ? 14 : 8, 340, 320, 0.35f, 0 );
		}
		CG_GoreLight( position, es->generic1 ? 160 : 110, 1.0f, 0.8f, 0.35f, 120 );
		if ( mine || onMe ) {
			trap_S_StartLocalSound( fzc_sndCrit[rand() & 1], CHAN_LOCAL_SOUND );
		} else {
			CG_FxSound( position, ENTITYNUM_WORLD, CHAN_AUTO, fzc_sndCrit[rand() & 1] );
		}
		break;
	case DSFZ_SPIKE:
		if ( es->generic1 > 0 && es->generic1 < DS_SPIKE_NUM ) {
			VectorCopy( fzc_spikeColors[es->generic1], col );
			// sparks around someone else (or a third person view): first person they'd fly through the eye
			if ( cg_fx.integer && ( !cg.snap || es->otherEntityNum != cg.snap->ps.clientNum || cg.renderingThirdPerson ) ) {
				CG_FxSparks( position, up, col, 30, 520, 700, 0.1f, 8 );
			}
			CG_GoreLight( position, 380, col[0], col[1], col[2], 400 );
			if ( cg.snap && es->otherEntityNum == cg.snap->ps.clientNum ) {
				CG_FxPunch( 3.0f );
			}
		}
		break;
	default:
		break;
	}
}

/*
===============================================================================

ENTITIES: loot beams, spike pickups

===============================================================================
*/

static void CGFZ_Sprite( const vec3_t org, float radius, const float *rgb, float alpha, qhandle_t shader, float rotation, int renderfx ) {
	refEntity_t	re;

	memset( &re, 0, sizeof( re ) );
	re.reType = RT_SPRITE;
	VectorCopy( org, re.origin );
	re.radius = radius;
	re.rotation = rotation;
	re.customShader = shader;
	re.shaderRGBA[0] = (byte)( 255 * CG_Clamp01( rgb[0] * alpha ) );
	re.shaderRGBA[1] = (byte)( 255 * CG_Clamp01( rgb[1] * alpha ) );
	re.shaderRGBA[2] = (byte)( 255 * CG_Clamp01( rgb[2] * alpha ) );
	re.shaderRGBA[3] = 255;
	re.renderfx = RF_NOSHADOW | renderfx;
	trap_R_AddRefEntityToScene( &re );
}

// a vertical camera-facing quad from base up height, half width w (additive, colour x alpha)
static void CGFZ_Pillar( const vec3_t base, float height, float w, const float *rgb, float alpha ) {
	polyVert_t	v[4];
	vec3_t		dir, right, up = { 0, 0, 1 }, top;
	byte		c[3];
	int			i;

	VectorSubtract( cg.refdef.vieworg, base, dir );
	dir[2] = 0;
	if ( VectorNormalize( dir ) < 0.001f ) {
		VectorCopy( cg.refdef.viewaxis[1], right );
	} else {
		CrossProduct( up, dir, right );
	}
	for ( i = 0 ; i < 3 ; i++ ) {
		c[i] = (byte)( 255 * CG_Clamp01( rgb[i] * alpha ) );
	}
	VectorMA( base, height, up, top );
	VectorMA( top, -w, right, v[0].xyz );
	VectorMA( top, w, right, v[1].xyz );
	VectorMA( base, w, right, v[2].xyz );
	VectorMA( base, -w, right, v[3].xyz );
	v[0].st[0] = 0; v[0].st[1] = 0;
	v[1].st[0] = 1; v[1].st[1] = 0;
	v[2].st[0] = 1; v[2].st[1] = 1;
	v[3].st[0] = 0; v[3].st[1] = 1;
	for ( i = 0 ; i < 4 ; i++ ) {
		v[i].modulate[0] = c[0];
		v[i].modulate[1] = c[1];
		v[i].modulate[2] = c[2];
		v[i].modulate[3] = 255;
	}
	trap_R_AddPolyToScene( fzc_beamShader, 4, v );
}

// a flat ring on the floor
static void CGFZ_FloorRing( const vec3_t org, float radius, const float *rgb, float alpha ) {
	polyVert_t	v[4];
	int			i;
	byte		c[3];
	static const float	sx[4] = { -1, 1, 1, -1 }, sy[4] = { -1, -1, 1, 1 };

	for ( i = 0 ; i < 3 ; i++ ) {
		c[i] = (byte)( 255 * CG_Clamp01( rgb[i] * alpha ) );
	}
	for ( i = 0 ; i < 4 ; i++ ) {
		v[i].xyz[0] = org[0] + sx[i] * radius;
		v[i].xyz[1] = org[1] + sy[i] * radius;
		v[i].xyz[2] = org[2];
		v[i].st[0] = ( sx[i] + 1 ) * 0.5f;
		v[i].st[1] = ( sy[i] + 1 ) * 0.5f;
		v[i].modulate[0] = c[0];
		v[i].modulate[1] = c[1];
		v[i].modulate[2] = c[2];
		v[i].modulate[3] = 255;
	}
	trap_R_AddPolyToScene( fzc_ringShader, 4, v );
}

static void CGFZ_Beam( centity_t *cent ) {
	entityState_t	*s = &cent->currentState;
	int				r = s->modelindex2, n = s->number, i, age;
	vec3_t			base, p, d;
	const float		*col;
	float			h, w, grow, pulse, dist, a, through, fwd;
	trace_t			tr;
	centity_t		*t;
	refEntity_t		re;

	if ( r < 0 || r >= DS_BEAM_NUM ) {
		r = 0;
	}
	col = fzc_beamColors[r];
	// follow the drop smoothly when it is in the snapshot
	t = s->otherEntityNum >= 0 && s->otherEntityNum < ENTITYNUM_MAX_NORMAL ? &cg_entities[s->otherEntityNum] : NULL;
	if ( t && t->currentValid && t->currentState.eType == ET_GENERAL ) {
		VectorCopy( t->lerpOrigin, base );
	} else {
		VectorCopy( cent->lerpOrigin, base );
	}
	base[2] -= 4;
	age = cg.time - s->time;
	if ( age < 0 ) {
		age = 0;
	}
	grow = CG_Clamp01( age / 450.0f );
	grow = 1.0f - ( 1.0f - grow ) * ( 1.0f - grow );
	pulse = 0.85f + 0.15f * sin( cg.time * 0.004f + n );
	h = fzc_beamHeight[r] * grow;
	w = fzc_beamWidth[r];
	// a bright flare as it appears
	a = age < 300 ? 1.0f + 1.5f * ( 1.0f - age / 300.0f ) : 1.0f;

	CGFZ_Pillar( base, h, w * ( 1.0f + 0.1f * pulse ), col, 0.9f * pulse * a );
	CGFZ_Pillar( base, h * 0.75f, w * 3.2f, col, 0.28f * pulse );
	CGFZ_FloorRing( base, 16 + 6 * r + 5 * sin( cg.time * 0.005f + n ), col, 0.55f * pulse );
	// motes drifting up the pillar
	for ( i = 0 ; i < 2 + r ; i++ ) {
		float	ph = cg.time * ( 0.0006f + 0.0001f * i ) + i * 0.37f + n * 0.13f;

		ph -= (int)ph;		// (no fmod in the QVM libc)

		VectorCopy( base, p );
		p[0] += ( w + 4 ) * sin( i * 2.1f + cg.time * 0.002f );
		p[1] += ( w + 4 ) * cos( i * 2.1f + cg.time * 0.002f );
		p[2] += h * 0.8f * ph;
		CGFZ_Sprite( p, 3.0f + r, col, 0.8f * ( 1.0f - ph ), fzc_glow, 0, 0 );
	}

	VectorSubtract( base, cg.refdef.vieworg, d );
	dist = VectorNormalize( d );
	fwd = DotProduct( d, cg.refdef.viewaxis[0] );

	// a soft light at the foot (near ones only)
	if ( dist < 1400 ) {
		VectorCopy( base, p );
		p[2] += 20;
		trap_R_AddLightToScene( p, 90 + 30 * r + 20 * pulse, col[0], col[1], col[2] );
	}

	// through walls: a faint glow down the pillar when the beam itself is hidden
	VectorCopy( base, p );
	p[2] += h * 0.45f;
	CG_Trace( &tr, cg.refdef.vieworg, NULL, NULL, p, cg.snap ? cg.snap->ps.clientNum : ENTITYNUM_NONE, MASK_SOLID );
	if ( tr.fraction < 1.0f && dist > 120 ) {
		through = ( r == DS_BEAM_LEGENDARY ? 0.32f : r == DS_BEAM_EPIC ? 0.26f : 0.2f ) * ( dist > 3000 ? 0.6f : 1.0f );
		// a thin line of the pillar (a rail core: an entity, so the depth hack applies) ...
		memset( &re, 0, sizeof( re ) );
		re.reType = RT_RAIL_CORE;
		re.customShader = cgs.media.railCoreShader;
		VectorCopy( base, re.origin );
		VectorCopy( base, re.oldorigin );
		re.oldorigin[2] += h * 0.85f;
		re.shaderRGBA[0] = (byte)( 255 * CG_Clamp01( col[0] * through * 1.6f * pulse ) );
		re.shaderRGBA[1] = (byte)( 255 * CG_Clamp01( col[1] * through * 1.6f * pulse ) );
		re.shaderRGBA[2] = (byte)( 255 * CG_Clamp01( col[2] * through * 1.6f * pulse ) );
		re.shaderRGBA[3] = 255;
		re.renderfx = RF_NOSHADOW | RF_DEPTHHACK;
		trap_R_AddRefEntityToScene( &re );
		// ... and soft glows along it, about the same size on screen at any distance
		for ( i = 0 ; i < 3 ; i++ ) {
			VectorCopy( base, p );
			p[2] += h * ( 0.08f + 0.3f * i );
			a = dist * 0.03f;
			CGFZ_Sprite( p, ( a < 20 ? 20 : a ) * ( 1.0f - 0.25f * i ), col, through * pulse * ( 1.0f - 0.25f * i ), fzc_glow, 0, RF_DEPTHHACK );
		}
	}

	// a shimmer the first time it is in view (once per beam)
	if ( fzc.beamShim[n] != s->time && fwd > 0.55f && ( tr.fraction >= 1.0f || dist < 900 ) ) {
		fzc.beamShim[n] = s->time;
		if ( cg.time - fzc.lastShimmer >= FZC_SHIMMER_GAP || r >= DS_BEAM_EPIC ) {
			fzc.lastShimmer = cg.time;
			trap_S_StartLocalSound( fzc_sndShimmer[r], CHAN_AUTO );
		}
	}
}

static void CGFZ_SpikePickup( centity_t *cent ) {
	entityState_t	*s = &cent->currentState;
	int				k = s->modelindex2;
	vec3_t			org;
	const float		*col;
	float			pulse, a = 1.0f;

	if ( k <= 0 || k >= DS_SPIKE_NUM ) {
		k = DS_SPIKE_OVERDRIVE;
	}
	col = fzc_spikeColors[k];
	if ( s->time2 && s->time2 - cg.time < 5000 && ( ( cg.time / 150 ) & 1 ) ) {
		a = 0.35f;		// about to vanish: blink
	}
	VectorCopy( cent->lerpOrigin, org );
	org[2] += 18 + 4 * sin( cg.time * 0.005f );
	pulse = 0.5f + 0.5f * sin( cg.time * 0.012f );
	CGFZ_Sprite( org, 14 + 4 * pulse, col, a, fzc_glintShader, cg.time * 0.15f, 0 );
	CGFZ_Sprite( org, 10, col, 0.9f * a, fzc_glow, 0, 0 );
	CGFZ_Sprite( org, 18 + 10 * pulse, col, 0.5f * a * ( 1 - pulse ), fzc_ringShader, 0, 0 );
	trap_R_AddLightToScene( org, 150 + 50 * pulse, col[0], col[1], col[2] );
}

// CG_General: qtrue when it was one of ours
qboolean CG_DS_FrenzyEntity( centity_t *cent ) {
	switch ( cent->currentState.generic1 ) {
	case RM_ENT_BEAM:
		CGFZ_Beam( cent );
		return qtrue;
	case RM_ENT_SPIKE:
		CGFZ_SpikePickup( cent );
		return qtrue;
	default:
		return qfalse;
	}
}

/*
===============================================================================

2D: edge glow / spike wash, the countdown ring, the chain counter and the cash-out tally

===============================================================================
*/

// additive glow along all four screen edges (real pixels, never the centre)
static void CGFZ_Edges( const float *rgb, float alpha, float side, float topBot ) {
	vec4_t	c;
	float	w = cgs.glconfig.vidHeight * side, h = cgs.glconfig.vidHeight * topBot;

	if ( alpha <= 0.003f ) {
		return;
	}
	c[0] = rgb[0] * alpha; c[1] = rgb[1] * alpha; c[2] = rgb[2] * alpha; c[3] = 1;
	trap_R_SetColor( c );
	trap_R_DrawStretchPic( 0, 0, w, cgs.glconfig.vidHeight, 0, 0, 1, 1, fzc_edgeH );
	trap_R_DrawStretchPic( cgs.glconfig.vidWidth - w, 0, w, cgs.glconfig.vidHeight, 1, 0, 0, 1, fzc_edgeH );
	if ( h > 0 ) {
		trap_R_DrawStretchPic( 0, 0, cgs.glconfig.vidWidth, h, 0, 0, 1, 1, fzc_edgeV );
		trap_R_DrawStretchPic( 0, cgs.glconfig.vidHeight - h, cgs.glconfig.vidWidth, h, 0, 1, 1, 0, fzc_edgeV );
	}
	trap_R_SetColor( NULL );
}

static void CGFZ_Text( float x, float y, const char *s, const float *color, float cw, float ch, int align ) {
	float	w = CG_DrawStrlen( s ) * cw;

	if ( align == 0 ) {
		x -= w * 0.5f;
	} else if ( align > 0 ) {
		x -= w;
	}
	CG_DrawStringExt( (int)x, (int)y, s, color, qtrue, qtrue, (int)( cw + 0.5f ), (int)( ch + 0.5f ), 0 );
}

static void CGFZ_Ring( float frac, const float *rgb, float alpha ) {
	int		i, lit = (int)( frac * FZC_RING_DOTS + 0.999f );
	float	ang, cx = 320 + cg_crosshairX.integer, cy = 240 + cg_crosshairY.integer, s;
	vec4_t	c;

	for ( i = 0 ; i < FZC_RING_DOTS ; i++ ) {
		ang = ( (float)i / FZC_RING_DOTS ) * 2 * M_PI;
		if ( i < lit ) {
			c[0] = rgb[0]; c[1] = rgb[1]; c[2] = rgb[2]; c[3] = alpha;
			s = 1.6f;
		} else {
			c[0] = c[1] = c[2] = 0.3f; c[3] = alpha * 0.25f;
			s = 1.0f;
		}
		CG_FillRect( cx + sin( ang ) * FZC_RING_R - s * 0.5f, cy - cos( ang ) * FZC_RING_R - s * 0.5f, s, s, c );
	}
}

static float CGFZ_EaseOut( float t ) {
	t = 1.0f - CG_Clamp01( t );
	return 1.0f - t * t * t;
}

void CG_DS_FrenzyDraw2D( void ) {
	vec4_t		col, bg;
	float		left, frac, pulse, a, p, scale, x, y, beat, w;
	int			t, xp;
	const char	*s;
	playerState_t	*ps;

	if ( !cg.snap ) {
		return;
	}
	ps = &cg.snap->ps;
	if ( ps->persistant[PERS_TEAM] == TEAM_SPECTATOR || ps->pm_type == PM_INTERMISSION ) {
		return;
	}
	// a spike's hum (a loop at the listener)
	if ( fzc.spike && fzc_sndHum[fzc.spike] && ps->stats[STAT_HEALTH] > 0 ) {
		trap_S_AddLoopingSound( ENTITYNUM_WORLD, cg.refdef.vieworg, vec3_origin, fzc_sndHum[fzc.spike] );
	}
	if ( ps->stats[STAT_HEALTH] <= 0 || cg.showScores ) {
		return;
	}

	// ---- edges: the chain glow, the spike wash on top
	if ( fzc.tier > 0 && fzc.count > 0 ) {
		left = fzc.expire - cg.time;
		beat = 0.5f + 0.5f * sin( cg.time * ( 0.006f + 0.0025f * fzc.tier ) );
		a = ( 0.05f + 0.045f * fzc.tier ) * ( 0.7f + 0.3f * beat );
		if ( left < 1200 ) {
			a *= CG_Clamp01( left / 1200.0f ) * ( 0.6f + 0.4f * ( ( cg.time / 120 ) & 1 ) );		// running out: flicker
		}
		p = (float)( cg.time - fzc.tierTime ) / 500.0f;
		if ( p >= 0 && p < 1 ) {
			a += 0.35f * ( 1.0f - p );		// a tier up flares
		}
		CGFZ_Edges( fzc_tierColors[fzc.tier], a, 0.09f + 0.025f * fzc.tier, 0.03f + 0.01f * fzc.tier );
	}
	if ( fzc.spike && fzc.spikeEnd - cg.time > 0 ) {
		left = fzc.spikeEnd - cg.time;
		pulse = 0.5f + 0.5f * sin( cg.time * ( fzc.spike == DS_SPIKE_BERSERK ? 0.016f : 0.03f ) );
		a = 0.26f + 0.12f * pulse;
		p = (float)( cg.time - fzc.spikeStart ) / 600.0f;
		if ( p >= 0 && p < 1 ) {
			a += 0.6f * ( 1.0f - p );		// the start: a big wash
		}
		if ( left < 1500 && ( ( cg.time / 110 ) & 1 ) ) {
			a *= 0.45f;					// ending: blink
		}
		CGFZ_Edges( fzc_spikeColors[fzc.spike], a, 0.2f, 0.07f );
		// the countdown ring, small, around the crosshair
		frac = CG_Clamp01( left / (float)fzc.spikeDur );
		CGFZ_Ring( frac, fzc_spikeColors[fzc.spike], 0.85f );
	} else if ( fzc.spikeEndedTime && cg.time - fzc.spikeEndedTime < 400 ) {
		CGFZ_Edges( fzc_spikeColors[fzc.spikeEndedKind], 0.3f * ( 1.0f - ( cg.time - fzc.spikeEndedTime ) / 400.0f ), 0.12f, 0 );
	}

	// ---- the crit: a small gold star on the crosshair for a moment
	if ( fzc.critTime && cg.time - fzc.critTime < 220 && cg_hitMarkers.integer ) {
		p = ( cg.time - fzc.critTime ) / 220.0f;
		w = 30 + 16 * ( 1.0f - p );
		col[0] = 1.0f * ( 1 - p ); col[1] = 0.8f * ( 1 - p ); col[2] = 0.25f * ( 1 - p ); col[3] = 1;
		trap_R_SetColor( col );
		CG_DrawPic( 320 - w * 0.5f + cg_crosshairX.integer, 240 - w * 0.5f + cg_crosshairY.integer, w, w, fzc_glintShader );
		trap_R_SetColor( NULL );
	}

	// ---- the chain counter under the corner block
	CG_HudAnchor( -1 );
	x = FZC_X;
	y = FZC_Y;
	if ( fzc.count > 0 ) {
		left = fzc.expire - cg.time;
		frac = fzc.window > 0 ? CG_Clamp01( left / (float)fzc.window ) : 0;
		Vector4Copy( fzc_tierColors[fzc.tier], col );
		p = (float)( cg.time - fzc.popTime ) / FZC_POP_TIME;
		scale = p >= 0 && p < 1 ? 1.0f + 0.45f * ( 1.0f - CGFZ_EaseOut( p ) ) : 1.0f;
		if ( p >= 0 && p < 0.5f ) {
			a = 1.0f - p * 2;		// pop: white hot, settling into the tier colour
			col[0] += ( 1 - col[0] ) * a;
			col[1] += ( 1 - col[1] ) * a;
			col[2] += ( 1 - col[2] ) * a;
		}
		bg[0] = bg[1] = bg[2] = 0; bg[3] = 0.28f;
		s = va( "x%i", fzc.count );
		w = CG_DrawStrlen( s ) * 11 + 5 + CG_DrawStrlen( fzc.tier > 0 ? ds_frenzyTierNames[fzc.tier] : "CHAIN" ) * 6 + 6;
		CG_FillRect( x - 3, y - 3, w < 96 ? 96 : w, 30, bg );
		col[3] = 0.95f;
		CG_DrawStringExt( (int)( x - 2 * ( scale - 1 ) * 8 ), (int)( y - 8 * ( scale - 1 ) ), s, col, qtrue, qtrue,
			(int)( 11 * scale ), (int)( 16 * scale ), 0 );
		w = CG_DrawStrlen( s ) * 11 + 5;
		CGFZ_Text( x + w, y, fzc.tier > 0 ? ds_frenzyTierNames[fzc.tier] : "CHAIN", col, 6, 8, -1 );
		if ( fzc.tier > 0 ) {
			col[0] = col[1] = col[2] = 0.9f; col[3] = 0.85f;
			CGFZ_Text( x + w, y + 10, va( "GEMS x%i.%02i", fzc.mult / 100, fzc.mult % 100 ), col, 4, 6, -1 );
		} else {
			col[0] = col[1] = col[2] = 0.7f; col[3] = 0.7f;
			CGFZ_Text( x + w, y + 10, va( "%i TO FRENZY", ds_frenzyTierKills[1] - fzc.count ), col, 4, 6, -1 );
		}
		// the timer: drains, flickers near the end
		bg[3] = 0.5f;
		CG_FillRect( x - 1, y + 19, 92, 4, bg );
		Vector4Copy( fzc_tierColors[fzc.tier], col );
		col[3] = left < 1000 && ( ( cg.time / 100 ) & 1 ) ? 0.45f : 0.95f;
		CG_FillRect( x, y + 20, 90 * frac, 2, col );
		y += 32;
	} else if ( fzc.coTime && cg.time - fzc.coTime < FZC_TALLY_TIME ) {
		// the cash-out: slides in, counts up, slides out to the left
		t = cg.time - fzc.coTime;
		p = t / 200.0f;
		x -= 40 * ( 1.0f - CGFZ_EaseOut( p ) );
		if ( t > FZC_TALLY_TIME - 300 ) {
			x -= 120 * ( ( t - ( FZC_TALLY_TIME - 300 ) ) / 300.0f );
		}
		a = CG_Clamp01( p ) * CG_Clamp01( ( FZC_TALLY_TIME - t ) / 300.0f );
		xp = (int)( fzc.coXP * CG_Clamp01( (float)t / FZC_TALLY_COUNT ) + 0.5f );
		bg[0] = bg[1] = bg[2] = 0; bg[3] = 0.35f * a;
		CG_FillRect( x - 3, y - 3, 110, 28, bg );
		Vector4Copy( fzc_tierColors[fzc.coTier < 0 ? 0 : fzc.coTier > DS_FRENZY_TIERS ? DS_FRENZY_TIERS : fzc.coTier], col );
		col[3] = a;
		CGFZ_Text( x, y, va( "x%i CHAIN", fzc.coCount ), col, 8, 11, -1 );
		col[0] = 0.45f; col[1] = 0.85f; col[2] = 1.0f;
		if ( t < FZC_TALLY_COUNT + 150 && ( ( t / 60 ) & 1 ) ) {
			col[0] = col[1] = col[2] = 1;		// counting: sparkle
		}
		CGFZ_Text( x, y + 13, va( "+%i XP", xp ), col, 6, 9, -1 );
		y += 30;
	}
	// a running spike: its name and seconds (the ring by the crosshair carries the rest)
	if ( fzc.spike && fzc.spikeEnd - cg.time > 0 ) {
		left = fzc.spikeEnd - cg.time;
		bg[0] = bg[1] = bg[2] = 0; bg[3] = 0.4f;
		CG_FillRect( x - 3, y - 2, 96, 12, bg );
		Vector4Copy( fzc_spikeColors[fzc.spike], col );
		col[0] += ( 1 - col[0] ) * 0.45f;		// lighter: it sits on its own coloured wash
		col[1] += ( 1 - col[1] ) * 0.45f;
		col[2] += ( 1 - col[2] ) * 0.45f;
		col[3] = 1.0f;
		t = left > 0 ? (int)left : 0;
		CGFZ_Text( x, y, va( "%s %i.%i", fzc_spikeNames[fzc.spike], t / 1000, ( t % 1000 ) / 100 ), col, 6, 8, -1 );
	}
	CG_HudAnchor( 0 );
}
