/*
===========================================================================
REARMED -- DESCENT BOSS REMATCH (round 4, en-rematch), UI side.

DESCENT > PRACTICE / REMATCH > REMATCH, or the console command "dsrematch [<boss 1-4> [mask]]".
Only bosses the player has beaten in a real descent (meta cnt_boss<n> > 0) can be rematched; the
others show how to unlock them. Pick a boss, stack HARDER modifiers (game/bg_ds_rematch.h), each
adds heat, then FIGHT: a BOSS PRACTICE floor (ui_ds_bossprac.c writes rearmed_practice.txt with
"rematch 1 / rm_mods / rm_prev"), so the run, meta, history, records, skills and skulls stay
untouched. The game answers "dsrmend <boss> <mask> <result> <kill ms> <bar %> <deaths>" and this
screen comes back with the result: the fastest kill per boss and heat lives in rearmed_rematch.txt
(this file is its only writer; Reset All Progress empties it):
	version 1
	last <boss 0-3> <mask>
	best <boss 0-3> <heat> <kill msec> <mask>
	stats <boss 0-3> <tries> <wins>
Dev: dsrminfo (prints the table), dsrmend by hand.
===========================================================================
*/
#include "ui_local.h"
#include "ui_ds_meta.h"
#include "../game/bg_ds_rematch.h"

#define DSR_BOSSES		4
#define DSR_HEATS		( DSRM_MAX_HEAT + 1 )
#define ID_DSR_BOSS0	300
#define ID_DSR_MOD0		310
#define ID_DSR_FIGHT	320
#define ID_DSR_PRAC		321

typedef struct {
	int		msec, mask;
} dsrBest_t;

typedef struct {
	menuframework_s	menu;
	rlButton_t		boss[DSR_BOSSES];
	rlButton_t		mod[DSRM_NUM];
	rlButton_t		fight;
	rlButton_t		prac;
	rlButton_t		back;
} dsRematch_t;

static dsRematch_t	s_dsr;

// the file
static dsrBest_t	dsr_best[DSR_BOSSES][DSR_HEATS];
static int			dsr_tries[DSR_BOSSES], dsr_wins[DSR_BOSSES];
static int			dsr_boss, dsr_mask;
// meta: beaten in a real descent
static int			dsr_beaten[DSR_BOSSES];
// the last rematch's result (this session)
static int			dsr_res, dsr_resBoss, dsr_resMask, dsr_resKill, dsr_resBar, dsr_resDeaths, dsr_resPrev, dsr_resNew;

static vec4_t		dsr_colGold = { 1.0f, 0.78f, 0.25f, 1.0f };
static vec4_t		dsr_colHeat = { 1.0f, 0.45f, 0.12f, 1.0f };

static const char *dsr_actNames[DSR_BOSSES] = { "ACT I", "ACT II", "ACT III", "ACT IV" };

/*
=======================================================================

THE FILE

=======================================================================
*/

static char	dsr_buf[8192];

