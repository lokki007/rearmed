/*
===========================================================================
REARMED -- round 4 ENEMIES, PERSONALITY (en-personality): taunts, grudges, telegraph shouts.
All modes.

TAUNTS (g_botTaunts 0 off / 1 some / 2 lots): bots talk in the chat area (top-left, never the
screen centre) with a short positional voice bark (cgame/cg_personality.c): after fragging you,
when you die to a barrel / a fall / yourself, when you're on a streak, when they get revenge,
when their morale breaks (DSEF_PANIC, en-feel), when a veteran returns (DESCENT, g_ds_rank.c),
and the nemesis's own lines (g_ds_nemesis.c). Every line goes through G_PR_Say: it is queued
with a short "typing" delay (a higher-priority line for the same bot replaces a pending one),
then emitted only if the global gap, the per-bot cooldown and the per-minute cap allow.
The stock Q3 bot chat (ai_chat.c, which also made bots stand still to type) is replaced.

GRUDGES (g_botGrudges 0 off / 1 on 40 s / 2 long 90 s): a bot killed by another client holds
a grudge (g_enemy[].grudge = killer, grudgeUntil, DSEF_GRUDGE). en-brains reads it with
G_EN_GrudgeTarget (ai_tactics.c: prefers / hunts that target). Killing the grudge target =
revenge (a line, the grudge ends). DESCENT: your own revenge kill (the bot that last killed you)
pays 1 skull (x difficulty), at most 3 a floor.

SHOUTS (g_botShouts 1): the rising edge of DSEF_TELEGRAPH (en-types / en-affix wind-ups) plays
a short shout from that bot: kamikaze "For glory!", summoner "Rise!", pull "Get over here!",
others "Here it comes!" / "Eat this!"; the sniper stays silent (its glint is the tell).

Dev (server console): prstat (counters), prsay <client> <kill|env|streak|revenge|vow|flee|arrive>,
prgrudge <bot> <target> [sec]. Logs: "Taunt:", "Grudge:", "Revenge:", "Shout:" in games.log.
===========================================================================
*/
#include "g_local.h"
#include "bg_ds_personality.h"

#define PR_LINE_LEN		100
#define PR_RECENT		12

typedef struct {
	int			enterTime;		// the slot's client (a new client resets the rest)
	int			nextTalk;		// per-bot cooldown
	int			nextShout;
	int			lastFlags;		// DSEF_ edge detection
	int			pendAt, pendPrio, pendBark, pendUntil;
	char		pend[PR_LINE_LEN];
} prClient_t;

typedef struct {
	int			nextAny;
	int			recent[PR_RECENT];	// level.time of the last lines (per-minute cap)
	int			nextShoutAny;
	int			revengeFloor;		// player revenge bonuses paid this map
	// counters (prstat, tests)
	int			said, dropped, barks, shouts, grudges, revenges, playerRevenges;
	int			byEvent[8];
} prState_t;

static prClient_t	prc[MAX_CLIENTS];
static prState_t	prs;
static vmCvar_t		prCvTaunts, prCvGrudges, prCvShouts;

enum { PRE_KILL, PRE_ENV, PRE_STREAK, PRE_REVENGE, PRE_VOW, PRE_FLEE, PRE_ARRIVE, PRE_OTHER };
static const char *prEventNames[8] = { "kill", "env", "streak", "revenge", "vow", "flee", "arrive", "other" };

static const char *prLinesKill[] = {
	"Too easy.", "Sit down.", "Next.", "Is that all?", "Stay down.", "Nothing personal.",
	"Get up. Try again.", "You're slipping, %s.", "Sloppy.", "That one's mine."
};
static const char *prLinesKillMelee[] = { "Humiliating, isn't it?", "Up close and personal." };
static const char *prLinesKillProp[] = { "Barrels. Every time.", "Boom.", "Shouldn't stand there, %s." };
static const char *prLinesEnv[] = {
	"Did you trip?", "Watch your step, %s.", "The floor wins again.", "Ha! Nice one.",
	"I didn't even touch you.", "Saved me a rocket."
};
static const char *prLinesEnvBarrel[] = { "Your own barrel? Really?", "Nice fireworks.", "Ha! Boom." };
static const char *prLinesStreak[] = {
	"Somebody stop that one!", "Focus %s!", "Enough of this.", "Lucky streak. It ends now.",
	"Everyone, on %s!"
};
static const char *prLinesRevenge[] = {
	"Payback, %s.", "Remember me?", "Told you I'd be back.", "We're even.", "That's for earlier."
};
static const char *prLinesVow[] = {
	"I'll remember that.", "Lucky shot.", "You're mine next, %s.", "Cheap.", "I'll be back for you."
};
static const char *prLinesFlee[] = { "I'm out!", "Fall back!", "Not today!", "Need health!", "Too hot!" };
static const char *prLinesBotKill[] = { "Out of my way.", "Clear.", "Move.", "Next." };

