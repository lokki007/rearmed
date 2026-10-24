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

DESCENT (the roguelike mode, g_rearmMode 5)

Hub, new run, permanent upgrades, records, the branching floor map,
card picks and the run-over screen.

State lives in two key/value files in the mod home directory:
rearmed_run.txt (the active run) and rearmed_meta.txt (permanent).
The UI starts a run, picks the next node, writes the node_* keys and
launches the level; the game module reads the run file at level start,
writes the result, offers cards and banks skulls at the end of the run,
then runs `rlmap` (next floor) or `rlover` (the run is over).

The floor map is generated from the run seed, one layer per floor, so
it is endless; the "map" key only holds the node picked on each floor.

=======================================================================
*/


#include "ui_local.h"


#include "ui_ds_meta.h"		// DESCENT v2 META: shared defines, types and helpers (ui_ds_meta.c)

#include "ui_ds_maps.h"		// ds-maps: RL_MAX_*, RLN_* and rlLayer_t, the map variety (ui_ds_maps.c)

#include "../game/bg_ds_daily.h"	// en-daily: DSD_RUN_FILE (the daily challenge's own run file)

#define RL_TIMELIMIT		10
#define RL_BOSS_TIMELIMIT	12
#define RL_VAULT_TIMELIMIT	3		// the game ends a vault floor after DS_VAULT_TIME

// card menu modes
#define RLCM_START			0		// the free starting ability (up_startcard)
#define RLCM_OFFER			1		// a level reward the game offered but that was never picked

// map layout (640x480 virtual; rl_ext widens it on wide screens)
#define RL_PANEL_TOP		70
#define RL_PANEL_BOTTOM		438
#define RL_PANEL_W			152
#define RL_MAP_TOP			102		// the floor map runs down the screen: deeper floors lower
#define RL_MAP_BOTTOM		418


// rlLayer_t: ui_ds_maps.h

// the parsed run file
typedef struct {
	int		active;
	int		seed;
	int		difficulty;
	int		lives;
	int		floor;
	int		skulls;
	int		kills;
	int		time;
	int		rerolls;
	int		path[RL_MAX_LAYERS];
	int		pathLen;
	int		cards[RL_MAX_CARDS];
	int		numCards;
	int		offer[RL_MAX_OFFER];
	int		numOffer;

	// derived
	int		layer;					// the layer the next node is picked in (== floor)
	int		reach[RL_MAX_NODES];
	int		numReach;
} rlState_t;



// the permanent upgrades: ds_upgrades[] in bg_descent.c (the tree menu is in ui_ds_meta.c)

// the run keys the game applies, copied from the meta file when a run starts
static const char *rl_runUpgradeKeys[] = { "up_health", "up_speed", "up_damage", "up_armor", "up_skulls", "up_cards" };

const char *rl_skillNames[5] = { "I CAN WIN", "BRING IT ON", "HURT ME PLENTY", "HARDCORE", "NIGHTMARE!" };
static const char *rl_nodeNames[RLN_NUM] = { "FIGHT", "ELITE", "EVENT", "BOSS", "???" };
static const char *rl_nodeKeys[RLN_NUM] = { "fight", "elite", "event", "boss", "vault" };
static const char *rl_nodeDescs[RLN_NUM] = {
	"A frag race against the bots.",
	"One bot is an elite: tougher and deadlier.",
	"A frag race under a forced random event.",
	"The act's master waits here.",		// (RL_NodeDesc writes the boss line for g_dsBossMode)
	"Something glitters down there. Nobody guards it."
};
static const char *rl_nodeRewards[RLN_NUM] = {
	"Pick 1 of 3 cards, skulls.",
	"Better cards, double skulls.",
	"Pick a card, +50% skulls.",
	"A big chest, red gems, a boss card offer, triple skulls.",
	"45 seconds of falling gems and a free chest. No cards."
};
// en-screens: the daily challenge pays no skulls (en-daily), so its map doesn't promise any
static const char *rl_nodeRewardsDaily[RLN_NUM] = {
	"Pick 1 of 3 cards.",
	"Better cards.",
	"Pick a card.",
	"A big chest, red gems and a boss card offer.",
	"45 seconds of falling gems and a free chest. No cards."
};
static const char *rl_kindNames[8] = { "ABILITY", "ALT-FIRE", "STAT", "PERK", "PASSIVE", "WEAPON", "SECRET", "LEGEND" };

// abilities with no prerequisite: the free starting card is one of these
static const int rl_startCards[] = { RLC_DOUBLEJUMP, RLC_WALLJUMP, RLC_WALLRUN, RLC_DASH, RLC_GRAPPLE, RLC_ZIPLINE, RLC_SLIDE, RLC_SLAM };

vec4_t rl_colBack		= { 0.035f, 0.016f, 0.014f, 1.0f };
vec4_t rl_colShade		= { 0.02f, 0.008f, 0.006f, 0.9f };
vec4_t rl_colPanel		= { 0.09f, 0.03f, 0.025f, 0.85f };
vec4_t rl_colPanelHi		= { 0.2f, 0.06f, 0.035f, 0.92f };
vec4_t rl_colLine		= { 0.42f, 0.08f, 0.05f, 1.0f };
vec4_t rl_colOrange		= { 1.0f, 0.42f, 0.1f, 1.0f };
vec4_t rl_colBone		= { 0.93f, 0.87f, 0.74f, 1.0f };
vec4_t rl_colDim			= { 0.6f, 0.53f, 0.46f, 1.0f };
vec4_t rl_colDark		= { 0.32f, 0.27f, 0.24f, 1.0f };
vec4_t rl_colBanner		= { 0.82f, 0.1f, 0.05f, 1.0f };
vec4_t rl_colGood		= { 0.45f, 0.9f, 0.4f, 1.0f };
vec4_t rl_colBad			= { 1.0f, 0.25f, 0.2f, 1.0f };
vec4_t rl_colBlack		= { 0, 0, 0, 0.85f };

static vec4_t rl_nodeColors[RLN_NUM] = {
	{ 0.86f, 0.78f, 0.62f, 1.0f },
	{ 1.0f, 0.35f, 0.12f, 1.0f },
	{ 0.35f, 0.72f, 1.0f, 1.0f },
	{ 1.0f, 0.1f, 0.08f, 1.0f },
	{ 1.0f, 0.8f, 0.25f, 1.0f }
};
static vec4_t rl_kindColors[8] = {
	{ 1.0f, 0.6f, 0.15f, 1.0f },
	{ 0.4f, 0.8f, 1.0f, 1.0f },
	{ 0.5f, 0.9f, 0.4f, 1.0f },
	{ 0.8f, 0.5f, 1.0f, 1.0f },
	{ 0.25f, 0.9f, 0.85f, 1.0f },	// passive (DESCENT v2)
	{ 1.0f, 0.42f, 0.3f, 1.0f },	// weapon level
	{ 1.0f, 0.84f, 0.25f, 1.0f },	// secret
	{ 1.0f, 0.62f, 0.1f, 1.0f }		// legendary (round 3, ds-cards)
};
static vec4_t rl_rarityColors[4] = {
	{ 0.55f, 0.5f, 0.45f, 1.0f },
	{ 0.3f, 0.6f, 1.0f, 1.0f },
	{ 1.0f, 0.75f, 0.2f, 1.0f },
	{ 1.0f, 0.62f, 0.1f, 1.0f }
};

static const char *rl_nodeIconNames[RLN_NUM] = { "icons/iconw_rocket", "menu/art/skill4", "icons/quad", "menu/art/skill5", "icons/iconh_mega" };


rlKV_t		rl_run;
rlKV_t		rl_meta;
static rlState_t	rl;
static char			rl_fileBuf[RL_MAX_KV * ( RL_KEY_LEN + RL_VAL_LEN + 4 ) + 256];

static char			rl_maps[RL_MAX_MAPS][32];
static int			rl_numMaps = -1;		// -1: pool not built yet
static qhandle_t	rl_mapShots[RL_MAX_MAPS];
static qboolean		rl_mapShotTried[RL_MAX_MAPS];
static int			rl_actMaps[DS_ACT_ABYSS + 1][RL_MAX_MAPS];	// ACTS: each act's pool (indexes into rl_maps)
static int			rl_actNumMaps[DS_ACT_ABYSS + 1];

static rlLayer_t	rl_layers[RL_MAX_LAYERS];
static int			rl_layersBuilt;
static int			rl_layersSeed;

qhandle_t	rl_shRing;
qhandle_t	rl_shGlow;
static qhandle_t	rl_shVignette;
static qhandle_t	rl_nodeIcons[RLN_NUM];
qhandle_t	rl_skillIcons[5];
sfxHandle_t	rl_buySound;

float		rl_ext;					// virtual units the layout spreads into on wide screens
qboolean		rl_hubDirty;


/*
=======================================================================

KEY / VALUE FILES

=======================================================================
*/

static void RL_KVParseLine( rlKV_t *kv, char *line ) {
	char	*p;
	char	*val;
	int		len;

	// trim the end (\r from CRLF files) and the start
	len = strlen( line );
	while ( len > 0 && ( line[len - 1] == '\r' || line[len - 1] == ' ' || line[len - 1] == '\t' ) ) {
		line[--len] = 0;
	}
	while ( *line == ' ' || *line == '\t' ) {
		line++;
	}
	if ( !*line || ( line[0] == '/' && line[1] == '/' ) ) {
		return;
	}
	if ( kv->num >= RL_MAX_KV ) {
		return;
	}

	p = line;
	while ( *p && *p != ' ' && *p != '\t' ) {
		p++;
	}
	val = p;
	if ( *p ) {
		*p = 0;
		val = p + 1;
		while ( *val == ' ' || *val == '\t' ) {
			val++;
		}
	}

	Q_strncpyz( kv->key[kv->num], line, RL_KEY_LEN );
	Q_strncpyz( kv->val[kv->num], val, RL_VAL_LEN );
	kv->num++;
}

void RL_KVLoad( rlKV_t *kv, const char *name ) {
	fileHandle_t	f;
	int				len;
	char			*p;
	char			*line;

	kv->num = 0;
	f = 0;
	len = trap_FS_FOpenFile( name, &f, FS_READ );
	if ( !f ) {
		return;
	}
	if ( len <= 0 ) {
		trap_FS_FCloseFile( f );
		return;
	}
	if ( len > (int)sizeof( rl_fileBuf ) - 1 ) {
		len = sizeof( rl_fileBuf ) - 1;
	}
	trap_FS_Read( rl_fileBuf, len, f );
	rl_fileBuf[len] = 0;
	trap_FS_FCloseFile( f );

	p = rl_fileBuf;
	while ( *p ) {
		line = p;
		while ( *p && *p != '\n' ) {
			p++;
		}
		if ( *p ) {
			*p++ = 0;
		}
		RL_KVParseLine( kv, line );
	}
}

void RL_KVSave( rlKV_t *kv, const char *name ) {
	fileHandle_t	f;
	int				i;
	int				len;

	f = 0;
	len = 0;
	rl_fileBuf[0] = 0;
	for ( i = 0; i < kv->num; i++ ) {
		if ( kv->val[i][0] ) {
			Com_sprintf( rl_fileBuf + len, sizeof( rl_fileBuf ) - len, "%s %s\n", kv->key[i], kv->val[i] );
		}
		else {
			Com_sprintf( rl_fileBuf + len, sizeof( rl_fileBuf ) - len, "%s\n", kv->key[i] );
		}
		len += strlen( rl_fileBuf + len );
	}

	trap_FS_FOpenFile( name, &f, FS_WRITE );
	if ( !f ) {
		Com_Printf( S_COLOR_YELLOW "DESCENT: can't write %s\n", name );
		return;
	}
	trap_FS_Write( rl_fileBuf, len, f );
	trap_FS_FCloseFile( f );
	rl_hubDirty = qtrue;
}

const char *RL_KVGet( rlKV_t *kv, const char *key ) {
	int		i;

	for ( i = 0; i < kv->num; i++ ) {
		if ( !Q_stricmp( kv->key[i], key ) ) {
			return kv->val[i];
		}
	}
	return "";
}

int RL_KVInt( rlKV_t *kv, const char *key ) {
	return atoi( RL_KVGet( kv, key ) );
}

void RL_KVSet( rlKV_t *kv, const char *key, const char *val ) {
	int		i;

	for ( i = 0; i < kv->num; i++ ) {
		if ( !Q_stricmp( kv->key[i], key ) ) {
			Q_strncpyz( kv->val[i], val, RL_VAL_LEN );
			return;
		}
	}
	if ( kv->num >= RL_MAX_KV ) {
		return;
	}
	Q_strncpyz( kv->key[kv->num], key, RL_KEY_LEN );
	Q_strncpyz( kv->val[kv->num], val, RL_VAL_LEN );
	kv->num++;
}

void RL_KVSetInt( rlKV_t *kv, const char *key, int val ) {
	RL_KVSet( kv, key, va( "%i", val ) );
}


/*
=================
RL_ParseInts

Parses "12,25,40" or "1.0.2"; returns the count
=================
*/
int RL_ParseInts( const char *s, int *out, int max ) {
	int		n;

	n = 0;
	while ( *s && n < max ) {
		while ( *s && ( *s < '0' || *s > '9' ) && *s != '-' ) {
			s++;
		}
		if ( !*s ) {
			break;
		}
		out[n++] = atoi( s );
		if ( *s == '-' ) {
			s++;
		}
		while ( *s >= '0' && *s <= '9' ) {
			s++;
		}
	}
	return n;
}


