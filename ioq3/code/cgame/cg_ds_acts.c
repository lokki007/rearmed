/*
===========================================================================
REARMED -- DESCENT v2 ACTS (owner ACTS), cgame side: the act biomes (colour grade, haze,
ambient particles), the act intro card, the vault timer, the bosses (scaled model, shield /
frenzy looks, the top health bar) and the boss attack telegraphs (RM_ENT_BOSSFX) and helpers
(RM_ENT_BOSSPROP drones and decoys). Server side: game/g_ds_boss.c.
cgaBiome 0 turns the grade, haze and particles off (1 default, 2 stronger).
===========================================================================
*/
#include "cg_local.h"

#define ACT_INTRO_TIME		3600
#define BOSSBAR_DELAY		3400		// after the floor strip
#define DS_MOTES			56
#define DS_MOTE_BOX			560.0f

typedef struct {
	vec3_t	org;
	vec3_t	vel;
	float	size, phase;
	int		alive;
} cgaMote_t;

typedef struct {
	// CS_DESCENT
	qboolean	on;
	int			floor, nodeType, phase;
	// CS_DESCENTBOSS
	qboolean	boss;
	int			client, type, hp, max, ph, nph, atk, att, deaths, shield, frenzy, roar, abyss, dead;
	int			vaultEnd, vaultMode, vaultBroken;		// CS_DESCENTBOSS vault / vm / vb
	// local
	int			firstIn;		// cg.time the player was first in this floor
	int			lastFloorSeen;
	float		shownHp;		// the bar's trailing ghost (fraction)
	int			lastAtk, lastAtkTime;
	// round 2 (ds-boss)
	int			lives, traits, grace;		// CS_DESCENTBOSS lv / tr / gr
	int			nem;						// fight floors: the nemesis client, -1 none
	int			lastDeaths, lostTime;		// a life lost: its skull flashes
	int			lastPh;
	vec3_t		trail[8];					// fast bosses: where it has been (streak)
	int			trailNext, trailHead, trailCount;
	int			calloutTime, calloutKind, calloutClient, calloutVal;
	int			emberTime;
	// round 2b: the boss's lives, the roster
	int			bl, blm, lastBl, blLostTime;
	int			nro, roClient[8], roLives[8];
	// round 4 (en-boss): the timed 1v1 clock (CS_DESCENTBOSS tm tl ts te kt lm tu tb)
	int			timed, tLimit, tStart, tEnd, tKill, tLastMin, tUp, tBonus;
	int			tTickSec, tLastMinSeen, tUpSeen;		// the last tick played, sounds once
	int			recBoss, recPrev, recNew;				// the fastest-kill record (checked once per floor)
	int			practice;								// boss practice (CS_DESCENTBOSS pr): no records
} cgaState_t;

static cgaState_t	cga;
static cgaMote_t			cgaMotes[DS_MOTES];
static vmCvar_t			cgaBiome;

static qhandle_t	cgaFilter, cgaVignette, cgaGlow, cgaRing, cgaRail;
static qhandle_t	cgaMeteor, cgaDrone;
static qhandle_t	cgaLifeSkull, cgaNemShell, cgaArmourShell;

#define DS_TANKY_SIZE		1.08f		// a tanky boss is drawn bigger (g_ds_boss.c: hit the same)
#define DST_FAST			1
#define DST_TANKY			2
#define DST_STRONG			4
static sfxHandle_t	cgaTick, cgaTock, cgaLastMin, cgaTimeUp;		// round 4: the boss clock
static sfxHandle_t	cgaWarn, cgaHook, cgaCharge, cgaRoar, cgaBlink, cgaWell, cgaActSnd, cgaVaultSnd, cgaShieldSnd, cgaNovaSnd;


/*
===============================================================================

STATE

===============================================================================
*/

void CG_DS_ActsRegister( void ) {
	trap_Cvar_Register( &cgaBiome, "cg_dsBiome", "1", CVAR_ARCHIVE );
	cgaFilter = trap_R_RegisterShaderNoMip( "rearmed/evFilter" );
	cgaVignette = trap_R_RegisterShaderNoMip( "rearmed/evVignette" );
	cgaGlow = trap_R_RegisterShader( "rearmed/glow" );
	cgaRing = trap_R_RegisterShader( "rearmed/ring" );
	cgaRail = trap_R_RegisterShader( "railCore" );
	cgaMeteor = trap_R_RegisterModel( "models/rearmed/meteor.md3" );
	cgaDrone = trap_R_RegisterModel( "models/rearmed/debris/metal1.md3" );
	cgaWarn = trap_S_RegisterSound( "sound/rearmed/acts/warn.wav", qfalse );
	cgaHook = trap_S_RegisterSound( "sound/rearmed/acts/hook.wav", qfalse );
	cgaCharge = trap_S_RegisterSound( "sound/rearmed/acts/charge.wav", qfalse );
	cgaRoar = trap_S_RegisterSound( "sound/rearmed/acts/roar.wav", qfalse );
	cgaBlink = trap_S_RegisterSound( "sound/rearmed/acts/blink.wav", qfalse );
	cgaWell = trap_S_RegisterSound( "sound/rearmed/acts/well.wav", qfalse );
	cgaActSnd = trap_S_RegisterSound( "sound/rearmed/acts/act.wav", qfalse );
	cgaVaultSnd = trap_S_RegisterSound( "sound/rearmed/acts/vault.wav", qfalse );
	cgaShieldSnd = trap_S_RegisterSound( "sound/rearmed/acts/shield.wav", qfalse );
	cgaNovaSnd = trap_S_RegisterSound( "sound/rearmed/acts/nova.wav", qfalse );
	cgaTick = trap_S_RegisterSound( "sound/rearmed/acts/clock_tick.wav", qfalse );
	cgaTock = trap_S_RegisterSound( "sound/rearmed/acts/clock_tock.wav", qfalse );
	cgaLastMin = trap_S_RegisterSound( "sound/rearmed/acts/lastminute.wav", qfalse );
	cgaTimeUp = trap_S_RegisterSound( "sound/rearmed/acts/timeup.wav", qfalse );
	cgaLifeSkull = trap_R_RegisterShaderNoMip( "rearmed/dsLifeSkull" );
	cgaNemShell = trap_R_RegisterShader( "rearmed/dsNemesisShell" );
	cgaArmourShell = trap_R_RegisterShader( "rearmed/dsArmourShell" );
	memset( &cga, 0, sizeof( cga ) );
	memset( cgaMotes, 0, sizeof( cgaMotes ) );
	cga.client = -1;
	cga.nem = -1;
	CG_DS_ActsParse();
}

static int CGA_Key( const char *info, const char *key ) {
	return atoi( Info_ValueForKey( info, key ) );
}

// CS_DESCENT / CS_DESCENTBOSS changed (and at init)
void CG_DS_ActsParse( void ) {
	const char	*info = CG_ConfigString( CS_DESCENT );
	const char	*b = CG_ConfigString( CS_DESCENTBOSS );
	int			oldAtk = cga.atk, oldAtt = cga.att;

	cga.on = info[0] && CGA_Key( info, "on" ) == 1;
	cga.floor = CGA_Key( info, "fl" );
	cga.nodeType = CGA_Key( info, "nt" );
	cga.phase = CGA_Key( info, "ph" );
	cga.boss = qfalse;
	cga.vaultEnd = 0;
	cga.nem = -1;
	if ( !cga.on || !b[0] ) {
		cga.client = -1;
		return;
	}
	if ( Info_ValueForKey( b, "vault" )[0] ) {
		cga.vaultEnd = CGA_Key( b, "vault" );
		cga.vaultMode = CGA_Key( b, "vm" );
		cga.vaultBroken = CGA_Key( b, "vb" );
		cga.client = -1;
		return;
	}
	if ( Info_ValueForKey( b, "nem" )[0] ) {
		cga.nem = CGA_Key( b, "nem" );		// a fight floor with the nemesis on it
	}
	if ( !Info_ValueForKey( b, "b" )[0] ) {
		cga.client = -1;
		return;		// not a boss floor
	}
	cga.boss = qtrue;
	cga.client = CGA_Key( b, "cl" );
	cga.type = CGA_Key( b, "b" );
	if ( cga.type < 0 || cga.type >= BOSS_NUM ) {
		cga.type = 0;
	}
	cga.hp = CGA_Key( b, "hp" );
	cga.max = CGA_Key( b, "max" );
	cga.ph = CGA_Key( b, "ph" );
	cga.nph = CGA_Key( b, "nph" );
	cga.atk = CGA_Key( b, "at" );
	cga.att = CGA_Key( b, "att" );
	cga.deaths = CGA_Key( b, "pd" );
	cga.shield = CGA_Key( b, "sh" );
	cga.frenzy = CGA_Key( b, "fz" );
	cga.roar = CGA_Key( b, "rr" );
	cga.abyss = CGA_Key( b, "ab" );
	cga.dead = CGA_Key( b, "dead" );
	cga.lives = CGA_Key( b, "lv" );
	if ( cga.lives < 1 ) {
		cga.lives = DS_BOSS_FRAGS_TO_LOSE;		// (an old server)
	}
	cga.traits = CGA_Key( b, "tr" );
	cga.grace = CGA_Key( b, "gr" );
	cga.timed = CGA_Key( b, "tm" );
	cga.tLimit = CGA_Key( b, "tl" );
	cga.tStart = CGA_Key( b, "ts" );
	cga.tEnd = CGA_Key( b, "te" );
	cga.tKill = CGA_Key( b, "kt" );
	cga.tLastMin = CGA_Key( b, "lm" );
	cga.tUp = CGA_Key( b, "tu" );
	cga.tBonus = CGA_Key( b, "tb" );
	cga.practice = CGA_Key( b, "pr" );
	cga.blm = CGA_Key( b, "blm" );
	cga.bl = CGA_Key( b, "bl" );
	if ( cga.bl < cga.lastBl ) {
		cga.blLostTime = cg.time;
	}
	cga.lastBl = cga.bl;
	{
		const char	*r = Info_ValueForKey( b, "ro" );

		cga.nro = 0;
		while ( *r && cga.nro < 8 ) {
			cga.roClient[cga.nro] = atoi( r );
			while ( *r && *r != ':' ) {
				r++;
			}
			if ( *r == ':' ) {
				r++;
			}
			cga.roLives[cga.nro] = atoi( r );
			cga.nro++;
			while ( *r && *r != '.' ) {
				r++;
			}
			if ( *r == '.' ) {
				r++;
			}
		}
	}
	if ( cga.deaths > cga.lastDeaths ) {
		cga.lostTime = cg.time;
	}
	cga.lastDeaths = cga.deaths;
	if ( cga.ph > cga.lastPh && cga.lastPh > 0 ) {
		CG_KillCamNotable( cga.client, KCN_PHASE );		// ds-killcam: a boss phase broken
	}
	cga.lastPh = cga.ph;
	if ( cga.max < 1 ) {
		cga.max = 1;
	}
	if ( cga.nph < 1 ) {
		cga.nph = 1;
	}
	if ( cga.atk && ( cga.atk != oldAtk || cga.att != oldAtt ) ) {
		cga.lastAtk = cga.atk;
		cga.lastAtkTime = cg.time;
	}
}

