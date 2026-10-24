/*
===========================================================================
REARMED -- DESCENT BOSS REMATCH (round 4, en-rematch): the shared contract.

A rematch is a BOSS PRACTICE floor (g_dsPractice, rearmed_practice.txt, nothing of the player's
saved) against a boss the player has beaten in a real descent (meta cnt_boss<n> > 0), with
stacked HARDER modifiers. Each modifier adds heat; the UI keeps the fastest kill per boss and
heat in rearmed_rematch.txt (separate from the real-run records in rearmed_records.txt).

Practice file keys (written by q3_ui/ui_ds_rematch.c): rematch 1, rm_mods <DSRM_ mask>,
rm_prev <the best kill msec at this heat, 0 none>.
CS_DESCENTBOSS keys (game/g_ds_rematch.c, on a rematch): rm <mask>, rmh <heat>, rmp <prev ms>.
End of the fight: the game runs "dsrmend <boss 1-4> <mask> <result 1 win / 2 loss> <kill msec>
<bar % taken> <player deaths>" (instead of dspracend).

Include this header once per module (q3asm makes file statics global).
===========================================================================
*/
#ifndef BG_DS_REMATCH_H
#define BG_DS_REMATCH_H

#define DSRM_FILE			"rearmed_rematch.txt"

#define DSRM_IRON			0x01		// the boss has +30% health
#define DSRM_FURY			0x02		// its scripted attacks come 25% sooner (gaps and cooldowns x0.75)
#define DSRM_HALFCLOCK		0x04		// half the clock (2:30 of 5:00)
#define DSRM_NOHEALTH		0x08		// no health pickups on the map
#define DSRM_GLASS			0x10		// the player's max health is halved
#define DSRM_ENRAGED		0x20		// the last-minute enrage from the first second
#define DSRM_ONELIFE		0x40		// dying ends the rematch
#define DSRM_NUM			7
#define DSRM_ALL			0x7f

#define DSRM_IRON_HP		1.30f
#define DSRM_FURY_PACE		0.75f
#define DSRM_MAX_HEAT		16			// all of them (the sum of dsrm_heat)

typedef struct {
	int			bit;
	const char	*name;		// short, upper case: the HUD line and the toggles
	int			heat;
	const char	*desc;		// one line, the rematch screen
} dsRmMod_t;

static const dsRmMod_t dsrm_mods[DSRM_NUM] = {
	{ DSRM_IRON,		"IRON",			2, "The boss has +30% health." },
	{ DSRM_FURY,		"FURY",			2, "Its big attacks come 25% sooner." },
	{ DSRM_HALFCLOCK,	"HALF CLOCK",	3, "Half the clock: 2:30 to kill it." },
	{ DSRM_NOHEALTH,	"NO HEALTH",	2, "No health pickups on the map." },
	{ DSRM_GLASS,		"GLASS",		2, "Your max health is halved." },
	{ DSRM_ENRAGED,		"ENRAGED",		2, "Last-minute fury from the first second." },
	{ DSRM_ONELIFE,		"ONE LIFE",		3, "Die once and the rematch is lost." }
};

static int BG_DSRM_Heat( int mask ) {
	int		i, h = 0;

	for ( i = 0 ; i < DSRM_NUM ; i++ ) {
		if ( mask & dsrm_mods[i].bit ) {
			h += dsrm_mods[i].heat;
		}
	}
	return h;
}

#endif // BG_DS_REMATCH_H
