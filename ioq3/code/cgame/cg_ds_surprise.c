/*
===========================================================================
REARMED -- DESCENT round 3 SURPRISES (ds-surprise), cgame side.

- RM_ENT_SURPRISE entities: the landing pillar (a beam of light where an ambusher / the ghost /
  a fodder bot arrives, a ring closing in, the warning sound, a thump on landing) and the
  golden meteor (falling, then its glowing crater).
- Side callouts at the right edge (server command dsuann): AMBUSH: THE HEADSMAN, events,
  the ghost, YOU SURPASSED YOURSELF. While a mini-boss lives a slim name + health line sits at
  the right edge; a running wild event shows its name and timer there. Nothing in the centre.
- BLACKOUT (the floor modifier): the world is darkened under the HUD, gems and chests glow
  through it. BLOOD ECLIPSE: red edges.
- The ghost of a past run is drawn as a pale, translucent shell (CG_DS_GhostRefEntity).
CS_DSSURPRISE (740) carries the state, see bg_ds_surprise.h.
===========================================================================
*/
#include "cg_local.h"

#define SURC_CALLOUT_TIME	3200

typedef struct {
	char		cs[256];
	qboolean	on;
	int			mod;
	int			mb, mbk, mbh;
	int			ev, evs, eve, evc;
	int			ghost;
	int			ghostLightTime;

	int			callKind, callA, callB, callTime, callDur;
	sfxHandle_t	voice;					// a voice line waiting for the announcer (ds-arrivals' queue)
	int			voiceUntil;				// dropped when it can't speak by then
	vec3_t		warnSpot;			// the last ambush / ghost pillar seen
	qboolean	warnSpotValid;

	qhandle_t	beamShader, glowShader, ringShader, ghostShader, goldShader, filterShader, vignetteShader;
	qhandle_t	meteorModel;
	sfxHandle_t	sndWarn, sndLand, sndPortal, sndGold, sndGhost, sndSurpassed, sndEvent, sndSlain;
	sfxHandle_t	vAmbush, vGold, vUprising, vEclipse, vBounty, vGhost, vSurpassed;
} surCg_t;

static surCg_t	surc;

void CG_DS_SurpriseRegister( void ) {
	memset( &surc, 0, sizeof( surc ) );
	surc.mb = surc.ghost = -1;
	surc.beamShader = trap_R_RegisterShader( "rearmed/dsSurBeam" );
	surc.glowShader = trap_R_RegisterShader( "rearmed/glow" );
	surc.ringShader = trap_R_RegisterShader( "rearmed/ring" );
	surc.ghostShader = trap_R_RegisterShader( "rearmed/dsGhost" );
	surc.goldShader = trap_R_RegisterShader( "rearmed/dsGold" );
	surc.filterShader = trap_R_RegisterShaderNoMip( "rearmed/evFilter" );
	surc.vignetteShader = trap_R_RegisterShaderNoMip( "rearmed/evVignette" );
	surc.meteorModel = trap_R_RegisterModel( "models/rearmed/meteor.md3" );
	surc.sndWarn = trap_S_RegisterSound( "sound/rearmed/surprise/warn.wav", qfalse );
	surc.sndLand = trap_S_RegisterSound( "sound/rearmed/surprise/land.wav", qfalse );
	surc.sndPortal = trap_S_RegisterSound( "sound/rearmed/surprise/portal.wav", qfalse );
	surc.sndGold = trap_S_RegisterSound( "sound/rearmed/surprise/gold.wav", qfalse );
	surc.sndGhost = trap_S_RegisterSound( "sound/rearmed/surprise/ghost.wav", qfalse );
	surc.sndSurpassed = trap_S_RegisterSound( "sound/rearmed/surprise/surpassed.wav", qfalse );
	surc.sndEvent = trap_S_RegisterSound( "sound/rearmed/surprise/event.wav", qfalse );
	surc.sndSlain = trap_S_RegisterSound( "sound/rearmed/surprise/slain.wav", qfalse );
	surc.vAmbush = trap_S_RegisterSound( "sound/rearmed/surprise/v_ambush.wav", qfalse );
	surc.vGold = trap_S_RegisterSound( "sound/rearmed/surprise/v_gold_meteor.wav", qfalse );
	surc.vUprising = trap_S_RegisterSound( "sound/rearmed/surprise/v_uprising.wav", qfalse );
	surc.vEclipse = trap_S_RegisterSound( "sound/rearmed/surprise/v_eclipse.wav", qfalse );
	surc.vBounty = trap_S_RegisterSound( "sound/rearmed/surprise/v_bounty.wav", qfalse );
	surc.vGhost = trap_S_RegisterSound( "sound/rearmed/surprise/v_ghost.wav", qfalse );
	surc.vSurpassed = trap_S_RegisterSound( "sound/rearmed/surprise/v_surpassed.wav", qfalse );
}

static int Surc_Key( const char *info, const char *key, int def ) {
	const char	*v = Info_ValueForKey( info, key );

	return v && v[0] ? atoi( v ) : def;
}

