/*
===========================================================================
REARMED -- DESCENT BOSS REMATCH (round 4, en-rematch), cgame side.

On a rematch floor (CS_DESCENTBOSS rm / rmh / rmp, game/g_ds_rematch.c) a slim block LEFT of the
top boss bar (the clock is on its right), never the centre:
	REMATCH  HEAT 9
	IRON  FURY  ONE LIFE
	BEST 1:23.4            (at the kill: NEW BEST -3.2 S, or +4.1 S OFF BEST)
Called from CGA_DrawBossBar (cg_ds_acts.c) with the bar's place. The best times themselves live in
the UI (rearmed_rematch.txt); the game passes the previous best at this heat as rmp.
===========================================================================
*/
#include "cg_local.h"
#include "../game/bg_ds_rematch.h"

// the CS_DESCENTBOSS keys this needs (-1 rm: not a rematch)
typedef struct {
	int		mask, heat, prev, kill, dead, up, deaths;
} cgRm_t;

static void CGRM_Read( cgRm_t *r ) {
	const char	*b = CG_ConfigString( CS_DESCENTBOSS );
	const char	*v = Info_ValueForKey( b, "rm" );

	r->mask = v[0] ? atoi( v ) : -1;
	r->heat = atoi( Info_ValueForKey( b, "rmh" ) );
	r->prev = atoi( Info_ValueForKey( b, "rmp" ) );
	r->kill = atoi( Info_ValueForKey( b, "kt" ) );
	r->dead = atoi( Info_ValueForKey( b, "dead" ) );
	r->up = atoi( Info_ValueForKey( b, "tu" ) );
	r->deaths = atoi( Info_ValueForKey( b, "pd" ) );
}

// m:ss.t (its own buffers)
static const char *CGRM_Time( int msec ) {
	static char	buf[2][16];
	static int	idx;
	char		*s = buf[idx++ & 1];

	if ( msec < 0 ) {
		msec = 0;
	}
	Com_sprintf( s, sizeof( buf[0] ), "%i:%02i.%i", msec / 60000, ( msec / 1000 ) % 60, ( msec % 1000 ) / 100 );
	return s;
}

static void CGRM_Text( float x, float y, const char *s, const float *color, float cw, float ch ) {
	float	w = CG_DrawStrlen( s ) * (int)( cw + 0.5f );

	CG_DrawStringExt( (int)( x - w ), (int)y, s, color, qtrue, qtrue, (int)( cw + 0.5f ), (int)( ch + 0.5f ), 0 );
}

// this practice floor is a rematch (CS_DESCENTBOSS rm)
qboolean CG_DS_RematchOn( void ) {
	return Info_ValueForKey( CG_ConfigString( CS_DESCENTBOSS ), "rm" )[0] != 0;
}

// a one-life rematch lost by dying (the bar's end label says so instead of TIME UP)
qboolean CG_DS_RematchFallen( void ) {
	cgRm_t	r;

	CGRM_Read( &r );
	return r.mask > 0 && ( r.mask & DSRM_ONELIFE ) && r.up && r.deaths > 0;
}

/*
=================
CG_DS_RematchDraw

x, y, h: the boss bar's left end, top and height; al its fade-in
=================
*/
void CG_DS_RematchDraw( float x, float y, float h, float al ) {
	cgRm_t		r;
	vec4_t		col;
	char		line[128];
	float		rx = x - 9, f;
	int			i, d;

	CGRM_Read( &r );
	if ( r.mask < 0 ) {
		return;
	}
	// REMATCH  HEAT n (orange; heat 0 dim)
	col[0] = 1.0f; col[1] = r.heat > 0 ? 0.55f : 0.8f; col[2] = r.heat > 0 ? 0.15f : 0.7f; col[3] = 0.95f * al;
	CGRM_Text( rx, y - 13, va( "REMATCH  HEAT %i", r.heat ), col, 6, 9 );
	// the modifiers
	line[0] = 0;
	for ( i = 0 ; i < DSRM_NUM ; i++ ) {
		if ( r.mask & dsrm_mods[i].bit ) {
			Q_strcat( line, sizeof( line ), line[0] ? va( "  %s", dsrm_mods[i].name ) : dsrm_mods[i].name );
		}
	}
	col[0] = 0.95f; col[1] = 0.88f; col[2] = 0.78f; col[3] = 0.9f * al;
	CGRM_Text( rx, y + h * 0.5f - 4, line[0] ? line : "NO MODIFIERS", col, 5, 8 );
	// the best time at this heat; at the kill the new record (or the gap)
	if ( r.dead && r.kill > 0 ) {
		d = r.kill - r.prev;
		if ( r.prev <= 0 || d < 0 ) {
			f = 0.75f + 0.25f * sin( cg.time * 0.01f );
			col[0] = 1.0f; col[1] = 0.82f * f + 0.1f; col[2] = 0.25f; col[3] = al;
			CGRM_Text( rx, y + h + 3, r.prev > 0 ? va( "NEW BEST  -%i.%i S", ( -d ) / 1000, ( ( -d ) % 1000 ) / 100 )
				: va( "FIRST BEST AT HEAT %i", r.heat ), col, 6, 9 );
		} else {
			col[0] = 0.8f; col[1] = 0.8f; col[2] = 0.8f; col[3] = 0.85f * al;
			CGRM_Text( rx, y + h + 3, va( "+%i.%i S OFF BEST %s", d / 1000, ( d % 1000 ) / 100, CGRM_Time( r.prev ) ), col, 5, 8 );
		}
	} else if ( r.prev > 0 ) {
		col[0] = 0.75f; col[1] = 0.72f; col[2] = 0.68f; col[3] = 0.8f * al;
		CGRM_Text( rx, y + h + 3, va( "BEST %s", CGRM_Time( r.prev ) ), col, 5, 8 );
	} else {
		col[0] = 0.6f; col[1] = 0.6f; col[2] = 0.6f; col[3] = 0.7f * al;
		CGRM_Text( rx, y + h + 3, "NO BEST AT THIS HEAT YET", col, 5, 8 );
	}
}
