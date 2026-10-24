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
// cg_wheel.c -- hold-to-open radial weapon wheel
//
// +wheel sets cl_wheelCapture; while it is set the engine (CL_MouseMove) stops turning
// the view and adds the mouse motion to cl_wheelX / cl_wheelY instead, in degrees the
// view would have turned. We drain those into cg.wheelX / cg.wheelY every frame.
// -wheel picks the slice under the pointer, or swaps to the previous weapon on a tap.
// A gamepad's right stick steers the pointer too (cl_input.c CL_JoystickMove).
// The game keeps running at full speed while the wheel is open.

#include "cg_local.h"

#define WHEEL_TAP_MSEC		180		// released sooner than this without aiming: previous weapon
#define WHEEL_DEADZONE		3.0f	// pointer length needed to pick a slice
#define WHEEL_LIMIT			9.0f	// pointer length clamp, so it never runs away

#define WHEEL_OPEN_MSEC		140
#define WHEEL_CLOSE_MSEC	120

static const vec4_t	wheelAccent = { 1.0f, 0.62f, 0.18f, 1.0f };

static int			wheelLastDraw;
static int			wheelCloseTime;			// cg.time the wheel closed (close animation)
static int			wheelCloseSlot = -1;	// slot picked on close
static int			wheelHoverWeapon;		// for the hover tick
static float		wheelHoverAmt[WP_NUM_WEAPONS];
static sfxHandle_t	wheelTickSound, wheelOpenSound, wheelCloseSound, wheelSelectSound;

/*
=================
CG_WheelCvar
=================
*/
static float CG_WheelCvar( const char *name ) {
	char	buf[64];

	trap_Cvar_VariableStringBuffer( name, buf, sizeof( buf ) );
	return atof( buf );
}

/*
=================
CG_WheelUsable

Alive, playing, not following someone and not in intermission
=================
*/
static qboolean CG_WheelUsable( void ) {
	if ( !cg.snap ) {
		return qfalse;
	}
	if ( cg.snap->ps.pm_flags & PMF_FOLLOW ) {
		return qfalse;
	}
	if ( cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
		return qfalse;
	}
	if ( cg.snap->ps.pm_type == PM_INTERMISSION ) {
		return qfalse;
	}
	if ( cg.snap->ps.stats[STAT_HEALTH] <= 0 ) {
		return qfalse;
	}
	return qtrue;
}

/*
=================
CG_WheelSelectable

Same rule as CG_WeaponSelectable: owned and has ammo (gauntlet's -1 counts)
=================
*/
static qboolean CG_WheelSelectable( int w ) {
	if ( w <= WP_NONE || w >= WP_NUM_WEAPONS || w == WP_GRAPPLING_HOOK ) {
		return qfalse;
	}
	if ( !( cg.snap->ps.stats[STAT_WEAPONS] & ( 1 << w ) ) ) {
		return qfalse;
	}
	return cg.snap->ps.ammo[w] != 0;
}

/*
=================
CG_WheelList

Owned weapons in weapon-number order, clockwise from the top
=================
*/
static int CG_WheelList( int *list ) {
	int		w, n;

	n = 0;
	for ( w = WP_NONE + 1 ; w < WP_NUM_WEAPONS ; w++ ) {
		if ( w == WP_GRAPPLING_HOOK ) {
			continue;
		}
		if ( cg.snap->ps.stats[STAT_WEAPONS] & ( 1 << w ) ) {
			list[n++] = w;
		}
	}
	return n;
}

/*
=================
CG_WheelPoll

Drain the engine's accumulated mouse motion into the clamped pointer
=================
*/
static void CG_WheelPoll( void ) {
	float	dx, dy, sens, len;

	dx = CG_WheelCvar( "cl_wheelX" );
	dy = CG_WheelCvar( "cl_wheelY" );
	if ( dx == 0.0f && dy == 0.0f ) {
		return;
	}
	trap_Cvar_Set( "cl_wheelX", "0" );
	trap_Cvar_Set( "cl_wheelY", "0" );

	sens = cg_wheelSensitivity.value > 0.0f ? cg_wheelSensitivity.value : 1.0f;
	cg.wheelX += dx * sens;
	cg.wheelY += dy * sens;
	len = sqrt( cg.wheelX * cg.wheelX + cg.wheelY * cg.wheelY );
	if ( len > WHEEL_LIMIT ) {
		cg.wheelX *= WHEEL_LIMIT / len;
		cg.wheelY *= WHEEL_LIMIT / len;
	}
}

