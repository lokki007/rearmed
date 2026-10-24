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
// cg_style.c -- style meter HUD, perk picker, personal records (fastest wins, weapon mastery)

#include "cg_local.h"

/*
===============================================================================

STYLE METER

===============================================================================
*/

#define STYLE_FEED			5
#define STYLE_FEED_TIME		2000
#define STYLE_POP_TIME		420
#define STYLE_SLIDE_TIME	180

static const char *styleLetters[8] = { "", "D", "C", "B", "A", "S", "SS", "SSS" };
static const char *styleWords[8] = { "", "DECENT", "CRUEL", "BRUTAL", "ANARCHIC", "SAVAGE", "SADISTIC", "SLAUGHTERHOUSE" };
static const int styleThresholds[8] = { 0, STYLE_RANK_D, STYLE_RANK_C, STYLE_RANK_B, STYLE_RANK_A, STYLE_RANK_S, STYLE_RANK_SS, STYLE_RANK_SSS };
static const vec4_t styleColors[8] = {
	{ 1, 1, 1, 1 },
	{ 0.55f, 0.65f, 0.8f, 1 },		// D
	{ 0.3f, 0.85f, 1.0f, 1 },		// C
	{ 0.35f, 1.0f, 0.4f, 1 },		// B
	{ 1.0f, 0.9f, 0.25f, 1 },		// A
	{ 1.0f, 0.55f, 0.1f, 1 },		// S
	{ 1.0f, 0.2f, 0.15f, 1 },		// SS
	{ 1.0f, 0.3f, 0.9f, 1 }		// SSS
};

typedef struct {
	char	label[32];
	int		points;
	int		time;		// last time points were added
	int		born;		// when the entry appeared
	float	y;			// animated slot position
} styleFeed_t;

static styleFeed_t	styleFeed[STYLE_FEED];
static int			styleRank;
static int			styleRankTime;
static int			styleRankDown;		// the pop was a drop
static float		styleShown;			// smoothed meter value
static int			styleLastDraw;
static sfxHandle_t	styleVoices[8];
static sfxHandle_t	perkOfferSound, newRecordSound, masterySound, perkPickSound, perkCardSound;

static int CG_StyleRankFor( int style ) {
	int		r;

	for ( r = 7 ; r > 0 ; r-- ) {
		if ( style >= styleThresholds[r] ) {
			return r;
		}
	}
	return 0;
}

void CG_StyleRegister( void ) {
	static const char *files[8] = { NULL, "rank_d", "rank_c", "rank_b", "rank_a", "rank_s", "rank_ss", "rank_sss" };
	int		i;

	for ( i = 1 ; i < 8 ; i++ ) {
		styleVoices[i] = trap_S_RegisterSound( va( "sound/rearmed/%s.wav", files[i] ), qfalse );
	}
	perkOfferSound = trap_S_RegisterSound( "sound/rearmed/perk_offer.wav", qfalse );
	newRecordSound = trap_S_RegisterSound( "sound/rearmed/new_record.wav", qfalse );
	masterySound = trap_S_RegisterSound( "sound/rearmed/mastery_up.wav", qfalse );
	perkPickSound = trap_S_RegisterSound( "sound/rearmed/events/perk_pick.wav", qfalse );
	perkCardSound = trap_S_RegisterSound( "sound/rearmed/events/perk_cards.wav", qfalse );
	memset( styleFeed, 0, sizeof( styleFeed ) );
	styleRank = 0;
	styleShown = 0;
	cg.rmPerkOffer[0] = -1;
}

static void CG_StyleFeedAdd( int points, const char *label ) {
	int		i;

	// the same label again soon just adds up
	for ( i = 0 ; i < 2 ; i++ ) {
		if ( styleFeed[i].time && cg.time - styleFeed[i].time < 900 && !Q_stricmp( styleFeed[i].label, label ) ) {
			styleFeed[i].points += points;
			styleFeed[i].time = cg.time;
			return;
		}
	}
	for ( i = STYLE_FEED - 1 ; i > 0 ; i-- ) {
		styleFeed[i] = styleFeed[i - 1];
	}
	Q_strncpyz( styleFeed[0].label, label, sizeof( styleFeed[0].label ) );
	styleFeed[0].points = points;
	styleFeed[0].time = cg.time;
	styleFeed[0].born = cg.time;
	styleFeed[0].y = -11;
}

static float CG_EaseOutBack( float t ) {
	float	s = 1.70158f;

	t = CG_Clamp01( t ) - 1.0f;
	return t * t * ( ( s + 1 ) * t + s ) + 1.0f;
}

static float CG_EaseOut( float t ) {
	t = 1.0f - CG_Clamp01( t );
	return 1.0f - t * t * t;
}

