/*
===========================================================================
REARMED -- DESCENT round 3 SURPRISES (ds-surprise), game side.

1. MID-FLOOR AMBUSH (g_dsAmbush): on about one fight / elite floor in three (never two floors
   in a row, never a boss floor), partway through, a mini-boss crashes in: a warning at the
   right edge ("AMBUSH: THE HEADSMAN"), a pillar of light where it will land (always a spawn
   spot at least DS_SURPRISE_MIN_DIST from the player, by preference 900+ and out of sight),
   4 s later it is there. Two strong sides of three like the act bosses, one life, one or two
   of the bosses' telegraphed attacks (RM_ENT_BOSSFX, the cgame draws them as on boss floors),
   it hunts everyone. Killing it: the elite chest + gems (LOOT's elite drop), more gems, skulls
   and a 35% OVERDRIVE drop (ds-frenzy's power spike). Health scales with G_DS_PowerScore.
2. FLOOR MODIFIERS (g_dsModifiers): run key node_mod (the UI writes it when the player toggled
   the node's modifier on the floor map): ds_modifiers[] in bg_ds_surprise.c.
3. WILD EVENTS (g_dsWildEvents, about one fight / elite floor in five): GOLD METEOR, BOT
   UPRISING, BLOOD ECLIPSE, BOUNTY.
4. GHOST OF A PAST RUN (g_dsGhosts): every floor start writes the player's kit at that depth
   (character, cards, weapon levels, passives, evolutions) to rearmed_ghost.txt (cur_<floor>);
   a new run moves those to past_<floor>. On a fight / elite floor with a past snapshot (and no
   ambush) the ghost comes about one time in three: "GHOST OF <name>", the past kit, a pale
   look (cgame), it hunts the player. Beating it pays skulls, gems, a chest: YOU SURPASSED
   YOURSELF.
Extras (ambusher, ghost, fodder) are out of the frag race both ways (no frags for killing them,
their own frags don't count) and leave when they die.
===========================================================================
*/
#include "g_local.h"

#define SUR_KIND_AMBUSH		1
#define SUR_KIND_GHOST		2
#define SUR_KIND_FODDER		3

#define SUR_RLR_BOT			2		// g_roguelike.c kit roles
#define SUR_RLR_ELITE		3

#define SUR_AMB_WARN		4000	// warning -> landing
#define SUR_GHOST_WARN		3500
#define SUR_PORTAL_WARN		1300
#define SUR_PREFER_DIST		900.0f
#define SUR_MAX_PENDING		10
#define SUR_MAX_FODDER		6
#define SUR_MAX_STRIKES		12
#define SUR_KICK_DELAY		1500
#define SUR_GOLD_FALL		6000	// the golden meteor: warning -> impact
#define SUR_GOLD_RACE		15000	// bots go for the crater this long after the impact
#define SUR_AMB_CHANCE		40		// % of eligible floors (not two in a row: ~1 in 3 overall)
#define SUR_EVENT_CHANCE	20		// % of eligible floors
#define SUR_GHOST_CHANCE	35		// % of eligible floors with a snapshot and no ambush
#define SUR_OVERDRIVE_PCT	35

#define SUR_GHOST_FILE		"rearmed_ghost.txt"
#define SUR_GH_MAX			96
#define SUR_GH_VAL			1024
#define SUR_GH_BUF			( SUR_GH_MAX * ( SUR_GH_VAL + 24 ) )

typedef struct {
	qboolean	used;
	int			kind;
	char		name[MAX_NETNAME];
	vec3_t		spot;
	float		yaw;
	int			landAt;			// addbot at this time
	qboolean	added;
	int			addedAt;
	int			warned;			// msec of warning it had
} surPending_t;

typedef struct {
	int			kind;			// DSB_
	int			land;
	int			damage;
	float		radius;
	vec3_t		org;
	gentity_t	*fx;
} surStrike_t;

typedef struct {
	qboolean	on;				// a DESCENT floor this file runs on
	int			floor, nodeType, seed, mod, difficulty;
	float		nodeSkill;
	int			numCards;
	int			humanIn;		// level.time the player was first in
	int			kind[MAX_CLIENTS];
	int			kickAt[MAX_CLIENTS];
	surPending_t	pend[SUR_MAX_PENDING];

	// ambush
	qboolean	ambPlan;
	int			ambArch;
	int			ambState;		// 0 waiting, 1 warning, 2 bot added, 3 live, 4 over
	int			ambEarliest, ambLatest;		// msec after the player came in
	int			ambWarnAt, ambLandAt;
	vec3_t		ambSpot;
	int			ambClient;
	float		ambPower;
	int			ambHpPct;
	// its attacks
	int			atk, atkStart, atkLand, atkEnd, atkStage, atkHitMask, atkTarget;
	vec3_t		atkFrom, atkTo, atkDir;
	gentity_t	*atkFx[6];
	int			nextAtk, cooldown[2];
	int			rootUntil, holdFireUntil;
	surStrike_t	strikes[SUR_MAX_STRIKES];

	// wild event
	int			evPlan;			// dsWildEvent_t planned
	int			evAt;			// msec after the player came in
	int			ev;				// running
	int			evStart, evEnd;
	int			evC;			// bounty target / gold impact time (CS)
	gentity_t	*evFx;
	vec3_t		goldSpot;
	int			goldImpact;
	gentity_t	*goldChest;
	int			goldRaceEnd;
	int			fodderNext[SUR_MAX_FODDER];
	int			fodderClient[SUR_MAX_FODDER];
	int			fodderSpawned, fodderSlain;
	int			bountyClient;
	int			evKills;

	// ghost
	qboolean	ghPlan;
	int			ghAt;
	int			ghState;		// 0 waiting, 1 warning, 2 added, 3 live, 4 over
	int			ghLandAt;
	vec3_t		ghSpot;
	int			ghClient;
	char		ghSnap[SUR_GH_VAL];
	int			ghChar;
	int			ghCards[RL_MAX_CARDS], ghNumCards;
	int			ghWl[DS_NUM_WEAPONS];
	int			ghPv[RLP_NUM];
	int			ghEvo;

	// network
	int			csDirty, csNext;

	// g_dsSurShots (tests): screenshots at the key moments
	int			shotAt[8], shotLook[8];
	qboolean	shotAttack, shotMod;
	qboolean	mbLogged;
	int			cvNext;
	qboolean	loggedXp, loggedGems;
} surState_t;

static surState_t	sur;
static vmCvar_t		surCvAmbush, surCvMods, surCvEvents, surCvGhosts, surCvShots;
static qboolean		surCvarsOn;

// rearmed_ghost.txt
static int			surGhNum;
static char			surGhKey[SUR_GH_MAX][24];
static char			surGhVal[SUR_GH_MAX][SUR_GH_VAL];
static char			surGhBuf[SUR_GH_BUF];

static const char *surFodderNames[SUR_MAX_FODDER] = { "Thrall", "Husk", "Ghoul", "Wretch", "Drudge", "Shade" };
#define SUR_FODDER_BOT		"Bones"

// the mini-bosses' guns (two each)
static const int surArchGuns[DS_MINIBOSS_NUM][2] = {
	{ WP_SHOTGUN, WP_ROCKET_LAUNCHER },
	{ WP_ROCKET_LAUNCHER, WP_MACHINEGUN },
	{ WP_RAILGUN, WP_PLASMAGUN },
	{ WP_GRENADE_LAUNCHER, WP_ROCKET_LAUNCHER },
	{ WP_LIGHTNING, WP_SHOTGUN }
};

static void Sur_EndAttack( void );

#define SUR_LOOK_NONE		0
#define SUR_LOOK_AMBUSH		1
#define SUR_LOOK_GHOST		2
#define SUR_LOOK_GOLD		3
#define SUR_LOOK_FODDER		4
#define SUR_LOOK_BOUNTY		5

// g_dsSurShots: a screenshot in delay msec, the player pointed at a surprise first
static void Sur_Shot( int delay, int look ) {
	int		i;

	if ( !surCvShots.integer ) {
		return;
	}
	for ( i = 0 ; i < 8 ; i++ ) {
		if ( !sur.shotAt[i] ) {
			sur.shotAt[i] = level.time + delay;
			sur.shotLook[i] = look;
			return;
		}
	}
}

/*
===============================================================================

HELPERS

===============================================================================
*/

static void Sur_Cvars( void ) {
	if ( !surCvarsOn ) {
		trap_Cvar_Register( &surCvAmbush, "g_dsAmbush", "1", CVAR_ARCHIVE );
		trap_Cvar_Register( &surCvMods, "g_dsModifiers", "1", CVAR_ARCHIVE );
		trap_Cvar_Register( &surCvEvents, "g_dsWildEvents", "1", CVAR_ARCHIVE );
		trap_Cvar_Register( &surCvGhosts, "g_dsGhosts", "1", CVAR_ARCHIVE );
		trap_Cvar_Register( &surCvShots, "g_dsSurShots", "0", 0 );		// tests only
		surCvarsOn = qtrue;
	}
	trap_Cvar_Update( &surCvAmbush );
	trap_Cvar_Update( &surCvMods );
	trap_Cvar_Update( &surCvEvents );
	trap_Cvar_Update( &surCvGhosts );
	trap_Cvar_Update( &surCvShots );
}

static unsigned Sur_Hash( unsigned a, unsigned b ) {
	unsigned	h = a * 2654435761u + b * 40503u + 977u;

	h ^= h >> 15;
	h *= 2246822519u;
	h ^= h >> 13;
	return h;
}

// a seeded roll 0..99 (the run seed and the floor: quitting and coming back doesn't re-roll)
static int Sur_Roll( int salt ) {
	return (int)( Sur_Hash( (unsigned)sur.seed + (unsigned)sur.floor * 7919u, (unsigned)salt ) % 100u );
}

static gentity_t *Sur_Human( void ) {
	int		h = G_RL_HumanClient();

	if ( h < 0 || h >= MAX_CLIENTS || !g_entities[h].inuse || !g_entities[h].client ) {
		return NULL;
	}
	return &g_entities[h];
}

static qboolean Sur_Alive( gentity_t *ent ) {
	return ent && ent->inuse && ent->client && ent->health > 0 && ent->client->ps.pm_type == PM_NORMAL
		&& ent->client->sess.sessionTeam != TEAM_SPECTATOR && ent->client->pers.connected == CON_CONNECTED;
}

static int Sur_Elapsed( void ) {
	return sur.humanIn ? level.time - sur.humanIn : 0;
}

static float Sur_PlayerDist( const vec3_t p ) {
	gentity_t	*h = Sur_Human();

	return h ? Distance( p, h->r.currentOrigin ) : 99999.0f;
}

static void Sur_Callout( int kind, int a, int b ) {
	trap_SendServerCommand( -1, va( "dsuann %i %i %i", kind, a, b ) );
}

static int Sur_FreeSlots( void ) {
	int		i, n = 0;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( level.clients[i].pers.connected == CON_DISCONNECTED ) {
			n++;
		}
	}
	for ( i = 0 ; i < SUR_MAX_PENDING ; i++ ) {
		if ( sur.pend[i].used && !sur.pend[i].added ) {
			n--;		// (already promised)
		}
	}
	return n;
}

