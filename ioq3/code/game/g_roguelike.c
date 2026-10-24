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
// g_roguelike.c -- DESCENT (g_rearmMode 5): one level of a roguelike run.
//
// The run lives in rearmed_run.txt (the UI writes it at run start and when a node is chosen,
// the game reads it at init and writes the result after the level). Everyone starts with no
// movement tech: the pmove bits of every client are ANDed with the cards that client owns.
// Bots get as many cards as the player, from the same pool, reproducible from the run seed.
// A won level offers 3 (or 4) cards (rlpick n), a lost one costs a life; the map never
// advances by itself: the game runs "rlmap" (node map) or "rlover" (run over), both UI commands.

#include "g_local.h"

#include "bg_ds_daily.h"		// en-daily: the daily challenge has its own run file
#define RL_RUN_FILE		( G_RL_Daily() ? DSD_RUN_FILE : "rearmed_run.txt" )
#define RL_META_FILE	"rearmed_meta.txt"
#define RL_PRACTICE_FILE	"rearmed_practice.txt"		// en-boss: boss practice (written by the UI, only read here)

static qboolean	rlPractice;		// en-boss: a boss practice floor: nothing is saved

// en-boss: a boss practice floor (g_dsPractice): no file of the player's is written
qboolean G_RL_Practice( void ) {
	return rlPractice;
}
#define RL_MAX_KV		160		// meta: DESCENT v2 adds cnt_ / seen_ / up_ keys
#define RL_KEY_LEN		32
#define RL_VAL_LEN		1024
#define RL_FILE_MAX		65536

// node types
#define RLN_FIGHT		0
#define RLN_ELITE		1
#define RLN_BOSS		2
#define RLN_EVENT		3

// kit roles
#define RLR_NONE		0
#define RLR_PLAYER		1
#define RLR_BOT			2
#define RLR_ELITE		3
#define RLR_BOSS		4

// level phases
#define RLP_PLAY		0
#define RLP_END			1		// result decided, intermission coming
#define RLP_CHEST		2		// win with chests: the jackpot screen (g_ds_loot.c), waiting for dschestdone
#define RLP_OFFER		3		// win: cards on screen, waiting for rlpick
#define RLP_DONE		4		// picked (or lost): map / run over screen soon
#define RLP_SENT		5		// rlmap / rlover issued
#define RL_MAX_OFFER	5		// cards in one offer (FIRSTPICK upgrade: 5 on an act's first offer)

#define RL_OFFER_DELAY	900		// msec after the intermission starts before the cards are live
#define RL_MAP_DELAY	2600	// msec after the pick before the node map opens
#define RL_LOSS_DELAY	5200	// msec after a loss before the node map / run over screen
#define RL_MAX_BOTS		12

typedef struct {
	char	key[RL_KEY_LEN];
	char	val[RL_VAL_LEN];
} rlKV_t;

typedef struct {
	int		num;
	rlKV_t	kv[RL_MAX_KV];
} rlFile_t;

typedef struct {
	int		count[RLC_NUM];
	int		order[RL_MAX_CARDS];
	int		num;
	int		role;
	qboolean assigned;
	int		perks;
} rlKit_t;

typedef struct {
	char	name[32];
	int		role;
	float	skill;
	rlKit_t	kit;
} rlPlan_t;

typedef struct {
	qboolean	active;
	int			seed, difficulty, lives, floor, skulls, kills, time;
	int			cards[RL_MAX_CARDS];
	int			numCards;
	int			nodeType, nodeBots, nodeFrags, nodeMutator, nodeTier, nodeFloor;
	float		nodeSkill;
	int			upHealth, upSpeed, upDamage, upArmor, upCards, upSkulls, rerolls;

	// this level
	int			phase;
	int			result;			// 0 none, 1 win, 2 loss
	int			endTime;		// level.time the result was decided
	int			phaseTime;
	int			offer[RL_MAX_OFFER];
	int			numOffer;
	int			rerollsUsed;
	int			picked;			// card taken, -1 none
	int			levelKills;
	int			bossKills;		// elite / boss frags by the player
	int			bestStyle;
	int			reward;
	int			rewardParts[5];	// floor, kills, style, node, bonus percent
	qboolean	runOver;
	int			banked, newBestFloor, newBestKills;
	int			mutatorStarted;
	int			human;			// client number, -1 = not in yet
	int			humanScore;
} rlState_t;

static rlState_t	rl;
static rlFile_t		rlRun;
static rlFile_t		rlMeta;
static rlKit_t		rlKits[MAX_CLIENTS];
static rlPlan_t		rlPlan[RL_MAX_BOTS];
static int			rlNumPlan;
#define RL_MAX_EXTRA	12		// ds-surprise: bots that join mid-floor (ambush, ghost, fodder)
static rlPlan_t		rlExtra[RL_MAX_EXTRA];
static int			rlNumExtra;
static qboolean		rlFreshLevel;		// a new map (not a map_restart): old bots must go
static qboolean		rlBotsPending;		// this floor's bots still to add
static char			rlFileBuf[RL_FILE_MAX];
static int			rlKitsDirty;
static int			rlLuckPct;			// the player's luck while their offer is dealt (0 for bots)

static void G_RL_UpdateConfigstring( void );
static void G_RL_UpdateKitsConfigstring( void );

/*
===============================================================================

KEY / VALUE FILES

===============================================================================
*/

static const char *RL_Get( rlFile_t *f, const char *key ) {
	int		i;

	for ( i = 0 ; i < f->num ; i++ ) {
		if ( !Q_stricmp( f->kv[i].key, key ) ) {
			return f->kv[i].val;
		}
	}
	return "";
}

static int RL_GetInt( rlFile_t *f, const char *key, int def ) {
	const char	*s = RL_Get( f, key );

	return *s ? atoi( s ) : def;
}

static void RL_Set( rlFile_t *f, const char *key, const char *val ) {
	int		i;

	for ( i = 0 ; i < f->num ; i++ ) {
		if ( !Q_stricmp( f->kv[i].key, key ) ) {
			Q_strncpyz( f->kv[i].val, val, sizeof( f->kv[i].val ) );
			return;
		}
	}
	if ( f->num >= RL_MAX_KV ) {
		G_Printf( S_COLOR_YELLOW "descent: too many keys, %s dropped\n", key );
		return;
	}
	Q_strncpyz( f->kv[f->num].key, key, sizeof( f->kv[f->num].key ) );
	Q_strncpyz( f->kv[f->num].val, val, sizeof( f->kv[f->num].val ) );
	f->num++;
}

static void RL_SetInt( rlFile_t *f, const char *key, int val ) {
	RL_Set( f, key, va( "%i", val ) );
}

