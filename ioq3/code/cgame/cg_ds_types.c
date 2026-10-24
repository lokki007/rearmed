/*
===========================================================================
REARMED -- round 4 ENEMY ARCHETYPES (en-types), cgame side: how each type LOOKS and SOUNDS.
The type of a client: CG_EN( client )->arch (CS_DSENEMY). Tells live on the enemy, never in
the middle of the screen; the type's name only at the edges (the arrival strip, TAB).

- body: the model comes from the server (bg_ds_types.c); here the juggernaut's 1.25x scale,
  the mimic's inky shell, the juggernaut's gunmetal shell, the kamikaze's hot glow
- tells: the sniper's red laser + head glint during its 0.7 s wind-up, the kamikaze's beeps
  (faster as it closes on you, frantic on the fuse), the shield bearer's energy riot shield
  (raised in front / lowered to the side), the medic's green back light, the summoner's ring
- effects (RM_ENT_DSTYPE): heal beams, raise rituals, blink flashes, kamikaze blasts, shield
  sparks
- the juggernaut's ground-shaking footsteps
===========================================================================
*/
#include "cg_local.h"
#include "../game/bg_ds_types.h"

typedef struct {
	int		lastFlags;
	int		tgStart;		// cg.time DSEF_TELEGRAPH came on
	int		nextBeep, beepFlash;
	int		shieldAt;		// cg.time the shield went up / down (swing)
	int		lastShield;
} cdtClient_t;

static struct {
	qhandle_t	flare, ring, beam, shell, dark, shieldFace, shieldModel, shieldFaceModel;
	sfxHandle_t	snipe, beep, fuse, boom, heal, blink, ritual, block, step, shieldUp;
	cdtClient_t	cl[MAX_CLIENTS];
} cdt;

void CG_DS_TypesRegister( void ) {
	memset( &cdt, 0, sizeof( cdt ) );
	cdt.flare = trap_R_RegisterShader( "rearmed/dsTypeFlare" );
	cdt.ring = trap_R_RegisterShader( "rearmed/dsTypeRing" );
	cdt.beam = trap_R_RegisterShader( "rearmed/dsTypeBeam" );
	cdt.shell = trap_R_RegisterShader( "rearmed/dsTypeShell" );
	cdt.dark = trap_R_RegisterShader( "rearmed/dsTypeDark" );
	cdt.shieldFace = trap_R_RegisterShader( "rearmed/dsTypeShieldFace" );
	cdt.shieldModel = trap_R_RegisterModel( "models/rearmed/types/shield.md3" );
	cdt.shieldFaceModel = trap_R_RegisterModel( "models/rearmed/types/shield_face.md3" );
	cdt.snipe = trap_S_RegisterSound( "sound/rearmed/types/snipe_charge.wav", qfalse );
	cdt.beep = trap_S_RegisterSound( "sound/rearmed/types/beep.wav", qfalse );
	cdt.fuse = trap_S_RegisterSound( "sound/rearmed/types/fuse.wav", qfalse );
	cdt.boom = trap_S_RegisterSound( "sound/rearmed/types/boom.wav", qfalse );
	cdt.heal = trap_S_RegisterSound( "sound/rearmed/types/heal_loop.wav", qfalse );
	cdt.blink = trap_S_RegisterSound( "sound/rearmed/types/blink.wav", qfalse );
	cdt.ritual = trap_S_RegisterSound( "sound/rearmed/types/ritual.wav", qfalse );
	cdt.block = trap_S_RegisterSound( "sound/rearmed/types/shield_block.wav", qfalse );
	cdt.step = trap_S_RegisterSound( "sound/rearmed/types/jugg_step.wav", qfalse );
	cdt.shieldUp = trap_S_RegisterSound( "sound/rearmed/types/shield_up.wav", qfalse );
}

static int CDT_Arch( int client ) {
	return CG_EN( client )->arch;
}

/*
en-tune: this run's type adaptation, from serverinfo g_dsTypeAdaptNow "<floor>:<arch>,<arch>"
(the sniper's shorter laser ramp, the run-start note)
*/
static int		cdtAdaptMask, cdtAdaptFloor, cdtAdaptCheck, cdtAdaptShown;
static char		cdtAdaptStr[32];
static int		cdtAdaptList[DST_ADAPT_MAX], cdtAdaptNum;