/*
=================
Sur_FarSpot

A deathmatch spawn far from the player for something arriving: at least minDist away (never
under DS_SURPRISE_MIN_DIST), by preference out of the player's sight, the farther the better,
not on top of anyone. qfalse: no such spot
=================
*/
static qboolean Sur_FarSpot( float minDist, float maxDist, vec3_t out, float *yaw ) {
	gentity_t	*spot = NULL, *best = NULL, *h = Sur_Human(), *e;
	vec3_t		org, eye, mins = { -15, -15, -24 }, maxs = { 15, 15, 32 };
	trace_t		tr;
	float		d, score, bestScore = -1e9f;
	int			i;
	qboolean	near;

	if ( minDist < DS_SURPRISE_MIN_DIST ) {
		minDist = DS_SURPRISE_MIN_DIST;
	}
	while ( ( spot = G_Find( spot, FOFS( classname ), "info_player_deathmatch" ) ) != NULL ) {
		VectorCopy( spot->s.origin, org );
		org[2] += 9;
		trap_Trace( &tr, org, mins, maxs, org, ENTITYNUM_NONE, MASK_PLAYERSOLID );
		if ( tr.startsolid || tr.allsolid ) {
			continue;
		}
		near = qfalse;
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			e = &g_entities[i];
			if ( Sur_Alive( e ) && Distance( e->r.currentOrigin, org ) < 96 ) {
				near = qtrue;
			}
		}
		for ( i = 0 ; i < SUR_MAX_PENDING ; i++ ) {
			if ( sur.pend[i].used && Distance( sur.pend[i].spot, org ) < 96 ) {
				near = qtrue;		// another arrival is already coming down there
			}
		}
		if ( near ) {
			continue;
		}
		d = h ? Distance( org, h->r.currentOrigin ) : 2000;
		if ( d < minDist || ( maxDist > 0 && d > maxDist ) ) {
			continue;
		}
		score = ( d > 2400 ? 2400 : d ) + random() * 250;
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

// a spot with the widest preference first, then any spot at the hard minimum
static qboolean Sur_PickSpot( vec3_t out, float *yaw ) {
	return Sur_FarSpot( SUR_PREFER_DIST, 0, out, yaw ) || Sur_FarSpot( DS_SURPRISE_MIN_DIST, 0, out, yaw );
}

// the landing marker the cgame draws (a beam of light, a sound)
static gentity_t *Sur_Pillar( const vec3_t spot, int land, int style, int arch ) {
	gentity_t	*ent;

	ent = G_Spawn();
	ent->classname = "ds_pillar";
	ent->s.eType = ET_GENERAL;
	ent->s.generic1 = RM_ENT_SURPRISE;
	ent->s.modelindex2 = DSX2_PILLAR;
	ent->s.frame = style;
	ent->s.otherEntityNum2 = arch;
	ent->s.time = level.time;
	ent->s.time2 = land;
	ent->r.svFlags |= SVF_BROADCAST;
	G_SetOrigin( ent, (float *)spot );
	ent->think = G_FreeEntity;
	ent->nextthink = land + 900;
	trap_LinkEntity( ent );
	return ent;
}

static surPending_t *Sur_AddPending( int kind, const char *name, const vec3_t spot, float yaw, int landAt, int warned ) {
	surPending_t	*p;
	int				i;

	for ( i = 0 ; i < SUR_MAX_PENDING ; i++ ) {
		p = &sur.pend[i];
		if ( !p->used ) {
			memset( p, 0, sizeof( *p ) );
			p->used = qtrue;
			p->kind = kind;
			Q_strncpyz( p->name, name, sizeof( p->name ) );
			VectorCopy( spot, p->spot );
			p->yaw = yaw;
			p->landAt = landAt;
			p->warned = warned;
			return p;
		}
	}
	return NULL;
}

static void Sur_KickSoon( int client ) {
	if ( client >= 0 && client < MAX_CLIENTS && !sur.kickAt[client] ) {
		sur.kickAt[client] = level.time + SUR_KICK_DELAY;
	}
}

static int Sur_RunInt( const char *key ) {
	return atoi( G_RL_RunGet( key ) );
}

static void Sur_RunAdd( const char *key, int n ) {
	G_RL_RunSet( key, va( "%i", Sur_RunInt( key ) + n ) );
}

static int Sur_ParseCsv( const char *s, int *out, int max ) {
	int		n = 0;

	while ( s && *s && n < max ) {
		while ( *s == ',' || *s == '.' || *s == ' ' ) {
			s++;
		}
		if ( !*s ) {
			break;
		}
		out[n++] = atoi( s );
		while ( *s && *s != ',' && *s != '.' && *s != ' ' ) {
			s++;
		}
	}
	return n;
}

int G_DS_SurpriseMod( void ) {
	return sur.on ? sur.mod : DSMOD_NONE;
}

qboolean G_DS_SurpriseExtra( int client ) {
	if ( G_DS_AffixExtra( client ) ) {
		return qtrue;		// en-affix: split copies and champions are one-life extras too
	}
	return sur.on && client >= 0 && client < MAX_CLIENTS && sur.kind[client] != 0;
}

/*
===============================================================================

THE GHOST TRACK (rearmed_ghost.txt): cur_seed, cur_<floor>, past_<floor>

===============================================================================
*/

static void Sur_GhLoad( void ) {
	fileHandle_t	fh;
	int				len, k;
	char			*p, *line, *end, *v;

	surGhNum = 0;
	len = trap_FS_FOpenFile( SUR_GHOST_FILE, &fh, FS_READ );
	if ( !fh ) {
		return;
	}
	if ( len <= 0 || len >= SUR_GH_BUF ) {
		trap_FS_FCloseFile( fh );
		return;
	}
	trap_FS_Read( surGhBuf, len, fh );
	trap_FS_FCloseFile( fh );
	surGhBuf[len] = 0;
	p = surGhBuf;
	while ( *p && surGhNum < SUR_GH_MAX ) {
		line = p;
		while ( *p && *p != '\n' ) {
			p++;
		}
		if ( *p ) {
			*p++ = 0;
		}
		end = line + strlen( line );
		while ( end > line && ( end[-1] == '\r' || end[-1] == ' ' ) ) {
			*--end = 0;
		}
		for ( k = 0 ; line[k] && line[k] != ' ' ; k++ ) {
		}
		if ( !k ) {
			continue;
		}
		v = line + k;
		while ( *v == ' ' ) {
			v++;
		}
		line[k] = 0;
		Q_strncpyz( surGhKey[surGhNum], line, sizeof( surGhKey[0] ) );
		Q_strncpyz( surGhVal[surGhNum], v, sizeof( surGhVal[0] ) );
		surGhNum++;
	}
}

static void Sur_GhSave( void ) {
	fileHandle_t	fh;
	int				i;

	if ( G_RL_NoSave() ) {
		return;		// en-boss: boss practice leaves the ghost track alone (en-daily: the daily too)
	}
	surGhBuf[0] = 0;
	for ( i = 0 ; i < surGhNum ; i++ ) {
		if ( surGhKey[i][0] ) {
			Q_strcat( surGhBuf, sizeof( surGhBuf ), va( "%s %s\n", surGhKey[i], surGhVal[i] ) );
		}
	}
	trap_FS_FOpenFile( SUR_GHOST_FILE, &fh, FS_WRITE );
	if ( !fh ) {
		G_Printf( S_COLOR_YELLOW "descent: can't write %s\n", SUR_GHOST_FILE );
		return;
	}
	trap_FS_Write( surGhBuf, strlen( surGhBuf ), fh );
	trap_FS_FCloseFile( fh );
}

static const char *Sur_GhGet( const char *key ) {
	int		i;

	for ( i = 0 ; i < surGhNum ; i++ ) {
		if ( !Q_stricmp( surGhKey[i], key ) ) {
			return surGhVal[i];
		}
	}
	return "";
}

static void Sur_GhSet( const char *key, const char *val ) {
	int		i;

	for ( i = 0 ; i < surGhNum ; i++ ) {
		if ( !Q_stricmp( surGhKey[i], key ) ) {
			Q_strncpyz( surGhVal[i], val, sizeof( surGhVal[0] ) );
			return;
		}
	}
	for ( i = 0 ; i < surGhNum ; i++ ) {
		if ( !surGhKey[i][0] ) {
			break;		// a deleted slot
		}
	}
	if ( i == surGhNum ) {
		if ( surGhNum >= SUR_GH_MAX ) {
			return;
		}
		surGhNum++;
	}
	Q_strncpyz( surGhKey[i], key, sizeof( surGhKey[0] ) );
	Q_strncpyz( surGhVal[i], val, sizeof( surGhVal[0] ) );
}

/*
=================
Sur_GhostTrack

Floor start: a new run moves the last run's snapshots to past_<floor> (each depth keeps the
latest run that reached it), then this floor's kit goes to cur_<floor>
=================
*/
static void Sur_GhostTrack( void ) {
	char	wl[128], wx[128], snap[SUR_GH_VAL], key[24];
	int		i, evo, moved = 0;

	Sur_GhLoad();
	if ( atoi( Sur_GhGet( "cur_seed" ) ) != sur.seed ) {
		for ( i = 0 ; i < surGhNum ; i++ ) {
			if ( !Q_stricmpn( surGhKey[i], "cur_", 4 ) && Q_stricmp( surGhKey[i], "cur_seed" ) && surGhVal[i][0] ) {
				Com_sprintf( key, sizeof( key ), "past_%s", surGhKey[i] + 4 );
				Q_strncpyz( snap, surGhVal[i], sizeof( snap ) );
				surGhKey[i][0] = 0;
				Sur_GhSet( key, snap );
				moved++;
			}
		}
		for ( i = 0 ; i < surGhNum ; i++ ) {
			if ( !Q_stricmpn( surGhKey[i], "cur_", 4 ) ) {
				surGhKey[i][0] = 0;
			}
		}
		Sur_GhSet( "cur_seed", va( "%i", sur.seed ) );
		if ( moved ) {
			G_LogPrintf( "DSGhost: a new run: %i floor snapshots of the last run kept as the past\n", moved );
		}
	}
	// character | cards | weapon levels | passive levels | evolutions
	G_DS_ArsenalSave( wl, wx, sizeof( wl ), &evo );
	Com_sprintf( snap, sizeof( snap ), "%i|%s|%s|%s|%i", atoi( G_RL_RunGet( "character" ) ), G_RL_RunGet( "cards" ), wl,
		G_RL_RunGet( "pv" ), evo );
	Sur_GhSet( va( "cur_%i", sur.floor ), snap );
	Sur_GhSave();
}

// "chr|cards|wl|pv|evo" -> sur.gh*
static qboolean Sur_GhParse( const char *snap ) {
	char	part[5][SUR_GH_VAL];
	int		n = 0, k = 0;
	const char	*s;

	memset( part, 0, sizeof( part ) );
	for ( s = snap ; *s && n < 5 ; s++ ) {
		if ( *s == '|' ) {
			part[n][k] = 0;
			n++;
			k = 0;
			continue;
		}
		if ( k < SUR_GH_VAL - 1 ) {
			part[n][k++] = *s;
		}
	}
	if ( n < 4 ) {
		return qfalse;
	}
	Q_strncpyz( sur.ghSnap, snap, sizeof( sur.ghSnap ) );
	sur.ghChar = atoi( part[0] );
	if ( sur.ghChar < 0 || sur.ghChar >= CHR_NUM ) {
		sur.ghChar = CHR_RANGER;
	}
	sur.ghNumCards = Sur_ParseCsv( part[1], sur.ghCards, RL_MAX_CARDS );
	memset( sur.ghWl, 0, sizeof( sur.ghWl ) );
	Sur_ParseCsv( part[2], sur.ghWl, DS_NUM_WEAPONS );
	memset( sur.ghPv, 0, sizeof( sur.ghPv ) );
	Sur_ParseCsv( part[3], sur.ghPv, RLP_NUM );
	sur.ghEvo = atoi( part[4] );
	return qtrue;
}

/*
===============================================================================

INIT / PLAN

===============================================================================
*/

static void Sur_Publish( void ) {
	char	s[256];

	if ( !sur.on ) {
		trap_SetConfigstring( CS_DSSURPRISE, "" );
		return;
	}
	Com_sprintf( s, sizeof( s ), "\\mod\\%i\\mb\\%i\\mbk\\%i\\mbh\\%i\\ev\\%i\\evs\\%i\\eve\\%i\\evc\\%i\\gh\\%i",
		sur.mod, sur.ambState == 3 ? sur.ambClient : -1, sur.ambArch, sur.ambHpPct, sur.ev, sur.evStart, sur.evEnd, sur.evC,
		sur.ghState == 3 ? sur.ghClient : -1 );
	trap_SetConfigstring( CS_DSSURPRISE, s );
	sur.csDirty = 0;
	sur.csNext = level.time + 250;
}

void G_DS_SurpriseInit( void ) {
	const char	*nt, *snap;
	int			lastAmb, r;
	qboolean	normal;

	Sur_Cvars();
	memset( &sur, 0, sizeof( sur ) );
	sur.ambClient = sur.ghClient = sur.bountyClient = -1;
	if ( !G_RL_Active() ) {
		trap_SetConfigstring( CS_DSSURPRISE, "" );
		return;
	}
	sur.on = qtrue;
	sur.floor = G_RL_Floor();
	sur.seed = atoi( G_RL_RunGet( "seed" ) );
	sur.difficulty = G_DS_Difficulty();
	sur.nodeSkill = atof( G_RL_RunGet( "node_skill" ) );
	if ( sur.nodeSkill < 1 ) {
		sur.nodeSkill = sur.difficulty;
	}
	{
		int		tmp[RL_MAX_CARDS];

		sur.numCards = Sur_ParseCsv( G_RL_RunGet( "cards" ), tmp, RL_MAX_CARDS );
	}
	nt = G_RL_RunGet( "node_type" );
	sur.nodeType = !Q_stricmp( nt, "elite" ) ? 1 : !Q_stricmp( nt, "boss" ) ? 2 : !Q_stricmp( nt, "event" ) ? 3 : !Q_stricmp( nt, "vault" ) ? 4 : 0;
	normal = ( sur.nodeType == 0 || sur.nodeType == 1 ) && !G_DS_IsBossFloor() && !G_DS_IsVaultFloor();

	// the floor modifier the player chose on the map
	sur.mod = surCvMods.integer && normal ? BG_DS_ModByKey( G_RL_RunGet( "node_mod" ) ) : DSMOD_NONE;

	// the ghost track: this floor's kit for future runs
	Sur_GhostTrack();

	// the plan (seeded: a quit and resume gets the same floor)
	lastAmb = Sur_RunInt( "sur_amb" );
	if ( surCvAmbush.integer && normal && sur.floor >= 2 && lastAmb != sur.floor - 1 && Sur_Roll( 1 ) < SUR_AMB_CHANCE ) {
		sur.ambPlan = qtrue;
		sur.ambArch = Sur_Roll( 2 ) % DS_MINIBOSS_NUM;
		if ( Sur_RunInt( "sur_arch" ) == sur.ambArch + 1 ) {
			sur.ambArch = ( sur.ambArch + 1 ) % DS_MINIBOSS_NUM;		// not the same one twice running
		}
		sur.ambEarliest = 45000 + Sur_Roll( 3 ) * 150;
		sur.ambLatest = 80000 + Sur_Roll( 4 ) * 200;
	}
	if ( surCvEvents.integer && normal && sur.floor >= 2 && Sur_Roll( 5 ) < SUR_EVENT_CHANCE ) {
		sur.evPlan = 1 + Sur_Roll( 6 ) % ( DSEV_NUM - 1 );
		sur.evAt = 25000 + Sur_Roll( 7 ) * 150;
	}
	snap = Sur_GhGet( va( "past_%i", sur.floor ) );
	if ( surCvGhosts.integer && normal && !G_RL_Daily() && sur.floor >= 2 && !sur.ambPlan && snap[0] && Sur_Roll( 8 ) < SUR_GHOST_CHANCE		// (en-daily: no ghost of the player's own past runs)
		&& Sur_GhParse( snap ) ) {
		sur.ghPlan = qtrue;
		sur.ghAt = 30000 + Sur_Roll( 9 ) * 250;
	}
	// an uprising runs 30 s: the ambush comes after it
	if ( sur.ambPlan && sur.evPlan == DSEV_UPRISING && sur.ambEarliest < sur.evAt + DS_UPRISING_TIME + 12000 ) {
		sur.ambEarliest = sur.evAt + DS_UPRISING_TIME + 12000;
		sur.ambLatest = sur.ambEarliest + 20000;
	}
	r = sur.ambPlan || sur.evPlan || sur.ghPlan || sur.mod;
	G_LogPrintf( "DSSurprise: floor %i type %i mod %s ambush %s event %s ghost %s (snapshot %s)\n", sur.floor, sur.nodeType,
		sur.mod ? ds_modifiers[sur.mod].name : "-", sur.ambPlan ? ds_miniBosses[sur.ambArch].name : "-",
		sur.evPlan ? ds_wildEventNames[sur.evPlan] : "-", sur.ghPlan ? "yes" : "-", snap[0] ? "yes" : "no" );
	(void)r;
	Sur_Publish();
}

void G_DS_SurprisePreload( void ) {
	if ( !sur.on ) {
		return;
	}
	if ( sur.ambPlan ) {
		G_PreloadAddBot( ds_miniBosses[sur.ambArch].botFile );
	}
	if ( sur.ghPlan ) {
		G_PreloadAddBot( ds_characters[sur.ghChar].name );
	}
	if ( sur.evPlan == DSEV_UPRISING ) {
		G_PreloadAddBot( SUR_FODDER_BOT );
	}
}

/*
===============================================================================

ARRIVALS

===============================================================================
*/

static void Sur_StartAmbushWarning( void ) {
	float	yaw;

	if ( !Sur_PickSpot( sur.ambSpot, &yaw ) || Sur_FreeSlots() < 1 ) {
		sur.ambState = 4;
		G_LogPrintf( "DSSurprise: ambush %s called off (no far spot / no free slot)\n", ds_miniBosses[sur.ambArch].name );
		return;
	}
	sur.ambState = 1;
	sur.ambWarnAt = level.time;
	sur.ambLandAt = level.time + SUR_AMB_WARN;
	Sur_Pillar( sur.ambSpot, sur.ambLandAt, DSXS_AMBUSH, sur.ambArch );
	Sur_AddPending( SUR_KIND_AMBUSH, ds_miniBosses[sur.ambArch].netName, sur.ambSpot, yaw, sur.ambLandAt, SUR_AMB_WARN );
	Sur_Callout( DSUC_AMBUSH_WARN, sur.ambArch, 0 );
	Sur_Shot( 1600, SUR_LOOK_AMBUSH );
	G_RL_RunSet( "sur_amb", va( "%i", sur.floor ) );
	G_RL_RunSet( "sur_arch", va( "%i", sur.ambArch + 1 ) );
	G_LogPrintf( "DSSurprise: ambush warning %s at %i %i %i, %.0f from the player, lands in %i ms\n", ds_miniBosses[sur.ambArch].name,
		(int)sur.ambSpot[0], (int)sur.ambSpot[1], (int)sur.ambSpot[2], Sur_PlayerDist( sur.ambSpot ), SUR_AMB_WARN );
}

static void Sur_StartGhostWarning( void ) {
	float	yaw;

	if ( !Sur_PickSpot( sur.ghSpot, &yaw ) || Sur_FreeSlots() < 1 ) {
		sur.ghState = 4;
		G_LogPrintf( "DSGhost: called off (no far spot / no free slot)\n" );
		return;
	}
	sur.ghState = 1;
	sur.ghLandAt = level.time + SUR_GHOST_WARN;
	Sur_Pillar( sur.ghSpot, sur.ghLandAt, DSXS_GHOST, 0 );
	Sur_Callout( DSUC_GHOST_WARN, sur.ghChar, 0 );
	Sur_Shot( 1600, SUR_LOOK_GHOST );
	G_LogPrintf( "DSGhost: warning at %i %i %i, %.0f from the player, lands in %i ms (character %s, %i cards, evo 0x%x)\n",
		(int)sur.ghSpot[0], (int)sur.ghSpot[1], (int)sur.ghSpot[2], Sur_PlayerDist( sur.ghSpot ), SUR_GHOST_WARN,
		ds_characters[sur.ghChar].name, sur.ghNumCards, sur.ghEvo );
}

static void Sur_GhostName( char *out, int size ) {
	gentity_t	*h = Sur_Human();
	char		name[MAX_NETNAME];

	G_EN_CleanName( h ? h->client->pers.netname : "YOU", name, sizeof( name ) );
	if ( strlen( name ) > 24 ) {
		name[24] = 0;
	}
	Q_strupr( name );
	Com_sprintf( out, size, "GHOST OF %s", name[0] ? name : "YOU" );
}

static void Sur_AddGhostBot( void ) {
	char	name[MAX_NETNAME];
	float	skill = sur.nodeSkill + 0.5f;

	Sur_GhostName( name, sizeof( name ) );
	if ( skill > 5 ) {
		skill = 5;
	}
	Sur_AddPending( SUR_KIND_GHOST, name, sur.ghSpot, 0, level.time, SUR_GHOST_WARN );
	G_RL_AddExtraBot( ds_characters[sur.ghChar].name, name, skill, SUR_RLR_BOT, sur.ghCards, sur.ghNumCards, sur.ghPv );
	sur.ghState = 2;
}

static void Sur_AddAmbushBot( void ) {
	float	skill = sur.nodeSkill + 0.75f;

	if ( skill > 5 ) {
		skill = 5;
	}
	G_RL_AddExtraBot( ds_miniBosses[sur.ambArch].botFile, ds_miniBosses[sur.ambArch].netName, skill, SUR_RLR_ELITE, NULL,
		G_DS_KitCards( sur.numCards ), NULL );
	sur.ambState = 2;
}

// pending arrivals whose time came: the bot joins (its spawn goes to the spot)
static void Sur_RunPending( void ) {
	surPending_t	*p;
	int				i;

	for ( i = 0 ; i < SUR_MAX_PENDING ; i++ ) {
		p = &sur.pend[i];
		if ( !p->used ) {
			continue;
		}
		if ( p->added ) {
			if ( level.time > p->addedAt + 6000 ) {
				G_LogPrintf( "DSSurprise: %s never joined\n", p->name );
				if ( p->kind == SUR_KIND_AMBUSH ) {
					sur.ambState = 4;
				} else if ( p->kind == SUR_KIND_GHOST ) {
					sur.ghState = 4;
				}
				p->used = qfalse;
			}
			continue;
		}
		if ( level.time < p->landAt ) {
			continue;
		}
		p->added = qtrue;
		p->addedAt = level.time;
		switch ( p->kind ) {
		case SUR_KIND_AMBUSH:
			Sur_AddAmbushBot();
			break;
		case SUR_KIND_GHOST:
			break;		// (Sur_AddGhostBot already added it)
		default:
			G_RL_AddExtraBot( SUR_FODDER_BOT, p->name, 1.5f, SUR_RLR_BOT, NULL, 0, NULL );
			break;
		}
	}
}

/*
=================
G_DS_SurpriseSpawn

G_RL_SpawnLoadout, last. An extra's first spawn: onto its warned spot (still far from the
player? else the farthest spot there is, never closer than DS_SURPRISE_MIN_DIST). The floor
modifier's armour. Extras that respawn by accident leave.
=================
*/
static void Sur_Place( gentity_t *ent, surPending_t *p ) {
	vec3_t		ang, spot;
	float		d, yaw = p->yaw;
	qboolean	repicked = qfalse;
	int			n = ent - g_entities;

	VectorCopy( p->spot, spot );
	d = Sur_PlayerDist( spot );
	if ( d < DS_SURPRISE_MIN_DIST ) {
		// the player walked up to the beam: land somewhere else, far away
		if ( Sur_FarSpot( DS_SURPRISE_MIN_DIST, 0, spot, &yaw ) ) {
			repicked = qtrue;
			d = Sur_PlayerDist( spot );
		} else {
			VectorCopy( ent->client->ps.origin, spot );		// the map's own pick
			d = Sur_PlayerDist( spot );
		}
	}
	VectorCopy( spot, ent->client->ps.origin );
	VectorClear( ent->client->ps.velocity );
	VectorSet( ang, 0, yaw, 0 );
	{
		gentity_t	*h = Sur_Human();
		vec3_t		dd;

		if ( h ) {
			VectorSubtract( h->r.currentOrigin, spot, dd );
			vectoangles( dd, ang );
			ang[PITCH] = ang[ROLL] = 0;
		}
	}
	SetClientViewAngle( ent, ang );
	G_LogPrintf( "DSSurprise: %s (kind %i, client %i) lands %.0f from the player, warned %i ms%s%s\n", p->name, p->kind, n, d,
		repicked ? 0 : p->warned, repicked ? " (re-picked: the player was near the beam)" : "",
		d < DS_SURPRISE_MIN_DIST ? " NEAR" : "" );
}

static void Sur_GiveGun( gentity_t *ent, int w, int ammo ) {
	if ( w <= WP_NONE || w >= WP_NUM_WEAPONS ) {
		return;
	}
	ent->client->ps.stats[STAT_WEAPONS] |= 1 << w;
	if ( ent->client->ps.ammo[w] < ammo ) {
		ent->client->ps.ammo[w] = ammo;
	}
}

/*
=================
G_DS_SurpriseRaise

en-types: a summoner raises a skeleton from a corpse: uprising fodder named netName lands at
spot after warnMsec (a small portal first). qfalse = no room (a client slot is kept spare).
=================
*/
qboolean G_DS_SurpriseRaise( const char *netName, const vec3_t spot, float yaw, int warnMsec ) {
	if ( !sur.on || !netName || Sur_FreeSlots() < 2 ) {
		return qfalse;
	}
	if ( !Sur_AddPending( SUR_KIND_FODDER, netName, spot, yaw, level.time + warnMsec, warnMsec ) ) {
		return qfalse;
	}
	Sur_Pillar( spot, level.time + warnMsec, DSXS_FODDER, 0 );
	return qtrue;
}

void G_DS_SurpriseSpawn( gentity_t *ent ) {
	char			name[MAX_NETNAME];
	surPending_t	*p;
	int				i, n, w;

	if ( !sur.on || !ent || !ent->client ) {
		return;
	}
	n = ent - g_entities;
	if ( n < 0 || n >= MAX_CLIENTS ) {
		return;
	}
	// BLOODLUST / ARMOURED etc: the enemies' side
	if ( sur.mod && ( ent->r.svFlags & SVF_BOT ) && !G_RL_IsPlayer( ent ) && ds_modifiers[sur.mod].enemyArmor > 0 ) {
		ent->client->ps.stats[STAT_ARMOR] += ds_modifiers[sur.mod].enemyArmor;
		if ( ent->client->ps.stats[STAT_ARMOR] > 200 ) {
			ent->client->ps.stats[STAT_ARMOR] = 200;
		}
	}
	if ( !( ent->r.svFlags & SVF_BOT ) ) {
		return;
	}
	if ( sur.kind[n] ) {
		// an extra came back from the dead: it has one life
		Sur_KickSoon( n );
		return;
	}
	G_EN_CleanName( ent->client->pers.netname, name, sizeof( name ) );
	for ( i = 0 ; i < SUR_MAX_PENDING ; i++ ) {
		p = &sur.pend[i];
		if ( p->used && !Q_stricmp( p->name, name ) ) {
			break;
		}
	}
	if ( i == SUR_MAX_PENDING ) {
		return;
	}
	sur.kind[n] = p->kind;
	sur.kickAt[n] = 0;
	Sur_Place( ent, p );
	switch ( p->kind ) {
	case SUR_KIND_AMBUSH:
		sur.ambClient = n;
		sur.ambState = 3;
		sur.ambHpPct = 100;
		Sur_Shot( 500, SUR_LOOK_AMBUSH );
		Sur_Shot( 2200, SUR_LOOK_AMBUSH );
		Sur_GiveGun( ent, surArchGuns[sur.ambArch][0], 60 );
		Sur_GiveGun( ent, surArchGuns[sur.ambArch][1], 60 );
		ent->client->ps.weapon = surArchGuns[sur.ambArch][0];
		sur.nextAtk = level.time + 2500;
		sur.cooldown[0] = sur.cooldown[1] = 0;
		Sur_Callout( DSUC_AMBUSH_LAND, sur.ambArch, n );
		G_LogPrintf( "DSSurprise: ambush %s is in (client %i)\n", ds_miniBosses[sur.ambArch].name, n );
		break;
	case SUR_KIND_GHOST:
		sur.ghClient = n;
		sur.ghState = 3;
		Sur_Shot( 700, SUR_LOOK_GHOST );
		Sur_Shot( 2500, SUR_LOOK_GHOST );
		G_DS_SetWeaponLevels( ent, sur.ghWl, sur.ghEvo );
		// the guns the past self had levelled, and the character's own
		for ( w = 1 ; w < DS_NUM_WEAPONS ; w++ ) {
			if ( DS_WEAPON_LEVELS( w ) && sur.ghWl[w] >= 2 ) {
				Sur_GiveGun( ent, w, 50 );
			}
		}
		Sur_GiveGun( ent, ds_characters[sur.ghChar].startWeapon, 60 );
		G_LogPrintf( "DSGhost: %s is in: character %s, health %i, weapons 0x%x\n", name, ds_characters[sur.ghChar].name,
			ent->client->ps.stats[STAT_MAX_HEALTH], ent->client->ps.stats[STAT_WEAPONS] );
		break;
	default:
		for ( i = 0 ; i < SUR_MAX_FODDER ; i++ ) {
			if ( !Q_stricmp( surFodderNames[i], name ) ) {
				sur.fodderClient[i] = n;
			}
		}
		sur.fodderSpawned++;
		break;
	}
	p->used = qfalse;
	sur.csDirty = 1;
}

/*
===============================================================================

HOOKS: health, damage, speed, gems, skulls, cards

===============================================================================
*/

int G_DS_SurpriseHealthBonus( gentity_t *ent, int bonus ) {
	int		n, b = 0;
	float	p;

	if ( !sur.on || !ent || !ent->client ) {
		return 0;
	}
	n = ent - g_entities;
	if ( n >= 0 && n < MAX_CLIENTS ) {
		if ( sur.kind[n] == SUR_KIND_AMBUSH || ( n == sur.ambClient && sur.ambState >= 2 ) ) {
			p = sur.ambPower;
			if ( p < 0.8f ) {
				p = 0.8f;
			} else if ( p > 4.0f ) {
				p = 4.0f;
			}
			b += (int)( ( 140 + 8 * sur.floor ) * ds_miniBosses[sur.ambArch].health * sqrt( p ) );
		} else if ( sur.kind[n] == SUR_KIND_FODDER ) {
			b -= 50;
		}
	}
	if ( sur.mod && ds_modifiers[sur.mod].healthPct != 100 ) {
		b -= ( ent->client->pers.maxHealth + bonus + b ) * ( 100 - ds_modifiers[sur.mod].healthPct ) / 100;
	}
	return b;
}

float G_DS_SurpriseDamageScale( gentity_t *targ, gentity_t *attacker ) {
	float	s = 1.0f;
	int		a;

	if ( !sur.on || !attacker || !attacker->client || attacker == targ ) {
		return 1.0f;
	}
	a = attacker - g_entities;
	if ( a >= 0 && a < MAX_CLIENTS ) {
		if ( sur.kind[a] == SUR_KIND_AMBUSH ) {
			s *= ds_miniBosses[sur.ambArch].damage;
		} else if ( sur.kind[a] == SUR_KIND_FODDER ) {
			s *= 0.55f;
		}
	}
	// the floor modifier: the enemies' side
	if ( sur.mod && ds_modifiers[sur.mod].enemyDmgPct && ( attacker->r.svFlags & SVF_BOT ) && !G_RL_IsPlayer( attacker )
		&& targ && G_RL_IsPlayer( targ ) ) {
		s *= 1.0f + ds_modifiers[sur.mod].enemyDmgPct * 0.01f;
	}
	return s;
}

float G_DS_SurpriseSpeedScale( gentity_t *ent ) {
	float	s = 1.0f;
	int		n;

	if ( !sur.on || !ent || !ent->client ) {
		return 1.0f;
	}
	n = ent - g_entities;
	if ( n >= 0 && n < MAX_CLIENTS && sur.kind[n] == SUR_KIND_AMBUSH ) {
		s *= ds_miniBosses[sur.ambArch].speed;
	}
	if ( sur.mod && ds_modifiers[sur.mod].enemySpeedPct && ( ent->r.svFlags & SVF_BOT ) && !G_RL_IsPlayer( ent ) ) {
		s *= 1.0f + ds_modifiers[sur.mod].enemySpeedPct * 0.01f;
	}
	return s;
}

qboolean G_DS_SurpriseNoScore( gentity_t *self ) {
	return self && G_DS_SurpriseExtra( self - g_entities );
}

int G_DS_SurpriseOfferBonus( void ) {
	return sur.on && sur.mod ? ds_modifiers[sur.mod].cards : 0;
}

float G_DS_SurpriseGemXpMult( void ) {
	if ( !sur.on || !sur.mod || !ds_modifiers[sur.mod].gemXpPct ) {
		return 1.0f;
	}
	if ( !sur.loggedXp ) {
		sur.loggedXp = qtrue;
		G_LogPrintf( "DSSurprise: %s gem xp x%.2f (first gem collected)\n", ds_modifiers[sur.mod].name, 1.0f + ds_modifiers[sur.mod].gemXpPct * 0.01f );
	}
	return 1.0f + ds_modifiers[sur.mod].gemXpPct * 0.01f;
}

int G_DS_SurpriseGemAmount( int xp ) {
	if ( !sur.on || !sur.mod || !ds_modifiers[sur.mod].gemPct || xp <= 0 ) {
		return xp;
	}
	if ( !sur.loggedGems ) {
		sur.loggedGems = qtrue;
		G_LogPrintf( "DSSurprise: %s gems %i -> %i (first spray)\n", ds_modifiers[sur.mod].name, xp, xp + ( xp * ds_modifiers[sur.mod].gemPct + 50 ) / 100 );
	}
	return xp + ( xp * ds_modifiers[sur.mod].gemPct + 50 ) / 100;
}

int G_DS_SurpriseModSkulls( int floorSkulls ) {
	int		n;

	if ( !sur.on || !sur.mod || !ds_modifiers[sur.mod].skullPct || floorSkulls <= 0 ) {
		return 0;
	}
	n = ( floorSkulls * ds_modifiers[sur.mod].skullPct + 50 ) / 100;
	return n < 1 ? 1 : n;
}

void G_DS_SurpriseFloorEnd( qboolean won ) {
	int		i;

	if ( !sur.on ) {
		return;
	}
	if ( won && sur.mod ) {
		for ( i = 0 ; i < ds_modifiers[sur.mod].chests ; i++ ) {
			G_DS_BankChest( 1 );
		}
		Sur_RunAdd( "sur_modwon", 1 );
	}
	if ( sur.mod ) {
		G_LogPrintf( "DSSurprise: floor end %s with modifier %s (chests +%i, cards +%i)\n", won ? "WON" : "LOST",
			ds_modifiers[sur.mod].name, won ? ds_modifiers[sur.mod].chests : 0, ds_modifiers[sur.mod].cards );
	}
}

/*
===============================================================================

THE MINI-BOSS'S ATTACKS (the act bosses' telegraphs, RM_ENT_BOSSFX)

===============================================================================
*/

static gentity_t *Sur_Mb( void ) {
	gentity_t	*e;

	if ( sur.ambState != 3 || sur.ambClient < 0 ) {
		return NULL;
	}
	e = &g_entities[sur.ambClient];
	return Sur_Alive( e ) ? e : NULL;
}

static gentity_t *Sur_Fx( int kind, const vec3_t from, const vec3_t to, int land, float radius, int linger ) {
	gentity_t	*ent;

	ent = G_Spawn();
	ent->classname = "ds_mbfx";
	ent->s.eType = ET_GENERAL;
	ent->s.generic1 = RM_ENT_BOSSFX;
	ent->s.modelindex2 = kind;
	ent->s.time = level.time;
	ent->s.time2 = land;
	ent->s.otherEntityNum = sur.ambClient >= 0 ? sur.ambClient : ENTITYNUM_NONE;
	ent->s.otherEntityNum2 = radius < 0 ? 0 : radius > 1023 ? 1023 : (int)radius;
	ent->s.frame = 0;
	ent->r.svFlags |= SVF_BROADCAST;
	VectorCopy( to, ent->s.origin2 );
	G_SetOrigin( ent, (float *)from );
	ent->think = G_FreeEntity;
	ent->nextthink = land + linger;
	trap_LinkEntity( ent );
	return ent;
}

static void Sur_FxMove( gentity_t *fx, const vec3_t from, const vec3_t to ) {
	if ( !fx || !fx->inuse ) {
		return;
	}
	VectorCopy( to, fx->s.origin2 );
	G_SetOrigin( fx, (float *)from );
	trap_LinkEntity( fx );
}

static void Sur_FxFree( gentity_t **fx ) {
	if ( *fx && ( *fx )->inuse && !Q_stricmp( ( *fx )->classname, "ds_mbfx" ) ) {
		G_FreeEntity( *fx );
	}
	*fx = NULL;
}

static int Sur_Dmg( int base ) {
	int		d = (int)( base * 0.8f + 0.5f );		// (G_RL_DamageScale adds its damage side)

	return d < 1 ? 1 : d;
}

static int Sur_Tele( int msec ) {
	float	k = 1.0f + 0.6f * ( ds_miniBosses[sur.ambArch].speed - 1.0f );
	int		t = (int)( msec / ( k < 0.5f ? 0.5f : k ) );

	return t < 800 ? 800 : t;
}

static void Sur_Eye( gentity_t *ent, vec3_t out ) {
	VectorCopy( ent->client->ps.origin, out );
	out[2] += ent->client->ps.viewheight;
}

static void Sur_ChestPt( gentity_t *ent, vec3_t out ) {
	VectorCopy( ent->r.currentOrigin, out );
	out[2] += 8;
}

static void Sur_Ground( const vec3_t p, vec3_t out ) {
	trace_t	tr;
	vec3_t	down;

	VectorCopy( p, down );
	down[2] -= 600;
	trap_Trace( &tr, p, NULL, NULL, down, ENTITYNUM_NONE, MASK_SOLID );
	VectorCopy( tr.endpos, out );
	out[2] += 2;
}

static qboolean Sur_FloorSpot( const vec3_t near, float minR, float maxR, vec3_t out ) {
	trace_t	tr;
	vec3_t	from, to, down;
	float	ang, r;
	int		tries;

	VectorCopy( near, from );
	from[2] += 24;
	for ( tries = 0 ; tries < 12 ; tries++ ) {
		ang = random() * 2 * M_PI;
		r = minR + random() * ( maxR - minR );
		to[0] = from[0] + cos( ang ) * r;
		to[1] = from[1] + sin( ang ) * r;
		to[2] = from[2];
		trap_Trace( &tr, from, NULL, NULL, to, ENTITYNUM_NONE, MASK_SOLID | CONTENTS_PLAYERCLIP );
		if ( tr.startsolid ) {
			continue;
		}
		VectorCopy( tr.endpos, to );
		if ( tr.fraction < 1.0f ) {
			VectorMA( tr.endpos, 20, tr.plane.normal, to );
		}
		VectorCopy( to, down );
		down[2] -= 400;
		trap_Trace( &tr, to, NULL, NULL, down, ENTITYNUM_NONE, MASK_SOLID | CONTENTS_PLAYERCLIP );
		if ( tr.startsolid || tr.fraction == 1.0f || tr.plane.normal[2] < 0.6f || ( tr.contents & ( CONTENTS_LAVA | CONTENTS_SLIME ) ) ) {
			continue;
		}
		VectorCopy( tr.endpos, out );
		out[2] += 2;
		return qtrue;
	}
	return qfalse;
}

static void Sur_Knock( gentity_t *ent, const vec3_t dir, float speed, float up ) {
	ent->client->ps.velocity[0] += dir[0] * speed;
	ent->client->ps.velocity[1] += dir[1] * speed;
	ent->client->ps.velocity[2] += up;
	ent->client->ps.pm_flags |= PMF_TIME_KNOCKBACK;
	ent->client->ps.pm_time = 250;
}

static qboolean Sur_OnGround( gentity_t *ent ) {
	trace_t	tr;
	vec3_t	down;

	if ( ent->client->ps.groundEntityNum != ENTITYNUM_NONE ) {
		return qtrue;
	}
	VectorCopy( ent->r.currentOrigin, down );
	down[2] -= 44;
	trap_Trace( &tr, ent->r.currentOrigin, ent->r.mins, ent->r.maxs, down, ent->s.number, MASK_PLAYERSOLID );
	return tr.fraction < 1.0f && !tr.startsolid;
}

// who the mini-boss's attacks can hurt: anyone alive but itself
static qboolean Sur_Victim( gentity_t *ent ) {
	return Sur_Alive( ent ) && ( ent - g_entities ) != sur.ambClient;
}

static qboolean Sur_Visible( gentity_t *from, gentity_t *to ) {
	trace_t	tr;
	vec3_t	a, b;

	Sur_Eye( from, a );
	Sur_ChestPt( to, b );
	trap_Trace( &tr, a, NULL, NULL, b, from->s.number, MASK_SOLID );
	return tr.fraction >= 1.0f;
}

// the nearest fighter in sight (the player counts a little nearer), -1 none
static int Sur_PickTarget( gentity_t *mb ) {
	gentity_t	*e;
	float		d, best = 1e9f;
	int			i, pick = -1;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		e = &g_entities[i];
		if ( !Sur_Victim( e ) ) {
			continue;
		}
		d = Distance( e->r.currentOrigin, mb->r.currentOrigin );
		if ( d > 1600 || !Sur_Visible( mb, e ) ) {
			continue;
		}
		if ( G_RL_IsPlayer( e ) ) {
			d *= 0.8f;
		}
		if ( d < best ) {
			best = d;
			pick = i;
		}
	}
	return pick;
}

