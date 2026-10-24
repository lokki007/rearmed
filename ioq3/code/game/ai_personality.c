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
// ai_personality.c -- rearmed bot personalities: a play style per bot that shifts the mix of
// dodges, jumps, hooks, wall runs and alt-fire, biases the weapon choice and the fighting
// distance, and adds flavoured taunts. BALANCED reproduces the plain rearmed bot exactly.
//
// bot_personality -1 = auto (from the bot's name), 0..6 = force one type on every bot

#include "g_local.h"
#include "../botlib/botlib.h"
#include "../botlib/be_aas.h"
#include "../botlib/be_ea.h"
#include "../botlib/be_ai_char.h"
#include "../botlib/be_ai_chat.h"
#include "../botlib/be_ai_gen.h"
#include "../botlib/be_ai_goal.h"
#include "../botlib/be_ai_move.h"
#include "../botlib/be_ai_weap.h"
#include "ai_main.h"
#include "ai_dmq3.h"
#include "ai_dmnet.h"
#include "chars.h"

#define TAUNT_KILL			0
#define TAUNT_DEATH			1
#define TAUNT_MULTI			2
#define TAUNT_AIRSHOT		3
#define TAUNT_NUM_EVENTS	4
#define TAUNT_LINES			6

#define TAUNT_CHANCE		0.35f
#define TAUNT_GAP			6000	// msec between two lines from one bot
#define TAUNT_MULTI_TIME	3000	// kills this close together make a multi-kill
#define TAUNT_AIR_HEIGHT	64		// AIRSHOT_HEIGHT in g_missile.c

#define KITE_BAND			60		// back off once this much inside the preferred distance
#define KITE_LOOK			160		// floor check this far behind us
#define KITE_DROP			300

extern bot_state_t	*botstates[MAX_CLIENTS];

static vmCvar_t	bot_personality;
static qboolean	rmPersonaCvarInit;

/*
==================
The personalities

BALANCED must stay all 1.0 / 0: it is today's rearmed bot.
Multipliers stay near 1: extra dodging and jumping costs the bots kills.
==================
*/
static const rmPersona_t rmPersonas[RM_NUM_PERSONAS] = {
	//					dodge	jump	hookO	hookT	wall	alt		charge	reflect	dist
	//		weapons, then v4:	slide	slam	blink	props	zip
	{ "BALANCED",		1.0f,	1.0f,	1.0f,	1.0f,	1.0f,	1.0f,	1.0f,	1.0f,	0,
		{ WP_NONE },
		1.0f,	1.0f,	1.0f,	1.0f,	1.0f },
	{ "RUSHER",			0.8f,	1.1f,	1.4f,	1.2f,	1.2f,	0.9f,	0.7f,	1.0f,	0,
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
		{ WP_SHOTGUN, WP_CHAINGUN, WP_PLASMAGUN, WP_NONE },
#else
		{ WP_SHOTGUN, WP_PLASMAGUN, WP_NONE },
#endif
		1.6f,	1.5f,	1.5f,	0.8f,	1.4f },
	{ "SNIPER",			1.1f,	0.6f,	0.4f,	0.8f,	0.6f,	1.0f,	1.4f,	0.8f,	700,
		{ WP_RAILGUN, WP_MACHINEGUN, WP_LIGHTNING, WP_NONE },
		0.4f,	0.3f,	0.6f,	1.3f,	1.5f },
	{ "TRICKSTER",		1.2f,	1.25f,	1.1f,	1.5f,	1.5f,	1.4f,	1.4f,	1.6f,	350,
		{ WP_ROCKET_LAUNCHER, WP_GRENADE_LAUNCHER, WP_NONE },
		1.6f,	1.5f,	1.4f,	1.4f,	1.6f },
	{ "BRAWLER",		0.7f,	0.8f,	1.5f,	1.0f,	0.8f,	1.0f,	0.8f,	1.3f,	0,
		{ WP_GAUNTLET, WP_SHOTGUN, WP_NONE },
		1.1f,	1.6f,	1.2f,	0.7f,	1.2f },
	{ "PYRO",			0.9f,	1.0f,	0.7f,	1.0f,	1.0f,	1.2f,	1.5f,	1.0f,	450,
		{ WP_GRENADE_LAUNCHER, WP_ROCKET_LAUNCHER, WP_NONE },
		0.9f,	1.0f,	0.9f,	1.8f,	0.8f },
	{ "STORMCALLER",	1.0f,	0.9f,	0.9f,	1.0f,	1.0f,	1.3f,	1.3f,	1.0f,	400,
		{ WP_LIGHTNING, WP_PLASMAGUN, WP_NONE },
		0.9f,	0.9f,	1.0f,	1.1f,	1.0f }
};

/*
==================
Hand-picked styles for the stock bots; anyone else is hashed from the name
==================
*/
typedef struct {
	const char	*name;
	int			persona;
} rmPersonaBot_t;

