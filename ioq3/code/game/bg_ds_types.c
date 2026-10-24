/*
===========================================================================
REARMED -- round 4 ENEMY ARCHETYPES (en-types): the shared type table (bg_ds_types.h).
===========================================================================
*/
#include "../qcommon/q_shared.h"
#include "bg_public.h"
#include "bg_ds_types.h"

/*
name, ^colour, rgb, model, weapon, backup, health, damage, speed, armour, first act, range

Budget notes (hp x dmg, then the trick that pays for the rest):
SNIPER     0.80 x 0.90  a rail from far, but a 0.7 s glint before every shot and it moves after
RUSHER     0.85 x 0.95  fast (1.18) and close, never retreats
MEDIC      0.85 x 0.60  its gun is weak; heals one wounded ally 12 hp/s
SHIELD     0.90 x 0.85  front blocks 80% of direct hits while up; can't shoot with it up
KAMIKAZE   0.50 x 1.00  gauntlet only; one 85-damage blast with a 1 s warning
BLINKER    0.80 x 0.90  flanking teleports (never within 400 of the player)
SUMMONER   0.85 x 0.70  raises up to 2 weak skeletons (fodder: half health, 0.55 damage)
MIMIC      1.00 x 0.85  the player's best weapon at the player's level
JUGGERNAUT 1.80 x 0.65  +50 armour, slow (0.78), hard to stagger
*/
const dsTypeInfo_t ds_types[DSAR_NUM] = {
	{ "",			"^7", { 1.00f, 1.00f, 1.00f }, "",					WP_NONE,			WP_NONE,			1.00f, 1.00f, 1.00f, 0,  1, 0 },
	{ "SNIPER",		"^1", { 1.00f, 0.18f, 0.12f }, "visor/default",		WP_RAILGUN,			WP_MACHINEGUN,		0.80f, 0.90f, 1.00f, 0,  1, 1100 },
	{ "RUSHER",		"^3", { 1.00f, 0.72f, 0.15f }, "slash/default",		WP_SHOTGUN,			WP_GAUNTLET,		0.85f, 0.95f, 1.18f, 0,  1, 140 },
	{ "MEDIC",		"^2", { 0.25f, 1.00f, 0.40f }, "lucy/default",		WP_MACHINEGUN,		WP_NONE,			0.85f, 0.60f, 1.00f, 0,  2, 700 },
	{ "SHIELD",		"^5", { 0.45f, 0.85f, 1.00f }, "keel/default",		WP_PLASMAGUN,		WP_MACHINEGUN,		0.90f, 0.85f, 0.92f, 0,  1, 320 },
	{ "KAMIKAZE",	"^1", { 1.00f, 0.42f, 0.05f }, "orbb/default",		WP_GAUNTLET,		WP_NONE,			0.50f, 1.00f, 1.25f, 0,  1, 0 },
	{ "BLINKER",	"^6", { 0.95f, 0.35f, 1.00f }, "mynx/default",		WP_LIGHTNING,		WP_SHOTGUN,			0.80f, 0.90f, 1.00f, 0,  2, 520 },
	{ "SUMMONER",	"^7", { 0.85f, 0.88f, 0.72f }, "ranger/wrack",		WP_GRENADE_LAUNCHER,	WP_MACHINEGUN,	0.85f, 0.70f, 0.95f, 0,  3, 650 },
	{ "MIMIC",		"^7", { 0.70f, 0.72f, 0.85f }, "*",					WP_NONE,			WP_NONE,			1.00f, 0.85f, 1.00f, 0,  3, 0 },
	{ "JUGGERNAUT",	"^4", { 0.55f, 0.62f, 0.75f }, "tankjr/default",	WP_CHAINGUN,		WP_ROCKET_LAUNCHER,	1.80f, 0.65f, 0.78f, 50, 2, 450 }
};

const char *BG_DS_TypeName( int arch ) {
	if ( arch <= DSAR_NONE || arch >= DSAR_NUM ) {
		return "";
	}
	return ds_types[arch].name;
}

