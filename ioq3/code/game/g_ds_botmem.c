/*
===========================================================================
REARMED -- round 4 BOT MEMORY + NEMESIS HALL OF FAME (en-memory), DESCENT.

Named bots remember the player across runs. A lifetime record per bot name lives in
rearmed_bots.txt (format: game/bg_ds_botmem.h): kills on the player and deaths to the player,
floors and runs met, the last meeting (date, floor result, that floor's score), how often it was
the nemesis and its peak level, the player's headshot / revenge kills on it, its revenge kills.

What the record changes (g_botMemory 1; 2 = the lines only; 0 = off, the record still grows):
- GREETINGS escalate with the history, by tier (BMT_*): stranger, familiar, rival, arch-rival,
  old nemesis. They quote the real numbers ("You again, Lokki. 12 to 9, me.", "Our 30th dance.")
  and always use the player's in-game name. One or two a floor (three with g_botTaunts 2), the
  most significant bots first, through the taunt limiter (G_PR_SayTo: g_botTaunts 0 = silent).
  The best rival also gets a short right-edge callout (prc PRC_RIVAL, cgame/cg_personality.c).
- TAUNTS: a bot with history that kills you (or dies to you) may quote the new score instead
  of a generic line (G_BM_KillLine / G_BM_DeathLine, from G_PR_Death).
- LIFETIME GRUDGE (BM_GRUDGE: it is losing to you, 4+ behind): it holds a grudge on you for its
  first 60 s on a floor and 30 s more every 2 minutes (en-personality's grudge, en-brains hunts:
  respects g_botGrudges).
- FEAR (BM_FEAR: you dominate it, 8+ kills and 3x its own): retreats to heal a little earlier and
  focuses you a little less (G_BM_Fear, ai_tactics.c), and now and then a panic line when you
  come close. Enemy-side only: nothing on the player's side changes.

NEMESIS HALL OF FAME: every nemesis of a run (g_ds_nemesis.c calls G_BM_NemesisRise / Update /
Slain): name, title, peak level, kills on the player, the run and floor where it rose, how it
ended (slain with which weapon, outlived the run, dethroned, the run ended). The UI's Records >
NEMESIS HALL (q3_ui/ui_ds_hall.c) lists it.

Writes: only at the floor end (G_BM_FloorEnd from RL_Finish), never mid-floor; nothing at all
on daily challenge floors (G_RL_Daily) or boss practice (G_RL_Practice), which also don't read it.

Dev (server console): dsbm (list), dsbm set <bot> <k> <d> <floors> <runs> [nem], dsbm say <bot>
(its greeting now, logged even if the limiter drops it), dsbm clear. Log lines "BotMem:".
===========================================================================
*/
#include "g_local.h"
#include "bg_ds_personality.h"
#include "bg_ds_botmem.h"

#define BM_LINE_LEN			100
#define BM_GREET_WINDOW		25000		// a bot is greeted within 25 s of its arrival (or of yours)
#define BM_GRUDGE_FIRST		60000
#define BM_GRUDGE_AGAIN		30000
#define BM_GRUDGE_EVERY		120000

typedef struct {
	char	name[24];
	int		k, d, fm, rm, lastRun, lastDate, lastRes, lastK, lastD, nem, nemPeak, hs, rv, brv;
	// this floor (not saved)
	int		fk, fd, seen;
} bmRec_t;

typedef struct {
	int		run, runNo, date;
	char	name[24];
	char	title[24];
	int		peak, kills, floorRose, status, floorEnd;
	char	how[24];
} bmHall_t;

typedef struct {
	qboolean	loaded;
	int			seed;
	bmRec_t		r[BM_MAX_BOTS];
	int			num;
	bmHall_t	h[BM_MAX_HALL];
	int			numHall;
	// this floor
	int			nextScan;
	int			humanInTime;
	int			enter[MAX_CLIENTS];		// pers.enterTime + 1 of the client last scanned
	int			rec[MAX_CLIENTS];		// record index of the client, -1 none
	int			arrive[MAX_CLIENTS];
	int			greeted[MAX_CLIENTS];
	int			grudgeNext[MAX_CLIENTS];
	int			panicNext[MAX_CLIENTS];
	int			panics[MAX_CLIENTS];
	int			nextGreet;
	int			greets, strangers, callouts;
	int			dirty;
	qboolean	floorDone;		// G_BM_FloorEnd ran: nothing more counts on this map
} bmState_t;

static bmState_t	bm;
static vmCvar_t		bmCv;
static char			bmBuf[16384];

/*
===============================================================================

LINES

{N} the player's name, {K} its kills on you, {D} your kills on it, {F} floors met, {G} this meeting's number, {M} the same
as an ordinal ("30th"), {R} runs met, {X} times it was your nemesis, {W} the weapon you
last slew it with as the nemesis, {S} the score said its way ("12 to 9, me.")

===============================================================================
*/

