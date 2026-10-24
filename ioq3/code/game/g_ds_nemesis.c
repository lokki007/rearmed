/*
===========================================================================
REARMED -- DESCENT v2 round 2 (ds-boss): the NEMESIS.

The bot that killed the player most on this run (3 kills or more, the same name) becomes the
nemesis: it keeps its name on every later floor (G_RL_InitBots puts it in; on a boss floor as
one of the two rivals), hits a little harder and takes a little less (single digit), wears a dark red rim (cgame,
CS_DESCENTBOSS \nem\<client> on fight floors) and taunts in the chat line now and then.
Killing it pays skulls (G_DS_Skulls) and a gem burst, with a short side callout
(server command "dsnem 2 <client> <skulls>"); then it is an ordinary bot again until someone
earns the title. Run keys: nemesis_name, nemesis_kills, nemesis_slain, nemesis_cand.
g_dsNemesis 0 turns it off.

Round 4 (en-personality), NEMESIS LEVELS (g_dsNemesisLevels 1): the nemesis is level 1 when
it takes the title and gains a level every further time it kills the player (max
DS_NEM_MAX_LEVEL 6, persisted: nemesis_level, nemesis_title, nemesis_affix). Per level:
damage dealt 1.05 + 0.03 x L (L1 = the old +8%, L6 +23%), damage taken 0.95 - 0.02 x L,
+10 max health and +15 spawn armour per level past 1, weapons +1 level at L2 and +2 at L4,
an affix at L3 and a second at L5 (en-affix's G_DS_AffixGrant: vampiric / regen / frost /
fire trail / reflect), a title from L2 ("Visor the Relentless"). Killing it pays by its level:
3 + floor/4 + 2 x (L-1) skulls, gems 30 + 6 x floor + 10 x (L-1). It announces itself at the
right edge when it arrives (server cmd prc, cgame/cg_personality.c) and on every level up.
Its lines go through the taunt limiter (G_PR_Say, g_botTaunts).
Dev: dsnemesis level <n>.
===========================================================================
*/
#include "g_local.h"
#include "bg_ds_personality.h"

#define NEM_MAX_CAND		16
#define NEM_KILLS_NEEDED	3
#define NEM_DMG_DEALT		1.08f		// +8% damage dealt (level 1 / g_dsNemesisLevels 0)
#define NEM_DMG_TAKEN		0.93f		// -7% damage taken
#define NEM_TAUNT_GAP		15000
#define NEM_NUM_TITLES		10
#define NEM_AFFIX_POOL		5

typedef struct {
	qboolean	loaded;
	char		name[32];			// "" no nemesis
	int			kills;				// times it killed the player (this run)
	int			slain;				// nemeses slain this run
	char		candName[NEM_MAX_CAND][32];
	int			candKills[NEM_MAX_CAND];
	int			numCand;
	int			client;				// on this floor, -1
	int			nextFind;
	int			nextTaunt;
	int			greetAt;			// a line when it shows up on a floor
	qboolean	greeted;
	// round 4 levels
	int			level;				// 1..DS_NEM_MAX_LEVEL while there is a nemesis
	char		title[32];			// "the Relentless" (from level 2)
	int			affix;				// DSAF_ mask it earned (levels 3 / 5)
} nemState_t;

static nemState_t	nemSt;
static vmCvar_t		nemCvOn;
static vmCvar_t		nemCvLevels;

static const char *nemTitles[NEM_NUM_TITLES] = {
	"the Relentless", "the Undertaker", "the Skullsplitter", "the Executioner", "the Reaper",
	"the Jackal", "the Bonebreaker", "the Hound", "the Widowmaker", "the Gravedigger"
};
static const int nemAffixPool[NEM_AFFIX_POOL] = { DSAF_VAMPIRIC, DSAF_REGEN, DSAF_FROST, DSAF_FIRETRAIL, DSAF_REFLECT };
static const char *nemLinesLevel[] = {
	"I grow stronger.", "Every death feeds me.", "Again, and stronger.", "You made me this."
};

