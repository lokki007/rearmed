/*
===========================================================================
REARMED -- DESCENT skills: data tables, the xp curve, the character level
(built into game, cgame and ui). Contract and file keys: bg_ds_skills.h.
===========================================================================
*/
#include "../qcommon/q_shared.h"
#include "bg_public.h"

const dsSkillInfo_t ds_skills[SK_NUM] = {
	{ "MELEE",		"The gauntlet",									{ 1.00f, 0.42f, 0.25f } },
	{ "BALLISTICS",	"Machinegun, chaingun, nailgun",				{ 0.95f, 0.80f, 0.40f } },
	{ "SCATTER",	"The shotgun",									{ 1.00f, 0.62f, 0.22f } },
	{ "EXPLOSIVES",	"Rockets, grenades, prox mines, the BFG",		{ 1.00f, 0.32f, 0.18f } },
	{ "ENERGY",		"Lightning gun, plasma gun",					{ 0.40f, 0.82f, 1.00f } },
	{ "PRECISION",	"The railgun, and headshots with any weapon",	{ 0.45f, 1.00f, 0.55f } },
	{ "TRAVERSAL",	"Wall runs, wall and air jumps, slides, zip line, grapple, fast air time",	{ 0.55f, 0.72f, 1.00f } },
	{ "EVASION",	"Dashes, blinks, near misses",					{ 0.78f, 0.58f, 1.00f } },
	{ "SURVIVAL",	"Damage taken and lived through, healing",		{ 1.00f, 0.48f, 0.62f } },
	{ "CARNAGE",	"Gibs and style points",						{ 0.90f, 0.14f, 0.18f } }
};

