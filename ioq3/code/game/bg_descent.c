/*
===========================================================================
REARMED -- DESCENT v2 data tables (built into game, cgame and ui).
The contract is in bg_descent.h; design notes in docs/DESCENT_V2.md.
Numbers here are starting values: the owners tune them (see the doc).
===========================================================================
*/
#include "../qcommon/q_shared.h"
#include "bg_public.h"

/*
-------------------------------------------------------------------------------
ACTS (index 1..5)
-------------------------------------------------------------------------------
*/
// roman numerals for acts and veteran ranks (en-review: one copy for cgame, ui and game)
const char *ds_roman[6] = { "", "I", "II", "III", "IV", "V" };

// map pools: ds-maps (round 3) re-balanced them by theme and size, >= 12 installed maps each
// (Team Arena ones only once copied from the player's install); the Abyss uses every map
const dsAct_t ds_acts[DS_ACT_ABYSS + 1] = {
	{ "", "", "", -1, { 1, 1, 1, 0 }, { 0, 0, 0, 0 }, DSFX_NONE, "" },
	{ "THE CRYPTS", "Where the dead learned to shoot back",
		"q3dm0 q3dm1 q3dm2 q3dm3 q3dm4 q3dm5 q3dm6 q3dm7 q3dm9 q3tourney2 pro-q3tourney2 rm_camp mptourney1 mptourney3", BOSS_BUTCHER,
		{ 1.05f, 0.95f, 0.85f, 0.25f }, { 0.35f, 0.28f, 0.20f, 0.15f }, DSFX_DUST, "music/sonic1.wav" },
	{ "THE FOUNDRY", "Iron, smoke and bad intentions",
		"q3dm6 q3dm10 q3dm11 q3dm12 pro-q3dm6 q3tourney1 q3tourney4 pro-q3tourney4 q3tourney5 q3ctf1 rm_camp mptourney2 mpteam1 mpteam2 mpteam4", BOSS_OVERSEER,
		{ 1.10f, 0.92f, 0.78f, 0.30f }, { 0.45f, 0.30f, 0.15f, 0.20f }, DSFX_EMBERS, "music/sonic3.wav" },
	{ "THE VOID", "Nothing below. Nothing above. Nothing kind.",
		"q3dm16 q3dm17 q3dm18 q3dm19 q3dm10 q3dm11 q3tourney6 q3tourney6_ctf q3ctf4 rm_yard pro-q3dm13 mptourney4 mpteam6 mpteam7", BOSS_HOLLOWKING,
		{ 0.85f, 0.92f, 1.15f, 0.30f }, { 0.15f, 0.18f, 0.35f, 0.15f }, DSFX_MOTES, "music/sonic5.wav" },
	{ "THE PIT", "The bottom has teeth",
		"q3dm7 q3dm8 q3dm12 q3dm13 q3dm14 q3dm15 q3tourney3 q3ctf1 q3ctf2 q3ctf3 pro-q3dm13 rm_lost mpteam4 mpteam7", BOSS_ARCHDEMON,
		{ 1.15f, 0.80f, 0.75f, 0.35f }, { 0.40f, 0.10f, 0.08f, 0.25f }, DSFX_ASH, "music/sonic6.wav" },
	{ "THE ABYSS", "Endless. It only gets worse.",
		"", -1,
		{ 0.95f, 0.85f, 1.05f, 0.40f }, { 0.20f, 0.05f, 0.25f, 0.30f }, DSFX_ASH, "music/sonic4.wav" }
};

/*
-------------------------------------------------------------------------------
WEAPON LEVELS
-------------------------------------------------------------------------------
*/
// total gem xp to reach level n (blue gem 1, green 5, red 25)
const int ds_wlevelXP[DS_MAX_WLEVEL + 1] = { 0, 0, 12, 30, 55, 90, 135, 190, 260 };

const char *ds_weaponNames[DS_NUM_WEAPONS] = {
	"", "GAUNTLET", "MACHINEGUN", "SHOTGUN", "GRENADE LAUNCHER", "ROCKET LAUNCHER", "LIGHTNING GUN",
	"RAILGUN", "PLASMA GUN", "BFG10K", "GRAPPLE", "NAILGUN", "PROX LAUNCHER", "CHAINGUN"
};

