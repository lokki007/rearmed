/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// g_gore.c -- rearmed: how bodies die (decapitation, charring, overkill), gore bookkeeping
//
// The server only decides WHAT happened; cgame (cg_gore.c) draws it:
//   G_GoreOnHit    every hit on a player or corpse sends an EV_GORE_HIT with the
//                  hit point, the shot direction and the damage (one per target
//                  per frame, shotgun pellets add up).
//   G_GoreOnDeath  sets RM_BODY_* flags in ps.generic1 (copied to the entity and
//                  later to the corpse): a headshot kill decapitates, burn and
//                  lightning kills char the body, a charged rail cuts the body in
//                  half, and far too much damage turns the death into a big gib.
// g_gore 0 turns both off (stock deaths).

#include "g_local.h"

static vmCvar_t	g_gore;
static qboolean	goreCvarRegistered;

static void G_GoreCvars( void ) {
	if ( !goreCvarRegistered ) {
		trap_Cvar_Register( &g_gore, "g_gore", "1", CVAR_ARCHIVE );
		goreCvarRegistered = qtrue;
	} else {
		trap_Cvar_Update( &g_gore );
	}
}

// per entity: this frame's EV_GORE_HIT (to merge pellets) and the last charged hit
static struct {
	int			time;
	gentity_t	*ev;
	int			chargedTime;
} goreHits[MAX_GENTITIES];

/*
==================
G_GoreOnHit

Called from G_Damage once the damage is known (take > 0).
==================
*/
void G_GoreOnHit( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, vec3_t dir, vec3_t point,
				 int damage, int dflags, int mod, int hitFlags ) {
	gentity_t	*ev;
	vec3_t		p;
	int			n, sum;

	if ( damage <= 0 || !targ ) {
		return;
	}
	if ( !targ->client && !( targ->s.eType == ET_PLAYER && ( targ->s.eFlags & EF_DEAD ) ) ) {
		return;		// players and their corpses bleed
	}
	G_GoreCvars();
	if ( !g_gore.integer || !g_blood.integer ) {
		return;
	}
	n = targ->s.number;

	if ( ( inflictor && inflictor->s.eType == ET_MISSILE && ( inflictor->s.generic1 & ALTFIRE_CHARGED ) )
		|| ( attacker && attacker->client && level.time - attacker->client->chargedShotTime < 100 ) ) {
		goreHits[n].chargedTime = level.time;
	}

	// shotgun pellets and multi-hits in one frame: one bigger event
	ev = goreHits[n].ev;
	if ( goreHits[n].time == level.time && ev && ev->inuse && ev->s.eType == ET_EVENTS + EV_GORE_HIT
		&& ev->s.otherEntityNum == n ) {
		sum = ev->s.eventParm + damage;
		ev->s.eventParm = sum > 255 ? 255 : sum;
		ev->s.generic1 |= hitFlags & 0x7f;
		return;
	}

	if ( point && !( dflags & DAMAGE_RADIUS ) ) {
		VectorCopy( point, p );
	} else {
		// splash: somewhere on the body
		VectorCopy( targ->r.currentOrigin, p );
		p[0] += crandom() * 8;
		p[1] += crandom() * 8;
		p[2] += 4 + random() * 18;
	}
	ev = G_TempEntity( p, EV_GORE_HIT );
	ev->s.eventParm = damage > 255 ? 255 : damage;
	ev->s.generic1 = ( hitFlags & 0x7f ) | ( ( dflags & DAMAGE_RADIUS ) ? 0x80 : 0 );
	ev->s.otherEntityNum = n;
	ev->s.otherEntityNum2 = attacker ? attacker->s.number : ENTITYNUM_WORLD;
	ev->s.weapon = mod;
	if ( dir ) {
		VectorCopy( dir, ev->s.angles2 );
		VectorNormalize( ev->s.angles2 );
	} else if ( point ) {
		VectorSubtract( targ->r.currentOrigin, point, ev->s.angles2 );
		VectorNormalize( ev->s.angles2 );
	}
	goreHits[n].time = level.time;
	goreHits[n].ev = ev;
}

static qboolean G_GoreExplosive( int mod ) {
	switch ( mod ) {
	case MOD_GRENADE:
	case MOD_GRENADE_SPLASH:
	case MOD_ROCKET:
	case MOD_ROCKET_SPLASH:
	case MOD_BFG:
	case MOD_BFG_SPLASH:
	case MOD_PROXIMITY_MINE:
	case MOD_PROP:
	case MOD_METEOR:
	case MOD_CORPSE:
	case MOD_SLAM:
	case MOD_TELEFRAG:
	case MOD_CRUSH:
		return qtrue;
	default:
		return qfalse;
	}
}

/*
==================
G_GoreOnDeath

player_die, after the kill has been scored and before the body is gibbed or
left as a corpse. damage is the killing blow, self->health is already below 0.
May push the health below GIB_HEALTH to turn an overkill into a gib.
==================
*/
void G_GoreOnDeath( gentity_t *self, gentity_t *attacker, int mod, int damage ) {
	gclient_t	*cl = self->client;
	int			flags, excess;
	qboolean	headshot, charged, explosive;

	if ( !cl ) {
		return;
	}
#ifdef MISSIONPACK
	if ( g_gametype.integer == GT_HARVESTER ) {
		return;		// generic1 counts the skulls there
	}
#endif
	cl->ps.generic1 = 0;
	self->s.generic1 = 0;
	G_GoreCvars();
	if ( !g_gore.integer || !g_blood.integer ) {
		return;
	}

	flags = 0;
	headshot = attacker && attacker->client && attacker != self
		&& attacker->client->hitTarget == self->s.number && ( attacker->client->hitFlags & HIT_HEADSHOT );
	charged = goreHits[self->s.number].chargedTime == level.time;
	explosive = G_GoreExplosive( mod );
	excess = -self->health;		// damage past zero health

	switch ( mod ) {
	case MOD_BURN:
	case MOD_THUNDER:
	case MOD_LIGHTNING:
	case MOD_LAVA:
		flags |= RM_BODY_CHARRED;
		break;
	default:
		break;
	}

	if ( headshot && !explosive && !( flags & RM_BODY_CHARRED ) ) {
		// headshots always take the head off, even when the damage would gib (a rail headshot
		// does 150): the body stays so the neck can bleed
		flags |= RM_BODY_HEADLESS;
		if ( self->health <= GIB_HEALTH ) {
			self->health = GIB_HEALTH + 1;
		}
	} else if ( mod == MOD_RAILGUN && ( charged || damage >= 150 ) ) {
		// charged rail / lance: cut in half, the legs stay
		flags |= RM_BODY_SPLIT;
		if ( self->health <= GIB_HEALTH ) {
			self->health = GIB_HEALTH + 1;
		}
	} else {
		// overkill: explosions and charged shots with lots of damage to spare turn into a big gib
		if ( ( explosive || charged ) && damage >= 80 && excess >= 25 ) {
			flags |= RM_BODY_OVERKILL;
		} else if ( excess >= 70 ) {
			flags |= RM_BODY_OVERKILL;
		}
		if ( ( flags & RM_BODY_OVERKILL ) && self->health > GIB_HEALTH ) {
			self->health = GIB_HEALTH - 1;
		}
	}

	cl->ps.generic1 = flags;
	self->s.generic1 = flags;
}
