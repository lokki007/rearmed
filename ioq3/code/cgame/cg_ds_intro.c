/*
===========================================================================
REARMED -- "announce the enemies like on single play" (ds-intro), cgame side.

1. Loading screen of a DESCENT floor (CG_DSI_DrawLoading from CG_DrawInformation): the floor,
   the act, the goal and the opponents with their head portraits (the player's own Q3 install:
   models/players/<model>/icon_<skin>.tga), names and a one-line threat note. The roster comes
   from CS_DSINTRO (g_ds_intro.c), the boss from CS_DESCENTBOSS, the run from CS_DESCENT.
2. At the floor start a short versus intro along the TOP EDGE (never the screen centre):
   YOU - VS - the opponents sliding in from the screen edges, the announcer reading the
   matchup (the player's own sound/player/announce/<Name>.wav, our generated boss / elite /
   nemesis / rivals lines in sound/rearmed/dsintro, the stock "Fight!"). Boss floors: a bigger
   boss block (name, title, its two strong sides), the two rivals smaller.
   ~3.3-4 s, firing skips it. While it runs the floor strip (cg_roguelike.c) stays away (the
   intro carries its information), the act card and the boss bar (cg_ds_acts.c) wait for it.
3. Skirmish (FFA, tournament, SP arenas): the same top-edge versus strip once the bots are in
   at the match start (names + "Fight!" in FFA; SP / tournament have their own announcer).
cg_dsIntro 0 turns 2 and 3 off (the loading screen roster always shows).
===========================================================================
*/
#include "cg_local.h"
#include "../game/bg_ds_types.h"

#define DSI_MAX			12
#define DSI_MAXQ		12
#define DSI_SLIDE		450
#define DSI_OUT			380
#define DSI_SKIP_OUT	260

#define DSR_BOT			2
#define DSR_ELITE		3
#define DSR_BOSS		4

typedef struct {
	char		name[32];		// clean, upper case
	char		model[MAX_QPATH];
	int			role, flags, lvl, abil, alts, perks, evo, nemKills, skill;
	qhandle_t	icon;
	sfxHandle_t	voice;
	int			flash;			// cg.time the announcer read it
	// ds-arrivals (CS_DSARRIVE): staggered entry
	int			plan;			// plan index (CS_DSINTRO f<i>)
	qboolean	late;			// joins after the start
	int			arrWarn, arrLand, arrIn;	// server times: warned / lands / joined (0 not yet)
	qboolean	announced, landed;
	int			rank, met;		// en-personality: veteran rank, 1 met before / 2 promoted since (CS_DSINTRO vr / vm)
	int			affix;			// en-affix: CS_DSINTRO a<i>, the elite's DSAF_ mask
	int			arch;			// en-types: CS_DSINTRO t<i>, the archetype (DSAR_)
} dsiFoe_t;

typedef struct {
	vmCvar_t	cv;
	// CS_DSINTRO copy (re-parsed when it changes)
	char		cs[MAX_INFO_STRING];
	int			num;
	dsiFoe_t	foe[DSI_MAX];
	int			boss;			// index of the boss, -1
	int			nemLevel;		// en-personality: CS_DSINTRO nl / nt
	char		nemTitle[32];
	char		youModel[MAX_QPATH];
	qhandle_t	youIcon;
	// CS_DESCENT / CS_DESCENTBOSS
	qboolean	ds;
	int			floor, nodeType, frags, lives, dif, phase, mut;
	int			bossType, traits, abyss, bossLives;
	int			bossTimed, bossTime;		// en-boss: a timed 1v1 boss floor, its clock (s)
	// the intro
	int			state;			// 0 waiting, 1 running, 2 over
	qboolean	skirmish;
	int			start, end, skipped;
	int			lastButtons, lastCmd;
	int			nq, qi, qAt[DSI_MAXQ], qFoe[DSI_MAXQ];
	sfxHandle_t	qSnd[DSI_MAXQ];
	int			qChan[DSI_MAXQ];
	qboolean	fightQueued;
	int			skCount, skSince;		// skirmish: the bot count and since when it is stable
	int			loadUntil;				// dev: the loading screen drawn again until then
	sfxHandle_t	sndBoss[BOSS_NUM], sndElite, sndNemesis, sndRivals, sndRoar, sndSwoosh;
	// ds-arrivals: CS_DSARRIVE, the slim strip (state 3) that fills up as they arrive
	char		acs[MAX_INFO_STRING];
	qboolean	stag;
	int			ns, numOrder, order[DSI_MAX];	// start count, plan indexes in arrival order
	float		curS, hdr;
	int			bigUntil, stripOut, stripEnd, lastLand;
	sfxHandle_t	sndRival;
} dsiState_t;

static dsiState_t	dsi;

static const char *dsiBossVoice[BOSS_NUM] = { "boss_butcher", "boss_overseer", "boss_hollowking", "boss_archdemon" };
// how long each of our voice lines speaks before the next may start (msec)
static const int dsiBossGap[BOSS_NUM] = { 900, 1150, 1150, 1150 };
#define DSI_GAP_NAME	600
#define DSI_GAP_ELITE	520
#define DSI_GAP_NEMESIS	950
#define DSI_GAP_RIVALS	760
#define DSI_VOICE_UNTIL	2300		// no new name after this (the intro stays ~3.5 s)

static const vec4_t dsiColFight = { 0.30f, 0.95f, 0.42f, 1 };
static const vec4_t dsiColElite = { 1.00f, 0.55f, 0.12f, 1 };
static const vec4_t dsiColBoss = { 0.78f, 0.42f, 1.00f, 1 };
static const vec4_t dsiColEvent = { 0.32f, 0.62f, 1.00f, 1 };
static const vec4_t dsiColNem = { 0.90f, 0.22f, 0.18f, 1 };
static const char *dsiNodeNames[5] = { "FIGHT", "ELITE", "BOSS", "EVENT", "VAULT" };

/*
===============================================================================

DATA

===============================================================================
*/

// the head portrait of a "model/skin" (as CG_LoadingClient finds it)
static qhandle_t DSI_Icon( const char *modelSkin ) {
	char		model[MAX_QPATH];
	char		*skin;
	qhandle_t	h;

	Q_strncpyz( model, modelSkin && modelSkin[0] ? modelSkin : DEFAULT_MODEL, sizeof( model ) );
	skin = strrchr( model, '/' );
	if ( skin ) {
		*skin++ = '\0';
	} else {
		skin = "default";
	}
	h = trap_R_RegisterShaderNoMip( va( "models/players/%s/icon_%s.tga", model, skin ) );
	if ( !h ) {
		h = trap_R_RegisterShaderNoMip( va( "models/players/characters/%s/icon_%s.tga", model, skin ) );
	}
	if ( !h ) {
		h = trap_R_RegisterShaderNoMip( va( "models/players/%s/icon_default.tga", model ) );
	}
	if ( !h ) {
		h = trap_R_RegisterShaderNoMip( va( "models/players/%s/icon_%s.tga", DEFAULT_MODEL, "default" ) );
	}
	return h;
}

static const char *DSI_Field( const char *s, int *out ) {
	*out = atoi( s );
	while ( *s && *s != ',' ) {
		s++;
	}
	return *s == ',' ? s + 1 : s;
}

// the run (CS_DESCENT) and the boss (CS_DESCENTBOSS): cheap, every frame
static void DSI_ParseRun( void ) {
	const char	*info = CG_ConfigString( CS_DESCENT );
	const char	*b = CG_ConfigString( CS_DESCENTBOSS );

	dsi.ds = info[0] && atoi( Info_ValueForKey( info, "on" ) ) == 1;
	if ( !dsi.ds ) {
		return;
	}
	dsi.floor = atoi( Info_ValueForKey( info, "fl" ) );
	dsi.nodeType = atoi( Info_ValueForKey( info, "nt" ) );
	if ( dsi.nodeType < 0 || dsi.nodeType > 4 ) {
		dsi.nodeType = 0;
	}
	dsi.frags = atoi( Info_ValueForKey( info, "fr" ) );
	dsi.lives = atoi( Info_ValueForKey( info, "lv" ) );
	dsi.dif = atoi( Info_ValueForKey( info, "dif" ) );
	dsi.phase = atoi( Info_ValueForKey( info, "ph" ) );
	dsi.mut = atoi( Info_ValueForKey( info, "mut" ) );
	dsi.bossType = -1;
	if ( b[0] && Info_ValueForKey( b, "b" )[0] ) {
		dsi.bossType = atoi( Info_ValueForKey( b, "b" ) );
		if ( dsi.bossType < 0 || dsi.bossType >= BOSS_NUM ) {
			dsi.bossType = 0;
		}
		dsi.traits = atoi( Info_ValueForKey( b, "tr" ) );
		dsi.abyss = atoi( Info_ValueForKey( b, "ab" ) );
		dsi.bossLives = atoi( Info_ValueForKey( b, "lv" ) );
		dsi.bossTimed = atoi( Info_ValueForKey( b, "tm" ) );
		dsi.bossTime = atoi( Info_ValueForKey( b, "tl" ) );
	}
}

