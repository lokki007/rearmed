/*
===========================================================================
REARMED round 4 (en-brains) -- ai_tactics.c: smarter bots in every mode.

Think-level tactics on top of the stock AI nodes (ai_dmnet.c):
  flank / pincer      2+ bots on one target: the closest holds, the others circle to a side
                      or behind it over an AAS route (BotTacticsAttackMove, BotTacticsChaseGoal)
  retreat to heal     hurt bots break off to the nearest health / armour away from the threat
                      and come back topped up (BotTacticsWantsRetreat / WantsChase / Goal)
  item timing         skilled bots time quad / red armour / mega etc. and wait for the respawn
  hunting by sound    gunfire, footsteps, jump pads, pain within a hearing radius: go and look
                      at the last heard spot; a chase follows the enemy's noise, not its position
  leader focus        some bots, some of the time, prefer whoever leads on frags
  grudges             (en-personality's state) strongly prefer / hunt the bot's last killer
  barrel lures        back off behind an explosive barrel, the existing prop shot sets it off
  panic / crawl       (en-feel's states) flee from the scarer, crawl toward health
  keep range          archetype range (en-types) or the human's habits (ai_habits.c)
The usercmd level (dodges, rail jukes, rocket / strafe jumps) is ai_tactics_move.c.

Every tactic: g_botTactics (master) + its own cvar, scaled by bot skill (1-2 barely, 4-5 fully)
and by the archetype (BotTypeTactic). Evidence: bt_debug 1 logs, `btstats` counts.
===========================================================================
*/
#include "g_local.h"
#include "../botlib/botlib.h"
#include "../botlib/be_aas.h"
#include "../botlib/be_ea.h"
#include "../botlib/be_ai_char.h"
#include "../botlib/be_ai_chat.h"
#include "../botlib/be_ai_gen.h"
#include "../botlib/be_ai_goal.h"
#include "../botlib/be_ai_move.h"
#include "../botlib/be_ai_weap.h"
#include "ai_main.h"
#include "ai_dmq3.h"
#include "ai_dmnet.h"
#include "ai_tactics.h"
#include "inv.h"

extern bot_state_t	*botstates[MAX_CLIENTS];

qboolean	BotRearmExplosiveProp( gentity_t *ent );
float		BotRearmPropRadius( gentity_t *prop );
qboolean	BotRearmAggressive( bot_state_t *bs );
qboolean	BotRearmPriorityTarget( bot_state_t *bs, int client );

/*
===============================================================================

CVARS

===============================================================================
*/
static vmCvar_t	g_botTactics, g_botFlank, g_botRetreat, g_botDodge, g_botRocketJump, g_botItemTiming;
static vmCvar_t	g_botHearing, g_botHabits, g_botLeader, g_botLure;
static vmCvar_t	bt_debug, bt_split;
static int		btCvarsDone;

static void BtCvars( void ) {
	if ( !btCvarsDone ) {
		btCvarsDone = 1;
		trap_Cvar_Register( &g_botTactics, "g_botTactics", "1", CVAR_ARCHIVE );
		trap_Cvar_Register( &g_botFlank, "g_botFlank", "1", CVAR_ARCHIVE );
		trap_Cvar_Register( &g_botRetreat, "g_botRetreat", "1", CVAR_ARCHIVE );
		trap_Cvar_Register( &g_botDodge, "g_botDodge", "1", CVAR_ARCHIVE );
		trap_Cvar_Register( &g_botRocketJump, "g_botRocketJump", "1", CVAR_ARCHIVE );
		trap_Cvar_Register( &g_botItemTiming, "g_botItemTiming", "1", CVAR_ARCHIVE );
		trap_Cvar_Register( &g_botHearing, "g_botHearing", "1", CVAR_ARCHIVE );
		trap_Cvar_Register( &g_botHabits, "g_botHabits", "1", CVAR_ARCHIVE );
		trap_Cvar_Register( &g_botLeader, "g_botLeader", "1", CVAR_ARCHIVE );
		trap_Cvar_Register( &g_botLure, "g_botLure", "1", CVAR_ARCHIVE );
		trap_Cvar_Register( &bt_debug, "bt_debug", "0", 0 );
		trap_Cvar_Register( &bt_split, "bt_split", "0", 0 );
	}
	trap_Cvar_Update( &g_botTactics );
	trap_Cvar_Update( &g_botFlank );
	trap_Cvar_Update( &g_botRetreat );
	trap_Cvar_Update( &g_botDodge );
	trap_Cvar_Update( &g_botRocketJump );
	trap_Cvar_Update( &g_botItemTiming );
	trap_Cvar_Update( &g_botHearing );
	trap_Cvar_Update( &g_botHabits );
	trap_Cvar_Update( &g_botLeader );
	trap_Cvar_Update( &g_botLure );
	trap_Cvar_Update( &bt_debug );
	trap_Cvar_Update( &bt_split );
}

int BotTacDebug( void ) {
	return bt_debug.integer;
}

/*
===============================================================================

STATE

===============================================================================
*/
#define BT_MAX_ITEMS		32

typedef struct {
	int		typeFlags;			// BotTypeThink result this think
	int		lastEnemy;
	// flank
	int		flankUntil, flankNext, flankEnemy;
	vec3_t	flankSpot;
	int		flankArea;
	// retreat to heal
	int		healEnt, healUntil, healNext, healStartHP, comebackUntil, comebackCounted;
	// item timing
	int		contestEnt, contestUntil, contestNext, contestAware, contestAwareNext;
	// hearing
	vec3_t	soundSpot;
	int		soundArea, soundUntil, soundNext, soundHeard[MAX_CLIENTS], trackTime;
	// leader / grudge
	int		leaderFocus, leaderNext, grudgeCounted;
	// lure
	int		lureProp, lureUntil, lureNext, lureEndTime;
	vec3_t	lureSpot;
	int		lureArea;
	// panic / crawl
	vec3_t	fleeSpot;
	int		fleeArea, fleeUntil, wasPanic, wasCrawl;
	// misc
	int		lastPropEnt, preaimNext, rjWilling, rjNext, rangeNext, rangeOn;
	// weapon thief: the human's dropped gun we race for
	int		prizeEnt, prizeNext;
	// pre-aim at the human's usual entrance while waiting
	vec3_t	preaimSpot;
	int		preaimUntil;
	// en-tune: low-skill gate windows per tactic (BotTacGate)
	int		gateUntil[BTAC_NUM], gateOn[BTAC_NUM];
} btBot_t;

typedef struct {
	vec3_t	pos;
	int		time;
	float	radius;
} btSound_t;

typedef struct {
	int		ent;
	float	value;
	int		avail;
	int		respawnAt;		// 0 unknown
	int		takenBy;
} btItem_t;

static btBot_t		bt[MAX_CLIENTS];
static btSound_t	btSnd[MAX_CLIENTS];
static int			btEvSeq[MAX_CLIENTS];
static int			btDmgEv[MAX_CLIENTS];
static btItem_t		btItems[BT_MAX_ITEMS];
static int			btNumItems;
static int			btItemScan;
static int			btRoleTime;
static int			btRole[MAX_CLIENTS];		// 0 holder / alone, k = k-th flanker on its target
static int			btHolder[MAX_CLIENTS];		// per target: the holding bot (-1)
static int			btLeader = -1;
static int			btLeaderTime;
static int			btLevelStart = -1;
static int			btCount[BTC_NUM];
static int			btStatsNext;
static int			btStatsFinal;

static const char *btCountNames[BTC_NUM] = {
	"flank", "flankReached", "retreat", "healed", "comeback",
	"dodge", "juke", "rjump", "rjumpChase", "strafeHop",
	"contest", "contestGot", "hear", "hearReached", "track",
	"habitWeapon", "range", "preaim", "leader", "grudge", "grudgeHunt",
	"lure", "lureBlown", "barrel", "panic", "crawl"
};

void BotTacCount( bot_state_t *bs, int counter ) {
	if ( counter < 0 || counter >= BTC_NUM ) {
		return;
	}
	btCount[counter]++;
	if ( bt_debug.integer >= 2 && bs ) {
		G_LogPrintf( "BtEvent: %i %s\n", bs->client, btCountNames[counter] );
	}
}

/*
===============================================================================

HELPERS

===============================================================================
*/
float BotTacSkill( bot_state_t *bs ) {
	float	t = ( bs->settings.skill - 1.0f ) / 3.0f;

	if ( t < 0 ) {
		t = 0;
	} else if ( t > 1 ) {
		t = 1;
	}
	return 0.05f + 0.95f * t * t;
}

static qboolean BtBoss( bot_state_t *bs ) {
	return bs->client == G_DS_BossClient();
}

/*
en-tune: the low-skill gate. Below skill 4 a bot uses each tactic only part of the time, in
windows of 12-22 s (stock AI the rest): skill 1 never (plain stock bots), 2 a third of the
time, 3 two thirds, 4-5 always. On top of BotTacSkill's scale, so skill 1-2 bots play like
stock bots and skill 3 gets a moderate bump (measured: tools/tune_sweep.sh, README).
*/
float BotTacGate( bot_state_t *bs ) {
	float	t = ( bs->settings.skill - 1.0f ) / 3.0f;

	return t < 0 ? 0 : t > 1 ? 1 : t;
}

