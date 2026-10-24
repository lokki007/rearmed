/*
===========================================================================
REARMED -- DESCENT round 3 SECRETS (ds-secrets, cgame side): the old strongbox
(RM_ENT_STASH), its faint hum (only audible close by; DOWSING ROD hears it further), the lid
opening, the "SECRET FOUND 2/3" fanfare at the right edge (CS_DSSECRETS) and, with the
DOWSING ROD card, a faint compass tick at the top edge. No marker otherwise.
Game side: game/g_ds_secrets.c.
===========================================================================
*/
#include "cg_local.h"

#define CSEC_HUM_BANDS		4
#define CSEC_FANFARE_MSEC	3400
#define CSEC_LID_MSEC		420
#define CSEC_HINGE_X		-12.0f		// the lid's hinge on the model (back top edge)
#define CSEC_HINGE_Z		15.0f

typedef struct {
	int		n, f, t, ft, dw;
	int		numPos;
	vec3_t	pos[4];
	int		posFound[4];
} csecState_t;

static csecState_t	csec;
static char			csecRaw[MAX_STRING_CHARS];
static qhandle_t	csecBody, csecLid, csecGlow, csecWhite;
static sfxHandle_t	csecHum[CSEC_HUM_BANDS], csecOpen, csecFound, csecFoundRare, csecTick;
static int			csecFanfareAt, csecFanfareType, csecFanfareN, csecFanfareF;
static int			csecOpenSeen[MAX_GENTITIES];
static int			csecNextTick;

static const vec4_t	csecGold = { 1.0f, 0.80f, 0.32f, 1 };
static const vec4_t	csecTypeCol[DSS_NUM] = {
	{ 0.45f, 0.75f, 1.00f, 1 },		// gems
	{ 1.00f, 0.80f, 0.25f, 1 },		// gold
	{ 1.00f, 0.55f, 0.15f, 1 },		// strongbox (chest)
	{ 1.00f, 0.62f, 0.10f, 1 },		// legendary
	{ 0.75f, 0.45f, 1.00f, 1 }		// overdrive
};

void CG_DS_SecretsRegister( void ) {
	int		i;

	csecBody = trap_R_RegisterModel( "models/rearmed/secrets/stash.md3" );
	csecLid = trap_R_RegisterModel( "models/rearmed/secrets/stash_lid.md3" );
	csecGlow = trap_R_RegisterShader( "rearmed/glow" );
	csecWhite = trap_R_RegisterShader( "white" );
	for ( i = 0 ; i < CSEC_HUM_BANDS ; i++ ) {
		csecHum[i] = trap_S_RegisterSound( va( "sound/rearmed/secrets/hum%i.wav", i ), qfalse );
	}
	csecOpen = trap_S_RegisterSound( "sound/rearmed/secrets/open.wav", qfalse );
	csecFound = trap_S_RegisterSound( "sound/rearmed/secrets/found.wav", qfalse );
	csecFoundRare = trap_S_RegisterSound( "sound/rearmed/secrets/found_rare.wav", qfalse );
	csecTick = trap_S_RegisterSound( "sound/rearmed/secrets/tick.wav", qfalse );
	memset( &csec, 0, sizeof( csec ) );
	memset( csecOpenSeen, 0, sizeof( csecOpenSeen ) );
	csecRaw[0] = 0;
	csecFanfareAt = 0;
	csecNextTick = 0;
}

/*
===============================================================================

STATE (CS_DSSECRETS, polled)

===============================================================================
*/

