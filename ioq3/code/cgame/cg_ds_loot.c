/*
===========================================================================
REARMED -- DESCENT v2 LOOT (cgame side): CS_DESCENTGROW (the player's growth), gems flying
in, the vacuum, chests, the xp bar + chest counter, the end-of-floor jackpot screen and the
synergy tags on the cards. Game side: game/g_ds_loot.c.
===========================================================================
*/
#include "cg_local.h"

#define RLP_CHEST_CG		2		// g_roguelike.c RLP_CHEST

#define DS_TICK_SOUNDS		12
#define DS_TICK_GAP			40		// msec between two pickup ticks at least (queued, not dropped)
#define DS_TICK_COMBO		450		// a tick within this of the last one climbs the scale

#define JP_INTRO			380		// msec: the chest plate drops in
#define JP_SPIN				800		// first reel stops this long after the intro
#define JP_STAGGER			320		// the next reels
#define JP_HOLD				1400
#define JP_HOLD_BIG			2200	// a jackpot or an evolution stays up longer
#define JP_REEL_W			74
#define JP_REEL_H			92
#define JP_REEL_GAP			10
#define JP_Y				150		// reel windows' top

typedef struct {
	qboolean	on;
	int			wl[DS_NUM_WEAPONS], wx[DS_NUM_WEAPONS], pv[RLP_NUM], ev;
	int			chests[16], numChests;
	int			gm, lt_gr, chr, act, wu, vac, lk;
	int			jpQ[8], jpN[8], jpR[8][5], numJP;
} lt_cgGrow_t;

static lt_cgGrow_t		lt_gr;

static qhandle_t	lt_gemModel[3], lt_chestModel[3], lt_magnetModel, lt_glowShader, lt_ringShader, lt_whiteShader;
static qhandle_t	lt_weaponIcon[DS_NUM_WEAPONS];
static sfxHandle_t	lt_sndTick[DS_TICK_SOUNDS], lt_sndVacuum, lt_sndChestGet, lt_sndReel, lt_sndStop, lt_sndJackpot, lt_sndEvolve, lt_sndOpen;

static int			lt_gemTicked[MAX_GENTITIES];	// s.time2 of the flight that already ticked
static int			lt_tickLast, lt_tickSound, lt_tickStep, lt_tickCount;
static int			lt_gemFlash;					// cg.time of the last gem collected (xp bar flash)
static int			lt_vacTime;					// cg.time the last vacuum was taken
static float		lt_barShown;
static int			lt_barWeapon;

// jackpot screen
static int			lt_jpSig, lt_jpStart, lt_jpChest, lt_jpChestStart, lt_jpDoneSent, lt_jpStopped, lt_jpReelTick, lt_jpLastCmd, lt_jpLastButtons, lt_jpLastUp;

static const vec4_t lt_gemColors[3] = {
	{ 0.30f, 0.62f, 1.00f, 1 },		// blue 1
	{ 0.30f, 1.00f, 0.45f, 1 },		// green 5
	{ 1.00f, 0.25f, 0.22f, 1 }		// red 25
};
static const vec4_t lt_chestColors[3] = {
	{ 0.85f, 0.62f, 0.30f, 1 },		// plain: bronze
	{ 1.00f, 0.55f, 0.12f, 1 },		// elite: orange
	{ 0.80f, 0.40f, 1.00f, 1 }		// boss: violet
};
static const char *lt_chestNames[3] = { "CHEST", "ELITE CHEST", "BOSS CHEST" };
static const vec4_t lt_passiveColor = { 0.25f, 0.90f, 0.85f, 1 };
static const vec4_t lt_weaponColor = { 1.00f, 0.42f, 0.30f, 1 };
static const vec4_t lt_evoColor = { 1.00f, 0.84f, 0.25f, 1 };

/*
===============================================================================

STATE

===============================================================================
*/

static int CLoot_ParseDots( const char *s, int *out, int max ) {
	int		n = 0;

	while ( *s && n < max ) {
		while ( *s == '.' || *s == ',' ) {
			s++;
		}
		if ( !*s || *s == '-' || *s == '_' ) {
			break;
		}
		out[n++] = atoi( s );
		while ( *s && *s != '.' && *s != ',' && *s != '_' ) {
			s++;
		}
	}
	return n;
}

void CG_DS_ParseGrow( void ) {
	const char	*info = CG_ConfigString( CS_DESCENTGROW );
	const char	*s;
	int			oldChests = lt_gr.numChests, oldVac = lt_gr.vac, oldGm = lt_gr.gm, oldOn = lt_gr.on, i, n;

	if ( !info[0] ) {
		memset( &lt_gr, 0, sizeof( lt_gr ) );
		return;
	}
	lt_gr.on = qtrue;
	memset( lt_gr.wl, 0, sizeof( lt_gr.wl ) );
	memset( lt_gr.wx, 0, sizeof( lt_gr.wx ) );
	memset( lt_gr.pv, 0, sizeof( lt_gr.pv ) );
	CLoot_ParseDots( Info_ValueForKey( info, "wl" ), lt_gr.wl, DS_NUM_WEAPONS );
	CLoot_ParseDots( Info_ValueForKey( info, "wx" ), lt_gr.wx, DS_NUM_WEAPONS );
	CLoot_ParseDots( Info_ValueForKey( info, "pv" ), lt_gr.pv, RLP_NUM );
	lt_gr.ev = atoi( Info_ValueForKey( info, "ev" ) );
	lt_gr.numChests = CLoot_ParseDots( Info_ValueForKey( info, "ch" ), lt_gr.chests, 16 );
	lt_gr.gm = atoi( Info_ValueForKey( info, "gm" ) );
	lt_gr.lt_gr = atoi( Info_ValueForKey( info, "lt_gr" ) );
	lt_gr.chr = atoi( Info_ValueForKey( info, "chr" ) );
	lt_gr.act = atoi( Info_ValueForKey( info, "act" ) );
	lt_gr.wu = atoi( Info_ValueForKey( info, "wu" ) );
	lt_gr.vac = atoi( Info_ValueForKey( info, "vac" ) );
	lt_gr.lk = atoi( Info_ValueForKey( info, "lk" ) );

	// jackpot: "q:r.r.r_q:r"
	lt_gr.numJP = 0;
	s = Info_ValueForKey( info, "jp" );
	while ( *s && lt_gr.numJP < 8 ) {
		lt_gr.jpQ[lt_gr.numJP] = atoi( s ) & 3;
		if ( lt_gr.jpQ[lt_gr.numJP] > 2 ) {
			lt_gr.jpQ[lt_gr.numJP] = 2;
		}
		while ( *s && *s != ':' ) {
			s++;
		}
		if ( *s ) {
			s++;
		}
		n = CLoot_ParseDots( s, lt_gr.jpR[lt_gr.numJP], 5 );
		lt_gr.jpN[lt_gr.numJP] = n;
		if ( n > 0 ) {
			lt_gr.numJP++;
		}
		while ( *s && *s != '_' ) {
			s++;
		}
		if ( *s ) {
			s++;
		}
	}

	if ( oldOn && cg.snap && cg.snap->ps.pm_type != PM_INTERMISSION ) {
		if ( lt_gr.numChests > oldChests ) {
			trap_S_StartLocalSound( lt_sndChestGet, CHAN_AUTO );
		}
		if ( lt_gr.vac > oldVac ) {
			lt_vacTime = cg.time;
			trap_S_StartLocalSound( lt_sndVacuum, CHAN_AUTO );
		}
	}
	if ( lt_gr.gm > oldGm ) {
		lt_gemFlash = cg.time;
	}
	for ( i = 0 ; i < DS_NUM_WEAPONS ; i++ ) {
		if ( lt_gr.wl[i] < 0 ) {
			lt_gr.wl[i] = 0;
		}
	}
}

