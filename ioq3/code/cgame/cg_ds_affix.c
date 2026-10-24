/*
===========================================================================
REARMED -- round 4 AFFIXES (en-affix), cgame side. Contract: game/bg_ds_affix.h.

The tells live ON the enemy (no overhead text, nothing in the centre of the screen):
  VAMPIRIC		a dark red mist curls around it; a red streak when it drinks from a hit
  EXPLOSIVE		a pulsing red-orange core light and spitting sparks; its corpse swells, then blows
  SPLITTING		three violet shards orbit it; a violet tear where it splits
  REFLECTING	white glints twinkle on it; a chrome flash on the body when it bounces damage
  FROST			ice crystals drift down from it, a cold blue light; a crystal burst on whoever it hits
  BLAZING		amber embers rise from its feet (and the fire patches it leaves)
  MAGNETIC		teal motes spiral in to it; the wind-up is a tether to you and a ring under it
  REGENERATING	pale mint motes rise from it while it heals out of combat
  champion		a faint gold sheen and light (three of them, sharing the affixes above)
Names only at the edges: the arrival callout (cg_ds_intro.c asks CG_DSA_Names), the TAB
scoreboard (CG_DSA_ScoreTag), the champion pack line at the right edge. The local player
slowed by FROST gets a frosty screen-edge tint.
===========================================================================
*/
#include "cg_local.h"

#define DSA_MAXP		640

typedef struct {
	vec3_t		org, vel;
	float		grav, size0, size1, drag;
	int			start, end;
	float		rgba[4];
	qhandle_t	shader;
	float		rot, spin;
	qboolean	additive;
} dsaPart_t;

typedef struct {
	char		cs[MAX_INFO_STRING];
	int			frostUntil, frostClient, regenMask;
	int			chState, chMask, chWarn, chLand, chAlive, chKills, chClient[3];
	int			chStateTime, chLastState;
	vec3_t		chSpot;
	qboolean	chSpotValid;
	// media
	qhandle_t	mist, crystal, shard, glint, chrome, gold, edgeH, edgeV, beam, ring;
	sfxHandle_t	sFuse, sBlast, sSplit, sReflect, sFrost, sDrain, sPullWind, sPullYank, sPullSnap, sRegen, sChampWarn, sChampLand;
	// per client
	int			nextEmit[MAX_CLIENTS];
	int			reflectUntil[MAX_CLIENTS];
	int			regenWas;
	int			partFrame;
	int			lastWarnSound;
} dsaState_t;

static dsaState_t	dsa_;
static vmCvar_t		dsaCvTells;
static dsaPart_t	dsaP[DSA_MAXP];
static int			dsaNumP;
static int			dsaSeen[MAX_GENTITIES];		// s.time of the fx entity seen (its start sound / burst)
static int			dsaLanded[MAX_GENTITIES];		// s.time of the fx entity whose landing ran

static const float dsaRGB[DSAF_NUM][3] = {
	{ 1.00f, 0.10f, 0.14f },	// VAMPIRIC: blood red mist
	{ 1.00f, 0.38f, 0.05f },	// EXPLOSIVE: hot red-orange core
	{ 0.72f, 0.38f, 1.00f },	// SPLITTING: violet shards
	{ 0.86f, 0.92f, 1.00f },	// REFLECTING: chrome white
	{ 0.42f, 0.78f, 1.00f },	// FROST: icy blue crystals
	{ 1.00f, 0.74f, 0.22f },	// BLAZING: amber embers
	{ 0.22f, 0.95f, 0.85f },	// MAGNETIC: teal motes
	{ 0.80f, 1.00f, 0.86f }		// REGENERATING: pale mint (a medic's beam is deep green)
};
static const float dsaGold[3] = { 1.0f, 0.80f, 0.30f };

/*
===============================================================================

NAMES (edges)

===============================================================================
*/

// "FROST VAMPIRIC" (strongest-reading order: bit order)
const char *CG_DSA_Names( int mask ) {
	static char	buf[2][96];
	static int	idx;
	char		*b;
	int			i;

	idx ^= 1;
	b = buf[idx];
	b[0] = 0;
	for ( i = 0 ; i < DSAF_NUM ; i++ ) {
		if ( mask & ( 1 << i ) ) {
			Q_strcat( b, 96, b[0] ? " " : "" );
			Q_strcat( b, 96, BG_DS_AffixName( i ) );
		}
	}
	return b;
}

// the colour of the first affix (or gold for a champion without one)
void CG_DSA_Color( int mask, vec4_t out ) {
	int		i;

	out[0] = dsaGold[0]; out[1] = dsaGold[1]; out[2] = dsaGold[2]; out[3] = 1;
	for ( i = 0 ; i < DSAF_NUM ; i++ ) {
		if ( mask & ( 1 << i ) ) {
			out[0] = dsaRGB[i][0]; out[1] = dsaRGB[i][1]; out[2] = dsaRGB[i][2];
			return;
		}
	}
}

/*
===============================================================================

REGISTER / CONFIGSTRING

===============================================================================
*/

