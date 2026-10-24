/*
===========================================================================
REARMED round 4 (en-brains) -- ai_habits.c: the bots learn the human's habits.

Tracked for the human (G_RL_HumanClient in DESCENT, else the first non-bot client;
bt_habitsTarget <client> picks one for bot-only tests):
  favourite weapon      time spent firing each weapon
  engagement range      distance to the enemy in the crosshair while firing (running average)
  hot spots             where the human runs and where bots first see them (256-unit cells)
Counters (g_botHabits, skill scaled): a weapon that beats the favourite, a range outside the
human's comfort zone, pre-aim at the hot spot the human usually comes from.
Reset every match; in DESCENT the weapon + range part rides the run (run key bt_hab),
hot spots are per floor (every floor is another map).
===========================================================================
*/
#include "g_local.h"
#include "../botlib/botlib.h"
#include "../botlib/be_aas.h"
#include "../botlib/be_ea.h"
#include "../botlib/be_ai_char.h"
#include "../botlib/be_ai_chat.h"
#include "../botlib/be_ai_gen.h"
#include "../botlib/be_ai_goal.h"
#include "../botlib/be_ai_move.h"
#include "../botlib/be_ai_weap.h"
#include "ai_main.h"
#include "ai_dmq3.h"
#include "ai_tactics.h"

extern bot_state_t	*botstates[MAX_CLIENTS];

#define HAB_CELL		256
#define HAB_MAX_CELLS	96
#define HAB_MIN_FIRE	12000		// ms of firing before a favourite counts
#define HAB_SAVE_TIME	15000

typedef struct {
	int		key;			// packed cell index
	float	sum[3];
	float	weight;
} habCell_t;

typedef struct {
	int			human;
	int			fireMs[WP_NUM_WEAPONS];
	float		range;
	int			rangeN;
	habCell_t	cells[HAB_MAX_CELLS];
	int			numCells;
	int			nextSample, nextSave, lastFrame;
	int			botEnemy[MAX_CLIENTS];
	// per bot decision windows
	int			weapNext[MAX_CLIENTS], weapPick[MAX_CLIENTS];
	int			rangeNext[MAX_CLIENTS];
	float		rangeWant[MAX_CLIENTS];
} habits_t;

static habits_t	hab;
static vmCvar_t	bt_habitsTarget;
static int		habCvar;

/*
===============================================================================
*/
static int HabHuman( void ) {
	int		i, h;

	if ( !habCvar ) {
		habCvar = 1;
		trap_Cvar_Register( &bt_habitsTarget, "bt_habitsTarget", "-1", 0 );
	}
	trap_Cvar_Update( &bt_habitsTarget );
	if ( bt_habitsTarget.integer >= 0 && bt_habitsTarget.integer < MAX_CLIENTS ) {
		return g_entities[bt_habitsTarget.integer].inuse ? bt_habitsTarget.integer : -1;
	}
	h = G_RL_HumanClient();
	if ( h >= 0 ) {
		return h;
	}
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( g_entities[i].inuse && g_entities[i].client && g_entities[i].client->pers.connected == CON_CONNECTED
			&& !( g_entities[i].r.svFlags & SVF_BOT ) && g_entities[i].client->sess.sessionTeam != TEAM_SPECTATOR ) {
			return i;
		}
	}
	return -1;
}

static void HabLoadRun( void ) {
	const char	*s;
	int			i, v;
	char		*p;
	char		buf[1024];

	if ( !G_RL_Active() ) {
		return;
	}
	s = G_RL_RunGet( "bt_hab" );
	if ( !*s ) {
		return;
	}
	// "range rangeN fire0 fire1 ..." (seconds of fire)
	Q_strncpyz( buf, s, sizeof( buf ) );
	p = buf;
	hab.range = atof( p );
	while ( *p && *p != ' ' ) p++;
	while ( *p == ' ' ) p++;
	hab.rangeN = atoi( p );
	for ( i = 0 ; i < WP_NUM_WEAPONS ; i++ ) {
		while ( *p && *p != ' ' ) p++;
		while ( *p == ' ' ) p++;
		if ( !*p ) {
			break;
		}
		v = atoi( p );
		hab.fireMs[i] = v * 1000;
	}
}