int CG_DS_WeaponLevel( int w ) {
	if ( !lt_gr.on || w < 0 || w >= DS_NUM_WEAPONS || !DS_WEAPON_LEVELS( w ) ) {
		return 0;
	}
	return lt_gr.wl[w] < 1 ? 1 : lt_gr.wl[w];
}

int CG_DS_WeaponXP( int w ) {
	return ( lt_gr.on && w >= 0 && w < DS_NUM_WEAPONS ) ? lt_gr.wx[w] : 0;
}

int CG_DS_PassiveLevel( int p ) {
	return ( lt_gr.on && p >= 0 && p < RLP_NUM ) ? lt_gr.pv[p] : 0;
}

int CG_DS_EvolvedMask( void ) {
	return lt_gr.on ? lt_gr.ev : 0;
}

int CG_DS_ChestsBanked( void ) {
	return lt_gr.on ? lt_gr.numChests : 0;
}

void CG_DS_LootRegister( void ) {
	int			i;
	gitem_t		*it;

	for ( i = 0 ; i < 3 ; i++ ) {
		lt_gemModel[i] = trap_R_RegisterModel( va( "models/rearmed/loot/gem%i.md3", i ) );
		lt_chestModel[i] = trap_R_RegisterModel( va( "models/rearmed/loot/chest%i.md3", i ) );
	}
	lt_magnetModel = trap_R_RegisterModel( "models/rearmed/loot/magnet.md3" );
	lt_glowShader = trap_R_RegisterShader( "rearmed/glow" );
	lt_ringShader = trap_R_RegisterShader( "rearmed/ring" );
	lt_whiteShader = trap_R_RegisterShader( "white" );
	for ( i = 0 ; i < DS_TICK_SOUNDS ; i++ ) {
		lt_sndTick[i] = trap_S_RegisterSound( va( "sound/rearmed/loot/tick%i.wav", i ), qfalse );
	}
	lt_sndVacuum = trap_S_RegisterSound( "sound/rearmed/loot/vacuum.wav", qfalse );
	lt_sndChestGet = trap_S_RegisterSound( "sound/rearmed/loot/chest_get.wav", qfalse );
	lt_sndReel = trap_S_RegisterSound( "sound/rearmed/loot/reel.wav", qfalse );
	lt_sndStop = trap_S_RegisterSound( "sound/rearmed/loot/reel_stop.wav", qfalse );
	lt_sndJackpot = trap_S_RegisterSound( "sound/rearmed/loot/jackpot.wav", qfalse );
	lt_sndEvolve = trap_S_RegisterSound( "sound/rearmed/loot/evolve.wav", qfalse );
	lt_sndOpen = trap_S_RegisterSound( "sound/rearmed/loot/chest_open.wav", qfalse );
	for ( i = 1 ; i < DS_NUM_WEAPONS ; i++ ) {
		it = BG_FindItemForWeapon( i );
		lt_weaponIcon[i] = ( it && it->icon ) ? trap_R_RegisterShaderNoMip( it->icon ) : 0;
	}
	memset( lt_gemTicked, 0, sizeof( lt_gemTicked ) );
	memset( &lt_gr, 0, sizeof( lt_gr ) );
	lt_jpSig = 0;
}

/*
===============================================================================

ENTITIES

===============================================================================
*/

static void CLoot_Sprite( const vec3_t org, float radius, const float *rgb, float alpha, qhandle_t shader, float rotation ) {
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
	re.renderfx = RF_NOSHADOW;
	trap_R_AddRefEntityToScene( &re );
}

static void CLoot_Model( qhandle_t model, const vec3_t org, float yaw, float pitch, float scale ) {
	refEntity_t	re;
	vec3_t		ang;

	if ( !model ) {
		return;
	}
	memset( &re, 0, sizeof( re ) );
	re.reType = RT_MODEL;
	re.hModel = model;
	VectorCopy( org, re.origin );
	VectorCopy( org, re.oldorigin );
	VectorCopy( org, re.lightingOrigin );
	VectorSet( ang, pitch, yaw, 0 );
	AnglesToAxis( ang, re.axis );
	if ( scale != 1.0f ) {
		VectorScale( re.axis[0], scale, re.axis[0] );
		VectorScale( re.axis[1], scale, re.axis[1] );
		VectorScale( re.axis[2], scale, re.axis[2] );
		re.nonNormalizedAxes = qtrue;
	}
	re.renderfx = RF_NOSHADOW | RF_MINLIGHT;
	trap_R_AddRefEntityToScene( &re );
}

// where a pulled gem is heading: the player's chest
static void CLoot_PullTarget( int clientNum, vec3_t out ) {
	if ( cg.snap && clientNum == cg.snap->ps.clientNum ) {
		VectorCopy( cg.predictedPlayerState.origin, out );
		out[2] += 6;
		return;
	}
	if ( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
		VectorCopy( cg_entities[clientNum].lerpOrigin, out );
		out[2] += 6;
		return;
	}
	VectorClear( out );
}

// a gem's place on its way in: accelerating, with a lift and a swirl to the side
static void CLoot_FlightPos( const vec3_t start, const vec3_t target, float t, int num, qboolean vacuum, vec3_t out ) {
	vec3_t	d, side;
	float	e, dist, amp, lift, arc;

	t = CG_Clamp01( t );
	VectorSubtract( target, start, d );
	dist = VectorLength( d );
	e = t * t * ( 0.35f + 0.65f * t );
	VectorSet( side, -d[1], d[0], 0 );
	VectorNormalize( side );
	amp = dist * ( vacuum ? 0.22f : 0.16f );
	if ( amp > ( vacuum ? 220.0f : 70.0f ) ) {
		amp = vacuum ? 220.0f : 70.0f;
	}
	amp *= ( ( num & 1 ) ? 1.0f : -1.0f ) * ( 0.5f + 0.5f * ( ( num * 7 ) % 5 ) / 4.0f );
	arc = sin( t * M_PI ) * ( 1.0f - t * 0.6f );
	lift = ( 14.0f + dist * ( vacuum ? 0.12f : 0.06f ) ) * arc;
	VectorMA( start, e, d, out );
	VectorMA( out, amp * arc, side, out );
	out[2] += lift;
}