/*
=================
RL_LoadRun / RL_LoadMeta
=================
*/
void RL_LoadRun( void ) {
	int		i;
	int		n;

	RL_KVLoad( &rl_run, UI_DS_DailyOn() ? DSD_RUN_FILE : RL_RUN_FILE );		// en-daily: the daily's own run file

	rl.active = RL_KVInt( &rl_run, "active" );
	rl.seed = RL_KVInt( &rl_run, "seed" );
	rl.difficulty = RL_KVInt( &rl_run, "difficulty" );
	if ( rl.difficulty < 1 ) {
		rl.difficulty = 1;
	}
	if ( rl.difficulty > 5 ) {
		rl.difficulty = 5;
	}
	rl.lives = RL_KVInt( &rl_run, "lives" );
	rl.floor = RL_KVInt( &rl_run, "floor" );
	if ( rl.floor < 0 ) {
		rl.floor = 0;
	}
	rl.skulls = RL_KVInt( &rl_run, "skulls" );
	rl.kills = RL_KVInt( &rl_run, "kills" );
	rl.time = RL_KVInt( &rl_run, "time" );
	rl.rerolls = RL_KVInt( &rl_run, "rerolls" );
	rl.pathLen = RL_ParseInts( RL_KVGet( &rl_run, "map" ), rl.path, RL_MAX_LAYERS );

	n = RL_ParseInts( RL_KVGet( &rl_run, "cards" ), rl.cards, RL_MAX_CARDS );
	rl.numCards = 0;
	for ( i = 0; i < n; i++ ) {
		if ( rl.cards[i] >= 0 && rl.cards[i] < RLC_NUM ) {
			rl.cards[rl.numCards++] = rl.cards[i];
		}
	}

	n = RL_ParseInts( RL_KVGet( &rl_run, "offer" ), rl.offer, RL_MAX_OFFER );
	rl.numOffer = 0;
	for ( i = 0; i < n; i++ ) {
		if ( rl.offer[i] >= 0 && rl.offer[i] < RLC_NUM && bg_rlCardNames[rl.offer[i]][0] ) {
			rl.offer[rl.numOffer++] = rl.offer[i];
		}
	}
}

void RL_LoadMeta( void ) {
	if ( UI_DS_DailyBlankMeta() ) {
		return;		// en-daily: a blank profile while the daily is on (no upgrades, unlocks, lifetime counts)
	}
	RL_KVLoad( &rl_meta, RL_META_FILE );
}

void RL_SaveRun( void ) {
	RL_KVSave( &rl_run, UI_DS_DailyOn() ? DSD_RUN_FILE : RL_RUN_FILE );		// en-daily
}

void RL_SaveMeta( void ) {
	if ( UI_DS_DailyOn() ) {
		return;		// en-daily: the daily never writes the player's meta
	}
	if ( !RL_KVGet( &rl_meta, "version" )[0] ) {
		RL_KVSet( &rl_meta, "version", "1" );
	}
	RL_KVSave( &rl_meta, RL_META_FILE );
}

int RL_BaseLives( int difficulty ) {
	if ( difficulty <= 2 ) {
		return 3;
	}
	if ( difficulty == 3 ) {
		return 2;
	}
	return 1;
}


/*
=======================================================================

CVARS AND LAUNCHING

=======================================================================
*/

void RL_RegisterCvars( void ) {
	trap_Cvar_Register( NULL, "ui_rlPrevMode", "0", CVAR_ARCHIVE );
	trap_Cvar_Register( NULL, "ui_rlOpenHub", "0", 0 );
	UI_DS_EconRegisterCvars();		// ds-economy: g_fragMult
	UI_DSM_RegisterCvars();			// ds-maps: g_dsVariants
}

qboolean RL_InGame( void ) {
	uiClientState_t	cstate;

	trap_GetClientState( &cstate );
	return ( cstate.connState >= CA_CONNECTED );
}


/*
=================
UI_RL_RestoreMode

Normal games stay normal: puts back the g_rearmMode the player had
before the descent started. Called from the main menu and the
skirmish / single player start paths.
=================
*/
void UI_RL_RestoreMode( void ) {
	int		prev;

	if ( (int)trap_Cvar_VariableValue( "g_rearmMode" ) != RM_MODE_ROGUELIKE ) {
		return;
	}
	RL_RegisterCvars();
	prev = (int)trap_Cvar_VariableValue( "ui_rlPrevMode" );
	if ( prev < 0 || prev >= RM_NUM_MODES || prev == RM_MODE_ROGUELIKE ) {
		prev = RM_MODE_NORMAL;
	}
	trap_Cvar_SetValue( "g_rearmMode", prev );
}

static void RL_SetModeForRun( void ) {
	int		cur;

	RL_RegisterCvars();
	cur = (int)trap_Cvar_VariableValue( "g_rearmMode" );
	if ( cur != RM_MODE_ROGUELIKE ) {
		trap_Cvar_SetValue( "ui_rlPrevMode", cur );
	}
	trap_Cvar_SetValue( "g_rearmMode", RM_MODE_ROGUELIKE );
}


/*
=======================================================================

MAP POOL AND FLOOR GENERATION

=======================================================================
*/

static unsigned int RL_Hash( unsigned int a, unsigned int b, unsigned int c ) {
	unsigned int	h;

	h = a * 0x9E3779B1u + b * 0x85EBCA77u + c * 0xC2B2AE3Du + 0x27D4EB2Fu;
	h ^= h >> 15;
	h *= 0x2C1B3C6Du;
	h ^= h >> 12;
	h *= 0x297A2D39u;
	h ^= h >> 15;
	return h;
}

static qboolean RL_MapAllowed( const char *name ) {
	const char	*info;
	int			n;

	if ( !UI_DSM_HasAAS( name ) ) {
		return qfalse;		// ds-maps: no .aas, the bots can't move
	}
	if ( BG_DSM_MapId( name ) >= 0 && ds_mapInfo[BG_DSM_MapId( name )].size != DSMAP_REJECTED ) {
		return qtrue;		// ds-maps: the DESCENT map table (bg_ds_maps.c), CTF / tourney / Team Arena included
	}
	if ( !Q_stricmpn( name, "q3dm", 4 ) ) {
		if ( name[4] < '0' || name[4] > '9' ) {
			return qfalse;
		}
		n = atoi( name + 4 );
		return ( n >= 1 && n <= 19 );
	}
	if ( !Q_stricmpn( name, "rm_", 3 ) ) {
		return qtrue;
	}
	if ( strstr( name, "ctf" ) || strstr( name, "tourney" ) || !Q_stricmpn( name, "mp", 2 ) ) {
		return qfalse;
	}
	// anything else must say it's a free-for-all map
	info = UI_GetArenaInfoByMap( name );
	if ( !info ) {
		return qfalse;
	}
	return ( strstr( Info_ValueForKey( info, "type" ), "ffa" ) != NULL );
}

/*
=================
RL_BuildActPools

ACTS: every act draws its maps from its own pool (ds_acts[].maps) as far as they are
installed; an act with fewer than 3 of them, and the Abyss, use every map
=================
*/
static void RL_BuildActPools( void ) {
	char		name[32];
	const char	*p;
	int			a, i, n;

	for ( a = 0; a <= DS_ACT_ABYSS; a++ ) {
		n = 0;
		p = ds_acts[a].maps;
		while ( a >= 1 && a <= DS_NUM_ACTS && p && *p ) {
			while ( *p == ' ' ) {
				p++;
			}
			for ( i = 0; *p && *p != ' ' && i < (int)sizeof( name ) - 1; i++ ) {
				name[i] = *p++;
			}
			name[i] = 0;
			if ( !name[0] ) {
				break;
			}
			for ( i = 0; i < rl_numMaps; i++ ) {
				if ( !Q_stricmp( rl_maps[i], name ) ) {
					rl_actMaps[a][n++] = i;
					break;
				}
			}
		}
		if ( n < 3 ) {
			for ( n = 0; n < rl_numMaps; n++ ) {
				rl_actMaps[a][n] = n;
			}
		}
		rl_actNumMaps[a] = n;
	}
}

static void RL_BuildMapPool( void ) {
	static char	list[8192];
	char		name[64];
	char		tmp[32];
	char		*p;
	int			count;
	int			i;
	int			j;
	int			len;

	if ( rl_numMaps >= 0 ) {
		return;
	}
	rl_numMaps = 0;

	count = trap_FS_GetFileList( "maps", ".bsp", list, sizeof( list ) );
	p = list;
	for ( i = 0; i < count && rl_numMaps < RL_MAX_MAPS; i++ ) {
		len = strlen( p );
		COM_StripExtension( p, name, sizeof( name ) );
		p += len + 1;
		Q_strlwr( name );
		if ( strlen( name ) >= sizeof( rl_maps[0] ) || !RL_MapAllowed( name ) ) {
			continue;
		}
		for ( j = 0; j < rl_numMaps; j++ ) {
			if ( !Q_stricmp( rl_maps[j], name ) ) {
				break;
			}
		}
		if ( j < rl_numMaps ) {
			continue;
		}
		Q_strncpyz( rl_maps[rl_numMaps++], name, sizeof( rl_maps[0] ) );
	}

	if ( !rl_numMaps ) {
		Q_strncpyz( rl_maps[rl_numMaps++], "q3dm17", sizeof( rl_maps[0] ) );
	}

	// sorted, so a seed always makes the same floors with the same maps installed
	for ( i = 1; i < rl_numMaps; i++ ) {
		for ( j = i; j > 0 && Q_stricmp( rl_maps[j - 1], rl_maps[j] ) > 0; j-- ) {
			Q_strncpyz( tmp, rl_maps[j], sizeof( tmp ) );
			Q_strncpyz( rl_maps[j], rl_maps[j - 1], sizeof( tmp ) );
			Q_strncpyz( rl_maps[j - 1], tmp, sizeof( tmp ) );
		}
	}

	for ( i = 0; i < RL_MAX_MAPS; i++ ) {
		rl_mapShots[i] = 0;
		rl_mapShotTried[i] = qfalse;
	}
	RL_BuildActPools();
	UI_DSM_SetPool( rl_maps, rl_numMaps, rl_actMaps, rl_actNumMaps );		// ds-maps
}

static qhandle_t RL_MapShot( int m ) {
	if ( m < 0 || m >= rl_numMaps ) {
		return 0;
	}
	if ( !rl_mapShotTried[m] ) {
		rl_mapShotTried[m] = qtrue;
		rl_mapShots[m] = trap_R_RegisterShaderNoMip( va( "levelshots/%s", rl_maps[m] ) );
	}
	return rl_mapShots[m];
}

static const char *RL_MapLongName( int m ) {
	const char	*info;
	const char	*s;

	if ( m < 0 || m >= rl_numMaps ) {
		return "";
	}
	info = UI_GetArenaInfoByMap( rl_maps[m] );
	if ( info ) {
		s = Info_ValueForKey( info, "longname" );
		if ( s[0] ) {
			return s;
		}
	}
	s = UI_DSM_LongName( rl_maps[m] );		// ds-maps
	if ( s ) {
		return s;
	}
	return rl_maps[m];
}


