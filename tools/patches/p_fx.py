"""Hook cg_fx.c (sparks, flashes, rings, screen shake) into the cgame."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from patch import sub

ROOT = pathlib.Path(__file__).resolve().parents[2] / 'ioq3'
cg = lambda f: str(ROOT / 'code' / 'cgame' / f)

sub(str(ROOT / 'cmake' / 'basegame.cmake'), '    ${SOURCE_DIR}/cgame/cg_effects.c\n', '    ${SOURCE_DIR}/cgame/cg_effects.c\n    ${SOURCE_DIR}/cgame/cg_fx.c\n')

# --- cg_local.h ---
sub(cg('cg_local.h'), '''	LE_SCOREPLUM,
#ifdef MISSIONPACK
	LE_KAMIKAZE,''', '''	LE_SCOREPLUM,
	LE_FX_SPARK,
	LE_FX_FLASH,
	LE_FX_RING,
#ifdef MISSIONPACK
	LE_KAMIKAZE,''')
sub(cg('cg_local.h'), '\tLEF_SOUND2\t\t\t = 0x0008\t\t\t// sound 2 for kamikaze\n',
    '\tLEF_SOUND2\t\t\t = 0x0008,\t\t\t// sound 2 for kamikaze\n\tLEF_FX_EMBER\t\t = 0x0010\t\t\t// LE_FX_SPARK drawn as a round glow\n')
sub(cg('cg_local.h'), '\tfloat\t\t\tlifeRate;\t\t\t// 1.0 / (endTime - startTime)\n',
    '\tfloat\t\t\tlifeRate;\t\t\t// 1.0 / (endTime - startTime)\n\tfloat\t\t\tendRadius;\t\t\t// LE_FX_FLASH / LE_FX_RING final size\n')
sub(cg('cg_local.h'), '\tint\t\t\theadshotTime;\t\t// last time we landed a headshot\n',
    '\tint\t\t\theadshotTime;\t\t// last time we landed a headshot\n'
    '\tint\t\t\tshakeTime;\t\t\t// screen shake start\n'
    '\tfloat\t\tshakeAmplitude;\t\t// screen shake strength in degrees\n'
    '\tqboolean\timpactAlt;\t\t\t// the impact being drawn is from an alt-fire projectile\n')
sub(cg('cg_local.h'), '\tsfxHandle_t\thookFireSound;\n', '\tsfxHandle_t\thookFireSound;\n\tqhandle_t\tfxSparkShader;\n\tqhandle_t\tfxGlowShader;\n\tqhandle_t\tfxRingShader;\n')
sub(cg('cg_local.h'), 'extern\tvmCvar_t\t\tcg_horPlus;\n', 'extern\tvmCvar_t\t\tcg_horPlus;\nextern\tvmCvar_t\t\tcg_fx;\nextern\tvmCvar_t\t\tcg_screenShake;\n')
sub(cg('cg_local.h'), 'void CG_ArcBeam( vec3_t start, vec3_t end );\n', '''void CG_ArcBeam( vec3_t start, vec3_t end );

//
// cg_fx.c
//
void CG_AddFxSpark( localEntity_t *le );
void CG_AddFxFlash( localEntity_t *le );
void CG_AddFxRing( localEntity_t *le );
void CG_ImpactFX( int weapon, const vec3_t origin, const vec3_t dir, qboolean alt );
void CG_MissileGlow( centity_t *cent );
void CG_ApplyScreenShake( void );
''')

# --- cg_main.c ---
sub(cg('cg_main.c'), 'vmCvar_t\tcg_horPlus;\n', 'vmCvar_t\tcg_horPlus;\nvmCvar_t\tcg_fx;\nvmCvar_t\tcg_screenShake;\n')
sub(cg('cg_main.c'), '\t{ &cg_horPlus, "cg_horPlus", "1", CVAR_ARCHIVE },\n',
    '\t{ &cg_horPlus, "cg_horPlus", "1", CVAR_ARCHIVE },\n\t{ &cg_fx, "cg_fx", "1", CVAR_ARCHIVE },\n\t{ &cg_screenShake, "cg_screenShake", "1", CVAR_ARCHIVE },\n')
sub(cg('cg_main.c'), '\tcgs.media.smokePuffShader = trap_R_RegisterShader( "smokePuff" );\n',
    '\tcgs.media.smokePuffShader = trap_R_RegisterShader( "smokePuff" );\n'
    '\tcgs.media.fxSparkShader = trap_R_RegisterShader( "rearmed/spark" );\n'
    '\tcgs.media.fxGlowShader = trap_R_RegisterShader( "rearmed/glow" );\n'
    '\tcgs.media.fxRingShader = trap_R_RegisterShader( "rearmed/ring" );\n')

# --- local entity pool + dispatch ---
sub(cg('cg_localents.c'), '#define\tMAX_LOCAL_ENTITIES\t512\n', '#define\tMAX_LOCAL_ENTITIES\t2048\n')
sub(cg('cg_localents.c'), '''		case LE_SCOREPLUM:''', '''		case LE_FX_SPARK:
			CG_AddFxSpark( le );
			break;

		case LE_FX_FLASH:
			CG_AddFxFlash( le );
			break;

		case LE_FX_RING:
			CG_AddFxRing( le );
			break;

		case LE_SCOREPLUM:''')

# --- impacts ---
sub(cg('cg_weapons.c'), '''	} else {
		CG_ImpactMark( mark, origin, dir, random()*360, 1,1,1,1, alphaFade, radius, qfalse );
	}
}''', '''	} else {
		CG_ImpactMark( mark, origin, dir, random()*360, 1,1,1,1, alphaFade, radius, qfalse );
	}

	CG_ImpactFX( weapon, origin, dir, cg.impactAlt );
}''')
sub(cg('cg_event.c'), '''	case EV_MISSILE_MISS:
		DEBUGNAME("EV_MISSILE_MISS");
		ByteToDir( es->eventParm, dir );
		CG_MissileHitWall( es->weapon, 0, position, dir, IMPACTSOUND_DEFAULT );
		break;

	case EV_MISSILE_MISS_METAL:
		DEBUGNAME("EV_MISSILE_MISS_METAL");
		ByteToDir( es->eventParm, dir );
		CG_MissileHitWall( es->weapon, 0, position, dir, IMPACTSOUND_METAL );
		break;''', '''	case EV_MISSILE_MISS:
		DEBUGNAME("EV_MISSILE_MISS");
		ByteToDir( es->eventParm, dir );
		cg.impactAlt = ( es->generic1 & ALTFIRE_PROJECTILE ) != 0;
		CG_MissileHitWall( es->weapon, 0, position, dir, IMPACTSOUND_DEFAULT );
		cg.impactAlt = qfalse;
		break;

	case EV_MISSILE_MISS_METAL:
		DEBUGNAME("EV_MISSILE_MISS_METAL");
		ByteToDir( es->eventParm, dir );
		cg.impactAlt = ( es->generic1 & ALTFIRE_PROJECTILE ) != 0;
		CG_MissileHitWall( es->weapon, 0, position, dir, IMPACTSOUND_METAL );
		cg.impactAlt = qfalse;
		break;''')

# --- missile glow, bigger plasma orb ---
sub(cg('cg_ents.c'), '''	if ( cent->currentState.weapon == WP_PLASMAGUN ) {
		ent.reType = RT_SPRITE;
		ent.radius = 16;''', '''	CG_MissileGlow( cent );

	if ( cent->currentState.weapon == WP_PLASMAGUN ) {
		ent.reType = RT_SPRITE;
		ent.radius = ( s1->generic1 & ALTFIRE_PROJECTILE ) ? 32 : 16;''')

# --- screen shake ---
sub(cg('cg_view.c'), '''	// position eye relative to origin
	AnglesToAxis( cg.refdefViewAngles, cg.refdef.viewaxis );''', '''	CG_ApplyScreenShake();

	// position eye relative to origin
	AnglesToAxis( cg.refdefViewAngles, cg.refdef.viewaxis );''')
print('fx hooks ok')
