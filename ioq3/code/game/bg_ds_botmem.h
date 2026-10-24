/*
===========================================================================
REARMED -- round 4 BOT MEMORY + NEMESIS HALL OF FAME (en-memory). Shared by game and q3_ui.

The game (game/g_ds_botmem.c) owns rearmed_bots.txt: it reads it when a DESCENT floor loads
and rewrites it once at every floor end (RL_Finish), never mid-floor, so a floor that is quit
and replayed counts once. Daily challenge floors (ds_daily) and boss practice (g_dsPractice)
neither read nor write it. The UI (q3_ui/ui_ds_hall.c) only reads it; Reset All Progress
(ui_ds_econ.c) backs it up and empties it.

One record per line, '|' separated:
	v|1
	b|<bot>|k|d|fm|rm|lastRun|lastDate|lastRes|lastK|lastD|nem|nemPeak|hs|rv|brv
		k		times the bot killed the player		d		times the player killed it
		fm / rm	floors / runs it met the player		lastRun	the run seed of the last meeting
		lastDate YYYYMMDD, lastRes 1 floor won / 2 floor lost, lastK / lastD that floor's score
		nem		times it became the nemesis			nemPeak	the highest nemesis level it reached
		hs		the player's headshot kills on it	rv		the player's revenge kills on it
		brv		its revenge kills on the player
	h|run|runNo|date|bot|title|peak|kills|floorRose|status|floorEnd|how
		one per nemesis (HALL OF FAME), newest last; status BMH_*; how = the weapon that killed it
		(BMH_SLAIN) or the bot that took the title (BMH_DETHRONED)
===========================================================================
*/
#ifndef BG_DS_BOTMEM_H
#define BG_DS_BOTMEM_H

#define BM_FILE				"rearmed_bots.txt"
#define BM_MAX_BOTS			64
#define BM_MAX_HALL			48

// hall of fame: how the nemesis's story ended
#define BMH_OPEN			0		// still the nemesis (an old run's open entry reads as BMH_RUNENDED)
#define BMH_SLAIN			1		// the player killed it
#define BMH_OUTLIVED		2		// the run was lost with it still the nemesis
#define BMH_DETHRONED		3		// another bot took the title
#define BMH_RUNENDED		4		// the run ended some other way (won, banked, abandoned)

// tiers of a bot's history with the player (greetings escalate with them)
#define BMT_STRANGER		0
#define BMT_FAMILIAR		1		// met before
#define BMT_RIVAL			2		// 6+ floors or 12+ kills between you
#define BMT_ARCH			3		// 15+ floors or 30+ kills between you
#define BMT_NEMHIST			4		// was your nemesis in a run

#define BM_RIVAL_FLOORS		6
#define BM_RIVAL_KILLS		12
#define BM_ARCH_FLOORS		15
#define BM_ARCH_KILLS		30

// moods from the lifetime score (k = its kills on you, d = your kills on it)
#define BM_FEAR( k, d )		( ( d ) >= 8 && ( d ) >= 3 * ( ( k ) + 1 ) )			// you dominate it
#define BM_GRUDGE( k, d )	( !BM_FEAR( k, d ) && ( d ) >= 4 && ( d ) >= ( k ) + 4 )	// it is losing: it wants you

#endif // BG_DS_BOTMEM_H
