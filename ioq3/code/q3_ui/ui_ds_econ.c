/*
===========================================================================
REARMED -- DESCENT round 2 ECONOMY, UI side (owner ds-economy).

- Difficulty switch between floors: a DIFFICULTY button on the floor map opens a
  5-level picker (bots' skill, boss strength and the reward multiplier follow from the
  next floor on; lives stay as set at run start). Run keys: difficulty, diff_start,
  diff_min / diff_max (set when a floor launches: the difficulties really played).
- Records go to the lowest difficulty a floor was played on (UI_DS_RecordDifficulty).
- RESET ALL PROGRESS (Records screen): two confirm screens, then rearmed_meta.txt,
  rearmed_run.txt, rearmed_history.txt and rearmed_skills.txt are copied to
  rearmed_backup_<n>_meta/run/history/skills.txt and emptied. Main menu only.
- ABANDON DESCENT (hub): ends the run as a loss, its skulls and records banked.
- g_fragMult (standard modes' frag target x1/x2/x3) is registered archived here too.

Dev: dsdiff <1-5> (switch now), dsdiffmenu, dsreset [2 | confirm], dsabandon [confirm]
===========================================================================
*/
#include "ui_local.h"
#include "ui_ds_meta.h"

#define DSE_SKILLS_FILE		"rearmed_skills.txt"
#define DSE_MAX_BACKUPS		999

#define DSE_NUM_FILES	11		// (en-boss: + the boss practice file; en-daily: + the daily run and its personal bests; en-rematch: + rematch bests; en-memory: + bot records / nemesis hall)
static const char *dse_files[DSE_NUM_FILES] = { RL_META_FILE, RL_RUN_FILE, RL_HISTORY_FILE, DSE_SKILLS_FILE, "rearmed_ghost.txt", "rearmed_tally.txt", "rearmed_practice.txt",
	"rearmed_daily_run.txt", "rearmed_daily.txt", "rearmed_rematch.txt", "rearmed_bots.txt" };
static const char *dse_tags[DSE_NUM_FILES] = { "meta", "run", "history", "skills", "ghost", "tally", "practice", "dailyrun", "daily", "rematch", "bots" };

static char			dse_buf[16384];
static char			dse_msg[96];		// the last reset / abandon result (shown on the Records screen)
static vec4_t		dse_colGold = { 1.0f, 0.78f, 0.25f, 1.0f };


/*
=======================================================================

RUN KEYS

=======================================================================
*/

void UI_DS_EconRegisterCvars( void ) {
	trap_Cvar_Register( NULL, "g_fragMult", "1", CVAR_ARCHIVE );
}

static int DSE_Clamp( int d ) {
	if ( d < 1 || d > 5 ) {
		return 3;
	}
	return d;
}

static int DSE_RunDifficulty( void ) {
	return DSE_Clamp( RL_KVInt( &rl_run, "difficulty" ) );
}

// RL_StartRun, the new run in rl_run
void UI_DS_EconStartRun( int difficulty ) {
	RL_KVSetInt( &rl_run, "diff_start", DSE_Clamp( difficulty ) );
	RL_KVSet( &rl_run, "diff_min", "" );
	RL_KVSet( &rl_run, "diff_max", "" );
}

// RL_LaunchNode, before the run is saved: this floor is played on the current difficulty
void UI_DS_EconLaunch( void ) {
	int		d;
	int		lo;
	int		hi;

	d = DSE_RunDifficulty();
	lo = RL_KVInt( &rl_run, "diff_min" );
	hi = RL_KVInt( &rl_run, "diff_max" );
	if ( lo < 1 || d < lo ) {
		RL_KVSetInt( &rl_run, "diff_min", d );
	}
	if ( hi < 1 || d > hi ) {
		RL_KVSetInt( &rl_run, "diff_max", d );
	}
	if ( !RL_KVGet( &rl_run, "diff_start" )[0] ) {
		RL_KVSetInt( &rl_run, "diff_start", d );		// a run from before the switch
	}
}

