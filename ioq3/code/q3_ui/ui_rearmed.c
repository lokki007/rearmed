/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
//
/*
=======================================================================

REARMED SETTINGS MENU

One menu with a row of tabs across the top. Every control is in the
same menuframework_s; switching tabs hides the other tabs' controls.
Cvars are read when the menu opens and written as soon as a control
changes.

=======================================================================
*/


#include "ui_local.h"


#define ART_FRAMEL				"menu/art/frame2_l"
#define ART_FRAMER				"menu/art/frame1_r"
#define ART_BACK0				"menu/art/back_0"
#define ART_BACK1				"menu/art/back_1"

#define REARMED_X_POS			360
#define REARMED_TAB_Y			58
#define REARMED_CENTER_Y		222
#define REARMED_SPACING			(BIGCHAR_HEIGHT+2)
#define REARMED_HELP_Y			372
#define REARMED_COLUMN_H		280		// most a tab's column may take (tabs end ~76, help starts 372)
#define REARMED_HELP_CHARS		72

#define ID_TAB0					10
#define ID_BACK					20
#define ID_APPLY				21
#define ID_ITEM0				100

// tabs
#define RMTAB_MOVEMENT			0
#define RMTAB_WEAPONS			1
#define RMTAB_RULES				2
#define RMTAB_HUD				3
#define RMTAB_CHAOS				4
#define RMTAB_DESCENT			5		// round 3 (ds-frenzy): the DESCENT-only switches
#define RMTAB_VIDEO				6
#define RMTAB_BOTS				7		// round 4: bot brains, personality, enemy types / affixes, boss mode
#define RMTAB_HITS				8		// round 4 (en-feel): flinch, stagger, dismemberment, crawl, morale, splats, cracks, screams
#define RM_NUM_TABS				9

// item flags
// RMF_SERVER: g_* and bot_* cvars, only matter on a server you host
// RMF_LATCH: latched, takes effect on the next map
// RMF_VIDRESTART: latched renderer cvar, needs vid_restart
#define RMF_SERVER				1
#define RMF_LATCH				2
#define RMF_VIDRESTART			4


// control types, picked from the table entry by Rearmed_ItemType
#define RMT_RADIO				0
#define RMT_SPIN				1
#define RMT_SLIDER				2


typedef struct {
	const char	*cvar;
	int			tab;
	const char	*label;
	const char	*help;
	const char	**names;		// spin control entries, NULL for a radio button or slider
	const float	*values;		// cvar value for each spin control entry
	float		defaultValue;	// shown when the cvar does not exist yet
	int			flags;
	// slider only (sliderStep > 0 makes the item a slider); the other items leave these out
	float		sliderMin;
	float		sliderMax;
	float		sliderStep;		// cvar change per slider notch
	const char	*sliderUnit;	// shown after the value readout, e.g. "%"
} rearmedItem_t;


static const char *rm_tabNames[RM_NUM_TABS] = {
	"MOVE",
	"WEAPONS",
	"RULES",
	"HUD",
	"CHAOS",
	"DESCENT",
	"VIDEO",
	"BOTS",
	"HITS"
};

// tab label order left to right; the centres are computed from the label widths (Rearmed_TabX)
static const int rm_tabOrder[RM_NUM_TABS] = { RMTAB_MOVEMENT, RMTAB_WEAPONS, RMTAB_RULES, RMTAB_HUD,
	RMTAB_CHAOS, RMTAB_HITS, RMTAB_BOTS, RMTAB_DESCENT, RMTAB_VIDEO };

static const char *rm_tabHelp[RM_NUM_TABS] = {
	"Movement tech: double jump, wall jump, wall run, dash, grapple, slide, ground pound and zip line.",
	"Alt-fire modes and new weapon behaviour.",
	"Game mode, mutators and match rules.",
	"HUD and feel: hit feedback, HUD layout, camera and tips.",
	"Physics props, corpse physics, slow motion, gore, kill cam and replays.",
	"DESCENT only: bosses, the nemesis, kill-chain frenzy, crits, power spikes, loot beams.",
	"Post-processing and lighting.",
	"Enemies: bot tactics and personality (all modes), enemy types, affixes and boss fights (DESCENT).",
	"Hit feel: flinch, stagger, heads and limbs blown off, crawling, panic, wall splats, armour cracks, death screams."
};

static const char *rm_offSomeAll_names[] = { "Off", "Some", "All", NULL };
static const float rm_offSomeAll_values[] = { 0, 1, 2 };

static const char *rm_taunts_names[] = { "Off", "Some", "Lots", NULL };		// en-personality: g_botTaunts
static const float rm_taunts_values[] = { 0, 1, 2 };
static const char *rm_grudges_names[] = { "Off", "40 seconds", "90 seconds", NULL };	// en-personality: g_botGrudges
static const float rm_grudges_values[] = { 0, 1, 2 };
static const char *rm_botMemory_names[] = { "Off", "On", "Talk only", NULL };		// en-memory: g_botMemory
static const float rm_botMemory_values[] = { 0, 1, 2 };

// en-screens: tabs drawn in two columns: how many of the tab's items (in rm_items order) go in the left
// column (0 = one centred column), the column x (item x: labels end left of it, values start right of it)
// and the headings over them
static const int rm_tabSplit[RM_NUM_TABS] = { 0, 0, 0, 0, 0, 0, 0, 9, 0 };
#define REARMED_COL1_X			204
#define REARMED_COL2_X			470
static const char *rm_colHeads[RM_NUM_TABS][2] = {
	{ NULL, NULL }, { NULL, NULL }, { NULL, NULL }, { NULL, NULL }, { NULL, NULL }, { NULL, NULL }, { NULL, NULL },
	{ "ALL MODES", "DESCENT" },
	{ NULL, NULL }
};

static const char *rm_mode_names[] = { "Normal", "Gun Game", "Juggernaut", "Instagib", "One in the Chamber", NULL };
static const float rm_mode_values[] = { 0, 1, 2, 3, 4 };

static const char *rm_fragMult_names[] = { "x1", "x2", "x3", NULL };		// ds-economy: g_fragMult
static const float rm_fragMult_values[] = { 1, 2, 3 };

static const char *rm_interval_names[] = { "30 seconds", "60 seconds", "90 seconds", "120 seconds", "180 seconds", NULL };
static const float rm_interval_values[] = { 30, 60, 90, 120, 180 };

static const char *rm_propCount_names[] = { "Auto", "Off", "Few", "Some", "Lots", NULL };
static const float rm_propCount_values[] = { -1, 0, 3, 8, 16 };

static const char *rm_display_names[] = { "Borderless", "Fullscreen", "Window", NULL };
static const float rm_display_values[] = { 2, 1, 0 };

static const char *rm_gore_names[] = { "Stock", "Light", "Heavy", "Absurd", NULL };
static const float rm_gore_values[] = { 0, 1, 2, 3 };

static const char *rm_offOnDouble_names[] = { "Off", "On", "Double", NULL };

static const char *rm_gorePersist_names[] = { "Fade", "Stay", NULL };
static const float rm_gorePersist_values[] = { 0, 1 };
static const float rm_offOnDouble_values[] = { 0, 1, 2 };

