/*
===========================================================================
REARMED -- DESCENT ARRIVALS (ds-arrivals, g_dsStagger 1), game side: the floor's bots join
one at a time instead of all at once.

The floor starts with the player and the first opponent (two when 5+ bots are planned); the
rest join one by one, DSA_Gap apart (about 6.5 s on floor 1 down to about 4.3 s deep down,
+-0.7 s). Each late arrival: a pillar of light (ds-surprise's RM_ENT_SURPRISE / DSXS_ARRIVAL)
on a spawn spot at least DS_SURPRISE_MIN_DIST (600) from the player (by preference 900+, out of
sight), the cgame reads that name alone and slides the portrait into the top strip
(cgame/cg_ds_intro.c), DSA_WARN later the bot lands there. Order: fight floors the regular
bots, then the nemesis, the elite last; boss floors the two rivals, then the boss with its big
introduction (a longer warning, 1100+ units away).
Frag race: a late joiner starts with the lowest score of the floor's bots already in (never
ahead of anyone, never hopelessly behind). The start bots also land 600+ from the player.
Bots are still only added once the AAS is up (g_roguelike.c G_RL_RunFrame); every planned
bot's model is preloaded on the loading screen (G_PreloadAddBot in G_RL_InitBots).

CS_DSARRIVE (742): "" = everyone at once (g_dsStagger 0, or nobody to stagger), else
	\st\1\ns\<start count>\o\<plan indexes in arrival order>\t0\<level time of the start>
	\w<i>\<warning level time>\l<i>\<landing time>\in<i>\<joined level time, -1 never>
	\hs<i>\<head start>   (i = plan index = CS_DSINTRO f<i>)
===========================================================================
*/
#include "g_local.h"

#ifndef DSXS_ARRIVAL
#define DSXS_ARRIVAL		3
#endif

#define DSA_MAX				12
#define DSA_WARN			1500	// warning (pillar, name) -> the bot lands
#define DSA_WARN_BOSS		2800
#define DSA_GAP_BOSS		6500
#define DSA_PREFER_DIST		900.0f
#define DSA_BOSS_DIST		1100.0f
#define DSA_JOIN_TIMEOUT	6000
#define DSA_JOIN_LEAD		0		// addbot this early (it joins the same frame; a "wait" in a cfg can hold it back: the pillar landing follows the real spawn)
#define DSA_REUSE_PENALTY	700.0f	// a spot an earlier arrival used this floor scores lower (variety)

typedef struct {
	qboolean	on;
	int			num, numStart;
	int			order[DSA_MAX];		// plan index by arrival position
	int			gap[DSA_MAX];		// msec from the previous landing, by arrival position
	int			pos;				// the next late arrival position
	int			t0, nextLand, lastAt, left;
	int			warnAt[DSA_MAX], landAt[DSA_MAX], inAt[DSA_MAX], added[DSA_MAX], head[DSA_MAX];	// by plan index
	vec3_t		spot[DSA_MAX];
	float		yaw[DSA_MAX];
	qboolean	hasSpot[DSA_MAX], boss[DSA_MAX];
	gentity_t	*pillar[DSA_MAX];
	int			maxFrame, watchUntil, lastReal;	// hitch watch: the longest real (wall clock) gap between game frames around a join
} dsArrive_t;

static dsArrive_t	dsa;
static vmCvar_t		dsaCvStagger;

static void DSA_Publish( void ) {
	char	s[MAX_INFO_STRING], o[64];
	int		i, p;

	if ( !dsa.on ) {
		trap_SetConfigstring( CS_DSARRIVE, "" );
		return;
	}
	o[0] = 0;
	for ( i = 0 ; i < dsa.num ; i++ ) {
		Q_strcat( o, sizeof( o ), va( i ? ",%i" : "%i", dsa.order[i] ) );
	}
	Com_sprintf( s, sizeof( s ), "\\st\\1\\ns\\%i\\o\\%s\\t0\\%i", dsa.numStart, o, dsa.t0 );
	for ( i = 0 ; i < dsa.num ; i++ ) {
		p = dsa.order[i];
		if ( dsa.warnAt[p] ) {
			Q_strcat( s, sizeof( s ), va( "\\w%i\\%i\\l%i\\%i", p, dsa.warnAt[p], p, dsa.landAt[p] ) );
		}
		if ( dsa.inAt[p] ) {
			Q_strcat( s, sizeof( s ), va( "\\in%i\\%i\\hs%i\\%i", p, dsa.inAt[p], p, dsa.head[p] ) );
		}
	}
	trap_SetConfigstring( CS_DSARRIVE, s );
}