static dsiFoe_t *DSI_FoeByPlan( int plan );

// CS_DSINTRO: the floor's opponents (only when it changed)
static void DSI_ParseRoster( void ) {
	const char	*cs = CG_ConfigString( CS_DSINTRO );
	const char	*s;
	dsiFoe_t	*f;
	char		key[8];
	int			i, n, len, affx;

	if ( !strcmp( cs, dsi.cs ) ) {
		return;
	}
	Q_strncpyz( dsi.cs, cs, sizeof( dsi.cs ) );
	dsi.num = 0;
	dsi.boss = -1;
	if ( !cs[0] ) {
		return;
	}
	n = atoi( Info_ValueForKey( cs, "n" ) );
	for ( i = 0 ; i < n && dsi.num < DSI_MAX ; i++ ) {
		affx = atoi( Info_ValueForKey( cs, va( "a%i", i ) ) );		// en-affix
		Com_sprintf( key, sizeof( key ), "f%i", i );
		s = Info_ValueForKey( cs, key );
		if ( !s[0] ) {
			continue;
		}
		f = &dsi.foe[dsi.num];
		memset( f, 0, sizeof( *f ) );
		f->plan = i;
		f->affix = affx;
		f->arch = atoi( Info_ValueForKey( cs, va( "t%i", i ) ) );		// en-types
		if ( f->arch < 0 || f->arch >= DSAR_NUM ) {
			f->arch = 0;
		}
		dsi.acs[0] = 0;		// ds-arrivals: apply CS_DSARRIVE to the new roster
		s = DSI_Field( s, &f->role );
		s = DSI_Field( s, &f->flags );
		s = DSI_Field( s, &f->lvl );
		s = DSI_Field( s, &f->abil );
		s = DSI_Field( s, &f->alts );
		s = DSI_Field( s, &f->perks );
		s = DSI_Field( s, &f->evo );
		s = DSI_Field( s, &f->nemKills );
		for ( len = 0 ; s[len] && s[len] != ',' && len < MAX_QPATH - 1 ; len++ ) {
			f->model[len] = s[len];
		}
		f->model[len] = 0;
		s += len;
		if ( *s == ',' ) {
			s++;
		}
		CG_CleanUpper( s, f->name, sizeof( f->name ) );
		f->icon = DSI_Icon( f->model );
		if ( f->role == DSR_BOSS ) {
			dsi.boss = dsi.num;
		} else {
			// the player's own Q3 announcer reads the bot names (Sarge.wav ..); missing = 0
			char	file[32];

			Q_strncpyz( file, s, sizeof( file ) );
			Q_CleanStr( file );
			f->voice = trap_S_RegisterSound( va( "sound/player/announce/%s.wav", file ), qtrue );
		}
		dsi.num++;
	}
	// en-personality: veteran ranks / met flags by plan index, the nemesis's level and title
	{
		const char	*vr = Info_ValueForKey( cs, "vr" );
		const char	*vm = Info_ValueForKey( cs, "vm" );
		int			k, v;

		for ( k = 0 ; k < DSI_MAX && ( *vr || *vm ) ; k++ ) {
			f = DSI_FoeByPlan( k );
			if ( *vr ) {
				vr = DSI_Field( vr, &v );
				if ( f ) {
					f->rank = v < 0 ? 0 : v > DS_MAX_RANK ? DS_MAX_RANK : v;
				}
			}
			if ( *vm ) {
				vm = DSI_Field( vm, &v );
				if ( f ) {
					f->met = v;
				}
			}
		}
		dsi.nemLevel = atoi( Info_ValueForKey( cs, "nl" ) );
		Q_strncpyz( dsi.nemTitle, Info_ValueForKey( cs, "nt" ), sizeof( dsi.nemTitle ) );
		Q_strupr( dsi.nemTitle );
	}
}

static void DSI_ParseYou( void ) {
	const char	*info = CG_ConfigString( CS_PLAYERS + cg.clientNum );
	const char	*m = Info_ValueForKey( info, "model" );

	if ( Q_stricmp( m, dsi.youModel ) || !dsi.youIcon ) {
		Q_strncpyz( dsi.youModel, m, sizeof( dsi.youModel ) );
		dsi.youIcon = DSI_Icon( m );
	}
}

static void DSI_Announce( dsiFoe_t *f );

static dsiFoe_t *DSI_FoeByPlan( int plan ) {
	int		i;

	for ( i = 0 ; i < dsi.num ; i++ ) {
		if ( dsi.foe[i].plan == plan ) {
			return &dsi.foe[i];
		}
	}
	return NULL;
}

/*
=================
DSI_ParseArrive

ds-arrivals: CS_DSARRIVE (only when it changed). A late joiner warned just now gets its
moment: the announcer reads its name alone, its portrait slides into the strip.
=================
*/
static void DSI_ParseArrive( void ) {
	const char	*cs = CG_ConfigString( CS_DSARRIVE );
	const char	*o;
	dsiFoe_t	*f;
	int			i, v;

	if ( !strcmp( cs[0] ? cs : " ", dsi.acs ) ) {
		return;
	}
	Q_strncpyz( dsi.acs, cs[0] ? cs : " ", sizeof( dsi.acs ) );
	dsi.stag = cs[0] && atoi( Info_ValueForKey( cs, "st" ) ) == 1 && dsi.ds;
	dsi.numOrder = 0;
	for ( i = 0 ; i < dsi.num ; i++ ) {
		dsi.foe[i].late = qfalse;
	}
	if ( !dsi.stag ) {
		return;
	}
	dsi.ns = atoi( Info_ValueForKey( cs, "ns" ) );
	o = Info_ValueForKey( cs, "o" );
	while ( *o && dsi.numOrder < DSI_MAX ) {
		o = DSI_Field( o, &v );
		dsi.order[dsi.numOrder] = v;
		f = DSI_FoeByPlan( v );
		if ( f ) {
			f->late = dsi.numOrder >= dsi.ns;
		}
		dsi.numOrder++;
	}
	for ( i = 0 ; i < dsi.num ; i++ ) {
		f = &dsi.foe[i];
		f->arrWarn = atoi( Info_ValueForKey( cs, va( "w%i", f->plan ) ) );
		f->arrLand = atoi( Info_ValueForKey( cs, va( "l%i", f->plan ) ) );
		f->arrIn = atoi( Info_ValueForKey( cs, va( "in%i", f->plan ) ) );
		if ( f->late && f->arrLand > dsi.lastLand ) {
			dsi.lastLand = f->arrLand;
		}
		if ( f->late && f->arrWarn && !f->announced ) {
			f->announced = qtrue;
			CG_DevPrintf( "DSArrive: cg warn %s at %i (lands %i)\n", f->name, f->arrWarn, f->arrLand );
			if ( dsi.cv.integer > 0 && cg.time - f->arrWarn < 4000 && ( dsi.state == 1 || dsi.state == 3 ) ) {
				DSI_Announce( f );
			}
		}
	}
}

/*
=================
CG_DSI_Register

CG_Init: the cvar, our voice lines, the roster's portraits and name sounds
=================
*/
void CG_DSI_Register( void ) {
	int		i;

	trap_Cvar_Register( &dsi.cv, "cg_dsIntro", "1", CVAR_ARCHIVE );
	memset( dsi.cs, 0, sizeof( dsi.cs ) );		// (the rest is zero at VM start)
	dsi.state = 0;
	dsi.num = 0;
	dsi.boss = -1;
	for ( i = 0 ; i < BOSS_NUM ; i++ ) {
		dsi.sndBoss[i] = trap_S_RegisterSound( va( "sound/rearmed/dsintro/%s.wav", dsiBossVoice[i] ), qfalse );
	}
	dsi.sndElite = trap_S_RegisterSound( "sound/rearmed/dsintro/elite.wav", qfalse );
	dsi.sndNemesis = trap_S_RegisterSound( "sound/rearmed/dsintro/nemesis.wav", qfalse );
	dsi.sndRivals = trap_S_RegisterSound( "sound/rearmed/dsintro/rivals.wav", qfalse );
	dsi.sndRoar = trap_S_RegisterSound( "sound/rearmed/acts/roar.wav", qfalse );
	dsi.sndSwoosh = trap_S_RegisterSound( "sound/rearmed/rl/rl_card.wav", qfalse );
	dsi.sndRival = trap_S_RegisterSound( "sound/rearmed/dsintro/rival.wav", qfalse );
	dsi.acs[0] = 0;
	dsi.stag = qfalse;
	DSI_ParseRun();
	DSI_ParseRoster();
	DSI_ParseYou();
}

