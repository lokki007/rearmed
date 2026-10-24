/*
===========================================================================
REARMED -- DESCENT v2 (0.5.0) shared contract: acts, weapon levels, evolutions,
passive items, secret cards, characters, bosses. Included by bg_public.h (before the
rlCard_t enum), so it must not depend on anything bg_public.h declares later: weapons
are plain ints in weapon_t order (WP_GAUNTLET 1 .. WP_CHAINGUN 13).
Data tables live in bg_descent.c (built into game, cgame and ui).
See docs/DESCENT_V2.md for the design and who owns what.
===========================================================================
*/
#ifndef BG_DESCENT_H
#define BG_DESCENT_H

#define DS_RUN_VERSION		2		// rearmed_run.txt "version": older runs are discarded

/*
-------------------------------------------------------------------------------
ACTS: 4 acts of 6 floors, the 6th floor of each act is that act's boss.
After the act 4 boss the run is WON; the player may go on into the Abyss (act 5,
endless, everything scales, a random boss every 6th floor).
-------------------------------------------------------------------------------
*/
#define DS_FLOORS_PER_ACT	6
#define DS_NUM_ACTS			4
#define DS_ACT_ABYSS		5
#define DS_WIN_FLOOR		( DS_NUM_ACTS * DS_FLOORS_PER_ACT )		// 24
#define DS_ActForFloor(f)	( (f) > DS_WIN_FLOOR ? DS_ACT_ABYSS : ( ( (f) - 1 ) / DS_FLOORS_PER_ACT + 1 ) )
#define DS_IsBossFloor(f)	( (f) > 0 && ( (f) % DS_FLOORS_PER_ACT ) == 0 )

typedef struct {
	const char	*name;			// "THE CRYPTS"
	const char	*sub;			// one-line flavour
	const char	*maps;			// space separated map pool (filtered by what is installed)
	int			boss;			// dsBoss_t of the act boss (-1 in the Abyss: random)
	float		grade[4];		// screen colour grade: rgb multiply + strength (cgame)
	float		fog[4];			// fog / haze tint rgb + density 0..1 (cgame overlay)
	int			particles;		// DSFX_ ambient particle kind
	const char	*music;			// music track (from the player's own Q3 install)
} dsAct_t;

#define DSFX_NONE		0
#define DSFX_DUST		1
#define DSFX_EMBERS		2
#define DSFX_MOTES		3		// void motes, drifting up
#define DSFX_ASH		4

extern const dsAct_t ds_acts[DS_ACT_ABYSS + 1];		// index 1..5 (0 unused)
extern const char *ds_roman[6];						// "", "I" .. "V": acts and veteran ranks (DS_MAX_RANK 5)

/*
-------------------------------------------------------------------------------
WEAPON LEVELS 1..8. Every weapon you hold starts at level 1. Gems from kills feed the
weapon that made the kill; cards and chests add whole levels. Level 8 + the
evolution's key card (a passive at any level, or a perk) = the evolution, granted
by the next treasure chest.
-------------------------------------------------------------------------------
*/
#define DS_NUM_WEAPONS		14		// weapon_t slots WP_NONE..WP_CHAINGUN (WP_NUM_WEAPONS)
#define DS_MAX_WLEVEL		8
#define DS_WEAPON_LEVELS(w)	( (w) >= 1 && (w) <= 13 && (w) != 10 )	// not WP_NONE / WP_GRAPPLING_HOOK

extern const int	ds_wlevelXP[DS_MAX_WLEVEL + 1];				// total xp needed to reach level n
extern const char	*ds_weaponNames[DS_NUM_WEAPONS];			// "ROCKET LAUNCHER"
extern const char	*ds_weaponLevelDesc[DS_NUM_WEAPONS][DS_MAX_WLEVEL + 1];	// what level n adds

/*
-------------------------------------------------------------------------------
PASSIVE ITEMS: up to DS_PASSIVE_SLOTS different ones, 5 levels each.
-------------------------------------------------------------------------------
*/
typedef enum {
	RLP_MIGHT,			// SPITE: +5% damage per level
	RLP_IRONHIDE,		// IRON HIDE: -4% damage taken per level
	RLP_HEART,			// HOLLOW HEART: +6 max health per level
	RLP_REGEN,			// BLACK BLOOD: regenerate 0.3 health/s per level (up to max)
	RLP_HASTE,			// QUICKSILVER: +3% move speed per level
	RLP_TEMPO,			// HAIR TRIGGER: fire 4% faster per level
	RLP_AREA,			// BLAST CORE: +6% splash radius per level
	RLP_VELOCITY,		// RIFLING: +6% projectile speed per level
	RLP_MAGNET,			// LODESTONE: +25% gem pull radius per level
	RLP_LUCK,			// RABBIT'S FOOT: +8% luck per level (chest jackpots, rarer cards)
	RLP_GROWTH,			// HUNGER: +8% gem xp per level
	RLP_GREED,			// GRAVE COIN: +8% skulls per level
	RLP_AMMOBELT,		// AMMO BELT: +8% max ammo and pickups per level
	RLP_NUM
} dsPassive_t;

