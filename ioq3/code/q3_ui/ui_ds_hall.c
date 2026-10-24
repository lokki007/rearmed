/*
===========================================================================
REARMED -- round 4 NEMESIS HALL OF FAME + RIVALS, UI side (en-memory).

Records > NEMESIS HALL (or `dshall [rivals]`). Reads rearmed_bots.txt (written only by the game
at floor ends, game/g_ds_botmem.c; format game/bg_ds_botmem.h) and never writes it.
Two lists, Tab / Q / E / shoulder buttons or a click on the tab switch them:
	HALL OF FAME	every nemesis of your runs, newest first: name + title, peak level, kills on you,
					the run and floor where it rose, how it ended (slain with which weapon, outlived
					the run, dethroned, the run ended, or still hunting you in the run in progress)
	RIVALS			every named bot's lifetime record with you, most history first
W / S, the mouse wheel, the d-pad or the left stick move the pick; the mouse picks by hovering;
the panel on the right has the details (the bot's lifetime record). Space / A / Enter: BACK.
===========================================================================
*/
#include "ui_local.h"
#include "ui_ds_meta.h"
#include "../game/bg_ds_botmem.h"

#define DSH_TAB_HALL		0
#define DSH_TAB_RIVALS		1
#define DSH_ROW_H			27
#define DSH_LIST_TOP		100
#define DSH_LIST_BOTTOM		432

typedef struct {
	char	name[24];
	int		k, d, fm, rm, lastRun, lastDate, lastRes, lastK, lastD, nem, nemPeak, hs, rv, brv;
	qhandle_t	icon;
} dshBot_t;

typedef struct {
	int		run, runNo, date;
	char	name[24];
	char	title[24];
	int		peak, kills, floorRose, status, floorEnd;
	char	how[24];
	qhandle_t	icon;
} dshHall_t;

typedef struct {
	menuframework_s	menu;
	rlButton_t		back;
	int				tab;
	int				sel;
	int				top;
	int				lastMouseX, lastMouseY;
	dshBot_t		bots[BM_MAX_BOTS];
	int				numBots;
	int				botOrder[BM_MAX_BOTS];
	dshHall_t		hall[BM_MAX_HALL];
	int				numHall;
	int				slain;
	int				runSeed;		// the run in progress (0 none): its open nemesis is still hunting
	char			buf[16384];
} dsHall_t;

static dsHall_t		s_dsh;
static vec4_t		dsh_colGold = { 1.0f, 0.78f, 0.25f, 1.0f };
static vec4_t		dsh_colRed = { 1.0f, 0.3f, 0.2f, 1.0f };
static vec4_t		dsh_colPurple = { 0.78f, 0.55f, 1.0f, 1.0f };

/*
===============================================================================

FILE

===============================================================================
*/

static void DSH_Field( const char *line, int n, char *out, int size ) {
	int		i;

	while ( n > 0 && *line ) {
		if ( *line == '|' ) {
			n--;
		}
		line++;
	}
	for ( i = 0; i < size - 1 && line[i] && line[i] != '|'; i++ ) {
		out[i] = line[i];
	}
	out[i] = 0;
}

static int DSH_Int( const char *line, int n ) {
	char	f[16];

	DSH_Field( line, n, f, sizeof( f ) );
	return atoi( f );
}

static qhandle_t DSH_Icon( const char *name ) {
	char	low[32];

	Q_strncpyz( low, name, sizeof( low ) );
	Q_strlwr( low );
	return trap_R_RegisterShaderNoMip( va( "models/players/%s/icon_default", low ) );
}

