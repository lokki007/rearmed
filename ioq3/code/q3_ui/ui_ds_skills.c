/*
===========================================================================
REARMED -- DESCENT skills, UI side (owner SKILLS).

The SKILLS screen (from the DESCENT hub): ten constellations, one per skill, perks
as stars (taken / available / locked), skill level + xp bar per tree, the perk
point counter, a details panel with the exact numbers, spending with click /
SPACE / gamepad A, a per-tree respec for skulls. Also the merge of the game's
pending skill xp (rearmed_skills.txt) into rearmed_meta.txt, exactly once per
batch (seq vs sk_mseq), and the "skills trained" block of the run summary.
Contract and file keys: game/bg_ds_skills.h.
===========================================================================
*/
#include "ui_local.h"
#include "ui_ds_meta.h"

#define SKU_WIDE_SPAN		1200	// this much room (640 space, very wide screens): all ten trees in one row
#define SKU_STAR_HIT		9		// click / hover radius of a star (640 space)
#define SKU_RESPEC_BASE		25		// skulls: 25 + 5 per rank given back (agreed with ds-economy)
#define SKU_RESPEC_RANK		5

static rlKV_t	sku_file;		// rearmed_skills.txt

typedef struct {
	menuframework_s	menu;
	rlButton_t		back;
	rlButton_t		respec;
	int				xp[SK_NUM];
	int				levels[SK_NUM];
	int				ranks[SKP_NUM];
	int				tree;			// focused skill
	int				perk;			// focused perk (dsPerk_t)
	char			msg[80];
	int				msgTime;
	qboolean		msgGood;
	int				lastMouseX, lastMouseY;
	int				takenTime[SKP_NUM];	// a star just lit up (flash)
	char			confirm[96];
	// layout (640 space)
	float			ext;			// this screen's own side room (wider than rl_ext on 32:9)
	int				cols, rows;
	float			left, right, top, bottom, gap, tw, th;
} skuMenu_t;

static skuMenu_t	s_sku;
static sfxHandle_t	sku_perkSound, sku_respecSound, sku_lockedSound;

/*
=======================================================================

DATA

=======================================================================
*/

static void SKU_ReadCsv( rlKV_t *kv, const char *key, int *out, int n ) {
	int		i;

	for ( i = 0; i < n; i++ ) {
		out[i] = 0;
	}
	RL_ParseInts( RL_KVGet( kv, key ), out, n );
}

static const char *SKU_Csv( const int *v, int n ) {
	static char	buf[2][512];
	static int	index;
	char		*b;
	int			i;

	b = buf[index++ & 1];
	b[0] = 0;
	for ( i = 0; i < n; i++ ) {
		Q_strcat( b, 512, va( i ? ",%i" : "%i", v[i] ) );
	}
	return b;
}

/*
=================
UI_DS_SkillsMerge

The game's pending skill xp into the meta file, once per batch. Leaves rl_meta loaded.
=================
*/
qboolean UI_DS_SkillsMerge( void ) {
	int		xp[SK_NUM];
	int		add[SK_NUM];
	int		seq;
	int		i;

	RL_LoadMeta();
	RL_KVLoad( &sku_file, SKILLS_FILE );
	seq = RL_KVInt( &sku_file, "seq" );
	if ( seq <= RL_KVInt( &rl_meta, "sk_mseq" ) ) {
		return qfalse;
	}
	SKU_ReadCsv( &rl_meta, "sk_xp", xp, SK_NUM );
	SKU_ReadCsv( &sku_file, "xp", add, SK_NUM );
	for ( i = 0; i < SK_NUM; i++ ) {
		if ( add[i] > 0 ) {
			xp[i] += add[i];
		}
	}
	RL_KVSet( &rl_meta, "sk_xp", SKU_Csv( xp, SK_NUM ) );
	RL_KVSetInt( &rl_meta, "sk_mseq", seq );
	RL_SaveMeta();
	Com_Printf( "SKILLS: merged batch %i (+%s), totals %s\n", seq, SKU_Csv( add, SK_NUM ), SKU_Csv( xp, SK_NUM ) );
	return qtrue;
}

static void SKU_Load( void ) {
	int		i;

	UI_DS_SkillsMerge();
	SKU_ReadCsv( &rl_meta, "sk_xp", s_sku.xp, SK_NUM );
	SKU_ReadCsv( &rl_meta, "sk_perks", s_sku.ranks, SKP_NUM );
	for ( i = 0; i < SK_NUM; i++ ) {
		s_sku.levels[i] = BG_DS_SkillLevel( s_sku.xp[i], NULL, NULL );
	}
	for ( i = 0; i < SKP_NUM; i++ ) {
		if ( s_sku.ranks[i] < 0 ) {
			s_sku.ranks[i] = 0;
		}
		else if ( s_sku.ranks[i] > ds_perks[i].maxRank ) {
			s_sku.ranks[i] = ds_perks[i].maxRank;
		}
	}
}

static int SKU_Points( void ) {
	return BG_DS_PerkPoints( s_sku.levels, s_sku.ranks );
}

static int SKU_TreeRanks( int s ) {
	int		p;
	int		n;

	n = 0;
	for ( p = s * SKP_PER_SKILL; p < ( s + 1 ) * SKP_PER_SKILL; p++ ) {
		n += s_sku.ranks[p];
	}
	return n;
}