static qboolean BtGateOpen( bot_state_t *bs, int tactic ) {
	btBot_t	*b = &bt[bs->client];
	float	g = BotTacGate( bs );

	if ( g >= 1.0f ) {
		return qtrue;
	}
	if ( g <= 0.0f || tactic < 0 || tactic >= BTAC_NUM ) {
		return qfalse;
	}
	if ( level.time >= b->gateUntil[tactic] ) {
		b->gateUntil[tactic] = level.time + 12000 + (int)( random() * 10000 );
		b->gateOn[tactic] = random() < g;
	}
	return b->gateOn[tactic];
}

qboolean BotTacOn( bot_state_t *bs, int tactic, float *scale ) {
	vmCvar_t	*cv;
	float		s;

	if ( scale ) {
		*scale = 0;
	}
	if ( !g_botTactics.integer ) {
		return qfalse;
	}
	if ( bt_split.integer == 1 && ( bs->client & 1 ) ) {
		return qfalse;		// test split: only even client numbers think
	}
	if ( bt_split.integer == 2 && !( bs->client & 1 ) ) {
		return qfalse;
	}
	if ( bt_split.integer == 3 && bs->client == 0 ) {
		return qfalse;		// en-tune: client 0 is a stock stand-in for the player
	}
	switch ( tactic ) {
	case BTAC_FLANK:	cv = &g_botFlank; break;
	case BTAC_RETREAT:	cv = &g_botRetreat; break;
	case BTAC_DODGE:	cv = &g_botDodge; break;
	case BTAC_RJUMP:	cv = &g_botRocketJump; break;
	case BTAC_ITEMS:	cv = &g_botItemTiming; break;
	case BTAC_SOUND:	cv = &g_botHearing; break;
	case BTAC_HABITS:	cv = &g_botHabits; break;
	case BTAC_LEADER:	cv = &g_botLeader; break;
	case BTAC_LURE:		cv = &g_botLure; break;
	default:			cv = &g_botTactics; break;
	}
	if ( !cv->integer ) {
		return qfalse;
	}
	// a DESCENT boss keeps its own scripted movement and targeting: dodging only (en-boss)
	if ( BtBoss( bs ) && tactic != BTAC_DODGE ) {
		return qfalse;
	}
	if ( bt[bs->client].typeFlags & BTT_NOTACTICS ) {
		return qfalse;
	}
	if ( !BtGateOpen( bs, tactic ) ) {
		return qfalse;		// en-tune: a low-skill bot plays stock this window
	}
	s = BotTacSkill( bs ) * BotTypeTactic( bs, tactic );
	if ( s <= 0 ) {
		return qfalse;
	}
	if ( scale ) {
		*scale = s > 1 ? 1 : s;
	}
	return qtrue;
}

int BotTacOwnMove( bot_state_t *bs ) {
	return bt[bs->client].typeFlags & ( BTT_OWNMOVE | BTT_NOTACTICS );
}

qboolean BotTacStaggered( bot_state_t *bs ) {
	gEnemy_t	*e = G_EN( bs->client );

	return e && ( ( e->flags & DSEF_STAGGER ) || level.time < e->staggerUntil );
}

qboolean BotTacCrawling( bot_state_t *bs ) {
	return G_EN_HasFlag( bs->client, DSEF_CRAWL );
}

// bt_split A/B tests: this bot is in the stock half
static qboolean BtSplitOut( bot_state_t *bs ) {
	return ( bt_split.integer == 1 && ( bs->client & 1 ) ) || ( bt_split.integer == 2 && !( bs->client & 1 ) )
		|| ( bt_split.integer == 3 && bs->client == 0 );
}

static qboolean BtPanic( bot_state_t *bs ) {
	return G_EN_HasFlag( bs->client, DSEF_PANIC ) && !BtBoss( bs ) && !BtSplitOut( bs );
}

// crawling (en-feel): we steer it to health / away
static qboolean BtCrawl( bot_state_t *bs ) {
	return BotTacCrawling( bs ) && !BtBoss( bs ) && !BtSplitOut( bs ) && bs->inventory[INVENTORY_HEALTH] < 60;
}

static qboolean BtAlive( int client ) {
	gentity_t	*ent;

	if ( client < 0 || client >= MAX_CLIENTS ) {
		return qfalse;
	}
	ent = &g_entities[client];
	return ent->inuse && ent->client && ent->client->pers.connected == CON_CONNECTED
		&& ent->client->sess.sessionTeam != TEAM_SPECTATOR && ent->health > 0
		&& ent->client->ps.pm_type == PM_NORMAL;
}

static qboolean BtBattleNode( bot_state_t *bs ) {
	return bs->ainode == AINode_Battle_Fight || bs->ainode == AINode_Battle_Chase
		|| bs->ainode == AINode_Battle_Retreat || bs->ainode == AINode_Battle_NBG;
}

// the grudge target (en-personality's state): -1 none
static int BtGrudge( bot_state_t *bs ) {
	int		g = G_EN_GrudgeTarget( bs->client );		// en-personality: checks g_botGrudges, timer, alive

	if ( g == bs->client || !BtAlive( g ) ) {
		return -1;
	}
	return g;
}

static float BtFlatDist( const vec3_t a, const vec3_t b ) {
	float	dx = a[0] - b[0], dy = a[1] - b[1];

	return sqrt( dx * dx + dy * dy );
}

static void BtRotateFlat( const vec3_t in, float deg, vec3_t out ) {
	float	r = DEG2RAD( deg ), c = cos( r ), s = sin( r );
	float	x = in[0], y = in[1];

	out[0] = x * c - y * s;
	out[1] = x * s + y * c;
	out[2] = 0;
}

/*
Drop a point onto the floor below it: out = a standing player origin there.
No lava / slime / void, room for a player.
*/
static qboolean BtGroundSpot( const vec3_t in, vec3_t out, int passent ) {
	trace_t	tr;
	vec3_t	start, end;
	vec3_t	mins = { -15, -15, -24 }, maxs = { 15, 15, 32 };
	int		contents;

	VectorCopy( in, start );
	start[2] += 48;
	VectorCopy( in, end );
	end[2] -= 320;
	trap_Trace( &tr, start, mins, maxs, end, passent, MASK_PLAYERSOLID );
	if ( tr.startsolid || tr.allsolid || tr.fraction >= 1.0f || tr.plane.normal[2] < 0.7f ) {
		return qfalse;
	}
	if ( tr.surfaceFlags & SURF_NODAMAGE ) {
		// fine, just no fall damage
	}
	VectorCopy( tr.endpos, out );
	contents = trap_PointContents( out, -1 );
	if ( contents & ( CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_NODROP ) ) {
		return qfalse;
	}
	end[0] = out[0];
	end[1] = out[1];
	end[2] = out[2] - 40;
	contents = trap_PointContents( end, -1 );
	if ( contents & ( CONTENTS_LAVA | CONTENTS_SLIME ) ) {
		return qfalse;
	}
	return qtrue;
}

static int BtArea( const vec3_t p ) {
	int		area = BotPointAreaNum( (float *)p );

	if ( !area || !trap_AAS_AreaReachability( area ) ) {
		return 0;
	}
	return area;
}

// travel time in 1/100 s, 0 = no route
static int BtTravel( bot_state_t *bs, int area, const vec3_t origin ) {
	if ( !area || !bs->areanum ) {
		return 0;
	}
	if ( area == bs->areanum ) {
		return 1;
	}
	return trap_AAS_AreaTravelTimeToGoalArea( bs->areanum, bs->origin, area, bs->tfl ? bs->tfl : TFL_DEFAULT );
}

static void BtGoalAt( bot_goal_t *goal, const vec3_t origin, int area, int entnum ) {
	memset( goal, 0, sizeof( *goal ) );
	VectorCopy( origin, goal->origin );
	goal->areanum = area;
	VectorSet( goal->mins, -16, -16, -16 );
	VectorSet( goal->maxs, 16, 16, 16 );
	goal->entitynum = entnum;
}

static qboolean BtMoveTo( bot_state_t *bs, int tfl, const vec3_t spot, int area, bot_moveresult_t *moveresult ) {
	bot_goal_t	goal;

	BtGoalAt( &goal, spot, area, -1 );
	BotSetupForMovement( bs );
	trap_BotMoveToGoal( moveresult, bs->ms, &goal, tfl );
	return !moveresult->failure;
}

static void BtEnemyOrigin( int client, vec3_t out ) {
	VectorCopy( g_entities[client].r.currentOrigin, out );
}

/*
===============================================================================

SOUNDS: what every client did that others can hear

===============================================================================
*/
static float BtWeaponLoudness( int weapon ) {
	switch ( weapon ) {
	case WP_GAUNTLET:			return 350;
	case WP_MACHINEGUN:			return 1400;
	case WP_SHOTGUN:			return 1500;
	case WP_GRENADE_LAUNCHER:	return 1300;
	case WP_ROCKET_LAUNCHER:	return 1700;
	case WP_LIGHTNING:			return 1100;
	case WP_RAILGUN:			return 2000;
	case WP_PLASMAGUN:			return 1300;
	case WP_BFG:				return 2000;
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case WP_NAILGUN:			return 1200;
	case WP_PROX_LAUNCHER:		return 1200;
	case WP_CHAINGUN:			return 1500;
#endif
	default:					return 1000;
	}
}