static int CDT_Adapted( void ) {
	const char	*s;

	if ( cg.time < cdtAdaptCheck && cg.time >= cdtAdaptCheck - 1000 ) {
		return cdtAdaptMask;
	}
	cdtAdaptCheck = cg.time + 1000;
	s = Info_ValueForKey( CG_ConfigString( CS_SERVERINFO ), "g_dsTypeAdaptNow" );
	if ( Q_stricmp( s, cdtAdaptStr ) ) {
		Q_strncpyz( cdtAdaptStr, s, sizeof( cdtAdaptStr ) );
		// the mask, and the order as published (heaviest first)
		cdtAdaptMask = BG_DS_TypeAdaptParse( s, &cdtAdaptFloor, cdtAdaptList, DST_ADAPT_MAX, &cdtAdaptNum );
		cdtAdaptShown = 0;
	}
	return cdtAdaptMask;
}

#define CDT_NOTE_DELAY	2500	// after the floor starts (the arrivals and the floor strip first)
#define CDT_NOTE_TIME	9000	// each line on screen

/*
=================
CG_DS_TypesDraw2D

en-tune: at a run's start (floor 1) the types that remember the player get one short line
each on the shared right-edge stack (cg_edge.c), never centre screen:
"THE SNIPERS REMEMBER YOU" / "their glint is shorter. Strafe early."
=================
*/
void CG_DS_TypesDraw2D( void ) {
	float		t, al, slide, x = 620, y, y0, h;
	char		l1[64], l2[96];
	int			i, a, k, start;
	static const vec3_t	bg = { 0.04f, 0.04f, 0.05f }, body = { 0.9f, 0.88f, 0.84f };

	if ( !cg.snap || cg.predictedPlayerState.pm_type == PM_INTERMISSION || cg.showScores ) {
		return;
	}
	if ( !CDT_Adapted() || cdtAdaptFloor != 1 || !cdtAdaptNum ) {
		return;
	}
	if ( cg.predictedPlayerState.pm_type != PM_NORMAL && !cdtAdaptShown ) {
		return;		// wait until the player is in
	}
	if ( !cdtAdaptShown ) {
		cdtAdaptShown = cg.time + CDT_NOTE_DELAY;
	}
	start = cdtAdaptShown;
	if ( cg.time < start ) {
		return;
	}
	h = 25;
	k = 0;
	for ( i = 0 ; i < cdtAdaptNum ; i++ ) {
		t = cg.time - start - i * 1200;
		if ( t >= 0 && t <= CDT_NOTE_TIME ) {
			k++;
		}
	}
	if ( !k ) {
		return;
	}
	y0 = CG_EdgeSlot( EDGE_TYPES, k * h + ( k - 1 ) * 4 );		// en-screens' right-edge stack, once a frame
	k = 0;
	for ( i = 0 ; i < cdtAdaptNum ; i++ ) {
		a = cdtAdaptList[i];
		t = cg.time - start - i * 1200;		// the second one a beat later
		if ( t < 0 || t > CDT_NOTE_TIME ) {
			continue;
		}
		al = CG_Clamp01( t / 200.0f ) * CG_Clamp01( ( CDT_NOTE_TIME - t ) / 600.0f );
		slide = 18.0f * ( 1.0f - CG_Clamp01( t / 200.0f ) );
		Com_sprintf( l1, sizeof( l1 ), "THE %s REMEMBER YOU", BG_DS_TypePlural( a ) );
		Q_strupr( l1 );
		Q_strncpyz( l2, BG_DS_TypeAdaptHint( a ), sizeof( l2 ) );
		if ( l2[0] >= 'a' && l2[0] <= 'z' ) {
			l2[0] += 'A' - 'a';
		}
		y = y0 + 3 + k * ( h + 4 );
		k++;
		CG_HudAnchor( 1 );
		CG_EdgeCard( x + slide, y, h, al, l1, l2, ds_types[a].rgb, bg, 0.35f, body );		// the type's colour bar
		CG_HudAnchor( 0 );
	}
}

// the type's name for the screen edges ("" plain)
const char *CG_DS_TypeName( int client ) {
	return BG_DS_TypeName( CDT_Arch( client ) );
}

