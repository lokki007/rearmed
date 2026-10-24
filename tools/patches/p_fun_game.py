"""Fun weapons, server + shared side: hit confirms, airshots, juggling, shotgun jump, chaingun spin-up,
gauntlet uppercut, ricochet nails, rail ricochet, plasma singularity, BFG lasers."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from patch import sub

G = pathlib.Path(__file__).resolve().parents[2] / 'ioq3' / 'code'
g = lambda f: str(G / 'game' / f)

# --- shared ------------------------------------------------------------------------
sub(g('bg_public.h'), '\tEV_FIRE_WEAPON_ALT,\n\tEV_ARC\t\t\t\t\t// alt lightning arc: origin -> origin2\n',
    '\tEV_FIRE_WEAPON_ALT,\n\tEV_ARC,\t\t\t\t\t// alt lightning arc: origin -> origin2\n'
    '\tEV_HITCONFIRM,\t\t\t// to the attacker only: eventParm damage, generic1 HIT_* flags\n'
    '\tEV_BFG_LASER\t\t\t// BFG ball zapping someone: origin -> origin2\n')
sub(g('bg_misc.c'), '\t"EV_ARC"\n', '\t"EV_ARC",\n\t"EV_HITCONFIRM",\n\t"EV_BFG_LASER"\n')
sub(g('bg_public.h'), '#define ALTFIRE_PROJECTILE\t1\t// entityState->generic1 bit on alt-fire missiles\n',
    '#define ALTFIRE_PROJECTILE\t1\t// entityState->generic1 bit on alt-fire missiles\n\n'
    '// EV_HITCONFIRM flags\n#define HIT_KILL\t\t1\n#define HIT_AIRSHOT\t\t2\n#define HIT_HEADSHOT\t4\n\n'
    '#define SHOTGUN_KICK\t\t\t320\t// shotgun recoil speed, fire at the floor to shotgun jump\n'
    '#define CHAINGUN_SPIN_TIME\t\t1200\t// msec of firing to reach full speed\n'
    '#define CHAINGUN_SLOW_TIME\t\t110\t// refire at the start of the spin\n'
    '#define CHAINGUN_FAST_TIME\t\t28\t// refire fully spun up\n')
sub(g('bg_public.h'), '\tSTAT_WALLJUMP_TIME,\t\t\t\t// msec until the next wall jump\n',
    '\tSTAT_WALLJUMP_TIME,\t\t\t\t// msec until the next wall jump\n\tSTAT_SPINUP,\t\t\t\t\t// chaingun: msec of continuous fire\n')

# pmove: shotgun recoil, chaingun spin-up
sub(g('bg_pmove.c'), '''	// check for fire
	altFire = qfalse;
	if ( ! (pm->cmd.buttons & BUTTON_ATTACK) ) {''', '''	// check for fire
	altFire = qfalse;
	if ( !( pm->cmd.buttons & BUTTON_ATTACK ) || pm->ps->weapon != WP_CHAINGUN ) {
		pm->ps->stats[STAT_SPINUP] = 0;		// the chaingun spins down as soon as you let go
	}
	if ( ! (pm->cmd.buttons & BUTTON_ATTACK) ) {''')
sub(g('bg_pmove.c'), '''	// fire weapon
	PM_AddEvent( EV_FIRE_WEAPON );
''', '''	// fire weapon
	PM_AddEvent( EV_FIRE_WEAPON );

	// shotgun recoil pushes the shooter back: aim down to shotgun jump
	if ( pm->ps->weapon == WP_SHOTGUN ) {
		VectorMA( pm->ps->velocity, -SHOTGUN_KICK, pml.forward, pm->ps->velocity );
		if ( pml.forward[2] < -0.3f ) {
			pm->ps->groundEntityNum = ENTITYNUM_NONE;
		}
	}
''')
sub(g('bg_pmove.c'), '''	case WP_CHAINGUN:
		addTime = 30;
		break;''', '''	case WP_CHAINGUN:
		// spin up from slow to very fast while the trigger is held
		addTime = CHAINGUN_SLOW_TIME - ( CHAINGUN_SLOW_TIME - CHAINGUN_FAST_TIME )
			* pm->ps->stats[STAT_SPINUP] / CHAINGUN_SPIN_TIME;
		pm->ps->stats[STAT_SPINUP] += addTime;
		if ( pm->ps->stats[STAT_SPINUP] > CHAINGUN_SPIN_TIME ) {
			pm->ps->stats[STAT_SPINUP] = CHAINGUN_SPIN_TIME;
		}
		break;''')

# --- hit confirms ----------------------------------------------------------------------
sub(g('g_local.h'), '#define DAMAGE_NO_PROTECTION\t\t0x00000008  // armor, shields, invulnerability, and godmode have no effect\n',
    '#define DAMAGE_NO_PROTECTION\t\t0x00000008  // armor, shields, invulnerability, and godmode have no effect\n'
    '#define DAMAGE_AIRSHOT\t\t\t\t0x00000040\t// direct projectile hit on an airborne player\n')
sub(g('g_local.h'), '\tqboolean\taltHeld;\t\t\t// alt-fire button was down last frame\n',
    '\tqboolean\taltHeld;\t\t\t// alt-fire button was down last frame\n'
    '\tint\t\t\thitTarget;\t\t\t// hit confirm being gathered this frame\n'
    '\tint\t\t\thitDamage;\n'
    '\tint\t\t\thitFlags;\n'
    '\tvec3_t\t\thitOrigin;\n')
sub(g('g_local.h'), 'void FireWeaponAlt( gentity_t *ent );\n',
    'void FireWeaponAlt( gentity_t *ent );\nvoid G_FlushHitConfirm( gentity_t *attacker );\nvoid G_SingularityThink( gentity_t *ent );\nvoid G_BFGThink( gentity_t *ent );\n')

sub(g('g_combat.c'), '''void G_Damage( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker,
			   vec3_t dir, vec3_t point, int damage, int dflags, int mod ) {''', '''/*
============
G_FlushHitConfirm

Send the damage gathered this frame against one target to the attacker:
hitmarker, damage number, kill / airshot / headshot callouts
============
*/
void G_FlushHitConfirm( gentity_t *attacker ) {
	gclient_t	*cl = attacker->client;
	gentity_t	*tent;

	if ( !cl || cl->hitDamage <= 0 ) {
		return;
	}
	tent = G_TempEntity( cl->hitOrigin, EV_HITCONFIRM );
	tent->s.eventParm = cl->hitDamage > 255 ? 255 : cl->hitDamage;
	tent->s.generic1 = cl->hitFlags;
	tent->s.otherEntityNum = cl->hitTarget;
	tent->r.svFlags |= SVF_SINGLECLIENT | SVF_BROADCAST;
	tent->r.singleClient = attacker->s.number;
	cl->hitDamage = 0;
	cl->hitFlags = 0;
}

static void G_GatherHitConfirm( gentity_t *targ, gentity_t *attacker, int damage, int flags ) {
	gclient_t	*cl = attacker->client;

	if ( cl->hitDamage > 0 && cl->hitTarget != targ->s.number ) {
		G_FlushHitConfirm( attacker );
	}
	cl->hitTarget = targ->s.number;
	cl->hitDamage += damage;
	cl->hitFlags |= flags;
	VectorCopy( targ->r.currentOrigin, cl->hitOrigin );
	cl->hitOrigin[2] += 40;
}

void G_Damage( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker,
			   vec3_t dir, vec3_t point, int damage, int dflags, int mod ) {''')
sub(g('g_combat.c'), '''	int			knockback;
	int			max;''', '''	int			knockback;
	int			max;
	int			hitFlags = 0;''')
sub(g('g_combat.c'), '''		damage = damage * HEADSHOT_SCALE_NUM / HEADSHOT_SCALE_DEN;
		attacker->client->ps.persistant[PERS_HEADSHOTS]++;''', '''		damage = damage * HEADSHOT_SCALE_NUM / HEADSHOT_SCALE_DEN;
		attacker->client->ps.persistant[PERS_HEADSHOTS]++;
		hitFlags |= HIT_HEADSHOT;''')
sub(g('g_combat.c'), '''	if ( dflags & DAMAGE_NO_KNOCKBACK ) {
		knockback = 0;
	}
''', '''	if ( dflags & DAMAGE_NO_KNOCKBACK ) {
		knockback = 0;
	}

	// airborne players fly further: juggle them with explosives, airshots send them away
	if ( knockback && targ->client && targ != attacker && targ->client->ps.groundEntityNum == ENTITYNUM_NONE ) {
		if ( dflags & DAMAGE_AIRSHOT ) {
			knockback = knockback * 3 / 2;
			hitFlags |= HIT_AIRSHOT;
		} else if ( mod == MOD_ROCKET || mod == MOD_ROCKET_SPLASH || mod == MOD_GRENADE || mod == MOD_GRENADE_SPLASH
			|| mod == MOD_PLASMA_SPLASH || mod == MOD_BFG_SPLASH ) {
			knockback = knockback * 13 / 10;
		}
	}
''')
sub(g('g_combat.c'), '''	// do the damage
	if (take) {
		targ->health = targ->health - take;''', '''	// hit confirm for the attacker
	if ( attacker->client && targ->client && targ != attacker && !OnSameTeam( targ, attacker )
		&& targ->health > 0 && ( take + asave ) > 0 ) {
		if ( targ->health - take <= 0 ) {
			hitFlags |= HIT_KILL;
		}
		G_GatherHitConfirm( targ, attacker, take + asave, hitFlags );
	}

	// do the damage
	if (take) {
		targ->health = targ->health - take;''')
sub(g('g_active.c'), '''	// apply all the damage taken this frame
	P_DamageFeedback (ent);''', '''	// apply all the damage taken this frame
	P_DamageFeedback (ent);

	// tell the player about the damage they dealt
	G_FlushHitConfirm( ent );''')

# --- airshots: direct projectile hits on players well off the ground ---
sub(g('g_missile.c'), '''			BG_EvaluateTrajectoryDelta( &ent->s.pos, level.time, velocity );
			if ( VectorLength( velocity ) == 0 ) {
				velocity[2] = 1;	// stepped on a grenade
			}
			G_Damage (other, ent, &g_entities[ent->r.ownerNum], velocity,
				ent->s.origin, ent->damage,
				0, ent->methodOfDeath);''', '''			BG_EvaluateTrajectoryDelta( &ent->s.pos, level.time, velocity );
			if ( VectorLength( velocity ) == 0 ) {
				velocity[2] = 1;	// stepped on a grenade
			}
			G_Damage (other, ent, &g_entities[ent->r.ownerNum], velocity,
				ent->s.origin, ent->damage,
				G_IsAirshot( ent, other ) ? DAMAGE_AIRSHOT : 0, ent->methodOfDeath);''')
sub(g('g_missile.c'), '''/*
================
G_MissileImpact
================
*/''', '''/*
================
G_IsAirshot

Direct hit on another player who is well above the ground
================
*/
#define AIRSHOT_HEIGHT	64

static qboolean G_IsAirshot( gentity_t *missile, gentity_t *other ) {
	trace_t	tr;
	vec3_t	down;

	if ( !other->client || other->health <= 0 || other->s.number == missile->r.ownerNum
		|| other->client->ps.groundEntityNum != ENTITYNUM_NONE ) {
		return qfalse;
	}
	VectorCopy( other->r.currentOrigin, down );
	down[2] -= AIRSHOT_HEIGHT - other->r.mins[2];
	trap_Trace( &tr, other->r.currentOrigin, NULL, NULL, down, other->s.number, MASK_SOLID );
	return tr.fraction == 1.0f;
}

/*
================
G_MissileImpact
================
*/''')

# --- ricochet nails ---
sub(g('g_missile.c'), '''	// check for bounce
	if ( !other->takedamage &&
		( ent->s.eFlags & ( EF_BOUNCE | EF_BOUNCE_HALF ) ) ) {''', '''	// nails ricochet a limited number of times
	if ( !other->takedamage && ( ent->s.eFlags & EF_BOUNCE ) && ent->s.weapon == WP_NAILGUN ) {
		if ( --ent->count <= 0 ) {
			ent->s.eFlags &= ~EF_BOUNCE;
		}
	}

	// check for bounce
	if ( !other->takedamage &&
		( ent->s.eFlags & ( EF_BOUNCE | EF_BOUNCE_HALF ) ) ) {''')
sub(g('g_missile.c'), '''	bolt->damage = 20;
	bolt->methodOfDeath = MOD_NAIL;''', '''	bolt->damage = 20;
	bolt->methodOfDeath = MOD_NAIL;
	bolt->s.eFlags = EF_BOUNCE;		// ricochets off walls
	bolt->count = 2;				// bounces left''')

# --- plasma singularity + BFG lasers ---
sub(g('g_missile.c'), '''/*
================
G_IsAirshot''', '''/*
================
G_SingularityThink

Plasma alt orb: drags players towards itself, then detonates
================
*/
#define SINGULARITY_RADIUS	420
#define SINGULARITY_PULL	1500	// units/sec^2 at the center
#define SINGULARITY_TICK	50

void G_SingularityThink( gentity_t *ent ) {
	gentity_t	*other;
	vec3_t		dir;
	trace_t		tr;
	float		dist, pull;
	int			i;

	if ( level.time >= ent->count ) {
		G_ExplodeMissile( ent );
		return;
	}
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		other = &g_entities[i];
		if ( !other->inuse || !other->client || other->health <= 0 || i == ent->r.ownerNum
			|| other->client->sess.sessionTeam == TEAM_SPECTATOR || other->client->noclip ) {
			continue;
		}
		VectorSubtract( ent->r.currentOrigin, other->r.currentOrigin, dir );
		dist = VectorNormalize( dir );
		if ( dist > SINGULARITY_RADIUS || dist < 1 ) {
			continue;
		}
		trap_Trace( &tr, ent->r.currentOrigin, NULL, NULL, other->r.currentOrigin, ENTITYNUM_NONE, MASK_SOLID );
		if ( tr.fraction < 1.0f ) {
			continue;
		}
		pull = SINGULARITY_PULL * ( 1.0f - dist / SINGULARITY_RADIUS ) * SINGULARITY_TICK / 1000.0f;
		VectorMA( other->client->ps.velocity, pull, dir, other->client->ps.velocity );
		if ( other->client->ps.groundEntityNum != ENTITYNUM_NONE && other->client->ps.velocity[2] < 60 ) {
			other->client->ps.velocity[2] = 60;		// lift off so friction doesn't hold them
		}
		other->client->ps.pm_time = 100;
		other->client->ps.pm_flags |= PMF_TIME_KNOCKBACK;
	}
	ent->nextthink = level.time + SINGULARITY_TICK;
}

/*
================
G_BFGThink

The BFG ball zaps enemies near its flight path
================
*/
#define BFG_LASER_RADIUS	450
#define BFG_LASER_TICK		100
#define BFG_LASER_DAMAGE	7
#define BFG_LASER_TARGETS	3

void G_BFGThink( gentity_t *ent ) {
	gentity_t	*other, *owner, *tent;
	vec3_t		dir, center;
	trace_t		tr;
	int			i, zapped;

	if ( level.time >= ent->count ) {
		G_ExplodeMissile( ent );
		return;
	}
	owner = &g_entities[ent->r.ownerNum];
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
		VectorSubtract( center, ent->r.currentOrigin, dir );
		// damage scales with the ball (quad damage)
		G_Damage( other, ent, owner, dir, center, BFG_LASER_DAMAGE * ent->damage / 100, DAMAGE_NO_KNOCKBACK, MOD_BFG );
		tent = G_TempEntity( ent->r.currentOrigin, EV_BFG_LASER );
		VectorCopy( center, tent->s.origin2 );
		SnapVector( tent->s.origin2 );
		zapped++;
	}
	ent->nextthink = level.time + BFG_LASER_TICK;
}

/*
================
G_IsAirshot''')

# --- weapons ---
sub(g('g_weapon.c'), '''	m = fire_bfg (ent, muzzle, forward);
	m->damage *= s_quadFactor;
	m->splashDamage *= s_quadFactor;
''', '''	m = fire_bfg (ent, muzzle, forward);
	m->damage *= s_quadFactor;
	m->splashDamage *= s_quadFactor;
	m->count = level.time + 10000;		// lifetime, the think zaps enemies meanwhile
	m->think = G_BFGThink;
	m->nextthink = level.time + 100;
''')
sub(g('g_weapon.c'), '''		m = fire_plasma( ent, muzzle, forward );
		VectorScale( forward, 700, m->s.pos.trDelta );
		SnapVector( m->s.pos.trDelta );
		m->s.generic1 |= ALTFIRE_PROJECTILE;
		m->damage = 50 * s_quadFactor;
		m->splashDamage = 70 * s_quadFactor;
		m->splashRadius = 160;''', '''		// singularity: slow orb that drags players in, then detonates
		m = fire_plasma( ent, muzzle, forward );
		VectorScale( forward, 380, m->s.pos.trDelta );
		SnapVector( m->s.pos.trDelta );
		m->s.generic1 |= ALTFIRE_PROJECTILE;
		m->damage = 60 * s_quadFactor;
		m->splashDamage = 90 * s_quadFactor;
		m->splashRadius = 200;
		m->count = level.time + 1800;
		m->think = G_SingularityThink;
		m->nextthink = level.time + 50;''')
sub(g('g_weapon.c'), '''		m->s.generic1 |= ALTFIRE_PROJECTILE;
		m->damage = STAKE_DAMAGE * s_quadFactor;''', '''		m->s.generic1 |= ALTFIRE_PROJECTILE;
		m->damage = STAKE_DAMAGE * s_quadFactor;
		m->count = 4;		// the stake ricochets even more''')

# gauntlet uppercut
sub(g('g_weapon.c'), '''	damage = 50 * s_quadFactor;
	G_Damage( traceEnt, ent, ent, forward, tr.endpos,
		damage, 0, MOD_GAUNTLET );

	return qtrue;''', '''	damage = 50 * s_quadFactor;
	G_Damage( traceEnt, ent, ent, forward, tr.endpos,
		damage, 0, MOD_GAUNTLET );

	// uppercut: launch the victim
	if ( traceEnt->client && traceEnt->health > 0 ) {
		VectorMA( traceEnt->client->ps.velocity, GAUNTLET_LAUNCH_PUSH, forward, traceEnt->client->ps.velocity );
		if ( traceEnt->client->ps.velocity[2] < GAUNTLET_LAUNCH_UP ) {
			traceEnt->client->ps.velocity[2] = GAUNTLET_LAUNCH_UP;
		}
		traceEnt->client->ps.pm_time = 200;
		traceEnt->client->ps.pm_flags |= PMF_TIME_KNOCKBACK;
	}

	return qtrue;''')
sub(g('g_weapon.c'), '#define NUM_NAILSHOTS 15\n', '#define NUM_NAILSHOTS 15\n\n#define GAUNTLET_LAUNCH_UP\t\t480\n#define GAUNTLET_LAUNCH_PUSH\t280\n')

# rail overcharge ricochet: re-fire from the wall along the reflection
sub(g('g_weapon.c'), '''#define	MAX_RAIL_HITS	4
static int	s_railDamage = 100;		// the alt-fire overcharge raises this for one shot
''', '''#define	MAX_RAIL_HITS	4
static int	s_railDamage = 100;		// the alt-fire overcharge raises this for one shot
static int	s_railBounces = 0;		// ricochets left for the current overcharged shot
''')
sub(g('g_weapon.c'), '''	// no explosion at end if SURF_NOIMPACT, but still make the trail
	if ( trace.surfaceFlags & SURF_NOIMPACT ) {
		tent->s.eventParm = 255;	// don't make the explosion at the end
	} else {
		tent->s.eventParm = DirToByte( trace.plane.normal );
	}
	tent->s.clientNum = ent->s.clientNum;
''', '''	// no explosion at end if SURF_NOIMPACT, but still make the trail
	if ( trace.surfaceFlags & SURF_NOIMPACT ) {
		tent->s.eventParm = 255;	// don't make the explosion at the end
	} else {
		tent->s.eventParm = DirToByte( trace.plane.normal );
	}
	tent->s.clientNum = ent->s.clientNum;

	// overcharged slugs ricochet off the world
	if ( s_railBounces > 0 && trace.fraction < 1.0f && !( trace.surfaceFlags & ( SURF_NOIMPACT | SURF_SKY ) ) ) {
		float	dot;

		s_railBounces--;
		dot = DotProduct( forward, trace.plane.normal );
		VectorMA( forward, -2 * dot, trace.plane.normal, forward );
		VectorNormalize( forward );
		VectorMA( trace.endpos, 2, trace.plane.normal, muzzle );
		PerpendicularVector( right, forward );
		CrossProduct( forward, right, up );
		weapon_railgun_fire( ent );
		return;
	}
''')
sub(g('g_weapon.c'), '''		s_railDamage = RAIL_OVERCHARGE_DAMAGE;
		weapon_railgun_fire( ent );
		s_railDamage = 100;''', '''		s_railDamage = RAIL_OVERCHARGE_DAMAGE;
		s_railBounces = 2;
		weapon_railgun_fire( ent );
		s_railDamage = 100;
		s_railBounces = 0;''')
print('game ok')