// msec between two landings: slower near the top, a bit faster deeper down, +-700
static int DSA_Gap( int floor, int k ) {
	unsigned	h = (unsigned)floor * 2654435761u + (unsigned)k * 40503u + 977u;
	int			base = 6500 - 110 * ( floor - 1 ), g;

	if ( base < 4300 ) {
		base = 4300;
	}
	h ^= h >> 13;
	h *= 0x5bd1e995u;
	h ^= h >> 15;
	g = base - 700 + (int)( h % 1401u );
	return g < 4000 ? 4000 : g > 7000 ? 7000 : g;
}

/*
=================
G_DS_ArrivePlan

G_InitGame (G_DS_IntroPublish): the arrival order, how many come in at the start, the gaps
=================
*/
void G_DS_ArrivePlan( void ) {
	const char	*nm, *nem;
	char		line[256];
	int			i, n, pass, role, kind, floor;

	memset( &dsa, 0, sizeof( dsa ) );
	trap_Cvar_Register( &dsaCvStagger, "g_dsStagger", "1", CVAR_ARCHIVE );
	n = G_RL_PlanNum();
	if ( n > DSA_MAX ) {
		n = DSA_MAX;
	}
	if ( !G_RL_Active() || n <= 0 || dsaCvStagger.integer <= 0 ) {
		trap_SetConfigstring( CS_DSARRIVE, "" );
		if ( G_RL_Active() && n > 0 ) {
			G_LogPrintf( "DSArrive: g_dsStagger 0, all %i bots at once\n", n );
		}
		return;
	}
	nem = G_DS_NemesisPlanName( 0 );
	floor = G_RL_Floor();
	// pass 0 regular bots / rivals, 1 the nemesis, 2 the elite, 3 the boss
	for ( pass = 0 ; pass < 4 ; pass++ ) {
		for ( i = 0 ; i < n ; i++ ) {
			nm = G_RL_PlanName( i, &role );
			if ( !nm ) {
				continue;
			}
			kind = role == 4 ? 3 : role == 3 ? 2 : ( nem && !Q_stricmp( nem, nm ) ) ? 1 : 0;
			if ( kind == pass ) {
				dsa.boss[i] = role == 4;
				dsa.order[dsa.num++] = i;
			}
		}
	}
	dsa.numStart = dsa.num >= 5 ? 2 : 1;
	if ( dsa.num == 1 && dsa.boss[dsa.order[0]] ) {
		dsa.numStart = 0;		// en-boss: a timed 1v1 boss floor: the boss makes its entrance (pillar, 1100+ away)
	}
	if ( dsa.num <= dsa.numStart ) {
		trap_SetConfigstring( CS_DSARRIVE, "" );		// nobody to stagger
		return;
	}
	dsa.on = qtrue;
	dsa.left = dsa.num;
	for ( i = dsa.numStart ; i < dsa.num ; i++ ) {
		dsa.gap[i] = dsa.boss[dsa.order[i]] ? DSA_GAP_BOSS : DSA_Gap( floor, i );
	}
	DSA_Publish();
	line[0] = 0;
	for ( i = 0 ; i < dsa.num ; i++ ) {
		nm = G_RL_PlanName( dsa.order[i], &role );
		Q_strcat( line, sizeof( line ), va( "%s%s(+%i)", i ? " " : "", nm ? nm : "?", dsa.gap[i] ) );
	}
	G_LogPrintf( "DSArrive: plan floor %i, %i bots, %i at the start: %s\n", floor, dsa.num, dsa.numStart, line );
}

// g_roguelike.c, once the AAS is up: does plan bot i come in at the start?
qboolean G_DS_ArriveAtStart( int i ) {
	int		k;

	if ( !dsa.on ) {
		return qtrue;
	}
	for ( k = 0 ; k < dsa.numStart ; k++ ) {
		if ( dsa.order[k] == i ) {
			return qtrue;
		}
	}
	return qfalse;
}

// g_roguelike.c: the start bots were just added
void G_DS_ArriveBegin( void ) {
	int		k;

	if ( !dsa.on || dsa.t0 ) {
		return;
	}
	dsa.t0 = level.time;
	for ( k = 0 ; k < dsa.numStart ; k++ ) {
		dsa.added[dsa.order[k]] = level.time;
	}
	dsa.pos = dsa.numStart;
	dsa.nextLand = level.time + dsa.gap[dsa.pos];
	DSA_Publish();
	G_LogPrintf( "DSArrive: start at %i ms, first late landing at %i ms\n", level.time - level.startTime, dsa.nextLand - level.startTime );
}