void CG_DSA_Register( void ) {
	memset( &dsa_, 0, sizeof( dsa_ ) );
	trap_Cvar_Register( &dsaCvTells, "cg_dsAffixTells", "1", CVAR_ARCHIVE );
	memset( dsaSeen, 0, sizeof( dsaSeen ) );
	memset( dsaLanded, 0, sizeof( dsaLanded ) );
	dsaNumP = 0;
	dsa_.mist = trap_R_RegisterShader( "rearmed/dsaMist" );
	dsa_.crystal = trap_R_RegisterShader( "rearmed/dsaCrystal" );
	dsa_.shard = trap_R_RegisterShader( "rearmed/dsaShard" );
	dsa_.glint = trap_R_RegisterShader( "rearmed/dsaGlint" );
	dsa_.chrome = trap_R_RegisterShader( "rearmed/dsaChrome" );
	dsa_.gold = trap_R_RegisterShader( "rearmed/dsaGold" );
	dsa_.edgeH = trap_R_RegisterShaderNoMip( "rearmed/dsaFrostEdgeH" );
	dsa_.edgeV = trap_R_RegisterShaderNoMip( "rearmed/dsaFrostEdgeV" );
	dsa_.beam = trap_R_RegisterShader( "rearmed/dsaBeam" );
	dsa_.ring = trap_R_RegisterShader( "rearmed/dsaRing" );
	dsa_.sFuse = trap_S_RegisterSound( "sound/rearmed/affix/bomb_fuse.wav", qfalse );
	dsa_.sBlast = trap_S_RegisterSound( "sound/rearmed/affix/bomb_blast.wav", qfalse );
	dsa_.sSplit = trap_S_RegisterSound( "sound/rearmed/affix/split.wav", qfalse );
	dsa_.sReflect = trap_S_RegisterSound( "sound/rearmed/affix/reflect.wav", qfalse );
	dsa_.sFrost = trap_S_RegisterSound( "sound/rearmed/affix/frost.wav", qfalse );
	dsa_.sDrain = trap_S_RegisterSound( "sound/rearmed/affix/drain.wav", qfalse );
	dsa_.sPullWind = trap_S_RegisterSound( "sound/rearmed/affix/pull_wind.wav", qfalse );
	dsa_.sPullYank = trap_S_RegisterSound( "sound/rearmed/affix/pull_yank.wav", qfalse );
	dsa_.sPullSnap = trap_S_RegisterSound( "sound/rearmed/affix/pull_snap.wav", qfalse );
	dsa_.sRegen = trap_S_RegisterSound( "sound/rearmed/affix/regen.wav", qfalse );
	dsa_.sChampWarn = trap_S_RegisterSound( "sound/rearmed/affix/champ_warn.wav", qfalse );
	dsa_.sChampLand = trap_S_RegisterSound( "sound/rearmed/affix/champ_land.wav", qfalse );
	dsa_.frostClient = -1;
}

static void DSA_ParseCS( void ) {
	const char	*s = CG_ConfigString( CS_DSAFFIX );
	const char	*v;
	int			i, st;

	if ( !strcmp( s, dsa_.cs ) ) {
		return;
	}
	Q_strncpyz( dsa_.cs, s, sizeof( dsa_.cs ) );
	dsa_.frostUntil = atoi( Info_ValueForKey( s, "fz" ) );
	v = Info_ValueForKey( s, "fc" );
	dsa_.frostClient = v[0] ? atoi( v ) : -1;
	dsa_.regenMask = atoi( Info_ValueForKey( s, "rg" ) );
	st = atoi( Info_ValueForKey( s, "cs" ) );
	if ( st != dsa_.chState ) {
		dsa_.chStateTime = cg.time;
		dsa_.chLastState = dsa_.chState;
	}
	dsa_.chState = st;
	dsa_.chMask = atoi( Info_ValueForKey( s, "cm" ) );
	dsa_.chWarn = atoi( Info_ValueForKey( s, "cw" ) );
	dsa_.chLand = atoi( Info_ValueForKey( s, "cl" ) );
	dsa_.chAlive = atoi( Info_ValueForKey( s, "cc" ) );
	dsa_.chKills = atoi( Info_ValueForKey( s, "ck" ) );
	v = Info_ValueForKey( s, "cx" );
	for ( i = 0 ; i < 3 ; i++ ) {
		dsa_.chClient[i] = v[0] ? atoi( v ) : -1;
		while ( *v && *v != '.' ) {
			v++;
		}
		if ( *v == '.' ) {
			v++;
		}
	}
}

/*
===============================================================================

PARTICLES (own pool: sprites, drawn once a frame)

===============================================================================
*/

static dsaPart_t *DSA_Part( const vec3_t org, const vec3_t vel, float size0, float size1, int life, const float *rgb, float alpha,
		qhandle_t shader, qboolean additive ) {
	dsaPart_t	*p;

	if ( dsaNumP >= DSA_MAXP ) {
		return NULL;
	}
	p = &dsaP[dsaNumP++];
	memset( p, 0, sizeof( *p ) );
	VectorCopy( org, p->org );
	VectorCopy( vel, p->vel );
	p->size0 = size0;
	p->size1 = size1;
	p->start = cg.time;
	p->end = cg.time + life;
	p->rgba[0] = rgb[0]; p->rgba[1] = rgb[1]; p->rgba[2] = rgb[2]; p->rgba[3] = alpha;
	p->shader = shader;
	p->additive = additive;
	p->rot = random() * 360;
	return p;
}

static void DSA_RunParticles( void ) {
	refEntity_t	re;
	dsaPart_t	*p;
	float		dt, f, fade, a;
	int			i;

	if ( dsa_.partFrame == cg.clientFrame ) {
		return;
	}
	dsa_.partFrame = cg.clientFrame;
	dt = cg.frametime * 0.001f;
	if ( dt > 0.1f ) {
		dt = 0.1f;
	}
	for ( i = 0 ; i < dsaNumP ; ) {
		p = &dsaP[i];
		if ( cg.time >= p->end || cg.time < p->start - 1000 ) {
			dsaP[i] = dsaP[--dsaNumP];
			continue;
		}
		p->vel[2] -= p->grav * dt;
		if ( p->drag > 0 ) {
			VectorScale( p->vel, 1.0f - p->drag * dt > 0 ? 1.0f - p->drag * dt : 0, p->vel );
		}
		VectorMA( p->org, dt, p->vel, p->org );
		p->rot += p->spin * dt;
		f = (float)( cg.time - p->start ) / (float)( p->end - p->start );
		fade = f < 0.15f ? f / 0.15f : 1.0f - ( f - 0.15f ) / 0.85f;
		memset( &re, 0, sizeof( re ) );
		re.reType = RT_SPRITE;
		VectorCopy( p->org, re.origin );
		VectorCopy( p->org, re.oldorigin );
		re.radius = p->size0 + ( p->size1 - p->size0 ) * f;
		re.rotation = p->rot;
		re.customShader = p->shader;
		a = p->rgba[3] * fade;
		if ( p->additive ) {
			re.shaderRGBA[0] = (byte)( 255 * p->rgba[0] * a );
			re.shaderRGBA[1] = (byte)( 255 * p->rgba[1] * a );
			re.shaderRGBA[2] = (byte)( 255 * p->rgba[2] * a );
			re.shaderRGBA[3] = 255;
		} else {
			re.shaderRGBA[0] = (byte)( 255 * p->rgba[0] );
			re.shaderRGBA[1] = (byte)( 255 * p->rgba[1] );
			re.shaderRGBA[2] = (byte)( 255 * p->rgba[2] );
			re.shaderRGBA[3] = (byte)( 255 * a );
		}
		trap_R_AddRefEntityToScene( &re );
		i++;
	}
}

