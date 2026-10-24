/*
===========================================================================
REARMED -- DESCENT v2 LOOT (game side): xp gems, the vacuum, treasure chests and their
end-of-floor jackpot, passive items, the v2 card pool rules, bot passives,
CS_DESCENTGROW and the run keys pv / chests / gems / wused.

Gems / vacuum / chests are ET_GENERAL entities (generic1 RM_ENT_GEM / VACUUM / CHEST).
A gem rests where it lands; when the human player comes inside the pull radius (or takes a
vacuum) the server writes the flight into the entity state once (s.time = start,
s.time2 = arrival, s.otherEntityNum = the player) and cgame flies it in, so a pull costs no
bandwidth after that. The gem's xp is credited at s.time2.
See docs/DESCENT_V2.md (LOOT row) and bg_descent.h.
===========================================================================
*/
#include "g_local.h"

#define DS_GEM_CAP			150		// live gems; more merge into the nearest resting one
#define DS_GEM_HARDCAP		190
#define DS_PULL_BASE		110.0f	// pull radius
#define DS_PICK_CHEST		46.0f	// chest / vacuum touch distance
#define DS_POP_MSEC			350		// a fresh gem flies a moment before it can be pulled
#define DS_MAX_CHESTS		12		// banked
#define DS_MAX_JP			8		// chests opened at one floor end
#define DS_MAX_REWARDS		5
#define DS_CS_MSEC			250
#define DS_STOPWATCH_MSEC	20000
#define DS_STOPWATCH_FREEZE	1500

// reward codes in the jackpot configstring
#define DSR_EVO				1		// 1000 + evo * 10
#define DSR_WEAPON			2		// 2000 + weapon * 10 + new level
#define DSR_PASSIVE			3		// 3000 + passive * 10 + new level
#define DSR_SKULLS			4		// 4000 + skulls

typedef struct {
	int		quality;
	int		num;
	int		reward[DS_MAX_REWARDS];
} lootJackpot_t;

typedef struct {
	int			pv[RLP_NUM];			// the human's passive levels
	int			chests[DS_MAX_CHESTS];	// banked chest qualities
	int			numChests;
	int			gemsRun;				// gems collected this run
	int			gemsFloor;
	int			wused;					// weapons used this run (bit per weapon_t)
	int			secrets;				// meta: unlocked secret cards
	int			vacuums;				// vacuums taken this floor (cgame: whoosh on change)
	float		xpFrac[DS_NUM_WEAPONS];

	lootJackpot_t	jp[DS_MAX_JP];
	int			numJP;
	qboolean	chestDone;				// the client finished (or skipped) the jackpot screen

	qboolean	dirty;
	int			csTime;

	float		regen[MAX_CLIENTS];
	qboolean	pentagramUsed;
	int			pentagramAt;			// level.time of a pending pentagram blast, 0 none
	int			stopwatchReady;			// level.time the stopwatch can fire again

	float		power[MAX_CLIENTS];		// G_DS_PowerScore, refreshed every second
	int			powerTime;
	qboolean	powerLogged;
} lootState_t;

static float Loot_MatchScale( gentity_t *bot );
static void Loot_LogPower( const char *when );

static lootState_t	lootSt;

static gentity_t *Loot_Human( void ) {
	int		h = G_RL_HumanClient();

	if ( h < 0 ) {
		return NULL;
	}
	return &g_entities[h];
}

static qboolean Loot_IsHuman( gentity_t *ent ) {
	return ent && ent->client && G_RL_IsPlayer( ent ) && ent - g_entities == G_RL_HumanClient();		// (or the g_dsBossSim bot)
}

void G_DS_GrowChanged( void ) {
	lootSt.dirty = qtrue;
}

/*
===============================================================================

PASSIVES

===============================================================================
*/

int G_DS_PassiveLevel( gentity_t *ent, int passive ) {
	int		n;

	if ( !G_RL_Active() || !ent || !ent->client || passive < 0 || passive >= RLP_NUM ) {
		return 0;
	}
	if ( G_RL_IsPlayer( ent ) ) {		// (or the ACTS g_dsBossSim bot)
		return lootSt.pv[passive];
	}
	n = G_RL_CardCount( ent, RLC_PASSIVE_FIRST + passive );
	return n > DS_MAX_PLEVEL ? DS_MAX_PLEVEL : n;
}

static float Loot_PV( gentity_t *ent, int p ) {
	return G_DS_PassiveLevel( ent, p ) * ds_passivePerLevel[p];
}

// different passives held / free slot
static int Loot_PassiveSlots( const int *levels ) {
	int		p, n = 0;

	for ( p = 0 ; p < RLP_NUM ; p++ ) {
		if ( levels[p] > 0 ) {
			n++;
		}
	}
	return n;
}

static void Loot_SetPassive( int p, int level ) {
	if ( p < 0 || p >= RLP_NUM ) {
		return;
	}
	if ( level < 0 ) {
		level = 0;
	} else if ( level > DS_MAX_PLEVEL ) {
		level = DS_MAX_PLEVEL;
	}
	lootSt.pv[p] = level;
	lootSt.dirty = qtrue;
}

// the human's luck multiplier (chest jackpots, card rarity, lucky drops)
static float Loot_Luck( void ) {
	gentity_t	*h = Loot_Human();

	return 1.0f + ( h ? Loot_PV( h, RLP_LUCK ) : lootSt.pv[RLP_LUCK] * ds_passivePerLevel[RLP_LUCK] ) + G_DS_UpValue( DSU_LUCK );
}

int G_DS_LuckPct( void ) {
	if ( !G_RL_Active() ) {
		return 0;
	}
	return (int)( ( Loot_Luck() - 1.0f ) * 100.0f + 0.5f );
}

/*
===============================================================================

POWER: one number for how strong a client is (1.0 = a fresh run's start). Enemies are matched
to the player's: normal bots ~0.95x, elites ~1.1x (their damage dealt and taken), bosses scale
their health by it (ACTS). The weapon part is small: ARSENAL already gives bots matching
weapon levels.

===============================================================================
*/

float G_DS_PowerScore( gentity_t *ent ) {
	float		wf, off, def, hp, it, regen;
	int			w, lv, best = 1, second = 1, c, perks = 0, abil = 0, e, held, evo = 0;
	qboolean	human;

	if ( !G_RL_Active() || !ent || !ent->client ) {
		return 1.0f;
	}
	human = Loot_IsHuman( ent );
	held = ent->client->ps.stats[STAT_WEAPONS];
	for ( w = 1 ; w < DS_NUM_WEAPONS ; w++ ) {
		if ( !DS_WEAPON_LEVELS( w ) ) {
			continue;
		}
		if ( !( held & ( 1 << w ) ) && !( human && ( lootSt.wused & ( 1 << w ) ) ) ) {
			continue;
		}
		lv = G_DS_WeaponLevel( ent, w );
		if ( lv > best ) {
			second = best;
			best = lv;
		} else if ( lv > second ) {
			second = lv;
		}
	}
	for ( e = 0 ; e < EVO_NUM ; e++ ) {
		if ( G_DS_Evolved( ent, e ) ) {
			evo++;
		}
	}
	wf = 1.0f + 0.03f * ( 0.65f * ( best - 1 ) + 0.35f * ( second - 1 ) ) + 0.04f * ( evo > 2 ? 2 : evo );
	off = wf * ( 1.0f + Loot_PV( ent, RLP_MIGHT ) ) * ( 1.0f + Loot_PV( ent, RLP_TEMPO ) )
		* ( 1.0f + 0.3f * Loot_PV( ent, RLP_AREA ) ) * ( 1.0f + 0.2f * Loot_PV( ent, RLP_VELOCITY ) );
	hp = ent->client->ps.stats[STAT_MAX_HEALTH] > 0 ? ent->client->ps.stats[STAT_MAX_HEALTH] / 100.0f : 1.0f;
	regen = Loot_PV( ent, RLP_REGEN );		// ~ +8% staying power per 0.3 hp/s
	def = hp / ( 1.0f - Loot_PV( ent, RLP_IRONHIDE ) ) * ( 1.0f + regen * 0.25f ) * ( 1.0f + 0.15f * Loot_PV( ent, RLP_HASTE ) );
	for ( c = 0 ; c < RLC_PASSIVE_FIRST ; c++ ) {
		switch ( bg_rlCardKind[c] ) {
		case RLK_PERK:
			perks += G_RL_CardCount( ent, c ) > 0;
			break;
		case RLK_ABILITY:
		case RLK_ALT:
			abil += G_RL_CardCount( ent, c ) > 0;
			break;
		default:
			break;
		}
	}
	it = 1.0f + 0.025f * perks + 0.01f * abil + G_DS_CardsPower( ent );		// + CARDS: legendaries held
	return (float)sqrt( off * def ) * it;
}

