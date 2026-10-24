/*
===========================================================================
REARMED -- DESCENT round 3 CARDS (ds-cards), cgame side.

- CARD RARITY REVEAL (cg_dsReveal 1): the offer deals face-down (the back's colour and
  frame tell the rarity), then the cards flip one by one; rare cards shimmer, epics sting,
  a legendary charges up, flips slowly with a roar / choir hit, a gold light sweep, a slow
  pulse and a flare at the screen edges. Fire / Space / a pick key during the reveal flips
  them all at once. cg_dsReveal 0: the cards are just there.
- SYNERGY DING: a pick that completes a combo / an evolution key snaps the two icons
  together with a chime and a burst of light at the side. In play, an evolution that just
  became ready shows "HELLFIRE READY: OPEN A CHEST" small at the right edge.
- FLOOR-END TALLY (RLP_END of a won floor): kills, headshots, gibs, best chain, gems,
  damage, style, skulls (with where they came from) roll up like a slot machine with
  ticks and a stamp each, NEW RECORD stamps; fire / Space skips. dsctallydone tells the
  server to go on to the chests / cards.
- LEGENDARY effects (EV_DS_CARDS), MAGNETAR orbs, the DOUBLE AGENT's shell, a small
  legendary row under the run HUD (dsclg).
Game side: game/g_ds_cards.c.
===========================================================================
*/
#include "cg_local.h"

#define RLP_END_CG		1		// g_roguelike.c RLP_END
#define RLP_OFFER_CG	3

// the reveal
#define RV_DEAL_IN		300		// a card slides in face-down
#define RV_DEAL_STAG	90
#define RV_FIRST_FLIP	140		// after the deal
#define RV_FLIP			200
#define RV_FLIP_LEG		520		// the legendary turns slowly
#define RV_GAP			55
#define RV_LEG_CHARGE	700		// its back glows and shakes first
#define RV_PULSE		900		// the slow-mo pulse after it opened
#define RV_FLARE		1600	// the screen-edge flare

// the ding
#define DING_SNAP		360
#define DING_TIME		1700

// the tally
#define TL_FIRST		300
#define TL_STEP			360
#define TL_ROLL			300
#define TL_LINES		8
#define TL_END			( TL_FIRST + ( TL_LINES - 1 ) * TL_STEP + TL_ROLL )
#define TL_HOLD			1350

#define LGC_ORB_RADIUS	64.0f
#define LGC_ORB_SPEED	0.0032f		// = g_ds_cards.c

static vmCvar_t		cg_dsReveal;

static sfxHandle_t	lgc_sFlip, lgc_sRare, lgc_sEpic, lgc_sCharge, lgc_sLegend, lgc_sSynergy, lgc_sSnap, lgc_sEvoReady;
static sfxHandle_t	lgc_sTick, lgc_sStamp, lgc_sRecord;
static sfxHandle_t	lgc_sFx[LGX_CHAIN + 1];
static qhandle_t	lgc_glow, lgc_agentShell, lgc_gem[3];

static const vec4_t	lgc_rarCol[4] = {
	{ 0.62f, 0.60f, 0.58f, 1 },		// common
	{ 0.30f, 0.62f, 1.00f, 1 },		// rare
	{ 0.75f, 0.38f, 1.00f, 1 },		// epic
	{ 1.00f, 0.74f, 0.16f, 1 }		// legendary
};
static const vec4_t	lgc_rarBack[4] = {
	{ 0.075f, 0.072f, 0.080f, 1 },
	{ 0.035f, 0.070f, 0.170f, 1 },
	{ 0.110f, 0.035f, 0.170f, 1 },
	{ 0.200f, 0.115f, 0.020f, 1 }
};
static const char	*lgc_styleLetters[8] = { "-", "D", "C", "B", "A", "S", "SS", "SSS" };

// reveal state (per offer, keyed by its start time)
static int			rv_start = -1, rv_skipAt, rv_played, rv_soundPlayed, rv_chargePlayed;
static int			rv_flipStart[8], rv_flipDur[8], rv_end, rv_n, rv_offer[8];

// ding
static int			dg_time, dg_card, dg_partner, dg_weapon, dg_snapped;
static char			dg_tag[64];
static float		dg_x, dg_y;

// tally: kills hs gibs chain gems dmg style skulls, mask, floor
static int			tl_v[10], tl_valid, tl_start, tl_done, tl_skipT, tl_preview, tl_stamped, tl_tickNext, tl_lastCmd, tl_lastButtons, tl_lastUp;
static int			tl_lastPress;

// legendary HUD (dsclg)
static int			lg_held, lg_mirror, lg_phoenix, lg_agentCl = -1, lg_agentEnd, lg_agentReadyAt, lg_orbs, lg_blackmass, lg_hudOn;
static int			lg_timeFlash, lg_timeAmount;
static int			lg_flash, lg_flashKind;

// evolution ready
static int			ev_mask, ev_init, ev_shown, ev_time, ev_evo;

/*
===============================================================================

SETUP, HELPERS

===============================================================================
*/

void CG_DSC_Register( void ) {
	static const char	*fx[LGX_CHAIN + 1] = { NULL, "lg_echo", "lg_vampire", "lg_time", "lg_blood", "lg_mirror", "lg_phoenix",
		"lg_orb", "lg_agent", "lg_grave", "lg_blackmass", "lg_deadeye", "lg_chain" };
	int		i;

	trap_Cvar_Register( &cg_dsReveal, "cg_dsReveal", "1", CVAR_ARCHIVE );
	lgc_sFlip = trap_S_RegisterSound( "sound/rearmed/cards/flip.wav", qfalse );
	lgc_sRare = trap_S_RegisterSound( "sound/rearmed/cards/rare.wav", qfalse );
	lgc_sEpic = trap_S_RegisterSound( "sound/rearmed/cards/epic.wav", qfalse );
	lgc_sCharge = trap_S_RegisterSound( "sound/rearmed/cards/leg_charge.wav", qfalse );
	lgc_sLegend = trap_S_RegisterSound( "sound/rearmed/cards/legendary.wav", qfalse );
	lgc_sSynergy = trap_S_RegisterSound( "sound/rearmed/cards/synergy.wav", qfalse );
	lgc_sSnap = trap_S_RegisterSound( "sound/rearmed/cards/snap.wav", qfalse );
	lgc_sEvoReady = trap_S_RegisterSound( "sound/rearmed/cards/evoready.wav", qfalse );
	lgc_sTick = trap_S_RegisterSound( "sound/rearmed/cards/tick.wav", qfalse );
	lgc_sStamp = trap_S_RegisterSound( "sound/rearmed/cards/stamp.wav", qfalse );
	lgc_sRecord = trap_S_RegisterSound( "sound/rearmed/cards/record.wav", qfalse );
	for ( i = 1 ; i <= LGX_CHAIN ; i++ ) {
		lgc_sFx[i] = trap_S_RegisterSound( va( "sound/rearmed/cards/%s.wav", fx[i] ), qfalse );
	}
	lgc_glow = trap_R_RegisterShader( "rearmed/glow" );
	lgc_agentShell = trap_R_RegisterShader( "rearmed/lgAgentShell" );
	for ( i = 0 ; i < 3 ; i++ ) {
		lgc_gem[i] = trap_R_RegisterModel( va( "models/rearmed/loot/gem%i.md3", i ) );
	}
	rv_start = -1;
	tl_valid = 0;
	tl_preview = 0;
	dg_time = 0;
	lg_agentCl = -1;
	lg_hudOn = 0;
	lg_held = 0;
	ev_init = 0;
	ev_time = 0;
}

static int LGC_Rarity( int card ) {
	int		r;

	if ( card < 0 || card >= RLC_NUM ) {
		return 0;
	}
	r = bg_rlCardRarity[card];
	return r < 0 ? 0 : r > 3 ? 3 : r;
}

