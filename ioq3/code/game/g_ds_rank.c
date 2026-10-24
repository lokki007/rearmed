/*
===========================================================================
REARMED -- round 4 ENEMIES, VETERANS (en-personality, DESCENT, g_dsVeterans 1).

A persistent roster of named bots for the run (run key vet_roster in rearmed_run.txt, gone
with the run; Reset All Progress clears the run file):
	vet_roster Name.xp.rank.met.kills.promo,Name...
Bots earn xp: +2 for every floor they were on (won or lost), +3 for each time they killed the
player, +1 per bot kill (3 a floor at most). Ranks 0..DS_MAX_RANK at 4 / 9 / 15 / 22 / 30 xp,
decided at the floor end. A rank (power-budgeted, small):
	+4 max health, +8 spawn armour, +2% damage dealt per rank; weapons +1 level from rank 3;
	+1 kit card from rank 2, +2 from rank 4.
Up to 2 returning veterans are put on each fight floor (G_DS_RankOrderNames), the strongest
more likely, so you meet the same faces again. A returning bot recognises you ("We meet again",
"Moved up since last time"). The rank shows only at the edges: the arrival callout / loading
roster (CS_DSINTRO \vr\ \vm\, cgame/cg_ds_intro.c), the TAB scoreboard chevrons and a small
shoulder light on the body (cgame/cg_personality.c, from CS_DSENEMY rank).
Never on the act boss, surprise extras (ambushers, ghosts, fodder) or timed boss floors.

Dev (server console): dsvet (list), dsvet <name> <rank>, dsvet clear.
===========================================================================
*/
#include "g_local.h"
#include "bg_ds_personality.h"

#define VET_MAX			32
#define VET_RETURN		2		// returning veterans put on a fight floor at most

typedef struct {
	char	name[24];
	int		xp, rank, met, kills, promo;
	int		floorXp, floorBotKills;		// this floor (applied at the floor end)
} vetEntry_t;

typedef struct {
	qboolean	loaded;
	vetEntry_t	e[VET_MAX];
	int			num;
	int			nextPublish;
	int			arrivedEnter[MAX_CLIENTS];	// pers.enterTime of the client whose arrival was handled
} vetState_t;

static vetState_t	vet;
static vmCvar_t		vetCv;

static const int vetXp[DS_MAX_RANK + 1] = { 0, 4, 9, 15, 22, 30 };

static const char *vetLinesAgain[] = { "We meet again.", "Back for more?", "You again.", "Remember me, %s?" };
static const char *vetLinesPromo[] = { "Moved up since last time.", "I've been practising.", "New stripes. Thanks to you, %s." };
static const char *vetLinesVeteran[] = { "Veteran on the floor.", "Heard about you, %s." };

#define VET_PICK( arr )		( arr[rand() % (int)( sizeof( arr ) / sizeof( arr[0] ) )] )

/*
===============================================================================

ROSTER

===============================================================================
*/

void G_DS_RankInit( void ) {
	trap_Cvar_Register( &vetCv, "g_dsVeterans", "1", CVAR_ARCHIVE );
	memset( &vet, 0, sizeof( vet ) );
}

static qboolean Vet_On( void ) {
	return vetCv.integer && G_RL_Active();
}

static void Vet_Load( void ) {
	const char	*s;
	char		tok[64];
	vetEntry_t	*v;
	int			n, f, val[5];
	char		*p;

	if ( vet.loaded || !G_RL_Active() ) {
		return;
	}
	vet.loaded = qtrue;
	vet.num = 0;
	s = G_RL_RunGet( "vet_roster" );
	while ( s && *s && vet.num < VET_MAX ) {
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
		p = strchr( tok, '.' );
		if ( !p || p == tok ) {
			continue;
		}
		*p++ = 0;
		memset( val, 0, sizeof( val ) );
		for ( f = 0 ; f < 5 && p && *p ; f++ ) {
			val[f] = atoi( p );
			p = strchr( p, '.' );
			if ( p ) {
				p++;
			}
		}
		v = &vet.e[vet.num++];
		memset( v, 0, sizeof( *v ) );
		Q_strncpyz( v->name, tok, sizeof( v->name ) );
		v->xp = val[0];
		v->rank = val[1] < 0 ? 0 : val[1] > DS_MAX_RANK ? DS_MAX_RANK : val[1];
		v->met = val[2];
		v->kills = val[3];
		v->promo = val[4];
	}
}