static void DSH_Load( void ) {
	fileHandle_t	f;
	int				len;
	int				i;
	int				j;
	int				t;
	char			*p;
	char			*line;
	dshBot_t		*b;
	dshHall_t		*h;

	s_dsh.numBots = s_dsh.numHall = s_dsh.slain = 0;
	RL_LoadRun();
	s_dsh.runSeed = RL_KVInt( &rl_run, "active" ) == 1 ? RL_KVInt( &rl_run, "seed" ) : 0;
	len = trap_FS_FOpenFile( BM_FILE, &f, FS_READ );
	if ( !f ) {
		return;
	}
	if ( len > (int)sizeof( s_dsh.buf ) - 1 ) {
		len = sizeof( s_dsh.buf ) - 1;
	}
	s_dsh.buf[0] = 0;
	if ( len > 0 ) {
		trap_FS_Read( s_dsh.buf, len, f );
		s_dsh.buf[len] = 0;
	}
	trap_FS_FCloseFile( f );
	p = s_dsh.buf;
	while ( *p ) {
		line = p;
		while ( *p && *p != '\n' ) {
			p++;
		}
		if ( *p ) {
			*p++ = 0;
		}
		len = strlen( line );
		while ( len > 0 && line[len - 1] == '\r' ) {
			line[--len] = 0;
		}
		if ( line[0] == 'b' && line[1] == '|' && s_dsh.numBots < BM_MAX_BOTS ) {
			b = &s_dsh.bots[s_dsh.numBots++];
			memset( b, 0, sizeof( *b ) );
			DSH_Field( line, 1, b->name, sizeof( b->name ) );
			b->k = DSH_Int( line, 2 );
			b->d = DSH_Int( line, 3 );
			b->fm = DSH_Int( line, 4 );
			b->rm = DSH_Int( line, 5 );
			b->lastRun = DSH_Int( line, 6 );
			b->lastDate = DSH_Int( line, 7 );
			b->lastRes = DSH_Int( line, 8 );
			b->lastK = DSH_Int( line, 9 );
			b->lastD = DSH_Int( line, 10 );
			b->nem = DSH_Int( line, 11 );
			b->nemPeak = DSH_Int( line, 12 );
			b->hs = DSH_Int( line, 13 );
			b->rv = DSH_Int( line, 14 );
			b->brv = DSH_Int( line, 15 );
			b->icon = DSH_Icon( b->name );
		} else if ( line[0] == 'h' && line[1] == '|' && s_dsh.numHall < BM_MAX_HALL ) {
			h = &s_dsh.hall[s_dsh.numHall++];
			memset( h, 0, sizeof( *h ) );
			h->run = DSH_Int( line, 1 );
			h->runNo = DSH_Int( line, 2 );
			h->date = DSH_Int( line, 3 );
			DSH_Field( line, 4, h->name, sizeof( h->name ) );
			DSH_Field( line, 5, h->title, sizeof( h->title ) );
			h->peak = DSH_Int( line, 6 );
			h->kills = DSH_Int( line, 7 );
			h->floorRose = DSH_Int( line, 8 );
			h->status = DSH_Int( line, 9 );
			h->floorEnd = DSH_Int( line, 10 );
			DSH_Field( line, 11, h->how, sizeof( h->how ) );
			if ( h->status == BMH_OPEN && ( !s_dsh.runSeed || h->run != s_dsh.runSeed ) ) {
				h->status = BMH_RUNENDED;
			}
			if ( h->status == BMH_SLAIN ) {
				s_dsh.slain++;
			}
			h->icon = DSH_Icon( h->name );
		}
	}
	// rivals: the most history first
	for ( i = 0; i < s_dsh.numBots; i++ ) {
		s_dsh.botOrder[i] = i;
	}
	for ( i = 1; i < s_dsh.numBots; i++ ) {
		for ( j = i; j > 0; j-- ) {
			b = &s_dsh.bots[s_dsh.botOrder[j]];
			if ( b->fm * 2 + b->k + b->d <= s_dsh.bots[s_dsh.botOrder[j - 1]].fm * 2 + s_dsh.bots[s_dsh.botOrder[j - 1]].k
				+ s_dsh.bots[s_dsh.botOrder[j - 1]].d ) {
				break;
			}
			t = s_dsh.botOrder[j];
			s_dsh.botOrder[j] = s_dsh.botOrder[j - 1];
			s_dsh.botOrder[j - 1] = t;
		}
	}
}

static dshBot_t *DSH_Bot( const char *name ) {
	int		i;

	for ( i = 0; i < s_dsh.numBots; i++ ) {
		if ( !Q_stricmp( s_dsh.bots[i].name, name ) ) {
			return &s_dsh.bots[i];
		}
	}
	return NULL;
}

/*
===============================================================================

TEXT

===============================================================================
*/

static int DSH_Count( void ) {
	return s_dsh.tab == DSH_TAB_HALL ? s_dsh.numHall : s_dsh.numBots;
}

// the hall is listed newest first
static dshHall_t *DSH_HallAt( int row ) {
	return &s_dsh.hall[s_dsh.numHall - 1 - row];
}