#define DS_MAX_PLEVEL		5
#define DS_PASSIVE_SLOTS	6

extern const char	*ds_passiveNames[RLP_NUM];
extern const char	*ds_passiveDescs[RLP_NUM];		// per level effect ("+8% damage")
extern const char	*ds_passiveShort[RLP_NUM];		// HUD tag
extern const float	ds_passivePerLevel[RLP_NUM];	// the number in the description, as a fraction or amount

/*
-------------------------------------------------------------------------------
EVOLUTIONS: weapon at level 8 + key card held -> the next chest evolves it.
EVO_ARMAGEDDON is the secret union: evolved rockets + evolved grenades, needs the
UNION secret card.
-------------------------------------------------------------------------------
*/
typedef enum {
	EVO_HEARTRIPPER,	// gauntlet + BLACK BLOOD
	EVO_BULLETHELL,		// machinegun + HAIR TRIGGER
	EVO_DRAGONBREATH,	// shotgun + BLAST CORE
	EVO_CLUSTERBOMB,	// grenade launcher + RABBIT'S FOOT
	EVO_HELLFIRE,		// rocket launcher + SPITE
	EVO_STORMCALLER,	// lightning gun + CONDUCTOR (perk)
	EVO_JUDGEMENT,		// railgun + HEADHUNTER (perk)
	EVO_STARFURNACE,	// plasma gun + RIFLING
	EVO_DOOMSDAY,		// BFG + LODESTONE
	EVO_IRONMAIDEN,		// nailgun + IRON HIDE
	EVO_MINEFIELD,		// prox launcher + PYROMANIAC (perk)
	EVO_RIPSAW,			// chaingun + AMMO BELT
	EVO_ARMAGEDDON,		// secret union: HELLFIRE + CLUSTER BOMB (+ UNION card) -> rocket launcher
	EVO_NUM
} dsEvolution_t;

typedef struct {
	int			weapon;		// weapon_t
	int			keyCard;	// rlCard_t that unlocks it (a passive card, a perk card or a secret)
	const char	*name;
	const char	*desc;
} dsEvoInfo_t;

extern const dsEvoInfo_t ds_evolutions[EVO_NUM];
int BG_DS_EvolutionForWeapon( int weapon );		// the normal (non-union) evolution, -1 none

/*
-------------------------------------------------------------------------------
SECRET CARDS: never offered until unlocked (meta "secrets" bit mask).
-------------------------------------------------------------------------------
*/
typedef enum {
	RLS_PENTAGRAM,		// once per floor: a killing blow instead gibs every enemy in sight, you live on 1 hp
	RLS_BLOODCROWN,		// gems give double xp, you take +15% damage
	RLS_GOLDENSKULL,	// every chest is a 5-reward jackpot
	RLS_STOPWATCH,		// every 20 s your next kill freezes time for 1.5 s
	RLS_UNION,			// evolved rockets + evolved grenades fuse into ARMAGEDDON
	RLS_DOWSING,		// ds-secrets: secret stashes hum louder, a faint compass tick on the HUD (5 stashes found)
	RLS_NUM
} dsSecret_t;

/*
-------------------------------------------------------------------------------
CHARACTERS: picked at the start of a run; unlocked by achievements (meta "chars" mask).
-------------------------------------------------------------------------------
*/
typedef enum {
	CHR_RANGER,			// free: machinegun lv2, no weakness
	CHR_SARGE,			// shotgun lv2 + IRON HIDE 1
	CHR_VISOR,			// railgun + HEADHUNTER
	CHR_ANARKI,			// double jump + dash, QUICKSILVER 2, -20 max health
	CHR_KLESK,			// gauntlet lv3 + BLACK BLOOD 2
	CHR_XAERO,			// plasma gun + BLAST CORE 1
	CHR_DOOM,			// rocket launcher + SPITE 1, -4% speed
	CHR_HUNTER,			// grapple + zip line, LODESTONE 2
	CHR_ORBB,			// secret: every alt-fire unlocked, 50 max health
	CHR_NUM
} dsCharacter_t;

typedef struct {
	const char	*name;
	const char	*model;			// player model/skin forced for the run
	const char	*desc;			// starting kit, one line
	const char	*unlock;		// how to unlock it ("Clear Act 1")
	int			startWeapon;	// weapon_t given at every spawn (0 none)
	int			startLevel;		// its starting level
	int			startCards[4];	// rlCard_t given at run start (-1 end)
	int			startPassiveLevels;	// how many levels of startCards[0] if that is a passive (else 0)
	int			healthDelta;	// max health change
	int			speedPct;		// move speed change in percent
} dsCharInfo_t;

