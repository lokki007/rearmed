/*
===========================================================================
REARMED -- DESCENT round 3 SECRETS (ds-secrets, game side): secret stashes on the stock maps.

No map is edited. At map load (once the bot library has the AAS) a scan walks every grounded
AAS area and finds hidden floor spots:
  - walkable spots that are out of the way: tucked into corners, under low ceilings (stairs),
    at dead ends, far from items / spawns, seen from few of the places players stand;
  - spots the bots' stock movement can't reach (no AAS route from the spawns), reachable with
    the player's abilities: a flown arc test (jump + double jump, dash, wall run) from a
    walkable launch point, or a zip line / grapple anchor with a clear line and a drop onto
    the spot. Every way that works is kept as an ability mask (SA_*); a stash only appears
    when the player owns one of them.
The results go to rearmed_secrets_<map>.txt (SEC_FILE_VERSION; any change of the AAS area
count recomputes). Each fight / elite / event floor picks 1-3 of them (seeded by the run seed
and the floor), rolls the loot and spawns an old strongbox (RM_ENT_STASH, solid, a faint hum
in the cgame). The human player finds it by walking up to it: it opens, the loot pops out,
a loot beam (ds-frenzy) marks the spot, CS_DSSECRETS drives the "SECRET FOUND 2/3" fanfare,
and DSST_STASHES counts it (meta cnt_stashes; 5 found unlock the DOWSING ROD secret card).
Bots ignore stashes: only the human opens them.

Switch g_dsSecrets (archived, 1). Dev: dssecrets list|show|hide|tp <n> [launch]|scan|pick|spawn,
dsstash <type> [here].
===========================================================================
*/
#include "g_local.h"
#include "../botlib/botlib.h"
#include "../botlib/be_aas.h"

#define SEC_USE_FRENZY		1		// ds-frenzy API (G_DS_BeamAt, G_DS_GrantSpike) is in the tree
#define SEC_USE_CARDS		1		// ds-cards API (G_DS_CardsGrantLegendary) is in the tree

#define SEC_FILE_VERSION	14
#define SEC_MAX_AREAS		16384
#define SEC_MAX_SAMPLES		6144
#define SEC_MAX_SPOTS		24		// kept per map
#define SEC_MAX_STASH		3		// per floor
#define SEC_MAX_VIS			96		// visibility probe points
#define SEC_MAX_ANCHORS		256
#define SEC_MAX_LAUNCH		8

// AAS area contents / flags (botlib/aasfile.h)
#define SEC_AREA_GROUNDED	1
#define SEC_AREA_LIQUID		4
#define SEC_AREA_DISABLED	8
#define SEC_AC_BAD			( 1 | 2 | 4 | 64 | 128 | 256 | 1024 )	// water lava slime teleporter jumppad donotenter mover

// ability masks (a spot keeps up to SEC_ALTS masks: any one of them, fully owned, reaches it)
#define SA_DJ				1
#define SA_DASH				2
#define SA_WALLRUN			4
#define SA_ZIP				8
#define SA_GRAPPLE			16
#define SEC_ALTS			4

// scan phases
#define SS_IDLE				0
#define SS_START			1
#define SS_SAMPLES			2
#define SS_WALK				3
#define SS_FEATURES			4
#define SS_ABILITY			5
#define SS_VIS				6
#define SS_PICK				7
#define SS_DONE				8
#define SS_AREAS			10		// the AAS areas, a tile of the world at a time
#define SS_COLUMNS			9		// floors the AAS has no route to (botclip, narrow beams): column traces

#define SEC_OPEN_DIST		70.0f	// the player this close (and in sight) opens a stash
#define SEC_SIM_DT			0.025f
#define SEC_JUMP			270		// JUMP_VELOCITY (bg_local.h)
#define SEC_STEP			18		// STEPSIZE

typedef struct {
	vec3_t		org;			// floor point
	int			area;
	int			walk;			// travel time from the spawns (0: no stock route)
	int			alts[SEC_ALTS];
	int			numAlts;
	float		score;
	float		vis;
	int			corner, ceil, reach;
	float		yaw;			// walkable: facing away from its wall (-1 none)
	float		itemDist;
	char		kind;
	vec3_t		launch;			// ability spots: where the move starts
	qboolean	ok;
	qboolean	shortlist;
	qboolean	tested;
	qboolean	jumpUp;		// off the AAS routes but a plain jump gets there
	int			tryIdx;		// ability tests: the next launch point (they run over several frames)
} secSample_t;

typedef struct {
	vec3_t		org;
	float		yaw;
	float		score;
	char		kind;
	int			alts[SEC_ALTS];
	int			numAlts;
	vec3_t		launch;
} secSpot_t;

typedef struct {
	gentity_t	*ent;
	int			spot;
	int			type;
	qboolean	open;
} secStash_t;

static vmCvar_t		secCvOn;
static vmCvar_t		secCvDebug;

static int			secPhase;
static int			secCursor;
static int			secScanStart;
static int			secBudget;			// msec of scan per server frame
static int			secSlowest;			// the longest slice (dev: frame hitches)
static qboolean		secForceScan;
static char			secMap[64];
static int			secMapSum;			// sv_mapChecksum: keys the cache
static int			secTile;

static byte			secAreaSeen[SEC_MAX_AREAS];
static int			secAreaWalk[SEC_MAX_AREAS];
static int			secAreaList[SEC_MAX_AREAS];
static int			secNumAreas;
static int			secMaxArea;
static int			secRefArea[3];
static vec3_t		secRefOrg[3];
static int			secNumRefs;
static vec3_t		secBMins, secBMaxs;	// the AAS world's bounds
static int			secColX, secColY;

static secSample_t	secSamples[SEC_MAX_SAMPLES];
static int			secNumSamples;
static vec3_t		secAnchors[SEC_MAX_ANCHORS];	// items, spawns: keep away, visibility probes
static int			secNumAnchors;
static vec3_t		secAvoid[SEC_MAX_ANCHORS];		// spawns, teleport exits, jump pad targets
static vec3_t		secSpawn[SEC_MAX_ANCHORS];		// deathmatch spawns (the stock routes start there)
static int			secNumSpawn;
static float		secVisThr;
static int			secNumAvoid;
static vec3_t		secVis[SEC_MAX_VIS];
static int			secNumVis;

static secSpot_t	secSpots[SEC_MAX_SPOTS];
static int			secNumSpots;
static qboolean		secSpotsReady;

static secStash_t	secStash[SEC_MAX_STASH];
static int			secNumStash;
static int			secFound;
static int			secLastType;
static int			secLastFound;
static qboolean		secPicked;
static int			secHumanSince;
static int			secOwnedMask;
static qboolean		secDowse;
static qboolean		secCsDirty;
static gentity_t	*secShow[SEC_MAX_SPOTS];

static const vec3_t	secPMins = { -15, -15, -24 };
static const vec3_t	secPMaxs = { 15, 15, 32 };
static const vec3_t	secBoxMins = { -14, -14, 0 };
static const vec3_t	secBoxMaxs = { 14, 14, 22 };

#define SEC_MASK	( CONTENTS_SOLID | CONTENTS_PLAYERCLIP )

/*
===============================================================================

HELPERS

===============================================================================
*/

static unsigned secRng;

static int Sec_Rand( void ) {
	secRng = secRng * 1103515245u + 12345u;
	return (int)( ( secRng >> 16 ) & 0x7fff );
}

static float Sec_Frand( void ) {
	return Sec_Rand() / 32768.0f;
}

static float Sec_Dist2D( const vec3_t a, const vec3_t b ) {
	float	dx = a[0] - b[0], dy = a[1] - b[1];

	return (float)sqrt( dx * dx + dy * dy );
}

static gentity_t *Sec_Human( void ) {
	int			i;
	gentity_t	*e;

	i = G_RL_HumanClient();
	if ( i >= 0 ) {
		return &g_entities[i];
	}
	if ( G_RL_Active() ) {
		return NULL;
	}
	// outside a run (dev tests): the first human
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		e = &g_entities[i];
		if ( e->inuse && e->client && e->client->pers.connected == CON_CONNECTED && !( e->r.svFlags & SVF_BOT ) ) {
			return e;
		}
	}
	return NULL;
}

/*
Traces must see the world only: players, bots, props and our own stashes are unlinked while
the scan works (the same frame), so the result never depends on who stood where.
*/
static int		secUnlinked[MAX_GENTITIES];
static int		secNumUnlinked;

static void Sec_UnlinkMovers( void ) {
	int			i;
	gentity_t	*e;

	secNumUnlinked = 0;
	for ( i = 0 ; i < level.num_entities ; i++ ) {
		e = &g_entities[i];
		if ( !e->inuse || !e->r.linked || e->s.eType == ET_MOVER || i == ENTITYNUM_WORLD ) {
			continue;
		}
		if ( !( e->r.contents & ( CONTENTS_SOLID | CONTENTS_BODY | CONTENTS_PLAYERCLIP | CONTENTS_CORPSE ) ) ) {
			continue;
		}
		trap_UnlinkEntity( e );
		secUnlinked[secNumUnlinked++] = i;
	}
}

static void Sec_RelinkMovers( void ) {
	int		i;

	for ( i = 0 ; i < secNumUnlinked ; i++ ) {
		trap_LinkEntity( &g_entities[secUnlinked[i]] );
	}
	secNumUnlinked = 0;
}

static qboolean Sec_NearAvoid( const vec3_t p, float dist ) {
	int		i;

	for ( i = 0 ; i < secNumAvoid ; i++ ) {
		if ( fabs( secAvoid[i][2] - p[2] ) < 96 && Sec_Dist2D( secAvoid[i], p ) < dist ) {
			return qtrue;
		}
	}
	return qfalse;
}

static void Sec_CollectAnchors( void ) {
	gentity_t	*e, *t;
	int			i;

	secNumAnchors = secNumAvoid = secNumSpawn = 0;
	for ( i = MAX_CLIENTS ; i < level.num_entities ; i++ ) {
		e = &g_entities[i];
		if ( !e->inuse || !e->classname ) {
			continue;
		}
		if ( !Q_stricmp( e->classname, "info_player_deathmatch" ) || !Q_stricmp( e->classname, "info_player_start" )
			|| !Q_stricmp( e->classname, "team_CTF_redspawn" ) || !Q_stricmp( e->classname, "team_CTF_bluespawn" )
			|| !Q_stricmp( e->classname, "team_CTF_redplayer" ) || !Q_stricmp( e->classname, "team_CTF_blueplayer" ) ) {
			if ( secNumSpawn < SEC_MAX_ANCHORS ) {
				VectorCopy( e->s.origin, secSpawn[secNumSpawn++] );
			}
			if ( secNumAnchors < SEC_MAX_ANCHORS ) {
				VectorCopy( e->s.origin, secAnchors[secNumAnchors++] );
			}
			if ( secNumAvoid < SEC_MAX_ANCHORS ) {
				VectorCopy( e->s.origin, secAvoid[secNumAvoid++] );
			}
		} else if ( !Q_stricmp( e->classname, "misc_teleporter_dest" ) || !Q_stricmp( e->classname, "target_position" )
			|| !Q_stricmp( e->classname, "info_notnull" ) ) {
			if ( secNumAvoid < SEC_MAX_ANCHORS ) {
				VectorCopy( e->s.origin, secAvoid[secNumAvoid++] );
			}
		} else if ( e->s.eType == ET_PUSH_TRIGGER && e->target ) {
			t = G_PickTarget( e->target );
			if ( t && secNumAvoid < SEC_MAX_ANCHORS ) {
				VectorCopy( t->s.origin, secAvoid[secNumAvoid++] );
			}
		} else if ( e->item && e->s.eType == ET_ITEM && !( e->flags & FL_DROPPED_ITEM ) ) {
			if ( secNumAnchors < SEC_MAX_ANCHORS ) {
				VectorCopy( e->s.origin, secAnchors[secNumAnchors++] );
			}
		}
	}
}

/*
=================
Sec_CheckSpot

A strongbox fits on this floor point: clear box and room to stand by it, floor under every
corner, flat world floor (no mover, no sky), no liquid / hurt / trigger, nothing important near.
=================
*/
static int		secRej[16];		// dev: why samples fail (0-8 Sec_CheckSpot, 10 no floor, 11 other area)