static const char *DSH_Date( int d ) {
	if ( d <= 0 ) {
		return "-";
	}
	return va( "%04i-%02i-%02i", d / 10000, ( d / 100 ) % 100, d % 100 );
}

static const char *DSH_Fate( const dshHall_t *h, const float **col ) {
	switch ( h->status ) {
	case BMH_SLAIN:
		*col = dsh_colGold;
		return va( "SLAIN BY YOU - %s, FLOOR %i", h->how, h->floorEnd );
	case BMH_OUTLIVED:
		*col = dsh_colRed;
		return va( "OUTLIVED YOU: YOUR RUN DIED, FLOOR %i", h->floorEnd );
	case BMH_DETHRONED:
		*col = rl_colDim;
		return va( "DETHRONED BY %s, FLOOR %i", h->how, h->floorEnd );
	case BMH_OPEN:
		*col = rl_colOrange;
		return "STILL HUNTING YOU";
	default:
		*col = rl_colDim;
		return "SURVIVED - THE RUN ENDED";
	}
}

static const char *DSH_TierName( const dshBot_t *b ) {
	if ( b->fm <= 0 ) {
		return "STRANGER";
	}
	if ( b->nem > 0 ) {
		return "OLD NEMESIS";
	}
	if ( b->fm >= BM_ARCH_FLOORS || b->k + b->d >= BM_ARCH_KILLS ) {
		return "ARCH-RIVAL";
	}
	if ( b->fm >= BM_RIVAL_FLOORS || b->k + b->d >= BM_RIVAL_KILLS ) {
		return "RIVAL";
	}
	return "FAMILIAR";
}

static void DSH_Pips( float x, float y, int n, int max, float sz, const float *on ) {
	int		i;

	for ( i = 0; i < max; i++ ) {
		UI_FillRect( x + i * ( sz + 2 ), y, sz, sz, i < n ? on : rl_colDark );
	}
}

/*
===============================================================================

DRAW

===============================================================================
*/

static void DSH_Geometry( float *x, float *w, float *dx, float *dw ) {
	float	total = 624 + 2 * rl_ext;

	*dw = total * 0.3f;
	if ( *dw < 200 ) {
		*dw = 200;
	} else if ( *dw > 360 ) {
		*dw = 360;
	}
	*x = 8 - rl_ext;
	*w = total - *dw - 8;
	*dx = *x + *w + 8;
}

static int DSH_Visible( void ) {
	return ( DSH_LIST_BOTTOM - DSH_LIST_TOP ) / DSH_ROW_H;
}

static void DSH_Clamp( void ) {
	int		n = DSH_Count();
	int		vis = DSH_Visible();

	if ( s_dsh.sel >= n ) {
		s_dsh.sel = n - 1;
	}
	if ( s_dsh.sel < 0 ) {
		s_dsh.sel = 0;
	}
	if ( s_dsh.sel < s_dsh.top ) {
		s_dsh.top = s_dsh.sel;
	}
	if ( s_dsh.sel >= s_dsh.top + vis ) {
		s_dsh.top = s_dsh.sel - vis + 1;
	}
	if ( s_dsh.top < 0 ) {
		s_dsh.top = 0;
	}
}

static void DSH_Tabs( float x, float w ) {
	static const char	*names[2] = { "HALL OF FAME", "RIVALS" };
	int					t;
	float				tw = w / 2;
	qboolean			hit;

	for ( t = 0; t < 2; t++ ) {
		hit = UI_CursorInRect( (int)( x + t * tw ), 66, (int)tw - 4, 26 );
		RL_Frame( x + t * tw, 66, tw - 4, 26, t == s_dsh.tab ? rl_colPanelHi : rl_colPanel,
			t == s_dsh.tab ? rl_colOrange : ( hit ? rl_colBone : rl_colLine ) );
		RL_Text( x + t * tw + ( tw - 4 ) / 2, 69, names[t], 7, 11, t == s_dsh.tab ? rl_colOrange : rl_colBone, 1 );
		RL_Text( x + t * tw + ( tw - 4 ) / 2, 82, t == 0 ? va( "%i NEMESES, %i SLAIN", s_dsh.numHall, s_dsh.slain )
			: va( "%i BOTS REMEMBER YOU", s_dsh.numBots ), 5, 8, rl_colDim, 1 );
	}
}