static void CSec_Parse( void ) {
	const char	*info = CG_ConfigString( CS_DSSECRETS );
	const char	*p;
	int			oldF = csec.f, oldN = csec.n, k;

	if ( !strcmp( info, csecRaw ) ) {
		return;
	}
	Q_strncpyz( csecRaw, info, sizeof( csecRaw ) );
	if ( !info[0] ) {
		memset( &csec, 0, sizeof( csec ) );
		return;
	}
	csec.n = atoi( Info_ValueForKey( info, "n" ) );
	csec.f = atoi( Info_ValueForKey( info, "f" ) );
	csec.t = atoi( Info_ValueForKey( info, "t" ) );
	csec.ft = atoi( Info_ValueForKey( info, "ft" ) );
	csec.dw = atoi( Info_ValueForKey( info, "dw" ) );
	csec.numPos = 0;
	p = Info_ValueForKey( info, "p" );
	if ( !strcmp( p, "-" ) ) {
		p = "";		// none sent (no DOWSING ROD)
	}
	while ( *p && csec.numPos < 4 ) {
		k = csec.numPos;
		csec.pos[k][0] = atof( p );
		while ( *p && *p != ',' ) p++;
		if ( *p ) p++;
		csec.pos[k][1] = atof( p );
		while ( *p && *p != ',' ) p++;
		if ( *p ) p++;
		csec.pos[k][2] = atof( p );
		while ( *p && *p != ',' ) p++;
		if ( *p ) p++;
		csec.posFound[k] = atoi( p );
		while ( *p && *p != ';' ) p++;
		if ( *p ) p++;
		csec.numPos++;
	}
	// a new find (not a map load / vid_restart catching up with an old one)
	if ( csec.f > oldF && cg.time - csec.ft < 2000 ) {
		csecFanfareAt = cg.time;
		csecFanfareType = csec.t < 0 || csec.t >= DSS_NUM ? 0 : csec.t;
		csecFanfareN = csec.n;
		csecFanfareF = csec.f;
		trap_S_StartLocalSound( csecFanfareType >= DSS_CHEST ? csecFoundRare : csecFound, CHAN_ANNOUNCER );
	}
	(void)oldN;
}

/*
===============================================================================

THE STRONGBOX

===============================================================================
*/

static void CSec_Model( qhandle_t model, const vec3_t org, const vec3_t angles ) {
	refEntity_t	re;

	if ( !model ) {
		return;
	}
	memset( &re, 0, sizeof( re ) );
	re.reType = RT_MODEL;
	re.hModel = model;
	VectorCopy( org, re.origin );
	VectorCopy( org, re.oldorigin );
	VectorCopy( org, re.lightingOrigin );
	AnglesToAxis( angles, re.axis );
	trap_R_AddRefEntityToScene( &re );
}

static void CSec_Sprite( const vec3_t org, float radius, const float *rgb, float alpha ) {
	refEntity_t	re;

	memset( &re, 0, sizeof( re ) );
	re.reType = RT_SPRITE;
	VectorCopy( org, re.origin );
	re.radius = radius;
	re.customShader = csecGlow;
	re.shaderRGBA[0] = (byte)( 255 * CG_Clamp01( rgb[0] * alpha ) );
	re.shaderRGBA[1] = (byte)( 255 * CG_Clamp01( rgb[1] * alpha ) );
	re.shaderRGBA[2] = (byte)( 255 * CG_Clamp01( rgb[2] * alpha ) );
	re.shaderRGBA[3] = 255;
	re.renderfx = RF_NOSHADOW;
	trap_R_AddRefEntityToScene( &re );
}

// the hum: four loudness bands by distance (a loop can't be scaled), silent past the last
static void CSec_Hum( centity_t *cent, const vec3_t org, qboolean dowse ) {
	float	d = Distance( org, cg.refdef.vieworg ), reach = dowse ? 2.0f : 1.0f;
	int		band;

	if ( d < 150 * reach ) {
		band = 0;
	} else if ( d < 260 * reach ) {
		band = 1;
	} else if ( d < 380 * reach ) {
		band = 2;
	} else if ( d < 520 * reach ) {
		band = 3;
	} else {
		return;
	}
	if ( csecHum[band] ) {
		trap_S_AddLoopingSound( cent->currentState.number, org, vec3_origin, csecHum[band] );
	}
}

