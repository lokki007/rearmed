"""Fun weapons, client side: hitmarkers, damage numbers, airshot callout, weapon kick + gun recoil,
singularity / BFG laser / ricochet visuals, juicier gibs."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from patch import sub

G = pathlib.Path(__file__).resolve().parents[2] / 'ioq3' / 'code'
cg = lambda f: str(G / 'cgame' / f)

# --- state, media, cvars -----------------------------------------------------------------
sub(cg('cg_local.h'), '\tqboolean\timpactAlt;\t\t\t// the impact being drawn is from an alt-fire projectile\n',
    '\tqboolean\timpactAlt;\t\t\t// the impact being drawn is from an alt-fire projectile\n'
    '\tint\t\t\thitMarkerTime;\t\t// hit confirm feedback\n'
    '\tint\t\t\thitMarkerDamage;\n'
    '\tqboolean\thitMarkerKill;\n'
    '\tint\t\t\tairshotTime;\n'
    '\tint\t\t\tkickTime;\t\t\t// weapon kick of the last shot\n'
    '\tfloat\t\tkickPitch;\n'
    '\tfloat\t\tkickYaw;\n')
sub(cg('cg_local.h'), '\tqhandle_t\tfxRingShader;\n', '''	qhandle_t	fxRingShader;
	qhandle_t	hitMarkerShader;
	sfxHandle_t	hitSounds[3];
	sfxHandle_t	killConfirmSound;
	sfxHandle_t	airshotSound;
	sfxHandle_t	singularitySound;
	sfxHandle_t	bfgZapSound;
''')
sub(cg('cg_local.h'), 'extern\tvmCvar_t\t\tcg_screenShake;\n',
    'extern\tvmCvar_t\t\tcg_screenShake;\nextern\tvmCvar_t\t\tcg_hitMarkers;\nextern\tvmCvar_t\t\tcg_damageNumbers;\nextern\tvmCvar_t\t\tcg_weaponKick;\n')
sub(cg('cg_local.h'), '''	LE_FX_RING,
''', '''	LE_FX_RING,
	LE_DAMAGE_NUMBER,
''')
sub(cg('cg_local.h'), 'void CG_ApplyScreenShake( void );\n', '''void CG_ApplyScreenShake( void );
void CG_HitConfirm( centity_t *cent );
void CG_AddDamageNumber( localEntity_t *le );
void CG_BFGLaser( vec3_t start, vec3_t end );
void CG_RicochetFX( vec3_t origin );
void CG_GibFX( vec3_t origin );
void CG_WeaponKick( int weapon );
''')
sub(cg('cg_main.c'), 'vmCvar_t\tcg_screenShake;\n', 'vmCvar_t\tcg_screenShake;\nvmCvar_t\tcg_hitMarkers;\nvmCvar_t\tcg_damageNumbers;\nvmCvar_t\tcg_weaponKick;\n')
sub(cg('cg_main.c'), '\t{ &cg_screenShake, "cg_screenShake", "1", CVAR_ARCHIVE },\n',
    '\t{ &cg_screenShake, "cg_screenShake", "1", CVAR_ARCHIVE },\n'
    '\t{ &cg_hitMarkers, "cg_hitMarkers", "1", CVAR_ARCHIVE },\n'
    '\t{ &cg_damageNumbers, "cg_damageNumbers", "1", CVAR_ARCHIVE },\n'
    '\t{ &cg_weaponKick, "cg_weaponKick", "1", CVAR_ARCHIVE },\n')
sub(cg('cg_main.c'), '\tcgs.media.fxRingShader = trap_R_RegisterShader( "rearmed/ring" );\n',
    '\tcgs.media.fxRingShader = trap_R_RegisterShader( "rearmed/ring" );\n'
    '\tcgs.media.hitMarkerShader = trap_R_RegisterShaderNoMip( "rearmed/hitmarker" );\n')
sub(cg('cg_main.c'), '\tcgs.media.hookFireSound = trap_S_RegisterSound("sound/weapons/rocket/rocklf1a.wav", qfalse);\n',
    '\tcgs.media.hookFireSound = trap_S_RegisterSound("sound/weapons/rocket/rocklf1a.wav", qfalse);\n'
    '\tcgs.media.hitSounds[0] = trap_S_RegisterSound("sound/rearmed/hit_low.wav", qfalse);\n'
    '\tcgs.media.hitSounds[1] = trap_S_RegisterSound("sound/rearmed/hit_mid.wav", qfalse);\n'
    '\tcgs.media.hitSounds[2] = trap_S_RegisterSound("sound/rearmed/hit_high.wav", qfalse);\n'
    '\tcgs.media.killConfirmSound = trap_S_RegisterSound("sound/rearmed/kill.wav", qfalse);\n'
    '\tcgs.media.airshotSound = trap_S_RegisterSound("sound/rearmed/airshot.wav", qfalse);\n'
    '\tcgs.media.singularitySound = trap_S_RegisterSound("sound/rearmed/singularity.wav", qfalse);\n'
    '\tcgs.media.bfgZapSound = trap_S_RegisterSound("sound/rearmed/bfgzap.wav", qfalse);\n')
sub(cg('cg_localents.c'), '''		case LE_FX_RING:
			CG_AddFxRing( le );
			break;
''', '''		case LE_FX_RING:
			CG_AddFxRing( le );
			break;

		case LE_DAMAGE_NUMBER:
			CG_AddDamageNumber( le );
			break;
''')

# --- events -------------------------------------------------------------------------------
sub(cg('cg_event.c'), '''	case EV_ARC:
		DEBUGNAME("EV_ARC");
		CG_ArcBeam( es->pos.trBase, es->origin2 );
		break;
''', '''	case EV_ARC:
		DEBUGNAME("EV_ARC");
		CG_ArcBeam( es->pos.trBase, es->origin2 );
		break;

	case EV_HITCONFIRM:
		DEBUGNAME("EV_HITCONFIRM");
		CG_HitConfirm( cent );
		break;

	case EV_BFG_LASER:
		DEBUGNAME("EV_BFG_LASER");
		CG_BFGLaser( es->pos.trBase, es->origin2 );
		break;
''')
sub(cg('cg_event.c'), '''	case EV_GRENADE_BOUNCE:
		DEBUGNAME("EV_GRENADE_BOUNCE");
		if ( rand() & 1 ) {''', '''	case EV_GRENADE_BOUNCE:
		DEBUGNAME("EV_GRENADE_BOUNCE");
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
		if ( es->weapon == WP_NAILGUN ) {
			CG_RicochetFX( cent->lerpOrigin );
			break;
		}
#endif
		if ( rand() & 1 ) {''')
# the hitmarker replaces the stock hit beep
sub(cg('cg_playerstate.c'), '''	if ( ps->persistant[PERS_HITS] > ops->persistant[PERS_HITS] ) {''',
    '''	if ( ps->persistant[PERS_HITS] > ops->persistant[PERS_HITS] && cg_hitMarkers.integer ) {
		// EV_HITCONFIRM plays the feedback
	} else if ( ps->persistant[PERS_HITS] > ops->persistant[PERS_HITS] ) {''')

# --- weapon kick + gun recoil ------------------------------------------------------------------
sub(cg('cg_weapons.c'), '''	// mark the entity as muzzle flashing, so when it is added it will
	// append the flash to the weapon model
	cent->muzzleFlashTime = cg.time;
''', '''	// mark the entity as muzzle flashing, so when it is added it will
	// append the flash to the weapon model
	cent->muzzleFlashTime = cg.time;

	if ( ent->number == cg.predictedPlayerState.clientNum ) {
		CG_WeaponKick( ent->weapon );
	}
''')
sub(cg('cg_weapons.c'), '''	VectorMA( hand.origin, (cg_gun_z.value+fovOffset), cg.refdef.viewaxis[2], hand.origin );
''', '''	VectorMA( hand.origin, (cg_gun_z.value+fovOffset), cg.refdef.viewaxis[2], hand.origin );

	// recoil: the gun slams back and settles
	if ( cg_weaponKick.integer && cg.kickTime && cg.time - cg.kickTime < 220 ) {
		float	f = ( cg.time - cg.kickTime ) / 220.0f;
		float	k = ( f < 0.15f ? f / 0.15f : 1.0f - ( f - 0.15f ) / 0.85f ) * cg.kickPitch;
		VectorMA( hand.origin, -1.4f * k, cg.refdef.viewaxis[0], hand.origin );
		VectorMA( hand.origin, 0.4f * k, cg.refdef.viewaxis[2], hand.origin );
	}
''')
sub(cg('cg_view.c'), '''	// add pitch based on fall kick
#if 0''', '''	// weapon kick
	if ( cg_weaponKick.integer && cg.kickTime ) {
		ratio = ( cg.time - cg.kickTime ) / 200.0f;
		if ( ratio < 1.0f ) {
			ratio = ratio < 0.15f ? ratio / 0.15f : 1.0f - ( ratio - 0.15f ) / 0.85f;
			angles[PITCH] -= ratio * cg.kickPitch * cg_weaponKick.value;
			angles[YAW] += ratio * cg.kickYaw * cg_weaponKick.value;
		}
	}

	// add pitch based on fall kick
#if 0''')

# --- singularity hum + swirl, chaingun heat -------------------------------------------------------
sub(cg('cg_ents.c'), '''	// add missile sound
	if ( weapon->missileSound ) {
		vec3_t	velocity;''', '''	// the plasma singularity hums instead
	if ( s1->weapon == WP_PLASMAGUN && ( s1->generic1 & ALTFIRE_PROJECTILE ) ) {
		vec3_t	velocity;

		BG_EvaluateTrajectoryDelta( &cent->currentState.pos, cg.time, velocity );
		trap_S_AddLoopingSound( cent->currentState.number, cent->lerpOrigin, velocity, cgs.media.singularitySound );
	} else
	// add missile sound
	if ( weapon->missileSound ) {
		vec3_t	velocity;''')

# --- gibs ---
sub(cg('cg_effects.c'), '''	if ( !cg_blood.integer ) {
		return;
	}

	VectorCopy( playerOrigin, origin );
	velocity[0] = crandom()*GIB_VELOCITY;
	velocity[1] = crandom()*GIB_VELOCITY;
	velocity[2] = GIB_JUMP + crandom()*GIB_VELOCITY;
	if ( rand() & 1 ) {''', '''	if ( !cg_blood.integer ) {
		return;
	}
	CG_GibFX( playerOrigin );

	VectorCopy( playerOrigin, origin );
	velocity[0] = crandom()*GIB_VELOCITY;
	velocity[1] = crandom()*GIB_VELOCITY;
	velocity[2] = GIB_JUMP + crandom()*GIB_VELOCITY;
	if ( rand() & 1 ) {''')

# --- hitmarker + airshot callout next to the headshot one ---
sub(cg('cg_draw.c'), '''	w = CG_DrawStrlen( s ) * SMALLCHAR_WIDTH;
	CG_DrawStringExt( 320 - w / 2, 240 - 36, s, color, qtrue, qtrue, SMALLCHAR_WIDTH, SMALLCHAR_HEIGHT, 0 );
}
''', '''	w = CG_DrawStrlen( s ) * SMALLCHAR_WIDTH;
	CG_DrawStringExt( 320 - w / 2, 240 - 36, s, color, qtrue, qtrue, SMALLCHAR_WIDTH, SMALLCHAR_HEIGHT, 0 );
}

/*
=================
CG_DrawHitFeedback

Hitmarker around the crosshair and the AIRSHOT! callout
=================
*/
#define HITMARKER_TIME		220
#define HITMARKER_KILL_TIME	450
#define AIRSHOT_TIME		1100

static void CG_DrawHitFeedback( void ) {
	float	*color;
	float	size, f;
	int		w, life;
	const char *s = "AIRSHOT!";

	if ( cg.airshotTime && ( color = CG_FadeColor( cg.airshotTime, AIRSHOT_TIME ) ) != NULL ) {
		color[0] = 0.35f;
		color[1] = 0.85f;
		color[2] = 1.0f;
		f = 1.0f + 0.6f * ( 1.0f - (float)( cg.time - cg.airshotTime ) / 150.0f );
		if ( f < 1.0f ) {
			f = 1.0f;
		}
		w = CG_DrawStrlen( s ) * BIGCHAR_WIDTH * f;
		CG_DrawStringExt( 320 - w / 2, 240 - 72, s, color, qtrue, qtrue, BIGCHAR_WIDTH * f, BIGCHAR_HEIGHT * f, 0 );
	}

	if ( !cg_hitMarkers.integer || !cg.hitMarkerTime ) {
		return;
	}
	life = cg.hitMarkerKill ? HITMARKER_KILL_TIME : HITMARKER_TIME;
	f = (float)( cg.time - cg.hitMarkerTime ) / life;
	if ( f >= 1.0f ) {
		return;
	}
	// bigger for big hits, with a little pop at the start
	size = 18 + cg.hitMarkerDamage * 0.08f;
	if ( size > 34 ) {
		size = 34;
	}
	size *= 1.0f + 0.35f * ( 1.0f - f ) * ( 1.0f - f );
	{
		vec4_t	c;
		if ( cg.hitMarkerKill ) {
			Vector4Set( c, 1.0f, 0.2f, 0.15f, 1.0f - f );
		} else {
			Vector4Set( c, 1.0f, 1.0f, 1.0f, 1.0f - f * f );
		}
		trap_R_SetColor( c );
		CG_DrawPic( 320 - size / 2 + cg_crosshairX.integer, 240 - size / 2 + cg_crosshairY.integer, size, size, cgs.media.hitMarkerShader );
		trap_R_SetColor( NULL );
	}
}
''')
sub(cg('cg_draw.c'), '''			CG_DrawHeadshot();''', '''			CG_DrawHeadshot();
			CG_DrawHitFeedback();''')

print('cg ok')