static void DSH_HallRow( int row, float x, float y, float w, qboolean picked ) {
	dshHall_t	*h = DSH_HallAt( row );
	const float	*col;
	char		fateBuf[96];
	char		pre[96];
	int			room;
	float		tx = x + 30;

	RL_Frame( x, y, w, DSH_ROW_H - 3, picked ? rl_colPanelHi : rl_colPanel, picked ? rl_colOrange : rl_colLine );
	if ( h->icon ) {
		RL_Pic( x + 3, y + 2, 20, 20, h->icon, NULL );
	}
	RL_TextS( tx, y + 2, h->title[0] ? va( "%s %s", h->name, h->title ) : h->name, 7, 11, rl_colBone, 0 );
	Q_strncpyz( fateBuf, DSH_Fate( h, &col ), sizeof( fateBuf ) );		// (va() buffers rotate: copy first)
	Q_strupr( fateBuf );
	Com_sprintf( pre, sizeof( pre ), "RUN #%i  FLOOR %i  -  %i KILLS ON YOU  -  ", h->runNo, h->floorRose, h->kills );
	RL_Text( tx, y + 15, pre, 5, 7, rl_colDim, 0 );
	room = (int)( ( x + w - 8 - 6 * 9 - 6 - ( tx + 5 * strlen( pre ) ) ) / 5 );		// up to the level pips
	if ( room < 0 ) {
		room = 0;
	}
	if ( room < (int)strlen( fateBuf ) ) {
		fateBuf[room] = 0;
	}
	RL_Text( tx + 5 * strlen( pre ), y + 15, fateBuf, 5, 7, col, 0 );
	RL_Text( x + w - 8, y + 3, va( "LV %i", h->peak ), 7, 11, h->peak >= 5 ? dsh_colRed : rl_colOrange, 2 );
	DSH_Pips( x + w - 8 - 6 * 9, y + 16, h->peak, 6, 6, h->peak >= 5 ? dsh_colRed : rl_colOrange );
}

static void DSH_RivalRow( int row, float x, float y, float w, qboolean picked ) {
	dshBot_t	*b = &s_dsh.bots[s_dsh.botOrder[row]];
	const float	*col = rl_colBone;
	const char	*mood = "";
	float		tx = x + 30;
	char		date[16];

	RL_Frame( x, y, w, DSH_ROW_H - 3, picked ? rl_colPanelHi : rl_colPanel, picked ? rl_colOrange : rl_colLine );
	if ( b->icon ) {
		RL_Pic( x + 3, y + 2, 20, 20, b->icon, NULL );
	}
	if ( b->nem > 0 ) {
		col = dsh_colRed;
	} else if ( !Q_stricmp( DSH_TierName( b ), "ARCH-RIVAL" ) ) {
		col = dsh_colPurple;
	}
	RL_TextS( tx, y + 2, b->name, 7, 11, rl_colBone, 0 );
	RL_Text( tx + 7 * strlen( b->name ) + 10, y + 4, DSH_TierName( b ), 5, 8, col, 0 );
	if ( BM_FEAR( b->k, b->d ) ) {
		mood = "  -  FEARS YOU";
	} else if ( BM_GRUDGE( b->k, b->d ) ) {
		mood = "  -  HOLDS A GRUDGE";
	}
	Q_strncpyz( date, DSH_Date( b->lastDate ), sizeof( date ) );		// (DSH_Date is a va(): copy first)
	RL_Text( tx, y + 15, va( "%i FLOOR%s  -  %i RUN%s  -  LAST MET %s%s", b->fm, b->fm == 1 ? "" : "S", b->rm, b->rm == 1 ? "" : "S", date, mood ), 5, 7, rl_colDim, 0 );
	RL_Text( x + w - 8, y + 3, va( "YOU %i - %i", b->d, b->k ), 7, 11, b->d > b->k ? rl_colGood : b->d < b->k ? dsh_colRed : rl_colOrange, 2 );
}

static float DSH_Line( float x, float y, float w, const char *label, const char *value, const float *col ) {
	RL_Text( x + 8, y, label, 5, 8, rl_colDim, 0 );
	RL_Text( x + w - 8, y, value, 6, 9, col ? col : rl_colBone, 2 );
	return y + 13;
}

