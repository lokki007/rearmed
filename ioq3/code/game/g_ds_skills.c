/*
===========================================================================
REARMED -- DESCENT skills, game side (owner SKILLS).

Skills train by doing in every mode against bots: the player's damage, kills,
headshots, movement tech, dashes, near misses, damage lived through, healing,
gibs and style. The xp is kept per map and written into rearmed_skills.txt at
floor end / match end / map change; the UI merges it into rearmed_meta.txt
(sk_xp) exactly once (q3_ui/ui_ds_skills.c). Perk effects (ranks in meta
sk_perks) only work in DESCENT. Contract: bg_ds_skills.h. API: g_descent.h.

Dev (server console): dsskills (print), dsskillxp <skill> <xp>, dsperk <perk> <rank>
(this map only), dsskillsave. g_skillsDebug 1 logs perk effects and level-ups to
games.log ("SkillPerk:" / "Skills:"), 2 also every xp gain ("SkillXP:").
g_skillsTrack <client> trains a bot instead of the human (tests).
===========================================================================
*/
#include "g_local.h"

#define SK_NORMAL_MULT		1.0f	// skill xp scale outside DESCENT (DESCENT: the difficulty's reward multiplier)
#define SK_FILE_MAX			16384
#define SK_MAX_KV			48
#define SK_NEARMISS_DIST	80		// an enemy missile this close that doesn't hurt you = a near miss
#define SK_NEARMISS_WAIT	400		// msec to make sure it didn't
#define SK_SURVIVE_TIME		3000	// damage taken counts once you are still alive this long after
#define SK_MAX_PENDING		8

// xp weights (before the multiplier). Tuned so every skill levels at a similar pace in
// a typical match (README "Skills" has the measured rates).
static const float skDamageXP[SK_NUM] = {
	0.28f,		// MELEE: per point of damage dealt
	0.105f,		// BALLISTICS
	0.125f,		// SCATTER
	0.09f,		// EXPLOSIVES
	0.105f,		// ENERGY
	0.13f,		// PRECISION (the railgun; headshots below)
	0, 0, 0, 0
};
static const float skKillXP[SK_NUM] = { 10, 4, 4, 4, 4, 5, 0, 0, 0, 0 };
#define SKX_HEADSHOT		3.0f	// PRECISION per headshot hit, any weapon
#define SKX_AIRJUMP			0.6f	// TRAVERSAL
#define SKX_WALLJUMP		1.0f
#define SKX_WALLRUN			0.7f
#define SKX_SLIDE			0.6f
#define SKX_ZIP				2.0f
#define SKX_GRAPPLE			1.0f
#define SKX_TECH_SEC		1.0f	// per second wall running / sliding / zipping / grappling
#define SKX_AIR_DIST		0.0015f	// per unit of fast air travel (> SK_AIR_SPEED)
#define SK_AIR_SPEED		400
#define SKX_DASH			2.0f	// EVASION
#define SKX_BLINK			2.5f
#define SKX_NEARMISS		3.0f	// + the missile's damage / 25
#define SKX_HURT			0.13f	// SURVIVAL per point taken and lived through
#define SKX_HEAL			0.09f	// per point healed
#define SKX_GIB				4.0f	// CARNAGE per gib
#define SKX_STYLE			0.03f	// per style point

typedef struct {
	char	key[32];
	char	val[256];
} skKV_t;

typedef struct {
	int		num;
	skKV_t	kv[SK_MAX_KV];
} skFile_t;

typedef struct {
	int		ent;
	int		stamp;
	int		time;
	int		damage;
} skNearMiss_t;

typedef struct {
	qboolean	loaded;
	int			base[SK_NUM];		// meta + pending at map start
	int			live[SK_NUM];		// trained this map (multiplier applied)
	int			unsaved[SK_NUM];	// trained since the last save
	float		frac[SK_NUM];
	int			level[SK_NUM];
	int			charLevel;
	int			perks[SKP_NUM];		// ranks owned (meta)
	// the DESCENT run the run_ keys follow
	int			runSeed;
	int			runXP[SK_NUM];
	int			runLv0[SK_NUM];
	int			runCl0;
	// training state
	int			client;				// tracked client, -1 none
	qboolean	training;
	int			botCheck;
	int			csTime;
	qboolean	csDirty;
	int			prevHealth;
	qboolean	wasDead;
	float		survPending;
	int			survTime;
	int			hurtTime;			// last time the tracked player took damage
	qboolean	wasGrapple;
	qboolean	wasZip;
	int			nearMissStamp[MAX_GENTITIES];
	skNearMiss_t	pend[SK_MAX_PENDING];
	// effect state (the DESCENT human)
	int			dashTime;			// last dash / blink
	int			killTime;			// last kill
	int			zipEndTime;			// last zip line / grapple end
	int			cheatDeathUsed;
	int			mendTime;
	int			streakTarget, streakCount, streakTime;	// SHREDDER
	int			lgTarget, lgStart, lgLast;				// STATIC CHARGE
	int			markedUntil[MAX_CLIENTS];				// MARKED
	int			headshotKill[MAX_CLIENTS];				// EXECUTIONER: the killing hit was a headshot
} gSkills_t;

static gSkills_t	sk;
static skFile_t		skF;
static char			skBuf[SK_FILE_MAX];

static vmCvar_t		g_skills;
static vmCvar_t		g_skillsDebug;
static vmCvar_t		g_skillsTrack;

/*
===============================================================================

FILES

===============================================================================
*/

