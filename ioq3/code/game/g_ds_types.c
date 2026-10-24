/*
===========================================================================
REARMED -- round 4 ENEMY ARCHETYPES (en-types), game side. DESCENT fight floors only.

- the floor's type mix (G_DS_TypesPlan, from G_RL_InitBots): act 1 has one or two typed bots
  among plain ones, later acts more types and combos; elites may carry a type, ambush
  mini-bosses (act 2+) a shield or blinks. Never on boss / vault floors. dstypes forces a mix.
- the kit at every spawn (G_DS_TypesSpawn): g_enemy[].arch, weapons, armour, the mimic's copy.
- the power budget (bg_ds_types.c): health / damage / speed per type.
- the type mechanics that live outside the bot brain: medic heal beams, kamikaze fuse and
  blast (and the pop when it is killed first), blinker teleports (never within 400 units of
  the player), summoner raise rituals (fodder via ds-surprise), the shield's frontal block.
- the effect entities (RM_ENT_DSTYPE) the cgame draws (cgame/cg_ds_types.c).
Bot behaviour (perches, charges, the sniper's glint, shield cycles): ai_types.c.
Switches: g_dsTypes 0/1 (F10 BOTS tab), g_dsTypesForce (dev mix), g_dsTypesDebug (log).
===========================================================================
*/
#include "g_local.h"
#include "bg_ds_types.h"

#define DST_MAX_PLAN		16
#define DST_RAISE_NAMES		6

typedef struct {
	char	name[MAX_NETNAME];
	int		arch;
	int		role;
} dstPlan_t;

typedef struct {
	qboolean	on;				// a DESCENT fight floor with g_dsTypes 1
	int			floor, act;
	unsigned	rng;
	int			numPlan;
	dstPlan_t	plan[DST_MAX_PLAN];
	int			force[MAX_CLIENTS];			// dstype: arch + 1 for this client's next spawn (0 none)

	// per client
	gentity_t	*fx[MAX_CLIENTS];			// medic beam / summoner ritual
	int			patient[MAX_CLIENTS], patientCheck[MAX_CLIENTS], healTick[MAX_CLIENTS], healCarry[MAX_CLIENTS];
	int			fuseEnd[MAX_CLIENTS];		// kamikaze: level.time it blows (0 none)
	qboolean	exploded[MAX_CLIENTS];
	int			nextBlink[MAX_CLIENTS];
	int			ritualEnd[MAX_CLIENTS], ritualCorpse[MAX_CLIENTS];
	int			nextRaise[MAX_CLIENTS];
	int			lastBlockFx[MAX_CLIENTS];
	int			mimicWeapon[MAX_CLIENTS], mimicLevel[MAX_CLIENTS];
	qboolean	mimicPending[MAX_CLIENTS];

	// summoned skeletons: name slot -> summoner client (-1 free)
	int			raiseOwner[DST_RAISE_NAMES];
	int			raiseAt[DST_RAISE_NAMES];

	// evidence counters (dstypestats, the floor end log)
	int			blinks, blinkMin, blinkFails;
	int			shieldFront, shieldPass, shieldDmgIn, shieldDmgOut;
	int			healed;
	int			booms, pops, boomHits;
	int			raises, rituals;
	int			snipes;
	int			nextReport;

	// en-tune: type adaptation (bg_ds_types.h)
	qboolean	adaptCount;					// this floor's deaths by killer type count (not daily / practice)
	int			adaptMask;					// 1 << arch of the adapted types this run
	int			adaptDeaths[DSAR_NUM];		// the player's deaths by killer type on this floor
	float		adaptW[DSAR_NUM];
	int			adaptPairs;					// kamikaze pairs planned (log)
	// en-tune: juggernaut hit box frames (evidence)
	int			juggBig, juggTall, juggPlain;
	qboolean	juggScaled[MAX_CLIENTS];	// this frame's box is the big one (headshot line)
} dstState_t;

static dstState_t	dst;
static vmCvar_t		dstCvOn, dstCvForce, dstCvDebug, dstCvShots;
static vmCvar_t		dstCvAdapt, dstCvAdaptNow, dstCvJuggBox;
static int			dstJuggLogNext, dstJuggLogLife[MAX_CLIENTS];		// en-tune: JuggMove log
static vec3_t		dstJuggLogPos[MAX_CLIENTS];

// g_dsTypesShots (tests): the game turns the player's view to a type's tell and takes a screenshot
#define DST_MAX_SHOTS	8
#define DST_SHOT_KINDS	10
static int			dstShotAt[DST_MAX_SHOTS], dstShotWho[DST_MAX_SHOTS], dstShotKind[DST_MAX_SHOTS];
static int			dstShotsTaken[DST_SHOT_KINDS];
static const char	*dstShotNames[DST_SHOT_KINDS] = { "look", "snipe", "fuse", "boom", "blink", "ritual", "shield", "heal", "spawn", "raise" };

static const char *dstRaiseNames[DST_RAISE_NAMES] = { "Risen", "Wight", "Revenant", "Mourner", "Lich", "Hollow" };

/*
===============================================================================

HELPERS

===============================================================================
*/

static void DST_Cvars( void ) {
	trap_Cvar_Register( &dstCvOn, "g_dsTypes", "1", CVAR_ARCHIVE | CVAR_SERVERINFO );
	trap_Cvar_Register( &dstCvForce, "g_dsTypesForce", "", 0 );
	trap_Cvar_Register( &dstCvDebug, "g_dsTypesDebug", "0", 0 );
	trap_Cvar_Register( &dstCvShots, "g_dsTypesShots", "0", 0 );
	trap_Cvar_Register( &dstCvAdapt, "g_dsTypeAdapt", "1", CVAR_ARCHIVE );		// en-tune
	trap_Cvar_Register( &dstCvAdaptNow, "g_dsTypeAdaptNow", "", CVAR_SERVERINFO );	// en-tune: "<floor>:<arch>,.." for cgame
	trap_Cvar_Register( &dstCvJuggBox, "g_dsJuggBox", "1", 0 );		// en-tune: 0 = the juggernaut's plain hit box (tests)
}

/*
===============================================================================

TYPE ADAPTATION (en-tune): the archetypes that killed the player most in earlier runs

===============================================================================
*/
static int DST_Rand( void );

static void DST_ParseFloats( const char *s, float *out, int n, float mul ) {
	int		i;

	for ( i = 0 ; i < n ; i++ ) {
		out[i] = 0;
	}
	for ( i = 0 ; s && *s && i < n ; i++ ) {
		out[i] = atoi( s ) * mul;
		while ( *s && *s != ',' ) {
			s++;
		}
		if ( *s ) {
			s++;
		}
	}
}

static void DST_AdaptInit( void ) {
	float	cur[DSAR_NUM];
	int		deaths[DSAR_NUM], list[DST_ADAPT_MAX], n, i;
	char	buf[64];

	dst.adaptMask = 0;
	memset( dst.adaptDeaths, 0, sizeof( dst.adaptDeaths ) );
	dst.adaptCount = dst.on && !G_RL_NoSave();
	if ( !dst.adaptCount || !dstCvAdapt.integer ) {
		trap_Cvar_Set( "g_dsTypeAdaptNow", "" );
		return;
	}
	// the weights this run plays with: meta tad_w (x10), rolled once more if the meta's
	// tad_cur still belongs to the last run (the UI rolls it at this run's first floor merge)
	DST_ParseFloats( G_RL_MetaGet( "tad_w" ), dst.adaptW, DSAR_NUM, 0.1f );
	if ( G_RL_MetaGetInt( "tad_seed", 0 ) != atoi( G_RL_RunGet( "seed" ) ) ) {
		DST_ParseFloats( G_RL_MetaGet( "tad_cur" ), cur, DSAR_NUM, 1.0f );
		for ( i = 0 ; i < DSAR_NUM ; i++ ) {
			deaths[i] = (int)cur[i];
		}
		BG_DS_TypeAdaptRoll( dst.adaptW, deaths );
	}
	n = BG_DS_TypeAdaptPick( dst.adaptW, list );
	Com_sprintf( buf, sizeof( buf ), "%i:", dst.floor );
	for ( i = 0 ; i < n ; i++ ) {
		dst.adaptMask |= 1 << list[i];
		Q_strcat( buf, sizeof( buf ), va( i ? ",%i" : "%i", list[i] ) );
	}
	trap_Cvar_Set( "g_dsTypeAdaptNow", n ? buf : "" );
	buf[0] = 0;
	for ( i = 1 ; i < DSAR_NUM ; i++ ) {
		if ( dst.adaptW[i] > 0 ) {
			Q_strcat( buf, sizeof( buf ), va( " %s %.1f", ds_types[i].name, dst.adaptW[i] ) );
		}
	}
	G_LogPrintf( "DSTypeAdapt: floor %i adapted %s %s (weights:%s)\n", dst.floor, n ? ds_types[list[0]].name : "none",
		n > 1 ? ds_types[list[1]].name : "", buf[0] ? buf : " none" );
}

/*
=================
G_DS_TypeAdapted

This run's adaptation for a type (ai_types.c, g_ds_types.c; never in the daily or practice).
=================
*/
qboolean G_DS_TypeAdapted( int arch ) {
	return dst.on && arch > 0 && arch < DSAR_NUM && ( dst.adaptMask & ( 1 << arch ) );
}

// KAMIKAZE adapted: once per floor, a planned kamikaze gets a partner (a plain bot of the plan)
static void DST_AdaptPair( void ) {
	int		i, c = 0, plain[DST_MAX_PLAN], np = 0;

	if ( !G_DS_TypeAdapted( DSAR_KAMIKAZE ) ) {
		return;
	}
	for ( i = 0 ; i < dst.numPlan ; i++ ) {
		if ( dst.plan[i].arch == DSAR_KAMIKAZE ) {
			c++;
		} else if ( !dst.plan[i].arch && dst.plan[i].role != 3 ) {
			plain[np++] = i;
		}
	}
	if ( c != 1 || !np ) {
		return;
	}
	dst.plan[plain[DST_Rand() % np]].arch = DSAR_KAMIKAZE;
	dst.adaptPairs++;
}

static gentity_t *DST_Human( void );

static void DST_Shot( int kind, int client, int delay ) {
	int		i;

	if ( !dstCvShots.integer || kind < 0 || kind >= DST_SHOT_KINDS || dstShotsTaken[kind] >= dstCvShots.integer ) {
		return;
	}
	for ( i = 0 ; i < DST_MAX_SHOTS ; i++ ) {
		if ( !dstShotAt[i] ) {
			dstShotAt[i] = level.time + delay;
			dstShotWho[i] = client;
			dstShotKind[i] = kind;
			dstShotsTaken[kind]++;
			return;
		}
	}
}