// CS_DSSURPRISE changed? (cheap: a copy compare once a frame)
static void Surc_Parse( void ) {
	const char	*s = CG_ConfigString( CS_DSSURPRISE );

	if ( !strcmp( s, surc.cs ) ) {
		return;
	}
	Q_strncpyz( surc.cs, s, sizeof( surc.cs ) );
	surc.on = s[0] != 0;
	surc.mod = Surc_Key( s, "mod", 0 );
	surc.mb = Surc_Key( s, "mb", -1 );
	surc.mbk = Surc_Key( s, "mbk", 0 );
	surc.mbh = Surc_Key( s, "mbh", 100 );
	surc.ev = Surc_Key( s, "ev", 0 );
	surc.evs = Surc_Key( s, "evs", 0 );
	surc.eve = Surc_Key( s, "eve", 0 );
	surc.evc = Surc_Key( s, "evc", 0 );
	surc.ghost = Surc_Key( s, "gh", -1 );
	if ( surc.mod < 0 || surc.mod >= DSMOD_NUM ) {
		surc.mod = 0;
	}
	if ( surc.mbk < 0 || surc.mbk >= DS_MINIBOSS_NUM ) {
		surc.mbk = 0;
	}
	if ( surc.ev < 0 || surc.ev >= DSEV_NUM ) {
		surc.ev = 0;
	}
}

static void Surc_Text( float x, float y, const char *s, const float *color, float cw, float ch, int align ) {
	float	w = CG_DrawStrlen( s ) * (int)( cw + 0.5f );

	if ( align == 0 ) {
		x -= w * 0.5f;
	} else if ( align > 0 ) {
		x -= w;
	}
	CG_DrawStringExt( (int)x, (int)y, s, color, qtrue, qtrue, (int)( cw + 0.5f ), (int)( ch + 0.5f ), 0 );
}

static void Surc_Full( qhandle_t shader, float r, float g, float b, float a ) {
	vec4_t	c;

	c[0] = r; c[1] = g; c[2] = b; c[3] = a;
	trap_R_SetColor( c );
	trap_R_DrawStretchPic( 0, 0, cgs.glconfig.vidWidth, cgs.glconfig.vidHeight, 0, 0, 1, 1, shader );
	trap_R_SetColor( NULL );
}

static const char *Surc_ClientName( int c ) {
	static char	buf[MAX_QPATH];

	if ( c < 0 || c >= MAX_CLIENTS || !cgs.clientinfo[c].infoValid ) {
		return "SOMEONE";
	}
	CG_CleanUpper( cgs.clientinfo[c].name, buf, sizeof( buf ) );
	return buf;
}

/*
===============================================================================

3D: PILLARS AND THE GOLDEN METEOR

===============================================================================
*/

// a vertical beam that faces the camera
static void Surc_Beam( const vec3_t base, float height, float width, const float *rgb, float k ) {
	polyVert_t	v[4];
	vec3_t		d, right;
	int			i, col[3];

	VectorSubtract( base, cg.refdef.vieworg, d );
	d[2] = 0;
	if ( VectorNormalize( d ) < 0.001f ) {
		VectorSet( d, 1, 0, 0 );
	}
	VectorSet( right, -d[1] * width * 0.5f, d[0] * width * 0.5f, 0 );
	for ( i = 0 ; i < 3 ; i++ ) {
		col[i] = (int)( 255 * CG_Clamp01( rgb[i] * k ) );
	}
	VectorAdd( base, right, v[0].xyz );
	VectorSubtract( base, right, v[1].xyz );
	VectorSubtract( base, right, v[2].xyz );
	VectorAdd( base, right, v[3].xyz );
	v[2].xyz[2] += height;
	v[3].xyz[2] += height;
	v[0].st[0] = 0; v[0].st[1] = 1;
	v[1].st[0] = 1; v[1].st[1] = 1;
	v[2].st[0] = 1; v[2].st[1] = 0;
	v[3].st[0] = 0; v[3].st[1] = 0;
	for ( i = 0 ; i < 4 ; i++ ) {
		v[i].modulate[0] = col[0];
		v[i].modulate[1] = col[1];
		v[i].modulate[2] = col[2];
		v[i].modulate[3] = 255;
	}
	trap_R_AddPolyToScene( surc.beamShader, 4, v );
}

static void Surc_Ground( const vec3_t org, float half, const float *rgb, float k, qhandle_t shader ) {
	polyVert_t	v[4];
	int			i, col[3];
	static const float	cx[4] = { -1, 1, 1, -1 }, cy[4] = { -1, -1, 1, 1 };

	if ( half < 1 ) {
		return;
	}
	for ( i = 0 ; i < 3 ; i++ ) {
		col[i] = (int)( 255 * CG_Clamp01( rgb[i] * k ) );
	}
	for ( i = 0 ; i < 4 ; i++ ) {
		v[i].xyz[0] = org[0] + cx[i] * half;
		v[i].xyz[1] = org[1] + cy[i] * half;
		v[i].xyz[2] = org[2] + 1.5f;
		v[i].st[0] = cx[i] > 0 ? 1 : 0;
		v[i].st[1] = cy[i] > 0 ? 1 : 0;
		v[i].modulate[0] = col[0];
		v[i].modulate[1] = col[1];
		v[i].modulate[2] = col[2];
		v[i].modulate[3] = 255;
	}
	trap_R_AddPolyToScene( shader, 4, v );
}

