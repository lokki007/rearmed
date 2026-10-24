"""Offhand grapple on BUTTON_HOOK (+button13), usable with any weapon."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from patch import sub

G = pathlib.Path(__file__).resolve().parents[2] / 'ioq3' / 'code'
g = lambda f: str(G / 'game' / f)
cg = lambda f: str(G / 'cgame' / f)

sub(g('g_local.h'), '\tqboolean\tfireHeld;\t\t\t// used for hook\n',
    '\tqboolean\tfireHeld;\t\t\t// used for hook\n\tqboolean\thookHeld;\t\t\t// offhand hook button was down last frame\n')
sub(g('g_local.h'), 'void Weapon_HookFree (gentity_t *ent);\n', 'void Weapon_HookFree (gentity_t *ent);\nvoid Weapon_OffhandHook( gentity_t *ent );\n')
sub(g('g_local.h'), 'extern\tvmCvar_t\tg_rearmItems;\n', 'extern\tvmCvar_t\tg_rearmItems;\nextern\tvmCvar_t\tg_offhandHook;\n')
sub(g('g_main.c'), 'vmCvar_t\tg_rearmItems;\n', 'vmCvar_t\tg_rearmItems;\nvmCvar_t\tg_offhandHook;\n')
sub(g('g_main.c'), '\t{ &g_rearmItems, "g_rearmItems", "1", CVAR_ARCHIVE | CVAR_SERVERINFO | CVAR_LATCH, 0, qfalse },\n',
    '\t{ &g_rearmItems, "g_rearmItems", "1", CVAR_ARCHIVE | CVAR_SERVERINFO | CVAR_LATCH, 0, qfalse },\n'
    '\t{ &g_offhandHook, "g_offhandHook", "1", CVAR_ARCHIVE | CVAR_SERVERINFO, 0, qfalse },\n')

sub(g('g_weapon.c'), '''void Weapon_HookThink (gentity_t *ent)''', '''/*
================
Weapon_OffhandHook

Fire the grapple from the eyes without switching weapons
================
*/
#define OFFHAND_HOOK_SPEED	2000

void Weapon_OffhandHook( gentity_t *ent ) {
	vec3_t	fwd, rt, u, start;
	gentity_t	*hook;

	if ( ent->client->hook ) {
		return;
	}
	AngleVectors( ent->client->ps.viewangles, fwd, rt, u );
	CalcMuzzlePointOrigin( ent, ent->client->oldOrigin, fwd, rt, u, start );
	hook = fire_grapple( ent, start, fwd );
	VectorScale( fwd, OFFHAND_HOOK_SPEED, hook->s.pos.trDelta );
	SnapVector( hook->s.pos.trDelta );
	G_AddEvent( ent, EV_FIRE_HOOK, 0 );
}

void Weapon_HookThink (gentity_t *ent)''')

sub(g('g_active.c'), '''	// Let go of the hook if we aren't firing
	if ( client->ps.weapon == WP_GRAPPLING_HOOK &&
		client->hook && !( ucmd->buttons & BUTTON_ATTACK ) ) {
		Weapon_HookFree(client->hook);
	}''', '''	// offhand hook: fire on press
	if ( ( ucmd->buttons & BUTTON_HOOK ) && g_offhandHook.integer
		&& client->ps.pm_type == PM_NORMAL && client->ps.stats[STAT_HEALTH] > 0 ) {
		if ( !client->hookHeld ) {
			client->hookHeld = qtrue;
			Weapon_OffhandHook( ent );
		}
	} else {
		client->hookHeld = qfalse;
	}

	// Let go of the hook unless the offhand button or the hook weapon's fire is held
	if ( client->hook && !client->hookHeld &&
		!( client->ps.weapon == WP_GRAPPLING_HOOK && ( ucmd->buttons & BUTTON_ATTACK ) ) ) {
		Weapon_HookFree(client->hook);
	}''')

# event so the firing client hears/sees the throw
sub(g('bg_public.h'), '\tEV_DOUBLE_JUMP,\n\tEV_WALL_JUMP,\n\tEV_DASH\n', '\tEV_DOUBLE_JUMP,\n\tEV_WALL_JUMP,\n\tEV_DASH,\n\tEV_FIRE_HOOK\n')
sub(g('bg_misc.c'), '\t"EV_DASH"\n', '\t"EV_DASH",\n\t"EV_FIRE_HOOK"\n')
sub(cg('cg_event.c'), '''	case EV_DASH:
		DEBUGNAME("EV_DASH");''', '''	case EV_FIRE_HOOK:
		DEBUGNAME("EV_FIRE_HOOK");
		trap_S_StartSound( NULL, es->number, CHAN_AUTO, cgs.media.hookFireSound );
		break;
	case EV_DASH:
		DEBUGNAME("EV_DASH");''')
sub(cg('cg_local.h'), '\tsfxHandle_t\tdashSound;\n', '\tsfxHandle_t\tdashSound;\n\tsfxHandle_t\thookFireSound;\n')
sub(cg('cg_main.c'), '\tcgs.media.dashSound = trap_S_RegisterSound("sound/rearmed/dash.wav", qfalse);\n',
    '\tcgs.media.dashSound = trap_S_RegisterSound("sound/rearmed/dash.wav", qfalse);\n'
    '\tcgs.media.hookFireSound = trap_S_RegisterSound("sound/weapons/rocket/rocklf1a.wav", qfalse);\n')

# the hook's beam is drawn by the grapple weapon info, needed even without a grapple item on the map
sub(cg('cg_main.c'), '''			CG_RegisterItemVisuals( i );
		}
	}
''', '''			CG_RegisterItemVisuals( i );
		}
	}
	CG_RegisterWeapon( WP_GRAPPLING_HOOK );	// offhand hook
''')
print('hook ok')
