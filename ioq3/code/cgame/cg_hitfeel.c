/*
===========================================================================
REARMED -- round 4 HIT FEEL (en-feel), cgame side. Server: game/g_hitfeel.c. Shared: bg_hitfeel.h.

Everything lives ON the bodies (centre of the screen stays clean):
  flinch     every hit kicks the torso / head away from the shot (EV_GORE_HIT dir), a stagger
             kicks harder and wobbles (DSEF_STAGGER).                        cg_hitFlinch
  limbs      EV_HITFEEL HFX_LIMB: the limb flies off as a persistent gib (cg_gore.c ring), the
             stump pumps blood; bodies keep it (entityState.time2 HFB_ bits): arm-less skins for
             models with separate arm surfaces (tools/make_hitfeel_assets.py), a meat stump
             on every model; legless bodies lose the legs model and lie low.
  crawl      DSEF_CRAWL: the body is pitched down and lowered, leaves a blood trail.
  panic      HFX_PANIC: a scream, the gun drops out of the hand for a moment.
  armour     cracks + a hot glow over the body as armorPct falls (CS_DSENEMY), sparks on hits,
             a shatter of plates when it breaks.                             cg_armorCracks
  screams    HFX_SCREAM: generated death screams by category and voice.     cg_deathScreams
  splats     EV_WALLSPLAT gets a body thud.
===========================================================================
*/
#include "cg_local.h"

static vmCvar_t	hf_cgFlinch, hf_cgCracks, hf_cgScreams;
static int		hf_cvarTime = -1;

static const char *hf_catNames[HFS_NUM] = { "gen", "head", "gib", "burn", "fall", "boom" };

static struct {
	qboolean	registered;
	sfxHandle_t	death[HFS_NUM][HFS_VOICES][2];
	sfxHandle_t	panic[HFS_VOICES][2];
	sfxHandle_t	groan[HFS_VOICES];
	sfxHandle_t	thud[2], shatter[2], clang, tear[2], crackSnd[2];
	qhandle_t	arm, leg, helmet, stump, shard;
	qhandle_t	crack[3], glow[3];
} hfm;

typedef struct {
	int			flinchTime;
	vec3_t		flinchDir;
	float		flinchAmp;
	int			panicTime;			// HFX_PANIC (weapon dropped)
	int			limbTime[3];		// larm, rarm, legs: when they came off (stump bleeding)
	int			crawlMark;
	vec3_t		crawlLast;
	int			bleedNext;
	vec3_t		lastHand;
	// arm-less skins for the current model / skin
	char		skinKey[MAX_QPATH * 2];
	qhandle_t	noArm[3];			// left, right, both
} hfPlayer_t;

static hfPlayer_t	hfp[MAX_CLIENTS];

static void CG_HF_Cvars( void ) {
	if ( hf_cvarTime < 0 ) {
		trap_Cvar_Register( &hf_cgFlinch, "cg_hitFlinch", "1", CVAR_ARCHIVE );
		trap_Cvar_Register( &hf_cgCracks, "cg_armorCracks", "1", CVAR_ARCHIVE );
		trap_Cvar_Register( &hf_cgScreams, "cg_deathScreams", "1", CVAR_ARCHIVE );
		hf_cvarTime = cg.time;
		return;
	}
	if ( hf_cvarTime != cg.time ) {
		hf_cvarTime = cg.time;
		trap_Cvar_Update( &hf_cgFlinch );
		trap_Cvar_Update( &hf_cgCracks );
		trap_Cvar_Update( &hf_cgScreams );
	}
}

