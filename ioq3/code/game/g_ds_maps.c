/*
===========================================================================
REARMED -- DESCENT map variety, game side (ds-maps). Contract: bg_ds_maps.h.

At map load in DESCENT (G_DSM_PostSpawn, right after the map's entities spawned, before the
items drop to the floor):
- CTF / Team Arena maps play as free-for-all arenas: their flags go, and a map with fewer
  than 8 deathmatch spawns gets one at every team spawn spot (team_CTF_red/blue spawn and
  player), so the floor's bots and the staggered arrivals have room.
- the floor's layout variant (run key node_variant, g_dsVariants): ARMS SWAP = the weapon
  pickups trade places (a seeded shuffle of their kinds), ARMOR SHIFT = armor / mega and
  health pickups trade places, SCRAMBLED = both. Nothing is added or removed. The bots'
  item knowledge follows (botlib BotUpdateEntityItems re-types a level item at its spot).
- CS_DESCENT "mv" carries the variant to the cgame (lighting: cg_ds_maps.c).
===========================================================================
*/
#include "g_local.h"

static vmCvar_t	g_dsVariants;
static int		dsmVariant;

#define DSM_MAX_TEAMSPOTS	128
static vec3_t	dsmTeamSpot[DSM_MAX_TEAMSPOTS], dsmTeamAng[DSM_MAX_TEAMSPOTS];
static int		dsmNumTeamSpots;

/*
=================
G_DSM_SpawnVars

g_spawn.c, every map entity once its fields are parsed (before notfree / gametype drop it):
the team spawn spots of CTF / Team Arena maps
=================
*/
void G_DSM_SpawnVars( gentity_t *ent ) {
	const char	*cls = ent->classname;

	if ( !cls || Q_stricmpn( cls, "team_CTF_", 9 ) || ( !Q_stristr( cls, "spawn" ) && !Q_stristr( cls, "player" ) ) ) {
		return;
	}
	if ( dsmNumTeamSpots < DSM_MAX_TEAMSPOTS ) {
		VectorCopy( ent->s.origin, dsmTeamSpot[dsmNumTeamSpots] );
		VectorCopy( ent->s.angles, dsmTeamAng[dsmNumTeamSpots] );
		dsmNumTeamSpots++;
	}
}

static unsigned int DSM_GRand( unsigned int *s ) {
	*s = *s * 1103515245u + 12345u;
	return ( *s >> 8 ) & 0xffffff;
}

static qboolean DSM_GIsPendingItem( gentity_t *e, int type ) {
	return e->inuse && e->item && e->item->giType == type && e->think == FinishSpawningItem && !( e->flags & FL_DROPPED_ITEM );
}