// a gem arrived: queue a tick (they play one at a time, climbing the scale while they chain)
static void CLoot_Tick( qboolean vacuum ) {
	if ( lt_tickCount < 10 ) {
		lt_tickCount++;
	}
	if ( vacuum ) {
		lt_tickLast = cg.time;		// a vacuum climbs slower so the rise lasts the whole stream
	}
}

// every frame: play the queued ticks, at most one per DS_TICK_GAP msec; a chain climbs the scale
// (lt_tickStep counts half steps while a vacuum streams in, so the rise lasts the whole stream)
static void CLoot_TickPump( void ) {
	int		note;

	if ( cg.time < lt_tickSound ) {
		lt_tickSound = 0;		// the clock went back (map change)
	}
	if ( lt_tickCount <= 0 || cg.time - lt_tickSound < DS_TICK_GAP ) {
		return;
	}
	if ( cg.time - lt_tickSound < DS_TICK_COMBO ) {
		lt_tickStep += cg.time - lt_tickLast < 600 ? 1 : 2;
	} else {
		lt_tickStep = 0;
	}
	note = lt_tickStep / 2;
	if ( note >= DS_TICK_SOUNDS ) {
		note = DS_TICK_SOUNDS - 1;
	}
	lt_tickCount--;
	lt_tickSound = cg.time;
	trap_S_StartLocalSound( lt_sndTick[note], CHAN_AUTO );
	CG_DS_FrenzyTick( note );		// FRENZY: a louder, brighter layer while a chain runs
}

static void CG_DS_Gem( centity_t *cent ) {
	entityState_t	*s = &cent->currentState;
	int				tier = s->modelindex2, n = s->number, i, k, dur;
	vec3_t			org, target, p;
	float			t, bob, spin, scale, glow;
	qboolean		vacuum = s->frame == 1;
	static const float	scales[3] = { 0.85f, 1.1f, 1.45f };
	static const float	glows[3] = { 7.0f, 9.5f, 14.0f };

	if ( tier < 0 || tier > 2 ) {
		tier = 0;
	}
	scale = scales[tier];
	spin = cg.time * ( 0.09f + 0.01f * ( n % 5 ) ) + n * 47;
	if ( s->time && cg.time >= s->time ) {
		dur = s->time2 - s->time;
		if ( dur < 1 ) {
			dur = 1;
		}
		t = (float)( cg.time - s->time ) / dur;
		if ( t >= 1.0f ) {
			if ( lt_gemTicked[n] != s->time2 && s->otherEntityNum == cg.snap->ps.clientNum ) {
				lt_gemTicked[n] = s->time2;
				CLoot_Tick( vacuum );
			}
			return;		// collected: gone until the server frees it
		}
		CLoot_PullTarget( s->otherEntityNum, target );
		CLoot_FlightPos( s->pos.trBase, target, t, n, vacuum, org );
		org[2] += 8;
		CLoot_Model( lt_gemModel[tier], org, spin * 4, 0, scale * ( 1.0f - 0.45f * t * t ) );
		CLoot_Sprite( org, glows[tier] * ( 1.1f + 0.4f * t ) * ( vacuum ? 1.5f : 1.0f ), lt_gemColors[tier], 0.9f, lt_glowShader, 0 );
		// a tail: short for a normal pull, a comet's for the vacuum
		k = vacuum ? 7 : 2;
		for ( i = 1 ; i <= k ; i++ ) {
			if ( t - i * 0.03f <= 0 ) {
				break;
			}
			CLoot_FlightPos( s->pos.trBase, target, t - i * 0.03f, n, vacuum, p );
			p[2] += 8;
			CLoot_Sprite( p, glows[tier] * ( vacuum ? 1.3f : 0.9f ) * ( 1.0f - (float)i / ( k + 2 ) ), lt_gemColors[tier],
				0.6f * ( 1.0f - (float)i / ( k + 1 ) ), lt_glowShader, 0 );
		}
		return;
	}
	VectorCopy( cent->lerpOrigin, org );
	bob = 8.0f + 2.5f * sin( cg.time * 0.004f + n * 1.7f );
	org[2] += bob;
	glow = glows[tier] * ( 0.85f + 0.15f * sin( cg.time * 0.006f + n ) );
	CLoot_Model( lt_gemModel[tier], org, spin, 0, scale );
	CLoot_Sprite( org, glow, lt_gemColors[tier], 0.75f, lt_glowShader, 0 );
	if ( !lt_gemModel[tier] ) {
		CLoot_Sprite( org, glow * 0.4f, lt_gemColors[tier], 1.0f, lt_whiteShader, 45 );
	}
}

static void CG_DS_Chest( centity_t *cent ) {
	entityState_t	*s = &cent->currentState;
	int				q = s->modelindex2, i;
	vec3_t			org, p;
	float			pulse = 0.5f + 0.5f * sin( cg.time * 0.005f + s->number );
	const float		*c;

	if ( q < 0 || q > 2 ) {
		q = 0;
	}
	c = lt_chestColors[q];
	VectorCopy( cent->lerpOrigin, org );
	CLoot_Model( lt_chestModel[q], org, s->number * 53.0f + cg.time * 0.01f, 0, 1.0f + 0.15f * q );
	// a soft pillar of light so it can be found from across the room
	for ( i = 0 ; i < 5 ; i++ ) {
		VectorCopy( org, p );
		p[2] += 18 + i * 16 + 4 * sin( cg.time * 0.003f + i );
		CLoot_Sprite( p, 16 - i * 2, c, ( 0.45f - i * 0.07f ) * ( 0.7f + 0.3f * pulse ), lt_glowShader, 0 );
	}
	VectorCopy( org, p );
	p[2] += 10;
	trap_R_AddLightToScene( p, 110 + 40 * pulse + 40 * q, c[0], c[1], c[2] );
}

