/*
===========================================================================
REARMED -- DESCENT map variety, UI side (ds-maps).

The player: "never the same level twice, and better variety, I keep playing the beginning
ones over and over". So:

- NO REPEATS IN A RUN. Every map launched this run goes into the run key ds_played (table ids
  or names, oldest first). The maps of the floor-map window (the floors you can see) are
  stored in ds_fm the first time they are shown, so they never change under you; a node
  whose map got played meanwhile (a loss retried, another path) is re-rolled. A new node
  avoids: maps played this run, its siblings, and the maps of every node above or below it
  on a path through it (so no path through the visible floors can meet a map twice).
  Choice order: the act's pool, then any installed map (a borrowed one keeps the act's
  look: grade, haze, particles, music); only when every map was played this run (deep in the
  Abyss) a node REVISITS the least recently played map (tag "REVISITED").
- VARIETY ACROSS RUNS. Lifetime play counts and the last run per map in rearmed_meta.txt
  (dsm_plays / dsm_last, csv in ds_mapInfo order; the first time they are seeded from the
  DESCENT floors in games.log). A map's weight grows the less it was played and the longer
  ago (stronger on an act's first floors), and follows the floor's size (bots + you vs the
  map's size class). Never played in DESCENT = "NEW".
- VARIANTS. Every node rolls a lighting / layout variant (bg_ds_maps.c), shown as a tag
  ("q3dm7  NIGHT  ARMS SWAP"); the game applies it from node_variant (g_ds_maps.c, cgame
  cg_ds_maps.c). g_dsVariants 0 turns them off.

Dev: dsmaps (pools, stats, the window), dsmapsim [runs] [floors] [loss%] [real] (the
simulation: repeats, re-rolls, the act-1 spread; writes rearmed_mapsim.txt).
===========================================================================
*/
#include "ui_local.h"
#include "ui_ds_meta.h"
#include "ui_ds_maps.h"

#define DSM_MAX_PLAYED		64		// the run's played list (oldest dropped: they come back first)
#define DSM_MAX_INFO		64		// >= ds_numMapInfo
#define DSM_SPAN			( RL_VIS_LAYERS + 2 )	// stored layers: the visible window
#define DSM_RECENT			12		// a REVISITED node stays valid while its map is not this recent

typedef struct {
	int			cur;						// the floor's layer to pick in
	int			numPlayed;
	char		played[DSM_MAX_PLAYED][32];
	int			playedIdx[DSM_MAX_PLAYED];	// index into the pool (-1: not installed)
	int			resumeL, resumeI;			// the floor in progress (quit mid-floor): keeps its map
	char		resumeMap[32];
	int			fmFirst;					// stored layers fmFirst .. fmFirst + DSM_SPAN - 1
	int			fmN[DSM_SPAN];				// 0: nothing stored for that layer
	char		fm[DSM_SPAN][RL_MAX_NODES][32];
	int			fmFlag[DSM_SPAN][RL_MAX_NODES];
	int			runNo;						// this run's number (meta runs + 1)
	int			plays[DSM_MAX_INFO];		// lifetime, ds_mapInfo order
	int			last[DSM_MAX_INFO];			// the run that last played it (0: never)
	int			devL, devI, devMap;			// devMap: dev forced map index + 1 (0: none)
	int			pathPrev;					// the node played on the floor above (-1: the first floor)
	qboolean	loaded;
} dsmState_t;

static char		(*dsm_maps)[32];
static int		dsm_numMaps;
static int		(*dsm_actMaps)[RL_MAX_MAPS];
static int		*dsm_actNum;

static dsmState_t	dsm_st;				// the real run
static dsmState_t	dsm_sim;			// the simulation
static qboolean		dsm_simOn;

static vmCvar_t		dsm_variants;		// g_dsVariants (shared with the game)

/*
=======================================================================

SETUP

=======================================================================
*/

void UI_DSM_RegisterCvars( void ) {
	trap_Cvar_Register( &dsm_variants, "g_dsVariants", "1", CVAR_ARCHIVE );
}

qboolean UI_DSM_HasAAS( const char *name ) {
	fileHandle_t	f;

	f = 0;
	trap_FS_FOpenFile( va( "maps/%s.aas", name ), &f, FS_READ );
	if ( !f ) {
		return qfalse;
	}
	trap_FS_FCloseFile( f );
	return qtrue;
}

const char *UI_DSM_LongName( const char *name ) {
	int		id = BG_DSM_MapId( name );

	return id >= 0 ? ds_mapInfo[id].longname : NULL;
}

void UI_DSM_SetPool( char maps[][32], int numMaps, int actMaps[][RL_MAX_MAPS], int *actNum ) {
	dsm_maps = maps;
	dsm_numMaps = numMaps;
	dsm_actMaps = actMaps;
	dsm_actNum = actNum;
}

static unsigned int DSM_Hash( unsigned int a, unsigned int b, unsigned int c ) {
	unsigned int	h;

	h = a * 0x9E3779B1u + b * 0x85EBCA77u + c * 0xC2B2AE3Du + 0x165667B1u;
	h ^= h >> 15;
	h *= 0x2C1B3C6Du;
	h ^= h >> 12;
	h *= 0x297A2D39u;
	h ^= h >> 15;
	return h;
}

static int DSM_Find( const char *name ) {
	int		i;

	for ( i = 0 ; i < dsm_numMaps ; i++ ) {
		if ( !Q_stricmp( dsm_maps[i], name ) ) {
			return i;
		}
	}
	return -1;
}

// run-file token: the table id for a known map, else the name
static const char *DSM_Token( const char *name ) {
	int		id = BG_DSM_MapId( name );

	return id >= 0 ? va( "%i", id ) : name;
}

static void DSM_Untoken( const char *tok, char *out, int size ) {
	int		id;

	if ( tok[0] >= '0' && tok[0] <= '9' ) {
		id = atoi( tok );
		Q_strncpyz( out, id >= 0 && id < ds_numMapInfo ? ds_mapInfo[id].name : "", size );
		return;
	}
	Q_strncpyz( out, tok, size );
}

/*
=======================================================================

LIFETIME STATS (rearmed_meta.txt)

=======================================================================
*/