static const rmPersonaBot_t rmPersonaBots[] = {
	{ "doom",		RM_PERSONA_RUSHER },
	{ "razor",		RM_PERSONA_RUSHER },
	{ "ranger",		RM_PERSONA_RUSHER },
	{ "major",		RM_PERSONA_RUSHER },
	{ "biker",		RM_PERSONA_RUSHER },
	{ "patriot",	RM_PERSONA_RUSHER },

	{ "visor",		RM_PERSONA_SNIPER },
	{ "stripe",		RM_PERSONA_SNIPER },
	{ "hunter",		RM_PERSONA_SNIPER },
	{ "bitterman",	RM_PERSONA_SNIPER },
	{ "cadavre",	RM_PERSONA_SNIPER },

	{ "xaero",		RM_PERSONA_TRICKSTER },
	{ "anarki",		RM_PERSONA_TRICKSTER },
	{ "mynx",		RM_PERSONA_TRICKSTER },
	{ "slash",		RM_PERSONA_TRICKSTER },
	{ "lucy",		RM_PERSONA_TRICKSTER },
	{ "bones",		RM_PERSONA_TRICKSTER },

	{ "sarge",		RM_PERSONA_BRAWLER },
	{ "tankjr",		RM_PERSONA_BRAWLER },
	{ "grunt",		RM_PERSONA_BRAWLER },
	{ "gorre",		RM_PERSONA_BRAWLER },
	{ "hossman",	RM_PERSONA_BRAWLER },

	{ "klesk",		RM_PERSONA_PYRO },
	{ "sorlag",		RM_PERSONA_PYRO },
	{ "wrack",		RM_PERSONA_PYRO },
	{ "daemia",		RM_PERSONA_PYRO },

	{ "uriel",		RM_PERSONA_STORMCALLER },
	{ "keel",		RM_PERSONA_STORMCALLER },
	{ "orbb",		RM_PERSONA_STORMCALLER },
	{ "phobos",		RM_PERSONA_STORMCALLER },

	{ "crash",		RM_PERSONA_BALANCED },
	{ "angel",		RM_PERSONA_BALANCED },

	{ NULL,			0 }
};

/*
==================
Taunts: [persona][event][line], %s is the victim / killer / shooter
==================
*/
static const char *rmTaunts[RM_NUM_PERSONAS][TAUNT_NUM_EVENTS][TAUNT_LINES] = {
	{	// BALANCED
		{ "Good fight, %s.", "Frag.", "Better luck next time, %s.", "That one is mine.", "Keep practicing, %s.", "Scoreboard says hi." },
		{ "Nice one, %s.", "Okay, you got me.", "Back in a second.", "Good shot, %s.", "I'll get you next round, %s.", "Respawning." },
		{ "On a roll!", "Streak!", "Who's next?", "That's how it's done.", "Keep them coming!", "Can't stop now!" },
		{ "Nice airshot, %s!", "Caught mid-jump. Well played.", "Good read, %s.", "Note to self: less jumping.", "Clean shot, %s.", "Didn't see that one coming." }
	},
	{	// RUSHER
		{ "Too slow, %s!", "In your face, %s.", "Point blank. Point made.", "Should've run, %s.", "Next!", "Close enough to shake hands, %s." },
		{ "Lucky shot, %s. I'm coming back.", "Respawning angry.", "Okay %s, round two. Right now.", "That's it, I'm charging.", "Didn't even slow me down.", "Save me a spot, %s." },
		{ "Who's next? Line up!", "Keep 'em coming!", "Full speed, no brakes!", "That's a pile-up!", "Just getting warmed up!", "Nobody outruns me!" },
		{ "Nice shot %s, now try it on the ground.", "Hey, I was flying!", "Grounded? Not for long.", "Fine, %s. I'll come to you.", "Air mail? Return to sender.", "Cheap trick, %s!" }
	},
	{	// SNIPER
		{ "%s, deleted.", "One shot. One frag.", "Stay still next time, %s.", "Scoped and dropped.", "I saw you first, %s.", "Distance is my friend." },
		{ "Got too close. Noted.", "Well played, %s. Won't happen twice.", "Adjusting my aim.", "You found my perch, %s.", "Relocating.", "Should've kept my distance." },
		{ "Clean sweep from the rafters.", "Line of sight, line of fire.", "Every shot counts.", "Precision beats panic.", "Tally rising.", "Nobody crosses my lane." },
		{ "Tracked me mid-air, %s? Respect.", "Nice lead, %s.", "Okay, that was a good read.", "Noted: no more jumping.", "You'll only land that once, %s.", "Sharp shooting, %s." }
	},
	{	// TRICKSTER
		{ "Now you see me, %s...", "Ta-da!", "Look behind you, %s. Oh wait.", "Was that your rocket, %s? Thanks!", "Magic trick number seven.", "Gotcha, %s!" },
		{ "That was my double.", "Okay %s, that trick was yours.", "I meant to do that.", "Plot twist!", "Enjoy it, %s. It's a setup.", "Just testing the respawn." },
		{ "And for my next trick...", "Pick a card, any card!", "Bounce, blast, bye!", "Round of applause, please!", "Nobody expects the ricochet!", "Encore!" },
		{ "Hey %s, I was showing off!", "Caught mid-flip. Rude.", "Okay, that one was pretty.", "Air traffic control, %s?", "Stick the landing, they said.", "Nice read, %s. Won't work twice." }
	},
	{	// BRAWLER
		{ "Up close and personal, %s.", "Down you go.", "Keep your guard up, %s.", "That's how we do it in the pit.", "Knuckles win again.", "Stay down, %s." },
		{ "Good hit, %s. Now fight me for real.", "Just a scratch.", "Back in the ring. Now.", "You hit hard, %s. I hit harder.", "Round two, %s.", "That only makes me angry." },
		{ "Who's next in the ring?", "Knockout streak!", "Pile 'em up!", "Nobody walks away from the pit.", "Bring the whole team!", "That's a combo!" },
		{ "Knocked me out of the sky, %s? Cute.", "Fight me on the ground, %s!", "Bah! Cheap pop.", "I don't do flying anyway.", "Lucky swing, %s.", "Get down here and brawl, %s." }
	},
	{	// PYRO
		{ "Toasted, %s.", "Feel the burn.", "Crispy!", "Fire in the hole, %s!", "Should've brought an extinguisher.", "Well done, %s. Extra well done." },
		{ "I'll be back with more fire.", "You put out my flame, %s? Not for long.", "Just stoking the coals.", "Burned out. Refueling.", "Next time I bring more napalm.", "Enjoy it while it lasts, %s." },
		{ "It's a bonfire!", "Everything burns!", "Barbecue for everyone!", "Firestorm rolling!", "Light 'em up!", "Hot streak!" },
		{ "Nice shot %s. You're still getting torched.", "Shot out of the sky? Bold.", "I'll return the favor, %s.", "Hot air rises, huh?", "Enjoy it, %s. The fire's coming.", "Landing in flames." }
	},
	{	// STORMCALLER
		{ "Shocking, %s.", "Grounded.", "Should've stayed dry, %s.", "Thunder follows lightning.", "Current events: %s is out.", "Forecast: frag." },
		{ "The storm will return, %s.", "Just a short circuit.", "Recharging.", "You broke the circuit, %s. It reconnects.", "Clear skies... for now.", "Static discharge. Back soon." },
		{ "Chain lightning!", "Storm's rolling in!", "High voltage!", "Everyone's conducting tonight!", "Thunder and more thunder!", "Surge!" },
		{ "Struck me out of the sky, %s?", "Nice bolt, %s.", "The air is my element, you know.", "Grounding me won't help, %s.", "Lightning never strikes twice. I do.", "Turbulence." }
	}
};

