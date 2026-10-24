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
// g_style.c -- rearmed style meter (D to SSS, fed by flashy kills), pick-a-perk builds,
// slow motion moments and the chain reaction corpse bombs

#include "g_local.h"

#define STYLE_IDLE_TIME		2000	// style starts draining this long after the last stylish act
#define STYLE_DRAIN			45		// points per second once draining
#define STYLE_HURT_SCALE	1.2f	// points lost per point of damage taken
#define STYLE_GORE			STYLE_RANK_S	// from this rank on, kills gib
#define STYLE_EVENT_SCALE	1.5f	// style gain while a random event runs

#define PERK_FIRST_KILLS	2		// kills to the first perk offer
#define PERK_EVERY_KILLS	3		// then one offer per this many kills
#define PERK_MAX			8		// perks per match
#define PERK_OFFER_TIME		15000	// an unanswered offer picks for you

#define SLOWMO_COOLDOWN		6000	// between the player's slow motion moments (the match winner ignores it)
#define QUICK_KILL_TIME		1000	// kills this close together are a multi kill for slow motion

#define CORPSE_BOMB_DELAY	1000
#define CORPSE_BOMB_DAMAGE	85
#define CORPSE_BOMB_RADIUS	190
#define MAX_CORPSE_BOMBS	12

#define FL_RM_RICOCHETED	0x00040000	// gentity flags: this missile already used its ricochet

void GibEntity( gentity_t *self, int killer );
void G_BounceMissile( gentity_t *ent, trace_t *trace );
void G_MeteorImpact( gentity_t *ent, trace_t *trace );

static int	lastDashTime[MAX_CLIENTS];
static int	lastKillTime[MAX_CLIENTS];
static int	quickKills[MAX_CLIENTS];
static int	thornsPending[MAX_CLIENTS];
static int	thornsFrom[MAX_CLIENTS];
static int	slowmoReady;
static int	corpseBombs;

/*
===============
G_StyleInit

Called from G_RearmInit on every map start / restart
===============
*/
void G_StyleInit( void ) {
	memset( lastDashTime, 0, sizeof( lastDashTime ) );
	memset( lastKillTime, 0, sizeof( lastKillTime ) );
	memset( quickKills, 0, sizeof( quickKills ) );
	memset( thornsPending, 0, sizeof( thornsPending ) );
	slowmoReady = 0;
	corpseBombs = 0;
}

/*
===============================================================================

STYLE

===============================================================================
*/

/*
===============
G_StyleAdd

Adds style points and tells the player what earned them
===============
*/
// feed labels sent this server frame, per client: a shotgun blast of headshot pellets
// must not flood the reliable command buffer (the engine drops a client at 64 pending)
static int			styleLabelTime[MAX_CLIENTS];
static int			styleLabelCount[MAX_CLIENTS];
static const char	*styleLabelLast[MAX_CLIENTS];

void G_StyleAdd( gentity_t *ent, int points, const char *label ) {
	gclient_t	*cl;
	int			n;

	if ( !G_RearmOn( RS_STYLE ) || !ent || !ent->client || level.warmupTime || level.intermissiontime ) {
		return;
	}
	if ( level.mutator != MUT_NONE && points > 0 ) {
		points = (int)( points * STYLE_EVENT_SCALE );
	}
	points = G_DS_SkillStyle( ent, points );	// SKILLS: CARNAGE xp, SHOWOFF
	cl = ent->client;
	cl->style += points;
	if ( cl->style > STYLE_MAX ) {
		cl->style = STYLE_MAX;
	}
	cl->styleTime = level.time;
	cl->ps.stats[STAT_STYLE] = (int)cl->style;
	if ( label && !( ent->r.svFlags & SVF_BOT ) ) {
		n = ent - g_entities;
		if ( styleLabelTime[n] != level.time ) {
			styleLabelTime[n] = level.time;
			styleLabelCount[n] = 0;
			styleLabelLast[n] = NULL;
		}
		if ( styleLabelCount[n] >= 6 || label == styleLabelLast[n] ) {
			return;		// the points still count, only the feed line is skipped
		}
		styleLabelCount[n]++;
		styleLabelLast[n] = label;
		trap_SendServerCommand( n, va( "rmsty %i \"%s\"", points, label ) );
	}
}

/*
===============
G_StyleFrame

Drain over time
===============
*/
void G_StyleFrame( gentity_t *ent ) {
	gclient_t	*cl = ent->client;

	if ( cl->style <= 0 ) {
		return;
	}
	if ( level.time - cl->styleTime > STYLE_IDLE_TIME ) {
		cl->style -= STYLE_DRAIN * ( level.time - level.previousTime ) * 0.001f;
		if ( cl->style < 0 ) {
			cl->style = 0;
		}
	}
	cl->ps.stats[STAT_STYLE] = (int)cl->style;
}

