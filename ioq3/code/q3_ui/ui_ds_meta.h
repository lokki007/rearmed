/*
===========================================================================
REARMED -- DESCENT UI shared between ui_roguelike.c (hub plumbing, floor map,
cards) and ui_ds_meta.c (DESCENT v2 META: hub, characters, upgrade tree,
collection, run summary, records + history, unlocks).
===========================================================================
*/
#ifndef UI_DS_META_H
#define UI_DS_META_H

#define RL_RUN_FILE			"rearmed_run.txt"
#define RL_META_FILE		"rearmed_meta.txt"
#define RL_HISTORY_FILE		"rearmed_history.txt"

#define RL_MAX_KV			160		// keys per file (the run file passes 64 with the v2 keys)
#define RL_KEY_LEN			32
#define RL_VAL_LEN			512

#define ID_RL_BACK			10
#define ID_RL_CONTINUE		11
#define ID_RL_NEWRUN		12
#define ID_RL_UPGRADES		13
#define ID_RL_RECORDS		14
#define ID_RL_COLLECTION	15
#define ID_RL_HISTORY		16
#define ID_RL_ABYSS			17
#define ID_RL_DIFFICULTY	18		// ds-economy: floor map difficulty picker
#define ID_RL_ABANDON		19		// ds-economy: hub ABANDON DESCENT
#define ID_RL_RESET			20		// ds-economy: records RESET ALL PROGRESS
#define ID_RL_SKILLS		21		// ds-skills: hub SKILLS
#define ID_RL_PRACTICE		22		// en-boss: hub BOSS PRACTICE
#define ID_RL_DAILY			23		// en-daily: hub DAILY CHALLENGE
#define ID_RL_HALL			26		// en-memory: records NEMESIS HALL
#define ID_RL_ITEM0			100

typedef struct {
	int		num;
	char	key[RL_MAX_KV][RL_KEY_LEN];
	char	val[RL_MAX_KV][RL_VAL_LEN];
} rlKV_t;

// one clickable box; bm must stay the first member, the menu code only sees a menubitmap_s
typedef struct {
	menubitmap_s	bm;
	char			label[48];
	char			sub[96];
	qhandle_t		icon;
	int				data;
	qboolean		center;
} rlButton_t;

/*
-------------------------------------------------------------------------------
ui_roguelike.c
-------------------------------------------------------------------------------
*/
extern rlKV_t		rl_run;
extern rlKV_t		rl_meta;
extern float		rl_ext;
extern qboolean		rl_hubDirty;
extern qhandle_t	rl_shRing;
extern qhandle_t	rl_shGlow;
extern qhandle_t	rl_skillIcons[5];
extern sfxHandle_t	rl_buySound;
extern const char	*rl_skillNames[5];

extern vec4_t	rl_colBack;
extern vec4_t	rl_colShade;
extern vec4_t	rl_colPanel;
extern vec4_t	rl_colPanelHi;
extern vec4_t	rl_colLine;
extern vec4_t	rl_colOrange;
extern vec4_t	rl_colBone;
extern vec4_t	rl_colDim;
extern vec4_t	rl_colDark;
extern vec4_t	rl_colBanner;
extern vec4_t	rl_colGood;
extern vec4_t	rl_colBad;
extern vec4_t	rl_colBlack;

void RL_KVLoad( rlKV_t *kv, const char *name );
void RL_KVSave( rlKV_t *kv, const char *name );
const char *RL_KVGet( rlKV_t *kv, const char *key );
int RL_KVInt( rlKV_t *kv, const char *key );
void RL_KVSet( rlKV_t *kv, const char *key, const char *val );
void RL_KVSetInt( rlKV_t *kv, const char *key, int val );
int RL_ParseInts( const char *s, int *out, int max );
void RL_LoadRun( void );
void RL_LoadMeta( void );
void RL_SaveRun( void );
void RL_SaveMeta( void );
int RL_BaseLives( int difficulty );
void RL_RegisterCvars( void );
qboolean RL_InGame( void );
void RL_LaunchNode( int L, int i );
void RL_StartRun( int difficulty );
void RL_OpenRun( void );
void RL_MapMenu( void );
void RL_NewRunMenu( void );
void RL_PopTop( void );
void RL_Layout( void );
void RL_Text( float x, float y, const char *str, float cw, float ch, const float *color, int align );
void RL_TextS( float x, float y, const char *str, float cw, float ch, const float *color, int align );
float RL_TextWrap( float x, float y, float w, const char *str, float cw, float ch, const float *color, int maxLines );
void RL_Frame( float x, float y, float w, float h, const float *fill, const float *edge );
void RL_Pic( float x, float y, float w, float h, qhandle_t shader, const float *color );
float RL_Pulse( float speed );
void RL_Scale( vec4_t out, const float *in, float k, float alpha );
void RL_Backdrop( qboolean opaque );
void RL_Header( const char *title, const char *sub );
void RL_DrawSkulls( int skulls );
const char *RL_TimeString( int msec );
void RL_ButtonDraw( void *self );
void RL_InitButton( rlButton_t *b, int id, float x, float y, float w, float h, const char *label, void (*callback)( void *, int ) );
int RL_KeyRemap( int key );
sfxHandle_t RL_MenuKey( int key );
void RL_Footer( const char *text );
void RL_SetupMenu( menuframework_s *menu, void (*draw)( void ), qboolean ingame );
void RL_DrawBuild( float x, float top, float w, float bottom );