static qboolean Sec_CheckSpot( vec3_t floor ) {
	vec3_t		start, end, mins, maxs;
	trace_t		tr;
	int			i, n, touch[32], c;
	gentity_t	*hit;

	// a flat world floor right under it
	VectorCopy( floor, start );
	start[2] += 8;
	VectorCopy( floor, end );
	end[2] -= 8;
	trap_Trace( &tr, start, NULL, NULL, end, ENTITYNUM_NONE, SEC_MASK );
	if ( tr.fraction < 1.0f && ( tr.surfaceFlags & ( SURF_NODRAW | SURF_NONSOLID ) ) ) {
		// a player clip lying on the floor (q3dm19's platforms): the real floor just under it
		trace_t	tr2;
		vec3_t	s2;

		VectorCopy( tr.endpos, s2 );
		s2[2] += 0.5f;
		trap_Trace( &tr2, s2, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID );
		if ( secCvDebug.integer && secRej[9]++ < 4 ) {
			G_LogPrintf( "Secrets: dbg clip floor at %s: solid frac %.2f at %s flags 0x%x\n", vtos( tr.endpos ), tr2.fraction,
				vtos( tr2.endpos ), tr2.surfaceFlags );
		}
		if ( tr2.fraction < 1.0f && tr.endpos[2] - tr2.endpos[2] < 8 ) {
			tr = tr2;
		}
	}
	if ( secCvDebug.integer ) {
		secRej[12] += tr.startsolid || tr.fraction >= 1.0f;
		secRej[13] += tr.fraction < 1.0f && tr.plane.normal[2] < 0.85f;
		secRej[14] += tr.fraction < 1.0f && tr.entityNum != ENTITYNUM_WORLD;
		secRej[15] += tr.fraction < 1.0f && ( tr.surfaceFlags & ( SURF_SKY | SURF_NOIMPACT | SURF_SLICK ) ) != 0;
		if ( tr.fraction < 1.0f && ( tr.surfaceFlags & ( SURF_SKY | SURF_NOIMPACT | SURF_SLICK ) ) && secRej[15] < 4 ) {
			G_LogPrintf( "Secrets: dbg surface reject at %s flags 0x%x\n", vtos( floor ), tr.surfaceFlags );
		}
	}
	if ( tr.startsolid || tr.fraction >= 1.0f || tr.plane.normal[2] < 0.85f || tr.entityNum != ENTITYNUM_WORLD
		|| ( tr.surfaceFlags & ( SURF_SKY | SURF_NOIMPACT | SURF_SLICK ) ) ) {
		{ secRej[0]++; return qfalse; }
	}
	VectorCopy( floor, start );
	start[2] += 1;
	trap_Trace( &tr, start, secBoxMins, secBoxMaxs, start, ENTITYNUM_NONE, SEC_MASK );
	if ( tr.startsolid || tr.allsolid ) {
		{ secRej[1]++; return qfalse; }
	}
	VectorSet( mins, -14, -14, 0 );
	VectorSet( maxs, 14, 14, 34 );		// crouch room at least (under the stairs)
	trap_Trace( &tr, start, mins, maxs, start, ENTITYNUM_NONE, SEC_MASK );
	if ( tr.startsolid || tr.allsolid ) {
		{ secRej[2]++; return qfalse; }
	}
	for ( i = 0 ; i < 4 ; i++ ) {
		VectorCopy( floor, start );
		start[0] += ( i & 1 ) ? 16 : -16;
		start[1] += ( i & 2 ) ? 16 : -16;
		start[2] += 4;
		VectorCopy( start, end );
		end[2] -= 14;
		trap_Trace( &tr, start, NULL, NULL, end, ENTITYNUM_NONE, SEC_MASK );
		if ( tr.fraction >= 1.0f || tr.startsolid ) {
			{ secRej[3]++; return qfalse; }
		}
	}
	VectorCopy( floor, start );
	start[2] += 2;		// a shallow lava / slime pool hides its floor (q3dm13)
	c = trap_PointContents( start, -1 );
	start[2] += 10;
	c |= trap_PointContents( start, -1 );
	if ( c & ( MASK_WATER | CONTENTS_NODROP | CONTENTS_TRIGGER ) ) {
		{ secRej[4]++; return qfalse; }
	}
	start[2] += 36;
	c = trap_PointContents( start, -1 );
	if ( c & ( MASK_WATER | CONTENTS_NODROP | CONTENTS_TRIGGER ) ) {
		{ secRej[5]++; return qfalse; }
	}
	VectorSet( mins, floor[0] - 56, floor[1] - 56, floor[2] - 8 );
	VectorSet( maxs, floor[0] + 56, floor[1] + 56, floor[2] + 96 );
	n = trap_EntitiesInBox( mins, maxs, touch, 32 );
	for ( i = 0 ; i < n ; i++ ) {
		hit = &g_entities[touch[i]];
		if ( hit->s.eType == ET_MOVER || hit->s.eType == ET_PUSH_TRIGGER || hit->s.eType == ET_TELEPORT_TRIGGER
			|| hit->s.eType == ET_ITEM || ( hit->r.contents & CONTENTS_TRIGGER ) ) {
			{ secRej[6]++; return qfalse; }
		}
	}
	if ( Sec_NearAvoid( floor, 112 ) ) {
		{ secRej[7]++; return qfalse; }
	}
	return qtrue;
}

/*
===============================================================================

THE ARC TEST: fly a jump (with the player's moves) from a launch point toward a spot

===============================================================================
*/

#define SM_DJ		1
#define SM_DASH		2
#define SM_WR		4

static qboolean Sec_Landed( const vec3_t pos, const vec3_t target, int area ) {
	vec3_t	p;

	VectorCopy( pos, p );
	p[2] += 1;
	if ( area > 0 && trap_AAS_PointAreaNum( p ) == area ) {
		return qtrue;
	}
	return Sec_Dist2D( pos, target ) < 40 && fabs( pos[2] - target[2] ) < 20;
}

/*
=================
Sec_PmFly

The real thing: a player state run through Pmove (the same code that moves the player) with
synthesized input. Runs from start (a player origin) toward the target, jumps at the edge /
when blocked / within jumpDist, then the moves: an air jump airDelay msec after the takeoff,
a dash dashDelay msec after it, jump held along walls (wall run, an air jump when it ends),
or a zip line fired at zipAnchor right away. qtrue = it lands on the target's floor.
=================
*/
#define SEC_PM_MSEC		16
#define SM_ZIP			8
#define SM_HOLD			16		// keep jump held after the takeoff (wall runs)

static playerState_t	secPs;
static pmove_t			secPm;

static qboolean Sec_PmFly( const vec3_t start, const vec3_t target, int area, int moves, float jumpDist,
		int airDelay, int dashDelay, const vec3_t zipAnchor ) {
	vec3_t		d, ang, p, e, eye;
	trace_t		tr;
	int			t, takeoff = -1, airAt = -1, dashAt = -1, landT = -1;
	float		hd, hs, yaw, floorZ;
	qboolean	onGround, wasWr = qfalse, zipped = qfalse, airDone = qfalse;

	memset( &secPs, 0, sizeof( secPs ) );
	memset( &secPm, 0, sizeof( secPm ) );
	secPs.pm_type = PM_NORMAL;
	secPs.clientNum = 0;
	VectorCopy( start, secPs.origin );
	secPs.gravity = g_gravity.integer;
	secPs.speed = g_speed.integer;
	secPs.stats[STAT_HEALTH] = secPs.stats[STAT_MAX_HEALTH] = 100;
	secPs.stats[STAT_RMFLAGS] = RMF_JUMPASSIST | ( ( moves & SM_DJ ) ? RMF_DOUBLEJUMP : 0 ) | ( ( moves & SM_DASH ) ? RMF_DASH : 0 )
		| ( ( moves & SM_WR ) ? RMF_WALLRUN : 0 );
	secPs.ammo[AMMO_RMFLAGS2] = ( moves & SM_ZIP ) ? RMF2_ZIPLINE : 0;
	secPs.loopSound = RM_ALTMASK_ALL;
	secPs.groundEntityNum = ENTITYNUM_NONE;
	secPs.viewheight = DEFAULT_VIEWHEIGHT;
	secPm.ps = &secPs;
	secPm.trace = trap_Trace;
	secPm.pointcontents = trap_PointContents;
	secPm.tracemask = MASK_PLAYERSOLID;
	floorZ = ( start[2] < target[2] ? start[2] : target[2] ) - 200;

	for ( t = 0 ; t < 3200 ; t += SEC_PM_MSEC ) {
		memset( &secPm.cmd, 0, sizeof( secPm.cmd ) );
		secPm.cmd.serverTime = t + SEC_PM_MSEC;
		VectorSubtract( target, secPs.origin, d );
		hd = (float)sqrt( d[0] * d[0] + d[1] * d[1] );
		yaw = RAD2DEG( atan2( d[1], d[0] ) );
		onGround = secPs.groundEntityNum != ENTITYNUM_NONE;
		hs = (float)sqrt( secPs.velocity[0] * secPs.velocity[0] + secPs.velocity[1] * secPs.velocity[1] );

		if ( ( moves & SM_ZIP ) && !zipped ) {
			// fire the cable at the anchor from where we stand
			VectorCopy( secPs.origin, eye );
			eye[2] += secPs.viewheight;
			VectorSubtract( zipAnchor, eye, e );
			vectoangles( e, ang );
			secPm.cmd.angles[PITCH] = ANGLE2SHORT( ang[PITCH] );
			secPm.cmd.angles[YAW] = ANGLE2SHORT( ang[YAW] );
			if ( t >= SEC_PM_MSEC * 2 ) {
				secPm.cmd.buttons |= BUTTON_ZIP;
				zipped = qtrue;
				takeoff = t;
			}
		} else {
			secPm.cmd.angles[YAW] = ANGLE2SHORT( yaw );
			if ( !( secPs.loopSound & RMZ_ZIPPING ) && hd > 6 ) {
				secPm.cmd.forwardmove = 127;
			}
		}

		if ( !( moves & SM_ZIP ) && takeoff < 0 && onGround && t > 0 ) {
			qboolean	go = hd < jumpDist;

			// blocked by a wall / at the edge of the floor: jump now
			if ( !go && t > 160 && hs < 60 && hd > 30 ) {
				go = qtrue;
			}
			if ( !go ) {
				VectorCopy( d, e );
				e[2] = 0;
				VectorNormalize( e );
				VectorMA( secPs.origin, 30, e, p );
				VectorCopy( p, e );
				e[2] -= 44;
				trap_Trace( &tr, p, NULL, NULL, e, ENTITYNUM_NONE, MASK_PLAYERSOLID );
				go = tr.fraction >= 1.0f;
			}
			if ( go ) {
				takeoff = t;
				if ( moves & SM_DJ ) {
					airAt = t + airDelay;
				}
				if ( moves & SM_DASH ) {
					dashAt = t + dashDelay;
				}
			}
		}
		if ( takeoff >= 0 && !( moves & SM_ZIP ) ) {
			if ( t - takeoff < 48 || ( ( moves & SM_HOLD ) && !airDone ) ) {
				secPm.cmd.upmove = 127;
			}
			// the air jump: at its time, or when a wall run ends
			if ( airAt >= 0 && !airDone ) {
				if ( ( moves & SM_HOLD ) ? ( wasWr && !( secPs.stats[STAT_RMFLAGS] & RMS_WALLRUN ) ) : t >= airAt ) {
					airDone = qtrue;
					airAt = t + SEC_PM_MSEC;		// released this frame, pressed the next ones
					secPm.cmd.upmove = 0;
				}
			} else if ( airDone && t >= airAt && t < airAt + 48 ) {
				secPm.cmd.upmove = 127;
			}
			if ( dashAt >= 0 && t >= dashAt && t < dashAt + 48 ) {
				secPm.cmd.buttons |= BUTTON_DASH;
			}
		}
		wasWr = ( secPs.stats[STAT_RMFLAGS] & RMS_WALLRUN ) != 0;

		Pmove( &secPm );

		if ( secPs.origin[2] < floorZ ) {
			return qfalse;
		}
		if ( trap_PointContents( secPs.origin, -1 ) & ( CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_NODROP ) ) {
			return qfalse;
		}
		if ( takeoff >= 0 && secPs.groundEntityNum != ENTITYNUM_NONE && t - takeoff > 64 ) {
			if ( Sec_Landed( secPs.origin, target, area ) ) {
				return qtrue;
			}
			// down somewhere else: give it a moment to walk the last bit (a lip, the ledge's edge)
			if ( landT < 0 ) {
				landT = t;
			}
			if ( t - landT > 400 || hd > 120 ) {
				return qfalse;
			}
		}
	}
	return qfalse;
}

/*
=================
Sec_TryFlown

Every flown variant of a move set from one launch point
=================
*/
static qboolean Sec_TryFlown( const vec3_t L, const vec3_t target, int area, int moves ) {
	static const float	jd[3] = { 0, 110, 230 };
	static const int	air[2] = { 60, 200 };
	static const int	dash[2] = { 120, 320 };
	int		i, j;

	if ( moves & SM_WR ) {
		for ( i = 0 ; i < 2 ; i++ ) {
			if ( Sec_PmFly( L, target, area, moves | SM_HOLD, jd[i * 2], 0, dash[1], NULL ) ) {
				return qtrue;
			}
		}
		return qfalse;
	}
	for ( i = 0 ; i < 3 ; i++ ) {
		for ( j = 0 ; j < 2 ; j++ ) {
			if ( Sec_PmFly( L, target, area, moves, jd[i], air[j], dash[j], NULL ) ) {
				if ( secCvDebug.integer ) {
					G_LogPrintf( "Secrets: dbg flown moves %i from %s to %s: jump at %.0f, air jump +%i ms, dash +%i ms\n",
						moves, vtos( L ), vtos( target ), jd[i], air[j], dash[j] );
				}
				return qtrue;
			}
			if ( !( moves & ( SM_DJ | SM_DASH ) ) ) {
				break;		// a plain jump has no timing to vary
			}
		}
	}
	return qfalse;
}

