/*
===========================================================================
REARMED -- DESCENT map variety (ds-maps) data: the map table and the floor variants.
Contract in bg_ds_maps.h.
===========================================================================
*/
#include "../qcommon/q_shared.h"
#include "bg_public.h"

/*
-------------------------------------------------------------------------------
MAPS (append only: the lifetime play counts are stored in this order)
Sizes from tools/map_audit.py (spread of spawns and items, spawn count) and play feel.
-------------------------------------------------------------------------------
*/
const dsMapInfo_t ds_mapInfo[] = {
	{ "q3dm0",			"Introduction",					DSMAP_MEDIUM,	DSMAP_SRC_ID },
	{ "q3dm1",			"Arena Gate",					DSMAP_SMALL,	DSMAP_SRC_ID },
	{ "q3dm2",			"House of Pain",				DSMAP_SMALL,	DSMAP_SRC_ID },
	{ "q3dm3",			"Arena of Death",				DSMAP_SMALL,	DSMAP_SRC_ID },
	{ "q3dm4",			"The Place of Many Deaths",		DSMAP_MEDIUM,	DSMAP_SRC_ID },
	{ "q3dm5",			"The Forgotten Place",			DSMAP_SMALL,	DSMAP_SRC_ID },
	{ "q3dm6",			"The Camping Grounds",			DSMAP_MEDIUM,	DSMAP_SRC_ID },
	{ "q3dm7",			"Temple of Retribution",		DSMAP_MEDIUM,	DSMAP_SRC_ID },
	{ "q3dm8",			"Brimstone Abbey",				DSMAP_MEDIUM,	DSMAP_SRC_ID },
	{ "q3dm9",			"Hero's Keep",					DSMAP_MEDIUM,	DSMAP_SRC_ID },
	{ "q3dm10",			"The Nameless Place",			DSMAP_SMALL,	DSMAP_SRC_ID },
	{ "q3dm11",			"Deva Station",					DSMAP_LARGE,	DSMAP_SRC_ID },
	{ "q3dm12",			"The Dredwerkz",				DSMAP_LARGE,	DSMAP_SRC_ID },
	{ "q3dm13",			"Lost World",					DSMAP_MEDIUM,	DSMAP_SRC_ID },
	{ "q3dm14",			"Grim Dungeons",				DSMAP_MEDIUM,	DSMAP_SRC_ID },
	{ "q3dm15",			"Demon Keep",					DSMAP_MEDIUM,	DSMAP_SRC_ID },
	{ "q3dm16",			"The Bouncy Map",				DSMAP_SMALL,	DSMAP_SRC_ID },
	{ "q3dm17",			"The Longest Yard",				DSMAP_MEDIUM,	DSMAP_SRC_ID },
	{ "q3dm18",			"Space Chamber",				DSMAP_MEDIUM,	DSMAP_SRC_ID },
	{ "q3dm19",			"Apocalypse Void",				DSMAP_MEDIUM,	DSMAP_SRC_ID },
	{ "q3tourney1",		"Powerstation 0218",			DSMAP_SMALL,	DSMAP_SRC_ID },
	{ "q3tourney2",		"The Proving Grounds",			DSMAP_SMALL,	DSMAP_SRC_ID },
	{ "q3tourney3",		"Hell's Gate",					DSMAP_SMALL,	DSMAP_SRC_ID },
	{ "q3tourney4",		"Vertical Vengeance",			DSMAP_SMALL,	DSMAP_SRC_ID },
	{ "q3tourney5",		"Fatal Instinct",				DSMAP_SMALL,	DSMAP_SRC_ID },
	{ "q3tourney6",		"The Very End of You",			DSMAP_SMALL,	DSMAP_SRC_ID },
	{ "q3ctf1",			"Dueling Keeps",				DSMAP_LARGE,	DSMAP_SRC_ID },
	{ "q3ctf2",			"Troubled Waters",				DSMAP_LARGE,	DSMAP_SRC_ID },
	{ "q3ctf3",			"The Stronghold",				DSMAP_LARGE,	DSMAP_SRC_ID },
	{ "q3ctf4",			"Space CTF",					DSMAP_LARGE,	DSMAP_SRC_ID },
	{ "q3tourney6_ctf",	"Across Space",				DSMAP_MEDIUM,	DSMAP_SRC_ID },
	{ "pro-q3dm6",		"The Campgrounds II",			DSMAP_MEDIUM,	DSMAP_SRC_POINT },
	{ "pro-q3dm13",		"Lost World II",				DSMAP_MEDIUM,	DSMAP_SRC_POINT },
	{ "pro-q3tourney2",	"The Proving Grounds II",		DSMAP_MEDIUM,	DSMAP_SRC_POINT },
	{ "pro-q3tourney4",	"Vertical Vengeance II",		DSMAP_SMALL,	DSMAP_SRC_POINT },
	{ "rm_camp",		"The Campgrounds: Rearmed",		DSMAP_MEDIUM,	DSMAP_SRC_REARMED },
	{ "rm_lost",		"Lost World: Rearmed",			DSMAP_MEDIUM,	DSMAP_SRC_REARMED },
	{ "rm_yard",		"The Longest Yard: Rearmed",	DSMAP_MEDIUM,	DSMAP_SRC_REARMED },
	{ "mptourney1",		"The House of Decay",			DSMAP_SMALL,	DSMAP_SRC_TA },
	{ "mptourney2",		"Death Factory",				DSMAP_MEDIUM,	DSMAP_SRC_TA },
	{ "mptourney3",		"Temple of Pain",				DSMAP_SMALL,	DSMAP_SRC_TA },
	{ "mptourney4",		"Evil Playground",				DSMAP_SMALL,	DSMAP_SRC_TA },
	{ "mpteam1",		"Base Siege",					DSMAP_LARGE,	DSMAP_SRC_TA },
	{ "mpteam2",		"Fallout Bunker",				DSMAP_LARGE,	DSMAP_SRC_TA },
	{ "mpteam3",		"Inner Sanctums",				DSMAP_REJECTED,	DSMAP_SRC_TA },	// too big for a frag race of 6 or fewer: half the kill pace of q3ctf
	{ "mpteam4",		"Scornforge",					DSMAP_LARGE,	DSMAP_SRC_TA },
	{ "mpteam5",		"Teamwerkz",					DSMAP_REJECTED,	DSMAP_SRC_TA },	// too big for a frag race of 6 or fewer: half the kill pace of q3ctf
	{ "mpteam6",		"Vortex Portal",				DSMAP_LARGE,	DSMAP_SRC_TA },
	{ "mpteam7",		"The Capture Chamber",			DSMAP_LARGE,	DSMAP_SRC_TA },
	{ "mpteam8",		"Assassin's Roost",				DSMAP_REJECTED,	DSMAP_SRC_TA }	// botlib AAS_LinkEntity stack overflows
};
const int ds_numMapInfo = sizeof( ds_mapInfo ) / sizeof( ds_mapInfo[0] );