static const char *bmStranger[] = {
	"First time? Welcome to the pit.", "New face. Let's see what you've got, {N}.", "Fresh meat. Hi, {N}."
};
static const char *bmFamiliar[] = {
	"You again, {N}.", "Back for more, {N}? {S}", "We've met. {S}", "{N}. I remember you."
};
static const char *bmRival[] = {
	"You again, {N}. {S}", "{F} floors we've done this. {S}", "{R} runs, {N}, and I'm still here.", "Our {M} meeting. {S}"
};
static const char *bmArch[] = {
	"Our {M} dance, {N}.", "{F} floors. {R} runs. Still standing. {S}", "{S} Let's settle it, {N}.",
	"Of course it's you. Round {G}, {N}."
};
static const char *bmNemOnce[] = {
	"I was your nemesis once, {N}. I remember how.", "Remember me, {N}? I wore the title. {S}"
};
static const char *bmNemMany[] = {
	"Your nemesis {X} times, {N}. Count them.", "Remember me, {N}? {X} times I hunted you. {S}"
};
static const char *bmNemSlain[] = {
	"Last time you needed a {W}. Bring it, {N}.", "You put me down with a {W}. Not again."
};
static const char *bmFear[] = {
	"Not you again...", "{D} times you've dropped me, {N}. Not today. Please.", "Oh no. It's {N}."
};
static const char *bmGrudge[] = {
	"{D} to {K}, {N}. That changes today.", "I've been waiting for you, {N}.", "You owe me {D} deaths, {N}."
};
static const char *bmKill[] = {
	"{K} to {D} now, {N}.", "That's {K}, {N}. I'm counting.", "Our {M} meeting and you're still dropping."
};
static const char *bmKillFear[] = { "I got you? I got you!", "Ha! One back! {K} to {D}!" };
static const char *bmKillGrudge[] = { "One back. {K} to {D}.", "That's for the other {D}, {N}." };
static const char *bmKillNem[] = { "Just like old times, {N}.", "The old title still fits." };
static const char *bmDeath[] = { "{D} now. I'm keeping score, {N}.", "Lucky. Still {K} to {D}." };
static const char *bmDeathFear[] = { "Not again!", "Why always me?", "{D}... make it stop." };
static const char *bmPanic[] = {
	"Stay away from me!", "Not {N}! Anyone but {N}!", "Somebody else take {N}!", "I'm not dying to {N} again!"
};

#define BM_NUM( arr )		( (int)( sizeof( arr ) / sizeof( arr[0] ) ) )
#define BM_PICK( arr )		( arr[rand() % BM_NUM( arr )] )

/*
===============================================================================

HELPERS

===============================================================================
*/

void G_BM_RegisterCvars( void ) {
	trap_Cvar_Register( &bmCv, "g_botMemory", "1", CVAR_ARCHIVE );
}

// a floor whose memory counts: a real DESCENT run (no daily, no boss practice)
static qboolean BM_Allowed( void ) {
	return G_RL_Active() && !G_RL_NoSave();
}

static qboolean BM_Active( void ) {
	return bm.loaded && BM_Allowed();
}

static int BM_Mode( void ) {
	return bmCv.integer < 0 ? 0 : bmCv.integer > 2 ? 2 : bmCv.integer;
}

// a name safe for the file and the chat formatter: colours off, no '|' or '%'
static void BM_Clean( const char *in, char *out, int size ) {
	int		i;

	Q_strncpyz( out, in ? in : "", size );
	Q_CleanStr( out );
	for ( i = 0 ; out[i] ; i++ ) {
		if ( out[i] == '|' || out[i] == '%' || out[i] == '\n' || out[i] == '\r' ) {
			out[i] = ' ';
		}
	}
}

static int BM_Today( void ) {
	qtime_t	qt;

	memset( &qt, 0, sizeof( qt ) );
	trap_RealTime( &qt );
	return ( qt.tm_year + 1900 ) * 10000 + ( qt.tm_mon + 1 ) * 100 + qt.tm_mday;
}

static const char *BM_Ordinal( int n ) {
	int		t = n % 100;

	if ( t >= 11 && t <= 13 ) {
		return va( "%ith", n );
	}
	switch ( n % 10 ) {
	case 1: return va( "%ist", n );
	case 2: return va( "%ind", n );
	case 3: return va( "%ird", n );
	default: return va( "%ith", n );
	}
}

static const char *BM_WeaponName( int mod ) {
	switch ( mod ) {
	case MOD_SHOTGUN: return "Shotgun";
	case MOD_GAUNTLET: return "Gauntlet";
	case MOD_MACHINEGUN: return "Machinegun";
	case MOD_GRENADE: case MOD_GRENADE_SPLASH: return "Grenade Launcher";
	case MOD_ROCKET: case MOD_ROCKET_SPLASH: return "Rocket Launcher";
	case MOD_PLASMA: case MOD_PLASMA_SPLASH: return "Plasma Gun";
	case MOD_RAILGUN: return "Railgun";
	case MOD_LIGHTNING: return "Lightning Gun";
	case MOD_BFG: case MOD_BFG_SPLASH: return "BFG";
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case MOD_NAIL: return "Nailgun";
	case MOD_CHAINGUN: return "Chaingun";
	case MOD_PROXIMITY_MINE: return "Prox Launcher";
#endif
	case MOD_TELEFRAG: return "Telefrag";
	case MOD_GRAPPLE: return "Grapple";
	case MOD_BURN: return "Fire";
	case MOD_THUNDER: return "Thunder";
	case MOD_SLAM: return "Ground Pound";
	case MOD_PROP: return "Barrel";
	case MOD_WALLSPLAT: return "Wall";
	case MOD_METEOR: return "Meteor";
	case MOD_THORNS: return "Thorns";
	case MOD_CORPSE: return "Corpse Blast";
	default: return "Bare hands";
	}
}

static bmRec_t *BM_Find( const char *rawName, qboolean add ) {
	char	name[24];
	int		i, low;

	BM_Clean( rawName, name, sizeof( name ) );
	if ( !name[0] ) {
		return NULL;
	}
	for ( i = 0 ; i < bm.num ; i++ ) {
		if ( !Q_stricmp( bm.r[i].name, name ) ) {
			return &bm.r[i];
		}
	}
	if ( !add ) {
		return NULL;
	}
	if ( bm.num < BM_MAX_BOTS ) {
		i = bm.num++;
	} else {
		// full: the least-met record makes room
		low = 0;
		for ( i = 1 ; i < BM_MAX_BOTS ; i++ ) {
			if ( bm.r[i].fm + bm.r[i].k + bm.r[i].d < bm.r[low].fm + bm.r[low].k + bm.r[low].d ) {
				low = i;
			}
		}
		i = low;
	}
	memset( &bm.r[i], 0, sizeof( bm.r[i] ) );
	Q_strncpyz( bm.r[i].name, name, sizeof( bm.r[i].name ) );
	return &bm.r[i];
}

