/*
===========================================================================
REARMED -- round 4 ENEMY ARCHETYPES (en-types), shared by game and cgame.
The archetype ids are dsArchetype_t (DSAR_*) in bg_ds_enemy.h; this is the per-type table
(names, colours, models, weapons, the power budget) and the effect entity they use.
Game: g_ds_types.c (plan, kits, hooks), ai_types.c (behaviour via en-brains' hooks).
cgame: cg_ds_types.c (looks, telegraphs, sounds, names at the screen edges).
Design: docs/DESCENT_V2.md "Round 4". Switch g_dsTypes.
===========================================================================
*/
#ifndef BG_DS_TYPES_H
#define BG_DS_TYPES_H

/*
POWER BUDGET: a type changes HOW a bot fights, not HOW MUCH. Each row trades health / damage /
speed against what the type does, so a typed bot is worth about one plain bot (hp x dmg ~ 0.7-1.2
before the type's own trick). The floor's tier, the bot kit and G_DS_PowerScore scaling still
apply on top (G_RL_MaxHealthBonus / G_RL_DamageScale).
*/
typedef struct {
	const char	*name;			// "SNIPER" (callout, scoreboard)
	const char	*color;			// ^ colour code for the scoreboard tag
	float		rgb[3];			// light / beam / name colour
	const char	*model;			// forced bot model ("" = keep, "*" = the player's own: the mimic)
	int			weapon;			// main weapon (WP_NONE = keep), given at spawn and preferred
	int			weapon2;		// backup weapon (WP_NONE none)
	float		health;			// max health multiplier
	float		damage;			// damage dealt multiplier
	float		speed;			// move speed multiplier
	int			armor;			// extra spawn armour
	int			minAct;			// first act it shows up in
	float		range;			// preferred fight distance (BotTypeRange), 0 = no opinion
} dsTypeInfo_t;

extern const dsTypeInfo_t	ds_types[DSAR_NUM];

// timings (msec) and sizes (units)
#define DST_SNIPE_WINDUP		700		// laser glint before a rail shot
#define DST_SNIPE_RELOCATE		3500	// moves to another perch after a shot
#define DST_KAMI_FUSE			1000	// beeping + glow before the blast
#define DST_KAMI_TRIGGER		170		// fuse starts this close to the player
#define DST_KAMI_DAMAGE			85		// blast at the centre (the bot's damage scale applies)
#define DST_KAMI_RADIUS			250
#define DST_KAMI_POP_DAMAGE		60		// killed before it blew: pops where it stands
#define DST_KAMI_POP_RADIUS		200
#define DST_BLINK_MINDIST		400		// never ends a blink closer than this to the player
#define DST_BLINK_SAFE			480		// what the spot search asks for (margin)
#define DST_BLINK_MIN			4500	// msec between blinks (random up to + 2500)
#define DST_HEAL_RANGE			650
#define DST_HEAL_PER_SEC		12		// medic beam
#define DST_SHIELD_UP_MIN		2000	// shield up, then lowered to fire
#define DST_SHIELD_UP_MAX		3000
#define DST_SHIELD_DOWN_MIN		1000
#define DST_SHIELD_DOWN_MAX		1400
#define DST_SHIELD_BLOCK		0.2f	// share of a frontal hit that gets through
#define DST_SHIELD_COS			0.5f	// front cone: +-60 degrees
#define DST_RITUAL_TIME			1500	// summoner raise ritual
#define DST_RAISE_COOLDOWN		9000
#define DST_RAISE_MAX_EACH		2		// skeletons alive per summoner
#define DST_RAISE_MAX_ALL		3		// and on the floor
#define DST_JUGG_SCALE			1.25f