// TAB scoreboard: "  ^1SNIPER" after the name ("" plain)
const char *CG_DS_TypeTag( int client ) {
	int		a = CDT_Arch( client );

	if ( a <= 0 || a >= DSAR_NUM ) {
		return "";
	}
	return va( "  %s%s", ds_types[a].color, ds_types[a].name );
}

void CG_DS_TypeColor( int arch, vec4_t out ) {
	if ( arch <= 0 || arch >= DSAR_NUM ) {
		out[0] = out[1] = out[2] = out[3] = 1;
		return;
	}
	out[0] = ds_types[arch].rgb[0];
	out[1] = ds_types[arch].rgb[1];
	out[2] = ds_types[arch].rgb[2];
	out[3] = 1;
}

/*
===============================================================================

DRAW HELPERS

===============================================================================
*/

static void CDT_Sprite( const vec3_t org, float radius, const float *rgb, float k, qhandle_t shader ) {
	refEntity_t	re;

	if ( !shader || k <= 0.01f ) {
		return;
	}
	memset( &re, 0, sizeof( re ) );
	re.reType = RT_SPRITE;
	VectorCopy( org, re.origin );
	re.radius = radius;
	re.customShader = shader;
	re.shaderRGBA[0] = (byte)( 255 * CG_Clamp01( rgb[0] * k ) );
	re.shaderRGBA[1] = (byte)( 255 * CG_Clamp01( rgb[1] * k ) );
	re.shaderRGBA[2] = (byte)( 255 * CG_Clamp01( rgb[2] * k ) );
	re.shaderRGBA[3] = 255;
	trap_R_AddRefEntityToScene( &re );
}

// thin laser (rail core) or a wide beam (lightning type, 8 units)
static void CDT_Beam( const vec3_t a, const vec3_t b, const float *rgb, float k, qboolean wide ) {
	refEntity_t	re;

	memset( &re, 0, sizeof( re ) );
	re.reType = wide ? RT_LIGHTNING : RT_RAIL_CORE;
	re.customShader = wide ? cdt.beam : cgs.media.railCoreShader;
	if ( !re.customShader ) {
		return;
	}
	VectorCopy( a, re.origin );
	VectorCopy( b, re.oldorigin );
	AxisClear( re.axis );
	re.shaderTime = cg.time / 1000.0f;
	re.shaderRGBA[0] = (byte)( 255 * CG_Clamp01( rgb[0] * k ) );
	re.shaderRGBA[1] = (byte)( 255 * CG_Clamp01( rgb[1] * k ) );
	re.shaderRGBA[2] = (byte)( 255 * CG_Clamp01( rgb[2] * k ) );
	re.shaderRGBA[3] = 255;
	trap_R_AddRefEntityToScene( &re );
}

// a flat ring on the floor at org
static void CDT_Ring( const vec3_t org, float radius, const float *rgb, float k, float spin ) {
	polyVert_t	v[4];
	vec3_t		f, r;
	int			i;
	float		c = cos( spin ), s = sin( spin );

	if ( !cdt.ring || k <= 0.01f ) {
		return;
	}
	VectorSet( f, c * radius, s * radius, 0 );
	VectorSet( r, -s * radius, c * radius, 0 );
	for ( i = 0 ; i < 4 ; i++ ) {
		VectorCopy( org, v[i].xyz );
		v[i].modulate[0] = (byte)( 255 * CG_Clamp01( rgb[0] * k ) );
		v[i].modulate[1] = (byte)( 255 * CG_Clamp01( rgb[1] * k ) );
		v[i].modulate[2] = (byte)( 255 * CG_Clamp01( rgb[2] * k ) );
		v[i].modulate[3] = 255;
	}
	VectorAdd( v[0].xyz, f, v[0].xyz ); VectorAdd( v[0].xyz, r, v[0].xyz );
	VectorAdd( v[1].xyz, f, v[1].xyz ); VectorSubtract( v[1].xyz, r, v[1].xyz );
	VectorSubtract( v[2].xyz, f, v[2].xyz ); VectorSubtract( v[2].xyz, r, v[2].xyz );
	VectorSubtract( v[3].xyz, f, v[3].xyz ); VectorAdd( v[3].xyz, r, v[3].xyz );
	v[0].st[0] = 1; v[0].st[1] = 1;
	v[1].st[0] = 1; v[1].st[1] = 0;
	v[2].st[0] = 0; v[2].st[1] = 0;
	v[3].st[0] = 0; v[3].st[1] = 1;
	trap_R_AddPolyToScene( cdt.ring, 4, v );
}

