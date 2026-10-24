/*
===========================================================================
REARMED -- DESCENT round 3 (ds-frenzy, game side): the dopamine layer of a fight.

KILL-CHAIN FRENZY: the human's kills within a decaying window build a chain (headshots, gibs,
the style rank and elites stretch the window). Tiers at 3 / 6 / 10 / 15 kills raise the gem
xp of every kill, pull gems in from further and faster; tier IV starts BERSERK. When the
chain breaks it cashes out: a little bonus xp for the held weapon and a tally at the side.

LUCKY CRITS: a small chance per hit (luck, the chain tier and BERSERK raise it) for a short
damage burst on that hit: x1.5, headshot crits x2. Bots crit too, less often. A crit never
turns a non-lethal hit into a kill on a target above 90% health (no crit one-shots).

POWER SPIKES: 5-8 s bursts for the human only, allowed past the single-digit rule because
they are short: BERSERK (+40% damage, 30% shorter refire, 15% lifesteal) and OVERDRIVE (+30%
speed, a dash that is always ready, the zip line refreshes in the air). A slow-mo kick at the
start. G_DS_PowerScore never sees them, so enemies are not buffed for the player's moment.

LOOT BEAMS: coloured pillars of light on rare drops (chests by quality, the vacuum, spike
pickups, red gems, the other workers' legendaries): a broadcast ET_GENERAL helper entity
(RM_ENT_BEAM) follows the drop, so cgame can draw it through walls a little.

Network: server commands to the human ("dsfz", "dsfzend", "dsfzspk"), EV_DS_FRENZY events
(crit glint), HIT_CRIT on the hit confirm. cgame: cgame/cg_ds_frenzy.c.
===========================================================================
*/
#include "g_local.h"

// frenzy
#define FZ_WINDOW_BASE		4200	// msec after a kill to make the next one ...
#define FZ_WINDOW_TIER		200		// ... less per tier reached (the peak is a sprint)
#define FZ_WINDOW_MIN		2600
#define FZ_WINDOW_MAX		7000
#define FZ_HEADSHOT_EXT		1000	// a headshot kill adds this to the window
#define FZ_GIB_EXT			700
#define FZ_ELITE_EXT		1500
#define FZ_BOSS_EXT			2500
#define FZ_STYLE_EXT		110		// per style rank (D..SSS)
#define FZ_HITHEAD_EXT		250		// a headshot hit (no kill) tops the timer up a little
#define FZ_BERSERK_EVERY	15		// BERSERK at 15, 30, 45 ... kills in one chain

static const float fz_gemMult[DS_FRENZY_TIERS + 1] = { 1.0f, 1.3f, 1.65f, 2.1f, 2.6f };
static const float fz_pullMult[DS_FRENZY_TIERS + 1] = { 1.0f, 1.15f, 1.3f, 1.5f, 1.75f };
static const float fz_flightMult[DS_FRENZY_TIERS + 1] = { 1.0f, 0.88f, 0.76f, 0.64f, 0.55f };

// crits
#define CRIT_BASE_PCT		4.0f	// the human's chance per hit
#define CRIT_LUCK_SCALE		0.15f	// + 0.15% per luck percent (RABBIT'S FOOT 5 = +40% luck = +6%)
#define CRIT_TIER_PCT		1.0f	// + per frenzy tier
#define CRIT_BERSERK_PCT	8.0f
#define CRIT_MAX_PCT		25.0f
#define CRIT_BOT_PCT		2.0f	// bots (elites / bosses a little more)
#define CRIT_ELITE_PCT		3.0f
#define CRIT_MULT			1.5f
#define CRIT_HEAD_MULT		2.0f
#define CRIT_SAFE_HEALTH	0.9f	// above this share of max health a crit can't be what kills

// spikes
#define SPIKE_BERSERK_MSEC	6000
#define SPIKE_OVERDRIVE_MSEC	7000
#define SPIKE_MAX_MSEC		12000	// extensions cap (ds-cards TIME THIEF)
#define BERSERK_DMG			1.4f
#define BERSERK_REFIRE		70		// refire percent
#define BERSERK_LEECH		0.15f
#define OVERDRIVE_SPEED		1.3f
#define SPIKE_PICKUP_LIFE	30000
#define SPIKE_ELITE_CHANCE	0.15f
#define SPIKE_BOSS_CHANCE	0.20f	// per boss life taken by the player
#define SPIKE_LUCKY_CHANCE	0.006f	// any other kill (x luck)

// beams
#define BEAM_MAX			24
#define BEAM_MAX_COMMON		6
#define BEAM_THINK			50

typedef struct {
	// switches (archived: tests must pass them)
	vmCvar_t	cvFrenzy, cvCrits, cvSpikes, cvBeams;

	// the human's chain
	int			count;
	int			tier;
	int			expire;			// level.time the chain breaks
	int			window;
	int			chainXP;		// extra gem xp in this chain
	int			best;			// best chain this floor
	int			floorXP;		// frenzy xp this floor (gem bonus + cash-outs)
	int			chains;			// chains cashed out this floor (>= 3 kills)
	int			berserks;		// berserk triggers this chain

	// spikes (the human only)
	int			spike;			// DS_SPIKE_
	int			spikeStart, spikeEnd;
	int			spikeCheck;		// level.time of the post-spike sanity log, 0 none
	int			spikeMid;		// level.time of the during-the-spike log, 0 done
	int			spikeLastKind;
	int			preSpeed, preFire;	// before the spike (sanity log)

	// crits
	qboolean	forceCrit;
	int			critFrame[MAX_CLIENTS];	// level.time of the last glint event per attacker
	int			hits[2], crits[2];		// [0] human, [1] bots
	int			lastHumanCrit;
	int			lastCritBase, lastCritRaw, lastCritDmg;	// the last crit: before, x mult, after the clamp

	int			human;			// G_RL_HumanClient at the last frame
	qboolean	floorOver;
} frenzyState_t;

static frenzyState_t	fz;

static void FZ_SpikeEnd( const char *why );

static int FZ_Debug( void ) {
	return trap_Cvar_VariableIntegerValue( "g_debugAlt" );
}

/*
===============================================================================

HELPERS

===============================================================================
*/

static qboolean FZ_On( vmCvar_t *cv ) {
	return G_RL_Active() && cv->integer != 0;
}

static gentity_t *FZ_Human( void ) {
	int		h = G_RL_HumanClient();

	if ( h < 0 || h >= MAX_CLIENTS || !g_entities[h].inuse || !g_entities[h].client ) {
		return NULL;
	}
	return &g_entities[h];
}