/*
=================
CG_DrawStyle

Rank letter, rank name, meter to the next rank and the recent style acts, right of the crosshair
=================
*/
/*
en-screens: the bottom (640x480 y) of the style acts feed under the rank letter right now, 0 when it
shows nothing. The shared right-edge callout stack (cg_edge.c) starts under it.
*/
float CG_StyleFeedBottom( void ) {
	float	b = 0;
	int		i;

	if ( !cg_style.integer || !( cgs.rmSwitches & ( 1 << RS_STYLE ) ) || !cg.snap
		|| cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
		return 0;
	}
	for ( i = 0 ; i < STYLE_FEED ; i++ ) {
		if ( styleFeed[i].time && cg.time - styleFeed[i].time <= STYLE_FEED_TIME && 150 + 66 + styleFeed[i].y + 11 > b ) {
			b = 150 + 66 + styleFeed[i].y + 11;		// (CG_DrawStyle: y 150, the feed at y + 66, 10 tall)
		}
	}
	return b;
}

void CG_DrawStyle( void ) {
	playerState_t	*ps = &cg.snap->ps;
	int		style, rank, i;
	float	x, y, scale, f, lo, hi, p, dt, jx, jy, slide, a;
	vec4_t	color, flash;

	if ( !cg_style.integer || !( cgs.rmSwitches & ( 1 << RS_STYLE ) ) || ps->persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
		return;
	}
	style = ps->stats[STAT_STYLE];
	rank = CG_StyleRankFor( style );

	// smoothed meter (fast up, softer down)
	dt = ( cg.time - styleLastDraw ) * 0.001f;
	styleLastDraw = cg.time;
	if ( dt < 0 || dt > 0.2f ) {
		dt = 0.2f;
	}
	styleShown += ( style - styleShown ) * CG_Clamp01( dt * ( style > styleShown ? 14.0f : 6.0f ) );

	// rank changes: announce going up
	if ( rank != styleRank ) {
		if ( rank > styleRank && rank > 0 ) {
			styleRankTime = cg.time;
			styleRankDown = qfalse;
			if ( cg_rearmAnnouncer.integer ) {
				trap_S_StartLocalSound( styleVoices[rank], CHAN_ANNOUNCER );
			}
		} else if ( rank > 0 ) {
			styleRankTime = cg.time;
			styleRankDown = qtrue;
		}
		styleRank = rank;
	}

	// right of the crosshair, pushed out to the 16:9 area on wide screens (cg_hudWide)
	CG_HudAnchor( 1 );
	x = 486;
	y = 150;
	if ( rank > 0 ) {
		Vector4Copy( styleColors[rank], color );
		if ( rank == 7 ) {
			f = 0.75f + 0.25f * sin( cg.time * 0.02f );
			color[0] *= f;
			color[2] *= f;
		}
		scale = 1.0f;
		jx = jy = 0;
		p = (float)( cg.time - styleRankTime ) / STYLE_POP_TIME;
		if ( p >= 0 && p < 1 ) {
			if ( styleRankDown ) {
				scale = 1.0f - 0.15f * ( 1.0f - p );
				f = 0.6f * ( 1.0f - p );
				color[0] = color[0] * ( 1 - f ) + 0.4f * f;
				color[1] = color[1] * ( 1 - f ) + 0.4f * f;
				color[2] = color[2] * ( 1 - f ) + 0.4f * f;
			} else {
				// pop: big and white, shaking, settling into the rank colour
				scale = 1.0f + 0.9f * ( 1.0f - CG_EaseOut( p ) );
				jx = crandom() * 3.0f * ( 1.0f - p );
				jy = crandom() * 3.0f * ( 1.0f - p );
				f = CG_Clamp01( 1.0f - p * 2.5f );
				color[0] = color[0] * ( 1 - f ) + f;
				color[1] = color[1] * ( 1 - f ) + f;
				color[2] = color[2] * ( 1 - f ) + f;
				// a ring of light behind the letter
				flash[0] = styleColors[rank][0]; flash[1] = styleColors[rank][1]; flash[2] = styleColors[rank][2];
				flash[3] = 0.25f * ( 1.0f - p );
				CG_FillRect( x - 6 - 14 * p, y - 4 - 14 * p, 40 + 28 * p, 50 + 28 * p, flash );
			}
		}
		// big letter
		CG_DrawStringExt( x + jx - 14 * ( scale - 1 ), y + jy - 20 * ( scale - 1 ), styleLetters[rank], color, qtrue, qtrue,
			(int)( 28 * scale ), (int)( 40 * scale ), 0 );
		// name
		CG_DrawStringExt( x, y + 42, styleWords[rank], color, qtrue, qtrue, 8, 12, 0 );
		// meter to the next rank
		lo = styleThresholds[rank];
		hi = rank < 7 ? styleThresholds[rank + 1] : STYLE_MAX;
		f = hi > lo ? ( styleShown - lo ) / ( hi - lo ) : 1.0f;
		f = CG_Clamp01( f );
		flash[0] = flash[1] = flash[2] = 0; flash[3] = 0.45f;
		CG_FillRect( x - 1, y + 56, 112, 5, flash );
		color[3] = 0.3f;
		CG_FillRect( x, y + 57, 110, 3, color );
		color[3] = 0.95f;
		CG_FillRect( x, y + 57, 110 * f, 3, color );
		// draining: a thin white edge at the tip
		if ( style < styleShown - 1 ) {
			flash[0] = flash[1] = flash[2] = 1; flash[3] = 0.6f;
			CG_FillRect( x + 110 * f - 1, y + 56, 2, 5, flash );
		}
	}

	// recent style acts: slide in from the right, glide to their slot, fade out
	for ( i = 0 ; i < STYLE_FEED ; i++ ) {
		if ( !styleFeed[i].time || cg.time - styleFeed[i].time > STYLE_FEED_TIME ) {
			continue;
		}
		styleFeed[i].y += ( i * 11 - styleFeed[i].y ) * CG_Clamp01( dt * 16 );
		f = 1.0f - (float)( cg.time - styleFeed[i].time ) / STYLE_FEED_TIME;
		p = (float)( cg.time - styleFeed[i].born ) / STYLE_SLIDE_TIME;
		slide = 26 * ( 1.0f - CG_EaseOut( p ) );
		a = f < 0.3f ? f / 0.3f : 1.0f;
		a *= CG_Clamp01( p * 2 ) * 0.85f;
		// a fresh combo flashes gold
		p = (float)( cg.time - styleFeed[i].time ) / 250.0f;
		color[0] = 1;
		color[1] = p < 1 ? 0.85f + 0.15f * p : 1;
		color[2] = p < 1 ? 0.35f + 0.65f * p : 1;
		color[3] = a;
		CG_DrawStringExt( x + slide, y + 66 + styleFeed[i].y, va( "+%i %s", styleFeed[i].points, styleFeed[i].label ), color,
			qfalse, qtrue, 7, 10, 0 );
	}
	CG_HudAnchor( 0 );
}

