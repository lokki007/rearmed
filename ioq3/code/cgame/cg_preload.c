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
// cg_preload.c -- rearmed (ds-fixes): bot models loaded during the loading screen
//
// With cg_deferPlayers 1 (the default), a client that joined after the map had loaded got
// a stand-in model (the first loaded client's: the player's own) until the scoreboard was
// shown. DESCENT adds every floor's bots ~800 ms in, so every enemy looked like the player
// until TAB. Now:
//  - the server lists the coming bot models in CS_PRELOAD (game/g_preload.c); CG_PreloadRoster
//    loads them while the loading screen is up and CG_ScanForExistingClientInfo copies the
//    handles when the bot joins (no hitch, no disk hit);
//  - a bot in our own local game is never deferred (CG_PreloadLoadNow): an unannounced one
//    (addbot by hand) costs a short load instead of wearing the wrong model.
// Remote humans joining a net game keep the stock deferred loading.

#include "cg_local.h"

#define CG_PRELOAD_MAX		12
#define CG_PRELOAD_MINMEM	8000000		// stop preloading when the hunk gets this low

static clientInfo_t	cgPreload[CG_PRELOAD_MAX];
static int			cgNumPreload;

static void CG_PreloadSplit( const char *in, char *model, int modelSize, char *skin, int skinSize ) {
	char	*slash;

	Q_strncpyz( model, in, modelSize );
	slash = strchr( model, '/' );
	if ( !slash ) {
		Q_strncpyz( skin, "default", skinSize );
	} else {
		Q_strncpyz( skin, slash + 1, skinSize );
		*slash = 0;
	}
}

/*
=================
CG_PreloadRoster

CG_Init, after CG_RegisterClients (cg.loading is still set)
=================
*/
void CG_PreloadRoster( void ) {
	char			buf[MAX_INFO_STRING], *s, *end, *sp;
	clientInfo_t	*ci;

	cgNumPreload = 0;
	memset( cgPreload, 0, sizeof( cgPreload ) );
	// team games pick skins by team; forcemodel shows everybody as the player's model anyway
	if ( cgs.gametype >= GT_TEAM || cg_forceModel.integer ) {
		return;
	}
	Q_strncpyz( buf, CG_ConfigString( CS_PRELOAD ), sizeof( buf ) );
	s = buf;
	while ( *s && cgNumPreload < CG_PRELOAD_MAX ) {
		end = strchr( s, ';' );
		if ( end ) {
			*end = 0;
		}
		sp = strchr( s, ' ' );
		if ( sp ) {
			*sp++ = 0;
		} else {
			sp = s;
		}
		if ( s[0] ) {
			if ( trap_MemoryRemaining() < CG_PRELOAD_MINMEM ) {
				CG_Printf( "preload: memory is low, %i bot models left to load on demand\n", CG_PRELOAD_MAX - cgNumPreload );
				break;
			}
			ci = &cgPreload[cgNumPreload];
			memset( ci, 0, sizeof( *ci ) );
			CG_PreloadSplit( s, ci->modelName, sizeof( ci->modelName ), ci->skinName, sizeof( ci->skinName ) );
			CG_PreloadSplit( sp, ci->headModelName, sizeof( ci->headModelName ), ci->headSkinName, sizeof( ci->headSkinName ) );
			CG_LoadingString( va( "%s", ci->modelName ) );
			CG_LoadClientInfoNow( ci );
			ci->infoValid = qtrue;
			cgNumPreload++;
		}
		if ( !end ) {
			break;
		}
		s = end + 1;
	}
	if ( cgNumPreload ) {
		CG_Printf( "preload: %i bot models loaded\n", cgNumPreload );
	}
}

/*
=================
CG_PreloadFind

A preloaded clientinfo with the same models and skins, or NULL
=================
*/
const clientInfo_t *CG_PreloadFind( const clientInfo_t *ci ) {
	int		i;

	if ( cgs.gametype >= GT_TEAM ) {
		return NULL;
	}
	for ( i = 0 ; i < cgNumPreload ; i++ ) {
		if ( !Q_stricmp( ci->modelName, cgPreload[i].modelName )
			&& !Q_stricmp( ci->skinName, cgPreload[i].skinName )
			&& !Q_stricmp( ci->headModelName, cgPreload[i].headModelName )
			&& !Q_stricmp( ci->headSkinName, cgPreload[i].headSkinName ) ) {
			return &cgPreload[i];
		}
	}
	return NULL;
}

/*
=================
CG_PreloadReport_f

dev "dsmodels": every client's model and whether it wears it (OWN) or a stand-in (deferred)
=================
*/
void CG_PreloadReport_f( void ) {
	clientInfo_t	*ci;
	qhandle_t		own;
	int				i, standIns = 0;

	for ( i = 0 ; i < cgs.maxclients ; i++ ) {
		ci = &cgs.clientinfo[i];
		if ( !ci->infoValid ) {
			continue;
		}
		own = trap_R_RegisterModel( va( "models/players/%s/lower.md3", ci->modelName ) );
		if ( ci->deferred || ( own && own != ci->legsModel ) ) {
			standIns++;
		}
		CG_DevPrintf( "DSModels: %i %s model %s/%s deferred %i wears %s\n", i, ci->name, ci->modelName, ci->skinName,
			ci->deferred, ( own && own == ci->legsModel ) ? "OWN" : "STAND-IN" );
	}
	CG_DevPrintf( "DSModels: %i stand-ins, %i preloaded\n", standIns, cgNumPreload );
}

/*
=================
CG_PreloadLoadNow

A bot joining our own local game (the player is alone with bots): load it at once
=================
*/
qboolean CG_PreloadLoadNow( const char *configstring ) {
	return cgs.localServer && Info_ValueForKey( configstring, "skill" )[0];
}
