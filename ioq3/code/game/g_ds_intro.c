/*
===========================================================================
REARMED -- DESCENT "announce the enemies" (ds-intro), game side.

G_RL_InitBots plans the floor's bots in G_InitGame, before any client gets the gamestate.
G_DS_IntroPublish puts that plan in CS_DSINTRO (bg_descent.h) so the cgame can show the
opponents on the loading screen (portrait, name, threat note) and run the versus intro at the
floor start (cgame/cg_ds_intro.c). The boss's type, traits and lives come from CS_DESCENTBOSS,
which G_DS_ActsInit already set by then.
===========================================================================
*/
#include "g_local.h"
#include "bg_ds_types.h"

#define DSI_MAX_FOES	12

void G_DS_IntroPublish( void ) {
	char		s[MAX_INFO_STRING];
	char		model[MAX_QPATH], name[32], entry[160], key[8];
	const char	*nm, *info, *nem;
	int			i, n, role, flags, lvl, evo, nemKills, act, floor;

	s[0] = 0;
	n = G_RL_PlanNum();
	G_DS_ArrivePlan();		// ds-arrivals: who comes in at the start, who joins later (CS_DSARRIVE)
	if ( !G_RL_Active() || n <= 0 ) {
		trap_SetConfigstring( CS_DSINTRO, "" );
		return;
	}
	if ( n > DSI_MAX_FOES ) {
		n = DSI_MAX_FOES;
	}
	floor = G_RL_Floor();
	act = DS_ActForFloor( floor < 1 ? 1 : floor );
	nem = G_DS_NemesisPlanName( 0 );
	nemKills = atoi( G_RL_RunGet( "nemesis_kills" ) );
	Info_SetValueForKey( s, "n", va( "%i", n ) );
	for ( i = 0 ; i < n ; i++ ) {
		nm = G_RL_PlanName( i, &role );
		if ( !nm ) {
			continue;
		}
		Q_strncpyz( name, nm, sizeof( name ) );
		Q_CleanStr( name );
		info = G_GetBotInfoByName( role == 4 ? G_DS_BossBotName() : name );
		Q_strncpyz( model, info ? Info_ValueForKey( info, "model" ) : "", sizeof( model ) );
		if ( !model[0] ) {
			Q_strncpyz( model, "visor/default", sizeof( model ) );
		}
		flags = 0;
		if ( nem && role != 4 && !Q_stricmp( nem, name ) ) {
			flags |= DSI_NEMESIS;
		}
		if ( G_DS_IsBossFloor() && role != 4 ) {
			flags |= DSI_RIVAL;
		}
		lvl = G_DS_BotLevelPreview( floor, role );
		evo = 0;
		if ( role == 4 ) {
			evo = EVO_ARMAGEDDON + ( act >= 4 ? 1 : 0 );
		} else if ( role == 3 && act >= 3 ) {
			evo = 3;
		}
		// en-types: an archetype shows its type's model (the mimic: the player's, the cgame picks it)
		if ( G_DS_TypesPlanArch( i ) > 0 && ds_types[G_DS_TypesPlanArch( i )].model[0] && ds_types[G_DS_TypesPlanArch( i )].model[0] != '*' ) {
			Q_strncpyz( model, ds_types[G_DS_TypesPlanArch( i )].model, sizeof( model ) );
		}
		Com_sprintf( entry, sizeof( entry ), "%i,%i,%i,%i,%i,%i,%i,%i,%s,%s", role, flags, lvl,
			G_RL_PlanCards( i, RLK_ABILITY ), G_RL_PlanCards( i, RLK_ALT ), G_RL_PlanCards( i, RLK_PERK ),
			evo, ( flags & DSI_NEMESIS ) ? nemKills : 0, model, name );
		Com_sprintf( key, sizeof( key ), "f%i", i );
		Info_SetValueForKey( s, key, entry );
		if ( G_DS_TypesPlanArch( i ) > 0 ) {
			Com_sprintf( key, sizeof( key ), "t%i", i );
			Info_SetValueForKey( s, key, va( "%i", G_DS_TypesPlanArch( i ) ) );	// en-types: the archetype (DSAR_)
		}
		if ( G_DS_AffixPlanMask( i ) ) {
			Com_sprintf( key, sizeof( key ), "a%i", i );
			Info_SetValueForKey( s, key, va( "%i", G_DS_AffixPlanMask( i ) ) );	// en-affix: the elite's affixes
		}
	}
	G_DS_RankIntro( s, sizeof( s ) );		// en-personality: \vr\ veteran ranks, \vm\ met flags, \nl\ \nt\ nemesis level + title
	trap_SetConfigstring( CS_DSINTRO, s );
	G_LogPrintf( "DSIntro: %s\n", s );
}
