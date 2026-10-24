/*
===========================================================================
REARMED -- DESCENT v2 ACTS (owner ACTS): act music, the vault floor and the act bosses.

A boss is a bot client (kit role boss) added by g_roguelike.c under the boss's own name.
This file keeps its health pool (the entity's own health stays full so the bot fights on;
G_DS_BossDamage takes what really comes off), splits it into phases (a "death" before the
last phase is a short invulnerable roar instead), and runs its scripted attacks: a cooldown
based picker per phase, every big attack telegraphed by RM_ENT_BOSSFX entities (ground
circles, lines, glows; the cgame draws them and plays the warning) for 0.8 - 1.2 s before
anything can hurt. The top health bar reads CS_DESCENTBOSS. On a boss floor the boss and its
adds are one side (they never hurt or hunt each other). Win = the boss dead in its last
phase; loss = the player runs out of lives (g_dsBossLives).

Round 2b (the player's call after playing): a boss floor is the player + 2 rival bots + the
boss, everyone with g_dsBossLives (10) lives (the boss DS_BOSS_LIVES). The rivals mostly go for
the boss but fight everyone; the boss fights everyone and aims its scripted attacks a little
more at whoever hurt it lately. Each boss life is a pool; it really dies and respawns far from
the player; phase 2 at 6 lives left, phase 3 at 3 (a roar when it comes back). A fighter out
of lives is eliminated (bots and the boss leave with a burst; the player out of lives loses the
floor). Win = the boss eliminated while the player still has lives. Nobody respawns next to
the boss, and the boss never teleports next to the player: a boss with nobody in sight walks
to the nearest fighter (ai: G_DS_BossHuntSpot); the Hollow King's blink is a sidestep that
ends 600+ units from the player.
Every boss has 2 of 3 strong sides (actsTraits: speed, health, damage; the ARCH-DEMON changes
its pair every phase, Abyss bosses roll one). g_dsBossScale is the overall tuning knob.
g_dsBossSim 1 (tests): a skill-5 bot named "Sim" plays the human's part (with the human's
growth, a modelled human dodge of the telegraphs) and the server quits after the result.

Round 4 (en-boss, the player's call again): g_dsBossMode 0 (default) = TIMED 1v1. Only the player
and the boss; one pool in phases (an empty phase = a roar, not a death); the player dies as often
as they like (2.5 s grace, back 700+ units away); g_dsBossTime (300 s) from the boss's first shot;
the last minute enrages it; time up = the floor lost; a kill pays a time bonus. g_dsBossMode 1 keeps
everything above. BOSS PRACTICE (g_dsPractice, q3_ui/ui_ds_bossprac.c) is always timed.

The vault floor: 45 s, no bots, one free chest, and one of DSV_NUM modes (the run seed and the
floor pick it; g_dsVaultMode forces one): RAIN (gems fall around the player), SHOOTING GALLERY
(floating crystals burst into gems when shot), RUSH (gems appear ahead of a fast player, more the
faster), SKY (columns of gems hang in the air, richer the higher). About 400-600 xp each.
===========================================================================
*/
#include "g_local.h"

#define DS_MAX_STRIKES		32
#define DS_MAX_PROPS		6
#define DS_ROAR_TIME		2000
#define DS_WIN_DELAY		4000		// the boss died: time to grab the loot before the floor ends
#define DS_NOVA_SPEED		560.0f
#define DS_NOVA_RADIUS		760.0f
#define DS_WELL_RADIUS		520.0f
#define DS_WELL_TIME		4500
#define DS_GRACE_TIME		2500		// a respawned player can't be hurt by the boss, and it holds its fire
#define DS_BOSS_LIVES		10			// the boss's lives (each one a pool)
#define DS_BOSS_LIFE_HP		0.30f		// a boss life = this x ds_bosses[].health (the old per-phase pool)
#define DS_RELOC_MIN		600.0f		// a boss relocation never ends closer to the player than this
#define DS_VAULT_TARGETS	5			// GALLERY: crystals up at once

typedef struct {
	int			kind;			// DSB_ attack
	int			land;
	vec3_t		org;
	int			damage;
	float		radius;
	gentity_t	*fx;
} dsStrike_t;

typedef struct {
	qboolean	on;				// a boss floor
	qboolean	vault;
	int			act, floor, difficulty, seed, abyss;
	int			type;			// dsBoss_t
	int			client;			// -1 until the boss spawned
	int			phase, phases;
	int			hp, maxHp;		// this phase's pool
	int			baseHp;			// before the player's power
	qboolean	powerSet;
	int			roarUntil;
	qboolean	dead;
	int			deathTime;
	int			playerDeaths;
	int			startTime;		// the human was first in
	float		dmgScale;		// scripted attack damage (difficulty)
	float		pace;			// attack gap multiplier (difficulty / phase)

	int			cooldown[DSB_NUM];
	int			nextAttack;
	int			lastNear;		// the boss last had the player close and in sight

	// the attack being cast
	int			atk;
	int			atkStart, atkLand, atkEnd;
	int			atkStage;
	vec3_t		atkFrom, atkTo, atkDir;
	int			atkHitMask;
	gentity_t	*atkFx[6];
	int			rootUntil;		// the bot stands still (casting)
	int			holdFireUntil;	// and doesn't shoot

	// lasting effects
	qboolean	frenzy;			// BUTCHER phase 3 / ARCH-DEMON enraged
	int			shieldUntil;
	int			wellUntil, wellStart, wellTick;
	vec3_t		wellOrg;
	int			novaStart, novaCount, novaHitMask;
	vec3_t		novaOrg;
	dsStrike_t	strikes[DS_MAX_STRIKES];
	gentity_t	*props[DS_MAX_PROPS];

	// network
	int			csDirty, csNext;

	// vault
	int			vaultEnd, vaultNextGem, vaultChestAt, vaultGems;
	int			vaultMode;					// DSV_
	gentity_t	*vaultTargets[DS_VAULT_TARGETS];	// GALLERY: the live crystals
	int			vaultTargetNext, vaultTargetCount, vaultBroken;
	float		vaultCredit;				// RUSH: xp earned by speed, not yet dropped

	// round 2: traits (this phase), lives, respawns
	int			traits;			// DST_ mask
	float		tSpeed, tHealth, tDamage;
	int			fullHp;			// a phase's pool before the health trait (power, g_dsBossScale)
	float		scale;			// g_dsBossScale at the floor start
	int			abyssPair;		// the Abyss: the rolled pair
	int			lives;			// g_dsBossLives at the floor start
	int			graceUntil;		// the player just came back: the boss can't hurt them yet
	int			savedWeapons;
	int			savedAmmo[MAX_WEAPONS];
	int			lifeStart;		// level.time the player's current life began
	int			lifeDmg;		// boss pool taken in this life (logs)
	int			totalTaken;		// pool removed this floor, all phases (progress)

	// round 2b: everyone has lives
	int			bossLivesLeft;
	int			deaths[MAX_CLIENTS];		// per fighter (the player: playerDeaths too)
	qboolean	out[MAX_CLIENTS];			// eliminated
	int			kickAt[MAX_CLIENTS];		// eliminated bots leave
	int			takenBy[MAX_CLIENTS];		// boss lives taken by each client
	int			takenByWorld;
	int			grace[MAX_CLIENTS];			// respawn grace end per fighter
	float		hurt[MAX_CLIENTS];			// recent damage dealt to the boss (decays)
	int			hurtDecay;
	int			target;						// the scripted attacks' current target client
	qboolean	roarOnSpawn;				// a new phase: the boss comes back roaring
	qboolean	bossFirstSpawn;
	int			relocations, relocNear;		// logs: boss relocations, how many within 600u of the player
	int			finalBySim;					// the player landed the boss's last life

	// round 4 (en-boss): g_dsBossMode 0 = TIMED 1v1 (the player and the boss, one pool in phases,
	// unlimited player deaths, g_dsBossTime to kill it)
	qboolean	timed;
	int			timeLimit;					// msec
	int			fightStart, fightEnd;		// level times (0 = the clock hasn't started)
	int			killTime;					// msec from fightStart to the kill (0 none)
	int			bossInAt;					// level time the boss first spawned
	qboolean	lastMinute;					// the last-minute enrage
	qboolean	timeUp;
	int			timeBonus;					// skulls for the time left (before the multiplier)

	// g_dsBossSim
	int			simClient;
	int			simAddAt;
	int			simQuitAt;
	qboolean	simLogged;
	int			simRollAtk, simRollTime;		// the attack the dodge roll was made for
	qboolean	simWillDodge;
	int			simReact;
	int			simJumpUntil;
	int			simDodges, simThreats;
} dsActs_t;

static dsActs_t	acts;

#define DS_TANKY_SIZE	1.08f		// a tanky boss is drawn and hit this much bigger (cg_ds_acts.c: the same)
#define DST_FAST		1
#define DST_TANKY		2
#define DST_STRONG		4

/*
Boss archetypes: move speed (bot movement, attack cadence, telegraph speed), health (the pool),
damage (everything it deals). Two strong sides, one weak, per boss and per phase.
*/
typedef struct {
	float	speed, health, damage;
} dsTraits_t;

static const dsTraits_t actsTraits[BOSS_NUM][3] = {
	// THE BUTCHER: fast + strong, frail
	{ { 1.10f, 0.62f, 1.30f }, { 1.10f, 0.62f, 1.30f }, { 1.10f, 0.62f, 1.30f } },
	// THE OVERSEER: tanky + strong, slow
	{ { 0.84f, 1.40f, 1.25f }, { 0.84f, 1.40f, 1.25f }, { 0.84f, 1.40f, 1.25f } },
	// THE HOLLOW KING: fast + tanky, weak hits
	{ { 1.10f, 1.35f, 0.65f }, { 1.10f, 1.35f, 0.65f }, { 1.10f, 1.35f, 0.65f } },
	// THE ARCH-DEMON: fast + strong, then tanky + strong, then fast + tanky
	{ { 1.10f, 0.70f, 1.28f }, { 0.85f, 1.40f, 1.28f }, { 1.10f, 1.35f, 0.70f } }
};
// the Abyss: a rolled pair of these
static const dsTraits_t actsAbyssPairs[3] = {
	{ 1.10f, 0.66f, 1.28f },		// fast + strong
	{ 0.85f, 1.38f, 1.28f },		// tanky + strong
	{ 1.10f, 1.35f, 0.68f }			// fast + tanky
};

/*
Calibration per boss (one life's pool, everything it deals, a rival's damage to it), measured
with tools/boss_sim.py (round 2b): the sim player (a skill 5 bot with the typical kit of the
floor before the boss) + 2 rival bots + the boss, everyone 10 lives, difficulty 3. Target: the
player wins 20-35% of boss floors and takes most of the boss's lives (README "Acts and bosses").
*/
static const float actsCalHp[BOSS_NUM] = { 1.10f, 0.32f, 0.52f, 0.29f };		// x one boss life (DS_BOSS_LIFE_HP)
// a rival bot's damage to the boss (round 2b), so the player takes most of its lives
static const float actsCalRival[BOSS_NUM] = { 0.55f, 0.55f, 0.55f, 0.40f };
static const float actsCalDmg[BOSS_NUM] = { 1.00f, 0.80f, 1.00f, 0.65f };

/*
Round 4 (en-boss), TIMED 1v1 (g_dsBossMode 0): one pool in actsPhases segments, each segment =
ds_bosses[].health x the difficulty x actsCalTimed x the player's power x the phase's health
side x g_dsBossScale. Calibrated with tools/boss_sim.py --mode timed (the sim player alone vs
the boss, 300 s): target ~15-30% sim wins at dodge 0.75 for the first boss (~10 tries for the
player), losses ending with a little of the bar left. Damage: actsCalDmgTimed.
The boss does NOT heal when it kills you: a death costs time (the respawn, the walk back, the
grace), never progress, so the clock stays the only pressure ("die as much as you want").
2026-10-19 (playtest): the sim out-damaged a real player ~3x. The Butcher had 3049 hp a phase
and the player took 13% of it in 2 minutes over 5 deaths, so the pools came down to what a
human deals in 300 s, and the damage with them. Fast is 1.10 (was 1.22: faster than the player).
*/
static const float actsCalTimed[BOSS_NUM] = { 1.10f, 0.50f, 0.58f, 0.42f };		// x ds_bosses[].health per phase
static const float actsCalDmgTimed[BOSS_NUM] = { 0.80f, 0.70f, 0.85f, 0.55f };
#define DS_BOSS_HIT_CAP		0.55f		// one boss hit takes at most this x the player's max health (no one-shots)
#define DS_LAST_MINUTE		60000		// the last-minute enrage: faster attacks
#define DS_FIGHT_DELAY		3000		// the clock starts this long after the boss is in (it holds its fire)
#define DS_TIME_BONUS_SEC	15			// a skull per this many seconds left at the kill

static vmCvar_t	actsCvLives, actsCvScale, actsCvSim, actsCvSimDodge, actsCvSimReact, actsCvMode, actsCvTime, actsCvVault;

static const char *actsBossNet[BOSS_NUM] = { "Butcher", "Overseer", "Hollow King", "Arch-Demon" };

// attacks per boss: attack, first phase it is used, cooldown msec
typedef struct {
	int		atk;
	int		minPhase;
	int		cooldown;
} dsAtkDef_t;

#define DS_ATKS		4
static const dsAtkDef_t actsAtks[BOSS_NUM][DS_ATKS] = {
	{ { DSB_HOOK, 1, 8000 },	{ DSB_CLEAVER, 1, 6500 },	{ DSB_SLAM, 2, 8000 },		{ DSB_NONE, 9, 0 } },
	{ { DSB_BARRAGE, 1, 6500 },	{ DSB_DRONES, 1, 17000 },	{ DSB_MORTAR, 3, 9000 },	{ DSB_NONE, 9, 0 } },
	{ { DSB_LANCE, 1, 6000 },	{ DSB_BLINK, 1, 9000 },		{ DSB_WELL, 2, 13000 },		{ DSB_CLONES, 3, 16000 } },
	{ { DSB_METEOR, 1, 8000 },	{ DSB_NOVA, 1, 7000 },		{ DSB_FIREFLOOR, 2, 9000 },	{ DSB_NONE, 9, 0 } }
};

static void Acts_EndAttack( void );
static void Acts_UpdateConfigstring( void );
gentity_t *G_DS_SpawnDrone( gentity_t *boss, vec3_t org, int slot, int kind );
void GibEntity( gentity_t *self, int killer );		// g_combat.c

/*
===============================================================================

HELPERS

===============================================================================
*/

qboolean G_DS_IsVaultFloor( void ) {
	return acts.vault;
}

qboolean G_DS_IsBossFloor( void ) {
	return acts.on;
}

int G_DS_BossClient( void ) {
	return acts.on ? acts.client : -1;
}

int G_DS_BossType( void ) {
	return acts.on ? acts.type : -1;
}

int G_DS_Act( void ) {
	return G_RL_Active() ? acts.act : 0;
}

const char *G_DS_BossBotName( void ) {
	return acts.type >= 0 && acts.type < BOSS_NUM ? ds_bosses[acts.type].model : "sorlag";
}

const char *G_DS_BossNetName( void ) {
	return acts.type >= 0 && acts.type < BOSS_NUM ? actsBossNet[acts.type] : "Butcher";
}

static gentity_t *Acts_Boss( void ) {
	gentity_t	*ent;

	if ( !acts.on || acts.client < 0 ) {
		return NULL;
	}
	ent = &g_entities[acts.client];
	if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED ) {
		return NULL;
	}
	return ent;
}

static qboolean Acts_Alive( gentity_t *ent ) {
	return ent && ent->inuse && ent->client && ent->health > 0 && ent->client->ps.pm_type == PM_NORMAL
		&& ent->client->sess.sessionTeam != TEAM_SPECTATOR;
}

/*
=================
G_DS_SimClient

g_dsBossSim: the bot named "Sim" that plays the human's part (-1 none)
=================
*/
int G_DS_SimClient( void ) {
	gentity_t	*e;
	int			i;

	if ( !actsCvSim.integer || !G_RL_Active() ) {
		return -1;
	}
	if ( acts.simClient >= 0 && acts.simClient < MAX_CLIENTS ) {
		e = &g_entities[acts.simClient];
		if ( e->inuse && e->client && e->client->pers.connected != CON_DISCONNECTED && ( e->r.svFlags & SVF_BOT )
			&& !Q_stricmp( e->client->pers.netname, "Sim" ) ) {
			return acts.simClient;
		}
	}
	acts.simClient = -1;
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		e = &g_entities[i];
		if ( e->inuse && e->client && e->client->pers.connected != CON_DISCONNECTED && ( e->r.svFlags & SVF_BOT )
			&& !Q_stricmp( e->client->pers.netname, "Sim" ) ) {
			acts.simClient = i;
			break;
		}
	}
	return acts.simClient;
}

// the player's side: the human (or the sim bot)
static qboolean Acts_IsPlayer( gentity_t *ent ) {
	int		n;

	if ( !ent || !ent->client ) {
		return qfalse;
	}
	n = ent - g_entities;
	if ( n == acts.client ) {
		return qfalse;
	}
	return !( ent->r.svFlags & SVF_BOT ) || ( actsCvSim.integer && n == G_DS_SimClient() );
}

// a fighter on a boss floor: the player or a rival bot (not the boss, not eliminated)
static qboolean Acts_Fighter( gentity_t *ent ) {
	int		n;

	if ( !ent || !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED
		|| ent->client->sess.sessionTeam == TEAM_SPECTATOR ) {
		return qfalse;
	}
	n = ent - g_entities;
	return n >= 0 && n < MAX_CLIENTS && n != acts.client && !acts.out[n];
}

// who the scripted attacks hurt: every living fighter, never the boss
static qboolean Acts_Victim( gentity_t *ent ) {
	return Acts_Alive( ent ) && Acts_Fighter( ent );
}

// the player (the human, or the sim bot) if there is one in the game
static gentity_t *Acts_PlayerEnt( void );

/*
=================
Acts_FarSpot

A spawn spot for the boss far from the player: at least DS_RELOC_MIN away, by preference out of
the player's sight, the farther the better (NULL: none at all)
=================
*/
static gentity_t *Acts_FarSpot( gentity_t *self, vec3_t out ) {
	gentity_t	*spot = NULL, *best = NULL, *pl = Acts_PlayerEnt();
	vec3_t		org, eye;
	trace_t		tr;
	float		d, score, bestScore = -1e9f;

	while ( ( spot = G_Find( spot, FOFS( classname ), "info_player_deathmatch" ) ) != NULL ) {
		VectorCopy( spot->s.origin, org );
		org[2] += 9;
		trap_Trace( &tr, org, self->r.mins, self->r.maxs, org, self->s.number, MASK_PLAYERSOLID );
		if ( tr.startsolid || tr.allsolid ) {
			continue;
		}
		score = random() * 100;
		if ( pl && pl->client ) {
			d = Distance( org, pl->r.currentOrigin );
			score += d < DS_RELOC_MIN ? -100000 + d : d;
			VectorCopy( pl->client->ps.origin, eye );
			eye[2] += pl->client->ps.viewheight;
			org[2] += 30;
			trap_Trace( &tr, eye, NULL, NULL, org, pl->s.number, MASK_SOLID );
			if ( tr.fraction < 1.0f ) {
				score += 600;		// out of the player's sight
			}
		}
		if ( score > bestScore ) {
			bestScore = score;
			best = spot;
		}
	}
	if ( best ) {
		VectorCopy( best->s.origin, out );
		out[2] += 9;
	}
	return best;
}