/*
===============
G_StyleOnDamage

Called when a hit lands (from the hit confirm in G_Damage)
===============
*/
void G_StyleOnDamage( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, int damage, int hitFlags ) {
	if ( !attacker || !attacker->client || !targ->client || attacker == targ ) {
		return;
	}
	if ( hitFlags & HIT_HEADSHOT ) {
		G_StyleAdd( attacker, 25, "HEADSHOT" );
	}
	if ( hitFlags & HIT_AIRSHOT ) {
		G_StyleAdd( attacker, 40, "AIRSHOT" );
	} else if ( targ->client->ps.groundEntityNum == ENTITYNUM_NONE && inflictor && inflictor->s.eType == ET_MISSILE ) {
		G_StyleAdd( attacker, 12, "JUGGLE" );
	}
	if ( inflictor && inflictor->s.eType == ET_MISSILE && ( inflictor->s.generic1 & ALTFIRE_REFLECTED ) ) {
		G_StyleAdd( attacker, 50, "RETURN TO SENDER" );
	}
	if ( inflictor && ( inflictor->flags & FL_RM_RICOCHETED ) ) {
		G_StyleAdd( attacker, 20, "BANK SHOT" );
	}
	// taking hits costs style
	if ( targ->client->style > 0 && damage > 0 ) {
		targ->client->style -= damage * STYLE_HURT_SCALE;
		if ( targ->client->style < 0 ) {
			targ->client->style = 0;
		}
		targ->client->ps.stats[STAT_STYLE] = (int)targ->client->style;
	}
}

/*
===============
G_StyleMatchPoint

Did this kill just win the match?
===============
*/
static qboolean G_StyleMatchPoint( gentity_t *attacker ) {
	gclient_t	*cl = attacker->client;

	if ( level.rmMode == RM_MODE_GUNGAME ) {
		return cl->pers.ggLevel >= GG_LEVELS;
	}
	if ( g_fraglimit.integer <= 0 || G_RearmIgnoresFraglimit() || g_gametype.integer >= GT_CTF ) {
		return qfalse;
	}
	if ( g_gametype.integer == GT_TEAM ) {
		return level.teamScores[cl->sess.sessionTeam] >= G_FragLimit();
	}
	return cl->ps.persistant[PERS_SCORE] >= G_FragLimit();
}

/*
===============
G_StyleSlowMo

Slow motion moments: the match-winning kill, bullet time, multi kills and SSS kills
===============
*/
static void G_StyleSlowMo( gentity_t *self, gentity_t *attacker, int quick, qboolean decap, qboolean gibbed ) {
	qboolean	human;

	// the match-winning frag always gets it, and the longest
	if ( G_StyleMatchPoint( attacker ) ) {
		G_SlowMoEx( 0.3f, 550, qtrue );
		slowmoReady = level.time + SLOWMO_COOLDOWN;
		return;
	}
	if ( level.mutator == MUT_BULLET_TIME ) {
		G_SlowMo( 0.4f, 300 );		// every kill (G_SlowMo keeps a gap between them)
		return;
	}
	// the rest only for the player's own kills, and not too often
	human = !( attacker->r.svFlags & SVF_BOT );
	if ( !human || level.time < slowmoReady ) {
		return;
	}
	if ( quick >= 3 ) {
		G_SlowMo( 0.33f, 420 );
	} else if ( quick == 2 ) {
		G_SlowMo( 0.37f, 360 );
	} else if ( decap || gibbed ) {
		G_SlowMo( 0.4f, 300 );		// headshot finisher, gib
	} else if ( attacker->client->style >= STYLE_RANK_SSS ) {
		G_SlowMo( 0.45f, 260 );
	} else {
		return;
	}
	slowmoReady = level.time + SLOWMO_COOLDOWN;
}

static void G_PerksOnVictim( gentity_t *self, gentity_t *attacker, int mod, qboolean gibbed );

