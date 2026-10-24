/*
===========================================================================
REARMED -- round 4 BESTIARY (en-bestiary): names, colours and tips (bg_ds_bestiary.h).
Archetype names come from ds_types[].name (en-types: NULL here); the affix names here are the
only copy (BG_DS_AffixName, en-affix's callouts and logs).
===========================================================================
*/
#include "../qcommon/q_shared.h"
#include "bg_public.h"
#include "bg_ds_bestiary.h"
#include "bg_ds_types.h"

const dsBstInfo_t bg_dsBst[DSB_NUM] = {
	// archetypes (DSAR_SNIPER ..)
	{ NULL,			"SNP", { 1.00f, 0.18f, 0.12f }, 1,
		"Railgun from high ground. A red laser glint before every shot, then it moves to a new perch.",
		"Strafe off the laser line before the rail fires, then rush it while it relocates." },
	{ NULL,			"RSH", { 1.00f, 0.72f, 0.15f }, 1,
		"Fast, shotgun in hand, charges straight in and never backs off.",
		"Back-pedal and splash it on the way in, or meet the charge with a point-blank shot of your own." },
	{ NULL,			"MED", { 0.25f, 1.00f, 0.40f }, 2,
		"Heals a wounded ally with a green beam, 12 health a second. Its own gun is weak.",
		"Kill it first. Follow the green beam back to it." },
	{ NULL,			"SHD", { 0.45f, 0.85f, 1.00f }, 1,
		"The shield bearer. A raised front shield stops 80% of direct hits; it lowers it to fire.",
		"Shoot its back or splash its feet, or hit it the moment the shield drops." },
	{ NULL,			"KMK", { 1.00f, 0.42f, 0.05f }, 1,
		"Runs at you, beeps and glows for a second, then blows up.",
		"Kill it at range: shot early it only pops. When it starts beeping, get away." },
	{ NULL,			"BLK", { 0.95f, 0.35f, 1.00f }, 2,
		"Short teleports to flank you. It never lands closer than 400 units.",
		"Keep moving and check behind you. Hit it as it lands, before it blinks again." },
	{ NULL,			"SUM", { 0.85f, 0.88f, 0.72f }, 3,
		"Raises skeletons from corpses: a pillar and a ring on the body during the ritual.",
		"Kill it during the ritual pillar, or gib the corpses before it gets to them." },
	{ NULL,			"MIM", { 0.70f, 0.72f, 0.85f }, 3,
		"Carries a copy of your best weapon, at your weapon level.",
		"Fight it where your best weapon is weak. You know that gun, so dodge it the way bots dodge you." },
	{ NULL,			"JUG", { 0.55f, 0.62f, 0.75f }, 2,
		"Big, slow, heavy weapons and 50 extra armour. Only big hits stagger it.",
		"Kite it and keep rockets and rails on it. Never stand in its chaingun line." },

	// affixes (bit order of DSAF_*)
	{ "VAMPIRIC",		"VMP", { 0.85f, 0.08f, 0.22f }, 1,
		"Heals 30% of the damage it deals.",
		"Don't trade slow hits with it: burst it down." },
	{ "EXPLOSIVE",		"EXP", { 1.00f, 0.50f, 0.10f }, 1,
		"Its corpse blows up 1.3 seconds after it dies (the body swells first).",
		"Kill it, then step away. Don't run over the body." },
	{ "SPLITTING",		"SPL", { 0.70f, 0.35f, 1.00f }, 1,
		"Its first death makes two weaker copies.",
		"Save a splash shot for the pair that comes out of the tear." },
	{ "REFLECTING",		"RFL", { 0.80f, 0.86f, 0.95f }, 1,
		"Sends 20% of the damage it takes back at you (never lethal).",
		"Watch your health when you unload on it. Bursts beat long sprays." },
	{ "FROST",			"FRS", { 0.50f, 0.85f, 1.00f }, 1,
		"Its hits slow you to 70% for 1.3 seconds.",
		"Keep your distance. Never let it chain hits on you." },
	{ "BLAZING",		"BLZ", { 1.00f, 0.35f, 0.05f }, 1,
		"Leaves short-lived fire patches behind it.",
		"Don't chase it through its own trail. Cut it off instead." },
	{ "MAGNETIC",		"MAG", { 0.40f, 0.50f, 1.00f }, 1,
		"Yanks you toward it after a 0.9 second wind-up (a tether shows).",
		"When the tether shows, break line of sight or get behind cover." },
	{ "REGENERATING",	"RGN", { 0.35f, 1.00f, 0.45f }, 1,
		"Heals 5% a second when it is out of combat.",
		"Once you start on it, stay on it. Don't let it slip away to heal." },

	// extras
	{ "CHAMPION PACK",	"CHP", { 1.00f, 0.80f, 0.25f }, 1,
		"Three elites that share the same affixes land together under gold pillars.",
		"Pull them apart and kill them one at a time. Don't fight all three in the open." },
	{ NULL, "HDS", { 1.00f, 0.18f, 0.12f }, 0,
		"Ambush mini-boss. Fast and deadly, but fragile. Cleaver charge and meat hook.",
		"Keep your range and side-step the charge. One life: kill it yourself for the loot." },
	{ NULL, "WRD", { 1.00f, 0.55f, 0.15f }, 0,
		"Ambush mini-boss. Slow, tough and deadly. Rocket barrage and ground slam.",
		"Stay out of slam range, strafe the barrages and chip it from afar." },
	{ NULL, "WRT", { 0.70f, 0.45f, 1.00f }, 0,
		"Ambush mini-boss. Fast and tough, but hits soft. Rail lances and meat hook.",
		"Side-step on the lance flash, then out-damage it up close." },
	{ NULL, "PYR", { 1.00f, 0.42f, 0.05f }, 0,
		"Ambush mini-boss. Slow, tough and deadly. Meteors and ground slam.",
		"Never stand still under the meteors. Keep it at range." },
	{ NULL, "HND", { 0.45f, 1.00f, 0.25f }, 0,
		"Ambush mini-boss. Fast and tough. Ground slam and cleaver charge.",
		"Dodge the charge sideways and punish it while it recovers." },
	{ NULL, "BUT", { 0.85f, 0.15f, 0.10f }, 1,
		"Act 1 boss. Meat hook pull, cleaver charge, ground slam; a blood frenzy in its last phase.",
		"Keep range and jump the slam. A timed 1v1: die as often as you like, just beat the clock." },
	{ NULL, "OVS", { 1.00f, 0.55f, 0.15f }, 2,
		"Act 2 boss. Rocket barrages, drones and an energy shield; mortar rain in its last phase.",
		"Clear the drones, wait out the shield, keep moving under the mortars." },
	{ NULL, "HKG", { 0.70f, 0.45f, 1.00f }, 3,
		"Act 3 boss. Blinks behind you, telegraphed rail lances, a gravity well; clones at the end.",
		"Keep turning. Side-step on the lance flash and stay out of the well." },
	{ NULL, "ARC", { 1.00f, 0.30f, 0.05f }, 4,
		"Act 4 boss. Meteor storm, fire nova rings, a burning floor; everything at once when enraged.",
		"Jump the rings, keep off the burning ground, and save your best weapon for the last phase." }
};

