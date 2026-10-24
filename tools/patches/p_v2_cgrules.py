"""v2 cg_rearmed.c: medals / bounty / warn, announcer toggle, warmup HUD, bounty marker,
dash meter, damage direction, low health pulse."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from patch import sub

ROOT = pathlib.Path(__file__).resolve().parents[2] / 'ioq3'
R = str(ROOT / 'code' / 'cgame' / 'cg_rearmed.c')

sub(R, '''	cgs.media.annInfo = trap_S_RegisterSound( "sound/rearmed/ann_info.wav", qfalse );
''', '''	cgs.media.annInfo = trap_S_RegisterSound( "sound/rearmed/ann_info.wav", qfalse );
	cgs.media.annMedal = trap_S_RegisterSound( "sound/rearmed/ann_medal.wav", qfalse );
	cgs.media.annBounty = trap_S_RegisterSound( "sound/rearmed/ann_bounty.wav", qfalse );
	cgs.media.annWarn = trap_S_RegisterSound( "sound/rearmed/ann_warn.wav", qfalse );
	cgs.media.armorBreakSound = trap_S_RegisterSound( "sound/rearmed/armorbreak.wav", qfalse );
	cgs.media.wallRunSound = trap_S_RegisterSound( "sound/rearmed/wallrun.wav", qfalse );
	cgs.media.hitSounds2[0] = trap_S_RegisterSound( "sound/rearmed/hit_low2.wav", qfalse );
	cgs.media.hitSounds2[1] = trap_S_RegisterSound( "sound/rearmed/hit_mid2.wav", qfalse );
	cgs.media.hitSounds2[2] = trap_S_RegisterSound( "sound/rearmed/hit_high2.wav", qfalse );
	cgs.media.edgeShader = trap_R_RegisterShaderNoMip( "rearmed/edge" );
	{
		int		i;
		for ( i = 0 ; i < 16 ; i++ ) {
			cgs.media.dmgDirShaders[i] = trap_R_RegisterShaderNoMip( va( "rearmed/dmgdir%02i", i ) );
		}
	}
''')

sub(R, '''	cgs.rmJuggernaut = info[0] ? atoi( Info_ValueForKey( info, "jug" ) ) : -1;
	if ( cgs.rmMutator < 0 || cgs.rmMutator >= MUT_NUM_MUTATORS ) {
		cgs.rmMutator = MUT_NONE;
	}
}''', '''	cgs.rmJuggernaut = info[0] ? atoi( Info_ValueForKey( info, "jug" ) ) : -1;
	cgs.rmSwitches = info[0] ? atoi( Info_ValueForKey( info, "sw" ) ) : -1;
	if ( cgs.rmMutator < 0 || cgs.rmMutator >= MUT_NUM_MUTATORS ) {
		cgs.rmMutator = MUT_NONE;
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
}''')
sub(R, '''void CG_RearmParseConfigstring( void ) {
	const char	*info = CG_ConfigString( CS_REARMED );
''', '''void CG_RearmParseConfigstring( void ) {
	const char	*info = CG_ConfigString( CS_REARMED );
	const char	*s;
	int			n;
''')

# announcements: medals and warnings get their own slot; the announcer can be turned off
sub(R, '''	if ( !strcmp( cmd, "rmann" ) ) {
		kind = CG_Argv( 1 );
		Q_strncpyz( cg.rmBanner, CG_Argv( 2 ), sizeof( cg.rmBanner ) );
		cg.rmBannerTime = cg.time;
		if ( !strcmp( kind, "streak" ) ) {''', '''	if ( !strcmp( cmd, "rmann" ) ) {
		kind = CG_Argv( 1 );
		CG_Printf( "%s\\n", CG_Argv( 2 ) );
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
		} else if ( !strcmp( kind, "streak" ) ) {''')
sub(R, '''		trap_S_StartLocalSound( sfx, CHAN_ANNOUNCER );
		CG_Printf( "%s\\n", cg.rmBanner );
		return qtrue;''', '''		trap_S_StartLocalSound( sfx, CHAN_ANNOUNCER );
		return qtrue;''')

sub(R, '''	static const vec4_t	bannerColors[5] = {
		{ 1.0f, 1.0f, 1.0f, 1.0f },		// info
		{ 1.0f, 0.55f, 0.15f, 1.0f },	// streak
		{ 0.5f, 0.85f, 1.0f, 1.0f },	// mutator
		{ 1.0f, 0.25f, 0.2f, 1.0f },	// juggernaut
		{ 0.5f, 1.0f, 0.45f, 1.0f },	// gun game
	};''', '''	static const vec4_t	bannerColors[6] = {
		{ 1.0f, 1.0f, 1.0f, 1.0f },		// info
		{ 1.0f, 0.55f, 0.15f, 1.0f },	// streak
		{ 0.5f, 0.85f, 1.0f, 1.0f },	// mutator
		{ 1.0f, 0.25f, 0.2f, 1.0f },	// juggernaut
		{ 0.5f, 1.0f, 0.45f, 1.0f },	// gun game
		{ 1.0f, 0.82f, 0.25f, 1.0f },	// bounty
	};''')
# medal slot, under the crosshair callouts
sub(R, '''	y = 34;
	// running mutator with its countdown''', '''	// medal / warning
	if ( cg.rmMedalTime && ( fade = CG_FadeColor( cg.rmMedalTime, 1800 ) ) != NULL ) {
		if ( cg.rmMedalKind ) {
			color[0] = 1.0f; color[1] = 0.25f; color[2] = 0.2f;
		} else {
			color[0] = 1.0f; color[1] = 0.82f; color[2] = 0.25f;
		}
		color[3] = fade[3];
		scale = 1.0f + 0.4f * ( 1.0f - (float)( cg.time - cg.rmMedalTime ) / 140.0f );
		if ( scale < 1.0f ) {
			scale = 1.0f;
		}
		w = CG_DrawStrlen( cg.rmMedal ) * 12 * scale;
		CG_DrawStringExt( 320 - w / 2, 330, cg.rmMedal, color, qtrue, qtrue, 12 * scale, 16 * scale, 0 );
	}

	y = 34;
	// running mutator with its countdown''')
# no ladder / juggernaut lines while the warmup countdown owns the top of the screen
sub(R, '''	if ( cgs.rmMode == RM_MODE_GUNGAME && cg.snap->ps.persistant[PERS_TEAM] != TEAM_SPECTATOR ) {''',
    '''	if ( cg.warmup ) {
		return;
	}
	if ( cgs.rmMode == RM_MODE_GUNGAME && cg.snap->ps.persistant[PERS_TEAM] != TEAM_SPECTATOR ) {''')

# bounty marker over players
sub(R, '''	if ( cent->currentState.eFlags & EF_JETTING ) {
		AngleVectors( cent->lerpAngles, forward, NULL, NULL );''', '''	if ( cgs.rmBounty[cent->currentState.clientNum & ( MAX_CLIENTS - 1 )] && cent->currentState.clientNum != cg.snap->ps.clientNum ) {
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
		glow.customShader = cgs.media.fxRingShader;
		glow.radius = 12 + 4 * ( ( cg.time % 900 ) / 900.0f );
		glow.shaderRGBA[3] = 160;
		trap_R_AddRefEntityToScene( &glow );
	}

	if ( cent->currentState.eFlags & EF_JETTING ) {
		AngleVectors( cent->lerpAngles, forward, NULL, NULL );''')

# overlays
R_text = pathlib.Path(R).read_text(newline='')
nl = '\r\n' if '\r\n' in R_text else '\n'
R_text += '''
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
			charges = DASH_CHARGES;
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

	// low health: red pulse creeping in from the screen edges
	if ( cg_lowHealthFX.integer && ps->stats[STAT_HEALTH] > 0 && ps->stats[STAT_HEALTH] <= 35
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
'''.replace('\n', nl)
pathlib.Path(R).write_text(R_text, newline='')
print('v2 cg rules ok')
