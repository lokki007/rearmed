/*
===========================================================================
REARMED -- round 4 HIT FEEL (en-feel), all modes. Contract: bg_ds_enemy.h + bg_hitfeel.h.

How enemies react to being hit (the server decides, cgame/cg_hitfeel.c draws):
  stagger    big hits (rail, rocket direct, charged alt-fire, headshot, 60+) stop a bot firing
             for a moment and make its aim drift (DSEF_STAGGER, g_enemy[].staggerUntil).
             The Juggernaut only staggers from the very biggest hits and takes x0.35 knockback.
  dismember  headshot kills (g_gore.c) and heavy upper-body kills pop the head; heavy hits tear
             arms off, big explosions take the legs (bodies keep it: HFB_ bits in s.time2).
             Armoured bots lose their helmet to a heavy headshot they survive.
  crawl      a bot nearly dead (or legless) may drop and crawl (DSEF_CRAWL): forced crouch.
  morale     big hits, lost limbs, gibbed allies nearby and a lost duel break morale; under
             the threshold DSEF_PANIC for a few seconds (scream, weapon dropped briefly).
             en-brains moves panicked / crawling bots (g_enemy[].spare[2] = scarer + 1).
  wall splat g_physics.c G_WallSplat asks G_HF_WallSplat (switch, stagger, morale).
  armour     armorPct (vs the most armour this life) is published for the cracks shell.
  screams    the death scream category is decided here and sent with EV_HITFEEL.
Switches: g_stagger, g_dismember, g_crawl, g_morale, g_wallSplat (all archived, default 1).
Log lines start with "HitFeel:". Dev: hftest <client> stagger|crawl|panic|larm|rarm|legs|helmet|armor|decap|stats
===========================================================================
*/
#include "g_local.h"

#define HF_MORALE_PANIC		0.25f	// panic under this
#define HF_MORALE_AFTER		0.60f	// where morale sits after a panic
#define HF_PANIC_COOLDOWN	7000
#define HF_STAGGER_LOCKOUT	900		// no new stagger this soon after one ended
#define HF_DROP_MSEC		1200	// panic: weapon dropped (no fire) this long
#define HF_CRAWL_HEALTH		15		// at or under: may drop to a crawl
#define HF_LEGLESS_MAX		35		// a legless bot bleeds down to this (and never heals above it) ...
#define HF_LEGLESS_MIN		8		// ... and on down to this, 2 health a second

typedef struct {
	qboolean	alive;
	int			lastThink;
	int			lastHit;
	int			staggerEnd;
	int			panicUntil;
	int			panicDrop;
	int			panicNext;
	float		taken, dealt;		// decaying damage (duel tracking)
	int			duelNext;
	int			armorBase;
	int			armorStep;			// last published armour step (0..10)
	int			limbs;
	int			bleedAcc;			// legless bleeding (msec)
	float		driftSeed;
	// the last hit (for the death decision)
	int			hitTime;
	int			hitZone;			// HFZ_
	int			hitMod;
	int			hitDflags;
	qboolean	hitHeavy;
	qboolean	hitHead;
	vec3_t		hitDir;
	vec3_t		hitPoint;
} hfClient_t;

#define HFZ_BODY	0
#define HFZ_HEAD	1
#define HFZ_NECK	2
#define HFZ_LARM	3
#define HFZ_RARM	4
#define HFZ_LEGS	5

static hfClient_t	hf_cl[MAX_CLIENTS];

static vmCvar_t	hf_stagger, hf_dismember, hf_crawl, hf_morale, hf_wallSplat;
static int		hf_cvarTime = -1;

// test counters (hftest stats, and the log)
static int		hf_nStagger, hf_nDecap, hf_nLimb, hf_nCrawl, hf_nPanic, hf_nSplat, hf_nHelmet, hf_nShatter;

static void HF_Cvars( void ) {
	if ( hf_cvarTime < 0 ) {
		trap_Cvar_Register( &hf_stagger, "g_stagger", "1", CVAR_ARCHIVE );
		trap_Cvar_Register( &hf_dismember, "g_dismember", "1", CVAR_ARCHIVE );
		trap_Cvar_Register( &hf_crawl, "g_crawl", "1", CVAR_ARCHIVE );
		trap_Cvar_Register( &hf_morale, "g_morale", "1", CVAR_ARCHIVE );
		trap_Cvar_Register( &hf_wallSplat, "g_wallSplat", "1", CVAR_ARCHIVE );
		hf_cvarTime = level.time;
		return;
	}
	if ( hf_cvarTime != level.time ) {
		hf_cvarTime = level.time;
		trap_Cvar_Update( &hf_stagger );
		trap_Cvar_Update( &hf_dismember );
		trap_Cvar_Update( &hf_crawl );
		trap_Cvar_Update( &hf_morale );
		trap_Cvar_Update( &hf_wallSplat );
	}
}