// the player looks at the bot (chest height) and a screenshot follows a few frames later
static void DST_ShotFrame( void ) {
	gentity_t	*h = DST_Human(), *e;
	vec3_t		eye, tg, d, ang;
	trace_t		tr;
	int			i;

	for ( i = 0 ; i < DST_MAX_SHOTS ; i++ ) {
		if ( !dstShotAt[i] || level.time < dstShotAt[i] ) {
			continue;
		}
		dstShotAt[i] = 0;
		if ( !h || !h->client ) {
			return;
		}
		e = &g_entities[dstShotWho[i]];
		VectorCopy( h->client->ps.origin, eye );
		eye[2] += h->client->ps.viewheight;
		VectorCopy( e->r.currentOrigin, tg );
		tg[2] += 10;
		trap_Trace( &tr, eye, NULL, NULL, tg, h - g_entities, MASK_SOLID );
		if ( tr.fraction < 1.0f ) {
			// out of sight: the (god mode) tester moves to a spot 200-300 units from it first
			static vec3_t	pmins = { -15, -15, -24 }, pmaxs = { 15, 15, 32 };
			vec3_t			p, dn;
			int				k;

			for ( k = 0 ; k < 16 ; k++ ) {
				p[0] = tg[0] + cos( k * M_PI / 8 ) * 280;
				p[1] = tg[1] + sin( k * M_PI / 8 ) * 280;
				p[2] = tg[2] + 16;
				trap_Trace( &tr, tg, pmins, pmaxs, p, e - g_entities, MASK_PLAYERSOLID );
				if ( tr.startsolid || tr.fraction < 0.7f ) {
					continue;
				}
				VectorCopy( tr.endpos, p );
				VectorCopy( p, dn );
				dn[2] -= 200;
				trap_Trace( &tr, p, pmins, pmaxs, dn, e - g_entities, MASK_PLAYERSOLID );
				if ( tr.fraction >= 1.0f || tr.startsolid ) {
					continue;
				}
				VectorCopy( tr.endpos, h->client->ps.origin );
				VectorClear( h->client->ps.velocity );
				h->client->ps.eFlags ^= EF_TELEPORT_BIT;
				VectorCopy( tr.endpos, eye );
				eye[2] += h->client->ps.viewheight;
				break;
			}
			trap_Trace( &tr, eye, NULL, NULL, tg, h - g_entities, MASK_SOLID );
		}
		VectorSubtract( tg, eye, d );
		vectoangles( d, ang );
		SetClientViewAngle( h, ang );
		trap_SendConsoleCommand( EXEC_INSERT, "wait 4; screenshotJPEG\n" );
		G_LogPrintf( "DSTypes: screenshot %s of %i (%s, %s) at %.0f units%s\n", dstShotNames[dstShotKind[i]], dstShotWho[i],
			e->client ? e->client->pers.netname : "?", e->client ? BG_DS_TypeName( g_enemy[dstShotWho[i]].arch ) : "",
			Distance( eye, tg ), tr.fraction < 1.0f ? " (blocked view)" : "" );
		return;		// one a frame
	}
}

static int DST_Rand( void ) {
	dst.rng = dst.rng * 1103515245u + 12345u;
	return (int)( ( dst.rng >> 8 ) & 0x7fffff );
}

static qboolean DST_Alive( gentity_t *e ) {
	return e && e->inuse && e->client && e->health > 0 && e->client->ps.pm_type == PM_NORMAL
		&& e->client->sess.sessionTeam != TEAM_SPECTATOR && e->client->pers.connected == CON_CONNECTED;
}

static int DST_Arch( int n ) {
	if ( !dst.on || n < 0 || n >= MAX_CLIENTS ) {
		return 0;
	}
	return g_enemy[n].arch;
}

static void DST_Eye( gentity_t *e, vec3_t out ) {
	VectorCopy( e->r.currentOrigin, out );
	out[2] += e->client ? e->client->ps.viewheight : 0;
}

// the human player (or the boss sim bot), NULL none
static gentity_t *DST_Human( void ) {
	int		h = G_RL_HumanClient();

	return h >= 0 ? &g_entities[h] : NULL;
}

// distance to the nearest living human (99999 none)
static float DST_HumanDist( const vec3_t p ) {
	float	d, best = 99999.0f;
	int		i;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( DST_Alive( &g_entities[i] ) && G_RL_IsPlayer( &g_entities[i] ) ) {
			d = Distance( p, g_entities[i].r.currentOrigin );
			if ( d < best ) {
				best = d;
			}
		}
	}
	return best;
}

static gentity_t *DST_Fx( int kind, int client, const vec3_t org, int life ) {
	gentity_t	*ent = G_Spawn();

	ent->classname = "ds_typefx";
	ent->s.eType = ET_GENERAL;
	ent->s.generic1 = RM_ENT_DSTYPE;
	ent->s.modelindex2 = kind;
	ent->s.otherEntityNum = client;
	ent->s.otherEntityNum2 = ENTITYNUM_NONE;
	ent->s.time = level.time;
	ent->s.time2 = life > 0 ? level.time + life : 0;
	G_SetOrigin( ent, (float *)org );
	if ( life > 0 ) {
		ent->think = G_FreeEntity;
		ent->nextthink = level.time + life + 200;
	}
	trap_LinkEntity( ent );
	return ent;
}

static void DST_FxOff( int n ) {
	if ( dst.fx[n] ) {
		G_FreeEntity( dst.fx[n] );
		dst.fx[n] = NULL;
	}
}

static void DST_Telegraph( int n, int msec ) {
	G_EN_SetFlag( n, DSEF_TELEGRAPH, msec > 0 );
	g_enemy[n].telegraphUntil = msec > 0 ? level.time + msec : 0;
}

// someone else's wind-up (an affix PULL) on this bot right now
qboolean G_DS_TypesOtherTelegraph( int n ) {
	if ( n < 0 || n >= MAX_CLIENTS ) {
		return qfalse;
	}
	if ( dst.fuseEnd[n] || dst.ritualEnd[n] ) {
		return qfalse;		// (that one is ours)
	}
	return ( g_enemy[n].flags & DSEF_TELEGRAPH ) || g_enemy[n].telegraphUntil > level.time;
}

/*
===============================================================================

INIT, PLAN

===============================================================================
*/

void G_DS_TypesInit( void ) {
	int		i;

	DST_Cvars();
	memset( &dst, 0, sizeof( dst ) );
	dstJuggLogNext = 0;
	memset( dstJuggLogLife, 0, sizeof( dstJuggLogLife ) );
	if ( dstCvAdaptNow.string[0] ) {
		trap_Cvar_Set( "g_dsTypeAdaptNow", "" );		// en-tune: set again below on a fight floor
	}
	memset( dstShotAt, 0, sizeof( dstShotAt ) );
	memset( dstShotsTaken, 0, sizeof( dstShotsTaken ) );
	for ( i = 0 ; i < MAX_CLIENTS ; i++ ) {
		dst.patient[i] = -1;
	}
	for ( i = 0 ; i < DST_RAISE_NAMES ; i++ ) {
		dst.raiseOwner[i] = -1;
	}
	dst.blinkMin = 99999;
	if ( !G_RL_Active() || !dstCvOn.integer || G_DS_IsBossFloor() || G_DS_IsVaultFloor() ) {
		return;
	}
	dst.on = qtrue;
	dst.floor = G_RL_Floor();
	if ( dst.floor < 1 ) {
		dst.floor = 1;
	}
	dst.act = DS_ActForFloor( dst.floor );
	dst.rng = (unsigned)atoi( G_RL_RunGet( "seed" ) ) * 2654435761u + (unsigned)dst.floor * 40503u + 977u;
	DST_AdaptInit();		// en-tune: last runs' killers adapt (g_dsTypeAdapt)
}

qboolean G_DS_TypesOn( void ) {
	return dst.on;
}

// how many typed bots among n planned (the elite counts if it got one)
static int DST_TypedCount( int nb ) {
	int		t;

	switch ( dst.act ) {
	case 1:
		t = dst.floor <= 1 ? 1 : 1 + DST_Rand() % 2;
		break;
	case 2:
		t = ( nb + 1 ) / 2;
		break;
	case 3:
		t = ( nb * 2 + 2 ) / 3;
		break;
	default:
		t = nb >= 4 ? nb - 1 : nb;
		break;
	}
	if ( t < 1 ) {
		t = 1;
	}
	if ( dst.act <= 1 && nb >= 2 && t >= nb ) {
		t = nb - 1;		// act 1: types stand out among plain bots
	}
	if ( t > nb ) {
		t = nb;
	}
	return t;
}

static int DST_CountOf( int arch ) {
	int		i, c = 0;

	for ( i = 0 ; i < dst.numPlan ; i++ ) {
		if ( dst.plan[i].arch == arch ) {
			c++;
		}
	}
	return c;
}

/*
Pick a type for a planned bot. Elites lean to the sturdy / tricky types; combos from act 3:
a medic wants a shield or a juggernaut to heal, a summoner brings kamikazes, a sniper hides
behind a shield.
*/
static int DST_PickType( qboolean elite, int nb ) {
	int		w[DSAR_NUM];
	int		a, sum = 0, r, cap;

	for ( a = 1 ; a < DSAR_NUM ; a++ ) {
		w[a] = 0;
		if ( ds_types[a].minAct > dst.act ) {
			continue;
		}
		w[a] = 10;
		cap = dst.act >= 4 ? 2 : 1;
		if ( a == DSAR_KAMIKAZE && dst.act >= 2 ) {
			cap = 2;
		}
		if ( a == DSAR_SUMMONER || a == DSAR_MIMIC || a == DSAR_MEDIC ) {
			cap = 1;
		}
		if ( DST_CountOf( a ) >= cap ) {
			w[a] = 0;
			continue;
		}
		if ( a == DSAR_MEDIC && nb < 3 ) {
			w[a] = 0;		// a medic needs others to heal
			continue;
		}
		if ( elite ) {
			if ( a == DSAR_KAMIKAZE ) {
				w[a] = 0;
			} else if ( a == DSAR_JUGGERNAUT || a == DSAR_SHIELD || a == DSAR_BLINKER || a == DSAR_MIMIC ) {
				w[a] = 22;
			}
		}
		if ( dst.act >= 3 ) {
			if ( DST_CountOf( DSAR_MEDIC ) && ( a == DSAR_SHIELD || a == DSAR_JUGGERNAUT ) ) {
				w[a] *= 3;
			}
			if ( DST_CountOf( DSAR_SUMMONER ) && a == DSAR_KAMIKAZE ) {
				w[a] *= 2;
			}
			if ( DST_CountOf( DSAR_SNIPER ) && a == DSAR_SHIELD ) {
				w[a] *= 2;
			}
		}
		if ( G_DS_TypeAdapted( a ) ) {
			w[a] = (int)( w[a] * DST_ADAPT_PICK );		// en-tune: an adapted type shows up a little more often
		}
		sum += w[a];
	}
	if ( sum <= 0 ) {
		return 0;
	}
	r = DST_Rand() % sum;
	for ( a = 1 ; a < DSAR_NUM ; a++ ) {
		if ( r < w[a] ) {
			return a;
		}
		r -= w[a];
	}
	return 0;
}

