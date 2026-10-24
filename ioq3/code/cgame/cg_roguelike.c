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
// cg_roguelike.c -- DESCENT (roguelike mode): run HUD, floor intro with the enemy kits,
// elite / boss auras and name tags, floor cleared / failed banners, the reward card offer
// (keys 1-4, or weapnext / weapprev + fire, jump rerolls), "ABILITY UNLOCKED" flash.
// The server state comes in CS_DESCENT and CS_DESCENTKITS (g_roguelike.c).

#include "cg_local.h"

#define RLN_FIGHT		0
#define RLN_ELITE		1
#define RLN_BOSS		2
#define RLN_EVENT		3

#define RLR_PLAYER		1
#define RLR_BOT			2
#define RLR_ELITE		3
#define RLR_BOSS		4

#define RLP_PLAY		0
#define RLP_END			1
#define RLP_CHEST		2		// DESCENT v2: the jackpot screen (cg_ds_loot.c)
#define RLP_OFFER		3
#define RLP_DONE		4
#define RLP_SENT		5
#define RL_MAX_OFFER	5

#define INTRO_TIME		3600		// the slim floor strip at the top (the full kit list is on the scoreboard)
#define INTRO_FADE		700
#define GOT_TIME		2800
#define RLCARD_W		140
#define RLCARD_H		196
#define RLCARD_GAP		16
#define RLCARD_Y		156
#define RLCARD_IN		520
#define RLCARD_STAGGER	150

typedef struct {
	qboolean	on;
	int			floorPlayed, cleared, lives, skulls, dif, nodeType, frags, mut, tier, rerolls;
	int			phase, result, reward, rb[5], picked, levelKills, runOver, banked, nbFloor, nbKills, kills, human;
	int			offer[RL_MAX_OFFER], numOffer;
	int			cards[RL_MAX_CARDS], numCards;
	int			rewardMult, bossPct, bossSkulls;	// ECONOMY: multiplier x100, boss bar % (-1 none), boss skulls
} cgRun_t;

static cgRun_t		rlc;
static int			rlcDaily;		// en-daily: a floor of the daily challenge (CS_DESCENT \dly\)
static int			kitRole[MAX_CLIENTS];

// en-daily: a daily challenge floor (cg_style.c keeps no records / mastery there)
qboolean CG_RL_Daily( void ) {
	return rlc.on && rlcDaily;
}

// en-review: boss practice / rematch or the daily: no records, no mastery
qboolean CG_DS_NoRecords( void ) {
	return CG_DS_Practice() || CG_RL_Daily();
}
static int			kitNum[MAX_CLIENTS];
static int			kitCards[MAX_CLIENTS][RL_MAX_CARDS];

static int			introStart;
static int			phaseStart;			// cg.time the phase last changed
static int			offerStart;			// cg.time this offer appeared (reroll restarts it)
static int			offerSig;			// detects a reroll
static int			endBannerStart;
static int			gotCard = -1, gotTime;
static int			pickSlot = -1, pickTime;
static int			shownOffer[RL_MAX_OFFER], shownNum;
static int			sel;				// highlighted card
static int			lastButtons, lastUp, lastCmd;
static int			skullTickNext, skullShown;

static sfxHandle_t	sndIntro, sndCleared, sndFailed, sndRunOver, sndCard, sndTake, sndUnlock, sndSkull;
static qhandle_t	eliteShell, bossShell;

static const vec4_t rlKindColors[8] = {
	{ 0.30f, 0.95f, 0.42f, 1 },		// ability
	{ 1.00f, 0.55f, 0.12f, 1 },		// alt-fire
	{ 0.32f, 0.62f, 1.00f, 1 },		// stat
	{ 0.78f, 0.42f, 1.00f, 1 },		// perk
	{ 0.25f, 0.90f, 0.85f, 1 },		// passive (v2)
	{ 1.00f, 0.42f, 0.30f, 1 },		// weapon level (v2)
	{ 1.00f, 0.84f, 0.25f, 1 },		// secret (v2)
	{ 1.00f, 0.62f, 0.10f, 1 }		// legendary (round 3, ds-cards)
};
static const char *rlKindNames[8] = { "ABILITY", "ALT-FIRE", "STAT", "PERK", "PASSIVE", "WEAPON", "SECRET", "LEGENDARY" };
static const char *rlRarityNames[4] = { "COMMON", "RARE", "EPIC", "LEGENDARY" };
static const char *rlNodeNames[5] = { "FIGHT", "ELITE", "BOSS", "EVENT", "VAULT" };		// 4: ACTS vault
static const char *rlDifNames[6] = { "", "I CAN WIN", "BRING IT ON", "HURT ME PLENTY", "HARDCORE", "NIGHTMARE" };
static const char *rlStyleLetters[8] = { "-", "D", "C", "B", "A", "S", "SS", "SSS" };

qboolean CG_RL_Active( void ) {
	return rlc.on;
}

// RLR_ELITE (3) / RLR_BOSS (4) for the crosshair name, 0 outside a descent
int CG_RL_ClientRole( int clientNum ) {
	if ( !rlc.on || clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return 0;
	}
	return kitRole[clientNum];
}

void CG_RL_RoleColor( int role, vec4_t out ) {
	if ( role == RLR_BOSS ) {
		Vector4Copy( rlKindColors[3], out );
	} else {
		Vector4Copy( rlKindColors[1], out );
	}
}

/*
===============================================================================

STATE

===============================================================================
*/

static int CG_RL_ParseList( const char *s, int *out, int max ) {
	int		n = 0;

	while ( *s && n < max ) {
		while ( *s == '.' || *s == ',' ) {
			s++;
		}
		if ( !*s || *s == ' ' ) {
			break;
		}
		out[n++] = atoi( s );
		while ( *s && *s != '.' && *s != ',' && *s != ' ' ) {
			s++;
		}
	}
	return n;
}

static void CG_RL_ParseKits( void ) {
	const char	*s = CG_ConfigString( CS_DESCENTKITS );
	int			cl;

	memset( kitRole, 0, sizeof( kitRole ) );
	memset( kitNum, 0, sizeof( kitNum ) );
	while ( *s ) {
		while ( *s == ' ' ) {
			s++;
		}
		if ( !*s ) {
			break;
		}
		cl = atoi( s );
		while ( *s && *s != ':' ) {
			s++;
		}
		if ( *s ) {
			s++;
		}
		if ( cl < 0 || cl >= MAX_CLIENTS ) {
			break;
		}
		kitRole[cl] = atoi( s );
		while ( *s && *s != ':' ) {
			s++;
		}
		if ( *s ) {
			s++;
		}
		kitNum[cl] = CG_RL_ParseList( s, kitCards[cl], RL_MAX_CARDS );
		while ( *s && *s != ' ' ) {
			s++;
		}
	}
}

