/*
===========================================================================
REARMED -- round 4 BESTIARY, UI side (en-bestiary). Table: game/bg_ds_bestiary.h.

UI_DS_BestiaryMerge: the finished floor's run keys (bst_fm, bst_fk; game/g_ds_bestiary.c)
into rearmed_meta.txt, called from DSM_MergeFloor (once per floor, the caller saves):
	bst_seen / bst_aseen / bst_xseen	masks (bit = DSAR_ id / DSAF_ bit index / DSBX_)
	bst_k / bst_ak / bst_xk				lifetime kills (csv)
	bst_d / bst_ad / bst_xd				first-met date YYYYMMDD (csv)
The BESTIARY tab of the Collection (ui_ds_meta.c DSC_BESTIARY) asks this file for its cells,
names, tiles and the detail panel. Act bosses also count as met once beaten (seen_boss) and
show META's cnt_boss<n> as their kills. Reset All Progress empties the meta file: these go too.
===========================================================================
*/
#include "ui_local.h"
#include "ui_ds_meta.h"
#include "../game/bg_ds_bestiary.h"

int trap_RealTime( qtime_t *qtime );

static vec4_t	dsb_colSilhouette = { 0.05f, 0.03f, 0.03f, 1.0f };
static vec4_t	dsb_colGold = { 1.0f, 0.78f, 0.25f, 1.0f };
static qhandle_t	dsb_bossIcons[BOSS_NUM];
static qboolean		dsb_cached;

// the meta keys of a kind: mask, kills, dates and their length
static const char *DSB_Key( int kind, int which ) {
	static const char	*keys[3][3] = {
		{ "bst_seen", "bst_k", "bst_d" },
		{ "bst_aseen", "bst_ak", "bst_ad" },
		{ "bst_xseen", "bst_xk", "bst_xd" }
	};

	return keys[kind][which];
}

static int DSB_KindLen( int kind ) {
	return kind == DSB_KIND_TYPE ? DSAR_NUM : kind == DSB_KIND_AFFIX ? DSAF_NUM : DSBX_NUM;
}

static void DSB_ReadCsv( rlKV_t *kv, const char *key, int *out, int n ) {
	int		i;

	for ( i = 0; i < n; i++ ) {
		out[i] = 0;
	}
	RL_ParseInts( RL_KVGet( kv, key ), out, n );
}

static const char *DSB_Csv( const int *v, int n ) {
	static char	buf[256];
	int			i;

	buf[0] = 0;
	for ( i = 0; i < n; i++ ) {
		Q_strcat( buf, sizeof( buf ), va( i ? ",%i" : "%i", v[i] ) );
	}
	return buf;
}

static int DSB_BossOf( int e ) {
	int		x;

	if ( BG_DS_BstKind( e ) != DSB_KIND_EXTRA ) {
		return -1;
	}
	x = BG_DS_BstId( e ) - DSBX_BOSS;
	return x >= 0 && x < BOSS_NUM ? x : -1;
}

/*
=================
UI_DS_BestiaryMerge

DSM_MergeFloor (rl_run and rl_meta loaded, the caller saves the meta file)
=================
*/
void UI_DS_BestiaryMerge( void ) {
	int		fk[DSB_NUM];
	int		kills[3][DSAR_NUM + DSBX_NUM];
	int		dates[3][DSAR_NUM + DSBX_NUM];
	int		masks[3];
	int		fm, e, kind, id, k, today, fresh;
	qtime_t	qt;

	fm = RL_KVInt( &rl_run, "bst_fm" );
	DSB_ReadCsv( &rl_run, "bst_fk", fk, DSB_NUM );
	for ( e = 0, k = 0; e < DSB_NUM; e++ ) {
		k += fk[e];
	}
	if ( !fm && !k ) {
		return;
	}
	memset( &qt, 0, sizeof( qt ) );
	trap_RealTime( &qt );
	today = ( qt.tm_year + 1900 ) * 10000 + ( qt.tm_mon + 1 ) * 100 + qt.tm_mday;
	for ( kind = 0; kind < 3; kind++ ) {
		masks[kind] = RL_KVInt( &rl_meta, DSB_Key( kind, 0 ) );
		DSB_ReadCsv( &rl_meta, DSB_Key( kind, 1 ), kills[kind], DSB_KindLen( kind ) );
		DSB_ReadCsv( &rl_meta, DSB_Key( kind, 2 ), dates[kind], DSB_KindLen( kind ) );
	}
	fresh = 0;
	for ( e = 0; e < DSB_NUM; e++ ) {
		kind = BG_DS_BstKind( e );
		id = BG_DS_BstId( e );
		if ( ( fm & ( 1 << e ) ) || fk[e] > 0 ) {
			if ( !( masks[kind] & ( 1 << id ) ) ) {
				fresh++;
			}
			masks[kind] |= 1 << id;
			if ( !dates[kind][id] ) {
				dates[kind][id] = today;
			}
		}
		if ( fk[e] > 0 ) {
			kills[kind][id] += fk[e];
		}
	}
	for ( kind = 0; kind < 3; kind++ ) {
		RL_KVSetInt( &rl_meta, DSB_Key( kind, 0 ), masks[kind] );
		RL_KVSet( &rl_meta, DSB_Key( kind, 1 ), DSB_Csv( kills[kind], DSB_KindLen( kind ) ) );
		RL_KVSet( &rl_meta, DSB_Key( kind, 2 ), DSB_Csv( dates[kind], DSB_KindLen( kind ) ) );
	}
	Com_Printf( "BESTIARY: floor merged, met mask %i, %i entry kills, %i new entries\n", fm, k, fresh );
}

