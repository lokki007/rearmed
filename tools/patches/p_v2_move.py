"""v2: feature switches in stats[STAT_RMFLAGS]; movement fixes and upgrades
(grapple refresh exploit, dash charges, coyote time + jump buffer, wall run)."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from patch import sub

ROOT = pathlib.Path(__file__).resolve().parents[2] / 'ioq3'
g = lambda f: str(ROOT / 'code' / 'game' / f)

# --- bg_public.h ---
sub(g('bg_public.h'), '''// rearmed: movement tech state (the unused pm_flags bits)
#define PMF_DOUBLE_JUMPED	4		// used the air jump since leaving the ground
#define PMF_DASH_HELD		128		// dash button must be released before dashing again
#define PMF_AIR_DASHED		32768	// used the air dash since leaving the ground
''', '''// rearmed: movement tech state (the unused pm_flags bits)
#define PMF_DOUBLE_JUMPED	4		// used the air jump since leaving the ground
#define PMF_DASH_HELD		128		// dash button must be released before dashing again
#define PMF_AIR_DASHED		32768	// (unused since dash charges)

// rearmed: feature switches. The server copies its g_ cvars into stats[STAT_RMFLAGS]
// before every move, so client prediction always runs the same rules as the server.
#define RMF_DOUBLEJUMP		0x0001
#define RMF_WALLJUMP		0x0002
#define RMF_WALLRUN			0x0004
#define RMF_DASH			0x0008
#define RMF_JUMPASSIST		0x0010		// coyote time + jump buffering
#define RMF_ALTFIRE			0x0020
#define RMF_SHOTGUNKICK		0x0040
#define RMF_SPINUP			0x0080
#define RMF_CONFIG			0x00ff		// the bits the server owns
// pmove state kept in the same stat
#define RMS_WALLRUN			0x0100		// running along a wall
#define RMS_WALLRUN_LEFT	0x0200		// ... and the wall is on the left
#define RMS_HOOKREFRESH		0x0400		// this grapple pull already restored the air moves
''')
sub(g('bg_public.h'), '''#define DASH_SPEED				720
#define DASH_LIFT				120
#define DASH_COOLDOWN			1500
''', '''#define DASH_SPEED				720
#define DASH_LIFT				120
#define DASH_COOLDOWN			1500	// recharge time per dash charge
#define DASH_CHARGES			2
#define DASH_READY(ps)			( (ps)->stats[STAT_DASH_TIME] <= DASH_COOLDOWN * ( DASH_CHARGES - 1 ) )

#define COYOTE_TIME				120		// msec after walking off a ledge that a ground jump still works
#define JUMP_BUFFER_TIME		110		// a jump pressed this close to landing waits for the ground

#define WALLRUN_MIN_SPEED		260		// horizontal speed needed to start a wall run
#define WALLRUN_TIME			1600	// wall run budget per air time
#define WALLRUN_DIST			22
#define WALLRUN_GRAVITY			0.2f	// fraction of gravity while running
#define WALLRUN_MAX_FALL		120
''')
sub(g('bg_public.h'), '#define WALL_JUMP_DIST\t\t\t14\t\t// how close a wall must be\n',
    '#define WALL_JUMP_DIST\t\t\t22\t\t// how close a wall must be\n')
sub(g('bg_public.h'), '''	STAT_GGLEVEL,					// gun game ladder position
''', '''	STAT_GGLEVEL,					// gun game ladder position
	STAT_RMFLAGS,					// RMF_ feature switches from the server + RMS_ move state
	STAT_WALLRUN_TIME,				// msec of wall running this air time
	STAT_JUMPTIMER,					// coyote time left
''')

# --- bg_pmove.c ---
P = g('bg_pmove.c')
sub(P, '''/*
=============
PM_FindWall
''', '''#define RMF(x)	( pm->ps->stats[STAT_RMFLAGS] & (x) )

/*
=============
PM_FindWall
''')

# air jump: jump buffer, coyote jump, feature switches; only a real air jump holds the button
sub(P, '''	if ( pm->cmd.upmove < 10 ) {
		return;
	}
	pm->ps->pm_flags |= PMF_JUMP_HELD;

	if ( pm->ps->stats[STAT_WALLJUMP_TIME] <= 0 && PM_FindWall( normal ) ) {''', '''	if ( pm->cmd.upmove < 10 ) {
		return;
	}

	if ( RMF( RMF_JUMPASSIST ) ) {
		trace_t	trace;
		vec3_t	end;

		// walked off a ledge a moment ago: still a normal ground jump
		if ( pm->ps->stats[STAT_JUMPTIMER] > 0 ) {
			pm->ps->stats[STAT_JUMPTIMER] = 0;
			pm->ps->pm_flags |= PMF_JUMP_HELD;
			if ( pm->ps->velocity[2] < JUMP_VELOCITY ) {
				pm->ps->velocity[2] = JUMP_VELOCITY;
			}
			PM_AddEvent( EV_JUMP );
			PM_ForceLegsAnim( pm->cmd.forwardmove >= 0 ? LEGS_JUMP : LEGS_JUMPB );
			return;
		}
		// about to land: keep the press for the ground jump instead of burning the air jump
		if ( pm->ps->velocity[2] < 0 ) {
			VectorCopy( pm->ps->origin, end );
			end[2] += pm->ps->velocity[2] * JUMP_BUFFER_TIME * 0.001f - 2;
			pm->trace( &trace, pm->ps->origin, pm->mins, pm->maxs, end, pm->ps->clientNum, pm->tracemask );
			if ( trace.fraction < 1.0f && trace.plane.normal[2] >= MIN_WALK_NORMAL ) {
				return;
			}
		}
	}

	if ( RMF( RMF_WALLJUMP ) && pm->ps->stats[STAT_WALLJUMP_TIME] <= 0 && PM_FindWall( normal ) ) {
		pm->ps->pm_flags |= PMF_JUMP_HELD;''')
sub(P, '''	if ( pm->ps->pm_flags & PMF_DOUBLE_JUMPED ) {
		return;
	}
	pm->ps->pm_flags |= PMF_DOUBLE_JUMPED;''', '''	if ( !RMF( RMF_DOUBLEJUMP ) || ( pm->ps->pm_flags & PMF_DOUBLE_JUMPED ) ) {
		return;		// the press stays free, so holding it jumps again on landing
	}
	pm->ps->pm_flags |= PMF_DOUBLE_JUMPED | PMF_JUMP_HELD;''')

# dash: charges instead of one per air time
sub(P, '''	if ( pm->ps->pm_type != PM_NORMAL || pm->waterlevel > 1 || pm->ps->stats[STAT_DASH_TIME] > 0 ) {
		return;
	}
	if ( !pml.walking && ( pm->ps->pm_flags & PMF_AIR_DASHED ) ) {
		return;
	}
''', '''	if ( !RMF( RMF_DASH ) || pm->ps->pm_type != PM_NORMAL || pm->waterlevel > 1 || !DASH_READY( pm->ps ) ) {
		return;
	}
''')
sub(P, '''	if ( pml.walking ) {
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
''', '''	if ( pml.walking ) {
		// hop off the ground so friction doesn't eat the dash
		pml.walking = qfalse;
		pml.groundPlane = qfalse;
		pm->ps->groundEntityNum = ENTITYNUM_NONE;
	}
	pm->ps->stats[STAT_RMFLAGS] &= ~( RMS_WALLRUN | RMS_WALLRUN_LEFT );
	pm->ps->stats[STAT_DASH_TIME] += DASH_COOLDOWN;
	PM_AddEvent( EV_DASH );
}

/*
=============
PM_CheckWallRun

Holding forward at speed along a wall runs on it for a while:
most of gravity is cancelled and the speed is kept. Jumping kicks off (wall jump).
=============
*/
static void PM_CheckWallRun( void ) {
	vec3_t		flat, side, end, normal;
	trace_t		trace;
	float		speed, into, sign;
	int			i, wasLeft;
	qboolean	was, found;

	was = ( pm->ps->stats[STAT_RMFLAGS] & RMS_WALLRUN ) != 0;
	wasLeft = pm->ps->stats[STAT_RMFLAGS] & RMS_WALLRUN_LEFT;
	pm->ps->stats[STAT_RMFLAGS] &= ~( RMS_WALLRUN | RMS_WALLRUN_LEFT );

	if ( !RMF( RMF_WALLRUN ) || pml.walking || pm->waterlevel > 1 || pm->ps->pm_type != PM_NORMAL
		|| pm->cmd.forwardmove <= 0 || ( pm->ps->pm_flags & ( PMF_GRAPPLE_PULL | PMF_TIME_WATERJUMP ) )
		|| pm->ps->powerups[PW_FLIGHT] || pm->ps->stats[STAT_WALLRUN_TIME] >= WALLRUN_TIME ) {
		return;
	}
#ifndef MISSIONPACK
	if ( pm->ps->powerups[PW_JETPACK] ) {
		return;
	}
#endif
	flat[0] = pm->ps->velocity[0];
	flat[1] = pm->ps->velocity[1];
	flat[2] = 0;
	speed = VectorNormalize( flat );
	if ( speed < WALLRUN_MIN_SPEED || ( !was && pm->ps->velocity[2] < -250 ) ) {
		return;
	}

	// a wall beside the direction of travel, the side we were running on first
	side[0] = flat[1];
	side[1] = -flat[0];
	side[2] = 0;
	found = qfalse;
	sign = 1;
	for ( i = 0 ; i < 2 && !found ; i++ ) {
		sign = ( ( i == 0 ) == ( wasLeft != 0 ) ) ? -1 : 1;
		VectorMA( pm->ps->origin, sign * WALLRUN_DIST, side, end );
		pm->trace( &trace, pm->ps->origin, pm->mins, pm->maxs, end, pm->ps->clientNum, pm->tracemask );
		if ( trace.fraction < 1.0f && !trace.allsolid && fabs( trace.plane.normal[2] ) < 0.3f
			&& trace.entityNum >= MAX_CLIENTS && !( trace.surfaceFlags & ( SURF_SKY | SURF_NOIMPACT ) ) ) {
			VectorCopy( trace.plane.normal, normal );
			found = qtrue;
		}
	}
	if ( !found ) {
		return;
	}

	if ( !was ) {
		// catching a wall gives the air jump back
		pm->ps->pm_flags &= ~PMF_DOUBLE_JUMPED;
		if ( pm->ps->velocity[2] < 0 ) {
			pm->ps->velocity[2] *= 0.3f;
		}
		PM_AddEvent( EV_WALLRUN );
	}
	// run along the wall
	into = DotProduct( pm->ps->velocity, normal );
	if ( into < 0 ) {
		VectorMA( pm->ps->velocity, -into, normal, pm->ps->velocity );
	}
	// cancel most of gravity (the air move applies all of it)
	pm->ps->velocity[2] += pm->ps->gravity * pml.frametime * ( 1.0f - WALLRUN_GRAVITY );
	if ( pm->ps->velocity[2] < -WALLRUN_MAX_FALL ) {
		pm->ps->velocity[2] = -WALLRUN_MAX_FALL;
	}
	pm->ps->stats[STAT_WALLRUN_TIME] += pml.msec;
	pm->ps->stats[STAT_RMFLAGS] |= RMS_WALLRUN | ( sign < 0 ? RMS_WALLRUN_LEFT : 0 );
	PM_ContinueLegsAnim( LEGS_RUN );
}
''')

# timers
sub(P, '''	if ( pm->ps->stats[STAT_WALLJUMP_TIME] > 0 ) {
		pm->ps->stats[STAT_WALLJUMP_TIME] -= pml.msec;
		if ( pm->ps->stats[STAT_WALLJUMP_TIME] < 0 ) {
			pm->ps->stats[STAT_WALLJUMP_TIME] = 0;
		}
	}
}''', '''	if ( pm->ps->stats[STAT_WALLJUMP_TIME] > 0 ) {
		pm->ps->stats[STAT_WALLJUMP_TIME] -= pml.msec;
		if ( pm->ps->stats[STAT_WALLJUMP_TIME] < 0 ) {
			pm->ps->stats[STAT_WALLJUMP_TIME] = 0;
		}
	}
	if ( pm->ps->stats[STAT_JUMPTIMER] > 0 ) {
		pm->ps->stats[STAT_JUMPTIMER] -= pml.msec;
		if ( pm->ps->stats[STAT_JUMPTIMER] < 0 ) {
			pm->ps->stats[STAT_JUMPTIMER] = 0;
		}
	}
}''')

# PmoveSingle: coyote detection, refresh rules, wall run
sub(P, '''	// set groundentity
	PM_GroundTrace();

	if ( pm->ps->pm_type == PM_DEAD ) {
		PM_DeadMove ();
	}

	PM_DropTimers();

	// touching the ground (or swimming, or hanging on the grapple) restores the air moves
	if ( pml.walking || pm->waterlevel > 1 || ( pm->ps->pm_flags & PMF_GRAPPLE_PULL ) ) {
		pm->ps->pm_flags &= ~( PMF_DOUBLE_JUMPED | PMF_AIR_DASHED );
	}
''', '''	// set groundentity
	wasOnGround = pm->ps->groundEntityNum != ENTITYNUM_NONE;
	PM_GroundTrace();

	if ( pm->ps->pm_type == PM_DEAD ) {
		PM_DeadMove ();
	}

	PM_DropTimers();

	// touching the ground or swimming restores the air moves
	if ( pml.walking || pm->waterlevel > 1 ) {
		pm->ps->pm_flags &= ~( PMF_DOUBLE_JUMPED | PMF_AIR_DASHED );
		pm->ps->stats[STAT_WALLRUN_TIME] = 0;
		pm->ps->stats[STAT_JUMPTIMER] = 0;
	} else if ( wasOnGround && pm->ps->velocity[2] <= 10 && RMF( RMF_JUMPASSIST ) ) {
		pm->ps->stats[STAT_JUMPTIMER] = COYOTE_TIME;	// walked off a ledge
	}
	// a grapple pull restores them once, when it starts
	if ( pm->ps->pm_flags & PMF_GRAPPLE_PULL ) {
		if ( !( pm->ps->stats[STAT_RMFLAGS] & RMS_HOOKREFRESH ) ) {
			pm->ps->stats[STAT_RMFLAGS] |= RMS_HOOKREFRESH;
			pm->ps->pm_flags &= ~( PMF_DOUBLE_JUMPED | PMF_AIR_DASHED );
			pm->ps->stats[STAT_WALLRUN_TIME] = 0;
		}
	} else {
		pm->ps->stats[STAT_RMFLAGS] &= ~RMS_HOOKREFRESH;
	}
''')
sub(P, '''	if ( !pm->ps->powerups[PW_FLIGHT] ) {
		PM_CheckDash();
	}
''', '''	if ( !pm->ps->powerups[PW_FLIGHT] ) {
		PM_CheckDash();
	}
	PM_CheckWallRun();
''')
# PmoveSingle local
sub(P, '''void PmoveSingle (pmove_t *pmove) {
''', '''void PmoveSingle (pmove_t *pmove) {
	qboolean	wasOnGround;

''')

# weapons: alt-fire switch, slug recoil, spin-up switch
sub(P, '''		if ( ( pm->cmd.buttons & BUTTON_ALTATTACK ) && bg_altFire[pm->ps->weapon].mode == ALTFIRE_SHOT ) {''',
    '''		if ( ( pm->cmd.buttons & BUTTON_ALTATTACK ) && bg_altFire[pm->ps->weapon].mode == ALTFIRE_SHOT && RMF( RMF_ALTFIRE ) ) {''')
sub(P, '''	// shotgun recoil pushes the shooter back: aim down to shotgun jump
	if ( pm->ps->weapon == WP_SHOTGUN ) {
		VectorMA( pm->ps->velocity, -SHOTGUN_KICK, pml.forward, pm->ps->velocity );
		if ( pml.forward[2] < -0.3f ) {
			pm->ps->groundEntityNum = ENTITYNUM_NONE;
		}
	}
''', '''	PM_ShotgunKick();
''')
sub(P, '''	PM_AddEvent( EV_FIRE_WEAPON_ALT );

	if ( pm->ps->weapon == WP_GAUNTLET ) {''', '''	PM_AddEvent( EV_FIRE_WEAPON_ALT );
	PM_ShotgunKick();

	if ( pm->ps->weapon == WP_GAUNTLET ) {''')
sub(P, '''#define GAUNTLET_LUNGE_SPEED	700
#define GAUNTLET_LUNGE_LIFT		150
''', '''#define GAUNTLET_LUNGE_SPEED	700
#define GAUNTLET_LUNGE_LIFT		150

/*
==============
PM_ShotgunKick

Shotgun recoil pushes the shooter back: aim down to shotgun jump
==============
*/
static void PM_ShotgunKick( void ) {
	if ( pm->ps->weapon != WP_SHOTGUN || !RMF( RMF_SHOTGUNKICK ) ) {
		return;
	}
	VectorMA( pm->ps->velocity, -SHOTGUN_KICK, pml.forward, pm->ps->velocity );
	if ( pml.forward[2] < -0.3f ) {
		pm->ps->groundEntityNum = ENTITYNUM_NONE;
	}
}
''')
sub(P, '''	case WP_CHAINGUN:
		// spin up from slow to very fast while the trigger is held
		addTime = CHAINGUN_SLOW_TIME''', '''	case WP_CHAINGUN:
		if ( !RMF( RMF_SPINUP ) ) {
			addTime = 30;		// Team Arena's fixed rate
			break;
		}
		// spin up from slow to very fast while the trigger is held
		addTime = CHAINGUN_SLOW_TIME''')

# new event for the wall run
sub(g('bg_public.h'), '''	EV_BFG_LASER			// BFG ball zapping someone: origin -> origin2
''', '''	EV_BFG_LASER,			// BFG ball zapping someone: origin -> origin2
	EV_WALLRUN				// started running along a wall
''')
sub(g('bg_misc.c'), '''	"EV_BFG_LASER"''', '''	"EV_BFG_LASER",
	"EV_WALLRUN"''')
print('v2 move ok')