static void DSR_Load( void ) {
	fileHandle_t	f;
	int				len, b, h, v[4], i;
	char			*p, *tok, *line, *rest, kind[16];

	memset( dsr_best, 0, sizeof( dsr_best ) );
	memset( dsr_tries, 0, sizeof( dsr_tries ) );
	memset( dsr_wins, 0, sizeof( dsr_wins ) );
	dsr_boss = -1;
	dsr_mask = 0;
	len = trap_FS_FOpenFile( DSRM_FILE, &f, FS_READ );
	if ( !f ) {
		return;
	}
	if ( len > (int)sizeof( dsr_buf ) - 1 ) {
		len = sizeof( dsr_buf ) - 1;
	}
	if ( len > 0 ) {
		trap_FS_Read( dsr_buf, len, f );
	}
	trap_FS_FCloseFile( f );
	dsr_buf[len > 0 ? len : 0] = 0;
	// one record a line: split at the newlines first (COM_ParseExt without line breaks steps past
	// the newline of a short line and would eat the next record)
	line = dsr_buf;
	while ( *line ) {
		p = line;
		while ( *p && *p != '\n' ) {
			p++;
		}
		if ( *p ) {
			*p++ = 0;
		}
		rest = line;
		line = p;
		tok = COM_ParseExt( &rest, qfalse );
		if ( !tok[0] ) {
			continue;
		}
		Q_strncpyz( kind, tok, sizeof( kind ) );
		for ( i = 0 ; i < 4 ; i++ ) {
			v[i] = atoi( COM_ParseExt( &rest, qfalse ) );
		}
		b = v[0];
		if ( !Q_stricmp( kind, "last" ) && b >= 0 && b < DSR_BOSSES ) {
			dsr_boss = b;
			dsr_mask = v[1] & DSRM_ALL;
		} else if ( !Q_stricmp( kind, "best" ) && b >= 0 && b < DSR_BOSSES ) {
			h = v[1];
			if ( h >= 0 && h < DSR_HEATS && v[2] > 0 ) {
				dsr_best[b][h].msec = v[2];
				dsr_best[b][h].mask = v[3] & DSRM_ALL;
			}
		} else if ( !Q_stricmp( kind, "stats" ) && b >= 0 && b < DSR_BOSSES ) {
			dsr_tries[b] = v[1];
			dsr_wins[b] = v[2];
		}
	}
}

static void DSR_Save( void ) {
	fileHandle_t	f;
	int				b, h;

	dsr_buf[0] = 0;
	Q_strcat( dsr_buf, sizeof( dsr_buf ), "version 1\n" );
	Q_strcat( dsr_buf, sizeof( dsr_buf ), va( "last %i %i\n", dsr_boss < 0 ? 0 : dsr_boss, dsr_mask ) );
	for ( b = 0 ; b < DSR_BOSSES ; b++ ) {
		if ( dsr_tries[b] || dsr_wins[b] ) {
			Q_strcat( dsr_buf, sizeof( dsr_buf ), va( "stats %i %i %i\n", b, dsr_tries[b], dsr_wins[b] ) );
		}
		for ( h = 0 ; h < DSR_HEATS ; h++ ) {
			if ( dsr_best[b][h].msec > 0 ) {
				Q_strcat( dsr_buf, sizeof( dsr_buf ), va( "best %i %i %i %i\n", b, h, dsr_best[b][h].msec, dsr_best[b][h].mask ) );
			}
		}
	}
	trap_FS_FOpenFile( DSRM_FILE, &f, FS_WRITE );
	if ( !f ) {
		Com_Printf( S_COLOR_RED "dsrematch: can't write %s\n", DSRM_FILE );
		return;
	}
	trap_FS_Write( dsr_buf, strlen( dsr_buf ), f );
	trap_FS_FCloseFile( f );
}

// meta: which bosses fell in a real descent (cnt_boss<n>, the true death of an act or Abyss boss)
static void DSR_LoadBeaten( void ) {
	int		b;

	RL_LoadMeta();
	for ( b = 0 ; b < DSR_BOSSES ; b++ ) {
		dsr_beaten[b] = RL_KVInt( &rl_meta, va( "cnt_boss%i", b ) );
	}
}

static int DSR_FirstBeaten( void ) {
	int		b;

	for ( b = 0 ; b < DSR_BOSSES ; b++ ) {
		if ( dsr_beaten[b] > 0 ) {
			return b;
		}
	}
	return -1;
}

// m:ss.t (two rotating buffers)
static const char *DSR_Time( int msec ) {
	static char	buf[4][16];
	static int	idx;
	char		*s = buf[idx++ & 3];

	if ( msec < 0 ) {
		msec = 0;
	}
	Com_sprintf( s, sizeof( buf[0] ), "%i:%02i.%i", msec / 60000, ( msec / 1000 ) % 60, ( msec % 1000 ) / 100 );
	return s;
}

static void DSR_ModNames( int mask, char *out, int size ) {
	int		i;

	out[0] = 0;
	for ( i = 0 ; i < DSRM_NUM ; i++ ) {
		if ( mask & dsrm_mods[i].bit ) {
			if ( out[0] ) {
				Q_strcat( out, size, "  " );
			}
			Q_strcat( out, size, dsrm_mods[i].name );
		}
	}
	if ( !out[0] ) {
		Q_strncpyz( out, "NO MODIFIERS", size );
	}
}

