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
//
// cg_rearmed.c -- client side of the rearmed rules: announcements, mode / mutator HUD,
// new powerup visuals, big heads, juggernaut marker

#include "cg_local.h"

#define BANNER_TIME		2400
#define MEDAL_TIME		1600

/*
=================
CG_RearmRegister
=================
*/
void CG_RearmRegister( void ) {
	cgs.media.vampireShell = trap_R_RegisterShader( "rearmed/vampireShell" );
	cgs.media.rapidShell = trap_R_RegisterShader( "rearmed/rapidShell" );
	cgs.media.vampireItem = trap_R_RegisterShader( "rearmed/vampireItem" );
	cgs.media.rapidItem = trap_R_RegisterShader( "rearmed/rapidItem" );
	cgs.media.jetpackItem = trap_R_RegisterShader( "rearmed/jetpackItem" );
	cgs.media.flameShader = trap_R_RegisterShader( "rearmed/flame" );
	cgs.media.annStreak = trap_S_RegisterSound( "sound/rearmed/ann_streak.wav", qfalse );
	cgs.media.annMutator = trap_S_RegisterSound( "sound/rearmed/ann_mutator.wav", qfalse );
	cgs.media.annJuggernaut = trap_S_RegisterSound( "sound/rearmed/ann_juggernaut.wav", qfalse );
	cgs.media.annGunGame = trap_S_RegisterSound( "sound/rearmed/ann_gungame.wav", qfalse );
	cgs.media.annInfo = trap_S_RegisterSound( "sound/rearmed/ann_info.wav", qfalse );
	CG_StyleRegister();
	cgs.media.annMedal = trap_S_RegisterSound( "sound/rearmed/ann_medal.wav", qfalse );
	cgs.media.annBounty = trap_S_RegisterSound( "sound/rearmed/ann_bounty.wav", qfalse );
	cgs.media.annWarn = trap_S_RegisterSound( "sound/rearmed/ann_warn.wav", qfalse );
	cgs.media.armorBreakSound = trap_S_RegisterSound( "sound/rearmed/armorbreak.wav", qfalse );
	cgs.media.wallRunSound = trap_S_RegisterSound( "sound/rearmed/wallrun.wav", qfalse );
	cgs.media.hitSounds2[0] = trap_S_RegisterSound( "sound/rearmed/hit_low2.wav", qfalse );
	cgs.media.hitSounds2[1] = trap_S_RegisterSound( "sound/rearmed/hit_mid2.wav", qfalse );
	cgs.media.hitSounds2[2] = trap_S_RegisterSound( "sound/rearmed/hit_high2.wav", qfalse );
	cgs.media.edgeShader = trap_R_RegisterShaderNoMip( "rearmed/edge" );
	cgs.media.wetShell = trap_R_RegisterShader( "rearmed/wetShell" );
	cgs.media.fireLoopSound = trap_S_RegisterSound( "sound/rearmed/fire_loop.wav", qfalse );
	cgs.media.thunderSound = trap_S_RegisterSound( "sound/rearmed/thunder.wav", qfalse );
	cgs.media.windSound = trap_S_RegisterSound( "sound/rearmed/wind.wav", qfalse );
	cgs.media.steamSound = trap_S_RegisterSound( "sound/rearmed/steam.wav", qfalse );
	cgs.media.zapSound = trap_S_RegisterSound( "sound/rearmed/zap.wav", qfalse );
	cgs.media.chargeReadySound = trap_S_RegisterSound( "sound/rearmed/charge_ready.wav", qfalse );
	{
		int		i;
		for ( i = 0 ; i < 16 ; i++ ) {
			cgs.media.dmgDirShaders[i] = trap_R_RegisterShaderNoMip( va( "rearmed/dmgdir%02i", i ) );
		}
	}
#ifndef MISSIONPACK
	// powerups can come from killstreaks on maps that don't have the item
	CG_RegisterItemVisuals( BG_FindItemForPowerup( PW_VAMPIRE ) - bg_itemlist );
	CG_RegisterItemVisuals( BG_FindItemForPowerup( PW_RAPIDFIRE ) - bg_itemlist );
	CG_RegisterItemVisuals( BG_FindItemForPowerup( PW_JETPACK ) - bg_itemlist );
	CG_RegisterItemVisuals( BG_FindItemForPowerup( PW_HASTE ) - bg_itemlist );
	CG_RegisterItemVisuals( BG_FindItemForPowerup( PW_QUAD ) - bg_itemlist );
	{
		gitem_t	*small = BG_FindItem( "5 Health" );		// butcher perk drops
		if ( small ) {
			CG_RegisterItemVisuals( small - bg_itemlist );
		}
	}
#endif
	CG_MutatorsRegister();
}