/*
=================
G_DSM_PostSpawn

g_main.c G_InitGame, right after G_SpawnEntitiesFromString
=================
*/
void G_DSM_PostSpawn( void ) {
	static gentity_t	*list[MAX_GENTITIES / 8];
	static gentity_t	*list2[MAX_GENTITIES / 8];
	gentity_t			*e, *sp;
	gitem_t				*tmp;
	vec3_t				o;
	unsigned int		seed;
	int					i, j, n, n2, dm, added, flags, layout, swaps;
	char				*cls;

	trap_Cvar_Register( &g_dsVariants, "g_dsVariants", "1", CVAR_ARCHIVE );
	dsmVariant = 0;
	if ( !G_RL_Active() ) {
		dsmNumTeamSpots = 0;
		return;
	}

	// free-for-all: the flags of a CTF map go
	flags = 0;
	for ( i = MAX_CLIENTS ; i < level.num_entities ; i++ ) {
		e = &g_entities[i];
		if ( e->inuse && e->item && e->item->giType == IT_TEAM ) {
			G_FreeEntity( e );
			flags++;
		}
	}

	// a deathmatch spawn at every team spawn when the map has few of its own
	dm = 0;
	for ( i = MAX_CLIENTS ; i < level.num_entities ; i++ ) {
		e = &g_entities[i];
		if ( e->inuse && e->classname && !Q_stricmp( e->classname, "info_player_deathmatch" ) ) {
			dm++;
		}
	}
	added = 0;
	if ( dm < 8 ) {
		for ( i = 0 ; i < dsmNumTeamSpots ; i++ ) {
			sp = G_Spawn();
			sp->classname = "info_player_deathmatch";
			VectorCopy( dsmTeamSpot[i], sp->s.origin );
			VectorCopy( dsmTeamAng[i], sp->s.angles );
			added++;
		}
	}
	dsmNumTeamSpots = 0;

	// the floor's layout variant
	dsmVariant = g_dsVariants.integer ? atoi( G_RL_RunGet( "node_variant" ) ) : 0;
	layout = DSV_LAYOUT( dsmVariant );
	seed = (unsigned)atoi( G_RL_RunGet( "seed" ) ) * 2654435761u + (unsigned)G_RL_Floor() * 40503u + 17u;
	swaps = 0;
	if ( layout == DSV_LAYOUT_ARMS || layout == DSV_LAYOUT_BOTH ) {
		n = 0;
		for ( i = MAX_CLIENTS ; i < level.num_entities && n < (int)ARRAY_LEN( list ) ; i++ ) {
			if ( DSM_GIsPendingItem( &g_entities[i], IT_WEAPON ) ) {
				list[n++] = &g_entities[i];
			}
		}
		// a shuffle of the kinds: every weapon stays on the map, on another spot
		for ( i = n - 1 ; i > 0 ; i-- ) {
			j = DSM_GRand( &seed ) % ( i + 1 );
			if ( list[i]->item != list[j]->item ) {
				tmp = list[i]->item;
				list[i]->item = list[j]->item;
				list[j]->item = tmp;
				cls = list[i]->classname;
				list[i]->classname = list[j]->classname;
				list[j]->classname = cls;
				swaps++;
			}
		}
	}
	if ( layout == DSV_LAYOUT_ARMOR || layout == DSV_LAYOUT_BOTH ) {
		n = n2 = 0;
		for ( i = MAX_CLIENTS ; i < level.num_entities ; i++ ) {
			e = &g_entities[i];
			if ( DSM_GIsPendingItem( e, IT_ARMOR ) && n < (int)ARRAY_LEN( list ) ) {
				list[n++] = e;
			} else if ( DSM_GIsPendingItem( e, IT_HEALTH ) && n2 < (int)ARRAY_LEN( list2 ) ) {
				list2[n2++] = e;
			}
		}
		// each armor / mega trades its spot with a random health pickup
		for ( i = 0 ; i < n && n2 > 0 ; i++ ) {
			j = DSM_GRand( &seed ) % n2;
			VectorCopy( list[i]->s.origin, o );
			VectorCopy( list2[j]->s.origin, list[i]->s.origin );
			VectorCopy( o, list2[j]->s.origin );
			list2[j] = list2[--n2];		// each health spot once
			swaps++;
		}
	}

	if ( flags || added || dsmVariant ) {
		G_LogPrintf( "DSMaps: variant %i (%s), %i items moved, %i flags removed, %i deathmatch spawns + %i from team spawns\n",
			dsmVariant, BG_DSM_VariantLabel( dsmVariant ), swaps, flags, dm, added );
	}
}

// G_RL_UpdateConfigstring: \mv\<variant> for the cgame's lighting
void G_DSM_Configstring( char *buf, int size ) {
	// (G_RL_Init sets CS_DESCENT before the map's entities spawn: read the run key here)
	trap_Cvar_Register( &g_dsVariants, "g_dsVariants", "1", CVAR_ARCHIVE );
	dsmVariant = ( G_RL_Active() && g_dsVariants.integer ) ? atoi( G_RL_RunGet( "node_variant" ) ) : 0;
	Q_strcat( buf, size, va( "\\mv\\%i", dsmVariant ) );
}
