/*
===========================================================================
REARMED -- round 4 ENEMIES, PERSONALITY (en-personality), cgame side. Never the screen centre.

- Voice barks (server cmd prb): a short shout from the bot, positional on its body
  (sound/rearmed/taunt/m_*.wav or f_*.wav by the model's gender; tools/make_taunt_voices.py).
- Edge callouts (server cmd prc): the nemesis arriving / levelling up, your revenge skull, a
  promoted veteran: two short lines at the right edge under the ds-boss callout, ~3 s.
- TAB scoreboard: veteran rank pips after the name, a red NEMESIS LV tag (CG_PR_ScoreTag).
- On the body: a small shoulder light, bronze / silver / gold by rank, a pulsing red one on the
  other shoulder for the nemesis (CG_PR_PlayerTell). No overhead text.
Contract: game/bg_ds_personality.h. Ranks / flags come from CS_DSENEMY (CG_EN).
cg_prTells 0 hides the shoulder lights (dev; the switch players use is g_dsVeterans).
===========================================================================
*/
#include "cg_local.h"
#include "../game/bg_ds_personality.h"

#define PRC_MAXQ		3
#define PRC_TIME		3200

static const char *prBarkFiles[PRB_NUM] = {
	"gotcha", "easy", "oops", "stop", "payback", "remember", "mine", "retreat", "again",
	"nemagain", "found", "stronger", "incoming", "eat", "glory", "rise", "getover"
};

typedef struct {
	int		kind, client, a, b, time;
	int		queued;		// cg.time it arrived (waits while you are dead / the scoreboard is up, 10 s at most)
	char	text[40];
} prCallout_t;

static sfxHandle_t	prBark[2][PRB_NUM];
static sfxHandle_t	prSndCallout;
static prCallout_t	prq[PRC_MAXQ];
static int			prqNum;
static vmCvar_t		prCvTells;

void CG_PR_Register( void ) {
	int		i;

	trap_Cvar_Register( &prCvTells, "cg_prTells", "1", 0 );
	for ( i = 0 ; i < PRB_NUM ; i++ ) {
		prBark[0][i] = trap_S_RegisterSound( va( "sound/rearmed/taunt/m_%s.wav", prBarkFiles[i] ), qfalse );
		prBark[1][i] = trap_S_RegisterSound( va( "sound/rearmed/taunt/f_%s.wav", prBarkFiles[i] ), qfalse );
	}
	prSndCallout = trap_S_RegisterSound( "sound/rearmed/ann_warn.wav", qfalse );
	prqNum = 0;
}

static const char *PR_AffixName( int mask ) {
	if ( mask & DSAF_VAMPIRIC ) return "VAMPIRIC";
	if ( mask & DSAF_REGEN ) return "REGENERATES";
	if ( mask & DSAF_FROST ) return "FROST";
	if ( mask & DSAF_FIRETRAIL ) return "FIRE TRAIL";
	if ( mask & DSAF_REFLECT ) return "REFLECTS";
	return "";
}


/*
=================
CG_PR_ServerCommand

prb <client> <bark>; prc <kind> <client> <a> <b> "<text>"
=================
*/
qboolean CG_PR_ServerCommand( const char *cmd ) {
	int			c, b, g;
	prCallout_t	*q;

	if ( !Q_stricmp( cmd, "prb" ) ) {
		c = atoi( CG_Argv( 1 ) );
		b = atoi( CG_Argv( 2 ) );
		if ( c < 0 || c >= MAX_CLIENTS || b < 0 || b >= PRB_NUM ) {
			return qtrue;
		}
		g = cgs.clientinfo[c].gender == GENDER_FEMALE ? 1 : 0;
		if ( !prBark[g][b] ) {
			g = !g;
		}
		// only a bot we can hear (in the snapshot): far away = silent
		if ( prBark[g][b] && cg_entities[c].currentValid ) {
			trap_S_StartSound( NULL, c, CHAN_AUTO, prBark[g][b] );		// (not CHAN_VOICE: pain sounds would cut it)
		}
		if ( cg_debugEvents.integer ) {
			CG_Printf( "PRBark: %i %s (%s)\n", c, prBarkFiles[b], cg_entities[c].currentValid ? "heard" : "far" );
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "prc" ) ) {
		if ( prqNum >= PRC_MAXQ ) {
			memmove( &prq[0], &prq[1], sizeof( prq[0] ) * ( PRC_MAXQ - 1 ) );
			prqNum = PRC_MAXQ - 1;
		}
		q = &prq[prqNum++];
		memset( q, 0, sizeof( *q ) );
		q->kind = atoi( CG_Argv( 1 ) );
		q->client = atoi( CG_Argv( 2 ) );
		q->a = atoi( CG_Argv( 3 ) );
		q->b = atoi( CG_Argv( 4 ) );
		Q_strncpyz( q->text, CG_Argv( 5 ), sizeof( q->text ) );
		Q_strupr( q->text );
		q->time = 0;		// starts when it reaches the front
		q->queued = cg.time;
		if ( cg_debugEvents.integer ) {		// (en-memory: printed on screen as a notify line otherwise)
			CG_Printf( "PRCallout: kind %i client %i a %i b %i '%s'\n", q->kind, q->client, q->a, q->b, q->text );
		}
		return qtrue;
	}
	return qfalse;
}