static qboolean FZ_IsHuman( gentity_t *ent ) {
	return ent && ent->client && ent - g_entities == G_RL_HumanClient() && !( ent->r.svFlags & SVF_BOT );
}

static int FZ_StyleRank( gentity_t *ent ) {
	static const int	th[7] = { STYLE_RANK_D, STYLE_RANK_C, STYLE_RANK_B, STYLE_RANK_A, STYLE_RANK_S, STYLE_RANK_SS, STYLE_RANK_SSS };
	int		r, s;

	if ( !ent || !ent->client ) {
		return 0;
	}
	s = ent->client->ps.stats[STAT_STYLE];
	for ( r = 7 ; r > 0 ; r-- ) {
		if ( s >= th[r - 1] ) {
			return r;
		}
	}
	return 0;
}

static int FZ_TierFor( int count ) {
	int		t;

	for ( t = DS_FRENZY_TIERS ; t > 0 ; t-- ) {
		if ( count >= ds_frenzyTierKills[t] ) {
			return t;
		}
	}
	return 0;
}

static void FZ_Send( const char *s ) {
	gentity_t	*h = FZ_Human();

	if ( h ) {
		trap_SendServerCommand( h - g_entities, s );
	}
}

static void FZ_SendChain( void ) {
	FZ_Send( va( "dsfz %i %i %i %i %i", fz.count, fz.tier, fz.expire, fz.window, (int)( fz_gemMult[fz.tier] * 100 + 0.5f ) ) );
}

/*
===============================================================================

FRENZY

===============================================================================
*/

int G_DS_FrenzyTier( void ) {
	return FZ_On( &fz.cvFrenzy ) ? fz.tier : 0;
}

float G_DS_FrenzyGemMult( void ) {
	return FZ_On( &fz.cvFrenzy ) && fz.count > 0 ? fz_gemMult[fz.tier] : 1.0f;
}

void G_DS_FrenzyGemXP( int extra ) {
	if ( extra > 0 ) {
		fz.chainXP += extra;
		fz.floorXP += extra;
	}
}

float G_DS_FrenzyPullScale( void ) {
	return FZ_On( &fz.cvFrenzy ) && fz.count > 0 ? fz_pullMult[fz.tier] : 1.0f;
}

float G_DS_FrenzyFlightScale( void ) {
	return FZ_On( &fz.cvFrenzy ) && fz.count > 0 ? fz_flightMult[fz.tier] : 1.0f;
}

int G_DS_FrenzyBestChain( void ) {
	return G_RL_Active() && fz.cvFrenzy.integer ? fz.best : 0;
}

int G_DS_FrenzyChainXP( void ) {
	return G_RL_Active() && fz.cvFrenzy.integer ? fz.floorXP : 0;
}

/*
=================
FZ_CashOut

The chain broke (timeout, death, floor end): a bonus for the held weapon and the side tally
=================
*/
static void FZ_CashOut( const char *why ) {
	gentity_t	*h = FZ_Human();
	int			bonus = 0, w;

	if ( fz.count <= 0 ) {
		return;
	}
	if ( fz.count >= ds_frenzyTierKills[1] ) {
		bonus = 3 * fz.count + ( fz.count >= 10 ? 2 * ( fz.count - 9 ) * 3 : 0 );
		bonus = (int)( bonus * G_DS_RewardMult() + 0.5f );
		if ( h && h->health > 0 ) {
			w = h->client->ps.weapon;
			if ( DS_WEAPON_LEVELS( w ) ) {
				G_DS_AddWeaponXP( h, w, bonus );
			}
		}
		fz.floorXP += bonus;
		fz.chains++;
		FZ_Send( va( "dsfzend %i %i %i %i", fz.count, fz.chainXP + bonus, fz.best, fz.tier ) );
	} else {
		FZ_Send( "dsfzend 0 0 0 0" );
	}
	G_LogPrintf( "Frenzy: chain %i cashed out (%s) tier %i gemxp %i bonus %i best %i\n", fz.count, why, fz.tier,
		fz.chainXP, bonus, fz.best );
	fz.count = 0;
	fz.tier = 0;
	fz.expire = 0;
	fz.chainXP = 0;
	fz.berserks = 0;
}

static void FZ_SetCount( int count, int window ) {
	int		oldTier = fz.tier, t;

	fz.count = count;
	fz.window = window;
	fz.expire = level.time + window;
	fz.tier = FZ_TierFor( count );
	if ( fz.count > fz.best ) {
		fz.best = fz.count;
	}
	for ( t = oldTier + 1 ; t <= fz.tier ; t++ ) {
		G_LogPrintf( "Frenzy: tier %i (%s) at chain %i window %i gem x%.2f pull x%.2f flight x%.2f\n", t, ds_frenzyTierNames[t],
			fz.count, window, fz_gemMult[t], fz_pullMult[t], fz_flightMult[t] );
	}
	FZ_SendChain();
	// the top tier: BERSERK (again every 15 more kills in the same chain)
	if ( fz.count >= FZ_BERSERK_EVERY && fz.count / FZ_BERSERK_EVERY > fz.berserks ) {
		fz.berserks = fz.count / FZ_BERSERK_EVERY;
		G_DS_GrantSpike( FZ_Human(), DS_SPIKE_BERSERK );
	}
}