static void Sk_FileLoad( skFile_t *f, const char *name ) {
	fileHandle_t	fh;
	int				len, k, v;
	char			*p, *line, *end;

	f->num = 0;
	len = trap_FS_FOpenFile( name, &fh, FS_READ );
	if ( !fh ) {
		return;
	}
	if ( len <= 0 || len >= SK_FILE_MAX ) {
		trap_FS_FCloseFile( fh );
		return;
	}
	trap_FS_Read( skBuf, len, fh );
	trap_FS_FCloseFile( fh );
	skBuf[len] = 0;
	p = skBuf;
	while ( *p ) {
		line = p;
		while ( *p && *p != '\n' ) {
			p++;
		}
		if ( *p ) {
			*p++ = 0;
		}
		end = line + strlen( line );
		while ( end > line && ( end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t' ) ) {
			*--end = 0;
		}
		// only the sk_ keys of the meta file (and everything of the skills file)
		if ( !*line || f->num >= SK_MAX_KV ) {
			continue;
		}
		for ( k = 0 ; line[k] && line[k] != ' ' && line[k] != '\t' ; k++ ) {
		}
		v = k;
		while ( line[v] == ' ' || line[v] == '\t' ) {
			v++;
		}
		if ( k >= (int)sizeof( f->kv[0].key ) ) {
			continue;
		}
		memcpy( f->kv[f->num].key, line, k );
		f->kv[f->num].key[k] = 0;
		if ( !Q_stricmp( name, "rearmed_meta.txt" ) && Q_strncmp( f->kv[f->num].key, "sk_", 3 ) ) {
			continue;
		}
		Q_strncpyz( f->kv[f->num].val, line + v, sizeof( f->kv[f->num].val ) );
		f->num++;
	}
}

static const char *Sk_Get( skFile_t *f, const char *key ) {
	int		i;

	for ( i = 0 ; i < f->num ; i++ ) {
		if ( !Q_stricmp( f->kv[i].key, key ) ) {
			return f->kv[i].val;
		}
	}
	return "";
}

static void Sk_Set( skFile_t *f, const char *key, const char *val ) {
	int		i;

	for ( i = 0 ; i < f->num ; i++ ) {
		if ( !Q_stricmp( f->kv[i].key, key ) ) {
			Q_strncpyz( f->kv[i].val, val, sizeof( f->kv[i].val ) );
			return;
		}
	}
	if ( f->num < SK_MAX_KV ) {
		Q_strncpyz( f->kv[f->num].key, key, sizeof( f->kv[f->num].key ) );
		Q_strncpyz( f->kv[f->num].val, val, sizeof( f->kv[f->num].val ) );
		f->num++;
	}
}

static void Sk_FileSave( skFile_t *f, const char *name ) {
	fileHandle_t	fh;
	int				i;

	if ( G_RL_NoSave() ) {
		return;		// en-boss: boss practice trains nothing (en-daily: the daily neither)
	}
	skBuf[0] = 0;
	for ( i = 0 ; i < f->num ; i++ ) {
		Q_strcat( skBuf, sizeof( skBuf ), va( "%s %s\n", f->kv[i].key, f->kv[i].val ) );
	}
	trap_FS_FOpenFile( name, &fh, FS_WRITE );
	if ( !fh ) {
		G_Printf( S_COLOR_RED "skills: can't write %s\n", name );
		return;
	}
	trap_FS_Write( skBuf, strlen( skBuf ), fh );
	trap_FS_FCloseFile( fh );
}

static int Sk_ParseCsv( const char *s, int *out, int max ) {
	int		n = 0;

	while ( *s && n < max ) {
		while ( *s == ',' || *s == ' ' || *s == '.' ) {
			s++;
		}
		if ( !*s ) {
			break;
		}
		out[n++] = atoi( s );
		while ( *s && *s != ',' && *s != '.' && *s != ' ' ) {
			s++;
		}
	}
	return n;
}

static const char *Sk_Csv( const int *v, int n ) {
	static char	buf[4][512];
	static int	index;
	char		*b = buf[index++ & 3];
	int			i;

	b[0] = 0;
	for ( i = 0 ; i < n ; i++ ) {
		Q_strcat( b, 512, va( i ? ",%i" : "%i", v[i] ) );
	}
	return b;
}

/*
===============================================================================

LEVELS

===============================================================================
*/

static int Sk_Total( int s ) {
	return sk.base[s] + sk.live[s];
}

static void Sk_Levels( int *out ) {
	int		s;

	for ( s = 0 ; s < SK_NUM ; s++ ) {
		out[s] = BG_DS_SkillLevel( Sk_Total( s ), NULL, NULL );
	}
}

static qboolean Sk_Effects( void ) {
	return G_RL_Active() && g_skills.integer && sk.loaded;
}

// the DESCENT human with the perk effects
static qboolean Sk_IsHuman( gentity_t *ent ) {
	int		h;

	if ( !Sk_Effects() || !ent || !ent->client ) {
		return qfalse;
	}
	if ( g_skillsTrack.integer >= 0 ) {
		return ent - g_entities == g_skillsTrack.integer;		// test: the tracked bot gets the perks
	}
	if ( ent->r.svFlags & SVF_BOT ) {
		return qfalse;
	}
	h = G_RL_HumanClient();
	return h >= 0 && ent - g_entities == h;
}

static int Sk_R( int perk ) {
	return ( perk >= 0 && perk < SKP_NUM ) ? sk.perks[perk] : 0;
}

static void Sk_PerkLog( int perk, const char *fmt, ... ) {
	va_list		argptr;
	char		text[256];

	if ( !g_skillsDebug.integer ) {
		return;
	}
	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );
	G_LogPrintf( "SkillPerk: %s %s\n", ds_perks[perk].name, text );
}

static void Sk_WriteConfigstring( void ) {
	char	buf[512];
	int		s, l, into, need;

	if ( !sk.training || sk.client < 0 ) {
		trap_SetConfigstring( CS_DSSKILLS, "" );
		return;
	}
	buf[0] = 0;
	Q_strcat( buf, sizeof( buf ), "\\lv\\" );
	for ( s = 0 ; s < SK_NUM ; s++ ) {
		Q_strcat( buf, sizeof( buf ), va( s ? ".%i" : "%i", sk.level[s] ) );
	}
	Q_strcat( buf, sizeof( buf ), "\\pm\\" );
	for ( s = 0 ; s < SK_NUM ; s++ ) {
		l = BG_DS_SkillLevel( Sk_Total( s ), &into, &need );
		Q_strcat( buf, sizeof( buf ), va( s ? ".%i" : "%i", ( l >= SK_MAX_LEVEL || need <= 0 ) ? 1000 : into * 1000 / need ) );
	}
	Q_strcat( buf, sizeof( buf ), va( "\\cl\\%i\\fx\\%i", sk.charLevel, Sk_Effects() ? 1 : 0 ) );
	trap_SetConfigstring( CS_DSSKILLS, buf );
	sk.csTime = level.time;
	sk.csDirty = qfalse;
}

/*
=================
Sk_Add

xp for the tracked player (before the multiplier)
=================
*/
static void Sk_Add( int s, float xp, const char *why ) {
	int			n, l, levels[SK_NUM], cl;
	float		mult;

	if ( !sk.training || s < 0 || s >= SK_NUM || xp <= 0 ) {
		return;
	}
	mult = G_RL_Active() ? G_DS_RewardMult() : SK_NORMAL_MULT;
	sk.frac[s] += xp * mult;
	n = (int)sk.frac[s];
	if ( g_skillsDebug.integer > 1 ) {
		G_LogPrintf( "SkillXP: %s +%.2f %s\n", ds_skills[s].name, xp * mult, why );
	}
	if ( n <= 0 ) {
		return;
	}
	sk.frac[s] -= n;
	sk.live[s] += n;
	sk.unsaved[s] += n;
	sk.csDirty = qtrue;
	l = BG_DS_SkillLevel( Sk_Total( s ), NULL, NULL );
	if ( l > sk.level[s] ) {
		sk.level[s] = l;
		if ( !( g_entities[sk.client].r.svFlags & SVF_BOT ) ) {
			trap_SendServerCommand( sk.client, va( "dsskl %i %i", s, l ) );
		}
		G_LogPrintf( "Skills: %s level %i (xp %i)\n", ds_skills[s].name, l, Sk_Total( s ) );
		Sk_Levels( levels );
		cl = BG_DS_CharLevel( levels );
		if ( cl > sk.charLevel ) {
			sk.charLevel = cl;
			G_LogPrintf( "Skills: character level %i (+1 perk point)\n", cl );	// shown at the run summary, never here
		}
		Sk_WriteConfigstring();
	}
}

/*
===============================================================================

INIT / SAVE

===============================================================================
*/