static void HabSaveRun( void ) {
	char	buf[512];
	int		i;

	if ( !G_RL_Active() ) {
		return;
	}
	Com_sprintf( buf, sizeof( buf ), "%.0f %i", hab.range, hab.rangeN );
	for ( i = 0 ; i < WP_NUM_WEAPONS ; i++ ) {
		Q_strcat( buf, sizeof( buf ), va( " %i", hab.fireMs[i] / 1000 ) );
	}
	G_RL_RunSet( "bt_hab", buf );
}

void BotHabitsInit( void ) {
	int		i;

	memset( &hab, 0, sizeof( hab ) );
	hab.human = -1;
	for ( i = 0 ; i < MAX_CLIENTS ; i++ ) {
		hab.botEnemy[i] = -1;
	}
	HabLoadRun();
	hab.nextSave = level.time + HAB_SAVE_TIME;
}

void BotHabitsShutdown( void ) {
	HabSaveRun();
}

static void HabAddCell( const vec3_t pos, float w ) {
	int			cx, cy, cz, key, i, worst;
	habCell_t	*c;

	cx = (int)floor( pos[0] / HAB_CELL ) & 1023;
	cy = (int)floor( pos[1] / HAB_CELL ) & 1023;
	cz = (int)floor( pos[2] / ( HAB_CELL / 2 ) ) & 1023;
	key = ( cx << 20 ) | ( cy << 10 ) | cz;
	for ( i = 0 ; i < hab.numCells ; i++ ) {
		if ( hab.cells[i].key == key ) {
			break;
		}
	}
	if ( i == hab.numCells ) {
		if ( hab.numCells < HAB_MAX_CELLS ) {
			hab.numCells++;
		} else {
			// replace the coldest
			worst = 0;
			for ( i = 1 ; i < hab.numCells ; i++ ) {
				if ( hab.cells[i].weight < hab.cells[worst].weight ) {
					worst = i;
				}
			}
			i = worst;
		}
		memset( &hab.cells[i], 0, sizeof( hab.cells[i] ) );
		hab.cells[i].key = key;
	}
	c = &hab.cells[i];
	VectorMA( c->sum, w, pos, c->sum );
	c->weight += w;
}

/*
The enemy closest to the human's crosshair (within ~20 degrees): engagement range sample
*/
static void HabRangeSample( gentity_t *h ) {
	vec3_t		fwd, eye, dir;
	gentity_t	*e;
	float		d, dot, best = 0.94f, bestD = 0;
	int			i;

	AngleVectors( h->client->ps.viewangles, fwd, NULL, NULL );
	VectorCopy( h->client->ps.origin, eye );
	eye[2] += h->client->ps.viewheight;
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		e = &g_entities[i];
		if ( e == h || !e->inuse || !e->client || e->health <= 0 || e->client->sess.sessionTeam == TEAM_SPECTATOR ) {
			continue;
		}
		VectorSubtract( e->r.currentOrigin, eye, dir );
		d = VectorNormalize( dir );
		if ( d < 32 || d > 3000 ) {
			continue;
		}
		dot = DotProduct( dir, fwd );
		if ( dot > best ) {
			best = dot;
			bestD = d;
		}
	}
	if ( bestD > 0 ) {
		if ( hab.rangeN < 20 ) {
			hab.range = ( hab.range * hab.rangeN + bestD ) / ( hab.rangeN + 1 );
		} else {
			hab.range = hab.range * 0.97f + bestD * 0.03f;
		}
		hab.rangeN++;
	}
}