// the difficulty this run's records go to (rl_run loaded): the lowest one played
int UI_DS_RecordDifficulty( void ) {
	int		d;

	d = RL_KVInt( &rl_run, "diff_min" );
	if ( d < 1 || d > 5 ) {
		d = DSE_RunDifficulty();
	}
	return d;
}

// the history line's difficulty field: "3", or "2-4" for a mixed run (atoi reads the record one)
const char *UI_DS_HistoryDifficulty( void ) {
	int		lo;
	int		hi;

	lo = UI_DS_RecordDifficulty();
	hi = RL_KVInt( &rl_run, "diff_max" );
	if ( hi > lo && hi <= 5 ) {
		return va( "%i-%i", lo, hi );
	}
	return va( "%i", lo );
}

static const char *DSE_MultString( int d ) {
	int		m;

	m = (int)( BG_DS_RewardMult( d ) * 100.0f + 0.5f );
	return va( "x%i.%02i", m / 100, m % 100 );
}

/*
=================
UI_DS_EconSummary

Run summary lines (left column): the reward multiplier, what the records count as
=================
*/
float UI_DS_EconSummary( float x, float y, float w ) {
	int		d;
	int		rec;
	int		hi;
	char	line[64];

	d = DSE_RunDifficulty();
	rec = UI_DS_RecordDifficulty();
	hi = RL_KVInt( &rl_run, "diff_max" );
	// where the skulls came from (game tallies, economy pass 2): floors (+ style + node), kills,
	// bosses (the lives you took), other (chests, nemesis, final blows), bonus % and difficulty
	if ( RL_KVGet( &rl_run, "sk_fl" )[0] || RL_KVGet( &rl_run, "sk_ot" )[0] ) {
		int		bn = RL_KVInt( &rl_run, "sk_bn" );

		RL_Text( x, y, "SKULLS FROM", 5, 8, rl_colDim, 0 );
		y += 10;
		Com_sprintf( line, sizeof( line ), "floors %i  kills %i", RL_KVInt( &rl_run, "sk_fl" ), RL_KVInt( &rl_run, "sk_ki" ) );
		RL_Text( x + 4, y, line, 5, 8, rl_colBone, 0 );
		y += 10;
		Com_sprintf( line, sizeof( line ), "bosses %i  loot+blows %i", RL_KVInt( &rl_run, "sk_bo" ), RL_KVInt( &rl_run, "sk_ot" ) );
		RL_Text( x + 4, y, line, 5, 8, rl_colBone, 0 );
		y += 10;
		Com_sprintf( line, sizeof( line ), "bonus & difficulty %s%i", bn >= 0 ? "+" : "", bn );
		RL_Text( x + 4, y, line, 5, 8, bn >= 0 ? rl_colBone : rl_colDim, 0 );
		y += 10;
		if ( RL_KVInt( &rl_run, "sk_md" ) > 0 ) {		// ds-surprise: the floor modifiers taken
			Com_sprintf( line, sizeof( line ), "floor modifiers %i", RL_KVInt( &rl_run, "sk_md" ) );
			RL_Text( x + 4, y, line, 5, 8, rl_colBone, 0 );
			y += 10;
		}
		y += 2;
	}
	Com_sprintf( line, sizeof( line ), "%s %s", rl_skillNames[d - 1], DSE_MultString( d ) );
	RL_Text( x, y, "REWARDS", 6, 9, rl_colDim, 0 );
	RL_Text( x + w, y, line, 5, 9, rl_colOrange, 2 );
	y += 12;
	if ( hi > rec && hi <= 5 ) {
		RL_Text( x, y, "RECORDS AS", 6, 9, rl_colDim, 0 );
		RL_Text( x + w, y, rl_skillNames[rec - 1], 5, 9, rl_colBone, 2 );
		y += 12;
	}
	return y;
}