static void DSH_Details( float x, float y, float w, float h ) {
	dshHall_t	*hl = NULL;
	dshBot_t	*b = NULL;
	const float	*col;
	char		fate[96];
	char		date[16];
	float		ty;

	RL_Frame( x, y, w, h, rl_colPanel, rl_colLine );
	if ( !DSH_Count() ) {
		RL_TextWrap( x + 10, y + 12, w - 20, s_dsh.tab == DSH_TAB_HALL
			? "No nemesis yet. A bot that kills you three times in a descent takes the title."
			: "No bot remembers you yet. Play a DESCENT floor.", 6, 9, rl_colDim, 6 );
		return;
	}
	if ( s_dsh.tab == DSH_TAB_HALL ) {
		hl = DSH_HallAt( s_dsh.sel );
		b = DSH_Bot( hl->name );
	} else {
		b = &s_dsh.bots[s_dsh.botOrder[s_dsh.sel]];
	}
	ty = y + 8;
	if ( ( hl && hl->icon ) || ( b && b->icon ) ) {
		RL_Pic( x + w / 2 - 24, ty, 48, 48, hl ? hl->icon : b->icon, NULL );
		ty += 54;
	}
	RL_TextS( x + w / 2, ty, hl ? hl->name : b->name, 10, 15, rl_colBone, 1 );
	ty += 18;
	if ( hl ) {
		if ( hl->title[0] ) {
			RL_Text( x + w / 2, ty, hl->title, 6, 9, rl_colOrange, 1 );
		}
		ty += 14;
		RL_Text( x + 8, ty, "PEAK LEVEL", 5, 8, rl_colDim, 0 );
		DSH_Pips( x + w - 8 - 6 * 10, ty, hl->peak, 6, 8, hl->peak >= 5 ? dsh_colRed : rl_colOrange );
		ty += 13;
		ty = DSH_Line( x, ty, w, "KILLS ON YOU", va( "%i", hl->kills ), NULL );
		ty = DSH_Line( x, ty, w, "ROSE", va( "RUN #%i, FLOOR %i", hl->runNo, hl->floorRose ), NULL );
		ty = DSH_Line( x, ty, w, "DATE", DSH_Date( hl->date ), NULL );
		Q_strncpyz( fate, DSH_Fate( hl, &col ), sizeof( fate ) );
		Q_strupr( fate );
		RL_Text( x + 8, ty, "HOW IT ENDED", 5, 8, rl_colDim, 0 );
		ty += 11;
		ty = RL_TextWrap( x + 8, ty, w - 16, fate, 6, 9, col, 3 ) + 6;
	} else {
		RL_Text( x + w / 2, ty, DSH_TierName( b ), 6, 9, b->nem > 0 ? dsh_colRed : rl_colOrange, 1 );
		ty += 18;
	}
	if ( !b ) {
		return;
	}
	UI_FillRect( x + 6, ty, w - 12, 1, rl_colLine );
	ty += 5;
	RL_Text( x + w / 2, ty, "LIFETIME, YOU AND IT", 5, 8, rl_colDim, 1 );
	ty += 13;
	ty = DSH_Line( x, ty, w, "YOUR KILLS ON IT", va( "%i", b->d ), rl_colGood );
	ty = DSH_Line( x, ty, w, "ITS KILLS ON YOU", va( "%i", b->k ), dsh_colRed );
	ty = DSH_Line( x, ty, w, "FLOORS / RUNS MET", va( "%i / %i", b->fm, b->rm ), NULL );
	ty = DSH_Line( x, ty, w, "TIMES YOUR NEMESIS", b->nem ? va( "%i (PEAK LV %i)", b->nem, b->nemPeak ) : "0", b->nem ? dsh_colRed : NULL );
	ty = DSH_Line( x, ty, w, "HEADSHOTS ON IT", va( "%i", b->hs ), NULL );
	ty = DSH_Line( x, ty, w, "REVENGES (YOURS / ITS)", va( "%i / %i", b->rv, b->brv ), NULL );
	Q_strncpyz( date, DSH_Date( b->lastDate ), sizeof( date ) );		// (DSH_Date is a va(): copy first)
	ty = DSH_Line( x, ty, w, "LAST MET", b->lastDate ? va( "%s, %s %i-%i", date, b->lastRes == 1 ? "WON" : "LOST",
		b->lastD, b->lastK ) : "-", NULL );
	if ( BM_FEAR( b->k, b->d ) ) {
		RL_TextWrap( x + 8, ty + 4, w - 16, "It fears you: it backs off sooner and avoids you.", 5, 8, rl_colGood, 3 );
	} else if ( BM_GRUDGE( b->k, b->d ) ) {
		RL_TextWrap( x + 8, ty + 4, w - 16, "It holds a grudge: it hunts you from its first minute on a floor.", 5, 8, dsh_colRed, 3 );
	}
}

