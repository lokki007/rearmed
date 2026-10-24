/*
===========================================================================
REARMED -- DESCENT round 3 CARDS (ds-cards), game side.

- LEGENDARY cards (dsLegend_t, RLC_LEGEND_FIRST + n): rare build-defining cards, the
  player's only (never in bot kits), at most one per offer. The offer chance grows with
  the floor, elite / boss offers and luck; a reroll keeps the floor's legendary (decided by
  the floor, not the deal), a secret stash (ds-secrets) owes one (run key lg_owed).
  Every effect works in DESCENT only and only for the player; numbers single digit or
  mechanical (the player's rule).
- The floor-end TALLY: the won floor's numbers go to the client (dsctally) and the
  server waits in RLP_END until the client says dsctallydone (or a timeout).
- Personal bests per floor in rearmed_tally.txt (the game's own file).
- The legendary HUD state goes to the client as "dsclg ..." when it changes.
Client side: cgame/cg_ds_cards.c. Contract: bg_descent.h (LG_*, LGX_*), g_descent.h CARDS.
===========================================================================
*/
#include "g_local.h"

#define LG_TALLY_FILE		"rearmed_tally.txt"
#define LG_TALLY_WAIT		11000		// msec the server waits for the client's tally at most
#define LG_ECHO_EVERY		5
#define LG_ECHO_DELAY		150
#define LG_CHAIN_DMG		40
#define LG_CHAIN_RANGE		700
#define LG_CHAIN_LINKS		4
#define LG_TIME_ADD			1500
#define LG_PHOENIX_RADIUS	320
#define LG_PHOENIX_DMG		60
#define LG_PHOENIX_GRACE	1200
#define LG_ORB_BASE			3
#define LG_ORB_MAX			6
#define LG_ORB_EXTRA		6000		// a picked-up gem orbits this long
#define LG_ORB_RADIUS		64.0f
#define LG_ORB_SPEED		0.0032f		// radians per msec (cgame uses the same)
#define LG_ORB_DMG			8
#define LG_ORB_REHIT		400
#define LG_AGENT_TIME		8000
#define LG_AGENT_COOLDOWN	30000
#define LG_GRAVE_RICH		5
#define LG_BLACKMASS_EVERY	9
#define LG_DEADEYE_EVERY	5
#define LG_MAX_GRAVES		96

typedef struct {
	// offer
	qboolean	offerHadLegend;
	// effects
	int			echoCount, echoTime, echoWeapon;
	vec3_t		echoAngles;
	qboolean	echoing;
	int			deadeyeCount;
	int			blackmassCount;
	int			mirrorLife;		// PERS_SPAWN_COUNT the mirror was used in (-1 never)
	qboolean	phoenixUsed;
	int			phoenixGrace;
	int			agent, agentUntil, agentReady;
	int			orbExtra[LG_ORB_MAX];
	int			orbHitNext[MAX_CLIENTS];
	int			orbsSent;
	int			graveNum[LG_MAX_GRAVES], graveTime[LG_MAX_GRAVES], graveHead, graveCount;
	int			internal;		// our own damage in progress (no DEADEYE / agent on it)
	int			chainDepth;
	// tally
	qboolean	tallyWait, tallyDone;
	int			tallySent;
	int			tally[10];		// last tally sent (dstally resends it): 8 values, record mask, floor
	int			graveTotal;
	qboolean	tallyValid;
	char		hud[96];
	int			hudNext;
} lgState_t;

static lgState_t	lg;
static int			lgForce = -2;		// dev dsoffer legendary: -2 none, -1 any, n = that one (survives the map change)

static const char	*lgTallyKeys[8] = { "kills", "hs", "gibs", "chain", "gems", "dmg", "style", "skulls" };

/*
===============================================================================

HELPERS

===============================================================================
*/

static gentity_t *LG_Human( void ) {
	int		h = G_RL_HumanClient();

	if ( h < 0 || h >= MAX_CLIENTS || !g_entities[h].inuse || !g_entities[h].client ) {
		return NULL;
	}
	return &g_entities[h];
}

// the player holds this legendary
static qboolean LG_Has( gentity_t *ent, int lg_ ) {
	if ( !G_RL_Active() || !ent || !ent->client || !G_RL_IsPlayer( ent ) ) {
		return qfalse;
	}
	return G_RL_HasCard( ent, RLC_LEGEND_FIRST + lg_ );
}

static qboolean LG_Enemy( gentity_t *e, gentity_t *player ) {
	return e && e->inuse && e->client && e != player && e->health > 0 && e->client->ps.pm_type != PM_DEAD
		&& e->client->sess.sessionTeam != TEAM_SPECTATOR;
}

static gentity_t *LG_Fx( int kind, const vec3_t from, const vec3_t to, int other, int other2, int extra ) {
	gentity_t	*t = G_TempEntity( (float *)from, EV_DS_CARDS );

	t->s.eventParm = kind;
	if ( to ) {
		VectorCopy( to, t->s.origin2 );
	} else {
		VectorCopy( from, t->s.origin2 );
	}
	t->s.otherEntityNum = other;
	t->s.otherEntityNum2 = other2;
	t->s.generic1 = extra;
	t->r.svFlags |= SVF_BROADCAST;
	return t;
}