static void BtHeard( int client, float radius ) {
	btSound_t	*s = &btSnd[client];

	if ( level.time - s->time > 400 || radius >= s->radius * 0.7f ) {
		VectorCopy( g_entities[client].r.currentOrigin, s->pos );
		if ( level.time - s->time > 400 || radius > s->radius ) {
			s->radius = radius;
		}
		s->time = level.time;
	}
}

static void BtSoundEvent( int client, int ev ) {
	switch ( ev ) {
	case EV_FOOTSTEP:
	case EV_FOOTSTEP_METAL:
	case EV_FOOTSPLASH:
	case EV_FOOTWADE:
	case EV_SWIM:
		BtHeard( client, 420 );
		break;
	case EV_JUMP:
		BtHeard( client, 520 );
		break;
	case EV_JUMP_PAD:
		BtHeard( client, 1100 );
		break;
	case EV_FALL_SHORT:
		BtHeard( client, 450 );
		break;
	case EV_FALL_MEDIUM:
	case EV_FALL_FAR:
		BtHeard( client, 800 );
		break;
	case EV_FIRE_WEAPON:
		BtHeard( client, BtWeaponLoudness( g_entities[client].client->ps.weapon ) );
		break;
	default:
		break;
	}
}

static void BtSoundFrame( void ) {
	gentity_t		*ent;
	playerState_t	*ps;
	int				i, seq;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		ent = &g_entities[i];
		if ( !BtAlive( i ) ) {
			if ( ent->client ) {
				btEvSeq[i] = ent->client->ps.eventSequence;
				btDmgEv[i] = ent->client->ps.damageEvent;
			}
			continue;
		}
		ps = &ent->client->ps;
		seq = btEvSeq[i];
		if ( ps->eventSequence - seq > MAX_PS_EVENTS || seq > ps->eventSequence ) {
			seq = ps->eventSequence - MAX_PS_EVENTS;
		}
		for ( ; seq < ps->eventSequence ; seq++ ) {
			BtSoundEvent( i, ps->events[seq & ( MAX_PS_EVENTS - 1 )] & ~EV_EVENT_BITS );
		}
		btEvSeq[i] = ps->eventSequence;
		if ( ( ps->eFlags & EF_FIRING ) && ps->weapon != WP_GAUNTLET ) {
			BtHeard( i, BtWeaponLoudness( ps->weapon ) );		// held fire: lightning, chaingun
		}
		if ( ps->damageEvent != btDmgEv[i] ) {
			btDmgEv[i] = ps->damageEvent;
			BtHeard( i, 900 );		// a pain cry
		}
	}
}

/*
Can this bot hear client? qtrue + where. Skill scales the radius; walls muffle (out of PVS).
*/
static qboolean BtCanHear( bot_state_t *bs, int client, float scale, float mul, vec3_t where, int *when ) {
	btSound_t	*s = &btSnd[client];
	float		radius, d;

	if ( client == bs->client || !s->time || level.time - s->time > 2500 || !BtAlive( client ) ) {
		return qfalse;
	}
	radius = s->radius * ( 0.4f + 0.6f * scale ) * mul;		// en-tune: was 0.55 + 0.45 (low skills heard 60% as far)
	d = Distance( s->pos, bs->origin );
	if ( d > radius ) {
		return qfalse;
	}
	if ( !trap_InPVS( bs->eye, s->pos ) && d > radius * 0.6f ) {
		return qfalse;
	}
	VectorCopy( s->pos, where );
	*when = s->time;
	return qtrue;
}

/*
===============================================================================

ITEMS: the big ones and when they come back

===============================================================================
*/
static float BtItemValue( gentity_t *ent ) {
	gitem_t	*it = ent->item;

	switch ( it->giType ) {
	case IT_POWERUP:
		switch ( it->giTag ) {
		case PW_QUAD:		return 3.0f;
		case PW_BATTLESUIT:	return 2.5f;
		case PW_REGEN:		return 1.8f;
		case PW_INVIS:		return 1.5f;
		case PW_HASTE:		return 1.3f;
		case PW_FLIGHT:		return 1.0f;
		default:			return 1.5f;
		}
	case IT_ARMOR:
		if ( it->quantity >= 100 ) {
			return 2.5f;
		}
		if ( it->quantity >= 50 ) {
			return 1.2f;
		}
		return 0;
	case IT_HEALTH:
		if ( it->quantity >= 100 ) {
			return 2.0f;
		}
		return 0;
	default:
		return 0;
	}
}

static qboolean BtItemPresent( gentity_t *ent ) {
	return ent->inuse && ent->item && ( ent->r.contents & CONTENTS_TRIGGER ) && !( ent->r.svFlags & SVF_NOCLIENT )
		&& !( ent->s.eFlags & EF_NODRAW );
}

static void BtItemScan( void ) {
	gentity_t	*ent;
	btItem_t	*it;
	int			i, j, avail, best;
	float		v, d, bestD;

	if ( level.time < btItemScan ) {
		return;
	}
	btItemScan = level.time + 500;
	if ( !btNumItems ) {
		// build the list once per map: the big items stay put
		for ( i = MAX_CLIENTS, ent = &g_entities[MAX_CLIENTS] ; i < level.num_entities && btNumItems < BT_MAX_ITEMS ; i++, ent++ ) {
			if ( !ent->inuse || !ent->item || ( ent->flags & FL_DROPPED_ITEM ) ) {
				continue;
			}
			v = BtItemValue( ent );
			if ( v <= 0 ) {
				continue;
			}
			it = &btItems[btNumItems++];
			memset( it, 0, sizeof( *it ) );
			it->ent = i;
			it->value = v;
			it->avail = BtItemPresent( ent );
			it->takenBy = -1;
		}
		if ( !btNumItems ) {
			btItemScan = level.time + 10000;
		}
		return;
	}
	for ( j = 0 ; j < btNumItems ; j++ ) {
		it = &btItems[j];
		ent = &g_entities[it->ent];
		avail = BtItemPresent( ent );
		if ( it->avail && !avail ) {
			// taken: by whom (the nearest player)?
			best = -1;
			bestD = 120;
			for ( i = 0 ; i < level.maxclients ; i++ ) {
				if ( !g_entities[i].inuse || !g_entities[i].client ) {
					continue;
				}
				d = Distance( g_entities[i].r.currentOrigin, ent->r.currentOrigin );
				if ( d < bestD ) {
					bestD = d;
					best = i;
				}
			}
			it->takenBy = best;
			if ( best >= 0 && botstates[best] && botstates[best]->inuse && bt[best].contestEnt == it->ent ) {
				BotTacCount( botstates[best], BTC_CONTEST_GOT );
				bt[best].contestEnt = 0;
			}
		}
		it->avail = avail;
		if ( !avail && ent->inuse && ent->think == RespawnItem && ent->nextthink > level.time ) {
			it->respawnAt = ent->nextthink;
		} else if ( avail ) {
			it->respawnAt = 0;
		}
	}
}

static btItem_t *BtItemByEnt( int entnum ) {
	int		i;

	for ( i = 0 ; i < btNumItems ; i++ ) {
		if ( btItems[i].ent == entnum ) {
			return &btItems[i];
		}
	}
	return NULL;
}

/*
===============================================================================

ROLES (flank) and the frag leader

===============================================================================
*/
static void BtRoles( void ) {
	int				i, j, k, n, target, list[MAX_CLIENTS];
	float			dist[MAX_CLIENTS], t;
	bot_state_t		*bs;
	int				best, second, score;

	if ( level.time >= btRoleTime ) {
		btRoleTime = level.time + 300;
		for ( i = 0 ; i < MAX_CLIENTS ; i++ ) {
			btRole[i] = 0;
			btHolder[i] = -1;
		}
		for ( target = 0 ; target < level.maxclients ; target++ ) {
			if ( !BtAlive( target ) ) {
				continue;
			}
			n = 0;
			for ( i = 0 ; i < level.maxclients ; i++ ) {
				bs = botstates[i];
				if ( !bs || !bs->inuse || bs->enemy != target || !BtAlive( i ) ) {
					continue;
				}
				if ( bs->ainode != AINode_Battle_Fight && bs->ainode != AINode_Battle_Chase ) {
					continue;
				}
				list[n] = i;
				dist[n] = Distance( g_entities[i].r.currentOrigin, g_entities[target].r.currentOrigin );
				n++;
			}
			if ( n < 2 ) {
				continue;
			}
			// sort by distance (insertion, n is small)
			for ( j = 1 ; j < n ; j++ ) {
				for ( k = j ; k > 0 && dist[k] < dist[k - 1] ; k-- ) {
					t = dist[k]; dist[k] = dist[k - 1]; dist[k - 1] = t;
					i = list[k]; list[k] = list[k - 1]; list[k - 1] = i;
				}
			}
			btHolder[target] = list[0];
			for ( j = 1 ; j < n ; j++ ) {
				btRole[list[j]] = j;
			}
			if ( bt_debug.integer >= 3 ) {
				G_LogPrintf( "BtRoles: target %i holder %i bots %i\n", target, list[0], n );
			}
		}
	}

	if ( level.time >= btLeaderTime ) {
		btLeaderTime = level.time + 1000;
		best = second = -9999;
		btLeader = -1;
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			if ( !g_entities[i].inuse || !g_entities[i].client || g_entities[i].client->pers.connected != CON_CONNECTED
				|| g_entities[i].client->sess.sessionTeam == TEAM_SPECTATOR ) {
				continue;
			}
			score = g_entities[i].client->ps.persistant[PERS_SCORE];
			if ( score > best ) {
				second = best;
				best = score;
				btLeader = i;
			} else if ( score > second ) {
				second = score;
			}
		}
		if ( best < 3 || best - second < 2 ) {
			btLeader = -1;		// nobody really leads
		}
	}
}