// the best kill of a boss at any heat >= h (0 none), and its heat
static int DSR_HighestHeat( int b ) {
	int		h;

	for ( h = DSR_HEATS - 1 ; h >= 0 ; h-- ) {
		if ( dsr_best[b][h].msec > 0 ) {
			return h;
		}
	}
	return -1;
}

/*
=======================================================================

LAUNCH

=======================================================================
*/

static void DSR_Launch( int b, int mask ) {
	int		heat;

	if ( b < 0 || b >= DSR_BOSSES ) {
		Com_Printf( "dsrematch: boss 1-4\n" );
		return;
	}
	if ( dsr_beaten[b] <= 0 ) {
		Com_Printf( "dsrematch: %s is locked - beat it in a real descent first (the %s boss, floor %i)\n", ds_bosses[b].name,
			dsr_actNames[b], ( b + 1 ) * DS_FLOORS_PER_ACT );
		return;
	}
	mask &= DSRM_ALL;
	heat = BG_DSRM_Heat( mask );
	dsr_boss = b;
	dsr_mask = mask;
	dsr_tries[b]++;
	DSR_Save();
	dsr_res = 0;
	Com_Printf( "DESCENT: rematch %i (%s) mods %i heat %i, best at this heat %s\n", b + 1, ds_bosses[b].name, mask, heat,
		dsr_best[b][heat].msec > 0 ? DSR_Time( dsr_best[b][heat].msec ) : "none" );
	UI_DS_BossPracLaunch( b + 1, va( "rematch 1\nrm_mods %i\nrm_prev %i\n", mask, dsr_best[b][heat].msec ) );
}

/*
=======================================================================

THE SCREEN

=======================================================================
*/

static void DSR_Event( void *ptr, int event ) {
	int		id;

	if ( event != QM_ACTIVATED ) {
		return;
	}
	id = ( (menucommon_s *)ptr )->id;
	if ( id >= ID_DSR_BOSS0 && id < ID_DSR_BOSS0 + DSR_BOSSES ) {
		if ( dsr_beaten[id - ID_DSR_BOSS0] > 0 ) {
			dsr_boss = id - ID_DSR_BOSS0;
			s_dsr.fight.bm.generic.flags &= ~QMF_GRAYED;
			Menu_SetCursorToItem( &s_dsr.menu, &s_dsr.mod[0] );		// next: the modifiers
		}
		return;
	}
	if ( id >= ID_DSR_MOD0 && id < ID_DSR_MOD0 + DSRM_NUM ) {
		dsr_mask ^= dsrm_mods[id - ID_DSR_MOD0].bit;
		return;
	}
	switch ( id ) {
	case ID_DSR_FIGHT:
		DSR_Launch( dsr_boss, dsr_mask );
		break;
	case ID_DSR_PRAC:
		UI_DS_BossPracMenu();
		break;
	case ID_RL_BACK:
		if ( RL_InGame() ) {
			trap_Cvar_Set( "ui_rlOpenHub", "1" );
			UI_ForceMenuOff();
			trap_Cmd_ExecuteText( EXEC_APPEND, "disconnect\n" );
		} else {
			UI_PopMenu();
		}
		break;
	}
}

