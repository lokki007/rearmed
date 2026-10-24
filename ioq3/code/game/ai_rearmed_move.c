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
//
// ai_rearmed_move.c -- the bots' rearmed movement: reactive dodge dashes, double jumps,
// offhand grapple (offense, traversal, escape), wall runs and the jetpack.
// Runs once per server frame on the usercmd the stock bot AI produced (see BotUpdateInput).
//
// Reads the live playerState (the game module owns it) instead of bs->cur_ps, which is only
// refreshed at bot_thinktime and would make apex / wall run timing a frame late.

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
#include "chars.h"

#define RM_PRESS_HOLD			120		// msec a tap (dash, air jump) is held so the server frame catches it
#define RM_TRACE_BUDGET			16		// traces per bot per think
#define RM_SAFE_DROP			300		// never dash / hook / jump where the floor drops further than this
#define RM_AIM_DOT				0.978f	// cos 12 degrees: the enemy is aiming at us
#define RM_MISSILE_RANGE		500
#define RM_MISSILE_MISS			60		// predicted closest approach that counts as a hit
#define RM_DASH_REACH			260		// about how far a dash carries
#define RM_JUMP_REACH			320		// about how far a jump + double jump carries at run speed
#define RM_APEX_VZ				80		// press the air jump once rising slower than this
#define RM_RUN_SPEED			250		// "moving at speed"
#define RM_HOOK_FAR				550		// offense hook beyond this
#define RM_HOOK_CLOSE			200		// ... and let go inside this
#define RM_HOOK_OFFENSE_TIME	1500
#define RM_HOOK_RANGE			900		// traversal / escape hook point search distance
#define RM_HOOK_MIN_DIST		250
#define RM_HOOK_MISS_TIME		450		// let go if the hook hasn't caught by then
#define RM_HOOK_SPEED			2000	// OFFHAND_HOOK_SPEED in g_weapon.c
#define RM_ESCAPE_HEALTH		40
#define RM_ESCAPE_DIST			350
#define RM_FLAG_HOME_DIST		1200	// no hooking this close to our base with the flag

// dodge triggers
#define RM_DODGE_NONE			0
#define RM_DODGE_AIM			1
#define RM_DODGE_HIT			2
#define RM_DODGE_MISSILE		3

// rmHookState
#define RM_HOOK_IDLE			0
#define RM_HOOK_AIM				1		// this frame turns the view, the next one fires
#define RM_HOOK_HOLD			2

// rmHookUse
#define RM_HOOKUSE_OFFENSE		1
#define RM_HOOKUSE_TRAVERSE		2
#define RM_HOOKUSE_ESCAPE		3
#define RM_HOOKUSE_YANK			4		// magnet hook perk: pull the enemy in
#define RM_HOOKUSE_RECOVER		5		// falling into a pit: hook the floor we left

// rmJumpState
#define RM_JUMP_IDLE			0
#define RM_JUMP_PRESS			1		// ground jump at rmJumpAt
#define RM_JUMP_APEX			2		// jump released, air jump near the apex
#define RM_JUMP_DONE			3		// wait for landing

static int rmTraces;				// traces left this frame for the bot being processed

/*
==================
RmTrace

trap_Trace within the per frame budget
==================
*/
static qboolean RmTrace( trace_t *tr, vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, int passent, int mask ) {
	if ( rmTraces <= 0 ) {
		return qfalse;
	}
	rmTraces--;
	trap_Trace( tr, start, mins, maxs, end, passent, mask );
	return qtrue;
}

/*
==================
RmSafeGround

Is there floor below point, no lower than floorZ, that isn't sky, lava, slime or a hurt trigger?
==================
*/
static qboolean RmSafeGround( int passent, vec3_t point, float floorZ ) {
	trace_t		tr;
	vec3_t		end;
	gentity_t	*hit;

	VectorCopy( point, end );
	end[2] = floorZ;
	if ( end[2] > point[2] - 64 ) {
		end[2] = point[2] - 64;
	}
	if ( !RmTrace( &tr, point, NULL, NULL, end, passent,
		MASK_PLAYERSOLID | CONTENTS_TRIGGER | CONTENTS_LAVA | CONTENTS_SLIME ) ) {
		return qfalse;
	}
	if ( tr.allsolid || tr.fraction >= 1.0f || ( tr.surfaceFlags & SURF_SKY )
		|| ( tr.contents & ( CONTENTS_LAVA | CONTENTS_SLIME ) ) ) {
		return qfalse;
	}
	if ( tr.entityNum >= 0 && tr.entityNum < ENTITYNUM_MAX_NORMAL ) {
		hit = &g_entities[tr.entityNum];
		if ( ( hit->r.contents & CONTENTS_TRIGGER ) && hit->classname && !Q_stricmp( hit->classname, "trigger_hurt" ) ) {
			return qfalse;
		}
	}
	return qtrue;
}

/*
==================
RmSafeAhead

Moving dist units along dir (flattened) from where we are, is there floor to land on?
Two traces.
==================
*/
static qboolean RmSafeAhead( playerState_t *ps, const vec3_t dir, float dist ) {
	trace_t	tr;
	vec3_t	flat, end;

	if ( rmTraces < 2 ) {
		return qfalse;
	}
	flat[0] = dir[0];
	flat[1] = dir[1];
	flat[2] = 0;
	if ( VectorNormalize( flat ) < 0.001f ) {
		return RmSafeGround( ps->clientNum, ps->origin, ps->origin[2] - RM_SAFE_DROP );
	}
	VectorMA( ps->origin, dist, flat, end );
	RmTrace( &tr, ps->origin, NULL, NULL, end, ps->clientNum, MASK_PLAYERSOLID );
	// stop short of any wall we hit
	VectorMA( tr.endpos, -16, flat, end );
	return RmSafeGround( ps->clientNum, end, ps->origin[2] - RM_SAFE_DROP );
}

/*
==================
RmSafePath

A dash or jump carries further than its nominal reach (momentum, air control, a dash in the
air), and narrow arenas like q3dm17 punish that: floor must be there halfway, at the reach
and half again past it. Six traces, then the arc itself is flown (RmMoveLands).
==================
*/
static qboolean RmMoveLands( playerState_t *ps, const vec3_t dir, float reach );

static qboolean RmSafePath( playerState_t *ps, const vec3_t dir, float reach ) {
	float	r = reach;

	if ( rmTraces < 6 ) {
		return qfalse;
	}
	if ( ps->groundEntityNum == ENTITYNUM_NONE ) {
		r *= 1.4f;		// nothing slows an air dash down
	}
	return RmSafeAhead( ps, dir, r * 0.5f ) && RmSafeAhead( ps, dir, r )
		&& RmSafeAhead( ps, dir, r * 1.5f ) && RmMoveLands( ps, dir, reach );
}

/*
==================
RmGroundBelow

Is there floor (not a pit or a hurt trigger's void) within dist below us? (jetpack)
==================
*/
static qboolean RmGroundBelow( playerState_t *ps, float dist ) {
	trace_t	tr;
	vec3_t	end;

	VectorCopy( ps->origin, end );
	end[2] -= dist;
	if ( !RmTrace( &tr, ps->origin, NULL, NULL, end, ps->clientNum, MASK_PLAYERSOLID ) ) {
		return qfalse;
	}
	return tr.fraction < 1.0f && !( tr.surfaceFlags & SURF_SKY );
}

/*
===============================================================================

Fall tracking: what each bot did before it fell to its death (a "Fall:" line in games.log
for every pit / lava / slime / crusher death), and its last footing, which the air
recovery steers back to.

===============================================================================
*/
enum {
	RM_ACT_STOCKJUMP,		// the stock AI jumped off the ground
	RM_ACT_PAD,				// jump pad
	RM_ACT_DASH,
	RM_ACT_JUMP,			// our ground jump
	RM_ACT_AIRJUMP,
	RM_ACT_SLIDE,
	RM_ACT_SLIDEJUMP,
	RM_ACT_SLAM,
	RM_ACT_WALLRUN,
	RM_ACT_WALLKICK,
	RM_ACT_ZIP,
	RM_ACT_HOOK,
	RM_ACT_DANGER,
	RM_ACT_KITE,
	RM_ACT_EDGE,			// backing off a ledge in a fight
	RM_ACT_KNOCK,			// hit hard by someone else
	RM_ACT_SELFKNOCK,		// our own splash
	RM_ACT_RECOVER,			// air recovery: steering back
	RM_ACT_RECOVERMOVE,		// air recovery: dash / air jump / hook back
	RM_ACT_NUM
};

static const char *rmActNames[RM_ACT_NUM] = {
	"stockjump", "pad", "dash", "jump", "airjump", "slide", "slidejump", "slam", "wallrun", "wallkick",
	"zip", "hook", "danger", "kite", "edge", "knock", "selfknock", "recover", "recovermove"
};

typedef struct {
	qboolean	alive;
	int			act[RM_ACT_NUM];		// level.time of the last of each
	int			knockAmt, knockBy;
	int			groundTime;				// last time on the ground
	int			takeoffSpeed;			// horizontal speed when we left it
	vec3_t		safePos;				// the last floor we stood on (world or a mover)
	int			safeTime;
	qboolean	padAir;					// this air time started on a jump pad (leave the arc alone)
	vec3_t		lastVel;
	int			lastDmgEvent, lastHealth;
	int			lastPmFlags, lastDashTime, lastWallJumpTime;
	int			recoverNext;			// next landing prediction
	int			recoverUntil;			// steering back until
	qboolean	landLogged;
	qboolean	recoverHooked;			// one recovery hook per air time (no hanging under a lip for ever)
	int			recoverJumpAt;			// air jump back: pressed from this time (0 = none)
	vec3_t		recoverDir;
	const char	*node;					// the AI node while alive
	int			edgeNext;				// next ledge check in a fight
	vec3_t		edgeAway;				// away from the nearest ledge, zero = none near
} rmTrack_t;

static rmTrack_t	rmTrack[MAX_CLIENTS];
static const char	*rmFallNames[5] = { "void", "falling", "lava", "slime", "crush" };

static void RmAct( int client, int act ) {
	if ( client >= 0 && client < MAX_CLIENTS ) {
		rmTrack[client].act[act] = level.time;
	}
}

static qboolean RmFallMod( int mod ) {
	return mod == MOD_TRIGGER_HURT || mod == MOD_FALLING || mod == MOD_LAVA || mod == MOD_SLIME || mod == MOD_CRUSH;
}

static const char *RmNodeName( bot_state_t *bs ) {
	if ( bs->ainode == AINode_Battle_Fight ) {
		return "fight";
	} else if ( bs->ainode == AINode_Battle_Chase ) {
		return "chase";
	} else if ( bs->ainode == AINode_Battle_Retreat ) {
		return "retreat";
	} else if ( bs->ainode == AINode_Battle_NBG ) {
		return "battle_nbg";
	} else if ( bs->ainode == AINode_Seek_LTG ) {
		return "seek_ltg";
	} else if ( bs->ainode == AINode_Seek_NBG ) {
		return "seek_nbg";
	}
	return "other";
}

/*
==================
BotRearmFallTrack

Every server frame for every bot, before the rearmed moves (and also with bot_rearm 0,
so stock bots can be measured the same way). ucmd is still the stock AI's command.
==================
*/
void BotRearmFallTrack( bot_state_t *bs, usercmd_t *ucmd ) {
	gentity_t		*ent;
	gclient_t		*cl;
	playerState_t	*ps;
	rmTrack_t		*t;
	char			acts[400];
	vec3_t			d;
	float			dv;
	int				i, mod, by;
	const char		*what;

	if ( bs->client < 0 || bs->client >= MAX_CLIENTS ) {
		return;
	}
	ent = &g_entities[bs->client];
	cl = ent->client;
	if ( !cl ) {
		return;
	}
	ps = &cl->ps;
	t = &rmTrack[bs->client];

	if ( ps->pm_type != PM_NORMAL || ps->stats[STAT_HEALTH] <= 0 ) {
		if ( t->alive && ps->stats[STAT_HEALTH] <= 0 ) {
			mod = cl->lasthurt_mod;
			if ( RmFallMod( mod ) ) {
				// the actions of the last 4 seconds, newest last
				acts[0] = 0;
				for ( i = 0 ; i < RM_ACT_NUM ; i++ ) {
					if ( t->act[i] && level.time - t->act[i] <= 4000 ) {
						Q_strcat( acts, sizeof( acts ), va( " %s@%i", rmActNames[i], level.time - t->act[i] ) );
					}
				}
				if ( t->act[RM_ACT_KNOCK] && level.time - t->act[RM_ACT_KNOCK] <= 3000 ) {
					what = "knocked";
				} else {
					what = "self";
				}
				switch ( mod ) {
				case MOD_TRIGGER_HURT:	by = 0; break;
				case MOD_FALLING:		by = 1; break;
				case MOD_LAVA:			by = 2; break;
				case MOD_SLIME:			by = 3; break;
				default:				by = 4; break;
				}
				G_LogPrintf( "Fall: %i %s %s air %i takeoff %i from %.0f %.0f %.0f at %.0f %.0f %.0f node %s knock %i by %i rearm %i acts:%s\n",
					bs->client, rmFallNames[by], what,
					level.time - t->groundTime, t->takeoffSpeed, t->safePos[0], t->safePos[1], t->safePos[2],
					ps->origin[0], ps->origin[1], ps->origin[2], t->node ? t->node : "?", t->knockAmt, t->knockBy,
					trap_Cvar_VariableIntegerValue( "bot_rearm" ), acts );
			}
		}
		t->alive = qfalse;
		return;
	}

	if ( !t->alive ) {
		memset( t, 0, sizeof( *t ) );
		t->alive = qtrue;
		VectorCopy( ps->origin, t->safePos );
		t->safeTime = level.time;
		t->groundTime = level.time;
		t->lastDmgEvent = ps->damageEvent;
		t->lastHealth = ps->stats[STAT_HEALTH];
		VectorCopy( ps->velocity, t->lastVel );
		t->lastPmFlags = ps->pm_flags;
		t->lastDashTime = ps->stats[STAT_DASH_TIME];
		t->lastWallJumpTime = ps->stats[STAT_WALLJUMP_TIME];
	}

	// a hit that threw us (our own movement changes the velocity far less in one frame)
	if ( ps->damageEvent != t->lastDmgEvent || ps->stats[STAT_HEALTH] < t->lastHealth ) {
		VectorSubtract( ps->velocity, t->lastVel, d );
		dv = VectorLength( d );
		if ( dv > 220 ) {
			if ( cl->lasthurt_client == bs->client || cl->lasthurt_client >= MAX_CLIENTS ) {
				t->act[RM_ACT_SELFKNOCK] = level.time;
			} else {
				t->act[RM_ACT_KNOCK] = level.time;
				t->padAir = qfalse;		// knocked off the pad's arc: fair game for the recovery
				t->knockAmt = (int)dv;
				t->knockBy = cl->lasthurt_client;
			}
		}
	}
	t->lastDmgEvent = ps->damageEvent;
	t->lastHealth = ps->stats[STAT_HEALTH];
	VectorCopy( ps->velocity, t->lastVel );

	t->node = RmNodeName( bs );
	// what the player state did, whoever asked for it (stock crouches slide, stock jumps air jump)
	if ( RM_FLAGS2( ps ) & RMS2_SLIDING ) {
		t->act[RM_ACT_SLIDE] = level.time;
	}
	if ( RM_FLAGS2( ps ) & RMS2_SLAMMING ) {
		t->act[RM_ACT_SLAM] = level.time;
	}
	if ( ps->stats[STAT_RMFLAGS] & RMS_WALLRUN ) {
		t->act[RM_ACT_WALLRUN] = level.time;
	}
	if ( ( ps->pm_flags & PMF_DOUBLE_JUMPED ) && !( t->lastPmFlags & PMF_DOUBLE_JUMPED ) ) {
		t->act[RM_ACT_AIRJUMP] = level.time;
	}
	if ( ps->stats[STAT_DASH_TIME] > t->lastDashTime + DASH_COOLDOWN / 2 ) {
		t->act[RM_ACT_DASH] = level.time;
	}
	if ( ps->stats[STAT_WALLJUMP_TIME] > t->lastWallJumpTime ) {
		t->act[RM_ACT_WALLKICK] = level.time;
	}
	if ( ps->loopSound & RMZ_ZIPPING ) {
		t->act[RM_ACT_ZIP] = level.time;
	}
	if ( ps->pm_flags & PMF_GRAPPLE_PULL ) {
		t->act[RM_ACT_HOOK] = level.time;
	}
	t->lastPmFlags = ps->pm_flags;
	t->lastDashTime = ps->stats[STAT_DASH_TIME];
	t->lastWallJumpTime = ps->stats[STAT_WALLJUMP_TIME];

	if ( ps->jumppad_ent ) {
		t->act[RM_ACT_PAD] = level.time;
		t->padAir = qtrue;
	}
	if ( ps->groundEntityNum != ENTITYNUM_NONE ) {
		if ( ucmd->upmove > 0 ) {
			t->act[RM_ACT_STOCKJUMP] = level.time;
		}
		if ( !t->landLogged && t->act[RM_ACT_RECOVER] && t->act[RM_ACT_RECOVER] >= t->groundTime
			&& ( BotTrickDebug() || trap_Cvar_VariableIntegerValue( "g_debugAlt" ) ) ) {
			G_LogPrintf( "Move: %i landed after a pit recovery\n", bs->client );
		}
		t->groundTime = level.time;
		t->takeoffSpeed = (int)sqrt( ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1] );
		if ( !ps->jumppad_ent ) {
			t->padAir = qfalse;
		}
		if ( ps->groundEntityNum == ENTITYNUM_WORLD || ps->groundEntityNum >= MAX_CLIENTS ) {
			VectorCopy( ps->origin, t->safePos );
			t->safeTime = level.time;
		}
		t->landLogged = qtrue;
		t->recoverHooked = qfalse;
		t->recoverUntil = 0;
	} else {
		t->landLogged = qfalse;
	}
}