int BG_DS_BstKind( int entry ) {
	if ( entry < 0 || entry >= DSB_NUM ) {
		return -1;
	}
	if ( entry < DSB_AFFIX_FIRST ) {
		return DSB_KIND_TYPE;
	}
	return entry < DSB_EXTRA_FIRST ? DSB_KIND_AFFIX : DSB_KIND_EXTRA;
}

int BG_DS_BstId( int entry ) {
	switch ( BG_DS_BstKind( entry ) ) {
	case DSB_KIND_TYPE:		return entry - DSB_TYPE_FIRST + 1;
	case DSB_KIND_AFFIX:	return entry - DSB_AFFIX_FIRST;
	case DSB_KIND_EXTRA:	return entry - DSB_EXTRA_FIRST;
	}
	return -1;
}

int BG_DS_BstEntry( int kind, int id ) {
	switch ( kind ) {
	case DSB_KIND_TYPE:
		return id > DSAR_NONE && id < DSAR_NUM ? DSB_TYPE_FIRST + id - 1 : -1;
	case DSB_KIND_AFFIX:
		return id >= 0 && id < DSAF_NUM ? DSB_AFFIX_FIRST + id : -1;
	case DSB_KIND_EXTRA:
		return id >= 0 && id < DSBX_NUM ? DSB_EXTRA_FIRST + id : -1;
	}
	return -1;
}

const char *BG_DS_BstName( int entry ) {
	int		x;

	if ( entry < 0 || entry >= DSB_NUM ) {
		return "";
	}
	if ( bg_dsBst[entry].name ) {
		return bg_dsBst[entry].name;
	}
	if ( entry < DSB_TYPE_FIRST + DSB_NUM_TYPES ) {
		return ds_types[entry - DSB_TYPE_FIRST + 1].name;		// "SNIPER" .. (the type table's names)
	}
	x = entry - DSB_EXTRA_FIRST;
	if ( x >= DSBX_MINIBOSS && x < DSBX_MINIBOSS + DS_MINIBOSS_NUM ) {
		return ds_miniBosses[x - DSBX_MINIBOSS].name;
	}
	if ( x >= DSBX_BOSS && x < DSBX_BOSS + BOSS_NUM ) {
		return ds_bosses[x - DSBX_BOSS].name;
	}
	return "";
}

// the affix names (en-review: g_ds_affix.c and cg_ds_affix.c each had a copy of these)
const char *BG_DS_AffixName( int bit ) {
	return bit >= 0 && bit < DSAF_NUM ? bg_dsBst[DSB_AFFIX_FIRST + bit].name : "";
}

const char *BG_DS_BstKindName( int entry ) {
	int		x;

	switch ( BG_DS_BstKind( entry ) ) {
	case DSB_KIND_TYPE:		return "ARCHETYPE";
	case DSB_KIND_AFFIX:	return "ELITE AFFIX";
	case DSB_KIND_EXTRA:
		x = entry - DSB_EXTRA_FIRST;
		if ( x == DSBX_CHAMPION ) {
			return "CHAMPIONS";
		}
		return x < DSBX_BOSS ? "AMBUSH MINI-BOSS" : "ACT BOSS";
	}
	return "";
}

int BG_DS_BstByName( const char *s ) {
	const char	*n;
	int			i;

	if ( !s || !s[0] ) {
		return -1;
	}
	if ( s[0] >= '0' && s[0] <= '9' ) {
		i = atoi( s );
		return i >= 0 && i < DSB_NUM ? i : -1;
	}
	for ( i = 0 ; i < DSB_NUM ; i++ ) {
		n = BG_DS_BstName( i );
		if ( !Q_stricmp( s, n ) || ( !Q_stricmpn( n, "THE ", 4 ) && !Q_stricmp( s, n + 4 ) ) ) {
			return i;
		}
	}
	for ( i = 0 ; i < DSB_NUM ; i++ ) {
		n = BG_DS_BstName( i );
		if ( !Q_stricmpn( n, "THE ", 4 ) ) {
			n += 4;
		}
		if ( strlen( s ) >= 3 && !Q_stricmpn( s, n, strlen( s ) ) ) {
			return i;
		}
	}
	return -1;
}