static void DST_PreloadModel( int arch ) {
	const char	*m;

	if ( arch <= 0 || arch >= DSAR_NUM ) {
		return;
	}
	m = ds_types[arch].model;
	if ( m[0] && m[0] != '*' ) {
		G_PreloadAddModel( m, m );
	}
}

/*
=================
G_DS_TypesPlan

G_RL_InitBots, before the preload list goes out: which planned bot gets which type.
g_dsTypesForce "sniper,medic,none,shield" overrides (in plan order).
=================
*/
void G_DS_TypesPlan( void ) {
	char		buf[MAX_CVAR_VALUE_STRING], log[512], *s, *tok;
	const char	*nm;
	int			i, n, role, nb, typed, k, a, idx[DST_MAX_PLAN], j, t;
	qboolean	forced = qfalse;

	dst.numPlan = 0;
	if ( !dst.on ) {
		return;
	}
	n = G_RL_PlanNum();
	for ( i = 0 ; i < n && dst.numPlan < DST_MAX_PLAN ; i++ ) {
		nm = G_RL_PlanName( i, &role );
		if ( !nm || role == 4 ) {
			continue;
		}
		G_EN_CleanName( nm, dst.plan[dst.numPlan].name, sizeof( dst.plan[0].name ) );
		dst.plan[dst.numPlan].role = role;
		dst.plan[dst.numPlan].arch = 0;
		dst.numPlan++;
	}
	nb = dst.numPlan;
	if ( !nb ) {
		return;
	}
	// a forced mix (dev, tests)
	trap_Cvar_VariableStringBuffer( "g_dsTypesForce", buf, sizeof( buf ) );
	if ( buf[0] ) {
		forced = qtrue;
		s = buf;
		for ( i = 0 ; i < nb ; i++ ) {
			while ( *s == ',' || *s == ' ' ) {
				s++;
			}
			if ( !*s ) {
				break;
			}
			tok = s;
			while ( *s && *s != ',' && *s != ' ' ) {
				s++;
			}
			if ( *s ) {
				*s++ = 0;
			}
			a = BG_DS_TypeByName( tok );
			dst.plan[i].arch = a > 0 ? a : 0;
		}
	} else {
		typed = DST_TypedCount( nb );
		// the elite first (act 1: 30%, act 2: 60%, act 3: 85%, then always)
		k = 0;
		for ( i = 0 ; i < nb ; i++ ) {
			if ( dst.plan[i].role == 3 ) {
				t = dst.act <= 1 ? 30 : dst.act == 2 ? 60 : dst.act == 3 ? 85 : 100;
				if ( DST_Rand() % 100 < t ) {
					dst.plan[i].arch = DST_PickType( qtrue, nb );
					if ( dst.plan[i].arch ) {
						k++;
					}
				}
			}
		}
		// then random plain bots
		for ( i = 0, j = 0 ; i < nb ; i++ ) {
			if ( !dst.plan[i].arch && dst.plan[i].role != 3 ) {
				idx[j++] = i;
			}
		}
		for ( i = 0 ; i < j ; i++ ) {
			t = i + DST_Rand() % ( j - i );
			a = idx[i]; idx[i] = idx[t]; idx[t] = a;
		}
		for ( i = 0 ; i < j && k < typed ; i++ ) {
			dst.plan[idx[i]].arch = DST_PickType( qfalse, nb );
			if ( dst.plan[idx[i]].arch ) {
				k++;
			}
		}
		DST_AdaptPair();		// en-tune: adapted kamikazes come in pairs (once per floor)
	}
	log[0] = 0;
	for ( i = 0 ; i < nb ; i++ ) {
		DST_PreloadModel( dst.plan[i].arch );
		Q_strcat( log, sizeof( log ), va( " %s=%s%s", dst.plan[i].name, dst.plan[i].arch ? ds_types[dst.plan[i].arch].name : "plain",
			dst.plan[i].role == 3 ? "(elite)" : "" ) );
	}
	G_LogPrintf( "DSTypes: floor %i act %i%s plan:%s\n", dst.floor, dst.act, forced ? " (forced)" : "", log );
}

static int DST_PlanArchByName( const char *clean ) {
	int		i;

	for ( i = 0 ; i < dst.numPlan ; i++ ) {
		if ( !Q_stricmp( clean, dst.plan[i].name ) ) {
			return dst.plan[i].arch;
		}
	}
	return 0;
}

// ds-intro: the type of plan index i (G_RL_PlanName order; the boss has none)
int G_DS_TypesPlanArch( int planIndex ) {
	const char	*nm;
	char		clean[MAX_NETNAME];
	int			role;

	if ( !dst.on ) {
		return 0;
	}
	nm = G_RL_PlanName( planIndex, &role );
	if ( !nm || role == 4 ) {
		return 0;
	}
	G_EN_CleanName( nm, clean, sizeof( clean ) );
	return DST_PlanArchByName( clean );
}

/*
=================
G_DS_TypeModel

ClientUserinfoChanged: the model a typed bot wears ("visor/default"), NULL = its own.
The mimic wears the player's model (the cgame darkens it).
=================
*/
const char *G_DS_TypeModel( gentity_t *ent ) {
	static char	model[MAX_QPATH];
	char		clean[MAX_NETNAME], ui[MAX_INFO_STRING];
	gentity_t	*h;
	const char	*m;
	int			n, arch;

	if ( !dst.on || !ent || !ent->client || !( ent->r.svFlags & SVF_BOT ) || G_RL_IsPlayer( ent ) ) {
		return NULL;
	}
	n = ent - g_entities;
	if ( n < 0 || n >= MAX_CLIENTS ) {
		return NULL;
	}
	if ( dst.force[n] ) {
		arch = dst.force[n] - 1;
	} else if ( g_enemy[n].arch ) {
		arch = g_enemy[n].arch;
	} else {
		G_EN_CleanName( ent->client->pers.netname, clean, sizeof( clean ) );
		arch = DST_PlanArchByName( clean );
	}
	if ( arch <= 0 || arch >= DSAR_NUM ) {
		return NULL;
	}
	if ( G_DS_SurpriseExtra( n ) ) {
		return NULL;		// an ambusher keeps its own look (the type shows in its tells)
	}
	m = ds_types[arch].model;
	if ( !m[0] ) {
		return NULL;
	}
	if ( m[0] == '*' ) {
		h = DST_Human();
		if ( !h || !h->client ) {
			return NULL;
		}
		m = G_DS_ForcedModel( h );
		if ( !m ) {
			trap_GetUserinfo( h - g_entities, ui, sizeof( ui ) );
			m = Info_ValueForKey( ui, "model" );
		}
		if ( !m || !m[0] ) {
			return NULL;
		}
	}
	Q_strncpyz( model, m, sizeof( model ) );
	return model;
}

/*
===============================================================================

SPAWN: the kit

===============================================================================
*/

static void DST_Give( gentity_t *ent, int w, int ammo ) {
	if ( w <= WP_NONE || w >= WP_NUM_WEAPONS ) {
		return;
	}
	ent->client->ps.stats[STAT_WEAPONS] |= 1 << w;
	if ( w == WP_GAUNTLET ) {
		ent->client->ps.ammo[w] = -1;
	} else if ( ent->client->ps.ammo[w] < ammo ) {
		ent->client->ps.ammo[w] = ammo;
	}
}

static int DST_Ammo( int w ) {
	switch ( w ) {
	case WP_RAILGUN:			return 25;
	case WP_SHOTGUN:			return 40;
	case WP_PLASMAGUN:			return 120;
	case WP_LIGHTNING:			return 150;
	case WP_GRENADE_LAUNCHER:	return 25;
	case WP_ROCKET_LAUNCHER:	return 20;
	case WP_MACHINEGUN:			return 150;
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case WP_CHAINGUN:			return 300;
#endif
	default:					return 50;
	}
}

// the player's best weapon (highest level; the one in hand wins a tie)
static int DST_HumanBest( gentity_t *h, int *lvlOut ) {
	int		w, best = WP_NONE, bestL = -1, l, have;

	*lvlOut = 1;
	if ( !h || !h->client ) {
		return WP_NONE;
	}
	have = h->client->ps.stats[STAT_WEAPONS];
	for ( w = WP_MACHINEGUN ; w < DS_NUM_WEAPONS && w < WP_NUM_WEAPONS ; w++ ) {
		if ( !( have & ( 1 << w ) ) || !DS_WEAPON_LEVELS( w ) ) {
			continue;
		}
		l = G_DS_WeaponLevel( h, w );
		if ( l > bestL || ( l == bestL && w == h->client->ps.weapon ) ) {
			bestL = l;
			best = w;
		}
	}
	*lvlOut = bestL > 0 ? bestL : 1;
	return best;
}

static void DST_MimicKit( gentity_t *ent, int n ) {
	int		levels[DS_NUM_WEAPONS];
	int		w, lvl;

	dst.mimicPending[n] = !DST_Human() || !DST_Alive( DST_Human() );		// the player isn't in yet: copy again once they are
	w = DST_HumanBest( DST_Human(), &lvl );
	if ( w == WP_NONE ) {
		w = WP_MACHINEGUN;
	}
	DST_Give( ent, w, DST_Ammo( w ) * 2 );
	ent->client->ps.weapon = w;
	memset( levels, 0, sizeof( levels ) );
	levels[w] = lvl;
	G_DS_SetWeaponLevels( ent, levels, 0 );
	dst.mimicWeapon[n] = w;
	dst.mimicLevel[n] = lvl;
	G_LogPrintf( "DSTypes: mimic %i (%s) copies weapon %i (%s) at level %i (bot level now %i)\n", n, ent->client->pers.netname,
		w, ds_weaponNames[w], lvl, G_DS_WeaponLevel( ent, w ) );
}

