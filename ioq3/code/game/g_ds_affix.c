/*
===========================================================================
REARMED -- round 4 AFFIXES (en-affix), game side. DESCENT only, never on boss / vault floors.

Diablo-style elite affixes (DSAF_* in bg_ds_enemy.h, g_enemy[c].affix):
  VAMPIRIC	heals 30% of the damage it deals (a red streak from the bitten to it)
  EXPLOSIVE	its corpse swells and glows for 1.3 s, then blows up (MOD_CORPSE)
  SPLITTING	its first death tears it into two weaker copies (one life each; no further splits)
  REFLECTING	20% of the damage it takes goes back to the attacker (never below 1 health)
  FROST		its hits slow the victim to 70% for 1.3 s (the player gets a frosty screen edge)
  BLAZING		drops short-lived fire patches (RM_ENT_FIRE) as it runs; its own fire doesn't burn it
  MAGNETIC	every 7-10 s, in sight 260-1100 units away: 0.9 s wind-up (DSEF_TELEGRAPH, a tether),
			then yanks the player toward it (magnet-hook physics); breaking line of sight snaps it
  REGENERATING	5% max health a second after 4 s out of combat (no damage taken or dealt)
Who gets them: the floor's elite (planned with the roster, so the loading screen and the
arrival callout name them), ambush mini-bosses (ds-surprise) and champion packs. Count by act:
1 in act 1, 2 in acts 2-3, 3 in act 4 / the Abyss. en-types' archetypes filter a few
(kamikaze: no explode / split / pull, summoner: no split, juggernaut: no split / regen,
medic: no regen). en-personality grants nemesis affixes (G_DS_AffixGrant).

CHAMPION PACKS (g_dsChampions): on about one fight / elite floor in six to four (deeper =
more often), 50-80 s in, after the arrivals: three gold pillars side by side far from the
player, 3 s later three champions land together, sharing one affix set. One life each, out of
the frag race (like ds-surprise's extras), they hunt the player. Each kill pays gems and a
skull; the last one a chest, a beam and more skulls.

Power: every affix has a threat weight; an affixed bot's health and damage are trimmed by
1 / (1 + w / 2) so the total threat stays near what Loot_MatchScale / G_DS_PowerScore want
(the affix numbers themselves are big on purpose). Explosion damage scales with the player's
power. Rewards: affixed kills pay more gems, skulls by affix count, 2+ affixes roll an extra chest.

Network: g_enemy[].affix (CS_DSENEMY), CS_DSAFFIX (frost, regen, champion pack),
RM_ENT_AFFIXFX entities (bomb, reflect flash, tether, split tear, pillars, drain, frost hit).
Logs: "DSAffix:" lines (vampire heal, reflect share, frost slow, split count, pull distance).
===========================================================================
*/
#include "g_local.h"

#define AFK_NONE			0
#define AFK_COPY			1		// a split copy
#define AFK_CHAMP			2		// a champion pack member

#define AFF_MAX_PEND		8
#define AFF_KICK_DELAY		1500
#define AFF_BOMB_FUSE		1300
#define AFF_BOMB_RADIUS		210.0f
#define AFF_SPLIT_DELAY		600
#define AFF_PULL_WIND		900
#define AFF_PULL_MIN		260.0f
#define AFF_PULL_MAX		1100.0f
#define AFF_FROST_MSEC		1300
#define AFF_FROST_SCALE		0.70f
#define AFF_VAMP_SHARE		0.30f
#define AFF_REFLECT_SHARE	0.20f
#define AFF_REGEN_IDLE		4000
#define AFF_REGEN_RATE		0.05f
#define AFF_TRAIL_GAP		450
#define AFF_TRAIL_MSEC		2800
#define AFF_TRAIL_RADIUS	44.0f
#define AFF_CHAMP_WARN		3000
#define AFF_CHAMP_PREFER	900.0f

typedef struct {
	char		name[MAX_NETNAME];		// whose record this is (clean name)
	int			kind;					// AFK_
	qboolean	assigned;
	int			spawns;
	int			lastHurt, lastHit;
	int			nextTrail;
	float		regenAcc;
	qboolean	regenOn;
	int			nextPull, pullAt, pullVictim, pullLogAt;
	float		pullD0;
	gentity_t	*pullFx;
	int			vampFxNext, reflectFxNext, frostFxNext;
	int			vampHeal, vampDealt, vampLogAt;		// totals (logs)
	int			reflSum, reflTaken, reflLogAt;
	int			kickAt;
	int			frostUntil;						// slowed (as a victim) until
	int			champSlot;						// 0..2 for a champion
} affClient_t;

typedef struct {
	qboolean	used, added;
	char		name[MAX_NETNAME];
	char		botFile[MAX_QPATH];
	float		skill;
	int			kind, mask, addAt, addedAt, champSlot;
	vec3_t		spot;
	float		yaw;
} affPend_t;

typedef struct {
	qboolean	on;						// a DESCENT fight / elite / event floor
	int			floor, act, nodeType, seed, numCards;
	int			humanIn;
	affClient_t	cl[MAX_CLIENTS];
	affPend_t	pend[AFF_MAX_PEND];
	// the roster's elite affixes (ds-intro reads them before anyone joined)
	qboolean	planDone;
	int			planMask[16];
	// dev: dsaffix next <mask> [n]
	int			forceMask, forceLeft;
	// champion pack
	qboolean	chPlan;
	int			chAt, chLatest, chMask, chState, chWarnAt, chLandAt, chKills, chAlive, chJoined;
	char		chBot[3][MAX_QPATH];
	vec3_t		chSpot[3];
	float		chYaw[3];
	int			chClient[3];
	// network
	int			regenMask;
	int			pullBusyUntil;			// one MAGNETIC wind-up / yank at a time on the player
	int			frostUntil, frostClient;
	qboolean	csDirty;
	int			nextCv;
	int			stats[8];		// per-floor tallies for dsaffixstate: heals, reflects, frosts, splits, pulls, bombs, trails, regen hp
	int			splitAt[MAX_CLIENTS];	// level.time a client's death used its SPLIT (G_DS_AffixSpentNow)
} affState_t;

static affState_t	aff;
static vmCvar_t		affCvOn, affCvChamp;
static qboolean		affCvReg;
static int			affInHit;

// threat weights (power budget): health and damage x 1 / (1 + w / 2)
static const float affThreat[DSAF_NUM] = { 0.08f, 0.04f, 0.10f, 0.06f, 0.06f, 0.05f, 0.06f, 0.05f };

// champions come from these bots (preloaded on the loading screen)
static const char *affChampBots[] = {
	"Bitterman", "Gorre", "Wrack", "TankJr", "Stripe", "Crash", "Razor", "Keel", "Hunter", "Angel", "Cadavre", "Daemia"
};
#define AFF_NUM_CHAMP_BOTS	( (int)( sizeof( affChampBots ) / sizeof( affChampBots[0] ) ) )

/*
===============================================================================

HELPERS

===============================================================================
*/

static unsigned Aff_Hash( unsigned a, unsigned b ) {
	unsigned	h = a * 2654435761u ^ ( b + 0x9e3779b9u + ( a << 6 ) + ( a >> 2 ) );

	h ^= h >> 13;
	h *= 0x5bd1e995u;
	h ^= h >> 15;
	return h;
}

static int Aff_Roll( int salt ) {
	return (int)( Aff_Hash( (unsigned)aff.seed * 31u + (unsigned)aff.floor, (unsigned)salt ) % 100u );
}

static void Aff_Cvars( void ) {
	if ( !affCvReg ) {
		trap_Cvar_Register( &affCvOn, "g_dsAffixes", "1", CVAR_ARCHIVE );
		trap_Cvar_Register( &affCvChamp, "g_dsChampions", "1", CVAR_ARCHIVE );
		affCvReg = qtrue;
	}
	trap_Cvar_Update( &affCvOn );
	trap_Cvar_Update( &affCvChamp );
}

static int Aff_Count( int mask ) {
	int		i, n = 0;

	for ( i = 0 ; i < DSAF_NUM ; i++ ) {
		if ( mask & ( 1 << i ) ) {
			n++;
		}
	}
	return n;
}

// "FROST VAMPIRIC" (logs)
static const char *Aff_MaskName( int mask ) {
	static char	bufs[2][128];
	static int	bi;
	char		*buf;
	int			i;

	bi ^= 1;
	buf = bufs[bi];
	buf[0] = 0;
	for ( i = 0 ; i < DSAF_NUM ; i++ ) {
		if ( mask & ( 1 << i ) ) {
			Q_strcat( buf, 128, buf[0] ? " " : "" );
			Q_strcat( buf, 128, BG_DS_AffixName( i ) );
		}
	}
	if ( mask & DSAF_CHAMPION ) {
		Q_strcat( buf, 128, buf[0] ? " +champion" : "champion" );
	}
	return buf[0] ? buf : "-";
}

// en-types' archetypes: affixes that clash with a type
static int Aff_ArchBlock( int arch ) {
	switch ( arch ) {
	case DSAR_KAMIKAZE:		return DSAF_EXPLODE | DSAF_SPLIT | DSAF_PULL;
	case DSAR_SUMMONER:		return DSAF_SPLIT;
	case DSAR_JUGGERNAUT:	return DSAF_SPLIT | DSAF_REGEN;
	case DSAR_MEDIC:		return DSAF_REGEN;
	default:				return 0;
	}
}