static void CG_DS_Stash( centity_t *cent ) {
	entityState_t	*s = &cent->currentState;
	vec3_t			org, ang, lidOrg, fwd, up, p;
	float			t, open, glint;
	int				n = s->number;
	static const vec4_t	debugCol = { 0.8f, 0.3f, 1.0f, 1 };

	VectorCopy( cent->lerpOrigin, org );
	VectorSet( ang, 0, s->angles[YAW], 0 );
	CSec_Model( csecBody, org, ang );
	AngleVectors( ang, fwd, NULL, up );

	open = 0;
	if ( s->time ) {
		t = (float)( cg.time - s->time ) / CSEC_LID_MSEC;
		t = CG_Clamp01( t );
		open = 1.0f - ( 1.0f - t ) * ( 1.0f - t );
		if ( t < 1.0f ) {
			open += 0.12f * sin( t * M_PI );		// a little overswing
		}
		if ( csecOpenSeen[n] != s->time ) {
			csecOpenSeen[n] = s->time;
			if ( cg.time - s->time < 1500 ) {
				trap_S_StartSound( org, ENTITYNUM_WORLD, CHAN_AUTO, csecOpen );
			}
		}
		// a warm flash out of the box
		if ( cg.time - s->time < 900 ) {
			t = 1.0f - ( cg.time - s->time ) / 900.0f;
			VectorMA( org, 22, up, p );
			trap_R_AddLightToScene( p, 80 + 160 * t, 1.0f, 0.75f, 0.3f );
			CSec_Sprite( p, 10 + 30 * t, csecGold, 0.6f * t );
		}
	}
	VectorMA( org, CSEC_HINGE_X, fwd, lidOrg );
	VectorMA( lidOrg, CSEC_HINGE_Z, up, lidOrg );
	ang[PITCH] = -105.0f * open;
	CSec_Model( csecLid, lidOrg, ang );

	if ( s->frame & 2 ) {
		// dev "dssecrets show": a tall marker
		for ( t = 0 ; t < 8 ; t++ ) {
			VectorCopy( org, p );
			p[2] += 30 + t * 40;
			CSec_Sprite( p, 14, debugCol, 0.7f );
		}
		return;
	}
	if ( s->time ) {
		return;
	}
	CSec_Hum( cent, org, ( s->frame & DSF_DOWSE ) != 0 );
	// now and then a faint glint at the lock (a hint for the curious, not a beacon)
	glint = (float)( ( cg.time + n * 977 ) % 4200 );
	if ( glint < 260 ) {
		VectorMA( org, 13, fwd, p );
		VectorMA( p, 11, up, p );
		CSec_Sprite( p, 3.5f + 3.0f * sin( glint / 260.0f * M_PI ), csecGold, 0.55f * sin( glint / 260.0f * M_PI ) );
	}
}

qboolean CG_DS_SecretsEntity( centity_t *cent ) {
	if ( cent->currentState.generic1 != RM_ENT_STASH ) {
		return qfalse;
	}
	CG_DS_Stash( cent );
	return qtrue;
}

/*
===============================================================================

HUD: the fanfare at the right edge, the DOWSING ROD compass tick at the top edge

===============================================================================
*/

static void CSec_Text( float x, float y, const char *s, const float *color, float cw, float ch ) {
	float	w = CG_DrawStrlen( s ) * (int)( cw + 0.5f );

	CG_DrawStringExt( (int)( x - w ), (int)y, s, color, qtrue, qtrue, (int)( cw + 0.5f ), (int)( ch + 0.5f ), 0 );
}