/*
=================
CG_WheelHover

Slot under the pointer, -1 while inside the dead zone
=================
*/
static int CG_WheelHover( int count ) {
	float	ang;
	int		slot;

	if ( count <= 0 ) {
		return -1;
	}
	if ( cg.wheelX * cg.wheelX + cg.wheelY * cg.wheelY < WHEEL_DEADZONE * WHEEL_DEADZONE ) {
		return -1;
	}
	ang = atan2( cg.wheelX, -cg.wheelY );	// clockwise from straight up (screen y points down)
	if ( ang < 0 ) {
		ang += 2 * M_PI;
	}
	slot = (int)( ang / ( 2 * M_PI / count ) + 0.5f );
	return slot % count;
}

/*
=================
CG_WheelSelect
=================
*/
static void CG_WheelSelect( int w ) {
	cg.weaponSelectTime = cg.time;
	cg.weaponSelect = w;
}

/*
=================
CG_WheelSwapLast
=================
*/
static void CG_WheelSwapLast( void ) {
	int		w;

	w = cg.wheelLastWeapon;
	if ( w == cg.weaponSelect ) {
		w = cg.wheelCurWeapon;		// already on the way there: flip back
	}
	if ( w != cg.weaponSelect && CG_WheelSelectable( w ) ) {
		CG_WheelSelect( w );
	}
}

/*
=================
CG_WheelDown_f

"+wheel <key> <time>"
=================
*/
void CG_WheelDown_f( void ) {
	if ( cg.wheelOpen || !CG_WheelUsable() ) {
		return;
	}
	if ( !cg_wheel.integer ) {
		CG_WheelSwapLast();
		return;
	}
	cg.wheelOpen = qtrue;
	cg.wheelOpenTime = cg.time;
	wheelCloseTime = 0;
	wheelHoverWeapon = WP_NONE;
	memset( wheelHoverAmt, 0, sizeof( wheelHoverAmt ) );
	trap_S_StartLocalSound( wheelOpenSound, CHAN_LOCAL_SOUND );
	CG_Tip( TIP_WHEELUSE );
	cg.wheelOpenMsec = atoi( CG_Argv( 2 ) );
	cg.wheelX = cg.wheelY = 0;
	trap_Cvar_Set( "cl_wheelX", "0" );
	trap_Cvar_Set( "cl_wheelY", "0" );
	trap_Cvar_Set( "cl_wheelCapture", "1" );
}

/*
=================
CG_WheelUp_f

"-wheel <key> <time>"
=================
*/
void CG_WheelUp_f( void ) {
	int		list[WP_NUM_WEAPONS];
	int		n, slot, held, upMsec;

	if ( !cg.wheelOpen ) {
		return;
	}
	CG_WheelPoll();
	cg.wheelOpen = qfalse;
	wheelCloseTime = cg.time;
	wheelCloseSlot = -1;
	trap_Cvar_Set( "cl_wheelCapture", "0" );
	trap_Cvar_Set( "cl_wheelX", "0" );
	trap_Cvar_Set( "cl_wheelY", "0" );

	// the key event times are real milliseconds; fall back to game time when typed
	upMsec = atoi( CG_Argv( 2 ) );
	if ( cg.wheelOpenMsec > 0 && upMsec >= cg.wheelOpenMsec ) {
		held = upMsec - cg.wheelOpenMsec;
	} else {
		held = cg.time - cg.wheelOpenTime;
	}

	if ( !CG_WheelUsable() ) {
		return;
	}
	n = CG_WheelList( list );
	slot = CG_WheelHover( n );
	wheelCloseSlot = slot;
	if ( slot >= 0 ) {
		if ( CG_WheelSelectable( list[slot] ) && list[slot] != cg.weaponSelect ) {
			CG_WheelSelect( list[slot] );
			trap_S_StartLocalSound( wheelSelectSound, CHAN_LOCAL_SOUND );
		} else {
			trap_S_StartLocalSound( wheelCloseSound, CHAN_LOCAL_SOUND );
		}
		return;
	}
	trap_S_StartLocalSound( wheelCloseSound, CHAN_LOCAL_SOUND );
	if ( held < WHEEL_TAP_MSEC ) {
		CG_WheelSwapLast();
	}
}