/*
===============================================================================

PERKS

===============================================================================
*/

#define CARD_W			150
#define CARD_H			76
#define CARD_GAP		12
#define CARD_Y			300
#define CARD_IN_TIME	340
#define CARD_STAGGER	70
#define CARD_PICK_TIME	520

static const vec4_t perkCatColors[4] = {
	{ 1.0f, 0.32f, 0.25f, 1 },		// offense
	{ 0.3f, 0.6f, 1.0f, 1 },		// defense
	{ 0.35f, 1.0f, 0.45f, 1 },		// movement
	{ 0.8f, 0.4f, 1.0f, 1 }			// chaos
};
static const char *perkCatNames[4] = { "OFFENSE", "DEFENSE", "MOVEMENT", "CHAOS" };

static int	perkOfferTime;			// cg.time the cards came in
static int	perkOfferMsec = 15000;
static int	perkShown[3];			// the cards on screen (kept for the pick animation)
static int	perkPickTime;
static int	perkPickIndex = -1;		// card that was taken, -1 = none
static int	perkGainTime[PERK_NUM];
static int	perkOrder[PERK_NUM];	// owned perks in the order they came
static int	perkOrderCount;

/*
=================
CG_PerkPanelPick

Called by the "weapon" command: while a perk is on offer, weapon 1..3 picks it
=================
*/
qboolean CG_PerkPanelPick( int num ) {
	if ( cg.rmPerkOffer[0] < 0 || num < 1 || num > 3 || cg.rmPerkOffer[num - 1] < 0 ) {
		return qfalse;
	}
	trap_SendClientCommand( va( "perkpick %i", num ) );
	perkPickIndex = num - 1;
	perkPickTime = cg.time;
	cg.rmPerkOffer[0] = -1;
	trap_S_StartLocalSound( perkPickSound, CHAN_LOCAL_SOUND );
	return qtrue;
}

/*
=================
CG_WrapLine

Next line of at most maxChars from *s, breaking at a space
=================
*/
static const char *CG_WrapLine( const char **s, int maxChars ) {
	static char	line[64];
	const char	*p = *s;
	int			len, cut;

	while ( *p == ' ' ) {
		p++;
	}
	len = strlen( p );
	if ( len <= maxChars ) {
		Q_strncpyz( line, p, sizeof( line ) );
		*s = p + len;
		return line;
	}
	for ( cut = maxChars ; cut > 0 && p[cut] != ' ' ; cut-- ) {
	}
	if ( cut <= 0 ) {
		cut = maxChars;
	}
	if ( cut >= (int)sizeof( line ) ) {
		cut = sizeof( line ) - 1;
	}
	memcpy( line, p, cut );
	line[cut] = 0;
	*s = p + cut;
	return line;
}

