/*
===========================================================================
REARMED round 4 (en-brains) -- g_thief.c: weapon thieves, all modes (g_botWeaponThief).

When the human dies, the weapon they drop is a prize: nearby bots race for it (ai_tactics.c
BotTacticsGoal), the one that grabs it switches to it and uses it (BotTacticsChooseWeapon).
DESCENT: the thief uses it at the human's weapon level, capped at its own level + 2 (power
scaling); weapons don't leave the human's kit there, so the drop is a copy.
The thief wears DSEF_THIEF (a faint gold glint at its gun hand, cgame/cg_thief.c) and the
human gets an edge callout "<NAME> TOOK YOUR RAILGUN". Killing the thief = RECLAIMED: callout +
sound, DESCENT pays a few skulls and a gem spray by the weapon's level, other modes style
points; the stolen gun drops back.
Server command to the human: thc <kind 1 took / 2 reclaimed / 3 lost> <thief> <weapon> <skulls>
Log: "Thief: ..." / "Reclaim: ..." lines in games.log.
===========================================================================
*/
#include "g_local.h"

#define TH_MAX_DROPS	8
#define TH_DROP_LIFE	30000

typedef struct {
	int		ent;			// the dropped item
	int		weapon;
	int		owner;			// the human it came from
	int		level;			// DESCENT weapon level of the owner's gun (0 elsewhere)
	int		time;
} thDrop_t;

typedef struct {
	int		weapon;			// 0 = not a thief
	int		owner;
	int		level;
	int		time;
} thThief_t;

static thDrop_t		thDrops[TH_MAX_DROPS];
static thThief_t	thThief[MAX_CLIENTS];
static vmCvar_t		g_botWeaponThief;
static vmCvar_t		bt_thiefTarget;
static int			thCvars;
static int			thSteals, thReclaims, thPaidSkulls, thPaidGems;

static void TH_Cvars( void ) {
	if ( !thCvars ) {
		thCvars = 1;
		trap_Cvar_Register( &g_botWeaponThief, "g_botWeaponThief", "1", CVAR_ARCHIVE );
		trap_Cvar_Register( &bt_thiefTarget, "bt_thiefTarget", "-1", 0 );
	}
	trap_Cvar_Update( &g_botWeaponThief );
	trap_Cvar_Update( &bt_thiefTarget );
}

static qboolean TH_On( void ) {
	TH_Cvars();
	if ( !g_botWeaponThief.integer ) {
		return qfalse;
	}
	// fixed-weapon modes: nothing to steal
	return level.rmMode != RM_MODE_GUNGAME && level.rmMode != RM_MODE_INSTAGIB && level.rmMode != RM_MODE_OITC
		&& level.mutator != MUT_WEAPON_ROULETTE;
}

// is this client the one whose weapon gets stolen? (the human; bt_thiefTarget for bot-only tests)
static qboolean TH_Victim( gentity_t *ent ) {
	int		n = ent - g_entities;

	if ( !ent->client || n < 0 || n >= MAX_CLIENTS ) {
		return qfalse;
	}
	if ( bt_thiefTarget.integer >= 0 ) {
		return n == bt_thiefTarget.integer;
	}
	return !( ent->r.svFlags & SVF_BOT );
}

static const char *TH_WeaponName( int w ) {
	gitem_t	*it = BG_FindItemForWeapon( w );

	return it ? it->pickup_name : "weapon";
}

void G_TH_Init( void ) {
	memset( thDrops, 0, sizeof( thDrops ) );
	memset( thThief, 0, sizeof( thThief ) );
	thSteals = thReclaims = thPaidSkulls = thPaidGems = 0;
	TH_Cvars();
}

static thDrop_t *TH_DropFor( gentity_t *item ) {
	int		i, n = item - g_entities;

	for ( i = 0 ; i < TH_MAX_DROPS ; i++ ) {
		if ( thDrops[i].ent == n && thDrops[i].weapon ) {
			return &thDrops[i];
		}
	}
	return NULL;
}