static void LGC_Text( float x, float y, const char *s, const float *color, float cw, float ch, int align, qboolean shadow ) {
	float	w = CG_DrawStrlen( s ) * (int)( cw + 0.5f );		// the width CG_DrawStringExt really draws

	if ( align == 0 ) {
		x -= w * 0.5f;
	} else if ( align > 0 ) {
		x -= w;
	}
	CG_DrawStringExt( (int)x, (int)y, s, color, qtrue, shadow, (int)( cw + 0.5f ), (int)( ch + 0.5f ), 0 );
}

static void LGC_Glow( float cx, float cy, float w, float h, const float *rgb, float a ) {
	vec4_t	c;

	if ( a <= 0.001f ) {
		return;
	}
	c[0] = rgb[0] * a; c[1] = rgb[1] * a; c[2] = rgb[2] * a; c[3] = 1;
	trap_R_SetColor( c );
	CG_DrawPic( cx - w * 0.5f, cy - h * 0.5f, w, h, lgc_glow );
	trap_R_SetColor( NULL );
}

static void LGC_Frame( float x, float y, float w, float h, float t, const float *col ) {
	CG_FillRect( x, y, w, t, col );
	CG_FillRect( x, y + h - t, w, t, col );
	CG_FillRect( x, y + t, t, h - 2 * t, col );
	CG_FillRect( x + w - t, y + t, t, h - 2 * t, col );
}

static float LGC_EaseOutBack( float t ) {
	float	s = 1.70158f;

	t = CG_Clamp01( t ) - 1.0f;
	return t * t * ( ( s + 1 ) * t + s ) + 1.0f;
}

static float LGC_ScreenLeft( void ) {
	return cgs.screenXScale > 0 ? -cgs.screenXBias / cgs.screenXScale : 0;
}

// a diagonal light band across a box (clipped to it): pos 0..1 crosses it
static void LGC_Sweep( float x, float y, float w, float h, float pos, const float *rgb, float a ) {
	int		i, strips = 18;
	float	sy, sh = h / strips, bx, bw, x0, x1, k;
	vec4_t	c;

	if ( a <= 0.01f ) {
		return;
	}
	bw = w * 0.22f;
	for ( i = 0 ; i < strips ; i++ ) {
		sy = y + i * sh;
		// the band leans: lower strips lag behind
		bx = x - bw - h * 0.5f + ( w + bw + h * 0.5f ) * pos + ( strips - i ) * sh * 0.5f - h * 0.25f;
		for ( k = 0 ; k < 3 ; k++ ) {
			x0 = bx + k * bw * 0.18f;
			x1 = bx + bw - k * bw * 0.18f;
			if ( x0 < x ) {
				x0 = x;
			}
			if ( x1 > x + w ) {
				x1 = x + w;
			}
			if ( x1 <= x0 ) {
				continue;
			}
			c[0] = rgb[0]; c[1] = rgb[1]; c[2] = rgb[2]; c[3] = a * ( 0.12f + 0.16f * k );
			CG_FillRect( x0, sy, x1 - x0, sh + 0.5f, c );
		}
	}
}

/*
===============================================================================

THE REVEAL

===============================================================================
*/

qboolean CG_DSC_RevealOn( void ) {
	static int	updated;

	if ( updated != cg.time ) {		// (F10 changes apply at once)
		updated = cg.time;
		trap_Cvar_Update( &cg_dsReveal );
	}
	return cg_dsReveal.integer != 0;
}

// the flip schedule of an offer (legendaries charge up first and turn slowly)
static void LGC_Schedule( int start, int n, const int *offer ) {
	int		i, t;

	if ( n > 8 ) {
		n = 8;
	}
	if ( start != rv_start ) {
		rv_start = start;
		rv_skipAt = 0;
		rv_played = 0;
		rv_soundPlayed = 0;
		rv_chargePlayed = 0;
	}
	rv_n = n;
	for ( i = 0 ; i < n ; i++ ) {
		rv_offer[i] = offer[i];
	}
	t = RV_DEAL_IN + ( n - 1 ) * RV_DEAL_STAG + RV_FIRST_FLIP;
	for ( i = 0 ; i < n ; i++ ) {
		if ( LGC_Rarity( offer[i] ) == RLRAR_LEGEND ) {
			t += RV_LEG_CHARGE;
			rv_flipDur[i] = RV_FLIP_LEG;
		} else {
			rv_flipDur[i] = RV_FLIP;
		}
		rv_flipStart[i] = t;
		t += rv_flipDur[i] + RV_GAP;
	}
	rv_end = n > 0 ? rv_flipStart[n - 1] + rv_flipDur[n - 1] : 0;
	if ( rv_skipAt > 0 ) {
		for ( i = 0 ; i < n ; i++ ) {
			if ( rv_flipStart[i] + rv_flipDur[i] > rv_skipAt ) {
				rv_flipStart[i] = rv_skipAt - rv_flipDur[i];
			}
		}
		if ( rv_end > rv_skipAt ) {
			rv_end = rv_skipAt;
		}
	}
}

int CG_DSC_RevealLeft( int start, int n, const int *offer ) {
	if ( !CG_DSC_RevealOn() ) {
		return -( cg.time - start );
	}
	LGC_Schedule( start, n, offer );
	return rv_end - ( cg.time - start );
}

void CG_DSC_RevealSkip( int start ) {
	int		i, best = 0;

	if ( start != rv_start || rv_skipAt ) {
		return;
	}
	rv_skipAt = cg.time - start;
	if ( rv_skipAt < RV_DEAL_IN ) {
		rv_skipAt = RV_DEAL_IN;
	}
	// one flip and the best rarity's sound for everything that hadn't turned yet
	for ( i = 0 ; i < rv_n ; i++ ) {
		if ( !( rv_soundPlayed & ( 1 << i ) ) ) {
			rv_soundPlayed |= 1 << i;
			rv_played |= 1 << i;
			rv_chargePlayed |= 1 << i;
			if ( LGC_Rarity( rv_offer[i] ) > best ) {
				best = LGC_Rarity( rv_offer[i] );
			}
		}
	}
	trap_S_StartLocalSound( lgc_sFlip, CHAN_LOCAL_SOUND );
	if ( best == RLRAR_LEGEND ) {
		trap_S_StartLocalSound( lgc_sLegend, CHAN_ANNOUNCER );
	} else if ( best == 2 ) {
		trap_S_StartLocalSound( lgc_sEpic, CHAN_AUTO );
	} else if ( best == 1 ) {
		trap_S_StartLocalSound( lgc_sRare, CHAN_AUTO );
	}
}