// 0 can take a rank, 1 maxed, 2 needs a parent, 3 needs a skill level, 4 no points
static int SKU_PerkState( int p ) {
	const dsPerkInfo_t	*pi;
	int					r;

	pi = &ds_perks[p];
	r = s_sku.ranks[p];
	if ( r >= pi->maxRank ) {
		return 1;
	}
	if ( !BG_DS_PerkOpen( s_sku.ranks, p ) ) {
		return 2;
	}
	if ( s_sku.levels[pi->skill] < pi->req[r] ) {
		return 3;
	}
	if ( SKU_Points() <= 0 ) {
		return 4;
	}
	return 0;
}

static void SKU_Msg( const char *text, qboolean good ) {
	Q_strncpyz( s_sku.msg, text, sizeof( s_sku.msg ) );
	s_sku.msgTime = uis.realtime;
	s_sku.msgGood = good;
}

static sfxHandle_t SKU_Take( int p ) {
	const dsPerkInfo_t	*pi;
	int					st;

	if ( p < 0 || p >= SKP_NUM ) {
		return 0;
	}
	pi = &ds_perks[p];
	SKU_Load();		// fresh: the game may have trained since
	st = SKU_PerkState( p );
	switch ( st ) {
	case 1:
		SKU_Msg( va( "%s IS MAXED", pi->name ), qfalse );
		return sku_lockedSound;
	case 2:
		SKU_Msg( va( "TAKE %s FIRST", ds_perks[pi->parent[0]].name ), qfalse );
		return sku_lockedSound;
	case 3:
		SKU_Msg( va( "NEEDS %s %i", ds_skills[pi->skill].name, pi->req[s_sku.ranks[p]] ), qfalse );
		return sku_lockedSound;
	case 4:
		SKU_Msg( "NO PERK POINTS: TRAIN YOUR SKILLS", qfalse );
		return sku_lockedSound;
	}
	s_sku.ranks[p]++;
	RL_KVSet( &rl_meta, "sk_perks", SKU_Csv( s_sku.ranks, SKP_NUM ) );
	RL_SaveMeta();
	s_sku.takenTime[p] = uis.realtime;
	SKU_Msg( va( "%s RANK %i: %s", pi->name, s_sku.ranks[p], BG_DS_PerkText( p, s_sku.ranks[p] ) ), qtrue );
	Com_Printf( "SKILLS: took %s rank %i (%i points left)\n", pi->name, s_sku.ranks[p], SKU_Points() );
	return sku_perkSound;
}

static int SKU_RespecCost( int s ) {
	return SKU_RESPEC_BASE + SKU_RESPEC_RANK * SKU_TreeRanks( s );
}

static void SKU_RespecAction( qboolean yes ) {
	int		s;
	int		p;
	int		cost;
	int		n;
	int		skulls;

	if ( !yes ) {
		return;
	}
	s = s_sku.tree;
	SKU_Load();
	n = SKU_TreeRanks( s );
	cost = SKU_RespecCost( s );
	skulls = RL_KVInt( &rl_meta, "skulls" );
	if ( !n ) {
		SKU_Msg( "NOTHING TO GIVE BACK", qfalse );
		return;
	}
	if ( skulls < cost ) {
		SKU_Msg( va( "NEED %i MORE SKULLS", cost - skulls ), qfalse );
		trap_S_StartLocalSound( sku_lockedSound, CHAN_LOCAL_SOUND );
		return;
	}
	for ( p = s * SKP_PER_SKILL; p < ( s + 1 ) * SKP_PER_SKILL; p++ ) {
		s_sku.ranks[p] = 0;
	}
	RL_KVSetInt( &rl_meta, "skulls", skulls - cost );
	RL_KVSetInt( &rl_meta, "sk_respecs", RL_KVInt( &rl_meta, "sk_respecs" ) + 1 );
	RL_KVSet( &rl_meta, "sk_perks", SKU_Csv( s_sku.ranks, SKP_NUM ) );
	RL_SaveMeta();
	SKU_Msg( va( "%s: %i POINT%s BACK FOR %i SKULLS", ds_skills[s].name, n, n == 1 ? "" : "S", cost ), qtrue );
	Com_Printf( "SKILLS: respec %s, %i ranks back, %i skulls\n", ds_skills[s].name, n, cost );
	trap_S_StartLocalSound( sku_respecSound, CHAN_LOCAL_SOUND );
}

static void SKU_RespecDraw( void ) {
	UI_DrawProportionalString( 320, 230, "EVERY PERK OF THIS TREE GIVES ITS POINTS BACK", UI_CENTER|UI_SMALLFONT, color_red );
}

static void SKU_Respec( void ) {
	int		s = s_sku.tree;

	SKU_Load();
	if ( !SKU_TreeRanks( s ) ) {
		SKU_Msg( va( "NO %s PERKS TO GIVE BACK", ds_skills[s].name ), qfalse );
		trap_S_StartLocalSound( sku_lockedSound, CHAN_LOCAL_SOUND );
		return;
	}
	Com_sprintf( s_sku.confirm, sizeof( s_sku.confirm ), "RESPEC %s FOR %i SKULLS?", ds_skills[s].name, SKU_RespecCost( s ) );
	UI_ConfirmMenu( s_sku.confirm, SKU_RespecDraw, SKU_RespecAction );
}