static void DSA_RandInBox( const vec3_t base, float r, float z0, float z1, vec3_t out ) {
	float	a = random() * 2 * M_PI, d = r * sqrt( random() );

	out[0] = base[0] + cos( a ) * d;
	out[1] = base[1] + sin( a ) * d;
	out[2] = base[2] + z0 + ( z1 - z0 ) * random();
}

static void DSA_Sprite( const vec3_t org, float radius, const float *rgb, float a, qhandle_t shader, float rot ) {
	refEntity_t	re;

	memset( &re, 0, sizeof( re ) );
	re.reType = RT_SPRITE;
	VectorCopy( org, re.origin );
	VectorCopy( org, re.oldorigin );
	re.radius = radius;
	re.rotation = rot;
	re.customShader = shader;
	re.shaderRGBA[0] = (byte)( 255 * CG_Clamp01( rgb[0] * a ) );
	re.shaderRGBA[1] = (byte)( 255 * CG_Clamp01( rgb[1] * a ) );
	re.shaderRGBA[2] = (byte)( 255 * CG_Clamp01( rgb[2] * a ) );
	re.shaderRGBA[3] = 255;
	trap_R_AddRefEntityToScene( &re );
}

static void DSA_Beam( const vec3_t a, const vec3_t b, const float *rgb, float alpha ) {
	refEntity_t	re;

	memset( &re, 0, sizeof( re ) );
	re.reType = RT_RAIL_CORE;
	re.customShader = dsa_.beam;
	VectorCopy( a, re.origin );
	VectorCopy( b, re.oldorigin );
	re.shaderRGBA[0] = (byte)( 255 * CG_Clamp01( rgb[0] * alpha ) );
	re.shaderRGBA[1] = (byte)( 255 * CG_Clamp01( rgb[1] * alpha ) );
	re.shaderRGBA[2] = (byte)( 255 * CG_Clamp01( rgb[2] * alpha ) );
	re.shaderRGBA[3] = 255;
	AxisClear( re.axis );
	trap_R_AddRefEntityToScene( &re );
}

// a flat ring on the floor
static void DSA_Ring( const vec3_t org, float radius, const float *rgb, float alpha ) {
	polyVert_t	v[4];
	int			i;

	if ( alpha <= 0.01f ) {
		return;
	}
	for ( i = 0 ; i < 4 ; i++ ) {
		v[i].xyz[0] = org[0] + ( i == 0 || i == 3 ? -radius : radius );
		v[i].xyz[1] = org[1] + ( i < 2 ? -radius : radius );
		v[i].xyz[2] = org[2] + 2;
		v[i].st[0] = ( i == 0 || i == 3 ) ? 0 : 1;
		v[i].st[1] = i < 2 ? 0 : 1;
		v[i].modulate[0] = (byte)( 255 * CG_Clamp01( rgb[0] * alpha ) );
		v[i].modulate[1] = (byte)( 255 * CG_Clamp01( rgb[1] * alpha ) );
		v[i].modulate[2] = (byte)( 255 * CG_Clamp01( rgb[2] * alpha ) );
		v[i].modulate[3] = 255;
	}
	trap_R_AddPolyToScene( dsa_.ring, 4, v );
}

static void DSA_Burst( const vec3_t org, const float *rgb, int n, float speed, int life, float size, qhandle_t shader, float grav ) {
	vec3_t		vel;
	dsaPart_t	*p;
	int			i;

	for ( i = 0 ; i < n ; i++ ) {
		VectorSet( vel, crandom(), crandom(), crandom() * 0.7f + 0.3f );
		VectorNormalize( vel );
		VectorScale( vel, speed * ( 0.4f + 0.6f * random() ), vel );
		p = DSA_Part( org, vel, size, size * 0.3f, life + (int)( random() * life * 0.4f ), rgb, 1.0f, shader, qtrue );
		if ( p ) {
			p->grav = grav;
			p->drag = 1.5f;
			p->spin = crandom() * 300;
		}
	}
}

/*
===============================================================================

ON THE BODY

===============================================================================
*/