// a boss: name, act, beaten count + best; locked: how to unlock it
static void DSR_BossDraw( void *self ) {
	rlButton_t		*bt = (rlButton_t *)self;
	menucommon_s	*g = &bt->bm.generic;
	float			x = g->left, y = g->top, w = g->right - g->left, h = g->bottom - g->top;
	int				b = bt->data, hh;
	qboolean		focus = Menu_ItemAtCursor( g->parent ) == self, locked = dsr_beaten[b] <= 0, sel = b == dsr_boss;
	vec4_t			text;

	RL_Frame( x, y, w, h, focus ? rl_colPanelHi : rl_colPanel, sel ? dsr_colGold : focus ? rl_colOrange : rl_colLine );
	if ( sel ) {
		RL_Frame( x + 1, y + 1, w - 2, h - 2, NULL, dsr_colGold );
		UI_FillRect( x, y, 4, h, dsr_colGold );
	} else if ( focus ) {
		UI_FillRect( x, y, 3, h, rl_colOrange );
	}
	Vector4Copy( locked ? rl_colDim : rl_colBone, text );
	RL_Text( x + 12, y + 5, ds_bosses[b].name, 9, 14, text, 0 );
	if ( locked ) {
		RL_Text( x + w - 8, y + 7, "LOCKED", 6, 10, rl_colBad, 2 );
		RL_Text( x + 12, y + h - 13, va( "Beat it in a descent to unlock: %s boss, floor %i", dsr_actNames[b], ( b + 1 ) * DS_FLOORS_PER_ACT ),
			5, 9, rl_colDim, 0 );
		return;
	}
	RL_Text( x + w - 8, y + 7, sel ? "SELECTED" : dsr_actNames[b], 6, 10, sel ? dsr_colGold : rl_colDim, 2 );
	hh = DSR_HighestHeat( b );
	RL_Text( x + 12, y + h - 13, hh >= 0 ? va( "Beaten %ix  -  rematch wins %i  -  top heat %i in %s", dsr_beaten[b], dsr_wins[b], hh,
		DSR_Time( dsr_best[b][hh].msec ) ) : va( "Beaten %ix in a descent  -  no rematch kill yet", dsr_beaten[b] ), 5, 9, rl_colDim, 0 );
}

// a modifier: a check box, the name, the heat, one line of what it does
static void DSR_ModDraw( void *self ) {
	rlButton_t		*bt = (rlButton_t *)self;
	menucommon_s	*g = &bt->bm.generic;
	float			x = g->left, y = g->top, w = g->right - g->left, h = g->bottom - g->top;
	const dsRmMod_t	*m = &dsrm_mods[bt->data];
	qboolean		focus = Menu_ItemAtCursor( g->parent ) == self, on = ( dsr_mask & m->bit ) != 0;
	vec4_t			c;

	RL_Frame( x, y, w, h, focus ? rl_colPanelHi : rl_colPanel, focus ? rl_colOrange : on ? dsr_colHeat : rl_colLine );
	if ( focus ) {
		UI_FillRect( x, y, 3, h, rl_colOrange );
	}
	// the box
	RL_Frame( x + 9, y + ( h - 14 ) / 2, 14, 14, rl_colBlack, on ? dsr_colHeat : rl_colDim );
	if ( on ) {
		UI_FillRect( x + 12, y + ( h - 14 ) / 2 + 3, 8, 8, dsr_colHeat );
	}
	Vector4Copy( on ? rl_colBone : rl_colDim, c );
	RL_Text( x + 32, y + 4, m->name, 8, 12, c, 0 );
	RL_Text( x + w - 8, y + 5, va( "+%i HEAT", m->heat ), 6, 10, on ? dsr_colHeat : rl_colDim, 2 );
	RL_Text( x + 32, y + h - 11, m->desc, 5, 8, on ? rl_colBone : rl_colDim, 0 );
}