/*
===============================================================================

SPOTS: flank, flee, lure

===============================================================================
*/
static qboolean BtLOS( const vec3_t a, const vec3_t b, int passent ) {
	trace_t	tr;
	vec3_t	s, e;

	VectorCopy( a, s );
	s[2] += 26;
	VectorCopy( b, e );
	e[2] += 26;
	trap_Trace( &tr, s, NULL, NULL, e, passent, MASK_SOLID );
	return tr.fraction >= 1.0f;
}

/*
A spot around target at angle deg from the holder's side, range r, that we can walk to.
*/
static qboolean BtFlankSpot( bot_state_t *bs, const vec3_t target, const vec3_t holder, float deg, float r,
		qboolean needLOS, vec3_t spot, int *area ) {
	vec3_t	back, dir, p, ground;
	int		a, travel;

	VectorSubtract( holder, target, back );
	back[2] = 0;
	if ( VectorNormalize( back ) < 32 ) {
		VectorSubtract( bs->origin, target, back );
		back[2] = 0;
		if ( VectorNormalize( back ) < 1 ) {
			return qfalse;
		}
	}
	BtRotateFlat( back, deg, dir );
	VectorMA( target, r, dir, p );
	p[2] = target[2];
	if ( !BtGroundSpot( p, ground, bs->client ) ) {
		return qfalse;
	}
	if ( fabs( ground[2] - target[2] ) > 200 ) {
		return qfalse;
	}
	a = BtArea( ground );
	if ( !a ) {
		return qfalse;
	}
	travel = BtTravel( bs, a, ground );
	if ( travel <= 0 || travel > 700 ) {
		return qfalse;
	}
	if ( needLOS && !BtLOS( ground, target, bs->client ) ) {
		return qfalse;
	}
	VectorCopy( ground, spot );
	*area = a;
	return qtrue;
}

static qboolean BtStartFlank( bot_state_t *bs, const vec3_t target, float scale ) {
	btBot_t	*b = &bt[bs->client];
	vec3_t	holder, rel, back;
	float	deg, r, side, cross;
	int		role = btRole[bs->client], h;

	if ( level.time < b->flankNext ) {
		return qfalse;
	}
	b->flankNext = level.time + 2500 + (int)( random() * 1500 );
	if ( random() >= 0.25f + 0.65f * scale ) {
		return qfalse;
	}
	h = btHolder[bs->enemy];
	if ( h < 0 || h == bs->client ) {
		return qfalse;
	}
	VectorCopy( g_entities[h].r.currentOrigin, holder );
	// stay on the side we're already on (don't run through the holder's line of fire)
	VectorSubtract( holder, target, back );
	VectorSubtract( bs->origin, target, rel );
	cross = back[0] * rel[1] - back[1] * rel[0];
	side = cross >= 0 ? 1 : -1;
	if ( role >= 3 ) {
		deg = 180;
	} else if ( role == 2 ) {
		deg = -side * ( 90 + random() * 30 );		// the other side
	} else {
		deg = side * ( 110 + random() * 50 );		// a pincer from our side
	}
	// a raised shield blocks the front: go round to its back (en-types DSEF_SHIELDUP)
	if ( bs->enemy < MAX_CLIENTS && G_EN_HasFlag( bs->enemy, DSEF_SHIELDUP ) ) {
		deg = side * 170;
	}
	r = 320 + random() * 220;
	if ( !BtFlankSpot( bs, target, holder, deg, r, qtrue, b->flankSpot, &b->flankArea )
		&& !BtFlankSpot( bs, target, holder, deg * 0.65f, r * 0.75f, qtrue, b->flankSpot, &b->flankArea )
		&& !BtFlankSpot( bs, target, holder, deg, r * 0.6f, qfalse, b->flankSpot, &b->flankArea ) ) {
		if ( bt_debug.integer >= 3 ) {
			G_LogPrintf( "BtFlankFail: %i role %i on %i (no spot)\n", bs->client, role, bs->enemy );
		}
		return qfalse;
	}
	b->flankUntil = level.time + 4000 + (int)( random() * 2000 );
	b->flankEnemy = bs->enemy;
	BotTacCount( bs, BTC_FLANK );
	if ( bt_debug.integer ) {
		G_LogPrintf( "BtFlank: %i role %i on %i deg %.0f r %.0f\n", bs->client, role, bs->enemy, deg, r );
	}
	return qtrue;
}

static qboolean BtFlanking( bot_state_t *bs ) {
	btBot_t	*b = &bt[bs->client];

	if ( !b->flankUntil ) {
		return qfalse;
	}
	if ( level.time > b->flankUntil || b->flankEnemy != bs->enemy || btRole[bs->client] == 0 ) {
		b->flankUntil = 0;
		return qfalse;
	}
	if ( BtFlatDist( bs->origin, b->flankSpot ) < 80 ) {
		b->flankUntil = 0;
		b->flankNext = level.time + 3500;
		BotTacCount( bs, BTC_FLANK_REACHED );
		return qfalse;
	}
	return qtrue;
}

/*
Somewhere well away from the threat we can get to quickly
*/
static qboolean BtFleeSpot( bot_state_t *bs, const vec3_t threat ) {
	btBot_t	*b = &bt[bs->client];
	vec3_t	dir, p, ground, best;
	float	off, gain, bestGain = 150, d;
	int		i, k, a, bestArea = 0, travel;
	static const float dists[2] = { 700, 420 };

	off = random() * 360;
	for ( k = 0 ; k < 2 ; k++ ) {
		for ( i = 0 ; i < 8 ; i++ ) {
			VectorSet( dir, 1, 0, 0 );
			BtRotateFlat( dir, off + i * 45, dir );
			VectorMA( bs->origin, dists[k], dir, p );
			if ( !BtGroundSpot( p, ground, bs->client ) || fabs( ground[2] - bs->origin[2] ) > 260 ) {
				continue;
			}
			a = BtArea( ground );
			travel = BtTravel( bs, a, ground );
			if ( !a || travel <= 0 || travel > 500 ) {
				continue;
			}
			d = Distance( ground, threat );
			gain = d - Distance( bs->origin, threat ) - travel * 0.5f;
			if ( !BtLOS( ground, threat, bs->client ) ) {
				gain += 150;		// out of its sight
			}
			if ( gain > bestGain ) {
				bestGain = gain;
				VectorCopy( ground, best );
				bestArea = a;
			}
		}
		if ( bestArea ) {
			break;
		}
	}
	if ( !bestArea ) {
		return qfalse;
	}
	VectorCopy( best, b->fleeSpot );
	b->fleeArea = bestArea;
	b->fleeUntil = level.time + 2500;
	return qtrue;
}

/*
The nearest health / armour that is there and not toward the threat (threat may be NULL)
*/
static int BtHealItem( bot_state_t *bs, const vec3_t threat, int maxTravel ) {
	gentity_t	*ent;
	gitem_t		*it;
	vec3_t		toItem, toThreat;
	int			i, best = 0, travel, area, health, armor;
	float		score, bestScore = 99999;

	health = bs->inventory[INVENTORY_HEALTH];
	armor = bs->inventory[INVENTORY_ARMOR];
	for ( i = MAX_CLIENTS, ent = &g_entities[MAX_CLIENTS] ; i < level.num_entities ; i++, ent++ ) {
		if ( !ent->inuse || !ent->item || !BtItemPresent( ent ) ) {
			continue;
		}
		it = ent->item;
		if ( it->giType == IT_HEALTH ) {
			if ( health >= 100 && it->quantity < 100 ) {
				continue;
			}
		} else if ( it->giType == IT_ARMOR ) {
			if ( armor >= 100 || it->quantity < 25 ) {
				continue;		// shards don't count
			}
		} else {
			continue;
		}
		area = BtArea( ent->r.currentOrigin );
		travel = BtTravel( bs, area, ent->r.currentOrigin );
		if ( travel <= 0 || travel > maxTravel ) {
			continue;
		}
		score = travel - it->quantity * 0.8f;
		if ( threat ) {
			VectorSubtract( ent->r.currentOrigin, bs->origin, toItem );
			VectorSubtract( threat, bs->origin, toThreat );
			toItem[2] = toThreat[2] = 0;
			VectorNormalize( toItem );
			VectorNormalize( toThreat );
			if ( DotProduct( toItem, toThreat ) > 0.5f
				&& Distance( ent->r.currentOrigin, threat ) < Distance( bs->origin, threat ) + 100 ) {
				score += 400;		// past the enemy
			}
		}
		if ( score < bestScore ) {
			bestScore = score;
			best = i;
		}
	}
	return best;
}