/*
==================
BotPersonaCvar
==================
*/
static int BotPersonaCvar( void ) {
	static int	updated = -1;

	if ( !rmPersonaCvarInit ) {
		trap_Cvar_Register( &bot_personality, "bot_personality", "-1", 0 );
		rmPersonaCvarInit = qtrue;
		updated = level.time;
	} else if ( updated != level.time ) {
		trap_Cvar_Update( &bot_personality );		// once a server frame
		updated = level.time;
	}
	return bot_personality.integer;
}

/*
==================
BotPersonaAssign

Pick the play style from the bot's name (or bot_personality)
==================
*/
static void BotPersonaAssign( bot_state_t *bs, int forced ) {
	char			userinfo[MAX_INFO_STRING], name[MAX_NETNAME], *s;
	const char		*how;
	unsigned int	hash;
	int				i;

	name[0] = '\0';
	trap_GetUserinfo( bs->client, userinfo, sizeof( userinfo ) );
	Q_strncpyz( name, Info_ValueForKey( userinfo, "n" ), sizeof( name ) );
	if ( !name[0] ) {
		ClientName( bs->client, name, sizeof( name ) );
	}
	Q_CleanStr( name );
	Q_strlwr( name );

	if ( forced >= 0 && forced < RM_NUM_PERSONAS ) {
		bs->rmPersona = forced;
		how = "forced";
	} else {
		how = NULL;
		for ( i = 0 ; rmPersonaBots[i].name ; i++ ) {
			if ( !Q_stricmp( name, rmPersonaBots[i].name ) ) {
				bs->rmPersona = rmPersonaBots[i].persona;
				how = "table";
				break;
			}
		}
		if ( !how ) {
			hash = 5381;
			for ( s = name ; *s ; s++ ) {
				hash = hash * 33 + (unsigned char)*s;
			}
			bs->rmPersona = (int)( hash % RM_NUM_PERSONAS );
			how = "hash";
		}
	}
	bs->rmPersonaForced = forced;
	bs->rmPersonaSet = qtrue;
	// start counting events from now
	bs->rmTauntKills = bs->num_kills;
	bs->rmTauntDeaths = bs->num_deaths;

	if ( trap_Cvar_VariableIntegerValue( "g_debugAlt" ) > 1 ) {
		G_Printf( "bot_persona: client %i \"%s\" is %s (%s)\n", bs->client, name, rmPersonas[bs->rmPersona].name, how );
	}
}