static qboolean HF_IsBot( gentity_t *ent ) {
	return ent && ent->client && ( ent->r.svFlags & SVF_BOT ) ? qtrue : qfalse;
}

static int HF_Arch( int n ) {
	gEnemy_t	*e = G_EN( n );

	return e ? e->arch : 0;
}

// bosses and kamikazes don't crawl / panic / lose limbs alive
static qboolean HF_Special( gentity_t *ent ) {
	int		n = ent->s.number;

	if ( ent->client->ps.generic1 & RM_BODY_BOSS ) {
		return qtrue;
	}
	if ( G_DS_BossClient() == n ) {
		return qtrue;
	}
	return HF_Arch( n ) == DSAR_KAMIKAZE;
}

static gentity_t *HF_Event( gentity_t *victim, int kind, const vec3_t at, const vec3_t dir, int g1 ) {
	gentity_t	*ev;
	vec3_t		p;

	if ( at ) {
		VectorCopy( at, p );
	} else {
		VectorCopy( victim->r.currentOrigin, p );
	}
	ev = G_TempEntity( p, EV_HITFEEL );
	ev->s.eventParm = kind;
	ev->s.otherEntityNum = victim->s.number;
	ev->s.generic1 = g1 < 0 ? 0 : g1 > 255 ? 255 : g1;
	if ( dir ) {
		VectorCopy( dir, ev->s.angles2 );
	}
	return ev;
}

static void HF_SetLimbs( gentity_t *ent, int limbs ) {
	hfClient_t	*hf = &hf_cl[ent->s.number];
	gEnemy_t	*e = G_EN( ent->s.number );

	hf->limbs = limbs;
	ent->s.time2 = limbs;
	if ( e ) {
		e->spare[3] = limbs & ( HFB_ARMS | HFB_LEGS );
	}
	G_EN_SetFlag( ent->s.number, DSEF_NOARM, ( limbs & HFB_ARMS ) ? qtrue : qfalse );
}

/*
=================
morale
=================
*/
static void HF_Morale( gentity_t *ent, float drop, gentity_t *scarer, const char *why ) {
	gEnemy_t	*e = G_EN( ent->s.number );
	hfClient_t	*hf = &hf_cl[ent->s.number];

	HF_Cvars();
	if ( !e || !hf_morale.integer || !HF_IsBot( ent ) || ent->health <= 0 || HF_Special( ent ) ) {
		return;
	}
	if ( HF_Arch( ent->s.number ) == DSAR_JUGGERNAUT ) {
		drop *= 0.5f;
	}
	e->morale -= drop;
	if ( e->morale < 0 ) {
		e->morale = 0;
	}
	if ( e->morale >= HF_MORALE_PANIC || ( e->flags & DSEF_PANIC ) || level.time < hf->panicNext ) {
		return;
	}
	// morale broke
	hf->panicUntil = level.time + 2500 + rand() % 1500;
	hf->panicDrop = level.time + HF_DROP_MSEC;
	hf->panicNext = hf->panicUntil + HF_PANIC_COOLDOWN;
	e->spare[2] = scarer && scarer->client && scarer != ent ? scarer->s.number + 1 : 0;
	G_EN_SetFlag( ent->s.number, DSEF_PANIC, qtrue );
	HF_Event( ent, HFX_PANIC, NULL, NULL, 0 );
	hf_nPanic++;
	G_LogPrintf( "HitFeel: panic %i (%s) scarer %i morale %.2f\n", ent->s.number, why, e->spare[2] - 1, e->morale );
}

/*
=================
G_HF_KnockbackScale

G_Damage: the Juggernaut barely moves; big direct hits on bots shove harder (wall splats).
=================
*/
float G_HF_KnockbackScale( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, int mod, int dflags ) {
	HF_Cvars();
	if ( !targ || !targ->client ) {
		return 1.0f;
	}
	if ( HF_Arch( targ->s.number ) == DSAR_JUGGERNAUT ) {
		return 0.35f;
	}
	if ( hf_wallSplat.integer && HF_IsBot( targ ) && !( dflags & DAMAGE_RADIUS )
		&& ( mod == MOD_ROCKET || mod == MOD_RAILGUN || mod == MOD_BFG
			|| ( inflictor && inflictor->s.eType == ET_MISSILE && ( inflictor->s.generic1 & ALTFIRE_CHARGED ) )
			|| ( attacker && attacker->client && level.time - attacker->client->chargedShotTime < 100 ) ) ) {
		return 1.25f;
	}
	return 1.0f;
}