static void DSR_Draw( void ) {
	float		x, y, w;
	int			heat = BG_DSRM_Heat( dsr_mask ), h, n, b = dsr_boss, prev;
	char		names[128];
	const char	*s;
	vec4_t		c;

	RL_Backdrop( !RL_InGame() );
	RL_Header( "BOSS REMATCH", "Bosses you beat in a descent. Stack harder modifiers, beat your best times. Nothing else is saved." );
	Menu_Draw( &s_dsr.menu );

	// the heat
	x = 336 + rl_ext * 0.5f;
	w = 286;
	y = 318;
	RL_Frame( x, y, w, 30, rl_colPanel, heat > 0 ? dsr_colHeat : rl_colLine );
	RL_Text( x + 10, y + 8, "TOTAL HEAT", 8, 13, rl_colBone, 0 );
	RL_TextS( x + w - 10, y + 5, va( "%i / %i", heat, DSRM_MAX_HEAT ), 12, 19, heat > 0 ? dsr_colHeat : rl_colDim, 2 );

	// the best times of the selected boss, by heat
	x = 18 - rl_ext * 0.5f;
	w = 300;
	y = 262;
	RL_Frame( x, y, w, 136, rl_colPanel, rl_colLine );
	if ( b < 0 ) {
		RL_TextWrap( x + 10, y + 10, w - 20, DSR_FirstBeaten() < 0
			? "No boss beaten yet. Kill an act boss in a real descent and its rematch opens here."
			: "Pick a boss.", 6, 10, rl_colDim, 4 );
	} else {
		RL_Text( x + 10, y + 7, va( "BEST TIMES  -  %s", ds_bosses[b].name ), 7, 11, rl_colBone, 0 );
		prev = dsr_best[b][heat].msec;
		if ( prev > 0 ) {
			s = va( "AT HEAT %i:  %s", heat, DSR_Time( prev ) );
		} else {
			s = va( "AT HEAT %i:  NO KILL YET", heat );
		}
		RL_Text( x + 10, y + 21, s, 6, 10, prev > 0 ? dsr_colGold : rl_colDim, 0 );
		n = 0;
		for ( h = DSR_HEATS - 1 ; h >= 0 && n < 8 ; h-- ) {
			if ( dsr_best[b][h].msec <= 0 ) {
				continue;
			}
			Vector4Copy( h == heat ? dsr_colGold : rl_colBone, c );
			RL_Text( x + 10, y + 38 + n * 12, va( "HEAT %2i", h ), 6, 10, c, 0 );
			RL_Text( x + 62, y + 38 + n * 12, DSR_Time( dsr_best[b][h].msec ), 6, 10, c, 0 );
			DSR_ModNames( dsr_best[b][h].mask, names, sizeof( names ) );
			RL_Text( x + 112, y + 39 + n * 12, names, 4, 8, rl_colDim, 0 );
			n++;
		}
		if ( !n ) {
			RL_Text( x + 10, y + 40, "No rematch kills yet: any heat counts.", 6, 10, rl_colDim, 0 );
		}
		RL_Text( x + 10, y + 124, va( "tries %i  -  wins %i", dsr_tries[b], dsr_wins[b] ), 5, 8, rl_colDim, 0 );
	}

	// the last rematch's result: the record moment (on this end screen, never mid-fight)
	if ( dsr_res && dsr_resBoss >= 0 && dsr_resBoss < DSR_BOSSES ) {
		h = BG_DSRM_Heat( dsr_resMask );
		if ( dsr_res == 1 && dsr_resNew ) {
			Vector4Copy( dsr_colGold, c );
			c[3] = 0.75f + 0.25f * RL_Pulse( 0.008f );
			if ( dsr_resPrev > 0 ) {
				s = va( "NEW BEST  -  %s IN %s AT HEAT %i  (-%i.%i S)", ds_bosses[dsr_resBoss].name, DSR_Time( dsr_resKill ), h,
					( dsr_resPrev - dsr_resKill ) / 1000, ( ( dsr_resPrev - dsr_resKill ) % 1000 ) / 100 );
			} else {
				s = va( "NEW BEST  -  %s IN %s  -  FIRST KILL AT HEAT %i", ds_bosses[dsr_resBoss].name, DSR_Time( dsr_resKill ), h );
			}
			RL_TextS( 320, 408, s, 8, 13, c, 1 );
		} else if ( dsr_res == 1 ) {
			s = va( "%s SLAIN IN %s AT HEAT %i  -  +%i.%i S OFF YOUR BEST %s", ds_bosses[dsr_resBoss].name, DSR_Time( dsr_resKill ), h,
				( dsr_resKill - dsr_resPrev ) / 1000, ( ( dsr_resKill - dsr_resPrev ) % 1000 ) / 100, DSR_Time( dsr_resPrev ) );
			RL_TextS( 320, 408, s, 7, 12, rl_colGood, 1 );
		} else if ( ( dsr_resMask & DSRM_ONELIFE ) && dsr_resDeaths > 0 && dsr_resBar < 100 ) {
			RL_TextS( 320, 408, va( "FALLEN  -  ONE LIFE  -  %s HAD %i%% LEFT", ds_bosses[dsr_resBoss].name, 100 - dsr_resBar ), 8, 13, rl_colBad, 1 );
		} else {
			RL_TextS( 320, 408, va( "TIME UP  -  %s HAD %i%% LEFT AT HEAT %i", ds_bosses[dsr_resBoss].name, 100 - dsr_resBar, h ), 8, 13, rl_colBad, 1 );
		}
	} else {
		RL_Text( 320, 410, "Rematch times are kept apart from your descent records.", 6, 10, rl_colDim, 1 );
	}
	RL_Footer( "MOUSE, SPACE / TAB, WASD OR GAMEPAD  -  SPACE TOGGLES A MODIFIER" );
}