static const char *nemLinesKill[] = {
	"Again.", "Stay down.", "You never learn.", "Mine.", "Predictable.", "Too slow.", "Back you go.", "Same mistake."
};
static const char *nemLinesNew[] = {
	"I know you now.", "Remember my name.", "You'll see me again.", "I'll be waiting below."
};
static const char *nemLinesGreet[] = {
	"Miss me?", "Still breathing?", "Down here too.", "Found you.", "Deeper, and still mine."
};
static const char *nemLinesSlain[] = {
	"...lucky.", "Not over.", "Enjoy it."
};

#define NEM_PICK( arr )	( arr[rand() % (int)( sizeof( arr ) / sizeof( arr[0] ) )] )

/*
===============================================================================

RUN KEYS

===============================================================================
*/

static void Nem_Save( void ) {
	char	buf[512];
	int		i;

	G_RL_RunSet( "nemesis_name", nemSt.name );
	G_RL_RunSet( "nemesis_kills", va( "%i", nemSt.kills ) );
	G_RL_RunSet( "nemesis_slain", va( "%i", nemSt.slain ) );
	G_RL_RunSet( "nemesis_level", va( "%i", nemSt.name[0] ? nemSt.level : 0 ) );
	G_RL_RunSet( "nemesis_title", nemSt.name[0] ? nemSt.title : "" );
	G_RL_RunSet( "nemesis_affix", va( "%i", nemSt.name[0] ? nemSt.affix : 0 ) );
	buf[0] = 0;
	for ( i = 0 ; i < nemSt.numCand ; i++ ) {
		if ( nemSt.candKills[i] <= 0 ) {
			continue;
		}
		Q_strcat( buf, sizeof( buf ), va( "%s%s:%i", buf[0] ? "," : "", nemSt.candName[i], nemSt.candKills[i] ) );
	}
	G_RL_RunSet( "nemesis_cand", buf );
}

static void Nem_Load( void ) {
	const char	*s;
	char		tok[40];
	int			n, k;

	Q_strncpyz( nemSt.name, G_RL_RunGet( "nemesis_name" ), sizeof( nemSt.name ) );
	nemSt.kills = atoi( G_RL_RunGet( "nemesis_kills" ) );
	nemSt.slain = atoi( G_RL_RunGet( "nemesis_slain" ) );
	nemSt.level = atoi( G_RL_RunGet( "nemesis_level" ) );
	if ( nemSt.name[0] && nemSt.level < 1 ) {
		nemSt.level = 1;		// a nemesis from before round 4
	} else if ( nemSt.level > DS_NEM_MAX_LEVEL ) {
		nemSt.level = DS_NEM_MAX_LEVEL;
	}
	Q_strncpyz( nemSt.title, G_RL_RunGet( "nemesis_title" ), sizeof( nemSt.title ) );
	nemSt.affix = atoi( G_RL_RunGet( "nemesis_affix" ) );
	nemSt.numCand = 0;
	s = G_RL_RunGet( "nemesis_cand" );
	while ( s && *s && nemSt.numCand < NEM_MAX_CAND ) {
		n = 0;
		while ( *s && *s != ',' && n < (int)sizeof( tok ) - 1 ) {
			tok[n++] = *s++;
		}
		tok[n] = 0;
		while ( *s && *s != ',' ) {
			s++;
		}
		if ( *s == ',' ) {
			s++;
		}
		for ( k = 0 ; tok[k] && tok[k] != ':' ; k++ ) {
		}
		if ( tok[k] != ':' || k == 0 ) {
			continue;
		}
		tok[k] = 0;
		Q_strncpyz( nemSt.candName[nemSt.numCand], tok, sizeof( nemSt.candName[0] ) );
		nemSt.candKills[nemSt.numCand] = atoi( tok + k + 1 );
		nemSt.numCand++;
	}
}