/*
=================
CG_HF_Register

Map load (from CG_GoreClear): every sound and model, so the first death doesn't hitch.
=================
*/
void CG_HF_Register( void ) {
	int		c, v, i;

	CG_HF_Cvars();
	memset( hfp, 0, sizeof( hfp ) );
	if ( hfm.registered ) {
		return;
	}
	hfm.registered = qtrue;
	for ( c = 0; c < HFS_NUM; c++ ) {
		for ( v = 0; v < HFS_VOICES; v++ ) {
			for ( i = 0; i < 2; i++ ) {
				hfm.death[c][v][i] = trap_S_RegisterSound( va( "sound/rearmed/hitfeel/death_%s_%i%c.wav", hf_catNames[c], v, 'a' + i ), qfalse );
			}
		}
	}
	for ( v = 0; v < HFS_VOICES; v++ ) {
		hfm.panic[v][0] = trap_S_RegisterSound( va( "sound/rearmed/hitfeel/panic_%ia.wav", v ), qfalse );
		hfm.panic[v][1] = trap_S_RegisterSound( va( "sound/rearmed/hitfeel/panic_%ib.wav", v ), qfalse );
		hfm.groan[v] = trap_S_RegisterSound( va( "sound/rearmed/hitfeel/groan_%i.wav", v ), qfalse );
	}
	for ( i = 0; i < 2; i++ ) {
		hfm.thud[i] = trap_S_RegisterSound( va( "sound/rearmed/hitfeel/thud%i.wav", i + 1 ), qfalse );
		hfm.shatter[i] = trap_S_RegisterSound( va( "sound/rearmed/hitfeel/shatter%i.wav", i + 1 ), qfalse );
		hfm.tear[i] = trap_S_RegisterSound( va( "sound/rearmed/hitfeel/tear%i.wav", i + 1 ), qfalse );
		hfm.crackSnd[i] = trap_S_RegisterSound( va( "sound/rearmed/hitfeel/crack%i.wav", i + 1 ), qfalse );
	}
	hfm.clang = trap_S_RegisterSound( "sound/rearmed/hitfeel/clang.wav", qfalse );
	hfm.arm = trap_R_RegisterModel( "models/rearmed/gibs/arm.md3" );
	hfm.leg = trap_R_RegisterModel( "models/rearmed/gibs/leg.md3" );
	hfm.helmet = trap_R_RegisterModel( "models/rearmed/gibs/helmet.md3" );
	hfm.stump = trap_R_RegisterModel( "models/rearmed/gibs/meat3.md3" );
	hfm.shard = trap_R_RegisterModel( "models/rearmed/debris/metal1.md3" );
	for ( i = 0; i < 3; i++ ) {
		hfm.crack[i] = trap_R_RegisterShader( va( "rearmed/hitfeel/crack%i", i + 1 ) );
		hfm.glow[i] = trap_R_RegisterShader( va( "rearmed/hitfeel/crackglow%i", i + 1 ) );
	}
}

static int CG_HF_Voice( int clientNum ) {
	clientInfo_t	*ci;
	const char		*s;
	int				h;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return 0;
	}
	ci = &cgs.clientinfo[clientNum];
	if ( ci->gender == GENDER_FEMALE ) {
		return 2;
	}
	if ( ci->gender == GENDER_NEUTER ) {
		return 3;
	}
	h = clientNum;
	for ( s = ci->modelName; *s; s++ ) {
		h = h * 31 + *s;
	}
	return ( h & 0x7fffffff ) % 2;
}

// ------------------------------------------------------------------ flinch

// the QVM libc has no exp(): (1 + x/256)^256 is close enough for envelopes
static float CG_HF_Exp( float x ) {
	float	y;
	int		i;

	if ( x < -12 ) {
		return 0;
	}
	y = 1.0f + x / 256.0f;
	if ( y < 0 ) {
		return 0;
	}
	for ( i = 0; i < 8; i++ ) {
		y *= y;
	}
	return y;
}

static void CG_HF_Kick( int clientNum, const vec3_t dir, float amp ) {
	hfPlayer_t	*p;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}
	p = &hfp[clientNum];
	// a fresh kick replaces a fading one; a weaker one during a big kick adds a little
	if ( cg.time - p->flinchTime < 120 && amp < p->flinchAmp ) {
		p->flinchAmp += amp * 0.25f;
		return;
	}
	p->flinchTime = cg.time;
	p->flinchAmp = amp;
	VectorCopy( dir, p->flinchDir );
}