int G_DS_ArrivalsLeft( void ) {
	return dsa.on ? dsa.left : 0;
}

int G_DS_ArrivalLastAt( void ) {
	return dsa.on ? dsa.lastAt : 0;
}

static gentity_t *DSA_Human( void ) {
	int		h = G_RL_HumanClient();

	return h >= 0 ? &g_entities[h] : NULL;
}

static float DSA_PlayerDist( const vec3_t p ) {
	gentity_t	*h = DSA_Human();

	return h && h->client ? Distance( p, h->r.currentOrigin ) : 99999.0f;
}

/*
=================
DSA_FarSpot

A deathmatch spawn for an arrival: never under DS_SURPRISE_MIN_DIST from the player, by
preference minDist+, the farther the better up to 2400, by preference out of the player's
sight, not on top of anyone, not where an earlier arrival landed a moment ago
=================
*/
static qboolean DSA_FarSpot( float minDist, int self, vec3_t out, float *yaw ) {
	gentity_t	*spot = NULL, *best = NULL, *h = DSA_Human(), *e;
	vec3_t		org, eye, mins = { -15, -15, -24 }, maxs = { 15, 15, 32 };
	trace_t		tr;
	float		d, score, bestScore = -1e9f;
	int			i;
	qboolean	near;

	while ( ( spot = G_Find( spot, FOFS( classname ), "info_player_deathmatch" ) ) != NULL ) {
		VectorCopy( spot->s.origin, org );
		org[2] += 9;
		trap_Trace( &tr, org, mins, maxs, org, self, MASK_PLAYERSOLID );
		if ( tr.startsolid || tr.allsolid ) {
			continue;
		}
		near = qfalse;
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			e = &g_entities[i];
			if ( i != self && e->inuse && e->client && e->client->pers.connected == CON_CONNECTED && e->health > 0
				&& e->client->sess.sessionTeam != TEAM_SPECTATOR && Distance( e->r.currentOrigin, org ) < 110 ) {
				near = qtrue;
			}
		}
		for ( i = 0 ; i < DSA_MAX ; i++ ) {
			if ( dsa.hasSpot[i] && dsa.landAt[i] > level.time - 4000 && dsa.landAt[i] != 0 && Distance( dsa.spot[i], org ) < 64
				&& i != self ) {
				near = qtrue;
			}
		}
		if ( near ) {
			continue;
		}
		d = h ? Distance( org, h->r.currentOrigin ) : 2000;
		if ( d < DS_SURPRISE_MIN_DIST ) {
			continue;
		}
		score = ( d > 2400 ? 2400 : d ) + random() * 250;
		if ( d < minDist ) {
			score -= 3000;		// under the preferred distance: only when nothing else is left
		}
		for ( i = 0 ; i < DSA_MAX ; i++ ) {
			if ( dsa.hasSpot[i] && Distance( dsa.spot[i], org ) < 64 ) {
				score -= DSA_REUSE_PENALTY;
			}
		}
		if ( h && h->client ) {
			VectorCopy( h->client->ps.origin, eye );
			eye[2] += h->client->ps.viewheight;
			org[2] += 30;
			trap_Trace( &tr, eye, NULL, NULL, org, h->s.number, MASK_SOLID );
			org[2] -= 30;
			if ( tr.fraction < 1.0f ) {
				score += 500;		// out of the player's sight
			}
		}
		if ( score > bestScore ) {
			bestScore = score;
			best = spot;
		}
	}
	if ( !best ) {
		return qfalse;
	}
	VectorCopy( best->s.origin, out );
	out[2] += 9;
	if ( yaw ) {
		*yaw = best->s.angles[YAW];
	}
	return qtrue;
}

// the landing pillar (drawn by cg_ds_surprise.c)
static gentity_t *DSA_Pillar( const vec3_t spot, int land ) {
	gentity_t	*ent = G_Spawn();

	ent->classname = "ds_arrival";
	ent->s.eType = ET_GENERAL;
	ent->s.generic1 = RM_ENT_SURPRISE;
	ent->s.modelindex2 = DSX2_PILLAR;
	ent->s.frame = DSXS_ARRIVAL;
	ent->s.time = level.time;
	ent->s.time2 = land;
	ent->r.svFlags |= SVF_BROADCAST;
	G_SetOrigin( ent, (float *)spot );
	ent->think = G_FreeEntity;
	ent->nextthink = land + 900;
	trap_LinkEntity( ent );
	return ent;
}