/*
=================
RL_BuildLayers

Floor f (1-based) is layer f-1. ACTS: four acts of DS_FLOORS_PER_ACT floors, the last floor
of an act is its boss (one node), then the endless Abyss with a boss every 6th floor. The
first floor of every act has 3 nodes, the others 2-4. Maps come from the act's pool. Now and
then an act hides a VAULT ("???") on one of its middle floors. Links between layers never
cross: a monotone walk from the left-most pair to the right-most pair.
=================
*/
static void RL_BuildLayers( int seed, int count ) {
	rlLayer_t	*ly;
	rlLayer_t	*prev;
	int			L;
	int			f;
	int			i;
	int			j;
	int			k;
	int			n;
	int			m;
	int			r;
	int			tries;
	int			used;
	int			nonElite;
	int			act;
	int			pool;
	unsigned	s;

	if ( count > RL_MAX_LAYERS ) {
		count = RL_MAX_LAYERS;
	}
	RL_BuildMapPool();
	if ( seed == rl_layersSeed && count <= rl_layersBuilt ) {
		UI_DSM_Assign( rl_layers, rl_layersBuilt, seed );		// ds-maps: no map twice in a run
		return;
	}
	rl_layersSeed = seed;
	rl_layersBuilt = count;
	s = (unsigned)seed;

	memset( rl_layers, 0, sizeof( rl_layers ) );

	// node counts
	for ( L = 0; L < count; L++ ) {
		f = L + 1;
		if ( DS_IsBossFloor( f ) ) {
			n = 1;
		}
		else if ( ( f - 1 ) % DS_FLOORS_PER_ACT == 0 ) {
			n = 3;
		}
		else {
			n = 2 + (int)( RL_Hash( s, L, 1 ) % 3 );
		}
		rl_layers[L].n = n;
	}

	// node contents
	for ( L = 0; L < count; L++ ) {
		f = L + 1;
		ly = &rl_layers[L];
		prev = ( L > 0 ) ? &rl_layers[L - 1] : NULL;
		nonElite = 0;
		act = DS_ActForFloor( f );
		pool = rl_actNumMaps[act] > 0 ? rl_actNumMaps[act] : 1;

		for ( i = 0; i < ly->n; i++ ) {
			r = (int)( RL_Hash( s, L, 10 + i ) % 100 );
			if ( DS_IsBossFloor( f ) ) {
				ly->type[i] = RLN_BOSS;
			}
			else if ( L == 0 ) {
				ly->type[i] = RLN_FIGHT;
			}
			else if ( f >= 3 && r < 18 ) {
				ly->type[i] = RLN_ELITE;
			}
			else if ( r < 43 ) {
				ly->type[i] = RLN_EVENT;
			}
			else {
				ly->type[i] = RLN_FIGHT;
			}
			if ( ly->type[i] != RLN_ELITE ) {
				nonElite++;
			}

			// a map of the act's pool not used on this floor or the one before
			k = (int)( RL_Hash( s, L, 20 + i ) % pool );
			for ( tries = 0; tries < pool; tries++ ) {
				used = 0;
				for ( j = 0; j < i; j++ ) {
					if ( ly->map[j] == rl_actMaps[act][k] ) {
						used = 1;
					}
				}
				if ( prev ) {
					for ( j = 0; j < prev->n; j++ ) {
						if ( prev->map[j] == rl_actMaps[act][k] ) {
							used = 1;
						}
					}
				}
				if ( !used ) {
					break;
				}
				k = ( k + 1 ) % pool;
			}
			ly->map[i] = rl_actNumMaps[act] > 0 ? rl_actMaps[act][k] : 0;

			// ~3-4 minutes a floor: bots 2 -> 5, frags 8 -> 16 over the 24 floors (a little more in the Abyss)
			ly->bots[i] = 2 + ( f - 1 ) / 4;
			if ( ly->bots[i] > 5 ) {
				ly->bots[i] = 5;
			}
			ly->frags[i] = 8 + ( f - 1 ) / 2;
			if ( ly->type[i] == RLN_ELITE ) {
				ly->frags[i] += 1;
			}
			if ( ly->frags[i] > 16 ) {
				ly->frags[i] = 16;
			}
			if ( f > DS_WIN_FLOOR ) {
				ly->frags[i] = 16 + ( f - DS_WIN_FLOOR ) / 6;
				if ( ly->frags[i] > 20 ) {
					ly->frags[i] = 20;
				}
			}
			if ( ly->type[i] == RLN_BOSS ) {
				// the boss and its adds: 1 add in act 1, 2 in acts 2-3, 3 after that
				ly->bots[i] = act <= 1 ? 2 : act <= 3 ? 3 : 4;
			}

			if ( ly->type[i] == RLN_EVENT ) {
				ly->mut[i] = 1 + (int)( RL_Hash( s, L, 30 + i ) % ( MUT_NUM_MUTATORS - 1 ) );
			}
			ly->jitter[i] = (int)( RL_Hash( s, L, 40 + i ) % 17 ) - 8;
		}

		if ( !nonElite ) {
			ly->type[0] = RLN_FIGHT;
		}
	}

	// the vault: about one act in four hides one on a middle floor (never the only way on)
	for ( act = 0; act * DS_FLOORS_PER_ACT < count; act++ ) {
		if ( RL_Hash( s, act, 77 ) % 100 >= 28 ) {
			continue;
		}
		L = act * DS_FLOORS_PER_ACT + 1 + (int)( RL_Hash( s, act, 78 ) % 4 );		// floors 2-5 of the act
		if ( L >= count || rl_layers[L].n < 2 ) {
			continue;
		}
		ly = &rl_layers[L];
		i = (int)( RL_Hash( s, act, 79 ) % ly->n );
		ly->type[i] = RLN_VAULT;
		ly->mut[i] = 0;
		ly->bots[i] = 0;
		ly->frags[i] = 99;
		// the other nodes on that floor keep a fight
		for ( j = 0, nonElite = 0; j < ly->n; j++ ) {
			if ( j != i && ly->type[j] != RLN_ELITE ) {
				nonElite++;
			}
		}
		if ( !nonElite ) {
			ly->type[( i + 1 ) % ly->n] = RLN_FIGHT;
		}
	}

	// links to the next layer
	for ( L = 0; L + 1 < count; L++ ) {
		ly = &rl_layers[L];
		n = ly->n;
		m = rl_layers[L + 1].n;
		if ( n == 1 ) {
			ly->edges[0] = ( 1 << m ) - 1;
			continue;
		}
		if ( m == 1 ) {
			for ( i = 0; i < n; i++ ) {
				ly->edges[i] = 1;
			}
			continue;
		}
		i = 0;
		j = 0;
		ly->edges[0] |= 1;
		while ( i < n - 1 || j < m - 1 ) {
			if ( i == n - 1 ) {
				j++;
			}
			else if ( j == m - 1 ) {
				i++;
			}
			else if ( i * ( m - 1 ) - j * ( n - 1 ) < -( ( n - 1 ) * ( m - 1 ) ) / 3 ) {
				i++;	// this side lags behind the diagonal: catch up, so links fan out evenly
			}
			else if ( j * ( n - 1 ) - i * ( m - 1 ) < -( ( n - 1 ) * ( m - 1 ) ) / 3 ) {
				j++;
			}
			else {
				r = (int)( RL_Hash( s, L, 50 + i * 4 + j ) % 3 );
				if ( r == 0 ) {
					i++;
				}
				else if ( r == 1 ) {
					j++;
				}
				else {
					i++;
					j++;
				}
			}
			ly->edges[i] |= ( 1 << j );
		}
	}

	UI_DSM_Assign( rl_layers, count, seed );		// ds-maps: the window's maps (stored in the run), variants
}

// ds-maps: the simulation builds the floors of other seeds
rlLayer_t *RL_DSM_Layers( int seed, int count ) {
	RL_BuildLayers( seed, count );
	return rl_layers;
}


/*
=================
RL_ComputeReach

Which nodes of the current layer can be picked
=================
*/
static void RL_ComputeReach( void ) {
	rlLayer_t	*ly;
	int			prev;
	int			i;

	rl.layer = rl.floor;
	if ( rl.layer > RL_MAX_LAYERS - RL_VIS_LAYERS - 2 ) {
		rl.layer = RL_MAX_LAYERS - RL_VIS_LAYERS - 2;
	}
	RL_BuildLayers( rl.seed, rl.layer + RL_VIS_LAYERS + 1 );

	ly = &rl_layers[rl.layer];
	rl.numReach = 0;

	prev = -1;
	if ( rl.layer > 0 && rl.pathLen >= rl.layer ) {
		prev = rl.path[rl.layer - 1];
		if ( prev < 0 || prev >= rl_layers[rl.layer - 1].n ) {
			prev = -1;
		}
	}

	for ( i = 0; i < ly->n; i++ ) {
		if ( prev < 0 || ( rl_layers[rl.layer - 1].edges[prev] & ( 1 << i ) ) ) {
			rl.reach[rl.numReach++] = i;
		}
	}
}

// node i of layer L was played
static qboolean RL_Visited( int L, int i ) {
	return ( L < rl.layer && L < rl.pathLen && rl.path[L] == i );
}

static qboolean RL_Reachable( int L, int i ) {
	int		k;

	if ( L != rl.layer ) {
		return qfalse;
	}
	for ( k = 0; k < rl.numReach; k++ ) {
		if ( rl.reach[k] == i ) {
			return qtrue;
		}
	}
	return qfalse;
}

static int RL_NodeSkill100( int f ) {
	int		s;

	s = rl.difficulty * 100;
	if ( f > 6 ) {
		s += 12 * ( f - 6 );		// ACTS: +2 skill over the 24 floors, the Abyss hits the cap
	}
	if ( s > 500 ) {
		s = 500;
	}
	return s;
}

// boss floors: the timed 1v1 (g_dsBossMode 0, the default; the daily always) or 10 lives with rivals (1)
static qboolean RL_BossTimed( void ) {
	return UI_DS_DailyOn() || (int)trap_Cvar_VariableValue( "g_dsBossMode" ) != 1;
}

// the floor type's line on the map; the boss line follows g_dsBossMode / g_dsBossTime (the daily is always 5:00)
// (about 50 characters: the floor panel wraps it at 2 lines of ~26)
static const char *RL_NodeDesc( int type ) {
	static char	buf[128];
	int			t;

	if ( type != RLN_BOSS ) {
		return rl_nodeDescs[type];
	}
	if ( !RL_BossTimed() ) {
		t = (int)trap_Cvar_VariableValue( "g_dsBossLives" );
		Com_sprintf( buf, sizeof( buf ), "With two rivals, %i lives each: slay it first.", t > 0 ? t : 10 );
		return buf;
	}
	t = UI_DS_DailyOn() ? 300 : (int)trap_Cvar_VariableValue( "g_dsBossTime" );
	t = t <= 0 ? 300 : t < 60 ? 60 : t > 1800 ? 1800 : t;		// unset = the 300 default; g_ds_boss.c's clamp
	Com_sprintf( buf, sizeof( buf ), "Timed 1v1: kill it in %i:%02i. Unlimited deaths.", t / 60, t % 60 );
	return buf;
}

static int RL_NodeTier( int f ) {
	if ( f <= DS_WIN_FLOOR ) {
		return ( f - 1 ) / 8;		// ACTS: 0, 1, 2 over the acts
	}
	return 3 + ( f - DS_WIN_FLOOR - 1 ) / 6;	// the Abyss
}


/*
=================
RL_LaunchNode

Writes the node into the run file and starts the level
=================
*/
void RL_LaunchNode( int L, int i ) {
	rlLayer_t	*ly;
	char		buf[RL_VAL_LEN];
	int			k;
	int			f;
	int			skill;
	int			len;

	RL_LoadRun();
	if ( !rl.active ) {
		return;
	}
	RL_BuildLayers( rl.seed, L + 2 );
	if ( L < 0 || L >= rl_layersBuilt ) {
		return;
	}
	ly = &rl_layers[L];
	if ( i < 0 || i >= ly->n ) {
		return;
	}
	f = L + 1;
	UI_DSM_Launch( L, i, rl_maps[ly->map[i]], ly->variant[i] );		// ds-maps: played this run, lifetime counts, node_variant

	// the picked node on every floor so far, then this one
	for ( k = rl.pathLen; k < L; k++ ) {
		rl.path[k] = 0;
	}
	rl.path[L] = i;
	rl.pathLen = L + 1;
	buf[0] = 0;
	len = 0;
	for ( k = 0; k < rl.pathLen && len < RL_VAL_LEN - 8; k++ ) {
		Com_sprintf( buf + len, sizeof( buf ) - len, k ? ".%i" : "%i", rl.path[k] );
		len += strlen( buf + len );
	}

	skill = RL_NodeSkill100( f );
	RL_KVSet( &rl_run, "node_type", rl_nodeKeys[ly->type[i]] );
	RL_KVSet( &rl_run, "node_map", rl_maps[ly->map[i]] );
	RL_KVSetInt( &rl_run, "node_bots", ly->bots[i] );
	RL_KVSetInt( &rl_run, "node_frags", ly->frags[i] );
	RL_KVSetInt( &rl_run, "node_mutator", ly->mut[i] );
	RL_KVSet( &rl_run, "node_skill", va( "%i.%02i", skill / 100, skill % 100 ) );
	RL_KVSetInt( &rl_run, "node_tier", RL_NodeTier( f ) );
	RL_KVSetInt( &rl_run, "node_floor", f );
	RL_KVSet( &rl_run, "map", buf );
	RL_KVSet( &rl_run, "result", "none" );
	RL_KVSet( &rl_run, "offer", "" );
	RL_KVSetInt( &rl_run, "act", DS_ActForFloor( f ) );		// ACTS
	RL_KVSetInt( &rl_run, "infloor", 1 );		// META: quit / crash mid-floor = this node again, no life lost
	RL_KVSetInt( &rl_run, "node_layer", L );
	RL_KVSetInt( &rl_run, "node_index", i );
	UI_DS_EconLaunch();		// ds-economy: diff_min / diff_max (records go to the easiest difficulty played)
	UI_DS_SurpriseLaunch( rl.seed, L, i, ly->type[i], ly->bots[i] );		// ds-surprise: the floor modifier taken (node_mod), SWARM's bots
	RL_SaveRun();

	RL_SetModeForRun();
	UI_DS_BossPracOff();		// en-boss: a real floor, never the practice file
	trap_Cvar_SetValue( "g_gametype", GT_FFA );
	trap_Cvar_SetValue( "fraglimit", ly->frags[i] );
	trap_Cvar_SetValue( "timelimit", ly->type[i] == RLN_BOSS ? RL_BOSS_TIMELIMIT : ly->type[i] == RLN_VAULT ? RL_VAULT_TIMELIMIT : RL_TIMELIMIT );
	trap_Cvar_SetValue( "ui_singlePlayerActive", 0 );
	if ( trap_Cvar_VariableValue( "sv_maxclients" ) < 8 ) {
		trap_Cvar_SetValue( "sv_maxclients", 8 );
	}

	Com_Printf( "DESCENT: floor %i, %s on %s, %i bots, %i frags\n", f, rl_nodeNames[ly->type[i]], rl_maps[ly->map[i]], ly->bots[i], ly->frags[i] );

	UI_ForceMenuOff();
	// right after this command (not at the end of a running script)
	// (tests: set ui_rlDevmap 1 keeps cheats on, for bot_pause / god / setviewpos)
	trap_Cmd_ExecuteText( EXEC_INSERT, va( "%s %s\n", trap_Cvar_VariableValue( "ui_rlDevmap" ) ? "devmap" : "map", rl_maps[ly->map[i]] ) );
}


/*
=======================================================================

DRAWING HELPERS

=======================================================================
*/

void RL_Layout( void ) {
	float	e;

	e = uis.bias / uis.xscale;
	if ( e > 200 ) {
		e = 200;
	}
	if ( e < 0 ) {
		e = 0;
	}
	rl_ext = e * 0.8f;
}