extern const dsCharInfo_t ds_characters[CHR_NUM];

/*
-------------------------------------------------------------------------------
BOSSES: one per act. A boss is a bot client (kit role boss) with a big health pool
split into phases (each "death" before the last starts the next phase), its own
attacks run by the game (g_descent_boss.c), a top-of-screen health bar.
Boss floor: win = boss dead; loss = the boss frags you DS_BOSS_FRAGS_TO_LOSE times.
-------------------------------------------------------------------------------
*/
typedef enum {
	BOSS_BUTCHER,		// act 1: meat hook pull, cleaver charge, ground slam; phase 3 blood frenzy
	BOSS_OVERSEER,		// act 2: rocket barrages, summons drones, energy shield; phase 3 mortar rain
	BOSS_HOLLOWKING,	// act 3: blink behind you, telegraphed rail lances, gravity well; phase 3 clones
	BOSS_ARCHDEMON,		// act 4: meteor storm, fire nova rings, fire floor; phase 3 everything, enraged
	BOSS_NUM
} dsBoss_t;

#define DS_BOSS_FRAGS_TO_LOSE	3

typedef struct {
	const char	*name;			// "THE BUTCHER"
	const char	*title;			// "Lord of the Crypts"
	const char	*model;			// player model/skin
	float		scale;			// model scale (cgame)
	int			health;			// health per phase at floor 6 (scaled by difficulty / Abyss depth)
	int			phases;
} dsBossInfo_t;

extern const dsBossInfo_t ds_bosses[BOSS_NUM];

// boss attacks (owner ACTS, g_ds_boss.c): RM_ENT_BOSSFX modelindex2 and the CS_DESCENTBOSS "at" key
typedef enum {
	DSB_NONE,
	DSB_HOOK,			// BUTCHER: a line to you, then the hook yanks you in
	DSB_CLEAVER,		// BUTCHER: a lane, then a fast charge with a heavy blow
	DSB_SLAM,			// BUTCHER: leaps, a ground circle, shockwave on landing (jump it)
	DSB_FRENZY,			// BUTCHER phase 3: faster, lifesteal (status)
	DSB_BARRAGE,		// OVERSEER: target circles, arcing rockets
	DSB_DRONES,			// OVERSEER: two drones appear (glow at their spots)
	DSB_SHIELD,			// OVERSEER: energy shield while the drones live
	DSB_MORTAR,			// OVERSEER phase 3: rain of mortar circles
	DSB_BLINK,			// HOLLOW KING: glows at both ends, then he is behind you
	DSB_LANCE,			// HOLLOW KING: rail lines that fire after a delay
	DSB_WELL,			// HOLLOW KING: a gravity well that drags you
	DSB_CLONES,			// HOLLOW KING phase 3: decoys that throw lances
	DSB_METEOR,			// ARCH-DEMON: meteor circles
	DSB_NOVA,			// ARCH-DEMON: a fire ring rolling outwards (jump it)
	DSB_FIREFLOOR,		// ARCH-DEMON: circles that become fire patches
	DSB_ROAR,			// phase change: short invulnerable roar
	DSB_STALK,			// any boss: too long out of reach, it comes for you (a glow where it lands)
	DSB_NUM
} dsBossAttack_t;

extern const char *ds_bossAttackNames[DSB_NUM];	// "MEAT HOOK"

#define DS_BOSS_TELEGRAPH	1000	// msec from the warning to the hit (0.8 - 1.2 s by attack)
#define DS_VAULT_TIME		45000	// msec of a vault floor
#define RM_ENT_BOSSPROP		73		// ET_GENERAL boss helper: modelindex2 0 drone, 1 decoy clone; otherEntityNum = boss client