static void Vet_Save( void ) {
	char	buf[1000];
	int		i;

	buf[0] = 0;
	for ( i = 0 ; i < vet.num ; i++ ) {
		Q_strcat( buf, sizeof( buf ), va( "%s%s.%i.%i.%i.%i.%i", buf[0] ? "," : "", vet.e[i].name, vet.e[i].xp,
			vet.e[i].rank, vet.e[i].met, vet.e[i].kills, vet.e[i].promo ) );
	}
	G_RL_RunSet( "vet_roster", buf );
}

static vetEntry_t *Vet_Find( const char *rawName, qboolean add ) {
	char	name[32];
	int		i, low;

	Vet_Load();
	if ( !vet.loaded || !rawName || !rawName[0] ) {
		return NULL;
	}
	G_EN_CleanName( rawName, name, sizeof( name ) );
	if ( !name[0] || strchr( name, '.' ) || strchr( name, ',' ) ) {
		return NULL;
	}
	for ( i = 0 ; i < vet.num ; i++ ) {
		if ( !Q_stricmp( vet.e[i].name, name ) ) {
			return &vet.e[i];
		}
	}
	if ( !add ) {
		return NULL;
	}
	if ( vet.num < VET_MAX ) {
		i = vet.num++;
	} else {
		// full: the one with the least xp makes room
		low = 0;
		for ( i = 1 ; i < VET_MAX ; i++ ) {
			if ( vet.e[i].xp < vet.e[low].xp ) {
				low = i;
			}
		}
		i = low;
	}
	memset( &vet.e[i], 0, sizeof( vet.e[i] ) );
	Q_strncpyz( vet.e[i].name, name, sizeof( vet.e[i].name ) );
	return &vet.e[i];
}

static int Vet_RankForXp( int xp ) {
	int		r;

	for ( r = DS_MAX_RANK ; r > 0 ; r-- ) {
		if ( xp >= vetXp[r] ) {
			return r;
		}
	}
	return 0;
}

// a bot that can carry a rank: no boss, no surprise extra
static qboolean Vet_Eligible( gentity_t *ent ) {
	int		c;

	if ( !ent || !ent->client || !( ent->r.svFlags & SVF_BOT ) || G_RL_IsPlayer( ent ) ) {
		return qfalse;
	}
	c = ent - g_entities;
	return c != G_DS_BossClient() && !G_DS_SurpriseExtra( c ) && !G_DS_AffixExtra( c ) && G_RL_ClientRole( ent ) != 4;
}

static vetEntry_t *Vet_For( gentity_t *ent ) {
	if ( !Vet_On() || !Vet_Eligible( ent ) ) {
		return NULL;
	}
	return Vet_Find( ent->client->pers.netname, qfalse );
}

/*
===============================================================================

API

===============================================================================
*/

int G_DS_RankFor( gentity_t *ent ) {
	vetEntry_t	*v = Vet_For( ent );

	return v ? v->rank : 0;
}

int G_DS_RankOfName( const char *name ) {
	vetEntry_t	*v;

	if ( !Vet_On() ) {
		return 0;
	}
	v = Vet_Find( name, qfalse );
	return v ? v->rank : 0;
}