/*
=================
G_DS_FrenzyKill

player_die, before the gem drop: a kill by the human grows the chain; the human's own death
breaks it (and ends a spike); kills drop spike pickups now and then
=================
*/
void G_DS_FrenzyKill( gentity_t *self, gentity_t *attacker, int mod ) {
	int			window, role, hs, gib, r;
	float		chance;
	gentity_t	*h;

	if ( !G_RL_Active() || !self || !self->client || level.warmupTime || level.intermissiontime || level.intermissionQueued ) {
		return;
	}
	h = FZ_Human();
	if ( self == h ) {
		if ( fz.count > 0 ) {
			FZ_CashOut( "death" );
		}
		if ( fz.spike ) {
			FZ_SpikeEnd( "death" );
		}
		return;
	}
	if ( !attacker || attacker != h || !FZ_IsHuman( attacker ) || OnSameTeam( self, attacker ) ) {
		return;
	}
	role = G_RL_ClientRole( self );

	if ( fz.cvFrenzy.integer ) {
		hs = attacker->client->hitTarget == self->s.number && ( attacker->client->hitFlags & HIT_HEADSHOT );
		gib = self->health <= GIB_HEALTH;
		r = FZ_StyleRank( attacker );
		window = FZ_WINDOW_BASE - FZ_WINDOW_TIER * FZ_TierFor( fz.count + 1 ) + r * FZ_STYLE_EXT;
		if ( hs ) {
			window += FZ_HEADSHOT_EXT;
		}
		if ( gib ) {
			window += FZ_GIB_EXT;
		}
		if ( role == 3 ) {
			window += FZ_ELITE_EXT;
		} else if ( role == 4 ) {
			window += FZ_BOSS_EXT;
		}
		if ( window < FZ_WINDOW_MIN ) {
			window = FZ_WINDOW_MIN;
		} else if ( window > FZ_WINDOW_MAX ) {
			window = FZ_WINDOW_MAX;
		}
		FZ_SetCount( fz.count + 1, window );
		if ( FZ_Debug() || fz.count <= 2 ) {
			G_LogPrintf( "Frenzy: kill chain %i window %i (hs %i gib %i rank %i role %i)\n", fz.count, window, hs, gib, r, role );
		}
	}

	// spike pickups: elites, bosses (each life), a lucky kill
	if ( fz.cvSpikes.integer && !fz.spike ) {
		chance = role == 3 ? SPIKE_ELITE_CHANCE : role == 4 ? SPIKE_BOSS_CHANCE : SPIKE_LUCKY_CHANCE;
		chance *= 1.0f + G_DS_LuckPct() * 0.01f;
		if ( random() < chance ) {
			G_DS_SpawnPowerSpike( self->r.currentOrigin, DS_SPIKE_OVERDRIVE );
		}
	}
	(void)mod;
}

/*
===============================================================================

POWER SPIKES

===============================================================================
*/

int G_DS_SpikeActive( gentity_t *ent ) {
	if ( !G_RL_Active() || !fz.spike || !ent || ent != FZ_Human() ) {
		return DS_SPIKE_NONE;
	}
	return fz.spike;
}

static void FZ_SpikeEnd( const char *why ) {
	gentity_t	*h = FZ_Human();

	if ( !fz.spike ) {
		return;
	}
	G_LogPrintf( "Frenzy: spike %s ended (%s) after %i ms\n", fz.spike == DS_SPIKE_BERSERK ? "berserk" : "overdrive", why,
		level.time - fz.spikeStart );
	fz.spikeLastKind = fz.spike;
	fz.spike = DS_SPIKE_NONE;
	fz.spikeEnd = 0;
	fz.spikeCheck = h ? level.time + 600 : 0;
	FZ_Send( "dsfzspk 0 0 0" );
}

void G_DS_GrantSpike( gentity_t *player, int kind ) {
	gentity_t	*te;
	int			msec;

	if ( !FZ_On( &fz.cvSpikes ) || !player || player != FZ_Human() || player->health <= 0
		|| kind <= DS_SPIKE_NONE || kind >= DS_SPIKE_NUM || level.intermissiontime || level.intermissionQueued ) {
		return;
	}
	msec = kind == DS_SPIKE_BERSERK ? SPIKE_BERSERK_MSEC : SPIKE_OVERDRIVE_MSEC;
	if ( fz.spike == kind ) {
		// the same again: a fresh burst from now (never longer than one)
		fz.spikeStart = level.time;
		fz.spikeEnd = level.time + msec;
		fz.spikeMid = level.time + 500;
		G_LogPrintf( "Frenzy: spike %s restarted %i ms\n", kind == DS_SPIKE_BERSERK ? "berserk" : "overdrive", msec );
	} else {
		if ( fz.spike ) {
			FZ_SpikeEnd( "replaced" );
		}
		fz.preSpeed = player->client->ps.speed;
		fz.preFire = player->client->ps.stats[STAT_DSFIRE] & DSF_RATE;
		fz.spike = kind;
		fz.spikeStart = level.time;
		fz.spikeEnd = level.time + msec;
		fz.spikeCheck = 0;
		fz.spikeMid = level.time + 500;
		G_SlowMoEx( 0.35f, 420, qtrue );		// the kick (one per spike)
		G_LogPrintf( "Frenzy: spike %s start %i ms (speed %i refire %i%% health %i)\n", kind == DS_SPIKE_BERSERK ? "berserk" : "overdrive",
			msec, fz.preSpeed, fz.preFire, player->health );
	}
	te = G_TempEntity( player->r.currentOrigin, EV_DS_FRENZY );
	te->s.eventParm = DSFZ_SPIKE;
	te->s.otherEntityNum = player->s.number;
	te->s.generic1 = kind;
	FZ_Send( va( "dsfzspk %i %i %i", fz.spike, fz.spikeEnd, fz.spikeEnd - fz.spikeStart ) );
}

int G_DS_FrenzySpikeExtend( gentity_t *ent, int msec ) {
	int		end, add;

	if ( !G_RL_Active() || !fz.spike || msec <= 0 || !ent || ent != FZ_Human() ) {
		return 0;
	}
	end = fz.spikeEnd + msec;
	if ( end > fz.spikeStart + SPIKE_MAX_MSEC ) {
		end = fz.spikeStart + SPIKE_MAX_MSEC;
	}
	add = end - fz.spikeEnd;
	if ( add <= 0 ) {
		return 0;
	}
	fz.spikeEnd = end;
	FZ_Send( va( "dsfzspk %i %i %i", fz.spike, fz.spikeEnd, fz.spikeEnd - fz.spikeStart ) );
	return add;
}

// G_RL_GateClient, after everything else: OVERDRIVE
void G_DS_FrenzyGate( gentity_t *ent ) {
	playerState_t	*ps;

	if ( fz.spike != DS_SPIKE_OVERDRIVE || ent != FZ_Human() ) {
		return;
	}
	ps = &ent->client->ps;
	ps->speed = (int)( ps->speed * OVERDRIVE_SPEED );
	if ( G_RearmMoveFlags() & RMF_DASH ) {
		ps->stats[STAT_RMFLAGS] |= RMF_DASH;			// the dash, even without the card
		ps->ammo[AMMO_RMFLAGS2] &= ~RMF2_DASH1;
	}
	ps->stats[STAT_DASH_TIME] = 0;						// always ready
	if ( !( ps->loopSound & RMZ_ZIPPING ) ) {
		ps->loopSound &= ~RMZ_USED;						// the zip line refreshes in the air
	}
}

// G_DS_ArsenalThink: BERSERK refire
int G_DS_FrenzyFirePct( gentity_t *ent, int pct ) {
	if ( fz.spike != DS_SPIKE_BERSERK || ent != FZ_Human() ) {
		return pct;
	}
	pct = pct * BERSERK_REFIRE / 100;
	return pct < 25 ? 25 : pct;
}

