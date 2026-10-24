/*
===========================================================================
REARMED round 4 -- archetype bot behaviour (OWNER: en-types).
API: game/ai_tactics.h ("archetype hooks", called by en-brains' code). Archetype:
g_enemy[bs->client].arch (DESCENT fight floors only, 0 elsewhere). The type mechanics that are
not "how the bot plays" (heal ticks, fuses, teleports, rituals, the shield's block) live in
g_ds_types.c; this file decides where the bot goes, what it holds and when it fires.

SNIPER     perches high with a long sightline; every rail shot has a 0.7 s glint first (fire
           held, DSEF_TELEGRAPH), then it relocates to another perch
RUSHER     charges in, circles close with the shotgun, never retreats
MEDIC      follows a wounded ally and stays behind it (away from the enemy), keeps range
SHIELD     cycles: shield up and advancing (no fire, DSEF_SHIELDUP) / lowered and firing
KAMIKAZE   runs straight at the player, no tactics, gauntlet only (the fuse: g_ds_types.c)
BLINKER    flanking teleports every ~5-7 s (spot rules in g_ds_types.c)
SUMMONER   walks to a corpse far from the player and stands still through the raise
MIMIC      holds its copy of the player's weapon
JUGGERNAUT walks at its enemy with the chaingun, rockets at range, never retreats
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
#include "ai_tactics.h"
#include "bg_ds_types.h"

typedef struct {
	int			arch;
	// sniper
	int			tgStart, tgWant, relocateUntil, perchAt, perchArrive;
	qboolean	havePerch;
	vec3_t		perch;
	// shield
	int			shieldNext;
	qboolean	shieldUp;
	// summoner
	int			corpse, corpseCheck;
	// medic
	int			allyCheck, ally;
} aiType_t;

static aiType_t	aiT[MAX_CLIENTS];

// en-tune: an adapted sniper (g_dsTypeAdapt) glints shorter and leaves an idle perch sooner
static int AIT_Windup( void ) {
	return G_DS_TypeAdapted( DSAR_SNIPER ) ? DST_SNIPE_WINDUP_ADAPT : DST_SNIPE_WINDUP;
}

static int AIT_Idle( void ) {
	return G_DS_TypeAdapted( DSAR_SNIPER ) ? DST_SNIPE_IDLE_ADAPT : DST_SNIPE_IDLE;
}

static int AIT_Arch( bot_state_t *bs ) {
	if ( !bs || bs->client < 0 || bs->client >= MAX_CLIENTS ) {
		return 0;
	}
	return g_enemy[bs->client].arch;
}

static aiType_t *AIT_State( bot_state_t *bs, int arch ) {
	aiType_t	*st = &aiT[bs->client];

	if ( st->arch != arch ) {
		memset( st, 0, sizeof( *st ) );
		st->arch = arch;
		st->corpse = st->ally = -1;
		st->shieldUp = qtrue;
	}
	return st;
}

static gentity_t *AIT_Ent( int n ) {
	if ( n < 0 || n >= MAX_CLIENTS ) {
		return NULL;
	}
	if ( !g_entities[n].inuse || !g_entities[n].client || g_entities[n].health <= 0 ) {
		return NULL;
	}
	return &g_entities[n];
}

static gentity_t *AIT_Human( void ) {
	return AIT_Ent( G_RL_HumanClient() );
}

static gentity_t *AIT_Enemy( bot_state_t *bs ) {
	return AIT_Ent( bs->enemy );
}

static qboolean AIT_EnemyVisible( bot_state_t *bs ) {
	return bs->enemy >= 0 && bs->enemyvisible_time > FloatTime() - 0.5f;
}

static float AIT_Dist( bot_state_t *bs, gentity_t *e ) {
	return e ? Distance( bs->origin, e->r.currentOrigin ) : 99999.0f;
}

static qboolean AIT_Has( bot_state_t *bs, int w ) {
	gclient_t	*cl = g_entities[bs->client].client;

	if ( w <= WP_NONE || w >= WP_NUM_WEAPONS || !cl ) {
		return qfalse;
	}
	return ( cl->ps.stats[STAT_WEAPONS] & ( 1 << w ) ) && ( cl->ps.ammo[w] != 0 || w == WP_GAUNTLET );
}

static qboolean AIT_Goal( vec3_t pos, bot_goal_t *goal ) {
	int		area = BotPointAreaNum( pos );

	if ( !area || !trap_AAS_AreaReachability( area ) ) {
		return qfalse;
	}
	memset( goal, 0, sizeof( *goal ) );
	VectorCopy( pos, goal->origin );
	goal->areanum = area;
	VectorSet( goal->mins, -8, -8, -8 );
	VectorSet( goal->maxs, 8, 8, 8 );
	goal->entitynum = -1;
	return qtrue;
}

// steer this frame (after the node moved): qtrue = we moved
static qboolean AIT_MoveTo( bot_state_t *bs, vec3_t pos, float stopDist ) {
	bot_goal_t			goal;
	bot_moveresult_t	mr;

	if ( Distance( bs->origin, pos ) < stopDist ) {
		return qfalse;
	}
	if ( !AIT_Goal( pos, &goal ) ) {
		return qfalse;
	}
	memset( &mr, 0, sizeof( mr ) );
	trap_BotMoveToGoal( &mr, bs->ms, &goal, bs->tfl );
	if ( mr.failure ) {
		trap_BotResetAvoidReach( bs->ms );
		return qfalse;
	}
	// nobody to aim at: look where we go
	if ( bs->enemy < 0 && !( mr.flags & ( MOVERESULT_MOVEMENTVIEWSET | MOVERESULT_MOVEMENTVIEW ) ) && VectorLengthSquared( mr.movedir ) > 0.01f ) {
		vectoangles( mr.movedir, bs->ideal_viewangles );
		bs->ideal_viewangles[PITCH] = 0;
	}
	return qtrue;
}

/*
===============================================================================

SNIPER PERCHES

===============================================================================
*/