/*
===============
G_StyleOnKill

Called from player_die. Returns qtrue when the kill should gib (high style rank, blood moon)
===============
*/
qboolean G_StyleOnKill( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int mod ) {
	gclient_t	*cl;
	qboolean	charged, gore, realKill, decap = qfalse;
	int			n, quick;

	// dying costs all your style
	self->client->style = 0;
	self->client->ps.stats[STAT_STYLE] = 0;

	realKill = attacker && attacker->client && attacker != self && !OnSameTeam( self, attacker );
	gore = level.mutator == MUT_BLOOD_MOON && !level.warmupTime;

	// chain reaction: the mutator blows up every corpse, the perk only your victims
	if ( !level.warmupTime && ( level.mutator == MUT_CHAIN_REACTION
		|| ( realKill && G_HasPerk( attacker, PERK_CHAINREACTION ) ) ) ) {
		G_CorpseBomb( self, realKill ? attacker : NULL );
	}
	if ( !realKill ) {
		return gore;
	}
	cl = attacker->client;
	n = attacker - g_entities;
	if ( G_RearmOn( RS_STYLE ) && cl->style >= STYLE_GORE ) {
		gore = qtrue;
	}

	// multi kill tracking for slow motion
	if ( level.time - lastKillTime[n] < QUICK_KILL_TIME ) {
		quickKills[n]++;
	} else {
		quickKills[n] = 1;
	}
	lastKillTime[n] = level.time;
	quick = quickKills[n];

	charged = ( inflictor && inflictor->s.eType == ET_MISSILE && ( inflictor->s.generic1 & ALTFIRE_CHARGED ) )
		|| level.time - cl->chargedShotTime < 100;
	if ( charged ) {
		G_StyleAdd( attacker, 70, "CHARGED KILL" );
	} else {
		G_StyleAdd( attacker, 60, "KILL" );
	}
	if ( self->client->ps.groundEntityNum == ENTITYNUM_NONE ) {
		if ( cl->ps.groundEntityNum == ENTITYNUM_NONE && mod != MOD_FALLING ) {
			G_StyleAdd( attacker, 55, "MIDAIR" );
		} else {
			G_StyleAdd( attacker, 30, "SKY KILL" );
		}
	}
	switch ( mod ) {
	case MOD_BURN:		G_StyleAdd( attacker, 40, "COOKED" ); break;
	case MOD_THUNDER:	G_StyleAdd( attacker, 40, "THUNDERSTRUCK" ); break;
	case MOD_SLAM:		G_StyleAdd( attacker, 70, "SLAM DUNK" ); break;
	case MOD_PROP:		break;		// g_physics.c awards BARREL KILL / TANK KILL itself
	case MOD_WALLSPLAT:	G_StyleAdd( attacker, 60, "SPLATTERED" ); break;
	case MOD_CORPSE:	G_StyleAdd( attacker, 65, "CHAIN REACTION" ); break;
	case MOD_METEOR:	G_StyleAdd( attacker, 50, "METEOR" ); break;
	case MOD_THORNS:	G_StyleAdd( attacker, 45, "THORNS" ); break;
	case MOD_GRAPPLE:	G_StyleAdd( attacker, 40, "HOOKED" ); break;
	default:			break;
	}
	if ( inflictor && ( inflictor->flags & FL_RM_RICOCHETED ) ) {
		G_StyleAdd( attacker, 40, "RICOCHET" );
	}
	if ( RM_FLAGS2( &cl->ps ) & RMS2_SLIDING ) {
		G_StyleAdd( attacker, 35, "SLIDE KILL" );
	}
	if ( level.time - lastDashTime[n] < 700 ) {
		if ( G_HasPerk( attacker, PERK_BLINK ) ) {
			G_StyleAdd( attacker, 45, "BLINK KILL" );
		} else {
			G_StyleAdd( attacker, 30, "DASH KILL" );
		}
	}
	if ( quick == 2 ) {
		G_StyleAdd( attacker, 50, "DOUBLE KILL" );
	} else if ( quick >= 3 ) {
		G_StyleAdd( attacker, 80, "MULTI KILL" );
	}
	if ( cl->hitTarget == self->s.number && ( cl->hitFlags & HIT_HEADSHOT ) && mod != MOD_BURN
		&& mod != MOD_THUNDER && mod != MOD_LIGHTNING && !( inflictor && inflictor->s.eType == ET_MISSILE
		&& inflictor->splashDamage ) ) {
		G_StyleAdd( attacker, 45, "DECAPITATED" );		// g_gore.c takes the head off instead of gibbing
		decap = qtrue;
	} else if ( self->health <= GIB_HEALTH || gore ) {
		G_StyleAdd( attacker, 35, "GIBBED" );
	}
	if ( !level.warmupTime ) {
		G_StyleSlowMo( self, attacker, quick, decap, self->health <= GIB_HEALTH );
		G_PerksOnVictim( self, attacker, mod, self->health <= GIB_HEALTH || gore );
	}
	return gore;
}

/*
===============================================================================

PERKS

===============================================================================
*/

static qboolean G_PerksOn( void ) {
	// descent: perk cards work whatever the switch says (the kill offers stay off)
	return ( G_RearmOn( RS_PERKS ) && level.rmMode == RM_MODE_NORMAL ) || G_RL_Active();
}

qboolean G_HasPerk( gentity_t *ent, int perk ) {
	return ent && ent->client && ( ent->client->pers.perks & ( 1 << perk ) ) && G_PerksOn();
}

/*
===============
G_PerkPmoveBits

Perks the movement code has to know about (client prediction)
===============
*/
int G_PerkPmoveBits( gclient_t *cl ) {
	int		bits = 0;

	if ( !G_PerksOn() ) {
		return 0;
	}
	if ( cl->pers.perks & ( 1 << PERK_QUICKHANDS ) ) {
		bits |= RMP_QUICKHANDS;
	}
	if ( cl->pers.perks & ( 1 << PERK_THIRDWIND ) ) {
		bits |= RMP_THIRDWIND;
	}
	if ( cl->pers.perks & ( 1 << PERK_OVERCHARGER ) ) {
		bits |= RMP_OVERCHARGER;
	}
	return bits;
}

