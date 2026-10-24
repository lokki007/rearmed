/*
===========================================================================
REARMED -- DESCENT round 3 SURPRISES (ds-surprise): mid-floor ambushes, pick-your-poison
floor modifiers, rare wild events, the ghost of a past run. Shared by game, cgame and ui
(tables in bg_ds_surprise.c). Included at the end of bg_descent.h.
Game: game/g_ds_surprise.c. cgame: cgame/cg_ds_surprise.c. ui: q3_ui/ui_ds_surprise.c.
===========================================================================
*/
#ifndef BG_DS_SURPRISE_H
#define BG_DS_SURPRISE_H

/*
-------------------------------------------------------------------------------
FLOOR MODIFIERS: offered on some fight / elite nodes of the floor map, toggled by the
player before launching (run key node_mod). The enemy side may go past single digits,
the player side never does; the reward is shown with its number.
-------------------------------------------------------------------------------
*/
typedef enum {
	DSMOD_NONE,
	DSMOD_ARMOURED,		// enemies spawn with +50 armor			+25% skulls
	DSMOD_BLOODLUST,	// enemies deal +20% damage				+1 elite chest (won floor)
	DSMOD_BLACKOUT,		// the lights go down, gems glow			+30% gem xp
	DSMOD_SWARM,		// +2 bots								+40% gems
	DSMOD_GLASS,		// everyone has half health				+25% skulls
	DSMOD_FERAL,		// enemies move 15% faster				+1 card to choose from
	DSMOD_NUM
} dsModifier_t;

typedef struct {
	const char	*name;			// "ARMOURED"
	const char	*key;			// dev / run key name: "armoured"
	const char	*risk;			// "Enemies spawn with +50 armor."
	const char	*reward;		// "+25% skulls"
	int			skullPct;		// won floor: + this % of the floor's skulls
	int			gemXpPct;		// gem xp + %
	int			gemPct;			// gems dropped + %
	int			chests;			// elite chests banked on a won floor
	int			cards;			// cards added to the floor's offer
	int			enemyDmgPct;	// enemies (bots) deal + % to the player
	int			enemyArmor;		// enemies spawn with this much armor
	int			enemySpeedPct;	// enemies move + %
	int			extraBots;		// bots added to the floor (UI: node_bots)
	int			healthPct;		// everyone's max health x % (100 = unchanged)
} dsModInfo_t;

extern const dsModInfo_t ds_modifiers[DSMOD_NUM];
int BG_DS_ModByKey( const char *key );		// "armoured" / "ARMOURED" / "1" -> DSMOD_, 0 none

/*
-------------------------------------------------------------------------------
MID-FLOOR AMBUSH: a mini-boss crashes in far from the player, after a warning.
Two of three strong sides like the act bosses (speed, health, damage), one life,
one or two signature attacks borrowed from the bosses (DSB_ telegraphs).
-------------------------------------------------------------------------------
*/
#define DS_MINIBOSS_NUM		5

typedef struct {
	const char	*name;			// "THE HEADSMAN"
	const char	*netName;		// the bot's name in game ("Headsman")
	const char	*botFile;		// the bot it plays as ("Gorre")
	float		speed, health, damage;	// traits (> 1 = a strong side)
	int			attacks[2];		// DSB_ attacks it casts (DSB_NONE = none)
	float		rgb[3];			// its colour (pillar, banner)
} dsMiniBoss_t;

extern const dsMiniBoss_t ds_miniBosses[DS_MINIBOSS_NUM];

/*
-------------------------------------------------------------------------------
WILD EVENTS: rare (about 1 floor in 6), announced at the side.
-------------------------------------------------------------------------------
*/
typedef enum {
	DSEV_NONE,
	DSEV_GOLDMETEOR,	// a golden meteor crashes somewhere: the crater holds gems and a chest
	DSEV_UPRISING,		// 30 s of weak bots pouring in from the spawn points: chain fuel
	DSEV_ECLIPSE,		// 25 s: every kill drops double gems
	DSEV_BOUNTY,		// the leading bot carries a chest for 40 s: kill it to take it
	DSEV_NUM
} dsWildEvent_t;

extern const char *ds_wildEventNames[DSEV_NUM];		// "GOLD METEOR"
extern const char *ds_wildEventKeys[DSEV_NUM];		// "goldmeteor"
extern const char *ds_wildEventHints[DSEV_NUM];		// one line

#define DS_UPRISING_TIME	30000
#define DS_ECLIPSE_TIME		25000
#define DS_BOUNTY_TIME		40000
#define DS_SURPRISE_MIN_DIST	600		// nothing appears closer to the player than this without a warning

/*
-------------------------------------------------------------------------------
NETWORK
-------------------------------------------------------------------------------
CS_DSSURPRISE (740): info string, "" outside a run
	\mod\<dsModifier_t of this floor>
	\mb\<mini-boss client, -1>\mbk\<archetype>\mbh\<its health %>
	\ev\<dsWildEvent_t running, 0>\evs\<start level time>\eve\<end level time, 0 open>
	\evc\<bounty target client / gold meteor impact time>
	\gh\<ghost client, -1>
Server command "dsuann <kind> <a> <b>" (side callouts; DSUC_ kinds below).
RM_ENT_SURPRISE (80) ET_GENERAL: modelindex2 = DSX2_ kind, time = start, time2 = landing,
	frame = DSXS_ style, origin2 = the sky start of a gold meteor.
*/
#define CS_DSSURPRISE		740
#define RM_ENT_SURPRISE		80

#define DSX2_PILLAR			0		// a landing warning: a beam of light where something arrives
#define DSX2_GOLDMETEOR		1		// the golden meteor falling (frame 1 = the crater after impact)

#define DSXS_AMBUSH			0		// red / the mini-boss's colour (otherEntityNum2 = archetype)
#define DSXS_GHOST			1		// pale blue-white
#define DSXS_FODDER			2		// a small purple portal
#define DSXS_ARRIVAL		3		// (ds-arrivals) a floor bot joining: warm white-orange, short

typedef enum {
	DSUC_NONE,
	DSUC_AMBUSH_WARN,		// a = archetype: AMBUSH: THE HEADSMAN (incoming)
	DSUC_AMBUSH_LAND,		// a = archetype, b = client
	DSUC_AMBUSH_SLAIN,		// a = archetype, b = skulls (you killed it)
	DSUC_AMBUSH_LOST,		// a = archetype: someone else killed it
	DSUC_EVENT_START,		// a = dsWildEvent_t
	DSUC_EVENT_END,			// a = dsWildEvent_t
	DSUC_GHOST_WARN,			// a = character
	DSUC_GHOST_SLAIN,		// b = skulls: YOU SURPASSED YOURSELF
	DSUC_GHOST_FADED,		// someone else got it
	DSUC_GOLD_SNATCHED,		// a = the bot that took the gold chest
	DSUC_BOUNTY_CLAIMED,		// a = target, b = skulls
	DSUC_NUM
} dsSurpriseCallout_t;

#endif // BG_DS_SURPRISE_H