static void DSR_InitItem( rlButton_t *bt, int id, float x, float y, float w, float h, int data, void (*draw)( void * ) ) {
	RL_InitButton( bt, id, x, y, w, h, "", DSR_Event );
	bt->data = data;
	bt->bm.generic.ownerdraw = draw;
}

/*
=================
UI_DS_RematchMenu

boss 0-3 (-1: the last one picked, else the first beaten)
=================
*/
void UI_DS_RematchMenu( int boss ) {
	float	x, y, w;
	int		i;

	memset( &s_dsr, 0, sizeof( s_dsr ) );
	DSR_Load();
	DSR_LoadBeaten();
	if ( boss >= 0 && boss < DSR_BOSSES && dsr_beaten[boss] > 0 ) {
		dsr_boss = boss;
	}
	if ( dsr_boss < 0 || dsr_boss >= DSR_BOSSES || dsr_beaten[dsr_boss] <= 0 ) {
		dsr_boss = DSR_FirstBeaten();
	}
	RL_SetupMenu( &s_dsr.menu, DSR_Draw, RL_InGame() );

	// the bosses (left)
	x = 18 - rl_ext * 0.5f;
	w = 300;
	y = 74;
	for ( i = 0 ; i < DSR_BOSSES ; i++ ) {
		DSR_InitItem( &s_dsr.boss[i], ID_DSR_BOSS0 + i, x, y, w, 42, i, DSR_BossDraw );
		if ( dsr_beaten[i] <= 0 ) {
			s_dsr.boss[i].bm.generic.flags |= QMF_GRAYED;		// shown with how to unlock it, not selectable
		}
		Menu_AddItem( &s_dsr.menu, &s_dsr.boss[i] );
		y += 46;
	}
	// the modifiers (right)
	x = 336 + rl_ext * 0.5f;
	w = 286;
	y = 74;
	for ( i = 0 ; i < DSRM_NUM ; i++ ) {
		DSR_InitItem( &s_dsr.mod[i], ID_DSR_MOD0 + i, x, y, w, 30, i, DSR_ModDraw );
		Menu_AddItem( &s_dsr.menu, &s_dsr.mod[i] );
		y += 34;
	}
	RL_InitButton( &s_dsr.fight, ID_DSR_FIGHT, x, 356, w, 38, "FIGHT", DSR_Event );
	s_dsr.fight.center = qtrue;
	if ( dsr_boss < 0 ) {
		s_dsr.fight.bm.generic.flags |= QMF_GRAYED;
	}
	Menu_AddItem( &s_dsr.menu, &s_dsr.fight );
	RL_InitButton( &s_dsr.back, ID_RL_BACK, 8 - rl_ext, 440, 120, 28, RL_InGame() ? "LEAVE" : "BACK", DSR_Event );
	s_dsr.back.center = qtrue;
	Menu_AddItem( &s_dsr.menu, &s_dsr.back );
	RL_InitButton( &s_dsr.prac, ID_DSR_PRAC, 632 + rl_ext - 170, 440, 170, 28, "PRACTICE", DSR_Event );
	s_dsr.prac.center = qtrue;
	Menu_AddItem( &s_dsr.menu, &s_dsr.prac );

	if ( dsr_boss >= 0 ) {
		Menu_SetCursorToItem( &s_dsr.menu, dsr_res ? (void *)&s_dsr.fight : (void *)&s_dsr.boss[dsr_boss] );
	} else {
		Menu_SetCursorToItem( &s_dsr.menu, &s_dsr.back );
	}
	if ( RL_InGame() ) {
		uis.menusp = 0;
	}
	UI_PushMenu( &s_dsr.menu );
}