static void CG_DS_VacuumPickup( centity_t *cent ) {
	entityState_t	*s = &cent->currentState;
	vec3_t			org;
	static const vec4_t	col = { 0.55f, 0.75f, 1.0f, 1 };
	float			pulse = 0.5f + 0.5f * sin( cg.time * 0.008f );

	VectorCopy( cent->lerpOrigin, org );
	org[2] += 16 + 3 * sin( cg.time * 0.004f );
	CLoot_Model( lt_magnetModel, org, cg.time * 0.12f, 0, 1.0f );
	CLoot_Sprite( org, 18 + 6 * pulse, col, 0.6f, lt_glowShader, 0 );
	CLoot_Sprite( org, 14 + 8 * pulse, col, 0.45f * ( 1 - pulse ), lt_ringShader, cg.time * 0.05f );
	if ( !lt_magnetModel ) {
		CLoot_Sprite( org, 8, col, 1.0f, lt_whiteShader, 45 );
	}
	trap_R_AddLightToScene( org, 120 + 40 * pulse, col[0], col[1], col[2] );
	(void)s;
}

/*
=================
CG_DS_VaultCrystal

RM_ENT_VAULTGEMS, the vault's SHOOTING GALLERY target: a big spinning gem in a ring of light
(golden: a red gem, gold light, bigger). Grows in over 0.25 s.
=================
*/
void CG_DS_VaultCrystal( centity_t *cent ) {
	entityState_t	*s = &cent->currentState;
	qboolean		gold = s->modelindex2 == 1;
	static const vec4_t	goldCol = { 1.0f, 0.82f, 0.25f, 1 };
	const float		*c = gold ? goldCol : lt_gemColors[0];
	float			pulse = 0.5f + 0.5f * sin( cg.time * 0.009f + s->number ), grow, sc;
	vec3_t			org;

	grow = s->time ? CG_Clamp01( ( cg.time - s->time ) / 250.0f ) : 1.0f;
	sc = ( gold ? 3.6f : 3.0f ) * grow;
	VectorCopy( cent->lerpOrigin, org );
	CLoot_Model( lt_gemModel[gold ? 2 : 0], org, cg.time * 0.15f + s->number * 31, 0, sc );
	CLoot_Sprite( org, ( 22 + 6 * pulse ) * grow, c, 0.7f, lt_glowShader, 0 );
	CLoot_Sprite( org, ( 26 + 4 * pulse ) * grow, c, 0.55f, lt_ringShader, cg.time * 0.08f );
	if ( !lt_gemModel[0] ) {
		CLoot_Sprite( org, 10 * grow, c, 1.0f, lt_whiteShader, 45 );
	}
	trap_R_AddLightToScene( org, ( 140 + 50 * pulse ) * grow, c[0], c[1], c[2] );
}

// CG_General: qtrue when it was one of ours
qboolean CG_DS_LootEntity( centity_t *cent ) {
	switch ( cent->currentState.generic1 ) {
	case RM_ENT_GEM:
		CG_DS_Gem( cent );
		return qtrue;
	case RM_ENT_CHEST:
		CG_DS_Chest( cent );
		return qtrue;
	case RM_ENT_VACUUM:
		CG_DS_VacuumPickup( cent );
		return qtrue;
	case RM_ENT_VAULTGEMS:
		CG_DS_VaultCrystal( cent );
		return qtrue;
	default:
		return qfalse;
	}
}

/*
===============================================================================

HUD: the held weapon's xp bar (under the ammo number), banked chests (corner block)

===============================================================================
*/

static void CLoot_Text( float x, float y, const char *s, const float *color, float cw, float ch, int align, qboolean shadow ) {
	float	w = CG_DrawStrlen( s ) * cw;

	if ( align == 0 ) {
		x -= w * 0.5f;
	} else if ( align > 0 ) {
		x -= w;
	}
	CG_DrawStringExt( (int)x, (int)y, s, color, qtrue, shadow, (int)( cw + 0.5f ), (int)( ch + 0.5f ), 0 );
}

void CG_DS_DrawHud( void ) {
	int		w, lv, lo, hi;
	float	f, flash, x = 4, y = 477, bw = 136;
	vec4_t	col;

	CLoot_TickPump();
	if ( !lt_gr.on || !cg.snap || cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR || cg.showScores
		|| cg.snap->ps.stats[STAT_HEALTH] <= 0 || !cg_drawStatus.integer ) {
		return;
	}
	w = cg.predictedPlayerState.weapon;
	if ( !DS_WEAPON_LEVELS( w ) ) {
		return;
	}
	lv = CG_DS_WeaponLevel( w );
	if ( lv >= DS_MAX_WLEVEL ) {
		f = 1.0f;
	} else {
		lo = ds_wlevelXP[lv];
		hi = ds_wlevelXP[lv + 1];
		f = hi > lo ? (float)lt_gr.wx[w] / ( hi - lo ) : 0;		// wx: xp toward the next level (ARSENAL)
		f = CG_Clamp01( f );
	}
	if ( w != lt_barWeapon ) {
		lt_barWeapon = w;
		lt_barShown = f;
	}
	lt_barShown += ( f - lt_barShown ) * 0.2f;
	if ( f < lt_barShown - 0.5f ) {
		lt_barShown = f;		// a level up: start the new bar from zero
	}
	flash = CG_Clamp01( 1.0f - ( cg.time - lt_gemFlash ) / 250.0f );
	col[0] = col[1] = col[2] = 0; col[3] = 0.45f;
	CG_FillRect( x - 1, y - 1, bw + 2, 4, col );
	if ( lv >= DS_MAX_WLEVEL ) {
		Vector4Copy( lt_evoColor, col );
	} else {
		Vector4Copy( lt_gemColors[0], col );
		col[0] += ( 1 - col[0] ) * flash;
		col[1] += ( 1 - col[1] ) * flash;
		col[2] += ( 1 - col[2] ) * flash;
	}
	col[3] = 0.85f;
	CG_FillRect( x, y, bw * lt_barShown, 2, col );
	// the vacuum: a short tag beside the bar
	if ( lt_vacTime && cg.time - lt_vacTime < 1600 ) {
		col[0] = 0.6f; col[1] = 0.85f; col[2] = 1; col[3] = CG_Clamp01( ( 1600 - ( cg.time - lt_vacTime ) ) / 400.0f )
			* ( 0.75f + 0.25f * sin( cg.time * 0.03f ) );
		CLoot_Text( x + bw + 6, y - 4, "VACUUM", col, 5, 7, -1, qtrue );
	}
}

// the corner block's chest row: returns the height it used (0 = no chests)
float CG_DS_DrawCornerChests( float x, float y, float w ) {
	int		i, n = CG_DS_ChestsBanked();
	vec4_t	col;
	float	cx;

	if ( n <= 0 ) {
		return 0;
	}
	cx = x;
	for ( i = 0 ; i < n && i < 8 ; i++ ) {
		Vector4Copy( lt_chestColors[lt_gr.chests[i] > 2 ? 2 : lt_gr.chests[i] < 0 ? 0 : lt_gr.chests[i]], col );
		col[3] = 0.9f;
		CG_FillRect( cx, y + 1, 7, 5, col );
		col[0] *= 0.5f; col[1] *= 0.5f; col[2] *= 0.5f;
		CG_FillRect( cx, y + 3, 7, 1, col );
		cx += 9;
	}
	col[0] = 0.9f; col[1] = 0.88f; col[2] = 0.8f; col[3] = 0.8f;
	CLoot_Text( x + w, y, va( "%i CHEST%s", n, n > 1 ? "S" : "" ), col, 4, 6, 1, qfalse );
	return 9;
}