/*
=================
CG_DSA_PlayerFX

CG_Player (next to CG_RL_PlayerFX): the affix tells around a living affixed bot
=================
*/
void CG_DSA_PlayerFX( centity_t *cent ) {
	const cgEnemy_t	*e;
	entityState_t	*s = &cent->currentState;
	int				c = s->clientNum, mask, i;
	vec3_t			base, p, vel;
	float			pulse, a, h;
	dsaPart_t		*pp;
	qboolean		emit;

	DSA_ParseCS();
	DSA_RunParticles();
	if ( c < 0 || c >= MAX_CLIENTS || ( s->eFlags & EF_DEAD ) ) {
		return;
	}
	if ( cg.clientFrame % 30 == 0 ) {
		trap_Cvar_Update( &dsaCvTells );
	}
	if ( !dsaCvTells.integer ) {
		return;
	}
	e = CG_EN( c );
	mask = e->affix & 0xff;
	if ( !mask && !( e->affix & DSAF_CHAMPION ) ) {
		return;
	}
	if ( c == cg.snap->ps.clientNum && !cg.renderingThirdPerson ) {
		return;
	}
	VectorCopy( cent->lerpOrigin, base );
	emit = cg.time >= dsa_.nextEmit[c];
	if ( emit ) {
		dsa_.nextEmit[c] = cg.time + 45;
	}

	if ( e->affix & DSAF_CHAMPION ) {
		VectorCopy( base, p );
		p[2] += 30;
		pulse = 0.7f + 0.3f * sin( cg.time * 0.004f + c );
		trap_R_AddLightToScene( p, 110 * pulse, dsaGold[0], dsaGold[1], dsaGold[2] );
	}
	if ( mask & DSAF_VAMPIRIC ) {
		if ( dsaCvTells.integer != 2 ) {		// (2: debug, no vampire light)
			VectorCopy( base, p );
			p[2] += 20;
			trap_R_AddLightToScene( p, 60 + 15 * sin( cg.time * 0.005f + c ), 0.9f, 0.05f, 0.08f );
		}
		if ( emit ) {
			if ( random() < 0.55f ) {		// a thin mist hugging the body (~12 puffs alive)
				DSA_RandInBox( base, 13, -16, 30, p );
				VectorSet( vel, crandom() * 10, crandom() * 10, 6 + random() * 10 );
				pp = DSA_Part( p, vel, 6, 15, 850, dsaRGB[0], 0.38f, dsa_.mist, qfalse );
				if ( pp ) {
					pp->spin = crandom() * 40;
					pp->rgba[0] = 0.85f; pp->rgba[1] = 0.04f; pp->rgba[2] = 0.08f;
				}
			}
			if ( random() < 0.35f ) {		// a bright blood droplet now and then
				DSA_RandInBox( base, 14, 0, 30, p );
				VectorSet( vel, crandom() * 30, crandom() * 30, 40 + random() * 40 );
				pp = DSA_Part( p, vel, 2.6f, 1.2f, 600, dsaRGB[0], 1.0f, cgs.media.fxGlowShader, qtrue );
				if ( pp ) {
					pp->grav = 300;
				}
			}
		}
	}
	if ( mask & DSAF_EXPLODE ) {
		VectorCopy( base, p );
		p[2] += 14;
		pulse = 0.5f + 0.5f * sin( cg.time * 0.012f + c );
		trap_R_AddLightToScene( p, 70 + 70 * pulse, 1.0f, 0.35f, 0.05f );
		if ( emit && random() < 0.5f ) {
			DSA_RandInBox( base, 10, 4, 26, p );
			VectorSet( vel, crandom() * 90, crandom() * 90, 60 + random() * 120 );
			pp = DSA_Part( p, vel, 3.2f, 1.0f, 450, dsaRGB[1], 1.0f, cgs.media.fxGlowShader, qtrue );
			if ( pp ) {
				pp->grav = 380;
			}
		}
	}
	if ( mask & DSAF_SPLIT ) {
		for ( i = 0 ; i < 3 ; i++ ) {
			a = cg.time * 0.0035f + i * ( 2 * M_PI / 3 ) + c;
			h = 8 + 18 * sin( cg.time * 0.002f + i * 2.1f );
			VectorSet( p, base[0] + cos( a ) * 26, base[1] + sin( a ) * 26, base[2] + h );
			DSA_Sprite( p, 7.5f, dsaRGB[2], 1.0f, dsa_.shard, cg.time * 0.3f + i * 120 );
		}
		trap_R_AddLightToScene( base, 70, dsaRGB[2][0], dsaRGB[2][1], dsaRGB[2][2] );
	}
	if ( mask & DSAF_REFLECT ) {
		if ( emit && random() < 0.6f ) {
			DSA_RandInBox( base, 14, -20, 30, p );
			VectorClear( vel );
			pp = DSA_Part( p, vel, 2.5f, 9, 280, dsaRGB[3], 1.0f, dsa_.glint, qtrue );
			if ( pp ) {
				pp->spin = 200;
			}
		}
	}
	if ( mask & DSAF_FROST ) {
		VectorCopy( base, p );
		p[2] += 10;
		trap_R_AddLightToScene( p, 60, 0.35f, 0.65f, 1.0f );
		if ( emit ) {
			for ( i = 0 ; i < 2 ; i++ ) {
				DSA_RandInBox( base, 20, -10, 40, p );
				VectorSet( vel, crandom() * 8, crandom() * 8, -20 - random() * 20 );
				pp = DSA_Part( p, vel, 4.5f, 1.5f, 900, dsaRGB[4], 1.0f, dsa_.crystal, qtrue );
				if ( pp ) {
					pp->spin = crandom() * 220;
					pp->grav = 30;
				}
			}
		}
	}
	if ( mask & DSAF_FIRETRAIL ) {
		VectorCopy( base, p );
		p[2] -= 16;
		trap_R_AddLightToScene( p, 70 + 20 * random(), 1.0f, 0.6f, 0.15f );
		if ( emit ) {
			for ( i = 0 ; i < 2 ; i++ ) {
				DSA_RandInBox( base, 14, -24, 10, p );
				VectorSet( vel, crandom() * 20, crandom() * 20, 70 + random() * 60 );
				pp = DSA_Part( p, vel, 3.6f, 0.8f, 750, dsaRGB[5], 1.0f, cgs.media.fxGlowShader, qtrue );
				if ( pp ) {
					pp->drag = 0.6f;
				}
			}
		}
	}
	if ( mask & DSAF_PULL ) {
		if ( emit ) {
			a = random() * 2 * M_PI;
			VectorSet( p, base[0] + cos( a ) * 46, base[1] + sin( a ) * 46, base[2] - 6 + random() * 24 );
			VectorSet( vel, -cos( a ) * 70 - sin( a ) * 40, -sin( a ) * 70 + cos( a ) * 40, 0 );
			pp = DSA_Part( p, vel, 3.4f, 1.2f, 600, dsaRGB[6], 1.0f, cgs.media.fxGlowShader, qtrue );
		}
		if ( e->flags & DSEF_TELEGRAPH ) {
			VectorCopy( base, p );
			p[2] -= 23;
			pulse = 0.5f + 0.5f * sin( cg.time * 0.03f );
			DSA_Ring( p, 34 + 10 * pulse, dsaRGB[6], 0.8f );
			trap_R_AddLightToScene( base, 150 + 80 * pulse, dsaRGB[6][0], dsaRGB[6][1], dsaRGB[6][2] );
		}
	}
	if ( ( mask & DSAF_REGEN ) && ( dsa_.regenMask & ( 1 << c ) ) ) {
		if ( !( dsa_.regenWas & ( 1 << c ) ) ) {
			trap_S_StartSound( NULL, c, CHAN_AUTO, dsa_.sRegen );
		}
		if ( emit ) {
			DSA_RandInBox( base, 16, -20, 20, p );
			VectorSet( vel, 0, 0, 40 + random() * 30 );
			DSA_Part( p, vel, 3.6f, 1.2f, 800, dsaRGB[7], 1.0f, cgs.media.fxGlowShader, qtrue );
		}
		VectorCopy( base, p );
		trap_R_AddLightToScene( p, 70, dsaRGB[7][0] * 0.6f, dsaRGB[7][1] * 0.8f, dsaRGB[7][2] * 0.6f );
	}
	if ( dsa_.regenMask & ( 1 << c ) ) {
		dsa_.regenWas |= 1 << c;
	} else {
		dsa_.regenWas &= ~( 1 << c );
	}
}

