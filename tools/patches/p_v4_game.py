"""v4: style meter (STAT_STYLE, D..SSS), pick-a-perk builds, hooks."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from patch import sub

ROOT = pathlib.Path(__file__).resolve().parents[2] / 'ioq3'
g = lambda f: str(ROOT / 'code' / 'game' / f)

sub(str(ROOT / 'cmake' / 'basegame.cmake'), '    ${SOURCE_DIR}/game/g_elements.c\n',
    '    ${SOURCE_DIR}/game/g_elements.c\n    ${SOURCE_DIR}/game/g_style.c\n')

# ---------- shared ----------
H = g('bg_public.h')
sub(H, '''	STAT_ALTCHARGE,					// msec alt-fire has been held (charged alt-fire)
''', '''	STAT_ALTCHARGE,					// msec alt-fire has been held (charged alt-fire)
	STAT_STYLE,						// style meter 0..STYLE_MAX
''')
sub(H, '''#define RMS_HOOKREFRESH		0x0400		// this grapple pull already restored the air moves
''', '''#define RMS_HOOKREFRESH		0x0400		// this grapple pull already restored the air moves
// perks the movement code needs (set by the server from the player's perks)
#define RMP_QUICKHANDS		0x1000		// fire 20% faster
#define RMP_THIRDWIND		0x2000		// a third dash charge
#define RMP_OVERCHARGER		0x4000		// alt-fire charges 40% faster
#define RMP_ALL				0x7000

// rearmed: style meter
#define STYLE_MAX			1000
#define STYLE_RANK_D		60
#define STYLE_RANK_C		180
#define STYLE_RANK_B		320
#define STYLE_RANK_A		460
#define STYLE_RANK_S		600
#define STYLE_RANK_SS		740
#define STYLE_RANK_SSS		880

// rearmed: perks (pick one of three every few kills, kept for the match)
typedef enum {
	PERK_THICKSKIN,
	PERK_HEAVYCALIBER,
	PERK_QUICKHANDS,
	PERK_THIRDWIND,
	PERK_OVERCHARGER,
	PERK_BLOODTHIRST,
	PERK_HEADHUNTER,
	PERK_SKYHUNTER,
	PERK_PYROMANIAC,
	PERK_CONDUCTOR,
	PERK_IRONCLAD,
	PERK_SCAVENGER,
	PERK_NUM
} rmPerk_t;

extern const char *bg_perkNames[PERK_NUM];
extern const char *bg_perkDescs[PERK_NUM];
''')
sub(H, '''#define DASH_READY(ps)			( (ps)->stats[STAT_DASH_TIME] <= DASH_COOLDOWN * ( DASH_CHARGES - 1 ) )''',
    '''#define DASH_CHARGE_COUNT(ps)	( DASH_CHARGES + ( ( (ps)->stats[STAT_RMFLAGS] & RMP_THIRDWIND ) ? 1 : 0 ) )
#define DASH_READY(ps)			( (ps)->stats[STAT_DASH_TIME] <= DASH_COOLDOWN * ( DASH_CHARGE_COUNT(ps) - 1 ) )''')
sub(H, '''#define ALTFIRE_CHARGED		2	// ... and on charged ones''', '''#define ALTFIRE_CHARGED		2	// ... and on charged ones
#define ALTFIRE_REFLECTED	4	// turned around by an air blast''')
sub(H, '''	RS_ELEMENTS,
	RS_CHARGE,
	RS_NUM''', '''	RS_ELEMENTS,
	RS_CHARGE,
	RS_STYLE,
	RS_PERKS,
	RS_NUM''')

sub(g('bg_misc.c'), '''const int bg_gunGameLadder[GG_LEVELS] = {''', '''const char *bg_perkNames[PERK_NUM] = {
	"THICK SKIN", "HEAVY CALIBER", "QUICK HANDS", "THIRD WIND", "OVERCHARGER", "BLOODTHIRST",
	"HEADHUNTER", "SKY HUNTER", "PYROMANIAC", "CONDUCTOR", "IRONCLAD", "SCAVENGER"
};
const char *bg_perkDescs[PERK_NUM] = {
	"+25 max health", "+15% damage", "fire 20% faster", "a third dash charge", "alt-fire charges 40% faster",
	"15% of damage dealt heals you", "headshots do double damage", "+30% damage to airborne enemies",
	"your dashes leave fire", "your lightning arcs like everyone is soaked", "armor regenerates up to 50",
	"kills refill your weapon"
};

const int bg_gunGameLadder[GG_LEVELS] = {''')

# ---------- pmove: quick hands, overcharger ----------
P = g('bg_pmove.c')
sub(P, '''			*charge += pml.msec;
			if ( *charge > ALT_CHARGE_TIME + 1 ) {''', '''			*charge += ( pm->ps->stats[STAT_RMFLAGS] & RMP_OVERCHARGER ) ? pml.msec * 7 / 5 : pml.msec;
			if ( *charge > ALT_CHARGE_TIME + 1 ) {''')
sub(P, '''	addTime = charge ? alt->chargeTime : alt->time;
	if ( pm->ps->powerups[PW_HASTE] ) {''', '''	addTime = charge ? alt->chargeTime : alt->time;
	if ( pm->ps->stats[STAT_RMFLAGS] & RMP_QUICKHANDS ) {
		addTime = addTime * 4 / 5;
	}
	if ( pm->ps->powerups[PW_HASTE] ) {''')
sub(P, '''#ifndef MISSIONPACK
	if ( pm->ps->powerups[PW_RAPIDFIRE] ) {
		addTime /= 2;
	}
#endif

	pm->ps->weaponTime += addTime;
}''', '''#ifndef MISSIONPACK
	if ( pm->ps->powerups[PW_RAPIDFIRE] ) {
		addTime /= 2;
	}
#endif
	if ( pm->ps->stats[STAT_RMFLAGS] & RMP_QUICKHANDS ) {
		addTime = addTime * 4 / 5;
	}

	pm->ps->weaponTime += addTime;
}''')

# ---------- g_local.h ----------
L = g('g_local.h')
sub(L, '''	int			pinnedUntil;		// pinned by a charged nailgun spike
''', '''	int			pinnedUntil;		// pinned by a charged nailgun spike
	float		style;				// style meter (mirrored in STAT_STYLE)
	int			styleTime;			// last stylish act
	int			chargedShotTime;	// last charged hitscan alt-fire (charged kill style)
	int			perkArmorTime;		// ironclad armor regeneration
''')
sub(L, '''	int			multiKills;
	int			multiKillTime;
''', '''	int			multiKills;
	int			multiKillTime;
	int			perks;				// PERK_ bits held this match
	int			perkKills;			// kills towards the next perk offer
	int			perkOffer[3];		// the offered perks, [0] = -1 when none
	int			perkOfferTime;
''')
sub(L, '''void G_Thunderstrike( gentity_t *ent, vec3_t muzzle, vec3_t forward, float charge, float quad );
''', '''void G_Thunderstrike( gentity_t *ent, vec3_t muzzle, vec3_t forward, float charge, float quad );

//
// g_style.c
//
void G_StyleAdd( gentity_t *ent, int points, const char *label );
void G_StyleFrame( gentity_t *ent );
void G_StyleOnDamage( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, int damage, int hitFlags );
qboolean G_StyleOnKill( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int mod );
qboolean G_HasPerk( gentity_t *ent, int perk );
int G_PerkPmoveBits( gclient_t *cl );
void G_PerkPick( gentity_t *ent, int choice );
void G_PerksOnKill( gentity_t *attacker );
void G_PerksFrame( gentity_t *ent );
float G_PerkDamageScale( gentity_t *targ, gentity_t *attacker, int hitFlags );
void G_PerkOnDash( gentity_t *ent );
''')

# ---------- switches ----------
sub(g('g_rearmed.c'), '''	{ { 0 }, "g_chargedAlt",		"1", RMF_CHARGE }
};''', '''	{ { 0 }, "g_chargedAlt",		"1", RMF_CHARGE },
	{ { 0 }, "g_style",				"1", 0 },
	{ { 0 }, "g_perks",				"1", 0 }
};''')
sub(g('g_rearmed.c'), '''		G_ElementsClientFrame( ent );''', '''		G_ElementsClientFrame( ent );
		G_StyleFrame( ent );
		G_PerksFrame( ent );''')
sub(g('g_rearmed.c'), '''		level.clients[i].pers.multiKillTime = 0;''', '''		level.clients[i].pers.multiKillTime = 0;
		level.clients[i].pers.perks = 0;
		level.clients[i].pers.perkKills = 0;
		level.clients[i].pers.perkOffer[0] = -1;''')
# bloodthirst
sub(g('g_rearmed.c'), '''	if ( !cl->ps.powerups[PW_VAMPIRE] && level.mutator != MUT_VAMPIRE ) {
		return;
	}
	max = cl->ps.stats[STAT_MAX_HEALTH] * 2;
	attacker->health += damage / 2;''', '''	if ( !cl->ps.powerups[PW_VAMPIRE] && level.mutator != MUT_VAMPIRE ) {
		if ( G_HasPerk( attacker, PERK_BLOODTHIRST ) && attacker->health < cl->ps.stats[STAT_MAX_HEALTH] ) {
			attacker->health += ( damage * 15 + 50 ) / 100;
			if ( attacker->health > cl->ps.stats[STAT_MAX_HEALTH] ) {
				attacker->health = cl->ps.stats[STAT_MAX_HEALTH];
			}
			cl->ps.stats[STAT_HEALTH] = attacker->health;
		}
		return;
	}
	max = cl->ps.stats[STAT_MAX_HEALTH] * 2;
	attacker->health += damage / 2;''')

# ---------- g_client.c: fresh offers, thick skin max health ----------
sub(g('g_client.c'), '''	client->pers.lastKiller = -1;
''', '''	client->pers.lastKiller = -1;
	client->pers.perkOffer[0] = -1;
''')

# ---------- g_active.c ----------
A = g('g_active.c')
sub(A, '''	client->ps.stats[STAT_RMFLAGS] = ( client->ps.stats[STAT_RMFLAGS] & ~RMF_CONFIG ) | G_RearmMoveFlags();''',
    '''	client->ps.stats[STAT_RMFLAGS] = ( client->ps.stats[STAT_RMFLAGS] & ~( RMF_CONFIG | RMP_ALL ) )
		| G_RearmMoveFlags() | G_PerkPmoveBits( client );''')
sub(A, '''		case EV_FIRE_WEAPON_ALT:
			if ( client->ps.eventParms[ i & ( MAX_PS_EVENTS - 1 ) ] > 0 ) {''', '''		case EV_DASH:
			G_PerkOnDash( ent );
			break;

		case EV_FIRE_WEAPON_ALT:
			if ( client->ps.eventParms[ i & ( MAX_PS_EVENTS - 1 ) ] > 0 ) {''')

# ---------- g_combat.c ----------
C = g('g_combat.c')
sub(C, '''	// fire, water and electricity
	damage = G_ElementsDamage( targ, attacker, damage, mod );''', '''	// fire, water and electricity
	damage = G_ElementsDamage( targ, attacker, damage, mod );
	// perks
	damage = damage * G_PerkDamageScale( targ, attacker, hitFlags );''')
sub(C, '''		if ( hitFlags & HIT_HEADSHOT ) {
			attacker->client->ps.persistant[PERS_HEADSHOTS]++;
		}
		G_GatherHitConfirm( targ, attacker, take + asave, hitFlags );''', '''		if ( hitFlags & HIT_HEADSHOT ) {
			attacker->client->ps.persistant[PERS_HEADSHOTS]++;
		}
		G_GatherHitConfirm( targ, attacker, take + asave, hitFlags );
		G_StyleOnDamage( targ, inflictor, attacker, take + asave, hitFlags );''')
sub(C, '''	// never gib in a nodrop
	contents = trap_PointContents( self->r.currentOrigin, -1 );
''', '''	// style: a high rank turns every kill into a gib; perks count kills
	if ( G_StyleOnKill( self, inflictor, attacker, meansOfDeath ) && self->health > GIB_HEALTH ) {
		self->health = GIB_HEALTH - 1;
	}
	if ( attacker && attacker->client && attacker != self && !OnSameTeam( self, attacker ) ) {
		G_PerksOnKill( attacker );
	}

	// never gib in a nodrop
	contents = trap_PointContents( self->r.currentOrigin, -1 );
''')

# ---------- g_cmds.c ----------
sub(g('g_cmds.c'), '''	if (Q_stricmp (cmd, "give") == 0)''', '''	if (Q_stricmp (cmd, "perkpick") == 0) {
		char	arg[8];

		trap_Argv( 1, arg, sizeof( arg ) );
		G_PerkPick( ent, atoi( arg ) );
		return;
	}

	if (Q_stricmp (cmd, "give") == 0)''')

# ---------- charged hitscan, reflected missiles, conductor ----------
sub(g('g_weapon.c'), '''	frac = ALT_CHARGE_FRAC( charge );
	s_quadFactor = ent->client->ps.powerups[PW_QUAD] ? g_quadfactor.value : 1;''', '''	frac = ALT_CHARGE_FRAC( charge );
	ent->client->chargedShotTime = level.time;
	s_quadFactor = ent->client->ps.powerups[PW_QUAD] ? g_quadfactor.value : 1;''')
E = g('g_elements.c')
sub(E, '''		other->r.ownerNum = ent->s.number;
		other->parent = ent;''', '''		other->r.ownerNum = ent->s.number;
		other->parent = ent;
		other->s.generic1 |= ALTFIRE_REFLECTED;''')
sub(E, '''	wet = targ->client->ps.powerups[PW_WET] > level.time;
''', '''	wet = targ->client->ps.powerups[PW_WET] > level.time;
	if ( !wet && ( mod == MOD_LIGHTNING || mod == MOD_THUNDER ) && G_HasPerk( attacker, PERK_CONDUCTOR ) ) {
		wet = qtrue;		// conductor: everyone counts as soaked
	}
''')
sub(E, '''		G_Shock( targ, SHOCK_TIME );
		if ( wet ) {
			damage = damage * WET_SHOCK_SCALE;
			G_WetArcs( targ, attacker, damage / 2 );
		}''', '''		G_Shock( targ, SHOCK_TIME );
		if ( wet ) {
			damage = damage * WET_SHOCK_SCALE;
			G_WetArcs( targ, attacker, damage / 2 );
			if ( attacker && attacker->client && attacker != targ && level.time - attacker->client->styleTime > 500 ) {
				G_StyleAdd( attacker, 30, "CONDUCTED" );
			}
		}''')
print('v4 game ok')