static void AIT_Eye( gentity_t *e, vec3_t out ) {
	VectorCopy( e->r.currentOrigin, out );
	out[2] += e->client ? e->client->ps.viewheight : 24;
}

// a spot high above the target with a clear line to it, not too far to walk
static qboolean AIT_PickPerch( bot_state_t *bs, aiType_t *st ) {
	gentity_t	*t = AIT_Enemy( bs ), *e;
	vec3_t		p, eye, te;
	trace_t		tr;
	float		d, score, best = -1e9f;
	int			i;
	qboolean	found = qfalse;

	if ( !t ) {
		t = AIT_Human();
	}
	if ( !t ) {
		return qfalse;
	}
	AIT_Eye( t, te );
	for ( i = MAX_CLIENTS ; i < level.num_entities ; i++ ) {
		e = &g_entities[i];
		if ( !e->inuse ) {
			continue;
		}
		if ( !e->item && Q_stricmp( e->classname, "info_player_deathmatch" ) ) {
			continue;
		}
		VectorCopy( e->s.origin, p );
		if ( e->item ) {
			VectorCopy( e->r.currentOrigin, p );
		}
		p[2] += 8;
		d = Distance( p, t->r.currentOrigin );
		if ( d < 550 || d > 2200 ) {
			continue;
		}
		if ( st->havePerch && Distance( p, st->perch ) < 220 ) {
			continue;		// relocating: somewhere else
		}
		if ( Distance( p, bs->origin ) > 1700 ) {
			continue;
		}
		if ( !BotPointAreaNum( p ) ) {
			continue;
		}
		VectorCopy( p, eye );
		eye[2] += 32;
		trap_Trace( &tr, eye, NULL, NULL, te, ENTITYNUM_NONE, MASK_SOLID );
		if ( tr.fraction < 1.0f ) {
			continue;
		}
		score = ( p[2] - t->r.currentOrigin[2] ) * 1.5f + ( d > 1400 ? 1400 : d ) * 0.2f - Distance( p, bs->origin ) * 0.3f
			+ ( rand() % 60 );
		if ( score > best ) {
			best = score;
			VectorCopy( p, st->perch );
			found = qtrue;
		}
	}
	st->perchAt = level.time;
	st->havePerch = found;
	if ( found && trap_Cvar_VariableIntegerValue( "g_dsTypesDebug" ) ) {
		G_LogPrintf( "DSTypes: sniper %i perch %s (%.0f above its target, %.0f away)\n", bs->client, vtos( st->perch ),
			st->perch[2] - t->r.currentOrigin[2], Distance( st->perch, t->r.currentOrigin ) );
	}
	return found;
}

/*
===============================================================================

THE HOOKS

===============================================================================
*/