/*
=================
CG_HF_Hit

EV_GORE_HIT (before cg_gore draws it): the body flinches, armour sparks.
=================
*/
void CG_HF_Hit( entityState_t *es, const vec3_t position ) {
	int				target = es->otherEntityNum;
	int				damage = es->eventParm;
	vec3_t			dir, n;
	const cgEnemy_t	*e;
	static vec3_t	sparkCol = { 1.0f, 0.85f, 0.55f };

	CG_HF_Cvars();
	if ( target < 0 || target >= MAX_CLIENTS ) {
		return;		// corpses don't flinch
	}
	VectorCopy( es->angles2, dir );
	if ( VectorNormalize( dir ) < 0.5f ) {
		VectorSubtract( cg_entities[target].lerpOrigin, position, dir );
		VectorNormalize( dir );
	}
	if ( hf_cgFlinch.integer ) {
		CG_HF_Kick( target, dir, 4.0f + damage * 0.22f );
	}
	e = CG_EN( target );
	if ( hf_cgCracks.integer && e->armorPct > 0 && !( es->generic1 & 0x80 ) ) {
		VectorNegate( dir, n );
		CG_FxSparks( position, n, sparkCol, 3 + damage / 12, 160, 260, 0.6f, 0 );
	}
}

// EV_PAIN without a blood event (g_gore 0): a small kick from nowhere in particular
void CG_HF_Pain( centity_t *cent ) {
	int		n = cent->currentState.number;
	vec3_t	dir;

	CG_HF_Cvars();
	if ( !hf_cgFlinch.integer || n < 0 || n >= MAX_CLIENTS || cg.time - hfp[n].flinchTime < 300 ) {
		return;
	}
	AngleVectors( cent->lerpAngles, dir, NULL, NULL );
	VectorNegate( dir, dir );
	CG_HF_Kick( n, dir, 6 );
}

/*
=================
CG_HF_PlayerAngles

CG_PlayerAngles, world angles before they are made relative: the flinch kick, the stagger
wobble, the crawl pose.
=================
*/
void CG_HF_PlayerAngles( centity_t *cent, vec3_t legs, vec3_t torso, vec3_t head ) {
	int				c = cent->currentState.clientNum;
	hfPlayer_t		*p;
	const cgEnemy_t	*e;
	vec3_t			fwd, right, ang;
	float			t, k, f, s;
	int				body;

	if ( c < 0 || c >= MAX_CLIENTS ) {
		return;
	}
	CG_HF_Cvars();
	p = &hfp[c];
	body = cent->currentState.time2 & HFB_ALL;
	if ( cent->currentState.eFlags & EF_DEAD ) {
		return;
	}
	e = CG_EN( c );

	if ( hf_cgFlinch.integer ) {
		t = cg.time - p->flinchTime;
		if ( p->flinchTime && t >= 0 && t < 450 ) {
			// fast in (40 ms), springy out
			k = ( t / 40.0f ) * CG_HF_Exp( 1.0f - t / 40.0f );
			if ( t > 80 ) {
				k = k * 0.8f + 0.2f * CG_HF_Exp( -( t - 80 ) / 110.0f ) * cos( ( t - 80 ) * 0.02f );
			}
			f = p->flinchAmp > 28 ? 28 : p->flinchAmp;
			VectorSet( ang, 0, legs[YAW], 0 );
			AngleVectors( ang, fwd, right, NULL );
			// shot from the front pushes the torso back (pitch up), from the side rolls it away
			torso[PITCH] += f * k * DotProduct( p->flinchDir, fwd );
			torso[ROLL] += f * k * DotProduct( p->flinchDir, right ) * 0.8f;
			torso[YAW] += f * k * DotProduct( p->flinchDir, right ) * 0.5f;
			head[PITCH] += f * k * 1.4f * DotProduct( p->flinchDir, fwd );
			head[ROLL] += f * k * 1.2f * DotProduct( p->flinchDir, right );
		}
		if ( e->flags & DSEF_STAGGER ) {
			s = cg.time * 0.001f + c;
			torso[ROLL] += 9 * sin( s * 13.0f );
			torso[PITCH] += 7 * sin( s * 9.0f ) + 6;
			head[ROLL] += 12 * sin( s * 11.0f + 1 );
			head[PITCH] += 10;
		}
	}

	if ( ( e->flags & DSEF_CRAWL ) || ( body & HFB_LEGS ) ) {
		if ( body & HFB_LEGS ) {
			torso[PITCH] += 70;		// dragging itself on its belly
			head[PITCH] -= 25;
		} else {
			legs[PITCH] += 30;
			torso[PITCH] += 55;
			head[PITCH] -= 35;
		}
	}
	if ( e->flags & DSEF_PANIC ) {
		head[YAW] += 25 * sin( cg.time * 0.007f + c );	// looking around wildly
	}
}