void RL_Text( float x, float y, const char *str, float cw, float ch, const float *color, int align ) {
	const char	*s;
	float		ax;
	float		ay;
	float		aw;
	float		ah;
	float		frow;
	float		fcol;
	int			c;

	if ( !str || !str[0] ) {
		return;
	}
	if ( align == 1 ) {
		x -= strlen( str ) * cw * 0.5f;
	}
	else if ( align == 2 ) {
		x -= strlen( str ) * cw;
	}

	ax = x * uis.xscale + uis.bias;
	ay = y * uis.yscale;
	aw = cw * uis.xscale;
	ah = ch * uis.yscale;

	trap_R_SetColor( color );
	for ( s = str; *s; s++ ) {
		c = *s & 255;
		if ( c != ' ' ) {
			frow = ( c >> 4 ) * 0.0625f;
			fcol = ( c & 15 ) * 0.0625f;
			trap_R_DrawStretchPic( ax, ay, aw, ah, fcol, frow, fcol + 0.0625f, frow + 0.0625f, uis.charset );
		}
		ax += aw;
	}
	trap_R_SetColor( NULL );
}

// text with a drop shadow
void RL_TextS( float x, float y, const char *str, float cw, float ch, const float *color, int align ) {
	vec4_t	shadow;

	shadow[0] = shadow[1] = shadow[2] = 0;
	shadow[3] = color[3] * 0.8f;
	RL_Text( x + 1, y + 1, str, cw, ch, shadow, align );
	RL_Text( x, y, str, cw, ch, color, align );
}

// a card's description, naming the key the player bound for ability cards with their own key
static const char *RL_CardDesc( int card ) {
	static char	name[32];
	char		b[256];
	const char	*cmd = BG_RLCardBinding( card );
	int			k;

	if ( !cmd ) {
		return bg_rlCardDescs[card];
	}
	for ( k = 0 ; k < 256 ; k++ ) {
		trap_Key_GetBindingBuf( k, b, sizeof( b ) );
		if ( b[0] && !Q_stricmp( b, cmd ) ) {
			trap_Key_KeynumToStringBuf( k, name, sizeof( name ) );
			Q_strupr( name );
			return BG_RLCardDescKey( card, name );
		}
	}
	return bg_rlCardDescs[card];
}

// word wrapped; returns the y below the last line
float RL_TextWrap( float x, float y, float w, const char *str, float cw, float ch, const float *color, int maxLines ) {
	char	line[128];
	int		maxChars;
	int		len;
	int		n;
	int		lines;

	maxChars = (int)( w / cw );
	if ( maxChars > 127 ) {
		maxChars = 127;
	}
	if ( maxChars < 1 ) {
		maxChars = 1;
	}
	lines = 0;
	while ( str && *str && lines < maxLines ) {
		len = strlen( str );
		if ( len > maxChars ) {
			for ( n = maxChars; n > 0 && str[n] != ' '; n-- ) {
			}
			if ( n == 0 ) {
				n = maxChars;
			}
		}
		else {
			n = len;
		}
		Q_strncpyz( line, str, n + 1 );
		RL_Text( x, y, line, cw, ch, color, 0 );
		str += n;
		while ( *str == ' ' ) {
			str++;
		}
		y += ch + 2;
		lines++;
	}
	return y;
}

void RL_Frame( float x, float y, float w, float h, const float *fill, const float *edge ) {
	if ( fill ) {
		UI_FillRect( x, y, w, h, fill );
	}
	if ( edge ) {
		UI_FillRect( x, y, w, 1, edge );
		UI_FillRect( x, y + h - 1, w, 1, edge );
		UI_FillRect( x, y, 1, h, edge );
		UI_FillRect( x + w - 1, y, 1, h, edge );
	}
}

void RL_Pic( float x, float y, float w, float h, qhandle_t shader, const float *color ) {
	if ( !shader ) {
		return;
	}
	trap_R_SetColor( color );
	UI_DrawHandlePic( x, y, w, h, shader );
	trap_R_SetColor( NULL );
}

static void RL_Dots( float x1, float y1, float x2, float y2, const float *color, float step, float size ) {
	float	dx;
	float	dy;
	float	len;
	float	t;
	int		n;
	int		i;

	dx = x2 - x1;
	dy = y2 - y1;
	len = sqrt( dx * dx + dy * dy );
	n = (int)( len / step );
	if ( n < 1 ) {
		n = 1;
	}
	for ( i = 0; i <= n; i++ ) {
		t = (float)i / n;
		UI_FillRect( x1 + dx * t - size * 0.5f, y1 + dy * t - size * 0.5f, size, size, color );
	}
}

float RL_Pulse( float speed ) {
	return 0.5f + 0.5f * sin( uis.realtime * speed );
}

void RL_Scale( vec4_t out, const float *in, float k, float alpha ) {
	out[0] = in[0] * k;
	out[1] = in[1] * k;
	out[2] = in[2] * k;
	out[3] = alpha;
}

/*
=================
RL_Backdrop

Fills the whole screen, beyond the 4:3 box on wide monitors
=================
*/
void RL_Backdrop( qboolean opaque ) {
	float	e;
	vec4_t	c;

	e = uis.bias / uis.xscale + 1;
	UI_FillRect( -e, 0, 640 + 2 * e, 480, opaque ? rl_colBack : rl_colShade );

	// a faint ember glow from below and dark corners
	c[0] = 0.22f;
	c[1] = 0.03f;
	c[2] = 0.015f;
	c[3] = 1;
	RL_Pic( 320 - 420 - e, 300, 840 + 2 * e, 420, rl_shGlow, c );
	c[0] = c[1] = c[2] = 0;
	c[3] = 0.7f;
	RL_Pic( -e, 0, 640 + 2 * e, 480, rl_shVignette, c );
}

void RL_Header( const char *title, const char *sub ) {
	UI_DrawBannerString( 320, 10, title, UI_CENTER, rl_colBanner );
	if ( sub && sub[0] ) {
		RL_TextS( 320, 50, sub, 7, 11, rl_colDim, 1 );
	}
}

void RL_DrawSkulls( int skulls ) {
	RL_TextS( 632 + rl_ext, 16, va( "%i", skulls ), 12, 18, rl_colOrange, 2 );
	RL_Text( 632 + rl_ext, 36, "SKULLS", 6, 9, rl_colDim, 2 );
}

const char *RL_TimeString( int msec ) {
	int		sec;

	sec = msec / 1000;
	if ( sec >= 3600 ) {
		return va( "%i:%02i:%02i", sec / 3600, ( sec / 60 ) % 60, sec % 60 );
	}
	return va( "%i:%02i", sec / 60, sec % 60 );
}


/*
=================
RL_ButtonDraw
=================
*/
void RL_ButtonDraw( void *self ) {
	rlButton_t		*b;
	menucommon_s	*g;
	float			x;
	float			y;
	float			w;
	float			h;
	float			tx;
	qboolean		focus;
	qboolean		grayed;
	vec4_t			text;

	b = (rlButton_t *)self;
	g = &b->bm.generic;
	x = g->left;
	y = g->top;
	w = g->right - g->left;
	h = g->bottom - g->top;
	grayed = ( g->flags & QMF_GRAYED ) != 0;
	focus = !grayed && ( Menu_ItemAtCursor( g->parent ) == self );

	RL_Frame( x, y, w, h, focus ? rl_colPanelHi : rl_colPanel, focus ? rl_colOrange : rl_colLine );
	if ( focus ) {
		UI_FillRect( x, y, 3, h, rl_colOrange );
	}

	if ( grayed ) {
		Vector4Copy( rl_colDark, text );
	}
	else if ( focus ) {
		Vector4Copy( rl_colBone, text );
		text[1] = rl_colBone[1] - 0.25f * RL_Pulse( 0.006f );
		text[2] = rl_colBone[2] - 0.45f * RL_Pulse( 0.006f );
	}
	else {
		Vector4Copy( rl_colBone, text );
	}

	tx = x + 12;
	if ( b->icon ) {
		RL_Pic( x + 6, y + 4, h - 8, h - 8, b->icon, grayed ? rl_colDark : NULL );
		tx = x + h + 6;
	}

	if ( b->center ) {
		UI_DrawProportionalString( (int)( x + w / 2 ), (int)( y + ( h - 20 ) / 2 ), b->label, UI_CENTER|UI_SMALLFONT, text );
	}
	else if ( b->sub[0] ) {
		UI_DrawProportionalString( (int)tx, (int)( y + 3 ), b->label, UI_LEFT|UI_SMALLFONT, text );
		RL_Text( tx, y + h - 13, b->sub, 6, 10, grayed ? rl_colDark : rl_colDim, 0 );
	}
	else {
		UI_DrawProportionalString( (int)tx, (int)( y + ( h - 20 ) / 2 ), b->label, UI_LEFT|UI_SMALLFONT, text );
	}
}

void RL_InitButton( rlButton_t *b, int id, float x, float y, float w, float h, const char *label, void (*callback)( void *, int ) ) {
	b->bm.generic.type = MTYPE_BITMAP;
	b->bm.generic.flags = QMF_LEFT_JUSTIFY;
	b->bm.generic.x = (int)x;
	b->bm.generic.y = (int)y;
	b->bm.generic.id = id;
	b->bm.generic.callback = callback;
	b->bm.generic.ownerdraw = RL_ButtonDraw;
	b->bm.width = (int)w;
	b->bm.height = (int)h;
	Q_strncpyz( b->label, label, sizeof( b->label ) );
}

/*
=================
RL_KeyRemap

Menus work without arrow keys: WASD, mouse wheel, TAB / shift-TAB and
gamepads move the focus; ENTER, SPACE and pad A pick; pad B goes back.
=================
*/
int RL_KeyRemap( int key ) {
	switch ( key ) {
	case 'w':
	case 'a':
	case K_LEFTARROW:
	case K_MWHEELUP:
	case K_PAD0_DPAD_UP:
	case K_PAD0_DPAD_LEFT:
	case K_PAD0_LEFTSTICK_UP:
	case K_PAD0_LEFTSTICK_LEFT:
		return K_UPARROW;
	case 's':
	case 'd':
	case K_RIGHTARROW:
	case K_MWHEELDOWN:
	case K_PAD0_DPAD_DOWN:
	case K_PAD0_DPAD_RIGHT:
	case K_PAD0_LEFTSTICK_DOWN:
	case K_PAD0_LEFTSTICK_RIGHT:
		return K_DOWNARROW;
	case K_TAB:
		if ( trap_Key_IsDown( K_SHIFT ) ) {
			return K_UPARROW;
		}
		return K_DOWNARROW;
	case K_SPACE:
	case K_PAD0_A:
	case K_PAD0_START:
		return K_ENTER;
	case K_PAD0_B:
	case K_PAD0_BACK:
		return K_ESCAPE;
	}
	return key;
}

sfxHandle_t RL_MenuKey( int key ) {
	return Menu_DefaultKey( uis.activemenu, RL_KeyRemap( key ) );
}

void RL_Footer( const char *text ) {
	RL_Text( 320, 466, text, 6, 9, rl_colDark, 1 );
}

void RL_SetupMenu( menuframework_s *menu, void (*draw)( void ), qboolean ingame ) {
	RL_Cache();
	RL_Layout();
	menu->wrapAround = qtrue;
	menu->fullscreen = !ingame;
	menu->showlogo = qfalse;
	menu->draw = draw;
	menu->key = RL_MenuKey;
}


/*
=======================================================================

FORWARD

=======================================================================
*/

static void RL_CardMenu( int mode );


/*
=================
RL_OpenRun

Where a run goes next: an unpicked card offer first, then the floor map.
A finished run shows the run-over screen.
=================
*/
void RL_OpenRun( void ) {
	RL_LoadRun();
	if ( UI_DS_OpenRun() ) {
		return;		// META: the run summary, the victory choice, an old run to end, a floor to resume
	}
	RL_LoadRun();
	if ( rl.numOffer > 0 ) {
		RL_CardMenu( RLCM_OFFER );
		return;
	}
	RL_MapMenu();
}

// replaces the top menu with another (keeps the hub below it)
void RL_PopTop( void ) {
	if ( uis.menusp > 0 ) {
		uis.menusp--;
		if ( uis.menusp > 0 ) {
			uis.activemenu = uis.stack[uis.menusp - 1];
		}
		else {
			uis.activemenu = NULL;
		}
	}
}


/*
=================
RL_StartRun
=================
*/
void RL_StartRun( int difficulty ) {
	int		seed;
	int		i;
	int		lives;

	if ( difficulty < 1 ) {
		difficulty = 1;
	}
	if ( difficulty > 5 ) {
		difficulty = 5;
	}

	RL_LoadMeta();
	UI_DS_BeforeNewRun();		// META: an abandoned descent goes into the history

	seed = (int)( RL_Hash( (unsigned)trap_Milliseconds(), (unsigned)uis.realtime, (unsigned)rand() ) & 0x7fffffff );
	if ( !seed ) {
		seed = 1;
	}
	lives = RL_BaseLives( difficulty );
	if ( RL_KVInt( &rl_meta, "up_life" ) > 0 ) {
		lives++;
	}

	// a fresh run file: the old run is gone (abandoning banks nothing)
	rl_run.num = 0;
	RL_KVSetInt( &rl_run, "version", DS_RUN_VERSION );
	RL_KVSetInt( &rl_run, "active", 1 );
	RL_KVSetInt( &rl_run, "seed", seed );
	RL_KVSetInt( &rl_run, "difficulty", difficulty );
	UI_DS_EconStartRun( difficulty );		// ds-economy: diff_start (the difficulty can change between floors)
	RL_KVSetInt( &rl_run, "lives", lives );
	RL_KVSetInt( &rl_run, "floor", 0 );
	RL_KVSetInt( &rl_run, "skulls", 0 );
	RL_KVSet( &rl_run, "cards", "" );
	RL_KVSetInt( &rl_run, "kills", 0 );
	RL_KVSetInt( &rl_run, "time", 0 );
	RL_KVSet( &rl_run, "result", "none" );
	RL_KVSet( &rl_run, "offer", "" );
	RL_KVSet( &rl_run, "map", "" );
	RL_KVSetInt( &rl_run, "rerolls", RL_KVInt( &rl_meta, "up_reroll" ) );
	for ( i = 0; i < (int)ARRAY_LEN( rl_runUpgradeKeys ); i++ ) {
		RL_KVSetInt( &rl_run, rl_runUpgradeKeys[i], RL_KVInt( &rl_meta, rl_runUpgradeKeys[i] ) );
	}
	UI_DS_StartRunKeys();		// META: character, starting kit, every upgrade rank
	RL_SaveRun();
	RL_LoadRun();

	Com_Printf( "DESCENT: new run, %s, %i lives, seed %i\n", rl_skillNames[difficulty - 1], lives, seed );

	if ( RL_KVInt( &rl_meta, "up_startcard" ) > 0 ) {
		RL_CardMenu( RLCM_START );
	}
	else {
		RL_MapMenu();
	}
}