#define PR_PICK( arr )		( arr[rand() % (int)( sizeof( arr ) / sizeof( arr[0] ) )] )

/*
===============================================================================

SETTINGS

===============================================================================
*/

void G_PR_Init( void ) {
	trap_Cvar_Register( &prCvTaunts, "g_botTaunts", "1", CVAR_ARCHIVE );
	trap_Cvar_Register( &prCvGrudges, "g_botGrudges", "1", CVAR_ARCHIVE );
	trap_Cvar_Register( &prCvShouts, "g_botShouts", "1", CVAR_ARCHIVE );
	memset( prc, 0, sizeof( prc ) );
	memset( &prs, 0, sizeof( prs ) );
	G_DS_RankInit();		// veterans + nemesis levels (g_ds_rank.c)
	G_BM_RegisterCvars();	// en-memory: g_botMemory (g_ds_botmem.c)
}

static int PR_Level( void ) {
	return prCvTaunts.integer < 0 ? 0 : prCvTaunts.integer > 2 ? 2 : prCvTaunts.integer;
}

// the stock Q3 bot chat is replaced by these taunts (it also made the bots stand still to type)
qboolean G_PR_StockChatOff( void ) {
	return qtrue;
}

static int PR_GapAny( void ) {
	return PR_Level() >= 2 ? 7000 : 20000;
}

static int PR_GapBot( void ) {
	return PR_Level() >= 2 ? 25000 : 60000;
}

static int PR_CapMinute( void ) {
	return PR_Level() >= 2 ? 6 : 3;
}

static float PR_Chance( float p ) {
	if ( PR_Level() >= 2 ) {
		p *= 1.6f;
	}
	return p > 1.0f ? 1.0f : p;
}

static qboolean PR_IsBot( int c ) {
	return c >= 0 && c < MAX_CLIENTS && g_entities[c].inuse && g_entities[c].client
		&& g_entities[c].client->pers.connected == CON_CONNECTED && ( g_entities[c].r.svFlags & SVF_BOT );
}

static qboolean PR_IsHuman( gentity_t *e ) {
	return e && e->client && !( e->r.svFlags & SVF_BOT );
}

static void PR_CleanName( int c, char *out, int size ) {
	if ( c < 0 || c >= MAX_CLIENTS || !g_entities[c].client ) {
		Q_strncpyz( out, "you", size );
		return;
	}
	G_EN_CleanName( g_entities[c].client->pers.netname, out, size );
}

/*
===============================================================================

SAYING THINGS

===============================================================================
*/

// a voice bark only (no chat line): cgame plays it from the bot (sound/rearmed/taunt)
void G_PR_Bark( int client, int bark ) {
	if ( client < 0 || client >= MAX_CLIENTS || bark < 0 || bark >= PRB_NUM ) {
		return;
	}
	trap_SendServerCommand( -1, va( "prb %i %i", client, bark ) );
	prs.barks++;
}