int BotTypeThink( bot_state_t *bs ) {
	int			a = AIT_Arch( bs ), c;
	aiType_t	*st;
	gentity_t	*en, *h, *self, *p;
	vec3_t		pos, dir;
	float		d;

	if ( !a ) {
		return 0;
	}
	st = AIT_State( bs, a );
	self = &g_entities[bs->client];
	if ( self->health <= 0 || !self->client ) {
		st->tgStart = 0;
		st->havePerch = qfalse;
		st->relocateUntil = 0;
		st->corpse = -1;
		return 0;
	}
	en = AIT_Enemy( bs );
	switch ( a ) {
	case DSAR_SNIPER:
		if ( !st->havePerch && level.time - st->perchAt > 3000 ) {
			AIT_PickPerch( bs, st );
		}
		if ( st->relocateUntil > level.time && st->havePerch && !st->tgStart ) {
			if ( AIT_MoveTo( bs, st->perch, 48 ) ) {
				return BTT_OWNMOVE;
			}
		}
		return 0;
	case DSAR_RUSHER:
		if ( en && AIT_Dist( bs, en ) > 260 ) {
			VectorCopy( en->r.currentOrigin, pos );
			// en-tune adapted: charge at a point off the target's side (one side per bot), straight in at the end
			if ( G_DS_TypeAdapted( DSAR_RUSHER ) && AIT_Dist( bs, en ) > 420 && en->client ) {
				vec3_t	ang, side, p2;

				VectorSet( ang, 0, en->client->ps.viewangles[YAW], 0 );
				AngleVectors( ang, NULL, side, NULL );
				VectorMA( pos, ( bs->client & 1 ) ? DST_RUSH_SIDE_ADAPT : -DST_RUSH_SIDE_ADAPT, side, p2 );
				if ( BotPointAreaNum( p2 ) ) {
					VectorCopy( p2, pos );
				}
			}
			if ( AIT_MoveTo( bs, pos, 0 ) ) {
				return BTT_OWNMOVE;
			}
		}
		return 0;
	case DSAR_KAMIKAZE:
		h = AIT_Human();
		if ( h ) {
			VectorCopy( h->r.currentOrigin, pos );
			if ( AIT_MoveTo( bs, pos, 0 ) ) {
				return BTT_OWNMOVE | BTT_NOTACTICS;
			}
		}
		return BTT_NOTACTICS;
	case DSAR_MEDIC:
		c = G_DS_TypesPatient( bs->client );
		p = AIT_Ent( c );
		if ( !p ) {
			// nobody hurt: stay near the closest ally bot
			if ( level.time >= st->allyCheck ) {
				st->allyCheck = level.time + 1000;
				st->ally = -1;
				d = 1400;
				for ( c = 0 ; c < level.maxclients ; c++ ) {
					gentity_t	*o = AIT_Ent( c );

					if ( !o || c == bs->client || G_RL_IsPlayer( o ) || !( o->r.svFlags & SVF_BOT ) ) {
						continue;
					}
					if ( AIT_Dist( bs, o ) < d ) {
						d = AIT_Dist( bs, o );
						st->ally = c;
					}
				}
			}
			p = AIT_Ent( st->ally );
			if ( !p || AIT_Dist( bs, p ) < 320 ) {
				return 0;
			}
		}
		// behind the patient, away from the enemy
		VectorCopy( p->r.currentOrigin, pos );
		if ( en && en != p ) {
			VectorSubtract( p->r.currentOrigin, en->r.currentOrigin, dir );
			dir[2] = 0;
			VectorNormalize( dir );
			VectorMA( pos, G_DS_TypeAdapted( DSAR_MEDIC ) ? DST_MEDIC_BACK_ADAPT : DST_MEDIC_BACK, dir, pos );		// en-tune
			if ( !BotPointAreaNum( pos ) ) {
				VectorCopy( p->r.currentOrigin, pos );
			}
		}
		if ( AIT_MoveTo( bs, pos, 140 ) ) {
			return BTT_OWNMOVE;
		}
		return 0;
	case DSAR_SHIELD:
		if ( en && st->shieldUp && AIT_Dist( bs, en ) > 330 ) {
			VectorCopy( en->r.currentOrigin, pos );
			if ( AIT_MoveTo( bs, pos, 0 ) ) {
				return BTT_OWNMOVE;
			}
		}
		return 0;
	case DSAR_BLINKER:
		if ( en && AIT_Dist( bs, en ) < 1600 && G_DS_TypesBlink( bs->client, bs->enemy, qfalse ) ) {
			trap_BotResetMoveState( bs->ms );
			trap_BotResetAvoidReach( bs->ms );
			return BTT_OWNMOVE;
		}
		return 0;
	case DSAR_SUMMONER:
		if ( G_DS_TypesInRitual( bs->client ) ) {
			return BTT_OWNMOVE | BTT_NOTACTICS;
		}
		if ( level.time >= st->corpseCheck ) {
			st->corpseCheck = level.time + 900;
			st->corpse = G_DS_TypesRaiseCorpse( bs->client );
		}
		if ( st->corpse >= 0 ) {
			gentity_t	*b = &g_entities[st->corpse];

			if ( !b->inuse || b->s.eType != ET_PLAYER ) {
				st->corpse = -1;
				return 0;
			}
			if ( Distance( bs->origin, b->r.currentOrigin ) < 220 ) {
				if ( G_DS_TypesRitual( bs->client, st->corpse ) ) {
					st->corpse = -1;
					return BTT_OWNMOVE | BTT_NOTACTICS;
				}
				st->corpse = -1;
				return 0;
			}
			VectorCopy( b->r.currentOrigin, pos );
			if ( AIT_MoveTo( bs, pos, 0 ) ) {
				return BTT_OWNMOVE;
			}
		}
		return 0;
	case DSAR_JUGGERNAUT:
		if ( en && AIT_Dist( bs, en ) > 520 ) {
			VectorCopy( en->r.currentOrigin, pos );
			if ( AIT_MoveTo( bs, pos, 0 ) ) {
				return BTT_OWNMOVE;
			}
		}
		return 0;
	default:
		return 0;
	}
}

