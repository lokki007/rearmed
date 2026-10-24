"""v2 cgame: switches / bounty from CS_REARMED, medals + warn + bounty banners, announcer toggle,
bounty marker, dash meter, damage direction, low health pulse, widescreen HUD anchors,
wall run roll + sound, headshot / armor break hit feedback, stacking damage numbers,
overcharged rail trail, review fixes (shake when dead, kick at 0.5, cvar clamps, spark trace budget)."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import patch
from patch import sub
if len(sys.argv) > 1 and sys.argv[1] == 'resume':
    patch.resume(__file__)
    sub = patch.sub

ROOT = pathlib.Path(__file__).resolve().parents[2] / 'ioq3'
c = lambda f: str(ROOT / 'code' / 'cgame' / f)
g = lambda f: str(ROOT / 'code' / 'game' / f)

# ---------- shared: armor break flag ----------
sub(g('bg_public.h'), '''#define HIT_HEADSHOT	4
''', '''#define HIT_HEADSHOT	4
#define HIT_ARMORBREAK	8
''')
sub(g('g_combat.c'), '''	// save some from armor
	asave = CheckArmor (targ, take, dflags);
	take -= asave;
''', '''	// save some from armor
	asave = CheckArmor (targ, take, dflags);
	take -= asave;
	if ( asave > 0 && client && client->ps.stats[STAT_ARMOR] <= 0 ) {
		hitFlags |= HIT_ARMORBREAK;
	}
''')

# ---------- cg_local.h ----------
H = c('cg_local.h')
sub(H, '''	char		rmBanner[128];		// rearmed announcement
	int			rmBannerTime;
	int			rmBannerKind;
''', '''	char		rmBanner[128];		// rearmed announcement
	int			rmBannerTime;
	int			rmBannerKind;
	char		rmMedal[64];		// medal / personal callout under the crosshair
	int			rmMedalTime;
	int			rmMedalKind;
	float		hudShift;			// widescreen HUD: virtual x offset for the element being drawn
	qboolean	hitMarkerHeadshot;
	float		rmDamageYaw[4];		// damage direction indicators: world yaw towards the attacker
	int			rmDamageTime[4];
	float		rmWallRoll;			// wall run camera roll
	int			rmDashFullTime;		// when the dash charges were last full
''')
sub(H, '''	int				rmJuggernaut;
''', '''	int				rmJuggernaut;
	int				rmSwitches;		// RS_ bits
	qboolean		rmBounty[MAX_CLIENTS];
''')
sub(H, '''	sfxHandle_t	annInfo;
''', '''	sfxHandle_t	annInfo;
	sfxHandle_t	annMedal;
	sfxHandle_t	annBounty;
	sfxHandle_t	annWarn;
	sfxHandle_t	armorBreakSound;
	sfxHandle_t	wallRunSound;
	sfxHandle_t	hitSounds2[3];
	qhandle_t	edgeShader;
	qhandle_t	dmgDirShaders[16];
''')
sub(H, '''extern	vmCvar_t		cg_weaponKick;
''', '''extern	vmCvar_t		cg_weaponKick;
extern	vmCvar_t		cg_damageIndicator;
extern	vmCvar_t		cg_dashHud;
extern	vmCvar_t		cg_rearmAnnouncer;
extern	vmCvar_t		cg_hudWide;
extern	vmCvar_t		cg_lowHealthFX;
''')
sub(H, '''void CG_DrawRearmed( void );
''', '''void CG_DrawRearmed( void );
void CG_RearmDrawOverlays( void );
void CG_RearmDamageDir( float yaw );
void CG_HudAnchor( int side );
void CG_OverchargeTrail( vec3_t start, vec3_t end );
float CG_Clamp01( float v );
extern localEntity_t	cg_activeLocalEntities;
''')

# ---------- cg_main.c: cvars ----------
M = c('cg_main.c')
sub(M, 'vmCvar_t\tcg_hitMarkers;\n', 'vmCvar_t\tcg_hitMarkers;\nvmCvar_t\tcg_damageIndicator;\nvmCvar_t\tcg_dashHud;\nvmCvar_t\tcg_rearmAnnouncer;\nvmCvar_t\tcg_hudWide;\nvmCvar_t\tcg_lowHealthFX;\n')
sub(M, '''	{ &cg_hitMarkers, "cg_hitMarkers", "1", CVAR_ARCHIVE },
''', '''	{ &cg_hitMarkers, "cg_hitMarkers", "1", CVAR_ARCHIVE },
	{ &cg_damageIndicator, "cg_damageIndicator", "1", CVAR_ARCHIVE },
	{ &cg_dashHud, "cg_dashHud", "1", CVAR_ARCHIVE },
	{ &cg_rearmAnnouncer, "cg_rearmAnnouncer", "1", CVAR_ARCHIVE },
	{ &cg_hudWide, "cg_hudWide", "1", CVAR_ARCHIVE },
	{ &cg_lowHealthFX, "cg_lowHealthFX", "1", CVAR_ARCHIVE },
''')

# ---------- cg_drawtools.c: widescreen HUD anchor ----------
T = c('cg_drawtools.c')
sub(T, '''	// 4:3 virtual screen, centered on wide displays
	*x = *x * cgs.screenXScale + cgs.screenXBias;''', '''	// 4:3 virtual screen, centered on wide displays (corner elements may be anchored outwards)
	*x = ( *x + cg.hudShift ) * cgs.screenXScale + cgs.screenXBias;''')
sub(T, '''	ax = x * cgs.screenXScale + cgs.screenXBias;
	ay = y * cgs.screenYScale;''', '''	ax = ( x + cg.hudShift ) * cgs.screenXScale + cgs.screenXBias;
	ay = y * cgs.screenYScale;''', count=2)
sub(T, '''/*
================
CG_FillRect
''', '''/*
================
CG_HudAnchor

cg_hudWide: move the HUD element about to be drawn from the 4:3 box towards
the screen edge on wide displays. 1 = the 16:9 area, 2 = the very edge.
side: -1 left, 1 right, 0 back to centered
================
*/
void CG_HudAnchor( int side ) {
	float	full, wide;

	cg.hudShift = 0;
	if ( !side || cg_hudWide.integer <= 0 || cgs.screenXBias <= 0 ) {
		return;
	}
	full = cgs.screenXBias / cgs.screenXScale;
	wide = ( SCREEN_HEIGHT * 16.0f / 9.0f - SCREEN_WIDTH ) * 0.5f;
	if ( cg_hudWide.integer == 1 && full > wide ) {
		full = wide;
	}
	cg.hudShift = side * full;
}

float CG_Clamp01( float v ) {
	return v < 0 ? 0 : v > 1 ? 1 : v;
}

/*
================
CG_FillRect
''')

# ---------- cg_draw.c ----------
D = c('cg_draw.c')
sub(D, '''	CG_DrawLagometer();

#ifdef MISSIONPACK
	if (!cg_paused.integer) {
		CG_DrawUpperRight(stereoFrame);
	}
#else
	CG_DrawUpperRight(stereoFrame);
#endif

#ifndef MISSIONPACK
	CG_DrawLowerRight();
	CG_DrawLowerLeft();
#endif
''', '''	CG_HudAnchor( 1 );
	CG_DrawLagometer();

#ifdef MISSIONPACK
	if (!cg_paused.integer) {
		CG_DrawUpperRight(stereoFrame);
	}
#else
	CG_DrawUpperRight(stereoFrame);
#endif

#ifndef MISSIONPACK
	CG_DrawLowerRight();
	CG_HudAnchor( -1 );
	CG_DrawLowerLeft();
#endif
	CG_HudAnchor( 0 );
''')
sub(D, '''	if ( cgs.gametype >= GT_TEAM ) {
#ifndef MISSIONPACK
		CG_DrawTeamInfo();
#endif
	}
''', '''	if ( cgs.gametype >= GT_TEAM ) {
#ifndef MISSIONPACK
		CG_HudAnchor( -1 );
		CG_DrawTeamInfo();
		CG_HudAnchor( 0 );
#endif
	}
''')
sub(D, '''			CG_DrawHitFeedback();
			CG_DrawRearmed();''', '''			CG_DrawHitFeedback();
			CG_DrawRearmed();
			CG_RearmDrawOverlays();''')
sub(D, '''		if ( cg.hitMarkerKill ) {
			c[0] = 1.0f; c[1] = 0.2f; c[2] = 0.15f; c[3] = 1.0f - f;
		} else {''', '''		if ( cg.hitMarkerKill ) {
			c[0] = 1.0f; c[1] = 0.2f; c[2] = 0.15f; c[3] = 1.0f - f;
		} else if ( cg.hitMarkerHeadshot ) {
			c[0] = 1.0f; c[1] = 0.8f; c[2] = 0.2f; c[3] = 1.0f - f * f;
		} else {''')

# ---------- cg_view.c: kick at any strength, wall run roll ----------
V = c('cg_view.c')
sub(V, '''	// weapon kick
	if ( cg_weaponKick.integer && cg.kickTime ) {
		ratio = ( cg.time - cg.kickTime ) / 200.0f;
		if ( ratio < 1.0f ) {
			ratio = ratio < 0.15f ? ratio / 0.15f : 1.0f - ( ratio - 0.15f ) / 0.85f;
			angles[PITCH] -= ratio * cg.kickPitch * cg_weaponKick.value;
			angles[YAW] += ratio * cg.kickYaw * cg_weaponKick.value;
		}
	}
''', '''	// weapon kick
	if ( cg_weaponKick.value > 0 && cg.kickTime ) {
		ratio = ( cg.time - cg.kickTime ) / 200.0f;
		if ( ratio < 1.0f ) {
			ratio = ratio < 0.15f ? ratio / 0.15f : 1.0f - ( ratio - 0.15f ) / 0.85f;
			angles[PITCH] -= ratio * cg.kickPitch * CG_Clamp01( cg_weaponKick.value );
			angles[YAW] += ratio * cg.kickYaw * CG_Clamp01( cg_weaponKick.value );
		}
	}

	// wall run: lean away from the wall
	{
		int		rm = cg.predictedPlayerState.stats[STAT_RMFLAGS];
		float	target = 0, step;

		if ( rm & RMS_WALLRUN ) {
			target = ( rm & RMS_WALLRUN_LEFT ) ? 9.0f : -9.0f;
		}
		step = cg.frametime * 0.012f;
		if ( step > 1.0f ) {
			step = 1.0f;
		}
		cg.rmWallRoll += ( target - cg.rmWallRoll ) * step;
		angles[ROLL] += cg.rmWallRoll;
	}
''')
sub(c('cg_weapons.c'), '''	if ( cg_weaponKick.integer && cg.kickTime && cg.time - cg.kickTime < 220 ) {''',
    '''	if ( cg_weaponKick.value > 0 && cg.kickTime && cg.time - cg.kickTime < 220 ) {''')
sub(c('cg_weapons.c'), '''		float	k = ( f < 0.15f ? f / 0.15f : 1.0f - ( f - 0.15f ) / 0.85f ) * cg.kickPitch;''',
    '''		float	k = ( f < 0.15f ? f / 0.15f : 1.0f - ( f - 0.15f ) / 0.85f ) * cg.kickPitch * CG_Clamp01( cg_weaponKick.value );''')

# ---------- cg_event.c ----------
E = c('cg_event.c')
sub(E, '''	case EV_DASH:
		DEBUGNAME("EV_DASH");''', '''	case EV_WALLRUN:
		DEBUGNAME("EV_WALLRUN");
		trap_S_StartSound( NULL, es->number, CHAN_BODY, cgs.media.wallRunSound );
		CG_MoveTechPuff( cent, 1 );
		break;
	case EV_DASH:
		DEBUGNAME("EV_DASH");''')
sub(E, '''		CG_RailTrail(ci, es->origin2, es->pos.trBase);
''', '''		CG_RailTrail(ci, es->origin2, es->pos.trBase);
		if ( es->generic1 & 1 ) {
			CG_OverchargeTrail( es->origin2, es->pos.trBase );
		}
''')

# ---------- cg_playerstate.c: damage direction ----------
sub(c('cg_playerstate.c'), '''		AngleVectors( angles, dir, NULL, NULL );
		VectorSubtract( vec3_origin, dir, dir );
''', '''		AngleVectors( angles, dir, NULL, NULL );
		VectorSubtract( vec3_origin, dir, dir );
		CG_RearmDamageDir( yaw + 180 );
''')

# ---------- cg_fx.c ----------
F = c('cg_fx.c')
# shake: clamp, and never on the death camera
sub(F, '''	amp = amplitude * ( 1.0f - dist / radius ) * cg_screenShake.value;''',
    '''	amp = amplitude * ( 1.0f - dist / radius ) * CG_Clamp01( cg_screenShake.value );''')
sub(F, '''void CG_ApplyScreenShake( void ) {
	float	f, amp;

	if ( !cg.shakeTime ) {
		return;
	}''', '''void CG_ApplyScreenShake( void ) {
	float	f, amp;

	if ( !cg.shakeTime ) {
		return;
	}
	if ( cg.snap && cg.snap->ps.stats[STAT_HEALTH] <= 0 ) {
		cg.shakeTime = 0;
		return;
	}''')
# sparks: a per-frame budget of collision traces
sub(F, '''	BG_EvaluateTrajectory( &le->pos, cg.time, newOrigin );
	CG_Trace( &trace, re->origin, NULL, NULL, newOrigin, -1, CONTENTS_SOLID );
	if ( trace.fraction < 1.0f ) {''', '''	BG_EvaluateTrajectory( &le->pos, cg.time, newOrigin );
	// a budget of collision traces per frame; past it sparks just fly (they are short lived)
	if ( traceFrame != cg.clientFrame ) {
		traceFrame = cg.clientFrame;
		tracesLeft = SPARK_TRACE_BUDGET;
	}
	if ( tracesLeft > 0 ) {
		tracesLeft--;
		CG_Trace( &trace, re->origin, NULL, NULL, newOrigin, -1, CONTENTS_SOLID );
	} else {
		trace.fraction = 1.0f;
	}
	if ( trace.fraction < 1.0f ) {''')
sub(F, '''void CG_AddFxSpark( localEntity_t *le ) {
	refEntity_t	*re = &le->refEntity;
	vec3_t		newOrigin, vel;
	trace_t		trace;
	float		fade, speed, dot;
''', '''#define SPARK_TRACE_BUDGET	160

void CG_AddFxSpark( localEntity_t *le ) {
	static int	traceFrame, tracesLeft;
	refEntity_t	*re = &le->refEntity;
	vec3_t		newOrigin, vel;
	trace_t		trace;
	float		fade, speed, dot;
''')
# hit confirm: headshot / armor break feedback, sound variety, stacking numbers
sub(F, '''	if ( es->generic1 & HIT_KILL ) {
		trap_S_StartLocalSound( cgs.media.killConfirmSound, CHAN_LOCAL_SOUND );
	} else if ( cg_hitMarkers.integer ) {
		trap_S_StartLocalSound( cgs.media.hitSounds[damage < 25 ? 0 : damage < 70 ? 1 : 2], CHAN_LOCAL_SOUND );
	}
	cg.hitMarkerTime = cg.time;
	cg.hitMarkerDamage = damage;
	cg.hitMarkerKill = ( es->generic1 & HIT_KILL ) != 0;
''', '''	if ( es->generic1 & HIT_KILL ) {
		trap_S_StartLocalSound( cgs.media.killConfirmSound, CHAN_LOCAL_SOUND );
	} else if ( cg_hitMarkers.integer ) {
		tier = damage < 25 ? 0 : damage < 70 ? 1 : 2;
		trap_S_StartLocalSound( ( rand() & 1 ) ? cgs.media.hitSounds2[tier] : cgs.media.hitSounds[tier], CHAN_LOCAL_SOUND );
	}
	if ( es->generic1 & HIT_ARMORBREAK ) {
		trap_S_StartLocalSound( cgs.media.armorBreakSound, CHAN_AUTO );
	}
	cg.hitMarkerTime = cg.time;
	cg.hitMarkerDamage = damage;
	cg.hitMarkerKill = ( es->generic1 & HIT_KILL ) != 0;
	cg.hitMarkerHeadshot = ( es->generic1 & HIT_HEADSHOT ) != 0;
''')
sub(F, '''	if ( !cg_damageNumbers.integer ) {
		return;
	}
	le = CG_AllocLocalEntity();''', '''	if ( !cg_damageNumbers.integer ) {
		return;
	}
	// stacking: keep adding to the number already floating over this target
	if ( cg_damageNumbers.integer >= 2 ) {
		for ( le = cg_activeLocalEntities.next ; le != &cg_activeLocalEntities ; le = le->next ) {
			if ( le->leType == LE_DAMAGE_NUMBER && le->leMarkType == es->otherEntityNum + 1
				&& cg.time - le->startTime < 700 ) {
				le->radius += damage;
				le->leFlags |= es->generic1;
				le->startTime = cg.time;
				le->endTime = cg.time + 900;
				VectorCopy( es->pos.trBase, le->pos.trBase );
				return;
			}
		}
	}
	le = CG_AllocLocalEntity();
	le->leMarkType = es->otherEntityNum + 1;''')
sub(F, '''	entityState_t	*es = &cent->currentState;
	localEntity_t	*le;
	int				damage = es->eventParm;
''', '''	entityState_t	*es = &cent->currentState;
	localEntity_t	*le;
	int				damage = es->eventParm;
	int				tier;
''')
# headshot numbers are gold
sub(F, '''	if ( le->leFlags & HIT_KILL ) {
		re->shaderRGBA[1] = re->shaderRGBA[2] = 30;
	}''', '''	if ( le->leFlags & HIT_KILL ) {
		re->shaderRGBA[1] = re->shaderRGBA[2] = 30;
	} else if ( le->leFlags & HIT_HEADSHOT ) {
		re->shaderRGBA[1] = 200;
		re->shaderRGBA[2] = 40;
	}''')
sub(F, '''/*
==================
CG_BFGLaser
''', '''/*
==================
CG_OverchargeTrail

Overcharged rail: hot embers along the beam
==================
*/
void CG_OverchargeTrail( vec3_t start, vec3_t end ) {
	static const vec3_t	hot = { 1.0f, 0.55f, 0.15f };
	vec3_t	dir, pos, vel;
	float	len, d, step;

	if ( !cg_fx.integer ) {
		return;
	}
	VectorSubtract( end, start, dir );
	len = VectorNormalize( dir );
	step = len / 40.0f;
	if ( step < 24 ) {
		step = 24;
	}
	for ( d = 16 ; d < len ; d += step ) {
		VectorMA( start, d, dir, pos );
		VectorSet( vel, crandom() * 40, crandom() * 40, 20 + random() * 40 );
		CG_FxSpark( pos, vel, hot, 500 + random() * 400, 0, qtrue, 5 + random() * 3 );
	}
}

/*
==================
CG_BFGLaser
''')
print('v2 cg ok')
