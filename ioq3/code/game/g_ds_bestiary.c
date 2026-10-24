/*
===========================================================================
REARMED -- round 4 BESTIARY, game side (en-bestiary). Table: bg_ds_bestiary.h.

The player MEETS an entry when a bot carrying it (archetype, affix, champion pack, ambush
mini-boss, act boss) comes into the player's view on their floor, or hurts the player.
Kills by the player count per entry. Nothing touches rearmed_meta.txt mid-run: the floor's
numbers go into the run file at floor end (bst_fm, bst_fk, bst_rm; G_DS_MetaFloorEnd) and the
UI merges them into the meta file once per floor (q3_ui/ui_ds_bestiary.c, DSM_MergeFloor).
The first meeting ever sends "dsbst <entry>" to the player: cgame shows "NEW IN BESTIARY"
at the left edge (cgame/cg_ds_bestiary.c).

Hooks (all from g_ds_meta.c): G_DS_BestiaryInit (G_DS_MetaInit), G_DS_BestiaryFrame
(G_DS_MetaFrame), G_DS_BestiaryKill (G_DS_MetaKill), G_DS_BestiaryFloorEnd
(G_DS_MetaFloorEnd), G_DS_BestiaryCommand (G_DS_MetaCommand).
Dev: dsbst (print), dsbstmeet <entry|name> (meet now), dsbstkill <entry|name> [n].
===========================================================================
*/
#include "g_local.h"
#include "bg_ds_bestiary.h"

#define BST_SCAN_MSEC		100		// visibility scan interval
#define BST_VIEW_DIST		4000.0f
#define BST_VIEW_COS		0.30f	// about 72 degrees off the view axis (wide screens see a lot)

typedef struct {
	qboolean	on;
	int			known;				// lifetime (meta) | this run (run key) | met since: no callout
	int			runMask;			// bst_rm at floor start
	int			met;				// met on this floor
	int			kills[DSB_NUM];		// on this floor
	int			nextScan;
	int			lastHp;				// the human's health + armour last frame (hurt check)
	int			lastHuman;
	int			mbClient;			// the ambush mini-boss seen in CS_DSSURPRISE (-1 none)
	int			mbKind;
} gBst_t;

static gBst_t	bst;

static const char *Bst_Csv( const int *v, int n ) {
	static char	buf[256];
	int			i;

	buf[0] = 0;
	for ( i = 0 ; i < n ; i++ ) {
		Q_strcat( buf, sizeof( buf ), va( i ? ",%i" : "%i", v[i] ) );
	}
	return buf;
}

// the lifetime meta masks as one entry mask
static int Bst_MetaKnown( void ) {
	int		m, i, types, affixes, extras;

	types = G_RL_MetaGetInt( "bst_seen", 0 );
	affixes = G_RL_MetaGetInt( "bst_aseen", 0 );
	extras = G_RL_MetaGetInt( "bst_xseen", 0 );
	m = 0;
	for ( i = 1 ; i < DSAR_NUM ; i++ ) {
		if ( types & ( 1 << i ) ) {
			m |= 1 << BG_DS_BstEntry( DSB_KIND_TYPE, i );
		}
	}
	for ( i = 0 ; i < DSAF_NUM ; i++ ) {
		if ( affixes & ( 1 << i ) ) {
			m |= 1 << BG_DS_BstEntry( DSB_KIND_AFFIX, i );
		}
	}
	for ( i = 0 ; i < DSBX_NUM ; i++ ) {
		if ( extras & ( 1 << i ) ) {
			m |= 1 << BG_DS_BstEntry( DSB_KIND_EXTRA, i );
		}
	}
	return m;
}

/*
=================
G_DS_BestiaryInit

G_DS_MetaInit (every floor of a run)
=================
*/
void G_DS_BestiaryInit( void ) {
	memset( &bst, 0, sizeof( bst ) );
	bst.mbClient = -1;
	bst.lastHuman = -1;
	if ( !G_RL_Active() || G_RL_Daily() ) {
		return;		// en-daily: a daily floor writes no meta and reads an empty profile: no bestiary
	}
	bst.on = qtrue;
	bst.runMask = atoi( G_RL_RunGet( "bst_rm" ) );
	bst.known = Bst_MetaKnown() | bst.runMask;
}

