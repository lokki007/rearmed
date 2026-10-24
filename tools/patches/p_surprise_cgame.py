"""ds-surprise: cgame hooks (one-shot, skips present edits)."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from patch import sub

C = pathlib.Path(__file__).resolve().parent.parent.parent / 'ioq3' / 'code' / 'cgame'


def edit(path, old, new, marker):
    s = open(path, newline='').read().replace('\r\n', '\n')
    if marker in s:
        print('skip (present):', path.name, marker)
        return
    sub(str(path), old, new)
    print('ok:', path.name, marker)


edit(C / 'cg_local.h', '''void CG_DS_ActsDraw2D( void );						// act intro, vault timer, boss bar
''', '''void CG_DS_ActsDraw2D( void );						// act intro, vault timer, boss bar
// ds-surprise (cg_ds_surprise.c): ambush pillars, the golden meteor, side callouts, BLACKOUT, the ghost's look
void CG_DS_SurpriseRegister( void );				// CG_Init
qboolean CG_DS_SurpriseEntity( centity_t *cent );	// CG_General: RM_ENT_SURPRISE
qboolean CG_DS_SurpriseServerCommand( const char *cmd );	// dsuann
void CG_DS_SurpriseDraw2D( void );					// CG_Draw2D: right-edge callouts, mini-boss / event line
void CG_DS_SurpriseDrawWorldFX( void );				// CG_Draw2D, under the HUD: BLACKOUT, BLOOD ECLIPSE
qboolean CG_DS_GhostRefEntity( refEntity_t *ent, entityState_t *state );	// CG_AddRefEntityWithPowerups (qtrue = drawn)
int CG_DS_SurpriseMod( void );						// the floor modifier (DSMOD_)
''', 'CG_DS_SurpriseRegister')
edit(C / 'cg_main.c', '''	CG_DS_FrenzyRegister();		// FRENZY: chain, crits, spikes, beams
''', '''	CG_DS_FrenzyRegister();		// FRENZY: chain, crits, spikes, beams
	CG_DS_SurpriseRegister();	// ds-surprise: ambushes, wild events, the ghost
''', 'CG_DS_SurpriseRegister')
edit(C / 'cg_ents.c', '''	if ( CG_DS_FrenzyEntity( cent ) ) {''', '''	if ( CG_DS_SurpriseEntity( cent ) ) {		// ds-surprise: landing pillars, the golden meteor
		return;
	}
	if ( CG_DS_FrenzyEntity( cent ) ) {''', 'CG_DS_SurpriseEntity')
edit(C / 'cg_servercmds.c', '''	if ( CG_DS_FrenzyServerCommand( cmd ) ) {''', '''	if ( CG_DS_SurpriseServerCommand( cmd ) ) {	// ds-surprise: dsuann (side callouts)
		return;
	}
	if ( CG_DS_FrenzyServerCommand( cmd ) ) {''', 'CG_DS_SurpriseServerCommand')
edit(C / 'cg_draw.c', '''	CG_DrawMutatorFX();		// rearmed: random event screen tints, slow motion (under the HUD)
''', '''	CG_DrawMutatorFX();		// rearmed: random event screen tints, slow motion (under the HUD)
	CG_DS_SurpriseDrawWorldFX();	// ds-surprise: BLACKOUT dark + glowing loot, BLOOD ECLIPSE edges (under the HUD)
''', 'CG_DS_SurpriseDrawWorldFX')
edit(C / 'cg_draw.c', '''	CG_DS_ActsDraw2D();	// descent: act intro, vault timer, boss health bar
''', '''	CG_DS_ActsDraw2D();	// descent: act intro, vault timer, boss health bar
	CG_DS_SurpriseDraw2D();	// ds-surprise: right-edge callouts (ambush, events, ghost), mini-boss / event line
''', 'CG_DS_SurpriseDraw2D')
edit(C / 'cg_players.c', '''void CG_AddRefEntityWithPowerups( refEntity_t *ent, entityState_t *state, int team ) {

	if ( state->powerups & ( 1 << PW_INVIS ) ) {''', '''void CG_AddRefEntityWithPowerups( refEntity_t *ent, entityState_t *state, int team ) {

	if ( CG_DS_GhostRefEntity( ent, state ) ) {
		return;		// ds-surprise: the ghost of a past run, pale and see-through
	}
	if ( state->powerups & ( 1 << PW_INVIS ) ) {''', 'CG_DS_GhostRefEntity')
print('done')