static qboolean RL_Load( rlFile_t *f, const char *name ) {
	fileHandle_t	fh;
	int				len, k, v;
	char			*p, *line, *end;

	f->num = 0;
	len = trap_FS_FOpenFile( name, &fh, FS_READ );
	if ( !fh ) {
		return qfalse;
	}
	if ( len <= 0 || len >= RL_FILE_MAX ) {
		trap_FS_FCloseFile( fh );
		return qfalse;
	}
	trap_FS_Read( rlFileBuf, len, fh );
	trap_FS_FCloseFile( fh );
	rlFileBuf[len] = 0;

	p = rlFileBuf;
	while ( *p ) {
		line = p;
		while ( *p && *p != '\n' ) {
			p++;
		}
		if ( *p ) {
			*p++ = 0;
		}
		end = line + strlen( line );
		while ( end > line && ( end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t' ) ) {
			*--end = 0;
		}
		while ( *line == ' ' || *line == '\t' ) {
			line++;
		}
		if ( !*line || f->num >= RL_MAX_KV ) {
			continue;
		}
		for ( k = 0 ; line[k] && line[k] != ' ' && line[k] != '\t' ; k++ ) {
		}
		v = k;
		while ( line[v] == ' ' || line[v] == '\t' ) {
			v++;
		}
		if ( k >= RL_KEY_LEN ) {
			k = RL_KEY_LEN - 1;
		}
		memcpy( f->kv[f->num].key, line, k );
		f->kv[f->num].key[k] = 0;
		Q_strncpyz( f->kv[f->num].val, line + v, sizeof( f->kv[f->num].val ) );
		f->num++;
	}
	return qtrue;
}

static void RL_Save( rlFile_t *f, const char *name ) {
	fileHandle_t	fh;
	int				i, len;

	if ( rlPractice ) {
		G_LogPrintf( "Descent: practice, %s not written\n", name );		// en-boss: boss practice saves nothing
		return;
	}
	if ( G_RL_Daily() && Q_stricmp( name, DSD_RUN_FILE ) ) {
		G_LogPrintf( "Descent: daily, %s not written\n", name );		// en-daily: only the daily's own run file
		return;
	}
	rlFileBuf[0] = 0;
	len = 0;
	for ( i = 0 ; i < f->num ; i++ ) {
		if ( f->kv[i].val[0] ) {
			Q_strcat( rlFileBuf, sizeof( rlFileBuf ), va( "%s %s\n", f->kv[i].key, f->kv[i].val ) );
		} else {
			Q_strcat( rlFileBuf, sizeof( rlFileBuf ), va( "%s\n", f->kv[i].key ) );	// a bare key = empty
		}
	}
	len = strlen( rlFileBuf );
	trap_FS_FOpenFile( name, &fh, FS_WRITE );
	if ( !fh ) {
		G_Printf( S_COLOR_RED "descent: can't write %s\n", name );
		return;
	}
	trap_FS_Write( rlFileBuf, len, fh );
	trap_FS_FCloseFile( fh );
}

// "a,b,c" -> ints
static int RL_ParseList( const char *s, int *out, int max ) {
	int		n = 0;

	while ( *s && n < max ) {
		while ( *s == ',' || *s == ' ' || *s == '.' ) {
			s++;
		}
		if ( !*s ) {
			break;
		}
		out[n++] = atoi( s );
		while ( *s && *s != ',' && *s != '.' && *s != ' ' ) {
			s++;
		}
	}
	return n;
}

static const char *RL_ListString( const int *list, int n, char sep ) {
	static char	bufs[4][RL_MAX_CARDS * 4 + 8];
	static int	index;
	char		*buf = bufs[index++ & 3];
	char		item[16];
	int			i;

	buf[0] = 0;
	for ( i = 0 ; i < n ; i++ ) {
		if ( i ) {
			Com_sprintf( item, sizeof( item ), "%c%i", sep, list[i] );
		} else {
			Com_sprintf( item, sizeof( item ), "%i", list[i] );
		}
		Q_strcat( buf, RL_MAX_CARDS * 4 + 8, item );
	}
	return buf;
}

/*
===============================================================================

RANDOM NUMBERS (reproducible from the run seed)

===============================================================================
*/

static int RL_Rand( unsigned *state ) {
	*state = *state * 1103515245u + 12345u;
	return (int)( ( *state >> 16 ) & 0x7fff );
}

static unsigned RL_Seed( int a, int b, int c ) {
	unsigned	s = (unsigned)rl.seed * 2654435761u + (unsigned)a * 7919u + (unsigned)b * 104729u + (unsigned)c * 15485863u + 17u;
	int			i;

	for ( i = 0 ; i < 4 ; i++ ) {
		RL_Rand( &s );
	}
	return s;
}

/*
===============================================================================

CARDS

===============================================================================
*/

static int RL_AltWeapon( int card ) {
	return card - RLC_ALT_FIRST + WP_GAUNTLET;
}

static qboolean RL_HasAnyAlt( const rlKit_t *k ) {
	int		c;

	for ( c = RLC_ALT_GAUNTLET ; c <= RLC_ALT_CHAINGUN ; c++ ) {
		if ( k->count[c] ) {
			return qtrue;
		}
	}
	return qfalse;
}

// the server switch behind the card must be on
static qboolean RL_CardSwitchOn( int c ) {
	switch ( c ) {
	case RLC_DOUBLEJUMP:	return G_RearmOn( RS_DOUBLEJUMP );
	case RLC_WALLJUMP:		return G_RearmOn( RS_WALLJUMP );
	case RLC_WALLRUN:		return G_RearmOn( RS_WALLRUN );
	case RLC_DASH:
	case RLC_DASHCHARGE:
	case RLC_DASHCD:		return G_RearmOn( RS_DASH );
	case RLC_GRAPPLE:		return g_offhandHook.integer != 0;
	case RLC_ZIPLINE:		return G_RearmOn( RS_ZIPLINE );
	case RLC_SLIDE:			return G_RearmOn( RS_SLIDE );
	case RLC_SLAM:			return G_RearmOn( RS_SLAM );
	case RLC_CHARGE:		return G_RearmOn( RS_ALTFIRE ) && G_RearmOn( RS_CHARGE );
	default:
		if ( RLC_IS_ALT( c ) ) {
			return G_RearmOn( RS_ALTFIRE );
		}
		return qtrue;
	}
}

static int RL_StatCap( int c ) {
	switch ( c ) {
	case RLC_SPEED:		return 6;
	case RLC_HEALTH:	return 8;
	case RLC_DAMAGE:	return 8;
	case RLC_ARMOR:		return 6;
	case RLC_DASHCD:	return 4;
	case RLC_AMMO:		return 3;
	default:			return 1;
	}
}

/*
=================
RL_CardAvailable

Can this card be offered to a kit: one-shot cards once, dash charges after the dash,
charged alt after an alt, perks only when their mechanic is unlocked
=================
*/
static qboolean RL_CardAvailable( const rlKit_t *k, int c ) {
	int		w, perk;

	if ( c < 0 || c >= RLC_NUM || !RL_CardSwitchOn( c ) || RLC_IS_LEGEND( c ) ) {
		return qfalse;		// (legendaries: never drawn, G_DS_CardsOffer adds them to the player's offer)
	}
	if ( c >= RLC_PASSIVE_FIRST || bg_rlCardKind[c] == RLK_STAT ) {
		// DESCENT v2: passives (6 slots, level 5), weapon levels, secrets; the old stat cards are retired
		return G_DS_LootCardAvailable( k->role, k->count, c );
	}
	switch ( bg_rlCardKind[c] ) {
	case RLK_ABILITY:
		if ( c == RLC_DASHCHARGE ) {
			return k->count[RLC_DASH] && k->count[c] < 2;
		}
		if ( c == RLC_CHARGE ) {
			return !k->count[c] && RL_HasAnyAlt( k );
		}
		return !k->count[c];
	case RLK_ALT:
		w = RL_AltWeapon( c );
		if ( c == RLC_ALT_GRAPPLE_UNUSED || w <= WP_NONE || w >= WP_NUM_WEAPONS || w == WP_GRAPPLING_HOOK ) {
			return qfalse;
		}
		return !k->count[c] && bg_altFire[w].mode != ALTFIRE_NONE;
	case RLK_STAT:
		if ( c == RLC_DASHCD && !k->count[RLC_DASH] ) {
			return qfalse;
		}
		return k->count[c] < RL_StatCap( c );
	default:
		perk = c - RLC_PERK_FIRST;
		if ( perk < 0 || perk >= PERK_NUM || k->count[c] ) {
			return qfalse;
		}
		switch ( perk ) {
		case PERK_THIRDWIND:
		case PERK_BLINK:
		case PERK_MIRAGE:
		case PERK_PYROMANIAC:
		case PERK_ADRENALINE:
			return k->count[RLC_DASH] != 0;
		case PERK_SLIPSTREAM:
			return k->count[RLC_SLIDE] != 0;
		case PERK_GROUNDZERO:
			return k->count[RLC_SLAM] != 0;
		case PERK_FEATHERWEIGHT:
			return k->count[RLC_DOUBLEJUMP] != 0;
		case PERK_OVERCHARGER:
			return k->count[RLC_CHARGE] != 0;
		case PERK_MAGNETHOOK:
			return k->count[RLC_GRAPPLE] != 0;
		case PERK_CONDUCTOR:
			return G_RearmOn( RS_ELEMENTS );
		case PERK_HEADHUNTER:
			return G_RearmOn( RS_HEADSHOTS );
		case PERK_BUTCHER:
			return g_blood.integer != 0;
		default:
			return qtrue;
		}
	}
}

/*
=================
RL_CardWeight

pickNum: the floor this offer is for (1-based). Abilities come thick early (by floor 4 you
usually hold one or two movement cards, by floor 10 most of them), stats and perks later.
quality: 0 normal, 1 elite, 2 boss (rarer cards come more often)
=================
*/
static int RL_CardWeight( int c, int pickNum, int quality ) {
	static const int rarity[3][3] = {
		{ 100, 55, 25 },	// fight / event
		{ 70, 100, 70 },	// elite
		{ 40, 100, 110 }	// boss
	};
	int		base, r;

	switch ( bg_rlCardKind[c] ) {
	case RLK_ABILITY:
		base = pickNum <= 10 ? 12 : 7;
		break;
	case RLK_ALT:
		base = ( c == RLC_ALT_MACHINEGUN || c == RLC_ALT_GAUNTLET ) ? 6 : 5;
		break;
	case RLK_STAT:
		base = pickNum > 8 ? 6 : 4;
		break;
	case RLK_PASSIVE:
	case RLK_WEAPON:
	case RLK_SECRET:
		base = G_DS_LootCardWeight( c, pickNum );
		break;
	default:
		base = pickNum < 3 ? 2 : 4;
		break;
	}
	r = bg_rlCardRarity[c];
	if ( r < 0 || r > 2 ) {
		r = 0;
	}
	if ( quality < 0 || quality > 2 ) {
		quality = 0;
	}
	// RABBIT'S FOOT (the player's offers): rare and epic cards come more often
	return base * rarity[quality][r] * ( 100 + rlLuckPct * r ) / 100;
}

// one weighted draw; kindFilter -1 = any; minRarity 0 = any; boostKindsNot: x2 for kinds not in this mask
static int RL_Draw( const rlKit_t *k, const int *taken, int numTaken, int pickNum, int quality,
	int kindFilter, int minRarity, int avoidKinds, unsigned *rng ) {
	int		c, i, w, total, r;
	int		weights[RLC_NUM];

	total = 0;
	for ( c = 0 ; c < RLC_NUM ; c++ ) {
		weights[c] = 0;
		if ( !RL_CardAvailable( k, c ) ) {
			continue;
		}
		if ( kindFilter >= 0 && bg_rlCardKind[c] != kindFilter ) {
			continue;
		}
		if ( bg_rlCardRarity[c] < minRarity ) {
			continue;
		}
		for ( i = 0 ; i < numTaken ; i++ ) {
			if ( taken[i] == c ) {
				break;
			}
		}
		if ( i < numTaken ) {
			continue;
		}
		w = RL_CardWeight( c, pickNum, quality );
		if ( avoidKinds && !( avoidKinds & ( 1 << bg_rlCardKind[c] ) ) ) {
			w *= 2;
		}
		weights[c] = w;
		total += w;
	}
	if ( total <= 0 ) {
		return -1;
	}
	r = ( RL_Rand( rng ) * 32768 + RL_Rand( rng ) ) % total;
	for ( c = 0 ; c < RLC_NUM ; c++ ) {
		r -= weights[c];
		if ( weights[c] && r < 0 ) {
			return c;
		}
	}
	return -1;
}

/*
=================
RL_MakeOffer

n different cards: the first slot is an ability while any is left (until floor 14),
the others try other kinds; elite / boss offers hold at least one rare card
=================
*/
static int RL_MakeOffer( const rlKit_t *k, int pickNum, int quality, int n, int *out, unsigned *rng ) {
	int		count = 0, c, kinds = 0, i;

	if ( pickNum <= 14 ) {
		c = RL_Draw( k, out, count, pickNum, quality, RLK_ABILITY, 0, 0, rng );
		if ( c >= 0 ) {
			out[count++] = c;
			kinds |= 1 << bg_rlCardKind[c];
		}
	}
	while ( count < n ) {
		c = RL_Draw( k, out, count, pickNum, quality, -1, 0, kinds, rng );
		if ( c < 0 ) {
			break;
		}
		out[count++] = c;
		kinds |= 1 << bg_rlCardKind[c];
	}
	if ( quality > 0 && count > 1 ) {
		for ( i = 0 ; i < count ; i++ ) {
			if ( bg_rlCardRarity[out[i]] > 0 ) {
				break;
			}
		}
		if ( i == count ) {
			c = RL_Draw( k, out, count, pickNum, quality, -1, 1, 0, rng );
			if ( c >= 0 ) {
				out[count - 1] = c;
			}
		}
	}
	return count;
}

static void RL_KitClear( rlKit_t *k, int role ) {
	memset( k, 0, sizeof( *k ) );
	k->role = role;
}

static void RL_KitAdd( rlKit_t *k, int c ) {
	if ( c < 0 || c >= RLC_NUM || k->num >= RL_MAX_CARDS ) {
		return;
	}
	k->count[c]++;
	k->order[k->num++] = c;
	if ( bg_rlCardKind[c] == RLK_PERK ) {
		k->perks |= 1 << ( c - RLC_PERK_FIRST );
	}
}

// bots: movement first early on, then whatever scores best with a little noise
static int RL_BotChoose( const int *offer, int n, int pickNum, unsigned *rng ) {
	int		i, best = 0, bestScore = -1, s;

	for ( i = 0 ; i < n ; i++ ) {
		switch ( bg_rlCardKind[offer[i]] ) {
		case RLK_ABILITY:	s = pickNum <= 10 ? 30 : 20; break;
		case RLK_ALT:		s = 20; break;
		case RLK_STAT:		s = 12; break;
		default:			s = 18; break;
		}
		s += bg_rlCardRarity[offer[i]] * 4 + RL_Rand( rng ) % 14;
		if ( s > bestScore ) {
			bestScore = s;
			best = i;
		}
	}
	return offer[best];
}

/*
=================
RL_BuildBotKit

Same pool, same weighting, as many cards as the player (elite +2). The boss holds nearly
every ability, some alt-fires and stats.
=================
*/
// DESCENT v2: top the bot's passive levels up to about the player's (chests grow the player too)
static void RL_BotPassives( rlKit_t *k, int role, unsigned *rng ) {
	int		target = G_DS_LootBotPassiveTarget( role ), have = 0, p, c;

	for ( p = 0 ; p < RLP_NUM ; p++ ) {
		have += k->count[RLC_PASSIVE_FIRST + p];
	}
	while ( have < target ) {
		c = RL_Draw( k, NULL, 0, 10, role == RLR_BOSS ? 2 : role == RLR_ELITE ? 1 : 0, RLK_PASSIVE, 0, 0, rng );
		if ( c < 0 ) {
			break;
		}
		RL_KitAdd( k, c );
		have++;
	}
}

static void RL_BuildBotKit( rlKit_t *k, int role, int numCards, unsigned rng ) {
	int		offer[4], n, i, c, drop[2], quality;

	RL_KitClear( k, role );
	if ( role == RLR_BOSS ) {
		drop[0] = drop[1] = -1;
		for ( i = 0 ; i < 2 ; i++ ) {
			c = RL_Rand( &rng ) % ( RLC_CHARGE + 1 );
			if ( c != RLC_DASH && c != RLC_DASHCHARGE ) {
				drop[i] = c;
			}
		}
		for ( c = RLC_DOUBLEJUMP ; c <= RLC_SLAM ; c++ ) {
			if ( c != drop[0] && c != drop[1] && c != RLC_DASHCHARGE && RL_CardAvailable( k, c ) ) {
				RL_KitAdd( k, c );
			}
		}
		if ( RL_CardAvailable( k, RLC_DASHCHARGE ) ) {
			RL_KitAdd( k, RLC_DASHCHARGE );
		}
		for ( i = 0 ; i < 4 + rl.nodeTier ; i++ ) {
			c = RL_Draw( k, NULL, 0, 10, 2, RLK_ALT, 0, 0, &rng );
			RL_KitAdd( k, c );
		}
		if ( RL_CardAvailable( k, RLC_CHARGE ) && drop[0] != RLC_CHARGE && drop[1] != RLC_CHARGE ) {
			RL_KitAdd( k, RLC_CHARGE );
		}
		for ( i = 0 ; i < 2 + rl.nodeTier ; i++ ) {
			c = RL_Draw( k, NULL, 0, 10, 2, RLK_PERK, 0, 0, &rng );
			RL_KitAdd( k, c );
		}
		RL_BotPassives( k, role, &rng );
		return;
	}
	if ( role == RLR_ELITE ) {
		numCards += 2;
	}
	quality = role == RLR_ELITE ? 1 : 0;
	for ( i = 0 ; i < numCards && i < RL_MAX_CARDS ; i++ ) {
		n = RL_MakeOffer( k, i + 1, quality, 3, offer, &rng );
		if ( n <= 0 ) {
			break;
		}
		RL_KitAdd( k, RL_BotChoose( offer, n, i + 1, &rng ) );
	}
	RL_BotPassives( k, role, &rng );
}

/*
===============================================================================

INIT

===============================================================================
*/

static int RL_NodeType( const char *s ) {
	if ( !Q_stricmp( s, "elite" ) ) {
		return RLN_ELITE;
	}
	if ( !Q_stricmp( s, "boss" ) ) {
		return RLN_BOSS;
	}
	if ( !Q_stricmp( s, "event" ) ) {
		return RLN_EVENT;
	}
	if ( !Q_stricmp( s, "vault" ) ) {
		return RLN_VAULT_GAME;		// ACTS: the secret bonus floor
	}
	return RLN_FIGHT;
}

qboolean G_RL_Active( void ) {
	return rl.active;
}

// DESCENT v2 accessors for the g_ds_*.c files: run file keys (written with the run at floor end),
// meta file keys (loaded at init), the floor being played
const char *G_RL_RunGet( const char *key ) {
	return RL_Get( &rlRun, key );
}

void G_RL_RunSet( const char *key, const char *val ) {
	RL_Set( &rlRun, key, val );
}

int G_RL_MetaGetInt( const char *key, int def ) {
	return RL_GetInt( &rlMeta, key, def );
}

const char *G_RL_MetaGet( const char *key ) {		// en-tune
	return RL_Get( &rlMeta, key );
}

int G_RL_Floor( void ) {
	return rl.nodeFloor;
}

/*
=================
G_RL_Init

Called from G_RearmInit (every map load and map_restart). g_rearmMode 5 without an
active run plays as Normal.
=================
*/
void G_RL_Init( void ) {
	int		i;

	memset( &rl, 0, sizeof( rl ) );
	memset( rlKits, 0, sizeof( rlKits ) );
	rlPractice = qfalse;
	G_DS_DailyInit();		// en-daily: ds_daily (rearmed_daily_run.txt, no meta / skills / tally / ghost written)
	rl.human = -1;
	rl.picked = -1;
	rlKitsDirty = 1;
	G_DS_LootReset();		// LOOT: no gems / chests / growth carried over
	G_DS_SecretsInit();		// ds-secrets: this map's stashes (the cvar, the spot scan state)
	G_DS_FrenzyInit();		// FRENZY: no chain / spike / beams carried over
	G_DS_CardsInit();		// CARDS: legendary effect state, the tally

	if ( level.rmMode != RM_MODE_ROGUELIKE ) {
		trap_SetConfigstring( CS_DESCENT, "" );
		trap_SetConfigstring( CS_DESCENTKITS, "" );
		return;
	}
	// en-boss: BOSS PRACTICE (g_dsPractice 1-4, the UI's dsbossprac): the floor comes from
	// rearmed_practice.txt and NOTHING is written back (run, meta, tally, skills, ghost, records)
	rlPractice = trap_Cvar_VariableIntegerValue( "g_dsPractice" ) > 0 && g_gametype.integer == GT_FFA
		&& RL_Load( &rlRun, RL_PRACTICE_FILE ) && RL_GetInt( &rlRun, "practice", 0 ) > 0 && RL_GetInt( &rlRun, "active", 0 ) == 1;
	if ( rlPractice ) {
		G_LogPrintf( "Descent: boss practice %i from %s\n", RL_GetInt( &rlRun, "practice", 0 ), RL_PRACTICE_FILE );
	} else if ( g_gametype.integer != GT_FFA || !RL_Load( &rlRun, RL_RUN_FILE ) || RL_GetInt( &rlRun, "active", 0 ) != 1 ) {
		G_Printf( "Descent: no active run in %s, playing Normal\n", RL_RUN_FILE );
		level.rmMode = RM_MODE_NORMAL;
		trap_SetConfigstring( CS_DESCENT, "" );
		trap_SetConfigstring( CS_DESCENTKITS, "" );
		return;
	}

	rl.active = qtrue;
	rl.seed = RL_GetInt( &rlRun, "seed", 1 );
	rl.difficulty = RL_GetInt( &rlRun, "difficulty", 3 );
	if ( rl.difficulty < 1 ) {
		rl.difficulty = 1;
	} else if ( rl.difficulty > 5 ) {
		rl.difficulty = 5;
	}
	rl.lives = RL_GetInt( &rlRun, "lives", 1 );
	rl.floor = RL_GetInt( &rlRun, "floor", 0 );
	rl.skulls = RL_GetInt( &rlRun, "skulls", 0 );
	rl.kills = RL_GetInt( &rlRun, "kills", 0 );
	rl.time = RL_GetInt( &rlRun, "time", 0 );
	rl.numCards = RL_ParseList( RL_Get( &rlRun, "cards" ), rl.cards, RL_MAX_CARDS );
	rl.nodeType = RL_NodeType( RL_Get( &rlRun, "node_type" ) );
	rl.nodeBots = RL_GetInt( &rlRun, "node_bots", 3 );
	if ( rl.nodeBots < 1 ) {
		rl.nodeBots = 1;
	} else if ( rl.nodeBots > RL_MAX_BOTS ) {
		rl.nodeBots = RL_MAX_BOTS;
	}
	rl.nodeFrags = RL_GetInt( &rlRun, "node_frags", 8 );
	if ( rl.nodeFrags < 1 ) {
		rl.nodeFrags = 8;
	}
	rl.nodeMutator = RL_GetInt( &rlRun, "node_mutator", 0 );
	rl.nodeTier = RL_GetInt( &rlRun, "node_tier", rl.floor >= 10 ? 1 + ( rl.floor - 10 ) / 5 : 0 );
	rl.nodeFloor = RL_GetInt( &rlRun, "node_floor", rl.floor + 1 );
	rl.nodeSkill = *RL_Get( &rlRun, "node_skill" ) ? atof( RL_Get( &rlRun, "node_skill" ) )
		: rl.difficulty + ( rl.floor > 10 ? 0.25f * ( rl.floor - 10 ) : 0 );
	if ( rl.nodeSkill < 1 ) {
		rl.nodeSkill = 1;
	} else if ( rl.nodeSkill > 5 ) {
		rl.nodeSkill = 5;
	}
	rl.upHealth = RL_GetInt( &rlRun, "up_health", 0 );
	rl.upSpeed = RL_GetInt( &rlRun, "up_speed", 0 );
	rl.upDamage = RL_GetInt( &rlRun, "up_damage", 0 );
	rl.upArmor = DS_UP_RETIRED( DSU_ARMOR ) ? 0 : RL_GetInt( &rlRun, "up_armor", 0 );		// (retired: economy pass 2)
	rl.upCards = RL_GetInt( &rlRun, "up_cards", 0 );
	rl.upSkulls = DS_UP_RETIRED( DSU_SKULLS ) ? 0 : RL_GetInt( &rlRun, "up_skulls", 0 );
	rl.rerolls = RL_GetInt( &rlRun, "rerolls", 0 );

	// the level: frag race, a sane time limit
	trap_Cvar_Set( "fraglimit", va( "%i", rl.nodeFrags ) );
	trap_Cvar_Update( &g_fraglimit );
	if ( g_timelimit.integer <= 0 ) {
		trap_Cvar_Set( "timelimit", va( "%i", 3 + rl.nodeFrags / 3 + ( rl.nodeType == RLN_BOSS ? 2 : 0 ) ) );
		trap_Cvar_Update( &g_timelimit );
	}
	G_DS_ActsInit( rl.nodeFloor, rl.nodeType, rl.difficulty, rl.seed );		// ACTS: boss / vault floor, act
	// no random events except the event node's own
	level.nextMutator = level.time + 600000;

	for ( i = 0 ; i < MAX_CLIENTS ; i++ ) {
		rlKits[i].assigned = qfalse;
	}
	G_Printf( "Descent: floor %i, %s, %i bots, %i frags, %i cards, %i lives\n", rl.nodeFloor,
		RL_Get( &rlRun, "node_type" ), rl.nodeBots, rl.nodeFrags, rl.numCards, rl.lives );
	G_LogPrintf( "Descent: floor %i type %i bots %i frags %i cards %s\n", rl.nodeFloor, rl.nodeType,
		rl.nodeBots, rl.nodeFrags, RL_ListString( rl.cards, rl.numCards, ',' ) );
	if ( G_RL_Daily() || !RL_Load( &rlMeta, RL_META_FILE ) ) {		// en-daily: a blank profile; G_RL_MetaGetInt (secrets, ...)
		rlMeta.num = 0;
	}
	G_DS_LootInit();		// LOOT: passives, banked chests, weapon growth (ARSENAL) from the run
	G_RL_UpdateConfigstring();
	G_DS_MetaInit();		// META: character, upgrades, stats
	G_DS_SurpriseInit();	// ds-surprise: ambush / event / ghost plan, the floor modifier, the ghost track
	G_DS_TypesInit();		// en-types: archetypes on this floor (g_dsTypes)
}

/*
=================
G_RL_InitBots

End of G_InitGame. The bots of this floor: varied names, elite / boss first.
=================
*/
static const char *rlBotNames[] = {
	"Sarge", "Anarki", "Grunt", "Major", "Visor", "Slash", "Doom", "Orbb", "Razor", "Keel", "Bitterman",
	"Phobos", "Lucy", "Biker", "Bones", "Cadavre", "Daemia", "Hossman", "Mynx", "Ranger", "TankJr", "Wrack",
	"Angel", "Gorre", "Stripe", "Crash", "Hunter"
};
static const char *rlBossNames[] = { "Xaero", "Klesk", "Uriel", "Sorlag", "Hunter", "Anarki" };
#define RL_NUM_BOT_NAMES	( (int)( sizeof( rlBotNames ) / sizeof( rlBotNames[0] ) ) )
#define RL_NUM_BOSS_NAMES	( (int)( sizeof( rlBossNames ) / sizeof( rlBossNames[0] ) ) )

void G_RL_InitBots( qboolean restart ) {
	int			idx[RL_NUM_BOT_NAMES];
	int			i, j, t, n;
	unsigned	rng;
	rlPlan_t	*p;
	const char	*boss = NULL, *nem;
	int			nbots;

	rlFreshLevel = !restart;
	rlBotsPending = qfalse;
	if ( !rl.active || restart ) {
		if ( !rl.active ) {
			G_DS_IntroPublish();		// ds-intro: no roster outside a run (a restart keeps it)
		}
		return;
	}
	rng = RL_Seed( rl.nodeFloor, 1, 0 );
	for ( i = 0 ; i < RL_NUM_BOT_NAMES ; i++ ) {
		idx[i] = i;
	}
	for ( i = 0 ; i < RL_NUM_BOT_NAMES ; i++ ) {
		j = i + RL_Rand( &rng ) % ( RL_NUM_BOT_NAMES - i );
		t = idx[i]; idx[i] = idx[j]; idx[j] = t;
	}
	G_DS_RankOrderNames( rlBotNames, idx, RL_NUM_BOT_NAMES, rl.nodeFloor );	// en-personality: returning veterans first
	if ( rl.nodeType == RLN_BOSS ) {
		boss = G_DS_BossNetName();		// ACTS: the act's boss (its bot file: G_DS_BossBotName)
	}
	G_DS_ActsStart();		// ACTS: act music
	nem = G_DS_NemesisPlanName( rl.nodeType );		// ACTS: the run's nemesis comes back (fight floors)

	rlNumPlan = 0;
	n = 0;
	// ACTS round 2b: a boss floor is the boss + 2 rival bots (power-matched, normal floor kits)
	nbots = rl.nodeType == RLN_BOSS ? ( G_DS_BossTimed() ? 1 : 3 ) : rl.nodeBots;		// en-boss: timed 1v1 = the boss alone
	for ( i = 0 ; i < nbots && rl.nodeType != RLN_VAULT_GAME ; i++ ) {		// the vault: nobody
		p = &rlPlan[rlNumPlan];
		memset( p, 0, sizeof( *p ) );
		p->skill = rl.nodeSkill;
		p->role = RLR_BOT;
		if ( i == 0 && rl.nodeType == RLN_BOSS ) {
			p->role = RLR_BOSS;
			Q_strncpyz( p->name, boss, sizeof( p->name ) );
			p->skill = rl.nodeSkill + 1.0f;
		} else if ( nem && i == nbots - 1 && !( i == 0 && rl.nodeType == RLN_ELITE ) ) {
			Q_strncpyz( p->name, nem, sizeof( p->name ) );		// ACTS: the nemesis
		} else {
			// skip the boss's and the nemesis's names
			while ( n < RL_NUM_BOT_NAMES && ( ( boss && !Q_stricmp( rlBotNames[idx[n]], boss ) )
				|| ( nem && !Q_stricmp( rlBotNames[idx[n]], nem ) ) ) ) {
				n++;
			}
			if ( n >= RL_NUM_BOT_NAMES ) {
				break;
			}
			Q_strncpyz( p->name, rlBotNames[idx[n++]], sizeof( p->name ) );
			if ( i == 0 && rl.nodeType == RLN_ELITE ) {
				p->role = RLR_ELITE;
				p->skill = rl.nodeSkill + 0.5f;
			}
		}
		if ( p->skill > 5 ) {
			p->skill = 5;
		}
		RL_BuildBotKit( &p->kit, p->role, G_DS_KitCards( rl.numCards ) + G_DS_RankKitBonus( p->name ), RL_Seed( rl.nodeFloor, 2, i ) );	// + en-personality: veteran cards
		rlNumPlan++;
	}
	// added on the first frames: the server reconnects (and we turn away) the last floor's bots
	// after G_InitGame, and an addbot run before that would be taken for one of them
	rlBotsPending = qtrue;
	G_DS_TypesPlan();			// en-types: which planned bot gets which archetype (+ their models to preload)
	// ds-fixes: their models go to the cgame now, so it loads them during the loading screen
	for ( i = 0 ; i < rlNumPlan ; i++ ) {
		if ( G_DS_TypesPlanArch( i ) > 0 && rlPlan[i].role != RLR_BOSS ) {
			continue;		// en-types: it wears its type's model (preloaded by G_DS_TypesPlan), not its own
		}
		G_PreloadAddBot( rlPlan[i].role == RLR_BOSS ? G_DS_BossBotName() : rlPlan[i].name );
	}
	rlNumExtra = 0;
	G_DS_SurprisePreload();		// ds-surprise: the models of the floor's planned ambusher / ghost / fodder
	G_DS_AffixPreload();		// en-affix: the elite's affixes, the champion pack's models
	G_PreloadPublish();
	G_DS_IntroPublish();		// ds-intro: the opponents for the loading screen / versus intro
}

/*
=================
G_RL_AddExtraBot

ds-surprise: a bot joins mid-floor (an ambusher, the ghost of a past run, uprising fodder) with
a kit planned here: the given cards and passive levels, or a regular kit of numCards (cards NULL).
A name used before gets the new kit (fodder comes back under the same names).
=================
*/
void G_RL_AddExtraBot( const char *botFile, const char *netName, float skill, int role, const int *cards, int numCards, const int *passives ) {
	rlPlan_t	*p = NULL;
	int			i, j;

	if ( !rl.active || !botFile || !netName ) {
		return;
	}
	for ( i = 0 ; i < rlNumExtra ; i++ ) {
		if ( !Q_stricmp( rlExtra[i].name, netName ) ) {
			p = &rlExtra[i];
			break;
		}
	}
	if ( !p ) {
		if ( rlNumExtra >= RL_MAX_EXTRA ) {
			// full: the oldest planned one makes room
			memmove( &rlExtra[0], &rlExtra[1], sizeof( rlExtra[0] ) * ( RL_MAX_EXTRA - 1 ) );
			rlNumExtra = RL_MAX_EXTRA - 1;
		}
		p = &rlExtra[rlNumExtra++];
	}
	memset( p, 0, sizeof( *p ) );
	Q_strncpyz( p->name, netName, sizeof( p->name ) );
	p->role = role;
	p->skill = skill < 1 ? 1 : skill > 5 ? 5 : skill;
	if ( cards ) {
		RL_KitClear( &p->kit, role );
		for ( i = 0 ; i < numCards ; i++ ) {
			if ( cards[i] >= 0 && cards[i] < RLC_PASSIVE_FIRST ) {
				RL_KitAdd( &p->kit, cards[i] );
			}
		}
		for ( i = 0 ; passives && i < RLP_NUM ; i++ ) {
			for ( j = 0 ; j < passives[i] && j < DS_MAX_PLEVEL ; j++ ) {
				RL_KitAdd( &p->kit, RLC_PASSIVE_FIRST + i );
			}
		}
	} else {
		RL_BuildBotKit( &p->kit, role, numCards, RL_Seed( rl.nodeFloor, 9, rlNumExtra + level.time ) );
	}
	trap_SendConsoleCommand( EXEC_INSERT, va( "addbot %s %.2f free 0 \"%s\"\n", botFile, p->skill, netName ) );
	G_LogPrintf( "Descent: extra bot %s (%s) role %i skill %.2f, %i cards\n", netName, botFile, role, p->skill, p->kit.num );
}

// ds-intro (g_ds_intro.c): this floor's planned bots. Name, kit role, cards of one kind (-1 all)
int G_RL_PlanNum( void ) {
	return rl.active ? rlNumPlan : 0;
}

const char *G_RL_PlanName( int i, int *role ) {
	if ( i < 0 || i >= rlNumPlan ) {
		return NULL;
	}
	*role = rlPlan[i].role;
	return rlPlan[i].name;
}

int G_RL_PlanCards( int i, int kind ) {
	int		c, n = 0;

	if ( i < 0 || i >= rlNumPlan ) {
		return 0;
	}
	for ( c = 0 ; c < RLC_NUM ; c++ ) {
		if ( rlPlan[i].kit.count[c] > 0 && ( kind < 0 || bg_rlCardKind[c] == kind ) ) {
			n++;
		}
	}
	return n;
}

/*
=================
G_RL_DenyBot

A new floor: the bots carried over from the last map leave, this floor adds its own
=================
*/
qboolean G_RL_DenyBot( qboolean firstTime ) {
	return rl.active && rlFreshLevel && !firstTime;
}

/*
===============================================================================

KITS

===============================================================================
*/

static int RL_HumanClient( void ) {
	int		i;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( level.clients[i].pers.connected == CON_CONNECTED && !( g_entities[i].r.svFlags & SVF_BOT )
			&& level.clients[i].sess.sessionTeam != TEAM_SPECTATOR ) {
			return i;
		}
	}
	return G_DS_SimClient();		// ACTS g_dsBossSim: a bot plays the human's part (-1 normally)
}