static int BM_Tier( const bmRec_t *r ) {
	if ( !r || r->fm <= 0 ) {
		return BMT_STRANGER;
	}
	if ( r->nem > 0 ) {
		return BMT_NEMHIST;
	}
	if ( r->fm >= BM_ARCH_FLOORS || r->k + r->d >= BM_ARCH_KILLS ) {
		return BMT_ARCH;
	}
	if ( r->fm >= BM_RIVAL_FLOORS || r->k + r->d >= BM_RIVAL_KILLS ) {
		return BMT_RIVAL;
	}
	return BMT_FAMILIAR;
}

static const char *BM_MoodName( const bmRec_t *r ) {
	return BM_FEAR( r->k, r->d ) ? "fear" : BM_GRUDGE( r->k, r->d ) ? "grudge" : "-";
}

// an eligible named bot: no boss, no surprise / affix extra, no boss-role kit
static qboolean BM_Eligible( gentity_t *ent ) {
	int		c;

	if ( !ent || !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED
		|| !( ent->r.svFlags & SVF_BOT ) || G_RL_IsPlayer( ent ) ) {
		return qfalse;
	}
	c = ent - g_entities;
	return c != G_DS_BossClient() && !G_DS_SurpriseExtra( c ) && !G_DS_AffixExtra( c ) && G_RL_ClientRole( ent ) != 4;
}

static bmRec_t *BM_RecOf( int c ) {
	if ( c < 0 || c >= MAX_CLIENTS || bm.rec[c] < 0 || bm.rec[c] >= bm.num ) {
		return NULL;
	}
	if ( !g_entities[c].inuse || !g_entities[c].client || bm.enter[c] != g_entities[c].client->pers.enterTime + 1 ) {
		return NULL;
	}
	return &bm.r[bm.rec[c]];
}

static bmHall_t *BM_HallOpen( const char *name ) {
	int		i;

	for ( i = bm.numHall - 1 ; i >= 0 ; i-- ) {
		if ( bm.h[i].status == BMH_OPEN && bm.h[i].run == bm.seed && !Q_stricmp( bm.h[i].name, name ) ) {
			return &bm.h[i];
		}
	}
	return NULL;
}

// the weapon the player last slew this bot with as the nemesis ("" none)
static const char *BM_HallWeapon( const char *name ) {
	int		i;

	for ( i = bm.numHall - 1 ; i >= 0 ; i-- ) {
		if ( bm.h[i].status == BMH_SLAIN && !Q_stricmp( bm.h[i].name, name ) ) {
			return bm.h[i].how;
		}
	}
	return "";
}

static void BM_PlayerName( char *out, int size ) {
	int		h = G_RL_HumanClient();

	if ( h >= 0 && g_entities[h].client ) {
		BM_Clean( g_entities[h].client->pers.netname, out, size );
	} else {
		out[0] = 0;
	}
	if ( !out[0] ) {
		Q_strncpyz( out, "you", size );
	}
}

/*
=================
BM_Format

A line template with the record's numbers (k / d / fm already include whatever the caller adds)
=================
*/
static void BM_Format( const char *tpl, const bmRec_t *r, int k, int d, int fm, char *out, int size ) {
	char		pname[32], tok;
	const char	*s, *add;
	int			n = 0;

	BM_PlayerName( pname, sizeof( pname ) );
	out[0] = 0;
	for ( s = tpl ; *s && n < size - 1 ; s++ ) {
		if ( s[0] == '{' && s[1] && s[2] == '}' ) {
			tok = s[1];
			s += 2;
			switch ( tok ) {
			case 'N': add = pname; break;
			case 'K': add = va( "%i", k ); break;
			case 'D': add = va( "%i", d ); break;
			case 'F': add = va( "%i", fm ); break;
			case 'G': add = va( "%i", fm + 1 ); break;
			case 'M': add = BM_Ordinal( fm + 1 ); break;
			case 'R': add = va( "%i", r->rm ); break;
			case 'X': add = va( "%i", r->nem ); break;
			case 'W': add = BM_HallWeapon( r->name ); break;
			case 'S':
				add = k > d ? va( "%i to %i, me.", k, d ) : k < d ? va( "%i to %i, you. For now.", d, k ) : va( "%i all.", k );
				break;
			default: add = ""; break;
			}
			Q_strncpyz( out + n, add, size - n );
			n = strlen( out );
			continue;
		}
		out[n++] = *s;
		out[n] = 0;
	}
	// never a stray format code for G_PR_SayTo
	for ( n = 0 ; out[n] ; n++ ) {
		if ( out[n] == '%' ) {
			out[n] = ' ';
		}
	}
}

/*
=================
BM_GreetLine

The greeting this record earns now (before this floor counts)
=================
*/
static const char *BM_GreetTemplate( const bmRec_t *r, int tier ) {
	if ( tier >= BMT_FAMILIAR && BM_FEAR( r->k, r->d ) ) {
		return BM_PICK( bmFear );
	}
	if ( tier >= BMT_FAMILIAR && tier < BMT_NEMHIST && BM_GRUDGE( r->k, r->d ) && ( rand() & 1 ) ) {
		return BM_PICK( bmGrudge );
	}
	switch ( tier ) {
	case BMT_STRANGER: return BM_PICK( bmStranger );
	case BMT_FAMILIAR: return BM_PICK( bmFamiliar );
	case BMT_RIVAL: return BM_PICK( bmRival );
	case BMT_ARCH: return BM_PICK( bmArch );
	default:
		if ( BM_HallWeapon( r->name )[0] && ( rand() % 3 ) == 0 ) {
			return BM_PICK( bmNemSlain );
		}
		return r->nem > 1 ? BM_PICK( bmNemMany ) : BM_PICK( bmNemOnce );
	}
}

