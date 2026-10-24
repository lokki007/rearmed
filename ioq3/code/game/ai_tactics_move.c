/*
===========================================================================
REARMED round 4 (en-brains) -- ai_tactics_move.c: usercmd-level tactics, every server frame,
after BotRearmInput (ai_rearmed_move.c keeps its dash dodges, double jumps, slides, hooks).

  missile sidestep   see a rocket / plasma bolt / grenade coming: strafe out of its path
                     (or hop the splash) after a skill-scaled reaction, with a miss chance.
                     Works without the dash, and stays out of the dash dodge's way.
  rail juke          an enemy with a railgun has a line on us: break the strafe rhythm
  rocket jump        chase an enemy (or its last spot) on a ledge 100-300 units up
  strafe jump        long straight runs out of combat: hop + air strafe for speed
Skips: staggered (en-feel), crawling, BTT_OWNMOVE (en-types), the stock / rearmed moves busy.
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

#define BTM_MISSILE_RANGE	650
#define BTM_MISSILE_MISS	72
#define BTM_DODGE_TIME		320
#define BTM_RJ_FRAMES		3

typedef struct {
	int		lastMissile;
	int		dodgeAt, dodgeUntil, dodgeJump, dodgeCounted;
	vec3_t	dodgeDir;
	int		jukeNext, jukeUntil, jukeCrouch, jukeJump;
	vec3_t	jukeDir;
	int		rjState, rjNext, rjAirUntil, rjCounted;
	vec3_t	rjTarget;
	int		hopUntil, hopNext, hopSide, hopCheck, hopOk, hopCount;
	vec3_t	hopDir;
} btMove_t;

static btMove_t	btm[MAX_CLIENTS];
static int		btmLevel = -1;

/*
===============================================================================
*/
static float BtmViewYaw( playerState_t *ps, usercmd_t *ucmd ) {
	return SHORT2ANGLE( ( ucmd->angles[YAW] + ps->delta_angles[YAW] ) & 65535 );
}

static float BtmViewPitch( playerState_t *ps, usercmd_t *ucmd ) {
	return AngleNormalize180( SHORT2ANGLE( ( ucmd->angles[PITCH] + ps->delta_angles[PITCH] ) & 65535 ) );
}

static void BtmMoveAlong( playerState_t *ps, usercmd_t *ucmd, const vec3_t dir ) {
	vec3_t	angles, fwd, right;
	float	f, r, m;

	VectorSet( angles, 0, BtmViewYaw( ps, ucmd ), 0 );
	AngleVectors( angles, fwd, right, NULL );
	f = fwd[0] * dir[0] + fwd[1] * dir[1];
	r = right[0] * dir[0] + right[1] * dir[1];
	m = fabs( f ) > fabs( r ) ? fabs( f ) : fabs( r );
	if ( m < 0.001f ) {
		return;
	}
	ucmd->forwardmove = (int)( 127 * f / m );
	ucmd->rightmove = (int)( 127 * r / m );
}

// the flat world direction the usercmd moves in (zero if none)
static void BtmWishDir( playerState_t *ps, usercmd_t *ucmd, vec3_t out ) {
	vec3_t	angles, fwd, right;

	VectorSet( angles, 0, BtmViewYaw( ps, ucmd ), 0 );
	AngleVectors( angles, fwd, right, NULL );
	VectorScale( fwd, ucmd->forwardmove, out );
	VectorMA( out, ucmd->rightmove, right, out );
	out[2] = 0;
	VectorNormalize( out );
}

static void BtmSetView( playerState_t *ps, usercmd_t *ucmd, const vec3_t angles ) {
	int		j;

	for ( j = 0 ; j < 3 ; j++ ) {
		ucmd->angles[j] = ANGLE2SHORT( angles[j] ) - ps->delta_angles[j];
	}
}

