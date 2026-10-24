/*
===========================================================================
REARMED -- DESCENT round 3 SURPRISES (ds-surprise), ui side: PICK-YOUR-POISON floor modifiers.

Some fight / elite nodes of the floor map offer a modifier (deterministic from the run seed, so
the map never changes under you): a chip under the node ("[ ] ARMOURED  +25% SKULLS"). Click
it (or Tab to it, Space) to take it on; the info panel shows the risk and the reward. The
choice lives in the run key mod_on ("<layer>:<node mask>"); launching that node writes
node_mod (the game reads it) and, for SWARM, two more bots. g_dsModifiers 0: no offers.
Dev: dsmods (list the offers of the next floors), dsmodnode <reach index> [on|off].
ui_dsModAll 1 (tests): every eligible node offers one.
===========================================================================
*/
#include "ui_local.h"
#include "ui_ds_meta.h"

#define SURUI_CHIP_ID		( ID_RL_ITEM0 + 50 )	// + reach index
#define SURUI_UI_FIGHT		0		// ui_roguelike.c RLN_* (ui numbering)
#define SURUI_UI_ELITE		1
#define SURUI_OFFER_PCT		38		// % of eligible nodes that offer a modifier

typedef struct {
	rlButton_t	chip;
	int			L, i;
	int			mod;
} surUiChip_t;

static surUiChip_t	surUiChips[8];
static int			surUiNumChips;
static qboolean		surUiCvars;
static vec4_t		surUiRed = { 1.0f, 0.28f, 0.2f, 1.0f };
static vec4_t		surUiGold = { 1.0f, 0.8f, 0.3f, 1.0f };

static void SurUi_Cvars( void ) {
	if ( !surUiCvars ) {
		trap_Cvar_Register( NULL, "g_dsModifiers", "1", CVAR_ARCHIVE );
		trap_Cvar_Register( NULL, "ui_dsModAll", "0", 0 );
		surUiCvars = qtrue;
	}
}

static unsigned SurUi_Hash( unsigned seed, int L, int i ) {
	unsigned	h = seed * 2654435761u + (unsigned)L * 7919u + (unsigned)i * 104729u + 31337u;

	h ^= h >> 15;
	h *= 2246822519u;
	h ^= h >> 13;
	h *= 3266489917u;
	h ^= h >> 16;
	return h;
}

/*
=================
UI_DS_NodeMod

The modifier node i of layer L offers (0 none): fight / elite nodes from floor 2, not the
first floor of an act, about 38% of them
=================
*/
int UI_DS_NodeMod( int seed, int L, int i, int type ) {
	unsigned	h;
	int			f = L + 1;

	SurUi_Cvars();
	if ( !trap_Cvar_VariableValue( "g_dsModifiers" ) ) {
		return DSMOD_NONE;
	}
	if ( ( type != SURUI_UI_FIGHT && type != SURUI_UI_ELITE ) || f < 2 || DS_IsBossFloor( f ) ) {
		return DSMOD_NONE;
	}
	h = SurUi_Hash( (unsigned)seed, L, i );
	if ( (int)( h % 100u ) >= SURUI_OFFER_PCT && !trap_Cvar_VariableValue( "ui_dsModAll" ) ) {
		return DSMOD_NONE;
	}
	return 1 + (int)( ( h >> 8 ) % ( DSMOD_NUM - 1 ) );
}

// the player's choice for layer L: a node mask
static int SurUi_Mask( int L ) {
	const char	*s = RL_KVGet( &rl_run, "mod_on" );
	const char	*c;

	if ( !s[0] || atoi( s ) != L ) {
		return 0;
	}
	c = strchr( s, ':' );
	return c ? atoi( c + 1 ) : 0;
}

static void SurUi_SetMask( int L, int mask ) {
	RL_KVSet( &rl_run, "mod_on", va( "%i:%i", L, mask ) );
	RL_SaveRun();
}

qboolean UI_DS_NodeModOn( int L, int i ) {
	return ( SurUi_Mask( L ) & ( 1 << i ) ) != 0;
}