static int CGA_Act( void ) {
	return cga.on && cga.floor > 0 ? DS_ActForFloor( cga.floor ) : 0;
}

int CG_DS_BossClient( void ) {
	return cga.boss ? cga.client : -1;
}

// en-boss: a boss practice floor (cg_style.c sets no records there)
qboolean CG_DS_Practice( void ) {
	return cga.on && cga.boss && cga.practice;
}

// round 4: msec left on the timed boss clock (the whole limit before it starts; -1 not timed)
static int CGA_TimeLeft( void ) {
	int		t;

	if ( !cga.boss || !cga.timed ) {
		return -1;
	}
	if ( cga.tUp ) {
		return 0;
	}
	if ( cga.tKill > 0 ) {
		t = cga.tLimit * 1000 - cga.tKill;
	} else if ( cga.tEnd > 0 ) {		// (te: 0 until the clock starts)
		t = cga.tEnd - cg.time;
	} else {
		t = cga.tLimit * 1000;
	}
	return t < 0 ? 0 : t;
}

// m:ss (rounded up: 0:00 only when it is really over)
// (its own buffers: va() has only two, and a label nests two clocks inside a va())
static const char *CGA_Clock( int msec ) {
	static char	buf[4][16];
	static int	idx;
	int			s = ( msec + 999 ) / 1000;
	char		*b = buf[idx++ & 3];

	if ( s < 0 ) {
		s = 0;
	}
	Com_sprintf( b, sizeof( buf[0] ), "%i:%02i", s / 60, s % 60 );
	return b;
}

/*
=================
CG_DS_FloorGoal

The floor strip's second line on boss / vault floors (NULL: the frag race line)
=================
*/
const char *CG_DS_FloorGoal( int lives ) {
	if ( cga.vaultEnd || cga.nodeType == 4 ) {
		static const char	*goals[DSV_NUM] = { "GATHER WHAT FALLS", "SHOOT THE CRYSTALS", "MOVE FAST, GATHER MORE", "THE HIGHER, THE RICHER" };

		return va( "THE VAULT  -  %i SECONDS  -  %s", DS_VAULT_TIME / 1000,
			goals[cga.vaultMode >= 0 && cga.vaultMode < DSV_NUM ? cga.vaultMode : DSV_RAIN] );
	}
	if ( cga.boss && cga.timed ) {
		return va( "KILL %s IN %s  -  DIE AS OFTEN AS YOU MUST", ds_bosses[cga.type].name, CGA_Clock( cga.tLimit * 1000 ) );
	}
	if ( cga.boss ) {
		return va( "SLAY %s BEFORE THE OTHERS DO  -  EVERYONE HAS %i LIVES  -  %i RUN %s LEFT", ds_bosses[cga.type].name,
			cga.lives, lives, lives == 1 ? "LIFE" : "LIVES" );
	}
	return NULL;
}

// the corner HUD's second line on boss / vault floors (NULL: frags)
const char *CG_DS_HudLine( void ) {
	int		left;

	if ( cga.vaultEnd ) {
		left = ( cga.vaultEnd - cg.time + 999 ) / 1000;
		return va( "VAULT %i:%02i", left < 0 ? 0 : left / 60, left < 0 ? 0 : left % 60 );
	}
	if ( cga.boss && cga.timed ) {
		return va( "TIME %s", CGA_Clock( CGA_TimeLeft() ) );
	}
	if ( cga.boss ) {
		return va( "LIVES %i/%i", cga.lives - cga.deaths > 0 ? cga.lives - cga.deaths : 0, cga.lives );
	}
	return NULL;
}

/*
===============================================================================

DRAW HELPERS

===============================================================================
*/

static void CGA_Text( float x, float y, const char *s, const float *color, float cw, float ch, int align, qboolean shadow ) {
	float	w = CG_DrawStrlen( s ) * (int)( cw + 0.5f );

	if ( align == 0 ) {
		x -= w * 0.5f;
	} else if ( align > 0 ) {
		x -= w;
	}
	CG_DrawStringExt( (int)x, (int)y, s, color, qtrue, shadow, (int)( cw + 0.5f ), (int)( ch + 0.5f ), 0 );
}

