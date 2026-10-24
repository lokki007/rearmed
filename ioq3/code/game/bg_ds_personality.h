/*
===========================================================================
REARMED -- round 4 ENEMIES, PERSONALITY (en-personality): shared by game and cgame.
Voice barks (sound/rearmed/taunt/<m|f>_<name>.wav, tools/make_taunt_voices.py) and the
server commands of game/ai_taunt.c + game/g_ds_rank.c, drawn by cgame/cg_personality.c.

Server commands:
	prb <client> <bark>						a voice bark from that bot (positional)
	prc <kind> <client> <a> <b> "<text>"	an edge callout (right side, never centre):
		PRC_NEMESIS_ARRIVE  a = nemesis level, b = kills       text = title ("" none)
		PRC_NEMESIS_LEVEL   a = new level, b = affix mask gained text = title
		PRC_REVENGE         a = skulls, b = 0                  text = ""
		PRC_PROMOTED        a = new rank, b = 0                 text = ""
CS_DESCENTBOSS (G_DS_NemesisInfo) adds \nl\<nemesis level>\nt\<title>.
CS_DSINTRO adds \vr\<rank of plan 0>,<rank of plan 1>,..  \vm\<met flags, same order>
	(met: 1 met before, 2 promoted since; ds-intro shows chevrons + the note).
===========================================================================
*/
#ifndef BG_DS_PERSONALITY_H
#define BG_DS_PERSONALITY_H

typedef enum {
	PRB_GOTCHA,			// "Got you!"
	PRB_EASY,			// "Too easy."
	PRB_OOPS,			// "Oops!"
	PRB_STOP,			// "Stop that one!"
	PRB_PAYBACK,		// "Payback!"
	PRB_REMEMBER,		// "Remember me?"
	PRB_MINE,			// "You're mine!"
	PRB_RETREAT,		// "Fall back!"
	PRB_AGAIN,			// "We meet again."
	PRB_NEMAGAIN,		// "Again."
	PRB_FOUND,			// "Found you."
	PRB_STRONGER,		// "I grow stronger."
	PRB_INCOMING,		// "Here it comes!"	(telegraph)
	PRB_EAT,			// "Eat this!"		(telegraph)
	PRB_GLORY,			// "For glory!"		(kamikaze fuse)
	PRB_RISE,			// "Rise!"			(summoner ritual)
	PRB_GETOVER,		// "Get over here!"	(pull affix)
	PRB_NUM
} prBark_t;

#define PRC_NEMESIS_ARRIVE	1
#define PRC_NEMESIS_LEVEL	2
#define PRC_REVENGE			3
#define PRC_PROMOTED		4
#define PRC_RIVAL			5		// en-memory: a = tier (BMT_*), b = floors met, text = "YOU 9 - 12   23 FLOORS"

#define DS_NEM_MAX_LEVEL	6		// the nemesis gains a level each time it kills the player

#endif // BG_DS_PERSONALITY_H
