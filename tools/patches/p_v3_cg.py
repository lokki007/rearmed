"""v3 cgame: remove damage numbers; charge meter; element visuals (burning / wet / shocked players,
fire patches, wind blast, thunderstrike, steam); new kill messages."""
import sys, pathlib, re
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from patch import sub

ROOT = pathlib.Path(__file__).resolve().parents[2] / 'ioq3'
c = lambda f: str(ROOT / 'code' / 'cgame' / f)


def cut(path, start, end):
	"""delete the text from start (inclusive) to end (exclusive)"""
	s = open(path, newline='').read()
	nl = '\r\n' if '\r\n' in s else '\n'
	a = s.index(start.replace('\n', nl))
	b = s.index(end.replace('\n', nl), a)
	open(path, 'w', newline='').write(s[:a] + s[b:])


def append(path, text):
	s = open(path, newline='').read()
	nl = '\r\n' if '\r\n' in s else '\n'
	open(path, 'w', newline='').write(s + text.replace('\n', nl))


F = c('cg_fx.c')
# ---------- damage numbers are gone ----------
cut(F, '''	if ( !cg_damageNumbers.integer ) {
		return;
	}''', '''}

/*
==================
CG_AddDamageNumber''')
cut(F, '''/*
==================
CG_AddDamageNumber''', '''/*
==================
CG_OverchargeTrail''')
sub(F, '''	entityState_t	*es = &cent->currentState;
	localEntity_t	*le;
	int				damage = es->eventParm;''', '''	entityState_t	*es = &cent->currentState;
	int				damage = es->eventParm;''')
sub(c('cg_localents.c'), '''		case LE_DAMAGE_NUMBER:
			CG_AddDamageNumber( le );
			break;
''', '')
sub(c('cg_local.h'), 'void CG_AddDamageNumber( localEntity_t *le );\n', '')
sub(c('cg_local.h'), 'extern\tvmCvar_t\t\tcg_damageNumbers;\n', '')
sub(c('cg_main.c'), 'vmCvar_t\tcg_damageNumbers;\n', '')
sub(c('cg_main.c'), '\t{ &cg_damageNumbers, "cg_damageNumbers", "1", CVAR_ARCHIVE },\n', '')

# ---------- media, fields, prototypes ----------
H = c('cg_local.h')
sub(H, '''	int				muzzleFlashTime;	// move to playerEntity?''', '''	int				muzzleFlashTime;	// move to playerEntity?
	int				rmFxTime;			// next element particle (burning / wet / shocked, fire patches)''')
sub(H, '''	qhandle_t	edgeShader;
''', '''	qhandle_t	edgeShader;
	qhandle_t	wetShell;
	sfxHandle_t	fireLoopSound;
	sfxHandle_t	thunderSound;
	sfxHandle_t	windSound;
	sfxHandle_t	steamSound;
	sfxHandle_t	zapSound;
	sfxHandle_t	chargeReadySound;
''')
sub(H, '''void CG_OverchargeTrail( vec3_t start, vec3_t end );''', '''void CG_OverchargeTrail( vec3_t start, vec3_t end );
void CG_ElementsPlayerFX( centity_t *cent );
void CG_FirePatch( centity_t *cent );
void CG_WindBlastFX( entityState_t *es );
void CG_ThunderFX( vec3_t ground, vec3_t sky );
void CG_SteamFX( vec3_t origin );''')

# ---------- registration ----------
R = c('cg_rearmed.c')
sub(R, '''	cgs.media.edgeShader = trap_R_RegisterShaderNoMip( "rearmed/edge" );''', '''	cgs.media.edgeShader = trap_R_RegisterShaderNoMip( "rearmed/edge" );
	cgs.media.wetShell = trap_R_RegisterShader( "rearmed/wetShell" );
	cgs.media.fireLoopSound = trap_S_RegisterSound( "sound/rearmed/fire_loop.wav", qfalse );
	cgs.media.thunderSound = trap_S_RegisterSound( "sound/rearmed/thunder.wav", qfalse );
	cgs.media.windSound = trap_S_RegisterSound( "sound/rearmed/wind.wav", qfalse );
	cgs.media.steamSound = trap_S_RegisterSound( "sound/rearmed/steam.wav", qfalse );
	cgs.media.zapSound = trap_S_RegisterSound( "sound/rearmed/zap.wav", qfalse );
	cgs.media.chargeReadySound = trap_S_RegisterSound( "sound/rearmed/charge_ready.wav", qfalse );''')