/*
=================
CG_DSA_BodyShell

CG_AddRefEntityWithPowerups (after CG_RL_BodyShell): the champion's gold sheen, the chrome flash
of a reflected hit
=================
*/
void CG_DSA_BodyShell( refEntity_t *ent, entityState_t *state ) {
	const cgEnemy_t	*e;
	int				c = state->clientNum;
	float			f;
	byte			save[4];
	qhandle_t		saveShader;

	if ( state->eType != ET_PLAYER || c < 0 || c >= MAX_CLIENTS || ( state->eFlags & EF_DEAD ) ) {
		return;
	}
	e = CG_EN( c );
	if ( !e->affix ) {
		return;
	}
	saveShader = ent->customShader;
	save[0] = ent->shaderRGBA[0]; save[1] = ent->shaderRGBA[1]; save[2] = ent->shaderRGBA[2]; save[3] = ent->shaderRGBA[3];
	if ( dsa_.reflectUntil[c] > cg.time ) {
		f = ( dsa_.reflectUntil[c] - cg.time ) / 180.0f;
		ent->customShader = dsa_.chrome;
		ent->shaderRGBA[0] = ent->shaderRGBA[1] = ent->shaderRGBA[2] = (byte)( 255 * CG_Clamp01( f ) );
		ent->shaderRGBA[3] = 255;
		trap_R_AddRefEntityToScene( ent );
	}
	if ( e->affix & DSAF_CHAMPION ) {
		f = 0.35f + 0.15f * sin( cg.time * 0.004f + c );
		ent->customShader = dsa_.gold;
		ent->shaderRGBA[0] = (byte)( 255 * f );
		ent->shaderRGBA[1] = (byte)( 205 * f );
		ent->shaderRGBA[2] = (byte)( 90 * f );
		ent->shaderRGBA[3] = 255;
		trap_R_AddRefEntityToScene( ent );
	}
	ent->shaderRGBA[0] = save[0]; ent->shaderRGBA[1] = save[1]; ent->shaderRGBA[2] = save[2]; ent->shaderRGBA[3] = save[3];
	ent->customShader = saveShader;
}

/*
===============================================================================

RM_ENT_AFFIXFX

===============================================================================
*/

static qboolean DSA_ClientPos( int c, vec3_t out, float up ) {
	centity_t	*ce;

	if ( c < 0 || c >= MAX_CLIENTS ) {
		return qfalse;
	}
	if ( c == cg.snap->ps.clientNum ) {
		VectorCopy( cg.predictedPlayerState.origin, out );
		out[2] += up;
		return qtrue;
	}
	ce = &cg_entities[c];
	if ( !ce->currentValid ) {
		return qfalse;
	}
	VectorCopy( ce->lerpOrigin, out );
	out[2] += up;
	return qtrue;
}

static void DSA_MaskColor( int mask, int k, float *out ) {
	int		i, n = 0;

	for ( i = 0 ; i < DSAF_NUM ; i++ ) {
		if ( mask & ( 1 << i ) ) {
			if ( n == k ) {
				VectorCopy( dsaRGB[i], out );
				return;
			}
			n++;
		}
	}
	VectorCopy( dsaGold, out );
}