/*
=================
G_PR_Say

Queue a line for a bot (a "typing" delay of 0.6-1.8 s). prio 0 chatter, 1 normal, 2 revenge /
nemesis (bypasses the per-bot cooldown, a 3 s gap). "%s" in the line = the target's name.
=================
*/
void G_PR_SayTo( int client, int prio, const char *line, int bark, int target ) {
	prClient_t	*p;
	char		name[32];

	if ( !PR_Level() || !PR_IsBot( client ) || !line || !line[0] ) {
		return;
	}
	p = &prc[client];
	if ( p->enterTime != g_entities[client].client->pers.enterTime ) {
		// a client G_PR_Frame hasn't seen yet: start its slot now (or the frame would wipe this line)
		memset( p, 0, sizeof( *p ) );
		p->enterTime = g_entities[client].client->pers.enterTime;
	}
	if ( p->pendAt && p->pendPrio > prio ) {
		return;		// something more important is already on its way
	}
	if ( prio < 2 && level.time < p->nextTalk ) {
		return;
	}
	// format only a single "%s" (lines built elsewhere may carry player names with '%' in them)
	if ( strstr( line, "%s" ) && strchr( line, '%' ) == strstr( line, "%s" ) && !strchr( strstr( line, "%s" ) + 2, '%' ) ) {
		PR_CleanName( target, name, sizeof( name ) );
		Com_sprintf( p->pend, sizeof( p->pend ), line, name );
	} else {
		Q_strncpyz( p->pend, line, sizeof( p->pend ) );
	}
	p->pendPrio = prio;
	p->pendBark = bark;
	p->pendAt = level.time + 600 + rand() % 1200;
	p->pendUntil = p->pendAt + ( prio >= 2 ? 4000 : 1500 );
}

void G_PR_Say( int client, int prio, const char *line, int bark ) {
	G_PR_SayTo( client, prio, line, bark, G_RL_Active() ? G_RL_HumanClient() : -1 );
}

static int PR_RecentCount( void ) {
	int		i, n = 0;

	for ( i = 0 ; i < PR_RECENT ; i++ ) {
		if ( prs.recent[i] && level.time - prs.recent[i] < 60000 ) {
			n++;
		}
	}
	return n;
}

static void PR_Emit( int c ) {
	prClient_t	*p = &prc[c];
	gentity_t	*ent = &g_entities[c];
	int			i, oldest = 0;
	qboolean	nem;

	if ( !PR_Level() || !PR_IsBot( c ) || level.intermissiontime ) {
		p->pendAt = 0;
		return;
	}
	// the global limits: a gap between any two lines and a per-minute cap (revenge / nemesis: 3 s, cap + 1)
	if ( level.time < ( p->pendPrio >= 2 ? prs.nextAny - PR_GapAny() + 3000 : prs.nextAny )
		|| PR_RecentCount() >= PR_CapMinute() + ( p->pendPrio >= 2 ? 1 : 0 ) ) {
		if ( level.time >= p->pendUntil ) {
			p->pendAt = 0;
			prs.dropped++;
		}
		return;		// wait (until pendUntil)
	}
	nem = c == G_DS_NemesisClient();
	trap_SendServerCommand( -1, va( "chat \"%s^7: %s%s\"", ent->client->pers.netname, nem ? "^1" : "^2", p->pend ) );
	if ( p->pendBark >= 0 ) {
		G_PR_Bark( c, p->pendBark );
	}
	G_LogPrintf( "Taunt: %i %s: \"%s\" prio %i bark %i (%i in the last minute)\n", c, ent->client->pers.netname,
		p->pend, p->pendPrio, p->pendBark, PR_RecentCount() + 1 );
	for ( i = 1 ; i < PR_RECENT ; i++ ) {
		if ( prs.recent[i] < prs.recent[oldest] ) {
			oldest = i;
		}
	}
	prs.recent[oldest] = level.time;
	prs.nextAny = level.time + PR_GapAny();
	p->nextTalk = level.time + PR_GapBot();
	p->pendAt = 0;
	prs.said++;
}

/*
===============================================================================

GRUDGES

===============================================================================
*/

static int PR_GrudgeTime( void ) {
	return prCvGrudges.integer >= 2 ? 90000 : 40000;
}

int G_EN_GrudgeTarget( int botClient ) {
	gEnemy_t	*e = G_EN( botClient );
	gentity_t	*t;

	if ( !e || !prCvGrudges.integer || !( e->flags & DSEF_GRUDGE ) || level.time >= e->grudgeUntil ) {
		return -1;
	}
	if ( e->grudge < 0 || e->grudge >= MAX_CLIENTS || e->grudge == botClient ) {
		return -1;
	}
	t = &g_entities[e->grudge];
	if ( !t->inuse || !t->client || t->client->pers.connected != CON_CONNECTED
		|| t->client->sess.sessionTeam == TEAM_SPECTATOR || t->health <= 0 ) {
		return -1;
	}
	return e->grudge;
}

// the raw grudge (the target may be dying right now: G_EN_GrudgeTarget wants it alive)
static qboolean PR_HoldsGrudge( int bot, int target ) {
	gEnemy_t	*e = G_EN( bot );

	return e && prCvGrudges.integer && ( e->flags & DSEF_GRUDGE ) && level.time < e->grudgeUntil && e->grudge == target;
}