/*
===============================================================================

DRAW HELPERS

===============================================================================
*/

static void DSI_Text( float x, float y, const char *s, const float *color, float cw, float ch, int align ) {
	int		icw = (int)( cw + 0.5f ), ich = (int)( ch + 0.5f );
	float	w;

	if ( icw < 1 ) {
		icw = 1;
	}
	if ( ich < 1 ) {
		ich = 1;
	}
	w = CG_DrawStrlen( s ) * icw;
	if ( align == 0 ) {
		x -= w * 0.5f;
	} else if ( align > 0 ) {
		x -= w;
	}
	CG_DrawStringExt( (int)x, (int)y, s, color, qtrue, qtrue, icw, ich, 0 );
}

static void DSI_Frame( float x, float y, float w, float h, float t, const float *col ) {
	CG_FillRect( x - t, y - t, w + 2 * t, t, col );
	CG_FillRect( x - t, y + h, w + 2 * t, t, col );
	CG_FillRect( x - t, y, t, h, col );
	CG_FillRect( x + w, y, t, h, col );
}

static float DSI_Ease( float t ) {
	t = CG_Clamp01( t );
	return 1.0f - ( 1.0f - t ) * ( 1.0f - t ) * ( 1.0f - t );
}

static void DSI_Col( vec4_t out, const float *c, float a ) {
	out[0] = c[0]; out[1] = c[1]; out[2] = c[2]; out[3] = a;
}

static const float *DSI_RoleColor( const dsiFoe_t *f ) {
	if ( f->role == DSR_BOSS ) {
		return dsiColBoss;
	}
	if ( f->flags & DSI_NEMESIS ) {
		return dsiColNem;
	}
	if ( f->role == DSR_ELITE ) {
		return dsiColElite;
	}
	return NULL;
}

// the one-line threat note under a name
static const char *DSI_Note( const dsiFoe_t *f ) {
	static char	buf[40];

	if ( f->role == 1 ) {		// the player: their lives
		if ( dsi.skirmish ) {
			buf[0] = 0;
		} else if ( dsi.boss >= 0 && dsi.bossTimed ) {
			Com_sprintf( buf, sizeof( buf ), "%i:%02i TO KILL IT", dsi.bossTime / 60, dsi.bossTime % 60 );	// en-boss
		} else {
			int		lv = dsi.boss >= 0 && dsi.bossLives > 0 ? dsi.bossLives : dsi.lives;

			Com_sprintf( buf, sizeof( buf ), "%i %s", lv, lv == 1 ? "LIFE" : "LIVES" );
		}
	} else if ( dsi.skirmish ) {
		Com_sprintf( buf, sizeof( buf ), f->skill > 0 ? "SKILL %i" : "", f->skill );
	} else if ( ( f->flags & DSI_NEMESIS ) && dsi.nemLevel > 0 ) {
		// en-personality: its level (and title, when it has one)
		if ( dsi.nemTitle[0] ) {
			Com_sprintf( buf, sizeof( buf ), "LV%i NEMESIS  %s", dsi.nemLevel, dsi.nemTitle );
		} else {
			Com_sprintf( buf, sizeof( buf ), "LV%i NEMESIS  %i KILLS", dsi.nemLevel, f->nemKills );
		}
	} else if ( f->flags & DSI_NEMESIS ) {
		Com_sprintf( buf, sizeof( buf ), f->nemKills > 0 ? "NEMESIS  %i KILLS" : "NEMESIS", f->nemKills );
	} else if ( f->met == 2 && f->rank > 0 && !( f->flags & DSI_RIVAL ) ) {
		Com_sprintf( buf, sizeof( buf ), "PROMOTED  VETERAN %s", ds_roman[f->rank] );		// en-personality
	} else if ( f->rank > 0 && f->role != DSR_ELITE && !( f->flags & DSI_RIVAL ) ) {
		Com_sprintf( buf, sizeof( buf ), f->met ? "VETERAN %s  LV%i  BACK" : "VETERAN %s  LV%i", ds_roman[f->rank], f->lvl );
	} else if ( f->role == DSR_ELITE ) {
		if ( f->evo > 0 ) {
			Com_sprintf( buf, sizeof( buf ), "ELITE  LV%i  %i EVO", f->lvl, f->evo );
		} else {
			Com_sprintf( buf, sizeof( buf ), "ELITE  LV%i  %i ABIL", f->lvl, f->abil );
		}
	} else if ( f->flags & DSI_RIVAL ) {
		Com_sprintf( buf, sizeof( buf ), "RIVAL  LV%i", f->lvl );
	} else if ( f->abil > 0 ) {
		Com_sprintf( buf, sizeof( buf ), "LV%i  %i ABIL", f->lvl, f->abil );
	} else if ( f->alts > 0 ) {
		Com_sprintf( buf, sizeof( buf ), "LV%i  %i ALT", f->lvl, f->alts );
	} else {
		Com_sprintf( buf, sizeof( buf ), "WEAPONS LV%i", f->lvl );
	}
	return buf;
}

// the boss's strong sides: "FAST + BRUTAL" (and the weak one: "FRAIL")
static void DSI_BossSides( char *strong, char *weak, int size ) {
	const char	*s[3];
	int			n = 0;

	if ( dsi.traits & 1 ) {
		s[n++] = "FAST";
	}
	if ( dsi.traits & 2 ) {
		s[n++] = "ARMOURED";
	}
	if ( dsi.traits & 4 ) {
		s[n++] = "BRUTAL";
	}
	if ( n >= 2 ) {
		Com_sprintf( strong, size, "%s + %s", s[0], s[1] );
	} else {
		Q_strncpyz( strong, n ? s[0] : "", size );
	}
	weak[0] = 0;
	if ( !( dsi.traits & 1 ) ) {
		Q_strncpyz( weak, "SLOW", size );
	} else if ( !( dsi.traits & 2 ) ) {
		Q_strncpyz( weak, "FRAIL", size );
	} else if ( !( dsi.traits & 4 ) ) {
		Q_strncpyz( weak, "WEAK HITS", size );
	}
}

// header: "FLOOR 7  ELITE" and the goal line
static void DSI_Header( char *title, char *goal, int size, vec4_t ncol ) {
	if ( dsi.skirmish ) {
		Q_strncpyz( title, cgs.gametype == GT_TOURNAMENT ? "TOURNAMENT" : cgs.gametype == GT_SINGLE_PLAYER ? "ARENA" : "FREE FOR ALL", size );
		if ( cgs.fraglimit > 0 ) {
			Com_sprintf( goal, size, "FIRST TO %i FRAGS", cgs.fraglimit );
		} else {
			goal[0] = 0;
		}
		DSI_Col( ncol, dsiColFight, 1 );
		return;
	}
	if ( dsi.nodeType == 3 && dsi.mut > 0 && dsi.mut < MUT_NUM_MUTATORS ) {
		Com_sprintf( title, size, "FLOOR %i  EVENT: %s", dsi.floor, bg_rmMutatorNames[dsi.mut] );
	} else {
		Com_sprintf( title, size, "FLOOR %i  %s", dsi.floor, dsiNodeNames[dsi.nodeType] );
	}
	DSI_Col( ncol, dsi.nodeType == 2 ? dsiColBoss : dsi.nodeType == 1 ? dsiColElite : dsi.nodeType == 3 ? dsiColEvent : dsiColFight, 1 );
	if ( dsi.bossType >= 0 && dsi.boss >= 0 && dsi.bossTimed ) {
		Com_sprintf( goal, size, "KILL %s IN %i:%02i  -  DIE AS OFTEN AS YOU MUST", ds_bosses[dsi.bossType].name,
			dsi.bossTime / 60, dsi.bossTime % 60 );		// en-boss: timed 1v1
	} else if ( dsi.bossType >= 0 && dsi.boss >= 0 ) {
		Com_sprintf( goal, size, "ELIMINATE %s  -  EVERYONE HAS %i LIVES", ds_bosses[dsi.bossType].name,
			dsi.bossLives > 0 ? dsi.bossLives : 10 );
	} else {
		Com_sprintf( goal, size, "FIRST TO %i FRAGS  -  %i %s LEFT", dsi.frags, dsi.lives, dsi.lives == 1 ? "LIFE" : "LIVES" );
	}
}

/*
===============================================================================

THE ROW (loading screen and versus intro)

All sizes in virtual units x s. t < 0: static (the loading screen); else msec since the start.
out: 0..1 leaving (slides up, fades).
===============================================================================
*/

#define DSI_YOU_W		62
#define DSI_VS_W		32
#define DSI_SLOT_W		76
#define DSI_PORT		26
#define DSI_BOSS_PORT	42
#define DSI_RIVAL_W		70
#define DSI_RIVAL_PORT	22