/*
=================
Sec_LineTo

Zip line (grapple: qtrue) from the launch point's eye to an anchor on or behind the spot: the
cable holds there (first world hit, not the sky), the rider's box gets close along the line,
then drops (a hop for the zip) onto the spot.
=================
*/
static qboolean Sec_LineAnchor( const vec3_t eye, const vec3_t anchor, float range ) {
	vec3_t		dir, end;
	trace_t		tr;
	float		d;

	VectorSubtract( anchor, eye, dir );
	d = VectorNormalize( dir );
	if ( d < ZIP_MIN_DIST + 16 || d > range ) {
		return qfalse;
	}
	VectorMA( anchor, 6, dir, end );
	trap_Trace( &tr, eye, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID );
	if ( tr.fraction >= 1.0f || ( tr.surfaceFlags & ( SURF_SKY | SURF_NOIMPACT ) ) || tr.entityNum != ENTITYNUM_WORLD ) {
		return qfalse;
	}
	return Distance( tr.endpos, anchor ) < 10;
}

static qboolean Sec_LineRide( const vec3_t launch, const vec3_t anchor, const vec3_t target, int area, qboolean grapple ) {
	vec3_t		dir, stop, p, end;
	trace_t		tr;
	float		d;

	VectorSubtract( anchor, launch, dir );
	d = VectorNormalize( dir );
	VectorMA( anchor, grapple ? -36.0f : -( ZIP_ARRIVE_DIST + 8 ), dir, stop );
	trap_Trace( &tr, launch, secPMins, secPMaxs, stop, ENTITYNUM_NONE, SEC_MASK );
	if ( !tr.allsolid && tr.fraction < 1.0f && Distance( tr.endpos, stop ) > 72 ) {
		// caught on a lip the eye line clears: step over it (PM_ZipStepOver)
		VectorCopy( tr.endpos, p );
		VectorCopy( p, end );
		end[2] += 48;
		trap_Trace( &tr, p, secPMins, secPMaxs, end, ENTITYNUM_NONE, SEC_MASK );
		VectorCopy( tr.endpos, p );
		trap_Trace( &tr, p, secPMins, secPMaxs, stop, ENTITYNUM_NONE, SEC_MASK );
	}
	if ( tr.allsolid || ( tr.fraction < 1.0f && Distance( tr.endpos, stop ) > 72 ) ) {
		return qfalse;
	}
	VectorCopy( tr.endpos, p );
	// drop off: the zip hops (ZIP_HOP up), then the rider steers over the spot and falls
	VectorCopy( p, end );
	end[2] += grapple ? 8 : 40;
	trap_Trace( &tr, p, secPMins, secPMaxs, end, ENTITYNUM_NONE, SEC_MASK );
	VectorCopy( tr.endpos, p );
	VectorSet( end, target[0], target[1], p[2] );
	trap_Trace( &tr, p, secPMins, secPMaxs, end, ENTITYNUM_NONE, SEC_MASK );
	if ( tr.allsolid || Sec_Dist2D( tr.endpos, target ) > 24 ) {
		return qfalse;
	}
	VectorCopy( tr.endpos, p );
	VectorCopy( p, end );
	end[2] -= 400;
	trap_Trace( &tr, p, secPMins, secPMaxs, end, ENTITYNUM_NONE, SEC_MASK );
	if ( tr.fraction >= 1.0f || tr.plane.normal[2] < 0.7f ) {
		return qfalse;
	}
	(void)d;
	return Sec_Landed( tr.endpos, target, area );
}

// the anchors to try for a spot: its floor, the walls around it (two heights)
static int Sec_LineAnchors( const vec3_t floor, vec3_t *out, int max ) {
	vec3_t		start, end;
	trace_t		tr;
	int			i, n = 0;
	float		a;

	VectorCopy( floor, out[n] );
	out[n][2] += 1;
	n++;
	// the ceiling right above (ride up to it, drop onto the spot)
	VectorCopy( floor, start );
	start[2] += 40;
	VectorCopy( start, end );
	end[2] += 600;
	trap_Trace( &tr, start, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID );
	if ( tr.fraction < 1.0f && !( tr.surfaceFlags & ( SURF_SKY | SURF_NOIMPACT ) ) && tr.endpos[2] - floor[2] > 90 ) {
		VectorCopy( tr.endpos, out[n] );
		out[n][2] -= 1;
		n++;
	}
	for ( i = 0 ; i < 8 && n < max - 1 ; i++ ) {
		a = i * M_PI / 4;
		VectorCopy( floor, start );
		start[2] += 30;
		VectorSet( end, start[0] + cos( a ) * 90, start[1] + sin( a ) * 90, start[2] );
		trap_Trace( &tr, start, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID );
		if ( tr.fraction < 1.0f && fabs( tr.plane.normal[2] ) < 0.3f && !( tr.surfaceFlags & SURF_SKY ) ) {
			VectorMA( tr.endpos, 1, tr.plane.normal, out[n] );
			n++;
			VectorCopy( out[n - 1], out[n] );
			out[n][2] += 34;
			n++;
		}
	}
	return n;
}

/*
=================
Sec_TryAbilities

An unreachable spot: the launch points nearby (walkable samples, and the edge toward the
spot), every move. Fills the sample's alternative masks (the cheapest ones).
=================
*/
static void Sec_AddAlt( secSample_t *s, int mask, const vec3_t launch ) {
	int		i;

	for ( i = 0 ; i < s->numAlts ; i++ ) {
		if ( ( s->alts[i] & mask ) == s->alts[i] ) {
			return;		// a cheaper (or equal) way is known
		}
	}
	// drop the ones this is cheaper than
	for ( i = 0 ; i < s->numAlts ; ) {
		if ( ( mask & s->alts[i] ) == mask ) {
			s->alts[i] = s->alts[--s->numAlts];
		} else {
			i++;
		}
	}
	if ( s->numAlts < SEC_ALTS ) {
		if ( !s->numAlts ) {
			VectorCopy( launch, s->launch );
		}
		s->alts[s->numAlts++] = mask;
	}
}

static qboolean Sec_HasAlt( secSample_t *s, int mask ) {
	int		i;

	for ( i = 0 ; i < s->numAlts ; i++ ) {
		if ( ( s->alts[i] & mask ) == s->alts[i] ) {
			return qtrue;
		}
	}
	return qfalse;
}

// the last floor point from L toward S (the edge a player would jump from)
static void Sec_EdgeToward( const vec3_t L, const vec3_t S, vec3_t out ) {
	vec3_t		dir, p, q, end;
	trace_t		tr;
	float		d, s;

	VectorCopy( L, out );
	VectorSubtract( S, L, dir );
	dir[2] = 0;
	d = VectorNormalize( dir );
	for ( s = 16 ; s < d - 16 && s < 600 ; s += 16 ) {
		VectorMA( L, s, dir, p );
		trap_Trace( &tr, out, secPMins, secPMaxs, p, ENTITYNUM_NONE, SEC_MASK );
		if ( tr.fraction < 1.0f ) {
			return;
		}
		VectorCopy( p, q );
		VectorCopy( p, end );
		end[2] -= 30;
		trap_Trace( &tr, q, secPMins, secPMaxs, end, ENTITYNUM_NONE, SEC_MASK );
		if ( tr.fraction >= 1.0f || tr.plane.normal[2] < 0.7f ) {
			return;
		}
		if ( trap_PointContents( p, -1 ) & ( CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_NODROP | CONTENTS_TRIGGER ) ) {
			return;
		}
		VectorCopy( tr.endpos, out );
	}
}

static qboolean Sec_TryAbilities( secSample_t *s, int deadline ) {
	int			i, j, k, li, n, cand[SEC_MAX_LAUNCH], nc = 0, na;
	float		cd[SEC_MAX_LAUNCH], d, dz;
	vec3_t		target, L, eye, anchors[18];
	secSample_t	*o;

	VectorCopy( s->org, target );
	target[2] += 24;
	// nearest walkable launch points (not much higher than the spot)
	for ( i = 0 ; i < secNumSamples ; i++ ) {
		o = &secSamples[i];
		if ( !o->ok || !o->walk ) {
			continue;
		}
		dz = s->org[2] - o->org[2];
		if ( dz < -500 || dz > 600 ) {
			continue;
		}
		d = Distance( o->org, s->org );
		if ( d > 1500 ) {
			continue;
		}
		if ( nc < SEC_MAX_LAUNCH ) {
			cand[nc] = i;
			cd[nc++] = d;
		} else {
			for ( k = 0, j = 1 ; j < nc ; j++ ) {
				if ( cd[j] > cd[k] ) {
					k = j;
				}
			}
			if ( d < cd[k] ) {
				cand[k] = i;
				cd[k] = d;
			}
		}
	}
	// nearest first
	for ( i = 0 ; i < nc ; i++ ) {
		for ( j = i + 1 ; j < nc ; j++ ) {
			if ( cd[j] < cd[i] ) {
				d = cd[i]; cd[i] = cd[j]; cd[j] = d;
				k = cand[i]; cand[i] = cand[j]; cand[j] = k;
			}
		}
	}
	// a floor nobody can even see from the walkable map (inside the walls, outside the hull): skip
	for ( i = 0 ; i < nc ; i++ ) {
		trace_t	tr;

		VectorCopy( secSamples[cand[i]].org, eye );
		eye[2] += 24 + DEFAULT_VIEWHEIGHT;
		VectorCopy( s->org, L );
		L[2] += 16;
		trap_Trace( &tr, eye, NULL, NULL, L, ENTITYNUM_NONE, MASK_SOLID );
		if ( tr.fraction >= 1.0f ) {
			break;
		}
	}
	if ( i >= nc ) {
		s->ok = qfalse;
		return qtrue;
	}
	na = Sec_LineAnchors( s->org, anchors, 18 );
	if ( secCvDebug.integer && !s->tryIdx ) {
		G_LogPrintf( "Secrets: dbg try %s: %i launches, nearest %.0f away dz %.0f (seen from launch %i at %s), %i anchors\n",
			vtos( s->org ), nc, cd[0], s->org[2] - secSamples[cand[0]].org[2], i, vtos( secSamples[cand[i]].org ), na );
	}
	for ( li = s->tryIdx ; li < nc * 2 ; li++ ) {
		// out of time for this frame: carry on from the next launch point next frame
		if ( li > s->tryIdx && trap_Milliseconds() > deadline ) {
			s->tryIdx = li;
			return qfalse;
		}
		o = &secSamples[cand[li / 2]];
		VectorCopy( o->org, L );
		L[2] += 24;
		if ( li & 1 ) {
			Sec_EdgeToward( L, target, L );
			if ( Distance( L, o->org ) < 30 ) {
				continue;
			}
		}
		dz = target[2] - L[2];
		d = Sec_Dist2D( L, target );
		// flown moves (real Pmove runs): near enough, not too high
		if ( li < 12 && d < 700 && dz < 140 && dz > -400 ) {
			// a plain jump does it: no ability needed (a jump-up spot, walkable)
			if ( !s->numAlts && Sec_TryFlown( L, target, s->area, 0 ) ) {
				s->walk = 1;
				s->jumpUp = qtrue;
				return qtrue;
			}
			if ( !Sec_HasAlt( s, SA_DJ ) && Sec_TryFlown( L, target, s->area, SM_DJ ) ) {
				Sec_AddAlt( s, SA_DJ, L );
			}
			if ( !Sec_HasAlt( s, SA_DASH ) && dz < 80 && Sec_TryFlown( L, target, s->area, SM_DASH ) ) {
				Sec_AddAlt( s, SA_DASH, L );
			}
			if ( !Sec_HasAlt( s, SA_WALLRUN ) && dz < 80 && d > 150 && Sec_TryFlown( L, target, s->area, SM_WR ) ) {
				Sec_AddAlt( s, SA_WALLRUN, L );
			}
			if ( !Sec_HasAlt( s, SA_DJ | SA_DASH ) && Sec_TryFlown( L, target, s->area, SM_DJ | SM_DASH ) ) {
				Sec_AddAlt( s, SA_DJ | SA_DASH, L );
			}
			if ( !Sec_HasAlt( s, SA_DJ | SA_WALLRUN ) && d > 150 && Sec_TryFlown( L, target, s->area, SM_DJ | SM_WR ) ) {
				Sec_AddAlt( s, SA_DJ | SA_WALLRUN, L );
			}
		}
		// cables
		if ( !Sec_HasAlt( s, SA_ZIP ) || !Sec_HasAlt( s, SA_GRAPPLE ) ) {
			VectorCopy( L, eye );
			eye[2] += DEFAULT_VIEWHEIGHT;
			for ( n = 0 ; n < na ; n++ ) {
				if ( !Sec_HasAlt( s, SA_ZIP ) && Sec_LineAnchor( eye, anchors[n], ZIP_RANGE - 300 )
					&& Sec_PmFly( L, target, s->area, SM_ZIP, 0, 0, 0, anchors[n] ) ) {
					Sec_AddAlt( s, SA_ZIP, L );
				}
				if ( !Sec_HasAlt( s, SA_GRAPPLE ) && anchors[n][2] > s->org[2] + 8 && Sec_LineAnchor( eye, anchors[n], 1200 )
					&& Sec_LineRide( L, anchors[n], target, s->area, qtrue ) ) {
					Sec_AddAlt( s, SA_GRAPPLE, L );
				}
			}
		}
		if ( Sec_HasAlt( s, SA_DJ ) && Sec_HasAlt( s, SA_DASH ) && Sec_HasAlt( s, SA_ZIP ) && Sec_HasAlt( s, SA_GRAPPLE ) ) {
			break;
		}
	}
	return qtrue;
}

/*
===============================================================================

THE SCAN (incremental: a slice per server frame)

===============================================================================
*/

static int	secBoxBuf[1024];