/*
=================
CG_DrawPerkCard
=================
*/
static void CG_DrawPerkCard( int slot, int perk, float cx, float cy, float scale, float alpha, float flash ) {
	vec4_t		bg, fg, cat;
	const char	*desc, *line, *key;
	float		x, y, w, h;
	int			c, n;

	c = bg_perkCategory[perk];
	if ( c < 0 || c > 3 ) {
		c = 0;
	}
	w = CARD_W * scale;
	h = CARD_H * scale;
	x = cx - w * 0.5f;
	y = cy - h * 0.5f;

	bg[0] = 0.035f; bg[1] = 0.035f; bg[2] = 0.05f; bg[3] = 0.86f * alpha;
	CG_FillRect( x, y, w, h, bg );
	Vector4Copy( perkCatColors[c], cat );
	cat[3] = 0.9f * alpha;
	CG_FillRect( x, y, 4 * scale, h, cat );			// category strip
	cat[3] = 0.55f * alpha;
	CG_FillRect( x, y, w, 1, cat );
	CG_FillRect( x, y + h - 1, w, 1, cat );
	cat[3] = 0.12f * alpha;
	CG_FillRect( x + 4 * scale, y, w - 4 * scale, 22 * scale, cat );

	// key hint
	key = CG_KeyForCommand( va( "weapon %i", slot + 1 ) );
	if ( !key ) {
		key = va( "%i", slot + 1 );
	}
	cat[3] = alpha;
	CG_FillRect( x + 8 * scale, y + 4 * scale, ( 8 + 10 * CG_DrawStrlen( key ) ) * scale, 16 * scale, cat );
	fg[0] = fg[1] = fg[2] = 0.02f; fg[3] = alpha;
	CG_DrawStringExt( x + 12 * scale, y + 5 * scale, key, fg, qtrue, qfalse, (int)( 10 * scale ), (int)( 14 * scale ), 0 );

	// name and category
	fg[0] = fg[1] = fg[2] = 1.0f; fg[3] = alpha;
	CG_DrawStringExt( x + ( 22 + 10 * CG_DrawStrlen( key ) ) * scale, y + 6 * scale, bg_perkNames[perk], fg, qtrue, qtrue,
		(int)( 8 * scale ), (int)( 12 * scale ), 0 );
	cat[3] = alpha;
	CG_DrawStringExt( x + 10 * scale, y + 25 * scale, perkCatNames[c], cat, qtrue, qfalse,
		(int)( 5 * scale ), (int)( 7 * scale ), 0 );

	// description, word wrapped
	desc = bg_perkDescs[perk];
	fg[0] = fg[1] = fg[2] = 0.88f;
	for ( n = 0 ; n < 4 && *desc ; n++ ) {
		line = CG_WrapLine( &desc, 26 );
		CG_DrawStringExt( x + 10 * scale, y + ( 36 + n * 10 ) * scale, line, fg, qfalse, qfalse,
			(int)( 5 * scale ), (int)( 8 * scale ), 0 );
	}

	// pick flash
	if ( flash > 0 ) {
		fg[0] = fg[1] = fg[2] = 1.0f; fg[3] = flash;
		CG_FillRect( x, y, w, h, fg );
	}
}

/*
=================
CG_DrawPerks

The offer (three cards, press 1-3) and the build so far
=================
*/
void CG_DrawPerks( void ) {
	vec4_t	col, bg;
	int		i, perk, left;
	float	cx, cy, t, e, scale, alpha, flash, total, f;

	if ( cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
		return;
	}
	total = 3 * CARD_W + 2 * CARD_GAP;

	if ( cg.rmPerkOffer[0] >= 0 ) {
		// cards fly in one after the other
		for ( i = 0 ; i < 3 ; i++ ) {
			perk = cg.rmPerkOffer[i];
			if ( perk < 0 || perk >= PERK_NUM ) {
				continue;
			}
			t = (float)( cg.time - perkOfferTime - i * CARD_STAGGER ) / CARD_IN_TIME;
			if ( t <= 0 ) {
				continue;
			}
			e = CG_EaseOutBack( t );
			scale = 0.55f + 0.45f * e;
			alpha = CG_Clamp01( t * 2.5f );
			cx = 320 - total * 0.5f + CARD_W * 0.5f + i * ( CARD_W + CARD_GAP );
			cy = CARD_Y + CARD_H * 0.5f + 46 * ( 1.0f - e );
			CG_DrawPerkCard( i, perk, cx, cy, scale, alpha, t < 0.4f ? 0.25f * ( 1.0f - t / 0.4f ) : 0 );
		}
		// header and the draining timer
		t = (float)( cg.time - perkOfferTime ) / 250.0f;
		alpha = CG_Clamp01( t );
		col[0] = 1.0f; col[1] = 0.8f; col[2] = 0.25f; col[3] = alpha * ( 0.8f + 0.2f * sin( cg.time * 0.01f ) );
		CG_DrawStringExt( 320 - 13 * 8 / 2, CARD_Y - 19, "CHOOSE A PERK", col, qtrue, qtrue, 8, 12, 0 );
		left = perkOfferMsec - ( cg.time - perkOfferTime );
		f = CG_Clamp01( (float)left / perkOfferMsec );
		bg[0] = bg[1] = bg[2] = 0; bg[3] = 0.55f * alpha;
		CG_FillRect( 320 - total * 0.5f, CARD_Y - 6, total, 4, bg );
		if ( left < 4000 ) {
			col[0] = 1.0f; col[1] = 0.25f + 0.2f * ( ( cg.time / 150 ) & 1 ); col[2] = 0.15f;
		}
		col[3] = alpha;
		CG_FillRect( 320 - total * 0.5f + 1, CARD_Y - 5, ( total - 2 ) * f, 2, col );
	} else if ( perkPickTime && cg.time - perkPickTime < CARD_PICK_TIME ) {
		// the taken card swells and flashes, the others drop away
		t = (float)( cg.time - perkPickTime ) / CARD_PICK_TIME;
		for ( i = 0 ; i < 3 ; i++ ) {
			perk = perkShown[i];
			if ( perk < 0 || perk >= PERK_NUM ) {
				continue;
			}
			cx = 320 - total * 0.5f + CARD_W * 0.5f + i * ( CARD_W + CARD_GAP );
			cy = CARD_Y + CARD_H * 0.5f;
			if ( i == perkPickIndex ) {
				scale = 1.0f + 0.14f * sin( CG_Clamp01( t * 1.6f ) * M_PI * 0.5f );
				alpha = t < 0.6f ? 1.0f : 1.0f - ( t - 0.6f ) / 0.4f;
				flash = CG_Clamp01( 0.9f * ( 1.0f - t * 2.2f ) );
				cy -= 18 * t;
			} else {
				scale = 1.0f - 0.2f * t;
				alpha = CG_Clamp01( 1.0f - t * 2.5f );
				flash = 0;
				cy += 60 * t * t;
			}
			if ( alpha > 0 ) {
				CG_DrawPerkCard( i, perk, cx, cy, scale, alpha, flash );
			}
		}
	}

	CG_DrawPerkBuild( qfalse );
}