/*
=================
CG_WheelTrackWeapon

Every frame: remember the weapon held before the current one
=================
*/
void CG_WheelTrackWeapon( void ) {
	int		w;

	// a capture left on by a cgame that went away mid-hold would freeze mouse look
	if ( !cg.wheelInit ) {
		cg.wheelInit = qtrue;
		wheelTickSound = trap_S_RegisterSound( "sound/rearmed/events/wheel_tick.wav", qfalse );
		wheelOpenSound = trap_S_RegisterSound( "sound/rearmed/events/wheel_open.wav", qfalse );
		wheelCloseSound = trap_S_RegisterSound( "sound/rearmed/events/wheel_close.wav", qfalse );
		wheelSelectSound = trap_S_RegisterSound( "sound/rearmed/events/wheel_select.wav", qfalse );
		if ( !cg.wheelOpen ) {
			trap_Cvar_Set( "cl_wheelCapture", "0" );
		}
	}

	if ( !cg.snap ) {
		return;
	}
	if ( ( cg.snap->ps.pm_flags & PMF_FOLLOW ) || cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
		cg.wheelCurWeapon = WP_NONE;	// someone else's weapons
		return;
	}
	w = cg.snap->ps.weapon;
	if ( w <= WP_NONE || w >= WP_NUM_WEAPONS || w == WP_GRAPPLING_HOOK || w == cg.wheelCurWeapon ) {
		return;
	}
	if ( cg.wheelCurWeapon != WP_NONE ) {
		cg.wheelLastWeapon = cg.wheelCurWeapon;
	}
	cg.wheelCurWeapon = w;
}

/*
==============================================================================

DRAWING -- real pixels, centred on the crosshair, round on any aspect

==============================================================================
*/

/*
=================
CG_WheelAnnulus

Filled ring (or disc when rIn is 0) from horizontal strips of the white shader
=================
*/
static void CG_WheelAnnulus( float cx, float cy, float rOut, float rIn, int step ) {
	int		i, rows;
	float	y0, y, dy, ho, hi;

	y0 = floor( cy - rOut );
	rows = (int)( ( rOut * 2 + 2 ) / step ) + 1;
	for ( i = 0 ; i < rows ; i++ ) {
		y = y0 + i * step;
		dy = y + step * 0.5f - cy;
		if ( dy * dy >= rOut * rOut ) {
			continue;
		}
		ho = sqrt( rOut * rOut - dy * dy );
		if ( dy * dy < rIn * rIn ) {
			hi = sqrt( rIn * rIn - dy * dy );
			trap_R_DrawStretchPic( cx - ho, y, ho - hi, step, 0, 0, 0, 0, cgs.media.whiteShader );
			trap_R_DrawStretchPic( cx + hi, y, ho - hi, step, 0, 0, 0, 0, cgs.media.whiteShader );
		} else {
			trap_R_DrawStretchPic( cx - ho, y, ho * 2, step, 0, 0, 0, 0, cgs.media.whiteShader );
		}
	}
}

/*
=================
CG_WheelArc

Band of overlapping squares along an arc (angles clockwise from the top)
=================
*/
static void CG_WheelArc( float cx, float cy, float r, float a0, float a1, float size ) {
	int		i, n;
	float	a;

	n = (int)( ( a1 - a0 ) * r / ( size * 0.4f ) ) + 1;
	for ( i = 0 ; i <= n ; i++ ) {
		a = a0 + ( a1 - a0 ) * i / n;
		trap_R_DrawStretchPic( cx + sin( a ) * r - size * 0.5f, cy - cos( a ) * r - size * 0.5f,
			size, size, 0, 0, 0, 0, cgs.media.whiteShader );
	}
}

/*
=================
CG_WheelChars / CG_WheelString

Charset text at a pixel size, centred on cx, with a drop shadow
=================
*/
static void CG_WheelChars( float x, float y, float cw, float ch, const char *s ) {
	int		c;
	float	fr, fc;

	while ( *s ) {
		if ( Q_IsColorString( s ) ) {
			s += 2;
			continue;
		}
		c = *s & 255;
		if ( c != ' ' ) {
			fr = ( c >> 4 ) * 0.0625f;
			fc = ( c & 15 ) * 0.0625f;
			trap_R_DrawStretchPic( x, y, cw, ch, fc, fr, fc + 0.0625f, fr + 0.0625f, cgs.media.charsetShader );
		}
		x += cw;
		s++;
	}
}

