/*
===========================================================================
REARMED -- en-screens: one shared stack for the right-edge callouts.

Several systems draw short callouts at the right edge of the HUD area (x 620, CG_HudAnchor( 1 )):
the nemesis / final blow line (cg_ds_acts.c), nemesis levels / revenge / veterans (cg_personality.c),
STOLEN / RECLAIMED (cg_thief.c), ambush / ghost / event callouts and the mini-boss / event status
lines (cg_ds_surprise.c), the champion pack line (cg_ds_affix.c), SECRET FOUND (cg_ds_secrets.c) and
the evolution-ready line (cg_ds_cards.c). Each used to pick its own fixed y, so two at once overlapped
(and the nemesis line sat on the style rank letter).

Now each asks CG_EdgeSlot( id, height ) for its y. Slots stack top to bottom in EDGE_* order (the
standing status lines first, then the callouts), starting under the style rank block (y 220) or, while
it shows, under the style acts feed (CG_StyleFeedBottom, up to y ~272), with a small gap. A slot that appears or disappears makes the ones under it glide (no jumps). The heights come
from this frame for slots already drawn and from the last frame for the rest (one frame late at most).
===========================================================================
*/

#include "cg_local.h"

#define EDGE_Y0		220.0f		// under the style rank letter, word and gauge (cg_style.c, x 486 y 150..210)
#define EDGE_GAP	4.0f
#define EDGE_YMAX	404.0f		// the score box (bottom right) starts under this
#define EDGE_GLIDE	90.0f		// msec to settle

typedef struct {
	int		frame;		// cg.clientFrame it was last drawn in
	float	h;
	float	y;
} edgeSlot_t;

static edgeSlot_t	edgeSlots[EDGE_NUM];
static int			edgeTopFrame;		// edgeTop is for this cg.clientFrame
static float		edgeTop;			// where the stack starts this frame

/*
=================
CG_EdgeSlot

y (640x480 units, top of the callout box) for right-edge callout <id> of height h this frame.
Never below EDGE_YMAX - h (a crowded stack squeezes at the bottom rather than covering the score box).
=================
*/
float CG_EdgeSlot( int id, float h ) {
	edgeSlot_t	*s;
	float		target = EDGE_Y0, f;
	int			i;

	if ( id < 0 || id >= EDGE_NUM ) {
		return EDGE_Y0;
	}
	// under the style acts feed while it shows (it lists the last kills' points under the rank letter,
	// in the same column); it moves smoothly itself, so the stack follows it smoothly
	if ( edgeTopFrame != cg.clientFrame ) {
		edgeTopFrame = cg.clientFrame;
		edgeTop = CG_StyleFeedBottom() + EDGE_GAP;
		if ( edgeTop < EDGE_Y0 ) {
			edgeTop = EDGE_Y0;
		}
	}
	target = edgeTop;
	for ( i = 0 ; i < id ; i++ ) {
		if ( edgeSlots[i].frame && edgeSlots[i].frame >= cg.clientFrame - 1 ) {
			target += edgeSlots[i].h + EDGE_GAP;
		}
	}
	if ( target > EDGE_YMAX - h ) {
		target = EDGE_YMAX - h;
	}
	s = &edgeSlots[id];
	if ( !s->frame || s->frame < cg.clientFrame - 1 || s->frame > cg.clientFrame ) {
		s->y = target;		// new (or a restarted map): straight in its place
	} else if ( s->frame != cg.clientFrame ) {
		f = cg.frametime > 0 ? cg.frametime / EDGE_GLIDE : 1.0f;
		s->y += ( target - s->y ) * ( f > 1.0f ? 1.0f : f );
		if ( fabs( target - s->y ) < 0.25f ) {
			s->y = target;
		}
	}
	s->frame = cg.clientFrame;
	s->h = h;
	return s->y;
}

/*
=================
CG_EdgeText / CG_EdgeCard

The card the thief, personality and type callouts share (en-review: it was three copies). Right-
aligned at x (the slide-in already added), y = the title's top (the box starts 3 above), box height h:
a dark box, a 2-unit accent bar on its right, the title (8x10, the accent lifted toward white by
lift) and the body line (5x7) under it. al fades it all: bar 0.9, box 0.5, title 1, body 0.85.
The caller sets CG_HudAnchor( 1 ).
=================
*/
void CG_EdgeText( float x, float y, const char *s, const float *col, int cw, int ch ) {
	CG_DrawStringExt( (int)( x - CG_DrawStrlen( s ) * cw ), (int)y, s, col, qfalse, qtrue, cw, ch, 0 );
}

void CG_EdgeCard( float x, float y, float h, float al, const char *l1, const char *l2,
	const vec3_t acc, const vec3_t bg, float lift, const vec3_t body ) {
	vec4_t	c;
	int		w;

	w = CG_DrawStrlen( l2 ) * 5;
	if ( CG_DrawStrlen( l1 ) * 8 > w ) {
		w = CG_DrawStrlen( l1 ) * 8;
	}
	VectorCopy( bg, c );
	c[3] = 0.5f * al;
	CG_FillRect( x - w - 8, y - 3, w + 10, h, c );
	VectorCopy( acc, c );
	c[3] = 0.9f * al;
	CG_FillRect( x + 1, y - 3, 2, h, c );
	c[0] = acc[0] + ( 1 - acc[0] ) * lift; c[1] = acc[1] + ( 1 - acc[1] ) * lift; c[2] = acc[2] + ( 1 - acc[2] ) * lift;
	c[3] = al;
	CG_EdgeText( x - 3, y, l1, c, 8, 10 );
	VectorCopy( body, c );
	c[3] = 0.85f * al;
	CG_EdgeText( x - 3, y + 12, l2, c, 5, 7 );
}

// a client's name for an edge line: no colour codes, upper case ("" for an out-of-range client)
void CG_CleanUpper( const char *in, char *out, int size ) {
	Q_strncpyz( out, in, size );
	Q_CleanStr( out );
	Q_strupr( out );
}

void CG_ClientNameUpper( int client, char *out, int size ) {
	if ( client < 0 || client >= MAX_CLIENTS ) {
		out[0] = 0;
		return;
	}
	CG_CleanUpper( cgs.clientinfo[client].name, out, size );
}