qboolean CG_DSA_Entity( centity_t *cent ) {
	entityState_t	*s = &cent->currentState;
	int				num = s->number, kind;
	qboolean		first, land;
	vec3_t			org, a, b, col, up = { 0, 0, 1 }, vel;
	float			f, r, pulse;
	int				i;

	if ( s->generic1 != RM_ENT_AFFIXFX ) {
		return qfalse;
	}
	DSA_ParseCS();
	DSA_RunParticles();
	kind = s->modelindex2;
	first = dsaSeen[num] != s->time;
	if ( first ) {
		dsaSeen[num] = s->time;
	}
	land = cg.time >= s->time2 && dsaLanded[num] != s->time;
	if ( land ) {
		dsaLanded[num] = s->time;
	}
	VectorCopy( cent->lerpOrigin, org );

	switch ( kind ) {
	case DSAFX_BOMB:
		if ( first ) {
			trap_S_StartSound( org, ENTITYNUM_WORLD, CHAN_AUTO, dsa_.sFuse );
		}
		if ( cg.time < s->time2 ) {
			f = CG_Clamp01( (float)( cg.time - s->time ) / (float)( s->time2 - s->time ) );
			pulse = 0.5f + 0.5f * sin( cg.time * ( 0.015f + 0.05f * f ) );
			VectorSet( col, 1.0f, 0.35f + 0.5f * f, 0.08f + 0.5f * f * f );
			a[0] = org[0]; a[1] = org[1]; a[2] = org[2] + 6;
			DSA_Sprite( a, 10 + 40 * f * f + 8 * pulse, col, 0.6f + 0.4f * pulse, cgs.media.fxGlowShader, 0 );
			trap_R_AddLightToScene( a, 90 + 170 * f + 40 * pulse, col[0], col[1], col[2] );
			if ( random() < 0.4f + 0.5f * f ) {
				VectorSet( vel, crandom() * 140, crandom() * 140, 80 + random() * 160 );
				DSA_Part( a, vel, 2, 0.6f, 400, col, 1.0f, cgs.media.fxGlowShader, qtrue );
			}
		}
		if ( land ) {
			VectorSet( col, 1.0f, 0.45f, 0.12f );
			CG_FxExplosion( org, up, 1.5f, col, qtrue );
			CG_FxShake( org, 9, 700 );
			DSA_Burst( org, dsaRGB[1], 26, 520, 600, 3, cgs.media.fxGlowShader, 500 );
			trap_S_StartSound( org, ENTITYNUM_WORLD, CHAN_AUTO, dsa_.sBlast );
		}
		return qtrue;

	case DSAFX_REFLECT:
		if ( first && s->otherEntityNum >= 0 && s->otherEntityNum < MAX_CLIENTS ) {
			dsa_.reflectUntil[s->otherEntityNum] = cg.time + 180;
			if ( DSA_ClientPos( s->otherEntityNum, a, 16 ) ) {
				DSA_Burst( a, dsaRGB[3], 8, 260, 250, 2.5f, dsa_.glint, 0 );
			}
			trap_S_StartSound( NULL, s->otherEntityNum, CHAN_AUTO, dsa_.sReflect );
		}
		return qtrue;

	case DSAFX_PULL:
		if ( first ) {
			trap_S_StartSound( NULL, s->otherEntityNum, CHAN_AUTO, dsa_.sPullWind );
		}
		if ( !DSA_ClientPos( s->otherEntityNum, a, 18 ) || !DSA_ClientPos( s->otherEntityNum2, b, 8 ) ) {
			return qtrue;
		}
		if ( s->frame & 0x100 ) {
			// snapped: a spray of motes where it broke
			if ( dsaLanded[num] != -s->time ) {
				dsaLanded[num] = -s->time;
				VectorAdd( a, b, col );
				VectorScale( col, 0.5f, col );
				DSA_Burst( col, dsaRGB[6], 14, 220, 400, 2.5f, cgs.media.fxGlowShader, 200 );
				trap_S_StartSound( col, ENTITYNUM_WORLD, CHAN_AUTO, dsa_.sPullSnap );
			}
			return qtrue;
		}
		if ( cg.time < s->time2 ) {
			f = CG_Clamp01( (float)( cg.time - s->time ) / (float)( s->time2 - s->time ) );
			pulse = 0.5f + 0.5f * sin( cg.time * ( 0.02f + 0.04f * f ) );
			DSA_Beam( a, b, dsaRGB[6], 0.25f + 0.6f * f * ( 0.6f + 0.4f * pulse ) );
		} else if ( cg.time < s->time2 + 250 ) {
			DSA_Beam( a, b, dsaRGB[6], 1.0f - ( cg.time - s->time2 ) / 250.0f );
		}
		if ( land ) {
			DSA_Burst( b, dsaRGB[6], 12, 200, 350, 2.5f, cgs.media.fxGlowShader, 0 );
			trap_S_StartSound( NULL, s->otherEntityNum2, CHAN_AUTO, dsa_.sPullYank );
			if ( s->otherEntityNum2 == cg.snap->ps.clientNum ) {
				CG_FxPunch( 3 );
			}
		}
		return qtrue;

	case DSAFX_SPLIT:
		if ( first ) {
			trap_S_StartSound( org, ENTITYNUM_WORLD, CHAN_AUTO, dsa_.sSplit );
		}
		if ( cg.time < s->time2 ) {
			f = CG_Clamp01( (float)( cg.time - s->time ) / (float)( s->time2 - s->time ) );
			for ( i = 0 ; i < 5 ; i++ ) {
				a[0] = org[0]; a[1] = org[1]; a[2] = org[2] - 16 + i * 12 * ( 0.4f + 0.6f * f );
				DSA_Sprite( a, 6 + 10 * f, dsaRGB[2], 0.8f, cgs.media.fxGlowShader, 0 );
			}
			trap_R_AddLightToScene( org, 100 + 200 * f, dsaRGB[2][0], dsaRGB[2][1], dsaRGB[2][2] );
		}
		if ( land ) {
			DSA_Burst( org, dsaRGB[2], 30, 360, 700, 4, dsa_.shard, 250 );
			CG_GoreLight( org, 360, dsaRGB[2][0], dsaRGB[2][1], dsaRGB[2][2], 300 );
		}
		return qtrue;

	case DSAFX_PILLAR:
		if ( first && cg.time - dsa_.lastWarnSound > 400 ) {
			dsa_.lastWarnSound = cg.time;
			trap_S_StartLocalSound( dsa_.sChampWarn, CHAN_LOCAL_SOUND );
			VectorCopy( org, dsa_.chSpot );
			dsa_.chSpotValid = qtrue;
		}
		if ( cg.time < s->time2 ) {
			f = CG_Clamp01( (float)( cg.time - s->time ) / (float)( s->time2 - s->time ) );
			pulse = 0.5f + 0.5f * sin( cg.time * 0.012f );
			VectorCopy( org, a );
			a[2] -= 24;
			VectorCopy( a, b );
			b[2] += 900;
			DSA_Beam( a, b, dsaGold, 0.5f + 0.5f * f );
			DSA_Ring( a, 40 + 30 * ( 1.0f - f ) + 6 * pulse, dsaGold, 0.7f );
			// the pack's affix colours swirl up the beam
			if ( random() < 0.5f ) {
				DSA_MaskColor( s->frame, rand() % ( 1 + ( ( s->frame & 0xff ) ? 2 : 0 ) ), col );
				r = random() * 2 * M_PI;
				VectorSet( b, a[0] + cos( r ) * 18, a[1] + sin( r ) * 18, a[2] + 4 );
				VectorSet( vel, 0, 0, 160 + random() * 120 );
				DSA_Part( b, vel, 3, 1, 900, col, 1.0f, cgs.media.fxGlowShader, qtrue );
			}
			trap_R_AddLightToScene( a, 120 + 120 * f, dsaGold[0], dsaGold[1], dsaGold[2] );
		}
		if ( land ) {
			DSA_Burst( org, dsaGold, 20, 300, 500, 3, cgs.media.fxGlowShader, 300 );
			trap_S_StartSound( org, ENTITYNUM_WORLD, CHAN_AUTO, dsa_.sChampLand );
			CG_FxShake( org, 5, 900 );
		}
		return qtrue;

	case DSAFX_DRAIN:
		if ( first && DSA_ClientPos( s->otherEntityNum, b, 14 ) ) {
			VectorCopy( org, a );
			a[2] += 16;
			VectorSubtract( b, a, vel );
			r = VectorLength( vel );
			VectorScale( vel, 1.0f / 0.35f, vel );		// arrives in ~0.35 s
			for ( i = 0 ; i < 8 ; i++ ) {
				dsaPart_t	*pp;

				VectorSet( col, a[0] + crandom() * 6, a[1] + crandom() * 6, a[2] + crandom() * 6 );
				pp = DSA_Part( col, vel, 3.0f - i * 0.2f, 1.5f, 330 + i * 10, dsaRGB[0], 1.0f, cgs.media.fxGlowShader, qtrue );
				if ( pp ) {
					pp->start -= i * 20;
				}
			}
			if ( r < 2000 ) {
				trap_S_StartSound( NULL, s->otherEntityNum, CHAN_AUTO, dsa_.sDrain );
			}
		}
		return qtrue;

	case DSAFX_FROSTHIT:
		if ( first && DSA_ClientPos( s->otherEntityNum2, a, 10 ) ) {
			if ( s->otherEntityNum2 != cg.snap->ps.clientNum || cg.renderingThirdPerson ) {
				DSA_Burst( a, dsaRGB[4], 14, 160, 600, 3, dsa_.crystal, 200 );
			}
			trap_S_StartSound( NULL, s->otherEntityNum2, CHAN_AUTO, dsa_.sFrost );
		}
		return qtrue;

	default:
		return qtrue;
	}
}