/*
Perks. Effects live in game/g_ds_skills.c (DESCENT only); numbers here are what the
game uses. No rank gives 10% or more.
*/
const dsPerkInfo_t ds_perks[SKP_NUM] = {
	// MELEE
	{ SK_MELEE, "BRAWLER",		"+%i%% gauntlet damage",							5, 3, { 1, 25, 50 },	{ -1, -1 },	0.50f, 0.06f },
	{ SK_MELEE, "LONG ARM",		"+%i%% gauntlet reach",								8, 1, { 15 },			{ SKP_BRAWLER, -1 },	0.28f, 0.30f },
	{ SK_MELEE, "BLOODLETTER",	"gauntlet hits heal %i health",						2, 2, { 20, 45 },		{ SKP_BRAWLER, -1 },	0.72f, 0.28f },
	{ SK_MELEE, "SWIFT FISTS",	"the gauntlet hits %i%% faster",					5, 2, { 30, 60 },		{ SKP_LONGARM, -1 },	0.20f, 0.58f },
	{ SK_MELEE, "JUGGERNAUT",	"-%i%% damage taken while holding the gauntlet",	6, 1, { 40 },			{ SKP_BLOODLETTER, -1 },	0.78f, 0.55f },
	{ SK_MELEE, "RECKONING",	"gauntlet kills refill a dash charge",				0, 1, { 55 },			{ SKP_SWIFTFISTS, SKP_JUGGERNAUT },	0.48f, 0.74f },
	{ SK_MELEE, "MEAT GRINDER",	"gauntlet kills always gib and drop a green gem",	0, 1, { 80 },			{ SKP_RECKONING, -1 },	0.55f, 0.95f },
	// BALLISTICS
	{ SK_BALLISTICS, "STEADY HANDS",	"+%i%% machinegun, chaingun and nailgun damage",	4, 3, { 1, 25, 50 },	{ -1, -1 },	0.50f, 0.06f },
	{ SK_BALLISTICS, "SCAVENGER",		"kills with them give back %i rounds",				10, 1, { 15 },			{ SKP_STEADYHANDS, -1 },	0.25f, 0.25f },
	{ SK_BALLISTICS, "RAPID CYCLE",		"they fire %i%% faster",							3, 2, { 20, 45 },		{ SKP_STEADYHANDS, -1 },	0.62f, 0.30f },
	{ SK_BALLISTICS, "SPIN DOCTOR",		"the chaingun spins up %i%% faster",				9, 1, { 30 },			{ SKP_RAPIDCYCLE, -1 },	0.86f, 0.48f },
	{ SK_BALLISTICS, "SHREDDER",		"+1%% damage per hit in a row on one target, up to +%i%%",	8, 1, { 40 },	{ SKP_RAPIDCYCLE, -1 },	0.45f, 0.55f },
	{ SK_BALLISTICS, "ARMOR PIERCER",	"+%i%% damage to armored enemies",					4, 2, { 50, 70 },		{ SKP_SCAVENGER, SKP_SHREDDER },	0.20f, 0.70f },
	{ SK_BALLISTICS, "TRIGGER DISCIPLINE", "%i%% chance a shot costs no ammo",				8, 1, { 75 },			{ SKP_PIERCER, SKP_SPINDOCTOR },	0.60f, 0.93f },
	// SCATTER
	{ SK_SCATTER, "BUCKSHOT",		"+%i%% shotgun damage",								4, 3, { 1, 25, 50 },	{ -1, -1 },	0.50f, 0.06f },
	{ SK_SCATTER, "POINT BLANK",	"+%i%% shotgun damage within 250 units",			5, 2, { 15, 40 },		{ SKP_BUCKSHOT, -1 },	0.30f, 0.30f },
	{ SK_SCATTER, "QUICK PUMP",		"the shotgun fires %i%% faster",					4, 2, { 20, 45 },		{ SKP_BUCKSHOT, -1 },	0.72f, 0.30f },
	{ SK_SCATTER, "FLAYER",			"shotgun kills within 250 units always gib",		0, 1, { 30 },			{ SKP_POINTBLANK, -1 },	0.18f, 0.58f },
	{ SK_SCATTER, "BRACE",			"-%i%% damage taken while holding the shotgun",		5, 1, { 40 },			{ SKP_POINTBLANK, SKP_QUICKPUMP },	0.50f, 0.55f },
	{ SK_SCATTER, "SECOND BARREL",	"%i%% chance a blast costs no shell",				8, 1, { 55 },			{ SKP_QUICKPUMP, -1 },	0.82f, 0.62f },
	{ SK_SCATTER, "LAST RITES",		"shotgun kills heal %i health",						5, 1, { 75 },			{ SKP_FLAYER, SKP_SECONDBARREL },	0.45f, 0.92f },
	// EXPLOSIVES
	{ SK_EXPLOSIVES, "DEMOLITION",	"+%i%% splash damage",								4, 3, { 1, 25, 50 },	{ -1, -1 },	0.50f, 0.06f },
	{ SK_EXPLOSIVES, "SAFETY FUSE",	"-%i%% damage from your own explosions",			6, 2, { 15, 35 },		{ SKP_DEMOLITION, -1 },	0.22f, 0.25f },
	{ SK_EXPLOSIVES, "PAYLOAD",		"rockets, grenades and mines fly %i%% faster",		5, 1, { 20 },			{ SKP_DEMOLITION, -1 },	0.76f, 0.24f },
	{ SK_EXPLOSIVES, "BLAST RADIUS",	"+%i%% splash radius",							4, 2, { 30, 55 },		{ SKP_DEMOLITION, -1 },	0.48f, 0.43f },
	{ SK_EXPLOSIVES, "AIR RAID",	"+%i%% explosive damage to airborne enemies",		7, 1, { 40 },			{ SKP_PAYLOAD, -1 },	0.84f, 0.56f },
	{ SK_EXPLOSIVES, "IMPACT",		"+%i%% damage from direct hits",					8, 1, { 50 },			{ SKP_SAFETYFUSE, SKP_BLASTRADIUS },	0.20f, 0.64f },
	{ SK_EXPLOSIVES, "CHAIN FUSE",	"explosive kills give back one round",				0, 1, { 65 },			{ SKP_IMPACT, SKP_AIRRAID },	0.53f, 0.91f },
	// ENERGY
	{ SK_ENERGY, "CAPACITOR",		"+%i%% lightning and plasma damage",				4, 3, { 1, 25, 50 },	{ -1, -1 },	0.50f, 0.06f },
	{ SK_ENERGY, "OVERCLOCK",		"the plasma gun fires %i%% faster",					3, 2, { 15, 40 },		{ SKP_CAPACITOR, -1 },	0.30f, 0.28f },
	{ SK_ENERGY, "GROUNDED",		"-%i%% damage taken from lightning and plasma",		6, 1, { 20 },			{ SKP_CAPACITOR, -1 },	0.75f, 0.32f },
	{ SK_ENERGY, "SUPERHEATED",		"+%i%% plasma splash radius",						6, 1, { 30 },			{ SKP_OVERCLOCK, -1 },	0.15f, 0.53f },
	{ SK_ENERGY, "STATIC CHARGE",	"+%i%% lightning damage after 1 s on one target",	6, 1, { 45 },			{ SKP_OVERCLOCK, SKP_GROUNDED },	0.56f, 0.56f },
	{ SK_ENERGY, "RECYCLER",		"%i%% chance an energy shot costs no ammo",			8, 1, { 60 },			{ SKP_SUPERHEATED, SKP_STATIC },	0.30f, 0.80f },
	{ SK_ENERGY, "SIPHON",			"energy kills restore %i armor",					5, 1, { 75 },			{ SKP_STATIC, -1 },	0.76f, 0.88f },
	// PRECISION
	{ SK_PRECISION, "DEADEYE",		"+%i%% railgun damage",								4, 3, { 1, 25, 50 },	{ -1, -1 },	0.50f, 0.06f },
	{ SK_PRECISION, "STEADY",		"the railgun recharges %i%% faster",				4, 2, { 15, 40 },		{ SKP_DEADEYE, -1 },	0.32f, 0.30f },
	{ SK_PRECISION, "SKULLCRACKER",	"+%i%% headshot damage with any weapon",			5, 2, { 20, 45 },		{ SKP_DEADEYE, -1 },	0.70f, 0.26f },
	{ SK_PRECISION, "LONG SHOT",	"+%i%% hitscan damage beyond 1200 units",			6, 1, { 30 },			{ SKP_STEADY, -1 },	0.20f, 0.58f },
	{ SK_PRECISION, "EXECUTIONER",	"headshot kills drop a green gem",					0, 1, { 40 },			{ SKP_SKULLCRACKER, -1 },	0.82f, 0.55f },
	{ SK_PRECISION, "SNIPER'S NEST",	"+%i%% railgun damage while standing still",	6, 1, { 55 },			{ SKP_LONGSHOT, SKP_STEADY },	0.44f, 0.73f },
	{ SK_PRECISION, "MARKED",		"railgun hits mark the enemy: +%i%% damage to it for 3 s",	5, 1, { 75 },	{ SKP_SNIPERNEST, SKP_EXECUTIONER },	0.64f, 0.94f },
	// TRAVERSAL
	{ SK_TRAVERSAL, "MOMENTUM",		"+%i%% speed while wall running",					3, 2, { 1, 30 },		{ -1, -1 },	0.50f, 0.06f },
	{ SK_TRAVERSAL, "SPRINGHEEL",	"air jumps and wall jumps %i%% higher",				5, 1, { 15 },			{ SKP_MOMENTUM, -1 },	0.25f, 0.28f },
	{ SK_TRAVERSAL, "SKYWALKER",	"wall runs last %i%% longer",						8, 2, { 20, 45 },		{ SKP_MOMENTUM, -1 },	0.73f, 0.30f },
	{ SK_TRAVERSAL, "SECOND WIND",	"a kill gives back your zip line and air jump",		0, 1, { 30 },			{ SKP_SPRINGHEEL, SKP_SKYWALKER },	0.42f, 0.52f },
	{ SK_TRAVERSAL, "AIRBORNE",		"+%i%% damage while airborne",						3, 2, { 40, 60 },		{ SKP_SPRINGHEEL, -1 },	0.15f, 0.67f },
	{ SK_TRAVERSAL, "SOFT LANDING",	"no fall damage",									0, 1, { 50 },			{ SKP_SKYWALKER, -1 },	0.83f, 0.62f },
	{ SK_TRAVERSAL, "SLINGSHOT",	"+%i%% speed for 2 s after a zip line or grapple",	5, 1, { 70 },			{ SKP_SECONDWIND, -1 },	0.50f, 0.91f },
	// EVASION
	{ SK_EVASION, "GHOST",			"the dash recharges %i%% faster",					5, 2, { 1, 30 },		{ -1, -1 },	0.50f, 0.06f },
	{ SK_EVASION, "SIDESTEP",		"-%i%% damage taken for 1 s after a dash",			4, 2, { 15, 40 },		{ SKP_GHOST, -1 },	0.27f, 0.30f },
	{ SK_EVASION, "SLIPPERY",		"-%i%% splash damage taken",						6, 1, { 20 },			{ SKP_GHOST, -1 },	0.73f, 0.28f },
	{ SK_EVASION, "AFTERIMAGE",		"near misses recharge your dash by 0.3 s",			0, 1, { 30 },			{ SKP_GHOST, -1 },	0.50f, 0.47f },
	{ SK_EVASION, "COUNTERSTEP",	"+%i%% damage for 1.5 s after a dash",				6, 1, { 45 },			{ SKP_SIDESTEP, SKP_AFTERIMAGE },	0.20f, 0.66f },
	{ SK_EVASION, "SHADOW",			"%i%% chance to dodge a direct hit",				5, 1, { 60 },			{ SKP_SLIPPERY, SKP_AFTERIMAGE },	0.80f, 0.66f },
	{ SK_EVASION, "REFLEX",			"a kill within 1 s of a dash refills a dash charge",	0, 1, { 75 },		{ SKP_COUNTERSTEP, SKP_SHADOW },	0.50f, 0.92f },
	// SURVIVAL
	{ SK_SURVIVAL, "TOUGHNESS",		"+%i max health",									3, 3, { 1, 25, 50 },	{ -1, -1 },	0.50f, 0.06f },
	{ SK_SURVIVAL, "IRON SKIN",		"-%i%% damage taken",								3, 2, { 15, 40 },		{ SKP_TOUGHNESS, -1 },	0.30f, 0.30f },
	{ SK_SURVIVAL, "MENDING",		"below 50 health, regenerate %i health every 2 s",	1, 2, { 20, 45 },		{ SKP_TOUGHNESS, -1 },	0.72f, 0.28f },
	{ SK_SURVIVAL, "ARMORER",		"spawn with %i armor",								10, 1, { 30 },			{ SKP_IRONSKIN, -1 },	0.18f, 0.56f },
	{ SK_SURVIVAL, "GRIT",			"-%i%% damage taken below 25 health",				8, 1, { 40 },			{ SKP_IRONSKIN, SKP_MENDING },	0.52f, 0.55f },
	{ SK_SURVIVAL, "UNBREAKABLE",	"every hit you take is 1 point smaller",			0, 1, { 55 },			{ SKP_ARMORER, SKP_GRIT },	0.30f, 0.82f },
	{ SK_SURVIVAL, "CHEAT DEATH",	"once per floor, a killing blow leaves you at 1 health",	0, 1, { 75 },	{ SKP_GRIT, -1 },	0.75f, 0.88f },
	// CARNAGE
	{ SK_CARNAGE, "RED RAIN",		"gibbing an enemy heals %i health",					2, 2, { 1, 30 },		{ -1, -1 },	0.50f, 0.06f },
	{ SK_CARNAGE, "SHOWOFF",		"+%i%% style points",								4, 2, { 15, 40 },		{ SKP_REDRAIN, -1 },	0.25f, 0.28f },
	{ SK_CARNAGE, "GORE HOUND",		"gib kills drop an extra blue gem",					0, 1, { 20 },			{ SKP_REDRAIN, -1 },	0.75f, 0.30f },
	{ SK_CARNAGE, "OVERKILL",		"+%i%% damage to enemies below 30 health",			5, 1, { 30 },			{ SKP_GOREHOUND, -1 },	0.56f, 0.50f },
	{ SK_CARNAGE, "FRENZY",			"at style rank S or above, fire %i%% faster",		4, 1, { 45 },			{ SKP_SHOWOFF, -1 },	0.20f, 0.60f },
	{ SK_CARNAGE, "CARNIVAL",		"gib kills hold your style meter for 3 s",			0, 1, { 55 },			{ SKP_FRENZY, SKP_OVERKILL },	0.42f, 0.80f },
	{ SK_CARNAGE, "MASSACRE",		"two kills within 2 s heal %i and refill a dash charge",	5, 1, { 75 },	{ SKP_OVERKILL, -1 },	0.78f, 0.90f }
};