// the human player (or the g_dsBossSim bot standing in for them)
qboolean G_RL_IsPlayer( gentity_t *ent ) {
	return ent && ent->client && ( !( ent->r.svFlags & SVF_BOT ) || ( ent - g_entities ) == G_DS_SimClient() );
}

static rlKit_t *RL_KitFor( gentity_t *ent ) {
	rlKit_t		*k;
	char		name[MAX_NETNAME];
	int			i, num;

	if ( !ent || !ent->client ) {
		return NULL;
	}
	num = ent - g_entities;
	if ( num < 0 || num >= MAX_CLIENTS ) {
		return NULL;
	}
	k = &rlKits[num];
	if ( k->assigned ) {
		return k;
	}
	if ( G_RL_IsPlayer( ent ) ) {
		RL_KitClear( k, RLR_PLAYER );
		for ( i = 0 ; i < rl.numCards ; i++ ) {
			RL_KitAdd( k, rl.cards[i] );
		}
	} else {
		Q_strncpyz( name, ent->client->pers.netname, sizeof( name ) );
		Q_CleanStr( name );
		for ( i = 0 ; i < rlNumPlan ; i++ ) {
			if ( !Q_stricmp( name, rlPlan[i].name ) ) {
				break;
			}
		}
		if ( i >= rlNumPlan ) {
			// ds-surprise: a bot that joined mid-floor with a planned kit (the latest by that name)
			for ( i = rlNumExtra - 1 ; i >= 0 ; i-- ) {
				if ( !Q_stricmp( name, rlExtra[i].name ) ) {
					break;
				}
			}
			if ( i >= 0 ) {
				*k = rlExtra[i].kit;
				k->assigned = qtrue;
				rlKitsDirty = 1;
				return k;
			}
			i = rlNumPlan;
		}
		if ( i < rlNumPlan ) {
			*k = rlPlan[i].kit;
		} else {
			// a bot nobody planned (addbot by hand, map_restart): a regular kit
			RL_BuildBotKit( k, RLR_BOT, G_DS_KitCards( rl.numCards ), RL_Seed( rl.nodeFloor, 3, num ) );
		}
	}
	k->assigned = qtrue;
	rlKitsDirty = 1;
	return k;
}