static const char *bmTierNames[5] = { "stranger", "familiar", "rival", "arch-rival", "old nemesis" };

static void BM_Greet( int c, bmRec_t *r, qboolean forced ) {
	char	line[BM_LINE_LEN], txt[40];
	int		tier = BM_Tier( r ), h = G_RL_HumanClient();

	BM_Format( BM_GreetTemplate( r, tier ), r, r->k, r->d, r->fm, line, sizeof( line ) );
	G_PR_SayTo( c, forced || tier >= BMT_ARCH ? 2 : 1, line, tier == BMT_STRANGER ? PRB_FOUND : BM_FEAR( r->k, r->d ) ? PRB_RETREAT : PRB_AGAIN, h );
	G_LogPrintf( "BotMem: greet %i %s tier %i %s (k %i d %i fm %i rm %i nem %i mood %s): \"%s\"\n", c, r->name, tier,
		bmTierNames[tier], r->k, r->d, r->fm, r->rm, r->nem, BM_MoodName( r ), line );
	bm.greeted[c] = 1;
	bm.greets++;
	if ( tier == BMT_STRANGER ) {
		bm.strangers++;
	}
	// the best rival of the floor: a short callout at the right edge (never centre)
	if ( tier >= BMT_RIVAL && !bm.callouts && h >= 0 ) {
		bm.callouts++;
		Com_sprintf( txt, sizeof( txt ), "YOU %i - %i   %i FLOORS", r->d, r->k, r->fm );
		trap_SendServerCommand( h, va( "prc %i %i %i %i \"%s\"", PRC_RIVAL, c, tier, r->fm, txt ) );
	}
}

/*
===============================================================================

FILE

===============================================================================
*/

// field n of a '|' line
static void BM_Field( const char *line, int n, char *out, int size ) {
	int		i;

	while ( n > 0 && *line ) {
		if ( *line == '|' ) {
			n--;
		}
		line++;
	}
	for ( i = 0 ; i < size - 1 && line[i] && line[i] != '|' ; i++ ) {
		out[i] = line[i];
	}
	out[i] = 0;
}

static int BM_FieldInt( const char *line, int n ) {
	char	f[16];

	BM_Field( line, n, f, sizeof( f ) );
	return atoi( f );
}

static void BM_ParseLine( const char *line ) {
	bmRec_t		*r;
	bmHall_t	*h;
	char		name[24];

	if ( line[0] == 'b' && line[1] == '|' ) {
		BM_Field( line, 1, name, sizeof( name ) );
		r = BM_Find( name, qtrue );
		if ( !r ) {
			return;
		}
		r->k = BM_FieldInt( line, 2 );
		r->d = BM_FieldInt( line, 3 );
		r->fm = BM_FieldInt( line, 4 );
		r->rm = BM_FieldInt( line, 5 );
		r->lastRun = BM_FieldInt( line, 6 );
		r->lastDate = BM_FieldInt( line, 7 );
		r->lastRes = BM_FieldInt( line, 8 );
		r->lastK = BM_FieldInt( line, 9 );
		r->lastD = BM_FieldInt( line, 10 );
		r->nem = BM_FieldInt( line, 11 );
		r->nemPeak = BM_FieldInt( line, 12 );
		r->hs = BM_FieldInt( line, 13 );
		r->rv = BM_FieldInt( line, 14 );
		r->brv = BM_FieldInt( line, 15 );
	} else if ( line[0] == 'h' && line[1] == '|' && bm.numHall < BM_MAX_HALL ) {
		h = &bm.h[bm.numHall++];
		memset( h, 0, sizeof( *h ) );
		h->run = BM_FieldInt( line, 1 );
		h->runNo = BM_FieldInt( line, 2 );
		h->date = BM_FieldInt( line, 3 );
		BM_Field( line, 4, h->name, sizeof( h->name ) );
		BM_Field( line, 5, h->title, sizeof( h->title ) );
		h->peak = BM_FieldInt( line, 6 );
		h->kills = BM_FieldInt( line, 7 );
		h->floorRose = BM_FieldInt( line, 8 );
		h->status = BM_FieldInt( line, 9 );
		h->floorEnd = BM_FieldInt( line, 10 );
		BM_Field( line, 11, h->how, sizeof( h->how ) );
		if ( h->status == BMH_OPEN && h->run != bm.seed ) {
			h->status = BMH_RUNENDED;		// an old run's nemesis whose run ended without us seeing it
			bm.dirty = 1;
		}
	}
}

static void BM_Load( void ) {
	fileHandle_t	f;
	int				len;
	char			*p, *line;

	bm.num = bm.numHall = 0;
	len = trap_FS_FOpenFile( BM_FILE, &f, FS_READ );
	if ( !f ) {
		return;
	}
	if ( len > (int)sizeof( bmBuf ) - 1 ) {
		len = sizeof( bmBuf ) - 1;
	}
	bmBuf[0] = 0;
	if ( len > 0 ) {
		trap_FS_Read( bmBuf, len, f );
		bmBuf[len] = 0;
	}
	trap_FS_FCloseFile( f );
	p = bmBuf;
	while ( *p ) {
		line = p;
		while ( *p && *p != '\n' ) {
			p++;
		}
		if ( *p ) {
			*p++ = 0;
		}
		len = strlen( line );
		while ( len > 0 && line[len - 1] == '\r' ) {
			line[--len] = 0;
		}
		BM_ParseLine( line );
	}
}