/*
=================
G_DS_ArriveFrame

G_RL_RunFrame: the next late arrival's warning, then its landing
=================
*/
void G_DS_ArriveFrame( void ) {
	const char	*nm;
	int			i, p, role, warn;

	if ( !dsa.on || !dsa.t0 ) {
		return;
	}
	i = trap_Milliseconds();
	if ( dsa.watchUntil ) {
		if ( dsa.lastReal && i - dsa.lastReal > dsa.maxFrame ) {
			dsa.maxFrame = i - dsa.lastReal;
		}
		if ( level.time >= dsa.watchUntil ) {
			G_LogPrintf( "DSArrive: longest real gap between game frames around the join: %i ms (a frame is %i)\n",
				dsa.maxFrame, level.time - level.previousTime );
			dsa.watchUntil = 0;
		}
	}
	dsa.lastReal = i;
	if ( level.intermissiontime || level.intermissionQueued ) {
		if ( dsa.pos < dsa.num ) {
			G_LogPrintf( "DSArrive: the floor ended, %i never came\n", dsa.num - dsa.pos );
			dsa.pos = dsa.num;
		}
		return;
	}
	// a bot that never joined (the sequence goes on without it)
	for ( i = 0 ; i < dsa.num ; i++ ) {
		p = dsa.order[i];
		if ( dsa.added[p] && !dsa.inAt[p] && level.time > dsa.added[p] + DSA_JOIN_TIMEOUT ) {
			nm = G_RL_PlanName( p, &role );
			G_LogPrintf( "DSArrive: %s never joined\n", nm ? nm : "?" );
			dsa.inAt[p] = -1;
			dsa.left--;
			DSA_Publish();
		}
	}
	if ( dsa.pos >= dsa.num ) {
		return;
	}
	p = dsa.order[dsa.pos];
	warn = dsa.boss[p] ? DSA_WARN_BOSS : DSA_WARN;
	nm = G_RL_PlanName( p, &role );
	if ( !dsa.warnAt[p] ) {
		if ( level.time < dsa.nextLand - warn ) {
			return;
		}
		dsa.warnAt[p] = level.time;
		dsa.landAt[p] = level.time + warn;
		dsa.hasSpot[p] = DSA_FarSpot( dsa.boss[p] ? DSA_BOSS_DIST : DSA_PREFER_DIST, ENTITYNUM_NONE, dsa.spot[p], &dsa.yaw[p] );
		if ( dsa.hasSpot[p] ) {
			dsa.pillar[p] = DSA_Pillar( dsa.spot[p], dsa.landAt[p] );
		}
		DSA_Publish();
		G_LogPrintf( "DSArrive: warn %s (plan %i, position %i) at %i ms, lands at %i ms, spot %s %.0f from the player\n",
			nm ? nm : "?", p, dsa.pos, level.time - level.startTime, dsa.landAt[p] - level.startTime,
			dsa.hasSpot[p] ? "found" : "NONE", dsa.hasSpot[p] ? DSA_PlayerDist( dsa.spot[p] ) : 0 );
		return;
	}
	if ( !dsa.added[p] && level.time >= dsa.landAt[p] - DSA_JOIN_LEAD ) {
		dsa.added[p] = level.time;
		dsa.maxFrame = 0;
		dsa.watchUntil = level.time + 4000;		// (the join extends it)
		G_RL_AddPlanBot( p );
		dsa.pos++;
		if ( dsa.pos < dsa.num ) {
			dsa.nextLand = dsa.landAt[p] + dsa.gap[dsa.pos];
		}
	}
}