static qboolean Sur_CanStart( int atk, float dist ) {
	switch ( atk ) {
	case DSB_HOOK:		return dist > 220 && dist < 1100;
	case DSB_CLEAVER:	return dist > 120 && dist < 700;
	case DSB_SLAM:		return dist < 600;
	case DSB_BARRAGE:	return dist < 1500;
	case DSB_LANCE:		return dist < 1600;
	case DSB_METEOR:	return dist < 1500;
	default:			return qfalse;
	}
}

static void Sur_AddStrike( int kind, const vec3_t org, int land, int damage, float radius ) {
	surStrike_t	*s;
	int			i;

	for ( i = 0 ; i < SUR_MAX_STRIKES ; i++ ) {
		s = &sur.strikes[i];
		if ( !s->kind ) {
			s->kind = kind;
			s->land = land;
			VectorCopy( org, s->org );
			s->damage = damage;
			s->radius = radius;
			s->fx = Sur_Fx( kind, org, org, land, radius, 350 );
			return;
		}
	}
}

static void Sur_StartAttack( int atk, gentity_t *mb, gentity_t *tg ) {
	vec3_t		eye, chest, dir, spot, end, from, ang, fwd;
	trace_t		tr;
	int			i, n;

	sur.atk = atk;
	sur.atkStart = level.time;
	sur.atkStage = 0;
	sur.atkHitMask = 0;
	sur.atkTarget = tg - g_entities;
	Sur_Eye( mb, eye );
	Sur_ChestPt( tg, chest );
	VectorSubtract( chest, eye, dir );
	VectorNormalize( dir );
	VectorCopy( dir, sur.atkDir );
	VectorCopy( eye, sur.atkFrom );
	VectorCopy( chest, sur.atkTo );
	sur.holdFireUntil = level.time + 900;
	for ( i = 0 ; i < 6 ; i++ ) {
		sur.atkFx[i] = NULL;
	}

	switch ( atk ) {
	case DSB_HOOK:
		sur.atkLand = level.time + Sur_Tele( 1000 );
		sur.atkEnd = sur.atkLand + 300;
		sur.rootUntil = sur.atkLand;
		sur.atkFx[0] = Sur_Fx( atk, eye, chest, sur.atkLand, 12, 300 );
		break;
	case DSB_CLEAVER:
		sur.atkLand = level.time + Sur_Tele( 1000 );
		sur.atkEnd = sur.atkLand + 450;
		sur.rootUntil = sur.atkEnd;
		sur.holdFireUntil = sur.atkEnd;
		VectorCopy( mb->r.currentOrigin, from );
		dir[2] = 0;
		VectorNormalize( dir );
		VectorCopy( dir, sur.atkDir );
		VectorMA( from, 600, dir, end );
		sur.atkFx[0] = Sur_Fx( atk, from, end, sur.atkLand, 56, 450 );
		break;
	case DSB_SLAM:
		sur.atkLand = level.time + Sur_Tele( 1150 );
		sur.atkEnd = sur.atkLand + 400;
		sur.rootUntil = sur.atkLand;
		sur.holdFireUntil = sur.atkLand;
		VectorSubtract( tg->r.currentOrigin, mb->r.currentOrigin, dir );
		dir[2] = 0;
		n = (int)VectorNormalize( dir );
		n = (int)( n * 0.6f > 400 ? 400 : n * 0.6f );
		mb->client->ps.velocity[0] = dir[0] * n;
		mb->client->ps.velocity[1] = dir[1] * n;
		mb->client->ps.velocity[2] = 440;
		mb->client->ps.groundEntityNum = ENTITYNUM_NONE;
		VectorMA( mb->r.currentOrigin, n * 1.1f, dir, spot );
		Sur_Ground( spot, spot );
		VectorCopy( spot, sur.atkTo );
		sur.atkFx[0] = Sur_Fx( atk, spot, spot, sur.atkLand, 300, 400 );
		break;
	case DSB_BARRAGE:
		sur.atkLand = level.time + 800;
		sur.atkEnd = sur.atkLand + 1100;
		sur.rootUntil = sur.atkLand;
		for ( i = 0 ; i < 4 ; i++ ) {
			if ( i == 0 ) {
				VectorMA( tg->r.currentOrigin, 0.5f, tg->client->ps.velocity, spot );
				spot[2] = tg->r.currentOrigin[2];
				Sur_Ground( spot, spot );
			} else if ( !Sur_FloorSpot( tg->r.currentOrigin, 90, 280, spot ) ) {
				continue;
			}
			sur.atkFx[i] = Sur_Fx( atk, spot, spot, sur.atkLand + 1000, 120, 150 );
		}
		break;
	case DSB_LANCE:
		n = 3;
		sur.atkLand = level.time + Sur_Tele( 1100 );
		sur.atkEnd = sur.atkLand + 200;
		sur.rootUntil = sur.atkLand;
		sur.holdFireUntil = sur.atkEnd;
		vectoangles( sur.atkDir, ang );
		for ( i = 0 ; i < n ; i++ ) {
			VectorSet( spot, ang[PITCH], ang[YAW] + ( i - ( n - 1 ) * 0.5f ) * 11.0f, 0 );
			AngleVectors( spot, fwd, NULL, NULL );
			VectorMA( eye, 2400, fwd, end );
			trap_Trace( &tr, eye, NULL, NULL, end, mb->s.number, MASK_SOLID );
			sur.atkFx[i] = Sur_Fx( atk, eye, tr.endpos, sur.atkLand, 14, 200 );
		}
		break;
	case DSB_METEOR:
		n = 5;
		sur.atkLand = level.time + 300;
		sur.atkEnd = level.time + 1300 + n * 230;
		sur.rootUntil = sur.atkLand + 500;
		sur.holdFireUntil = sur.rootUntil;
		for ( i = 0 ; i < n ; i++ ) {
			if ( i == 0 ) {
				Sur_Ground( tg->r.currentOrigin, spot );
			} else if ( !Sur_FloorSpot( tg->r.currentOrigin, 80, 360, spot ) ) {
				continue;
			}
			Sur_AddStrike( DSB_METEOR, spot, level.time + Sur_Tele( 1150 ) + i * 230, 40, 160 );
		}
		break;
	default:
		sur.atk = 0;
		return;
	}
	G_LogPrintf( "DSSurprise: mini-boss %s attack %s telegraph at %s\n", ds_miniBosses[sur.ambArch].name, ds_bossAttackNames[atk],
		tg->client->pers.netname );
}