/*
===============================================================================

THINK (every BotDeathmatchAI, after the AI node ran)

===============================================================================
*/
static void BtThreat( bot_state_t *bs, vec3_t out ) {
	int		s = 0;
	gEnemy_t	*e = G_EN( bs->client );

	if ( e && e->spare[2] > 0 && BtAlive( e->spare[2] - 1 ) ) {
		s = e->spare[2];		// en-feel: who scared us
	}
	if ( s ) {
		BtEnemyOrigin( s - 1, out );
	} else if ( bs->enemy >= 0 && bs->enemy < MAX_CLIENTS && BtAlive( bs->enemy ) ) {
		BtEnemyOrigin( bs->enemy, out );
	} else {
		VectorCopy( bs->lastenemyorigin, out );
	}
}

static void BtHealThink( bot_state_t *bs ) {
	btBot_t		*b = &bt[bs->client];
	float		scale;
	vec3_t		threat;
	int			health = bs->inventory[INVENTORY_HEALTH], armor = bs->inventory[INVENTORY_ARMOR];
	gentity_t	*item;
	int			enemyHp;
	float		fear;		// en-memory: a bot the player dominates (G_BM_Fear) breaks off a little earlier from them

	if ( b->healEnt ) {
		item = &g_entities[b->healEnt];
		if ( !BtItemPresent( item ) || level.time > b->healUntil || health >= 95 ) {
			if ( b->healStartHP < 90 && ( health >= b->healStartHP + 20 || health >= 95 ) ) {
				BotTacCount( bs, BTC_HEALED );
				b->comebackUntil = level.time + 6000;
				b->comebackCounted = 0;
			}
			if ( bt_debug.integer ) {
				G_LogPrintf( "BtHeal: %i done hp %i -> %i\n", bs->client, b->healStartHP, health );
			}
			b->healEnt = 0;
			b->healNext = level.time + 6000;
		}
		return;
	}
	if ( level.time < b->healNext || !BotTacOn( bs, BTAC_RETREAT, &scale ) || BotRearmAggressive( bs ) ) {
		return;
	}
	b->healNext = level.time + 1000;
	if ( bs->enemy >= 0 && BotRearmPriorityTarget( bs, bs->enemy ) ) {
		return;
	}
	// only when losing: very low, or low and the enemy is much healthier
	enemyHp = ( bs->enemy >= 0 && bs->enemy < MAX_CLIENTS ) ? g_entities[bs->enemy].health : 0;
	fear = bs->enemy >= 0 && bs->enemy < MAX_CLIENTS && G_RL_IsPlayer( &g_entities[bs->enemy] ) ? G_BM_Fear( bs->client ) : 0;
	if ( !( health < 40 + 15 * fear || ( health < 65 + 10 * fear && armor < 15 && enemyHp > health + 30 - 15 * fear ) ) ) {
		return;
	}
	if ( random() >= 0.2f + 0.75f * scale ) {
		return;
	}
	BtThreat( bs, threat );
	b->healEnt = BtHealItem( bs, bs->enemy >= 0 ? threat : NULL, 450 );
	if ( !b->healEnt ) {
		return;
	}
	b->healUntil = level.time + 9000;
	b->healStartHP = health;
	BotTacCount( bs, BTC_RETREAT );
	if ( bt_debug.integer ) {
		G_LogPrintf( "BtHeal: %i hp %i armor %i goes for %s\n", bs->client, health, armor, g_entities[b->healEnt].item->classname );
	}
}

static void BtContestThink( bot_state_t *bs ) {
	btBot_t		*b = &bt[bs->client];
	btItem_t	*it, *best = NULL;
	gentity_t	*ent;
	float		scale, score, bestScore = 0;
	int			i, j, travel, wait, area, others;

	if ( b->contestEnt ) {
		it = BtItemByEnt( b->contestEnt );
		if ( !it || level.time > b->contestUntil || ( !it->avail && !it->respawnAt ) || bs->enemy >= 0 ) {
			b->contestEnt = 0;
			b->contestNext = level.time + 2000;
		}
		return;
	}
	if ( level.time < b->contestNext || !btNumItems || bs->enemy >= 0 ) {
		return;
	}
	b->contestNext = level.time + 1000;
	if ( !BotTacOn( bs, BTAC_ITEMS, &scale ) || BotRearmAggressive( bs ) ) {
		return;
	}
	if ( level.time >= b->contestAwareNext ) {
		b->contestAwareNext = level.time + 15000;
		b->contestAware = random() < 0.15f + 0.85f * scale;		// keeping track of the clock at all?
	}
	if ( !b->contestAware ) {
		return;
	}
	for ( i = 0 ; i < btNumItems ; i++ ) {
		it = &btItems[i];
		ent = &g_entities[it->ent];
		if ( !it->avail && !it->respawnAt ) {
			continue;
		}
		area = BtArea( ent->r.currentOrigin );
		travel = BtTravel( bs, area, ent->r.currentOrigin );
		if ( travel <= 0 || travel > 1200 ) {
			continue;
		}
		if ( it->avail ) {
			if ( travel > 500 ) {
				continue;
			}
		} else {
			wait = it->respawnAt - level.time;
			// leave so we're there a few seconds early, not half a minute
			if ( wait > travel * 10 + 1500 + (int)( 2500 * scale ) ) {
				continue;
			}
		}
		others = 0;
		for ( j = 0 ; j < level.maxclients ; j++ ) {
			if ( j != bs->client && bt[j].contestEnt == it->ent ) {
				others++;
			}
		}
		if ( others >= 2 ) {
			continue;
		}
		score = it->value / ( 1.0f + travel / 500.0f );
		if ( score > bestScore ) {
			bestScore = score;
			best = it;
		}
	}
	if ( !best || best->value < 2.0f || bestScore < 0.8f ) {		// the big ones only
		return;
	}
	b->contestEnt = best->ent;
	b->contestUntil = level.time + ( best->avail ? 10000 : best->respawnAt - level.time + 6000 );
	BotTacCount( bs, BTC_CONTEST );
	if ( bt_debug.integer ) {
		G_LogPrintf( "BtContest: %i -> %s %s in %i ms\n", bs->client, g_entities[best->ent].item->classname,
			best->avail ? "(up)" : "(timed)", best->avail ? 0 : best->respawnAt - level.time );
	}
}

static void BtHearThink( bot_state_t *bs ) {
	btBot_t		*b = &bt[bs->client];
	float		scale, d, bestD = 99999;
	vec3_t		where, best, ground;
	int			i, when, bestWho = -1, bestWhen = 0, area;

	if ( b->soundUntil ) {
		if ( level.time > b->soundUntil || bs->enemy >= 0 ) {
			b->soundUntil = 0;
		} else if ( BtFlatDist( bs->origin, b->soundSpot ) < 100 ) {
			BotTacCount( bs, BTC_HEAR_REACHED );
			b->soundUntil = 0;
			b->soundNext = level.time + 1500;
			b->preaimNext = 0;
		}
	}
	if ( !BotTacOn( bs, BTAC_SOUND, &scale ) ) {
		return;
	}
	// in a chase: follow the noise it makes (not where it really is)
	if ( bs->ainode == AINode_Battle_Chase && bs->enemy >= 0 && bs->enemy < MAX_CLIENTS ) {
		if ( BtCanHear( bs, bs->enemy, scale, 1.0f, where, &when ) && when > b->trackTime ) {
			b->trackTime = when;
			area = BtArea( where );
			if ( area ) {
				VectorCopy( where, bs->lastenemyorigin );
				bs->lastenemyareanum = area;
				bs->chase_time = FloatTime();
				BotTacCount( bs, BTC_TRACK );
			}
		}
		return;
	}
	if ( bs->enemy >= 0 || BtBattleNode( bs ) || level.time < b->soundNext ) {
		return;
	}
	// only go looking for a fight when fit for one: some health and a real gun (else the stock item run)
	if ( bs->inventory[INVENTORY_HEALTH] < 50 || ( bs->cur_ps.weapon <= WP_MACHINEGUN && bs->inventory[INVENTORY_HEALTH] < 120 ) ) {
		return;
	}
	b->soundNext = level.time + 300;
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( i == bs->client || BotSameTeam( bs, i ) ) {
			continue;
		}
		if ( !BtCanHear( bs, i, scale, 1.0f, where, &when ) || when <= b->soundHeard[i] ) {
			continue;
		}
		d = Distance( where, bs->origin );
		if ( d < bestD ) {
			bestD = d;
			bestWho = i;
			bestWhen = when;
			VectorCopy( where, best );
		}
	}
	if ( bestWho < 0 ) {
		return;
	}
	b->soundHeard[bestWho] = bestWhen;
	if ( bestD < 150 || random() >= 0.2f + 0.75f * scale ) {
		return;
	}
	if ( !BtGroundSpot( best, ground, bs->client ) ) {
		VectorCopy( best, ground );
	}
	area = BtArea( ground );
	if ( !area || BtTravel( bs, area, ground ) <= 0 ) {
		return;
	}
	if ( !b->soundUntil ) {
		BotTacCount( bs, BTC_HEAR );
		if ( bt_debug.integer ) {
			G_LogPrintf( "BtHear: %i hears %i at %.0f units\n", bs->client, bestWho, bestD );
		}
	}
	VectorCopy( ground, b->soundSpot );
	b->soundArea = area;
	b->soundUntil = level.time + 9000;
}