static int HF_Zone( gentity_t *targ, gentity_t *inflictor, const vec3_t point, int dflags, qboolean headshot ) {
	vec3_t	rel, fwd, right, from;
	float	z, side, eye;

	if ( headshot ) {
		return HFZ_HEAD;
	}
	if ( !point || ( dflags & DAMAGE_RADIUS ) ) {
		// splash: below the hips takes the legs, otherwise an arm on the blast's side
		if ( !inflictor ) {
			return HFZ_BODY;
		}
		VectorCopy( inflictor->r.currentOrigin, from );
		if ( from[2] < targ->r.currentOrigin[2] - 4 ) {
			return HFZ_LEGS;
		}
		AngleVectors( targ->client->ps.viewangles, fwd, right, NULL );
		VectorSubtract( from, targ->r.currentOrigin, rel );
		return DotProduct( rel, right ) > 0 ? HFZ_RARM : HFZ_LARM;
	}
	VectorSubtract( point, targ->r.currentOrigin, rel );
	z = rel[2];
	eye = targ->client->ps.viewheight;
	if ( z >= eye - 18 ) {
		return HFZ_NECK;		// neck and upper chest (the head itself is the headshot line)
	}
	if ( z < -2 ) {
		return HFZ_LEGS;
	}
	AngleVectors( targ->client->ps.viewangles, fwd, right, NULL );
	side = DotProduct( rel, right );
	if ( side > 7 ) {
		return HFZ_RARM;
	}
	if ( side < -7 ) {
		return HFZ_LARM;
	}
	return HFZ_BODY;
}

static void HF_LimbPos( gentity_t *ent, int limb, vec3_t out ) {
	vec3_t	fwd, right;

	AngleVectors( ent->client->ps.viewangles, fwd, right, NULL );
	VectorCopy( ent->r.currentOrigin, out );
	if ( limb == HFB_LEGS ) {
		out[2] -= 12;
	} else {
		out[2] += ent->client->ps.viewheight - 12;
		VectorMA( out, limb == HFB_RARM ? 10 : -10, right, out );
	}
}

static void HF_LoseLimb( gentity_t *ent, int limb, const vec3_t dir, qboolean lethal, gentity_t *attacker ) {
	hfClient_t	*hf = &hf_cl[ent->s.number];
	vec3_t		at;

	if ( hf->limbs & limb ) {
		return;
	}
	HF_SetLimbs( ent, hf->limbs | limb );
	HF_LimbPos( ent, limb, at );
	HF_Event( ent, HFX_LIMB, at, dir, limb );
	hf_nLimb++;
	G_LogPrintf( "HitFeel: limb %i %s %s\n", ent->s.number,
		limb == HFB_LEGS ? "legs" : limb == HFB_LARM ? "larm" : "rarm", lethal ? "lethal" : "alive" );
	if ( !lethal ) {
		HF_Morale( ent, 0.35f, attacker, "limb" );
	}
}

/*
=================
crawl
=================
*/
static void HF_StartCrawl( gentity_t *ent, const char *why, gentity_t *attacker ) {
	if ( G_EN_HasFlag( ent->s.number, DSEF_CRAWL ) ) {
		return;
	}
	G_EN_SetFlag( ent->s.number, DSEF_CRAWL, qtrue );
	HF_Event( ent, HFX_CRAWL, NULL, NULL, 0 );
	hf_nCrawl++;
	G_LogPrintf( "HitFeel: crawl %i (%s) health %i\n", ent->s.number, why, ent->health );
	HF_Morale( ent, 0.3f, attacker, "crawl" );
}

static void HF_Stagger( gentity_t *ent, int strength, const vec3_t dir, const char *why ) {
	gEnemy_t	*e = G_EN( ent->s.number );
	hfClient_t	*hf = &hf_cl[ent->s.number];
	int			msec;

	if ( !e ) {
		return;
	}
	if ( !( e->flags & DSEF_STAGGER ) && level.time - hf->staggerEnd < HF_STAGGER_LOCKOUT ) {
		return;		// no stun lock
	}
	msec = 420 + strength * 3;
	if ( msec > 900 ) {
		msec = 900;
	}
	if ( HF_Arch( ent->s.number ) == DSAR_JUGGERNAUT ) {
		msec /= 2;
	}
	if ( level.time + msec > e->staggerUntil ) {
		e->staggerUntil = level.time + msec;
	}
	if ( !( e->flags & DSEF_STAGGER ) ) {
		hf_nStagger++;
		hf->driftSeed = random() * 6.28f;
		G_LogPrintf( "HitFeel: stagger %i (%s) %i ms\n", ent->s.number, why, msec );
	}
	G_EN_SetFlag( ent->s.number, DSEF_STAGGER, qtrue );
	HF_Event( ent, HFX_STAGGER, NULL, dir, strength );
}