/*
-------------------------------------------------------------------------------
ui_ds_meta.c
-------------------------------------------------------------------------------
*/
void UI_DS_Cache( void );
void UI_DS_CharacterMenu( int difficulty );		// after the difficulty pick: starts the run
void UI_DS_BeforeNewRun( void );				// RL_StartRun, before the old run file is replaced
void UI_DS_StartRunKeys( void );				// RL_StartRun, before the new run is saved: v2 keys, kit
qboolean UI_DS_OpenRun( void );					// RL_OpenRun: qtrue = a META screen took over
void UI_DS_SummaryMenu( void );					// the run is over (rlover)
void UI_DS_VictoryMenu( void );					// the act 4 boss fell (rlvictory)
void UI_DS_UpgradesMenu( void );
void UI_DS_RecordsMenu( void );
void UI_DS_HistoryMenu( void );
void UI_DS_CollectionMenu( void );
qboolean UI_DS_BuyKey( const char *key, char *msg, int msgSize );
qboolean UI_DS_ConsoleCommand( const char *cmd );
void UI_DS_AbandonRun( void );					// (ds-economy) ends the loaded run as a loss, banks it, the summary

/*
-------------------------------------------------------------------------------
ui_ds_econ.c (ds-economy): difficulty switch, records difficulty, reset, abandon
-------------------------------------------------------------------------------
*/
void UI_DS_EconRegisterCvars( void );			// RL_RegisterCvars: g_fragMult (archived)
void UI_DS_EconStartRun( int difficulty );		// RL_StartRun: diff_start
void UI_DS_EconLaunch( void );					// RL_LaunchNode: diff_min / diff_max = the difficulties played
int UI_DS_RecordDifficulty( void );				// the lowest difficulty a floor was played on (records)
const char *UI_DS_HistoryDifficulty( void );	// "3" or "2-4" (a mixed run)
float UI_DS_EconSummary( float x, float y, float w );	// run summary lines: rewards multiplier, records as
void UI_DS_DifficultyMenu( void );				// the floor map's difficulty picker
void UI_DS_SmallButton( rlButton_t *b );		// compact ownerdraw (ABANDON / RESET)

/*
-------------------------------------------------------------------------------
ui_ds_surprise.c (ds-surprise): pick-your-poison floor modifiers on the floor map
-------------------------------------------------------------------------------
*/
int UI_DS_NodeMod( int seed, int L, int i, int type );	// the DSMOD_ node i of layer L offers (0 none)
qboolean UI_DS_NodeModOn( int L, int i );				// the player took it (run key mod_on)
void UI_DS_SurpriseLaunch( int seed, int L, int i, int type, int bots );	// RL_LaunchNode: node_mod, SWARM bots, sv_maxclients
void UI_DS_SurpriseMapReset( void );					// RL_MapMenu, before the chips
void UI_DS_SurpriseAddChip( menuframework_s *menu, int seed, int k, int L, int i, int type, float x, float y );	// the chip under a reachable node
qboolean UI_DS_SurpriseChipNode( int id, int *L, int *i );	// RL_PanelNode: a chip stands for its node
float UI_DS_SurprisePanel( int seed, int L, int i, int type, float x, float y, float w, qboolean reachable );	// RL_DrawInfoPanel
qboolean UI_DS_SurpriseCommand( const char *cmd, int seed, int layer, const int *reach, int numReach, const int *types );	// dev: dsmodnode
void UI_DS_InitDiffButton( rlButton_t *b, int id, float x, float y, float w, float h, void (*callback)( void *, int ) );
qboolean UI_DS_ResetProgress( void );			// backup + empty meta / run / history / skills
void UI_DS_ResetMenu( void );					// RESET ALL PROGRESS: the first of two confirm screens
void UI_DS_AbandonMenu( void );					// ABANDON DESCENT: confirm, then UI_DS_AbandonRun
const char *UI_DS_EconMessage( void );			// the last reset result ("" none)
qboolean UI_DS_EconCommand( const char *cmd );	// dev: dsdiff dsdiffmenu dsreset dsabandon
void UI_DS_EconMigrate( void );					// economy pass 2: refunds for retired upgrades, once (rl_meta loaded)
void UI_DS_EconTreeLine( float x, float y, float w );	// upgrades screen: the tree's progress