static void Surc_Sprite( const vec3_t org, float radius, const float *rgb, float k ) {
	refEntity_t	re;

	memset( &re, 0, sizeof( re ) );
	re.reType = RT_SPRITE;
	VectorCopy( org, re.origin );
	re.radius = radius;
	re.customShader = surc.glowShader;
	re.shaderRGBA[0] = (byte)( 255 * CG_Clamp01( rgb[0] * k ) );
	re.shaderRGBA[1] = (byte)( 255 * CG_Clamp01( rgb[1] * k ) );
	re.shaderRGBA[2] = (byte)( 255 * CG_Clamp01( rgb[2] * k ) );
	re.shaderRGBA[3] = 255;
	trap_R_AddRefEntityToScene( &re );
}

static void Surc_StyleColor( int style, int arch, vec3_t rgb ) {
	switch ( style ) {
	case DSXS_GHOST:	VectorSet( rgb, 0.72f, 0.86f, 1.0f ); break;
	case DSXS_FODDER:	VectorSet( rgb, 0.62f, 0.22f, 1.0f ); break;
	case DSXS_ARRIVAL:	VectorSet( rgb, 1.0f, 0.72f, 0.35f ); break;		// (ds-arrivals)
	default:
		if ( arch < 0 || arch >= DS_MINIBOSS_NUM ) {
			arch = 0;
		}
		VectorCopy( ds_miniBosses[arch].rgb, rgb );
		break;
	}
}

static void Surc_Pillar( centity_t *cent ) {
	entityState_t	*es = &cent->currentState;
	vec3_t			org, rgb, p;
	float			total, frac, pulse, h, w, k, t;
	int				style = es->frame;

	VectorCopy( cent->lerpOrigin, org );
	org[2] -= 8;
	Surc_StyleColor( style, es->otherEntityNum2, rgb );
	total = es->time2 - es->time;
	frac = total > 0 ? CG_Clamp01( (float)( cg.time - es->time ) / total ) : 1.0f;
	pulse = 0.5f + 0.5f * sin( cg.time * ( 0.01f + 0.025f * frac ) );
	h = style == DSXS_FODDER ? 260 : style == DSXS_ARRIVAL ? 420 : 1600;
	w = style == DSXS_FODDER || style == DSXS_ARRIVAL ? 40 : 120;

	// the warning sound once, when it first shows up
	if ( cent->miscTime != es->time ) {
		cent->miscTime = es->time;
		cent->trailTime = 0;
		if ( style == DSXS_FODDER || style == DSXS_ARRIVAL ) {
			trap_S_StartSound( org, ENTITYNUM_WORLD, CHAN_AUTO, surc.sndPortal );
		} else {
			trap_S_StartSound( org, ENTITYNUM_WORLD, CHAN_AUTO, style == DSXS_GHOST ? surc.sndGhost : surc.sndWarn );
			VectorCopy( org, surc.warnSpot );
			surc.warnSpotValid = qtrue;
		}
		if ( cg_debugEvents.integer ) {
			CG_Printf( "surprise pillar style %i at %i %i %i, lands in %i ms\n", style, (int)org[0], (int)org[1], (int)org[2], es->time2 - cg.time );
		}
	}
	if ( cg.time < es->time2 ) {
		// the warning: the beam grows brighter, a ring closes on the spot
		k = 0.25f + 0.6f * frac + 0.15f * pulse;
		Surc_Beam( org, h, w * ( 0.55f + 0.45f * frac ), rgb, k );
		Surc_Beam( org, h * 0.8f, w * 0.25f, rgb, 0.6f + 0.4f * frac );
		Surc_Ground( org, ( style == DSXS_FODDER || style == DSXS_ARRIVAL ? 70 : 160 ) * ( 1.0f - 0.7f * frac ) + 20, rgb, 0.5f + 0.5f * pulse, surc.ringShader );
		Surc_Ground( org, style == DSXS_FODDER || style == DSXS_ARRIVAL ? 50 : 110, rgb, 0.3f + 0.4f * frac, surc.glowShader );
		VectorCopy( org, p );
		p[2] += 30;
		Surc_Sprite( p, ( style == DSXS_FODDER || style == DSXS_ARRIVAL ? 22 : 40 ) * ( 0.6f + 0.4f * pulse ), rgb, 0.8f );
		trap_R_AddLightToScene( p, ( style == DSXS_FODDER || style == DSXS_ARRIVAL ? 120 : 220 ) * ( 0.5f + 0.5f * frac ), rgb[0], rgb[1], rgb[2] );
		return;
	}
	// it landed: a flash that fades
	if ( cent->trailTime != 1 ) {
		cent->trailTime = 1;
		if ( style != DSXS_FODDER ) {		// (the arrival thump too)
			trap_S_StartSound( org, ENTITYNUM_WORLD, CHAN_AUTO, surc.sndLand );
		}
	}
	t = CG_Clamp01( ( cg.time - es->time2 ) / 800.0f );
	Surc_Beam( org, h, w * ( 1.4f - 0.6f * t ), rgb, 1.0f - t );
	Surc_Ground( org, 60 + 200 * t, rgb, 1.0f - t, surc.ringShader );
	VectorCopy( org, p );
	p[2] += 40;
	trap_R_AddLightToScene( p, 300 * ( 1.0f - t ), rgb[0], rgb[1], rgb[2] );
}