// logs every boss relocation and how near to the player it ended
static void Acts_LogReloc( const char *why, const vec3_t org ) {
	gentity_t	*pl = Acts_PlayerEnt();
	float		d = pl ? Distance( org, pl->r.currentOrigin ) : 99999;

	acts.relocations++;
	if ( d < DS_RELOC_MIN ) {
		acts.relocNear++;
	}
	G_LogPrintf( "DSBoss: relocate %s %.0f from the player%s\n", why, d, d < DS_RELOC_MIN ? " (NEAR)" : "" );
}

static gentity_t *Acts_Human( void ) {
	int		h = G_RL_HumanClient();

	if ( h < 0 || h >= MAX_CLIENTS ) {
		return NULL;
	}
	return &g_entities[h];
}

static gentity_t *Acts_PlayerEnt( void ) {
	gentity_t	*h = Acts_Human();

	return h && h->inuse && h->client ? h : NULL;
}

/*
=================
Acts_PickTarget

Whom the next scripted attack goes for: any living fighter in reach, weighted by who hurt the
boss lately (decaying), the player a touch more. -1 none
=================
*/
static int Acts_PickTarget( gentity_t *boss ) {
	gentity_t	*e;
	float		w[MAX_CLIENTS], sum = 0, r, d;
	int			i, last = -1;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		w[i] = 0;
		e = &g_entities[i];
		if ( !Acts_Victim( e ) || level.time < acts.grace[i] ) {
			continue;
		}
		d = Distance( e->r.currentOrigin, boss->r.currentOrigin );
		if ( d > 1800 ) {
			continue;
		}
		w[i] = 1.0f + ( acts.hurt[i] / 60.0f > 3.0f ? 3.0f : acts.hurt[i] / 60.0f ) + ( Acts_IsPlayer( e ) ? 0.3f : 0.0f );
		if ( d < 1100 ) {
			w[i] += 0.5f;
		}
		sum += w[i];
		last = i;
	}
	if ( sum <= 0 ) {
		return -1;
	}
	r = random() * sum;
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( w[i] <= 0 ) {
			continue;
		}
		if ( r < w[i] ) {
			return i;
		}
		r -= w[i];
	}
	return last;
}

static int Acts_Scaled( int damage ) {
	int		d = (int)( damage * acts.dmgScale + 0.5f );

	return d < 1 ? 1 : d;
}

/*
=================
Acts_Tele

A telegraph time for this boss's speed: fast bosses warn a little quicker, slow ones longer,
never under 0.8 s
=================
*/
static int Acts_Tele( int msec ) {
	float	k = 1.0f + 0.6f * ( acts.tSpeed - 1.0f );
	int		t;

	if ( k < 0.5f ) {
		k = 0.5f;
	}
	t = (int)( msec / k );
	return t < 800 ? 800 : t;
}

/*
=================
Acts_SetTraits

This phase's sides: the ARCH-DEMON changes its pair, Abyss bosses use the rolled pair
=================
*/
static void Acts_SetTraits( void ) {
	const dsTraits_t	*t;
	int					p = acts.phase < 1 ? 0 : acts.phase > 3 ? 2 : acts.phase - 1;

	if ( acts.type < 0 || acts.type >= BOSS_NUM ) {
		return;
	}
	if ( acts.abyss > 0 ) {
		t = &actsAbyssPairs[acts.type == BOSS_ARCHDEMON ? ( acts.abyssPair + p ) % 3 : acts.abyssPair];
	} else {
		t = &actsTraits[acts.type][p];
	}
	acts.tSpeed = t->speed;
	acts.tHealth = t->health;
	acts.tDamage = t->damage;
	acts.traits = ( t->speed > 1.0f ? DST_FAST : 0 ) | ( t->health > 1.0f ? DST_TANKY : 0 ) | ( t->damage > 1.0f ? DST_STRONG : 0 );
	acts.maxHp = (int)( acts.fullHp * acts.tHealth );
	if ( acts.maxHp < 1 ) {
		acts.maxHp = 1;
	}
	acts.csDirty = 1;
}

/*
=================
G_DS_BossProgress

Round 2b: the share of the boss's lives the PLAYER took this floor (0..1; ds-economy pays boss
skulls by it, won or lost)
=================
*/
static float Acts_BarLeft( void );

float G_DS_BossProgress( void ) {
	gentity_t	*pl = Acts_PlayerEnt();
	float		f;

	if ( !acts.on || !pl ) {
		return 0.0f;
	}
	if ( acts.timed ) {
		f = 1.0f - Acts_BarLeft();		// timed 1v1: only the player hurts it
	} else {
		f = (float)acts.takenBy[pl - g_entities] / DS_BOSS_LIVES;
	}
	return f < 0 ? 0 : f > 1 ? 1 : f;
}

// round 4: a timed 1v1 boss floor (g_dsBossMode 0)
qboolean G_DS_BossTimed( void ) {
	return acts.on && acts.timed;
}

// round 4: msec left on the boss clock (the whole limit before it starts, -1 off a timed boss floor)
int G_DS_BossTimeLeft( void ) {
	int		t;

	if ( !acts.on || !acts.timed ) {
		return -1;
	}
	if ( !acts.fightStart ) {
		return acts.timeLimit;
	}
	if ( acts.dead && acts.killTime ) {
		t = acts.timeLimit - acts.killTime;
	} else {
		t = acts.fightEnd - level.time;
	}
	return t < 0 ? 0 : t;
}

// round 4: the skulls for the time left (before the multiplier; 0 unless the boss died in time)
int G_DS_BossTimeBonus( void ) {
	return acts.on && acts.timed && acts.dead ? acts.timeBonus : 0;
}

// what is left of the boss in all (lives and this life's pool / the timed phases), 0..1
static float Acts_BarLeft( void ) {
	float	f, seg;

	if ( !acts.on || acts.dead || acts.bossLivesLeft <= 0 ) {
		return 0.0f;
	}
	seg = acts.maxHp > 0 ? (float)( acts.hp > 0 ? acts.hp : 0 ) / acts.maxHp : 0;
	if ( acts.timed ) {
		f = ( acts.phases - acts.phase + seg ) / ( acts.phases > 0 ? acts.phases : 1 );
	} else {
		f = ( acts.bossLivesLeft - 1 + seg ) / DS_BOSS_LIVES;
	}
	return f < 0 ? 0 : f > 1 ? 1 : f;
}

/*
=================
G_DS_ActsDamageScale

G_RL_DamageScale: the boss's damage side and g_dsBossScale; the nemesis's small edge
=================
*/
float G_DS_ActsDamageScale( gentity_t *targ, gentity_t *attacker ) {
	float	s = 1.0f;

	if ( !G_RL_Active() ) {
		return 1.0f;
	}
	if ( acts.on && attacker && acts.client >= 0 && ( attacker - g_entities ) == acts.client && targ != attacker ) {
		s *= acts.tDamage * acts.scale * ( acts.timed ? actsCalDmgTimed[acts.type] : actsCalDmg[acts.type] );
	}
	s *= G_DS_NemesisDamageScale( targ, attacker );
	return s;
}

static void Acts_Eye( gentity_t *ent, vec3_t out ) {
	VectorCopy( ent->client->ps.origin, out );
	out[2] += ent->client->ps.viewheight;
}

static void Acts_Chest( gentity_t *ent, vec3_t out ) {
	VectorCopy( ent->r.currentOrigin, out );
	out[2] += 8;
}

static qboolean Acts_OnGroundH( gentity_t *ent, float h ) {
	trace_t	tr;
	vec3_t	down;

	if ( ent->client->ps.groundEntityNum != ENTITYNUM_NONE ) {
		return qtrue;
	}
	VectorCopy( ent->r.currentOrigin, down );
	down[2] -= h;		// a few units over the floor still counts (no cheap hop over a shockwave)
	trap_Trace( &tr, ent->r.currentOrigin, ent->r.mins, ent->r.maxs, down, ent->s.number, MASK_PLAYERSOLID );
	return tr.fraction < 1.0f && !tr.startsolid;
}

static qboolean Acts_OnGround( gentity_t *ent ) {
	return Acts_OnGroundH( ent, 44 );
}

/*
=================
Acts_FloorSpot

A floor point near a spot, on our side of the walls
=================
*/
static qboolean Acts_FloorSpot( const vec3_t near, float minR, float maxR, float yaw, vec3_t out ) {
	trace_t	tr;
	vec3_t	from, to, down;
	float	ang, r;
	int		tries;

	VectorCopy( near, from );
	from[2] += 24;
	for ( tries = 0 ; tries < 12 ; tries++ ) {
		ang = yaw >= 0 ? DEG2RAD( yaw ) + crandom() * 0.35f : random() * 2 * M_PI;
		r = minR + random() * ( maxR - minR );
		to[0] = from[0] + cos( ang ) * r;
		to[1] = from[1] + sin( ang ) * r;
		to[2] = from[2];
		trap_Trace( &tr, from, NULL, NULL, to, ENTITYNUM_NONE, MASK_SOLID | CONTENTS_PLAYERCLIP );
		if ( tr.startsolid ) {
			continue;
		}
		VectorCopy( tr.endpos, to );
		if ( tr.fraction < 1.0f ) {
			VectorMA( tr.endpos, 20, tr.plane.normal, to );
		}
		VectorCopy( to, down );
		down[2] -= 400;
		trap_Trace( &tr, to, NULL, NULL, down, ENTITYNUM_NONE, MASK_SOLID | CONTENTS_PLAYERCLIP );
		if ( tr.startsolid || tr.fraction == 1.0f || tr.plane.normal[2] < 0.6f || ( tr.contents & ( CONTENTS_LAVA | CONTENTS_SLIME ) ) ) {
			continue;
		}
		VectorCopy( tr.endpos, out );
		out[2] += 2;
		return qtrue;
	}
	return qfalse;
}

// the floor right under a point (or the point itself)
static void Acts_Ground( const vec3_t p, vec3_t out ) {
	trace_t	tr;
	vec3_t	down;

	VectorCopy( p, down );
	down[2] -= 600;
	trap_Trace( &tr, p, NULL, NULL, down, ENTITYNUM_NONE, MASK_SOLID );
	VectorCopy( tr.endpos, out );
	out[2] += 2;
}

/*
=================
Acts_Fx

A telegraph the cgame draws: kind = DSB_ attack, origin / origin2 = from / to, time2 = when
it lands, radius, frame 0 = warning, 1 = live / fired. Lingers a little after landing.
=================
*/
static gentity_t *Acts_Fx( int kind, const vec3_t from, const vec3_t to, int land, float radius, int linger ) {
	gentity_t	*ent;

	ent = G_Spawn();
	ent->classname = "boss_fx";
	ent->s.eType = ET_GENERAL;
	ent->s.generic1 = RM_ENT_BOSSFX;
	ent->s.modelindex2 = kind;
	ent->s.time = level.time;
	ent->s.time2 = land;
	ent->s.otherEntityNum = acts.client >= 0 ? acts.client : ENTITYNUM_NONE;
	ent->s.otherEntityNum2 = radius < 0 ? 0 : radius > 1023 ? 1023 : (int)radius;
	ent->s.frame = 0;
	ent->r.svFlags |= SVF_BROADCAST;
	VectorCopy( to, ent->s.origin2 );
	G_SetOrigin( ent, (float *)from );
	ent->think = G_FreeEntity;
	ent->nextthink = land + linger;
	trap_LinkEntity( ent );
	return ent;
}

static void Acts_FxMove( gentity_t *fx, const vec3_t from, const vec3_t to ) {
	if ( !fx || !fx->inuse ) {
		return;
	}
	VectorCopy( to, fx->s.origin2 );
	G_SetOrigin( fx, (float *)from );
	trap_LinkEntity( fx );
}

static void Acts_FxFree( gentity_t **fx ) {
	if ( *fx && ( *fx )->inuse && !Q_stricmp( ( *fx )->classname, "boss_fx" ) ) {
		G_FreeEntity( *fx );
	}
	*fx = NULL;
}

// an explosion the cgame draws (rocket blast; meteor look when meteor is set)
static void Acts_Blast( const vec3_t org, qboolean meteor ) {
	gentity_t	*te;
	vec3_t		up = { 0, 0, 1 };

	te = G_TempEntity( (float *)org, EV_MISSILE_MISS );
	te->s.weapon = WP_ROCKET_LAUNCHER;
	te->s.eventParm = DirToByte( up );
	te->s.generic1 = meteor ? RM_MISSILE_METEOR : 0;
	te->r.svFlags |= SVF_BROADCAST;
}

static void Acts_Knock( gentity_t *ent, const vec3_t dir, float speed, float up ) {
	ent->client->ps.velocity[0] += dir[0] * speed;
	ent->client->ps.velocity[1] += dir[1] * speed;
	ent->client->ps.velocity[2] += up;
	ent->client->ps.pm_flags |= PMF_TIME_KNOCKBACK;
	ent->client->ps.pm_time = 250;
}

/*
===============================================================================

INIT

===============================================================================
*/

void G_DS_ActsInit( int nodeFloor, int nodeType, int difficulty, int seed ) {
	static const float	hpScale[6] = { 1.0f, 0.7f, 0.85f, 1.0f, 1.2f, 1.45f };
	int		i;
	float	h;

	static vmCvar_t	actMusic;

	trap_Cvar_Register( &actMusic, "g_dsActMusic", "1", CVAR_ARCHIVE );		// 0: the maps keep their own music
	trap_Cvar_Register( &actsCvLives, "g_dsBossLives", "10", CVAR_ARCHIVE );	// the player's lives on a boss floor
	trap_Cvar_Register( &actsCvScale, "g_dsBossScale", "1.0", CVAR_ARCHIVE );	// boss health and damage, x
	trap_Cvar_Register( &actsCvSim, "g_dsBossSim", "0", 0 );					// tests: a bot plays the human's part
	trap_Cvar_Register( &actsCvSimDodge, "g_dsSimDodge", "0.75", 0 );			// sim: chance to read a telegraph
	trap_Cvar_Register( &actsCvSimReact, "g_dsSimReact", "350", 0 );			// sim: reaction time, msec
	trap_Cvar_Register( &actsCvMode, "g_dsBossMode", "0", CVAR_ARCHIVE );		// 0 timed 1v1, 1 everyone 10 lives (many vs 1)
	trap_Cvar_Register( &actsCvTime, "g_dsBossTime", "300", CVAR_ARCHIVE );		// timed: seconds to kill the boss
	trap_Cvar_Register( &actsCvVault, "g_dsVaultMode", "-1", 0 );				// tests: force a vault mode (DSV_), -1 the seed's
	memset( &acts, 0, sizeof( acts ) );
	acts.client = -1;
	acts.type = -1;
	acts.simClient = -1;
	acts.tSpeed = acts.tHealth = acts.tDamage = 1.0f;
	acts.scale = 1.0f;
	G_DS_NemesisInit();
	if ( !G_RL_Active() ) {
		trap_SetConfigstring( CS_DESCENTBOSS, "" );
		return;
	}
	if ( difficulty < 1 ) {
		difficulty = 1;
	} else if ( difficulty > 5 ) {
		difficulty = 5;
	}
	acts.floor = nodeFloor;
	acts.act = DS_ActForFloor( nodeFloor < 1 ? 1 : nodeFloor );
	acts.difficulty = difficulty;
	acts.seed = seed;
	acts.abyss = nodeFloor > DS_WIN_FLOOR ? ( nodeFloor - DS_WIN_FLOOR + DS_FLOORS_PER_ACT - 1 ) / DS_FLOORS_PER_ACT : 0;
	// the Abyss: small steps every floor past 24 (single digit percent)
	acts.dmgScale = 0.6f + 0.1f * difficulty + 0.01f * ( nodeFloor > DS_WIN_FLOOR ? nodeFloor - DS_WIN_FLOOR : 0 );
	acts.vault = nodeType == RLN_VAULT_GAME;
	if ( acts.vault ) {
		acts.vaultMode = actsCvVault.integer >= 0 && actsCvVault.integer < DSV_NUM ? actsCvVault.integer
			: (int)( ( ( (unsigned)seed * 2654435761u ) ^ ( (unsigned)nodeFloor * 97u ) ) >> 11 ) % DSV_NUM;
		G_LogPrintf( "DSVault: mode %i\n", acts.vaultMode );
	}
	acts.on = nodeType == 2;		// RLN_BOSS
	if ( acts.on ) {
		if ( acts.act >= 1 && acts.act <= DS_NUM_ACTS && ds_acts[acts.act].boss >= 0 ) {
			acts.type = ds_acts[acts.act].boss;
		} else {
			// the Abyss: a random boss, the same one for this floor and seed
			acts.type = (int)( ( (unsigned)seed * 2654435761u + (unsigned)nodeFloor * 40503u ) >> 7 ) % BOSS_NUM;
			if ( acts.type < 0 ) {
				acts.type = -acts.type;
			}
		}
		acts.phases = ds_bosses[acts.type].phases > 0 ? ds_bosses[acts.type].phases : 3;
		acts.phase = 1;
		acts.abyssPair = (int)( ( (unsigned)seed * 40503u + (unsigned)nodeFloor * 2654435761u ) >> 9 ) % 3;
		acts.scale = actsCvScale.value > 0.2f ? ( actsCvScale.value < 4.0f ? actsCvScale.value : 4.0f ) : 0.2f;
		acts.lives = actsCvLives.integer < 1 ? 1 : actsCvLives.integer > 30 ? 30 : actsCvLives.integer;
		h = ds_bosses[acts.type].health;
		if ( acts.abyss > 0 && h < ds_bosses[BOSS_ARCHDEMON].health ) {
			h = ds_bosses[BOSS_ARCHDEMON].health;
		}
		h *= hpScale[difficulty] * ( 1.0f + 0.06f * ( nodeFloor > DS_WIN_FLOOR ? nodeFloor - DS_WIN_FLOOR : 0 ) );
		acts.timed = actsCvMode.integer == 0 || G_RL_NoSave();		// (boss practice and the daily are always timed)
		if ( acts.timed ) {
			// round 4: one pool in phases (a segment per phase), one "life", a clock
			acts.timeLimit = ( actsCvTime.integer < 60 ? 60 : actsCvTime.integer > 1800 ? 1800 : actsCvTime.integer ) * 1000;
			if ( G_RL_Daily() ) {
				acts.timeLimit = 300000;		// en-daily: the same clock for everyone (DSD_BOSS_TIME)
			}
			h *= actsCalTimed[acts.type];		// one phase's segment
			h *= G_DS_RmHealthScale();			// en-rematch: IRON
			acts.timeLimit = G_DS_RmTimeLimit( acts.timeLimit );		// en-rematch: HALF CLOCK (+ the rematch log line)
		} else {
			h *= actsCalHp[acts.type] * DS_BOSS_LIFE_HP;		// one of its DS_BOSS_LIVES lives
		}
		acts.baseHp = (int)h;		// x the player's power once they are in (Acts_PowerScale)
		acts.fullHp = (int)( h * acts.scale );
		Acts_SetTraits();		// maxHp = fullHp x this phase's health side
		acts.hp = acts.maxHp;
		acts.pace = 0.8f + 0.1f * ( 5 - difficulty );
		for ( i = 0 ; i < DSB_NUM ; i++ ) {
			acts.cooldown[i] = 0;
		}
		acts.nextAttack = level.time + 6000;		// the floor intro first
		acts.lastNear = level.time + 4000;
		acts.bossLivesLeft = acts.timed ? 1 : DS_BOSS_LIVES;
		acts.target = -1;
		acts.bossFirstSpawn = qtrue;
		G_LogPrintf( "DSBoss: floor %i act %i boss %i %s hp %i x %i phases, abyss %i, traits %i (speed %.2f health %.2f damage %.2f) scale %.2f lives %i mode %s time %i\n",
			nodeFloor, acts.act, acts.type, ds_bosses[acts.type].name, acts.maxHp, acts.phases, acts.abyss,
			acts.traits, acts.tSpeed, acts.tHealth, acts.tDamage, acts.scale, acts.lives, acts.timed ? "timed" : "lives",
			acts.timeLimit / 1000 );
	}
	acts.csDirty = 1;
	Acts_UpdateConfigstring();
}