/*
=================
G_RL_GateClient

Every ClientThink, after the global switch bits went in: keep only what this client owns
=================
*/
void G_RL_GateClient( gentity_t *ent ) {
	gclient_t		*cl = ent->client;
	playerState_t	*ps = &cl->ps;
	rlKit_t			*k;
	int				f, f2, mask, c, w, msec, extra;
	float			speed;

	if ( !rl.active ) {
		return;
	}
	k = RL_KitFor( ent );
	if ( !k ) {
		return;
	}

	f = ps->stats[STAT_RMFLAGS];
	if ( !k->count[RLC_DOUBLEJUMP] ) {
		f &= ~RMF_DOUBLEJUMP;
	}
	if ( !k->count[RLC_WALLJUMP] ) {
		f &= ~RMF_WALLJUMP;
	}
	if ( !k->count[RLC_WALLRUN] ) {
		f &= ~RMF_WALLRUN;
	}
	if ( !k->count[RLC_DASH] ) {
		f &= ~RMF_DASH;
	}
	if ( !k->count[RLC_CHARGE] ) {
		f &= ~RMF_CHARGE;
	}
	if ( !RL_HasAnyAlt( k ) ) {
		f &= ~RMF_ALTFIRE;
	}
	// two DASH CHARGE cards = the third charge (the Third Wind perk gives it too)
	if ( k->count[RLC_DASHCHARGE] >= 2 ) {
		f |= RMP_THIRDWIND;
	}
	ps->stats[STAT_RMFLAGS] = f;

	f2 = ps->ammo[AMMO_RMFLAGS2];
	if ( !k->count[RLC_SLIDE] ) {
		f2 &= ~RMF2_SLIDE;
	}
	if ( !k->count[RLC_SLAM] ) {
		f2 &= ~RMF2_SLAM;
	}
	if ( !k->count[RLC_ZIPLINE] ) {
		f2 &= ~RMF2_ZIPLINE;
	}
	if ( k->count[RLC_DASH] && !k->count[RLC_DASHCHARGE] ) {
		f2 |= RMF2_DASH1;
	} else {
		f2 &= ~RMF2_DASH1;
	}
	ps->ammo[AMMO_RMFLAGS2] = f2;

	// alt-fire per weapon
	mask = 0;
	for ( c = RLC_ALT_GAUNTLET ; c <= RLC_ALT_CHAINGUN ; c++ ) {
		w = RL_AltWeapon( c );
		if ( k->count[c] && w > WP_NONE && w < 14 ) {
			mask |= 1 << w;
		}
	}
	ps->loopSound = ( ps->loopSound & RMZ_STATE ) | ( mask & RM_ALTMASK_ALL );

	// stats
	speed = 1.0f + 0.06f * k->count[RLC_SPEED] + G_DS_LootSpeedBonus( ent );		// + QUICKSILVER
	if ( k->role == RLR_PLAYER ) {
		speed += 0.02f * rl.upSpeed + G_DS_CharSpeed();
	}
	ps->speed *= speed * G_DS_SpeedScale( ent ) * G_DS_SurpriseSpeedScale( ent ) * G_DS_AffixSpeedScale( ent ) * G_DS_TypesSpeedScale( ent );		// ACTS: boss frenzy; ds-surprise: mini-boss, FERAL; en-affix: FROST

	if ( k->count[RLC_DASHCD] && ps->stats[STAT_DASH_TIME] > 0 ) {
		msec = cl->pers.cmd.serverTime - ps->commandTime;
		if ( msec > 0 && msec < 250 ) {
			extra = msec * 12 * k->count[RLC_DASHCD] / 100;
			ps->stats[STAT_DASH_TIME] -= extra;
			if ( ps->stats[STAT_DASH_TIME] < 0 ) {
				ps->stats[STAT_DASH_TIME] = 0;
			}
		}
	}
	G_DS_SkillGate( ent );		// SKILLS: wall run speed, dash recharge, wall run time, regen
	G_DS_FrenzyGate( ent );		// FRENZY: OVERDRIVE speed, dash, zip refresh
}