/*
=======================================================================

COLLECTION TAB

=======================================================================
*/

// en-screens: the grid starts each group on a new row: 0 archetypes, 1 affixes, 2 the champion pack,
// 3 mini-bosses, 4 act bosses
int UI_DS_BestiaryGroup( int e ) {
	int		x;

	switch ( BG_DS_BstKind( e ) ) {
	case DSB_KIND_TYPE:
		return 0;
	case DSB_KIND_AFFIX:
		return 1;
	case DSB_KIND_EXTRA:
		x = BG_DS_BstId( e );
		return x == DSBX_CHAMPION ? 2 : x < DSBX_BOSS ? 3 : 4;
	}
	return 4;
}

int UI_DS_BestiaryItems( int *out ) {
	int		e;

	for ( e = 0; e < DSB_NUM; e++ ) {
		out[e] = e;
	}
	return DSB_NUM;
}

qboolean UI_DS_BestiaryFound( int e ) {
	int		kind;
	int		b;

	kind = BG_DS_BstKind( e );
	if ( kind < 0 ) {
		return qfalse;
	}
	b = DSB_BossOf( e );
	if ( b >= 0 && ( RL_KVInt( &rl_meta, "seen_boss" ) & ( 1 << b ) ) ) {
		return qtrue;		// beaten before the bestiary existed
	}
	return ( RL_KVInt( &rl_meta, DSB_Key( kind, 0 ) ) & ( 1 << BG_DS_BstId( e ) ) ) != 0;
}

int UI_DS_BestiaryKills( int e ) {
	int		v[DSAR_NUM + DSBX_NUM];
	int		kind;
	int		b;

	kind = BG_DS_BstKind( e );
	if ( kind < 0 ) {
		return 0;
	}
	b = DSB_BossOf( e );
	if ( b >= 0 ) {
		return RL_KVInt( &rl_meta, va( "cnt_boss%i", b ) );
	}
	DSB_ReadCsv( &rl_meta, DSB_Key( kind, 1 ), v, DSB_KindLen( kind ) );
	return v[BG_DS_BstId( e )];
}

static int DSB_Date( int e ) {
	int		v[DSAR_NUM + DSBX_NUM];
	int		kind;

	kind = BG_DS_BstKind( e );
	if ( kind < 0 ) {
		return 0;
	}
	DSB_ReadCsv( &rl_meta, DSB_Key( kind, 2 ), v, DSB_KindLen( kind ) );
	return v[BG_DS_BstId( e )];
}

const char *UI_DS_BestiaryName( int e, qboolean found ) {
	return found ? BG_DS_BstName( e ) : "???";
}

static void DSB_Cache( void ) {
	int		i;

	if ( dsb_cached ) {
		return;
	}
	dsb_cached = qtrue;
	for ( i = 0; i < BOSS_NUM; i++ ) {
		dsb_bossIcons[i] = trap_R_RegisterShaderNoMip( va( "models/players/%s/icon_default", ds_bosses[i].model ) );
	}
}

