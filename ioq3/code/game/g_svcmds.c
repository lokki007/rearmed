/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
//

// this file holds commands that can be executed by the server console, but not remote clients

#include "g_local.h"


/*
==============================================================================

PACKET FILTERING
 

You can add or remove addresses from the filter list with:

addip <ip>
removeip <ip>

The ip address is specified in dot format, and you can use '*' to match any value
so you can specify an entire class C network with "addip 192.246.40.*"

Removeip will only remove an address specified exactly the same way.  You cannot addip a subnet, then removeip a single host.

listip
Prints the current list of filters.

g_filterban <0 or 1>

If 1 (the default), then ip addresses matching the current list will be prohibited from entering the game.  This is the default setting.

If 0, then only addresses matching the list will be allowed.  This lets you easily set up a private game, or a game that only allows players from your local network.

TTimo NOTE: for persistence, bans are stored in g_banIPs cvar MAX_CVAR_VALUE_STRING
The size of the cvar string buffer is limiting the banning to around 20 masks
this could be improved by putting some g_banIPs2 g_banIps3 etc. maybe
still, you should rely on PB for banning instead

==============================================================================
*/

typedef struct ipFilter_s
{
	unsigned	mask;
	unsigned	compare;
} ipFilter_t;

#define	MAX_IPFILTERS	1024

static ipFilter_t	ipFilters[MAX_IPFILTERS];
static int			numIPFilters;

/*
=================
StringToFilter
=================
*/
static qboolean StringToFilter (char *s, ipFilter_t *f)
{
	char	num[128];
	int		i, j;
	byte	b[4];
	byte	m[4];
	
	for (i=0 ; i<4 ; i++)
	{
		b[i] = 0;
		m[i] = 0;
	}
	
	for (i=0 ; i<4 ; i++)
	{
		if (*s < '0' || *s > '9')
		{
			if (*s == '*') // 'match any'
			{
				// b[i] and m[i] to 0
				s++;
				if (!*s)
					break;
				s++;
				continue;
			}
			G_Printf( "Bad filter address: %s\n", s );
			return qfalse;
		}
		
		j = 0;
		while (*s >= '0' && *s <= '9')
		{
			num[j++] = *s++;
		}
		num[j] = 0;
		b[i] = atoi(num);
		m[i] = 255;

		if (!*s)
			break;
		s++;
	}
	
	f->mask = *(unsigned *)m;
	f->compare = *(unsigned *)b;
	
	return qtrue;
}

/*
=================
UpdateIPBans
=================
*/
static void UpdateIPBans (void)
{
	byte	b[4] = {0};
	byte	m[4] = {0};
	int		i,j;
	char	iplist_final[MAX_CVAR_VALUE_STRING] = {0};
	char	ip[64] = {0};

	*iplist_final = 0;
	for (i = 0 ; i < numIPFilters ; i++)
	{
		if (ipFilters[i].compare == 0xffffffff)
			continue;

		*(unsigned *)b = ipFilters[i].compare;
		*(unsigned *)m = ipFilters[i].mask;
		*ip = 0;
		for (j = 0 ; j < 4 ; j++)
		{
			if (m[j]!=255)
				Q_strcat(ip, sizeof(ip), "*");
			else
				Q_strcat(ip, sizeof(ip), va("%i", b[j]));
			Q_strcat(ip, sizeof(ip), (j<3) ? "." : " ");
		}		
		if (strlen(iplist_final)+strlen(ip) < MAX_CVAR_VALUE_STRING)
		{
			Q_strcat( iplist_final, sizeof(iplist_final), ip);
		}
		else
		{
			Com_Printf("g_banIPs overflowed at MAX_CVAR_VALUE_STRING\n");
			break;
		}
	}

	trap_Cvar_Set( "g_banIPs", iplist_final );
}

/*
=================
G_FilterPacket
=================
*/
qboolean G_FilterPacket (char *from)
{
	int		i;
	unsigned	in;
	byte m[4] = {0};
	char *p;

	i = 0;
	p = from;
	while (*p && i < 4) {
		m[i] = 0;
		while (*p >= '0' && *p <= '9') {
			m[i] = m[i]*10 + (*p - '0');
			p++;
		}
		if (!*p || *p == ':')
			break;
		i++, p++;
	}
	
	in = *(unsigned *)m;

	for (i=0 ; i<numIPFilters ; i++)
		if ( (in & ipFilters[i].mask) == ipFilters[i].compare)
			return g_filterBan.integer != 0;

	return g_filterBan.integer == 0;
}