/*
=================
UI_DS_SurpriseLaunch

RL_LaunchNode, before the run is saved: node_mod (and SWARM's bots) for the node launched
=================
*/
void UI_DS_SurpriseLaunch( int seed, int L, int i, int type, int bots ) {
	int		mod = UI_DS_NodeMod( seed, L, i, type );

	if ( !mod || !UI_DS_NodeModOn( L, i ) ) {
		mod = DSMOD_NONE;
	}
	RL_KVSet( &rl_run, "node_mod", mod ? ds_modifiers[mod].key : "" );
	if ( mod && ds_modifiers[mod].extraBots > 0 ) {
		RL_KVSetInt( &rl_run, "node_bots", bots + ds_modifiers[mod].extraBots );
	}
	// room for an ambusher, the ghost and uprising fodder next to the floor's bots
	if ( trap_Cvar_VariableValue( "sv_maxclients" ) < 16 ) {
		trap_Cvar_SetValue( "sv_maxclients", 16 );
	}
	if ( mod ) {
		Com_Printf( "DESCENT: floor modifier %s (%s / %s)\n", ds_modifiers[mod].name, ds_modifiers[mod].risk, ds_modifiers[mod].reward );
	}
}

/*
===============================================================================

THE CHIPS ON THE FLOOR MAP

===============================================================================
*/

static void SurUi_ChipDraw( void *self ) {
	rlButton_t		*b = (rlButton_t *)self;
	menucommon_s	*g = &b->bm.generic;
	surUiChip_t		*c = NULL;
	qboolean		focus, on;
	float			x = g->left, y = g->top, w = g->right - g->left, h = g->bottom - g->top, p;
	vec4_t			fill, edge, text;
	int				k;

	for ( k = 0 ; k < surUiNumChips ; k++ ) {
		if ( &surUiChips[k].chip == b ) {
			c = &surUiChips[k];
		}
	}
	if ( !c ) {
		return;
	}
	focus = Menu_ItemAtCursor( g->parent ) == self;
	on = UI_DS_NodeModOn( c->L, c->i );
	p = RL_Pulse( 0.006f );
	if ( on ) {
		RL_Scale( fill, surUiRed, 0.35f + 0.1f * p, 0.9f );
		Vector4Copy( surUiRed, edge );
		Vector4Copy( rl_colBone, text );
	} else {
		Vector4Copy( rl_colBlack, fill );
		RL_Scale( edge, surUiRed, focus ? 0.9f : 0.45f, 1 );
		RL_Scale( text, surUiRed, focus ? 1.0f : 0.75f, 1 );
	}
	RL_Frame( x, y, w, h, fill, focus ? rl_colOrange : edge );
	RL_Text( x + 3, y + 2, on ? "[X]" : "[ ]", 4, 7, on ? surUiGold : text, 0 );
	RL_Text( x + 18, y + 2, ds_modifiers[c->mod].name, 4, 7, text, 0 );
}

static void SurUi_ChipEvent( void *ptr, int event ) {
	int		id, k, mask;

	if ( event != QM_ACTIVATED ) {
		return;
	}
	id = ( (menucommon_s *)ptr )->id;
	for ( k = 0 ; k < surUiNumChips ; k++ ) {
		if ( surUiChips[k].chip.bm.generic.id == id ) {
			mask = SurUi_Mask( surUiChips[k].L ) ^ ( 1 << surUiChips[k].i );
			SurUi_SetMask( surUiChips[k].L, mask );
			trap_S_StartLocalSound( rl_buySound, CHAN_LOCAL_SOUND );
			Com_Printf( "DESCENT: modifier %s %s on floor %i node %i\n", ds_modifiers[surUiChips[k].mod].name,
				( mask & ( 1 << surUiChips[k].i ) ) ? "taken" : "dropped", surUiChips[k].L + 1, surUiChips[k].i );
			return;
		}
	}
}

/*
=================
UI_DS_SurpriseMapReset / UI_DS_SurpriseAddChip

RL_MapMenu: a chip under every reachable node that offers a modifier (x, y = the node's
bottom centre)
=================
*/
void UI_DS_SurpriseMapReset( void ) {
	surUiNumChips = 0;
}