/*
-------------------------------------------------------------------------------
NETWORK
-------------------------------------------------------------------------------
CS_DESCENTGROW (26): the human player's growth, info string
	\wl\l.l.l...		weapon levels, DS_NUM_WEAPONS values (0 = not levelled yet = 1)
	\wx\x.x.x...		weapon xp
	\pv\l.l.l...		passive levels, RLP_NUM values
	\ev\<mask>			evolved weapons, bit per dsEvolution_t
	\ch\q.q.q			banked chests (quality each: 0 normal, 1 elite, 2 boss)
	\gm\<n>				gems collected this floor
	\chr\<n>			character
	\act\<n>			act (1..5)
CS_DESCENTBOSS (31): \cl\<client>\b\<dsBoss_t>\hp\<n>\max\<n>\ph\<phase>\nph\<phases>\at\<attack>\att\<time>
	\pd\<player deaths this floor>\sh\<1 shield up>\fz\<1 frenzy / enraged>\rr\<roar until (level time)>
	\ab\<abyss depth, 0 in acts 1-4>\dead\<1 the boss is beaten>
	vault floor instead: \vault\<end level time>  (empty when neither)
*/
#define CS_DESCENTGROW		26
#define CS_DESCENTBOSS		31
/*
CS_DSINTRO (737, ds-intro: g_ds_intro.c -> cg_ds_intro.c): this floor's planned opponents, set in
G_InitGame so the loading screen has them. Info string \n\<count>\f<i>\<entry>, entry =
"role,flags,wlevel,abilities,alts,perks,evolved,nemesisKills,model/skin,Name" (role 2 bot,
3 elite, 4 boss; flags 1 nemesis, 2 boss-floor rival). Empty outside a run.
*/
#define CS_DSINTRO			737
#define CS_DSARRIVE			742		// ds-arrivals: g_dsStagger staggered bot arrivals (game/g_ds_arrive.c), "" = all at once
#define DSI_NEMESIS			1
#define DSI_RIVAL			2

// entityState->generic1 markers of ET_GENERAL descent entities (continue the RM_ENT_ series)
#define RM_ENT_GEM			68		// xp gem: modelindex2 = tier (0 blue 1, 1 green 5, 2 red 25), otherEntityNum2 = owner weapon hint
#define RM_ENT_CHEST		69		// treasure chest: modelindex2 = quality 0..2
#define RM_ENT_VACUUM		70		// pickup: pulls every gem on the map to whoever takes it
#define RM_ENT_BOSSFX		71		// boss attack telegraph: modelindex2 = DSB_ attack, origin/origin2 = from/to, time2 = when it lands
#define RM_ENT_VAULTGEMS	72		// vault SHOOTING GALLERY crystal: modelindex2 1 = golden; shoot it for gems
// vault floor modes (g_ds_boss.c Acts_VaultFrame, CS_DESCENTBOSS vm; g_dsVaultMode forces one)
#define DSV_RAIN			0		// gems rain around the player
#define DSV_GALLERY			1		// floating crystals: shoot them, they burst into gems
#define DSV_RUSH			2		// gems appear ahead of a fast player: the faster, the more
#define DSV_SKY				3		// gems hang in the air in columns: the higher, the richer
#define DSV_NUM				4
#define DSV_RUSH_RUN		300		// RUSH speed tiers (horizontal ups): x1 at a run, x2 fast, x4 blazing
#define DSV_RUSH_FAST		450
#define DSV_RUSH_BLAZE		650

/*
-------------------------------------------------------------------------------
META (owner META): per-floor stats, permanent upgrades, unlocks.

Per-floor stats: the game counts the human player's floor in dsStat_t slots
(G_DS_Stat / G_DS_BossDefeated in g_roguelike.c) and writes them at floor end into
rearmed_run.txt: `fst` (this floor, csv in dsStat_t order), `st` (this run, csv) and
`fseq` (+1 every finished floor). The UI merges each finished floor once (`fmerged`)
into the lifetime counters `cnt_<ds_statKeys[i]>` of rearmed_meta.txt and checks
the unlocks there. Weapon breakdown keys (game, floor end): `dmg`, `wkills`, `wtime`
(DS_NUM_WEAPONS csv each: damage dealt, kills, msec held).
-------------------------------------------------------------------------------
*/
typedef enum {
	DSST_KILLS,			// enemies killed by the player
	DSST_HEADSHOTS,		// headshot kills
	DSST_GKILLS,		// gauntlet kills
	DSST_GIBS,			// enemies gibbed
	DSST_ZIPS,			// zip line rides started
	DSST_CHESTS,		// chests opened (LOOT)
	DSST_EVOS,			// evolutions gained (ARSENAL / LOOT)
	DSST_ELITES,		// elites killed
	DSST_BOSSES,		// act bosses defeated (ACTS: G_DS_BossDefeated)
	DSST_DEATHS,		// times the player died
	DSST_GEMS,			// gems collected (LOOT)
	DSST_DAMAGE,		// damage dealt to enemies
	DSST_TIME,			// msec played
	DSST_WON,			// floors won
	DSST_FLOOR,			// the floor number (fst: set, not added)
	DSST_BOSSID,		// fst: dsBoss_t + 1 of the boss beaten on this floor, 0 none (set)
	DSST_VACUUMS,		// vacuum pickups taken (LOOT)
	DSST_LEVELUPS,		// weapon levels gained (ARSENAL)
	DSST_STASHES,		// secret stashes found (ds-secrets, g_ds_secrets.c): meta cnt_stashes
	DSST_NUM
} dsStat_t;