/*
=================
CG_DrawPerkBuild

The build on the left: "PERKS n" and one coloured pip per perk. A new perk's name shows
for a few seconds; the whole list (expanded) while the scoreboard is up.
=================
*/
#define PERK_NAME_TIME		4000

void CG_DrawPerkBuild( qboolean expanded ) {
	vec4_t	col;
	int		n, perk, line;
	float	x, y, t, f, a;

	if ( !perkOrderCount || cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
		return;
	}
	CG_HudAnchor( -1 );
	y = CG_RL_Active() ? 126 : 100;
	col[0] = col[1] = col[2] = 0.75f; col[3] = 0.7f;
	CG_DrawStringExt( 8, y, va( "PERKS %i", perkOrderCount ), col, qtrue, qtrue, 4, 6, 0 );
	x = 8 + ( perkOrderCount >= 10 ? 9 : 8 ) * 4 + 3;
	for ( n = 0 ; n < perkOrderCount ; n++ ) {
		perk = perkOrder[n];
		Vector4Copy( perkCatColors[bg_perkCategory[perk] & 3], col );
		t = (float)( cg.time - perkGainTime[perk] ) / 600.0f;
		col[3] = t < 1 ? 1.0f : 0.8f;
		CG_FillRect( x + n * 5, y, 3, t < 1 ? 7 : 6, col );
	}
	for ( line = 0, n = 0 ; n < perkOrderCount ; n++ ) {
		perk = perkOrder[n];
		t = (float)( cg.time - perkGainTime[perk] );
		if ( !expanded && t >= PERK_NAME_TIME ) {
			continue;
		}
		a = expanded ? 0.9f : t > PERK_NAME_TIME - 600 ? ( PERK_NAME_TIME - t ) / 600.0f : 1.0f;
		x = 8 - ( expanded ? 0 : 30 * ( 1.0f - CG_EaseOut( t / 400.0f ) ) );
		Vector4Copy( perkCatColors[bg_perkCategory[perk] & 3], col );
		if ( t < 2500 ) {
			// new: flashes white, settles into the category colour
			f = 1.0f - t / 2500.0f;
			col[0] = col[0] * ( 1 - f ) + f;
			col[1] = col[1] * ( 1 - f ) + f;
			col[2] = col[2] * ( 1 - f ) + f;
		} else {
			col[0] = 0.5f + 0.5f * col[0];
			col[1] = 0.5f + 0.5f * col[1];
			col[2] = 0.5f + 0.5f * col[2];
		}
		col[3] = a;
		CG_DrawStringExt( x, y + 10 + line * 8, bg_perkNames[perk], col, qtrue, qtrue, 5, 7, 0 );
		line++;
	}
	CG_HudAnchor( 0 );
}

/*
=================
CG_PerksSetOwned

rmperks <mask>: keep the build list in pick order and time the new ones
=================
*/
static void CG_PerksSetOwned( int mask ) {
	int		i, j, n;

	// drop perks we lost (new map, switch off)
	for ( i = 0, n = 0 ; i < perkOrderCount ; i++ ) {
		if ( mask & ( 1 << perkOrder[i] ) ) {
			perkOrder[n++] = perkOrder[i];
		}
	}
	perkOrderCount = n;
	for ( i = 0 ; i < PERK_NUM ; i++ ) {
		if ( !( mask & ( 1 << i ) ) ) {
			continue;
		}
		for ( j = 0 ; j < perkOrderCount ; j++ ) {
			if ( perkOrder[j] == i ) {
				break;
			}
		}
		if ( j == perkOrderCount && perkOrderCount < PERK_NUM ) {
			perkOrder[perkOrderCount++] = i;
			perkGainTime[i] = cg.time;
		}
	}
	// several at once (a new map, a descent floor starting): no name pop-up, just the pips
	if ( perkOrderCount - n > 1 ) {
		for ( j = n ; j < perkOrderCount ; j++ ) {
			perkGainTime[perkOrder[j]] = cg.time - 10000;
		}
	}
	cg.rmPerks = mask;
}

/*
===============================================================================

RECORDS: fastest win per arena and difficulty, kills per weapon (mastery)

===============================================================================
*/

#define RECORDS_FILE	"rearmed_records.txt"
#define MAX_ARENA_RECORDS	128

typedef struct {
	char	map[40];
	int		skill;
	int		msec;
} arenaRecord_t;

