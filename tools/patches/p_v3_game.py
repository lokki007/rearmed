"""v3 server: charged alt-fire per weapon, elements hooks, whirlpool / BFG overload / pinning spike,
wall-piercing rail lance, remove nothing else."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from patch import sub

ROOT = pathlib.Path(__file__).resolve().parents[2] / 'ioq3'
g = lambda f: str(ROOT / 'code' / 'game' / f)

sub(str(ROOT / 'cmake' / 'basegame.cmake'), '    ${SOURCE_DIR}/game/g_rearmed.c\n',
    '    ${SOURCE_DIR}/game/g_rearmed.c\n    ${SOURCE_DIR}/game/g_elements.c\n')

# ---------- g_local.h ----------
L = g('g_local.h')
sub(L, '''	int			lungeTime;			// last gauntlet lunge, a hit soon after is a combo
''', '''	int			lungeTime;			// last gauntlet lunge, a hit soon after is a combo
	float		lungeCharge;		// 0..1, a charged lunge (rocket punch) hits harder
	int			burnNextTick;		// burning damage over time
	int			burnAttacker;		// who set us on fire (client number or ENTITYNUM_WORLD)
	int			pinnedUntil;		// pinned by a charged nailgun spike
''')
sub(L, '''void Weapon_AltTrigger( gentity_t *ent );
''', '''void Weapon_AltTrigger( gentity_t *ent );
void FireWeaponAltCharged( gentity_t *ent, int charge );

//
// g_elements.c
//
void G_ElementsInit( void );
int G_ElementsDamage( gentity_t *targ, gentity_t *attacker, int damage, int mod );
void G_ElementsClientFrame( gentity_t *ent );
float G_ElementsSpeedScale( gclient_t *cl );
void G_SpawnFirePatch( vec3_t origin, gentity_t *owner, int msec, float radius );
void G_EarthTremor( vec3_t origin, gentity_t *attacker, float radius, float up );
void G_ElementsExplode( gentity_t *missile, vec3_t origin );
void G_WhirlpoolSoak( gentity_t *other );
void G_AirBlast( gentity_t *ent, vec3_t muzzle, vec3_t forward, float charge );
void G_Thunderstrike( gentity_t *ent, vec3_t muzzle, vec3_t forward, float charge, float quad );
''')

# ---------- g_rearmed.c: init + per client frame ----------
R = g('g_rearmed.c')
sub(R, '''	level.firstBlood = qfalse;
	// a new level''', '''	level.firstBlood = qfalse;
	G_ElementsInit();
	// a new level''')
sub(R, '''		if ( level.mutator == MUT_JETPACKS && ent->client->ps.powerups[PW_JETPACK] < level.time + 1000 ) {''',
    '''		G_ElementsClientFrame( ent );
		if ( level.mutator == MUT_JETPACKS && ent->client->ps.powerups[PW_JETPACK] < level.time + 1000 ) {''')

# ---------- g_active.c: speed, charged event ----------
A = g('g_active.c')
sub(A, '''	client->ps.speed = g_speed.value * G_RearmSpeedScale();
''', '''	client->ps.speed = g_speed.value * G_RearmSpeedScale() * G_ElementsSpeedScale( client );
''')
sub(A, '''		case EV_FIRE_WEAPON_ALT:
			FireWeaponAlt( ent );
			break;''', '''		case EV_FIRE_WEAPON_ALT:
			if ( client->ps.eventParms[ i & ( MAX_PS_EVENTS - 1 ) ] > 0 ) {
				FireWeaponAltCharged( ent, client->ps.eventParms[ i & ( MAX_PS_EVENTS - 1 ) ] );
			} else {
				FireWeaponAlt( ent );
			}
			break;''')

# ---------- g_combat.c: elements before armor ----------
C = g('g_combat.c')
sub(C, '''	if ( damage < 1 ) {
		damage = 1;
	}
	take = damage;

	// save some from armor''', '''	// fire, water and electricity
	damage = G_ElementsDamage( targ, attacker, damage, mod );

	if ( damage < 1 ) {
		damage = 1;
	}
	take = damage;

	// save some from armor''')

# ---------- g_missile.c ----------
M = g('g_missile.c')
# explosions: fire and earth
sub(M, '''	ent->freeAfterEvent = qtrue;

	// splash damage
	if ( ent->splashDamage ) {
		if( G_RadiusDamage( ent->r.currentOrigin, ent->parent, ent->splashDamage, ent->splashRadius, ent
			, ent->splashMethodOfDeath ) ) {''', '''	ent->freeAfterEvent = qtrue;

	G_ElementsExplode( ent, ent->r.currentOrigin );

	// splash damage
	if ( ent->splashDamage ) {
		if( G_RadiusDamage( ent->r.currentOrigin, ent->parent, ent->splashDamage, ent->splashRadius, ent
			, ent->splashMethodOfDeath ) ) {''')
sub(M, '''	// splash damage (doesn't apply to person directly hit)
	if ( ent->splashDamage ) {
		if( G_RadiusDamage( trace->endpos, ent->parent, ent->splashDamage, ent->splashRadius, ''',
    '''	G_ElementsExplode( ent, trace->endpos );

	// splash damage (doesn't apply to person directly hit)
	if ( ent->splashDamage ) {
		if( G_RadiusDamage( trace->endpos, ent->parent, ent->splashDamage, ent->splashRadius, ''')
# whirlpool: radius / pull from the entity, soak everyone it reaches
sub(M, '''		VectorSubtract( ent->r.currentOrigin, other->r.currentOrigin, dir );
		dist = VectorNormalize( dir );
		if ( dist > SINGULARITY_RADIUS || dist < 1 ) {
			continue;
		}''', '''		VectorSubtract( ent->r.currentOrigin, other->r.currentOrigin, dir );
		dist = VectorNormalize( dir );
		if ( dist > ent->random || dist < 1 ) {
			continue;
		}''')
sub(M, '''		pull = SINGULARITY_PULL * ( 1.0f - dist / SINGULARITY_RADIUS ) * SINGULARITY_TICK / 1000.0f;''',
    '''		pull = ent->speed * ( 1.0f - dist / ent->random ) * SINGULARITY_TICK / 1000.0f;
		G_WhirlpoolSoak( other );''')
# BFG overload: longer lasers, more targets
sub(M, '''		dist = Distance( ent->r.currentOrigin, center );
		if ( dist > BFG_LASER_RADIUS ) {
			continue;
		}''', '''		dist = Distance( ent->r.currentOrigin, center );
		if ( dist > ( ( ent->s.generic1 & ALTFIRE_CHARGED ) ? BFG_LASER_RADIUS * 2 : BFG_LASER_RADIUS ) ) {
			continue;
		}''')
sub(M, '''	for ( zapped = 0 ; zapped < numTargets && zapped < BFG_LASER_TARGETS ; zapped++ ) {''',
    '''	for ( zapped = 0 ; zapped < numTargets && zapped < ( ( ent->s.generic1 & ALTFIRE_CHARGED ) ? BFG_LASER_TARGETS * 2 : BFG_LASER_TARGETS ) ; zapped++ ) {''')
# pinning spike
sub(M, '''			G_Damage (other, ent, &g_entities[ent->r.ownerNum], velocity,
				trace->endpos, ent->damage,''', '''			if ( ent->s.weapon == WP_NAILGUN && ( ent->s.generic1 & ALTFIRE_CHARGED ) && other->client ) {
				other->client->pinnedUntil = level.time + 600 + 800 * ent->random;
				VectorClear( other->client->ps.velocity );
				trap_SendServerCommand( other - g_entities, "rmann warn \\"PINNED\\"" );
			}
			G_Damage (other, ent, &g_entities[ent->r.ownerNum], velocity,
				trace->endpos, ent->damage,''')

# ---------- g_weapon.c ----------
W = g('g_weapon.c')
# tap whirlpool uses the entity radius / pull
sub(W, '''		m->count = level.time + 1800;
		m->think = G_SingularityThink;
		m->nextthink = level.time + 50;''', '''		m->count = level.time + 1300;
		m->random = 300;		// pull radius
		m->speed = 2400;		// pull strength
		m->think = G_SingularityThink;
		m->nextthink = level.time + 50;''')
# rocket punch hits harder
sub(W, '''		if ( level.time - ent->client->lungeTime < GAUNTLET_COMBO_TIME ) {
			scale = 1.6f;
			ent->client->lungeTime = 0;''', '''		if ( level.time - ent->client->lungeTime < GAUNTLET_COMBO_TIME ) {
			scale = 1.6f + 0.8f * ent->client->lungeCharge;
			if ( ent->client->lungeCharge > 0 ) {
				G_Damage( traceEnt, ent, ent, forward, tr.endpos, 50 * ent->client->lungeCharge * s_quadFactor, 0, MOD_GAUNTLET );
			}
			ent->client->lungeTime = 0;
			ent->client->lungeCharge = 0;''')
sub(W, '''	case WP_GAUNTLET:
		ent->client->lungeTime = level.time;
		break;''', '''	case WP_GAUNTLET:
		ent->client->lungeTime = level.time;
		ent->client->lungeCharge = 0;
		break;''')
# rail: the charged lance goes through one wall
sub(W, '''static int	s_railDepth = 0;		// ricochet segment of the current shot''',
    '''static int	s_railDepth = 0;		// ricochet segment of the current shot
static int	s_railPierce = 0;		// walls the current shot may still go through (charged lance)''')
sub(W, '''	// overcharged slugs ricochet off the world
	if ( s_railBounces > 0 && trace.fraction < 1.0f && !( trace.surfaceFlags & ( SURF_NOIMPACT | SURF_SKY ) ) ) {''', '''	// the lance punches through a wall up to RAIL_PIERCE_DEPTH thick
	if ( s_railPierce > 0 && trace.fraction < 1.0f && !( trace.surfaceFlags & SURF_SKY ) ) {
		vec3_t	exitPoint;
		int		k;

		for ( k = 8 ; k <= RAIL_PIERCE_DEPTH ; k += 8 ) {
			VectorMA( trace.endpos, k, forward, exitPoint );
			if ( !( trap_PointContents( exitPoint, -1 ) & CONTENTS_SOLID ) ) {
				s_railPierce--;
				VectorCopy( exitPoint, muzzle );
				s_railDepth++;
				weapon_railgun_fire( ent );
				s_railDepth--;
				return;
			}
		}
	}
	// overcharged slugs ricochet off the world
	if ( s_railBounces > 0 && trace.fraction < 1.0f && !( trace.surfaceFlags & ( SURF_NOIMPACT | SURF_SKY ) ) ) {''')
sub(W, '''#define	MAX_RAIL_HITS	4
''', '''#define	MAX_RAIL_HITS	4
#define RAIL_PIERCE_DEPTH	80
''')
# the charged versions
sub(W, '''/*
===============
Weapon_AltTrigger''', '''/*
===============
FireWeaponAltCharged

The held (charged) alt-fire. charge is 1..255 from the event, frac 0..1
===============
*/
void FireWeaponAltCharged( gentity_t *ent, int charge ) {
	gentity_t	*m;
	vec3_t		angles, dir;
	float		frac;
	int			i;

	frac = ALT_CHARGE_FRAC( charge );
	s_quadFactor = ent->client->ps.powerups[PW_QUAD] ? g_quadfactor.value : 1;
	AngleVectors( ent->client->ps.viewangles, forward, right, up );
	CalcMuzzlePointOrigin( ent, ent->client->oldOrigin, forward, right, up, muzzle );

	switch ( ent->s.weapon ) {
	case WP_GAUNTLET:
		// rocket punch: the lunge itself happened in pmove
		ent->client->lungeTime = level.time;
		ent->client->lungeCharge = frac;
		break;
	case WP_MACHINEGUN:
		// marksman: one dead-accurate heavy round
		Bullet_Fire( ent, 0, 20 + 40 * frac, MOD_MACHINEGUN );
		ent->client->accuracy_shots++;
		break;
	case WP_SHOTGUN:
		G_AirBlast( ent, muzzle, forward, frac );
		break;
	case WP_GRENADE_LAUNCHER:
		// napalm: bursts into a ring of fire
		forward[2] += 0.15f;
		VectorNormalize( forward );
		m = fire_grenade( ent, muzzle, forward );
		m->s.generic1 |= ALTFIRE_PROJECTILE | ALTFIRE_CHARGED;
		m->random = frac;
		m->damage *= s_quadFactor;
		m->splashDamage *= s_quadFactor;
		m->nextthink = level.time + 1400;
		break;
	case WP_ROCKET_LAUNCHER:
		// boulder: slow, heavy, shakes the whole area
		m = fire_rocket( ent, muzzle, forward );
		VectorScale( forward, 520, m->s.pos.trDelta );
		SnapVector( m->s.pos.trDelta );
		m->s.generic1 |= ALTFIRE_PROJECTILE | ALTFIRE_CHARGED;
		m->random = frac;
		m->damage = ( 100 + 60 * frac ) * s_quadFactor;
		m->splashDamage = ( 110 + 60 * frac ) * s_quadFactor;
		m->splashRadius = 150 + 150 * frac;
		break;
	case WP_LIGHTNING:
		G_Thunderstrike( ent, muzzle, forward, frac, s_quadFactor );
		ent->client->accuracy_shots++;
		break;
	case WP_RAILGUN:
		// lance: harder the longer you hold, goes through a wall
		s_railDamage = 100 + 100 * frac;
		s_railPierce = 1;
		weapon_railgun_fire( ent );
		s_railDamage = 100;
		s_railPierce = 0;
		ent->client->accuracy_shots++;
		break;
	case WP_PLASMAGUN:
		// a big whirlpool
		m = fire_plasma( ent, muzzle, forward );
		VectorScale( forward, 340, m->s.pos.trDelta );
		SnapVector( m->s.pos.trDelta );
		m->s.generic1 |= ALTFIRE_PROJECTILE | ALTFIRE_CHARGED;
		m->damage = ( 60 + 40 * frac ) * s_quadFactor;
		m->splashDamage = ( 90 + 60 * frac ) * s_quadFactor;
		m->splashRadius = 200 + 80 * frac;
		m->count = level.time + 1500 + 1300 * frac;
		m->random = 300 + 220 * frac;
		m->speed = 2400 + 1800 * frac;
		m->think = G_SingularityThink;
		m->nextthink = level.time + 50;
		break;
	case WP_BFG:
		// overload: one huge ball with twice the laser reach
		m = fire_bfg( ent, muzzle, forward );
		m->s.generic1 |= ALTFIRE_PROJECTILE | ALTFIRE_CHARGED;
		m->damage = m->damage * ( 2 + frac ) * s_quadFactor;
		m->splashDamage = m->splashDamage * ( 1.5f + frac ) * s_quadFactor;
		m->splashRadius = 180 + 100 * frac;
		break;
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case WP_NAILGUN:
		// pinning spike: fast, heavy, holds the target in place
		m = fire_nail( ent, muzzle, forward, right, up );
		VectorScale( forward, 3400, m->s.pos.trDelta );
		SnapVector( m->s.pos.trDelta );
		m->s.generic1 |= ALTFIRE_PROJECTILE | ALTFIRE_CHARGED;
		m->s.eFlags &= ~EF_BOUNCE;
		m->random = frac;
		m->damage = ( 45 + 45 * frac ) * s_quadFactor;
		ent->client->accuracy_shots++;
		break;
	case WP_PROX_LAUNCHER:
		// minefield: three mines in a fan
		for ( i = -1 ; i <= 1 ; i++ ) {
			vectoangles( forward, angles );
			angles[YAW] += i * 9;
			angles[PITCH] -= 4;
			AngleVectors( angles, dir, NULL, NULL );
			m = fire_prox( ent, muzzle, dir );
			m->damage *= s_quadFactor;
			m->splashDamage *= s_quadFactor;
		}
		break;
#endif
	default:
		break;
	}
}

/*
===============
Weapon_AltTrigger''')
# the quick shotgun alt is now the air blast (the slug is gone)
sub(W, '''	case WP_SHOTGUN:
		Bullet_Fire( ent, 0, SLUG_DAMAGE, MOD_SHOTGUN );
		ent->client->accuracy_shots++;
		break;''', '''	case WP_SHOTGUN:
		G_AirBlast( ent, muzzle, forward, 0 );
		break;''')
print('v3 game ok')