/*
-------------------------------------------------------------------------------
THE XP CURVE (Skyrim-like: fast early, slow late)

xp from level L to L + 1 = SKC_A + SKC_B * L + SKC_C * L * L / 10
(120 for level 1 -> 2, 815 xp reach level 6, 9.9k level 25, 41.5k level 50, 207k level 100)
The character level: level n -> n + 1 needs ( SKC_CL + n ) more skill levels
(level 2 at 7 skill levels gained, 3 at 15, 4 at 24, 5 at 34; all 990 = level 39, 38 points for 104 ranks)
(skill levels gained = the sum of every skill's level - 1).
Numbers and the reasoning: README "Skills".
-------------------------------------------------------------------------------
*/
#define SKC_A		100
#define SKC_B		20
#define SKC_C		3
#define SKC_CL		6

int BG_DS_SkillXPToNext( int level ) {
	if ( level < 1 ) {
		level = 1;
	}
	if ( level >= SK_MAX_LEVEL ) {
		return 0;
	}
	return SKC_A + SKC_B * level + SKC_C * level * level / 10;
}

int BG_DS_SkillTotalXP( int level ) {
	int		l, t = 0;

	for ( l = 1 ; l < level && l < SK_MAX_LEVEL ; l++ ) {
		t += BG_DS_SkillXPToNext( l );
	}
	return t;
}

