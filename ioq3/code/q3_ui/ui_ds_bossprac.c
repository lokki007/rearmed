/*
===========================================================================
REARMED -- DESCENT BOSS PRACTICE (round 4, en-boss), UI side.

Straight into a TIMED 1v1 boss fight, without a descent: DESCENT > BOSS PRACTICE (the hub's
bottom row) or the console command "dsbossprac <1-4>" ("dsbossprac" alone opens the picker).
Each boss comes with a typical kit for a player who reaches it (the cards, passives, weapon
levels and upgrades tools/boss_sim.py uses), difficulty Hurt Me Plenty, g_dsBossTime on the clock.

Nothing of the player's is touched: the floor is described in rearmed_practice.txt (this file
only ever writes that one), the game reads it when g_dsPractice is set and then writes no file
(run, meta, tally, skills, ghost; the cgame no records). No skulls, no records. After the fight
the game runs "dspracend <result> <msec left> <bar %>" and the picker comes back with the result.
g_dsPractice is cleared when the main menu opens and when a real floor launches.
===========================================================================
*/
#include "ui_local.h"
#include "ui_ds_meta.h"

#define DSP_FILE		"rearmed_practice.txt"
#define DSP_BOSSES		4
#define ID_DSP_BOSS0	200
#define ID_DSP_REMATCH	210		// en-rematch

typedef struct {
	int			floor;				// the floor cleared before the boss
	const char	*cards, *pv, *wl;
	int			wused;
	const char	*maps[4];
	const char	*skill;
} dspKit_t;

// a typical kit for the floor before each boss (difficulty 3), = tools/boss_sim.py KITS
static const dspKit_t dsp_kits[DSP_BOSSES] = {
	{ 5, "0,3,14", "2,1,1,0,0,0,0,0,0,0,0,0,0", "0,2,3,3,1,3,2,2,2,0,0,0,0,0",
		( 1 << 2 ) | ( 1 << 3 ) | ( 1 << 5 ) | ( 1 << 6 ) | ( 1 << 7 ) | ( 1 << 8 ), { "q3dm6", "q3dm7", "q3dm3", "q3dm5" }, "3.00" },
	{ 11, "0,3,14,7,4,12", "3,2,2,1,1,0,0,0,0,0,0,0,0", "0,3,4,5,3,5,4,4,4,0,0,2,0,2",
		0x29fc, { "q3dm8", "q3dm9", "q3dm10", "q3dm12" }, "3.60" },
	{ 17, "0,3,14,7,4,12,1,8,15", "4,3,3,2,2,1,0,0,0,0,0,0,0", "0,4,6,6,4,7,5,6,6,2,0,3,0,3",
		0x2bfc, { "q3dm16", "q3dm18", "q3dm19", "q3dm17" }, "4.30" },
	{ 23, "0,3,14,7,4,12,1,8,15,4,17", "5,4,4,3,3,2,0,0,0,0,0,0,0", "0,6,8,7,5,8,7,8,7,3,0,4,0,4",
		0x2bfc, { "q3dm13", "q3dm14", "q3dm15", "q3dm13" }, "5.00" }
};

static const char *dsp_actNames[DSP_BOSSES] = { "ACT I", "ACT II", "ACT III", "ACT IV" };

typedef struct {
	menuframework_s	menu;
	rlButton_t		boss[DSP_BOSSES];
	rlButton_t		back;
	rlButton_t		rematch;		// en-rematch: REMATCH (beaten bosses, harder modifiers, best times)
} dsPrac_t;

static dsPrac_t	s_dsPrac;
static int		dsp_lastBoss, dsp_lastResult, dsp_lastLeft, dsp_lastBar;		// the last practice fight (this session)