static float DSI_BossBlockW( void ) {
	float	w, tw;
	char	strong[40], weak[40];

	if ( dsi.boss < 0 || dsi.bossType < 0 ) {
		return 0;
	}
	DSI_BossSides( strong, weak, sizeof( strong ) );
	w = CG_DrawStrlen( ds_bosses[dsi.bossType].name ) * 9;
	tw = CG_DrawStrlen( ds_bosses[dsi.bossType].title ) * 5;
	if ( tw > w ) {
		w = tw;
	}
	tw = ( CG_DrawStrlen( strong ) + CG_DrawStrlen( weak ) + 2 ) * 5;
	if ( tw > w ) {
		w = tw;
	}
	return DSI_BOSS_PORT + 8 + w + 10;
}

static float DSI_RowWidth( void ) {
	int		others = dsi.num - ( dsi.boss >= 0 ? 1 : 0 );

	if ( dsi.boss >= 0 ) {
		return DSI_YOU_W + DSI_VS_W + DSI_BossBlockW() + others * DSI_RIVAL_W;
	}
	return DSI_YOU_W + DSI_VS_W + dsi.num * DSI_SLOT_W;
}

// one portrait with its frame, the read-out flash, name and note under it
static void DSI_DrawFoe( dsiFoe_t *f, float cx, float y, float port, float s, float alpha, qboolean small ) {
	vec4_t		col, fr;
	const float	*rc = DSI_RoleColor( f );
	float		fl = 0, p, nameW = small ? 4.5f : 5.0f;

	float		nk = 1.0f;

	if ( f->flash && cg.time - f->flash < 500 && cg.time >= f->flash ) {
		fl = 1.0f - ( cg.time - f->flash ) / 500.0f;
	}
	// ds-arrivals: on its way (warned, not landed yet): the frame pulses
	if ( f->late && f->arrWarn && cg.time < f->arrLand && fl < 0.5f ) {
		fl = 0.25f + 0.25f * sin( cg.time * 0.02f );
	}
	// the slim strip: names only, the note for the one arriving now
	if ( dsi.state == 3 ) {
		nk = dsi.hdr;
		if ( f->late && f->arrWarn && cg.time - f->arrWarn < 4500 ) {
			nk = 1.0f;
		}
	}
	p = port * s * ( 1.0f + 0.14f * fl );
	col[0] = col[1] = col[2] = 0; col[3] = 0.55f * alpha;
	CG_FillRect( cx - p * 0.5f, y + ( port * s - p ) * 0.5f, p, p, col );
	col[0] = col[1] = col[2] = 0.75f + 0.25f * fl; col[3] = alpha;
	trap_R_SetColor( col );
	if ( f->arch == DSAR_MIMIC && dsi.youIcon ) {
		CG_DrawPic( cx - p * 0.5f, y + ( port * s - p ) * 0.5f, p, p, dsi.youIcon );		// en-types: the mimic wears your face
	} else if ( f->icon ) {
		CG_DrawPic( cx - p * 0.5f, y + ( port * s - p ) * 0.5f, p, p, f->icon );
	}
	trap_R_SetColor( NULL );
	if ( rc ) {
		DSI_Col( fr, rc, alpha );
	} else {
		fr[0] = fr[1] = fr[2] = 0.55f; fr[3] = 0.8f * alpha;
	}
	fr[0] += ( 1 - fr[0] ) * fl; fr[1] += ( 1 - fr[1] ) * fl; fr[2] += ( 1 - fr[2] ) * fl;
	DSI_Frame( cx - p * 0.5f, y + ( port * s - p ) * 0.5f, p, p, ( rc ? 1.2f : 0.7f ) * s, fr );
	// en-personality: veteran rank pips along the portrait's top edge
	if ( f->rank > 0 ) {
		int		k;
		float	pw = 2.2f * s, ph = 4.0f * s, px = cx - p * 0.5f + 1.5f * s, py = y + ( port * s - p ) * 0.5f + 1.5f * s;

		col[0] = f->rank >= 5 ? 1.0f : f->rank >= 3 ? 0.8f : 0.9f;
		col[1] = f->rank >= 5 ? 0.8f : f->rank >= 3 ? 0.88f : 0.55f;
		col[2] = f->rank >= 5 ? 0.3f : f->rank >= 3 ? 1.0f : 0.25f;
		col[3] = 0.95f * alpha;
		for ( k = 0 ; k < f->rank ; k++ ) {
			CG_FillRect( px + k * ( pw + 1.2f * s ), py, pw, ph, col );
		}
	}
	col[0] = col[1] = col[2] = 1; col[3] = alpha;
	if ( f->arch > 0 ) {
		// en-types: "SNIPER - ANGEL", the type in its colour
		vec4_t	tc;
		float	cw = nameW * s, tw, nw, maxW = port * s * 2.6f;
		int		chars = strlen( BG_DS_TypeName( f->arch ) ) + 3 + strlen( f->name );

		if ( chars * cw > maxW ) {
			cw = maxW / chars;		// fit the slot (no running into the next name)
		}
		tw = ( strlen( BG_DS_TypeName( f->arch ) ) + 3 ) * cw;
		nw = strlen( f->name ) * cw;
		CG_DS_TypeColor( f->arch, tc );
		tc[3] = alpha;
		DSI_Text( cx - ( tw + nw ) * 0.5f, y + port * s + 2 * s, va( "%s - ", BG_DS_TypeName( f->arch ) ), tc, cw, ( small ? 6.5f : 7 ) * s, -1 );
		DSI_Text( cx - ( tw + nw ) * 0.5f + tw, y + port * s + 2 * s, f->name, col, cw, ( small ? 6.5f : 7 ) * s, -1 );
	} else {
		DSI_Text( cx, y + port * s + 2 * s, f->name, col, nameW * s, ( small ? 6.5f : 7 ) * s, 0 );
	}
	if ( rc ) {
		DSI_Col( col, rc, 0.95f * alpha );
	} else {
		col[0] = col[1] = col[2] = 0.72f; col[3] = 0.9f * alpha;
	}
	col[3] *= nk;
	if ( f->affix && col[3] > 0.01f ) {
		// en-affix: an affixed elite's note is its affixes ("FROST VAMPIRIC"), in the first one's colour,
		// squeezed to the slot (the orange frame and the header already say ELITE)
		vec4_t		ac;
		const char	*an = CG_DSA_Names( f->affix );
		float		acw = 3.6f, len = (float)strlen( an );

		if ( len * acw > DSI_SLOT_W * 0.95f ) {
			acw = DSI_SLOT_W * 0.95f / len;
		}
		CG_DSA_Color( f->affix, ac );
		ac[3] = col[3];
		DSI_Text( cx, y + port * s + 10 * s, an, ac, acw * s, 5.5f * s, 0 );
	} else if ( col[3] > 0.01f ) {
		DSI_Text( cx, y + port * s + 10 * s, DSI_Note( f ), col, 3.6f * s, 5.5f * s, 0 );
	}
}

// ds-arrivals: a slot still empty (that opponent joins later): a dim frame with a "?"
static void DSI_DrawSlot( float cx, float y, float port, float s, float alpha, const float *rc ) {
	vec4_t	col;
	float	p = port * s;

	col[0] = col[1] = col[2] = 0; col[3] = 0.35f * alpha;
	CG_FillRect( cx - p * 0.5f, y, p, p, col );
	if ( rc ) {
		DSI_Col( col, rc, 0.45f * alpha );
	} else {
		col[0] = col[1] = col[2] = 0.45f; col[3] = 0.45f * alpha;
	}
	DSI_Frame( cx - p * 0.5f, y, p, p, 0.6f * s, col );
	col[3] = 0.6f * alpha;
	DSI_Text( cx, y + p * 0.5f - p * 0.22f, "?", col, p * 0.3f, p * 0.44f, 0 );
}

// ds-arrivals: is this opponent still to come (no warning yet)?
static qboolean DSI_Pending( const dsiFoe_t *f ) {
	return dsi.stag && f->late && !f->arrWarn;
}

// when an opponent's portrait starts sliding in (cg.time); start + d for the ones there at the start
static int DSI_InAt( const dsiFoe_t *f, int d ) {
	return dsi.stag && f->late ? f->arrWarn : dsi.start + d;
}