static void Sur_FireLance( gentity_t *mb, const vec3_t from, const vec3_t to, int damage ) {
	gentity_t	*te, *hit;
	trace_t		tr;
	vec3_t		mins = { -14, -14, -14 }, maxs = { 14, 14, 14 }, dir;

	trap_Trace( &tr, from, mins, maxs, to, mb->s.number, MASK_SHOT );
	VectorSubtract( to, from, dir );
	VectorNormalize( dir );
	if ( tr.entityNum < MAX_CLIENTS ) {
		hit = &g_entities[tr.entityNum];
		if ( Sur_Victim( hit ) ) {
			G_Damage( hit, mb, mb, dir, tr.endpos, Sur_Dmg( damage ), 0, MOD_RAILGUN );
		}
	}
	te = G_TempEntity( (float *)to, EV_RAILTRAIL );
	te->s.clientNum = mb->s.number;
	VectorCopy( from, te->s.origin2 );
	te->s.eventParm = 255;
	te->r.svFlags |= SVF_BROADCAST;
}

static void Sur_RunAttack( gentity_t *mb ) {
	gentity_t	*ent, *tg;
	vec3_t		eye, chest, d, end, spot;
	trace_t		tr;
	vec3_t		mins = { -10, -10, -10 }, maxs = { 10, 10, 10 };
	float		dist;
	int			i;

	tg = sur.atkTarget >= 0 && sur.atkTarget < MAX_CLIENTS ? &g_entities[sur.atkTarget] : NULL;
	Sur_Eye( mb, eye );
	switch ( sur.atk ) {
	case DSB_HOOK:
		if ( sur.atkStage == 0 ) {
			if ( level.time < sur.atkLand - 450 && Sur_Victim( tg ) ) {
				Sur_ChestPt( tg, chest );
				VectorSubtract( chest, eye, sur.atkDir );
				VectorNormalize( sur.atkDir );
				VectorCopy( chest, sur.atkTo );
			}
			VectorMA( eye, 1100, sur.atkDir, end );
			Sur_FxMove( sur.atkFx[0], eye, level.time < sur.atkLand - 450 ? sur.atkTo : end );
			if ( level.time >= sur.atkLand ) {
				sur.atkStage = 1;
				if ( sur.atkFx[0] ) {
					sur.atkFx[0]->s.frame = 1;
				}
				trap_Trace( &tr, eye, mins, maxs, end, mb->s.number, MASK_SHOT );
				Sur_FxMove( sur.atkFx[0], eye, tr.endpos );
				if ( tr.entityNum < MAX_CLIENTS && Sur_Victim( &g_entities[tr.entityNum] ) ) {
					ent = &g_entities[tr.entityNum];
					VectorSubtract( mb->r.currentOrigin, ent->r.currentOrigin, d );
					dist = VectorNormalize( d );
					G_Damage( ent, mb, mb, NULL, tr.endpos, Sur_Dmg( 12 ), DAMAGE_NO_KNOCKBACK, MOD_GRAPPLE );
					VectorClear( ent->client->ps.velocity );
					Sur_Knock( ent, d, dist * 1.6f > 1100 ? 1100 : dist * 1.6f, 240 );
					ent->client->ps.pm_time = 450;
					G_LogPrintf( "DSSurprise: mini-boss hook hit %s\n", ent->client->pers.netname );
				}
			}
		}
		break;

	case DSB_CLEAVER:
		if ( sur.atkStage == 0 ) {
			if ( level.time < sur.atkLand - 400 && Sur_Victim( tg ) ) {
				VectorSubtract( tg->r.currentOrigin, mb->r.currentOrigin, sur.atkDir );
				sur.atkDir[2] = 0;
				VectorNormalize( sur.atkDir );
				VectorMA( mb->r.currentOrigin, 600, sur.atkDir, end );
				Sur_FxMove( sur.atkFx[0], mb->r.currentOrigin, end );
			}
			if ( level.time >= sur.atkLand ) {
				sur.atkStage = 1;
				if ( sur.atkFx[0] ) {
					sur.atkFx[0]->s.frame = 1;
				}
			}
		} else if ( level.time < sur.atkEnd ) {
			mb->client->ps.velocity[0] = sur.atkDir[0] * 1100;
			mb->client->ps.velocity[1] = sur.atkDir[1] * 1100;
			for ( i = 0 ; i < level.maxclients ; i++ ) {
				ent = &g_entities[i];
				if ( !Sur_Victim( ent ) || ( sur.atkHitMask & ( 1 << ( i & 31 ) ) ) ) {
					continue;
				}
				VectorSubtract( ent->r.currentOrigin, mb->r.currentOrigin, d );
				if ( fabs( d[2] ) > 70 || d[0] * d[0] + d[1] * d[1] > 80 * 80 ) {
					continue;
				}
				sur.atkHitMask |= 1 << ( i & 31 );
				G_Damage( ent, mb, mb, sur.atkDir, ent->r.currentOrigin, Sur_Dmg( 40 ), DAMAGE_NO_KNOCKBACK, MOD_GAUNTLET );
				if ( ent->client ) {
					Sur_Knock( ent, sur.atkDir, 600, 280 );
				}
				G_LogPrintf( "DSSurprise: mini-boss cleaver hit %s\n", ent->client->pers.netname );
			}
		}
		break;

	case DSB_SLAM:
		if ( sur.atkStage == 0 ) {
			Sur_Ground( mb->r.currentOrigin, spot );
			Sur_FxMove( sur.atkFx[0], spot, spot );
			if ( level.time >= sur.atkLand || ( level.time > sur.atkStart + 500 && mb->client->ps.groundEntityNum != ENTITYNUM_NONE ) ) {
				sur.atkStage = 1;
				sur.atkEnd = level.time + 400;
				if ( sur.atkFx[0] ) {
					sur.atkFx[0]->s.frame = 1;
					sur.atkFx[0]->s.time2 = level.time;
					sur.atkFx[0]->nextthink = level.time + 450;
				}
				for ( i = 0 ; i < level.maxclients ; i++ ) {
					ent = &g_entities[i];
					if ( !Sur_Victim( ent ) ) {
						continue;
					}
					VectorSubtract( ent->r.currentOrigin, spot, d );
					dist = sqrt( d[0] * d[0] + d[1] * d[1] );
					if ( dist > 300 || fabs( d[2] ) > 120 || !Sur_OnGround( ent ) ) {
						continue;		// jumped it
					}
					d[2] = 0;
					VectorNormalize( d );
					G_Damage( ent, mb, mb, d, ent->r.currentOrigin, Sur_Dmg( 10 + (int)( 26 * ( 1.0f - dist / 300 ) ) ), DAMAGE_NO_KNOCKBACK, MOD_SLAM );
					Sur_Knock( ent, d, 300, 360 );
				}
				G_AddEvent( mb, EV_FALL_FAR, 0 );
				G_LogPrintf( "DSSurprise: mini-boss slam lands\n" );
			}
		}
		break;

	case DSB_BARRAGE:
		if ( sur.atkStage == 0 && level.time >= sur.atkLand ) {
			sur.atkStage = 1;
			for ( i = 0 ; i < 4 ; i++ ) {
				if ( !sur.atkFx[i] || !sur.atkFx[i]->inuse ) {
					continue;
				}
				VectorCopy( eye, spot );
				spot[2] += 24;
				VectorSubtract( sur.atkFx[i]->r.currentOrigin, spot, d );
				VectorCopy( d, end );
				ent = fire_rocket( mb, spot, end );
				if ( !ent ) {
					continue;
				}
				ent->s.pos.trType = TR_GRAVITY;
				ent->s.pos.trTime = level.time;
				VectorCopy( spot, ent->s.pos.trBase );
				VectorCopy( d, ent->s.pos.trDelta );
				ent->s.pos.trDelta[2] += 0.5f * DEFAULT_GRAVITY * 1.0f;
				ent->damage = Sur_Dmg( 45 );
				ent->splashDamage = Sur_Dmg( 40 );
				ent->splashRadius = 130;
				ent->nextthink = level.time + 4000;
				VectorCopy( spot, ent->r.currentOrigin );
			}
			G_LogPrintf( "DSSurprise: mini-boss barrage launches\n" );
		}
		break;

	case DSB_LANCE:
		if ( sur.atkStage == 0 && level.time >= sur.atkLand ) {
			sur.atkStage = 1;
			for ( i = 0 ; i < 6 ; i++ ) {
				if ( sur.atkFx[i] && sur.atkFx[i]->inuse ) {
					sur.atkFx[i]->s.frame = 1;
					Sur_FireLance( mb, sur.atkFx[i]->r.currentOrigin, sur.atkFx[i]->s.origin2, 34 );
				}
			}
			G_LogPrintf( "DSSurprise: mini-boss lances fire\n" );
		}
		break;

	default:
		break;
	}
	if ( level.time >= sur.atkEnd && ( sur.atkStage > 0 || sur.atk == DSB_METEOR ) ) {
		Sur_EndAttack();
	}
}