/*
=================
G_DS_ActsStart

After the map's entities spawned: the act's music (the player's own Q3 tracks), a combat
track on boss floors
=================
*/
void G_DS_ActsStart( void ) {
	const char	*music;

	if ( !G_RL_Active() || !trap_Cvar_VariableIntegerValue( "g_dsActMusic" ) ) {
		return;
	}
	if ( acts.on ) {
		music = acts.act >= DS_ACT_ABYSS || acts.type == BOSS_ARCHDEMON ? "music/fla22k_04_intro music/fla22k_04_loop"
			: acts.type == BOSS_HOLLOWKING ? "music/fla22k_06" : acts.type == BOSS_OVERSEER ? "music/fla22k_05" : "music/fla22k_03";
	} else if ( acts.vault ) {
		music = "music/sonic2";
	} else {
		music = ds_acts[acts.act >= 1 && acts.act <= DS_ACT_ABYSS ? acts.act : 1].music;
	}
	if ( music && music[0] ) {
		trap_SetConfigstring( CS_MUSIC, music );
	}
}

/*
=================
G_DS_BossSpawn

The boss (re)spawned: its kit weapons, full health for the bot's own judgement
=================
*/
void G_DS_BossSpawn( gentity_t *ent ) {
	gclient_t	*cl = ent->client;
	int			w1 = WP_SHOTGUN, w2 = WP_GAUNTLET;

	if ( !acts.on ) {
		return;
	}
	if ( acts.client != ent - g_entities ) {
		// first in: it doesn't open fire for 3 s (the floor intro, the player finds it)
		acts.holdFireUntil = level.time + 3000;
		// ds-arrivals: it may arrive well after the floor start; its scripted attacks wait too
		if ( acts.nextAttack < level.time + 5000 ) {
			acts.nextAttack = level.time + 5000;
		}
		if ( !acts.bossInAt ) {
			acts.bossInAt = level.time;		// timed: the clock starts DS_FIGHT_DELAY later
		}
	}
	acts.client = ent - g_entities;
	acts.out[acts.client] = qfalse;
	switch ( acts.type ) {
	case BOSS_OVERSEER:		w1 = WP_ROCKET_LAUNCHER; w2 = WP_MACHINEGUN; break;
	case BOSS_HOLLOWKING:	w1 = WP_PLASMAGUN; w2 = WP_RAILGUN; break;
	case BOSS_ARCHDEMON:	w1 = WP_ROCKET_LAUNCHER; w2 = WP_LIGHTNING; break;
	default:				w1 = WP_SHOTGUN; w2 = WP_GAUNTLET; break;
	}
	cl->ps.stats[STAT_WEAPONS] |= ( 1 << w1 ) | ( 1 << w2 ) | ( 1 << WP_GAUNTLET );
	if ( cl->ps.ammo[w1] < 60 ) {
		cl->ps.ammo[w1] = 60;
	}
	if ( w2 != WP_GAUNTLET && cl->ps.ammo[w2] < 80 ) {
		cl->ps.ammo[w2] = 80;
	}
	cl->ps.weapon = w1;
	cl->ps.weaponstate = WEAPON_READY;
	acts.csDirty = 1;
	G_LogPrintf( "DSBoss: spawned client %i (%s)\n", acts.client, cl->pers.netname );
}

/*
=================
Acts_PowerScale

The boss grows with the player's real power (weapon levels, passives, cards: LOOT's
G_DS_PowerScore, 1.0 = a fresh run), clamped so a strong build still feels strong
=================
*/
static void Acts_PowerScale( gentity_t *human ) {
	float	p = G_DS_RmPowerScore( human );		// (= G_DS_PowerScore; en-rematch: GLASS doesn't shrink the boss)
	int		full;

	acts.powerSet = qtrue;
	if ( p < 1.0f ) {
		p = 1.0f;
	} else if ( p > 2.5f ) {
		p = 2.5f;
	}
	full = (int)( acts.baseHp * p * acts.scale * acts.tHealth );
	if ( acts.phase == 1 && acts.hp == acts.maxHp ) {
		acts.hp = full;
	} else {
		acts.hp = (int)( (float)acts.hp * full / ( acts.maxHp > 0 ? acts.maxHp : 1 ) );
	}
	acts.fullHp = (int)( acts.baseHp * p * acts.scale );
	acts.maxHp = full;
	acts.csDirty = 1;
	G_LogPrintf( "DSBoss: power %.2f -> %i per phase (health side %.2f)\n", p, acts.maxHp, acts.tHealth );
}

/*
===============================================================================

DAMAGE, PHASES, DEATH

===============================================================================
*/

static void Acts_Roar( gentity_t *boss ) {
	gentity_t	*ent;
	vec3_t		d;
	float		dist;
	int			i;

	acts.roarUntil = level.time + DS_ROAR_TIME;
	acts.rootUntil = acts.roarUntil;
	acts.holdFireUntil = acts.roarUntil;
	Acts_Fx( DSB_ROAR, boss->r.currentOrigin, boss->r.currentOrigin, level.time + 300, 320, DS_ROAR_TIME );
	// the shout throws everyone near back a little (no damage)
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		ent = &g_entities[i];
		if ( !Acts_Victim( ent ) ) {
			continue;
		}
		VectorSubtract( ent->r.currentOrigin, boss->r.currentOrigin, d );
		d[2] = 0;
		dist = VectorNormalize( d );
		if ( dist < 320 ) {
			Acts_Knock( ent, d, 520 * ( 1.0f - dist / 400.0f ), 220 );
		}
	}
	G_SlowMo( 0.6f, 600 );
}

// the phase for the boss's lives left: 1 (10-7), 2 (6-4), 3 (3-1)
static int Acts_PhaseForLives( int left ) {
	return left >= 7 ? 1 : left >= 4 ? 2 : 3;
}

static void Acts_SetPhase( int phase ) {
	acts.phase = phase;
	Acts_SetTraits();		// the ARCH-DEMON's next pair
	acts.pace = ( 0.8f + 0.1f * ( 5 - acts.difficulty ) ) * ( phase >= 3 ? 0.82f * 0.82f : phase >= 2 ? 0.82f : 1.0f );
	if ( phase >= acts.phases && ( acts.type == BOSS_BUTCHER || acts.type == BOSS_ARCHDEMON ) ) {
		acts.frenzy = qtrue;
	}
	acts.csDirty = 1;
	G_LogPrintf( "DSBoss: phase %i of %i (life hp %i, traits %i speed %.2f health %.2f damage %.2f, lives left %i, player deaths %i)\n",
		acts.phase, acts.phases, acts.maxHp, acts.traits, acts.tSpeed, acts.tHealth, acts.tDamage, acts.bossLivesLeft, acts.playerDeaths );
}

static void Acts_FreeProps( void ) {
	int		i;

	for ( i = 0 ; i < DS_MAX_PROPS ; i++ ) {
		if ( acts.props[i] && acts.props[i]->inuse && acts.props[i]->s.generic1 == RM_ENT_BOSSPROP ) {
			G_FreeEntity( acts.props[i] );
		}
		acts.props[i] = NULL;
	}
}

static void Acts_ClearStrikes( void ) {
	int		i;

	for ( i = 0 ; i < DS_MAX_STRIKES ; i++ ) {
		if ( acts.strikes[i].kind ) {
			Acts_FxFree( &acts.strikes[i].fx );
			acts.strikes[i].kind = 0;
		}
	}
}

// the boss's helper drones / decoys / its attack in progress are gone with a boss life
static void Acts_BossDown( void ) {
	Acts_EndAttack();
	Acts_FreeProps();
	Acts_ClearStrikes();
	acts.shieldUntil = 0;
	acts.wellUntil = 0;
	acts.novaStart = 0;
}

/*
=================
G_DS_BossDamage

From G_Damage right before the health comes off: returns what really comes off.
Boss floors: everyone fights everyone; a fighter just back from a death can't be hurt by the
boss; the boss's pool takes the hits (the entity itself stays healthy) and an empty pool is a
real death (a boss life).
=================
*/
int G_DS_BossDamage( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, int take, int mod ) {
	gentity_t	*spot;
	vec3_t		org;
	int			num, a;

	if ( !acts.on || take <= 0 || !targ ) {
		return take;
	}
	// drones and decoys: anyone but the boss breaks them
	if ( !targ->client ) {
		if ( targ->s.eType == ET_GENERAL && targ->s.generic1 == RM_ENT_BOSSPROP
			&& ( !attacker || !attacker->client || ( attacker - g_entities ) == acts.client ) ) {
			return 0;
		}
		return take;
	}
	num = targ - g_entities;
	a = attacker ? attacker - g_entities : -1;
	// a fighter just came back: nothing of the boss's touches them yet
	if ( num != acts.client && num >= 0 && num < MAX_CLIENTS && level.time < acts.grace[num] && attacker
		&& ( a == acts.client || ( attacker->r.ownerNum == acts.client && !attacker->client ) ) ) {
		return 0;
	}
	// blood frenzy: the butcher heals a little from what it deals
	if ( acts.frenzy && acts.type == BOSS_BUTCHER && a == acts.client && num != acts.client
		&& !acts.dead && acts.phase >= acts.phases ) {
		acts.hp += take / 4;
		if ( acts.hp > acts.maxHp ) {
			acts.hp = acts.maxHp;
		}
		acts.csDirty = 1;
	}
	if ( num != acts.client ) {
		return take;
	}
	if ( acts.dead ) {
		return take;
	}
	if ( acts.timeUp ) {
		return 0;		// timed: the clock ran out, nothing counts any more
	}
	// the void / lava / crushers don't take a boss life: back to a spawn spot far from the player
	if ( !attacker || !attacker->client || attacker == targ ) {
		if ( mod == MOD_TRIGGER_HURT || mod == MOD_LAVA || mod == MOD_SLIME || mod == MOD_CRUSH ) {
			spot = Acts_FarSpot( targ, org );
			if ( spot ) {
				vec3_t	ang;

				VectorCopy( spot->s.angles, ang );
				TeleportPlayer( targ, org, ang );
				Acts_LogReloc( "rescue", org );
			}
		}
		return 0;
	}
	if ( level.time < acts.roarUntil || mod == MOD_TELEFRAG ) {
		return 0;
	}
	if ( acts.shieldUntil > level.time ) {
		take = take / 8;		// the shield holds while the drones live
		if ( take < 1 ) {
			return 0;
		}
	}
	// the rivals chip at it, the player is the one who really cuts it down
	if ( attacker->client && ( attacker->r.svFlags & SVF_BOT ) && !Acts_IsPlayer( attacker ) ) {
		take = (int)( take * actsCalRival[acts.type] + 0.5f );
		if ( take < 1 ) {
			take = 1;
		}
	}
	if ( a >= 0 && a < MAX_CLIENTS ) {
		acts.hurt[a] += take;
	}
	if ( Acts_IsPlayer( attacker ) ) {
		acts.lifeDmg += take < acts.hp ? take : acts.hp;
	}
	acts.totalTaken += take < acts.hp ? take : acts.hp;
	acts.hp -= take;
	acts.csDirty = 1;
	if ( acts.hp > 0 ) {
		return 0;
	}
	// timed 1v1: a phase threshold is no death: it roars where it stands (a short invulnerable
	// moment), and the next phase's segment begins
	if ( acts.timed && acts.phase < acts.phases ) {
		vec3_t	gem;

		Acts_BossDown();
		Acts_SetPhase( acts.phase + 1 );
		acts.hp = acts.maxHp;
		VectorCopy( targ->r.currentOrigin, gem );
		gem[2] += 16;
		G_DS_SpawnGems( gem, 20 + 10 * acts.act, -1 );
		Acts_Roar( targ );
		acts.nextAttack = acts.roarUntil + 600;
		if ( a >= 0 && a < MAX_CLIENTS ) {
			acts.takenBy[a]++;
		}
		acts.csDirty = 1;
		G_LogPrintf( "DSBoss: timed phase %i at %i s (%i s left), player deaths %i\n", acts.phase,
			acts.fightStart ? ( level.time - acts.fightStart ) / 1000 : 0, G_DS_BossTimeLeft() / 1000, acts.playerDeaths );
		return 0;
	}
	// a boss life is over: a real death (the last one gibs)
	acts.hp = 0;
	return targ->health + ( acts.bossLivesLeft <= 1 ? 400 : 100 );
}

// a bot or the boss out of lives leaves the arena: a burst where it fell, then it's gone
static void Acts_Eliminate( gentity_t *self ) {
	int		n = self - g_entities;
	gentity_t	*te;

	acts.out[n] = qtrue;
	te = G_TempEntity( self->r.currentOrigin, EV_PLAYER_TELEPORT_OUT );
	te->s.clientNum = n;
	if ( self->health > GIB_HEALTH ) {
		GibEntity( self, 0 );
	}
	if ( self->r.svFlags & SVF_BOT ) {
		acts.kickAt[n] = level.time + 1500;
	}
	trap_SendServerCommand( -1, va( "dsnem 3 %i 0", n ) );
	acts.csDirty = 1;
	G_LogPrintf( "DSBoss: %s eliminated\n", self->client->pers.netname );
}

void G_DS_ActsPlayerDie( gentity_t *self, gentity_t *attacker ) {
	vec3_t	org;
	int		n, a, newPhase;

	if ( !acts.on || !self || !self->client ) {
		return;
	}
	n = self - g_entities;
	a = attacker && attacker->client ? attacker - g_entities : -1;
	if ( n == acts.client ) {
		if ( acts.dead || acts.hp > 0 ) {
			return;		// (a stray death: it respawns with its pool)
		}
		acts.bossLivesLeft--;
		if ( a >= 0 && a < MAX_CLIENTS && a != n ) {
			acts.takenBy[a]++;
		} else {
			acts.takenByWorld++;
		}
		Acts_BossDown();
		VectorCopy( self->r.currentOrigin, org );
		org[2] += 16;
		G_LogPrintf( "DSBoss: boss life taken by %s (%s), %i left, player took %i\n", a >= 0 ? level.clients[a].pers.netname : "world",
			a >= 0 && Acts_IsPlayer( &g_entities[a] ) ? "player" : "other", acts.bossLivesLeft,
			Acts_PlayerEnt() ? acts.takenBy[Acts_PlayerEnt() - g_entities] : 0 );
		if ( acts.bossLivesLeft > 0 ) {
			G_DS_SpawnGems( org, 8 + 4 * acts.act, -1 );
			newPhase = Acts_PhaseForLives( acts.bossLivesLeft );
			if ( newPhase > acts.phase ) {
				Acts_SetPhase( newPhase );
				acts.roarOnSpawn = qtrue;
				G_DS_SpawnGems( org, 20 + 10 * acts.act, -1 );
			}
			acts.hp = acts.maxHp;		// the next life (it respawns with it)
			acts.csDirty = 1;
			return;
		}
		// the last life: the boss is beaten
		acts.dead = qtrue;
		acts.deathTime = level.time;
		if ( acts.timed ) {
			int		left;

			if ( !acts.fightStart ) {
				acts.fightStart = level.time;		// (a dev kill before the clock ran)
				acts.fightEnd = level.time + acts.timeLimit;
			}
			acts.killTime = level.time - acts.fightStart;
			if ( acts.killTime < 1 ) {
				acts.killTime = 1;
			}
			left = acts.fightEnd - level.time;
			acts.timeBonus = left > 0 ? left / 1000 / DS_TIME_BONUS_SEC : 0;
			G_LogPrintf( "DSBoss: timed kill in %i.%i s, %i s left, time bonus %i skulls, player deaths %i\n",
				acts.killTime / 1000, ( acts.killTime % 1000 ) / 100, left > 0 ? left / 1000 : 0, acts.timeBonus, acts.playerDeaths );
		}
		G_DS_SpawnChest( org, 2 );
		G_DS_SpawnGems( org, 200 + 60 * acts.act, -1 );
		Acts_Eliminate( self );		// (its callout first: the final-blow one replaces it)
		if ( a >= 0 && Acts_IsPlayer( &g_entities[a] ) ) {
			// the final blow was yours: skulls and a second chest
			int		sk = G_DS_Skulls( 5 + acts.floor / 3 );

			G_RL_AddSkulls( sk );
			acts.finalBySim = 1;
			org[2] += 30;
			G_DS_SpawnChest( org, 1 );
			trap_SendServerCommand( -1, va( "dsnem 4 %i %i", n, sk ) );
			G_LogPrintf( "DSBoss: final blow by the player: +%i skulls, a second chest\n", sk );
		}
		G_DS_BossDefeated( acts.type );
		G_SlowMo( 0.4f, 700 );
		acts.csDirty = 1;
		G_LogPrintf( "DSBoss: defeated %s (player deaths %i, lives taken by the player %i, by others %i)\n", ds_bosses[acts.type].name,
			acts.playerDeaths, Acts_PlayerEnt() ? acts.takenBy[Acts_PlayerEnt() - g_entities] : 0,
			DS_BOSS_LIVES - ( Acts_PlayerEnt() ? acts.takenBy[Acts_PlayerEnt() - g_entities] : 0 ) );
		return;
	}
	if ( acts.dead || n < 0 || n >= MAX_CLIENTS || acts.out[n] ) {
		return;
	}
	acts.deaths[n]++;
	acts.hurt[n] = 0;
	if ( acts.target == n && acts.atk ) {
		Acts_EndAttack();		// the attack aimed at them is called off
	}
	if ( Acts_IsPlayer( self ) ) {
		int		w;

		acts.playerDeaths++;
		acts.csDirty = 1;
		// the next life starts fresh: the guns the player found come back with them
		acts.savedWeapons = self->client->ps.stats[STAT_WEAPONS];
		for ( w = 0 ; w < MAX_WEAPONS ; w++ ) {
			acts.savedAmmo[w] = self->client->ps.ammo[w];
		}
		G_LogPrintf( "DSBoss: player died (%i of %i) by %s mod %i, life %i ms, boss took %i this life, bar %.3f, phase %i\n",
			acts.playerDeaths, G_DS_PlayerBossLives( acts.lives ), attacker && attacker->client ? attacker->client->pers.netname : "world",
			self->client->lasthurt_mod, level.time - acts.lifeStart, acts.lifeDmg, Acts_BarLeft(), acts.phase );
		acts.lifeDmg = 0;
		return;
	}
	// a rival bot
	acts.csDirty = 1;
	if ( acts.deaths[n] >= acts.lives ) {
		Acts_Eliminate( self );
	}
}