static void DSI_DrawRow( float cx, float y0, float s, int t, float out, float hdr ) {
	vec4_t		col, ncol, bg;
	char		title[64], goal[96], strong[40], weak[40];
	float		rowW, x, y, alpha, a, k, left, right, bias, plateH, slideX, bw;
	int			i, n, d, at, slots[DSI_MAX], ns;
	dsiFoe_t	*f;
	qboolean	anim = t >= 0;

	alpha = 1.0f - out;
	if ( alpha <= 0 ) {
		return;
	}
	y0 -= 70 * s * DSI_Ease( out );
	bias = cgs.screenXScale > 0 ? cgs.screenXBias / cgs.screenXScale : 0;
	left = -bias - 10;
	right = SCREEN_WIDTH + bias + 10;
	rowW = DSI_RowWidth() * s;
	x = cx - rowW * 0.5f;
	y = y0 + 12 * s * hdr;
	plateH = 12 * hdr + ( dsi.boss >= 0 ? DSI_BOSS_PORT + 6 : DSI_PORT + 22 ) + 4;

	// the slot order: ds-arrivals fills the strip left to right in arrival order
	ns = 0;
	if ( anim && dsi.stag ) {
		for ( i = 0 ; i < dsi.numOrder ; i++ ) {
			for ( n = 0 ; n < dsi.num ; n++ ) {
				if ( dsi.foe[n].plan == dsi.order[i] && n != dsi.boss ) {
					slots[ns++] = n;
				}
			}
		}
	}
	if ( ns == 0 ) {
		for ( i = 0 ; i < dsi.num ; i++ ) {
			if ( i != dsi.boss ) {
				slots[ns++] = i;
			}
		}
	}

	// the plate tight to the row, a hairline in the node colour
	DSI_Header( title, goal, sizeof( title ), ncol );
	k = anim ? DSI_Ease( t / 300.0f ) : 1.0f;
	bg[0] = bg[1] = bg[2] = 0; bg[3] = 0.40f * alpha * k;
	CG_FillRect( cx - ( rowW * 0.5f + 10 * s ) * k, y0 - 3 * s, ( rowW + 20 * s ) * k, plateH * s, bg );
	ncol[3] = 0.85f * alpha * k;
	CG_FillRect( cx - ( rowW * 0.5f + 10 * s ) * k, y0 - 3 * s + plateH * s - 1, ( rowW + 20 * s ) * k, 1, ncol );

	if ( hdr > 0.01f ) {
		a = alpha * hdr * ( anim ? CG_Clamp01( t / 250.0f ) : 1.0f );
		ncol[3] = a;
		DSI_Text( cx - 4 * s, y0, title, ncol, 6 * s, 9 * s, 1 );
		col[0] = col[1] = col[2] = 0.88f; col[3] = 0.9f * a;
		DSI_Text( cx + 4 * s, y0 + 2 * s, goal, col, 4.5f * s, 7 * s, -1 );
	}

	// YOU from the left edge
	slideX = anim ? ( left - x ) * ( 1.0f - DSI_Ease( t / (float)DSI_SLIDE ) ) : 0;
	{
		dsiFoe_t	you;
		float		py = y + ( dsi.boss >= 0 ? 2 * s : 0 );		// (its name and lives stay inside the plate)

		memset( &you, 0, sizeof( you ) );
		Q_strncpyz( you.name, "YOU", sizeof( you.name ) );
		you.icon = dsi.youIcon;
		you.role = 1;
		a = alpha * ( anim ? CG_Clamp01( t / 200.0f ) : 1.0f );
		DSI_DrawFoe( &you, x + slideX + DSI_YOU_W * 0.5f * s, py, DSI_PORT, s, a, qfalse );
	}
	x += DSI_YOU_W * s;

	// VS pops in
	a = alpha * ( anim ? CG_Clamp01( ( t - 280 ) / 200.0f ) : 1.0f );
	if ( a > 0 ) {
		float	pop = anim ? 1.0f + 0.6f * ( 1.0f - DSI_Ease( ( t - 280 ) / 260.0f ) ) : 1.0f;
		float	vy = y + ( ( dsi.boss >= 0 ? DSI_BOSS_PORT : DSI_PORT ) * 0.5f - 6 * pop ) * s;

		col[0] = 1.0f; col[1] = 0.45f; col[2] = 0.12f; col[3] = a;
		DSI_Text( x + DSI_VS_W * 0.5f * s, vy, "VS", col, 9 * pop * s, 12 * pop * s, 0 );
	}
	x += DSI_VS_W * s;

	// the boss block
	if ( dsi.boss >= 0 && dsi.bossType >= 0 ) {
		f = &dsi.foe[dsi.boss];
		bw = DSI_BossBlockW() * s;
		at = DSI_InAt( f, 150 );
		k = anim ? DSI_Ease( ( cg.time - at ) / (float)DSI_SLIDE ) : 1.0f;
		a = alpha * ( anim ? CG_Clamp01( ( cg.time - at ) / 220.0f ) : 1.0f );
		if ( anim && DSI_Pending( f ) ) {
			// ds-arrivals: the boss is still to come: a big dim block
			col[0] = 0.06f; col[1] = 0; col[2] = 0.08f; col[3] = 0.45f * alpha;
			CG_FillRect( x, y, DSI_BOSS_PORT * s, DSI_BOSS_PORT * s, col );
			DSI_Col( col, dsiColBoss, ( 0.35f + 0.15f * sin( cg.time * 0.004f ) ) * alpha );
			DSI_Frame( x, y, DSI_BOSS_PORT * s, DSI_BOSS_PORT * s, 1.0f * s, col );
			DSI_Text( x + DSI_BOSS_PORT * 0.5f * s, y + DSI_BOSS_PORT * 0.22f * s, "?", col, 14 * s, 22 * s, 0 );
			col[0] = col[1] = col[2] = 0.6f; col[3] = 0.7f * alpha;
			DSI_Text( x + ( DSI_BOSS_PORT + 8 ) * s, y + 12 * s, "THE BOSS IS COMING", col, 5 * s, 7 * s, -1 );
		} else if ( a > 0 ) {
			float	bx = x + ( right - x ) * ( 1.0f - k ), fl = 0, p;
			vec4_t	fr;

			if ( f->flash && cg.time >= f->flash && cg.time - f->flash < 700 ) {
				fl = 1.0f - ( cg.time - f->flash ) / 700.0f;
			}
			if ( f->late && f->arrWarn && cg.time < f->arrLand && fl < 0.5f ) {
				fl = 0.3f + 0.3f * sin( cg.time * 0.02f );		// on its way
			}
			p = DSI_BOSS_PORT * s * ( 1.0f + 0.10f * fl );
			col[0] = 0.06f; col[1] = 0; col[2] = 0.08f; col[3] = 0.7f * a;
			CG_FillRect( bx + ( DSI_BOSS_PORT * s - p ) * 0.5f, y + ( DSI_BOSS_PORT * s - p ) * 0.5f, p, p, col );
			col[0] = col[1] = col[2] = 0.8f + 0.2f * fl; col[3] = a;
			trap_R_SetColor( col );
			if ( f->icon ) {
				CG_DrawPic( bx + ( DSI_BOSS_PORT * s - p ) * 0.5f, y + ( DSI_BOSS_PORT * s - p ) * 0.5f, p, p, f->icon );
			}
			trap_R_SetColor( NULL );
			DSI_Col( fr, dsiColBoss, a );
			fr[0] += ( 1 - fr[0] ) * fl; fr[1] += ( 1 - fr[1] ) * fl; fr[2] += ( 1 - fr[2] ) * fl;
			DSI_Frame( bx + ( DSI_BOSS_PORT * s - p ) * 0.5f, y + ( DSI_BOSS_PORT * s - p ) * 0.5f, p, p, 1.6f * s, fr );
			// name, title, sides
			col[0] = 1; col[1] = 0.93f; col[2] = 0.86f; col[3] = a;
			DSI_Text( bx + ( DSI_BOSS_PORT + 8 ) * s, y + 1 * s, ds_bosses[dsi.bossType].name, col, 9 * s, 13 * s, -1 );
			col[0] = col[1] = col[2] = 0.75f; col[3] = 0.9f * a;
			DSI_Text( bx + ( DSI_BOSS_PORT + 8 ) * s, y + 16 * s, dsi.abyss > 0 ? va( "%s  -  ABYSS %i", ds_bosses[dsi.bossType].title, dsi.abyss )
				: ds_bosses[dsi.bossType].title, col, 5 * s, 7 * s, -1 );
			DSI_BossSides( strong, weak, sizeof( strong ) );
			DSI_Col( col, dsiColBoss, a );
			col[0] = 0.55f + 0.45f * col[0]; col[1] = 0.45f + 0.45f * col[1]; col[2] = 0.45f + 0.45f * col[2];
			DSI_Text( bx + ( DSI_BOSS_PORT + 8 ) * s, y + 26 * s, strong, col, 5 * s, 7 * s, -1 );
			if ( weak[0] ) {
				col[0] = col[1] = col[2] = 0.55f; col[3] = 0.85f * a;
				DSI_Text( bx + ( DSI_BOSS_PORT + 8 + ( CG_DrawStrlen( strong ) + 2 ) * 5 ) * s, y + 26 * s, weak, col, 5 * s, 7 * s, -1 );
			}
		}
		x += bw;
		// the two rivals, smaller
		for ( n = 0 ; n < ns ; n++ ) {
			f = &dsi.foe[slots[n]];
			d = 420 + n * 110;
			at = DSI_InAt( f, d );
			k = anim ? DSI_Ease( ( cg.time - at ) / (float)DSI_SLIDE ) : 1.0f;
			a = alpha * ( anim ? CG_Clamp01( ( cg.time - at ) / 220.0f ) : 1.0f );
			if ( anim && DSI_Pending( f ) ) {
				DSI_DrawSlot( x + DSI_RIVAL_W * 0.5f * s, y + 3 * s, DSI_RIVAL_PORT, s, alpha, NULL );
			} else if ( a > 0 ) {
				float	fx = x + DSI_RIVAL_W * 0.5f * s;

				DSI_DrawFoe( f, fx + ( right - fx ) * ( 1.0f - k ), y + 3 * s, DSI_RIVAL_PORT, s, a, qtrue );
			}
			x += DSI_RIVAL_W * s;
		}
		return;
	}

	// the opponents from the right edge, one after another
	for ( n = 0 ; n < ns ; n++ ) {
		float	fx = x + DSI_SLOT_W * 0.5f * s;
		int		port;

		f = &dsi.foe[slots[n]];
		port = f->role == DSR_ELITE || ( f->flags & DSI_NEMESIS ) ? DSI_PORT + 2 : DSI_PORT;
		d = 160 + n * 95;
		at = DSI_InAt( f, d );
		k = anim ? DSI_Ease( ( cg.time - at ) / (float)DSI_SLIDE ) : 1.0f;
		a = alpha * ( anim ? CG_Clamp01( ( cg.time - at ) / 220.0f ) : 1.0f );
		if ( anim && DSI_Pending( f ) ) {
			DSI_DrawSlot( fx, y, DSI_PORT, s, alpha, NULL );
		} else if ( a > 0 ) {
			DSI_DrawFoe( f, fx + ( right - fx ) * ( 1.0f - k ), y, port, s, a, qfalse );
		}
		x += DSI_SLOT_W * s;
	}
}