static arenaRecord_t	arenaRecords[MAX_ARENA_RECORDS];
static int				numArenaRecords;
static int				masteryKills[WP_NUM_WEAPONS];
static qboolean			recordsLoaded;
static int				recordThisMatch;		// msec of a new record set this match, else 0
static int				recordPrevious;

static const char *masteryNames[6] = { "", "BRONZE", "SILVER", "GOLD", "PLATINUM", "DIAMOND" };
static const int masterySteps[6] = { 0, 25, 100, 250, 500, 1000 };

static int CG_MasteryTier( int kills ) {
	int		t;

	for ( t = 5 ; t > 0 ; t-- ) {
		if ( kills >= masterySteps[t] ) {
			return t;
		}
	}
	return 0;
}

static void CG_RecordsLoad( void ) {
	fileHandle_t	f;
	int				len, w;
	char			buf[16384];
	char			*p, *tok;

	recordsLoaded = qtrue;
	numArenaRecords = 0;
	memset( masteryKills, 0, sizeof( masteryKills ) );
	len = trap_FS_FOpenFile( RECORDS_FILE, &f, FS_READ );
	if ( len <= 0 || !f ) {
		if ( f ) {
			trap_FS_FCloseFile( f );
		}
		return;
	}
	if ( len >= (int)sizeof( buf ) ) {
		len = sizeof( buf ) - 1;
	}
	trap_FS_Read( buf, len, f );
	buf[len] = 0;
	trap_FS_FCloseFile( f );

	p = buf;
	while ( 1 ) {
		tok = COM_Parse( &p );
		if ( !tok[0] ) {
			break;
		}
		if ( !Q_stricmp( tok, "mastery" ) ) {
			w = atoi( COM_Parse( &p ) );
			if ( w > 0 && w < WP_NUM_WEAPONS ) {
				masteryKills[w] = atoi( COM_Parse( &p ) );
			}
		} else if ( !Q_stricmp( tok, "arena" ) && numArenaRecords < MAX_ARENA_RECORDS ) {
			Q_strncpyz( arenaRecords[numArenaRecords].map, COM_Parse( &p ), sizeof( arenaRecords[0].map ) );
			arenaRecords[numArenaRecords].skill = atoi( COM_Parse( &p ) );
			arenaRecords[numArenaRecords].msec = atoi( COM_Parse( &p ) );
			numArenaRecords++;
		}
	}
}

static void CG_RecordsSave( void ) {
	fileHandle_t	f;
	char			*line;
	int				i;

	trap_FS_FOpenFile( RECORDS_FILE, &f, FS_WRITE );
	if ( !f ) {
		return;
	}
	line = "// REARMED personal records\n";
	trap_FS_Write( line, strlen( line ), f );
	for ( i = 1 ; i < WP_NUM_WEAPONS ; i++ ) {
		if ( masteryKills[i] ) {
			line = va( "mastery %i %i\n", i, masteryKills[i] );
			trap_FS_Write( line, strlen( line ), f );
		}
	}
	for ( i = 0 ; i < numArenaRecords ; i++ ) {
		line = va( "arena %s %i %i\n", arenaRecords[i].map, arenaRecords[i].skill, arenaRecords[i].msec );
		trap_FS_Write( line, strlen( line ), f );
	}
	trap_FS_FCloseFile( f );
}

static int CG_WeaponForMod( int mod ) {
	switch ( mod ) {
	case MOD_GAUNTLET:			return WP_GAUNTLET;
	case MOD_MACHINEGUN:		return WP_MACHINEGUN;
	case MOD_SHOTGUN:			return WP_SHOTGUN;
	case MOD_GRENADE:
	case MOD_GRENADE_SPLASH:
	case MOD_BURN:				return WP_GRENADE_LAUNCHER;
	case MOD_ROCKET:
	case MOD_ROCKET_SPLASH:		return WP_ROCKET_LAUNCHER;
	case MOD_PLASMA:
	case MOD_PLASMA_SPLASH:		return WP_PLASMAGUN;
	case MOD_RAILGUN:			return WP_RAILGUN;
	case MOD_LIGHTNING:
	case MOD_THUNDER:			return WP_LIGHTNING;
	case MOD_BFG:
	case MOD_BFG_SPLASH:		return WP_BFG;
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case MOD_NAIL:				return WP_NAILGUN;
	case MOD_CHAINGUN:			return WP_CHAINGUN;
	case MOD_PROXIMITY_MINE:	return WP_PROX_LAUNCHER;
#endif
	default:					return WP_NONE;
	}
}

static const char *CG_MapBaseName( void ) {
	static char	name[40];
	const char	*s = cgs.mapname;
	char		*dot;

	if ( !Q_stricmpn( s, "maps/", 5 ) ) {
		s += 5;
	}
	Q_strncpyz( name, s, sizeof( name ) );
	dot = strchr( name, '.' );
	if ( dot ) {
		*dot = 0;
	}
	return name;
}