// the first time: the DESCENT floors the player already played, from the game log
static void DSM_SeedFromLog( int *plays, const char *logName ) {
	static char		buf[8192 + 1024];
	fileHandle_t	f;
	char			name[64];
	char			map[32];
	char			*line, *p, *e, *q;
	int				len, carry, n, id, k, found;

	trap_Cvar_VariableStringBuffer( "g_log", name, sizeof( name ) );
	if ( logName ) {
		Q_strncpyz( name, logName, sizeof( name ) );		// dsmapsim log:<file>
	}
	if ( !name[0] ) {
		Q_strncpyz( name, "games.log", sizeof( name ) );
	}
	f = 0;
	len = trap_FS_FOpenFile( name, &f, FS_READ );
	if ( !f ) {
		return;
	}
	carry = 0;
	found = 0;
	while ( len > 0 ) {
		n = len > 8192 ? 8192 : len;
		trap_FS_Read( buf + carry, n, f );
		len -= n;
		n += carry;
		buf[n] = 0;
		line = buf;
		while ( 1 ) {
			e = strchr( line, '\n' );
			if ( !e && len > 0 ) {
				break;		// a partial line: carried into the next chunk
			}
			if ( e ) {
				*e = 0;
			}
			if ( strstr( line, "InitGame:" ) && strstr( line, "\\g_rearmMode\\5\\" ) ) {
				p = strstr( line, "\\mapname\\" );
				if ( p ) {
					p += 9;
					for ( k = 0, q = p ; *q && *q != '\\' && k < (int)sizeof( map ) - 1 ; q++ ) {
						map[k++] = *q;
					}
					map[k] = 0;
					id = BG_DSM_MapId( map );
					if ( id >= 0 && id < DSM_MAX_INFO ) {
						plays[id]++;
						found++;
					}
				}
			}
			if ( !e ) {
				line += strlen( line );
				break;
			}
			line = e + 1;
		}
		carry = buf + n - line;
		if ( carry > 1024 ) {
			carry = 0;		// an absurd line: drop it
		}
		if ( carry > 0 ) {
			memmove( buf, line, carry );
		}
	}
	trap_FS_FCloseFile( f );
	Com_Printf( "DESCENT maps: %i floors played before found in %s\n", found, name );
}

static void DSM_LoadStats( dsmState_t *st ) {
	int		i, n;
	int		tmp[DSM_MAX_INFO];

	memset( st->plays, 0, sizeof( st->plays ) );
	memset( st->last, 0, sizeof( st->last ) );
	RL_LoadMeta();
	if ( !RL_KVGet( &rl_meta, "dsm_v" )[0] ) {
		// first run with ds-maps: seed the counts once
		DSM_SeedFromLog( st->plays, NULL );
		RL_KVSetInt( &rl_meta, "dsm_v", 1 );
		RL_KVSet( &rl_meta, "dsm_plays", "" );
		for ( i = 0 ; i < ds_numMapInfo && i < DSM_MAX_INFO ; i++ ) {
			RL_KVSet( &rl_meta, "dsm_plays", va( "%s%s%i", RL_KVGet( &rl_meta, "dsm_plays" ), i ? "," : "", st->plays[i] ) );
		}
		RL_SaveMeta();
	}
	n = RL_ParseInts( RL_KVGet( &rl_meta, "dsm_plays" ), tmp, DSM_MAX_INFO );
	for ( i = 0 ; i < n ; i++ ) {
		st->plays[i] = tmp[i] > 0 ? tmp[i] : 0;
	}
	n = RL_ParseInts( RL_KVGet( &rl_meta, "dsm_last" ), tmp, DSM_MAX_INFO );
	for ( i = 0 ; i < n ; i++ ) {
		st->last[i] = tmp[i] > 0 ? tmp[i] : 0;
	}
	st->runNo = RL_KVInt( &rl_meta, "runs" ) + 1;
}

static void DSM_SaveStats( dsmState_t *st ) {
	char	a[RL_VAL_LEN], b[RL_VAL_LEN];
	int		i, la, lb;

	a[0] = b[0] = 0;
	la = lb = 0;
	for ( i = 0 ; i < ds_numMapInfo && i < DSM_MAX_INFO ; i++ ) {
		Com_sprintf( a + la, sizeof( a ) - la, i ? ",%i" : "%i", st->plays[i] );
		la += strlen( a + la );
		Com_sprintf( b + lb, sizeof( b ) - lb, i ? ",%i" : "%i", st->last[i] );
		lb += strlen( b + lb );
	}
	RL_KVSet( &rl_meta, "dsm_plays", a );
	RL_KVSet( &rl_meta, "dsm_last", b );
}

/*
=======================================================================

RUN STATE (rearmed_run.txt: ds_played, ds_fm)

=======================================================================
*/

static void DSM_AddPlayed( dsmState_t *st, const char *name ) {
	int		i;

	if ( st->numPlayed >= DSM_MAX_PLAYED ) {
		for ( i = 1 ; i < DSM_MAX_PLAYED ; i++ ) {
			Q_strncpyz( st->played[i - 1], st->played[i], sizeof( st->played[0] ) );
			st->playedIdx[i - 1] = st->playedIdx[i];
		}
		st->numPlayed = DSM_MAX_PLAYED - 1;
	}
	Q_strncpyz( st->played[st->numPlayed], name, sizeof( st->played[0] ) );
	st->playedIdx[st->numPlayed] = DSM_Find( name );
	st->numPlayed++;
}

static void DSM_ParsePlayed( dsmState_t *st, const char *s ) {
	char	tok[32], name[32];
	int		k;

	st->numPlayed = 0;
	while ( *s ) {
		while ( *s == ',' || *s == ' ' ) {
			s++;
		}
		for ( k = 0 ; *s && *s != ',' && k < (int)sizeof( tok ) - 1 ; s++ ) {
			tok[k++] = *s;
		}
		tok[k] = 0;
		if ( !tok[0] ) {
			break;
		}
		DSM_Untoken( tok, name, sizeof( name ) );
		if ( name[0] ) {
			DSM_AddPlayed( st, name );
		}
	}
}

static const char *DSM_PlayedString( dsmState_t *st ) {
	static char	out[RL_VAL_LEN];
	int			i, len;

	out[0] = 0;
	len = 0;
	for ( i = 0 ; i < st->numPlayed && len < RL_VAL_LEN - 40 ; i++ ) {
		Com_sprintf( out + len, sizeof( out ) - len, i ? ",%s" : "%s", DSM_Token( st->played[i] ) );
		len += strlen( out + len );
	}
	return out;
}

// "L:t.t.t;L:t.t" (t = table id or name, "*" first = REVISITED)
static void DSM_ParseFM( dsmState_t *st, const char *s ) {
	char	tok[40];
	int		L, n, k, slot;

	memset( st->fmN, 0, sizeof( st->fmN ) );
	st->fmFirst = st->cur - 2 < 0 ? 0 : st->cur - 2;
	while ( *s ) {
		while ( *s == ';' || *s == ' ' ) {
			s++;
		}
		if ( !*s ) {
			break;
		}
		L = atoi( s );
		while ( *s && *s != ':' && *s != ';' ) {
			s++;
		}
		if ( *s != ':' ) {
			continue;
		}
		s++;
		slot = L - st->fmFirst;
		n = 0;
		while ( *s && *s != ';' ) {
			for ( k = 0 ; *s && *s != '.' && *s != ';' && k < (int)sizeof( tok ) - 1 ; s++ ) {
				tok[k++] = *s;
			}
			tok[k] = 0;
			if ( *s == '.' ) {
				s++;
			}
			if ( slot >= 0 && slot < DSM_SPAN && n < RL_MAX_NODES ) {
				st->fmFlag[slot][n] = tok[0] == '*' ? DSMAP_F_REVISIT : 0;
				DSM_Untoken( tok[0] == '*' ? tok + 1 : tok, st->fm[slot][n], sizeof( st->fm[0][0] ) );
				n++;
			}
		}
		if ( slot >= 0 && slot < DSM_SPAN ) {
			st->fmN[slot] = n;
		}
	}
}