extern const char *ds_statKeys[DSST_NUM];		// "kills" -> meta cnt_kills

// permanent upgrades (rearmed_meta.txt ranks, copied into the run file at run start)
// Economy pass 2 (ds-economy, 2026-10-02): 14 upgrades in 4 distinct tiers, I FLESH (the body),
// II MARROW (the cards), III IRON (the spoils), IV SOUL (death). RETIRED entries keep their enum
// slot and their old prices (maxRank 0, tier -1): they duplicated passives / skill perks; owned
// ranks are refunded on load (UI_DS_EconMigrate) and G_DS_UpRank returns 0 for them.
typedef enum {
	DSU_HEALTH,			// up_health		I	+5 max health
	DSU_SPEED,			// up_speed			I	+2% move speed
	DSU_DAMAGE,			// up_damage		I	+3% damage
	DSU_ARMOR,			// up_armor			RETIRED (= ARMORER skill perk)
	DSU_SKULLS,			// up_skulls		RETIRED (= GRAVE COIN passive; skull multipliers snowball)
	DSU_REROLL,			// up_reroll		II	+1 card reroll per descent (UI)
	DSU_MAGNET,			// up_magnet		RETIRED (= LODESTONE passive)
	DSU_GROWTH,			// up_growth		RETIRED (= HUNGER passive)
	DSU_LUCK,			// up_luck			RETIRED (= RABBIT'S FOOT passive)
	DSU_TEMPO,			// up_tempo			RETIRED (= HAIR TRIGGER passive, RAPID CYCLE ...)
	DSU_STARTCARD,		// up_startcard		II	free starting ability (UI)
	DSU_CARDS,			// up_cards			II	4 cards per offer (game)
	DSU_AREA,			// up_area			RETIRED (= BLAST CORE passive, BLAST RADIUS perk)
	DSU_IRONHIDE,		// up_ironhide		RETIRED (= IRON HIDE passive, IRON SKIN perk)
	DSU_TREASURE,		// up_treasure		III	+5% chance of one more chest reward (LOOT)
	DSU_LIFE,			// up_life			IV	+1 life (UI)
	DSU_REVIVAL,		// up_revival		IV	once per descent a lost floor costs no life (game, META)
	DSU_ARSENAL,		// up_arsenal		III	every weapon starts at level 2 (ARSENAL)
	DSU_FIRSTPICK,		// up_firstpick		II	the first card offer of every act shows 5 cards (LOOT offer)
	DSU_VACUUM,			// up_vacuum		RETIRED (+6% of a 1.5% drop chance: nobody could feel it)
	DSU_LASTBREATH,		// up_lastbreath	IV	+1 life on boss floors per rank (ACTS, G_DS_PlayerBossLives)
	DSU_QUARTER,		// up_quarter		III	the first floor of every act starts with a banked chest (LOOT)
	DSU_NUM
} dsUpgrade_t;
#define DS_UP_RETIRED(u)	( ds_upgrades[u].maxRank <= 0 )

#define DS_UP_TIERS		4

typedef struct {
	const char	*key;			// meta / run key
	const char	*name;
	const char	*desc;			// per rank
	int			tier;			// 0..DS_UP_TIERS-1
	int			maxRank;
	int			cost[5];
	float		perRank;		// the effect per rank (fraction or amount, see the comment above)
} dsUpgradeInfo_t;

extern const dsUpgradeInfo_t	ds_upgrades[DSU_NUM];
extern const char				*ds_upTierNames[DS_UP_TIERS];
extern const char				*ds_upTierDescs[DS_UP_TIERS];	// "the body" ...
extern const int				ds_upTierNeed[DS_UP_TIERS];	// ranks bought in the tier above to open this one

// unlocks: meta `chars` (bit per dsCharacter_t, RANGER always) and `secrets` (bit per dsSecret_t)
extern const char *ds_secretHints[RLS_NUM];		// vague, shown while locked
extern const char *ds_secretConds[RLS_NUM];		// the condition, shown once found
extern const char *ds_orbbHint;

/*
-------------------------------------------------------------------------------
ECONOMY (round 2, ds-economy: game/g_ds_econ.c, q3_ui/ui_ds_econ.c)

Difficulty can change between floors (floor map selector). Lives stay as set at
run start. Run keys: `difficulty` (current, 1..5), `diff_start` (at run start),
`diff_min` / `diff_max` (lowest / highest difficulty a floor was PLAYED on; set when
a floor launches). Records (best_floor_<d>, won_<d>, history) go to diff_min: a
mixed run counts as the easiest difficulty used.
Reward multiplier by difficulty: skulls, gem xp, skill xp (G_DS_RewardMult in game).
-------------------------------------------------------------------------------
*/
extern const float ds_rewardMult[6];				// index 1..5 (0 unused = 1.0)
float BG_DS_RewardMult( int difficulty );			// 1.0 outside 1..5