/*
=======================================================================

LAYOUT

=======================================================================
*/

static void SKU_Layout( void ) {
	float	e;

	// the whole width of a very wide screen (rl_ext stops at 160): 32:9 shows the ten trees in a row
	e = uis.bias / uis.xscale;
	if ( e < 0 ) {
		e = 0;
	}
	s_sku.ext = e * 0.9f > 480 ? 480 : e * 0.9f;
	if ( s_sku.ext < rl_ext ) {
		s_sku.ext = rl_ext;
	}
	s_sku.left = 8 - s_sku.ext;
	s_sku.right = 632 + s_sku.ext;
	s_sku.cols = ( s_sku.right - s_sku.left >= SKU_WIDE_SPAN ) ? SK_NUM : SK_NUM / 2;
	s_sku.rows = SK_NUM / s_sku.cols;
	s_sku.top = 66;
	s_sku.bottom = 372;
	s_sku.gap = 6;
	s_sku.tw = ( s_sku.right - s_sku.left - s_sku.gap * ( s_sku.cols - 1 ) ) / s_sku.cols;
	s_sku.th = ( s_sku.bottom - s_sku.top - s_sku.gap * ( s_sku.rows - 1 ) ) / s_sku.rows;
}

static void SKU_TreeRect( int s, float *x, float *y ) {
	*x = s_sku.left + ( s % s_sku.cols ) * ( s_sku.tw + s_sku.gap );
	*y = s_sku.top + ( s / s_sku.cols ) * ( s_sku.th + s_sku.gap );
}

static void SKU_StarPos( int p, float *sx, float *sy ) {
	float	bx;
	float	by;
	float	x0;
	float	x1;
	float	y0;
	float	y1;

	SKU_TreeRect( ds_perks[p].skill, &bx, &by );
	x0 = bx + 14;
	x1 = bx + s_sku.tw - 14;
	y0 = by + 34;
	y1 = by + s_sku.th - 16;
	*sx = x0 + ds_perks[p].x * ( x1 - x0 );
	*sy = y1 - ds_perks[p].y * ( y1 - y0 );
}

static int SKU_StarAt( float mx, float my ) {
	int		p;
	float	sx;
	float	sy;
	float	best;
	float	d;
	int		bestP;

	bestP = -1;
	best = SKU_STAR_HIT * SKU_STAR_HIT;
	for ( p = 0; p < SKP_NUM; p++ ) {
		SKU_StarPos( p, &sx, &sy );
		d = ( sx - mx ) * ( sx - mx ) + ( sy - my ) * ( sy - my );
		if ( d < best ) {
			best = d;
			bestP = p;
		}
	}
	return bestP;
}

static void SKU_Focus( int p ) {
	if ( p >= 0 && p < SKP_NUM ) {
		s_sku.perk = p;
		s_sku.tree = ds_perks[p].skill;
	}
}

/*
=======================================================================

DRAWING

=======================================================================
*/

// a dotted line between two stars (the UI has no rotated lines)
static void SKU_Line( float x0, float y0, float x1, float y1, const float *color, float step, float size ) {
	float	dx;
	float	dy;
	float	len;
	float	t;
	int		n;
	int		i;

	dx = x1 - x0;
	dy = y1 - y0;
	len = sqrt( dx * dx + dy * dy );
	if ( len < 1 ) {
		return;
	}
	n = (int)( len / step );
	for ( i = 1; i < n; i++ ) {
		t = (float)i / n;
		UI_FillRect( x0 + dx * t - size * 0.5f, y0 + dy * t - size * 0.5f, size, size, color );
	}
}

static void SKU_Color( int s, float k, float alpha, vec4_t out ) {
	out[0] = ds_skills[s].color[0] * k;
	out[1] = ds_skills[s].color[1] * k;
	out[2] = ds_skills[s].color[2] * k;
	out[3] = alpha;
}