/*
=================
CG_RecordsObituary

Our kills count towards weapon mastery
=================
*/
void CG_RecordsObituary( int target, int attacker, int mod ) {
	int		w, before, after;
	gitem_t	*item;

	if ( !recordsLoaded ) {
		CG_RecordsLoad();
	}
	if ( attacker != cg.snap->ps.clientNum || target == attacker || cg.warmup || CG_DS_NoRecords() ) {
		return;		// (en-boss: boss practice counts no mastery)
	}
	w = CG_WeaponForMod( mod );
	if ( w <= WP_NONE ) {
		return;
	}
	before = CG_MasteryTier( masteryKills[w] );
	masteryKills[w]++;
	after = CG_MasteryTier( masteryKills[w] );
	if ( after > before ) {
		item = BG_FindItemForWeapon( w );
		Q_strncpyz( cg.rmMedal, va( "%s MASTERY: %s", item ? item->pickup_name : "WEAPON", masteryNames[after] ), sizeof( cg.rmMedal ) );
		Q_strupr( cg.rmMedal );
		cg.rmMedalTime = cg.time;
		cg.rmMedalKind = 0;
		trap_S_StartLocalSound( masterySound, CHAN_ANNOUNCER );
	}
	if ( masteryKills[w] % 5 == 0 || after > before ) {
		CG_RecordsSave();
	}
}

static int CG_SpSkill( void ) {
	char	buf[16];

	trap_Cvar_VariableStringBuffer( "g_spSkill", buf, sizeof( buf ) );
	return atoi( buf );
}

static const char *CG_SkillName( int skill ) {
	static const char *names[6] = { "", "I CAN WIN", "BRING IT ON", "HURT ME PLENTY", "HARDCORE", "NIGHTMARE" };
	return ( skill >= 1 && skill <= 5 ) ? names[skill] : "CUSTOM";
}

static arenaRecord_t *CG_FindArenaRecord( const char *map, int skill ) {
	int		i;

	for ( i = 0 ; i < numArenaRecords ; i++ ) {
		if ( arenaRecords[i].skill == skill && !Q_stricmp( arenaRecords[i].map, map ) ) {
			return &arenaRecords[i];
		}
	}
	return NULL;
}

/*
=================
CG_RecordsFrame

At the end of a match: did we win, and was it the fastest win on this arena at this difficulty?
=================
*/
void CG_RecordsFrame( void ) {
	arenaRecord_t	*rec;
	const char		*map;
	int				msec, skill, rank;

	if ( !cg.snap ) {
		return;
	}
	CG_MutatorsFrame();		// random event sounds / world FX, tips (cg_mutators.c)
	if ( !recordsLoaded ) {
		CG_RecordsLoad();
	}
	if ( cg.snap->ps.pm_type != PM_INTERMISSION ) {
		cg.rmRecordChecked = qfalse;
		return;
	}
	if ( cg.rmRecordChecked ) {
		return;
	}
	cg.rmRecordChecked = qtrue;
	recordThisMatch = 0;
	if ( CG_DS_NoRecords() ) {		// (en-daily: the daily neither)
		return;		// en-boss: boss practice sets no records
	}

	rank = cg.snap->ps.persistant[PERS_RANK];
	if ( cgs.gametype >= GT_TEAM || cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR
		|| ( rank & RANK_TIED_FLAG ) || rank != 0 || cg.snap->ps.persistant[PERS_SCORE] <= 0 ) {
		return;
	}
	msec = cg.time - cgs.levelStartTime;
	skill = CG_SpSkill();
	map = CG_MapBaseName();
	rec = CG_FindArenaRecord( map, skill );
	if ( rec && rec->msec <= msec ) {
		return;
	}
	recordPrevious = rec ? rec->msec : 0;
	if ( !rec ) {
		if ( numArenaRecords >= MAX_ARENA_RECORDS ) {
			return;
		}
		rec = &arenaRecords[numArenaRecords++];
		Q_strncpyz( rec->map, map, sizeof( rec->map ) );
		rec->skill = skill;
	}
	rec->msec = msec;
	recordThisMatch = msec;
	CG_RecordsSave();
	trap_S_StartLocalSound( newRecordSound, CHAN_ANNOUNCER );
}

/*
=================
CG_RecordsBossKill

en-boss: the fastest timed kill per DESCENT boss and records difficulty, kept as an "arena"
line named boss_<n> (the "records" command lists it). Returns the old record (msec, 0 none);
*isNew when this kill beat it (and it was saved).
=================
*/
int CG_RecordsBossKill( int boss, int skill, int msec, int *isNew ) {
	arenaRecord_t	*rec;
	const char		*name = va( "boss_%i", boss );
	int				prev;

	*isNew = 0;
	if ( !recordsLoaded ) {
		CG_RecordsLoad();
	}
	if ( msec <= 0 || CG_DS_NoRecords() ) {
		return 0;
	}
	rec = CG_FindArenaRecord( name, skill );
	prev = rec ? rec->msec : 0;
	if ( rec && rec->msec <= msec ) {
		return prev;
	}
	if ( !rec ) {
		if ( numArenaRecords >= MAX_ARENA_RECORDS ) {
			return 0;
		}
		rec = &arenaRecords[numArenaRecords++];
		Q_strncpyz( rec->map, name, sizeof( rec->map ) );
		rec->skill = skill;
	}
	rec->msec = msec;
	*isNew = 1;
	CG_RecordsSave();
	trap_S_StartLocalSound( newRecordSound, CHAN_ANNOUNCER );
	return prev;
}