/*
=======================================================================

ECONOMY PASS 2: refunds for retired upgrades, the tree's progress line

=======================================================================
*/

/*
=================
UI_DS_EconMigrate

Once per meta file (econ_v 2): ranks owned in a retired upgrade are refunded at the price
paid (the retired row keeps its old prices) and zeroed; a kept upgrade with more ranks than
it has now gives the extra ranks back the same way. The caller has rl_meta loaded; saves.
=================
*/
void UI_DS_EconMigrate( void ) {
	int		u;
	int		r;
	int		k;
	int		keep;
	int		refund;
	int		total;

	if ( RL_KVInt( &rl_meta, "econ_v" ) >= 2 ) {
		return;
	}
	total = 0;
	for ( u = 0; u < DSU_NUM; u++ ) {
		r = RL_KVInt( &rl_meta, ds_upgrades[u].key );
		keep = DS_UP_RETIRED( u ) ? 0 : ds_upgrades[u].maxRank;
		if ( r <= keep ) {
			continue;
		}
		refund = 0;
		for ( k = keep; k < r && k < 5; k++ ) {
			refund += ds_upgrades[u].cost[k];
		}
		RL_KVSetInt( &rl_meta, ds_upgrades[u].key, keep );
		total += refund;
		Com_Printf( "DESCENT: economy pass 2: %s rank %i -> %i, %i skulls back\n", ds_upgrades[u].key, r, keep, refund );
	}
	if ( total > 0 ) {
		RL_KVSetInt( &rl_meta, "skulls", RL_KVInt( &rl_meta, "skulls" ) + total );
		RL_KVSetInt( &rl_meta, "econ_refund", RL_KVInt( &rl_meta, "econ_refund" ) + total );
	}
	RL_KVSetInt( &rl_meta, "econ_v", 2 );
	RL_SaveMeta();
	Com_Printf( "DESCENT: economy pass 2 applied (%i skulls refunded)\n", total );
}

/*
=================
UI_DS_EconTreeLine

Upgrades screen: how much of the tree is bought (skulls at today's prices, ranks)
=================
*/
void UI_DS_EconTreeLine( float x, float y, float w ) {
	int		u;
	int		r;
	int		k;
	int		spent;
	int		all;
	int		ranks;
	int		maxRanks;

	spent = all = ranks = maxRanks = 0;
	for ( u = 0; u < DSU_NUM; u++ ) {
		if ( DS_UP_RETIRED( u ) ) {
			continue;
		}
		r = RL_KVInt( &rl_meta, ds_upgrades[u].key );
		for ( k = 0; k < ds_upgrades[u].maxRank && k < 5; k++ ) {
			all += ds_upgrades[u].cost[k];
			if ( k < r ) {
				spent += ds_upgrades[u].cost[k];
			}
		}
		ranks += r < ds_upgrades[u].maxRank ? r : ds_upgrades[u].maxRank;
		maxRanks += ds_upgrades[u].maxRank;
	}
	RL_Text( x, y, va( "THE TREE: %i OF %i RANKS  |  %i OF %i SKULLS' WORTH (%i%%)", ranks, maxRanks, spent, all,
		all ? spent * 100 / all : 0 ), 5, 8, rl_colDim, 0 );
	if ( RL_KVInt( &rl_meta, "econ_refund" ) > 0 ) {
		RL_Text( x + w, y, va( "%i skulls refunded for retired upgrades", RL_KVInt( &rl_meta, "econ_refund" ) ), 5, 8, rl_colGood, 2 );
	}
}


/*
=======================================================================

DIFFICULTY PICKER (from the floor map)

=======================================================================
*/

typedef struct {
	menuframework_s	menu;
	rlButton_t		diff[5];
	rlButton_t		back;
	int				current;
} dseDiff_t;

static dseDiff_t s_dseDiff;