// ------------------------------------------------------------------ bodies

/*
=================
CG_HF_Legs

CG_Player, after the legs are placed: crawlers lie low, the legless lower to the floor.
Returns qtrue if the legs model must not be drawn (it still gives the torso its tag).
=================
*/
qboolean CG_HF_Legs( centity_t *cent, refEntity_t *legs ) {
	int				c = cent->currentState.clientNum;
	int				body = cent->currentState.time2 & HFB_ALL;
	qboolean		dead = ( cent->currentState.eFlags & EF_DEAD ) != 0;
	const cgEnemy_t	*e;
	hfPlayer_t		*p;
	vec3_t			end, vel;
	trace_t			tr;
	int				i;

	if ( c < 0 || c >= MAX_CLIENTS ) {
		return qfalse;
	}
	p = &hfp[c];
	e = CG_EN( c );
	if ( !dead && cent->currentState.number < MAX_CLIENTS ) {
		if ( body & HFB_LEGS ) {
			legs->origin[2] -= 20;
		} else if ( e->flags & DSEF_CRAWL ) {
			legs->origin[2] -= 9;
		}
		VectorCopy( legs->origin, legs->oldorigin );
		// a crawling body leaves a trail of blood
		if ( ( ( e->flags & DSEF_CRAWL ) || ( body & HFB_LEGS ) ) && cg.time >= p->crawlMark ) {
			if ( Distance( cent->lerpOrigin, p->crawlLast ) > 18 ) {
				VectorCopy( cent->lerpOrigin, p->crawlLast );
				VectorCopy( cent->lerpOrigin, end );
				end[2] -= 64;
				CG_Trace( &tr, cent->lerpOrigin, NULL, NULL, end, c, CONTENTS_SOLID );
				if ( tr.fraction < 1.0f ) {
					CG_GoreTrailMark( tr.endpos, tr.plane.normal, 7 + 5 * random() );
				}
			}
			p->crawlMark = cg.time + 120;
		}
	}
	// the hips of a legless body pump blood for a while
	if ( ( body & HFB_LEGS ) && p->limbTime[2] && cg.time - p->limbTime[2] < 7000 && cg.time >= p->bleedNext ) {
		p->bleedNext = cg.time + 60;
		for ( i = 0; i < 2; i++ ) {
			VectorSet( vel, crandom() * 60, crandom() * 60, 40 + random() * 90 );
			CG_GoreParticleDrop( legs->origin, vel, 1.6f, 1200, NULL );
		}
	}
	return ( body & HFB_LEGS ) != 0;
}