// a sprite that stays readable far away (the sniper's glint)
static float CDT_FarRadius( const vec3_t org, float base ) {
	float	d = Distance( org, cg.refdef.vieworg );

	return base + d * 0.012f;
}

/*
===============================================================================

PLAYER HOOKS (cg_players.c)

===============================================================================
*/

/*
=================
CG_DS_TypesPlayer

CG_Player, right after CG_DS_BossScale: the juggernaut's size, the tells on the body
=================
*/
void CG_DS_TypesPlayer( centity_t *cent, refEntity_t *legs ) {
	int				c = cent->currentState.clientNum, a, flags, i, iv;
	cdtClient_t		*st;
	vec3_t			org, eye, fwd, end, rgb, ang, up, back;
	trace_t			tr;
	float			t, pulse, d, s;
	qboolean		dead = ( cent->currentState.eFlags & EF_DEAD ) != 0;

	if ( c < 0 || c >= MAX_CLIENTS ) {
		return;
	}
	a = CDT_Arch( c );
	if ( a <= 0 || a >= DSAR_NUM ) {
		return;
	}
	if ( a == DSAR_JUGGERNAUT ) {
		s = DST_JUGG_SCALE;
		for ( i = 0 ; i < 3 ; i++ ) {
			VectorScale( legs->axis[i], s, legs->axis[i] );
		}
		legs->nonNormalizedAxes = qtrue;
		legs->origin[2] += 24.0f * ( s - 1.0f );
		VectorCopy( legs->origin, legs->oldorigin );
	}
	if ( dead || cent->currentState.number != c ) {
		return;		// corpses: the size only
	}
	st = &cdt.cl[c];
	flags = CG_EN( c )->flags;
	if ( ( flags & DSEF_TELEGRAPH ) && !( st->lastFlags & DSEF_TELEGRAPH ) ) {
		st->tgStart = cg.time;
		if ( a == DSAR_SNIPER && cdt.snipe ) {
			trap_S_StartSound( NULL, c, CHAN_ITEM, cdt.snipe );
		} else if ( a == DSAR_KAMIKAZE && cdt.fuse ) {
			trap_S_StartSound( NULL, c, CHAN_ITEM, cdt.fuse );
		}
	}
	if ( ( flags ^ st->lastFlags ) & DSEF_SHIELDUP ) {
		st->shieldAt = cg.time;
		if ( ( flags & DSEF_SHIELDUP ) && cdt.shieldUp ) {
			CG_FxSound( NULL, c, CHAN_AUTO, cdt.shieldUp );
		}
	}
	st->lastFlags = flags;
	VectorCopy( ds_types[a].rgb, rgb );
	VectorCopy( cent->lerpOrigin, org );
	VectorCopy( cent->lerpOrigin, eye );
	eye[2] += DEFAULT_VIEWHEIGHT;
	pulse = 0.5f + 0.5f * sin( cg.time * 0.006f );

	switch ( a ) {
	case DSAR_SNIPER:
		if ( !( flags & DSEF_TELEGRAPH ) ) {
			break;
		}
		// the glint: a red laser from the scope along its aim, a flare on the head
		t = CG_Clamp01( ( cg.time - st->tgStart ) / (float)( ( CDT_Adapted() & ( 1 << DSAR_SNIPER ) ) ? DST_SNIPE_WINDUP_ADAPT : DST_SNIPE_WINDUP ) );
		VectorCopy( cent->lerpAngles, ang );
		AngleVectors( ang, fwd, NULL, NULL );
		VectorMA( eye, 14, fwd, eye );
		VectorMA( eye, 8192, fwd, end );
		CG_Trace( &tr, eye, NULL, NULL, end, c, MASK_SHOT );
		CDT_Beam( eye, tr.endpos, rgb, 0.45f + 0.55f * t, qfalse );
		CDT_Sprite( eye, CDT_FarRadius( eye, 12 + 16 * t ) * ( 0.85f + 0.3f * sin( cg.time * 0.05f ) ), rgb, 1.0f, cdt.flare );
		CDT_Sprite( eye, CDT_FarRadius( eye, 6 + 4 * t ), colorWhite, 0.7f + 0.3f * t, cdt.flare );
		CDT_Sprite( tr.endpos, 4 + 3 * t, rgb, 1.0f, cdt.flare );
		trap_R_AddLightToScene( eye, 60 + 80 * t, rgb[0], rgb[1], rgb[2] );
		break;
	case DSAR_KAMIKAZE:
		// beeps: faster the closer it is to you, frantic on the fuse
		d = Distance( org, cg.refdef.vieworg );
		iv = ( flags & DSEF_TELEGRAPH ) ? 90 : (int)( 120 + ( d - 150 ) * 0.9f );
		if ( iv < 120 && !( flags & DSEF_TELEGRAPH ) ) {
			iv = 120;
		}
		if ( iv > 1100 ) {
			iv = 1100;
		}
		if ( d < 1600 && cg.time >= st->nextBeep ) {
			st->nextBeep = cg.time + iv;
			st->beepFlash = cg.time;
			if ( cdt.beep ) {
				trap_S_StartSound( NULL, c, CHAN_BODY, cdt.beep );
			}
		}
		VectorCopy( org, up );
		up[2] += 22;
		t = cg.time - st->beepFlash < 80 ? 1.0f : 0.25f;
		if ( flags & DSEF_TELEGRAPH ) {
			t = 1.0f;
			trap_R_AddLightToScene( org, 220 + 120 * pulse, 1.0f, 0.4f, 0.05f );
			CDT_Sprite( org, 34 + 14 * sin( cg.time * 0.04f ), rgb, 0.8f, cdt.flare );
		} else {
			trap_R_AddLightToScene( org, 70 + 40 * pulse, 1.0f, 0.35f, 0.05f );
		}
		CDT_Sprite( up, 7 + 6 * t, rgb, t, cdt.flare );
		break;
	case DSAR_MEDIC:
		// a green light on its back (the beam itself is an effect entity)
		VectorSet( ang, 0, cent->lerpAngles[YAW], 0 );
		AngleVectors( ang, fwd, NULL, NULL );
		VectorMA( org, -12, fwd, back );
		back[2] += 18;
		CDT_Sprite( back, 6 + 2 * pulse, rgb, 0.8f, cdt.flare );
		trap_R_AddLightToScene( back, 60 + 20 * pulse, rgb[0], rgb[1], rgb[2] );
		break;
	case DSAR_BLINKER:
		// a faint unstable shimmer at its feet
		VectorCopy( org, up );
		up[2] -= 20;
		trap_R_AddLightToScene( up, 40 + 30 * ( 0.5f + 0.5f * sin( cg.time * 0.023f ) ), rgb[0], rgb[1], rgb[2] );
		break;
	case DSAR_SUMMONER:
		if ( flags & DSEF_TELEGRAPH ) {
			t = CG_Clamp01( ( cg.time - st->tgStart ) / (float)DST_RITUAL_TIME );
			VectorCopy( org, up );
			up[2] -= 22;
			CDT_Ring( up, 40 + 20 * t, rgb, 0.9f, cg.time * 0.003f );
			trap_R_AddLightToScene( eye, 120 + 60 * pulse, rgb[0], rgb[1], rgb[2] );
		}
		break;
	case DSAR_JUGGERNAUT:
		break;
	default:
		break;
	}
}

