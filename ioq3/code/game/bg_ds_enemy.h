/*
===========================================================================
REARMED -- round 4 ENEMIES contract (2026-10-13). Shared by game, cgame and ui.
Included at the end of bg_descent.h. Design: docs/DESCENT_V2.md "Round 4".
Game storage: game/g_enemy.c (g_enemy[], G_EN_*). cgame mirror: cgame/cg_enemy.c (CG_EN_*).
Owners: archetypes en-types, affixes en-affix, rank / grudge / nemesis en-personality,
morale / crawl / stagger en-feel, tactics + habits en-brains, timed boss en-boss.
===========================================================================
*/
#ifndef BG_DS_ENEMY_H
#define BG_DS_ENEMY_H

/*
ARCHETYPES (DESCENT only, en-types): what a bot IS. Changes how it fights, not how much:
every archetype is power-budgeted against G_DS_PowerScore like a normal bot.
*/
typedef enum {
	DSAR_NONE,			// a plain bot (and every bot outside DESCENT)
	DSAR_SNIPER,			// railgun from high ground, laser glint before the shot
	DSAR_RUSHER,			// shotgun, charges in, fast
	DSAR_MEDIC,			// heals nearby enemies with a beam, kill it first
	DSAR_SHIELD,			// frontal shield blocks most damage, hit the back / splash
	DSAR_KAMIKAZE,		// runs at you beeping and blows up
	DSAR_BLINKER,		// short teleports, never ending within 400 units of the player
	DSAR_SUMMONER,		// raises skeletons from corpses
	DSAR_MIMIC,			// carries a copy of the player's best weapon (and its level)
	DSAR_JUGGERNAUT,		// slow, big, heavy weapon, staggers only from big hits
	DSAR_NUM
} dsArchetype_t;

/*
AFFIXES (DESCENT only, en-affix): bit mask on elites (and ambush mini-bosses, champion packs).
1 affix in act 1, 2 in acts 2-3, 3 in act 4 / Abyss. Enemy-side numbers may pass 10%.
*/
#define DSAF_VAMPIRIC		0x0001	// heals from damage dealt
#define DSAF_EXPLODE		0x0002	// blows up a moment after death (warned)
#define DSAF_SPLIT			0x0004	// splits into two weaker copies on its first death
#define DSAF_REFLECT		0x0008	// reflects part of the damage taken
#define DSAF_FROST			0x0010	// hits slow you briefly
#define DSAF_FIRETRAIL		0x0020	// leaves short-lived fire patches (RM_ENT_FIRE)
#define DSAF_PULL			0x0040	// periodically yanks you toward it (telegraphed)
#define DSAF_REGEN			0x0080	// regenerates out of combat
#define DSAF_NUM			8
#define DSAF_CHAMPION		0x8000	// one of a champion pack of 3 sharing the same affixes

/*
PER-CLIENT FLAGS (g_enemy[].flags, published): states other workers read.
*/
#define DSEF_STAGGER		0x0001	// en-feel: staggered right now (can't fire)
#define DSEF_CRAWL			0x0002	// en-feel: crawling (nearly dead, legs gone)
#define DSEF_PANIC			0x0004	// en-feel: morale broken, wants to flee (en-brains moves it)
#define DSEF_NOHEAD			0x0008	// en-feel: head / helmet gone (cgame hides the head)
#define DSEF_NOARM			0x0010	// en-feel: an arm gone
#define DSEF_NEMESIS		0x0020	// en-personality: the run's nemesis
#define DSEF_GRUDGE			0x0040	// en-personality: hunting its last killer
#define DSEF_TELEGRAPH		0x0080	// en-types / en-affix: a big attack is winding up
#define DSEF_SHIELDUP		0x0100	// en-types: shield raised
#define DSEF_THIEF			0x0200	// en-brains: carries the human's stolen weapon (all modes, g_botWeaponThief)

#define DS_MAX_RANK			5		// en-personality: bot veteran ranks through a run (0 = recruit)

/*
CS_DSENEMY (743): one info string, key = client number, value
"arch,affix,rank,flags,armorPct" for every bot that has anything non-zero (absent = all zero).
Written by G_EN_Publish (game/g_enemy.c) at most once per server frame when something changed.
*/
#define CS_DSENEMY			743

#include "bg_ds_affix.h"		// en-affix: RM_ENT_AFFIXFX, CS_DSAFFIX
#include "bg_hitfeel.h"		// en-feel: EV_HITFEEL kinds (HFX_), body bits (HFB_), death screams (HFS_)

#endif // BG_DS_ENEMY_H