// what each level adds (level 1 = the stock weapon). The arsenal owner implements these.
const char *ds_weaponLevelDesc[DS_NUM_WEAPONS][DS_MAX_WLEVEL + 1] = {
	{ "" },
	{ "", "", "+8% damage", "swing 6% faster", "hits knock enemies back hard", "+8% damage",
		"every hit heals 3", "swing 6% faster", "kills gib and splash gore that hurts nearby enemies" },
	{ "", "", "+6% damage", "fire 5% faster", "bullets pierce one enemy", "+6% damage",
		"1 in 12 shots costs no ammo", "fire 5% faster", "every 5th bullet is explosive" },
	{ "", "", "+1 pellet", "+6% damage", "tighter spread", "+1 pellet",
		"fire 5% faster", "+6% damage", "pellets set enemies on fire" },
	{ "", "", "+6% damage", "+6% blast radius", "fire 5% faster", "grenades bounce twice as lively",
		"+6% damage", "+6% blast radius", "every grenade splits into 3 bomblets" },
	{ "", "", "+6% damage", "+8% rocket speed", "+6% blast radius", "fire 5% faster",
		"+6% damage", "+8% rocket speed", "every 4th shot adds a half-strength rocket" },
	{ "", "", "+6% damage", "hits slow the target", "+6% damage", "fire 5% faster",
		"1 in 12 shots costs no ammo", "+6% damage", "the beam arcs to a second enemy" },
	{ "", "", "+6% damage", "recharge 5% faster", "slugs ricochet off a wall", "+6% damage",
		"recharge 5% faster", "+6% damage", "kills explode" },
	{ "", "", "+6% damage", "+8% bolt speed", "fire 5% faster", "+8% splash",
		"+6% damage", "fire 5% faster", "bolts grow as they fly" },
	{ "", "", "+6% damage", "+8% blast radius", "recharge 6% faster", "+6% damage",
		"the ball zaps further and 2 more enemies", "+8% blast radius", "the blast leaves a burning crater" },
	{ "" },
	{ "", "", "+6% damage", "+1 nail", "nails ricochet once more", "+6% damage",
		"fire 5% faster", "+1 nail", "nails pin enemies (slow them)" },
	{ "", "", "+6% damage", "+8% blast radius", "mines arm twice as fast", "fire 5% faster",
		"+6% damage", "+8% blast radius", "mines leave fire" },
	{ "", "", "+6% damage", "spins up 9% faster", "+6% damage", "bullets pierce one enemy",
		"1 in 12 shots costs no ammo", "fire 5% faster", "no spin-down: it stays at full speed" }
};

/*
-------------------------------------------------------------------------------
PASSIVES
-------------------------------------------------------------------------------
*/
const char *ds_passiveNames[RLP_NUM] = {
	"SPITE", "IRON HIDE", "HOLLOW HEART", "BLACK BLOOD", "QUICKSILVER", "HAIR TRIGGER", "BLAST CORE",
	"RIFLING", "LODESTONE", "RABBIT'S FOOT", "HUNGER", "GRAVE COIN", "AMMO BELT"
};
const char *ds_passiveDescs[RLP_NUM] = {
	"+5% damage", "-4% damage taken", "+6 max health", "regenerate 0.3 health a second", "+3% move speed",
	"fire 4% faster", "+6% blast radius", "+6% projectile speed", "+25% gem pull radius",
	"+8% luck", "+8% gem xp", "+8% skulls", "+8% max ammo and pickups"
};
const char *ds_passiveShort[RLP_NUM] = {
	"SPI", "HID", "HRT", "BLD", "QSV", "TRG", "BLC", "RIF", "MAG", "LCK", "HUN", "GRD", "AMB"
};
const float ds_passivePerLevel[RLP_NUM] = {
	0.05f, 0.04f, 6.0f, 0.3f, 0.03f, 0.04f, 0.06f, 0.06f, 0.25f, 0.08f, 0.08f, 0.08f, 0.08f
};	// single-digit steps (user rule: no double-digit boost from one level)

