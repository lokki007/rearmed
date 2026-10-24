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
// bg_public.h -- definitions shared by both the server game and client game modules

// because games can change separately from the main system version, we need a
// second version that must match between game and cgame

#define	GAME_VERSION		"rearmed-1"
#define	REARMED_VERSION		"0.6.1-beta"		// shown in the menus and as the rearmed_version serverinfo cvar

// Team Arena weapons (nailgun, prox launcher, chaingun) in the base game
#define TA_WEAPONS

#define	DEFAULT_GRAVITY		800
#define	GIB_HEALTH			-40
#define	ARMOR_PROTECTION	0.66

#define	MAX_ITEMS			256

#define	RANK_TIED_FLAG		0x4000

#define DEFAULT_SHOTGUN_SPREAD	700
#define DEFAULT_SHOTGUN_COUNT	11

#define	ITEM_RADIUS			15		// item sizes are needed for client side pickup detection

#define	LIGHTNING_RANGE		768

#define	SCORE_NOT_PRESENT	-9999	// for the CS_SCORES[12] when only one player is present

#define	VOTE_TIME			30000	// 30 seconds before vote times out

#define PLAYER_WIDTH		15
#define	MINS_Z				-24
#define DEFAULT_HEIGHT		32
#define	DEFAULT_VIEWHEIGHT	26
#define CROUCH_HEIGHT		16
#define CROUCH_VIEWHEIGHT	12
#define DEAD_HEIGHT			-8
#define	DEAD_VIEWHEIGHT		-16
#define INVUL_RADIUS		42

//
// config strings are a general means of communicating variable length strings
// from the server to all connected clients.
//

// CS_SERVERINFO and CS_SYSTEMINFO are defined in q_shared.h
#define	CS_MUSIC				2
#define	CS_MESSAGE				3		// from the map worldspawn's message field
#define	CS_MOTD					4		// g_motd string for server message of the day
#define	CS_WARMUP				5		// server time when the match will be restarted
#define	CS_SCORES1				6
#define	CS_SCORES2				7
#define CS_VOTE_TIME			8
#define CS_VOTE_STRING			9
#define	CS_VOTE_YES				10
#define	CS_VOTE_NO				11

#define CS_TEAMVOTE_TIME		12
#define CS_TEAMVOTE_STRING		14
#define	CS_TEAMVOTE_YES			16
#define	CS_TEAMVOTE_NO			18

#define	CS_GAME_VERSION			20
#define	CS_LEVEL_START_TIME		21		// so the timer only shows the current level
#define	CS_INTERMISSION			22		// when 1, fraglimit/timelimit has been hit and intermission will start in a second or two
#define CS_FLAGSTATUS			23		// string indicating flag status in CTF
#define CS_SHADERSTATE			24
#define CS_BOTINFO				25

#define	CS_ITEMS				27		// string of 0's and 1's that tell which items are present
#define	CS_REARMED				28		// info string: game mode, mutator, juggernaut
#define	CS_DESCENT				29		// info string: the descent run (floor, lives, skulls, cards, level end, offer)
#define	CS_DESCENTKITS			30		// "client:role:c.c.c " per player: everyone's cards this floor

#define	CS_MODELS				32
#define	CS_SOUNDS				(CS_MODELS+MAX_MODELS)
#define	CS_PLAYERS				(CS_SOUNDS+MAX_SOUNDS)
#define CS_LOCATIONS			(CS_PLAYERS+MAX_CLIENTS)
#define CS_PARTICLES			(CS_LOCATIONS+MAX_LOCATIONS)

#define CS_MAX					(CS_PARTICLES+MAX_LOCATIONS)
#define CS_PRELOAD				738		// ds-fixes: "model hmodel;..." bot models coming this map (736 CS_DSSKILLS, 737 CS_DSINTRO)

#if (CS_MAX) > MAX_CONFIGSTRINGS
#error overflow: (CS_MAX) > MAX_CONFIGSTRINGS
#endif

typedef enum {
	GT_FFA,				// free for all
	GT_TOURNAMENT,		// one on one tournament
	GT_SINGLE_PLAYER,	// single player ffa

	//-- team games go after this --

	GT_TEAM,			// team deathmatch
	GT_CTF,				// capture the flag
	GT_1FCTF,
	GT_OBELISK,
	GT_HARVESTER,
	GT_MAX_GAME_TYPE
} gametype_t;

typedef enum { GENDER_MALE, GENDER_FEMALE, GENDER_NEUTER } gender_t;

/*
===================================================================================

PMOVE MODULE

The pmove code takes a player_state_t and a usercmd_t and generates a new player_state_t
and some other output data.  Used for local prediction on the client game and true
movement on the server game.
===================================================================================
*/

typedef enum {
	PM_NORMAL,		// can accelerate and turn
	PM_NOCLIP,		// noclip movement
	PM_SPECTATOR,	// still run into walls
	PM_DEAD,		// no acceleration or turning, but free falling
	PM_FREEZE,		// stuck in place with no control
	PM_INTERMISSION,	// no movement or status bar
	PM_SPINTERMISSION	// no movement or status bar
} pmtype_t;

typedef enum {
	WEAPON_READY,
	WEAPON_RAISING,
	WEAPON_DROPPING,
	WEAPON_FIRING
} weaponstate_t;

// pmove->pm_flags
#define	PMF_DUCKED			1
#define	PMF_JUMP_HELD		2
#define	PMF_BACKWARDS_JUMP	8		// go into backwards land
#define	PMF_BACKWARDS_RUN	16		// coast down to backwards run
#define	PMF_TIME_LAND		32		// pm_time is time before rejump
#define	PMF_TIME_KNOCKBACK	64		// pm_time is an air-accelerate only time
#define	PMF_TIME_WATERJUMP	256		// pm_time is waterjump
#define	PMF_RESPAWNED		512		// clear after attack and jump buttons come up
#define	PMF_USE_ITEM_HELD	1024
#define PMF_GRAPPLE_PULL	2048	// pull towards grapple location
#define PMF_FOLLOW			4096	// spectate following another player
#define PMF_SCOREBOARD		8192	// spectate as a scoreboard
#define PMF_INVULEXPAND		16384	// invulnerability sphere set to full size

#define	PMF_ALL_TIMES	(PMF_TIME_WATERJUMP|PMF_TIME_LAND|PMF_TIME_KNOCKBACK)

// rearmed: movement tech state (the unused pm_flags bits)
#define PMF_DOUBLE_JUMPED	4		// used the air jump since leaving the ground
#define PMF_DASH_HELD		128		// dash button must be released before dashing again
#define PMF_AIR_DASHED		32768	// (unused since dash charges)