int BotTypeChooseWeapon( bot_state_t *bs, int weapon ) {
	int			a = AIT_Arch( bs ), w = WP_NONE;
	float		d;
	gentity_t	*en;

	if ( !a ) {
		return weapon;
	}
	en = AIT_Enemy( bs );
	d = en ? AIT_Dist( bs, en ) : 600;
	switch ( a ) {
	case DSAR_SNIPER:
		w = WP_RAILGUN;
		break;
	case DSAR_RUSHER:
		w = d < 64 && AIT_Has( bs, WP_GAUNTLET ) && !AIT_Has( bs, WP_SHOTGUN ) ? WP_GAUNTLET : WP_SHOTGUN;
		break;
	case DSAR_MEDIC:
		w = WP_MACHINEGUN;
		break;
	case DSAR_SHIELD:
		w = WP_PLASMAGUN;
		break;
	case DSAR_KAMIKAZE:
		return WP_GAUNTLET;
	case DSAR_BLINKER:
		w = d < 720 ? WP_LIGHTNING : WP_SHOTGUN;
		break;
	case DSAR_SUMMONER:
		w = d > 180 ? WP_GRENADE_LAUNCHER : WP_MACHINEGUN;
		break;
	case DSAR_MIMIC:
		w = g_entities[bs->client].client ? g_entities[bs->client].client->ps.weapon : WP_NONE;
		{
			int		k, best = -1, bl = -1, l;

			// the copied weapon is the one at the player's level (the highest it has)
			for ( k = WP_MACHINEGUN ; k < WP_NUM_WEAPONS && k < DS_NUM_WEAPONS ; k++ ) {
				if ( AIT_Has( bs, k ) && DS_WEAPON_LEVELS( k ) ) {
					l = G_DS_WeaponLevel( &g_entities[bs->client], k );
					if ( l > bl ) {
						bl = l;
						best = k;
					}
				}
			}
			if ( best > 0 ) {
				w = best;
			}
		}
		break;
	case DSAR_JUGGERNAUT:
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
		w = d > ( G_DS_TypeAdapted( DSAR_JUGGERNAUT ) ? DST_JUGG_ROCKET_ADAPT : DST_JUGG_ROCKET ) && AIT_Has( bs, WP_ROCKET_LAUNCHER )
			? WP_ROCKET_LAUNCHER : WP_CHAINGUN;		// en-tune: sooner when adapted
#else
		w = WP_ROCKET_LAUNCHER;
#endif
		break;
	default:
		break;
	}
	if ( w > WP_NONE && AIT_Has( bs, w ) ) {
		return w;
	}
	if ( a == DSAR_JUGGERNAUT && AIT_Has( bs, WP_ROCKET_LAUNCHER ) ) {
		return WP_ROCKET_LAUNCHER;
	}
	return weapon;
}