// count distinct affixes, none of block
static int Aff_RollMask( unsigned seed, int count, int block ) {
	int		pool[DSAF_NUM], n = 0, i, j, t, mask = 0;

	for ( i = 0 ; i < DSAF_NUM ; i++ ) {
		if ( !( block & ( 1 << i ) ) ) {
			pool[n++] = i;
		}
	}
	for ( i = 0 ; i < n ; i++ ) {
		j = i + (int)( Aff_Hash( seed, (unsigned)i + 77u ) % (unsigned)( n - i ) );
		t = pool[i]; pool[i] = pool[j]; pool[j] = t;
	}
	for ( i = 0 ; i < count && i < n ; i++ ) {
		mask |= 1 << pool[i];
	}
	return mask;
}

static gentity_t *Aff_Human( void ) {
	int		h = G_RL_HumanClient();

	return h >= 0 && g_entities[h].inuse && g_entities[h].client ? &g_entities[h] : NULL;
}

static qboolean Aff_Alive( gentity_t *e ) {
	return e && e->inuse && e->client && e->client->pers.connected == CON_CONNECTED && e->health > 0
		&& e->client->sess.sessionTeam != TEAM_SPECTATOR;
}

static int Aff_Mask( int client ) {
	if ( client < 0 || client >= MAX_CLIENTS ) {
		return 0;
	}
	return g_enemy[client].affix & 0xff;
}

static int Aff_FreeSlots( void ) {
	int		i, n = 0;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( level.clients[i].pers.connected == CON_DISCONNECTED ) {
			n++;
		}
	}
	for ( i = 0 ; i < AFF_MAX_PEND ; i++ ) {
		if ( aff.pend[i].used && !aff.pend[i].added ) {
			n--;
		}
	}
	return n;
}

static void Aff_Eye( gentity_t *e, vec3_t out ) {
	VectorCopy( e->r.currentOrigin, out );
	out[2] += e->client ? e->client->ps.viewheight : 24;
}

static qboolean Aff_Sees( gentity_t *a, gentity_t *b ) {
	trace_t		tr;
	vec3_t		ea, eb;

	Aff_Eye( a, ea );
	Aff_Eye( b, eb );
	trap_Trace( &tr, ea, NULL, NULL, eb, a->s.number, MASK_SOLID );
	return tr.fraction >= 1.0f;
}

// player-sized room at p (standing on something within 64 units)
static qboolean Aff_Room( const vec3_t p, int skip ) {
	trace_t		tr;
	vec3_t		mins = { -16, -16, -24 }, maxs = { 16, 16, 32 }, down;

	trap_Trace( &tr, p, mins, maxs, p, skip, MASK_PLAYERSOLID );
	if ( tr.startsolid || tr.allsolid ) {
		return qfalse;
	}
	VectorCopy( p, down );
	down[2] -= 64;
	trap_Trace( &tr, p, mins, maxs, down, skip, MASK_PLAYERSOLID );
	return tr.fraction < 1.0f && !tr.startsolid;
}

static gentity_t *Aff_Fx( int kind, const vec3_t org, int client, int victim, int land, int life, int mask ) {
	gentity_t	*fx = G_Spawn();

	fx->classname = "ds_affixfx";
	fx->s.eType = ET_GENERAL;
	fx->s.generic1 = RM_ENT_AFFIXFX;
	fx->s.modelindex2 = kind;
	fx->s.time = level.time;
	fx->s.time2 = land;
	fx->s.otherEntityNum = client >= 0 ? client : ENTITYNUM_NONE;
	fx->s.otherEntityNum2 = victim >= 0 ? victim : ENTITYNUM_NONE;
	fx->s.frame = mask;
	fx->r.svFlags |= SVF_BROADCAST;
	G_SetOrigin( fx, (float *)org );
	fx->think = G_FreeEntity;
	fx->nextthink = level.time + life;
	trap_LinkEntity( fx );
	return fx;
}

static void Aff_RunKey( const char *key, int n ) {
	G_RL_RunSet( key, va( "%i", atoi( G_RL_RunGet( key ) ) + n ) );
}

/*
===============================================================================

NETWORK

===============================================================================
*/

static void Aff_Publish( void ) {
	char	s[MAX_INFO_STRING], cx[32];
	int		i;

	aff.csDirty = qfalse;
	if ( !aff.on ) {
		trap_SetConfigstring( CS_DSAFFIX, "" );
		return;
	}
	cx[0] = 0;
	for ( i = 0 ; i < 3 ; i++ ) {
		Q_strcat( cx, sizeof( cx ), va( i ? ".%i" : "%i", aff.chClient[i] ) );
	}
	Com_sprintf( s, sizeof( s ), "\\fz\\%i\\fc\\%i\\fp\\%i\\rg\\%i\\cs\\%i\\cm\\%i\\cw\\%i\\cl\\%i\\cc\\%i\\ck\\%i\\cx\\%s",
		aff.frostUntil, aff.frostClient, (int)( ( 1.0f - AFF_FROST_SCALE ) * 100 + 0.5f ), aff.regenMask,
		aff.chState, aff.chMask, aff.chWarnAt, aff.chLandAt, aff.chAlive, aff.chKills, cx );
	trap_SetConfigstring( CS_DSAFFIX, s );
}

/*
===============================================================================

INIT / PLAN

===============================================================================
*/

void G_DS_AffixInit( void ) {
	const char	*nt;
	int			i;

	Aff_Cvars();
	memset( &aff, 0, sizeof( aff ) );
	for ( i = 0 ; i < 3 ; i++ ) {
		aff.chClient[i] = -1;
	}
	aff.frostClient = -1;
	if ( !G_RL_Active() || G_DS_IsBossFloor() || G_DS_IsVaultFloor() ) {
		trap_SetConfigstring( CS_DSAFFIX, "" );
		return;
	}
	nt = G_RL_RunGet( "node_type" );
	aff.nodeType = !Q_stricmp( nt, "elite" ) ? 1 : !Q_stricmp( nt, "boss" ) ? 2 : !Q_stricmp( nt, "event" ) ? 3 : !Q_stricmp( nt, "vault" ) ? 4 : 0;
	if ( aff.nodeType == 2 || aff.nodeType == 4 ) {
		trap_SetConfigstring( CS_DSAFFIX, "" );
		return;
	}
	aff.on = qtrue;
	aff.floor = G_RL_Floor() < 1 ? 1 : G_RL_Floor();
	aff.act = DS_ActForFloor( aff.floor );
	aff.seed = atoi( G_RL_RunGet( "seed" ) );
	{
		const char	*c = G_RL_RunGet( "cards" );
		int			n = 0;

		while ( c && *c ) {
			while ( *c == ',' || *c == '.' || *c == ' ' ) {
				c++;
			}
			if ( !*c ) {
				break;
			}
			n++;
			while ( *c && *c != ',' && *c != '.' && *c != ' ' ) {
				c++;
			}
		}
		aff.numCards = n;
	}
	aff.csDirty = qtrue;
	G_LogPrintf( "DSAffix: floor %i act %i type %i, affixes %s (%i per elite), champions %s\n", aff.floor, aff.act, aff.nodeType,
		affCvOn.integer ? "on" : "off", DSAF_COUNT_FOR_ACT( aff.act ), affCvChamp.integer ? "on" : "off" );
}

// the roster's elites: their affixes, decided once (ds-intro shows them on the loading screen)
static void Aff_Plan( void ) {
	int		i, n, role;

	if ( aff.planDone ) {
		return;
	}
	aff.planDone = qtrue;
	memset( aff.planMask, 0, sizeof( aff.planMask ) );
	if ( !aff.on || !affCvOn.integer ) {
		return;
	}
	n = G_RL_PlanNum();
	for ( i = 0 ; i < n && i < 16 ; i++ ) {
		if ( !G_RL_PlanName( i, &role ) || role != 3 ) {
			continue;
		}
		aff.planMask[i] = Aff_RollMask( Aff_Hash( (unsigned)aff.seed + 101u * (unsigned)aff.floor, 300u + (unsigned)i ),
			DSAF_COUNT_FOR_ACT( aff.act ), 0 );
	}
}

int G_DS_AffixPlanMask( int planIndex ) {
	if ( !aff.on || planIndex < 0 || planIndex >= 16 ) {
		return 0;
	}
	Aff_Plan();
	return aff.planMask[planIndex];
}