static void Sec_AddArea( int a ) {
	aas_areainfo_t	info;

	if ( a <= 0 || a >= SEC_MAX_AREAS || secAreaSeen[a] ) {
		return;
	}
	secAreaSeen[a] = 1;
	if ( a > secMaxArea ) {
		secMaxArea = a;
	}
	if ( !trap_AAS_AreaInfo( a, &info ) ) {
		return;
	}
	AddPointToBounds( info.mins, secBMins, secBMaxs );
	AddPointToBounds( info.maxs, secBMins, secBMaxs );
	if ( !( info.flags & SEC_AREA_GROUNDED ) || ( info.flags & ( SEC_AREA_LIQUID | SEC_AREA_DISABLED ) )
		|| ( info.contents & SEC_AC_BAD ) ) {
		return;
	}
	secAreaList[secNumAreas++] = a;
}

// every area in a box: a full answer (1024) splits the box in eight
static void Sec_BoxAreas( const vec3_t mins, const vec3_t maxs, int depth ) {
	vec3_t	lo, hi, mid;
	int		n, i;

	n = trap_AAS_BBoxAreas( (float *)mins, (float *)maxs, secBoxBuf, 1024 );
	if ( n >= 1024 && depth < 8 ) {
		VectorAdd( mins, maxs, mid );
		VectorScale( mid, 0.5f, mid );
		for ( i = 0 ; i < 8 ; i++ ) {
			lo[0] = ( i & 1 ) ? mid[0] : mins[0];
			hi[0] = ( i & 1 ) ? maxs[0] : mid[0];
			lo[1] = ( i & 2 ) ? mid[1] : mins[1];
			hi[1] = ( i & 2 ) ? maxs[1] : mid[1];
			lo[2] = ( i & 4 ) ? mid[2] : mins[2];
			hi[2] = ( i & 4 ) ? maxs[2] : mid[2];
			Sec_BoxAreas( lo, hi, depth + 1 );
		}
		return;
	}
	for ( i = 0 ; i < n ; i++ ) {
		Sec_AddArea( secBoxBuf[i] );
	}
}

// one area -> up to 5 sample points (its middle, and the corners of a big one)
static void Sec_SampleArea( int area ) {
	aas_areainfo_t	info;
	vec3_t		pts[5], start, end, p;
	trace_t		tr;
	int			n = 1, i;
	secSample_t	*s;
	static const vec3_t	pmins = { -8, -8, 0 }, pmaxs = { 8, 8, 4 };

	if ( !trap_AAS_AreaInfo( area, &info ) ) {
		return;
	}
	VectorCopy( info.center, pts[0] );
	if ( info.maxs[0] - info.mins[0] > 96 && info.maxs[1] - info.mins[1] > 96 ) {
		for ( i = 0 ; i < 4 ; i++ ) {
			pts[n][0] = ( i & 1 ) ? info.maxs[0] - 20 : info.mins[0] + 20;
			pts[n][1] = ( i & 2 ) ? info.maxs[1] - 20 : info.mins[1] + 20;
			pts[n][2] = info.center[2];
			n++;
		}
	}
	for ( i = 0 ; i < n && secNumSamples < SEC_MAX_SAMPLES ; i++ ) {
		VectorSet( start, pts[i][0], pts[i][1], info.mins[2] + 4 );
		VectorSet( end, pts[i][0], pts[i][1], info.mins[2] - 72 );
		trap_Trace( &tr, start, pmins, pmaxs, end, ENTITYNUM_NONE, SEC_MASK );
		if ( tr.startsolid || tr.fraction >= 1.0f ) {
			secRej[10]++;
			continue;
		}
		VectorCopy( tr.endpos, p );
		p[2] += 25;
		if ( trap_AAS_PointAreaNum( p ) != area ) {
			secRej[11]++;
			continue;
		}
		p[2] -= 25;
		p[2] += 0.25f;
		if ( !Sec_CheckSpot( p ) ) {
			continue;
		}
		s = &secSamples[secNumSamples++];
		memset( s, 0, sizeof( *s ) );
		VectorCopy( p, s->org );
		s->area = area;
		s->ok = qtrue;
	}
}

static int Sec_SpawnArea( const vec3_t org, vec3_t out ) {
	int		a;
	trace_t	tr;
	vec3_t	end;

	VectorCopy( org, end );
	end[2] -= 256;
	trap_Trace( &tr, org, NULL, NULL, end, ENTITYNUM_NONE, SEC_MASK );		// a point: the box would start in the floor
	if ( tr.startsolid || tr.fraction >= 1.0f ) {
		VectorCopy( org, out );
	} else {
		VectorCopy( tr.endpos, out );
		out[2] += 24;
	}
	out[2] += 1;
	a = trap_AAS_PointAreaNum( out );
	if ( !a ) {
		out[2] += 8;
		a = trap_AAS_PointAreaNum( out );
	}
	return a;
}

// up to 3 spawn points far apart: the stock routes start from these
static void Sec_PickRefs( void ) {
	int		i, j, best, a;
	float	d, md, bd;
	vec3_t	p;

	secNumRefs = 0;
	while ( secNumRefs < 3 ) {
		best = -1;
		bd = -1;
		for ( i = 0 ; i < secNumSpawn ; i++ ) {
			a = Sec_SpawnArea( secSpawn[i], p );
			if ( !a ) {
				continue;
			}
			md = 1e9f;
			for ( j = 0 ; j < secNumRefs ; j++ ) {
				d = Distance( p, secRefOrg[j] );
				if ( d < md ) {
					md = d;
				}
			}
			if ( !secNumRefs ) {
				best = i;
				break;
			}
			if ( md > bd && md > 64 ) {
				bd = md;
				best = i;
			}
		}
		if ( best < 0 ) {
			break;
		}
		secRefArea[secNumRefs] = Sec_SpawnArea( secSpawn[best], secRefOrg[secNumRefs] );
		if ( secCvDebug.integer ) {
			aas_areainfo_t	info;

			memset( &info, 0, sizeof( info ) );
			trap_AAS_AreaInfo( secRefArea[secNumRefs], &info );
			G_LogPrintf( "Secrets: dbg ref %i spawn %s -> %s area %i flags %i presence %i reach %i\n", secNumRefs, vtos( secSpawn[best] ),
				vtos( secRefOrg[secNumRefs] ), secRefArea[secNumRefs], info.flags, info.presencetype, trap_AAS_AreaReachability( secRefArea[secNumRefs] ) );
		}
		secNumRefs++;
	}
}
// stock travel time from the spawns (0: none), -1 not asked yet
static int Sec_WalkTime( int area ) {
	int		i, t, best = 0;

	if ( area <= 0 ) {
		return 0;
	}
	for ( i = 0 ; i < secNumRefs ; i++ ) {
		if ( secRefArea[i] == area ) {
			return 1;
		}
		t = trap_AAS_AreaTravelTimeToGoalArea( secRefArea[i], secRefOrg[i], area, TFL_DEFAULT );
		if ( t > 0 && ( !best || t < best ) ) {
			best = t;
		}
	}
	return best;
}

#define SEC_COL_STEP		40

static int	secNumColumn;

/*
=================
Sec_Column

Every floor down one column of the map: trace down, note the floor, sink through its brush,
trace on. A floor the AAS has no area for (botclip ledges, beams, wall tops) or no stock route
to becomes an ability candidate.
=================
*/
static void Sec_Column( int x, int y ) {
	vec3_t		start, end, p;
	trace_t		tr;
	int			i, layers = 0, a, steps;
	float		bottom = secBMins[2] - 64;
	secSample_t	*s;

	VectorSet( start, x, y, secBMaxs[2] + 64 );
	while ( start[2] > bottom && layers < 16 && secNumSamples < SEC_MAX_SAMPLES ) {
		// out of any brush first
		for ( steps = 0 ; steps < 256 && start[2] > bottom && ( trap_PointContents( start, -1 ) & SEC_MASK ) ; steps++ ) {
			start[2] -= 8;
		}
		VectorSet( end, x, y, bottom );
		trap_Trace( &tr, start, NULL, NULL, end, ENTITYNUM_NONE, SEC_MASK );
		if ( tr.startsolid || tr.fraction >= 1.0f ) {
			break;
		}
		layers++;
		VectorCopy( tr.endpos, p );
		start[2] = p[2] - 2;
		if ( tr.plane.normal[2] < 0.85f || tr.entityNum != ENTITYNUM_WORLD || ( tr.surfaceFlags & ( SURF_SKY | SURF_NOIMPACT ) ) ) {
			continue;
		}
		p[2] += 0.25f;
		p[2] += 25;
		a = trap_AAS_PointAreaNum( p );
		p[2] -= 25;
		if ( a > 0 && a < SEC_MAX_AREAS ) {
			if ( secAreaWalk[a] < 0 ) {
				secAreaWalk[a] = Sec_WalkTime( a );
			}
			if ( secAreaWalk[a] > 0 ) {
				continue;		// a normal floor: the area samples have it
			}
		}
		// next to a wall the player's box can't stand on the point itself (no AAS area there):
		// a reachable area a step away means a normal floor too
		{
			vec3_t	q;
			int		k, b;

			for ( k = 0 ; k < 8 ; k++ ) {
				VectorCopy( p, q );
				q[0] += ( k == 0 || k == 4 || k == 5 ) ? 24 : ( k == 1 || k == 6 || k == 7 ) ? -24 : 0;
				q[1] += ( k == 2 || k == 4 || k == 6 ) ? 24 : ( k == 3 || k == 5 || k == 7 ) ? -24 : 0;
				q[2] += 25;
				b = trap_AAS_PointAreaNum( q );
				if ( b > 0 && b < SEC_MAX_AREAS ) {
					if ( secAreaWalk[b] < 0 ) {
						secAreaWalk[b] = Sec_WalkTime( b );
					}
					if ( secAreaWalk[b] > 0 ) {
						break;
					}
				}
			}
			if ( k < 8 ) {
				continue;
			}
		}
		// next to one we kept already (this column or the last ones)
		for ( i = secNumSamples - 1 ; i >= 0 && i >= secNumSamples - 40 ; i-- ) {
			if ( secSamples[i].area == a && fabs( secSamples[i].org[2] - p[2] ) < 4 && Sec_Dist2D( secSamples[i].org, p ) < 70 ) {
				break;
			}
		}
		if ( i >= 0 && i >= secNumSamples - 40 ) {
			continue;
		}
		if ( !Sec_CheckSpot( p ) ) {
			continue;
		}
		s = &secSamples[secNumSamples++];
		memset( s, 0, sizeof( *s ) );
		VectorCopy( p, s->org );
		s->area = a;
		s->ok = qtrue;
		secNumColumn++;
	}
}


/*
=================
Sec_Snug

Push a walkable spot back against the nearest wall, then along it into a corner when one is
close: a strongbox stands against a wall, not in the middle of a corridor. *yaw = its front.
=================
*/
static qboolean Sec_WallDir( const vec3_t org, const vec3_t dir, float range, float *dist, vec3_t normal ) {
	vec3_t		start, end;
	trace_t		tr;

	VectorCopy( org, start );
	start[2] += 12;
	VectorMA( start, range, dir, end );
	trap_Trace( &tr, start, NULL, NULL, end, ENTITYNUM_NONE, SEC_MASK );
	if ( tr.fraction >= 1.0f || fabs( tr.plane.normal[2] ) > 0.3f ) {
		return qfalse;
	}
	*dist = tr.fraction * range;
	if ( normal ) {
		VectorCopy( tr.plane.normal, normal );
	}
	return qtrue;
}

static void Sec_Snug( secSample_t *s ) {
	vec3_t		dir, best, nrm, p, side;
	float		a, d, bd = 1e9f;
	int			i, k;
	qboolean	found = qfalse;

	for ( i = 0 ; i < 8 ; i++ ) {
		a = i * M_PI / 4;
		VectorSet( dir, cos( a ), sin( a ), 0 );
		if ( Sec_WallDir( s->org, dir, 120, &d, nrm ) && d < bd ) {
			bd = d;
			VectorScale( nrm, -1, best );		// straight at the wall
			best[2] = 0;
			VectorNormalize( best );
			found = qtrue;
		}
	}
	s->yaw = -1;
	if ( !found ) {
		return;
	}
	// back against it: 16 units off (the box is 13 deep from its middle)
	if ( Sec_WallDir( s->org, best, 140, &d, NULL ) && d > 17 ) {
		VectorMA( s->org, d - 16, best, p );
		if ( Sec_CheckSpot( p ) ) {
			VectorCopy( p, s->org );
		}
	}
	s->yaw = AngleNormalize360( RAD2DEG( atan2( -best[1], -best[0] ) ) );
	// a corner along the wall?
	VectorSet( side, -best[1], best[0], 0 );
	for ( k = 0 ; k < 2 ; k++ ) {
		if ( k ) {
			VectorScale( side, -1, side );
		}
		if ( Sec_WallDir( s->org, side, 72, &d, NULL ) && d > 20 ) {
			VectorMA( s->org, d - 19, side, p );
			if ( Sec_CheckSpot( p ) ) {
				VectorCopy( p, s->org );
				break;
			}
		}
	}
}