static void BtmRotateFlat( const vec3_t in, float deg, vec3_t out ) {
	float	r = DEG2RAD( deg ), c = cos( r ), s = sin( r );
	float	x = in[0], y = in[1];

	out[0] = x * c - y * s;
	out[1] = x * s + y * c;
	out[2] = 0;
}

/*
Floor ahead along dir at dist (no pit, no lava), and nothing in the way at body height
*/
static qboolean BtmClearAhead( gentity_t *ent, const vec3_t dir, float dist ) {
	trace_t	tr;
	vec3_t	start, end, down;
	vec3_t	mins = { -15, -15, 0 }, maxs = { 15, 15, 32 };
	int		contents;

	VectorCopy( ent->r.currentOrigin, start );
	VectorMA( start, dist, dir, end );
	trap_Trace( &tr, start, mins, maxs, end, ent->s.number, MASK_PLAYERSOLID );
	if ( tr.fraction < 1.0f || tr.startsolid ) {
		return qfalse;
	}
	VectorCopy( end, down );
	down[2] -= 96;
	trap_Trace( &tr, end, NULL, NULL, down, ent->s.number, MASK_PLAYERSOLID | CONTENTS_LAVA | CONTENTS_SLIME );
	if ( tr.fraction >= 1.0f || tr.plane.normal[2] < 0.7f ) {
		return qfalse;
	}
	contents = trap_PointContents( tr.endpos, ent->s.number );
	if ( contents & ( CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_NODROP ) ) {
		return qfalse;
	}
	return qtrue;
}

/*
===============================================================================

MISSILE SIDESTEP

===============================================================================
*/
static gentity_t *BtmIncoming( bot_state_t *bs, playerState_t *ps, vec3_t away, float *tHit ) {
	gentity_t	*m;
	vec3_t		rel, vel, off;
	float		speed2, t;
	int			i;

	for ( i = MAX_CLIENTS, m = &g_entities[MAX_CLIENTS] ; i < level.num_entities ; i++, m++ ) {
		if ( !m->inuse || m->s.eType != ET_MISSILE || m->r.ownerNum == bs->client || m->s.weapon == WP_GRAPPLING_HOOK ) {
			continue;
		}
		if ( m->r.ownerNum < MAX_CLIENTS && BotSameTeam( bs, m->r.ownerNum ) ) {
			continue;
		}
		VectorSubtract( ps->origin, m->r.currentOrigin, rel );
		if ( VectorLengthSquared( rel ) > BTM_MISSILE_RANGE * BTM_MISSILE_RANGE ) {
			continue;
		}
		BG_EvaluateTrajectoryDelta( &m->s.pos, level.time, vel );
		speed2 = VectorLengthSquared( vel );
		if ( speed2 < 60 * 60 ) {
			continue;
		}
		t = DotProduct( rel, vel ) / speed2;
		if ( t <= 0 || t > 0.9f ) {
			continue;
		}
		VectorMA( rel, -t, vel, off );
		if ( VectorLengthSquared( off ) >= BTM_MISSILE_MISS * BTM_MISSILE_MISS
			&& !( m->s.weapon == WP_GRENADE_LAUNCHER && VectorLength( rel ) < 220 ) ) {
			continue;
		}
		away[0] = off[0];
		away[1] = off[1];
		away[2] = 0;
		if ( VectorNormalize( away ) < 6 ) {
			away[0] = -vel[1];
			away[1] = vel[0];
			away[2] = 0;
			VectorNormalize( away );
			if ( random() < 0.5f ) {
				VectorNegate( away, away );
			}
		}
		*tHit = t;
		return m;
	}
	return NULL;
}