/*
=================
DSP_Launch

Writes rearmed_practice.txt (only that file) and starts the boss floor
=================
*/
static void DSP_LaunchEx( int boss, const char *extra ) {		// (en-rematch: extra practice-file keys)
	const dspKit_t	*k;
	fileHandle_t	f;
	char			buf[2048], mapKey[128];
	const char		*mp;
	int				i, cur, seed;

	if ( boss < 1 || boss > DSP_BOSSES ) {
		Com_Printf( "dsbossprac: boss 1-4\n" );
		return;
	}
	k = &dsp_kits[boss - 1];
	seed = 1000 + ( uis.realtime & 0xffff );
	mp = k->maps[( uis.realtime / 7 ) & 3];
	mapKey[0] = 0;
	for ( i = 0; i < k->floor && i < 60; i++ ) {
		Q_strcat( mapKey, sizeof( mapKey ), i ? ".0" : "0" );
	}
	Com_sprintf( buf, sizeof( buf ),
		"version 2\nactive 1\npractice %i\nseed %i\ndifficulty 3\nlives 9\nfloor %i\nskulls 0\ncards %s\nkills 0\ntime 0\n"
		"result none\noffer\nmap %s\nrerolls 0\nup_health 3\nup_speed 1\nup_damage 2\nup_armor 0\nup_skulls 0\nup_cards 0\n"
		"character 0\nstartcards 0\npv %s\nwl %s\nwused %i\nact %i\ninfloor 1\nnode_type boss\nnode_map %s\nnode_bots 1\n"
		"node_frags 16\nnode_mutator 0\nnode_skill %s\nnode_tier %i\nnode_floor %i\nnode_layer %i\nnode_index 0\n",
		boss, seed, k->floor, k->cards, mapKey, k->pv, k->wl, k->wused, boss, mp, k->skill,
		( k->floor + 1 ) / 8 > 2 ? 2 : ( k->floor + 1 ) / 8, k->floor + 1, k->floor );
	if ( extra && extra[0] ) {
		Q_strcat( buf, sizeof( buf ), extra );		// en-rematch: rematch 1, rm_mods, rm_prev
	}
	trap_FS_FOpenFile( DSP_FILE, &f, FS_WRITE );
	if ( !f ) {
		Com_Printf( S_COLOR_RED "dsbossprac: can't write %s\n", DSP_FILE );
		return;
	}
	trap_FS_Write( buf, strlen( buf ), f );
	trap_FS_FCloseFile( f );

	RL_RegisterCvars();
	cur = (int)trap_Cvar_VariableValue( "g_rearmMode" );
	if ( cur != RM_MODE_ROGUELIKE ) {
		trap_Cvar_SetValue( "ui_rlPrevMode", cur );		// (UI_RL_RestoreMode puts it back)
	}
	trap_Cvar_SetValue( "g_rearmMode", RM_MODE_ROGUELIKE );
	trap_Cvar_SetValue( "g_dsPractice", boss );
	UI_DS_DailySet( qfalse );		// en-review: a practice is never a daily floor (`dsbossprac` typed with ds_daily still set)
	trap_Cvar_SetValue( "g_gametype", GT_FFA );
	trap_Cvar_SetValue( "fraglimit", 16 );
	trap_Cvar_SetValue( "timelimit", 0 );
	trap_Cvar_SetValue( "ui_singlePlayerActive", 0 );
	if ( trap_Cvar_VariableValue( "sv_maxclients" ) < 8 ) {
		trap_Cvar_SetValue( "sv_maxclients", 8 );
	}
	dsp_lastBoss = boss;
	dsp_lastResult = 0;
	Com_Printf( "DESCENT: boss practice %i (%s) on %s, floor %i kit, nothing is saved\n", boss, ds_bosses[boss - 1].name, mp, k->floor + 1 );
	UI_ForceMenuOff();
	trap_Cmd_ExecuteText( EXEC_INSERT, va( "%s %s\n", trap_Cvar_VariableValue( "ui_rlDevmap" ) ? "devmap" : "map", mp ) );
}

static void DSP_Launch( int boss ) {
	DSP_LaunchEx( boss, NULL );
}

// en-rematch: the same practice floor with extra keys (ui_ds_rematch.c)
void UI_DS_BossPracLaunch( int boss, const char *extra ) {
	DSP_LaunchEx( boss, extra );
}

static void DSP_Event( void *ptr, int event ) {
	int		id;

	if ( event != QM_ACTIVATED ) {
		return;
	}
	id = ( (menucommon_s *)ptr )->id;
	if ( id >= ID_DSP_BOSS0 && id < ID_DSP_BOSS0 + DSP_BOSSES ) {
		DSP_Launch( id - ID_DSP_BOSS0 + 1 );
		return;
	}
	if ( id == ID_DSP_REMATCH ) {
		UI_DS_RematchMenu( -1 );		// en-rematch
		return;
	}
	if ( id == ID_RL_BACK ) {
		if ( RL_InGame() ) {
			// after a practice fight: leave the server, the main menu (and the hub) come back
			trap_Cvar_Set( "ui_rlOpenHub", "1" );
			UI_ForceMenuOff();
			trap_Cmd_ExecuteText( EXEC_APPEND, "disconnect\n" );
		} else {
			UI_PopMenu();
		}
	}
}