static void PR_SetGrudge( int bot, int target, int msec ) {
	gEnemy_t	*e = G_EN( bot );

	if ( !e ) {
		return;
	}
	e->grudge = target;
	e->grudgeUntil = level.time + msec;
	G_EN_SetFlag( bot, DSEF_GRUDGE, qtrue );
	prs.grudges++;
	G_LogPrintf( "Grudge: %i %s -> %i %s for %i s\n", bot, g_entities[bot].client->pers.netname, target,
		g_entities[target].client ? g_entities[target].client->pers.netname : "?", msec / 1000 );
}

static void PR_ClearGrudge( int bot ) {
	gEnemy_t	*e = G_EN( bot );

	if ( !e ) {
		return;
	}
	e->grudgeUntil = 0;
	G_EN_SetFlag( bot, DSEF_GRUDGE, qfalse );
}

/*
=================
G_PR_Death

player_die, before G_RearmPlayerDie (the REVENGE medal clears pers.lastKiller there)
=================
*/
static int PR_PickBot( int exclude, int prefer, vec3_t near ) {
	int			i, best = -1, n = 0;
	float		d, bestD = 0;
	vec3_t		v;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( i == exclude || !PR_IsBot( i ) || g_entities[i].client->sess.sessionTeam == TEAM_SPECTATOR ) {
			continue;
		}
		if ( G_DS_BossClient() == i && G_RL_Active() ) {
			continue;		// the act boss has its own voice
		}
		if ( prefer >= 0 && G_EN_GrudgeTarget( i ) == prefer ) {
			return i;
		}
		if ( near ) {
			VectorSubtract( g_entities[i].r.currentOrigin, near, v );
			d = VectorLength( v );
			if ( best < 0 || d < bestD ) {
				best = i;
				bestD = d;
			}
		} else if ( rand() % ( ++n ) == 0 ) {
			best = i;
		}
	}
	return best;
}

static qboolean PR_EnvMod( int mod ) {
	return mod == MOD_FALLING || mod == MOD_LAVA || mod == MOD_SLIME || mod == MOD_WATER || mod == MOD_CRUSH
		|| mod == MOD_TRIGGER_HURT || mod == MOD_SUICIDE || mod == MOD_PROP || mod == MOD_TARGET_LASER;
}