/*
=================
CG_DSC_CardAnim

One card of the offer this frame: qfalse = not dealt yet. flip: 0 = the back flat on,
0.5 = edge on, 1 = the face (CG_RL_DrawCard's convention).
=================
*/
qboolean CG_DSC_CardAnim( int i, int start, int n, const int *offer, float *dy, float *alpha, float *flip, float *scale, float *flash ) {
	int		t, td, tf, tp, r, dur;
	float	e, k;

	LGC_Schedule( start, n, offer );
	if ( i < 0 || i >= rv_n ) {
		return qfalse;
	}
	t = cg.time - start;
	td = t - i * RV_DEAL_STAG;
	if ( rv_skipAt && td < RV_DEAL_IN && t >= rv_skipAt ) {
		td = RV_DEAL_IN;
	}
	if ( td <= 0 ) {
		return qfalse;
	}
	r = LGC_Rarity( offer[i] );
	e = LGC_EaseOutBack( (float)td / RV_DEAL_IN );
	*dy = 160 * ( 1.0f - e );
	*alpha = CG_Clamp01( td / 120.0f );
	*scale = 1.0f;
	*flash = 0;
	dur = rv_flipDur[i];
	tf = t - rv_flipStart[i];
	if ( tf < 0 ) {
		*flip = 0;
		// a legendary charges: it shakes and swells, the rest of the screen waits
		if ( r == RLRAR_LEGEND && tf > -RV_LEG_CHARGE ) {
			k = 1.0f - (float)( -tf ) / RV_LEG_CHARGE;
			*dy += sin( cg.time * 0.11f ) * 2.5f * k;
			*scale = 1.0f + 0.07f * k;
			if ( !( rv_chargePlayed & ( 1 << i ) ) ) {
				rv_chargePlayed |= 1 << i;
				trap_S_StartLocalSound( lgc_sCharge, CHAN_LOCAL_SOUND );
			}
		}
		return qtrue;
	}
	if ( !( rv_played & ( 1 << i ) ) ) {
		rv_played |= 1 << i;
		trap_S_StartLocalSound( lgc_sFlip, CHAN_LOCAL_SOUND );
	}
	if ( tf < dur ) {
		*flip = (float)tf / dur;
		if ( r == RLRAR_LEGEND ) {
			*scale = 1.07f;
		}
		return qtrue;
	}
	*flip = 1.0f;
	if ( !( rv_soundPlayed & ( 1 << i ) ) ) {
		rv_soundPlayed |= 1 << i;
		if ( r == 1 ) {
			trap_S_StartLocalSound( lgc_sRare, CHAN_AUTO );
		} else if ( r == 2 ) {
			trap_S_StartLocalSound( lgc_sEpic, CHAN_AUTO );
		} else if ( r == RLRAR_LEGEND ) {
			trap_S_StartLocalSound( lgc_sLegend, CHAN_ANNOUNCER );
		}
	}
	tp = tf - dur;
	switch ( r ) {
	case RLRAR_LEGEND:
		if ( tp < RV_PULSE ) {
			*scale = 1.0f + 0.14f * sin( M_PI * (float)tp / RV_PULSE );		// the slow-mo pulse
		}
		*flash = tp < 350 ? 0.9f * ( 1.0f - tp / 350.0f ) : 0;
		break;
	case 2:
		if ( tp < 260 ) {
			*scale = 1.0f + 0.07f * sin( M_PI * tp / 260.0f );
		}
		*flash = tp < 220 ? 0.55f * ( 1.0f - tp / 220.0f ) : 0;
		break;
	case 1:
		*flash = tp < 180 ? 0.35f * ( 1.0f - tp / 180.0f ) : 0;
		break;
	default:
		*flash = tp < 120 ? 0.18f * ( 1.0f - tp / 120.0f ) : 0;
		break;
	}
	return qtrue;
}

/*
=================
CG_DSC_CardBack

The face-down card: the back colour and frame tell the rarity
=================
*/
qboolean CG_DSC_CardBack( int card, float x, float y, float w, float h, float sx, float alpha ) {
	vec4_t	c;
	int		r = LGC_Rarity( card ), i;
	float	cx = x + w * 0.5f, cy = y + h * 0.5f, pulse;

	if ( !CG_DSC_RevealOn() ) {
		return qfalse;
	}
	pulse = 0.75f + 0.25f * sin( cg.time * ( r == RLRAR_LEGEND ? 0.012f : 0.006f ) );
	if ( r >= 2 ) {
		LGC_Glow( cx, cy, w * 1.9f, h * 1.5f, lgc_rarCol[r], ( r == RLRAR_LEGEND ? 0.55f : 0.3f ) * pulse * alpha );
	}
	Vector4Copy( lgc_rarBack[r], c );
	c[3] = 0.97f * alpha;
	CG_FillRect( x, y, w, h, c );
	// a lattice of the rarity colour
	Vector4Copy( lgc_rarCol[r], c );
	for ( i = 1 ; i < 6 ; i++ ) {
		c[3] = 0.10f * alpha;
		CG_FillRect( x + w * i / 6.0f, y + 6, 1, h - 12, c );
		CG_FillRect( x + 6 * sx, y + h * i / 6.0f, w - 12 * sx, 1, c );
	}
	c[3] = ( r == 0 ? 0.55f : 0.9f ) * alpha;
	LGC_Frame( x, y, w, h, r == RLRAR_LEGEND ? 3 : 2, c );
	c[3] = 0.35f * alpha;
	LGC_Frame( x + 6 * sx, y + 6, w - 12 * sx, h - 12, 1, c );
	// the emblem
	c[3] = ( 0.45f + 0.4f * ( r > 0 ) ) * alpha * pulse;
	CG_FillRect( cx - w * 0.18f, cy - h * 0.13f, w * 0.36f, h * 0.26f, c );
	Vector4Copy( lgc_rarBack[r], c );
	c[3] = alpha;
	LGC_Text( cx, cy - 15, "D", c, 22 * sx, 30, 0, qfalse );
	if ( r == RLRAR_LEGEND ) {
		LGC_Sweep( x, y, w, h, ( ( cg.time % 900 ) / 900.0f ), lgc_rarCol[r], 0.8f * alpha );
	} else if ( r >= 1 ) {
		LGC_Sweep( x, y, w, h, ( ( cg.time % 2200 ) / 1500.0f ), lgc_rarCol[r], 0.45f * alpha );
	}
	return qtrue;
}

/*
=================
CG_DSC_CardFace

The open card: a rarity frame, a shimmer for rare+, the gold sweep and halo of a legendary
=================
*/
void CG_DSC_CardFace( int slot, int card, float x, float y, float w, float h, float alpha ) {
	int		r = LGC_Rarity( card ), period;
	vec4_t	c;
	float	pulse = 0.7f + 0.3f * sin( cg.time * 0.008f + slot ), pos;

	Vector4Copy( lgc_rarCol[r], c );
	if ( r == RLRAR_LEGEND ) {
		LGC_Glow( x + w * 0.5f, y + h * 0.5f, w * 1.8f, h * 1.45f, lgc_rarCol[r], 0.35f * pulse * alpha );
	}
	c[3] = ( r == 0 ? 0.35f : 0.85f ) * alpha;
	LGC_Frame( x - 2, y - 2, w + 4, h + 4, r == RLRAR_LEGEND ? 3 : 2, c );
	if ( r == 0 ) {
		return;
	}
	period = r == RLRAR_LEGEND ? 1700 : r == 2 ? 2300 : 3100;
	pos = ( ( cg.time + slot * 577 ) % period ) / 900.0f;
	if ( pos <= 1.2f ) {
		LGC_Sweep( x, y, w, h, pos, r == RLRAR_LEGEND ? lgc_rarCol[3] : colorWhite, ( r == RLRAR_LEGEND ? 0.9f : r == 2 ? 0.5f : 0.35f ) * alpha );
	}
}

/*
=================
CG_DSC_DrawOfferFX

Over the cards: the legendary's charge dims the screen, its opening flashes gold, flares
at both screen edges and says LEGENDARY
=================
*/
void CG_DSC_DrawOfferFX( int start, int n, const int *offer ) {
	int		i, t, tf, tp;
	float	k, left, right, a, s, cs, cx;
	vec4_t	c;

	if ( !CG_DSC_RevealOn() || n <= 0 ) {
		return;
	}
	LGC_Schedule( start, n, offer );
	t = cg.time - start;
	left = LGC_ScreenLeft();
	right = SCREEN_WIDTH - left;
	for ( i = 0 ; i < rv_n ; i++ ) {
		if ( LGC_Rarity( offer[i] ) != RLRAR_LEGEND ) {
			continue;
		}
		tf = t - rv_flipStart[i];
		if ( tf < 0 && tf > -RV_LEG_CHARGE ) {
			k = 1.0f - (float)( -tf ) / RV_LEG_CHARGE;
			c[0] = c[1] = c[2] = 0; c[3] = 0.3f * k;
			CG_FillRect( -2000, 0, 4640, 480, c );
			continue;
		}
		tp = tf - rv_flipDur[i];
		if ( tp < 0 || tp > RV_FLARE ) {
			continue;
		}
		a = 1.0f - (float)tp / RV_FLARE;
		// a gold flash over everything
		if ( tp < 320 ) {
			c[0] = 1; c[1] = 0.8f; c[2] = 0.35f; c[3] = 0.28f * ( 1.0f - tp / 320.0f );
			CG_FillRect( -2000, 0, 4640, 480, c );
		}
		// flares at the screen edges
		LGC_Glow( left, 240, 260, 820, lgc_rarCol[3], 0.9f * a );
		LGC_Glow( right, 240, 260, 820, lgc_rarCol[3], 0.9f * a );
		LGC_Glow( left, 240, 90, 600, colorWhite, 0.5f * a * a );
		LGC_Glow( right, 240, 90, 600, colorWhite, 0.5f * a * a );
		// LEGENDARY! under the card and its key hints
		if ( tp < 1400 ) {
			cs = n > 4 ? 0.86f : 1.0f;
			cx = 320 - ( n * 140 + ( n - 1 ) * 16 ) * cs * 0.5f + ( 70 + i * 156 ) * cs;
			s = 1.0f + 0.9f * ( 1.0f - LGC_EaseOutBack( tp / 300.0f ) );
			c[0] = c[1] = c[2] = 0; c[3] = 0.7f * ( tp > 1000 ? ( 1400 - tp ) / 400.0f : 1.0f );
			CG_FillRect( cx - 90, 394, 180, 30, c );
			Vector4Copy( lgc_rarCol[3], c );
			c[3] = tp > 1000 ? ( 1400 - tp ) / 400.0f : 1.0f;
			LGC_Text( cx, 400 - 8 * s, "LEGENDARY!", c, 14 * s, 18 * s, 0, qtrue );
		}
	}
}