// rearmed: feature switches. The server copies its g_ cvars into stats[STAT_RMFLAGS]
// before every move, so client prediction always runs the same rules as the server.
#define RMF_DOUBLEJUMP		0x0001
#define RMF_WALLJUMP		0x0002
#define RMF_WALLRUN			0x0004
#define RMF_DASH			0x0008
#define RMF_JUMPASSIST		0x0010		// coyote time + jump buffering
#define RMF_ALTFIRE			0x0020
#define RMF_SHOTGUNKICK		0x0040
#define RMF_SPINUP			0x0080
#define RMF_CHARGE			0x0800		// hold alt-fire to charge it
#define RMF_CONFIG			0x08ff		// the bits the server owns
// pmove state kept in the same stat
#define RMS_WALLRUN			0x0100		// running along a wall
#define RMS_WALLRUN_LEFT	0x0200		// ... and the wall is on the left
#define RMS_HOOKREFRESH		0x0400		// this grapple pull already restored the air moves
// perks the movement code needs (set by the server from the player's perks)
#define RMP_QUICKHANDS		0x1000		// fire 20% faster
#define RMP_THIRDWIND		0x2000		// a third dash charge
#define RMP_OVERCHARGER		0x4000		// alt-fire charges 40% faster
#define RMP_ALL				0x7000

// rearmed: a second flags word in ps->ammo[AMMO_RMFLAGS2]. The ammo slots past the last weapon
// are free; they travel as signed 16-bit shorts, so only the low 15 bits are used.
#define AMMO_RMFLAGS2		14
#define AMMO_RMSTATE2		15			// pmove timer: msec of slide left
#define RM_FLAGS2(ps)		( (ps)->ammo[AMMO_RMFLAGS2] )
#define RMF2_SLIDE			0x0001		// crouch while running on the ground to slide
#define RMF2_SLAM			0x0002		// crouch in the air to ground pound
#define RMF2_ICE			0x0004		// mutator: ice rink, almost no ground friction
#define RMF2_LOWGRAV		0x0008		// (reserved)
#define RMF2_OVERCHARGE		0x0010		// mutator: alt-fire charges three times as fast
#define RMF2_ZIPLINE		0x0020		// zip line (BUTTON_ZIP) allowed
#define RMF2_DASH1			0x0040		// roguelike: only one dash charge (until a DASH CHARGE card)
#define RMF2_CONFIG			0x00ff		// the bits the server owns
#define RMS2_SLIDING		0x0100		// state: sliding
#define RMS2_SLAMMING		0x0200		// state: ground pound in progress
#define RMS2_CROUCH_HELD	0x0400		// crouch must be released before the next slide / slam
#define RMS2_ALL			0x0f00
#define RMP2_BLINK			0x1000		// perk: the dash is a short teleport
#define RMP2_SLIPSTREAM		0x2000		// perk: longer slides that keep their speed
#define RMP2_FEATHER		0x4000		// perk: a second air jump, no fall damage
#define RMP2_ALL			0x7000

// rearmed: ps->generic1 of a player (copied to its entity and its corpse): how the body died
#define RM_BODY_HEADLESS	1			// headshot kill: no head, the neck bleeds
#define RM_BODY_CHARRED		2			// burned or electrocuted to death
#define RM_BODY_SPLIT		4			// overkill: torn in half (torso gone)
#define RM_BODY_OVERKILL	8			// gibbed by far more damage than needed: a bigger gib
#define RM_BODY_ALL			15
#define RM_BODY_ELITE		16			// roguelike: a living elite bot (aura), set at spawn
#define RM_BODY_BOSS		32			// roguelike: a living boss bot

// rearmed: ps->loopSound is unused by the game code, so it carries more pmove state
// (BG_PlayerStateToEntityState must not copy it into the entity's loopSound):
// bits 1..WP_NUM_WEAPONS-1 = alt-fire unlocked for that weapon (all set outside roguelike),
// RMZ_ZIPPING = riding a zip line (anchor in ps->grapplePoint). Keep below 0x8000.
#define RM_ALTMASK_ALL		0x3ffe
#define RMZ_ZIPPING			0x4000
#define RM_ALT_ALLOWED(ps, w)	( ( (ps)->loopSound >> (w) ) & 1 )
// what pmove, the server alt triggers and the bots test (a locked alt does nothing at all)
// (the server writes the mask every ClientThink before Pmove: RM_ALTMASK_ALL outside descent)
#define RM_ALT_USABLE(ps, w)	RM_ALT_ALLOWED( ps, w )
// zip line state (bg_pmove.c PM_ZipLine). loopSound travels as an unsigned 16-bit field.
// The server keeps ( loopSound & RMZ_STATE ) when it rewrites the alt mask.
#define RMZ_HELD			0x0001		// zip button was down last frame (bit 0 = WP_NONE, never an alt)
#define RMZ_USED			0x8000		// zip fired this air time (cleared on the ground, in water, by a grapple pull)
#define RMZ_STATE			0xc001		// RMZ_ZIPPING | RMZ_HELD | RMZ_USED
// entityState->generic1 bit of a living player riding a zip line (the anchor is in s->origin2)
#define RM_BODY_ZIP			64
#define ZIP_RANGE			2000		// hitscan reach
#define ZIP_MIN_DIST		96			// closer anchors are a miss
#define ZIP_SPEED			1000		// cruise speed (faster riders keep their speed)
#define ZIP_START_SPEED		420			// speed right after attaching
#define ZIP_ACCEL			2600		// ups per second up to ZIP_SPEED
#define ZIP_ARRIVE_DIST		48			// this close to the anchor drops you off
#define ZIP_HOP				260			// up speed of the drop-off hop
#define ZIP_RELEASE_KICK	140			// up speed added when letting go
// why a zip press held or not (BG_ZipAim result, the EV_ZIP_MISS eventParm)
#define ZIPR_OK				0
#define ZIPR_NOTHING		1			// sky, or nothing within ZIP_RANGE
#define ZIPR_CLOSE			2			// the surface is closer than ZIP_MIN_DIST
#define ZIPR_USED			3			// already zipped this air time
#define ZIPR_LOCKED			4			// switched off (g_zipLine) or not unlocked (descent card)
// The aim from the eye along forward: players, bodies and props in the way are passed
// (the cable holds on the wall behind them), the world and movers hold it.
int BG_ZipAim( void (*trace)( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs,
	const vec3_t end, int passEntityNum, int contentMask ), const vec3_t eye, const vec3_t forward,
	int passEntityNum, vec3_t anchor, float *dist );


// rearmed: entityState->generic1 markers of ET_GENERAL rearmed entities (fire patch is RM_ENT_FIRE)
#define RM_ENT_PROP			65			// physics prop: modelindex2 = propType_t
#define RM_ENT_METEOR		66			// (meteors are ET_MISSILE with generic1 bit RM_MISSILE_METEOR)
#define RM_MISSILE_METEOR	8			// entityState->generic1 bit on a falling meteor
#define RM_ENT_STRIKE		67			// ET_GENERAL danger marker: origin = impact point, time2 = impact
										// time, otherEntityNum2 = radius, modelindex2 = RM_STRIKE_ kind
#define RM_STRIKE_METEOR	0
#define RM_STRIKE_LIGHTNING	1
#define RM_STRIKE_CORPSE	2			// chain reaction: a corpse about to blow up