void G_PR_Death( gentity_t *self, gentity_t *attacker, int mod ) {
	int			s, a, b, streak, nem;
	qboolean	realKill;
	const char	*ml;

	if ( !self || !self->client || level.warmupTime ) {
		return;
	}
	s = self - g_entities;
	a = attacker && attacker->client ? attacker - g_entities : -1;
	realKill = a >= 0 && a != s && !OnSameTeam( self, attacker );
	nem = G_DS_NemesisClient();
	if ( realKill ) {
		G_DS_RankKill( self, attacker );		// veterans: kills earn rank xp (DESCENT)
	}

	// revenge by a bot: it killed the one it held a grudge against
	if ( realKill && PR_IsBot( a ) && PR_HoldsGrudge( a, s ) ) {
		PR_ClearGrudge( a );
		prs.revenges++;
		G_LogPrintf( "Revenge: %i %s killed %i %s\n", a, attacker->client->pers.netname, s, self->client->pers.netname );
		// on you: nearly always, and it may cut in; on another bot: now and then, plain chatter
		if ( PR_IsHuman( self ) ? random() < PR_Chance( 0.8f ) : random() < PR_Chance( 0.25f ) ) {
			G_PR_SayTo( a, PR_IsHuman( self ) ? 2 : 0, PR_PICK( prLinesRevenge ), rand() & 1 ? PRB_PAYBACK : PRB_REMEMBER, s );
			prs.byEvent[PRE_REVENGE]++;
		}
	}

	// a bot killed: a grudge against its killer
	if ( realKill && PR_IsBot( s ) && prCvGrudges.integer && !( G_RL_Active() && s == G_DS_BossClient() ) ) {
		PR_SetGrudge( s, a, PR_GrudgeTime() );
	}

	if ( PR_IsHuman( self ) ) {
		if ( realKill && PR_IsBot( a ) && a != nem ) {
			// a bot fragged the player (the nemesis has its own lines)
			if ( prc[a].pendPrio < 2 || !prc[a].pendAt ) {
				if ( mod == MOD_PROP ) {
					G_PR_SayTo( a, 1, PR_PICK( prLinesKillProp ), PRB_OOPS, s );
				} else if ( mod == MOD_GAUNTLET ) {
					G_PR_SayTo( a, 1, PR_PICK( prLinesKillMelee ), PRB_EASY, s );
				} else if ( ( ml = G_BM_KillLine( a ) ) != NULL ) {
					G_PR_SayTo( a, 1, ml, rand() & 1 ? PRB_GOTCHA : PRB_EASY, s );		// en-memory: the lifetime score
				} else if ( random() < PR_Chance( 0.35f ) ) {
					G_PR_SayTo( a, 1, PR_PICK( prLinesKill ), rand() & 1 ? PRB_GOTCHA : PRB_EASY, s );
				}
				prs.byEvent[PRE_KILL]++;
			}
		} else if ( !realKill && ( PR_EnvMod( mod ) || a == s ) && random() < PR_Chance( 0.6f ) ) {
			// a barrel / a fall / their own rocket: the nearest bot enjoys it
			b = PR_PickBot( -1, -1, self->r.currentOrigin );
			if ( b >= 0 ) {
				G_PR_SayTo( b, 1, mod == MOD_PROP ? PR_PICK( prLinesEnvBarrel ) : PR_PICK( prLinesEnv ), PRB_OOPS, s );
				prs.byEvent[PRE_ENV]++;
			}
		}
	} else if ( PR_IsBot( s ) && realKill ) {
		if ( PR_IsHuman( attacker ) ) {
			// the player on a streak (G_RearmStreak adds this kill after us)
			streak = attacker->client->streak + 1;
			if ( streak >= 5 && streak % 5 == 0 && random() < PR_Chance( 0.75f ) ) {
				b = PR_PickBot( s, a, NULL );
				if ( b >= 0 ) {
					G_PR_SayTo( b, 1, PR_PICK( prLinesStreak ), PRB_STOP, a );
					prs.byEvent[PRE_STREAK]++;
				}
			} else if ( s != nem && ( ml = G_BM_DeathLine( s ) ) != NULL ) {
				G_PR_SayTo( s, 0, ml, -1, a );		// en-memory: it keeps score (or whines)
			} else if ( s != nem && random() < PR_Chance( 0.15f ) ) {
				// a vow from the one who just died (it holds the grudge now)
				G_PR_SayTo( s, 0, PR_PICK( prLinesVow ), prCvGrudges.integer ? PRB_MINE : -1, a );
				prs.byEvent[PRE_VOW]++;
			}
			// DESCENT: the player's own revenge (the bot that last killed them) pays a little
			if ( G_RL_Active() && prCvGrudges.integer && attacker->client->pers.lastKiller == s && s != nem
				&& prs.revengeFloor < 3 && !G_DS_SurpriseExtra( s ) ) {
				int		sk = G_DS_Skulls( 1 );

				prs.revengeFloor++;
				prs.playerRevenges++;
				G_RL_AddSkulls( sk );
				trap_SendServerCommand( a, va( "prc %i %i %i 0 \"\"", PRC_REVENGE, s, sk ) );
				G_LogPrintf( "Revenge: player %i took revenge on %i %s: +%i skulls (%i this floor)\n", a, s,
					self->client->pers.netname, sk, prs.revengeFloor );
			}
		} else if ( PR_IsBot( a ) && a != nem && PR_Level() >= 2 && random() < 0.12f ) {
			G_PR_SayTo( a, 0, PR_PICK( prLinesBotKill ), -1, s );
			prs.byEvent[PRE_OTHER]++;
		}
	}
}

/*
===============================================================================

FRAME

===============================================================================
*/

static int PR_ShoutBark( int c ) {
	gEnemy_t	*e = G_EN( c );

	switch ( e ? e->arch : 0 ) {
	case DSAR_SNIPER:
		return -1;		// the glint is the tell
	case DSAR_KAMIKAZE:
		return PRB_GLORY;
	case DSAR_SUMMONER:
		return PRB_RISE;
	default:
		break;
	}
	if ( e && ( e->affix & DSAF_PULL ) ) {
		return PRB_GETOVER;
	}
	return rand() & 1 ? PRB_INCOMING : PRB_EAT;
}