static void DSH_Draw( void ) {
	float	x;
	float	w;
	float	dx;
	float	dw;
	float	y;
	int		i;
	int		n;
	int		vis;

	RL_Backdrop( !RL_InGame() );
	RL_Header( "NEMESIS HALL", "The bots that hunted you, and the ones that remember you." );
	DSH_Geometry( &x, &w, &dx, &dw );
	DSH_Clamp();
	DSH_Tabs( x, w );
	n = DSH_Count();
	vis = DSH_Visible();

	// mouse hover picks the row (only when the mouse moved: the keys keep theirs)
	if ( uis.cursorx != s_dsh.lastMouseX || uis.cursory != s_dsh.lastMouseY ) {
		s_dsh.lastMouseX = uis.cursorx;
		s_dsh.lastMouseY = uis.cursory;
		for ( i = 0; i < vis && s_dsh.top + i < n; i++ ) {
			if ( UI_CursorInRect( (int)x, DSH_LIST_TOP + i * DSH_ROW_H, (int)w, DSH_ROW_H - 3 ) ) {
				s_dsh.sel = s_dsh.top + i;
			}
		}
	}
	if ( !n ) {
		RL_Text( x + w / 2, 200, s_dsh.tab == DSH_TAB_HALL ? "No nemesis has risen yet." : "Nobody remembers you yet.", 7, 11, rl_colDim, 1 );
	}
	for ( i = 0; i < vis && s_dsh.top + i < n; i++ ) {
		y = DSH_LIST_TOP + i * DSH_ROW_H;
		if ( s_dsh.tab == DSH_TAB_HALL ) {
			DSH_HallRow( s_dsh.top + i, x, y, w, s_dsh.top + i == s_dsh.sel );
		} else {
			DSH_RivalRow( s_dsh.top + i, x, y, w, s_dsh.top + i == s_dsh.sel );
		}
	}
	if ( n > vis ) {
		RL_Text( x + w - 4, DSH_LIST_BOTTOM - 2, va( "%i-%i OF %i", s_dsh.top + 1, s_dsh.top + vis < n ? s_dsh.top + vis : n, n ), 5, 8, rl_colDim, 2 );
	}
	DSH_Details( dx, 66, dw, 366 );
	Menu_Draw( &s_dsh.menu );
	RL_Footer( "TAB: SWITCH LIST   W S / WHEEL: PICK   ESC: BACK" );
}

/*
===============================================================================

INPUT

===============================================================================
*/

static void DSH_SetTab( int tab ) {
	s_dsh.tab = tab & 1;
	s_dsh.sel = 0;
	s_dsh.top = 0;
}