static const char *DSM_FMString( dsmState_t *st ) {
	static char	out[RL_VAL_LEN];
	int			slot, i, len;

	out[0] = 0;
	len = 0;
	for ( slot = 0 ; slot < DSM_SPAN ; slot++ ) {
		if ( !st->fmN[slot] ) {
			continue;
		}
		Com_sprintf( out + len, sizeof( out ) - len, "%s%i:", len ? ";" : "", st->fmFirst + slot );
		len += strlen( out + len );
		for ( i = 0 ; i < st->fmN[slot] ; i++ ) {
			Com_sprintf( out + len, sizeof( out ) - len, "%s%s%s", i ? "." : "", ( st->fmFlag[slot][i] & DSMAP_F_REVISIT ) ? "*" : "",
				DSM_Token( st->fm[slot][i] ) );
			len += strlen( out + len );
		}
	}
	return out;
}

static int DSM_CurLayer( int floor ) {
	if ( floor < 0 ) {
		floor = 0;
	}
	if ( floor > RL_MAX_LAYERS - RL_VIS_LAYERS - 2 ) {
		floor = RL_MAX_LAYERS - RL_VIS_LAYERS - 2;		// as RL_ComputeReach
	}
	return floor;
}

// the real run's state from rl_run (+ the lifetime stats from the meta file)
static qboolean DSM_LoadRun( dsmState_t *st ) {
	int		devL, devI, devMap;

	if ( !RL_KVInt( &rl_run, "active" ) ) {
		return qfalse;
	}
	devL = st->devL;
	devI = st->devI;
	devMap = st->devMap;
	memset( st, 0, sizeof( *st ) );
	st->devL = devL;
	st->devI = devI;
	st->devMap = devMap;
	st->cur = DSM_CurLayer( RL_KVInt( &rl_run, "floor" ) );
	DSM_ParsePlayed( st, RL_KVGet( &rl_run, "ds_played" ) );
	if ( !st->numPlayed && RL_KVGet( &rl_run, "node_map" )[0] ) {
		DSM_AddPlayed( st, RL_KVGet( &rl_run, "node_map" ) );		// a run from before ds-maps
	}
	st->pathPrev = -1;
	if ( st->cur > 0 ) {
		int		path[RL_MAX_LAYERS];

		if ( RL_ParseInts( RL_KVGet( &rl_run, "map" ), path, RL_MAX_LAYERS ) >= st->cur ) {
			st->pathPrev = path[st->cur - 1];
		}
	}
	st->resumeL = st->resumeI = -1;
	if ( RL_KVInt( &rl_run, "infloor" ) && RL_KVGet( &rl_run, "node_layer" )[0] ) {
		st->resumeL = RL_KVInt( &rl_run, "node_layer" );
		st->resumeI = RL_KVInt( &rl_run, "node_index" );
		Q_strncpyz( st->resumeMap, RL_KVGet( &rl_run, "node_map" ), sizeof( st->resumeMap ) );
	}
	DSM_ParseFM( st, RL_KVGet( &rl_run, "ds_fm" ) );
	DSM_LoadStats( st );
	st->loaded = qtrue;
	return qtrue;
}

/*
=======================================================================

ASSIGNMENT

=======================================================================
*/

// position in the played list (newest = numPlayed - 1), -1 = not played this run
static int DSM_PlayedPos( dsmState_t *st, int idx ) {
	int		i;

	for ( i = st->numPlayed - 1 ; i >= 0 ; i-- ) {
		if ( st->playedIdx[i] == idx ) {
			return i;
		}
	}
	return -1;
}

static int DSM_FighterClass( int fighters ) {
	return fighters <= 3 ? 0 : fighters == 4 ? 1 : fighters == 5 ? 2 : 3;
}

static const float dsm_fit[4][3] = {
	// small, medium, large
	{ 1.0f,  0.7f, 0.15f },		// 3 fighters (you + 2 bots)
	{ 0.6f,  1.0f, 0.4f },		// 4
	{ 0.3f,  1.0f, 0.8f },		// 5
	{ 0.12f, 0.8f, 1.0f }		// 6+
};

static float DSM_Weight( dsmState_t *st, int idx, int L, int fighters, qboolean boss, float avgPlays ) {
	float	fit, f;
	int		id, size, since;

	id = BG_DSM_MapId( dsm_maps[idx] );
	size = ( id >= 0 && ds_mapInfo[id].size >= 0 ) ? ds_mapInfo[id].size : DSMAP_MEDIUM;
	fit = dsm_fit[DSM_FighterClass( fighters )][size];
	if ( boss ) {
		// 2026-10-21 (playtest): on a big map the boss is hard to find; bosses fight on small
		// maps (its blink / relocation / respawn distances are preferences, they fit what is there)
		// (DSM_Choose: any act's small map beats the act's own medium ones)
		fit = size == DSMAP_SMALL ? 1.0f : size == DSMAP_MEDIUM ? 0.03f : 0.002f;
	}
	f = 1.0f;
	if ( id >= 0 && id < DSM_MAX_INFO ) {
		if ( st->plays[id] <= 0 ) {
			f = 2.5f;		// never played in DESCENT
		} else {
			f = ( avgPlays + 1.0f ) / ( st->plays[id] + 1.0f );
			if ( f < 0.2f ) {
				f = 0.2f;
			}
			if ( f > 2.0f ) {
				f = 2.0f;
			}
		}
		if ( st->last[id] > 0 ) {
			since = st->runNo - st->last[id];
			if ( since <= 1 ) {
				f *= 0.3f;
			} else if ( since == 2 ) {
				f *= 0.55f;
			} else if ( since == 3 ) {
				f *= 0.8f;
			}
		}
		if ( L < DS_FLOORS_PER_ACT ) {
			f *= f;		// the first act: the run's start pulls hardest towards the fresh maps
		}
	}
	return fit * f;
}

typedef struct {
	int		first, last, cur;
	int		chosen[DSM_SPAN][RL_MAX_NODES];		// pool index, -1 = not yet
	int		flags[DSM_SPAN][RL_MAX_NODES];
} dsmWork_t;