void G_PR_Frame( void ) {
	int			i, flags, bark;
	prClient_t	*p;
	gentity_t	*ent;
	gEnemy_t	*e;

	G_DS_RankFrame();		// veterans: ranks onto g_enemy[].rank, arrivals (g_ds_rank.c)
	G_BM_Frame();			// en-memory: lifetime greetings, grudges, panic lines (g_ds_botmem.c)
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		ent = &g_entities[i];
		p = &prc[i];
		if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED ) {
			continue;
		}
		if ( p->enterTime != ent->client->pers.enterTime ) {
			memset( p, 0, sizeof( *p ) );
			p->enterTime = ent->client->pers.enterTime;
		}
		if ( !( ent->r.svFlags & SVF_BOT ) ) {
			continue;
		}
		e = G_EN( i );
		flags = e->flags;
		// a grudge ran out
		if ( ( flags & DSEF_GRUDGE ) && ( level.time >= e->grudgeUntil || !prCvGrudges.integer ) ) {
			PR_ClearGrudge( i );
			flags = e->flags;
		}
		// telegraph shouts (en-types / en-affix wind-ups)
		if ( ( flags & DSEF_TELEGRAPH ) && !( p->lastFlags & DSEF_TELEGRAPH ) && prCvShouts.integer
			&& level.time >= p->nextShout && level.time >= prs.nextShoutAny && ent->health > 0 ) {
			bark = PR_ShoutBark( i );
			if ( bark >= 0 ) {
				G_PR_Bark( i, bark );
				p->nextShout = level.time + 6000;
				prs.nextShoutAny = level.time + 1200;
				prs.shouts++;
				G_LogPrintf( "Shout: %i %s bark %i (arch %i)\n", i, ent->client->pers.netname, bark, e->arch );
			}
		}
		// morale broke (en-feel): a flee line now and then
		if ( ( flags & DSEF_PANIC ) && !( p->lastFlags & DSEF_PANIC ) && ent->health > 0 && random() < PR_Chance( 0.35f ) ) {
			G_PR_Say( i, 0, PR_PICK( prLinesFlee ), PRB_RETREAT );
			prs.byEvent[PRE_FLEE]++;
		}
		p->lastFlags = flags;
		if ( p->pendAt && level.time >= p->pendAt ) {
			PR_Emit( i );
		}
	}
}

/*
===============================================================================

DEV

===============================================================================
*/

// dev: a client number or a bot's name (colour codes ignored), -1 none
static int PR_ClientArg( const char *a ) {
	char	name[32];
	int		i;

	if ( !a[0] ) {
		return -1;
	}
	if ( a[0] >= '0' && a[0] <= '9' ) {
		return atoi( a );
	}
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( !g_entities[i].inuse || !g_entities[i].client ) {
			continue;
		}
		G_EN_CleanName( g_entities[i].client->pers.netname, name, sizeof( name ) );
		if ( !Q_stricmp( name, a ) ) {
			return i;
		}
	}
	return -1;
}

