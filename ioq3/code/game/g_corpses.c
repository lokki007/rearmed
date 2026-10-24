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
// g_corpses.c -- rearmed: corpses that stay until the map ends (cg_gorePersist)
//
// Stock Q3 keeps 8 bodies that sink after 5 seconds. With cg_gorePersist 1 the
// body queue grows on demand up to cg_gorePersistMax corpses (8-64, entity
// slots permitting) and a corpse at rest stops thinking for good: it can still
// be shot, gibbed and kicked (g_physics.c wakes it up). When the cap is reached
// the best victim goes: a corpse no human can see (instant, nobody notices),
// else the oldest far one, which sinks quickly while a spare slot takes the new
// body. Gibbed corpses free their slot.
//
// Snapshots carry at most 256 entities. Every half second the manager estimates
// how many entities the player's PVS holds; above CORPSE_SNAP_HIGH the farthest
// old corpses are hidden from snapshots (SVF_NOCLIENT) until there is room.
//
// cg_gorePersist 0: the stock 8-body ring and sink.
// Dev: gorestats, gorefill <deaths> [radius] (bots die around player 0).

#include "g_local.h"

void BodySink( gentity_t *ent );		// g_client.c

#define CORPSE_TABLE		72			// cap + spares used while visible victims sink
#define CORPSE_CAP_MAX		64
#define CORPSE_CAP_MIN		8
#define CORPSE_STOCK		BODY_QUEUE_SIZE
#define CORPSE_SINK_MSEC	1400		// the quick sink of a recycled corpse
#define CORPSE_SINK_DEPTH	36
#define CORPSE_ENT_RESERVE	256			// entity slots always left for everything else
#define CORPSE_MANAGER_MSEC	500
#define CORPSE_SNAP_HIGH	200			// estimated snapshot entities: hide far corpses above this
#define CORPSE_SNAP_LOW		176			// ... and show them again below this
#define CORPSE_FRESH_MSEC	15000		// never hidden this soon after death
#define CORPSE_VIEW_RANGE	3000
#define CORPSE_VIEW_DOT		0.1f		// wide: the player plays at 32:9 with hor+ FOV

typedef struct {
	gentity_t	*ent;
	int			birth;			// level.time it became a corpse, 0 = free slot
	int			sinkStart;		// > 0: quick sink running (recycled)
	qboolean	hidden;			// SVF_NOCLIENT set by the snapshot guard
} corpse_t;

static corpse_t		corpses[CORPSE_TABLE];
static int			numCorpses;
static int			stockNext;
static qboolean		wasPersist;

static vmCvar_t		cv_persist;
static vmCvar_t		cv_max;
static vmCvar_t		cv_snapHigh;		// g_corpseSnapHigh (dev): the snapshot guard threshold
static qboolean		cvRegistered;

// gorefill (dev)
static int			fillLeft, fillNext, fillRadius, fillBot;
static int			fillRespawn[MAX_CLIENTS];
static int			lastSnapEstimate;
static int			recycledUnseen, recycledSunk, recycledInstant;	// gorestats

static void G_CorpseManagerThink( gentity_t *self );

static void G_CorpseCvars( void ) {
	if ( !cvRegistered ) {
		// the client's switch: on a listen server it is the same cvar
		trap_Cvar_Register( &cv_persist, "cg_gorePersist", "1", CVAR_ARCHIVE );
		trap_Cvar_Register( &cv_max, "cg_gorePersistMax", "64", CVAR_ARCHIVE );
		trap_Cvar_Register( &cv_snapHigh, "g_corpseSnapHigh", "200", 0 );
		cvRegistered = qtrue;
	} else {
		trap_Cvar_Update( &cv_persist );
		trap_Cvar_Update( &cv_max );
		trap_Cvar_Update( &cv_snapHigh );
	}
}

qboolean G_CorpsePersist( void ) {
	G_CorpseCvars();
	return cv_persist.integer != 0;
}

static int G_CorpseCap( void ) {
	int		cap = cv_max.integer;

	if ( cap < CORPSE_CAP_MIN ) {
		cap = CORPSE_CAP_MIN;
	} else if ( cap > CORPSE_CAP_MAX ) {
		cap = CORPSE_CAP_MAX;
	}
	return cap;
}