/*
The human just died: their gun on the floor is a prize (g_thief.c, g_botWeaponThief)
*/
static void BtPrizeThink( bot_state_t *bs ) {
	btBot_t		*b = &bt[bs->client];
	gentity_t	*e;
	int			i, j, area, travel, best = 0, bestTravel = 700, racers;

	if ( b->prizeEnt ) {
		if ( G_TH_PrizeOwner( &g_entities[b->prizeEnt] ) < 0 ) {
			b->prizeEnt = 0;
		}
		return;
	}
	if ( level.time < b->prizeNext || BtBoss( bs ) || G_TH_Thief( bs->client ) ) {
		return;
	}
	b->prizeNext = level.time + 400;
	for ( i = MAX_CLIENTS, e = &g_entities[MAX_CLIENTS] ; i < level.num_entities ; i++, e++ ) {
		if ( !e->inuse || !e->item || !( e->flags & FL_DROPPED_ITEM ) ) {
			continue;
		}
		j = G_TH_PrizeOwner( e );
		if ( j < 0 || j == bs->client ) {
			continue;
		}
		racers = 0;
		for ( j = 0 ; j < level.maxclients ; j++ ) {
			if ( j != bs->client && bt[j].prizeEnt == i ) {
				racers++;
			}
		}
		if ( racers >= 2 ) {
			continue;
		}
		area = BtArea( e->r.currentOrigin );
		travel = BtTravel( bs, area, e->r.currentOrigin );
		if ( travel > 0 && travel < bestTravel ) {
			bestTravel = travel;
			best = i;
		}
	}
	if ( best ) {
		b->prizeEnt = best;
		if ( bt_debug.integer ) {
			G_LogPrintf( "BtPrize: %i races for %s (%i)\n", bs->client, g_entities[best].item->classname, bestTravel );
		}
	}
}

static void BtLeaderThink( bot_state_t *bs ) {
	btBot_t		*b = &bt[bs->client];
	float		scale;
	int			i, focused, bots;

	if ( level.time < b->leaderNext ) {
		return;
	}
	b->leaderNext = level.time + 8000 + (int)( random() * 6000 );
	b->leaderFocus = 0;
	if ( btLeader < 0 || btLeader == bs->client || !BotTacOn( bs, BTAC_LEADER, &scale ) ) {
		return;
	}
	if ( BotSameTeam( bs, btLeader ) ) {
		return;
	}
	// a little more, not a pile-on: at most half the bots at once
	focused = bots = 0;
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( botstates[i] && botstates[i]->inuse ) {
			bots++;
			if ( bt[i].leaderFocus ) {
				focused++;
			}
		}
	}
	if ( focused * 2 >= bots ) {
		return;
	}
	b->leaderFocus = random() < 0.15f + 0.35f * scale;
}

/*
Look at where the human usually comes from while waiting
*/
static void BtPreaim( bot_state_t *bs ) {
	btBot_t		*b = &bt[bs->client];
	vec3_t		spot, dir;
	float		speed;
	float		scale;

	if ( bs->enemy >= 0 || ( bs->ainode != AINode_Seek_LTG && bs->ainode != AINode_Seek_NBG ) ) {
		return;
	}
	speed = sqrt( bs->cur_ps.velocity[0] * bs->cur_ps.velocity[0] + bs->cur_ps.velocity[1] * bs->cur_ps.velocity[1] );
	if ( speed > 120 && !b->contestEnt ) {
		return;
	}
	if ( !BotTacOn( bs, BTAC_HABITS, &scale ) ) {
		return;
	}
	if ( level.time >= b->preaimNext ) {
		b->preaimNext = level.time + 2500;
		if ( random() < 0.3f + 0.7f * scale && BotHabitsHotSpot( bs, spot ) ) {
			VectorCopy( spot, b->preaimSpot );
			b->preaimUntil = level.time + 2500;
			BotTacCount( bs, BTC_PREAIM );
		}
	}
	if ( level.time < b->preaimUntil ) {
		VectorSubtract( b->preaimSpot, bs->eye, dir );
		vectoangles( dir, bs->ideal_viewangles );
		bs->ideal_viewangles[2] = 0;
	}
}

static void BtBarrelThink( bot_state_t *bs ) {
	btBot_t		*b = &bt[bs->client];
	gentity_t	*prop;

	if ( b->lastPropEnt && !bs->rmPropEnt ) {
		prop = &g_entities[b->lastPropEnt];
		if ( !BotRearmExplosiveProp( prop ) || ( prop->s.frame & ( PROPF_FUSE | PROPF_RUPTURED | PROPF_BURNING ) ) ) {
			BotTacCount( bs, BTC_BARREL );
			if ( b->lureUntil || level.time < b->lureEndTime + 4000 ) {
				BotTacCount( bs, BTC_LURE_BLOWN );
				if ( bt_debug.integer ) {
					G_LogPrintf( "BtLure: %i blew the barrel\n", bs->client );
				}
			}
		}
	}
	b->lastPropEnt = bs->rmPropEnt;
}

void BotTacticsThink( bot_state_t *bs ) {
	btBot_t		*b = &bt[bs->client];
	int			g, panic, crawl;

	b->typeFlags = BotTypeThink( bs );
	if ( !g_botTactics.integer ) {
		return;
	}
	if ( !BtAlive( bs->client ) ) {
		b->flankUntil = b->healEnt = b->contestEnt = b->soundUntil = b->lureUntil = 0;
		b->wasPanic = b->wasCrawl = 0;
		b->lastEnemy = -1;
		return;
	}
	panic = BtPanic( bs );
	crawl = BotTacCrawling( bs );
	if ( panic && !b->wasPanic ) {
		BotTacCount( bs, BTC_PANIC );
		b->fleeUntil = 0;
	}
	if ( crawl && !b->wasCrawl ) {
		BotTacCount( bs, BTC_CRAWL );
		b->fleeUntil = 0;
	}
	b->wasPanic = panic;
	b->wasCrawl = crawl;

	BtHealThink( bs );
	BtPrizeThink( bs );
	BtContestThink( bs );
	BtHearThink( bs );
	BtLeaderThink( bs );
	BtBarrelThink( bs );
	BtPreaim( bs );

	// who did we just pick? (evidence for leader focus and grudges)
	if ( bs->enemy != b->lastEnemy && bs->enemy >= 0 && bs->enemy < MAX_CLIENTS ) {
		g = BtGrudge( bs );
		if ( g >= 0 && bs->enemy == g ) {
			BotTacCount( bs, BTC_GRUDGE );
			if ( bt_debug.integer ) {
				G_LogPrintf( "GrudgePick: %i -> %i\n", bs->client, g );
			}
		} else if ( b->leaderFocus && bs->enemy == btLeader ) {
			BotTacCount( bs, BTC_LEADER );
		}
	}
	b->lastEnemy = bs->enemy;
	if ( b->comebackUntil && level.time < b->comebackUntil && !b->comebackCounted
		&& ( bs->ainode == AINode_Battle_Chase || bs->ainode == AINode_Battle_Fight ) ) {
		b->comebackCounted = 1;
		BotTacCount( bs, BTC_COMEBACK );
	}
}

/*
===============================================================================

HOOKS IN THE STOCK AI

===============================================================================
*/
int BotTacticsWantsRetreat( bot_state_t *bs ) {
	btBot_t	*b = &bt[bs->client];

	if ( !g_botTactics.integer || BtBoss( bs ) ) {
		return -1;
	}
	if ( BtPanic( bs ) || BtCrawl( bs ) ) {
		return 1;
	}
	if ( b->healEnt ) {
		return 1;
	}
	if ( b->comebackUntil && level.time < b->comebackUntil ) {
		return 0;
	}
	return -1;
}

int BotTacticsWantsChase( bot_state_t *bs ) {
	btBot_t	*b = &bt[bs->client];

	if ( !g_botTactics.integer || BtBoss( bs ) ) {
		return -1;
	}
	if ( BtPanic( bs ) || BtCrawl( bs ) || b->healEnt ) {
		return 0;
	}
	if ( b->comebackUntil && level.time < b->comebackUntil && bs->lastenemyareanum ) {
		return 1;		// topped up: back at it
	}
	return -1;
}

float BotTacticsEnemyScale( bot_state_t *bs, int client ) {
	btBot_t	*b = &bt[bs->client];
	float	s = 1.0f, sk;

	if ( !g_botTactics.integer || client < 0 || client >= MAX_CLIENTS || BtBoss( bs ) ) {
		return 1.0f;
	}
	if ( BtSplitOut( bs ) ) {
		return 1.0f;
	}
	if ( client == BtGrudge( bs ) ) {
		sk = BotTacSkill( bs );
		// the higher the skill the more single-minded (x0.4 at most: a nearer threat still wins);
		// en-tune: scaled by the low-skill gate (skill 1: no grudge targeting, it ganged up on the player)
		s *= 1.0f - BotTacGate( bs ) * ( 0.25f + 0.35f * sk );
	}
	if ( b->leaderFocus && client == btLeader ) {
		s *= 0.5f;
	}
	if ( G_RL_IsPlayer( &g_entities[client] ) && G_BM_Fear( bs->client ) > 0 ) {
		s *= 1.3f;		// en-memory: a bot the player dominates would rather fight someone else
	}
	return s;
}