// the row scale that fits the 16:9 area (or the screen, when narrower)
static float DSI_FitScale( float want, float margin ) {
	float	bias = cgs.screenXScale > 0 ? cgs.screenXBias / cgs.screenXScale : 0;
	float	wide = ( SCREEN_HEIGHT * 16.0f / 9.0f - SCREEN_WIDTH ) * 0.5f;
	float	avail, w = DSI_RowWidth() + 20;

	if ( bias > wide ) {
		bias = wide;
	}
	avail = SCREEN_WIDTH + 2 * bias - 2 * margin;
	if ( w * want > avail ) {
		want = avail / w;
	}
	return want;
}

/*
===============================================================================

LOADING SCREEN

===============================================================================
*/

/*
=================
CG_DSI_DrawLoading

From CG_DrawInformation. qtrue: a DESCENT floor with opponents was drawn (the stock game type,
frag limit and loading icons stay away).
=================
*/
qboolean CG_DSI_DrawLoading( void ) {
	vec4_t		col, ncol;
	char		title[64], goal[96];
	const char	*sub;
	float		s, y;
	int			act;

	DSI_ParseRun();
	if ( !dsi.ds ) {
		return qfalse;
	}
	DSI_ParseRoster();
	DSI_ParseYou();
	dsi.skirmish = qfalse;
	act = dsi.floor > 0 ? DS_ActForFloor( dsi.floor ) : 0;

	// the act and the floor, the goal
	y = 236;
	col[0] = 0; col[1] = 0; col[2] = 0; col[3] = 0.55f;
	CG_FillRect( -1000, y - 8, SCREEN_WIDTH + 2000, 236, col );
	if ( act >= 1 && act <= DS_ACT_ABYSS ) {
		sub = act == DS_ACT_ABYSS ? va( "THE ABYSS  -  %s", ds_acts[act].name ) : va( "ACT %s  -  %s", ds_roman[act], ds_acts[act].name );
		col[0] = 1.0f; col[1] = 0.55f; col[2] = 0.15f; col[3] = 0.95f;
		DSI_Text( 320, y, sub, col, 7, 10, 0 );
		y += 14;
	}
	DSI_Header( title, goal, sizeof( title ), ncol );
	DSI_Text( 320, y, title, ncol, 12, 17, 0 );
	y += 21;
	col[0] = col[1] = col[2] = 0.9f; col[3] = 1;
	DSI_Text( 320, y, goal, col, 6, 9, 0 );
	y += 18;
	if ( dsi.nodeType == 4 ) {
		col[0] = 1.0f; col[1] = 0.82f; col[2] = 0.3f; col[3] = 1;
		DSI_Text( 320, y + 20, "A SECRET VAULT  -  NOBODY WAITS HERE", col, 7, 10, 0 );
		return qtrue;
	}
	if ( dsi.num <= 0 ) {
		return qtrue;
	}
	DSI_ParseArrive();
	col[0] = col[1] = col[2] = 0.6f; col[3] = 1;
	if ( dsi.stag ) {
		DSI_Text( 320, y, dsi.boss >= 0 ? "YOUR OPPONENTS  -  TWO RIVALS, THEN THE BOSS" : va( "YOUR OPPONENTS  -  %i, JOINING ONE BY ONE", dsi.num ),
			col, 5, 7, 0 );
	} else {
		DSI_Text( 320, y, dsi.boss >= 0 ? "YOUR OPPONENTS  -  THE BOSS AND TWO RIVALS" : va( "YOUR OPPONENTS  -  %i", dsi.num ), col, 5, 7, 0 );
	}
	y += 14;
	s = DSI_FitScale( 1.9f, 12 );
	DSI_DrawRow( 320, y, s, -1, 0, 0.0f );
	return qtrue;
}

/*
===============================================================================

THE VERSUS INTRO

===============================================================================
*/

static int	dsiAnnHold;		// another voice line (ds-surprise) speaks until then

// one voice line at cg.time "at"; len: how long it speaks (the shared announcer is busy till then)
static void DSI_Queue( int at, sfxHandle_t snd, int foe, int chan, int len ) {
	int		i;

	if ( !snd ) {
		return;
	}
	if ( dsi.qi >= dsi.nq ) {
		dsi.qi = dsi.nq = 0;
	}
	if ( dsi.nq >= DSI_MAXQ && dsi.qi > 0 ) {
		// drop the lines already played
		for ( i = dsi.qi ; i < dsi.nq ; i++ ) {
			dsi.qAt[i - dsi.qi] = dsi.qAt[i];
			dsi.qSnd[i - dsi.qi] = dsi.qSnd[i];
			dsi.qFoe[i - dsi.qi] = dsi.qFoe[i];
			dsi.qChan[i - dsi.qi] = dsi.qChan[i];
		}
		dsi.nq -= dsi.qi;
		dsi.qi = 0;
	}
	if ( dsi.nq >= DSI_MAXQ ) {
		return;
	}
	dsi.qAt[dsi.nq] = at;
	dsi.qSnd[dsi.nq] = snd;
	dsi.qFoe[dsi.nq] = foe;
	dsi.qChan[dsi.nq] = chan;
	dsi.nq++;
	if ( chan == CHAN_ANNOUNCER && at + len > dsiAnnHold ) {
		dsiAnnHold = at + len;
	}
}