static corpse_t *G_CorpseOf( gentity_t *ent ) {
	int		i;

	for ( i = 0 ; i < numCorpses ; i++ ) {
		if ( corpses[i].ent == ent ) {
			return &corpses[i];
		}
	}
	return NULL;
}

/*
=============
G_CorpsesInit

InitBodyQue: the stock 8 bodies become the first table entries; the manager
entity runs the cap, the snapshot guard and gorefill.
=============
*/
void G_CorpsesInit( void ) {
	gentity_t	*mgr;
	int			i;

	G_CorpseCvars();
	memset( corpses, 0, sizeof( corpses ) );
	memset( fillRespawn, 0, sizeof( fillRespawn ) );
	numCorpses = 0;
	stockNext = 0;
	fillLeft = 0;
	lastSnapEstimate = 0;
	recycledUnseen = recycledSunk = recycledInstant = 0;
	wasPersist = ( cv_persist.integer != 0 );
	for ( i = 0 ; i < BODY_QUEUE_SIZE ; i++ ) {
		corpses[numCorpses++].ent = level.bodyQue[i];
	}

	mgr = G_Spawn();
	mgr->classname = "corpse_manager";
	mgr->r.svFlags = SVF_NOCLIENT;
	mgr->think = G_CorpseManagerThink;
	mgr->nextthink = level.time + CORPSE_MANAGER_MSEC;
}

/*
=============
G_CorpseRefresh

Slots whose body went away (gibbed: ET_INVISIBLE, fell into a nodrop and got
unlinked, sank) are free again.
=============
*/
static void G_CorpseRefresh( void ) {
	int			i;
	corpse_t	*c;

	for ( i = 0, c = corpses ; i < numCorpses ; i++, c++ ) {
		if ( !c->birth ) {
			continue;
		}
		if ( !c->ent->r.linked || c->ent->s.eType != ET_PLAYER ) {
			trap_UnlinkEntity( c->ent );
			c->birth = 0;
			c->sinkStart = 0;
			c->hidden = qfalse;
		}
	}
}

/*
=============
G_CorpseSeen

0: no human has it in the PVS, 1: in a human's PVS but outside the view,
2: in view (or close).
=============
*/
static int G_CorpseSeen( gentity_t *body, float *nearest ) {
	int			i, seen = 0;
	gclient_t	*cl;
	vec3_t		eye, fwd, delta;
	float		d;

	*nearest = 99999;
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		cl = &level.clients[i];
		if ( cl->pers.connected != CON_CONNECTED || ( g_entities[i].r.svFlags & SVF_BOT ) ) {
			continue;
		}
		VectorCopy( cl->ps.origin, eye );
		eye[2] += cl->ps.viewheight;
		VectorSubtract( body->r.currentOrigin, eye, delta );
		d = VectorNormalize( delta );
		if ( d < *nearest ) {
			*nearest = d;
		}
		if ( !trap_InPVS( eye, body->r.currentOrigin ) ) {
			continue;
		}
		if ( seen < 1 ) {
			seen = 1;
		}
		AngleVectors( cl->ps.viewangles, fwd, NULL, NULL );
		if ( d < 160 || ( d < CORPSE_VIEW_RANGE && DotProduct( delta, fwd ) > CORPSE_VIEW_DOT ) ) {
			return 2;
		}
	}
	return seen;
}

/*
=============
G_CorpseVictim

The corpse to give up: unseen first, then the oldest and farthest.
=============
*/
static corpse_t *G_CorpseVictim( int *seenOut ) {
	int			i, seen, bestSeen = 0;
	float		score, best = -1, nearest;
	corpse_t	*c, *pick = NULL;

	for ( i = 0, c = corpses ; i < numCorpses ; i++, c++ ) {
		if ( !c->birth || c->sinkStart ) {
			continue;
		}
		seen = G_CorpseSeen( c->ent, &nearest );
		// unseen beats everything; 1000 units of distance count like 20 s of age
		score = ( 2 - seen ) * 1.0e8f + ( level.time - c->birth ) + ( nearest > 4000 ? 4000 : nearest ) * 20.0f;
		if ( score > best ) {
			best = score;
			pick = c;
			bestSeen = seen;
		}
	}
	*seenOut = bestSeen;
	return pick;
}