static dsmWork_t	dsm_w;

// maps on every node above (to cur) or below (to last) node L,i on some path through it
static int DSM_Related( rlLayer_t *layers, int L, int i, int *out ) {
	int		n, l, j, mask, m;

	n = 0;
	mask = 1 << i;
	for ( l = L - 1 ; l >= dsm_w.cur ; l-- ) {
		m = 0;
		for ( j = 0 ; j < layers[l].n ; j++ ) {
			if ( layers[l].edges[j] & mask ) {
				m |= 1 << j;
				if ( dsm_w.chosen[l - dsm_w.first][j] >= 0 ) {
					out[n++] = dsm_w.chosen[l - dsm_w.first][j];
				}
			}
		}
		mask = m;
	}
	mask = 1 << i;
	for ( l = L ; l < dsm_w.last ; l++ ) {
		m = 0;
		for ( j = 0 ; j < layers[l].n ; j++ ) {
			if ( mask & ( 1 << j ) ) {
				m |= layers[l].edges[j];
			}
		}
		mask = m;
		for ( j = 0 ; j < layers[l + 1].n ; j++ ) {
			if ( ( mask & ( 1 << j ) ) && dsm_w.chosen[l + 1 - dsm_w.first][j] >= 0 ) {
				out[n++] = dsm_w.chosen[l + 1 - dsm_w.first][j];
			}
		}
	}
	return n;
}

static qboolean DSM_InList( int idx, const int *list, int n ) {
	int		k;

	for ( k = 0 ; k < n ; k++ ) {
		if ( list[k] == idx ) {
			return qtrue;
		}
	}
	return qfalse;
}

static qboolean DSM_SiblingHas( int L, int i, int n, int idx ) {
	int		j;

	for ( j = 0 ; j < n ; j++ ) {
		if ( j != i && dsm_w.chosen[L - dsm_w.first][j] == idx ) {
			return qtrue;
		}
	}
	return qfalse;
}

static void DSM_Choose( dsmState_t *st, rlLayer_t *layers, int L, int i, int seed, float avgPlays ) {
	static float	w[RL_MAX_MAPS];
	int				rel[RL_MAX_LAYERS * 0 + DSM_SPAN * RL_MAX_NODES];
	int				nrel, tier, k, idx, act, n, best, bestPos, pos;
	int				*pool;
	float			total, r;
	rlLayer_t		*ly = &layers[L];
	qboolean		boss = ( ly->type[i] == RLN_BOSS );

	act = DS_ActForFloor( L + 1 );
	nrel = DSM_Related( layers, L, i, rel );

	for ( tier = 1 ; tier <= 4 ; tier++ ) {
		// 1: the act's pool, no clash on any path; 2: any map, no clash (a borrowed map keeps the act's look);
		// 3 / 4: the same with a clash on some path (re-rolled if that path is taken)
		pool = ( ( tier == 1 || tier == 3 ) && dsm_actNum[act] > 0 && !boss ) ? dsm_actMaps[act] : NULL;	// (bosses: every map, small first)
		n = pool ? dsm_actNum[act] : dsm_numMaps;
		total = 0;
		for ( k = 0 ; k < n ; k++ ) {
			idx = pool ? pool[k] : k;
			w[k] = 0;
			if ( DSM_PlayedPos( st, idx ) >= 0 || DSM_SiblingHas( L, i, ly->n, idx ) ) {
				continue;
			}
			if ( tier <= 2 && DSM_InList( idx, rel, nrel ) ) {
				continue;
			}
			w[k] = DSM_Weight( st, idx, L, ly->bots[i] + 1, boss, avgPlays );
			if ( boss && dsm_actNum[act] > 0 && !DSM_InList( idx, dsm_actMaps[act], dsm_actNum[act] ) ) {
				w[k] *= 0.4f;		// a borrowed small map keeps the act's look
			}
			total += w[k];
		}
		if ( total <= 0 ) {
			continue;
		}
		r = ( DSM_Hash( (unsigned)seed, (unsigned)L * 8 + i, 900 + tier ) & 0xffffff ) / 16777216.0f * total;
		for ( k = 0 ; k < n ; k++ ) {
			if ( w[k] <= 0 ) {
				continue;
			}
			r -= w[k];
			if ( r < 0 ) {
				break;
			}
		}
		if ( k >= n ) {
			for ( k = n - 1 ; k > 0 && w[k] <= 0 ; k-- ) {
			}
		}
		dsm_w.chosen[L - dsm_w.first][i] = pool ? pool[k] : k;
		if ( trap_Cvar_VariableValue( "ui_dsmDebug" ) ) {
			Com_Printf( "dsm: floor %i node %i: tier %i, %s (w %.2f of %.2f, %i related, %i played)\n", L + 1, i, tier,
				dsm_maps[dsm_w.chosen[L - dsm_w.first][i]], w[k], total, nrel, st->numPlayed );
		}
		dsm_w.flags[L - dsm_w.first][i] = 0;
		return;
	}

	// every map was played this run: the least recently played one comes back (not one on a path through here)
	best = -1;
	bestPos = 1 << 30;
	for ( idx = 0 ; idx < dsm_numMaps * 2 ; idx++ ) {
		if ( idx == dsm_numMaps && best >= 0 ) {
			break;
		}
		if ( DSM_SiblingHas( L, i, ly->n, idx % dsm_numMaps ) || ( idx < dsm_numMaps && DSM_InList( idx, rel, nrel ) ) ) {
			continue;
		}
		pos = DSM_PlayedPos( st, idx % dsm_numMaps );
		if ( pos < bestPos ) {
			bestPos = pos;
			best = idx % dsm_numMaps;
		}
	}
	if ( best < 0 ) {
		best = ly->map[i];		// fewer installed maps than nodes on the floor
	}
	dsm_w.chosen[L - dsm_w.first][i] = best;
	dsm_w.flags[L - dsm_w.first][i] = DSMAP_F_REVISIT;
}

// bit i of out[L - first]: node i of floor L can still be reached from the node played above cur
static void DSM_Ahead( rlLayer_t *layers, int first, int cur, int last, int pathPrev, int *out ) {
	int		L, i, slot;

	for ( L = cur ; L <= last ; L++ ) {
		slot = L - first;
		if ( L == cur ) {
			out[slot] = ( pathPrev >= 0 && L > 0 && pathPrev < layers[L - 1].n ) ? layers[L - 1].edges[pathPrev] : ( 1 << layers[L].n ) - 1;
		} else {
			out[slot] = 0;
			for ( i = 0 ; i < layers[L - 1].n ; i++ ) {
				if ( out[slot - 1] & ( 1 << i ) ) {
					out[slot] |= layers[L - 1].edges[i];
				}
			}
		}
	}
}

