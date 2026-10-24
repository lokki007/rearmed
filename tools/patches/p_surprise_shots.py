"""ds-surprise: g_dsSurShots (tests): the game takes screenshots at the key moments, after pointing the
player at the surprise (Sur_DevLook). One-shot."""
import pathlib
p = pathlib.Path(__file__).resolve().parent.parent.parent / 'ioq3' / 'code' / 'game' / 'g_ds_surprise.c'
s = open(p, newline='').read()


def rep(old, new, count=1):
    global s
    assert s.count(old) == count, (old, s.count(old))
    s = s.replace(old, new)


rep('''	int			csDirty, csNext;
''', '''	int			csDirty, csNext;

	// g_dsSurShots (tests): screenshots at the key moments
	int			shotAt[8], shotLook[8];
	qboolean	shotAttack, shotMod;
''')
rep('''static vmCvar_t		surCvAmbush, surCvMods, surCvEvents, surCvGhosts;
''', '''static vmCvar_t		surCvAmbush, surCvMods, surCvEvents, surCvGhosts, surCvShots;
''')
rep('''		trap_Cvar_Register( &surCvGhosts, "g_dsGhosts", "1", CVAR_ARCHIVE );
''', '''		trap_Cvar_Register( &surCvGhosts, "g_dsGhosts", "1", CVAR_ARCHIVE );
		trap_Cvar_Register( &surCvShots, "g_dsSurShots", "0", 0 );		// tests only
''')
rep('''static void Sur_EndAttack( void );
''', '''static void Sur_EndAttack( void );

#define SUR_LOOK_NONE		0
#define SUR_LOOK_AMBUSH		1
#define SUR_LOOK_GHOST		2
#define SUR_LOOK_GOLD		3
#define SUR_LOOK_FODDER		4
#define SUR_LOOK_BOUNTY		5

// g_dsSurShots: a screenshot in delay msec, the player pointed at a surprise first
static void Sur_Shot( int delay, int look ) {
	int		i;

	if ( !surCvShots.integer ) {
		return;
	}
	for ( i = 0 ; i < 8 ; i++ ) {
		if ( !sur.shotAt[i] ) {
			sur.shotAt[i] = level.time + delay;
			sur.shotLook[i] = look;
			return;
		}
	}
}
''')
rep('''	Sur_Callout( DSUC_AMBUSH_WARN, sur.ambArch, 0 );
''', '''	Sur_Callout( DSUC_AMBUSH_WARN, sur.ambArch, 0 );
	Sur_Shot( 1600, SUR_LOOK_AMBUSH );
''')
rep('''	Sur_Callout( DSUC_GHOST_WARN, sur.ghChar, 0 );
''', '''	Sur_Callout( DSUC_GHOST_WARN, sur.ghChar, 0 );
	Sur_Shot( 1600, SUR_LOOK_GHOST );
''')
rep('''		sur.ambHpPct = 100;
''', '''		sur.ambHpPct = 100;
		Sur_Shot( 500, SUR_LOOK_AMBUSH );
		Sur_Shot( 2200, SUR_LOOK_AMBUSH );
''')
rep('''		sur.ghState = 3;
''', '''		sur.ghState = 3;
		Sur_Shot( 700, SUR_LOOK_GHOST );
		Sur_Shot( 2500, SUR_LOOK_GHOST );
''')
rep('''		Sur_StartAttack( ds_miniBosses[sur.ambArch].attacks[k], mb, tg );
''', '''		Sur_StartAttack( ds_miniBosses[sur.ambArch].attacks[k], mb, tg );
		if ( !sur.shotAttack ) {
			sur.shotAttack = qtrue;
			Sur_Shot( 450, SUR_LOOK_AMBUSH );
		}
''')
rep('''	Sur_Callout( DSUC_EVENT_START, ev, ev == DSEV_BOUNTY ? sur.bountyClient : 0 );
''', '''	Sur_Callout( DSUC_EVENT_START, ev, ev == DSEV_BOUNTY ? sur.bountyClient : 0 );
	Sur_Shot( ev == DSEV_GOLDMETEOR ? 3500 : ev == DSEV_UPRISING ? 7000 : 1500,
		ev == DSEV_GOLDMETEOR ? SUR_LOOK_GOLD : ev == DSEV_UPRISING ? SUR_LOOK_FODDER : ev == DSEV_BOUNTY ? SUR_LOOK_BOUNTY : SUR_LOOK_NONE );
	if ( ev == DSEV_UPRISING ) {
		Sur_Shot( 15000, SUR_LOOK_FODDER );
	}
''')
rep('''	sur.goldRaceEnd = level.time + SUR_GOLD_RACE;
''', '''	sur.goldRaceEnd = level.time + SUR_GOLD_RACE;
	Sur_Shot( 900, SUR_LOOK_GOLD );
	Sur_Shot( 3500, SUR_LOOK_GOLD );
''')
rep('''			Sur_Callout( DSUC_AMBUSH_SLAIN, sur.ambArch, sk );
''', '''			Sur_Callout( DSUC_AMBUSH_SLAIN, sur.ambArch, sk );
			Sur_Shot( 450, SUR_LOOK_NONE );
''')
rep('''			Sur_Callout( DSUC_GHOST_SLAIN, sur.ghChar, sk );
''', '''			Sur_Callout( DSUC_GHOST_SLAIN, sur.ghChar, sk );
			Sur_Shot( 450, SUR_LOOK_NONE );
''')
# run the shots in the frame (before the configstring), the BLACKOUT shot once the player is in
rep('''	if ( sur.csDirty && level.time >= sur.csNext ) {
		Sur_Publish();
	}
}
''', '''	Sur_ShotFrame( h );
	if ( sur.csDirty && level.time >= sur.csNext ) {
		Sur_Publish();
	}
}
''')
rep('''void G_DS_SurpriseFrame( void ) {''', '''static void Sur_DevLook( gentity_t *h, const vec3_t target );

// where a look goes: qfalse = nothing there
static qboolean Sur_LookTarget( int what, vec3_t out ) {
	gentity_t	*t = NULL;
	int			i;

	switch ( what ) {
	case SUR_LOOK_AMBUSH:
		if ( sur.ambClient >= 0 && Sur_Alive( &g_entities[sur.ambClient] ) ) {
			t = &g_entities[sur.ambClient];
		} else if ( sur.ambState == 1 || sur.ambState == 2 ) {
			VectorCopy( sur.ambSpot, out );
			return qtrue;
		}
		break;
	case SUR_LOOK_GHOST:
		if ( sur.ghClient >= 0 && Sur_Alive( &g_entities[sur.ghClient] ) ) {
			t = &g_entities[sur.ghClient];
		} else if ( sur.ghState == 1 || sur.ghState == 2 ) {
			VectorCopy( sur.ghSpot, out );
			return qtrue;
		}
		break;
	case SUR_LOOK_GOLD:
		VectorCopy( sur.goldSpot, out );
		return qtrue;
	case SUR_LOOK_FODDER:
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			if ( sur.kind[i] == SUR_KIND_FODDER && Sur_Alive( &g_entities[i] ) ) {
				t = &g_entities[i];
				break;
			}
		}
		break;
	case SUR_LOOK_BOUNTY:
		if ( sur.bountyClient >= 0 ) {
			t = &g_entities[sur.bountyClient];
		}
		break;
	default:
		break;
	}
	if ( t && Sur_Alive( t ) ) {
		VectorCopy( t->r.currentOrigin, out );
		return qtrue;
	}
	return qfalse;
}

static void Sur_ShotFrame( gentity_t *h ) {
	vec3_t	tg;
	int		i;

	if ( !surCvShots.integer || !h ) {
		return;
	}
	if ( sur.mod && !sur.shotMod && sur.humanIn && level.time > sur.humanIn + 3000 ) {
		sur.shotMod = qtrue;
		Sur_Shot( 0, SUR_LOOK_NONE );
	}
	for ( i = 0 ; i < 8 ; i++ ) {
		if ( !sur.shotAt[i] || level.time < sur.shotAt[i] ) {
			continue;
		}
		sur.shotAt[i] = 0;
		if ( sur.shotLook[i] && Sur_LookTarget( sur.shotLook[i], tg ) ) {
			Sur_DevLook( h, tg );
		}
		trap_SendConsoleCommand( EXEC_INSERT, "wait 2; screenshotJPEG\\n" );
		G_LogPrintf( "DSSurprise: screenshot (look %i)\\n", sur.shotLook[i] );
		return;		// one a frame
	}
}

void G_DS_SurpriseFrame( void ) {''')
open(p, 'w', newline='').write(s)
print('ok')