/*
===============================================================================

2D: frost edges, the champion pack line, TAB

===============================================================================
*/

static void DSA_Text( float x, float y, const char *s, const float *color, float cw, float ch, int align ) {
	float	w = CG_DrawStrlen( s ) * (int)( cw + 0.5f );

	if ( align == 0 ) {
		x -= w * 0.5f;
	} else if ( align > 0 ) {
		x -= w;
	}
	CG_DrawStringExt( (int)x, (int)y, s, color, qtrue, qtrue, (int)( cw + 0.5f ), (int)( ch + 0.5f ), 0 );
}

// draws "FROST BLAZING" word by word in each affix's colour, right-aligned at x
static void DSA_AffixLine( float x, float y, int mask, float cw, float ch, float alpha, int align ) {
	char	word[32];
	vec4_t	col;
	float	w = 0, xx;
	int		i, n = 0;

	for ( i = 0 ; i < DSAF_NUM ; i++ ) {
		if ( mask & ( 1 << i ) ) {
			w += ( strlen( BG_DS_AffixName( i ) ) + ( n ? 1 : 0 ) ) * (int)( cw + 0.5f );
			n++;
		}
	}
	xx = align > 0 ? x - w : align == 0 ? x - w * 0.5f : x;
	n = 0;
	for ( i = 0 ; i < DSAF_NUM ; i++ ) {
		if ( !( mask & ( 1 << i ) ) ) {
			continue;
		}
		Com_sprintf( word, sizeof( word ), "%s%s", n ? " " : "", BG_DS_AffixName( i ) );
		// en-screens: lifted a quarter toward white (the deep VAMPIRIC red vanished on the dark callout box)
		col[0] = dsaRGB[i][0] * 0.75f + 0.25f; col[1] = dsaRGB[i][1] * 0.75f + 0.25f; col[2] = dsaRGB[i][2] * 0.75f + 0.25f; col[3] = alpha;
		CG_DrawStringExt( (int)xx, (int)y, word, col, qtrue, qtrue, (int)( cw + 0.5f ), (int)( ch + 0.5f ), 0 );
		xx += strlen( word ) * (int)( cw + 0.5f );
		n++;
	}
}