/*
=================
CG_RearmParseConfigstring
=================
*/
void CG_RearmParseConfigstring( void ) {
	const char	*info = CG_ConfigString( CS_REARMED );
	const char	*s;
	int			n;

	cgs.rmMode = atoi( Info_ValueForKey( info, "mode" ) );
	cgs.rmMutator = atoi( Info_ValueForKey( info, "mut" ) );
	cgs.rmMutatorEnd = atoi( Info_ValueForKey( info, "mutend" ) );
	cgs.rmJuggernaut = info[0] ? atoi( Info_ValueForKey( info, "jug" ) ) : -1;
	cgs.rmSwitches = info[0] ? atoi( Info_ValueForKey( info, "sw" ) ) : -1;
	cgs.rmMutatorStart = atoi( Info_ValueForKey( info, "mutstart" ) );
	cgs.rmMutatorNext = atoi( Info_ValueForKey( info, "mutnext" ) );
	cgs.rmMutatorNextTime = atoi( Info_ValueForKey( info, "mutat" ) );
	s = Info_ValueForKey( info, "fl" );		// ds-economy: fraglimit x g_fragMult (0 = no limit), DESCENT: the floor's frags
	if ( s[0] ) {
		cgs.fraglimit = atoi( s );
	}
	if ( cgs.rmMutator < 0 || cgs.rmMutator >= MUT_NUM_MUTATORS ) {
		cgs.rmMutator = MUT_NONE;
	}
	if ( cgs.rmMutatorNext < 0 || cgs.rmMutatorNext >= MUT_NUM_MUTATORS ) {
		cgs.rmMutatorNext = MUT_NONE;
	}
	// bounties: "3.7." client numbers
	memset( cgs.rmBounty, 0, sizeof( cgs.rmBounty ) );
	s = Info_ValueForKey( info, "bty" );
	while ( *s ) {
		n = atoi( s );
		if ( n >= 0 && n < MAX_CLIENTS ) {
			cgs.rmBounty[n] = qtrue;
		}
		while ( *s && *s != '.' ) {
			s++;
		}
		if ( *s ) {
			s++;
		}
	}
}

/*
=================
CG_RearmServerCommand

rmann <kind> <text>: banner + sound; rmweapon <n>: the server switched our weapon
=================
*/
qboolean CG_RearmServerCommand( const char *cmd ) {
	char		kind[32];		// a copy: CG_Argv returns one static buffer that the next call overwrites
	sfxHandle_t	sfx;

	if ( CG_StyleServerCommand( cmd ) ) {
		return qtrue;
	}
	if ( !strcmp( cmd, "rmann" ) ) {
		Q_strncpyz( kind, CG_Argv( 1 ), sizeof( kind ) );
		CG_Printf( "%s\n", CG_Argv( 2 ) );
		if ( !cg_rearmAnnouncer.integer ) {
			return qtrue;
		}
		if ( !strcmp( kind, "medal" ) || !strcmp( kind, "warn" ) ) {
			Q_strncpyz( cg.rmMedal, CG_Argv( 2 ), sizeof( cg.rmMedal ) );
			cg.rmMedalTime = cg.time;
			cg.rmMedalKind = kind[0] == 'w';
			trap_S_StartLocalSound( cg.rmMedalKind ? cgs.media.annWarn : cgs.media.annMedal, CHAN_LOCAL_SOUND );
			return qtrue;
		}
		Q_strncpyz( cg.rmBanner, CG_Argv( 2 ), sizeof( cg.rmBanner ) );
		cg.rmBannerTime = cg.time;
		if ( !strcmp( kind, "bounty" ) ) {
			cg.rmBannerKind = 5;
			sfx = cgs.media.annBounty;
		} else if ( !strcmp( kind, "streak" ) ) {
			cg.rmBannerKind = 1;
			sfx = cgs.media.annStreak;
		} else if ( !strcmp( kind, "mutator" ) ) {
			// the event plate under the banner already shows the name: sound only
			cg.rmBannerTime = 0;
			trap_S_StartLocalSound( cgs.media.annMutator, CHAN_ANNOUNCER );
			return qtrue;
		} else if ( !strcmp( kind, "juggernaut" ) ) {
			cg.rmBannerKind = 3;
			sfx = cgs.media.annJuggernaut;
		} else if ( !strcmp( kind, "gungame" ) ) {
			cg.rmBannerKind = 4;
			sfx = cgs.media.annGunGame;
		} else {
			cg.rmBannerKind = 0;
			sfx = cgs.media.annInfo;
		}
		trap_S_StartLocalSound( sfx, CHAN_ANNOUNCER );
		return qtrue;
	}
	if ( !strcmp( cmd, "rmweapon" ) ) {
		cg.weaponSelect = atoi( CG_Argv( 1 ) );
		cg.weaponSelectTime = cg.time;
		return qtrue;
	}
	return qfalse;
}