static void LG_Damage( gentity_t *targ, gentity_t *attacker, vec3_t point, int dmg, int dflags, int mod ) {
	lg.internal++;
	G_Damage( targ, attacker, attacker, NULL, point, dmg, dflags, mod );
	lg.internal--;
}

static int LG_ParseCsv( const char *s, int *out, int max ) {
	int		n = 0;

	while ( *s && n < max ) {
		while ( *s == ',' || *s == ' ' ) {
			s++;
		}
		if ( !*s ) {
			break;
		}
		out[n++] = atoi( s );
		while ( *s && *s != ',' ) {
			s++;
		}
	}
	return n;
}

/*
===============================================================================

INIT / OFFER

===============================================================================
*/

void G_DS_CardsInit( void ) {
	int		i;

	memset( &lg, 0, sizeof( lg ) );
	lg.mirrorLife = -1;
	lg.agent = -1;
	for ( i = 0 ; i < MAX_CLIENTS ; i++ ) {
		lg.orbHitNext[i] = 0;
	}
}

void G_DS_CardsGrantLegendary( void ) {
	if ( !G_RL_Active() ) {
		return;
	}
	G_RL_RunSet( "lg_owed", "1" );
	G_LogPrintf( "Legend: a legendary is owed in the next offer (stash)\n" );
}

/*
=================
G_DS_CardsOffer

RL_MakePlayerOffer, after the deal: maybe swap the last card for one legendary. Decided
by the run seed and the floor (not the reroll): a reroll keeps it, rerolling for one
doesn't work. Chance: 3% + floor / 2, elite +8, boss +25, x luck, max 50; owed = always.
=================
*/
void G_DS_CardsOffer( int *offer, int numOffer, int nodeType, int floor, int luckPct ) {
	gentity_t	*h = LG_Human();
	int			avail[LG_NUM], n = 0, i, chance, roll, pick, owed;
	unsigned	rng;

	lg.offerHadLegend = qfalse;
	if ( !G_RL_Active() || !h || numOffer <= 0 ) {
		return;
	}
	for ( i = 0 ; i < LG_NUM ; i++ ) {
		if ( !G_RL_HasCard( h, RLC_LEGEND_FIRST + i ) ) {
			avail[n++] = i;
		}
	}
	if ( !n ) {
		return;
	}
	owed = atoi( G_RL_RunGet( "lg_owed" ) );
	chance = 3 + floor / 2;
	if ( nodeType == 1 ) {
		chance += 8;		// elite
	} else if ( nodeType == 2 ) {
		chance += 25;		// boss
	}
	chance = chance * ( 100 + luckPct ) / 100;
	if ( chance > 50 ) {
		chance = 50;
	}
	rng = (unsigned)atoi( G_RL_RunGet( "seed" ) ) * 2654435761u + (unsigned)floor * 40503u + 0x51ed27u;
	rng = rng * 1103515245u + 12345u;
	roll = (int)( ( rng >> 16 ) % 100 );
	rng = rng * 1103515245u + 12345u;
	pick = avail[( rng >> 16 ) % n];
	if ( lgForce >= 0 && lgForce < LG_NUM ) {
		for ( i = 0 ; i < n && avail[i] != lgForce ; i++ ) {
		}
		if ( i < n ) {
			pick = lgForce;
		}
	}
	if ( !owed && lgForce == -2 && roll >= chance ) {
		G_LogPrintf( "Legend: offer floor %i roll %i chance %i%% -> none\n", floor, roll, chance );
		return;
	}
	offer[numOffer - 1] = RLC_LEGEND_FIRST + pick;
	lg.offerHadLegend = qtrue;
	G_LogPrintf( "Legend: offer floor %i roll %i chance %i%% owed %i forced %i -> %s (card %i)\n", floor, roll, chance,
		owed, lgForce != -2, bg_rlCardNames[RLC_LEGEND_FIRST + pick], RLC_LEGEND_FIRST + pick );
}

void G_DS_CardsPicked( int card ) {
	if ( lg.offerHadLegend ) {
		lgForce = -2;
		G_RL_RunSet( "lg_owed", "" );
		lg.offerHadLegend = qfalse;
	}
	if ( RLC_IS_LEGEND( card ) ) {
		G_RL_RunSet( "lg_taken", va( "%i", atoi( G_RL_RunGet( "lg_taken" ) ) | ( 1 << ( card - RLC_LEGEND_FIRST ) ) ) );
		G_LogPrintf( "Legend: TAKEN %s\n", bg_rlCardNames[card] );
	}
}

// G_DS_PowerScore: each legendary held counts like a few perks (enemies scale with the player)
float G_DS_CardsPower( gentity_t *ent ) {
	int		i, n = 0;

	if ( !G_RL_Active() || !ent || !ent->client ) {
		return 0;
	}
	for ( i = 0 ; i < LG_NUM ; i++ ) {
		if ( G_RL_HasCard( ent, RLC_LEGEND_FIRST + i ) ) {
			n++;
		}
	}
	return 0.04f * n;
}

/*
===============================================================================

EFFECTS

===============================================================================
*/