typedef enum {
	PROP_BARREL,		// explosive: fire + blast, can be kicked and knocked around
	PROP_CRATE,			// breaks into debris, drops a small item sometimes
	PROP_TANK,			// gas tank: ruptures, spins off like a rocket, then explodes
	PROP_NUM_TYPES
} propType_t;

// rearmed: prop state bits in entityState->frame (g_physics.c sets them, cgame and bots read them)
#define PROPF_BURNING		1			// on fire: explodes at s.time2 (level time)
#define PROPF_RUPTURED		2			// gas tank venting: flies like a rocket, explodes at s.time2
#define PROPF_LYING			4			// lying on its side (barrels / tanks roll)
#define PROPF_FUSE			8			// about to blow (chain reaction / splash fuse), explodes at s.time2
#define PROPF_AIRBORNE		16			// in flight (tumbles)
#define PROPF_DANGER		( PROPF_BURNING | PROPF_RUPTURED | PROPF_FUSE )
#define PROP_BLAST_RADIUS		200		// barrel explosion (MOD_PROP)
#define PROP_BLAST_DAMAGE		110
#define PROP_TANK_BLAST_RADIUS	170		// gas tank explosion
#define PROP_TANK_BLAST_DAMAGE	90

// rearmed: style meter
#define STYLE_MAX			1000
#define STYLE_RANK_D		60
#define STYLE_RANK_C		180
#define STYLE_RANK_B		320
#define STYLE_RANK_A		460
#define STYLE_RANK_S		600
#define STYLE_RANK_SS		740
#define STYLE_RANK_SSS		880

// rearmed: perks (pick one of three every few kills, kept for the match)
typedef enum {
	PERK_THICKSKIN,
	PERK_HEAVYCALIBER,
	PERK_QUICKHANDS,
	PERK_THIRDWIND,
	PERK_OVERCHARGER,
	PERK_BLOODTHIRST,
	PERK_HEADHUNTER,
	PERK_SKYHUNTER,
	PERK_PYROMANIAC,
	PERK_CONDUCTOR,
	PERK_IRONCLAD,
	PERK_SCAVENGER,
	PERK_ADRENALINE,		// kills refill the dash charges and heal a little
	PERK_GLASSCANNON,		// +35% damage, -35 max health
	PERK_BLINK,				// the dash is a short teleport (pmove, RMP2_BLINK)
	PERK_GROUNDZERO,		// ground pound shockwave twice as big
	PERK_SLIPSTREAM,		// long slides that keep their speed (pmove, RMP2_SLIPSTREAM)
	PERK_RICOCHET,			// rockets, grenades and plasma bounce off the first wall
	PERK_CHAINREACTION,		// your victims explode a second after dying
	PERK_LASTSTAND,			// under 30 health: +50% damage dealt, -25% taken
	PERK_FEATHERWEIGHT,		// a second air jump, no fall damage (pmove, RMP2_FEATHER)
	PERK_MAGNETHOOK,		// the grapple yanks enemies to you
	PERK_BUTCHER,			// gib kills drop a health bubble
	PERK_KINETIC,			// damage grows with your speed
	PERK_MIRAGE,			// dashing turns you invisible for a moment
	PERK_THORNS,			// 30% of the damage you take hits back
	PERK_NUM
} rmPerk_t;

extern const char *bg_perkNames[PERK_NUM];
extern const char *bg_perkDescs[PERK_NUM];

#include "bg_descent.h"	// rearmed: DESCENT v2 contract (acts, weapon levels, passives, evolutions, ...)

// rearmed: roguelike reward cards. The run's cards live in rearmed_run.txt ("cards" key).
typedef enum {
	// abilities (locked at the start of a run)
	RLC_DOUBLEJUMP,
	RLC_WALLJUMP,
	RLC_WALLRUN,
	RLC_DASH,				// unlocks the dash with one charge
	RLC_DASHCHARGE,			// +1 dash charge (stacks to 3 with the base, Third Wind adds one more)
	RLC_GRAPPLE,
	RLC_ZIPLINE,
	RLC_SLIDE,
	RLC_SLAM,
	RLC_CHARGE,				// charged alt-fire (needs that weapon's alt unlocked)
	// alt-fire per weapon: RLC_ALT_FIRST + weapon - WP_GAUNTLET
	RLC_ALT_GAUNTLET,
	RLC_ALT_MACHINEGUN,
	RLC_ALT_SHOTGUN,
	RLC_ALT_GRENADE,
	RLC_ALT_ROCKET,
	RLC_ALT_LIGHTNING,
	RLC_ALT_RAILGUN,
	RLC_ALT_PLASMA,
	RLC_ALT_BFG,
	RLC_ALT_GRAPPLE_UNUSED,	// WP_GRAPPLING_HOOK has no alt-fire; never offered
	RLC_ALT_NAILGUN,
	RLC_ALT_PROX,
	RLC_ALT_CHAINGUN,
	// stats (stack)
	RLC_SPEED,				// +6% move speed
	RLC_HEALTH,				// +15 max health
	RLC_DAMAGE,				// +8% damage
	RLC_ARMOR,				// spawn with +25 armor
	RLC_DASHCD,				// dash recharges 12% faster (server-side scale of STAT_DASH_TIME)
	RLC_AMMO,				// +50% ammo from pickups
	// perks: RLC_PERK_FIRST + perk (one each)
	RLC_PERK_FIRST,
	// DESCENT v2: a passive item level (RLC_PASSIVE_FIRST + dsPassive_t, up to 5 levels each)
	RLC_PASSIVE_FIRST = RLC_PERK_FIRST + PERK_NUM,
	// +1 weapon level (RLC_WEAPON_FIRST + weapon - WP_GAUNTLET; the grapple slot is never offered)
	RLC_WEAPON_FIRST = RLC_PASSIVE_FIRST + RLP_NUM,
	// secret cards (RLC_SECRET_FIRST + dsSecret_t), offered only once unlocked
	RLC_SECRET_FIRST = RLC_WEAPON_FIRST + ( DS_NUM_WEAPONS - 1 ),
	// round 3 (ds-cards): LEGENDARY cards (RLC_LEGEND_FIRST + dsLegend_t), rare, the player's only
	RLC_LEGEND_FIRST = RLC_SECRET_FIRST + RLS_NUM,
	RLC_NUM = RLC_LEGEND_FIRST + LG_NUM
} rlCard_t;