/*
=================
CG_DrawRecordsIntermission

The arena record line on the end-of-match screen
=================
*/
void CG_DrawRecordsIntermission( void ) {
	arenaRecord_t	*rec;
	const char		*s;
	vec4_t			color;
	int				skill, w;

	if ( !recordsLoaded || cgs.gametype >= GT_TEAM ) {
		return;
	}
	skill = CG_SpSkill();
	rec = CG_FindArenaRecord( CG_MapBaseName(), skill );
	color[0] = 1.0f; color[1] = 0.8f; color[2] = 0.25f; color[3] = 1.0f;
	if ( recordThisMatch ) {
		if ( recordPrevious ) {
			s = va( "NEW ARENA RECORD %i:%02i  (was %i:%02i)  %s", recordThisMatch / 60000, ( recordThisMatch / 1000 ) % 60,
				recordPrevious / 60000, ( recordPrevious / 1000 ) % 60, CG_SkillName( skill ) );
		} else {
			s = va( "FIRST WIN HERE: %i:%02i  %s", recordThisMatch / 60000, ( recordThisMatch / 1000 ) % 60, CG_SkillName( skill ) );
		}
		if ( ( cg.time >> 8 ) & 1 ) {
			color[3] = 0.7f;
		}
	} else if ( rec ) {
		s = va( "ARENA RECORD (%s): %i:%02i", CG_SkillName( skill ), rec->msec / 60000, ( rec->msec / 1000 ) % 60 );
		color[0] = color[1] = color[2] = 0.85f;
	} else {
		return;
	}
	w = CG_DrawStrlen( s ) * 8;
	CG_DrawStringExt( 320 - w / 2, 452, s, color, qtrue, qtrue, 8, 12, 0 );
}

/*
=================
CG_Records_f

"records" console command
=================
*/
void CG_Records_f( void ) {
	int		i;
	gitem_t	*item;

	if ( !recordsLoaded ) {
		CG_RecordsLoad();
	}
	CG_Printf( "^3Weapon mastery^7\n" );
	for ( i = 1 ; i < WP_NUM_WEAPONS ; i++ ) {
		if ( masteryKills[i] ) {
			item = BG_FindItemForWeapon( i );
			CG_Printf( "  %-20s %5i kills  %s\n", item ? item->pickup_name : "?", masteryKills[i], masteryNames[CG_MasteryTier( masteryKills[i] )] );
		}
	}
	CG_Printf( "^3Fastest wins^7\n" );
	for ( i = 0 ; i < numArenaRecords ; i++ ) {
		CG_Printf( "  %-16s %-15s %i:%02i\n", arenaRecords[i].map, CG_SkillName( arenaRecords[i].skill ),
			arenaRecords[i].msec / 60000, ( arenaRecords[i].msec / 1000 ) % 60 );
	}
}

/*
===============================================================================

SERVER COMMANDS

===============================================================================
*/

/*
=================
CG_StyleServerCommand

rmsty <points> <label>, rmperk <a> <b> <c> [msec], rmperkgot <perk>, rmperks <mask>
=================
*/
qboolean CG_StyleServerCommand( const char *cmd ) {
	int		i, perk;

	if ( !strcmp( cmd, "rmsty" ) ) {
		CG_StyleFeedAdd( atoi( CG_Argv( 1 ) ), CG_Argv( 2 ) );
		return qtrue;
	}
	if ( !strcmp( cmd, "rmperk" ) ) {
		for ( i = 0 ; i < 3 ; i++ ) {
			cg.rmPerkOffer[i] = atoi( CG_Argv( 1 + i ) );
		}
		if ( cg.rmPerkOffer[0] >= 0 ) {
			for ( i = 0 ; i < 3 ; i++ ) {
				perkShown[i] = cg.rmPerkOffer[i];
			}
			perkOfferTime = cg.time;
			perkOfferMsec = atoi( CG_Argv( 4 ) );
			if ( perkOfferMsec <= 0 ) {
				perkOfferMsec = 15000;
			}
			perkPickIndex = -1;
			perkPickTime = 0;
			trap_S_StartLocalSound( perkOfferSound, CHAN_ANNOUNCER );
			trap_S_StartLocalSound( perkCardSound, CHAN_LOCAL_SOUND );
			CG_Tip( TIP_PERK );
		}
		return qtrue;
	}
	if ( !strcmp( cmd, "rmperkgot" ) ) {
		// the server granted a perk: animate the card it came from (also for the auto pick)
		perk = atoi( CG_Argv( 1 ) );
		if ( perkPickIndex < 0 || !perkPickTime || cg.time - perkPickTime > 1000 ) {
			for ( i = 0 ; i < 3 ; i++ ) {
				if ( perkShown[i] == perk ) {
					perkPickIndex = i;
					perkPickTime = cg.time;
					trap_S_StartLocalSound( perkPickSound, CHAN_LOCAL_SOUND );
					break;
				}
			}
		}
		cg.rmPerkOffer[0] = -1;
		return qtrue;
	}
	if ( !strcmp( cmd, "rmperks" ) ) {
		CG_PerksSetOwned( atoi( CG_Argv( 1 ) ) );
		return qtrue;
	}
	return qfalse;
}