/*
=================
G_DS_CardsDamage

G_Damage, after the damage scales (before the damage < 0 check): MIRROR throws the
first hit of a life back, DOUBLE AGENT turns the enemy hit (and keeps it from hurting
you / you from hurting it), DEADEYE makes every 5th hit a headshot, PHOENIX's grace.
-1 = no hit.
=================
*/
int G_DS_CardsDamage( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, int damage, int dflags, int mod, int *hitFlags ) {
	gentity_t	*h;
	int			life;

	if ( !G_RL_Active() || damage <= 0 || !targ || !targ->client || !attacker ) {
		return damage;
	}
	h = LG_Human();
	if ( !h ) {
		return damage;
	}
	// the agent and the player can't hurt each other
	if ( lg.agent >= 0 && ( ( attacker == h && targ->s.number == lg.agent ) || ( targ == h && attacker->s.number == lg.agent ) ) ) {
		return -1;
	}
	if ( targ == h ) {
		if ( lg.phoenixGrace > level.time ) {
			return -1;
		}
		// MIRROR: the first hit of a life goes back to whoever threw it
		if ( attacker != h && attacker->client && LG_Has( h, LG_MIRROR ) ) {
			life = h->client->ps.persistant[PERS_SPAWN_COUNT];
			if ( lg.mirrorLife != life ) {
				lg.mirrorLife = life;
				LG_Fx( LGX_MIRROR, h->r.currentOrigin, attacker->r.currentOrigin, h->s.number, attacker->s.number, damage );
				G_LogPrintf( "Legend: MIRROR threw %i damage back at %i\n", damage, attacker->s.number );
				if ( LG_Enemy( attacker, h ) ) {
					LG_Damage( attacker, h, attacker->r.currentOrigin, damage, DAMAGE_NO_KNOCKBACK, mod );
				}
				return -1;
			}
		}
		return damage;
	}
	if ( attacker != h || targ == h || lg.internal ) {
		return damage;
	}
	// DOUBLE AGENT: the next enemy you hit (not the act boss) fights for you
	if ( LG_Has( h, LG_DOUBLEAGENT ) && lg.agent < 0 && level.time >= lg.agentReady && targ->s.number != G_DS_BossClient()
		&& ( targ->r.svFlags & SVF_BOT ) && targ->health > 0 ) {
		lg.agent = targ->s.number;
		lg.agentUntil = level.time + LG_AGENT_TIME;
		lg.agentReady = level.time + LG_AGENT_COOLDOWN;
		LG_Fx( LGX_AGENT, targ->r.currentOrigin, h->r.currentOrigin, h->s.number, targ->s.number, 1 );
		G_LogPrintf( "Legend: DOUBLE AGENT turned %i (%s) for %i ms\n", targ->s.number, targ->client->pers.netname, LG_AGENT_TIME );
		return -1;
	}
	// DEADEYE: every 5th hit you land is a headshot
	if ( LG_Has( h, LG_DEADEYE ) && !( dflags & DAMAGE_RADIUS ) && hitFlags && !( *hitFlags & HIT_HEADSHOT ) ) {
		if ( ++lg.deadeyeCount >= LG_DEADEYE_EVERY ) {
			lg.deadeyeCount = 0;
			*hitFlags |= HIT_HEADSHOT;
			damage = damage * 3 / 2;		// g_combat.c HEADSHOT_SCALE
			LG_Fx( LGX_DEADEYE, targ->r.currentOrigin, NULL, h->s.number, targ->s.number, 0 );
			G_LogPrintf( "Legend: DEADEYE headshot on %i -> %i damage\n", targ->s.number, damage );
		}
	}
	return damage;
}

/*
=================
G_DS_CardsHit

G_Damage, the final take (after CHEAT DEATH / PENTAGRAM): PHOENIX turns the player's
killing blow into a revive with a fire nova; VAMPIRE LORD turns overkill into armor.
=================
*/
int G_DS_CardsHit( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, int take, int mod ) {
	gentity_t	*h, *e;
	int			max, armor, over, i;
	float		d;

	if ( !G_RL_Active() || take <= 0 || !targ || !targ->client || !( h = LG_Human() ) ) {
		return take;
	}
	if ( targ == h && take >= targ->health && targ->health > 0 && LG_Has( h, LG_PHOENIX ) && !lg.phoenixUsed ) {
		lg.phoenixUsed = qtrue;
		max = h->client->ps.stats[STAT_MAX_HEALTH] > 0 ? h->client->ps.stats[STAT_MAX_HEALTH] : 100;
		h->health = max / 2 > h->health ? max / 2 : h->health;
		h->client->ps.stats[STAT_HEALTH] = h->health;
		lg.phoenixGrace = level.time + LG_PHOENIX_GRACE;
		LG_Fx( LGX_PHOENIX, h->r.currentOrigin, NULL, h->s.number, ENTITYNUM_NONE, LG_PHOENIX_RADIUS );
		lg.internal++;
		G_RadiusDamage( h->r.currentOrigin, h, LG_PHOENIX_DMG, LG_PHOENIX_RADIUS, h, MOD_ROCKET_SPLASH );
		lg.internal--;
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			e = &g_entities[i];
			if ( LG_Enemy( e, h ) ) {
				d = Distance( e->r.currentOrigin, h->r.currentOrigin );
				if ( d < LG_PHOENIX_RADIUS ) {
					G_ElementsIgnite( e, h, 2000 );
				}
			}
		}
		G_LogPrintf( "Legend: PHOENIX revived the player at %i health (blow %i)\n", h->health, take );
		return 0;
	}
	if ( attacker == h && targ != h && take >= targ->health && targ->health > 0 && LG_Has( h, LG_VAMPIRE ) ) {
		over = take - targ->health;
		max = h->client->ps.stats[STAT_MAX_HEALTH] > 0 ? h->client->ps.stats[STAT_MAX_HEALTH] : 100;
		armor = h->client->ps.stats[STAT_ARMOR];
		if ( over > 0 && armor < max ) {
			if ( armor + over > max ) {
				over = max - armor;
			}
			h->client->ps.stats[STAT_ARMOR] = armor + over;
			LG_Fx( LGX_VAMPIRE, targ->r.currentOrigin, h->r.currentOrigin, h->s.number, targ->s.number, over );
			G_LogPrintf( "Legend: VAMPIRE LORD overkill %i -> armor %i\n", over, h->client->ps.stats[STAT_ARMOR] );
		}
	}
	return take;
}

