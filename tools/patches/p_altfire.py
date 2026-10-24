"""Alt-fire (BUTTON_ALTATTACK, +button12) for every weapon.

Shot modes are predicted in pmove (EV_FIRE_WEAPON_ALT); trigger modes (remote detonation of
rockets / prox mines) are server side only."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from patch import sub

G = pathlib.Path(__file__).resolve().parents[2] / 'ioq3' / 'code'
g = lambda f: str(G / 'game' / f)
cg = lambda f: str(G / 'cgame' / f)

# --- shared table ---------------------------------------------------------------
sub(g('bg_public.h'), '\tEV_DASH,\n\tEV_FIRE_HOOK\n', '\tEV_DASH,\n\tEV_FIRE_HOOK,\n\tEV_FIRE_WEAPON_ALT,\n\tEV_ARC\t\t\t\t\t// alt lightning arc: origin -> origin2\n')
sub(g('bg_misc.c'), '\t"EV_FIRE_HOOK"\n', '\t"EV_FIRE_HOOK",\n\t"EV_FIRE_WEAPON_ALT",\n\t"EV_ARC"\n')

sub(g('bg_public.h'), '''// rearmed: extra usercmd buttons (+button12 .. +button14)''', '''// rearmed: alt-fire modes
typedef enum {
	ALTFIRE_NONE,
	ALTFIRE_SHOT,		// fires through pmove like the primary attack
	ALTFIRE_TRIGGER		// server side action on press (remote detonation)
} altFireMode_t;

typedef struct {
	altFireMode_t	mode;
	int				ammo;		// ammo used per alt shot
	int				time;		// refire time in msec
} altFire_t;

extern const altFire_t	bg_altFire[];

#define ALTFIRE_PROJECTILE	1	// entityState->generic1 bit on alt-fire missiles

// rearmed: extra usercmd buttons (+button12 .. +button14)''')

sub(g('bg_misc.c'), '''int		bg_numItems = ARRAY_LEN( bg_itemlist ) - 1;''', '''int		bg_numItems = ARRAY_LEN( bg_itemlist ) - 1;

// alt-fire per weapon, in weapon_t order
const altFire_t bg_altFire[] = {
	{ ALTFIRE_NONE, 0, 0 },			// WP_NONE
	{ ALTFIRE_SHOT, 0, 1000 },		// gauntlet: lunge
	{ ALTFIRE_SHOT, 3, 450 },		// machinegun: accurate 3 round burst
	{ ALTFIRE_SHOT, 1, 1100 },		// shotgun: slug
	{ ALTFIRE_SHOT, 1, 800 },		// grenade launcher: sticky grenade
	{ ALTFIRE_TRIGGER, 0, 0 },		// rocket launcher: detonate rockets in flight
	{ ALTFIRE_SHOT, 6, 700 },		// lightning gun: chain arc
	{ ALTFIRE_SHOT, 2, 2200 },		// railgun: overcharged slug
	{ ALTFIRE_SHOT, 8, 900 },		// plasma gun: plasma orb
	{ ALTFIRE_SHOT, 3, 1400 },		// BFG: triple spread
	{ ALTFIRE_NONE, 0, 0 },			// grappling hook
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	{ ALTFIRE_SHOT, 3, 700 },		// nailgun: heavy stake
	{ ALTFIRE_TRIGGER, 0, 0 },		// prox launcher: detonate mines
	{ ALTFIRE_SHOT, 1, 90 },		// chaingun: accurate fire
#endif
};''')

# --- pmove ------------------------------------------------------------------------
sub(g('bg_pmove.c'), '''/*
==============
PM_Weapon

Generates weapon events and modifes the weapon counter
==============
*/
static void PM_Weapon( void ) {
	int		addTime;
''', '''/*
==============
PM_AltFire

Alt-fire shot: ammo, event, refire time
==============
*/
#define GAUNTLET_LUNGE_SPEED	700
#define GAUNTLET_LUNGE_LIFT		150

static void PM_AltFire( void ) {
	const altFire_t	*alt = &bg_altFire[pm->ps->weapon];
	int				addTime;

	if ( pm->ps->ammo[pm->ps->weapon] != -1 && pm->ps->ammo[pm->ps->weapon] < alt->ammo ) {
		PM_AddEvent( EV_NOAMMO );
		pm->ps->weaponTime += 500;
		return;
	}
	if ( pm->ps->ammo[pm->ps->weapon] != -1 ) {
		pm->ps->ammo[pm->ps->weapon] -= alt->ammo;
	}

	PM_AddEvent( EV_FIRE_WEAPON_ALT );

	if ( pm->ps->weapon == WP_GAUNTLET ) {
		// lunge along the view direction
		VectorScale( pml.forward, GAUNTLET_LUNGE_SPEED, pm->ps->velocity );
		if ( pm->ps->velocity[2] < GAUNTLET_LUNGE_LIFT ) {
			pm->ps->velocity[2] = GAUNTLET_LUNGE_LIFT;
		}
		pm->ps->groundEntityNum = ENTITYNUM_NONE;
	}

	addTime = alt->time;
	if ( pm->ps->powerups[PW_HASTE] ) {
		addTime /= 1.3;
	}
	pm->ps->weaponTime += addTime;
}

/*
==============
PM_Weapon

Generates weapon events and modifes the weapon counter
==============
*/
static void PM_Weapon( void ) {
	int		addTime;
	qboolean	altFire;
''')

sub(g('bg_pmove.c'), '''	// check for fire
	if ( ! (pm->cmd.buttons & BUTTON_ATTACK) ) {
		pm->ps->weaponTime = 0;
		pm->ps->weaponstate = WEAPON_READY;
		return;
	}

	// start the animation even if out of ammo
	if ( pm->ps->weapon == WP_GAUNTLET ) {''', '''	// check for fire
	altFire = qfalse;
	if ( ! (pm->cmd.buttons & BUTTON_ATTACK) ) {
		if ( ( pm->cmd.buttons & BUTTON_ALTATTACK ) && bg_altFire[pm->ps->weapon].mode == ALTFIRE_SHOT ) {
			altFire = qtrue;
		} else {
			pm->ps->weaponTime = 0;
			pm->ps->weaponstate = WEAPON_READY;
			return;
		}
	}

	if ( altFire ) {
		PM_StartTorsoAnim( pm->ps->weapon == WP_GAUNTLET ? TORSO_ATTACK2 : TORSO_ATTACK );
		pm->ps->weaponstate = WEAPON_FIRING;
		PM_AltFire();
		return;
	}

	// start the animation even if out of ammo
	if ( pm->ps->weapon == WP_GAUNTLET ) {''')

# --- game: alt weapons ---------------------------------------------------------------
sub(g('g_weapon.c'), '''#define	MAX_RAIL_HITS	4
void weapon_railgun_fire (gentity_t *ent) {''', '''#define	MAX_RAIL_HITS	4
static int	s_railDamage = 100;		// the alt-fire overcharge raises this for one shot

void weapon_railgun_fire (gentity_t *ent) {''')
sub(g('g_weapon.c'), '''	damage = 100 * s_quadFactor;

	VectorMA (muzzle, 8192, forward, end);

	// trace only against the solids, so the railgun will go through people''', '''	damage = s_railDamage * s_quadFactor;

	VectorMA (muzzle, 8192, forward, end);

	// trace only against the solids, so the railgun will go through people''')

sub(g('g_weapon.c'), '''/*
===============
FireWeapon
===============
*/''', '''/*
======================================================================

ALT-FIRE

======================================================================
*/

#define MG_BURST_SPREAD		40
#define MG_BURST_DAMAGE		8
#define SLUG_DAMAGE			55
#define CHAINGUN_ALT_SPREAD	120
#define CHAINGUN_ALT_DAMAGE	9
#define RAIL_OVERCHARGE_DAMAGE	150
#define ARC_RANGE			768
#define ARC_DAMAGE			30
#define ARC_JUMP_RANGE		350
#define ARC_JUMPS			2
#define STAKE_SPEED			2400
#define STAKE_DAMAGE		45

void Bullet_Fire (gentity_t *ent, float spread, int damage, int mod );

/*
===============
Weapon_ArcTarget

Nearest living client within ARC_JUMP_RANGE of org, visible from it, not yet hit
===============
*/
static gentity_t *Weapon_ArcTarget( gentity_t *attacker, vec3_t org, gentity_t **hit, int numHit ) {
	gentity_t	*best, *other;
	trace_t		tr;
	vec3_t		center;
	float		dist, bestDist;
	int			i, j;

	best = NULL;
	bestDist = ARC_JUMP_RANGE;
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		other = &g_entities[i];
		if ( !other->inuse || !other->client || other->health <= 0 || other == attacker
			|| other->client->sess.sessionTeam == TEAM_SPECTATOR || OnSameTeam( other, attacker ) ) {
			continue;
		}
		for ( j = 0 ; j < numHit ; j++ ) {
			if ( hit[j] == other ) {
				break;
			}
		}
		if ( j < numHit ) {
			continue;
		}
		VectorCopy( other->r.currentOrigin, center );
		dist = Distance( org, center );
		if ( dist >= bestDist ) {
			continue;
		}
		trap_Trace( &tr, org, NULL, NULL, center, ENTITYNUM_NONE, MASK_SOLID );
		if ( tr.fraction < 1.0f ) {
			continue;
		}
		best = other;
		bestDist = dist;
	}
	return best;
}

static void Weapon_ArcBolt( vec3_t start, vec3_t end ) {
	gentity_t	*tent;

	tent = G_TempEntity( start, EV_ARC );
	VectorCopy( end, tent->s.origin2 );
	SnapVector( tent->s.origin2 );
}

/*
===============
Weapon_LightningArc

Lightning alt-fire: a bolt that jumps from the target to nearby enemies
===============
*/
static void Weapon_LightningArc( gentity_t *ent ) {
	gentity_t	*hit[ARC_JUMPS + 1];
	gentity_t	*target, *tent;
	trace_t		tr;
	vec3_t		end, from, to, dir;
	int			numHit, damage;

	VectorMA( muzzle, ARC_RANGE, forward, end );
	trap_Trace( &tr, muzzle, NULL, NULL, end, ent->s.number, MASK_SHOT );
	Weapon_ArcBolt( muzzle, tr.endpos );

	if ( tr.entityNum == ENTITYNUM_NONE ) {
		return;
	}
	target = &g_entities[tr.entityNum];
	if ( !target->takedamage ) {
		if ( !( tr.surfaceFlags & SURF_NOIMPACT ) ) {
			tent = G_TempEntity( tr.endpos, EV_MISSILE_MISS );
			tent->s.eventParm = DirToByte( tr.plane.normal );
			tent->s.weapon = WP_LIGHTNING;
		}
		return;
	}

	damage = ARC_DAMAGE * s_quadFactor;
	numHit = 0;
	VectorCopy( tr.endpos, to );
	while ( target ) {
		if ( LogAccuracyHit( target, ent ) ) {
			ent->client->accuracy_hits++;
		}
		VectorSubtract( to, muzzle, dir );
		G_Damage( target, ent, ent, dir, to, damage, 0, MOD_LIGHTNING );
		hit[numHit++] = target;
		if ( numHit > ARC_JUMPS || !target->client ) {
			break;
		}
		VectorCopy( to, from );
		target = Weapon_ArcTarget( ent, from, hit, numHit );
		if ( target ) {
			VectorCopy( target->r.currentOrigin, to );
			Weapon_ArcBolt( from, to );
			damage = damage * 2 / 3;
		}
	}
}

/*
===============
FireWeaponAlt
===============
*/
void FireWeaponAlt( gentity_t *ent ) {
	gentity_t	*m;
	vec3_t		angles, dir;
	int			i;

	s_quadFactor = ent->client->ps.powerups[PW_QUAD] ? g_quadfactor.value : 1;

	AngleVectors( ent->client->ps.viewangles, forward, right, up );
	CalcMuzzlePointOrigin( ent, ent->client->oldOrigin, forward, right, up, muzzle );

	switch ( ent->s.weapon ) {
	case WP_MACHINEGUN:
		for ( i = 0 ; i < 3 ; i++ ) {
			Bullet_Fire( ent, MG_BURST_SPREAD, MG_BURST_DAMAGE, MOD_MACHINEGUN );
		}
		ent->client->accuracy_shots += 3;
		break;
	case WP_SHOTGUN:
		Bullet_Fire( ent, 0, SLUG_DAMAGE, MOD_SHOTGUN );
		ent->client->accuracy_shots++;
		break;
	case WP_GRENADE_LAUNCHER:
		forward[2] += 0.2f;
		VectorNormalize( forward );
		m = fire_grenade( ent, muzzle, forward );
		m->classname = "sticky_grenade";
		m->s.eFlags &= ~EF_BOUNCE_HALF;
		m->s.generic1 |= ALTFIRE_PROJECTILE;
		m->damage *= s_quadFactor;
		m->splashDamage *= s_quadFactor;
		break;
	case WP_LIGHTNING:
		Weapon_LightningArc( ent );
		ent->client->accuracy_shots++;
		break;
	case WP_RAILGUN:
		s_railDamage = RAIL_OVERCHARGE_DAMAGE;
		weapon_railgun_fire( ent );
		s_railDamage = 100;
		ent->client->accuracy_shots++;
		break;
	case WP_PLASMAGUN:
		m = fire_plasma( ent, muzzle, forward );
		VectorScale( forward, 700, m->s.pos.trDelta );
		SnapVector( m->s.pos.trDelta );
		m->s.generic1 |= ALTFIRE_PROJECTILE;
		m->damage = 50 * s_quadFactor;
		m->splashDamage = 70 * s_quadFactor;
		m->splashRadius = 160;
		break;
	case WP_BFG:
		for ( i = -1 ; i <= 1 ; i++ ) {
			vectoangles( forward, angles );
			angles[YAW] += i * 6;
			AngleVectors( angles, dir, NULL, NULL );
			m = fire_bfg( ent, muzzle, dir );
			m->damage *= s_quadFactor;
			m->splashDamage *= s_quadFactor;
		}
		break;
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case WP_NAILGUN:
		m = fire_nail( ent, muzzle, forward, right, up );
		VectorScale( forward, STAKE_SPEED, m->s.pos.trDelta );
		SnapVector( m->s.pos.trDelta );
		m->s.generic1 |= ALTFIRE_PROJECTILE;
		m->damage = STAKE_DAMAGE * s_quadFactor;
		ent->client->accuracy_shots++;
		break;
	case WP_CHAINGUN:
		Bullet_Fire( ent, CHAINGUN_ALT_SPREAD, CHAINGUN_ALT_DAMAGE, MOD_CHAINGUN );
		ent->client->accuracy_shots++;
		break;
#endif
	default:
		break;
	}
}

/*
===============
Weapon_AltTrigger

Remote detonation of the player's rockets / prox mines
===============
*/
void Weapon_AltTrigger( gentity_t *ent ) {
	gentity_t	*m;
	int			i;

	for ( i = MAX_CLIENTS, m = &g_entities[MAX_CLIENTS] ; i < level.num_entities ; i++, m++ ) {
		if ( !m->inuse || m->s.eType != ET_MISSILE || m->r.ownerNum != ent->s.number ) {
			continue;
		}
		if ( ent->s.weapon == WP_ROCKET_LAUNCHER && m->s.weapon == WP_ROCKET_LAUNCHER ) {
			G_ExplodeMissile( m );
		}
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
		// only mines lying around, not the ones stuck to players
		if ( ent->s.weapon == WP_PROX_LAUNCHER && m->s.weapon == WP_PROX_LAUNCHER && !( m->r.svFlags & SVF_NOCLIENT ) ) {
			if ( m->activator ) {
				G_FreeEntity( m->activator );
				m->activator = NULL;
			}
			G_ExplodeMissile( m );
		}
#endif
	}
}

/*
===============
FireWeapon
===============
*/''')

sub(g('g_local.h'), 'void Weapon_OffhandHook( gentity_t *ent );\n',
    'void Weapon_OffhandHook( gentity_t *ent );\nvoid FireWeaponAlt( gentity_t *ent );\nvoid Weapon_AltTrigger( gentity_t *ent );\n')
sub(g('g_local.h'), '\tqboolean\thookHeld;\t\t\t// offhand hook button was down last frame\n',
    '\tqboolean\thookHeld;\t\t\t// offhand hook button was down last frame\n\tqboolean\taltHeld;\t\t\t// alt-fire button was down last frame\n')

sub(g('g_active.c'), '''		case EV_FIRE_WEAPON:
			FireWeapon( ent );
			break;
''', '''		case EV_FIRE_WEAPON:
			FireWeapon( ent );
			break;

		case EV_FIRE_WEAPON_ALT:
			FireWeaponAlt( ent );
			break;
''')
sub(g('g_active.c'), '''	// offhand hook: fire on press''', '''	// alt-fire triggers (remote detonation) act on press
	if ( ucmd->buttons & BUTTON_ALTATTACK ) {
		if ( !client->altHeld ) {
			client->altHeld = qtrue;
			if ( bg_altFire[client->ps.weapon].mode == ALTFIRE_TRIGGER
				&& client->ps.pm_type == PM_NORMAL && client->ps.stats[STAT_HEALTH] > 0 ) {
				Weapon_AltTrigger( ent );
			}
		}
	} else {
		client->altHeld = qfalse;
	}

	// offhand hook: fire on press''')

# sticky grenades stick to world surfaces instead of bouncing
sub(g('g_missile.c'), '''	// check for bounce
	if ( !other->takedamage &&
		( ent->s.eFlags & ( EF_BOUNCE | EF_BOUNCE_HALF ) ) ) {''', '''	// sticky grenades stay where they hit the world, then explode on their fuse
	if ( !other->takedamage && !strcmp( ent->classname, "sticky_grenade" ) ) {
		SnapVectorTowards( trace->endpos, ent->s.pos.trBase );
		G_SetOrigin( ent, trace->endpos );
		G_AddEvent( ent, EV_GRENADE_BOUNCE, 0 );
		return;
	}

	// check for bounce
	if ( !other->takedamage &&
		( ent->s.eFlags & ( EF_BOUNCE | EF_BOUNCE_HALF ) ) ) {''')

# --- cgame ------------------------------------------------------------------------------
sub(cg('cg_event.c'), '''	case EV_FIRE_WEAPON:
		DEBUGNAME("EV_FIRE_WEAPON");
		CG_FireWeapon( cent );
		break;
''', '''	case EV_FIRE_WEAPON:
		DEBUGNAME("EV_FIRE_WEAPON");
		CG_FireWeapon( cent );
		break;

	case EV_FIRE_WEAPON_ALT:
		DEBUGNAME("EV_FIRE_WEAPON_ALT");
		if ( es->weapon == WP_GAUNTLET ) {
			trap_S_StartSound( NULL, es->number, CHAN_WEAPON, cgs.media.dashSound );
		} else {
			CG_FireWeapon( cent );
		}
		break;

	case EV_ARC:
		DEBUGNAME("EV_ARC");
		CG_ArcBeam( es->pos.trBase, es->origin2 );
		break;
''')
sub(cg('cg_local.h'), 'void CG_MoveTechPuff( centity_t *cent, int type );\n',
    'void CG_MoveTechPuff( centity_t *cent, int type );\nvoid CG_ArcBeam( vec3_t start, vec3_t end );\n')
sub(cg('cg_effects.c'), '''/*
==================
CG_MoveTechPuff''', '''/*
==================
CG_ArcBeam

Short lived jagged lightning bolt for the lightning gun alt-fire
==================
*/
#define ARC_SEGMENTS	6
#define ARC_TIME		220

void CG_ArcBeam( vec3_t start, vec3_t end ) {
	localEntity_t	*le;
	refEntity_t		*beam;
	vec3_t			prev, next, delta;
	int				i;

	VectorSubtract( end, start, delta );
	VectorCopy( start, prev );
	for ( i = 1 ; i <= ARC_SEGMENTS ; i++ ) {
		VectorMA( start, (float)i / ARC_SEGMENTS, delta, next );
		if ( i < ARC_SEGMENTS ) {
			next[0] += crandom() * 14;
			next[1] += crandom() * 14;
			next[2] += crandom() * 14;
		}
		le = CG_AllocLocalEntity();
		le->leFlags = 0;
		le->leType = LE_FADE_RGB;
		le->startTime = cg.time;
		le->endTime = cg.time + ARC_TIME;
		le->lifeRate = 1.0 / ( le->endTime - le->startTime );
		le->color[0] = 0.7f;
		le->color[1] = 0.8f;
		le->color[2] = 1.0f;
		le->color[3] = 1.0f;
		beam = &le->refEntity;
		beam->reType = RT_LIGHTNING;
		beam->customShader = cgs.media.lightningShader;
		VectorCopy( prev, beam->origin );
		VectorCopy( next, beam->oldorigin );
		beam->shaderRGBA[0] = beam->shaderRGBA[1] = beam->shaderRGBA[2] = beam->shaderRGBA[3] = 255;
		VectorCopy( next, prev );
	}
}

/*
==================
CG_MoveTechPuff''')
print('altfire ok')