static void DSE_SetDifficulty( int d ) {
	int		old;

	RL_LoadRun();
	if ( !RL_KVInt( &rl_run, "active" ) ) {
		Com_Printf( "DESCENT: no descent in progress\n" );
		return;
	}
	d = DSE_Clamp( d );
	old = DSE_RunDifficulty();
	if ( !RL_KVGet( &rl_run, "diff_start" )[0] ) {
		RL_KVSetInt( &rl_run, "diff_start", old );
	}
	RL_KVSetInt( &rl_run, "difficulty", d );
	RL_SaveRun();
	RL_LoadRun();		// the floor map's copy (header, node skill)
	Com_Printf( "DESCENT: difficulty %s -> %s, rewards %s from the next floor (lives stay %i)\n",
		rl_skillNames[old - 1], rl_skillNames[d - 1], DSE_MultString( d ), RL_KVInt( &rl_run, "lives" ) );
}

static void DSE_DiffEvent( void *ptr, int event ) {
	int		id;

	if ( event != QM_ACTIVATED ) {
		return;
	}
	id = ((menucommon_s *)ptr)->id;
	if ( id >= ID_RL_ITEM0 && id < ID_RL_ITEM0 + 5 ) {
		DSE_SetDifficulty( id - ID_RL_ITEM0 + 1 );
		trap_S_StartLocalSound( rl_buySound, CHAN_LOCAL_SOUND );
	}
	UI_PopMenu();
}

static void DSE_DiffDraw( void ) {
	int		rec;
	int		lives;
	float	y;

	RL_Backdrop( !RL_InGame() );
	RL_Header( "DIFFICULTY", "From the next floor on: the bots' skill, the bosses' strength and your rewards." );
	Menu_Draw( &s_dseDiff.menu );

	rec = UI_DS_RecordDifficulty();
	lives = RL_KVInt( &rl_run, "lives" );
	y = 392;
	RL_Text( 320, y, va( "Your lives stay as they are (%i). Rewards: skulls, gem xp and skill xp.", lives ), 6, 10, rl_colDim, 1 );
	y += 14;
	if ( RL_KVGet( &rl_run, "diff_min" )[0] ) {
		RL_Text( 320, y, va( "Records count the easiest difficulty you play a floor on: so far %s.", rl_skillNames[rec - 1] ), 6, 10, rl_colDim, 1 );
	}
	else {
		RL_Text( 320, y, "Records count the easiest difficulty you play a floor on.", 6, 10, rl_colDim, 1 );
	}
	RL_Footer( "MOUSE, SPACE / TAB, WASD OR GAMEPAD  |  ESC: BACK TO THE MAP" );
}

void UI_DS_DifficultyMenu( void ) {
	int		i;
	float	y;

	memset( &s_dseDiff, 0, sizeof( s_dseDiff ) );
	RL_SetupMenu( &s_dseDiff.menu, DSE_DiffDraw, RL_InGame() );
	RL_LoadRun();
	s_dseDiff.current = DSE_RunDifficulty();

	y = 72;
	for ( i = 0; i < 5; i++ ) {
		RL_InitButton( &s_dseDiff.diff[i], ID_RL_ITEM0 + i, 320 - 180, y, 360, 50, rl_skillNames[i], DSE_DiffEvent );
		s_dseDiff.diff[i].icon = rl_skillIcons[i];
		Com_sprintf( s_dseDiff.diff[i].sub, sizeof( s_dseDiff.diff[i].sub ), "bots at skill %i  |  rewards %s%s", i + 1,
			DSE_MultString( i + 1 ), i + 1 == s_dseDiff.current ? "  |  NOW" : "" );
		Menu_AddItem( &s_dseDiff.menu, &s_dseDiff.diff[i] );
		y += 61;
	}
	RL_InitButton( &s_dseDiff.back, ID_RL_BACK, 8 - rl_ext, 440, 120, 28, "BACK", DSE_DiffEvent );
	s_dseDiff.back.center = qtrue;
	Menu_AddItem( &s_dseDiff.menu, &s_dseDiff.back );

	UI_PushMenu( &s_dseDiff.menu );
	Menu_SetCursorToItem( &s_dseDiff.menu, &s_dseDiff.diff[s_dseDiff.current - 1] );
}