static const char *rm_offLowFull_names[] = { "Off", "Low", "Full", NULL };
static const float rm_offLowFull_values[] = { 0, 0.5f, 1 };
static const char *rm_offOnStrong_names[] = { "Off", "On", "Strong", NULL };		// ds-maps: cg_dsVariantLight
static const float rm_offOnStrong_values[] = { 0, 1, 2 };

static const char *rm_hudWide_names[] = { "4:3 box", "16:9 area", "Screen edges", NULL };
static const float rm_hudWide_values[] = { 0, 1, 2 };

static const char *rm_bossLives_names[] = { "3", "5", "7", "10", "15", "20", NULL };
static const float rm_bossLives_values[] = { 3, 5, 7, 10, 15, 20 };

static const char *rm_bossScale_names[] = { "Weaker (0.7)", "Softer (0.85)", "Normal", "Tougher (1.15)", "Brutal (1.3)", NULL };
static const float rm_bossScale_values[] = { 0.7f, 0.85f, 1.0f, 1.15f, 1.3f };
// round 4 (en-boss): boss floors timed 1v1 or 10 lives many vs 1, the clock
static const char *rm_bossMode_names[] = { "Timed 1v1", "10 lives, many vs 1", NULL };
static const float rm_bossMode_values[] = { 0, 1 };
static const char *rm_bossTime_names[] = { "3:00", "4:00", "5:00", "6:00", "7:00", "8:00", "10:00", NULL };
static const float rm_bossTime_values[] = { 180, 240, 300, 360, 420, 480, 600 };

static const char *rm_enemyTags_names[] = { "Off", "Minimal", "Full", NULL };
static const float rm_enemyTags_values[] = { 0, 1, 2 };

static const char *rm_killCam_names[] = { "Off", "Notable kills", "Every kill", NULL };
static const float rm_killCam_values[] = { 0, 1, 2 };

static const char *rm_killCamSide_names[] = { "Auto", "Left", "Right", NULL };
static const float rm_killCamSide_values[] = { 0, 1, 2 };

static const char *rm_bloomStrength_names[] ={ "Low", "Medium", "High", NULL };
static const float rm_bloomStrength_values[] = { 0.35f, 0.6f, 0.9f };

static const char *rm_sharpen_names[] = { "Off", "Low", "High", NULL };
static const float rm_sharpen_values[] = { 0, 0.3f, 0.6f };