#define RLC_ALT_FIRST		RLC_ALT_GAUNTLET
#define RLC_IS_ALT(c)		( (c) >= RLC_ALT_GAUNTLET && (c) <= RLC_ALT_CHAINGUN )
#define RL_MAX_CARDS		128		// cards held in one run (a long endless run)
#define RLK_ABILITY			0		// bg_rlCardKind values
#define RLK_ALT				1
#define RLK_STAT			2
#define RLK_PERK			3
#define RLK_PASSIVE			4		// DESCENT v2
#define RLK_WEAPON			5
#define RLK_SECRET			6
#define RLC_IS_PASSIVE(c)	( (c) >= RLC_PASSIVE_FIRST && (c) < RLC_WEAPON_FIRST )
#define RLC_IS_WEAPON(c)	( (c) >= RLC_WEAPON_FIRST && (c) < RLC_SECRET_FIRST )
#define RLC_IS_SECRET(c)	( (c) >= RLC_SECRET_FIRST && (c) < RLC_LEGEND_FIRST )
#define RLK_LEGEND			7		// round 3 (ds-cards): legendary cards
#define RLC_IS_LEGEND(c)	( (c) >= RLC_LEGEND_FIRST && (c) < RLC_NUM )
#define RLRAR_LEGEND		3		// bg_rlCardRarity: 0 common, 1 rare, 2 epic, 3 legendary
#define RLC_WEAPON_OF(c)	( (c) - RLC_WEAPON_FIRST + 1 )		// weapon_t of a weapon card
#define RLC_FOR_WEAPON(w)	( RLC_WEAPON_FIRST + (w) - 1 )
extern const char *bg_rlCardNames[RLC_NUM];
extern const char *bg_rlCardDescs[RLC_NUM];
const char *BG_RLCardBinding( int card );						// the command an ability card's key is bound to, or NULL
const char *BG_RLCardDescKey( int card, const char *keyName );	// the description naming that key
extern const char *bg_rlCardShort[RLC_NUM];	// HUD tag ("2J", "WR", ...)
extern const int bg_rlCardKind[RLC_NUM];	// RLK_
extern const int bg_rlCardRarity[RLC_NUM];	// 0 common, 1 rare, 2 epic, 3 legendary (RLRAR_LEGEND)
// perk categories (card colour, offer variety)
#define PERKCAT_OFFENSE		0
#define PERKCAT_DEFENSE		1
#define PERKCAT_MOVEMENT	2
#define PERKCAT_CHAOS		3
extern const int bg_perkCategory[PERK_NUM];

// rearmed: game modes (g_rearmMode)
typedef enum {
	RM_MODE_NORMAL,
	RM_MODE_GUNGAME,
	RM_MODE_JUGGERNAUT,
	RM_MODE_INSTAGIB,
	RM_MODE_OITC,			// one in the chamber
	RM_MODE_ROGUELIKE,		// a level of a roguelike run (rearmed_run.txt), started from the ROGUELIKE menu
	RM_NUM_MODES
} rmMode_t;

// rearmed: feature switches (g_ cvars), bit numbers in the CS_REARMED "sw" value
typedef enum {
	RS_DOUBLEJUMP,
	RS_WALLJUMP,
	RS_WALLRUN,
	RS_DASH,
	RS_JUMPASSIST,
	RS_ALTFIRE,
	RS_SHOTGUNKICK,
	RS_SPINUP,
	RS_RICOCHET,
	RS_STICKY,
	RS_SINGULARITY,
	RS_BFGLASERS,
	RS_UPPERCUT,
	RS_HEADSHOTS,
	RS_AIRSHOTS,
	RS_KILLSTREAKS,
	RS_BOUNTY,
	RS_MEDALS,
	RS_POWERUPS,
	RS_ELEMENTS,
	RS_CHARGE,
	RS_STYLE,
	RS_PERKS,
	RS_SLIDE,
	RS_SLAM,
	RS_PROPS,			// physics props (barrels, crates, tanks) scattered over the map
	RS_CORPSEPHYSICS,	// corpses and gibs react to blasts, wall splats
	RS_SLOWMO,			// slow motion on big kills (single player / listen server only)
	RS_ZIPLINE,			// zip line on BUTTON_ZIP (g_zipLine)
	RS_NUM
} rmSwitch_t;

// rearmed: match mutators (g_mutators)
typedef enum {
	MUT_NONE,
	MUT_LOW_GRAVITY,
	MUT_VAMPIRE,
	MUT_ROCKET_FRENZY,
	MUT_SPEED_RUSH,
	MUT_BIG_HEADS,
	MUT_KNOCKBACK_CHAOS,
	MUT_QUAD_MADNESS,
	MUT_JETPACKS,
	MUT_BLOOD_MOON,			// every kill gibs, lifesteal, red sky
	MUT_METEOR_SHOWER,		// burning rocks fall from the sky
	MUT_OVERCHARGE,			// alt-fire charges three times as fast
	MUT_BULLET_TIME,		// every kill slows time for a moment
	MUT_ICE_RINK,			// no ground friction
	MUT_BARREL_BONANZA,		// explosive barrels everywhere
	MUT_WEAPON_ROULETTE,	// everyone's weapon changes every few seconds
	MUT_THUNDERSTORM,		// lightning strikes whoever stands in the open
	MUT_CHAIN_REACTION,		// every corpse explodes
	MUT_NUM_MUTATORS
} rmMutator_t;

extern const char *bg_rmModeNames[RM_NUM_MODES];
extern const char *bg_rmMutatorNames[MUT_NUM_MUTATORS];
extern const char *bg_rmMutatorHints[MUT_NUM_MUTATORS];

#define GG_LEVELS	11
extern const int bg_gunGameLadder[GG_LEVELS];

#define EF_JETTING		EF_KAMIKAZE		// jetpack thrusting (kamikaze is Team Arena only)
#define JETPACK_THRUST	1500		// upward acceleration while holding jump
#define JETPACK_MAX_UP	380			// max climb speed

// rearmed: alt-fire modes
typedef enum {
	ALTFIRE_NONE,
	ALTFIRE_SHOT,		// fires through pmove like the primary attack
	ALTFIRE_TRIGGER		// server side action on press (remote detonation)
} altFireMode_t;

typedef struct {
	altFireMode_t	mode;
	int				ammo;		// ammo used per alt shot
	int				time;		// refire time in msec
	int				chargeAmmo;	// ammo used by the charged (held) version, 0 = can't charge
	int				chargeTime;	// refire time after the charged version
} altFire_t;

// charged alt-fire: a press shorter than ALT_TAP_TIME is the quick alt, a longer hold
// charges up to ALT_CHARGE_TIME. EV_FIRE_WEAPON_ALT carries the charge: 0 = tap, 1..255 = charged
#define ALT_TAP_TIME		250
#define ALT_CHARGE_TIME		1200
#define ALT_CHARGE_FRAC(parm)	( (parm) <= 0 ? 0.0f : ( (parm) - 1 ) / 254.0f )

extern const altFire_t	bg_altFire[];

#define ALTFIRE_PROJECTILE	1	// entityState->generic1 bit on alt-fire missiles
#define ALTFIRE_CHARGED		2	// ... and on charged ones
#define ALTFIRE_REFLECTED	4	// turned around by an air blast
#define RM_ENT_FIRE			64	// entityState->generic1 of an ET_GENERAL fire patch

// EV_HITCONFIRM flags
#define HIT_KILL		1
#define HIT_AIRSHOT		2
#define HIT_HEADSHOT	4
#define HIT_ARMORBREAK	8
#define HIT_CRIT		16		// DESCENT lucky crit (ds-frenzy, g_ds_frenzy.c): gold hitmarker