/*
===============================================================================

THE SYNERGY DING

===============================================================================
*/

void CG_DSC_OnPick( int card, float cx, float cy ) {
	const char	*tag;
	int			partner, weapon;

	tag = CG_DS_CardTagEx( card, &partner, &weapon );
	if ( !tag || !Q_strncmp( tag, "TOWARD", 6 ) ) {
		return;
	}
	Q_strncpyz( dg_tag, tag, sizeof( dg_tag ) );
	dg_time = cg.time;
	dg_card = card;
	dg_partner = partner;
	dg_weapon = weapon;
	dg_snapped = 0;
	dg_x = cx;
	dg_y = cy;
	CG_DevPrintf( "Synergy: %s\n", tag );
}

// one icon of the ding: a card chip or a weapon icon
static void LGC_DingIcon( float cx, float cy, float size, int card, int weapon, float a ) {
	vec4_t		c, dark;
	const char	*tag;
	int			k;

	if ( card < 0 && weapon > 0 && CG_DS_WeaponIcon( weapon ) ) {
		c[0] = c[1] = c[2] = 0.06f; c[3] = 0.9f * a;
		CG_FillRect( cx - size * 0.5f, cy - size * 0.5f, size, size, c );
		Vector4Copy( lgc_rarCol[3], c );
		c[3] = a;
		LGC_Frame( cx - size * 0.5f, cy - size * 0.5f, size, size, 2, c );
		c[0] = c[1] = c[2] = 1;
		trap_R_SetColor( c );
		CG_DrawPic( cx - size * 0.4f, cy - size * 0.4f, size * 0.8f, size * 0.8f, CG_DS_WeaponIcon( weapon ) );
		trap_R_SetColor( NULL );
		return;
	}
	if ( card < 0 || card >= RLC_NUM ) {
		return;
	}
	k = bg_rlCardKind[card];
	tag = bg_rlCardShort[card][0] ? bg_rlCardShort[card] : "?";
	Vector4Copy( lgc_rarCol[LGC_Rarity( card )], c );
	c[0] = 0.25f + 0.75f * c[0]; c[3] = 0.95f * a;
	CG_FillRect( cx - size * 0.5f, cy - size * 0.5f, size, size, c );
	dark[0] = dark[1] = dark[2] = 0.04f; dark[3] = a;
	LGC_Frame( cx - size * 0.5f, cy - size * 0.5f, size, size, 2, dark );
	LGC_Text( cx, cy - size * 0.2f, k == RLK_PERK ? "P" : tag, dark, size * 0.26f, size * 0.4f, 0, qfalse );
}

void CG_DSC_DrawDing( void ) {
	int			t;
	float		k, gap, a, x, y, s, edge;
	vec4_t		c;
	const char	*head;
	qboolean	evo;

	if ( !dg_time || cg.time - dg_time > DING_TIME || cg.time < dg_time ) {
		return;
	}
	t = cg.time - dg_time;
	evo = !Q_strncmp( dg_tag, "EVOLVES", 7 ) || !Q_strncmp( dg_tag, "FUSES", 5 ) || !Q_strncmp( dg_tag, "KEY FOR", 7 );
	head = !Q_strncmp( dg_tag, "COMBO", 5 ) ? "COMBO!" : !Q_strncmp( dg_tag, "KEY FOR", 7 ) ? "EVOLUTION KEY!" : "EVOLUTION READY!";
	// right of the picked card (it moves to the middle)
	x = 320 + 150;
	y = dg_y;
	a = t > DING_TIME - 300 ? ( DING_TIME - t ) / 300.0f : 1.0f;
	if ( t < DING_SNAP ) {
		k = (float)t / DING_SNAP;
		gap = 90 * ( 1.0f - k * k );		// they accelerate into each other
	} else {
		gap = 0;
		if ( !dg_snapped ) {
			dg_snapped = 1;
			trap_S_StartLocalSound( lgc_sSnap, CHAN_LOCAL_SOUND );
			trap_S_StartLocalSound( lgc_sSynergy, CHAN_ANNOUNCER );
		}
	}
	// the burst of light at the side
	if ( t >= DING_SNAP ) {
		k = CG_Clamp01( ( t - DING_SNAP ) / 500.0f );
		edge = SCREEN_WIDTH - LGC_ScreenLeft();
		LGC_Glow( x, y, 60 + 220 * k, 60 + 220 * k, evo ? lgc_rarCol[3] : lgc_rarCol[1], 0.9f * ( 1.0f - k ) * a );
		LGC_Glow( edge, y, 200, 500, evo ? lgc_rarCol[3] : lgc_rarCol[1], 0.75f * ( 1.0f - k ) * a );
		c[0] = c[1] = c[2] = 1; c[3] = 0.6f * ( 1.0f - CG_Clamp01( ( t - DING_SNAP ) / 160.0f ) ) * a;
		CG_FillRect( x - 40, y - 26, 80, 52, c );
	} else {
		LGC_Glow( x, y, 120, 80, evo ? lgc_rarCol[3] : lgc_rarCol[1], 0.3f * ( t / (float)DING_SNAP ) );
	}
	s = 40 * ( t >= DING_SNAP && t < DING_SNAP + 160 ? 1.0f + 0.25f * sin( M_PI * ( t - DING_SNAP ) / 160.0f ) : 1.0f );
	LGC_DingIcon( x - 21 - gap, y, s, dg_partner, dg_partner < 0 ? dg_weapon : 0, a );
	LGC_DingIcon( x + 21 + gap, y, s, dg_card, 0, a );
	if ( t >= DING_SNAP ) {
		k = LGC_EaseOutBack( ( t - DING_SNAP ) / 260.0f );
		Vector4Copy( evo ? lgc_rarCol[3] : lgc_rarCol[1], c );
		c[3] = a;
		LGC_Text( x, y - 50 - 4 * k, head, c, 9 * k, 13 * k, 0, qtrue );
		c[0] = c[1] = c[2] = 0.95f;
		LGC_Text( x, y + 30, dg_tag, c, CG_DrawStrlen( dg_tag ) > 22 ? 4.5f : 5.5f, 8, 0, qtrue );
	}
}

/*
===============================================================================

THE FLOOR-END TALLY

===============================================================================
*/

static void LGC_TallyButtons( qboolean *pressed ) {
	usercmd_t	cmd;
	int			n = trap_GetCurrentCmdNumber();
	int			down, up;

	*pressed = qfalse;
	if ( n == tl_lastCmd || !trap_GetUserCmd( n, &cmd ) ) {
		return;
	}
	tl_lastCmd = n;
	down = cmd.buttons & ~tl_lastButtons;
	up = cmd.upmove > 0 && tl_lastUp <= 0;
	tl_lastButtons = cmd.buttons;
	tl_lastUp = cmd.upmove;
	*pressed = ( down & BUTTON_ATTACK ) || up;
}

static int LGC_StylePart( int rank ) {
	return rank >= 5 ? 3 : rank >= 3 ? 2 : rank >= 1 ? 1 : 0;
}