static const rearmedItem_t rm_items[] = {
	// MOVEMENT
	{ "g_doubleJump", RMTAB_MOVEMENT, "Double jump:", "Jump again in mid-air.", NULL, NULL, 1, RMF_SERVER },
	{ "g_wallJump", RMTAB_MOVEMENT, "Wall jump:", "Jump off a wall you are touching in the air.", NULL, NULL, 1, RMF_SERVER },
	{ "g_wallRun", RMTAB_MOVEMENT, "Wall run:", "Keep jump held while you run fast along a wall; tap jump again to kick off.", NULL, NULL, 1, RMF_SERVER },
	{ "g_airDash", RMTAB_MOVEMENT, "Dash:", "Burst of speed in your move direction (Q / Shift).", NULL, NULL, 1, RMF_SERVER },
	{ "g_jumpAssist", RMTAB_MOVEMENT, "Jump assist:", "Coyote time and jump buffering: jumps feel forgiving.", NULL, NULL, 1, RMF_SERVER },
	{ "g_offhandHook", RMTAB_MOVEMENT, "Offhand grapple:", "Hold E to fire a grapple with any weapon.", NULL, NULL, 1, RMF_SERVER },
	{ "g_slide", RMTAB_MOVEMENT, "Slide:", "Crouch while running to slide; jump out of it to keep the speed.", NULL, NULL, 1, RMF_SERVER },
	{ "g_slam", RMTAB_MOVEMENT, "Ground pound:", "Crouch in the air to slam down; the shockwave launches enemies.", NULL, NULL, 1, RMF_SERVER },
	{ "g_zipLine", RMTAB_MOVEMENT, "Zip line:", "Aim at a wall and press the zip key (Z, Setup > Controls) to ride a cable there.", NULL, NULL, 1, RMF_SERVER },

	// WEAPONS
	{ "g_altFire", RMTAB_WEAPONS, "Alt-fire:", "Right mouse fires every weapon's alternate mode.", NULL, NULL, 1, RMF_SERVER },
	{ "g_chargedAlt", RMTAB_WEAPONS, "Charged alt-fire:", "Hold alt-fire to charge a stronger shot, tap it for the quick one.", NULL, NULL, 1, RMF_SERVER },
	{ "g_rearmItems", RMTAB_WEAPONS, "TA weapons on stock maps:", "Swap GL/plasma/shotgun spawns for nailgun, prox launcher, chaingun.", rm_offSomeAll_names, rm_offSomeAll_values, 1, RMF_SERVER|RMF_LATCH },
	{ "g_weaponRandom", RMTAB_WEAPONS, "Random weapons:", "Chance that each weapon or ammo spawn is a random weapon. Re-rolled every respawn.", NULL, NULL, 25, RMF_SERVER, 0, 100, 5, "%" },
	{ "g_shotgunKick", RMTAB_WEAPONS, "Shotgun recoil:", "Shotgun pushes you back; shoot the floor to shotgun-jump.", NULL, NULL, 1, RMF_SERVER },
	{ "g_chaingunSpin", RMTAB_WEAPONS, "Chaingun spin-up:", "Chaingun fire rate ramps up while you hold the trigger.", NULL, NULL, 1, RMF_SERVER },
	{ "g_ricochet", RMTAB_WEAPONS, "Ricochets:", "Nails and overcharged rails bounce off walls.", NULL, NULL, 1, RMF_SERVER },
	{ "g_stickyGrenades", RMTAB_WEAPONS, "Sticky grenades:", "Alt-fire grenades stick to walls and players; charged alt fires napalm.", NULL, NULL, 1, RMF_SERVER },
	{ "g_singularity", RMTAB_WEAPONS, "Plasma singularity:", "Plasma alt-fire makes a whirlpool that drags and soaks enemies.", NULL, NULL, 1, RMF_SERVER },
	{ "g_bfgLasers", RMTAB_WEAPONS, "BFG lasers:", "The BFG ball zaps nearby enemies as it flies.", NULL, NULL, 1, RMF_SERVER },
	{ "g_elements", RMTAB_WEAPONS, "Elements:", "Grenades burn, plasma soaks, lightning shocks, shotgun alt blows, rockets shake the ground.", NULL, NULL, 1, RMF_SERVER },
	{ "g_uppercut", RMTAB_WEAPONS, "Gauntlet uppercut:", "Gauntlet hits launch enemies; lunge then hit for a combo.", NULL, NULL, 1, RMF_SERVER },

	// RULES
	{ "g_rearmMode", RMTAB_RULES, "Game mode:", "Starts on the next map. Gun Game, Juggernaut and One in the Chamber are free-for-all only.", rm_mode_names, rm_mode_values, 0, RMF_SERVER },
	{ "g_fragMult", RMTAB_RULES, "Frag target:", "Multiplies the frags to win: skirmish, single player arenas, online. Not DESCENT.", rm_fragMult_names, rm_fragMult_values, 1, RMF_SERVER },
	{ "g_mutators", RMTAB_RULES, "Mutators:", "A random match-wide event every few minutes.", NULL, NULL, 1, RMF_SERVER },
	{ "g_mutatorInterval", RMTAB_RULES, "Mutator every:", "Time between mutators.", rm_interval_names, rm_interval_values, 60, RMF_SERVER },
	{ "g_headshots", RMTAB_RULES, "Headshots:", "Hitscan and nail headshots do 1.5x damage.", NULL, NULL, 1, RMF_SERVER },
	{ "g_airshots", RMTAB_RULES, "Airshots & juggles:", "Direct hits and explosions on airborne players knock them further.", NULL, NULL, 1, RMF_SERVER },
	{ "g_killstreaks", RMTAB_RULES, "Killstreak rewards:", "Powerups at 3/5/8/12 kills in a life.", NULL, NULL, 1, RMF_SERVER },
	{ "g_bounty", RMTAB_RULES, "Bounty:", "Players on a 5+ streak carry a bounty; ending it pays out.", NULL, NULL, 1, RMF_SERVER },
	{ "g_medals", RMTAB_RULES, "Medals:", "First blood, multi-kills, revenge and more.", NULL, NULL, 1, RMF_SERVER },
	{ "g_style", RMTAB_RULES, "Style meter:", "Flashy kills build a D to SSS rank; at S and above every kill gibs.", NULL, NULL, 1, RMF_SERVER },
	{ "g_perks", RMTAB_RULES, "Perks:", "Every 3 kills pick one of three perks with keys 1-3; they stack for the match.", NULL, NULL, 1, RMF_SERVER },
	{ "g_rearmPowerups", RMTAB_RULES, "New powerups:", "Vampire, Rapid Fire and Jetpack replace some powerup spawns.", NULL, NULL, 1, RMF_SERVER },
	{ "bot_rearm", RMTAB_RULES, "Smart bots:", "Bots dash, double jump, grapple, use alt-fire and play the modes.", NULL, NULL, 1, RMF_SERVER },
	{ "bot_rearmTricks", RMTAB_RULES, "Bot tricks:", "Bots slide, ground pound, blink, combo elements and pick perks by personality.", NULL, NULL, 1, RMF_SERVER },
	{ "bot_useProps", RMTAB_RULES, "Bots use props:", "Bots shoot explosive barrels and tanks next to you.", NULL, NULL, 1, RMF_SERVER },
	{ "bot_avoidDanger", RMTAB_RULES, "Bots dodge hazards:", "Bots step out of meteor and lightning markers, fire and ground pounds.", NULL, NULL, 1, RMF_SERVER },
	{ "g_dsBossLives", RMTAB_DESCENT, "DESCENT boss lives:", "10-lives boss fights only (BOTS tab): your lives on a boss floor. Out of lives = the floor is lost.", rm_bossLives_names, rm_bossLives_values, 10, RMF_SERVER },
	{ "g_dsBossScale", RMTAB_DESCENT, "DESCENT boss strength:", "Boss health and damage, from the next boss floor.", rm_bossScale_names, rm_bossScale_values, 1, RMF_SERVER },
	// en-screens: the BOTS tab is two columns, ALL MODES (the first 9) | DESCENT (rm_tabSplit, rm_colHeads); keep the order
	// round 4 en-brains (all modes; bot skill scales them: skill 1-2 barely, 4-5 fully). Console-only extras:
	// g_botHearing, g_botItemTiming, g_botRocketJump, g_botLeader, g_botLure (README)
	{ "g_botTactics", RMTAB_BOTS, "Smarter bots:", "Master switch: bots flank, dodge, retreat to heal, time the big items, hunt by sound, rocket jump, use barrels as traps.", NULL, NULL, 1, RMF_SERVER },
	{ "g_botFlank", RMTAB_BOTS, "Flank you:", "When two or more bots go for you, one holds and the others circle round to your side or back.", NULL, NULL, 1, RMF_SERVER },
	{ "g_botDodge", RMTAB_BOTS, "Dodge:", "Bots sidestep or hop your rockets, plasma and grenades and juke when your railgun is on them. Skilled bots react faster.", NULL, NULL, 1, RMF_SERVER },
	{ "g_botRetreat", RMTAB_BOTS, "Retreat to heal:", "Badly hurt bots break off toward health or armour away from you, then come back topped up.", NULL, NULL, 1, RMF_SERVER },
	{ "g_botHabits", RMTAB_BOTS, "Learn your habits:", "Bots notice your favourite weapon, range and routes this match (DESCENT: this run) and counter them.", NULL, NULL, 1, RMF_SERVER },
	{ "g_botWeaponThief", RMTAB_BOTS, "Weapon thieves:", "When you die a bot may grab your dropped gun and use it. Kill the thief to reclaim it for a bonus.", NULL, NULL, 1, RMF_SERVER },
	{ "g_botTaunts", RMTAB_BOTS, "Trash talk:", "Bots taunt in the chat corner with a short voice bark: kills, your barrel deaths, streaks, revenge, fleeing. Rate-limited.", rm_taunts_names, rm_taunts_values, 1, RMF_SERVER },
	{ "g_botGrudges", RMTAB_BOTS, "Grudges:", "A killed bot holds a grudge against its killer and goes after them. DESCENT: your own revenge kills pay a skull.", rm_grudges_names, rm_grudges_values, 1, RMF_SERVER },
	{ "g_botShouts", RMTAB_BOTS, "Attack shouts:", "A bot shouts right before a big wound-up attack (kamikaze, summoner, pull ...). A warning, not chatter.", NULL, NULL, 1, RMF_SERVER },
	{ "g_dsTypes", RMTAB_BOTS, "Enemy types:", "Some bots come as types: sniper (laser glint first), rusher, medic, shield bearer, kamikaze, blinker, summoner, mimic, juggernaut. More of them deeper down.", NULL, NULL, 1, RMF_SERVER },
	{ "g_dsAffixes", RMTAB_BOTS, "Elite affixes:", "Elites and ambush mini-bosses carry affixes (vampiric, explosive, splitting, frost ...): 1 in act 1, 2 in acts 2-3, 3 deeper. Shown by colour on the bot.", NULL, NULL, 1, RMF_SERVER },
	{ "g_dsChampions", RMTAB_BOTS, "Champion packs:", "Now and then three champions land together (gold pillars first), sharing affixes. One life each, better loot.", NULL, NULL, 1, RMF_SERVER },
	{ "g_dsNemesisLevels", RMTAB_BOTS, "Nemesis levels:", "Each time your nemesis kills you it levels up: tougher, better guns, a title, affixes at 3 and 5. Pays by its level.", NULL, NULL, 1, RMF_SERVER },
	{ "g_dsVeterans", RMTAB_BOTS, "Veteran bots:", "Bots that survive floors or frag you rank up for the run (small bumps) and come back. Rank pips on TAB, a shoulder light.", NULL, NULL, 1, RMF_SERVER },
	{ "g_botMemory", RMTAB_BOTS, "Bot memory:", "DESCENT: named bots remember you across runs: greetings and taunts quote your lifetime record, bots you've beaten often hold a grudge or show fear. Talk only: they talk about it but play as usual. Respects Trash talk.", rm_botMemory_names, rm_botMemory_values, 1, RMF_SERVER },
	{ "cg_dsBestiaryNote", RMTAB_BOTS, "Bestiary callout:", "DESCENT: the first time you meet an enemy kind, NEW IN BESTIARY shows at the left edge. The bestiary records either way.", NULL, NULL, 1, 0 },
	{ "g_dsBossMode", RMTAB_BOTS, "Boss fights:", "Timed 1v1: you and the boss, unlimited deaths, kill it before the clock runs out. 10 lives: you + 2 rivals vs the boss.", rm_bossMode_names, rm_bossMode_values, 0, RMF_SERVER },
	{ "g_dsBossTime", RMTAB_BOTS, "Boss clock:", "Timed boss fights: the time you have to kill the boss, from the next boss floor.", rm_bossTime_names, rm_bossTime_values, 300, RMF_SERVER },
	{ "g_skills", RMTAB_RULES, "Skill trees:", "Skills train by doing against bots (every mode); their perks work in DESCENT. Off: no xp, no perks.", NULL, NULL, 1, RMF_SERVER },
	{ "g_dsAmbush", RMTAB_DESCENT, "Mid-floor ambushes:", "Now and then a mini-boss crashes in partway through a floor (a warning and a pillar first). Kill it for loot.", NULL, NULL, 1, RMF_SERVER },
	{ "g_dsModifiers", RMTAB_DESCENT, "Floor modifiers:", "Some floors on the map offer an optional risk (ARMOURED, GLASS, SWARM ...) for more loot. Click the chip to take it.", NULL, NULL, 1, RMF_SERVER },
	{ "g_dsStagger", RMTAB_DESCENT, "Enemies join one by one:", "A floor starts with one or two opponents; the rest arrive every few seconds, each one announced. Off: all at once.", NULL, NULL, 1, RMF_SERVER },
	{ "g_dsWildEvents", RMTAB_DESCENT, "Wild events:", "Rare floor events: a golden meteor, a bot uprising, a blood eclipse, a bounty.", NULL, NULL, 1, RMF_SERVER },
	{ "g_dsGhosts", RMTAB_DESCENT, "Ghosts of past runs:", "Your past self (that run's kit at this depth) may come hunting you. Beat it for a bonus.", NULL, NULL, 1, RMF_SERVER },
	{ "g_dsNemesis", RMTAB_DESCENT, "DESCENT nemesis:", "The bot that kills you most (3+) follows you down, a little stronger. Revenge pays.", NULL, NULL, 1, RMF_SERVER },
	{ "g_dsFrenzy", RMTAB_DESCENT, "Kill-chain frenzy:", "Quick kills chain up: more and bigger gems, faster pull, a glow at the edges; 15 = BERSERK.", NULL, NULL, 1, RMF_SERVER },
	{ "g_dsCrits", RMTAB_DESCENT, "Lucky crits:", "A small chance per hit (luck raises it) for x1.5 damage, x2 on headshots. Bots crit less.", NULL, NULL, 1, RMF_SERVER },
	{ "g_dsSpikes", RMTAB_DESCENT, "Power spikes:", "Short god-mode bursts: BERSERK at a 15 chain, OVERDRIVE pickups from elites and lucky kills.", NULL, NULL, 1, RMF_SERVER },
	{ "g_dsBeams", RMTAB_DESCENT, "Loot beams:", "Coloured light pillars on rare drops, seen faintly through walls: white, blue, purple, gold.", NULL, NULL, 1, RMF_SERVER },
	{ "g_dsSecrets", RMTAB_DESCENT, "Secret stashes:", "1-3 hidden strongboxes per floor, some only reachable with your moves. Listen for the hum.", NULL, NULL, 1, RMF_SERVER },
	{ "cg_dsReveal", RMTAB_DESCENT, "Card reveal:", "Cards deal face-down and flip one by one, legendaries roar, the floor tally counts up. Off: instant.", NULL, NULL, 1, 0 },
	{ "g_dsVariants", RMTAB_DESCENT, "Floor variants:", "Each floor rolls a twist on its map: the weapons or armor trade places, NIGHT / FOG / BLOOD MOON light. Shown on the floor map.", NULL, NULL, 1, RMF_SERVER },
	{ "cg_dsVariantLight", RMTAB_DESCENT, "Variant lighting:", "The floor's light variant (NIGHT, FOG, DUSK ...) tints the world on top of the act's look.", rm_offOnStrong_names, rm_offOnStrong_values, 1, 0 },

	// HUD & FEEL
	{ "cg_hitMarkers", RMTAB_HUD, "Hitmarkers:", "Crosshair marker and tick sound on every hit.", NULL, NULL, 1, 0 },
	{ "cg_damageIndicator", RMTAB_HUD, "Damage direction:", "Red arrows around the crosshair point at whoever hit you.", NULL, NULL, 1, 0 },
	{ "cg_weaponKick", RMTAB_HUD, "View kick:", "Camera and gun recoil when firing.", rm_offLowFull_names, rm_offLowFull_values, 1, 0 },
	{ "cg_screenShake", RMTAB_HUD, "Screen shake:", "Shake from nearby explosions.", rm_offLowFull_names, rm_offLowFull_values, 1, 0 },
	{ "cg_fx", RMTAB_HUD, "Extra particles:", "Sparks, embers, shockwaves and gib bursts.", NULL, NULL, 1, 0 },
	{ "cg_dashHud", RMTAB_HUD, "Dash meter:", "Small dash-ready indicator under the crosshair.", NULL, NULL, 1, 0 },
	{ "cg_zipHud", RMTAB_HUD, "Zip line marker:", "Cyan [ ] around the crosshair when a zip would hold; a hint when it can't.", NULL, NULL, 1, 0 },
	{ "cg_rearmAnnouncer", RMTAB_HUD, "Announcer banners:", "Streak, medal and mutator banners with sounds.", NULL, NULL, 1, 0 },
	{ "cg_hudWide", RMTAB_HUD, "HUD corners:", "Where the HUD corner items sit on wide monitors.", rm_hudWide_names, rm_hudWide_values, 1, 0 },
	{ "cg_horPlus", RMTAB_HUD, "Hor+ FOV:", "Widen the field of view to fit wide screens.", NULL, NULL, 1, 0 },
	{ "cg_lowHealthFX", RMTAB_HUD, "Low health effect:", "Red pulse at the screen edges when health is low.", NULL, NULL, 1, 0 },
	{ "cg_speedFov", RMTAB_HUD, "Speed FOV:", "Field of view widens with speed and punches on dash, slide and slam.", NULL, NULL, 1, 0 },
	{ "cg_moveCamera", RMTAB_HUD, "Movement camera:", "Landing dip, slide lean and wall-run lean.", NULL, NULL, 1, 0 },
	{ "cg_tips", RMTAB_HUD, "First-time tips:", "One-line hints the first time a mechanic comes up.", NULL, NULL, 1, 0 },
	{ "cg_skillHud", RMTAB_HUD, "Weapon skill:", "The held weapon's skill level and xp bar above the ammo, a short flash on a level-up.", NULL, NULL, 1, 0 },
	{ "cg_style", RMTAB_HUD, "Style rank HUD:", "Show the style rank, meter and what earned it.", NULL, NULL, 1, 0 },
	{ "cg_enemyTags", RMTAB_HUD, "Enemy tags:", "Off: auras only. Minimal: small ELITE / BOSS label. Full: names, kits and award badges.", rm_enemyTags_names, rm_enemyTags_values, 0, 0 },
	{ "cg_dsIntro", RMTAB_HUD, "Enemy intro:", "Floor / match start: the opponents slide in at the top edge, the announcer reads them out. Fire skips.", NULL, NULL, 1, 0 },

	// VIDEO
	{ "g_props", RMTAB_CHAOS, "Physics props:", "Explosive barrels, gas tanks and crates you can shoot, kick and launch.", NULL, NULL, 1, RMF_SERVER },
	{ "g_corpsePhysics", RMTAB_CHAOS, "Corpse physics:", "Bodies and gibs fly from blasts; knock enemies into walls for a splat.", NULL, NULL, 1, RMF_SERVER },
	{ "g_propCount", RMTAB_CHAOS, "Props per map:", "How many props the map gets. Auto scales with the map size.", rm_propCount_names, rm_propCount_values, -1, RMF_SERVER|RMF_LATCH },
	{ "cg_physicsFX", RMTAB_CHAOS, "Debris:", "Rock chips, shrapnel and gibs pushed around by blasts.", rm_offOnDouble_names, rm_offOnDouble_values, 1, 0 },
	{ "cg_eventFX", RMTAB_CHAOS, "Event screen effects:", "Blood moon red, storm flashes, ice frost and other event tints.", NULL, NULL, 1, 0 },
	{ "cg_gore", RMTAB_CHAOS, "Gore:", "Decapitations, overkill gibs, charred bodies, splatter and footprints.", rm_gore_names, rm_gore_values, 2, 0 },
	{ "cg_gorePersist", RMTAB_CHAOS, "Corpses and blood:", "Stay: bodies, blood, gibs and bullet holes stay until the map ends. Fade: stock.", rm_gorePersist_names, rm_gorePersist_values, 1, 0 },
	{ "cg_gorePersistMax", RMTAB_CHAOS, "Corpses kept:", "Most bodies kept at once (blood and gibs scale with it). Lower it on slow PCs.", NULL, NULL, 64, 0, 8, 64, 8, "" },
	{ "g_gore", RMTAB_CHAOS, "Special deaths:", "Headshots decapitate, overkills explode, lightning chars (server).", NULL, NULL, 1, RMF_SERVER },
	{ "cg_screenFX", RMTAB_CHAOS, "Screen effects:", "Master switch for blood on screen, kill flash and speed lines.", NULL, NULL, 1, 0 },
	{ "cg_bloodScreen", RMTAB_CHAOS, "Blood on screen:", "Close gibs and big hits splatter your view.", NULL, NULL, 1, 0 },
	{ "cg_killFlash", RMTAB_CHAOS, "Kill flash:", "Flash, thump and camera punch on decapitations, overkills and SSS kills.", NULL, NULL, 1, 0 },
	{ "cg_speedLines", RMTAB_CHAOS, "Speed lines:", "Streaks at the screen edges when you move very fast.", NULL, NULL, 1, 0 },
	{ "g_slowmo", RMTAB_CHAOS, "Slow motion:", "Big kills slow time for a moment (single player only).", NULL, NULL, 1, RMF_SERVER },
	{ "cg_killCam", RMTAB_CHAOS, "Side kill cam:", "Slow motion replay of gibs, headshots, multi kills and boss kills in a side panel.", rm_killCam_names, rm_killCam_values, 1, 0 },
	{ "cg_killCamSide", RMTAB_CHAOS, "Kill cam side:", "Auto: the side the kill happened on (left on 16:9 screens).", rm_killCamSide_names, rm_killCamSide_values, 0, 0 },
	{ "cg_deathCam", RMTAB_CHAOS, "Death cam:", "When you die: slow motion of your killer, with their health and weapon.", NULL, NULL, 1, 0 },
	{ "cg_replays", RMTAB_CHAOS, "Best kill replay:", "Replays your best kill at the floor / match end. Jump or fire skips it.", NULL, NULL, 1, 0 },
	// round 4 HIT FEEL (en-feel): g_hitfeel.c / cg_hitfeel.c
	{ "cg_hitFlinch", RMTAB_HITS, "Hit flinch:", "Bodies jerk away from every hit: torso and head kick back from the shot's direction.", NULL, NULL, 1, 0 },
	{ "g_stagger", RMTAB_HITS, "Stagger:", "Rails, direct rockets, charged shots, headshots and 60+ hits stagger a bot: it can't fire and its aim drifts. Juggernauts resist.", NULL, NULL, 1, RMF_SERVER },
	{ "g_dismember", RMTAB_HITS, "Dismemberment:", "Heavy hits pop heads and helmets and tear off arms; big blasts take the legs. The pieces stay on the floor.", NULL, NULL, 1, RMF_SERVER },
	{ "g_crawl", RMTAB_HITS, "Crawling wounded:", "A bot that is nearly dead or lost its legs may drop and crawl away, leaving a blood trail.", NULL, NULL, 1, RMF_SERVER },
	{ "g_morale", RMTAB_HITS, "Morale and panic:", "Big hits, lost limbs, gibbed friends and lost duels break a bot's nerve: it screams, drops its gun and runs for a few seconds.", NULL, NULL, 1, RMF_SERVER },
	{ "g_wallSplat", RMTAB_HITS, "Wall splats:", "Knock a body into a wall hard: a thud, a big blood splat and extra damage. Needs corpse physics.", NULL, NULL, 1, RMF_SERVER },
	{ "cg_armorCracks", RMTAB_HITS, "Armour cracks:", "Armour shows cracks and a hot glow as it is shot away, sparks on hits, and shatters when it breaks.", NULL, NULL, 1, 0 },
	{ "cg_deathScreams", RMTAB_HITS, "Death screams:", "Different screams by how they died (headshot gurgle, gib, burning, falling, blast), a voice per bot.", NULL, NULL, 1, 0 },
	{ "r_fullscreen", RMTAB_VIDEO, "Display mode:", "Borderless fills the screen without exclusive fullscreen: alt-tab and pop-ups don't throw you out.", rm_display_names, rm_display_values, 2, RMF_VIDRESTART },
	{ "r_bloom", RMTAB_VIDEO, "Bloom:", "Glow around bright lights and effects.", NULL, NULL, 1, 0 },
	{ "r_bloomIntensity", RMTAB_VIDEO, "Bloom strength:", "How strong the bloom glow is.", rm_bloomStrength_names, rm_bloomStrength_values, 0.6f, 0 },
	{ "r_rmSharpen", RMTAB_VIDEO, "Sharpen:", "Contrast-adaptive sharpening.", rm_sharpen_names, rm_sharpen_values, 0.3f, 0 },
	{ "r_rmVignette", RMTAB_VIDEO, "Vignette:", "Darkens the screen corners slightly.", NULL, NULL, 1, 0 },
	{ "r_rmGrain", RMTAB_VIDEO, "Film grain:", "Subtle animated film grain.", NULL, NULL, 0, 0 },
	{ "r_rmChroma", RMTAB_VIDEO, "Chromatic aberration:", "Colour fringing at the screen edges.", NULL, NULL, 0, 0 },
	{ "r_ssao", RMTAB_VIDEO, "Ambient occlusion:", "SSAO. Needs vid_restart.", NULL, NULL, 0, RMF_VIDRESTART },
	{ "r_sunShadows", RMTAB_VIDEO, "Sun shadows:", "Needs vid_restart.", NULL, NULL, 1, RMF_VIDRESTART }
};