/*
=================
G_DS_ActsPlayerSpawn

G_RL_SpawnLoadout, last, for everyone on a boss floor. A fighter back from a death comes in
700 - 1400 units from the boss, by preference out of its sight, facing it, with a short grace
(the player also with the guns they had). The boss back from a lost life comes in far from the
player (never within DS_RELOC_MIN), roaring when a new phase begins.
=================
*/
void G_DS_ActsPlayerSpawn( gentity_t *ent ) {
	gentity_t	*boss, *spot, *best = NULL;
	gclient_t	*cl;
	vec3_t		d, ang, org;
	float		dist, score, bestScore = 1e9f;
	trace_t		tr;
	int			w, n;

	if ( !acts.on || acts.dead || !ent || !ent->client ) {
		return;
	}
	cl = ent->client;
	n = ent - g_entities;
	if ( n == acts.client ) {
		if ( acts.bossFirstSpawn ) {
			gentity_t	*pl = Acts_PlayerEnt();

			acts.bossFirstSpawn = qfalse;
			// timed 1v1 (no staggered entrance when g_dsStagger is 0): never in the player's face
			if ( acts.timed && pl && Distance( cl->ps.origin, pl->r.currentOrigin ) < DS_RELOC_MIN ) {
				spot = Acts_FarSpot( ent, org );
				if ( spot ) {
					VectorCopy( org, cl->ps.origin );
					VectorClear( cl->ps.velocity );
					VectorCopy( spot->s.angles, ang );
					SetClientViewAngle( ent, ang );
					Acts_LogReloc( "first spawn", org );
				}
			}
			return;
		}
		// the boss's next life: far from the player, a short invulnerable moment
		spot = Acts_FarSpot( ent, org );
		if ( spot ) {
			VectorCopy( org, cl->ps.origin );
			VectorClear( cl->ps.velocity );
			VectorCopy( spot->s.angles, ang );
			SetClientViewAngle( ent, ang );
			Acts_LogReloc( "respawn", org );
		}
		acts.hp = acts.maxHp;
		acts.roarUntil = level.time + 1500;
		acts.holdFireUntil = level.time + 1500;
		acts.nextAttack = level.time + 2500;
		acts.lastNear = level.time;
		if ( acts.roarOnSpawn ) {
			acts.roarOnSpawn = qfalse;
			VectorCopy( org, ent->r.currentOrigin );
			Acts_Roar( ent );
			acts.nextAttack = acts.roarUntil + 600;
		}
		acts.csDirty = 1;
		return;
	}
	if ( n < 0 || n >= MAX_CLIENTS || acts.out[n] ) {
		return;
	}
	if ( Acts_IsPlayer( ent ) ) {
		acts.lifeStart = level.time;
		acts.lifeDmg = 0;
	}
	if ( acts.deaths[n] <= 0 ) {
		return;		// the floor's first spawn
	}
	acts.grace[n] = level.time + DS_GRACE_TIME;
	if ( Acts_IsPlayer( ent ) ) {
		acts.graceUntil = acts.grace[n];
		// the guns come back (at least half their ammo)
		cl->ps.stats[STAT_WEAPONS] |= acts.savedWeapons & ~( 1 << WP_GRAPPLING_HOOK );
		for ( w = 1 ; w < MAX_WEAPONS ; w++ ) {
			if ( ( acts.savedWeapons & ( 1 << w ) ) && cl->ps.ammo[w] < acts.savedAmmo[w] / 2 ) {
				cl->ps.ammo[w] = acts.savedAmmo[w] / 2;
			}
			if ( ( acts.savedWeapons & ( 1 << w ) ) && cl->ps.ammo[w] < 10 && w != WP_GAUNTLET ) {
				cl->ps.ammo[w] = 10;
			}
		}
	}
	// where: a spawn spot 700 - 1400 units from the boss, by preference out of its sight
	boss = Acts_Boss();
	if ( !boss || boss->health <= 0 ) {
		return;
	}
	spot = NULL;
	while ( ( spot = G_Find( spot, FOFS( classname ), "info_player_deathmatch" ) ) != NULL ) {
		VectorSubtract( spot->s.origin, boss->r.currentOrigin, d );
		dist = VectorLength( d );
		score = dist < 700 ? 3000 + ( 700 - dist ) * 4 : dist > 1400 ? dist - 1400 : 0;
		VectorCopy( spot->s.origin, org );
		org[2] += 40;
		trap_Trace( &tr, boss->r.currentOrigin, NULL, NULL, org, boss->s.number, MASK_SOLID );
		if ( tr.fraction >= 1.0f ) {
			score += 800;		// it would see you come in
		}
		score += random() * 150;
		if ( score < bestScore ) {
			bestScore = score;
			best = spot;
		}
	}
	if ( !best ) {
		return;
	}
	VectorCopy( best->s.origin, org );
	org[2] += 9;
	trap_Trace( &tr, org, ent->r.mins, ent->r.maxs, org, ent->s.number, MASK_PLAYERSOLID );
	if ( tr.startsolid || tr.allsolid ) {
		return;
	}
	VectorCopy( org, cl->ps.origin );
	VectorClear( cl->ps.velocity );
	VectorSubtract( boss->r.currentOrigin, org, d );
	vectoangles( d, ang );
	ang[PITCH] = 0;
	ang[ROLL] = 0;
	SetClientViewAngle( ent, ang );
	if ( Acts_IsPlayer( ent ) ) {
		G_LogPrintf( "DSBoss: player respawn %.0f units from the boss (grace 2.5 s)\n", Distance( org, boss->r.currentOrigin ) );
	}
}

// g_dsBossSim: one result line, then the server quits (the harness starts the next run)
static void Acts_SimResult( int result ) {
	gentity_t	*sim;
	float		p;

	if ( acts.simLogged ) {
		return;
	}
	acts.simLogged = qtrue;
	acts.simQuitAt = level.time + ( result == 1 ? 1500 : 500 );
	sim = Acts_Human();
	p = G_DS_PowerScore( sim );
	{
		int		i, mine = sim ? acts.takenBy[sim - g_entities] : 0, rivals = 0, outs = 0;

		for ( i = 0 ; i < MAX_CLIENTS ; i++ ) {
			if ( i != acts.client && ( !sim || i != sim - g_entities ) ) {
				rivals += acts.takenBy[i];
				if ( acts.out[i] ) {
					outs++;
				}
			}
		}
		G_LogPrintf( "DSSim: result %s boss %s floor %i diff %i lives_used %i of %i bar_left %.3f boss_lives_left %i taken_sim %i taken_rivals %i taken_world %i rivals_out %i final_by_sim %i phase %i time %i power %.2f hp %i traits %i scale %.2f dodges %i of %i reloc %i near %i\n",
			result == 1 ? "WIN" : "LOSS", ds_bosses[acts.type].name, acts.floor, acts.difficulty,
			acts.playerDeaths + ( result == 1 ? 1 : 0 ), acts.lives, Acts_BarLeft(), acts.bossLivesLeft, mine, rivals,
			acts.takenByWorld, outs, acts.finalBySim, acts.phase, level.time - level.startTime, p, acts.maxHp, acts.traits, acts.scale,
			acts.simDodges, acts.simThreats, acts.relocations, acts.relocNear );
		G_LogPrintf( "DSSimT: mode %s deaths %i kill_ms %i limit_ms %i fight_ms %i bar_left %.3f phase %i\n",
			acts.timed ? "timed" : "lives", acts.playerDeaths, acts.killTime, acts.timeLimit,
			acts.fightStart ? level.time - acts.fightStart : 0, Acts_BarLeft(), acts.phase );
	}
}

/*
=================
G_DS_FloorResult

Boss and vault floors decide their own end (G_RL_CheckExitRules)
=================
*/
int G_DS_FloorResult( void ) {
	if ( acts.vault ) {
		return acts.vaultEnd && level.time >= acts.vaultEnd ? 1 : 0;
	}
	if ( !acts.on ) {
		return 0;
	}
	if ( acts.dead ) {
		if ( acts.simQuitAt == 0 && actsCvSim.integer ) {
			Acts_SimResult( 1 );
		}
		return level.time >= acts.deathTime + DS_WIN_DELAY ? 1 : 0;
	}
	if ( acts.timed ) {
		// timed 1v1: deaths don't decide it, the clock does
		if ( acts.timeUp ) {
			if ( actsCvSim.integer ) {
				Acts_SimResult( 2 );
			}
			return level.time >= acts.deathTime + 2500 ? 2 : 0;		// a beat to see TIME UP
		}
		return 0;
	}
	if ( acts.playerDeaths >= G_DS_PlayerBossLives( acts.lives ) ) {		// (+ LAST BREATH, ds-economy)
		if ( actsCvSim.integer ) {
			Acts_SimResult( 2 );
		}
		return 2;
	}
	// no clock on a boss fight (lives decide it); a sim stops after 30 minutes
	if ( actsCvSim.integer && level.time - level.startTime >= 30 * 60000 ) {
		Acts_SimResult( 2 );
		return 2;
	}
	return 0;
}

/*
===============================================================================

BOT HOOKS

===============================================================================
*/

void G_DS_BossBounds( gentity_t *ent ) {
	float	s;

	if ( !acts.on || !ent || ( ent - g_entities ) != acts.client || acts.type < 0 || ent->health <= 0 ) {
		return;
	}
	s = ds_bosses[acts.type].scale * ( ( acts.traits & DST_TANKY ) ? DS_TANKY_SIZE : 1.0f );		// (cgame: the same)
	ent->r.mins[0] *= s;
	ent->r.mins[1] *= s;
	ent->r.maxs[0] *= s;
	ent->r.maxs[1] *= s;
	ent->r.maxs[2] = ( ent->r.maxs[2] + 24 ) * s - 24;		// the feet stay where they are
}

float G_DS_SpeedScale( gentity_t *ent ) {
	if ( !acts.on || !ent || ( ent - g_entities ) != acts.client ) {
		return 1.0f;
	}
	return acts.tSpeed * ( acts.frenzy ? ( acts.type == BOSS_BUTCHER ? 1.05f : 1.04f ) : 1.0f ) * ( acts.lastMinute ? 1.04f : 1.0f );
}

/*
=================
G_DS_BossHitCap

From G_Damage (before armor): one hit from the boss never takes more than DS_BOSS_HIT_CAP of a
human's max health, so a point-blank shotgun or a grenade to the face is never a one-shot
=================
*/
int G_DS_BossHitCap( gentity_t *targ, gentity_t *attacker, int damage ) {
	int		cap;

	if ( !acts.on || !attacker || acts.client < 0 || ( attacker - g_entities ) != acts.client || targ == attacker
		|| !targ || !targ->client || ( targ->r.svFlags & SVF_BOT ) ) {
		return damage;
	}
	cap = (int)( ( targ->client->ps.stats[STAT_MAX_HEALTH] > 0 ? targ->client->ps.stats[STAT_MAX_HEALTH] : 100 ) * DS_BOSS_HIT_CAP );
	return damage > cap ? cap : damage;
}

/*
===============================================================================

SIM PLAYER (g_dsBossSim): a skill 5 bot with the human's growth plays the player's part.
Bots don't see telegraphs, so this models a human's read of them: every attack aimed at
the sim is read with chance g_dsSimDodge, after g_dsSimReact msec (+0..150); a read
telegraph is dodged the way the HUD teaches (step off a line or lane, run out of a circle,
jump a slam or a fire ring). An unread one is eaten.

===============================================================================
*/

// distance from p to the segment a-b (2D), and which side of it p is on
static float Acts_SegDist( const vec3_t a, const vec3_t b, const vec3_t p, vec3_t away ) {
	vec3_t	ab, ap, c;
	float	t, l2;

	VectorSubtract( b, a, ab );
	ab[2] = 0;
	VectorSubtract( p, a, ap );
	ap[2] = 0;
	l2 = DotProduct( ab, ab );
	t = l2 > 0 ? DotProduct( ap, ab ) / l2 : 0;
	if ( t < 0 ) {
		t = 0;
	} else if ( t > 1 ) {
		t = 1;
	}
	VectorMA( a, t, ab, c );
	c[2] = p[2];
	// away: perpendicular to the line, on p's side
	VectorSet( away, -ab[1], ab[0], 0 );
	VectorNormalize( away );
	if ( DotProduct( away, ap ) < 0 ) {
		VectorNegate( away, away );
	}
	return sqrt( ( p[0] - c[0] ) * ( p[0] - c[0] ) + ( p[1] - c[1] ) * ( p[1] - c[1] ) );
}

static void Acts_SimDodge( gentity_t *ent, usercmd_t *ucmd ) {
	vec3_t		p, away, best, d, a, b, ang, fwd, right;
	float		dist, urgency = 0, u, R;
	qboolean	jump = qfalse;
	int			i, key, land = 0;
	gentity_t	*fx;

	if ( !Acts_Alive( ent ) ) {
		return;
	}
	VectorCopy( ent->r.currentOrigin, p );
	VectorClear( best );
	// one roll per attack (the attack start time is its key; strikes by their land time)
	key = acts.atk ? acts.atkStart : 0;
	if ( key && key != acts.simRollTime ) {
		acts.simRollTime = key;
		acts.simRollAtk = acts.atk;
		acts.simWillDodge = random() < actsCvSimDodge.value;
		acts.simReact = actsCvSimReact.integer + rand() % 150;
		acts.simThreats++;
		if ( acts.simWillDodge ) {
			acts.simDodges++;
		}
		G_LogPrintf( "DSSim: read %s %i (react %i, telegraph %i)\n", ds_bossAttackNames[acts.atk], acts.simWillDodge, acts.simReact,
			acts.atkLand - acts.atkStart );
	}
	if ( acts.atk && acts.atkStage == 0 && acts.simWillDodge && level.time >= acts.atkStart + acts.simReact ) {
		switch ( acts.atk ) {
		case DSB_HOOK:
			VectorMA( acts.atkFrom, 1100, acts.atkDir, b );
			dist = Acts_SegDist( acts.atkFrom, b, p, away );
			if ( dist < 70 ) {
				VectorCopy( away, best );
				urgency = 1;
			}
			break;
		case DSB_CLEAVER:
			if ( acts.atkFx[0] && acts.atkFx[0]->inuse ) {
				VectorCopy( acts.atkFx[0]->r.currentOrigin, a );
				VectorCopy( acts.atkFx[0]->s.origin2, b );
			} else {
				VectorCopy( acts.atkFrom, a );
				VectorCopy( acts.atkTo, b );
			}
			dist = Acts_SegDist( a, b, p, away );
			if ( dist < 120 ) {
				VectorCopy( away, best );
				urgency = 1;
			}
			break;
		case DSB_LANCE:
			for ( i = 0 ; i < 6 ; i++ ) {
				fx = acts.atkFx[i];
				if ( !fx || !fx->inuse ) {
					continue;
				}
				dist = Acts_SegDist( fx->r.currentOrigin, fx->s.origin2, p, away );
				if ( dist < 60 ) {
					VectorAdd( best, away, best );
					urgency = 1;
				}
			}
			break;
		case DSB_SLAM:
			VectorSubtract( p, acts.atkTo, d );
			d[2] = 0;
			if ( VectorLength( d ) < 360 ) {
				land = acts.atkLand;
				jump = qtrue;
			}
			break;
		case DSB_BARRAGE:
			for ( i = 0 ; i < 6 ; i++ ) {
				fx = acts.atkFx[i];
				if ( !fx || !fx->inuse ) {
					continue;
				}
				VectorSubtract( p, fx->r.currentOrigin, d );
				d[2] = 0;
				dist = VectorNormalize( d );
				if ( dist < 170 ) {
					VectorMA( best, 1.0f - dist / 170, d, best );
					urgency = 1;
				}
			}
			break;
		case DSB_WELL:
		default:
			break;
		}
	}
	// strikes on the floor (mortar, meteor, fire floor): read with the same roll
	if ( acts.simWillDodge && level.time >= acts.simRollTime + acts.simReact ) {
		for ( i = 0 ; i < DS_MAX_STRIKES ; i++ ) {
			if ( !acts.strikes[i].kind || acts.strikes[i].land - level.time > 1400 ) {
				continue;
			}
			VectorSubtract( p, acts.strikes[i].org, d );
			d[2] = 0;
			dist = VectorNormalize( d );
			R = acts.strikes[i].radius + 40;
			if ( dist < R ) {
				VectorMA( best, 1.0f - dist / R, d, best );
				urgency = 1;
			}
		}
	}
	// the gravity well: run out of it
	if ( acts.wellUntil > level.time && acts.simWillDodge ) {
		VectorSubtract( p, acts.wellOrg, d );
		d[2] = 0;
		dist = VectorNormalize( d );
		if ( dist < DS_WELL_RADIUS + 40 ) {
			VectorAdd( best, d, best );
			urgency = 1;
		}
	}
	// a fire ring coming: jump it
	if ( acts.novaStart && acts.simWillDodge ) {
		u = ( level.time - acts.novaStart ) * 0.001f;
		VectorSubtract( p, acts.novaOrg, d );
		d[2] = 0;
		dist = VectorLength( d );
		for ( i = 0 ; i < acts.novaCount ; i++ ) {
			R = DS_NOVA_SPEED * ( u - i * 0.7f );
			if ( dist - R > 20 && dist - R < 150 ) {
				jump = qtrue;
				land = level.time + (int)( ( dist - R ) / DS_NOVA_SPEED * 1000 );
			}
		}
	}
	if ( jump && land - level.time < 260 && land - level.time > -100 ) {
		acts.simJumpUntil = level.time + 150;
	}
	if ( level.time < acts.simJumpUntil ) {
		ucmd->upmove = 127;
	}
	if ( urgency > 0 && VectorNormalize( best ) > 0 ) {
		VectorSet( ang, 0, ent->client->ps.viewangles[YAW], 0 );
		AngleVectors( ang, fwd, right, NULL );
		a[0] = DotProduct( best, fwd );
		a[1] = DotProduct( best, right );
		dist = fabs( a[0] ) > fabs( a[1] ) ? fabs( a[0] ) : fabs( a[1] );
		if ( dist > 0.01f ) {
			ucmd->forwardmove = (signed char)( 127 * a[0] / dist );
			ucmd->rightmove = (signed char)( 127 * a[1] / dist );
		}
	}
}