int BG_DSM_MapId( const char *name ) {
	int		i;

	for ( i = 0 ; i < ds_numMapInfo ; i++ ) {
		if ( !Q_stricmp( ds_mapInfo[i].name, name ) ) {
			return i;
		}
	}
	return -1;
}

/*
-------------------------------------------------------------------------------
VARIANTS
-------------------------------------------------------------------------------
*/
#define A1	( 1 << 1 )
#define A2	( 1 << 2 )
#define A3	( 1 << 3 )
#define A4	( 1 << 4 )
#define A5	( 1 << 5 )

const dsLightVariant_t ds_lightVariants[DSV_LIGHT_NUM] = {
	{ "",				{ 1.0f, 1.0f, 1.0f },		{ 0, 0, 0, 0 },					{ 0, 0, 0 },			A1|A2|A3|A4|A5,	qfalse },
	{ "NIGHT",			{ 0.56f, 0.62f, 0.86f },	{ 0.0f, 0.01f, 0.05f, 0.55f },	{ 0, 0, 0 },			A1|A2|A3|A4|A5,	qtrue },
	{ "BLOOD MOON",		{ 1.0f, 0.62f, 0.56f },		{ 0.28f, 0.0f, 0.0f, 0.45f },	{ 0.02f, 0, 0 },		A1|A4|A5,		qtrue },
	{ "DUSK",			{ 1.0f, 0.8f, 0.6f },		{ 0.22f, 0.08f, 0.0f, 0.35f },	{ 0.02f, 0.01f, 0 },	A1|A2|A4,		qfalse },
	{ "FOG",			{ 0.9f, 0.93f, 0.96f },		{ 0.55f, 0.58f, 0.62f, 0.5f },	{ 0.09f, 0.09f, 0.1f },	A1|A3|A5,		qfalse },
	{ "SMOG",			{ 0.86f, 0.82f, 0.66f },	{ 0.3f, 0.25f, 0.14f, 0.5f },	{ 0.07f, 0.06f, 0.03f },A2|A4,			qfalse },
	{ "COLD LIGHT",		{ 0.74f, 0.88f, 1.0f },		{ 0.0f, 0.05f, 0.12f, 0.3f },	{ 0.0f, 0.01f, 0.03f },	A2|A3|A5,		qfalse },
	{ "TOXIC",			{ 0.74f, 1.0f, 0.62f },		{ 0.05f, 0.2f, 0.0f, 0.4f },	{ 0.01f, 0.03f, 0 },	A2|A3|A5,		qfalse },
	{ "INFERNO",		{ 1.0f, 0.7f, 0.46f },		{ 0.35f, 0.08f, 0.0f, 0.5f },	{ 0.07f, 0.02f, 0 },	A4|A5,			qfalse },
	{ "FLOODED LIGHT",	{ 1.0f, 0.98f, 0.92f },		{ 0, 0, 0, 0 },					{ 0.13f, 0.12f, 0.1f },	A1|A2|A3,		qfalse }
};