#define SHOTGUN_KICK			320	// shotgun recoil speed, fire at the floor to shotgun jump
#define CHAINGUN_SPIN_TIME		1200	// msec of firing to reach full speed
#define CHAINGUN_SLOW_TIME		110	// refire at the start of the spin
#define CHAINGUN_FAST_TIME		28	// refire fully spun up

// rearmed: extra usercmd buttons (+button12 .. +button14)
#define BUTTON_ALTATTACK	4096
#define BUTTON_HOOK			8192
#define BUTTON_DASH			16384
#define BUTTON_ZIP			32768		// +button15: zip line

#define DOUBLE_JUMP_VELOCITY	280
#define WALL_JUMP_VELOCITY		300
#define WALL_JUMP_PUSH			330		// speed away from the wall
#define WALL_JUMP_DIST			22		// how close a wall must be
#define WALL_JUMP_COOLDOWN		350
#define DASH_SPEED				720
#define DASH_LIFT				120
#define DASH_COOLDOWN			1500	// recharge time per dash charge
#define DASH_CHARGES			2
// RMF2_DASH1 (roguelike, no DASH CHARGE card yet) drops the base to one charge
#define DASH_CHARGE_COUNT(ps)	( ( ( RM_FLAGS2( ps ) & RMF2_DASH1 ) ? 1 : DASH_CHARGES ) \
									+ ( ( (ps)->stats[STAT_RMFLAGS] & RMP_THIRDWIND ) ? 1 : 0 ) )
#define DASH_READY(ps)			( (ps)->stats[STAT_DASH_TIME] <= DASH_COOLDOWN * ( DASH_CHARGE_COUNT(ps) - 1 ) )

#define COYOTE_TIME				120		// msec after walking off a ledge that a ground jump still works
#define JUMP_BUFFER_TIME		110		// a jump pressed this close to landing waits for the ground

#define WALLRUN_MIN_SPEED		260		// horizontal speed needed to start a wall run
#define WALLRUN_TIME			1600	// wall run budget per air time
#define WALLRUN_DIST			40		// how far a wall can be to catch it
#define WALLRUN_GRAVITY			0.2f	// fraction of gravity while running
#define WALLRUN_MAX_FALL		120
#define WALLRUN_STICK			40		// speed towards the wall while running

// rearmed v4 moves: slide, ground pound, blink, featherweight, ice (bg_pmove.c)
#define RMS2_CROUCH_LAST		0x0800	// crouch was down last frame (a slam needs a fresh press)
#define SLIDE_MIN_SPEED			280		// horizontal speed needed to start a slide (crouch on the ground)
#define SLIDE_TIME				900		// msec (doubled by Slipstream)
#define SLIDE_BOOST				110		// speed added at the start ...
#define SLIDE_BOOST_CAP			470		// ... but never past this speed (no infinite slide chains)
#define SLIDE_FRICTION			0.55f	// ground friction while sliding (normal is 6)
#define SLIDE_END_SPEED			170		// slower than this the slide ends
#define SLIDE_TURN_RATE			75		// degrees per second of steering
#define SLIDE_RELEASE_GRACE		200		// after crouch is let go the slide lasts this long (time to jump)
#define SLAM_MIN_HEIGHT			96		// ground pound needs this much air below
#define SLAM_HANG_TIME			130		// msec of hang before the plunge
#define SLAM_SPEED				1200	// plunge speed
#define SLAM_KEEP_SPEED			0.6f	// fraction of horizontal speed kept in the plunge
#define SLAM_FULL_HEIGHT		768		// plunge distance for full strength (255)
#define STOMP_BOUNCE			380		// a ground pound on someone's head bounces off it
#define BLINK_DIST				220		// Blink perk: dash teleport distance
#define FEATHER_JUMP_VELOCITY	260		// Featherweight perk: the second air jump
#define ICE_FRICTION			0.1f	// Ice Rink: fraction of ground friction
#define ICE_ACCEL				0.45f	// Ice Rink: fraction of ground acceleration
#define OVERCHARGE_RATE			3		// Overcharge mutator: alt-fire charge speed multiplier
#define PMF_EXTRA_JUMPED		PMF_AIR_DASHED	// used the Featherweight second air jump

#define	MAXTOUCH	32
typedef struct {
	// state (in / out)
	playerState_t	*ps;

	// command (in)
	usercmd_t	cmd;
	int			tracemask;			// collide against these types of surfaces
	int			debugLevel;			// if set, diagnostic output will be printed
	qboolean	noFootsteps;		// if the game is setup for no footsteps by the server
	qboolean	gauntletHit;		// true if a gauntlet attack would actually hit something

	int			framecount;

	// results (out)
	int			numtouch;
	int			touchents[MAXTOUCH];

	vec3_t		mins, maxs;			// bounding box size

	int			watertype;
	int			waterlevel;

	float		xyspeed;

	// for fixed msec Pmove
	int			pmove_fixed;
	int			pmove_msec;

	// callbacks to test the world
	// these will be different functions during game and cgame
	void		(*trace)( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int passEntityNum, int contentMask );
	int			(*pointcontents)( const vec3_t point, int passEntityNum );
} pmove_t;

// if a full pmove isn't done on the client, you can just update the angles
void PM_UpdateViewAngles( playerState_t *ps, const usercmd_t *cmd );
void Pmove (pmove_t *pmove);

//===================================================================================


// player_state->stats[] indexes
// NOTE: may not have more than 16
typedef enum {
	STAT_HEALTH,
	STAT_HOLDABLE_ITEM,
#ifdef MISSIONPACK
	STAT_PERSISTANT_POWERUP,
#endif
	STAT_WEAPONS,					// 16 bit fields
	STAT_ARMOR,
	STAT_DEAD_YAW,					// look this direction when dead (FIXME: get rid of?)
	STAT_CLIENTS_READY,				// bit mask of clients wishing to exit the intermission (FIXME: configstring?)
	STAT_DASH_TIME,					// msec until the dash is ready
	STAT_WALLJUMP_TIME,				// msec until the next wall jump
	STAT_SPINUP,					// chaingun: msec of continuous fire
	STAT_GGLEVEL,					// gun game ladder position
	STAT_RMFLAGS,					// RMF_ feature switches from the server + RMS_ move state
	STAT_WALLRUN_TIME,				// msec of wall running this air time
	STAT_JUMPTIMER,					// coyote time left
	STAT_ALTCHARGE,					// msec alt-fire has been held (charged alt-fire)
	STAT_STYLE,						// style meter 0..STYLE_MAX
	STAT_MAX_HEALTH					// health / armor limit, changeable by handicap
} statIndex_t;