/*
===============================================================================

CARDS: synergy tags, passive / weapon footers

===============================================================================
*/

typedef struct {
	int		a, b;
} lt_dsCombo_t;

// held card + offered card = a combo worth pointing out
static const lt_dsCombo_t lt_dsCombos[] = {
	{ RLC_PERK_FIRST + PERK_BLINK, RLC_PERK_FIRST + PERK_MIRAGE },
	{ RLC_SLIDE, RLC_PERK_FIRST + PERK_SLIPSTREAM },
	{ RLC_GRAPPLE, RLC_PERK_FIRST + PERK_MAGNETHOOK },
	{ RLC_DASH, RLC_PERK_FIRST + PERK_PYROMANIAC },
	{ RLC_DASH, RLC_PERK_FIRST + PERK_ADRENALINE },
	{ RLC_DOUBLEJUMP, RLC_PERK_FIRST + PERK_FEATHERWEIGHT },
	{ RLC_SLAM, RLC_PERK_FIRST + PERK_GROUNDZERO },
	{ RLC_CHARGE, RLC_PERK_FIRST + PERK_OVERCHARGER },
	{ RLC_PASSIVE_FIRST + RLP_HASTE, RLC_PERK_FIRST + PERK_KINETIC },
	{ RLC_PASSIVE_FIRST + RLP_REGEN, RLC_PERK_FIRST + PERK_BLOODTHIRST },
	{ RLC_PASSIVE_FIRST + RLP_IRONHIDE, RLC_PERK_FIRST + PERK_THORNS },
	{ RLC_PASSIVE_FIRST + RLP_HEART, RLC_PERK_FIRST + PERK_GLASSCANNON },
	{ RLC_PASSIVE_FIRST + RLP_AREA, RLC_PERK_FIRST + PERK_RICOCHET },
	{ RLC_PASSIVE_FIRST + RLP_AREA, RLC_PERK_FIRST + PERK_CHAINREACTION },
	{ RLC_PASSIVE_FIRST + RLP_MAGNET, RLC_PASSIVE_FIRST + RLP_GROWTH },
	{ RLC_PASSIVE_FIRST + RLP_LUCK, RLC_PASSIVE_FIRST + RLP_GREED },
	{ RLC_PASSIVE_FIRST + RLP_TEMPO, RLC_PASSIVE_FIRST + RLP_AMMOBELT },
	{ RLC_ALT_RAILGUN, RLC_PERK_FIRST + PERK_HEADHUNTER },
	{ RLC_ALT_LIGHTNING, RLC_PERK_FIRST + PERK_CONDUCTOR },
	{ RLC_SECRET_FIRST + RLS_BLOODCROWN, RLC_PASSIVE_FIRST + RLP_IRONHIDE }
};
#define DS_NUM_COMBOS	( (int)( sizeof( lt_dsCombos ) / sizeof( lt_dsCombos[0] ) ) )

// does the player hold this card (passives: any level)
static qboolean CLoot_Holds( int card ) {
	if ( card < 0 || card >= RLC_NUM ) {
		return qfalse;
	}
	if ( RLC_IS_PASSIVE( card ) ) {
		return lt_gr.pv[card - RLC_PASSIVE_FIRST] > 0;
	}
	return CG_RL_CardCount( card ) > 0;
}

static qboolean CLoot_Used( int w ) {
	return w > 0 && w < DS_NUM_WEAPONS && ( ( lt_gr.wu & ( 1 << w ) ) || lt_gr.wl[w] > 0 );
}

/*
=================
CG_DS_CardTag

"EVOLVES ROCKET LAUNCHER", "KEY FOR HELLFIRE", "COMBO: BLINK + MIRAGE" or NULL
=================
*/
// ds-cards: the same tag, and what the card snaps together with (the held card and / or the weapon)
const char *CG_DS_CardTagEx( int card, int *partner, int *weapon ) {
	static char	buf[64];
	int			e, w, i, other, dummyP, dummyW;

	if ( !partner ) {
		partner = &dummyP;
	}
	if ( !weapon ) {
		weapon = &dummyW;
	}
	*partner = -1;
	*weapon = 0;
	if ( !lt_gr.on || card < 0 || card >= RLC_NUM ) {
		return NULL;
	}
	// a key card for an evolution
	for ( e = 0 ; e < EVO_NUM ; e++ ) {
		if ( ds_evolutions[e].keyCard != card || ( lt_gr.ev & ( 1 << e ) ) ) {
			continue;
		}
		if ( e == EVO_ARMAGEDDON ) {
			*weapon = ds_evolutions[e].weapon;
			if ( ( lt_gr.ev & ( 1 << EVO_HELLFIRE ) ) && ( lt_gr.ev & ( 1 << EVO_CLUSTERBOMB ) ) ) {
				return "FUSES ARMAGEDDON";
			}
			if ( lt_gr.ev & ( ( 1 << EVO_HELLFIRE ) | ( 1 << EVO_CLUSTERBOMB ) ) ) {
				return "KEY FOR ARMAGEDDON";
			}
			continue;
		}
		if ( CLoot_Holds( card ) ) {
			continue;
		}
		w = ds_evolutions[e].weapon;
		*weapon = w;
		if ( CG_DS_WeaponLevel( w ) >= DS_MAX_WLEVEL ) {
			Com_sprintf( buf, sizeof( buf ), "EVOLVES %s", ds_weaponNames[w] );
			return buf;
		}
		if ( CLoot_Used( w ) ) {
			Com_sprintf( buf, sizeof( buf ), "KEY FOR %s", ds_evolutions[e].name );
			return buf;
		}
	}
	// a weapon level toward an evolution whose key you already hold
	if ( RLC_IS_WEAPON( card ) ) {
		w = RLC_WEAPON_OF( card );
		e = BG_DS_EvolutionForWeapon( w );
		if ( e >= 0 && !( lt_gr.ev & ( 1 << e ) ) && CLoot_Holds( ds_evolutions[e].keyCard ) ) {
			*weapon = w;
			*partner = ds_evolutions[e].keyCard;
			if ( CG_DS_WeaponLevel( w ) + 1 >= DS_MAX_WLEVEL ) {
				Com_sprintf( buf, sizeof( buf ), "EVOLVES %s", ds_weaponNames[w] );
			} else {
				Com_sprintf( buf, sizeof( buf ), "TOWARD %s", ds_evolutions[e].name );
			}
			return buf;
		}
	}
	// a combo with something held
	for ( i = 0 ; i < DS_NUM_COMBOS ; i++ ) {
		if ( lt_dsCombos[i].a == card ) {
			other = lt_dsCombos[i].b;
		} else if ( lt_dsCombos[i].b == card ) {
			other = lt_dsCombos[i].a;
		} else {
			continue;
		}
		if ( CLoot_Holds( other ) && !CLoot_Holds( card ) ) {
			*partner = other;
			*weapon = 0;
			Com_sprintf( buf, sizeof( buf ), "COMBO: %s + %s", bg_rlCardNames[other], bg_rlCardNames[card] );
			return buf;
		}
	}
	*partner = -1;
	*weapon = 0;
	return NULL;
}