/*
=================
CG_DS_TypesTorso

CG_Player, after the torso is placed: the shield bearer's riot shield (up in front of it or
swung down to its side)
=================
*/
void CG_DS_TypesTorso( centity_t *cent, refEntity_t *torso ) {
	int			c = cent->currentState.clientNum;
	refEntity_t	re;
	vec3_t		fwd, left, up, ang, axis[3];
	float		k, sw, yaw, cs, sn;
	int			i;
	qboolean	raised;

	if ( c < 0 || c >= MAX_CLIENTS || CDT_Arch( c ) != DSAR_SHIELD || ( cent->currentState.eFlags & EF_DEAD )
		|| cent->currentState.number != c ) {
		return;
	}
	raised = ( CG_EN( c )->flags & DSEF_SHIELDUP ) != 0;
	k = CG_Clamp01( ( cg.time - cdt.cl[c].shieldAt ) / 220.0f );
	sw = raised ? k : 1.0f - k;		// 1 = in front, 0 = at the side
	VectorSet( ang, 0, cent->lerpAngles[YAW], 0 );
	AngleVectors( ang, fwd, left, up );
	VectorNegate( left, left );		// AngleVectors gives right
	// the shield's frame: +x out of its face, +y across, +z up; swing around z
	yaw = ( 1.0f - sw ) * 70.0f * M_PI / 180.0f;
	cs = cos( yaw );
	sn = sin( yaw );
	for ( i = 0 ; i < 3 ; i++ ) {
		axis[0][i] = fwd[i] * cs + left[i] * sn;
		axis[1][i] = left[i] * cs - fwd[i] * sn;
		axis[2][i] = up[i];
	}
	memset( &re, 0, sizeof( re ) );
	VectorCopy( cent->lerpOrigin, re.origin );
	VectorMA( re.origin, 6 + 14 * sw, fwd, re.origin );
	VectorMA( re.origin, 15 * ( 1.0f - sw ), left, re.origin );
	re.origin[2] += 2 - 6 * ( 1.0f - sw );
	VectorCopy( re.origin, re.oldorigin );
	VectorCopy( cent->lerpOrigin, re.lightingOrigin );
	re.renderfx = RF_LIGHTING_ORIGIN | ( torso->renderfx & RF_THIRD_PERSON );
	for ( i = 0 ; i < 3 ; i++ ) {
		VectorCopy( axis[i], re.axis[i] );
	}
	if ( cdt.shieldModel ) {
		re.hModel = cdt.shieldModel;
		trap_R_AddRefEntityToScene( &re );
		if ( cdt.shieldFaceModel ) {
			re.hModel = cdt.shieldFaceModel;
			re.shaderRGBA[0] = (byte)( 255 * ( 0.35f + 0.65f * sw ) );
			re.shaderRGBA[1] = re.shaderRGBA[0];
			re.shaderRGBA[2] = re.shaderRGBA[0];
			re.shaderRGBA[3] = 255;
			trap_R_AddRefEntityToScene( &re );
		}
	} else if ( cdt.shieldFace ) {
		// no model: a flat energy panel
		polyVert_t	v[4];
		vec3_t		w, h;

		VectorScale( axis[1], 13, w );
		VectorScale( axis[2], 22, h );
		for ( i = 0 ; i < 4 ; i++ ) {
			VectorCopy( re.origin, v[i].xyz );
			v[i].modulate[0] = v[i].modulate[1] = v[i].modulate[2] = (byte)( 255 * ( 0.35f + 0.65f * sw ) );
			v[i].modulate[3] = 255;
		}
		VectorAdd( v[0].xyz, w, v[0].xyz ); VectorAdd( v[0].xyz, h, v[0].xyz );
		VectorSubtract( v[1].xyz, w, v[1].xyz ); VectorAdd( v[1].xyz, h, v[1].xyz );
		VectorSubtract( v[2].xyz, w, v[2].xyz ); VectorSubtract( v[2].xyz, h, v[2].xyz );
		VectorAdd( v[3].xyz, w, v[3].xyz ); VectorSubtract( v[3].xyz, h, v[3].xyz );
		v[0].st[0] = 0; v[0].st[1] = 0;
		v[1].st[0] = 1; v[1].st[1] = 0;
		v[2].st[0] = 1; v[2].st[1] = 1;
		v[3].st[0] = 0; v[3].st[1] = 1;
		trap_R_AddPolyToScene( cdt.shieldFace, 4, v );
	}
	if ( raised ) {
		trap_R_AddLightToScene( re.origin, 50, ds_types[DSAR_SHIELD].rgb[0], ds_types[DSAR_SHIELD].rgb[1], ds_types[DSAR_SHIELD].rgb[2] );
	}
}