static int Nem_Cand( const char *name, qboolean add ) {
	int		i, low = 0;

	for ( i = 0 ; i < nemSt.numCand ; i++ ) {
		if ( !Q_stricmp( nemSt.candName[i], name ) ) {
			return i;
		}
	}
	if ( !add ) {
		return -1;
	}
	if ( nemSt.numCand < NEM_MAX_CAND ) {
		i = nemSt.numCand++;
	} else {
		// full: the one with the fewest kills makes room
		for ( i = 1 ; i < NEM_MAX_CAND ; i++ ) {
			if ( nemSt.candKills[i] < nemSt.candKills[low] ) {
				low = i;
			}
		}
		i = low;
	}
	Q_strncpyz( nemSt.candName[i], name, sizeof( nemSt.candName[0] ) );
	nemSt.candKills[i] = 0;
	return i;
}

/*
===============================================================================

API

===============================================================================
*/

void G_DS_NemesisInit( void ) {
	trap_Cvar_Register( &nemCvOn, "g_dsNemesis", "1", CVAR_ARCHIVE );
	trap_Cvar_Register( &nemCvLevels, "g_dsNemesisLevels", "1", CVAR_ARCHIVE );
	memset( &nemSt, 0, sizeof( nemSt ) );
	nemSt.client = -1;
	G_BM_Init();		// en-memory: the lifetime bot records (rearmed_bots.txt; not on daily / practice floors)
	if ( !G_RL_Active() ) {
		return;
	}
	Nem_Load();
	nemSt.loaded = qtrue;
	if ( nemSt.name[0] ) {
		G_LogPrintf( "DSNemesis: %s (killed the player %i times), slain so far %i, level %i '%s' affix %i\n", nemSt.name,
			nemSt.kills, nemSt.slain, nemSt.level, nemSt.title, nemSt.affix );
	}
}

static qboolean Nem_On( void ) {
	return nemCvOn.integer && G_RL_Active() && nemSt.loaded;
}

const char *G_DS_NemesisPlanName( int nodeType ) {
	if ( !Nem_On() || !nemSt.name[0] ) {
		return NULL;
	}
	if ( nodeType == RLN_VAULT_GAME ) {
		return NULL;		// the vault has nobody (boss floors: it can be one of the two rivals)
	}
	return nemSt.name;
}

int G_DS_NemesisClient( void ) {
	return Nem_On() && nemSt.name[0] ? nemSt.client : -1;
}

void G_DS_NemesisInfo( char *buf, int size ) {
	int		c = G_DS_NemesisClient();

	if ( c >= 0 ) {
		Q_strcat( buf, size, va( "\\nem\\%i\\nl\\%i", c, G_DS_NemesisLevel() ) );
		if ( nemSt.title[0] && nemCvLevels.integer ) {
			Q_strcat( buf, size, va( "\\nt\\%s", nemSt.title ) );
		}
	}
}

// round 4: the nemesis's level (1 with g_dsNemesisLevels 0), 0 = no nemesis
int G_DS_NemesisLevel( void ) {
	if ( !Nem_On() || !nemSt.name[0] ) {
		return 0;
	}
	if ( !nemCvLevels.integer ) {
		return 1;
	}
	return nemSt.level < 1 ? 1 : nemSt.level > DS_NEM_MAX_LEVEL ? DS_NEM_MAX_LEVEL : nemSt.level;
}

const char *G_DS_NemesisTitle( void ) {
	return G_DS_NemesisLevel() > 0 && nemCvLevels.integer ? nemSt.title : "";
}

// is this bot the nemesis (by name: also before G_DS_NemesisFrame found its client)?
qboolean G_DS_NemesisIs( gentity_t *ent ) {
	char	name[32];

	if ( !ent || !ent->client || !Nem_On() || !nemSt.name[0] || !( ent->r.svFlags & SVF_BOT ) ) {
		return qfalse;
	}
	if ( ( ent - g_entities ) == G_DS_BossClient() || G_DS_SurpriseExtra( ent - g_entities ) || G_DS_AffixExtra( ent - g_entities ) ) {
		return qfalse;
	}
	G_EN_CleanName( ent->client->pers.netname, name, sizeof( name ) );
	return !Q_stricmp( name, nemSt.name );
}