#define RM_NUM_ITEMS			((int)ARRAY_LEN( rm_items ))


typedef struct {
	menuframework_s		menu;

	menutext_s			banner;
	menubitmap_s		framel;
	menubitmap_s		framer;

	menutext_s			tabs[RM_NUM_TABS];

	// each entry of rm_items uses one of radio[i], spin[i] or slider[i]
	menuradiobutton_s	radio[RM_NUM_ITEMS];
	menulist_s			spin[RM_NUM_ITEMS];
	menuslider_s		slider[RM_NUM_ITEMS];

	menutext_s			apply;
	menubitmap_s		back;

	int					tab;
	int					headY[RM_NUM_TABS];		// en-screens: y of a two-column tab's headings
} rearmed_t;

static rearmed_t s_rearmed;

// remembered across openings of the menu
static int rm_lastTab = RMTAB_MOVEMENT;

static vec4_t rm_orange = { 1.0f, 0.42f, 0.1f, 1.0f };


/*
=================
Rearmed_ItemType
=================
*/
static int Rearmed_ItemType( int i ) {
	if ( rm_items[i].sliderStep > 0 ) {
		return RMT_SLIDER;
	}
	if ( rm_items[i].names ) {
		return RMT_SPIN;
	}
	return RMT_RADIO;
}