/*
=======================================================================

HUB

=======================================================================
*/

// the hub: UI_RL_HubMenu in ui_ds_meta.c


/*
=======================================================================

NEW RUN (difficulty)

=======================================================================
*/

typedef struct {
	menuframework_s	menu;
	rlButton_t		diff[5];
	rlButton_t		back;
	int				pending;
} rlNewRun_t;

static rlNewRun_t s_rlNew;


static void RL_AbandonAction( qboolean result ) {
	if ( !result ) {
		return;
	}
	UI_DS_CharacterMenu( s_rlNew.pending );	// META: then the character; that menu starts the run
}

static void RL_AbandonDraw( void ) {
	UI_DrawProportionalString( 320, 230, "NOTHING IS BANKED", UI_CENTER|UI_SMALLFONT, color_red );
}

static void RL_NewRunEvent( void *ptr, int event ) {
	int		id;

	if ( event != QM_ACTIVATED ) {
		return;
	}
	id = ((menucommon_s *)ptr)->id;
	if ( id == ID_RL_BACK ) {
		UI_PopMenu();
		return;
	}
	if ( id >= ID_RL_ITEM0 && id < ID_RL_ITEM0 + 5 ) {
		s_rlNew.pending = id - ID_RL_ITEM0 + 1;
		RL_LoadRun();
		if ( rl.active ) {
			UI_ConfirmMenu( "ABANDON THE DESCENT?", RL_AbandonDraw, RL_AbandonAction );
			return;
		}
		UI_DS_CharacterMenu( s_rlNew.pending );	// META: then the character; that menu starts the run
	}
}

static void RL_NewRunDraw( void ) {
	RL_Backdrop( !RL_InGame() );
	RL_Header( "NEW DESCENT", "Choose how hard the bots fight. Every ability starts locked." );
	RL_DrawSkulls( RL_KVInt( &rl_meta, "skulls" ) );
	Menu_Draw( &s_rlNew.menu );
	RL_Text( 320, 410, "Deeper floors bring more bots, more frags to win, elites, events and bosses.", 6, 10, rl_colDim, 1 );
	RL_Footer( "MOUSE, ENTER / TAB, WASD OR GAMEPAD" );
}

void RL_NewRunMenu( void ) {
	static const char *descs[5] = {
		"bots at skill 1",
		"bots at skill 2",
		"bots at skill 3",
		"bots at skill 4",
		"bots at skill 5"
	};
	int		i;
	int		lives;
	int		bonus;
	float	y;

	memset( &s_rlNew, 0, sizeof( s_rlNew ) );
	RL_SetupMenu( &s_rlNew.menu, RL_NewRunDraw, RL_InGame() );
	RL_LoadMeta();
	RL_LoadRun();
	bonus = ( RL_KVInt( &rl_meta, "up_life" ) > 0 );

	y = 76;
	for ( i = 0; i < 5; i++ ) {
		RL_InitButton( &s_rlNew.diff[i], ID_RL_ITEM0 + i, 320 - 170, y, 340, 50, rl_skillNames[i], RL_NewRunEvent );
		s_rlNew.diff[i].icon = rl_skillIcons[i];
		lives = RL_BaseLives( i + 1 ) + bonus;
		Com_sprintf( s_rlNew.diff[i].sub, sizeof( s_rlNew.diff[i].sub ), "%i %s  |  %s%s", lives, lives == 1 ? "life" : "lives",
			descs[i], bonus ? "  |  +1 spare soul" : "" );
		Menu_AddItem( &s_rlNew.menu, &s_rlNew.diff[i] );
		y += 62;
	}
	RL_InitButton( &s_rlNew.back, ID_RL_BACK, 8 - rl_ext, 440, 120, 28, "BACK", RL_NewRunEvent );
	s_rlNew.back.center = qtrue;
	Menu_AddItem( &s_rlNew.menu, &s_rlNew.back );

	UI_PushMenu( &s_rlNew.menu );
	// start on the difficulty of the last run (or Hurt Me Plenty)
	i = RL_KVInt( &rl_run, "difficulty" );
	if ( i < 1 || i > 5 ) {
		i = 3;
	}
	Menu_SetCursorToItem( &s_rlNew.menu, &s_rlNew.diff[i - 1] );
}


// upgrades, records: ui_ds_meta.c


/*
=======================================================================

CARD PICK (the free starting card, or an offer left unpicked)

=======================================================================
*/

typedef struct {
	menuframework_s	menu;
	rlButton_t		cards[RL_MAX_OFFER];
	int				ids[RL_MAX_OFFER];
	int				num;
	int				mode;
	qboolean		ingame;
} rlCards_t;

static rlCards_t s_rlCards;


static void RL_CardDraw( void *self ) {
	rlButton_t		*b;
	menucommon_s	*g;
	float			x;
	float			y;
	float			w;
	float			h;
	int				card;
	int				kind;
	int				rarity;
	qboolean		focus;
	vec4_t			c;

	b = (rlButton_t *)self;
	g = &b->bm.generic;
	x = g->left;
	y = g->top;
	w = g->right - g->left;
	h = g->bottom - g->top;
	card = b->data;
	kind = bg_rlCardKind[card];
	if ( kind < 0 || kind > 7 ) {
		kind = 0;
	}
	rarity = bg_rlCardRarity[card];
	if ( rarity < 0 || rarity > 3 ) {
		rarity = 0;
	}
	focus = ( Menu_ItemAtCursor( g->parent ) == self );

	if ( focus ) {
		y -= 6;
		RL_Scale( c, rl_kindColors[kind], 0.45f, 1 );
		RL_Pic( x - 30, y - 30, w + 60, h + 60, rl_shGlow, c );
	}
	RL_Frame( x, y, w, h, focus ? rl_colPanelHi : rl_colPanel, focus ? rl_colOrange : rl_rarityColors[rarity] );
	UI_FillRect( x + 1, y + 1, w - 2, 22, rl_kindColors[kind] );
	RL_Text( x + w / 2, y + 7, rl_kindNames[kind], 7, 10, rl_colBlack, 1 );

	RL_TextWrap( x + 8, y + 36, w - 16, bg_rlCardNames[card], 9, 13, rl_colBone, 3 );
	RL_TextWrap( x + 8, y + 84, w - 16, RL_CardDesc( card ), 6, 10, rl_colDim, 9 );

	RL_Text( x + w / 2, y + h - 16, rarity == 3 ? "LEGENDARY" : rarity == 2 ? "EPIC" : rarity == 1 ? "RARE" : "COMMON", 6, 9, rl_rarityColors[rarity], 1 );
}

/*
=================
RL_ApplyGrowthCard

DESCENT v2: a passive / weapon level card taken from this menu levels it up in the run file
(the game does the same in RL_Pick): pv (passive levels) / wl (weapon levels, 0 = never held)
=================
*/
static void RL_ApplyGrowthCard( int card ) {
	int			v[16];
	int			n;
	int			i;
	int			idx;
	char		buf[128];
	const char	*key;
	const char	*s;

	if ( RLC_IS_PASSIVE( card ) ) {
		key = "pv";
		idx = card - RLC_PASSIVE_FIRST;
		n = RLP_NUM;
	}
	else if ( RLC_IS_WEAPON( card ) ) {
		key = "wl";
		idx = RLC_WEAPON_OF( card );
		n = DS_NUM_WEAPONS;
	}
	else {
		return;
	}
	memset( v, 0, sizeof( v ) );
	s = RL_KVGet( &rl_run, key );
	for ( i = 0; i < n && *s; i++ ) {
		v[i] = atoi( s );
		while ( *s && *s != ',' ) {
			s++;
		}
		if ( *s ) {
			s++;
		}
	}
	if ( key[0] == 'p' ) {
		v[idx] = v[idx] + 1 > DS_MAX_PLEVEL ? DS_MAX_PLEVEL : v[idx] + 1;
	}
	else {
		v[idx] = ( v[idx] < 1 ? 1 : v[idx] ) + 1;
		if ( v[idx] > DS_MAX_WLEVEL ) {
			v[idx] = DS_MAX_WLEVEL;
		}
	}
	buf[0] = 0;
	for ( i = 0; i < n; i++ ) {
		Q_strcat( buf, sizeof( buf ), va( i ? ",%i" : "%i", v[i] ) );
	}
	RL_KVSet( &rl_run, key, buf );
}

static void RL_CardEvent( void *ptr, int event ) {
	rlButton_t	*b;
	char		buf[RL_VAL_LEN];
	const char	*cards;
	int			card;

	if ( event != QM_ACTIVATED ) {
		return;
	}
	b = (rlButton_t *)ptr;
	card = b->data;

	RL_LoadRun();
	if ( !rl.active ) {
		return;
	}
	cards = RL_KVGet( &rl_run, "cards" );
	if ( cards[0] ) {
		Com_sprintf( buf, sizeof( buf ), "%s,%i", cards, card );
	}
	else {
		Com_sprintf( buf, sizeof( buf ), "%i", card );
	}
	RL_KVSet( &rl_run, "cards", buf );
	RL_ApplyGrowthCard( card );
	if ( s_rlCards.mode == RLCM_OFFER ) {
		RL_KVSet( &rl_run, "offer", "" );
	}
	RL_SaveRun();
	Com_Printf( "DESCENT: took card %s\n", bg_rlCardNames[card] );

	RL_PopTop();
	RL_MapMenu();
}

static void RL_CardsDraw( void ) {
	RL_Backdrop( !s_rlCards.ingame );
	if ( s_rlCards.mode == RLCM_START ) {
		RL_Header( "BIRTHRIGHT", "Choose the ability you start this descent with." );
	}
	else {
		RL_Header( "SPOILS", "You won the floor. Choose a card." );
	}
	Menu_Draw( &s_rlCards.menu );
	RL_Footer( "CLICK A CARD, OR TAB / WASD THEN ENTER" );
}

static sfxHandle_t RL_CardsKey( int key ) {
	key = RL_KeyRemap( key );
	if ( key == K_ESCAPE || key == K_MOUSE2 ) {
		return menu_buzz_sound;		// a card must be picked
	}
	return Menu_DefaultKey( &s_rlCards.menu, key );
}

static void RL_CardMenu( int mode ) {
	int		i;
	int		j;
	int		k;
	int		n;
	int		pool[ARRAY_LEN( rl_startCards )];
	float	w;
	float	x;

	memset( &s_rlCards, 0, sizeof( s_rlCards ) );
	s_rlCards.ingame = RL_InGame();
	s_rlCards.mode = mode;
	RL_SetupMenu( &s_rlCards.menu, RL_CardsDraw, s_rlCards.ingame );
	s_rlCards.menu.key = RL_CardsKey;

	if ( mode == RLCM_OFFER ) {
		for ( i = 0; i < rl.numOffer; i++ ) {
			s_rlCards.ids[s_rlCards.num++] = rl.offer[i];
		}
	}
	else {
		// 3 different abilities, from the run seed
		n = ARRAY_LEN( rl_startCards );
		for ( i = 0; i < n; i++ ) {
			pool[i] = rl_startCards[i];
		}
		for ( i = 0; i < 3; i++ ) {
			k = i + (int)( RL_Hash( (unsigned)rl.seed, 999, i ) % ( n - i ) );
			j = pool[i];
			pool[i] = pool[k];
			pool[k] = j;
			s_rlCards.ids[s_rlCards.num++] = pool[i];
		}
	}

	if ( !s_rlCards.num ) {
		RL_MapMenu();
		return;
	}

	w = 136;
	x = 320 - ( s_rlCards.num * w + ( s_rlCards.num - 1 ) * 14 ) / 2;
	for ( i = 0; i < s_rlCards.num; i++ ) {
		RL_InitButton( &s_rlCards.cards[i], ID_RL_ITEM0 + i, x, 110, w, 240, bg_rlCardNames[s_rlCards.ids[i]], RL_CardEvent );
		s_rlCards.cards[i].bm.generic.ownerdraw = RL_CardDraw;
		s_rlCards.cards[i].data = s_rlCards.ids[i];
		Menu_AddItem( &s_rlCards.menu, &s_rlCards.cards[i] );
		x += w + 14;
	}

	if ( s_rlCards.ingame ) {
		uis.menusp = 0;
	}
	UI_PushMenu( &s_rlCards.menu );
}


/*
=======================================================================

FLOOR MAP

=======================================================================
*/

typedef struct {
	menuframework_s	menu;
	rlButton_t		nodes[RL_MAX_NODES];
	rlButton_t		quit;
	rlButton_t		diff;			// ds-economy: the difficulty picker
	qboolean		ingame;
	int				first;			// lowest layer drawn
	int				last;			// highest layer drawn
} rlMapMenu_t;

static rlMapMenu_t s_rlMap;