// cached once a second (G_DS_LootFrame)
static float Loot_Power( int client ) {
	if ( client < 0 || client >= MAX_CLIENTS ) {
		return 1.0f;
	}
	if ( lootSt.power[client] <= 0 ) {
		lootSt.power[client] = G_DS_PowerScore( &g_entities[client] );
	}
	return lootSt.power[client];
}

/*
=================
Loot_MatchScale

A bot's damage dealt is multiplied and its damage taken divided by this: sqrt of how far
its power is from where it should sit relative to the player's (0.95x normal, 1.1x elite),
clamped so a strong build still wins
=================
*/
static float Loot_MatchScale( gentity_t *bot ) {
	int		h = G_RL_HumanClient(), role;
	float	want, m;

	if ( h < 0 || !bot || !bot->client ) {
		return 1.0f;
	}
	role = G_RL_ClientRole( bot );
	want = Loot_Power( h ) * ( role == 3 ? 1.1f : 0.95f );
	m = want / Loot_Power( bot - g_entities );
	if ( m < 0.75f ) {
		m = 0.75f;
	} else if ( m > 1.6f ) {
		m = 1.6f;
	}
	return (float)sqrt( m );
}

static void Loot_LogPower( const char *when ) {
	int			i, h = G_RL_HumanClient(), n = 0;
	float		sum = 0;
	gentity_t	*e;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		e = &g_entities[i];
		if ( i == h || !e->inuse || !e->client || e->client->pers.connected != CON_CONNECTED ) {
			continue;
		}
		sum += G_DS_PowerScore( e );
		n++;
	}
	G_LogPrintf( "Loot: power %s floor %i player %.2f bots %.2f (%i)\n", when, G_RL_Floor(),
		h >= 0 ? G_DS_PowerScore( &g_entities[h] ) : 1.0f, n ? sum / n : 0.0f, n );
}

/*
=================
G_DS_LootDamageScale

From G_RL_DamageScale (any damage in a run): SPITE on the attacker, IRON HIDE / BLOOD CROWN
on the target
=================
*/
float G_DS_LootDamageScale( gentity_t *targ, gentity_t *attacker ) {
	float	s = 1.0f, t;
	int		role;

	if ( !G_RL_Active() ) {
		return 1.0f;
	}
	if ( attacker && attacker->client && attacker != targ ) {
		s *= 1.0f + Loot_PV( attacker, RLP_MIGHT );
		// enemies keep up with the player's power (never far past it)
		role = G_RL_ClientRole( attacker );
		if ( role == 2 || role == 3 ) {
			s *= Loot_MatchScale( attacker );
		}
	}
	if ( targ && targ->client && attacker != targ ) {
		role = G_RL_ClientRole( targ );
		if ( role == 2 || role == 3 ) {
			s /= Loot_MatchScale( targ );
		}
	}
	if ( targ && targ->client ) {
		t = Loot_PV( targ, RLP_IRONHIDE );
		if ( Loot_IsHuman( targ ) ) {
			t += G_DS_UpValue( DSU_IRONHIDE );
			if ( G_RL_HasCard( targ, RLC_SECRET_FIRST + RLS_BLOODCROWN ) ) {
				t -= 0.15f;
			}
		}
		if ( t > 0.6f ) {
			t = 0.6f;
		}
		s *= 1.0f - t;
	}
	return s;
}

float G_DS_LootSpeedBonus( gentity_t *ent ) {
	if ( !G_RL_Active() ) {
		return 0;
	}
	return Loot_PV( ent, RLP_HASTE );
}

int G_DS_LootHealthBonus( gentity_t *ent ) {
	if ( !G_RL_Active() ) {
		return 0;
	}
	return (int)( Loot_PV( ent, RLP_HEART ) + 0.5f );
}

int G_DS_LootAmmoPickup( gentity_t *ent, int quantity ) {
	if ( !G_RL_Active() ) {
		return quantity;
	}
	return quantity + (int)( quantity * Loot_PV( ent, RLP_AMMOBELT ) + 0.5f );
}

int G_DS_MaxAmmo( gentity_t *ent, int base ) {
	if ( !G_RL_Active() || !ent || !ent->client ) {
		return base;
	}
	return base + (int)( base * Loot_PV( ent, RLP_AMMOBELT ) + 0.5f );
}

// GRAVE COIN: extra skull percent at the floor end
int G_DS_LootSkullPct( void ) {
	gentity_t	*h = Loot_Human();

	if ( !G_RL_Active() || !h ) {
		return 0;
	}
	return (int)( Loot_PV( h, RLP_GREED ) * 100.0f + 0.5f );
}

static float Loot_PullRadius( gentity_t *h ) {
	return DS_PULL_BASE * ( 1.0f + Loot_PV( h, RLP_MAGNET ) + G_DS_UpValue( DSU_MAGNET ) ) * G_DS_FrenzyPullScale();	// FRENZY: chains pull from further
}

/*
=================
G_DS_LootLethal

g_combat.c, just before the damage is applied: PENTAGRAM turns the player's killing blow
into a 1 hp survival and a blast that gibs every enemy in sight (once a floor)
=================
*/
int G_DS_LootLethal( gentity_t *targ, gentity_t *attacker, int take ) {
	if ( !G_RL_Active() || take <= 0 || !Loot_IsHuman( targ ) || targ->health - take > 0 || targ->health <= 0
		|| lootSt.pentagramUsed || level.intermissiontime || !G_RL_HasCard( targ, RLC_SECRET_FIRST + RLS_PENTAGRAM ) ) {
		return take;
	}
	lootSt.pentagramUsed = qtrue;
	lootSt.pentagramAt = level.time + 50;
	G_LogPrintf( "Loot: pentagram saves client %i\n", (int)( targ - g_entities ) );
	return targ->health - 1;
}

static void Loot_PentagramBlast( gentity_t *h ) {
	int			i;
	gentity_t	*e;
	trace_t		tr;
	vec3_t		eye, dir;

	VectorCopy( h->client->ps.origin, eye );
	eye[2] += h->client->ps.viewheight;
	G_SlowMoEx( 0.3f, 900, qtrue );
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		e = &g_entities[i];
		if ( e == h || !e->inuse || !e->client || e->health <= 0 || e->client->sess.sessionTeam == TEAM_SPECTATOR ) {
			continue;
		}
		if ( !trap_InPVS( eye, e->r.currentOrigin ) ) {
			continue;
		}
		trap_Trace( &tr, eye, NULL, NULL, e->r.currentOrigin, h->s.number, MASK_SOLID );
		if ( tr.fraction < 1.0f ) {
			continue;
		}
		VectorSubtract( e->r.currentOrigin, eye, dir );
		VectorNormalize( dir );
		G_Damage( e, h, h, dir, e->r.currentOrigin, G_RL_ClientRole( e ) == 4 ? 400 : 999, DAMAGE_NO_ARMOR, MOD_CORPSE );
	}
}