static void BM_Write( void ) {
	fileHandle_t	f;
	int				i;
	bmRec_t			*r;
	bmHall_t		*h;

	bmBuf[0] = 0;
	Q_strcat( bmBuf, sizeof( bmBuf ), "v|1\n" );
	for ( i = 0 ; i < bm.num ; i++ ) {
		r = &bm.r[i];
		Q_strcat( bmBuf, sizeof( bmBuf ), va( "b|%s|%i|%i|%i|%i|%i|%i|%i|%i|%i|%i|%i|%i|%i|%i\n", r->name, r->k, r->d, r->fm,
			r->rm, r->lastRun, r->lastDate, r->lastRes, r->lastK, r->lastD, r->nem, r->nemPeak, r->hs, r->rv, r->brv ) );
	}
	for ( i = 0 ; i < bm.numHall ; i++ ) {
		h = &bm.h[i];
		Q_strcat( bmBuf, sizeof( bmBuf ), va( "h|%i|%i|%i|%s|%s|%i|%i|%i|%i|%i|%s\n", h->run, h->runNo, h->date, h->name,
			h->title, h->peak, h->kills, h->floorRose, h->status, h->floorEnd, h->how ) );
	}
	trap_FS_FOpenFile( BM_FILE, &f, FS_WRITE );
	if ( !f ) {
		G_LogPrintf( "BotMem: can't write %s\n", BM_FILE );
		return;
	}
	trap_FS_Write( bmBuf, strlen( bmBuf ), f );
	trap_FS_FCloseFile( f );
	bm.dirty = 0;
}

/*
=================
G_BM_Init

G_DS_NemesisInit (every DESCENT floor load, after the run file is read)
=================
*/
void G_BM_Init( void ) {
	int		i;

	G_BM_RegisterCvars();
	memset( &bm, 0, sizeof( bm ) );
	for ( i = 0 ; i < MAX_CLIENTS ; i++ ) {
		bm.rec[i] = -1;
	}
	if ( !BM_Allowed() ) {
		return;		// daily / practice: nothing read, nothing written
	}
	bm.seed = atoi( G_RL_RunGet( "seed" ) );
	BM_Load();
	bm.loaded = qtrue;
	G_LogPrintf( "BotMem: loaded %i bots, %i hall entries (run %i, g_botMemory %i)\n", bm.num, bm.numHall, bm.seed, BM_Mode() );
}

/*
===============================================================================

PLAY

===============================================================================
*/

static void BM_SetGrudge( int c, int h, int msec, bmRec_t *r ) {
	gEnemy_t	*e = G_EN( c );

	if ( !e || h < 0 || !trap_Cvar_VariableIntegerValue( "g_botGrudges" ) ) {
		return;		// (en-review: with g_botGrudges 0 G_PR_Frame cleared it the next frame anyway)
	}
	if ( ( e->flags & DSEF_GRUDGE ) && level.time < e->grudgeUntil && e->grudge != h ) {
		return;		// a fresh grudge on someone else wins
	}
	e->grudge = h;
	if ( e->grudgeUntil < level.time + msec ) {
		e->grudgeUntil = level.time + msec;
	}
	G_EN_SetFlag( c, DSEF_GRUDGE, qtrue );
	G_LogPrintf( "BotMem: lifetime grudge %i %s hunts the player for %i s (lifetime %i-%i)\n", c, r->name, msec / 1000, r->k, r->d );
}

static qboolean BM_Near( gentity_t *a, gentity_t *b, float dist ) {
	vec3_t	v;
	trace_t	tr;

	VectorSubtract( a->r.currentOrigin, b->r.currentOrigin, v );
	if ( VectorLength( v ) > dist || !trap_InPVS( a->r.currentOrigin, b->r.currentOrigin ) ) {
		return qfalse;
	}
	trap_Trace( &tr, a->client->ps.origin, NULL, NULL, b->client->ps.origin, a->s.number, MASK_SOLID );
	return tr.fraction >= 1.0f;
}