static qhandle_t CG_HF_ArmSkin( int c, clientInfo_t *ci, int body ) {
	hfPlayer_t	*p = &hfp[c];
	char		key[MAX_QPATH * 2];
	const char	*skin;
	char		path[MAX_QPATH];
	fileHandle_t	f;
	int			i, len;
	static const char sfx[3] = { 'l', 'r', 'b' };

	skin = ci->skinName;
	if ( cgs.gametype >= GT_TEAM ) {
		skin = ci->team == TEAM_BLUE ? "blue" : "red";
	}
	Com_sprintf( key, sizeof( key ), "%s/%s", ci->modelName, skin );
	if ( strcmp( key, p->skinKey ) ) {
		Q_strncpyz( p->skinKey, key, sizeof( p->skinKey ) );
		for ( i = 0; i < 3; i++ ) {
			p->noArm[i] = 0;
			Com_sprintf( path, sizeof( path ), "models/players/%s/upper_%s_hf%c.skin", ci->modelName, skin, sfx[i] );
			len = trap_FS_FOpenFile( path, &f, FS_READ );
			if ( len <= 0 ) {
				if ( f ) {
					trap_FS_FCloseFile( f );
				}
				Com_sprintf( path, sizeof( path ), "models/players/%s/upper_default_hf%c.skin", ci->modelName, sfx[i] );
				len = trap_FS_FOpenFile( path, &f, FS_READ );
			}
			if ( f ) {
				trap_FS_FCloseFile( f );
			}
			if ( len > 0 ) {
				p->noArm[i] = trap_R_RegisterSkin( path );
			}
		}
	}
	if ( ( body & HFB_ARMS ) == HFB_ARMS ) {
		return p->noArm[2];
	}
	return p->noArm[( body & HFB_RARM ) ? 1 : 0];
}

/*
=================
CG_HF_Torso

CG_Player, the torso placed on the legs: the arm-less skin, the shoulder stumps and their
bleeding. Also remembers the hand for the panic weapon drop.
=================
*/
void CG_HF_Torso( centity_t *cent, refEntity_t *torso, clientInfo_t *ci ) {
	int			c = cent->currentState.clientNum;
	int			body = cent->currentState.time2 & HFB_ALL;
	hfPlayer_t	*p;
	refEntity_t	stump;
	qhandle_t	skin;
	vec3_t		sh, vel, axis[3];
	int			side, i, n;
	float		sgn;

	if ( c < 0 || c >= MAX_CLIENTS ) {
		return;
	}
	p = &hfp[c];
	if ( cent->currentState.number < MAX_CLIENTS ) {
		VectorMA( torso->origin, 12, torso->axis[2], p->lastHand );
	}
	if ( !( body & HFB_ARMS ) || CG_GoreLevel() <= 0 ) {
		return;
	}
	skin = CG_HF_ArmSkin( c, ci, body );
	if ( skin ) {
		torso->customSkin = skin;
	}
	for ( side = 0; side < 2; side++ ) {
		if ( !( body & ( side ? HFB_RARM : HFB_LARM ) ) ) {
			continue;
		}
		sgn = side ? -1.0f : 1.0f;		// axis[1] points left
		VectorMA( torso->origin, 15, torso->axis[2], sh );
		VectorMA( sh, 8.5f * sgn, torso->axis[1], sh );
		// a ragged stump at the shoulder
		memset( &stump, 0, sizeof( stump ) );
		stump.reType = RT_MODEL;
		stump.hModel = hfm.stump;
		VectorCopy( sh, stump.origin );
		VectorCopy( sh, stump.oldorigin );
		VectorCopy( torso->lightingOrigin, stump.lightingOrigin );
		AxisCopy( torso->axis, axis );
		VectorScale( axis[0], 0.55f, stump.axis[0] );
		VectorScale( axis[1], 0.55f, stump.axis[1] );
		VectorScale( axis[2], 0.55f, stump.axis[2] );
		stump.nonNormalizedAxes = qtrue;
		stump.renderfx = torso->renderfx;
		trap_R_AddRefEntityToScene( &stump );
		// pumping blood out sideways for a few seconds
		if ( p->limbTime[side] && cg.time - p->limbTime[side] < 6000 && cg.time >= p->bleedNext ) {
			p->bleedNext = cg.time + 45;
			n = 1 + ( cg.time - p->limbTime[side] < 2000 );
			for ( i = 0; i < n; i++ ) {
				VectorScale( torso->axis[1], sgn * ( 80 + 90 * random() ), vel );
				vel[0] += crandom() * 30;
				vel[1] += crandom() * 30;
				vel[2] += 40 + 60 * random();
				CG_GoreParticleDrop( sh, vel, 1.5f, 1300, NULL );
			}
		}
	}
}