static void G_CorpseSinkDone( gentity_t *ent ) {
	corpse_t	*c = G_CorpseOf( ent );

	trap_UnlinkEntity( ent );
	ent->physicsObject = qfalse;
	if ( c ) {
		c->birth = 0;
		c->sinkStart = 0;
		c->hidden = qfalse;
	}
}

/*
=============
G_CorpseStartSink

A quick sink (the client interpolates the linear move). The body stops
blocking shots at once.
=============
*/
static void G_CorpseStartSink( corpse_t *c ) {
	gentity_t	*ent = c->ent;

	c->sinkStart = level.time;
	ent->physicsObject = qfalse;
	ent->takedamage = qfalse;
	ent->r.contents = 0;
	VectorCopy( ent->r.currentOrigin, ent->s.pos.trBase );
	VectorSet( ent->s.pos.trDelta, 0, 0, -CORPSE_SINK_DEPTH * 1000.0f / CORPSE_SINK_MSEC );
	ent->s.pos.trType = TR_LINEAR;
	ent->s.pos.trTime = level.time;
	ent->think = G_CorpseSinkDone;
	ent->nextthink = level.time + CORPSE_SINK_MSEC;
	trap_LinkEntity( ent );
}

static qboolean G_CorpseRoom( void ) {
	int		i, used = 0;

	for ( i = 0 ; i < level.num_entities ; i++ ) {
		if ( g_entities[i].inuse ) {
			used++;
		}
	}
	return used < MAX_GENTITIES - CORPSE_ENT_RESERVE;
}

static corpse_t *G_CorpseFreeSlot( qboolean grow ) {
	int			i;
	gentity_t	*ent;

	for ( i = 0 ; i < numCorpses ; i++ ) {
		if ( !corpses[i].birth ) {
			return &corpses[i];
		}
	}
	if ( !grow || numCorpses >= CORPSE_TABLE || !G_CorpseRoom() ) {
		return NULL;
	}
	ent = G_Spawn();
	ent->classname = "bodyque";
	ent->neverFree = qtrue;
	corpses[numCorpses].ent = ent;
	corpses[numCorpses].birth = 0;
	return &corpses[numCorpses++];
}

static gentity_t *G_CorpseClaim( corpse_t *c, gentity_t *from ) {
	trap_UnlinkEntity( c->ent );
	c->birth = level.time;
	c->sinkStart = 0;
	c->hidden = qfalse;
	c->ent->health = from->health > GIB_HEALTH ? from->health : GIB_HEALTH + 1;
	return c->ent;
}

/*
=============
G_CorpseSlot

CopyToBodyQue: the entity to turn into a corpse, NULL for no corpse.
=============
*/
gentity_t *G_CorpseSlot( gentity_t *ent ) {
	corpse_t	*c, *v;
	int			i, active, seen;

	G_CorpseCvars();
	if ( !cv_persist.integer ) {
		// stock: cycle the first 8
		c = &corpses[stockNext];
		stockNext = ( stockNext + 1 ) % CORPSE_STOCK;
		return G_CorpseClaim( c, ent );
	}

	// a gibbed player leaves nothing (stock leaves an invisible body that still stops shots)
	if ( ent->s.eType != ET_PLAYER ) {
		return NULL;
	}

	G_CorpseRefresh();
	active = 0;
	for ( i = 0 ; i < numCorpses ; i++ ) {
		if ( corpses[i].birth && !corpses[i].sinkStart ) {
			active++;
		}
	}
	if ( active < G_CorpseCap() ) {
		c = G_CorpseFreeSlot( qtrue );
		if ( c ) {
			return G_CorpseClaim( c, ent );
		}
	}

	v = G_CorpseVictim( &seen );
	if ( !v ) {
		// everything is sinking: take the one that started first
		for ( i = 0 ; i < numCorpses ; i++ ) {
			if ( !v || corpses[i].sinkStart < v->sinkStart ) {
				v = &corpses[i];
			}
		}
		return G_CorpseClaim( v, ent );
	}
	if ( !seen ) {
		recycledUnseen++;
		return G_CorpseClaim( v, ent );		// nobody can see it go
	}
	// in sight: let it sink, a spare slot takes the new body
	G_CorpseStartSink( v );
	c = G_CorpseFreeSlot( qtrue );
	if ( c ) {
		recycledSunk++;
		return G_CorpseClaim( c, ent );
	}
	recycledInstant++;
	return G_CorpseClaim( v, ent );
}