void BotHabitsFrame( void ) {
	gentity_t		*h;
	playerState_t	*ps;
	bot_state_t		*bs;
	int				i, msec;

	if ( hab.lastFrame == level.time ) {
		return;
	}
	msec = hab.lastFrame ? level.time - hab.lastFrame : 0;
	hab.lastFrame = level.time;
	hab.human = HabHuman();
	if ( hab.human < 0 ) {
		return;
	}
	h = &g_entities[hab.human];
	if ( !h->client || h->health <= 0 || h->client->ps.pm_type != PM_NORMAL ) {
		return;
	}
	ps = &h->client->ps;
	if ( ( ps->eFlags & EF_FIRING ) || ps->weaponstate == WEAPON_FIRING ) {
		if ( ps->weapon > WP_NONE && ps->weapon < WP_NUM_WEAPONS && msec > 0 && msec < 200 ) {
			hab.fireMs[ps->weapon] += msec;
		}
		if ( level.time >= hab.nextSample ) {
			HabRangeSample( h );
		}
	}
	if ( level.time >= hab.nextSample ) {
		hab.nextSample = level.time + 500;
		if ( VectorLength( ps->velocity ) > 100 ) {
			HabAddCell( ps->origin, 1.0f );
		}
	}
	// bots that just spotted the human: that's an entrance
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		bs = botstates[i];
		if ( !bs || !bs->inuse ) {
			continue;
		}
		if ( bs->enemy == hab.human && hab.botEnemy[i] != hab.human ) {
			HabAddCell( ps->origin, 3.0f );
		}
		hab.botEnemy[i] = bs->enemy;
	}
	if ( level.time >= hab.nextSave ) {
		hab.nextSave = level.time + HAB_SAVE_TIME;
		HabSaveRun();
	}
}

/*
===============================================================================
*/
static int HabFavourite( float *share ) {
	int		i, best = 0, total = 0;

	for ( i = 1 ; i < WP_NUM_WEAPONS ; i++ ) {
		total += hab.fireMs[i];
		if ( hab.fireMs[i] > hab.fireMs[best] ) {
			best = i;
		}
	}
	if ( share ) {
		*share = total ? (float)hab.fireMs[best] / total : 0;
	}
	if ( total < HAB_MIN_FIRE || hab.fireMs[best] * 100 < total * 35 ) {
		return 0;
	}
	return best;
}

// weapons that beat the favourite, best first (0 terminated)
static const int *HabCounters( int fav ) {
	static const int vsRail[] = { WP_LIGHTNING, WP_PLASMAGUN, WP_ROCKET_LAUNCHER, WP_SHOTGUN, 0 };
	static const int vsRocket[] = { WP_RAILGUN, WP_LIGHTNING, WP_MACHINEGUN, 0 };
	static const int vsLightning[] = { WP_ROCKET_LAUNCHER, WP_RAILGUN, 0 };
	static const int vsClose[] = { WP_RAILGUN, WP_ROCKET_LAUNCHER, WP_LIGHTNING, 0 };
	static const int vsPlasma[] = { WP_RAILGUN, WP_LIGHTNING, 0 };
	static const int vsSpray[] = { WP_ROCKET_LAUNCHER, WP_RAILGUN, WP_LIGHTNING, 0 };
	static const int vsBfg[] = { WP_RAILGUN, 0 };

	switch ( fav ) {
	case WP_RAILGUN:			return vsRail;
	case WP_ROCKET_LAUNCHER:	return vsRocket;
	case WP_LIGHTNING:			return vsLightning;
	case WP_GAUNTLET:
	case WP_SHOTGUN:			return vsClose;
	case WP_PLASMAGUN:
	case WP_GRENADE_LAUNCHER:	return vsPlasma;
	case WP_BFG:				return vsBfg;
	default:					return vsSpray;	// machine gun, nailgun, chaingun, prox
	}
}

// the range that keeps a bot out of the favourite's sweet spot (0 = none)
static float HabCounterRange( int fav ) {
	switch ( fav ) {
	case WP_RAILGUN:			return 280;		// get in its face
	case WP_ROCKET_LAUNCHER:	return 650;
	case WP_LIGHTNING:			return 850;		// outside the beam
	case WP_GAUNTLET:
	case WP_SHOTGUN:			return 600;
	case WP_PLASMAGUN:			return 650;
	case WP_GRENADE_LAUNCHER:	return 600;
	case WP_BFG:				return 900;
	default:					return 0;
	}
}

static qboolean HabWeaponModeFree( void ) {
	return level.rmMode != RM_MODE_GUNGAME && level.rmMode != RM_MODE_INSTAGIB && level.rmMode != RM_MODE_OITC
		&& level.mutator != MUT_WEAPON_ROULETTE;
}

static qboolean HabHasWeapon( bot_state_t *bs, int w ) {
	playerState_t	*ps = &g_entities[bs->client].client->ps;

	return ( ps->stats[STAT_WEAPONS] & ( 1 << w ) ) && ps->ammo[w] > ( w == WP_LIGHTNING ? 20 : 3 );
}