void G_BM_Frame( void ) {
	int			i, h, mode, best, bestScore, score, tier, start, maxGreets;
	gentity_t	*ent;
	bmRec_t		*r;
	qboolean	humanIn;
	char		line[BM_LINE_LEN];

	if ( !BM_Active() || bm.floorDone || level.time < bm.nextScan ) {
		return;
	}
	bm.nextScan = level.time + 500;
	trap_Cvar_Update( &bmCv );
	mode = BM_Mode();
	h = G_RL_HumanClient();
	humanIn = h >= 0 && g_entities[h].client && g_entities[h].client->pers.connected == CON_CONNECTED
		&& level.time - g_entities[h].client->pers.enterTime > 4000 && !level.intermissiontime && !level.intermissionQueued;
	if ( humanIn && !bm.humanInTime ) {
		bm.humanInTime = level.time;
	}
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		ent = &g_entities[i];
		if ( !BM_Eligible( ent ) ) {
			continue;
		}
		if ( bm.enter[i] != ent->client->pers.enterTime + 1 ) {
			// a new bot in this slot
			r = BM_Find( ent->client->pers.netname, qtrue );
			bm.enter[i] = ent->client->pers.enterTime + 1;
			bm.rec[i] = r ? r - bm.r : -1;
			bm.arrive[i] = level.time;
			bm.greeted[i] = 0;
			bm.panics[i] = 0;
			bm.panicNext[i] = level.time + 20000;
			bm.grudgeNext[i] = 0;
		}
		r = BM_RecOf( i );
		if ( !r ) {
			continue;
		}
		r->seen = 1;
		if ( !humanIn || ent->health <= 0 || mode != 1 ) {
			continue;
		}
		// lifetime grudge: hunts you from its first minute, again every 2 minutes
		if ( BM_GRUDGE( r->k, r->d ) && r->fm > 0 && level.time >= bm.grudgeNext[i] && !G_DS_NemesisIs( ent ) ) {
			BM_SetGrudge( i, h, bm.grudgeNext[i] ? BM_GRUDGE_AGAIN : BM_GRUDGE_FIRST, r );
			bm.grudgeNext[i] = level.time + BM_GRUDGE_EVERY;
		}
		// fear: a panic line now and then when you come close (2 a floor at most)
		if ( BM_FEAR( r->k, r->d ) && level.time >= bm.panicNext[i] && bm.panics[i] < 2 && g_entities[h].health > 0
			&& BM_Near( ent, &g_entities[h], 700 ) ) {
			bm.panicNext[i] = level.time + 45000;
			if ( random() < 0.4f ) {
				bm.panics[i]++;
				BM_Format( BM_PICK( bmPanic ), r, r->k, r->d, r->fm, line, sizeof( line ) );
				G_PR_SayTo( i, 0, line, PRB_RETREAT, h );
				G_LogPrintf( "BotMem: panic %i %s (lifetime %i-%i): \"%s\"\n", i, r->name, r->k, r->d, line );
			}
		}
	}

	// greetings: the most significant bot that arrived recently, one at a time
	if ( !humanIn || !mode || level.time < bm.nextGreet ) {
		return;
	}
	maxGreets = trap_Cvar_VariableIntegerValue( "g_botTaunts" ) >= 2 ? 3 : 2;
	if ( bm.greets >= maxGreets || level.time - bm.humanInTime < 1000 ) {
		return;
	}
	best = -1;
	bestScore = -1;
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		ent = &g_entities[i];
		r = BM_RecOf( i );
		if ( !r || bm.greeted[i] || ent->health <= 0 || !BM_Eligible( ent ) || G_DS_NemesisIs( ent ) ) {
			continue;
		}
		start = bm.arrive[i] > bm.humanInTime ? bm.arrive[i] : bm.humanInTime;
		if ( level.time - start > BM_GREET_WINDOW ) {
			continue;
		}
		tier = BM_Tier( r );
		if ( tier == BMT_STRANGER && ( bm.strangers > 0 || bm.greets > 0 ) ) {
			continue;		// one stranger a floor, only when nobody knows you
		}
		score = tier * 1000 + ( r->k + r->d < 999 ? r->k + r->d : 999 );
		if ( score > bestScore ) {
			bestScore = score;
			best = i;
		}
	}
	if ( best < 0 ) {
		return;
	}
	r = BM_RecOf( best );
	if ( BM_Tier( r ) == BMT_STRANGER && random() < 0.5f ) {
		bm.greeted[best] = 1;		// strangers don't always bother
		return;
	}
	BM_Greet( best, r, qfalse );
	bm.nextGreet = level.time + ( trap_Cvar_VariableIntegerValue( "g_botTaunts" ) >= 2 ? 9000 : 21000 );
}

// g_ds_rank.c Vet_Arrive: memory speaks for this bot (the veteran's generic "We meet again" stays quiet)
qboolean G_BM_OwnsArrival( gentity_t *ent ) {
	bmRec_t	*r;

	if ( !BM_Active() || !BM_Mode() || !ent || !ent->client ) {
		return qfalse;
	}
	r = BM_Find( ent->client->pers.netname, qfalse );
	return r && r->fm > 0;
}

// ai_tactics.c: 1 = this bot fears the player (retreats a little earlier, focuses them a little less)
float G_BM_Fear( int client ) {
	bmRec_t	*r;

	if ( !BM_Active() || BM_Mode() != 1 ) {
		return 0;
	}
	r = BM_RecOf( client );
	return r && r->fm > 0 && BM_FEAR( r->k, r->d ) ? 1.0f : 0.0f;
}

/*
=================
G_BM_Death

G_RL_PlayerDie (a DESCENT floor in play), before the REVENGE medal clears pers.lastKiller
=================
*/
void G_BM_Death( gentity_t *self, gentity_t *attacker ) {
	bmRec_t		*r;
	int			s, a;

	if ( !BM_Active() || bm.floorDone || !self || !self->client || !attacker || !attacker->client || attacker == self ) {
		return;
	}
	s = self - g_entities;
	a = attacker - g_entities;
	if ( G_RL_IsPlayer( self ) && BM_Eligible( attacker ) ) {
		r = BM_Find( attacker->client->pers.netname, qtrue );
		if ( !r ) {
			return;
		}
		r->k++;
		r->fk++;
		r->seen = 1;
		if ( attacker->client->pers.lastKiller == s ) {
			r->brv++;
		}
	} else if ( G_RL_IsPlayer( attacker ) && BM_Eligible( self ) ) {
		r = BM_Find( self->client->pers.netname, qtrue );
		if ( !r ) {
			return;
		}
		r->d++;
		r->fd++;
		r->seen = 1;
		if ( attacker->client->hitTarget == s && ( attacker->client->hitFlags & HIT_HEADSHOT ) ) {
			r->hs++;
			G_LogPrintf( "BotMem: headshot on %s (%i lifetime)\n", r->name, r->hs );
		}
		if ( attacker->client->pers.lastKiller == s ) {
			r->rv++;
			G_LogPrintf( "BotMem: revenge on %s (%i lifetime)\n", r->name, r->rv );
		}
	}
}

/*
=================
G_BM_KillLine / G_BM_DeathLine

G_PR_Death (player_die, BEFORE G_BM_Death counts this kill): a line quoting the record, or NULL
=================
*/
const char *G_BM_KillLine( int bot ) {
	static char	line[BM_LINE_LEN];
	bmRec_t		*r;
	int			tier;
	const char	*tpl;

	if ( !BM_Active() || !BM_Mode() || !BM_Eligible( &g_entities[bot] ) ) {
		return NULL;
	}
	r = BM_Find( g_entities[bot].client->pers.netname, qfalse );
	tier = BM_Tier( r );
	if ( !r || tier == BMT_STRANGER || random() >= ( tier >= BMT_RIVAL ? 0.5f : 0.3f ) ) {
		return NULL;
	}
	tpl = BM_FEAR( r->k, r->d ) ? BM_PICK( bmKillFear ) : BM_GRUDGE( r->k, r->d ) ? BM_PICK( bmKillGrudge )
		: tier == BMT_NEMHIST && ( rand() & 1 ) ? BM_PICK( bmKillNem ) : BM_PICK( bmKill );
	BM_Format( tpl, r, r->k + 1, r->d, r->fm, line, sizeof( line ) );
	G_LogPrintf( "BotMem: kill line %s tier %i: \"%s\"\n", r->name, tier, line );
	return line;
}