/*
===============================================================================

EDGE CALLOUTS

===============================================================================
*/

static void PR_Pips( float x, float y, int rank, float h, const float *col ) {
	int		i;
	float	w = h * 0.45f;
	static int	warned;

	if ( rank < 0 || rank > DS_MAX_RANK ) {
		if ( !warned ) {
			warned = 1;
			CG_Printf( "PRPips: bad rank %i (clamped)\n", rank );
		}
		rank = rank < 0 ? 0 : DS_MAX_RANK;
	}
	for ( i = 0 ; i < rank ; i++ ) {
		CG_FillRect( x + i * ( w + 2 ), y, w, h, col );
	}
}

void CG_PR_Draw2D( void ) {
	prCallout_t	*q;
	vec4_t		col;
	vec3_t		acc;
	float		t, al, slide, x = 620, y;
	char		name[32], l1[64], l2[80];
	static const vec3_t	bg = { 0.06f, 0.02f, 0.02f }, body = { 0.9f, 0.86f, 0.8f };

	if ( prCvTells.integer < 0 || !prqNum || cg.snap == NULL || cg.predictedPlayerState.pm_type == PM_INTERMISSION ) {
		return;
	}
	q = &prq[0];
	if ( !q->time && ( cg.scoreBoardShowing || cg.snap->ps.stats[STAT_HEALTH] <= 0 ) ) {
		// you just died (a nemesis level-up always comes then): show it once you're back, not under TAB
		if ( cg.time - q->queued > 10000 ) {
			memmove( &prq[0], &prq[1], sizeof( prq[0] ) * ( PRC_MAXQ - 1 ) );
			prqNum--;
		}
		return;
	}
	if ( !q->time ) {
		q->time = cg.time;
		if ( q->kind == PRC_NEMESIS_ARRIVE || q->kind == PRC_NEMESIS_LEVEL ) {
			trap_S_StartLocalSound( prSndCallout, CHAN_LOCAL_SOUND );
		}
	}
	t = cg.time - q->time;
	if ( t < 0 || t > PRC_TIME ) {
		memmove( &prq[0], &prq[1], sizeof( prq[0] ) * ( PRC_MAXQ - 1 ) );
		prqNum--;
		return;
	}
	al = CG_Clamp01( t / 150.0f ) * CG_Clamp01( ( PRC_TIME - t ) / 400.0f );
	slide = 18.0f * ( 1.0f - CG_Clamp01( t / 180.0f ) );
	CG_ClientNameUpper( q->client, name, sizeof( name ) );
	acc[0] = 0.85f; acc[1] = 0.12f; acc[2] = 0.08f;
	switch ( q->kind ) {
	case PRC_NEMESIS_ARRIVE:
		Com_sprintf( l1, sizeof( l1 ), "NEMESIS LV%i", q->a );
		Com_sprintf( l2, sizeof( l2 ), q->text[0] ? "%s %s  %i KILLS" : "%s%s  %i KILLS", name, q->text, q->b );
		break;
	case PRC_NEMESIS_LEVEL:
		Com_sprintf( l1, sizeof( l1 ), "NEMESIS  LV%i", q->a );
		if ( q->b ) {
			Com_sprintf( l2, sizeof( l2 ), "%s%s%s  +%s", name, q->text[0] ? " " : "", q->text, PR_AffixName( q->b ) );
		} else {
			Com_sprintf( l2, sizeof( l2 ), "%s%s%s GROWS STRONGER", name, q->text[0] ? " " : "", q->text );
		}
		break;
	case PRC_REVENGE:
		Com_sprintf( l1, sizeof( l1 ), "REVENGE" );
		Com_sprintf( l2, sizeof( l2 ), "%s  +%i SKULL%s", name, q->a, q->a == 1 ? "" : "S" );
		acc[0] = 0.95f; acc[1] = 0.75f; acc[2] = 0.25f;
		break;
	case PRC_PROMOTED:
		Com_sprintf( l1, sizeof( l1 ), "VETERAN %s", q->a >= 0 && q->a <= DS_MAX_RANK ? ds_roman[q->a] : "" );
		Com_sprintf( l2, sizeof( l2 ), "%s IS BACK, PROMOTED", name );
		acc[0] = 0.95f; acc[1] = 0.75f; acc[2] = 0.25f;
		break;
	case PRC_RIVAL:
		// en-memory (g_ds_botmem.c): a bot with a lifetime history with you, a = tier (bg_ds_botmem.h BMT_*)
		Com_sprintf( l1, sizeof( l1 ), "%s", q->a >= 4 ? "OLD NEMESIS" : q->a == 3 ? "ARCH-RIVAL" : "RIVAL" );
		Com_sprintf( l2, sizeof( l2 ), "%s  %s", name, q->text );
		acc[0] = 0.75f; acc[1] = 0.45f; acc[2] = 0.95f;
		break;
	default:
		return;
	}
	y = CG_EdgeSlot( EDGE_PERSONALITY, 25 ) + 3;		// en-screens: the shared right-edge stack
	CG_HudAnchor( 1 );
	CG_EdgeCard( x + slide, y, 25, al, l1, l2, acc, bg, 0.3f, body );
	if ( q->kind == PRC_PROMOTED ) {
		col[0] = acc[0] + ( 1 - acc[0] ) * 0.3f; col[1] = acc[1] + ( 1 - acc[1] ) * 0.3f; col[2] = acc[2] + ( 1 - acc[2] ) * 0.3f; col[3] = al;
		PR_Pips( x - 3 + slide - CG_DrawStrlen( l1 ) * 8 - 6 - q->a * 6, y + 1, q->a, 8, col );		// the title's colour
	}
	CG_HudAnchor( 0 );
}