static void RL_NoDraw( void *self ) {
}

static float RL_MapLeft( void ) {
	return 168 - rl_ext;
}

static float RL_MapRight( void ) {
	return 472 + rl_ext;
}

static void RL_NodePos( int L, int i, float *x, float *y ) {
	float	l;
	float	r;
	float	step;
	int		n;

	n = rl_layers[L].n;
	l = RL_MapLeft() + 34;
	r = RL_MapRight() - 34;
	*x = l + ( r - l ) * ( i + 0.5f ) / n + rl_layers[L].jitter[i] * ( 1.0f + rl_ext / 100.0f );
	step = (float)( RL_MAP_BOTTOM - RL_MAP_TOP ) / ( RL_VIS_LAYERS - 1 );
	*y = RL_MAP_TOP + ( L - s_rlMap.first ) * step;
}

// ACTS: the boss guarding floor f (the Abyss: the same "random" one the game picks)
static int RL_BossForFloor( int f ) {
	int		act = DS_ActForFloor( f );

	if ( act >= 1 && act <= DS_NUM_ACTS && ds_acts[act].boss >= 0 ) {
		return ds_acts[act].boss;
	}
	return (int)( ( (unsigned)rl.seed * 2654435761u + (unsigned)f * 40503u ) >> 7 ) % BOSS_NUM;
}


static const char *RL_ActLabel( int f ) {
	int		act = DS_ActForFloor( f );

	if ( act >= DS_ACT_ABYSS ) {
		return va( "THE ABYSS  -  DEPTH %i", f - DS_WIN_FLOOR );
	}
	return va( "ACT %s  -  %s", ds_roman[act], ds_acts[act].name );
}

static float RL_NodeSize( int L, int i ) {
	return rl_layers[L].type[i] == RLN_BOSS ? 34 : 24;
}

static void RL_DrawNode( int L, int i, qboolean focus ) {
	rlLayer_t	*ly;
	float		x;
	float		y;
	float		sz;
	float		k;
	float		p;
	int			type;
	qboolean	visited;
	qboolean	reach;
	qboolean	current;
	vec4_t		edge;
	vec4_t		c;

	ly = &rl_layers[L];
	type = ly->type[i];
	RL_NodePos( L, i, &x, &y );
	sz = RL_NodeSize( L, i );
	visited = RL_Visited( L, i );
	reach = RL_Reachable( L, i );
	current = ( visited && L == rl.layer - 1 );

	// brightness by state
	if ( reach ) {
		k = 1.0f;
	}
	else if ( visited ) {
		k = 0.75f;
	}
	else if ( L > rl.layer ) {
		k = 0.55f;
	}
	else {
		k = 0.22f;		// a road not taken
	}

	if ( reach ) {
		p = RL_Pulse( 0.005f );
		RL_Scale( c, rl_nodeColors[type], ( focus ? 0.7f : 0.3f ) + 0.25f * p, 1 );
		RL_Pic( x - sz * 1.4f, y - sz * 1.4f, sz * 2.8f, sz * 2.8f, rl_shGlow, c );
	}
	if ( current ) {
		p = RL_Pulse( 0.004f );
		RL_Scale( c, rl_colOrange, 0.6f + 0.4f * p, 1 );
		RL_Pic( x - sz * 0.95f, y - sz * 0.95f, sz * 1.9f, sz * 1.9f, rl_shRing, c );
	}

	RL_Scale( edge, rl_nodeColors[type], k, 1 );
	RL_Frame( x - sz / 2, y - sz / 2, sz, sz, visited ? rl_colPanelHi : rl_colBlack, edge );
	if ( focus ) {
		RL_Frame( x - sz / 2 - 3, y - sz / 2 - 3, sz + 6, sz + 6, NULL, rl_colOrange );
	}

	c[0] = c[1] = c[2] = k;
	c[3] = 1;
	if ( type == RLN_VAULT ) {
		RL_Scale( c, rl_nodeColors[RLN_VAULT], k * ( 0.75f + 0.25f * RL_Pulse( 0.003f ) ), 1 );
		RL_Text( x, y - 4, "???", 5, 8, c, 1 );		// ACTS: the secret vault
	}
	else {
		RL_Pic( x - sz * 0.36f, y - sz * 0.36f, sz * 0.72f, sz * 0.72f, rl_nodeIcons[type], c );
	}

	if ( current ) {
		RL_TextS( x + sz / 2 + 4, y - 4, "YOU", 5, 8, rl_colOrange, 0 );	// beside: the paths run above and below
	}
	if ( type != RLN_VAULT ) {
		UI_DSM_DrawNodeTag( x, y, sz, rl_maps[ly->map[i]], ly->mflags[i], k, L >= rl.layer );		// ds-maps: NEW / REVISITED
	}
	// ds-surprise: a floor further down that offers a modifier: a small red mark (the chip shows when it's reachable)
	if ( L > rl.layer && UI_DS_NodeMod( rl.seed, L, i, type ) ) {
		vec4_t	m = { 1.0f, 0.28f, 0.2f, 1.0f };

		m[3] = k;
		UI_FillRect( x + sz / 2 - 3, y - sz / 2 - 3, 6, 6, m );
	}
}

// the node the side panel describes: the one under the mouse, else the focused one
static qboolean RL_PanelNode( int *outL, int *outI ) {
	menucommon_s	*item;
	float			x;
	float			y;
	float			sz;
	int				L;
	int				i;

	for ( L = s_rlMap.first; L <= s_rlMap.last; L++ ) {
		for ( i = 0; i < rl_layers[L].n; i++ ) {
			RL_NodePos( L, i, &x, &y );
			sz = RL_NodeSize( L, i ) + 6;
			if ( uis.cursorx >= x - sz / 2 && uis.cursorx <= x + sz / 2 && uis.cursory >= y - sz / 2 && uis.cursory <= y + sz / 2 ) {
				*outL = L;
				*outI = i;
				return qtrue;
			}
		}
	}

	item = (menucommon_s *)Menu_ItemAtCursor( &s_rlMap.menu );
	if ( item && item->id >= ID_RL_ITEM0 && item->id < ID_RL_ITEM0 + rl.numReach ) {
		*outL = rl.layer;
		*outI = rl.reach[item->id - ID_RL_ITEM0];
		return qtrue;
	}
	if ( item && UI_DS_SurpriseChipNode( item->id, outL, outI ) ) {
		return qtrue;		// ds-surprise: a modifier chip stands for its node
	}
	return qfalse;
}

static void RL_DrawInfoPanel( void ) {
	rlLayer_t	*ly;
	float		x;
	float		y;
	float		w;
	qhandle_t	shot;
	int			L;
	int			i;
	int			type;
	int			f;
	int			skill;
	vec4_t		c;

	x = 480 + rl_ext;
	w = RL_PANEL_W;
	RL_Frame( x, RL_PANEL_TOP, w, RL_PANEL_BOTTOM - RL_PANEL_TOP, rl_colPanel, rl_colLine );

	if ( !RL_PanelNode( &L, &i ) ) {
		UI_DrawProportionalString( (int)( x + w / 2 ), RL_PANEL_TOP + 6, "THE WAY DOWN", UI_CENTER|UI_SMALLFONT, rl_colOrange );
		y = RL_TextWrap( x + 8, RL_PANEL_TOP + 36, w - 16, "Point at a floor to see what waits there.", 6, 10, rl_colDim, 4 );
		y += 10;
		for ( type = 0; type < RLN_VAULT; type++ ) {		// (the vault stays a secret)
			RL_Frame( x + 8, y, 14, 14, rl_colBlack, rl_nodeColors[type] );
			RL_Pic( x + 10, y + 2, 10, 10, rl_nodeIcons[type], NULL );
			RL_Text( x + 28, y + 2, rl_nodeNames[type], 7, 10, rl_nodeColors[type], 0 );
			y = RL_TextWrap( x + 28, y + 15, w - 36, RL_NodeDesc( type ), 5, 8, rl_colDim, 3 );
			y += 8;
		}
		return;
	}

	ly = &rl_layers[L];
	type = ly->type[i];
	f = L + 1;

	UI_DrawProportionalString( (int)( x + w / 2 ), RL_PANEL_TOP + 6, rl_nodeNames[type], UI_CENTER|UI_SMALLFONT, rl_nodeColors[type] );
	RL_Text( x + w / 2, RL_PANEL_TOP + 30, va( "FLOOR %i", f ), 6, 9, rl_colDim, 1 );

	// level shot
	y = RL_PANEL_TOP + 44;
	RL_Frame( x + 6, y, w - 12, ( w - 12 ) * 0.75f, rl_colBlack, rl_colLine );
	shot = RL_MapShot( ly->map[i] );
	if ( shot ) {
		c[0] = c[1] = c[2] = ( RL_Visited( L, i ) || RL_Reachable( L, i ) || L > rl.layer ) ? 1.0f : 0.4f;
		c[3] = 1;
		RL_Pic( x + 7, y + 1, w - 14, ( w - 12 ) * 0.75f - 2, shot, c );
	}
	y += ( w - 12 ) * 0.75f + 6;

	y = RL_TextWrap( x + 8, y, w - 16, RL_MapLongName( ly->map[i] ), 7, 11, rl_colBone, 2 );
	y = UI_DSM_DrawMapLine( x + 8, y, w - 16, rl_maps[ly->map[i]], type == RLN_VAULT ? 0 : ly->variant[i], ly->mflags[i], L < rl.layer );		// ds-maps

	skill = RL_NodeSkill100( f );
	if ( type == RLN_BOSS ) {
		// ACTS: who waits there
		RL_Text( x + 8, y, ds_bosses[RL_BossForFloor( f )].name, 7, 11, rl_nodeColors[RLN_BOSS], 0 );
		y += 13;
		RL_Text( x + 8, y, ds_bosses[RL_BossForFloor( f )].title, 5, 8, rl_colDim, 0 );
		y += 11;
		RL_Text( x + 8, y, va( "ADDS %i", RL_BossTimed() ? 0 : ly->bots[i] - 1 ), 6, 10, rl_colBone, 0 );	// (timed: just the boss)
		RL_Text( x + w - 8, y, va( "SKILL %i.%02i", skill / 100, skill % 100 ), 6, 10, rl_colBone, 2 );
		y += 13;
	}
	else if ( type == RLN_VAULT ) {
		RL_Text( x + 8, y, "NO ENEMIES", 6, 10, rl_colBone, 0 );
		RL_Text( x + w - 8, y, va( "%i SECONDS", DS_VAULT_TIME / 1000 ), 6, 10, rl_colBone, 2 );
		y += 13;
	}
	else {
		RL_Text( x + 8, y, va( "BOTS %i", ly->bots[i] ), 6, 10, rl_colBone, 0 );
		RL_Text( x + w - 8, y, va( "SKILL %i.%02i", skill / 100, skill % 100 ), 6, 10, rl_colBone, 2 );
		y += 13;
		RL_Text( x + 8, y, va( "FRAGS TO WIN %i", ly->frags[i] ), 6, 10, rl_colBone, 0 );
		y += 13;
	}
	if ( RL_NodeTier( f ) > 0 ) {
		RL_Text( x + 8, y, va( "%s TIER +%i", f > DS_WIN_FLOOR ? "ABYSS" : "THREAT", RL_NodeTier( f ) ), 6, 10, rl_colBad, 0 );
		y += 13;
	}
	y += 3;

	if ( type == RLN_EVENT && ly->mut[i] > 0 && ly->mut[i] < MUT_NUM_MUTATORS ) {
		RL_Text( x + 8, y, bg_rmMutatorNames[ly->mut[i]], 6, 10, rl_nodeColors[RLN_EVENT], 0 );
		y += 12;
		y = RL_TextWrap( x + 8, y, w - 16, bg_rmMutatorHints[ly->mut[i]], 5, 8, rl_colDim, 2 );
		y += 3;
	}
	y = RL_TextWrap( x + 8, y, w - 16, RL_NodeDesc( type ), 5, 8, rl_colDim, 2 );
	y += 3;
	RL_Text( x + 8, y, "REWARD", 5, 8, rl_colOrange, 0 );
	y = RL_TextWrap( x + 44, y, w - 52, UI_DS_DailyOn() ? rl_nodeRewardsDaily[type] : rl_nodeRewards[type], 5, 8, rl_colBone, 2 );
	y = UI_DS_SurprisePanel( rl.seed, L, i, type, x, y, w, RL_Reachable( L, i ) );		// ds-surprise: the floor modifier on offer

	// what this node is to you
	if ( RL_Reachable( L, i ) ) {
		Vector4Copy( rl_colOrange, c );
		c[3] = 0.6f + 0.4f * RL_Pulse( 0.006f );
		RL_Text( x + w / 2, RL_PANEL_BOTTOM - 16, "CLICK OR ENTER: DESCEND", 6, 9, c, 1 );
	}
	else if ( RL_Visited( L, i ) ) {
		RL_Text( x + w / 2, RL_PANEL_BOTTOM - 16, "CLEARED", 6, 9, rl_colGood, 1 );
	}
	else if ( L > rl.layer ) {
		RL_Text( x + w / 2, RL_PANEL_BOTTOM - 16, "FURTHER DOWN", 6, 9, rl_colDim, 1 );
	}
	else {
		RL_Text( x + w / 2, RL_PANEL_BOTTOM - 16, "NOT ON YOUR PATH", 6, 9, rl_colDark, 1 );
	}
}