static void BtmDodge( bot_state_t *bs, playerState_t *ps, usercmd_t *ucmd, float scale, qboolean onGround ) {
	btMove_t	*m = &btm[bs->client];
	gentity_t	*missile;
	vec3_t		away;
	float		tHit, reaction;
	int			busy;

	// the rearmed dash dodge has it: stay out of its way
	busy = bs->rmDodgeAt || level.time < bs->rmDashHoldUntil;
	if ( !m->dodgeUntil && !busy ) {
		missile = BtmIncoming( bs, ps, away, &tHit );
		if ( missile && missile->s.number != m->lastMissile ) {
			m->lastMissile = missile->s.number;
			// not perfect: skill-scaled reaction and a miss chance
			if ( random() < 0.25f + 0.6f * scale ) {
				reaction = 0.42f - 0.28f * scale + crandom() * 0.05f;
				if ( reaction < tHit - 0.05f ) {
					m->dodgeAt = level.time + (int)( reaction * 1000 );
					m->dodgeUntil = m->dodgeAt + BTM_DODGE_TIME;
					m->dodgeCounted = 0;
					VectorCopy( away, m->dodgeDir );
					m->dodgeJump = onGround && ( missile->s.weapon == WP_ROCKET_LAUNCHER || missile->s.weapon == WP_GRENADE_LAUNCHER )
						&& missile->r.currentOrigin[2] < ps->origin[2] + 16 && random() < 0.5f * scale
						&& !BotTacCrawling( bs );
				}
			}
		}
	}
	if ( m->dodgeUntil ) {
		if ( level.time > m->dodgeUntil || busy ) {
			m->dodgeUntil = 0;
			return;
		}
		if ( level.time >= m->dodgeAt ) {
			if ( level.time - m->dodgeAt < 60 ) {
				if ( !BtmClearAhead( &g_entities[bs->client], m->dodgeDir, 96 ) ) {
					VectorNegate( m->dodgeDir, m->dodgeDir );		// the other way, or stay
					if ( !BtmClearAhead( &g_entities[bs->client], m->dodgeDir, 96 ) ) {
						m->dodgeUntil = 0;
						return;
					}
				}
				if ( !m->dodgeCounted ) {
					m->dodgeCounted = 1;
					BotTacCount( bs, BTC_DODGE );
				}
			}
			BtmMoveAlong( ps, ucmd, m->dodgeDir );
			if ( m->dodgeJump && onGround && level.time - m->dodgeAt < 150 ) {
				ucmd->upmove = 127;
			}
		}
	}
}

/*
===============================================================================

RAIL JUKE

===============================================================================
*/
static void BtmJuke( bot_state_t *bs, playerState_t *ps, usercmd_t *ucmd, float scale, qboolean onGround ) {
	btMove_t	*m = &btm[bs->client];
	gentity_t	*e;
	vec3_t		eye, dir, fwd, to;
	trace_t		tr;
	float		d;

	if ( level.time < m->jukeUntil ) {
		BtmMoveAlong( ps, ucmd, m->jukeDir );
		if ( m->jukeCrouch ) {
			ucmd->upmove = -127;
		} else if ( m->jukeJump && onGround ) {
			ucmd->upmove = 127;
		}
		return;
	}
	if ( level.time < m->jukeNext || bs->enemy < 0 || bs->enemy >= MAX_CLIENTS ) {
		return;
	}
	m->jukeNext = level.time + 150;
	e = &g_entities[bs->enemy];
	if ( !e->inuse || !e->client || e->health <= 0 || e->client->ps.weapon != WP_RAILGUN ) {
		return;
	}
	if ( e->client->ps.weaponTime > 450 ) {
		return;		// still reloading: no threat yet
	}
	VectorCopy( e->client->ps.origin, eye );
	eye[2] += e->client->ps.viewheight;
	VectorSubtract( ps->origin, eye, dir );
	d = VectorNormalize( dir );
	if ( d < 250 ) {
		return;
	}
	AngleVectors( e->client->ps.viewangles, fwd, NULL, NULL );
	if ( DotProduct( fwd, dir ) < 0.993f ) {
		return;		// not lined up on us (about 7 degrees)
	}
	trap_Trace( &tr, eye, NULL, NULL, ps->origin, e->s.number, MASK_SHOT );
	if ( tr.fraction < 1.0f && tr.entityNum != bs->client ) {
		return;
	}
	m->jukeNext = level.time + 600 + (int)( random() * 600 ) - (int)( 250 * scale );
	if ( random() >= 0.15f + 0.55f * scale ) {
		return;		// didn't notice
	}
	// reverse the strafe we're doing (or pick a side)
	BtmWishDir( ps, ucmd, to );
	fwd[0] = -dir[1];
	fwd[1] = dir[0];
	fwd[2] = 0;
	if ( DotProduct( to, fwd ) > 0 ) {
		VectorNegate( fwd, fwd );
	}
	VectorCopy( fwd, m->jukeDir );
	if ( !BtmClearAhead( &g_entities[bs->client], m->jukeDir, 80 ) ) {
		VectorNegate( m->jukeDir, m->jukeDir );
		if ( !BtmClearAhead( &g_entities[bs->client], m->jukeDir, 80 ) ) {
			return;
		}
	}
	m->jukeUntil = level.time + 200 + (int)( random() * 150 );
	m->jukeCrouch = random() < 0.2f;
	m->jukeJump = !m->jukeCrouch && onGround && random() < 0.25f && !BotTacCrawling( bs );
	BotTacCount( bs, BTC_JUKE );
}