void UI_DSM_Assign( rlLayer_t *layers, int count, int seed ) {
	dsmState_t	*st;
	rlLayer_t	*ly;
	char		oldFM[RL_VAL_LEN];
	int			L, i, slot, idx, pass, act, n, k;
	int			order[DSM_SPAN];
	int			ahead[DSM_SPAN];
	float		avgPlays;
	qboolean	valid;

	if ( !dsm_maps || dsm_numMaps <= 0 || count <= 0 ) {
		return;
	}
	UI_DSM_RegisterCvars();
	trap_Cvar_Update( &dsm_variants );
	if ( dsm_simOn ) {
		st = &dsm_sim;
	} else {
		st = &dsm_st;
		if ( !DSM_LoadRun( st ) ) {
			return;
		}
	}

	dsm_w.cur = st->cur;
	if ( dsm_w.cur >= count ) {
		return;
	}
	dsm_w.first = dsm_w.cur - 2 < 0 ? 0 : dsm_w.cur - 2;
	dsm_w.last = dsm_w.first + RL_VIS_LAYERS - 1;
	if ( dsm_w.last > count - 1 ) {
		dsm_w.last = count - 1;
	}
	Q_strncpyz( oldFM, DSM_FMString( st ), sizeof( oldFM ) );
	if ( st->fmFirst != dsm_w.first ) {
		// the window moved: re-base the stored layers
		dsmState_t	*t = st;
		int			shift = dsm_w.first - t->fmFirst;

		for ( slot = 0 ; slot < DSM_SPAN ; slot++ ) {
			k = slot + shift;
			if ( k >= 0 && k < DSM_SPAN ) {
				t->fmN[slot] = t->fmN[k];
				for ( i = 0 ; i < RL_MAX_NODES ; i++ ) {
					Q_strncpyz( t->fm[slot][i], t->fm[k][i], sizeof( t->fm[0][0] ) );
					t->fmFlag[slot][i] = t->fmFlag[k][i];
				}
			} else {
				t->fmN[slot] = 0;
			}
		}
		t->fmFirst = dsm_w.first;
	}

	// the average play count over the installed known maps
	avgPlays = 0;
	n = 0;
	for ( idx = 0 ; idx < dsm_numMaps ; idx++ ) {
		k = BG_DSM_MapId( dsm_maps[idx] );
		if ( k >= 0 && k < DSM_MAX_INFO ) {
			avgPlays += st->plays[k];
			n++;
		}
	}
	avgPlays = n ? avgPlays / n : 0;

	// variants (deterministic per node) and the floors above: as stored
	for ( L = dsm_w.first ; L <= dsm_w.last ; L++ ) {
		ly = &layers[L];
		slot = L - dsm_w.first;
		act = DS_ActForFloor( L + 1 );
		for ( i = 0 ; i < ly->n ; i++ ) {
			dsm_w.chosen[slot][i] = -1;
			dsm_w.flags[slot][i] = 0;
			ly->mflags[i] = 0;
			ly->variant[i] = dsm_variants.integer ? BG_DSM_PickVariant( DSM_Hash( (unsigned)seed, (unsigned)L, 60 + i ), act,
				ly->type[i] == RLN_BOSS || ly->type[i] == RLN_VAULT ) : 0;
			if ( ly->type[i] == RLN_VAULT ) {
				ly->variant[i] = 0;
			}
		}
		if ( L < dsm_w.cur && st->fmN[slot] == ly->n ) {
			for ( i = 0 ; i < ly->n ; i++ ) {
				idx = DSM_Find( st->fm[slot][i] );
				if ( idx >= 0 ) {
					ly->map[i] = idx;
					ly->mflags[i] = st->fmFlag[slot][i];
				}
			}
		}
	}

	// the nodes still ahead of you (a node off your path never changes: it can't be played)
	DSM_Ahead( layers, dsm_w.first, dsm_w.cur, dsm_w.last, st->pathPrev, ahead );

	// keep what was shown, when still fine
	for ( L = dsm_w.cur ; L <= dsm_w.last ; L++ ) {
		ly = &layers[L];
		slot = L - dsm_w.first;
		for ( i = 0 ; i < ly->n ; i++ ) {
			idx = -1;
			valid = qfalse;
			if ( !( ahead[slot] & ( 1 << i ) ) && st->fmN[slot] == ly->n && ( idx = DSM_Find( st->fm[slot][i] ) ) >= 0
				&& !DSM_SiblingHas( L, i, ly->n, idx ) ) {
				dsm_w.chosen[slot][i] = idx;		// off your path: as shown
				dsm_w.flags[slot][i] = st->fmFlag[slot][i];
				continue;
			}
			if ( !dsm_simOn && st->devMap > 0 && st->devL == L && st->devI == i && st->devMap <= dsm_numMaps ) {
				idx = st->devMap - 1;
				valid = qtrue;
			} else if ( L == st->resumeL && i == st->resumeI && st->resumeMap[0] ) {
				idx = DSM_Find( st->resumeMap );
				valid = idx >= 0;		// the floor in progress keeps its map
			} else if ( st->fmN[slot] == ly->n && st->fm[slot][i][0] ) {
				idx = DSM_Find( st->fm[slot][i] );
				if ( idx >= 0 && !DSM_SiblingHas( L, i, ly->n, idx ) ) {
					k = DSM_PlayedPos( st, idx );
					valid = ( k < 0 ) || ( ( st->fmFlag[slot][i] & DSMAP_F_REVISIT ) && k < st->numPlayed - DSM_RECENT );
				}
			}
			if ( valid ) {
				dsm_w.chosen[slot][i] = idx;
				dsm_w.flags[slot][i] = ( st->fmN[slot] == ly->n ) ? ( st->fmFlag[slot][i] & DSMAP_F_REVISIT ) : 0;
				if ( DSM_PlayedPos( st, idx ) < 0 ) {
					dsm_w.flags[slot][i] = 0;
				}
			}
		}
	}

	// fill the rest: boss floors first (everything above them in the act is "related")
	n = 0;
	for ( pass = 0 ; pass < 2 ; pass++ ) {
		for ( L = dsm_w.cur ; L <= dsm_w.last ; L++ ) {
			if ( ( layers[L].type[0] == RLN_BOSS ) == ( pass == 0 ) ) {
				order[n++] = L;
			}
		}
	}
	for ( k = 0 ; k < n ; k++ ) {
		L = order[k];
		ly = &layers[L];
		for ( i = 0 ; i < ly->n ; i++ ) {
			if ( dsm_w.chosen[L - dsm_w.first][i] < 0 ) {
				DSM_Choose( st, layers, L, i, seed, avgPlays );
			}
		}
	}

	// into the floors, and stored
	for ( L = dsm_w.cur ; L <= dsm_w.last ; L++ ) {
		ly = &layers[L];
		slot = L - dsm_w.first;
		st->fmN[slot] = ly->n;
		for ( i = 0 ; i < ly->n ; i++ ) {
			ly->map[i] = dsm_w.chosen[slot][i];
			ly->mflags[i] = dsm_w.flags[slot][i];
			Q_strncpyz( st->fm[slot][i], dsm_maps[ly->map[i]], sizeof( st->fm[0][0] ) );
			st->fmFlag[slot][i] = ly->mflags[i];
		}
	}
	for ( slot = dsm_w.last - dsm_w.first + 1 ; slot < DSM_SPAN ; slot++ ) {
		st->fmN[slot] = 0;
	}

	if ( !dsm_simOn && strcmp( oldFM, DSM_FMString( st ) ) ) {
		RL_KVSet( &rl_run, "ds_fm", DSM_FMString( st ) );
		RL_SaveRun();
	}
}