/*
=================
Rearmed_ItemCommon
=================
*/
static menucommon_s *Rearmed_ItemCommon( int i ) {
	switch ( Rearmed_ItemType( i ) ) {
	case RMT_SLIDER:
		return &s_rearmed.slider[i].generic;
	case RMT_SPIN:
		return &s_rearmed.spin[i].generic;
	default:
		return &s_rearmed.radio[i].generic;
	}
}


/*
=================
Rearmed_SliderValue

The cvar value of the slider position, snapped to whole steps
=================
*/
static float Rearmed_SliderValue( int i ) {
	int		notch;

	notch = (int)( s_rearmed.slider[i].curvalue + 0.5f );
	return rm_items[i].sliderMin + notch * rm_items[i].sliderStep;
}


/*
=================
Rearmed_ReadValue

Returns the cvar value, or the item default if the cvar does not exist
yet (the game or cgame module that registers it has not run this session).
=================
*/
static float Rearmed_ReadValue( int i ) {
	char	buf[32];

	trap_Cvar_VariableStringBuffer( rm_items[i].cvar, buf, sizeof( buf ) );
	if ( !buf[0] ) {
		return rm_items[i].defaultValue;
	}
	return atof( buf );
}


/*
=================
Rearmed_SetMenuItems
=================
*/
static void Rearmed_SetMenuItems( void ) {
	int		i;
	int		n;
	int		best;
	float	value;
	float	diff;
	float	bestDiff;

	for ( i = 0; i < RM_NUM_ITEMS; i++ ) {
		value = Rearmed_ReadValue( i );

		if ( Rearmed_ItemType( i ) == RMT_SLIDER ) {
			// slider notches run 0..maxvalue, one per step
			if ( value < rm_items[i].sliderMin ) {
				value = rm_items[i].sliderMin;
			}
			if ( value > rm_items[i].sliderMax ) {
				value = rm_items[i].sliderMax;
			}
			s_rearmed.slider[i].curvalue = (int)( ( value - rm_items[i].sliderMin ) / rm_items[i].sliderStep + 0.5f );
			continue;
		}

		if ( Rearmed_ItemType( i ) == RMT_RADIO ) {
			s_rearmed.radio[i].curvalue = ( value != 0 );
			continue;
		}

		// pick the closest entry
		best = 0;
		bestDiff = 999999.0f;
		for ( n = 0; rm_items[i].names[n]; n++ ) {
			diff = value - rm_items[i].values[n];
			if ( diff < 0 ) {
				diff = -diff;
			}
			if ( diff < bestDiff ) {
				bestDiff = diff;
				best = n;
			}
		}
		s_rearmed.spin[i].curvalue = best;
	}
}


