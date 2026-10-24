/*
===========================================================================
REARMED -- DESCENT round 3 SURPRISES (ds-surprise): shared tables (game, cgame, ui).
See bg_ds_surprise.h.
===========================================================================
*/
#include "../qcommon/q_shared.h"
#include "bg_public.h"

/*
Floor modifiers. Enemy side: past single digits is fine (it's their side); the player's
reward is a reward multiplier / an extra chest / an extra card, never a stat boost.
*/
const dsModInfo_t ds_modifiers[DSMOD_NUM] = {
	//	name			key			risk												reward					sk%  xp%  gem% ch cd dmg% arm spd% bots hp%
	{ "",				"none",		"",													"",						0,   0,   0,   0, 0, 0,   0,  0,   0,   100 },
	{ "ARMOURED",		"armoured",	"Enemies spawn with +50 armor.",					"+25% SKULLS",			25,  0,   0,   0, 0, 0,   50, 0,   0,   100 },
	{ "BLOODLUST",		"bloodlust","Enemies deal +20% damage.",						"+1 ELITE CHEST",		0,   0,   0,   1, 0, 20,  0,  0,   0,   100 },
	{ "BLACKOUT",		"blackout",	"The lights go down. Gems glow through the dark.",	"+30% GEM XP",			0,   30,  0,   0, 0, 0,   0,  0,   0,   100 },
	{ "SWARM",			"swarm",	"Two more bots join the floor.",					"+40% GEMS",			0,   0,   40,  0, 0, 0,   0,  0,   2,   100 },
	{ "GLASS",			"glass",	"Everyone has half health. You too.",				"+25% SKULLS",			25,  0,   0,   0, 0, 0,   0,  0,   0,   50 },
	{ "FERAL",			"feral",	"Enemies move 15% faster.",							"+1 CARD TO CHOOSE",	0,   0,   0,   0, 1, 0,   0,  15,  0,   100 }
};

int BG_DS_ModByKey( const char *key ) {
	int		m;

	if ( !key || !key[0] ) {
		return DSMOD_NONE;
	}
	if ( key[0] >= '0' && key[0] <= '9' ) {
		m = atoi( key );
		return m > 0 && m < DSMOD_NUM ? m : DSMOD_NONE;
	}
	for ( m = 1 ; m < DSMOD_NUM ; m++ ) {
		if ( !Q_stricmp( key, ds_modifiers[m].key ) || !Q_stricmp( key, ds_modifiers[m].name ) ) {
			return m;
		}
	}
	return DSMOD_NONE;
}

/*
Mini-bosses: two strong sides each (speed / health / damage > 1), like the act bosses but
smaller pools, one life, one or two of the bosses' attacks.
*/
const dsMiniBoss_t ds_miniBosses[DS_MINIBOSS_NUM] = {
	// name				net name	bot file	speed  health damage  attacks							colour
	{ "THE HEADSMAN",	"Headsman",	"Gorre",	1.18f, 0.75f, 1.25f, { DSB_CLEAVER, DSB_HOOK },		{ 1.0f, 0.18f, 0.12f } },
	{ "THE WARDEN",		"Warden",	"TankJr",	0.88f, 1.35f, 1.20f, { DSB_BARRAGE, DSB_SLAM },		{ 1.0f, 0.55f, 0.15f } },
	{ "THE WRAITH",		"Wraith",	"Wrack",	1.18f, 1.30f, 0.75f, { DSB_LANCE, DSB_HOOK },		{ 0.7f, 0.45f, 1.0f } },
	{ "THE PYRE",		"Pyre",		"Cadavre",	0.90f, 1.30f, 1.20f, { DSB_METEOR, DSB_SLAM },		{ 1.0f, 0.42f, 0.05f } },
	{ "THE HOUND",		"Hound",	"Slash",	1.22f, 1.25f, 0.80f, { DSB_SLAM, DSB_CLEAVER },		{ 0.45f, 1.0f, 0.25f } }
};

const char *ds_wildEventNames[DSEV_NUM] = { "", "GOLD METEOR", "BOT UPRISING", "BLOOD ECLIPSE", "BOUNTY" };
const char *ds_wildEventKeys[DSEV_NUM] = { "none", "goldmeteor", "uprising", "eclipse", "bounty" };
const char *ds_wildEventHints[DSEV_NUM] = {
	"",
	"A golden meteor is falling. Its crater holds gems and a chest: get there first.",
	"Weak bots pour in from the spawn points for 30 seconds. Chain fuel.",
	"For 25 seconds every kill drops double gems.",
	"The leader carries a chest for 40 seconds. Kill them to take it."
};