static void SKU_DrawTree( int s ) {
	float		bx;
	float		by;
	float		sx;
	float		sy;
	float		px;
	float		py;
	float		f;
	float		r;
	int			p;
	int			k;
	int			i;
	int			st;
	int			into;
	int			need;
	int			lvl;
	unsigned	seed;
	qboolean	focusTree;
	vec4_t		c;
	vec4_t		edge;

	SKU_TreeRect( s, &bx, &by );
	focusTree = ( s == s_sku.tree );
	SKU_Color( s, focusTree ? 0.9f : 0.35f, focusTree ? 1.0f : 0.8f, edge );
	RL_Frame( bx, by, s_sku.tw, s_sku.th, focusTree ? rl_colPanelHi : rl_colPanel, edge );

	// faint background stars (fixed per tree)
	seed = 1103u + s * 7919u;
	for ( i = 0; i < 26; i++ ) {
		seed = seed * 1664525u + 1013904223u;
		px = bx + 4 + ( ( seed >> 8 ) % 1000 ) / 1000.0f * ( s_sku.tw - 8 );
		seed = seed * 1664525u + 1013904223u;
		py = by + 30 + ( ( seed >> 8 ) % 1000 ) / 1000.0f * ( s_sku.th - 34 );
		c[0] = c[1] = 0.75f;
		c[2] = 0.9f;
		c[3] = 0.10f + 0.12f * ( ( seed >> 4 ) & 3 ) / 3.0f;
		UI_FillRect( px, py, 1, 1, c );
	}

	// name, level, xp bar
	lvl = BG_DS_SkillLevel( s_sku.xp[s], &into, &need );
	SKU_Color( s, 1, 1, c );
	RL_TextS( bx + 6, by + 5, ds_skills[s].name, 6, 9, c, 0 );
	RL_TextS( bx + s_sku.tw - 6, by + 4, va( "%i", lvl ), 8, 11, focusTree ? rl_colBone : rl_colDim, 2 );
	UI_FillRect( bx + 6, by + 18, s_sku.tw - 12, 3, rl_colBlack );
	SKU_Color( s, 0.9f, 0.9f, c );
	UI_FillRect( bx + 6, by + 18, ( s_sku.tw - 12 ) * ( lvl >= SK_MAX_LEVEL || need <= 0 ? 1.0f : (float)into / need ), 3, c );
	if ( focusTree ) {
		RL_Text( bx + 6, by + 23, lvl >= SK_MAX_LEVEL ? "MASTERED" : va( "%i / %i XP", into, need ), 4, 6, rl_colDim, 0 );
	}
	k = SKU_TreeRanks( s );
	if ( k ) {
		RL_Text( bx + s_sku.tw - 6, by + 23, va( "%i RANK%s", k, k == 1 ? "" : "S" ), 4, 6, rl_colDim, 2 );
	}

	// links
	for ( p = s * SKP_PER_SKILL; p < ( s + 1 ) * SKP_PER_SKILL; p++ ) {
		SKU_StarPos( p, &sx, &sy );
		for ( i = 0; i < 2; i++ ) {
			k = ds_perks[p].parent[i];
			if ( k < 0 ) {
				continue;
			}
			SKU_StarPos( k, &px, &py );
			if ( s_sku.ranks[k] && s_sku.ranks[p] ) {
				SKU_Color( s, 1, 0.85f, c );
				SKU_Line( px, py, sx, sy, c, 1.5f, 1.2f );
			}
			else if ( s_sku.ranks[k] ) {
				SKU_Color( s, 0.8f, 0.55f, c );
				SKU_Line( px, py, sx, sy, c, 3, 1 );
			}
			else {
				c[0] = c[1] = c[2] = 0.5f;
				c[3] = 0.25f;
				SKU_Line( px, py, sx, sy, c, 4, 1 );
			}
		}
	}

	// stars
	for ( p = s * SKP_PER_SKILL; p < ( s + 1 ) * SKP_PER_SKILL; p++ ) {
		SKU_StarPos( p, &sx, &sy );
		st = SKU_PerkState( p );
		r = 4 + ds_perks[p].maxRank;
		f = 0;
		if ( s_sku.takenTime[p] && uis.realtime - s_sku.takenTime[p] < 900 ) {
			f = 1 - ( uis.realtime - s_sku.takenTime[p] ) / 900.0f;
		}
		if ( s_sku.ranks[p] > 0 ) {
			// lit: a halo and a bright core
			SKU_Color( s, 1, 0.55f + 0.45f * f, c );
			RL_Pic( sx - r * 3 - f * 12, sy - r * 3 - f * 12, r * 6 + f * 24, r * 6 + f * 24, rl_shGlow, c );
			c[0] = 0.5f + 0.5f * c[0];
			c[1] = 0.5f + 0.5f * c[1];
			c[2] = 0.5f + 0.5f * c[2];
			c[3] = 1;
			UI_FillRect( sx - 2, sy - 2, 4, 4, c );
			UI_FillRect( sx - 0.5f, sy - r, 1, r * 2, c );
			UI_FillRect( sx - r, sy - 0.5f, r * 2, 1, c );
		}
		else if ( st == 0 ) {
			// can be taken now: a pulsing ring
			SKU_Color( s, 1, 0.55f + 0.45f * RL_Pulse( 0.006f ), c );
			RL_Pic( sx - r - 1, sy - r - 1, r * 2 + 2, r * 2 + 2, rl_shRing, c );
			UI_FillRect( sx - 1, sy - 1, 2, 2, c );
		}
		else if ( st == 3 || st == 4 ) {
			// open, but the skill level (or a point) is missing
			SKU_Color( s, 0.55f, 0.6f, c );
			RL_Pic( sx - r, sy - r, r * 2, r * 2, rl_shRing, c );
			UI_FillRect( sx - 1, sy - 1, 2, 2, c );
			if ( st == 3 ) {
				RL_Text( sx, sy + r + 1, va( "%i", ds_perks[p].req[s_sku.ranks[p]] ), 4, 6, rl_colDim, 1 );
			}
		}
		else {
			// behind a perk not taken yet
			c[0] = c[1] = c[2] = 0.45f;
			c[3] = 0.5f;
			RL_Pic( sx - r + 1, sy - r + 1, r * 2 - 2, r * 2 - 2, rl_shRing, c );
		}
		// rank pips under multi-rank perks
		if ( ds_perks[p].maxRank > 1 && ( s_sku.ranks[p] > 0 || st != 2 ) ) {
			for ( i = 0; i < ds_perks[p].maxRank; i++ ) {
				px = sx - ( ds_perks[p].maxRank * 4 ) / 2.0f + i * 4 + 0.5f;
				if ( i < s_sku.ranks[p] ) {
					SKU_Color( s, 1, 1, c );
				}
				else {
					c[0] = c[1] = c[2] = 0.3f;
					c[3] = 0.8f;
				}
				UI_FillRect( px, sy - r - 5, 3, 2, c );
			}
		}
		// the focused star
		if ( p == s_sku.perk ) {
			f = 0.6f + 0.4f * RL_Pulse( 0.008f );
			c[0] = c[1] = c[2] = 1;
			c[3] = f;
			RL_Pic( sx - r - 5, sy - r - 5, r * 2 + 10, r * 2 + 10, rl_shRing, c );
		}
	}
}