/*
=================
Rearmed_WriteItem
=================
*/
static void Rearmed_WriteItem( int i ) {
	switch ( Rearmed_ItemType( i ) ) {
	case RMT_SLIDER:
		// a mouse click can leave the thumb between notches: snap it
		s_rearmed.slider[i].curvalue = (int)( s_rearmed.slider[i].curvalue + 0.5f );
		trap_Cvar_SetValue( rm_items[i].cvar, Rearmed_SliderValue( i ) );
		break;
	case RMT_SPIN:
		trap_Cvar_SetValue( rm_items[i].cvar, rm_items[i].values[s_rearmed.spin[i].curvalue] );
		break;
	default:
		trap_Cvar_SetValue( rm_items[i].cvar, s_rearmed.radio[i].curvalue );
		break;
	}
}


/*
=================
Rearmed_SetTab
=================
*/
static void Rearmed_SetTab( int tab ) {
	int				i;
	menucommon_s	*item;

	if ( tab < 0 || tab >= RM_NUM_TABS ) {
		tab = RMTAB_MOVEMENT;
	}
	s_rearmed.tab = tab;
	rm_lastTab = tab;

	for ( i = 0; i < RM_NUM_TABS; i++ ) {
		if ( i == tab ) {
			s_rearmed.tabs[i].color = color_white;
		}
		else {
			s_rearmed.tabs[i].color = color_red;
		}
	}

	for ( i = 0; i < RM_NUM_ITEMS; i++ ) {
		item = Rearmed_ItemCommon( i );
		if ( rm_items[i].tab == tab ) {
			item->flags &= ~( QMF_HIDDEN|QMF_INACTIVE );
		}
		else {
			item->flags |= ( QMF_HIDDEN|QMF_INACTIVE );
		}
	}

	if ( tab == RMTAB_VIDEO ) {
		s_rearmed.apply.generic.flags &= ~( QMF_HIDDEN|QMF_INACTIVE );
	}
	else {
		s_rearmed.apply.generic.flags |= ( QMF_HIDDEN|QMF_INACTIVE );
	}
}


/*
=================
Rearmed_Event
=================
*/
static void Rearmed_Event( void *ptr, int notification ) {
	int		id;

	if ( notification != QM_ACTIVATED ) {
		return;
	}

	id = ((menucommon_s*)ptr)->id;

	if ( id >= ID_TAB0 && id < ID_TAB0 + RM_NUM_TABS ) {
		Rearmed_SetTab( id - ID_TAB0 );
		return;
	}

	if ( id >= ID_ITEM0 && id < ID_ITEM0 + RM_NUM_ITEMS ) {
		Rearmed_WriteItem( id - ID_ITEM0 );
		return;
	}

	switch ( id ) {
	case ID_APPLY:
		trap_Cmd_ExecuteText( EXEC_APPEND, "vid_restart\n" );
		break;

	case ID_BACK:
		UI_PopMenu();
		break;
	}
}


