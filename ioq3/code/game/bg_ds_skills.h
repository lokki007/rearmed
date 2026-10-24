/*
===========================================================================
REARMED -- DESCENT skills (owner SKILLS, 0.5.0): Skyrim-style skill trees.

Ten skills train by doing, in every mode against bots; their perks only work in
DESCENT. Skill levels 1..100 (fast early, slow late), every few skill levels raise
the character level, each character level past 1 is a perk point. Permanent and
shared by every DESCENT character.

Files:
	rearmed_meta.txt	sk_xp		permanent xp per skill (SK_NUM csv, merged by the UI)
						sk_perks	perk ranks (SKP_NUM csv)
						sk_mseq		the rearmed_skills.txt seq merged last
						sk_respecs	respecs bought (the price grows)
	rearmed_skills.txt	(game writes at floor end / match end / map change)
						seq			+1 for every new batch of pending xp
						xp			pending xp not merged yet (SK_NUM csv, multiplier applied)
						run_seed	the DESCENT run the run_ keys belong to
						run_xp		xp trained in that run (SK_NUM csv)
						run_lv0		skill levels when that run began (SK_NUM csv)
						run_cl0		character level when that run began
						last_xp		xp of the last floor / match (SK_NUM csv)
The game reads sk_xp + the pending xp (when seq > sk_mseq) at every map start, so its
levels are right whether or not the UI merged yet.

Network: CS_DSSKILLS (configstring, the HUD): \lv\l.l.l..\pm\p.p.p..  levels and
permille progress to the next level; empty when nothing trains. Server command
"dsskl <skill> <level>": a skill level-up (small flash beside the weapon pips).
Design: docs/DESCENT_V2.md round 2 item 8.
===========================================================================
*/
#ifndef BG_DS_SKILLS_H
#define BG_DS_SKILLS_H

#define SKILLS_FILE			"rearmed_skills.txt"
#define CS_DSSKILLS			736		// CS_MAX: the first configstring past CS_PARTICLES' range

typedef enum {
	SK_MELEE,			// gauntlet
	SK_BALLISTICS,		// machinegun, chaingun, nailgun
	SK_SCATTER,			// shotgun
	SK_EXPLOSIVES,		// rockets, grenades, prox mines, BFG
	SK_ENERGY,			// lightning gun, plasma gun
	SK_PRECISION,		// railgun + headshots with any weapon
	SK_TRAVERSAL,		// wall run / jump, air jump, slide, zip line, grapple
	SK_EVASION,			// dashes, blinks, near misses
	SK_SURVIVAL,		// damage taken and survived, healing
	SK_CARNAGE,			// gibs, style points
	SK_NUM
} dsSkill_t;

#define SK_MAX_LEVEL		100

typedef struct {
	const char	*name;			// "EXPLOSIVES"
	const char	*trains;		// "Rockets, grenades, prox mines, the BFG"
	float		color[3];
} dsSkillInfo_t;

extern const dsSkillInfo_t ds_skills[SK_NUM];

// perks: every perk belongs to one skill, ranks need a skill level each
typedef enum {
	// MELEE
	SKP_BRAWLER, SKP_LONGARM, SKP_BLOODLETTER, SKP_SWIFTFISTS, SKP_JUGGERNAUT, SKP_RECKONING, SKP_MEATGRINDER,
	// BALLISTICS
	SKP_STEADYHANDS, SKP_SCAVENGER, SKP_RAPIDCYCLE, SKP_SPINDOCTOR, SKP_SHREDDER, SKP_PIERCER, SKP_DISCIPLINE,
	// SCATTER
	SKP_BUCKSHOT, SKP_POINTBLANK, SKP_QUICKPUMP, SKP_FLAYER, SKP_BRACE, SKP_SECONDBARREL, SKP_LASTRITES,
	// EXPLOSIVES
	SKP_DEMOLITION, SKP_SAFETYFUSE, SKP_PAYLOAD, SKP_BLASTRADIUS, SKP_AIRRAID, SKP_IMPACT, SKP_CHAINFUSE,
	// ENERGY
	SKP_CAPACITOR, SKP_OVERCLOCK, SKP_GROUNDED, SKP_SUPERHEATED, SKP_STATIC, SKP_RECYCLER, SKP_SIPHON,
	// PRECISION
	SKP_DEADEYE, SKP_STEADY, SKP_SKULLCRACKER, SKP_LONGSHOT, SKP_EXECUTIONER, SKP_SNIPERNEST, SKP_MARKED,
	// TRAVERSAL
	SKP_MOMENTUM, SKP_SPRINGHEEL, SKP_SKYWALKER, SKP_SECONDWIND, SKP_AIRBORNE, SKP_SOFTLANDING, SKP_SLINGSHOT,
	// EVASION
	SKP_GHOST, SKP_SIDESTEP, SKP_SLIPPERY, SKP_AFTERIMAGE, SKP_COUNTERSTEP, SKP_SHADOW, SKP_REFLEX,
	// SURVIVAL
	SKP_TOUGHNESS, SKP_IRONSKIN, SKP_MENDING, SKP_ARMORER, SKP_GRIT, SKP_UNBREAKABLE, SKP_CHEATDEATH,
	// CARNAGE
	SKP_REDRAIN, SKP_SHOWOFF, SKP_GOREHOUND, SKP_OVERKILL, SKP_FRENZY, SKP_CARNIVAL, SKP_MASSACRE,
	SKP_NUM
} dsPerk_t;

#define SKP_MAX_RANK		3
#define SKP_PER_SKILL		7		// every tree has 7 perks, in dsPerk_t order

typedef struct {
	int			skill;
	const char	*name;
	const char	*fmt;			// the effect; a %i gets value * rank ("+%i%% splash damage")
	int			value;			// per rank (percent or amount)
	int			maxRank;
	int			req[SKP_MAX_RANK];	// skill level each rank needs
	int			parent[2];		// perks of the same tree (dsPerk_t), one of them taken opens this one; -1 none
	float		x, y;			// star position in the constellation (0..1, y up)
} dsPerkInfo_t;

extern const dsPerkInfo_t ds_perks[SKP_NUM];

// xp curve and character level (bg_ds_skills.c)
int BG_DS_SkillXPToNext( int level );					// xp from level to level + 1 (0 at SK_MAX_LEVEL)
int BG_DS_SkillLevel( int xp, int *into, int *need );	// level 1..100 for total xp, xp into it / to the next
int BG_DS_SkillTotalXP( int level );					// total xp that reaches a level
int BG_DS_CharLevel( const int *levels );				// from SK_NUM skill levels (1 + ...)
int BG_DS_CharLevelNeed( int charLevel );				// skill levels gained (sum of level - 1) that reach it
int BG_DS_SkillForWeapon( int weapon );					// the weapon's skill, -1 none
int BG_DS_PerkPoints( const int *levels, const int *ranks );	// unspent perk points
qboolean BG_DS_PerkOpen( const int *ranks, int perk );	// a parent taken (roots always)
const char *BG_DS_PerkText( int perk, int rank );		// the effect at a rank (rank 0 = rank 1's)

#endif // BG_DS_SKILLS_H