static void Surc_GoldMeteor( centity_t *cent ) {
	entityState_t	*es = &cent->currentState;
	vec3_t			org, sky, p, d, gold = { 1.0f, 0.78f, 0.25f };
	float			total, frac, t, pulse, life;
	refEntity_t		re;
	int				i;

	VectorCopy( cent->lerpOrigin, org );
	VectorCopy( es->origin2, sky );
	pulse = 0.5f + 0.5f * sin( cg.time * 0.012f );
	if ( cent->miscTime != es->time ) {
		cent->miscTime = es->time;
		trap_S_StartLocalSound( surc.sndGold, CHAN_LOCAL_SOUND );
	}
	if ( !es->frame ) {
		// falling: the rock comes down along its line, the crater spot glows gold
		total = es->time2 - es->time;
		frac = total > 0 ? CG_Clamp01( (float)( cg.time - es->time ) / total ) : 1.0f;
		t = frac * frac;
		VectorSubtract( org, sky, d );
		VectorMA( sky, t, d, p );
		if ( surc.meteorModel ) {
			memset( &re, 0, sizeof( re ) );
			re.hModel = surc.meteorModel;
			VectorCopy( p, re.origin );
			VectorCopy( p, re.lightingOrigin );
			{
				vec3_t	ang;

				VectorSet( ang, cg.time * 0.09f, cg.time * 0.13f, 0 );
				AnglesToAxis( ang, re.axis );
			}
			for ( i = 0 ; i < 3 ; i++ ) {
				VectorScale( re.axis[i], 1.7f, re.axis[i] );
			}
			re.nonNormalizedAxes = qtrue;
			trap_R_AddRefEntityToScene( &re );
			re.customShader = surc.goldShader;
			trap_R_AddRefEntityToScene( &re );
		}
		Surc_Sprite( p, 70 + 20 * pulse, gold, 1.0f );
		for ( i = 1 ; i <= 5 ; i++ ) {
			vec3_t	q;

			VectorMA( p, -0.04f * i, d, q );
			Surc_Sprite( q, 50 - i * 7, gold, 0.8f - i * 0.13f );
		}
		trap_R_AddLightToScene( p, 300, gold[0], gold[1], gold[2] );
		// the target on the ground: rings closing in
		Surc_Ground( org, 260 * ( 1.0f - 0.6f * frac ) + 30, gold, 0.6f + 0.4f * pulse, surc.ringShader );
		Surc_Ground( org, 260, gold, 0.25f + 0.25f * frac, surc.ringShader );
		Surc_Ground( org, 200, gold, 0.15f + 0.35f * frac, surc.glowShader );
		Surc_Beam( org, 1400, 40, gold, 0.3f + 0.4f * frac );
		return;
	}
	// the crater: a gold glow and rising sparks, fading out
	life = CG_Clamp01( ( es->time2 + 25000 - cg.time ) / 5000.0f );
	Surc_Ground( org, 200, gold, ( 0.45f + 0.15f * pulse ) * life, surc.glowShader );
	Surc_Ground( org, 120, gold, 0.6f * life, surc.glowShader );
	for ( i = 0 ; i < 6 ; i++ ) {
		t = ( ( cg.time + i * 377 ) % 1600 ) / 1600.0f;
		p[0] = org[0] + cos( i * 1.7f + cg.time * 0.0007f ) * 70;
		p[1] = org[1] + sin( i * 1.7f + cg.time * 0.0007f ) * 70;
		p[2] = org[2] + 10 + t * 160;
		Surc_Sprite( p, 10, gold, ( 1.0f - t ) * life );
	}
	VectorCopy( org, p );
	p[2] += 30;
	trap_R_AddLightToScene( p, 260 * life, gold[0], gold[1], gold[2] );
}

qboolean CG_DS_SurpriseEntity( centity_t *cent ) {
	if ( cent->currentState.generic1 != RM_ENT_SURPRISE ) {
		return qfalse;
	}
	if ( cent->currentState.modelindex2 == DSX2_GOLDMETEOR ) {
		Surc_GoldMeteor( cent );
	} else {
		Surc_Pillar( cent );
	}
	return qtrue;
}

/*
=================
CG_DS_GhostRefEntity

CG_AddRefEntityWithPowerups: the ghost of a past run is a pale, see-through shell instead of
its skin (qtrue = drawn)
=================
*/
qboolean CG_DS_GhostRefEntity( refEntity_t *ent, entityState_t *state ) {
	if ( !surc.on || surc.ghost < 0 || state->eType != ET_PLAYER || state->clientNum != surc.ghost || !surc.ghostShader ) {
		return qfalse;
	}
	ent->customShader = surc.ghostShader;
	trap_R_AddRefEntityToScene( ent );
	if ( surc.ghostLightTime != cg.time ) {
		surc.ghostLightTime = cg.time;		// (legs, torso and head come through here: one cold light)
		trap_R_AddLightToScene( ent->origin, 110 + 20 * sin( cg.time * 0.004f ), 0.45f, 0.6f, 0.9f );
	}
	return qtrue;
}

/*
===============================================================================

SERVER COMMANDS

===============================================================================
*/