/*
=================
Rearmed_DrawHelp

Draws help text centered at the bottom of the screen, word wrapped.
=================
*/
static void Rearmed_DrawHelp( const char *text ) {
	char	line[REARMED_HELP_CHARS + 1];
	int		len;
	int		n;
	int		y;

	y = REARMED_HELP_Y;
	while ( *text ) {
		len = strlen( text );
		if ( len > REARMED_HELP_CHARS ) {
			// break at the last space that fits
			for ( n = REARMED_HELP_CHARS; n > 0 && text[n] != ' '; n-- ) {
			}
			if ( n == 0 ) {
				n = REARMED_HELP_CHARS;
			}
		}
		else {
			n = len;
		}

		Q_strncpyz( line, text, n + 1 );
		UI_DrawString( 320, y, line, UI_CENTER|UI_SMALLFONT, colorWhite );

		text += n;
		while ( *text == ' ' ) {
			text++;
		}
		y += SMALLCHAR_HEIGHT + 2;
	}
}


/*
=================
Rearmed_StatusBar
=================
*/
static void Rearmed_StatusBar( void *self ) {
	char				text[256];
	int					id;
	const rearmedItem_t	*def;

	id = ((menucommon_s*)self)->id;

	if ( id >= ID_TAB0 && id < ID_TAB0 + RM_NUM_TABS ) {
		Rearmed_DrawHelp( rm_tabHelp[id - ID_TAB0] );
		return;
	}

	if ( id == ID_APPLY ) {
		Rearmed_DrawHelp( "Restart the renderer now so ambient occlusion and sun shadows change." );
		return;
	}

	if ( id < ID_ITEM0 || id >= ID_ITEM0 + RM_NUM_ITEMS ) {
		return;
	}

	def = &rm_items[id - ID_ITEM0];
	Q_strncpyz( text, def->help, sizeof( text ) );
	if ( def->flags & RMF_LATCH ) {
		Q_strcat( text, sizeof( text ), "  Takes effect on the next map." );
	}
	if ( def->flags & RMF_VIDRESTART ) {
		Q_strcat( text, sizeof( text ), "  Press APPLY to restart video." );
	}
	if ( ( def->flags & RMF_SERVER ) && !trap_Cvar_VariableValue( "sv_running" ) ) {
		Q_strcat( text, sizeof( text ), "  (applies when you host)" );
	}
	Rearmed_DrawHelp( text );
}


/*
=================
Rearmed_MenuDraw
=================
*/
static void Rearmed_MenuDraw( void ) {
	menutext_s		*t;
	menuslider_s	*s;
	char			buf[32];
	int				i;

	Menu_Draw( &s_rearmed.menu );

	// underline the current tab
	t = &s_rearmed.tabs[s_rearmed.tab];
	UI_FillRect( t->generic.left, t->generic.bottom + 1, t->generic.right - t->generic.left, 2, rm_orange );

	// en-screens: a two-column tab's headings, centred over the label / value seam of each column
	if ( rm_tabSplit[s_rearmed.tab] && rm_colHeads[s_rearmed.tab][0] ) {
		for ( i = 0; i < 2; i++ ) {
			int		cx = i ? REARMED_COL2_X : REARMED_COL1_X;
			int		hw = strlen( rm_colHeads[s_rearmed.tab][i] ) * SMALLCHAR_WIDTH / 2 + 6;

			UI_DrawString( cx, s_rearmed.headY[s_rearmed.tab], rm_colHeads[s_rearmed.tab][i], UI_CENTER|UI_SMALLFONT, color_white );
			UI_FillRect( cx - hw, s_rearmed.headY[s_rearmed.tab] + SMALLCHAR_HEIGHT + 1, hw * 2, 1, rm_orange );
		}
	}

	// sliders don't show their value, so print it right of the bar
	for ( i = 0; i < RM_NUM_ITEMS; i++ ) {
		if ( Rearmed_ItemType( i ) != RMT_SLIDER ) {
			continue;
		}
		s = &s_rearmed.slider[i];
		if ( s->generic.flags & QMF_HIDDEN ) {
			continue;
		}
		Com_sprintf( buf, sizeof( buf ), "%i%s", (int)Rearmed_SliderValue( i ),
			rm_items[i].sliderUnit ? rm_items[i].sliderUnit : "" );
		UI_DrawString( s->generic.x + ( SLIDER_RANGE + 4 ) * SMALLCHAR_WIDTH, s->generic.y, buf, UI_LEFT|UI_SMALLFONT, text_color_normal );
	}
}


/*
=================
Rearmed_MenuInit
=================
*/
/*
Rearmed_TabX: label centres for the tab row, in rm_tabOrder, equal gaps between 12 and 628
*/
static void Rearmed_TabX( int *tabX ) {
	float	w[RM_NUM_TABS];
	float	total, gap, x, scale, extra;
	int		i, t;

	scale = UI_ProportionalSizeScale( UI_SMALLFONT );
	total = 0;
	for ( i = 0; i < RM_NUM_TABS; i++ ) {
		w[i] = UI_ProportionalStringWidth( rm_tabNames[rm_tabOrder[i]] ) * scale;
		total += w[i];
	}
	// en-screens: 9 labels don't fit 616 units with real gaps (they ran together); on a wide screen
	// the row may reach up to 70 units past each side of the 4:3 box
	extra = uis.xscale > 0 ? uis.bias / uis.xscale : 0;
	if ( extra > 70.0f ) {
		extra = 70.0f;
	}
	if ( extra < 0 ) {
		extra = 0;
	}
	gap = ( 616.0f + 2 * extra - total ) / ( RM_NUM_TABS - 1 );
	if ( gap < 4.0f ) {
		gap = 4.0f;
	}
	x = 12.0f - extra;
	if ( gap > 24.0f ) {		// keep the row together on very wide screens' worth of room
		x += ( gap - 24.0f ) * ( RM_NUM_TABS - 1 ) * 0.5f;
		gap = 24.0f;
	}
	for ( i = 0; i < RM_NUM_TABS; i++ ) {
		t = rm_tabOrder[i];
		tabX[t] = (int)( x + w[i] * 0.5f );
		x += w[i] + gap;
	}
}