/*
=================
CG_DrawRearmed

Announcement banner, running mutator, gun game ladder, juggernaut
=================
*/
static void CG_DrawCenteredSmall( int y, const char *s, const float *color ) {
	int w = CG_DrawStrlen( s ) * SMALLCHAR_WIDTH;
	CG_DrawStringExt( 320 - w / 2, y, s, color, qfalse, qtrue, SMALLCHAR_WIDTH, SMALLCHAR_HEIGHT, 0 );
}

void CG_DrawRearmed( void ) {
	static const vec4_t	bannerColors[6] = {
		{ 1.0f, 1.0f, 1.0f, 1.0f },		// info
		{ 1.0f, 0.55f, 0.15f, 1.0f },	// streak
		{ 0.5f, 0.85f, 1.0f, 1.0f },	// mutator
		{ 1.0f, 0.25f, 0.2f, 1.0f },	// juggernaut
		{ 0.5f, 1.0f, 0.45f, 1.0f },	// gun game
		{ 1.0f, 0.82f, 0.25f, 1.0f },	// bounty
	};
	vec4_t	color;
	float	*fade, scale;
	int		w, y, secs;
	const char	*s;

	// banner
	if ( cg.rmBannerTime && ( fade = CG_FadeColor( cg.rmBannerTime, BANNER_TIME ) ) != NULL ) {
		Vector4Copy( bannerColors[cg.rmBannerKind], color );
		color[3] = fade[3];
		// pop in (smaller than it was: the top centre is for the event plate and the floor strip)
		scale = 1.0f + 0.3f * ( 1.0f - (float)( cg.time - cg.rmBannerTime ) / 180.0f );
		if ( scale < 1.0f ) {
			scale = 1.0f;
		}
		scale *= cg.rmBannerKind == 0 ? 0.5f : 0.7f;
		color[3] *= 0.9f;
		w = CG_DrawStrlen( cg.rmBanner ) * BIGCHAR_WIDTH * scale;
		CG_DrawStringExt( 320 - w / 2, 62, cg.rmBanner, color, cg.rmBannerKind != 0, qtrue,
			BIGCHAR_WIDTH * scale, BIGCHAR_HEIGHT * scale, 0 );
	}

	// medals go to the style column right of the crosshair (with the style acts they come with);
	// warnings about you (STUCK, PINNED) stay under the crosshair, smaller
	if ( cg.rmMedalTime && ( fade = CG_FadeColor( cg.rmMedalTime, MEDAL_TIME ) ) != NULL ) {
		scale = 1.0f + 0.3f * ( 1.0f - (float)( cg.time - cg.rmMedalTime ) / 140.0f );
		if ( scale < 1.0f ) {
			scale = 1.0f;
		}
		if ( cg.rmMedalKind ) {
			color[0] = 1.0f; color[1] = 0.25f; color[2] = 0.2f; color[3] = fade[3];
			w = CG_DrawStrlen( cg.rmMedal ) * 8 * scale;
			CG_DrawStringExt( 320 - w / 2, 304, cg.rmMedal, color, qtrue, qtrue, 8 * scale, 11 * scale, 0 );
		} else {
			color[0] = 1.0f; color[1] = 0.82f; color[2] = 0.25f; color[3] = fade[3];
			secs = CG_DrawStrlen( cg.rmMedal ) > 16 ? 6 : 8;		// char width
			CG_HudAnchor( 1 );
			CG_DrawStringExt( 486, 132 - 4 * ( scale - 1 ), cg.rmMedal, color, qtrue, qtrue,
				secs * scale, ( secs + 3 ) * scale, 0 );
			CG_HudAnchor( 0 );
		}
	}

	y = 88;
	// incoming / running random event with its timer
	CG_DrawTip();
	if ( cgs.rmMutator || cgs.rmMutatorNext ) {
		CG_DrawEventHud( y );
		y += cgs.rmMutatorNext ? 52 : ( cgs.rmMutator == MUT_WEAPON_ROULETTE ? 48 : 34 );
	}

	// gun game ladder
	if ( cg.warmup ) {
		return;
	}
	if ( cgs.rmMode == RM_MODE_GUNGAME && cg.snap->ps.persistant[PERS_TEAM] != TEAM_SPECTATOR ) {
		int		lvl = cg.snap->ps.stats[STAT_GGLEVEL];
		gitem_t	*next;

		color[0] = 0.5f; color[1] = 1.0f; color[2] = 0.45f; color[3] = 1.0f;
		if ( lvl >= GG_LEVELS - 1 ) {
			s = va( "GUN GAME  %i/%i  - GAUNTLET KILL TO WIN -", lvl + 1, GG_LEVELS );
		} else {
			next = BG_FindItemForWeapon( bg_gunGameLadder[lvl + 1] );
			s = va( "GUN GAME  %i/%i   next: %s", lvl + 1, GG_LEVELS, next ? next->pickup_name : "?" );
		}
		CG_DrawCenteredSmall( y, s, color );
		y += SMALLCHAR_HEIGHT + 2;
	}

	// who is the juggernaut
	if ( cgs.rmMode == RM_MODE_JUGGERNAUT ) {
		color[0] = 1.0f; color[1] = 0.3f; color[2] = 0.25f; color[3] = 1.0f;
		if ( cgs.rmJuggernaut < 0 ) {
			s = "JUGGERNAUT: first kill takes it";
		} else if ( cgs.rmJuggernaut == cg.snap->ps.clientNum ) {
			s = "YOU ARE THE JUGGERNAUT";
		} else {
			s = va( "JUGGERNAUT: %s", cgs.clientinfo[cgs.rmJuggernaut].name );
		}
		CG_DrawCenteredSmall( y, s, color );
	}
}