/*
=================
UI_DSM_Preview

en-daily: the floors a fresh run of this seed shows on its first floor map, without a run
file: the sim state as DSM_LoadRun would build it for floor 1 (nothing played, the lifetime
counts RL_LoadMeta gives: a blank profile while the daily is on). Writes nothing.
=================
*/
rlLayer_t *UI_DSM_Preview( int seed, int count ) {
	rlLayer_t	*ly;

	memset( &dsm_sim, 0, sizeof( dsm_sim ) );
	DSM_LoadStats( &dsm_sim );
	dsm_sim.resumeL = dsm_sim.resumeI = -1;
	dsm_sim.pathPrev = -1;
	dsm_simOn = qtrue;
	ly = RL_DSM_Layers( seed, count );
	dsm_simOn = qfalse;
	return ly;
}

// en-daily: a pool map index's name ("" out of range)
const char *UI_DSM_MapName( int idx ) {
	return ( dsm_maps && idx >= 0 && idx < dsm_numMaps ) ? dsm_maps[idx] : "";
}

void UI_DSM_DevForce( int L, int i, int mapIndex ) {
	dsm_st.devL = L;
	dsm_st.devI = i;
	dsm_st.devMap = mapIndex + 1;		// (0: none)
}

void UI_DSM_Launch( int L, int i, const char *map, int variant ) {
	dsmState_t	*st = &dsm_st;
	int			id;
	qboolean	resume;

	if ( dsm_simOn || !DSM_LoadRun( st ) ) {
		return;
	}
	resume = ( L == st->resumeL && i == st->resumeI && !Q_stricmp( st->resumeMap, map ) );
	if ( !resume ) {
		DSM_AddPlayed( st, map );
		RL_KVSet( &rl_run, "ds_played", DSM_PlayedString( st ) );
		id = BG_DSM_MapId( map );
		if ( id >= 0 && id < DSM_MAX_INFO ) {
			st->plays[id]++;
			st->last[id] = st->runNo;
			DSM_SaveStats( st );		// (DSM_LoadRun loaded the meta file)
			RL_SaveMeta();
		}
	}
	RL_KVSetInt( &rl_run, "node_variant", dsm_variants.integer ? variant : 0 );
	if ( st->devL == L && st->devI == i ) {
		st->devMap = 0;
	}
	Com_Printf( "DESCENT maps: floor %i on %s%s%s (%i played this run)\n", L + 1, map, variant && dsm_variants.integer ? ", " : "",
		dsm_variants.integer ? BG_DSM_VariantLabel( variant ) : "", st->numPlayed );
}

/*
=======================================================================

DRAWING (the floor map)

=======================================================================
*/

static qboolean DSM_IsNew( const char *map ) {
	int		id = BG_DSM_MapId( map );

	if ( !dsm_st.loaded ) {
		return qfalse;
	}
	return id >= 0 && id < DSM_MAX_INFO && dsm_st.plays[id] <= 0;
}

static const char *dsm_layoutDescs[DSV_LAYOUT_NUM] = {
	"",
	"The weapons trade places.",
	"Armor and health trade places.",
	"Weapons, armor, health: all moved."
};

float UI_DSM_DrawMapLine( float x, float y, float w, const char *map, int variant, int mflags, qboolean past ) {
	const dsLightVariant_t	*lv;
	const char				*label;
	vec4_t					c;
	float					tx, p;

	trap_Cvar_Update( &dsm_variants );
	RL_Text( x, y, map, 5, 8, rl_colDark, 0 );
	tx = x + strlen( map ) * 5 + 8;
	label = dsm_variants.integer ? BG_DSM_VariantLabel( variant ) : "";
	if ( label[0] ) {
		lv = &ds_lightVariants[DSV_LIGHT( variant ) % DSV_LIGHT_NUM];
		if ( DSV_LIGHT( variant ) ) {
			c[0] = 0.45f + 0.55f * lv->mul[0];
			c[1] = 0.45f + 0.55f * lv->mul[1];
			c[2] = 0.45f + 0.55f * lv->mul[2];
		} else {
			c[0] = 1.0f;
			c[1] = 0.72f;
			c[2] = 0.4f;
		}
		c[3] = past ? 0.5f : 1.0f;
		if ( tx + strlen( label ) * 5 > x + w - ( ( !past && DSM_IsNew( map ) ) ? 24 : 0 ) ) {
			y += 10;		// too long beside the name: its own line
			tx = x;
		}
		RL_Text( tx, y, label, 5, 8, c, 0 );
	}
	if ( !past && ( mflags & DSMAP_F_REVISIT ) ) {
		RL_Text( x + w, y, "REVISITED", 4, 8, rl_colDim, 2 );
	} else if ( !past && DSM_IsNew( map ) ) {
		p = RL_Pulse( 0.004f );
		Vector4Copy( rl_colGood, c );
		c[3] = 0.7f + 0.3f * p;
		RL_Text( x + w, y, "NEW", 5, 8, c, 2 );
	}
	y += 11;
	if ( dsm_variants.integer && DSV_LAYOUT( variant ) && !past ) {
		y = RL_TextWrap( x, y, w, dsm_layoutDescs[DSV_LAYOUT( variant )], 4, 7, rl_colDim, 1 );
	}
	return y + 3;
}

void UI_DSM_DrawNodeTag( float x, float y, float sz, const char *map, int mflags, float k, qboolean ahead ) {
	vec4_t	c;

	if ( !ahead ) {
		return;
	}
	// the nodes you can pick now (full brightness) say it; the floors further down get a small mark
	if ( mflags & DSMAP_F_REVISIT ) {
		RL_Scale( c, rl_colDim, k, 1 );
		if ( k >= 1.0f ) {
			RL_Text( x - sz / 2 - 3, y - 4, "AGAIN", 4, 7, c, 2 );
		} else {
			UI_FillRect( x - sz / 2 - 1, y - sz / 2 - 1, 3, 3, c );
		}
	} else if ( DSM_IsNew( map ) ) {
		RL_Scale( c, rl_colGood, k, 1 );
		if ( k >= 1.0f ) {
			RL_Text( x - sz / 2 - 3, y - 4, "NEW", 4, 7, c, 2 );
		} else {
			UI_FillRect( x - sz / 2 - 1, y - sz / 2 - 1, 3, 3, c );
		}
	}
}