static void SKU_DrawDetails( void ) {
	const dsPerkInfo_t	*pi;
	float				x;
	float				y;
	float				w;
	int					p;
	int					s;
	int					r;
	int					st;
	int					lvl;
	int					into;
	int					need;
	const char			*right;
	const float			*rc;
	vec4_t				c;

	x = s_sku.left;
	y = 377;
	w = s_sku.right - s_sku.left;
	p = s_sku.perk;
	pi = &ds_perks[p];
	s = pi->skill;
	r = s_sku.ranks[p];
	st = SKU_PerkState( p );
	RL_Frame( x, y, w, 56, rl_colPanel, rl_colLine );

	// the perk
	SKU_Color( s, 1, 1, c );
	RL_TextS( x + 10, y + 5, pi->name, 8, 12, c, 0 );
	RL_Text( x + 10 + ( strlen( pi->name ) + 1 ) * 8, y + 8, va( "RANK %i / %i", r, pi->maxRank ), 6, 9, r ? rl_colBone : rl_colDim, 0 );
	switch ( st ) {
	case 0:
		right = va( "SPACE, CLICK OR A: TAKE RANK %i", r + 1 );
		rc = rl_colGood;
		break;
	case 1:
		right = "MAXED";
		rc = rl_colGood;
		break;
	case 2:
		right = pi->parent[1] >= 0 ? va( "NEEDS %s OR %s", ds_perks[pi->parent[0]].name, ds_perks[pi->parent[1]].name )
			: va( "NEEDS %s", ds_perks[pi->parent[0]].name );
		rc = rl_colBad;
		break;
	case 3:
		right = va( "NEEDS %s %i (YOU: %i)", ds_skills[s].name, pi->req[r], s_sku.levels[s] );
		rc = rl_colBad;
		break;
	default:
		right = "NO PERK POINTS LEFT";
		rc = rl_colBad;
		break;
	}
	RL_TextS( x + w - 10, y + 7, right, 6, 9, rc, 2 );

	if ( r > 0 ) {
		RL_Text( x + 10, y + 22, va( "NOW: %s", BG_DS_PerkText( p, r ) ), 6, 9, rl_colGood, 0 );
	}
	if ( r < pi->maxRank ) {
		RL_Text( r > 0 ? x + w * 0.45f : x + 10, y + 22, va( "RANK %i: %s  (%s %i)", r + 1, BG_DS_PerkText( p, r + 1 ),
			ds_skills[s].name, pi->req[r] ), 6, 9, st == 0 ? rl_colBone : rl_colDim, 0 );
	}

	// the skill
	lvl = BG_DS_SkillLevel( s_sku.xp[s], &into, &need );
	RL_Text( x + 10, y + 40, va( "%s %i  %s  TRAINS WITH: %s", ds_skills[s].name, lvl,
		lvl >= SK_MAX_LEVEL ? "(MASTERED)" : va( "(%i / %i XP TO %i)", into, need, lvl + 1 ), ds_skills[s].trains ), 5, 8, rl_colDim, 0 );
	RL_Text( x + w - 10, y + 40, "PERKS WORK IN DESCENT, FOR EVERY CHARACTER", 5, 8, rl_colDark, 2 );
}

// the respec button: "RESPEC <TREE>" and its price on one line
static void SKU_RespecButtonDraw( void *self ) {
	rlButton_t		*b;
	menucommon_s	*g;
	float			x;
	float			y;
	float			w;
	float			h;
	qboolean		focus;
	qboolean		gray;
	vec4_t			c;

	b = (rlButton_t *)self;
	g = &b->bm.generic;
	x = g->left;
	y = g->top;
	w = g->right - g->left;
	h = g->bottom - g->top;
	focus = ( Menu_ItemAtCursor( g->parent ) == self );
	gray = ( g->flags & QMF_GRAYED ) != 0;
	RL_Frame( x, y, w, h, focus && !gray ? rl_colPanelHi : rl_colPanel, gray ? rl_colDark : focus ? rl_colOrange : rl_colLine );
	if ( gray ) {
		Vector4Copy( rl_colDark, c );
	}
	else {
		SKU_Color( s_sku.tree, 1, 1, c );
	}
	RL_TextS( x + 8, y + h / 2 - 5, va( "RESPEC %s", ds_skills[s_sku.tree].name ), 7, 10, c, 0 );
	RL_Text( x + w - 8, y + h / 2 - 4, va( "%i SKULLS", SKU_RespecCost( s_sku.tree ) ), 5, 8,
		gray ? rl_colDark : RL_KVInt( &rl_meta, "skulls" ) >= SKU_RespecCost( s_sku.tree ) ? rl_colOrange : rl_colBad, 2 );
}