/*
-------------------------------------------------------------------------------
EVOLUTIONS
-------------------------------------------------------------------------------
*/
const dsEvoInfo_t ds_evolutions[EVO_NUM] = {
	{ 1,  RLC_PASSIVE_FIRST + RLP_REGEN,	"HEARTRIPPER",	"a lunging claw that rips hearts out: every hit heals you, kills burst" },
	{ 2,  RLC_PASSIVE_FIRST + RLP_TEMPO,	"BULLET HELL",	"twin barrels, piercing rounds, no spread" },
	{ 3,  RLC_PASSIVE_FIRST + RLP_AREA,		"DRAGON'S BREATH", "incendiary pellets that leave the floor burning" },
	{ 4,  RLC_PASSIVE_FIRST + RLP_LUCK,		"CLUSTER BOMB",	"every grenade bursts into a carpet of bomblets" },
	{ 5,  RLC_PASSIVE_FIRST + RLP_MIGHT,	"HELLFIRE",		"a salvo of three seeking rockets" },
	{ 6,  RLC_PERK_FIRST + PERK_CONDUCTOR,	"STORM CALLER",	"the beam forks to every enemy near the target" },
	{ 7,  RLC_PERK_FIRST + PERK_HEADHUNTER,	"JUDGEMENT",	"slugs pierce everything and detonate along their path" },
	{ 8,  RLC_PASSIVE_FIRST + RLP_VELOCITY,	"STAR FURNACE",	"star orbs that burst into a spray of smaller orbs" },
	{ 9,  RLC_PASSIVE_FIRST + RLP_MAGNET,	"DOOMSDAY",		"a black hole that drags enemies in before it blows" },
	{ 11, RLC_PASSIVE_FIRST + RLP_IRONHIDE,	"IRON MAIDEN",	"nails ricochet between enemies" },
	{ 12, RLC_PERK_FIRST + PERK_PYROMANIAC,	"MINEFIELD",	"mines chain-detonate and set the ground on fire" },
	{ 13, RLC_PASSIVE_FIRST + RLP_AMMOBELT,	"RIPSAW",		"no spin-up, never runs dry, shreds bodies" },
	{ 5,  RLC_SECRET_FIRST + RLS_UNION,		"ARMAGEDDON",	"HELLFIRE and CLUSTER BOMB as one: rockets that rain bomblets" }
};

int BG_DS_EvolutionForWeapon( int weapon ) {
	int		i;

	for ( i = 0 ; i < EVO_ARMAGEDDON ; i++ ) {
		if ( ds_evolutions[i].weapon == weapon ) {
			return i;
		}
	}
	return -1;
}

/*
-------------------------------------------------------------------------------
CHARACTERS
-------------------------------------------------------------------------------
*/
const dsCharInfo_t ds_characters[CHR_NUM] = {
	{ "RANGER", "ranger", "machinegun lv2. No weakness.", "", 2, 2, { -1 }, 0, 0, 0 },
	{ "SARGE", "sarge", "shotgun lv2, IRON HIDE 1.", "Clear Act 1",
		3, 2, { RLC_PASSIVE_FIRST + RLP_IRONHIDE, -1 }, 1, 0, 0 },
	{ "VISOR", "visor", "railgun, HEADHUNTER.", "Land 300 headshots",
		7, 1, { RLC_PERK_FIRST + PERK_HEADHUNTER, -1 }, 0, 0, 0 },
	{ "ANARKI", "anarki", "double jump and dash, QUICKSILVER 2, -20 max health.", "Reach floor 10",
		0, 0, { RLC_PASSIVE_FIRST + RLP_HASTE, RLC_DOUBLEJUMP, RLC_DASH, -1 }, 2, -20, 0 },
	{ "KLESK", "klesk", "gauntlet lv3, BLACK BLOOD 2.", "200 gauntlet kills",
		1, 3, { RLC_PASSIVE_FIRST + RLP_REGEN, -1 }, 2, 0, 0 },
	{ "XAERO", "xaero", "plasma gun, BLAST CORE 1.", "Evolve a weapon",
		8, 1, { RLC_PASSIVE_FIRST + RLP_AREA, -1 }, 1, 0, 0 },
	{ "DOOM", "doom", "rocket launcher, SPITE 1, a little slower.", "1000 kills",
		5, 1, { RLC_PASSIVE_FIRST + RLP_MIGHT, -1 }, 1, 0, -4 },
	{ "HUNTER", "hunter", "grapple and zip line, LODESTONE 2.", "Zip 100 times",
		0, 0, { RLC_PASSIVE_FIRST + RLP_MAGNET, RLC_GRAPPLE, RLC_ZIPLINE, -1 }, 2, 0, 0 },
	{ "ORBB", "orbb", "every alt-fire, 50 max health.", "???",
		0, 0, { -1 }, 0, -50, 0 }
};