const char *CG_DS_CardTag( int card ) {
	return CG_DS_CardTagEx( card, NULL, NULL );
}

// "SPITE 2 -> 3", "NEW  (3/6 SLOTS)", "LV 4 -> 5"; NULL for the other kinds
const char *CG_DS_CardFooter( int card ) {
	int		p, lv, slots;

	if ( card < 0 || card >= RLC_NUM ) {
		return NULL;
	}
	if ( RLC_IS_PASSIVE( card ) ) {
		p = card - RLC_PASSIVE_FIRST;
		lv = lt_gr.pv[p];
		if ( lv > 0 ) {
			return va( "%s %i -> %i", ds_passiveNames[p], lv, lv + 1 );
		}
		for ( slots = 0, p = 0 ; p < RLP_NUM ; p++ ) {
			if ( lt_gr.pv[p] > 0 ) {
				slots++;
			}
		}
		return va( "NEW  -  SLOT %i/%i", slots + 1, DS_PASSIVE_SLOTS );
	}
	if ( RLC_IS_WEAPON( card ) ) {
		lv = CG_DS_WeaponLevel( RLC_WEAPON_OF( card ) );
		return va( "LV %i -> %i", lv, lv + 1 );
	}
	if ( RLC_IS_SECRET( card ) ) {
		return "SECRET";
	}
	return NULL;
}

// weapon cards: what the next level adds
const char *CG_DS_CardDesc( int card ) {
	int		w, lv;

	if ( !RLC_IS_WEAPON( card ) ) {
		return NULL;
	}
	w = RLC_WEAPON_OF( card );
	lv = CG_DS_WeaponLevel( w ) + 1;
	if ( w <= 0 || w >= DS_NUM_WEAPONS || lv > DS_MAX_WLEVEL || !ds_weaponLevelDesc[w][lv] || !ds_weaponLevelDesc[w][lv][0] ) {
		return NULL;
	}
	return va( "%s: %s", ds_weaponNames[w], ds_weaponLevelDesc[w][lv] );
}

qhandle_t CG_DS_WeaponIcon( int w ) {
	return ( w > 0 && w < DS_NUM_WEAPONS ) ? lt_weaponIcon[w] : 0;
}

/*
===============================================================================

THE JACKPOT SCREEN (RLP_CHEST)

===============================================================================
*/

static int CLootJp_Active( int n, int reel ) {
	// which of the 5 windows run for n rewards, and the order they stop in
	if ( n >= 5 ) {
		return reel;
	}
	if ( n >= 3 ) {
		return reel >= 1 && reel <= 3 ? reel - 1 : -1;
	}
	return reel == 2 ? 0 : -1;
}

static int CLootJp_LastStop( int n ) {
	return JP_INTRO + JP_SPIN + ( n - 1 ) * JP_STAGGER;
}

static int CLootJp_HoldTime( int c ) {
	int		i;

	if ( lt_gr.jpN[c] >= 5 ) {
		return JP_HOLD_BIG;
	}
	for ( i = 0 ; i < lt_gr.jpN[c] ; i++ ) {
		if ( lt_gr.jpR[c][i] / 1000 == 1 ) {
			return JP_HOLD_BIG;
		}
	}
	return JP_HOLD;
}

// one symbol: a weapon icon, a passive chip, an evolution, skulls
static void CLootJp_Symbol( int code, float cx, float cy, float size, float squash, float alpha ) {
	int		type = code / 1000, id = ( code % 1000 ) / 10;
	vec4_t	col, dark;
	float	h = size * squash, cw;
	const char	*tag;

	if ( squash <= 0.05f || alpha <= 0 ) {
		return;
	}
	switch ( type ) {
	case 1:		// evolution: the weapon in a gold frame
		id = id >= 0 && id < EVO_NUM ? ds_evolutions[id].weapon : 0;
		Vector4Copy( lt_evoColor, col );
		col[3] = 0.55f * alpha;
		CG_FillRect( cx - size * 0.55f, cy - h * 0.55f, size * 1.1f, h * 1.1f, col );
		// fall through
	case 2:
		if ( lt_weaponIcon[id] ) {
			col[0] = col[1] = col[2] = 1; col[3] = alpha;
			trap_R_SetColor( col );
			CG_DrawPic( cx - size * 0.45f, cy - h * 0.45f, size * 0.9f, h * 0.9f, lt_weaponIcon[id] );
			trap_R_SetColor( NULL );
		} else {
			Vector4Copy( lt_weaponColor, col );
			col[3] = alpha;
			CLoot_Text( cx, cy - h * 0.25f, id > 0 && id < DS_NUM_WEAPONS ? bg_rlCardShort[RLC_FOR_WEAPON( id )] : "?", col,
				size * 0.25f, h * 0.45f, 0, qtrue );
		}
		break;
	case 3:		// passive chip
		Vector4Copy( lt_passiveColor, col );
		col[3] = 0.9f * alpha;
		CG_FillRect( cx - size * 0.42f, cy - h * 0.32f, size * 0.84f, h * 0.64f, col );
		dark[0] = dark[1] = dark[2] = 0.03f; dark[3] = alpha;
		tag = id >= 0 && id < RLP_NUM ? ds_passiveShort[id] : "?";
		cw = size * 0.22f;
		CLoot_Text( cx, cy - h * 0.2f, tag, dark, cw, h * 0.4f, 0, qfalse );
		break;
	default:	// skulls
		Vector4Copy( lt_evoColor, col );
		col[3] = alpha;
		CLoot_Text( cx, cy - h * 0.22f, va( "+%i", code % 1000 ), col, size * 0.2f, h * 0.42f, 0, qtrue );
		break;
	}
}

// symbols that roll past while a reel spins
static int CLootJp_FakeSymbol( int reel, int idx ) {
	static const int pool[] = { 2050, 3000, 2070, 3090, 2030, 3040, 2080, 4005, 2020, 3010, 2060, 3110, 2040, 3070, 2130, 3020 };
	unsigned	h = (unsigned)( idx * 2654435761u + reel * 40503u );

	return pool[( h >> 7 ) % ( sizeof( pool ) / sizeof( pool[0] ) )];
}

