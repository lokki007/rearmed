/*
===========================================================================
REARMED -- round 4 BESTIARY, cgame side (en-bestiary). Table: game/bg_ds_bestiary.h.

The game sends "dsbst <entry>" the first time ever the player meets an enemy kind
(game/g_ds_bestiary.c). A small callout slides in at the LEFT edge of the HUD area:
"NEW IN BESTIARY" over the name in the entry's colour, never in the centre.
Switch: cg_dsBestiaryNote (0 = no callout; the bestiary still records).
===========================================================================
*/
#include "cg_local.h"
#include "../game/bg_ds_bestiary.h"

#define BSTC_MAX		4
#define BSTC_TIME		3600		// msec on screen
#define BSTC_X			10
#define BSTC_Y			236			// under the corner block / frenzy counter, above the pickup line
#define BSTC_STEP		27

typedef struct {
	int		entry;
	int		time;
} bstNote_t;

static bstNote_t	bstc[BSTC_MAX];
static qboolean		bstcInit;
static sfxHandle_t	bstcSound;
static vmCvar_t		cg_dsBestiaryNote;

static void Bstc_Init( void ) {
	if ( bstcInit ) {
		return;
	}
	bstcInit = qtrue;
	trap_Cvar_Register( &cg_dsBestiaryNote, "cg_dsBestiaryNote", "1", CVAR_ARCHIVE );
	bstcSound = trap_S_RegisterSound( "sound/misc/menu1.wav", qfalse );
}

/*
=================
CG_DS_BestiaryServerCommand

dsbst <entry>
=================
*/
qboolean CG_DS_BestiaryServerCommand( const char *cmd ) {
	int		e, i, slot, oldest;

	if ( Q_stricmp( cmd, "dsbst" ) ) {
		return qfalse;
	}
	Bstc_Init();
	trap_Cvar_Update( &cg_dsBestiaryNote );
	e = atoi( CG_Argv( 1 ) );
	if ( e < 0 || e >= DSB_NUM ) {
		return qtrue;
	}
	if ( cg_debugEvents.integer ) {
		CG_Printf( "dsbst %i %s\n", e, BG_DS_BstName( e ) );
	}
	if ( !cg_dsBestiaryNote.integer ) {
		return qtrue;
	}
	// a free slot (or the oldest one)
	slot = -1;
	oldest = 0;
	for ( i = 0 ; i < BSTC_MAX ; i++ ) {
		if ( !bstc[i].time || cg.time - bstc[i].time > BSTC_TIME || bstc[i].time > cg.time ) {
			slot = i;
			break;
		}
		if ( bstc[i].time < bstc[oldest].time ) {
			oldest = i;
		}
	}
	if ( slot < 0 ) {
		slot = oldest;
	}
	bstc[slot].entry = e;
	bstc[slot].time = cg.time;
	trap_S_StartLocalSound( bstcSound, CHAN_LOCAL_SOUND );
	return qtrue;
}

/*
=================
CG_DS_BestiaryDraw2D

CG_Draw2D (not with the scoreboard up): the callouts at the left edge, oldest on top
=================
*/
void CG_DS_BestiaryDraw2D( void ) {
	int				order[BSTC_MAX];
	int				n, i, j, k;
	float			t, al, slide, x, y, w;
	vec4_t			col, bg;
	const char		*name;
	const dsBstInfo_t	*b;

	Bstc_Init();		// first frame: the switch (archived, F10 BOTS tab) and the sound
	if ( cg.showScores ) {
		return;
	}
	n = 0;
	for ( i = 0 ; i < BSTC_MAX ; i++ ) {
		if ( bstc[i].time && cg.time >= bstc[i].time && cg.time - bstc[i].time <= BSTC_TIME ) {
			order[n++] = i;
		}
	}
	if ( !n ) {
		return;
	}
	for ( i = 1 ; i < n ; i++ ) {		// by arrival
		for ( j = i ; j > 0 && bstc[order[j]].time < bstc[order[j - 1]].time ; j-- ) {
			k = order[j]; order[j] = order[j - 1]; order[j - 1] = k;
		}
	}
	CG_HudAnchor( -1 );		// the left side of the HUD area (cg_hudWide)
	y = BSTC_Y;
	for ( i = 0 ; i < n ; i++ ) {
		b = &bg_dsBst[bstc[order[i]].entry];
		name = BG_DS_BstName( bstc[order[i]].entry );
		t = cg.time - bstc[order[i]].time;
		al = CG_Clamp01( t / 150.0f ) * CG_Clamp01( ( BSTC_TIME - t ) / 450.0f );
		slide = -24.0f * ( 1.0f - CG_Clamp01( t / 220.0f ) );
		x = BSTC_X + slide;
		w = CG_DrawStrlen( name ) * 8;
		if ( w < 15 * 5 ) {
			w = 15 * 5;
		}
		w += 12;
		bg[0] = 0.04f; bg[1] = 0.03f; bg[2] = 0.02f; bg[3] = 0.6f * al;
		CG_FillRect( x - 4, y - 3, w, 24, bg );
		bg[0] = b->rgb[0]; bg[1] = b->rgb[1]; bg[2] = b->rgb[2]; bg[3] = 0.95f * al;
		CG_FillRect( x - 6, y - 3, 2, 24, bg );
		col[0] = 1.0f; col[1] = 0.8f; col[2] = 0.3f; col[3] = 0.9f * al;
		CG_DrawStringExt( (int)x, (int)y, "NEW IN BESTIARY", col, qtrue, qtrue, 5, 7, 0 );
		col[0] = b->rgb[0] * 0.75f + 0.25f; col[1] = b->rgb[1] * 0.75f + 0.25f; col[2] = b->rgb[2] * 0.75f + 0.25f; col[3] = al;
		CG_DrawStringExt( (int)x, (int)( y + 9 ), name, col, qtrue, qtrue, 8, 11, 0 );
		y += BSTC_STEP;
	}
	CG_HudAnchor( 0 );
}
