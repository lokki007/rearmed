"""ds-surprise: hooks in the shared game files (one-shot; resume-safe via patch.resume semantics:
each edit is skipped when its new text is already there)."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from patch import sub

G = pathlib.Path(__file__).resolve().parent.parent.parent / 'ioq3' / 'code' / 'game'


def edit(path, old, new):
    s = open(path, newline='').read()
    probe = new.replace('\r\n', '\n').split('\n')
    marker = [l for l in probe if 'urprise' in l or 'rlExtra' in l or 'G_DS_BankChest' in l or 'G_DS_SetWeaponLevels' in l]
    if marker and marker[0].strip() in s.replace('\r\n', '\n'):
        print('skip (present):', path.name, marker[0].strip()[:60])
        return
    sub(str(path), old, new)
    print('ok:', path.name, (marker[0].strip() if marker else new.strip())[:60])


R = G / 'g_roguelike.c'
edit(R, '''static rlPlan_t		rlPlan[RL_MAX_BOTS];
static int			rlNumPlan;
''', '''static rlPlan_t		rlPlan[RL_MAX_BOTS];
static int			rlNumPlan;
#define RL_MAX_EXTRA	12		// ds-surprise: bots that join mid-floor (ambush, ghost, fodder)
static rlPlan_t		rlExtra[RL_MAX_EXTRA];
static int			rlNumExtra;
''')
edit(R, '''	G_DS_MetaInit();		// META: character, upgrades, stats
''', '''	G_DS_MetaInit();		// META: character, upgrades, stats
	G_DS_SurpriseInit();	// ds-surprise: ambush / event / ghost plan, the floor modifier, the ghost track
''')
edit(R, '''	G_PreloadPublish();
''', '''	rlNumExtra = 0;
	G_DS_SurprisePreload();		// ds-surprise: the models of the floor's planned ambusher / ghost / fodder
	G_PreloadPublish();
''')
edit(R, '''		if ( i < rlNumPlan ) {
			*k = rlPlan[i].kit;
		} else {''', '''		if ( i >= rlNumPlan ) {
			// ds-surprise: a bot that joined mid-floor with a planned kit (the latest by that name)
			for ( i = rlNumExtra - 1 ; i >= 0 ; i-- ) {
				if ( !Q_stricmp( name, rlExtra[i].name ) ) {
					break;
				}
			}
			if ( i >= 0 ) {
				*k = rlExtra[i].kit;
				k->assigned = qtrue;
				rlKitsDirty = 1;
				return k;
			}
			i = rlNumPlan;
		}
		if ( i < rlNumPlan ) {
			*k = rlPlan[i].kit;
		} else {''')
edit(R, '''	ps->speed *= speed * G_DS_SpeedScale( ent );		// ACTS: boss frenzy
''', '''	ps->speed *= speed * G_DS_SpeedScale( ent ) * G_DS_SurpriseSpeedScale( ent );		// ACTS: boss frenzy; ds-surprise: mini-boss, FERAL
''')
edit(R, '''	return s * ls * G_DS_ActsDamageScale( targ, attacker );		// ACTS: the boss's damage side, the nemesis
''', '''	return s * ls * G_DS_ActsDamageScale( targ, attacker ) * G_DS_SurpriseDamageScale( targ, attacker );		// ACTS: the boss's damage side, the nemesis; ds-surprise
''')
edit(R, '''	default:
		b += 10 * rl.nodeTier;
		break;
	}
	return b;
}
''', '''	default:
		b += 10 * rl.nodeTier;
		break;
	}
	b += G_DS_SurpriseHealthBonus( ent, b );		// ds-surprise: GLASS, the mini-boss's pool, weak fodder
	return b;
}
''')
edit(R, '''	G_DS_ActsPlayerSpawn( ent );		// ACTS: boss floors, the player's respawn spot, guns and grace
''', '''	G_DS_ActsPlayerSpawn( ent );		// ACTS: boss floors, the player's respawn spot, guns and grace
	G_DS_SurpriseSpawn( ent );			// ds-surprise: an ambusher / ghost / fodder lands on its warned spot; ARMOURED
''')
edit(R, '''		G_DS_NemesisDeath( self, attacker );		// ACTS: the nemesis (kills of the player, revenge)
''', '''		G_DS_NemesisDeath( self, attacker );		// ACTS: the nemesis (kills of the player, revenge)
		G_DS_SurpriseDeath( self, attacker );		// ds-surprise: mini-boss / ghost / bounty rewards, one-life extras
''')
edit(R, '''		n = 5;		// FIRSTPICK: the first offer of every act
	}
''', '''		n = 5;		// FIRSTPICK: the first offer of every act
	}
	n += G_DS_SurpriseOfferBonus();		// ds-surprise: FERAL +1 card
	if ( n > RL_MAX_OFFER ) {
		n = RL_MAX_OFFER;
	}
''')
edit(R, '''	G_DS_FrenzyFloorEnd();		// FRENZY: cash the chain out (weapon xp before the run is saved), end a spike
''', '''	G_DS_FrenzyFloorEnd();		// FRENZY: cash the chain out (weapon xp before the run is saved), end a spike
	G_DS_SurpriseFloorEnd( won );		// ds-surprise: BLOODLUST's chest is banked before the chests open
''')
edit(R, '''		if ( i == h || cl->pers.connected != CON_CONNECTED || cl->sess.sessionTeam == TEAM_SPECTATOR ) {
''', '''		if ( i == h || cl->pers.connected != CON_CONNECTED || cl->sess.sessionTeam == TEAM_SPECTATOR || G_DS_SurpriseExtra( i ) ) {
''')
edit(R, '''	G_DS_MetaFrame();		// META: time held per weapon
''', '''	G_DS_MetaFrame();		// META: time held per weapon
	G_DS_SurpriseFrame();	// ds-surprise: ambushes, wild events, the ghost
''')
edit(R, '''	if ( G_DS_LootConsoleCommand( cmd ) ) {''', '''	if ( G_DS_SurpriseCommand( cmd ) ) {		// ds-surprise dev: dsambush / dsevent / dsghost / dsmod / dssurprise
		return qtrue;
	}
	if ( G_DS_LootConsoleCommand( cmd ) ) {''')
edit(R, '''// ds-intro (g_ds_intro.c): this floor's planned bots. Name, kit role, cards of one kind (-1 all)
''', '''/*
=================
G_RL_AddExtraBot

ds-surprise: a bot joins mid-floor (an ambusher, the ghost of a past run, uprising fodder) with
a kit planned here: the given cards and passive levels, or a regular kit of numCards (cards NULL).
A name used before gets the new kit (fodder comes back under the same names).
=================
*/
void G_RL_AddExtraBot( const char *botFile, const char *netName, float skill, int role, const int *cards, int numCards, const int *passives ) {
	rlPlan_t	*p = NULL;
	int			i, j;

	if ( !rl.active || !botFile || !netName ) {
		return;
	}
	for ( i = 0 ; i < rlNumExtra ; i++ ) {
		if ( !Q_stricmp( rlExtra[i].name, netName ) ) {
			p = &rlExtra[i];
			break;
		}
	}
	if ( !p ) {
		if ( rlNumExtra >= RL_MAX_EXTRA ) {
			// full: the oldest planned one makes room
			memmove( &rlExtra[0], &rlExtra[1], sizeof( rlExtra[0] ) * ( RL_MAX_EXTRA - 1 ) );
			rlNumExtra = RL_MAX_EXTRA - 1;
		}
		p = &rlExtra[rlNumExtra++];
	}
	memset( p, 0, sizeof( *p ) );
	Q_strncpyz( p->name, netName, sizeof( p->name ) );
	p->role = role;
	p->skill = skill < 1 ? 1 : skill > 5 ? 5 : skill;
	if ( cards ) {
		RL_KitClear( &p->kit, role );
		for ( i = 0 ; i < numCards ; i++ ) {
			if ( cards[i] >= 0 && cards[i] < RLC_PASSIVE_FIRST ) {
				RL_KitAdd( &p->kit, cards[i] );
			}
		}
		for ( i = 0 ; passives && i < RLP_NUM ; i++ ) {
			for ( j = 0 ; j < passives[i] && j < DS_MAX_PLEVEL ; j++ ) {
				RL_KitAdd( &p->kit, RLC_PASSIVE_FIRST + i );
			}
		}
	} else {
		RL_BuildBotKit( &p->kit, role, numCards, RL_Seed( rl.nodeFloor, 9, rlNumExtra + level.time ) );
	}
	trap_SendConsoleCommand( EXEC_APPEND, va( "addbot %s %.2f free 0 \\"%s\\"\\n", botFile, p->skill, netName ) );
	G_LogPrintf( "Descent: extra bot %s (%s) role %i skill %.2f, %i cards\\n", netName, botFile, role, p->skill, p->kit.num );
}

// ds-intro (g_ds_intro.c): this floor's planned bots. Name, kit role, cards of one kind (-1 all)
''')

# player_die: killing an extra is no frag
edit(G / 'g_combat.c', '''			if ( !G_RearmHandlesScore() ) {
				AddScore( attacker, self->r.currentOrigin, 1 );
			}''', '''			if ( !G_RearmHandlesScore() && !G_DS_SurpriseNoScore( self ) ) {		// ds-surprise: ambushers / ghosts / fodder are no frag
				AddScore( attacker, self->r.currentOrigin, 1 );
			}''')

# nemesis: an extra never takes the title
edit(G / 'g_ds_nemesis.c', '''		Q_strncpyz( name, attacker->client->pers.netname, sizeof( name ) );
		Q_CleanStr( name );
		if ( !name[0] ) {
			return;
		}''', '''		Q_strncpyz( name, attacker->client->pers.netname, sizeof( name ) );
		Q_CleanStr( name );
		if ( !name[0] || G_DS_SurpriseExtra( attacker - g_entities ) ) {
			return;		// (ds-surprise: an ambusher / ghost / fodder never becomes the nemesis)
		}''')

# loot: gem xp (BLACKOUT), gem amount (SWARM), a banked chest
L = G / 'g_ds_loot.c'
edit(L, '''	if ( !G_RL_Active() || xp <= 0 ) {
		return;
	}
	VectorCopy( origin, org );
	org[2] += 16;
	while ( xp > 0 && n < 24 ) {''', '''	if ( !G_RL_Active() || xp <= 0 ) {
		return;
	}
	xp = G_DS_SurpriseGemAmount( xp );		// ds-surprise: SWARM
	VectorCopy( origin, org );
	org[2] += 16;
	while ( xp > 0 && n < 24 ) {''')
edit(L, '''static void Loot_BankChest( int quality ) {
	if ( lootSt.numChests < DS_MAX_CHESTS ) {
		lootSt.chests[lootSt.numChests++] = quality;
	}
	lootSt.dirty = qtrue;
	G_LogPrintf( "Loot: chest %i banked (%i)\\n", quality, lootSt.numChests );
}
''', '''static void Loot_BankChest( int quality ) {
	if ( lootSt.numChests < DS_MAX_CHESTS ) {
		lootSt.chests[lootSt.numChests++] = quality;
	}
	lootSt.dirty = qtrue;
	G_LogPrintf( "Loot: chest %i banked (%i)\\n", quality, lootSt.numChests );
}

// ds-surprise: a chest straight into the bank (BLOODLUST's extra chest at the floor end)
void G_DS_BankChest( int quality ) {
	if ( G_RL_Active() ) {
		Loot_BankChest( quality < 0 ? 0 : quality > 2 ? 2 : quality );
	}
}
''')

# arsenal: a bot's exact weapon levels (the ghost's past kit)
edit(G / 'g_ds_arsenal.c', '''void G_DS_ArsenalClientBegin( gentity_t *ent ) {''', '''/*
=================
G_DS_SetWeaponLevels

ds-surprise: a bot gets exact weapon levels (levels[w] > 0; 0 keeps its own) and evolutions
(the ghost of a past run carries the past kit)
=================
*/
void G_DS_SetWeaponLevels( gentity_t *ent, const int *levels, int evoMask ) {
	arsGrowth_t	*g = Ars_Growth( ent );
	int			w;

	if ( !g || !levels || G_RL_IsPlayer( ent ) ) {
		return;
	}
	for ( w = 1 ; w < DS_NUM_WEAPONS ; w++ ) {
		if ( DS_WEAPON_LEVELS( w ) && levels[w] > 0 ) {
			g->level[w] = levels[w] > DS_MAX_WLEVEL ? DS_MAX_WLEVEL : levels[w];
			g->xp[w] = 0;
		}
	}
	g->evo = evoMask & ( ( 1 << EVO_NUM ) - 1 );
	G_LogPrintf( "DSBot: client %i set levels (ghost) evo 0x%x\\n", (int)( ent - g_entities ), g->evo );
}

void G_DS_ArsenalClientBegin( gentity_t *ent ) {''')
print('done')