qboolean G_RL_HookAllowed( gclient_t *cl ) {
	rlKit_t		*k;

	if ( !rl.active ) {
		return qtrue;
	}
	k = RL_KitFor( &g_entities[cl - level.clients] );
	return k && k->count[RLC_GRAPPLE];
}

// how many of a card a client's kit holds (0 outside a run)
int G_RL_CardCount( gentity_t *ent, int card ) {
	rlKit_t		*k;

	if ( !rl.active || card < 0 || card >= RLC_NUM || !( k = RL_KitFor( ent ) ) ) {
		return 0;
	}
	return k->count[card];
}

qboolean G_RL_HasCard( gentity_t *ent, int card ) {
	return G_RL_CardCount( ent, card ) > 0;
}

int G_RL_HumanClient( void ) {
	return rl.active ? RL_HumanClient() : -1;
}

// arsenal: the kit role of a client (RLR_ value: 1 player, 2 bot, 3 elite, 4 boss; 0 outside a run)
int G_RL_ClientRole( gentity_t *ent ) {
	rlKit_t		*k;

	if ( !rl.active || !( k = RL_KitFor( ent ) ) ) {
		return 0;
	}
	return k->role;
}

float G_RL_DamageScale( gentity_t *targ, gentity_t *attacker ) {
	rlKit_t		*k;
	float		s = 1.0f, ls;

	if ( !rl.active ) {
		return 1.0f;
	}
	ls = G_DS_LootDamageScale( targ, attacker );		// LOOT: SPITE dealt, IRON HIDE / BLOOD CROWN taken
	if ( !attacker || !attacker->client || attacker == targ ) {
		return ls;
	}
	k = RL_KitFor( attacker );
	if ( !k ) {
		return ls;
	}
	s += 0.08f * k->count[RLC_DAMAGE];
	if ( k->role == RLR_PLAYER ) {
		s += 0.03f * rl.upDamage;
	} else {
		s += 0.1f * rl.nodeTier;
		if ( k->role == RLR_ELITE ) {
			s += 0.1f;
		} else if ( k->role == RLR_BOSS ) {
			s += 0.15f;
		}
	}
	return s * ls * G_DS_ActsDamageScale( targ, attacker ) * G_DS_SurpriseDamageScale( targ, attacker )
		* G_DS_AffixDamageScale( targ, attacker ) * G_DS_TypesDamageScale( targ, attacker );		// ACTS: the boss's damage side, the nemesis; ds-surprise; en-affix: affix budget, copies, champions
}

int G_RL_AmmoPickup( gentity_t *ent, int quantity ) {
	rlKit_t		*k;

	if ( !rl.active || !( k = RL_KitFor( ent ) ) ) {
		return quantity;
	}
	return G_DS_LootAmmoPickup( ent, quantity + quantity * k->count[RLC_AMMO] / 2 );		// + AMMO BELT
}

int G_RL_MaxHealthBonus( gentity_t *ent ) {
	rlKit_t		*k;
	int			b;

	if ( !rl.active || !( k = RL_KitFor( ent ) ) ) {
		return 0;
	}
	b = 15 * k->count[RLC_HEALTH] + G_DS_LootHealthBonus( ent );		// + HOLLOW HEART
	switch ( k->role ) {
	case RLR_PLAYER:
		b += 5 * rl.upHealth + G_DS_CharHealth() + G_DS_SkillMaxHealth( ent );	// + SKILLS: TOUGHNESS
		break;
	case RLR_ELITE:
		b += 60 + 10 * rl.nodeTier;
		break;
	case RLR_BOSS:
		b += 160 + 25 * rl.nodeTier;
		break;
	default:
		b += 10 * rl.nodeTier;
		break;
	}
	b += G_DS_SurpriseHealthBonus( ent, b );		// ds-surprise: GLASS, the mini-boss's pool, weak fodder
	b += G_DS_TypesHealthBonus( ent, b );		// en-types: the archetype's health budget
	b += G_DS_AffixHealthBonus( ent, b );		// en-affix: the affix budget, half-health split copies, champions
	b += G_DS_RankHealthBonus( ent );		// en-personality: veteran rank, nemesis level
	b += G_DS_RmHealthBonus( ent, b );		// en-rematch: GLASS halves the player's limit
	return b;
}

/*
=================
G_RL_SpawnLoadout

From G_RearmSpawnLoadout, before the health limit is applied
=================
*/
void G_RL_SpawnLoadout( gentity_t *ent ) {
	gclient_t	*cl = ent->client;
	rlKit_t		*k;
	int			armor;

	if ( !rl.active || !( k = RL_KitFor( ent ) ) ) {
		return;
	}
	if ( cl->pers.perks != k->perks ) {
		cl->pers.perks = k->perks;
		if ( !( ent->r.svFlags & SVF_BOT ) ) {
			trap_SendServerCommand( ent - g_entities, va( "rmperks %i", k->perks ) );
		}
	}
	cl->pers.perkOffer[0] = -1;
	armor = 25 * k->count[RLC_ARMOR];
	if ( k->role == RLR_PLAYER ) {
		armor += 10 * rl.upArmor;
	} else if ( k->role == RLR_ELITE ) {
		armor += 50;
	} else if ( k->role == RLR_BOSS ) {
		armor += 100 + 25 * rl.nodeTier;
	}
	if ( armor > cl->ps.stats[STAT_ARMOR] ) {
		cl->ps.stats[STAT_ARMOR] = armor;
	}
	cl->ps.generic1 &= ~( RM_BODY_ELITE | RM_BODY_BOSS );
	if ( k->role == RLR_ELITE ) {
		cl->ps.generic1 |= RM_BODY_ELITE;
	} else if ( k->role == RLR_BOSS ) {
		cl->ps.generic1 |= RM_BODY_BOSS;
	}
	// the health limit changed: refill to it (G_PerkApplyMaxHealth only refills on a change)
	cl->ps.stats[STAT_MAX_HEALTH] = -1;
	G_DS_MetaSpawn( ent );		// META: the character's weapon
	G_DS_SkillSpawn( ent );		// SKILLS: ARMORER
	if ( k->role == RLR_BOSS ) {
		G_DS_BossSpawn( ent );		// ACTS: the boss's weapons, its client number
	}
	G_DS_ActsPlayerSpawn( ent );		// ACTS: boss floors, the player's respawn spot, guns and grace
	G_DS_SurpriseSpawn( ent );			// ds-surprise: an ambusher / ghost / fodder lands on its warned spot; ARMOURED
	G_DS_ArriveSpawn( ent );			// ds-arrivals: a planned bot's first spawn: its arrival spot, a late joiner's head start
	G_DS_AffixSpawn( ent );			// en-affix: elite / mini-boss affixes, split copies and champions land on their spots
	G_DS_RankSpawn( ent );				// en-personality: veteran / nemesis armour + affixes, a returning veteran's line
	G_DS_TypesSpawn( ent );			// en-types: the archetype's kit (weapons, armour, the mimic's copy)
}