/*
==================
RmViewYaw

The yaw the server will use for this usercmd
==================
*/
static float RmViewYaw( playerState_t *ps, usercmd_t *ucmd ) {
	return SHORT2ANGLE( ( ucmd->angles[YAW] + ps->delta_angles[YAW] ) & 65535 );
}

/*
==================
RmMoveAlong

Set forward/rightmove so the movement points along the world direction dir
==================
*/
static void RmMoveAlong( playerState_t *ps, usercmd_t *ucmd, const vec3_t dir ) {
	vec3_t	angles, fwd, right;
	float	f, r, m;

	VectorSet( angles, 0, RmViewYaw( ps, ucmd ), 0 );
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

/*
==================
RmSetViewAngles

Override the usercmd view, the same way BotInputToUserCommand builds it
==================
*/
static void RmSetViewAngles( playerState_t *ps, usercmd_t *ucmd, vec3_t angles ) {
	short	temp;
	int		j;

	for ( j = 0 ; j < 3 ; j++ ) {
		temp = ANGLE2SHORT( angles[j] ) - ps->delta_angles[j];
		ucmd->angles[j] = temp;
	}
}

/*
==================
RmEnemyAiming

Is the enemy's crosshair within about 12 degrees of us?
==================
*/
// weapons that want to be close: worth grappling in with
static qboolean RmCloseRangeWeapon( int weapon ) {
	switch ( weapon ) {
	case WP_GAUNTLET:
	case WP_SHOTGUN:
	case WP_LIGHTNING:
	case WP_PLASMAGUN:
	case WP_GRENADE_LAUNCHER:
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case WP_NAILGUN:
	case WP_CHAINGUN:
#endif
		return qtrue;
	default:
		return qfalse;
	}
}

static qboolean RmEnemyAiming( playerState_t *ps, gentity_t *enemy ) {
	vec3_t	eye, dir, fwd;

	VectorCopy( enemy->client->ps.origin, eye );
	eye[2] += enemy->client->ps.viewheight;
	VectorSubtract( ps->origin, eye, dir );
	if ( VectorNormalize( dir ) < 1 ) {
		return qfalse;
	}
	AngleVectors( enemy->client->ps.viewangles, fwd, NULL, NULL );
	return DotProduct( fwd, dir ) > RM_AIM_DOT;
}

/*
==================
RmIncomingMissile

An enemy projectile that will pass within RM_MISSILE_MISS of us.
away is set to the horizontal direction out of its path.
==================
*/
static qboolean RmIncomingMissile( bot_state_t *bs, playerState_t *ps, vec3_t away ) {
	gentity_t	*m;
	vec3_t		rel, vel, off;
	float		speed2, t;
	int			i;

	for ( i = MAX_CLIENTS, m = &g_entities[MAX_CLIENTS] ; i < level.num_entities ; i++, m++ ) {
		if ( !m->inuse || m->s.eType != ET_MISSILE || m->r.ownerNum == bs->client
			|| m->s.weapon == WP_GRAPPLING_HOOK ) {
			continue;
		}
		if ( m->r.ownerNum < MAX_CLIENTS && BotSameTeam( bs, m->r.ownerNum ) ) {
			continue;
		}
		VectorSubtract( ps->origin, m->r.currentOrigin, rel );
		if ( VectorLengthSquared( rel ) > RM_MISSILE_RANGE * RM_MISSILE_RANGE ) {
			continue;
		}
		BG_EvaluateTrajectoryDelta( &m->s.pos, level.time, vel );
		speed2 = VectorLengthSquared( vel );
		if ( speed2 < 50 * 50 ) {
			continue;		// resting grenade / stuck mine
		}
		t = DotProduct( rel, vel ) / speed2;
		if ( t <= 0 ) {
			continue;		// flying away from us
		}
		// our offset from the closest point of its path
		VectorMA( rel, -t, vel, off );
		if ( VectorLengthSquared( off ) >= RM_MISSILE_MISS * RM_MISSILE_MISS ) {
			continue;
		}
		away[0] = off[0];
		away[1] = off[1];
		away[2] = 0;
		if ( VectorNormalize( away ) < 4 ) {
			// dead on: either side of its path
			away[0] = -vel[1];
			away[1] = vel[0];
			away[2] = 0;
			if ( VectorNormalize( away ) < 0.001f ) {
				VectorSet( away, crandom(), crandom(), 0 );
				VectorNormalize( away );
			}
			if ( random() < 0.5f ) {
				VectorNegate( away, away );
			}
		}
		return qtrue;
	}
	return qfalse;
}

/*
==================
RmSideStepDir

A horizontal direction across the line to the enemy (or across the view),
mostly keeping the strafe the bot is already doing
==================
*/
static void RmSideStepDir( playerState_t *ps, usercmd_t *ucmd, gentity_t *enemy, vec3_t out ) {
	vec3_t	to, angles, fwd, right, move;
	float	d;

	VectorSet( angles, 0, RmViewYaw( ps, ucmd ), 0 );
	AngleVectors( angles, fwd, right, NULL );
	if ( enemy ) {
		VectorSubtract( enemy->r.currentOrigin, ps->origin, to );
		to[2] = 0;
	} else {
		VectorCopy( fwd, to );
	}
	if ( VectorNormalize( to ) < 0.001f ) {
		VectorCopy( fwd, to );
	}
	out[0] = to[1];
	out[1] = -to[0];
	out[2] = 0;

	VectorScale( fwd, ucmd->forwardmove, move );
	VectorMA( move, ucmd->rightmove, right, move );
	d = DotProduct( move, out );
	if ( d < 0 || ( d == 0 && random() < 0.5f ) ) {
		VectorNegate( out, out );
	}
	if ( random() < 0.25f ) {
		VectorNegate( out, out );		// don't be predictable
	}
}

/*
==================
RmSplashWeapon
==================
*/
static qboolean RmSplashWeapon( int weapon ) {
	switch ( weapon ) {
	case WP_ROCKET_LAUNCHER:
	case WP_GRENADE_LAUNCHER:
	case WP_BFG:
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case WP_PROX_LAUNCHER:
#endif
		return qtrue;
	default:
		return qfalse;
	}
}

/*
==================
RmFlagRunHome

Carrying the flag close to our base: no grapple stunts
==================
*/
static qboolean RmFlagRunHome( bot_state_t *bs, playerState_t *ps ) {
	bot_goal_t	*base;

	if ( BotCTFCarryingFlag( bs ) == CTF_FLAG_NONE ) {
		return qfalse;
	}
	base = ( BotTeam( bs ) == TEAM_RED ) ? &ctf_redflag : &ctf_blueflag;
	return Distance( ps->origin, base->origin ) < RM_FLAG_HOME_DIST;
}

/*
==================
RmHookPoint

Along angles from the eye: a solid, non-sky world surface between minDist and RM_HOOK_RANGE
with safe floor below where the pull ends. Two traces.
==================
*/
static qboolean RmHookPoint( playerState_t *ps, vec3_t angles, float minDist ) {
	trace_t	tr;
	vec3_t	eye, fwd, end, back;

	if ( rmTraces < 2 ) {
		return qfalse;
	}
	VectorCopy( ps->origin, eye );
	eye[2] += ps->viewheight;
	AngleVectors( angles, fwd, NULL, NULL );
	VectorMA( eye, RM_HOOK_RANGE, fwd, end );
	RmTrace( &tr, eye, NULL, NULL, end, ps->clientNum, MASK_SHOT );
	if ( tr.startsolid || tr.fraction >= 1.0f || tr.entityNum != ENTITYNUM_WORLD
		|| ( tr.surfaceFlags & ( SURF_SKY | SURF_NOIMPACT ) ) || tr.fraction * RM_HOOK_RANGE < minDist ) {
		return qfalse;
	}
	// the pull ends a little short of the hook point; don't hang over a pit
	VectorMA( tr.endpos, -48, fwd, back );
	return RmSafeGround( ps->clientNum, back, ps->origin[2] - RM_SAFE_DROP );
}

/*
==================
RmFindEscape

A hook point that pulls us away from the enemy
==================
*/
static qboolean RmFindEscape( playerState_t *ps, gentity_t *enemy, vec3_t angles ) {
	static const float	offsets[3] = { 0, 40, -40 };
	vec3_t	away, base;
	int		i, first;

	VectorSubtract( ps->origin, enemy->r.currentOrigin, away );
	away[2] = 0;
	if ( VectorNormalize( away ) < 0.001f ) {
		return qfalse;
	}
	vectoangles( away, base );
	first = rand() % 3;
	for ( i = 0 ; i < 3 && rmTraces >= 2 ; i++ ) {
		angles[PITCH] = -20 - random() * 15;
		angles[YAW] = base[YAW] + offsets[( first + i ) % 3];
		angles[ROLL] = 0;
		if ( RmHookPoint( ps, angles, RM_HOOK_MIN_DIST - 50 ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
==================
RmHookStart
==================
*/
/*
==================
RmHookStillPulling

A traversal / escape / recovery hook that hasn't reached its point yet: letting go now would
drop us wherever we are (often over the pit we were crossing), so hold on (up to 2.6 s)
==================
*/
static qboolean RmHookStillPulling( bot_state_t *bs, playerState_t *ps, qboolean onGround ) {
	if ( bs->rmHookUse != RM_HOOKUSE_TRAVERSE && bs->rmHookUse != RM_HOOKUSE_ESCAPE && bs->rmHookUse != RM_HOOKUSE_RECOVER ) {
		return qfalse;
	}
	return !onGround && ( ps->pm_flags & PMF_GRAPPLE_PULL ) && level.time - bs->rmHookStart < 2600
		&& Distance( ps->origin, ps->grapplePoint ) > 96;
}

static void RmHookStart( bot_state_t *bs, int use, vec3_t angles, int hold ) {
	bs->rmHookState = RM_HOOK_AIM;
	bs->rmHookUse = use;
	bs->rmHookStart = level.time;
	bs->rmHookHoldUntil = level.time + hold;
	VectorCopy( angles, bs->rmHookAngles );
}

/*
===============================================================================

Zip line (BUTTON_ZIP): traversal along the run, up to the item we're going for,
chasing a far enemy, escaping a close one. pmove fires the cable along the view of the
usercmd that presses the button, so the view is locked on the anchor while it's held.

===============================================================================
*/
#define RM_ZIP_IDLE				0
#define RM_ZIP_FIRE				1		// button held, view locked on the anchor
#define RM_ZIP_RIDE				2

#define RM_ZIPUSE_TRAVERSE		1
#define RM_ZIPUSE_GOAL			2
#define RM_ZIPUSE_CHASE			3
#define RM_ZIPUSE_ESCAPE		4

#define RM_ZIP_FAR				1700	// search range (a bit short of ZIP_RANGE)
#define RM_ZIP_MIN				350		// not worth it closer than this
#define RM_ZIP_CHASE_DIST		750		// chase a visible enemy beyond this
#define RM_ZIP_CHASE_CLOSE		260		// ... and let go inside this
#define RM_ZIP_RIDE_MAX			3500	// safety: let go after this long

static const char *rmZipUseNames[5] = { "?", "traverse", "goal", "chase", "escape" };

/*
==================
RmZipPoint

Along angles from the eye: where the zip line would hold (the same test pmove makes),
with floor under the end of the ride. Two traces.
==================
*/
static qboolean RmZipPoint( playerState_t *ps, const vec3_t angles, float minDist, vec3_t anchor ) {
	trace_t	tr;
	vec3_t	eye, fwd, end, back;

	if ( rmTraces < 2 ) {
		return qfalse;
	}
	VectorCopy( ps->origin, eye );
	eye[2] += ps->viewheight;
	AngleVectors( angles, fwd, NULL, NULL );
	VectorMA( eye, RM_ZIP_FAR, fwd, end );
	RmTrace( &tr, eye, NULL, NULL, end, ps->clientNum, MASK_SHOT );
	if ( tr.startsolid || tr.fraction >= 1.0f || tr.entityNum != ENTITYNUM_WORLD
		|| ( tr.surfaceFlags & ( SURF_SKY | SURF_NOIMPACT ) ) || tr.fraction * RM_ZIP_FAR < minDist ) {
		return qfalse;
	}
	VectorCopy( tr.endpos, anchor );
	// the ride drops us off at the anchor: no pits, lava or hurt triggers below it
	VectorMA( tr.endpos, -40, fwd, back );
	return RmSafeGround( ps->clientNum, back, back[2] - RM_SAFE_DROP );
}

/*
==================
RmZipAt

Angles from the eye to a point
==================
*/
static void RmZipAt( playerState_t *ps, const vec3_t point, vec3_t angles ) {
	vec3_t	eye, dir;

	VectorCopy( ps->origin, eye );
	eye[2] += ps->viewheight;
	VectorSubtract( point, eye, dir );
	vectoangles( dir, angles );
	angles[ROLL] = 0;
}

static void RmZipStart( bot_state_t *bs, int use, const vec3_t angles, float dist ) {
	bs->rmZipState = RM_ZIP_FIRE;
	bs->rmZipUse = use;
	bs->rmZipStart = level.time;
	VectorCopy( angles, bs->rmZipAngles );
	if ( BotTrickDebug() || trap_Cvar_VariableIntegerValue( "g_debugAlt" ) ) {
		G_LogPrintf( "Move: %i zip %s %.0f\n", bs->client, rmZipUseNames[use], dist );
	}
}

/*
==================
BotRearmZip

Returns qtrue while a zip is being fired or ridden (the other moves stay out of the way)
==================
*/
static qboolean BotRearmZip( bot_state_t *bs, playerState_t *ps, usercmd_t *ucmd, gentity_t *enemy, float dist,
		qboolean visible, float skill, float hspeed, qboolean jetpack ) {
	gentity_t			*ent = &g_entities[bs->client];
	const rmPersona_t	*persona = BotPersona( bs );
	bot_goal_t			goal;
	vec3_t				angles, away, base, target, anchor, flat;
	float				chance, d;
	int					i, first;
	qboolean			release;
	static const float	offsets[3] = { 0, 35, -35 };

	// riding
	if ( ps->loopSound & RMZ_ZIPPING ) {
		if ( bs->rmZipState != RM_ZIP_RIDE ) {
			if ( bs->rmZipState == RM_ZIP_IDLE ) {
				bs->rmZipUse = RM_ZIPUSE_TRAVERSE;		// (not ours: keep riding)
			}
			bs->rmZipState = RM_ZIP_RIDE;
			bs->rmZipStart = level.time;
			if ( trap_Cvar_VariableIntegerValue( "g_debugAlt" ) ) {
				// what the other clients draw the cable from (BG_PlayerStateToEntityState)
				G_LogPrintf( "Move: %i zip ride: entity zip bit %i anchor %.0f %.0f %.0f\n", bs->client,
					( ent->s.generic1 & RM_BODY_ZIP ) != 0, ent->s.origin2[0], ent->s.origin2[1], ent->s.origin2[2] );
			}
		}
		ucmd->buttons &= ~( BUTTON_ZIP | BUTTON_DASH | BUTTON_HOOK );
		RmAct( bs->client, RM_ACT_ZIP );
		release = level.time - bs->rmZipStart > RM_ZIP_RIDE_MAX;
		if ( bs->rmZipUse == RM_ZIPUSE_CHASE && ( !enemy || dist < RM_ZIP_CHASE_CLOSE ) ) {
			release = qtrue;		// on top of them: let go and keep the momentum
		}
		// the stock AI's jumps would let go by accident; a held jump lets go on purpose
		ucmd->upmove = release ? 127 : 0;
		return qtrue;
	}
	if ( bs->rmZipState == RM_ZIP_RIDE ) {
		// dropped off, let go or knocked off
		bs->rmZipState = RM_ZIP_IDLE;
		bs->rmZipNext = level.time + 1500 + (int)( random() * 2000 );
		return qfalse;
	}
	if ( bs->rmZipState == RM_ZIP_FIRE ) {
		if ( level.time - bs->rmZipStart > 250 ) {
			// the cable didn't hold (moved, blocked) or we already arrived
			bs->rmZipState = RM_ZIP_IDLE;
			bs->rmZipNext = level.time + 1500;
			return qfalse;
		}
		if ( level.time - bs->rmZipStart < RM_PRESS_HOLD ) {
			ucmd->buttons |= BUTTON_ZIP;
		}
		RmSetViewAngles( ps, ucmd, bs->rmZipAngles );
		if ( bs->rmZipUse != RM_ZIPUSE_CHASE ) {
			ucmd->buttons &= ~( BUTTON_ATTACK | BUTTON_ALTATTACK );
		}
		ucmd->upmove = 0;
		return qtrue;
	}

	// --- decide -------------------------------------------------------------------------------
	if ( !( RM_FLAGS2( ps ) & RMF2_ZIPLINE ) || ( ps->loopSound & RMZ_USED ) || jetpack || ent->waterlevel > 1
		|| bs->rmHookState != RM_HOOK_IDLE || bs->rmSlideState != 0 || ( RM_FLAGS2( ps ) & RMS2_SLAMMING )
		|| level.time < bs->rmZipNext || rmTraces < 4 || persona->zip <= 0 || RmFlagRunHome( bs, ps ) ) {
		return qfalse;
	}
	bs->rmZipNext = level.time + 400;

	if ( enemy && dist < 450 && ps->stats[STAT_HEALTH] < 45 ) {
		// escape: zip away from a close enemy when hurt
		chance = ( 0.3f + 0.45f * skill ) * persona->zip;
		if ( random() < chance ) {
			VectorSubtract( ps->origin, enemy->r.currentOrigin, away );
			away[2] = 0;
			if ( VectorNormalize( away ) > 0.001f ) {
				vectoangles( away, base );
				first = rand() % 3;
				for ( i = 0 ; i < 3 && rmTraces >= 2 ; i++ ) {
					angles[PITCH] = -12 - random() * 25;
					angles[YAW] = base[YAW] + offsets[( first + i ) % 3];
					angles[ROLL] = 0;
					if ( RmZipPoint( ps, angles, RM_ZIP_MIN + 100, anchor ) ) {
						RmZipStart( bs, RM_ZIPUSE_ESCAPE, angles, Distance( ps->origin, anchor ) );
						return qtrue;
					}
				}
			}
		}
		bs->rmZipNext = level.time + 900;
		return qfalse;
	}

	if ( enemy && visible ) {
		// chase: zip in on a far enemy (or one high above), most of all with a close range gun
		if ( dist < RM_ZIP_CHASE_DIST || dist > RM_ZIP_FAR - 100 ) {
			return qfalse;
		}
		if ( enemy->r.currentOrigin[2] < ps->origin[2] + 96 && !RmCloseRangeWeapon( ps->weapon ) ) {
			return qfalse;
		}
		bs->rmZipNext = level.time + 1500 + (int)( random() * 1500 );
		chance = ( 0.15f + 0.35f * skill ) * persona->zip * persona->hookOffense;
		if ( random() >= chance ) {
			return qfalse;
		}
		// the floor just in front of their feet, else the wall behind them
		VectorSubtract( enemy->r.currentOrigin, ps->origin, flat );
		flat[2] = 0;
		VectorNormalize( flat );
		VectorMA( enemy->r.currentOrigin, -56, flat, target );
		target[2] += MINS_Z - 8;
		RmZipAt( ps, target, angles );
		if ( !RmZipPoint( ps, angles, RM_ZIP_MIN, anchor ) || Distance( anchor, enemy->r.currentOrigin ) > 220 ) {
			VectorMA( enemy->r.currentOrigin, 120, flat, target );
			target[2] += 24;
			RmZipAt( ps, target, angles );
			if ( !RmZipPoint( ps, angles, RM_ZIP_MIN, anchor ) || Distance( anchor, enemy->r.currentOrigin ) > 260 ) {
				return qfalse;
			}
		}
		RmZipStart( bs, RM_ZIPUSE_CHASE, angles, Distance( ps->origin, anchor ) );
		return qtrue;
	}
	if ( enemy ) {
		return qfalse;		// hunting a hidden enemy: the stock AI knows the way
	}

	// out of combat: up to the item we're going for when it's on a ledge above
	if ( trap_BotGetTopGoal( bs->gs, &goal ) ) {
		VectorSubtract( goal.origin, ps->origin, flat );
		d = flat[2];
		flat[2] = 0;
		if ( d > 100 && VectorLength( flat ) > 120 && VectorLength( flat ) < RM_ZIP_FAR - 300 ) {
			bs->rmZipNext = level.time + 1200;
			chance = ( 0.45f + 0.4f * skill ) * persona->zip;
			if ( random() < chance ) {
				VectorCopy( goal.origin, target );
				target[2] += goal.mins[2] - 6;		// the floor it sits on
				RmZipAt( ps, target, angles );
				if ( RmZipPoint( ps, angles, RM_ZIP_MIN - 100, anchor ) && Distance( anchor, target ) < 96 ) {
					RmZipStart( bs, RM_ZIPUSE_GOAL, angles, Distance( ps->origin, anchor ) );
					return qtrue;
				}
			}
		}
	}

	// traversal: grab something far ahead and up along the way we're running
	if ( hspeed > RM_RUN_SPEED && level.time >= bs->rmZipTravNext ) {
		bs->rmZipTravNext = level.time + 5000 + (int)( random() * 6000 / persona->zip );
		chance = ( 0.3f + 0.4f * skill ) * persona->zip * persona->hookTraverse;
		if ( random() < chance ) {
			VectorSet( flat, ps->velocity[0], ps->velocity[1], 0 );
			vectoangles( flat, angles );
			angles[PITCH] = -12 - random() * 22;
			angles[ROLL] = 0;
			if ( RmZipPoint( ps, angles, RM_ZIP_MIN + 250, anchor ) ) {
				RmZipStart( bs, RM_ZIPUSE_TRAVERSE, angles, Distance( ps->origin, anchor ) );
				return qtrue;
			}
		}
	}
	return qfalse;
}

/*
===============================================================================

v4: danger spots, slides, ground pounds, blinks, triple jumps

===============================================================================
*/
#define RM_SLIDE_IDLE			0
#define RM_SLIDE_PENDING		1		// a dodge slide waiting out the reaction delay
#define RM_SLIDE_CROUCH			2
#define RM_SLIDE_JUMP			3
#define RM_SLIDE_RELEASE		4

#define RM_DANGER_MARGIN		40		// get this far outside a danger radius
#define RM_SLAM_DANGER			190		// keep out from under a ground pound this far
#define RM_WALK_SPEED			300		// how fast we can walk out of a danger spot
#define RM_SLAM_REACH			130		// predicted landing this close to the enemy
#define RM_GAP_MIN				350		// gap-closing dash / blink band
#define RM_GAP_MAX				900
#define RM_ADRENALINE_TIME		3000	// dash-happy this long after a kill (adrenaline perk)

#ifndef RM_ENT_STRIKE
#define RM_ENT_STRIKE			67
#endif

typedef struct {
	vec3_t		origin;
	float		radius;
	int			timeLeft;		// msec until it goes off
	qboolean	urgent;			// dash out if walking won't make it
	int			kind;			// for the debug print
} rmDanger_t;

// kinds (debug)
#define RM_DK_STRIKE			1
#define RM_DK_FIRE				2
#define RM_DK_PROP_LIT			3
#define RM_DK_PROP_THREAT		4
#define RM_DK_SLAM				5
#define RM_DK_CORPSE			6

static const char *rmDangerNames[] = { "?", "strike", "fire", "burning prop", "prop under fire", "ground pound", "corpse" };

/*
==================
RmExplosiveWeapon
==================
*/
static qboolean RmExplosiveWeapon( int weapon ) {
	switch ( weapon ) {
	case WP_ROCKET_LAUNCHER:
	case WP_GRENADE_LAUNCHER:
	case WP_BFG:
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case WP_PROX_LAUNCHER:
#endif
		return qtrue;
	default:
		return qfalse;
	}
}

/*
==================
RmExplosiveThreat

An enemy with a splash gun close enough to set off a barrel next to us
==================
*/
static qboolean RmExplosiveThreat( bot_state_t *bs, playerState_t *ps ) {
	gentity_t	*other;

	// only the enemy we're fighting (seen in the last two seconds): barrels everywhere
	// (barrel bonanza) must not turn every walk into a slalom
	if ( bs->enemy < 0 || bs->enemy >= MAX_CLIENTS || level.time - bs->rmEnemySeen > 2000 ) {
		return qfalse;
	}
	other = &g_entities[bs->enemy];
	if ( !other->inuse || !other->client || other->health <= 0 ) {
		return qfalse;
	}
	return RmExplosiveWeapon( other->client->ps.weapon ) && Distance( other->r.currentOrigin, ps->origin ) < 1300;
}

/*
==================
RmDangerTest

Is point inside this spot (with the margin)? Keeps the most pressing one in best.
==================
*/
static void RmDangerTest( const vec3_t point, const vec3_t origin, float radius, int timeLeft, qboolean urgent,
	int kind, float below, float above, rmDanger_t *best, qboolean *found ) {
	float	dx, dy, dz, r;

	dz = point[2] - origin[2];
	if ( dz < -below || dz > above ) {
		return;
	}
	dx = point[0] - origin[0];
	dy = point[1] - origin[1];
	r = radius + RM_DANGER_MARGIN;
	if ( dx * dx + dy * dy > r * r ) {
		return;
	}
	if ( *found && timeLeft >= best->timeLeft ) {
		return;
	}
	VectorCopy( origin, best->origin );
	best->radius = radius;
	best->timeLeft = timeLeft;
	best->urgent = urgent;
	best->kind = kind;
	*found = qtrue;
}

/*
==================
RmDangerSpot

The most pressing danger spot containing point: event strike markers (thunderstorm, meteors),
fire patches, burning / ruptured props, barrels when an enemy carries explosives,
under an enemy ground pound, chain reaction corpses.
==================
*/
static qboolean RmDangerSpot( bot_state_t *bs, playerState_t *ps, const vec3_t point, rmDanger_t *out, qboolean lookahead ) {
	gentity_t	*ent;
	gclient_t	*cl;
	vec3_t		spot;
	float		radius, fall;
	int			i, threat = -1, left;
	qboolean	found = qfalse;

	for ( i = 0, ent = g_entities ; i < level.num_entities ; i++, ent++ ) {
		if ( !ent->inuse ) {
			continue;
		}
		if ( i < MAX_CLIENTS ) {
			cl = ent->client;
			if ( !cl || i == bs->client || cl->sess.sessionTeam == TEAM_SPECTATOR ) {
				continue;
			}
			// an enemy ground pound coming down on us
			if ( ent->health > 0 && ( RM_FLAGS2( &cl->ps ) & RMS2_SLAMMING ) && !BotSameTeam( bs, i ) ) {
				fall = cl->ps.origin[2] - point[2];
				if ( fall > 16 ) {
					left = (int)( 1000 * fall / SLAM_SPEED );
					VectorMA( cl->ps.origin, SLAM_KEEP_SPEED * left / 1000.0f, cl->ps.velocity, spot );
					spot[2] = point[2];
					RmDangerTest( point, spot, RM_SLAM_DANGER, left, qtrue, RM_DK_SLAM, 64, 64, out, &found );
				}
				continue;
			}
			continue;
		}
		if ( ent->s.eType != ET_GENERAL ) {
			continue;
		}
		switch ( ent->s.generic1 ) {
		case RM_ENT_STRIKE:
			// meteor / lightning warning markers, chain reaction corpses about to blow
			radius = ent->random > 0 ? ent->random : ent->s.otherEntityNum2;
			if ( ent->s.modelindex2 == RM_STRIKE_CORPSE ) {
				if ( ent->spawnflags && ent->parent == &g_entities[bs->client] ) {
					break;		// the chain reaction perk spares its owner
				}
				RmDangerTest( point, ent->r.currentOrigin, radius, ent->s.time2 - level.time, qtrue, RM_DK_CORPSE, 96, 96, out, &found );
				break;
			}
			RmDangerTest( point, ent->r.currentOrigin, radius, ent->s.time2 - level.time, qtrue, RM_DK_STRIKE, 64, 200, out, &found );
			break;
		case RM_ENT_FIRE:
			RmDangerTest( point, ent->r.currentOrigin, ent->random, 2000, qfalse, RM_DK_FIRE, 40, 88, out, &found );
			break;
		case RM_ENT_PROP:
			if ( !BotRearmExplosiveProp( ent ) ) {
				break;
			}
			if ( BotRearmPropBurning( ent ) ) {
				RmDangerTest( point, ent->r.currentOrigin, BotRearmPropRadius( ent ), ent->s.time2 - level.time,
					qtrue, RM_DK_PROP_LIT, 96, 120, out, &found );
			} else {
				if ( threat < 0 ) {
					threat = !lookahead && RmExplosiveThreat( bs, ps );
				}
				if ( threat ) {
					// don't stand next to the barrel while someone has a rocket launcher
					RmDangerTest( point, ent->r.currentOrigin, BotRearmPropRadius( ent ) * 0.6f, 3000,
						qfalse, RM_DK_PROP_THREAT, 96, 120, out, &found );
				}
			}
			break;
		default:
			break;
		}
	}
	return found;
}

/*
==================
RmStartDash

Dash (or blink) along dir, held for RM_PRESS_HOLD
==================
*/
static void RmStartDash( bot_state_t *bs, playerState_t *ps, usercmd_t *ucmd, const vec3_t dir ) {
	RmMoveAlong( ps, ucmd, dir );
	ucmd->buttons |= BUTTON_DASH;
	bs->rmDashHoldUntil = level.time + RM_PRESS_HOLD;
	bs->rmDashFwd = ucmd->forwardmove;
	bs->rmDashRight = ucmd->rightmove;
	bs->rmDodgeAt = 0;
	RmAct( bs->client, RM_ACT_DASH );
}

/*
==================
RmRotateFlat

Rotate a horizontal direction by deg degrees around the up axis
==================
*/
static void RmRotateFlat( const vec3_t in, float deg, vec3_t out ) {
	float	s, c, x, y;

	s = sin( DEG2RAD( deg ) );
	c = cos( DEG2RAD( deg ) );
	x = in[0];
	y = in[1];
	out[0] = x * c - y * s;
	out[1] = x * s + y * c;
	out[2] = 0;
}

/*
==================
RmDangerMove

Step (or dash) out of a danger spot, or steer around one we're about to walk into.
Returns qtrue when it took over the movement this frame.
==================
*/
static qboolean RmDangerMove( bot_state_t *bs, playerState_t *ps, usercmd_t *ucmd, gentity_t *enemy,
	float skill, float hspeed, qboolean canDash ) {
	rmDanger_t	d;
	vec3_t		away, try1, ahead, move, angles, fwd, right, to;
	float		len, need, along;
	int			i, reaction;

	if ( RmDangerSpot( bs, ps, ps->origin, &d, qfalse ) ) {
		if ( !bs->rmDangerSeen ) {
			bs->rmDangerSeen = level.time;
			if ( BotTrickDebug() ) {
				G_Printf( "bot_tricks: %i in danger (%s, r %.0f, %i ms)\n", bs->client, rmDangerNames[d.kind], d.radius, d.timeLeft );
			}
		}
		reaction = 150 + (int)( 350 * ( 1 - skill ) );
		if ( level.time < bs->rmDangerSeen + reaction ) {
			return qfalse;
		}
		if ( !d.urgent && level.time > bs->rmDangerSeen + 2500 ) {
			return qfalse;		// a lingering spot we can't get out of (the goal is in it): carry on
		}
		VectorSubtract( ps->origin, d.origin, away );
		away[2] = 0;
		len = VectorNormalize( away );
		if ( len < 8 ) {
			RmSideStepDir( ps, ucmd, enemy, away );
		}
		need = d.radius + RM_DANGER_MARGIN - len;
		if ( need < 16 ) {
			need = 16;
		}
		// straight away, else 60 / 90 degrees off either side
		for ( i = 0 ; i < 5 ; i++ ) {
			static const float	turn[5] = { 0, 60, -60, 100, -100 };
			RmRotateFlat( away, turn[i], try1 );
			if ( RmSafeAhead( ps, try1, need + 24 ) ) {
				break;
			}
		}
		if ( i == 5 ) {
			return qfalse;		// nowhere safe to go: take it rather than jump into a pit
		}
		RmMoveAlong( ps, ucmd, try1 );
		if ( ucmd->upmove < 0 ) {
			ucmd->upmove = 0;
		}
		if ( d.urgent && canDash && level.time >= bs->rmDangerDash && level.time >= bs->rmDashHoldUntil
			&& need > RM_WALK_SPEED * d.timeLeft / 1000.0f - 16 && RmSafePath( ps, try1, RM_DASH_REACH ) ) {
			bs->rmDangerDash = level.time + 900;
			RmStartDash( bs, ps, ucmd, try1 );
			if ( BotTrickDebug() ) {
				G_Printf( "bot_tricks: %i dashes out of %s (need %.0f, %i ms)\n", bs->client, rmDangerNames[d.kind], need, d.timeLeft );
			}
		}
		return qtrue;
	}
	bs->rmDangerSeen = 0;

	// about to walk into one: slide the move along its edge instead
	if ( hspeed < 100 ) {
		return qfalse;
	}
	VectorMA( ps->origin, 0.3f, ps->velocity, ahead );
	if ( !RmDangerSpot( bs, ps, ahead, &d, qtrue ) ) {
		return qfalse;
	}
	VectorSet( angles, 0, RmViewYaw( ps, ucmd ), 0 );
	AngleVectors( angles, fwd, right, NULL );
	VectorScale( fwd, ucmd->forwardmove, move );
	VectorMA( move, ucmd->rightmove, right, move );
	move[2] = 0;
	VectorSubtract( d.origin, ps->origin, to );
	to[2] = 0;
	if ( VectorNormalize( to ) < 0.001f || VectorNormalize( move ) < 0.001f ) {
		return qfalse;
	}
	along = DotProduct( move, to );
	if ( along <= 0 ) {
		return qfalse;
	}
	VectorMA( move, -along, to, move );
	if ( VectorNormalize( move ) < 0.1f ) {
		move[0] = -to[1];
		move[1] = to[0];
	}
	RmMoveAlong( ps, ucmd, move );
	return qtrue;
}

/*
==================
RmSlideStart
==================
*/
static void RmSlideStart( bot_state_t *bs, playerState_t *ps, gentity_t *enemy, float dist, float skill, int delay, const char *why ) {
	int		full;

	full = ( RM_FLAGS2( ps ) & RMP2_SLIPSTREAM ) ? 2 * SLIDE_TIME : SLIDE_TIME;
	bs->rmSlideJump = random() < ( enemy && dist < 600 ? 0.55f : 0.35f ) + 0.35f * skill;
	bs->rmSlideStart = level.time + delay;
	bs->rmSlideState = delay > 0 ? RM_SLIDE_PENDING : RM_SLIDE_CROUCH;
	// a slide-jump comes early (it keeps the speed); a plain slide runs out
	bs->rmSlideUntil = bs->rmSlideStart + ( bs->rmSlideJump ? 300 + (int)( random() * 350 ) : full + 100 );
	RmAct( bs->client, RM_ACT_SLIDE );
	if ( BotTrickDebug() ) {
		G_Printf( "bot_tricks: %i slides (%s)%s\n", bs->client, why, bs->rmSlideJump ? " + jump" : "" );
	}
}

/*
==================
BotRearmSlide

Slides: into fights, chasing, landing at speed, and (from the dodge code) under fire.
Crouch while running starts it; half the time it ends early in a slide-jump that keeps the speed.
==================
*/
static void BotRearmSlide( bot_state_t *bs, playerState_t *ps, usercmd_t *ucmd, gentity_t *enemy, float dist, qboolean visible,
	float skill, float hspeed, qboolean onGround, qboolean landed, qboolean jetpack, float slideMul, qboolean dangerMove ) {
	gentity_t			*ent = &g_entities[bs->client];
	const rmPersona_t	*persona = BotPersona( bs );
	int					flags2 = RM_FLAGS2( ps );
	vec3_t				to, vdir, angles, fwd, right, move;
	float				chance, toward;
	const char			*why;

	if ( !( flags2 & RMF2_SLIDE ) || ent->waterlevel > 1 || jetpack || bs->rmHookState != RM_HOOK_IDLE ) {
		if ( bs->rmSlideState == RM_SLIDE_CROUCH && ucmd->upmove < 0 ) {
			ucmd->upmove = 0;
		}
		bs->rmSlideState = RM_SLIDE_IDLE;
		return;
	}

	switch ( bs->rmSlideState ) {
	case RM_SLIDE_PENDING:
		if ( level.time < bs->rmSlideStart ) {
			return;
		}
		if ( !onGround || hspeed < SLIDE_MIN_SPEED || ( flags2 & RMS2_CROUCH_HELD ) || bs->rmJumpState != RM_JUMP_IDLE ) {
			bs->rmSlideState = RM_SLIDE_IDLE;
			bs->rmSlideNext = level.time + 300;
			return;
		}
		bs->rmSlideState = RM_SLIDE_CROUCH;
		ucmd->upmove = -127;
		return;

	case RM_SLIDE_CROUCH:
		if ( level.time - bs->rmSlideStart > 200 && !( flags2 & RMS2_SLIDING ) ) {
			// didn't catch, or it ran out
			bs->rmSlideState = RM_SLIDE_RELEASE;
			bs->rmSlideUntil = level.time + 100;
			ucmd->upmove = 0;
		} else if ( level.time >= bs->rmSlideUntil ) {
			if ( bs->rmSlideJump && ( flags2 & RMS2_SLIDING ) && RmSafePath( ps, ps->velocity, RM_JUMP_REACH ) ) {
				bs->rmSlideState = RM_SLIDE_JUMP;
				bs->rmSlideUntil = level.time + RM_PRESS_HOLD + 30;
				ucmd->upmove = 127;
				RmAct( bs->client, RM_ACT_SLIDEJUMP );
				if ( BotTrickDebug() ) {
					G_Printf( "bot_tricks: %i slide-jumps at %.0f ups\n", bs->client, hspeed );
				}
			} else {
				bs->rmSlideState = RM_SLIDE_RELEASE;
				bs->rmSlideUntil = level.time + 100;
				ucmd->upmove = 0;
			}
		} else {
			ucmd->upmove = -127;
		}
		return;

	case RM_SLIDE_JUMP:
		ucmd->upmove = 127;
		if ( !onGround && level.time >= bs->rmSlideUntil ) {
			bs->rmSlideState = RM_SLIDE_IDLE;
			bs->rmSlideNext = level.time + 600 + (int)( random() * 800 );
			// an air jump near the apex keeps the flow going
			if ( ( ps->stats[STAT_RMFLAGS] & RMF_DOUBLEJUMP ) && bs->rmJumpState == RM_JUMP_IDLE
				&& random() < 0.5f * persona->jump && RmSafePath( ps, ps->velocity, RM_JUMP_REACH ) ) {
				bs->rmJumpState = RM_JUMP_APEX;
				ucmd->upmove = 0;
			}
		} else if ( level.time > bs->rmSlideUntil + 300 ) {
			bs->rmSlideState = RM_SLIDE_IDLE;
			bs->rmSlideNext = level.time + 600;
		}
		return;

	case RM_SLIDE_RELEASE:
		if ( ucmd->upmove < 0 ) {
			ucmd->upmove = 0;
		}
		if ( level.time >= bs->rmSlideUntil ) {
			bs->rmSlideState = RM_SLIDE_IDLE;
			bs->rmSlideNext = level.time + 600 + (int)( random() * 900 );
		}
		return;

	default:
		break;
	}

	// idle: start one?
	if ( !onGround || hspeed < SLIDE_MIN_SPEED + 10 || bs->rmJumpState != RM_JUMP_IDLE || ( flags2 & RMS2_CROUCH_HELD )
		|| dangerMove || ucmd->upmove > 0 ) {
		return;
	}
	if ( !landed && level.time < bs->rmSlideNext ) {
		return;
	}
	bs->rmSlideNext = level.time + 600 + (int)( random() * 800 );
	VectorSet( vdir, ps->velocity[0], ps->velocity[1], 0 );
	VectorNormalize( vdir );
	// only when the move we want is where we're going (no sliding past corners)
	VectorSet( angles, 0, RmViewYaw( ps, ucmd ), 0 );
	AngleVectors( angles, fwd, right, NULL );
	VectorScale( fwd, ucmd->forwardmove, move );
	VectorMA( move, ucmd->rightmove, right, move );
	move[2] = 0;
	if ( VectorNormalize( move ) < 0.001f || DotProduct( move, vdir ) < 0.85f ) {
		return;
	}
	chance = 0;
	why = "flair";
	if ( enemy ) {
		VectorSubtract( enemy->r.currentOrigin, ps->origin, to );
		to[2] = 0;
		VectorNormalize( to );
		toward = DotProduct( vdir, to );
		if ( landed && hspeed > 400 ) {
			chance = 0.5f;
			why = "landing";
		} else if ( toward > 0.7f && dist > 250 && dist < 1100 ) {
			chance = visible ? 0.3f : 0.22f;
			why = visible ? "engage" : "chase";
		} else if ( visible && dist < 300 && RmCloseRangeWeapon( ps->weapon ) ) {
			chance = 0.15f;
			why = "close";
		}
	} else {
		chance = ( landed && hspeed > 400 ) ? 0.3f : 0.06f;
	}
	chance *= persona->slide * slideMul * ( 0.6f + 0.4f * skill );
	if ( random() >= chance || !RmSafePath( ps, ps->velocity, RM_JUMP_REACH ) ) {
		return;
	}
	RmSlideStart( bs, ps, enemy, dist, skill, 0, why );
	ucmd->upmove = -127;
}

/*
==================
BotRearmSlam

Ground pound onto an enemy below: needs SLAM_MIN_HEIGHT of air and a fresh crouch press,
aims by predicting where the plunge lands (it keeps SLAM_KEEP_SPEED of the horizontal speed)
==================
*/
static void BotRearmSlam( bot_state_t *bs, playerState_t *ps, usercmd_t *ucmd, gentity_t *enemy, qboolean visible,
	float skill, qboolean onGround, qboolean wallrun, qboolean jetpack, float vertMul ) {
	int		flags2 = RM_FLAGS2( ps );
	trace_t	tr;
	vec3_t	end;
	float	fall, t, dx, dy, chance;

	if ( bs->rmSlamUntil ) {
		if ( onGround || ( flags2 & RMS2_SLAMMING ) || level.time >= bs->rmSlamUntil ) {
			bs->rmSlamUntil = 0;
			if ( ucmd->upmove < 0 ) {
				ucmd->upmove = 0;
			}
		} else {
			ucmd->upmove = -127;
		}
		return;
	}
	if ( !( flags2 & RMF2_SLAM ) || onGround || wallrun || !enemy || !visible || ( flags2 & RMS2_SLAMMING ) || rmTrack[bs->client].padAir
		|| bs->rmHookState != RM_HOOK_IDLE || g_entities[bs->client].waterlevel > 0 || level.time < bs->rmSlamNext ) {
		return;
	}
	fall = ps->origin[2] - enemy->r.currentOrigin[2];
	if ( fall < 64 ) {
		return;
	}
	bs->rmSlamNext = level.time + 250;
	// where the plunge lands vs where they'll be (better bots lead them)
	t = fall / SLAM_SPEED + SLAM_HANG_TIME / 1000.0f;
	dx = ps->origin[0] + ps->velocity[0] * SLAM_KEEP_SPEED * t - ( enemy->r.currentOrigin[0] + enemy->client->ps.velocity[0] * t * skill );
	dy = ps->origin[1] + ps->velocity[1] * SLAM_KEEP_SPEED * t - ( enemy->r.currentOrigin[1] + enemy->client->ps.velocity[1] * t * skill );
	if ( dx * dx + dy * dy > RM_SLAM_REACH * RM_SLAM_REACH ) {
		return;
	}
	// enough air below (the world only: the enemy under us doesn't count)
	VectorCopy( ps->origin, end );
	end[2] -= SLAM_MIN_HEIGHT + 32;
	if ( !RmTrace( &tr, ps->origin, NULL, NULL, end, ps->clientNum, MASK_SOLID ) || tr.fraction < 1.0f ) {
		return;
	}
	chance = ( 0.35f + 0.45f * skill ) * BotPersona( bs )->slam * ( vertMul > 1.0f ? 1.3f : 1.0f );
	if ( random() >= chance ) {
		bs->rmSlamNext = level.time + 700;
		return;
	}
	bs->rmSlamUntil = level.time + 200;
	bs->rmSlamNext = level.time + 1500;
	ucmd->upmove = -127;
	RmAct( bs->client, RM_ACT_SLAM );
	if ( bs->rmJumpState != RM_JUMP_IDLE ) {
		bs->rmJumpState = RM_JUMP_DONE;
		bs->rmJumpAt = 0;
		bs->rmJumpThird = 0;
	}
	if ( BotTrickDebug() ) {
		G_Printf( "bot_tricks: %i GROUND POUNDS onto %i from %.0f up%s\n", bs->client, bs->enemy, fall, jetpack ? " (jetpack)" : "" );
	}
}

/*
===============================================================================

Landing prediction, air recovery and ledges in fights

===============================================================================
*/
#define RM_JUMP_VELOCITY		270		// JUMP_VELOCITY (bg_local.h)
#define RM_AIR_ACCEL			1.0f	// pm_airaccelerate
#define RM_LAND_STEP			0.12f	// seconds per predicted arc segment
#define RM_LAND_TIME			2.4f	// how far ahead the arc is flown
#define RM_HURT_LETHAL			30		// a trigger_hurt doing this much is a pit
#define RM_RECOVER_CHECK		100		// msec between landing predictions in the air
#define RM_RECOVER_STEER		300		// msec of steering back per decision
#define RM_EDGE_DIST			80		// a pit this close beside us is a ledge we fight on
#define RM_EDGE_CHECK			450

#define RM_BOX_NONE				0
#define RM_BOX_HURT				1
#define RM_BOX_PAD				2		// a jump pad or teleporter takes over

/*
==================
RmTriggerInBox

A lethal trigger_hurt (the void of q3dm17 / q3dm18) or a jump pad / teleporter touching the box
==================
*/
static int RmTriggerInBox( const vec3_t mins, const vec3_t maxs ) {
	int			list[64], n, i, found = RM_BOX_NONE;
	gentity_t	*e;

	n = trap_EntitiesInBox( mins, maxs, list, 64 );
	for ( i = 0 ; i < n ; i++ ) {
		e = &g_entities[list[i]];
		if ( !( e->r.contents & CONTENTS_TRIGGER ) || !e->classname ) {
			continue;
		}
		if ( !Q_stricmp( e->classname, "trigger_hurt" ) && e->damage >= RM_HURT_LETHAL ) {
			return RM_BOX_HURT;
		}
		if ( !Q_stricmp( e->classname, "trigger_push" ) || !Q_stricmp( e->classname, "trigger_teleport" ) ) {
			found = RM_BOX_PAD;
		}
	}
	return found;
}

/*
==================
RmPredictLanding

Fly the arc from start with velocity velIn (plus the air control PM_AirMove gives along steer,
a flat unit vector, when it's set) for up to RM_LAND_TIME. qtrue when it comes down on floor:
a lethal hurt trigger, lava, slime, a sky floor, or still falling well below floorZ is a fall.
Up to 18 traces and box queries (outside the per frame budget: it runs every 100 ms in the air).
==================
*/
static qboolean RmPredictLanding( gentity_t *ent, playerState_t *ps, const vec3_t start, const vec3_t velIn,
	const vec3_t steer, float floorZ ) {
	trace_t	tr;
	vec3_t	pos, vel, next, bmins, bmaxs;
	float	t, dt, g, wish, add, into;
	int		i, clips = 0, box;

	VectorCopy( start, pos );
	VectorCopy( velIn, vel );
	g = ps->gravity;
	wish = ps->speed;
	dt = RM_LAND_STEP;
	for ( t = 0 ; t < RM_LAND_TIME ; t += dt ) {
		if ( steer ) {
			add = wish - DotProduct( vel, steer );
			if ( add > 0 ) {
				if ( add > wish * dt * RM_AIR_ACCEL ) {
					add = wish * dt * RM_AIR_ACCEL;
				}
				vel[0] += add * steer[0];
				vel[1] += add * steer[1];
			}
		}
		next[0] = pos[0] + vel[0] * dt;
		next[1] = pos[1] + vel[1] * dt;
		next[2] = pos[2] + vel[2] * dt - 0.5f * g * dt * dt;
		vel[2] -= g * dt;
		trap_Trace( &tr, pos, ent->r.mins, ent->r.maxs, next, ps->clientNum, MASK_PLAYERSOLID | CONTENTS_LAVA | CONTENTS_SLIME );
		if ( tr.allsolid || tr.startsolid ) {
			return qtrue;		// wedged in something: not a fall
		}
		for ( i = 0 ; i < 3 ; i++ ) {
			bmins[i] = ( pos[i] < tr.endpos[i] ? pos[i] : tr.endpos[i] ) + ent->r.mins[i];
			bmaxs[i] = ( pos[i] > tr.endpos[i] ? pos[i] : tr.endpos[i] ) + ent->r.maxs[i];
		}
		box = RmTriggerInBox( bmins, bmaxs );
		if ( box == RM_BOX_HURT ) {
			return qfalse;
		}
		if ( box == RM_BOX_PAD ) {
			return qtrue;
		}
		if ( tr.contents & ( CONTENTS_LAVA | CONTENTS_SLIME ) ) {
			return qfalse;
		}
		VectorCopy( tr.endpos, pos );
		if ( tr.fraction < 1.0f ) {
			if ( tr.plane.normal[2] >= 0.7f ) {
				return !( tr.surfaceFlags & SURF_SKY );		// the floor
			}
			// a wall or a ceiling: slide along it like pmove does. Pressed against the side of
			// a ledge (steering into it), we just slide down it: what's below decides.
			if ( ++clips > 3 ) {
				VectorCopy( pos, next );
				next[2] -= 2048;
				trap_Trace( &tr, pos, ent->r.mins, ent->r.maxs, next, ps->clientNum, MASK_PLAYERSOLID | CONTENTS_LAVA | CONTENTS_SLIME );
				VectorAdd( pos, ent->r.mins, bmins );
				VectorAdd( tr.endpos, ent->r.maxs, bmaxs );
				bmins[2] = tr.endpos[2] + ent->r.mins[2];
				bmaxs[2] = pos[2] + ent->r.maxs[2];
				if ( tr.allsolid || tr.startsolid ) {
					return qtrue;
				}
				if ( tr.fraction >= 1.0f || tr.plane.normal[2] < 0.7f || ( tr.surfaceFlags & SURF_SKY )
					|| ( tr.contents & ( CONTENTS_LAVA | CONTENTS_SLIME ) ) || RmTriggerInBox( bmins, bmaxs ) == RM_BOX_HURT ) {
					return qfalse;
				}
				return tr.endpos[2] > floorZ - RM_SAFE_DROP;
			}
			into = DotProduct( vel, tr.plane.normal );
			if ( into < 0 ) {
				VectorMA( vel, -1.001f * into, tr.plane.normal, vel );
			}
		}
	}
	// still in the air after that long: a fall, unless we're still well above where we stood
	return pos[2] > floorZ + 64;
}

/*
==================
RmWishDir

The flat world direction the usercmd moves in (zero if none)
==================
*/
static void RmWishDir( playerState_t *ps, usercmd_t *ucmd, vec3_t out ) {
	vec3_t	angles, fwd, right;

	VectorSet( angles, 0, RmViewYaw( ps, ucmd ), 0 );
	AngleVectors( angles, fwd, right, NULL );
	VectorScale( fwd, ucmd->forwardmove, out );
	VectorMA( out, ucmd->rightmove, right, out );
	out[2] = 0;
	VectorNormalize( out );
}

/*
==================
RmMoveLands

Fly the arc of the move RmSafePath is asked about: a dash (or blink) along dir for the dash
reaches, otherwise a jump (an air jump in the air) carrying on along dir
==================
*/
static qboolean RmMoveLands( playerState_t *ps, const vec3_t dir, float reach ) {
	gentity_t	*ent = &g_entities[ps->clientNum];
	trace_t		tr;
	vec3_t		flat, vel, start;
	float		hs, floorZ;
	qboolean	onGround = ps->groundEntityNum != ENTITYNUM_NONE;

	if ( !ent->client ) {
		return qtrue;
	}
	VectorSet( flat, dir[0], dir[1], 0 );
	if ( VectorNormalize( flat ) < 0.001f ) {
		return qtrue;
	}
	floorZ = onGround ? ps->origin[2] : rmTrack[ps->clientNum].safePos[2];
	hs = sqrt( ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1] );
	VectorCopy( ps->origin, start );
	if ( reach == RM_DASH_REACH || reach == BLINK_DIST ) {
		if ( RM_FLAGS2( ps ) & RMP2_BLINK ) {
			VectorMA( ps->origin, BLINK_DIST, flat, start );
			trap_Trace( &tr, ps->origin, ent->r.mins, ent->r.maxs, start, ps->clientNum, MASK_PLAYERSOLID );
			VectorCopy( tr.endpos, start );
			VectorCopy( ps->velocity, vel );
		} else {
			if ( hs < DASH_SPEED ) {
				hs = DASH_SPEED;
			}
			VectorScale( flat, hs, vel );
			vel[2] = ps->velocity[2] < DASH_LIFT ? DASH_LIFT : ps->velocity[2];
		}
	} else {
		if ( hs < RM_RUN_SPEED ) {
			hs = RM_RUN_SPEED;
		}
		VectorScale( flat, hs, vel );
		if ( onGround ) {
			vel[2] = RM_JUMP_VELOCITY;
		} else {
			vel[2] = ( ps->velocity[2] > 0 ? ps->velocity[2] * 0.5f : 0 ) + DOUBLE_JUMP_VELOCITY;
		}
	}
	return RmPredictLanding( ent, ps, start, vel, flat, floorZ );
}

/*
==================
RmAirJumpLands

Would an air jump now (jumpVel up, steering the way the usercmd moves) come down on floor?
==================
*/
static qboolean RmAirJumpLands( bot_state_t *bs, playerState_t *ps, usercmd_t *ucmd, float jumpVel ) {
	vec3_t	vel, wish;

	VectorCopy( ps->velocity, vel );
	vel[2] = ( vel[2] > 0 ? vel[2] * 0.5f : 0 ) + jumpVel;
	RmWishDir( ps, ucmd, wish );
	return RmPredictLanding( &g_entities[bs->client], ps, ps->origin, vel, ( wish[0] || wish[1] ) ? wish : NULL,
		rmTrack[bs->client].safePos[2] );
}

/*
==================
BotRearmAirRecover

In the air with the landing over a pit (whatever threw us: a knock, a slide, a jump, the stock
AI's own route): steer back toward the last floor we stood on, or dash, air jump or hook back to
it, whichever the prediction says lands. Returns qtrue while it owns the movement.
==================
*/
static qboolean BotRearmAirRecover( bot_state_t *bs, playerState_t *ps, usercmd_t *ucmd, float skill,
	qboolean canHook, qboolean zipBusy, qboolean jetpack ) {
	gentity_t	*ent = &g_entities[bs->client];
	rmTrack_t	*t = &rmTrack[bs->client];
	vec3_t		wishDir, home, vel, eye, target, angles, dir, end;
	trace_t		tr;
	float		hs;
	qboolean	airJump, extraJump;
	const char	*how;

	if ( ps->groundEntityNum != ENTITYNUM_NONE || t->padAir || zipBusy || jetpack || ent->waterlevel > 1
		|| ( ps->pm_flags & PMF_GRAPPLE_PULL ) || ( ps->stats[STAT_RMFLAGS] & RMS_WALLRUN )
		|| ( bs->rmHookState != RM_HOOK_IDLE && bs->rmHookUse != RM_HOOKUSE_RECOVER ) ) {
		t->recoverUntil = 0;
		t->recoverJumpAt = 0;
		return qfalse;
	}
	if ( bs->rmHookState != RM_HOOK_IDLE ) {
		return qtrue;		// hooking back: the hook code holds it
	}

	if ( level.time >= t->recoverUntil && level.time >= t->recoverNext ) {
		t->recoverNext = level.time + RM_RECOVER_CHECK;
		// where the current input takes us
		RmWishDir( ps, ucmd, wishDir );
		if ( !RmPredictLanding( ent, ps, ps->origin, ps->velocity, ( wishDir[0] || wishDir[1] ) ? wishDir : NULL, t->safePos[2] ) ) {
			// going down into a pit: back toward the floor we left
			VectorSubtract( t->safePos, ps->origin, home );
			home[2] = 0;
			if ( VectorNormalize( home ) < 16 ) {
				VectorSet( home, -ps->velocity[0], -ps->velocity[1], 0 );
				if ( VectorNormalize( home ) < 0.001f ) {
					VectorCopy( wishDir, home );
				}
			}
			airJump = ( ps->stats[STAT_RMFLAGS] & RMF_DOUBLEJUMP ) && !( ps->pm_flags & PMF_DOUBLE_JUMPED );
			extraJump = ( ps->stats[STAT_RMFLAGS] & RMF_DOUBLEJUMP ) && ( RM_FLAGS2( ps ) & RMP2_FEATHER )
				&& ( ps->pm_flags & PMF_DOUBLE_JUMPED ) && !( ps->pm_flags & PMF_EXTRA_JUMPED );
			how = NULL;
			VectorCopy( home, t->recoverDir );
			t->recoverUntil = level.time + RM_RECOVER_STEER;
			RmAct( bs->client, RM_ACT_RECOVER );

			if ( RmPredictLanding( ent, ps, ps->origin, ps->velocity, home, t->safePos[2] ) ) {
				how = "steers back";
			}
			// dash back (it replaces the horizontal speed), or blink back
			if ( !how && ( ps->stats[STAT_RMFLAGS] & RMF_DASH ) && DASH_READY( ps ) && level.time >= bs->rmDashHoldUntil ) {
				if ( RM_FLAGS2( ps ) & RMP2_BLINK ) {
					VectorMA( ps->origin, BLINK_DIST, home, end );
					trap_Trace( &tr, ps->origin, ent->r.mins, ent->r.maxs, end, bs->client, MASK_PLAYERSOLID );
					if ( RmPredictLanding( ent, ps, tr.endpos, ps->velocity, home, t->safePos[2] ) ) {
						how = "blinks back";
					}
				} else {
					hs = sqrt( ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1] );
					if ( hs < DASH_SPEED ) {
						hs = DASH_SPEED;
					}
					VectorScale( home, hs, vel );
					vel[2] = ps->velocity[2] < DASH_LIFT ? DASH_LIFT : ps->velocity[2];
					if ( RmPredictLanding( ent, ps, ps->origin, vel, home, t->safePos[2] ) ) {
						how = "dashes back";
					}
				}
				if ( how ) {
					RmStartDash( bs, ps, ucmd, home );
				}
			}
			// air jump back (more air time to steer)
			if ( !how && ( airJump || extraJump ) && !( RM_FLAGS2( ps ) & RMS2_SLAMMING ) ) {
				VectorCopy( ps->velocity, vel );
				if ( vel[2] < 0 ) {
					vel[2] = 0;
				}
				vel[2] = vel[2] * 0.5f + ( airJump ? DOUBLE_JUMP_VELOCITY : FEATHER_JUMP_VELOCITY );
				if ( RmPredictLanding( ent, ps, ps->origin, vel, home, t->safePos[2] ) ) {
					how = "air jumps back";
					// release first if jump is held, then press (pmove wants a fresh press)
					t->recoverJumpAt = level.time + ( ucmd->upmove > 0 || ( ps->pm_flags & PMF_JUMP_HELD ) ? 60 : 0 );
					bs->rmJumpState = RM_JUMP_DONE;
					bs->rmJumpAt = 0;
					bs->rmJumpThird = 0;
				}
			}
			// grapple the floor we left
			if ( !how && canHook && skill > 0.2f && !t->recoverHooked ) {
				VectorCopy( t->safePos, target );
				target[2] += MINS_Z - 2;
				VectorCopy( ps->origin, eye );
				eye[2] += ps->viewheight;
				VectorSubtract( target, eye, dir );
				vectoangles( dir, angles );
				angles[ROLL] = 0;
				VectorNormalize( dir );
				VectorMA( target, 48, dir, end );
				trap_Trace( &tr, eye, NULL, NULL, end, bs->client, MASK_SHOT );
				if ( !tr.startsolid && tr.fraction < 1.0f && tr.entityNum == ENTITYNUM_WORLD
					&& !( tr.surfaceFlags & ( SURF_SKY | SURF_NOIMPACT ) ) && Distance( tr.endpos, target ) < 96
					&& tr.plane.normal[2] >= 0.7f ) {		// the floor itself: a wall would leave us hanging under the lip
					RmHookStart( bs, RM_HOOKUSE_RECOVER, angles, 1600 );
					t->recoverHooked = qtrue;
					RmSetViewAngles( ps, ucmd, angles );		// the aim frame (the hook code ran already)
					ucmd->buttons &= ~( BUTTON_ATTACK | BUTTON_ALTATTACK );
					how = "hooks back";
				}
			}
			if ( how && Q_stricmp( how, "steers back" ) ) {
				RmAct( bs->client, RM_ACT_RECOVERMOVE );
			}
			if ( BotTrickDebug() || trap_Cvar_VariableIntegerValue( "g_debugAlt" ) ) {
				G_LogPrintf( "Move: %i over a pit (from %.0f %.0f %.0f, %.0f below, air %i ms, vel %.0f %.0f %.0f): %s\n",
					bs->client, t->safePos[0], t->safePos[1], t->safePos[2], t->safePos[2] - ps->origin[2],
					level.time - t->groundTime, ps->velocity[0], ps->velocity[1], ps->velocity[2],
					how ? how : "nothing lands, steers back" );
			}
		}
	}

	if ( level.time >= t->recoverUntil ) {
		return qfalse;
	}
	RmMoveAlong( ps, ucmd, t->recoverDir );
	if ( level.time < bs->rmDashHoldUntil ) {
		ucmd->buttons |= BUTTON_DASH;
		ucmd->forwardmove = bs->rmDashFwd;
		ucmd->rightmove = bs->rmDashRight;
	}
	if ( t->recoverJumpAt ) {
		if ( level.time < t->recoverJumpAt ) {
			ucmd->upmove = 0;
		} else if ( level.time < t->recoverJumpAt + RM_PRESS_HOLD ) {
			ucmd->upmove = 127;
		} else {
			t->recoverJumpAt = 0;
		}
	} else if ( ucmd->upmove < 0 ) {
		ucmd->upmove = 0;		// no ground pound over a pit
	}
	return qtrue;
}

/*
==================
BotRearmAvoidLedge / BotRearmAvoidSpots

Ledges the brake stopped us at are avoid spots for the route planner for a while (botlib
clears the spots every think: BotCheckSnapshot adds ours back through BotRearmAvoidSpots)
==================
*/
#define RM_LEDGE_SPOTS			4
#define RM_LEDGE_RADIUS			80
#define RM_LEDGE_TIME			12000

static vec3_t	rmLedgeSpot[MAX_CLIENTS][RM_LEDGE_SPOTS];
static int		rmLedgeUntil[MAX_CLIENTS][RM_LEDGE_SPOTS];

static void BotRearmAvoidLedge( bot_state_t *bs, const vec3_t spot ) {
	int		i, oldest = 0;

	for ( i = 0 ; i < RM_LEDGE_SPOTS ; i++ ) {
		if ( rmLedgeUntil[bs->client][i] > level.time && Distance( rmLedgeSpot[bs->client][i], spot ) < RM_LEDGE_RADIUS ) {
			rmLedgeUntil[bs->client][i] = level.time + RM_LEDGE_TIME;
			return;
		}
		if ( rmLedgeUntil[bs->client][i] < rmLedgeUntil[bs->client][oldest] ) {
			oldest = i;
		}
	}
	VectorCopy( spot, rmLedgeSpot[bs->client][oldest] );
	rmLedgeUntil[bs->client][oldest] = level.time + RM_LEDGE_TIME;
	trap_BotAddAvoidSpot( bs->ms, rmLedgeSpot[bs->client][oldest], RM_LEDGE_RADIUS, AVOID_ALWAYS );
}

void BotRearmAvoidSpots( bot_state_t *bs ) {
	int		i;

	if ( bs->client < 0 || bs->client >= MAX_CLIENTS ) {
		return;
	}
	for ( i = 0 ; i < RM_LEDGE_SPOTS ; i++ ) {
		if ( rmLedgeUntil[bs->client][i] > level.time ) {
			trap_BotAddAvoidSpot( bs->ms, rmLedgeSpot[bs->client][i], RM_LEDGE_RADIUS, AVOID_ALWAYS );
		}
	}
}

/*
==================
BotRearmEdgeBrake

On the ground, about to run off a ledge (the stock route, a strafe, a slide): if the arc off it
comes down in a pit, and a jump the stock AI is pressing wouldn't clear it either, don't go.
One trace a frame while moving; the arc is only flown at an actual ledge.
==================
*/
static qboolean BotRearmEdgeBrake( bot_state_t *bs, playerState_t *ps, usercmd_t *ucmd, float hspeed ) {
	gentity_t	*ent = &g_entities[bs->client];
	vec3_t		flat, ahead, end, wish, vel;
	trace_t		tr;
	float		look;

	if ( hspeed < 80 || level.time < bs->rmDashHoldUntil || rmTraces < 2 ) {
		return qfalse;
	}
	VectorSet( flat, ps->velocity[0], ps->velocity[1], 0 );
	VectorNormalize( flat );
	// where we'll be in about 0.15 s (plus a half box): floor under it?
	look = hspeed * 0.15f + 20;
	if ( RM_FLAGS2( ps ) & RMS2_SLIDING ) {
		look = hspeed * 0.45f + 40;		// a slide hardly brakes: see the ledge coming much sooner
	}
	VectorMA( ps->origin, look, flat, ahead );
	VectorCopy( ahead, end );
	end[2] -= 72;
	RmTrace( &tr, ahead, NULL, NULL, end, bs->client, MASK_PLAYERSOLID );
	if ( tr.startsolid || tr.fraction < 1.0f ) {
		return qfalse;		// a wall or floor ahead: no ledge
	}
	// a ledge: fly the walk off (and the jump, if the stock AI is jumping) with its own steering
	RmWishDir( ps, ucmd, wish );
	VectorCopy( ps->velocity, vel );
	vel[2] = ucmd->upmove > 0 ? RM_JUMP_VELOCITY : 0;
	if ( RmPredictLanding( ent, ps, ps->origin, vel, ( wish[0] || wish[1] ) ? wish : NULL, ps->origin[2] ) ) {
		return qfalse;
	}
	if ( ucmd->upmove <= 0 && !( ps->pm_flags & PMF_JUMP_HELD ) ) {
		// walking off falls short, but a jump from the lip makes it: jump the gap
		vel[2] = RM_JUMP_VELOCITY;
		if ( RmPredictLanding( ent, ps, ps->origin, vel, ( wish[0] || wish[1] ) ? wish : NULL, ps->origin[2] ) ) {
			ucmd->upmove = 127;
			if ( BotTrickDebug() || trap_Cvar_VariableIntegerValue( "g_debugAlt" ) > 1 ) {
				G_LogPrintf( "Move: %i jumps a ledge it would have walked off (%.0f ups)\n", bs->client, hspeed );
			}
			return qtrue;
		}
	}
	// brake: back the way we came, no jump, no slide; and route around this ledge for a while
	VectorNegate( flat, wish );
	RmMoveAlong( ps, ucmd, wish );
	ucmd->upmove = 0;
	RmAct( bs->client, RM_ACT_EDGE );
	BotRearmAvoidLedge( bs, ahead );
	if ( BotTrickDebug() || trap_Cvar_VariableIntegerValue( "g_debugAlt" ) > 1 ) {
		G_LogPrintf( "Move: %i brakes at a ledge (%.0f ups)\n", bs->client, hspeed );
	}
	return qtrue;
}

/*
==================
BotRearmAirPressCheck

A fresh jump press in the air is an air jump, or a wall jump when a wall is beside us (pmove
picks). The stock AI presses jump in the air by itself, and a wall kick throws us off the
wall: if what the press would do comes down in a pit, don't press.
==================
*/
static void BotRearmAirPressCheck( bot_state_t *bs, playerState_t *ps, usercmd_t *ucmd ) {
	static const float	dirs[8][2] = {
		{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
		{ 0.7071f, 0.7071f }, { 0.7071f, -0.7071f }, { -0.7071f, 0.7071f }, { -0.7071f, -0.7071f }
	};
	gentity_t	*ent = &g_entities[bs->client];
	rmTrack_t	*t = &rmTrack[bs->client];
	trace_t		tr;
	vec3_t		vel, end, normal, wish;
	float		best, into, reach;
	int			i, rmflags = ps->stats[STAT_RMFLAGS];
	qboolean	wall = qfalse;

	if ( ucmd->upmove < 10 || ( ps->pm_flags & ( PMF_JUMP_HELD | PMF_RESPAWNED ) ) || ( RM_FLAGS2( ps ) & RMS2_SLAMMING )
		|| t->padAir || t->recoverJumpAt ) {
		return;
	}
	if ( ( rmflags & RMF_JUMPASSIST ) && ps->stats[STAT_JUMPTIMER] > 0 ) {
		return;		// coyote time: still a ground jump
	}
	VectorCopy( ps->velocity, vel );
	if ( ( rmflags & RMF_WALLJUMP ) && ps->stats[STAT_WALLJUMP_TIME] <= 0 ) {
		// the same wall search as PM_FindWall
		reach = ( rmflags & RMS_WALLRUN ) ? WALLRUN_DIST : WALL_JUMP_DIST;
		best = 1.0f;
		for ( i = 0 ; i < 8 ; i++ ) {
			end[0] = ps->origin[0] + dirs[i][0] * reach;
			end[1] = ps->origin[1] + dirs[i][1] * reach;
			end[2] = ps->origin[2];
			trap_Trace( &tr, ps->origin, ent->r.mins, ent->r.maxs, end, bs->client, MASK_PLAYERSOLID );
			if ( tr.allsolid || tr.fraction >= best || fabs( tr.plane.normal[2] ) > 0.7f
				|| tr.entityNum < MAX_CLIENTS || ( tr.surfaceFlags & SURF_SKY ) ) {
				continue;
			}
			best = tr.fraction;
			VectorCopy( tr.plane.normal, normal );
			wall = qtrue;
		}
	}
	if ( wall ) {
		into = DotProduct( vel, normal );
		if ( into < 0 ) {
			VectorMA( vel, -into, normal, vel );
		}
		VectorMA( vel, WALL_JUMP_PUSH, normal, vel );
		vel[2] = WALL_JUMP_VELOCITY;
	} else if ( ( rmflags & RMF_DOUBLEJUMP ) && ( !( ps->pm_flags & PMF_DOUBLE_JUMPED )
		|| ( ( RM_FLAGS2( ps ) & RMP2_FEATHER ) && !( ps->pm_flags & PMF_EXTRA_JUMPED ) ) ) ) {
		vel[2] = ( vel[2] > 0 ? vel[2] * 0.5f : 0 )
			+ ( ( ps->pm_flags & PMF_DOUBLE_JUMPED ) ? FEATHER_JUMP_VELOCITY : DOUBLE_JUMP_VELOCITY );
	} else {
		return;		// the press does nothing up here
	}
	RmWishDir( ps, ucmd, wish );
	if ( !RmPredictLanding( ent, ps, ps->origin, vel, ( wish[0] || wish[1] ) ? wish : NULL, t->safePos[2] ) ) {
		ucmd->upmove = 0;
		if ( BotTrickDebug() || trap_Cvar_VariableIntegerValue( "g_debugAlt" ) > 1 ) {
			G_LogPrintf( "Move: %i holds a %s that would end in a pit\n", bs->client, wall ? "wall jump" : "air jump" );
		}
	}
}

/*
==================
BotRearmLedge

In a fight on the ground next to a pit (RM_EDGE_DIST beside us): stop moving toward it, and lean
away from it. Rockets, the earth element and air blasts throw players, and a ledge turns a hit into
a fall. Eight one-trace probes every RM_EDGE_CHECK msec.
==================
*/
static qboolean BotRearmLedge( bot_state_t *bs, playerState_t *ps, usercmd_t *ucmd ) {
	rmTrack_t	*t = &rmTrack[bs->client];
	vec3_t		dir, point, end, move;
	trace_t		tr;
	float		a, toward;
	int			i, edges = 0;

	if ( level.time >= t->edgeNext && rmTraces >= 10 ) {
		t->edgeNext = level.time + RM_EDGE_CHECK;
		VectorClear( t->edgeAway );
		for ( i = 0 ; i < 8 ; i++ ) {
			a = DEG2RAD( i * 45.0f );
			VectorSet( dir, cos( a ), sin( a ), 0 );
			VectorMA( ps->origin, RM_EDGE_DIST, dir, point );
			VectorCopy( point, end );
			end[2] -= RM_SAFE_DROP;
			if ( !RmTrace( &tr, point, NULL, NULL, end, ps->clientNum,
				MASK_PLAYERSOLID | CONTENTS_TRIGGER | CONTENTS_LAVA | CONTENTS_SLIME ) ) {
				break;
			}
			if ( tr.startsolid ) {
				continue;		// a wall there, not a pit
			}
			if ( tr.fraction >= 1.0f || ( tr.surfaceFlags & SURF_SKY ) || ( tr.contents & ( CONTENTS_LAVA | CONTENTS_SLIME ) )
				|| ( tr.entityNum >= 0 && tr.entityNum < ENTITYNUM_MAX_NORMAL && g_entities[tr.entityNum].classname
				&& !Q_stricmp( g_entities[tr.entityNum].classname, "trigger_hurt" ) ) ) {
				VectorSubtract( t->edgeAway, dir, t->edgeAway );
				edges++;
			}
		}
		if ( edges >= 7 || VectorNormalize( t->edgeAway ) < 0.001f ) {
			VectorClear( t->edgeAway );		// a pillar top or a beam: nowhere better nearby
		}
	}
	if ( !t->edgeAway[0] && !t->edgeAway[1] ) {
		return qfalse;
	}
	RmWishDir( ps, ucmd, move );
	toward = -DotProduct( move, t->edgeAway );
	if ( toward < -0.3f ) {
		return qfalse;		// already moving away
	}
	// keep the strafe across, drop the part toward the pit, and lean away from it
	if ( toward > 0 ) {
		VectorMA( move, toward, t->edgeAway, move );
	}
	VectorMA( move, 0.8f, t->edgeAway, move );
	if ( VectorNormalize( move ) < 0.001f ) {
		return qfalse;
	}
	RmMoveAlong( ps, ucmd, move );
	RmAct( bs->client, RM_ACT_EDGE );
	return qtrue;
}

/*
==================
BotRearmMovement

Dodge dash, double jump, grapple, wall run and jetpack on top of the stock bot's usercmd.
enemy / dist / visible come from BotRearmInput (enemy is NULL out of combat).
==================
*/
void BotRearmMovement( bot_state_t *bs, usercmd_t *ucmd, gentity_t *enemy, float dist, qboolean visible ) {
	gentity_t		*ent = &g_entities[bs->client];
	const rmPersona_t	*persona = BotPersona( bs );		// play style multipliers (all 1 for BALANCED)
	playerState_t	*ps;
	vec3_t			dir, angles, eye, target, side, end;
	trace_t			tr;
	float			skill, hspeed, chance, vertMul, slideMul, dashMul, reach;
	int				rmflags, flags2, trigger;
	qboolean		onGround, jetpack, hurt, canDash, canJump, release, wallrun, reserve;
	qboolean		tricks, magnet, mirage, adrenaline, dangerMove, blink, landed;
	qboolean		canHook, zipBusy, airJump, padAir;
	float			kit;
	int				missing, kiteFwd, kiteRight;

	if ( !ent->client ) {
		return;
	}
	ps = &ent->client->ps;
	if ( ps->pm_type != PM_NORMAL || ps->stats[STAT_HEALTH] <= 0 ) {
		bs->rmHookState = RM_HOOK_IDLE;
		bs->rmJumpState = RM_JUMP_IDLE;
		bs->rmDodgeAt = 0;
		bs->rmSlideState = RM_SLIDE_IDLE;
		bs->rmSlamUntil = 0;
		bs->rmJumpThird = 0;
		bs->rmDangerSeen = 0;
		bs->rmZipState = RM_ZIP_IDLE;
		return;
	}
	if ( enemy && !enemy->client ) {
		enemy = NULL;
	}
	if ( !enemy ) {
		visible = qfalse;
	}

	rmTraces = RM_TRACE_BUDGET;
	skill = trap_Characteristic_BFloat( bs->character, CHARACTERISTIC_AIM_SKILL, 0, 1 );
	rmflags = ps->stats[STAT_RMFLAGS];
	onGround = ps->groundEntityNum != ENTITYNUM_NONE;
	hspeed = sqrt( ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1] );
	jetpack = qfalse;
	wallrun = qfalse;
	dangerMove = qfalse;

	// v4 tricks and the perks / events that shape them
	tricks = BotTrickOn( BOT_TRICK_MOVES );
	flags2 = RM_FLAGS2( ps );
	// descent: only what this bot owns (the server clears the bits of locked moves per client)
	canHook = g_offhandHook.integer && G_RL_HookAllowed( ent->client );
	padAir = !onGround && rmTrack[bs->client].padAir;
	airJump = ( rmflags & RMF_DOUBLEJUMP ) != 0;
	magnet = tricks && canHook && G_HasPerk( ent, PERK_MAGNETHOOK );
	mirage = tricks && G_HasPerk( ent, PERK_MIRAGE );
	adrenaline = tricks && G_HasPerk( ent, PERK_ADRENALINE ) && level.time - bs->rmKillTime < RM_ADRENALINE_TIME;
	blink = tricks && ( flags2 & RMP2_BLINK );
	landed = onGround && !bs->rmOnGroundLast;
	missing = !( rmflags & RMF_DASH ) + !airJump + !( rmflags & RMF_WALLRUN ) + !( flags2 & RMF2_SLIDE )
		+ !( flags2 & RMF2_SLAM ) + !( flags2 & RMF2_ZIPLINE ) + !canHook;
	kit = ( level.rmMode == RM_MODE_ROGUELIKE ) ? 1.0f + 0.08f * missing : 1.0f;
	vertMul = 1.0f;		// low gravity / jetpacks: go vertical
	slideMul = 1.0f;	// ice: slide more ...
	dashMul = 1.0f;		// ... and dash less
	if ( tricks ) {
		if ( level.mutator == MUT_LOW_GRAVITY || level.mutator == MUT_JETPACKS ) {
			vertMul = 1.8f;
		}
		if ( flags2 & RMF2_ICE ) {
			slideMul = 2.0f;
			dashMul = 0.5f;
		}
		if ( G_HasPerk( ent, PERK_PYROMANIAC ) ) {
			dashMul *= 1.3f;	// the dashes leave fire
		}
		if ( adrenaline ) {
			dashMul *= 1.6f;
		}
	}

	// did we just get hit? (health above max ticks down 1 a second, so ignore tiny drops)
	hurt = ps->damageEvent != bs->rmLastDamageEvent || ps->stats[STAT_HEALTH] < bs->rmLastHealth - 2;
	bs->rmLastDamageEvent = ps->damageEvent;
	bs->rmLastHealth = ps->stats[STAT_HEALTH];

	// --- jetpack: climb towards enemies above us, never drop into a pit, hop around otherwise ----
#ifndef MISSIONPACK
	if ( ps->powerups[PW_JETPACK] ) {
		jetpack = qtrue;
		if ( ( enemy && enemy->r.currentOrigin[2] > ps->origin[2] + 48 ) || !RmGroundBelow( ps, 700 )
			|| ( ( level.time / 700 ) % 3 == 0 && RmGroundBelow( ps, 200 ) ) ) {
			ucmd->upmove = 127;
		}
	}
#endif

	// --- offhand grapple -----------------------------------------------------------------------
	if ( bs->rmHookState == RM_HOOK_AIM ) {
		if ( level.time - bs->rmHookStart > 300 || !canHook || ( ps->loopSound & RMZ_ZIPPING ) ) {
			bs->rmHookState = RM_HOOK_IDLE;		// stale (we died or missed frames)
		} else {
			// the view turned last frame: the server fires along it on this press
			bs->rmHookState = RM_HOOK_HOLD;
		}
	}
	if ( bs->rmHookState == RM_HOOK_HOLD ) {
		release = ( level.time >= bs->rmHookHoldUntil && !RmHookStillPulling( bs, ps, onGround ) )
			|| !canHook || ( ps->loopSound & RMZ_ZIPPING );
		if ( level.time - bs->rmHookStart > 150 && !ent->client->hook ) {
			release = qtrue;		// freed (sky, no-impact surface)
		}
		if ( level.time - bs->rmHookStart > RM_HOOK_MISS_TIME && !( ps->pm_flags & PMF_GRAPPLE_PULL ) ) {
			release = qtrue;		// still flying: missed
		}
		if ( bs->rmHookUse == RM_HOOKUSE_OFFENSE && ( !enemy || !visible || dist < RM_HOOK_CLOSE ) ) {
			release = qtrue;
		}
		if ( bs->rmHookUse == RM_HOOKUSE_YANK && ( !enemy || !visible || dist < 110 ) ) {
			release = qtrue;
		}
		if ( bs->rmHookUse == RM_HOOKUSE_RECOVER && onGround && level.time - bs->rmHookStart > 250 ) {
			release = qtrue;		// back on the floor
		}
		if ( release ) {
			bs->rmHookState = RM_HOOK_IDLE;
			bs->rmHookReady = level.time + ( bs->rmHookUse == RM_HOOKUSE_ESCAPE ? 1500 : 2500 ) + random() * 1500;
		} else {
			ucmd->buttons |= BUTTON_HOOK;
			RmAct( bs->client, RM_ACT_HOOK );
			if ( level.time - bs->rmHookStart < 150 ) {
				RmSetViewAngles( ps, ucmd, bs->rmHookAngles );		// the fire frame: keep the view
			}
		}
	}

	if ( bs->rmHookState == RM_HOOK_IDLE && canHook && !padAir && level.time >= bs->rmHookReady && bs->rmZipState == RM_ZIP_IDLE
		&& ent->waterlevel <= 1 && !RmFlagRunHome( bs, ps ) ) {
		if ( enemy && dist < RM_ESCAPE_DIST && ps->stats[STAT_HEALTH] < RM_ESCAPE_HEALTH && rmTraces >= 2 ) {
			// escape: hook away from a close enemy when hurt
			if ( random() < 0.4f + 0.5f * skill && RmFindEscape( ps, enemy, angles ) ) {
				RmHookStart( bs, RM_HOOKUSE_ESCAPE, angles, 700 + random() * 400 );
			} else {
				bs->rmHookReady = level.time + 600;
			}
		} else if ( magnet && enemy && visible && dist > 250 && dist < RM_HOOK_RANGE - 50 && persona->hookOffense >= 0.5f
			&& ( RmCloseRangeWeapon( ps->weapon ) || ps->weapon == WP_ROCKET_LAUNCHER ) ) {
			// magnet hook: hook the enemy itself and yank them in to our gun
			if ( random() < ( 0.3f + 0.5f * skill ) * persona->hookOffense ) {
				VectorMA( enemy->r.currentOrigin, skill * dist / RM_HOOK_SPEED, enemy->client->ps.velocity, target );
				VectorCopy( ps->origin, eye );
				eye[2] += ps->viewheight;
				VectorSubtract( target, eye, dir );
				vectoangles( dir, angles );
				RmHookStart( bs, RM_HOOKUSE_YANK, angles, 500 + (int)( random() * 300 ) );
				if ( BotTrickDebug() ) {
					G_Printf( "bot_tricks: %i magnet hooks %i at %.0f\n", bs->client, bs->enemy, dist );
				}
			} else {
				bs->rmHookReady = level.time + 700;
			}
		} else if ( enemy && visible && dist > RM_HOOK_FAR && RmCloseRangeWeapon( ps->weapon ) ) {
			// offense: pull ourselves to a far enemy, leading it by skill
			// the pull carries us past them: floor under them and beyond them
			VectorSubtract( enemy->r.currentOrigin, ps->origin, dir );
			dir[2] = 0;
			VectorNormalize( dir );
			VectorMA( enemy->r.currentOrigin, 220, dir, end );
			if ( random() < ( 0.25f + 0.55f * skill ) * persona->hookOffense
				&& ( enemy->client->ps.groundEntityNum != ENTITYNUM_NONE
				|| RmSafeGround( bs->client, enemy->r.currentOrigin, enemy->r.currentOrigin[2] - RM_SAFE_DROP ) )
				&& RmSafeGround( bs->client, end, enemy->r.currentOrigin[2] - RM_SAFE_DROP ) ) {
				VectorMA( enemy->r.currentOrigin, skill * dist / RM_HOOK_SPEED, enemy->client->ps.velocity, target );
				VectorCopy( ps->origin, eye );
				eye[2] += ps->viewheight;
				VectorSubtract( target, eye, dir );
				vectoangles( dir, angles );
				RmHookStart( bs, RM_HOOKUSE_OFFENSE, angles, RM_HOOK_OFFENSE_TIME );
			} else {
				bs->rmHookReady = level.time + 500;
			}
		} else if ( !enemy && level.time >= bs->rmHookTravNext && hspeed > RM_RUN_SPEED && rmTraces >= 2 ) {
			// traversal: grab something ahead and up along the way we're running
			bs->rmHookTravNext = level.time + 6000 + random() * 6000;
			if ( random() < ( 0.4f + 0.5f * skill ) * persona->hookTraverse ) {
				VectorSet( dir, ps->velocity[0], ps->velocity[1], 0 );
				vectoangles( dir, angles );
				angles[PITCH] = -20 - random() * 15;
				angles[ROLL] = 0;
				if ( RmHookPoint( ps, angles, RM_HOOK_MIN_DIST ) ) {
					RmHookStart( bs, RM_HOOKUSE_TRAVERSE, angles, 500 + random() * 700 );
				}
			}
		}
	}

	if ( bs->rmHookState == RM_HOOK_AIM ) {
		// frame N: turn the view; the hook fires along it next frame
		RmSetViewAngles( ps, ucmd, bs->rmHookAngles );
		if ( bs->rmHookUse != RM_HOOKUSE_OFFENSE && bs->rmHookUse != RM_HOOKUSE_YANK ) {
			ucmd->buttons &= ~( BUTTON_ATTACK | BUTTON_ALTATTACK );		// don't shoot at the wall
		}
	}

	// --- zip line ------------------------------------------------------------------------------
	zipBusy = BotRearmZip( bs, ps, ucmd, enemy, dist, visible, skill, hspeed, jetpack );

	// personality: keep the preferred fighting distance instead of closing in
	kiteFwd = ucmd->forwardmove;
	kiteRight = ucmd->rightmove;
	BotPersonaSpacing( bs, ps, ucmd, enemy, dist, visible && bs->rmHookState == RM_HOOK_IDLE );
	if ( ucmd->forwardmove != kiteFwd || ucmd->rightmove != kiteRight ) {
		RmAct( bs->client, RM_ACT_KITE );
	}
	// don't fight on the lip of a pit: the next rocket throws us in
	if ( enemy && visible && onGround && !zipBusy && bs->rmHookState == RM_HOOK_IDLE && bs->rmSlideState == RM_SLIDE_IDLE ) {
		BotRearmLedge( bs, ps, ucmd );
	}

	// --- reactive dodge ------------------------------------------------------------------------
	canDash = ( rmflags & RMF_DASH ) && DASH_READY( ps ) && ent->waterlevel <= 1 && bs->rmHookState == RM_HOOK_IDLE && !zipBusy
		&& !padAir;		// a jump pad's arc is aimed: leave it alone
	canJump = !zipBusy && !jetpack && ent->waterlevel <= 1 && onGround
		&& bs->rmJumpState == RM_JUMP_IDLE && bs->rmHookState == RM_HOOK_IDLE && bs->rmSlideState == RM_SLIDE_IDLE;

	if ( bs->rmDodgeAt && level.time > bs->rmDodgeAt + 500 ) {
		bs->rmDodgeAt = 0;		// stale
	}
	if ( !bs->rmDodgeAt && level.time >= bs->rmNextDodgeCheck && ( canDash || canJump ) ) {
		trigger = RM_DODGE_NONE;
		if ( RmIncomingMissile( bs, ps, dir ) ) {
			trigger = RM_DODGE_MISSILE;
		} else if ( hurt ) {
			trigger = RM_DODGE_HIT;
		} else if ( visible && ( enemy->s.eFlags & EF_FIRING ) && RmEnemyAiming( ps, enemy ) ) {
			trigger = RM_DODGE_AIM;
		}
		if ( trigger != RM_DODGE_NONE ) {
			if ( trigger == RM_DODGE_MISSILE ) {
				chance = 0.35f + 0.6f * skill;
			} else {
				RmSideStepDir( ps, ucmd, enemy, dir );
				chance = ( trigger == RM_DODGE_HIT ) ? 0.15f + 0.3f * skill : 0.1f + 0.3f * skill;
			}
			chance *= persona->dodge * kit;
			if ( tricks ) {
				chance *= dashMul;
				if ( mirage && enemy && trigger != RM_DODGE_MISSILE && ps->stats[STAT_HEALTH] < 60 ) {
					// mirage: dash away and vanish out of a losing fight
					VectorSubtract( ps->origin, enemy->r.currentOrigin, dir );
					dir[2] = 0;
					VectorNormalize( dir );
					RmSideStepDir( ps, ucmd, enemy, side );
					VectorAdd( dir, side, dir );
					VectorNormalize( dir );
					chance = 0.5f + 0.4f * skill;
				}
			}
			// down to the last charge: better players keep it for rockets
			reserve = trigger != RM_DODGE_MISSILE && ps->stats[STAT_DASH_TIME] > 0 && random() < 0.2f + 0.6f * skill
				&& !adrenaline && !( mirage && ps->stats[STAT_HEALTH] < 60 );

			if ( tricks && trigger != RM_DODGE_MISSILE && ( flags2 & RMF2_SLIDE ) && onGround && hspeed >= SLIDE_MIN_SPEED
				&& bs->rmSlideState == RM_SLIDE_IDLE && bs->rmJumpState == RM_JUMP_IDLE && !( flags2 & RMS2_CROUCH_HELD )
				&& random() < 0.3f * persona->slide * slideMul ) {
				// duck under it: a slide shrinks the target and keeps the speed
				RmSlideStart( bs, ps, enemy, dist, skill, (int)( 350 - 250 * skill ), "dodge" );
				bs->rmNextDodgeCheck = level.time + 900;
			} else if ( random() >= chance ) {
				bs->rmNextDodgeCheck = level.time + 250 + random() * 350;
			} else if ( canDash && !reserve ) {
				// react after a human-ish delay
				bs->rmDodgeAt = level.time + (int)( 350 - 250 * skill );
				VectorCopy( dir, bs->rmDodgeDir );
				if ( trap_Cvar_VariableIntegerValue( "g_debugAlt" ) > 1 ) {
					G_Printf( "sched %i at %i now %i\n", bs->client, bs->rmDodgeAt, level.time );
				}
			} else if ( canJump && ( trigger == RM_DODGE_MISSILE || ( enemy && RmSplashWeapon( enemy->client->ps.weapon ) ) )
				&& ( hspeed < 100 || RmSafePath( ps, ps->velocity, RM_JUMP_REACH ) ) ) {
				// no dash to spare: jump over the splash
				bs->rmJumpState = RM_JUMP_PRESS;
				bs->rmJumpAt = level.time + (int)( 350 - 250 * skill );
				bs->rmNextDodgeCheck = level.time + 800;
			} else {
				bs->rmNextDodgeCheck = level.time + 600 + random() * 400;
			}
		}
	}
	if ( level.time < bs->rmDashHoldUntil ) {
		ucmd->buttons |= BUTTON_DASH;
		ucmd->forwardmove = bs->rmDashFwd;
		ucmd->rightmove = bs->rmDashRight;
	}
	if ( bs->rmDodgeAt && level.time >= bs->rmDodgeAt ) {
		bs->rmDodgeAt = 0;
		if ( trap_Cvar_VariableIntegerValue( "g_debugAlt" ) > 1 ) {
			G_Printf( "exec %i canDash %i\n", bs->client, canDash );
		}
		if ( canDash ) {
			VectorCopy( bs->rmDodgeDir, dir );
			if ( !RmSafePath( ps, dir, RM_DASH_REACH ) ) {
				VectorNegate( dir, dir );
				if ( !RmSafePath( ps, dir, RM_DASH_REACH ) ) {
					VectorClear( dir );
				}
			}
			if ( trap_Cvar_VariableIntegerValue( "g_debugAlt" ) > 1 ) {
				G_Printf( "dash %i: dir %f %f traces %i\n", bs->client, dir[0], dir[1], rmTraces );
			}
			if ( dir[0] || dir[1] ) {
				RmMoveAlong( ps, ucmd, dir );
				ucmd->buttons |= BUTTON_DASH;
				// a one-frame press can be replaced before the server runs it: hold it briefly
				bs->rmDashHoldUntil = level.time + RM_PRESS_HOLD;
				bs->rmDashFwd = ucmd->forwardmove;
				bs->rmDashRight = ucmd->rightmove;
				bs->rmNextDodgeCheck = level.time + (int)( 500 - 300 * skill ) + random() * 400;
				RmAct( bs->client, RM_ACT_DASH );
			} else {
				bs->rmNextDodgeCheck = level.time + 300;
			}
		}
	}

	// --- v4: step out of danger spots (strike markers, fire, burning barrels, ground pounds) --
	if ( BotTrickOn( BOT_TRICK_DANGER ) && bs->rmHookState == RM_HOOK_IDLE && ent->waterlevel <= 1 && !zipBusy ) {
		dangerMove = RmDangerMove( bs, ps, ucmd, enemy, skill, hspeed, canDash && level.time >= bs->rmDashHoldUntil );
		if ( dangerMove ) {
			RmAct( bs->client, RM_ACT_DANGER );
		}
	} else {
		bs->rmDangerSeen = 0;
	}

	// --- v4: gap-closing / flanking dashes (blink perk, adrenaline, or a full dash bar) ------
	if ( tricks && enemy && visible && canDash && !dangerMove && !bs->rmDodgeAt && level.time >= bs->rmDashHoldUntil
		&& level.time >= bs->rmBlinkNext && bs->rmSlideState == RM_SLIDE_IDLE ) {
		bs->rmBlinkNext = level.time + 900 + (int)( random() * 900 );
		if ( blink || adrenaline || ps->stats[STAT_DASH_TIME] <= 0 ) {
			chance = ( blink ? 0.3f : 0.1f ) * persona->blink * ( 0.5f + 0.5f * skill );
			if ( adrenaline ) {
				chance += 0.25f;
			}
			if ( flags2 & RMF2_ICE ) {
				chance *= 0.5f;
			}
			VectorSubtract( enemy->r.currentOrigin, ps->origin, dir );
			dir[2] = 0;
			VectorNormalize( dir );
			reach = blink ? BLINK_DIST : RM_DASH_REACH;
			trigger = 0;
			if ( RmCloseRangeWeapon( ps->weapon ) && dist > RM_GAP_MIN && dist < RM_GAP_MAX ) {
				// in, a little off the line of fire
				RmRotateFlat( dir, ( random() < 0.5f ? 1 : -1 ) * ( 10 + random() * 25 ), dir );
				trigger = 1;
			} else if ( blink && dist < 300 ) {
				// around them: across and past
				RmRotateFlat( dir, ( random() < 0.5f ? 1 : -1 ) * ( 60 + random() * 40 ), dir );
				trigger = 2;
			}
			if ( trigger && random() < chance && RmSafePath( ps, dir, reach ) ) {
				RmStartDash( bs, ps, ucmd, dir );
				bs->rmNextDodgeCheck = level.time + 400;
				if ( BotTrickDebug() ) {
					G_Printf( "bot_tricks: %i %s %s at %.0f\n", bs->client, blink ? "blinks" : "dashes",
						trigger == 1 ? "in" : "around", dist );
				}
			}
		}
	}

	// --- wall run: hold forward along a wall beside us, kick off before it runs out ------------
	if ( ( rmflags & RMF_WALLRUN ) && !jetpack && !onGround && !padAir && ent->waterlevel <= 1 && bs->rmHookState == RM_HOOK_IDLE && !zipBusy ) {
		if ( rmflags & RMS_WALLRUN ) {
			wallrun = qtrue;
			RmAct( bs->client, RM_ACT_WALLRUN );
			ucmd->forwardmove = 127;
			ucmd->upmove = 0;
			bs->rmWallRunUntil = level.time + 150;
			if ( ps->stats[STAT_WALLRUN_TIME] >= WALLRUN_TIME - 200 && ( rmflags & ( RMF_WALLJUMP | RMF_DOUBLEJUMP ) )
				&& level.time - bs->rmWallKick > 600 && rmTraces >= 2 ) {
				bs->rmWallKick = level.time;
				// the kick goes away from the wall: check that side for floor
				VectorSet( dir, ps->velocity[0], ps->velocity[1], 0 );
				VectorNormalize( dir );
				side[0] = dir[1];		// right of travel
				side[1] = -dir[0];
				side[2] = 0;
				if ( !( rmflags & RMS_WALLRUN_LEFT ) ) {
					VectorNegate( side, side );
				}
				VectorAdd( dir, side, dir );
				if ( RmSafePath( ps, dir, RM_JUMP_REACH ) ) {
					ucmd->upmove = 127;
					RmAct( bs->client, RM_ACT_WALLKICK );
					bs->rmJumpState = RM_JUMP_DONE;
					bs->rmJumpThird = 0;
				}
			}
		} else if ( ucmd->forwardmove > 0 && hspeed >= WALLRUN_MIN_SPEED && ps->stats[STAT_WALLRUN_TIME] < WALLRUN_TIME ) {
			if ( bs->rmWallRunUntil > level.time ) {
				wallrun = qtrue;
				ucmd->forwardmove = 127;
			} else if ( ucmd->forwardmove >= 64 && level.time >= bs->rmWallCheck && rmTraces > 0 ) {
				// one side per check, the same box trace pmove uses
				bs->rmWallCheck = level.time + (int)( 100 / persona->wallrun );
				side[0] = ps->velocity[1] / hspeed;
				side[1] = -ps->velocity[0] / hspeed;
				side[2] = 0;
				if ( ( level.time / 100 ) & 1 ) {
					VectorNegate( side, side );
				}
				VectorMA( ps->origin, WALLRUN_DIST, side, end );
				RmTrace( &tr, ps->origin, ent->r.mins, ent->r.maxs, end, bs->client, MASK_PLAYERSOLID );
				if ( tr.fraction < 1.0f && !tr.allsolid && fabs( tr.plane.normal[2] ) < 0.3f
					&& tr.entityNum >= MAX_CLIENTS && !( tr.surfaceFlags & SURF_SKY )
					&& ( persona->wallrun >= 1.0f || random() < persona->wallrun ) ) {
					bs->rmWallRunUntil = level.time + 400;
					wallrun = qtrue;
					ucmd->forwardmove = 127;
					// a wall run starts only with jump still held from the takeoff
					if ( ps->pm_flags & PMF_JUMP_HELD ) {
						ucmd->upmove = 127;
					}
				}
			}
		}
	}

	// --- jumps and double jumps ----------------------------------------------------------------
	if ( jetpack || ent->waterlevel > 1 || zipBusy || padAir ) {
		bs->rmJumpState = RM_JUMP_IDLE;
	} else {
		canJump = onGround && bs->rmJumpState == RM_JUMP_IDLE && bs->rmHookState == RM_HOOK_IDLE && ucmd->upmove >= 0
			&& bs->rmSlideState == RM_SLIDE_IDLE && !dangerMove;

		if ( enemy && canJump && level.time >= bs->rmCombatJumpNext ) {
			// fights: hop over splash damage, now and then at random
			bs->rmCombatJumpNext = level.time + 1400 + random() * 1600;
			if ( visible && dist < 1000 && RmSplashWeapon( enemy->client->ps.weapon ) ) {
				chance = 0.2f + 0.35f * skill;
			} else {
				chance = 0.04f + 0.1f * skill;
			}
			chance *= persona->jump * vertMul * kit;
			if ( random() < chance && ( hspeed < 100 || RmSafePath( ps, ps->velocity, RM_JUMP_REACH ) ) ) {
				bs->rmJumpState = RM_JUMP_PRESS;
				bs->rmJumpAt = level.time;
			}
		} else if ( !enemy && canJump && airJump && hspeed > RM_RUN_SPEED && ucmd->forwardmove > 0 && ucmd->upmove == 0
			&& level.time >= bs->rmBunnyNext ) {
			// out of combat: a jump + double jump forward for flair
			bs->rmBunnyNext = level.time + 2000 + random() * 3000;
			if ( RmSafePath( ps, ps->velocity, RM_JUMP_REACH ) ) {
				bs->rmJumpState = RM_JUMP_PRESS;
				bs->rmJumpAt = level.time;
			}
		}

		// the stock AI jumped by itself: sometimes add the air jump in fights
		if ( bs->rmJumpState == RM_JUMP_IDLE && airJump && enemy && bs->rmOnGroundLast && !onGround
			&& ps->velocity[2] > 200 && ps->velocity[2] < 340 && bs->rmHookState == RM_HOOK_IDLE && !wallrun
			&& random() < ( 0.25f + 0.45f * skill ) * persona->jump * vertMul && RmSafePath( ps, ps->velocity, RM_JUMP_REACH ) ) {
			bs->rmJumpState = RM_JUMP_APEX;
			ucmd->upmove = 0;			// release, so the air jump can trigger
		} else {
			switch ( bs->rmJumpState ) {
			case RM_JUMP_PRESS:
				if ( level.time < bs->rmJumpAt ) {
					break;
				}
				if ( onGround ) {
					if ( level.time > bs->rmJumpAt + 400 ) {
						bs->rmJumpState = RM_JUMP_IDLE;		// couldn't get off the ground
					} else {
						ucmd->upmove = 127;
						RmAct( bs->client, RM_ACT_JUMP );
					}
				} else if ( airJump ) {
					bs->rmJumpState = RM_JUMP_APEX;
					ucmd->upmove = 0;		// release
				} else {
					bs->rmJumpState = RM_JUMP_DONE;		// no air jump owned: just the hop
					bs->rmJumpAt = level.time;
					bs->rmJumpThird = 0;
				}
				break;
			case RM_JUMP_APEX:
				if ( onGround ) {
					bs->rmJumpState = RM_JUMP_IDLE;
				} else if ( wallrun ) {
					ucmd->upmove = 0;		// a press now would kick off the wall
				} else if ( ps->velocity[2] < RM_APEX_VZ && !RmAirJumpLands( bs, ps, ucmd, DOUBLE_JUMP_VELOCITY ) ) {
					bs->rmJumpState = RM_JUMP_DONE;		// it would carry us over a pit: keep it
					bs->rmJumpAt = 0;
					bs->rmJumpThird = 0;
				} else if ( ps->velocity[2] < RM_APEX_VZ ) {
					ucmd->upmove = 127;
					RmAct( bs->client, RM_ACT_AIRJUMP );
					bs->rmJumpState = RM_JUMP_DONE;
					bs->rmJumpAt = level.time + RM_PRESS_HOLD;	// hold it long enough for the server to see
					// featherweight: plan the second air jump (a triple jump)
					bs->rmJumpThird = 0;
					if ( tricks && ( flags2 & RMP2_FEATHER ) && ( enemy || random() < 0.5f )
						&& random() < ( 0.45f + 0.4f * skill ) * persona->jump * vertMul
						&& RmSafePath( ps, ps->velocity, RM_JUMP_REACH ) ) {
						bs->rmJumpThird = 1;
					}
				} else {
					ucmd->upmove = 0;
				}
				break;
			case RM_JUMP_DONE:
				if ( onGround ) {
					bs->rmJumpState = RM_JUMP_IDLE;
					bs->rmJumpThird = 0;
				} else if ( level.time < bs->rmJumpAt ) {
					ucmd->upmove = 127;
				} else if ( bs->rmJumpThird == 1 || bs->rmJumpThird == 2 ) {
					// featherweight: release, then the third jump near the next apex
					if ( bs->rmJumpThird == 2 && !wallrun && ps->velocity[2] < RM_APEX_VZ
						&& !( RM_FLAGS2( ps ) & RMS2_SLAMMING ) && RmAirJumpLands( bs, ps, ucmd, FEATHER_JUMP_VELOCITY ) ) {
						ucmd->upmove = 127;
						RmAct( bs->client, RM_ACT_AIRJUMP );
						bs->rmJumpThird = 3;
						bs->rmJumpAt = level.time + RM_PRESS_HOLD;
						if ( BotTrickDebug() ) {
							G_Printf( "bot_tricks: %i triple jump\n", bs->client );
						}
					} else {
						ucmd->upmove = 0;
						bs->rmJumpThird = 2;
					}
				}
				break;
			default:
				break;
			}
		}
	}

	if ( tricks && !zipBusy ) {
		BotRearmSlide( bs, ps, ucmd, enemy, dist, visible, skill, hspeed, onGround, landed, jetpack, slideMul, dangerMove );
		BotRearmSlam( bs, ps, ucmd, enemy, visible, skill, onGround, wallrun, jetpack, vertMul );
	} else {
		bs->rmSlideState = RM_SLIDE_IDLE;
		bs->rmSlamUntil = 0;
	}

	// --- about to run off a ledge into a pit: jump it if that lands, else stop -----------------
	if ( onGround && !zipBusy && bs->rmHookState == RM_HOOK_IDLE && ent->waterlevel <= 1 ) {
		BotRearmEdgeBrake( bs, ps, ucmd, hspeed );
	} else if ( !onGround && !zipBusy && !jetpack && ent->waterlevel <= 1 ) {
		BotRearmAirPressCheck( bs, ps, ucmd );
	}

	// --- falling into a pit: steer, dash, air jump or hook back to the floor we left ----------
	if ( BotRearmAirRecover( bs, ps, ucmd, skill, canHook, zipBusy, jetpack ) ) {
		bs->rmSlamUntil = 0;
		bs->rmSlamNext = level.time + 1000;
	}
	bs->rmOnGroundLast = onGround;
}