/*
=================
AddIP
=================
*/
static void AddIP( char *str )
{
	int		i;

	for (i = 0 ; i < numIPFilters ; i++)
		if (ipFilters[i].compare == 0xffffffff)
			break;		// free spot
	if (i == numIPFilters)
	{
		if (numIPFilters == MAX_IPFILTERS)
		{
			G_Printf ("IP filter list is full\n");
			return;
		}
		numIPFilters++;
	}
	
	if (!StringToFilter (str, &ipFilters[i]))
		ipFilters[i].compare = 0xffffffffu;

	UpdateIPBans();
}

/*
=================
G_ProcessIPBans
=================
*/
void G_ProcessIPBans(void) 
{
	char *s, *t;
	char		str[MAX_CVAR_VALUE_STRING];

	Q_strncpyz( str, g_banIPs.string, sizeof(str) );

	for (t = s = g_banIPs.string; *t; /* */ ) {
		s = strchr(s, ' ');
		if (!s)
			break;
		while (*s == ' ')
			*s++ = 0;
		if (*t)
			AddIP( t );
		t = s;
	}
}


/*
=================
Svcmd_AddIP_f
=================
*/
void Svcmd_AddIP_f (void)
{
	char		str[MAX_TOKEN_CHARS];

	if ( trap_Argc() < 2 ) {
		G_Printf("Usage: addip <ip-mask>\n");
		return;
	}

	trap_Argv( 1, str, sizeof( str ) );

	AddIP( str );

}

/*
=================
Svcmd_RemoveIP_f
=================
*/
void Svcmd_RemoveIP_f (void)
{
	ipFilter_t	f;
	int			i;
	char		str[MAX_TOKEN_CHARS];

	if ( trap_Argc() < 2 ) {
		G_Printf("Usage: removeip <ip-mask>\n");
		return;
	}

	trap_Argv( 1, str, sizeof( str ) );

	if (!StringToFilter (str, &f))
		return;

	for (i=0 ; i<numIPFilters ; i++) {
		if (ipFilters[i].mask == f.mask	&&
			ipFilters[i].compare == f.compare) {
			ipFilters[i].compare = 0xffffffffu;
			G_Printf ("Removed.\n");

			UpdateIPBans();
			return;
		}
	}

	G_Printf ( "Didn't find %s.\n", str );
}

/*
===================
Svcmd_EntityList_f
===================
*/
void	Svcmd_EntityList_f (void) {
	int			e;
	gentity_t		*check;

	check = g_entities;
	for (e = 0; e < level.num_entities ; e++, check++) {
		if ( !check->inuse ) {
			continue;
		}
		G_Printf("%3i:", e);
		switch ( check->s.eType ) {
		case ET_GENERAL:
			G_Printf("ET_GENERAL          ");
			break;
		case ET_PLAYER:
			G_Printf("ET_PLAYER           ");
			break;
		case ET_ITEM:
			G_Printf("ET_ITEM             ");
			break;
		case ET_MISSILE:
			G_Printf("ET_MISSILE          ");
			break;
		case ET_MOVER:
			G_Printf("ET_MOVER            ");
			break;
		case ET_BEAM:
			G_Printf("ET_BEAM             ");
			break;
		case ET_PORTAL:
			G_Printf("ET_PORTAL           ");
			break;
		case ET_SPEAKER:
			G_Printf("ET_SPEAKER          ");
			break;
		case ET_PUSH_TRIGGER:
			G_Printf("ET_PUSH_TRIGGER     ");
			break;
		case ET_TELEPORT_TRIGGER:
			G_Printf("ET_TELEPORT_TRIGGER ");
			break;
		case ET_INVISIBLE:
			G_Printf("ET_INVISIBLE        ");
			break;
		case ET_GRAPPLE:
			G_Printf("ET_GRAPPLE          ");
			break;
		default:
			G_Printf("%3i                 ", check->s.eType);
			break;
		}

		if ( check->classname ) {
			G_Printf("%s", check->classname);
		}
		G_Printf("\n");
	}
}