const char *ds_layoutVariantNames[DSV_LAYOUT_NUM] = { "", "ARMS SWAP", "ARMOR SHIFT", "SCRAMBLED" };

int BG_DSM_PickVariant( unsigned int h, int act, qboolean bossOrVault ) {
	int		light, layout, n, i, k;
	int		ok[DSV_LIGHT_NUM];

	light = 0;
	if ( h % 100 < 55 ) {
		n = 0;
		for ( i = 1 ; i < DSV_LIGHT_NUM ; i++ ) {
			if ( ds_lightVariants[i].acts & ( 1 << act ) ) {
				ok[n++] = i;
			}
		}
		if ( n ) {
			light = ok[( h >> 8 ) % n];
		}
	}
	layout = DSV_LAYOUT_NONE;
	k = (int)( ( h >> 16 ) % 100 );
	if ( !bossOrVault ) {
		if ( k < 20 ) {
			layout = DSV_LAYOUT_ARMS;
		} else if ( k < 34 ) {
			layout = DSV_LAYOUT_ARMOR;
		} else if ( k < 45 ) {
			layout = DSV_LAYOUT_BOTH;
		}
	}
	return light | ( layout << 4 );
}

const char *BG_DSM_VariantLabel( int variant ) {
	static char	buf[48];
	const char	*l = ds_lightVariants[DSV_LIGHT( variant ) % DSV_LIGHT_NUM].name;
	const char	*o = ds_layoutVariantNames[DSV_LAYOUT( variant )];

	Com_sprintf( buf, sizeof( buf ), "%s%s%s", l, ( l[0] && o[0] ) ? "  " : "", o );
	return buf;
}
