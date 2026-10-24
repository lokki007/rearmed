/*
===========================================================================
REARMED -- DESCENT map variety (ds-maps): the map table, floor variants.
Shared by game, cgame and ui (tables in bg_ds_maps.c). Included at the end of bg_descent.h.

- Every map a DESCENT floor can use: ds_mapInfo[] (stable ids: only append; the lifetime
  play counts in rearmed_meta.txt "dsm_plays" / "dsm_last" are csv in this order).
  The act pools stay in ds_acts[].maps (bg_descent.c); a pool map that is not installed
  (or has no bot .aas file) is skipped by the UI.
- A floor VARIANT (run key node_variant = light | layout << 4): a lighting tint / haze for
  the cgame (cg_ds_maps.c, cg_dsVariantLight) and an item layout change for the game
  (g_ds_maps.c, g_dsVariants): the weapons swap places, or armor and health swap places.
  No map is edited: everything happens at map load / on screen.
Game: game/g_ds_maps.c. cgame: cgame/cg_ds_maps.c. ui: q3_ui/ui_ds_maps.c.
===========================================================================
*/
#ifndef BG_DS_MAPS_H
#define BG_DS_MAPS_H

#define DSMAP_SMALL			0		// tourney size: 2-3 fighters
#define DSMAP_MEDIUM			1		// classic deathmatch: 4-5
#define DSMAP_LARGE			2		// CTF / team arenas: 5+
#define DSMAP_REJECTED		-1		// tested and left out (the reason is in bg_ds_maps.c); the id stays

#define DSMAP_SRC_ID			0		// Quake III Arena
#define DSMAP_SRC_POINT		1		// the point release "pro" maps
#define DSMAP_SRC_REARMED		2		// our rm_ remixes
#define DSMAP_SRC_TA			3		// Team Arena (copied from the player's own install)

typedef struct {
	const char	*name;
	const char	*longname;
	int			size;			// DSMAP_SMALL / MEDIUM / LARGE
	int			src;			// DSMAP_SRC_
} dsMapInfo_t;

extern const dsMapInfo_t	ds_mapInfo[];
extern const int			ds_numMapInfo;

int BG_DSM_MapId( const char *name );						// -1: not in the table

// lighting variants (cgame)
typedef struct {
	const char	*name;			// the floor map tag ("" = as built)
	float		mul[3];			// a colour multiply over the world (under the HUD)
	float		edge[4];		// edge haze rgb + alpha (the vignette)
	float		haze[3];		// an even additive haze (washes the contrast out: fog / flooded light)
	int			acts;			// bit (1 << act): the acts it can show up in
	qboolean	dark;			// skipped under the BLACKOUT floor modifier
} dsLightVariant_t;

#define DSV_LIGHT_NUM		10
extern const dsLightVariant_t	ds_lightVariants[DSV_LIGHT_NUM];

// layout variants (game)
#define DSV_LAYOUT_NONE		0
#define DSV_LAYOUT_ARMS		1		// the weapon pickups trade places
#define DSV_LAYOUT_ARMOR	2		// armor / mega and health trade places
#define DSV_LAYOUT_BOTH		3
#define DSV_LAYOUT_NUM		4
extern const char *ds_layoutVariantNames[DSV_LAYOUT_NUM];

#define DSV_LIGHT(v)		( (v) & 15 )
#define DSV_LAYOUT(v)		( ( (v) >> 4 ) & 3 )

// the variant of a floor-map node from a hash (deterministic): about 55% get a lighting tag,
// 45% a layout change (never on boss / vault floors: those are tuned); bossOrVault 1 = light only
int BG_DSM_PickVariant( unsigned int h, int act, qboolean bossOrVault );
// "NIGHT  ARMS SWAP" (empty: as built)
const char *BG_DSM_VariantLabel( int variant );

#endif