void G_RL_PlayerDie( gentity_t *self, gentity_t *attacker ) {
	rlKit_t		*k;

	G_DS_AffixDeath( self, attacker );		// en-affix: explosive corpses, splits, affix rewards, one-life extras
	if ( rl.active && rl.phase == RLP_PLAY ) {
		G_DS_ActsPlayerDie( self, attacker );		// ACTS: boss beaten / player deaths on a boss floor
		G_DS_NemesisDeath( self, attacker );		// ACTS: the nemesis (kills of the player, revenge)
		G_BM_Death( self, attacker );				// en-memory: the lifetime record (rearmed_bots.txt at the floor end)
		G_DS_SurpriseDeath( self, attacker );		// ds-surprise: mini-boss / ghost / bounty rewards, one-life extras
	}
	G_DS_TypesDeath( self, attacker );		// en-types: a kamikaze killed first pops, rituals / beams stop
	if ( !rl.active || rl.phase != RLP_PLAY || level.warmupTime || !attacker || !attacker->client
		|| attacker == self || ( attacker->r.svFlags & SVF_BOT ) ) {
		return;
	}
	rl.levelKills++;
	k = RL_KitFor( self );
	if ( k && ( k->role == RLR_ELITE || k->role == RLR_BOSS ) ) {
		rl.bossKills++;
	}
}

void G_RL_ClientDisconnect( gentity_t *ent ) {
	int		n = ent - g_entities;

	if ( n >= 0 && n < MAX_CLIENTS ) {
		rlKits[n].assigned = qfalse;
		rlKitsDirty = 1;
		G_DS_TypesDisconnect( n );		// en-types
	}
}

/*
===============================================================================

CONFIGSTRINGS

===============================================================================
*/

static void G_RL_UpdateConfigstring( void ) {
	static char	s[2048];

	if ( !rl.active ) {
		return;
	}
	Com_sprintf( s, sizeof( s ), "\\on\\1\\fl\\%i\\cl\\%i\\lv\\%i\\sk\\%i\\dif\\%i\\nt\\%i\\fr\\%i\\mut\\%i\\tier\\%i"
		"\\rr\\%i\\ph\\%i\\res\\%i\\of\\%s\\rw\\%i\\rb\\%i.%i.%i.%i.%i\\pk\\%i\\lk\\%i\\ov\\%i\\bk\\%i\\nb\\%i.%i\\kl\\%i\\hc\\%i",
		rl.nodeFloor, rl.floor, rl.lives, rl.skulls, rl.difficulty, rl.nodeType, rl.nodeFrags, rl.nodeMutator, rl.nodeTier,
		rl.rerolls, rl.phase, rl.result, RL_ListString( rl.offer, rl.numOffer, '.' ), rl.reward,
		rl.rewardParts[0], rl.rewardParts[1], rl.rewardParts[2], rl.rewardParts[3], rl.rewardParts[4],
		rl.picked, rl.levelKills, rl.runOver, rl.banked, rl.newBestFloor, rl.newBestKills, rl.kills, rl.human );
	Q_strcat( s, sizeof( s ), va( "\\cards\\%s", RL_ListString( rl.cards, rl.numCards, '.' ) ) );
	G_DS_EconConfigstring( s, sizeof( s ) );		// ECONOMY: reward multiplier, boss damage pay
	G_DSM_Configstring( s, sizeof( s ) );		// ds-maps: \mv\ the floor variant (cgame lighting)
	if ( G_RL_Daily() ) {
		Q_strcat( s, sizeof( s ), "\\dly\\1" );		// en-daily: the cgame's floor-end words (no skulls, no records)
	}
	trap_SetConfigstring( CS_DESCENT, s );
}

static void G_RL_UpdateKitsConfigstring( void ) {
	static char	s[4096];
	int			i;

	s[0] = 0;
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( !rlKits[i].assigned || level.clients[i].pers.connected == CON_DISCONNECTED ) {
			continue;
		}
		Q_strcat( s, sizeof( s ), va( "%i:%i:%s ", i, rlKits[i].role, RL_ListString( rlKits[i].order, rlKits[i].num, '.' ) ) );
	}
	trap_SetConfigstring( CS_DESCENTKITS, s );
}

/*
===============================================================================

LEVEL END

===============================================================================
*/

static int RL_StyleRank( int style ) {
	static const int th[8] = { 0, STYLE_RANK_D, STYLE_RANK_C, STYLE_RANK_B, STYLE_RANK_A, STYLE_RANK_S, STYLE_RANK_SS, STYLE_RANK_SSS };
	int		r;

	for ( r = 7 ; r > 0 ; r-- ) {
		if ( style >= th[r] ) {
			return r;
		}
	}
	return 0;
}

static void RL_WriteRunCommon( void ) {
	RL_SetInt( &rlRun, "floor", rl.floor );
	RL_SetInt( &rlRun, "lives", rl.lives );
	RL_SetInt( &rlRun, "skulls", rl.skulls );
	RL_SetInt( &rlRun, "kills", rl.kills );
	RL_SetInt( &rlRun, "time", rl.time );
	RL_SetInt( &rlRun, "rerolls", rl.rerolls );
	RL_Set( &rlRun, "cards", RL_ListString( rl.cards, rl.numCards, ',' ) );
	RL_Set( &rlRun, "offer", RL_ListString( rl.offer, rl.numOffer, ',' ) );
	RL_Set( &rlRun, "result", rl.result == 1 ? "win" : rl.result == 2 ? "loss" : "none" );
	G_DS_LootSaveRun();		// LOOT: pv, chests, gems, wused + ARSENAL wl / wx / evo
}

static void RL_MakePlayerOffer( void ) {
	rlKit_t		k;
	int			i, j, quality, n, prev, attempt, cnt, same, bestSame;
	int			old[RL_MAX_OFFER], deal[RL_MAX_OFFER];
	unsigned	rng;

	RL_KitClear( &k, RLR_PLAYER );
	for ( i = 0 ; i < rl.numCards ; i++ ) {
		RL_KitAdd( &k, rl.cards[i] );
	}
	G_DS_LootFillKit( k.count );		// passive levels (chests add levels without cards)
	quality = rl.nodeType == RLN_BOSS ? 2 : rl.nodeType == RLN_ELITE ? 1 : 0;
	n = rl.upCards >= 1 ? 4 : 3;
	if ( G_DS_UpRank( DSU_FIRSTPICK ) > 0 && rl.nodeFloor % DS_FLOORS_PER_ACT == 1 ) {
		n = 5;		// FIRSTPICK: the first offer of every act
	}
	n += G_DS_SurpriseOfferBonus();		// ds-surprise: FERAL +1 card
	if ( n > RL_MAX_OFFER ) {
		n = RL_MAX_OFFER;
	}
	rlLuckPct = G_DS_LuckPct();
	// a reroll keeps the deal that repeats the fewest cards of the last one
	for ( prev = 0 ; prev < rl.numOffer && prev < RL_MAX_OFFER ; prev++ ) {
		old[prev] = rl.offer[prev];
	}
	bestSame = 99;
	for ( attempt = 0 ; attempt < ( rl.rerollsUsed ? 8 : 1 ) ; attempt++ ) {
		rng = RL_Seed( rl.nodeFloor, 4 + attempt * 31, rl.rerollsUsed );
		cnt = RL_MakeOffer( &k, rl.nodeFloor, quality, n, deal, &rng );
		same = 0;
		for ( i = 0 ; i < cnt ; i++ ) {
			for ( j = 0 ; j < prev ; j++ ) {
				if ( deal[i] == old[j] ) {
					same++;
				}
			}
		}
		if ( same < bestSame ) {
			bestSame = same;
			rl.numOffer = cnt;
			for ( i = 0 ; i < cnt ; i++ ) {
				rl.offer[i] = deal[i];
			}
		}
	}
	G_DS_CardsOffer( rl.offer, rl.numOffer, rl.nodeType, rl.nodeFloor, rlLuckPct );	// CARDS: maybe one legendary
	rlLuckPct = 0;
}

/*
=================
RL_EndRun

Out of lives: bank the skulls, records, run over
=================
*/
static void RL_EndRun( void ) {
	int		best, bestKills;
	char	key[32];

	rl.runOver = qtrue;
	if ( G_RL_Daily() || !RL_Load( &rlMeta, RL_META_FILE ) ) {		// en-daily: never the player's meta
		rlMeta.num = 0;
		RL_SetInt( &rlMeta, "version", 1 );
	}
	rl.banked = rl.skulls;
	RL_SetInt( &rlMeta, "skulls", RL_GetInt( &rlMeta, "skulls", 0 ) + rl.banked );
	RL_SetInt( &rlMeta, "runs", RL_GetInt( &rlMeta, "runs", 0 ) + 1 );
	Com_sprintf( key, sizeof( key ), "best_floor_%i", G_DS_RecordDifficulty() );	// ECONOMY: the lowest difficulty played
	best = RL_GetInt( &rlMeta, key, 0 );
	rl.newBestFloor = rl.floor > best;
	if ( rl.newBestFloor ) {
		RL_SetInt( &rlMeta, key, rl.floor );
	}
	bestKills = RL_GetInt( &rlMeta, "best_kills", 0 );
	rl.newBestKills = rl.kills > bestKills;
	if ( rl.newBestKills ) {
		RL_SetInt( &rlMeta, "best_kills", rl.kills );
	}
	RL_SetInt( &rlMeta, "total_kills", RL_GetInt( &rlMeta, "total_kills", 0 ) + rl.kills );
	RL_SetInt( &rlMeta, "total_floors", RL_GetInt( &rlMeta, "total_floors", 0 ) + rl.floor );
	RL_Save( &rlMeta, RL_META_FILE );
	if ( G_RL_Daily() ) {
		rl.banked = 0;		// en-daily: nothing banked, no records (RL_Save wrote no meta)
		rl.newBestFloor = rl.newBestKills = qfalse;
	}

	RL_SetInt( &rlRun, "active", 0 );
	RL_SetInt( &rlRun, "banked", rl.banked );
	RL_SetInt( &rlRun, "new_best_floor", rl.newBestFloor );
	RL_SetInt( &rlRun, "new_best_kills", rl.newBestKills );
	G_LogPrintf( "Descent: run over at floor %i, %i kills, %i skulls banked\n", rl.floor, rl.kills, rl.banked );
}

