/*
===========================================================================
REARMED round 4 (en-brains): smarter bots in every mode.
ai_tactics.c (team play: flank, retreat to heal, item timing, sound hunting, leader focus,
barrel lures, panic / crawl / grudge movement), ai_tactics_move.c (usercmd level: dodges,
rail jukes, rocket jumps, strafe jumps), ai_habits.c (learns the human's habits).
Archetype hooks BotType* are implemented by en-types in ai_types.c.
Include after ai_main.h.
===========================================================================
*/
#ifndef AI_TACTICS_H
#define AI_TACTICS_H

// tactics (BotTypeTactic multipliers, g_botTactics bit per tactic, stats)
#define BTAC_FLANK		0
#define BTAC_RETREAT	1
#define BTAC_DODGE		2
#define BTAC_RJUMP		3		// rocket jumps + strafe jumps
#define BTAC_ITEMS		4		// powerup / armour timing
#define BTAC_SOUND		5
#define BTAC_HABITS		6
#define BTAC_LEADER		7
#define BTAC_LURE		8
#define BTAC_RANGE		9		// keep the preferred range
#define BTAC_NUM		10

// BotTypeThink return mask
#define BTT_OWNMOVE		1		// the archetype steers this frame: no tactic movement overrides
#define BTT_NOTACTICS	2		// skip every tactic this frame

/*
--- archetype hooks: en-types, game/ai_types.c ---------------------------------------------------
*/
int		BotTypeThink( bot_state_t *bs );						// every BotDeathmatchAI, after the AI node ran
int		BotTypeChooseWeapon( bot_state_t *bs, int weapon );	// end of BotChooseWeapon: return weapon = no opinion
float	BotTypeTactic( bot_state_t *bs, int tactic );			// 0..1 multiplier per BTAC_
float	BotTypeRange( bot_state_t *bs );						// preferred fight distance, 0 = no opinion
void	BotTypeInput( bot_state_t *bs, usercmd_t *ucmd );		// last word on the usercmd (BotUpdateInput)
qboolean BotTypeGoal( bot_state_t *bs, bot_goal_t *goal, int retreat );	// top of BotGetLongTermGoal; qtrue = use goal

/*
--- en-brains entry points (the hooks in the shared ai files) ------------------------------------
*/
void	BotTacticsFrame( void );								// BotAIStartFrame, once a server frame
void	BotTacticsThink( bot_state_t *bs );						// BotDeathmatchAI, after the node
void	BotTacticsInput( bot_state_t *bs, usercmd_t *ucmd );	// BotUpdateInput, after BotRearmInput
int		BotTacticsChooseWeapon( bot_state_t *bs, int weapon );	// BotChooseWeapon (habits counter + type)
qboolean BotTacticsGoal( bot_state_t *bs, bot_goal_t *goal, int retreat );	// BotGetLongTermGoal
void	BotTacticsChaseGoal( bot_state_t *bs, bot_goal_t *goal );	// AINode_Battle_Chase: flank route
qboolean BotTacticsAttackMove( bot_state_t *bs, int tfl, bot_moveresult_t *moveresult );	// top of BotAttackMove
int		BotTacticsWantsRetreat( bot_state_t *bs );				// -1 no opinion, else 0 / 1
int		BotTacticsWantsChase( bot_state_t *bs );				// -1 no opinion, else 0 / 1
float	BotTacticsEnemyScale( bot_state_t *bs, int client );	// BotFindEnemy: multiplier on the squared distance (<1 = preferred)
int		BotTacticsRocketJump( bot_state_t *bs );				// BotCanAndWantsToRocketJump: -1 stock, 0 / 1
// (BotTacticsCommand, the btstats / btreset ConsoleCommand hook, is declared in g_descent.h)

// evidence counters (btstats, "BtStats:" lines in games.log)
typedef enum {
	BTC_FLANK, BTC_FLANK_REACHED, BTC_RETREAT, BTC_HEALED, BTC_COMEBACK,
	BTC_DODGE, BTC_JUKE, BTC_RJUMP, BTC_RJUMP_CHASE, BTC_STRAFEHOP,
	BTC_CONTEST, BTC_CONTEST_GOT, BTC_HEAR, BTC_HEAR_REACHED, BTC_TRACK,
	BTC_HABIT_WEAPON, BTC_RANGE, BTC_PREAIM, BTC_LEADER, BTC_GRUDGE, BTC_GRUDGE_HUNT,
	BTC_LURE, BTC_LURE_BLOWN, BTC_BARREL, BTC_PANIC, BTC_CRAWL,
	BTC_NUM
} btCounter_t;

// shared helpers
float	BotTacSkill( bot_state_t *bs );		// 0 (skill 1) .. 1 (skill 4-5)
float	BotTacGate( bot_state_t *bs );		// en-tune: share of the time a low-skill bot uses its tactics (0 skill 1 .. 1 skill 4)
qboolean BotTacOn( bot_state_t *bs, int tactic, float *scale );	// switch + archetype + skill: scale 0..1
void	BotTacCount( bot_state_t *bs, int counter );				// evidence counters (btstats)
int		BotTacDebug( void );											// bt_debug
int		BotTacOwnMove( bot_state_t *bs );								// BotTypeThink said BTT_OWNMOVE / BTT_NOTACTICS
qboolean BotTacStaggered( bot_state_t *bs );							// en-feel stagger: no dodges / jumps
qboolean BotTacCrawling( bot_state_t *bs );

// ai_tactics_move.c
void	BotTacMoveInput( bot_state_t *bs, usercmd_t *ucmd );

// ai_habits.c
void	BotHabitsInit( void );
void	BotHabitsFrame( void );
int		BotHabitsWeapon( bot_state_t *bs, int weapon );		// counter the human's favourite weapon
float	BotHabitsRange( bot_state_t *bs, int enemy );			// distance to keep from this enemy, 0 = none
qboolean BotHabitsHotSpot( bot_state_t *bs, vec3_t spot );		// a spot the human often comes from, near us
void	BotHabitsReport( void );
void	BotHabitsShutdown( void );

#endif