// CHAIN LIGHTNING SOUL: the nearest enemy in sight of the body takes an arc
static void LG_ChainArc( gentity_t *from, gentity_t *h ) {
	gentity_t	*e, *best = NULL;
	float		d, bestD = LG_CHAIN_RANGE;
	trace_t		tr;
	vec3_t		a, b;
	int			i;

	VectorCopy( from->r.currentOrigin, a );
	a[2] += 16;
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		e = &g_entities[i];
		if ( e == from || !LG_Enemy( e, h ) || i == lg.agent ) {
			continue;
		}
		d = Distance( e->r.currentOrigin, a );
		if ( d >= bestD ) {
			continue;
		}
		VectorCopy( e->r.currentOrigin, b );
		b[2] += 16;
		trap_Trace( &tr, a, NULL, NULL, b, from->s.number, MASK_SOLID );
		if ( tr.fraction < 1.0f ) {
			continue;
		}
		best = e;
		bestD = d;
	}
	if ( !best ) {
		return;
	}
	VectorCopy( best->r.currentOrigin, b );
	b[2] += 16;
	LG_Fx( LGX_CHAIN, a, b, h->s.number, best->s.number, lg.chainDepth );
	G_LogPrintf( "Legend: CHAIN LIGHTNING SOUL arc %i -> %i (link %i)\n", from->s.number, best->s.number, lg.chainDepth );
	lg.chainDepth++;
	LG_Damage( best, h, b, LG_CHAIN_DMG, DAMAGE_NO_KNOCKBACK, MOD_LIGHTNING );
	lg.chainDepth--;
}

static int LG_AmmoRefund( int w ) {
	switch ( w ) {
	case WP_MACHINEGUN:
	case WP_LIGHTNING:
	case WP_PLASMAGUN:
	case WP_CHAINGUN:
		return 9;
	case WP_NAILGUN:
		return 3;
	case WP_SHOTGUN:
	case WP_GRENADE_LAUNCHER:
	case WP_ROCKET_LAUNCHER:
	case WP_PROX_LAUNCHER:
		return 2;
	case WP_RAILGUN:
	case WP_BFG:
		return 1;
	default:
		return 0;
	}
}

/*
=================
G_DS_CardsKill

player_die: the agent's kills don't score, CHAIN LIGHTNING SOUL, TIME THIEF, BLOOD
ENGINE, BLACK MASS
=================
*/
void G_DS_CardsKill( gentity_t *self, gentity_t *attacker, int mod, qboolean gibbed ) {
	gentity_t	*h;
	int			added, pw, w, n;

	if ( !G_RL_Active() || !self || !self->client || !( h = LG_Human() ) ) {
		return;
	}
	if ( self->s.number == lg.agent ) {
		lg.agentUntil = level.time;		// the agent died: it's over (the frame ends it)
	}
	// the agent fights for you, not for its own score (no frag-race help for anyone)
	if ( attacker && attacker->client && attacker->s.number == lg.agent && attacker != self ) {
		attacker->client->ps.persistant[PERS_SCORE]--;
		CalculateRanks();
		G_LogPrintf( "Legend: DOUBLE AGENT %i killed %i (no frag)\n", attacker->s.number, self->s.number );
		return;
	}
	if ( attacker != h || self == h ) {
		return;
	}
	if ( LG_Has( h, LG_CHAINSOUL ) && lg.chainDepth < LG_CHAIN_LINKS ) {
		LG_ChainArc( self, h );
	}
	if ( LG_Has( h, LG_TIMETHIEF ) ) {
		added = G_DS_FrenzySpikeExtend( h, LG_TIME_ADD );
		for ( pw = PW_QUAD ; pw <= PW_FLIGHT ; pw++ ) {
			if ( h->client->ps.powerups[pw] > level.time && h->client->ps.powerups[pw] < level.time + 600000 ) {
				h->client->ps.powerups[pw] += LG_TIME_ADD;
				added += LG_TIME_ADD;
			}
		}
		if ( added > 0 ) {
			LG_Fx( LGX_TIME, h->r.currentOrigin, NULL, h->s.number, ENTITYNUM_NONE, added > LG_TIME_ADD ? LG_TIME_ADD : added );
			G_LogPrintf( "Legend: TIME THIEF +%i ms\n", added );
		}
	}
	if ( gibbed && LG_Has( h, LG_BLOODENGINE ) ) {
		h->client->ps.stats[STAT_DASH_TIME] -= DASH_COOLDOWN;
		if ( h->client->ps.stats[STAT_DASH_TIME] < 0 ) {
			h->client->ps.stats[STAT_DASH_TIME] = 0;
		}
		w = h->client->ps.weapon;
		n = LG_AmmoRefund( w );
		if ( n && w > WP_NONE && w < WP_NUM_WEAPONS && h->client->ps.ammo[w] >= 0 ) {
			h->client->ps.ammo[w] += n;
			if ( h->client->ps.ammo[w] > 200 ) {
				h->client->ps.ammo[w] = 200;
			}
		}
		LG_Fx( LGX_BLOODENGINE, self->r.currentOrigin, h->r.currentOrigin, h->s.number, self->s.number, n );
		G_LogPrintf( "Legend: BLOOD ENGINE dash refunded (dash time %i), +%i ammo\n", h->client->ps.stats[STAT_DASH_TIME], n );
	}
	if ( LG_Has( h, LG_BLACKMASS ) && ++lg.blackmassCount >= LG_BLACKMASS_EVERY ) {
		lg.blackmassCount = 0;
		LG_Fx( LGX_BLACKMASS, h->r.currentOrigin, NULL, h->s.number, ENTITYNUM_NONE, 0 );
		G_DS_Vacuum( h );
		G_LogPrintf( "Legend: BLACK MASS pulled every gem\n" );
	}
}