static void Sur_EndAttack( void ) {
	int		i;

	for ( i = 0 ; i < 6 ; i++ ) {
		Sur_FxFree( &sur.atkFx[i] );
	}
	if ( sur.atk ) {
		sur.nextAtk = level.time + (int)( ( 4500 + rand() % 2500 ) / ds_miniBosses[sur.ambArch].speed );
	}
	sur.atk = 0;
	sur.atkStage = 0;
	sur.rootUntil = 0;
	sur.holdFireUntil = 0;
}

static void Sur_RunStrikes( void ) {
	surStrike_t	*s;
	gentity_t	*att, *te;
	vec3_t		up = { 0, 0, 1 };
	int			i;

	att = Sur_Mb();
	for ( i = 0 ; i < SUR_MAX_STRIKES ; i++ ) {
		s = &sur.strikes[i];
		if ( !s->kind || level.time < s->land ) {
			continue;
		}
		te = G_TempEntity( s->org, EV_MISSILE_MISS );
		te->s.weapon = WP_ROCKET_LAUNCHER;
		te->s.eventParm = DirToByte( up );
		te->s.generic1 = RM_MISSILE_METEOR;
		te->r.svFlags |= SVF_BROADCAST;
		G_RadiusDamage( s->org, att ? att : &g_entities[ENTITYNUM_WORLD], Sur_Dmg( s->damage ), s->radius, NULL, MOD_METEOR );
		if ( s->fx && s->fx->inuse ) {
			s->fx->s.frame = 1;
		}
		s->fx = NULL;
		s->kind = 0;
	}
}

static void Sur_MiniBossFrame( void ) {
	gentity_t	*mb = Sur_Mb(), *tg;
	int			i, k, a, pct;
	float		d;

	if ( !mb ) {
		if ( sur.atk ) {
			Sur_EndAttack();
		}
		return;
	}
	// its health for the edge line
	pct = mb->client->ps.stats[STAT_MAX_HEALTH] > 0 ? 100 * mb->health / mb->client->ps.stats[STAT_MAX_HEALTH] : 0;
	if ( pct > 100 ) {
		pct = 100;
	} else if ( pct < 0 ) {
		pct = 0;
	}
	if ( abs( pct - sur.ambHpPct ) >= 4 ) {
		sur.ambHpPct = pct;
		sur.csDirty = 1;
	}
	if ( !sur.mbLogged ) {
		sur.mbLogged = qtrue;
		G_LogPrintf( "DSSurprise: mini-boss %s health %i / max %i (power %.2f, floor %i), armor %i, speed x%.2f, damage x%.2f\n",
			ds_miniBosses[sur.ambArch].name, mb->health, mb->client->ps.stats[STAT_MAX_HEALTH], sur.ambPower, sur.floor,
			mb->client->ps.stats[STAT_ARMOR], ds_miniBosses[sur.ambArch].speed, ds_miniBosses[sur.ambArch].damage );
	}
	if ( sur.atk ) {
		Sur_RunAttack( mb );
		return;
	}
	if ( level.time < sur.nextAtk ) {
		return;
	}
	i = Sur_PickTarget( mb );
	if ( i < 0 ) {
		sur.nextAtk = level.time + 700;
		return;
	}
	tg = &g_entities[i];
	d = Distance( tg->r.currentOrigin, mb->r.currentOrigin );
	// one of its two attacks whose cooldown is over and that works from here
	k = rand() & 1;
	for ( a = 0 ; a < 2 ; a++, k ^= 1 ) {
		if ( ds_miniBosses[sur.ambArch].attacks[k] == DSB_NONE || level.time < sur.cooldown[k] ) {
			continue;
		}
		if ( !Sur_CanStart( ds_miniBosses[sur.ambArch].attacks[k], d ) ) {
			continue;
		}
		sur.cooldown[k] = level.time + (int)( 8000 / ds_miniBosses[sur.ambArch].speed );
		Sur_StartAttack( ds_miniBosses[sur.ambArch].attacks[k], mb, tg );
		if ( !sur.shotAttack ) {
			sur.shotAttack = qtrue;
			Sur_Shot( 450, SUR_LOOK_AMBUSH );
		}
		return;
	}
	sur.nextAtk = level.time + 800;
}