/*
-------------------------------------------------------------------------------
BOSSES
-------------------------------------------------------------------------------
*/
const dsBossInfo_t ds_bosses[BOSS_NUM] = {
	{ "THE BUTCHER",		"Lord of the Crypts",		"sorlag",	1.75f, 900,  3 },
	{ "THE OVERSEER",		"Iron Warden",				"tankjr",	1.35f, 1100, 3 },
	{ "THE HOLLOW KING",	"Crowned in Nothing",		"uriel",	1.40f, 1300, 3 },
	{ "THE ARCH-DEMON",		"The Bottom of the Pit",	"bones",	1.55f, 1600, 3 }
};

const char *ds_bossAttackNames[DSB_NUM] = {
	"", "MEAT HOOK", "CLEAVER CHARGE", "GROUND SLAM", "BLOOD FRENZY",
	"ROCKET BARRAGE", "DRONES", "ENERGY SHIELD", "MORTAR RAIN",
	"BLINK", "RAIL LANCES", "GRAVITY WELL", "DECOYS",
	"METEOR STORM", "FIRE NOVA", "FIRE FLOOR", "ROAR", "ON THE HUNT"
};

/*
-------------------------------------------------------------------------------
META (owner META)
-------------------------------------------------------------------------------
*/
const char *ds_statKeys[DSST_NUM] = {
	"kills", "headshots", "gkills", "gibs", "zips", "chests", "evos", "elites", "bosses",
	"deaths", "gems", "damage", "time", "won", "floor", "bossid", "vacuums", "levelups", "stashes"
};

// ds-secrets (g_ds_secrets.c): stash types, dsStash_t order
const char *ds_stashNames[DSS_NUM] = { "GEM CACHE", "GOLD CACHE", "STRONGBOX", "LEGENDARY", "OVERDRIVE" };