void G_DS_BotInput( int client, usercmd_t *ucmd ) {
	if ( !acts.on ) {
		return;
	}
	if ( client >= 0 && client < MAX_CLIENTS && acts.out[client] ) {
		ucmd->buttons = 0;		// eliminated: no respawn, it leaves in a moment
		ucmd->forwardmove = ucmd->rightmove = ucmd->upmove = 0;
		return;
	}
	if ( actsCvSim.integer && client == G_DS_SimClient() ) {
		Acts_SimDodge( &g_entities[client], ucmd );
		return;
	}
	if ( client != acts.client ) {
		return;
	}
	if ( acts.dead ) {
		ucmd->buttons = 0;		// never respawn: the floor is ending
		ucmd->forwardmove = ucmd->rightmove = ucmd->upmove = 0;
		return;
	}
	if ( acts.timeUp ) {
		ucmd->buttons &= ~( BUTTON_ATTACK | BUTTON_ALTATTACK );		// timed: it stands over you, done
	}
	if ( level.time < acts.rootUntil ) {
		ucmd->forwardmove = ucmd->rightmove = 0;
		if ( ucmd->upmove > 0 ) {
			ucmd->upmove = 0;
		}
	}
	if ( level.time < acts.holdFireUntil ) {
		ucmd->buttons &= ~( BUTTON_ATTACK | BUTTON_ALTATTACK );
	}
}

qboolean G_DS_BotAllies( int a, int b ) {
	return qfalse;		// round 2b: on a boss floor everyone is a rival
}

/*
=================
G_DS_BotPriority

A rival bot treats the boss as its priority target most of the time (it stays on it and
picks it over nearer enemies): 65% of 6 s windows, a different pattern per bot; the sim bot
(standing in for a human, who'd go for the boss) 80%. The boss has no priority: the nearest.
=================
*/
qboolean G_DS_BotPriority( int bot, int client ) {
	unsigned	h;
	int			pct;

	if ( !acts.on || acts.dead || client != acts.client || bot == acts.client || bot < 0 || bot >= MAX_CLIENTS ) {
		return qfalse;
	}
	if ( !g_entities[client].inuse || g_entities[client].health <= 0 ) {
		return qfalse;
	}
	pct = Acts_IsPlayer( &g_entities[bot] ) ? 80 : 65;
	h = (unsigned)( level.time / 6000 ) * 2654435761u + (unsigned)bot * 40503u;
	h ^= h >> 13;
	return (int)( ( h * 2246822519u ) >> 16 ) % 100 < pct;
}

/*
=================
G_DS_BossHuntSpot

ai (BotLongTermGoal): the boss with nobody in sight walks to a fighter (the one it's after,
else the nearest), by its own paths. qfalse = no hunt
=================
*/
qboolean G_DS_BossHuntSpot( int client, vec3_t out ) {
	gentity_t	*boss, *e, *best = NULL;
	float		d, bestD = 1e9f;
	int			i;

	if ( !acts.on || acts.dead || client != acts.client ) {
		return qfalse;
	}
	boss = Acts_Boss();
	if ( !Acts_Alive( boss ) ) {
		return qfalse;
	}
	if ( acts.target >= 0 && acts.target < MAX_CLIENTS && Acts_Victim( &g_entities[acts.target] ) ) {
		best = &g_entities[acts.target];
	} else {
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			e = &g_entities[i];
			if ( !Acts_Victim( e ) ) {
				continue;
			}
			d = Distance( e->r.currentOrigin, boss->r.currentOrigin );
			if ( d < bestD ) {
				bestD = d;
				best = e;
			}
		}
	}
	if ( !best ) {
		return qfalse;
	}
	VectorCopy( best->r.currentOrigin, out );
	return qtrue;
}

/*
===============================================================================

ATTACKS

===============================================================================
*/

static void Acts_EndAttack( void ) {
	int		i;

	for ( i = 0 ; i < 6 ; i++ ) {
		Acts_FxFree( &acts.atkFx[i] );
	}
	if ( acts.atk ) {
		acts.nextAttack = level.time + (int)( ( 2200 + rand() % 1200 ) * acts.pace * ( acts.lastMinute ? 0.8f : 1.0f ) * G_DS_RmPace() / acts.tSpeed );		// (en-rematch: FURY)
		if ( acts.atkStage == 0 && acts.atk != DSB_MORTAR && acts.atk != DSB_METEOR && acts.atk != DSB_FIREFLOOR ) {
			G_LogPrintf( "DSBoss: attack %s cut short\n", ds_bossAttackNames[acts.atk] );
		}
	}
	acts.atk = 0;
	acts.atkStage = 0;
	acts.rootUntil = 0;
	acts.holdFireUntil = 0;
	acts.csDirty = 1;
}

static dsStrike_t *Acts_AddStrike( int kind, const vec3_t org, int land, int damage, float radius ) {
	dsStrike_t	*s;
	int			i;

	for ( i = 0 ; i < DS_MAX_STRIKES ; i++ ) {
		s = &acts.strikes[i];
		if ( !s->kind ) {
			s->kind = kind;
			s->land = land;
			VectorCopy( org, s->org );
			s->damage = damage;
			s->radius = radius;
			s->fx = Acts_Fx( kind, org, org, land, radius, 350 );
			return s;
		}
	}
	return NULL;
}

/*
=================
Acts_Visible

A clear line from the boss's eyes to the target's chest
=================
*/
static qboolean Acts_Visible( gentity_t *from, gentity_t *to ) {
	trace_t	tr;
	vec3_t	a, b;

	Acts_Eye( from, a );
	Acts_Chest( to, b );
	trap_Trace( &tr, a, NULL, NULL, b, from->s.number, MASK_SOLID );
	return tr.fraction >= 1.0f;
}

static qboolean Acts_CanStart( int atk, gentity_t *boss, gentity_t *target, float dist, qboolean vis ) {
	int		i, n;

	switch ( atk ) {
	case DSB_HOOK:		return vis && dist > 220 && dist < 1100;
	case DSB_CLEAVER:	return vis && dist > 120 && dist < 700;
	case DSB_SLAM:		return dist < 650;
	case DSB_BARRAGE:	return dist < 1600;
	case DSB_MORTAR:	return dist < 1800;
	case DSB_LANCE:		return vis && dist < 1800;
	case DSB_BLINK:		return dist > 300 || !vis;
	case DSB_WELL:		return dist < 1400 && acts.wellUntil < level.time;
	case DSB_METEOR:	return dist < 1800;
	case DSB_NOVA:		return dist < 600;
	case DSB_FIREFLOOR:	return dist < 1400;
	case DSB_DRONES:
	case DSB_CLONES:
		for ( n = 0, i = 0 ; i < DS_MAX_PROPS ; i++ ) {
			if ( acts.props[i] && acts.props[i]->inuse ) {
				n++;
			}
		}
		return n == 0;
	default:
		return qfalse;
	}
}

static void Acts_StartAttack( int atk, gentity_t *boss, gentity_t *target ) {
	vec3_t		eye, chest, dir, right, spot, end, from;
	vec3_t		ang;
	trace_t		tr;
	int			i, n, land, k;
	float		yaw;

	acts.atk = atk;
	acts.atkStart = level.time;
	acts.atkStage = 0;
	acts.atkHitMask = 0;
	Acts_Eye( boss, eye );
	Acts_Chest( target, chest );
	VectorSubtract( chest, eye, dir );
	VectorNormalize( dir );
	VectorCopy( dir, acts.atkDir );
	VectorCopy( eye, acts.atkFrom );
	VectorCopy( chest, acts.atkTo );
	acts.holdFireUntil = level.time + 900;

	switch ( atk ) {
	case DSB_HOOK:
		acts.atkLand = level.time + Acts_Tele( 1000 );
		acts.atkEnd = acts.atkLand + 300;
		acts.rootUntil = acts.atkLand;
		acts.atkFx[0] = Acts_Fx( atk, eye, chest, acts.atkLand, 12, 300 );
		break;
	case DSB_CLEAVER:
		acts.atkLand = level.time + Acts_Tele( 1000 );
		acts.atkEnd = acts.atkLand + 480;
		acts.rootUntil = acts.atkEnd;
		acts.holdFireUntil = acts.atkEnd;
		VectorCopy( boss->r.currentOrigin, from );
		VectorMA( from, 640, dir, end );
		acts.atkFx[0] = Acts_Fx( atk, from, end, acts.atkLand, 60, 480 );
		break;
	case DSB_SLAM:
		// a leap toward the player, the circle where it comes down
		acts.atkLand = level.time + Acts_Tele( 1150 );
		acts.atkEnd = acts.atkLand + 400;
		acts.rootUntil = acts.atkLand;
		acts.holdFireUntil = acts.atkLand;
		VectorSubtract( target->r.currentOrigin, boss->r.currentOrigin, dir );
		dir[2] = 0;
		n = (int)VectorNormalize( dir );
		boss->client->ps.velocity[0] = dir[0] * ( n * 0.6f > 420 ? 420 : n * 0.6f );
		boss->client->ps.velocity[1] = dir[1] * ( n * 0.6f > 420 ? 420 : n * 0.6f );
		boss->client->ps.velocity[2] = 460;
		boss->client->ps.groundEntityNum = ENTITYNUM_NONE;
		VectorMA( boss->r.currentOrigin, ( n * 0.6f > 420 ? 420 : n * 0.6f ) * 1.1f, dir, spot );
		Acts_Ground( spot, spot );
		VectorCopy( spot, acts.atkTo );
		acts.atkFx[0] = Acts_Fx( atk, spot, spot, acts.atkLand, 340, 400 );
		break;
	case DSB_BARRAGE:
		n = acts.phase >= 3 ? 7 : 5;
		acts.atkLand = level.time + 800;		// launch; each rocket lands 1 s later
		acts.atkEnd = acts.atkLand + 1100;
		acts.rootUntil = acts.atkLand;
		for ( i = 0 ; i < n && i < 6 ; i++ ) {
			if ( i == 0 ) {
				VectorMA( target->r.currentOrigin, 0.5f, target->client->ps.velocity, spot );
				spot[2] = target->r.currentOrigin[2];
				Acts_Ground( spot, spot );
			} else if ( !Acts_FloorSpot( target->r.currentOrigin, 90, 300, -1, spot ) ) {
				continue;
			}
			acts.atkFx[i] = Acts_Fx( atk, spot, spot, acts.atkLand + 1000, 120, 150 );
		}
		break;
	case DSB_DRONES:
		acts.atkLand = level.time + Acts_Tele( 900 );
		acts.atkEnd = acts.atkLand;
		acts.rootUntil = acts.atkLand;
		VectorSet( ang, 0, boss->client->ps.viewangles[YAW], 0 );
		AngleVectors( ang, NULL, right, NULL );
		for ( i = 0 ; i < 2 ; i++ ) {
			VectorMA( boss->r.currentOrigin, i ? 110 : -110, right, spot );
			spot[2] += 70;
			trap_Trace( &tr, boss->r.currentOrigin, NULL, NULL, spot, boss->s.number, MASK_SOLID );
			VectorCopy( tr.endpos, spot );
			acts.atkFx[i] = Acts_Fx( atk, spot, spot, acts.atkLand, 40, 0 );
		}
		if ( acts.phase >= 2 ) {
			acts.atkFx[2] = Acts_Fx( DSB_SHIELD, boss->r.currentOrigin, boss->r.currentOrigin, acts.atkLand, 70, 0 );
		}
		break;
	case DSB_MORTAR:
		acts.atkLand = level.time + 300;
		acts.atkEnd = level.time + 2200;
		acts.rootUntil = acts.atkLand + 1200;
		acts.holdFireUntil = acts.rootUntil;
		for ( i = 0 ; i < 9 ; i++ ) {
			land = level.time + Acts_Tele( 1100 ) + i * 190;
			if ( i == 0 || i == 4 ) {
				Acts_Ground( target->r.currentOrigin, spot );
			} else if ( !Acts_FloorSpot( target->r.currentOrigin, 60, 340, -1, spot ) ) {
				continue;
			}
			Acts_AddStrike( DSB_MORTAR, spot, land, 42, 150 );
		}
		break;
	case DSB_BLINK:
		// a shadow step: a short sidestep along a visible path, across the line to its target,
		// never ending within DS_RELOC_MIN of the player
		acts.atkLand = level.time + Acts_Tele( 800 );
		acts.atkEnd = acts.atkLand + 200;
		acts.rootUntil = acts.atkLand;
		{
			gentity_t	*pl = Acts_PlayerEnt();
			float		side, len;

			VectorSubtract( target->r.currentOrigin, boss->r.currentOrigin, dir );
			dir[2] = 0;
			VectorNormalize( dir );
			yaw = RAD2DEG( atan2( dir[1], dir[0] ) );
			side = random() < 0.5f ? 1.0f : -1.0f;
			for ( k = 0 ; k < 6 ; k++ ) {
				len = 420.0f - 40.0f * ( k / 2 );
				VectorSet( ang, 0, yaw + side * ( k & 1 ? -1 : 1 ) * ( 70 + 15 * ( k / 2 ) ), 0 );
				AngleVectors( ang, right, NULL, NULL );
				VectorCopy( boss->r.currentOrigin, from );
				VectorMA( from, len, right, spot );
				trap_Trace( &tr, from, boss->r.mins, boss->r.maxs, spot, boss->s.number, MASK_PLAYERSOLID );
				if ( tr.startsolid || tr.fraction < 0.75f ) {
					continue;
				}
				VectorCopy( tr.endpos, spot );
				if ( pl && Distance( spot, pl->r.currentOrigin ) < DS_RELOC_MIN ) {
					continue;
				}
				VectorCopy( spot, end );
				end[2] -= 160;
				trap_Trace( &tr, spot, boss->r.mins, boss->r.maxs, end, boss->s.number, MASK_PLAYERSOLID );
				if ( tr.fraction < 1.0f && !( tr.contents & ( CONTENTS_LAVA | CONTENTS_SLIME ) ) ) {
					break;		// floor under it
				}
			}
		}
		if ( k == 6 ) {
			acts.atk = 0;
			acts.nextAttack = level.time + 800;
			return;
		}
		VectorCopy( spot, acts.atkTo );
		acts.atkFx[0] = Acts_Fx( atk, boss->r.currentOrigin, spot, acts.atkLand, 40, 250 );
		break;
	case DSB_LANCE:
		n = acts.phase >= 3 ? 5 : 3;
		acts.atkLand = level.time + Acts_Tele( 1100 );
		acts.atkEnd = acts.atkLand + 200;
		acts.rootUntil = acts.atkLand;
		acts.holdFireUntil = acts.atkEnd;
		vectoangles( dir, ang );
		for ( i = 0 ; i < n ; i++ ) {
			VectorSet( spot, ang[PITCH], ang[YAW] + ( i - ( n - 1 ) * 0.5f ) * 11.0f, 0 );
			AngleVectors( spot, right, NULL, NULL );
			VectorMA( eye, 2400, right, end );
			trap_Trace( &tr, eye, NULL, NULL, end, boss->s.number, MASK_SOLID );
			acts.atkFx[i] = Acts_Fx( atk, eye, tr.endpos, acts.atkLand, 14, 200 );
		}
		break;
	case DSB_WELL:
		// a spot near the player, by preference one with a drop behind it
		acts.atkLand = level.time + Acts_Tele( 900 );
		acts.atkEnd = acts.atkLand;
		acts.rootUntil = acts.atkLand;
		k = 0;
		for ( i = 0 ; i < 10 ; i++ ) {
			if ( !Acts_FloorSpot( target->r.currentOrigin, 220, 420, i < 6 ? target->client->ps.viewangles[YAW] : -1, spot ) ) {
				continue;
			}
			VectorSubtract( spot, target->r.currentOrigin, dir );
			dir[2] = 0;
			VectorNormalize( dir );
			VectorMA( spot, 140, dir, from );
			from[2] += 24;
			VectorCopy( from, end );
			end[2] -= 900;
			trap_Trace( &tr, from, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID );
			VectorCopy( spot, acts.wellOrg );
			k = 1;
			if ( tr.fraction == 1.0f || ( tr.contents & ( CONTENTS_LAVA | CONTENTS_SLIME ) ) ) {
				break;		// an edge
			}
		}
		if ( !k ) {
			Acts_Ground( target->r.currentOrigin, acts.wellOrg );
		}
		acts.atkFx[0] = Acts_Fx( atk, acts.wellOrg, acts.wellOrg, acts.atkLand, DS_WELL_RADIUS, DS_WELL_TIME );
		break;
	case DSB_CLONES:
		acts.atkLand = level.time + Acts_Tele( 900 );
		acts.atkEnd = acts.atkLand;
		acts.rootUntil = acts.atkLand;
		for ( i = 0 ; i < 2 ; i++ ) {
			if ( !Acts_FloorSpot( target->r.currentOrigin, 260, 420, target->client->ps.viewangles[YAW] + ( i ? 90 : -90 ), spot ) ) {
				continue;
			}
			spot[2] += 24;
			acts.atkFx[i] = Acts_Fx( atk, spot, spot, acts.atkLand, 40, 0 );
		}
		break;
	case DSB_METEOR:
		n = acts.phase >= 3 ? 9 : 6;
		acts.atkLand = level.time + 300;
		acts.atkEnd = level.time + 1300 + n * 230;
		acts.rootUntil = acts.atkLand + 500;
		acts.holdFireUntil = acts.rootUntil;
		for ( i = 0 ; i < n ; i++ ) {
			if ( i == 0 || i == 3 ) {
				Acts_Ground( target->r.currentOrigin, spot );
			} else if ( !Acts_FloorSpot( target->r.currentOrigin, 80, 380, -1, spot ) ) {
				continue;
			}
			Acts_AddStrike( DSB_METEOR, spot, level.time + Acts_Tele( 1150 ) + i * 230, 48, 170 );
		}
		break;
	case DSB_NOVA:
		acts.atkLand = level.time + Acts_Tele( 950 );
		acts.atkEnd = acts.atkLand + 300;
		acts.rootUntil = acts.atkLand + 300;
		acts.holdFireUntil = acts.rootUntil;
		Acts_Ground( boss->r.currentOrigin, spot );
		VectorCopy( spot, acts.atkTo );
		acts.atkFx[0] = Acts_Fx( atk, spot, spot, acts.atkLand, 120, 0 );
		break;
	case DSB_FIREFLOOR:
		n = acts.phase >= 3 ? 6 : 4;
		acts.atkLand = level.time + 300;
		acts.atkEnd = level.time + 1300;
		acts.rootUntil = acts.atkLand;
		for ( i = 0 ; i < n ; i++ ) {
			if ( i == 0 ) {
				Acts_Ground( target->r.currentOrigin, spot );
			} else if ( !Acts_FloorSpot( target->r.currentOrigin, 110, 360, -1, spot ) ) {
				continue;
			}
			Acts_AddStrike( DSB_FIREFLOOR, spot, level.time + Acts_Tele( 1050 ) + i * 60, 0, 100 );
		}
		break;
	default:
		acts.atk = 0;
		return;
	}
	acts.csDirty = 1;
	G_LogPrintf( "DSBoss: attack %s telegraph (phase %i)\n", ds_bossAttackNames[atk], acts.phase );
}