// every entry a client stands for (0 for the human / non-bots)
static int Bst_Entries( int client ) {
	gEnemy_t	*e;
	int			m, i, b;

	if ( client < 0 || client >= MAX_CLIENTS || !g_entities[client].inuse || !g_entities[client].client
		|| !( g_entities[client].r.svFlags & SVF_BOT ) ) {
		return 0;
	}
	m = 0;
	e = G_EN( client );
	if ( e ) {
		if ( e->arch > DSAR_NONE && e->arch < DSAR_NUM ) {
			m |= 1 << BG_DS_BstEntry( DSB_KIND_TYPE, e->arch );
		}
		for ( i = 0 ; i < DSAF_NUM ; i++ ) {
			if ( e->affix & ( 1 << i ) ) {
				m |= 1 << BG_DS_BstEntry( DSB_KIND_AFFIX, i );
			}
		}
		if ( e->affix & DSAF_CHAMPION ) {
			m |= 1 << BG_DS_BstEntry( DSB_KIND_EXTRA, DSBX_CHAMPION );
		}
	}
	if ( client == bst.mbClient && bst.mbKind >= 0 && bst.mbKind < DS_MINIBOSS_NUM ) {
		m |= 1 << BG_DS_BstEntry( DSB_KIND_EXTRA, DSBX_MINIBOSS + bst.mbKind );
	}
	if ( client == G_DS_BossClient() ) {
		b = G_DS_BossType();
		if ( b >= 0 && b < BOSS_NUM ) {
			m |= 1 << BG_DS_BstEntry( DSB_KIND_EXTRA, DSBX_BOSS + b );
		}
	}
	return m;
}

static void Bst_Meet( int client, int mask, const char *how ) {
	int		fresh, i, h;

	fresh = mask & ~bst.met;
	if ( !fresh ) {
		return;
	}
	bst.met |= fresh;
	h = G_RL_HumanClient();
	for ( i = 0 ; i < DSB_NUM ; i++ ) {
		if ( !( fresh & ( 1 << i ) ) ) {
			continue;
		}
		G_LogPrintf( "Bestiary: met %s (entry %i) client %i by %s%s\n", BG_DS_BstName( i ), i, client, how,
			( bst.known & ( 1 << i ) ) ? "" : " - NEW" );
		if ( bst.known & ( 1 << i ) ) {
			continue;
		}
		bst.known |= 1 << i;
		if ( h >= 0 ) {
			trap_SendServerCommand( h, va( "dsbst %i", i ) );
		}
	}
}

// the ambush mini-boss from ds-surprise's configstring (its client stays known after it dies)
static void Bst_ReadMiniBoss( void ) {
	char	buf[MAX_INFO_STRING];
	int		c;

	trap_GetConfigstring( CS_DSSURPRISE, buf, sizeof( buf ) );
	if ( !buf[0] ) {
		return;
	}
	c = atoi( Info_ValueForKey( buf, "mb" ) );
	if ( c >= 0 && c < MAX_CLIENTS && Info_ValueForKey( buf, "mb" )[0] ) {
		bst.mbClient = c;
		bst.mbKind = atoi( Info_ValueForKey( buf, "mbk" ) );
	}
}

static qboolean Bst_InView( gentity_t *viewer, gentity_t *other ) {
	vec3_t		eye, fwd, dir, spot;
	trace_t		tr;
	float		dist;

	VectorCopy( viewer->client->ps.origin, eye );
	eye[2] += viewer->client->ps.viewheight;
	VectorCopy( other->r.currentOrigin, spot );
	spot[2] += 16;
	VectorSubtract( spot, eye, dir );
	dist = VectorNormalize( dir );
	if ( dist > BST_VIEW_DIST ) {
		return qfalse;
	}
	AngleVectors( viewer->client->ps.viewangles, fwd, NULL, NULL );
	if ( dist > 64 && DotProduct( fwd, dir ) < BST_VIEW_COS ) {
		return qfalse;
	}
	if ( !trap_InPVS( eye, spot ) ) {
		return qfalse;
	}
	trap_Trace( &tr, eye, NULL, NULL, spot, viewer->s.number, CONTENTS_SOLID );
	return tr.fraction >= 0.99f || tr.entityNum == other->s.number;
}

/*
=================
G_DS_BestiaryFrame

G_DS_MetaFrame, first thing (every server frame of a run)
=================
*/
void G_DS_BestiaryFrame( void ) {
	gentity_t	*human, *ent;
	int			h, i, hp, m, c;

	if ( !bst.on || level.intermissiontime ) {
		return;
	}
	h = G_RL_HumanClient();
	if ( h < 0 || !g_entities[h].client ) {
		return;
	}
	human = &g_entities[h];

	// hurt: health + armour went down and the last one to hurt the player is a bot
	hp = human->client->ps.stats[STAT_HEALTH] + human->client->ps.stats[STAT_ARMOR];
	if ( h == bst.lastHuman && hp < bst.lastHp ) {
		c = human->client->lasthurt_client;
		if ( c != h && c >= 0 && c < MAX_CLIENTS ) {
			m = Bst_Entries( c );
			if ( m ) {
				Bst_Meet( c, m, "hurt" );
			}
		}
	}
	bst.lastHp = hp;
	bst.lastHuman = h;

	if ( level.time < bst.nextScan ) {
		return;
	}
	bst.nextScan = level.time + BST_SCAN_MSEC;
	Bst_ReadMiniBoss();
	if ( human->health <= 0 || human->client->sess.sessionTeam == TEAM_SPECTATOR ) {
		return;
	}
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( i == h ) {
			continue;
		}
		ent = &g_entities[i];
		if ( !ent->inuse || !ent->client || ent->health <= 0 || ent->client->pers.connected != CON_CONNECTED
			|| ent->client->sess.sessionTeam == TEAM_SPECTATOR || ( ent->r.svFlags & SVF_NOCLIENT ) ) {
			continue;
		}
		m = Bst_Entries( i ) & ~bst.met;
		if ( m && Bst_InView( human, ent ) ) {
			Bst_Meet( i, m, "sight" );
		}
	}
}