static void CG_WheelString( float cx, float y, float ch, const char *s, const float *color ) {
	vec4_t	shadow;
	float	cw, x, off;

	cw = ch * 0.5f;
	x = cx - CG_DrawStrlen( s ) * cw * 0.5f;
	off = ch * 0.08f;
	shadow[0] = shadow[1] = shadow[2] = 0;
	shadow[3] = color[3] * 0.8f;
	trap_R_SetColor( shadow );
	CG_WheelChars( x + off, y + off, cw, ch, s );
	trap_R_SetColor( color );
	CG_WheelChars( x, y, cw, ch, s );
	trap_R_SetColor( NULL );
}

/*
=================
CG_WheelAmmoFull

Ammo that counts as a full bar
=================
*/
static int CG_WheelAmmoFull( int w ) {
	switch ( w ) {
	case WP_MACHINEGUN:			return 150;
	case WP_SHOTGUN:			return 25;
	case WP_GRENADE_LAUNCHER:	return 25;
	case WP_ROCKET_LAUNCHER:	return 25;
	case WP_LIGHTNING:			return 150;
	case WP_RAILGUN:			return 25;
	case WP_PLASMAGUN:			return 150;
	case WP_BFG:				return 25;
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	case WP_NAILGUN:			return 50;
	case WP_PROX_LAUNCHER:		return 20;
	case WP_CHAINGUN:			return 200;
#endif
	default:					return 100;
	}
}

