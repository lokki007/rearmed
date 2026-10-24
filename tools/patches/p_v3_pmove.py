"""v3 pmove: hold alt-fire to charge it; the chaingun alt spins the barrel."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from patch import sub

ROOT = pathlib.Path(__file__).resolve().parents[2] / 'ioq3'
P = str(ROOT / 'code' / 'game' / 'bg_pmove.c')

sub(P, '''	PM_AddEvent( EV_CHANGE_WEAPON );
	pm->ps->weaponstate = WEAPON_DROPPING;''', '''	PM_AddEvent( EV_CHANGE_WEAPON );
	pm->ps->stats[STAT_ALTCHARGE] = 0;		// switching drops a charge
	pm->ps->weaponstate = WEAPON_DROPPING;''')

# PM_AltFire takes the charge (0 = tap)
sub(P, '''static void PM_AltFire( void ) {
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
	PM_ShotgunKick();

	if ( pm->ps->weapon == WP_GAUNTLET ) {
		// lunge along the view direction
		VectorScale( pml.forward, GAUNTLET_LUNGE_SPEED, pm->ps->velocity );''', '''static void PM_AltFire( int charge ) {
	const altFire_t	*alt = &bg_altFire[pm->ps->weapon];
	int				addTime, ammo;

	ammo = charge ? alt->chargeAmmo : alt->ammo;
	if ( pm->ps->ammo[pm->ps->weapon] != -1 && pm->ps->ammo[pm->ps->weapon] < ammo ) {
		PM_AddEvent( EV_NOAMMO );
		pm->ps->weaponTime += 500;
		return;
	}
	if ( pm->ps->ammo[pm->ps->weapon] != -1 ) {
		pm->ps->ammo[pm->ps->weapon] -= ammo;
	}

	BG_AddPredictableEventToPlayerstate( EV_FIRE_WEAPON_ALT, charge, pm->ps );
	PM_ShotgunKick();

	if ( pm->ps->weapon == WP_GAUNTLET ) {
		// lunge along the view direction, much further when charged (rocket punch)
		VectorScale( pml.forward, GAUNTLET_LUNGE_SPEED + GAUNTLET_PUNCH_SPEED * ALT_CHARGE_FRAC( charge ), pm->ps->velocity );''')
sub(P, '''	addTime = alt->time;
	if ( pm->ps->powerups[PW_HASTE] ) {''', '''	addTime = charge ? alt->chargeTime : alt->time;
	if ( pm->ps->powerups[PW_HASTE] ) {''')
sub(P, '''#define GAUNTLET_LUNGE_SPEED	700
#define GAUNTLET_LUNGE_LIFT		150
''', '''#define GAUNTLET_LUNGE_SPEED	700
#define GAUNTLET_PUNCH_SPEED	600		// extra lunge speed at full charge
#define GAUNTLET_LUNGE_LIFT		150

/*
==============
PM_ChargeAlt

Charged alt-fire: holding alt-fire charges it, releasing fires. A short press is the quick alt.
Returns qtrue when the alt button owns the weapon this frame.
==============
*/
static qboolean PM_ChargeAlt( void ) {
	const altFire_t	*alt = &bg_altFire[pm->ps->weapon];
	int				*charge = &pm->ps->stats[STAT_ALTCHARGE];
	int				held, parm;

	if ( alt->mode == ALTFIRE_NONE ) {
		*charge = 0;
		return qfalse;
	}
	if ( ( pm->cmd.buttons & BUTTON_ALTATTACK ) && !( pm->cmd.buttons & BUTTON_ATTACK ) ) {
		// charging (starts only with the weapon ready)
		if ( *charge < ALT_CHARGE_TIME + 1 ) {
			if ( *charge < ALT_CHARGE_TIME && *charge + pml.msec >= ALT_CHARGE_TIME
				&& ( alt->chargeAmmo > 0 || pm->ps->weapon == WP_CHAINGUN ) ) {
				PM_AddEvent( EV_CHARGE_READY );
			}
			*charge += pml.msec;
			if ( *charge > ALT_CHARGE_TIME + 1 ) {
				*charge = ALT_CHARGE_TIME + 1;
			}
		}
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
		// the chaingun alt spins the barrel up without firing
		if ( pm->ps->weapon == WP_CHAINGUN && *charge >= ALT_TAP_TIME ) {
			pm->ps->stats[STAT_SPINUP] += pml.msec;
			if ( pm->ps->stats[STAT_SPINUP] > CHAINGUN_SPIN_TIME ) {
				pm->ps->stats[STAT_SPINUP] = CHAINGUN_SPIN_TIME;
			}
		}
#endif
		pm->ps->weaponstate = WEAPON_READY;
		return qtrue;
	}
	if ( *charge <= 0 ) {
		return qfalse;
	}

	// released
	held = *charge;
	*charge = 0;
	if ( pm->cmd.buttons & BUTTON_ATTACK ) {
		return qfalse;		// the primary trigger cancels the charge
	}
	if ( held < ALT_TAP_TIME ) {
		if ( alt->mode != ALTFIRE_SHOT ) {
			return qtrue;	// trigger alts acted on the press (server side)
		}
		parm = 0;
	} else {
		if ( alt->chargeAmmo <= 0 ) {
			return qtrue;	// nothing to fire (chaingun spin-up)
		}
		if ( held > ALT_CHARGE_TIME ) {
			held = ALT_CHARGE_TIME;
		}
		parm = 1 + ( held - ALT_TAP_TIME ) * 254 / ( ALT_CHARGE_TIME - ALT_TAP_TIME );
	}
	PM_StartTorsoAnim( pm->ps->weapon == WP_GAUNTLET ? TORSO_ATTACK2 : TORSO_ATTACK );
	pm->ps->weaponstate = WEAPON_FIRING;
	PM_AltFire( parm );
	return qtrue;
}
''')

# check for fire: charged alt first; the quick alt on press only without charging
sub(P, '''	// check for fire
	altFire = qfalse;
	if ( !( pm->cmd.buttons & BUTTON_ATTACK ) || pm->ps->weapon != WP_CHAINGUN ) {
		pm->ps->stats[STAT_SPINUP] = 0;		// the chaingun spins down as soon as you let go
	}
	if ( ! (pm->cmd.buttons & BUTTON_ATTACK) ) {
		if ( ( pm->cmd.buttons & BUTTON_ALTATTACK ) && bg_altFire[pm->ps->weapon].mode == ALTFIRE_SHOT && RMF( RMF_ALTFIRE ) ) {''', '''	// check for fire
	altFire = qfalse;
	if ( pm->ps->weapon != WP_CHAINGUN ) {
		pm->ps->stats[STAT_SPINUP] = 0;
	} else if ( !( pm->cmd.buttons & ( BUTTON_ATTACK | BUTTON_ALTATTACK ) ) ) {
		// the chaingun spins down quickly once both triggers are let go
		pm->ps->stats[STAT_SPINUP] -= pml.msec * 3;
		if ( pm->ps->stats[STAT_SPINUP] < 0 ) {
			pm->ps->stats[STAT_SPINUP] = 0;
		}
	}
	if ( RMF( RMF_ALTFIRE ) && RMF( RMF_CHARGE ) ) {
		if ( PM_ChargeAlt() ) {
			return;
		}
	} else {
		pm->ps->stats[STAT_ALTCHARGE] = 0;
	}
	if ( ! (pm->cmd.buttons & BUTTON_ATTACK) ) {
		if ( ( pm->cmd.buttons & BUTTON_ALTATTACK ) && bg_altFire[pm->ps->weapon].mode == ALTFIRE_SHOT && RMF( RMF_ALTFIRE )
			&& !RMF( RMF_CHARGE ) ) {''')
sub(P, '''		pm->ps->weaponstate = WEAPON_FIRING;
		PM_AltFire();
		return;''', '''		pm->ps->weaponstate = WEAPON_FIRING;
		PM_AltFire( 0 );
		return;''')
print('v3 pmove ok')