// player_state->persistant[] indexes
// these fields are the only part of player_state that isn't
// cleared on respawn
// NOTE: may not have more than 16
typedef enum {
	PERS_SCORE,						// !!! MUST NOT CHANGE, SERVER AND GAME BOTH REFERENCE !!!
	PERS_HITS,						// total points damage inflicted so damage beeps can sound on change
	PERS_RANK,						// player rank or team rank
	PERS_TEAM,						// player team
	PERS_SPAWN_COUNT,				// incremented every respawn
	PERS_PLAYEREVENTS,				// 16 bits that can be flipped for events
	PERS_ATTACKER,					// clientnum of last damage inflicter
	PERS_ATTACKEE_ARMOR,			// health/armor of last person we attacked
	PERS_KILLED,					// count of the number of times you died
	// player awards tracking
	PERS_IMPRESSIVE_COUNT,			// two railgun hits in a row
	PERS_EXCELLENT_COUNT,			// two successive kills in a short amount of time
	PERS_DEFEND_COUNT,				// defend awards
	PERS_ASSIST_COUNT,				// assist awards
	PERS_GAUNTLET_FRAG_COUNT,		// kills with the guantlet
	PERS_CAPTURES,					// captures
	PERS_HEADSHOTS					// headshots landed, cgame plays a sound on change
} persEnum_t;


// entityState_t->eFlags
#define	EF_DEAD				0x00000001		// don't draw a foe marker over players with EF_DEAD
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
#define EF_TICKING			0x00000002		// used to make players play the prox mine ticking sound
#endif
#define	EF_TELEPORT_BIT		0x00000004		// toggled every time the origin abruptly changes
#define	EF_AWARD_EXCELLENT	0x00000008		// draw an excellent sprite
#define EF_PLAYER_EVENT		0x00000010
#define	EF_BOUNCE			0x00000010		// for missiles
#define	EF_BOUNCE_HALF		0x00000020		// for missiles
#define	EF_AWARD_GAUNTLET	0x00000040		// draw a gauntlet sprite
#define	EF_NODRAW			0x00000080		// may have an event, but no model (unspawned items)
#define	EF_FIRING			0x00000100		// for lightning gun
#define	EF_KAMIKAZE			0x00000200
#define	EF_MOVER_STOP		0x00000400		// will push otherwise
#define EF_AWARD_CAP		0x00000800		// draw the capture sprite
#define	EF_TALK				0x00001000		// draw a talk balloon
#define	EF_CONNECTION		0x00002000		// draw a connection trouble sprite
#define	EF_VOTED			0x00004000		// already cast a vote
#define	EF_AWARD_IMPRESSIVE	0x00008000		// draw an impressive sprite
#define	EF_AWARD_DEFEND		0x00010000		// draw a defend sprite
#define	EF_AWARD_ASSIST		0x00020000		// draw a assist sprite
#define EF_AWARD_DENIED		0x00040000		// denied
#define EF_TEAMVOTED		0x00080000		// already cast a team vote

// NOTE: may not have more than 16
typedef enum {
	PW_NONE,

	PW_QUAD,
	PW_BATTLESUIT,
	PW_HASTE,
	PW_INVIS,
	PW_REGEN,
	PW_FLIGHT,

	PW_REDFLAG,
	PW_BLUEFLAG,
	PW_NEUTRALFLAG,

#ifdef MISSIONPACK
	PW_SCOUT,
	PW_GUARD,
	PW_DOUBLER,
	PW_AMMOREGEN,
	PW_INVULNERABILITY,
#else
	// rearmed powerups take the Team Arena persistant powerup slots
	PW_VAMPIRE,		// damage dealt heals you
	PW_RAPIDFIRE,	// double fire rate
	PW_JETPACK,		// hold jump to fly
	// element statuses (no items): the time they wear off
	PW_BURNING,
	PW_WET,
	PW_SHOCKED,
#endif

	PW_NUM_POWERUPS

} powerup_t;

typedef enum {
	HI_NONE,

	HI_TELEPORTER,
	HI_MEDKIT,
	HI_KAMIKAZE,
	HI_PORTAL,
	HI_INVULNERABILITY,

	HI_NUM_HOLDABLE
} holdable_t;


typedef enum {
	WP_NONE,

	WP_GAUNTLET,
	WP_MACHINEGUN,
	WP_SHOTGUN,
	WP_GRENADE_LAUNCHER,
	WP_ROCKET_LAUNCHER,
	WP_LIGHTNING,
	WP_RAILGUN,
	WP_PLASMAGUN,
	WP_BFG,
	WP_GRAPPLING_HOOK,
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	WP_NAILGUN,
	WP_PROX_LAUNCHER,
	WP_CHAINGUN,
#endif

	WP_NUM_WEAPONS
} weapon_t;


// reward sounds (stored in ps->persistant[PERS_PLAYEREVENTS])
#define	PLAYEREVENT_DENIEDREWARD		0x0001
#define	PLAYEREVENT_GAUNTLETREWARD		0x0002
#define PLAYEREVENT_HOLYSHIT			0x0004

// entityState_t->event values
// entity events are for effects that take place relative
// to an existing entities origin.  Very network efficient.

// two bits at the top of the entityState->event field
// will be incremented with each change in the event so
// that an identical event started twice in a row can
// be distinguished.  And off the value with ~EV_EVENT_BITS
// to retrieve the actual event number
#define	EV_EVENT_BIT1		0x00000100
#define	EV_EVENT_BIT2		0x00000200
#define	EV_EVENT_BITS		(EV_EVENT_BIT1|EV_EVENT_BIT2)

#define	EVENT_VALID_MSEC	300