/*
===============================================================================

GEMS

===============================================================================
*/

static int Loot_GemTier( int xp ) {
	return xp >= 25 ? 2 : xp >= 5 ? 1 : 0;
}

static int Loot_LiveGems( void ) {
	int			i, n = 0;
	gentity_t	*e;

	for ( i = MAX_CLIENTS, e = &g_entities[MAX_CLIENTS] ; i < level.num_entities ; i++, e++ ) {
		if ( e->inuse && e->s.eType == ET_GENERAL && e->s.generic1 == RM_ENT_GEM ) {
			n++;
		}
	}
	return n;
}

// a resting (not pulled) gem near a point, the same weapon if possible
static gentity_t *Loot_MergeTarget( vec3_t origin, int weapon ) {
	int			i, pass;
	gentity_t	*e, *best = NULL;
	float		d, bestD = 0;

	for ( pass = 0 ; pass < 2 && !best ; pass++ ) {
		for ( i = MAX_CLIENTS, e = &g_entities[MAX_CLIENTS] ; i < level.num_entities ; i++, e++ ) {
			if ( !e->inuse || e->s.eType != ET_GENERAL || e->s.generic1 != RM_ENT_GEM || e->s.time ) {
				continue;
			}
			if ( pass == 0 && e->s.otherEntityNum2 != weapon ) {
				continue;
			}
			d = Distance( e->r.currentOrigin, origin );
			if ( !best || d < bestD ) {
				best = e;
				bestD = d;
			}
		}
	}
	return best;
}

static void Loot_DropThink( gentity_t *ent );

// a drop entity flying out of origin
static gentity_t *Loot_SpawnDrop( vec3_t origin, int marker, float speed, float up ) {
	gentity_t	*ent;
	float		a;

	ent = G_Spawn();
	ent->s.eType = ET_GENERAL;
	ent->s.generic1 = marker;
	ent->r.svFlags = 0;
	ent->r.contents = 0;
	VectorSet( ent->r.mins, -4, -4, -4 );
	VectorSet( ent->r.maxs, 4, 4, 4 );
	G_SetOrigin( ent, origin );
	a = random() * 2 * M_PI;
	ent->s.pos.trType = TR_GRAVITY;
	ent->s.pos.trTime = level.time;
	VectorCopy( origin, ent->s.pos.trBase );
	ent->s.pos.trDelta[0] = cos( a ) * speed * ( 0.4f + 0.6f * random() );
	ent->s.pos.trDelta[1] = sin( a ) * speed * ( 0.4f + 0.6f * random() );
	ent->s.pos.trDelta[2] = up * ( 0.75f + 0.5f * random() );
	ent->s.time = 0;
	ent->s.time2 = 0;
	ent->s.otherEntityNum = ENTITYNUM_NONE;
	ent->s.angles[YAW] = random() * 360;
	ent->timestamp = level.time;
	ent->think = Loot_DropThink;
	ent->nextthink = level.time + 1;
	trap_LinkEntity( ent );
	return ent;
}

static gentity_t *Loot_SpawnGem( vec3_t origin, int xp, int weapon ) {
	gentity_t	*ent;
	int			live;

	if ( xp <= 0 ) {
		return NULL;
	}
	if ( weapon < 0 || weapon >= DS_NUM_WEAPONS || !DS_WEAPON_LEVELS( weapon ) ) {
		weapon = 0;
	}
	live = Loot_LiveGems();
	if ( live >= DS_GEM_CAP ) {
		ent = Loot_MergeTarget( origin, weapon );
		if ( ent || live >= DS_GEM_HARDCAP ) {
			if ( ent ) {
				ent->count += xp;
				ent->s.modelindex2 = Loot_GemTier( ent->count );
			}
			return ent;
		}
	}
	ent = Loot_SpawnDrop( origin, RM_ENT_GEM, 170, 230 );
	ent->classname = "ds_gem";
	ent->count = xp;
	ent->s.modelindex2 = Loot_GemTier( xp );
	ent->s.otherEntityNum2 = weapon;
	if ( ent->s.modelindex2 == 2 ) {
		G_DS_LootBeam( ent, DS_BEAM_COMMON );		// FRENZY: a white beam on a big red gem
	}
	return ent;
}

/*
=================
G_DS_SpawnGems

A spray of gems worth about xp in total (biggest tiers first, at least a few gems so a kill
looks generous). weaponHint 0 / -1: the xp goes to the weapon held when it is collected.
=================
*/
void G_DS_SpawnGems( vec3_t origin, int xp, int weaponHint ) {
	int		vals[24], n = 0, i, v;
	vec3_t	org;

	if ( !G_RL_Active() || xp <= 0 ) {
		return;
	}
	xp = G_DS_SurpriseGemAmount( xp );		// ds-surprise: SWARM
	VectorCopy( origin, org );
	org[2] += 16;
	while ( xp > 0 && n < 24 ) {
		v = xp >= 25 ? 25 : xp >= 5 ? 5 : 1;
		// break a green up while the spray is still small
		if ( v == 5 && n < 3 && xp < 10 ) {
			v = 1;
		}
		if ( n == 23 ) {
			v = xp;
		}
		vals[n++] = v;
		xp -= v;
	}
	for ( i = 0 ; i < n ; i++ ) {
		Loot_SpawnGem( org, vals[i], weaponHint );
	}
}

/*
=================
G_DS_SpawnGemFloat

One gem that hangs where it is put (the vault's RUSH and SKY): no pop, no fall
=================
*/
void G_DS_SpawnGemFloat( vec3_t origin, int xp ) {
	gentity_t	*ent;

	if ( !G_RL_Active() || xp <= 0 ) {
		return;
	}
	ent = Loot_SpawnGem( origin, xp, 0 );
	if ( ent && ent->timestamp == level.time && ent->count == xp && ent->s.pos.trType == TR_GRAVITY && !ent->s.time ) {
		G_SetOrigin( ent, origin );		// (a merged gem stays where it was)
		ent->timestamp = level.time - DS_POP_MSEC;
		trap_LinkEntity( ent );
	}
}

gentity_t *G_DS_SpawnChest( vec3_t origin, int quality ) {
	gentity_t	*ent;
	vec3_t		org;

	if ( !G_RL_Active() ) {
		return NULL;
	}
	VectorCopy( origin, org );
	org[2] += 16;
	ent = Loot_SpawnDrop( org, RM_ENT_CHEST, 90, 260 );
	ent->classname = "ds_chest";
	ent->s.modelindex2 = quality < 0 ? 0 : quality > 2 ? 2 : quality;
	VectorSet( ent->r.mins, -8, -8, 0 );
	VectorSet( ent->r.maxs, 8, 8, 12 );
	G_LogPrintf( "Loot: chest %i dropped\n", ent->s.modelindex2 );
	G_DS_LootBeam( ent, ent->s.modelindex2 == 2 ? DS_BEAM_LEGENDARY : ent->s.modelindex2 == 1 ? DS_BEAM_EPIC : DS_BEAM_RARE );	// FRENZY
	return ent;
}

static gentity_t *Loot_SpawnVacuum( vec3_t origin ) {
	gentity_t	*ent;
	vec3_t		org;

	VectorCopy( origin, org );
	org[2] += 16;
	ent = Loot_SpawnDrop( org, RM_ENT_VACUUM, 110, 280 );
	ent->classname = "ds_vacuum";
	G_LogPrintf( "Loot: vacuum dropped\n" );
	G_DS_LootBeam( ent, DS_BEAM_RARE );		// FRENZY
	return ent;
}