/*
=================
CG_DS_TypesShell

CG_AddRefEntityWithPowerups, after the other shells: the mimic's ink, the juggernaut's
gunmetal, the kamikaze glowing hot on its fuse
=================
*/
void CG_DS_TypesShell( refEntity_t *ent, entityState_t *state ) {
	int		a, c;
	float	k;

	if ( state->eType != ET_PLAYER || ( state->eFlags & EF_DEAD ) ) {
		return;
	}
	c = state->clientNum;
	if ( c < 0 || c >= MAX_CLIENTS ) {
		return;
	}
	a = CDT_Arch( c );
	switch ( a ) {
	case DSAR_MIMIC:
		if ( cdt.dark ) {
			ent->customShader = cdt.dark;
			trap_R_AddRefEntityToScene( ent );
		}
		if ( cdt.shell ) {
			ent->customShader = cdt.shell;
			ent->shaderRGBA[0] = 60; ent->shaderRGBA[1] = 62; ent->shaderRGBA[2] = 78; ent->shaderRGBA[3] = 255;
			trap_R_AddRefEntityToScene( ent );
		}
		break;
	case DSAR_JUGGERNAUT:
		if ( cdt.shell ) {
			ent->customShader = cdt.shell;
			ent->shaderRGBA[0] = 70; ent->shaderRGBA[1] = 78; ent->shaderRGBA[2] = 92; ent->shaderRGBA[3] = 255;
			trap_R_AddRefEntityToScene( ent );
		}
		break;
	case DSAR_KAMIKAZE:
		if ( cdt.shell && ( CG_EN( c )->flags & DSEF_TELEGRAPH ) ) {
			k = 0.6f + 0.4f * sin( cg.time * 0.045f );
			ent->customShader = cdt.shell;
			ent->shaderRGBA[0] = (byte)( 255 * k ); ent->shaderRGBA[1] = (byte)( 110 * k ); ent->shaderRGBA[2] = (byte)( 20 * k );
			ent->shaderRGBA[3] = 255;
			trap_R_AddRefEntityToScene( ent );
		}
		break;
	default:
		break;
	}
}

