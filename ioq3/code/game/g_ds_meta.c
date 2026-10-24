/*
===========================================================================
REARMED -- DESCENT v2 META, game side (owner META).

Characters (forced model, starting weapon, health / speed), permanent upgrade
ranks, per-floor stats and the per-weapon damage breakdown, written into the run
file at floor end (keys: fst, st, fseq, dmg, wkills, wtime, cause, lastdeath,
fastest, infloor 0). The UI merges the finished floors into the lifetime
counters of rearmed_meta.txt and checks the unlocks (q3_ui/ui_ds_meta.c).
API: game/g_descent.h (META section). Contract: bg_descent.h.
===========================================================================
*/
#include "g_local.h"

typedef struct {
	int		character;
	int		ups[DSU_NUM];
	int		fst[DSST_NUM];			// this floor
	int		dmg[DS_NUM_WEAPONS];	// run totals (loaded at init, this floor added)
	int		wkills[DS_NUM_WEAPONS];
	int		wtime[DS_NUM_WEAPONS];
	int		startCards;
	int		revived;
	char	lastDeath[96];			// what killed the player last on this floor
} gMeta_t;

static gMeta_t	gmeta;

static int GMETA_ParseCsv( const char *s, int *out, int max ) {
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

static const char *GMETA_Csv( const int *v, int n ) {
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

static int GMETA_RunInt( const char *key, int def ) {
	const char	*s = G_RL_RunGet( key );

	return *s ? atoi( s ) : def;
}

static qboolean GMETA_IsHuman( gentity_t *ent ) {
	int		h;

	if ( !ent || !ent->client ) {
		return qfalse;
	}
	h = G_RL_HumanClient();
	return h >= 0 && ent - g_entities == h;
}

/*
=================
G_DS_MetaInit

End of G_RL_Init, only when a run is active: the run's character, upgrades and the
breakdown so far. This floor's stats start at zero (a resumed floor starts over).
=================
*/
void G_DS_MetaInit( void ) {
	int		i;

	memset( &gmeta, 0, sizeof( gmeta ) );
	G_DS_BestiaryInit();		// en-bestiary: who the player met, kills per enemy kind
	if ( !G_RL_Active() ) {
		return;
	}
	gmeta.character = GMETA_RunInt( "character", CHR_RANGER );
	if ( gmeta.character < 0 || gmeta.character >= CHR_NUM ) {
		gmeta.character = CHR_RANGER;
	}
	for ( i = 0 ; i < DSU_NUM ; i++ ) {
		gmeta.ups[i] = GMETA_RunInt( ds_upgrades[i].key, 0 );
		if ( gmeta.ups[i] < 0 ) {
			gmeta.ups[i] = 0;
		} else if ( gmeta.ups[i] > ds_upgrades[i].maxRank ) {
			gmeta.ups[i] = ds_upgrades[i].maxRank;
		}
	}
	GMETA_ParseCsv( G_RL_RunGet( "dmg" ), gmeta.dmg, DS_NUM_WEAPONS );
	GMETA_ParseCsv( G_RL_RunGet( "wkills" ), gmeta.wkills, DS_NUM_WEAPONS );
	GMETA_ParseCsv( G_RL_RunGet( "wtime" ), gmeta.wtime, DS_NUM_WEAPONS );
	gmeta.startCards = GMETA_RunInt( "startcards", 0 );
	gmeta.revived = GMETA_RunInt( "revived", 0 );
}

/*
===============================================================================

QUERIES

===============================================================================
*/

int G_DS_Character( void ) {
	return G_RL_Active() ? gmeta.character : -1;
}

int G_DS_UpRank( int up ) {
	if ( !G_RL_Active() || up < 0 || up >= DSU_NUM || DS_UP_RETIRED( up ) ) {
		return 0;		// (ds-economy pass 2: retired upgrades do nothing)
	}
	return gmeta.ups[up];
}

float G_DS_UpValue( int up ) {
	return G_DS_UpRank( up ) * ( up >= 0 && up < DSU_NUM ? ds_upgrades[up].perRank : 0 );
}

// the bots draw as many cards as the player picked: the free character kit doesn't count
int G_DS_KitCards( int numCards ) {
	if ( !G_RL_Active() ) {
		return numCards;
	}
	numCards -= gmeta.startCards;
	return numCards < 0 ? 0 : numCards;
}

int G_DS_CharHealth( void ) {
	return G_RL_Active() ? ds_characters[gmeta.character].healthDelta : 0;
}

float G_DS_CharSpeed( void ) {
	return G_RL_Active() ? ds_characters[gmeta.character].speedPct * 0.01f : 0;
}

const char *G_DS_ForcedModel( gentity_t *ent ) {
	if ( !G_RL_Active() || !ent || ( ent->r.svFlags & SVF_BOT ) ) {
		return NULL;
	}
	return ds_characters[gmeta.character].model;
}

/*
=================
G_DS_MetaSpawn

G_RL_SpawnLoadout, the human player: the character's weapon in hand
=================
*/
void G_DS_MetaSpawn( gentity_t *ent ) {
	gclient_t	*cl = ent->client;
	gitem_t		*item;
	int			w;

	if ( !G_RL_Active() || ( ent->r.svFlags & SVF_BOT ) ) {
		return;
	}
	w = ds_characters[gmeta.character].startWeapon;
	if ( w <= WP_NONE || w >= WP_NUM_WEAPONS ) {
		return;
	}
	cl->ps.stats[STAT_WEAPONS] |= 1 << w;
	if ( w != WP_GAUNTLET ) {
		item = BG_FindItemForWeapon( w );
		if ( item && cl->ps.ammo[w] < item->quantity ) {
			cl->ps.ammo[w] = item->quantity;
		}
	}
	cl->ps.weapon = w;
	cl->ps.weaponstate = WEAPON_READY;
}

/*
=================
G_DS_MetaRevive

A lost floor: qtrue = REVIVAL saves the life. It waits for the loss that would end
the descent, once per descent.
=================
*/
qboolean G_DS_MetaRevive( int livesLeft ) {
	if ( !G_RL_Active() || gmeta.revived || gmeta.ups[DSU_REVIVAL] <= 0 || livesLeft > 1 ) {
		return qfalse;
	}
	gmeta.revived = 1;
	G_RL_RunSet( "revived", "1" );
	G_LogPrintf( "Descent: REVIVAL spent, the loss costs no life\n" );
	return qtrue;
}

/*
===============================================================================

STATS

===============================================================================
*/

void G_DS_Stat( int stat, int n ) {
	if ( !G_RL_Active() || stat < 0 || stat >= DSST_NUM ) {
		return;
	}
	if ( stat == DSST_FLOOR || stat == DSST_BOSSID ) {
		gmeta.fst[stat] = n;
	} else {
		gmeta.fst[stat] += n;
	}
}

void G_DS_BossDefeated( int boss ) {
	if ( !G_RL_Active() || boss < 0 || boss >= BOSS_NUM ) {
		return;
	}
	gmeta.fst[DSST_BOSSES]++;
	gmeta.fst[DSST_BOSSID] = boss + 1;
}

// meansOfDeath -> weapon_t (0: not a weapon)
static int GMETA_ModWeapon( int mod, gentity_t *attacker ) {
	int		w;

	w = G_DS_WeaponForMod( mod );		// ARSENAL: covers its level 8 / evolution blasts
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
	// level 8 / evolution blasts, burns and the like: the gun in hand
	if ( mod == MOD_BURN || mod == MOD_THUNDER || mod >= MOD_CORPSE ) {
		if ( attacker && attacker->client ) {
			return attacker->client->ps.weapon;
		}
	}
	return 0;
}

/*
=================
G_DS_MetaDamage

G_Damage, a hit that landed on an enemy player: dealt = health + armor taken
=================
*/
void G_DS_MetaDamage( gentity_t *targ, gentity_t *attacker, int dealt, int mod, qboolean headshot ) {
	int		w;

	if ( !G_RL_Active() || targ == attacker || !targ->client || !GMETA_IsHuman( attacker ) || level.intermissiontime ) {
		return;
	}
	if ( dealt <= 0 ) {
		return;
	}
	w = GMETA_ModWeapon( mod, attacker );
	if ( w > 0 && w < DS_NUM_WEAPONS ) {
		gmeta.dmg[w] += dealt;
	}
	gmeta.fst[DSST_DAMAGE] += dealt;
	if ( headshot ) {
		gmeta.fst[DSST_HEADSHOTS]++;
	}
}

/*
=================
G_DS_MetaKill

player_die: kills by weapon and the kill stats for the human player, deaths and
what killed them
=================
*/
void G_DS_MetaKill( gentity_t *self, gentity_t *attacker, int mod, qboolean gibbed ) {
	char	name[MAX_NETNAME];
	int		w;

	G_DS_BestiaryKill( self, attacker );		// en-bestiary: kills per archetype / affix / ...
	if ( !G_RL_Active() || !self || !self->client || level.intermissiontime ) {
		return;
	}
	if ( GMETA_IsHuman( self ) ) {
		gmeta.fst[DSST_DEATHS]++;
		if ( attacker && attacker->client && attacker != self ) {
			Q_strncpyz( name, attacker->client->pers.netname, sizeof( name ) );
			Q_CleanStr( name );
			w = GMETA_ModWeapon( mod, attacker );
			if ( w > 0 && w < DS_NUM_WEAPONS && ds_weaponNames[w][0] ) {
				Com_sprintf( gmeta.lastDeath, sizeof( gmeta.lastDeath ), "%s's %s", name, ds_weaponNames[w] );
			} else {
				Q_strncpyz( gmeta.lastDeath, name, sizeof( gmeta.lastDeath ) );
			}
		} else {
			Q_strncpyz( gmeta.lastDeath, mod == MOD_FALLING || mod == MOD_TRIGGER_HURT ? "the void" : "yourself",
				sizeof( gmeta.lastDeath ) );
		}
		return;
	}
	if ( !GMETA_IsHuman( attacker ) || attacker == self ) {
		return;
	}
	gmeta.fst[DSST_KILLS]++;
	w = GMETA_ModWeapon( mod, attacker );
	if ( w > 0 && w < DS_NUM_WEAPONS ) {
		gmeta.wkills[w]++;
	}
	if ( mod == MOD_GAUNTLET ) {
		gmeta.fst[DSST_GKILLS]++;
	}
	if ( gibbed ) {
		gmeta.fst[DSST_GIBS]++;
	}
	if ( G_RL_ClientRole( self ) == 3 ) {
		gmeta.fst[DSST_ELITES]++;
	}
}

/*
=================
G_DS_MetaFrame

G_RL_RunFrame: time held per weapon
=================
*/
void G_DS_MetaFrame( void ) {
	gentity_t	*ent;
	int			h, w, msec;

	G_DS_BestiaryFrame();		// en-bestiary: meetings (sight / hurt)
	if ( !G_RL_Active() || level.intermissiontime || level.warmupTime ) {
		return;
	}
	h = G_RL_HumanClient();
	if ( h < 0 ) {
		return;
	}
	ent = &g_entities[h];
	if ( ent->health <= 0 || ent->client->ps.pm_type != PM_NORMAL ) {
		return;
	}
	w = ent->client->ps.weapon;
	msec = level.time - level.previousTime;
	if ( w > 0 && w < DS_NUM_WEAPONS && msec > 0 && msec < 1000 ) {
		gmeta.wtime[w] += msec;
	}
}

/*
=================
G_DS_MetaFloorEnd

RL_Finish, before the run file is saved: this floor's stats, the run totals, the
breakdown and (lost) what beat the player
=================
*/
void G_DS_MetaFloorEnd( qboolean won ) {
	int			st[DSST_NUM];
	int			i, h, best, bestClient, t, fastest;
	gclient_t	*cl;
	char		name[MAX_NETNAME];

	if ( !G_RL_Active() ) {
		return;
	}
	t = level.time - level.startTime;
	gmeta.fst[DSST_WON] = won ? 1 : 0;
	gmeta.fst[DSST_FLOOR] = G_RL_Floor();
	gmeta.fst[DSST_TIME] = t;

	memset( st, 0, sizeof( st ) );
	GMETA_ParseCsv( G_RL_RunGet( "st" ), st, DSST_NUM );
	for ( i = 0 ; i < DSST_NUM ; i++ ) {
		if ( i == DSST_FLOOR ) {
			if ( gmeta.fst[i] > st[i] ) {
				st[i] = gmeta.fst[i];
			}
		} else if ( i == DSST_BOSSID ) {
			if ( gmeta.fst[i] ) {
				st[i] = gmeta.fst[i];
			}
		} else {
			st[i] += gmeta.fst[i];
		}
	}

	G_RL_RunSet( "fst", GMETA_Csv( gmeta.fst, DSST_NUM ) );
	G_RL_RunSet( "st", GMETA_Csv( st, DSST_NUM ) );
	G_RL_RunSet( "fseq", va( "%i", GMETA_RunInt( "fseq", 0 ) + 1 ) );
	G_RL_RunSet( "dmg", GMETA_Csv( gmeta.dmg, DS_NUM_WEAPONS ) );
	G_RL_RunSet( "wkills", GMETA_Csv( gmeta.wkills, DS_NUM_WEAPONS ) );
	G_RL_RunSet( "wtime", GMETA_Csv( gmeta.wtime, DS_NUM_WEAPONS ) );
	G_RL_RunSet( "infloor", "0" );
	G_DS_BestiaryFloorEnd();		// en-bestiary: bst_fm / bst_fk / bst_rm (merged by the UI with fst)

	if ( won ) {
		fastest = GMETA_RunInt( "fastest", 0 );
		if ( !fastest || t < fastest ) {
			G_RL_RunSet( "fastest", va( "%i", t ) );
		}
		return;
	}

	// lost: who beat the player
	h = G_RL_HumanClient();
	best = -9999;
	bestClient = -1;
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		cl = &level.clients[i];
		if ( i == h || cl->pers.connected != CON_CONNECTED || cl->sess.sessionTeam == TEAM_SPECTATOR ) {
			continue;
		}
		if ( cl->ps.persistant[PERS_SCORE] > best ) {
			best = cl->ps.persistant[PERS_SCORE];
			bestClient = i;
		}
	}
	if ( bestClient >= 0 ) {
		Q_strncpyz( name, level.clients[bestClient].pers.netname, sizeof( name ) );
		Q_CleanStr( name );
		G_RL_RunSet( "cause", va( "Outfragged by %s, %i to %i, on %s", name, best,
			h >= 0 ? level.clients[h].ps.persistant[PERS_SCORE] : 0, G_RL_RunGet( "node_map" ) ) );
	} else {
		G_RL_RunSet( "cause", va( "Lost floor %i", G_RL_Floor() ) );
	}
	G_RL_RunSet( "lastdeath", gmeta.lastDeath[0] ? gmeta.lastDeath : "" );
}

/*
=================
G_DS_MetaCommand

Dev: dsstat <stat> <n> (add to this floor), dsmeta (print)
=================
*/
qboolean G_DS_MetaCommand( const char *cmd ) {
	char	arg[16];
	int		s;

	if ( G_DS_BestiaryCommand( cmd ) ) {		// en-bestiary dev: dsbst / dsbstmeet / dsbstkill
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsstat" ) ) {
		trap_Argv( 1, arg, sizeof( arg ) );
		s = atoi( arg );
		trap_Argv( 2, arg, sizeof( arg ) );
		G_DS_Stat( s, atoi( arg ) );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsmeta" ) ) {
		G_Printf( "dsmeta: active %i character %i startcards %i revived %i\n  fst %s\n  dmg %s\n  wkills %s\n  wtime %s\n",
			G_RL_Active(), gmeta.character, gmeta.startCards, gmeta.revived, GMETA_Csv( gmeta.fst, DSST_NUM ),
			GMETA_Csv( gmeta.dmg, DS_NUM_WEAPONS ), GMETA_Csv( gmeta.wkills, DS_NUM_WEAPONS ), GMETA_Csv( gmeta.wtime, DS_NUM_WEAPONS ) );
		G_LogPrintf( "DSMeta: character %i fst %s dmg %s wkills %s wtime %s\n", gmeta.character, GMETA_Csv( gmeta.fst, DSST_NUM ),
			GMETA_Csv( gmeta.dmg, DS_NUM_WEAPONS ), GMETA_Csv( gmeta.wkills, DS_NUM_WEAPONS ), GMETA_Csv( gmeta.wtime, DS_NUM_WEAPONS ) );
		return qtrue;
	}
	return qfalse;
}
