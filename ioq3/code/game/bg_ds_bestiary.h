/*
===========================================================================
REARMED -- round 4 BESTIARY (en-bestiary): the entry table shared by game, cgame and ui.
Include after the module's own header (needs DSAR_NUM, DSAF_NUM, DS_MINIBOSS_NUM, BOSS_NUM).

Entries (one index space, DSB_NUM <= 31 so one int mask holds them all):
	0..8	archetypes DSAR_SNIPER..DSAR_JUGGERNAUT (entry = arch - 1)
	9..16	affixes by bit (entry = 9 + bit index of DSAF_*)
	17		the champion pack (DSAF_CHAMPION)
	18..22	ambush mini-bosses (ds_miniBosses[])
	23..26	act bosses (ds_bosses[])
An entry unlocks the first time the player MEETS it: it comes into view on the player's floor,
or it hurts the player. Game: game/g_ds_bestiary.c (tracks, run keys, the callout command).
cgame: cgame/cg_ds_bestiary.c ("NEW IN BESTIARY" at the left edge). ui: q3_ui/ui_ds_bestiary.c
(the merge into rearmed_meta.txt and the BESTIARY tab of the Collection).

Run keys (rearmed_run.txt, written by the game at floor end next to fst/fseq):
	bst_fm	entry mask met this floor		bst_fk	kills this floor per entry (DSB_NUM csv)
	bst_rm	entry mask met this run (so a later floor never repeats a callout before the merge)
Meta keys (rearmed_meta.txt, merged once per finished floor by the UI, DSM_MergeFloor):
	bst_seen	archetypes met (bit = DSAR_ id)		bst_k	kills per archetype (DSAR_NUM csv, [0] unused)
	bst_aseen	affixes met (bit = DSAF_ bit index)	bst_ak	kills per affix (DSAF_NUM csv)
	bst_xseen	extras met (bit = DSBX_)			bst_xk	kills per extra (DSBX_NUM csv; bosses: cnt_boss<n>)
	bst_d / bst_ad / bst_xd		first-met dates YYYYMMDD, csv like the kill keys
===========================================================================
*/
#ifndef BG_DS_BESTIARY_H
#define BG_DS_BESTIARY_H

#define DSB_TYPE_FIRST		0
#define DSB_NUM_TYPES		( DSAR_NUM - 1 )
#define DSB_AFFIX_FIRST		( DSB_TYPE_FIRST + DSB_NUM_TYPES )		// 9
#define DSB_EXTRA_FIRST		( DSB_AFFIX_FIRST + DSAF_NUM )			// 17
#define DSBX_CHAMPION		0
#define DSBX_MINIBOSS		1										// + ds_miniBosses index
#define DSBX_BOSS			( DSBX_MINIBOSS + DS_MINIBOSS_NUM )		// + dsBoss_t
#define DSBX_NUM			( DSBX_BOSS + BOSS_NUM )				// 10
#define DSB_NUM				( DSB_EXTRA_FIRST + DSBX_NUM )			// 27

#define DSB_KIND_TYPE		0
#define DSB_KIND_AFFIX		1
#define DSB_KIND_EXTRA		2

typedef struct {
	const char	*name;			// NULL: the archetype / mini-boss / boss table's name (BG_DS_BstName)
	const char	*tag;			// 3 letters for the tile
	float		rgb[3];			// tile / callout colour (archetypes: the same as ds_types[].rgb)
	int			minAct;			// first act it can show up in (0 = any)
	const char	*what;			// one line: what it does
	const char	*tip;			// how to beat it
} dsBstInfo_t;

extern const dsBstInfo_t	bg_dsBst[DSB_NUM];

int BG_DS_BstKind( int entry );						// DSB_KIND_*, -1 out of range
int BG_DS_BstId( int entry );						// arch (DSAR_), affix bit index, or DSBX_
int BG_DS_BstEntry( int kind, int id );				// the other way, -1 none
const char *BG_DS_BstName( int entry );				// "SNIPER", "VAMPIRIC", "CHAMPION PACK", "THE HEADSMAN"
const char *BG_DS_BstKindName( int entry );			// "ARCHETYPE", "AFFIX", "CHAMPION PACK", "AMBUSH MINI-BOSS", "ACT BOSS"
int BG_DS_BstByName( const char *s );				// "sniper" / "frost" / "champion" / "headsman" / "12" -> entry, -1

#endif // BG_DS_BESTIARY_H
