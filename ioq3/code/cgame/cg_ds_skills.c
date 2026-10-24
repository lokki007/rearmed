/*
===========================================================================
REARMED -- DESCENT skills, cgame side (owner SKILLS).

The held weapon's skill ("EXPLOSIVES 23" + a thin xp bar) small above the weapon
level pips by the ammo (cg_ds_arsenal.c draws the pips at y-9..y-3 and its level-up
label right of them), and a short level-up flash there ("dsskl <skill> <level>").
Never mid-screen. Numbers: CS_DSSKILLS (bg_ds_skills.h). Switch: cg_skillHud.
===========================================================================
*/
#include "cg_local.h"

#define SKC_FLASH_MSEC		1800

static struct {
	vmCvar_t	hud;
	sfxHandle_t	levelup;
	int			flashTime;
	int			flashSkill;
	int			flashLevel;
} skc;

void CG_DS_SkillsRegister( void ) {
	memset( &skc, 0, sizeof( skc ) );
	trap_Cvar_Register( &skc.hud, "cg_skillHud", "1", CVAR_ARCHIVE );
	skc.levelup = trap_S_RegisterSound( "sound/rearmed/skills/sk_levelup.wav", qfalse );
}

qboolean CG_DS_SkillsServerCommand( const char *cmd ) {
	if ( Q_stricmp( cmd, "dsskl" ) ) {
		return qfalse;
	}
	skc.flashSkill = atoi( CG_Argv( 1 ) );
	skc.flashLevel = atoi( CG_Argv( 2 ) );
	if ( skc.flashSkill < 0 || skc.flashSkill >= SK_NUM ) {
		return qtrue;
	}
	skc.flashTime = cg.time;
	trap_S_StartLocalSound( skc.levelup, CHAN_LOCAL_SOUND );
	return qtrue;
}

// level and permille of a skill from the configstring, qfalse when nothing trains
static qboolean SKC_Get( int s, int *lvl, int *pm ) {
	const char	*info = CG_ConfigString( CS_DSSKILLS );
	const char	*p;
	int			i;

	if ( !info[0] || s < 0 || s >= SK_NUM ) {
		return qfalse;
	}
	p = Info_ValueForKey( info, "lv" );
	for ( i = 0 ; i < s && *p ; i++ ) {
		while ( *p && *p != '.' ) {
			p++;
		}
		if ( *p ) {
			p++;
		}
	}
	*lvl = atoi( p );
	p = Info_ValueForKey( info, "pm" );
	for ( i = 0 ; i < s && *p ; i++ ) {
		while ( *p && *p != '.' ) {
			p++;
		}
		if ( *p ) {
			p++;
		}
	}
	*pm = atoi( p );
	return *lvl > 0;
}

/*
=================
CG_DS_DrawSkill

Status bar, right after the weapon level pips (same x / y: the ammo field's
top left in 640 space). Two short rows above the pips.
=================
*/
void CG_DS_DrawSkill( float x, float y ) {
	int			s, lvl, pm, t;
	vec4_t		col, back;
	float		flash, f, w;
	const char	*str;

	trap_Cvar_Update( &skc.hud );
	if ( !skc.hud.integer || !cg.snap || cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR
		|| cg.snap->ps.pm_type == PM_INTERMISSION ) {
		return;
	}
	t = cg.time - skc.flashTime;
	flash = ( skc.flashTime && t >= 0 && t < SKC_FLASH_MSEC ) ? 1.0f - (float)t / SKC_FLASH_MSEC : 0;
	// a level-up of another skill (movement, survival...) takes the row for a moment
	s = flash > 0 ? skc.flashSkill : BG_DS_SkillForWeapon( cg.predictedPlayerState.weapon );
	if ( !SKC_Get( s, &lvl, &pm ) ) {
		return;
	}
	if ( pm < 0 ) {
		pm = 0;
	} else if ( pm > 1000 ) {
		pm = 1000;
	}
	col[0] = ds_skills[s].color[0];
	col[1] = ds_skills[s].color[1];
	col[2] = ds_skills[s].color[2];
	col[3] = 0.75f;
	if ( flash > 0 ) {
		f = flash * ( 0.65f + 0.35f * sin( cg.time * 0.03f ) );
		col[0] += ( 1 - col[0] ) * f;
		col[1] += ( 1 - col[1] ) * f;
		col[2] += ( 1 - col[2] ) * f;
		col[3] = 0.75f + 0.25f * flash;
	}
	str = va( "%s %i", ds_skills[s].name, lvl );
	CG_DrawStringExt( x + 6, y - 26, str, col, qtrue, qtrue, 5, 7, 0 );
	if ( flash > 0 ) {
		// "+1" beside it, fading
		col[3] = flash;
		CG_DrawStringExt( x + 8 + CG_DrawStrlen( str ) * 5, y - 26, "+1", col, qtrue, qtrue, 5, 7, 0 );
		col[3] = 0.75f + 0.25f * flash;
	}
	// the xp bar: 86 wide (the width of the 8 pips), 2 high
	back[0] = back[1] = back[2] = 0; back[3] = 0.45f;
	w = 86;
	CG_FillRect( x + 5, y - 16, w, 3, back );
	col[3] *= 0.9f;
	CG_FillRect( x + 5, y - 15.5f, w * pm / 1000.0f, 2, col );
}