static const char *DSA_Bearing( const vec3_t spot ) {
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

static void DSA_FrostEdges( void ) {
	vec4_t	c;
	float	a, w, h;

	if ( dsa_.frostClient != cg.snap->ps.clientNum || dsa_.frostUntil <= cg.time - 400 ) {
		return;
	}
	a = dsa_.frostUntil > cg.time ? CG_Clamp01( ( dsa_.frostUntil - cg.time ) / 300.0f ) : 0;
	a = 0.65f * ( a > 0 ? a : CG_Clamp01( 1.0f - ( cg.time - dsa_.frostUntil ) / 400.0f ) );
	if ( a <= 0.01f ) {
		return;
	}
	w = cgs.glconfig.vidHeight * 0.16f;
	h = cgs.glconfig.vidHeight * 0.10f;
	c[0] = 0.55f * a; c[1] = 0.80f * a; c[2] = 1.0f * a; c[3] = 1;
	trap_R_SetColor( c );
	trap_R_DrawStretchPic( 0, 0, w, cgs.glconfig.vidHeight, 0, 0, 1, 1, dsa_.edgeH );
	trap_R_DrawStretchPic( cgs.glconfig.vidWidth - w, 0, w, cgs.glconfig.vidHeight, 1, 0, 0, 1, dsa_.edgeH );
	trap_R_DrawStretchPic( 0, 0, cgs.glconfig.vidWidth, h, 0, 0, 1, 1, dsa_.edgeV );
	trap_R_DrawStretchPic( 0, cgs.glconfig.vidHeight - h, cgs.glconfig.vidWidth, h, 0, 1, 1, 0, dsa_.edgeV );
	trap_R_SetColor( NULL );
}

static void DSA_ChampLine( void ) {
	vec4_t		col, bg;
	float		x = 620, y, t, al, slide, w;
	char		l1[64], l2[64];
	int			dur;

	if ( !dsa_.chState ) {
		return;
	}
	t = cg.time - dsa_.chStateTime;
	l1[0] = l2[0] = 0;
	switch ( dsa_.chState ) {
	case 1:
		Q_strncpyz( l1, "CHAMPION PACK", sizeof( l1 ) );
		Com_sprintf( l2, sizeof( l2 ), "INCOMING - %s", dsa_.chSpotValid ? DSA_Bearing( dsa_.chSpot ) : "WATCH FOR THE GOLD" );
		dur = 6000;
		break;
	case 2:
		if ( t < 3500 ) {
			Q_strncpyz( l1, "CHAMPIONS HAVE LANDED", sizeof( l1 ) );
			Q_strncpyz( l2, "3 OF THEM - ONE LIFE EACH - BETTER LOOT", sizeof( l2 ) );
		} else {
			Com_sprintf( l1, sizeof( l1 ), "CHAMPIONS %i/3", dsa_.chAlive > 0 ? dsa_.chAlive : 0 );
		}
		dur = 9999999;
		break;
	default:
		Q_strncpyz( l1, dsa_.chKills > 0 ? "CHAMPION PACK DESTROYED" : "CHAMPION PACK GONE", sizeof( l1 ) );
		if ( dsa_.chKills > 0 ) {
			Com_sprintf( l2, sizeof( l2 ), "%i OF 3 BY YOU - A CHEST", dsa_.chKills );
		}
		dur = 3500;
		break;
	}
	if ( t > dur ) {
		return;
	}
	al = CG_Clamp01( t / 150.0f ) * ( dur < 9999999 ? CG_Clamp01( ( dur - t ) / 400.0f ) : 1.0f );
	slide = 22.0f * ( 1.0f - CG_Clamp01( t / 200.0f ) );
	y = CG_EdgeSlot( EDGE_CHAMP, l2[0] ? 33 : 24 ) + 3;		// en-screens: the shared right-edge stack
	CG_HudAnchor( 1 );
	w = CG_DrawStrlen( l1 ) * 8 + 12;
	if ( CG_DrawStrlen( l2 ) * 5 + 12 > w ) {
		w = CG_DrawStrlen( l2 ) * 5 + 12;
	}
	if ( CG_DrawStrlen( CG_DSA_Names( dsa_.chMask ) ) * 5 + 12 > w ) {
		w = CG_DrawStrlen( CG_DSA_Names( dsa_.chMask ) ) * 5 + 12;
	}
	bg[0] = 0.06f; bg[1] = 0.04f; bg[2] = 0.0f; bg[3] = 0.55f * al;
	CG_FillRect( x - w + slide, y - 3, w + 2, l2[0] ? 33 : 24, bg );
	bg[0] = dsaGold[0]; bg[1] = dsaGold[1]; bg[2] = dsaGold[2]; bg[3] = 0.9f * al;
	CG_FillRect( x + 1 + slide, y - 3, 2, l2[0] ? 33 : 24, bg );
	col[0] = dsaGold[0]; col[1] = dsaGold[1]; col[2] = dsaGold[2]; col[3] = al;
	if ( dsa_.chState == 1 ) {
		col[3] = al * ( 0.7f + 0.3f * sin( cg.time * 0.02f ) );
	}
	DSA_Text( x - 3 + slide, y, l1, col, 8, 11, 1 );
	DSA_AffixLine( x - 3 + slide, y + 13, dsa_.chMask, 5, 7, 0.95f * al, 1 );
	if ( l2[0] ) {
		col[0] = 0.9f; col[1] = 0.86f; col[2] = 0.8f; col[3] = 0.85f * al;
		DSA_Text( x - 3 + slide, y + 22, l2, col, 5, 7, 1 );
	}
	CG_HudAnchor( 0 );
}

/*
=================
CG_DSA_Draw2D

CG_Draw2D, with the other DESCENT edge callouts
=================
*/
void CG_DSA_Draw2D( void ) {
	DSA_ParseCS();
	if ( !dsa_.cs[0] ) {
		return;
	}
	DSA_FrostEdges();
	DSA_ChampLine();
}

/*
=================
CG_DSA_ScoreTag

TAB scoreboard: the affix names small under a bot's name, each in its colour (en-types' archetype
and en-personality's rank pips stay on the name line). The compact board (many players) has no
room under the name: the names go to the right end of the row, tiny.
=================
*/
void CG_DSA_ScoreTag( int client, float x, float y, qboolean large, float fade ) {
	const cgEnemy_t	*e = CG_EN( client );
	vec4_t			col;
	int				mask;
	float			w;

	if ( !e->affix ) {
		return;
	}
	mask = e->affix & 0xff;
	if ( large ) {
		y += BIGCHAR_HEIGHT + 2;
		if ( e->affix & DSAF_CHAMPION ) {
			col[0] = dsaGold[0]; col[1] = dsaGold[1]; col[2] = dsaGold[2]; col[3] = fade;
			CG_DrawStringExt( (int)x, (int)y, "CHAMPION", col, qtrue, qtrue, 6, 8, 0 );
			x += 9 * 6;
		}
		if ( mask ) {
			DSA_AffixLine( x, y, mask, 6, 8, fade, -1 );
		}
		return;
	}
	w = ( mask ? CG_DrawStrlen( CG_DSA_Names( mask ) ) * 4 : 0 ) + ( ( e->affix & DSAF_CHAMPION ) ? 9 * 4 : 0 );
	x = 636 - w;
	if ( e->affix & DSAF_CHAMPION ) {
		col[0] = dsaGold[0]; col[1] = dsaGold[1]; col[2] = dsaGold[2]; col[3] = fade;
		CG_DrawStringExt( (int)x, (int)y + 10, "CHAMPION", col, qtrue, qtrue, 4, 6, 0 );
		x += 9 * 4;
	}
	if ( mask ) {
		DSA_AffixLine( x, y + 10, mask, 4, 6, fade, -1 );
	}
}