// how long a pulled gem flies (accelerating in)
static int Loot_FlightMsec( float dist ) {
	return (int)( ( 140 + sqrt( dist ) * 17.0f ) * G_DS_FrenzyFlightScale() );	// FRENZY: chains pull faster
}

static void Loot_StartPull( gentity_t *gem, gentity_t *h, int delay, qboolean vacuum ) {
	vec3_t	pos;

	BG_EvaluateTrajectory( &gem->s.pos, level.time, pos );
	G_SetOrigin( gem, pos );
	gem->s.time = level.time + delay;
	gem->s.time2 = gem->s.time + Loot_FlightMsec( Distance( pos, h->r.currentOrigin ) );
	gem->s.otherEntityNum = h->s.number;
	gem->s.frame = vacuum ? 1 : 0;
	if ( vacuum ) {
		gem->r.svFlags |= SVF_BROADCAST;		// streams from rooms the player can't see
	}
	trap_LinkEntity( gem );
}

static void Loot_CollectGem( gentity_t *gem, gentity_t *h ) {
	int		w = gem->s.otherEntityNum2, n;
	float	xp;

	if ( !w ) {
		w = h->client->ps.weapon;
	}
	if ( w > 0 && w < DS_NUM_WEAPONS && DS_WEAPON_LEVELS( w ) ) {
		xp = gem->count * ( 1.0f + Loot_PV( h, RLP_GROWTH ) + G_DS_UpValue( DSU_GROWTH ) ) * G_DS_RewardMult();	// difficulty reward multiplier (ds-economy)
		if ( G_RL_HasCard( h, RLC_SECRET_FIRST + RLS_BLOODCROWN ) ) {
			xp *= 2;
		}
		xp *= G_DS_SurpriseGemXpMult();		// ds-surprise: BLACKOUT
		xp += lootSt.xpFrac[w];
		n = (int)xp;
		lootSt.xpFrac[w] = xp - n;
		if ( n > 0 ) {
			G_DS_AddWeaponXP( h, w, n );
		}
		lootSt.wused |= 1 << w;
	}
	lootSt.gemsFloor++;
	lootSt.gemsRun++;
	G_DS_Stat( DSST_GEMS, 1 );
	G_DS_CardsGem( h );		// CARDS: MAGNETAR (a gem joins the orbit)
	lootSt.dirty = qtrue;
	gem->count = 0;			// (freed by its think a moment later)
}

/*
=================
G_DS_Vacuum

Every gem on the map flies to the player: the near ones first, then streams from far away
=================
*/
void G_DS_Vacuum( gentity_t *h ) {
	int			i, n = 0, delay;
	gentity_t	*e;
	float		d;

	if ( !G_RL_Active() || !h || !h->client ) {
		return;
	}
	for ( i = MAX_CLIENTS, e = &g_entities[MAX_CLIENTS] ; i < level.num_entities ; i++, e++ ) {
		if ( !e->inuse || e->s.eType != ET_GENERAL || e->s.generic1 != RM_ENT_GEM || e->s.time ) {
			continue;
		}
		d = Distance( e->r.currentOrigin, h->r.currentOrigin );
		delay = (int)( d * 0.22f ) + rand() % 90;
		if ( delay > 900 ) {
			delay = 900 + rand() % 150;
		}
		Loot_StartPull( e, h, delay, qtrue );
		n++;
	}
	lootSt.vacuums++;
	lootSt.dirty = qtrue;
	G_DS_Stat( DSST_VACUUMS, 1 );
	G_LogPrintf( "Loot: vacuum pulls %i gems\n", n );
}

static void Loot_BankChest( int quality ) {
	if ( lootSt.numChests < DS_MAX_CHESTS ) {
		lootSt.chests[lootSt.numChests++] = quality;
	}
	lootSt.dirty = qtrue;
	G_LogPrintf( "Loot: chest %i banked (%i)\n", quality, lootSt.numChests );
}

// ds-surprise: a chest straight into the bank (BLOODLUST's extra chest at the floor end)
void G_DS_BankChest( int quality ) {
	if ( G_RL_Active() ) {
		Loot_BankChest( quality < 0 ? 0 : quality > 2 ? 2 : quality );
	}
}

/*
=================
Loot_DropThink

Every frame for a gem / chest / vacuum: fly (bounce off walls, settle on floors), then wait
for the human player
=================
*/
// a chest / vacuum the player walks over (a box test: it lies on the floor, the player's origin is 24 up)
static qboolean Loot_Touching( gentity_t *ent, gentity_t *h ) {
	float	dx = ent->r.currentOrigin[0] - h->r.currentOrigin[0];
	float	dy = ent->r.currentOrigin[1] - h->r.currentOrigin[1];
	float	dz = ent->r.currentOrigin[2] - h->r.currentOrigin[2];

	return dx * dx + dy * dy < DS_PICK_CHEST * DS_PICK_CHEST && dz > -64 && dz < 48;
}

static void Loot_DropThink( gentity_t *ent ) {
	gentity_t	*h;
	trace_t		tr;
	vec3_t		pos, vel, eye;
	float		d, dot;

	ent->nextthink = level.time + 1;
	h = Loot_Human();

	// a gem on its way in
	if ( ent->s.generic1 == RM_ENT_GEM && ent->s.time ) {
		// credited on arrival, freed a moment later so every client sees it arrive (the pickup tick)
		if ( level.time >= ent->s.time2 && !ent->spawnflags ) {
			ent->spawnflags = 1;
			if ( h && h->s.number == ent->s.otherEntityNum ) {
				Loot_CollectGem( ent, h );
			}
		}
		if ( level.time >= ent->s.time2 + 200 ) {
			G_FreeEntity( ent );
		}
		return;
	}

	if ( ent->s.pos.trType == TR_GRAVITY ) {
		BG_EvaluateTrajectory( &ent->s.pos, level.time, pos );
		trap_Trace( &tr, ent->r.currentOrigin, ent->r.mins, ent->r.maxs, pos, ENTITYNUM_NONE, MASK_SOLID );
		if ( tr.startsolid || tr.allsolid ) {
			ent->s.pos.trType = TR_STATIONARY;
			VectorCopy( ent->r.currentOrigin, ent->s.pos.trBase );
		} else if ( tr.fraction < 1.0f ) {
			if ( tr.plane.normal[2] > 0.6f ) {
				G_SetOrigin( ent, tr.endpos );
			} else {
				// off a wall or a ceiling: reflect, lose half the speed
				BG_EvaluateTrajectoryDelta( &ent->s.pos, level.time, vel );
				dot = DotProduct( vel, tr.plane.normal );
				VectorMA( vel, -2 * dot, tr.plane.normal, vel );
				VectorScale( vel, 0.5f, vel );
				VectorCopy( tr.endpos, ent->s.pos.trBase );
				VectorCopy( vel, ent->s.pos.trDelta );
				ent->s.pos.trTime = level.time;
				VectorCopy( tr.endpos, ent->r.currentOrigin );
			}
		} else {
			VectorCopy( pos, ent->r.currentOrigin );
		}
		if ( ent->s.pos.trType == TR_GRAVITY && level.time - ent->timestamp > 5000 ) {
			G_FreeEntity( ent );		// fell into the void
			return;
		}
		if ( trap_PointContents( ent->r.currentOrigin, -1 ) & CONTENTS_NODROP ) {
			G_FreeEntity( ent );
			return;
		}
		trap_LinkEntity( ent );
	}

	if ( !h || h->health <= 0 || level.intermissiontime || level.intermissionQueued
		|| h->client->sess.sessionTeam == TEAM_SPECTATOR || level.time - ent->timestamp < DS_POP_MSEC ) {
		return;
	}
	VectorCopy( h->r.currentOrigin, eye );
	eye[2] += 8;
	d = Distance( ent->r.currentOrigin, eye );
	switch ( ent->s.generic1 ) {
	case RM_ENT_GEM:
		if ( d < Loot_PullRadius( h ) ) {
			Loot_StartPull( ent, h, 0, qfalse );
		}
		break;
	case RM_ENT_CHEST:
		if ( Loot_Touching( ent, h ) ) {
			Loot_BankChest( ent->s.modelindex2 );
			G_FreeEntity( ent );
		}
		break;
	case RM_ENT_VACUUM:
		if ( Loot_Touching( ent, h ) ) {
			G_FreeEntity( ent );
			G_DS_Vacuum( h );
		}
		break;
	default:
		break;
	}
}