// g_active.c after FireWeapon: ECHO counts the shots
void G_DS_CardsFired( gentity_t *ent ) {
	if ( lg.echoing || !LG_Has( ent, LG_ECHO ) || ent->client->ps.weapon == WP_GRAPPLING_HOOK ) {
		return;
	}
	if ( ++lg.echoCount >= LG_ECHO_EVERY && !lg.echoTime ) {
		lg.echoCount = 0;
		lg.echoTime = level.time + LG_ECHO_DELAY;
		lg.echoWeapon = ent->client->ps.weapon;
		VectorCopy( ent->client->ps.viewangles, lg.echoAngles );
	}
}

static void LG_EchoFrame( gentity_t *h ) {
	vec3_t	save, fwd, org;

	if ( !lg.echoTime || level.time < lg.echoTime ) {
		return;
	}
	lg.echoTime = 0;
	if ( h->health <= 0 || h->client->ps.weapon != lg.echoWeapon || level.intermissiontime ) {
		return;
	}
	VectorCopy( h->client->ps.viewangles, save );
	VectorCopy( lg.echoAngles, h->client->ps.viewangles );
	lg.echoing = qtrue;
	FireWeapon( h );
	lg.echoing = qfalse;
	VectorCopy( save, h->client->ps.viewangles );
	AngleVectors( lg.echoAngles, fwd, NULL, NULL );
	VectorCopy( h->client->ps.origin, org );
	org[2] += h->client->ps.viewheight;
	VectorMA( org, 24, fwd, org );
	LG_Fx( LGX_ECHO, org, NULL, h->s.number, ENTITYNUM_NONE, lg.echoWeapon );
	G_LogPrintf( "Legend: ECHO fired weapon %i again\n", lg.echoWeapon );
}

// g_ds_loot.c: a gem collected
void G_DS_CardsGem( gentity_t *player ) {
	int		i, oldest = 0;

	if ( !LG_Has( player, LG_MAGNETAR ) ) {
		return;
	}
	for ( i = 0 ; i < LG_ORB_MAX - LG_ORB_BASE ; i++ ) {
		if ( lg.orbExtra[i] <= level.time ) {
			lg.orbExtra[i] = level.time + LG_ORB_EXTRA;
			return;
		}
		if ( lg.orbExtra[i] < lg.orbExtra[oldest] ) {
			oldest = i;
		}
	}
	lg.orbExtra[oldest] = level.time + LG_ORB_EXTRA;
}

static int LG_Orbs( void ) {
	int		i, n = LG_ORB_BASE;

	for ( i = 0 ; i < LG_ORB_MAX - LG_ORB_BASE ; i++ ) {
		if ( lg.orbExtra[i] > level.time ) {
			n++;
		}
	}
	return n;
}

// MAGNETAR: orbs circle the player (cgame draws the same circle from the count) and burn
static void LG_OrbFrame( gentity_t *h ) {
	int			n, i, k;
	float		a;
	vec3_t		orb, c;
	gentity_t	*e;

	if ( h->health <= 0 ) {
		return;
	}
	n = LG_Orbs();
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		e = &g_entities[i];
		if ( !LG_Enemy( e, h ) || i == lg.agent || lg.orbHitNext[i] > level.time ) {
			continue;
		}
		if ( DistanceSquared( e->r.currentOrigin, h->r.currentOrigin ) > Square( LG_ORB_RADIUS + 60 ) ) {
			continue;
		}
		VectorCopy( e->r.currentOrigin, c );
		c[2] += 8;
		for ( k = 0 ; k < n ; k++ ) {
			a = level.time * LG_ORB_SPEED + k * 2.0f * M_PI / n;
			orb[0] = h->r.currentOrigin[0] + cos( a ) * LG_ORB_RADIUS;
			orb[1] = h->r.currentOrigin[1] + sin( a ) * LG_ORB_RADIUS;
			orb[2] = h->r.currentOrigin[2] + 10;
			if ( DistanceSquared( orb, c ) < Square( 40 ) ) {
				lg.orbHitNext[i] = level.time + LG_ORB_REHIT;
				LG_Fx( LGX_ORBHIT, orb, NULL, h->s.number, i, 0 );
				G_LogPrintf( "Legend: MAGNETAR orb burned %i (%i orbs)\n", i, n );
				LG_Damage( e, h, orb, LG_ORB_DMG, DAMAGE_NO_KNOCKBACK, MOD_PLASMA_SPLASH );
				break;
			}
		}
	}
}