// the gun drops out of a panicking hand for a moment
qboolean CG_HF_HideWeapon( centity_t *cent ) {
	int		c = cent->currentState.clientNum;

	if ( c < 0 || c >= MAX_CLIENTS || cent->currentState.number >= MAX_CLIENTS ) {
		return qfalse;
	}
	return hfp[c].panicTime && cg.time - hfp[c].panicTime < 1200 && ( CG_EN( c )->flags & DSEF_PANIC );
}

/*
=================
CG_HF_BodyShell

CG_AddRefEntityWithPowerups (every body part): cracks over armour that is being shot away.
=================
*/
void CG_HF_BodyShell( refEntity_t *ent, entityState_t *state ) {
	const cgEnemy_t	*e;
	refEntity_t		shell;
	int				stage, pct;
	float			pulse;

	if ( state->eType != ET_PLAYER || ( state->eFlags & EF_DEAD ) || state->number >= MAX_CLIENTS
		|| ( state->powerups & ( 1 << PW_INVIS ) ) ) {
		return;
	}
	CG_HF_Cvars();
	if ( !hf_cgCracks.integer || !hfm.registered ) {
		return;
	}
	e = CG_EN( state->clientNum );
	pct = e->armorPct;
	if ( pct <= 0 || pct > 80 ) {
		return;
	}
	stage = pct > 50 ? 0 : pct > 25 ? 1 : 2;
	shell = *ent;
	shell.customSkin = 0;
	shell.customShader = hfm.crack[stage];
	shell.shaderRGBA[0] = shell.shaderRGBA[1] = shell.shaderRGBA[2] = 255;
	shell.shaderRGBA[3] = (byte)( 185 + 35 * stage );
	trap_R_AddRefEntityToScene( &shell );
	// hot rims, pulsing faster as it gets worse
	pulse = 0.55f + 0.45f * sin( cg.time * ( 0.004f + 0.003f * stage ) + state->number );
	shell.customShader = hfm.glow[stage];
	shell.shaderRGBA[0] = (byte)( 200 * pulse * ( 0.4f + 0.2f * stage ) );
	shell.shaderRGBA[1] = (byte)( 150 * pulse * ( 0.4f + 0.2f * stage ) );
	shell.shaderRGBA[2] = (byte)( 120 * pulse * ( 0.4f + 0.2f * stage ) );
	shell.shaderRGBA[3] = 255;
	trap_R_AddRefEntityToScene( &shell );
}

// ------------------------------------------------------------------ events

static void CG_HF_Fragment( qhandle_t model, const vec3_t org, const vec3_t vel, float scale, int life ) {
	localEntity_t	*le;
	refEntity_t		*re;
	vec3_t			ang;

	if ( !model ) {
		return;
	}
	le = CG_AllocLocalEntity();
	re = &le->refEntity;
	le->leType = LE_FRAGMENT;
	le->startTime = cg.time;
	le->endTime = cg.time + life;
	VectorCopy( org, re->origin );
	VectorSet( ang, random() * 360, random() * 360, random() * 360 );
	AnglesToAxis( ang, re->axis );
	if ( scale != 1.0f ) {
		VectorScale( re->axis[0], scale, re->axis[0] );
		VectorScale( re->axis[1], scale, re->axis[1] );
		VectorScale( re->axis[2], scale, re->axis[2] );
		re->nonNormalizedAxes = qtrue;
	}
	re->hModel = model;
	le->pos.trType = TR_GRAVITY;
	VectorCopy( org, le->pos.trBase );
	VectorCopy( vel, le->pos.trDelta );
	le->pos.trTime = cg.time;
	le->bounceFactor = 0.35f;
	le->leBounceSoundType = LEBS_NONE;
	le->leMarkType = LEMT_NONE;
}