/*
=================
CG_RL_ParseConfigstrings

CS_DESCENT / CS_DESCENTKITS changed (and at init)
=================
*/
void CG_RL_ParseConfigstrings( void ) {
	const char	*info = CG_ConfigString( CS_DESCENT );
	int			oldPhase = rlc.phase, oldOn = rlc.on, sig, i;
	int			rb[5];

	CG_RL_ParseKits();
	if ( !info[0] || atoi( Info_ValueForKey( info, "on" ) ) != 1 ) {
		rlc.on = qfalse;
		return;
	}
	rlc.on = qtrue;
	rlc.floorPlayed = atoi( Info_ValueForKey( info, "fl" ) );
	rlc.cleared = atoi( Info_ValueForKey( info, "cl" ) );
	rlc.lives = atoi( Info_ValueForKey( info, "lv" ) );
	rlc.skulls = atoi( Info_ValueForKey( info, "sk" ) );
	rlc.dif = atoi( Info_ValueForKey( info, "dif" ) );
	rlc.nodeType = atoi( Info_ValueForKey( info, "nt" ) );
	if ( rlc.nodeType < 0 || rlc.nodeType > 4 ) {
		rlc.nodeType = 0;
	}
	rlc.frags = atoi( Info_ValueForKey( info, "fr" ) );
	rlc.mut = atoi( Info_ValueForKey( info, "mut" ) );
	rlc.tier = atoi( Info_ValueForKey( info, "tier" ) );
	rlc.rerolls = atoi( Info_ValueForKey( info, "rr" ) );
	rlc.phase = atoi( Info_ValueForKey( info, "ph" ) );
	rlc.result = atoi( Info_ValueForKey( info, "res" ) );
	rlc.numOffer = CG_RL_ParseList( Info_ValueForKey( info, "of" ), rlc.offer, RL_MAX_OFFER );
	rlc.reward = atoi( Info_ValueForKey( info, "rw" ) );
	for ( i = 0 ; i < 5 ; i++ ) {
		rb[i] = 0;
	}
	CG_RL_ParseList( Info_ValueForKey( info, "rb" ), rb, 5 );
	for ( i = 0 ; i < 5 ; i++ ) {
		rlc.rb[i] = rb[i];
	}
	rlc.picked = atoi( Info_ValueForKey( info, "pk" ) );
	rlc.rewardMult = Info_ValueForKey( info, "rm" )[0] ? atoi( Info_ValueForKey( info, "rm" ) ) : 100;	// ECONOMY
	rlc.bossPct = Info_ValueForKey( info, "bd" )[0] ? atoi( Info_ValueForKey( info, "bd" ) ) : -1;
	rlc.bossSkulls = atoi( Info_ValueForKey( info, "bs" ) );
	rlc.levelKills = atoi( Info_ValueForKey( info, "lk" ) );
	rlc.runOver = atoi( Info_ValueForKey( info, "ov" ) );
	rlc.banked = atoi( Info_ValueForKey( info, "bk" ) );
	rlcDaily = atoi( Info_ValueForKey( info, "dly" ) );		// en-daily
	CG_RL_ParseList( Info_ValueForKey( info, "nb" ), rb, 2 );
	rlc.nbFloor = rb[0];
	rlc.nbKills = rb[1];
	rlc.kills = atoi( Info_ValueForKey( info, "kl" ) );
	rlc.human = atoi( Info_ValueForKey( info, "hc" ) );
	rlc.numCards = CG_RL_ParseList( Info_ValueForKey( info, "cards" ), rlc.cards, RL_MAX_CARDS );

	if ( !oldOn ) {
		introStart = 0;
		gotCard = -1;
		pickSlot = -1;
		shownNum = 0;
		offerSig = 0;
		endBannerStart = 0;
	}
	if ( rlc.phase != oldPhase || !oldOn ) {
		phaseStart = cg.time;
		if ( rlc.phase == RLP_END && !endBannerStart ) {
			endBannerStart = cg.time;
			trap_S_StartLocalSound( rlc.result == 1 ? sndCleared : rlc.lives > 0 ? sndFailed : sndRunOver, CHAN_ANNOUNCER );
		}
	}
	// a new offer (first one or a reroll): deal the cards again
	sig = rlc.phase == RLP_OFFER ? 1 + rlc.offer[0] * 7 + rlc.offer[1] * 131 + rlc.offer[2] * 1031 + rlc.rerolls * 9973 : 0;
	if ( sig && sig != offerSig ) {
		offerStart = cg.time;
		sel = 0;
		pickSlot = -1;
		skullShown = 0;
		skullTickNext = 0;
		for ( i = 0 ; i < rlc.numOffer ; i++ ) {
			shownOffer[i] = rlc.offer[i];
		}
		shownNum = rlc.numOffer;
		trap_S_StartLocalSound( sndCard, CHAN_LOCAL_SOUND );
	}
	offerSig = sig;
}

void CG_RL_Register( void ) {
	sndIntro = trap_S_RegisterSound( "sound/rearmed/rl/rl_intro.wav", qfalse );
	sndCleared = trap_S_RegisterSound( "sound/rearmed/rl/rl_cleared.wav", qfalse );
	sndFailed = trap_S_RegisterSound( "sound/rearmed/rl/rl_failed.wav", qfalse );
	sndRunOver = trap_S_RegisterSound( "sound/rearmed/rl/rl_runover.wav", qfalse );
	sndCard = trap_S_RegisterSound( "sound/rearmed/rl/rl_card.wav", qfalse );
	sndTake = trap_S_RegisterSound( "sound/rearmed/rl/rl_take.wav", qfalse );
	sndUnlock = trap_S_RegisterSound( "sound/rearmed/rl/rl_unlock.wav", qfalse );
	sndSkull = trap_S_RegisterSound( "sound/rearmed/rl/rl_skull.wav", qfalse );
	eliteShell = trap_R_RegisterShader( "rearmed/rlEliteShell" );
	CG_DSC_Register();		// CARDS: the reveal, legendaries, the tally (cg_ds_cards.c)
	bossShell = trap_R_RegisterShader( "rearmed/rlBossShell" );
	memset( &rlc, 0, sizeof( rlc ) );
	introStart = 0;
	gotCard = -1;
}

/*
=================
CG_RL_ServerCommand

rlgot <card>: the card just taken (unlock flash). rlend <result>.
=================
*/
qboolean CG_RL_ServerCommand( const char *cmd ) {
	if ( CG_DSC_ServerCommand( cmd ) ) {		// CARDS: dsctally / dsclg
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "rlgot" ) ) {
		gotCard = atoi( CG_Argv( 1 ) );
		gotTime = cg.time;
		if ( gotCard >= 0 && gotCard < RLC_NUM ) {
			trap_S_StartLocalSound( bg_rlCardKind[gotCard] == RLK_ABILITY ? sndUnlock : sndTake, CHAN_ANNOUNCER );
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "rlend" ) ) {
		if ( !endBannerStart ) {
			endBannerStart = cg.time;
		}
		return qtrue;
	}
	return qfalse;
}

/*
===============================================================================

PICKING

===============================================================================
*/

static void CG_RL_SendPick( int slot ) {
	if ( rlc.phase != RLP_OFFER || pickSlot >= 0 || slot < 0 || slot >= rlc.numOffer ) {
		return;
	}
	if ( CG_DSC_RevealOn() ? CG_DSC_RevealLeft( offerStart, shownNum, shownOffer ) > 0 : cg.time - offerStart < RLCARD_IN ) {
		CG_DSC_RevealSkip( offerStart );		// CARDS: a pick key during the reveal flips them all
		return;		// let them land first
	}
	CG_DSC_OnPick( rlc.offer[slot], 320 + ( slot - ( rlc.numOffer - 1 ) * 0.5f ) * ( RLCARD_W + RLCARD_GAP ), RLCARD_Y + RLCARD_H * 0.5f - 24 );	// CARDS: the synergy ding
	trap_SendClientCommand( va( "rlpick %i", slot + 1 ) );
	pickSlot = slot;
	pickTime = cg.time;
	trap_S_StartLocalSound( sndTake, CHAN_LOCAL_SOUND );
}

// weapon 1..4 while the cards are up
qboolean CG_RL_Pick( int num ) {
	if ( !rlc.on || rlc.phase != RLP_OFFER ) {
		return qfalse;
	}
	if ( num >= 1 && num <= rlc.numOffer ) {
		CG_RL_SendPick( num - 1 );
	}
	return qtrue;
}

// weapnext / weapprev move the highlight
qboolean CG_RL_Cycle( int dir ) {
	if ( !rlc.on || rlc.phase != RLP_OFFER || rlc.numOffer <= 0 || pickSlot >= 0 ) {
		return qfalse;
	}
	sel = ( sel + dir + rlc.numOffer ) % rlc.numOffer;
	trap_S_StartLocalSound( sndSkull, CHAN_LOCAL_SOUND );
	return qtrue;
}

// fire takes the highlighted card, jump rerolls (gamepads)
static void CG_RL_ReadButtons( void ) {
	usercmd_t	cmd;
	int			n = trap_GetCurrentCmdNumber();
	int			pressed, up;

	if ( n == lastCmd || !trap_GetUserCmd( n, &cmd ) ) {
		return;
	}
	lastCmd = n;
	pressed = cmd.buttons & ~lastButtons;
	up = cmd.upmove > 0 && lastUp <= 0;
	lastButtons = cmd.buttons;
	lastUp = cmd.upmove;
	if ( rlc.phase == RLP_OFFER && pickSlot < 0 && CG_DSC_RevealOn() && CG_DSC_RevealLeft( offerStart, shownNum, shownOffer ) > -250 ) {
		if ( ( ( pressed & BUTTON_ATTACK ) || up ) && CG_DSC_RevealLeft( offerStart, shownNum, shownOffer ) > 0 ) {
			CG_DSC_RevealSkip( offerStart );		// CARDS: fire / jump during the reveal flips them all
		}
		return;
	}
	if ( rlc.phase != RLP_OFFER || pickSlot >= 0 || ( !CG_DSC_RevealOn() && cg.time - offerStart < RLCARD_IN + 250 ) ) {
		return;
	}
	if ( pressed & BUTTON_ATTACK ) {
		CG_RL_SendPick( sel );
	} else if ( up && rlc.rerolls > 0 ) {
		trap_SendClientCommand( "rlreroll" );
	}
}

/*
===============================================================================

DRAW HELPERS

===============================================================================
*/

static void CG_RL_Text( float x, float y, const char *s, const float *color, float cw, float ch, int align, qboolean shadow ) {
	float	w = CG_DrawStrlen( s ) * cw;

	if ( align == 0 ) {
		x -= w * 0.5f;
	} else if ( align > 0 ) {
		x -= w;
	}
	CG_DrawStringExt( (int)x, (int)y, s, color, qtrue, shadow, (int)( cw + 0.5f ), (int)( ch + 0.5f ), 0 );
}