static void DSP_Draw( void ) {
	const char	*s;
	int			left;

	RL_Backdrop( !RL_InGame() );
	RL_Header( "BOSS PRACTICE", "A timed 1v1 against an act boss, with a typical kit for that depth. Nothing is saved." );
	Menu_Draw( &s_dsPrac.menu );
	if ( dsp_lastBoss >= 1 && dsp_lastBoss <= DSP_BOSSES && dsp_lastResult ) {
		left = ( dsp_lastLeft + 999 ) / 1000;
		if ( dsp_lastResult == 1 ) {
			s = va( "%s SLAIN  -  %i:%02i LEFT ON THE CLOCK", ds_bosses[dsp_lastBoss - 1].name, left / 60, left % 60 );
		} else {
			s = va( "TIME UP  -  %s HAD %i%% LEFT", ds_bosses[dsp_lastBoss - 1].name, 100 - dsp_lastBar );
		}
		RL_TextS( 320, 330, s, 8, 12, dsp_lastResult == 1 ? rl_colGood : rl_colBad, 1 );
	}
	RL_Text( 320, 368, "Kill it before the clock runs out. Die as often as you like: you come back far from it.", 6, 10, rl_colDim, 1 );
	RL_Text( 320, 381, "No skulls, no records, your descent and progress stay untouched.", 6, 10, rl_colDim, 1 );
	RL_Footer( "MOUSE, SPACE / TAB, WASD OR GAMEPAD" );
}

/*
=================
UI_DS_BossPracMenu
=================
*/
void UI_DS_BossPracMenu( void ) {
	float	x, w = 360, y = 80;
	int		i, t;

	memset( &s_dsPrac, 0, sizeof( s_dsPrac ) );
	RL_SetupMenu( &s_dsPrac.menu, DSP_Draw, RL_InGame() );
	x = 320 - w / 2;
	t = (int)trap_Cvar_VariableValue( "g_dsBossTime" );
	if ( t < 60 ) {
		t = 300;
	}
	for ( i = 0; i < DSP_BOSSES; i++ ) {
		RL_InitButton( &s_dsPrac.boss[i], ID_DSP_BOSS0 + i, x, y, w, 44, ds_bosses[i].name, DSP_Event );
		Com_sprintf( s_dsPrac.boss[i].sub, sizeof( s_dsPrac.boss[i].sub ), "%s  -  floor %i kit  -  %i:%02i on the clock%s",
			dsp_actNames[i], dsp_kits[i].floor + 1, t / 60, t % 60, i == dsp_lastBoss - 1 ? "  -  AGAIN" : "" );
		Menu_AddItem( &s_dsPrac.menu, &s_dsPrac.boss[i] );
		y += 54;
	}
	RL_InitButton( &s_dsPrac.back, ID_RL_BACK, 8 - rl_ext, 440, 120, 28, RL_InGame() ? "LEAVE" : "BACK", DSP_Event );
	s_dsPrac.back.center = qtrue;
	Menu_AddItem( &s_dsPrac.menu, &s_dsPrac.back );
	RL_InitButton( &s_dsPrac.rematch, ID_DSP_REMATCH, 632 + rl_ext - 170, 440, 170, 28, "REMATCH", DSP_Event );	// en-rematch
	s_dsPrac.rematch.center = qtrue;
	Menu_AddItem( &s_dsPrac.menu, &s_dsPrac.rematch );
	if ( dsp_lastBoss >= 1 && dsp_lastBoss <= DSP_BOSSES ) {
		Menu_SetCursorToItem( &s_dsPrac.menu, &s_dsPrac.boss[dsp_lastBoss - 1] );
	}
	if ( RL_InGame() ) {
		uis.menusp = 0;
	}
	UI_PushMenu( &s_dsPrac.menu );
}

/*
=================
UI_DS_BossPracCommand

dsbossprac [1-4] (no number: the picker); dspracend <result> <msec left> <bar % removed> (the game,
after a practice fight); dspracoff (clears g_dsPractice: the main menu / a real floor)
=================
*/
qboolean UI_DS_BossPracCommand( const char *cmd ) {
	if ( UI_DS_RematchCommand( cmd ) ) {
		return qtrue;		// en-rematch: dsrematch, dsrmend, dsrminfo
	}
	if ( !Q_stricmp( cmd, "dsbossprac" ) ) {
		if ( UI_Argv( 1 )[0] ) {
			DSP_Launch( atoi( UI_Argv( 1 ) ) );
		} else {
			UI_DS_BossPracMenu();
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dspracend" ) ) {
		dsp_lastResult = atoi( UI_Argv( 1 ) );
		dsp_lastLeft = atoi( UI_Argv( 2 ) );
		dsp_lastBar = atoi( UI_Argv( 3 ) );
		if ( !dsp_lastBoss ) {
			dsp_lastBoss = (int)trap_Cvar_VariableValue( "g_dsPractice" );
		}
		Com_Printf( "DESCENT: boss practice over: %s, %i ms left, %i%% of the bar taken\n", dsp_lastResult == 1 ? "WIN" : "LOSS",
			dsp_lastLeft, dsp_lastBar );
		UI_DS_BossPracMenu();
		return qtrue;
	}
	return qfalse;
}

// the main menu opened / a real floor launches: no practice any more
void UI_DS_BossPracOff( void ) {
	if ( trap_Cvar_VariableValue( "g_dsPractice" ) ) {
		trap_Cvar_Set( "g_dsPractice", "0" );
	}
}