static void CG_HF_Limb( entityState_t *es, const vec3_t at, const vec3_t dir ) {
	int			c = es->otherEntityNum;
	int			limb = es->generic1;
	vec3_t		vel, out;
	qhandle_t	model;

	if ( c >= 0 && c < MAX_CLIENTS ) {
		hfp[c].limbTime[limb == HFB_LARM ? 0 : limb == HFB_RARM ? 1 : 2] = cg.time;
		hfp[c].bleedNext = cg.time;
	}
	model = limb == HFB_LEGS ? hfm.leg : hfm.arm;
	VectorScale( dir, 170 + 120 * random(), vel );
	vel[0] += crandom() * 70;
	vel[1] += crandom() * 70;
	vel[2] += 190 + 120 * random();
	CG_GoreLimbGib( model, at, vel, 1.0f, 1.0f, qfalse );
	if ( limb == HFB_LEGS ) {
		// both legs: a second one
		vel[0] += crandom() * 120;
		vel[1] += crandom() * 120;
		CG_GoreLimbGib( model, at, vel, 1.0f, 0.8f, qfalse );
	}
	VectorCopy( dir, out );
	out[2] += 0.4f;
	CG_GoreBloodBurst( at, out, 24, 300, 2, 7 );
	CG_FxSound( at, ENTITYNUM_WORLD, CHAN_AUTO, hfm.tear[rand() & 1] );
}