/*
=================
CG_RearmPowerupShells

Extra glow shells for the rearmed powerups
=================
*/
void CG_RearmPowerupShells( refEntity_t *ent, entityState_t *state ) {
#ifndef MISSIONPACK
	if ( state->powerups & ( 1 << PW_VAMPIRE ) ) {
		ent->customShader = cgs.media.vampireShell;
		trap_R_AddRefEntityToScene( ent );
	}
	if ( state->powerups & ( 1 << PW_RAPIDFIRE ) ) {
		ent->customShader = cgs.media.rapidShell;
		trap_R_AddRefEntityToScene( ent );
	}
	if ( state->powerups & ( 1 << PW_WET ) ) {
		ent->customShader = cgs.media.wetShell;
		trap_R_AddRefEntityToScene( ent );
	}
#endif
}

/*
=================
CG_RearmItemShader

Tint the borrowed item models of the new powerups
=================
*/
void CG_RearmItemShader( gitem_t *item, refEntity_t *ent ) {
#ifndef MISSIONPACK
	if ( item->giType != IT_POWERUP ) {
		return;
	}
	if ( item->giTag == PW_VAMPIRE ) {
		ent->customShader = cgs.media.vampireItem;
	} else if ( item->giTag == PW_RAPIDFIRE ) {
		ent->customShader = cgs.media.rapidItem;
	} else if ( item->giTag == PW_JETPACK ) {
		ent->customShader = cgs.media.jetpackItem;
	}
#endif
}