static void RL_Finish( qboolean won ) {
	int		pct, total, styleRank;

	rl.phase = RLP_END;
	rl.phaseTime = level.time;
	rl.endTime = level.time;
	rl.result = won ? 1 : 2;
	rl.kills += rl.levelKills;
	rl.time += level.time - level.startTime;
	G_DS_FrenzyFloorEnd();		// FRENZY: cash the chain out (weapon xp before the run is saved), end a spike
	G_DS_TypesFloorEnd();			// en-types: the evidence counters to the log
	G_DS_SurpriseFloorEnd( won );		// ds-surprise: BLOODLUST's chest is banked before the chests open

	if ( won ) {
		rl.floor++;
		styleRank = RL_StyleRank( rl.bestStyle );
		// ECONOMY (g_ds_econ.c): floor + kills + style, the node map's promise (elite x2, event x1.5,
		// boss x3), the boss bar and kill, + GRAVE ROBBER / GRAVE COIN %, x the difficulty multiplier
		pct = 10 * rl.upSkulls + G_DS_LootSkullPct();
		total = G_DS_FloorSkulls( qtrue, rl.levelKills, styleRank, rl.nodeType, rl.nodeFloor, pct, rl.rewardParts );
		rl.reward = total;
		rl.skulls += total;
		G_DS_LootFloorEnd( qtrue );		// LOOT: open the banked chests (rewards change the offer)
		RL_MakePlayerOffer();
		if ( rl.nodeType == RLN_VAULT_GAME ) {
			rl.numOffer = 0;		// ACTS: the vault is its own reward (gems and the chest), no cards
		}
	} else {
		// ECONOMY: a lost floor still pays its kills and, on a boss floor, the boss damage dealt
		pct = 10 * rl.upSkulls + G_DS_LootSkullPct();
		rl.reward = G_DS_FloorSkulls( qfalse, rl.levelKills, 0, rl.nodeType, rl.nodeFloor, pct, rl.rewardParts );
		rl.skulls += rl.reward;
		if ( !G_DS_MetaRevive( rl.lives ) ) {	// META: REVIVAL
			rl.lives--;
		}
		rl.numOffer = 0;
		if ( rl.lives <= 0 ) {
			rl.lives = 0;
		}
	}
	if ( G_RL_Daily() ) {
		rl.reward = 0;		// en-daily: the daily pays no skulls (the score is its reward)
		rl.skulls = 0;
		memset( rl.rewardParts, 0, sizeof( rl.rewardParts ) );
	}
	RL_WriteRunCommon();
	// ACTS: the act of the next floor, bosses beaten, vaults, the victory after floor 24, the Abyss depth
	RL_SetInt( &rlRun, "act", DS_ActForFloor( rl.floor + 1 ) );
	if ( won && rl.nodeType == RLN_BOSS && G_DS_BossType() >= 0 ) {
		RL_SetInt( &rlRun, "bosses", RL_GetInt( &rlRun, "bosses", 0 ) | ( 1 << G_DS_BossType() ) );
		if ( rl.nodeFloor == DS_WIN_FLOOR ) {
			RL_SetInt( &rlRun, "won", 1 );
			RL_SetInt( &rlRun, "victory", 1 );		// the UI's victory screen: into the Abyss or bank
			G_LogPrintf( "Descent: VICTORY - the act %i boss is dead\n", DS_NUM_ACTS );
		}
	}
	if ( rl.nodeType == RLN_VAULT_GAME ) {
		RL_SetInt( &rlRun, "vault", RL_GetInt( &rlRun, "vault", 0 ) + 1 );
	}
	if ( rl.floor > DS_WIN_FLOOR ) {
		RL_SetInt( &rlRun, "abyss", rl.floor - DS_WIN_FLOOR );
	}
	G_DS_MetaFloorEnd( won );		// META: floor stats, damage breakdown
	G_DS_CardsFloorEnd( won, rl.levelKills, won ? RL_StyleRank( rl.bestStyle ) : 0, rl.reward );	// CARDS: the floor-end tally
	G_DS_RankFloorEnd( won );		// en-personality: veteran xp and ranks (vet_roster)
	// en-daily: the floor's record for the daily score; its last floor won ends the run (no cards)
	if ( G_DS_DailyFloorEnd( won, won ? RL_StyleRank( rl.bestStyle ) : 0, rl.levelKills, level.time - level.startTime,
		rl.nodeFloor, rl.nodeType == RLN_BOSS ) ) {
		rl.numOffer = 0;
		RL_Set( &rlRun, "offer", "" );
		RL_EndRun();
	} else if ( !won && rl.lives <= 0 ) {
		RL_EndRun();
	}
	G_BM_FloorEnd( won, rl.runOver );		// en-memory: bot records + nemesis hall (rearmed_bots.txt; never daily / practice)
	RL_Save( &rlRun, RL_RUN_FILE );
	G_LogPrintf( "Descent: floor %i %s, kills %i, reward %i, offer %s\n", rl.nodeFloor, won ? "WON" : "LOST",
		rl.levelKills, rl.reward, RL_ListString( rl.offer, rl.numOffer, ',' ) );
	G_RL_UpdateConfigstring();
	trap_SendServerCommand( -1, va( "rlend %i", rl.result ) );
	LogExit( won ? "Descent: floor cleared." : "Descent: floor failed." );
}

/*
=================
G_RL_CheckExitRules

Replaces the frag / time limit rules and the intermission exit while a run is on.
=================
*/
qboolean G_RL_CheckExitRules( void ) {
	gclient_t	*cl;
	int			i, best, h;

	if ( !rl.active ) {
		return qfalse;
	}
	if ( level.intermissiontime ) {
		// never the normal exit: the next floor comes from the node map
		switch ( rl.phase ) {
		case RLP_END:
			if ( rlPractice ) {
				// en-boss: boss practice: no tally, chests or cards, straight back to the practice menu
				rl.phase = RLP_DONE;
				rl.phaseTime = level.time + 1500 - RL_MAP_DELAY;
				G_RL_UpdateConfigstring();
			} else if ( rl.result == 1 && !G_DS_CardsTallyReady() ) {
				// CARDS: the floor-end tally counts up first (the client says dsctallydone, or a timeout)
			} else if ( rl.result == 1 && level.time >= level.intermissiontime + RL_OFFER_DELAY && G_DS_LootChestsOpened() > 0 ) {
				rl.phase = RLP_CHEST;		// LOOT: the jackpot screen first
				rl.phaseTime = level.time;
				G_RL_UpdateConfigstring();
			} else if ( rl.result == 1 && level.time >= level.intermissiontime + RL_OFFER_DELAY ) {
				rl.phase = RLP_OFFER;
				rl.phaseTime = level.time;
				G_RL_UpdateConfigstring();
				if ( rl.numOffer <= 0 ) {		// nothing left to offer
					rl.phase = RLP_DONE;
					G_RL_UpdateConfigstring();
				}
			} else if ( rl.result == 2 ) {
				rl.phase = RLP_DONE;
				rl.phaseTime = rl.endTime + RL_LOSS_DELAY - RL_MAP_DELAY;
				G_RL_UpdateConfigstring();
			}
			break;
		case RLP_CHEST:
			// the client says it's done (or skipped), or it took too long
			if ( G_DS_LootChestDone() || level.time >= rl.phaseTime + G_DS_LootChestMsec() ) {
				rl.phase = rl.numOffer > 0 ? RLP_OFFER : RLP_DONE;
				rl.phaseTime = level.time;
				G_RL_UpdateConfigstring();
			}
			break;
		case RLP_DONE:
			if ( level.time >= rl.phaseTime + RL_MAP_DELAY ) {
				rl.phase = RLP_SENT;
				G_RL_UpdateConfigstring();
				// ACTS: the act 4 boss is dead: the victory screen (META) instead of the map
				i = !rl.runOver && RL_GetInt( &rlRun, "victory", 0 ) == 1;
				if ( rlPractice ) {		// en-boss: the UI's practice menu shows the result
					if ( !G_DS_RematchEnd( rl.result ) ) {		// (en-rematch: a rematch reports with dsrmend)
						trap_SendConsoleCommand( EXEC_INSERT, va( "dspracend %i %i %i\n", rl.result, G_DS_BossTimeLeft(),
							G_DS_BossTimed() ? (int)( G_DS_BossProgress() * 100.0f + 0.5f ) : 0 ) );
					}
					G_LogPrintf( "Descent: practice over (result %i)\n", rl.result );
					break;
				}
				trap_SendConsoleCommand( EXEC_INSERT, rl.runOver ? "rlover\n" : i ? "rlvictory\n" : "rlmap\n" );
				G_LogPrintf( "Descent: %s\n", rl.runOver ? "rlover" : i ? "rlvictory" : "rlmap" );
			}
			break;
		default:
			break;
		}
		return qtrue;
	}
	if ( level.intermissionQueued ) {
		return qfalse;		// the normal code moves everyone to the intermission
	}
	if ( rl.phase != RLP_PLAY || level.warmupTime ) {
		return qtrue;
	}

	h = RL_HumanClient();
	if ( h < 0 ) {
		return qtrue;		// still loading in
	}
	rl.human = h;
	rl.humanScore = level.clients[h].ps.persistant[PERS_SCORE];
	// ACTS: boss floors (boss dead / the player out of g_dsBossLives lives) and the vault (its timer)
	if ( G_DS_IsBossFloor() || G_DS_IsVaultFloor() ) {
		i = G_DS_FloorResult();
		if ( i ) {
			RL_Finish( i == 1 );
		}
		return qtrue;
	}
	best = -9999;
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		cl = &level.clients[i];
		if ( i == h || cl->pers.connected != CON_CONNECTED || cl->sess.sessionTeam == TEAM_SPECTATOR || G_DS_SurpriseExtra( i ) ) {
			continue;
		}
		if ( cl->ps.persistant[PERS_SCORE] > best ) {
			best = cl->ps.persistant[PERS_SCORE];
		}
	}
	if ( rl.humanScore >= rl.nodeFrags ) {
		RL_Finish( qtrue );
		return qtrue;
	}
	if ( best >= rl.nodeFrags ) {
		RL_Finish( qfalse );
		return qtrue;
	}
	if ( g_timelimit.integer > 0 && level.time - level.startTime >= g_timelimit.integer * 60000 ) {
		if ( rl.humanScore > best ) {
			RL_Finish( qtrue );
		} else if ( rl.humanScore < best ) {
			RL_Finish( qfalse );
		}
		// a tie plays on: sudden death
	}
	return qtrue;
}

// ds-arrivals: addbot for planned bot i (at the start, or when its arrival comes)
void G_RL_AddPlanBot( int i ) {
	if ( i < 0 || i >= rlNumPlan ) {
		return;
	}
	if ( rlPlan[i].role == RLR_BOSS ) {		// ACTS: the boss's own model, its boss name
		trap_SendConsoleCommand( EXEC_INSERT, va( "addbot %s %.2f free 0 \"%s\"\n", G_DS_BossBotName(), rlPlan[i].skill, rlPlan[i].name ) );
		return;
	}
	trap_SendConsoleCommand( EXEC_INSERT, va( "addbot %s %.2f free 0\n", rlPlan[i].name, rlPlan[i].skill ) );
}