/*
=============
G_CorpseHold

BodySink: with cg_gorePersist a resting corpse stops thinking until the map ends.
=============
*/
qboolean G_CorpseHold( gentity_t *ent ) {
	corpse_t	*c;

	if ( !G_CorpsePersist() ) {
		return qfalse;
	}
	c = G_CorpseOf( ent );
	if ( !c || c->sinkStart ) {
		return qfalse;
	}
	ent->nextthink = 0;
	return qtrue;
}

/*
=============
G_CorpseKickable

g_physics.c: may this corpse still be pushed around?
=============
*/
qboolean G_CorpseKickable( gentity_t *ent ) {
	corpse_t	*c;

	if ( level.time - ent->timestamp < 5000 ) {
		return qtrue;
	}
	if ( !G_CorpsePersist() ) {
		return qfalse;
	}
	c = G_CorpseOf( ent );
	return c && c->birth && !c->sinkStart;
}

/*
=============
G_CorpseSnapshotGuard

Estimate the snapshot of the first human (entities in the PVS + broadcast
ones); hide the farthest old corpses when it gets close to 256.
=============
*/
static void G_CorpseSnapshotGuard( void ) {
	int			i, count, hide, n, high, low;
	gentity_t	*e, *player = NULL;
	vec3_t		eye;
	corpse_t	*c, *pick;
	float		d, bestD;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( level.clients[i].pers.connected == CON_CONNECTED && !( g_entities[i].r.svFlags & SVF_BOT ) ) {
			player = &g_entities[i];
			break;
		}
	}
	if ( !player ) {
		return;
	}
	VectorCopy( player->client->ps.origin, eye );
	eye[2] += player->client->ps.viewheight;

	count = 0;
	for ( i = 0, e = g_entities ; i < level.num_entities ; i++, e++ ) {
		if ( !e->inuse || !e->r.linked ) {
			continue;
		}
		if ( e->r.svFlags & SVF_NOCLIENT ) {
			continue;
		}
		if ( ( e->r.svFlags & SVF_BROADCAST ) || trap_InPVS( eye, e->r.currentOrigin ) ) {
			count++;
		}
	}
	lastSnapEstimate = count;

	high = cv_snapHigh.integer > 32 ? cv_snapHigh.integer : CORPSE_SNAP_HIGH;
	low = high - ( CORPSE_SNAP_HIGH - CORPSE_SNAP_LOW );
	if ( count > high ) {
		hide = count - high;
		for ( n = 0 ; n < hide ; n++ ) {
			pick = NULL;
			bestD = 0;
			for ( i = 0, c = corpses ; i < numCorpses ; i++, c++ ) {
				if ( !c->birth || c->sinkStart || c->hidden || level.time - c->birth < CORPSE_FRESH_MSEC
					|| !c->ent->r.linked ) {
					continue;
				}
				d = Distance( eye, c->ent->r.currentOrigin );
				if ( d > bestD ) {
					bestD = d;
					pick = c;
				}
			}
			if ( !pick ) {
				break;
			}
			pick->hidden = qtrue;
			pick->ent->r.svFlags |= SVF_NOCLIENT;
		}
	} else if ( count < low ) {
		n = low - count;
		while ( n-- > 0 ) {
			pick = NULL;
			bestD = 1.0e9f;
			for ( i = 0, c = corpses ; i < numCorpses ; i++, c++ ) {
				if ( !c->birth || !c->hidden ) {
					continue;
				}
				d = Distance( eye, c->ent->r.currentOrigin );
				if ( d < bestD ) {
					bestD = d;
					pick = c;
				}
			}
			if ( !pick ) {
				break;
			}
			pick->hidden = qfalse;
			pick->ent->r.svFlags &= ~SVF_NOCLIENT;
		}
	}
}