/*
==================
BotPersona

The play style of this bot, assigned on first use (the bot state is cleared on spawn / map change)
==================
*/
const rmPersona_t *BotPersona( bot_state_t *bs ) {
	int		forced = BotPersonaCvar();

	if ( forced < 0 || forced >= RM_NUM_PERSONAS ) {
		forced = -1;
	}
	if ( !bs->rmPersonaSet || bs->rmPersonaForced != forced ) {
		BotPersonaAssign( bs, forced );
	}
	if ( bs->rmPersona < 0 || bs->rmPersona >= RM_NUM_PERSONAS ) {
		bs->rmPersona = RM_PERSONA_BALANCED;
	}
	return &rmPersonas[bs->rmPersona];
}

/*
==================
BotPersonaWeaponOK

Owned, enough ammo, and sensible at this distance.
slack widens the range for the weapon already in hand, so the bot doesn't flip-flop.
==================
*/
static qboolean BotPersonaWeaponOK( bot_state_t *bs, int weapon, float dist, qboolean slack ) {
	playerState_t	*ps = &bs->cur_ps;
	int				ammo, minAmmo;
	float			s = slack ? 1.2f : 1.0f;

	if ( weapon <= WP_NONE || weapon >= WP_NUM_WEAPONS || !( ps->stats[STAT_WEAPONS] & ( 1 << weapon ) ) ) {
		return qfalse;
	}
	switch ( weapon ) {
	case WP_MACHINEGUN:			minAmmo = 20; break;
	case WP_LIGHTNING:			minAmmo = 25; break;
	case WP_PLASMAGUN:			minAmmo = 15; break;
	case WP_RAILGUN:			minAmmo = 2; break;
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case WP_CHAINGUN:			minAmmo = 40; break;
#endif
	default:					minAmmo = 3; break;
	}
	ammo = ps->ammo[weapon];
	if ( weapon != WP_GAUNTLET && ammo >= 0 && ammo < minAmmo ) {
		return qfalse;
	}
	switch ( weapon ) {
	case WP_GAUNTLET:
		return dist < 150 * s && ps->stats[STAT_HEALTH] >= 50;
	case WP_SHOTGUN:
		return dist < 450 * s;
	case WP_MACHINEGUN:
		return dist > 900 / s;			// only as the long range marksman gun
	case WP_LIGHTNING:
		return dist < 700 * s;
	case WP_PLASMAGUN:
		return dist < 1000 * s;
	case WP_ROCKET_LAUNCHER:
		return dist > 160 / s && dist < 1500 * s;
	case WP_GRENADE_LAUNCHER:
		return dist > 160 / s && dist < 800 * s;
	case WP_RAILGUN:
		return dist > 250 / s;
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case WP_CHAINGUN:
		return dist < 900 * s;
#endif
	default:
		return qtrue;
	}
}

