"""Rules layer, shared defs: modes, mutators, new powerups (vampire, rapid fire, jetpack), items."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from patch import sub

G = pathlib.Path(__file__).resolve().parents[2] / 'ioq3' / 'code'
g = lambda f: str(G / 'game' / f)

sub(g('bg_public.h'), '''	PW_SCOUT,
	PW_GUARD,
	PW_DOUBLER,
	PW_AMMOREGEN,
	PW_INVULNERABILITY,
''', '''#ifdef MISSIONPACK
	PW_SCOUT,
	PW_GUARD,
	PW_DOUBLER,
	PW_AMMOREGEN,
	PW_INVULNERABILITY,
#else
	// rearmed powerups take the Team Arena persistant powerup slots
	PW_VAMPIRE,		// damage dealt heals you
	PW_RAPIDFIRE,	// double fire rate
	PW_JETPACK,		// hold jump to fly
#endif
''')

sub(g('bg_public.h'), '#define\tCS_ITEMS\t\t\t\t27\t\t// string of 0\'s and 1\'s that tell which items are present\n',
    '#define\tCS_ITEMS\t\t\t\t27\t\t// string of 0\'s and 1\'s that tell which items are present\n'
    '#define\tCS_REARMED\t\t\t\t28\t\t// info string: game mode, mutator, juggernaut\n')

sub(g('bg_public.h'), '// rearmed: alt-fire modes', '''// rearmed: game modes (g_rearmMode)
typedef enum {
	RM_MODE_NORMAL,
	RM_MODE_GUNGAME,
	RM_MODE_JUGGERNAUT,
	RM_MODE_INSTAGIB,
	RM_NUM_MODES
} rmMode_t;

// rearmed: match mutators (g_mutators)
typedef enum {
	MUT_NONE,
	MUT_LOW_GRAVITY,
	MUT_VAMPIRE,
	MUT_ROCKET_FRENZY,
	MUT_SPEED_RUSH,
	MUT_BIG_HEADS,
	MUT_KNOCKBACK_CHAOS,
	MUT_QUAD_MADNESS,
	MUT_NUM_MUTATORS
} rmMutator_t;

extern const char *bg_rmModeNames[RM_NUM_MODES];
extern const char *bg_rmMutatorNames[MUT_NUM_MUTATORS];
extern const char *bg_rmMutatorHints[MUT_NUM_MUTATORS];

#define GG_LEVELS	11
extern const int bg_gunGameLadder[GG_LEVELS];

#define EF_JETTING		EF_KAMIKAZE		// jetpack thrusting (kamikaze is Team Arena only)
#define JETPACK_THRUST	1500		// upward acceleration while holding jump
#define JETPACK_MAX_UP	380			// max climb speed

// rearmed: alt-fire modes''')

sub(g('bg_public.h'), '\tSTAT_SPINUP,\t\t\t\t\t// chaingun: msec of continuous fire\n',
    '\tSTAT_SPINUP,\t\t\t\t\t// chaingun: msec of continuous fire\n\tSTAT_GGLEVEL,\t\t\t\t\t// gun game ladder position\n')

sub(g('bg_misc.c'), '// alt-fire per weapon, in weapon_t order', '''const char *bg_rmModeNames[RM_NUM_MODES] = { "Normal", "Gun Game", "Juggernaut", "Instagib" };

const char *bg_rmMutatorNames[MUT_NUM_MUTATORS] = {
	"", "LOW GRAVITY", "VAMPIRE", "ROCKET FRENZY", "SPEED RUSH", "BIG HEADS", "KNOCKBACK CHAOS", "QUAD MADNESS"
};
const char *bg_rmMutatorHints[MUT_NUM_MUTATORS] = {
	"", "jump for the sky", "damage heals you", "rockets for everyone", "everyone moves faster",
	"headshots are easy", "everything hits harder", "everyone has quad damage"
};

const int bg_gunGameLadder[GG_LEVELS] = {
	WP_MACHINEGUN, WP_SHOTGUN, WP_CHAINGUN, WP_NAILGUN, WP_PLASMAGUN, WP_LIGHTNING,
	WP_GRENADE_LAUNCHER, WP_ROCKET_LAUNCHER, WP_RAILGUN, WP_BFG, WP_GAUNTLET
};

// alt-fire per weapon, in weapon_t order''')

# items, appended after the Team Arena weapons
sub(g('bg_misc.c'), '''/* sounds */ "sound/weapons/vulcan/wvulwind.wav"
	},
#endif
''', '''/* sounds */ "sound/weapons/vulcan/wvulwind.wav"
	},
#endif

#ifndef MISSIONPACK
/*QUAKED item_vampire (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"item_vampire",
		"sound/items/regeneration.wav",
		{ "models/powerups/instant/regen.md3",
		"models/powerups/instant/regen_ring.md3",
		NULL, NULL },
/* icon */		"icons/rm_vampire",
/* pickup */	"Vampire",
		30,
		IT_POWERUP,
		PW_VAMPIRE,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED item_rapidfire (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"item_rapidfire",
		"sound/items/haste.wav",
		{ "models/powerups/instant/haste.md3",
		"models/powerups/instant/haste_ring.md3",
		NULL, NULL },
/* icon */		"icons/rm_rapidfire",
/* pickup */	"Rapid Fire",
		30,
		IT_POWERUP,
		PW_RAPIDFIRE,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED item_jetpack (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"item_jetpack",
		"sound/items/flight.wav",
		{ "models/powerups/instant/flight.md3",
		"models/powerups/instant/flight_ring.md3",
		NULL, NULL },
/* icon */		"icons/rm_jetpack",
/* pickup */	"Jetpack",
		45,
		IT_POWERUP,
		PW_JETPACK,
/* precache */ "",
/* sounds */ ""
	},
#endif
''')

# --- pmove: jetpack + rapid fire ---
sub(g('bg_pmove.c'), '''	if ( pm->ps->pm_type != PM_NORMAL || pm->waterlevel > 1 ) {
		return;
	}
	if ( pm->ps->pm_flags & ( PMF_RESPAWNED | PMF_JUMP_HELD | PMF_TIME_WATERJUMP ) ) {''', '''	if ( pm->ps->pm_type != PM_NORMAL || pm->waterlevel > 1 ) {
		return;
	}
#ifndef MISSIONPACK
	if ( pm->ps->powerups[PW_JETPACK] ) {
		return;		// holding jump flies instead
	}
#endif
	if ( pm->ps->pm_flags & ( PMF_RESPAWNED | PMF_JUMP_HELD | PMF_TIME_WATERJUMP ) ) {''')
sub(g('bg_pmove.c'), '''	if ( !pm->ps->powerups[PW_FLIGHT] ) {
		PM_CheckDash();
	}
''', '''	if ( !pm->ps->powerups[PW_FLIGHT] ) {
		PM_CheckDash();
	}

#ifndef MISSIONPACK
	// jetpack: holding jump thrusts upwards
	pm->ps->eFlags &= ~EF_JETTING;
	if ( pm->ps->powerups[PW_JETPACK] && pm->cmd.upmove >= 10 && pm->ps->pm_type == PM_NORMAL
		&& pm->waterlevel <= 1 && !( pm->ps->pm_flags & PMF_RESPAWNED ) ) {
		if ( pml.walking ) {
			pml.walking = qfalse;
			pml.groundPlane = qfalse;
			pm->ps->groundEntityNum = ENTITYNUM_NONE;
		}
		if ( pm->ps->velocity[2] < JETPACK_MAX_UP ) {
			pm->ps->velocity[2] += JETPACK_THRUST * pml.frametime;
			if ( pm->ps->velocity[2] > JETPACK_MAX_UP ) {
				pm->ps->velocity[2] = JETPACK_MAX_UP;
			}
		}
		pm->ps->eFlags |= EF_JETTING;
	}
#endif
''')
sub(g('bg_pmove.c'), '''	addTime = alt->time;
	if ( pm->ps->powerups[PW_HASTE] ) {
		addTime /= 1.3;
	}''', '''	addTime = alt->time;
	if ( pm->ps->powerups[PW_HASTE] ) {
		addTime /= 1.3;
	}
#ifndef MISSIONPACK
	if ( pm->ps->powerups[PW_RAPIDFIRE] ) {
		addTime /= 2;
	}
#endif''')
sub(g('bg_pmove.c'), '''	if ( pm->ps->powerups[PW_HASTE] ) {
		addTime /= 1.3;
	}

	pm->ps->weaponTime += addTime;''', '''	if ( pm->ps->powerups[PW_HASTE] ) {
		addTime /= 1.3;
	}
#ifndef MISSIONPACK
	if ( pm->ps->powerups[PW_RAPIDFIRE] ) {
		addTime /= 2;
	}
#endif

	pm->ps->weaponTime += addTime;''')
print('shared ok')