static void FZ_SpikeFrame( void ) {
	gentity_t	*h = FZ_Human();

	if ( fz.spike ) {
		if ( !fz.cvSpikes.integer ) {
			FZ_SpikeEnd( "switched off" );
		} else if ( !h || h->health <= 0 ) {
			FZ_SpikeEnd( "death" );
		} else if ( level.time >= fz.spikeEnd ) {
			FZ_SpikeEnd( "time" );
		}
	}
	// evidence: the spike really applies (speed / refire / dash), then that it is gone
	if ( fz.spike && fz.spikeMid && level.time >= fz.spikeMid && h ) {
		fz.spikeMid = 0;
		G_LogPrintf( "Frenzy: during %s speed %i refire %i%% dash %i dashTime %i health %i\n",
			fz.spike == DS_SPIKE_BERSERK ? "berserk" : "overdrive", h->client->ps.speed, h->client->ps.stats[STAT_DSFIRE] & DSF_RATE,
			( h->client->ps.stats[STAT_RMFLAGS] & RMF_DASH ) != 0, h->client->ps.stats[STAT_DASH_TIME], h->health );
	}
	// the sanity log: everything back to normal
	if ( fz.spikeCheck && level.time >= fz.spikeCheck ) {
		fz.spikeCheck = 0;
		if ( h ) {
			G_LogPrintf( "Frenzy: after %s speed %i (before %i) refire %i%% (before %i%%) dash %i dashTime %i\n",
				fz.spikeLastKind == DS_SPIKE_BERSERK ? "berserk" : "overdrive", h->client->ps.speed, fz.preSpeed,
				h->client->ps.stats[STAT_DSFIRE] & DSF_RATE, fz.preFire, ( h->client->ps.stats[STAT_RMFLAGS] & RMF_DASH ) != 0,
				h->client->ps.stats[STAT_DASH_TIME] );
		}
	}
}

/*
===============================================================================

SPIKE PICKUP

===============================================================================
*/

static int FZ_CountMarker( int marker ) {
	int			i, n = 0;
	gentity_t	*e;

	for ( i = MAX_CLIENTS, e = &g_entities[MAX_CLIENTS] ; i < level.num_entities ; i++, e++ ) {
		if ( e->inuse && e->s.eType == ET_GENERAL && e->s.generic1 == marker ) {
			n++;
		}
	}
	return n;
}

static void FZ_PickupThink( gentity_t *ent ) {
	gentity_t	*h = FZ_Human();
	trace_t		tr;
	vec3_t		pos;
	float		dx, dy, dz;

	ent->nextthink = level.time + 1;
	if ( level.time >= ent->s.time2 || level.intermissiontime || !G_RL_Active() ) {
		G_FreeEntity( ent );
		return;
	}
	if ( ent->s.pos.trType == TR_GRAVITY ) {
		BG_EvaluateTrajectory( &ent->s.pos, level.time, pos );
		trap_Trace( &tr, ent->r.currentOrigin, ent->r.mins, ent->r.maxs, pos, ENTITYNUM_NONE, MASK_SOLID );
		if ( tr.startsolid || tr.allsolid || ( tr.fraction < 1.0f && tr.plane.normal[2] > 0.6f ) ) {
			G_SetOrigin( ent, tr.fraction < 1.0f && !tr.startsolid ? tr.endpos : ent->r.currentOrigin );
		} else if ( tr.fraction < 1.0f ) {
			// off a wall: drop straight down from here
			VectorCopy( tr.endpos, ent->s.pos.trBase );
			VectorClear( ent->s.pos.trDelta );
			ent->s.pos.trTime = level.time;
			VectorCopy( tr.endpos, ent->r.currentOrigin );
		} else {
			VectorCopy( pos, ent->r.currentOrigin );
		}
		if ( ent->s.pos.trType == TR_GRAVITY && level.time - ent->s.time > 5000 ) {
			G_FreeEntity( ent );		// the void
			return;
		}
		if ( trap_PointContents( ent->r.currentOrigin, -1 ) & CONTENTS_NODROP ) {
			G_FreeEntity( ent );
			return;
		}
		trap_LinkEntity( ent );
	}
	if ( !h || h->health <= 0 || level.time - ent->s.time < 400 ) {
		return;
	}
	dx = ent->r.currentOrigin[0] - h->r.currentOrigin[0];
	dy = ent->r.currentOrigin[1] - h->r.currentOrigin[1];
	dz = ent->r.currentOrigin[2] - h->r.currentOrigin[2];
	if ( dx * dx + dy * dy < 48 * 48 && dz > -64 && dz < 48 ) {
		G_LogPrintf( "Frenzy: spike pickup %i taken\n", ent->s.modelindex2 );
		G_DS_GrantSpike( h, ent->s.modelindex2 );
		G_FreeEntity( ent );
	}
}

gentity_t *G_DS_SpawnPowerSpike( vec3_t origin, int kind ) {
	gentity_t	*ent;
	float		a;

	if ( !FZ_On( &fz.cvSpikes ) || kind <= DS_SPIKE_NONE || kind >= DS_SPIKE_NUM || FZ_CountMarker( RM_ENT_SPIKE ) >= 2 ) {
		return NULL;
	}
	ent = G_Spawn();
	ent->classname = "ds_spike";
	ent->s.eType = ET_GENERAL;
	ent->s.generic1 = RM_ENT_SPIKE;
	ent->s.modelindex2 = kind;
	ent->r.contents = 0;
	VectorSet( ent->r.mins, -6, -6, 0 );
	VectorSet( ent->r.maxs, 6, 6, 12 );
	G_SetOrigin( ent, origin );
	ent->r.currentOrigin[2] += 16;
	a = random() * 2 * M_PI;
	ent->s.pos.trType = TR_GRAVITY;
	ent->s.pos.trTime = level.time;
	VectorCopy( ent->r.currentOrigin, ent->s.pos.trBase );
	ent->s.pos.trDelta[0] = cos( a ) * 70;
	ent->s.pos.trDelta[1] = sin( a ) * 70;
	ent->s.pos.trDelta[2] = 300;
	ent->s.time = level.time;
	ent->s.time2 = level.time + SPIKE_PICKUP_LIFE;
	ent->think = FZ_PickupThink;
	ent->nextthink = level.time + 1;
	trap_LinkEntity( ent );
	G_DS_LootBeam( ent, DS_BEAM_LEGENDARY );
	G_LogPrintf( "Frenzy: spike pickup %i dropped\n", kind );
	return ent;
}

/*
===============================================================================

CRITS (and BERSERK damage / lifesteal)

===============================================================================
*/