/*
=================
DSR_End

dsrmend <boss 1-4> <mask> <result> <kill msec> <bar % taken> <deaths> (the game, after a rematch)
=================
*/
static void DSR_End( int b, int mask, int result, int kill, int bar, int deaths ) {
	int		heat;

	DSR_Load();
	DSR_LoadBeaten();
	mask &= DSRM_ALL;
	heat = BG_DSRM_Heat( mask );
	dsr_res = result == 1 ? 1 : 2;
	dsr_resBoss = b;
	dsr_resMask = mask;
	dsr_resKill = kill;
	dsr_resBar = bar;
	dsr_resDeaths = deaths;
	dsr_resNew = 0;
	dsr_resPrev = 0;
	if ( b < 0 || b >= DSR_BOSSES ) {
		dsr_res = 0;
		return;
	}
	dsr_resPrev = dsr_best[b][heat].msec;
	if ( dsr_res == 1 && kill > 0 ) {
		dsr_wins[b]++;
		if ( dsr_resPrev <= 0 || kill < dsr_resPrev ) {
			dsr_resNew = 1;
			dsr_best[b][heat].msec = kill;
			dsr_best[b][heat].mask = mask;
		}
	}
	dsr_boss = b;
	dsr_mask = mask;
	DSR_Save();
	Com_Printf( "DESCENT: rematch over: boss %i %s heat %i %s kill %i ms (prev %i)%s, bar %i%%, deaths %i\n", b + 1, ds_bosses[b].name, heat,
		dsr_res == 1 ? "WIN" : "LOSS", kill, dsr_resPrev, dsr_resNew ? " NEW BEST" : "", bar, deaths );
}

static void DSR_Info( void ) {
	int		b, h;

	DSR_Load();
	DSR_LoadBeaten();
	for ( b = 0 ; b < DSR_BOSSES ; b++ ) {
		Com_Printf( "rematch boss %i %s: %s, tries %i wins %i\n", b + 1, ds_bosses[b].name,
			dsr_beaten[b] > 0 ? va( "unlocked (beaten %i)", dsr_beaten[b] ) : "LOCKED", dsr_tries[b], dsr_wins[b] );
		for ( h = 0 ; h < DSR_HEATS ; h++ ) {
			if ( dsr_best[b][h].msec > 0 ) {
				Com_Printf( "  heat %i: %s (%i ms, mods %i)\n", h, DSR_Time( dsr_best[b][h].msec ), dsr_best[b][h].msec, dsr_best[b][h].mask );
			}
		}
	}
}

/*
=================
UI_DS_RematchCommand

dsrematch (the screen) / dsrematch <boss 1-4> [mask] (straight in; locked bosses refuse);
dsrmend (the game); dsrminfo (the table)
=================
*/
qboolean UI_DS_RematchCommand( const char *cmd ) {
	int		a[6], i;

	if ( !Q_stricmp( cmd, "dsrematch" ) ) {
		if ( UI_Argv( 1 )[0] ) {
			a[0] = atoi( UI_Argv( 1 ) );
			a[1] = atoi( UI_Argv( 2 ) );
			DSR_Load();
			DSR_LoadBeaten();
			DSR_Launch( a[0] - 1, a[1] );
		} else {
			UI_DS_RematchMenu( -1 );
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsrmend" ) ) {
		for ( i = 0 ; i < 6 ; i++ ) {
			a[i] = atoi( UI_Argv( i + 1 ) );
		}
		DSR_End( a[0] - 1, a[1], a[2], a[3], a[4], a[5] );
		UI_DS_RematchMenu( -1 );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsrminfo" ) ) {
		DSR_Info();
		return qtrue;
	}
	return qfalse;
}