/*
=================
G_DS_AffixPreload

G_RL_InitBots, before G_PreloadPublish: the champion pack of this floor (seeded), its models
=================
*/
void G_DS_AffixPreload( void ) {
	int			i, j, k, n, role, chance;
	const char	*nm;
	qboolean	used;

	if ( !aff.on ) {
		return;
	}
	Aff_Plan();
	{
		char	line[128];

		line[0] = 0;
		for ( i = 0 ; i < 16 ; i++ ) {
			if ( aff.planMask[i] ) {
				nm = G_RL_PlanName( i, &role );
				Q_strcat( line, sizeof( line ), va( "%s%s = %s", line[0] ? ", " : "", nm ? nm : "?", Aff_MaskName( aff.planMask[i] ) ) );
			}
		}
		if ( line[0] ) {
			G_LogPrintf( "DSAffix: roster %s\n", line );
		}
	}
	if ( !affCvChamp.integer || aff.floor < 2 ) {
		return;
	}
	chance = 10 + 4 * aff.act;		// 14% act 1 .. 30% the Abyss
	if ( Aff_Roll( 1 ) >= chance ) {
		return;
	}
	aff.chPlan = qtrue;
	aff.chMask = Aff_RollMask( Aff_Hash( (unsigned)aff.seed, 900u + (unsigned)aff.floor ), DSAF_COUNT_FOR_ACT( aff.act ), DSAF_SPLIT );
	aff.chAt = 50000 + Aff_Roll( 2 ) * 300;
	aff.chLatest = aff.chAt + 60000;
	// three different bots, none of this floor's
	k = Aff_Roll( 3 );
	n = 0;
	for ( i = 0 ; i < AFF_NUM_CHAMP_BOTS && n < 3 ; i++ ) {
		nm = affChampBots[( i + k ) % AFF_NUM_CHAMP_BOTS];
		used = qfalse;
		for ( j = 0 ; j < G_RL_PlanNum() ; j++ ) {
			const char	*pn = G_RL_PlanName( j, &role );

			if ( pn && !Q_stricmp( pn, nm ) ) {
				used = qtrue;
			}
		}
		if ( used || !G_GetBotInfoByName( nm ) ) {
			continue;
		}
		Q_strncpyz( aff.chBot[n++], nm, sizeof( aff.chBot[0] ) );
		G_PreloadAddBot( nm );
	}
	if ( n < 3 ) {
		aff.chPlan = qfalse;
		G_LogPrintf( "DSAffix: champion pack called off (only %i bots free)\n", n );
		return;
	}
	G_LogPrintf( "DSAffix: champion pack planned at %i s: %s, %s, %s = %s\n", aff.chAt / 1000, aff.chBot[0], aff.chBot[1], aff.chBot[2],
		Aff_MaskName( aff.chMask ) );
}

/*
===============================================================================

ASSIGN / GRANT

===============================================================================
*/

static void Aff_SetMask( int n, int mask, const char *why ) {
	int		old = g_enemy[n].affix;

	g_enemy[n].affix = mask;
	if ( old != mask ) {
		G_EN_MarkDirty();
		G_LogPrintf( "DSAffix: %s %s -> %s (%s)\n", level.clients[n].pers.netname, Aff_MaskName( old ), Aff_MaskName( mask ), why );
	}
}

void G_DS_AffixGrant( int client, int mask ) {
	affClient_t	*c;

	if ( !aff.on || client < 0 || client >= MAX_CLIENTS || !g_entities[client].inuse ) {
		return;
	}
	mask &= 0xff;
	c = &aff.cl[client];
	c->nextPull = level.time + 5000;
	c->nextTrail = 0;
	c->regenAcc = 0;
	Aff_SetMask( client, g_enemy[client].affix | mask, "grant" );
}

static float Aff_Threat( int mask ) {
	float	w = 0;
	int		i;

	for ( i = 0 ; i < DSAF_NUM ; i++ ) {
		if ( mask & ( 1 << i ) ) {
			w += affThreat[i];
		}
	}
	return w;
}

// health / damage factor of an affixed bot (the power budget) and of copies / champions
static float Aff_HpFactor( int n ) {
	float	f = 1.0f / ( 1.0f + 0.5f * Aff_Threat( Aff_Mask( n ) ) );

	if ( aff.cl[n].kind == AFK_COPY ) {
		f *= 0.5f;
	} else if ( aff.cl[n].kind == AFK_CHAMP ) {
		f *= 0.9f;
	}
	return f;
}

static float Aff_DmgFactor( int n ) {
	float	f = 1.0f / ( 1.0f + 0.5f * Aff_Threat( Aff_Mask( n ) ) );

	if ( aff.cl[n].kind == AFK_COPY ) {
		f *= 0.6f;
	} else if ( aff.cl[n].kind == AFK_CHAMP ) {
		f *= 0.8f;
	}
	return f;
}

static affPend_t *Aff_PendByName( const char *name ) {
	int		i;

	for ( i = 0 ; i < AFF_MAX_PEND ; i++ ) {
		if ( aff.pend[i].used && !Q_stricmp( aff.pend[i].name, name ) ) {
			return &aff.pend[i];
		}
	}
	return NULL;
}

static void Aff_FaceHuman( gentity_t *ent, const vec3_t spot, float yaw ) {
	gentity_t	*h = Aff_Human();
	vec3_t		ang, d;

	VectorSet( ang, 0, yaw, 0 );
	if ( h ) {
		VectorSubtract( h->r.currentOrigin, spot, d );
		vectoangles( d, ang );
		ang[PITCH] = ang[ROLL] = 0;
	}
	SetClientViewAngle( ent, ang );
}

/*
=================
G_DS_AffixSpawn

G_RL_SpawnLoadout, last (before the health limit is applied). A first spawn: the planned
elite's affixes, a mini-boss's roll, a copy's / champion's landing spot. A copy or champion
coming back from the dead leaves (one life).
=================
*/
void G_DS_AffixSpawn( gentity_t *ent ) {
	char		name[MAX_NETNAME];
	affClient_t	*c;
	affPend_t	*p;
	int			n, i, role, mask = 0, plan;
	const char	*why = "";

	if ( !aff.on || !ent || !ent->client || !( ent->r.svFlags & SVF_BOT ) ) {
		return;
	}
	n = ent - g_entities;
	if ( n < 0 || n >= MAX_CLIENTS ) {
		return;
	}
	c = &aff.cl[n];
	G_EN_CleanName( ent->client->pers.netname, name, sizeof( name ) );
	if ( Q_stricmp( c->name, name ) ) {
		memset( c, 0, sizeof( *c ) );
		Q_strncpyz( c->name, name, sizeof( c->name ) );
	}
	c->spawns++;
	c->lastHurt = c->lastHit = level.time;
	c->nextPull = level.time + 4000 + ( n * 397 ) % 3000;
	c->pullAt = 0;
	if ( c->assigned ) {
		if ( c->kind != AFK_NONE && !c->kickAt ) {
			c->kickAt = level.time + 50;		// one life: it leaves
			G_LogPrintf( "DSAffix: %s came back, one life: it leaves\n", name );
		}
		return;
	}
	c->assigned = qtrue;
	p = Aff_PendByName( name );
	if ( p ) {
		// a split copy / a champion: onto its spot
		c->kind = p->kind;
		c->champSlot = p->champSlot;
		mask = p->mask;
		VectorCopy( p->spot, ent->client->ps.origin );
		VectorClear( ent->client->ps.velocity );
		Aff_FaceHuman( ent, p->spot, p->yaw );
		if ( c->kind == AFK_CHAMP && p->champSlot >= 0 && p->champSlot < 3 ) {
			aff.chClient[p->champSlot] = n;
			aff.chAlive++;
			aff.chJoined++;
			if ( aff.chState < 2 ) {
				aff.chState = 2;
			}
			aff.csDirty = qtrue;
		}
		G_LogPrintf( "DSAffix: %s %s lands %i ms after its call (%s)\n", c->kind == AFK_COPY ? "split copy" : "champion", name,
			level.time - p->addedAt, Aff_MaskName( mask ) );
		p->used = qfalse;
		Aff_SetMask( n, ( g_enemy[n].affix & ~0xff ) | mask | ( c->kind == AFK_CHAMP ? DSAF_CHAMPION : 0 ), c->kind == AFK_COPY ? "split copy" : "champion" );
		return;
	}
	if ( !affCvOn.integer ) {
		return;
	}
	role = G_RL_ClientRole( ent );
	if ( aff.forceLeft > 0 && ( role == 2 || role == 3 ) ) {
		aff.forceLeft--;
		mask = aff.forceMask;
		why = "dev dsaffix next";
	} else if ( role == 3 ) {
		if ( G_DS_SurpriseExtra( n ) ) {
			mask = Aff_RollMask( Aff_Hash( (unsigned)aff.seed + 7u * (unsigned)aff.floor, 500u ),
				DSAF_COUNT_FOR_ACT( aff.act ), Aff_ArchBlock( g_enemy[n].arch ) );
			why = "ambush mini-boss";
		} else {
			Aff_Plan();
			plan = -1;
			for ( i = 0 ; i < G_RL_PlanNum() && i < 16 ; i++ ) {
				const char	*pn = G_RL_PlanName( i, &role );

				if ( pn && !Q_stricmp( pn, name ) ) {
					plan = i;
					break;
				}
			}
			mask = plan >= 0 ? aff.planMask[plan] : Aff_RollMask( Aff_Hash( (unsigned)aff.seed + 101u * (unsigned)aff.floor, 399u ),
				DSAF_COUNT_FOR_ACT( aff.act ), 0 );
			why = "elite";
		}
	}
	if ( mask ) {
		Aff_SetMask( n, g_enemy[n].affix | mask, why );
		G_LogPrintf( "DSAffix: %s carries %i affixes, threat +%.2f: health x%.2f damage x%.2f\n", name, Aff_Count( mask ),
			Aff_Threat( mask ), Aff_HpFactor( n ), Aff_DmgFactor( n ) );
	}
}

/*
===============================================================================

COMBAT HOOKS

===============================================================================
*/

qboolean G_DS_AffixExtra( int client ) {
	return aff.on && client >= 0 && client < MAX_CLIENTS && aff.cl[client].kind != AFK_NONE && aff.cl[client].assigned
		&& g_entities[client].inuse;
}