static const char *CLootJp_RewardName( int code, vec4_t col ) {
	int		type = code / 1000, id = ( code % 1000 ) / 10, lv = code % 10;

	switch ( type ) {
	case 1:
		Vector4Copy( lt_evoColor, col );
		return id >= 0 && id < EVO_NUM ? ds_evolutions[id].name : "EVOLUTION";
	case 2:
		Vector4Copy( lt_weaponColor, col );
		return id > 0 && id < DS_NUM_WEAPONS ? ds_weaponNames[id] : "";
	case 3:
		Vector4Copy( lt_passiveColor, col );
		return id >= 0 && id < RLP_NUM ? ds_passiveNames[id] : "";
	default:
		Vector4Copy( lt_evoColor, col );
		(void)lv;
		return "SKULLS";
	}
}

static const char *CLootJp_RewardSub( int code ) {
	int		type = code / 1000, lv = code % 10;

	switch ( type ) {
	case 1:		return "EVOLVED!";
	case 2:		return lv >= DS_MAX_WLEVEL ? "LV 7 -> 8  MAX" : va( "LV %i -> %i", lv - 1, lv );
	case 3:		return lv <= 1 ? "NEW PASSIVE" : va( "LV %i -> %i", lv - 1, lv );
	default:	return "NOTHING LEFT";
	}
}

static void CLootJp_Buttons( qboolean *pressed ) {
	usercmd_t	cmd;
	int			n = trap_GetCurrentCmdNumber();
	int			down, up;

	*pressed = qfalse;
	if ( n == lt_jpLastCmd || !trap_GetUserCmd( n, &cmd ) ) {
		return;
	}
	lt_jpLastCmd = n;
	down = cmd.buttons & ~lt_jpLastButtons;
	up = cmd.upmove > 0 && lt_jpLastUp <= 0;
	lt_jpLastButtons = cmd.buttons;
	lt_jpLastUp = cmd.upmove;
	*pressed = ( down & BUTTON_ATTACK ) || up;
}

static void CLootJp_NextChest( void ) {
	lt_jpChest++;
	lt_jpChestStart = cg.time;
	lt_jpStopped = 0;
	if ( lt_jpChest < lt_gr.numJP ) {
		trap_S_StartLocalSound( lt_sndOpen, CHAN_LOCAL_SOUND );
	}
}