static void CG_RL_Mix( vec4_t out, const float *a, float f, float alpha ) {
	out[0] = a[0] + ( 1 - a[0] ) * f;
	out[1] = a[1] + ( 1 - a[1] ) * f;
	out[2] = a[2] + ( 1 - a[2] ) * f;
	out[3] = alpha;
}

static float CG_RL_EaseOutBack( float t ) {
	float	s = 1.70158f;

	t = CG_Clamp01( t ) - 1.0f;
	return t * t * ( ( s + 1 ) * t + s ) + 1.0f;
}

static float CG_RL_EaseOut( float t ) {
	t = 1.0f - CG_Clamp01( t );
	return 1.0f - t * t * t;
}

static int CG_RL_Kind( int c ) {
	if ( c < 0 || c >= RLC_NUM || bg_rlCardKind[c] < 0 || bg_rlCardKind[c] > 7 ) {
		return 0;
	}
	return bg_rlCardKind[c];
}

int CG_RL_CardCount( int card ) {
	int		i, n = 0;

	for ( i = 0 ; i < rlc.numCards ; i++ ) {
		if ( rlc.cards[i] == card ) {
			n++;
		}
	}
	return n;
}

/*
=================
CG_RL_DrawChips

A kit as coloured tags: abilities, alt-fires, stats with their stacks, the perk count.
Returns the width drawn.
=================
*/
static float CG_RL_DrawChips( float x, float y, const int *cards, int num, float ch, float alpha, float maxW ) {
	int			count[RLC_NUM];
	int			c, i, perks = 0;
	float		cw = ch * 0.7f, w, x0 = x;
	char		tag[16];
	vec4_t		col, dark;

	memset( count, 0, sizeof( count ) );
	for ( i = 0 ; i < num ; i++ ) {
		if ( cards[i] >= 0 && cards[i] < RLC_NUM ) {
			count[cards[i]]++;
		}
	}
	if ( cards == rlc.cards ) {
		for ( i = 0 ; i < RLP_NUM ; i++ ) {		// the player's passives: their real levels (chests too)
			count[RLC_PASSIVE_FIRST + i] = CG_DS_PassiveLevel( i );
		}
	}
	dark[0] = dark[1] = dark[2] = 0.03f; dark[3] = alpha;
	for ( c = 0 ; c < RLC_NUM ; c++ ) {
		if ( !count[c] ) {
			continue;
		}
		if ( bg_rlCardKind[c] == RLK_PERK ) {
			perks += count[c];
			continue;
		}
		if ( count[c] > 1 ) {
			Com_sprintf( tag, sizeof( tag ), "%s%i", bg_rlCardShort[c], count[c] );
		} else {
			Q_strncpyz( tag, bg_rlCardShort[c], sizeof( tag ) );
		}
		w = strlen( tag ) * cw + 4;
		if ( maxW > 0 && x + w - x0 > maxW ) {
			break;
		}
		Vector4Copy( rlKindColors[CG_RL_Kind( c )], col );
		col[3] = 0.85f * alpha;
		CG_FillRect( x, y, w, ch + 2, col );
		CG_RL_Text( x + 2, y + 1, tag, dark, cw, ch, -1, qfalse );
		x += w + 2;
	}
	if ( perks ) {
		Com_sprintf( tag, sizeof( tag ), "%i PERK%s", perks, perks > 1 ? "S" : "" );
		w = strlen( tag ) * cw + 4;
		if ( maxW <= 0 || x + w - x0 <= maxW ) {
			Vector4Copy( rlKindColors[RLK_PERK], col );
			col[3] = 0.85f * alpha;
			CG_FillRect( x, y, w, ch + 2, col );
			CG_RL_Text( x + 2, y + 1, tag, dark, cw, ch, -1, qfalse );
			x += w + 2;
		}
	}
	if ( x == x0 ) {
		col[0] = col[1] = col[2] = 0.6f; col[3] = 0.8f * alpha;
		CG_RL_Text( x, y + 1, "NO CARDS", col, cw, ch, -1, qfalse );
		x += 8 * cw;
	}
	return x - x0;
}