/*
=================
G_HF_Damage

G_Damage, after the boss / loot / skills / cards hooks settled take, before the health drops.
=================
*/
void G_HF_Damage( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, vec3_t dir, vec3_t point,
				 int take, int asave, int dflags, int mod, int hitFlags ) {
	hfClient_t	*hf;
	gEnemy_t	*e;
	int			n, total, zone, arch, armor;
	qboolean	bot, charged, direct, explosive, big, blocked, lethal, headshot;
	vec3_t		d;

	if ( !targ || !targ->client || targ->health <= 0 || ( take <= 0 && asave <= 0 ) ) {
		return;
	}
	HF_Cvars();
	n = targ->s.number;
	if ( n >= MAX_CLIENTS ) {
		return;
	}
	hf = &hf_cl[n];
	e = G_EN( n );
	bot = HF_IsBot( targ );
	total = take + asave;
	arch = HF_Arch( n );
	lethal = targ->health - take <= 0;
	headshot = ( hitFlags & HIT_HEADSHOT ) != 0;
	direct = !( dflags & DAMAGE_RADIUS );
	charged = ( inflictor && inflictor->s.eType == ET_MISSILE && ( inflictor->s.generic1 & ALTFIRE_CHARGED ) )
		|| ( attacker && attacker->client && level.time - attacker->client->chargedShotTime < 100 );
	explosive = mod == MOD_ROCKET || mod == MOD_ROCKET_SPLASH || mod == MOD_GRENADE || mod == MOD_GRENADE_SPLASH
		|| mod == MOD_BFG || mod == MOD_BFG_SPLASH || mod == MOD_PROXIMITY_MINE || mod == MOD_PROP
		|| mod == MOD_METEOR || mod == MOD_CORPSE;
	big = headshot || mod == MOD_RAILGUN || ( mod == MOD_ROCKET && direct ) || charged || total >= 60;
	if ( arch == DSAR_JUGGERNAUT ) {
		big = total >= 60 || charged || mod == MOD_RAILGUN || ( mod == MOD_ROCKET && direct );
	}
	blocked = e && e->arch == DSAR_SHIELD && e->spare[0] == level.time;		// en-types: frontal shield block
	if ( attacker == targ || mod == MOD_FALLING || mod == MOD_WALLSPLAT || mod == MOD_TRIGGER_HURT ) {
		big = qfalse;		// no stagger from your own rocket jumps, falls or splats (G_HF_WallSplat staggers those)
	}
	if ( dir ) {
		VectorCopy( dir, d );
		VectorNormalize( d );
	} else {
		VectorClear( d );
	}

	// remember the hit: the death decision reads it
	zone = HF_Zone( targ, inflictor, point, dflags, headshot );
	hf->hitTime = level.time;
	hf->hitZone = zone;
	hf->hitMod = mod;
	hf->hitDflags = dflags;
	hf->hitHead = headshot;
	hf->hitHeavy = charged || mod == MOD_RAILGUN || ( mod == MOD_ROCKET && direct ) || mod == MOD_BFG || total >= 80;
	VectorCopy( d, hf->hitDir );
	if ( point ) {
		VectorCopy( point, hf->hitPoint );
	} else {
		VectorCopy( targ->r.currentOrigin, hf->hitPoint );
	}
	hf->lastHit = level.time;

	// duels: who is winning
	hf->taken += total;
	if ( attacker && attacker->client && attacker != targ && attacker->s.number < MAX_CLIENTS ) {
		hf_cl[attacker->s.number].dealt += total;
	}

	// armour: a step down cracks it, the last of it shatters
	armor = targ->client->ps.stats[STAT_ARMOR];
	if ( asave > 0 ) {
		if ( hitFlags & HIT_ARMORBREAK ) {
			HF_Event( targ, HFX_ARMORBREAK, point, d, 0 );
			hf_nShatter++;
			G_LogPrintf( "HitFeel: armorbreak %i\n", n );
			HF_Morale( targ, 0.1f, attacker, "armorbreak" );
		} else if ( hf->armorBase > 0 && ( armor * 10 ) / hf->armorBase < hf->armorStep ) {
			HF_Event( targ, HFX_ARMORHIT, point, d, armor * 100 / hf->armorBase );
		}
	}

	if ( blocked || lethal ) {
		return;
	}

	// stagger
	if ( hf_stagger.integer && bot && big ) {
		HF_Stagger( targ, total, d, headshot ? "headshot" : charged ? "charged" : mod == MOD_RAILGUN ? "rail"
			: mod == MOD_ROCKET ? "rocket" : "heavy" );
	}

	// helmet: a heavy headshot on an armoured bot that survives
	if ( hf_dismember.integer && bot && headshot && armor > 0 && !( hf->limbs & HFB_HELMET )
		&& ( hf->hitHeavy || total >= 30 ) ) {
		HF_SetLimbs( targ, hf->limbs | HFB_HELMET );
		HF_Event( targ, HFX_HELMET, point, d, 0 );
		hf_nHelmet++;
		G_LogPrintf( "HitFeel: helmet %i\n", n );
	}

	// limbs off on heavy non-lethal hits (bots only; never the Juggernaut, bosses or kamikazes)
	if ( hf_dismember.integer && bot && arch != DSAR_JUGGERNAUT && !HF_Special( targ ) ) {
		if ( ( zone == HFZ_LARM || zone == HFZ_RARM ) && direct && hf->hitHeavy && total >= 50 && random() < 0.5f ) {
			HF_LoseLimb( targ, zone == HFZ_LARM ? HFB_LARM : HFB_RARM, d, qfalse, attacker );
		} else if ( explosive && total >= 70 && ( zone == HFZ_LEGS || !direct ) && random() < 0.35f ) {
			HF_LoseLimb( targ, HFB_LEGS, d, qfalse, attacker );
		}
	}

	// crawl: nearly dead, or legless
	if ( hf_crawl.integer && bot && !HF_Special( targ ) ) {
		if ( hf->limbs & HFB_LEGS ) {
			HF_StartCrawl( targ, "legs", attacker );
		} else if ( targ->health - take <= HF_CRAWL_HEALTH && !G_EN_HasFlag( n, DSEF_CRAWL ) && random() < 0.3f ) {
			HF_StartCrawl( targ, "health", attacker );
		}
	}

	// morale
	if ( bot ) {
		HF_Morale( targ, total / 600.0f + ( big ? 0.08f : 0.0f ), attacker, big ? "big hit" : "hit" );
		if ( hf->taken >= 70 && hf->dealt < hf->taken * 0.3f && level.time >= hf->duelNext ) {
			hf->duelNext = level.time + 4000;
			HF_Morale( targ, 0.2f, attacker, "lost duel" );
		}
	}
}