/*
=================
RL_DrawBuild

The owned cards, grouped by kind, stacks counted
=================
*/
void RL_DrawBuild( float x, float top, float w, float bottom ) {
	int		counts[RLC_NUM];
	int		kindStep;
	int		i;
	int		kind;
	int		distinct;
	int		shown;
	int		columns;
	int		col;
	float	y;
	float	cw;
	float	ch;
	float	step;
	char	line[64];

	memset( counts, 0, sizeof( counts ) );
	distinct = 0;
	for ( i = 0; i < rl.numCards; i++ ) {
		if ( !counts[rl.cards[i]] ) {
			distinct++;
		}
		counts[rl.cards[i]]++;
	}

	if ( !distinct ) {
		RL_TextWrap( x, top, w, "No cards yet. Win a floor to earn one.", 6, 10, rl_colDim, 4 );
		return;
	}

	// wide areas (the run-over screen) use columns
	columns = ( w >= 260 ) ? 2 : 1;
	col = 0;
	cw = 6;
	ch = 10;
	step = 12;
	if ( distinct * step > ( bottom - top ) * columns ) {
		cw = 5;
		ch = 8;
		step = 9;
	}

	y = top;
	shown = 0;
	for ( kindStep = 0, kind = RLK_LEGEND; kindStep < 8; kindStep++, kind = kindStep - 1 ) {	// legendaries first
		for ( i = 0; i < RLC_NUM; i++ ) {
			if ( !counts[i] || bg_rlCardKind[i] != kind ) {
				continue;
			}
			if ( y + step > bottom && col + 1 < columns ) {
				col++;
				x += w / columns;
				y = top;
			}
			if ( y + step > bottom ) {
				RL_Text( x, y, va( "+%i MORE", distinct - shown ), cw, ch, rl_colDim, 0 );
				return;
			}
			UI_FillRect( x, y + 1, ch - 2, ch - 2, rl_kindColors[kind] );
			if ( counts[i] > 1 ) {
				Com_sprintf( line, sizeof( line ), "%s x%i", bg_rlCardNames[i], counts[i] );
			}
			else {
				Q_strncpyz( line, bg_rlCardNames[i], sizeof( line ) );
			}
			RL_Text( x + ch + 2, y, line, cw, ch, rl_colBone, 0 );
			y += step;
			shown++;
		}
	}
}

static void RL_DrawBuildPanel( void ) {
	float	x;
	float	w;
	float	y;
	float	lx;
	int		kind;
	const char	*label;

	x = 8 - rl_ext;
	w = RL_PANEL_W;
	RL_Frame( x, RL_PANEL_TOP, w, RL_PANEL_BOTTOM - RL_PANEL_TOP, rl_colPanel, rl_colLine );
	UI_DrawProportionalString( (int)( x + w / 2 ), RL_PANEL_TOP + 6, "YOUR BUILD", UI_CENTER|UI_SMALLFONT, rl_colOrange );

	y = RL_PANEL_TOP + 32;
	RL_Text( x + 8, y, va( "LIVES %i", rl.lives ), 7, 11, rl.lives <= 1 ? rl_colBad : rl_colBone, 0 );
	// en-screens: the daily pays no skulls, so its build panel names the mode instead of "SKULLS 0"
	RL_Text( x + w - 8, y, UI_DS_DailyOn() ? "DAILY" : va( "SKULLS %i", rl.skulls ), 7, 11, rl_colOrange, 2 );
	y += 14;
	RL_Text( x + 8, y, va( "KILLS %i", rl.kills ), 6, 10, rl_colDim, 0 );
	RL_Text( x + w - 8, y, RL_TimeString( rl.time ), 6, 10, rl_colDim, 2 );
	y += 13;
	if ( rl.rerolls > 0 ) {
		RL_Text( x + 8, y, va( "REROLLS %i", rl.rerolls ), 6, 10, rl_colDim, 0 );
		y += 13;
	}
	UI_FillRect( x + 8, y + 2, w - 16, 1, rl_colLine );
	y += 8;

	RL_DrawBuild( x + 8, y, w - 16, RL_PANEL_BOTTOM - 22 );

	// legend
	lx = x + 8;
	for ( kind = 0; kind < 4; kind++ ) {
		label = ( kind == 1 ) ? "ALT" : rl_kindNames[kind];
		UI_FillRect( lx, RL_PANEL_BOTTOM - 13, 6, 6, rl_kindColors[kind] );
		RL_Text( lx + 8, RL_PANEL_BOTTOM - 14, label, 4, 8, rl_colDim, 0 );
		lx += 8 + strlen( label ) * 4 + 8;
	}
}

static void RL_MapDraw( void ) {
	rlLayer_t		*ly;
	menucommon_s	*item;
	float			x;
	float			y;
	float			x2;
	float			y2;
	float			dx;
	float			dy;
	float			len;
	float			r1;
	float			r2;
	float			step;
	int				L;
	int				i;
	int				j;
	int				focusNode;
	int				bossIn;
	char			actLabel[64];
	char			bossLabel[32];
	vec4_t			c;

	RL_Backdrop( !s_rlMap.ingame );

	bossIn = DS_FLOORS_PER_ACT - ( rl.layer + 1 ) % DS_FLOORS_PER_ACT;
	if ( bossIn == DS_FLOORS_PER_ACT ) {
		bossIn = 0;
	}
	// (va's buffers rotate: every piece goes through its own buffer first)
	Q_strncpyz( actLabel, RL_ActLabel( rl.layer + 1 ), sizeof( actLabel ) );
	Q_strncpyz( bossLabel, bossIn == 0 ? "A BOSS GUARDS THIS FLOOR" : bossIn == 1 ? "BOSS ON THE NEXT FLOOR" : va( "BOSS IN %i FLOORS", bossIn ), sizeof( bossLabel ) );
	RL_Header( "DESCENT", va( "FLOOR %i   |   %s   |   %s   |   %s", rl.layer + 1, actLabel, rl_skillNames[rl.difficulty - 1], bossLabel ) );

	// the map area
	x = RL_MapLeft();
	RL_Frame( x, RL_PANEL_TOP, RL_MapRight() - x, RL_PANEL_BOTTOM - RL_PANEL_TOP, rl_colPanel, rl_colLine );

	step = (float)( RL_MAP_BOTTOM - RL_MAP_TOP ) / ( RL_VIS_LAYERS - 1 );
	for ( L = s_rlMap.first; L <= s_rlMap.last; L++ ) {
		y = RL_MAP_TOP + ( L - s_rlMap.first ) * step;
		if ( rl_layers[L].type[0] == RLN_BOSS ) {
			c[0] = 0.35f;
			c[1] = 0.02f;
			c[2] = 0.02f;
			c[3] = 0.35f;
			UI_FillRect( x + 1, y - step * 0.4f, RL_MapRight() - x - 2, step * 0.8f, c );
		}
		if ( L == rl.layer ) {
			RL_Scale( c, rl_colOrange, 1, 0.08f );
			UI_FillRect( x + 1, y - step * 0.45f, RL_MapRight() - x - 2, step * 0.9f, c );
		}
		RL_Text( x + 6, y - 4, va( "%i", L + 1 ), 6, 9, L == rl.layer ? rl_colOrange : rl_colDark, 0 );
		if ( rl_layers[L].type[0] == RLN_BOSS ) {
			RL_Text( RL_MapRight() - 6, y - 4, ds_bosses[RL_BossForFloor( L + 1 )].name, 5, 8, rl_nodeColors[RLN_BOSS], 2 );
		}
		// ACTS: where an act begins
		if ( L > s_rlMap.first && L % DS_FLOORS_PER_ACT == 0 ) {
			RL_Scale( c, rl_colLine, 1, 0.9f );
			UI_FillRect( x + 1, y - step * 0.5f, RL_MapRight() - x - 2, 1, c );
			RL_Text( RL_MapRight() - 6, y - step * 0.5f + 2, RL_ActLabel( L + 1 ), 4, 7, rl_colOrange, 2 );
		}
	}
	RL_Text( RL_MapRight() - 6, RL_PANEL_TOP + 5, RL_ActLabel( s_rlMap.first + 1 ), 4, 7, rl_colDim, 2 );

	// paths (dotted)
	for ( L = s_rlMap.first; L < s_rlMap.last; L++ ) {
		ly = &rl_layers[L];
		for ( i = 0; i < ly->n; i++ ) {
			for ( j = 0; j < rl_layers[L + 1].n; j++ ) {
				if ( !( ly->edges[i] & ( 1 << j ) ) ) {
					continue;
				}
				RL_NodePos( L, i, &x, &y );
				RL_NodePos( L + 1, j, &x2, &y2 );
				dx = x2 - x;
				dy = y2 - y;
				len = sqrt( dx * dx + dy * dy );
				if ( len < 1 ) {
					continue;
				}
				r1 = RL_NodeSize( L, i ) * 0.5f + 5;
				r2 = RL_NodeSize( L + 1, j ) * 0.5f + 5;
				x += dx * r1 / len;
				y += dy * r1 / len;
				x2 -= dx * r2 / len;
				y2 -= dy * r2 / len;

				if ( RL_Visited( L, i ) && RL_Visited( L + 1, j ) ) {
					RL_Dots( x, y, x2, y2, rl_colBone, 4, 2 );
				}
				else if ( RL_Visited( L, i ) && RL_Reachable( L + 1, j ) ) {
					Vector4Copy( rl_colOrange, c );
					c[3] = 0.55f + 0.45f * RL_Pulse( 0.005f );
					RL_Dots( x, y, x2, y2, c, 4, 2.2f );
				}
				else {
					RL_Scale( c, rl_colDim, 1, L >= rl.layer ? 0.35f : 0.15f );
					RL_Dots( x, y, x2, y2, c, 5, 1.5f );
				}
			}
		}
	}

	// the entrance, above the first floor
	if ( s_rlMap.first == 0 ) {
		x = ( RL_MapLeft() + RL_MapRight() ) * 0.5f;
		RL_Text( x, RL_PANEL_TOP + 5, rl.layer == 0 ? "YOU STAND AT THE GATE" : "THE GATE", 5, 8, rl.layer == 0 ? rl_colOrange : rl_colDark, 1 );
	}

	// nodes
	item = (menucommon_s *)Menu_ItemAtCursor( &s_rlMap.menu );
	focusNode = -1;
	if ( item && item->id >= ID_RL_ITEM0 && item->id < ID_RL_ITEM0 + rl.numReach ) {
		focusNode = rl.reach[item->id - ID_RL_ITEM0];
	}
	for ( L = s_rlMap.first; L <= s_rlMap.last; L++ ) {
		for ( i = 0; i < rl_layers[L].n; i++ ) {
			RL_DrawNode( L, i, ( L == rl.layer && i == focusNode ) );
		}
	}

	RL_DrawBuildPanel();
	RL_DrawInfoPanel();

	Menu_Draw( &s_rlMap.menu );

	RL_Footer( "PICK YOUR NEXT FLOOR: CLICK A GLOWING NODE, OR TAB / WASD THEN ENTER" );
}

static void RL_QuitAction( qboolean result ) {
	if ( !result ) {
		return;
	}
	RL_RegisterCvars();
	trap_Cvar_Set( "ui_rlOpenHub", "1" );
	UI_ForceMenuOff();
	trap_Cmd_ExecuteText( EXEC_APPEND, "disconnect\n" );
}

static void RL_QuitDraw( void ) {
	UI_DrawProportionalString( 320, 230, "YOUR DESCENT IS SAVED", UI_CENTER|UI_SMALLFONT, color_red );
}

static void RL_MapEvent( void *ptr, int event ) {
	int		id;

	if ( event != QM_ACTIVATED ) {
		return;
	}
	id = ((menucommon_s *)ptr)->id;
	if ( id == ID_RL_BACK ) {
		if ( s_rlMap.ingame ) {
			UI_ConfirmMenu( "QUIT TO THE MENU?", RL_QuitDraw, RL_QuitAction );
		}
		else {
			UI_PopMenu();
		}
		return;
	}
	if ( id == ID_RL_DIFFICULTY ) {
		UI_DS_DifficultyMenu();		// ds-economy
		return;
	}
	if ( id >= ID_RL_ITEM0 && id < ID_RL_ITEM0 + rl.numReach ) {
		RL_LaunchNode( rl.layer, rl.reach[id - ID_RL_ITEM0] );
	}
}

static sfxHandle_t RL_MapKey( int key ) {
	key = RL_KeyRemap( key );
	if ( ( key == K_ESCAPE || key == K_MOUSE2 ) && s_rlMap.ingame ) {
		UI_ConfirmMenu( "QUIT TO THE MENU?", RL_QuitDraw, RL_QuitAction );
		return menu_out_sound;
	}
	return Menu_DefaultKey( &s_rlMap.menu, key );
}