qboolean G_PR_Command( const char *cmd ) {
	char	a1[32], a2[32], a3[16];
	int		c, t;

	if ( !Q_stricmp( cmd, "prstat" ) ) {
		G_Printf( "prstat: taunts %i (dropped %i, last minute %i), barks %i, shouts %i, grudges %i, revenges %i, player revenges %i\n",
			prs.said, prs.dropped, PR_RecentCount(), prs.barks, prs.shouts, prs.grudges, prs.revenges, prs.playerRevenges );
		G_LogPrintf( "PRStat: time %i taunts %i dropped %i barks %i shouts %i grudges %i revenges %i playerRevenges %i"
			" ev kill %i env %i streak %i revenge %i vow %i flee %i arrive %i other %i\n",
			level.time - level.startTime, prs.said, prs.dropped, prs.barks, prs.shouts, prs.grudges, prs.revenges,
			prs.playerRevenges, prs.byEvent[0], prs.byEvent[1], prs.byEvent[2], prs.byEvent[3], prs.byEvent[4],
			prs.byEvent[5], prs.byEvent[6], prs.byEvent[7] );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "prsay" ) ) {
		trap_Argv( 1, a1, sizeof( a1 ) );
		trap_Argv( 2, a2, sizeof( a2 ) );
		c = PR_ClientArg( a1 );
		for ( t = 0 ; t < 8 ; t++ ) {
			if ( !Q_stricmp( a2, prEventNames[t] ) ) {
				break;
			}
		}
		switch ( t ) {
		case PRE_KILL: G_PR_Say( c, 1, PR_PICK( prLinesKill ), PRB_GOTCHA ); break;
		case PRE_ENV: G_PR_Say( c, 1, PR_PICK( prLinesEnv ), PRB_OOPS ); break;
		case PRE_STREAK: G_PR_Say( c, 1, PR_PICK( prLinesStreak ), PRB_STOP ); break;
		case PRE_REVENGE: G_PR_Say( c, 2, PR_PICK( prLinesRevenge ), PRB_PAYBACK ); break;
		case PRE_VOW: G_PR_Say( c, 0, PR_PICK( prLinesVow ), PRB_MINE ); break;
		case PRE_FLEE: G_PR_Say( c, 0, PR_PICK( prLinesFlee ), PRB_RETREAT ); break;
		default: G_PR_Bark( c, atoi( a2 ) ); break;
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "prgrudge" ) ) {
		trap_Argv( 1, a1, sizeof( a1 ) );
		trap_Argv( 2, a2, sizeof( a2 ) );
		trap_Argv( 3, a3, sizeof( a3 ) );
		c = PR_ClientArg( a1 );
		t = PR_ClientArg( a2 );
		if ( PR_IsBot( c ) && t >= 0 && t < MAX_CLIENTS && g_entities[t].client ) {
			PR_SetGrudge( c, t, a3[0] ? atoi( a3 ) * 1000 : PR_GrudgeTime() );
		}
		G_Printf( "prgrudge: %i -> %i\n", c, G_EN_GrudgeTarget( c ) );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "prkill" ) ) {
		// test: prkill <victim> [attacker]: the attacker (default: the human) kills the victim
		gentity_t	*v, *k;

		trap_Argv( 1, a1, sizeof( a1 ) );
		trap_Argv( 2, a2, sizeof( a2 ) );
		c = PR_ClientArg( a1 );
		t = PR_ClientArg( a2 );
		if ( t < 0 ) {
			for ( t = 0 ; t < level.maxclients ; t++ ) {
				if ( g_entities[t].inuse && PR_IsHuman( &g_entities[t] ) ) {
					break;
				}
			}
		}
		if ( c >= 0 && c < level.maxclients && t >= 0 && t < level.maxclients && g_entities[c].inuse && g_entities[t].inuse
			&& g_entities[c].client && g_entities[t].client && g_entities[c].health > 0 ) {
			v = &g_entities[c];
			k = &g_entities[t];
			v->flags &= ~FL_GODMODE;
			G_Damage( v, k, k, NULL, NULL, v->health + 400, DAMAGE_NO_ARMOR | DAMAGE_NO_PROTECTION, MOD_RAILGUN );
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "prkillme" ) ) {
		// test: prkillme <bot>: that bot kills the human (by name or number)
		gentity_t	*v, *k;

		trap_Argv( 1, a1, sizeof( a1 ) );
		c = PR_ClientArg( a1 );
		for ( t = 0 ; t < level.maxclients ; t++ ) {
			if ( g_entities[t].inuse && PR_IsHuman( &g_entities[t] ) ) {
				break;
			}
		}
		if ( c >= 0 && c < level.maxclients && t < level.maxclients && g_entities[c].inuse && g_entities[c].client
			&& g_entities[t].health > 0 ) {
			v = &g_entities[t];
			k = &g_entities[c];
			v->flags &= ~FL_GODMODE;
			G_Damage( v, k, k, NULL, NULL, v->health + 400, DAMAGE_NO_ARMOR | DAMAGE_NO_PROTECTION, MOD_ROCKET );
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "prflag" ) ) {
		// test: prflag <client> <DSEF bit> <0|1> (e.g. 128 = DSEF_TELEGRAPH: a shout on the rising edge)
		trap_Argv( 1, a1, sizeof( a1 ) );
		trap_Argv( 2, a2, sizeof( a2 ) );
		trap_Argv( 3, a3, sizeof( a3 ) );
		G_EN_SetFlag( PR_ClientArg( a1 ), atoi( a2 ), atoi( a3 ) ? qtrue : qfalse );
		return qtrue;
	}
	return G_DS_RankCommand( cmd ) || G_BM_Command( cmd );		// (en-memory: dsbm)
}