int BG_DS_SkillLevel( int xp, int *into, int *need ) {
	int		l, n;

	if ( xp < 0 ) {
		xp = 0;
	}
	for ( l = 1 ; l < SK_MAX_LEVEL ; l++ ) {
		n = BG_DS_SkillXPToNext( l );
		if ( xp < n ) {
			break;
		}
		xp -= n;
	}
	if ( into ) {
		*into = l >= SK_MAX_LEVEL ? 0 : xp;
	}
	if ( need ) {
		*need = BG_DS_SkillXPToNext( l );
	}
	return l;
}

int BG_DS_CharLevelNeed( int charLevel ) {
	int		n, t = 0;

	for ( n = 1 ; n < charLevel ; n++ ) {
		t += SKC_CL + n;
	}
	return t;
}

int BG_DS_CharLevel( const int *levels ) {
	int		i, gained = 0, cl;

	for ( i = 0 ; i < SK_NUM ; i++ ) {
		if ( levels[i] > 1 ) {
			gained += levels[i] - 1;
		}
	}
	cl = 1;
	while ( gained >= BG_DS_CharLevelNeed( cl + 1 ) ) {
		cl++;
	}
	return cl;
}

int BG_DS_PerkPoints( const int *levels, const int *ranks ) {
	int		i, spent = 0;

	for ( i = 0 ; i < SKP_NUM ; i++ ) {
		spent += ranks[i];
	}
	return BG_DS_CharLevel( levels ) - 1 - spent;
}