/*
=================
CG_HF_Event

EV_HITFEEL
=================
*/
void CG_HF_Event( entityState_t *es, const vec3_t position ) {
	int				c = es->otherEntityNum;
	int				v, cat;
	vec3_t			dir, vel, n, head;
	centity_t		*cent;
	static vec3_t	hot = { 1.0f, 0.7f, 0.35f };
	static vec3_t	white = { 1.0f, 0.95f, 0.85f };
	int				i;

	CG_HF_Cvars();
	if ( !hfm.registered ) {
		CG_HF_Register();
	}
	VectorCopy( es->angles2, dir );
	if ( VectorNormalize( dir ) < 0.5f ) {
		VectorSet( dir, crandom(), crandom(), 0.3f );
		VectorNormalize( dir );
	}
	cent = ( c >= 0 && c < MAX_GENTITIES ) ? &cg_entities[c] : NULL;

	switch ( es->eventParm ) {
	case HFX_STAGGER:
		if ( hf_cgFlinch.integer && c < MAX_CLIENTS ) {
			CG_HF_Kick( c, dir, 18 + es->generic1 * 0.06f );
		}
		break;

	case HFX_LIMB:
		CG_HF_Limb( es, position, dir );
		break;

	case HFX_HELMET:
		VectorScale( dir, 150, vel );
		vel[2] += 280;
		VectorCopy( position, head );
		if ( cent ) {
			VectorCopy( cent->lerpOrigin, head );
			head[2] += 30;
		}
		CG_GoreLimbGib( hfm.helmet, head, vel, 1.0f, 0.0f, qfalse );
		VectorNegate( dir, n );
		CG_FxSparks( head, n, white, 14, 260, 400, 0.4f, 2 );
		CG_FxSound( head, ENTITYNUM_WORLD, CHAN_AUTO, hfm.clang );
		break;

	case HFX_ARMORBREAK:
		if ( !hf_cgCracks.integer ) {
			break;
		}
		VectorCopy( position, head );
		if ( cent ) {
			VectorCopy( cent->lerpOrigin, head );
			head[2] += 8;
		}
		for ( i = 0; i < 9; i++ ) {
			VectorSet( vel, crandom() * 220, crandom() * 220, 120 + random() * 220 );
			VectorMA( vel, 120, dir, vel );
			CG_HF_Fragment( hfm.shard, head, vel, 0.35f + 0.25f * random(), 2500 + rand() % 1500 );
		}
		VectorSet( n, 0, 0, 1 );
		CG_FxSparks( head, n, hot, 26, 300, 500, 0.0f, 6 );
		CG_GoreLight( head, 160, 1.0f, 0.7f, 0.4f, 180 );
		CG_FxSound( head, ENTITYNUM_WORLD, CHAN_AUTO, hfm.shatter[rand() & 1] );
		break;

	case HFX_ARMORHIT:
		if ( !hf_cgCracks.integer ) {
			break;
		}
		VectorNegate( dir, n );
		CG_FxSparks( position, n, white, 8, 220, 300, 0.5f, 1 );
		CG_FxSound( position, ENTITYNUM_WORLD, CHAN_AUTO, hfm.crackSnd[rand() & 1] );
		break;

	case HFX_PANIC:
		if ( c < MAX_CLIENTS ) {
			hfp[c].panicTime = cg.time;
			v = CG_HF_Voice( c );
			trap_S_StartSound( NULL, c, CHAN_VOICE, hfm.panic[v][rand() & 1] );
			// the gun falls out of the hand
			if ( cent && cent->currentState.weapon > WP_NONE && cent->currentState.weapon < MAX_WEAPONS
				&& cg_weapons[cent->currentState.weapon].weaponModel ) {
				VectorSet( vel, crandom() * 60, crandom() * 60, 60 );
				if ( VectorLengthSquared( hfp[c].lastHand ) > 0 ) {
					VectorCopy( hfp[c].lastHand, head );
				} else {
					VectorCopy( cent->lerpOrigin, head );
				}
				CG_HF_Fragment( cg_weapons[cent->currentState.weapon].weaponModel, head, vel, 1.0f, 1100 );
			}
		}
		break;

	case HFX_CRAWL:
		if ( c < MAX_CLIENTS ) {
			trap_S_StartSound( NULL, c, CHAN_VOICE, hfm.groan[CG_HF_Voice( c )] );
		}
		break;

	case HFX_SCREAM:
		if ( !hf_cgScreams.integer || c < 0 || c >= MAX_CLIENTS ) {
			break;
		}
		cat = es->generic1;
		if ( cat < 0 || cat >= HFS_NUM ) {
			cat = HFS_GENERIC;
		}
		if ( cent ) {
			VectorCopy( cent->lerpOrigin, head );
		} else {
			VectorCopy( position, head );
		}
		head[2] += 24;
		if ( CG_PointContents( head, -1 ) & MASK_WATER ) {
			break;		// drowning: the stock gurgle plays (cg_event.c)
		}
		v = CG_HF_Voice( c );
		// a static source at the death spot: the client respawns long before a fall scream ends
		trap_S_StartSound( head, ENTITYNUM_WORLD, CHAN_AUTO, hfm.death[cat][v][rand() & 1] );
		if ( cg_debugEvents.integer ) {
			CG_Printf( "HitFeel scream %i %s voice %i\n", c, hf_catNames[cat], v );
		}
		break;

	default:
		break;
	}
}

// EV_DEATHx: cg_deathScreams replaces the stock *death sounds (the scream comes with EV_HITFEEL)
qboolean CG_HF_DeathScreams( void ) {
	CG_HF_Cvars();
	return hf_cgScreams.integer != 0;
}

// EV_WALLSPLAT (cg_physics.c): the body's thud
void CG_HF_WallSplat( const vec3_t origin, int speed ) {
	if ( !hfm.registered ) {
		CG_HF_Register();
	}
	CG_FxSound( origin, ENTITYNUM_WORLD, CHAN_AUTO, hfm.thud[speed > 900 ? 1 : 0] );
}