# wet shell on the body parts
sub(R, '''	if ( state->powerups & ( 1 << PW_RAPIDFIRE ) ) {
		ent->customShader = cgs.media.rapidShell;
		trap_R_AddRefEntityToScene( ent );
	}''', '''	if ( state->powerups & ( 1 << PW_RAPIDFIRE ) ) {
		ent->customShader = cgs.media.rapidShell;
		trap_R_AddRefEntityToScene( ent );
	}
	if ( state->powerups & ( 1 << PW_WET ) ) {
		ent->customShader = cgs.media.wetShell;
		trap_R_AddRefEntityToScene( ent );
	}''')
# per player element particles
sub(R, '''	if ( cent->currentState.eFlags & EF_JETTING ) {
		AngleVectors( cent->lerpAngles, forward, NULL, NULL );''', '''	CG_ElementsPlayerFX( cent );

	if ( cent->currentState.eFlags & EF_JETTING ) {
		AngleVectors( cent->lerpAngles, forward, NULL, NULL );''')
# charge meter + status tint, inside the overlays
sub(R, '''	// low health: red pulse creeping in from the screen edges''', '''	// charged alt-fire meter over the dash pips
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

	// low health: red pulse creeping in from the screen edges''')

# ---------- entities and events ----------
sub(c('cg_ents.c'), '''	s1 = &cent->currentState;

	// if set to invisible, skip
	if (!s1->modelindex) {
		return;
	}''', '''	s1 = &cent->currentState;

	if ( s1->generic1 == RM_ENT_FIRE ) {
		CG_FirePatch( cent );
		return;
	}

	// if set to invisible, skip
	if (!s1->modelindex) {
		return;
	}''')
E = c('cg_event.c')
sub(E, '''	case EV_ARC:
		DEBUGNAME("EV_ARC");
		CG_ArcBeam( es->pos.trBase, es->origin2 );
		break;''', '''	case EV_ARC:
		DEBUGNAME("EV_ARC");
		CG_ArcBeam( es->pos.trBase, es->origin2 );
		break;

	case EV_WINDBLAST:
		DEBUGNAME("EV_WINDBLAST");
		CG_WindBlastFX( es );
		break;

	case EV_THUNDER:
		DEBUGNAME("EV_THUNDER");
		CG_ThunderFX( es->pos.trBase, es->origin2 );
		break;

	case EV_STEAM:
		DEBUGNAME("EV_STEAM");
		CG_SteamFX( es->pos.trBase );
		break;

	case EV_CHARGE_READY:
		DEBUGNAME("EV_CHARGE_READY");
		if ( es->number == cg.snap->ps.clientNum ) {
			trap_S_StartLocalSound( cgs.media.chargeReadySound, CHAN_LOCAL_SOUND );
		}
		break;''')
sub(E, '''	if ( attacker != ENTITYNUM_WORLD ) {
		switch (mod) {
		case MOD_GRAPPLE:
			message = "was caught by";
			break;''', '''	if ( attacker != ENTITYNUM_WORLD ) {
		switch (mod) {
		case MOD_GRAPPLE:
			message = "was caught by";
			break;
		case MOD_BURN:
			message = "was incinerated by";
			break;
		case MOD_THUNDER:
			message = "was thunderstruck by";
			break;''')