/*
=================
UI_DS_DiffButtonDraw

The floor map's DIFFICULTY button: icon, name, reward multiplier
=================
*/
static void UI_DS_DiffButtonDraw( void *self ) {
	rlButton_t		*b;
	menucommon_s	*g;
	float			x;
	float			y;
	float			w;
	float			h;
	qboolean		focus;
	int				d;

	b = (rlButton_t *)self;
	g = &b->bm.generic;
	x = g->left;
	y = g->top;
	w = g->right - g->left;
	h = g->bottom - g->top;
	focus = ( Menu_ItemAtCursor( g->parent ) == self );
	d = DSE_RunDifficulty();

	RL_Frame( x, y, w, h, focus ? rl_colPanelHi : rl_colPanel, focus ? rl_colOrange : rl_colLine );
	if ( focus ) {
		UI_FillRect( x, y, 3, h, rl_colOrange );
	}
	RL_Pic( x + 5, y + 3, h - 6, h - 6, rl_skillIcons[d - 1], NULL );
	RL_Text( x + h + 2, y + 2, "DIFFICULTY", 4, 7, rl_colDim, 0 );
	RL_Text( x + h + 2, y + 10, rl_skillNames[d - 1], 6, 9, focus ? rl_colBone : rl_colBone, 0 );
	RL_Text( x + w - 6, y + 2, "REWARDS", 4, 7, rl_colDim, 2 );
	RL_Text( x + w - 6, y + 10, DSE_MultString( d ), 6, 9, d >= 4 ? dse_colGold : rl_colOrange, 2 );
}

/*
=================
UI_DS_SmallButton

A compact button (small fixed font): ABANDON DESCENT, RESET ALL PROGRESS
=================
*/
static void DSE_SmallButtonDraw( void *self ) {
	rlButton_t		*b;
	menucommon_s	*g;
	float			x;
	float			y;
	float			w;
	float			h;
	qboolean		focus;
	qboolean		grayed;

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
	RL_Text( x + w / 2, y + ( h - 11 ) / 2, b->label, 7, 11, grayed ? rl_colDark : focus ? rl_colBone : rl_colBad, 1 );
}

void UI_DS_SmallButton( rlButton_t *b ) {
	b->bm.generic.ownerdraw = DSE_SmallButtonDraw;
}

void UI_DS_InitDiffButton( rlButton_t *b, int id, float x, float y, float w, float h, void (*callback)( void *, int ) ) {
	RL_InitButton( b, id, x, y, w, h, "DIFFICULTY", callback );
	b->bm.generic.ownerdraw = UI_DS_DiffButtonDraw;
}


/*
=======================================================================

RESET ALL PROGRESS (two confirm screens, backups first)

=======================================================================
*/

static qboolean DSE_Exists( const char *name ) {
	fileHandle_t	f;

	f = 0;
	trap_FS_FOpenFile( name, &f, FS_READ );
	if ( !f ) {
		return qfalse;
	}
	trap_FS_FCloseFile( f );
	return qtrue;
}

// copies a file; -1 no source, -2 can't write the copy, else the bytes copied
static int DSE_Copy( const char *from, const char *to ) {
	fileHandle_t	in;
	fileHandle_t	out;
	int				len;
	int				n;
	int				total;

	in = out = 0;
	len = trap_FS_FOpenFile( from, &in, FS_READ );
	if ( !in ) {
		return -1;
	}
	trap_FS_FOpenFile( to, &out, FS_WRITE );
	if ( !out ) {
		trap_FS_FCloseFile( in );
		return -2;
	}
	total = 0;
	while ( len > 0 ) {
		n = len > (int)sizeof( dse_buf ) ? (int)sizeof( dse_buf ) : len;
		trap_FS_Read( dse_buf, n, in );
		trap_FS_Write( dse_buf, n, out );
		len -= n;
		total += n;
	}
	trap_FS_FCloseFile( in );
	trap_FS_FCloseFile( out );
	return total;
}