static qboolean BotPersonaPrefers( const rmPersona_t *p, int weapon ) {
	int		i;

	for ( i = 0 ; i < 4 && p->weapons[i] != WP_NONE ; i++ ) {
		if ( p->weapons[i] == weapon ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
==================
BotPersonaWeapon

Called from BotChooseWeapon with the stock pick: lean towards the favourite guns
when they are owned, loaded and right for the distance. Otherwise keep the stock pick.
==================
*/
int BotPersonaWeapon( bot_state_t *bs, int stockWeapon ) {
	const rmPersona_t	*p;
	gentity_t			*enemy;
	float				dist;
	int					i;

	// weapon roulette: the server picks the gun, don't switch back
	if ( level.mutator == MUT_WEAPON_ROULETTE && bs->cur_ps.weapon > WP_NONE && bs->cur_ps.weapon < WP_NUM_WEAPONS ) {
		return bs->cur_ps.weapon;
	}
	if ( !trap_Cvar_VariableIntegerValue( "bot_rearm" ) ) {
		return stockWeapon;
	}
	p = BotPersona( bs );
	if ( p->weapons[0] == WP_NONE || stockWeapon == WP_BFG
		|| level.rmMode == RM_MODE_GUNGAME || level.rmMode == RM_MODE_INSTAGIB || level.rmMode == RM_MODE_OITC ) {
		return stockWeapon;
	}
	// only in a fight: out of combat the stock pick is the best all-rounder to meet someone with
	if ( bs->enemy < 0 || bs->enemy >= MAX_CLIENTS ) {
		return stockWeapon;
	}
	enemy = &g_entities[bs->enemy];
	if ( !enemy->inuse || !enemy->client || enemy->health <= 0 ) {
		return stockWeapon;
	}
	dist = Distance( bs->origin, enemy->r.currentOrigin );

	// stormcaller: soak them with plasma, then shock the wet target with lightning
	if ( bs->rmPersona == RM_PERSONA_STORMCALLER ) {
#ifndef MISSIONPACK
		if ( enemy->client->ps.powerups[PW_WET] > level.time && BotPersonaWeaponOK( bs, WP_LIGHTNING, dist, bs->weaponnum == WP_LIGHTNING ) ) {
			return WP_LIGHTNING;
		}
		// v4: close enough to shock but still dry: soak first (the lightning comes once they're wet)
		if ( BotTrickOn( BOT_TRICK_MOVES ) && G_RearmOn( RS_ELEMENTS ) && dist < 550
			&& BotPersonaWeaponOK( bs, WP_LIGHTNING, dist, qtrue )
			&& BotPersonaWeaponOK( bs, WP_PLASMAGUN, dist, bs->weaponnum == WP_PLASMAGUN ) ) {
			return WP_PLASMAGUN;
		}
#endif
		if ( dist > 600 && BotPersonaWeaponOK( bs, WP_PLASMAGUN, dist, bs->weaponnum == WP_PLASMAGUN ) ) {
			return WP_PLASMAGUN;
		}
	}

	if ( BotTrickOn( BOT_TRICK_MOVES ) ) {
#ifndef MISSIONPACK
		// anyone: a soaked enemy in beam range takes 1.5x lightning (the sniper keeps its rail at range)
		if ( G_RearmOn( RS_ELEMENTS ) && enemy->client->ps.powerups[PW_WET] > level.time + 400
			&& ( bs->rmPersona != RM_PERSONA_SNIPER || dist < 400 )
			&& BotPersonaWeaponOK( bs, WP_LIGHTNING, dist, bs->weaponnum == WP_LIGHTNING ) ) {
			return WP_LIGHTNING;
		}
#endif
		// an explosive prop next to them: splash sets it off
		if ( BotTrickOn( BOT_TRICK_PROPS ) && BotRearmPropNearEnemy( bs, enemy, NULL ) ) {
			if ( BotPersonaWeaponOK( bs, WP_ROCKET_LAUNCHER, dist, bs->weaponnum == WP_ROCKET_LAUNCHER ) && dist > 300 ) {
				return WP_ROCKET_LAUNCHER;
			}
			if ( BotPersonaWeaponOK( bs, WP_GRENADE_LAUNCHER, dist, bs->weaponnum == WP_GRENADE_LAUNCHER ) && dist > 300 && dist < 650 ) {
				return WP_GRENADE_LAUNCHER;
			}
		}
	}

	// descent: a gun three levels better than the stock pick (an evolved one counts +4) that fits wins
	if ( G_RL_Active() ) {
		gentity_t	*self = &g_entities[bs->client];
		int			w, b, best = WP_NONE, bestB = 2 + G_DS_BotWeaponBonus( self, stockWeapon );

		for ( w = WP_GAUNTLET + 1 ; w < WP_NUM_WEAPONS ; w++ ) {
			b = G_DS_BotWeaponBonus( self, w ) + ( w == bs->weaponnum ? 1 : 0 );
			if ( b > bestB && BotPersonaWeaponOK( bs, w, dist, w == bs->weaponnum ) ) {
				best = w;
				bestB = b;
			}
		}
		if ( best != WP_NONE ) {
			return best;
		}
	}

	// the stock pick is already a favourite that fits
	if ( BotPersonaPrefers( p, stockWeapon ) && BotPersonaWeaponOK( bs, stockWeapon, dist, stockWeapon == bs->weaponnum ) ) {
		return stockWeapon;
	}
	// keep the favourite in hand while it still fits (no swap churn)
	if ( BotPersonaPrefers( p, bs->weaponnum ) && BotPersonaWeaponOK( bs, bs->weaponnum, dist, qtrue ) ) {
		return bs->weaponnum;
	}
	for ( i = 0 ; i < 4 && p->weapons[i] != WP_NONE ; i++ ) {
		if ( BotPersonaWeaponOK( bs, p->weapons[i], dist, qfalse ) ) {
			return p->weapons[i];
		}
	}
	return stockWeapon;
}

/*
==================
BotPersonaSafeBehind

Floor (not a pit, lava or slime) KITE_LOOK units along dir
==================
*/
static qboolean BotPersonaSafeBehind( playerState_t *ps, const vec3_t dir ) {
	trace_t	tr;
	vec3_t	start, end;

	VectorMA( ps->origin, KITE_LOOK, dir, end );
	trap_Trace( &tr, ps->origin, NULL, NULL, end, ps->clientNum, MASK_PLAYERSOLID );
	// stop short of any wall, then look for floor below
	VectorMA( tr.endpos, -16, dir, start );
	VectorCopy( start, end );
	end[2] -= KITE_DROP;
	trap_Trace( &tr, start, NULL, NULL, end, ps->clientNum, MASK_PLAYERSOLID | CONTENTS_LAVA | CONTENTS_SLIME );
	return tr.fraction < 1.0f && !tr.allsolid && !( tr.surfaceFlags & SURF_SKY )
		&& !( tr.contents & ( CONTENTS_LAVA | CONTENTS_SLIME ) );
}

/*
==================
BotPersonaSpacing

The stock fight movement closes to 140 units. Styles with a preferred distance keep strafing
but back off instead of closing in while the enemy is inside it.
==================
*/
void BotPersonaSpacing( bot_state_t *bs, playerState_t *ps, usercmd_t *ucmd, gentity_t *enemy, float dist, qboolean visible ) {
	const rmPersona_t	*p = BotPersona( bs );
	vec3_t				angles, fwd, right, to, move;
	float				pref, along, f, r, m;

	if ( p->engageDist <= 0 || !enemy || !visible || bs->ainode != AINode_Battle_Fight
		|| ps->groundEntityNum == ENTITYNUM_NONE ) {
		return;
	}
	if ( ps->weapon == WP_GAUNTLET || ps->weapon == WP_SHOTGUN ) {
		return;			// these want contact
	}
	pref = p->engageDist;
	if ( ps->weapon == WP_LIGHTNING && pref > 550 ) {
		pref = 550;		// stay inside the beam's reach
	}
	if ( dist >= pref - KITE_BAND ) {
		return;
	}

	VectorSet( angles, 0, SHORT2ANGLE( ( ucmd->angles[YAW] + ps->delta_angles[YAW] ) & 65535 ), 0 );
	AngleVectors( angles, fwd, right, NULL );
	VectorSubtract( enemy->r.currentOrigin, ps->origin, to );
	to[2] = 0;
	if ( VectorNormalize( to ) < 0.001f ) {
		return;
	}
	VectorScale( fwd, ucmd->forwardmove, move );
	VectorMA( move, ucmd->rightmove, right, move );
	move[2] = 0;
	along = DotProduct( move, to );
	if ( along <= 0 ) {
		return;			// already strafing or backing off
	}

	if ( level.time >= bs->rmKiteNext ) {
		bs->rmKiteNext = level.time + 300;
		VectorNegate( to, to );
		bs->rmKiteOK = BotPersonaSafeBehind( ps, to );
		VectorNegate( to, to );
	}
	if ( !bs->rmKiteOK ) {
		return;
	}

	// mirror the closing part of the move: same strafe, away from the enemy
	VectorMA( move, -2 * along, to, move );
	f = DotProduct( fwd, move );
	r = DotProduct( right, move );
	m = fabs( f ) > fabs( r ) ? fabs( f ) : fabs( r );
	if ( m < 0.001f ) {
		return;
	}
	ucmd->forwardmove = (int)( 127 * f / m );
	ucmd->rightmove = (int)( 127 * r / m );
}

/*
==================
BotPersonaDirectHit

Means of death of a direct projectile hit (the kind that counts as an airshot)
==================
*/
static qboolean BotPersonaDirectHit( int mod ) {
	switch ( mod ) {
	case MOD_ROCKET:
	case MOD_GRENADE:
	case MOD_PLASMA:
	case MOD_BFG:
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case MOD_NAIL:
#endif
		return qtrue;
	default:
		return qfalse;
	}
}

static qboolean BotPersonaHighAir( gentity_t *ent ) {
	trace_t	tr;
	vec3_t	down;

	VectorCopy( ent->r.currentOrigin, down );
	down[2] -= TAUNT_AIR_HEIGHT - ent->r.mins[2];
	trap_Trace( &tr, ent->r.currentOrigin, NULL, NULL, down, ent->s.number, MASK_SOLID );
	return tr.fraction == 1.0f;
}

static qboolean BotPersonaFoe( bot_state_t *bs, int client ) {
	return client >= 0 && client < MAX_CLIENTS && client != bs->client && g_entities[client].inuse
		&& g_entities[client].client && !BotSameTeam( bs, client );
}

/*
==================
BotPersonaTaunt

Queue a line for this event: about a third of the time, one per TAUNT_GAP, never over the stock chat
==================
*/
static void BotPersonaTaunt( bot_state_t *bs, int event, int other ) {
	char		name[32];
	const char	*line;
	int			i;

	if ( bot_nochat.integer || level.intermissiontime || bs->rmTauntAt || level.time < bs->rmTauntNext ) {
		return;
	}
	if ( bs->lastchat_time > 0 && FloatTime() - bs->lastchat_time < TAUNT_GAP / 1000 ) {
		return;		// the stock chat just said something
	}
	if ( random() >= TAUNT_CHANCE ) {
		return;
	}
	i = rand() % TAUNT_LINES;
	if ( i == bs->rmTauntLast ) {
		i = ( i + 1 ) % TAUNT_LINES;
	}
	bs->rmTauntLast = i;
	line = rmTaunts[bs->rmPersona][event][i];

	name[0] = '\0';
	if ( other >= 0 && other < MAX_CLIENTS ) {
		EasyClientName( other, name, sizeof( name ) );
	}
	if ( !name[0] ) {
		Q_strncpyz( name, "you", sizeof( name ) );
	}
	Com_sprintf( bs->rmTauntText, sizeof( bs->rmTauntText ), line, name );
	// a human-ish pause before typing it
	bs->rmTauntAt = level.time + 400 + (int)( random() * 800 );
	bs->rmTauntNext = bs->rmTauntAt + TAUNT_GAP;
}

/*
==================
BotPersonaFrame

Every server frame from BotRearmInput (alive or dead): spot kills, deaths, multi-kills and
airshots taken, and say a queued taunt when its time comes
==================
*/
void BotPersonaFrame( bot_state_t *bs ) {
	gentity_t	*ent = &g_entities[bs->client];
	gclient_t	*cl = ent->client;
	int			kills, deaths, health, event = -1, other = -1;

	if ( !cl ) {
		return;
	}
	BotPersona( bs );

	kills = bs->num_kills - bs->rmTauntKills;
	deaths = bs->num_deaths - bs->rmTauntDeaths;
	bs->rmTauntKills = bs->num_kills;
	bs->rmTauntDeaths = bs->num_deaths;

	if ( deaths > 0 && !bs->botsuicide && BotPersonaFoe( bs, bs->lastkilledby ) ) {
		event = TAUNT_DEATH;
		other = bs->lastkilledby;
	}
	if ( kills > 0 ) {
		if ( level.time - bs->rmTauntChainTime < TAUNT_MULTI_TIME ) {
			bs->rmTauntChain += kills;
		} else {
			bs->rmTauntChain = kills;
		}
		bs->rmTauntChainTime = level.time;
		if ( event < 0 && BotPersonaFoe( bs, bs->lastkilledplayer ) ) {
			event = bs->rmTauntChain >= 2 ? TAUNT_MULTI : TAUNT_KILL;
			other = bs->lastkilledplayer;
		}
	}

	// airshot taken: a real chunk of health gone to a direct projectile while well off the ground
	health = cl->ps.stats[STAT_HEALTH];
	if ( event < 0 && health > 0 && bs->rmTauntHealth > 0 && health < bs->rmTauntHealth - 14
		&& cl->ps.groundEntityNum == ENTITYNUM_NONE && BotPersonaDirectHit( cl->lasthurt_mod )
		&& BotPersonaFoe( bs, cl->lasthurt_client ) && BotPersonaHighAir( ent ) ) {
		event = TAUNT_AIRSHOT;
		other = cl->lasthurt_client;
	}
	bs->rmTauntHealth = health;

	if ( event >= 0 ) {
		BotPersonaTaunt( bs, event, other );
	}

	if ( bs->rmTauntAt && level.time >= bs->rmTauntAt ) {
		bs->rmTauntAt = 0;
		if ( !bot_nochat.integer && !level.intermissiontime && bs->rmTauntText[0] ) {
			trap_EA_Say( bs->client, bs->rmTauntText );
			bs->lastchat_time = FloatTime();		// the stock chat holds off too, so they never pile up
		}
		bs->rmTauntText[0] = '\0';
	}
}

/*
==================
BotPersonaPickPerk

Which of the offered perks (count of them) a bot takes: returns 0..count-1.
Every perk has a base worth, each style adds its favourites on top, perks the bot can't use
(the move is switched off) are worth little, and a random roll keeps the picks varied.
==================
*/
typedef struct {
	int		persona;
	int		perk;
	float	weight;
} rmPerkLike_t;

// base worth for everyone (BALANCED takes these straight)
static const float rmPerkBase[PERK_NUM] = {
	3.0f,	// THICKSKIN
	3.0f,	// HEAVYCALIBER
	2.5f,	// QUICKHANDS
	2.0f,	// THIRDWIND
	1.5f,	// OVERCHARGER
	2.5f,	// BLOODTHIRST
	2.0f,	// HEADHUNTER
	1.5f,	// SKYHUNTER
	1.5f,	// PYROMANIAC
	1.0f,	// CONDUCTOR
	2.5f,	// IRONCLAD
	2.0f,	// SCAVENGER
	2.5f,	// ADRENALINE
	1.5f,	// GLASSCANNON
	2.0f,	// BLINK
	1.0f,	// GROUNDZERO
	1.5f,	// SLIPSTREAM
	1.5f,	// RICOCHET
	1.5f,	// CHAINREACTION
	2.0f,	// LASTSTAND
	1.5f,	// FEATHERWEIGHT
	1.5f,	// MAGNETHOOK
	1.5f,	// BUTCHER
	1.5f,	// KINETIC
	1.5f,	// MIRAGE
	2.0f	// THORNS
};

static const rmPerkLike_t rmPerkLikes[] = {
	{ RM_PERSONA_RUSHER,		PERK_ADRENALINE,	6 },
	{ RM_PERSONA_RUSHER,		PERK_THIRDWIND,		5 },
	{ RM_PERSONA_RUSHER,		PERK_BLINK,			5 },
	{ RM_PERSONA_RUSHER,		PERK_KINETIC,		4 },
	{ RM_PERSONA_RUSHER,		PERK_SLIPSTREAM,	3 },
	{ RM_PERSONA_RUSHER,		PERK_GLASSCANNON,	2 },

	{ RM_PERSONA_SNIPER,		PERK_HEADHUNTER,	7 },
	{ RM_PERSONA_SNIPER,		PERK_SKYHUNTER,		5 },
	{ RM_PERSONA_SNIPER,		PERK_OVERCHARGER,	4 },
	{ RM_PERSONA_SNIPER,		PERK_HEAVYCALIBER,	3 },
	{ RM_PERSONA_SNIPER,		PERK_GLASSCANNON,	2 },

	{ RM_PERSONA_TRICKSTER,		PERK_MIRAGE,		6 },
	{ RM_PERSONA_TRICKSTER,		PERK_BLINK,			6 },
	{ RM_PERSONA_TRICKSTER,		PERK_FEATHERWEIGHT,	5 },
	{ RM_PERSONA_TRICKSTER,		PERK_RICOCHET,		4 },
	{ RM_PERSONA_TRICKSTER,		PERK_SKYHUNTER,		3 },
	{ RM_PERSONA_TRICKSTER,		PERK_GROUNDZERO,	2 },

	{ RM_PERSONA_BRAWLER,		PERK_THICKSKIN,		6 },
	{ RM_PERSONA_BRAWLER,		PERK_THORNS,		6 },
	{ RM_PERSONA_BRAWLER,		PERK_LASTSTAND,		5 },
	{ RM_PERSONA_BRAWLER,		PERK_GROUNDZERO,	4 },
	{ RM_PERSONA_BRAWLER,		PERK_MAGNETHOOK,	4 },
	{ RM_PERSONA_BRAWLER,		PERK_BUTCHER,		3 },

	{ RM_PERSONA_PYRO,			PERK_PYROMANIAC,	7 },
	{ RM_PERSONA_PYRO,			PERK_CHAINREACTION,	6 },
	{ RM_PERSONA_PYRO,			PERK_RICOCHET,		4 },
	{ RM_PERSONA_PYRO,			PERK_BUTCHER,		3 },
	{ RM_PERSONA_PYRO,			PERK_HEAVYCALIBER,	2 },

	{ RM_PERSONA_STORMCALLER,	PERK_CONDUCTOR,		7 },
	{ RM_PERSONA_STORMCALLER,	PERK_OVERCHARGER,	6 },
	{ RM_PERSONA_STORMCALLER,	PERK_QUICKHANDS,	3 },
	{ RM_PERSONA_STORMCALLER,	PERK_IRONCLAD,		2 },

	{ -1, 0, 0 }
};

static float BotPersonaPerkWorth( int persona, gclient_t *cl, int perk ) {
	int		flags = cl->ps.stats[STAT_RMFLAGS];
	int		flags2 = RM_FLAGS2( &cl->ps );
	float	w;
	int		i;

	if ( perk < 0 || perk >= PERK_NUM ) {
		return -1;
	}
	w = rmPerkBase[perk];
	for ( i = 0 ; rmPerkLikes[i].persona >= 0 ; i++ ) {
		if ( rmPerkLikes[i].persona == persona && rmPerkLikes[i].perk == perk ) {
			w += rmPerkLikes[i].weight;
		}
	}
	// perks that need a move that is switched off are close to worthless
	switch ( perk ) {
	case PERK_THIRDWIND:
	case PERK_BLINK:
	case PERK_MIRAGE:
	case PERK_PYROMANIAC:
		if ( !( flags & RMF_DASH ) ) {
			w *= 0.1f;
		}
		break;
	case PERK_FEATHERWEIGHT:
		if ( !( flags & RMF_DOUBLEJUMP ) ) {
			w *= 0.3f;
		}
		break;
	case PERK_OVERCHARGER:
		if ( !( flags & RMF_CHARGE ) ) {
			w *= 0.1f;
		}
		break;
	case PERK_GROUNDZERO:
		if ( !( flags2 & RMF2_SLAM ) ) {
			w *= 0.1f;
		}
		break;
	case PERK_SLIPSTREAM:
		if ( !( flags2 & RMF2_SLIDE ) ) {
			w *= 0.1f;
		}
		break;
	case PERK_MAGNETHOOK:
		if ( !g_offhandHook.integer || !G_RL_HookAllowed( cl ) ) {
			w *= 0.1f;
		}
		break;
	case PERK_HEADHUNTER:
		if ( !G_RearmOn( RS_HEADSHOTS ) ) {
			w *= 0.2f;
		}
		break;
	case PERK_GLASSCANNON:
		// no glass cannon when it would leave us nearly dead
		if ( cl->ps.stats[STAT_MAX_HEALTH] <= 75 ) {
			w *= 0.2f;
		}
		break;
	default:
		break;
	}
	return w;
}

int BotPersonaPickPerk( int client, const int *offers, int count ) {
	bot_state_t	*bs;
	gclient_t	*cl;
	int			i, best, persona;
	float		w, bestW;

	if ( count <= 0 || !offers ) {
		return 0;
	}
	if ( client < 0 || client >= MAX_CLIENTS || !g_entities[client].client
		|| !trap_Cvar_VariableIntegerValue( "bot_rearm" ) || !BotTrickOn( BOT_TRICK_MOVES ) ) {
		return rand() % count;
	}
	cl = g_entities[client].client;
	bs = botstates[client];
	persona = ( bs && bs->inuse ) ? (int)( BotPersona( bs ) - rmPersonas ) : RM_PERSONA_BALANCED;

	best = 0;
	bestW = -999;
	for ( i = 0 ; i < count ; i++ ) {
		// the roll keeps it a preference, not a rule (a strong base perk can win)
		w = BotPersonaPerkWorth( persona, cl, offers[i] ) + random() * 3.0f;
		if ( w > bestW ) {
			bestW = w;
			best = i;
		}
	}
	if ( BotTrickDebug() ) {
		G_Printf( "bot_tricks: client %i (%s) picks perk %s from", client, rmPersonas[persona].name,
			( offers[best] >= 0 && offers[best] < PERK_NUM ) ? bg_perkNames[offers[best]] : "?" );
		for ( i = 0 ; i < count ; i++ ) {
			G_Printf( " %s", ( offers[i] >= 0 && offers[i] < PERK_NUM ) ? bg_perkNames[offers[i]] : "?" );
		}
		G_Printf( "\n" );
	}
	return best;
}