/*
=================
CG_DS_TypesFootstep

cg_event.c EV_FOOTSTEP*: the juggernaut's steps thud and shake the ground near you
=================
*/
void CG_DS_TypesFootstep( centity_t *cent ) {
	int		c = cent->currentState.clientNum;

	if ( c < 0 || c >= MAX_CLIENTS || CDT_Arch( c ) != DSAR_JUGGERNAUT ) {
		return;
	}
	if ( cdt.step ) {
		trap_S_StartSound( NULL, cent->currentState.number, CHAN_AUTO, cdt.step );
	}
	if ( Distance( cent->lerpOrigin, cg.refdef.vieworg ) < 700 ) {
		CG_FxShake( cent->lerpOrigin, 1.2f, 700 );
	}
}

/*
===============================================================================

EFFECT ENTITIES (RM_ENT_DSTYPE, cg_ents.c CG_General)

===============================================================================
*/

static centity_t *CDT_Cent( int n ) {
	if ( n < 0 || n >= ENTITYNUM_MAX_NORMAL ) {
		return NULL;
	}
	return cg_entities[n].currentValid ? &cg_entities[n] : NULL;
}

qboolean CG_DS_TypesEntity( centity_t *cent ) {
	entityState_t	*s = &cent->currentState;
	centity_t		*a, *b;
	vec3_t			p, q, rgb, up;
	float			t, k, r;
	qboolean		first;

	if ( s->generic1 != RM_ENT_DSTYPE ) {
		return qfalse;
	}
	first = cent->miscTime != s->time;
	cent->miscTime = s->time;
	switch ( s->modelindex2 ) {
	case DSTFX_HEAL:
		if ( s->otherEntityNum2 == ENTITYNUM_NONE ) {
			break;
		}
		a = CDT_Cent( s->otherEntityNum );
		b = CDT_Cent( s->otherEntityNum2 );
		if ( !a || !b ) {
			break;
		}
		VectorCopy( ds_types[DSAR_MEDIC].rgb, rgb );
		VectorCopy( a->lerpOrigin, p );
		p[2] += 14;
		VectorCopy( b->lerpOrigin, q );
		q[2] += 6;
		CDT_Beam( p, q, rgb, 0.75f + 0.25f * sin( cg.time * 0.02f ), qtrue );
		CDT_Sprite( q, 18 + 6 * sin( cg.time * 0.012f ), rgb, 0.55f, cdt.flare );
		trap_R_AddLightToScene( q, 90, rgb[0], rgb[1], rgb[2] );
		if ( cdt.heal ) {
			trap_S_AddLoopingSound( s->number, q, vec3_origin, cdt.heal );
		}
		break;
	case DSTFX_RITUAL:
		VectorCopy( ds_types[DSAR_SUMMONER].rgb, rgb );
		if ( first && cdt.ritual ) {
			trap_S_StartSound( cent->lerpOrigin, ENTITYNUM_WORLD, CHAN_AUTO, cdt.ritual );
		}
		t = CG_Clamp01( ( cg.time - s->time ) / (float)( s->time2 - s->time > 0 ? s->time2 - s->time : 1 ) );
		k = cg.time > s->time2 ? CG_Clamp01( 1.0f - ( cg.time - s->time2 ) / 400.0f ) : 1.0f;
		VectorCopy( cent->lerpOrigin, p );
		p[2] -= 18;
		CDT_Ring( p, 30 + 30 * t, rgb, k, -cg.time * 0.004f );
		VectorCopy( p, up );
		up[2] += 40 + 120 * t;
		CDT_Beam( p, up, rgb, k * ( 0.4f + 0.6f * t ), qtrue );
		a = CDT_Cent( s->otherEntityNum );
		if ( a && cg.time < s->time2 ) {
			VectorCopy( a->lerpOrigin, q );
			q[2] += 20;
			CDT_Beam( q, p, rgb, 0.5f * k, qfalse );
		}
		trap_R_AddLightToScene( p, 100 + 100 * t, rgb[0] * k, rgb[1] * k, rgb[2] * k );
		break;
	case DSTFX_BLINK_OUT:
	case DSTFX_BLINK_IN:
		VectorCopy( ds_types[DSAR_BLINKER].rgb, rgb );
		if ( first && cdt.blink ) {
			CG_FxSound( cent->lerpOrigin, ENTITYNUM_WORLD, CHAN_AUTO, cdt.blink );
		}
		t = ( cg.time - s->time ) / 450.0f;
		if ( t >= 1.0f ) {
			break;
		}
		VectorCopy( cent->lerpOrigin, p );
		r = s->modelindex2 == DSTFX_BLINK_IN ? 50 * ( 1.0f - t ) + 10 : 20 + 40 * t;
		CDT_Sprite( p, r, rgb, 1.0f - t, cdt.flare );
		CDT_Sprite( p, r * 0.45f, colorWhite, 1.0f - t, cdt.flare );
		VectorCopy( p, up );
		up[2] += 60;
		p[2] -= 30;
		CDT_Beam( p, up, rgb, 1.0f - t, qtrue );
		trap_R_AddLightToScene( cent->lerpOrigin, 200 * ( 1.0f - t ), rgb[0], rgb[1], rgb[2] );
		break;
	case DSTFX_BOOM:
		VectorCopy( ds_types[DSAR_KAMIKAZE].rgb, rgb );
		r = s->frame * 4.0f;
		if ( first ) {
			if ( cdt.boom ) {
				trap_S_StartSound( cent->lerpOrigin, ENTITYNUM_WORLD, CHAN_AUTO, cdt.boom );
			}
			CG_FxShake( cent->lerpOrigin, 6, r * 3 );
		}
		t = ( cg.time - s->time ) / 650.0f;
		if ( t >= 1.0f ) {
			break;
		}
		CDT_Sprite( cent->lerpOrigin, r * ( 0.3f + 0.7f * t ), rgb, 1.0f - t, cdt.flare );
		CDT_Sprite( cent->lerpOrigin, r * 0.35f * ( 1.0f - t ), colorWhite, 1.0f - t, cdt.flare );
		VectorCopy( cent->lerpOrigin, p );
		p[2] -= 6;
		CDT_Ring( p, r * ( 0.2f + 0.9f * t ), rgb, 1.0f - t, 0 );
		trap_R_AddLightToScene( cent->lerpOrigin, 500 * ( 1.0f - t ), 1.0f, 0.5f, 0.1f );
		break;
	case DSTFX_BLOCK:
		VectorCopy( ds_types[DSAR_SHIELD].rgb, rgb );
		if ( first && cdt.block ) {
			CG_FxSound( cent->lerpOrigin, ENTITYNUM_WORLD, CHAN_AUTO, cdt.block );
		}
		t = ( cg.time - s->time ) / 220.0f;
		if ( t >= 1.0f ) {
			break;
		}
		CDT_Sprite( cent->lerpOrigin, 14 + 10 * t, rgb, 1.0f - t, cdt.flare );
		CDT_Sprite( cent->lerpOrigin, 5, colorWhite, 1.0f - t, cdt.flare );
		trap_R_AddLightToScene( cent->lerpOrigin, 90 * ( 1.0f - t ), rgb[0], rgb[1], rgb[2] );
		break;
	default:
		break;
	}
	return qtrue;
}