// GRAVEDIGGER: a corpse you walk over gives up a gem (once each); every 5th a big one with a beam
static void LG_GraveFrame( gentity_t *h ) {
	gentity_t	*e;
	int			i, j;
	vec3_t		d, org;

	if ( h->health <= 0 ) {
		return;
	}
	for ( i = MAX_CLIENTS ; i < level.num_entities ; i++ ) {
		e = &g_entities[i];
		if ( !e->inuse || e->s.eType != ET_PLAYER || !e->r.linked || !e->classname || Q_stricmp( e->classname, "bodyque" ) ) {
			continue;
		}
		VectorSubtract( e->r.currentOrigin, h->r.currentOrigin, d );
		if ( d[0] * d[0] + d[1] * d[1] > Square( 52 ) || d[2] > 64 || d[2] < -64 ) {
			continue;
		}
		for ( j = 0 ; j < lg.graveCount ; j++ ) {
			if ( lg.graveNum[j] == i && lg.graveTime[j] == e->s.time ) {
				break;
			}
		}
		if ( j < lg.graveCount ) {
			continue;
		}
		lg.graveNum[lg.graveHead] = i;
		lg.graveTime[lg.graveHead] = e->s.time;
		lg.graveHead = ( lg.graveHead + 1 ) % LG_MAX_GRAVES;
		if ( lg.graveCount < LG_MAX_GRAVES ) {
			lg.graveCount++;
		}
		VectorCopy( e->r.currentOrigin, org );
		org[2] += 12;
		if ( ( ++lg.graveTotal % LG_GRAVE_RICH ) == 0 ) {
			G_DS_SpawnGems( org, 25, 0 );
			G_DS_BeamAt( org, DS_BEAM_LEGENDARY, 2500 );
			LG_Fx( LGX_GRAVE, org, NULL, h->s.number, i, 1 );
			G_LogPrintf( "Legend: GRAVEDIGGER rich grave %i -> 25 xp\n", i );
		} else {
			G_DS_SpawnGems( org, 1, 0 );
			LG_Fx( LGX_GRAVE, org, NULL, h->s.number, i, 0 );
			G_LogPrintf( "Legend: GRAVEDIGGER grave %i -> 1 xp\n", i );
		}
	}
}

static void LG_AgentFrame( gentity_t *h ) {
	gentity_t	*a;

	if ( lg.agent < 0 ) {
		return;
	}
	a = &g_entities[lg.agent];
	if ( level.time >= lg.agentUntil || !a->inuse || !a->client || a->health <= 0 ) {
		if ( a->inuse && a->client ) {
			LG_Fx( LGX_AGENT, a->r.currentOrigin, h->r.currentOrigin, h->s.number, lg.agent, 0 );
		}
		G_LogPrintf( "Legend: DOUBLE AGENT %i is back on its own side\n", lg.agent );
		lg.agent = -1;
	}
}

qboolean G_DS_CardsAllies( int a, int b ) {
	int		h;

	if ( lg.agent < 0 || !G_RL_Active() ) {
		return qfalse;
	}
	h = G_RL_HumanClient();
	return ( a == lg.agent && b == h ) || ( b == lg.agent && a == h );
}

// the legendary HUD row: "dsclg <mirror ready> <phoenix ready> <agent client> <agent s left> <agent cooldown s> <orbs> <black mass count> <held mask>"
static void LG_HudFrame( gentity_t *h ) {
	char	s[96];
	int		life = h->client->ps.persistant[PERS_SPAWN_COUNT];
	int		cd = lg.agentReady > level.time ? ( lg.agentReady - level.time + 999 ) / 1000 : 0;
	int		left = lg.agent >= 0 ? ( lg.agentUntil - level.time + 999 ) / 1000 : 0;
	int		held = 0, i;

	for ( i = 0 ; i < LG_NUM ; i++ ) {
		if ( LG_Has( h, i ) ) {
			held |= 1 << i;
		}
	}
	Com_sprintf( s, sizeof( s ), "%i %i %i %i %i %i %i %i", lg.mirrorLife != life, !lg.phoenixUsed, lg.agent, left, cd,
		LG_Has( h, LG_MAGNETAR ) ? LG_Orbs() : 0, lg.blackmassCount, held );
	if ( strcmp( s, lg.hud ) && ( level.time >= lg.hudNext || lg.agent >= 0 ) ) {
		Q_strncpyz( lg.hud, s, sizeof( lg.hud ) );
		lg.hudNext = level.time + 250;
		trap_SendServerCommand( h->s.number, va( "dsclg %s", s ) );
	}
}

void G_DS_CardsFrame( void ) {
	gentity_t	*h;

	if ( !G_RL_Active() || !( h = LG_Human() ) || level.intermissiontime ) {
		return;
	}
	LG_EchoFrame( h );
	if ( LG_Has( h, LG_MAGNETAR ) ) {
		LG_OrbFrame( h );
	}
	if ( LG_Has( h, LG_GRAVEDIGGER ) ) {
		LG_GraveFrame( h );
	}
	LG_AgentFrame( h );
	LG_HudFrame( h );
}