/*
=======================================================================

DEV: dsmaps, dsmapsim

=======================================================================
*/

static void DSM_PrintPools( fileHandle_t f ) {
	char	line[1024];
	int		a, k, id, s[3];
	static const char	*actNames[DS_ACT_ABYSS + 1] = { "", "1 CRYPTS", "2 FOUNDRY", "3 VOID", "4 PIT", "5 ABYSS" };
	static const char	*sizeNames = "SML";

	for ( a = 1 ; a <= DS_ACT_ABYSS ; a++ ) {
		s[0] = s[1] = s[2] = 0;
		Com_sprintf( line, sizeof( line ), "act %-9s %2i maps:", actNames[a], dsm_actNum[a] );
		for ( k = 0 ; k < dsm_actNum[a] ; k++ ) {
			id = BG_DSM_MapId( dsm_maps[dsm_actMaps[a][k]] );
			s[( id >= 0 && ds_mapInfo[id].size >= 0 ) ? ds_mapInfo[id].size : 1]++;
			if ( a < DS_ACT_ABYSS ) {
				Q_strcat( line, sizeof( line ), va( " %s(%c)", dsm_maps[dsm_actMaps[a][k]], ( id >= 0 && ds_mapInfo[id].size >= 0 ) ? sizeNames[ds_mapInfo[id].size] : '?' ) );
			}
		}
		Q_strcat( line, sizeof( line ), va( "  [S %i M %i L %i]\n", s[0], s[1], s[2] ) );
		Com_Printf( "%s", line );
		if ( f ) {
			trap_FS_Write( line, strlen( line ), f );
		}
	}
}

static void DSM_Out( fileHandle_t f, const char *s ) {
	Com_Printf( "%s", s );
	if ( f ) {
		trap_FS_Write( s, strlen( s ), f );
	}
}

/*
=================
DSM_Simulate

runs x floors: a random walk down each run's floor map (a loss now and then: the floor is
retried), checking every shown node. Fresh profile, or the real lifetime counts ("real").
=================
*/
#define DSM_SIM_MAXF	120