int G_DS_NemesisHealthBonus( gentity_t *ent ) {
	return G_DS_NemesisIs( ent ) ? 10 * ( G_DS_NemesisLevel() - 1 ) : 0;
}

int G_DS_NemesisWeaponBonus( gentity_t *ent ) {
	int		l;

	if ( !G_DS_NemesisIs( ent ) ) {
		return 0;
	}
	l = G_DS_NemesisLevel();
	return l >= 4 ? 2 : l >= 2 ? 1 : 0;
}

// G_DS_RankSpawn (every spawn of a DESCENT bot): the level's armour, the earned affixes
void G_DS_NemesisSpawn( gentity_t *ent ) {
	int		l, c;

	if ( !G_DS_NemesisIs( ent ) ) {
		return;
	}
	l = G_DS_NemesisLevel();
	c = ent - g_entities;
	ent->client->ps.stats[STAT_ARMOR] += 15 * ( l - 1 );
	if ( nemCvLevels.integer && nemSt.affix && ( g_enemy[c].affix & nemSt.affix ) != nemSt.affix ) {
		G_DS_AffixGrant( c, nemSt.affix & ~g_enemy[c].affix );
	}
	if ( !( g_enemy[c].flags & DSEF_NEMESIS ) ) {
		G_EN_SetFlag( c, DSEF_NEMESIS, qtrue );
	}
}

float G_DS_NemesisDamageScale( gentity_t *targ, gentity_t *attacker ) {
	int		c = G_DS_NemesisClient();
	int		l = G_DS_NemesisLevel();
	float	vs = G_DS_RankDamageScale( targ, attacker );		// round 4 VETERANS: +2% dealt per rank (g_ds_rank.c)
	float	s;

	if ( c < 0 || !attacker || !targ || attacker == targ ) {
		return vs;
	}
	if ( ( attacker - g_entities ) == c && G_RL_IsPlayer( targ ) ) {
		s = nemCvLevels.integer ? 1.05f + 0.03f * l : NEM_DMG_DEALT;
		s *= vs;
		return s > 1.30f ? 1.30f : s;		// the nemesis + a veteran rank: +30% at most
	}
	if ( ( targ - g_entities ) == c && G_RL_IsPlayer( attacker ) ) {
		return ( nemCvLevels.integer ? 0.95f - 0.02f * l : NEM_DMG_TAKEN ) * vs;
	}
	return vs;
}

// a short line in the chat area (never centre-screen), through the taunt limiter (ai_taunt.c)
static void Nem_Taunt( gentity_t *ent, const char *line, qboolean force, int bark ) {
	if ( !ent || !ent->client || !line ) {
		return;
	}
	if ( !force && level.time < nemSt.nextTaunt ) {
		return;
	}
	nemSt.nextTaunt = level.time + NEM_TAUNT_GAP;
	G_PR_SayTo( ent - g_entities, 2, line, bark, G_RL_HumanClient() );
	G_LogPrintf( "DSNemesis: taunt \"%s\"\n", line );
}

static unsigned Nem_Hash( const char *s, int salt ) {
	unsigned	h = 2166136261u ^ (unsigned)salt;

	while ( *s ) {
		h = ( h ^ (unsigned char)tolower( *s ) ) * 16777619u;
		s++;
	}
	return h;
}

// the edge callout (cgame/cg_personality.c): arrival or a level up
static void Nem_Callout( int kind, int affixGained ) {
	int		h = G_RL_HumanClient();

	if ( h < 0 || nemSt.client < 0 || !nemCvLevels.integer ) {
		return;
	}
	trap_SendServerCommand( h, va( "prc %i %i %i %i \"%s\"", kind, nemSt.client, G_DS_NemesisLevel(),
		kind == PRC_NEMESIS_LEVEL ? affixGained : nemSt.kills, nemSt.title ) );
}