int BG_DS_TypeByName( const char *s ) {
	int		i;

	if ( !s || !s[0] ) {
		return -1;
	}
	if ( s[0] >= '0' && s[0] <= '9' ) {
		i = atoi( s );
		return i >= 0 && i < DSAR_NUM ? i : -1;
	}
	if ( !Q_stricmp( s, "none" ) || !Q_stricmp( s, "plain" ) || !Q_stricmp( s, "-" ) ) {
		return 0;
	}
	for ( i = 1 ; i < DSAR_NUM ; i++ ) {
		if ( !Q_stricmp( s, ds_types[i].name ) ) {
			return i;
		}
	}
	// short forms: "jugg", "kami", "shield" ...
	for ( i = 1 ; i < DSAR_NUM ; i++ ) {
		if ( !Q_stricmpn( s, ds_types[i].name, 4 ) ) {
			return i;
		}
	}
	return -1;
}

/*
===============================================================================

TYPE ADAPTATION (en-tune): see bg_ds_types.h

===============================================================================
*/
void BG_DS_TypeAdaptRoll( float *w, const int *runDeaths ) {
	int		a;

	w[0] = 0;
	for ( a = 1 ; a < DSAR_NUM ; a++ ) {
		w[a] = w[a] * DST_ADAPT_DECAY + ( runDeaths ? runDeaths[a] : 0 );
		if ( w[a] < 0.05f ) {
			w[a] = 0;
		}
		if ( w[a] > 99 ) {
			w[a] = 99;
		}
	}
}

int BG_DS_TypeAdaptPick( const float *w, int *out ) {
	int		a, n, k, best;

	n = 0;
	for ( k = 0 ; k < DST_ADAPT_MAX ; k++ ) {
		best = 0;
		for ( a = 1 ; a < DSAR_NUM ; a++ ) {
			if ( w[a] < DST_ADAPT_MIN || ( n > 0 && out[0] == a ) ) {
				continue;
			}
			if ( !best || w[a] > w[best] ) {
				best = a;		// ties: the lower id (stable)
			}
		}
		if ( !best ) {
			break;
		}
		out[n++] = best;
	}
	return n;
}

const char *BG_DS_TypeAdaptHint( int arch ) {
	switch ( arch ) {
	case DSAR_SNIPER:		return "their glint is shorter. Strafe early.";
	case DSAR_RUSHER:		return "they charge in from the side. Watch your flanks.";
	case DSAR_MEDIC:		return "they hang further back. Push past the front.";
	case DSAR_SHIELD:		return "their shield covers more. Circle wide.";
	case DSAR_KAMIKAZE:		return "they come in pairs. Count the beeps.";
	case DSAR_BLINKER:		return "they blink more often. Keep turning.";
	case DSAR_SUMMONER:		return "they raise the dead faster. Gib the corpses.";
	case DSAR_MIMIC:		return "there are more of them. Switch guns to throw them.";
	case DSAR_JUGGERNAUT:	return "they fire rockets sooner. Close in or take cover.";
	default:				return "";
	}
}

const char *BG_DS_TypePlural( int arch ) {
	switch ( arch ) {
	case DSAR_SNIPER:		return "Snipers";
	case DSAR_RUSHER:		return "Rushers";
	case DSAR_MEDIC:		return "Medics";
	case DSAR_SHIELD:		return "Shield bearers";
	case DSAR_KAMIKAZE:		return "Kamikazes";
	case DSAR_BLINKER:		return "Blinkers";
	case DSAR_SUMMONER:		return "Summoners";
	case DSAR_MIMIC:		return "Mimics";
	case DSAR_JUGGERNAUT:	return "Juggernauts";
	default:				return "";
	}
}

// "<floor>:<arch>,<arch>" -> bit mask of the adapted archs (0 none); list (may be NULL): the archs
// in the published order (heaviest first), at most max, *numOut of them
int BG_DS_TypeAdaptParse( const char *s, int *floorOut, int *list, int max, int *numOut ) {
	int		mask = 0, v, num = 0;

	if ( floorOut ) {
		*floorOut = 0;
	}
	if ( numOut ) {
		*numOut = 0;
	}
	if ( !s || !s[0] ) {
		return 0;
	}
	if ( floorOut ) {
		*floorOut = atoi( s );
	}
	while ( *s && *s != ':' ) {
		s++;
	}
	while ( *s ) {
		s++;
		v = atoi( s );
		if ( v > 0 && v < DSAR_NUM ) {
			mask |= 1 << v;
			if ( list && num < max ) {
				list[num++] = v;
			}
		}
		while ( *s && *s != ',' ) {
			s++;
		}
	}
	if ( numOut ) {
		*numOut = num;
	}
	return mask;
}