/*
=================
UI_DS_BestiaryIcon

A tile in the entry's colour: archetypes solid, affixes a ring, champions gold, mini-bosses
a double frame, bosses their portrait. Kills in the corner. Not met: a dark "?".
=================
*/
void UI_DS_BestiaryIcon( int e, qboolean found, float x, float y, float sz ) {
	const dsBstInfo_t	*b;
	vec4_t	c;
	vec4_t	edge;
	float	cw;
	int		kind;
	int		boss;
	int		kills;
	const char	*tag;

	if ( e < 0 || e >= DSB_NUM ) {
		return;
	}
	DSB_Cache();
	b = &bg_dsBst[e];
	kind = BG_DS_BstKind( e );
	boss = DSB_BossOf( e );
	edge[0] = b->rgb[0]; edge[1] = b->rgb[1]; edge[2] = b->rgb[2]; edge[3] = 1;

	if ( !found ) {
		RL_Frame( x, y, sz, sz, dsb_colSilhouette, rl_colDark );
		if ( boss >= 0 ) {
			RL_Pic( x + 2, y + 2, sz - 4, sz - 4, dsb_bossIcons[boss], dsb_colSilhouette );
		}
		RL_TextS( x + sz / 2, y + sz / 2 - sz * 0.2f, "?", sz * 0.28f, sz * 0.4f, rl_colDim, 1 );
		return;
	}
	RL_Scale( c, edge, 0.45f, 1 );
	RL_Pic( x - sz * 0.25f, y - sz * 0.25f, sz * 1.5f, sz * 1.5f, rl_shGlow, c );
	if ( boss >= 0 ) {
		RL_Frame( x, y, sz, sz, rl_colBlack, edge );
		RL_Pic( x + 2, y + 2, sz - 4, sz - 4, dsb_bossIcons[boss], NULL );
	}
	else if ( kind == DSB_KIND_AFFIX ) {
		RL_Frame( x, y, sz, sz, rl_colBlack, rl_colLine );
		RL_Pic( x + sz * 0.06f, y + sz * 0.06f, sz * 0.88f, sz * 0.88f, rl_shRing, edge );
	}
	else {
		RL_Scale( c, edge, kind == DSB_KIND_TYPE ? 0.38f : 0.22f, 1 );
		RL_Frame( x, y, sz, sz, c, edge );
		if ( kind == DSB_KIND_EXTRA ) {
			RL_Frame( x + 3, y + 3, sz - 6, sz - 6, NULL, BG_DS_BstId( e ) == DSBX_CHAMPION ? dsb_colGold : edge );
		}
	}
	tag = b->tag;
	if ( boss < 0 && tag && tag[0] ) {
		cw = sz * 0.8f / strlen( tag );
		if ( cw > sz * 0.28f ) {
			cw = sz * 0.28f;
		}
		RL_TextS( x + sz / 2, y + sz / 2 - cw * 0.75f, tag, cw, cw * 1.4f, rl_colBone, 1 );
	}
	kills = UI_DS_BestiaryKills( e );
	if ( kills > 0 && sz >= 30 ) {
		cw = sz * 0.13f;
		RL_TextS( x + sz - 2, y + sz - cw * 1.5f - 2, va( "%i", kills ), cw, cw * 1.4f, dsb_colGold, 2 );
	}
}

/*
=================
UI_DS_BestiaryDetails

The detail panel under the tile and the name (DSC_Details drew those): what kind, first met,
kills, what it does and how to beat it. Not met: where to look.
=================
*/
void UI_DS_BestiaryDetails( int e, qboolean found, float x, float ty, float w, float bottom ) {
	const dsBstInfo_t	*b;
	vec4_t	col;
	int		date;
	int		kills;
	int		kind;

	if ( e < 0 || e >= DSB_NUM ) {
		return;
	}
	b = &bg_dsBst[e];
	kind = BG_DS_BstKind( e );
	col[0] = b->rgb[0] * 0.7f + 0.3f; col[1] = b->rgb[1] * 0.7f + 0.3f; col[2] = b->rgb[2] * 0.7f + 0.3f; col[3] = 1;
	RL_Text( x, ty, BG_DS_BstKindName( e ), 5, 8, found ? col : rl_colDim, 0 );
	ty += 12;
	if ( !found ) {
		if ( kind == DSB_KIND_TYPE ) {
			ty = RL_TextWrap( x, ty, w, va( "Not met yet. Shows up from act %i of a descent.", b->minAct ), 6, 10, rl_colDim, 3 );
		}
		else if ( kind == DSB_KIND_AFFIX ) {
			ty = RL_TextWrap( x, ty, w, "Not met yet. Elites carry affixes: one in act 1, more deeper down.", 6, 10, rl_colDim, 3 );
		}
		else if ( DSB_BossOf( e ) >= 0 ) {
			ty = RL_TextWrap( x, ty, w, va( "Not met yet. The boss of act %i.", b->minAct ), 6, 10, rl_colDim, 3 );
		}
		else if ( BG_DS_BstId( e ) == DSBX_CHAMPION ) {
			ty = RL_TextWrap( x, ty, w, "Not met yet. Three elites that land together.", 6, 10, rl_colDim, 3 );
		}
		else {
			ty = RL_TextWrap( x, ty, w, "Not met yet. Crashes into a floor mid-fight, after a warning.", 6, 10, rl_colDim, 3 );
		}
		ty += 6;
		RL_TextWrap( x, ty, w, "Meet it (see it or take a hit from it) to fill this page.", 5, 8, rl_colDark, 3 );
		return;
	}
	date = DSB_Date( e );
	kills = UI_DS_BestiaryKills( e );
	RL_Text( x, ty, va( "KILLED  %i", kills ), 7, 11, kills > 0 ? dsb_colGold : rl_colBone, 0 );
	ty += 14;
	if ( date > 0 ) {
		RL_Text( x, ty, va( "FIRST MET  %04i-%02i-%02i", date / 10000, ( date / 100 ) % 100, date % 100 ), 5, 8, rl_colDim, 0 );
	}
	else {
		RL_Text( x, ty, "FIRST MET  BEFORE THE BESTIARY", 5, 8, rl_colDim, 0 );
	}
	ty += 14;
	ty = RL_TextWrap( x, ty, w, b->what, 6, 10, rl_colBone, 5 );
	ty += 8;
	if ( ty < bottom - 20 ) {
		RL_Text( x, ty, "HOW TO BEAT IT", 5, 8, rl_colOrange, 0 );
		ty += 11;
		RL_TextWrap( x, ty, w, b->tip, 6, 10, rl_colGood, (int)( ( bottom - ty ) / 11 ) );
	}
}
