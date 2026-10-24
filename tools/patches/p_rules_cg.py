"""Rules layer, cgame + ui hooks for cg_rearmed.c and the start server menu."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from patch import sub

ROOT = pathlib.Path(__file__).resolve().parents[2] / 'ioq3' / 'code'
cg = lambda f: str(ROOT / 'cgame' / f)
ui = lambda f: str(ROOT / 'q3_ui' / f)

# --- cg_local.h ---
sub(cg('cg_local.h'), '\tint\t\t\tairshotTime;\n', '\tint\t\t\tairshotTime;\n\tchar\t\trmBanner[128];\t\t// rearmed announcement\n\tint\t\t\trmBannerTime;\n\tint\t\t\trmBannerKind;\n')
sub(cg('cg_local.h'), '\tsfxHandle_t\tbfgZapSound;\n', '''	sfxHandle_t	bfgZapSound;
	qhandle_t	vampireShell;
	qhandle_t	rapidShell;
	qhandle_t	vampireItem;
	qhandle_t	rapidItem;
	qhandle_t	jetpackItem;
	qhandle_t	flameShader;
	sfxHandle_t	annStreak;
	sfxHandle_t	annMutator;
	sfxHandle_t	annJuggernaut;
	sfxHandle_t	annGunGame;
	sfxHandle_t	annInfo;
''')
sub(cg('cg_local.h'), '\tfloat\t\t\tscreenXStretch;\t// full-width x scale, for full screen fills\n',
    '\tfloat\t\t\tscreenXStretch;\t// full-width x scale, for full screen fills\n'
    '\tint\t\t\t\trmMode;\t\t\t// rearmed game mode (CS_REARMED)\n'
    '\tint\t\t\t\trmMutator;\n'
    '\tint\t\t\t\trmMutatorEnd;\n'
    '\tint\t\t\t\trmJuggernaut;\n')
sub(cg('cg_local.h'), 'void CG_WeaponKick( int weapon );\n', '''void CG_WeaponKick( int weapon );

//
// cg_rearmed.c
//
void CG_RearmRegister( void );
void CG_RearmParseConfigstring( void );
qboolean CG_RearmServerCommand( const char *cmd );
void CG_DrawRearmed( void );
void CG_RearmPowerupShells( refEntity_t *ent, entityState_t *state );
void CG_RearmItemShader( gitem_t *item, refEntity_t *ent );
void CG_RearmHead( refEntity_t *head );
void CG_RearmPlayerFX( centity_t *cent, refEntity_t *head );
''')

# --- init / configstrings / commands ---
sub(cg('cg_main.c'), '\tCG_RegisterWeapon( WP_GRAPPLING_HOOK );\t// offhand hook\n',
    '\tCG_RegisterWeapon( WP_GRAPPLING_HOOK );\t// offhand hook\n\tCG_RearmRegister();\n')
sub(cg('cg_main.c'), '\tCG_ParseServerinfo();\n', '\tCG_ParseServerinfo();\n\tCG_RearmParseConfigstring();\n')
sub(cg('cg_servercmds.c'), '''	if ( num == CS_MUSIC ) {''', '''	if ( num == CS_REARMED ) {
		CG_RearmParseConfigstring();
	} else if ( num == CS_MUSIC ) {''')
sub(cg('cg_servercmds.c'), '''	if ( !strcmp( cmd, "cp" ) ) {''', '''	if ( CG_RearmServerCommand( cmd ) ) {
		return;
	}

	if ( !strcmp( cmd, "cp" ) ) {''')

# --- visuals ---
sub(cg('cg_draw.c'), '''			CG_DrawHeadshot();
			CG_DrawHitFeedback();''', '''			CG_DrawHeadshot();
			CG_DrawHitFeedback();
			CG_DrawRearmed();''')
sub(cg('cg_players.c'), '''		if ( state->powerups & ( 1 << PW_BATTLESUIT ) ) {
			ent->customShader = cgs.media.battleSuitShader;
			trap_R_AddRefEntityToScene( ent );
		}''', '''		if ( state->powerups & ( 1 << PW_BATTLESUIT ) ) {
			ent->customShader = cgs.media.battleSuitShader;
			trap_R_AddRefEntityToScene( ent );
		}
		CG_RearmPowerupShells( ent, state );''')
sub(cg('cg_players.c'), '''	CG_PositionRotatedEntityOnTag( &head, &torso, ci->torsoModel, "tag_head");

	head.shadowPlane = shadowPlane;
	head.renderfx = renderfx;

	CG_AddRefEntityWithPowerups( &head, &cent->currentState, ci->team );''', '''	CG_PositionRotatedEntityOnTag( &head, &torso, ci->torsoModel, "tag_head");
	CG_RearmHead( &head );

	head.shadowPlane = shadowPlane;
	head.renderfx = renderfx;

	CG_AddRefEntityWithPowerups( &head, &cent->currentState, ci->team );
	CG_RearmPlayerFX( cent, &head );''')
sub(cg('cg_ents.c'), '''	ent.hModel = cg_items[es->modelindex].models[0];
''', '''	ent.hModel = cg_items[es->modelindex].models[0];
	CG_RearmItemShader( item, &ent );
''')

# --- start server menu: mode + mutators replace the punkbuster control ---
sub(ui('ui_startserver.c'), '''static const char *dedicated_list[] = {''', '''static const char *rearmMode_list[] = {
	"Normal",
	"Gun Game",
	"Juggernaut",
	"Instagib",
	NULL
};

static const char *dedicated_list[] = {''')
sub(ui('ui_startserver.c'), '\tmenulist_s\t\tpunkbuster;\n', '\tmenulist_s\t\tpunkbuster;\n\tmenulist_s\t\t\trearmMode;\n\tmenuradiobutton_s\tmutators;\n')
sub(ui('ui_startserver.c'), '''	y += BIGCHAR_HEIGHT+2;
	s_serveroptions.punkbuster.generic.type			= MTYPE_SPINCONTROL;''', '''	y += BIGCHAR_HEIGHT+2;
	s_serveroptions.rearmMode.generic.type		= MTYPE_SPINCONTROL;
	s_serveroptions.rearmMode.generic.name		= "Mode:";
	s_serveroptions.rearmMode.generic.flags		= QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	s_serveroptions.rearmMode.generic.x			= OPTIONS_X;
	s_serveroptions.rearmMode.generic.y			= y;
	s_serveroptions.rearmMode.itemnames			= rearmMode_list;

	y += BIGCHAR_HEIGHT+2;
	s_serveroptions.mutators.generic.type		= MTYPE_RADIOBUTTON;
	s_serveroptions.mutators.generic.name		= "Mutators:";
	s_serveroptions.mutators.generic.flags		= QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	s_serveroptions.mutators.generic.x			= OPTIONS_X;
	s_serveroptions.mutators.generic.y			= y;

	y += BIGCHAR_HEIGHT+2;
	s_serveroptions.punkbuster.generic.type			= MTYPE_SPINCONTROL;''')
sub(ui('ui_startserver.c'), '''	Menu_AddItem( &s_serveroptions.menu, &s_serveroptions.pure );
''', '''	Menu_AddItem( &s_serveroptions.menu, &s_serveroptions.pure );
	Menu_AddItem( &s_serveroptions.menu, &s_serveroptions.rearmMode );
	Menu_AddItem( &s_serveroptions.menu, &s_serveroptions.mutators );
''')
sub(ui('ui_startserver.c'), '''	s_serveroptions.pure.curvalue = Com_Clamp( 0, 1, trap_Cvar_VariableValue( "sv_pure" ) );''',
    '''	s_serveroptions.pure.curvalue = Com_Clamp( 0, 1, trap_Cvar_VariableValue( "sv_pure" ) );
	s_serveroptions.rearmMode.curvalue = Com_Clamp( 0, 3, trap_Cvar_VariableValue( "g_rearmMode" ) );
	s_serveroptions.mutators.curvalue = Com_Clamp( 0, 1, trap_Cvar_VariableValue( "g_mutators" ) );''')
sub(ui('ui_startserver.c'), '''	trap_Cvar_SetValue( "sv_pure", pure );''', '''	trap_Cvar_SetValue( "sv_pure", pure );
	trap_Cvar_SetValue( "g_rearmMode", s_serveroptions.rearmMode.curvalue );
	trap_Cvar_SetValue( "g_mutators", s_serveroptions.mutators.curvalue );''')
print('rules cg ok')