/*
=================
G_DS_TypesSpawn

G_RL_SpawnLoadout (after the other DESCENT spawn hooks): the bot's type, weapons, armour
=================
*/
void G_DS_TypesSpawn( gentity_t *ent ) {
	char		clean[MAX_NETNAME];
	int			n, arch, old, w;
	gclient_t	*cl;

	if ( !ent || !ent->client ) {
		return;
	}
	n = ent - g_entities;
	if ( n < 0 || n >= MAX_CLIENTS ) {
		return;
	}
	cl = ent->client;
	old = g_enemy[n].arch;
	// transient state from the last life
	dst.fuseEnd[n] = 0;
	dst.exploded[n] = qfalse;
	dst.ritualEnd[n] = 0;
	dst.patient[n] = -1;
	DST_FxOff( n );
	G_EN_SetFlag( n, DSEF_SHIELDUP, qfalse );
	if ( g_enemy[n].flags & DSEF_TELEGRAPH ) {
		DST_Telegraph( n, 0 );
	}
	if ( !dst.on || !( ent->r.svFlags & SVF_BOT ) || G_RL_IsPlayer( ent ) || n == G_DS_BossClient() ) {
		if ( old ) {
			g_enemy[n].arch = 0;
			G_EN_MarkDirty();
		}
		return;
	}
	G_EN_CleanName( cl->pers.netname, clean, sizeof( clean ) );
	if ( dst.force[n] ) {
		arch = dst.force[n] - 1;
	} else {
		arch = old ? old : DST_PlanArchByName( clean );
		// an ambush mini-boss (act 2+): sometimes a shield or blinks (it keeps its own guns and look)
		if ( !arch && G_DS_SurpriseExtra( n ) && G_RL_ClientRole( ent ) == 3 && dst.act >= 2 && DST_Rand() % 100 < 50 ) {
			arch = DST_Rand() % 2 ? DSAR_SHIELD : DSAR_BLINKER;
			G_LogPrintf( "DSTypes: ambusher %i (%s) is a %s\n", n, clean, ds_types[arch].name );
		} else if ( G_DS_SurpriseExtra( n ) && !old ) {
			arch = 0;		// ghosts and fodder stay plain
		}
	}
	if ( arch < 0 || arch >= DSAR_NUM ) {
		arch = 0;
	}
	if ( arch != old ) {
		g_enemy[n].arch = arch;
		G_EN_MarkDirty();
	}
	if ( !arch ) {
		return;
	}
	if ( G_DS_SurpriseExtra( n ) ) {
		// the ambusher's guns are its own
	} else if ( arch == DSAR_MIMIC ) {
		DST_MimicKit( ent, n );
	} else if ( arch == DSAR_KAMIKAZE ) {
		cl->ps.stats[STAT_WEAPONS] = 1 << WP_GAUNTLET;
		cl->ps.ammo[WP_GAUNTLET] = -1;
		cl->ps.weapon = WP_GAUNTLET;
	} else {
		w = ds_types[arch].weapon;
		DST_Give( ent, w, DST_Ammo( w ) );
		DST_Give( ent, ds_types[arch].weapon2, DST_Ammo( ds_types[arch].weapon2 ) );
		if ( w > WP_NONE ) {
			cl->ps.weapon = w;
		}
	}
	if ( ds_types[arch].armor > 0 ) {
		cl->ps.stats[STAT_ARMOR] += ds_types[arch].armor;
	}
	dst.nextBlink[n] = level.time + 2500 + DST_Rand() % 2000;
	dst.nextRaise[n] = level.time + 4000;
	if ( arch == DSAR_SHIELD ) {
		G_EN_SetFlag( n, DSEF_SHIELDUP, qtrue );
		DST_Shot( 6, n, 2500 );
	} else {
		DST_Shot( 8, n, 2000 );
	}
	if ( dstCvDebug.integer || old != arch ) {
		G_LogPrintf( "DSTypes: spawn %i (%s) %s weapons 0x%x armor %i maxhp %i\n", n, clean, ds_types[arch].name,
			cl->ps.stats[STAT_WEAPONS], cl->ps.stats[STAT_ARMOR], cl->ps.stats[STAT_MAX_HEALTH] );
	}
}

/*
===============================================================================

BUDGET HOOKS (g_roguelike.c)

===============================================================================
*/

int G_DS_TypesHealthBonus( gentity_t *ent, int bonus ) {
	int		a, base;

	if ( !ent || !ent->client ) {
		return 0;
	}
	a = DST_Arch( ent - g_entities );
	if ( !a ) {
		return 0;
	}
	base = ent->client->pers.maxHealth > 0 ? ent->client->pers.maxHealth : 100;
	if ( G_DS_TypeAdapted( a ) ) {
		return (int)( ( base + bonus ) * ( ds_types[a].health * DST_ADAPT_HP - 1.0f ) );	// en-tune: pays for its trick
	}
	return (int)( ( base + bonus ) * ( ds_types[a].health - 1.0f ) );
}

float G_DS_TypesDamageScale( gentity_t *targ, gentity_t *attacker ) {
	int		a;

	if ( !attacker || !attacker->client || attacker == targ ) {
		return 1.0f;
	}
	a = DST_Arch( attacker - g_entities );
	return a ? ds_types[a].damage : 1.0f;
}

float G_DS_TypesSpeedScale( gentity_t *ent ) {
	int		n, a;
	float	s;

	if ( !ent || !ent->client ) {
		return 1.0f;
	}
	n = ent - g_entities;
	a = DST_Arch( n );
	if ( !a ) {
		return 1.0f;
	}
	s = ds_types[a].speed;
	if ( a == DSAR_SHIELD && ( g_enemy[n].flags & DSEF_SHIELDUP ) ) {
		s *= 0.85f;
	}
	if ( a == DSAR_KAMIKAZE && dst.fuseEnd[n] ) {
		s *= 1.1f;
	}
	if ( a == DSAR_SUMMONER && dst.ritualEnd[n] ) {
		s = 0.05f;		// rooted while it raises the dead
	}
	return s;
}

/*
=================
G_DS_TypesDamage

G_Damage, after the other DESCENT damage hooks: the shield bearer's raised shield stops most of
a hit from its front (+-60 degrees). Splash and hits from behind / the sides go through.
=================
*/
int G_DS_TypesDamage( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, int damage, int dflags, int mod, vec3_t point ) {
	vec3_t		fwd, to, src, ang, at;
	int			n, out;
	float		d;
	gentity_t	*fx;

	if ( !dst.on || !targ || !targ->client || damage <= 0 ) {
		return damage;
	}
	n = targ - g_entities;
	if ( DST_Arch( n ) != DSAR_SHIELD || targ->health <= 0 ) {
		return damage;
	}
	// en-review: spare[0] says "THIS hit was blocked" to G_HF_Damage (it runs right after): clear it
	// first, or a side / back / splash hit in the same frame as a blocked one (shotgun pellets,
	// a rocket + its splash) also counted as blocked (no flinch, no limbs)
	g_enemy[n].spare[0] = 0;
	if ( !attacker || attacker == targ || mod == MOD_FALLING || mod == MOD_TRIGGER_HURT || mod == MOD_WATER
		|| mod == MOD_SLIME || mod == MOD_LAVA || mod == MOD_CRUSH || mod == MOD_TELEFRAG ) {
		return damage;
	}
	if ( inflictor && inflictor != attacker && inflictor->s.eType == ET_MISSILE ) {
		VectorCopy( inflictor->r.currentOrigin, src );
	} else {
		VectorCopy( attacker->r.currentOrigin, src );
	}
	VectorSet( ang, 0, targ->client->ps.viewangles[YAW], 0 );
	AngleVectors( ang, fwd, NULL, NULL );
	VectorSubtract( src, targ->r.currentOrigin, to );
	to[2] = 0;
	VectorNormalize( to );
	d = DotProduct( fwd, to );
	if ( !( g_enemy[n].flags & DSEF_SHIELDUP ) || ( dflags & DAMAGE_RADIUS )
		|| d < ( G_DS_TypeAdapted( DSAR_SHIELD ) ? DST_SHIELD_COS_ADAPT : DST_SHIELD_COS ) ) {		// en-tune: wider when adapted
		dst.shieldPass++;
		if ( dstCvDebug.integer ) {
			G_LogPrintf( "DSTypes: shield %i hit %s%s (dot %.2f, mod %i): %i passes\n", n,
				( dflags & DAMAGE_RADIUS ) ? "SPLASH" : d >= DST_SHIELD_COS ? "FRONT" : d > -0.5f ? "SIDE" : "BACK",
				( g_enemy[n].flags & DSEF_SHIELDUP ) ? "" : " (lowered)", d, mod, damage );
		}
		return damage;
	}
	out = (int)( damage * DST_SHIELD_BLOCK + 0.5f );
	dst.shieldFront++;
	dst.shieldDmgIn += damage;
	dst.shieldDmgOut += out;
	g_enemy[n].spare[0] = level.time;		// en-feel: a blocked hit (no flinch, no limbs)
	if ( dstCvDebug.integer ) {
		G_LogPrintf( "DSTypes: shield %i hit FRONT (dot %.2f, mod %i): %i -> %i\n", n, d, mod, damage, out );
	}
	if ( level.time - dst.lastBlockFx[n] > 90 ) {
		dst.lastBlockFx[n] = level.time;
		if ( point ) {
			VectorCopy( point, at );
		} else {
			VectorMA( targ->r.currentOrigin, 20, to, at );
			at[2] += 12;
		}
		fx = DST_Fx( DSTFX_BLOCK, n, at, 300 );
		fx->s.angles2[0] = to[0];
		fx->s.angles2[1] = to[1];
		fx->s.angles2[2] = to[2];
	}
	return out;
}

/*
===============================================================================

KAMIKAZE

===============================================================================
*/

static void DST_Boom( gentity_t *ent, int n, float damage, float radius, qboolean pop ) {
	vec3_t		org;
	gentity_t	*fx, *tent;
	qboolean	hit;
	float		hd;

	if ( dst.exploded[n] ) {
		return;
	}
	dst.exploded[n] = qtrue;
	dst.fuseEnd[n] = 0;
	DST_Telegraph( n, 0 );
	VectorCopy( ent->r.currentOrigin, org );
	org[2] += 8;
	hd = DST_HumanDist( org );
	fx = DST_Fx( DSTFX_BOOM, n, org, 900 );
	fx->s.frame = (int)( radius / 4 );
	fx->r.svFlags |= SVF_BROADCAST;
	tent = G_TempEntity( org, EV_MISSILE_MISS );
	tent->s.weapon = WP_ROCKET_LAUNCHER;
	tent->s.eventParm = DirToByte( tv( 0, 0, 1 ) );
	hit = G_RadiusDamage( org, ent, damage, radius, pop ? ent : NULL, MOD_ROCKET_SPLASH );
	DST_Shot( 3, n, 90 );
	if ( pop ) {
		dst.pops++;
	} else {
		dst.booms++;
	}
	if ( hit ) {
		dst.boomHits++;
	}
	G_LogPrintf( "DSTypes: kamikaze %i %s at %s, player %.0f away, radius %.0f, damage %.0f%s\n", n,
		pop ? "POPS (killed first)" : "EXPLODES", vtos( org ), hd, radius, damage, hit ? ", hit something" : "" );
}

static void DST_KamikazeFrame( gentity_t *ent, int n ) {
	gentity_t	*h = DST_Human();
	vec3_t		a, b;
	trace_t		tr;
	float		d;

	if ( dst.fuseEnd[n] ) {
		if ( level.time >= dst.fuseEnd[n] ) {
			DST_Boom( ent, n, DST_KAMI_DAMAGE, DST_KAMI_RADIUS, qfalse );
			// it goes with its blast
			if ( ent->health > 0 ) {
				G_Damage( ent, ent, ent, NULL, NULL, 10000, DAMAGE_NO_PROTECTION, MOD_SUICIDE );
			}
		}
		return;
	}
	if ( !h || !DST_Alive( h ) || G_DS_TypesOtherTelegraph( n ) ) {
		return;
	}
	d = Distance( ent->r.currentOrigin, h->r.currentOrigin );
	if ( d > DST_KAMI_TRIGGER ) {
		return;
	}
	DST_Eye( ent, a );
	DST_Eye( h, b );
	trap_Trace( &tr, a, NULL, NULL, b, n, MASK_SOLID );
	if ( tr.fraction < 1.0f ) {
		return;
	}
	dst.fuseEnd[n] = level.time + DST_KAMI_FUSE;
	DST_Telegraph( n, DST_KAMI_FUSE );
	G_LogPrintf( "DSTypes: kamikaze %i fuse lit, player %.0f away, blows at +%i ms\n", n, d, DST_KAMI_FUSE );
	DST_Shot( 2, n, 500 );
}

