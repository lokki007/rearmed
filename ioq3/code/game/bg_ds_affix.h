/*
===========================================================================
REARMED -- round 4 AFFIXES (en-affix): ids and tables shared by game, cgame and ui.
Included at the end of bg_ds_enemy.h. Game: game/g_ds_affix.c, cgame: cgame/cg_ds_affix.c.
Affix bits (DSAF_*) live in bg_ds_enemy.h; the mask of a bot is g_enemy[c].affix / CG_EN(c)->affix.
===========================================================================
*/
#ifndef BG_DS_AFFIX_H
#define BG_DS_AFFIX_H

/*
RM_ENT_AFFIXFX (84) ET_GENERAL, SVF_BROADCAST: modelindex2 = DSAFX_ kind, time = start,
time2 = the moment it lands (blast / yank / copies / landing), otherEntityNum = the affixed
client, otherEntityNum2 = the victim (pull, vampire drain), frame = affix mask (pillar colours).
*/
#define RM_ENT_AFFIXFX		84

#define DSAFX_BOMB			0		// EXPLODES ON DEATH: the swelling glow at origin, the blast at time2
#define DSAFX_REFLECT		1		// a chrome flash on otherEntityNum (it bounced damage back)
#define DSAFX_PULL			2		// a tether otherEntityNum -> otherEntityNum2, the yank at time2 (frame bit 0x100: it snapped)
#define DSAFX_SPLIT			3		// violet tear at origin, the two copies at time2
#define DSAFX_PILLAR		4		// a champion pack member lands here at time2 (gold beam, affix colours from frame)
#define DSAFX_DRAIN			5		// VAMPIRIC: a red streak from otherEntityNum2 (bitten) to otherEntityNum
#define DSAFX_FROSTHIT		6		// FROST: crystals burst on otherEntityNum2 (slowed)

/*
CS_DSAFFIX (744): info string, "" outside DESCENT:
	\fz\<level time the local slow ends>\fc\<the slowed client>\fp\<slow percent>
	\rg\<bit mask of clients regenerating now (REGEN, out of combat)>
	\cs\<champion pack state: 0 none, 1 warned (pillars), 2 in, 3 over>\cm\<pack affix mask>
	\cw\<warn level time>\cl\<landing level time>\cc\<alive member count>\ck\<members killed by the player>
	\cx\<member clients, dot separated>
*/
#define CS_DSAFFIX			744

// how many affixes by act: 1 in act 1, 2 in acts 2-3, 3 in act 4 and the Abyss
#define DSAF_COUNT_FOR_ACT(a)	( (a) <= 1 ? 1 : (a) <= 3 ? 2 : 3 )

// names: BG_DS_AffixName (the bestiary table, game/bg_ds_bestiary.c, in all three modules);
// tell colours: cgame/cg_ds_affix.c (CG_DSA_Color).
// (no tables here: q3asm turns file statics global, a static table in a header collides)
const char *BG_DS_AffixName( int bit );		// bit index 0..DSAF_NUM-1 -> "VAMPIRIC" .. "REGENERATING" ("" out of range)

#endif // BG_DS_AFFIX_H