float G_DS_AffixDamageScale( gentity_t *targ, gentity_t *attacker ) {
	int		a;

	if ( !aff.on || !attacker || !attacker->client || attacker == targ ) {
		return 1.0f;
	}
	a = attacker - g_entities;
	if ( a < 0 || a >= MAX_CLIENTS || ( !Aff_Mask( a ) && aff.cl[a].kind == AFK_NONE ) ) {
		return 1.0f;
	}
	return Aff_DmgFactor( a );
}

int G_DS_AffixHealthBonus( gentity_t *ent, int bonus ) {
	int		n, base;

	if ( !aff.on || !ent || !ent->client ) {
		return 0;
	}
	n = ent - g_entities;
	if ( n < 0 || n >= MAX_CLIENTS || ( !Aff_Mask( n ) && aff.cl[n].kind == AFK_NONE ) ) {
		return 0;
	}
	base = ent->client->pers.maxHealth + bonus;
	if ( base < 1 ) {
		return 0;
	}
	return (int)( base * Aff_HpFactor( n ) ) - base;
}

float G_DS_AffixSpeedScale( gentity_t *ent ) {
	int		n;

	if ( !aff.on || !ent || !ent->client ) {
		return 1.0f;
	}
	n = ent - g_entities;
	if ( n >= 0 && n < MAX_CLIENTS && aff.cl[n].frostUntil > level.time ) {
		return AFF_FROST_SCALE;
	}
	return 1.0f;
}

qboolean G_DS_AffixNoSelfBurn( gentity_t *owner, gentity_t *other ) {
	return aff.on && owner && other == owner && owner->client && ( Aff_Mask( owner->s.number ) & DSAF_FIRETRAIL );
}

/*
=================
G_DS_AffixHit

G_Damage, right before the health comes off (take is final). Vampire heal, frost slow,
reflected share; who is in combat (regen).
=================
*/
void G_DS_AffixHit( gentity_t *targ, gentity_t *attacker, int take, int mod ) {
	int			t = -1, a = -1, amask, tmask, heal, share, cap, maxh;
	affClient_t	*ca, *ct;

	if ( !aff.on || affInHit || take <= 0 || !targ ) {
		return;
	}
	if ( targ->client && targ - g_entities < MAX_CLIENTS ) {
		t = targ - g_entities;
		aff.cl[t].lastHurt = level.time;
	}
	if ( attacker && attacker->client && attacker != targ && attacker - g_entities < MAX_CLIENTS ) {
		a = attacker - g_entities;
		aff.cl[a].lastHit = level.time;
	}
	if ( t < 0 || a < 0 ) {
		return;
	}
	affInHit = 1;
	amask = Aff_Mask( a );
	tmask = Aff_Mask( t );
	ca = &aff.cl[a];
	ct = &aff.cl[t];
	if ( take > targ->health && targ->health > 0 ) {
		take = targ->health;		// (overkill doesn't feed anyone)
	}

	// VAMPIRIC: a share of the damage dealt back as health
	if ( ( amask & DSAF_VAMPIRIC ) && attacker->health > 0 && mod != MOD_BURN ) {
		maxh = attacker->client->ps.stats[STAT_MAX_HEALTH];
		heal = (int)( take * AFF_VAMP_SHARE + 0.5f );
		if ( heal < 1 ) {
			heal = 1;
		}
		if ( attacker->health + heal > maxh ) {
			heal = maxh - attacker->health;
		}
		ca->vampDealt += take;
		if ( heal > 0 ) {
			attacker->health += heal;
			attacker->client->ps.stats[STAT_HEALTH] = attacker->health;
			ca->vampHeal += heal;
			aff.stats[0] += heal;
			if ( level.time >= ca->vampFxNext ) {
				ca->vampFxNext = level.time + 350;
				Aff_Fx( DSAFX_DRAIN, targ->r.currentOrigin, a, t, level.time, 450, heal );
			}
		}
		if ( level.time >= ca->vampLogAt ) {
			ca->vampLogAt = level.time + 2000;
			G_LogPrintf( "DSAffix: vampire %s heals %i of %i dealt (last hit %i on %s, health %i/%i)\n", attacker->client->pers.netname,
				ca->vampHeal, ca->vampDealt, take, targ->client->pers.netname, attacker->health, maxh );
		}
	}

	// FROST: the victim is slowed for a moment
	if ( ( amask & DSAF_FROST ) && mod != MOD_BURN && targ->health > take ) {
		if ( ct->frostUntil < level.time ) {
			aff.stats[2]++;
			G_LogPrintf( "DSAffix: frost %s slows %s to %i%% for %i ms\n", attacker->client->pers.netname, targ->client->pers.netname,
				(int)( AFF_FROST_SCALE * 100 + 0.5f ), AFF_FROST_MSEC );
		}
		ct->frostUntil = level.time + AFF_FROST_MSEC;
		if ( t == G_RL_HumanClient() ) {
			aff.frostUntil = ct->frostUntil;
			aff.frostClient = t;
			aff.csDirty = qtrue;
		}
		if ( level.time >= ct->frostFxNext ) {
			ct->frostFxNext = level.time + 400;
			Aff_Fx( DSAFX_FROSTHIT, targ->r.currentOrigin, a, t, level.time, 500, 0 );
		}
	}

	// REFLECTING: a share back to the attacker, never the killing blow
	if ( ( tmask & DSAF_REFLECT ) && attacker->health > 1 && mod != MOD_THORNS ) {
		share = (int)( take * AFF_REFLECT_SHARE + 0.5f );
		if ( share < 1 && take >= 3 ) {
			share = 1;
		}
		cap = attacker->health - 1;
		if ( share > cap ) {
			share = cap;
		}
		ct->reflTaken += take;
		if ( share > 0 ) {
			attacker->health -= share;
			attacker->client->ps.stats[STAT_HEALTH] = attacker->health;
			attacker->client->damage_blood += share;
			VectorCopy( targ->r.currentOrigin, attacker->client->damage_from );
			attacker->client->damage_fromWorld = qfalse;
			ct->reflSum += share;
			aff.stats[1] += share;
			if ( level.time >= ct->reflectFxNext ) {
				ct->reflectFxNext = level.time + 160;
				Aff_Fx( DSAFX_REFLECT, targ->r.currentOrigin, t, a, level.time, 300, share );
			}
		}
		if ( level.time >= ct->reflLogAt ) {
			ct->reflLogAt = level.time + 2000;
			G_LogPrintf( "DSAffix: reflect %s sends %i of %i taken back (last %i of %i to %s, now %i health)\n", targ->client->pers.netname,
				ct->reflSum, ct->reflTaken, share, take, attacker->client->pers.netname, attacker->health );
		}
	}
	affInHit = 0;
}

/*
=================
EXPLOSIVE: the corpse's fuse
=================
*/
static void Aff_BombThink( gentity_t *fx ) {
	gentity_t	*owner = NULL, *body;
	int			o = fx->r.ownerNum, hit, i;
	float		dmg = fx->damage;

	if ( o >= 0 && o < MAX_CLIENTS && g_entities[o].inuse && g_entities[o].client ) {
		owner = &g_entities[o];
	}
	hit = 0;
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		body = &g_entities[i];
		if ( Aff_Alive( body ) && Distance( body->r.currentOrigin, fx->r.currentOrigin ) < AFF_BOMB_RADIUS ) {
			hit++;
		}
	}
	G_RadiusDamage( fx->r.currentOrigin, owner ? owner : fx, dmg, AFF_BOMB_RADIUS, NULL, MOD_CORPSE );
	// the body goes with it (if it's still lying there)
	if ( owner && owner->health <= 0 && !( owner->s.eFlags & EF_NODRAW ) && owner->client->ps.pm_type == PM_DEAD
		&& Distance( owner->r.currentOrigin, fx->r.currentOrigin ) < 64 ) {
		G_AddEvent( owner, EV_GIB_PLAYER, 0 );
		owner->takedamage = qfalse;
		owner->s.eType = ET_INVISIBLE;
		owner->r.contents = 0;
	}
	aff.stats[5]++;
	G_LogPrintf( "DSAffix: explosive corpse blows up: %.0f damage, radius %.0f, %i in reach\n", dmg, AFF_BOMB_RADIUS, hit );
	fx->think = G_FreeEntity;
	fx->nextthink = level.time + 700;
}

static void Aff_Bomb( gentity_t *self ) {
	gentity_t	*fx, *h = Aff_Human();
	vec3_t		org;
	float		p = h ? G_DS_PowerScore( h ) : 1.0f;

	if ( p < 0.8f ) {
		p = 0.8f;
	} else if ( p > 3.0f ) {
		p = 3.0f;
	}
	VectorCopy( self->r.currentOrigin, org );
	org[2] += 8;
	fx = Aff_Fx( DSAFX_BOMB, org, self->s.number, -1, level.time + AFF_BOMB_FUSE, AFF_BOMB_FUSE + 800, Aff_Mask( self->s.number ) );
	fx->r.ownerNum = self->s.number;
	fx->damage = (int)( ( 55 + 2.5f * aff.floor ) * sqrt( p ) );
	fx->think = Aff_BombThink;
	fx->nextthink = level.time + AFF_BOMB_FUSE;
	G_LogPrintf( "DSAffix: %s is EXPLOSIVE: fuse %i ms, %i damage\n", self->client->pers.netname, AFF_BOMB_FUSE, fx->damage );
}