// dev / tests: light the fuse now
static void DST_LightFuse( int n ) {
	dst.fuseEnd[n] = level.time + DST_KAMI_FUSE;
	DST_Telegraph( n, DST_KAMI_FUSE );
	G_LogPrintf( "DSTypes: kamikaze %i fuse lit (forced)\n", n );
}

int G_DS_TypesFuse( int client ) {
	return client >= 0 && client < MAX_CLIENTS ? dst.fuseEnd[client] : 0;
}

/*
===============================================================================

MEDIC

===============================================================================
*/

static int DST_FindPatient( gentity_t *medic, int n ) {
	gentity_t	*e;
	vec3_t		a, b;
	trace_t		tr;
	float		d, frac, best = 2.0f;
	int			i, pick = -1, mx;

	DST_Eye( medic, a );
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		e = &g_entities[i];
		if ( i == n || !DST_Alive( e ) || G_RL_IsPlayer( e ) || !( e->r.svFlags & SVF_BOT ) || i == G_DS_BossClient() ) {
			continue;
		}
		mx = e->client->ps.stats[STAT_MAX_HEALTH];
		if ( mx <= 0 || e->health >= mx ) {
			continue;
		}
		d = Distance( medic->r.currentOrigin, e->r.currentOrigin );
		if ( d > DST_HEAL_RANGE ) {
			continue;
		}
		DST_Eye( e, b );
		trap_Trace( &tr, a, NULL, NULL, b, n, MASK_SOLID );
		if ( tr.fraction < 1.0f ) {
			continue;
		}
		frac = (float)e->health / mx + d / ( DST_HEAL_RANGE * 4.0f );
		if ( frac < best ) {
			best = frac;
			pick = i;
		}
	}
	return pick;
}

static void DST_MedicFrame( gentity_t *ent, int n ) {
	gentity_t	*p;
	int			mx, add;

	if ( level.time >= dst.patientCheck[n] ) {
		dst.patientCheck[n] = level.time + 400;
		dst.patient[n] = DST_FindPatient( ent, n );
	}
	p = dst.patient[n] >= 0 ? &g_entities[dst.patient[n]] : NULL;
	if ( p && ( !DST_Alive( p ) || p->health >= p->client->ps.stats[STAT_MAX_HEALTH]
		|| Distance( p->r.currentOrigin, ent->r.currentOrigin ) > DST_HEAL_RANGE + 80 ) ) {
		dst.patient[n] = -1;
		p = NULL;
	}
	if ( !p ) {
		if ( dst.fx[n] ) {
			dst.fx[n]->s.otherEntityNum2 = ENTITYNUM_NONE;
		}
		return;
	}
	if ( !dst.fx[n] ) {
		dst.fx[n] = DST_Fx( DSTFX_HEAL, n, ent->r.currentOrigin, 0 );
		DST_Shot( 7, n, 400 );
	}
	G_SetOrigin( dst.fx[n], ent->r.currentOrigin );
	dst.fx[n]->s.otherEntityNum2 = p - g_entities;
	trap_LinkEntity( dst.fx[n] );
	// health over time: DST_HEAL_PER_SEC, a tick every 250 ms
	if ( level.time >= dst.healTick[n] ) {
		dst.healTick[n] = level.time + 250;
		dst.healCarry[n] += DST_HEAL_PER_SEC;
		add = dst.healCarry[n] / 4;
		dst.healCarry[n] -= add * 4;
		mx = p->client->ps.stats[STAT_MAX_HEALTH];
		if ( p->health + add > mx ) {
			add = mx - p->health;
		}
		if ( add > 0 ) {
			p->health += add;
			p->client->ps.stats[STAT_HEALTH] = p->health;
			dst.healed += add;
			if ( dstCvDebug.integer ) {
				G_LogPrintf( "DSTypes: medic %i heals %i (%s) +%i -> %i/%i\n", n, (int)( p - g_entities ), p->client->pers.netname,
					add, p->health, mx );
			}
		}
	}
}

int G_DS_TypesPatient( int client ) {
	return client >= 0 && client < MAX_CLIENTS ? dst.patient[client] : -1;
}

/*
===============================================================================

BLINKER

===============================================================================
*/

static qboolean DST_BlinkSpot( gentity_t *ent, gentity_t *enemy, vec3_t out ) {
	static vec3_t	pmins = { -15, -15, -24 }, pmaxs = { 15, 15, 32 };
	vec3_t		c, p, down, eye, ang, fwd;
	trace_t		tr;
	float		base, yaw, r, best = -1e9f, score;
	int			i, cont;
	qboolean	found = qfalse;

	VectorCopy( enemy->r.currentOrigin, c );
	base = enemy->client ? enemy->client->ps.viewangles[YAW] : 0;
	DST_Eye( enemy, eye );
	for ( i = 0 ; i < 24 ; i++ ) {
		// flank: to the enemy's side or back
		yaw = base + 180.0f + ( ( DST_Rand() % 200 ) - 100 );
		r = 470 + DST_Rand() % 300;
		VectorSet( ang, 0, yaw, 0 );
		AngleVectors( ang, fwd, NULL, NULL );
		VectorMA( c, r, fwd, p );
		p[2] += 40;
		// room for a player there, and a straight line from the enemy's height
		trap_Trace( &tr, c, pmins, pmaxs, p, ENTITYNUM_NONE, MASK_PLAYERSOLID );
		if ( tr.startsolid ) {
			VectorCopy( p, tr.endpos );
		}
		VectorCopy( tr.endpos, p );
		if ( Distance( p, c ) < 300 ) {
			continue;
		}
		// floor under it (not a pit)
		VectorCopy( p, down );
		down[2] -= 160;
		trap_Trace( &tr, p, pmins, pmaxs, down, ENTITYNUM_NONE, MASK_PLAYERSOLID );
		if ( tr.startsolid || tr.allsolid || tr.fraction >= 1.0f || tr.plane.normal[2] < 0.7f ) {
			continue;
		}
		VectorCopy( tr.endpos, p );
		p[2] += 1;
		cont = trap_PointContents( p, -1 );
		if ( cont & ( CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_NODROP | CONTENTS_SOLID ) ) {
			continue;
		}
		down[0] = p[0]; down[1] = p[1]; down[2] = p[2] - 30;
		if ( trap_PointContents( down, -1 ) & ( CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_NODROP ) ) {
			continue;
		}
		if ( !trap_AAS_PointAreaNum( p ) ) {
			continue;
		}
		// the hard rule: never close to the player
		if ( DST_HumanDist( p ) < DST_BLINK_SAFE ) {
			continue;
		}
		// it wants to see its target from there
		p[2] += 26;
		trap_Trace( &tr, p, NULL, NULL, eye, ENTITYNUM_NONE, MASK_SOLID );
		p[2] -= 26;
		score = tr.fraction >= 1.0f ? 100 : 0;
		score -= fabs( r - 600 ) * 0.05f;
		score += ( DST_Rand() % 20 );
		if ( score > best ) {
			best = score;
			VectorCopy( p, out );
			found = qtrue;
		}
	}
	return found;
}

/*
=================
G_DS_TypesBlink

ai_types.c: a blinker wants to flank its enemy. qtrue = it moved (flash out here, in there).
Never ends within DST_BLINK_MINDIST of the player (checked twice).
=================
*/
qboolean G_DS_TypesBlink( int client, int enemy, qboolean force ) {
	gentity_t	*ent, *en;
	vec3_t		spot, from, dir, ang;
	float		hd;

	if ( client < 0 || client >= MAX_CLIENTS || enemy < 0 || enemy >= MAX_CLIENTS ) {
		return qfalse;
	}
	ent = &g_entities[client];
	en = &g_entities[enemy];
	if ( DST_Arch( client ) != DSAR_BLINKER || !DST_Alive( ent ) || !DST_Alive( en ) ) {
		return qfalse;
	}
	if ( !force && level.time < dst.nextBlink[client] ) {
		return qfalse;
	}
	if ( G_DS_TypesOtherTelegraph( client ) ) {
		return qfalse;
	}
	dst.nextBlink[client] = level.time + ( G_DS_TypeAdapted( DSAR_BLINKER ) ? DST_BLINK_MIN_ADAPT : DST_BLINK_MIN ) + DST_Rand() % 2500;
	if ( !DST_BlinkSpot( ent, en, spot ) ) {
		dst.blinkFails++;
		if ( dstCvDebug.integer ) {
			G_LogPrintf( "DSTypes: blinker %i found no spot near %i\n", client, enemy );
		}
		dst.nextBlink[client] = level.time + 1200;
		return qfalse;
	}
	hd = DST_HumanDist( spot );
	if ( hd < DST_BLINK_MINDIST ) {
		G_LogPrintf( "DSTypes: blinker %i blink REFUSED: %.0f from the player\n", client, hd );
		return qfalse;
	}
	VectorCopy( ent->r.currentOrigin, from );
	DST_Fx( DSTFX_BLINK_OUT, client, from, 600 );
	VectorCopy( spot, ent->client->ps.origin );
	VectorClear( ent->client->ps.velocity );
	ent->client->ps.eFlags ^= EF_TELEPORT_BIT;
	VectorSubtract( en->r.currentOrigin, spot, dir );
	vectoangles( dir, ang );
	ang[PITCH] = ang[ROLL] = 0;
	SetClientViewAngle( ent, ang );
	BG_PlayerStateToEntityState( &ent->client->ps, &ent->s, qtrue );
	VectorCopy( ent->client->ps.origin, ent->r.currentOrigin );
	trap_LinkEntity( ent );
	DST_Fx( DSTFX_BLINK_IN, client, spot, 600 );
	DST_Shot( 4, client, 130 );
	dst.blinks++;
	if ( hd < dst.blinkMin ) {
		dst.blinkMin = (int)hd;
	}
	G_LogPrintf( "DSTypes: blinker %i blinks %.0f units (from %s to %s), %.0f from the player, target %i\n", client,
		Distance( from, spot ), vtos( from ), vtos( spot ), hd, enemy );
	return qtrue;
}

/*
===============================================================================

SUMMONER

===============================================================================
*/

static int DST_AliveRaised( int owner ) {
	char		clean[MAX_NETNAME];
	int			i, k, c = 0;
	gentity_t	*e;

	for ( k = 0 ; k < DST_RAISE_NAMES ; k++ ) {
		if ( dst.raiseOwner[k] < 0 || ( owner >= 0 && dst.raiseOwner[k] != owner ) ) {
			continue;
		}
		// pending (warned, not in yet) counts too, for a few seconds
		if ( level.time - dst.raiseAt[k] < 5000 ) {
			c++;
			continue;
		}
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			e = &g_entities[i];
			if ( !e->inuse || !e->client || e->client->pers.connected != CON_CONNECTED ) {
				continue;
			}
			G_EN_CleanName( e->client->pers.netname, clean, sizeof( clean ) );
			if ( !Q_stricmp( clean, dstRaiseNames[k] ) ) {
				break;
			}
		}
		if ( i < level.maxclients && g_entities[i].health > 0 ) {
			c++;
		} else {
			dst.raiseOwner[k] = -1;		// slain (or kicked): the name is free again
		}
	}
	return c;
}