static void Sec_Features( secSample_t *s ) {
	vec3_t		start, end;
	trace_t		tr;
	int			i;
	float		a, d;

	s->corner = 0;
	for ( i = 0 ; i < 8 ; i++ ) {
		a = i * M_PI / 4;
		VectorCopy( s->org, start );
		start[2] += 20;
		VectorSet( end, start[0] + cos( a ) * 64, start[1] + sin( a ) * 64, start[2] );
		trap_Trace( &tr, start, NULL, NULL, end, ENTITYNUM_NONE, SEC_MASK );
		if ( tr.fraction < 1.0f ) {
			s->corner++;
		}
	}
	VectorCopy( s->org, start );
	start[2] += 8;
	VectorCopy( start, end );
	end[2] += 256;
	trap_Trace( &tr, start, NULL, NULL, end, ENTITYNUM_NONE, SEC_MASK );
	s->ceil = (int)( tr.endpos[2] - s->org[2] );
	if ( tr.surfaceFlags & SURF_SKY ) {
		s->ceil = 999;
	}
	s->reach = trap_AAS_AreaReachability( s->area );
	s->itemDist = 99999;
	for ( i = 0 ; i < secNumAnchors ; i++ ) {
		d = Distance( secAnchors[i], s->org );
		if ( d < s->itemDist ) {
			s->itemDist = d;
		}
	}
}

static float Sec_Visibility( const vec3_t org ) {
	vec3_t		p;
	trace_t		tr;
	int			i, seen = 0;

	if ( !secNumVis ) {
		return 0;
	}
	VectorCopy( org, p );
	p[2] += 16;
	for ( i = 0 ; i < secNumVis ; i++ ) {
		if ( Distance( secVis[i], p ) > 2400 || !trap_InPVS( secVis[i], p ) ) {
			continue;
		}
		trap_Trace( &tr, secVis[i], NULL, NULL, p, ENTITYNUM_NONE, MASK_SOLID );
		if ( tr.fraction >= 1.0f ) {
			seen++;
		}
	}
	return (float)seen / secNumVis;
}

static float Sec_YawFor( const vec3_t org ) {
	vec3_t		start, end;
	trace_t		tr;
	int			i, best = -1;
	float		a, bf = 1.0f;

	VectorCopy( org, start );
	start[2] += 12;
	for ( i = 0 ; i < 8 ; i++ ) {
		a = i * M_PI / 4;
		VectorSet( end, start[0] + cos( a ) * 96, start[1] + sin( a ) * 96, start[2] );
		trap_Trace( &tr, start, NULL, NULL, end, ENTITYNUM_NONE, SEC_MASK );
		if ( tr.fraction < bf ) {
			bf = tr.fraction;
			best = i;
		}
	}
	if ( best < 0 ) {
		return ( (int)( org[0] * 7 + org[1] * 13 ) % 8 ) * 45.0f;
	}
	return AngleNormalize360( best * 45.0f + 180.0f );		// the back against the nearest wall
}

static int Sec_ScoreCompare( const void *a, const void *b ) {
	const secSample_t	*x = &secSamples[*(const int *)a], *y = &secSamples[*(const int *)b];

	return x->score < y->score ? 1 : x->score > y->score ? -1 : 0;
}

static int	secOrder[SEC_MAX_SAMPLES];

static void Sec_FileName( char *out, int size ) {
	Com_sprintf( out, size, "rearmed_secrets_%s.txt", secMap );
}

static void Sec_WriteCache( void ) {
	fileHandle_t	fh;
	char			name[MAX_QPATH], line[256];
	int				i;
	secSpot_t		*s;

	Sec_FileName( name, sizeof( name ) );
	trap_FS_FOpenFile( name, &fh, FS_WRITE );
	if ( !fh ) {
		G_Printf( S_COLOR_RED "secrets: can't write %s\n", name );
		return;
	}
	Com_sprintf( line, sizeof( line ), "v %i %i %i\n", SEC_FILE_VERSION, secMapSum, secNumSpots );
	trap_FS_Write( line, strlen( line ), fh );
	for ( i = 0 ; i < secNumSpots ; i++ ) {
		s = &secSpots[i];
		Com_sprintf( line, sizeof( line ), "%.1f %.1f %.1f %.0f %c %.1f %i %i %i %i %i %.0f %.0f %.0f\n",
			s->org[0], s->org[1], s->org[2], s->yaw, s->kind, s->score, s->numAlts,
			s->alts[0], s->alts[1], s->alts[2], s->alts[3], s->launch[0], s->launch[1], s->launch[2] );
		trap_FS_Write( line, strlen( line ), fh );
	}
	trap_FS_FCloseFile( fh );
}

// qtrue: the spots came from the cache (and the AAS still matches it)
static qboolean Sec_ReadCache( int mapSum ) {
	fileHandle_t	fh;
	char			name[MAX_QPATH], buf[8192], *p, *tok;
	int				len, v, ma, n, i, k;
	secSpot_t		*s;

	Sec_FileName( name, sizeof( name ) );
	len = trap_FS_FOpenFile( name, &fh, FS_READ );
	if ( !fh ) {
		return qfalse;
	}
	if ( len <= 0 || len >= (int)sizeof( buf ) ) {
		trap_FS_FCloseFile( fh );
		return qfalse;
	}
	trap_FS_Read( buf, len, fh );
	trap_FS_FCloseFile( fh );
	buf[len] = 0;
	p = buf;
	tok = COM_Parse( &p );
	if ( Q_stricmp( tok, "v" ) ) {
		return qfalse;
	}
	v = atoi( COM_Parse( &p ) );
	ma = atoi( COM_Parse( &p ) );
	n = atoi( COM_Parse( &p ) );
	if ( v != SEC_FILE_VERSION || ma != mapSum || n < 0 || n > SEC_MAX_SPOTS ) {
		return qfalse;
	}
	for ( i = 0 ; i < n ; i++ ) {
		s = &secSpots[i];
		memset( s, 0, sizeof( *s ) );
		s->org[0] = atof( COM_Parse( &p ) );
		s->org[1] = atof( COM_Parse( &p ) );
		s->org[2] = atof( COM_Parse( &p ) );
		s->yaw = atof( COM_Parse( &p ) );
		tok = COM_Parse( &p );
		s->kind = tok[0] ? tok[0] : '?';
		s->score = atof( COM_Parse( &p ) );
		s->numAlts = atoi( COM_Parse( &p ) );
		for ( k = 0 ; k < SEC_ALTS ; k++ ) {
			s->alts[k] = atoi( COM_Parse( &p ) );
		}
		s->launch[0] = atof( COM_Parse( &p ) );
		s->launch[1] = atof( COM_Parse( &p ) );
		s->launch[2] = atof( COM_Parse( &p ) );
		if ( s->numAlts < 0 || s->numAlts > SEC_ALTS ) {
			return qfalse;
		}
	}
	secNumSpots = n;
	return qtrue;
}

static const char *Sec_AltString( const int *alts, int num ) {
	static char	buf[128];
	int			i;

	buf[0] = 0;
	if ( !num ) {
		return "walk";
	}
	for ( i = 0 ; i < num ; i++ ) {
		if ( i ) {
			Q_strcat( buf, sizeof( buf ), " or " );
		}
		if ( !alts[i] ) {
			Q_strcat( buf, sizeof( buf ), "walk" );
			continue;
		}
		Q_strcat( buf, sizeof( buf ), va( "%s%s%s%s%s",
			( alts[i] & SA_DJ ) ? "DJ+" : "", ( alts[i] & SA_DASH ) ? "DASH+" : "", ( alts[i] & SA_WALLRUN ) ? "WALLRUN+" : "",
			( alts[i] & SA_ZIP ) ? "ZIP+" : "", ( alts[i] & SA_GRAPPLE ) ? "GRAPPLE+" : "" ) );
		buf[strlen( buf ) - 1] = 0;		// the last '+'
	}
	return buf;
}

/*
=================
Sec_Pick

The cache list: the best walkable and ability spots, spread over the map
=================
*/
static void Sec_PickSpots( void ) {
	int			i, j, n = 0, nWalk = 0, nAb = 0, tw = 0, nt = 0;
	secSample_t	*s;
	secSpot_t	*o;
	float		d, mt;
	qboolean	far;

	// median-ish walk time for "far from the spawns"
	for ( i = 0 ; i < secNumSamples ; i++ ) {
		if ( secSamples[i].ok && secSamples[i].walk > 0 ) {
			tw += secSamples[i].walk;
			nt++;
		}
	}
	mt = nt ? (float)tw / nt : 1;
	// hidden = among the least seen quarter of the shortlist (at least <= 12% seen, never above 30%)
	{
		float	v[600], t;
		int		nv = 0, a, b;

		for ( i = 0 ; i < secNumSamples && nv < 600 ; i++ ) {
			if ( secSamples[i].ok && secSamples[i].shortlist && secSamples[i].walk ) {
				v[nv++] = secSamples[i].vis;
			}
		}
		for ( a = 0 ; a < nv ; a++ ) {
			for ( b = a + 1 ; b < nv ; b++ ) {
				if ( v[b] < v[a] ) {
					t = v[a]; v[a] = v[b]; v[b] = t;
				}
			}
		}
		secVisThr = nv ? v[nv / 4] : 0.12f;
		if ( secVisThr < 0.12f ) {
			secVisThr = 0.12f;
		} else if ( secVisThr > 0.3f ) {
			secVisThr = 0.3f;
		}
	}
	for ( i = 0 ; i < secNumSamples ; i++ ) {
		s = &secSamples[i];
		if ( !s->ok || !s->shortlist ) {
			continue;
		}
		if ( s->walk ) {
			if ( s->vis > secVisThr || s->itemDist < 150 ) {
				continue;
			}
			s->score = 45.0f * ( 1.0f - s->vis / 0.3f ) * ( 1.0f - s->vis / 0.3f ) + 2.5f * s->corner
				+ ( s->ceil < 100 ? 12 : 0 ) + ( s->reach <= 1 ? 10 : 0 ) + 10.0f * Com_Clamp( 0, 1, ( s->walk - mt ) / mt )
				+ ( s->itemDist > 600 ? 6 : s->itemDist / 100.0f );
			s->kind = s->jumpUp ? 'J' : s->ceil < 100 ? 'U' : s->reach <= 1 ? 'D' : s->corner >= 4 ? 'C' : 'F';
			if ( s->jumpUp ) {
				s->score += 10;		// up on something: you have to look up to see it
			}
			if ( s->kind == 'F' && s->corner < 3 ) {
				continue;		// open floor: nothing hides it
			}
		} else {
			if ( !s->numAlts ) {
				continue;
			}
			s->score = 50.0f + 20.0f * ( 1.0f - s->vis ) + s->corner + ( s->itemDist > 300 ? 4 : 0 );
			{
				int	k, any = 0;

				for ( k = 0 ; k < s->numAlts ; k++ ) {
					any |= s->alts[k];
				}
				s->kind = ( any & ( SA_DJ | SA_DASH ) ) ? 'H' : ( any & SA_WALLRUN ) ? 'W' : 'Z';	// a jump, a run along a wall, a cable
			}
		}
		secOrder[n++] = i;
	}
	qsort( secOrder, n, sizeof( int ), Sec_ScoreCompare );
	secNumSpots = 0;
	for ( i = 0 ; i < n && secNumSpots < SEC_MAX_SPOTS ; i++ ) {
		s = &secSamples[secOrder[i]];
		if ( s->walk ? nWalk >= SEC_MAX_SPOTS / 2 : nAb >= SEC_MAX_SPOTS / 2 ) {
			continue;
		}
		far = qtrue;
		for ( j = 0 ; j < secNumSpots && far ; j++ ) {
			d = Distance( secSpots[j].org, s->org );
			if ( d < 320 && !( fabs( secSpots[j].org[2] - s->org[2] ) > 96 && d > 200 ) ) {
				far = qfalse;
			}
		}
		if ( !far ) {
			continue;
		}
		o = &secSpots[secNumSpots++];
		memset( o, 0, sizeof( *o ) );
		VectorCopy( s->org, o->org );
		o->yaw = s->walk && s->yaw >= 0 ? s->yaw : Sec_YawFor( s->org );
		o->score = s->score;
		o->kind = s->kind;
		if ( s->walk ) {
			o->numAlts = 0;
			VectorCopy( s->org, o->launch );
			nWalk++;
		} else {
			o->numAlts = s->numAlts;
			memcpy( o->alts, s->alts, sizeof( o->alts ) );
			VectorCopy( s->launch, o->launch );
			o->launch[2] -= 24;
			nAb++;
		}
	}
	{
		int		nu = 0, na = 0, nvis = 0, k;

		for ( k = 0 ; k < secNumSamples ; k++ ) {
			if ( !secSamples[k].ok ) {
				continue;
			}
			if ( !secSamples[k].walk ) {
				nu++;
				na += secSamples[k].numAlts > 0;
			} else if ( secSamples[k].shortlist && secSamples[k].vis <= secVisThr ) {
				nvis++;
			}
		}
		G_LogPrintf( "Secrets: scan %s: %i areas (max %i), %i samples, %i off the stock routes (%i reachable with moves), "
			"%i hidden walkable, %i refs, %i spots (%i walkable, %i ability) in %i ms, slowest slice %i ms\n", secMap, secNumAreas, secMaxArea,
			secNumSamples, nu, na, nvis, secNumRefs, secNumSpots, nWalk, nAb, trap_Milliseconds() - secScanStart, secSlowest );
		if ( secCvDebug.integer ) G_LogPrintf( "Secrets: rejected samples: floor %i box %i room %i corners %i liquid %i liquid2 %i ents %i spawn %i | nofloor %i otherarea %i\n",
			secRej[0], secRej[1], secRej[2], secRej[3], secRej[4], secRej[5], secRej[6], secRej[7], secRej[10], secRej[11] );
		if ( secCvDebug.integer ) G_LogPrintf( "Secrets: floor rejects: none/solid %i steep %i entity %i surface %i\n", secRej[12], secRej[13], secRej[14], secRej[15] );
	}
}