// the announcer's script: the boss and its rivals, or the elite / nemesis first, then the names.
// ds-arrivals (staggered): only the ones there at the start; the rest get their own moment.
static void DSI_Script( qboolean voice ) {
	int		t = 260, i, n, pass, order[DSI_MAX], no = 0, t0 = dsi.start;
	qboolean	bossNow;

	dsi.nq = dsi.qi = 0;
	dsi.fightQueued = qfalse;
	if ( dsi.boss >= 0 && dsi.bossType >= 0 ) {
		bossNow = !( dsi.stag && dsi.foe[dsi.boss].late );
		if ( bossNow ) {
			DSI_Queue( t0 + 150, dsi.sndRoar, -1, CHAN_LOCAL_SOUND, 0 );
			if ( voice ) {
				DSI_Queue( t0 + t, dsi.sndBoss[dsi.bossType], dsi.boss, CHAN_ANNOUNCER, dsiBossGap[dsi.bossType] );
			}
			t += dsiBossGap[dsi.bossType];
		}
		if ( voice && dsi.num > 1 ) {
			DSI_Queue( t0 + t, dsi.sndRivals, -1, CHAN_ANNOUNCER, DSI_GAP_RIVALS );
			t += DSI_GAP_RIVALS;
		}
		for ( i = 0 ; i < dsi.num ; i++ ) {
			if ( i != dsi.boss && dsi.foe[i].voice && voice && !( dsi.stag && dsi.foe[i].late ) ) {
				DSI_Queue( t0 + t, dsi.foe[i].voice, i, CHAN_ANNOUNCER, DSI_GAP_NAME );
				t += DSI_GAP_NAME;
			}
		}
		dsi.end = t + 800 > 3600 ? t + 800 : 3600;
	} else {
		// the elite and the nemesis first, then the rest; at most 4 names
		for ( pass = 0 ; pass < 3 ; pass++ ) {
			for ( i = 0 ; i < dsi.num ; i++ ) {
				int		special = dsi.foe[i].role == DSR_ELITE ? 0 : ( dsi.foe[i].flags & DSI_NEMESIS ) ? 1 : 2;

				if ( special == pass && !( dsi.stag && dsi.foe[i].late ) ) {
					order[no++] = i;
				}
			}
		}
		for ( n = 0, i = 0 ; i < no && n < 4 && t <= DSI_VOICE_UNTIL && voice ; i++ ) {
			dsiFoe_t	*f = &dsi.foe[order[i]];

			if ( !f->voice ) {
				continue;
			}
			if ( !dsi.skirmish && f->role == DSR_ELITE ) {
				DSI_Queue( t0 + t, dsi.sndElite, -1, CHAN_ANNOUNCER, DSI_GAP_ELITE );
				t += DSI_GAP_ELITE;
			} else if ( !dsi.skirmish && ( f->flags & DSI_NEMESIS ) ) {
				DSI_Queue( t0 + t, dsi.sndNemesis, -1, CHAN_ANNOUNCER, DSI_GAP_NEMESIS );
				t += DSI_GAP_NEMESIS;
			}
			DSI_Queue( t0 + t, f->voice, order[i], CHAN_ANNOUNCER, DSI_GAP_NAME );
			t += DSI_GAP_NAME;
			n++;
		}
		dsi.end = t + 750 > 3300 ? t + 750 : 3300;
	}
	if ( voice ) {
		DSI_Queue( t0 + t, cgs.media.countFightSound, -1, CHAN_ANNOUNCER, 700 );
		dsi.fightQueued = qtrue;
	}
	// names nobody reads out still flash in turn as they arrive
	for ( i = 0 ; i < dsi.num ; i++ ) {
		dsi.foe[i].flash = 0;
	}
}

/*
=================
DSI_Announce

ds-arrivals: a late joiner's own moment, as soon as the announcer is free: its tag ("Elite!",
"Your nemesis!", "Rival!"), then its name alone. The boss: a roar, its name, and the strip
grows to the full versus row for a few seconds (its big introduction).
=================
*/
static void DSI_Announce( dsiFoe_t *f ) {
	int		at = CG_DSI_AnnouncerFreeAt(), idx = f - dsi.foe;
	const char	*tag = "";

	if ( at < cg.time + 120 ) {
		at = cg.time + 120;		// (the swoosh first)
	}
	trap_S_StartLocalSound( dsi.sndSwoosh, CHAN_LOCAL_SOUND );
	if ( f->role == DSR_BOSS && dsi.bossType >= 0 ) {
		trap_S_StartLocalSound( dsi.sndRoar, CHAN_LOCAL_SOUND );
		if ( at < cg.time + 450 ) {
			at = cg.time + 450;
		}
		DSI_Queue( at, dsi.sndBoss[dsi.bossType], idx, CHAN_ANNOUNCER, dsiBossGap[dsi.bossType] );
		dsi.bigUntil = f->arrLand + 2600;
		tag = "boss";
	} else {
		if ( f->role == DSR_ELITE ) {
			DSI_Queue( at, dsi.sndElite, -1, CHAN_ANNOUNCER, DSI_GAP_ELITE );
			at += DSI_GAP_ELITE;
			tag = "elite";
		} else if ( f->flags & DSI_NEMESIS ) {
			DSI_Queue( at, dsi.sndNemesis, -1, CHAN_ANNOUNCER, DSI_GAP_NEMESIS );
			at += DSI_GAP_NEMESIS;
			tag = "nemesis";
		} else if ( ( f->flags & DSI_RIVAL ) && dsi.sndRival ) {
			DSI_Queue( at, dsi.sndRival, -1, CHAN_ANNOUNCER, DSI_GAP_ELITE );
			at += DSI_GAP_ELITE;
			tag = "rival";
		}
		DSI_Queue( at, f->voice, idx, CHAN_ANNOUNCER, DSI_GAP_NAME );
	}
	CG_DevPrintf( "DSArrive: announce %s%s%s, the name at +%i ms\n", f->name, tag[0] ? " tag " : "", tag, at - cg.time );
}

static void DSI_Start( qboolean skirmish, qboolean voice ) {
	dsi.state = 1;
	dsi.skirmish = skirmish;
	dsi.start = cg.time;
	dsi.skipped = 0;
	dsi.stripOut = dsi.stripEnd = 0;
	DSI_ParseYou();
	if ( skirmish ) {
		dsi.stag = qfalse;
	}
	DSI_Script( voice );
	trap_S_StartLocalSound( dsi.sndSwoosh, CHAN_LOCAL_SOUND );
	CG_DevPrintf( "DSIntro: start %s, %i opponents, boss %i, %i voice lines, %i ms%s\n", skirmish ? "skirmish" : "descent",
		dsi.num, dsi.boss, dsi.nq, dsi.end, dsi.stag ? va( ", staggered (%i at the start)", dsi.ns ) : "" );
}

// skirmish: the bots in the game (from their configstrings: the model is the real one even
// while the cgame defers loading it)
static int DSI_SkirmishRoster( qboolean fill ) {
	int				i, n = 0;
	const char		*info;
	clientInfo_t	*ci;
	dsiFoe_t		*f;
	char			file[32];

	for ( i = 0 ; i < MAX_CLIENTS && n < DSI_MAX ; i++ ) {
		ci = &cgs.clientinfo[i];
		if ( i == cg.clientNum || !ci->infoValid || ci->botSkill <= 0 || ci->team == TEAM_SPECTATOR ) {
			continue;
		}
		if ( fill ) {
			info = CG_ConfigString( CS_PLAYERS + i );
			f = &dsi.foe[n];
			memset( f, 0, sizeof( *f ) );
			f->role = DSR_BOT;
			f->skill = ci->botSkill;
			Q_strncpyz( f->model, Info_ValueForKey( info, "model" ), sizeof( f->model ) );
			CG_CleanUpper( ci->name, f->name, sizeof( f->name ) );
			f->icon = DSI_Icon( f->model );
			Q_strncpyz( file, ci->name, sizeof( file ) );
			Q_CleanStr( file );
			f->voice = trap_S_RegisterSound( va( "sound/player/announce/%s.wav", file ), qtrue );
		}
		n++;
	}
	if ( fill ) {
		dsi.num = n;
		dsi.boss = -1;
		dsi.cs[0] = 1;		// (not a CS_DSINTRO copy any more: parse it again if one comes)
		dsi.cs[1] = 0;
	}
	return n;
}

// should the intro start now? (once per floor / match)
static void DSI_Decide( void ) {
	int		since;

	if ( dsi.state != 0 || !cg.snap || cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR
		|| cg.snap->ps.pm_type == PM_INTERMISSION ) {
		return;
	}
	since = cg.snap->serverTime - cgs.levelStartTime;
	if ( dsi.ds ) {
		if ( dsi.phase != 0 ) {
			return;
		}
		if ( dsi.cv.integer <= 0 || dsi.nodeType == 4 || dsi.num <= 0 || since > 30000 ) {
			dsi.state = 2;		// off / the vault / joined late: the old floor strip
			return;
		}
		DSI_Start( qfalse, qtrue );
		return;
	}
	// skirmish: FFA, tournament, SP arenas, at the start of the match once the bots are in
	if ( dsi.cv.integer <= 0 || cgs.gametype >= GT_TEAM || since > 25000 ) {
		dsi.state = 2;
		return;
	}
	if ( cg.warmup ) {
		return;
	}
	{
		int		n = DSI_SkirmishRoster( qfalse );

		if ( n != dsi.skCount ) {
			dsi.skCount = n;
			dsi.skSince = cg.time;
			return;
		}
		if ( n <= 0 || cg.time - dsi.skSince < 1200 ) {
			return;
		}
	}
	DSI_SkirmishRoster( qtrue );
	// SP arenas read the names as the bots spawn and the tournament counts down itself
	DSI_Start( qtrue, cgs.gametype == GT_FFA );
}