/*
=================
Acts_PickAttack

A random usable attack whose cooldown is over (phase 3 / enraged: shorter cooldowns)
=================
*/
static int Acts_PickAttack( gentity_t *boss, gentity_t *target ) {
	int			cand[DS_ATKS], n = 0, i;
	float		dist;
	qboolean	vis;
	const dsAtkDef_t	*a;

	dist = Distance( boss->r.currentOrigin, target->r.currentOrigin );
	vis = Acts_Visible( boss, target );
	for ( i = 0 ; i < DS_ATKS ; i++ ) {
		a = &actsAtks[acts.type][i];
		if ( a->atk == DSB_NONE || acts.phase < a->minPhase || level.time < acts.cooldown[a->atk] ) {
			continue;
		}
		if ( !Acts_CanStart( a->atk, boss, target, dist, vis ) ) {
			continue;
		}
		cand[n++] = a->atk;
	}
	if ( !n ) {
		return DSB_NONE;
	}
	return cand[rand() % n];
}

static int Acts_Cooldown( int atk ) {
	int		i;
	float	k = ( acts.frenzy ? 0.65f : 1.0f ) * ( acts.lastMinute ? 0.75f : 1.0f ) * G_DS_RmPace();		// round 4: the last-minute enrage; en-rematch: FURY

	for ( i = 0 ; i < DS_ATKS ; i++ ) {
		if ( actsAtks[acts.type][i].atk == atk ) {
			return (int)( actsAtks[acts.type][i].cooldown * k * ( 0.8f + 0.1f * ( 5 - acts.difficulty ) ) / acts.tSpeed );
		}
	}
	return 8000;
}

// a lance line: whoever stands on it when it fires
static void Acts_FireLance( gentity_t *boss, const vec3_t from, const vec3_t to, int damage ) {
	gentity_t	*te, *hit;
	trace_t		tr;
	vec3_t		mins = { -14, -14, -14 }, maxs = { 14, 14, 14 }, dir;

	trap_Trace( &tr, from, mins, maxs, to, boss->s.number, MASK_SHOT );
	VectorSubtract( to, from, dir );
	VectorNormalize( dir );
	if ( tr.entityNum < MAX_CLIENTS ) {
		hit = &g_entities[tr.entityNum];
		if ( Acts_Victim( hit ) ) {
			G_Damage( hit, boss, boss, dir, tr.endpos, Acts_Scaled( damage ), 0, MOD_RAILGUN );
			G_LogPrintf( "DSBoss: lance hit %i\n", tr.entityNum );
		}
	}
	te = G_TempEntity( (float *)to, EV_RAILTRAIL );
	te->s.clientNum = boss->s.number;
	VectorCopy( from, te->s.origin2 );
	te->s.eventParm = 255;
	te->r.svFlags |= SVF_BROADCAST;
}

/*
=================
Acts_RunAttack

The attack being cast: tracking, landing, the after-effects
=================
*/
static void Acts_RunAttack( gentity_t *boss, gentity_t *target ) {
	gentity_t	*ent;
	vec3_t		eye, chest, d, end, spot;
	trace_t		tr;
	vec3_t		mins = { -10, -10, -10 }, maxs = { 10, 10, 10 };
	float		dist, r;
	int			i, hits;

	Acts_Eye( boss, eye );
	switch ( acts.atk ) {
	case DSB_HOOK:
		if ( acts.atkStage == 0 ) {
			// tracks the player until 0.45 s before it flies, then the line is locked: step off it
			if ( level.time < acts.atkLand - 450 && Acts_Alive( target ) ) {
				Acts_Chest( target, chest );
				VectorSubtract( chest, eye, acts.atkDir );
				VectorNormalize( acts.atkDir );
				VectorCopy( chest, acts.atkTo );
			}
			VectorMA( eye, 1100, acts.atkDir, end );
			Acts_FxMove( acts.atkFx[0], eye, level.time < acts.atkLand - 450 ? acts.atkTo : end );
			if ( level.time >= acts.atkLand ) {
				acts.atkStage = 1;
				if ( acts.atkFx[0] ) {
					acts.atkFx[0]->s.frame = 1;
				}
				trap_Trace( &tr, eye, mins, maxs, end, boss->s.number, MASK_SHOT );
				Acts_FxMove( acts.atkFx[0], eye, tr.endpos );
				hits = 0;
				if ( tr.entityNum < MAX_CLIENTS && Acts_Victim( &g_entities[tr.entityNum] ) ) {
					ent = &g_entities[tr.entityNum];
					VectorSubtract( boss->r.currentOrigin, ent->r.currentOrigin, d );
					dist = VectorNormalize( d );
					G_Damage( ent, boss, boss, NULL, tr.endpos, Acts_Scaled( 12 ), DAMAGE_NO_KNOCKBACK, MOD_GRAPPLE );
					// yanked in, landing just short of the boss: the cleaver comes next
					VectorClear( ent->client->ps.velocity );
					Acts_Knock( ent, d, dist * 1.6f > 1100 ? 1100 : dist * 1.6f, 240 );
					ent->client->ps.pm_time = 450;
					acts.cooldown[DSB_CLEAVER] = level.time + 700;
					acts.nextAttack = level.time + 500;
					hits = 1;
				}
				G_LogPrintf( "DSBoss: attack MEAT HOOK lands, hit %i\n", hits );
			}
		}
		break;

	case DSB_CLEAVER:
		if ( acts.atkStage == 0 ) {
			if ( level.time < acts.atkLand - 400 && Acts_Alive( target ) ) {
				VectorSubtract( target->r.currentOrigin, boss->r.currentOrigin, acts.atkDir );
				acts.atkDir[2] = 0;
				VectorNormalize( acts.atkDir );
				VectorMA( boss->r.currentOrigin, 640, acts.atkDir, end );
				Acts_FxMove( acts.atkFx[0], boss->r.currentOrigin, end );
			}
			if ( level.time >= acts.atkLand ) {
				acts.atkStage = 1;
				if ( acts.atkFx[0] ) {
					acts.atkFx[0]->s.frame = 1;
				}
				G_LogPrintf( "DSBoss: attack CLEAVER CHARGE lands\n" );
			}
		} else if ( level.time < acts.atkEnd ) {
			// the dash: hits everyone it passes once
			boss->client->ps.velocity[0] = acts.atkDir[0] * 1150;
			boss->client->ps.velocity[1] = acts.atkDir[1] * 1150;
			for ( i = 0 ; i < level.maxclients ; i++ ) {
				ent = &g_entities[i];
				if ( !Acts_Victim( ent ) || ( acts.atkHitMask & ( 1 << i ) ) ) {
					continue;
				}
				VectorSubtract( ent->r.currentOrigin, boss->r.currentOrigin, d );
				if ( fabs( d[2] ) > 70 || d[0] * d[0] + d[1] * d[1] > 80 * 80 ) {
					continue;
				}
				acts.atkHitMask |= 1 << i;
				G_Damage( ent, boss, boss, acts.atkDir, ent->r.currentOrigin, Acts_Scaled( 42 ), DAMAGE_NO_KNOCKBACK, MOD_GAUNTLET );
				if ( ent->client ) {
					Acts_Knock( ent, acts.atkDir, 650, 300 );
				}
				G_LogPrintf( "DSBoss: cleaver hit %i\n", i );
			}
		}
		break;

	case DSB_SLAM:
		if ( acts.atkStage == 0 ) {
			Acts_Ground( boss->r.currentOrigin, spot );
			Acts_FxMove( acts.atkFx[0], spot, spot );
			if ( level.time >= acts.atkLand || ( level.time > acts.atkStart + 500 && boss->client->ps.groundEntityNum != ENTITYNUM_NONE ) ) {
				acts.atkStage = 1;
				acts.atkEnd = level.time + 400;
				if ( acts.atkFx[0] ) {
					acts.atkFx[0]->s.frame = 1;
					acts.atkFx[0]->s.time2 = level.time;
					acts.atkFx[0]->nextthink = level.time + 450;
				}
				hits = 0;
				for ( i = 0 ; i < level.maxclients ; i++ ) {
					ent = &g_entities[i];
					if ( !Acts_Victim( ent ) ) {
						continue;
					}
					VectorSubtract( ent->r.currentOrigin, spot, d );
					dist = sqrt( d[0] * d[0] + d[1] * d[1] );
					if ( dist > 340 || fabs( d[2] ) > 120 || !Acts_OnGround( ent ) ) {
						continue;		// jumped it
					}
					d[2] = 0;
					VectorNormalize( d );
					G_Damage( ent, boss, boss, d, ent->r.currentOrigin, Acts_Scaled( 12 + (int)( 30 * ( 1.0f - dist / 340 ) ) ), DAMAGE_NO_KNOCKBACK, MOD_SLAM );
					if ( ent->client ) {
						Acts_Knock( ent, d, 300, 380 );
					}
					hits++;
				}
				G_AddEvent( boss, EV_FALL_FAR, 0 );
				G_LogPrintf( "DSBoss: attack GROUND SLAM lands, hit %i\n", hits );
			}
		}
		break;

	case DSB_BARRAGE:
		if ( acts.atkStage == 0 && level.time >= acts.atkLand ) {
			// arcing rockets, one to every circle, all landing a second later
			acts.atkStage = 1;
			for ( i = 0 ; i < 6 ; i++ ) {
				if ( !acts.atkFx[i] ) {
					continue;
				}
				VectorCopy( eye, spot );
				spot[2] += 24;
				VectorSubtract( acts.atkFx[i]->r.currentOrigin, spot, d );
				VectorCopy( d, end );		// fire_rocket normalizes its direction
				ent = fire_rocket( boss, spot, end );
				if ( !ent ) {
					continue;
				}
				ent->s.pos.trType = TR_GRAVITY;
				ent->s.pos.trTime = level.time;
				VectorCopy( spot, ent->s.pos.trBase );
				VectorCopy( d, ent->s.pos.trDelta );		// 1 s flight
				ent->s.pos.trDelta[2] += 0.5f * DEFAULT_GRAVITY * 1.0f;
				ent->damage = Acts_Scaled( 50 );
				ent->splashDamage = Acts_Scaled( 45 );
				ent->splashRadius = 130;
				ent->nextthink = level.time + 4000;
				VectorCopy( spot, ent->r.currentOrigin );
			}
			G_LogPrintf( "DSBoss: attack ROCKET BARRAGE launches\n" );
		}
		break;

	case DSB_DRONES:
		if ( acts.atkStage == 0 && level.time >= acts.atkLand ) {
			acts.atkStage = 1;
			for ( i = 0 ; i < 2 ; i++ ) {
				if ( acts.atkFx[i] ) {
					G_DS_SpawnDrone( boss, acts.atkFx[i]->r.currentOrigin, i, 0 );
				}
			}
			if ( acts.phase >= 2 ) {
				acts.shieldUntil = level.time + 8000;
			}
			G_LogPrintf( "DSBoss: attack DRONES lands (shield %i)\n", acts.phase >= 2 );
		}
		break;

	case DSB_CLONES:
		if ( acts.atkStage == 0 && level.time >= acts.atkLand ) {
			acts.atkStage = 1;
			for ( i = 0 ; i < 2 ; i++ ) {
				if ( acts.atkFx[i] ) {
					G_DS_SpawnDrone( boss, acts.atkFx[i]->r.currentOrigin, i, 1 );
				}
			}
			G_LogPrintf( "DSBoss: attack DECOYS lands\n" );
		}
		break;

	case DSB_BLINK:
		if ( acts.atkStage == 0 && level.time >= acts.atkLand ) {
			vec3_t	ang;

			acts.atkStage = 1;
			// still free there, and still well away from the player?
			trap_Trace( &tr, acts.atkTo, boss->r.mins, boss->r.maxs, acts.atkTo, boss->s.number, MASK_PLAYERSOLID );
			if ( Acts_PlayerEnt() && Distance( acts.atkTo, Acts_PlayerEnt()->r.currentOrigin ) < DS_RELOC_MIN ) {
				tr.startsolid = qtrue;		// the player came close: no step
			}
			if ( !tr.startsolid && !tr.allsolid ) {
				Acts_LogReloc( "shadow step", acts.atkTo );
				G_TempEntity( boss->client->ps.origin, EV_PLAYER_TELEPORT_OUT );
				VectorCopy( acts.atkTo, boss->client->ps.origin );
				VectorClear( boss->client->ps.velocity );
				boss->client->ps.eFlags ^= EF_TELEPORT_BIT;
				if ( Acts_Alive( target ) ) {
					VectorSubtract( target->r.currentOrigin, acts.atkTo, d );
					vectoangles( d, ang );
					ang[PITCH] = 0;
					ang[ROLL] = 0;
					SetClientViewAngle( boss, ang );
				}
				BG_PlayerStateToEntityState( &boss->client->ps, &boss->s, qtrue );
				VectorCopy( boss->client->ps.origin, boss->r.currentOrigin );
				trap_LinkEntity( boss );
				G_TempEntity( boss->client->ps.origin, EV_PLAYER_TELEPORT_IN );
				// a lance right after the blink comes sooner
				acts.cooldown[DSB_LANCE] = level.time + 600;
				acts.nextAttack = level.time + 700;
			}
			G_LogPrintf( "DSBoss: attack BLINK lands %s\n", tr.startsolid ? "blocked" : "ok" );
		}
		break;

	case DSB_LANCE:
		if ( acts.atkStage == 0 && level.time >= acts.atkLand ) {
			acts.atkStage = 1;
			for ( i = 0 ; i < 6 ; i++ ) {
				if ( acts.atkFx[i] ) {
					acts.atkFx[i]->s.frame = 1;
					Acts_FireLance( boss, acts.atkFx[i]->r.currentOrigin, acts.atkFx[i]->s.origin2, 40 );
				}
			}
			G_LogPrintf( "DSBoss: attack RAIL LANCES fire\n" );
		}
		break;

	case DSB_WELL:
		if ( acts.atkStage == 0 && level.time >= acts.atkLand ) {
			acts.atkStage = 1;
			acts.wellStart = level.time;
			acts.wellUntil = level.time + DS_WELL_TIME;
			if ( acts.atkFx[0] ) {
				acts.atkFx[0]->s.frame = 1;
				acts.atkFx[0]->s.time2 = acts.wellUntil;
				acts.atkFx[0] = NULL;		// lives on by itself
			}
			G_LogPrintf( "DSBoss: attack GRAVITY WELL opens\n" );
		}
		break;

	case DSB_NOVA:
		if ( acts.atkStage == 0 && level.time >= acts.atkLand ) {
			acts.atkStage = 1;
			acts.novaStart = level.time;
			acts.novaCount = acts.phase >= 3 ? 2 : 1;
			acts.novaHitMask = 0;
			VectorCopy( acts.atkTo, acts.novaOrg );
			if ( acts.atkFx[0] ) {
				acts.atkFx[0]->s.frame = 1;
				acts.atkFx[0]->s.time2 = level.time;
				acts.atkFx[0]->s.otherEntityNum2 = acts.novaCount;		// rings
				acts.atkFx[0]->nextthink = level.time + (int)( DS_NOVA_RADIUS / DS_NOVA_SPEED * 1000 ) + 700 * acts.novaCount;
				acts.atkFx[0] = NULL;
			}
			G_LogPrintf( "DSBoss: attack FIRE NOVA lands\n" );
		}
		break;

	default:
		break;
	}
	(void)r;
	if ( level.time >= acts.atkEnd && ( acts.atkStage > 0 || acts.atk == DSB_MORTAR || acts.atk == DSB_METEOR || acts.atk == DSB_FIREFLOOR ) ) {
		Acts_EndAttack();
	}
}

/*
=================
Acts_RunStrikes

Mortar shells, meteors and fire circles landing on their marks
=================
*/
static void Acts_RunStrikes( gentity_t *boss ) {
	dsStrike_t	*s;
	gentity_t	*ent;
	int			i;

	for ( i = 0 ; i < DS_MAX_STRIKES ; i++ ) {
		s = &acts.strikes[i];
		if ( !s->kind || level.time < s->land ) {
			continue;
		}
		switch ( s->kind ) {
		case DSB_MORTAR:
		case DSB_METEOR:
			Acts_Blast( s->org, s->kind == DSB_METEOR );
			ent = boss ? boss : &g_entities[ENTITYNUM_WORLD];
			G_RadiusDamage( s->org, ent, Acts_Scaled( s->damage ), s->radius, NULL, s->kind == DSB_METEOR ? MOD_METEOR : MOD_ROCKET_SPLASH );
			if ( s->kind == DSB_METEOR && acts.phase >= 2 ) {
				G_SpawnFirePatch( s->org, boss, 2500, 70 );
			}
			break;
		case DSB_FIREFLOOR:
			G_SpawnFirePatch( s->org, boss, 7000, s->radius );
			break;
		default:
			break;
		}
		if ( s->fx && s->fx->inuse ) {
			s->fx->s.frame = 1;
		}
		s->fx = NULL;		// the fx frees itself after its linger
		s->kind = 0;
	}
}

