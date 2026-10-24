"""Rules layer, server hooks for g_rearmed.c."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from patch import sub

ROOT = pathlib.Path(__file__).resolve().parents[2] / 'ioq3'
g = lambda f: str(ROOT / 'code' / 'game' / f)

sub(str(ROOT / 'cmake' / 'basegame.cmake'), '    ${SOURCE_DIR}/cgame/cg_fx.c\n', '    ${SOURCE_DIR}/cgame/cg_fx.c\n    ${SOURCE_DIR}/cgame/cg_rearmed.c\n')
sub(str(ROOT / 'cmake' / 'basegame.cmake'), '    ${SOURCE_DIR}/game/g_main.c\n', '    ${SOURCE_DIR}/game/g_main.c\n    ${SOURCE_DIR}/game/g_rearmed.c\n')

# --- g_local.h ---
sub(g('g_local.h'), '\tint\t\t\trearmSpawnCount[6];\t// g_rearmItems: per-classname spawn counters\n',
    '\tint\t\t\trearmSpawnCount[16];\t// g_rearmItems: per-classname spawn counters\n'
    '\tint\t\t\trmMode;\t\t\t\t// rmMode_t, from g_rearmMode at map start\n'
    '\tint\t\t\tmutator;\t\t\t// running mutator (rmMutator_t)\n'
    '\tint\t\t\tmutatorEnd;\n'
    '\tint\t\t\tnextMutator;\n'
    '\tint\t\t\tlastMutator;\n'
    '\tint\t\t\tjuggernaut;\t\t\t// client number or -1\n')
sub(g('g_local.h'), '\tint\t\t\thitTarget;\t\t\t// hit confirm being gathered this frame\n',
    '\tint\t\t\tstreak;\t\t\t\t// kills this life\n\tint\t\t\tggLevel;\t\t\t// gun game ladder position\n'
    '\tint\t\t\thitTarget;\t\t\t// hit confirm being gathered this frame\n')
sub(g('g_local.h'), 'extern\tvmCvar_t\tg_offhandHook;\n',
    'extern\tvmCvar_t\tg_offhandHook;\nextern\tvmCvar_t\tg_rearmMode;\nextern\tvmCvar_t\tg_mutators;\n'
    'extern\tvmCvar_t\tg_mutatorInterval;\nextern\tvmCvar_t\tg_mutatorDuration;\n')
sub(g('g_local.h'), 'void CalculateRanks( void );\n', '''void CalculateRanks( void );

//
// g_rearmed.c
//
void G_RearmInit( void );
void G_RearmRunFrame( void );
void G_RearmSpawnLoadout( gentity_t *ent );
void G_RearmPlayerDie( gentity_t *self, gentity_t *attacker, int mod );
void G_RearmOnDamage( gentity_t *targ, gentity_t *attacker, int damage );
qboolean G_RearmHandlesScore( void );
qboolean G_RearmItemAllowed( gitem_t *item );
qboolean G_RearmDropsWeapons( void );
int G_RearmRailDamage( int damage );
float G_RearmGravityScale( void );
float G_RearmSpeedScale( void );
float G_RearmKnockbackScale( void );
int G_RearmHeadshotBelowEyes( void );
void LogExit( const char *string );
''')

# --- g_main.c: cvars, init, frame ---
sub(g('g_main.c'), 'vmCvar_t\tg_offhandHook;\n', 'vmCvar_t\tg_offhandHook;\nvmCvar_t\tg_rearmMode;\nvmCvar_t\tg_mutators;\nvmCvar_t\tg_mutatorInterval;\nvmCvar_t\tg_mutatorDuration;\n')
sub(g('g_main.c'), '\t{ &g_offhandHook, "g_offhandHook", "1", CVAR_ARCHIVE | CVAR_SERVERINFO, 0, qfalse },\n',
    '\t{ &g_offhandHook, "g_offhandHook", "1", CVAR_ARCHIVE | CVAR_SERVERINFO, 0, qfalse },\n'
    '\t{ &g_rearmMode, "g_rearmMode", "0", CVAR_ARCHIVE | CVAR_SERVERINFO | CVAR_LATCH, 0, qfalse },\n'
    '\t{ &g_mutators, "g_mutators", "1", CVAR_ARCHIVE | CVAR_SERVERINFO, 0, qfalse },\n'
    '\t{ &g_mutatorInterval, "g_mutatorInterval", "90", CVAR_ARCHIVE, 0, qfalse },\n'
    '\t{ &g_mutatorDuration, "g_mutatorDuration", "40", CVAR_ARCHIVE, 0, qfalse },\n')
sub(g('g_main.c'), '''	G_RemapTeamShaders();

	trap_SetConfigstring( CS_INTERMISSION, "" );
}''', '''	G_RemapTeamShaders();

	trap_SetConfigstring( CS_INTERMISSION, "" );

	G_RearmInit();
}''')
sub(g('g_main.c'), '''	// see if it is time to do a tournement restart
	CheckTournament();

	// see if it is time to end the level
	CheckExitRules();
''', '''	// see if it is time to do a tournement restart
	CheckTournament();

	// modes, mutators
	G_RearmRunFrame();

	// see if it is time to end the level
	CheckExitRules();
''')

# --- spawn loadout ---
sub(g('g_client.c'), '''			for (i = WP_NUM_WEAPONS - 1 ; i > 0 ; i--) {
				if (client->ps.stats[STAT_WEAPONS] & (1 << i)) {
					client->ps.weapon = i;
					break;
				}
			}''', '''			for (i = WP_NUM_WEAPONS - 1 ; i > 0 ; i--) {
				if (client->ps.stats[STAT_WEAPONS] & (1 << i)) {
					client->ps.weapon = i;
					break;
				}
			}
			// game mode / mutator loadouts
			G_RearmSpawnLoadout( ent );''')

# --- death + scoring ---
sub(g('g_combat.c'), '''	self->client->ps.persistant[PERS_KILLED]++;

	if (attacker && attacker->client) {
		attacker->client->lastkilled_client = self->s.number;

		if ( attacker == self || OnSameTeam (self, attacker ) ) {
			AddScore( attacker, self->r.currentOrigin, -1 );
		} else {
			AddScore( attacker, self->r.currentOrigin, 1 );
''', '''	self->client->ps.persistant[PERS_KILLED]++;

	// streaks, gun game ladder, juggernaut (these modes do their own scoring)
	G_RearmPlayerDie( self, attacker, meansOfDeath );

	if (attacker && attacker->client) {
		attacker->client->lastkilled_client = self->s.number;

		if ( attacker == self || OnSameTeam (self, attacker ) ) {
			if ( !G_RearmHandlesScore() ) {
				AddScore( attacker, self->r.currentOrigin, -1 );
			}
		} else {
			if ( !G_RearmHandlesScore() ) {
				AddScore( attacker, self->r.currentOrigin, 1 );
			}
''')
sub(g('g_combat.c'), '''	if ( weapon > WP_MACHINEGUN && weapon != WP_GRAPPLING_HOOK &&
		self->client->ps.ammo[ weapon ] ) {''', '''	if ( weapon > WP_MACHINEGUN && weapon != WP_GRAPPLING_HOOK &&
		self->client->ps.ammo[ weapon ] && G_RearmDropsWeapons() ) {''')
sub(g('g_combat.c'), '''		&& point[2] >= targ->r.currentOrigin[2] + targ->client->ps.viewheight - HEADSHOT_BELOW_EYES ) {''',
    '''		&& point[2] >= targ->r.currentOrigin[2] + targ->client->ps.viewheight - G_RearmHeadshotBelowEyes() ) {''')
sub(g('g_combat.c'), '''	if ( dflags & DAMAGE_NO_KNOCKBACK ) {
		knockback = 0;
	}
''', '''	if ( dflags & DAMAGE_NO_KNOCKBACK ) {
		knockback = 0;
	}
	knockback *= G_RearmKnockbackScale();
''')
sub(g('g_combat.c'), '''	// do the damage
	if (take) {
		targ->health = targ->health - take;''', '''	// vampire powerup / mutator
	if ( take ) {
		G_RearmOnDamage( targ, attacker, take );
	}

	// do the damage
	if (take) {
		targ->health = targ->health - take;''')

# --- items, physics, instagib ---
sub(g('g_items.c'), '''	RegisterItem( item );
	if ( G_ItemDisabled(item) )
		return;''', '''	RegisterItem( item );
	if ( G_ItemDisabled(item) || !G_RearmItemAllowed( item ) )
		return;''')
sub(g('g_active.c'), '\tclient->ps.gravity = g_gravity.value;\n', '\tclient->ps.gravity = g_gravity.value * G_RearmGravityScale();\n')
sub(g('g_active.c'), '\tclient->ps.speed = g_speed.value;\n', '\tclient->ps.speed = g_speed.value * G_RearmSpeedScale();\n')
sub(g('g_weapon.c'), '\tdamage = s_railDamage * s_quadFactor;\n', '\tdamage = G_RearmRailDamage( s_railDamage ) * s_quadFactor;\n')
sub(g('g_spawn.c'), '''	{ "ammo_shells", "ammo_belt" },
};''', '''	{ "ammo_shells", "ammo_belt" },
#ifndef MISSIONPACK
	{ "item_regen", "item_vampire" },
	{ "item_haste", "item_jetpack" },
	{ "item_invis", "item_rapidfire" },
#endif
};''')
print('rules game ok')
# (appended later) bot_rearm switch lives in g_main.c too, see p_bots2.py