/*
=================
CG_DS_DrawJackpot

From CG_RL_DrawIntermission while the server is in RLP_CHEST. The rewards are already
decided (CS_DESCENTGROW jp); this only shows them: reels spin and stop one by one.
Fire / jump skips (first press: stop the reels, second: next chest).
=================
*/
qboolean CG_DS_DrawJackpot( int phase ) {
	int			sig, i, k, n, t, stopT, act, idx, q, hold, lastStop;
	float		x0, cx, cy, p, frac, ang, sq, a, drop, rem, d, yoff, half, room;
	vec4_t		col, bg;
	qboolean	pressed, spinning;
	const char	*name, *key;

	if ( phase != RLP_CHEST_CG || !lt_gr.on || lt_gr.numJP <= 0 ) {
		lt_jpSig = 0;
		return qfalse;
	}
	sig = lt_gr.numJP * 7 + lt_gr.jpR[0][0] * 13 + lt_gr.jpN[0] * 101 + lt_gr.lt_gr;
	if ( sig != lt_jpSig ) {
		lt_jpSig = sig;
		lt_jpStart = cg.time;
		lt_jpChest = -1;
		lt_jpDoneSent = 0;
		CLootJp_NextChest();
	}
	CLootJp_Buttons( &pressed );

	if ( lt_jpChest >= lt_gr.numJP ) {
		if ( !lt_jpDoneSent ) {
			lt_jpDoneSent = 1;
			trap_SendClientCommand( "dschestdone" );
		}
		col[0] = col[1] = col[2] = 0.8f; col[3] = 0.6f + 0.4f * sin( cg.time * 0.006f );
		CLoot_Text( 320, 300, "...", col, 10, 14, 0, qtrue );
		return qtrue;
	}

	n = lt_gr.jpN[lt_jpChest];
	q = lt_gr.jpQ[lt_jpChest];
	t = cg.time - lt_jpChestStart;
	lastStop = CLootJp_LastStop( n );
	hold = CLootJp_HoldTime( lt_jpChest );
	if ( pressed && t > 350 ) {
		if ( t < lastStop ) {
			lt_jpChestStart = cg.time - lastStop;		// stop every reel now
			t = lastStop;
		} else {
			CLootJp_NextChest();
			return qtrue;
		}
	}
	if ( t >= lastStop + hold ) {
		CLootJp_NextChest();
		return qtrue;
	}

	// the plate drops in
	drop = 1.0f - CG_Clamp01( t / (float)JP_INTRO );
	drop = drop * drop * 40;
	x0 = 320 - ( 5 * JP_REEL_W + 4 * JP_REEL_GAP ) * 0.5f;
	cy = JP_Y - drop;

	bg[0] = 0.04f; bg[1] = 0.03f; bg[2] = 0.05f; bg[3] = 0.92f;
	CG_FillRect( x0 - 18, cy - 44, 5 * JP_REEL_W + 4 * JP_REEL_GAP + 36, JP_REEL_H + 108, bg );
	Vector4Copy( lt_chestColors[q], col );
	col[3] = 0.9f;
	CG_FillRect( x0 - 18, cy - 44, 5 * JP_REEL_W + 4 * JP_REEL_GAP + 36, 2, col );
	CG_FillRect( x0 - 18, cy + JP_REEL_H + 62, 5 * JP_REEL_W + 4 * JP_REEL_GAP + 36, 2, col );

	// title: the chest and how many
	CLoot_Text( 320, cy - 36, lt_chestNames[q], col, 10, 14, 0, qtrue );
	if ( lt_gr.numJP > 1 ) {
		col[0] = col[1] = col[2] = 0.7f; col[3] = 0.9f;
		CLoot_Text( x0 + 5 * JP_REEL_W + 4 * JP_REEL_GAP + 12, cy - 36, va( "%i / %i", lt_jpChest + 1, lt_gr.numJP ), col, 6, 9, 1, qfalse );
	}

	// marquee bulbs: they chase while it spins, all blaze on a jackpot
	spinning = t < lastStop;
	for ( i = 0 ; i < 24 ; i++ ) {
		float	bx = x0 - 12 + i * ( 5 * JP_REEL_W + 4 * JP_REEL_GAP + 24 ) / 23.0f;
		int		on;

		if ( spinning ) {
			on = ( ( i + cg.time / 70 ) % 4 ) == 0;
		} else if ( n >= 5 ) {
			on = ( ( cg.time / 120 ) & 1 ) ^ ( i & 1 );
		} else {
			on = ( i % 2 ) == 0;
		}
		Vector4Copy( on ? lt_evoColor : lt_chestColors[q], col );
		col[3] = on ? 0.95f : 0.25f;
		CG_FillRect( bx - 2, cy - 19, 4, 4, col );
		CG_FillRect( bx - 2, cy + JP_REEL_H + 6, 4, 4, col );
	}

	// spinning tick
	if ( spinning && t > JP_INTRO && cg.time >= lt_jpReelTick ) {
		lt_jpReelTick = cg.time + 75;
		trap_S_StartLocalSound( lt_sndReel, CHAN_AUTO );
	}

	for ( k = 0 ; k < 5 ; k++ ) {
		cx = x0 + k * ( JP_REEL_W + JP_REEL_GAP ) + JP_REEL_W * 0.5f;
		act = CLootJp_Active( n, k );
		bg[0] = 0.10f; bg[1] = 0.09f; bg[2] = 0.11f; bg[3] = act >= 0 ? 0.95f : 0.6f;
		CG_FillRect( cx - JP_REEL_W * 0.5f, cy, JP_REEL_W, JP_REEL_H, bg );
		if ( act < 0 ) {
			col[0] = col[1] = col[2] = 0.3f; col[3] = 0.8f;
			CLoot_Text( cx, cy + JP_REEL_H * 0.5f - 6, "-", col, 10, 12, 0, qfalse );
			continue;
		}
		stopT = JP_INTRO + JP_SPIN + act * JP_STAGGER;
		// drum position in symbols: fast, then easing into the reward
		d = (float)( stopT - t );
		if ( d > 300 ) {
			rem = 0.018f * ( d - 150 );
		} else if ( d > 0 ) {
			rem = 0.018f * d * d / 600.0f;
		} else {
			rem = -0.18f * sin( CG_Clamp01( -d / 260.0f ) * M_PI ) * ( 1.0f - CG_Clamp01( -d / 260.0f ) );
		}
		if ( t < JP_INTRO ) {
			rem = 0.018f * ( stopT - JP_INTRO - 150 );
		}
		p = 1000.0f - rem;
		idx = (int)floor( p );
		frac = p - idx;
		for ( i = -1 ; i <= 2 ; i++ ) {
			ang = ( i - frac ) * 0.95f;		// radians on the drum
			if ( ang < -1.45f || ang > 1.45f ) {
				continue;
			}
			sq = cos( ang );
			a = sq * sq;
			// keep the symbol inside its window: squash it further near the drum's edges
			yoff = sin( ang ) * JP_REEL_H * 0.4f;
			half = JP_REEL_W * 0.62f * sq * 0.5f;
			room = JP_REEL_H * 0.5f - 4 - ( yoff < 0 ? -yoff : yoff );
			if ( room <= 3 ) {
				continue;
			}
			if ( half > room ) {
				sq *= room / half;
			}
			CLootJp_Symbol( ( idx + i ) == 1000 && d <= 300 ? lt_gr.jpR[lt_jpChest][act] : CLootJp_FakeSymbol( k, idx + i ),
				cx, cy + JP_REEL_H * 0.5f + yoff, JP_REEL_W * 0.62f, sq, a );
		}
		// shade the drum's top and bottom
		bg[0] = bg[1] = bg[2] = 0.02f; bg[3] = 0.55f;
		CG_FillRect( cx - JP_REEL_W * 0.5f, cy, JP_REEL_W, 8, bg );
		CG_FillRect( cx - JP_REEL_W * 0.5f, cy + JP_REEL_H - 8, JP_REEL_W, 8, bg );

		// stopped: a thunk, the reward's name under the window
		if ( t >= stopT ) {
			if ( lt_jpStopped <= act ) {
				lt_jpStopped = act + 1;
				trap_S_StartLocalSound( lt_gr.jpR[lt_jpChest][act] / 1000 == 1 ? lt_sndEvolve : lt_sndStop, CHAN_AUTO );
				if ( act == n - 1 && n >= 5 ) {
					trap_S_StartLocalSound( lt_sndJackpot, CHAN_ANNOUNCER );
				}
			}
			a = CG_Clamp01( ( t - stopT ) / 200.0f );
			name = CLootJp_RewardName( lt_gr.jpR[lt_jpChest][act], col );
			col[3] = a;
			// a flash on the window as it locks
			if ( t - stopT < 250 ) {
				bg[0] = col[0]; bg[1] = col[1]; bg[2] = col[2]; bg[3] = 0.35f * ( 1.0f - ( t - stopT ) / 250.0f );
				CG_FillRect( cx - JP_REEL_W * 0.5f, cy, JP_REEL_W, JP_REEL_H, bg );
			}
			CG_FillRect( cx - JP_REEL_W * 0.5f, cy + JP_REEL_H, JP_REEL_W, 2, col );
			CLoot_Text( cx, cy + JP_REEL_H + 18, name, col, CG_DrawStrlen( name ) > 13 ? 4.5f : 5.0f, 8, 0, qtrue );
			col[0] = col[1] = col[2] = 0.85f;
			CLoot_Text( cx, cy + JP_REEL_H + 30, CLootJp_RewardSub( lt_gr.jpR[lt_jpChest][act] ), col, 4.5f, 7, 0, qfalse );
		}
	}

	// JACKPOT / EVOLUTION banner once everything stopped
	if ( t >= lastStop ) {
		a = CG_Clamp01( ( t - lastStop ) / 250.0f );
		sq = 1.0f + 0.4f * ( 1.0f - a );
		name = NULL;
		for ( i = 0 ; i < n ; i++ ) {
			if ( lt_gr.jpR[lt_jpChest][i] / 1000 == 1 ) {
				name = "EVOLUTION!";
			}
		}
		if ( n >= 5 ) {
			name = "JACKPOT!";
		}
		if ( name ) {
			Vector4Copy( lt_evoColor, col );
			col[3] = a * ( 0.8f + 0.2f * sin( cg.time * 0.02f ) );
			CLoot_Text( 320, cy + JP_REEL_H + 44 - 6 * sq, name, col, 12 * sq, 16 * sq, 0, qtrue );
		}
	}

	// hint
	key = CG_KeyForCommand( "+attack" );
	col[0] = col[1] = col[2] = 0.7f; col[3] = 0.8f;
	CLoot_Text( 320, cy + JP_REEL_H + 70, va( "[%s] %s", key ? key : "FIRE",
		t < lastStop ? "SKIP" : lt_jpChest + 1 < lt_gr.numJP ? "NEXT CHEST" : "CONTINUE" ), col, 5, 7, 0, qfalse );
	return qtrue;
}