// the skulls' sources as chips under the SKULLS line (CS_DESCENT: rb, nt, bs, md, rm)
static void LGC_TallyBreakdown( float y, float a ) {
	const char	*info = CG_ConfigString( CS_DESCENT );
	const char	*s;
	char		parts[10][24];		// (en-boss: + TIME LEFT)
	int			rb[5], np = 0, i, nt, bs, md, rm;
	float		w, x, cw = 5;		// whole units: CG_DrawStringExt rounds the char width
	vec4_t		c, dark;
	static const char	*nodes[5] = { "FIGHT", "ELITE", "BOSS", "EVENT", "VAULT" };

	memset( rb, 0, sizeof( rb ) );
	s = Info_ValueForKey( info, "rb" );
	for ( i = 0 ; i < 5 && *s ; i++ ) {
		rb[i] = atoi( s );
		while ( *s && *s != '.' ) {
			s++;
		}
		if ( *s ) {
			s++;
		}
	}
	nt = atoi( Info_ValueForKey( info, "nt" ) );
	bs = atoi( Info_ValueForKey( info, "bs" ) );
	md = atoi( Info_ValueForKey( info, "md" ) );
	rm = Info_ValueForKey( info, "rm" )[0] ? atoi( Info_ValueForKey( info, "rm" ) ) : 100;
	if ( rb[0] ) {
		Com_sprintf( parts[np++], 24, "FLOOR +%i", rb[0] );
	}
	if ( rb[1] ) {
		Com_sprintf( parts[np++], 24, "KILLS +%i", rb[1] );
	}
	if ( LGC_StylePart( rb[2] ) ) {
		Com_sprintf( parts[np++], 24, "STYLE +%i", LGC_StylePart( rb[2] ) );
	}
	if ( rb[3] ) {
		Com_sprintf( parts[np++], 24, "%s +%i", nodes[nt >= 0 && nt < 5 ? nt : 0], rb[3] );
	}
	if ( bs > 0 ) {
		Com_sprintf( parts[np++], 24, "BOSS +%i", bs );
	}
	if ( md > 0 ) {
		Com_sprintf( parts[np++], 24, "MODIFIER +%i", md );
	}
	if ( atoi( Info_ValueForKey( info, "tb" ) ) > 0 ) {		// en-boss: the timed boss's time bonus
		Com_sprintf( parts[np++], 24, "TIME LEFT +%i", atoi( Info_ValueForKey( info, "tb" ) ) );
	}
	if ( rb[4] ) {
		Com_sprintf( parts[np++], 24, "+%i%%", rb[4] );
	}
	if ( rm != 100 ) {
		Com_sprintf( parts[np++], 24, "x%i.%02i", rm / 100, rm % 100 );
	}
	for ( w = 0, i = 0 ; i < np ; i++ ) {
		w += CG_DrawStrlen( parts[i] ) * cw + 8;
	}
	x = 320 - w * 0.5f;
	dark[0] = dark[1] = dark[2] = 0.05f; dark[3] = a;
	for ( i = 0 ; i < np ; i++ ) {
		Vector4Copy( lgc_rarCol[3], c );
		c[3] = 0.85f * a * CG_Clamp01( ( cg.time - tl_start - TL_END + 140 - i * 60 ) / 120.0f + ( tl_skipT ? 1 : 0 ) );
		w = CG_DrawStrlen( parts[i] ) * cw + 4;
		CG_FillRect( x, y, w, 9, c );
		dark[3] = c[3] / 0.85f;
		LGC_Text( x + 2, y + 1, parts[i], dark, cw, 7, -1, qfalse );
		x += w + 4;
	}
}

static void LGC_DrawTallyPlate( int floorPlayed, float alpha ) {
	static const char	*labels[TL_LINES] = { "KILLS", "HEADSHOTS", "GIBS", "BEST CHAIN", "GEMS", "DAMAGE", "STYLE", "SKULLS" };
	int			t, i, t0, v, shown, mask = tl_v[8];
	float		y, k, s, x0 = 176, x1 = 464, top = 90;
	vec4_t		c, bg;
	char		val[32];
	qboolean	rolling = qfalse;
	const char	*key;

	t = cg.time - tl_start;
	if ( tl_skipT && t < TL_END ) {
		t = TL_END;
	}
	bg[0] = 0.03f; bg[1] = 0.02f; bg[2] = 0.035f; bg[3] = 0.88f * alpha;
	CG_FillRect( x0 - 14, top, x1 - x0 + 28, 266, bg );
	Vector4Copy( lgc_rarCol[3], c );
	c[3] = 0.8f * alpha;
	CG_FillRect( x0 - 14, top, x1 - x0 + 28, 2, c );
	CG_FillRect( x0 - 14, top + 264, x1 - x0 + 28, 2, c );
	LGC_Text( 320, top + 7, va( "FLOOR %i TALLY", floorPlayed ), c, 8, 11, 0, qtrue );
	for ( i = 0 ; i < TL_LINES ; i++ ) {
		y = top + 28 + i * 25;
		t0 = TL_FIRST + i * TL_STEP;
		if ( t < t0 ) {
			c[0] = c[1] = c[2] = 0.35f; c[3] = 0.6f * alpha;
			LGC_Text( x0, y, labels[i], c, 7, 10, -1, qfalse );
			continue;
		}
		k = CG_Clamp01( (float)( t - t0 ) / TL_ROLL );
		v = tl_v[i];
		if ( k < 1.0f ) {
			rolling = qtrue;
		}
		// a slot reel: the digits spin past before they settle
		if ( i == 6 ) {
			shown = k < 1.0f ? ( ( cg.time / 45 ) % 8 ) : ( v >= 0 && v < 8 ? v : 0 );
			Q_strncpyz( val, lgc_styleLetters[shown], sizeof( val ) );
		} else {
			shown = (int)( v * ( 1.0f - ( 1.0f - k ) * ( 1.0f - k ) ) + 0.5f );
			if ( k < 1.0f && v > 9 ) {
				shown = shown - shown % 10 + ( cg.time / 30 ) % 10;
			}
			if ( i == 3 && v <= 0 && k >= 1.0f ) {
				Q_strncpyz( val, "-", sizeof( val ) );
			} else {
				Com_sprintf( val, sizeof( val ), i == 7 ? "+%i" : "%i", shown );
			}
		}
		c[0] = c[1] = c[2] = 0.88f; c[3] = alpha;
		LGC_Text( x0, y, labels[i], c, 7, 10, -1, qfalse );
		// the stamp: the value lands big and settles
		s = 1.0f;
		if ( k >= 1.0f && t - t0 - TL_ROLL < 140 ) {
			s = 1.0f + 0.5f * ( 1.0f - ( t - t0 - TL_ROLL ) / 140.0f );
		}
		Vector4Copy( i == 7 ? lgc_rarCol[3] : colorWhite, c );
		c[3] = alpha;
		LGC_Text( x1 - 70, y - 2 - 3 * ( s - 1 ), val, c, 9 * s, 13 * s, 1, qtrue );
		if ( k >= 1.0f && !( tl_stamped & ( 1 << i ) ) ) {
			tl_stamped |= 1 << i;
			if ( !tl_skipT ) {
				trap_S_StartLocalSound( ( mask & ( 1 << i ) ) ? lgc_sRecord : lgc_sStamp, CHAN_LOCAL_SOUND );
			}
		}
		// NEW RECORD: a red stamp slammed beside the value
		if ( ( mask & ( 1 << i ) ) && k >= 1.0f ) {
			int		ts = t - t0 - TL_ROLL;

			s = ts < 130 ? 1.0f + 1.2f * ( 1.0f - ts / 130.0f ) : 1.0f;
			c[0] = 1.0f; c[1] = 0.22f; c[2] = 0.12f; c[3] = alpha * ( ts < 130 ? ts / 130.0f : 1.0f );
			LGC_Frame( x1 - 27 - 36 * s, y - 3 - 7 * ( s - 1 ), 72 * s, 14 * s, 1, c );
			LGC_Text( x1 - 27, y - 1 - 7 * ( s - 1 ), "NEW RECORD", c, 5.5f * s, 9 * s, 0, qfalse );
		}
		bg[0] = bg[1] = bg[2] = 1; bg[3] = 0.06f * alpha;
		CG_FillRect( x0, y + 13, x1 - x0, 1, bg );
	}
	if ( rolling && cg.time >= tl_tickNext && !tl_skipT ) {
		tl_tickNext = cg.time + 45;
		trap_S_StartLocalSound( lgc_sTick, CHAN_AUTO );
	}
	if ( t >= TL_END - 140 ) {
		LGC_TallyBreakdown( top + 28 + 7 * 25 + 16, alpha );
	}
	key = CG_KeyForCommand( "+attack" );
	c[0] = c[1] = c[2] = 0.7f; c[3] = 0.8f * alpha;
	LGC_Text( 320, top + 250, va( "[%s] %s", key ? key : "FIRE", t < TL_END ? "SKIP" : "CONTINUE" ), c, 5, 7, 0, qfalse );
}

