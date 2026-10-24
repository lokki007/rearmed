/*
===========================================================================
REARMED -- DAILY DESCENT CHALLENGE (round 4, en-daily). Shared by game and UI.

One fixed challenge per local date: the seed comes from YYYYMMDD (trap_RealTime;
dev override cvar ds_dailyDate). Act 1 only: 6 floors, floor 6 = the act boss as a
TIMED 1v1 (always, whatever g_dsBossMode says). Hurt Me Plenty, 2 lives, the day's
character and a starting ability from the seed, no permanent upgrades, no skill perks.

Files (mod home):
- rearmed_daily_run.txt: the daily run (same keys as rearmed_run.txt plus dly_*). The
  player's main descent in rearmed_run.txt is never read or written while it's on.
- rearmed_daily.txt: personal bests: the best score per date, today's attempts, the
  streak, an all-time top 10 (written by the UI only).
Neither pays skulls nor writes meta, skills, tally, ghost or history.

Flag: cvar ds_daily 1 (set by the UI while a daily is being played, cleared by the
main menu and the main DESCENT hub). The game reads it at G_RL_Init: G_RL_Daily().
===========================================================================
*/
#ifndef BG_DS_DAILY_H
#define BG_DS_DAILY_H

#define DSD_RUN_FILE		"rearmed_daily_run.txt"
#define DSD_PB_FILE			"rearmed_daily.txt"
#define DSD_FLAG_CVAR		"ds_daily"
#define DSD_DATE_CVAR		"ds_dailyDate"		// dev: "YYYYMMDD" fakes today's date ("" = the clock)

#define DSD_FLOORS			6					// act 1: floor 6 is the boss
#define DSD_DIFFICULTY		3					// Hurt Me Plenty
#define DSD_BOSS_TIME		300					// seconds on the boss clock

// the game's floor records, run keys dly_r0 .. dly_r<dly_n - 1> (one per floor played, lost
// floors too): "floor won kills style secs deaths bossLeftMsec"
#define DSD_REC_FLOOR		0
#define DSD_REC_WON			1
#define DSD_REC_KILLS		2
#define DSD_REC_STYLE		3		// 0..7 (- D C B A S SS SSS), won floors only
#define DSD_REC_SECS		4
#define DSD_REC_DEATHS		5
#define DSD_REC_BOSSLEFT	6		// msec left on the boss clock at the kill (0 otherwise)
#define DSD_REC_NUM			7
#define DSD_MAX_RECS		16

// the score (UI: ui_ds_daily.c computes it from the dly_r records)
#define DSD_PTS_FLOOR		1000	// a floor won
#define DSD_PTS_BOSS		3000	// the boss floor won (instead of DSD_PTS_FLOOR)
#define DSD_PTS_KILL		20		// every kill, won or lost floors
#define DSD_PTS_STYLE		150		// x the floor's best style rank (won floors)
#define DSD_PAR_SECS		240		// won normal floor: (par - seconds) x DSD_PTS_SEC
#define DSD_PTS_SEC			4
#define DSD_PTS_BOSSSEC		10		// boss floor: x seconds left on the clock
#define DSD_PTS_DEATHLESS	250		// a won floor without dying
#define DSD_PTS_FLAWLESS	1500	// all 6 floors cleared without a single death

#endif