// G_RL_InitBots, after the name shuffle: up to 2 returning veterans move to the front
void G_DS_RankOrderNames( const char **names, int *idx, int num, int floor ) {
	int			i, j, k, t, moved = 0, cand[VET_MAX], w[VET_MAX], nc = 0, sum, r;
	unsigned	rng;
	vetEntry_t	*v;

	if ( !Vet_On() || G_DS_IsBossFloor() ) {
		return;
	}
	Vet_Load();
	rng = (unsigned)( atoi( G_RL_RunGet( "seed" ) ) * 31 + floor * 7919 + 17 );
	for ( i = 0 ; i < vet.num ; i++ ) {
		if ( vet.e[i].met <= 0 ) {
			continue;
		}
		for ( j = 0 ; j < num ; j++ ) {
			if ( !Q_stricmp( names[idx[j]], vet.e[i].name ) ) {
				break;
			}
		}
		if ( j < num ) {
			cand[nc] = j;		// position in idx
			w[nc] = 2 + vet.e[i].rank * 2;
			nc++;
		}
	}
	while ( moved < VET_RETURN && nc > 0 ) {
		rng = rng * 1103515245u + 12345u;
		// about half the floors bring a second one back
		if ( moved > 0 && ( ( rng >> 16 ) & 1 ) ) {
			break;
		}
		sum = 0;
		for ( i = 0 ; i < nc ; i++ ) {
			sum += w[i];
		}
		rng = rng * 1103515245u + 12345u;
		r = (int)( ( rng >> 8 ) % (unsigned)sum );
		for ( k = 0 ; k < nc - 1 && r >= w[k] ; k++ ) {
			r -= w[k];
		}
		// swap idx[cand[k]] into position moved (the front)
		t = idx[moved];
		idx[moved] = idx[cand[k]];
		idx[cand[k]] = t;
		for ( i = 0 ; i < nc ; i++ ) {
			if ( cand[i] == moved ) {
				cand[i] = cand[k];		// the name we pushed out now sits where the veteran was
			}
		}
		v = Vet_Find( names[idx[moved]], qfalse );
		G_LogPrintf( "DSVet: floor %i brings back %s (rank %i, met %i)\n", floor, names[idx[moved]], v ? v->rank : 0, v ? v->met : 0 );
		cand[k] = cand[nc - 1];
		w[k] = w[nc - 1];
		nc--;
		moved++;
	}
}

int G_DS_RankKitBonus( const char *name ) {
	int		r = G_DS_RankOfName( name );

	return r >= 4 ? 2 : r >= 2 ? 1 : 0;
}

int G_DS_RankHealthBonus( gentity_t *ent ) {
	return 4 * G_DS_RankFor( ent ) + G_DS_NemesisHealthBonus( ent );
}

int G_DS_RankWeaponBonus( gentity_t *ent ) {
	return ( G_DS_RankFor( ent ) >= 3 ? 1 : 0 ) + G_DS_NemesisWeaponBonus( ent );
}

// damage dealt by a veteran (the nemesis's own scale is G_DS_NemesisDamageScale)
float G_DS_RankDamageScale( gentity_t *targ, gentity_t *attacker ) {
	if ( !attacker || !targ || attacker == targ || !attacker->client ) {
		return 1.0f;
	}
	return 1.0f + 0.02f * G_DS_RankFor( attacker );
}

/*
=================
G_DS_RankSpawn

End of G_RL_SpawnLoadout (every DESCENT spawn): rank armour, the nemesis's level kit,
a returning veteran's line on its first spawn of the floor
=================
*/
void G_DS_RankSpawn( gentity_t *ent ) {
	vetEntry_t	*v;
	int			armor;

	if ( !ent || !ent->client || !( ent->r.svFlags & SVF_BOT ) ) {
		return;
	}
	G_DS_NemesisSpawn( ent );		// level armour + affixes (g_ds_nemesis.c)
	v = Vet_For( ent );
	if ( !v ) {
		return;
	}
	armor = 8 * v->rank;
	if ( armor > 0 ) {
		ent->client->ps.stats[STAT_ARMOR] += armor;
	}
}