// a hit that can crit: the player's weapons, not fire / falls / the world
static qboolean FZ_CritMod( int mod ) {
	if ( mod == MOD_THUNDER ) {
		return qfalse;
	}
	return G_DS_WeaponForMod( mod ) > WP_NONE;
}

static float FZ_CritChance( gentity_t *attacker ) {
	float	pct;
	int		role;

	if ( FZ_IsHuman( attacker ) ) {
		pct = CRIT_BASE_PCT + G_DS_LuckPct() * CRIT_LUCK_SCALE;
		if ( fz.cvFrenzy.integer ) {
			pct += fz.tier * CRIT_TIER_PCT;
		}
		if ( fz.spike == DS_SPIKE_BERSERK ) {
			pct += CRIT_BERSERK_PCT;
		}
		return ( pct > CRIT_MAX_PCT ? CRIT_MAX_PCT : pct ) * 0.01f;
	}
	role = G_RL_ClientRole( attacker );
	return ( role == 3 || role == 4 ? CRIT_ELITE_PCT : CRIT_BOT_PCT ) * 0.01f;
}

int G_DS_FrenzyDamage( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, int damage, int mod, int *hitFlags, vec3_t point ) {
	gclient_t	*cl;
	gentity_t	*te;
	int			n, max, base, heal;
	float		m;
	qboolean	human, headshot;
	vec3_t		org;

	if ( !G_RL_Active() || !targ || !targ->client || !attacker || !attacker->client || attacker == targ
		|| targ->health <= 0 || damage <= 0 || OnSameTeam( targ, attacker ) || level.intermissiontime ) {
		return damage;
	}
	human = FZ_IsHuman( attacker );
	headshot = hitFlags && ( *hitFlags & HIT_HEADSHOT );
	n = attacker - g_entities;

	// BERSERK: the human's burst
	if ( human && fz.spike == DS_SPIKE_BERSERK ) {
		damage = (int)( damage * BERSERK_DMG + 0.5f );
		heal = (int)( ( damage < targ->health ? damage : targ->health ) * BERSERK_LEECH + 0.5f );
		max = attacker->client->ps.stats[STAT_MAX_HEALTH];
		if ( heal > 0 && attacker->health > 0 && attacker->health < max ) {
			attacker->health += heal;
			if ( attacker->health > max ) {
				attacker->health = max;
			}
			attacker->client->ps.stats[STAT_HEALTH] = attacker->health;
		}
	}
	// a headshot keeps the chain warm
	if ( human && headshot && fz.count > 0 && fz.cvFrenzy.integer ) {
		fz.expire += FZ_HITHEAD_EXT;
		if ( fz.expire > level.time + FZ_WINDOW_MAX ) {
			fz.expire = level.time + FZ_WINDOW_MAX;
		}
	}

	if ( !fz.cvCrits.integer || !FZ_CritMod( mod ) ) {
		return damage;
	}
	fz.hits[human ? 0 : 1]++;
	if ( !( human && fz.forceCrit ) && random() >= FZ_CritChance( attacker ) ) {
		return damage;
	}
	fz.forceCrit = qfalse;
	fz.crits[human ? 0 : 1]++;
	base = damage;
	m = headshot ? CRIT_HEAD_MULT : CRIT_MULT;
	damage = (int)( damage * m + 0.5f );
	fz.lastCritRaw = damage;
	// fair: a crit never decides a fight against a healthy target on its own
	max = targ->client->ps.stats[STAT_MAX_HEALTH];
	if ( max > 0 && targ->health >= max * CRIT_SAFE_HEALTH && base < targ->health && damage >= targ->health ) {
		damage = targ->health - 1;
		if ( damage < base ) {
			damage = base;
		}
	}
	fz.lastCritBase = base;
	fz.lastCritDmg = damage;
	if ( hitFlags ) {
		*hitFlags |= HIT_CRIT;
	}
	if ( human ) {
		fz.lastHumanCrit = level.time;
	}
	cl = attacker->client;
	// the glint: one per attacker per server frame (shotgun pellets)
	if ( fz.critFrame[n] != level.time ) {
		fz.critFrame[n] = level.time;
		if ( point ) {
			VectorCopy( point, org );
		} else {
			VectorCopy( targ->r.currentOrigin, org );
			org[2] += 24;
		}
		te = G_TempEntity( org, EV_DS_FRENZY );
		te->s.eventParm = DSFZ_CRIT;
		te->s.otherEntityNum = attacker->s.number;
		te->s.otherEntityNum2 = targ->s.number;
		te->s.generic1 = headshot ? 1 : 0;
		if ( human ) {
			te->r.svFlags |= SVF_BROADCAST;		// always reaches the player who made it
		}
	}
	if ( FZ_Debug() ) {
		G_LogPrintf( "Frenzy: crit %i -> %i mod %i %i -> %i%s\n", n, targ->s.number, mod, base, damage, headshot ? " head" : "" );
	}
	(void)inflictor;
	(void)cl;
	return damage;
}

/*
===============================================================================

LOOT BEAMS

===============================================================================
*/

static gentity_t *FZ_BeamFor( gentity_t *target ) {
	int			i;
	gentity_t	*e;

	for ( i = MAX_CLIENTS, e = &g_entities[MAX_CLIENTS] ; i < level.num_entities ; i++, e++ ) {
		if ( e->inuse && e->s.eType == ET_GENERAL && e->s.generic1 == RM_ENT_BEAM && e->enemy == target ) {
			return e;
		}
	}
	return NULL;
}

// room for one more beam of this rarity: the oldest weaker (or equal) one makes way
static qboolean FZ_BeamRoom( int rarity ) {
	int			i, n = 0, nCommon = 0;
	gentity_t	*e, *victim = NULL;

	for ( i = MAX_CLIENTS, e = &g_entities[MAX_CLIENTS] ; i < level.num_entities ; i++, e++ ) {
		if ( !e->inuse || e->s.eType != ET_GENERAL || e->s.generic1 != RM_ENT_BEAM ) {
			continue;
		}
		n++;
		if ( e->s.modelindex2 == DS_BEAM_COMMON ) {
			nCommon++;
		}
		if ( e->s.modelindex2 <= rarity && ( !victim || e->s.modelindex2 < victim->s.modelindex2
			|| ( e->s.modelindex2 == victim->s.modelindex2 && e->s.time < victim->s.time ) ) ) {
			victim = e;
		}
	}
	if ( rarity == DS_BEAM_COMMON && nCommon >= BEAM_MAX_COMMON ) {
		return qfalse;		// red gems: a few at a time is enough
	}
	if ( n < BEAM_MAX ) {
		return qtrue;
	}
	if ( !victim ) {
		return qfalse;
	}
	G_FreeEntity( victim );
	return qtrue;
}