/*
=============
G_CorpseFill

gorefill: one bot dies near player 0 every 150 ms (two wounds, then a normal,
gib or charring death) and respawns 1.7 s later, leaving its corpse.
=============
*/
static void G_CorpseFill( void ) {
	int			i, tries, r;
	gentity_t	*bot, *player = &g_entities[0];
	vec3_t		spot, end, dir, point, mins = { -15, -15, -24 }, maxs = { 15, 15, 32 };
	trace_t		tr;
	float		a, dist;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		bot = &g_entities[i];
		if ( fillRespawn[i] && level.time >= fillRespawn[i] ) {
			fillRespawn[i] = 0;
			if ( bot->inuse && bot->client && bot->client->ps.pm_type == PM_DEAD ) {
				ClientRespawn( bot );
			}
		}
	}
	if ( fillLeft <= 0 || level.time < fillNext || !player->inuse || !player->client ) {
		return;
	}
	fillNext = level.time + 150;

	for ( tries = 0 ; tries < level.maxclients ; tries++ ) {
		fillBot = ( fillBot + 1 ) % level.maxclients;
		bot = &g_entities[fillBot];
		if ( bot->inuse && bot->client && ( bot->r.svFlags & SVF_BOT ) && bot->health > 0
			&& bot->client->sess.sessionTeam != TEAM_SPECTATOR ) {
			break;
		}
	}
	if ( tries == level.maxclients ) {
		return;
	}

	// a free spot on the floor around the player
	for ( tries = 0 ; tries < 12 ; tries++ ) {
		a = random() * 2 * M_PI;
		dist = 80 + random() * fillRadius;
		VectorCopy( player->client->ps.origin, spot );
		spot[0] += cos( a ) * dist;
		spot[1] += sin( a ) * dist;
		spot[2] += 40;
		trap_Trace( &tr, player->client->ps.origin, mins, maxs, spot, player->s.number, MASK_PLAYERSOLID );
		VectorCopy( tr.endpos, spot );
		VectorCopy( spot, end );
		end[2] -= 400;
		trap_Trace( &tr, spot, mins, maxs, end, player->s.number, MASK_PLAYERSOLID );
		if ( tr.startsolid || tr.allsolid || tr.fraction >= 1.0f ) {
			continue;
		}
		if ( trap_PointContents( tr.endpos, -1 ) & ( CONTENTS_NODROP | CONTENTS_LAVA | CONTENTS_SLIME ) ) {
			continue;
		}
		VectorCopy( tr.endpos, spot );
		break;
	}
	if ( tries == 12 ) {
		return;
	}

	VectorCopy( spot, bot->client->ps.origin );
	VectorClear( bot->client->ps.velocity );
	bot->client->ps.eFlags ^= EF_TELEPORT_BIT;
	BG_PlayerStateToEntityState( &bot->client->ps, &bot->s, qtrue );
	VectorCopy( spot, bot->r.currentOrigin );
	trap_LinkEntity( bot );

	VectorSubtract( spot, player->client->ps.origin, dir );
	dir[2] = 0;
	VectorNormalize( dir );
	VectorCopy( spot, point );
	point[2] += 10;
	G_Damage( bot, NULL, NULL, dir, point, 20, 0, MOD_MACHINEGUN );
	point[2] += 12;
	G_Damage( bot, NULL, NULL, dir, point, 20, 0, MOD_SHOTGUN );
	r = rand() % 10;
	if ( bot->health > 0 ) {
		if ( r < 6 ) {
			G_Damage( bot, NULL, NULL, dir, point, bot->health + 15, 0, MOD_MACHINEGUN );
		} else if ( r < 9 ) {
			G_Damage( bot, NULL, NULL, dir, point, bot->health + 140, DAMAGE_RADIUS, MOD_ROCKET_SPLASH );
		} else {
			G_Damage( bot, NULL, NULL, dir, point, bot->health + 10, 0, MOD_LIGHTNING );
		}
	}
	fillRespawn[fillBot] = level.time + 1700;
	fillLeft--;
	if ( !fillLeft ) {
		G_Printf( "gorefill: done\n" );
	}
}