int G_PerkPmoveBits2( gclient_t *cl ) {
	int		bits = 0;

	if ( !G_PerksOn() ) {
		return 0;
	}
	if ( cl->pers.perks & ( 1 << PERK_BLINK ) ) {
		bits |= RMP2_BLINK;
	}
	if ( cl->pers.perks & ( 1 << PERK_SLIPSTREAM ) ) {
		bits |= RMP2_SLIPSTREAM;
	}
	if ( cl->pers.perks & ( 1 << PERK_FEATHERWEIGHT ) ) {
		bits |= RMP2_FEATHER;
	}
	return bits;
}

/*
===============
G_PerkApplyMaxHealth

Thick skin raises, glass cannon lowers the health limit. spawn: also refill to the new max.
===============
*/
void G_PerkApplyMaxHealth( gentity_t *ent, qboolean spawn ) {
	gclient_t	*cl = ent->client;
	int			max;

	if ( ( level.rmMode != RM_MODE_NORMAL && !G_RL_Active() ) || !cl ) {
		return;		// juggernaut owns the max health
	}
	max = cl->pers.maxHealth + G_RL_MaxHealthBonus( ent );
	if ( G_HasPerk( ent, PERK_THICKSKIN ) ) {
		max += G_RL_Active() ? DSP_THICKSKIN_HP : 25;		// DESCENT: single digits
	}
	if ( G_HasPerk( ent, PERK_GLASSCANNON ) ) {
		max -= G_RL_Active() ? DSP_GLASS_HP : 35;
	}
	if ( max < 25 ) {
		max = 25;
	}
	if ( cl->ps.stats[STAT_MAX_HEALTH] == max ) {
		return;
	}
	cl->ps.stats[STAT_MAX_HEALTH] = max;
	if ( spawn ) {
		ent->health = cl->ps.stats[STAT_HEALTH] = max + 25;
	} else if ( ent->health > max * 2 ) {
		ent->health = cl->ps.stats[STAT_HEALTH] = max * 2;
	}
}

static int G_PerkCount( gclient_t *cl ) {
	int		i, count;

	for ( count = 0, i = 0 ; i < PERK_NUM ; i++ ) {
		if ( cl->pers.perks & ( 1 << i ) ) {
			count++;
		}
	}
	return count;
}

static void G_PerkSendOwned( gentity_t *ent ) {
	if ( !( ent->r.svFlags & SVF_BOT ) ) {
		trap_SendServerCommand( ent - g_entities, va( "rmperks %i", ent->client->pers.perks ) );
	}
}

static void G_PerkGrant( gentity_t *ent, int perk ) {
	gclient_t	*cl = ent->client;

	if ( perk < 0 || perk >= PERK_NUM || ( cl->pers.perks & ( 1 << perk ) ) ) {
		return;
	}
	cl->pers.perks |= 1 << perk;
	cl->pers.perkOffer[0] = -1;
	if ( perk == PERK_THICKSKIN && ent->health > 0 ) {
		ent->health += G_RL_Active() ? DSP_THICKSKIN_HP : 25;
		cl->ps.stats[STAT_HEALTH] = ent->health;
	}
	if ( ent->health > 0 ) {
		G_PerkApplyMaxHealth( ent, qfalse );
	}
	G_PerkSendOwned( ent );
	if ( !( ent->r.svFlags & SVF_BOT ) ) {
		trap_SendServerCommand( ent - g_entities, va( "rmperkgot %i", perk ) );
	}
	if ( trap_Cvar_VariableIntegerValue( "g_debugAlt" ) ) {
		G_LogPrintf( "Perk: %i %s\n", (int)( ent - g_entities ), bg_perkNames[perk] );
	}
}

/*
===============
G_PerkUseful

Don't offer perks whose mechanic is switched off
===============
*/
static qboolean G_PerkUseful( int perk ) {
	switch ( perk ) {
	case PERK_THIRDWIND:
	case PERK_BLINK:
	case PERK_MIRAGE:
	case PERK_PYROMANIAC:
		return G_RearmOn( RS_DASH );
	case PERK_SLIPSTREAM:
		return G_RearmOn( RS_SLIDE );
	case PERK_GROUNDZERO:
		return G_RearmOn( RS_SLAM );
	case PERK_FEATHERWEIGHT:
		return G_RearmOn( RS_DOUBLEJUMP );
	case PERK_OVERCHARGER:
		return G_RearmOn( RS_ALTFIRE ) && G_RearmOn( RS_CHARGE );
	case PERK_CONDUCTOR:
		return G_RearmOn( RS_ELEMENTS );
	case PERK_HEADHUNTER:
		return G_RearmOn( RS_HEADSHOTS );
	case PERK_MAGNETHOOK:
		return g_offhandHook.integer != 0;
	case PERK_BUTCHER:
		return g_blood.integer != 0;
	default:
		return qtrue;
	}
}

