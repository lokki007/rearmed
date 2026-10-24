/*
===========================================================================
REARMED -- DESCENT map variety, UI side (ds-maps): which map each floor-map node gets.
Shared by ui_roguelike.c (the floor map) and ui_ds_maps.c. The floor layer type lives here
so both files can see it.
===========================================================================
*/
#ifndef UI_DS_MAPS_H
#define UI_DS_MAPS_H

#define RL_MAX_MAPS			64
#define RL_MAX_LAYERS		200
#define RL_MAX_NODES		4
#define RL_VIS_LAYERS		7
#define RL_MAX_OFFER		4

// node types
#define RLN_FIGHT			0
#define RLN_ELITE			1
#define RLN_EVENT			2
#define RLN_BOSS			3
#define RLN_VAULT			4		// ACTS: the rare secret bonus floor ("???")
#define RLN_NUM				5

// node map flags (ds-maps)
#define DSMAP_F_REVISIT		1		// the pool ran dry: a map already played this run (the least recent)

typedef struct {
	int		n;
	int		type[RL_MAX_NODES];
	int		map[RL_MAX_NODES];
	int		bots[RL_MAX_NODES];
	int		frags[RL_MAX_NODES];
	int		mut[RL_MAX_NODES];
	int		jitter[RL_MAX_NODES];
	int		edges[RL_MAX_NODES];	// bit j: a path to node j of the next layer
	int		variant[RL_MAX_NODES];	// ds-maps: lighting | layout << 4 (bg_ds_maps.h)
	int		mflags[RL_MAX_NODES];	// ds-maps: DSM_F_
} rlLayer_t;

// ui_roguelike.c: the floors of a seed (ds-maps' simulation builds other seeds through it)
rlLayer_t *RL_DSM_Layers( int seed, int count );

// ui_ds_maps.c
void UI_DSM_RegisterCvars( void );
qboolean UI_DSM_HasAAS( const char *name );
const char *UI_DSM_LongName( const char *name );		// NULL: not in the table
void UI_DSM_SetPool( char maps[][32], int numMaps, int actMaps[][RL_MAX_MAPS], int *actNum );
// RL_BuildLayers (every call, cached or not): the window's maps, never one played this run
void UI_DSM_Assign( rlLayer_t *layers, int count, int seed );
// RL_LaunchNode, before the run is saved: played list, lifetime counts, node_variant
void UI_DSM_Launch( int L, int i, const char *map, int variant );
// dev: dsboss <act> <map> / dsvault <map> force a node's map until the run moves on
void UI_DSM_DevForce( int L, int i, int mapIndex );
// the floor map: the tag line under the map's name in the info panel (returns the new y)
float UI_DSM_DrawMapLine( float x, float y, float w, const char *map, int variant, int mflags, qboolean past );
// the floor map: a small NEW / REVISITED mark beside a node
void UI_DSM_DrawNodeTag( float x, float y, float sz, const char *map, int mflags, float k, qboolean ahead );
qboolean UI_DSM_ConsoleCommand( const char *cmd );		// dsmaps, dsmapsim
// en-daily: the floors a fresh run of a seed shows (no file written), a pool map's name
rlLayer_t *UI_DSM_Preview( int seed, int count );
const char *UI_DSM_MapName( int idx );

#endif
