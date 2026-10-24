"""v2 server side: feature switches, OITC mode, jetpack mutator, medals/bounty fields,
review fixes (headshots, nail headshot point, singularity team check, rail ricochet cap,
gun game fraglimit / world deaths, prox detonate on stuck mines), sticky grenades on players,
BFG nearest targets, lunge -> uppercut combo."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import patch
from patch import sub
if len(sys.argv) > 1 and sys.argv[1] == 'resume':
    patch.resume(__file__)
    sub = patch.sub

ROOT = pathlib.Path(__file__).resolve().parents[2] / 'ioq3'
g = lambda f: str(ROOT / 'code' / 'game' / f)

# ---------------- bg_public.h / bg_misc.c ----------------
sub(g('bg_public.h'), '''	RM_MODE_INSTAGIB,
	RM_NUM_MODES
} rmMode_t;''', '''	RM_MODE_INSTAGIB,
	RM_MODE_OITC,			// one in the chamber
	RM_NUM_MODES
} rmMode_t;

// rearmed: feature switches (g_ cvars), bit numbers in the CS_REARMED "sw" value
typedef enum {
	RS_DOUBLEJUMP,
	RS_WALLJUMP,
	RS_WALLRUN,
	RS_DASH,
	RS_JUMPASSIST,
	RS_ALTFIRE,
	RS_SHOTGUNKICK,
	RS_SPINUP,
	RS_RICOCHET,
	RS_STICKY,
	RS_SINGULARITY,
	RS_BFGLASERS,
	RS_UPPERCUT,
	RS_HEADSHOTS,
	RS_AIRSHOTS,
	RS_KILLSTREAKS,
	RS_BOUNTY,
	RS_MEDALS,
	RS_POWERUPS,
	RS_NUM
} rmSwitch_t;''')
sub(g('bg_public.h'), '''	MUT_QUAD_MADNESS,
	MUT_NUM_MUTATORS''', '''	MUT_QUAD_MADNESS,
	MUT_JETPACKS,
	MUT_NUM_MUTATORS''')
sub(g('bg_misc.c'), 'const char *bg_rmModeNames[RM_NUM_MODES] = { "Normal", "Gun Game", "Juggernaut", "Instagib" };',
    'const char *bg_rmModeNames[RM_NUM_MODES] = { "Normal", "Gun Game", "Juggernaut", "Instagib", "One in the Chamber" };')
sub(g('bg_misc.c'), '''	"", "LOW GRAVITY", "VAMPIRE", "ROCKET FRENZY", "SPEED RUSH", "BIG HEADS", "KNOCKBACK CHAOS", "QUAD MADNESS"
};''', '''	"", "LOW GRAVITY", "VAMPIRE", "ROCKET FRENZY", "SPEED RUSH", "BIG HEADS", "KNOCKBACK CHAOS", "QUAD MADNESS",
	"JETPACK PARTY"
};''')
sub(g('bg_misc.c'), '''	"headshots are easy", "everything hits harder", "everyone has quad damage"
};''', '''	"headshots are easy", "everything hits harder", "everyone has quad damage",
	"hold jump to fly"
};''')

# ---------------- g_local.h ----------------
sub(g('g_local.h'), '''	int			streak;				// kills this life
	int			ggLevel;			// gun game ladder position
''', '''	int			streak;				// kills this life
	qboolean	bounty;				// streak long enough to carry a bounty
	qboolean	frenzyHadLauncher;	// loadout before rocket frenzy, restored after
	int			frenzyAmmo;
	int			lungeTime;			// last gauntlet lunge, a hit soon after is a combo
''')
sub(g('g_local.h'), '''typedef struct {
	clientConnected_t	connected;
''', '''typedef struct {
	clientConnected_t	connected;
	// rearmed: kept across respawns
	int			ggLevel;			// gun game ladder position
	int			lastKiller;			// client that killed us last (revenge medal), -1 none
	int			deathStreak;		// deaths since our last kill
	int			multiKills;
	int			multiKillTime;
''')
sub(g('g_local.h'), '''	int			juggernaut;			// client number or -1
''', '''	int			juggernaut;			// client number or -1
	qboolean	firstBlood;
''')
sub(g('g_local.h'), '''qboolean G_RearmHandlesScore( void );
''', '''qboolean G_RearmHandlesScore( void );
qboolean G_RearmOn( int sw );
int G_RearmMoveFlags( void );
qboolean G_RearmIgnoresFraglimit( void );
void G_RearmClientDisconnect( gentity_t *ent );
''')

# ---------------- g_client.c / g_main.c / g_active.c ----------------
sub(g('g_client.c'), '''	client->pers.connected = CON_CONNECTING;
''', '''	client->pers.connected = CON_CONNECTING;
	client->pers.lastKiller = -1;
''')
sub(g('g_client.c'), '''	ent = g_entities + clientNum;
	if (!ent->client || ent->client->pers.connected == CON_DISCONNECTED) {
		return;
	}
''', '''	ent = g_entities + clientNum;
	if (!ent->client || ent->client->pers.connected == CON_DISCONNECTED) {
		return;
	}
	G_RearmClientDisconnect( ent );
''')
sub(g('g_main.c'), '	if ( g_gametype.integer < GT_CTF && g_fraglimit.integer ) {\n',
    '	if ( g_gametype.integer < GT_CTF && g_fraglimit.integer && !G_RearmIgnoresFraglimit() ) {\n')
sub(g('g_active.c'), '''	pm.pmove_fixed = pmove_fixed.integer | client->pers.pmoveFixed;
	pm.pmove_msec = pmove_msec.integer;
''', '''	pm.pmove_fixed = pmove_fixed.integer | client->pers.pmoveFixed;
	pm.pmove_msec = pmove_msec.integer;

	// feature switches travel in the playerState so prediction matches
	client->ps.stats[STAT_RMFLAGS] = ( client->ps.stats[STAT_RMFLAGS] & ~RMF_CONFIG ) | G_RearmMoveFlags();
''')
sub(g('g_active.c'), '''			if ( bg_altFire[client->ps.weapon].mode == ALTFIRE_TRIGGER
				&& client->ps.pm_type == PM_NORMAL && client->ps.stats[STAT_HEALTH] > 0 ) {''',
    '''			if ( bg_altFire[client->ps.weapon].mode == ALTFIRE_TRIGGER && G_RearmOn( RS_ALTFIRE )
				&& client->ps.pm_type == PM_NORMAL && client->ps.stats[STAT_HEALTH] > 0 ) {''')

# ---------------- g_combat.c ----------------
C = g('g_combat.c')
sub(C, '''	if ( targ->client && attacker->client && attacker != targ && point && G_IsHeadshotMod( mod )
		&& point[2] >= targ->r.currentOrigin[2] + targ->client->ps.viewheight - G_RearmHeadshotBelowEyes() ) {
		damage = damage * HEADSHOT_SCALE_NUM / HEADSHOT_SCALE_DEN;
		attacker->client->ps.persistant[PERS_HEADSHOTS]++;
		hitFlags |= HIT_HEADSHOT;
	}''', '''	if ( G_RearmOn( RS_HEADSHOTS ) && targ->client && attacker->client && attacker != targ && point && G_IsHeadshotMod( mod )
		&& point[2] >= targ->r.currentOrigin[2] + targ->client->ps.viewheight - G_RearmHeadshotBelowEyes() ) {
		damage = damage * HEADSHOT_SCALE_NUM / HEADSHOT_SCALE_DEN;
		hitFlags |= HIT_HEADSHOT;		// counted below, once the hit is known to land
	}''')
sub(C, '''	if ( knockback && targ->client && targ != attacker && targ->client->ps.groundEntityNum == ENTITYNUM_NONE ) {''',
    '''	if ( G_RearmOn( RS_AIRSHOTS ) && knockback && targ->client && targ != attacker
		&& targ->client->ps.groundEntityNum == ENTITYNUM_NONE ) {''')
sub(C, '''		if ( targ->health - take <= 0 ) {
			hitFlags |= HIT_KILL;
		}
		G_GatherHitConfirm( targ, attacker, take + asave, hitFlags );''', '''		if ( targ->health - take <= 0 ) {
			hitFlags |= HIT_KILL;
		}
		if ( hitFlags & HIT_HEADSHOT ) {
			attacker->client->ps.persistant[PERS_HEADSHOTS]++;
		}
		G_GatherHitConfirm( targ, attacker, take + asave, hitFlags );''')
# world / environment deaths don't cost gun game or juggernaut score
sub(C, '''	} else {
		AddScore( self, self->r.currentOrigin, -1 );
	}

	// Add team bonuses''', '''	} else if ( !G_RearmHandlesScore() ) {
		AddScore( self, self->r.currentOrigin, -1 );
	}

	// Add team bonuses''')

# ---------------- g_missile.c ----------------
M = g('g_missile.c')
# singularity: switch + team check
sub(M, '''		if ( !other->inuse || !other->client || other->health <= 0 || i == ent->r.ownerNum
			|| other->client->sess.sessionTeam == TEAM_SPECTATOR || other->client->noclip ) {
			continue;
		}
		VectorSubtract( ent->r.currentOrigin, other->r.currentOrigin, dir );''', '''		if ( !other->inuse || !other->client || other->health <= 0 || i == ent->r.ownerNum
			|| other->client->sess.sessionTeam == TEAM_SPECTATOR || other->client->noclip
			|| OnSameTeam( other, &g_entities[ent->r.ownerNum] ) ) {
			continue;
		}
		VectorSubtract( ent->r.currentOrigin, other->r.currentOrigin, dir );''')
sub(M, '''	if ( level.time >= ent->count ) {
		G_ExplodeMissile( ent );
		return;
	}
	for ( i = 0 ; i < level.maxclients ; i++ ) {''', '''	if ( level.time >= ent->count ) {
		G_ExplodeMissile( ent );
		return;
	}
	ent->nextthink = level.time + SINGULARITY_TICK;
	if ( !G_RearmOn( RS_SINGULARITY ) ) {
		return;		// just a slow orb
	}
	for ( i = 0 ; i < level.maxclients ; i++ ) {''')
# BFG: the nearest targets rather than the lowest client numbers
sub(M, '''	owner = &g_entities[ent->r.ownerNum];
	zapped = 0;
	for ( i = 0 ; i < level.maxclients && zapped < BFG_LASER_TARGETS ; i++ ) {
		other = &g_entities[i];
		if ( !other->inuse || !other->client || other->health <= 0 || i == ent->r.ownerNum
			|| other->client->sess.sessionTeam == TEAM_SPECTATOR || OnSameTeam( other, owner ) ) {
			continue;
		}
		VectorCopy( other->r.currentOrigin, center );
		center[2] += 8;
		if ( Distance( ent->r.currentOrigin, center ) > BFG_LASER_RADIUS ) {
			continue;
		}
		trap_Trace( &tr, ent->r.currentOrigin, NULL, NULL, center, ENTITYNUM_NONE, MASK_SOLID );
		if ( tr.fraction < 1.0f ) {
			continue;
		}
		VectorSubtract( center, ent->r.currentOrigin, dir );''', '''	owner = &g_entities[ent->r.ownerNum];
	ent->nextthink = level.time + BFG_LASER_TICK;
	if ( !G_RearmOn( RS_BFGLASERS ) ) {
		return;
	}
	// candidates in sight, nearest first
	numTargets = 0;
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		other = &g_entities[i];
		if ( !other->inuse || !other->client || other->health <= 0 || i == ent->r.ownerNum
			|| other->client->sess.sessionTeam == TEAM_SPECTATOR || OnSameTeam( other, owner ) ) {
			continue;
		}
		VectorCopy( other->r.currentOrigin, center );
		center[2] += 8;
		dist = Distance( ent->r.currentOrigin, center );
		if ( dist > BFG_LASER_RADIUS ) {
			continue;
		}
		trap_Trace( &tr, ent->r.currentOrigin, NULL, NULL, center, ENTITYNUM_NONE, MASK_SOLID );
		if ( tr.fraction < 1.0f ) {
			continue;
		}
		for ( j = numTargets ; j > 0 && targetDist[j - 1] > dist ; j-- ) {
			targets[j] = targets[j - 1];
			targetDist[j] = targetDist[j - 1];
		}
		targets[j] = other;
		targetDist[j] = dist;
		numTargets++;
	}
	for ( zapped = 0 ; zapped < numTargets && zapped < BFG_LASER_TARGETS ; zapped++ ) {
		other = targets[zapped];
		VectorCopy( other->r.currentOrigin, center );
		center[2] += 8;
		VectorSubtract( center, ent->r.currentOrigin, dir );''')
sub(M, '''		tent = G_TempEntity( ent->r.currentOrigin, EV_BFG_LASER );
		VectorCopy( center, tent->s.origin2 );
		SnapVector( tent->s.origin2 );
		zapped++;
	}
	ent->nextthink = level.time + BFG_LASER_TICK;
}''', '''		tent = G_TempEntity( ent->r.currentOrigin, EV_BFG_LASER );
		VectorCopy( center, tent->s.origin2 );
		SnapVector( tent->s.origin2 );
	}
}''')
sub(M, '''	gentity_t	*other, *owner, *tent;
	vec3_t		dir, center;
	trace_t		tr;
	int			i, zapped;''', '''	gentity_t	*other, *owner, *tent;
	gentity_t	*targets[MAX_CLIENTS];
	float		targetDist[MAX_CLIENTS];
	vec3_t		dir, center;
	trace_t		tr;
	float		dist;
	int			i, j, zapped, numTargets;''')

# sticky grenades: stick to players too, with a short fuse
sub(M, '''/*
================
G_IsAirshot''', '''/*
================
G_StickyThink

A sticky grenade riding on a player until its fuse runs out
================
*/
#define STICKY_PLAYER_FUSE	1000
static void G_StickyThink( gentity_t *ent ) {
	gentity_t	*victim = ent->enemy;

	if ( level.time >= ent->count ) {
		G_ExplodeMissile( ent );
		return;
	}
	if ( victim && victim->inuse && victim->client && victim->health > 0 ) {
		VectorAdd( victim->r.currentOrigin, ent->movedir, ent->s.pos.trBase );
		VectorCopy( victim->client->ps.velocity, ent->s.pos.trDelta );
		ent->s.pos.trType = TR_LINEAR;
		ent->s.pos.trTime = level.time;
		VectorCopy( ent->s.pos.trBase, ent->r.currentOrigin );
		trap_LinkEntity( ent );
	} else if ( ent->s.pos.trType != TR_STATIONARY ) {
		ent->enemy = NULL;
		G_SetOrigin( ent, ent->r.currentOrigin );
	}
	ent->nextthink = level.time + FRAMETIME;
}

static void G_StickToPlayer( gentity_t *ent, gentity_t *victim, trace_t *trace ) {
	gentity_t	*owner = &g_entities[ent->r.ownerNum];

	ent->enemy = victim;
	VectorSubtract( trace->endpos, victim->r.currentOrigin, ent->movedir );
	ent->count = level.time + STICKY_PLAYER_FUSE;
	ent->think = G_StickyThink;
	ent->nextthink = level.time;
	ent->r.contents = 0;
	ent->s.eFlags &= ~( EF_BOUNCE | EF_BOUNCE_HALF );
	G_AddEvent( ent, EV_GRENADE_BOUNCE, 0 );
	trap_SendServerCommand( victim - g_entities, "rmann warn \\"STUCK! GET CLOSE TO THEM\\"" );
	if ( owner->client ) {
		trap_SendServerCommand( ent->r.ownerNum, "rmann medal \\"STUCK\\"" );
	}
	G_StickyThink( ent );
}

/*
================
G_IsAirshot''')
sub(M, '''	// sticky grenades stay where they hit the world, then explode on their fuse
	if ( !other->takedamage && !strcmp( ent->classname, "sticky_grenade" ) ) {''', '''	// sticky grenades ride on the players they hit
	if ( other->client && other->takedamage && other->health > 0 && !strcmp( ent->classname, "sticky_grenade" )
		&& G_RearmOn( RS_STICKY ) && !OnSameTeam( other, &g_entities[ent->r.ownerNum] ) ) {
		G_StickToPlayer( ent, other, trace );
		return;
	}
	// sticky grenades stay where they hit the world, then explode on their fuse
	if ( !other->takedamage && !strcmp( ent->classname, "sticky_grenade" ) ) {''')
# direct missile hits: the impact point, not the (never set) entity origin, so nail headshots work
sub(M, '''			G_Damage (other, ent, &g_entities[ent->r.ownerNum], velocity,
				ent->s.origin, ent->damage,''', '''			G_Damage (other, ent, &g_entities[ent->r.ownerNum], velocity,
				trace->endpos, ent->damage,''')
# nail ricochet switch
sub(M, '''	bolt->s.eFlags = EF_BOUNCE;		// ricochets off walls
	bolt->count = 2;				// bounces left''', '''	bolt->s.eFlags = G_RearmOn( RS_RICOCHET ) ? EF_BOUNCE : 0;		// ricochets off walls
	bolt->count = 2;				// bounces left''')

# ---------------- g_weapon.c ----------------
W = g('g_weapon.c')
# uppercut switch + lunge combo
sub(W, '''	// uppercut: launch the victim
	if ( traceEnt->client && traceEnt->health > 0 ) {
		VectorMA( traceEnt->client->ps.velocity, GAUNTLET_LAUNCH_PUSH, forward, traceEnt->client->ps.velocity );
		if ( traceEnt->client->ps.velocity[2] < GAUNTLET_LAUNCH_UP ) {
			traceEnt->client->ps.velocity[2] = GAUNTLET_LAUNCH_UP;
		}''', '''	// uppercut: launch the victim, much higher right after a lunge
	if ( G_RearmOn( RS_UPPERCUT ) && traceEnt->client && traceEnt->health > 0 ) {
		float	scale = 1.0f;

		if ( level.time - ent->client->lungeTime < GAUNTLET_COMBO_TIME ) {
			scale = 1.6f;
			ent->client->lungeTime = 0;
			trap_SendServerCommand( ent - g_entities, "rmann medal \\"LUNGE COMBO\\"" );
		}
		VectorMA( traceEnt->client->ps.velocity, GAUNTLET_LAUNCH_PUSH * scale, forward, traceEnt->client->ps.velocity );
		if ( traceEnt->client->ps.velocity[2] < GAUNTLET_LAUNCH_UP * scale ) {
			traceEnt->client->ps.velocity[2] = GAUNTLET_LAUNCH_UP * scale;
		}''')
sub(W, '''#define GAUNTLET_LAUNCH_UP		480
#define GAUNTLET_LAUNCH_PUSH	280
''', '''#define GAUNTLET_LAUNCH_UP		480
#define GAUNTLET_LAUNCH_PUSH	280
#define GAUNTLET_COMBO_TIME		700		// a hit this soon after a lunge is a combo
''')
# rail: every target once per shot, damage falls off per ricochet
sub(W, '''static int	s_railDamage = 100;		// the alt-fire overcharge raises this for one shot
static int	s_railBounces = 0;		// ricochets left for the current overcharged shot''', '''static int	s_railDamage = 100;		// the alt-fire overcharge raises this for one shot
static int	s_railBounces = 0;		// ricochets left for the current overcharged shot
static int	s_railDepth = 0;		// ricochet segment of the current shot
static gentity_t	*s_railHit[MAX_RAIL_HITS];	// targets already hit by this shot, across ricochets
static int	s_railNumHit = 0;''')
sub(W, '''	damage = G_RearmRailDamage( s_railDamage ) * s_quadFactor;
''', '''	damage = G_RearmRailDamage( s_railDamage ) * s_quadFactor;
	if ( s_railDepth == 0 ) {
		s_railNumHit = 0;
	} else {
		damage = damage * ( 10 - 3 * s_railDepth ) / 10;	// 100%, 70%, 40%
	}
''')
sub(W, '''#else
				if( LogAccuracyHit( traceEnt, ent ) ) {
					hits++;
				}
				G_Damage (traceEnt, ent, ent, forward, trace.endpos, damage, 0, MOD_RAILGUN);
#endif''', '''#else
			for ( i = 0 ; i < s_railNumHit && s_railHit[i] != traceEnt ; i++ ) {
			}
			if ( i == s_railNumHit && s_railNumHit < MAX_RAIL_HITS ) {
				s_railHit[s_railNumHit++] = traceEnt;
				if( LogAccuracyHit( traceEnt, ent ) ) {
					hits++;
				}
				G_Damage (traceEnt, ent, ent, forward, trace.endpos, damage, 0, MOD_RAILGUN);
			}
#endif''')
sub(W, '''	// set player number for custom colors on the railtrail
	tent->s.clientNum = ent->s.clientNum;

	VectorCopy( muzzle, tent->s.origin2 );''', '''	// set player number for custom colors on the railtrail
	tent->s.clientNum = ent->s.clientNum;
	tent->s.generic1 = s_railDamage > 100 ? 1 : 0;		// overcharged: the cgame draws a heavier beam

	VectorCopy( muzzle, tent->s.origin2 );''')
sub(W, '''		CrossProduct( forward, right, up );
		weapon_railgun_fire( ent );
		return;''', '''		CrossProduct( forward, right, up );
		s_railDepth++;
		weapon_railgun_fire( ent );
		s_railDepth--;
		return;''')
sub(W, '''		s_railDamage = RAIL_OVERCHARGE_DAMAGE;
		s_railBounces = 2;''', '''		s_railDamage = RAIL_OVERCHARGE_DAMAGE;
		s_railBounces = G_RearmOn( RS_RICOCHET ) ? 2 : 0;''')
sub(W, '''		m->damage = STAKE_DAMAGE * s_quadFactor;
		m->count = 4;		// the stake ricochets even more''', '''		m->damage = STAKE_DAMAGE * s_quadFactor;
		m->count = 4;		// the stake ricochets even more
		if ( !G_RearmOn( RS_RICOCHET ) ) {
			m->s.eFlags &= ~EF_BOUNCE;
		}''')
# lunge time for the combo
sub(W, '''	switch ( ent->s.weapon ) {
	case WP_MACHINEGUN:
		for ( i = 0 ; i < 3 ; i++ ) {''', '''	switch ( ent->s.weapon ) {
	case WP_GAUNTLET:
		ent->client->lungeTime = level.time;
		break;
	case WP_MACHINEGUN:
		for ( i = 0 ; i < 3 ; i++ ) {''')
# prox: detonate the mines stuck to players too
sub(W, '''		// only mines lying around, not the ones stuck to players
		if ( ent->s.weapon == WP_PROX_LAUNCHER && m->s.weapon == WP_PROX_LAUNCHER && !( m->r.svFlags & SVF_NOCLIENT ) ) {''', '''		// mines stuck to players go off right away
		if ( ent->s.weapon == WP_PROX_LAUNCHER && m->s.weapon == WP_PROX_LAUNCHER && ( m->r.svFlags & SVF_NOCLIENT ) ) {
			m->nextthink = level.time;
			continue;
		}
		if ( ent->s.weapon == WP_PROX_LAUNCHER && m->s.weapon == WP_PROX_LAUNCHER ) {''')

# ---------------- g_spawn.c: powerup swaps have their own switch ----------------
sub(g('g_spawn.c'), '''static void G_RearmClassname( gentity_t *ent ) {
	int		i;

	if ( g_rearmItems.integer <= 0 ) {
		return;
	}
	for ( i = 0 ; i < ARRAY_LEN( rearmSwaps ) ; i++ ) {
		if ( !strcmp( ent->classname, rearmSwaps[i][0] ) ) {
			if ( g_rearmItems.integer >= 2 || ( level.rearmSpawnCount[i]++ & 1 ) ) {''', '''#define REARM_WEAPON_SWAPS	6		// the first entries are weapons / ammo, the rest powerups

static void G_RearmClassname( gentity_t *ent ) {
	int		i;
	qboolean	on;

	for ( i = 0 ; i < ARRAY_LEN( rearmSwaps ) ; i++ ) {
		if ( !strcmp( ent->classname, rearmSwaps[i][0] ) ) {
			on = i < REARM_WEAPON_SWAPS ? g_rearmItems.integer > 0 : G_RearmOn( RS_POWERUPS );
			if ( !on ) {
				return;
			}
			if ( ( i < REARM_WEAPON_SWAPS && g_rearmItems.integer >= 2 ) || ( level.rearmSpawnCount[i]++ & 1 ) ) {''')
print('v2 game ok')