/*
===============
G_PerkOffer

Three perks the player doesn't have yet, from different categories when possible
===============
*/
static void G_PerkOffer( gentity_t *ent ) {
	gclient_t	*cl = ent->client;
	int			pool[PERK_NUM], n, i, j, t, count, pass, usedCats;

	n = 0;
	for ( i = 0 ; i < PERK_NUM ; i++ ) {
		if ( !( cl->pers.perks & ( 1 << i ) ) && G_PerkUseful( i ) ) {
			pool[n++] = i;
		}
	}
	if ( n == 0 ) {
		return;
	}
	for ( i = 0 ; i < n ; i++ ) {
		j = i + rand() % ( n - i );
		t = pool[i]; pool[i] = pool[j]; pool[j] = t;
	}
	// pass 0 takes one per category, pass 1 fills up with whatever is left
	count = 0;
	usedCats = 0;
	for ( pass = 0 ; pass < 2 && count < 3 ; pass++ ) {
		for ( i = 0 ; i < n && count < 3 ; i++ ) {
			if ( pool[i] < 0 ) {
				continue;
			}
			if ( pass == 0 && ( usedCats & ( 1 << bg_perkCategory[pool[i]] ) ) ) {
				continue;
			}
			usedCats |= 1 << bg_perkCategory[pool[i]];
			cl->pers.perkOffer[count++] = pool[i];
			pool[i] = -1;
		}
	}
	for ( i = count ; i < 3 ; i++ ) {
		cl->pers.perkOffer[i] = -1;
	}
	cl->pers.perkOfferTime = level.time;

	if ( ent->r.svFlags & SVF_BOT ) {
		i = BotPersonaPickPerk( ent - g_entities, cl->pers.perkOffer, count );
		if ( i < 0 || i >= count ) {
			i = 0;
		}
		G_PerkGrant( ent, cl->pers.perkOffer[i] );
		return;
	}
	trap_SendServerCommand( ent - g_entities, va( "rmperk %i %i %i %i", cl->pers.perkOffer[0], cl->pers.perkOffer[1],
		cl->pers.perkOffer[2], PERK_OFFER_TIME ) );
}

/*
===============
G_PerkPick

Client command "perkpick <1..3>"
===============
*/
void G_PerkPick( gentity_t *ent, int choice ) {
	gclient_t	*cl = ent->client;

	if ( !G_PerksOn() || cl->pers.perkOffer[0] < 0 || choice < 1 || choice > 3 || cl->pers.perkOffer[choice - 1] < 0 ) {
		return;
	}
	G_PerkGrant( ent, cl->pers.perkOffer[choice - 1] );
}

// dev commands (g_rearmed.c G_RearmConsoleCommand)
void G_PerkGive( gentity_t *ent, int perk ) {
	G_PerkGrant( ent, perk );
}

void G_PerkForceOffer( gentity_t *ent ) {
	G_PerkOffer( ent );
}

/*
===============
G_PerksOnKill

Kills earn perk offers; scavenger refills
===============
*/
void G_PerksOnKill( gentity_t *attacker ) {
	gclient_t	*cl = attacker->client;
	int			w, k;

	if ( !G_PerksOn() || level.warmupTime ) {
		return;
	}
	if ( G_HasPerk( attacker, PERK_SCAVENGER ) ) {
		w = cl->ps.weapon;
		if ( w > WP_GAUNTLET && w < WP_NUM_WEAPONS && cl->ps.ammo[w] >= 0 ) {
			cl->ps.ammo[w] += 10;
			if ( cl->ps.ammo[w] > 200 ) {
				cl->ps.ammo[w] = 200;
			}
		}
	}
	cl->pers.perkKills++;
	k = cl->pers.perkKills;
	if ( !G_RL_Active() && k >= PERK_FIRST_KILLS && ( k - PERK_FIRST_KILLS ) % PERK_EVERY_KILLS == 0
		&& G_PerkCount( cl ) < PERK_MAX && cl->pers.perkOffer[0] < 0 ) {
		G_PerkOffer( attacker );
	}
}