/*
=================
Nem_LevelUp

The nemesis killed the player again: a level, maybe a title / an affix
=================
*/
static void Nem_LevelUp( gentity_t *attacker ) {
	int			gained = 0, i, pick, c;
	unsigned	h;

	if ( !nemCvLevels.integer || nemSt.level >= DS_NEM_MAX_LEVEL ) {
		return;
	}
	nemSt.level++;
	h = Nem_Hash( nemSt.name, atoi( G_RL_RunGet( "seed" ) ) );
	if ( nemSt.level >= 2 && !nemSt.title[0] ) {
		Q_strncpyz( nemSt.title, nemTitles[h % NEM_NUM_TITLES], sizeof( nemSt.title ) );
	}
	if ( nemSt.level == 3 || nemSt.level == 5 ) {
		pick = (int)( ( h >> 8 ) % NEM_AFFIX_POOL );
		for ( i = 0 ; i < NEM_AFFIX_POOL ; i++ ) {
			if ( !( nemSt.affix & nemAffixPool[( pick + i ) % NEM_AFFIX_POOL] ) ) {
				gained = nemAffixPool[( pick + i ) % NEM_AFFIX_POOL];
				break;
			}
		}
		nemSt.affix |= gained;
		c = attacker ? attacker - g_entities : -1;
		if ( gained && c >= 0 && c < MAX_CLIENTS ) {
			G_DS_AffixGrant( c, gained );
		}
	}
	G_DS_ActsDirty();		// CS_DESCENTBOSS: the level and title keys
	G_BM_NemesisUpdate( nemSt.name, nemSt.level, nemSt.title, nemSt.kills );		// en-memory: hall of fame peak
	Nem_Callout( PRC_NEMESIS_LEVEL, gained );
	G_LogPrintf( "DSNemesis: %s %s levels up to %i (affix +%i = %i, kills %i)\n", nemSt.name, nemSt.title,
		nemSt.level, gained, nemSt.affix, nemSt.kills );
}

void G_DS_NemesisFrame( void ) {
	gentity_t	*e;
	char		name[32];
	int			i, found = -1;

	if ( !Nem_On() || !nemSt.name[0] ) {
		if ( nemSt.client >= 0 ) {
			nemSt.client = -1;
			G_DS_ActsDirty();
		}
		return;
	}
	if ( level.time >= nemSt.nextFind ) {
		nemSt.nextFind = level.time + 500;
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			e = &g_entities[i];
			if ( !e->inuse || !e->client || e->client->pers.connected != CON_CONNECTED || !( e->r.svFlags & SVF_BOT ) ) {
				continue;
			}
			G_EN_CleanName( e->client->pers.netname, name, sizeof( name ) );
			if ( !Q_stricmp( name, nemSt.name ) && i != G_DS_BossClient() ) {
				found = i;
				break;
			}
		}
		if ( found != nemSt.client ) {
			nemSt.client = found;
			G_DS_ActsDirty();
			if ( found >= 0 && !nemSt.greeted ) {
				nemSt.greetAt = level.time + 4000 + rand() % 4000;
			}
		}
	}
	if ( nemSt.client >= 0 && !nemSt.greeted && nemSt.greetAt && level.time >= nemSt.greetAt && !level.intermissiontime ) {
		nemSt.greeted = qtrue;
		Nem_Callout( PRC_NEMESIS_ARRIVE, 0 );		// round 4: it announces itself at the edge
		if ( random() < 0.6f || nemCvLevels.integer ) {
			Nem_Taunt( &g_entities[nemSt.client], NEM_PICK( nemLinesGreet ), qtrue, PRB_FOUND );
		}
	}
}