/*
=================
G_DS_SkillsInit

G_RearmInit, after G_RL_Init (every map start / restart)
=================
*/
void G_DS_SkillsInit( void ) {
	int			s, mseq, seq, pend[SK_NUM], levels[SK_NUM];
	const char	*v;

	trap_Cvar_Register( &g_skills, "g_skills", "1", CVAR_ARCHIVE );
	trap_Cvar_Register( &g_skillsDebug, "g_skillsDebug", "0", 0 );
	trap_Cvar_Register( &g_skillsTrack, "g_skillsTrack", "-1", 0 );
	memset( &sk, 0, sizeof( sk ) );
	sk.client = -1;
	sk.lgTarget = sk.streakTarget = -1;

	Sk_FileLoad( &skF, "rearmed_meta.txt" );
	Sk_ParseCsv( Sk_Get( &skF, "sk_xp" ), sk.base, SK_NUM );
	Sk_ParseCsv( Sk_Get( &skF, "sk_perks" ), sk.perks, SKP_NUM );
	mseq = atoi( Sk_Get( &skF, "sk_mseq" ) );
	for ( s = 0 ; s < SKP_NUM ; s++ ) {
		if ( sk.perks[s] < 0 ) {
			sk.perks[s] = 0;
		} else if ( sk.perks[s] > ds_perks[s].maxRank ) {
			sk.perks[s] = ds_perks[s].maxRank;
		}
	}
	if ( G_RL_Daily() ) {
		memset( sk.perks, 0, sizeof( sk.perks ) );		// en-daily: the same challenge for everyone: no perks
	}

	Sk_FileLoad( &skF, SKILLS_FILE );
	seq = atoi( Sk_Get( &skF, "seq" ) );
	if ( seq > mseq ) {		// not merged yet: it counts
		memset( pend, 0, sizeof( pend ) );
		Sk_ParseCsv( Sk_Get( &skF, "xp" ), pend, SK_NUM );
		for ( s = 0 ; s < SK_NUM ; s++ ) {
			if ( pend[s] > 0 ) {
				sk.base[s] += pend[s];
			}
		}
	}
	for ( s = 0 ; s < SK_NUM ; s++ ) {
		if ( sk.base[s] < 0 ) {
			sk.base[s] = 0;
		}
	}
	Sk_Levels( sk.level );
	sk.charLevel = BG_DS_CharLevel( sk.level );

	// the run's own numbers (the run summary shows what this run trained)
	if ( G_RL_Active() ) {
		v = G_RL_RunGet( "seed" );
		sk.runSeed = atoi( v );
		if ( atoi( Sk_Get( &skF, "run_seed" ) ) == sk.runSeed && sk.runSeed ) {
			Sk_ParseCsv( Sk_Get( &skF, "run_xp" ), sk.runXP, SK_NUM );
			Sk_ParseCsv( Sk_Get( &skF, "run_lv0" ), sk.runLv0, SK_NUM );
			sk.runCl0 = atoi( Sk_Get( &skF, "run_cl0" ) );
		} else {
			Sk_Levels( levels );
			memcpy( sk.runLv0, levels, sizeof( levels ) );
			sk.runCl0 = sk.charLevel;
		}
	}
	sk.loaded = qtrue;
	trap_SetConfigstring( CS_DSSKILLS, "" );
	if ( g_skillsDebug.integer ) {
		G_LogPrintf( "Skills: init levels %s char %i perks %s descent %i\n", Sk_Csv( sk.level, SK_NUM ), sk.charLevel,
			Sk_Csv( sk.perks, SKP_NUM ), G_RL_Active() );
	}
}

/*
=================
G_DS_SkillsSave

LogExit (floor end / match end) and G_ShutdownGame: the xp trained since the last
save into rearmed_skills.txt. A batch the UI already merged starts a new seq.
=================
*/
void G_DS_SkillsSave( void ) {
	int			s, mseq, seq, pend[SK_NUM], any;

	if ( !sk.loaded ) {
		return;
	}
	any = 0;
	for ( s = 0 ; s < SK_NUM ; s++ ) {
		any |= sk.unsaved[s];
	}
	if ( !any ) {
		return;
	}
	Sk_FileLoad( &skF, "rearmed_meta.txt" );
	mseq = atoi( Sk_Get( &skF, "sk_mseq" ) );
	Sk_FileLoad( &skF, SKILLS_FILE );
	seq = atoi( Sk_Get( &skF, "seq" ) );
	memset( pend, 0, sizeof( pend ) );
	if ( seq > mseq ) {
		Sk_ParseCsv( Sk_Get( &skF, "xp" ), pend, SK_NUM );		// still waiting for the UI: add to it
	} else {
		seq = ( seq > mseq ? seq : mseq ) + 1;					// merged: a new batch
	}
	for ( s = 0 ; s < SK_NUM ; s++ ) {
		pend[s] += sk.unsaved[s];
		if ( G_RL_Active() ) {
			sk.runXP[s] += sk.unsaved[s];
		}
		sk.unsaved[s] = 0;
	}
	Sk_Set( &skF, "seq", va( "%i", seq ) );
	Sk_Set( &skF, "xp", Sk_Csv( pend, SK_NUM ) );
	Sk_Set( &skF, "last_xp", Sk_Csv( sk.live, SK_NUM ) );
	if ( G_RL_Active() && sk.runSeed ) {
		Sk_Set( &skF, "run_seed", va( "%i", sk.runSeed ) );
		Sk_Set( &skF, "run_xp", Sk_Csv( sk.runXP, SK_NUM ) );
		Sk_Set( &skF, "run_lv0", Sk_Csv( sk.runLv0, SK_NUM ) );
		Sk_Set( &skF, "run_cl0", va( "%i", sk.runCl0 ) );
	}
	Sk_FileSave( &skF, SKILLS_FILE );
	if ( G_RL_NoSave() ) {
		G_LogPrintf( "Skills: not saved (%s)\n", G_RL_Daily() ? "daily" : "practice" );	// Sk_FileSave wrote nothing
		return;
	}
	G_LogPrintf( "Skills: saved seq %i pending %s map %s levels %s char %i\n", seq, Sk_Csv( pend, SK_NUM ),
		Sk_Csv( sk.live, SK_NUM ), Sk_Csv( sk.level, SK_NUM ), sk.charLevel );
}

/*
===============================================================================

TRAINING

===============================================================================
*/

static int Sk_FindClient( void ) {
	int			i;
	gclient_t	*cl;

	if ( g_skillsTrack.integer >= 0 && g_skillsTrack.integer < level.maxclients ) {
		cl = &level.clients[g_skillsTrack.integer];
		return cl->pers.connected == CON_CONNECTED ? g_skillsTrack.integer : -1;
	}
	if ( G_RL_Active() ) {
		return G_RL_HumanClient();
	}
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		cl = &level.clients[i];
		if ( cl->pers.connected == CON_CONNECTED && !( g_entities[i].r.svFlags & SVF_BOT )
			&& cl->sess.sessionTeam != TEAM_SPECTATOR ) {
			return i;
		}
	}
	return -1;
}