/*
===============================================================================

WILD EVENTS

===============================================================================
*/

static void Sur_EventEnd( void ) {
	int		i;

	if ( !sur.ev ) {
		return;
	}
	Sur_Callout( DSUC_EVENT_END, sur.ev, sur.ev == DSEV_UPRISING ? sur.fodderSlain : sur.evKills );
	G_LogPrintf( "DSEvent: %s over (kills %i, fodder spawned %i slain by the player %i)\n", ds_wildEventNames[sur.ev], sur.evKills,
		sur.fodderSpawned, sur.fodderSlain );
	if ( sur.ev == DSEV_UPRISING ) {
		// the rest crumble away
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			if ( sur.kind[i] == SUR_KIND_FODDER && g_entities[i].inuse ) {
				Sur_KickSoon( i );
				sur.kickAt[i] = level.time + 200 + rand() % 600;
			}
		}
		for ( i = 0 ; i < SUR_MAX_PENDING ; i++ ) {
			if ( sur.pend[i].used && sur.pend[i].kind == SUR_KIND_FODDER && !sur.pend[i].added ) {
				sur.pend[i].used = qfalse;
			}
		}
	}
	if ( sur.ev == DSEV_BOUNTY && sur.bountyClient >= 0 ) {
		G_DS_LootBeamOff( &g_entities[sur.bountyClient] );
	}
	sur.ev = 0;
	sur.evEnd = level.time;
	sur.bountyClient = -1;
	sur.csDirty = 1;
}

// a floor spot with room above it, 700-2200 from the player (the gold has to be raced for)
static qboolean Sur_GoldSpot( vec3_t ground, vec3_t sky ) {
	trace_t	tr;
	vec3_t	up, side;
	int		tries;
	float	yaw;

	for ( tries = 0 ; tries < 3 ; tries++ ) {
		if ( !Sur_FarSpot( tries == 0 ? 800 : DS_SURPRISE_MIN_DIST + 100, tries < 2 ? 2400 : 0, ground, &yaw ) ) {
			continue;
		}
		Sur_Ground( ground, ground );
		VectorCopy( ground, up );
		up[2] += 2400;
		trap_Trace( &tr, ground, NULL, NULL, up, ENTITYNUM_NONE, MASK_SOLID );
		VectorCopy( tr.endpos, sky );
		sky[2] -= 16;
		if ( sky[2] - ground[2] < 160 ) {
			continue;
		}
		// in at an angle when there is room
		side[0] = crandom();
		side[1] = crandom();
		side[2] = 0;
		VectorNormalize( side );
		VectorMA( sky, ( sky[2] - ground[2] ) * 0.35f, side, up );
		trap_Trace( &tr, ground, NULL, NULL, up, ENTITYNUM_NONE, MASK_SOLID );
		if ( tr.fraction >= 0.95f ) {
			VectorCopy( up, sky );
		}
		return qtrue;
	}
	return qfalse;
}

static void Sur_StartEvent( int ev ) {
	gentity_t	*e, *best = NULL;
	vec3_t		sky;
	int			i, top = -9999, h = G_RL_HumanClient();

	if ( sur.ev ) {
		Sur_EventEnd();
	}
	switch ( ev ) {
	case DSEV_GOLDMETEOR:
		if ( !Sur_GoldSpot( sur.goldSpot, sky ) ) {
			G_LogPrintf( "DSEvent: GOLD METEOR called off (no spot)\n" );
			return;
		}
		sur.goldImpact = level.time + SUR_GOLD_FALL;
		sur.goldChest = NULL;
		sur.goldRaceEnd = 0;
		e = G_Spawn();
		e->classname = "ds_goldmeteor";
		e->s.eType = ET_GENERAL;
		e->s.generic1 = RM_ENT_SURPRISE;
		e->s.modelindex2 = DSX2_GOLDMETEOR;
		e->s.time = level.time;
		e->s.time2 = sur.goldImpact;
		e->s.frame = 0;
		e->r.svFlags |= SVF_BROADCAST;
		VectorCopy( sky, e->s.origin2 );
		G_SetOrigin( e, sur.goldSpot );
		e->think = G_FreeEntity;
		e->nextthink = sur.goldImpact + 25000;
		trap_LinkEntity( e );
		sur.evFx = e;
		sur.evStart = level.time;
		sur.evEnd = sur.goldImpact + SUR_GOLD_RACE;
		sur.evC = sur.goldImpact;
		G_LogPrintf( "DSEvent: GOLD METEOR falls at %i %i %i, %.0f from the player, impact in %i ms\n", (int)sur.goldSpot[0],
			(int)sur.goldSpot[1], (int)sur.goldSpot[2], Sur_PlayerDist( sur.goldSpot ), SUR_GOLD_FALL );
		break;
	case DSEV_UPRISING:
		if ( Sur_FreeSlots() < 2 ) {
			G_LogPrintf( "DSEvent: BOT UPRISING called off (%i free slots)\n", Sur_FreeSlots() );
			return;
		}
		sur.evStart = level.time;
		sur.evEnd = level.time + DS_UPRISING_TIME;
		for ( i = 0 ; i < SUR_MAX_FODDER ; i++ ) {
			sur.fodderNext[i] = level.time + 600 + i * 450;
			sur.fodderClient[i] = -1;
		}
		sur.fodderSpawned = sur.fodderSlain = 0;
		G_LogPrintf( "DSEvent: BOT UPRISING for %i s (%i free slots)\n", DS_UPRISING_TIME / 1000, Sur_FreeSlots() );
		break;
	case DSEV_ECLIPSE:
		sur.evStart = level.time;
		sur.evEnd = level.time + DS_ECLIPSE_TIME;
		G_LogPrintf( "DSEvent: BLOOD ECLIPSE for %i s\n", DS_ECLIPSE_TIME / 1000 );
		break;
	case DSEV_BOUNTY:
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			e = &g_entities[i];
			if ( i == h || !Sur_Alive( e ) || !( e->r.svFlags & SVF_BOT ) || sur.kind[i] || i == G_DS_BossClient() ) {
				continue;
			}
			if ( e->client->ps.persistant[PERS_SCORE] > top ) {
				top = e->client->ps.persistant[PERS_SCORE];
				best = e;
			}
		}
		if ( !best ) {
			G_LogPrintf( "DSEvent: BOUNTY called off (no bot)\n" );
			return;
		}
		sur.bountyClient = best - g_entities;
		G_DS_LootBeam( best, DS_BEAM_EPIC );
		sur.evStart = level.time;
		sur.evEnd = level.time + DS_BOUNTY_TIME;
		sur.evC = sur.bountyClient;
		G_LogPrintf( "DSEvent: BOUNTY on %s (score %i) for %i s\n", best->client->pers.netname, top, DS_BOUNTY_TIME / 1000 );
		break;
	default:
		return;
	}
	sur.ev = ev;
	sur.evKills = 0;
	Sur_RunAdd( "sur_events", 1 );
	Sur_Callout( DSUC_EVENT_START, ev, ev == DSEV_BOUNTY ? sur.bountyClient : 0 );
	Sur_Shot( ev == DSEV_GOLDMETEOR ? 5300 : ev == DSEV_UPRISING ? 7000 : 1500,
		ev == DSEV_GOLDMETEOR ? SUR_LOOK_GOLD : ev == DSEV_UPRISING ? SUR_LOOK_FODDER : ev == DSEV_BOUNTY ? SUR_LOOK_BOUNTY : SUR_LOOK_NONE );
	if ( ev == DSEV_UPRISING ) {
		Sur_Shot( 15000, SUR_LOOK_FODDER );
	}
	sur.csDirty = 1;
}

static void Sur_GoldImpact( void ) {
	gentity_t	*te;
	vec3_t		up = { 0, 0, 1 }, org;
	int			xp = 70 + 8 * sur.floor;

	te = G_TempEntity( sur.goldSpot, EV_MISSILE_MISS );
	te->s.weapon = WP_ROCKET_LAUNCHER;
	te->s.eventParm = DirToByte( up );
	te->s.generic1 = RM_MISSILE_METEOR;
	te->r.svFlags |= SVF_BROADCAST;
	G_RadiusDamage( sur.goldSpot, &g_entities[ENTITYNUM_WORLD], 60, 260, NULL, MOD_METEOR );
	G_EarthTremor( sur.goldSpot, NULL, 420, 360 );
	if ( sur.evFx && sur.evFx->inuse ) {
		sur.evFx->s.frame = 1;		// the crater glows on
	}
	VectorCopy( sur.goldSpot, org );
	G_DS_SpawnGems( org, xp, -1 );
	sur.goldChest = G_DS_SpawnChest( org, 1 );
	G_DS_BeamAt( org, DS_BEAM_LEGENDARY, SUR_GOLD_RACE + 8000 );
	sur.goldRaceEnd = level.time + SUR_GOLD_RACE;
	Sur_Shot( 900, SUR_LOOK_GOLD );
	Sur_Shot( 3500, SUR_LOOK_GOLD );
	G_LogPrintf( "DSEvent: GOLD METEOR impact: gems %i, chest, the race is on (player %.0f away)\n", xp, Sur_PlayerDist( sur.goldSpot ) );
}

static void Sur_EventFrame( void ) {
	gentity_t	*e;
	vec3_t		spot;
	float		yaw;
	int			i, j, n, alive;

	if ( !sur.ev ) {
		return;
	}
	switch ( sur.ev ) {
	case DSEV_GOLDMETEOR:
		if ( sur.goldImpact && level.time >= sur.goldImpact ) {
			sur.goldImpact = 0;
			Sur_GoldImpact();
		}
		// a bot that reaches the chest first takes it
		if ( sur.goldChest ) {
			if ( !sur.goldChest->inuse || Q_stricmp( sur.goldChest->classname, "ds_chest" ) ) {
				sur.goldChest = NULL;		// the player took it
				G_LogPrintf( "DSEvent: GOLD METEOR chest taken by the player\n" );
			} else {
				for ( i = 0 ; i < level.maxclients ; i++ ) {
					e = &g_entities[i];
					if ( !Sur_Alive( e ) || !( e->r.svFlags & SVF_BOT ) || G_RL_IsPlayer( e ) ) {
						continue;
					}
					if ( Distance( e->r.currentOrigin, sur.goldChest->r.currentOrigin ) < 52 ) {
						G_TempEntity( sur.goldChest->r.currentOrigin, EV_PLAYER_TELEPORT_OUT );
						G_FreeEntity( sur.goldChest );
						sur.goldChest = NULL;
						Sur_Callout( DSUC_GOLD_SNATCHED, i, 0 );
						G_LogPrintf( "DSEvent: GOLD METEOR chest snatched by %s\n", e->client->pers.netname );
						break;
					}
				}
			}
		}
		if ( level.time >= sur.evEnd ) {
			Sur_EventEnd();
		}
		break;

	case DSEV_UPRISING:
		if ( level.time >= sur.evEnd ) {
			Sur_EventEnd();
			break;
		}
		// keep the slots full: a portal, then a body
		for ( i = 0 ; i < SUR_MAX_FODDER ; i++ ) {
			if ( sur.fodderClient[i] >= 0 ) {
				e = &g_entities[sur.fodderClient[i]];
				if ( e->inuse && e->client && e->client->pers.connected != CON_DISCONNECTED && sur.kind[sur.fodderClient[i]] == SUR_KIND_FODDER ) {
					continue;		// still in (alive or about to leave)
				}
				sur.fodderClient[i] = -1;
				sur.fodderNext[i] = level.time + 1200 + rand() % 1500;
			}
			if ( !sur.fodderNext[i] || level.time < sur.fodderNext[i] || level.time > sur.evEnd - 2500 ) {
				continue;
			}
			// already promised?
			for ( j = 0, n = 0 ; j < SUR_MAX_PENDING ; j++ ) {
				if ( sur.pend[j].used && !Q_stricmp( sur.pend[j].name, surFodderNames[i] ) ) {
					n = 1;
				}
			}
			if ( n ) {
				continue;
			}
			for ( j = 0, alive = 0 ; j < level.maxclients ; j++ ) {
				if ( level.clients[j].pers.connected == CON_DISCONNECTED ) {
					alive++;
				}
			}
			if ( Sur_FreeSlots() < 2 ) {		// one slot stays free for an ambush / ghost
				sur.fodderNext[i] = level.time + 1500;
				continue;
			}
			if ( !Sur_FarSpot( 800, 0, spot, &yaw ) && !Sur_FarSpot( DS_SURPRISE_MIN_DIST, 0, spot, &yaw ) ) {
				sur.fodderNext[i] = level.time + 1000;
				continue;
			}
			sur.fodderNext[i] = 0;
			Sur_Pillar( spot, level.time + SUR_PORTAL_WARN, DSXS_FODDER, 0 );
			Sur_AddPending( SUR_KIND_FODDER, surFodderNames[i], spot, yaw, level.time + SUR_PORTAL_WARN, SUR_PORTAL_WARN );
		}
		break;

	case DSEV_BOUNTY:
		if ( level.time >= sur.evEnd || sur.bountyClient < 0 || !g_entities[sur.bountyClient].inuse ) {
			Sur_EventEnd();
		}
		break;

	default:
		if ( level.time >= sur.evEnd ) {
			Sur_EventEnd();
		}
		break;
	}
}

/*
===============================================================================

DEATHS

===============================================================================
*/