void UI_DS_SurpriseAddChip( menuframework_s *menu, int seed, int k, int L, int i, int type, float x, float y ) {
	surUiChip_t	*c;
	int			mod = UI_DS_NodeMod( seed, L, i, type );
	float		w;

	if ( !mod || surUiNumChips >= (int)ARRAY_LEN( surUiChips ) ) {
		return;
	}
	c = &surUiChips[surUiNumChips++];
	memset( c, 0, sizeof( *c ) );
	c->L = L;
	c->i = i;
	c->mod = mod;
	w = 24 + strlen( ds_modifiers[mod].name ) * 4;
	RL_InitButton( &c->chip, SURUI_CHIP_ID + k, x - w * 0.5f, y + 3, w, 11, ds_modifiers[mod].name, SurUi_ChipEvent );
	c->chip.bm.generic.ownerdraw = SurUi_ChipDraw;
	Menu_AddItem( menu, &c->chip );
}

// RL_PanelNode: the chip under the cursor stands for its node
qboolean UI_DS_SurpriseChipNode( int id, int *L, int *i ) {
	int		k;

	for ( k = 0 ; k < surUiNumChips ; k++ ) {
		if ( surUiChips[k].chip.bm.generic.id == id ) {
			*L = surUiChips[k].L;
			*i = surUiChips[k].i;
			return qtrue;
		}
	}
	return qfalse;
}

/*
=================
UI_DS_SurprisePanel

RL_DrawInfoPanel, under the node's reward: the modifier on offer, its risk and its reward
=================
*/
float UI_DS_SurprisePanel( int seed, int L, int i, int type, float x, float y, float w, qboolean reachable ) {
	int		mod = UI_DS_NodeMod( seed, L, i, type );
	qboolean	on;

	if ( !mod ) {
		return y;
	}
	on = UI_DS_NodeModOn( L, i );
	y += 4;
	UI_FillRect( x + 8, y, w - 16, 1, rl_colLine );
	y += 4;
	RL_Text( x + 8, y, va( "POISON: %s", ds_modifiers[mod].name ), 5, 8, surUiRed, 0 );
	RL_Text( x + w - 8, y, on ? "TAKEN" : "OFF", 5, 8, on ? surUiGold : rl_colDim, 2 );
	y += 10;
	y = RL_TextWrap( x + 8, y, w - 16, ds_modifiers[mod].risk, 4, 7, rl_colDim, 2 );
	RL_Text( x + 8, y, va( "REWARD %s", ds_modifiers[mod].reward ), 5, 8, surUiGold, 0 );
	y += 10;
	if ( reachable ) {
		y = RL_TextWrap( x + 8, y, w - 16, on ? "Click its chip to drop it." : "Click the chip under the node to take it.", 4, 7, rl_colDim, 2 );
	}
	return y;
}

/*
=================
UI_DS_SurpriseCommand

Dev: dsmods [floors] (the offers), dsmodnode <reach index> [on|off] (toggle on the current floor)
=================
*/
qboolean UI_DS_SurpriseCommand( const char *cmd, int seed, int layer, const int *reach, int numReach, const int *types ) {
	char	arg[16];
	int		k, mod, mask;

	if ( !Q_stricmp( cmd, "dsmodnode" ) ) {
		Q_strncpyz( arg, UI_Argv( 1 ), sizeof( arg ) );
		k = atoi( arg );
		if ( k < 0 || k >= numReach ) {
			Com_Printf( "dsmodnode: no reachable node %i\n", k );
			return qtrue;
		}
		mod = UI_DS_NodeMod( seed, layer, reach[k], types[k] );
		if ( !mod ) {
			Com_Printf( "dsmodnode: node %i offers no modifier\n", k );
			return qtrue;
		}
		mask = SurUi_Mask( layer );
		Q_strncpyz( arg, UI_Argv( 2 ), sizeof( arg ) );
		if ( !Q_stricmp( arg, "off" ) ) {
			mask &= ~( 1 << reach[k] );
		} else {
			mask |= 1 << reach[k];
		}
		SurUi_SetMask( layer, mask );
		Com_Printf( "dsmodnode: floor %i node %i %s %s\n", layer + 1, reach[k], ds_modifiers[mod].name,
			( mask & ( 1 << reach[k] ) ) ? "ON" : "off" );
		return qtrue;
	}
	return qfalse;
}