/*
=================
G_RL_RunFrame
=================
*/
void G_RL_RunFrame( void ) {
	int		h, i;

	if ( !rl.active ) {
		return;
	}
	// past the 3 settle frames of SV_SpawnServer, and once the bot library has the map's AAS
	// (a bot added before that fails "AAS not initialized"; after 8 s add them anyway)
	if ( rlBotsPending && level.time - level.startTime >= 800
		&& ( trap_AAS_Initialized() || level.time - level.startTime >= 8000 ) ) {
		rlBotsPending = qfalse;
		for ( i = 0 ; i < rlNumPlan ; i++ ) {
			if ( G_DS_ArriveAtStart( i ) ) {		// ds-arrivals: g_dsStagger 1 = only the first one(s) now
				G_RL_AddPlanBot( i );
			}
		}
		G_DS_ArriveBegin();
	}
	G_DS_ArriveFrame();		// ds-arrivals: the rest join one at a time
	// random events only on event nodes, and those last the whole floor
	if ( rl.nodeType == RLN_EVENT && rl.nodeMutator > MUT_NONE && rl.nodeMutator < MUT_NUM_MUTATORS ) {
		if ( !rl.mutatorStarted && level.time - level.startTime > 2500 && !level.intermissiontime ) {
			rl.mutatorStarted = 1;
			// ds-fixes: a floor-long event (no timer; the HUD says THIS FLOOR). It used to be a
			// timed one whose end was pushed back each frame, so the HUD's 35 s timer ran out
			// while the event went on.
			G_RearmStartFloorMutator( rl.nodeMutator );
		}
	}
	if ( !level.mutator ) {
		level.nextMutator = level.time + 600000;
	}

	h = RL_HumanClient();
	if ( h >= 0 && rl.phase == RLP_PLAY && level.clients[h].ps.stats[STAT_STYLE] > rl.bestStyle ) {
		rl.bestStyle = level.clients[h].ps.stats[STAT_STYLE];
	}
	if ( rlKitsDirty ) {
		rlKitsDirty = 0;
		G_RL_UpdateKitsConfigstring();
	}
	G_DS_LootFrame();		// LOOT: regeneration, growth configstring
	G_DS_ActsFrame();		// ACTS: boss attacks and phases, the vault
	G_DS_MetaFrame();		// META: time held per weapon
	G_DS_SurpriseFrame();	// ds-surprise: ambushes, wild events, the ghost
	G_DS_FrenzyFrame();		// FRENZY: chain timer, power spikes
	G_DS_CardsFrame();		// CARDS: legendary effects (echo, orbs, graves, the agent), HUD state
	G_DS_TypesFrame();		// en-types: medic beams, kamikaze fuses, summoner rituals
}

/*
===============================================================================

COMMANDS

===============================================================================
*/

static void RL_Pick( gentity_t *ent, int choice ) {
	int		card;

	if ( rl.phase != RLP_OFFER || choice < 1 || choice > rl.numOffer ) {
		return;
	}
	card = rl.offer[choice - 1];
	if ( rl.numCards < RL_MAX_CARDS ) {
		rl.cards[rl.numCards++] = card;
	}
	rl.picked = card;
	rl.numOffer = 0;
	rl.phase = RLP_DONE;
	rl.phaseTime = level.time;
	G_DS_LootPickCard( ent, card );		// LOOT: passive / weapon level cards
	G_DS_CardsPicked( card );		// CARDS: a legendary taken, the owed one paid
	RL_WriteRunCommon();
	RL_Save( &rlRun, RL_RUN_FILE );
	G_LogPrintf( "Descent: picked %i %s\n", card, bg_rlCardNames[card] );
	trap_SendServerCommand( ent - g_entities, va( "rlgot %i", card ) );
	G_RL_UpdateConfigstring();
}

static void RL_Reroll( void ) {
	if ( rl.phase != RLP_OFFER || rl.rerolls <= 0 ) {
		return;
	}
	rl.rerolls--;
	rl.rerollsUsed++;
	RL_MakePlayerOffer();
	RL_WriteRunCommon();
	RL_Save( &rlRun, RL_RUN_FILE );
	G_RL_UpdateConfigstring();
}

qboolean G_RL_ClientCommand( gentity_t *ent, const char *cmd ) {
	char	arg[16];

	if ( !Q_stricmp( cmd, "rlpick" ) ) {
		if ( rl.active && !( ent->r.svFlags & SVF_BOT ) ) {
			trap_Argv( 1, arg, sizeof( arg ) );
			RL_Pick( ent, atoi( arg ) );
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "rlreroll" ) ) {
		if ( rl.active && !( ent->r.svFlags & SVF_BOT ) ) {
			RL_Reroll();
		}
		return qtrue;
	}
	if ( G_DS_CardsClientCommand( ent, cmd ) ) {		// CARDS: dsctallydone
		return qtrue;
	}
	if ( G_DS_LootClientCommand( ent, cmd ) ) {		// dschestdone
		return qtrue;
	}
	return qfalse;
}

int G_RL_AddSkulls( int n ) {
	if ( rl.active ) {
		rl.skulls += n;
		G_DS_EconOtherSkulls( n );		// ECONOMY: the run summary's breakdown
	}
	return rl.skulls;
}

/*
=================
G_RL_ConsoleCommand

Dev: rlstate, rlwin, rllose, rlgive <card> (this floor only), rlkits
=================
*/
qboolean G_RL_ConsoleCommand( const char *cmd ) {
	char	arg[16];
	int		i, c, h;

	if ( G_DS_SkillsCommand( cmd ) ) {		// SKILLS dev: dsskills / dsskillxp / dsperk / dsskillsave
		return qtrue;
	}
	if ( G_DS_MetaCommand( cmd ) ) {		// META dev: dsstat / dsmeta
		return qtrue;
	}
	if ( G_DS_EconCommand( cmd ) ) {		// ECONOMY dev: dsecon [sim]
		return qtrue;
	}
	if ( G_DS_ActsCommand( cmd ) ) {		// ACTS dev: dsattack / dsphase / dsbossdmg / dskillboss / dsbossinfo / dsvaultend
		return qtrue;
	}
	if ( G_DS_CardsCommand( cmd ) ) {		// CARDS dev: dsoffer legendary / dstally / dslegend / dslgstate
		return qtrue;
	}
	if ( G_DS_FrenzyCommand( cmd ) ) {		// FRENZY dev: dsfrenzy / dscrit / dsspike / dsbeam / dsfzstate
		return qtrue;
	}
	if ( G_DS_TypesCommand( cmd ) ) {		// en-types dev: dstypes / dstype / dstypeact / dstypestats
		return qtrue;
	}
	if ( G_DS_SurpriseCommand( cmd ) ) {		// ds-surprise dev: dsambush / dsevent / dsghost / dsmod / dssurprise
		return qtrue;
	}
	if ( G_DS_AffixCommand( cmd ) ) {		// en-affix dev: dsaffix / dschampions / dsaffixstate
		return qtrue;
	}
	if ( G_DS_LootConsoleCommand( cmd ) ) {		// LOOT dev: dsgems / dsscatter / dschest / dsvacuum / dspassive / dsloot
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "rlstate" ) ) {
		G_Printf( "descent: active %i floor %i (cleared %i) lives %i skulls %i phase %i result %i cards %s offer %s\n",
			rl.active, rl.nodeFloor, rl.floor, rl.lives, rl.skulls, rl.phase, rl.result,
			RL_ListString( rl.cards, rl.numCards, ',' ), RL_ListString( rl.offer, rl.numOffer, ',' ) );
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			if ( level.clients[i].pers.connected == CON_CONNECTED ) {
				G_LogPrintf( "RLState: client %i rmflags 0x%x flags2 0x%x altmask 0x%x speed %i maxhp %i armor %i perks 0x%x generic1 %i\n",
					i, level.clients[i].ps.stats[STAT_RMFLAGS] & ( RMF_CONFIG | RMP_ALL ),
					level.clients[i].ps.ammo[AMMO_RMFLAGS2] & ( RMF2_CONFIG | RMP2_ALL ), level.clients[i].ps.loopSound & RM_ALTMASK_ALL,
					level.clients[i].ps.speed, level.clients[i].ps.stats[STAT_MAX_HEALTH], level.clients[i].ps.stats[STAT_ARMOR],
					level.clients[i].pers.perks, level.clients[i].ps.generic1 );
			}
		}
		G_LogPrintf( "RLState: floor %i cleared %i lives %i skulls %i phase %i result %i cards %s offer %s\n",
			rl.nodeFloor, rl.floor, rl.lives, rl.skulls, rl.phase, rl.result,
			RL_ListString( rl.cards, rl.numCards, ',' ), RL_ListString( rl.offer, rl.numOffer, ',' ) );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "rlshow" ) ) {		// line the bots up in front of the player (screenshots)
		vec3_t	fwd, right, pos, ang;
		int		n = 0, j = 0;

		h = RL_HumanClient();
		if ( h < 0 ) {
			return qtrue;
		}
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			if ( i != h && level.clients[i].pers.connected == CON_CONNECTED && g_entities[i].health > 0 ) {
				n++;
			}
		}
		VectorSet( ang, 0, level.clients[h].ps.viewangles[YAW], 0 );
		AngleVectors( ang, fwd, right, NULL );
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			if ( i == h || level.clients[i].pers.connected != CON_CONNECTED || g_entities[i].health <= 0 ) {
				continue;
			}
			VectorMA( level.clients[h].ps.origin, 150, fwd, pos );
			VectorMA( pos, ( j - ( n - 1 ) * 0.5f ) * 64, right, pos );
			VectorCopy( pos, level.clients[i].ps.origin );
			VectorClear( level.clients[i].ps.velocity );
			level.clients[i].ps.eFlags ^= EF_TELEPORT_BIT;
			VectorSet( ang, 0, level.clients[h].ps.viewangles[YAW] + 180, 0 );
			SetClientViewAngle( &g_entities[i], ang );
			BG_PlayerStateToEntityState( &level.clients[i].ps, &g_entities[i].s, qtrue );
			VectorCopy( pos, g_entities[i].r.currentOrigin );
			trap_LinkEntity( &g_entities[i] );
			j++;
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "rlkits" ) ) {
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			if ( rlKits[i].assigned ) {
				G_Printf( "%2i role %i %s: %s\n", i, rlKits[i].role, level.clients[i].pers.netname,
					RL_ListString( rlKits[i].order, rlKits[i].num, ',' ) );
				G_LogPrintf( "Kit: %i role %i %s: %s\n", i, rlKits[i].role, level.clients[i].pers.netname,
					RL_ListString( rlKits[i].order, rlKits[i].num, ',' ) );
			}
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "rlwin" ) || !Q_stricmp( cmd, "rllose" ) ) {
		if ( rl.active && rl.phase == RLP_PLAY && !level.intermissionQueued && !level.intermissiontime ) {
			RL_Finish( !Q_stricmp( cmd, "rlwin" ) );
			// dev: rlwin <card> <card> <card> deals that offer (card screen tests)
			if ( rl.numOffer && trap_Argc() > 1 ) {
				for ( i = 0 ; i < RL_MAX_OFFER && i + 1 < trap_Argc() ; i++ ) {
					trap_Argv( i + 1, arg, sizeof( arg ) );
					c = atoi( arg );
					rl.offer[i] = ( c >= 0 && c < RLC_NUM ) ? c : 0;
				}
				rl.numOffer = i;
				G_RL_UpdateConfigstring();
			}
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "rlgive" ) ) {
		trap_Argv( 1, arg, sizeof( arg ) );
		c = atoi( arg );
		h = RL_HumanClient();
		if ( rl.active && h >= 0 && c >= 0 && c < RLC_NUM ) {
			RL_KitAdd( RL_KitFor( &g_entities[h] ), c );
			rlKitsDirty = 1;
		}
		return qtrue;
	}
	return qfalse;
}