int G_TH_PrizeOwner( gentity_t *item ) {
	thDrop_t	*d;

	if ( !item || !item->inuse ) {
		return -1;
	}
	d = TH_DropFor( item );
	return d ? d->owner : -1;
}

int G_TH_Thief( int botClient ) {
	gentity_t	*ent;

	if ( botClient < 0 || botClient >= MAX_CLIENTS || !thThief[botClient].weapon ) {
		return 0;
	}
	ent = &g_entities[botClient];
	if ( !ent->client || !( ent->client->ps.stats[STAT_WEAPONS] & ( 1 << thThief[botClient].weapon ) )
		|| !ent->client->ps.ammo[thThief[botClient].weapon] ) {
		return 0;
	}
	return thThief[botClient].weapon;
}

static void TH_ClearThief( int c ) {
	if ( thThief[c].weapon ) {
		thThief[c].weapon = 0;
		G_EN_SetFlag( c, DSEF_THIEF, qfalse );
	}
}

// the item a death just dropped for this weapon (TossClientItems ran this frame)
static gentity_t *TH_FreshDrop( gentity_t *self, int weapon ) {
	gentity_t	*e;
	int			i;

	for ( i = MAX_CLIENTS, e = &g_entities[MAX_CLIENTS] ; i < level.num_entities ; i++, e++ ) {
		if ( !e->inuse || !e->item || !( e->flags & FL_DROPPED_ITEM ) || e->item->giType != IT_WEAPON
			|| e->item->giTag != weapon || e->s.pos.trTime != level.time ) {
			continue;
		}
		if ( Distance( e->s.pos.trBase, self->s.pos.trBase ) < 64 ) {
			return e;
		}
	}
	return NULL;
}

static void TH_AddDrop( gentity_t *item, int weapon, int owner, int lvl ) {
	int			i, oldest = 0;
	thDrop_t	*d;

	for ( i = 0 ; i < TH_MAX_DROPS ; i++ ) {
		if ( !thDrops[i].weapon ) {
			oldest = i;
			break;
		}
		if ( thDrops[i].time < thDrops[oldest].time ) {
			oldest = i;
		}
	}
	d = &thDrops[oldest];
	d->ent = item - g_entities;
	d->weapon = weapon;
	d->owner = owner;
	d->level = lvl;
	d->time = level.time;
}