/*
=================
G_DS_LootPlayerDie

A kill by the human player: gems for the weapon that made it; elites add a chest and maybe
a vacuum; rare lucky chests / vacuums from anyone (bosses: ACTS drops their loot)
=================
*/
// FRENZY (ds-frenzy): the kill chain's tier makes the spray bigger
static int Loot_FrenzyXP( int xp ) {
	float	m = G_DS_FrenzyGemMult();
	int		extra;

	if ( m <= 1.0f ) {
		return xp;
	}
	extra = (int)( xp * ( m - 1.0f ) + 0.5f );
	G_DS_FrenzyGemXP( extra );
	return xp + extra;
}

void G_DS_LootPlayerDie( gentity_t *self, gentity_t *attacker, int mod ) {
	int		role, xp, w, floorNum;
	float	luck, r;
	vec3_t	org;

	if ( !G_RL_Active() || !self || !self->client || !attacker || attacker == self || !Loot_IsHuman( attacker )
		|| level.warmupTime || level.intermissiontime || level.intermissionQueued ) {
		return;
	}
	role = G_RL_ClientRole( self );
	if ( role == 4 ) {
		return;		// bosses: ACTS drops the big chest and the gems
	}
	floorNum = G_RL_Floor();
	w = G_DS_WeaponForMod( mod );
	if ( w <= 0 ) {
		w = attacker->client->ps.weapon;
	}
	VectorCopy( self->r.currentOrigin, org );
	luck = Loot_Luck();
	if ( role == 3 ) {
		xp = Loot_FrenzyXP( 25 + 2 * floorNum + rand() % 6 );
		G_DS_SpawnGems( org, xp, w );
		G_DS_SpawnChest( org, 1 );
		if ( random() < 0.25f * luck * ( 1.0f + G_DS_UpValue( DSU_VACUUM ) ) ) {
			Loot_SpawnVacuum( org );
		}
		return;
	}
	xp = Loot_FrenzyXP( 4 + floorNum / 3 + rand() % 3 );
	G_DS_SpawnGems( org, xp, w );
	r = random();
	if ( r < 0.015f * luck * ( 1.0f + G_DS_UpValue( DSU_VACUUM ) ) ) {
		Loot_SpawnVacuum( org );
	} else if ( random() < 0.01f * luck ) {
		G_DS_SpawnChest( org, 0 );
	}
	// STOPWATCH: every 20 s the next kill freezes time for a moment
	if ( G_RL_HasCard( attacker, RLC_SECRET_FIRST + RLS_STOPWATCH ) && level.time >= lootSt.stopwatchReady ) {
		lootSt.stopwatchReady = level.time + DS_STOPWATCH_MSEC;
		G_SlowMoEx( 0.12f, DS_STOPWATCH_FREEZE, qtrue );
	}
}

/*
===============================================================================

CHESTS: rewards decided here at the floor end, shown by cgame (RLP_CHEST)

===============================================================================
*/

static int Loot_RollRewards( int quality, gentity_t *h ) {
	float	luck = Loot_Luck(), p5, p3, r;
	int		n;

	if ( G_RL_HasCard( h, RLC_SECRET_FIRST + RLS_GOLDENSKULL ) ) {
		return 5;
	}
	switch ( quality ) {
	case 2:		p5 = 0.30f; p3 = 1.00f; break;
	case 1:		p5 = 0.10f; p3 = 0.50f; break;
	default:	p5 = 0.04f; p3 = 0.22f; break;
	}
	r = G_DS_DailyRandom();		// en-daily: the seeded chest stream on a daily floor (random() otherwise)
	if ( r < p5 * luck ) {
		n = 5;
	} else if ( r < ( p5 + p3 ) * luck ) {
		n = 3;
	} else {
		n = 1;
	}
	// TREASURE upgrade: a chance of one more reward
	if ( n < 5 && G_DS_DailyRandom() < G_DS_UpValue( DSU_TREASURE ) ) {
		n++;
	}
	return n;
}

// a weapon level or a passive level for the chest, -1 nothing left
static int Loot_ChestPick( gentity_t *h ) {
	int		w, p, lv, total, r;
	int		weights[DS_NUM_WEAPONS + RLP_NUM];
	int		slots = Loot_PassiveSlots( lootSt.pv );

	total = 0;
	for ( w = 0 ; w < DS_NUM_WEAPONS ; w++ ) {
		weights[w] = 0;
		if ( !DS_WEAPON_LEVELS( w ) || !( lootSt.wused & ( 1 << w ) ) ) {
			continue;
		}
		lv = G_DS_WeaponLevel( h, w );
		if ( lv >= DS_MAX_WLEVEL ) {
			continue;
		}
		weights[w] = 30 + 10 * lv;		// push the weapons you lean on toward their evolution
		total += weights[w];
	}
	for ( p = 0 ; p < RLP_NUM ; p++ ) {
		weights[DS_NUM_WEAPONS + p] = 0;
		if ( lootSt.pv[p] >= DS_MAX_PLEVEL ) {
			continue;
		}
		if ( lootSt.pv[p] > 0 ) {
			weights[DS_NUM_WEAPONS + p] = 26;
		} else if ( slots < DS_PASSIVE_SLOTS ) {
			weights[DS_NUM_WEAPONS + p] = 7;
		}
		total += weights[DS_NUM_WEAPONS + p];
	}
	if ( total <= 0 ) {
		return -1;
	}
	r = G_DS_DailyRand() % total;		// en-daily: seeded on a daily floor
	for ( w = 0 ; w < DS_NUM_WEAPONS + RLP_NUM ; w++ ) {
		r -= weights[w];
		if ( weights[w] && r < 0 ) {
			return w;
		}
	}
	return -1;
}

static int Loot_RewardSort( const void *a, const void *b ) {
	return *(const int *)a - *(const int *)b;
}

static void Loot_OpenChest( gentity_t *h, int quality, lootJackpot_t *jp ) {
	int		i, e, pick, lv, n;

	jp->quality = quality;
	jp->num = 0;
	n = Loot_RollRewards( quality, h );
	// 1: an evolution when one is ready (one per chest)
	for ( e = EVO_NUM - 1 ; e >= 0 ; e-- ) {		// the union first: it consumes HELLFIRE
		if ( G_DS_CanEvolve( h, e ) ) {
			G_DS_Evolve( h, e );
			G_DS_Stat( DSST_EVOS, 1 );
			jp->reward[jp->num++] = DSR_EVO * 1000 + e * 10;
			break;
		}
	}
	// 2: weapon levels and passive levels
	for ( i = jp->num ; i < n ; i++ ) {
		pick = Loot_ChestPick( h );
		if ( pick < 0 ) {
			lv = G_DS_Skulls( 5 + G_RL_Floor() / 2 );		// x the difficulty's reward multiplier (ds-economy)
			G_RL_AddSkulls( lv );
			jp->reward[jp->num++] = DSR_SKULLS * 1000 + lv;
			continue;
		}
		if ( pick < DS_NUM_WEAPONS ) {
			G_DS_AddWeaponLevel( h, pick, 1 );
			lv = G_DS_WeaponLevel( h, pick );
			jp->reward[jp->num++] = DSR_WEAPON * 1000 + pick * 10 + ( lv > 9 ? 9 : lv );
		} else {
			pick -= DS_NUM_WEAPONS;
			Loot_SetPassive( pick, lootSt.pv[pick] + 1 );
			jp->reward[jp->num++] = DSR_PASSIVE * 1000 + pick * 10 + lootSt.pv[pick];
		}
	}
	// shown in order: evolution, weapons, passives (level order inside a kind)
	qsort( jp->reward, jp->num, sizeof( int ), Loot_RewardSort );
	G_DS_Stat( DSST_CHESTS, 1 );
}

