/*
===========================================================================
REARMED -- round 4 ENEMIES shared state (contract, bg_ds_enemy.h).
g_enemy[] holds per-client enemy facts several workers write (each its own fields) and
read. G_EN_Publish mirrors the visible part into CS_DSENEMY for cgame.
Owners: arch en-types, affix en-affix, rank/grudge en-personality, morale/flags
en-feel (stagger/crawl/panic/limbs), en-types (telegraph/shield), armorPct anyone.
===========================================================================
*/
#include "g_local.h"

gEnemy_t	g_enemy[MAX_CLIENTS];
static char	en_lastCS[MAX_INFO_STRING];
static int	en_dirty;

// a name without colour codes (Q_strncpyz + Q_CleanStr): the shared copy for the round 4 files
void G_EN_CleanName( const char *in, char *out, int size ) {
	Q_strncpyz( out, in, size );
	Q_CleanStr( out );
}

// G_InitGame / every map: everything back to a plain bot
void G_EN_Init( void ) {
	memset( g_enemy, 0, sizeof( g_enemy ) );
	en_lastCS[0] = 0;
	en_dirty = 1;
	trap_SetConfigstring( CS_DSENEMY, "" );
}

// ClientDisconnect / ClientBegin of a new bot: forget the slot
void G_EN_Clear( int clientNum ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}
	memset( &g_enemy[clientNum], 0, sizeof( g_enemy[0] ) );
	en_dirty = 1;
}

gEnemy_t *G_EN( int clientNum ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return NULL;
	}
	return &g_enemy[clientNum];
}

void G_EN_SetFlag( int clientNum, int flag, qboolean on ) {
	gEnemy_t	*e = G_EN( clientNum );
	int			old;

	if ( !e ) {
		return;
	}
	old = e->flags;
	if ( on ) {
		e->flags |= flag;
	} else {
		e->flags &= ~flag;
	}
	if ( old != e->flags ) {
		en_dirty = 1;
	}
}

qboolean G_EN_HasFlag( int clientNum, int flag ) {
	gEnemy_t	*e = G_EN( clientNum );

	return e && ( e->flags & flag ) ? qtrue : qfalse;
}

// call after changing a published field (arch, affix, rank, armorPct) directly
void G_EN_MarkDirty( void ) {
	en_dirty = 1;
}

// G_RunFrame, once: rebuild CS_DSENEMY if anything published changed
void G_EN_Publish( void ) {
	char		buf[MAX_INFO_STRING];
	char		val[64];
	gEnemy_t	*e;
	int			i;

	if ( !en_dirty ) {
		return;
	}
	en_dirty = 0;
	buf[0] = 0;
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		e = &g_enemy[i];
		if ( !g_entities[i].inuse ) {
			continue;
		}
		if ( !e->arch && !e->affix && !e->rank && !e->flags && !e->armorPct ) {
			continue;
		}
		Com_sprintf( val, sizeof( val ), "%i,%i,%i,%i,%i", e->arch, e->affix, e->rank, e->flags, e->armorPct );
		Info_SetValueForKey( buf, va( "%i", i ), val );
	}
	if ( strcmp( buf, en_lastCS ) ) {
		Q_strncpyz( en_lastCS, buf, sizeof( en_lastCS ) );
		trap_SetConfigstring( CS_DSENEMY, buf );
	}
}