static void DSE_Empty( const char *name ) {
	fileHandle_t	f;

	f = 0;
	trap_FS_FOpenFile( name, &f, FS_WRITE );
	if ( f ) {
		trap_FS_FCloseFile( f );
	}
}

/*
=================
UI_DS_ResetProgress

Backs the four files up as rearmed_backup_<n>_<tag>.txt (the first n with none of them),
then empties them. Nothing is emptied unless every existing file was copied.
=================
*/
qboolean UI_DS_ResetProgress( void ) {
	int		n;
	int		i;
	int		r;
	int		copied;
	char	name[MAX_QPATH];

	for ( n = 1; n <= DSE_MAX_BACKUPS; n++ ) {
		for ( i = 0; i < DSE_NUM_FILES; i++ ) {
			if ( DSE_Exists( va( "rearmed_backup_%i_%s.txt", n, dse_tags[i] ) ) ) {
				break;
			}
		}
		if ( i == DSE_NUM_FILES ) {
			break;
		}
	}
	if ( n > DSE_MAX_BACKUPS ) {
		Q_strncpyz( dse_msg, "No backup slot left: nothing was reset.", sizeof( dse_msg ) );
		Com_Printf( S_COLOR_YELLOW "DESCENT: %s\n", dse_msg );
		return qfalse;
	}
	copied = 0;
	for ( i = 0; i < DSE_NUM_FILES; i++ ) {
		Com_sprintf( name, sizeof( name ), "rearmed_backup_%i_%s.txt", n, dse_tags[i] );
		r = DSE_Copy( dse_files[i], name );
		if ( r == -2 ) {
			Com_sprintf( dse_msg, sizeof( dse_msg ), "Can't write %s: nothing was reset.", name );
			Com_Printf( S_COLOR_YELLOW "DESCENT: %s\n", dse_msg );
			return qfalse;
		}
		if ( r >= 0 ) {
			copied++;
			Com_Printf( "DESCENT: backup %s -> %s (%i bytes)\n", dse_files[i], name, r );
		}
	}
	for ( i = 0; i < DSE_NUM_FILES; i++ ) {
		if ( DSE_Exists( dse_files[i] ) ) {
			DSE_Empty( dse_files[i] );
		}
	}
	rl_run.num = 0;
	rl_meta.num = 0;
	RL_LoadMeta();
	RL_LoadRun();
	rl_hubDirty = qtrue;
	Com_sprintf( dse_msg, sizeof( dse_msg ), "Progress reset. %i files backed up as rearmed_backup_%i_*.txt", copied, n );
	Com_Printf( "DESCENT: progress reset, %i files backed up as rearmed_backup_%i_*.txt\n", copied, n );
	return qtrue;
}

static void DSE_Reset2Draw( void ) {
	RL_Text( 320, 234, "EVERY SKULL, UPGRADE, UNLOCK, RECORD AND SKILL GOES.", 7, 11, color_red, 1 );
	RL_Text( 320, 249, "THERE IS NO UNDO IN THE GAME.", 7, 11, color_red, 1 );
	RL_Text( 320, 298, "(the old files are kept as rearmed_backup_<n>_*.txt in the mod folder)", 5, 8, rl_colDim, 1 );
}

static void DSE_Reset2Action( qboolean result ) {
	if ( !result ) {
		return;
	}
	UI_DS_ResetProgress();
}

static void DSE_Reset1Draw( void ) {
	RL_Text( 320, 234, "SKULLS, UPGRADES, CHARACTERS, SECRETS, RECORDS,", 7, 11, color_red, 1 );
	RL_Text( 320, 249, "HISTORY, SKILLS AND THE DESCENT IN PROGRESS", 7, 11, color_red, 1 );
}