/*
=================
Vet_Arrive

A bot's first moment on the floor, once you are in and the versus intro is over (G_DS_RankFrame):
a returning veteran recognises you
=================
*/
static void Vet_Arrive( gentity_t *ent, vetEntry_t *v, int h ) {
	int		c = ent - g_entities;

	if ( c == G_DS_NemesisClient() || G_DS_NemesisIs( ent ) ) {
		return;		// the nemesis announces itself
	}
	if ( G_BM_OwnsArrival( ent ) ) {
		// en-memory greets bots with a lifetime history (g_ds_botmem.c): only the promotion callout here
		if ( v->promo && v->met > 0 && h >= 0 ) {
			trap_SendServerCommand( h, va( "prc %i %i %i 0 \"\"", PRC_PROMOTED, c, v->rank ) );
		}
		v->promo = 0;
	} else if ( v->promo && v->met > 0 ) {
		G_PR_SayTo( c, 1, VET_PICK( vetLinesPromo ), PRB_AGAIN, h );
		if ( h >= 0 ) {
			trap_SendServerCommand( h, va( "prc %i %i %i 0 \"\"", PRC_PROMOTED, c, v->rank ) );
		}
		v->promo = 0;
	} else if ( v->met > 0 && random() < 0.7f ) {
		G_PR_SayTo( c, 1, VET_PICK( vetLinesAgain ), PRB_AGAIN, h );
	} else if ( v->rank >= 3 && random() < 0.5f ) {
		G_PR_SayTo( c, 0, VET_PICK( vetLinesVeteran ), -1, h );
	}
	G_LogPrintf( "DSVet: %s arrives, rank %i met %i xp %i\n", v->name, v->rank, v->met, v->xp );
}

// G_PR_Death (a real kill): xp for the killer
void G_DS_RankKill( gentity_t *self, gentity_t *attacker ) {
	vetEntry_t	*v;

	if ( !Vet_On() || !Vet_Eligible( attacker ) ) {
		return;
	}
	v = Vet_Find( attacker->client->pers.netname, qtrue );
	if ( !v ) {
		return;
	}
	if ( G_RL_IsPlayer( self ) ) {
		v->floorXp += 3;
		v->kills++;
	} else if ( v->floorBotKills < 3 ) {
		v->floorBotKills++;
		v->floorXp += 1;
	}
}

// G_PR_Frame: the ranks onto g_enemy[] (CS_DSENEMY) twice a second
void G_DS_RankFrame( void ) {
	int			i, r, h;
	gentity_t	*ent;
	gEnemy_t	*e;
	vetEntry_t	*v;
	qboolean	humanIn;

	if ( level.time < vet.nextPublish ) {
		return;
	}
	vet.nextPublish = level.time + 500;
	// arrivals wait until you are in the game and the versus intro had its 4 s
	h = Vet_On() ? G_RL_HumanClient() : -1;
	humanIn = h >= 0 && g_entities[h].client && g_entities[h].client->pers.connected == CON_CONNECTED
		&& level.time - g_entities[h].client->pers.enterTime > 4000 && !level.intermissiontime;
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		ent = &g_entities[i];
		e = G_EN( i );
		if ( !e ) {
			continue;
		}
		r = ent->inuse && ent->client && ent->client->pers.connected == CON_CONNECTED ? G_DS_RankFor( ent ) : 0;
		if ( e->rank != r ) {
			e->rank = r;
			G_EN_MarkDirty();
		}
		if ( humanIn && ent->inuse && ent->client && ent->health > 0 && vet.arrivedEnter[i] != ent->client->pers.enterTime + 1
			&& ( v = Vet_For( ent ) ) != NULL ) {
			vet.arrivedEnter[i] = ent->client->pers.enterTime + 1;
			Vet_Arrive( ent, v, h );
		}
	}
}