/*
=================
CG_RL_Project

World point to virtual 640x480 screen coordinates
=================
*/
static qboolean CG_RL_Project( const vec3_t world, float *x, float *y ) {
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
===============================================================================

AURAS (cg_players.c hooks)

===============================================================================
*/

void CG_RL_BodyShell( refEntity_t *ent, entityState_t *state ) {
	if ( state->eFlags & EF_DEAD ) {
		return;
	}
	CG_DSC_BodyShell( ent, state );		// CARDS: the DOUBLE AGENT's green shell
	if ( CG_DS_BodyShell( ent, state ) ) {
		return;		// ds-boss: an armoured act boss (the nemesis's rim adds to an elite aura)
	}
	if ( ( state->generic1 & RM_BODY_BOSS ) && state->clientNum == CG_DS_BossClient() && state->eType == ET_PLAYER ) {
		return;		// ACTS: an act boss is told by its size, its bar and its own glow (cg_ds_acts.c)
	}
	if ( state->generic1 & RM_BODY_BOSS ) {
		ent->customShader = bossShell;
		trap_R_AddRefEntityToScene( ent );
	} else if ( state->generic1 & RM_BODY_ELITE ) {
		ent->customShader = eliteShell;
		trap_R_AddRefEntityToScene( ent );
	}
}

void CG_RL_PlayerFX( centity_t *cent ) {
	entityState_t	*s = &cent->currentState;
	float			pulse;
	vec3_t			org;

	if ( s->eFlags & EF_DEAD || !( s->generic1 & ( RM_BODY_ELITE | RM_BODY_BOSS ) ) ) {
		return;
	}
	VectorCopy( cent->lerpOrigin, org );
	org[2] += 8;
	pulse = 0.75f + 0.25f * sin( cg.time * 0.006f + s->number );
	if ( s->generic1 & RM_BODY_BOSS ) {
		trap_R_AddLightToScene( org, 230 * pulse, 0.7f, 0.15f, 1.0f );
	} else {
		trap_R_AddLightToScene( org, 170 * pulse, 1.0f, 0.45f, 0.08f );
	}
}

/*
=================
CG_RL_DrawNameTags

ELITE / BOSS over their heads (when in sight). cg_enemyTags: 0 = none (the orange / purple
aura says it), 1 = a small ELITE / BOSS label, 2 = label, name and kit
=================
*/
static void CG_RL_DrawNameTags( void ) {
	centity_t	*cent;
	trace_t		tr;
	vec3_t		head;
	vec4_t		col, bg;
	float		x, y, dist, ch, w;
	int			i, role;
	const char	*label;

	if ( cg_enemyTags.integer <= 0 ) {
		return;
	}
	for ( i = 0 ; i < MAX_CLIENTS ; i++ ) {
		role = kitRole[i];
		if ( role != RLR_ELITE && role != RLR_BOSS ) {
			continue;
		}
		if ( i == cg.snap->ps.clientNum || !cgs.clientinfo[i].infoValid ) {
			continue;
		}
		cent = &cg_entities[i];
		if ( !cent->currentValid || ( cent->currentState.eFlags & EF_DEAD ) ) {
			continue;
		}
		VectorCopy( cent->lerpOrigin, head );
		head[2] += 52;
		dist = Distance( head, cg.refdef.vieworg );
		if ( dist > 2400 ) {
			continue;
		}
		CG_Trace( &tr, cg.refdef.vieworg, NULL, NULL, head, cg.snap->ps.clientNum, CONTENTS_SOLID );
		if ( tr.fraction < 1.0f ) {
			continue;
		}
		if ( !CG_RL_Project( head, &x, &y ) ) {
			continue;
		}
		ch = 3000.0f / ( dist + 200 );
		if ( ch < 5 ) {
			ch = 5;
		} else if ( ch > 10 ) {
			ch = 10;
		}
		label = role == RLR_BOSS ? "BOSS" : "ELITE";
		Vector4Copy( role == RLR_BOSS ? rlKindColors[RLK_PERK] : rlKindColors[RLK_ALT], col );
		if ( cg_enemyTags.integer == 1 ) {
			// minimal: just the small label, a little above the head
			ch *= 0.6f;
			col[3] = 0.7f;
			CG_RL_Text( x, y - ch * 1.6f, label, col, ch * 0.9f, ch * 1.2f, 0, qtrue );
			continue;
		}
		col[3] = 0.95f;
		CG_RL_Text( x, y - ch * 2.6f, label, col, ch * 0.9f, ch * 1.2f, 0, qtrue );
		col[0] = col[1] = col[2] = 1; col[3] = 0.9f;
		CG_RL_Text( x, y - ch * 1.25f, cgs.clientinfo[i].name, col, ch * 0.6f, ch * 0.85f, 0, qtrue );
		if ( dist < 1400 && kitNum[i] ) {
			bg[0] = bg[1] = bg[2] = 0; bg[3] = 0.4f;
			w = 34 * ch * 0.5f;
			CG_FillRect( x - w * 0.5f - 1, y - 1, w + 2, ch * 0.7f + 4, bg );
			CG_RL_DrawChips( x - w * 0.5f, y, kitCards[i], kitNum[i], ch * 0.7f, 0.9f, w );
		}
	}
}

/*
===============================================================================

RUN HUD, INTRO, BANNERS

===============================================================================
*/

static void CG_RL_DrawLives( float x, float y, float size, int lives, float alpha ) {
	vec4_t	col;
	int		i;

	col[0] = 1; col[1] = 0.18f; col[2] = 0.15f; col[3] = alpha;
	for ( i = 0 ; i < lives && i < 8 ; i++ ) {
		CG_FillRect( x + i * ( size + 3 ), y, size, size, col );
	}
}

// the node's name and colour: "ELITE", "BOSS", "EVENT: LOW GRAVITY", "FIGHT"
static const char *CG_RL_NodeLabel( vec4_t color ) {
	if ( color ) {
		Vector4Copy( rlc.nodeType == RLN_BOSS ? rlKindColors[RLK_PERK] : rlc.nodeType == RLN_ELITE ? rlKindColors[RLK_ALT]
			: rlc.nodeType == RLN_EVENT ? rlKindColors[RLK_STAT] : rlKindColors[RLK_ABILITY], color );
	}
	if ( rlc.nodeType == RLN_EVENT && rlc.mut > 0 && rlc.mut < MUT_NUM_MUTATORS ) {
		return va( "EVENT: %s", bg_rmMutatorNames[rlc.mut] );
	}
	return rlNodeNames[rlc.nodeType];
}

/*
=================
CG_RL_DrawHud

Compact corner block: floor, node, lives, frags, skulls. The kits are on the scoreboard.
=================
*/
static void CG_RL_DrawHud( void ) {
	vec4_t		col, bg;
	char		title[32], node[48];
	float		x = 8, y = 100, tw, nw, lw, w;
	int			lives = rlc.lives < 0 ? 0 : rlc.lives > 8 ? 8 : rlc.lives;

	CG_HudAnchor( -1 );
	Com_sprintf( title, sizeof( title ), "FLOOR %i", rlc.floorPlayed );
	Q_strncpyz( node, rlc.nodeType != RLN_FIGHT ? CG_RL_NodeLabel( NULL ) : "", sizeof( node ) );
	tw = CG_DrawStrlen( title ) * 6;
	nw = node[0] ? CG_DrawStrlen( node ) * 4 + 5 : 0;
	lw = lives ? lives * 6 + 6 : 0;
	w = tw + nw + lw;
	if ( w < 78 ) {
		w = 78;
	}
	bg[0] = bg[1] = bg[2] = 0; bg[3] = 0.28f;
	CG_FillRect( x - 3, y - 3, w + 6, 21 + ( CG_DS_ChestsBanked() > 0 ? 9 : 0 ), bg );
	CG_DS_DrawCornerChests( x, y + 20, w );		// LOOT: banked chests
	col[0] = 1; col[1] = 0.8f; col[2] = 0.3f; col[3] = 0.9f;
	CG_RL_Text( x, y, title, col, 6, 8, -1, qtrue );
	if ( node[0] ) {
		CG_RL_NodeLabel( col );
		col[3] = 0.9f;
		CG_RL_Text( x + tw + 5, y + 2, node, col, 4, 6, -1, qtrue );
	}
	CG_RL_DrawLives( x + w - lives * 6 + 2, y + 2, 4, lives, 0.9f );
	col[0] = 0.9f; col[1] = 0.88f; col[2] = 0.8f; col[3] = 0.8f;
	CG_RL_Text( x, y + 11, CG_DS_HudLine() ? CG_DS_HudLine() : va( "%i/%i FRAGS", cg.snap->ps.persistant[PERS_SCORE], rlc.frags ), col, 4, 6, -1, qfalse );	// ACTS: boss / vault
	CG_RL_Text( x + w, y + 11, va( "%i SKULLS", rlc.skulls ), col, 4, 6, 1, qfalse );
	CG_HudAnchor( 0 );
}

static float CG_RL_Chip( float x, float y, const char *tag, int kind, float ch, float alpha, qboolean draw ) {
	float	cw = (int)( ch * 0.7f + 0.5f ), w = CG_DrawStrlen( tag ) * cw + 4;	// whole units, as CG_RL_Text draws
	vec4_t	col, dark;

	if ( draw ) {
		Vector4Copy( rlKindColors[kind < 0 || kind > 7 ? 0 : kind], col );
		col[3] = 0.8f * alpha;
		CG_FillRect( x, y, w, ch + 2, col );
		dark[0] = dark[1] = dark[2] = 0.03f; dark[3] = alpha;
		CG_RL_Text( x + 2, y + 1, tag, dark, cw, ch, -1, qfalse );
	}
	return w + 2;
}

/*
=================
CG_RL_EnemySummary

The enemies' kits in one row: every ability any of them has, then how many alt-fires and
the most perks one of them carries. Returns the width (draw qfalse only measures).
=================
*/
static float CG_RL_EnemySummary( float x, float y, float ch, float alpha, qboolean draw ) {
	int		has[RLC_NUM];
	int		i, j, c, alts = 0, perks, maxPerks = 0;
	float	x0 = x;

	memset( has, 0, sizeof( has ) );
	for ( i = 0 ; i < MAX_CLIENTS ; i++ ) {
		if ( kitRole[i] < RLR_BOT || !cgs.clientinfo[i].infoValid ) {
			continue;
		}
		for ( perks = 0, j = 0 ; j < kitNum[i] ; j++ ) {
			c = kitCards[i][j];
			if ( c < 0 || c >= RLC_NUM ) {
				continue;
			}
			if ( bg_rlCardKind[c] == RLK_PERK ) {
				perks++;
			} else {
				has[c] = 1;
			}
		}
		if ( perks > maxPerks ) {
			maxPerks = perks;
		}
	}
	for ( c = 0 ; c < RLC_NUM ; c++ ) {
		if ( has[c] && bg_rlCardKind[c] == RLK_ABILITY ) {
			x += CG_RL_Chip( x, y, bg_rlCardShort[c], RLK_ABILITY, ch, alpha, draw );
		} else if ( has[c] && bg_rlCardKind[c] == RLK_ALT ) {
			alts++;
		}
	}
	if ( alts ) {
		x += CG_RL_Chip( x, y, va( "%i ALT", alts ), RLK_ALT, ch, alpha, draw );
	}
	if ( maxPerks ) {
		x += CG_RL_Chip( x, y, va( "%i PERK%s", maxPerks, maxPerks > 1 ? "S" : "" ), RLK_PERK, ch, alpha, draw );
	}
	return x - x0;
}

/*
=================
CG_RL_DrawIntroStrip

A slim strip at the top for a few seconds when the floor starts: FLOOR N and the node, the frag
target, enemies and lives, one row with what the enemies can do, and the elite / boss.
=================
*/
static void CG_RL_DrawIntroStrip( float alpha, float drop ) {
	vec4_t		col, ncol, bg;
	char		title[32], node[48], line[96], special[64];
	float		y = 5 + drop, tw, nw, lw, sw, w, h, x;
	int			i, n = 0, sp = -1;

	for ( i = 0 ; i < MAX_CLIENTS ; i++ ) {
		if ( kitRole[i] >= RLR_BOT && cgs.clientinfo[i].infoValid ) {
			n++;
			if ( kitRole[i] == RLR_BOSS || ( kitRole[i] == RLR_ELITE && sp < 0 ) ) {
				sp = i;
			}
		}
	}
	Com_sprintf( title, sizeof( title ), "FLOOR %i", rlc.floorPlayed );
	Q_strncpyz( node, CG_RL_NodeLabel( ncol ), sizeof( node ) );
	Com_sprintf( line, sizeof( line ), "FIRST TO %i FRAGS  -  %i %s  -  %i %s", rlc.frags, n, n == 1 ? "ENEMY" : "ENEMIES",
		rlc.lives, rlc.lives == 1 ? "LIFE" : "LIVES" );
	if ( CG_DS_FloorGoal( rlc.lives ) ) {
		Q_strncpyz( line, CG_DS_FloorGoal( rlc.lives ), sizeof( line ) );		// ACTS: boss / vault floors
	}
	special[0] = 0;
	if ( sp >= 0 ) {
		Com_sprintf( special, sizeof( special ), "%s  %s", kitRole[sp] == RLR_BOSS ? "BOSS" : "ELITE", cgs.clientinfo[sp].name );
	}
	tw = CG_DrawStrlen( title ) * 9 + 8 + CG_DrawStrlen( node ) * 6;
	lw = CG_DrawStrlen( line ) * 5;
	sw = CG_RL_EnemySummary( 0, 0, 5, alpha, qfalse );
	w = tw > lw ? tw : lw;
	if ( sw > w ) {
		w = sw;
	}
	w += 16;
	h = special[0] ? 48 : 38;

	// a plate tight to the text, with a hairline in the node colour
	bg[0] = bg[1] = bg[2] = 0; bg[3] = 0.38f * alpha;
	CG_FillRect( 320 - w * 0.5f, y - 3, w, h, bg );
	ncol[3] = 0.8f * alpha;
	CG_FillRect( 320 - w * 0.5f, y - 3 + h - 1, w, 1, ncol );

	x = 320 - tw * 0.5f;
	col[0] = 1; col[1] = 0.82f; col[2] = 0.35f; col[3] = alpha;
	CG_RL_Text( x, y, title, col, 9, 12, -1, qtrue );
	ncol[3] = alpha;
	CG_RL_Text( x + CG_DrawStrlen( title ) * 9 + 8, y + 3, node, ncol, 6, 9, -1, qtrue );
	col[0] = col[1] = col[2] = 0.9f; col[3] = 0.9f * alpha;
	CG_RL_Text( 320, y + 15, line, col, 5, 7, 0, qtrue );
	CG_RL_EnemySummary( 320 - ( sw - 2 ) * 0.5f, y + 25, 5, alpha, qtrue );
	if ( special[0] ) {
		CG_RL_RoleColor( kitRole[sp], col );
		col[3] = alpha;
		CG_RL_Text( 320, y + 35, special, col, 5, 7, 0, qtrue );
	}
}

/*
=================
CG_RL_DrawKitPanel

While the scoreboard is up: every enemy with its whole kit. Right of the scoreboard on
wide screens, under it otherwise.
=================
*/
static void CG_RL_DrawKitPanel( float alpha ) {
	vec4_t		col, bg;
	float		y, x, h, margin;
	int			i, n;

	// the enemies and their kits
	bg[0] = bg[1] = bg[2] = 0; bg[3] = 0.5f * alpha;
	for ( n = 0, i = 0 ; i < MAX_CLIENTS ; i++ ) {
		if ( kitRole[i] >= RLR_BOT && cgs.clientinfo[i].infoValid ) {
			n++;
		}
	}
	h = 42 + n * 13;
	margin = cgs.screenXScale > 0 ? cgs.screenXBias / cgs.screenXScale : 0;
	if ( margin >= 380 ) {
		x = 712;
		y = 120;
	} else {
		x = 320 - 150;
		y = 476 - h;
	}
	CG_FillRect( x - 6, y - 4, 312, h, bg );
	CG_RL_NodeLabel( col );
	col[3] = alpha;
	CG_RL_Text( x, y, va( "FLOOR %i  %s  -  FIRST TO %i  -  %s", rlc.floorPlayed, CG_RL_NodeLabel( NULL ), rlc.frags,
		rlc.dif >= 1 && rlc.dif <= 5 ? rlDifNames[rlc.dif] : "" ), col, 5, 7, -1, qtrue );
	y += 12;
	col[0] = 0.3f; col[1] = 0.95f; col[2] = 0.42f; col[3] = alpha;
	CG_RL_Text( x, y + 1, "YOU", col, 4, 7, -1, qfalse );
	col[0] = col[1] = col[2] = 1;
	CG_RL_Text( x + 24, y, cgs.clientinfo[cg.clientNum].name, col, 6, 9, -1, qfalse );
	CG_RL_DrawChips( x + 110, y, rlc.cards, rlc.numCards, 6, alpha, 190 );
	y += 14;
	col[0] = col[1] = col[2] = 0.7f; col[3] = alpha;
	CG_RL_Text( x, y, va( "%i ENEMIES", n ), col, 5, 7, -1, qfalse );
	y += 10;
	for ( i = 0 ; i < MAX_CLIENTS ; i++ ) {
		if ( kitRole[i] < RLR_BOT || !cgs.clientinfo[i].infoValid ) {
			continue;
		}
		if ( kitRole[i] == RLR_BOSS || kitRole[i] == RLR_ELITE ) {
			Vector4Copy( kitRole[i] == RLR_BOSS ? rlKindColors[RLK_PERK] : rlKindColors[RLK_ALT], col );
			col[3] = alpha;
			CG_RL_Text( x, y + 1, kitRole[i] == RLR_BOSS ? "BOSS" : "ELITE", col, 4, 7, -1, qfalse );
		}
		col[0] = col[1] = col[2] = 1; col[3] = alpha;
		CG_RL_Text( x + 24, y, cgs.clientinfo[i].name, col, 6, 9, -1, qfalse );
		CG_RL_DrawChips( x + 110, y, kitCards[i], kitNum[i], 6, alpha, 190 );
		y += 13;
	}
}

/*
=================
CG_RL_DrawGot

"ABILITY UNLOCKED: WALL RUN"
=================
*/
static void CG_RL_DrawGot( float y ) {
	float		t, s, a;
	vec4_t		col;
	int			k;
	const char	*head;

	if ( gotCard < 0 || gotCard >= RLC_NUM || cg.time - gotTime > GOT_TIME ) {
		return;
	}
	t = (float)( cg.time - gotTime ) / GOT_TIME;
	k = CG_RL_Kind( gotCard );
	head = k == RLK_ABILITY ? "ABILITY UNLOCKED" : k == RLK_ALT ? "ALT-FIRE UNLOCKED" : k == RLK_STAT ? "STAT UP"
		: k == RLK_PASSIVE ? "PASSIVE UP" : k == RLK_WEAPON ? "WEAPON LEVEL UP" : k == RLK_SECRET ? "SECRET CLAIMED" : k == RLK_LEGEND ? "LEGENDARY CLAIMED" : "PERK GAINED";
	s = 1.0f + 0.5f * ( 1.0f - CG_RL_EaseOutBack( t * 5 ) );
	a = t < 0.75f ? 1.0f : 1.0f - ( t - 0.75f ) / 0.25f;
	CG_RL_Mix( col, rlKindColors[k], CG_Clamp01( 1.0f - t * 4 ), a );
	CG_RL_Text( 320, y, head, col, 9 * s, 12 * s, 0, qtrue );
	col[0] = col[1] = col[2] = 1; col[3] = a;
	CG_RL_Text( 320, y + 16 * s, bg_rlCardNames[gotCard], col, 18 * s, 26 * s, 0, qtrue );
}

/*
=================
CG_RL_DrawEndBanner

LEVEL CLEARED / FLOOR FAILED / THE DESCENT ENDS over the last moment of the floor
=================
*/
static void CG_RL_DrawEndBanner( float y, float alpha ) {
	vec4_t		col, bg;
	float		t, s;
	const char	*s1;

	t = (float)( cg.time - endBannerStart ) / 700.0f;
	s = 1.0f + 0.6f * ( 1.0f - CG_RL_EaseOutBack( t ) );
	if ( rlc.result == 1 ) {
		s1 = "LEVEL CLEARED";
		col[0] = 0.4f; col[1] = 1.0f; col[2] = 0.5f;
	} else if ( rlc.runOver ) {
		s1 = rlcDaily ? "OUT OF LIVES - THE ATTEMPT ENDS" : "YOU DIED - THE DESCENT ENDS";		// (en-daily)
		col[0] = 1.0f; col[1] = 0.2f; col[2] = 0.15f;
	} else {
		s1 = "FLOOR FAILED";
		col[0] = 1.0f; col[1] = 0.3f; col[2] = 0.2f;
	}
	// a plate tight to the words
	bg[0] = bg[1] = bg[2] = 0; bg[3] = 0.45f * alpha * CG_Clamp01( t * 3 );
	CG_FillRect( 320 - CG_DrawStrlen( s1 ) * 10 - 16, y - 6, CG_DrawStrlen( s1 ) * 20 + 32, 44, bg );
	col[3] = alpha * CG_Clamp01( t * 3 );
	CG_RL_Text( 320, y + 16 - 16 * s, s1, col, 20 * s, 30 * s, 0, qtrue );
}

/*
=================
CG_RL_Draw2D

From CG_Draw2D (not at the intermission)
=================
*/
void CG_RL_Draw2D( void ) {
	float	t, a;

	if ( !rlc.on || !cg.snap ) {
		return;
	}
	if ( !introStart && rlc.phase == RLP_PLAY && cg.snap->ps.persistant[PERS_TEAM] != TEAM_SPECTATOR ) {
		introStart = cg.time;
		trap_S_StartLocalSound( sndIntro, CHAN_ANNOUNCER );
	}
	CG_RL_DrawNameTags();
	if ( cg.snap->ps.persistant[PERS_TEAM] != TEAM_SPECTATOR && !cg.showScores ) {
		CG_RL_DrawHud();
	}
	CG_DS_DrawHud();		// LOOT: the held weapon's xp bar
	CG_DS_FrenzyDraw2D();	// FRENZY: chain counter, cash-out, edge glow, spike wash + ring
	CG_DSC_Draw2D();		// CARDS: legendary row, evolution ready, dstally preview
	if ( cg.showScores ) {
		CG_RL_DrawKitPanel( 1.0f );
	} else if ( introStart && rlc.phase == RLP_PLAY && !CG_DSI_Covers() ) {		// ds-intro carries the strip
		t = (float)( cg.time - introStart );
		if ( t < INTRO_TIME ) {
			a = t < 250 ? t / 250.0f : t > INTRO_TIME - INTRO_FADE ? ( INTRO_TIME - t ) / INTRO_FADE : 1.0f;
			CG_RL_DrawIntroStrip( a, -28 * ( 1.0f - CG_RL_EaseOut( t / 350.0f ) ) );
		}
	}
	if ( rlc.phase >= RLP_END && endBannerStart ) {
		CG_RL_DrawEndBanner( 150, 1.0f );
	}
	CG_RL_DrawGot( 190 );
}

/*
===============================================================================

THE INTERMISSION: result, skulls, the card offer

===============================================================================
*/

// next line of at most maxChars from *s, broken at a space
static const char *CG_RL_NextLine( const char **s, int maxChars ) {
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
CG_RL_DrawCard

flip: 0 = edge on, showing the back until 0.5, the face after; 1 = face fully open
=================
*/
static void CG_RL_DrawCard( int slot, int card, float cx, float cy, float scale, float flip, float alpha,
	qboolean highlight, float flash ) {
	vec4_t		bg, kc, fg, dim;
	float		w, h, x, y, sx, cw, chh, gy;
	int			k, r, i, n, owned;
	const char	*desc, *line, *key, *name, *tag;

	k = CG_RL_Kind( card );
	r = bg_rlCardRarity[card];
	if ( r < 0 || r > 3 ) {
		r = 0;
	}
	sx = flip < 0.5f ? cos( flip * M_PI ) : -cos( flip * M_PI );
	if ( sx < 0.02f ) {
		return;
	}
	w = RLCARD_W * scale * sx;
	h = RLCARD_H * scale;
	x = cx - w * 0.5f;
	y = cy - h * 0.5f;
	Vector4Copy( rlKindColors[k], kc );

	if ( flip < 0.5f ) {
		if ( CG_DSC_CardBack( card, x, y, w, h, sx, alpha ) ) {
			return;		// CARDS: the back tells the rarity
		}
		// the back: dark with a kind-coloured frame and a big D
		bg[0] = 0.06f; bg[1] = 0.05f; bg[2] = 0.08f; bg[3] = 0.95f * alpha;
		CG_FillRect( x, y, w, h, bg );
		kc[3] = 0.7f * alpha;
		CG_FillRect( x, y, w, 2, kc );
		CG_FillRect( x, y + h - 2, w, 2, kc );
		CG_FillRect( x, y, 2 * sx, h, kc );
		CG_FillRect( x + w - 2 * sx, y, 2 * sx, h, kc );
		kc[3] = 0.25f * alpha;
		CG_FillRect( x + w * 0.2f, y + h * 0.2f, w * 0.6f, h * 0.6f, kc );
		kc[3] = 0.8f * alpha;
		CG_RL_Text( cx, cy - 20 * scale, "D", kc, 30 * scale * sx, 40 * scale, 0, qfalse );
		return;
	}

	// highlight: lifted with a pulsing glow
	if ( highlight ) {
		kc[3] = ( 0.35f + 0.25f * sin( cg.time * 0.012f ) ) * alpha;
		CG_FillRect( x - 4, y - 4, w + 8, h + 8, kc );
	}
	bg[0] = 0.035f; bg[1] = 0.035f; bg[2] = 0.05f; bg[3] = 0.93f * alpha;
	CG_FillRect( x, y, w, h, bg );
	// kind band, frame
	kc[3] = 0.95f * alpha;
	CG_FillRect( x, y, w, 30 * scale, kc );
	kc[3] = 0.6f * alpha;
	CG_FillRect( x, y + h - 2, w, 2, kc );
	CG_FillRect( x, y, 2, h, kc );
	CG_FillRect( x + w - 2, y, 2, h, kc );

	cw = scale * sx;
	// key badge
	key = CG_KeyForCommand( va( "weapon %i", slot + 1 ) );
	if ( !key ) {
		key = va( "%i", slot + 1 );
	}
	fg[0] = fg[1] = fg[2] = 0.02f; fg[3] = alpha;
	dim[0] = dim[1] = dim[2] = 1; dim[3] = 0.9f * alpha;
	CG_FillRect( x + 6 * cw, y + 6 * scale, ( 6 + 9 * CG_DrawStrlen( key ) ) * cw, 18 * scale, dim );
	CG_RL_Text( x + 9 * cw, y + 8 * scale, key, fg, 9 * cw, 14 * scale, -1, qfalse );
	// kind + rarity
	CG_RL_Text( x + w - 6 * cw, y + 6 * scale, rlKindNames[k], fg, 6 * cw, 9 * scale, 1, qfalse );
	for ( i = 0 ; i <= r ; i++ ) {
		CG_FillRect( x + w - ( 10 + i * 8 ) * cw, y + 19 * scale, 5 * cw, 5 * scale, fg );
	}

	// big tag
	CG_RL_Mix( fg, rlKindColors[k], 0.15f, alpha );
	gy = y + 44 * scale;
	if ( k == RLK_PERK ) {
		CG_RL_Text( cx, gy, "PERK", fg, 22 * cw, 30 * scale, 0, qtrue );
	} else if ( k == RLK_WEAPON && CG_DS_WeaponIcon( RLC_WEAPON_OF( card ) ) ) {
		trap_R_SetColor( dim );
		CG_DrawPic( cx - 17 * cw, gy - 4 * scale, 34 * cw, 34 * scale, CG_DS_WeaponIcon( RLC_WEAPON_OF( card ) ) );
		trap_R_SetColor( NULL );
	} else {
		CG_RL_Text( cx, gy, bg_rlCardShort[card], fg, ( strlen( bg_rlCardShort[card] ) > 2 ? 20 : 26 ) * cw, 34 * scale, 0, qtrue );
	}

	// name
	name = bg_rlCardNames[card];
	fg[0] = fg[1] = fg[2] = 1; fg[3] = alpha;
	n = CG_DrawStrlen( name );
	chh = n > 17 ? 6.0f : n > 12 ? 7.5f : 9.5f;
	CG_RL_Text( cx, y + 90 * scale, name, fg, chh * cw, chh * 1.4f * scale, 0, qtrue );

	// description
	desc = BG_RLCardBinding( card ) ? BG_RLCardDescKey( card, CG_KeyForCommand( BG_RLCardBinding( card ) ) )
		: bg_rlCardDescs[card];		// rearmed: name the key the player bound (zip line, dash, grapple)
	if ( CG_DS_CardDesc( card ) ) {
		desc = CG_DS_CardDesc( card );		// weapon cards: what the next level adds
	}
	fg[0] = fg[1] = fg[2] = 0.85f;
	for ( n = 0 ; n < 4 && *desc ; n++ ) {
		line = CG_RL_NextLine( &desc, 20 );
		CG_RL_Text( cx, y + ( 112 + n * 12 ) * scale, line, fg, 6 * cw, 9 * scale, 0, qfalse );
	}

	// footer: new / stacking / rarity
	owned = 0;
	for ( i = 0 ; i < rlc.numCards ; i++ ) {
		if ( rlc.cards[i] == card ) {
			owned++;
		}
	}
	if ( rlc.phase >= RLP_DONE && card == rlc.picked && owned > 0 ) {
		owned--;		// already in the run's cards: show it as it was offered
	}
	// DESCENT v2 synergy: a glowing ribbon ("EVOLVES ROCKET LAUNCHER", "COMBO: BLINK + MIRAGE")
	tag = CG_DS_CardTag( card );
	if ( tag ) {
		float	glow = 0.65f + 0.35f * sin( cg.time * 0.009f + slot );
		float	tw = CG_DrawStrlen( tag ) * 5.0f * cw;

		fg[0] = 1.0f; fg[1] = 0.84f; fg[2] = 0.25f; fg[3] = 0.3f * glow * alpha;
		CG_FillRect( x - 3, y - 3, w + 6, 3, fg );
		CG_FillRect( x - 3, y + h, w + 6, 3, fg );
		CG_FillRect( x - 3, y, 3, h, fg );
		CG_FillRect( x + w, y, 3, h, fg );
		fg[3] = 0.9f * glow * alpha;
		CG_FillRect( x + 2, y + 160 * scale, w - 4, 13 * scale, fg );
		fg[0] = fg[1] = fg[2] = 0.05f; fg[3] = alpha;
		CG_RL_Text( cx, y + 163 * scale, tag, fg, tw > w - 8 ? ( w - 8 ) / CG_DrawStrlen( tag ) : 5.0f * cw, 8 * scale, 0, qfalse );
	}
	Vector4Copy( rlKindColors[k], fg );
	fg[3] = alpha;
	if ( CG_DS_CardFooter( card ) ) {
		CG_RL_Text( cx, y + h - 18 * scale, CG_DS_CardFooter( card ), fg, 5.5f * cw, 8 * scale, 0, qfalse );
	} else if ( owned ) {
		CG_RL_Text( cx, y + h - 18 * scale, va( "OWNED x%i  ->  x%i", owned, owned + 1 ), fg, 5.5f * cw, 8 * scale, 0, qfalse );
	} else {
		CG_RL_Text( cx, y + h - 18 * scale, va( "NEW  -  %s", rlRarityNames[r] ), fg, 5.5f * cw, 8 * scale, 0, qfalse );
	}

	CG_DSC_CardFace( slot, card, x, y, w, h, alpha );		// CARDS: rarity frame, shimmer, legendary sweep
	if ( flash > 0 ) {
		fg[0] = fg[1] = fg[2] = 1; fg[3] = flash * alpha;
		CG_FillRect( x, y, w, h, fg );
	}
}

static void CG_RL_DrawOffer( void ) {
	vec4_t		col;
	float		total, t, e, cx, cy, scale, alpha, flip, flash, cs;
	int			i, n = shownNum;
	const char	*hint, *key1;

	if ( n <= 0 ) {
		return;
	}
	cs = n > 4 ? 0.86f : 1.0f;
	total = ( n * RLCARD_W + ( n - 1 ) * RLCARD_GAP ) * cs;
	for ( i = 0 ; i < n ; i++ ) {
		cx = 320 - total * 0.5f + ( RLCARD_W * 0.5f + i * ( RLCARD_W + RLCARD_GAP ) ) * cs;
		cy = RLCARD_Y + RLCARD_H * 0.5f;
		if ( pickSlot < 0 && rlc.phase == RLP_OFFER && CG_DSC_RevealOn() ) {
			// CARDS: dealt face-down, flipped one by one (cg_ds_cards.c)
			if ( !CG_DSC_CardAnim( i, offerStart, n, shownOffer, &e, &alpha, &flip, &scale, &flash ) ) {
				continue;
			}
			cy += e;
			scale *= cs;
			if ( i == sel && CG_DSC_RevealLeft( offerStart, n, shownOffer ) <= 0 ) {
				cy -= 8;
				scale *= 1.04f;
			}
			CG_RL_DrawCard( i, shownOffer[i], cx, cy, scale, flip, alpha, i == sel && CG_DSC_RevealLeft( offerStart, n, shownOffer ) <= 0, flash );
		} else if ( pickSlot < 0 && rlc.phase == RLP_OFFER && !CG_DSC_RevealOn() ) {
			CG_RL_DrawCard( i, shownOffer[i], cx, cy - ( i == sel ? 8 : 0 ), cs * ( i == sel ? 1.04f : 1.0f ), 1.0f, 1.0f, i == sel, 0 );	// cg_dsReveal 0: just there
		} else if ( pickSlot < 0 && rlc.phase == RLP_OFFER ) {
			// slide in from below, flip open
			t = (float)( cg.time - offerStart - i * RLCARD_STAGGER ) / RLCARD_IN;
			if ( t <= 0 ) {
				continue;
			}
			e = CG_RL_EaseOutBack( t );
			cy += 160 * ( 1.0f - e );
			alpha = CG_Clamp01( t * 3 );
			flip = CG_Clamp01( ( t - 0.35f ) / 0.65f ) * 0.5f + 0.5f;
			if ( t < 0.35f ) {
				flip = 0.25f + t / 0.35f * 0.25f;		// back side turning
			}
			scale = cs;
			if ( i == sel && t >= 1 ) {
				cy -= 8;
				scale = 1.04f * cs;
			}
			flash = t > 0.9f && t < 1.3f ? 0.3f * ( 1.3f - t ) / 0.4f : 0;
			CG_RL_DrawCard( i, shownOffer[i], cx, cy, scale, flip, alpha, i == sel && t >= 1, flash );
		} else {
			// taken: the card swells and rises, the others fall away
			t = pickSlot >= 0 ? (float)( cg.time - pickTime ) / 900.0f : 1.0f;
			if ( i == ( pickSlot >= 0 ? pickSlot : -1 ) || ( pickSlot < 0 && shownOffer[i] == rlc.picked ) ) {
				scale = cs * ( 1.0f + 0.12f * sin( CG_Clamp01( t * 1.4f ) * M_PI * 0.5f ) );
				cy -= 24 * CG_RL_EaseOut( t );
				cx += ( 320 - cx ) * CG_RL_EaseOut( t * 1.2f );
				flash = CG_Clamp01( 0.8f * ( 1.0f - t * 2.5f ) );
				CG_RL_DrawCard( i, shownOffer[i], cx, cy, scale, 1.0f, 1.0f, qtrue, flash );
			} else {
				alpha = CG_Clamp01( 1.0f - t * 2.5f );
				if ( alpha > 0 ) {
					CG_RL_DrawCard( i, shownOffer[i], cx, cy + 120 * t * t, cs * ( 1.0f - 0.2f * t ), 1.0f, alpha, qfalse, 0 );
				}
			}
		}
	}

	if ( rlc.phase == RLP_OFFER && pickSlot < 0 ) {
		CG_DSC_DrawOfferFX( offerStart, n, shownOffer );		// CARDS: the legendary's flare
	}
	if ( rlc.phase == RLP_OFFER && pickSlot < 0 ) {
		t = (float)( cg.time - offerStart ) / 300.0f;
		col[0] = 1; col[1] = 0.82f; col[2] = 0.3f; col[3] = CG_Clamp01( t ) * ( 0.8f + 0.2f * sin( cg.time * 0.008f ) );
		CG_RL_Text( 320, RLCARD_Y - 22, "CHOOSE YOUR REWARD", col, 10, 14, 0, qtrue );
		key1 = CG_KeyForCommand( "+attack" );
		hint = va( "[1]-[%i] TAKE    [%s] TAKE HIGHLIGHTED    [WHEEL] MOVE", n, key1 ? key1 : "FIRE" );
		col[0] = col[1] = col[2] = 0.85f; col[3] = CG_Clamp01( t );
		CG_RL_Text( 320, RLCARD_Y + RLCARD_H + 16, hint, col, 6, 9, 0, qtrue );
		if ( rlc.rerolls > 0 ) {
			key1 = CG_KeyForCommand( "+moveup" );
			col[0] = 1; col[1] = 0.75f; col[2] = 0.3f;
			CG_RL_Text( 320, RLCARD_Y + RLCARD_H + 30, va( "[%s] REROLL (%i LEFT)", key1 ? key1 : "JUMP", rlc.rerolls ),
				col, 6, 9, 0, qtrue );
		}
	}
}

// count the skulls up with ticks
static void CG_RL_DrawSkulls( float y ) {
	vec4_t		col;
	float		t;
	char		line[160];
	int			want;

	if ( rlcDaily ) {
		return;		// en-daily: the daily pays no skulls (its score is on the result screen)
	}
	t = (float)( cg.time - offerStart ) / 1000.0f;
	want = (int)( rlc.reward * CG_Clamp01( ( cg.time - offerStart - 200 ) / 900.0f ) );
	if ( offerStart == 0 ) {
		want = rlc.reward;
	}
	if ( want > skullShown && cg.time >= skullTickNext ) {
		skullShown++;
		skullTickNext = cg.time + 45;
		trap_S_StartLocalSound( sndSkull, CHAN_LOCAL_SOUND );
	}
	Com_sprintf( line, sizeof( line ), "+%i FLOOR   +%i KILLS   STYLE %s   %s%s%s",
		rlc.rb[0], rlc.rb[1], rlc.rb[2] >= 0 && rlc.rb[2] < 8 ? rlStyleLetters[rlc.rb[2]] : "-",
		rlc.rb[3] ? va( "+%i %s   ", rlc.rb[3], rlNodeNames[rlc.nodeType] ) : "",
		rlc.rb[4] ? va( "+%i%%   ", rlc.rb[4] ) : "", "" );
	// ECONOMY: the boss bar / kill and the difficulty multiplier (one va per piece: va buffers rotate)
	if ( rlc.bossSkulls > 0 ) {
		Q_strcat( line, sizeof( line ), va( "+%i BOSS   ", rlc.bossSkulls ) );
	}
	if ( rlc.rewardMult != 100 ) {
		Q_strcat( line, sizeof( line ), va( "x%i.%02i DIFFICULTY", rlc.rewardMult / 100, rlc.rewardMult % 100 ) );
	}
	col[0] = col[1] = col[2] = 0.85f; col[3] = CG_Clamp01( t * 2 );
	CG_RL_Text( 320, y, line, col, 6, 9, 0, qtrue );
	col[0] = 1; col[1] = 0.88f; col[2] = 0.5f;
	CG_RL_Text( 320, y + 13, va( "+%i SKULLS   (RUN TOTAL %i)", skullShown, rlc.skulls - rlc.reward + skullShown ),
		col, 9, 12, 0, qtrue );
}

/*
=================
CG_RL_DrawLostPay

ECONOMY: a lost floor still pays its kills and the boss damage dealt
=================
*/
static void CG_RL_DrawLostPay( float y ) {
	vec4_t	col;
	char	line[128];

	if ( rlc.reward <= 0 ) {
		return;
	}
	line[0] = 0;
	if ( rlc.rb[1] > 0 ) {
		Q_strcat( line, sizeof( line ), va( "+%i KILLS   ", rlc.rb[1] ) );
	}
	if ( rlc.bossPct >= 0 ) {
		Q_strcat( line, sizeof( line ), va( "BOSS BAR %i%%: +%i   ", rlc.bossPct, rlc.bossSkulls ) );
	}
	if ( rlc.rb[4] ) {
		Q_strcat( line, sizeof( line ), va( "+%i%%   ", rlc.rb[4] ) );
	}
	if ( rlc.rewardMult != 100 ) {
		Q_strcat( line, sizeof( line ), va( "x%i.%02i DIFFICULTY", rlc.rewardMult / 100, rlc.rewardMult % 100 ) );
	}
	col[0] = col[1] = col[2] = 0.85f; col[3] = 1;
	CG_RL_Text( 320, y, line, col, 6, 9, 0, qtrue );
	col[0] = 1; col[1] = 0.88f; col[2] = 0.5f;
	CG_RL_Text( 320, y + 13, va( "+%i SKULLS FOR THE FIGHT", rlc.reward ), col, 9, 12, 0, qtrue );
}

/*
=================
CG_RL_DrawIntermission

Instead of the scoreboard while a run is on
=================
*/
qboolean CG_RL_DrawIntermission( void ) {
	vec4_t		col, bg;
	float		t;

	if ( !rlc.on || !cg.snap ) {
		return qfalse;
	}
	CG_RL_ReadButtons();
	bg[0] = 0.01f; bg[1] = 0.0f; bg[2] = 0.02f; bg[3] = 0.6f;
	CG_FillRect( -2000, 0, 4640, 480, bg );

	t = (float)( cg.time - phaseStart ) / 600.0f;
	if ( CG_DS_Practice() ) {
		// en-boss: BOSS PRACTICE: no lives, cards or tally, the practice picker comes next
		col[0] = 1; col[1] = rlc.result == 1 ? 0.82f : 0.25f; col[2] = rlc.result == 1 ? 0.3f : 0.15f; col[3] = CG_Clamp01( t );
		CG_RL_Text( 320, 120, rlc.result == 1 ? "BOSS SLAIN" : CG_DS_RematchFallen() ? "FALLEN" : "TIME UP", col, 22, 30, 0, qtrue );		// (en-rematch: ONE LIFE)
		col[0] = col[1] = col[2] = 0.8f;
		CG_RL_Text( 320, 168, CG_DS_RematchOn() ? "REMATCH  -  YOUR TIMES ARE ON THE NEXT SCREEN" : "PRACTICE  -  NOTHING WAS SAVED", col, 8, 12, 0, qtrue );		// (en-rematch)
		return qtrue;
	}
	if ( rlc.result == 1 ) {
		col[0] = 0.4f; col[1] = 1.0f; col[2] = 0.5f; col[3] = 1;
		if ( rlcDaily && rlc.runOver ) {
			col[0] = 1; col[1] = 0.8f; col[2] = 0.3f;		// en-daily: the last floor of the daily
			CG_RL_Text( 320, 40, "THE DAILY IS CLEARED", col, 20, 28, 0, qtrue );
		} else {
			CG_RL_Text( 320, 40, va( "FLOOR %i CLEARED", rlc.floorPlayed ), col, 20, 28, 0, qtrue );
		}
		CG_RL_DrawLives( 320 - rlc.lives * 6, 74, 8, rlc.lives, 1 );
		if ( rlc.phase == RLP_END && CG_DSC_DrawTally( rlc.floorPlayed ) ) {	// CARDS: the floor-end tally first
			return qtrue;
		}
		if ( CG_DS_DrawJackpot( rlc.phase ) ) {		// LOOT: chests before the cards
			return qtrue;
		}
		if ( ( rlc.phase == RLP_OFFER || ( rlc.phase == RLP_DONE && rlc.numOffer <= 0 ) )
			&& CG_KillCamReplayGate() ) {	// 16:9: the floor's best kill, then the cards
			offerStart = cg.time;		// the cards fly in (and take picks) only after it
			return qtrue;
		}
		if ( rlc.phase >= RLP_OFFER ) {
			CG_RL_DrawSkulls( 92 );
		}
		CG_RL_DrawOffer();
		CG_DSC_DrawDing();		// CARDS: the synergy ding
		if ( rlc.phase >= RLP_DONE ) {
			CG_RL_DrawGot( 380 );
			if ( cg.time - gotTime > 900 || gotCard < 0 ) {
				col[0] = col[1] = col[2] = 0.8f; col[3] = 0.6f + 0.4f * sin( cg.time * 0.006f );
				CG_RL_Text( 320, 440, rlcDaily && rlc.runOver ? "YOUR SCORE AWAITS..." : "THE WAY DOWN OPENS...", col, 7, 10, 0, qtrue );
			}
		}
	} else {
		if ( rlc.runOver && rlcDaily ) {
			// en-daily: no skulls, no records here: the score waits on the daily's result screen
			col[0] = 1; col[1] = rlc.result == 1 ? 0.75f : 0.18f; col[2] = rlc.result == 1 ? 0.25f : 0.12f; col[3] = CG_Clamp01( t );
			CG_RL_Text( 320, 110, rlc.result == 1 ? "THE DAILY IS CLEARED" : "THE ATTEMPT ENDS", col, 24, 34, 0, qtrue );
			col[0] = col[1] = col[2] = 0.9f;
			CG_RL_Text( 320, 160, va( "%i FLOORS CLEARED   %i KILLS", rlc.cleared, rlc.kills ), col, 8, 12, 0, qtrue );
		} else if ( rlc.runOver ) {
			col[0] = 1; col[1] = 0.18f; col[2] = 0.12f; col[3] = CG_Clamp01( t );
			CG_RL_Text( 320, 110, "THE DESCENT ENDS", col, 24, 34, 0, qtrue );
			col[0] = col[1] = col[2] = 0.9f;
			CG_RL_Text( 320, 160, va( "%i FLOORS CLEARED   %i KILLS   %i SKULLS BANKED", rlc.cleared, rlc.kills, rlc.banked ),
				col, 8, 12, 0, qtrue );
			col[0] = 1; col[1] = 0.85f; col[2] = 0.3f; col[3] = CG_Clamp01( t - 1 ) * ( 0.75f + 0.25f * sin( cg.time * 0.01f ) );
			if ( rlc.nbFloor ) {
				CG_RL_Text( 320, 190, "NEW BEST FLOOR!", col, 12, 16, 0, qtrue );
			}
			if ( rlc.nbKills ) {
				CG_RL_Text( 320, 212, "NEW BEST KILLS!", col, 9, 12, 0, qtrue );
			}
			CG_RL_DrawLostPay( 244 );		// ECONOMY
		} else {
			col[0] = 1; col[1] = 0.3f; col[2] = 0.2f; col[3] = CG_Clamp01( t );
			CG_RL_Text( 320, 120, va( "FLOOR %i FAILED", rlc.floorPlayed ), col, 22, 30, 0, qtrue );
			col[0] = col[1] = col[2] = 0.92f;
			CG_RL_Text( 320, 168, va( "%i %s LEFT", rlc.lives, rlc.lives == 1 ? "LIFE" : "LIVES" ), col, 10, 14, 0, qtrue );
			CG_RL_DrawLives( 320 - rlc.lives * 7, 190, 10, rlc.lives, CG_Clamp01( t ) );
			col[0] = col[1] = col[2] = 0.75f;
			CG_RL_Text( 320, 220, "PICK ANOTHER PATH ON THIS FLOOR", col, 7, 10, 0, qtrue );
			CG_RL_DrawLostPay( 244 );		// ECONOMY
		}
	}
	return qtrue;
}