static void Sec_LogSpots( void ) {
	int			i;
	secSpot_t	*s;

	for ( i = 0 ; i < secNumSpots ; i++ ) {
		s = &secSpots[i];
		G_LogPrintf( "Secrets: spot %i %s at %.0f %.0f %.0f kind %c score %.1f needs %s\n", i, secMap,
			s->org[0], s->org[1], s->org[2], s->kind, s->score, Sec_AltString( s->alts, s->numAlts ) );
	}
}

// one slice of the scan; returns when the frame's budget is spent
static void Sec_ScanStep( void ) {
	int			i, t0 = trap_Milliseconds(), phaseWas;
	secSample_t	*s;

	// a few msec a frame while someone plays (a listen server shares the frame), more for the batch tool
	secBudget = secCvDebug.integer >= 2 ? 12 : 3;
	phaseWas = secPhase;
	Sec_UnlinkMovers();
	switch ( secPhase ) {
	case SS_START:
		secScanStart = trap_Milliseconds();
		secSlowest = 0;
		Sec_CollectAnchors();
		if ( !secForceScan && Sec_ReadCache( secMapSum ) ) {
			secSpotsReady = qtrue;
			secPhase = SS_DONE;
			G_LogPrintf( "Secrets: %s: %i spots from the cache (map checksum %i)\n", secMap, secNumSpots, secMapSum );
			Sec_LogSpots();
			break;
		}
		secForceScan = qfalse;
		secNumSamples = 0;
		memset( secRej, 0, sizeof( secRej ) );
		memset( secAreaSeen, 0, sizeof( secAreaSeen ) );
		secNumAreas = 0;
		secMaxArea = 0;
		ClearBounds( secBMins, secBMaxs );
		secTile = 0;
		secPhase = SS_AREAS;
		break;
	case SS_AREAS:
		// 16 x 16 x 8 tiles of 1024 units (a full tile splits itself, Sec_BoxAreas)
		for ( ; secTile < 16 * 16 * 8 && trap_Milliseconds() - t0 < secBudget ; secTile++ ) {
			vec3_t	lo, hi;

			VectorSet( lo, -8192 + ( secTile & 15 ) * 1024, -8192 + ( ( secTile >> 4 ) & 15 ) * 1024, -4096 + ( secTile >> 8 ) * 1024 );
			VectorSet( hi, lo[0] + 1024, lo[1] + 1024, lo[2] + 1024 );
			Sec_BoxAreas( lo, hi, 0 );
		}
		if ( secTile >= 16 * 16 * 8 ) {
			secCursor = 0;
			secPhase = SS_SAMPLES;
			G_LogPrintf( "Secrets: scanning %s (%i grounded areas, max %i)\n", secMap, secNumAreas, secMaxArea );
		}
		break;
	case SS_SAMPLES:
		for ( ; secCursor < secNumAreas && trap_Milliseconds() - t0 < secBudget ; secCursor++ ) {
			Sec_SampleArea( secAreaList[secCursor] );
		}
		if ( secCursor >= secNumAreas ) {
			Sec_PickRefs();
			for ( i = 0 ; i < SEC_MAX_AREAS ; i++ ) {
				secAreaWalk[i] = -1;
			}
			if ( secCvDebug.integer >= 2 ) {
				int		nz = 0;

				for ( i = 0 ; i < secNumAreas ; i++ ) {
					secAreaWalk[secAreaList[i]] = Sec_WalkTime( secAreaList[i] );
					if ( !secAreaWalk[secAreaList[i]] ) {
						aas_areainfo_t	info;

						nz++;
						trap_AAS_AreaInfo( secAreaList[i], &info );
						G_LogPrintf( "Secrets: dbg area %i off the stock routes: mins %s maxs %s\n", secAreaList[i],
							vtos( info.mins ), vtos( info.maxs ) );
					}
				}
				G_LogPrintf( "Secrets: dbg %i of %i grounded areas off the stock routes\n", nz, secNumAreas );
			}
			secColX = (int)secBMins[0];
			secColY = (int)secBMins[1];
			secNumColumn = 0;
			secPhase = SS_COLUMNS;
		}
		break;
	case SS_COLUMNS:
		while ( secColX <= secBMaxs[0] && trap_Milliseconds() - t0 < secBudget ) {
			Sec_Column( secColX, secColY );
			secColY += SEC_COL_STEP;
			if ( secColY > secBMaxs[1] ) {
				secColY = (int)secBMins[1];
				secColX += SEC_COL_STEP;
			}
		}
		if ( secColX > secBMaxs[0] ) {
			G_LogPrintf( "Secrets: columns %s: %i samples with the off-route floors (%i)\n", secMap, secNumSamples, secNumColumn );
			secCursor = 0;
			secPhase = SS_WALK;
		}
		break;
	case SS_WALK:
		for ( ; secCursor < secNumSamples && trap_Milliseconds() - t0 < secBudget ; secCursor++ ) {
			s = &secSamples[secCursor];
			if ( secAreaWalk[s->area] < 0 ) {
				secAreaWalk[s->area] = Sec_WalkTime( s->area );
			}
			s->walk = secAreaWalk[s->area];
		}
		if ( secCursor >= secNumSamples ) {
			secCursor = 0;
			secPhase = SS_FEATURES;
		}
		break;
	case SS_FEATURES:
		for ( ; secCursor < secNumSamples && trap_Milliseconds() - t0 < secBudget ; secCursor++ ) {
			s = &secSamples[secCursor];
			s->yaw = -1;
			if ( s->ok && s->walk ) {
				Sec_Snug( s );
			}
			Sec_Features( s );
		}
		if ( secCursor >= secNumSamples ) {
			secCursor = 0;
			secPhase = SS_ABILITY;
		}
		break;
	case SS_ABILITY:
		while ( secCursor < secNumSamples && trap_Milliseconds() - t0 < secBudget ) {
			s = &secSamples[secCursor];
			if ( !s->ok || s->walk ) {
				secCursor++;
				continue;
			}
			if ( !s->tryIdx ) {
				// one test per patch of floor: a tested neighbour on the same level speaks for this one
				for ( i = 0 ; i < secCursor ; i++ ) {
					if ( secSamples[i].tested && fabs( secSamples[i].org[2] - s->org[2] ) < 8
						&& Distance( secSamples[i].org, s->org ) < ( secSamples[i].numAlts ? 160 : 72 ) ) {
						break;
					}
				}
				if ( i < secCursor ) {
					secCursor++;
					continue;
				}
				s->tested = qtrue;
				// the way back down: a stock route from the spot to a spawn (no AAS area: dropping off is the way)
				if ( s->area > 0 && secNumRefs && trap_AAS_AreaTravelTimeToGoalArea( s->area, s->org, secRefArea[0], TFL_DEFAULT ) <= 0 ) {
					vec3_t	o;

					VectorCopy( s->org, o );
					o[2] += 25;
					if ( trap_AAS_AreaTravelTimeToGoalArea( s->area, o, secRefArea[0], TFL_DEFAULT ) <= 0 ) {
						secCursor++;
						continue;
					}
				}
			}
			if ( !Sec_TryAbilities( s, t0 + secBudget ) ) {
				break;		// more launch points next frame
			}
			if ( secCvDebug.integer ) {
				G_LogPrintf( "Secrets: dbg tested %s area %i -> %s\n", vtos( s->org ), s->area,
					s->jumpUp ? "a plain jump" : s->numAlts ? Sec_AltString( s->alts, s->numAlts ) : "none" );
			}
			secCursor++;
		}
		if ( secCursor >= secNumSamples ) {
			int		n = 0, j, k;

			// the shortlist: every ability spot, the best-looking walkable ones
			for ( i = 0 ; i < secNumSamples ; i++ ) {
				s = &secSamples[i];
				s->shortlist = qfalse;
				if ( !s->ok ) {
					continue;
				}
				if ( !s->walk ) {
					s->shortlist = s->numAlts > 0;
					continue;
				}
				if ( s->itemDist < 150 ) {
					continue;
				}
				s->score = 3.0f * s->corner + ( s->ceil < 100 ? 10 : 0 ) + ( s->reach <= 1 ? 8 : 0 ) + ( s->itemDist > 600 ? 6 : s->itemDist / 100.0f );
				secOrder[n++] = i;
			}
			qsort( secOrder, n, sizeof( int ), Sec_ScoreCompare );
			for ( i = 0 ; i < n && i < 500 ; i++ ) {
				secSamples[secOrder[i]].shortlist = qtrue;
			}
			// visibility probes: spawns, items, a spread of walkable floor
			secNumVis = 0;
			for ( i = 0 ; i < secNumAnchors && secNumVis < SEC_MAX_VIS / 2 ; i++ ) {
				VectorCopy( secAnchors[i], secVis[secNumVis] );
				secVis[secNumVis++][2] += 40;
			}
			k = 0;
			for ( i = 0 ; i < secNumSamples ; i++ ) {
				if ( secSamples[i].ok && secSamples[i].walk ) {
					k++;
				}
			}
			for ( i = 0, j = 0 ; i < secNumSamples && secNumVis < SEC_MAX_VIS ; i++ ) {
				if ( !secSamples[i].ok || !secSamples[i].walk ) {
					continue;
				}
				if ( ( j++ % ( 1 + k / ( SEC_MAX_VIS / 2 ) ) ) == 0 ) {
					VectorCopy( secSamples[i].org, secVis[secNumVis] );
					secVis[secNumVis++][2] += 50;
				}
			}
			secCursor = 0;
			secPhase = SS_VIS;
		}
		break;
	case SS_VIS:
		for ( ; secCursor < secNumSamples && trap_Milliseconds() - t0 < secBudget ; secCursor++ ) {
			s = &secSamples[secCursor];
			if ( s->shortlist ) {
				s->vis = Sec_Visibility( s->org );
			}
		}
		if ( secCursor >= secNumSamples ) {
			secPhase = SS_PICK;
		}
		break;
	case SS_PICK:
		Sec_PickSpots();
		Sec_WriteCache();
		Sec_LogSpots();
		secSpotsReady = qtrue;
		secPhase = SS_DONE;
		break;
	default:
		break;
	}
	Sec_RelinkMovers();
	if ( trap_Milliseconds() - t0 > secSlowest ) {
		secSlowest = trap_Milliseconds() - t0;
	}
	if ( secCvDebug.integer && trap_Milliseconds() - t0 > 15 ) {
		G_LogPrintf( "Secrets: dbg slow slice %i ms in phase %i at %i\n", trap_Milliseconds() - t0, phaseWas, secCursor );
	}
}

/*
===============================================================================

STASHES ON THE FLOOR

===============================================================================
*/

static int Sec_OwnedMask( gentity_t *h ) {
	int		m = 0, f, f2;

	if ( !h || !h->client ) {
		return 0;
	}
	f = h->client->ps.stats[STAT_RMFLAGS];
	f2 = RM_FLAGS2( &h->client->ps );
	if ( f & RMF_DOUBLEJUMP ) {
		m |= SA_DJ;
	}
	if ( ( f & RMF_DASH ) && !( f2 & RMP2_BLINK ) ) {		// a blink dash teleports: the arc test doesn't hold
		m |= SA_DASH;
	}
	if ( f & RMF_WALLRUN ) {
		m |= SA_WALLRUN;
	}
	if ( f2 & RMF2_ZIPLINE ) {
		m |= SA_ZIP;
	}
	if ( g_offhandHook.integer && G_RL_HookAllowed( h->client ) ) {
		m |= SA_GRAPPLE;
	}
	return m;
}

static qboolean Sec_SpotAllowed( const secSpot_t *s, int owned ) {
	int		i;

	if ( !s->numAlts ) {
		return qtrue;
	}
	for ( i = 0 ; i < s->numAlts ; i++ ) {
		if ( ( s->alts[i] & owned ) == s->alts[i] ) {
			return qtrue;
		}
	}
	return qfalse;
}

// the better hidden, the likelier (squared: a plain corner rarely beats a ledge or a dead end)
static float Sec_PickWeight( const secSpot_t *s ) {
	float	w = s->score - 35.0f;

	return w < 1 ? 1 : w * w;
}

static int Sec_RollType( void ) {
	int		w[DSS_NUM], i, total = 0, r;
	float	luck = 1.0f + G_DS_LuckPct() * 0.01f;

	w[DSS_GEMS] = 52;
	w[DSS_GOLD] = 22;
	w[DSS_CHEST] = 14;
	w[DSS_OVERDRIVE] = SEC_USE_FRENZY ? (int)( 7 * luck ) : 0;
	w[DSS_LEGEND] = SEC_USE_CARDS ? (int)( 5 * luck ) : 0;
	for ( i = 0 ; i < DSS_NUM ; i++ ) {
		total += w[i];
	}
	r = Sec_Rand() % total;
	for ( i = 0 ; i < DSS_NUM ; i++ ) {
		r -= w[i];
		if ( r < 0 ) {
			return i;
		}
	}
	return DSS_GEMS;
}