/*
RM_ENT_DSTYPE (ET_GENERAL, generic1): type effects. modelindex2 = DSTFX_ kind,
otherEntityNum = the bot, otherEntityNum2 = the target entity (ENTITYNUM_NONE none),
time = start, time2 = end, origin = where, origin2 = to.
*/
#define RM_ENT_DSTYPE		86
#define DSTFX_HEAL			1		// medic beam: bot -> otherEntityNum2 (follows both), loops a hum
#define DSTFX_RITUAL		2		// summoner raise: a pillar + ring on the corpse at origin until time2
#define DSTFX_BLINK_OUT		3		// blinker left from origin
#define DSTFX_BLINK_IN		4		// blinker arrived at origin
#define DSTFX_BOOM			5		// kamikaze blast at origin (frame = radius / 4)
#define DSTFX_BLOCK			6		// shield stopped a hit at origin

/*
TYPE ADAPTATION (en-tune, switch g_dsTypeAdapt): the archetypes that killed the player most
remember it in the next run. Deaths by killer archetype go into the run file at floor end
(run key tad_f, csv per DSAR_), the UI folds them into the meta file once per floor
(tad_cur = this run so far, tad_seed = its run seed, tad_w = the weights x10). At a new run's
first merge: tad_w = tad_w * DST_ADAPT_DECAY + tad_cur. Each floor the game computes the same
weights (BG_DS_TypeAdaptRoll on a seed change) and adapts the top DST_ADAPT_MAX types whose
weight is at least DST_ADAPT_MIN. An adapted type pays DST_ADAPT_HP of its health for its
trick (power budget) and is picked DST_ADAPT_PICK times as often. Daily runs and boss
practice neither count nor adapt. Published to cgame in serverinfo g_dsTypeAdaptNow
"<floor>:<arch>,<arch>" (the run-start hint, the sniper's shorter laser ramp).
*/
#define DST_ADAPT_MIN			3.0f	// weighted deaths before a type adapts (about 3 in one run)
#define DST_ADAPT_DECAY			0.5f	// per later run (8 deaths fade below the bar after 2 clean runs)
#define DST_ADAPT_MAX			2
#define DST_ADAPT_HP			0.95f	// health of an adapted type (pays for the trick)
#define DST_ADAPT_PICK			1.5f	// pick weight of an adapted type
#define DST_SNIPE_WINDUP_ADAPT	560		// sniper: glint 0.7 -> 0.56 s
#define DST_SNIPE_IDLE			8000	// sniper: a perch with nothing to shoot is left after this
#define DST_SNIPE_IDLE_ADAPT	5000
#define DST_RUSH_SIDE_ADAPT		220		// rusher: charges at a point this far to the target's side
#define DST_SHIELD_COS_ADAPT	0.34f	// shield: front cone +-70 degrees
#define DST_BLINK_MIN_ADAPT		3600	// blinker: blinks sooner
#define DST_RAISE_COOLDOWN_ADAPT	7000	// summoner
#define DST_MEDIC_BACK			170		// medic: stands this far behind its patient
#define DST_MEDIC_BACK_ADAPT	260
#define DST_JUGG_ROCKET			850		// juggernaut: rockets beyond this range
#define DST_JUGG_ROCKET_ADAPT	600

void BG_DS_TypeAdaptRoll( float *w, const int *runDeaths );		// w[DSAR_NUM] = w * decay + deaths
int BG_DS_TypeAdaptPick( const float *w, int *out );			// the adapted archs (0..DST_ADAPT_MAX), heaviest first
const char *BG_DS_TypeAdaptHint( int arch );					// "their glint is shorter. Strafe early."
const char *BG_DS_TypePlural( int arch );						// "Snipers"
int BG_DS_TypeAdaptParse( const char *s, int *floorOut, int *list, int max, int *numOut );	// g_dsTypeAdaptNow -> arch bit mask (+ ordered list)

const char *BG_DS_TypeName( int arch );		// "SNIPER" ("" for DSAR_NONE / out of range)
int BG_DS_TypeByName( const char *s );		// "sniper" / "1" -> DSAR_SNIPER, -1 unknown, 0 "none"

#endif // BG_DS_TYPES_H