static void FZ_BeamThink( gentity_t *ent ) {
	gentity_t	*t = ent->enemy;

	ent->nextthink = level.time + BEAM_THINK;
	if ( !G_RL_Active() || !fz.cvBeams.integer || ( ent->s.time2 && level.time >= ent->s.time2 ) ) {
		G_FreeEntity( ent );
		return;
	}
	if ( t ) {
		// the drop is gone (picked up, freed, its slot reused), or a gem is on its way in
		if ( !t->inuse || t->s.eType != ent->count || t->s.generic1 != ent->health
			|| ( t->s.eType == ET_GENERAL && t->s.generic1 == RM_ENT_GEM && t->s.time ) ) {
			G_FreeEntity( ent );
			return;
		}
		if ( DistanceSquared( t->r.currentOrigin, ent->r.currentOrigin ) > 1.0f ) {
			G_SetOrigin( ent, t->r.currentOrigin );
			trap_LinkEntity( ent );
		}
	}
}

static gentity_t *FZ_SpawnBeam( vec3_t origin, int rarity ) {
	gentity_t	*ent;

	ent = G_Spawn();
	ent->classname = "ds_beam";
	ent->s.eType = ET_GENERAL;
	ent->s.generic1 = RM_ENT_BEAM;
	ent->s.modelindex2 = rarity;
	ent->s.otherEntityNum = ENTITYNUM_NONE;
	ent->s.time = level.time;
	ent->s.time2 = 0;
	ent->r.contents = 0;
	ent->r.svFlags = SVF_BROADCAST;		// a beam is seen (a little) through the level
	G_SetOrigin( ent, origin );
	ent->think = FZ_BeamThink;
	ent->nextthink = level.time + BEAM_THINK;
	trap_LinkEntity( ent );
	return ent;
}

void G_DS_LootBeam( gentity_t *target, int rarity ) {
	gentity_t	*ent;

	if ( !FZ_On( &fz.cvBeams ) || !target || !target->inuse ) {
		return;
	}
	rarity = rarity < 0 ? 0 : rarity >= DS_BEAM_NUM ? DS_BEAM_NUM - 1 : rarity;
	ent = FZ_BeamFor( target );
	if ( ent ) {
		if ( rarity > ent->s.modelindex2 ) {
			ent->s.modelindex2 = rarity;
			ent->s.time = level.time;		// a new shimmer
		}
		return;
	}
	if ( !FZ_BeamRoom( rarity ) ) {
		return;
	}
	ent = FZ_SpawnBeam( target->r.currentOrigin, rarity );
	ent->enemy = target;
	ent->count = target->s.eType;		// identity of what it follows (the slot may be reused later)
	ent->health = target->s.generic1;
	ent->s.otherEntityNum = target->s.number;
	G_LogPrintf( "Frenzy: beam %i on %s (%i)\n", rarity, target->classname ? target->classname : "?", target->s.number );
}

gentity_t *G_DS_BeamAt( vec3_t origin, int rarity, int msec ) {
	gentity_t	*ent;

	if ( !FZ_On( &fz.cvBeams ) ) {
		return NULL;
	}
	rarity = rarity < 0 ? 0 : rarity >= DS_BEAM_NUM ? DS_BEAM_NUM - 1 : rarity;
	if ( !FZ_BeamRoom( rarity ) ) {
		return NULL;
	}
	ent = FZ_SpawnBeam( origin, rarity );
	ent->s.time2 = msec > 0 ? level.time + msec : 0;
	G_LogPrintf( "Frenzy: beam %i at %i %i %i (%i ms)\n", rarity, (int)origin[0], (int)origin[1], (int)origin[2], msec );
	return ent;
}

void G_DS_LootBeamOff( gentity_t *ent ) {
	gentity_t	*b;

	if ( !ent || !ent->inuse ) {
		return;
	}
	if ( ent->s.eType == ET_GENERAL && ent->s.generic1 == RM_ENT_BEAM ) {
		G_FreeEntity( ent );
		return;
	}
	b = FZ_BeamFor( ent );
	if ( b ) {
		G_FreeEntity( b );
	}
}

/*
===============================================================================

FRAME / INIT

===============================================================================
*/

void G_DS_FrenzyInit( void ) {
	vmCvar_t	cf, cc, cs, cb;

	trap_Cvar_Register( &cf, "g_dsFrenzy", "1", CVAR_ARCHIVE );
	trap_Cvar_Register( &cc, "g_dsCrits", "1", CVAR_ARCHIVE );
	trap_Cvar_Register( &cs, "g_dsSpikes", "1", CVAR_ARCHIVE );
	trap_Cvar_Register( &cb, "g_dsBeams", "1", CVAR_ARCHIVE );
	memset( &fz, 0, sizeof( fz ) );
	fz.cvFrenzy = cf;
	fz.cvCrits = cc;
	fz.cvSpikes = cs;
	fz.cvBeams = cb;
	fz.human = -1;
	if ( G_RL_Active() ) {
		G_LogPrintf( "Frenzy: init frenzy %i crits %i spikes %i beams %i\n", fz.cvFrenzy.integer, fz.cvCrits.integer,
			fz.cvSpikes.integer, fz.cvBeams.integer );
	}
}

// RL_Finish (and the frame, once the intermission runs): cash out, end the spike, the summary
void G_DS_FrenzyFloorEnd( void ) {
	if ( !G_RL_Active() || fz.floorOver ) {
		return;
	}
	fz.floorOver = qtrue;
	FZ_CashOut( "floor end" );
	FZ_SpikeEnd( "floor end" );
	G_LogPrintf( "Frenzy: floor summary best %i chains %i xp %i crits %i/%i (%.1f%%) bot crits %i/%i (%.1f%%)\n",
		fz.best, fz.chains, fz.floorXP, fz.crits[0], fz.hits[0], fz.hits[0] ? 100.0f * fz.crits[0] / fz.hits[0] : 0.0f,
		fz.crits[1], fz.hits[1], fz.hits[1] ? 100.0f * fz.crits[1] / fz.hits[1] : 0.0f );
}

void G_DS_FrenzyFrame( void ) {
	gentity_t	*h;

	trap_Cvar_Update( &fz.cvFrenzy );
	trap_Cvar_Update( &fz.cvCrits );
	trap_Cvar_Update( &fz.cvSpikes );
	trap_Cvar_Update( &fz.cvBeams );
	if ( !G_RL_Active() ) {
		return;
	}
	h = FZ_Human();
	if ( level.intermissiontime || level.intermissionQueued ) {
		G_DS_FrenzyFloorEnd();
	}
	if ( fz.count > 0 && ( !fz.cvFrenzy.integer || level.time >= fz.expire ) ) {
		FZ_CashOut( fz.cvFrenzy.integer ? "time" : "switched off" );
	}
	FZ_SpikeFrame();
	(void)h;
}