static void SKU_MenuDraw( void ) {
	int		s;
	int		p;
	int		cl;
	int		pts;
	int		gained;
	int		i;
	float	t;
	vec4_t	c;

	// mouse over a star focuses it (only when the mouse moved: keys keep their focus)
	if ( uis.cursorx != s_sku.lastMouseX || uis.cursory != s_sku.lastMouseY ) {
		s_sku.lastMouseX = uis.cursorx;
		s_sku.lastMouseY = uis.cursory;
		p = SKU_StarAt( uis.cursorx, uis.cursory );
		if ( p >= 0 ) {
			SKU_Focus( p );
		}
	}

	RL_Backdrop( !RL_InGame() );
	RL_Header( "SKILLS", "Train by doing, in every mode against bots. Perks work in DESCENT." );
	RL_TextS( s_sku.right, 16, va( "%i", RL_KVInt( &rl_meta, "skulls" ) ), 12, 18, rl_colOrange, 2 );
	RL_Text( s_sku.right, 36, "SKULLS", 6, 9, rl_colDim, 2 );

	// the character level and the points (top left)
	cl = BG_DS_CharLevel( s_sku.levels );
	pts = SKU_Points();
	gained = 0;
	for ( i = 0; i < SK_NUM; i++ ) {
		gained += s_sku.levels[i] - 1;
	}
	RL_TextS( s_sku.left, 12, va( "LEVEL %i", cl ), 10, 15, rl_colBone, 0 );
	if ( pts > 0 ) {
		Vector4Copy( rl_colGood, c );
		c[3] = 0.7f + 0.3f * RL_Pulse( 0.005f );
		RL_TextS( s_sku.left, 30, va( "%i PERK POINT%s", pts, pts == 1 ? "" : "S" ), 7, 11, c, 0 );
	}
	else {
		RL_Text( s_sku.left, 30, "NO PERK POINTS", 6, 9, rl_colDim, 0 );
	}
	RL_Text( s_sku.left, 44, va( "NEXT POINT IN %i SKILL LEVEL%s", BG_DS_CharLevelNeed( cl + 1 ) - gained,
		BG_DS_CharLevelNeed( cl + 1 ) - gained == 1 ? "" : "S" ), 4, 7, rl_colDim, 0 );

	for ( s = 0; s < SK_NUM; s++ ) {
		SKU_DrawTree( s );
	}
	SKU_DrawDetails();

	// the respec button (owner-drawn: the tree and its price)
	if ( SKU_TreeRanks( s_sku.tree ) ) {
		s_sku.respec.bm.generic.flags &= ~QMF_GRAYED;
	}
	else {
		s_sku.respec.bm.generic.flags |= QMF_GRAYED;
	}
	Menu_Draw( &s_sku.menu );

	if ( s_sku.msg[0] ) {
		t = ( uis.realtime - s_sku.msgTime ) / 2200.0f;
		if ( t < 1 ) {
			Vector4Copy( s_sku.msgGood ? rl_colGood : rl_colBad, c );
			c[3] = 1 - t * t;
			RL_TextS( 320, 444, s_sku.msg, 6, 10, c, 1 );
		}
	}
	RL_Text( 320, 466, "WASD MOVE   Q / E TREE   SPACE, CLICK, A TAKE   R, Y RESPEC", 5, 8, rl_colDark, 1 );
}

/*
=======================================================================

INPUT

=======================================================================
*/

// the nearest star in a direction (across trees)
static int SKU_Nearest( int from, float dx, float dy ) {
	float	fx;
	float	fy;
	float	sx;
	float	sy;
	float	along;
	float	side;
	float	score;
	float	best;
	int		p;
	int		bestP;

	SKU_StarPos( from, &fx, &fy );
	bestP = -1;
	best = 1e9f;
	for ( p = 0; p < SKP_NUM; p++ ) {
		if ( p == from ) {
			continue;
		}
		SKU_StarPos( p, &sx, &sy );
		along = ( sx - fx ) * dx + ( sy - fy ) * dy;
		side = ( sx - fx ) * dy - ( sy - fy ) * dx;
		if ( side < 0 ) {
			side = -side;
		}
		if ( along < 2 || side > along * 2.5f ) {
			continue;
		}
		score = along + side * 2;
		if ( score < best ) {
			best = score;
			bestP = p;
		}
	}
	return bestP;
}

static void SKU_SetTree( int s ) {
	int		p;
	int		best;

	s = ( s + SK_NUM ) % SK_NUM;
	// keep the same spot in the new tree: the perk with the same index
	p = s_sku.perk % SKP_PER_SKILL;
	best = s * SKP_PER_SKILL + p;
	SKU_Focus( best );
}