/*
===============================================================================

THE FLOOR-END TALLY

===============================================================================
*/

/*
=================
G_DS_CardsFloorEnd

RL_Finish, after G_DS_MetaFloorEnd (the run key fst holds this floor): a won floor sends
"dsctally kills headshots gibs chain gems damage styleRank skulls recordMask floor preview" and the
server waits in RLP_END for the client's dsctallydone. Personal bests: rearmed_tally.txt.
=================
*/
static void LG_TallyRecords( int *v, int *mask ) {
	fileHandle_t	fh;
	char			buf[1024], line[64];
	int				len, i, best[8];
	char			*p;

	memset( best, 0, sizeof( best ) );
	len = trap_FS_FOpenFile( LG_TALLY_FILE, &fh, FS_READ );
	if ( fh ) {
		if ( len > 0 && len < (int)sizeof( buf ) ) {
			trap_FS_Read( buf, len, fh );
			buf[len] = 0;
			for ( i = 0 ; i < 8 ; i++ ) {
				p = strstr( buf, va( "best_%s ", lgTallyKeys[i] ) );
				if ( p ) {
					best[i] = atoi( p + strlen( lgTallyKeys[i] ) + 6 );
				}
			}
		}
		trap_FS_FCloseFile( fh );
	}
	*mask = 0;
	buf[0] = 0;
	for ( i = 0 ; i < 8 ; i++ ) {
		if ( v[i] > best[i] && v[i] > 0 ) {
			*mask |= 1 << i;
			best[i] = v[i];
		}
		Com_sprintf( line, sizeof( line ), "best_%s %i\n", lgTallyKeys[i], best[i] );
		Q_strcat( buf, sizeof( buf ), line );
	}
	if ( !*mask || G_RL_NoSave() ) {		// (en-daily: the daily keeps no tally records)
		if ( G_RL_NoSave() ) {
			*mask = 0;		// en-boss: boss practice sets no records
		}
		return;
	}
	trap_FS_FOpenFile( LG_TALLY_FILE, &fh, FS_WRITE );
	if ( fh ) {
		trap_FS_Write( buf, strlen( buf ), fh );
		trap_FS_FCloseFile( fh );
	}
}

static void LG_SendTally( qboolean preview ) {
	gentity_t	*h = LG_Human();

	if ( !h ) {
		return;
	}
	trap_SendServerCommand( h->s.number, va( "dsctally %i %i %i %i %i %i %i %i %i %i %i", lg.tally[0], lg.tally[1], lg.tally[2],
		lg.tally[3], lg.tally[4], lg.tally[5], lg.tally[6], lg.tally[7], lg.tally[8], lg.tally[9], preview ) );
}

void G_DS_CardsFloorEnd( qboolean won, int levelKills, int styleRank, int reward ) {
	int			fst[DSST_NUM + 8], v[8], mask, i;
	gentity_t	*h = LG_Human();

	lg.tallyWait = qfalse;
	if ( !G_RL_Active() || !won || !h || G_RL_Practice() ) {		// (en-boss: boss practice has no tally)
		return;
	}
	memset( fst, 0, sizeof( fst ) );
	LG_ParseCsv( G_RL_RunGet( "fst" ), fst, DSST_NUM );
	v[0] = fst[DSST_KILLS] > levelKills ? fst[DSST_KILLS] : levelKills;
	v[1] = fst[DSST_HEADSHOTS];
	v[2] = fst[DSST_GIBS];
	v[3] = G_DS_FrenzyBestChain();
	v[4] = fst[DSST_GEMS];
	v[5] = fst[DSST_DAMAGE];
	v[6] = styleRank;
	v[7] = reward;
	LG_TallyRecords( v, &mask );
	for ( i = 0 ; i < 8 ; i++ ) {
		lg.tally[i] = v[i];
	}
	lg.tally[8] = mask;
	lg.tally[9] = G_RL_Floor();
	lg.tallyValid = qtrue;
	lg.tallyWait = !( h->r.svFlags & SVF_BOT );		// the sim bot plays no tally
	lg.tallyDone = qfalse;
	lg.tallySent = level.time;
	LG_SendTally( qfalse );
	G_LogPrintf( "Legend: tally floor %i kills %i hs %i gibs %i chain %i gems %i dmg %i style %i skulls %i records 0x%x\n",
		G_RL_Floor(), v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], mask );
}

qboolean G_DS_CardsTallyReady( void ) {
	if ( !lg.tallyWait || lg.tallyDone || level.time >= lg.tallySent + LG_TALLY_WAIT ) {
		return qtrue;
	}
	return qfalse;
}

qboolean G_DS_CardsClientCommand( gentity_t *ent, const char *cmd ) {
	if ( !Q_stricmp( cmd, "dsctallydone" ) ) {
		if ( lg.tallyWait && !lg.tallyDone ) {
			lg.tallyDone = qtrue;
			G_LogPrintf( "Legend: tally done after %i ms\n", level.time - lg.tallySent );
		}
		return qtrue;
	}
	return qfalse;
}