/*
=================
G_DS_BestiaryKill

G_DS_MetaKill (player_die): a bot the player killed counts for each of its entries.
Bosses count their true death in cnt_boss<n> (META) instead (a phase "death" is no kill).
=================
*/
void G_DS_BestiaryKill( gentity_t *self, gentity_t *attacker ) {
	int		h, m, i;

	if ( !bst.on || !self || !attacker || self == attacker || level.intermissiontime ) {
		return;
	}
	h = G_RL_HumanClient();
	if ( h < 0 || attacker - g_entities != h ) {
		return;
	}
	m = Bst_Entries( self - g_entities );
	if ( G_DS_AffixSpentNow( self - g_entities ) & DSAF_SPLIT ) {
		m |= 1 << BG_DS_BstEntry( DSB_KIND_AFFIX, 2 );		// SPLITTING (bit 2): en-affix cleared it on this death
	}
	if ( !m ) {
		return;
	}
	Bst_Meet( self - g_entities, m, "kill" );
	for ( i = 0 ; i < DSB_NUM ; i++ ) {
		if ( ( m & ( 1 << i ) ) && !( BG_DS_BstKind( i ) == DSB_KIND_EXTRA && BG_DS_BstId( i ) >= DSBX_BOSS ) ) {
			bst.kills[i]++;
		}
	}
	if ( self - g_entities == bst.mbClient ) {
		bst.mbClient = -1;		// the slot may hold another bot later
	}
}

/*
=================
G_DS_BestiaryFloorEnd

G_DS_MetaFloorEnd (RL_Finish, before the run file is saved): this floor's numbers
=================
*/
void G_DS_BestiaryFloorEnd( void ) {
	if ( !bst.on ) {
		return;
	}
	G_RL_RunSet( "bst_fm", va( "%i", bst.met ) );
	G_RL_RunSet( "bst_fk", Bst_Csv( bst.kills, DSB_NUM ) );
	G_RL_RunSet( "bst_rm", va( "%i", bst.runMask | bst.met ) );
	G_LogPrintf( "Bestiary: floor end met %i run %i kills %s\n", bst.met, bst.runMask | bst.met, Bst_Csv( bst.kills, DSB_NUM ) );
	bst.on = qfalse;		// written: nothing after the floor's end counts (the intermission comes a moment later)
}

/*
=================
G_DS_BestiaryCommand

Dev (server console): dsbst, dsbstmeet <entry|name>, dsbstkill <entry|name> [n]
=================
*/
qboolean G_DS_BestiaryCommand( const char *cmd ) {
	char	arg[64];
	int		e, n, i;

	if ( !Q_stricmp( cmd, "dsbst" ) ) {
		G_Printf( "dsbst: on %i known %i run %i met %i mb %i/%i\n  kills %s\n", bst.on, bst.known, bst.runMask, bst.met,
			bst.mbClient, bst.mbKind, Bst_Csv( bst.kills, DSB_NUM ) );
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			if ( g_entities[i].inuse && Bst_Entries( i ) ) {
				G_Printf( "  client %i %s entries %i\n", i, g_entities[i].client->pers.netname, Bst_Entries( i ) );
			}
		}
		G_LogPrintf( "Bestiary: state on %i known %i run %i met %i kills %s\n", bst.on, bst.known, bst.runMask, bst.met,
			Bst_Csv( bst.kills, DSB_NUM ) );
		return qtrue;
	}
	if ( !Q_stricmp( cmd, "dsbstmeet" ) || !Q_stricmp( cmd, "dsbstkill" ) ) {
		trap_Argv( 1, arg, sizeof( arg ) );
		e = BG_DS_BstByName( arg );
		if ( e < 0 || !bst.on ) {
			G_Printf( "%s: unknown entry '%s' or no run\n", cmd, arg );
			return qtrue;
		}
		if ( !Q_stricmp( cmd, "dsbstmeet" ) ) {
			Bst_Meet( -1, 1 << e, "dev" );
		} else {
			trap_Argv( 2, arg, sizeof( arg ) );
			n = arg[0] ? atoi( arg ) : 1;
			Bst_Meet( -1, 1 << e, "dev" );
			bst.kills[e] += n;
			G_LogPrintf( "Bestiary: dev kills %s +%i\n", BG_DS_BstName( e ), n );
		}
		return qtrue;
	}
	return qfalse;
}