/*
=================
CG_RearmHead

Big heads mutator
=================
*/
void CG_RearmHead( refEntity_t *head ) {
	if ( cgs.rmMutator == MUT_BIG_HEADS ) {
		VectorScale( head->axis[0], 2.2f, head->axis[0] );
		VectorScale( head->axis[1], 2.2f, head->axis[1] );
		VectorScale( head->axis[2], 2.2f, head->axis[2] );
		head->nonNormalizedAxes = qtrue;
	}
}

/*
=================
CG_RearmPlayerFX

Juggernaut beacon, jetpack flames
=================
*/
void CG_RearmPlayerFX( centity_t *cent, refEntity_t *head ) {
	refEntity_t	glow;
	vec3_t		forward, back, vel;

	if ( cgs.rmMode == RM_MODE_JUGGERNAUT && cent->currentState.clientNum == cgs.rmJuggernaut ) {
		memset( &glow, 0, sizeof( glow ) );
		glow.reType = RT_SPRITE;
		VectorCopy( head->origin, glow.origin );
		glow.origin[2] += 34;
		glow.radius = 10 + 3 * sin( cg.time * 0.01f );
		glow.customShader = cgs.media.fxGlowShader;
		glow.shaderRGBA[0] = 255;
		glow.shaderRGBA[1] = 50;
		glow.shaderRGBA[2] = 40;
		glow.shaderRGBA[3] = 255;
		trap_R_AddRefEntityToScene( &glow );
		trap_R_AddLightToScene( glow.origin, 120, 1.0f, 0.2f, 0.15f );
	}

	if ( cgs.rmBounty[cent->currentState.clientNum & ( MAX_CLIENTS - 1 )] && cent->currentState.clientNum != cg.snap->ps.clientNum ) {
		memset( &glow, 0, sizeof( glow ) );
		glow.reType = RT_SPRITE;
		VectorCopy( head->origin, glow.origin );
		glow.origin[2] += 30;
		glow.radius = 8 + 2 * sin( cg.time * 0.008f );
		glow.customShader = cgs.media.fxGlowShader;
		glow.shaderRGBA[0] = 255;
		glow.shaderRGBA[1] = 200;
		glow.shaderRGBA[2] = 50;
		glow.shaderRGBA[3] = 255;
		trap_R_AddRefEntityToScene( &glow );
		if ( cg_enemyTags.integer > 0 ) {		// the pulsing ring only with enemy tags on
			glow.customShader = cgs.media.fxRingShader;
			glow.radius = 12 + 4 * ( ( cg.time % 900 ) / 900.0f );
			glow.shaderRGBA[3] = 160;
			trap_R_AddRefEntityToScene( &glow );
		}
	}

	CG_ElementsPlayerFX( cent );

	if ( cent->currentState.eFlags & EF_JETTING ) {
		AngleVectors( cent->lerpAngles, forward, NULL, NULL );
		forward[2] = 0;
		VectorNormalize( forward );
		VectorMA( cent->lerpOrigin, -14, forward, back );
		back[2] += 8;
		VectorSet( vel, crandom() * 20, crandom() * 20, -260 );
		if ( cg_fx.integer ) {
			CG_SmokePuff( back, vel, 7 + random() * 5, 1.0f, 0.55f + 0.2f * random(), 0.2f, 0.9f, 220,
				cg.time, 0, 0, cgs.media.flameShader );
			if ( ( cg.time / 50 ) & 1 ) {
				VectorSet( vel, crandom() * 30, crandom() * 30, -120 );
				CG_SmokePuff( back, vel, 10, 0.4f, 0.4f, 0.4f, 0.35f, 700, cg.time, 0, 0, cgs.media.smokePuffShader );
			}
		}
		trap_R_AddLightToScene( back, 110, 1.0f, 0.6f, 0.2f );
		trap_S_AddLoopingSound( cent->currentState.number, cent->lerpOrigin, vec3_origin, cgs.media.flightSound );
	}
}

/*
=================
CG_RearmDamageDir

Remember where damage came from (world yaw towards the attacker)
=================
*/
void CG_RearmDamageDir( float yaw ) {
	int		i, oldest = 0;

	for ( i = 1 ; i < 4 ; i++ ) {
		if ( cg.rmDamageTime[i] < cg.rmDamageTime[oldest] ) {
			oldest = i;
		}
	}
	// merge with a recent hit from about the same direction
	for ( i = 0 ; i < 4 ; i++ ) {
		if ( cg.time - cg.rmDamageTime[i] < 1200 && fabs( AngleSubtract( cg.rmDamageYaw[i], yaw ) ) < 20 ) {
			oldest = i;
			break;
		}
	}
	cg.rmDamageYaw[oldest] = yaw;
	cg.rmDamageTime[oldest] = cg.time;
}