void G_DS_SurpriseDeath( gentity_t *self, gentity_t *attacker ) {
	vec3_t		org;
	int			n, sk, byPlayer, xp;
	qboolean	od = qfalse;

	if ( !sur.on || !self || !self->client ) {
		return;
	}
	n = self - g_entities;
	if ( n < 0 || n >= MAX_CLIENTS ) {
		return;
	}
	byPlayer = attacker && attacker->client && attacker != self && G_RL_IsPlayer( attacker );
	VectorCopy( self->r.currentOrigin, org );
	org[2] += 16;

	switch ( sur.kind[n] ) {
	case SUR_KIND_AMBUSH:
		if ( sur.ambState != 3 ) {
			break;
		}
		sur.ambState = 4;
		Sur_EndAttack();
		Sur_KickSoon( n );
		if ( byPlayer ) {
			sk = G_DS_Skulls( 4 + sur.floor / 3 );
			G_RL_AddSkulls( sk );
			xp = 30 + 5 * sur.floor;
			G_DS_SpawnGems( org, xp, -1 );		// (+ LOOT's elite drop: a chest and its gems)
			if ( Sur_Roll( 100 + level.time % 97 ) < SUR_OVERDRIVE_PCT ) {
				od = G_DS_SpawnPowerSpike( org, DS_SPIKE_OVERDRIVE ) != NULL;
			}
			G_DS_BeamAt( org, DS_BEAM_EPIC, 8000 );
			Sur_RunAdd( "sur_ambk", 1 );
			Sur_Callout( DSUC_AMBUSH_SLAIN, sur.ambArch, sk );
			Sur_Shot( 450, SUR_LOOK_NONE );
			G_LogPrintf( "DSSurprise: ambush %s slain by the player: +%i skulls, gems %i (+ the elite chest), overdrive %i\n",
				ds_miniBosses[sur.ambArch].name, sk, xp, od );
		} else {
			Sur_Callout( DSUC_AMBUSH_LOST, sur.ambArch, attacker && attacker->client ? (int)( attacker - g_entities ) : -1 );
			G_LogPrintf( "DSSurprise: ambush %s killed by %s (no reward)\n", ds_miniBosses[sur.ambArch].name,
				attacker && attacker->client ? attacker->client->pers.netname : "the world" );
		}
		sur.csDirty = 1;
		return;
	case SUR_KIND_GHOST:
		if ( sur.ghState != 3 ) {
			break;
		}
		sur.ghState = 4;
		Sur_KickSoon( n );
		if ( byPlayer ) {
			sk = G_DS_Skulls( 5 + sur.floor / 3 );
			G_RL_AddSkulls( sk );
			xp = 40 + 6 * sur.floor;
			G_DS_SpawnGems( org, xp, -1 );
			G_DS_SpawnChest( org, 1 );
			G_DS_BeamAt( org, DS_BEAM_LEGENDARY, 8000 );
			Sur_RunAdd( "sur_ghk", 1 );
			Sur_Callout( DSUC_GHOST_SLAIN, sur.ghChar, sk );
			Sur_Shot( 450, SUR_LOOK_NONE );
			G_SlowMo( 0.5f, 600 );
			G_LogPrintf( "DSGhost: YOU SURPASSED YOURSELF: +%i skulls, gems %i, a chest\n", sk, xp );
		} else {
			Sur_Callout( DSUC_GHOST_FADED, 0, 0 );
			G_LogPrintf( "DSGhost: faded (killed by %s)\n", attacker && attacker->client ? attacker->client->pers.netname : "the world" );
		}
		sur.csDirty = 1;
		return;
	case SUR_KIND_FODDER:
		Sur_KickSoon( n );
		if ( byPlayer ) {
			sur.fodderSlain++;
		}
		break;
	default:
		break;
	}
	if ( !sur.ev || level.intermissiontime ) {
		return;
	}
	if ( byPlayer && !G_RL_IsPlayer( self ) ) {
		sur.evKills++;
		if ( sur.ev == DSEV_ECLIPSE ) {
			G_DS_SpawnGems( org, 4 + sur.floor / 3, -1 );		// the double
		}
	}
	if ( sur.ev == DSEV_BOUNTY && n == sur.bountyClient ) {
		G_DS_LootBeamOff( self );
		if ( byPlayer ) {
			sk = G_DS_Skulls( 2 + sur.floor / 4 );
			G_RL_AddSkulls( sk );
			G_DS_SpawnChest( org, 1 );
			G_DS_SpawnGems( org, 20 + 4 * sur.floor, -1 );
			Sur_Callout( DSUC_BOUNTY_CLAIMED, n, sk );
			G_LogPrintf( "DSEvent: BOUNTY claimed by the player: +%i skulls, a chest\n", sk );
			sur.bountyClient = -1;
			Sur_EventEnd();
		} else if ( attacker && attacker->client && attacker != self && !sur.kind[attacker - g_entities] ) {
			// the killer takes the bounty on
			sur.bountyClient = attacker - g_entities;
			sur.evC = sur.bountyClient;
			G_DS_LootBeam( attacker, DS_BEAM_EPIC );
			Sur_Callout( DSUC_EVENT_START, DSEV_BOUNTY, sur.bountyClient );
			G_LogPrintf( "DSEvent: BOUNTY passes to %s\n", attacker->client->pers.netname );
			sur.csDirty = 1;
		} else {
			sur.bountyClient = -1;
			Sur_EventEnd();
		}
	}
}

/*
===============================================================================

BOTS (ai hooks)

===============================================================================
*/

void G_DS_SurpriseBotInput( int client, usercmd_t *ucmd ) {
	gentity_t	*e;

	if ( !sur.on || client < 0 || client >= MAX_CLIENTS || !sur.kind[client] ) {
		return;
	}
	e = &g_entities[client];
	if ( e->health <= 0 || sur.kickAt[client] ) {
		ucmd->buttons = 0;		// one life: no respawn, it leaves in a moment
		ucmd->forwardmove = ucmd->rightmove = ucmd->upmove = 0;
		return;
	}
	if ( client == sur.ambClient && sur.ambState == 3 ) {
		if ( level.time < sur.rootUntil ) {
			ucmd->forwardmove = ucmd->rightmove = 0;
			if ( ucmd->upmove > 0 ) {
				ucmd->upmove = 0;
			}
		}
		if ( level.time < sur.holdFireUntil ) {
			ucmd->buttons &= ~( BUTTON_ATTACK | BUTTON_ALTATTACK );
		}
	}
}

qboolean G_DS_SurpriseHuntSpot( int client, vec3_t out ) {
	gentity_t	*self, *e, *best = NULL;
	float		d, bestD = 1e9f;
	int			i;

	if ( !sur.on || client < 0 || client >= MAX_CLIENTS ) {
		return qfalse;
	}
	self = &g_entities[client];
	if ( !Sur_Alive( self ) ) {
		return qfalse;
	}
	// the gold race: about half the floor's own bots go for the crater
	if ( sur.ev == DSEV_GOLDMETEOR && !sur.kind[client] && ( sur.goldChest || sur.goldImpact )
		&& level.time < sur.goldRaceEnd + ( sur.goldImpact ? SUR_GOLD_FALL : 0 ) && ( client & 1 ) ) {
		VectorCopy( sur.goldSpot, out );
		return qtrue;
	}
	switch ( sur.kind[client] ) {
	case SUR_KIND_GHOST:
		e = Sur_Human();
		if ( Sur_Alive( e ) ) {
			VectorCopy( e->r.currentOrigin, out );
			return qtrue;
		}
		return qfalse;
	case SUR_KIND_AMBUSH:
	case SUR_KIND_FODDER:
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			e = &g_entities[i];
			if ( i == client || !Sur_Alive( e ) || sur.kind[i] == SUR_KIND_FODDER ) {
				continue;
			}
			d = Distance( e->r.currentOrigin, self->r.currentOrigin );
			if ( G_RL_IsPlayer( e ) ) {
				d *= 0.7f;
			}
			if ( d < bestD ) {
				bestD = d;
				best = e;
			}
		}
		if ( best ) {
			VectorCopy( best->r.currentOrigin, out );
			return qtrue;
		}
		return qfalse;
	default:
		return qfalse;
	}
}

qboolean G_DS_SurprisePriority( int bot, int client ) {
	if ( !sur.on || bot < 0 || bot >= MAX_CLIENTS ) {
		return qfalse;
	}
	return sur.kind[bot] == SUR_KIND_GHOST && client == G_RL_HumanClient();
}

qboolean G_DS_SurpriseAggressive( int client ) {
	return sur.on && client >= 0 && client < MAX_CLIENTS && ( sur.kind[client] == SUR_KIND_AMBUSH || sur.kind[client] == SUR_KIND_GHOST );
}

/*
===============================================================================

FRAME

===============================================================================
*/

// ds-arrivals: the floor's own bots arrive one at a time early on; nothing of ours lands on top of that
static qboolean Sur_ArrivalsDone( void ) {
	return G_DS_ArrivalsLeft() == 0 && level.time >= G_DS_ArrivalLastAt() + 3000;
}

static int Sur_LeaderFrags( void ) {
	int		i, best = 0;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( level.clients[i].pers.connected == CON_CONNECTED && !sur.kind[i] && level.clients[i].ps.persistant[PERS_SCORE] > best ) {
			best = level.clients[i].ps.persistant[PERS_SCORE];
		}
	}
	return best;
}

static void Sur_DevLook( gentity_t *h, const vec3_t target, int live );

// where a look goes: qfalse = nothing there
static qboolean Sur_LookTarget( int what, vec3_t out, int *live ) {
	gentity_t	*t = NULL;
	int			i;

	switch ( what ) {
	case SUR_LOOK_AMBUSH:
		if ( sur.ambClient >= 0 && Sur_Alive( &g_entities[sur.ambClient] ) ) {
			t = &g_entities[sur.ambClient];
		} else if ( sur.ambState == 1 || sur.ambState == 2 ) {
			VectorCopy( sur.ambSpot, out );
			return qtrue;
		}
		break;
	case SUR_LOOK_GHOST:
		if ( sur.ghClient >= 0 && Sur_Alive( &g_entities[sur.ghClient] ) ) {
			t = &g_entities[sur.ghClient];
		} else if ( sur.ghState == 1 || sur.ghState == 2 ) {
			VectorCopy( sur.ghSpot, out );
			return qtrue;
		}
		break;
	case SUR_LOOK_GOLD:
		VectorCopy( sur.goldSpot, out );
		if ( sur.goldImpact ) {
			out[2] += 260;		// (still falling: look up the line it comes down)
		}
		return qtrue;
	case SUR_LOOK_FODDER:
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			if ( sur.kind[i] == SUR_KIND_FODDER && Sur_Alive( &g_entities[i] ) ) {
				t = &g_entities[i];
				break;
			}
		}
		break;
	case SUR_LOOK_BOUNTY:
		if ( sur.bountyClient >= 0 ) {
			t = &g_entities[sur.bountyClient];
		}
		break;
	default:
		break;
	}
	if ( t && Sur_Alive( t ) ) {
		VectorCopy( t->r.currentOrigin, out );
		*live = t->s.number;
		return qtrue;
	}
	return qfalse;
}

static void Sur_ShotFrame( gentity_t *h ) {
	vec3_t		tg;
	int			i;
	int			live = -1;

	if ( !surCvShots.integer || !h ) {
		return;
	}
	if ( sur.mod && !sur.shotMod && sur.humanIn && level.time > sur.humanIn + 3000 ) {
		sur.shotMod = qtrue;
		Sur_Shot( 0, SUR_LOOK_NONE );
	}
	for ( i = 0 ; i < 8 ; i++ ) {
		if ( !sur.shotAt[i] || level.time < sur.shotAt[i] ) {
			continue;
		}
		sur.shotAt[i] = 0;
		live = -1;
		if ( sur.shotLook[i] && Sur_LookTarget( sur.shotLook[i], tg, &live ) ) {
			Sur_DevLook( h, tg, live );
		}
		trap_SendConsoleCommand( EXEC_INSERT, "wait 40; screenshotJPEG\n" );
		G_LogPrintf( "DSSurprise: screenshot (look %i)\n", sur.shotLook[i] );
		return;		// one a frame
	}
}

void G_DS_SurpriseFrame( void ) {
	gentity_t	*h, *e;
	int			i, el, frags;

	if ( !sur.on ) {
		return;
	}
	if ( level.time >= sur.cvNext ) {
		sur.cvNext = level.time + 1000;
		Sur_Cvars();		// (switches changed mid-floor, g_dsSurShots from a test cfg)
	}
	// the leavers
	for ( i = 0 ; i < MAX_CLIENTS ; i++ ) {
		if ( sur.kickAt[i] && level.time >= sur.kickAt[i] ) {
			sur.kickAt[i] = 0;
			e = &g_entities[i];
			if ( e->inuse && e->client && ( e->r.svFlags & SVF_BOT ) && sur.kind[i] ) {
				G_TempEntity( e->r.currentOrigin, EV_PLAYER_TELEPORT_OUT );
				trap_DropClient( i, "is gone" );
			}
			sur.kind[i] = 0;
			if ( i == sur.ambClient ) {
				sur.ambClient = -1;
			}
			if ( i == sur.ghClient ) {
				sur.ghClient = -1;
			}
			sur.csDirty = 1;
		}
		// a client slot reused by someone else
		if ( sur.kind[i] && level.clients[i].pers.connected == CON_DISCONNECTED ) {
			sur.kind[i] = 0;
			sur.kickAt[i] = 0;
		}
	}
	h = Sur_Human();
	if ( !h || level.intermissiontime || level.intermissionQueued || level.warmupTime ) {
		if ( sur.csDirty && level.time >= sur.csNext ) {
			Sur_Publish();
		}
		return;
	}
	if ( !sur.humanIn ) {
		sur.humanIn = level.time;
		sur.ambPower = G_DS_PowerScore( h );
	}
	el = Sur_Elapsed();
	Sur_RunPending();

	// the ambush: partway through the floor (the leader at a third of the frags, or late enough)
	if ( sur.ambPlan && sur.ambState == 0 && el >= sur.ambEarliest && Sur_ArrivalsDone() ) {
		frags = Sur_LeaderFrags();
		if ( el >= sur.ambLatest || frags * 100 >= G_FragLimit() * 35 ) {
			sur.ambPower = G_DS_PowerScore( h );
			Sur_StartAmbushWarning();
		}
	}
	Sur_MiniBossFrame();
	Sur_RunStrikes();

	// the wild event
	if ( sur.evPlan && el >= sur.evAt && Sur_ArrivalsDone() ) {
		i = sur.evPlan;
		sur.evPlan = 0;
		Sur_StartEvent( i );
	}
	Sur_EventFrame();

	// the ghost
	if ( sur.ghPlan && sur.ghState == 0 && el >= sur.ghAt && Sur_ArrivalsDone() ) {
		Sur_StartGhostWarning();
	}
	if ( sur.ghState == 1 && level.time >= sur.ghLandAt ) {
		Sur_AddGhostBot();
	}

	Sur_ShotFrame( h );
	if ( sur.csDirty && level.time >= sur.csNext ) {
		Sur_Publish();
	}
}