static void G_CorpseManagerThink( gentity_t *self ) {
	int			i, active, seen;
	qboolean	persist;
	corpse_t	*c;

	self->nextthink = level.time + ( fillLeft > 0 ? 50 : CORPSE_MANAGER_MSEC );
	for ( i = 0 ; i < MAX_CLIENTS ; i++ ) {
		if ( fillRespawn[i] ) {
			self->nextthink = level.time + 50;
			break;
		}
	}
	G_CorpseFill();

	persist = G_CorpsePersist();
	G_CorpseRefresh();
	if ( !persist ) {
		if ( wasPersist ) {
			// switched off: every corpse sinks like a stock one
			for ( i = 0, c = corpses ; i < numCorpses ; i++, c++ ) {
				if ( c->hidden ) {
					c->hidden = qfalse;
					c->ent->r.svFlags &= ~SVF_NOCLIENT;
				}
				if ( c->birth && !c->sinkStart && c->ent->think != BodySink ) {
					c->ent->timestamp = level.time - 5000;
					c->ent->think = BodySink;
					c->ent->nextthink = level.time + 100 + ( i % 8 ) * 50;
				} else if ( c->birth && !c->sinkStart && c->ent->nextthink == 0 ) {
					c->ent->timestamp = level.time - 5000;
					c->ent->nextthink = level.time + 100;
				}
			}
		}
		wasPersist = qfalse;
		return;
	}
	wasPersist = qtrue;

	// the cap went down: one extra corpse goes per tick
	active = 0;
	for ( i = 0 ; i < numCorpses ; i++ ) {
		if ( corpses[i].birth && !corpses[i].sinkStart ) {
			active++;
		}
	}
	if ( active > G_CorpseCap() ) {
		c = G_CorpseVictim( &seen );
		if ( c ) {
			if ( seen ) {
				G_CorpseStartSink( c );
			} else {
				G_CorpseSinkDone( c->ent );
			}
		}
	}

	G_CorpseSnapshotGuard();
}