static void DSM_Simulate( int runs, int floors, int lossPct, const char *profile ) {
	static int	seen[DSM_SIM_MAXF + RL_VIS_LAYERS + 2][RL_MAX_NODES];
	static int	act1[RL_MAX_MAPS], allPlays[RL_MAX_MAPS];
	static int	path[DSM_SIM_MAXF + 2];
	int			ahead[DSM_SPAN];
	static char	runLine[2048];
	rlLayer_t	*layers;
	fileHandle_t	f;
	int			r, seed, launches, L, i, j, last, first, n, reach[RL_MAX_NODES], pick, idx, id, lost, slot;
	int			repeats, revisits, rerolls, lossRerolls, offeredPlayed, sibDup, totalLaunch, lastLost, borrowed, k2, act;
	int			distinct, mn, mx;
	char		line[256];

	if ( floors > DSM_SIM_MAXF ) {
		floors = DSM_SIM_MAXF;
	}
	memset( &dsm_sim, 0, sizeof( dsm_sim ) );
	dsm_sim.runNo = 1;
	if ( !Q_stricmp( profile, "real" ) ) {
		DSM_LoadStats( &dsm_sim );		// the lifetime counts in rearmed_meta.txt
	} else if ( !Q_stricmpn( profile, "log:", 4 ) ) {
		DSM_SeedFromLog( dsm_sim.plays, profile + 4 );		// the counts a game log gives (a copy of the player's)
	}
	memset( act1, 0, sizeof( act1 ) );
	memset( allPlays, 0, sizeof( allPlays ) );
	repeats = revisits = rerolls = lossRerolls = offeredPlayed = sibDup = totalLaunch = borrowed = 0;

	f = 0;
	trap_FS_FOpenFile( va( "rearmed_mapsim_%s_%i.txt", !profile[0] ? "fresh" : !Q_stricmpn( profile, "log:", 4 ) ? "log" : "real", floors ), &f, FS_WRITE );
	DSM_Out( f, va( "DESCENT map simulation: %i runs x %i floors, %i%% floors lost and retried, %s profile, %i maps installed\n",
		runs, floors, lossPct, profile[0] ? profile : "a fresh", dsm_numMaps ) );
	DSM_PrintPools( f );

	dsm_simOn = qtrue;
	for ( r = 0 ; r < runs ; r++ ) {
		seed = (int)( DSM_Hash( (unsigned)r + 1, 777, 31337 ) & 0x7fffffff );
		dsm_sim.numPlayed = 0;
		dsm_sim.cur = 0;
		dsm_sim.resumeL = dsm_sim.resumeI = -1;
		dsm_sim.pathPrev = -1;
		dsm_sim.fmFirst = 0;
		memset( dsm_sim.fmN, 0, sizeof( dsm_sim.fmN ) );
		for ( L = 0 ; L < DSM_SIM_MAXF + RL_VIS_LAYERS + 2 ; L++ ) {
			for ( i = 0 ; i < RL_MAX_NODES ; i++ ) {
				seen[L][i] = -1;
			}
		}
		Com_sprintf( runLine, sizeof( runLine ), "run %2i:", r + 1 );
		launches = 0;
		lastLost = -1;
		while ( dsm_sim.cur < floors && launches < floors * 3 ) {
			dsm_sim.pathPrev = dsm_sim.cur > 0 ? path[dsm_sim.cur - 1] : -1;
			layers = RL_DSM_Layers( seed, dsm_sim.cur + RL_VIS_LAYERS + 1 );
			first = dsm_sim.cur - 2 < 0 ? 0 : dsm_sim.cur - 2;
			last = first + RL_VIS_LAYERS - 1;
			DSM_Ahead( layers, first, dsm_sim.cur, last, dsm_sim.pathPrev, ahead );
			for ( L = dsm_sim.cur ; L <= last ; L++ ) {
				for ( i = 0 ; i < layers[L].n ; i++ ) {
					idx = layers[L].map[i];
					if ( !( ahead[L - first] & ( 1 << i ) ) ) {
						seen[L][i] = -1;		// off the path: never played, never counted
						continue;
					}
					if ( seen[L][i] >= 0 && seen[L][i] != idx ) {
						if ( L == lastLost ) {
							lossRerolls++;		// the floor you lost on: its played map is replaced
						} else {
							rerolls++;
						}
					}
					seen[L][i] = idx;
					for ( j = 0 ; j < i ; j++ ) {
						if ( layers[L].map[j] == idx ) {
							sibDup++;
						}
					}
					if ( DSM_PlayedPos( &dsm_sim, idx ) >= 0 && !( layers[L].mflags[i] & DSMAP_F_REVISIT ) ) {
						offeredPlayed++;
					}
				}
			}
			// the reachable nodes, then a random pick
			L = dsm_sim.cur;
			n = 0;
			for ( i = 0 ; i < layers[L].n ; i++ ) {
				if ( L == 0 || ( layers[L - 1].edges[path[L - 1]] & ( 1 << i ) ) ) {
					reach[n++] = i;
				}
			}
			if ( !n ) {
				break;
			}
			pick = reach[DSM_Hash( (unsigned)seed, (unsigned)launches, 4242 ) % n];
			idx = layers[L].map[pick];
			if ( layers[L].mflags[pick] & DSMAP_F_REVISIT ) {
				revisits++;
			} else if ( DSM_PlayedPos( &dsm_sim, idx ) >= 0 ) {
				repeats++;
			}
			DSM_AddPlayed( &dsm_sim, dsm_maps[idx] );
			id = BG_DSM_MapId( dsm_maps[idx] );
			if ( id >= 0 && id < DSM_MAX_INFO ) {
				dsm_sim.plays[id]++;
				dsm_sim.last[id] = dsm_sim.runNo;
			}
			allPlays[idx]++;
			act = DS_ActForFloor( L + 1 );
			for ( k2 = 0 ; k2 < dsm_actNum[act] && dsm_actMaps[act][k2] != idx ; k2++ ) {
			}
			if ( k2 >= dsm_actNum[act] ) {
				borrowed++;
			}
			if ( DS_ActForFloor( L + 1 ) == 1 ) {
				act1[idx]++;
			}
			launches++;
			totalLaunch++;
			if ( L < 24 ) {
				Q_strcat( runLine, sizeof( runLine ), va( " %s%s", dsm_maps[idx], ( layers[L].mflags[pick] & DSMAP_F_REVISIT ) ? "*" : "" ) );
			}
			lost = (int)( DSM_Hash( (unsigned)seed, (unsigned)launches, 5151 ) % 100 ) < lossPct;
			if ( lost ) {
				lastLost = L;
				Q_strcat( runLine, sizeof( runLine ), "(lost)" );
			} else {
				path[L] = pick;
				dsm_sim.cur++;
				lastLost = -1;
			}
		}
		Q_strcat( runLine, sizeof( runLine ), "\n" );
		DSM_Out( f, runLine );
		dsm_sim.runNo++;
	}
	dsm_simOn = qfalse;

	DSM_Out( f, va( "floors launched %i: repeats within a run %i, REVISITED (pool dry) %i, shown nodes offering a played map %i, sibling duplicates %i\n",
		totalLaunch, repeats, revisits, offeredPlayed, sibDup ) );
	DSM_Out( f, va( "shown map changed before it was played: %i (a path made it a repeat), plus %i on floors lost and retried; %i floors borrowed a map from another act's pool\n", rerolls, lossRerolls, borrowed ) );
	DSM_Out( f, "act 1 (floors 1-6) maps over all runs:\n" );
	distinct = 0;
	mn = 1 << 30;
	mx = 0;
	for ( slot = 0 ; slot < dsm_actNum[1] ; slot++ ) {
		idx = dsm_actMaps[1][slot];
		Com_sprintf( line, sizeof( line ), "  %-16s %3i\n", dsm_maps[idx], act1[idx] );
		DSM_Out( f, line );
		if ( act1[idx] ) {
			distinct++;
		}
		if ( act1[idx] < mn ) {
			mn = act1[idx];
		}
		if ( act1[idx] > mx ) {
			mx = act1[idx];
		}
	}
	for ( idx = 0 ; idx < dsm_numMaps ; idx++ ) {
		for ( slot = 0 ; slot < dsm_actNum[1] && dsm_actMaps[1][slot] != idx ; slot++ ) {
		}
		if ( slot >= dsm_actNum[1] && act1[idx] ) {
			DSM_Out( f, va( "  %-16s %3i (borrowed)\n", dsm_maps[idx], act1[idx] ) );
		}
	}
	DSM_Out( f, va( "act 1 pool: %i maps, %i used, min %i max %i plays\n", dsm_actNum[1], distinct, mn, mx ) );
	DSM_Out( f, "every floor, every map:" );
	for ( idx = 0 ; idx < dsm_numMaps ; idx++ ) {
		DSM_Out( f, va( " %s %i", dsm_maps[idx], allPlays[idx] ) );
	}
	DSM_Out( f, "\n" );
	if ( f ) {
		trap_FS_FCloseFile( f );
	}
	Com_Printf( "dsmapsim: written to rearmed_mapsim_*.txt\n" );
	dsm_st.loaded = qfalse;
}

qboolean UI_DSM_ConsoleCommand( const char *cmd ) {
	int		i, id;

	if ( !Q_stricmp( cmd, "dsmapsim" ) ) {
		char	profile[64];
		int		runs, floors, loss;

		runs = atoi( UI_Argv( 1 ) );
		floors = atoi( UI_Argv( 2 ) );
		loss = UI_Argv( 3 )[0] ? atoi( UI_Argv( 3 ) ) : 15;
		Q_strncpyz( profile, UI_Argv( 4 ), sizeof( profile ) );
		RL_DSM_Layers( 1, 1 );		// the pool
		DSM_Simulate( runs > 0 ? runs : 20, floors > 0 ? floors : 24, loss, profile );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsmaps" ) ) {
		RL_LoadRun();
		RL_DSM_Layers( RL_KVInt( &rl_run, "seed" ), DSM_CurLayer( RL_KVInt( &rl_run, "floor" ) ) + RL_VIS_LAYERS + 1 );
		DSM_PrintPools( 0 );
		if ( !dsm_st.loaded ) {
			DSM_LoadStats( &dsm_st );
		}
		Com_Printf( "lifetime (plays/last run, run now %i):", dsm_st.runNo );
		for ( i = 0 ; i < dsm_numMaps ; i++ ) {
			id = BG_DSM_MapId( dsm_maps[i] );
			if ( id >= 0 ) {
				Com_Printf( " %s %i/%i", dsm_maps[i], dsm_st.plays[id], dsm_st.last[id] );
			}
		}
		Com_Printf( "\nthis run: played %s\nwindow: %s\n", RL_KVGet( &rl_run, "ds_played" ), RL_KVGet( &rl_run, "ds_fm" ) );
		return qtrue;
	}
	return qfalse;
}