static sfxHandle_t SKU_MenuKey( int key ) {
	int		p;
	float	dx;
	float	dy;

	dx = dy = 0;
	switch ( key ) {
	case 'a':
	case K_LEFTARROW:
	case K_PAD0_DPAD_LEFT:
	case K_PAD0_LEFTSTICK_LEFT:
		dx = -1;
		break;
	case 'd':
	case K_RIGHTARROW:
	case K_PAD0_DPAD_RIGHT:
	case K_PAD0_LEFTSTICK_RIGHT:
		dx = 1;
		break;
	case 'w':
	case K_UPARROW:
	case K_PAD0_DPAD_UP:
	case K_PAD0_LEFTSTICK_UP:
		dy = -1;
		break;
	case 's':
	case K_DOWNARROW:
	case K_PAD0_DPAD_DOWN:
	case K_PAD0_LEFTSTICK_DOWN:
		dy = 1;
		break;
	case K_TAB:
		SKU_SetTree( s_sku.tree + ( trap_Key_IsDown( K_SHIFT ) ? -1 : 1 ) );
		return menu_move_sound;
	case 'e':
	case K_PAD0_RIGHTSHOULDER:
	case K_MWHEELDOWN:
		SKU_SetTree( s_sku.tree + 1 );
		return menu_move_sound;
	case 'q':
	case K_PAD0_LEFTSHOULDER:
	case K_MWHEELUP:
		SKU_SetTree( s_sku.tree - 1 );
		return menu_move_sound;
	case K_SPACE:
	case K_ENTER:
	case K_KP_ENTER:
	case K_PAD0_A:
		return SKU_Take( s_sku.perk );
	case 'r':
	case K_PAD0_Y:
		SKU_Respec();
		return menu_move_sound;
	case K_MOUSE1:
		p = SKU_StarAt( uis.cursorx, uis.cursory );
		if ( p >= 0 ) {
			if ( p == s_sku.perk ) {
				return SKU_Take( p );
			}
			SKU_Focus( p );
			return menu_move_sound;
		}
		return Menu_DefaultKey( &s_sku.menu, key );
	case K_ESCAPE:
	case K_MOUSE2:
	case K_PAD0_B:
	case K_PAD0_BACK:
		UI_PopMenu();
		rl_hubDirty = qtrue;
		return menu_out_sound;
	default:
		return Menu_DefaultKey( &s_sku.menu, key );
	}
	p = SKU_Nearest( s_sku.perk, dx, dy );
	if ( p >= 0 ) {
		SKU_Focus( p );
		return menu_move_sound;
	}
	return 0;
}

static void SKU_MenuEvent( void *ptr, int event ) {
	if ( event != QM_ACTIVATED ) {
		return;
	}
	switch ( ((menucommon_s *)ptr)->id ) {
	case ID_RL_BACK:
		UI_PopMenu();
		rl_hubDirty = qtrue;
		break;
	case ID_RL_ITEM0:
		SKU_Respec();
		break;
	}
}

void UI_DS_SkillsCache( void ) {
	sku_perkSound = trap_S_RegisterSound( "sound/rearmed/skills/sk_perk.wav", qfalse );
	sku_respecSound = trap_S_RegisterSound( "sound/rearmed/skills/sk_respec.wav", qfalse );
	sku_lockedSound = trap_S_RegisterSound( "sound/rearmed/skills/sk_locked.wav", qfalse );
}

/*
=================
UI_DS_SkillsMenu

The DESCENT hub's SKILLS button. focusSkill < 0: the tree with the most to spend.
=================
*/
void UI_DS_SkillsMenu( int focusSkill ) {
	int		s;
	int		p;

	memset( &s_sku, 0, sizeof( s_sku ) );
	UI_DS_SkillsCache();
	RL_SetupMenu( &s_sku.menu, SKU_MenuDraw, RL_InGame() );
	s_sku.menu.key = SKU_MenuKey;
	SKU_Layout();
	SKU_Load();
	s_sku.lastMouseX = uis.cursorx;
	s_sku.lastMouseY = uis.cursory;

	// start on the first perk that can be taken, else the highest skill's root
	s_sku.perk = -1;
	if ( focusSkill >= 0 && focusSkill < SK_NUM ) {
		s_sku.perk = focusSkill * SKP_PER_SKILL;
	}
	for ( p = 0; p < SKP_NUM && s_sku.perk < 0; p++ ) {
		if ( SKU_PerkState( p ) == 0 ) {
			s_sku.perk = p;
		}
	}
	if ( s_sku.perk < 0 ) {
		s = 0;
		for ( p = 1; p < SK_NUM; p++ ) {
			if ( s_sku.xp[p] > s_sku.xp[s] ) {
				s = p;
			}
		}
		s_sku.perk = s * SKP_PER_SKILL;
	}
	SKU_Focus( s_sku.perk );

	RL_InitButton( &s_sku.back, ID_RL_BACK, s_sku.left, 440, 120, 28, "BACK", SKU_MenuEvent );
	s_sku.back.center = qtrue;
	RL_InitButton( &s_sku.respec, ID_RL_ITEM0, s_sku.right - 200, 440, 200, 28, "RESPEC", SKU_MenuEvent );
	s_sku.respec.bm.generic.ownerdraw = SKU_RespecButtonDraw;
	Menu_AddItem( &s_sku.menu, &s_sku.back );
	Menu_AddItem( &s_sku.menu, &s_sku.respec );
	UI_PushMenu( &s_sku.menu );
}

/*
=======================================================================

HUB + RUN SUMMARY

=======================================================================
*/

// the hub button's line: "LEVEL 7  |  2 PERK POINTS TO SPEND"
const char *UI_DS_SkillsHubLine( void ) {
	int		xp[SK_NUM];
	int		lv[SK_NUM];
	int		ranks[SKP_NUM];
	int		i;
	int		pts;

	SKU_ReadCsv( &rl_meta, "sk_xp", xp, SK_NUM );
	SKU_ReadCsv( &rl_meta, "sk_perks", ranks, SKP_NUM );
	for ( i = 0; i < SK_NUM; i++ ) {
		lv[i] = BG_DS_SkillLevel( xp[i], NULL, NULL );
	}
	pts = BG_DS_PerkPoints( lv, ranks );
	if ( pts > 0 ) {
		return va( "Character level %i  |  %i PERK POINT%s TO SPEND", BG_DS_CharLevel( lv ), pts, pts == 1 ? "" : "S" );
	}
	return va( "Character level %i  |  ten skill trees, trained by doing", BG_DS_CharLevel( lv ) );
}