/*
=================
G_HF_Die

player_die, right after G_GoreOnDeath (body flags known, may still turn into a gib).
=================
*/
void G_HF_Die( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int mod, int damage ) {
	hfClient_t	*hf;
	int			n, i, flags, cat, contents, limb;
	qboolean	gib, recent, explosive, gore;
	gentity_t	*other;
	vec3_t		delta;
	trace_t		tr;

	if ( !self->client || self->s.number >= MAX_CLIENTS ) {
		return;
	}
	HF_Cvars();
	n = self->s.number;
	hf = &hf_cl[n];
	recent = level.time - hf->hitTime < 100;
	contents = trap_PointContents( self->r.currentOrigin, -1 );
	gore = g_blood.integer && trap_Cvar_VariableIntegerValue( "g_gore" );
	explosive = mod == MOD_ROCKET || mod == MOD_ROCKET_SPLASH || mod == MOD_GRENADE || mod == MOD_GRENADE_SPLASH
		|| mod == MOD_BFG || mod == MOD_BFG_SPLASH || mod == MOD_PROXIMITY_MINE || mod == MOD_PROP
		|| mod == MOD_METEOR || mod == MOD_CORPSE || mod == MOD_SLAM;
	flags = self->s.generic1 & RM_BODY_ALL;
	gib = self->health <= GIB_HEALTH && !( contents & CONTENTS_NODROP ) && g_blood.integer;

	// heavy kills to the neck / shoulders pop the head; heavy hits tear arms off; blasts take legs
	if ( hf_dismember.integer && gore && !gib && recent && mod != MOD_SUICIDE
		&& !( flags & ( RM_BODY_HEADLESS | RM_BODY_SPLIT | RM_BODY_CHARRED | RM_BODY_OVERKILL ) ) ) {
		if ( hf->hitZone == HFZ_NECK && hf->hitHeavy && !( hf->hitDflags & DAMAGE_RADIUS ) ) {
			flags |= RM_BODY_HEADLESS;
			self->client->ps.generic1 |= RM_BODY_HEADLESS;
			self->s.generic1 |= RM_BODY_HEADLESS;
			hf_nDecap++;
			G_LogPrintf( "HitFeel: decap %i heavy mod %i\n", n, mod );
		} else if ( ( hf->hitZone == HFZ_LARM || hf->hitZone == HFZ_RARM ) && ( hf->hitHeavy || damage >= 40 )
			&& random() < 0.7f ) {
			HF_LoseLimb( self, hf->hitZone == HFZ_LARM ? HFB_LARM : HFB_RARM, hf->hitDir, qtrue, attacker );
		} else if ( explosive && damage >= 40 && random() < 0.6f ) {
			i = rand() % 10;
			limb = i < 4 ? HFB_LEGS : i < 7 ? HFB_LARM : i < 9 ? HFB_RARM : ( HFB_LARM | HFB_LEGS );
			if ( limb & HFB_LEGS ) {
				HF_LoseLimb( self, HFB_LEGS, hf->hitDir, qtrue, attacker );
			}
			if ( limb & HFB_LARM ) {
				HF_LoseLimb( self, HFB_LARM, hf->hitDir, qtrue, attacker );
			}
			if ( limb & HFB_RARM ) {
				HF_LoseLimb( self, HFB_RARM, hf->hitDir, qtrue, attacker );
			}
		}
	}
	if ( flags & RM_BODY_HEADLESS ) {
		G_EN_SetFlag( n, DSEF_NOHEAD, qtrue );
		if ( recent && hf->hitHead ) {
			hf_nDecap++;		// g_gore.c's headshot decapitation
			G_LogPrintf( "HitFeel: decap %i headshot mod %i\n", n, mod );
		}
	}

	// the death scream, by how they died (cgame picks the voice)
	if ( gib ) {
		cat = HFS_GIB;
	} else if ( mod == MOD_BURN || mod == MOD_LAVA || mod == MOD_LIGHTNING || mod == MOD_THUNDER || ( flags & RM_BODY_CHARRED ) ) {
		cat = HFS_BURN;
	} else if ( mod == MOD_FALLING || mod == MOD_TRIGGER_HURT || mod == MOD_CRUSH ) {
		cat = HFS_FALL;
	} else if ( ( flags & RM_BODY_HEADLESS ) || ( recent && hf->hitHead ) ) {
		cat = HFS_HEADSHOT;
	} else if ( explosive ) {
		cat = HFS_EXPLODE;
	} else {
		cat = HFS_GENERIC;
	}
	HF_Event( self, HFX_SCREAM, NULL, NULL, cat );

	// the living states end with the life
	G_EN_SetFlag( n, DSEF_STAGGER | DSEF_CRAWL | DSEF_PANIC, qfalse );

	// a gory death nearby shakes the other bots
	if ( hf_morale.integer && ( gib || ( flags & ( RM_BODY_HEADLESS | RM_BODY_SPLIT | RM_BODY_OVERKILL ) ) || hf->limbs ) ) {
		for ( i = 0; i < level.maxclients; i++ ) {
			other = &g_entities[i];
			if ( i == n || !other->inuse || !HF_IsBot( other ) || other->health <= 0 ) {
				continue;
			}
			if ( g_gametype.integer >= GT_TEAM && !OnSameTeam( other, self ) ) {
				continue;
			}
			VectorSubtract( other->r.currentOrigin, self->r.currentOrigin, delta );
			if ( VectorLengthSquared( delta ) > 600.0f * 600.0f ) {
				continue;
			}
			trap_Trace( &tr, other->client->ps.origin, NULL, NULL, self->r.currentOrigin, i, MASK_SOLID );
			if ( tr.fraction < 1.0f ) {
				continue;
			}
			HF_Morale( other, 0.25f, attacker, "gibbed ally" );
		}
	}
}