// skull rewards (before the difficulty multiplier, GRAVE ROBBER / GRAVE COIN)
#define DS_SK_KILLS_PER		3		// a skull per 3 kills on a floor (won or lost) ...
#define DS_SK_KILLS_MAX		4		// ... at most 4
#define DS_SK_BOSS_BAR		24		// boss floor: the whole boss bar (10 lives) is worth 24 + 6 x act,
#define DS_SK_BOSS_BAR_ACT	6		// paid by the share of the boss's lives YOU took (won or lost)
#define DS_SK_BOSS_TRY		2		// lost boss floor: + 2 for any damage at all
// (pass 2: the separate boss kill bonus is gone: the boss floor's x3 and the final blow pay for it)

/*
-------------------------------------------------------------------------------
PERKS IN DESCENT follow the single-digit rule (normal modes keep their numbers).
Applied in g_style.c / g_rearmed.c / g_moves.c / bg_pmove.c; DESCENT card texts in
bg_rlCardDescs (bg_misc.c), normal perk offers use bg_perkDescs.
pmove tells DESCENT by STAT_DSFIRE & DSF_ON (set for every client in a run).
-------------------------------------------------------------------------------
*/
#define DSP_THICKSKIN_HP		8			// THICK SKIN: +8 max health (normal 25)
#define DSP_HEAVYCALIBER		1.06f		// HEAVY CALIBER: +6% damage (normal 15%)
#define DSP_QUICKHANDS_PCT		94			// QUICK HANDS: refire 94% = 6% faster (normal 80%)
#define DSP_OVERCHARGER_PCT		9			// OVERCHARGER: alt-fire charges 9% faster (normal 40%)
#define DSP_BLOODTHIRST_PCT		5			// BLOODTHIRST: 5% of damage dealt heals (normal 15%)
#define DSP_HEADHUNTER			1.09f		// HEADHUNTER: +9% headshot damage (normal x2 instead of x1.5)
#define DSP_SKYHUNTER			1.08f		// SKY HUNTER: +8% vs airborne (normal 30%)
#define DSP_GLASS_DMG			1.09f		// GLASS CANNON: +9% damage ...
#define DSP_GLASS_HP			9			// ... -9 max health (normal 35 / 35)
#define DSP_LASTSTAND_DEALT		1.09f		// LAST STAND under 30 health: +9% dealt ...
#define DSP_LASTSTAND_TAKEN		0.91f		// ... -9% taken (normal +50% / -25%)
#define DSP_KINETIC_MAX			0.08f		// KINETIC: up to +8% (normal 50%)
#define DSP_THORNS_PCT			8			// THORNS: 8% hits back (normal 30%)
#define DSP_IRONCLAD_ARMOR		25			// IRONCLAD: armor regenerates up to 25 (normal 50)
#define DSP_ADRENALINE_HEAL		5			// ADRENALINE: kills heal 5 (normal 10)
#define DSP_GROUNDZERO			1.09f		// GROUND ZERO: +9% shockwave (normal 1.5x)
#define DS_PM_ON(ps)			( ( (ps)->stats[STAT_DSFIRE] & DSF_ON ) != 0 )

/*
-------------------------------------------------------------------------------
ARSENAL (owner ARSENAL): network bits of weapon levels and evolutions.
-------------------------------------------------------------------------------
Fire rate: in DESCENT the server writes the held weapon's refire scale into
ps->stats[STAT_DSFIRE] before every Pmove (STAT_GGLEVEL, only used by gun game),
so client prediction runs the same refire times as the server.
*/
#define STAT_DSFIRE			STAT_GGLEVEL
#define DSF_ON				0x4000		// the stat holds DESCENT fire modifiers
#define DSF_RATE			0x00ff		// refire time, percent of normal (1..255)
#define DSF_SPINFAST		0x0100		// chaingun spins up 9% faster
#define DSF_NOSPINDOWN		0x0200		// chaingun keeps its spin
#define DSF_NOSPINUP		0x0400		// chaingun is at full speed at once (RIPSAW)
#define DS_FIRETIME(ps, t)	( ( (ps)->stats[STAT_DSFIRE] & DSF_ON ) ? (t) * ( (ps)->stats[STAT_DSFIRE] & DSF_RATE ) / 100 : (t) )

// playerState / entityState generic1 bit of a player: the held weapon is evolved (weapon glow)
#define RM_BODY_EVOLVED		128