/*
=================
Acts_RunLasting

The gravity well's pull, the fire nova's rings
=================
*/
static void Acts_RunLasting( gentity_t *boss ) {
	gentity_t	*ent;
	vec3_t		d;
	float		dist, pull, dt, R, t;
	int			i, k;

	dt = level.time - level.previousTime;
	if ( dt <= 0 || dt > 200 ) {
		dt = 50;
	}
	dt *= 0.001f;

	if ( acts.wellUntil > level.time ) {
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			ent = &g_entities[i];
			if ( !Acts_Victim( ent ) ) {
				continue;
			}
			VectorSubtract( acts.wellOrg, ent->r.currentOrigin, d );
			d[2] = 0;
			dist = VectorNormalize( d );
			if ( dist > DS_WELL_RADIUS ) {
				continue;
			}
			// strong enough to fight, never stronger than running away
			pull = 260 + 380 * ( 1.0f - dist / DS_WELL_RADIUS );
			ent->client->ps.velocity[0] += d[0] * pull * dt;
			ent->client->ps.velocity[1] += d[1] * pull * dt;
			if ( dist < 72 && level.time >= acts.wellTick ) {
				G_Damage( ent, boss, boss, NULL, NULL, Acts_Scaled( 8 ), DAMAGE_NO_KNOCKBACK, MOD_CRUSH );
			}
		}
		if ( level.time >= acts.wellTick ) {
			acts.wellTick = level.time + 250;
		}
	}

	if ( acts.novaStart ) {
		t = ( level.time - acts.novaStart ) * 0.001f;
		for ( k = 0 ; k < acts.novaCount ; k++ ) {
			R = DS_NOVA_SPEED * ( t - k * 0.7f );
			if ( R < 0 || R > DS_NOVA_RADIUS ) {
				continue;
			}
			for ( i = 0 ; i < level.maxclients ; i++ ) {
				ent = &g_entities[i];
				if ( !Acts_Victim( ent ) || ( acts.novaHitMask & ( 1 << ( ( i + 16 * k ) & 31 ) ) ) ) {
					continue;
				}
				VectorSubtract( ent->r.currentOrigin, acts.novaOrg, d );
				if ( fabs( d[2] ) > 90 ) {
					continue;
				}
				d[2] = 0;
				dist = VectorNormalize( d );
				if ( fabs( dist - R ) > 34 || !Acts_OnGroundH( ent, 24 ) ) {		// a low ring: any real jump clears it
					continue;		// jump the ring
				}
				acts.novaHitMask |= 1 << ( ( i + 16 * k ) & 31 );
				G_Damage( ent, boss, boss, d, ent->r.currentOrigin, Acts_Scaled( 26 ), DAMAGE_NO_KNOCKBACK, MOD_BURN );
				Acts_Knock( ent, d, 260, 260 );
				G_LogPrintf( "DSBoss: nova hit %i\n", i );
			}
		}
		if ( DS_NOVA_SPEED * ( t - ( acts.novaCount - 1 ) * 0.7f ) > DS_NOVA_RADIUS ) {
			acts.novaStart = 0;
		}
	}

	if ( acts.shieldUntil && acts.shieldUntil > level.time ) {
		// the shield drops when the drones are gone
		for ( k = 0, i = 0 ; i < DS_MAX_PROPS ; i++ ) {
			if ( acts.props[i] && acts.props[i]->inuse && acts.props[i]->s.modelindex2 == 0 ) {
				k++;
			}
		}
		if ( !k ) {
			acts.shieldUntil = 0;
			acts.csDirty = 1;
			G_LogPrintf( "DSBoss: shield down\n" );
		}
	} else if ( acts.shieldUntil ) {
		acts.shieldUntil = 0;
		acts.csDirty = 1;
	}
}

/*
===============================================================================

DRONES AND DECOYS (RM_ENT_BOSSPROP)

===============================================================================
*/

static void Acts_PropDie( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int mod ) {
	int		i;

	Acts_Blast( self->r.currentOrigin, qfalse );
	for ( i = 0 ; i < DS_MAX_PROPS ; i++ ) {
		if ( acts.props[i] == self ) {
			acts.props[i] = NULL;
		}
	}
	G_LogPrintf( "DSBoss: %s destroyed\n", self->s.modelindex2 ? "decoy" : "drone" );
	self->takedamage = qfalse;
	G_FreeEntity( self );
}

static void Acts_PropThink( gentity_t *self ) {
	gentity_t	*boss = Acts_Boss(), *target = Acts_Human(), *bolt;
	vec3_t		org, aim, d;
	trace_t		tr;
	float		a;
	int			i;

	if ( level.time >= self->count || !boss || acts.dead ) {
		for ( i = 0 ; i < DS_MAX_PROPS ; i++ ) {
			if ( acts.props[i] == self ) {
				acts.props[i] = NULL;
			}
		}
		G_FreeEntity( self );
		return;
	}
	self->nextthink = level.time + 50;
	if ( self->s.modelindex2 == 0 ) {
		// a drone circles the boss's shoulders
		a = level.time * 0.0012f + self->s.generic1 * M_PI;
		VectorCopy( boss->r.currentOrigin, org );
		org[0] += cos( a ) * 90;
		org[1] += sin( a ) * 90;
		org[2] += 70 + 10 * sin( level.time * 0.003f + self->s.generic1 );
		trap_Trace( &tr, boss->r.currentOrigin, self->r.mins, self->r.maxs, org, boss->s.number, MASK_SOLID );
		VectorCopy( tr.endpos, org );
		VectorSubtract( org, self->r.currentOrigin, d );
		VectorCopy( self->r.currentOrigin, self->s.pos.trBase );
		VectorScale( d, 1000.0f / 50, self->s.pos.trDelta );
		self->s.pos.trType = TR_LINEAR;
		self->s.pos.trTime = level.time;
		VectorCopy( org, self->r.currentOrigin );
		self->s.pos.trDuration = 50;
		trap_LinkEntity( self );
	}
	if ( level.time < self->wait || !Acts_Victim( target ) ) {
		return;
	}
	Acts_Chest( target, aim );
	trap_Trace( &tr, self->r.currentOrigin, NULL, NULL, aim, self->s.number, MASK_SOLID );
	if ( tr.fraction < 1.0f && tr.entityNum != target->s.number ) {
		self->wait = level.time + 400;
		return;
	}
	if ( self->s.modelindex2 == 0 ) {
		// drone: a plasma bolt, a little lead
		VectorMA( aim, 0.15f, target->client->ps.velocity, aim );
		VectorSubtract( aim, self->r.currentOrigin, d );
		VectorNormalize( d );
		bolt = fire_plasma( boss, self->r.currentOrigin, d );
		if ( bolt ) {
			bolt->damage = Acts_Scaled( 14 );
			bolt->splashDamage = Acts_Scaled( 8 );
		}
		self->wait = level.time + 1500 + rand() % 500;
	} else {
		// decoy: a single telegraphed lance
		if ( !self->target_ent ) {
			self->target_ent = Acts_Fx( DSB_LANCE, self->r.currentOrigin, aim, level.time + 1100, 14, 200 );
			VectorSubtract( aim, self->r.currentOrigin, d );
			VectorNormalize( d );
			VectorMA( self->r.currentOrigin, 2400, d, aim );
			trap_Trace( &tr, self->r.currentOrigin, NULL, NULL, aim, self->s.number, MASK_SOLID );
			Acts_FxMove( self->target_ent, self->r.currentOrigin, tr.endpos );
			self->wait = level.time + 1100;
			// keep facing it
			vectoangles( d, self->s.apos.trBase );
			self->s.apos.trBase[PITCH] = 0;
			return;
		}
		if ( self->target_ent->inuse ) {
			self->target_ent->s.frame = 1;
			Acts_FireLance( boss, self->target_ent->r.currentOrigin, self->target_ent->s.origin2, 30 );
		}
		self->target_ent = NULL;
		self->wait = level.time + 2600 + rand() % 900;
	}
}

gentity_t *G_DS_SpawnDrone( gentity_t *boss, vec3_t org, int slot, int kind ) {
	gentity_t	*ent;
	int			i;

	for ( i = 0 ; i < DS_MAX_PROPS ; i++ ) {
		if ( !acts.props[i] || !acts.props[i]->inuse ) {
			break;
		}
	}
	if ( i == DS_MAX_PROPS ) {
		return NULL;
	}
	ent = G_Spawn();
	ent->classname = kind ? "boss_decoy" : "boss_drone";
	ent->s.eType = ET_GENERAL;
	ent->s.generic1 = RM_ENT_BOSSPROP;
	ent->s.modelindex2 = kind;
	ent->s.otherEntityNum = boss->s.number;
	ent->s.time = level.time;
	ent->r.svFlags |= SVF_BROADCAST;
	if ( kind ) {
		VectorSet( ent->r.mins, -15, -15, -24 );
		VectorSet( ent->r.maxs, 15, 15, 32 );
		ent->health = 70;
		ent->count = level.time + 14000;
		ent->wait = level.time + 700 + slot * 900;
		VectorCopy( boss->client->ps.viewangles, ent->s.apos.trBase );
		ent->s.apos.trBase[PITCH] = 0;
	} else {
		VectorSet( ent->r.mins, -12, -12, -12 );
		VectorSet( ent->r.maxs, 12, 12, 12 );
		ent->health = 90 + 20 * acts.act;
		ent->count = level.time + 30000;
		ent->wait = level.time + 1200 + slot * 700;
	}
	ent->s.generic1 = RM_ENT_BOSSPROP;
	ent->r.contents = CONTENTS_BODY;
	ent->clipmask = MASK_SHOT;
	ent->takedamage = qtrue;
	ent->die = Acts_PropDie;
	ent->think = Acts_PropThink;
	ent->nextthink = level.time + 50;
	ent->s.frame = slot;
	G_SetOrigin( ent, org );
	trap_LinkEntity( ent );
	acts.props[i] = ent;
	return ent;
}

/*
===============================================================================

VAULT

===============================================================================
*/

// a spot up to rise units over a floor point, under the ceiling (qfalse: no room)
static qboolean Acts_AirSpot( const vec3_t floor, float rise, vec3_t out ) {
	trace_t	tr;
	vec3_t	top;

	VectorCopy( floor, top );
	top[2] += rise + 24;
	trap_Trace( &tr, floor, NULL, NULL, top, ENTITYNUM_NONE, MASK_SOLID | CONTENTS_PLAYERCLIP );
	if ( tr.startsolid || tr.endpos[2] - floor[2] < 40 ) {
		return qfalse;
	}
	VectorCopy( floor, out );
	out[2] = tr.endpos[2] - 24 < floor[2] + rise ? tr.endpos[2] - 24 : floor[2] + rise;
	return qtrue;
}

/*
=================
SHOOTING GALLERY

DS_VAULT_TARGETS crystals float around the player (250-850 away, preferably in sight); a hit
bursts one into gems (8 xp, a golden one every 7th: 30) and a new one comes 0.5 s later. Each
drifts off after 9 s unbroken. CONTENTS_CORPSE: every weapon hits it, the player walks through.
About one a second for a good shot: ~450 xp.
=================
*/
static void Acts_VaultTargetGone( gentity_t *self ) {
	int		i;

	for ( i = 0 ; i < DS_VAULT_TARGETS ; i++ ) {
		if ( acts.vaultTargets[i] == self ) {
			acts.vaultTargets[i] = NULL;
		}
	}
	self->takedamage = qfalse;
	G_FreeEntity( self );
}

static void Acts_VaultTargetDie( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int mod ) {
	gentity_t	*te;
	vec3_t		org, up = { 0, 0, 1 };

	if ( !self->takedamage ) {
		return;
	}
	VectorCopy( self->r.currentOrigin, org );
	te = G_TempEntity( org, EV_MISSILE_MISS );
	te->s.weapon = WP_PLASMAGUN;
	te->s.eventParm = DirToByte( up );
	G_DS_SpawnGems( org, self->count, -1 );
	acts.vaultBroken++;
	acts.csDirty = 1;
	if ( acts.vaultTargetNext < level.time + 500 ) {
		acts.vaultTargetNext = level.time + 500;
	}
	G_LogPrintf( "DSVault: crystal %i broken (%i xp, mod %i)\n", acts.vaultBroken, self->count, mod );
	Acts_VaultTargetGone( self );
}

static void Acts_VaultTargetThink( gentity_t *self ) {
	if ( level.time >= self->wait || level.time >= acts.vaultEnd ) {
		Acts_VaultTargetGone( self );
		return;
	}
	self->nextthink = level.time + 100;
}

static void Acts_VaultGallery( gentity_t *h ) {
	gentity_t	*ent;
	vec3_t		floor, spot, eye;
	trace_t		tr;
	int			slot, tries;

	if ( level.time < acts.vaultTargetNext || level.time >= acts.vaultEnd - 2000 ) {
		return;
	}
	for ( slot = 0 ; slot < DS_VAULT_TARGETS && acts.vaultTargets[slot] ; slot++ ) {
	}
	if ( slot >= DS_VAULT_TARGETS ) {
		return;
	}
	VectorCopy( h->client->ps.origin, eye );
	eye[2] += h->client->ps.viewheight;
	for ( tries = 0 ; tries < 6 ; tries++ ) {
		if ( !Acts_FloorSpot( h->r.currentOrigin, 250, 850, -1, floor ) || !Acts_AirSpot( floor, 40 + random() * 140, spot ) ) {
			continue;
		}
		trap_Trace( &tr, eye, NULL, NULL, spot, h->s.number, MASK_SOLID );
		if ( tr.fraction >= 1.0f || tries == 5 ) {
			break;		// (in sight if it can be)
		}
	}
	if ( tries >= 6 ) {
		acts.vaultTargetNext = level.time + 200;
		return;
	}
	acts.vaultTargetNext = level.time + 350;
	acts.vaultTargetCount++;
	ent = G_Spawn();
	ent->classname = "ds_vault_crystal";
	ent->s.eType = ET_GENERAL;
	ent->s.generic1 = RM_ENT_VAULTGEMS;
	ent->s.modelindex2 = acts.vaultTargetCount % 7 == 0 ? 1 : 0;
	ent->count = ent->s.modelindex2 ? 30 : 8;
	VectorSet( ent->r.mins, -14, -14, -14 );
	VectorSet( ent->r.maxs, 14, 14, 14 );
	ent->r.contents = CONTENTS_CORPSE;
	ent->takedamage = qtrue;
	ent->health = 1;
	ent->die = Acts_VaultTargetDie;
	ent->think = Acts_VaultTargetThink;
	ent->nextthink = level.time + 100;
	ent->wait = level.time + 9000;
	ent->r.svFlags |= SVF_BROADCAST;
	G_SetOrigin( ent, spot );
	// a slow bob
	ent->s.pos.trType = TR_SINE;
	ent->s.pos.trTime = level.time;
	ent->s.pos.trDuration = 2400;
	ent->s.pos.trDelta[2] = 10;
	ent->s.time = level.time;		// (cgame: it grows in)
	trap_LinkEntity( ent );
	acts.vaultTargets[slot] = ent;
}

/*
=================
RUSH

Every 100 ms the player's ground speed earns xp: a run (DSV_RUSH_RUN+) 0.4, fast (dash, slide,
strafe jumps: DSV_RUSH_FAST+) 1.5, blazing (DSV_RUSH_BLAZE+) 3.5. Every 0.3 s what was earned
hangs as gems 150-260 ahead on the line of travel, so the player runs through them. A plain run
gives ~160 xp; keeping the speed up, 500+.
=================
*/
static void Acts_VaultRush( gentity_t *h ) {
	float	*v = h->client->ps.velocity, spd;
	vec3_t	spot;
	int		xp;

	while ( level.time >= acts.vaultNextGem && level.time < acts.vaultEnd - 2000 ) {
		acts.vaultNextGem += 100;
		spd = sqrt( v[0] * v[0] + v[1] * v[1] );
		acts.vaultCredit += spd >= DSV_RUSH_BLAZE ? 3.5f : spd >= DSV_RUSH_FAST ? 1.5f : spd >= DSV_RUSH_RUN ? 0.4f : 0;
		if ( ( acts.vaultNextGem / 100 ) % 3 || acts.vaultCredit < 1.0f || spd < DSV_RUSH_RUN ) {
			continue;
		}
		if ( !Acts_FloorSpot( h->r.currentOrigin, 150, 260, AngleNormalize360( RAD2DEG( atan2( v[1], v[0] ) ) ), spot ) ) {
			continue;
		}
		spot[2] += 26;
		xp = (int)acts.vaultCredit;
		acts.vaultCredit -= xp;
		acts.vaultGems += xp;
		while ( xp > 0 ) {
			G_DS_SpawnGemFloat( spot, xp >= 5 ? 5 : xp );
			spot[2] += 10;
			xp -= 5;
		}
	}
}

/*
=================
SKY

Every 1.4 s a column of 5 gems hangs over a floor spot 150-650 away: 40, 104, 168, 232 and 296
up (under the ceiling), worth 1, 2, 3, 5 and 8 (every 5th column's top: 25). The low ones are a
walk, the high ones a double jump, a wall jump, the zip line or a rocket jump. ~28 columns.
=================
*/
static void Acts_VaultSky( gentity_t *h ) {
	static const int	val[5] = { 1, 2, 3, 5, 8 };
	vec3_t				floor, spot;
	int					i;

	if ( level.time < acts.vaultNextGem || level.time >= acts.vaultEnd - 4000 ) {
		return;
	}
	acts.vaultNextGem = level.time + 1400;
	if ( !Acts_FloorSpot( h->r.currentOrigin, 150, 650, -1, floor ) ) {
		acts.vaultNextGem = level.time + 200;
		return;
	}
	acts.vaultTargetCount++;
	for ( i = 0 ; i < 5 ; i++ ) {
		if ( !Acts_AirSpot( floor, 40 + 64 * i, spot ) || spot[2] < floor[2] + 32 + 64 * i ) {
			break;		// the ceiling
		}
		G_DS_SpawnGemFloat( spot, i == 4 && acts.vaultTargetCount % 5 == 0 ? 25 : val[i] );
		acts.vaultGems++;
	}
}

static void Acts_VaultFrame( void ) {
	gentity_t	*h = Acts_Human();
	vec3_t		spot, fwd, ang;

	if ( !Acts_Alive( h ) ) {
		return;
	}
	if ( !acts.vaultEnd ) {
		acts.vaultEnd = level.time + DS_VAULT_TIME;
		acts.vaultNextGem = level.time + 1200;
		acts.vaultChestAt = level.time + 1500;
		acts.csDirty = 1;
		G_LogPrintf( "DSVault: open for %i s\n", DS_VAULT_TIME / 1000 );
	}
	if ( acts.vaultChestAt && level.time >= acts.vaultChestAt ) {
		acts.vaultChestAt = 0;
		VectorSet( ang, 0, h->client->ps.viewangles[YAW], 0 );
		AngleVectors( ang, fwd, NULL, NULL );
		if ( !Acts_FloorSpot( h->r.currentOrigin, 120, 200, h->client->ps.viewangles[YAW], spot ) ) {
			VectorCopy( h->r.currentOrigin, spot );
		}
		spot[2] += 16;
		G_DS_SpawnChest( spot, 1 );
		G_LogPrintf( "DSVault: chest\n" );
	}
	switch ( acts.vaultMode ) {
	case DSV_GALLERY:
		Acts_VaultGallery( h );
		break;
	case DSV_RUSH:
		Acts_VaultRush( h );
		break;
	case DSV_SKY:
		Acts_VaultSky( h );
		break;
	default:
		// gems rain all around until the last seconds
		// (~280 drops: mostly blue, a green every 12th, a red every 70th: about 500 xp in all)
		while ( level.time >= acts.vaultNextGem && level.time < acts.vaultEnd - 3000 ) {
			acts.vaultNextGem += 150;
			if ( Acts_FloorSpot( h->r.currentOrigin, 40, 460, -1, spot ) ) {
				spot[2] += 64;
				acts.vaultGems++;
				G_DS_SpawnGems( spot, acts.vaultGems % 70 == 0 ? 25 : acts.vaultGems % 12 == 0 ? 5 : 1, -1 );
			}
		}
		break;
	}
}

/*
===============================================================================

FRAME

===============================================================================
*/