int BotTacticsRocketJump( bot_state_t *bs ) {
	btBot_t	*b = &bt[bs->client];
	float	scale;

	if ( !BotTacOn( bs, BTAC_RJUMP, &scale ) || BotTacCrawling( bs ) ) {
		return -1;
	}
	if ( bs->inventory[INVENTORY_ROCKETLAUNCHER] <= 0 || bs->inventory[INVENTORY_ROCKETS] < 3
		|| bs->inventory[INVENTORY_QUAD] ) {
		return -1;
	}
	if ( bs->inventory[INVENTORY_HEALTH] < 60 || bs->inventory[INVENTORY_HEALTH] + bs->inventory[INVENTORY_ARMOR] < 110 ) {
		return 0;
	}
	if ( level.time >= b->rjNext ) {
		b->rjNext = level.time + 10000;
		b->rjWilling = random() < 0.1f + 0.8f * scale;
	}
	return b->rjWilling ? 1 : -1;
}

/*
Long-term goal: panic / crawl flight, healing, item timing, grudge hunts, sounds.
*/
qboolean BotTacticsGoal( bot_state_t *bs, bot_goal_t *goal, int retreat ) {
	btBot_t		*b = &bt[bs->client];
	gentity_t	*ent;
	vec3_t		threat, where;
	int			area, when, g, item;
	float		scale;

	if ( !g_botTactics.integer || BtBoss( bs ) || !BtAlive( bs->client ) || ( b->typeFlags & BTT_OWNMOVE ) ) {
		return qfalse;
	}
	// panic: run (toward health if it's that way), crawl: toward health / away
	if ( BtPanic( bs ) || BtCrawl( bs ) ) {
		BtThreat( bs, threat );
		if ( BtCrawl( bs ) && !b->healEnt && bs->inventory[INVENTORY_HEALTH] < 90 ) {
			item = BtHealItem( bs, threat, 900 );
			if ( item ) {
				b->healEnt = item;
				b->healUntil = level.time + 15000;
				b->healStartHP = bs->inventory[INVENTORY_HEALTH];
			}
		}
		if ( !b->healEnt ) {
			if ( level.time > b->fleeUntil && !BtFleeSpot( bs, threat ) ) {
				return qfalse;
			}
			if ( b->fleeArea ) {
				BtGoalAt( goal, b->fleeSpot, b->fleeArea, -1 );
				return qtrue;
			}
			return qfalse;
		}
	}
	if ( b->healEnt ) {
		ent = &g_entities[b->healEnt];
		area = BtArea( ent->r.currentOrigin );
		if ( area ) {
			BtGoalAt( goal, ent->r.currentOrigin, area, b->healEnt );
			return qtrue;
		}
		b->healEnt = 0;
	}
	if ( retreat ) {
		return qfalse;
	}
	// the human's dropped gun: grab it (a thief uses it)
	if ( b->prizeEnt ) {
		ent = &g_entities[b->prizeEnt];
		area = BtArea( ent->r.currentOrigin );
		if ( area && G_TH_PrizeOwner( ent ) >= 0 ) {
			BtGoalAt( goal, ent->r.currentOrigin, area, b->prizeEnt );
			return qtrue;
		}
		b->prizeEnt = 0;
	}
	if ( b->contestEnt ) {
		ent = &g_entities[b->contestEnt];
		area = BtArea( ent->r.currentOrigin );
		if ( area ) {
			BtGoalAt( goal, ent->r.currentOrigin, area, b->contestEnt );
			return qtrue;
		}
		b->contestEnt = 0;
	}
	// grudge: hunt the killer by what we hear of it (twice the usual radius)
	g = BtGrudge( bs );
	if ( g >= 0 && BotTacOn( bs, BTAC_SOUND, &scale ) && BtCanHear( bs, g, scale, 2.0f, where, &when ) ) {
		area = BtArea( where );
		if ( area && BtTravel( bs, area, where ) > 0 ) {
			if ( !b->grudgeCounted || level.time - b->grudgeCounted > 8000 ) {
				BotTacCount( bs, BTC_GRUDGE_HUNT );
				if ( bt_debug.integer ) {
					G_LogPrintf( "GrudgeHunt: %i -> %i\n", bs->client, g );
				}
			}
			b->grudgeCounted = level.time;
			BtGoalAt( goal, where, area, -1 );
			return qtrue;
		}
	}
	if ( b->soundUntil && b->soundArea ) {
		BtGoalAt( goal, b->soundSpot, b->soundArea, -1 );
		return qtrue;
	}
	return qfalse;
}

/*
Battle_Chase: a flanker circles to where the enemy was last seen / heard instead of following
the others down the same corridor.
*/
void BotTacticsChaseGoal( bot_state_t *bs, bot_goal_t *goal ) {
	btBot_t	*b = &bt[bs->client];
	float	scale;

	if ( btRole[bs->client] == 0 || ( b->typeFlags & BTT_OWNMOVE ) || !BotTacOn( bs, BTAC_FLANK, &scale ) ) {
		return;
	}
	if ( !BtFlanking( bs ) && !BtStartFlank( bs, bs->lastenemyorigin, scale ) ) {
		return;
	}
	BtGoalAt( goal, b->flankSpot, b->flankArea, -1 );
}

/*
Strafe across the enemy, closing to / backing off to dist want
*/
static void BtRangeMove( bot_state_t *bs, float dist, float want, const vec3_t forward, bot_moveresult_t *moveresult ) {
	vec3_t	hordir, sideward, up = { 0, 0, 1 }, move;
	int		i;

	bs->attackstrafe_time += bs->thinktime;
	if ( bs->attackstrafe_time > 0.6f && random() > 0.9f ) {
		bs->flags ^= BFL_STRAFERIGHT;
		bs->attackstrafe_time = 0;
	}
	for ( i = 0 ; i < 2 ; i++ ) {
		VectorSet( hordir, forward[0], forward[1], 0 );
		VectorNormalize( hordir );
		CrossProduct( hordir, up, sideward );
		if ( bs->flags & BFL_STRAFERIGHT ) {
			VectorNegate( sideward, sideward );
		}
		VectorCopy( sideward, move );
		if ( dist < want - 80 ) {
			VectorMA( move, -1.2f, hordir, move );
		} else if ( dist > want + 160 ) {
			VectorMA( move, 1.0f, hordir, move );
		}
		if ( trap_BotMoveInDirection( bs->ms, move, 400, MOVE_WALK ) ) {
			return;
		}
		bs->flags ^= BFL_STRAFERIGHT;
		bs->attackstrafe_time = 0;
	}
}

/*
Lure: an explosive barrel near us that the enemy has to pass: get behind it, the prop shot
(ai_rearmed.c BotRearmPropShot) sets it off once the enemy is in its blast.
*/
static qboolean BtStartLure( bot_state_t *bs, const vec3_t enemyPos, float dist, float scale ) {
	btBot_t		*b = &bt[bs->client];
	gentity_t	*prop;
	vec3_t		away, spot, ground;
	float		r, dp, ep;
	int			i, area, travel;

	if ( level.time < b->lureNext ) {
		return qfalse;
	}
	b->lureNext = level.time + 1500;
	if ( dist < 250 || dist > 1000 || bs->cur_ps.weapon == WP_GAUNTLET || random() >= 0.2f + 0.7f * scale ) {
		return qfalse;
	}
	if ( !trap_Cvar_VariableIntegerValue( "bot_useProps" ) ) {
		return qfalse;		// the prop shot is what springs the trap
	}
	for ( i = MAX_CLIENTS, prop = &g_entities[MAX_CLIENTS] ; i < level.num_entities ; i++, prop++ ) {
		if ( !BotRearmExplosiveProp( prop ) || ( prop->s.frame & PROPF_DANGER ) ) {
			continue;
		}
		r = BotRearmPropRadius( prop );
		dp = Distance( prop->r.currentOrigin, bs->origin );
		ep = Distance( prop->r.currentOrigin, enemyPos );
		if ( dp > 500 || ep < r || ep > dist + 150 ) {
			continue;		// too far for us / already in its blast / not on the enemy's way
		}
		VectorSubtract( prop->r.currentOrigin, enemyPos, away );
		away[2] = 0;
		if ( VectorNormalize( away ) < 1 ) {
			continue;
		}
		VectorMA( prop->r.currentOrigin, r + 110, away, spot );
		if ( !BtGroundSpot( spot, ground, bs->client ) || fabs( ground[2] - prop->r.currentOrigin[2] ) > 96 ) {
			continue;
		}
		area = BtArea( ground );
		travel = BtTravel( bs, area, ground );
		if ( !area || travel <= 0 || travel > 300 ) {
			continue;
		}
		if ( !BtLOS( ground, prop->r.currentOrigin, bs->client ) ) {
			continue;
		}
		VectorCopy( ground, b->lureSpot );
		b->lureArea = area;
		b->lureProp = i;
		b->lureUntil = level.time + 5500;
		BotTacCount( bs, BTC_LURE );
		if ( bt_debug.integer ) {
			G_LogPrintf( "BtLure: %i backs behind barrel %i (enemy %.0f from it)\n", bs->client, i, ep );
		}
		return qtrue;
	}
	return qfalse;
}