/*
=================
CG_PR_ScoreTag

cg_scoreboard.c, after a row's text: veteran pips and the nemesis tag after the name
(x = the end of the name, y = the row top, h = the row's text height)
=================
*/
void CG_PR_ScoreTag( float x, float y, int client, float h, float fade ) {
	const cgEnemy_t	*e = CG_EN( client );
	vec4_t			col;
	float			ph = h * 0.6f;

	if ( prCvTells.integer < 0 ) {
		return;		// dev: cg_prTells -1 = none of this file's drawing
	}

	if ( e->rank > 0 ) {
		col[0] = e->rank >= 5 ? 1.0f : e->rank >= 3 ? 0.85f : 0.85f;
		col[1] = e->rank >= 5 ? 0.82f : e->rank >= 3 ? 0.88f : 0.55f;
		col[2] = e->rank >= 5 ? 0.3f : e->rank >= 3 ? 1.0f : 0.28f;
		col[3] = fade;
		PR_Pips( x, y + ( h - ph ) * 0.5f, e->rank, ph, col );
		x += e->rank * ( ph * 0.45f + 2 ) + 6;
	}
	if ( e->flags & DSEF_NEMESIS ) {
		const char	*b = CG_ConfigString( CS_DESCENTBOSS );
		int			lv = atoi( Info_ValueForKey( b, "nl" ) );

		col[0] = 1.0f; col[1] = 0.25f; col[2] = 0.18f; col[3] = fade;
		CG_DrawStringExt( (int)x, (int)( y + ( h - 8 ) * 0.5f ), lv > 0 ? va( "NEMESIS LV%i", lv ) : "NEMESIS", col, qtrue, qtrue, 6, 8, 0 );
	}
}

/*
=================
CG_PR_PlayerTell

CG_Player, after the head: a small shoulder light by rank, a red one for the nemesis
=================
*/
static void PR_Light( const vec3_t org, float radius, float r, float g, float b, float a ) {
	refEntity_t		re;

	memset( &re, 0, sizeof( re ) );
	re.reType = RT_SPRITE;
	VectorCopy( org, re.origin );
	re.radius = radius;
	re.customShader = cgs.media.fxGlowShader;
	re.shaderRGBA[0] = (byte)( 255 * CG_Clamp01( r * a ) );
	re.shaderRGBA[1] = (byte)( 255 * CG_Clamp01( g * a ) );
	re.shaderRGBA[2] = (byte)( 255 * CG_Clamp01( b * a ) );
	re.shaderRGBA[3] = 255;
	trap_R_AddRefEntityToScene( &re );
}

void CG_PR_PlayerTell( centity_t *cent, refEntity_t *torso, refEntity_t *head ) {
	const cgEnemy_t	*e;
	vec3_t			p;
	float			pulse, r;
	int				c = cent->currentState.clientNum;

	if ( prCvTells.integer <= 0 || ( cent->currentState.eFlags & EF_DEAD ) || c == cg.snap->ps.clientNum ) {
		return;
	}
	e = CG_EN( c );
	if ( e->rank > 0 ) {
		// on top of the left shoulder pad: from the neck tag out to the side, a little up and forward
		VectorMA( head->origin, 7.0f, torso->axis[1], p );
		VectorMA( p, 0.5f, torso->axis[2], p );
		VectorMA( p, 3.0f, torso->axis[0], p );
		r = 2.4f + 0.25f * e->rank;
		if ( e->rank >= 5 ) {
			PR_Light( p, r, 1.0f, 0.78f, 0.25f, 1.0f );
		} else if ( e->rank >= 3 ) {
			PR_Light( p, r, 0.75f, 0.85f, 1.0f, 0.95f );
		} else {
			PR_Light( p, r, 1.0f, 0.55f, 0.2f, 0.9f );
		}
	}
	if ( e->flags & DSEF_NEMESIS ) {
		pulse = 0.7f + 0.3f * sin( cg.time * 0.006f );
		VectorMA( head->origin, -7.0f, torso->axis[1], p );
		VectorMA( p, 0.5f, torso->axis[2], p );
		VectorMA( p, 3.0f, torso->axis[0], p );
		PR_Light( p, 3.0f, 1.0f, 0.12f, 0.06f, pulse );
	}
}