gclient_t	*ClientForString( const char *s ) {
	gclient_t	*cl;
	int			i;
	int			idnum;

	// numeric values are just slot numbers
	if ( s[0] >= '0' && s[0] <= '9' ) {
		idnum = atoi( s );
		if ( idnum < 0 || idnum >= level.maxclients ) {
			Com_Printf( "Bad client slot: %i\n", idnum );
			return NULL;
		}

		cl = &level.clients[idnum];
		if ( cl->pers.connected == CON_DISCONNECTED ) {
			G_Printf( "Client %i is not connected\n", idnum );
			return NULL;
		}
		return cl;
	}

	// check for a name match
	for ( i=0 ; i < level.maxclients ; i++ ) {
		cl = &level.clients[i];
		if ( cl->pers.connected == CON_DISCONNECTED ) {
			continue;
		}
		if ( !Q_stricmp( cl->pers.netname, s ) ) {
			return cl;
		}
	}

	G_Printf( "User %s is not on the server\n", s );

	return NULL;
}

/*
===================
Svcmd_ForceTeam_f

forceteam <player> <team>
===================
*/
void	Svcmd_ForceTeam_f( void ) {
	gclient_t	*cl;
	char		str[MAX_TOKEN_CHARS];

	if ( trap_Argc() < 3 ) {
		G_Printf("Usage: forceteam <player> <team>\n");
		return;
	}

	// find the player
	trap_Argv( 1, str, sizeof( str ) );
	cl = ClientForString( str );
	if ( !cl ) {
		return;
	}

	// set the team
	trap_Argv( 2, str, sizeof( str ) );
	SetTeam( &g_entities[cl - level.clients], str );
}

char	*ConcatArgs( int start );

/*
=================
ConsoleCommand

=================
*/
/*
=================
Svcmd_FindWall_f

Dev tool: list floor spots next to a long straight wall (wall run test sites).
Prints origin, run yaw and the wall side.
=================
*/
static void Svcmd_FindWall_f( void ) {
	static const vec3_t	mins = { -15, -15, -24 }, maxs = { 15, 15, 32 };
	trace_t		tr;
	vec3_t		top, bottom, org, side, end, run;
	int			x, y, d, found;
	float		yaw, s;

	found = 0;
	for ( x = -4096 ; x <= 4096 && found < 12 ; x += 96 ) {
		for ( y = -4096 ; y <= 4096 && found < 12 ; y += 96 ) {
			VectorSet( top, x, y, 4096 );
			VectorSet( bottom, x, y, -4096 );
			trap_Trace( &tr, top, NULL, NULL, bottom, ENTITYNUM_NONE, MASK_PLAYERSOLID );
			if ( tr.fraction == 1.0f || tr.startsolid || tr.plane.normal[2] < 0.9f || ( tr.surfaceFlags & SURF_SKY ) ) {
				continue;
			}
			VectorCopy( tr.endpos, org );
			org[2] += 25;
			trap_Trace( &tr, org, mins, maxs, org, ENTITYNUM_NONE, MASK_PLAYERSOLID );
			if ( tr.startsolid ) {
				continue;
			}
			for ( d = 0 ; d < 4 ; d++ ) {
				yaw = d * 90;
				VectorSet( run, cos( DEG2RAD( yaw ) ), sin( DEG2RAD( yaw ) ), 0 );
				for ( s = -1 ; s <= 1 ; s += 2 ) {
					VectorSet( side, run[1] * s, -run[0] * s, 0 );
					VectorMA( org, 40, side, end );
					trap_Trace( &tr, org, mins, maxs, end, ENTITYNUM_NONE, MASK_PLAYERSOLID );
					if ( tr.fraction == 1.0f || fabs( tr.plane.normal[2] ) > 0.3f ) {
						continue;
					}
					// a real wall, still there at jump height
					VectorCopy( org, top );
					top[2] += 90;
					VectorMA( top, 40, side, bottom );
					trap_Trace( &tr, top, mins, maxs, bottom, ENTITYNUM_NONE, MASK_PLAYERSOLID );
					if ( tr.fraction == 1.0f || tr.startsolid || fabs( tr.plane.normal[2] ) > 0.3f ) {
						continue;
					}
					VectorMA( org, 40 * tr.fraction - 8, side, top );
					top[2] += 40;
					VectorMA( top, 700, run, end );
					trap_Trace( &tr, top, mins, maxs, end, ENTITYNUM_NONE, MASK_PLAYERSOLID );
					if ( tr.fraction < 1.0f ) {
						continue;
					}
					VectorMA( end, 40, side, bottom );
					trap_Trace( &tr, end, mins, maxs, bottom, ENTITYNUM_NONE, MASK_PLAYERSOLID );
					if ( tr.fraction == 1.0f ) {
						continue;
					}
					G_Printf( "wall: %s yaw %i wall %s\n", vtos( org ), (int)yaw, s > 0 ? "right" : "left" );
					found++;
					break;
				}
			}
		}
	}
	G_Printf( "findwall: %i\n", found );
}