/*
===============================================================================

DEV COMMANDS

===============================================================================
*/

// a floor spot in front of the player
static void FZ_FrontSpot( gentity_t *h, float dist, float yawOfs, vec3_t out ) {
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

static gentity_t *FZ_NearestBot( gentity_t *h ) {
	int			i;
	float		d, best = 0;
	gentity_t	*e, *found = NULL;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		e = &g_entities[i];
		if ( e == h || !e->inuse || !e->client || e->health <= 0 || !( e->r.svFlags & SVF_BOT )
			|| e->client->sess.sessionTeam == TEAM_SPECTATOR ) {
			continue;
		}
		d = Distance( e->r.currentOrigin, h->r.currentOrigin );
		if ( !found || d < best ) {
			found = e;
			best = d;
		}
	}
	return found;
}

/*
=================
G_DS_FrenzyCommand

dsfrenzy <n> | end			set the chain (tiers, berserk) / cash out now
dscrit [stats | sim <n>]	the player's next hit crits / counters / roll n times (rates)
dsspike berserk|overdrive [drop]	start a spike / drop its pickup in front
dsbeam <rarity> [msec]		a beam in front of the player; dsbeam chests: one chest of each quality
							on the map's spawn points (the beams test)
dsfzstate
=================
*/
qboolean G_DS_FrenzyCommand( const char *cmd ) {
	char		a1[32], a2[32];
	gentity_t	*h, *spot, *bot;
	vec3_t		org;
	int			i, n, k, kind, hitsH, critsH, hitsB, critsB;
	float		pH, pB;

	if ( Q_stricmp( cmd, "dsfrenzy" ) && Q_stricmp( cmd, "dscrit" ) && Q_stricmp( cmd, "dsspike" )
		&& Q_stricmp( cmd, "dsbeam" ) && Q_stricmp( cmd, "dsfzstate" ) ) {
		return qfalse;
	}
	trap_Argv( 1, a1, sizeof( a1 ) );
	trap_Argv( 2, a2, sizeof( a2 ) );
	h = FZ_Human();
	if ( !Q_stricmp( cmd, "dsfzstate" ) ) {
		G_Printf( "frenzy: active %i on %i/%i/%i/%i chain %i tier %i left %i best %i xp %i spike %i left %i crits %i/%i bots %i/%i beams %i\n",
			G_RL_Active(), fz.cvFrenzy.integer, fz.cvCrits.integer, fz.cvSpikes.integer, fz.cvBeams.integer, fz.count, fz.tier,
			fz.count ? fz.expire - level.time : 0, fz.best, fz.floorXP, fz.spike, fz.spike ? fz.spikeEnd - level.time : 0,
			fz.crits[0], fz.hits[0], fz.crits[1], fz.hits[1], FZ_CountMarker( RM_ENT_BEAM ) );
		G_LogPrintf( "Frenzy: state chain %i tier %i best %i xp %i spike %i crits %i/%i bots %i/%i beams %i spikes %i\n", fz.count, fz.tier,
			fz.best, fz.floorXP, fz.spike, fz.crits[0], fz.hits[0], fz.crits[1], fz.hits[1], FZ_CountMarker( RM_ENT_BEAM ),
			FZ_CountMarker( RM_ENT_SPIKE ) );
		return qtrue;
	}
	if ( !G_RL_Active() || !h ) {
		G_Printf( "frenzy: needs an active descent with the player in\n" );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsfrenzy" ) ) {
		if ( !Q_stricmp( a1, "end" ) ) {
			FZ_CashOut( "dev" );
			return qtrue;
		}
		n = atoi( a1 );
		if ( n <= 0 ) {
			FZ_CashOut( "dev" );
			return qtrue;
		}
		FZ_SetCount( n, FZ_WINDOW_MAX );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dscrit" ) ) {
		if ( !Q_stricmp( a1, "stats" ) ) {
			G_LogPrintf( "Frenzy: crit stats player %i/%i (%.2f%%) bots %i/%i (%.2f%%)\n", fz.crits[0], fz.hits[0],
				fz.hits[0] ? 100.0f * fz.crits[0] / fz.hits[0] : 0.0f, fz.crits[1], fz.hits[1], fz.hits[1] ? 100.0f * fz.crits[1] / fz.hits[1] : 0.0f );
			return qtrue;
		}
		if ( !Q_stricmp( a1, "sim" ) ) {
			// roll the real chance functions n times: the player now, and the first bot
			n = a2[0] ? atoi( a2 ) : 10000;
			bot = NULL;
			for ( i = 0 ; i < level.maxclients ; i++ ) {
				if ( g_entities[i].inuse && g_entities[i].client && ( g_entities[i].r.svFlags & SVF_BOT ) ) {
					bot = &g_entities[i];
					break;
				}
			}
			pH = FZ_CritChance( h );
			pB = bot ? FZ_CritChance( bot ) : 0;
			hitsH = critsH = hitsB = critsB = 0;
			for ( k = 0 ; k < n ; k++ ) {
				hitsH++;
				critsH += random() < pH;
				if ( bot ) {
					hitsB++;
					critsB += random() < pB;
				}
			}
			G_LogPrintf( "Frenzy: crit sim n %i luck %i%% tier %i spike %i player p %.2f%% got %.2f%% bot p %.2f%% got %.2f%%\n", n,
				G_DS_LuckPct(), fz.tier, fz.spike, pH * 100, 100.0f * critsH / ( hitsH ? hitsH : 1 ), pB * 100, 100.0f * critsB / ( hitsB ? hitsB : 1 ) );
			return qtrue;
		}
		if ( !Q_stricmp( a1, "hits" ) || !Q_stricmp( a1, "clamp" ) ) {
			// real hits through G_Damage on the nearest bot (kept alive): the crit rate / the fairness clamp
			bot = FZ_NearestBot( h );
			if ( !bot ) {
				G_Printf( "dscrit: no bot alive\n" );
				return qtrue;
			}
			VectorCopy( bot->r.currentOrigin, org );
			org[2] += 16;
			if ( !Q_stricmp( a1, "clamp" ) ) {
				// a forced crit with a big non-lethal hit on a full-health target: must not kill
				bot->health = bot->client->ps.stats[STAT_MAX_HEALTH];
				bot->client->ps.stats[STAT_HEALTH] = bot->health;
				bot->client->ps.stats[STAT_ARMOR] = 0;
				k = bot->health;
				n = a2[0] ? atoi( a2 ) : k * 3 / 4;
				fz.forceCrit = qtrue;
				fz.lastCritBase = fz.lastCritRaw = fz.lastCritDmg = 0;
				G_Damage( bot, h, h, NULL, org, n, DAMAGE_NO_ARMOR | DAMAGE_NO_KNOCKBACK, MOD_MACHINEGUN );
				G_LogPrintf( "Frenzy: crit clamp target %i health %i -> %i (max %i) hit %i scaled %i crit x %i clamped %i %s\n", bot->s.number,
					k, bot->health, bot->client->ps.stats[STAT_MAX_HEALTH], n, fz.lastCritBase, fz.lastCritRaw, fz.lastCritDmg,
					bot->health > 0 ? "ALIVE (ok)" : "KILLED (bad)" );
				return qtrue;
			}
			n = a2[0] ? atoi( a2 ) : 50;
			if ( n > 100 ) {
				n = 100;		// gore / hit events: a frame can only take so many temp entities
			}
			hitsH = fz.hits[0];
			critsH = fz.crits[0];
			for ( k = 0 ; k < n && bot->inuse && bot->health > 0 ; k++ ) {
				bot->health = bot->client->ps.stats[STAT_MAX_HEALTH];
				G_Damage( bot, h, h, NULL, org, 4, DAMAGE_NO_ARMOR | DAMAGE_NO_KNOCKBACK, ( k & 1 ) ? MOD_MACHINEGUN : MOD_RAILGUN );
			}
			bot->health = bot->client->ps.stats[STAT_MAX_HEALTH];
			bot->client->ps.stats[STAT_HEALTH] = bot->health;
			G_LogPrintf( "Frenzy: crit hits %i crits %i rate %.2f%% (expected %.2f%%: luck %i%% tier %i spike %i)\n", fz.hits[0] - hitsH,
				fz.crits[0] - critsH, fz.hits[0] - hitsH ? 100.0f * ( fz.crits[0] - critsH ) / ( fz.hits[0] - hitsH ) : 0.0f,
				FZ_CritChance( h ) * 100, G_DS_LuckPct(), fz.tier, fz.spike );
			return qtrue;
		}
		if ( !Q_stricmp( a1, "fx" ) ) {
			// the look only: a glint in front of the player and a crit hit confirm (screenshots)
			FZ_FrontSpot( h, 110, 0, org );
			org[2] += h->client->ps.viewheight - 18;
			spot = G_TempEntity( org, EV_DS_FRENZY );
			spot->s.eventParm = DSFZ_CRIT;
			spot->s.otherEntityNum = h->s.number;
			spot->s.otherEntityNum2 = ENTITYNUM_NONE;
			spot->s.generic1 = a2[0] ? atoi( a2 ) : 0;
			spot = G_TempEntity( org, EV_HITCONFIRM );
			spot->s.eventParm = 40;
			spot->s.generic1 = HIT_CRIT | ( a2[0] && atoi( a2 ) ? HIT_HEADSHOT : 0 );
			spot->s.otherEntityNum = ENTITYNUM_NONE;
			spot->r.svFlags |= SVF_SINGLECLIENT | SVF_BROADCAST;
			spot->r.singleClient = h->s.number;
			return qtrue;
		}
		fz.forceCrit = qtrue;
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsspike" ) ) {
		kind = !Q_stricmp( a1, "overdrive" ) ? DS_SPIKE_OVERDRIVE : !Q_stricmp( a1, "off" ) ? DS_SPIKE_NONE : DS_SPIKE_BERSERK;
		if ( kind == DS_SPIKE_NONE ) {
			FZ_SpikeEnd( "dev" );
		} else if ( !Q_stricmp( a2, "drop" ) || !Q_stricmp( a2, "here" ) ) {
			if ( !Q_stricmp( a2, "here" ) ) {
				VectorCopy( h->r.currentOrigin, org );		// lands on the player: the pickup test
			} else {
				FZ_FrontSpot( h, 140, 0, org );
			}
			G_DS_SpawnPowerSpike( org, kind );
		} else {
			G_DS_GrantSpike( h, kind );
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsbeam" ) ) {
		if ( !Q_stricmp( a1, "chests" ) ) {
			// a chest of each quality and a beam of each rarity on the spawn points: the across-the-map test
			spot = NULL;
			for ( i = 0 ; i < 12 ; i++ ) {
				spot = G_Find( spot, FOFS( classname ), "info_player_deathmatch" );
				if ( !spot ) {
					break;
				}
				if ( Distance( spot->s.origin, h->r.currentOrigin ) < 200 ) {
					continue;
				}
				if ( i % 4 == 3 ) {
					G_DS_SpawnPowerSpike( spot->s.origin, DS_SPIKE_OVERDRIVE );
				} else {
					G_DS_SpawnChest( spot->s.origin, i % 4 );
				}
			}
			return qtrue;
		}
		if ( !Q_stricmp( a1, "hidden" ) ) {
			// beams on the spawn points the player can't see (behind walls); the player turns to the nearest
			vec3_t	eye, dir, ang;
			trace_t	tr;
			float	d, best = 0;
			gentity_t	*near = NULL;

			VectorCopy( h->client->ps.origin, eye );
			eye[2] += h->client->ps.viewheight;
			spot = NULL;
			n = 0;
			while ( ( spot = G_Find( spot, FOFS( classname ), "info_player_deathmatch" ) ) != NULL && n < 12 ) {
				VectorCopy( spot->s.origin, org );
				org[2] += 40;
				d = Distance( org, eye );
				trap_Trace( &tr, eye, NULL, NULL, org, h->s.number, MASK_SOLID );
				if ( tr.fraction >= 1.0f || d < 150 || d > 2000 ) {
					continue;
				}
				G_DS_BeamAt( spot->s.origin, DS_BEAM_LEGENDARY - ( n % 3 ), 0 );
				n++;
				if ( !near || d < best ) {
					near = spot;
					best = d;
				}
			}
			if ( near ) {
				VectorSubtract( near->s.origin, eye, dir );
				dir[2] += 120;
				vectoangles( dir, ang );
				SetClientViewAngle( h, ang );
			}
			G_LogPrintf( "Frenzy: %i hidden beams, facing the nearest at %.0f units\n", n, best );
			return qtrue;
		}
		FZ_FrontSpot( h, 160, 0, org );
		G_DS_BeamAt( org, atoi( a1 ), a2[0] ? atoi( a2 ) : 0 );
		return qtrue;
	}
	return qfalse;
}