/*
=================
G_HF_WallSplat

g_physics.c G_WallSplat, before anything happens: qfalse = no splat (g_wallSplat 0, or a
Juggernaut that wasn't hit hard enough). Living bots that splat are staggered and shaken.
=================
*/
qboolean G_HF_WallSplat( gentity_t *victim, gentity_t *attacker, float impact ) {
	qboolean	alive;

	HF_Cvars();
	if ( !hf_wallSplat.integer ) {
		return qfalse;
	}
	alive = victim->client && victim->health > 0;
	if ( alive && victim->s.number < MAX_CLIENTS && HF_Arch( victim->s.number ) == DSAR_JUGGERNAUT && impact < 800 ) {
		return qfalse;
	}
	hf_nSplat++;
	G_LogPrintf( "HitFeel: wallsplat %i %s impact %i\n", victim->s.number, alive ? "alive" : "corpse", (int)impact );
	if ( alive && HF_IsBot( victim ) ) {
		if ( hf_stagger.integer ) {
			HF_Stagger( victim, 200, NULL, "wall splat" );
		}
		HF_Morale( victim, 0.2f, attacker, "wall splat" );
	}
	return qtrue;
}

static void HF_Spawned( gentity_t *ent ) {
	hfClient_t	*hf = &hf_cl[ent->s.number];
	gEnemy_t	*e = G_EN( ent->s.number );

	memset( hf, 0, sizeof( *hf ) );
	hf->alive = qtrue;
	hf->lastThink = level.time;
	hf->armorBase = ent->client->ps.stats[STAT_ARMOR];
	hf->armorStep = hf->armorBase > 0 ? 10 : 0;
	ent->s.time2 = 0;
	if ( e ) {
		e->morale = 1.0f;
		e->staggerUntil = 0;
		e->spare[2] = 0;
		e->spare[3] = 0;
		if ( e->armorPct != ( hf->armorBase > 0 ? 100 : 0 ) ) {
			e->armorPct = hf->armorBase > 0 ? 100 : 0;
			G_EN_MarkDirty();
		}
	}
	G_EN_SetFlag( ent->s.number, DSEF_STAGGER | DSEF_CRAWL | DSEF_PANIC | DSEF_NOHEAD | DSEF_NOARM, qfalse );
}