/*
player_die, after TossClientItems: the human's gun becomes a prize; a thief's death reclaims it
*/
void G_TH_PlayerDie( gentity_t *self, gentity_t *attacker, int mod ) {
	thThief_t	*t;
	gentity_t	*drop, *owner;
	gitem_t		*it;
	int			c = self - g_entities, w, sk, gems;

	if ( c < 0 || c >= MAX_CLIENTS || !self->client ) {
		return;
	}
	// a thief dies: by the owner = RECLAIMED, otherwise just lost
	t = &thThief[c];
	if ( t->weapon ) {
		w = t->weapon;
		owner = ( t->owner >= 0 && t->owner < MAX_CLIENTS ) ? &g_entities[t->owner] : NULL;
		// the gun drops back (TossClientItems dropped it if it was in hand)
		drop = TH_FreshDrop( self, w );
		if ( !drop && TH_On() && ( it = BG_FindItemForWeapon( w ) ) != NULL ) {
			drop = Drop_Item( self, it, 0 );
		}
		if ( owner && owner->client && attacker == owner ) {
			thReclaims++;
			sk = gems = 0;
			if ( G_RL_Active() ) {
				// a one-off payout scaled by the weapon's level: 1-3 skulls, a gem spray
				sk = G_DS_Skulls( 1 + t->level / 3 );
				G_RL_AddSkulls( sk );
				gems = 6 + 3 * ( t->level > 0 ? t->level : 1 );
				G_DS_SpawnGems( self->r.currentOrigin, gems, w );
				thPaidSkulls += sk;
				thPaidGems += gems;
			} else {
				G_StyleAdd( owner, 80, "RECLAIMED" );
			}
			trap_SendServerCommand( t->owner, va( "thc 2 %i %i %i", c, w, sk ) );
			G_LogPrintf( "Reclaim: %i took back %s from %i (level %i, skulls %i, gem xp %i)\n", t->owner, TH_WeaponName( w ),
				c, t->level, sk, gems );
		} else {
			G_LogPrintf( "ThiefDown: %i (had %s of %i) killed by %i\n", c, TH_WeaponName( w ), t->owner,
				attacker && attacker->client ? (int)( attacker - g_entities ) : -1 );
		}
		TH_ClearThief( c );
	}

	// the human dies: the gun on the floor is a prize
	if ( !TH_On() || !TH_Victim( self ) ) {
		return;
	}
	w = self->s.weapon;
	if ( w <= WP_MACHINEGUN || w == WP_GRAPPLING_HOOK || w >= WP_NUM_WEAPONS ) {
		return;		// gauntlet / machine gun: not worth stealing
	}
	drop = TH_FreshDrop( self, w );
	if ( !drop ) {
		// DESCENT (or anywhere drops are off): a copy, the human keeps their kit
		it = BG_FindItemForWeapon( w );
		if ( !it ) {
			return;
		}
		drop = Drop_Item( self, it, 0 );
	}
	drop->nextthink = level.time + TH_DROP_LIFE;
	TH_AddDrop( drop, w, c, G_RL_Active() ? G_DS_WeaponLevel( self, w ) : 0 );
	G_LogPrintf( "ThiefDrop: %i dropped %s (level %i)\n", c, TH_WeaponName( w ), G_RL_Active() ? G_DS_WeaponLevel( self, w ) : 0 );
}

/*
Touch_Item, before the normal pickup: a bot grabbing a prize becomes its thief
*/
qboolean G_TH_Pickup( gentity_t *item, gentity_t *other ) {
	thDrop_t	*d;
	thThief_t	*t;
	int			c, cap, lvl, have;

	if ( !item || !other || !other->client ) {
		return qfalse;
	}
	d = TH_DropFor( item );
	if ( !d ) {
		return qfalse;
	}
	c = other - g_entities;
	if ( c == d->owner || !( other->r.svFlags & SVF_BOT ) || !TH_On() ) {
		d->weapon = 0;		// the owner took it back (or a human friend): no thief
		return qfalse;
	}
	t = &thThief[c];
	t->weapon = d->weapon;
	t->owner = d->owner;
	t->level = d->level;
	t->time = level.time;
	d->weapon = 0;
	// DESCENT: the human's level, capped by the bot's own power (its level + 2)
	if ( G_RL_Active() && d->level > 0 ) {
		have = G_DS_WeaponLevel( other, t->weapon );
		cap = have + 2;
		lvl = d->level < cap ? d->level : cap;
		if ( lvl > have ) {
			G_DS_AddWeaponLevel( other, t->weapon, lvl - have );
		}
		t->level = lvl > have ? lvl : have;
	}
	G_EN_SetFlag( c, DSEF_THIEF, qtrue );
	thSteals++;
	if ( t->owner >= 0 && t->owner < MAX_CLIENTS && g_entities[t->owner].inuse ) {
		trap_SendServerCommand( t->owner, va( "thc 1 %i %i 0", c, t->weapon ) );
	}
	G_LogPrintf( "Thief: %i took %s from %i (level %i)\n", c, TH_WeaponName( t->weapon ), t->owner, t->level );
	return qfalse;		// the normal pickup goes on (weapon + ammo)
}

void G_TH_Frame( void ) {
	int			i;
	gentity_t	*e;

	for ( i = 0 ; i < TH_MAX_DROPS ; i++ ) {
		if ( !thDrops[i].weapon ) {
			continue;
		}
		e = &g_entities[thDrops[i].ent];
		if ( !e->inuse || !e->item || e->item->giType != IT_WEAPON || e->item->giTag != thDrops[i].weapon
			|| level.time - thDrops[i].time > TH_DROP_LIFE + 1000 ) {
			thDrops[i].weapon = 0;		// gone (expired, or the slot was reused)
		}
	}
	for ( i = 0 ; i < MAX_CLIENTS ; i++ ) {
		if ( thThief[i].weapon && ( !g_entities[i].inuse || !g_entities[i].client ) ) {
			TH_ClearThief( i );
		}
	}
}