/*
===============
G_PerksOnVictim

Kill perks that need the victim: adrenaline, butcher
===============
*/
static void G_PerksOnVictim( gentity_t *self, gentity_t *attacker, int mod, qboolean gibbed ) {
	gclient_t	*cl = attacker->client;
	gitem_t		*item;
	gentity_t	*drop;
	vec3_t		vel, org;
	int			i;

	if ( !G_PerksOn() ) {
		return;
	}
	if ( G_HasPerk( attacker, PERK_ADRENALINE ) && attacker->health > 0 ) {
		cl->ps.stats[STAT_DASH_TIME] = 0;
		if ( attacker->health < cl->ps.stats[STAT_MAX_HEALTH] ) {
			attacker->health += G_RL_Active() ? DSP_ADRENALINE_HEAL : 10;
			if ( attacker->health > cl->ps.stats[STAT_MAX_HEALTH] ) {
				attacker->health = cl->ps.stats[STAT_MAX_HEALTH];
			}
			cl->ps.stats[STAT_HEALTH] = attacker->health;
		}
	}
	if ( G_HasPerk( attacker, PERK_BUTCHER ) && gibbed && g_blood.integer ) {
		item = NULL;
		for ( i = 1 ; i < bg_numItems ; i++ ) {
			if ( bg_itemlist[i].classname && !strcmp( bg_itemlist[i].classname, "item_health_small" ) ) {
				item = &bg_itemlist[i];
				break;
			}
		}
		if ( item ) {
			VectorCopy( self->r.currentOrigin, org );
			org[2] += 8;
			VectorSet( vel, crandom() * 90, crandom() * 90, 260 );
			drop = LaunchItem( item, org, vel );
			drop->count = 15;			// a bigger bite than the map's small health
			drop->nextthink = level.time + 15000;
		}
	}
}

/*
===============
G_PerksFrame

Unanswered offers pick for you, armor regeneration, health limit, thorns
===============
*/
void G_PerksFrame( gentity_t *ent ) {
	gclient_t	*cl = ent->client;

	if ( level.rmMode == RM_MODE_NORMAL || G_RL_Active() ) {
		G_PerkApplyMaxHealth( ent, qfalse );
	}
	if ( !G_PerksOn() ) {
		return;
	}
	if ( cl->pers.perkOffer[0] >= 0 && level.time - cl->pers.perkOfferTime > PERK_OFFER_TIME ) {
		G_PerkGrant( ent, cl->pers.perkOffer[0] );
		trap_SendServerCommand( ent - g_entities, "rmperk -1 -1 -1" );
	}
	if ( G_HasPerk( ent, PERK_IRONCLAD ) && ent->health > 0 && cl->ps.stats[STAT_ARMOR] < ( G_RL_Active() ? DSP_IRONCLAD_ARMOR : 50 )
		&& level.time >= cl->perkArmorTime ) {
		cl->perkArmorTime = level.time + 400;
		cl->ps.stats[STAT_ARMOR]++;
	}
}

/*
===============
G_PerksThornsFrame

Thorns hit back one frame later (never from inside G_Damage, never thorns on thorns)
===============
*/
void G_PerksThornsFrame( void ) {
	gentity_t	*self, *other;
	int			i, dmg;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( !thornsPending[i] ) {
			continue;
		}
		dmg = thornsPending[i];
		thornsPending[i] = 0;
		self = &g_entities[i];
		other = &g_entities[thornsFrom[i]];
		if ( !self->inuse || !self->client || !other->inuse || !other->client || other->health <= 0
			|| other->client->ps.pm_type != PM_NORMAL ) {
			continue;
		}
		G_Damage( other, self, self, NULL, NULL, dmg, DAMAGE_NO_KNOCKBACK, MOD_THORNS );
	}
}

/*
===============
G_PerkDamageTaken

Called from G_RearmOnDamage with the damage the target is about to take
===============
*/
void G_PerkDamageTaken( gentity_t *targ, gentity_t *attacker, int take ) {
	int		n;

	if ( !targ->client || !attacker || !attacker->client || attacker == targ || take <= 0
		|| targ->client->lasthurt_mod == MOD_THORNS || OnSameTeam( targ, attacker ) || !G_HasPerk( targ, PERK_THORNS ) ) {
		return;
	}
	n = targ - g_entities;
	thornsPending[n] += G_RL_Active() ? ( take * DSP_THORNS_PCT + 50 ) / 100 : ( take * 3 + 5 ) / 10;	// DESCENT: 8%
	thornsFrom[n] = attacker - g_entities;
}