/*
=================
G_HF_ClientThink

ClientThink_real, just before Pmove, on the pmove's copy of the command: stagger and panic
take the trigger away and make the aim drift, a crawler stays down. Also the per-life
bookkeeping (spawn reset, morale recovery, armour %).
=================
*/
void G_HF_ClientThink( gentity_t *ent, usercmd_t *cmd ) {
	gclient_t	*cl = ent->client;
	hfClient_t	*hf;
	gEnemy_t	*e;
	int			n, dt, armor, step, pct;
	float		k, t, amp;

	if ( !cl || ent->s.number >= MAX_CLIENTS ) {
		return;
	}
	n = ent->s.number;
	hf = &hf_cl[n];
	e = G_EN( n );
	if ( !e ) {
		return;
	}
	if ( cl->ps.pm_type == PM_DEAD || ent->health <= 0 || cl->sess.sessionTeam == TEAM_SPECTATOR ) {
		hf->alive = qfalse;
		return;
	}
	HF_Cvars();
	if ( !hf->alive ) {
		HF_Spawned( ent );
	}
	dt = level.time - hf->lastThink;
	hf->lastThink = level.time;
	if ( dt < 0 || dt > 1000 ) {
		dt = 0;
	}

	// duel memory fades (half life ~3 s)
	k = 1.0f - dt / 4300.0f;
	if ( k < 0 ) {
		k = 0;
	}
	hf->taken *= k;
	hf->dealt *= k;

	// morale comes back after 3 s without being hit
	if ( level.time - hf->lastHit > 2500 && e->morale < 1.0f && !( e->flags & DSEF_PANIC ) ) {
		e->morale += dt * 0.00006f;
		if ( e->morale > 1.0f ) {
			e->morale = 1.0f;
		}
	}

	// stagger: no trigger, aim drifts
	if ( e->flags & DSEF_STAGGER ) {
		if ( level.time >= e->staggerUntil || !hf_stagger.integer ) {
			G_EN_SetFlag( n, DSEF_STAGGER, qfalse );
			hf->staggerEnd = level.time;
		} else {
			cmd->buttons &= ~( BUTTON_ATTACK | BUTTON_ALTATTACK );
			t = level.time * 0.011f + hf->driftSeed;
			amp = ( e->staggerUntil - level.time ) / 60.0f;		// up to ~15 degrees, easing out
			if ( amp > 15 ) {
				amp = 15;
			}
			cmd->angles[YAW] += ANGLE2SHORT( amp * sin( t ) );
			cmd->angles[PITCH] += ANGLE2SHORT( amp * 0.6f * sin( t * 1.7f + 1.0f ) - amp * 0.3f );
		}
	}

	// panic: the weapon drops for a moment, then the flag goes
	if ( e->flags & DSEF_PANIC ) {
		if ( level.time >= hf->panicUntil || !hf_morale.integer ) {
			G_EN_SetFlag( n, DSEF_PANIC, qfalse );
			e->spare[2] = 0;
			if ( e->morale < HF_MORALE_AFTER ) {
				e->morale = HF_MORALE_AFTER;
			}
			G_LogPrintf( "HitFeel: recover %i\n", n );
		} else if ( level.time < hf->panicDrop ) {
			cmd->buttons &= ~( BUTTON_ATTACK | BUTTON_ALTATTACK );
		}
	}

	// crawl: stay down, no jumping; stand up again only with legs and health back
	if ( e->flags & DSEF_CRAWL ) {
		if ( !hf_crawl.integer || ( !( hf->limbs & HFB_LEGS ) && ent->health > 50 ) ) {
			G_EN_SetFlag( n, DSEF_CRAWL, qfalse );
		} else {
			cmd->upmove = -127;
			// legless: bleeding out, no healing it back
			if ( hf->limbs & HFB_LEGS ) {
				if ( ent->health > HF_LEGLESS_MAX ) {
					ent->health = HF_LEGLESS_MAX;
				}
				hf->bleedAcc += dt;
				if ( hf->bleedAcc >= 500 ) {
					hf->bleedAcc -= 500;
					if ( ent->health > HF_LEGLESS_MIN ) {
						ent->health--;
					}
				}
				cl->ps.stats[STAT_HEALTH] = ent->health;
			}
			if ( cl->ps.speed > 260 ) {
				cl->ps.speed = 260;		// crouched: x0.25 -> a 65 ups crawl
			}
		}
	}

	// armour left vs the most this life (published in 10% steps)
	armor = cl->ps.stats[STAT_ARMOR];
	if ( armor > hf->armorBase ) {
		hf->armorBase = armor;
	}
	if ( hf->armorBase > 0 && armor > 0 ) {
		step = ( armor * 10 + hf->armorBase - 1 ) / hf->armorBase;		// 1..10
		pct = step * 10;
	} else {
		step = 0;
		pct = 0;
		hf->armorBase = 0;
	}
	hf->armorStep = step;
	if ( e->armorPct != pct ) {
		e->armorPct = pct;
		G_EN_MarkDirty();
	}
}