qboolean CG_DS_SurpriseServerCommand( const char *cmd ) {
	sfxHandle_t	snd = 0, voice = 0;

	if ( Q_stricmp( cmd, "dsuann" ) ) {
		return qfalse;
	}
	surc.callKind = atoi( CG_Argv( 1 ) );
	surc.callA = atoi( CG_Argv( 2 ) );
	surc.callB = atoi( CG_Argv( 3 ) );
	surc.callTime = cg.time;
	surc.callDur = SURC_CALLOUT_TIME;
	switch ( surc.callKind ) {
	case DSUC_AMBUSH_WARN:
		surc.callDur = 5200;
		snd = surc.sndWarn;
		voice = surc.vAmbush;
		break;
	case DSUC_AMBUSH_LAND:
		surc.callDur = 2600;
		break;
	case DSUC_AMBUSH_SLAIN:
	case DSUC_BOUNTY_CLAIMED:
		snd = surc.sndSlain;
		break;
	case DSUC_EVENT_START:
		surc.callDur = 4200;
		snd = surc.sndEvent;
		voice = surc.callA == DSEV_GOLDMETEOR ? surc.vGold : surc.callA == DSEV_UPRISING ? surc.vUprising
			: surc.callA == DSEV_ECLIPSE ? surc.vEclipse : surc.vBounty;
		break;
	case DSUC_GHOST_WARN:
		surc.callDur = 4600;
		voice = surc.vGhost;
		break;
	case DSUC_GHOST_SLAIN:
		surc.callDur = 4000;
		snd = surc.sndSurpassed;
		voice = surc.vSurpassed;
		break;
	default:
		break;
	}
	if ( snd ) {
		trap_S_StartLocalSound( snd, CHAN_LOCAL_SOUND );
	}
	if ( voice && cg_rearmAnnouncer.integer ) {
		surc.voice = voice;
		surc.voiceUntil = cg.time + 2500;
	}
	if ( cg_debugEvents.integer ) {
		CG_Printf( "dsuann %i %i %i\n", surc.callKind, surc.callA, surc.callB );
	}
	return qtrue;
}

/*
===============================================================================

2D

===============================================================================
*/

// where the warned spot is, in words (no arrow in the middle of the screen)
static const char *Surc_Bearing( const vec3_t spot ) {
	vec3_t	d;
	float	f, r, dist;

	VectorSubtract( spot, cg.refdef.vieworg, d );
	dist = VectorLength( d );
	d[2] = 0;
	VectorNormalize( d );
	f = DotProduct( d, cg.refdef.viewaxis[0] );
	r = -DotProduct( d, cg.refdef.viewaxis[1] );
	return va( "%s  %im", f > 0.7f ? "AHEAD" : f < -0.6f ? "BEHIND" : r > 0 ? "RIGHT" : "LEFT", (int)( dist / 32.0f ) );
}