// 0..1 multiplier on en-brains' tactics per archetype
float BotTypeTactic( bot_state_t *bs, int tactic ) {
	switch ( AIT_Arch( bs ) ) {
	case DSAR_SNIPER:
		return tactic == BTAC_FLANK || tactic == BTAC_LURE ? 0.0f : tactic == BTAC_RJUMP ? 0.3f : tactic == BTAC_ITEMS ? 0.4f : 1.0f;
	case DSAR_RUSHER:
		return tactic == BTAC_RETREAT || tactic == BTAC_LURE ? 0.0f : tactic == BTAC_ITEMS ? 0.3f
			: tactic == BTAC_FLANK ? ( G_DS_TypeAdapted( DSAR_RUSHER ) ? 1.0f : 0.4f ) : 1.0f;		// en-tune: flanks fully when adapted
	case DSAR_MEDIC:
		return tactic == BTAC_FLANK || tactic == BTAC_LURE || tactic == BTAC_LEADER ? 0.0f : 1.0f;
	case DSAR_SHIELD:
		return tactic == BTAC_FLANK || tactic == BTAC_RJUMP || tactic == BTAC_LURE ? 0.0f : tactic == BTAC_DODGE ? 0.2f
			: tactic == BTAC_RETREAT ? 0.3f : 1.0f;
	case DSAR_KAMIKAZE:
		return tactic == BTAC_SOUND || tactic == BTAC_HABITS ? 1.0f : 0.0f;
	case DSAR_BLINKER:
		return tactic == BTAC_FLANK ? 0.3f : 1.0f;		// its blinks do the flanking
	case DSAR_SUMMONER:
		return tactic == BTAC_FLANK || tactic == BTAC_LURE ? 0.0f : 1.0f;
	case DSAR_JUGGERNAUT:
		return tactic == BTAC_RETREAT || tactic == BTAC_FLANK || tactic == BTAC_RJUMP || tactic == BTAC_LURE
			|| tactic == BTAC_DODGE ? 0.0f : 1.0f;
	default:
		return 1.0f;
	}
}

float BotTypeRange( bot_state_t *bs ) {
	int		a = AIT_Arch( bs );

	if ( !a ) {
		return 0;
	}
	return ds_types[a].range;
}

/*
=================
BotTypeInput

The last word on the usercmd: the sniper's glint before every rail shot, the shield's
up / down cycle, the kamikaze's empty hands, the summoner standing through its ritual.
=================
*/
void BotTypeInput( bot_state_t *bs, usercmd_t *ucmd ) {
	int				a = AIT_Arch( bs );
	aiType_t		*st;
	gentity_t		*self, *en;
	playerState_t	*ps;
	qboolean		wants;

	if ( !a ) {
		return;
	}
	st = AIT_State( bs, a );
	self = &g_entities[bs->client];
	if ( !self->client || self->health <= 0 ) {
		if ( st->tgStart ) {
			st->tgStart = 0;
		}
		return;
	}
	ps = &self->client->ps;
	en = AIT_Enemy( bs );
	wants = ( ucmd->buttons & BUTTON_ATTACK ) != 0;
	switch ( a ) {
	case DSAR_SNIPER:
		if ( ps->weapon != WP_RAILGUN ) {
			if ( st->tgStart ) {
				st->tgStart = 0;
				G_DS_TypesTelegraph( bs->client, 0 );
			}
			return;
		}
		ucmd->buttons &= ~BUTTON_ALTATTACK;
		if ( !st->tgStart ) {
			ucmd->buttons &= ~BUTTON_ATTACK;		// never a rail without the glint
			if ( wants && ps->weaponTime <= 0 && en && !G_DS_TypesOtherTelegraph( bs->client ) ) {
				st->tgStart = level.time;
				st->tgWant = level.time;
				G_DS_TypesTelegraph( bs->client, AIT_Windup() );
			}
		} else if ( level.time < st->tgStart + AIT_Windup() ) {
			ucmd->buttons &= ~BUTTON_ATTACK;
			if ( wants ) {
				st->tgWant = level.time;
			} else if ( level.time - st->tgWant > 600 || !en ) {
				st->tgStart = 0;		// lost the shot: the glint goes out
				G_DS_TypesTelegraph( bs->client, 0 );
			}
		} else {
			ucmd->buttons |= BUTTON_ATTACK;
			if ( ps->weaponTime > 0 || level.time > st->tgStart + AIT_Windup() + 300 ) {
				st->tgStart = 0;
				G_DS_TypesTelegraph( bs->client, 0 );
				G_DS_TypesSnipe( bs->client, bs->enemy, en ? AIT_Dist( bs, en ) : 0 );
				// relocate
				st->relocateUntil = level.time + DST_SNIPE_RELOCATE;
				AIT_PickPerch( bs, st );
			}
		}
		return;
	case DSAR_SHIELD:
		if ( !st->shieldNext ) {
			st->shieldNext = level.time + DST_SHIELD_UP_MIN;
			st->shieldUp = qtrue;
		}
		if ( level.time >= st->shieldNext ) {
			if ( st->shieldUp ) {
				if ( en && AIT_EnemyVisible( bs ) ) {
					st->shieldUp = qfalse;
					st->shieldNext = level.time + DST_SHIELD_DOWN_MIN + rand() % ( DST_SHIELD_DOWN_MAX - DST_SHIELD_DOWN_MIN );
					if ( trap_Cvar_VariableIntegerValue( "g_dsTypesDebug" ) ) {
						G_LogPrintf( "DSTypes: shield %i lowered to fire at %i for %i ms\n", bs->client, bs->enemy, st->shieldNext - level.time );
					}
				} else {
					st->shieldNext = level.time + 300;		// nothing to shoot: keep it up
				}
			} else {
				st->shieldUp = qtrue;
				st->shieldNext = level.time + DST_SHIELD_UP_MIN + rand() % ( DST_SHIELD_UP_MAX - DST_SHIELD_UP_MIN );
			}
		}
		G_EN_SetFlag( bs->client, DSEF_SHIELDUP, st->shieldUp );
		if ( st->shieldUp ) {
			ucmd->buttons &= ~( BUTTON_ATTACK | BUTTON_ALTATTACK );
		}
		return;
	case DSAR_KAMIKAZE:
		ucmd->buttons &= ~BUTTON_ALTATTACK;
		if ( !en || AIT_Dist( bs, en ) > 90 ) {
			ucmd->buttons &= ~BUTTON_ATTACK;
		}
		return;
	case DSAR_SUMMONER:
		if ( G_DS_TypesInRitual( bs->client ) ) {
			ucmd->forwardmove = ucmd->rightmove = 0;
			if ( ucmd->upmove > 0 ) {
				ucmd->upmove = 0;
			}
			ucmd->buttons &= ~( BUTTON_ATTACK | BUTTON_ALTATTACK );
		}
		return;
	default:
		return;
	}
}

