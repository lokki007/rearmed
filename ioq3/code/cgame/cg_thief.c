/*
===========================================================================
REARMED round 4 (en-brains) -- cg_thief.c: weapon thieves, cgame side (game/g_thief.c).

- Edge callout (server cmd thc <kind> <thief> <weapon> <skulls>): right edge, under the other
  round 4 callouts, ~3 s. 1 = "STOLEN / <NAME> TOOK YOUR RAILGUN" (warning ping),
  2 = "RECLAIMED / RAILGUN  +2 SKULLS" (medal sound).
- On the body: a thief (DSEF_THIEF in CS_DSENEMY) has a faint gold glint at its gun hand that
  flares every 1.6 s. Nothing overhead, nothing in the screen centre.
===========================================================================
*/
#include "cg_local.h"

#define THC_TIME	3000
#define THC_MAXQ	3

typedef struct {
	int		kind, client, weapon, skulls, time;
} thCallout_t;

static thCallout_t	thq[THC_MAXQ];
static int			thqNum;
static sfxHandle_t	thSndStolen, thSndReclaim;

void CG_TH_Register( void ) {
	thSndStolen = trap_S_RegisterSound( "sound/rearmed/ann_warn.wav", qfalse );
	thSndReclaim = trap_S_RegisterSound( "sound/rearmed/ann_medal.wav", qfalse );
	thqNum = 0;
}

qboolean CG_TH_ServerCommand( const char *cmd ) {
	thCallout_t	*q;

	if ( Q_stricmp( cmd, "thc" ) ) {
		return qfalse;
	}
	// a reclaim replaces the "stolen" line of the same thief (shown or still queued)
	if ( atoi( CG_Argv( 1 ) ) == 2 ) {
		int		i, c = atoi( CG_Argv( 2 ) );

		for ( i = 0 ; i < thqNum ; ) {
			if ( thq[i].kind == 1 && thq[i].client == c ) {
				memmove( &thq[i], &thq[i + 1], sizeof( thq[0] ) * ( thqNum - i - 1 ) );
				thqNum--;
			} else {
				i++;
			}
		}
	}
	if ( thqNum >= THC_MAXQ ) {
		memmove( &thq[0], &thq[1], sizeof( thq[0] ) * ( THC_MAXQ - 1 ) );
		thqNum = THC_MAXQ - 1;
	}
	q = &thq[thqNum++];
	memset( q, 0, sizeof( *q ) );
	q->kind = atoi( CG_Argv( 1 ) );
	q->client = atoi( CG_Argv( 2 ) );
	q->weapon = atoi( CG_Argv( 3 ) );
	q->skulls = atoi( CG_Argv( 4 ) );
	if ( cg_debugEvents.integer ) {		// (not on the player's screen in normal play)
		CG_Printf( "ThiefCallout: kind %i client %i weapon %i skulls %i\n", q->kind, q->client, q->weapon, q->skulls );
	}
	return qtrue;
}

void CG_TH_Draw2D( void ) {
	thCallout_t	*q;
	gitem_t		*it;
	vec3_t		acc;
	float		t, al, slide, x = 620, y;
	char		name[32], wname[32], l1[48], l2[96];
	static const vec3_t	bg = { 0.06f, 0.04f, 0.02f }, body = { 0.9f, 0.86f, 0.8f };

	if ( !thqNum || cg.snap == NULL || cg.predictedPlayerState.pm_type == PM_INTERMISSION ) {
		return;
	}
	q = &thq[0];
	if ( !q->time ) {
		q->time = cg.time;
		trap_S_StartLocalSound( q->kind == 2 ? thSndReclaim : thSndStolen, CHAN_LOCAL_SOUND );
	}
	t = cg.time - q->time;
	if ( t < 0 || t > THC_TIME ) {
		memmove( &thq[0], &thq[1], sizeof( thq[0] ) * ( THC_MAXQ - 1 ) );
		thqNum--;
		return;
	}
	al = CG_Clamp01( t / 150.0f ) * CG_Clamp01( ( THC_TIME - t ) / 400.0f );
	slide = 18.0f * ( 1.0f - CG_Clamp01( t / 180.0f ) );
	CG_ClientNameUpper( q->client, name, sizeof( name ) );
	it = ( q->weapon > WP_NONE && q->weapon < WP_NUM_WEAPONS ) ? BG_FindItemForWeapon( q->weapon ) : NULL;
	Q_strncpyz( wname, it ? it->pickup_name : "WEAPON", sizeof( wname ) );
	Q_strupr( wname );
	if ( q->kind == 2 ) {
		Q_strncpyz( l1, "RECLAIMED", sizeof( l1 ) );
		if ( q->skulls > 0 ) {
			Com_sprintf( l2, sizeof( l2 ), "%s  +%i SKULL%s", wname, q->skulls, q->skulls == 1 ? "" : "S" );
		} else {
			Com_sprintf( l2, sizeof( l2 ), "%s IS YOURS AGAIN", wname );
		}
		acc[0] = 0.95f; acc[1] = 0.75f; acc[2] = 0.25f;
	} else {
		Q_strncpyz( l1, "STOLEN", sizeof( l1 ) );
		Com_sprintf( l2, sizeof( l2 ), "%s TOOK YOUR %s", name, wname );
		acc[0] = 0.95f; acc[1] = 0.45f; acc[2] = 0.1f;
	}
	y = CG_EdgeSlot( EDGE_THIEF, 25 ) + 3;		// en-screens: the shared right-edge stack
	CG_HudAnchor( 1 );
	CG_EdgeCard( x + slide, y, 25, al, l1, l2, acc, bg, 0.3f, body );
	CG_HudAnchor( 0 );
}

/*
CG_Player, after the head: a faint gold glint at a thief's gun hand
*/
void CG_TH_PlayerTell( centity_t *cent, refEntity_t *torso ) {
	clientInfo_t	*ci;
	orientation_t	tag;
	refEntity_t		re;
	vec3_t			p;
	float			flare, a;
	int				i, c = cent->currentState.clientNum, phase;

	if ( c < 0 || c >= MAX_CLIENTS || ( cent->currentState.eFlags & EF_DEAD ) || c == cg.snap->ps.clientNum ) {
		return;
	}
	if ( !( CG_EN( c )->flags & DSEF_THIEF ) ) {
		return;
	}
	ci = &cgs.clientinfo[c];
	if ( !ci->torsoModel || !trap_R_LerpTag( &tag, ci->torsoModel, torso->oldframe, torso->frame, 1.0f - torso->backlerp, "tag_weapon" ) ) {
		return;
	}
	VectorCopy( torso->origin, p );
	for ( i = 0 ; i < 3 ; i++ ) {
		VectorMA( p, tag.origin[i], torso->axis[i], p );
	}
	// just above and in front of the hand, so the gun model doesn't swallow it
	VectorMA( p, 5.0f, torso->axis[2], p );
	VectorMA( p, 4.0f, torso->axis[0], p );
	phase = ( cg.time + c * 211 ) % 1600;
	flare = phase < 260 ? 1.0f - phase / 260.0f : 0;
	a = 0.45f + 0.55f * flare;
	memset( &re, 0, sizeof( re ) );
	re.reType = RT_SPRITE;
	VectorCopy( p, re.origin );
	re.radius = 2.6f + 4.0f * flare;
	re.customShader = cgs.media.fxGlowShader;
	re.shaderRGBA[0] = (byte)( 255 * a );
	re.shaderRGBA[1] = (byte)( 200 * a );
	re.shaderRGBA[2] = (byte)( 70 * a );
	re.shaderRGBA[3] = 255;
	trap_R_AddRefEntityToScene( &re );
}