// entityState->generic1 bits of ET_MISSILE (ALTFIRE_* use 1/2/4, RM_MISSILE_METEOR 8)
#define DSM_EVOLVED			16			// fired by an evolved weapon: s->weapon tells which evolution
#define DSM_SPECIAL			32			// level-8 special (growing plasma, bomb-carrying rocket, ...)
#define DSM_SMALL			64			// a child projectile (bomblet, split orb)
#define DSM_UNION			128			// ARMAGEDDON (with DSM_EVOLVED on a rocket)

// EV_SHOTGUN generic1: extra pellets (0..15) | spread step << 4 (0 normal, 1 -20%, 2 -40%, 3 -60%) | evolved
#define DSSG_PELLETS(g)		( (g) & 15 )
#define DSSG_SPREAD(g)		( DEFAULT_SHOTGUN_SPREAD * ( 10 - 2 * ( ( (g) >> 4 ) & 3 ) ) / 10 )
#define DSSG_EVOLVED		64
#define DSSG_FIRE			128			// level 8: incendiary pellets

// EV_DS_FX: eventParm = DSX_ kind, origin / origin2 = from / to, otherEntityNum = shooter,
// generic1 = extra (kind specific), weapon = weapon_t
typedef enum {
	DSX_NONE,
	DSX_LEVELUP,		// (local) unused as an event: levels arrive by server command "dslvl"
	DSX_TRACER,			// evolved bullets: muzzle -> origin2 tracer; generic1 = evolution
	DSX_TWIN,			// BULLET HELL twin tracer: origin = end 1, origin2 = end 2
	DSX_BLAST,			// small arsenal explosion at origin: generic1 = radius / 4, weapon = colour family
	DSX_ARC,			// level-8 / STORM CALLER fork: origin -> origin2; generic1 1 = evolved
	DSX_CLAW,			// HEARTRIPPER slash at origin, origin2 = direction point
	DSX_GOREBURST,		// gauntlet level 8 / HEARTRIPPER kill burst at origin
	DSX_JUDGEMENT,		// JUDGEMENT path: origin -> origin2 holy beam + blasts
	DSX_RICOCHET,		// IRON MAIDEN nail redirect spark at origin
	DSX_BLACKHOLE,		// DOOMSDAY collapse at origin
	DSX_EVOLVE,			// someone's weapon just evolved (burst of light at the player)
	DSX_NUM
} dsArsenalFx_t;

/*
-------------------------------------------------------------------------------
FRENZY (round 3, ds-frenzy: game/g_ds_frenzy.c, cgame/cg_ds_frenzy.c): kill-chain frenzy,
lucky crits, power spikes (BERSERK / OVERDRIVE), loot beams. Switches g_dsFrenzy, g_dsCrits,
g_dsSpikes, g_dsBeams (F10 DESCENT tab). Temporary bursts only: the single-digit rule bends
for a few seconds, never for anything permanent; G_DS_PowerScore never counts them.
-------------------------------------------------------------------------------
*/
#define RM_ENT_BEAM			74		// loot beam: modelindex2 = DS_BEAM_ rarity, otherEntityNum = the entity it follows (ENTITYNUM_NONE: standalone), time = birth
#define RM_ENT_SPIKE		75		// power spike pickup: modelindex2 = DS_SPIKE_ kind

#define DS_BEAM_COMMON		0		// white
#define DS_BEAM_RARE		1		// blue
#define DS_BEAM_EPIC		2		// purple
#define DS_BEAM_LEGENDARY	3		// orange / gold
#define DS_BEAM_NUM			4

#define DS_SPIKE_NONE		0
#define DS_SPIKE_BERSERK	1		// +damage, fire rate, lifesteal (frenzy top tier)
#define DS_SPIKE_OVERDRIVE	2		// speed, a dash that's always ready, zip refresh (rare pickup)
#define DS_SPIKE_NUM		3

#define DS_FRENZY_TIERS		4		// chain tiers I..IV at ds_frenzyTierKills kills
extern const int	ds_frenzyTierKills[DS_FRENZY_TIERS + 1];	// 0, 3, 6, 10, 15
extern const char	*ds_frenzyTierNames[DS_FRENZY_TIERS + 1];	// "", "BLOODLUST", "RAMPAGE", "UNSTOPPABLE", "MAYHEM" (IV starts BERSERK)

// EV_DS_FRENZY eventParm (origin = where, otherEntityNum = attacker / owner, generic1 = extra)
#define DSFZ_CRIT			1		// a lucky crit at origin: generic1 1 = headshot crit, 2 = the attacker is the viewer's target (unused)
#define DSFZ_SPIKE			2		// a power spike started on otherEntityNum: generic1 = kind (third person flash)