int BotHabitsWeapon( bot_state_t *bs, int weapon ) {
	const int	*list;
	float		scale;
	int			fav, i, c = bs->client;

	if ( hab.human < 0 || bs->enemy != hab.human || !HabWeaponModeFree() || !BotTacOn( bs, BTAC_HABITS, &scale ) ) {
		return weapon;
	}
	fav = HabFavourite( NULL );
	if ( !fav ) {
		return weapon;
	}
	if ( level.time >= hab.weapNext[c] ) {
		hab.weapNext[c] = level.time + 4000;
		hab.weapPick[c] = 0;
		if ( random() < 0.2f + 0.7f * scale ) {
			list = HabCounters( fav );
			for ( i = 0 ; list[i] ; i++ ) {
				if ( list[i] == weapon ) {
					break;		// already countering
				}
				if ( HabHasWeapon( bs, list[i] ) ) {
					hab.weapPick[c] = list[i];
					BotTacCount( bs, BTC_HABIT_WEAPON );
					if ( BotTacDebug() ) {
						G_LogPrintf( "BtHabit: %i counters weapon %i with %i\n", c, fav, list[i] );
					}
					break;
				}
			}
		}
	}
	if ( hab.weapPick[c] && HabHasWeapon( bs, hab.weapPick[c] ) ) {
		return hab.weapPick[c];
	}
	return weapon;
}

float BotHabitsRange( bot_state_t *bs, int enemy ) {
	float	scale, want;
	int		fav, w, c = bs->client;

	if ( hab.human < 0 || enemy != hab.human || !BotTacOn( bs, BTAC_HABITS, &scale ) ) {
		return 0;
	}
	if ( level.time >= hab.rangeNext[c] ) {
		hab.rangeNext[c] = level.time + 6000;
		hab.rangeWant[c] = 0;
		if ( random() < 0.15f + 0.65f * scale ) {
			fav = HabFavourite( NULL );
			want = fav ? HabCounterRange( fav ) : 0;
			if ( !want && hab.rangeN >= 20 ) {
				if ( hab.range < 350 ) {
					want = 650;		// a close fighter: keep away
				} else if ( hab.range > 900 ) {
					want = 300;		// a sniper: get close
				}
			}
			hab.rangeWant[c] = want;
		}
	}
	want = hab.rangeWant[c];
	w = bs->cur_ps.weapon;
	if ( want > 400 && ( w == WP_SHOTGUN || w == WP_GAUNTLET ) ) {
		return 0;		// no kiting with a shotgun
	}
	if ( w == WP_LIGHTNING && want > 700 ) {
		want = 700;
	}
	return want;
}

qboolean BotHabitsHotSpot( bot_state_t *bs, vec3_t spot ) {
	habCell_t	*c;
	vec3_t		p, eye;
	trace_t		tr;
	float		d, best = 0, score;
	int			i;

	if ( hab.human < 0 || bs->client == hab.human ) {
		return qfalse;
	}
	VectorCopy( bs->eye, eye );
	for ( i = 0 ; i < hab.numCells ; i++ ) {
		c = &hab.cells[i];
		if ( c->weight < 4 ) {
			continue;
		}
		VectorScale( c->sum, 1.0f / c->weight, p );
		p[2] += 24;
		d = Distance( p, bs->origin );
		if ( d < 300 || d > 1800 ) {
			continue;
		}
		score = c->weight / ( 1.0f + d / 1000.0f );
		if ( score <= best ) {
			continue;
		}
		if ( !trap_InPVS( eye, p ) ) {
			continue;
		}
		trap_Trace( &tr, eye, NULL, NULL, p, bs->client, MASK_SOLID );
		if ( tr.fraction < 0.9f ) {
			continue;		// we couldn't see it come round that corner anyway
		}
		best = score;
		VectorCopy( p, spot );
	}
	return best > 0;
}

void BotHabitsReport( void ) {
	float	share;
	int		fav = HabFavourite( &share ), i, top = -1;

	for ( i = 0 ; i < hab.numCells ; i++ ) {
		if ( top < 0 || hab.cells[i].weight > hab.cells[top].weight ) {
			top = i;
		}
	}
	G_LogPrintf( "BtHabits: human %i fav %i share %.2f range %.0f samples %i cells %i top %.0f\n", hab.human, fav, share,
		hab.range, hab.rangeN, hab.numCells, top >= 0 ? hab.cells[top].weight : 0 );
}
