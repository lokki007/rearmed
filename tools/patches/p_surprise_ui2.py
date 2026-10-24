"""ds-surprise: floor map hooks in ui_roguelike.c + declarations in ui_ds_meta.h (one-shot, skips present edits)."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from patch import sub

U = pathlib.Path(__file__).resolve().parent.parent.parent / 'ioq3' / 'code' / 'q3_ui'


def edit(path, old, new, marker):
    s = open(path, newline='').read().replace('\r\n', '\n')
    if marker in s:
        print('skip (present):', path.name, marker)
        return
    sub(str(path), old, new)
    print('ok:', path.name, marker)


edit(U / 'ui_ds_meta.h', '''void UI_DS_SmallButton( rlButton_t *b );		// compact ownerdraw (ABANDON / RESET)
''', '''void UI_DS_SmallButton( rlButton_t *b );		// compact ownerdraw (ABANDON / RESET)

/*
-------------------------------------------------------------------------------
ui_ds_surprise.c (ds-surprise): pick-your-poison floor modifiers on the floor map
-------------------------------------------------------------------------------
*/
int UI_DS_NodeMod( int seed, int L, int i, int type );	// the DSMOD_ node i of layer L offers (0 none)
qboolean UI_DS_NodeModOn( int L, int i );				// the player took it (run key mod_on)
void UI_DS_SurpriseLaunch( int seed, int L, int i, int type, int bots );	// RL_LaunchNode: node_mod, SWARM bots, sv_maxclients
void UI_DS_SurpriseMapReset( void );					// RL_MapMenu, before the chips
void UI_DS_SurpriseAddChip( menuframework_s *menu, int seed, int k, int L, int i, int type, float x, float y );	// the chip under a reachable node
qboolean UI_DS_SurpriseChipNode( int id, int *L, int *i );	// RL_PanelNode: a chip stands for its node
float UI_DS_SurprisePanel( int seed, int L, int i, int type, float x, float y, float w, qboolean reachable );	// RL_DrawInfoPanel
qboolean UI_DS_SurpriseCommand( const char *cmd, int seed, int layer, const int *reach, int numReach, const int *types );	// dev: dsmodnode
''', 'UI_DS_NodeMod')

R = U / 'ui_roguelike.c'
edit(R, '''	item = (menucommon_s *)Menu_ItemAtCursor( &s_rlMap.menu );
	if ( item && item->id >= ID_RL_ITEM0 && item->id < ID_RL_ITEM0 + rl.numReach ) {
		*outL = rl.layer;
		*outI = rl.reach[item->id - ID_RL_ITEM0];
		return qtrue;
	}
	return qfalse;
}''', '''	item = (menucommon_s *)Menu_ItemAtCursor( &s_rlMap.menu );
	if ( item && item->id >= ID_RL_ITEM0 && item->id < ID_RL_ITEM0 + rl.numReach ) {
		*outL = rl.layer;
		*outI = rl.reach[item->id - ID_RL_ITEM0];
		return qtrue;
	}
	if ( item && UI_DS_SurpriseChipNode( item->id, outL, outI ) ) {
		return qtrue;		// ds-surprise: a modifier chip stands for its node
	}
	return qfalse;
}''', 'UI_DS_SurpriseChipNode( item->id')
edit(R, '''	RL_Text( x + 8, y, "REWARD", 5, 8, rl_colOrange, 0 );
	y = RL_TextWrap( x + 44, y, w - 52, rl_nodeRewards[type], 5, 8, rl_colBone, 2 );
''', '''	RL_Text( x + 8, y, "REWARD", 5, 8, rl_colOrange, 0 );
	y = RL_TextWrap( x + 44, y, w - 52, rl_nodeRewards[type], 5, 8, rl_colBone, 2 );
	y = UI_DS_SurprisePanel( rl.seed, L, i, type, x, y, w, RL_Reachable( L, i ) );		// ds-surprise: the floor modifier on offer
''', 'UI_DS_SurprisePanel( rl.seed')
edit(R, '''	if ( current ) {
		RL_TextS( x + sz / 2 + 4, y - 4, "YOU", 5, 8, rl_colOrange, 0 );	// beside: the paths run above and below
	}
}''', '''	if ( current ) {
		RL_TextS( x + sz / 2 + 4, y - 4, "YOU", 5, 8, rl_colOrange, 0 );	// beside: the paths run above and below
	}
	// ds-surprise: a floor further down that offers a modifier: a small red mark (the chip shows when it's reachable)
	if ( L > rl.layer && UI_DS_NodeMod( rl.seed, L, i, type ) ) {
		vec4_t	m = { 1.0f, 0.28f, 0.2f, 1.0f };

		m[3] = k;
		UI_FillRect( x + sz / 2 - 3, y - sz / 2 - 3, 6, 6, m );
	}
}''', 'ds-surprise: a floor further down that offers')
edit(R, '''	RL_InitButton( &s_rlMap.quit, ID_RL_BACK, 8 - rl_ext, 446, 152, 26, s_rlMap.ingame ? "SAVE & QUIT" : "BACK", RL_MapEvent );''', '''	// ds-surprise: pick-your-poison chips under the reachable nodes that offer a floor modifier
	UI_DS_SurpriseMapReset();
	for ( k = 0; k < rl.numReach; k++ ) {
		i = rl.reach[k];
		RL_NodePos( rl.layer, i, &x, &y );
		UI_DS_SurpriseAddChip( &s_rlMap.menu, rl.seed, k, rl.layer, i, rl_layers[rl.layer].type[i], x, y + RL_NodeSize( rl.layer, i ) * 0.5f + 2 );
	}

	RL_InitButton( &s_rlMap.quit, ID_RL_BACK, 8 - rl_ext, 446, 152, 26, s_rlMap.ingame ? "SAVE & QUIT" : "BACK", RL_MapEvent );''', 'UI_DS_SurpriseMapReset();')
edit(R, '''	UI_DS_EconLaunch();		// ds-economy: diff_min / diff_max (records go to the easiest difficulty played)
''', '''	UI_DS_EconLaunch();		// ds-economy: diff_min / diff_max (records go to the easiest difficulty played)
	UI_DS_SurpriseLaunch( rl.seed, L, i, ly->type[i], ly->bots[i] );		// ds-surprise: the floor modifier taken (node_mod), SWARM's bots
''', 'UI_DS_SurpriseLaunch(')
edit(R, '''	// dev: pick the n-th reachable node (0-based) and launch it
	if ( !Q_stricmp( cmd, "rlpicknode" ) ) {''', '''	// ds-surprise dev: dsmods (the floor modifiers on offer), dsmodnode <reach index> [on|off]
	if ( !Q_stricmp( cmd, "dsmods" ) || !Q_stricmp( cmd, "dsmodnode" ) ) {
		int		types[RL_MAX_NODES];
		int		L;

		RL_LoadRun();
		if ( !rl.active ) {
			Com_Printf( "%s: no descent in progress\\n", cmd );
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
			Com_Printf( "\\n" );
		}
		return qtrue;
	}

	// dev: pick the n-th reachable node (0-based) and launch it
	if ( !Q_stricmp( cmd, "rlpicknode" ) ) {''', 'cmd, "dsmods"')
print('done')