static void Surc_DrawCallout( void ) {
	vec4_t		col, bg, acc;
	float		t = cg.time - surc.callTime, al, slide, x = 620, y = 258, cw = 8, ch = 11;
	char		l1[96], l2[96], l3[96];
	const dsMiniBoss_t	*mb = &ds_miniBosses[surc.callA >= 0 && surc.callA < DS_MINIBOSS_NUM ? surc.callA : 0];

	if ( !surc.callTime || t < 0 || t > surc.callDur ) {
		return;
	}
	al = CG_Clamp01( t / 150.0f ) * CG_Clamp01( ( surc.callDur - t ) / 400.0f );
	slide = 22.0f * ( 1.0f - CG_Clamp01( t / 200.0f ) );
	acc[0] = 1.0f; acc[1] = 0.3f; acc[2] = 0.15f; acc[3] = 1;
	l1[0] = l2[0] = 0;
	switch ( surc.callKind ) {
	case DSUC_AMBUSH_WARN:
		Com_sprintf( l1, sizeof( l1 ), "AMBUSH: %s", mb->name );
		Com_sprintf( l2, sizeof( l2 ), "INCOMING - %s", surc.warnSpotValid ? Surc_Bearing( surc.warnSpot ) : "WATCH FOR THE PILLAR" );
		VectorCopy( mb->rgb, acc );
		cw = 9; ch = 13;
		break;
	case DSUC_AMBUSH_LAND:
		Com_sprintf( l1, sizeof( l1 ), "%s HAS LANDED", mb->name );
		l2[0] = 0;
		if ( mb->speed > 1 ) {
			Q_strcat( l2, sizeof( l2 ), "FAST" );
		}
		if ( mb->health > 1 ) {
			Q_strcat( l2, sizeof( l2 ), l2[0] ? " + TANKY" : "TANKY" );
		}
		if ( mb->damage > 1 ) {
			Q_strcat( l2, sizeof( l2 ), l2[0] ? " + DEADLY" : "DEADLY" );
		}
		Q_strcat( l2, sizeof( l2 ), " - ONE LIFE, HUNTS EVERYONE" );
		if ( CG_EN( surc.callB )->affix & 0xff ) {		// en-affix: its affixes lead the line
			char	tmp[96];

			Com_sprintf( tmp, sizeof( tmp ), "%s - %s", CG_DSA_Names( CG_EN( surc.callB )->affix & 0xff ), l2 );
			Q_strncpyz( l2, tmp, sizeof( l2 ) );
		}
		VectorCopy( mb->rgb, acc );
		break;
	case DSUC_AMBUSH_SLAIN:
		Q_strncpyz( l1, "AMBUSH CRUSHED", sizeof( l1 ) );
		Com_sprintf( l2, sizeof( l2 ), "%s  +%i SKULLS", mb->name, surc.callB );
		acc[0] = 1; acc[1] = 0.82f; acc[2] = 0.35f;
		break;
	case DSUC_AMBUSH_LOST:
		Com_sprintf( l1, sizeof( l1 ), "%s IS DOWN", mb->name );
		Com_sprintf( l2, sizeof( l2 ), "%s TOOK IT - NO LOOT", Surc_ClientName( surc.callB ) );
		break;
	case DSUC_EVENT_START:
		if ( surc.callA == DSEV_BOUNTY ) {
			Com_sprintf( l1, sizeof( l1 ), "BOUNTY: %s", Surc_ClientName( surc.callB ) );
			Q_strncpyz( l2, "KILL THEM FOR A CHEST", sizeof( l2 ) );
		} else {
			Q_strncpyz( l1, surc.callA > 0 && surc.callA < DSEV_NUM ? ds_wildEventNames[surc.callA] : "EVENT", sizeof( l1 ) );
			Q_strncpyz( l2, surc.callA == DSEV_GOLDMETEOR ? "ITS CRATER HOLDS GEMS AND A CHEST - RACE FOR IT"
				: surc.callA == DSEV_UPRISING ? "WEAK BOTS POUR IN FOR 30 S - FEED THE CHAIN"
				: surc.callA == DSEV_ECLIPSE ? "25 S OF DOUBLE GEMS ON EVERY KILL" : "", sizeof( l2 ) );
		}
		acc[0] = 1; acc[1] = 0.75f; acc[2] = 0.2f;
		if ( surc.callA == DSEV_UPRISING ) {
			acc[0] = 0.7f; acc[1] = 0.3f; acc[2] = 1;
		} else if ( surc.callA == DSEV_ECLIPSE ) {
			acc[0] = 1; acc[1] = 0.12f; acc[2] = 0.1f;
		}
		cw = 9; ch = 13;
		break;
	case DSUC_EVENT_END:
		Com_sprintf( l1, sizeof( l1 ), "%s OVER", surc.callA > 0 && surc.callA < DSEV_NUM ? ds_wildEventNames[surc.callA] : "EVENT" );
		if ( surc.callA == DSEV_UPRISING ) {
			Com_sprintf( l2, sizeof( l2 ), "YOU CUT DOWN %i", surc.callB );
		}
		acc[0] = 0.6f; acc[1] = 0.55f; acc[2] = 0.5f;
		cw = 7; ch = 10;
		break;
	case DSUC_GHOST_WARN:
		Com_sprintf( l1, sizeof( l1 ), "GHOST OF %s", Surc_ClientName( cg.clientNum ) );
		Com_sprintf( l2, sizeof( l2 ), "YOUR PAST SELF HUNTS YOU - %s", surc.warnSpotValid ? Surc_Bearing( surc.warnSpot ) : "" );
		acc[0] = 0.72f; acc[1] = 0.86f; acc[2] = 1;
		cw = 9; ch = 13;
		break;
	case DSUC_GHOST_SLAIN:
		Q_strncpyz( l1, "YOU SURPASSED YOURSELF", sizeof( l1 ) );
		Com_sprintf( l2, sizeof( l2 ), "+%i SKULLS  +A CHEST", surc.callB );
		acc[0] = 1; acc[1] = 0.85f; acc[2] = 0.4f;
		cw = 10; ch = 14;
		break;
	case DSUC_GHOST_FADED:
		Q_strncpyz( l1, "YOUR GHOST FADED", sizeof( l1 ) );
		Q_strncpyz( l2, "SOMEONE ELSE GOT TO IT", sizeof( l2 ) );
		acc[0] = 0.6f; acc[1] = 0.7f; acc[2] = 0.8f;
		break;
	case DSUC_GOLD_SNATCHED:
		Com_sprintf( l1, sizeof( l1 ), "%s SNATCHED THE GOLD", Surc_ClientName( surc.callA ) );
		Q_strncpyz( l2, "THE GEMS ARE STILL THERE", sizeof( l2 ) );
		acc[0] = 1; acc[1] = 0.6f; acc[2] = 0.2f;
		break;
	case DSUC_BOUNTY_CLAIMED:
		Q_strncpyz( l1, "BOUNTY CLAIMED", sizeof( l1 ) );
		Com_sprintf( l2, sizeof( l2 ), "%s  +%i SKULLS  +A CHEST", Surc_ClientName( surc.callA ), surc.callB );
		acc[0] = 1; acc[1] = 0.82f; acc[2] = 0.35f;
		break;
	default:
		return;
	}
	// en-screens: a long second line ("FROST BLAZING - FAST + TANKY - ONE LIFE, HUNTS EVERYONE") reached the middle
	// of the screen: past 38 characters it breaks at the last " - " (or space) into a third line
	l3[0] = 0;
	if ( strlen( l2 ) > 38 ) {
		int		k, cut = -1;

		for ( k = 38; k > 8 && cut < 0; k-- ) {
			if ( l2[k] == ' ' && l2[k + 1] == '-' && l2[k + 2] == ' ' ) {
				cut = k;
			}
		}
		for ( k = 38; k > 8 && cut < 0; k-- ) {
			if ( l2[k] == ' ' ) {
				cut = k;
			}
		}
		if ( cut > 0 ) {
			k = cut;
			while ( l2[k] == ' ' || l2[k] == '-' ) {
				k++;
			}
			Q_strncpyz( l3, l2 + k, sizeof( l3 ) );
			l2[cut] = 0;
		}
	}
	y = CG_EdgeSlot( EDGE_SURPRISE, ch + 15 + ( l3[0] ? 9 : 0 ) ) + 3;		// en-screens: the shared right-edge stack
	CG_HudAnchor( 1 );		// the right side of the HUD area (cg_hudWide)
	{
		float	w = CG_DrawStrlen( l1 ) * (int)( cw + 0.5f ), w2 = CG_DrawStrlen( l2 ) * 5;

		if ( w2 > w ) {
			w = w2;
		}
		if ( CG_DrawStrlen( l3 ) * 5 > w ) {
			w = CG_DrawStrlen( l3 ) * 5;
		}
		w += 12;
		bg[0] = 0.05f; bg[1] = 0.02f; bg[2] = 0.02f; bg[3] = 0.55f * al;
		CG_FillRect( x - w + slide, y - 3, w + 2, ch + 15 + ( l3[0] ? 9 : 0 ), bg );
		bg[0] = acc[0]; bg[1] = acc[1]; bg[2] = acc[2]; bg[3] = 0.9f * al;
		CG_FillRect( x + 1 + slide, y - 3, 2, ch + 15 + ( l3[0] ? 9 : 0 ), bg );
	}
	col[0] = acc[0]; col[1] = acc[1]; col[2] = acc[2]; col[3] = al;
	if ( surc.callKind == DSUC_AMBUSH_WARN || surc.callKind == DSUC_GHOST_WARN ) {
		col[3] = al * ( 0.7f + 0.3f * sin( cg.time * 0.02f ) );		// a pulse while it's incoming
	}
	Surc_Text( x - 3 + slide, y, l1, col, cw, ch, 1 );
	col[0] = 0.9f; col[1] = 0.86f; col[2] = 0.8f; col[3] = 0.85f * al;
	Surc_Text( x - 3 + slide, y + ch + 2, l2, col, 5, 7, 1 );
	if ( l3[0] ) {
		Surc_Text( x - 3 + slide, y + ch + 11, l3, col, 5, 7, 1 );
	}
	CG_HudAnchor( 0 );
}