/*
=================
G_HF_Command

dev: hftest <client> stagger|crawl|panic|larm|rarm|legs|helmet|armor|decap|stats
=================
*/
qboolean G_HF_Command( const char *cmd ) {
	char		arg[64];
	gentity_t	*ent;
	vec3_t		dir = { 1, 0, 0 };
	int			c;

	if ( Q_stricmp( cmd, "hftest" ) ) {
		return qfalse;
	}
	trap_Argv( 1, arg, sizeof( arg ) );
	if ( !Q_stricmp( arg, "stats" ) || !arg[0] ) {
		G_Printf( "HitFeel stats: stagger %i decap %i limb %i crawl %i panic %i splat %i helmet %i shatter %i\n",
			hf_nStagger, hf_nDecap, hf_nLimb, hf_nCrawl, hf_nPanic, hf_nSplat, hf_nHelmet, hf_nShatter );
		G_LogPrintf( "HitFeel: stats stagger %i decap %i limb %i crawl %i panic %i splat %i helmet %i shatter %i\n",
			hf_nStagger, hf_nDecap, hf_nLimb, hf_nCrawl, hf_nPanic, hf_nSplat, hf_nHelmet, hf_nShatter );
		return qtrue;
	}
	c = atoi( arg );
	if ( c < 0 || c >= level.maxclients || !g_entities[c].inuse || !g_entities[c].client || g_entities[c].health <= 0 ) {
		G_Printf( "hftest: no live client %i\n", c );
		return qtrue;
	}
	ent = &g_entities[c];
	trap_Argv( 2, arg, sizeof( arg ) );
	HF_Cvars();
	if ( !Q_stricmp( arg, "stagger" ) ) {
		HF_Stagger( ent, 150, dir, "hftest" );
	} else if ( !Q_stricmp( arg, "crawl" ) ) {
		ent->health = ent->client->ps.stats[STAT_HEALTH] = HF_CRAWL_HEALTH;	// nearly dead, or it stands right up
		HF_StartCrawl( ent, "hftest", NULL );
	} else if ( !Q_stricmp( arg, "panic" ) ) {
		HF_Morale( ent, 2.0f, NULL, "hftest" );
	} else if ( !Q_stricmp( arg, "larm" ) ) {
		HF_LoseLimb( ent, HFB_LARM, dir, qfalse, NULL );
	} else if ( !Q_stricmp( arg, "rarm" ) ) {
		HF_LoseLimb( ent, HFB_RARM, dir, qfalse, NULL );
	} else if ( !Q_stricmp( arg, "legs" ) ) {
		HF_LoseLimb( ent, HFB_LEGS, dir, qfalse, NULL );
		HF_StartCrawl( ent, "hftest legs", NULL );
	} else if ( !Q_stricmp( arg, "helmet" ) ) {
		HF_SetLimbs( ent, hf_cl[c].limbs | HFB_HELMET );
		HF_Event( ent, HFX_HELMET, NULL, dir, 0 );
	} else if ( !Q_stricmp( arg, "armor" ) ) {
		// armour left of a 200 base (default 100 = 50%)
		trap_Argv( 3, arg, sizeof( arg ) );
		ent->client->ps.stats[STAT_ARMOR] = arg[0] ? atoi( arg ) : 100;
		hf_cl[c].armorBase = 200;
	} else if ( !Q_stricmp( arg, "decap" ) ) {
		// a heavy neck hit that kills
		vec3_t	p;

		VectorCopy( ent->r.currentOrigin, p );
		p[2] += ent->client->ps.viewheight - 11;		// the neck, under the headshot line
		ent->client->ps.stats[STAT_ARMOR] = 0;
		G_Damage( ent, NULL, ent == &g_entities[0] ? NULL : &g_entities[0], dir, p, ent->health + 20,
			DAMAGE_NO_PROTECTION | DAMAGE_NO_KNOCKBACK, MOD_RAILGUN );
	} else {
		G_Printf( "hftest <client> stagger|crawl|panic|larm|rarm|legs|helmet|armor [n]|decap, hftest stats\n" );
	}
	return qtrue;
}