/*
=============
G_CorpseLook

gorelook [n]: puts client 0 where it sees the n-th densest pile of corpses
(0 = the densest), 260 units away, looking down at it.
=============
*/
static void G_CorpseLook( void ) {
	char		buf[MAX_TOKEN_CHARS];
	int			i, j, k, rank, count[CORPSE_TABLE], best, bestCount;
	byte		used[CORPSE_TABLE];
	gentity_t	*player = &g_entities[0];
	vec3_t		center, from, org, angles, delta;
	trace_t		tr;
	float		a, dist;
	qboolean	found;

	if ( !player->inuse || !player->client ) {
		return;
	}
	rank = 0;
	if ( trap_Argc() > 1 ) {
		trap_Argv( 1, buf, sizeof( buf ) );
		rank = atoi( buf );
	}
	G_CorpseRefresh();
	for ( i = 0 ; i < numCorpses ; i++ ) {
		count[i] = 0;
		used[i] = 0;
		if ( !corpses[i].birth ) {
			continue;
		}
		for ( j = 0 ; j < numCorpses ; j++ ) {
			if ( corpses[j].birth && Distance( corpses[i].ent->r.currentOrigin, corpses[j].ent->r.currentOrigin ) < 300 ) {
				count[i]++;
			}
		}
	}
	best = -1;
	for ( k = 0 ; k <= rank ; k++ ) {
		best = -1;
		bestCount = 0;
		for ( i = 0 ; i < numCorpses ; i++ ) {
			if ( !used[i] && count[i] > bestCount ) {
				bestCount = count[i];
				best = i;
			}
		}
		if ( best < 0 ) {
			G_Printf( "gorelook: no corpses\n" );
			return;
		}
		// the next pile must be somewhere else
		for ( i = 0 ; i < numCorpses ; i++ ) {
			if ( corpses[i].birth && Distance( corpses[i].ent->r.currentOrigin, corpses[best].ent->r.currentOrigin ) < 400 ) {
				used[i] = 1;
			}
		}
	}
	VectorCopy( corpses[best].ent->r.currentOrigin, center );
	// the first spot with room for the player and a clear line to the pile
	found = qfalse;
	for ( k = 0 ; k < 24 && !found ; k++ ) {
		a = ( k % 8 ) * M_PI / 4;
		dist = k < 8 ? 170 : k < 16 ? 240 : 110;
		VectorSet( from, center[0] + cos( a ) * dist, center[1] + sin( a ) * dist, center[2] + 120 );
		trap_Trace( &tr, center, NULL, NULL, from, ENTITYNUM_NONE, MASK_SOLID );
		if ( tr.fraction < 0.98f ) {
			continue;
		}
		VectorCopy( from, org );
		org[2] -= player->client->ps.viewheight;
		trap_Trace( &tr, org, player->r.mins, player->r.maxs, org, player->s.number, MASK_PLAYERSOLID );
		if ( !tr.startsolid && !tr.allsolid ) {
			found = qtrue;
		}
	}
	if ( !found ) {
		G_Printf( "gorelook: no clear spot near the pile at %.0f %.0f %.0f\n", center[0], center[1], center[2] );
		return;
	}
	VectorSubtract( center, from, delta );
	vectoangles( delta, angles );
	// view pitch: down is positive (vectoangles gives up as positive)
	angles[PITCH] = atan2( -delta[2], sqrt( delta[0] * delta[0] + delta[1] * delta[1] ) ) * ( 180.0f / M_PI );
	VectorCopy( org, player->client->ps.origin );
	VectorClear( player->client->ps.velocity );
	player->client->ps.eFlags ^= EF_TELEPORT_BIT;
	SetClientViewAngle( player, angles );
	BG_PlayerStateToEntityState( &player->client->ps, &player->s, qtrue );
	VectorCopy( player->client->ps.origin, player->r.currentOrigin );
	trap_LinkEntity( player );
	G_Printf( "gorelook: pile of %i corpses at %.0f %.0f %.0f, eye %.0f %.0f %.0f, pitch %.0f yaw %.0f\n", count[best],
		center[0], center[1], center[2], from[0], from[1], from[2], angles[PITCH], angles[YAW] );
}

/*
=============
G_CorpseCommand

gorestats / gorefill <deaths> [radius]
=============
*/
qboolean G_CorpseCommand( const char *cmd ) {
	char	buf[MAX_TOKEN_CHARS];
	int		i, active = 0, sinking = 0, hidden = 0, used = 0;

	if ( !Q_stricmp( cmd, "gorestats" ) ) {
		G_CorpseRefresh();
		for ( i = 0 ; i < numCorpses ; i++ ) {
			if ( corpses[i].birth ) {
				if ( corpses[i].sinkStart ) {
					sinking++;
				} else {
					active++;
				}
				if ( corpses[i].hidden ) {
					hidden++;
				}
			}
		}
		for ( i = 0 ; i < level.num_entities ; i++ ) {
			if ( g_entities[i].inuse ) {
				used++;
			}
		}
		G_Printf( "gorestats: persist %i cap %i corpses %i sinking %i hidden %i table %i | entities %i used / %i high | snapshot estimate %i | recycled unseen %i sunk %i instant %i\n",
			cv_persist.integer, G_CorpseCap(), active, sinking, hidden, numCorpses, used, level.num_entities, lastSnapEstimate,
			recycledUnseen, recycledSunk, recycledInstant );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "gorefill" ) ) {
		trap_Argv( 1, buf, sizeof( buf ) );
		fillLeft = atoi( buf );
		if ( fillLeft <= 0 ) {
			fillLeft = 32;
		}
		fillRadius = 600;
		if ( trap_Argc() > 2 ) {
			trap_Argv( 2, buf, sizeof( buf ) );
			fillRadius = atoi( buf );
		}
		fillNext = level.time;
		G_Printf( "gorefill: %i deaths within %i units of client 0\n", fillLeft, fillRadius );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "gorelook" ) ) {
		G_CorpseLook();
		return qtrue;
	}
	return qfalse;
}