static gentity_t *Sec_SpawnStash( const vec3_t org, float yaw, int type, int flags ) {
	gentity_t	*ent;

	ent = G_Spawn();
	ent->classname = "ds_stash";
	ent->s.eType = ET_GENERAL;
	ent->s.generic1 = RM_ENT_STASH;
	ent->s.modelindex2 = type;
	ent->s.frame = flags;
	ent->s.time = 0;
	ent->s.angles[YAW] = yaw;
	VectorCopy( ent->s.angles, ent->s.apos.trBase );
	VectorCopy( secBoxMins, ent->r.mins );
	VectorCopy( secBoxMaxs, ent->r.maxs );
	ent->r.maxs[2] = 20;
	ent->r.contents = ( flags & 6 ) ? 0 : CONTENTS_SOLID;
	ent->clipmask = 0;
	ent->takedamage = qfalse;
	G_SetOrigin( ent, (float *)org );
	{
		// on the visible floor (a thin player clip may lie on top of it)
		trace_t	tr;
		vec3_t	st, en;

		VectorCopy( org, st );
		st[2] += 2;
		VectorCopy( org, en );
		en[2] -= 9;
		trap_Trace( &tr, st, NULL, NULL, en, ENTITYNUM_NONE, MASK_SOLID );
		if ( tr.fraction < 1.0f && !tr.startsolid ) {
			ent->r.currentOrigin[2] = tr.endpos[2];
			ent->s.pos.trBase[2] = tr.endpos[2];
		}
	}
	ent->r.currentOrigin[2] += 0.5f;
	ent->s.pos.trBase[2] += 0.5f;
	trap_LinkEntity( ent );
	return ent;
}

static void Sec_WriteCS( void ) {
	char	pos[700], buf[1000];
	int		i;
	vec3_t	o;

	pos[0] = 0;
	if ( secDowse ) {
		for ( i = 0 ; i < secNumStash ; i++ ) {
			if ( !secStash[i].ent ) {
				continue;
			}
			VectorCopy( secStash[i].ent->r.currentOrigin, o );
			Q_strcat( pos, sizeof( pos ), va( "%s%i,%i,%i,%i", i ? ";" : "", (int)o[0], (int)o[1], (int)o[2], secStash[i].open ) );
		}
	}
	if ( !secNumStash && !secFound ) {
		buf[0] = 0;
	} else {
		Com_sprintf( buf, sizeof( buf ), "\\n\\%i\\f\\%i\\t\\%i\\ft\\%i\\dw\\%i\\p\\%s", secNumStash, secFound, secLastType,
			secLastFound, secDowse, pos[0] ? pos : "-" );
	}
	trap_SetConfigstring( CS_DSSECRETS, buf );
	secCsDirty = qfalse;
}

static void Sec_ClearStashes( void ) {
	int		i;

	for ( i = 0 ; i < secNumStash ; i++ ) {
		if ( secStash[i].ent && secStash[i].ent->inuse ) {
			G_FreeEntity( secStash[i].ent );
		}
	}
	secNumStash = 0;
	secFound = 0;
}

/*
=================
Sec_PickFloor

1-3 stashes from the spots the player can reach, seeded by the run and the floor
=================
*/
static void Sec_PickFloor( gentity_t *h, int count ) {
	int			elig[SEC_MAX_SPOTS], ne = 0, i, j, k, n, pick, seed;
	float		w, total, r, d;
	qboolean	ok;
	char		list[256];
	const char	*why;

	Sec_ClearStashes();
	secOwnedMask = Sec_OwnedMask( h );
	seed = atoi( G_RL_RunGet( "seed" ) );
	secRng = (unsigned)( seed * 7919 + G_RL_Floor() * 104729 + 17 );
	if ( !G_RL_Active() ) {
		secRng = (unsigned)trap_Milliseconds();
	}
	Sec_Rand();
	list[0] = 0;
	for ( i = 0 ; i < secNumSpots ; i++ ) {
		ok = Sec_SpotAllowed( &secSpots[i], secOwnedMask );
		why = ok ? "eligible" : "locked (a move you don't own)";
		if ( ok && h && Distance( h->r.currentOrigin, secSpots[i].org ) < 400 ) {
			ok = qfalse;		// not right at the player's start
			why = "near the start";
		}
		G_LogPrintf( "Secrets: floor %i spot %i needs %s owned 0x%x -> %s\n", G_RL_Floor(), i,
			Sec_AltString( secSpots[i].alts, secSpots[i].numAlts ), secOwnedMask, why );
		if ( ok ) {
			elig[ne++] = i;
		}
	}
	if ( count <= 0 ) {
		r = Sec_Frand();
		count = r < 0.35f ? 1 : r < 0.8f ? 2 : 3;
	}
	for ( n = 0 ; n < count && n < SEC_MAX_STASH && ne > 0 ; ) {
		total = 0;
		for ( i = 0 ; i < ne ; i++ ) {
			total += Sec_PickWeight( &secSpots[elig[i]] );
		}
		r = Sec_Frand() * total;
		pick = ne - 1;
		for ( i = 0 ; i < ne ; i++ ) {
			w = Sec_PickWeight( &secSpots[elig[i]] );
			if ( r < w ) {
				pick = i;
				break;
			}
			r -= w;
		}
		k = elig[pick];
		elig[pick] = elig[--ne];
		// apart from the others
		for ( j = 0 ; j < n ; j++ ) {
			d = Distance( secSpots[secStash[j].spot].org, secSpots[k].org );
			if ( d < 600 ) {
				break;
			}
		}
		if ( j < n ) {
			continue;
		}
		secStash[n].spot = k;
		secStash[n].type = Sec_RollType();
		secStash[n].open = qfalse;
		secStash[n].ent = Sec_SpawnStash( secSpots[k].org, secSpots[k].yaw, secStash[n].type, secDowse ? DSF_DOWSE : 0 );
		Q_strcat( list, sizeof( list ), va( " %i(%s,%c)", k, ds_stashNames[secStash[n].type], secSpots[k].kind ) );
		n++;
	}
	secNumStash = n;
	secCsDirty = qtrue;
	G_LogPrintf( "Secrets: floor %i %s: %i spots, owned 0x%x, %i stashes:%s\n", G_RL_Floor(), secMap, secNumSpots,
		secOwnedMask, n, list );
}

/*
=================
Sec_Open

The human found a stash: it opens, the loot pops, a beam, the fanfare, the stats
=================
*/
static void Sec_Open( secStash_t *st, gentity_t *h ) {
	gentity_t	*e = st->ent;
	vec3_t		org;
	int			floorNum = G_RL_Floor(), xp, sk;

	st->open = qtrue;
	e->s.time = level.time;
	e->s.frame &= ~DSF_DOWSE;
	trap_LinkEntity( e );
	VectorCopy( e->r.currentOrigin, org );
	org[2] += 26;
	if ( floorNum < 1 ) {
		floorNum = 1;
	}
	switch ( st->type ) {
	case DSS_GOLD:
		xp = 75 + 5 * floorNum;
		G_DS_SpawnGems( org, xp, 0 );
		sk = G_DS_Skulls( 2 + floorNum / 6 );
		if ( G_RL_Active() ) {
			G_RL_AddSkulls( sk );
		}
		break;
	case DSS_CHEST:
		G_DS_SpawnChest( org, 1 );
		G_DS_SpawnGems( org, 8 + floorNum, 0 );
		break;
	case DSS_LEGEND:
#if SEC_USE_CARDS
		G_DS_CardsGrantLegendary();
#endif
		G_DS_SpawnGems( org, 10 + floorNum, 0 );
		break;
	case DSS_OVERDRIVE:
#if SEC_USE_FRENZY
		G_DS_GrantSpike( h, DS_SPIKE_OVERDRIVE );
#endif
		G_DS_SpawnGems( org, 10 + floorNum, 0 );
		break;
	default:
		xp = 18 + 2 * floorNum;
		G_DS_SpawnGems( org, xp, 0 );
		break;
	}
#if SEC_USE_FRENZY
	G_DS_BeamAt( e->r.currentOrigin, st->type == DSS_LEGEND || st->type == DSS_GOLD ? DS_BEAM_LEGENDARY
		: st->type == DSS_OVERDRIVE || st->type == DSS_CHEST ? DS_BEAM_EPIC : DS_BEAM_RARE, 4500 );
#endif
	secFound++;
	secLastType = st->type;
	secLastFound = level.time;
	secCsDirty = qtrue;
	G_DS_Stat( DSST_STASHES, 1 );
	G_LogPrintf( "Secrets: found stash %i of %i (%s, spot %i, %c) floor %i\n", secFound, secNumStash,
		ds_stashNames[st->type], st->spot, secSpots[st->spot].kind, G_RL_Floor() );
	(void)h;
}

static qboolean Sec_CanFloor( void ) {
	if ( !secCvOn.integer || !G_RL_Active() ) {
		return qfalse;
	}
	return !G_DS_IsBossFloor() && !G_DS_IsVaultFloor();
}

/*
===============================================================================

HOOKS

===============================================================================
*/

void G_DS_SecretsInit( void ) {
	trap_Cvar_Register( &secCvOn, "g_dsSecrets", "1", CVAR_ARCHIVE );
	trap_Cvar_Register( &secCvDebug, "g_dsSecretsDebug", "0", 0 );
	secPhase = SS_IDLE;
	secSpotsReady = qfalse;
	secNumSpots = 0;
	secNumStash = 0;
	secFound = 0;
	secLastType = 0;
	secLastFound = 0;
	secPicked = qfalse;
	secHumanSince = 0;
	secForceScan = qfalse;
	memset( secStash, 0, sizeof( secStash ) );
	memset( secShow, 0, sizeof( secShow ) );
	trap_Cvar_VariableStringBuffer( "mapname", secMap, sizeof( secMap ) );
	secMapSum = trap_Cvar_VariableIntegerValue( "sv_mapChecksum" );
	Q_strlwr( secMap );
	trap_SetConfigstring( CS_DSSECRETS, "" );
}

void G_DS_SecretsFrame( void ) {
	gentity_t	*h;
	int			i;
	vec3_t		eye, c;
	trace_t		tr;
	qboolean	dowse;
	float		dz;

	trap_Cvar_Update( &secCvOn );
	// the scan starts once the bot library has the map (any mode: the dev commands use it too)
	if ( secPhase == SS_IDLE ) {
		// g_dsSecretsDebug 2: scan / load at once in any mode, 3: and quit when done (tools/secrets_scan.py)
		if ( ( Sec_CanFloor() || secForceScan || secCvDebug.integer >= 2 ) && trap_AAS_Initialized() && level.time - level.startTime > 300 ) {
			if ( secCvDebug.integer >= 4 ) {
				secForceScan = qtrue;	// 4: ignore the cache
			}
			secPhase = SS_START;
		} else {
			return;
		}
	}
	if ( secPhase != SS_DONE ) {
		Sec_ScanStep();
		if ( secPhase == SS_DONE && secCvDebug.integer >= 3 ) {
			G_LogPrintf( "Secrets: done, quitting (g_dsSecretsDebug %i)\n", secCvDebug.integer );
			trap_SendConsoleCommand( EXEC_APPEND, "quit\n" );
		}
		return;
	}
	h = Sec_Human();
	if ( !h || !h->client || h->client->pers.connected != CON_CONNECTED || h->client->sess.sessionTeam == TEAM_SPECTATOR ) {
		secHumanSince = 0;
		return;
	}
	// the floor's stashes once the player is in (its abilities are known)
	if ( !secPicked && Sec_CanFloor() && secSpotsReady ) {
		if ( h->health > 0 && !secHumanSince ) {
			secHumanSince = level.time;
		}
		if ( secHumanSince && level.time - secHumanSince > 1000 ) {
			secPicked = qtrue;
			secDowse = G_RL_HasCard( h, RLC_SECRET_FIRST + RLS_DOWSING );
			Sec_PickFloor( h, 0 );
		}
	}
	dowse = G_RL_Active() && G_RL_HasCard( h, RLC_SECRET_FIRST + RLS_DOWSING );
	if ( dowse != secDowse ) {
		secDowse = dowse;
		for ( i = 0 ; i < secNumStash ; i++ ) {
			if ( secStash[i].ent && !secStash[i].open ) {
				secStash[i].ent->s.frame = dowse ? DSF_DOWSE : 0;
			}
		}
		secCsDirty = qtrue;
	}
	// discovery: the human walks up to a closed stash in sight
	if ( h->health > 0 && !level.intermissiontime && !level.intermissionQueued ) {
		VectorCopy( h->client->ps.origin, eye );
		eye[2] += h->client->ps.viewheight;
		for ( i = 0 ; i < secNumStash ; i++ ) {
			if ( secStash[i].open || !secStash[i].ent ) {
				continue;
			}
			VectorCopy( secStash[i].ent->r.currentOrigin, c );
			c[2] += 14;
			// on its level: standing by it (a step lower at most) or above it, not jumping up at it from below
			dz = h->client->ps.origin[2] - ( secStash[i].ent->r.currentOrigin[2] + 24 );
			if ( Sec_Dist2D( c, h->client->ps.origin ) > SEC_OPEN_DIST || dz < -20 || dz > 64 ) {
				continue;
			}
			trap_Trace( &tr, eye, NULL, NULL, c, h->s.number, MASK_SOLID );
			if ( tr.fraction < 1.0f && tr.entityNum != secStash[i].ent->s.number ) {
				continue;
			}
			Sec_Open( &secStash[i], h );
		}
	}
	// dev proof that bots walk past stashes without opening them (bots never find stashes)
	if ( secCvDebug.integer && level.time % 1000 < 50 ) {
		int			k;
		gentity_t	*b;

		for ( k = 0 ; k < level.maxclients ; k++ ) {
			b = &g_entities[k];
			if ( !b->inuse || !b->client || !( b->r.svFlags & SVF_BOT ) || b->health <= 0 ) {
				continue;
			}
			for ( i = 0 ; i < secNumStash ; i++ ) {
				if ( secStash[i].ent && !secStash[i].open && Distance( b->r.currentOrigin, secStash[i].ent->r.currentOrigin ) < 150 ) {
					G_LogPrintf( "Secrets: bot %s is %.0f from stash %i (closed): bots don't open stashes\n",
						b->client->pers.netname, Distance( b->r.currentOrigin, secStash[i].ent->r.currentOrigin ), i );
				}
			}
		}
	}
	if ( secCsDirty ) {
		Sec_WriteCS();
	}
}