// Economy pass 2 (ds-economy): prices = the model in docs/DESCENT_V2.md "Economy" (the whole tree
// 14,800 skulls = roughly 40-60 descents). RETIRED rows keep their old prices for the refund.
const dsUpgradeInfo_t ds_upgrades[DSU_NUM] = {
	{ "up_health",		"IRON HEART",		"+5 max health per rank.",						0, 5, { 60, 110, 170, 240, 320 },	5.0f },
	{ "up_speed",		"FLEET FOOT",		"+2% move speed per rank.",						0, 5, { 60, 110, 170, 240, 320 },	0.02f },
	{ "up_damage",		"WHETSTONE",		"+3% damage per rank.",							0, 5, { 70, 130, 200, 280, 370 },	0.03f },
	{ "up_armor",		"BONE PLATE",		"(retired)",									-1, 0, { 30, 60, 100 },				10.0f },
	{ "up_skulls",		"GRAVE ROBBER",		"(retired)",									-1, 0, { 50, 110, 200 },			0.10f },

	{ "up_reroll",		"SECOND THOUGHTS",	"One card offer reroll per descent, per rank.",	1, 3, { 250, 450, 700 },			1.0f },
	{ "up_magnet",		"LODESTONE",		"(retired)",									-1, 0, { 40, 80, 130 },				0.06f },
	{ "up_growth",		"HUNGER",			"(retired)",									-1, 0, { 50, 100, 160 },			0.05f },
	{ "up_luck",		"RABBIT'S FOOT",	"(retired)",									-1, 0, { 50, 100, 160 },			0.05f },
	{ "up_tempo",		"HAIR TRIGGER",		"(retired)",									-1, 0, { 60, 120, 190 },			0.02f },

	{ "up_startcard",	"BIRTHRIGHT",		"Start every descent with a free ability card you pick.",	1, 1, { 400 },		1.0f },
	{ "up_cards",		"WIDER CHOICE",		"Every card offer shows 4 cards instead of 3.",	1, 1, { 900 },						1.0f },
	{ "up_area",		"BLAST CORE",		"(retired)",									-1, 0, { 70, 140, 220 },			0.04f },
	{ "up_ironhide",	"IRON HIDE",		"(retired)",									-1, 0, { 70, 140, 220 },			0.03f },
	{ "up_treasure",	"TREASURE HUNTER",	"+5% chance per rank of one more chest reward.",	2, 3, { 350, 600, 900 },	0.05f },

	{ "up_life",		"SPARE SOUL",		"+1 life on any difficulty.",					3, 1, { 1500 },						1.0f },
	{ "up_revival",		"REVIVAL",			"Once per descent, the loss that would end it costs no life.",	3, 1, { 1200 },				1.0f },
	{ "up_arsenal",		"ARSENAL",			"Every weapon starts at level 2.",				2, 1, { 1000 },						1.0f },
	{ "up_firstpick",	"FIRST PICK",		"The first card offer of every act shows 5 cards.",	1, 1, { 600 },					1.0f },
	{ "up_vacuum",		"EVENT HORIZON",	"(retired)",									-1, 0, { 150, 250, 380 },		0.06f },
	{ "up_lastbreath",	"LAST BREATH",		"+1 life on every boss floor per rank.",		3, 2, { 700, 1200 },				1.0f },
	{ "up_quarter",		"QUARTERMASTER",	"Every act's first floor starts with a chest in the bank.",	2, 1, { 1200 },		1.0f }
};

const char *ds_upTierNames[DS_UP_TIERS] = { "FLESH", "MARROW", "IRON", "SOUL" };
const char *ds_upTierDescs[DS_UP_TIERS] = { "the body", "the cards", "the spoils", "the grave" };
const int ds_upTierNeed[DS_UP_TIERS] = { 0, 5, 3, 3 };

const char *ds_secretHints[RLS_NUM] = {
	"Paint one floor red.",
	"Master three weapons in one descent.",
	"Greed pays, in the long run.",
	"Some floors are over before they begin.",
	"Two masterpieces in one descent.",
	"Some treasure was never meant to be found."
};
const char *ds_secretConds[RLS_NUM] = {
	"Gib 10 enemies on one floor.",
	"Three weapons at level 8 in one descent.",
	"Open 25 chests.",
	"Win a floor in under 60 seconds.",
	"Evolve two weapons in one descent.",
	"Find 5 secret stashes."
};
const char *ds_orbbHint = "The eye opens for those who survive the worst, or who see every secret.";

/*
-------------------------------------------------------------------------------
ECONOMY: reward multiplier by difficulty (skulls, gem xp, skill xp)
-------------------------------------------------------------------------------
*/
const float ds_rewardMult[6] = { 1.0f, 0.5f, 0.75f, 1.0f, 1.05f, 1.10f };

float BG_DS_RewardMult( int difficulty ) {
	if ( difficulty < 1 || difficulty > 5 ) {
		return 1.0f;
	}
	return ds_rewardMult[difficulty];
}

/*
-------------------------------------------------------------------------------
FRENZY (ds-frenzy): kill-chain tiers
-------------------------------------------------------------------------------
*/
const int ds_frenzyTierKills[DS_FRENZY_TIERS + 1] = { 0, 3, 6, 10, 15 };
const char *ds_frenzyTierNames[DS_FRENZY_TIERS + 1] = { "", "BLOODLUST", "RAMPAGE", "UNSTOPPABLE", "MAYHEM" };