static qboolean Sk_BotsIn( int except ) {
	int		i;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( i != except && level.clients[i].pers.connected == CON_CONNECTED && ( g_entities[i].r.svFlags & SVF_BOT ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

static qboolean Sk_Tracked( gentity_t *ent ) {
	return sk.training && ent && ent->client && ent - g_entities == sk.client;
}

// meansOfDeath -> weapon_t (0 none)
static int Sk_ModWeapon( int mod, gentity_t *attacker ) {
	int		w;

	w = G_RL_Active() ? G_DS_WeaponForMod( mod ) : 0;
	if ( w > 0 && w < DS_NUM_WEAPONS ) {
		return w;
	}
	switch ( mod ) {
	case MOD_GAUNTLET:			return WP_GAUNTLET;
	case MOD_MACHINEGUN:		return WP_MACHINEGUN;
	case MOD_SHOTGUN:			return WP_SHOTGUN;
	case MOD_GRENADE:
	case MOD_GRENADE_SPLASH:	return WP_GRENADE_LAUNCHER;
	case MOD_ROCKET:
	case MOD_ROCKET_SPLASH:		return WP_ROCKET_LAUNCHER;
	case MOD_LIGHTNING:			return WP_LIGHTNING;
	case MOD_RAILGUN:			return WP_RAILGUN;
	case MOD_PLASMA:
	case MOD_PLASMA_SPLASH:		return WP_PLASMAGUN;
	case MOD_BFG:
	case MOD_BFG_SPLASH:		return WP_BFG;
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case MOD_NAIL:				return WP_NAILGUN;
	case MOD_CHAINGUN:			return WP_CHAINGUN;
	case MOD_PROXIMITY_MINE:	return WP_PROX_LAUNCHER;
#endif
	case MOD_GRAPPLE:			return WP_GRAPPLING_HOOK;
	default:
		break;
	}
	if ( mod == MOD_BURN || mod == MOD_THUNDER || mod >= MOD_CORPSE ) {
		if ( attacker && attacker->client ) {
			return attacker->client->ps.weapon;
		}
	}
	return 0;
}

static int Sk_ModSkill( int mod, gentity_t *attacker ) {
	int		w = Sk_ModWeapon( mod, attacker );

	return w > 0 ? BG_DS_SkillForWeapon( w ) : -1;
}

static qboolean Sk_HitscanMod( int mod ) {
	switch ( mod ) {
	case MOD_MACHINEGUN:
	case MOD_SHOTGUN:
	case MOD_LIGHTNING:
	case MOD_RAILGUN:
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case MOD_CHAINGUN:
#endif
		return qtrue;
	default:
		return qfalse;
	}
}

static void Sk_Heal( gentity_t *ent, int n, int perk ) {
	int		max = ent->client->ps.stats[STAT_MAX_HEALTH];

	if ( ent->health <= 0 || n <= 0 || ent->health >= max ) {
		return;
	}
	ent->health += n;
	if ( ent->health > max ) {
		ent->health = max;
	}
	ent->client->ps.stats[STAT_HEALTH] = ent->health;
	Sk_PerkLog( perk, "heal %i -> %i", n, ent->health );
}

static void Sk_DashRefill( gentity_t *ent, int msec, int perk ) {
	playerState_t	*ps = &ent->client->ps;

	if ( ps->stats[STAT_DASH_TIME] <= 0 ) {
		return;
	}
	ps->stats[STAT_DASH_TIME] -= msec;
	if ( ps->stats[STAT_DASH_TIME] < 0 ) {
		ps->stats[STAT_DASH_TIME] = 0;
	}
	Sk_PerkLog( perk, "dash -%i ms -> %i", msec, ps->stats[STAT_DASH_TIME] );
}

static void Sk_GiveAmmo( gentity_t *ent, int w, int n, int perk ) {
	playerState_t	*ps = &ent->client->ps;

	if ( w <= WP_GAUNTLET || w >= WP_NUM_WEAPONS || w == WP_GRAPPLING_HOOK || ps->ammo[w] < 0 ) {
		return;
	}
	ps->ammo[w] += n;
	if ( ps->ammo[w] > 200 ) {
		ps->ammo[w] = 200;
	}
	Sk_PerkLog( perk, "ammo w%i +%i -> %i", w, n, ps->ammo[w] );
}

/*
=================
Sk_NearMisses

Enemy missiles passing close to the tracked player without hurting them
=================
*/
static void Sk_NearMisses( gentity_t *player ) {
	gentity_t	*m;
	vec3_t		center, d;
	int			i, j, free;

	VectorCopy( player->r.currentOrigin, center );
	center[2] += 8;
	for ( i = MAX_CLIENTS ; i < level.num_entities ; i++ ) {
		m = &g_entities[i];
		if ( !m->inuse || m->s.eType != ET_MISSILE || m->r.ownerNum == player->s.number
			|| m->r.ownerNum < 0 || m->r.ownerNum >= MAX_CLIENTS || m->damage + m->splashDamage <= 0 ) {
			continue;
		}
		if ( sk.nearMissStamp[i] == m->s.pos.trTime + 1 ) {
			continue;
		}
		VectorSubtract( m->r.currentOrigin, center, d );
		if ( VectorLength( d ) > SK_NEARMISS_DIST ) {
			continue;
		}
		sk.nearMissStamp[i] = m->s.pos.trTime + 1;
		free = -1;
		for ( j = 0 ; j < SK_MAX_PENDING ; j++ ) {
			if ( !sk.pend[j].time ) {
				free = j;
				break;
			}
		}
		if ( free >= 0 ) {
			sk.pend[free].ent = i;
			sk.pend[free].time = level.time;
			sk.pend[free].damage = m->damage > m->splashDamage ? m->damage : m->splashDamage;
		}
	}
	// a while later: not hurt since = dodged
	for ( j = 0 ; j < SK_MAX_PENDING ; j++ ) {
		if ( !sk.pend[j].time || level.time < sk.pend[j].time + SK_NEARMISS_WAIT ) {
			continue;
		}
		if ( sk.hurtTime < sk.pend[j].time && player->health > 0 ) {
			Sk_Add( SK_EVASION, SKX_NEARMISS + sk.pend[j].damage / 25.0f, "near miss" );
			if ( Sk_IsHuman( player ) && Sk_R( SKP_AFTERIMAGE ) ) {
				Sk_DashRefill( player, 300, SKP_AFTERIMAGE );
			}
		}
		sk.pend[j].time = 0;
	}
}

/*
=================
G_DS_SkillsFrame

G_RunFrame, after G_RL_RunFrame
=================
*/
void G_DS_SkillsFrame( void ) {
	gentity_t		*ent;
	playerState_t	*ps;
	int				msec, f2;
	float			hspeed;
	qboolean		was, tech;

	if ( !sk.loaded ) {
		return;
	}
	msec = level.time - level.previousTime;
	if ( level.time >= sk.botCheck ) {
		sk.botCheck = level.time + 1000;
		trap_Cvar_Update( &g_skills );
		trap_Cvar_Update( &g_skillsDebug );
		trap_Cvar_Update( &g_skillsTrack );
		sk.client = Sk_FindClient();
		was = sk.training;
		sk.training = g_skills.integer && sk.client >= 0 && Sk_BotsIn( sk.client );
		if ( was != sk.training ) {
			sk.csDirty = qtrue;
			if ( g_skillsDebug.integer ) {
				G_LogPrintf( "Skills: training %i client %i xp multiplier %.2f\n", sk.training, sk.client,
					G_RL_Active() ? G_DS_RewardMult() : SK_NORMAL_MULT );
			}
		}
	}
	if ( sk.csDirty && level.time >= sk.csTime + 300 ) {
		Sk_WriteConfigstring();
	}
	if ( !sk.training || level.intermissiontime || level.warmupTime || msec <= 0 || msec > 500 ) {
		return;
	}
	ent = &g_entities[sk.client];
	if ( !ent->inuse || !ent->client ) {
		return;
	}
	ps = &ent->client->ps;

	// SURVIVAL: the damage taken counts once you are still alive a while later; healing
	if ( ent->health <= 0 || ps->pm_type == PM_DEAD ) {
		sk.wasDead = qtrue;
		sk.survPending = 0;
		return;
	}
	if ( sk.wasDead ) {
		sk.wasDead = qfalse;
		sk.prevHealth = ent->health;
	}
	if ( ent->health > sk.prevHealth ) {
		Sk_Add( SK_SURVIVAL, ( ent->health - sk.prevHealth ) * SKX_HEAL, "heal" );
	}
	sk.prevHealth = ent->health;
	if ( sk.survPending > 0 && level.time >= sk.survTime + SK_SURVIVE_TIME ) {
		Sk_Add( SK_SURVIVAL, sk.survPending, "survived" );
		sk.survPending = 0;
	}

	// TRAVERSAL: time on walls, slides, zip lines and the grapple; fast air time
	f2 = ps->ammo[AMMO_RMFLAGS2];
	tech = ( ps->stats[STAT_RMFLAGS] & RMS_WALLRUN ) || ( f2 & RMS2_SLIDING ) || ( ps->loopSound & RMZ_ZIPPING )
		|| ( ps->pm_flags & PMF_GRAPPLE_PULL );
	if ( tech ) {
		Sk_Add( SK_TRAVERSAL, SKX_TECH_SEC * msec * 0.001f, "tech" );
	}
	if ( ( ps->pm_flags & PMF_GRAPPLE_PULL ) && !sk.wasGrapple ) {
		Sk_Add( SK_TRAVERSAL, SKX_GRAPPLE, "grapple" );
	}
	if ( !( ps->pm_flags & PMF_GRAPPLE_PULL ) && sk.wasGrapple ) {
		sk.zipEndTime = level.time;
	}
	sk.wasGrapple = ( ps->pm_flags & PMF_GRAPPLE_PULL ) != 0;
	if ( ps->groundEntityNum == ENTITYNUM_NONE && !tech ) {
		hspeed = sqrt( ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1] );
		if ( hspeed > SK_AIR_SPEED ) {
			Sk_Add( SK_TRAVERSAL, hspeed * msec * 0.001f * SKX_AIR_DIST, "air" );
		}
	}

	// EVASION: near misses
	Sk_NearMisses( ent );
}

/*
=================
G_DS_SkillEvent

ClientEvents (server side, after Pmove): the movement tech events, any client
=================
*/
void G_DS_SkillEvent( gentity_t *ent, int event, int parm ) {
	playerState_t	*ps;
	vec3_t			h;
	float			s;

	if ( !ent->client ) {
		return;
	}
	ps = &ent->client->ps;
	if ( Sk_Tracked( ent ) && !level.intermissiontime ) {
		switch ( event ) {
		case EV_DOUBLE_JUMP:	Sk_Add( SK_TRAVERSAL, SKX_AIRJUMP, "air jump" ); break;
		case EV_WALL_JUMP:		Sk_Add( SK_TRAVERSAL, SKX_WALLJUMP, "wall jump" ); break;
		case EV_WALLRUN:		Sk_Add( SK_TRAVERSAL, SKX_WALLRUN, "wall run" ); break;
		case EV_SLIDE:			Sk_Add( SK_TRAVERSAL, SKX_SLIDE, "slide" ); break;
		case EV_ZIP_FIRE:		Sk_Add( SK_TRAVERSAL, SKX_ZIP, "zip" ); break;
		case EV_DASH:			Sk_Add( SK_EVASION, SKX_DASH, "dash" ); break;
		case EV_BLINK:			Sk_Add( SK_EVASION, SKX_BLINK, "blink" ); break;
		default: break;
		}
	}
	if ( !Sk_IsHuman( ent ) ) {
		return;
	}
	switch ( event ) {
	case EV_DASH:
	case EV_BLINK:
		sk.dashTime = level.time;
		break;
	case EV_ZIP_END:
		sk.zipEndTime = level.time;
		break;
	case EV_DOUBLE_JUMP:
	case EV_WALL_JUMP:
		if ( Sk_R( SKP_SPRINGHEEL ) && ps->velocity[2] > 0 ) {
			ps->velocity[2] *= 1.0f + ds_perks[SKP_SPRINGHEEL].value * 0.01f;
			Sk_PerkLog( SKP_SPRINGHEEL, "vz -> %.0f", ps->velocity[2] );
		}
		break;
	case EV_WALLRUN:
		if ( Sk_R( SKP_MOMENTUM ) ) {
			s = 1.0f + ds_perks[SKP_MOMENTUM].value * Sk_R( SKP_MOMENTUM ) * 0.01f;
			VectorSet( h, ps->velocity[0], ps->velocity[1], 0 );
			ps->velocity[0] *= s;
			ps->velocity[1] *= s;
			Sk_PerkLog( SKP_MOMENTUM, "speed %.0f -> %.0f", VectorLength( h ), VectorLength( h ) * s );
		}
		break;
	default:
		break;
	}
	(void)parm;
}

/*
===============================================================================

HOOKS FROM THE SHARED FILES

===============================================================================
*/

/*
=================
G_DS_SkillDamage

G_Damage, after the arsenal scale: the perks' damage changes (DESCENT human).
-1 = the hit doesn't happen (SHADOW dodge, SOFT LANDING).
=================
*/
int G_DS_SkillDamage( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, int damage, int mod, int dflags, int hitFlags ) {
	int			pct, red, s, w, before = damage;
	vec3_t		d;
	float		dist;
	gclient_t	*acl;

	// outgoing
	if ( Sk_IsHuman( attacker ) && targ != attacker && targ->client ) {
		acl = attacker->client;
		w = Sk_ModWeapon( mod, attacker );
		s = w > 0 ? BG_DS_SkillForWeapon( w ) : -1;
		VectorSubtract( targ->r.currentOrigin, attacker->r.currentOrigin, d );
		dist = VectorLength( d );
		pct = 0;
		if ( mod == MOD_GAUNTLET ) {
			pct += ds_perks[SKP_BRAWLER].value * Sk_R( SKP_BRAWLER );
		}
		if ( s == SK_BALLISTICS ) {
			pct += ds_perks[SKP_STEADYHANDS].value * Sk_R( SKP_STEADYHANDS );
			if ( Sk_R( SKP_SHREDDER ) && sk.streakTarget == targ->s.number && level.time - sk.streakTime < 1000 ) {
				pct += sk.streakCount < ds_perks[SKP_SHREDDER].value ? sk.streakCount : ds_perks[SKP_SHREDDER].value;
			}
			if ( targ->client->ps.stats[STAT_ARMOR] > 0 ) {
				pct += ds_perks[SKP_PIERCER].value * Sk_R( SKP_PIERCER );
			}
		}
		if ( s == SK_SCATTER ) {
			pct += ds_perks[SKP_BUCKSHOT].value * Sk_R( SKP_BUCKSHOT );
			if ( dist < 250 ) {
				pct += ds_perks[SKP_POINTBLANK].value * Sk_R( SKP_POINTBLANK );
			}
		}
		if ( dflags & DAMAGE_RADIUS ) {
			pct += ds_perks[SKP_DEMOLITION].value * Sk_R( SKP_DEMOLITION );
		}
		if ( s == SK_EXPLOSIVES ) {
			if ( Sk_R( SKP_AIRRAID ) && targ->client->ps.groundEntityNum == ENTITYNUM_NONE ) {
				pct += ds_perks[SKP_AIRRAID].value;
			}
			if ( Sk_R( SKP_IMPACT ) && !( dflags & DAMAGE_RADIUS ) ) {
				pct += ds_perks[SKP_IMPACT].value;
			}
		}
		if ( s == SK_ENERGY ) {
			pct += ds_perks[SKP_CAPACITOR].value * Sk_R( SKP_CAPACITOR );
		}
		if ( mod == MOD_LIGHTNING ) {
			if ( sk.lgTarget != targ->s.number || level.time - sk.lgLast > 200 ) {
				sk.lgTarget = targ->s.number;
				sk.lgStart = level.time;
			}
			sk.lgLast = level.time;
			if ( Sk_R( SKP_STATIC ) && level.time - sk.lgStart >= 1000 ) {
				pct += ds_perks[SKP_STATIC].value;
			}
		}
		if ( mod == MOD_RAILGUN ) {
			pct += ds_perks[SKP_DEADEYE].value * Sk_R( SKP_DEADEYE );
			if ( Sk_R( SKP_SNIPERNEST ) && acl->ps.groundEntityNum != ENTITYNUM_NONE && VectorLength( acl->ps.velocity ) < 40 ) {
				pct += ds_perks[SKP_SNIPERNEST].value;
			}
		}
		if ( hitFlags & HIT_HEADSHOT ) {
			pct += ds_perks[SKP_SKULLCRACKER].value * Sk_R( SKP_SKULLCRACKER );
		}
		if ( Sk_R( SKP_LONGSHOT ) && Sk_HitscanMod( mod ) && dist > 1200 ) {
			pct += ds_perks[SKP_LONGSHOT].value;
		}
		if ( Sk_R( SKP_MARKED ) && targ->s.number < MAX_CLIENTS && sk.markedUntil[targ->s.number] > level.time ) {
			pct += ds_perks[SKP_MARKED].value;
		}
		if ( acl->ps.groundEntityNum == ENTITYNUM_NONE ) {
			pct += ds_perks[SKP_AIRBORNE].value * Sk_R( SKP_AIRBORNE );
		}
		if ( Sk_R( SKP_COUNTERSTEP ) && sk.dashTime && level.time - sk.dashTime < 1500 ) {
			pct += ds_perks[SKP_COUNTERSTEP].value;
		}
		if ( Sk_R( SKP_OVERKILL ) && targ->health < 30 ) {
			pct += ds_perks[SKP_OVERKILL].value;
		}
		if ( pct ) {
			damage = ( damage * ( 100 + pct ) + 50 ) / 100;
			if ( g_skillsDebug.integer ) {
				G_LogPrintf( "SkillPerk: dealt mod %i +%i%% %i -> %i\n", mod, pct, before, damage );
			}
		}
		return damage;
	}

	// incoming
	if ( !Sk_IsHuman( targ ) ) {
		return damage;
	}
	if ( mod == MOD_FALLING && Sk_R( SKP_SOFTLANDING ) ) {
		Sk_PerkLog( SKP_SOFTLANDING, "fall damage %i ignored", damage );
		return -1;
	}
	red = 0;
	if ( attacker == targ ) {
		if ( ( dflags & DAMAGE_RADIUS ) || Sk_ModSkill( mod, attacker ) == SK_EXPLOSIVES ) {
			red += ds_perks[SKP_SAFETYFUSE].value * Sk_R( SKP_SAFETYFUSE );
		}
	} else if ( attacker && attacker->client ) {
		if ( Sk_R( SKP_SHADOW ) && !( dflags & DAMAGE_RADIUS ) && random() * 100 < ds_perks[SKP_SHADOW].value ) {
			Sk_PerkLog( SKP_SHADOW, "dodged %i (mod %i)", damage, mod );
			G_StyleAdd( targ, 10, "DODGE" );
			return -1;
		}
		w = targ->client->ps.weapon;
		if ( w == WP_GAUNTLET ) {
			red += ds_perks[SKP_JUGGERNAUT].value * Sk_R( SKP_JUGGERNAUT );
		}
		if ( w == WP_SHOTGUN ) {
			red += ds_perks[SKP_BRACE].value * Sk_R( SKP_BRACE );
		}
		if ( Sk_ModSkill( mod, attacker ) == SK_ENERGY ) {
			red += ds_perks[SKP_GROUNDED].value * Sk_R( SKP_GROUNDED );
		}
		if ( sk.dashTime && level.time - sk.dashTime < 1000 ) {
			red += ds_perks[SKP_SIDESTEP].value * Sk_R( SKP_SIDESTEP );
		}
		if ( dflags & DAMAGE_RADIUS ) {
			red += ds_perks[SKP_SLIPPERY].value * Sk_R( SKP_SLIPPERY );
		}
		red += ds_perks[SKP_IRONSKIN].value * Sk_R( SKP_IRONSKIN );
		if ( targ->health < 25 ) {
			red += ds_perks[SKP_GRIT].value * Sk_R( SKP_GRIT );
		}
	}
	if ( red ) {
		damage = ( damage * ( 100 - red ) + 50 ) / 100;
	}
	if ( Sk_R( SKP_UNBREAKABLE ) && attacker != targ && damage > 1 ) {
		damage--;
	}
	if ( damage != before && g_skillsDebug.integer ) {
		G_LogPrintf( "SkillPerk: taken mod %i -%i%% %i -> %i\n", mod, red, before, damage );
	}
	return damage;
}

/*
=================
G_DS_SkillHit

G_Damage, after the boss / loot / arsenal adjustments, before the health comes off:
xp for hits and damage lived through, the hit perks, CHEAT DEATH. Returns take.
=================
*/
int G_DS_SkillHit( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, int take, int asave, int mod, int hitFlags ) {
	int		dealt, s, w, n;

	if ( !targ->client || targ->health <= 0 ) {
		return take;
	}
	// training: the tracked player hits an enemy / gets hurt by one
	if ( Sk_Tracked( attacker ) && targ != attacker && !OnSameTeam( targ, attacker ) && !level.intermissiontime ) {
		dealt = ( take < targ->health ? take : targ->health ) + asave;
		s = Sk_ModSkill( mod, attacker );
		if ( s >= 0 && dealt > 0 ) {
			Sk_Add( s, dealt * skDamageXP[s], "damage" );
		}
		if ( hitFlags & HIT_HEADSHOT ) {
			Sk_Add( SK_PRECISION, SKX_HEADSHOT, "headshot" );
		}
	}
	if ( Sk_Tracked( targ ) && attacker && attacker->client && attacker != targ && ( take + asave ) > 0 ) {
		n = targ->health + targ->client->ps.stats[STAT_ARMOR];
		dealt = take + asave < n ? take + asave : n;
		sk.survPending += dealt * SKX_HURT;
		sk.survTime = level.time;
	}
	if ( Sk_Tracked( targ ) && take + asave > 0 ) {
		sk.hurtTime = level.time;
	}

	// perks of the DESCENT human
	if ( Sk_IsHuman( attacker ) && targ != attacker ) {
		w = Sk_ModWeapon( mod, attacker );
		s = w > 0 ? BG_DS_SkillForWeapon( w ) : -1;
		if ( mod == MOD_GAUNTLET && Sk_R( SKP_BLOODLETTER ) ) {
			Sk_Heal( attacker, ds_perks[SKP_BLOODLETTER].value * Sk_R( SKP_BLOODLETTER ), SKP_BLOODLETTER );
		}
		if ( s == SK_BALLISTICS ) {
			if ( sk.streakTarget == targ->s.number && level.time - sk.streakTime < 1000 ) {
				sk.streakCount++;
			} else {
				sk.streakTarget = targ->s.number;
				sk.streakCount = 1;
			}
			sk.streakTime = level.time;
		}
		if ( mod == MOD_RAILGUN && Sk_R( SKP_MARKED ) && targ->s.number < MAX_CLIENTS ) {
			sk.markedUntil[targ->s.number] = level.time + 3000;
			Sk_PerkLog( SKP_MARKED, "client %i for 3 s", targ->s.number );
		}
		if ( targ->s.number < MAX_CLIENTS ) {
			sk.headshotKill[targ->s.number] = ( ( hitFlags & HIT_HEADSHOT ) && take >= targ->health ) ? level.time : 0;
		}
	}
	if ( Sk_IsHuman( targ ) && attacker != targ && Sk_R( SKP_CHEATDEATH ) && !sk.cheatDeathUsed && take >= targ->health ) {
		sk.cheatDeathUsed = 1;
		Sk_PerkLog( SKP_CHEATDEATH, "a %i hit left 1 health", take );
		G_StyleAdd( targ, 30, "CHEATED DEATH" );
		return targ->health - 1;
	}
	return take;
}

/*
=================
G_DS_SkillForceGib

player_die, before the gib decision: MEAT GRINDER, FLAYER
=================
*/
qboolean G_DS_SkillForceGib( gentity_t *self, gentity_t *attacker, int mod ) {
	vec3_t	d;

	if ( !Sk_IsHuman( attacker ) || attacker == self ) {
		return qfalse;
	}
	if ( mod == MOD_GAUNTLET && Sk_R( SKP_MEATGRINDER ) ) {
		Sk_PerkLog( SKP_MEATGRINDER, "gib" );
		return qtrue;
	}
	if ( mod == MOD_SHOTGUN && Sk_R( SKP_FLAYER ) ) {
		VectorSubtract( self->r.currentOrigin, attacker->r.currentOrigin, d );
		if ( VectorLength( d ) < 250 ) {
			Sk_PerkLog( SKP_FLAYER, "gib" );
			return qtrue;
		}
	}
	return qfalse;
}

/*
=================
G_DS_SkillKill

player_die (next to G_DS_MetaKill): kill / gib xp, the kill perks
=================
*/
void G_DS_SkillKill( gentity_t *self, gentity_t *attacker, int mod, qboolean gibbed ) {
	int			s, w, n;
	gclient_t	*cl;

	if ( !self || !self->client ) {
		return;
	}
	if ( Sk_Tracked( self ) ) {
		sk.survPending = 0;		// didn't live through it
	}
	if ( !attacker || !attacker->client || attacker == self || OnSameTeam( self, attacker ) ) {
		return;
	}
	if ( Sk_Tracked( attacker ) && !level.intermissiontime ) {
		s = Sk_ModSkill( mod, attacker );
		if ( s >= 0 ) {
			Sk_Add( s, skKillXP[s], "kill" );
		}
		if ( gibbed ) {
			Sk_Add( SK_CARNAGE, SKX_GIB, "gib" );
		}
	}
	if ( !Sk_IsHuman( attacker ) ) {
		return;
	}
	cl = attacker->client;
	w = Sk_ModWeapon( mod, attacker );
	s = w > 0 ? BG_DS_SkillForWeapon( w ) : -1;
	n = self->s.number;

	if ( mod == MOD_GAUNTLET && Sk_R( SKP_RECKONING ) ) {
		Sk_DashRefill( attacker, DASH_COOLDOWN, SKP_RECKONING );
	}
	if ( mod == MOD_GAUNTLET && Sk_R( SKP_MEATGRINDER ) ) {
		G_DS_SpawnGems( self->r.currentOrigin, 5, WP_GAUNTLET );
		Sk_PerkLog( SKP_MEATGRINDER, "green gem" );
	}
	if ( s == SK_BALLISTICS && Sk_R( SKP_SCAVENGER ) ) {
		Sk_GiveAmmo( attacker, w, ds_perks[SKP_SCAVENGER].value, SKP_SCAVENGER );
	}
	if ( s == SK_SCATTER && Sk_R( SKP_LASTRITES ) ) {
		Sk_Heal( attacker, ds_perks[SKP_LASTRITES].value, SKP_LASTRITES );
	}
	if ( s == SK_EXPLOSIVES && Sk_R( SKP_CHAINFUSE ) ) {
		Sk_GiveAmmo( attacker, w, 1, SKP_CHAINFUSE );
	}
	if ( s == SK_ENERGY && Sk_R( SKP_SIPHON ) ) {
		cl->ps.stats[STAT_ARMOR] += ds_perks[SKP_SIPHON].value;
		if ( cl->ps.stats[STAT_ARMOR] > cl->ps.stats[STAT_MAX_HEALTH] * 2 ) {
			cl->ps.stats[STAT_ARMOR] = cl->ps.stats[STAT_MAX_HEALTH] * 2;
		}
		Sk_PerkLog( SKP_SIPHON, "armor -> %i", cl->ps.stats[STAT_ARMOR] );
	}
	if ( n < MAX_CLIENTS && sk.headshotKill[n] == level.time && Sk_R( SKP_EXECUTIONER ) ) {
		G_DS_SpawnGems( self->r.currentOrigin, 5, w );
		Sk_PerkLog( SKP_EXECUTIONER, "green gem" );
	}
	if ( Sk_R( SKP_SECONDWIND ) ) {
		cl->ps.loopSound &= ~RMZ_USED;
		cl->ps.pm_flags &= ~( PMF_DOUBLE_JUMPED | PMF_EXTRA_JUMPED );
		Sk_PerkLog( SKP_SECONDWIND, "zip + air jump back" );
	}
	if ( Sk_R( SKP_REFLEX ) && sk.dashTime && level.time - sk.dashTime < 1000 ) {
		Sk_DashRefill( attacker, DASH_COOLDOWN, SKP_REFLEX );
	}
	if ( gibbed ) {
		if ( Sk_R( SKP_REDRAIN ) ) {
			Sk_Heal( attacker, ds_perks[SKP_REDRAIN].value * Sk_R( SKP_REDRAIN ), SKP_REDRAIN );
		}
		if ( Sk_R( SKP_GOREHOUND ) ) {
			G_DS_SpawnGems( self->r.currentOrigin, 1, w );
			Sk_PerkLog( SKP_GOREHOUND, "blue gem" );
		}
		if ( Sk_R( SKP_CARNIVAL ) && cl->styleTime < level.time + 1000 ) {
			cl->styleTime = level.time + 1000;		// + the 2 s idle time before the drain = 3 s
			Sk_PerkLog( SKP_CARNIVAL, "style held" );
		}
	}
	if ( Sk_R( SKP_MASSACRE ) && sk.killTime && level.time - sk.killTime < 2000 ) {
		Sk_Heal( attacker, ds_perks[SKP_MASSACRE].value, SKP_MASSACRE );
		Sk_DashRefill( attacker, DASH_COOLDOWN, SKP_MASSACRE );
	}
	sk.killTime = level.time;
}

/*
=================
G_DS_SkillGate

G_RL_GateClient (DESCENT, every ClientThink before Pmove): speed, dash, wall run, regen
=================
*/
void G_DS_SkillGate( gentity_t *ent ) {
	gclient_t		*cl;
	playerState_t	*ps;
	int				msec, r, extra;

	if ( !Sk_IsHuman( ent ) ) {
		return;
	}
	cl = ent->client;
	ps = &cl->ps;
	msec = cl->pers.cmd.serverTime - ps->commandTime;
	if ( msec <= 0 || msec >= 250 ) {
		msec = 0;
	}
	if ( ( r = Sk_R( SKP_MOMENTUM ) ) && ( ps->stats[STAT_RMFLAGS] & RMS_WALLRUN ) ) {
		ps->speed = ps->speed * ( 100 + ds_perks[SKP_MOMENTUM].value * r ) / 100;
	}
	if ( Sk_R( SKP_SLINGSHOT ) && sk.zipEndTime && level.time - sk.zipEndTime < 2000 ) {
		ps->speed = ps->speed * ( 100 + ds_perks[SKP_SLINGSHOT].value ) / 100;
	}
	if ( ( r = Sk_R( SKP_GHOST ) ) && ps->stats[STAT_DASH_TIME] > 0 && msec ) {
		extra = msec * ds_perks[SKP_GHOST].value * r / 100;
		ps->stats[STAT_DASH_TIME] -= extra > 0 ? extra : 1;
		if ( ps->stats[STAT_DASH_TIME] < 0 ) {
			ps->stats[STAT_DASH_TIME] = 0;
		}
	}
	if ( ( r = Sk_R( SKP_SKYWALKER ) ) && ( ps->stats[STAT_RMFLAGS] & RMS_WALLRUN ) && msec ) {
		extra = msec * ds_perks[SKP_SKYWALKER].value * r / 100;
		ps->stats[STAT_WALLRUN_TIME] -= extra > 0 ? extra : 1;
		if ( ps->stats[STAT_WALLRUN_TIME] < 0 ) {
			ps->stats[STAT_WALLRUN_TIME] = 0;
		}
	}
	if ( ( r = Sk_R( SKP_MENDING ) ) && ent->health > 0 && ent->health < 50 ) {
		if ( !sk.mendTime ) {
			sk.mendTime = level.time + 2000;
		} else if ( level.time >= sk.mendTime ) {
			sk.mendTime = level.time + 2000;
			Sk_Heal( ent, ds_perks[SKP_MENDING].value * r, SKP_MENDING );
		}
	} else {
		sk.mendTime = 0;
	}
}

// g_ds_arsenal.c: the held weapon's refire time (percent faster)
int G_DS_SkillFirePct( gentity_t *ent, int w ) {
	int		pct = 0;

	if ( !Sk_IsHuman( ent ) ) {
		return 0;
	}
	switch ( BG_DS_SkillForWeapon( w ) ) {
	case SK_MELEE:		pct += ds_perks[SKP_SWIFTFISTS].value * Sk_R( SKP_SWIFTFISTS ); break;
	case SK_BALLISTICS:	pct += ds_perks[SKP_RAPIDCYCLE].value * Sk_R( SKP_RAPIDCYCLE ); break;
	case SK_SCATTER:	pct += ds_perks[SKP_QUICKPUMP].value * Sk_R( SKP_QUICKPUMP ); break;
	case SK_PRECISION:	pct += ds_perks[SKP_STEADY].value * Sk_R( SKP_STEADY ); break;
	default: break;
	}
	if ( w == WP_PLASMAGUN ) {
		pct += ds_perks[SKP_OVERCLOCK].value * Sk_R( SKP_OVERCLOCK );
	}
	if ( Sk_R( SKP_FRENZY ) && ent->client->style >= STYLE_RANK_S ) {
		pct += ds_perks[SKP_FRENZY].value;
	}
	return pct;
}

int G_DS_SkillFireFlags( gentity_t *ent, int w ) {
	if ( w == WP_CHAINGUN && Sk_IsHuman( ent ) && Sk_R( SKP_SPINDOCTOR ) ) {
		return DSF_SPINFAST;
	}
	return 0;
}

// g_ds_arsenal.c: splash radius and projectile speed bonus (fractions)
float G_DS_SkillAreaBonus( gentity_t *ent, int w ) {
	float	b = 0;

	if ( !Sk_IsHuman( ent ) ) {
		return 0;
	}
	if ( BG_DS_SkillForWeapon( w ) == SK_EXPLOSIVES ) {
		b += ds_perks[SKP_BLASTRADIUS].value * Sk_R( SKP_BLASTRADIUS ) * 0.01f;
	}
	if ( w == WP_PLASMAGUN ) {
		b += ds_perks[SKP_SUPERHEATED].value * Sk_R( SKP_SUPERHEATED ) * 0.01f;
	}
	return b;
}

float G_DS_SkillSpeedBonus( gentity_t *ent, int w ) {
	if ( !Sk_IsHuman( ent ) || !Sk_R( SKP_PAYLOAD ) ) {
		return 0;
	}
	if ( w == WP_ROCKET_LAUNCHER || w == WP_GRENADE_LAUNCHER
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
		|| w == WP_PROX_LAUNCHER
#endif
		) {
		return ds_perks[SKP_PAYLOAD].value * 0.01f;
	}
	return 0;
}

// g_ds_arsenal.c G_DS_GauntletRange
float G_DS_SkillGauntletScale( gentity_t *ent ) {
	return Sk_IsHuman( ent ) && Sk_R( SKP_LONGARM ) ? 1.0f + ds_perks[SKP_LONGARM].value * 0.01f : 1.0f;
}

// g_weapon.c FireWeapon (pmove already took the ammo): a chance the shot was free
void G_DS_SkillOnFire( gentity_t *ent ) {
	int		w, perk, s;

	if ( !Sk_IsHuman( ent ) ) {
		return;
	}
	w = ent->s.weapon;
	s = BG_DS_SkillForWeapon( w );
	perk = s == SK_BALLISTICS ? SKP_DISCIPLINE : s == SK_SCATTER ? SKP_SECONDBARREL : s == SK_ENERGY ? SKP_RECYCLER : -1;
	if ( perk < 0 || !Sk_R( perk ) || ent->client->ps.ammo[w] < 0 ) {
		return;
	}
	if ( random() * 100 < ds_perks[perk].value ) {
		ent->client->ps.ammo[w]++;
		Sk_PerkLog( perk, "free shot w%i ammo %i", w, ent->client->ps.ammo[w] );
	}
}

// G_RL_MaxHealthBonus (the player): TOUGHNESS
int G_DS_SkillMaxHealth( gentity_t *ent ) {
	return Sk_IsHuman( ent ) ? ds_perks[SKP_TOUGHNESS].value * Sk_R( SKP_TOUGHNESS ) : 0;
}

// G_RL_SpawnLoadout: ARMORER
void G_DS_SkillSpawn( gentity_t *ent ) {
	int		a;

	if ( !Sk_IsHuman( ent ) || !Sk_R( SKP_ARMORER ) ) {
		return;
	}
	a = ds_perks[SKP_ARMORER].value;
	if ( ent->client->ps.stats[STAT_ARMOR] < a ) {
		ent->client->ps.stats[STAT_ARMOR] = a;
		Sk_PerkLog( SKP_ARMORER, "armor %i", a );
	}
}

// G_StyleAdd: CARNAGE xp, SHOWOFF
int G_DS_SkillStyle( gentity_t *ent, int points ) {
	int		r;

	if ( points <= 0 ) {
		return points;
	}
	if ( Sk_Tracked( ent ) ) {
		Sk_Add( SK_CARNAGE, points * SKX_STYLE, "style" );
	}
	if ( Sk_IsHuman( ent ) && ( r = Sk_R( SKP_SHOWOFF ) ) ) {
		points = ( points * ( 100 + ds_perks[SKP_SHOWOFF].value * r ) + 50 ) / 100;
	}
	return points;
}

/*
=================
G_DS_SkillsCommand

Dev: dsskills, dsskillxp <skill> <xp>, dsperk <perk> <rank>, dsskillsave
=================
*/
qboolean G_DS_SkillsCommand( const char *cmd ) {
	char	arg[32];
	int		s, n, into, need, l, levels[SK_NUM];

	if ( !Q_stricmp( cmd, "dsskills" ) ) {
		Sk_Levels( levels );
		G_Printf( "skills: training %i client %i effects %i char level %i points %i\n", sk.training, sk.client,
			Sk_Effects(), BG_DS_CharLevel( levels ), BG_DS_PerkPoints( levels, sk.perks ) );
		for ( s = 0 ; s < SK_NUM ; s++ ) {
			l = BG_DS_SkillLevel( Sk_Total( s ), &into, &need );
			G_Printf( "  %-10s lv %3i  xp %7i (base %i, map %i)  %i/%i\n", ds_skills[s].name, l, Sk_Total( s ),
				sk.base[s], sk.live[s], into, need );
		}
		G_Printf( "  perks %s\n", Sk_Csv( sk.perks, SKP_NUM ) );
		G_LogPrintf( "Skills: levels %s xp %s map %s perks %s\n", Sk_Csv( levels, SK_NUM ), Sk_Csv( sk.base, SK_NUM ),
			Sk_Csv( sk.live, SK_NUM ), Sk_Csv( sk.perks, SKP_NUM ) );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsskillxp" ) ) {
		trap_Argv( 1, arg, sizeof( arg ) );
		s = atoi( arg );
		trap_Argv( 2, arg, sizeof( arg ) );
		n = atoi( arg );
		if ( s >= 0 && s < SK_NUM && n > 0 && sk.client >= 0 ) {
			n = sk.training;
			sk.training = qtrue;
			trap_Argv( 2, arg, sizeof( arg ) );
			Sk_Add( s, (float)atoi( arg ) / ( G_RL_Active() ? G_DS_RewardMult() : SK_NORMAL_MULT ), "dev" );
			sk.training = n;
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsperk" ) ) {
		trap_Argv( 1, arg, sizeof( arg ) );
		s = atoi( arg );
		trap_Argv( 2, arg, sizeof( arg ) );
		n = atoi( arg );
		if ( s >= 0 && s < SKP_NUM ) {
			sk.perks[s] = n < 0 ? 0 : n > ds_perks[s].maxRank ? ds_perks[s].maxRank : n;
			G_Printf( "dsperk: %s rank %i (this map)\n", ds_perks[s].name, sk.perks[s] );
		}
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsperkall" ) ) {
		trap_Argv( 1, arg, sizeof( arg ) );
		n = atoi( arg );
		for ( s = 0 ; s < SKP_NUM ; s++ ) {
			sk.perks[s] = n < 0 ? 0 : n > ds_perks[s].maxRank ? ds_perks[s].maxRank : n;
		}
		G_Printf( "dsperkall: every perk at rank %i (capped at its max) this map\n", n );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsskillsave" ) ) {
		G_DS_SkillsSave();
		return qtrue;
	}
	return qfalse;
}