/*
=================
CG_DSC_DrawTally

From CG_RL_DrawIntermission in RLP_END of a won floor. qtrue while the tally owns the
screen (and after it's done, until the server moves on)
=================
*/
qboolean CG_DSC_DrawTally( int floorPlayed ) {
	qboolean	pressed;
	int			t;

	if ( !tl_valid || tl_v[9] != floorPlayed || tl_preview ) {
		return qfalse;
	}
	if ( !CG_DSC_RevealOn() ) {
		if ( !tl_done ) {
			tl_done = 1;
			trap_SendClientCommand( "dsctallydone" );
		}
		return qfalse;
	}
	if ( !tl_start ) {
		tl_start = cg.time;
		tl_lastCmd = trap_GetCurrentCmdNumber();
		tl_lastPress = cg.time;
	}
	LGC_TallyButtons( &pressed );
	t = cg.time - tl_start;
	if ( pressed && cg.time - tl_lastPress > 180 && !tl_done ) {
		tl_lastPress = cg.time;
		if ( t < TL_END && !tl_skipT ) {
			tl_skipT = cg.time;
			trap_S_StartLocalSound( tl_v[8] ? lgc_sRecord : lgc_sStamp, CHAN_LOCAL_SOUND );
		} else {
			tl_done = 1;
		}
	}
	if ( !tl_done && ( t >= TL_END + TL_HOLD || ( tl_skipT && cg.time - tl_skipT >= TL_HOLD + 400 ) ) ) {
		tl_done = 1;
	}
	if ( tl_done == 1 ) {
		tl_done = 2;
		trap_SendClientCommand( "dsctallydone" );
	}
	LGC_DrawTallyPlate( floorPlayed, tl_done ? 0.8f : 1.0f );
	return qtrue;
}

/*
===============================================================================

SERVER COMMANDS, EVENTS

===============================================================================
*/

qboolean CG_DSC_ServerCommand( const char *cmd ) {
	int		i;

	if ( !Q_stricmp( cmd, "dsctally" ) ) {
		for ( i = 0 ; i < 10 ; i++ ) {
			tl_v[i] = atoi( CG_Argv( 1 + i ) );
		}
		tl_preview = atoi( CG_Argv( 11 ) );
		tl_valid = 1;
		tl_start = tl_preview ? cg.time : 0;
		tl_done = 0;
		tl_skipT = 0;
		tl_stamped = 0;
		tl_lastCmd = trap_GetCurrentCmdNumber();
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsclg" ) ) {
		lg_mirror = atoi( CG_Argv( 1 ) );
		lg_phoenix = atoi( CG_Argv( 2 ) );
		lg_agentCl = atoi( CG_Argv( 3 ) );
		lg_agentEnd = cg.time + 1000 * atoi( CG_Argv( 4 ) );
		lg_agentReadyAt = cg.time + 1000 * atoi( CG_Argv( 5 ) );
		lg_orbs = atoi( CG_Argv( 6 ) );
		lg_blackmass = atoi( CG_Argv( 7 ) );
		lg_held = atoi( CG_Argv( 8 ) );		// the server's kit (cards given this floor too)
		lg_hudOn = 1;
		return qtrue;
	}
	return qfalse;
}