/*
=================
G_DS_LootFloorEnd

RL_Finish, before the card offer: a won floor banks every chest still lying around and opens
them all. A lost floor keeps the banked ones for later.
=================
*/
void G_DS_LootFloorEnd( qboolean won ) {
	gentity_t	*h = Loot_Human();
	gentity_t	*e;
	int			i;

	lootSt.numJP = 0;
	lootSt.chestDone = qfalse;
	if ( !G_RL_Active() ) {
		return;
	}
	Loot_LogPower( won ? "won" : "lost" );
	if ( won ) {
		for ( i = MAX_CLIENTS, e = &g_entities[MAX_CLIENTS] ; i < level.num_entities ; i++, e++ ) {
			if ( e->inuse && e->s.eType == ET_GENERAL && e->s.generic1 == RM_ENT_CHEST ) {
				Loot_BankChest( e->s.modelindex2 );
				G_FreeEntity( e );
			}
		}
	}
	if ( won && h && lootSt.numChests > 0 ) {
		for ( i = 0 ; i < lootSt.numChests && lootSt.numJP < DS_MAX_JP ; i++ ) {
			Loot_OpenChest( h, lootSt.chests[i], &lootSt.jp[lootSt.numJP++] );
		}
		// more than fit on the screen: the rest stay for the next floor
		for ( i = lootSt.numJP ; i < lootSt.numChests ; i++ ) {
			lootSt.chests[i - lootSt.numJP] = lootSt.chests[i];
		}
		lootSt.numChests -= lootSt.numJP;
		G_LogPrintf( "Loot: opened %i chests\n", lootSt.numJP );
	}
	lootSt.dirty = qtrue;
	G_DS_LootWriteConfigstring();
}

int G_DS_LootChestsOpened( void ) {
	return G_RL_Active() ? lootSt.numJP : 0;
}

// how long the server waits for the jackpot screen before moving on by itself
int G_DS_LootChestMsec( void ) {
	int		i, t = 1500;

	for ( i = 0 ; i < lootSt.numJP ; i++ ) {
		t += 2600 + lootSt.jp[i].num * 450;
	}
	return t + 3000;
}

qboolean G_DS_LootChestDone( void ) {
	return lootSt.chestDone;
}

/*
===============================================================================

CARD POOL (RL_CardAvailable / RL_CardWeight for the v2 kinds)

===============================================================================
*/

// counts: the kit's card counts (the player's passive counts are its levels, see G_DS_LootFillKit)
qboolean G_DS_LootCardAvailable( int role, const int *counts, int c ) {
	int		p, w, lv, slots, i;
	gentity_t	*h;

	switch ( bg_rlCardKind[c] ) {
	case RLK_STAT:
		return qfalse;		// v2: the passives replaced the stat cards
	case RLK_PASSIVE:
		p = c - RLC_PASSIVE_FIRST;
		if ( counts[c] >= DS_MAX_PLEVEL ) {
			return qfalse;
		}
		if ( counts[c] > 0 ) {
			return qtrue;
		}
		for ( slots = 0, i = 0 ; i < RLP_NUM ; i++ ) {
			if ( counts[RLC_PASSIVE_FIRST + i] > 0 ) {
				slots++;
			}
		}
		return slots < DS_PASSIVE_SLOTS && p >= 0;
	case RLK_WEAPON:
		if ( role != 1 ) {
			return qfalse;		// bots' weapon levels come from their floor (ARSENAL)
		}
		w = RLC_WEAPON_OF( c );
		if ( !DS_WEAPON_LEVELS( w ) || !( lootSt.wused & ( 1 << w ) ) || !( h = Loot_Human() ) ) {
			return qfalse;
		}
		lv = G_DS_WeaponLevel( h, w );
		return lv > 0 && lv < DS_MAX_WLEVEL;
	case RLK_SECRET:
		if ( role != 1 || counts[c] ) {
			return qfalse;
		}
		return ( lootSt.secrets & ( 1 << ( c - RLC_SECRET_FIRST ) ) ) != 0;
	default:
		return qtrue;
	}
}

int G_DS_LootCardWeight( int c, int pickNum ) {
	int		w, lv;
	gentity_t	*h;

	switch ( bg_rlCardKind[c] ) {
	case RLK_PASSIVE:
		return pickNum > 4 ? 6 : 5;
	case RLK_WEAPON:
		w = RLC_WEAPON_OF( c );
		h = Loot_Human();
		lv = h ? G_DS_WeaponLevel( h, w ) : 1;
		return 4 + lv;		// the weapons you've grown come up more
	case RLK_SECRET:
		return 4;
	default:
		return 4;
	}
}

// the player's offer kit: passive card counts = the real levels (chests add levels too)
void G_DS_LootFillKit( int *counts ) {
	int		p;

	for ( p = 0 ; p < RLP_NUM ; p++ ) {
		counts[RLC_PASSIVE_FIRST + p] = lootSt.pv[p];
	}
}

// how many passive levels a bot of this role should hold (about the player's)
int G_DS_LootBotPassiveTarget( int role ) {
	int		p, total = 0;

	for ( p = 0 ; p < RLP_NUM ; p++ ) {
		total += lootSt.pv[p];
	}
	switch ( role ) {
	case 3:		total += 2; break;
	case 4:		total += 4; break;
	default:	total = total * 3 / 4; break;
	}
	return total > DS_PASSIVE_SLOTS * DS_MAX_PLEVEL ? DS_PASSIVE_SLOTS * DS_MAX_PLEVEL : total;
}

/*
=================
G_DS_LootPickCard

RL_Pick: the card the player took. Passives level up, weapon cards add a level.
=================
*/
void G_DS_LootPickCard( gentity_t *ent, int card ) {
	if ( !G_RL_Active() || card < 0 || card >= RLC_NUM ) {
		return;
	}
	if ( RLC_IS_PASSIVE( card ) ) {
		Loot_SetPassive( card - RLC_PASSIVE_FIRST, lootSt.pv[card - RLC_PASSIVE_FIRST] + 1 );
	} else if ( RLC_IS_WEAPON( card ) && ent ) {
		G_DS_AddWeaponLevel( ent, RLC_WEAPON_OF( card ), 1 );
	}
	lootSt.dirty = qtrue;
}

/*
===============================================================================

RUN FILE / CONFIGSTRING

===============================================================================
*/

static const char *Loot_Csv( const int *v, int n, char sep ) {
	static char	bufs[4][256];
	static int	index;
	char		*buf = bufs[index++ & 3];
	int			i;

	buf[0] = 0;
	for ( i = 0 ; i < n ; i++ ) {
		if ( i ) {
			Q_strcat( buf, 256, va( "%c%i", sep, v[i] ) );
		} else {
			Q_strcat( buf, 256, va( "%i", v[i] ) );
		}
	}
	return buf;
}

static int Loot_ParseCsv( const char *s, int *out, int max ) {
	int		n = 0;

	while ( *s && n < max ) {
		while ( *s == ',' || *s == '.' || *s == ' ' ) {
			s++;
		}
		if ( !*s ) {
			break;
		}
		out[n++] = atoi( s );
		while ( *s && *s != ',' && *s != '.' && *s != ' ' ) {
			s++;
		}
	}
	return n;
}