typedef enum {
	EV_NONE,

	EV_FOOTSTEP,
	EV_FOOTSTEP_METAL,
	EV_FOOTSPLASH,
	EV_FOOTWADE,
	EV_SWIM,

	EV_STEP_4,
	EV_STEP_8,
	EV_STEP_12,
	EV_STEP_16,

	EV_FALL_SHORT,
	EV_FALL_MEDIUM,
	EV_FALL_FAR,

	EV_JUMP_PAD,			// boing sound at origin, jump sound on player

	EV_JUMP,
	EV_WATER_TOUCH,	// foot touches
	EV_WATER_LEAVE,	// foot leaves
	EV_WATER_UNDER,	// head touches
	EV_WATER_CLEAR,	// head leaves

	EV_ITEM_PICKUP,			// normal item pickups are predictable
	EV_GLOBAL_ITEM_PICKUP,	// powerup / team sounds are broadcast to everyone

	EV_NOAMMO,
	EV_CHANGE_WEAPON,
	EV_FIRE_WEAPON,

	EV_USE_ITEM0,
	EV_USE_ITEM1,
	EV_USE_ITEM2,
	EV_USE_ITEM3,
	EV_USE_ITEM4,
	EV_USE_ITEM5,
	EV_USE_ITEM6,
	EV_USE_ITEM7,
	EV_USE_ITEM8,
	EV_USE_ITEM9,
	EV_USE_ITEM10,
	EV_USE_ITEM11,
	EV_USE_ITEM12,
	EV_USE_ITEM13,
	EV_USE_ITEM14,
	EV_USE_ITEM15,

	EV_ITEM_RESPAWN,
	EV_ITEM_POP,
	EV_PLAYER_TELEPORT_IN,
	EV_PLAYER_TELEPORT_OUT,

	EV_GRENADE_BOUNCE,		// eventParm will be the soundindex

	EV_GENERAL_SOUND,
	EV_GLOBAL_SOUND,		// no attenuation
	EV_GLOBAL_TEAM_SOUND,

	EV_BULLET_HIT_FLESH,
	EV_BULLET_HIT_WALL,

	EV_MISSILE_HIT,
	EV_MISSILE_MISS,
	EV_MISSILE_MISS_METAL,
	EV_RAILTRAIL,
	EV_SHOTGUN,
	EV_BULLET,				// otherEntity is the shooter

	EV_PAIN,
	EV_DEATH1,
	EV_DEATH2,
	EV_DEATH3,
	EV_OBITUARY,

	EV_POWERUP_QUAD,
	EV_POWERUP_BATTLESUIT,
	EV_POWERUP_REGEN,

	EV_GIB_PLAYER,			// gib a previously living player
	EV_SCOREPLUM,			// score plum

//#ifdef MISSIONPACK
	EV_PROXIMITY_MINE_STICK,
	EV_PROXIMITY_MINE_TRIGGER,
	EV_KAMIKAZE,			// kamikaze explodes
	EV_OBELISKEXPLODE,		// obelisk explodes
	EV_OBELISKPAIN,			// obelisk is in pain
	EV_INVUL_IMPACT,		// invulnerability sphere impact
	EV_JUICED,				// invulnerability juiced effect
	EV_LIGHTNINGBOLT,		// lightning bolt bounced of invulnerability sphere
//#endif

	EV_DEBUG_LINE,
	EV_STOPLOOPINGSOUND,
	EV_TAUNT,
	EV_TAUNT_YES,
	EV_TAUNT_NO,
	EV_TAUNT_FOLLOWME,
	EV_TAUNT_GETFLAG,
	EV_TAUNT_GUARDBASE,
	EV_TAUNT_PATROL,

	EV_DOUBLE_JUMP,
	EV_WALL_JUMP,
	EV_DASH,
	EV_FIRE_HOOK,
	EV_FIRE_WEAPON_ALT,
	EV_ARC,					// alt lightning arc: origin -> origin2
	EV_HITCONFIRM,			// to the attacker only: eventParm damage, generic1 HIT_* flags
	EV_BFG_LASER,			// BFG ball zapping someone: origin -> origin2
	EV_WALLRUN,				// started running along a wall
	EV_WINDBLAST,			// shotgun air blast: origin, angles2 = direction, eventParm = strength
	EV_THUNDER,				// thunderstrike: origin2 (sky) -> origin
	EV_STEAM,				// fire meets water
	EV_CHARGE_READY,		// the alt-fire charge is full
	EV_SLIDE,				// started a slide
	EV_SLAM,				// started a ground pound
	EV_SLAM_LAND,			// ground pound hit the floor: eventParm = strength 0..255
	EV_BLINK,				// blink dash (perk): the player moved instantly
	EV_PROP_BREAK,			// prop destroyed: eventParm = propType_t
	EV_PROP_HIT,			// prop hit / bumped: eventParm = strength 0..255
	EV_WALLSPLAT,			// body slammed into a wall: eventParm = speed / 8, angles2 = wall normal
	EV_CORPSE_EXPLODE,		// chain reaction: a corpse blows up
	EV_SLOWMO,				// global: slow motion started, eventParm = strength (screen FX only)
	EV_GORE_HIT,			// blood: origin = hit point, angles2 = shot dir, eventParm = damage, generic1 = HIT_* (0x80 splash), otherEntityNum = target, otherEntityNum2 = attacker, weapon = MOD
	EV_ZIP_FIRE,			// zip line attached: eventParm = distance / 16 (anchor in ps->grapplePoint)
	EV_ZIP_MISS,			// zip line press that didn't hold: eventParm = ZIPR_ reason
	EV_ZIP_END,				// let go of / arrived at / knocked off the zip line
	EV_DS_FX,				// descent arsenal effect: eventParm = DSX_ kind (bg_descent.h)
	EV_DS_FRENZY,			// ds-frenzy effect: eventParm = DSFZ_ kind (bg_descent.h)
	EV_DS_CARDS,				// ds-cards legendary effect: eventParm = LGX_ kind (bg_descent.h)
	EV_HITFEEL				// en-feel hit reactions: eventParm = HFX_ kind (bg_hitfeel.h), otherEntityNum = victim

} entity_event_t;


typedef enum {
	GTS_RED_CAPTURE,
	GTS_BLUE_CAPTURE,
	GTS_RED_RETURN,
	GTS_BLUE_RETURN,
	GTS_RED_TAKEN,
	GTS_BLUE_TAKEN,
	GTS_REDOBELISK_ATTACKED,
	GTS_BLUEOBELISK_ATTACKED,
	GTS_REDTEAM_SCORED,
	GTS_BLUETEAM_SCORED,
	GTS_REDTEAM_TOOK_LEAD,
	GTS_BLUETEAM_TOOK_LEAD,
	GTS_TEAMS_ARE_TIED,
	GTS_KAMIKAZE
} global_team_sound_t;

// animations
typedef enum {
	BOTH_DEATH1,
	BOTH_DEAD1,
	BOTH_DEATH2,
	BOTH_DEAD2,
	BOTH_DEATH3,
	BOTH_DEAD3,

	TORSO_GESTURE,

	TORSO_ATTACK,
	TORSO_ATTACK2,

	TORSO_DROP,
	TORSO_RAISE,

	TORSO_STAND,
	TORSO_STAND2,

	LEGS_WALKCR,
	LEGS_WALK,
	LEGS_RUN,
	LEGS_BACK,
	LEGS_SWIM,

	LEGS_JUMP,
	LEGS_LAND,

	LEGS_JUMPB,
	LEGS_LANDB,

	LEGS_IDLE,
	LEGS_IDLECR,

	LEGS_TURN,

	TORSO_GETFLAG,
	TORSO_GUARDBASE,
	TORSO_PATROL,
	TORSO_FOLLOWME,
	TORSO_AFFIRMATIVE,
	TORSO_NEGATIVE,

	MAX_ANIMATIONS,

	LEGS_BACKCR,
	LEGS_BACKWALK,
	FLAG_RUN,
	FLAG_STAND,
	FLAG_STAND2RUN,

	MAX_TOTALANIMATIONS
} animNumber_t;


typedef struct animation_s {
	int		firstFrame;
	int		numFrames;
	int		loopFrames;			// 0 to numFrames
	int		frameLerp;			// msec between frames
	int		initialLerp;		// msec to get to first frame
	int		reversed;			// true if animation is reversed
	int		flipflop;			// true if animation should flipflop back to base
} animation_t;


// flip the togglebit every time an animation
// changes so a restart of the same anim can be detected
#define	ANIM_TOGGLEBIT		128


typedef enum {
	TEAM_FREE,
	TEAM_RED,
	TEAM_BLUE,
	TEAM_SPECTATOR,

	TEAM_NUM_TEAMS
} team_t;

// Time between location updates
#define TEAM_LOCATION_UPDATE_TIME		1000

// How many players on the overlay
#define TEAM_MAXOVERLAY		32