static sfxHandle_t DSH_Key( int key ) {
	float	x;
	float	w;
	float	dx;
	float	dw;
	int		t;
	int		i;
	int		sel = s_dsh.sel;

	DSH_Geometry( &x, &w, &dx, &dw );
	switch ( key ) {
	case K_TAB:
	case 'q':
	case 'e':
	case K_PAD0_LEFTSHOULDER:
	case K_PAD0_RIGHTSHOULDER:
		DSH_SetTab( s_dsh.tab + 1 );
		return menu_move_sound;
	case 'w':
	case K_UPARROW:
	case K_PAD0_DPAD_UP:
	case K_PAD0_LEFTSTICK_UP:
	case K_MWHEELUP:
		sel--;
		break;
	case 's':
	case K_DOWNARROW:
	case K_PAD0_DPAD_DOWN:
	case K_PAD0_LEFTSTICK_DOWN:
	case K_MWHEELDOWN:
		sel++;
		break;
	case K_PGUP:
		sel -= DSH_Visible();
		break;
	case K_PGDN:
		sel += DSH_Visible();
		break;
	case K_MOUSE1:
		for ( t = 0; t < 2; t++ ) {
			if ( UI_CursorInRect( (int)( x + t * w / 2 ), 66, (int)( w / 2 ) - 4, 26 ) ) {
				if ( t != s_dsh.tab ) {
					DSH_SetTab( t );
				}
				return menu_move_sound;
			}
		}
		for ( i = 0; i < DSH_Visible() && s_dsh.top + i < DSH_Count(); i++ ) {
			if ( UI_CursorInRect( (int)x, DSH_LIST_TOP + i * DSH_ROW_H, (int)w, DSH_ROW_H - 3 ) ) {
				s_dsh.sel = s_dsh.top + i;
				return menu_move_sound;
			}
		}
		return Menu_DefaultKey( &s_dsh.menu, key );
	case K_ESCAPE:
	case K_MOUSE2:
	case K_PAD0_B:
	case K_PAD0_BACK:
		UI_PopMenu();
		return menu_out_sound;
	default:
		return Menu_DefaultKey( &s_dsh.menu, RL_KeyRemap( key ) );		// Space / Enter / pad A: BACK
	}
	if ( sel < 0 ) {
		sel = 0;
	}
	if ( sel > DSH_Count() - 1 ) {
		sel = DSH_Count() - 1;
	}
	if ( sel != s_dsh.sel && sel >= 0 ) {
		s_dsh.sel = sel;
		DSH_Clamp();
		return menu_move_sound;
	}
	return 0;
}

static void DSH_Event( void *ptr, int event ) {
	if ( event == QM_ACTIVATED ) {
		UI_PopMenu();
	}
}

void UI_DS_HallMenu( int tab ) {
	memset( &s_dsh, 0, sizeof( s_dsh ) );
	RL_SetupMenu( &s_dsh.menu, DSH_Draw, RL_InGame() );
	s_dsh.menu.key = DSH_Key;
	DSH_Load();
	s_dsh.lastMouseX = uis.cursorx;
	s_dsh.lastMouseY = uis.cursory;
	RL_InitButton( &s_dsh.back, ID_RL_BACK, 8 - rl_ext, 440, 120, 28, "BACK", DSH_Event );
	s_dsh.back.center = qtrue;
	Menu_AddItem( &s_dsh.menu, &s_dsh.back );
	DSH_SetTab( tab );
	UI_PushMenu( &s_dsh.menu );
	Com_Printf( "DSHall: %i nemeses (%i slain), %i bots remembered\n", s_dsh.numHall, s_dsh.slain, s_dsh.numBots );
}

// Records: the compact NEMESIS HALL button (bone text, 6x10: fits between BACK and RUN HISTORY on 4:3)
static void DSH_ButtonDraw( void *self ) {
	rlButton_t		*b = (rlButton_t *)self;
	menucommon_s	*g = &b->bm.generic;
	float			x = g->left;
	float			y = g->top;
	float			w = g->right - g->left;
	float			h = g->bottom - g->top;
	qboolean		focus = Menu_ItemAtCursor( g->parent ) == self;

	RL_Frame( x, y, w, h, focus ? rl_colPanelHi : rl_colPanel, focus ? rl_colOrange : rl_colLine );
	if ( focus ) {
		UI_FillRect( x, y, 3, h, rl_colOrange );
	}
	RL_Text( x + w / 2, y + ( h - 10 ) / 2, b->label, 6, 10, focus ? rl_colOrange : rl_colBone, 1 );
}

void UI_DS_HallButton( rlButton_t *b ) {
	b->bm.generic.ownerdraw = DSH_ButtonDraw;
}

// dshall [rivals | records]: opens Records > NEMESIS HALL (tests, the console)
qboolean UI_DS_HallCommand( const char *cmd ) {
	int		tab;

	if ( Q_stricmp( cmd, "dshall" ) ) {
		return qfalse;
	}
	tab = !Q_stricmp( UI_Argv( 1 ), "rivals" ) ? DSH_TAB_RIVALS : !Q_stricmp( UI_Argv( 1 ), "records" ) ? -1 : DSH_TAB_HALL;
	UI_RL_HubMenu();
	UI_DS_RecordsMenu();
	if ( tab >= 0 ) {
		UI_DS_HallMenu( tab );		// (dshall records: stop at the Records screen and its NEMESIS HALL button)
	}
	return qtrue;
}