static void CSec_DrawFanfare( void ) {
	float		t = cg.time - csecFanfareAt, al, slide, x = 620, y = 188, pulse;
	vec4_t		col, bg;

	if ( !csecFanfareAt || t < 0 || t > CSEC_FANFARE_MSEC ) {
		return;
	}
	al = CG_Clamp01( t / 140.0f ) * CG_Clamp01( ( CSEC_FANFARE_MSEC - t ) / 500.0f );
	slide = 24.0f * ( 1.0f - CG_Clamp01( t / 200.0f ) );
	pulse = 1.0f - CG_Clamp01( t / 600.0f );
	y = CG_EdgeSlot( EDGE_SECRET, 28 ) + 3;		// en-screens: the shared right-edge stack
	CG_HudAnchor( 1 );
	bg[0] = 0.07f; bg[1] = 0.05f; bg[2] = 0.0f; bg[3] = 0.55f * al;
	CG_FillRect( x - 168 + slide, y - 3, 170, 28, bg );
	bg[0] = 1.0f; bg[1] = 0.78f; bg[2] = 0.25f; bg[3] = al;
	CG_FillRect( x + 1 + slide, y - 3, 2, 28, bg );
	col[0] = 1.0f; col[1] = 0.85f + 0.15f * pulse; col[2] = 0.35f + 0.6f * pulse; col[3] = al;
	CSec_Text( x - 3 + slide, y, va( "SECRET FOUND %i/%i", csecFanfareF, csecFanfareN > csecFanfareF ? csecFanfareN : csecFanfareF ), col, 8, 11 );
	VectorCopy( csecTypeCol[csecFanfareType], col );
	col[3] = 0.9f * al;
	CSec_Text( x - 3 + slide, y + 13, ds_stashNames[csecFanfareType], col, 5, 7 );
	CG_HudAnchor( 0 );
}

static void CSec_DrawCompass( void ) {
	vec3_t		d, me;
	float		best = 1e9f, dist, yaw, rel, x, al;
	int			i, k = -1, gap;
	vec4_t		col;

	if ( !csec.dw || !csec.numPos || !cg.snap || cg.snap->ps.pm_type != PM_NORMAL || CG_DSI_Busy() ) {
		return;		// (the versus intro owns the top edge while it runs)
	}
	VectorCopy( cg.predictedPlayerState.origin, me );
	for ( i = 0 ; i < csec.numPos ; i++ ) {
		if ( csec.posFound[i] ) {
			continue;
		}
		dist = Distance( me, csec.pos[i] );
		if ( dist < best ) {
			best = dist;
			k = i;
		}
	}
	if ( k < 0 || best > 2000 ) {
		return;
	}
	VectorSubtract( csec.pos[k], me, d );
	yaw = atan2( d[1], d[0] ) * 180.0f / M_PI;
	rel = AngleNormalize180( yaw - cg.predictedPlayerState.viewangles[YAW] );
	if ( rel > 90 ) {
		rel = 90;
	} else if ( rel < -90 ) {
		rel = -90;
	}
	// a thin arc at the top edge; the dot slides with the bearing (left of centre = to the left)
	al = 0.22f + 0.28f * CG_Clamp01( 1.0f - best / 2000.0f );
	col[0] = 0.85f; col[1] = 0.75f; col[2] = 0.45f; col[3] = al * 0.35f;
	CG_FillRect( 320 - 60, 6, 120, 1, col );
	x = 320 - rel / 90.0f * 60.0f;
	col[3] = al;
	CG_FillRect( x - 2, 3, 4, 7, col );
	// the tick: faster as the stash gets closer
	gap = 450 + (int)( best * 1.1f );
	if ( cg.time >= csecNextTick || csecNextTick - cg.time > 5000 ) {
		csecNextTick = cg.time + gap;
		if ( best < 1400 ) {
			trap_S_StartLocalSound( csecTick, CHAN_AUTO );
		}
	}
}

// from CG_Draw2D next to the descent HUD
void CG_DS_SecretsDraw2D( void ) {
	CSec_Parse();
	if ( !cg.snap || cg.snap->ps.pm_type == PM_INTERMISSION ) {
		return;
	}
	CSec_DrawFanfare();
	CSec_DrawCompass();
}