qboolean G_TH_Command( const char *cmd ) {
	int		i;

	char		arg[MAX_TOKEN_CHARS];
	gentity_t	*bot, *victim;
	int			c, v, w;

	if ( Q_stricmp( cmd, "btthief" ) ) {
		return qfalse;
	}
	// dev: btthief give <bot> (make it the thief of the human's gun now) / btthief reclaimfx <bot>
	trap_Argv( 1, arg, sizeof( arg ) );
	if ( !Q_stricmp( arg, "give" ) || !Q_stricmp( arg, "reclaimfx" ) || !Q_stricmp( arg, "killby" ) ) {
		trap_Argv( 2, arg, sizeof( arg ) );
		c = atoi( arg );
		v = -1;
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			if ( g_entities[i].inuse && g_entities[i].client && TH_Victim( &g_entities[i] ) ) {
				v = i;
				break;
			}
		}
		if ( c < 0 || c >= MAX_CLIENTS || !g_entities[c].inuse || !g_entities[c].client || v < 0 ) {
			G_Printf( "btthief give|reclaimfx <bot client>: no such bot / no human\n" );
			return qtrue;
		}
		bot = &g_entities[c];
		victim = &g_entities[v];
		w = victim->client->ps.weapon > WP_MACHINEGUN ? victim->client->ps.weapon : WP_RAILGUN;
		trap_Argv( 1, arg, sizeof( arg ) );
		if ( !Q_stricmp( arg, "killby" ) ) {
			// the real reclaim path: the human kills the thief
			G_Damage( bot, victim, victim, NULL, NULL, 2000, DAMAGE_NO_PROTECTION, MOD_RAILGUN );
		} else if ( !Q_stricmp( arg, "give" ) ) {
			thThief[c].weapon = w;
			thThief[c].owner = v;
			thThief[c].level = G_RL_Active() ? G_DS_WeaponLevel( victim, w ) : 0;
			thThief[c].time = level.time;
			bot->client->ps.stats[STAT_WEAPONS] |= 1 << w;
			if ( bot->client->ps.ammo[w] < 20 ) {
				bot->client->ps.ammo[w] = 20;
			}
			G_EN_SetFlag( c, DSEF_THIEF, qtrue );
			trap_SendServerCommand( v, va( "thc 1 %i %i 0", c, w ) );
			G_LogPrintf( "Thief: %i took %s from %i (dev)\n", c, TH_WeaponName( w ), v );
		} else {
			trap_SendServerCommand( v, va( "thc 2 %i %i %i", c, thThief[c].weapon ? thThief[c].weapon : w, 2 ) );
		}
		return qtrue;
	}
	G_Printf( "thief: on %i steals %i reclaims %i paid skulls %i gem xp %i\n", TH_On(), thSteals, thReclaims, thPaidSkulls, thPaidGems );
	G_LogPrintf( "BtThief: steals %i reclaims %i skulls %i gems %i\n", thSteals, thReclaims, thPaidSkulls, thPaidGems );
	for ( i = 0 ; i < MAX_CLIENTS ; i++ ) {
		if ( thThief[i].weapon ) {
			G_Printf( "  %i holds %s of %i (level %i)\n", i, TH_WeaponName( thThief[i].weapon ), thThief[i].owner, thThief[i].level );
		}
	}
	for ( i = 0 ; i < TH_MAX_DROPS ; i++ ) {
		if ( thDrops[i].weapon ) {
			G_Printf( "  drop %i: %s of %i\n", thDrops[i].ent, TH_WeaponName( thDrops[i].weapon ), thDrops[i].owner );
		}
	}
	return qtrue;
}