static void Acts_UpdateConfigstring( void ) {
	char	s[MAX_STRING_CHARS];

	acts.csDirty = 0;
	acts.csNext = level.time + 100;
	if ( acts.vault ) {
		Com_sprintf( s, sizeof( s ), "\\vault\\%i\\act\\%i\\vm\\%i\\vb\\%i", acts.vaultEnd, acts.act, acts.vaultMode, acts.vaultBroken );
		trap_SetConfigstring( CS_DESCENTBOSS, s );
		return;
	}
	if ( !acts.on ) {
		// fight floors: only the nemesis (its client) when it is here
		s[0] = 0;
		G_DS_NemesisInfo( s, sizeof( s ) );
		trap_SetConfigstring( CS_DESCENTBOSS, s );
		return;
	}
	// lv: the player's lives on this floor, tr: DST_ traits of this phase, gr: the respawn grace end
	Com_sprintf( s, sizeof( s ), "\\cl\\%i\\b\\%i\\hp\\%i\\max\\%i\\ph\\%i\\nph\\%i\\at\\%i\\att\\%i\\pd\\%i\\sh\\%i\\fz\\%i\\rr\\%i\\ab\\%i\\dead\\%i\\lv\\%i\\tr\\%i\\gr\\%i",
		acts.client, acts.type, acts.hp, acts.maxHp, acts.phase, acts.phases, acts.atk, acts.atkLand, acts.playerDeaths,
		acts.shieldUntil > level.time, acts.frenzy, acts.roarUntil, acts.abyss, acts.dead, G_DS_PlayerBossLives( acts.lives ), acts.traits, acts.graceUntil );
	// round 4, timed 1v1: tm 1, tl the limit (s), ts / te the clock's start / end (level times, 0 =
	// not started), kt msec of the kill, lm the last-minute enrage, tu time up, tb the time bonus
	// skulls; no boss lives (blm 0) and no roster
	if ( acts.timed ) {
		Q_strcat( s, sizeof( s ), va( "\\tm\\1\\tl\\%i\\ts\\%i\\te\\%i\\kt\\%i\\lm\\%i\\tu\\%i\\tb\\%i\\bl\\1\\blm\\0\\pr\\%i",
			acts.timeLimit / 1000, acts.fightStart, acts.fightEnd, acts.killTime, acts.lastMinute, acts.timeUp, acts.timeBonus,
			G_RL_Practice() ? 1 : 0 ) );		// pr: boss practice (no record)
		G_DS_RematchInfo( s, sizeof( s ) );		// en-rematch: rm / rmh / rmp (the HUD's modifier line)
		G_DS_NemesisInfo( s, sizeof( s ) );
		trap_SetConfigstring( CS_DESCENTBOSS, s );
		return;
	}
	// round 2b: bl = the boss's lives left (of blm), ro = the roster "client:lives left" (the player
	// first, then the rivals, then the boss), nem = the nemesis when it is one of the rivals
	{
		char		ro[256];
		gentity_t	*pl = Acts_PlayerEnt();
		int			i, left;

		ro[0] = 0;
		if ( pl ) {
			left = G_DS_PlayerBossLives( acts.lives ) - acts.playerDeaths;
			Q_strcat( ro, sizeof( ro ), va( "%i:%i", (int)( pl - g_entities ), left < 0 ? 0 : left ) );
		}
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			if ( i == acts.client || ( pl && i == pl - g_entities ) || !( g_entities[i].r.svFlags & SVF_BOT )
				|| ( !g_entities[i].inuse && !acts.out[i] ) || ( g_entities[i].inuse && !g_entities[i].client ) ) {
				continue;
			}
			if ( !acts.out[i] && level.clients[i].pers.connected != CON_CONNECTED ) {
				continue;
			}
			left = acts.out[i] ? 0 : acts.lives - acts.deaths[i];
			Q_strcat( ro, sizeof( ro ), va( "%s%i:%i", ro[0] ? "." : "", i, left < 0 ? 0 : left ) );
		}
		if ( acts.client >= 0 ) {
			Q_strcat( ro, sizeof( ro ), va( "%s%i:%i", ro[0] ? "." : "", acts.client, acts.bossLivesLeft ) );
		}
		Q_strcat( s, sizeof( s ), va( "\\bl\\%i\\blm\\%i\\ro\\%s", acts.bossLivesLeft, DS_BOSS_LIVES, ro ) );
		G_DS_NemesisInfo( s, sizeof( s ) );
	}
	trap_SetConfigstring( CS_DESCENTBOSS, s );
}

void G_DS_ActsDirty( void ) {
	acts.csDirty = 1;
}

void G_DS_ActsFrame( void ) {
	gentity_t	*boss, *target;
	int			atk;

	if ( !G_RL_Active() ) {
		return;
	}
	// g_dsBossSim: the sim player joins, the server quits after the result
	if ( actsCvSim.integer ) {
		if ( acts.simQuitAt && level.time >= acts.simQuitAt ) {
			acts.simQuitAt = level.time + 100000;
			trap_SendConsoleCommand( EXEC_APPEND, "quit\n" );
		}
		if ( !acts.simAddAt && level.time - level.startTime > 1500 && trap_AAS_Initialized() ) {
			acts.simAddAt = level.time;
			trap_SendConsoleCommand( EXEC_APPEND, "addbot ranger 5 free 0 \"Sim\"\n" );
		}
	}
	G_DS_NemesisFrame();
	if ( level.intermissiontime || level.intermissionQueued ) {
		if ( acts.atk ) {
			Acts_EndAttack();
		}
		return;
	}
	if ( acts.vault ) {
		Acts_VaultFrame();
	}
	if ( acts.on ) {
		int		i;

		boss = Acts_Boss();
		target = Acts_Human();
		if ( boss && !acts.dead && !acts.powerSet && Acts_Victim( target ) ) {
			Acts_PowerScale( target );
		}
		// who hurt the boss lately (halves in about 4 s); eliminated bots leave
		if ( level.time >= acts.hurtDecay ) {
			acts.hurtDecay = level.time + 1000;
			for ( i = 0 ; i < MAX_CLIENTS ; i++ ) {
				acts.hurt[i] *= 0.84f;
			}
		}
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			if ( acts.kickAt[i] && level.time >= acts.kickAt[i] ) {
				acts.kickAt[i] = 0;
				if ( g_entities[i].inuse && ( g_entities[i].r.svFlags & SVF_BOT ) && i != acts.client ) {
					trap_DropClient( i, "was eliminated" );
				}
			}
		}
		// round 4, timed 1v1: the clock starts once the boss is in and opens fire; the last minute
		// enrages it; at 0:00 the floor is lost
		if ( acts.timed && !acts.dead && !acts.timeUp ) {
			if ( !acts.fightStart && acts.bossInAt && Acts_Alive( boss ) && Acts_Alive( Acts_Human() )
				&& level.time >= acts.bossInAt + DS_FIGHT_DELAY ) {
				acts.fightStart = level.time;
				acts.fightEnd = level.time + acts.timeLimit;
				acts.csDirty = 1;
				G_LogPrintf( "DSBoss: timed fight starts, %i s on the clock (%i ms after the floor start)\n", acts.timeLimit / 1000,
					level.time - level.startTime );
			}
			if ( acts.fightStart ) {
				i = acts.fightEnd - level.time;
				if ( !acts.lastMinute && ( ( i <= DS_LAST_MINUTE && acts.timeLimit > DS_LAST_MINUTE ) || G_DS_RmEnraged() ) ) {		// (en-rematch: ENRAGED)
					acts.lastMinute = qtrue;
					acts.csDirty = 1;
					G_LogPrintf( "DSBoss: the last minute: enraged (bar left %.3f, phase %i, player deaths %i)\n", Acts_BarLeft(), acts.phase,
						acts.playerDeaths );
				}
				if ( i <= 0 || G_DS_RmFallen( acts.playerDeaths ) ) {		// (en-rematch: ONE LIFE ends it like the clock)
					acts.timeUp = qtrue;
					acts.deathTime = level.time;
					Acts_BossDown();
					acts.holdFireUntil = level.time + 600000;
					acts.csDirty = 1;
					G_LogPrintf( "DSBoss: TIME UP, bar left %.3f, phase %i, player deaths %i\n", Acts_BarLeft(), acts.phase, acts.playerDeaths );
				}
			}
		}
		target = acts.target >= 0 && acts.target < MAX_CLIENTS ? &g_entities[acts.target] : NULL;
		if ( boss && !acts.dead && !acts.timeUp ) {
			boss->r.svFlags |= SVF_BROADCAST;		// always in the snapshot: the HUD's boss locator (cg_ds_acts.c)
			// the pool lives here: keep the bot itself healthy and without armor
			if ( boss->health > 0 ) {
				boss->health = boss->client->ps.stats[STAT_HEALTH] = boss->client->ps.stats[STAT_MAX_HEALTH] > 0
					? boss->client->ps.stats[STAT_MAX_HEALTH] : 100;
				boss->client->ps.stats[STAT_ARMOR] = 0;
			}
			if ( acts.atk ) {
				if ( !Acts_Alive( boss ) ) {
					Acts_EndAttack();
				} else {
					Acts_RunAttack( boss, target );
				}
			} else if ( level.time >= acts.nextAttack && level.time >= acts.roarUntil && Acts_Alive( boss ) ) {
				i = Acts_PickTarget( boss );
				atk = DSB_NONE;
				if ( i >= 0 ) {
					acts.target = i;
					target = &g_entities[i];
					atk = Acts_PickAttack( boss, target );
				}
				if ( atk ) {
					acts.cooldown[atk] = level.time + Acts_Cooldown( atk );
					Acts_StartAttack( atk, boss, target );
				} else {
					acts.nextAttack = level.time + 500;
				}
			}
			Acts_RunStrikes( boss );
			Acts_RunLasting( boss );
		} else {
			Acts_RunStrikes( boss );
		}
	}
	// g_dsBossSim: bots only shoot clients, so the sim "spends" its time on drones and decoys the
	// way a human would: each one breaks 4 s (+1.5 s per slot) after it appeared, if the sim lives
	if ( acts.on && !acts.dead && actsCvSim.integer ) {
		int			i;
		gentity_t	*sim;

		sim = Acts_Human();
		for ( i = 0 ; i < DS_MAX_PROPS ; i++ ) {
			if ( acts.props[i] && acts.props[i]->inuse && acts.props[i]->takedamage && Acts_Alive( sim )
				&& level.time > acts.props[i]->s.time + 4000 + 1500 * acts.props[i]->s.frame ) {
				G_Damage( acts.props[i], sim, sim, NULL, NULL, acts.props[i]->health + 50, DAMAGE_NO_PROTECTION, MOD_MACHINEGUN );
			}
		}
	}
	// boss floors: the player is back in the fight about 2.3 s after a death
	if ( acts.on && !acts.dead ) {
		target = Acts_Human();
		if ( target && target->client && !( target->r.svFlags & SVF_BOT ) && target->health <= 0
			&& target->client->ps.pm_type == PM_DEAD && level.time > target->client->respawnTime + 600 ) {
			ClientRespawn( target );
		}
	}
	if ( acts.csDirty && level.time >= acts.csNext ) {
		Acts_UpdateConfigstring();
	}
}

/*
=================
G_DS_ActsCommand

Dev (server console): dsattack <n | name>, dsphase, dsbossdmg <n>, dskillboss, dsbossinfo, dsvaultend
=================
*/
qboolean G_DS_ActsCommand( const char *cmd ) {
	char		arg[64];
	gentity_t	*boss, *h;
	int			i, n;

	if ( !Q_stricmp( cmd, "dsbossinfo" ) ) {
		G_Printf( "dsboss: on %i vault %i act %i floor %i type %i client %i phase %i/%i hp %i/%i atk %i deaths %i dead %i abyss %i\n",
			acts.on, acts.vault, acts.act, acts.floor, acts.type, acts.client, acts.phase, acts.phases, acts.hp, acts.maxHp, acts.atk,
			acts.playerDeaths, acts.dead, acts.abyss );
		G_LogPrintf( "DSBoss: info on %i vault %i act %i floor %i type %i client %i phase %i/%i hp %i/%i deaths %i dead %i abyss %i vaultgems %i\n",
			acts.on, acts.vault, acts.act, acts.floor, acts.type, acts.client, acts.phase, acts.phases, acts.hp, acts.maxHp,
			acts.playerDeaths, acts.dead, acts.abyss, acts.vaultGems );
		G_LogPrintf( "DSBoss: info boss lives %i of %i, target %i, relocations %i (near %i)\n", acts.bossLivesLeft, DS_BOSS_LIVES,
			acts.target, acts.relocations, acts.relocNear );
		return qtrue;
	}
	if ( G_DS_NemesisCommand( cmd ) ) {		// dsnemesis <name> [kills] | clear
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsvaultend" ) ) {
		if ( acts.vault && acts.vaultEnd ) {
			acts.vaultEnd = level.time + 500;
			acts.csDirty = 1;
		}
		return qtrue;
	}
	boss = Acts_Boss();
	h = Acts_Human();
	if ( !Q_stricmp( cmd, "dsshow" ) ) {
		// screenshots: the boss <dist> units in front of the player facing them, the adds out of the way
		vec3_t	fwd, pos, ang, d;
		trace_t	tr;
		float	dist;

		if ( !boss || !h || !h->client ) {
			return qtrue;
		}
		trap_Argv( 1, arg, sizeof( arg ) );
		dist = atof( arg ) > 0 ? atof( arg ) : 380;
		// the player turns to the most open direction
		{
			float	best = -1, yaw, bestYaw = h->client->ps.viewangles[YAW];
			int		k;

			for ( k = 0 ; k < 16 ; k++ ) {
				yaw = k * 22.5f;
				VectorSet( ang, 0, yaw, 0 );
				AngleVectors( ang, fwd, NULL, NULL );
				VectorMA( h->client->ps.origin, dist, fwd, pos );
				trap_Trace( &tr, h->client->ps.origin, boss->r.mins, boss->r.maxs, pos, h->s.number, MASK_PLAYERSOLID );
				if ( tr.fraction > best ) {
					// and floor to stand on there
					trace_t	down;
					vec3_t	under;

					VectorCopy( tr.endpos, under );
					under[2] -= 160;
					trap_Trace( &down, tr.endpos, boss->r.mins, boss->r.maxs, under, h->s.number, MASK_PLAYERSOLID );
					if ( down.fraction < 1.0f ) {
						best = tr.fraction;
						bestYaw = yaw;
					}
				}
			}
			VectorSet( ang, 0, bestYaw, 0 );
			SetClientViewAngle( h, ang );
		}
		VectorSet( ang, 0, h->client->ps.viewangles[YAW], 0 );
		AngleVectors( ang, fwd, NULL, NULL );
		VectorMA( h->client->ps.origin, dist, fwd, pos );
		trap_Trace( &tr, h->client->ps.origin, boss->r.mins, boss->r.maxs, pos, h->s.number, MASK_PLAYERSOLID );
		VectorCopy( tr.endpos, pos );
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			gentity_t	*e = &g_entities[i];

			if ( e == h || !e->inuse || !e->client || e->client->pers.connected != CON_CONNECTED || e->health <= 0 ) {
				continue;
			}
			if ( e != boss ) {
				continue;		// the adds stay where they are
			}
			VectorCopy( pos, d );
			VectorCopy( d, e->client->ps.origin );
			VectorClear( e->client->ps.velocity );
			e->client->ps.eFlags ^= EF_TELEPORT_BIT;
			VectorSet( ang, 0, h->client->ps.viewangles[YAW] + ( e == boss ? 180 : 0 ), 0 );
			SetClientViewAngle( e, ang );
			BG_PlayerStateToEntityState( &e->client->ps, &e->s, qtrue );
			VectorCopy( d, e->r.currentOrigin );
			trap_LinkEntity( e );
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsattack" ) ) {
		if ( !boss || !Acts_Victim( h ) ) {
			G_Printf( "dsattack: no boss / player\n" );
			return qtrue;
		}
		trap_Argv( 1, arg, sizeof( arg ) );
		n = atoi( arg );
		if ( !n && arg[0] ) {
			// any word of the name: "hook", "slam", "lances", "nova" ...
			for ( i = 1 ; i < DSB_NUM && !n ; i++ ) {
				const char	*w = ds_bossAttackNames[i];

				while ( *w ) {
					if ( !Q_stricmpn( w, arg, strlen( arg ) ) ) {
						n = i;
						break;
					}
					while ( *w && *w != ' ' ) {
						w++;
					}
					while ( *w == ' ' ) {
						w++;
					}
				}
			}
		}
		if ( n <= 0 || n >= DSB_NUM || n == DSB_FRENZY || n == DSB_SHIELD || n == DSB_ROAR ) {
			G_Printf( "dsattack: unknown attack '%s'\n", arg );
			return qtrue;
		}
		if ( acts.atk ) {
			Acts_EndAttack();
		}
		acts.roarUntil = 0;
		Acts_StartAttack( n, boss, h );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsphase" ) ) {
		// the boss down to the life before the next phase, then the player takes that life
		if ( boss && h && !acts.dead && acts.phase < acts.phases && boss->health > 0 ) {
			if ( !acts.timed ) {
				acts.bossLivesLeft = acts.phase == 1 ? 7 : 4;
			}
			acts.hp = 1;
			acts.roarUntil = 0;
			G_Damage( boss, h, h, NULL, NULL, 50, DAMAGE_NO_PROTECTION | DAMAGE_NO_ARMOR, MOD_UNKNOWN );
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsbosstime" ) ) {
		// dev (timed 1v1): dsbosstime <seconds left> (starts the clock if it hasn't)
		trap_Argv( 1, arg, sizeof( arg ) );
		n = atoi( arg );
		if ( acts.on && acts.timed && !acts.dead && !acts.timeUp && n >= 0 ) {
			acts.fightEnd = level.time + n * 1000;
			acts.fightStart = acts.fightEnd - acts.timeLimit;		// (the limit stays: the bonus, the last minute)
			if ( n * 1000 > DS_LAST_MINUTE ) {
				acts.lastMinute = qfalse;
			}
			acts.csDirty = 1;
			G_LogPrintf( "DSBoss: dsbosstime %i s left\n", n );
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsbosslife" ) ) {
		// dev: the player takes one boss life
		if ( boss && h && !acts.dead && boss->health > 0 ) {
			acts.hp = 1;
			acts.roarUntil = 0;
			G_Damage( boss, h, h, NULL, NULL, 50, DAMAGE_NO_PROTECTION | DAMAGE_NO_ARMOR, MOD_UNKNOWN );
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsbossdmg" ) || !Q_stricmp( cmd, "dskillboss" ) ) {
		if ( !boss || !h ) {
			return qtrue;
		}
		if ( !Q_stricmp( cmd, "dskillboss" ) ) {
			acts.bossLivesLeft = 1;
			Acts_SetPhase( 3 );
			acts.hp = 1;
			acts.roarUntil = 0;
			n = 50;
		} else {
			trap_Argv( 1, arg, sizeof( arg ) );
			n = atoi( arg );
		}
		if ( n > 0 ) {
			G_Damage( boss, h, h, NULL, NULL, n, DAMAGE_NO_PROTECTION | DAMAGE_NO_ARMOR, MOD_UNKNOWN );
		}
		return qtrue;
	}
	return qfalse;
}