// the slim edge lines: a living mini-boss (name + health), a running event (name + time)
static void Surc_DrawStatus( void ) {
	vec4_t		col, bg;
	float		x = 620, y = 300, w = 110, f;
	int			left;
	const dsMiniBoss_t	*mb;

	// en-screens: the top of the shared right-edge stack (the height of the lines drawn below)
	left = -1;
	if ( surc.ev && surc.eve > 0 ) {
		left = ( surc.eve - cg.time ) / 1000;
		if ( surc.ev == DSEV_GOLDMETEOR && surc.evc > cg.time ) {
			left = ( surc.evc - cg.time ) / 1000;
		}
	}
	f = ( surc.mb >= 0 ? 16 : 0 ) + ( left >= 0 ? ( surc.ev == DSEV_BOUNTY ? 17 : 8 ) : 0 ) + ( surc.mb >= 0 && left >= 0 ? 4 : 0 );
	if ( f <= 0 ) {
		return;
	}
	y = CG_EdgeSlot( EDGE_STATUS, f );
	CG_HudAnchor( 1 );
	if ( surc.mb >= 0 ) {
		mb = &ds_miniBosses[surc.mbk];
		col[0] = mb->rgb[0]; col[1] = mb->rgb[1]; col[2] = mb->rgb[2]; col[3] = 0.9f;
		Surc_Text( x, y, mb->name, col, 6, 8, 1 );
		bg[0] = 0; bg[1] = 0; bg[2] = 0; bg[3] = 0.55f;
		CG_FillRect( x - w, y + 10, w, 4, bg );
		f = CG_Clamp01( surc.mbh / 100.0f );
		col[3] = 0.85f;
		CG_FillRect( x - w * f, y + 10, w * f, 4, col );
		y += 20;
	}
	if ( surc.ev && surc.eve > 0 ) {
		left = ( surc.eve - cg.time ) / 1000;
		if ( surc.ev == DSEV_GOLDMETEOR && surc.evc > cg.time ) {
			left = ( surc.evc - cg.time ) / 1000;		// the fall
		}
		if ( left >= 0 ) {
			col[0] = 1; col[1] = 0.75f; col[2] = 0.25f; col[3] = 0.85f;
			if ( surc.ev == DSEV_UPRISING ) {
				col[0] = 0.75f; col[1] = 0.45f; col[2] = 1;
			} else if ( surc.ev == DSEV_ECLIPSE ) {
				col[0] = 1; col[1] = 0.25f; col[2] = 0.2f;
			}
			Surc_Text( x, y, va( "%s %s%i:%02i", ds_wildEventNames[surc.ev],
				surc.ev == DSEV_GOLDMETEOR && surc.evc > cg.time ? "LANDS " : "", left / 60, left % 60 ), col, 6, 8, 1 );
			if ( surc.ev == DSEV_BOUNTY ) {
				col[3] = 0.7f;
				Surc_Text( x, y + 10, Surc_ClientName( surc.evc ), col, 5, 7, 1 );
			}
		}
	}
	CG_HudAnchor( 0 );
}