/*
-------------------------------------------------------------------------------
ui_ds_skills.c (ds-skills): the SKILLS screen, the skill xp merge, the summary block
-------------------------------------------------------------------------------
*/
qboolean UI_DS_SkillsMerge( void );				// rearmed_skills.txt pending xp -> meta sk_xp, once per batch (leaves rl_meta loaded)
void UI_DS_SkillsMenu( int focusSkill );		// the screen (-1: a perk that can be taken)
void UI_DS_SkillsCache( void );
const char *UI_DS_SkillsHubLine( void );		// the hub button's line (meta loaded)
int UI_DS_SkillsPoints( void );					// unspent perk points (meta loaded)
float UI_DS_SkillsSummary( float x, float y, float w );	// run summary: skills trained + perk points earned
qboolean UI_DS_SkillsCommand( const char *cmd );	// dev: dsskillmerge

/*
-------------------------------------------------------------------------------
ui_ds_bestiary.c (round 4 en-bestiary): the BESTIARY tab of the Collection, the meta merge
-------------------------------------------------------------------------------
*/
void UI_DS_BestiaryMerge( void );				// DSM_MergeFloor: run bst_fm / bst_fk -> meta bst_* (caller saves)
int UI_DS_BestiaryItems( int *out );			// the tab's cells (bg_ds_bestiary.h entries)
int UI_DS_BestiaryGroup( int e );				// en-screens: 0 types, 1 affixes, 2 champion, 3 mini-bosses, 4 bosses (a row each)
qboolean UI_DS_BestiaryFound( int e );			// met at least once (meta loaded)
int UI_DS_BestiaryKills( int e );				// lifetime kills by the player
const char *UI_DS_BestiaryName( int e, qboolean found );
void UI_DS_BestiaryIcon( int e, qboolean found, float x, float y, float sz );
void UI_DS_BestiaryDetails( int e, qboolean found, float x, float ty, float w, float bottom );

/*
-------------------------------------------------------------------------------
ui_ds_hall.c (round 4 en-memory): Records > NEMESIS HALL (hall of fame + rivals), reads
rearmed_bots.txt (game/bg_ds_botmem.h; only the game writes it)
-------------------------------------------------------------------------------
*/
void UI_DS_HallMenu( int tab );					// 0 hall of fame, 1 rivals
qboolean UI_DS_HallCommand( const char *cmd );	// dshall [rivals | records]
void UI_DS_HallButton( rlButton_t *b );			// the Records screen's compact NEMESIS HALL button

/*
-------------------------------------------------------------------------------
ui_ds_bossprac.c (en-boss): BOSS PRACTICE, a timed 1v1 boss fight that saves nothing
-------------------------------------------------------------------------------
*/
void UI_DS_BossPracMenu( void );				// the boss picker (the hub's BOSS PRACTICE)
qboolean UI_DS_BossPracCommand( const char *cmd );	// dsbossprac [1-4], dspracend
void UI_DS_BossPracOff( void );					// clears g_dsPractice (main menu, a real floor launch)
void UI_DS_BossPracLaunch( int boss, const char *extra );	// en-rematch: a practice floor + extra practice-file keys

/*
-------------------------------------------------------------------------------
ui_ds_rematch.c (en-rematch): BOSS REMATCH, beaten bosses + harder modifiers, best times (rearmed_rematch.txt)
-------------------------------------------------------------------------------
*/
void UI_DS_RematchMenu( int boss );				// the rematch screen (boss 0-3, -1 = the last one)
qboolean UI_DS_RematchCommand( const char *cmd );	// dsrematch [boss mask], dsrmend (the game), dsrminfo

/*
-------------------------------------------------------------------------------
ui_ds_daily.c (en-daily): the DAILY CHALLENGE (game/bg_ds_daily.h). While UI_DS_DailyOn():
the run file is rearmed_daily_run.txt, the meta a blank profile that is never saved.
-------------------------------------------------------------------------------
*/
qboolean UI_DS_DailyOn( void );					// cvar ds_daily: a daily is being played
void UI_DS_DailySet( qboolean on );
qboolean UI_DS_DailyBlankMeta( void );			// RL_LoadMeta: qtrue = rl_meta is the blank daily profile
int UI_DS_DailyToday( void );					// YYYYMMDD (ds_dailyDate fakes it)
void UI_DS_DailyMenu( void );					// the daily screen (sets the flag)
qboolean UI_DS_DailyOpenRun( void );			// UI_DS_OpenRun while on: result / resume (qtrue = handled)
qboolean UI_DS_DailyMainMenu( void );			// UI_RL_MainMenuOpened: clears the flag, returns whether it was on
const char *UI_DS_DailyHubLine( void );			// the hub button's line
qboolean UI_DS_DailyCommand( const char *cmd );	// first in UI_RL_ConsoleCommand: rlover (daily), dsdaily*, "descent" clears

#endif // UI_DS_META_H