/*
===============================================================================

DEV COMMANDS

===============================================================================
*/

// a spot to look at spot n from: in sight, 150-300 units off, standing room
static qboolean Sec_ViewSpot( const vec3_t org, vec3_t out, vec3_t angles ) {
	vec3_t		p, end, dir, c;
	trace_t		tr;
	int			i, k;
	float		a, d, best = 1e9f, score;
	qboolean	found = qfalse;

	VectorCopy( org, c );
	c[2] += 14;
	for ( k = 0 ; k < 3 ; k++ ) {
		for ( i = 0 ; i < 16 ; i++ ) {
			a = i * M_PI / 8;
			d = 160 + 70 * k;
			VectorSet( p, org[0] + cos( a ) * d, org[1] + sin( a ) * d, org[2] + 60 );
			trap_Trace( &tr, c, NULL, NULL, p, ENTITYNUM_NONE, MASK_SOLID );
			if ( tr.fraction < 1.0f ) {
				continue;
			}
			VectorCopy( p, end );
			end[2] -= 300;
			trap_Trace( &tr, p, secPMins, secPMaxs, end, ENTITYNUM_NONE, MASK_PLAYERSOLID );
			if ( tr.startsolid || tr.fraction >= 1.0f || tr.plane.normal[2] < 0.7f ) {
				continue;
			}
			if ( trap_PointContents( tr.endpos, -1 ) & ( MASK_WATER | CONTENTS_NODROP | CONTENTS_TRIGGER ) ) {
				continue;
			}
			VectorCopy( tr.endpos, p );
			p[2] += DEFAULT_VIEWHEIGHT;
			trap_Trace( &tr, p, NULL, NULL, c, ENTITYNUM_NONE, MASK_SOLID );
			if ( tr.fraction < 1.0f ) {
				continue;
			}
			score = fabs( Distance( p, c ) - 200 ) + fabs( p[2] - c[2] ) * 0.5f;
			if ( score < best ) {
				best = score;
				VectorCopy( p, out );
				out[2] -= DEFAULT_VIEWHEIGHT + 24;	// the floor under the viewer
				found = qtrue;
			}
		}
		if ( found ) {
			break;
		}
	}
	if ( !found ) {
		return qfalse;
	}
	VectorCopy( out, p );
	p[2] += 24 + DEFAULT_VIEWHEIGHT;
	VectorSubtract( c, p, dir );
	vectoangles( dir, angles );
	return qtrue;
}

static void Sec_Teleport( gentity_t *h, const vec3_t org, const vec3_t angles ) {
	vec3_t	o;

	VectorCopy( org, o );
	o[2] += 25;		// org is a floor point
	TeleportPlayer( h, o, (float *)angles );
	VectorClear( h->client->ps.velocity );
}

static void Sec_ShowAll( qboolean on, qboolean plain ) {
	int		i;

	for ( i = 0 ; i < SEC_MAX_SPOTS ; i++ ) {
		if ( secShow[i] && secShow[i]->inuse ) {
			G_FreeEntity( secShow[i] );
		}
		secShow[i] = NULL;
	}
	if ( !on ) {
		return;
	}
	for ( i = 0 ; i < secNumSpots ; i++ ) {
		secShow[i] = Sec_SpawnStash( secSpots[i].org, secSpots[i].yaw, DSS_GEMS, plain ? 4 : 2 );
	}
}

qboolean G_DS_SecretsCommand( const char *cmd ) {
	char		a1[32], a2[32], csBuf[256];
	gentity_t	*h;
	int			i, t;
	vec3_t		org, ang, fwd;
	secStash_t	*st;

	if ( Q_stricmp( cmd, "dssecrets" ) && Q_stricmp( cmd, "dsstash" ) ) {
		return qfalse;
	}
	trap_Argv( 1, a1, sizeof( a1 ) );
	trap_Argv( 2, a2, sizeof( a2 ) );
	h = Sec_Human();
	if ( !Q_stricmp( cmd, "dsstash" ) ) {
		// dsstash <type 0-4|name> [here]: a stash in front of the player (or under it), opens on approach
		if ( !h ) {
			G_Printf( "dsstash: no player\n" );
			return qtrue;
		}
		t = atoi( a1 );
		for ( i = 0 ; i < DSS_NUM ; i++ ) {
			if ( !Q_stricmpn( a1, ds_stashNames[i], strlen( a1 ) ) && a1[0] && !( a1[0] >= '0' && a1[0] <= '9' ) ) {
				t = i;
			}
		}
		if ( t < 0 || t >= DSS_NUM ) {
			t = 0;
		}
		if ( secNumStash >= SEC_MAX_STASH ) {
			Sec_ClearStashes();
		}
		VectorCopy( h->client->ps.origin, org );
		if ( Q_stricmp( a2, "here" ) ) {
			trace_t	tr;
			vec3_t	end;

			VectorSet( ang, 0, h->client->ps.viewangles[YAW], 0 );
			AngleVectors( ang, fwd, NULL, NULL );
			VectorMA( org, 120, fwd, org );
			VectorCopy( org, end );
			end[2] -= 200;
			trap_Trace( &tr, org, secBoxMins, secBoxMaxs, end, h->s.number, MASK_PLAYERSOLID );
			VectorCopy( tr.endpos, org );
		} else {
			org[2] -= 24;
		}
		st = &secStash[secNumStash];
		st->spot = 0;
		st->type = t;
		st->open = qfalse;
		st->ent = Sec_SpawnStash( org, h->client->ps.viewangles[YAW] + 180, t, secDowse ? DSF_DOWSE : 0 );
		secNumStash++;
		secCsDirty = qtrue;
		G_Printf( "dsstash: %s at %s\n", ds_stashNames[t], vtos( org ) );
		return qtrue;
	}
	if ( !Q_stricmp( a1, "scan" ) ) {
		secForceScan = qtrue;
		secSpotsReady = qfalse;
		secPhase = SS_IDLE;
		G_Printf( "dssecrets: rescanning %s\n", secMap );
		return qtrue;
	}
	if ( !Q_stricmp( a1, "load" ) ) {
		if ( secPhase == SS_IDLE ) {
			secPhase = SS_START;		// cache or scan, outside a run too
		}
		return qtrue;
	}
	if ( !secSpotsReady ) {
		G_Printf( "dssecrets: no spots yet (phase %i, %i/%i); 'dssecrets load' starts it\n", secPhase, secCursor, secNumSamples );
		return qtrue;
	}
	if ( !a1[0] || !Q_stricmp( a1, "list" ) ) {
		for ( i = 0 ; i < secNumSpots ; i++ ) {
			G_Printf( "%2i %c %5.1f %s at %.0f %.0f %.0f%s\n", i, secSpots[i].kind, secSpots[i].score,
				Sec_AltString( secSpots[i].alts, secSpots[i].numAlts ), secSpots[i].org[0], secSpots[i].org[1], secSpots[i].org[2],
				h && Sec_SpotAllowed( &secSpots[i], Sec_OwnedMask( h ) ) ? "" : " (locked)" );
		}
		for ( i = 0 ; i < secNumStash ; i++ ) {
			G_Printf( "stash %i: spot %i %s %s\n", i, secStash[i].spot, ds_stashNames[secStash[i].type], secStash[i].open ? "found" : "hidden" );
		}
		Sec_LogSpots();
		trap_GetConfigstring( CS_DSSECRETS, csBuf, sizeof( csBuf ) );
		G_LogPrintf( "Secrets: list %s dowse %i card %i cs [%s] owned 0x%x stashes %i found %i\n", secMap, secDowse, RLC_SECRET_FIRST + RLS_DOWSING, csBuf, h ? Sec_OwnedMask( h ) : 0, secNumStash, secFound );
		return qtrue;
	}
	if ( !Q_stricmp( a1, "show" ) || !Q_stricmp( a1, "hide" ) ) {
		Sec_ShowAll( !Q_stricmp( a1, "show" ), !Q_stricmp( a2, "plain" ) );
		return qtrue;
	}
	if ( !Q_stricmp( a1, "put" ) ) {
		// dssecrets put <n> [type]: a real stash (opens on approach) on spot n: the reachability test
		char	a3[16];

		i = atoi( a2 );
		trap_Argv( 3, a3, sizeof( a3 ) );
		if ( i < 0 || i >= secNumSpots ) {
			return qtrue;
		}
		if ( secNumStash >= SEC_MAX_STASH ) {
			Sec_ClearStashes();
		}
		st = &secStash[secNumStash];
		st->spot = i;
		st->type = a3[0] ? atoi( a3 ) : DSS_GEMS;
		if ( st->type < 0 || st->type >= DSS_NUM ) {
			st->type = DSS_GEMS;
		}
		st->open = qfalse;
		st->ent = Sec_SpawnStash( secSpots[i].org, secSpots[i].yaw, st->type, secDowse ? DSF_DOWSE : 0 );
		secNumStash++;
		secCsDirty = qtrue;
		G_LogPrintf( "Secrets: put stash on spot %i (%c, %s)\n", i, secSpots[i].kind, Sec_AltString( secSpots[i].alts, secSpots[i].numAlts ) );
		return qtrue;
	}
	if ( !Q_stricmp( a1, "pick" ) || !Q_stricmp( a1, "spawn" ) ) {
		Sec_PickFloor( h, a2[0] ? atoi( a2 ) : 0 );
		return qtrue;
	}
	if ( !Q_stricmp( a1, "tp" ) || !Q_stricmp( a1, "tpstash" ) ) {
		char	a3[16];

		i = atoi( a2 );
		trap_Argv( 3, a3, sizeof( a3 ) );
		if ( !Q_stricmp( a1, "tpstash" ) ) {
			// tpstash <n>: to this floor's n-th stash (its spot)
			i = i >= 0 && i < secNumStash ? secStash[i].spot : -1;
		}
		if ( !h || i < 0 || i >= secNumSpots ) {
			G_Printf( "dssecrets tp: no spot %i\n", i );
			return qtrue;
		}
		if ( !Q_stricmp( a3, "launch" ) || !Q_stricmp( a3, "run" ) ) {
			VectorSubtract( secSpots[i].org, secSpots[i].launch, fwd );
			fwd[2] = 0;
			VectorNormalize( fwd );
			vectoangles( fwd, ang );
			ang[PITCH] = 0;
			VectorCopy( secSpots[i].launch, org );
			if ( !Q_stricmp( a3, "run" ) ) {
				// a run-up: back off along the line as far as the floor goes (up to 160)
				trace_t	tr;
				vec3_t	p, e;
				float	b;

				for ( b = 96 ; b >= 0 ; b -= 16 ) {
					VectorMA( secSpots[i].launch, -b, fwd, p );
					p[2] += 25;
					trap_Trace( &tr, p, secPMins, secPMaxs, p, h->s.number, MASK_PLAYERSOLID );
					if ( tr.startsolid ) {
						continue;
					}
					VectorCopy( p, e );
					e[2] -= 40;
					trap_Trace( &tr, p, secPMins, secPMaxs, e, h->s.number, MASK_PLAYERSOLID );
					if ( tr.fraction < 1.0f && fabs( tr.endpos[2] - ( secSpots[i].launch[2] + 24 ) ) < 20 ) {
						VectorCopy( tr.endpos, org );
						org[2] -= 24;
						break;
					}
				}
			}
			Sec_Teleport( h, org, ang );
		} else if ( !Q_stricmp( a3, "on" ) ) {
			VectorSet( ang, 0, secSpots[i].yaw, 0 );
			VectorCopy( secSpots[i].org, org );
			org[2] += 36;
			Sec_Teleport( h, org, ang );
		} else if ( Sec_ViewSpot( secSpots[i].org, org, ang ) ) {
			Sec_Teleport( h, org, ang );
		} else {
			G_Printf( "dssecrets tp: no view spot for %i\n", i );
			return qtrue;
		}
		G_LogPrintf( "Secrets: tp %i (%c, %s) to %s\n", i, secSpots[i].kind, Sec_AltString( secSpots[i].alts, secSpots[i].numAlts ),
			vtos( h->client->ps.origin ) );
		return qtrue;
	}
	G_Printf( "dssecrets list|show|hide|tp <n> [launch|on]|scan|load|pick [n]; dsstash <type> [here]\n" );
	return qtrue;
}