/*
===============================================================================

ROCKET JUMP (chase) and counting every rocket jump

===============================================================================
*/
/*
Count every rocket jump (botlib's TRAVEL_ROCKETJUMP ones and ours): a rocket at our feet on a jump.
Runs on the final usercmd.
*/
static void BtmCountRJ( bot_state_t *bs, playerState_t *ps, usercmd_t *ucmd, qboolean onGround ) {
	btMove_t	*m = &btm[bs->client];

	if ( ps->weapon == WP_ROCKET_LAUNCHER && ( ucmd->buttons & BUTTON_ATTACK ) && BtmViewPitch( ps, ucmd ) > 50
		&& ps->weaponTime <= 0 && ( ucmd->upmove > 0 || !onGround ) && level.time > m->rjCounted ) {
		m->rjCounted = level.time + 1500;
		BotTacCount( bs, BTC_RJUMP );
		if ( BotTacDebug() ) {
			G_LogPrintf( "BtRJ: %i rocket jumps (node %s)\n", bs->client, bs->ainode == AINode_Seek_LTG ? "ltg" : "other" );
		}
	}
}

static void BtmRocketJump( bot_state_t *bs, playerState_t *ps, usercmd_t *ucmd, float scale, qboolean onGround ) {
	btMove_t	*m = &btm[bs->client];
	gentity_t	*ent = &g_entities[bs->client];
	vec3_t		target, dir, angles, up;
	trace_t		tr;
	float		dz, flat;
	qboolean	visible;

	if ( m->rjState > 0 ) {
		// the jump frames: look down, jump, fire, run at it
		VectorSubtract( m->rjTarget, ps->origin, dir );
		vectoangles( dir, angles );
		angles[PITCH] = 82;
		angles[ROLL] = 0;
		BtmSetView( ps, ucmd, angles );
		ucmd->buttons |= BUTTON_ATTACK;
		ucmd->buttons &= ~BUTTON_ALTATTACK;
		ucmd->upmove = 127;
		ucmd->forwardmove = 127;
		ucmd->rightmove = 0;
		m->rjState++;
		if ( m->rjState > BTM_RJ_FRAMES || ( !onGround && m->rjState > 2 ) ) {
			m->rjState = 0;
			m->rjAirUntil = level.time + 900;
		}
		return;
	}
	if ( level.time < m->rjAirUntil ) {
		// air control toward the ledge
		if ( onGround && level.time > m->rjAirUntil - 700 ) {
			m->rjAirUntil = 0;
			return;
		}
		VectorSubtract( m->rjTarget, ps->origin, dir );
		dir[2] = 0;
		if ( VectorNormalize( dir ) > 16 ) {
			BtmMoveAlong( ps, ucmd, dir );
		}
		ucmd->upmove = 0;
		return;
	}
	if ( level.time < m->rjNext || !onGround || ps->weapon != WP_ROCKET_LAUNCHER || ps->weaponTime > 0
		|| ps->ammo[WP_ROCKET_LAUNCHER] < 3 || ps->powerups[PW_QUAD] || ent->waterlevel > 1 ) {
		return;
	}
	if ( bs->enemy < 0 || bs->enemy >= MAX_CLIENTS
		|| ( bs->ainode != AINode_Battle_Fight && bs->ainode != AINode_Battle_Chase ) ) {
		return;
	}
	m->rjNext = level.time + 700;
	if ( ps->stats[STAT_HEALTH] < 80 && ps->stats[STAT_HEALTH] + ps->stats[STAT_ARMOR] < 130 ) {
		return;
	}
	visible = bs->ainode == AINode_Battle_Fight;
	if ( visible ) {
		VectorCopy( g_entities[bs->enemy].r.currentOrigin, target );
	} else {
		VectorCopy( bs->lastenemyorigin, target );
	}
	dz = target[2] - ps->origin[2];
	flat = sqrt( ( target[0] - ps->origin[0] ) * ( target[0] - ps->origin[0] )
		+ ( target[1] - ps->origin[1] ) * ( target[1] - ps->origin[1] ) );
	if ( dz < 100 || dz > 300 || flat < 120 || flat > 520 ) {
		return;
	}
	m->rjNext = level.time + 3500;
	if ( random() >= 0.15f + 0.6f * scale ) {
		return;
	}
	// headroom
	VectorCopy( ps->origin, up );
	up[2] += dz + 64;
	trap_Trace( &tr, ps->origin, ent->r.mins, ent->r.maxs, up, bs->client, MASK_PLAYERSOLID );
	if ( tr.fraction < 0.85f ) {
		return;
	}
	VectorCopy( target, m->rjTarget );
	m->rjState = 1;
	BotTacCount( bs, BTC_RJUMP_CHASE );
	if ( BotTacDebug() ) {
		G_LogPrintf( "BtRJump: %i chases %i up %.0f over %.0f\n", bs->client, bs->enemy, dz, flat );
	}
}