int UI_DS_SkillsPoints( void ) {
	int		xp[SK_NUM];
	int		lv[SK_NUM];
	int		ranks[SKP_NUM];
	int		i;

	SKU_ReadCsv( &rl_meta, "sk_xp", xp, SK_NUM );
	SKU_ReadCsv( &rl_meta, "sk_perks", ranks, SKP_NUM );
	for ( i = 0; i < SK_NUM; i++ ) {
		lv[i] = BG_DS_SkillLevel( xp[i], NULL, NULL );
	}
	return BG_DS_PerkPoints( lv, ranks );
}

/*
=================
UI_DS_SkillsSummary

Run summary (right panel): the skills this run trained, levels before > now, and the
perk points it earned. Needs the merge done (DSM_ProcessRun). Returns the next y.
=================
*/
float UI_DS_SkillsSummary( float x, float y, float w ) {
	int		runXP[SK_NUM];
	int		lv0[SK_NUM];
	int		xp[SK_NUM];
	int		lv[SK_NUM];
	int		order[SK_NUM];
	int		n;
	int		i;
	int		j;
	int		t;
	int		cl0;
	int		cl;
	vec4_t	c;

	RL_KVLoad( &sku_file, SKILLS_FILE );
	if ( !RL_KVInt( &rl_run, "seed" ) || RL_KVInt( &sku_file, "run_seed" ) != RL_KVInt( &rl_run, "seed" ) ) {
		return y;
	}
	SKU_ReadCsv( &sku_file, "run_xp", runXP, SK_NUM );
	SKU_ReadCsv( &sku_file, "run_lv0", lv0, SK_NUM );
	cl0 = RL_KVInt( &sku_file, "run_cl0" );
	SKU_ReadCsv( &rl_meta, "sk_xp", xp, SK_NUM );
	n = 0;
	for ( i = 0; i < SK_NUM; i++ ) {
		lv[i] = BG_DS_SkillLevel( xp[i], NULL, NULL );
		if ( runXP[i] > 0 ) {
			order[n++] = i;
		}
	}
	if ( !n ) {
		return y;
	}
	for ( i = 1; i < n; i++ ) {
		for ( j = i; j > 0 && runXP[order[j]] > runXP[order[j - 1]]; j-- ) {
			t = order[j];
			order[j] = order[j - 1];
			order[j - 1] = t;
		}
	}
	RL_Text( x, y, "SKILLS TRAINED", 6, 9, rl_colOrange, 0 );
	y += 13;
	for ( i = 0; i < n && i < 6; i++ ) {
		t = order[i];
		SKU_Color( t, 1, 1, c );
		UI_FillRect( x, y + 2, 5, 5, c );
		RL_Text( x + 8, y, ds_skills[t].name, 5, 8, rl_colBone, 0 );
		if ( lv0[t] > 0 && lv[t] > lv0[t] ) {
			RL_Text( x + w, y, va( "%i > %i", lv0[t], lv[t] ), 5, 8, rl_colGood, 2 );
		}
		else {
			RL_Text( x + w, y, va( "+%i XP", runXP[t] ), 5, 8, rl_colDim, 2 );
		}
		y += 11;
	}
	cl = BG_DS_CharLevel( lv );
	if ( cl0 > 0 && cl > cl0 ) {
		Vector4Copy( rl_colGood, c );
		c[3] = 0.75f + 0.25f * RL_Pulse( 0.005f );
		RL_TextS( x, y + 2, va( "+%i PERK POINT%s", cl - cl0, cl - cl0 == 1 ? "" : "S" ), 7, 11, c, 0 );
		y += 15;
		RL_Text( x, y, "Spend them in SKILLS (the hub)", 4, 7, rl_colDim, 0 );
		y += 10;
	}
	return y + 4;
}

/*
=================
UI_DS_SkillsCommand

Dev: dsskillmerge, dsskilltake <perk> (spend a point as the screen would),
dsskillrespec <skill> (no confirm) (the screen: "descent skills [skill]", ui_ds_meta.c)
=================
*/
qboolean UI_DS_SkillsCommand( const char *cmd ) {
	int		n;

	if ( !Q_stricmp( cmd, "dsskilltake" ) ) {
		n = atoi( UI_Argv( 1 ) );
		SKU_Take( n );
		Com_Printf( "dsskilltake: %s\n", s_sku.msg );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsskillrespec" ) ) {
		n = atoi( UI_Argv( 1 ) );
		if ( n >= 0 && n < SK_NUM ) {
			s_sku.tree = n;
			SKU_RespecAction( qtrue );
			Com_Printf( "dsskillrespec: %s\n", s_sku.msg );
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsskillmerge" ) ) {
		if ( !UI_DS_SkillsMerge() ) {
			Com_Printf( "SKILLS: nothing to merge\n" );
		}
		return qtrue;
	}
	return qfalse;
}