static void DSE_Reset1Action( qboolean result ) {
	if ( !result ) {
		return;
	}
	UI_ConfirmMenu( "ARE YOU SURE?", DSE_Reset2Draw, DSE_Reset2Action );		// the second, separate confirm
}

void UI_DS_ResetMenu( void ) {
	if ( RL_InGame() ) {
		Q_strncpyz( dse_msg, "Quit to the main menu first.", sizeof( dse_msg ) );
		return;
	}
	UI_ConfirmMenu( "RESET ALL PROGRESS?", DSE_Reset1Draw, DSE_Reset1Action );
}

const char *UI_DS_EconMessage( void ) {
	return dse_msg;
}


/*
=======================================================================

ABANDON DESCENT (hub)

=======================================================================
*/

static void DSE_AbandonDraw( void ) {
	UI_DrawProportionalString( 320, 230, "IT ENDS AS A LOSS. SKULLS AND RECORDS ARE BANKED", UI_CENTER|UI_SMALLFONT, color_red );
}

static void DSE_AbandonAction( qboolean result ) {
	if ( !result ) {
		return;
	}
	UI_DS_AbandonRun();
}

void UI_DS_AbandonMenu( void ) {
	RL_LoadRun();
	if ( !RL_KVInt( &rl_run, "active" ) || RL_InGame() ) {
		return;
	}
	UI_ConfirmMenu( "ABANDON THE DESCENT?", DSE_AbandonDraw, DSE_AbandonAction );
}


/*
=================
UI_DS_EconCommand

Dev: dsdiff <1-5>, dsdiffmenu, dsreset [2 | confirm], dsabandon [confirm]
=================
*/
qboolean UI_DS_EconCommand( const char *cmd ) {
	char	arg[32];

	if ( !Q_stricmp( cmd, "dsdiff" ) ) {
		Q_strncpyz( arg, UI_Argv( 1 ), sizeof( arg ) );
		if ( atoi( arg ) < 1 || atoi( arg ) > 5 ) {
			RL_LoadRun();
			Com_Printf( "dsdiff <1-5>: now %i (start %s, min %s, max %s), rewards %s, records %i\n", DSE_RunDifficulty(),
				RL_KVGet( &rl_run, "diff_start" ), RL_KVGet( &rl_run, "diff_min" ), RL_KVGet( &rl_run, "diff_max" ),
				DSE_MultString( DSE_RunDifficulty() ), UI_DS_RecordDifficulty() );
			return qtrue;
		}
		DSE_SetDifficulty( atoi( arg ) );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsdiffmenu" ) ) {
		if ( RL_InGame() ) {
			uis.menusp = 0;
		}
		RL_MapMenu();
		UI_DS_DifficultyMenu();
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsreset" ) ) {
		Q_strncpyz( arg, UI_Argv( 1 ), sizeof( arg ) );
		if ( !Q_stricmp( arg, "confirm" ) ) {
			UI_DS_ResetProgress();
			return qtrue;
		}
		UI_RL_HubMenu();
		UI_DS_RecordsMenu();
		if ( !Q_stricmp( arg, "2" ) ) {
			DSE_Reset1Action( qtrue );		// (screenshot of the second screen)
		}
		else {
			UI_DS_ResetMenu();
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsabandon" ) ) {
		Q_strncpyz( arg, UI_Argv( 1 ), sizeof( arg ) );
		RL_LoadRun();
		if ( !RL_KVInt( &rl_run, "active" ) ) {
			Com_Printf( "dsabandon: no descent in progress\n" );
			return qtrue;
		}
		if ( !Q_stricmp( arg, "confirm" ) ) {
			UI_DS_AbandonRun();
			return qtrue;
		}
		UI_RL_HubMenu();
		UI_DS_AbandonMenu();
		return qtrue;
	}
	return qfalse;
}