/*
===============
G_PerkDamageScale

Outgoing and incoming damage perks
===============
*/
float G_PerkDamageScale( gentity_t *targ, gentity_t *attacker, int hitFlags ) {
	gclient_t	*cl;
	float		scale = 1.0f, speed;

	if ( !G_PerksOn() ) {
		return 1.0f;
	}
	// incoming: last stand
	if ( targ->client && targ->health > 0 && targ->health < 30 && attacker != targ && G_HasPerk( targ, PERK_LASTSTAND ) ) {
		scale *= G_RL_Active() ? DSP_LASTSTAND_TAKEN : 0.75f;
	}
	if ( !attacker || !attacker->client || attacker == targ ) {
		return scale;
	}
	cl = attacker->client;
	// the handicap scaling in G_Damage must not turn a changed health limit into damage
	if ( cl->ps.stats[STAT_MAX_HEALTH] > 0 && cl->ps.stats[STAT_MAX_HEALTH] != cl->pers.maxHealth ) {
		scale *= (float)cl->pers.maxHealth / cl->ps.stats[STAT_MAX_HEALTH];
	}
	if ( G_HasPerk( attacker, PERK_HEAVYCALIBER ) ) {
		scale *= G_RL_Active() ? DSP_HEAVYCALIBER : 1.15f;		// DESCENT: single digits (bg_descent.h DSP_*)
	}
	if ( ( hitFlags & HIT_HEADSHOT ) && G_HasPerk( attacker, PERK_HEADHUNTER ) ) {
		scale *= G_RL_Active() ? DSP_HEADHUNTER : 4.0f / 3.0f;		// 1.5x becomes 2x (DESCENT: +9%)
	}
	if ( targ->client && targ->client->ps.groundEntityNum == ENTITYNUM_NONE && G_HasPerk( attacker, PERK_SKYHUNTER ) ) {
		scale *= G_RL_Active() ? DSP_SKYHUNTER : 1.3f;
	}
	if ( G_HasPerk( attacker, PERK_GLASSCANNON ) ) {
		scale *= G_RL_Active() ? DSP_GLASS_DMG : 1.35f;
	}
	if ( attacker->health > 0 && attacker->health < 30 && G_HasPerk( attacker, PERK_LASTSTAND ) ) {
		scale *= G_RL_Active() ? DSP_LASTSTAND_DEALT : 1.5f;
	}
	if ( G_HasPerk( attacker, PERK_KINETIC ) ) {
		speed = sqrt( cl->ps.velocity[0] * cl->ps.velocity[0] + cl->ps.velocity[1] * cl->ps.velocity[1] );
		if ( speed > 320 ) {
			speed = ( speed - 320 ) * 0.001f;		// +1% per 10 ups
			scale *= 1.0f + ( speed > ( G_RL_Active() ? DSP_KINETIC_MAX : 0.5f ) ? ( G_RL_Active() ? DSP_KINETIC_MAX : 0.5f ) : speed );
		}
	}
	return scale;
}

/*
===============
G_PerkOnDash

Every dash (and blink): pyromaniac fire, mirage invisibility, dash kill timing
===============
*/
void G_PerkOnDash( gentity_t *ent ) {
	if ( !ent->client ) {
		return;
	}
	lastDashTime[ent - g_entities] = level.time;
	if ( G_HasPerk( ent, PERK_PYROMANIAC ) ) {
		G_SpawnFirePatch( ent->r.currentOrigin, ent, 2500, 60 );
	}
	if ( G_HasPerk( ent, PERK_MIRAGE ) && ent->client->ps.powerups[PW_INVIS] < level.time + 1500 ) {
		ent->client->ps.powerups[PW_INVIS] = level.time + 1500;
	}
}

const char *G_PerkName( int perk ) {
	return ( perk >= 0 && perk < PERK_NUM ) ? bg_perkNames[perk] : "";
}

/*
===============================================================================

MISSILE IMPACTS: ricochet, magnet hook (meteors live in g_rearmed.c)

===============================================================================
*/

static void G_MagnetYank( gentity_t *hook, gentity_t *owner, gentity_t *victim, trace_t *trace ) {
	gentity_t	*tent;
	vec3_t		dir, vel;
	float		dist, speed;

	VectorSubtract( owner->r.currentOrigin, victim->r.currentOrigin, dir );
	dist = VectorNormalize( dir );
	speed = dist * 2.2f;
	if ( speed < 520 ) {
		speed = 520;
	} else if ( speed > 1250 ) {
		speed = 1250;
	}
	VectorScale( dir, speed, vel );
	vel[2] += 240;
	VectorCopy( vel, victim->client->ps.velocity );
	victim->client->ps.groundEntityNum = ENTITYNUM_NONE;
	victim->client->ps.pm_time = 300;
	victim->client->ps.pm_flags |= PMF_TIME_KNOCKBACK;

	tent = G_TempEntity( trace->endpos, EV_MISSILE_HIT );
	tent->s.otherEntityNum = victim->s.number;
	tent->s.eventParm = DirToByte( trace->plane.normal );
	tent->s.weapon = WP_GRAPPLING_HOOK;

	G_StyleAdd( owner, 30, "GET OVER HERE" );
	if ( trap_Cvar_VariableIntegerValue( "g_debugAlt" ) ) {
		G_LogPrintf( "MagnetHook: %i yanks %i\n", owner->s.number, victim->s.number );
	}
	Weapon_HookFree( hook );
	G_Damage( victim, owner, owner, dir, trace->endpos, 10, DAMAGE_NO_KNOCKBACK, MOD_GRAPPLE );
}