/*
===================
Svcmd_Teleport_f

teleport <clientnum> <x> <y> <z> [yaw] [pitch]
Places a client at rest, for setting up test scenes (e.g. with bot_pause 1)
===================
*/
static void Svcmd_Teleport_f( void ) {
	gentity_t	*ent;
	vec3_t		angles;
	char		buf[MAX_TOKEN_CHARS];
	int			i, num;

	if ( trap_Argc() < 5 ) {
		G_Printf( "usage: teleport <clientnum> <x> <y> <z> [yaw]\n" );
		return;
	}
	trap_Argv( 1, buf, sizeof( buf ) );
	num = atoi( buf );
	if ( num < 0 || num >= level.maxclients || !g_entities[num].inuse || !g_entities[num].client ) {
		G_Printf( "teleport: bad client %i\n", num );
		return;
	}
	ent = &g_entities[num];
	for ( i = 0 ; i < 3 ; i++ ) {
		trap_Argv( 2 + i, buf, sizeof( buf ) );
		ent->client->ps.origin[i] = atof( buf );
	}
	VectorClear( angles );
	if ( trap_Argc() > 5 ) {
		trap_Argv( 5, buf, sizeof( buf ) );
		angles[YAW] = atof( buf );
	}
	if ( trap_Argc() > 6 ) {		// rearmed: optional pitch (aim tests)
		trap_Argv( 6, buf, sizeof( buf ) );
		angles[PITCH] = atof( buf );
	}
	VectorClear( ent->client->ps.velocity );
	ent->client->ps.eFlags ^= EF_TELEPORT_BIT;
	ent->client->ps.loopSound &= ~RMZ_ZIPPING;
	SetClientViewAngle( ent, angles );
	BG_PlayerStateToEntityState( &ent->client->ps, &ent->s, qtrue );
	VectorCopy( ent->client->ps.origin, ent->r.currentOrigin );
	trap_LinkEntity( ent );
}

/*
=================
SvDev_Blocked

Round 4 dev commands that change the match or saved progress. With two or more humans connected
(a 1v1 online) they need cheats (devmap / sv_cheats 1) or developer 1; single player and the bot-only
test / sim servers are unchanged. Read-only ones (btstats, prstat, dsvet, dstypestats, ...) stay open.
=================
*/
static const char *svDevCmds[] = {
	"btthief", "hftest", "prsay", "prgrudge", "prkill", "prkillme", "prflag", "dsnemesis",
	"dstypes", "dstype", "dstypeact", "dstadapt", "dsaffix", "dschampions", "dsbst", "dsbstmeet",
	"dsbstkill", "dsbm", "dsbosstime", NULL
};

static qboolean SvDev_Blocked( const char *cmd ) {
	int		i, humans;

	for ( i = 0 ; svDevCmds[i] && Q_stricmp( cmd, svDevCmds[i] ) ; i++ ) {
	}
	if ( !svDevCmds[i] || g_cheats.integer || trap_Cvar_VariableIntegerValue( "developer" ) ) {
		return qfalse;
	}
	humans = 0;
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( level.clients[i].pers.connected != CON_DISCONNECTED && !( g_entities[i].r.svFlags & SVF_BOT ) ) {
			humans++;
		}
	}
	if ( humans < 2 ) {
		return qfalse;
	}
	G_Printf( "%s is a dev command: with two or more players it needs devmap (sv_cheats 1) or developer 1\n", cmd );
	return qtrue;
}