/*
=================
SPLITTING: two weaker copies where it fell
=================
*/
static void Aff_BotFile( gentity_t *ent, char *out, int size ) {
	char		ui[MAX_INFO_STRING], name[MAX_NETNAME];
	const char	*model, *bm, *info;
	int			i;

	G_EN_CleanName( ent->client->pers.netname, name, sizeof( name ) );
	if ( G_GetBotInfoByName( name ) ) {
		Q_strncpyz( out, name, size );
		return;
	}
	trap_GetUserinfo( ent - g_entities, ui, sizeof( ui ) );
	model = Info_ValueForKey( ui, "model" );
	for ( i = 0 ; ( info = G_GetBotInfoByNumber( i ) ) != NULL ; i++ ) {
		bm = Info_ValueForKey( info, "model" );
		if ( model[0] && !Q_stricmp( bm, model ) ) {
			Q_strncpyz( out, Info_ValueForKey( info, "name" ), size );
			return;
		}
	}
	Q_strncpyz( out, "Sarge", size );
}

static qboolean Aff_AddPend( int kind, const char *name, const char *botFile, float skill, int mask, const vec3_t spot, float yaw, int addAt, int slot ) {
	affPend_t	*p;
	int			i;

	for ( i = 0 ; i < AFF_MAX_PEND ; i++ ) {
		p = &aff.pend[i];
		if ( !p->used ) {
			memset( p, 0, sizeof( *p ) );
			p->used = qtrue;
			p->kind = kind;
			Q_strncpyz( p->name, name, sizeof( p->name ) );
			Q_strncpyz( p->botFile, botFile, sizeof( p->botFile ) );
			p->skill = skill;
			p->mask = mask;
			VectorCopy( spot, p->spot );
			p->yaw = yaw;
			p->addAt = addAt;
			p->champSlot = slot;
			return qtrue;
		}
	}
	return qfalse;
}

/*
=================
Aff_TwoSpots

two clear player-sized spots around p (side by side across the player's view first, then any
of 16 directions at 48 / 72 / 100 units), apart from each other and from p
=================
*/
static void Aff_TwoSpots( const vec3_t p, int skip, vec3_t s1, vec3_t s2 ) {
	gentity_t	*h = Aff_Human();
	vec3_t		d, t, cand[2];
	float		base = 0, a, r;
	int			k, ring, got = 0;
	static const float	radii[3] = { 48, 72, 100 };

	if ( h ) {
		VectorSubtract( p, h->r.currentOrigin, d );
		base = atan2( d[1], d[0] ) + M_PI * 0.5f;		// across the line of sight
	}
	for ( ring = 0 ; ring < 3 && got < 2 ; ring++ ) {
		r = radii[ring];
		for ( k = 0 ; k < 16 && got < 2 ; k++ ) {
			a = base + ( k & 1 ? M_PI : 0 ) + ( k / 2 ) * ( M_PI / 8 ) * ( ( k / 2 ) & 1 ? -1 : 1 );
			VectorSet( t, p[0] + cos( a ) * r, p[1] + sin( a ) * r, p[2] + 4 );
			if ( !Aff_Room( t, skip ) ) {
				continue;
			}
			{
				trace_t		tr;

				trap_Trace( &tr, p, NULL, NULL, t, skip, MASK_SOLID );		// not through a wall
				if ( tr.fraction < 1.0f ) {
					continue;
				}
			}
			if ( got == 1 && Distance( t, cand[0] ) < 40 ) {
				continue;
			}
			VectorCopy( t, cand[got] );
			got++;
		}
	}
	if ( got < 1 ) {
		VectorCopy( p, cand[0] );
		cand[0][2] += 4;
	}
	if ( got < 2 ) {
		VectorCopy( cand[0], cand[1] );
		cand[1][2] += 70;		// nothing else: on top (it drops off)
	}
	VectorCopy( cand[0], s1 );
	VectorCopy( cand[1], s2 );
}

static void Aff_Split( gentity_t *self ) {
	char		name[MAX_NETNAME], file[MAX_QPATH], ui[MAX_INFO_STRING], cn[MAX_NETNAME];
	vec3_t		org, s1, s2;
	int			n = self - g_entities, mask, delay;
	float		skill, yaw = self->client->ps.viewangles[YAW];

	G_EN_CleanName( self->client->pers.netname, name, sizeof( name ) );
	if ( strlen( name ) > 24 ) {
		name[24] = 0;
	}
	if ( Aff_FreeSlots() < 2 ) {
		G_LogPrintf( "DSAffix: %s can't split: no free slots\n", name );
		return;
	}
	Aff_BotFile( self, file, sizeof( file ) );
	trap_GetUserinfo( n, ui, sizeof( ui ) );
	skill = atof( Info_ValueForKey( ui, "skill" ) );
	if ( skill < 1 ) {
		skill = 2;
	}
	VectorCopy( self->r.currentOrigin, org );
	org[2] += 8;
	if ( !Aff_Room( org, n ) ) {
		org[2] += 16;
	}
	Aff_TwoSpots( org, n, s1, s2 );
	mask = Aff_Mask( n ) & ~( DSAF_SPLIT | DSAF_EXPLODE | DSAF_PULL );
	// an EXPLOSIVE parent: the copies step out of the smoke after the blast, not into it
	delay = ( Aff_Mask( n ) & DSAF_EXPLODE ) ? AFF_BOMB_FUSE + 250 : AFF_SPLIT_DELAY;
	Com_sprintf( cn, sizeof( cn ), "%s-A", name );
	Aff_AddPend( AFK_COPY, cn, file, skill, mask, s1, yaw, level.time + delay, -1 );
	Com_sprintf( cn, sizeof( cn ), "%s-B", name );
	Aff_AddPend( AFK_COPY, cn, file, skill, mask, s2, yaw, level.time + delay, -1 );
	Aff_Fx( DSAFX_SPLIT, org, n, -1, level.time + delay, delay + 900, Aff_Mask( n ) );
	// no further splits
	Aff_SetMask( n, g_enemy[n].affix & ~DSAF_SPLIT, "split used" );
	aff.splitAt[n] = level.time;		// (the bestiary counts this death as a SPLITTING kill)
	aff.stats[3]++;
	G_LogPrintf( "DSAffix: %s SPLITS into 2 copies (%s-A, %s-B, bot %s, health x0.5 damage x0.6, %s)\n", name, name, name, file, Aff_MaskName( mask ) );
}

/*
=================
G_DS_AffixSpentNow

en-review: the affix bits a client's death spent this frame (SPLIT: "split used" clears it in
G_DS_AffixDeath, before player_die reaches G_DS_BestiaryKill), so the kill still counts for them.
=================
*/
int G_DS_AffixSpentNow( int client ) {
	if ( client < 0 || client >= MAX_CLIENTS || !level.time || aff.splitAt[client] != level.time ) {
		return 0;
	}
	return DSAF_SPLIT;
}

/*
=================
G_DS_AffixDeath

G_RL_PlayerDie, first. Explosive corpses, splits, the rewards, one-life extras.
=================
*/
void G_DS_AffixDeath( gentity_t *self, gentity_t *attacker ) {
	affClient_t	*c;
	vec3_t		org;
	int			n, mask, naff, xp, sk, i;
	qboolean	byPlayer;

	if ( !aff.on || !self || !self->client ) {
		return;
	}
	n = self - g_entities;
	if ( n < 0 || n >= MAX_CLIENTS ) {
		return;
	}
	c = &aff.cl[n];
	mask = Aff_Mask( n );
	c->frostUntil = 0;
	if ( c->pullAt ) {
		c->pullAt = 0;
		g_enemy[n].telegraphUntil = 0;
		G_EN_SetFlag( n, DSEF_TELEGRAPH, qfalse );
		if ( c->pullFx && c->pullFx->inuse ) {
			c->pullFx->s.frame |= 0x100;		// snapped
			c->pullFx->nextthink = level.time + 300;
		}
		c->pullFx = NULL;
		G_LogPrintf( "DSAffix: pull of %s broken (it died)\n", self->client->pers.netname );
	}
	if ( level.intermissiontime || level.intermissionQueued ) {
		return;
	}
	byPlayer = attacker && attacker->client && attacker != self && G_RL_IsPlayer( attacker );
	VectorCopy( self->r.currentOrigin, org );
	org[2] += 16;

	if ( mask & DSAF_EXPLODE ) {
		Aff_Bomb( self );
	}
	if ( ( mask & DSAF_SPLIT ) && c->kind == AFK_NONE ) {
		Aff_Split( self );
	}
	if ( c->kind != AFK_NONE ) {
		c->kickAt = level.time + AFF_KICK_DELAY;		// one life
	}
	// rewards
	if ( byPlayer && ( mask || c->kind != AFK_NONE ) ) {
		naff = Aff_Count( mask );
		if ( c->kind == AFK_COPY ) {
			xp = 3 + aff.floor / 4;
			G_DS_SpawnGems( org, xp, -1 );
			G_LogPrintf( "DSAffix: reward split copy: gems %i\n", xp );
		} else if ( c->kind == AFK_CHAMP ) {
			aff.chKills++;
			xp = 10 + aff.floor / 2 + 3 * naff;
			G_DS_SpawnGems( org, xp, -1 );
			sk = G_DS_Skulls( 1 );
			G_RL_AddSkulls( sk );
			Aff_RunKey( "aff_champk", 1 );
			G_LogPrintf( "DSAffix: reward champion %i/3: gems %i, +%i skull\n", aff.chKills, xp, sk );
		} else {
			xp = 6 * naff + aff.floor / 2;
			G_DS_SpawnGems( org, xp, -1 );
			sk = G_DS_Skulls( naff );
			G_RL_AddSkulls( sk );
			i = 0;
			if ( naff >= 2 && random() < 0.35f * ( naff - 1 ) ) {
				G_DS_SpawnChest( org, 1 );		// the better chest roll
				i = 1;
			}
			Aff_RunKey( "aff_kills", 1 );
			G_LogPrintf( "DSAffix: reward %s (%s): gems +%i, +%i skulls, extra chest %i\n", self->client->pers.netname, Aff_MaskName( mask ),
				xp, sk, i );
		}
	}
	if ( c->kind == AFK_CHAMP ) {
		aff.chAlive--;
		if ( aff.chAlive <= 0 && aff.chJoined >= 3 && aff.chState == 2 ) {
			aff.chState = 3;
			if ( aff.chKills > 0 && byPlayer ) {
				sk = G_DS_Skulls( 2 + aff.act / 2 );
				G_RL_AddSkulls( sk );
				G_DS_SpawnChest( org, 1 );
				G_DS_BeamAt( org, DS_BEAM_LEGENDARY, 8000 );
				Aff_RunKey( "aff_packs", 1 );
				G_LogPrintf( "DSAffix: champion pack DESTROYED (%i of 3 by the player): +%i skulls, a chest\n", aff.chKills, sk );
			} else {
				G_LogPrintf( "DSAffix: champion pack gone (%i of 3 by the player)\n", aff.chKills );
			}
		}
		aff.csDirty = qtrue;
	}
}