// G_RL_Init, first thing: nothing carried over from the last map
void G_DS_LootReset( void ) {
	memset( &lootSt, 0, sizeof( lootSt ) );
	trap_SetConfigstring( CS_DESCENTGROW, "" );
}

// G_RL_Init, once the run is read
void G_DS_LootInit( void ) {
	int		i, n;

	if ( !G_RL_Active() ) {
		return;
	}
	n = Loot_ParseCsv( G_RL_RunGet( "pv" ), lootSt.pv, RLP_NUM );
	for ( i = 0 ; i < RLP_NUM ; i++ ) {
		if ( i >= n || lootSt.pv[i] < 0 ) {
			lootSt.pv[i] = 0;
		} else if ( lootSt.pv[i] > DS_MAX_PLEVEL ) {
			lootSt.pv[i] = DS_MAX_PLEVEL;
		}
	}
	lootSt.numChests = Loot_ParseCsv( G_RL_RunGet( "chests" ), lootSt.chests, DS_MAX_CHESTS );
	for ( i = 0 ; i < lootSt.numChests ; i++ ) {
		if ( lootSt.chests[i] < 0 || lootSt.chests[i] > 2 ) {
			lootSt.chests[i] = 0;
		}
	}
	lootSt.gemsRun = atoi( G_RL_RunGet( "gems" ) );
	lootSt.wused = atoi( G_RL_RunGet( "wused" ) );
	// QUARTERMASTER (ds-economy): the first floor of every act starts with a chest in the bank, once
	// per act (run key qm_act; the run file is read here before META loads the upgrade ranks)
	if ( atoi( G_RL_RunGet( "up_quarter" ) ) > 0 && !DS_UP_RETIRED( DSU_QUARTER )
		&& ( G_RL_Floor() - 1 ) % DS_FLOORS_PER_ACT == 0 && atoi( G_RL_RunGet( "qm_act" ) ) < DS_ActForFloor( G_RL_Floor() ) ) {
		Loot_BankChest( 0 );
		G_RL_RunSet( "qm_act", va( "%i", DS_ActForFloor( G_RL_Floor() ) ) );
		G_LogPrintf( "Loot: QUARTERMASTER chest for act %i\n", DS_ActForFloor( G_RL_Floor() ) );
	}
	lootSt.secrets = G_RL_MetaGetInt( "secrets", 0 );
	G_DS_ArsenalLoad( G_RL_RunGet( "wl" ), G_RL_RunGet( "wx" ), atoi( G_RL_RunGet( "evo" ) ) );
	lootSt.stopwatchReady = 0;
	lootSt.dirty = qtrue;
	G_DS_LootWriteConfigstring();
}

// RL_WriteRunCommon: the growth goes into the run with everything else
void G_DS_LootSaveRun( void ) {
	char	wl[128], wx[128];
	int		evo = 0;

	if ( !G_RL_Active() ) {
		return;
	}
	G_RL_RunSet( "pv", Loot_Csv( lootSt.pv, RLP_NUM, ',' ) );
	G_RL_RunSet( "chests", Loot_Csv( lootSt.chests, lootSt.numChests, ',' ) );
	G_RL_RunSet( "gems", va( "%i", lootSt.gemsRun ) );
	G_RL_RunSet( "wused", va( "%i", lootSt.wused ) );
	wl[0] = wx[0] = 0;
	G_DS_ArsenalSave( wl, wx, sizeof( wl ), &evo );
	G_RL_RunSet( "wl", wl );
	G_RL_RunSet( "wx", wx );
	G_RL_RunSet( "evo", va( "%i", evo ) );
}

void G_DS_LootWriteConfigstring( void ) {
	static char	s[2048];
	char		jp[512];
	int			i, j, floorNum;

	if ( !G_RL_Active() ) {
		return;
	}
	lootSt.dirty = qfalse;
	lootSt.csTime = level.time;
	s[0] = 0;
	G_DS_ArsenalGrowInfo( s, sizeof( s ) );
	jp[0] = 0;
	for ( i = 0 ; i < lootSt.numJP ; i++ ) {
		Q_strcat( jp, sizeof( jp ), va( "%s%i:", i ? "_" : "", lootSt.jp[i].quality ) );
		for ( j = 0 ; j < lootSt.jp[i].num ; j++ ) {
			Q_strcat( jp, sizeof( jp ), va( j ? ".%i" : "%i", lootSt.jp[i].reward[j] ) );
		}
	}
	floorNum = G_RL_Floor();
	Q_strcat( s, sizeof( s ), va( "\\pv\\%s\\ch\\%s\\gm\\%i\\gr\\%i\\chr\\%i\\act\\%i\\wu\\%i\\vac\\%i\\lk\\%i\\pr\\%i",
		Loot_Csv( lootSt.pv, RLP_NUM, '.' ), lootSt.numChests ? Loot_Csv( lootSt.chests, lootSt.numChests, '.' ) : "-",
		lootSt.gemsFloor, lootSt.gemsRun, atoi( G_RL_RunGet( "character" ) ), DS_ActForFloor( floorNum > 0 ? floorNum : 1 ),
		lootSt.wused, lootSt.vacuums, G_DS_LuckPct(), (int)( DS_PULL_BASE * 100 ) ) );
	if ( jp[0] ) {
		Q_strcat( s, sizeof( s ), va( "\\jp\\%s", jp ) );
	}
	trap_SetConfigstring( CS_DESCENTGROW, s );
}

/*
=================
G_DS_LootFrame

G_RL_RunFrame: regeneration, used weapons, the pentagram, the growth configstring
=================
*/
void G_DS_LootFrame( void ) {
	gentity_t	*e, *h;
	int			i, max, add, msec;

	if ( !G_RL_Active() ) {
		return;
	}
	msec = level.time - level.previousTime;
	if ( msec < 0 || msec > 200 ) {
		msec = 0;
	}
	h = Loot_Human();
	// BLACK BLOOD: regenerate up to the health limit
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		e = &g_entities[i];
		if ( !e->inuse || !e->client || e->health <= 0 || e->client->sess.sessionTeam == TEAM_SPECTATOR
			|| level.intermissiontime ) {
			lootSt.regen[i] = 0;
			continue;
		}
		if ( !G_DS_PassiveLevel( e, RLP_REGEN ) ) {
			continue;
		}
		max = e->client->ps.stats[STAT_MAX_HEALTH];
		if ( e->health >= max ) {
			lootSt.regen[i] = 0;
			continue;
		}
		lootSt.regen[i] += Loot_PV( e, RLP_REGEN ) * msec * 0.001f;
		if ( lootSt.regen[i] >= 1.0f ) {
			add = (int)lootSt.regen[i];
			lootSt.regen[i] -= add;
			e->health += add;
			if ( e->health > max ) {
				e->health = max;
			}
			e->client->ps.stats[STAT_HEALTH] = e->health;
		}
	}
	if ( h && h->client->ps.weaponstate == WEAPON_FIRING && DS_WEAPON_LEVELS( h->client->ps.weapon )
		&& !( lootSt.wused & ( 1 << h->client->ps.weapon ) ) ) {
		lootSt.wused |= 1 << h->client->ps.weapon;
		lootSt.dirty = qtrue;
	}
	if ( lootSt.pentagramAt && level.time >= lootSt.pentagramAt ) {
		lootSt.pentagramAt = 0;
		if ( h && h->health > 0 ) {
			Loot_PentagramBlast( h );
		}
	}
	if ( level.time - lootSt.powerTime >= 1000 || level.time < lootSt.powerTime ) {
		lootSt.powerTime = level.time;
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			lootSt.power[i] = g_entities[i].inuse && g_entities[i].client ? G_DS_PowerScore( &g_entities[i] ) : 1.0f;
		}
		if ( !lootSt.powerLogged && h && level.time - level.startTime > 15000 && !level.intermissiontime ) {
			lootSt.powerLogged = qtrue;
			Loot_LogPower( "start" );
		}
	}
	if ( lootSt.dirty && level.time - lootSt.csTime >= DS_CS_MSEC ) {
		G_DS_LootWriteConfigstring();
	}
}