static void CGA_Shake( const vec3_t org, float amplitude, float radius ) {
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

static void CGA_Full( qhandle_t shader, float r, float g, float b, float a ) {
	vec4_t	c;

	c[0] = r; c[1] = g; c[2] = b; c[3] = a;
	trap_R_SetColor( c );
	trap_R_DrawStretchPic( 0, 0, cgs.glconfig.vidWidth, cgs.glconfig.vidHeight, 0, 0, 1, 1, shader );
	trap_R_SetColor( NULL );
}

// a flat quad on the floor, turned by yaw (degrees)
static void CGA_Ground( const vec3_t org, float half, float yaw, const float *rgb, float k, qhandle_t shader ) {
	polyVert_t	v[4];
	float		s, c;
	int			i, col[3];
	static const float	cx[4] = { -1, 1, 1, -1 }, cy[4] = { -1, -1, 1, 1 };

	if ( half < 1 ) {
		return;
	}
	s = sin( DEG2RAD( yaw ) ) * half;
	c = cos( DEG2RAD( yaw ) ) * half;
	for ( i = 0 ; i < 3 ; i++ ) {
		col[i] = (int)( 255 * CG_Clamp01( rgb[i] * k ) );
	}
	for ( i = 0 ; i < 4 ; i++ ) {
		v[i].xyz[0] = org[0] + cx[i] * c - cy[i] * s;
		v[i].xyz[1] = org[1] + cx[i] * s + cy[i] * c;
		v[i].xyz[2] = org[2];
		v[i].st[0] = cx[i] > 0 ? 1 : 0;
		v[i].st[1] = cy[i] > 0 ? 1 : 0;
		v[i].modulate[0] = col[0];
		v[i].modulate[1] = col[1];
		v[i].modulate[2] = col[2];
		v[i].modulate[3] = 255;
	}
	trap_R_AddPolyToScene( shader, 4, v );
}

// a flat strip on the floor from a to b (a lane)
static void CGA_Lane( const vec3_t a, const vec3_t b, float halfW, const float *rgb, float k, qhandle_t shader ) {
	polyVert_t	v[4];
	vec3_t		d, side;
	int			i, col[3];

	VectorSubtract( b, a, d );
	d[2] = 0;
	if ( VectorNormalize( d ) < 1 ) {
		return;
	}
	VectorSet( side, -d[1] * halfW, d[0] * halfW, 0 );
	for ( i = 0 ; i < 3 ; i++ ) {
		col[i] = (int)( 255 * CG_Clamp01( rgb[i] * k ) );
	}
	VectorAdd( a, side, v[0].xyz );
	VectorAdd( b, side, v[1].xyz );
	VectorSubtract( b, side, v[2].xyz );
	VectorSubtract( a, side, v[3].xyz );
	for ( i = 0 ; i < 4 ; i++ ) {
		v[i].xyz[2] += 2;
		v[i].st[0] = ( i == 0 || i == 3 ) ? 0.15f : 0.85f;
		v[i].st[1] = ( i < 2 ) ? 0 : 1;
		v[i].modulate[0] = col[0];
		v[i].modulate[1] = col[1];
		v[i].modulate[2] = col[2];
		v[i].modulate[3] = 255;
	}
	trap_R_AddPolyToScene( shader, 4, v );
}

static void CGA_Sprite( const vec3_t org, float radius, const float *rgb, float k, qhandle_t shader ) {
	refEntity_t	re;

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

static void CGA_Beam( const vec3_t a, const vec3_t b, const float *rgb, float k ) {
	refEntity_t	re;

	memset( &re, 0, sizeof( re ) );
	re.reType = RT_RAIL_CORE;
	re.customShader = cgaRail;
	VectorCopy( a, re.origin );
	VectorCopy( b, re.oldorigin );
	re.shaderRGBA[0] = (byte)( 255 * CG_Clamp01( rgb[0] * k ) );
	re.shaderRGBA[1] = (byte)( 255 * CG_Clamp01( rgb[1] * k ) );
	re.shaderRGBA[2] = (byte)( 255 * CG_Clamp01( rgb[2] * k ) );
	re.shaderRGBA[3] = 255;
	AxisClear( re.axis );
	trap_R_AddRefEntityToScene( &re );
}

// a crisp circle on the floor (beam segments): the edge of a danger zone
static void CGA_Circle( const vec3_t org, float radius, const float *rgb, float k ) {
	vec3_t	a, b;
	int		i, n = radius > 200 ? 24 : 16;

	for ( i = 0 ; i < n ; i++ ) {
		a[0] = org[0] + cos( i * 6.2832f / n ) * radius;
		a[1] = org[1] + sin( i * 6.2832f / n ) * radius;
		b[0] = org[0] + cos( ( i + 1 ) * 6.2832f / n ) * radius;
		b[1] = org[1] + sin( ( i + 1 ) * 6.2832f / n ) * radius;
		a[2] = b[2] = org[2] + 2;
		CGA_Beam( a, b, rgb, k );
	}
}

/*
===============================================================================

BIOMES

===============================================================================
*/

/*
=================
CG_DS_DrawBiome

Under the HUD: the act's colour grade (a multiply that only takes colour away) and a tinted
haze at the edges. Subtle on purpose.
=================
*/
void CG_DS_DrawBiome( void ) {
	const dsAct_t	*a;
	float			k, m, g[3];
	int				act, i;

	if ( !cg.snap || cgaBiome.integer <= 0 ) {
		return;
	}
	act = CGA_Act();
	if ( act < 1 || act > DS_ACT_ABYSS ) {
		return;
	}
	a = &ds_acts[act];
	k = cgaBiome.integer >= 2 ? 1.6f : 1.0f;
	m = a->grade[0];
	for ( i = 1 ; i < 3 ; i++ ) {
		if ( a->grade[i] > m ) {
			m = a->grade[i];
		}
	}
	for ( i = 0 ; i < 3 ; i++ ) {
		g[i] = 1.0f - ( 1.0f - a->grade[i] / m ) * CG_Clamp01( a->grade[3] * k );
	}
	CGA_Full( cgaFilter, g[0], g[1], g[2], 1 );
	CGA_Full( cgaVignette, a->fog[0], a->fog[1], a->fog[2], CG_Clamp01( a->fog[3] * 1.0f * k ) );
	// the boss floor: the edges darken a little more
	if ( cga.boss && !cga.dead ) {
		CGA_Full( cgaVignette, a->fog[0] * 0.5f, 0, 0, 0.1f * k );
	}
}

/*
=================
CG_DS_AddBiome

Ambient particles around the viewer: dust (crypts), embers (foundry), void cgaMotes, ash
=================
*/
void CG_DS_AddBiome( void ) {
	const dsAct_t	*a;
	cgaMote_t		*p;
	vec3_t			rgb, d;
	float			dt, k, flick;
	int				act, i, kind, n;

	if ( !cg.snap || cgaBiome.integer <= 0 || cg.snap->ps.pm_type == PM_INTERMISSION ) {
		return;
	}
	act = CGA_Act();
	if ( act < 1 || act > DS_ACT_ABYSS ) {
		return;
	}
	a = &ds_acts[act];
	kind = a->particles;
	if ( kind == DSFX_NONE ) {
		return;
	}
	dt = cg.frametime * 0.001f;
	if ( dt > 0.1f ) {
		dt = 0.1f;
	}
	n = cgaBiome.integer >= 2 ? DS_MOTES : DS_MOTES * 2 / 3;
	for ( i = 0 ; i < n ; i++ ) {
		p = &cgaMotes[i];
		VectorSubtract( p->org, cg.refdef.vieworg, d );
		if ( !p->alive || fabs( d[0] ) > DS_MOTE_BOX || fabs( d[1] ) > DS_MOTE_BOX || fabs( d[2] ) > DS_MOTE_BOX * 0.6f ) {
			// (re)born somewhere around the viewer, not inside a wall
			p->org[0] = cg.refdef.vieworg[0] + crandom() * DS_MOTE_BOX;
			p->org[1] = cg.refdef.vieworg[1] + crandom() * DS_MOTE_BOX;
			p->org[2] = cg.refdef.vieworg[2] + crandom() * DS_MOTE_BOX * 0.5f;
			p->alive = !( CG_PointContents( p->org, -1 ) & ( CONTENTS_SOLID | CONTENTS_PLAYERCLIP ) );
			p->phase = random() * 6.28f;
			p->size = 0.6f + random() * 0.9f;
			switch ( kind ) {
			case DSFX_EMBERS:	VectorSet( p->vel, crandom() * 12, crandom() * 12, 18 + random() * 30 ); break;
			case DSFX_MOTES:	VectorSet( p->vel, crandom() * 6, crandom() * 6, 6 + random() * 10 ); break;
			case DSFX_ASH:		VectorSet( p->vel, crandom() * 10, crandom() * 10, -( 10 + random() * 16 ) ); break;
			default:			VectorSet( p->vel, crandom() * 5, crandom() * 5, crandom() * 3 ); break;
			}
			if ( !p->alive ) {
				continue;
			}
		}
		p->phase += dt;
		p->org[0] += ( p->vel[0] + sin( p->phase * 0.9f ) * 6 ) * dt;
		p->org[1] += ( p->vel[1] + cos( p->phase * 0.7f ) * 6 ) * dt;
		p->org[2] += p->vel[2] * dt;
		switch ( kind ) {
		case DSFX_EMBERS:
			flick = 0.6f + 0.4f * sin( p->phase * 9.0f );
			VectorSet( rgb, 1.0f, 0.42f, 0.08f );
			k = 0.75f * flick;
			CGA_Sprite( p->org, 1.1f * p->size, rgb, k, cgaGlow );
			break;
		case DSFX_MOTES:
			VectorSet( rgb, 0.45f, 0.5f, 1.0f );
			k = 0.35f + 0.25f * sin( p->phase * 2.0f );
			CGA_Sprite( p->org, 1.4f * p->size, rgb, k, cgaGlow );
			break;
		case DSFX_ASH:
			VectorSet( rgb, 0.5f, 0.42f, 0.38f );
			CGA_Sprite( p->org, 1.3f * p->size, rgb, 0.3f, cgaGlow );
			break;
		default:
			VectorSet( rgb, 0.75f, 0.68f, 0.55f );
			CGA_Sprite( p->org, 0.9f * p->size, rgb, 0.22f, cgaGlow );
			break;
		}
	}
}

/*
===============================================================================

BOSS LOOKS (cg_players.c)

===============================================================================
*/

static void CGA_BossColor( int type, vec3_t out ) {
	switch ( type ) {
	case BOSS_OVERSEER:		VectorSet( out, 1.0f, 0.6f, 0.15f ); break;
	case BOSS_HOLLOWKING:	VectorSet( out, 0.62f, 0.42f, 1.0f ); break;
	case BOSS_ARCHDEMON:	VectorSet( out, 1.0f, 0.38f, 0.06f ); break;
	default:				VectorSet( out, 1.0f, 0.12f, 0.08f ); break;
	}
}

/*
=================
CG_DS_BossScale

From CG_Player after the legs got their place: the boss is drawn bigger (the torso, head and
gun follow the legs' axes), standing on the same floor. Shield bubble, frenzy glow, roar.
=================
*/
void CG_DS_BossScale( centity_t *cent, refEntity_t *legs ) {
	float	s, pulse, speed, k;
	vec3_t	org, rgb, p;
	int		i, n, j;

	// the nemesis (fight floors): a dim dark red glow at its feet, no text
	if ( cga.nem >= 0 && cent->currentState.clientNum == cga.nem && !( cent->currentState.eFlags & EF_DEAD ) ) {
		VectorCopy( cent->lerpOrigin, org );
		org[2] += 4;
		pulse = 0.8f + 0.2f * sin( cg.time * 0.004f );
		trap_R_AddLightToScene( org, 110 * pulse, 0.55f, 0.02f, 0.02f );
	}
	if ( !cga.boss || cent->currentState.clientNum != cga.client || cga.type < 0 || cga.type >= BOSS_NUM ) {
		return;
	}
	s = ds_bosses[cga.type].scale * ( ( cga.traits & DST_TANKY ) ? DS_TANKY_SIZE : 1.0f );
	if ( s <= 0 ) {
		return;
	}
	for ( i = 0 ; i < 3 ; i++ ) {
		VectorScale( legs->axis[i], s, legs->axis[i] );
	}
	legs->nonNormalizedAxes = qtrue;
	legs->origin[2] += 24.0f * ( s - 1.0f );
	VectorCopy( legs->origin, legs->oldorigin );
	if ( cent->currentState.eFlags & EF_DEAD ) {
		return;
	}
	VectorCopy( cent->lerpOrigin, org );
	org[2] += 20 * s;
	pulse = 0.5f + 0.5f * sin( cg.time * 0.008f );
	if ( cga.shield ) {
		VectorSet( rgb, 0.3f, 0.75f, 1.0f );
		CGA_Sprite( org, 62 * s, rgb, 0.22f + 0.1f * pulse, cgaRing );
		CGA_Sprite( org, 70 * s, rgb, 0.08f, cgaGlow );
		trap_R_AddLightToScene( org, 160 + 40 * pulse, 0.3f, 0.7f, 1.0f );
	}
	if ( cga.frenzy && !cga.dead ) {
		trap_R_AddLightToScene( org, 140 + 80 * pulse, 1.0f, 0.1f, 0.05f );
	}
	if ( cga.roar > cg.time ) {
		CGA_BossColor( cga.type, rgb );
		CGA_Sprite( org, 40 * s + 30 * pulse, rgb, 0.6f, cgaGlow );
	}
	CGA_BossColor( cga.type, rgb );
	// STRONG: embers smoulder in its hands (a red-hot glow at chest height)
	if ( cga.traits & DST_STRONG ) {
		VectorCopy( cent->lerpOrigin, p );
		p[2] += 34 * s;
		trap_R_AddLightToScene( p, 90 + 50 * pulse, 1.0f, 0.3f, 0.05f );
		if ( cg.time - cga.emberTime > 90 || cga.emberTime > cg.time ) {
			vec3_t	v;

			cga.emberTime = cg.time;
			VectorSet( v, crandom() * 12, crandom() * 12, 30 + random() * 20 );
			p[0] += crandom() * 14 * s;
			p[1] += crandom() * 14 * s;
			CG_SmokePuff( p, v, 4 + random() * 3, 1.0f, 0.45f, 0.1f, 0.9f, 500, cg.time, 0, 0, cgs.media.flameShader );
		}
	}
	// FAST: a streak of where it just was (only when it really moves)
	if ( cga.traits & DST_FAST ) {
		if ( cg.time >= cga.trailNext ) {
			cga.trailNext = cg.time + 35;
			VectorCopy( cent->lerpOrigin, cga.trail[cga.trailHead] );
			cga.trailHead = ( cga.trailHead + 1 ) % 8;
			if ( cga.trailCount < 8 ) {
				cga.trailCount++;
			}
		}
		speed = VectorLength( cent->currentState.pos.trDelta );
		if ( speed > 260 ) {
			k = CG_Clamp01( ( speed - 260 ) / 300.0f );
			n = cga.trailCount;
			for ( i = 1 ; i < n ; i++ ) {
				j = ( cga.trailHead - 1 - i + 16 ) % 8;
				VectorCopy( cga.trail[j], p );
				p[2] += 22 * s;
				CGA_Sprite( p, ( 30 - 2.5f * i ) * s, rgb, k * 0.45f * ( 1.0f - (float)i / n ), cgaGlow );
			}
		}
	} else {
		cga.trailCount = 0;
	}
}

/*
=================
CG_DS_AnimSpeed

cg_players.c: a fast boss moves its limbs quicker, a slow one heavier
=================
*/
float CG_DS_AnimSpeed( centity_t *cent ) {
	if ( !cga.boss || cent->currentState.clientNum != cga.client ) {
		return 1.0f;
	}
	return ( cga.traits & DST_FAST ) ? 1.25f : 0.88f;
}

/*
=================
CG_DS_BodyShell

CG_RL_BodyShell: the nemesis's dark red rim, a tanky boss's steel sheen (qtrue = handled)
=================
*/
qboolean CG_DS_BodyShell( refEntity_t *ent, entityState_t *state ) {
	if ( state->eFlags & EF_DEAD || state->eType != ET_PLAYER ) {
		return qfalse;
	}
	if ( cga.nem >= 0 && state->clientNum == cga.nem && cgaNemShell ) {
		ent->customShader = cgaNemShell;
		trap_R_AddRefEntityToScene( ent );
		return qfalse;		// (an elite aura still shows)
	}
	if ( cga.boss && state->clientNum == cga.client && ( cga.traits & DST_TANKY ) && cgaArmourShell ) {
		ent->customShader = cgaArmourShell;
		trap_R_AddRefEntityToScene( ent );
		return qtrue;
	}
	return qfalse;
}

/*
===============================================================================

TELEGRAPHS (RM_ENT_BOSSFX)

===============================================================================
*/

static void CGA_SkyStart( centity_t *cent, float maxH ) {
	trace_t	tr;
	vec3_t	up;

	if ( cent->dustTrailTime == cent->currentState.time ) {
		return;
	}
	cent->dustTrailTime = cent->currentState.time;
	VectorCopy( cent->lerpOrigin, up );
	up[2] += maxH;
	CG_Trace( &tr, cent->lerpOrigin, NULL, NULL, up, ENTITYNUM_NONE, CONTENTS_SOLID );
	VectorCopy( tr.endpos, cent->beamEnd );
	cent->beamEnd[2] -= 16;
	// come in at a slant
	cent->beamEnd[0] += ( ( cent->currentState.number * 37 ) % 120 ) - 60;
	cent->beamEnd[1] += ( ( cent->currentState.number * 53 ) % 120 ) - 60;
}

void CG_DS_BossFx( centity_t *cent ) {
	entityState_t	*es = &cent->currentState;
	vec3_t			org, to, rgb, p, d;
	float			total, frac, radius, pulse, t, R, ang;
	int				kind = es->modelindex2, i, k, rings, live;
	sfxHandle_t		snd;
	refEntity_t		re;

	VectorCopy( cent->lerpOrigin, org );
	VectorCopy( es->origin2, to );
	total = es->time2 - es->time;
	frac = total > 0 ? CG_Clamp01( (float)( cg.time - es->time ) / total ) : 1.0f;
	radius = es->otherEntityNum2 > 0 ? es->otherEntityNum2 : 120;
	pulse = 0.5f + 0.5f * sin( cg.time * ( 0.012f + 0.03f * frac ) );
	live = es->frame;
	CGA_BossColor( cga.type, rgb );

	// the warning: once, when it first shows up
	if ( cent->miscTime != es->time ) {
		cent->miscTime = es->time;
		if ( cg_debugEvents.integer ) {
			CG_Printf( "bossfx %i: %s at %i %i %i, lands in %i ms\n", es->number, kind > 0 && kind < DSB_NUM ? ds_bossAttackNames[kind] : "?",
				(int)org[0], (int)org[1], (int)org[2], es->time2 - cg.time );
		}
		cent->trailTime = 0;
		snd = cgaWarn;
		switch ( kind ) {
		case DSB_HOOK:		snd = cgaHook; break;
		case DSB_LANCE:		snd = cgaCharge; break;
		case DSB_BLINK:
		case DSB_STALK:
		case DSB_CLONES:	snd = cgaBlink; break;
		case DSB_WELL:		snd = cgaWell; break;
		case DSB_ROAR:		snd = cgaRoar; break;
		case DSB_SHIELD:	snd = cgaShieldSnd; break;
		case DSB_NOVA:		snd = cgaNovaSnd; break;
		default:			break;
		}
		if ( !live || kind == DSB_ROAR ) {
			// circles near the player warn loud and local, the rest where they are
			if ( kind == DSB_HOOK || kind == DSB_LANCE || kind == DSB_ROAR || Distance( org, cg.refdef.vieworg ) < 700 ) {
				trap_S_StartLocalSound( snd, CHAN_LOCAL_SOUND );
			} else {
				trap_S_StartSound( org, ENTITYNUM_WORLD, CHAN_AUTO, snd );
			}
		}
	}
	// the moment it goes off (once)
	if ( live && cent->trailTime != 1 ) {
		cent->trailTime = 1;
		if ( kind == DSB_SLAM || kind == DSB_NOVA ) {
			CGA_Shake( org, ( cga.traits & DST_STRONG ) ? 16 : 10, ( cga.traits & DST_STRONG ) ? 1300 : 900 );
		} else if ( ( cga.traits & DST_STRONG ) && ( kind == DSB_CLEAVER || kind == DSB_MORTAR || kind == DSB_METEOR || kind == DSB_HOOK ) ) {
			CGA_Shake( org, 9, 900 );		// a strong boss's blows land heavy
			if ( kind != DSB_HOOK ) {
				VectorSet( d, 0, 0, 1 );
				CG_SmokePuff( org, d, 60, 1.0f, 0.5f, 0.2f, 0.7f, 450, cg.time, 0, 0, cgs.media.flameShader );
			}
		}
	}

	switch ( kind ) {
	case DSB_HOOK:
		org[2] -= 6;
		if ( !live ) {
			// a dashed red line to you; brighter when it locks
			k = es->time2 - cg.time < 450;		// locked
			CGA_Beam( org, to, rgb, ( k ? 0.7f : 0.35f ) + 0.3f * pulse );
			CGA_Sprite( to, 10 + 10 * frac, rgb, 0.8f, cgaGlow );
			// the hook hand glows: readable when the line points straight at you
			CGA_Sprite( org, 7 + 7 * frac + 3 * pulse, rgb, 0.9f, cgaGlow );
			trap_R_AddLightToScene( org, 80 + 120 * frac, rgb[0], rgb[1], rgb[2] );
		} else {
			// the chain flies
			VectorSet( rgb, 0.9f, 0.85f, 0.8f );
			CGA_Beam( org, to, rgb, 1.0f );
			CGA_Sprite( to, 16, rgb, 0.9f, cgaGlow );
		}
		break;

	case DSB_CLEAVER:
		// the lane he will charge down: a glow and two hard edges on the floor
		VectorCopy( org, p );
		p[2] -= 20;
		to[2] = p[2];
		CGA_Lane( p, to, radius, rgb, live ? 0.6f : 0.2f + 0.35f * frac, cgaGlow );
		CGA_Lane( p, to, radius * 0.3f, rgb, live ? 0.7f : 0.35f + 0.3f * pulse, cgaGlow );
		VectorSubtract( to, p, d );
		d[2] = 0;
		if ( VectorNormalize( d ) > 1 ) {
			vec3_t	a1, b1, side;

			p[2] += 3;
			to[2] = p[2];
			VectorSet( side, -d[1] * radius, d[0] * radius, 0 );
			VectorAdd( p, side, a1 );
			VectorAdd( to, side, b1 );
			CGA_Beam( a1, b1, rgb, 0.5f + 0.5f * frac );
			VectorSubtract( p, side, a1 );
			VectorSubtract( to, side, b1 );
			CGA_Beam( a1, b1, rgb, 0.5f + 0.5f * frac );
		}
		CGA_Sprite( to, 14 + 10 * frac, rgb, 0.7f, cgaGlow );
		break;

	case DSB_SLAM:
	case DSB_BARRAGE:
	case DSB_MORTAR:
	case DSB_METEOR:
	case DSB_FIREFLOOR:
		org[2] += 1.5f;
		if ( kind == DSB_SLAM && live ) {
			// the shockwave rolls out
			t = CG_Clamp01( ( cg.time - es->time2 ) / 350.0f );
			CGA_Ground( org, radius * t / 0.8f + 8, 0, rgb, 1.0f - t, cgaRing );
			CGA_Ground( org, radius * 0.9f, 0, rgb, 0.25f * ( 1.0f - t ), cgaGlow );
			break;
		}
		if ( live ) {
			break;		// landed: the blast is an event
		}
		if ( kind == DSB_FIREFLOOR ) {
			VectorSet( rgb, 1.0f, 0.45f, 0.08f );
		} else if ( kind == DSB_METEOR ) {
			VectorSet( rgb, 1.0f, 0.4f, 0.1f );
		}
		CGA_Circle( org, radius, rgb, 0.55f + 0.45f * frac );
		CGA_Ground( org, radius * 0.95f, 0, rgb, 0.12f + 0.2f * frac, cgaGlow );
		CGA_Ground( org, radius / 0.8f, 0, rgb, 0.6f + 0.35f * pulse, cgaRing );
		CGA_Ground( org, radius / 0.8f + 3, 0, rgb, 0.4f, cgaRing );
		CGA_Ground( org, radius * ( 1.0f - frac ) / 0.8f + 4, 0, rgb, 0.55f + 0.45f * frac, cgaRing );
		trap_R_AddLightToScene( org, 50 + 110 * frac, rgb[0], rgb[1], rgb[2] );
		if ( kind == DSB_METEOR || kind == DSB_MORTAR ) {
			// the thing that falls: a rock all the way down, a shell for the last moment
			CGA_SkyStart( cent, kind == DSB_METEOR ? 1100 : 700 );
			t = kind == DSB_METEOR ? frac : CG_Clamp01( ( cg.time - ( es->time2 - 450 ) ) / 450.0f );
			if ( t > 0 ) {
				VectorSubtract( org, cent->beamEnd, d );
				VectorMA( cent->beamEnd, t, d, p );
				if ( kind == DSB_METEOR && cgaMeteor ) {
					memset( &re, 0, sizeof( re ) );
					VectorCopy( p, re.origin );
					VectorCopy( p, re.oldorigin );
					to[0] = cg.time * 0.31f;
					to[1] = cg.time * 0.17f;
					to[2] = cg.time * 0.23f;
					AnglesToAxis( to, re.axis );
					for ( i = 0 ; i < 3 ; i++ ) {
						VectorScale( re.axis[i], 1.2f, re.axis[i] );
					}
					re.nonNormalizedAxes = qtrue;
					re.hModel = cgaMeteor;
					trap_R_AddRefEntityToScene( &re );
					if ( cg.time - cent->errorTime > 35 ) {
						cent->errorTime = cg.time;
						VectorSet( d, crandom() * 20, crandom() * 20, 30 );
						CG_SmokePuff( p, d, 22 + random() * 10, 1.0f, 0.55f, 0.2f, 0.9f, 320, cg.time, 0, 0, cgs.media.flameShader );
					}
				}
				VectorSet( to, 1.0f, 0.7f, 0.3f );
				CGA_Sprite( p, kind == DSB_METEOR ? 40 : 14, to, 0.6f, cgaGlow );
				trap_R_AddLightToScene( p, 180, 1.0f, 0.55f, 0.2f );
			}
		}
		break;

	case DSB_BLINK:
	case DSB_STALK:
		// a glow where he stands and one where he will be (the hunt: in the boss's colour)
		if ( kind == DSB_BLINK ) {
			VectorSet( rgb, 0.62f, 0.42f, 1.0f );
		}
		CGA_Sprite( org, 20 + 30 * frac, rgb, live ? 0.2f : 0.5f + 0.3f * pulse, cgaGlow );
		if ( !live ) {
			// the shadow step's path: a dim line from where it stands to where it will be
			if ( kind == DSB_BLINK ) {
				vec3_t	a, b2;

				VectorCopy( org, a );
				VectorCopy( to, b2 );
				a[2] += 20;
				b2[2] += 20;
				CGA_Beam( a, b2, rgb, 0.25f + 0.45f * frac );
			}
			VectorCopy( to, p );
			p[2] += 8;
			CGA_Sprite( p, 18 + 34 * frac, rgb, 0.7f + 0.3f * pulse, cgaGlow );
			p[2] -= 6;
			CGA_Ground( p, 40 / 0.8f, 0, rgb, 0.5f + 0.3f * pulse, cgaRing );
			trap_R_AddLightToScene( p, 80 + 120 * frac, rgb[0], rgb[1], rgb[2] );
		}
		break;

	case DSB_LANCE:
		VectorSet( rgb, 0.85f, 0.3f, 1.0f );
		if ( !live ) {
			CGA_Beam( org, to, rgb, 0.18f + 0.55f * frac + 0.2f * pulse );
			CGA_Sprite( org, 6 + 14 * frac, rgb, 0.8f, cgaGlow );
		}
		break;

	case DSB_WELL:
		VectorSet( rgb, 0.55f, 0.35f, 1.0f );
		org[2] += 2;
		if ( !live ) {
			CGA_Circle( org, radius, rgb, 0.5f + 0.5f * frac );
			CGA_Ground( org, radius / 0.8f, cg.time * 0.05f, rgb, 0.2f + 0.3f * frac, cgaRing );
			CGA_Ground( org, radius * ( 1.0f - frac ) / 0.8f + 10, 0, rgb, 0.5f, cgaRing );
			break;
		}
		// open: a turning vortex, cgaMotes spiralling in, a dark heart
		t = ( cg.time - es->time ) * 0.001f;
		for ( i = 0 ; i < 3 ; i++ ) {
			CGA_Ground( org, ( radius * ( 0.35f + 0.3f * i ) ) / 0.8f, t * ( 90 + 50 * i ) * ( i & 1 ? -1 : 1 ), rgb, 0.3f - 0.06f * i, cgaRing );
		}
		CGA_Circle( org, radius, rgb, 0.35f + 0.15f * pulse );
		for ( i = 0 ; i < 14 ; i++ ) {
			R = radius * ( 1.0f - ( t * 0.45f + i / 14.0f - (int)( t * 0.45f + i / 14.0f ) ) );
			ang = i * 0.449f * 6.28f + t * 2.2f + R * 0.006f;
			p[0] = org[0] + cos( ang ) * R;
			p[1] = org[1] + sin( ang ) * R;
			p[2] = org[2] + 10 + 30 * ( R / radius );
			CGA_Sprite( p, 5, rgb, 0.7f, cgaGlow );
		}
		VectorCopy( org, p );
		p[2] += 24;
		CGA_Sprite( p, 34 + 6 * pulse, rgb, 0.9f, cgaGlow );
		trap_R_AddLightToScene( p, 220, 0.5f, 0.3f, 1.0f );
		break;

	case DSB_NOVA:
		VectorSet( rgb, 1.0f, 0.42f, 0.06f );
		org[2] += 2;
		if ( !live ) {
			// the demon gathers fire
			CGA_Circle( org, radius, rgb, 0.5f + 0.5f * frac );
			CGA_Ground( org, radius / 0.8f, 0, rgb, 0.4f + 0.4f * frac, cgaRing );
			p[0] = org[0]; p[1] = org[1]; p[2] = org[2] + 40;
			CGA_Sprite( p, 30 + 50 * frac, rgb, 0.5f + 0.3f * pulse, cgaGlow );
			trap_R_AddLightToScene( p, 120 + 200 * frac, 1.0f, 0.45f, 0.1f );
			break;
		}
		rings = es->otherEntityNum2 > 0 && es->otherEntityNum2 < 4 ? es->otherEntityNum2 : 1;
		t = ( cg.time - es->time2 ) * 0.001f;
		for ( k = 0 ; k < rings ; k++ ) {
			R = 560.0f * ( t - k * 0.7f );
			if ( R < 0 || R > 760 ) {
				continue;
			}
			CGA_Circle( org, R, rgb, 1.0f - R / 900.0f );
			CGA_Ground( org, R / 0.8f + 10, 0, rgb, 0.9f * ( 1.0f - R / 900.0f ), cgaRing );
			CGA_Ground( org, R / 0.8f + 26, 0, rgb, 0.4f * ( 1.0f - R / 900.0f ), cgaRing );
			for ( i = 0 ; i < 18 ; i++ ) {
				ang = i * ( 6.2832f / 18 ) + k;
				p[0] = org[0] + cos( ang ) * R;
				p[1] = org[1] + sin( ang ) * R;
				p[2] = org[2] + 14;
				CGA_Sprite( p, 16, rgb, 0.65f, cgaGlow );
			}
		}
		break;

	case DSB_DRONES:
	case DSB_CLONES:
		CGA_Sprite( org, 10 + 30 * frac, rgb, 0.4f + 0.5f * frac, cgaGlow );
		trap_R_AddLightToScene( org, 60 + 120 * frac, rgb[0], rgb[1], rgb[2] );
		break;

	case DSB_SHIELD:
		VectorSet( rgb, 0.3f, 0.75f, 1.0f );
		VectorCopy( org, p );
		p[2] += 20;
		CGA_Sprite( p, 30 + 40 * frac, rgb, 0.35f + 0.3f * pulse, cgaRing );
		break;

	case DSB_ROAR:
		t = CG_Clamp01( ( cg.time - es->time ) / 600.0f );
		org[2] += 2;
		CGA_Ground( org, radius * t / 0.8f + 10, 0, rgb, 1.0f - t, cgaRing );
		if ( cent->trailTime != 2 ) {
			cent->trailTime = 2;
			CGA_Shake( org, 12, 1200 );
		}
		break;

	default:
		break;
	}
}

/*
===============================================================================

DRONES AND DECOYS (RM_ENT_BOSSPROP)

===============================================================================
*/

void CG_DS_BossProp( centity_t *cent ) {
	entityState_t	*es = &cent->currentState;
	clientInfo_t	*ci;
	refEntity_t		legs, torso, head;
	vec3_t			ang, rgb, eye;
	float			s, in;
	int				i, f;
	animation_t		*an;

	in = CG_Clamp01( ( cg.time - es->time ) / 300.0f );
	if ( es->modelindex2 == 0 ) {
		// a scrap drone: a turning hunk of metal with a burning eye
		memset( &legs, 0, sizeof( legs ) );
		VectorCopy( cent->lerpOrigin, legs.origin );
		VectorCopy( cent->lerpOrigin, legs.oldorigin );
		ang[0] = 20 * sin( cg.time * 0.002f );
		ang[1] = cg.time * 0.12f;
		ang[2] = 15 * cos( cg.time * 0.0017f );
		AnglesToAxis( ang, legs.axis );
		for ( i = 0 ; i < 3 ; i++ ) {
			VectorScale( legs.axis[i], 2.2f * in, legs.axis[i] );
		}
		legs.nonNormalizedAxes = qtrue;
		legs.hModel = cgaDrone;
		trap_R_AddRefEntityToScene( &legs );
		VectorSet( rgb, 1.0f, 0.5f, 0.1f );
		VectorMA( cent->lerpOrigin, 10, legs.axis[0], eye );
		CGA_Sprite( eye, 9 + 2 * sin( cg.time * 0.02f ), rgb, 1.0f, cgaGlow );
		trap_R_AddLightToScene( cent->lerpOrigin, 110, 1.0f, 0.5f, 0.15f );
		return;
	}
	// a decoy: the boss's own body, standing, ghost-lit
	if ( !cga.boss || cga.client < 0 || cga.client >= MAX_CLIENTS ) {
		return;
	}
	ci = &cgs.clientinfo[cga.client];
	if ( !ci->infoValid || !ci->legsModel || !ci->torsoModel || !ci->headModel ) {
		return;
	}
	s = ds_bosses[cga.type].scale * in;
	memset( &legs, 0, sizeof( legs ) );
	memset( &torso, 0, sizeof( torso ) );
	memset( &head, 0, sizeof( head ) );
	VectorSet( ang, 0, es->apos.trBase[YAW], 0 );
	AnglesToAxis( ang, legs.axis );
	for ( i = 0 ; i < 3 ; i++ ) {
		VectorScale( legs.axis[i], s, legs.axis[i] );
	}
	legs.nonNormalizedAxes = qtrue;
	VectorCopy( cent->lerpOrigin, legs.origin );
	legs.origin[2] += 24.0f * ( s - 1.0f );
	VectorCopy( legs.origin, legs.oldorigin );
	VectorCopy( cent->lerpOrigin, legs.lightingOrigin );
	legs.renderfx = RF_LIGHTING_ORIGIN;
	an = &ci->animations[LEGS_IDLE];
	f = an->numFrames > 0 && an->frameLerp > 0 ? ( cg.time / an->frameLerp ) % an->numFrames : 0;
	legs.frame = legs.oldframe = an->firstFrame + f;
	legs.hModel = ci->legsModel;
	legs.customSkin = ci->legsSkin;
	trap_R_AddRefEntityToScene( &legs );

	an = &ci->animations[TORSO_STAND];
	f = an->numFrames > 0 && an->frameLerp > 0 ? ( cg.time / an->frameLerp ) % an->numFrames : 0;
	torso.frame = torso.oldframe = an->firstFrame + f;
	AxisClear( torso.axis );
	CG_PositionRotatedEntityOnTag( &torso, &legs, ci->legsModel, "tag_torso" );
	torso.nonNormalizedAxes = qtrue;
	torso.renderfx = RF_LIGHTING_ORIGIN;
	VectorCopy( cent->lerpOrigin, torso.lightingOrigin );
	torso.hModel = ci->torsoModel;
	torso.customSkin = ci->torsoSkin;
	trap_R_AddRefEntityToScene( &torso );

	AxisClear( head.axis );
	CG_PositionRotatedEntityOnTag( &head, &torso, ci->torsoModel, "tag_head" );
	head.nonNormalizedAxes = qtrue;
	head.renderfx = RF_LIGHTING_ORIGIN;
	VectorCopy( cent->lerpOrigin, head.lightingOrigin );
	head.hModel = ci->headModel;
	head.customSkin = ci->headSkin;
	trap_R_AddRefEntityToScene( &head );

	// the same boss aura, so you can't tell at a glance
	CG_RL_BodyShell( &legs, es );
	CG_RL_BodyShell( &torso, es );
	CG_RL_BodyShell( &head, es );
	VectorSet( rgb, 0.62f, 0.42f, 1.0f );
	VectorCopy( cent->lerpOrigin, eye );
	eye[2] += 8;
	trap_R_AddLightToScene( eye, 120, rgb[0], rgb[1], rgb[2] );
}

/*
===============================================================================

2D: act intro, vault timer, boss bar

===============================================================================
*/

static void CGA_DrawActIntro( void ) {
	const dsAct_t	*a;
	vec4_t			col, line;
	float			t, al, x, y, slide, w;
	int				act;
	const char		*head;

	act = CGA_Act();
	if ( act < 1 || act > DS_ACT_ABYSS ) {
		return;
	}
	t = cg.time - cga.firstIn;
	if ( t < 0 || t > ACT_INTRO_TIME ) {
		return;
	}
	a = &ds_acts[act];
	al = t < 300 ? t / 300.0f : t > ACT_INTRO_TIME - 700 ? ( ACT_INTRO_TIME - t ) / 700.0f : 1.0f;
	slide = 18.0f * ( 1.0f - CG_Clamp01( t / 450.0f ) ) * ( 1.0f - CG_Clamp01( t / 450.0f ) );
	x = 614 + slide;		// off-centre: the top right of the play view (the corner HUD is left)
	y = 40;

	head = act == DS_ACT_ABYSS ? "BEYOND THE LAST ACT" : va( "ACT %s", ds_roman[act] );
	col[0] = 1.0f; col[1] = 0.55f; col[2] = 0.15f; col[3] = 0.95f * al;
	CGA_Text( x, y, head, col, 6, 9, 1, qtrue );
	w = CG_DrawStrlen( a->name ) * 14;
	line[0] = 0.5f + 0.5f * a->fog[0] / ( a->fog[0] + a->fog[1] + a->fog[2] + 0.01f );
	line[1] = 0.35f;
	line[2] = 0.2f + 0.6f * a->fog[2] / ( a->fog[0] + a->fog[1] + a->fog[2] + 0.01f );
	line[3] = 0.7f * al;
	CG_FillRect( x - w * CG_Clamp01( t / 500.0f ), y + 12, w * CG_Clamp01( t / 500.0f ), 1, line );
	col[0] = 0.95f; col[1] = 0.9f; col[2] = 0.8f; col[3] = al;
	CGA_Text( x, y + 16, a->name, col, 14, 20, 1, qtrue );
	col[0] = col[1] = col[2] = 0.72f; col[3] = 0.85f * al;
	CGA_Text( x, y + 40, a->sub, col, 5, 8, 1, qtrue );
}

// the vault's modes (DSV_): the card's title and line, the hint under the clock
static const char *cgaVaultTitle[DSV_NUM] = { "THE VAULT", "SHOOTING GALLERY", "RUSH", "SKY VAULT" };
static const char *cgaVaultLine[DSV_NUM] = {
	"Gems fall like rain. A chest waits. Take what you can.",
	"Crystals float in the air. Shoot them and they burst into gems.",
	"Gems appear ahead of you. The faster you move, the more you get.",
	"Gems hang in the air. The higher they are, the more they are worth."
};
static const char *cgaVaultHint[DSV_NUM] = { "", "SHOOT THE CRYSTALS", "KEEP MOVING FAST", "JUMP HIGH" };

static void CGA_DrawVault( void ) {
	vec4_t	col, bg;
	float	t, al, spd, *v;
	int		left, mode = cga.vaultMode >= 0 && cga.vaultMode < DSV_NUM ? cga.vaultMode : DSV_RAIN, tier;

	t = cg.time - cga.firstIn;
	if ( t >= 0 && t < ACT_INTRO_TIME ) {
		al = t < 300 ? t / 300.0f : t > ACT_INTRO_TIME - 700 ? ( ACT_INTRO_TIME - t ) / 700.0f : 1.0f;
		col[0] = 1.0f; col[1] = 0.82f; col[2] = 0.3f; col[3] = 0.95f * al;
		CGA_Text( 614, 40, "A SECRET", col, 6, 9, 1, qtrue );
		col[0] = 0.95f; col[1] = 0.9f; col[2] = 0.8f; col[3] = al;
		CGA_Text( 614, 56, cgaVaultTitle[mode], col, 14, 20, 1, qtrue );
		col[0] = col[1] = col[2] = 0.72f; col[3] = 0.85f * al;
		CGA_Text( 614, 80, cgaVaultLine[mode], col, 5, 8, 1, qtrue );
	}
	if ( !cga.vaultEnd || cg.showScores || t < ACT_INTRO_TIME ) {		// (the floor strip is up there first)
		return;
	}
	left = ( cga.vaultEnd - cg.time + 999 ) / 1000;
	if ( left < 0 ) {
		left = 0;
	}
	bg[0] = bg[1] = bg[2] = 0; bg[3] = 0.3f;
	CG_FillRect( 320 - 34, 6, 68, 18, bg );
	col[0] = 1.0f; col[1] = left <= 5 ? 0.35f : 0.82f; col[2] = left <= 5 ? 0.2f : 0.3f; col[3] = 0.95f;
	CGA_Text( 320, 9, va( "%i:%02i", left / 60, left % 60 ), col, 9, 12, 0, qtrue );
	// the mode's hint under the clock; RUSH: the speed tier, live
	if ( mode == DSV_RUSH ) {
		v = cg.predictedPlayerState.velocity;
		spd = sqrt( v[0] * v[0] + v[1] * v[1] );
		tier = spd >= DSV_RUSH_BLAZE ? 3 : spd >= DSV_RUSH_FAST ? 2 : spd >= DSV_RUSH_RUN ? 1 : 0;
		bg[3] = 0.3f;
		CG_FillRect( 320 - 50, 27, 100, 5, bg );
		col[0] = tier == 3 ? 1.0f : tier == 2 ? 0.45f : 0.6f;
		col[1] = tier == 3 ? 0.55f : tier == 2 ? 1.0f : 0.6f;
		col[2] = tier == 3 ? 0.15f : tier == 2 ? 0.45f : 0.6f;
		col[3] = tier ? 0.9f : 0.4f;
		CG_FillRect( 320 - 49, 28, 98 * CG_Clamp01( spd / ( DSV_RUSH_BLAZE * 1.15f ) ), 3, col );
		CGA_Text( 320, 35, tier == 3 ? "BLAZING  x4" : tier == 2 ? "FAST  x2" : tier == 1 ? "RUNNING  x1" : "MOVE!", col, 5, 7, 0, qtrue );
	} else if ( mode == DSV_GALLERY ) {
		col[0] = 1.0f; col[1] = 0.9f; col[2] = 0.7f; col[3] = 0.8f;
		CGA_Text( 320, 28, va( "%s  -  %i", cgaVaultHint[mode], cga.vaultBroken ), col, 5, 7, 0, qtrue );
	} else if ( mode != DSV_RAIN ) {
		col[0] = 1.0f; col[1] = 0.9f; col[2] = 0.7f; col[3] = 0.8f;
		CGA_Text( 320, 28, cgaVaultHint[mode], col, 5, 7, 0, qtrue );
	}
}

/*
=================
CGA_DrawClock

Round 4 (en-boss): the timed boss's clock, right of the boss bar at the top (never the centre):
big, warm white; amber in the last minute; red in the last 30 s, punching up on every second
(harder in the last 10, with tenths). Dim before the fight starts and frozen at the kill.
=================
*/
static void CGA_DrawClock( float x, float ymid, float al ) {
	vec4_t		col, bg;
	int			left = CGA_TimeLeft(), sec;
	float		cw = 13, ch = 19, s = 1.0f, k;
	qboolean	live;
	const char	*txt;

	if ( left < 0 ) {
		return;
	}
	live = cga.tEnd > 0 && !cga.tKill && !cga.tUp && !cga.dead;
	sec = ( left + 999 ) / 1000;
	if ( cga.tKill > 0 ) {
		col[0] = 1.0f; col[1] = 0.82f; col[2] = 0.3f; col[3] = al;
	} else if ( !live && !cga.tUp ) {
		col[0] = col[1] = col[2] = 0.75f; col[3] = 0.7f * al;
	} else if ( sec > 60 ) {
		col[0] = 0.95f; col[1] = 0.92f; col[2] = 0.85f; col[3] = al;
	} else if ( sec > 30 ) {
		col[0] = 1.0f; col[1] = 0.72f; col[2] = 0.2f; col[3] = al;
	} else {
		col[0] = 1.0f; col[1] = 0.22f; col[2] = 0.12f; col[3] = al;
		if ( live && left > 0 ) {
			k = ( left % 1000 ) / 1000.0f;		// 1 right after a second ticked over
			s = 1.0f + ( sec <= 10 ? 0.35f : 0.18f ) * k * k;
			col[1] += 0.6f * k * k;
			col[2] += 0.5f * k * k;
		} else if ( cga.tUp ) {
			col[3] = al * ( 0.55f + 0.45f * sin( cg.time * 0.012f ) );
		}
	}
	if ( live && left > 0 && left < 10000 ) {
		txt = va( "%i.%i", left / 1000, ( left % 1000 ) / 100 );
	} else {
		txt = CGA_Clock( left );
	}
	bg[0] = 0.02f; bg[1] = 0.0f; bg[2] = 0.0f; bg[3] = ( live && sec <= 30 ? 0.6f : 0.45f ) * al;
	CG_FillRect( x - 4, ymid - ch * 0.5f - 3, 5 * cw + 8, ch + 6, bg );
	if ( live && sec <= 30 ) {
		bg[0] = 0.85f; bg[1] = 0.08f; bg[2] = 0.05f; bg[3] = 0.9f * al;
		CG_FillRect( x - 4, ymid + ch * 0.5f + 2, ( 5 * cw + 8 ) * ( left / 30000.0f ), 1.5f, bg );		// the last 30 s drain
	}
	CGA_Text( x, ymid - ch * s * 0.5f, txt, col, cw * s, ch * s, -1, qtrue );
}

/*
=================
CGA_DrawBossLocator

Where the boss is (the server always sends it): on screen, a diamond over its head and the
distance, through walls; off screen, a chevron on a ring around the crosshair pointing the way.
Faint while it is in plain sight and close.
=================
*/
static void CGA_LocatorDot( float x, float y, float s, const float *col ) {
	CG_FillRect( x - s * 0.5f, y - s * 0.5f, s, s, col );
}

static void CGA_DrawBossLocator( void ) {
	centity_t	*cent;
	vec3_t		head, d, rgb;
	trace_t		tr;
	float		fwd, side, up, px, py, vx, vy, dist, al, pulse, tx, ty, ang, c, sn, r, k, w, left, right;
	vec4_t		col;
	int			i, meters;
	qboolean	onScreen, seen;

	if ( !cga.boss || cga.client < 0 || cga.client >= MAX_CLIENTS || cga.dead || cga.tUp
		|| ( cg.snap->ps.pm_type != PM_NORMAL && cg.snap->ps.pm_type != PM_NOCLIP ) || cg.snap->ps.stats[STAT_HEALTH] <= 0 || CG_DSI_Busy() ) {
		return;
	}
	cent = &cg_entities[cga.client];
	if ( !cent->currentValid || ( cent->currentState.eFlags & EF_DEAD ) ) {
		return;
	}
	VectorCopy( cent->lerpOrigin, head );
	head[2] += 30.0f + 34.0f * ( cga.type >= 0 && cga.type < BOSS_NUM ? ds_bosses[cga.type].scale : 1.0f );
	VectorSubtract( head, cg.refdef.vieworg, d );
	dist = VectorLength( d );
	meters = (int)( dist / 32.0f + 0.5f );		// (32 units ~ a metre)
	fwd = DotProduct( d, cg.refdef.viewaxis[0] );
	side = DotProduct( d, cg.refdef.viewaxis[1] );
	up = DotProduct( d, cg.refdef.viewaxis[2] );
	CG_Trace( &tr, cg.refdef.vieworg, NULL, NULL, head, cg.snap->ps.clientNum, MASK_SOLID );
	seen = tr.fraction >= 1.0f;
	pulse = 0.5f + 0.5f * sin( cg.time * 0.008f );
	CGA_BossColor( cga.type, rgb );
	onScreen = qfalse;
	px = py = 0;
	if ( fwd > 8.0f ) {
		px = cg.refdef.x + cg.refdef.width * 0.5f * ( 1.0f - side / fwd / tan( cg.refdef.fov_x * M_PI / 360.0f ) );
		py = cg.refdef.y + cg.refdef.height * 0.5f * ( 1.0f - up / fwd / tan( cg.refdef.fov_y * M_PI / 360.0f ) );
		onScreen = px > cg.refdef.x + 24 && px < cg.refdef.x + cg.refdef.width - 24
			&& py > cg.refdef.y + 24 && py < cg.refdef.y + cg.refdef.height - 24;
	}
	if ( onScreen ) {
		// a diamond over its head: through walls, or far off; in plain sight and near, its nameplate does
		al = seen ? CG_Clamp01( ( dist - 900.0f ) / 600.0f ) * 0.8f : 0.9f;
		if ( al <= 0.02f ) {
			return;
		}
		vx = ( px - cgs.screenXBias ) / cgs.screenXScale;
		vy = py / cgs.screenYScale;
		k = 7.0f + 1.5f * pulse;
		// a solid diamond (one-unit rows) on a dark rim
		for ( i = -(int)k - 2 ; i <= (int)k + 2 ; i++ ) {
			w = 2.0f * ( k + 2.0f - fabs( (float)i ) );
			col[0] = 0; col[1] = 0; col[2] = 0; col[3] = 0.5f * al;
			CG_FillRect( vx - w * 0.5f, vy + i - 0.5f, w, 1.0f, col );
			if ( fabs( (float)i ) <= k ) {
				w = 2.0f * ( k - fabs( (float)i ) );
				col[0] = rgb[0]; col[1] = rgb[1]; col[2] = rgb[2]; col[3] = al;
				CG_FillRect( vx - w * 0.5f, vy + i - 0.5f, w, 1.0f, col );
			}
		}
		col[0] = col[1] = col[2] = 1.0f; col[3] = 0.9f * al;
		CGA_Text( vx, vy + k + 4, va( "%im", meters ), col, 6, 9, 0, qtrue );
		return;
	}
	// off screen: a chevron on a ring around the crosshair, pointing the way (behind = down)
	ang = atan2( -side, fwd > 0 ? up : -fabs( up ) - 1.0f );		// 0 = up the screen, + = clockwise
	if ( fwd <= 8.0f ) {
		ang = side > 0 ? -M_PI * 0.5f - 0.6f * CG_Clamp01( -fwd / ( dist + 1 ) ) : M_PI * 0.5f + 0.6f * CG_Clamp01( -fwd / ( dist + 1 ) );
	}
	c = cos( ang );
	sn = sin( ang );
	r = 120.0f;
	tx = 320 + sn * r;
	ty = 240 - c * r;
	al = 0.65f + 0.35f * pulse;
	// the chevron: two arms of dots meeting at the tip
	for ( i = 0 ; i < 7 ; i++ ) {
		k = i * 3.0f;
		left = ang + M_PI * 0.78f;
		right = ang - M_PI * 0.78f;
		col[0] = 0; col[1] = 0; col[2] = 0; col[3] = 0.4f * al;
		CGA_LocatorDot( tx + sin( left ) * k, ty - cos( left ) * k, 6.0f, col );
		CGA_LocatorDot( tx + sin( right ) * k, ty - cos( right ) * k, 6.0f, col );
		col[0] = rgb[0]; col[1] = rgb[1]; col[2] = rgb[2]; col[3] = al;
		CGA_LocatorDot( tx + sin( left ) * k, ty - cos( left ) * k, 4.0f, col );
		CGA_LocatorDot( tx + sin( right ) * k, ty - cos( right ) * k, 4.0f, col );
	}
	col[0] = col[1] = col[2] = 1.0f; col[3] = 0.9f;
	CGA_Text( tx - sn * 26, ty + c * 26 - 4, va( "%im", meters ), col, 6, 9, 0, qtrue );
}

/*
=================
CGA_ClockFrame

The clock's sounds (a tick a second in the last 30 s, a harder one in the last 10, the
last-minute sting, TIME UP) and the fastest-kill record (once, at the kill)
=================
*/
static void CGA_ClockFrame( void ) {
	int		left, sec;

	if ( !cga.boss || !cga.timed ) {
		return;
	}
	if ( cga.tLastMin && !cga.tLastMinSeen && !cga.dead ) {
		cga.tLastMinSeen = 1;
		trap_S_StartLocalSound( cgaLastMin, CHAN_ANNOUNCER );
	}
	if ( cga.tUp && !cga.tUpSeen ) {
		cga.tUpSeen = 1;
		trap_S_StartLocalSound( cgaTimeUp, CHAN_ANNOUNCER );
	}
	if ( cga.dead && cga.tKill > 0 && !cga.recBoss && !cga.practice ) {
		cga.recBoss = 1;
		cga.recPrev = CG_RecordsBossKill( cga.type, atoi( Info_ValueForKey( CG_ConfigString( CS_DESCENT ), "rd" ) ), cga.tKill, &cga.recNew );
	}
	if ( cga.tEnd <= 0 || cga.tKill || cga.tUp || cga.dead ) {
		return;
	}
	left = cga.tEnd - cg.time;
	sec = ( left + 999 ) / 1000;
	if ( sec >= 1 && sec <= 30 && sec != cga.tTickSec ) {
		cga.tTickSec = sec;
		trap_S_StartLocalSound( sec <= 10 ? cgaTock : cgaTick, CHAN_LOCAL_SOUND );
	}
}

static void CGA_DrawBossBar( void ) {
	vec4_t		col, bg, fill, ghost;
	vec3_t		rgb;
	float		t, al, drop, w = 260, h = 6, x, y, frac, f;
	int			i, left, lives;
	const char	*name, *label;

	t = CG_DSI_BossBarTime( cg.time - cga.firstIn - BOSSBAR_DELAY );		// ds-arrivals: not before a staggered boss is in
	if ( t < 0 ) {
		return;
	}
	al = CG_Clamp01( t / 400.0f );
	drop = -20.0f * ( 1.0f - CG_Clamp01( t / 350.0f ) );
	// the bar: this boss life's pool (round 2b: the boss has lives)
	frac = cga.dead ? 0 : CG_Clamp01( ( cga.hp > 0 ? cga.hp : 0 ) / (float)cga.max );
	if ( cga.timed && !cga.dead ) {
		// round 4: one pool, the phases are thirds of it (marks on the bar)
		frac = CG_Clamp01( ( cga.nph - cga.ph + frac ) / (float)cga.nph );
	}
	// the ghost trails behind (and jumps back up with a new life)
	if ( cga.shownHp < frac || cga.shownHp > 1 ) {
		cga.shownHp = frac;
	} else {
		cga.shownHp -= ( cga.shownHp - frac ) * CG_Clamp01( cg.frametime * 0.004f );
	}
	x = 320 - w * 0.5f;
	y = 22 + drop;
	CGA_BossColor( cga.type, rgb );

	// the name line (and the phase)
	name = cga.abyss > 0 ? va( "%s  -  ABYSS %i", ds_bosses[cga.type].name, cga.abyss ) : ds_bosses[cga.type].name;

	col[0] = 0.95f; col[1] = 0.9f; col[2] = 0.82f; col[3] = al;
	CGA_Text( x, y - 13, name, col, 7, 10, -1, qtrue );
	col[0] = col[1] = col[2] = 0.65f; col[3] = 0.85f * al;
	CGA_Text( x + w, y - 10, cga.nph > 1 ? va( "%s  -  PHASE %i", cga.practice ? "PRACTICE" : ds_bosses[cga.type].title, cga.ph ) : ds_bosses[cga.type].title,
		col, 4, 7, 1, qtrue );

	bg[0] = bg[1] = bg[2] = 0.02f; bg[3] = 0.65f * al;
	CG_FillRect( x - 1, y - 1, w + 2, h + 2, bg );
	ghost[0] = 1; ghost[1] = 0.9f; ghost[2] = 0.75f; ghost[3] = 0.45f * al;
	CG_FillRect( x, y, w * cga.shownHp, h, ghost );
	if ( cga.shield ) {
		fill[0] = 0.3f; fill[1] = 0.75f; fill[2] = 1.0f;
	} else {
		fill[0] = rgb[0]; fill[1] = rgb[1] * 0.8f; fill[2] = rgb[2] * 0.8f;
	}
	fill[3] = 0.95f * al;
	if ( cga.roar > cg.time ) {
		f = 0.5f + 0.5f * sin( cg.time * 0.03f );
		fill[0] += ( 1 - fill[0] ) * f;
		fill[1] += ( 1 - fill[1] ) * f;
		fill[2] += ( 1 - fill[2] ) * f;
	}
	CG_FillRect( x, y, w * frac, h, fill );
	if ( cga.timed ) {
		// the phase marks, and the clock right of the bar
		bg[0] = bg[1] = bg[2] = 0.0f; bg[3] = 0.8f * al;
		for ( i = 1 ; i < cga.nph ; i++ ) {
			CG_FillRect( x + w * i / (float)cga.nph - 0.5f, y - 2, 1, h + 4, bg );
		}
		CGA_DrawClock( x + w + 8, y + h * 0.5f, al );
		CG_DS_RematchDraw( x, y, h, al );		// en-rematch: heat + modifiers + best, left of the bar
	}

	// the boss's lives: small skulls in its colour right of the bar (dark = taken; the one just
	// taken flashes), the count after them
	lives = cga.blm > 0 ? cga.blm : 0;
	left = cga.dead ? 0 : cga.bl;
	if ( lives > 0 ) {
		float	sx = x + w + 7, sz = 7, gap = 1.5f, flash;
		int		shown = lives > 15 ? 15 : lives;

		for ( i = 0 ; i < shown ; i++ ) {
			flash = 0;
			if ( i < left ) {
				col[0] = 0.55f + 0.45f * rgb[0]; col[1] = 0.45f + 0.45f * rgb[1]; col[2] = 0.4f + 0.45f * rgb[2]; col[3] = 0.95f * al;
			} else {
				col[0] = 0.25f; col[1] = 0.06f; col[2] = 0.05f; col[3] = 0.7f * al;
				if ( i == left && cg.time - cga.blLostTime < 1600 ) {
					flash = 1.0f - ( cg.time - cga.blLostTime ) / 1600.0f;
					col[0] += ( 1.0f - col[0] ) * flash;
					col[1] += ( 0.85f - col[1] ) * flash;
					col[2] += ( 0.5f - col[2] ) * flash;
					col[3] = al;
				}
			}
			trap_R_SetColor( col );
			if ( cgaLifeSkull ) {
				CG_DrawPic( sx + i * ( sz + gap ) - flash * 2, y - 1 - flash * 2, sz + flash * 4, sz + flash * 4, cgaLifeSkull );
			} else {
				CG_FillRect( sx + i * ( sz + gap ), y, sz - 2, h, col );
			}
			trap_R_SetColor( NULL );
		}
		col[0] = 0.95f; col[1] = 0.9f; col[2] = 0.8f; col[3] = 0.95f * al;
		CGA_Text( sx + shown * ( sz + gap ) + 3, y - 1, va( "%i", left ), col, 6, 8, -1, qtrue );
	}

	// its sides (the trade-off): FAST / SLOW, ARMOURED / FRAIL, BRUTAL / WEAK, under the bar's right end
	if ( cga.traits && !( cga.timed && ( cga.dead || cga.tUp ) ) ) {		// (timed: the end line needs the room)
		const char	*tr = va( "%s  %s  %s", ( cga.traits & DST_FAST ) ? "FAST" : "SLOW",
			( cga.traits & DST_TANKY ) ? "ARMOURED" : "FRAIL", ( cga.traits & DST_STRONG ) ? "BRUTAL" : "WEAK" );

		col[0] = 0.7f + 0.3f * rgb[0]; col[1] = 0.6f + 0.2f * rgb[1]; col[2] = 0.55f + 0.2f * rgb[2]; col[3] = 0.7f * al;
		CGA_Text( x + w, y + h + 4, tr, col, 4, 6, 1, qtrue );
	}

	// under the bar: what is coming, the shield, the frenzy, the end
	label = NULL;
	if ( cga.dead && cga.timed && cga.tKill > 0 ) {
		// round 4: the kill time, and the fastest-kill record
		label = cga.recNew ? ( cga.recPrev > 0 ? va( "SLAIN IN %s  -  NEW RECORD (WAS %s)", CGA_Clock( cga.tKill ), CGA_Clock( cga.recPrev ) )
			: va( "SLAIN IN %s  -  FIRST KILL RECORDED", CGA_Clock( cga.tKill ) ) )
			: cga.recPrev > 0 ? va( "SLAIN IN %s  -  RECORD %s", CGA_Clock( cga.tKill ), CGA_Clock( cga.recPrev ) ) : va( "SLAIN IN %s", CGA_Clock( cga.tKill ) );
		col[0] = 1.0f; col[1] = 0.82f; col[2] = 0.3f;
	} else if ( cga.dead ) {
		label = "DEFEATED";
		col[0] = 1.0f; col[1] = 0.82f; col[2] = 0.3f;
	} else if ( cga.timed && cga.tUp ) {
		label = CG_DS_RematchFallen() ? "FALLEN  -  ONE LIFE" : "TIME UP";		// (en-rematch)
		f = 0.6f + 0.4f * sin( cg.time * 0.012f );
		col[0] = 1.0f; col[1] = 0.25f * f; col[2] = 0.15f * f;
	} else if ( cga.lastAtk > 0 && cga.lastAtk < DSB_NUM && cg.time - cga.lastAtkTime < 1600 && cga.lastAtk != DSB_ROAR ) {
		label = ds_bossAttackNames[cga.lastAtk];
		f = 0.6f + 0.4f * sin( cg.time * 0.025f );
		col[0] = 1.0f; col[1] = 0.3f * f; col[2] = 0.2f * f;
	} else if ( cga.shield ) {
		label = "SHIELDED  -  DESTROY THE DRONES";
		col[0] = 0.4f; col[1] = 0.8f; col[2] = 1.0f;
	} else if ( cga.timed && cga.tLastMin ) {
		label = cga.frenzy && cga.type == BOSS_BUTCHER ? "BLOOD FRENZY" : "ENRAGED";		// (the clock says it is the last minute)
		col[0] = 1.0f; col[1] = 0.25f; col[2] = 0.15f;
	} else if ( cga.frenzy ) {
		label = cga.type == BOSS_BUTCHER ? "BLOOD FRENZY" : "ENRAGED";
		col[0] = 1.0f; col[1] = 0.25f; col[2] = 0.15f;
	}
	if ( label ) {
		col[3] = 0.9f * al;
		CGA_Text( 320, y + h + 4, label, col, 5, 8, 0, qtrue );
	}

	// the roster, one slim line: YOU 7   SARGE 5   KLESK OUT   BUTCHER 6 (lives left each)
	if ( cga.nro > 0 ) {
		char		line[256];
		const char	*nm;
		char		clean[32];
		float		cw = 4, lx, tw;

		line[0] = 0;
		tw = 0;
		for ( i = 0 ; i < cga.nro ; i++ ) {
			if ( cga.roClient[i] == cg.snap->ps.clientNum ) {
				nm = "YOU";
			} else if ( cga.roClient[i] >= 0 && cga.roClient[i] < MAX_CLIENTS && cgs.clientinfo[cga.roClient[i]].infoValid ) {
				CG_CleanUpper( cgs.clientinfo[cga.roClient[i]].name, clean, sizeof( clean ) );
				nm = clean;
			} else {
				continue;
			}
			tw += ( CG_DrawStrlen( nm ) + 4 + ( cga.roLives[i] > 0 ? ( cga.roLives[i] >= 10 ? 2 : 1 ) : 3 ) ) * cw;
		}
		lx = 320 - tw * 0.5f;
		for ( i = 0 ; i < cga.nro ; i++ ) {
			if ( cga.roClient[i] == cg.snap->ps.clientNum ) {
				nm = "YOU";
				col[0] = 0.95f; col[1] = 0.9f; col[2] = 0.78f;
			} else if ( cga.roClient[i] >= 0 && cga.roClient[i] < MAX_CLIENTS && cgs.clientinfo[cga.roClient[i]].infoValid ) {
				CG_CleanUpper( cgs.clientinfo[cga.roClient[i]].name, clean, sizeof( clean ) );
				nm = clean;
				if ( cga.roClient[i] == cga.client ) {
					col[0] = 0.55f + 0.45f * rgb[0]; col[1] = 0.45f + 0.45f * rgb[1]; col[2] = 0.4f + 0.45f * rgb[2];
				} else if ( cga.roClient[i] == cga.nem ) {
					col[0] = 0.85f; col[1] = 0.25f; col[2] = 0.2f;
				} else {
					col[0] = col[1] = col[2] = 0.72f;
				}
			} else {
				continue;
			}
			if ( cga.roLives[i] <= 0 ) {
				col[0] *= 0.5f; col[1] *= 0.5f; col[2] *= 0.5f;
			}
			col[3] = 0.85f * al;
			Com_sprintf( line, sizeof( line ), cga.roLives[i] > 0 ? "%s %i" : "%s OUT", nm, cga.roLives[i] );
			CGA_Text( lx, y + h + 14, line, col, cw, 6, -1, qtrue );
			lx += ( CG_DrawStrlen( line ) + 3 ) * cw;
		}
	}
}

/*
=================
CG_DS_ActsServerCommand

dsnem 1 <client> <kills>: a bot became your nemesis; dsnem 2 <client> <skulls>: you slew it;
dsnem 3 <client>: eliminated (boss floors); dsnem 4 <boss> <skulls>: your final blow on the boss
=================
*/
qboolean CG_DS_ActsServerCommand( const char *cmd ) {
	if ( Q_stricmp( cmd, "dsnem" ) ) {
		return qfalse;
	}
	cga.calloutKind = atoi( CG_Argv( 1 ) );
	cga.calloutClient = atoi( CG_Argv( 2 ) );
	cga.calloutVal = atoi( CG_Argv( 3 ) );
	cga.calloutTime = cg.time;
	if ( cga.calloutKind == 2 ) {
		trap_S_StartLocalSound( cgaRoar, CHAN_LOCAL_SOUND );
		CG_KillCamNotable( cga.calloutClient, KCN_NEMESIS );		// ds-killcam: the revenge kill
	} else if ( cga.calloutKind == 4 ) {
		trap_S_StartLocalSound( cgaRoar, CHAN_LOCAL_SOUND );
	} else if ( cga.calloutKind == 3 ) {
		// an elimination: no sound of its own (the kill already made one)
	} else {
		trap_S_StartLocalSound( cgaWarn, CHAN_LOCAL_SOUND );
	}
	return qtrue;
}

// a short callout at the right edge (2.6 s): NEMESIS SLAIN / NEMESIS: <name>
static void CGA_DrawCallout( void ) {
	vec4_t		col, bg;
	float		t = cg.time - cga.calloutTime, al, slide, x = 620, y;
	const char	*name, *l1, *l2;

	if ( !cga.calloutTime || t < 0 || t > 2600 ) {
		return;
	}
	al = CG_Clamp01( t / 150.0f ) * CG_Clamp01( ( 2600 - t ) / 400.0f );
	slide = 18.0f * ( 1.0f - CG_Clamp01( t / 180.0f ) );
	name = cga.calloutClient >= 0 && cga.calloutClient < MAX_CLIENTS ? cgs.clientinfo[cga.calloutClient].name : "";
	if ( cga.calloutKind == 3 ) {
		// an elimination (boss floors): one small line
		char	clean[32];

		CG_CleanUpper( name, clean, sizeof( clean ) );
		y = CG_EdgeSlot( EDGE_NEMESIS, 12 ) + 2;		// en-screens: the shared right-edge stack
		CG_HudAnchor( 1 );
		bg[0] = 0.05f; bg[1] = 0.0f; bg[2] = 0.0f; bg[3] = 0.45f * al;
		CG_FillRect( x - 110 + slide, y - 2, 112, 12, bg );
		col[0] = 0.9f; col[1] = 0.5f; col[2] = 0.4f; col[3] = 0.9f * al;
		CGA_Text( x - 3 + slide, y, va( "%s ELIMINATED", clean ), col, 5, 8, 1, qtrue );
		CG_HudAnchor( 0 );
		return;
	}
	if ( cga.calloutKind == 2 ) {
		l1 = "NEMESIS SLAIN";
		l2 = va( "%s  +%i SKULLS", name, cga.calloutVal );
	} else if ( cga.calloutKind == 4 ) {
		l1 = "FINAL BLOW";
		l2 = va( "%s IS DOWN  +%i SKULLS", ds_bosses[cga.type].name, cga.calloutVal );
	} else {
		l1 = va( "NEMESIS: %s", name );
		l2 = va( "KILLED YOU %i TIMES", cga.calloutVal );
	}
	y = CG_EdgeSlot( EDGE_NEMESIS, 27 ) + 3;		// en-screens: the shared right-edge stack
	CG_HudAnchor( 1 );		// the right side of the HUD area (cg_hudWide)
	bg[0] = 0.08f; bg[1] = 0.0f; bg[2] = 0.0f; bg[3] = 0.55f * al;
	CG_FillRect( x - 150 + slide, y - 3, 152, 27, bg );
	bg[0] = 0.75f; bg[1] = 0.05f; bg[2] = 0.03f; bg[3] = 0.9f * al;
	CG_FillRect( x + 1 + slide, y - 3, 2, 27, bg );
	col[0] = 1.0f; col[1] = cga.calloutKind >= 2 ? 0.82f : 0.25f; col[2] = cga.calloutKind >= 2 ? 0.35f : 0.18f; col[3] = al;
	CGA_Text( x - 3 + slide, y, l1, col, 8, 11, 1, qtrue );
	col[0] = 0.9f; col[1] = 0.85f; col[2] = 0.78f; col[3] = 0.85f * al;
	CGA_Text( x - 3 + slide, y + 13, l2, col, 5, 7, 1, qtrue );
	CG_HudAnchor( 0 );
}

/*
=================
CG_DS_ActsDraw2D

From CG_Draw2D next to the descent HUD (not at the intermission)
=================
*/
void CG_DS_ActsDraw2D( void ) {
	int		act;

	if ( !cg.snap || !cga.on ) {
		return;
	}
	if ( !cg.showScores ) {
		CGA_DrawCallout();
	}
	CGA_ClockFrame();		// round 4: the boss clock's sounds, the fastest-kill record
	if ( cga.floor != cga.lastFloorSeen ) {
		cga.lastFloorSeen = cga.floor;
		cga.firstIn = 0;
		cga.shownHp = 1;
	}
	if ( !cga.firstIn && cga.phase == 0 && cg.snap->ps.persistant[PERS_TEAM] != TEAM_SPECTATOR && !CG_DSI_Busy() ) {
		// ds-intro: after the versus intro; it replaced the floor strip, so the boss bar comes at once
		cga.firstIn = cg.time - ( cga.boss && CG_DSI_Covers() ? BOSSBAR_DELAY : 0 );
		if ( cga.firstIn <= 0 ) {
			cga.firstIn = 1;
		}
		act = CGA_Act();
		// a new act: its card and sting (floors 1, 7, 13, 19 and the first of the Abyss)
		if ( cga.nodeType == 4 ) {
			trap_S_StartLocalSound( cgaVaultSnd, CHAN_LOCAL_SOUND );
		} else if ( act >= 1 && ( cga.floor - 1 ) % DS_FLOORS_PER_ACT == 0 && cga.floor <= DS_WIN_FLOOR + 1 ) {
			trap_S_StartLocalSound( cgaActSnd, CHAN_LOCAL_SOUND );
		}
	}
	if ( !cga.firstIn || cg.showScores ) {
		return;
	}
	if ( cga.vaultEnd || cga.nodeType == 4 ) {
		CGA_DrawVault();
		return;
	}
	if ( CG_DSI_Busy() ) {
		return;		// ds-intro: the versus intro has the top edge (also a "dsintro" replay)
	}
	act = CGA_Act();
	if ( act >= 1 && ( cga.floor - 1 ) % DS_FLOORS_PER_ACT == 0 && cga.floor <= DS_WIN_FLOOR + 1 ) {
		CGA_DrawActIntro();
	}
	if ( cga.boss && cga.phase == 0 ) {
		CGA_DrawBossBar();
		CGA_DrawBossLocator();
	}
}