const char *G_BM_DeathLine( int bot ) {
	static char	line[BM_LINE_LEN];
	bmRec_t		*r;
	int			tier;
	const char	*tpl;

	if ( !BM_Active() || !BM_Mode() || !BM_Eligible( &g_entities[bot] ) ) {
		return NULL;
	}
	r = BM_Find( g_entities[bot].client->pers.netname, qfalse );
	tier = BM_Tier( r );
	if ( !r || ( tier < BMT_RIVAL && !BM_FEAR( r->k, r->d ) ) || random() >= 0.25f ) {
		return NULL;
	}
	if ( BM_FEAR( r->k, r->d ) ) {
		tpl = BM_PICK( bmDeathFear );
	} else {
		tpl = r->k > r->d + 1 ? bmDeath[1] : bmDeath[0];
	}
	BM_Format( tpl, r, r->k, r->d + 1, r->fm, line, sizeof( line ) );
	G_LogPrintf( "BotMem: death line %s tier %i: \"%s\"\n", r->name, tier, line );
	return line;
}

/*
===============================================================================

NEMESIS HALL OF FAME (g_ds_nemesis.c)

===============================================================================
*/

static bmHall_t *BM_HallAdd( void ) {
	int		i, drop = -1;

	if ( bm.numHall >= BM_MAX_HALL ) {
		for ( i = 0 ; i < bm.numHall ; i++ ) {
			if ( bm.h[i].status != BMH_OPEN ) {
				drop = i;
				break;
			}
		}
		if ( drop < 0 ) {
			drop = 0;
		}
		memmove( &bm.h[drop], &bm.h[drop + 1], sizeof( bm.h[0] ) * ( bm.numHall - drop - 1 ) );
		bm.numHall--;
	}
	memset( &bm.h[bm.numHall], 0, sizeof( bm.h[0] ) );
	return &bm.h[bm.numHall++];
}

// a bot takes the nemesis title (oldName: the nemesis it dethroned, "" none)
void G_BM_NemesisRise( const char *rawName, int kills, const char *oldName ) {
	char		name[24], old[24];
	bmHall_t	*h;
	bmRec_t		*r;

	if ( !BM_Active() ) {
		return;
	}
	BM_Clean( rawName, name, sizeof( name ) );
	BM_Clean( oldName, old, sizeof( old ) );
	if ( !name[0] ) {
		return;
	}
	if ( old[0] && Q_stricmp( old, name ) && ( h = BM_HallOpen( old ) ) != NULL ) {
		h->status = BMH_DETHRONED;
		h->floorEnd = G_RL_Floor();
		Q_strncpyz( h->how, name, sizeof( h->how ) );
		G_LogPrintf( "BotMem: hall %s dethroned by %s on floor %i\n", old, name, h->floorEnd );
	}
	if ( BM_HallOpen( name ) ) {
		return;		// (dev: named again)
	}
	h = BM_HallAdd();
	h->run = bm.seed;
	h->runNo = G_RL_MetaGetInt( "runs", 0 ) + 1;
	h->date = BM_Today();
	Q_strncpyz( h->name, name, sizeof( h->name ) );
	h->peak = 1;
	h->kills = kills;
	h->floorRose = G_RL_Floor();
	h->status = BMH_OPEN;
	r = BM_Find( name, qtrue );
	if ( r ) {
		r->nem++;
		if ( r->nemPeak < 1 ) {
			r->nemPeak = 1;
		}
	}
	G_LogPrintf( "BotMem: hall %s rises in run #%i on floor %i (%i kills)\n", name, h->runNo, h->floorRose, kills );
}

// a level / title / kills change of the nemesis
void G_BM_NemesisUpdate( const char *rawName, int lvl, const char *title, int kills ) {
	char		name[24];
	bmHall_t	*h;
	bmRec_t		*r;

	if ( !BM_Active() ) {
		return;
	}
	BM_Clean( rawName, name, sizeof( name ) );
	h = BM_HallOpen( name );
	if ( h ) {
		if ( lvl > h->peak ) {
			h->peak = lvl;
		}
		if ( title && title[0] ) {
			BM_Clean( title, h->title, sizeof( h->title ) );
		}
		if ( kills > h->kills ) {
			h->kills = kills;
		}
	}
	r = BM_Find( name, qfalse );
	if ( r && lvl > r->nemPeak ) {
		r->nemPeak = lvl;
	}
}

// the player killed the nemesis (mod = what killed it)
void G_BM_NemesisSlain( const char *rawName, int lvl, int kills, int mod ) {
	char		name[24];
	bmHall_t	*h;

	if ( !BM_Active() ) {
		return;
	}
	G_BM_NemesisUpdate( rawName, lvl, NULL, kills );
	BM_Clean( rawName, name, sizeof( name ) );
	h = BM_HallOpen( name );
	if ( !h ) {
		return;
	}
	h->status = BMH_SLAIN;
	h->floorEnd = G_RL_Floor();
	Q_strncpyz( h->how, BM_WeaponName( mod ), sizeof( h->how ) );
	G_LogPrintf( "BotMem: hall %s slain by the player (%s) on floor %i, peak level %i, %i kills\n", name, h->how,
		h->floorEnd, h->peak, h->kills );
}