# ---------- the element effects ----------
append(F, r'''
/*
==================
CG_ElementsPlayerFX

Burning players trail flames, soaked ones drip, shocked ones crackle
==================
*/
void CG_ElementsPlayerFX( centity_t *cent ) {
#ifndef MISSIONPACK
	static const vec3_t	blue = { 0.3f, 0.6f, 1.0f };
	static const vec3_t	cyan = { 0.55f, 0.9f, 1.0f };
	int		powerups = cent->currentState.powerups;
	vec3_t	org, vel;

	if ( !( powerups & ( ( 1 << PW_BURNING ) | ( 1 << PW_WET ) | ( 1 << PW_SHOCKED ) ) ) ) {
		return;
	}
	if ( powerups & ( 1 << PW_BURNING ) ) {
		trap_R_AddLightToScene( cent->lerpOrigin, 140 + 20 * sin( cg.time * 0.03f ), 1.0f, 0.5f, 0.1f );
		trap_S_AddLoopingSound( cent->currentState.number, cent->lerpOrigin, vec3_origin, cgs.media.fireLoopSound );
	}
	if ( powerups & ( 1 << PW_SHOCKED ) ) {
		trap_R_AddLightToScene( cent->lerpOrigin, 90 + 60 * random(), 0.4f, 0.8f, 1.0f );
	}
	if ( !cg_fx.integer || cg.time < cent->rmFxTime ) {
		return;
	}
	cent->rmFxTime = cg.time + 35;

	VectorCopy( cent->lerpOrigin, org );
	org[0] += crandom() * 12;
	org[1] += crandom() * 12;
	org[2] += -16 + random() * 44;
	if ( powerups & ( 1 << PW_BURNING ) ) {
		VectorSet( vel, crandom() * 15, crandom() * 15, 60 + random() * 50 );
		CG_SmokePuff( org, vel, 8 + random() * 6, 1.0f, 0.6f + 0.2f * random(), 0.25f, 0.9f, 380,
			cg.time, 0, 0, cgs.media.flameShader );
	}
	if ( powerups & ( 1 << PW_WET ) ) {
		VectorSet( vel, crandom() * 20, crandom() * 20, -40 );
		CG_FxSpark( org, vel, blue, 450, 0, qtrue, 2.5f );
	}
	if ( powerups & ( 1 << PW_SHOCKED ) ) {
		VectorSet( vel, crandom() * 160, crandom() * 160, crandom() * 160 );
		CG_FxSpark( org, vel, cyan, 180, 0, qfalse, 1.5f );
	}
#endif
}

/*
==================
CG_FirePatch

Burning ground from grenades and napalm: flames that fade in and out
==================
*/
void CG_FirePatch( centity_t *cent ) {
	static const vec3_t	ember = { 1.0f, 0.5f, 0.1f };
	entityState_t	*s = &cent->currentState;
	float	life, radius, a;
	vec3_t	org, vel;
	int		i;

	radius = s->otherEntityNum2 > 0 ? s->otherEntityNum2 : 80;
	life = 1.0f;
	if ( cg.time - s->time < 250 ) {
		life = ( cg.time - s->time ) / 250.0f;
	} else if ( s->time2 - cg.time < 600 ) {
		life = ( s->time2 - cg.time ) / 600.0f;
	}
	if ( life <= 0 ) {
		return;
	}
	trap_R_AddLightToScene( cent->lerpOrigin, radius * ( 1.6f + 0.2f * sin( cg.time * 0.02f ) ) * life, 1.0f, 0.45f, 0.08f );
	trap_S_AddLoopingSound( s->number, cent->lerpOrigin, vec3_origin, cgs.media.fireLoopSound );

	if ( cg.time < cent->rmFxTime ) {
		return;
	}
	cent->rmFxTime = cg.time + ( cg_fx.integer ? 28 : 70 );
	for ( i = 0 ; i < 2 ; i++ ) {
		a = random() * 2 * M_PI;
		VectorCopy( cent->lerpOrigin, org );
		org[0] += cos( a ) * radius * 0.8f * random();
		org[1] += sin( a ) * radius * 0.8f * random();
		org[2] += 4;
		VectorSet( vel, crandom() * 10, crandom() * 10, 70 + random() * 70 );
		CG_SmokePuff( org, vel, 12 + random() * 12 * life, 1.0f, 0.55f + 0.2f * random(), 0.2f, 0.95f * life,
			500 + random() * 300, cg.time, 0, 0, cgs.media.flameShader );
	}
	if ( cg_fx.integer && random() < 0.15f ) {
		VectorSet( vel, crandom() * 40, crandom() * 40, 120 + random() * 120 );
		CG_FxSpark( org, vel, ember, 900, 0.3f, qtrue, 3 );
	}
}

/*
==================
CG_WindBlastFX

Shotgun air blast: a cone of rushing air
==================
*/
void CG_WindBlastFX( entityState_t *es ) {
	vec3_t	dir, right, up, org, vel;
	float	charge = es->eventParm / 255.0f, speed;
	int		i, n;

	VectorCopy( es->angles2, dir );
	PerpendicularVector( right, dir );
	CrossProduct( dir, right, up );
	trap_S_StartSound( es->pos.trBase, es->otherEntityNum, CHAN_WEAPON, cgs.media.windSound );
	n = 10 + 10 * charge;
	speed = 700 + 500 * charge;
	for ( i = 0 ; i < n ; i++ ) {
		VectorMA( es->pos.trBase, 8 + random() * 24, dir, org );
		VectorScale( dir, speed * ( 0.6f + 0.4f * random() ), vel );
		VectorMA( vel, crandom() * speed * 0.35f, right, vel );
		VectorMA( vel, crandom() * speed * 0.35f, up, vel );
		CG_SmokePuff( org, vel, 10 + random() * 10, 0.85f, 0.9f, 0.95f, 0.35f, 350 + random() * 200,
			cg.time, 0, 0, cgs.media.smokePuffShader );
	}
	CG_AddScreenShake( es->pos.trBase, 1.5f + 2 * charge, 200 );
}

/*
==================
CG_ThunderFX

Thunderstrike: a jagged bolt from the sky, a flash and a boom
==================
*/
void CG_ThunderFX( vec3_t ground, vec3_t sky ) {
	static const vec3_t	white = { 0.85f, 0.95f, 1.0f };
	static const vec3_t	up = { 0, 0, 1 };
	vec3_t	a, b;
	int		i;

	VectorCopy( sky, a );
	for ( i = 1 ; i <= 5 ; i++ ) {
		VectorScale( sky, 1.0f - i / 5.0f, b );
		VectorMA( b, i / 5.0f, ground, b );
		if ( i < 5 ) {
			b[0] += crandom() * 40;
			b[1] += crandom() * 40;
		}
		CG_ArcBeam( a, b );
		VectorCopy( b, a );
	}
	CG_ArcBeam( sky, ground );
	CG_FxFlash( ground, white, 40, 320, 350 );
	CG_FxRing( ground, up, white, 300, 450 );
	CG_FxBurst( ground, up, white, 30, 500, 600, 0.3f, 8 );
	trap_S_StartSound( ground, ENTITYNUM_WORLD, CHAN_AUTO, cgs.media.thunderSound );
	CG_AddScreenShake( ground, 7, 900 );
}

/*
==================
CG_SteamFX

Fire meets water
==================
*/
void CG_SteamFX( vec3_t origin ) {
	vec3_t	org, vel;
	int		i;

	trap_S_StartSound( origin, ENTITYNUM_WORLD, CHAN_AUTO, cgs.media.steamSound );
	for ( i = 0 ; i < 8 ; i++ ) {
		VectorCopy( origin, org );
		org[0] += crandom() * 14;
		org[1] += crandom() * 14;
		org[2] += random() * 30;
		VectorSet( vel, crandom() * 30, crandom() * 30, 50 + random() * 60 );
		CG_SmokePuff( org, vel, 16 + random() * 10, 0.9f, 0.9f, 0.9f, 0.55f, 900 + random() * 400,
			cg.time, 0, 0, cgs.media.smokePuffShader );
	}
}
''')
print('v3 cg ok')