/*
-------------------------------------------------------------------------------
SECRETS (round 3, ds-secrets: game/g_ds_secrets.c, cgame/cg_ds_secrets.c): secret stashes,
1-3 per fight / elite / event floor at hidden spots found from the AAS data at map load
(cache rearmed_secrets_<map>.txt). Switch g_dsSecrets. No HUD marker, a faint hum nearby.
-------------------------------------------------------------------------------
*/
#define RM_ENT_STASH		77		// secret stash: modelindex2 = dsStash_t, frame = DSF_ flags, time = opened at (0 closed), angles = facing
#define DSF_DOWSE			1		// the player holds DOWSING ROD: the louder hum
#define CS_DSSECRETS		739		// \n\<stashes this floor>\f\<found>\t\<type last found>\ft\<level time of the last find>
									// \dw\<1 DOWSING ROD>\p\<x,y,z,found;...> (positions only with DOWSING ROD)

typedef enum {
	DSS_GEMS,			// a small gem cache
	DSS_GOLD,			// a cache of red (25) gems and a few skulls
	DSS_CHEST,			// an elite chest
	DSS_LEGEND,			// a legendary card owed in the next offer (ds-cards)
	DSS_OVERDRIVE,		// an OVERDRIVE power spike (ds-frenzy)
	DSS_NUM
} dsStash_t;

extern const char *ds_stashNames[DSS_NUM];		// "GEM CACHE"

/*
-------------------------------------------------------------------------------
CARDS (round 3, ds-cards: game/g_ds_cards.c, cgame/cg_ds_cards.c): the card rarity
reveal, LEGENDARY cards, the synergy ding, the floor-end tally. Switch cg_dsReveal
(0 instant / 1 full show). Legendaries are the player's only (never in bot kits),
at most one per offer, effects in DESCENT only; numbers single digit or mechanical.
-------------------------------------------------------------------------------
*/
typedef enum {
	LG_CHAINSOUL,		// CHAIN LIGHTNING SOUL: every kill arcs lightning into the nearest enemy; a killing arc arcs on
	LG_ECHO,			// ECHO: every 5th shot echoes, a free copy fires a moment later
	LG_VAMPIRE,			// VAMPIRE LORD: overkill damage becomes armor (up to max health)
	LG_TIMETHIEF,		// TIME THIEF: kills add time to power spikes and powerups
	LG_BLOODENGINE,		// BLOOD ENGINE: a gib refunds a dash charge and a few shots
	LG_MIRROR,			// MIRROR: the first hit each life is thrown back at the attacker
	LG_PHOENIX,			// PHOENIX: once a floor a killing blow revives you in a fire nova
	LG_MAGNETAR,		// MAGNETAR: gems orbit you and burn enemies they touch
	LG_DOUBLEAGENT,		// DOUBLE AGENT: the next enemy you hit fights for you for a few seconds
	LG_GRAVEDIGGER,		// GRAVEDIGGER: corpses you walk over give up a gem
	LG_BLACKMASS,		// BLACK MASS: every 9th kill pulls every gem on the map to you
	LG_DEADEYE,			// DEADEYE: every 5th hit you land is a headshot
	LG_NUM
} dsLegend_t;

// EV_DS_CARDS eventParm (origin / origin2 = from / to, otherEntityNum = the player, generic1 = extra)
#define LGX_ECHO			1		// an echo shot at origin (the muzzle)
#define LGX_VAMPIRE			2		// overkill mist: origin victim -> origin2 player, generic1 = armor gained
#define LGX_TIME			3		// TIME THIEF: generic1 = msec added
#define LGX_BLOODENGINE		4		// a gib refunded a dash charge: origin = the gib
#define LGX_MIRROR			5		// MIRROR shattered: origin = the player, origin2 = the attacker
#define LGX_PHOENIX			6		// PHOENIX rose: origin = the player, generic1 = nova radius
#define LGX_ORBHIT			7		// a MAGNETAR orb burned someone at origin
#define LGX_AGENT			8		// DOUBLE AGENT turned otherEntityNum2 (generic1 1) / it came back (0)
#define LGX_GRAVE			9		// GRAVEDIGGER: a grave gave up a gem at origin (generic1 1 = a rich grave)
#define LGX_BLACKMASS		10		// BLACK MASS: the pull started at origin
#define LGX_DEADEYE			11		// DEADEYE headshot at origin
#define LGX_CHAIN			12		// a CHAIN LIGHTNING SOUL arc: origin -> origin2, generic1 = link

#include "bg_ds_skills.h"	// SKILLS: skill trees, perks, the xp curve
#include "bg_ds_surprise.h"	// SURPRISES (ds-surprise): ambushes, floor modifiers, wild events, ghosts
#include "bg_ds_enemy.h"	// ENEMIES (round 4): archetypes, affixes, CS_DSENEMY
#include "bg_ds_maps.h"		// MAPS (ds-maps): the map table, floor variants (lighting / item layout)

#endif // BG_DESCENT_H