/*
=================
G_DS_NemesisDeath

Every death in a DESCENT floor's play (G_RL_PlayerDie)
=================
*/
void G_DS_NemesisDeath( gentity_t *self, gentity_t *attacker ) {
	char	name[32];
	int		c, i, top, skulls, floor, lvl;
	vec3_t	org;

	if ( !Nem_On() || !self || !self->client || !attacker || !attacker->client || attacker == self ) {
		return;
	}
	// the player killed by a bot (not the act boss)
	if ( G_RL_IsPlayer( self ) && ( attacker->r.svFlags & SVF_BOT ) && !G_RL_IsPlayer( attacker )
		&& ( attacker - g_entities ) != G_DS_BossClient() ) {
		G_EN_CleanName( attacker->client->pers.netname, name, sizeof( name ) );
		if ( !name[0] || G_DS_SurpriseExtra( attacker - g_entities ) ) {
			return;		// (ds-surprise: an ambusher / ghost / fodder never becomes the nemesis)
		}
		c = Nem_Cand( name, qtrue );
		nemSt.candKills[c]++;
		if ( nemSt.name[0] && !Q_stricmp( name, nemSt.name ) ) {
			nemSt.kills = nemSt.candKills[c];
			if ( nemCvLevels.integer && nemSt.level < DS_NEM_MAX_LEVEL ) {
				Nem_LevelUp( attacker );		// round 4: every kill of the player = a level
				Nem_Taunt( attacker, NEM_PICK( nemLinesLevel ), qtrue, PRB_STRONGER );
			} else if ( random() < 0.6f ) {
				Nem_Taunt( attacker, NEM_PICK( nemLinesKill ), qfalse, PRB_NEMAGAIN );
			}
			G_BM_NemesisUpdate( nemSt.name, G_DS_NemesisLevel(), nemSt.title, nemSt.kills );	// en-memory: hall kills
		} else if ( nemSt.candKills[c] >= NEM_KILLS_NEEDED ) {
			// the top killer of the run takes the title (a tie keeps the old one)
			top = nemSt.name[0] ? Nem_Cand( nemSt.name, qfalse ) : -1;
			if ( top < 0 || nemSt.candKills[c] > nemSt.candKills[top] ) {
				G_BM_NemesisRise( name, nemSt.candKills[c], nemSt.name );		// en-memory: hall of fame (+ the one it dethroned)
				Q_strncpyz( nemSt.name, name, sizeof( nemSt.name ) );
				nemSt.kills = nemSt.candKills[c];
				nemSt.client = attacker - g_entities;
				nemSt.greeted = qtrue;
				nemSt.level = 1;		// round 4: a new nemesis starts at level 1, no title, no affix
				nemSt.title[0] = 0;
				nemSt.affix = 0;
				G_DS_ActsDirty();
				G_EN_SetFlag( nemSt.client, DSEF_NEMESIS, qtrue );
				trap_SendServerCommand( -1, va( "dsnem 1 %i %i", nemSt.client, nemSt.kills ) );
				Nem_Taunt( attacker, NEM_PICK( nemLinesNew ), qtrue, PRB_MINE );
				G_LogPrintf( "DSNemesis: new nemesis %s (%i kills)\n", nemSt.name, nemSt.kills );
			}
		}
		Nem_Save();
		return;
	}
	// revenge: the player kills the nemesis
	c = G_DS_NemesisClient();
	if ( c >= 0 && ( self - g_entities ) == c && G_RL_IsPlayer( attacker ) ) {
		floor = G_RL_Floor();
		lvl = G_DS_NemesisLevel();		// round 4: pays by its level
		skulls = G_DS_Skulls( 3 + floor / 4 + 2 * ( lvl - 1 ) );
		G_RL_AddSkulls( skulls );
		VectorCopy( self->r.currentOrigin, org );
		org[2] += 16;
		G_DS_SpawnGems( org, 30 + 6 * floor + 10 * ( lvl - 1 ), -1 );
		trap_SendServerCommand( -1, va( "dsnem 2 %i %i", c, skulls ) );
		if ( random() < 0.5f ) {
			Nem_Taunt( self, NEM_PICK( nemLinesSlain ), qtrue, -1 );
		}
		G_LogPrintf( "DSNemesis: %s slain (it had killed the player %i times, level %i): +%i skulls, gems %i\n", nemSt.name,
			nemSt.kills, lvl, skulls, 30 + 6 * floor + 10 * ( lvl - 1 ) );
		G_EN_SetFlag( c, DSEF_NEMESIS, qfalse );
		G_BM_NemesisSlain( nemSt.name, lvl, nemSt.kills, self->client->lasthurt_mod );		// en-memory: hall of fame
		nemSt.level = 0;
		nemSt.title[0] = 0;
		nemSt.affix = 0;
		i = Nem_Cand( nemSt.name, qfalse );
		if ( i >= 0 ) {
			nemSt.candKills[i] = 0;
		}
		nemSt.name[0] = 0;
		nemSt.kills = 0;
		nemSt.slain++;
		nemSt.client = -1;
		G_DS_ActsDirty();
		Nem_Save();
	}
}