static void DSI_ReadSkip( void ) {
	usercmd_t	cmd;
	int			n = trap_GetCurrentCmdNumber(), pressed;

	if ( n == dsi.lastCmd || !trap_GetUserCmd( n, &cmd ) ) {
		return;
	}
	dsi.lastCmd = n;
	pressed = cmd.buttons & ~dsi.lastButtons;
	dsi.lastButtons = cmd.buttons;
	if ( dsi.state != 1 || dsi.skipped || cg.time - dsi.start < 200 ) {
		return;
	}
	if ( pressed & BUTTON_ATTACK ) {
		dsi.skipped = cg.time;
		dsi.end = cg.time - dsi.start + DSI_SKIP_OUT;
		// the rest of the read-out goes; "Fight!" right away if it was still to come
		if ( dsi.fightQueued && dsi.qi < dsi.nq ) {
			trap_S_StartLocalSound( cgs.media.countFightSound, CHAN_ANNOUNCER );
		}
		dsi.qi = dsi.nq;
		dsiAnnHold = cg.time + 600;
		CG_DevPrintf( "DSIntro: skipped at %i ms\n", cg.time - dsi.start );
	}
}

/*
=================
CG_DSI_Busy / CG_DSI_Covers

Busy: the intro runs (the act card and the boss bar wait). Covers: it ran this floor (the
floor strip, whose information it carries, stays away).
=================
*/
qboolean CG_DSI_Busy( void ) {
	DSI_Decide();
	return dsi.state == 1;
}

qboolean CG_DSI_Covers( void ) {
	DSI_Decide();
	return dsi.state == 1 || dsi.state == 3 || ( dsi.state == 2 && dsi.start > 0 );
}

// ds-arrivals: everyone is in (late joiners landed; the server gave up on any that never came)
static qboolean DSI_AllIn( void ) {
	int		i;
	dsiFoe_t	*f;

	for ( i = 0 ; i < dsi.num ; i++ ) {
		f = &dsi.foe[i];
		if ( f->late && ( !f->arrLand || cg.time < f->arrLand ) ) {
			return qfalse;
		}
	}
	return qtrue;
}

// the voice queue: lines whose time came; a late joiner landing flashes its portrait
static void DSI_RunQueue( void ) {
	int		i, f;

	while ( dsi.qi < dsi.nq && cg.time >= dsi.qAt[dsi.qi] ) {
		trap_S_StartLocalSound( dsi.qSnd[dsi.qi], dsi.qChan[dsi.qi] );
		f = dsi.qFoe[dsi.qi];
		if ( f >= 0 && f < dsi.num ) {
			dsi.foe[f].flash = cg.time;
			CG_DevPrintf( "DSVoice: read %s at %i (+%i ms into the floor)\n", dsi.foe[f].name, cg.time, cg.time - cgs.levelStartTime );
		}
		dsi.qi++;
	}
	for ( i = 0 ; i < dsi.num ; i++ ) {
		if ( dsi.foe[i].late && dsi.foe[i].arrLand && !dsi.foe[i].landed && cg.time >= dsi.foe[i].arrLand ) {
			dsi.foe[i].landed = qtrue;
			dsi.foe[i].flash = cg.time;
		}
	}
}

/*
=================
CG_DSI_Draw2D

From CG_Draw2D, before the descent HUD (it decides first, the others ask Busy / Covers).
State 1 the versus intro; state 3 (ds-arrivals) the slim strip that fills up as the late
joiners arrive and collapses once everyone is in.
=================
*/
void CG_DSI_Draw2D( void ) {
	int		t;
	float	out, s, sBig, sSlim, target;

	trap_Cvar_Update( &dsi.cv );
	DSI_ParseRun();
	if ( dsi.ds ) {
		DSI_ParseRoster();
		DSI_ParseArrive();
	}
	DSI_Decide();
	DSI_ReadSkip();
	DSI_RunQueue();
	if ( dsi.state != 1 && dsi.state != 3 ) {
		return;
	}
	t = cg.time - dsi.start;
	sBig = DSI_FitScale( 1.0f, 8 );
	sSlim = DSI_FitScale( 0.62f, 8 );
	if ( dsi.state == 1 ) {
		if ( t >= dsi.end ) {
			if ( dsi.stag && !dsi.skirmish && ( !DSI_AllIn() || cg.time < dsi.lastLand + 2500 ) ) {
				dsi.state = 3;		// ds-arrivals: on to the slim strip
				dsi.curS = sBig;
				dsi.hdr = 1.0f;
				CG_DevPrintf( "DSArrive: strip open at %i ms\n", t );
			} else {
				dsi.state = 2;
				CG_DevPrintf( "DSIntro: over at %i ms (cg.time %i)\n", t, cg.time );
				return;
			}
		}
	}
	if ( dsi.state == 3 ) {
		// everyone in (and the last one's moment over): collapse
		if ( !dsi.stripOut && ( ( DSI_AllIn() && cg.time >= dsi.lastLand + 2500 && cg.time >= dsi.bigUntil ) || t > 120000 ) ) {
			dsi.stripOut = cg.time;
		}
		if ( dsi.stripOut && cg.time - dsi.stripOut >= DSI_OUT ) {
			dsi.state = 2;
			dsi.stripEnd = cg.time;
			CG_DevPrintf( "DSArrive: strip closed at %i ms, everyone is in\n", t );
			return;
		}
	}
	if ( cg.showScores || !cg.snap || cg.snap->ps.pm_type == PM_INTERMISSION ) {
		return;
	}
	if ( dsi.state == 1 ) {
		if ( dsi.skipped ) {
			out = CG_Clamp01( ( cg.time - dsi.skipped ) / (float)DSI_SKIP_OUT );
		} else {
			out = CG_Clamp01( ( t - ( dsi.end - DSI_OUT ) ) / (float)DSI_OUT );
		}
		if ( dsi.stag && !dsi.skirmish ) {
			out = 0;		// the strip carries on (state 3), also after a skip
		}
		s = sBig;
		dsi.hdr = 1.0f;
		DSI_DrawRow( 320, 5, s, t, out, 1.0f );
		return;
	}
	// state 3: the strip; the boss's big introduction grows it back to the full row
	target = cg.time < dsi.bigUntil ? sBig : sSlim;
	dsi.curS += ( target - dsi.curS ) * CG_Clamp01( cg.frametime * 0.007f );
	dsi.hdr = sBig > sSlim ? CG_Clamp01( ( dsi.curS - sSlim ) / ( sBig - sSlim ) ) : 0;
	out = dsi.stripOut ? CG_Clamp01( ( cg.time - dsi.stripOut ) / (float)DSI_OUT ) : 0;
	DSI_DrawRow( 320, 5, dsi.curS, t, out, dsi.hdr );
}

/*
=================
CG_DSI_Command

dev: dsintro = replay the intro now (dsintro skirmish = from the bots in the game)
=================
*/
qboolean CG_DSI_ShowLoading( void ) {
	return dsi.loadUntil > cg.time && dsi.loadUntil - cg.time < 60000;
}

void CG_DSI_Command_f( void ) {
	if ( !Q_stricmp( CG_Argv( 1 ), "loading" ) ) {
		dsi.loadUntil = cg.time + ( atoi( CG_Argv( 2 ) ) > 0 ? atoi( CG_Argv( 2 ) ) : 5000 );
		return;
	}
	if ( !Q_stricmp( CG_Argv( 1 ), "skirmish" ) || !dsi.ds ) {
		DSI_SkirmishRoster( qtrue );
		DSI_Start( qtrue, qtrue );
	} else {
		dsi.cs[0] = 0;
		DSI_ParseRoster();
		DSI_ParseArrive();
		DSI_Start( qfalse, qtrue );
	}
}

/*
=================
CG_DSI_AnnouncerFreeAt / CG_DSI_AnnouncerHold

ds-arrivals: one announcer for the intro, the arrivals and ds-surprise's callouts. FreeAt =
the cg.time the last queued or held line ends; Hold = another line speaks until then.
=================
*/
int CG_DSI_AnnouncerFreeAt( void ) {
	return dsiAnnHold > cg.time ? dsiAnnHold : cg.time;
}

void CG_DSI_AnnouncerHold( int untilTime ) {
	if ( untilTime > dsiAnnHold && untilTime - cg.time < 10000 ) {
		dsiAnnHold = untilTime;
	}
}

/*
=================
CG_DSI_BossBarTime

ds-arrivals: cg_ds_acts.c's boss bar clock t (msec since it may show). -1 while a staggered
boss is still to come or its big introduction / the strip is still up; then it slides in.
=================
*/
int CG_DSI_BossBarTime( int t ) {
	dsiFoe_t	*b;
	int			from, tb;

	if ( !dsi.stag || dsi.boss < 0 || dsi.boss >= dsi.num ) {
		return t;
	}
	b = &dsi.foe[dsi.boss];
	if ( !b->late ) {
		return t;
	}
	if ( !b->arrLand || cg.time < b->arrLand || dsi.state == 3 || dsi.state == 1 ) {
		return -1;
	}
	from = dsi.stripEnd > b->arrLand ? dsi.stripEnd : b->arrLand;
	tb = cg.time - from;
	return tb < t ? tb : t;
}
