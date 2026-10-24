/*
===========================================================================
REARMED -- round 4 HIT FEEL (en-feel), shared by game and cgame.
Included at the end of bg_ds_enemy.h. Game: game/g_hitfeel.c. cgame: cgame/cg_hitfeel.c.
Flags the other workers read live in bg_ds_enemy.h (DSEF_STAGGER / CRAWL / PANIC / NOHEAD /
NOARM) and g_enemy[] (morale, staggerUntil, armorPct, spare[2] scarer + 1, spare[3] limbs).
===========================================================================
*/
#ifndef BG_HITFEEL_H
#define BG_HITFEEL_H

// EV_HITFEEL: eventParm = HFX_ kind, otherEntityNum = the victim, angles2 = hit direction,
// generic1 = kind specific (below), origin = where
#define HFX_STAGGER		1	// a big hit staggered it: generic1 = strength 0..255
#define HFX_LIMB		2	// a limb came off: generic1 = HFB_ bit of the limb
#define HFX_HELMET		3	// a headshot knocked the helmet off (armoured, survived)
#define HFX_ARMORBREAK	4	// the last of its armour shattered
#define HFX_PANIC		5	// morale broke: a scream, the weapon drops for a moment
#define HFX_SCREAM		6	// death scream: generic1 = HFS_ category
#define HFX_CRAWL		7	// dropped to the floor to crawl
#define HFX_ARMORHIT	8	// a hit cracked its armour (sparks + shards): generic1 = armour % left

// body bits in entityState.time2 of players (copied to corpses by CopyToBodyQue)
#define HFB_LARM		1
#define HFB_RARM		2
#define HFB_LEGS		4
#define HFB_HELMET		8	// helmet knocked off (cosmetic)
#define HFB_ARMS		( HFB_LARM | HFB_RARM )
#define HFB_ALL			15

// death scream categories
#define HFS_GENERIC		0
#define HFS_HEADSHOT	1	// gurgle
#define HFS_GIB			2	// torn-off shriek
#define HFS_BURN		3
#define HFS_FALL		4
#define HFS_EXPLODE		5
#define HFS_NUM			6
#define HFS_VOICES		4

#endif // BG_HITFEEL_H
