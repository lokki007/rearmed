/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.
===========================================================================
*/
// g_preload.c -- rearmed (ds-fixes): tell the cgame which bot models are coming
//
// Bots that join after the client has loaded the map (DESCENT adds a floor's bots ~800 ms in,
// skirmish adds them a few frames after "map") used to get a deferred model: with
// cg_deferPlayers 1 the cgame dressed them in the first loaded client's model (the player's
// own) until the scoreboard came up. The server knows the roster at G_InitGame, so it lists
// the models in CS_PRELOAD and the cgame loads them during the loading screen; when the bot
// joins, its clientinfo is copied from that cache without a disk hit.
//
// CS_PRELOAD: "model hmodel;model hmodel;..." (the same strings the bots' userinfo will carry)

#include "g_local.h"

#define PRELOAD_MAX		12

static char		preloadList[MAX_INFO_STRING];
static int		preloadCount;

void G_PreloadReset( void ) {
	preloadList[0] = 0;
	preloadCount = 0;
}

/*
=================
G_PreloadAddBot

name: a bot's name in scripts/bots.txt (G_AddBot's model / headmodel defaults)
=================
*/
void G_PreloadAddBot( const char *name ) {
	char		*botinfo;
	char		model[MAX_QPATH], head[MAX_QPATH], entry[MAX_QPATH * 2 + 4];

	if ( !name || !name[0] || preloadCount >= PRELOAD_MAX ) {
		return;
	}
	botinfo = G_GetBotInfoByName( name );
	if ( !botinfo ) {
		return;
	}
	Q_strncpyz( model, Info_ValueForKey( botinfo, "model" ), sizeof( model ) );
	if ( !model[0] ) {
		Q_strncpyz( model, "visor/default", sizeof( model ) );
	}
	Q_strncpyz( head, Info_ValueForKey( botinfo, "headmodel" ), sizeof( head ) );
	if ( !head[0] ) {
		Q_strncpyz( head, model, sizeof( head ) );
	}
	if ( strchr( model, ';' ) || strchr( model, ' ' ) || strchr( head, ';' ) || strchr( head, ' ' ) ) {
		return;
	}
	Com_sprintf( entry, sizeof( entry ), "%s %s;", model, head );
	if ( strstr( preloadList, entry ) ) {
		return;		// two bots, one model
	}
	if ( strlen( preloadList ) + strlen( entry ) >= sizeof( preloadList ) - 1 ) {
		return;
	}
	Q_strcat( preloadList, sizeof( preloadList ), entry );
	preloadCount++;
}

// en-types: a model a bot will be forced into ("visor/default"; head = the same)
void G_PreloadAddModel( const char *model, const char *head ) {
	char		entry[MAX_QPATH * 2 + 4];

	if ( !model || !model[0] || preloadCount >= PRELOAD_MAX ) {
		return;
	}
	if ( !head || !head[0] ) {
		head = model;
	}
	if ( strchr( model, ';' ) || strchr( model, ' ' ) || strchr( head, ';' ) || strchr( head, ' ' ) ) {
		return;
	}
	Com_sprintf( entry, sizeof( entry ), "%s %s;", model, head );
	if ( strstr( preloadList, entry ) || strlen( preloadList ) + strlen( entry ) >= sizeof( preloadList ) - 1 ) {
		return;
	}
	Q_strcat( preloadList, sizeof( preloadList ), entry );
	preloadCount++;
}

/*
=================
G_PreloadAddList

Space separated bot names (the g_preloadBots cvar the skirmish menu sets before "map")
=================
*/
void G_PreloadAddList( const char *names ) {
	char		buf[MAX_CVAR_VALUE_STRING], *s, *tok;

	Q_strncpyz( buf, names, sizeof( buf ) );
	s = buf;
	while ( 1 ) {
		tok = COM_Parse( &s );
		if ( !tok[0] ) {
			break;
		}
		G_PreloadAddBot( tok );
	}
}

void G_PreloadPublish( void ) {
	trap_SetConfigstring( CS_PRELOAD, preloadList );
	if ( preloadCount && trap_Cvar_VariableIntegerValue( "developer" ) ) {
		G_Printf( "preload: %i bot models: %s\n", preloadCount, preloadList );
	}
}

/*
=================
G_PreloadCommand

dev "dslineup [dist]": every bot stands in a row in front of client 0, facing it (screenshots of
the bots' models; pair with bot_pause 1)
=================
*/
qboolean G_PreloadCommand( const char *cmd ) {
	gentity_t	*pl, *ent;
	vec3_t		fwd, right, ang, spot, start;
	trace_t		tr;
	char		arg[MAX_TOKEN_CHARS];
	float		dist;
	int			i, n, k;
	static vec3_t	pmins = { -15, -15, -24 }, pmaxs = { 15, 15, 32 };

	if ( Q_stricmp( cmd, "dslineup" ) ) {
		return qfalse;
	}
	pl = &g_entities[0];
	if ( !pl->inuse || !pl->client ) {
		return qtrue;
	}
	trap_Argv( 1, arg, sizeof( arg ) );
	dist = atof( arg ) > 0 ? atof( arg ) : 170;
	VectorSet( ang, 0, pl->client->ps.viewangles[YAW], 0 );
	AngleVectors( ang, fwd, right, NULL );
	for ( i = 1, n = 0 ; i < level.maxclients ; i++ ) {
		if ( g_entities[i].inuse && ( g_entities[i].r.svFlags & SVF_BOT ) && g_entities[i].health > 0 ) {
			n++;
		}
	}
	VectorCopy( pl->client->ps.origin, start );
	for ( i = 1, k = 0 ; i < level.maxclients ; i++ ) {
		ent = &g_entities[i];
		if ( !ent->inuse || !( ent->r.svFlags & SVF_BOT ) || ent->health <= 0 ) {
			continue;
		}
		VectorMA( start, dist, fwd, spot );
		VectorMA( spot, ( k - ( n - 1 ) * 0.5f ) * 52, right, spot );
		trap_Trace( &tr, start, pmins, pmaxs, spot, 0, MASK_PLAYERSOLID & ~CONTENTS_BODY );
		VectorCopy( tr.endpos, ent->client->ps.origin );
		VectorClear( ent->client->ps.velocity );
		VectorSet( ang, 0, AngleMod( pl->client->ps.viewangles[YAW] + 180 ), 0 );
		ent->client->ps.eFlags ^= EF_TELEPORT_BIT;
		SetClientViewAngle( ent, ang );
		BG_PlayerStateToEntityState( &ent->client->ps, &ent->s, qtrue );
		VectorCopy( ent->client->ps.origin, ent->r.currentOrigin );
		trap_LinkEntity( ent );
		G_Printf( "dslineup: %i %s at %s\n", i, ent->client->pers.netname, vtos( ent->client->ps.origin ) );
		k++;
	}
	G_Printf( "dslineup: %i bots\n", k );
	return qtrue;
}

/*
=================
G_PreloadInit

Start of G_InitBots (G_InitGame): a fresh list, plus the skirmish menu's bots
=================
*/
void G_PreloadInit( void ) {
	char	names[MAX_CVAR_VALUE_STRING];

	G_PreloadReset();
	trap_Cvar_VariableStringBuffer( "g_preloadBots", names, sizeof( names ) );
	if ( names[0] ) {
		trap_Cvar_Set( "g_preloadBots", "" );		// one map's worth
		G_PreloadAddList( names );
	}
	G_PreloadPublish();
}