static void LGC_Flash( const vec3_t org, const vec3_t color, float radius, float endRadius, int life ) {
	localEntity_t	*le = CG_AllocLocalEntity();

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

static void LGC_Ring( const vec3_t org, const vec3_t normal, const vec3_t color, float endRadius, int life ) {
	localEntity_t	*le = CG_AllocLocalEntity();

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

static void LGC_Mote( const vec3_t org, const vec3_t vel, const vec3_t color, int life, float size ) {
	localEntity_t	*le = CG_AllocLocalEntity();

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

static void LGC_Beam( const vec3_t a, const vec3_t b, const vec3_t color, int life ) {
	localEntity_t	*le = CG_AllocLocalEntity();

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

static void LGC_Bolt( const vec3_t a, const vec3_t b, const vec3_t color, int segs, float jitter, int life ) {
	vec3_t	dir, right, up, p0, p1;
	float	len;
	int		i;

	VectorSubtract( b, a, dir );
	len = VectorNormalize( dir );
	if ( len < 1 ) {
		return;
	}
	PerpendicularVector( right, dir );
	CrossProduct( dir, right, up );
	VectorCopy( a, p0 );
	for ( i = 1 ; i <= segs ; i++ ) {
		VectorMA( a, len * i / segs, dir, p1 );
		if ( i < segs ) {
			VectorMA( p1, crandom() * jitter, right, p1 );
			VectorMA( p1, crandom() * jitter, up, p1 );
		}
		LGC_Beam( p0, p1, color, life );
		VectorCopy( p1, p0 );
	}
}

static void LGC_Sound( const vec3_t org, int kind, qboolean mine ) {
	if ( kind <= 0 || kind > LGX_CHAIN || !lgc_sFx[kind] ) {
		return;
	}
	if ( mine ) {
		trap_S_StartLocalSound( lgc_sFx[kind], CHAN_AUTO );
	} else {
		CG_FxSound( org, ENTITYNUM_WORLD, CHAN_AUTO, lgc_sFx[kind] );
	}
}

void CG_DSC_Event( centity_t *cent, vec3_t position ) {
	entityState_t	*es = &cent->currentState;
	vec3_t			col, up, d, a, v, p;
	int				i, kind = es->eventParm;
	qboolean		mine = cg.snap && es->otherEntityNum == cg.snap->ps.clientNum;
	float			len;

	VectorSet( up, 0, 0, 1 );
	switch ( kind ) {
	case LGX_CHAIN:
		VectorSet( col, 0.55f, 0.8f, 1.0f );
		LGC_Bolt( position, es->origin2, col, 7, 16, 160 );
		LGC_Bolt( position, es->origin2, col, 5, 9, 110 );
		VectorSet( col, 1.0f, 1.0f, 1.0f );
		LGC_Bolt( position, es->origin2, col, 4, 4, 70 );
		VectorSet( col, 0.55f, 0.8f, 1.0f );
		LGC_Flash( es->origin2, col, 12, 60, 220 );
		VectorSubtract( position, es->origin2, d );
		VectorNormalize( d );
		CG_FxSparks( es->origin2, d, col, 8, 260, 260, 0.2f, 0 );
		LGC_Sound( es->origin2, kind, qfalse );
		break;
	case LGX_ECHO:
		VectorSet( col, 0.35f, 0.9f, 1.0f );
		LGC_Flash( position, col, 6, 30, 160 );
		LGC_Ring( position, up, col, 40, 260 );
		LGC_Sound( position, kind, mine );
		lg_flash = cg.time; lg_flashKind = kind;
		break;
	case LGX_VAMPIRE:
		VectorSet( col, 0.9f, 0.08f, 0.12f );
		VectorSubtract( es->origin2, position, d );
		len = VectorLength( d );
		for ( i = 0 ; i < 14 ; i++ ) {
			VectorSet( a, position[0] + crandom() * 12, position[1] + crandom() * 12, position[2] + 10 + crandom() * 12 );
			VectorScale( d, 1.0f / 0.45f, v );
			v[2] += 40 * crandom();
			LGC_Mote( a, v, col, 420 + rand() % 80, 3 + es->generic1 / 20.0f );
		}
		LGC_Flash( es->origin2, col, 10, 48, 300 );
		LGC_Sound( es->origin2, kind, mine );
		(void)len;
		lg_flash = cg.time; lg_flashKind = kind;
		break;
	case LGX_TIME:
		lg_timeFlash = cg.time;
		lg_timeAmount = es->generic1;
		VectorSet( col, 0.4f, 1.0f, 0.9f );
		LGC_Ring( position, up, col, 70, 380 );
		LGC_Sound( position, kind, mine );
		break;
	case LGX_BLOODENGINE:
		VectorSet( col, 1.0f, 0.12f, 0.08f );
		LGC_Flash( position, col, 16, 70, 260 );
		VectorSubtract( es->origin2, position, d );
		for ( i = 0 ; i < 8 ; i++ ) {
			VectorSet( a, position[0] + crandom() * 16, position[1] + crandom() * 16, position[2] + crandom() * 16 );
			VectorScale( d, 1.0f / 0.35f, v );
			LGC_Mote( a, v, col, 330, 4 );
		}
		LGC_Sound( position, kind, mine );
		lg_flash = cg.time; lg_flashKind = kind;
		break;
	case LGX_MIRROR:
		VectorSet( col, 0.75f, 0.95f, 1.0f );
		LGC_Flash( position, col, 30, 90, 260 );
		LGC_Ring( position, up, col, 110, 360 );
		LGC_Beam( position, es->origin2, col, 240 );
		CG_FxSparks( position, up, col, 18, 320, 500, 0.0f, 0 );
		LGC_Sound( position, kind, mine );
		lg_flash = cg.time; lg_flashKind = kind;
		break;
	case LGX_PHOENIX:
		VectorSet( col, 1.0f, 0.5f, 0.1f );
		if ( cg_fx.integer ) {
			CG_FxExplosion( position, up, 1.6f, col, qtrue );
		}
		LGC_Ring( position, up, col, es->generic1 > 0 ? es->generic1 : 320, 600 );
		VectorSet( col, 1.0f, 0.8f, 0.3f );
		LGC_Ring( position, up, col, ( es->generic1 > 0 ? es->generic1 : 320 ) * 0.6f, 450 );
		LGC_Flash( position, col, 60, 220, 500 );
		for ( i = 0 ; i < 30 ; i++ ) {
			VectorSet( a, position[0] + crandom() * 30, position[1] + crandom() * 30, position[2] - 10 + random() * 40 );
			VectorSet( v, crandom() * 90, crandom() * 90, 160 + random() * 220 );
			LGC_Mote( a, v, col, 900 + rand() % 600, 5 );
		}
		CG_FxShake( position, 6, 900 );
		LGC_Sound( position, kind, mine );
		lg_flash = cg.time; lg_flashKind = kind;
		break;
	case LGX_ORBHIT:
		VectorSet( col, 0.85f, 0.4f, 1.0f );
		LGC_Flash( position, col, 8, 30, 140 );
		CG_FxSparks( position, up, col, 5, 200, 220, 0.0f, 0 );
		LGC_Sound( position, kind, qfalse );
		break;
	case LGX_AGENT:
		VectorSet( col, 0.2f, 1.0f, 0.35f );
		LGC_Flash( position, col, 20, 80, 300 );
		LGC_Ring( position, up, col, 80, 400 );
		if ( es->generic1 ) {
			LGC_Beam( es->origin2, position, col, 300 );
			lg_agentCl = es->otherEntityNum2;
			lg_agentEnd = cg.time + 8000;
		} else if ( lg_agentCl == es->otherEntityNum2 ) {
			lg_agentEnd = cg.time;
		}
		LGC_Sound( position, kind, mine );
		break;
	case LGX_GRAVE:
		VectorSet( col, 0.55f, 0.42f, 0.3f );
		CG_FxSparks( position, up, col, 10, 160, 500, 0.5f, 0 );
		if ( es->generic1 ) {
			VectorSet( col, 1.0f, 0.75f, 0.2f );
			LGC_Flash( position, col, 20, 90, 400 );
			LGC_Ring( position, up, col, 70, 400 );
		}
		LGC_Sound( position, kind, mine );
		break;
	case LGX_BLACKMASS:
		VectorSet( col, 0.5f, 0.15f, 0.9f );
		LGC_Flash( position, col, 120, 10, 500 );		// collapses inward
		LGC_Ring( position, up, col, 420, 700 );
		for ( i = 0 ; i < 20 ; i++ ) {
			VectorSet( p, crandom(), crandom(), crandom() * 0.4f );
			VectorNormalize( p );
			VectorMA( position, 160, p, a );
			VectorScale( p, -320, v );
			LGC_Mote( a, v, col, 500, 5 );
		}
		CG_FxShake( position, 3, 700 );
		LGC_Sound( position, kind, mine );
		lg_flash = cg.time; lg_flashKind = kind;
		break;
	case LGX_DEADEYE:
		VectorCopy( position, p );
		p[2] += 26;
		VectorSet( col, 1.0f, 0.85f, 0.3f );
		LGC_Flash( p, col, 6, 34, 220 );
		CG_FxSparks( p, up, col, 6, 220, 260, 0.0f, 0 );
		LGC_Sound( p, kind, mine );
		break;
	default:
		break;
	}
}

/*
===============================================================================

IN PLAY: MAGNETAR orbs, the agent's shell, the legendary row, evolution ready, dstally

===============================================================================
*/

static qboolean LGC_Holds( int lg ) {
	return CG_RL_Active() && ( CG_RL_CardCount( RLC_LEGEND_FIRST + lg ) > 0 || ( lg_held & ( 1 << lg ) ) );
}

void CG_DSC_AddScene( void ) {
	refEntity_t	re;
	vec3_t		org, p, ang;
	int			k, n, t;
	float		a, a2;
	static const vec3_t	col = { 0.8f, 0.35f, 1.0f };

	if ( !LGC_Holds( LG_MAGNETAR ) || !cg.snap || cg.snap->ps.stats[STAT_HEALTH] <= 0
		|| cg.snap->ps.pm_type == PM_INTERMISSION ) {
		return;
	}
	n = lg_orbs > 0 ? lg_orbs : 3;
	VectorCopy( cg.predictedPlayerState.origin, org );
	for ( k = 0 ; k < n ; k++ ) {
		a = cg.time * LGC_ORB_SPEED + k * 2.0f * M_PI / n;
		p[0] = org[0] + cos( a ) * LGC_ORB_RADIUS;
		p[1] = org[1] + sin( a ) * LGC_ORB_RADIUS;
		p[2] = org[2] + 10 + 3 * sin( cg.time * 0.005f + k );
		memset( &re, 0, sizeof( re ) );
		re.reType = RT_MODEL;
		re.hModel = lgc_gem[k % 3];
		VectorCopy( p, re.origin );
		VectorCopy( p, re.oldorigin );
		VectorCopy( p, re.lightingOrigin );
		VectorSet( ang, 0, cg.time * 0.4f + k * 60, 0 );
		AnglesToAxis( ang, re.axis );
		re.renderfx = RF_NOSHADOW | RF_MINLIGHT;
		if ( re.hModel ) {
			trap_R_AddRefEntityToScene( &re );
		}
		// a bright core, a halo and a short comet tail along the orbit
		for ( t = 0 ; t < 6 ; t++ ) {
			memset( &re, 0, sizeof( re ) );
			re.reType = RT_SPRITE;
			a2 = a - t * 0.09f;
			re.origin[0] = org[0] + cos( a2 ) * LGC_ORB_RADIUS;
			re.origin[1] = org[1] + sin( a2 ) * LGC_ORB_RADIUS;
			re.origin[2] = p[2];
			re.radius = t == 0 ? 15 : 9 - t;
			re.customShader = lgc_glow;
			re.shaderRGBA[0] = (byte)( 210 - t * 25 ); re.shaderRGBA[1] = (byte)( 100 - t * 12 ); re.shaderRGBA[2] = (byte)( 255 - t * 30 ); re.shaderRGBA[3] = 255;
			re.renderfx = RF_NOSHADOW;
			trap_R_AddRefEntityToScene( &re );
		}
	}
	trap_R_AddLightToScene( org, 90, col[0], col[1], col[2] );
}

void CG_DSC_BodyShell( refEntity_t *ent, entityState_t *state ) {
	if ( lg_agentCl < 0 || state->clientNum != lg_agentCl || cg.time >= lg_agentEnd || ( state->eFlags & EF_DEAD ) ) {
		return;
	}
	ent->customShader = lgc_agentShell;
	trap_R_AddRefEntityToScene( ent );
	ent->customShader = 0;
}

// a newly ready evolution (level 8 + key held + not evolved): mask of dsEvolution_t
static int LGC_ReadyMask( void ) {
	int		e, w, key, held, m = 0, ev = CG_DS_EvolvedMask();

	for ( e = 0 ; e < EVO_NUM ; e++ ) {
		if ( ev & ( 1 << e ) ) {
			continue;
		}
		key = ds_evolutions[e].keyCard;
		held = RLC_IS_PASSIVE( key ) ? CG_DS_PassiveLevel( key - RLC_PASSIVE_FIRST ) > 0 : CG_RL_CardCount( key ) > 0;
		if ( !held ) {
			continue;
		}
		if ( e == EVO_ARMAGEDDON ) {
			if ( ( ev & ( 1 << EVO_HELLFIRE ) ) && ( ev & ( 1 << EVO_CLUSTERBOMB ) ) ) {
				m |= 1 << e;
			}
			continue;
		}
		w = ds_evolutions[e].weapon;
		if ( CG_DS_WeaponLevel( w ) >= DS_MAX_WLEVEL ) {
			m |= 1 << e;
		}
	}
	return m;
}

static void LGC_DrawEvoReady( void ) {
	int			m, e, t;
	vec4_t		c;
	float		a, x, y;
	const char	*s;

	if ( !CG_RL_Active() || !cg.snap ) {
		ev_init = 0;
		return;
	}
	m = LGC_ReadyMask();
	if ( !ev_init || cg.time < ev_time ) {
		ev_init = 1;
		ev_mask = m;
		ev_time = 0;
	}
	if ( m & ~ev_mask ) {
		for ( e = 0 ; e < EVO_NUM ; e++ ) {
			if ( ( m & ~ev_mask ) & ( 1 << e ) ) {
				ev_evo = e;
				break;
			}
		}
		ev_time = cg.time;
		trap_S_StartLocalSound( lgc_sEvoReady, CHAN_LOCAL_SOUND );
		CG_Printf( "Evolution ready: %s\n", ds_evolutions[ev_evo].name );
	}
	ev_mask = m;
	if ( !ev_time || cg.time - ev_time > 4000 || cg.showScores ) {
		return;
	}
	t = cg.time - ev_time;
	a = t < 200 ? t / 200.0f : t > 3400 ? ( 4000 - t ) / 600.0f : 1.0f;
	CG_HudAnchor( 1 );
	x = 617 + cg.hudShift;		// en-screens: in line with the other right-edge callouts, in the shared stack
	y = CG_EdgeSlot( EDGE_EVO, 14 ) + 3;
	s = va( "%s READY: OPEN A CHEST", ds_evolutions[ev_evo].name );
	LGC_Glow( x - CG_DrawStrlen( s ) * 2.75f, y + 4, CG_DrawStrlen( s ) * 6.0f, 40, lgc_rarCol[3], 0.35f * a * ( t < 600 ? 1.0f + ( 600 - t ) / 300.0f : 1.0f ) );
	Vector4Copy( lgc_rarCol[3], c );
	c[3] = a;
	cg.hudShift = 0;
	LGC_Text( x, y, s, c, 5.5f, 8, 1, qtrue );
	CG_HudAnchor( 0 );
}

// the small legendary row under the corner HUD: gold chips, dim = spent / cooling down
static void LGC_DrawLegendRow( void ) {
	int			i, n = 0, left;
	float		x = 8, y, cw = 4, w;
	vec4_t		c, dark;
	char		tag[24];
	qboolean	dim;

	if ( !CG_RL_Active() || !cg.snap || cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR || cg.showScores
		|| cg.snap->ps.pm_type == PM_INTERMISSION ) {
		return;
	}
	for ( i = 0 ; i < LG_NUM ; i++ ) {
		if ( LGC_Holds( i ) ) {
			n++;
		}
	}
	if ( !n ) {
		return;
	}
	CG_HudAnchor( -1 );
	y = 87;		// just above the corner block (the frenzy counter sits under it)
	dark[0] = dark[1] = dark[2] = 0.04f; dark[3] = 0.95f;
	for ( i = 0 ; i < LG_NUM ; i++ ) {
		if ( !LGC_Holds( i ) ) {
			continue;
		}
		Q_strncpyz( tag, bg_rlCardShort[RLC_LEGEND_FIRST + i], sizeof( tag ) );
		dim = qfalse;
		switch ( i ) {
		case LG_MIRROR:
			dim = lg_hudOn && !lg_mirror;
			break;
		case LG_PHOENIX:
			dim = lg_hudOn && !lg_phoenix;
			break;
		case LG_DOUBLEAGENT:
			if ( lg_agentCl >= 0 && cg.time < lg_agentEnd ) {
				Q_strncpyz( tag, va( "DAG %i", ( lg_agentEnd - cg.time + 999 ) / 1000 ), sizeof( tag ) );
			} else if ( cg.time < lg_agentReadyAt ) {
				left = ( lg_agentReadyAt - cg.time + 999 ) / 1000;
				Q_strncpyz( tag, va( "DAG %i", left ), sizeof( tag ) );
				dim = qtrue;
			}
			break;
		case LG_MAGNETAR:
			Q_strncpyz( tag, va( "MGT %i", lg_orbs > 0 ? lg_orbs : 3 ), sizeof( tag ) );
			break;
		case LG_BLACKMASS:
			Q_strncpyz( tag, va( "BMS %i/9", lg_blackmass ), sizeof( tag ) );
			break;
		case LG_TIMETHIEF:
			if ( lg_timeFlash && cg.time - lg_timeFlash < 1200 ) {
				Q_strncpyz( tag, va( "+%i.%is", lg_timeAmount / 1000, ( lg_timeAmount % 1000 ) / 100 ), sizeof( tag ) );
			}
			break;
		default:
			break;
		}
		w = CG_DrawStrlen( tag ) * cw + 4;
		Vector4Copy( lgc_rarCol[3], c );
		c[3] = dim ? 0.35f : 0.9f;
		if ( lg_flash && cg.time - lg_flash < 300 && ( ( lg_flashKind == LGX_MIRROR && i == LG_MIRROR ) || ( lg_flashKind == LGX_PHOENIX && i == LG_PHOENIX )
			|| ( lg_flashKind == LGX_VAMPIRE && i == LG_VAMPIRE ) || ( lg_flashKind == LGX_BLOODENGINE && i == LG_BLOODENGINE )
			|| ( lg_flashKind == LGX_BLACKMASS && i == LG_BLACKMASS ) || ( lg_flashKind == LGX_ECHO && i == LG_ECHO ) ) ) {
			c[0] = c[1] = c[2] = 1; c[3] = 1;
		}
		CG_FillRect( x, y, w, 8, c );
		LGC_Text( x + 2, y + 1, tag, dark, cw, 6, -1, qfalse );
		x += w + 2;
	}
	CG_HudAnchor( 0 );
}

void CG_DSC_Draw2D( void ) {
	vec4_t	c;

	LGC_DrawEvoReady();
	LGC_DrawLegendRow();
	// dev: dstally shows the tally over the game
	if ( tl_preview && tl_valid ) {
		if ( cg.time - tl_start > TL_END + TL_HOLD + 2000 ) {
			tl_preview = 0;
			return;
		}
		c[0] = c[1] = c[2] = 0; c[3] = 0.5f;
		CG_FillRect( -2000, 0, 4640, 480, c );
		LGC_DrawTallyPlate( tl_v[9], 1.0f );
	}
}