/*
===============================================================================

DEV

===============================================================================
*/

/*
=================
Sur_DevLook

Dev / tests: the player goes to a spot 650-1300 from target with a clear line to it (never
closer, so a landing there is not re-picked) and faces it
=================
*/
static void Sur_DevLook( gentity_t *h, const vec3_t target, int live ) {
	gentity_t	*spot = NULL, *best = NULL;
	trace_t		tr;
	vec3_t		org, tg, d, ang;
	float		dd, bestScore = 1e9f;


	VectorCopy( target, tg );
	tg[2] += 40;
	while ( ( spot = G_Find( spot, FOFS( classname ), "info_player_deathmatch" ) ) != NULL ) {
		VectorCopy( spot->s.origin, org );
		org[2] += 40;
		dd = Distance( org, tg );
		if ( dd < ( live >= 0 ? 250 : 650 ) || dd > 2400 ) {
			continue;
		}
		trap_Trace( &tr, org, NULL, NULL, tg, ENTITYNUM_NONE, MASK_SOLID );
		if ( tr.fraction < 1.0f ) {
			continue;
		}
		if ( fabs( dd - ( live >= 0 ? 380 : 900 ) ) < bestScore ) {
			bestScore = fabs( dd - ( live >= 0 ? 380 : 900 ) );
			best = spot;
		}
	}
	if ( best ) {
		VectorCopy( best->s.origin, h->client->ps.origin );
		h->client->ps.origin[2] += 9;
		VectorClear( h->client->ps.velocity );
		h->client->ps.eFlags ^= EF_TELEPORT_BIT;
	} else {
		// no spawn spot in sight of it: a floor point 650-1010 away along a clear line
		vec3_t	mins = { -15, -15, -24 }, maxs = { 15, 15, 32 }, p2;
		int		k;

		int		nSolid = 0, nShort = 0, nAir = 0;

		tg[2] -= 32;		// (the hull fits under low ceilings)
		for ( k = 0 ; k < 96 ; k++ ) {
			// as far as the room goes along this line (at least 650; after a landing 350 will do)
			VectorSet( p2, tg[0] + cos( k * 0.39f ) * 1300, tg[1] + sin( k * 0.39f ) * 1300, tg[2] );
			trap_Trace( &tr, tg, mins, maxs, p2, live >= 0 ? live : ENTITYNUM_NONE, MASK_PLAYERSOLID );
			if ( tr.startsolid ) {
				nSolid++;
				continue;
			}
			dd = Distance( tr.endpos, tg );
			if ( dd < ( k < 48 ? 650 : 350 ) || ( k >= 48 && live < 0 ) ) {
				nShort++;
				continue;
			}
			VectorSubtract( tr.endpos, tg, p2 );
			VectorMA( tg, ( dd - 24 ) / dd, p2, p2 );
			VectorCopy( p2, org );
			org[2] -= 400;
			trap_Trace( &tr, p2, mins, maxs, org, ENTITYNUM_NONE, MASK_PLAYERSOLID );
			if ( tr.fraction >= 1.0f || tr.startsolid ) {
				nAir++;
				continue;
			}
			VectorCopy( tr.endpos, h->client->ps.origin );
			VectorClear( h->client->ps.velocity );
			h->client->ps.eFlags ^= EF_TELEPORT_BIT;
			break;
		}
		if ( k == 96 ) {
			G_LogPrintf( "DSSurprise: look found no floor point (start solid %i, short %i, no floor %i)\n", nSolid, nShort, nAir );
		}
	}
	VectorCopy( h->client->ps.origin, org );
	org[2] += h->client->ps.viewheight;
	VectorSubtract( target, org, d );
	d[2] += 20;
	vectoangles( d, ang );
	ang[ROLL] = 0;
	SetClientViewAngle( h, ang );
	BG_PlayerStateToEntityState( &h->client->ps, &h->s, qtrue );
	VectorCopy( h->client->ps.origin, h->r.currentOrigin );
	trap_LinkEntity( h );
	G_LogPrintf( "DSSurprise: look at %i %i %i from %.0f (%s, player at %i %i %i)\n", (int)target[0], (int)target[1], (int)target[2],
		Distance( target, h->r.currentOrigin ), best ? "spawn spot" : "floor point", (int)h->r.currentOrigin[0], (int)h->r.currentOrigin[1],
		(int)h->r.currentOrigin[2] );
}

/*
=================
Sur_PlanSim

Dev (dssurprise sim): the plan rolls of G_DS_SurpriseInit over 2000 runs x floors 2-24 (boss
floors left out, every other floor taken as a fight / elite floor, a past snapshot everywhere):
how often each surprise comes, the longest ambush streak
=================
*/
static void Sur_PlanSim( void ) {
	int		saveSeed = sur.seed, saveFloor = sur.floor;
	int		run, f, last, floors = 0, amb = 0, ev[DSEV_NUM], gh = 0, any = 0, streak, maxStreak = 0, both = 0, e, a, g;

	memset( ev, 0, sizeof( ev ) );
	for ( run = 0 ; run < 2000 ; run++ ) {
		sur.seed = (int)( Sur_Hash( 12345u, (unsigned)run ) & 0x7fffffff );
		last = -9;
		streak = 0;
		for ( f = 2 ; f <= DS_WIN_FLOOR ; f++ ) {
			if ( DS_IsBossFloor( f ) ) {
				streak = 0;
				continue;
			}
			sur.floor = f;
			floors++;
			a = last != f - 1 && Sur_Roll( 1 ) < SUR_AMB_CHANCE;
			e = Sur_Roll( 5 ) < SUR_EVENT_CHANCE ? 1 + Sur_Roll( 6 ) % ( DSEV_NUM - 1 ) : 0;
			g = !a && Sur_Roll( 8 ) < SUR_GHOST_CHANCE;
			if ( a ) {
				amb++;
				last = f;
				streak++;
				if ( streak > maxStreak ) {
					maxStreak = streak;
				}
			} else {
				streak = 0;
			}
			ev[e]++;
			gh += g;
			any += a || e || g;
			both += a && e;
		}
	}
	sur.seed = saveSeed;
	sur.floor = saveFloor;
	G_LogPrintf( "DSSurprise: sim %i eligible floors: ambush %.1f%% (1 in %.1f, longest run of ambush floors %i), events %.1f%% (gold %i uprising %i eclipse %i bounty %i), ghost %.1f%% (with a snapshot), any %.1f%%, ambush+event %.1f%%\n",
		floors, 100.0f * amb / floors, (float)floors / ( amb ? amb : 1 ), maxStreak, 100.0f * ( floors - ev[0] ) / floors,
		ev[1], ev[2], ev[3], ev[4], 100.0f * gh / floors, 100.0f * any / floors, 100.0f * both / floors );
	G_Printf( "dssurprise sim: see games.log (DSSurprise: sim)\n" );
}

qboolean G_DS_SurpriseCommand( const char *cmd ) {
	char	arg[64];
	int		i, n;

	if ( !Q_stricmp( cmd, "dsambush" ) ) {
		if ( !sur.on || !Sur_Human() ) {
			G_Printf( "dsambush: no descent floor with the player in\n" );
			return qtrue;
		}
		trap_Argv( 1, arg, sizeof( arg ) );
		n = arg[0] ? atoi( arg ) : rand() % DS_MINIBOSS_NUM;
		for ( i = 0 ; arg[0] && i < DS_MINIBOSS_NUM ; i++ ) {
			if ( !Q_stricmp( arg, ds_miniBosses[i].netName ) ) {
				n = i;
			}
		}
		if ( sur.ambState == 3 || sur.ambState == 1 || sur.ambState == 2 ) {
			G_Printf( "dsambush: one is already here\n" );
			return qtrue;
		}
		sur.ambArch = n < 0 || n >= DS_MINIBOSS_NUM ? 0 : n;
		sur.ambPlan = qtrue;
		sur.ambState = 0;
		sur.ambPower = G_DS_PowerScore( Sur_Human() );
		Sur_StartAmbushWarning();
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsevent" ) ) {
		if ( !sur.on || !Sur_Human() ) {
			G_Printf( "dsevent: no descent floor with the player in\n" );
			return qtrue;
		}
		trap_Argv( 1, arg, sizeof( arg ) );
		for ( n = 1 ; n < DSEV_NUM ; n++ ) {
			if ( !Q_stricmp( arg, ds_wildEventKeys[n] ) || atoi( arg ) == n ) {
				break;
			}
		}
		if ( n >= DSEV_NUM ) {
			G_Printf( "dsevent goldmeteor | uprising | eclipse | bounty | end\n" );
			if ( !Q_stricmp( arg, "end" ) ) {
				Sur_EventEnd();
			}
			return qtrue;
		}
		Sur_StartEvent( n );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsghost" ) ) {
		const char	*snap;

		if ( !sur.on || !Sur_Human() ) {
			G_Printf( "dsghost: no descent floor with the player in\n" );
			return qtrue;
		}
		if ( sur.ghState >= 1 && sur.ghState <= 3 ) {
			G_Printf( "dsghost: the ghost is already here\n" );
			return qtrue;
		}
		trap_Argv( 1, arg, sizeof( arg ) );
		n = arg[0] ? atoi( arg ) : sur.floor;
		Sur_GhLoad();
		snap = Sur_GhGet( va( "past_%i", n ) );
		if ( !snap[0] ) {
			snap = Sur_GhGet( va( "cur_%i", n ) );		// (tests: no past run yet, use this run's)
		}
		if ( !snap[0] || !Sur_GhParse( snap ) ) {
			G_Printf( "dsghost: no snapshot for floor %i\n", n );
			return qtrue;
		}
		sur.ghPlan = qtrue;
		sur.ghState = 0;
		Sur_StartGhostWarning();
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsmod" ) ) {
		trap_Argv( 1, arg, sizeof( arg ) );
		if ( !sur.on ) {
			G_Printf( "dsmod: no descent floor\n" );
			return qtrue;
		}
		sur.mod = BG_DS_ModByKey( arg );
		G_RL_RunSet( "node_mod", sur.mod ? ds_modifiers[sur.mod].key : "" );
		sur.csDirty = 1;
		Sur_Publish();
		G_Printf( "dsmod: this floor's modifier is now %s (spawn / respawn effects apply from the next spawn)\n", sur.mod ? ds_modifiers[sur.mod].name : "none" );
		G_LogPrintf( "DSSurprise: dsmod %s\n", sur.mod ? ds_modifiers[sur.mod].name : "none" );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dssurprise" ) ) {
		gentity_t	*h = Sur_Human(), *t = NULL;
		vec3_t		target, d, ang;

		trap_Argv( 1, arg, sizeof( arg ) );
		if ( !Q_stricmp( arg, "sim" ) ) {
			Sur_PlanSim();
			return qtrue;
		}
		if ( !Q_stricmp( arg, "kill" ) || !Q_stricmp( arg, "look" ) ) {
			// dev: kill <ambush|ghost|bounty|fodder> as the player / look: face (and get in sight of) the surprise
			char	what[16];
			qboolean	have = qfalse;

			trap_Argv( 2, what, sizeof( what ) );
			if ( !h ) {
				return qtrue;
			}
			if ( !Q_stricmp( what, "ghost" ) && sur.ghClient >= 0 ) {
				t = &g_entities[sur.ghClient];
			} else if ( !Q_stricmp( what, "bounty" ) && sur.bountyClient >= 0 ) {
				t = &g_entities[sur.bountyClient];
			} else if ( !Q_stricmp( what, "fodder" ) ) {
				for ( i = 0 ; i < level.maxclients ; i++ ) {
					if ( sur.kind[i] == SUR_KIND_FODDER && Sur_Alive( &g_entities[i] ) ) {
						t = &g_entities[i];
						break;
					}
				}
			} else if ( !Q_stricmp( what, "gold" ) && sur.ev == DSEV_GOLDMETEOR ) {
				VectorCopy( sur.goldSpot, target );
				have = qtrue;
			} else if ( sur.ambClient >= 0 ) {
				t = &g_entities[sur.ambClient];
			} else if ( sur.ambState == 1 || sur.ambState == 2 ) {
				VectorCopy( sur.ambSpot, target );
				have = qtrue;
			} else if ( sur.ghState == 1 || sur.ghState == 2 ) {
				VectorCopy( sur.ghSpot, target );
				have = qtrue;
			}
			if ( t && Sur_Alive( t ) ) {
				VectorCopy( t->r.currentOrigin, target );
				have = qtrue;
			}
			if ( !have ) {
				G_Printf( "dssurprise %s: nothing there\n", arg );
				return qtrue;
			}
			if ( !Q_stricmp( arg, "kill" ) ) {
				if ( t && Sur_Alive( t ) ) {
					t->flags &= ~FL_GODMODE;
					G_Damage( t, h, h, NULL, t->r.currentOrigin, t->health + 999, DAMAGE_NO_ARMOR | DAMAGE_NO_PROTECTION, MOD_RAILGUN );
				}
				return qtrue;
			}
			Sur_DevLook( h, target, t ? t->s.number : -1 );
			return qtrue;
		}
		G_Printf( "dssurprise: on %i floor %i type %i mod %i amb plan %i arch %i state %i client %i hp %i%% | ev plan %i run %i end %i | ghost plan %i state %i client %i | free slots %i\n",
			sur.on, sur.floor, sur.nodeType, sur.mod, sur.ambPlan, sur.ambArch, sur.ambState, sur.ambClient, sur.ambHpPct, sur.evPlan,
			sur.ev, sur.evEnd, sur.ghPlan, sur.ghState, sur.ghClient, Sur_FreeSlots() );
		G_LogPrintf( "DSSurprise: state mod %i amb %i/%i/%i ev %i ghost %i/%i shots %i (%i %i)\n", sur.mod, sur.ambPlan, sur.ambState, sur.ambClient,
			sur.ev, sur.ghPlan, sur.ghState, surCvShots.integer, sur.shotAt[0], level.time );
		return qtrue;
	}
	return qfalse;
}