/*
=================
G_DS_CardsCommand

Dev: dsoffer legendary [n] (the next offer holds one: rlwin deals it), dstally (show the last
tally again, or a sample one), dslegend <n> (give the player legendary n this floor),
dslgstate
=================
*/
qboolean G_DS_CardsCommand( const char *cmd ) {
	char	arg[32];
	int		n, i;

	if ( !Q_stricmp( cmd, "dsoffer" ) ) {
		trap_Argv( 1, arg, sizeof( arg ) );
		if ( !Q_stricmp( arg, "legendary" ) ) {
			trap_Argv( 2, arg, sizeof( arg ) );
			lgForce = arg[0] ? atoi( arg ) : -1;
			G_Printf( "dsoffer: the next offer holds a legendary (%s)\n", lgForce >= 0 && lgForce < LG_NUM
				? bg_rlCardNames[RLC_LEGEND_FIRST + lgForce] : "any" );
		} else {
			lgForce = -2;
			G_Printf( "dsoffer legendary [0-%i]: force a legendary into the next offer\n", LG_NUM - 1 );
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dstally" ) ) {
		if ( !lg.tallyValid ) {
			lg.tally[0] = 14; lg.tally[1] = 5; lg.tally[2] = 7; lg.tally[3] = 6; lg.tally[4] = 86;
			lg.tally[5] = 2140; lg.tally[6] = 6; lg.tally[7] = 23; lg.tally[8] = 0x9 | 0x80; lg.tally[9] = G_RL_Floor();
		}
		LG_SendTally( qtrue );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dslegend" ) ) {
		trap_Argv( 1, arg, sizeof( arg ) );
		n = atoi( arg );
		if ( n >= 0 && n < LG_NUM ) {
			trap_SendConsoleCommand( EXEC_INSERT, va( "rlgive %i\n", RLC_LEGEND_FIRST + n ) );		// (now, not after the rest of a cfg)
			G_Printf( "dslegend: %s (card %i)\n", bg_rlCardNames[RLC_LEGEND_FIRST + n], RLC_LEGEND_FIRST + n );
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dslgtest" ) ) {
		// dev / tests: dslgtest pull <client> [dist] | hit <client> <dmg> | hurt <client> <dmg> | bm <n> | agentkill
		gentity_t	*h = LG_Human(), *e = NULL;
		char		a1[16], a2[16];
		vec3_t		fwd, ang, pos;

		trap_Argv( 1, arg, sizeof( arg ) );
		trap_Argv( 2, a1, sizeof( a1 ) );
		trap_Argv( 3, a2, sizeof( a2 ) );
		if ( !h ) {
			return qtrue;
		}
		n = atoi( a1 );
		if ( n >= 0 && n < level.maxclients && g_entities[n].inuse && g_entities[n].client ) {
			e = &g_entities[n];
		}
		if ( !Q_stricmp( arg, "pull" ) && e && e != h ) {
			VectorSet( ang, 0, h->client->ps.viewangles[YAW], 0 );
			AngleVectors( ang, fwd, NULL, NULL );
			VectorMA( h->client->ps.origin, a2[0] ? atoi( a2 ) : 60, fwd, pos );
			VectorCopy( pos, e->client->ps.origin );
			VectorClear( e->client->ps.velocity );
			e->client->ps.eFlags ^= EF_TELEPORT_BIT;
			BG_PlayerStateToEntityState( &e->client->ps, &e->s, qtrue );
			VectorCopy( pos, e->r.currentOrigin );
			trap_LinkEntity( e );
		} else if ( !Q_stricmp( arg, "hit" ) && e && e != h ) {
			G_Damage( e, h, h, NULL, NULL, atoi( a2 ), 0, MOD_MACHINEGUN );
		} else if ( !Q_stricmp( arg, "hurt" ) && e && e != h ) {
			G_Damage( h, e, e, NULL, NULL, atoi( a2 ), 0, MOD_MACHINEGUN );
		} else if ( !Q_stricmp( arg, "bm" ) ) {
			lg.blackmassCount = n;
		} else if ( !Q_stricmp( arg, "agentkill" ) && lg.agent >= 0 ) {
			for ( i = 0 ; i < level.maxclients ; i++ ) {
				e = &g_entities[i];
				if ( i != lg.agent && LG_Enemy( e, h ) ) {
					G_LogPrintf( "LGTest: agent %i shoots %i (health %i)\n", lg.agent, i, e->health );
					G_Damage( e, &g_entities[lg.agent], &g_entities[lg.agent], NULL, NULL, e->health + 60, DAMAGE_NO_PROTECTION | DAMAGE_NO_ARMOR, MOD_SHOTGUN );
					G_LogPrintf( "LGTest: -> health %i score of the agent %i\n", e->health, g_entities[lg.agent].client->ps.persistant[PERS_SCORE] );
					break;
				}
			}
		}
		G_LogPrintf( "LGTest: %s %s %s -> player health %i armor %i score %i\n", arg, a1, a2, h->health, h->client->ps.stats[STAT_ARMOR],
			h->client->ps.persistant[PERS_SCORE] );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dslgstate" ) ) {
		gentity_t	*h = LG_Human();

		for ( i = 0 ; i < LG_NUM ; i++ ) {
			G_LogPrintf( "LGState: %i %s held %i\n", i, bg_rlCardNames[RLC_LEGEND_FIRST + i], h ? LG_Has( h, i ) : 0 );
		}
		G_LogPrintf( "LGState: hud %s agent %i orbs %i phoenixUsed %i mirrorLife %i\n", lg.hud, lg.agent, LG_Orbs(),
			lg.phoenixUsed, lg.mirrorLife );
		return qtrue;
	}
	return qfalse;
}