#define DAMAGE_DIR_TIME		1400

/*
=================
CG_RearmDrawOverlays

Dash meter, damage direction indicators, low health pulse. Screen-space, in real pixels
so they stay round on any aspect ratio.
=================
*/
void CG_RearmDrawOverlays( void ) {
	playerState_t	*ps = &cg.predictedPlayerState;
	float	cx, cy, size, a, rel, x, y, f, w, h, pulse;
	vec4_t	col;
	int		i, k, charges, debt;

	cx = cgs.glconfig.vidWidth * 0.5f;
	cy = cgs.glconfig.vidHeight * 0.5f;

	CG_DrawStyle();
	CG_DrawPerks();

	// damage direction: wedges around the crosshair pointing at the attacker
	if ( cg_damageIndicator.integer ) {
		size = cgs.glconfig.vidHeight * 0.26f;
		for ( i = 0 ; i < 4 ; i++ ) {
			if ( !cg.rmDamageTime[i] || cg.time - cg.rmDamageTime[i] >= DAMAGE_DIR_TIME ) {
				continue;
			}
			f = (float)( cg.time - cg.rmDamageTime[i] ) / DAMAGE_DIR_TIME;
			rel = AngleNormalize360( cg.refdefViewAngles[YAW] - cg.rmDamageYaw[i] );	// clockwise from ahead
			k = (int)( rel / 22.5f + 0.5f ) & 15;
			col[0] = 1.0f; col[1] = 0.15f; col[2] = 0.1f;
			col[3] = f < 0.1f ? 0.9f : 0.9f * ( 1.0f - ( f - 0.1f ) / 0.9f );
			trap_R_SetColor( col );
			trap_R_DrawStretchPic( cx - size / 2, cy - size / 2, size, size, 0, 0, 1, 1, cgs.media.dmgDirShaders[k] );
		}
		trap_R_SetColor( NULL );
	}

	// dash meter: one pip per charge under the crosshair, shown while recharging
	if ( cg_dashHud.integer && ( cgs.rmSwitches & ( 1 << RS_DASH ) ) && ps->stats[STAT_HEALTH] > 0 ) {
		debt = ps->stats[STAT_DASH_TIME];
		if ( debt <= 0 && !cg.rmDashFullTime ) {
			cg.rmDashFullTime = cg.time;
		} else if ( debt > 0 ) {
			cg.rmDashFullTime = 0;
		}
		a = debt > 0 ? 0.85f : 0.85f * ( 1.0f - ( cg.time - cg.rmDashFullTime ) / 500.0f );
		if ( a > 0 ) {
			w = cgs.glconfig.vidHeight * 0.022f;
			h = cgs.glconfig.vidHeight * 0.004f;
			y = cy + cgs.glconfig.vidHeight * 0.035f;
			charges = DASH_CHARGE_COUNT( ps );
			for ( i = 0 ; i < charges ; i++ ) {
				// charge i is full when the debt is at most (charges - 1 - i) cooldowns
				f = 1.0f - (float)( debt - ( charges - 1 - i ) * DASH_COOLDOWN ) / DASH_COOLDOWN;
				f = CG_Clamp01( f );
				x = cx + ( i - charges * 0.5f ) * ( w + h * 2 ) + h;
				col[0] = col[1] = col[2] = 0.15f; col[3] = a * 0.6f;
				trap_R_SetColor( col );
				trap_R_DrawStretchPic( x, y, w, h, 0, 0, 0, 0, cgs.media.whiteShader );
				if ( f >= 1.0f ) {
					col[0] = 0.35f; col[1] = 0.85f; col[2] = 1.0f;
				} else {
					col[0] = col[1] = col[2] = 0.8f;
				}
				col[3] = a;
				trap_R_SetColor( col );
				trap_R_DrawStretchPic( x, y, w * f, h, 0, 0, 0, 0, cgs.media.whiteShader );
			}
			trap_R_SetColor( NULL );
		}
	}

	CG_ZipDrawHud( cx, cy );	// zip line anchor brackets + miss reason (cg_zipline.c)

	// charged alt-fire meter over the dash pips
	if ( ps->stats[STAT_ALTCHARGE] >= ALT_TAP_TIME && ps->stats[STAT_HEALTH] > 0 ) {
		f = (float)( ps->stats[STAT_ALTCHARGE] - ALT_TAP_TIME ) / ( ALT_CHARGE_TIME - ALT_TAP_TIME );
		f = CG_Clamp01( f );
		w = cgs.glconfig.vidHeight * 0.06f;
		h = cgs.glconfig.vidHeight * 0.005f;
		y = cy + cgs.glconfig.vidHeight * 0.025f;
		col[0] = col[1] = col[2] = 0.1f; col[3] = 0.6f;
		trap_R_SetColor( col );
		trap_R_DrawStretchPic( cx - w / 2, y, w, h, 0, 0, 0, 0, cgs.media.whiteShader );
		if ( f >= 1.0f ) {
			pulse = 0.6f + 0.4f * sin( cg.time * 0.03f );
			col[0] = 1.0f; col[1] = 0.85f * pulse + 0.15f; col[2] = 0.5f * pulse; col[3] = 1.0f;
		} else {
			col[0] = 1.0f; col[1] = 0.42f + 0.3f * f; col[2] = 0.1f; col[3] = 0.95f;
		}
		trap_R_SetColor( col );
		trap_R_DrawStretchPic( cx - w / 2, y, w * f, h, 0, 0, 0, 0, cgs.media.whiteShader );
		trap_R_SetColor( NULL );
	}

	// element statuses tint the screen edges
	k = -1;
	if ( ps->powerups[PW_BURNING] > cg.time ) {
		col[0] = 1.0f; col[1] = 0.45f; col[2] = 0.05f; k = 0;
	} else if ( ps->powerups[PW_SHOCKED] > cg.time ) {
		col[0] = 0.4f; col[1] = 0.85f; col[2] = 1.0f; k = 1;
	} else if ( ps->powerups[PW_WET] > cg.time ) {
		col[0] = 0.15f; col[1] = 0.45f; col[2] = 1.0f; k = 2;
	}
	if ( k >= 0 && ps->stats[STAT_HEALTH] > 0 ) {
		pulse = 0.5f + 0.5f * sin( cg.time * ( k == 1 ? 0.05f : 0.012f ) );
		col[3] = k == 2 ? 0.22f : 0.3f + 0.25f * pulse;
		trap_R_SetColor( col );
		w = cgs.glconfig.vidHeight * ( k == 2 ? 0.12f : 0.2f );
		trap_R_DrawStretchPic( 0, 0, w, cgs.glconfig.vidHeight, 0, 0, 1, 1, cgs.media.edgeShader );
		trap_R_DrawStretchPic( cgs.glconfig.vidWidth - w, 0, w, cgs.glconfig.vidHeight, 1, 0, 0, 1, cgs.media.edgeShader );
		trap_R_SetColor( NULL );
	}

	// low health: red pulse creeping in from the screen edges
	if ( cg_lowHealthFX.integer && !cg_screenFX.integer && ps->stats[STAT_HEALTH] > 0 && ps->stats[STAT_HEALTH] <= 35
		&& ps->persistant[PERS_TEAM] != TEAM_SPECTATOR ) {
		f = 1.0f - ps->stats[STAT_HEALTH] / 35.0f;
		pulse = 0.5f + 0.5f * sin( cg.time * ( 0.005f + 0.006f * f ) );
		col[0] = 0.8f; col[1] = 0.0f; col[2] = 0.0f;
		col[3] = ( 0.25f + 0.35f * f ) * ( 0.6f + 0.4f * pulse );
		trap_R_SetColor( col );
		w = cgs.glconfig.vidHeight * ( 0.18f + 0.1f * f );
		trap_R_DrawStretchPic( 0, 0, w, cgs.glconfig.vidHeight, 0, 0, 1, 1, cgs.media.edgeShader );
		trap_R_DrawStretchPic( cgs.glconfig.vidWidth - w, 0, w, cgs.glconfig.vidHeight, 1, 0, 0, 1, cgs.media.edgeShader );
		trap_R_SetColor( NULL );
	}
}