//team task
typedef enum {
	TEAMTASK_NONE,
	TEAMTASK_OFFENSE,
	TEAMTASK_DEFENSE,
	TEAMTASK_PATROL,
	TEAMTASK_FOLLOW,
	TEAMTASK_RETRIEVE,
	TEAMTASK_ESCORT,
	TEAMTASK_CAMP
} teamtask_t;

// means of death
typedef enum {
	MOD_UNKNOWN,
	MOD_SHOTGUN,
	MOD_GAUNTLET,
	MOD_MACHINEGUN,
	MOD_GRENADE,
	MOD_GRENADE_SPLASH,
	MOD_ROCKET,
	MOD_ROCKET_SPLASH,
	MOD_PLASMA,
	MOD_PLASMA_SPLASH,
	MOD_RAILGUN,
	MOD_LIGHTNING,
	MOD_BFG,
	MOD_BFG_SPLASH,
	MOD_WATER,
	MOD_SLIME,
	MOD_LAVA,
	MOD_CRUSH,
	MOD_TELEFRAG,
	MOD_FALLING,
	MOD_SUICIDE,
	MOD_TARGET_LASER,
	MOD_TRIGGER_HURT,
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	MOD_NAIL,
	MOD_CHAINGUN,
	MOD_PROXIMITY_MINE,
#endif
#ifdef MISSIONPACK
	MOD_KAMIKAZE,
	MOD_JUICED,
#endif
	MOD_GRAPPLE,
	MOD_BURN,
	MOD_THUNDER,
	MOD_SLAM,				// ground pound
	MOD_PROP,				// exploding barrel / tank, flying prop
	MOD_WALLSPLAT,			// knocked into a wall
	MOD_METEOR,
	MOD_THORNS,
	MOD_CORPSE				// chain reaction corpse explosion
} meansOfDeath_t;


//---------------------------------------------------------

// gitem_t->type
typedef enum {
	IT_BAD,
	IT_WEAPON,				// EFX: rotate + upscale + minlight
	IT_AMMO,				// EFX: rotate
	IT_ARMOR,				// EFX: rotate + minlight
	IT_HEALTH,				// EFX: static external sphere + rotating internal
	IT_POWERUP,				// instant on, timer based
							// EFX: rotate + external ring that rotates
	IT_HOLDABLE,			// single use, holdable item
							// EFX: rotate + bob
	IT_PERSISTANT_POWERUP,
	IT_TEAM
} itemType_t;

#define MAX_ITEM_MODELS 4

typedef struct gitem_s {
	char		*classname;	// spawning name
	char		*pickup_sound;
	char		*world_model[MAX_ITEM_MODELS];

	char		*icon;
	char		*pickup_name;	// for printing on pickup

	int			quantity;		// for ammo how much, or duration of powerup
	itemType_t  giType;			// IT_* flags

	int			giTag;

	char		*precaches;		// string of all models and images this item will use
	char		*sounds;		// string of all sounds this item will use
} gitem_t;

// included in both the game dll and the client
extern	gitem_t	bg_itemlist[];
extern	int		bg_numItems;

gitem_t	*BG_FindItem( const char *pickupName );
gitem_t	*BG_FindItemForWeapon( weapon_t weapon );
gitem_t	*BG_FindItemForPowerup( powerup_t pw );
gitem_t	*BG_FindItemForHoldable( holdable_t pw );
#define	ITEM_INDEX(x) ((x)-bg_itemlist)

qboolean	BG_CanItemBeGrabbed( int gametype, const entityState_t *ent, const playerState_t *ps );


// g_dmflags->integer flags
#define	DF_NO_FALLING			8
#define DF_FIXED_FOV			16
#define	DF_NO_FOOTSTEPS			32

// content masks
#define	MASK_ALL				(-1)
#define	MASK_SOLID				(CONTENTS_SOLID)
#define	MASK_PLAYERSOLID		(CONTENTS_SOLID|CONTENTS_PLAYERCLIP|CONTENTS_BODY)
#define	MASK_DEADSOLID			(CONTENTS_SOLID|CONTENTS_PLAYERCLIP)
#define	MASK_WATER				(CONTENTS_WATER|CONTENTS_LAVA|CONTENTS_SLIME)
#define	MASK_OPAQUE				(CONTENTS_SOLID|CONTENTS_SLIME|CONTENTS_LAVA)
#define	MASK_SHOT				(CONTENTS_SOLID|CONTENTS_BODY|CONTENTS_CORPSE)


//
// entityState_t->eType
//
typedef enum {
	ET_GENERAL,
	ET_PLAYER,
	ET_ITEM,
	ET_MISSILE,
	ET_MOVER,
	ET_BEAM,
	ET_PORTAL,
	ET_SPEAKER,
	ET_PUSH_TRIGGER,
	ET_TELEPORT_TRIGGER,
	ET_INVISIBLE,
	ET_GRAPPLE,				// grapple hooked on wall
	ET_TEAM,

	ET_EVENTS				// any of the EV_* events can be added freestanding
							// by setting eType to ET_EVENTS + eventNum
							// this avoids having to set eFlags and eventNum
} entityType_t;



void	BG_EvaluateTrajectory( const trajectory_t *tr, int atTime, vec3_t result );
void	BG_EvaluateTrajectoryDelta( const trajectory_t *tr, int atTime, vec3_t result );

void	BG_AddPredictableEventToPlayerstate( int newEvent, int eventParm, playerState_t *ps );

void	BG_TouchJumpPad( playerState_t *ps, entityState_t *jumppad );

void	BG_PlayerStateToEntityState( playerState_t *ps, entityState_t *s, qboolean snap );
void	BG_PlayerStateToEntityStateExtraPolate( playerState_t *ps, entityState_t *s, int time, qboolean snap );

qboolean	BG_PlayerTouchesItem( playerState_t *ps, entityState_t *item, int atTime );


#define ARENAS_PER_TIER		4
#define MAX_ARENAS			1024
#define	MAX_ARENAS_TEXT		8192

#define MAX_BOTS			1024
#define MAX_BOTS_TEXT		8192


// Kamikaze

// 1st shockwave times
#define KAMI_SHOCKWAVE_STARTTIME		0
#define KAMI_SHOCKWAVEFADE_STARTTIME	1500
#define KAMI_SHOCKWAVE_ENDTIME			2000
// explosion/implosion times
#define KAMI_EXPLODE_STARTTIME			250
#define KAMI_IMPLODE_STARTTIME			2000
#define KAMI_IMPLODE_ENDTIME			2250
// 2nd shockwave times
#define KAMI_SHOCKWAVE2_STARTTIME		2000
#define KAMI_SHOCKWAVE2FADE_STARTTIME	2500
#define KAMI_SHOCKWAVE2_ENDTIME			3000
// radius of the models without scaling
#define KAMI_SHOCKWAVEMODEL_RADIUS		88
#define KAMI_BOOMSPHEREMODEL_RADIUS		72
// maximum radius of the models during the effect
#define KAMI_SHOCKWAVE_MAXRADIUS		1320
#define KAMI_BOOMSPHERE_MAXRADIUS		720
#define KAMI_SHOCKWAVE2_MAXRADIUS		704