/*
===============================================================================

BOTS (ai hooks)

===============================================================================
*/

void G_DS_AffixBotInput( int client, usercmd_t *ucmd ) {
	gentity_t	*e;

	if ( !G_DS_AffixExtra( client ) ) {
		return;
	}
	e = &g_entities[client];
	if ( e->health <= 0 || aff.cl[client].kickAt ) {
		ucmd->buttons = 0;		// one life: it stays down and leaves
		ucmd->forwardmove = ucmd->rightmove = ucmd->upmove = 0;
	}
}

qboolean G_DS_AffixHuntSpot( int client, vec3_t out ) {
	gentity_t	*h;

	if ( !G_DS_AffixExtra( client ) || !Aff_Alive( &g_entities[client] ) ) {
		return qfalse;
	}
	h = Aff_Human();
	if ( !Aff_Alive( h ) ) {
		return qfalse;
	}
	VectorCopy( h->r.currentOrigin, out );
	return qtrue;
}

/*
===============================================================================

FRAME

===============================================================================
*/

// MAGNETIC: the wind-up and the yank
static void Aff_PullFrame( int n, gentity_t *ent, affClient_t *c ) {
	gentity_t	*h = Aff_Human(), *v;
	vec3_t		d, vel;
	float		dist, speed;

	if ( !c->pullAt ) {
		if ( level.time < c->nextPull || !Aff_Alive( h ) || ent == h ) {
			return;
		}
		c->nextPull = level.time + 600;		// (retry soon)
		dist = Distance( ent->r.currentOrigin, h->r.currentOrigin );
		if ( dist < AFF_PULL_MIN || dist > AFF_PULL_MAX || G_EN_HasFlag( n, DSEF_TELEGRAPH ) || g_enemy[n].telegraphUntil > level.time
			|| aff.pullBusyUntil > level.time || !Aff_Sees( ent, h ) ) {
			return;
		}
		aff.pullBusyUntil = level.time + AFF_PULL_WIND + 2500;		// nobody else winds up until 2.5 s after this yank
		c->pullAt = level.time + AFF_PULL_WIND;
		c->pullVictim = h->s.number;
		g_enemy[n].telegraphUntil = c->pullAt;
		G_EN_SetFlag( n, DSEF_TELEGRAPH, qtrue );
		c->pullFx = Aff_Fx( DSAFX_PULL, ent->r.currentOrigin, n, h->s.number, c->pullAt, AFF_PULL_WIND + 600, 0 );
		c->nextPull = c->pullAt + 7000 + (int)( random() * 3000 );
		G_LogPrintf( "DSAffix: %s winds up a pull on %s at %.0f units\n", ent->client->pers.netname, h->client->pers.netname, dist );
		return;
	}
	if ( c->pullFx && c->pullFx->inuse ) {
		G_SetOrigin( c->pullFx, ent->r.currentOrigin );
		trap_LinkEntity( c->pullFx );
	}
	if ( level.time < c->pullAt ) {
		return;
	}
	c->pullAt = 0;
	g_enemy[n].telegraphUntil = 0;
	G_EN_SetFlag( n, DSEF_TELEGRAPH, qfalse );
	v = &g_entities[c->pullVictim];
	if ( !Aff_Alive( v ) || !Aff_Sees( ent, v ) ) {
		if ( c->pullFx && c->pullFx->inuse ) {
			c->pullFx->s.frame |= 0x100;
			c->pullFx->nextthink = level.time + 300;
		}
		c->pullFx = NULL;
		G_LogPrintf( "DSAffix: pull of %s snapped (line of sight broken)\n", ent->client->pers.netname );
		return;
	}
	// the magnet hook's yank: toward the puller, landing short of it
	VectorSubtract( ent->r.currentOrigin, v->r.currentOrigin, d );
	dist = VectorNormalize( d );
	speed = dist * 1.25f;
	if ( speed < 450 ) {
		speed = 450;
	} else if ( speed > 950 ) {
		speed = 950;
	}
	VectorScale( d, speed, vel );
	vel[2] += 230;
	VectorCopy( vel, v->client->ps.velocity );
	v->client->ps.groundEntityNum = ENTITYNUM_NONE;
	v->client->ps.pm_time = 300;
	v->client->ps.pm_flags |= PMF_TIME_KNOCKBACK;
	c->pullD0 = dist;
	c->pullLogAt = level.time + 650;
	aff.stats[4]++;
	c->pullFx = NULL;
	G_LogPrintf( "DSAffix: %s YANKS %s from %.0f units (speed %.0f)\n", ent->client->pers.netname, v->client->pers.netname, dist, speed );
}

static void Aff_RegenFrame( int n, gentity_t *ent, affClient_t *c ) {
	int		maxh = ent->client->ps.stats[STAT_MAX_HEALTH], add;
	qboolean	on;

	on = level.time - c->lastHurt > AFF_REGEN_IDLE && level.time - c->lastHit > AFF_REGEN_IDLE && ent->health < maxh;
	if ( on ) {
		c->regenAcc += maxh * AFF_REGEN_RATE * ( level.time - level.previousTime ) * 0.001f;
		add = (int)c->regenAcc;
		if ( add > 0 ) {
			c->regenAcc -= add;
			if ( ent->health + add > maxh ) {
				add = maxh - ent->health;
			}
			ent->health += add;
			ent->client->ps.stats[STAT_HEALTH] = ent->health;
			aff.stats[7] += add;
		}
	} else {
		c->regenAcc = 0;
	}
	if ( on != c->regenOn ) {
		c->regenOn = on;
		if ( on ) {
			aff.regenMask |= 1 << n;
			G_LogPrintf( "DSAffix: %s regenerates (out of combat, health %i/%i)\n", ent->client->pers.netname, ent->health, maxh );
		} else {
			aff.regenMask &= ~( 1 << n );
			G_LogPrintf( "DSAffix: %s stops regenerating at %i/%i\n", ent->client->pers.netname, ent->health, maxh );
		}
		aff.csDirty = qtrue;
	}
}

