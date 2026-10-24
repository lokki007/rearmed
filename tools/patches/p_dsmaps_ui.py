"""ds-maps: hooks in q3_ui/ui_roguelike.c (one-shot; resume-safe via tools/patch.py style checks)."""
import pathlib
p = pathlib.Path(__file__).resolve().parents[2] / 'ioq3/code/q3_ui/ui_roguelike.c'
s = p.read_bytes().decode('latin-1')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def sub(old, new, count=1):
	global s
	if new in s and old not in s:
		print('already:', new.strip().splitlines()[0][:70])
		return
	assert s.count(old) == count, (s.count(old), old)
	s = s.replace(old, new)

# 1. the layer type + defines move to ui_ds_maps.h
sub('''#define RL_MAX_MAPS			64
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
''', '''#include "ui_ds_maps.h"		// ds-maps: RL_MAX_*, RLN_* and rlLayer_t, the map variety (ui_ds_maps.c)
''')
sub('''typedef struct {
	int		n;
	int		type[RL_MAX_NODES];
	int		map[RL_MAX_NODES];
	int		bots[RL_MAX_NODES];
	int		frags[RL_MAX_NODES];
	int		mut[RL_MAX_NODES];
	int		jitter[RL_MAX_NODES];
	int		edges[RL_MAX_NODES];	// bit j: a path to node j of the next layer
} rlLayer_t;

''', '''// rlLayer_t: ui_ds_maps.h

''')

# 2. cvars
sub('''	UI_DS_EconRegisterCvars();		// ds-economy: g_fragMult
''', '''	UI_DS_EconRegisterCvars();		// ds-economy: g_fragMult
	UI_DSM_RegisterCvars();			// ds-maps: g_dsVariants
''')

# 3. map allowed: the DESCENT map table, bots need the .aas
sub('''static qboolean RL_MapAllowed( const char *name ) {
	const char	*info;
	int			n;

''', '''static qboolean RL_MapAllowed( const char *name ) {
	const char	*info;
	int			n;

	if ( !UI_DSM_HasAAS( name ) ) {
		return qfalse;		// ds-maps: no .aas, the bots can't move
	}
	if ( BG_DSM_MapId( name ) >= 0 ) {
		return qtrue;		// ds-maps: the DESCENT map table (bg_ds_maps.c), CTF / tourney / Team Arena included
	}
''')

# 4. pool handed to ds-maps
sub('''	RL_BuildActPools();
}
''', '''	RL_BuildActPools();
	UI_DSM_SetPool( rl_maps, rl_numMaps, rl_actMaps, rl_actNumMaps );		// ds-maps
}
''')

# 5. long names of maps without an arena file (Team Arena copies)
sub('''		if ( s[0] ) {
			return s;
		}
	}
	return rl_maps[m];
}
''', '''		if ( s[0] ) {
			return s;
		}
	}
	s = UI_DSM_LongName( rl_maps[m] );		// ds-maps
	if ( s ) {
		return s;
	}
	return rl_maps[m];
}
''')

# 6. assignment after every build (cached too: the run moves on under the same seed)
sub('''	RL_BuildMapPool();
	if ( seed == rl_layersSeed && count <= rl_layersBuilt ) {
		return;
	}
''', '''	RL_BuildMapPool();
	if ( seed == rl_layersSeed && count <= rl_layersBuilt ) {
		UI_DSM_Assign( rl_layers, rl_layersBuilt, seed );		// ds-maps: no map twice in a run
		return;
	}
''')
sub('''			ly->edges[i] |= ( 1 << j );
		}
	}
}
''', '''			ly->edges[i] |= ( 1 << j );
		}
	}

	UI_DSM_Assign( rl_layers, count, seed );		// ds-maps: the window's maps (stored in the run), variants
}

// ds-maps: the simulation builds the floors of other seeds
rlLayer_t *RL_DSM_Layers( int seed, int count ) {
	RL_BuildLayers( seed, count );
	return rl_layers;
}
''')

# 7. launch: played list, lifetime counts, node_variant
sub('''	f = L + 1;

	// the picked node on every floor so far, then this one
''', '''	f = L + 1;
	UI_DSM_Launch( L, i, rl_maps[ly->map[i]], ly->variant[i] );		// ds-maps: played this run, lifetime counts, node_variant

	// the picked node on every floor so far, then this one
''')

# 8. node mark (NEW / REVISITED) on the map
sub('''	if ( current ) {
		RL_TextS( x + sz / 2 + 4, y - 4, "YOU", 5, 8, rl_colOrange, 0 );	// beside: the paths run above and below
	}
''', '''	if ( current ) {
		RL_TextS( x + sz / 2 + 4, y - 4, "YOU", 5, 8, rl_colOrange, 0 );	// beside: the paths run above and below
	}
	if ( type != RLN_VAULT ) {
		UI_DSM_DrawNodeTag( x, y, sz, rl_maps[ly->map[i]], ly->mflags[i], k, L >= rl.layer );		// ds-maps: NEW / REVISITED
	}
''')

# 9. info panel: the map tag line (name, variant, NEW)
sub('''	y = RL_TextWrap( x + 8, y, w - 16, RL_MapLongName( ly->map[i] ), 7, 11, rl_colBone, 2 );
	RL_Text( x + 8, y, rl_maps[ly->map[i]], 5, 8, rl_colDark, 0 );
	y += 14;
''', '''	y = RL_TextWrap( x + 8, y, w - 16, RL_MapLongName( ly->map[i] ), 7, 11, rl_colBone, 2 );
	y = UI_DSM_DrawMapLine( x + 8, y, w - 16, rl_maps[ly->map[i]], type == RLN_VAULT ? 0 : ly->variant[i], ly->mflags[i], L < rl.layer );		// ds-maps
''')

# 10. dev: dsboss / dsvault map override survives the reassignment
sub('''				rl_layers[rl.layer].map[rl.reach[0]] = i;
				break;
''', '''				rl_layers[rl.layer].map[rl.reach[0]] = i;
				UI_DSM_DevForce( rl.layer, rl.reach[0], i );		// ds-maps
				break;
''')

# 11. console commands
sub('''	if ( UI_DS_EconCommand( cmd ) ) {		// ds-economy dev: dsdiff dsdiffmenu dsreset dsabandon
		return qtrue;
	}
''', '''	if ( UI_DS_EconCommand( cmd ) ) {		// ds-economy dev: dsdiff dsdiffmenu dsreset dsabandon
		return qtrue;
	}
	if ( UI_DSM_ConsoleCommand( cmd ) ) {		// ds-maps dev: dsmaps, dsmapsim
		return qtrue;
	}
''')

if crlf:
	s = s.replace('\n', '\r\n')
p.write_bytes(s.encode('latin-1'))
print('ok')