qboolean BotTypeGoal( bot_state_t *bs, bot_goal_t *goal, int retreat ) {
	int			a = AIT_Arch( bs );
	aiType_t	*st;
	gentity_t	*h, *p;
	vec3_t		pos;

	if ( !a || g_entities[bs->client].health <= 0 ) {
		return qfalse;
	}
	st = AIT_State( bs, a );
	switch ( a ) {
	case DSAR_KAMIKAZE:
	case DSAR_RUSHER:
	case DSAR_JUGGERNAUT:
		if ( retreat && a != DSAR_KAMIKAZE ) {
			return qfalse;
		}
		h = AIT_Human();
		if ( !h ) {
			return qfalse;
		}
		VectorCopy( h->r.currentOrigin, pos );
		return AIT_Goal( pos, goal );
	case DSAR_SNIPER:
		if ( !st->havePerch && level.time - st->perchAt > 2000 ) {
			AIT_PickPerch( bs, st );
		}
		if ( !st->havePerch ) {
			return qfalse;
		}
		// hold the perch; nothing to shoot for 8 s there: find another
		if ( Distance( bs->origin, st->perch ) < 80 ) {
			if ( !st->perchArrive ) {
				st->perchArrive = level.time;
			} else if ( level.time - st->perchArrive > AIT_Idle() && level.time - st->perchAt > AIT_Idle() ) {		// en-tune
				st->perchArrive = 0;
				AIT_PickPerch( bs, st );
			}
		} else {
			st->perchArrive = 0;
		}
		return st->havePerch ? AIT_Goal( st->perch, goal ) : qfalse;
	case DSAR_MEDIC:
		p = AIT_Ent( G_DS_TypesPatient( bs->client ) );
		if ( !p ) {
			p = AIT_Ent( st->ally );
		}
		if ( !p ) {
			return qfalse;
		}
		VectorCopy( p->r.currentOrigin, pos );
		return AIT_Goal( pos, goal );
	case DSAR_SUMMONER:
		if ( st->corpse >= 0 && g_entities[st->corpse].inuse ) {
			VectorCopy( g_entities[st->corpse].r.currentOrigin, pos );
			return AIT_Goal( pos, goal );
		}
		return qfalse;
	default:
		return qfalse;
	}
}