/*
===============
G_RearmMissileImpact

Called first thing in G_MissileImpact. qtrue: handled, the normal impact is skipped.
===============
*/
qboolean G_RearmMissileImpact( gentity_t *ent, trace_t *trace ) {
	gentity_t	*other = &g_entities[trace->entityNum];
	gentity_t	*owner;
	int			half;

	if ( ent->s.generic1 & RM_MISSILE_METEOR ) {
		G_MeteorImpact( ent, trace );
		return qtrue;
	}
	if ( ent->r.ownerNum < 0 || ent->r.ownerNum >= MAX_CLIENTS ) {
		return qfalse;
	}
	owner = &g_entities[ent->r.ownerNum];
	if ( !owner->inuse || !owner->client ) {
		return qfalse;
	}

	// magnet hook: the grapple drags its victim to you instead
	if ( ent->classname && !strcmp( ent->classname, "hook" ) && other->client && other->takedamage && other->health > 0
		&& other != owner && !OnSameTeam( other, owner ) && G_HasPerk( owner, PERK_MAGNETHOOK ) ) {
		G_MagnetYank( ent, owner, other, trace );
		return qtrue;
	}

	// ricochet: rockets, grenades and plasma bounce off the first wall
	if ( !other->takedamage && !( ent->flags & FL_RM_RICOCHETED ) && G_HasPerk( owner, PERK_RICOCHET )
		&& ( ent->s.weapon == WP_ROCKET_LAUNCHER || ent->s.weapon == WP_PLASMAGUN || ent->s.weapon == WP_GRENADE_LAUNCHER )
		&& ent->s.pos.trType != TR_STATIONARY ) {
		ent->flags |= FL_RM_RICOCHETED;
		half = ent->s.eFlags & EF_BOUNCE_HALF;
		ent->s.eFlags &= ~EF_BOUNCE_HALF;		// the ricochet keeps all the speed
		G_BounceMissile( ent, trace );
		ent->s.eFlags |= half;
		if ( ent->s.weapon == WP_ROCKET_LAUNCHER ) {
			vectoangles( ent->s.pos.trDelta, ent->s.angles );
		}
		G_AddEvent( ent, EV_GRENADE_BOUNCE, 0 );
		if ( trap_Cvar_VariableIntegerValue( "g_debugAlt" ) ) {
			G_LogPrintf( "Ricochet: %i weapon %i\n", ent->r.ownerNum, ent->s.weapon );
		}
		return qtrue;
	}
	return qfalse;
}

/*
===============================================================================

CHAIN REACTION: corpses that blow up a second after dying

===============================================================================
*/

static void G_CorpseBombThink( gentity_t *ent ) {
	gentity_t	*victim = ent->enemy, *killer = ent->parent, *tent;
	qboolean	corpse;

	corpse = victim && victim->inuse && victim->client && victim->health <= 0 && victim->s.eType == ET_PLAYER
		&& victim->client->ps.persistant[PERS_SPAWN_COUNT] == ent->count;
	if ( corpse ) {
		G_SetOrigin( ent, victim->r.currentOrigin );
		trap_LinkEntity( ent );
	}
	if ( level.time < ent->s.time2 ) {
		ent->nextthink = level.time + 50;
		return;
	}
	if ( !killer || !killer->inuse || !killer->client ) {
		killer = &g_entities[ENTITYNUM_WORLD];
	}
	tent = G_TempEntity( ent->r.currentOrigin, EV_CORPSE_EXPLODE );
	tent->s.eventParm = corpse ? 1 : 0;
	G_RadiusDamage( ent->r.currentOrigin, killer, CORPSE_BOMB_DAMAGE, CORPSE_BOMB_RADIUS,
		ent->spawnflags ? killer : NULL, MOD_CORPSE );
	if ( corpse && victim->s.eType == ET_PLAYER && g_blood.integer ) {
		GibEntity( victim, killer->s.number < MAX_CLIENTS ? killer->s.number : 0 );
	}
	G_EarthTremor( ent->r.currentOrigin, killer->client ? killer : NULL, 180, 280 );
	corpseBombs--;
	G_FreeEntity( ent );
}

/*
===============
G_CorpseBomb

The victim's body explodes in a second, killer gets the credit.
The perk protects its owner from the blast; the mutator hits everyone.
===============
*/
void G_CorpseBomb( gentity_t *victim, gentity_t *killer ) {
	gentity_t	*ent;

	if ( corpseBombs >= MAX_CORPSE_BOMBS || !victim->client ) {
		return;
	}
	ent = G_Spawn();
	ent->classname = "corpse_bomb";
	ent->s.eType = ET_GENERAL;
	ent->s.generic1 = RM_ENT_STRIKE;
	ent->s.modelindex2 = RM_STRIKE_CORPSE;
	ent->s.time = level.time;
	ent->s.time2 = level.time + CORPSE_BOMB_DELAY;
	ent->s.otherEntityNum2 = CORPSE_BOMB_RADIUS;
	ent->random = CORPSE_BOMB_RADIUS;
	ent->enemy = victim;
	ent->parent = killer;
	ent->count = victim->client->ps.persistant[PERS_SPAWN_COUNT];
	ent->spawnflags = ( killer && level.mutator != MUT_CHAIN_REACTION ) ? 1 : 0;	// perk: spare the killer
	ent->think = G_CorpseBombThink;
	ent->nextthink = level.time + 50;
	G_SetOrigin( ent, victim->r.currentOrigin );
	trap_LinkEntity( ent );
	corpseBombs++;
}