/*
=================
G_BM_FloorEnd

RL_Finish, after the run-over decision, before the run file is saved: the floor counts, the file
is written (the only write)
=================
*/
void G_BM_FloorEnd( qboolean won, qboolean runOver ) {
	int		i, date, met = 0;
	bmRec_t	*r;

	if ( !BM_Active() ) {
		return;
	}
	if ( bm.floorDone ) {
		return;
	}
	bm.floorDone = qtrue;
	date = BM_Today();
	for ( i = 0 ; i < bm.num ; i++ ) {
		r = &bm.r[i];
		if ( !r->seen ) {
			continue;
		}
		r->fm++;
		if ( r->lastRun != bm.seed || !r->rm ) {
			r->rm++;
			r->lastRun = bm.seed;
		}
		r->lastDate = date;
		r->lastRes = won ? 1 : 2;
		r->lastK = r->fk;
		r->lastD = r->fd;
		G_LogPrintf( "BotMem: floor end %s: %i-%i this floor, lifetime k %i d %i fm %i rm %i nem %i peak %i hs %i rv %i brv %i tier %i\n",
			r->name, r->fk, r->fd, r->k, r->d, r->fm, r->rm, r->nem, r->nemPeak, r->hs, r->rv, r->brv, BM_Tier( r ) );
		r->fk = r->fd = r->seen = 0;
		met++;
	}
	if ( runOver ) {
		for ( i = 0 ; i < bm.numHall ; i++ ) {
			if ( bm.h[i].status == BMH_OPEN && bm.h[i].run == bm.seed ) {
				bm.h[i].status = BMH_OUTLIVED;
				bm.h[i].floorEnd = G_RL_Floor();
				G_LogPrintf( "BotMem: hall %s outlived the run (floor %i)\n", bm.h[i].name, bm.h[i].floorEnd );
			}
		}
	}
	BM_Write();
	G_LogPrintf( "BotMem: wrote %s: %i bots (%i met this floor), %i hall entries, floor %s%s\n", BM_FILE, bm.num, met,
		bm.numHall, won ? "won" : "lost", runOver ? ", run over" : "" );
}

/*
=================
G_BM_Command

Dev: dsbm | dsbm set <bot> <k> <d> <floors> <runs> [nem] | dsbm say <bot> | dsbm clear
=================
*/
qboolean G_BM_Command( const char *cmd ) {
	char	a[5][32];
	int		i, c;
	bmRec_t	*r;

	if ( Q_stricmp( cmd, "dsbm" ) ) {
		return qfalse;
	}
	if ( !BM_Active() ) {
		G_Printf( "dsbm: no bot memory on this floor (not DESCENT, daily or practice)\n" );
		G_LogPrintf( "BotMem: inactive (daily %i practice %i)\n", G_RL_Daily(), G_RL_Practice() );
		return qtrue;
	}
	for ( i = 0 ; i < 5 ; i++ ) {
		trap_Argv( i + 1, a[i], sizeof( a[i] ) );
	}
	// a client number for the bot: its name (tests: names vary by seed and floor)
	c = atoi( a[1] );
	if ( a[1][0] >= '0' && a[1][0] <= '9' && c >= 0 && c < level.maxclients && g_entities[c].inuse && g_entities[c].client ) {
		BM_Clean( g_entities[c].client->pers.netname, a[1], sizeof( a[1] ) );
	}
	if ( !Q_stricmp( a[0], "set" ) ) {
		r = BM_Find( a[1], qtrue );
		if ( r ) {
			r->k = atoi( a[2] );
			r->d = atoi( a[3] );
			r->fm = atoi( a[4] );
			trap_Argv( 6, a[0], sizeof( a[0] ) );
			r->rm = atoi( a[0] );
			trap_Argv( 7, a[0], sizeof( a[0] ) );
			r->nem = atoi( a[0] );
			G_LogPrintf( "BotMem: set %s k %i d %i fm %i rm %i nem %i tier %i\n", r->name, r->k, r->d, r->fm, r->rm, r->nem, BM_Tier( r ) );
		}
		return qtrue;
	}
	if ( !Q_stricmp( a[0], "say" ) ) {
		for ( c = 0 ; c < level.maxclients ; c++ ) {
			if ( BM_Eligible( &g_entities[c] ) && ( r = BM_RecOf( c ) ) != NULL && !Q_stricmp( r->name, a[1] ) ) {
				BM_Greet( c, r, qtrue );
				return qtrue;
			}
		}
		G_Printf( "dsbm say: %s is not on this floor\n", a[1] );
		return qtrue;
	}
	if ( !Q_stricmp( a[0], "clear" ) ) {
		bm.num = bm.numHall = 0;
		memset( bm.rec, -1, sizeof( bm.rec ) );
		memset( bm.enter, 0, sizeof( bm.enter ) );
		return qtrue;
	}
	for ( i = 0 ; i < bm.num ; i++ ) {
		r = &bm.r[i];
		G_Printf( "  %-12s k %3i d %3i floors %3i runs %2i nem %i peak %i hs %i rv %i brv %i  tier %s mood %s\n", r->name, r->k, r->d,
			r->fm, r->rm, r->nem, r->nemPeak, r->hs, r->rv, r->brv, bmTierNames[BM_Tier( r )], BM_MoodName( r ) );
	}
	for ( i = 0 ; i < bm.numHall ; i++ ) {
		G_Printf( "  hall %s %s run #%i floor %i peak %i kills %i status %i (%s)\n", bm.h[i].name, bm.h[i].title, bm.h[i].runNo,
			bm.h[i].floorRose, bm.h[i].peak, bm.h[i].kills, bm.h[i].status, bm.h[i].how );
	}
	G_LogPrintf( "BotMem: list %i bots %i hall\n", bm.num, bm.numHall );
	return qtrue;
}