// a far spawn spot for the pack and two clear spots beside it
static qboolean Aff_ChampSpots( void ) {
	gentity_t	*spot = NULL, *best = NULL, *h = Aff_Human();
	vec3_t		org, eye;
	trace_t		tr;
	float		d, score, bestScore = -1e9f;
	int			i;

	while ( ( spot = G_Find( spot, FOFS( classname ), "info_player_deathmatch" ) ) != NULL ) {
		VectorCopy( spot->s.origin, org );
		org[2] += 9;
		if ( !Aff_Room( org, ENTITYNUM_NONE ) ) {
			continue;
		}
		d = h ? Distance( org, h->r.currentOrigin ) : 2000;
		if ( d < DS_SURPRISE_MIN_DIST ) {
			continue;
		}
		score = ( d > 2400 ? 2400 : d ) + random() * 250;
		if ( d < AFF_CHAMP_PREFER ) {
			score -= 3000;
		}
		if ( h ) {
			Aff_Eye( h, eye );
			trap_Trace( &tr, eye, NULL, NULL, org, h->s.number, MASK_SOLID );
			if ( tr.fraction < 1.0f ) {
				score += 500;
			}
		}
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			if ( Aff_Alive( &g_entities[i] ) && Distance( g_entities[i].r.currentOrigin, org ) < 200 ) {
				score -= 1500;
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
	VectorCopy( best->s.origin, aff.chSpot[0] );
	aff.chSpot[0][2] += 9;
	aff.chYaw[0] = aff.chYaw[1] = aff.chYaw[2] = best->s.angles[YAW];
	Aff_TwoSpots( aff.chSpot[0], ENTITYNUM_NONE, aff.chSpot[1], aff.chSpot[2] );
	return qtrue;
}

static qboolean Aff_SurpriseBusy( void ) {
	int		i;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( level.clients[i].pers.connected == CON_CONNECTED && G_DS_SurpriseExtra( i ) && !G_DS_AffixExtra( i ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

static void Aff_ChampWarn( void ) {
	int		i;
	float	skill = atof( G_RL_RunGet( "node_skill" ) );

	if ( skill < 1 ) {
		skill = G_DS_Difficulty();
	}
	skill += 0.25f;
	if ( skill > 5 ) {
		skill = 5;
	}
	if ( !Aff_ChampSpots() || Aff_FreeSlots() < 3 ) {
		aff.chPlan = qfalse;
		G_LogPrintf( "DSAffix: champion pack called off (no spot / no free slots)\n" );
		return;
	}
	aff.chState = 1;
	aff.chWarnAt = level.time;
	aff.chLandAt = level.time + AFF_CHAMP_WARN;
	for ( i = 0 ; i < 3 ; i++ ) {
		Aff_Fx( DSAFX_PILLAR, aff.chSpot[i], -1, -1, aff.chLandAt, AFF_CHAMP_WARN + 900, aff.chMask );
		Aff_AddPend( AFK_CHAMP, aff.chBot[i], aff.chBot[i], skill, aff.chMask, aff.chSpot[i], aff.chYaw[i], aff.chLandAt, i );
	}
	aff.csDirty = qtrue;
	G_LogPrintf( "DSAffix: CHAMPION PACK warned (%s): %s %s %s land in %i ms at %i %i %i, %.0f from the player\n", Aff_MaskName( aff.chMask ),
		aff.chBot[0], aff.chBot[1], aff.chBot[2], AFF_CHAMP_WARN, (int)aff.chSpot[0][0], (int)aff.chSpot[0][1], (int)aff.chSpot[0][2],
		Aff_Human() ? Distance( aff.chSpot[0], Aff_Human()->r.currentOrigin ) : 0.0f );
}

static void Aff_ChampFrame( void ) {
	int		el;

	if ( !aff.chPlan || aff.chState != 0 || !aff.humanIn ) {
		return;
	}
	el = level.time - aff.humanIn;
	if ( el > aff.chLatest ) {
		aff.chPlan = qfalse;
		G_LogPrintf( "DSAffix: champion pack never found a moment (%i s in)\n", el / 1000 );
		return;
	}
	if ( el < aff.chAt || G_DS_ArrivalsLeft() > 0 || Aff_SurpriseBusy() || level.intermissiontime || level.intermissionQueued
		|| !Aff_Alive( Aff_Human() ) ) {
		return;
	}
	Aff_ChampWarn();
}

static void Aff_PendFrame( void ) {
	affPend_t	*p;
	int			i;

	for ( i = 0 ; i < AFF_MAX_PEND ; i++ ) {
		p = &aff.pend[i];
		if ( !p->used ) {
			continue;
		}
		if ( p->added ) {
			if ( level.time > p->addedAt + 6000 ) {
				G_LogPrintf( "DSAffix: %s never joined\n", p->name );
				p->used = qfalse;
			}
			continue;
		}
		if ( level.time < p->addAt ) {
			continue;
		}
		if ( level.intermissiontime || level.intermissionQueued ) {
			p->used = qfalse;
			continue;
		}
		p->added = qtrue;
		p->addedAt = level.time;
		G_RL_AddExtraBot( p->botFile, p->name, p->skill, 2, NULL, G_DS_KitCards( aff.numCards ), NULL );
	}
}

void G_DS_AffixFrame( void ) {
	gentity_t	*ent;
	affClient_t	*c;
	int			i, mask, block;
	char		name[MAX_NETNAME];

	if ( !aff.on ) {
		return;
	}
	if ( level.time >= aff.nextCv ) {
		aff.nextCv = level.time + 1000;
		Aff_Cvars();
	}
	if ( !aff.humanIn && Aff_Human() && Aff_Human()->client->pers.connected == CON_CONNECTED ) {
		aff.humanIn = level.time;
	}
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		ent = &g_entities[i];
		c = &aff.cl[i];
		if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED ) {
			if ( c->name[0] ) {
				if ( c->kind == AFK_CHAMP && aff.chState == 2 && ent->health > 0 ) {
					aff.chAlive--;		// (left alive: dropped)
				}
				memset( c, 0, sizeof( *c ) );
				if ( aff.regenMask & ( 1 << i ) ) {
					aff.regenMask &= ~( 1 << i );
					aff.csDirty = qtrue;
				}
			}
			continue;
		}
		if ( c->name[0] ) {
			G_EN_CleanName( ent->client->pers.netname, name, sizeof( name ) );
			if ( Q_stricmp( name, c->name ) ) {
				memset( c, 0, sizeof( *c ) );		// a different client in the slot
			}
		}
		if ( c->kickAt && level.time >= c->kickAt ) {
			c->kickAt = 0;
			G_LogPrintf( "DSAffix: %s leaves (one life)\n", ent->client->pers.netname );
			trap_DropClient( i, "is gone" );
			continue;
		}
		if ( c->pullLogAt && level.time >= c->pullLogAt ) {
			c->pullLogAt = 0;
			if ( c->pullVictim >= 0 && c->pullVictim < MAX_CLIENTS && g_entities[c->pullVictim].inuse ) {
				G_LogPrintf( "DSAffix: pull distance %.0f -> %.0f (pulled %.0f units closer)\n", c->pullD0,
					Distance( ent->r.currentOrigin, g_entities[c->pullVictim].r.currentOrigin ),
					c->pullD0 - Distance( ent->r.currentOrigin, g_entities[c->pullVictim].r.currentOrigin ) );
			}
		}
		mask = Aff_Mask( i );
		if ( !mask ) {
			continue;
		}
		// en-types: an archetype that showed up after the roll
		block = Aff_ArchBlock( g_enemy[i].arch );
		if ( mask & block ) {
			Aff_SetMask( i, g_enemy[i].affix & ~block, "archetype filter" );
			mask &= ~block;
		}
		if ( ent->health <= 0 ) {
			continue;
		}
		if ( ( mask & DSAF_FIRETRAIL ) && level.time >= c->nextTrail && ent->client->ps.groundEntityNum != ENTITYNUM_NONE
			&& VectorLength( ent->client->ps.velocity ) > 90 ) {
			c->nextTrail = level.time + ( c->kind == AFK_CHAMP ? AFF_TRAIL_GAP * 2 : AFF_TRAIL_GAP );		// a pack of three: half as many patches each
			G_SpawnFirePatch( ent->r.currentOrigin, ent, AFF_TRAIL_MSEC, AFF_TRAIL_RADIUS );
			aff.stats[6]++;
		}
		if ( mask & DSAF_REGEN ) {
			Aff_RegenFrame( i, ent, c );
		}
		if ( mask & DSAF_PULL ) {
			Aff_PullFrame( i, ent, c );
		}
	}
	if ( aff.frostClient >= 0 && aff.frostUntil && level.time > aff.frostUntil + 2000 ) {
		aff.frostUntil = 0;
		aff.frostClient = -1;
		aff.csDirty = qtrue;
	}
	Aff_PendFrame();
	Aff_ChampFrame();
	if ( aff.csDirty ) {
		Aff_Publish();
	}
}

/*
===============================================================================

DEV

===============================================================================
*/

// "frost vamp 3" / "0x11" / "all": a mask from words / numbers
static int Aff_ParseMask( int first, int *count ) {
	char	arg[64];
	int		i, k, n = trap_Argc(), mask = 0, len;

	if ( count ) {
		*count = 1;
	}
	for ( i = first ; i < n ; i++ ) {
		trap_Argv( i, arg, sizeof( arg ) );
		if ( !Q_stricmp( arg, "all" ) ) {
			mask |= ( 1 << DSAF_NUM ) - 1;
			continue;
		}
		if ( arg[0] == '0' && ( arg[1] == 'x' || arg[1] == 'X' ) ) {
			mask |= (int)strtol( arg + 2, NULL, 16 );
			continue;
		}
		if ( arg[0] >= '0' && arg[0] <= '9' ) {
			if ( count ) {
				*count = atoi( arg );
			}
			continue;
		}
		len = strlen( arg );
		for ( k = 0 ; k < DSAF_NUM ; k++ ) {
			if ( len >= 3 && !Q_stricmpn( BG_DS_AffixName( k ), arg, len ) ) {
				mask |= 1 << k;
			}
		}
		if ( !Q_stricmp( arg, "explode" ) ) {
			mask |= DSAF_EXPLODE;
		} else if ( !Q_stricmp( arg, "split" ) ) {
			mask |= DSAF_SPLIT;
		} else if ( !Q_stricmp( arg, "reflect" ) ) {
			mask |= DSAF_REFLECT;
		} else if ( !Q_stricmp( arg, "fire" ) || !Q_stricmp( arg, "firetrail" ) ) {
			mask |= DSAF_FIRETRAIL;
		} else if ( !Q_stricmp( arg, "pull" ) ) {
			mask |= DSAF_PULL;
		} else if ( !Q_stricmp( arg, "regen" ) ) {
			mask |= DSAF_REGEN;
		}
	}
	return mask & 0xff;
}

qboolean G_DS_AffixCommand( const char *cmd ) {
	char	arg[64];
	int		i, mask, count;

	if ( !Q_stricmp( cmd, "dsaffix" ) ) {
		if ( !aff.on ) {
			G_Printf( "dsaffix: not on a DESCENT fight / elite / event floor\n" );
			return qtrue;
		}
		trap_Argv( 1, arg, sizeof( arg ) );
		if ( !Q_stricmp( arg, "next" ) ) {
			mask = Aff_ParseMask( 2, &count );
			aff.forceMask = mask;
			aff.forceLeft = count > 0 ? count : 1;
			G_Printf( "dsaffix: the next %i bot(s) to join get %s\n", aff.forceLeft, Aff_MaskName( mask ) );
			G_LogPrintf( "DSAffix: dev next %i bots get %s\n", aff.forceLeft, Aff_MaskName( mask ) );
		} else if ( !Q_stricmp( arg, "give" ) ) {
			trap_Argv( 2, arg, sizeof( arg ) );
			mask = Aff_ParseMask( 3, NULL );
			for ( i = 0 ; i < level.maxclients ; i++ ) {
				if ( !g_entities[i].inuse || !( g_entities[i].r.svFlags & SVF_BOT ) || G_RL_IsPlayer( &g_entities[i] ) ) {
					continue;
				}
				if ( !Q_stricmp( arg, "all" ) || atoi( arg ) == i ) {
					G_DS_AffixGrant( i, mask );
				}
			}
		} else if ( !Q_stricmp( arg, "set" ) ) {
			trap_Argv( 2, arg, sizeof( arg ) );
			mask = Aff_ParseMask( 3, NULL );
			for ( i = 0 ; i < level.maxclients ; i++ ) {
				if ( g_entities[i].inuse && ( g_entities[i].r.svFlags & SVF_BOT ) && !G_RL_IsPlayer( &g_entities[i] )
					&& ( !Q_stricmp( arg, "all" ) || atoi( arg ) == i ) ) {
					Aff_SetMask( i, ( g_enemy[i].affix & ~0xff ) | mask, "dev set" );
					aff.cl[i].nextPull = level.time + 2000;
				}
			}
		} else if ( !Q_stricmp( arg, "look" ) ) {
			// dev (screenshots): the player stands <dist> in front of bot <n>, facing it
			gentity_t	*b, *h = Aff_Human();
			vec3_t		fwd, org, ang, d;
			float		dist;

			trap_Argv( 2, arg, sizeof( arg ) );
			i = atoi( arg );
			trap_Argv( 3, arg, sizeof( arg ) );
			dist = arg[0] ? atof( arg ) : 160;
			if ( i < 0 || i >= level.maxclients || !Aff_Alive( &g_entities[i] ) || !h ) {
				return qtrue;
			}
			b = &g_entities[i];
			{
				int		k;
				float	yaw0 = b->client->ps.viewangles[YAW], yaw;
				trace_t	tr;

				// in front of it first, then around it: a clear spot it can be seen from
				for ( k = 0 ; k < 8 ; k++ ) {
					yaw = yaw0 + ( k & 1 ? -1 : 1 ) * ( ( k + 1 ) / 2 ) * 45.0f;
					VectorSet( ang, 0, yaw, 0 );
					AngleVectors( ang, fwd, NULL, NULL );
					VectorMA( b->r.currentOrigin, dist, fwd, org );
					trap_Trace( &tr, b->r.currentOrigin, NULL, NULL, org, i, MASK_SOLID );
					if ( tr.fraction >= 1.0f && Aff_Room( org, h->s.number ) ) {
						break;
					}
				}
			}
			VectorSubtract( b->r.currentOrigin, org, d );
			vectoangles( d, ang );
			ang[PITCH] = 8;
			TeleportPlayer( h, org, ang );
		} else if ( !Q_stricmp( arg, "lookpack" ) ) {
			// dev (screenshots): the player stands 380 units from the champion pack's first spot, facing it
			gentity_t	*h = Aff_Human();
			vec3_t		org, ang, d;

			if ( h && aff.chState >= 1 ) {
				int			k, best = -1;
				float		dd, a;
				trace_t		tr;
				vec3_t		eye, tgt;

				VectorCopy( aff.chSpot[0], tgt );
				tgt[2] += 20;
				for ( k = 0 ; k < 48 && best < 0 ; k++ ) {
					a = ( k % 16 ) * ( M_PI / 8 );
					dd = k < 16 ? 420 : k < 32 ? 300 : 200;
					VectorSet( org, aff.chSpot[0][0] + cos( a ) * dd, aff.chSpot[0][1] + sin( a ) * dd, aff.chSpot[0][2] + 8 );
					if ( !Aff_Room( org, h->s.number ) ) {
						continue;
					}
					VectorCopy( org, eye );
					eye[2] += 26;
					trap_Trace( &tr, eye, NULL, NULL, tgt, h->s.number, MASK_SOLID );
					if ( tr.fraction >= 1.0f ) {
						best = k;
					}
				}
				if ( best >= 0 ) {
					VectorSubtract( tgt, org, d );
					vectoangles( d, ang );
					ang[PITCH] = -6;
					TeleportPlayer( h, org, ang );
				}
			}
		} else if ( !Q_stricmp( arg, "frost" ) ) {
			// dev (screenshots): the player is slowed as if a FROST bot hit them
			gentity_t	*h = Aff_Human();

			if ( h ) {
				aff.cl[h->s.number].frostUntil = level.time + 3000;
				aff.frostUntil = level.time + 3000;
				aff.frostClient = h->s.number;
				aff.csDirty = qtrue;
			}
		} else if ( !Q_stricmp( arg, "kill" ) || !Q_stricmp( arg, "hurt" ) ) {
			// dev: kill bot <n> (or hurt it by <dmg>) as if the player did it (death effects, rewards, reflect)
			int		dmg = !Q_stricmp( arg, "kill" ) ? 10000 : 0;

			trap_Argv( 2, arg, sizeof( arg ) );
			i = atoi( arg );
			if ( !dmg ) {
				trap_Argv( 3, arg, sizeof( arg ) );
				dmg = atoi( arg ) > 0 ? atoi( arg ) : 50;
			}
			if ( i >= 0 && i < level.maxclients && Aff_Alive( &g_entities[i] ) && Aff_Human() ) {
				G_Damage( &g_entities[i], Aff_Human(), Aff_Human(), NULL, NULL, dmg, DAMAGE_NO_PROTECTION, MOD_RAILGUN );
			}
		} else {
			G_Printf( "dsaffix next <affixes> [n] | give <client|all> <affixes> | set <client|all> <affixes> | kill <client>\n"
				"  affixes: vampiric explode split reflect frost fire pull regen | all | 0x<mask>\n" );
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dschampions" ) ) {
		if ( !aff.on ) {
			G_Printf( "dschampions: not on a DESCENT fight / elite / event floor\n" );
			return qtrue;
		}
		if ( aff.chState != 0 ) {
			G_Printf( "dschampions: a pack already came this floor (state %i)\n", aff.chState );
			return qtrue;
		}
		if ( !aff.chPlan ) {
			int		k = level.time;

			mask = Aff_ParseMask( 1, NULL );
			aff.chMask = mask ? mask : Aff_RollMask( (unsigned)k, DSAF_COUNT_FOR_ACT( aff.act ), DSAF_SPLIT );
			count = 0;
			for ( i = 0 ; i < AFF_NUM_CHAMP_BOTS && count < 3 ; i++ ) {
				const char	*nm = affChampBots[( i + k / 7 ) % AFF_NUM_CHAMP_BOTS];
				int			j, used = 0;

				for ( j = 0 ; j < level.maxclients ; j++ ) {
					char	cn[MAX_NETNAME];

					if ( level.clients[j].pers.connected != CON_DISCONNECTED ) {
						G_EN_CleanName( level.clients[j].pers.netname, cn, sizeof( cn ) );
						if ( !Q_stricmp( cn, nm ) ) {
							used = 1;
						}
					}
				}
				if ( !used && G_GetBotInfoByName( nm ) ) {
					Q_strncpyz( aff.chBot[count++], nm, sizeof( aff.chBot[0] ) );
				}
			}
			if ( count < 3 ) {
				G_Printf( "dschampions: not enough free bot names\n" );
				return qtrue;
			}
			aff.chPlan = qtrue;
		}
		aff.humanIn = aff.humanIn ? aff.humanIn : level.time;
		Aff_ChampWarn();
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsaffixstate" ) ) {
		G_Printf( "affixes: on %i floor %i act %i (%i each), champions plan %i state %i alive %i kills %i mask %s\n", aff.on, aff.floor,
			aff.act, DSAF_COUNT_FOR_ACT( aff.act ), aff.chPlan, aff.chState, aff.chAlive, aff.chKills, Aff_MaskName( aff.chMask ) );
		G_Printf( "  tallies: vamp heal %i, reflected %i, frosts %i, splits %i, pulls %i, bombs %i, trail patches %i, regen hp %i\n",
			aff.stats[0], aff.stats[1], aff.stats[2], aff.stats[3], aff.stats[4], aff.stats[5], aff.stats[6], aff.stats[7] );
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			if ( g_entities[i].inuse && g_entities[i].client && ( g_enemy[i].affix || aff.cl[i].kind ) ) {
				G_Printf( "  %2i %-16s %s kind %i health %i/%i\n", i, g_entities[i].client->pers.netname, Aff_MaskName( g_enemy[i].affix ),
					aff.cl[i].kind, g_entities[i].health, g_entities[i].client->ps.stats[STAT_MAX_HEALTH] );
			}
		}
		return qtrue;
	}
	return qfalse;
}