/*
===============================================================================

COMMANDS

===============================================================================
*/

qboolean G_DS_LootClientCommand( gentity_t *ent, const char *cmd ) {
	if ( !Q_stricmp( cmd, "dschestdone" ) ) {
		if ( G_RL_Active() && !( ent->r.svFlags & SVF_BOT ) ) {
			lootSt.chestDone = qtrue;
		}
		return qtrue;
	}
	return qfalse;
}

// a floor spot in front of the player
static void Loot_FrontSpot( gentity_t *h, float dist, float yawOfs, vec3_t out ) {
	vec3_t	fwd, ang, start, end;
	trace_t	tr;

	VectorSet( ang, 0, h->client->ps.viewangles[YAW] + yawOfs, 0 );
	AngleVectors( ang, fwd, NULL, NULL );
	VectorCopy( h->r.currentOrigin, start );
	start[2] += 16;
	VectorMA( start, dist, fwd, end );
	trap_Trace( &tr, start, NULL, NULL, end, h->s.number, MASK_SOLID );
	VectorMA( tr.endpos, -8, fwd, out );
}

/*
=================
G_DS_LootConsoleCommand

Dev: dsgems [xp] [bursts], dsscatter [n], dschest <q> [bank|here], dsvacuum [now|here], dspassive <p> <lvl>,
	dsfrag [client], dsused <weapon>, dspower, dsloot
=================
*/
qboolean G_DS_LootConsoleCommand( const char *cmd ) {
	char		a1[32], a2[32];
	gentity_t	*h, *spot;
	vec3_t		org;
	int			i, n, xp;

	if ( Q_stricmpn( cmd, "ds", 2 ) ) {
		return qfalse;
	}
	trap_Argv( 1, a1, sizeof( a1 ) );
	trap_Argv( 2, a2, sizeof( a2 ) );
	h = Loot_Human();
	if ( !Q_stricmp( cmd, "dsloot" ) ) {
		G_Printf( "loot: active %i pv %s chests %s gems %i/%i wused 0x%x live gems %i luck %i%% secrets 0x%x\n",
			G_RL_Active(), Loot_Csv( lootSt.pv, RLP_NUM, ',' ), Loot_Csv( lootSt.chests, lootSt.numChests, ',' ), lootSt.gemsFloor, lootSt.gemsRun,
			lootSt.wused, Loot_LiveGems(), G_DS_LuckPct(), lootSt.secrets );
		G_LogPrintf( "Loot: state pv %s chests %s gems %i/%i wused 0x%x live %i ents %i\n", Loot_Csv( lootSt.pv, RLP_NUM, ',' ),
			Loot_Csv( lootSt.chests, lootSt.numChests, ',' ), lootSt.gemsFloor, lootSt.gemsRun, lootSt.wused, Loot_LiveGems(), level.num_entities );
		return qtrue;
	}
	if ( !G_RL_Active() || !h ) {
		if ( !Q_stricmp( cmd, "dsgems" ) || !Q_stricmp( cmd, "dsscatter" ) || !Q_stricmp( cmd, "dschest" )
			|| !Q_stricmp( cmd, "dsvacuum" ) || !Q_stricmp( cmd, "dspassive" ) ) {
			G_Printf( "loot: needs an active descent with the player in\n" );
			return qtrue;
		}
		return qfalse;
	}
	if ( !Q_stricmp( cmd, "dsgems" ) ) {
		xp = a1[0] ? atoi( a1 ) : 40;
		n = a2[0] ? atoi( a2 ) : 1;
		for ( i = 0 ; i < n && i < 30 ; i++ ) {
			Loot_FrontSpot( h, 150 + 70 * ( i % 6 ), ( i % 5 - 2 ) * 9.0f, org );
			G_DS_SpawnGems( org, xp, h->client->ps.weapon );
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsscatter" ) ) {
		// gems all over the map (from the spawn points): the vacuum test
		n = a1[0] ? atoi( a1 ) : 120;
		spot = NULL;
		for ( i = 0 ; i < n ; ) {
			spot = G_Find( spot, FOFS( classname ), "info_player_deathmatch" );
			if ( !spot ) {
				if ( i == 0 ) {
					break;
				}
				continue;
			}
			Loot_SpawnGem( spot->s.origin, ( i % 9 ) == 0 ? 5 : 1, h->client->ps.weapon );
			i++;
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dschest" ) ) {
		if ( !Q_stricmp( a2, "bank" ) ) {
			Loot_BankChest( atoi( a1 ) );
		} else if ( !Q_stricmp( a2, "here" ) ) {
			spot = G_DS_SpawnChest( h->r.currentOrigin, atoi( a1 ) );		// drops on you: the touch test
			if ( spot ) {
				VectorClear( spot->s.pos.trDelta );
			}
		} else {
			Loot_FrontSpot( h, 100, 0, org );
			G_DS_SpawnChest( org, atoi( a1 ) );
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsvacuum" ) ) {
		if ( !Q_stricmp( a1, "now" ) ) {
			G_DS_Vacuum( h );
		} else if ( !Q_stricmp( a1, "here" ) ) {
			spot = Loot_SpawnVacuum( h->r.currentOrigin );
			VectorClear( spot->s.pos.trDelta );
		} else {
			Loot_FrontSpot( h, 120, 0, org );
			Loot_SpawnVacuum( org );
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dspassive" ) ) {
		Loot_SetPassive( atoi( a1 ), atoi( a2 ) );
		h->client->ps.stats[STAT_MAX_HEALTH] = -1;		// re-apply the health limit (HOLLOW HEART)
		G_PerkApplyMaxHealth( h, qfalse );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dspower" ) ) {
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			spot = &g_entities[i];
			if ( !spot->inuse || !spot->client || spot->client->pers.connected != CON_CONNECTED ) {
				continue;
			}
			G_Printf( "%2i %-16s role %i power %.2f match x%.2f\n", i, spot->client->pers.netname, G_RL_ClientRole( spot ),
				G_DS_PowerScore( spot ), ( G_RL_ClientRole( spot ) == 2 || G_RL_ClientRole( spot ) == 3 ) ? Loot_MatchScale( spot ) : 1.0f );
		}
		Loot_LogPower( "dspower" );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsfrag" ) ) {
		// the player frags a bot (the nearest, or client <n>) with the held weapon: the kill drop test
		spot = NULL;
		if ( a1[0] ) {
			i = atoi( a1 );
			if ( i >= 0 && i < level.maxclients && g_entities[i].inuse && g_entities[i].client ) {
				spot = &g_entities[i];
			}
		} else {
			float	d, best = 0;

			for ( i = 0 ; i < level.maxclients ; i++ ) {
				if ( i == h->s.number || !g_entities[i].inuse || !g_entities[i].client || g_entities[i].health <= 0 ) {
					continue;
				}
				d = Distance( g_entities[i].r.currentOrigin, h->r.currentOrigin );
				if ( !spot || d < best ) {
					spot = &g_entities[i];
					best = d;
				}
			}
		}
		if ( spot && spot != h && spot->health > 0 ) {
			G_Damage( spot, h, h, NULL, NULL, spot->health + 200, DAMAGE_NO_ARMOR | DAMAGE_NO_PROTECTION, MOD_ROCKET );
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsused" ) ) {
		lootSt.wused |= 1 << atoi( a1 );
		lootSt.dirty = qtrue;
		return qtrue;
	}
	return qfalse;
}