qboolean BotTacticsAttackMove( bot_state_t *bs, int tfl, bot_moveresult_t *moveresult ) {
	btBot_t		*b = &bt[bs->client];
	vec3_t		enemyPos, forward, threat;
	float		dist, scale, want;
	gentity_t	*prop;

	if ( !g_botTactics.integer || bs->enemy < 0 || bs->enemy >= MAX_CLIENTS || ( b->typeFlags & BTT_OWNMOVE )
		|| BtBoss( bs ) || bs->attackchase_time > FloatTime() ) {
		return qfalse;
	}
	memset( moveresult, 0, sizeof( *moveresult ) );
	BtEnemyOrigin( bs->enemy, enemyPos );
	VectorSubtract( enemyPos, bs->origin, forward );
	dist = VectorNormalize( forward );

	// panic: get away from it (shooting back over the shoulder is en-feel's call)
	if ( BtPanic( bs ) || BtCrawl( bs ) ) {
		BtThreat( bs, threat );
		if ( level.time > b->fleeUntil ) {
			BtFleeSpot( bs, threat );
		}
		if ( b->fleeArea && level.time <= b->fleeUntil + 1500 ) {
			BtMoveTo( bs, tfl, b->fleeSpot, b->fleeArea, moveresult );
			return qtrue;
		}
		BotSetupForMovement( bs );
		VectorNegate( forward, forward );
		trap_BotMoveInDirection( bs->ms, forward, 400, MOVE_WALK );
		return qtrue;
	}

	// lure: behind the barrel, wait there for them
	if ( b->lureUntil ) {
		prop = &g_entities[b->lureProp];
		if ( level.time > b->lureUntil || !BotRearmExplosiveProp( prop ) || ( prop->s.frame & PROPF_DANGER ) ) {
			b->lureUntil = 0;
			b->lureEndTime = level.time;
		} else if ( BtFlatDist( bs->origin, b->lureSpot ) > 48 ) {
			if ( !BtMoveTo( bs, tfl, b->lureSpot, b->lureArea, moveresult ) ) {
				b->lureUntil = 0;
				b->lureEndTime = level.time;
				return qfalse;
			}
			return qtrue;
		} else {
			// in place: small strafes, keep the barrel between us
			BotSetupForMovement( bs );
			BtRangeMove( bs, dist, dist, forward, moveresult );
			return qtrue;
		}
	} else if ( BotTacOn( bs, BTAC_LURE, &scale ) && !BotRearmAggressive( bs ) && BtStartLure( bs, enemyPos, dist, scale ) ) {
		BtMoveTo( bs, tfl, b->lureSpot, b->lureArea, moveresult );
		return qtrue;
	}

	// flank: a second bot on the same target goes round
	if ( btRole[bs->client] > 0 && BotTacOn( bs, BTAC_FLANK, &scale ) ) {
		if ( BtFlanking( bs ) || BtStartFlank( bs, enemyPos, scale ) ) {
			if ( BtMoveTo( bs, tfl, b->flankSpot, b->flankArea, moveresult ) ) {
				return qtrue;
			}
			b->flankUntil = 0;
		}
	}

	// keep a range: the archetype's, else one that counters the human's habits
	want = 0;
	if ( BotTacOn( bs, BTAC_RANGE, &scale ) ) {
		want = BotTypeRange( bs );
		if ( want <= 0 ) {
			want = BotHabitsRange( bs, bs->enemy );
		}
	}
	if ( want > 0 && bs->cur_ps.weapon != WP_GAUNTLET ) {
		if ( level.time >= b->rangeNext ) {
			b->rangeNext = level.time + 5000;
			BotTacCount( bs, BTC_RANGE );
		}
		BotSetupForMovement( bs );
		BtRangeMove( bs, dist, want, forward, moveresult );
		return qtrue;
	}
	return qfalse;
}

int BotTacticsChooseWeapon( bot_state_t *bs, int weapon ) {
	int		w;

	// a weapon thief uses what it took (g_botWeaponThief, works without g_botTactics)
	w = G_TH_Thief( bs->client );
	if ( w ) {
		return w;
	}
	if ( g_botTactics.integer ) {
		w = BotHabitsWeapon( bs, weapon );
		if ( w != weapon ) {
			weapon = w;
		}
	}
	return BotTypeChooseWeapon( bs, weapon );
}

void BotTacticsInput( bot_state_t *bs, usercmd_t *ucmd ) {
	if ( g_botTactics.integer ) {
		BotTacMoveInput( bs, ucmd );
	}
	BotTypeInput( bs, ucmd );
}

/*
===============================================================================

FRAME, STATS, COMMANDS

===============================================================================
*/
static void BtStatsPrint( qboolean toLog ) {
	char	buf[1024];
	int		i, even = 0, odd = 0, score;

	buf[0] = 0;
	for ( i = 0 ; i < BTC_NUM ; i++ ) {
		Q_strcat( buf, sizeof( buf ), va( " %s=%i", btCountNames[i], btCount[i] ) );
	}
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( !g_entities[i].inuse || !g_entities[i].client || g_entities[i].client->pers.connected != CON_CONNECTED ) {
			continue;
		}
		score = g_entities[i].client->ps.persistant[PERS_SCORE];
		if ( i & 1 ) {
			odd += score;
		} else {
			even += score;
		}
	}
	if ( toLog ) {
		G_LogPrintf( "BtStats: t=%i split=%i even=%i odd=%i%s\n", ( level.time - level.startTime ) / 1000, bt_split.integer, even, odd, buf );
	} else {
		G_Printf( "BtStats: t=%i split=%i even=%i odd=%i%s\n", ( level.time - level.startTime ) / 1000, bt_split.integer, even, odd, buf );
	}
}

/*
en-tune bench (bt_bench 1, tests only): client 0 is the stand-in "player". Logs
"BtBench: in <ms>" = from the first hit it took in an engagement to its death (the enemies' time
to kill it) and "BtBench: out <ms>" = from its first hit on a bot to that bot's death by it.
An engagement ends after 4 s without a hit.
*/
static vmCvar_t	bt_bench;
static int		btbReg;
static int		btbHp[MAX_CLIENTS], btbStart[MAX_CLIENTS], btbLast[MAX_CLIENTS];

static void BtBenchFrame( void ) {
	gentity_t	*e;
	int			i, hp, by;

	if ( !bt_bench.integer || level.intermissiontime ) {
		return;
	}
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		e = &g_entities[i];
		if ( !e->inuse || !e->client || e->client->pers.connected != CON_CONNECTED ) {
			btbHp[i] = btbStart[i] = 0;
			continue;
		}
		hp = e->health;
		by = e->client->lasthurt_client;
		if ( btbStart[i] && level.time - btbLast[i] > 4000 ) {
			btbStart[i] = 0;
		}
		if ( btbHp[i] > 0 && hp < btbHp[i] && ( i == 0 ? by != 0 : by == 0 ) ) {
			if ( !btbStart[i] ) {
				btbStart[i] = level.time;
			}
			btbLast[i] = level.time;
		}
		if ( btbHp[i] > 0 && hp <= 0 ) {
			if ( btbStart[i] && ( i == 0 ? by != 0 : by == 0 ) ) {
				G_LogPrintf( "BtBench: %s %i\n", i == 0 ? "in" : "out", level.time - btbStart[i] );
			}
			btbStart[i] = 0;
		}
		btbHp[i] = hp;
	}
}

void BotTacticsFrame( void ) {
	BtCvars();
	if ( !btbReg ) {
		btbReg = 1;
		trap_Cvar_Register( &bt_bench, "bt_bench", "0", 0 );
	}
	trap_Cvar_Update( &bt_bench );
	BtBenchFrame();
	if ( btLevelStart != level.startTime ) {
		// a new map / restart: everything learned in the last one goes (habits keep the run)
		btLevelStart = level.startTime;
		memset( bt, 0, sizeof( bt ) );
		memset( btSnd, 0, sizeof( btSnd ) );
		memset( btCount, 0, sizeof( btCount ) );
		btNumItems = 0;
		btItemScan = 0;
		btRoleTime = 0;
		btLeader = -1;
		btStatsNext = level.time + 60000;
		btStatsFinal = 0;
		BotHabitsInit();
	}
	BtSoundFrame();
	// the count line (also with the tactics off, for baseline logs): every minute, once at the end
	if ( bt_debug.integer && !level.intermissiontime && level.time >= btStatsNext ) {
		btStatsNext = level.time + 60000;
		BtStatsPrint( qtrue );
		BotHabitsReport();
	}
	if ( bt_debug.integer && level.intermissiontime && !btStatsFinal ) {
		btStatsFinal = 1;
		BtStatsPrint( qtrue );
		BotHabitsReport();
	}
	if ( !g_botTactics.integer ) {
		return;
	}
	BtItemScan();
	BtRoles();
	BotHabitsFrame();
}

qboolean BotTacticsCommand( const char *cmd ) {
	if ( !Q_stricmp( cmd, "btstats" ) ) {
		BtStatsPrint( qfalse );
		BtStatsPrint( qtrue );
		BotHabitsReport();
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "btreset" ) ) {
		memset( btCount, 0, sizeof( btCount ) );
		G_Printf( "btstats reset\n" );
		return qtrue;
	}
	return qfalse;
}