qboolean BG_DS_PerkOpen( const int *ranks, int perk ) {
	const dsPerkInfo_t	*p;
	int					i;

	if ( perk < 0 || perk >= SKP_NUM ) {
		return qfalse;
	}
	p = &ds_perks[perk];
	if ( p->parent[0] < 0 ) {
		return qtrue;
	}
	for ( i = 0 ; i < 2 ; i++ ) {
		if ( p->parent[i] >= 0 && ranks[p->parent[i]] > 0 ) {
			return qtrue;
		}
	}
	return qfalse;
}

int BG_DS_SkillForWeapon( int weapon ) {
	switch ( weapon ) {
	case 1:		return SK_MELEE;			// WP_GAUNTLET
	case 2:		// WP_MACHINEGUN
	case 11:	// WP_NAILGUN
	case 13:	return SK_BALLISTICS;		// WP_CHAINGUN
	case 3:		return SK_SCATTER;			// WP_SHOTGUN
	case 4:		// WP_GRENADE_LAUNCHER
	case 5:		// WP_ROCKET_LAUNCHER
	case 9:		// WP_BFG
	case 12:	return SK_EXPLOSIVES;		// WP_PROX_LAUNCHER
	case 6:		// WP_LIGHTNING
	case 8:		return SK_ENERGY;			// WP_PLASMAGUN
	case 7:		return SK_PRECISION;		// WP_RAILGUN
	case 10:	return SK_TRAVERSAL;		// WP_GRAPPLING_HOOK
	default:
		return -1;
	}
}

const char *BG_DS_PerkText( int perk, int rank ) {
	static char		buf[2][128];
	static int		index;
	char			*b;

	if ( perk < 0 || perk >= SKP_NUM ) {
		return "";
	}
	b = buf[index++ & 1];
	if ( rank < 1 ) {
		rank = 1;
	}
	Com_sprintf( b, sizeof( buf[0] ), ds_perks[perk].fmt, ds_perks[perk].value * rank );
	return b;
}