static qboolean Surc_Project( const vec3_t world, float *x, float *y ) {
	vec3_t	d;
	float	fw, rt, up, tx, ty, px, py;

	VectorSubtract( world, cg.refdef.vieworg, d );
	fw = DotProduct( d, cg.refdef.viewaxis[0] );
	if ( fw < 8 ) {
		return qfalse;
	}
	rt = -DotProduct( d, cg.refdef.viewaxis[1] );
	up = DotProduct( d, cg.refdef.viewaxis[2] );
	tx = tan( DEG2RAD( cg.refdef.fov_x ) * 0.5f );
	ty = tan( DEG2RAD( cg.refdef.fov_y ) * 0.5f );
	px = cg.refdef.x + cg.refdef.width * 0.5f * ( 1.0f + rt / ( fw * tx ) );
	py = cg.refdef.y + cg.refdef.height * 0.5f * ( 1.0f - up / ( fw * ty ) );
	*x = ( px - cgs.screenXBias ) / cgs.screenXScale;
	*y = py / cgs.screenYScale;
	return qtrue;
}

/*
=================
CG_DS_SurpriseDrawWorldFX

Under the HUD (with the event tints): BLACKOUT darkens the world and the gems / chests glow
through it; BLOOD ECLIPSE reddens the edges
=================
*/
void CG_DS_SurpriseDrawWorldFX( void ) {
	centity_t	*cent;
	trace_t		tr;
	vec4_t		c;
	float		x, y, d, r;
	int			i, n = 0, tier;
	static const float	tierRgb[3][3] = { { 0.35f, 0.6f, 1.0f }, { 0.35f, 1.0f, 0.45f }, { 1.0f, 0.3f, 0.25f } };

	if ( !cg.snap ) {
		return;
	}
	Surc_Parse();
	if ( !surc.on || cg.snap->ps.pm_type == PM_INTERMISSION ) {
		return;
	}
	if ( surc.ev == DSEV_ECLIPSE && surc.eve > cg.time ) {
		Surc_Full( surc.vignetteShader, 0.9f, 0.05f, 0.04f, 0.28f + 0.08f * sin( cg.time * 0.004f ) );
	}
	if ( surc.mod != DSMOD_BLACKOUT ) {
		return;
	}
	Surc_Full( surc.filterShader, 0.36f, 0.37f, 0.44f, 1 );
	Surc_Full( surc.vignetteShader, 0, 0, 0, 0.65f );
	// the loot glows through the dark
	for ( i = 0 ; i < cg.snap->numEntities && n < 80 ; i++ ) {
		cent = &cg_entities[cg.snap->entities[i].number];
		if ( cent->currentState.eType != ET_GENERAL ) {
			continue;
		}
		if ( cent->currentState.generic1 == RM_ENT_GEM ) {
			tier = cent->currentState.modelindex2;
			if ( tier < 0 || tier > 2 ) {
				tier = 0;
			}
			c[0] = tierRgb[tier][0]; c[1] = tierRgb[tier][1]; c[2] = tierRgb[tier][2];
			r = 14 + 6 * tier;
		} else if ( cent->currentState.generic1 == RM_ENT_CHEST ) {
			c[0] = 1; c[1] = 0.8f; c[2] = 0.3f;
			r = 30;
		} else {
			continue;
		}
		d = Distance( cent->lerpOrigin, cg.refdef.vieworg );
		if ( d > 2600 || !Surc_Project( cent->lerpOrigin, &x, &y ) ) {
			continue;
		}
		CG_Trace( &tr, cg.refdef.vieworg, NULL, NULL, cent->lerpOrigin, cg.snap->ps.clientNum, MASK_SOLID );
		if ( tr.fraction < 0.97f ) {
			continue;
		}
		r = r * 260.0f / ( d < 120 ? 120 : d ) + 4;
		c[3] = 0.85f;
		trap_R_SetColor( c );
		CG_DrawPic( x - r, y - r, r * 2, r * 2, surc.glowShader );
		n++;
	}
	trap_R_SetColor( NULL );
}

/*
=================
CG_DS_SurpriseDraw2D

From CG_Draw2D next to the other DESCENT HUD pieces (not at the intermission)
=================
*/
void CG_DS_SurpriseDraw2D( void ) {
	if ( !cg.snap ) {
		return;
	}
	Surc_Parse();
	// our voice line: when the shared announcer is free (ds-arrivals reads the arriving bots' names)
	if ( surc.voice ) {
		if ( cg.time > surc.voiceUntil ) {
			surc.voice = 0;
		} else if ( CG_DSI_AnnouncerFreeAt() <= cg.time ) {
			trap_S_StartLocalSound( surc.voice, CHAN_ANNOUNCER );
			CG_DSI_AnnouncerHold( cg.time + 1400 );
			surc.voice = 0;
		}
	}
	if ( !surc.on || cg.showScores || cg.snap->ps.pm_type == PM_INTERMISSION ) {
		return;
	}
	Surc_DrawCallout();
	Surc_DrawStatus();
}

int CG_DS_SurpriseMod( void ) {
	Surc_Parse();
	return surc.on ? surc.mod : 0;
}