/*
=================
G_DS_RankFloorEnd

RL_Finish, before the run file is saved: the bots on this floor gain xp, ranks are decided
=================
*/
void G_DS_RankFloorEnd( qboolean won ) {
	int			i, old;
	gentity_t	*ent;
	vetEntry_t	*v;
	qboolean	human = G_RL_HumanClient() >= 0;

	if ( !Vet_On() ) {
		return;
	}
	Vet_Load();
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		ent = &g_entities[i];
		if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED || !Vet_Eligible( ent ) ) {
			continue;
		}
		v = Vet_Find( ent->client->pers.netname, qtrue );
		if ( !v ) {
			continue;
		}
		v->floorXp += 2;
		if ( human ) {
			v->met++;
		}
	}
	for ( i = 0 ; i < vet.num ; i++ ) {
		v = &vet.e[i];
		if ( !v->floorXp ) {
			continue;
		}
		old = v->rank;
		v->xp += v->floorXp;
		v->rank = Vet_RankForXp( v->xp );
		if ( v->rank > old ) {
			v->promo = 1;
		}
		G_LogPrintf( "DSVet: %s xp +%i = %i, rank %i -> %i, met %i, kills %i (floor %s)\n", v->name, v->floorXp,
			v->xp, old, v->rank, v->met, v->kills, won ? "won" : "lost" );
		v->floorXp = 0;
		v->floorBotKills = 0;
	}
	Vet_Save();
}

// G_DS_IntroPublish: \vr\ ranks and \vm\ met flags of the floor's planned bots (plan order)
void G_DS_RankIntro( char *s, int size ) {
	char		vr[64], vm[64];
	const char	*nm;
	vetEntry_t	*v;
	int			i, n, role, any = 0;

	// the nemesis's level and title (cg_ds_intro.c: its note on the roster)
	if ( G_DS_NemesisLevel() > 0 ) {
		Info_SetValueForKey( s, "nl", va( "%i", G_DS_NemesisLevel() ) );
		if ( G_DS_NemesisTitle()[0] ) {
			Info_SetValueForKey( s, "nt", G_DS_NemesisTitle() );
		}
	}
	if ( !Vet_On() ) {
		return;
	}
	vr[0] = vm[0] = 0;
	n = G_RL_PlanNum();
	for ( i = 0 ; i < n && i < 12 ; i++ ) {
		nm = G_RL_PlanName( i, &role );
		v = nm && role != 4 ? Vet_Find( nm, qfalse ) : NULL;
		Q_strcat( vr, sizeof( vr ), va( "%s%i", i ? "," : "", v ? v->rank : 0 ) );
		Q_strcat( vm, sizeof( vm ), va( "%s%i", i ? "," : "", !v || v->met <= 0 ? 0 : v->promo ? 2 : 1 ) );
		if ( v && ( v->rank || v->met ) ) {
			any = 1;
		}
	}
	if ( any ) {
		Info_SetValueForKey( s, "vr", vr );
		Info_SetValueForKey( s, "vm", vm );
	}
}

qboolean G_DS_RankCommand( const char *cmd ) {
	char		a1[32], a2[16];
	vetEntry_t	*v;
	int			i;

	if ( Q_stricmp( cmd, "dsvet" ) ) {
		return qfalse;
	}
	if ( !G_RL_Active() ) {
		G_Printf( "dsvet: no descent\n" );
		return qtrue;
	}
	Vet_Load();
	trap_Argv( 1, a1, sizeof( a1 ) );
	trap_Argv( 2, a2, sizeof( a2 ) );
	if ( !Q_stricmp( a1, "clear" ) ) {
		vet.num = 0;
		Vet_Save();
	} else if ( a1[0] ) {
		v = Vet_Find( a1, qtrue );
		if ( v ) {
			v->rank = atoi( a2 ) < 0 ? 0 : atoi( a2 ) > DS_MAX_RANK ? DS_MAX_RANK : atoi( a2 );
			v->xp = vetXp[v->rank];
			if ( !v->met ) {
				v->met = 1;
			}
			Vet_Save();
		}
		vet.nextPublish = 0;
	}
	for ( i = 0 ; i < vet.num ; i++ ) {
		G_Printf( "  %s rank %i xp %i met %i kills %i%s\n", vet.e[i].name, vet.e[i].rank, vet.e[i].xp, vet.e[i].met,
			vet.e[i].kills, vet.e[i].promo ? " (promoted)" : "" );
	}
	G_LogPrintf( "DSVet: roster '%s'\n", G_RL_RunGet( "vet_roster" ) );
	return qtrue;
}