static void Rearmed_MenuInit( void ) {
	int				i;
	int				tabX[RM_NUM_TABS];
	int				tab;
	int				count;
	int				y;
	int				spacing;
	int				split, rows, y0, k, x;
	menucommon_s	*item;

	memset( &s_rearmed, 0, sizeof( rearmed_t ) );

	Rearmed_Cache();

	s_rearmed.menu.wrapAround = qtrue;
	s_rearmed.menu.fullscreen = qtrue;
	s_rearmed.menu.draw       = Rearmed_MenuDraw;

	s_rearmed.banner.generic.type  = MTYPE_BTEXT;
	s_rearmed.banner.generic.x     = 320;
	s_rearmed.banner.generic.y     = 16;
	s_rearmed.banner.string        = "REARMED SETTINGS";
	s_rearmed.banner.color         = color_white;
	s_rearmed.banner.style         = UI_CENTER;

	s_rearmed.framel.generic.type  = MTYPE_BITMAP;
	s_rearmed.framel.generic.name  = ART_FRAMEL;
	s_rearmed.framel.generic.flags = QMF_INACTIVE;
	s_rearmed.framel.generic.x     = 0;
	s_rearmed.framel.generic.y     = 78;
	s_rearmed.framel.width         = 256;
	s_rearmed.framel.height        = 329;

	s_rearmed.framer.generic.type  = MTYPE_BITMAP;
	s_rearmed.framer.generic.name  = ART_FRAMER;
	s_rearmed.framer.generic.flags = QMF_INACTIVE;
	s_rearmed.framer.generic.x     = 376;
	s_rearmed.framer.generic.y     = 76;
	s_rearmed.framer.width         = 256;
	s_rearmed.framer.height        = 334;

	// tab row: labels spread over the frame with equal gaps
	Rearmed_TabX( tabX );
	for ( i = 0; i < RM_NUM_TABS; i++ ) {
		s_rearmed.tabs[i].generic.type      = MTYPE_PTEXT;
		s_rearmed.tabs[i].generic.flags     = QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
		s_rearmed.tabs[i].generic.x         = tabX[i];
		s_rearmed.tabs[i].generic.y         = REARMED_TAB_Y;
		s_rearmed.tabs[i].generic.id        = ID_TAB0 + i;
		s_rearmed.tabs[i].generic.callback  = Rearmed_Event;
		s_rearmed.tabs[i].generic.statusbar = Rearmed_StatusBar;
		s_rearmed.tabs[i].string            = (char *)rm_tabNames[i];
		s_rearmed.tabs[i].color             = color_red;
		s_rearmed.tabs[i].style             = UI_CENTER|UI_SMALLFONT;
	}

	// controls, each tab's column centered vertically
	for ( tab = 0; tab < RM_NUM_TABS; tab++ ) {
		count = 0;
		for ( i = 0; i < RM_NUM_ITEMS; i++ ) {
			if ( rm_items[i].tab == tab ) {
				count++;
			}
		}

		// en-screens: a two-column tab (rm_tabSplit) lays out the longer column's rows
		split = rm_tabSplit[tab] > 0 && rm_tabSplit[tab] < count ? rm_tabSplit[tab] : 0;
		rows = split ? ( split > count - split ? split : count - split ) : count;

		// a crowded tab packs its rows tighter so they stay between the tab labels and the help text
		spacing = rows * REARMED_SPACING > REARMED_COLUMN_H ? REARMED_COLUMN_H / rows : REARMED_SPACING;
		y = REARMED_CENTER_Y - ( rows * spacing ) / 2;
		if ( split && rm_colHeads[tab][0] ) {
			y += 10;		// room for the column headings
			s_rearmed.headY[tab] = y - REARMED_SPACING - 4;
		}
		y0 = y;
		k = 0;
		for ( i = 0; i < RM_NUM_ITEMS; i++ ) {
			if ( rm_items[i].tab != tab ) {
				continue;
			}
			if ( split ) {
				x = k < split ? REARMED_COL1_X : REARMED_COL2_X;
				y = y0 + ( k < split ? k : k - split ) * spacing;
			} else {
				x = REARMED_X_POS;
			}
			k++;

			switch ( Rearmed_ItemType( i ) ) {
			case RMT_SLIDER:
				s_rearmed.slider[i].generic.type = MTYPE_SLIDER;
				s_rearmed.slider[i].minvalue     = 0;
				s_rearmed.slider[i].maxvalue     = (int)( ( rm_items[i].sliderMax - rm_items[i].sliderMin ) / rm_items[i].sliderStep + 0.5f );
				break;
			case RMT_SPIN:
				s_rearmed.spin[i].generic.type = MTYPE_SPINCONTROL;
				s_rearmed.spin[i].itemnames    = rm_items[i].names;
				break;
			default:
				s_rearmed.radio[i].generic.type = MTYPE_RADIOBUTTON;
				break;
			}

			item = Rearmed_ItemCommon( i );
			item->name      = rm_items[i].label;
			item->flags     = QMF_PULSEIFFOCUS|QMF_SMALLFONT;
			item->callback  = Rearmed_Event;
			item->statusbar = Rearmed_StatusBar;
			item->id        = ID_ITEM0 + i;
			item->x         = x;
			item->y         = y;

			y += spacing;
		}
	}

	s_rearmed.apply.generic.type      = MTYPE_PTEXT;
	s_rearmed.apply.generic.flags     = QMF_RIGHT_JUSTIFY|QMF_PULSEIFFOCUS;
	s_rearmed.apply.generic.x         = 624;
	s_rearmed.apply.generic.y         = 480 - 44;
	s_rearmed.apply.generic.id        = ID_APPLY;
	s_rearmed.apply.generic.callback  = Rearmed_Event;
	s_rearmed.apply.generic.statusbar = Rearmed_StatusBar;
	s_rearmed.apply.string            = "APPLY";
	s_rearmed.apply.color             = color_red;
	s_rearmed.apply.style             = UI_RIGHT;

	s_rearmed.back.generic.type     = MTYPE_BITMAP;
	s_rearmed.back.generic.name     = ART_BACK0;
	s_rearmed.back.generic.flags    = QMF_LEFT_JUSTIFY|QMF_PULSEIFFOCUS;
	s_rearmed.back.generic.callback = Rearmed_Event;
	s_rearmed.back.generic.id       = ID_BACK;
	s_rearmed.back.generic.x        = 0;
	s_rearmed.back.generic.y        = 480-64;
	s_rearmed.back.width            = 128;
	s_rearmed.back.height           = 64;
	s_rearmed.back.focuspic         = ART_BACK1;

	Menu_AddItem( &s_rearmed.menu, &s_rearmed.banner );
	Menu_AddItem( &s_rearmed.menu, &s_rearmed.framel );
	Menu_AddItem( &s_rearmed.menu, &s_rearmed.framer );

	for ( i = 0; i < RM_NUM_TABS; i++ ) {
		Menu_AddItem( &s_rearmed.menu, &s_rearmed.tabs[i] );
	}

	for ( i = 0; i < RM_NUM_ITEMS; i++ ) {
		Menu_AddItem( &s_rearmed.menu, Rearmed_ItemCommon( i ) );
	}

	Menu_AddItem( &s_rearmed.menu, &s_rearmed.apply );
	Menu_AddItem( &s_rearmed.menu, &s_rearmed.back );

	Rearmed_SetMenuItems();
	Rearmed_SetTab( rm_lastTab );
}


/*
===============
Rearmed_Cache
===============
*/
void Rearmed_Cache( void ) {
	trap_R_RegisterShaderNoMip( ART_FRAMEL );
	trap_R_RegisterShaderNoMip( ART_FRAMER );
	trap_R_RegisterShaderNoMip( ART_BACK0 );
	trap_R_RegisterShaderNoMip( ART_BACK1 );
}


/*
===============
UI_RearmedMenu
===============
*/
void UI_RearmedMenuTab( int tab ) {
	if ( tab >= 0 && tab < RM_NUM_TABS ) {
		rm_lastTab = tab;
	}
	UI_RearmedMenu();
}

void UI_RearmedMenu( void ) {
	Rearmed_MenuInit();
	UI_PushMenu( &s_rearmed.menu );
	Menu_SetCursorToItem( &s_rearmed.menu, &s_rearmed.tabs[s_rearmed.tab] );
}
