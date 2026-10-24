"""Movement tech: double jump, wall jump, air dash (shared pmove, predicted on the client)."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from patch import sub

G = pathlib.Path(__file__).resolve().parents[2] / 'ioq3' / 'code'
g = lambda f: str(G / 'game' / f)
cg = lambda f: str(G / 'cgame' / f)

# --- shared definitions -------------------------------------------------------
sub(g('bg_public.h'), '#define\tPMF_ALL_TIMES\t(PMF_TIME_WATERJUMP|PMF_TIME_LAND|PMF_TIME_KNOCKBACK)\n', '''#define\tPMF_ALL_TIMES\t(PMF_TIME_WATERJUMP|PMF_TIME_LAND|PMF_TIME_KNOCKBACK)

// rearmed: movement tech state (the unused pm_flags bits)
#define PMF_DOUBLE_JUMPED	4		// used the air jump since leaving the ground
#define PMF_DASH_HELD		128		// dash button must be released before dashing again
#define PMF_AIR_DASHED		32768	// used the air dash since leaving the ground

// rearmed: extra usercmd buttons (+button12 .. +button14)
#define BUTTON_ALTATTACK	4096
#define BUTTON_HOOK			8192
#define BUTTON_DASH			16384

#define DOUBLE_JUMP_VELOCITY	280
#define WALL_JUMP_VELOCITY		300
#define WALL_JUMP_PUSH			330		// speed away from the wall
#define WALL_JUMP_DIST			14		// how close a wall must be
#define WALL_JUMP_COOLDOWN		350
#define DASH_SPEED				720
#define DASH_LIFT				120
#define DASH_COOLDOWN			1500
''')
sub(g('bg_public.h'), '\tSTAT_CLIENTS_READY,\t\t\t\t// bit mask of clients wishing to exit the intermission (FIXME: configstring?)\n',
    '\tSTAT_CLIENTS_READY,\t\t\t\t// bit mask of clients wishing to exit the intermission (FIXME: configstring?)\n'
    '\tSTAT_DASH_TIME,\t\t\t\t\t// msec until the dash is ready\n'
    '\tSTAT_WALLJUMP_TIME,\t\t\t\t// msec until the next wall jump\n')
sub(g('bg_public.h'), '\tEV_TAUNT_PATROL\n\n} entity_event_t;', '\tEV_TAUNT_PATROL,\n\n\tEV_DOUBLE_JUMP,\n\tEV_WALL_JUMP,\n\tEV_DASH\n\n} entity_event_t;')
sub(g('bg_misc.c'), '\t"EV_TAUNT_PATROL"\n', '\t"EV_TAUNT_PATROL",\n\t"EV_DOUBLE_JUMP",\n\t"EV_WALL_JUMP",\n\t"EV_DASH"\n')

# --- pmove ------------------------------------------------------------------
sub(g('bg_pmove.c'), '''/*
=============
PM_CheckWaterJump
=============
*/''', '''/*
=============
PM_FindWall

Looks for a wall close to the player's sides to kick off from
=============
*/
static qboolean PM_FindWall( vec3_t normal ) {
	static const float	dirs[8][2] = {
		{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
		{ 0.7071f, 0.7071f }, { 0.7071f, -0.7071f }, { -0.7071f, 0.7071f }, { -0.7071f, -0.7071f }
	};
	trace_t		trace;
	vec3_t		end;
	float		best;
	int			i;

	best = 1.0f;
	for ( i = 0 ; i < 8 ; i++ ) {
		end[0] = pm->ps->origin[0] + dirs[i][0] * WALL_JUMP_DIST;
		end[1] = pm->ps->origin[1] + dirs[i][1] * WALL_JUMP_DIST;
		end[2] = pm->ps->origin[2];
		pm->trace( &trace, pm->ps->origin, pm->mins, pm->maxs, end, pm->ps->clientNum, pm->tracemask );
		if ( trace.allsolid || trace.fraction >= best ) {
			continue;
		}
		// only mostly vertical world surfaces, never other players or the sky
		if ( fabs( trace.plane.normal[2] ) > 0.7f || trace.entityNum < MAX_CLIENTS
			|| ( trace.surfaceFlags & ( SURF_SKY | SURF_NOIMPACT ) ) ) {
			continue;
		}
		best = trace.fraction;
		VectorCopy( trace.plane.normal, normal );
	}
	return best < 1.0f;
}

/*
=============
PM_CheckAirJump

Wall jump when next to a wall, otherwise a single double jump per air time
=============
*/
static void PM_CheckAirJump( void ) {
	vec3_t	normal;
	float	into;

	if ( pm->ps->pm_type != PM_NORMAL || pm->waterlevel > 1 ) {
		return;
	}
	if ( pm->ps->pm_flags & ( PMF_RESPAWNED | PMF_JUMP_HELD | PMF_TIME_WATERJUMP ) ) {
		return;
	}
	if ( pm->cmd.upmove < 10 ) {
		return;
	}
	pm->ps->pm_flags |= PMF_JUMP_HELD;

	if ( pm->ps->stats[STAT_WALLJUMP_TIME] <= 0 && PM_FindWall( normal ) ) {
		// drop the velocity into the wall, then kick away from it
		into = DotProduct( pm->ps->velocity, normal );
		if ( into < 0 ) {
			VectorMA( pm->ps->velocity, -into, normal, pm->ps->velocity );
		}
		VectorMA( pm->ps->velocity, WALL_JUMP_PUSH, normal, pm->ps->velocity );
		pm->ps->velocity[2] = WALL_JUMP_VELOCITY;
		pm->ps->stats[STAT_WALLJUMP_TIME] = WALL_JUMP_COOLDOWN;
		PM_AddEvent( EV_WALL_JUMP );
		PM_ForceLegsAnim( LEGS_JUMP );
		return;
	}

	if ( pm->ps->pm_flags & PMF_DOUBLE_JUMPED ) {
		return;
	}
	pm->ps->pm_flags |= PMF_DOUBLE_JUMPED;
	if ( pm->ps->velocity[2] < 0 ) {
		pm->ps->velocity[2] = 0;
	}
	pm->ps->velocity[2] = pm->ps->velocity[2] * 0.5f + DOUBLE_JUMP_VELOCITY;
	PM_AddEvent( EV_DOUBLE_JUMP );
	if ( pm->cmd.forwardmove >= 0 ) {
		PM_ForceLegsAnim( LEGS_JUMP );
		pm->ps->pm_flags &= ~PMF_BACKWARDS_JUMP;
	} else {
		PM_ForceLegsAnim( LEGS_JUMPB );
		pm->ps->pm_flags |= PMF_BACKWARDS_JUMP;
	}
}

/*
=============
PM_CheckDash

Burst of horizontal speed in the movement direction (or view direction),
on the ground or once per air time
=============
*/
static void PM_CheckDash( void ) {
	vec3_t	angles, forward, right, dir;
	float	speed;

	if ( !( pm->cmd.buttons & BUTTON_DASH ) ) {
		pm->ps->pm_flags &= ~PMF_DASH_HELD;
		return;
	}
	if ( pm->ps->pm_flags & ( PMF_DASH_HELD | PMF_RESPAWNED ) ) {
		return;
	}
	pm->ps->pm_flags |= PMF_DASH_HELD;

	if ( pm->ps->pm_type != PM_NORMAL || pm->waterlevel > 1 || pm->ps->stats[STAT_DASH_TIME] > 0 ) {
		return;
	}
	if ( !pml.walking && ( pm->ps->pm_flags & PMF_AIR_DASHED ) ) {
		return;
	}

	VectorSet( angles, 0, pm->ps->viewangles[YAW], 0 );
	AngleVectors( angles, forward, right, NULL );
	VectorScale( forward, pm->cmd.forwardmove, dir );
	VectorMA( dir, pm->cmd.rightmove, right, dir );
	if ( VectorNormalize( dir ) == 0 ) {
		VectorCopy( forward, dir );
	}

	// never slower than the current horizontal speed
	speed = sqrt( pm->ps->velocity[0] * pm->ps->velocity[0] + pm->ps->velocity[1] * pm->ps->velocity[1] );
	if ( speed < DASH_SPEED ) {
		speed = DASH_SPEED;
	}
	pm->ps->velocity[0] = dir[0] * speed;
	pm->ps->velocity[1] = dir[1] * speed;
	if ( pm->ps->velocity[2] < DASH_LIFT ) {
		pm->ps->velocity[2] = DASH_LIFT;
	}

	if ( pml.walking ) {
		// hop off the ground so friction doesn't eat the dash
		pml.walking = qfalse;
		pml.groundPlane = qfalse;
		pm->ps->groundEntityNum = ENTITYNUM_NONE;
	} else {
		pm->ps->pm_flags |= PMF_AIR_DASHED;
	}
	pm->ps->stats[STAT_DASH_TIME] = DASH_COOLDOWN;
	PM_AddEvent( EV_DASH );
}

/*
=============
PM_CheckWaterJump
=============
*/''')

sub(g('bg_pmove.c'), '''	PM_Friction();

	fmove = pm->cmd.forwardmove;
	smove = pm->cmd.rightmove;

	cmd = pm->cmd;
	scale = PM_CmdScale( &cmd );

	// set the movementDir so clients can rotate the legs for strafing
	PM_SetMovementDir();

	// project moves down to flat plane
	pml.forward[2] = 0;
	pml.right[2] = 0;
	VectorNormalize (pml.forward);
	VectorNormalize (pml.right);

	for ( i = 0 ; i < 2 ; i++ ) {
		wishvel[i] = pml.forward[i]*fmove + pml.right[i]*smove;
	}
	wishvel[2] = 0;

	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize(wishdir);
	wishspeed *= scale;

	// not on ground, so little effect on velocity''', '''	PM_CheckAirJump();

	PM_Friction();

	fmove = pm->cmd.forwardmove;
	smove = pm->cmd.rightmove;

	cmd = pm->cmd;
	scale = PM_CmdScale( &cmd );

	// set the movementDir so clients can rotate the legs for strafing
	PM_SetMovementDir();

	// project moves down to flat plane
	pml.forward[2] = 0;
	pml.right[2] = 0;
	VectorNormalize (pml.forward);
	VectorNormalize (pml.right);

	for ( i = 0 ; i < 2 ; i++ ) {
		wishvel[i] = pml.forward[i]*fmove + pml.right[i]*smove;
	}
	wishvel[2] = 0;

	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize(wishdir);
	wishspeed *= scale;

	// not on ground, so little effect on velocity''')

sub(g('bg_pmove.c'), '''	if ( pm->ps->torsoTimer > 0 ) {
		pm->ps->torsoTimer -= pml.msec;
		if ( pm->ps->torsoTimer < 0 ) {
			pm->ps->torsoTimer = 0;
		}
	}
}''', '''	if ( pm->ps->torsoTimer > 0 ) {
		pm->ps->torsoTimer -= pml.msec;
		if ( pm->ps->torsoTimer < 0 ) {
			pm->ps->torsoTimer = 0;
		}
	}

	// movement tech cooldowns
	if ( pm->ps->stats[STAT_DASH_TIME] > 0 ) {
		pm->ps->stats[STAT_DASH_TIME] -= pml.msec;
		if ( pm->ps->stats[STAT_DASH_TIME] < 0 ) {
			pm->ps->stats[STAT_DASH_TIME] = 0;
		}
	}
	if ( pm->ps->stats[STAT_WALLJUMP_TIME] > 0 ) {
		pm->ps->stats[STAT_WALLJUMP_TIME] -= pml.msec;
		if ( pm->ps->stats[STAT_WALLJUMP_TIME] < 0 ) {
			pm->ps->stats[STAT_WALLJUMP_TIME] = 0;
		}
	}
}''')

sub(g('bg_pmove.c'), '''	PM_DropTimers();

#ifdef MISSIONPACK
	if ( pm->ps->powerups[PW_INVULNERABILITY] ) {''', '''	PM_DropTimers();

	// touching the ground (or swimming, or hanging on the grapple) restores the air moves
	if ( pml.walking || pm->waterlevel > 1 || ( pm->ps->pm_flags & PMF_GRAPPLE_PULL ) ) {
		pm->ps->pm_flags &= ~( PMF_DOUBLE_JUMPED | PMF_AIR_DASHED );
	}

	if ( !pm->ps->powerups[PW_FLIGHT] ) {
		PM_CheckDash();
	}

#ifdef MISSIONPACK
	if ( pm->ps->powerups[PW_INVULNERABILITY] ) {''')

# --- cgame: sounds for the new events -------------------------------------------
sub(cg('cg_local.h'), '\tsfxHandle_t\theadshotSound;\n', '\tsfxHandle_t\theadshotSound;\n\tsfxHandle_t\tdoubleJumpSound;\n\tsfxHandle_t\twallJumpSound;\n\tsfxHandle_t\tdashSound;\n')
sub(cg('cg_main.c'), '\tcgs.media.headshotSound = trap_S_RegisterSound("sound/rearmed/headshot.wav", qfalse);\n',
    '\tcgs.media.headshotSound = trap_S_RegisterSound("sound/rearmed/headshot.wav", qfalse);\n'
    '\tcgs.media.doubleJumpSound = trap_S_RegisterSound("sound/rearmed/doublejump.wav", qfalse);\n'
    '\tcgs.media.wallJumpSound = trap_S_RegisterSound("sound/rearmed/walljump.wav", qfalse);\n'
    '\tcgs.media.dashSound = trap_S_RegisterSound("sound/rearmed/dash.wav", qfalse);\n')
sub(cg('cg_event.c'), '''	case EV_JUMP:
		DEBUGNAME("EV_JUMP");''', '''	case EV_DOUBLE_JUMP:
		DEBUGNAME("EV_DOUBLE_JUMP");
		trap_S_StartSound( NULL, es->number, CHAN_BODY, cgs.media.doubleJumpSound );
		CG_MoveTechPuff( cent, 0 );
		break;
	case EV_WALL_JUMP:
		DEBUGNAME("EV_WALL_JUMP");
		trap_S_StartSound( NULL, es->number, CHAN_BODY, cgs.media.wallJumpSound );
		CG_MoveTechPuff( cent, 1 );
		break;
	case EV_DASH:
		DEBUGNAME("EV_DASH");
		trap_S_StartSound( NULL, es->number, CHAN_BODY, cgs.media.dashSound );
		CG_MoveTechPuff( cent, 2 );
		break;

	case EV_JUMP:
		DEBUGNAME("EV_JUMP");''')
print('movement ok')

sub(cg('cg_local.h'), 'void CG_SpawnEffect( vec3_t org );\n', 'void CG_SpawnEffect( vec3_t org );\nvoid CG_MoveTechPuff( centity_t *cent, int type );\n')
sub(cg('cg_effects.c'), '''localEntity_t *CG_SmokePuff( const vec3_t p, const vec3_t vel, ''', '''/*
==================
CG_MoveTechPuff

Air puffs for double jump (0), wall jump (1) and dash (2)
==================
*/
void CG_MoveTechPuff( centity_t *cent, int type ) {
	vec3_t	org, vel;
	float	ang, s, c;
	int		i;

	VectorCopy( cent->lerpOrigin, org );
	if ( cent->currentState.number == cg.snap->ps.clientNum ) {
		VectorCopy( cg.predictedPlayerState.origin, org );
	}
	org[2] -= ( type == 0 ) ? 22 : 8;

	for ( i = 0 ; i < 8 ; i++ ) {
		ang = i * ( M_PI * 2 / 8 );
		s = sin( ang );
		c = cos( ang );
		if ( type == 0 ) {
			VectorSet( vel, c * 90, s * 90, -40 );
		} else if ( type == 1 ) {
			VectorSet( vel, c * 60, s * 60, 30 );
		} else {
			VectorSet( vel, c * 40 - cent->currentState.pos.trDelta[0] * 0.15f,
				s * 40 - cent->currentState.pos.trDelta[1] * 0.15f, 10 );
		}
		CG_SmokePuff( org, vel, 10, 0.8f, 0.9f, 1.0f, 0.45f, 450, cg.time, 0, LEF_PUFF_DONT_SCALE, cgs.media.smokePuffShader );
	}
}

localEntity_t *CG_SmokePuff( const vec3_t p, const vec3_t vel, ''')
print('puff ok')