/*
===============================================================================

STRAFE JUMP (out of combat, long straights)

===============================================================================
*/
static void BtmStrafeJump( bot_state_t *bs, playerState_t *ps, usercmd_t *ucmd, float scale, qboolean onGround ) {
	btMove_t	*m = &btm[bs->client];
	gentity_t	*ent = &g_entities[bs->client];
	vec3_t		wish, vdir, steer;
	float		speed, theta, cross;

	speed = sqrt( ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1] );
	if ( bs->enemy >= 0 || ( bs->ainode != AINode_Seek_LTG && bs->ainode != AINode_Seek_NBG ) || ent->waterlevel > 1
		|| bs->rmJumpState || bs->rmSlideState || bs->rmHookState || bs->rmZipState || ucmd->forwardmove <= 0
		|| ucmd->upmove < 0 || ( ps->pm_flags & PMF_DUCKED ) ) {
		m->hopUntil = 0;
		return;
	}
	BtmWishDir( ps, ucmd, wish );
	if ( !m->hopUntil ) {
		if ( !onGround || speed < 220 || level.time < m->hopNext ) {
			return;
		}
		m->hopNext = level.time + 2500;
		if ( random() >= 0.1f + 0.8f * scale || !BtmClearAhead( ent, wish, 200 ) || !BtmClearAhead( ent, wish, 420 ) ) {
			return;
		}
		m->hopUntil = level.time + 4000;
		m->hopCheck = 0;
		m->hopCount = 0;
		m->hopSide = random() < 0.5f ? 1 : -1;
		VectorCopy( wish, m->hopDir );
	}
	if ( level.time > m->hopUntil ) {
		m->hopUntil = 0;
		return;
	}
	// the route turned: stop
	if ( DotProduct( wish, m->hopDir ) < 0.7f ) {
		m->hopUntil = 0;
		return;
	}
	VectorCopy( wish, m->hopDir );
	if ( onGround ) {
		// each landing: is the next stretch still clear? then jump again
		if ( level.time >= m->hopCheck ) {
			m->hopCheck = level.time + 250;
			m->hopOk = BtmClearAhead( ent, wish, 220 ) && BtmClearAhead( ent, wish, 400 );
		}
		if ( !m->hopOk ) {
			m->hopUntil = 0;
			return;
		}
		if ( !( ps->pm_flags & PMF_JUMP_HELD ) ) {
			ucmd->upmove = 127;
			m->hopCount++;
			m->hopSide = -m->hopSide;
			BotTacCount( bs, BTC_STRAFEHOP );
		} else {
			ucmd->upmove = 0;		// release so the next landing can jump
		}
		return;
	}
	// in the air: wish at the angle that still gains speed, turning toward the route
	ucmd->upmove = 0;
	VectorSet( vdir, ps->velocity[0], ps->velocity[1], 0 );
	if ( VectorNormalize( vdir ) < 1 ) {
		return;
	}
	cross = 300.0f / speed;		// acos via atan2 (the QVM has no acos)
	theta = speed > 330 ? RAD2DEG( atan2( sqrt( 1.0f - cross * cross ), cross ) ) : 25;
	if ( theta < 20 ) {
		theta = 20;
	} else if ( theta > 60 ) {
		theta = 60;
	}
	cross = vdir[0] * wish[1] - vdir[1] * wish[0];
	if ( fabs( cross ) > 0.09f ) {
		m->hopSide = cross > 0 ? 1 : -1;		// steer back onto the route
	}
	BtmRotateFlat( vdir, m->hopSide * theta, steer );
	BtmMoveAlong( ps, ucmd, steer );
}