/*
=================
CG_DrawWeaponWheel
=================
*/
void CG_DrawWeaponWheel( void ) {
	int		list[WP_NUM_WEAPONS];
	int		n, i, k, w, hover, step, ammo;
	float	H, cx, cy, rOut, rIn, rIcon, icon, size, half, a, x, y, len, pr, textH;
	float	open, dt, amt, f, alpha;
	vec4_t	col, glow;
	const char	*name;

	dt = ( cg.time - wheelLastDraw ) * 0.001f;
	wheelLastDraw = cg.time;
	if ( dt < 0 || dt > 0.1f ) {
		dt = 0.1f;
	}
	if ( !CG_WheelUsable() || cg.showScores ) {
		return;
	}
	// open: ease out over WHEEL_OPEN_MSEC, closing: shrink away over WHEEL_CLOSE_MSEC
	if ( cg.wheelOpen ) {
		open = CG_Clamp01( (float)( cg.time - cg.wheelOpenTime ) / WHEEL_OPEN_MSEC );
		open = 1.0f - ( 1.0f - open ) * ( 1.0f - open ) * ( 1.0f - open );
	} else if ( wheelCloseTime && cg.time - wheelCloseTime < WHEEL_CLOSE_MSEC ) {
		open = 1.0f - (float)( cg.time - wheelCloseTime ) / WHEEL_CLOSE_MSEC;
	} else {
		return;
	}
	if ( cg.wheelOpen ) {
		CG_WheelPoll();
	}
	n = CG_WheelList( list );
	if ( !n ) {
		return;
	}
	hover = cg.wheelOpen ? CG_WheelHover( n ) : wheelCloseSlot;
	alpha = open;

	// hover tick
	w = hover >= 0 ? list[hover] : WP_NONE;
	if ( cg.wheelOpen && w != wheelHoverWeapon ) {
		if ( w != WP_NONE ) {
			trap_S_StartLocalSound( wheelTickSound, CHAN_LOCAL_SOUND );
		}
		wheelHoverWeapon = w;
	}

	H = cgs.glconfig.vidHeight;
	cx = cgs.glconfig.vidWidth * 0.5f;
	cy = H * 0.5f;
	f = 0.8f + 0.2f * open;			// the whole wheel scales in
	rOut = H * 0.25f * f;
	rIn = H * 0.095f * f;
	rIcon = H * 0.172f * f;
	half = M_PI / n;
	icon = 2 * M_PI * rIcon / n * 0.8f;		// fit the slice when the wheel is full
	if ( icon > H * 0.078f * f ) {
		icon = H * 0.078f * f;
	}
	textH = H * 0.019f;
	step = (int)( H / 600 );
	if ( step < 2 ) {
		step = 2;
	}

	// dark ring with a lighter hole for the name
	col[0] = col[1] = col[2] = 0; col[3] = 0.4f * alpha;
	trap_R_SetColor( col );
	CG_WheelAnnulus( cx, cy, rIn, 0, step );
	col[3] = 0.6f * alpha;
	trap_R_SetColor( col );
	CG_WheelAnnulus( cx, cy, rOut, rIn, step );
	// thin accent rim
	Vector4Copy( wheelAccent, col );
	col[3] = 0.25f * alpha;
	trap_R_SetColor( col );
	CG_WheelAnnulus( cx, cy, rOut, rOut - H * 0.003f, 1 );

	// highlighted slice: glow behind the icon, a band along the rim and a spoke to the centre
	if ( hover >= 0 ) {
		a = hover * 2 * half;
		x = cx + sin( a ) * rIcon;
		y = cy - cos( a ) * rIcon;
		if ( CG_WheelSelectable( list[hover] ) ) {
			Vector4Copy( wheelAccent, col );
		} else {
			col[0] = 0.6f; col[1] = 0.2f; col[2] = 0.15f; col[3] = 1.0f;
		}
		glow[0] = col[0] * 0.55f * alpha; glow[1] = col[1] * 0.55f * alpha; glow[2] = col[2] * 0.55f * alpha; glow[3] = 1.0f;
		trap_R_SetColor( glow );	// additive shader: rgb sets the strength
		size = icon * 2.9f;
		trap_R_DrawStretchPic( x - size * 0.5f, y - size * 0.5f, size, size, 0, 0, 1, 1, cgs.media.fxGlowShader );
		col[3] = 0.95f * alpha;
		trap_R_SetColor( col );
		size = H * 0.013f;
		CG_WheelArc( cx, cy, rOut - size * 0.9f, a - half * 0.94f, a + half * 0.94f, size );
		col[3] = 0.35f * alpha;
		trap_R_SetColor( col );
		CG_WheelArc( cx, cy, rIn + size * 0.6f, a - half * 0.94f, a + half * 0.94f, size * 0.5f );
		trap_R_SetColor( NULL );
	}

	// icons with ammo bars underneath; the hovered one last so it sits on top
	for ( k = 0 ; k <= n ; k++ ) {
		if ( k == n ) {
			i = hover;
		} else {
			i = k;
		}
		if ( i < 0 || ( k < n && i == hover ) ) {
			continue;
		}
		w = list[i];
		CG_RegisterWeapon( w );
		// smooth hover growth per weapon
		amt = wheelHoverAmt[w];
		amt += ( ( i == hover ? 1.0f : 0.0f ) - amt ) * CG_Clamp01( dt * 18 );
		wheelHoverAmt[w] = amt;

		a = i * 2 * half;
		x = cx + sin( a ) * ( rIcon + amt * H * 0.008f );
		y = cy - cos( a ) * ( rIcon + amt * H * 0.008f );
		size = icon * ( 1.0f + 0.38f * amt );

		// descent arsenal: an evolved weapon glows in its colour behind the icon
		if ( CG_DS_ArsenalEvoColor( w, glow ) ) {
			pr = 0.45f + 0.15f * sin( cg.time * 0.006f );
			glow[0] *= pr * alpha; glow[1] *= pr * alpha; glow[2] *= pr * alpha; glow[3] = 1.0f;
			trap_R_SetColor( glow );
			trap_R_DrawStretchPic( x - size, y - size, size * 2, size * 2, 0, 0, 1, 1, cgs.media.fxGlowShader );
		}
		if ( !CG_WheelSelectable( w ) ) {
			col[0] = col[1] = col[2] = 0.45f; col[3] = 0.4f;
		} else {
			col[0] = col[1] = col[2] = 0.82f + 0.18f * amt; col[3] = 0.8f + 0.2f * amt;
		}
		col[3] *= alpha;
		trap_R_SetColor( col );
		trap_R_DrawStretchPic( x - size * 0.5f, y - size * 0.5f, size, size, 0, 0, 1, 1, cg_weapons[w].weaponIcon );
		// descent arsenal: the weapon level, small at the icon's top right
		if ( CG_DS_ArsenalLevel( w ) > 0 ) {
			if ( !CG_DS_ArsenalEvoColor( w, col ) ) {
				col[0] = 1.0f; col[1] = 0.85f; col[2] = 0.45f;
			}
			col[3] = ( 0.75f + 0.25f * amt ) * alpha;
			CG_WheelString( x + size * 0.5f, y - size * 0.62f, textH * 0.9f, va( "%i", CG_DS_ArsenalLevel( w ) ), col );
		}
		if ( !cg.snap->ps.ammo[w] ) {
			col[0] = col[1] = col[2] = 1.0f; col[3] = 0.6f * alpha;
			trap_R_SetColor( col );
			trap_R_DrawStretchPic( x - size * 0.5f, y - size * 0.5f, size, size, 0, 0, 1, 1, cgs.media.noammoShader );
		}
		// the weapon in hand: a small accent dot above it
		if ( w == cg.weaponSelect ) {
			Vector4Copy( wheelAccent, col );
			col[3] = alpha;
			trap_R_SetColor( col );
			CG_WheelAnnulus( x, y - size * 0.62f, H * 0.0045f, 0, 1 );
		}
		trap_R_SetColor( NULL );

		ammo = cg.snap->ps.ammo[w];
		if ( ammo >= 0 ) {
			// ammo bar
			f = CG_Clamp01( (float)ammo / CG_WheelAmmoFull( w ) );
			pr = size * 0.8f;
			col[0] = col[1] = col[2] = 0; col[3] = 0.6f * alpha;
			trap_R_SetColor( col );
			trap_R_DrawStretchPic( x - pr * 0.5f - 1, y + size * 0.5f + H * 0.002f - 1, pr + 2, H * 0.006f + 2, 0, 0, 0, 0, cgs.media.whiteShader );
			if ( f > 0.5f ) {
				col[0] = 0.4f + 0.6f * ( 1.0f - f ) * 2; col[1] = 0.95f; col[2] = 0.35f;
			} else if ( f > 0.2f ) {
				col[0] = 1.0f; col[1] = 0.85f; col[2] = 0.25f;
			} else {
				col[0] = 1.0f; col[1] = 0.3f; col[2] = 0.2f;
			}
			col[3] = 0.95f * alpha;
			trap_R_SetColor( col );
			trap_R_DrawStretchPic( x - pr * 0.5f, y + size * 0.5f + H * 0.002f, pr * f, H * 0.006f, 0, 0, 0, 0, cgs.media.whiteShader );
			trap_R_SetColor( NULL );
			// the count
			if ( !ammo ) {
				col[0] = 1.0f; col[1] = 0.3f; col[2] = 0.25f; col[3] = 0.8f;
			} else {
				col[0] = col[1] = col[2] = 1.0f; col[3] = 0.7f + 0.3f * amt;
			}
			col[3] *= alpha;
			CG_WheelString( x, y + size * 0.5f + H * 0.011f, textH * ( 0.9f + 0.2f * amt ), va( "%i", ammo ), col );
		}
	}

	// pointer: a dot inside the hole in the mouse / stick direction
	len = sqrt( cg.wheelX * cg.wheelX + cg.wheelY * cg.wheelY );
	if ( len > 0.001f ) {
		pr = ( rIn - H * 0.014f ) * len / WHEEL_LIMIT;
		x = cx + cg.wheelX / len * pr;
		y = cy + cg.wheelY / len * pr;
	} else {
		x = cx;
		y = cy;
	}
	if ( hover >= 0 ) {
		Vector4Copy( wheelAccent, col );
	} else {
		col[0] = col[1] = col[2] = 1.0f; col[3] = 0.5f;
	}
	col[3] *= alpha;
	trap_R_SetColor( col );
	CG_WheelAnnulus( x, y, H * 0.008f, 0, 1 );
	trap_R_SetColor( NULL );

	// name in the centre: the hovered weapon, else the one in hand
	w = hover >= 0 ? list[hover] : cg.weaponSelect;
	if ( w > WP_NONE && w < WP_NUM_WEAPONS ) {
		CG_RegisterWeapon( w );
		name = cg_weapons[w].item ? cg_weapons[w].item->pickup_name : NULL;
		if ( name ) {
			if ( hover >= 0 ) {
				col[0] = col[1] = col[2] = 1.0f; col[3] = 1.0f;
			} else {
				col[0] = col[1] = col[2] = 0.8f; col[3] = 0.6f;
			}
			col[3] *= alpha;
			CG_WheelString( cx, cy - textH * 1.2f, textH * 1.25f, name, col );
		}
		if ( hover >= 0 && !CG_WheelSelectable( w ) ) {
			col[0] = 1.0f; col[1] = 0.3f; col[2] = 0.25f; col[3] = alpha;
			CG_WheelString( cx, cy + textH * 0.3f, textH, "NO AMMO", col );
		} else if ( hover >= 0 && cg.snap->ps.ammo[w] > 0 ) {
			col[0] = col[1] = col[2] = 0.75f; col[3] = 0.8f * alpha;
			CG_WheelString( cx, cy + textH * 0.3f, textH * 0.85f, va( "%i", cg.snap->ps.ammo[w] ), col );
		}
	}
}
