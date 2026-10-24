"""v2 bots: dash charges, jetpack void safety, mode awareness (juggernaut / bounty targets,
no retreating in fixed-weapon modes)."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from patch import sub

ROOT = pathlib.Path(__file__).resolve().parents[2] / 'ioq3'
g = lambda f: str(ROOT / 'code' / 'game' / f)

A = g('ai_rearmed.c')
sub(A, '''	if ( enemy && visible && ps->stats[STAT_DASH_TIME] <= 0 && level.time > bs->rmNextDodge ) {''',
    '''	if ( enemy && visible && DASH_READY( ps ) && level.time > bs->rmNextDodge ) {''')
sub(A, '''	// jetpack: climb towards enemies above us, hop around otherwise
	if ( ps->powerups[PW_JETPACK] ) {
		if ( ( enemy && enemy->r.currentOrigin[2] > ps->origin[2] + 48 ) || ( ( level.time / 700 ) % 3 == 0 ) ) {
			ucmd->upmove = 127;
		}
	}''', '''	// jetpack: climb towards enemies above us, never drop into a pit, hop around otherwise
	if ( ps->powerups[PW_JETPACK] ) {
		if ( ( enemy && enemy->r.currentOrigin[2] > ps->origin[2] + 48 ) || !BotRearmGroundBelow( bs, 700 )
			|| ( ( level.time / 700 ) % 3 == 0 && BotRearmGroundBelow( bs, 200 ) ) ) {
			ucmd->upmove = 127;
		}
	}''')
sub(A, '''/*
==================
BotRearmEnemiesNear''', '''/*
==================
BotRearmGroundBelow

Is there floor (not a pit or a hurt trigger's void) within dist below the bot?
==================
*/
static qboolean BotRearmGroundBelow( bot_state_t *bs, float dist ) {
	trace_t	tr;
	vec3_t	end;

	VectorCopy( bs->cur_ps.origin, end );
	end[2] -= dist;
	trap_Trace( &tr, bs->cur_ps.origin, NULL, NULL, end, bs->client, MASK_PLAYERSOLID );
	return tr.fraction < 1.0f && !( tr.surfaceFlags & SURF_SKY );
}

/*
==================
BotRearmPriorityTarget

The juggernaut and players with a bounty are worth chasing across the map
==================
*/
qboolean BotRearmPriorityTarget( bot_state_t *bs, int client ) {
	if ( client < 0 || client >= MAX_CLIENTS || client == bs->client ) {
		return qfalse;
	}
	return client == level.juggernaut || level.clients[client].bounty;
}

/*
==================
BotRearmAggressive

Modes where running away for health and weapons doesn't make sense
==================
*/
qboolean BotRearmAggressive( bot_state_t *bs ) {
	if ( !trap_Cvar_VariableIntegerValue( "bot_rearm" ) ) {
		return qfalse;
	}
	return level.rmMode == RM_MODE_GUNGAME || level.rmMode == RM_MODE_INSTAGIB || level.rmMode == RM_MODE_OITC
		|| bs->client == level.juggernaut;
}

/*
==================
BotRearmEnemiesNear''')

sub(g('ai_main.h'), 'void BotRearmInput(bot_state_t *bs, usercmd_t *ucmd);\n',
    'void BotRearmInput(bot_state_t *bs, usercmd_t *ucmd);\nqboolean BotRearmPriorityTarget(bot_state_t *bs, int client);\nqboolean BotRearmAggressive(bot_state_t *bs);\n')

D = g('ai_dmq3.c')
sub(D, '''int BotWantsToRetreat(bot_state_t *bs) {
	aas_entityinfo_t entinfo;
''', '''int BotWantsToRetreat(bot_state_t *bs) {
	aas_entityinfo_t entinfo;

	//rearmed modes where there is nothing to go and get
	if (BotRearmAggressive(bs) || BotRearmPriorityTarget(bs, bs->enemy))
		return qfalse;
''')
sub(D, '''int BotWantsToChase(bot_state_t *bs) {
	aas_entityinfo_t entinfo;
''', '''int BotWantsToChase(bot_state_t *bs) {
	aas_entityinfo_t entinfo;

	if (BotRearmAggressive(bs) || BotRearmPriorityTarget(bs, bs->enemy))
		return qtrue;
''')
sub(D, '''	if (curenemy >= 0) {
		BotEntityInfo(curenemy, &curenemyinfo);
		if (EntityCarriesFlag(&curenemyinfo)) return qfalse;''', '''	if (curenemy >= 0) {
		BotEntityInfo(curenemy, &curenemyinfo);
		if (EntityCarriesFlag(&curenemyinfo)) return qfalse;
		//stay on the juggernaut / bounty target
		if (BotRearmPriorityTarget(bs, curenemy)) return qfalse;''')
sub(D, '''		//if this entity is not carrying a flag
		if (!EntityCarriesFlag(&entinfo))
		{''', '''		//if this entity is not carrying a flag (or a bounty, or the juggernaut's crown)
		if (!EntityCarriesFlag(&entinfo) && !BotRearmPriorityTarget(bs, i))
		{''')
print('v2 bots ok')