/*
===============================================================================
*/
void BotTacMoveInput( bot_state_t *bs, usercmd_t *ucmd ) {
	gentity_t		*ent = &g_entities[bs->client];
	playerState_t	*ps;
	btMove_t		*m;
	float			scale;
	qboolean		onGround;

	if ( btmLevel != level.startTime ) {
		btmLevel = level.startTime;
		memset( btm, 0, sizeof( btm ) );
	}
	m = &btm[bs->client];
	if ( !ent->client ) {
		return;
	}
	ps = &ent->client->ps;
	if ( ps->pm_type != PM_NORMAL || ps->stats[STAT_HEALTH] <= 0 ) {
		memset( m, 0, sizeof( *m ) );
		return;
	}
	if ( BotTacOwnMove( bs ) || BotTacStaggered( bs ) || BotTacCrawling( bs ) ) {
		m->rjState = 0;
		m->hopUntil = 0;
		m->dodgeUntil = 0;
		return;
	}
	if ( ps->loopSound & RMZ_ZIPPING ) {
		return;
	}
	onGround = ps->groundEntityNum != ENTITYNUM_NONE;

	if ( BotTacOn( bs, BTAC_RJUMP, &scale ) ) {
		BtmRocketJump( bs, ps, ucmd, scale, onGround );
		if ( m->rjState || level.time < m->rjAirUntil ) {
			BtmCountRJ( bs, ps, ucmd, onGround );
			return;
		}
		BtmStrafeJump( bs, ps, ucmd, scale, onGround );
	}
	if ( BotTacOn( bs, BTAC_DODGE, &scale ) ) {
		BtmDodge( bs, ps, ucmd, scale, onGround );
		if ( !m->dodgeUntil || level.time < m->dodgeAt ) {
			BtmJuke( bs, ps, ucmd, scale, onGround );
		}
	}
	BtmCountRJ( bs, ps, ucmd, onGround );
}