/*
=================
G_DS_NemesisCommand

Dev (server console): dsnemesis <bot name> [kills] (make it the nemesis), dsnemesis clear,
dsnemesis (print)
=================
*/
qboolean G_DS_NemesisCommand( const char *cmd ) {
	char	arg[32], arg2[16];
	int		c;

	if ( Q_stricmp( cmd, "dsnemesis" ) ) {
		return qfalse;
	}
	if ( !G_RL_Active() ) {
		G_Printf( "dsnemesis: no descent\n" );
		return qtrue;
	}
	trap_Argv( 1, arg, sizeof( arg ) );
	trap_Argv( 2, arg2, sizeof( arg2 ) );
	if ( !Q_stricmp( arg, "killme" ) ) {
		// test: bot <client> kills the player (counts toward the title)
		gentity_t	*h, *b;

		c = atoi( arg2 );
		if ( G_RL_HumanClient() >= 0 && c >= 0 && c < level.maxclients && g_entities[c].inuse && g_entities[c].client ) {
			h = &g_entities[G_RL_HumanClient()];
			b = &g_entities[c];
			if ( h->health > 0 ) {
				h->flags &= ~FL_GODMODE;
				G_Damage( h, b, b, NULL, NULL, h->health + 200, DAMAGE_NO_ARMOR | DAMAGE_NO_PROTECTION, MOD_ROCKET );
			}
		}
		return qtrue;
	}
	if ( !Q_stricmp( arg, "level" ) ) {
		// round 4 test: dsnemesis level <n> (levels up one at a time from the current level)
		c = atoi( arg2 );
		if ( nemSt.name[0] ) {
			while ( nemCvLevels.integer && nemSt.level < c && nemSt.level < DS_NEM_MAX_LEVEL ) {
				Nem_LevelUp( nemSt.client >= 0 ? &g_entities[nemSt.client] : NULL );
			}
			Nem_Save();
		}
	} else if ( !Q_stricmp( arg, "clear" ) ) {
		nemSt.name[0] = 0;
		nemSt.level = 0;
		nemSt.title[0] = 0;
		nemSt.affix = 0;
		nemSt.kills = 0;
		nemSt.client = -1;
		Nem_Save();
		G_DS_ActsDirty();
	} else if ( arg[0] ) {
		c = Nem_Cand( arg, qtrue );
		nemSt.candKills[c] = arg2[0] ? atoi( arg2 ) : NEM_KILLS_NEEDED;
		G_BM_NemesisRise( arg, nemSt.candKills[c], nemSt.name );		// en-memory (dev path too: scripted tests)
		Q_strncpyz( nemSt.name, arg, sizeof( nemSt.name ) );
		nemSt.kills = nemSt.candKills[c];
		if ( nemSt.level < 1 ) {
			nemSt.level = 1;
		}
		nemSt.nextFind = 0;
		Nem_Save();
		G_DS_NemesisFrame();
		if ( nemSt.client >= 0 ) {
			trap_SendServerCommand( -1, va( "dsnem 1 %i %i", nemSt.client, nemSt.kills ) );
		}
	}
	G_Printf( "dsnemesis: '%s' level %i '%s' affix %i kills %i client %i slain %i (%i candidates)\n", nemSt.name, nemSt.level, nemSt.title, nemSt.affix, nemSt.kills, nemSt.client,
		nemSt.slain, nemSt.numCand );
	G_LogPrintf( "DSNemesis: info '%s' kills %i client %i slain %i\n", nemSt.name, nemSt.kills, nemSt.client, nemSt.slain );
	return qtrue;
}
