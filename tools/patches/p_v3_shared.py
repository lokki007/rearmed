"""v3 shared: charged alt-fire (hold to charge), elements (burning / wet / shocked statuses),
new MODs and events, switches g_elements / g_chargedAlt."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from patch import sub

ROOT = pathlib.Path(__file__).resolve().parents[2] / 'ioq3'
g = lambda f: str(ROOT / 'code' / 'game' / f)
c = lambda f: str(ROOT / 'code' / 'cgame' / f)

H = g('bg_public.h')
sub(H, '''#define RMF_SPINUP			0x0080
#define RMF_CONFIG			0x00ff		// the bits the server owns''', '''#define RMF_SPINUP			0x0080
#define RMF_CHARGE			0x0800		// hold alt-fire to charge it
#define RMF_CONFIG			0x08ff		// the bits the server owns''')
sub(H, '''	STAT_JUMPTIMER,					// coyote time left
''', '''	STAT_JUMPTIMER,					// coyote time left
	STAT_ALTCHARGE,					// msec alt-fire has been held (charged alt-fire)
''')
sub(H, '''typedef struct {
	altFireMode_t	mode;
	int				ammo;		// ammo used per alt shot
	int				time;		// refire time in msec
} altFire_t;''', '''typedef struct {
	altFireMode_t	mode;
	int				ammo;		// ammo used per alt shot
	int				time;		// refire time in msec
	int				chargeAmmo;	// ammo used by the charged (held) version, 0 = can't charge
	int				chargeTime;	// refire time after the charged version
} altFire_t;

// charged alt-fire: a press shorter than ALT_TAP_TIME is the quick alt, a longer hold
// charges up to ALT_CHARGE_TIME. EV_FIRE_WEAPON_ALT carries the charge: 0 = tap, 1..255 = charged
#define ALT_TAP_TIME		250
#define ALT_CHARGE_TIME		1200
#define ALT_CHARGE_FRAC(parm)	( (parm) <= 0 ? 0.0f : ( (parm) - 1 ) / 254.0f )''')
sub(H, '''#define ALTFIRE_PROJECTILE	1	// entityState->generic1 bit on alt-fire missiles''',
    '''#define ALTFIRE_PROJECTILE	1	// entityState->generic1 bit on alt-fire missiles
#define ALTFIRE_CHARGED		2	// ... and on charged ones
#define RM_ENT_FIRE			64	// entityState->generic1 of an ET_GENERAL fire patch''')
sub(H, '''	PW_JETPACK,		// hold jump to fly
#endif''', '''	PW_JETPACK,		// hold jump to fly
	// element statuses (no items): the time they wear off
	PW_BURNING,
	PW_WET,
	PW_SHOCKED,
#endif''')
sub(H, '''	MOD_GRAPPLE
} meansOfDeath_t;''', '''	MOD_GRAPPLE,
	MOD_BURN,
	MOD_THUNDER
} meansOfDeath_t;''')
sub(H, '''	EV_WALLRUN				// started running along a wall
''', '''	EV_WALLRUN,				// started running along a wall
	EV_WINDBLAST,			// shotgun air blast: origin, angles2 = direction, eventParm = strength
	EV_THUNDER,				// thunderstrike: origin2 (sky) -> origin
	EV_STEAM,				// fire meets water
	EV_CHARGE_READY			// the alt-fire charge is full
''')
sub(H, '''	RS_POWERUPS,
	RS_NUM''', '''	RS_POWERUPS,
	RS_ELEMENTS,
	RS_CHARGE,
	RS_NUM''')

M = g('bg_misc.c')
sub(M, '''	"EV_WALLRUN"''', '''	"EV_WALLRUN",
	"EV_WINDBLAST",
	"EV_THUNDER",
	"EV_STEAM",
	"EV_CHARGE_READY"''')
sub(M, '''	{ ALTFIRE_NONE, 0, 0 },			// WP_NONE
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
#endif''', '''	// mode, ammo, refire; charged ammo, charged refire
	{ ALTFIRE_NONE, 0, 0, 0, 0 },			// WP_NONE
	{ ALTFIRE_SHOT, 0, 1000, 0, 1300 },		// gauntlet: lunge / rocket punch
	{ ALTFIRE_SHOT, 3, 450, 3, 700 },		// machinegun: 3 round burst / marksman shot
	{ ALTFIRE_SHOT, 1, 900, 2, 1300 },		// shotgun: air blast / gale (wind)
	{ ALTFIRE_SHOT, 1, 800, 3, 1300 },		// grenade launcher: sticky grenade / napalm (fire)
	{ ALTFIRE_TRIGGER, 0, 0, 2, 1400 },		// rocket launcher: detonate in flight / boulder (earth)
	{ ALTFIRE_SHOT, 6, 700, 30, 1500 },		// lightning gun: chain arc / thunderstrike (electricity)
	{ ALTFIRE_SHOT, 2, 2200, 2, 2600 },		// railgun: ricochet / wall-piercing lance
	{ ALTFIRE_SHOT, 8, 900, 25, 1600 },		// plasma gun: whirlpool, charged = bigger (water)
	{ ALTFIRE_SHOT, 3, 1400, 4, 1800 },		// BFG: triple spread / overload
	{ ALTFIRE_NONE, 0, 0, 0, 0 },			// grappling hook
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	{ ALTFIRE_SHOT, 3, 700, 5, 1200 },		// nailgun: ricochet stake / pinning spike
	{ ALTFIRE_TRIGGER, 0, 0, 3, 1300 },		// prox launcher: detonate / minefield
	{ ALTFIRE_SHOT, 1, 90, 0, 0 },			// chaingun: accurate shot / hold to spin up
#endif''')

sub(g('g_combat.c'), '''	"MOD_GRAPPLE"''', '''	"MOD_GRAPPLE",
	"MOD_BURN",
	"MOD_THUNDER"''')

# statuses have no items and no wear-off beeps
sub(c('cg_view.c'), '''	// powerup timers going away
	for ( i = 0 ; i < MAX_POWERUPS ; i++ ) {''', '''	// powerup timers going away (the element statuses aren't powerups)
	for ( i = 0 ; i < PW_NUM_POWERUPS ; i++ ) {
#ifndef MISSIONPACK
		if ( i >= PW_BURNING ) {
			break;
		}
#endif''')
print('v3 shared ok')