void RL_MapMenu( void ) {
	float	x;
	float	y;
	float	sz;
	int		k;
	int		i;

	memset( &s_rlMap, 0, sizeof( s_rlMap ) );
	s_rlMap.ingame = RL_InGame();
	RL_SetupMenu( &s_rlMap.menu, RL_MapDraw, s_rlMap.ingame );
	s_rlMap.menu.key = RL_MapKey;

	RL_LoadRun();
	RL_ComputeReach();

	s_rlMap.first = rl.layer - 2;
	if ( s_rlMap.first < 0 ) {
		s_rlMap.first = 0;
	}
	s_rlMap.last = s_rlMap.first + RL_VIS_LAYERS - 1;
	if ( UI_DS_DailyOn() && s_rlMap.last > DSD_FLOORS - 1 ) {
		s_rlMap.last = DSD_FLOORS - 1;		// en-daily: the daily ends with the act 1 boss
	}

	for ( k = 0; k < rl.numReach; k++ ) {
		i = rl.reach[k];
		RL_NodePos( rl.layer, i, &x, &y );
		sz = RL_NodeSize( rl.layer, i ) + 10;
		// an invisible hit box: RL_MapDraw draws the node and its focus
		RL_InitButton( &s_rlMap.nodes[k], ID_RL_ITEM0 + k, x - sz / 2, y - sz / 2, sz, sz, rl_nodeNames[rl_layers[rl.layer].type[i]], RL_MapEvent );
		s_rlMap.nodes[k].bm.generic.ownerdraw = RL_NoDraw;
		Menu_AddItem( &s_rlMap.menu, &s_rlMap.nodes[k] );
	}

	// ds-surprise: pick-your-poison chips under the reachable nodes that offer a floor modifier
	UI_DS_SurpriseMapReset();
	for ( k = 0; k < rl.numReach; k++ ) {
		i = rl.reach[k];
		RL_NodePos( rl.layer, i, &x, &y );
		UI_DS_SurpriseAddChip( &s_rlMap.menu, rl.seed, k, rl.layer, i, rl_layers[rl.layer].type[i], x, y + RL_NodeSize( rl.layer, i ) * 0.5f + 2 );
	}

	RL_InitButton( &s_rlMap.quit, ID_RL_BACK, 8 - rl_ext, 446, 152, 26, s_rlMap.ingame ? "SAVE & QUIT" : "BACK", RL_MapEvent );
	s_rlMap.quit.center = qtrue;
	Menu_AddItem( &s_rlMap.menu, &s_rlMap.quit );
	// ds-economy: the difficulty for the next floors (bottom right, under the info panel)
	UI_DS_InitDiffButton( &s_rlMap.diff, ID_RL_DIFFICULTY, 480 + rl_ext, 442, RL_PANEL_W, 22, RL_MapEvent );
	Menu_AddItem( &s_rlMap.menu, &s_rlMap.diff );
	if ( UI_DS_DailyOn() ) {
		s_rlMap.diff.bm.generic.flags |= QMF_HIDDEN | QMF_INACTIVE;		// en-daily: the difficulty is fixed
	}

	if ( s_rlMap.ingame ) {
		uis.menusp = 0;
	}
	UI_PushMenu( &s_rlMap.menu );
}


// the run over screen: the run summary in ui_ds_meta.c


/*
=======================================================================

HOOKS

=======================================================================
*/

/*
=================
RL_Cache
=================
*/
void RL_Cache( void ) {
	int		i;

	rl_shRing = trap_R_RegisterShaderNoMip( "rearmed/ring" );
	rl_shGlow = trap_R_RegisterShaderNoMip( "rearmed/glow" );
	rl_shVignette = trap_R_RegisterShaderNoMip( "rearmed/evVignette" );
	for ( i = 0; i < RLN_NUM; i++ ) {
		rl_nodeIcons[i] = trap_R_RegisterShaderNoMip( rl_nodeIconNames[i] );
	}
	for ( i = 0; i < 5; i++ ) {
		rl_skillIcons[i] = trap_R_RegisterShaderNoMip( va( "menu/art/skill%i", i + 1 ) );
	}
	rl_buySound = trap_S_RegisterSound( "sound/misc/w_pkup.wav", qfalse );
	UI_DS_Cache();
}

/*
=================
UI_RL_MainMenuOpened

The main menu came up: restore the normal game mode, and open the hub
when a descent was quit from in game.
=================
*/
void UI_RL_MainMenuOpened( void ) {
	qboolean	daily;

	UI_RL_RestoreMode();
	UI_DS_BossPracOff();		// en-boss: no boss practice outside its own launch
	daily = UI_DS_DailyMainMenu();		// en-daily: the flag goes off here
	RL_RegisterCvars();
	if ( trap_Cvar_VariableValue( "ui_rlOpenHub" ) ) {
		trap_Cvar_Set( "ui_rlOpenHub", "0" );
		UI_RL_HubMenu();
		if ( daily ) {
			UI_DS_DailyMenu();		// en-daily: a daily floor was quit: its screen again
		}
	}
}

/*
=================
UI_RL_ConsoleCommand
=================
*/
qboolean UI_RL_ConsoleCommand( const char *cmd ) {
	const char	*arg;
	char		msg[64];
	int			n;
	int			i;
	char		cmdBuf[64];

	// cmd is UI_Argv( 0 )'s static buffer: every later UI_Argv( n ) would overwrite it
	Q_strncpyz( cmdBuf, cmd, sizeof( cmdBuf ) );
	cmd = cmdBuf;
	if ( UI_DS_DailyCommand( cmd ) ) {		// en-daily: rlover of a daily, dsdaily*, "descent" leaves the daily
		return qtrue;
	}
	if ( UI_DS_ConsoleCommand( cmd ) ) {		// META: descent collection / characters / history, rlvictory, dev
		return qtrue;
	}
	if ( UI_DS_EconCommand( cmd ) ) {		// ds-economy dev: dsdiff dsdiffmenu dsreset dsabandon
		return qtrue;
	}
	if ( UI_DSM_ConsoleCommand( cmd ) ) {		// ds-maps dev: dsmaps, dsmapsim
		return qtrue;
	}
	if ( UI_DS_BossPracCommand( cmd ) ) {		// en-boss: dsbossprac [1-4], dspracend (the game, after a practice fight)
		return qtrue;
	}
	if ( UI_DS_HallCommand( cmd ) ) {		// en-memory: dshall [rivals]
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "descent" ) || !Q_stricmp( cmd, "roguelike" ) ) {
		arg = UI_Argv( 1 );
		if ( !Q_stricmp( arg, "new" ) ) {
			// scripted / bindable: start a run at a difficulty (1-5)
			n = atoi( UI_Argv( 2 ) );
			if ( n < 1 || n > 5 ) {
				n = 3;
			}
			if ( RL_InGame() ) {
				uis.menusp = 0;
			}
			RL_StartRun( n );
		}
		else if ( !Q_stricmp( arg, "upgrades" ) ) {
			UI_RL_HubMenu();
			UI_DS_UpgradesMenu();
		}
		else if ( !Q_stricmp( arg, "records" ) ) {
			UI_RL_HubMenu();
			UI_DS_RecordsMenu();
		}
		else {
			UI_RL_HubMenu();
		}
		return qtrue;
	}

	if ( !Q_stricmp( cmd, "rlmap" ) ) {
		if ( RL_InGame() ) {
			uis.menusp = 0;
		}
		RL_OpenRun();
		return qtrue;
	}

	if ( !Q_stricmp( cmd, "rlover" ) ) {
		if ( RL_InGame() ) {
			uis.menusp = 0;
		}
		UI_DS_SummaryMenu();
		return qtrue;
	}

	// ds-surprise dev: dsmods (the floor modifiers on offer), dsmodnode <reach index> [on|off]
	if ( !Q_stricmp( cmd, "dsmods" ) || !Q_stricmp( cmd, "dsmodnode" ) ) {
		int		types[RL_MAX_NODES];
		int		L;

		RL_LoadRun();
		if ( !rl.active ) {
			Com_Printf( "%s: no descent in progress\n", cmd );
			return qtrue;
		}
		RL_ComputeReach();
		for ( i = 0; i < rl.numReach; i++ ) {
			types[i] = rl_layers[rl.layer].type[rl.reach[i]];
		}
		if ( !Q_stricmp( cmd, "dsmodnode" ) ) {
			return UI_DS_SurpriseCommand( cmd, rl.seed, rl.layer, rl.reach, rl.numReach, types );
		}
		for ( L = rl.layer; L < rl.layer + RL_VIS_LAYERS && L < rl_layersBuilt; L++ ) {
			Com_Printf( "floor %2i:", L + 1 );
			for ( i = 0; i < rl_layers[L].n; i++ ) {
				n = UI_DS_NodeMod( rl.seed, L, i, rl_layers[L].type[i] );
				Com_Printf( " %s%s%s", rl_nodeKeys[rl_layers[L].type[i]], n ? "/" : "", n ? ds_modifiers[n].key : "" );
				if ( n && UI_DS_NodeModOn( L, i ) ) {
					Com_Printf( "(ON)" );
				}
			}
			Com_Printf( "\n" );
		}
		return qtrue;
	}

	// dev: pick the n-th reachable node (0-based) and launch it
	if ( !Q_stricmp( cmd, "rlpicknode" ) ) {
		RL_LoadRun();
		if ( !rl.active ) {
			Com_Printf( "rlpicknode: no descent in progress\n" );
			return qtrue;
		}
		RL_ComputeReach();
		n = atoi( UI_Argv( 1 ) );
		Com_Printf( "rlpicknode: floor %i, %i reachable:", rl.layer + 1, rl.numReach );
		for ( i = 0; i < rl.numReach; i++ ) {
			Com_Printf( " %i=%s/%s", i, rl_nodeNames[rl_layers[rl.layer].type[rl.reach[i]]], rl_maps[rl_layers[rl.layer].map[rl.reach[i]]] );
		}
		Com_Printf( "\n" );
		if ( n < 0 || n >= rl.numReach ) {
			Com_Printf( "rlpicknode: no node %i\n", n );
			return qtrue;
		}
		RL_LaunchNode( rl.layer, rl.reach[n] );
		return qtrue;
	}

	// ACTS dev: dsact <n> (jump the run to act n's first floor), dsboss <n> (play act n's boss now;
	// 5 = the first Abyss boss), dsvault (play a vault now), dslayers [count] (print the floors)
	if ( !Q_stricmp( cmd, "dsact" ) || !Q_stricmp( cmd, "dsboss" ) || !Q_stricmp( cmd, "dsvault" ) || !Q_stricmp( cmd, "dslayers" ) ) {
		char	path[RL_VAL_LEN];

		RL_LoadRun();
		if ( !rl.active ) {
			Com_Printf( "%s: no descent in progress (descent new <difficulty> first)\n", cmd );
			return qtrue;
		}
		if ( !Q_stricmp( cmd, "dslayers" ) ) {
			n = atoi( UI_Argv( 1 ) );
			if ( n <= 0 || n > RL_MAX_LAYERS ) {
				n = 31;
			}
			RL_BuildLayers( rl.seed, n );
			for ( i = 0; i <= DS_ACT_ABYSS; i++ ) {
				Com_Printf( "act %i pool:", i );
				for ( n = 0; n < rl_actNumMaps[i] && n < 24; n++ ) {
					Com_Printf( " %s", rl_maps[rl_actMaps[i][n]] );
				}
				Com_Printf( "\n" );
			}
			for ( i = 0; i < rl_layersBuilt; i++ ) {
				Com_Printf( "floor %2i act %i:", i + 1, DS_ActForFloor( i + 1 ) );
				for ( n = 0; n < rl_layers[i].n; n++ ) {
					Com_Printf( " %s/%s/b%i/f%i", rl_nodeKeys[rl_layers[i].type[n]], rl_maps[rl_layers[i].map[n]],
						rl_layers[i].bots[n], rl_layers[i].frags[n] );
				}
				Com_Printf( "\n" );
			}
			return qtrue;
		}
		n = atoi( UI_Argv( 1 ) );
		if ( !Q_stricmp( cmd, "dsvault" ) ) {
			n = rl.floor;
		} else if ( n < 1 || n > DS_ACT_ABYSS ) {
			Com_Printf( "%s: act 1-%i\n", cmd, DS_ACT_ABYSS );
			return qtrue;
		} else {
			n = !Q_stricmp( cmd, "dsboss" ) ? n * DS_FLOORS_PER_ACT - 1 : ( n - 1 ) * DS_FLOORS_PER_ACT;
		}
		// the floors before: cleared along the left-most path
		path[0] = 0;
		for ( i = 0; i < n && i < RL_MAX_LAYERS - 1; i++ ) {
			Q_strcat( path, sizeof( path ), i ? ".0" : "0" );
		}
		RL_KVSetInt( &rl_run, "floor", n );
		RL_KVSet( &rl_run, "map", path );
		RL_KVSetInt( &rl_run, "act", DS_ActForFloor( n + 1 ) );
		RL_KVSet( &rl_run, "infloor", "0" );
		RL_SaveRun();
		RL_LoadRun();
		Com_Printf( "%s: the run is now on floor %i (act %i)\n", cmd, n + 1, DS_ActForFloor( n + 1 ) );
		if ( !Q_stricmp( cmd, "dsact" ) ) {
			if ( RL_InGame() ) {
				uis.menusp = 0;
			}
			RL_MapMenu();
			return qtrue;
		}
		RL_ComputeReach();
		if ( !rl.numReach ) {
			return qtrue;
		}
		if ( !Q_stricmp( cmd, "dsvault" ) ) {
			rl_layers[rl.layer].type[rl.reach[0]] = RLN_VAULT;		// (dev: until the floors are rebuilt)
			rl_layers[rl.layer].bots[rl.reach[0]] = 0;
			rl_layers[rl.layer].mut[rl.reach[0]] = 0;
		}
		// dsboss <act> <map> / dsvault <map>: on that map (tests)
		arg = UI_Argv( !Q_stricmp( cmd, "dsvault" ) ? 1 : 2 );
		for ( i = 0; arg[0] && i < rl_numMaps; i++ ) {
			if ( !Q_stricmp( rl_maps[i], arg ) ) {
				rl_layers[rl.layer].map[rl.reach[0]] = i;
				UI_DSM_DevForce( rl.layer, rl.reach[0], i );		// ds-maps
				break;
			}
		}
		RL_LaunchNode( rl.layer, rl.reach[0] );
		return qtrue;
	}

	// dev: buy an upgrade by key (up_health ...)
	if ( !Q_stricmp( cmd, "rlbuy" ) ) {
		arg = UI_Argv( 1 );
		UI_DS_BuyKey( arg, msg, sizeof( msg ) );
		Com_Printf( "rlbuy: %s\n", msg );
		return qtrue;
	}

	return qfalse;
}
