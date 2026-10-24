"""v4 cgame hooks: style meter HUD, perk picker, records."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from patch import sub

ROOT = pathlib.Path(__file__).resolve().parents[2] / 'ioq3'
c = lambda f: str(ROOT / 'code' / 'cgame' / f)

sub(str(ROOT / 'cmake' / 'basegame.cmake'), '    ${SOURCE_DIR}/cgame/cg_rearmed.c\n',
    '    ${SOURCE_DIR}/cgame/cg_rearmed.c\n    ${SOURCE_DIR}/cgame/cg_style.c\n')

H = c('cg_local.h')
sub(H, '''	float		rmWallRoll;			// wall run camera roll
''', '''	float		rmWallRoll;			// wall run camera roll
	int			rmPerkOffer[3];		// perks on offer ([0] = -1 for none)
	int			rmPerks;			// PERK_ bits we hold
	qboolean	rmRecordChecked;	// the end-of-match record check ran
''')
sub(H, '''extern	vmCvar_t		cg_lowHealthFX;
''', '''extern	vmCvar_t		cg_lowHealthFX;
extern	vmCvar_t		cg_style;
''')
sub(H, '''void CG_RearmDrawOverlays( void );
''', '''void CG_RearmDrawOverlays( void );

//
// cg_style.c
//
void CG_StyleRegister( void );
void CG_DrawStyle( void );
void CG_DrawPerks( void );
qboolean CG_PerkPanelPick( int num );
void CG_RecordsObituary( int target, int attacker, int mod );
void CG_RecordsFrame( void );
void CG_DrawRecordsIntermission( void );
void CG_Records_f( void );
qboolean CG_StyleServerCommand( const char *cmd );
''')

M = c('cg_main.c')
sub(M, 'vmCvar_t\tcg_lowHealthFX;\n', 'vmCvar_t\tcg_lowHealthFX;\nvmCvar_t\tcg_style;\n')
sub(M, '''	{ &cg_lowHealthFX, "cg_lowHealthFX", "1", CVAR_ARCHIVE },
''', '''	{ &cg_lowHealthFX, "cg_lowHealthFX", "1", CVAR_ARCHIVE },
	{ &cg_style, "cg_style", "1", CVAR_ARCHIVE },
''')

R = c('cg_rearmed.c')
sub(R, '''	cgs.media.annMedal = trap_S_RegisterSound( "sound/rearmed/ann_medal.wav", qfalse );''',
    '''	CG_StyleRegister();
	cgs.media.annMedal = trap_S_RegisterSound( "sound/rearmed/ann_medal.wav", qfalse );''')
sub(R, '''	if ( !strcmp( cmd, "rmann" ) ) {
		kind = CG_Argv( 1 );''', '''	if ( CG_StyleServerCommand( cmd ) ) {
		return qtrue;
	}
	if ( !strcmp( cmd, "rmann" ) ) {
		kind = CG_Argv( 1 );''')
sub(R, '''			charges = DASH_CHARGES;''', '''			charges = DASH_CHARGE_COUNT( ps );''')
sub(R, '''	cx = cgs.glconfig.vidWidth * 0.5f;
	cy = cgs.glconfig.vidHeight * 0.5f;
''', '''	cx = cgs.glconfig.vidWidth * 0.5f;
	cy = cgs.glconfig.vidHeight * 0.5f;

	CG_DrawStyle();
	CG_DrawPerks();
''')

sub(c('cg_weapons.c'), '''	num = atoi( CG_Argv( 1 ) );

	if ( num < 1 || num > MAX_WEAPONS-1 ) {''', '''	num = atoi( CG_Argv( 1 ) );

	// a perk on offer takes 1-3
	if ( CG_PerkPanelPick( num ) ) {
		return;
	}

	if ( num < 1 || num > MAX_WEAPONS-1 ) {''')
sub(c('cg_event.c'), '''	mod = ent->eventParm;
''', '''	mod = ent->eventParm;
	CG_RecordsObituary( target, attacker, mod );
''')
sub(c('cg_view.c'), '''	CG_ProcessSnapshots();
''', '''	CG_ProcessSnapshots();
	CG_RecordsFrame();
''')
sub(c('cg_draw.c'), '''static void CG_DrawIntermission( void ) {
//	int key;''', '''static void CG_DrawIntermission( void ) {
//	int key;
	CG_DrawRecordsIntermission();''')
print('v4 cg ok')