/*
=================
G_DS_ArriveSpawn

G_RL_SpawnLoadout, last: a planned bot's first spawn lands on its arrival spot (re-picked when
the player came within 600), facing the player; a late joiner gets its head start
=================
*/
void G_DS_ArriveSpawn( gentity_t *ent ) {
	char		name[MAX_NETNAME];
	const char	*nm;
	gclient_t	*cl;
	gentity_t	*h, *e;
	vec3_t		ang, dd, spot;
	float		yaw = 0, dist;
	int			i, p = -1, pos = -1, role, low, n, best;
	qboolean	have, late, repicked = qfalse;

	if ( !dsa.on || !ent || !ent->client || !( ent->r.svFlags & SVF_BOT ) ) {
		return;
	}
	cl = ent->client;
	Q_strncpyz( name, cl->pers.netname, sizeof( name ) );
	Q_CleanStr( name );
	for ( i = 0 ; i < dsa.num ; i++ ) {
		nm = G_RL_PlanName( dsa.order[i], &role );
		if ( nm && !Q_stricmp( nm, name ) ) {
			p = dsa.order[i];
			pos = i;
			break;
		}
	}
	if ( p < 0 || dsa.inAt[p] > 0 ) {
		return;		// not a planned bot, or a respawn
	}
	if ( dsa.inAt[p] < 0 ) {
		dsa.left++;		// it came after all (counted as lost before)
	}
	late = pos >= dsa.numStart;
	have = dsa.hasSpot[p];
	if ( have ) {
		VectorCopy( dsa.spot[p], spot );
		yaw = dsa.yaw[p];
	}
	if ( !have || DSA_PlayerDist( spot ) < DS_SURPRISE_MIN_DIST ) {
		repicked = have;
		have = DSA_FarSpot( dsa.boss[p] ? DSA_BOSS_DIST : DSA_PREFER_DIST, ent - g_entities, spot, &yaw );
	}
	if ( have ) {
		VectorCopy( spot, cl->ps.origin );
		VectorClear( cl->ps.velocity );
		VectorCopy( spot, dsa.spot[p] );		// (later arrivals prefer other spots)
		dsa.hasSpot[p] = qtrue;
	} else {
		VectorCopy( cl->ps.origin, spot );		// the map's own pick
	}
	VectorSet( ang, 0, yaw, 0 );
	h = DSA_Human();
	if ( h && h->client ) {
		VectorSubtract( h->r.currentOrigin, spot, dd );
		vectoangles( dd, ang );
		ang[PITCH] = ang[ROLL] = 0;
	}
	SetClientViewAngle( ent, ang );
	dist = DSA_PlayerDist( spot );
	// the frag race: a late joiner starts with the lowest score of the floor's bots already in
	low = 0;
	if ( late && !G_DS_IsBossFloor() ) {
		low = 9999;
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			e = &g_entities[i];
			if ( e == ent || !e->inuse || !e->client || e->client->pers.connected != CON_CONNECTED || !( e->r.svFlags & SVF_BOT )
				|| e->client->sess.sessionTeam == TEAM_SPECTATOR || G_DS_SurpriseExtra( i ) ) {
				continue;
			}
			if ( e->client->ps.persistant[PERS_SCORE] < low ) {
				low = e->client->ps.persistant[PERS_SCORE];
			}
		}
		if ( low == 9999 || low < 0 ) {
			low = 0;
		}
		if ( g_fraglimit.integer > 0 && low > g_fraglimit.integer - 3 ) {
			low = g_fraglimit.integer - 3 > 0 ? g_fraglimit.integer - 3 : 0;
		}
		cl->ps.persistant[PERS_SCORE] = low;
	}
	dsa.head[p] = low;
	dsa.inAt[p] = level.time;
	if ( late && dsa.landAt[p] ) {
		// the landing is now: the pillar's flash and the cgame's portrait flash follow the real spawn
		if ( dsa.pillar[p] && dsa.pillar[p]->inuse && !Q_stricmp( dsa.pillar[p]->classname, "ds_arrival" ) ) {
			dsa.pillar[p]->s.time2 = level.time;
			dsa.pillar[p]->nextthink = level.time + 900;
			if ( repicked ) {
				G_SetOrigin( dsa.pillar[p], spot );		// (the flash where it really lands)
				trap_LinkEntity( dsa.pillar[p] );
			}
		}
		G_LogPrintf( "DSArrive: %s lands %i ms after the planned landing\n", name, level.time - dsa.landAt[p] );
		dsa.landAt[p] = level.time;
	}
	if ( !dsa.added[p] ) {
		dsa.added[p] = level.time;
	}
	dsa.left--;
	dsa.lastAt = level.time;
	dsa.watchUntil = level.time + 1500;
	DSA_Publish();
	best = 0;
	n = G_RL_HumanClient();
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( i != n && i != ent - g_entities && level.clients[i].pers.connected == CON_CONNECTED && !G_DS_SurpriseExtra( i )
			&& level.clients[i].ps.persistant[PERS_SCORE] > best ) {
			best = level.clients[i].ps.persistant[PERS_SCORE];
		}
	}
	G_LogPrintf( "DSArrive: %s (plan %i, position %i%s) in at %i ms, %.0f from the player%s%s, head start %i (player %i, best bot %i), %i to come\n",
		name, p, pos, dsa.boss[p] ? ", the boss" : late ? "" : ", start", level.time - level.startTime, dist,
		repicked ? " (re-picked: the player came near)" : "", have ? "" : " (map spot)", low,
		n >= 0 ? level.clients[n].ps.persistant[PERS_SCORE] : 0, best, dsa.left );
	if ( dist < DS_SURPRISE_MIN_DIST && h ) {
		G_LogPrintf( "DSArrive: NEAR %s %.0f from the player\n", name, dist );
	}
}