// a skeleton a summoner raised (fodder, arch 0)
qboolean G_DS_TypesRaised( int client ) {
	char	clean[MAX_NETNAME];
	int		k;

	if ( client < 0 || client >= MAX_CLIENTS || !g_entities[client].client || !G_DS_SurpriseExtra( client ) ) {
		return qfalse;
	}
	G_EN_CleanName( g_entities[client].client->pers.netname, clean, sizeof( clean ) );
	for ( k = 0 ; k < DST_RAISE_NAMES ; k++ ) {
		if ( dst.raiseOwner[k] >= 0 && !Q_stricmp( clean, dstRaiseNames[k] ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

static qboolean DST_CorpseOk( gentity_t *b ) {
	return b && b->inuse && b->r.linked && b->s.eType == ET_PLAYER && ( b->r.contents & CONTENTS_CORPSE ) && !( b->r.svFlags & SVF_NOCLIENT )
		&& Q_stricmp( b->classname, "player" );
}

/*
=================
G_DS_TypesRaiseCorpse

ai_types.c: a corpse this summoner may raise now (cooldown, caps, >= 650 from the player, in
reach), its entity number; -1 none
=================
*/
int G_DS_TypesRaiseCorpse( int client ) {
	gentity_t	*ent, *b = NULL, *best = NULL;
	float		d, bd = 1e9f;

	if ( DST_Arch( client ) != DSAR_SUMMONER || dst.ritualEnd[client] || level.time < dst.nextRaise[client] ) {
		return -1;
	}
	if ( DST_AliveRaised( client ) >= DST_RAISE_MAX_EACH || DST_AliveRaised( -1 ) >= DST_RAISE_MAX_ALL ) {
		return -1;
	}
	ent = &g_entities[client];
	while ( ( b = G_Find( b, FOFS( classname ), "bodyque" ) ) != NULL ) {
		if ( !DST_CorpseOk( b ) ) {
			continue;
		}
		if ( DST_HumanDist( b->r.currentOrigin ) < DS_SURPRISE_MIN_DIST + 50 ) {
			continue;
		}
		d = Distance( b->r.currentOrigin, ent->r.currentOrigin );
		if ( d < 1100 && d < bd ) {
			bd = d;
			best = b;
		}
	}
	return best ? best - g_entities : -1;
}

/*
=================
G_DS_TypesRitual

ai_types.c: the summoner stands over the corpse and starts the raise (1.5 s, telegraphed)
=================
*/
qboolean G_DS_TypesRitual( int client, int corpse ) {
	gentity_t	*b;

	if ( corpse < 0 || corpse >= ENTITYNUM_MAX_NORMAL || dst.ritualEnd[client] || G_DS_TypesOtherTelegraph( client ) ) {
		return qfalse;
	}
	b = &g_entities[corpse];
	if ( !DST_CorpseOk( b ) ) {
		return qfalse;
	}
	dst.ritualEnd[client] = level.time + DST_RITUAL_TIME;
	dst.ritualCorpse[client] = corpse;
	DST_Telegraph( client, DST_RITUAL_TIME );
	DST_FxOff( client );
	dst.fx[client] = DST_Fx( DSTFX_RITUAL, client, b->r.currentOrigin, DST_RITUAL_TIME + 400 );
	dst.fx[client]->s.otherEntityNum2 = corpse;
	dst.fx[client]->s.time2 = dst.ritualEnd[client];
	dst.rituals++;
	DST_Shot( 5, client, 900 );
	G_LogPrintf( "DSTypes: summoner %i starts a raise over corpse %i (%.0f from the player)\n", client, corpse,
		DST_HumanDist( b->r.currentOrigin ) );
	return qtrue;
}

qboolean G_DS_TypesInRitual( int client ) {
	return client >= 0 && client < MAX_CLIENTS && dst.ritualEnd[client] > 0;
}

static void DST_RitualEnd( gentity_t *ent, int n, qboolean done ) {
	gentity_t	*b = &g_entities[dst.ritualCorpse[n]];
	vec3_t		spot;
	int			k;

	dst.ritualEnd[n] = 0;
	DST_Telegraph( n, 0 );
	dst.nextRaise[n] = level.time + ( G_DS_TypeAdapted( DSAR_SUMMONER ) ? DST_RAISE_COOLDOWN_ADAPT : DST_RAISE_COOLDOWN );
	if ( !done ) {
		DST_FxOff( n );
		return;
	}
	if ( !DST_CorpseOk( b ) ) {
		G_LogPrintf( "DSTypes: summoner %i raise fizzles (the corpse is gone)\n", n );
		return;
	}
	for ( k = 0 ; k < DST_RAISE_NAMES ; k++ ) {
		if ( dst.raiseOwner[k] < 0 ) {
			break;
		}
	}
	if ( k == DST_RAISE_NAMES ) {
		return;
	}
	VectorCopy( b->r.currentOrigin, spot );
	spot[2] += 24;
	if ( !G_DS_SurpriseRaise( dstRaiseNames[k], spot, ent->client->ps.viewangles[YAW], 400 ) ) {
		G_LogPrintf( "DSTypes: summoner %i raise failed (no free slot)\n", n );
		return;
	}
	dst.raiseOwner[k] = n;
	dst.raiseAt[k] = level.time;
	dst.raises++;
	DST_Shot( 9, n, 1500 );
	// the corpse is used up: it rises (gone from the floor)
	b->s.eType = ET_INVISIBLE;
	b->r.contents = 0;
	b->takedamage = qfalse;
	trap_LinkEntity( b );
	G_LogPrintf( "DSTypes: summoner %i raises %s from corpse at %s (%i of its own up)\n", n, dstRaiseNames[k], vtos( spot ),
		DST_AliveRaised( n ) );
}

/*
===============================================================================

SNIPER (ai_types.c counts the shots)

===============================================================================
*/

void G_DS_TypesSnipe( int client, int target, float dist ) {
	dst.snipes++;
	if ( dstCvDebug.integer ) {
		G_LogPrintf( "DSTypes: sniper %i fires at %i (%.0f away) after the glint\n", client, target, dist );
	}
}

void G_DS_TypesTelegraph( int client, int msec ) {
	if ( client >= 0 && client < MAX_CLIENTS ) {
		DST_Telegraph( client, msec );
		if ( msec > 0 && DST_Arch( client ) == DSAR_SNIPER ) {
			DST_Shot( 1, client, 480 );
		}
	}
}

/*
===============================================================================

FRAME, DEATH

===============================================================================
*/

static void DST_Report( void ) {
	G_LogPrintf( "DSTypesStats: floor %i blinks %i (min dist to player %i, no spot %i) shield front %i (dmg %i -> %i) passed %i, "
		"healed %i, kamikaze booms %i pops %i (hit %i), rituals %i raises %i, snipes %i\n", dst.floor, dst.blinks,
		dst.blinks ? dst.blinkMin : -1, dst.blinkFails, dst.shieldFront, dst.shieldDmgIn, dst.shieldDmgOut, dst.shieldPass,
		dst.healed, dst.booms, dst.pops, dst.boomHits, dst.rituals, dst.raises, dst.snipes );
}

void G_DS_TypesFrame( void ) {
	gentity_t	*ent;
	int			n, a;

	if ( !dst.on ) {
		return;
	}
	trap_Cvar_Update( &dstCvDebug );
	trap_Cvar_Update( &dstCvShots );
	trap_Cvar_Update( &dstCvJuggBox );
	for ( n = 0 ; n < level.maxclients ; n++ ) {
		a = g_enemy[n].arch;
		ent = &g_entities[n];
		if ( !a ) {
			if ( dst.fx[n] ) {
				DST_FxOff( n );
			}
			continue;
		}
		if ( !DST_Alive( ent ) || level.intermissiontime ) {
			if ( dst.fx[n] && a != DSAR_SUMMONER ) {
				DST_FxOff( n );
			}
			if ( dst.ritualEnd[n] ) {
				DST_RitualEnd( ent, n, qfalse );
			}
			continue;
		}
		switch ( a ) {
		case DSAR_MEDIC:
			DST_MedicFrame( ent, n );
			break;
		case DSAR_KAMIKAZE:
			DST_KamikazeFrame( ent, n );
			break;
		case DSAR_MIMIC:
			if ( dst.mimicPending[n] && DST_Human() && DST_Alive( DST_Human() ) ) {
				DST_MimicKit( ent, n );
			}
			break;
		case DSAR_SUMMONER:
			if ( dst.ritualEnd[n] && level.time >= dst.ritualEnd[n] ) {
				DST_RitualEnd( ent, n, qtrue );
			}
			break;
		default:
			break;
		}
	}
	DST_ShotFrame();
	// en-tune (g_dsTypesDebug 2): where the juggernauts are every 5 s, how far they moved (stuck check)
	if ( dstCvDebug.integer >= 2 && level.time >= dstJuggLogNext ) {
		dstJuggLogNext = level.time + 5000;
		for ( n = 0 ; n < level.maxclients ; n++ ) {
			ent = &g_entities[n];
			if ( g_enemy[n].arch != DSAR_JUGGERNAUT || !DST_Alive( ent ) ) {
				dstJuggLogLife[n] = 0;
				continue;
			}
			G_LogPrintf( "JuggMove: %i at %s moved %.0f in 5 s%s, box %s, frames big %i tall %i plain %i\n", n, vtos( ent->r.currentOrigin ),
				dstJuggLogLife[n] ? Distance( ent->r.currentOrigin, dstJuggLogPos[n] ) : -1.0f,
				ent->client->ps.groundEntityNum == ENTITYNUM_NONE ? " (air)" : "", dst.juggScaled[n] ? "big" : "plain",
				dst.juggBig, dst.juggTall, dst.juggPlain );
			VectorCopy( ent->r.currentOrigin, dstJuggLogPos[n] );
			dstJuggLogLife[n] = 1;
		}
	}
	if ( dstCvDebug.integer && level.time >= dst.nextReport ) {
		dst.nextReport = level.time + 10000;
		DST_Report();
	}
}

/*
=================
G_DS_TypesDeath

G_RL_PlayerDie: a kamikaze killed before it blew pops where it stands (hurts anyone close,
bots too); everything else just stops.
=================
*/
void G_DS_TypesDeath( gentity_t *self, gentity_t *attacker ) {
	int		n;

	if ( !self || !self->client ) {
		return;
	}
	n = self - g_entities;
	if ( n < 0 || n >= MAX_CLIENTS ) {
		return;
	}
	// en-tune: the player's deaths by killer type (type adaptation in the next runs)
	if ( dst.adaptCount && G_RL_IsPlayer( self ) && !( self->r.svFlags & SVF_BOT ) && attacker && attacker->client
		&& attacker != self && attacker - g_entities < MAX_CLIENTS && DST_Arch( attacker - g_entities ) ) {
		dst.adaptDeaths[DST_Arch( attacker - g_entities )]++;
		G_LogPrintf( "DSTypeAdapt: the player died to a %s (%i on this floor)\n", ds_types[DST_Arch( attacker - g_entities )].name,
			dst.adaptDeaths[DST_Arch( attacker - g_entities )] );
	}
	if ( DST_Arch( n ) == DSAR_KAMIKAZE && !dst.exploded[n] ) {
		DST_Boom( self, n, DST_KAMI_POP_DAMAGE, DST_KAMI_POP_RADIUS, qtrue );
	}
	if ( dst.ritualEnd[n] ) {
		DST_RitualEnd( self, n, qfalse );
	}
	dst.fuseEnd[n] = 0;
	dst.patient[n] = -1;
	DST_FxOff( n );
	G_EN_SetFlag( n, DSEF_SHIELDUP, qfalse );
	if ( g_enemy[n].flags & DSEF_TELEGRAPH ) {
		DST_Telegraph( n, 0 );
	}
}

void G_DS_TypesDisconnect( int n ) {
	if ( n >= 0 && n < MAX_CLIENTS ) {
		DST_FxOff( n );
		dst.force[n] = 0;
		dst.fuseEnd[n] = dst.ritualEnd[n] = 0;
	}
}

void G_DS_TypesFloorEnd( void ) {
	char	buf[96];
	int		a;

	if ( dst.on ) {
		DST_Report();
	}
	// en-tune: this floor's deaths by killer type, always written (zeros on boss / vault floors) so
	// the UI's once-per-floor merge never counts a floor twice; never in the daily or practice
	if ( G_RL_NoSave() ) {
		return;
	}
	buf[0] = 0;
	for ( a = 0 ; a < DSAR_NUM ; a++ ) {
		Q_strcat( buf, sizeof( buf ), va( a ? ",%i" : "%i", dst.adaptCount ? dst.adaptDeaths[a] : 0 ) );
	}
	G_RL_RunSet( "tad_f", buf );
	if ( dst.on ) {
		G_LogPrintf( "DSTypeAdapt: floor %i end, deaths by type %s (kamikaze pairs %i), juggernaut box frames big %i tall %i plain %i\n",
			dst.floor, buf, dst.adaptPairs, dst.juggBig, dst.juggTall, dst.juggPlain );
	}
}

/*
===============================================================================

JUGGERNAUT HIT BOX (en-tune)

The cgame draws the juggernaut at DST_JUGG_SCALE from its feet. Its hit box follows: after
Pmove (g_active.c, next to G_DS_BossBounds) the linked box is scaled the same way, standing,
crouched and dead (the body queue copies the dead box, so the corpse gibs where it is drawn).
The MOVEMENT hull stays the plain player hull (pm.mins / pm.maxs every frame), so doors, jump
pads, crouch holes and the bots' AAS routes (a fixed player hull) work as before. The big box
is narrowed for a frame (plain width, still the taller head) when it would overlap a wall or
another player, and plain when even that does not fit or the juggernaut rides a mover / stands
on someone: a mover tests the linked box (a big one could block a lift) and a player inside it
would be stuck.
G_DS_TypesHeadLine: the headshot line scales with it (g_combat.c).

===============================================================================
*/

void G_DS_TypesBounds( gentity_t *ent ) {
	vec3_t		mins, maxs;
	trace_t		tr;
	float		s = DST_JUGG_SCALE;
	int			n, g;

	if ( !ent || !ent->client ) {
		return;
	}
	n = ent - g_entities;
	if ( n < 0 || n >= MAX_CLIENTS ) {
		return;
	}
	dst.juggScaled[n] = qfalse;
	if ( !dst.on || DST_Arch( n ) != DSAR_JUGGERNAUT || !dstCvJuggBox.integer || ent->client->noclip
		|| ent->client->ps.pm_type == PM_SPECTATOR || ent->client->ps.pm_type == PM_NOCLIP ) {
		return;
	}
	VectorCopy( ent->r.mins, mins );
	VectorCopy( ent->r.maxs, maxs );
	mins[0] *= s;
	mins[1] *= s;
	maxs[0] *= s;
	maxs[1] *= s;
	maxs[2] = ( maxs[2] - mins[2] ) * s + mins[2];		// the feet stay where they are (cgame: origin + 24 * (s - 1))
	if ( ent->health > 0 ) {
		g = ent->client->ps.groundEntityNum;
		if ( g != ENTITYNUM_NONE && g != ENTITYNUM_WORLD ) {
			dst.juggPlain++;
			return;		// on a mover / a player
		}
		trap_Trace( &tr, ent->client->ps.origin, mins, maxs, ent->client->ps.origin, n, MASK_PLAYERSOLID );
		if ( tr.startsolid || tr.allsolid ) {
			// against a wall / somebody: keep at least the taller head (plain width) if that fits
			VectorCopy( ent->r.mins, mins );
			maxs[0] = ent->r.maxs[0];
			maxs[1] = ent->r.maxs[1];
			trap_Trace( &tr, ent->client->ps.origin, mins, maxs, ent->client->ps.origin, n, MASK_PLAYERSOLID );
			if ( tr.startsolid || tr.allsolid ) {
				dst.juggPlain++;
				return;		// the plain box this frame
			}
			dst.juggTall++;
			VectorCopy( maxs, ent->r.maxs );
			dst.juggScaled[n] = qtrue;		// the head is where it is drawn
			return;
		}
		dst.juggBig++;
	}
	VectorCopy( mins, ent->r.mins );
	VectorCopy( maxs, ent->r.maxs );
	dst.juggScaled[n] = qtrue;
}

// z above the origin where headshots start (g_combat.c: point[2] >= origin + this)
float G_DS_TypesHeadLine( gentity_t *targ ) {
	float	vh, below = G_RearmHeadshotBelowEyes();
	int		n;

	if ( !targ || !targ->client ) {
		return 0;
	}
	vh = targ->client->ps.viewheight;
	n = targ - g_entities;
	if ( n >= 0 && n < MAX_CLIENTS && dst.juggScaled[n] ) {
		return ( vh - MINS_Z ) * DST_JUGG_SCALE + MINS_Z - below * DST_JUGG_SCALE;
	}
	return vh - below;
}

/*
dsjugtrace <client>: shots (MASK_SHOT traces, the hitscan weapons' mask) from 120 units out on
+x at the drawn juggernaut's chest, head, head top, both shoulder edges and two points outside
it; logs hit / miss and whether the hit is above the headshot line. g_dsJuggBox 0 = before.
*/
static void DST_JuggTrace( int n ) {
	static const char	*names[7] = { "chest", "head", "headtop", "shoulderL", "shoulderR", "outside", "above" };
	static const float	pts[7][2] = { { 0, 10 }, { 0, 38 }, { 0, 44 }, { 17, 26 }, { -17, 26 }, { 24, 20 }, { 0, 52 } };
	static const char	*dnames[7] = { "body", "bodytop", "bodytop2", "sideL", "sideR", "outside", "above" };
	static const float	dpts[7][2] = { { 0, -14 }, { 0, -6 }, { 0, -5 }, { 17, -14 }, { -17, -14 }, { 24, -14 }, { 0, 0 } };
	gentity_t	*e = &g_entities[n];
	vec3_t		start, end;
	trace_t		tr;
	int			i, hits = 0;
	float		line;
	qboolean	hit;

	if ( n < 0 || n >= MAX_CLIENTS || !e->inuse || !e->client ) {
		G_Printf( "dsjugtrace: no client %i\n", n );
		return;
	}
	line = G_DS_TypesHeadLine( e );
	G_LogPrintf( "JuggTrace: client %i %s box %s mins %s maxs %s headline %.1f\n", n, ds_types[DST_Arch( n )].name,
		dst.juggScaled[n] ? "BIG" : "plain", vtos( e->r.mins ), vtos( e->r.maxs ), line );
	for ( i = 0 ; i < 7 ; i++ ) {
		VectorCopy( e->r.currentOrigin, end );
		end[1] += e->health > 0 ? pts[i][0] : dpts[i][0];		// a corpse: its lying box
		end[2] += e->health > 0 ? pts[i][1] : dpts[i][1];
		VectorCopy( end, start );
		start[0] += 120;
		end[0] -= 120;
		trap_Trace( &tr, start, NULL, NULL, end, ENTITYNUM_NONE, MASK_SHOT );
		hit = tr.entityNum == n;
		hits += hit;
		G_LogPrintf( "JuggTrace: %-9s (side %+3.0f up %+3.0f) %s%s\n", e->health > 0 ? names[i] : dnames[i],
			e->health > 0 ? pts[i][0] : dpts[i][0], e->health > 0 ? pts[i][1] : dpts[i][1],
			hit ? "HIT" : tr.entityNum == ENTITYNUM_WORLD ? "miss (wall)" : "miss",
			hit && tr.endpos[2] >= e->r.currentOrigin[2] + line ? " HEADSHOT" : "" );
	}
	G_Printf( "dsjugtrace: %i of 7 hit (games.log JuggTrace)\n", hits );
}

/*
===============================================================================

DEV COMMANDS

dstypes                      list the bots and their types
dstypes <type,type,...>      give the bots (client order) these types now (none = plain) and
                             remember the mix for the next floors (g_dsTypesForce)
dstypes clear                forget the forced mix
dstype <client> <type>       one bot, respawned with the type
dstypeact <client> blink|fuse|raise   trigger a type action now
dstypestats                  the evidence counters (also logged at the floor end)

===============================================================================
*/

static void DST_Apply( int n, int arch ) {
	gentity_t	*ent = &g_entities[n];

	if ( !ent->inuse || !ent->client || !( ent->r.svFlags & SVF_BOT ) ) {
		return;
	}
	dst.force[n] = arch + 1;
	g_enemy[n].arch = arch;
	G_EN_MarkDirty();
	ClientUserinfoChanged( n );		// the type's model
	if ( ent->health > 0 && ent->client->sess.sessionTeam != TEAM_SPECTATOR ) {
		ClientSpawn( ent );
	}
	G_LogPrintf( "DSTypes: dev: client %i (%s) is now %s\n", n, ent->client->pers.netname, arch ? ds_types[arch].name : "plain" );
}

/*
dev "dstypesim <floor> <bots> [runs] [elite]": the planner's natural mix for that floor over a few
seeds (no bots needed); the counts per type go to the log (DSTypesSim)
*/
static void DST_Sim( int floor, int nb, int runs, qboolean elite ) {
	static dstState_t	dstSimSave;
	int			r, i, typed, k, a, idx[DST_MAX_PLAN], j, t, tot[DSAR_NUM], typedSum = 0;
	char		line[256];

	if ( nb < 1 ) {
		nb = 1;
	}
	if ( nb > DST_MAX_PLAN ) {
		nb = DST_MAX_PLAN;
	}
	memcpy( &dstSimSave, &dst, sizeof( dst ) );
	memset( tot, 0, sizeof( tot ) );
	for ( r = 0 ; r < runs ; r++ ) {
		dst.floor = floor;
		dst.act = DS_ActForFloor( floor );
		dst.rng = (unsigned)( 1000 + r ) * 2654435761u + (unsigned)floor * 40503u + 977u;
		dst.numPlan = nb;
		for ( i = 0 ; i < nb ; i++ ) {
			dst.plan[i].arch = 0;
			dst.plan[i].role = ( elite && i == 0 ) ? 3 : 2;
			Com_sprintf( dst.plan[i].name, sizeof( dst.plan[i].name ), "bot%i", i );
		}
		typed = DST_TypedCount( nb );
		k = 0;
		if ( elite ) {
			t = dst.act <= 1 ? 30 : dst.act == 2 ? 60 : dst.act == 3 ? 85 : 100;
			if ( DST_Rand() % 100 < t ) {
				dst.plan[0].arch = DST_PickType( qtrue, nb );
				if ( dst.plan[0].arch ) {
					k++;
				}
			}
		}
		for ( i = 0, j = 0 ; i < nb ; i++ ) {
			if ( !dst.plan[i].arch && dst.plan[i].role != 3 ) {
				idx[j++] = i;
			}
		}
		for ( i = 0 ; i < j ; i++ ) {
			t = i + DST_Rand() % ( j - i );
			a = idx[i]; idx[i] = idx[t]; idx[t] = a;
		}
		for ( i = 0 ; i < j && k < typed ; i++ ) {
			dst.plan[idx[i]].arch = DST_PickType( qfalse, nb );
			if ( dst.plan[idx[i]].arch ) {
				k++;
			}
		}
		DST_AdaptPair();		// en-tune (mirrors G_DS_TypesPlan)
		line[0] = 0;
		for ( i = 0 ; i < nb ; i++ ) {
			tot[dst.plan[i].arch]++;
			if ( dst.plan[i].arch ) {
				typedSum++;
			}
			Q_strcat( line, sizeof( line ), va( " %s%s", dst.plan[i].arch ? ds_types[dst.plan[i].arch].name : "plain",
				dst.plan[i].role == 3 ? "(elite)" : "" ) );
		}
		G_LogPrintf( "DSTypesSim: floor %i act %i bots %i seed %i:%s\n", floor, dst.act, nb, r, line );
	}
	line[0] = 0;
	for ( a = 0 ; a < DSAR_NUM ; a++ ) {
		Q_strcat( line, sizeof( line ), va( " %s %i", a ? ds_types[a].name : "plain", tot[a] ) );
	}
	G_LogPrintf( "DSTypesSim: floor %i act %i bots %i, %i runs: %.2f typed per floor;%s\n", floor, DS_ActForFloor( floor ), nb, runs,
		runs ? (float)typedSum / runs : 0.0f, line );
	G_Printf( "dstypesim: floor %i, %i runs: %.2f typed per floor (games.log DSTypesSim)\n", floor, runs, runs ? (float)typedSum / runs : 0.0f );
	memcpy( &dst, &dstSimSave, sizeof( dst ) );
}

qboolean G_DS_TypesCommand( const char *cmd ) {
	char		arg[MAX_TOKEN_CHARS], arg2[MAX_TOKEN_CHARS], list[MAX_TOKEN_CHARS];
	char		*s, *tok;
	int			i, n, a, k;
	gentity_t	*e;

	if ( !Q_stricmp( cmd, "dstypesim" ) ) {
		trap_Argv( 1, arg, sizeof( arg ) );
		trap_Argv( 2, arg2, sizeof( arg2 ) );
		n = atoi( arg2 );
		trap_Argv( 3, list, sizeof( list ) );
		k = atoi( list );
		trap_Argv( 4, list, sizeof( list ) );
		DST_Sim( atoi( arg ) > 0 ? atoi( arg ) : 1, n > 0 ? n : 4, k > 0 ? k : 10, atoi( list ) != 0 );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dstadapt" ) ) {		// en-tune: dstadapt [type n] = the player died n times to type (tests)
		trap_Argv( 1, arg, sizeof( arg ) );
		trap_Argv( 2, arg2, sizeof( arg2 ) );
		a = BG_DS_TypeByName( arg );
		if ( a > 0 && dst.adaptCount ) {
			dst.adaptDeaths[a] += atoi( arg2 ) > 0 ? atoi( arg2 ) : 1;
		}
		list[0] = 0;
		for ( i = 1 ; i < DSAR_NUM ; i++ ) {
			Q_strcat( list, sizeof( list ), va( " %s w %.1f d %i%s", ds_types[i].name, dst.adaptW[i], dst.adaptDeaths[i],
				G_DS_TypeAdapted( i ) ? " ADAPTED" : "" ) );
		}
		G_Printf( "dstadapt: count %i switch %i:%s\n", dst.adaptCount, dstCvAdapt.integer, list );
		G_LogPrintf( "DSTypeAdapt: state count %i switch %i:%s\n", dst.adaptCount, dstCvAdapt.integer, list );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsjugtrace" ) ) {		// en-tune
		trap_Argv( 1, arg, sizeof( arg ) );
		DST_JuggTrace( atoi( arg ) );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dstypestats" ) ) {
		DST_Report();
		G_Printf( "dstypestats: see games.log (DSTypesStats)\n" );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dstypes" ) ) {
		if ( trap_Argc() < 2 ) {
			G_Printf( "dstypes: on %i floor %i act %i\n", dst.on, dst.floor, dst.act );
			for ( i = 0 ; i < level.maxclients ; i++ ) {
				e = &g_entities[i];
				if ( e->inuse && e->client && e->client->pers.connected == CON_CONNECTED ) {
					G_Printf( "  %2i %-16s %s hp %i/%i armor %i weapon %i\n", i, e->client->pers.netname,
						g_enemy[i].arch ? ds_types[g_enemy[i].arch].name : "-", e->health, e->client->ps.stats[STAT_MAX_HEALTH],
						e->client->ps.stats[STAT_ARMOR], e->client->ps.weapon );
					G_LogPrintf( "DSTypes: list %i %s %s hp %i/%i armor %i weapon %i\n", i, e->client->pers.netname,
						g_enemy[i].arch ? ds_types[g_enemy[i].arch].name : "plain", e->health, e->client->ps.stats[STAT_MAX_HEALTH],
						e->client->ps.stats[STAT_ARMOR], e->client->ps.weapon );
				}
			}
			return qtrue;
		}
		trap_Argv( 1, arg, sizeof( arg ) );
		if ( !Q_stricmp( arg, "clear" ) ) {
			trap_Cvar_Set( "g_dsTypesForce", "" );
			G_Printf( "dstypes: forced mix cleared\n" );
			return qtrue;
		}
		// the rest of the arguments, joined
		list[0] = 0;
		for ( i = 1 ; i < trap_Argc() ; i++ ) {
			trap_Argv( i, arg, sizeof( arg ) );
			Q_strcat( list, sizeof( list ), arg );
			Q_strcat( list, sizeof( list ), "," );
		}
		trap_Cvar_Set( "g_dsTypesForce", list );
		if ( !dst.on ) {
			G_Printf( "dstypes: not on a DESCENT fight floor (or g_dsTypes 0): the mix applies from the next floor\n" );
			return qtrue;
		}
		s = list;
		k = 0;
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			e = &g_entities[i];
			if ( !e->inuse || !e->client || !( e->r.svFlags & SVF_BOT ) || e->client->pers.connected != CON_CONNECTED
				|| i == G_DS_BossClient() || G_RL_IsPlayer( e ) ) {
				continue;
			}
			while ( *s == ',' ) {
				s++;
			}
			if ( !*s ) {
				break;
			}
			tok = s;
			while ( *s && *s != ',' ) {
				s++;
			}
			if ( *s ) {
				*s++ = 0;
			}
			a = BG_DS_TypeByName( tok );
			if ( a < 0 ) {
				G_Printf( "dstypes: unknown type '%s'\n", tok );
				continue;
			}
			DST_Apply( i, a );
			k++;
		}
		G_Printf( "dstypes: %i bots retyped\n", k );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dstype" ) ) {
		trap_Argv( 1, arg, sizeof( arg ) );
		trap_Argv( 2, arg2, sizeof( arg2 ) );
		n = atoi( arg );
		a = BG_DS_TypeByName( arg2 );
		if ( n < 0 || n >= MAX_CLIENTS || a < 0 ) {
			G_Printf( "usage: dstype <client> <type|none>\n" );
			return qtrue;
		}
		if ( !dst.on ) {
			G_Printf( "dstype: not on a DESCENT fight floor (or g_dsTypes 0)\n" );
			return qtrue;
		}
		DST_Apply( n, a );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dstypeact" ) ) {
		trap_Argv( 1, arg, sizeof( arg ) );
		trap_Argv( 2, arg2, sizeof( arg2 ) );
		n = atoi( arg );
		if ( arg[0] && ( arg[0] < '0' || arg[0] > '9' ) ) {
			// a type name: the first living bot of that type
			a = BG_DS_TypeByName( arg );
			for ( n = 0 ; n < level.maxclients ; n++ ) {
				if ( a > 0 && DST_Arch( n ) == a && DST_Alive( &g_entities[n] ) ) {
					break;
				}
			}
		}
		if ( n < 0 || n >= MAX_CLIENTS || !DST_Arch( n ) ) {
			G_Printf( "usage: dstypeact <client|type> blink|fuse|raise|tell|shieldup|shielddown (a typed bot)\n" );
			return qtrue;
		}
		if ( !Q_stricmp( arg2, "tell" ) ) {
			DST_Telegraph( n, 5000 );		// tests (bots paused): the wind-up look only, nothing fires
			G_Printf( "dstypeact: %i shows its tell\n", n );
		} else if ( !Q_stricmp( arg2, "shieldup" ) || !Q_stricmp( arg2, "shielddown" ) ) {
			G_EN_SetFlag( n, DSEF_SHIELDUP, !Q_stricmp( arg2, "shieldup" ) );
		} else if ( !Q_stricmp( arg2, "fuse" ) && DST_Arch( n ) == DSAR_KAMIKAZE ) {
			DST_LightFuse( n );
		} else if ( !Q_stricmp( arg2, "blink" ) ) {
			i = G_RL_HumanClient();
			if ( i >= 0 ) {
				G_Printf( "dstypeact: blink %s\n", G_DS_TypesBlink( n, i, qtrue ) ? "done" : "failed" );
			}
		} else if ( !Q_stricmp( arg2, "raise" ) ) {
			dst.nextRaise[n] = 0;
			i = G_DS_TypesRaiseCorpse( n );
			G_Printf( "dstypeact: raise corpse %i %s\n", i, i >= 0 && G_DS_TypesRitual( n, i ) ? "started" : "failed" );
		}
		return qtrue;
	}
	return qfalse;
}