qboolean	ConsoleCommand( void ) {
	char	cmd[MAX_TOKEN_CHARS];

	trap_Argv( 0, cmd, sizeof( cmd ) );

	if ( SvDev_Blocked( cmd ) ) {
		return qtrue;
	}

	if ( Q_stricmp (cmd, "entitylist") == 0 ) {
		Svcmd_EntityList_f();
		return qtrue;
	}

	if ( Q_stricmp (cmd, "forceteam") == 0 ) {
		Svcmd_ForceTeam_f();
		return qtrue;
	}

	if (Q_stricmp (cmd, "game_memory") == 0) {
		Svcmd_GameMem_f();
		return qtrue;
	}

	if (Q_stricmp (cmd, "addbot") == 0) {
		Svcmd_AddBot_f();
		return qtrue;
	}

	if (Q_stricmp (cmd, "teleport") == 0) {
		Svcmd_Teleport_f();
		return qtrue;
	}

	if ( G_KillCamConsoleCommand( cmd ) ) {	// kill cam dev: kchurt
		return qtrue;
	}
	if ( G_PreloadCommand( cmd ) ) {		// ds-fixes dev: dslineup
		return qtrue;
	}
	if ( G_RearmConsoleCommand( cmd ) ) {	// mutator / perkgive / perkoffer / slowmo
		return qtrue;
	}

	if ( G_DS_ArsenalCommand( cmd ) ) {		// descent dev: dslevel / dsxp / dsevolve / dsstate
		return qtrue;
	}
	if ( G_RL_ConsoleCommand( cmd ) ) {		// descent dev: rlstate / rlwin / rllose / rlgive / rlkits
		return qtrue;
	}
	if ( G_DS_SecretsCommand( cmd ) ) {		// ds-secrets dev: dssecrets / dsstash
		return qtrue;
	}
	if ( G_PR_Command( cmd ) ) {			// en-personality dev: prstat / prsay / prgrudge / dsvet
		return qtrue;
	}

	if ( G_CorpseCommand( cmd ) ) {			// gorestats / gorefill (g_corpses.c)
		return qtrue;
	}
	if ( G_HF_Command( cmd ) ) {			// en-feel dev: hftest
		return qtrue;
	}
	if ( BotTacticsCommand( cmd ) ) {		// en-brains dev: btstats / btreset
		return qtrue;
	}
	if ( G_TH_Command( cmd ) ) {			// en-brains dev: btthief
		return qtrue;
	}
	if (Q_stricmp (cmd, "findwall") == 0) {
		Svcmd_FindWall_f();
		return qtrue;
	}

	if (Q_stricmp (cmd, "where") == 0) {
		int i;
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			gclient_t *cl = &level.clients[i];
			if ( cl->pers.connected == CON_CONNECTED ) {
				G_Printf( "client %i %s: origin %s health %i pm_type %i score %i gg %i streak %i bounty %i\n", i, cl->pers.netname,
					vtos( cl->ps.origin ), cl->ps.stats[STAT_HEALTH], cl->ps.pm_type, cl->ps.persistant[PERS_SCORE],
					cl->pers.ggLevel, cl->streak, cl->bounty );
			}
		}
		return qtrue;
	}

	if (Q_stricmp (cmd, "botlist") == 0) {
		Svcmd_BotList_f();
		return qtrue;
	}

	if (Q_stricmp (cmd, "abort_podium") == 0) {
		Svcmd_AbortPodium_f();
		return qtrue;
	}

	if (Q_stricmp (cmd, "addip") == 0) {
		Svcmd_AddIP_f();
		return qtrue;
	}

	if (Q_stricmp (cmd, "removeip") == 0) {
		Svcmd_RemoveIP_f();
		return qtrue;
	}

	if (Q_stricmp (cmd, "listip") == 0) {
		trap_SendConsoleCommand( EXEC_NOW, "g_banIPs\n" );
		return qtrue;
	}

	if (g_dedicated.integer) {
		if (Q_stricmp (cmd, "say") == 0) {
			trap_SendServerCommand( -1, va("print \"server: %s\n\"", ConcatArgs(1) ) );
			return qtrue;
		}
		// everything else will also be printed as a say command
		trap_SendServerCommand( -1, va("print \"server: %s\n\"", ConcatArgs(0) ) );
		return qtrue;
	}

	return qfalse;
}

