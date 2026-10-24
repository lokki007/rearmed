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
// cg_killcam.c -- rearmed 0.5: side kill cam, death cam and the best-kill replay
//
// RECORDER: cg_local.h routes trap_R_AddRefEntityToScene / AddPolyToScene / AddLightToScene /
// RenderScene of every cgame file through the CG_KC_* wrappers below. While the main 3D view
// is built (CG_KillCamBeginFrame .. its RenderScene) the wrappers copy the refEntities and
// dlights into a ring of 25 Hz samples (about 6 s), plus every client's position.
//
// PLAYBACK: a "show" plays a stretch of samples in slow motion into a second scene on a
// side panel: the renderer gets the world again plus the interpolated recorded entities
// (matched sample to sample by model and nearest origin; animation frames blended across
// frame boundaries). The main view keeps running. Shows: KILL (notable kill by the player,
// rmkc from g_killcam.c), DEATH (the player's own death: the killer, rmkiller), REPLAY (the
// best kill of the match / floor, copied out of the ring into its own clip, played at the
// intermission: in the side gutter on wide screens, full size before the cards on 16:9).

#define CG_KC_RAW_TRAPS
#include "cg_local.h"
#include "../game/bg_killcam.h"

#define KC_SAMPLE_MSEC		40		// recording rate
#define KC_RING_SAMPLES		160
#define KC_RING_ENTS		24576
#define KC_RING_LIGHTS		2048
#define KC_CLIP_SAMPLES		80
#define KC_CLIP_ENTS		10240
#define KC_CLIP_LIGHTS		640
#define KC_SAMPLE_ENTS		448		// most entities kept per sample
#define KC_SAMPLE_SPRITES	96		// of which sprites (blood, smoke)
#define KC_SAMPLE_LIGHTS	12
#define KC_RECORD_RANGE		3000	// entities further from the eye are not recorded
#define KC_FRAME_ENTS		1000	// renderer: MAX_REFENTITIES (1023) per frame, all scenes together
#define KC_PIP_ENTS			420
#define KC_PIP_LIGHTS		6
#define KC_PIP_RANGE		2600
#define KC_POLYS			384
#define KC_POLYVERTS		3072
#define KC_POLY_RANGE		700
#define KC_MATCH_RANGE		160		// an entity moves at most this far between two samples

#define KC_KILL_PRE			450		// game msec shown before the kill / after it
#define KC_KILL_POST		1050
#define KC_DEATH_PRE		1400
#define KC_DEATH_POST		700
#define KC_REPLAY_PRE		1500
#define KC_REPLAY_POST		1100
#define KC_QUEUE_AGE		2600	// a queued kill cam older than this is dropped
#define KC_MERGE_TIME		1500	// a kill this soon after the shown one joins it
#define KC_MERGE_RANGE		800
#define KC_SLIDE_MSEC		220
#define KC_FADE_MSEC		260
#define KC_MAX_PENDING		8

enum { KCS_NONE, KCS_KILL, KCS_DEATH, KCS_REPLAY };

typedef struct {
	vec3_t		origin;
	vec3_t		oldorigin;
	vec3_t		lightingOrigin;
	vec3_t		axis[3];
	qhandle_t	hModel;
	qhandle_t	customSkin;
	qhandle_t	customShader;
	int			renderfx;
	short		frame, oldframe;
	float		backlerp;
	byte		reType, skinNum, nonNormal, pad;
	byte		rgba[4];
	float		shaderTime;
	float		radius, rotation;
	float		st[2];
} kcEnt_t;

typedef struct {
	vec3_t		origin;
	float		intensity, r, g, b;
	int			additive;
} kcLight_t;

typedef struct {
	int			time;
	int			firstEnt, numEnts;
	int			firstLight, numLights;
	int			valid[2], dead[2];		// client bits
	vec3_t		pos[MAX_CLIENTS];
} kcSample_t;

typedef struct {
	kcSample_t	*samples;
	int			maxSamples, numSamples;	// numSamples: written so far (absolute index)
	kcEnt_t		*ents;
	int			maxEnts, entWrite;
	kcLight_t	*lights;
	int			maxLights, lightWrite;
} kcStore_t;

typedef struct {
	int			time;		// cg.time of the kill (0 = none)
	int			victim;		// the one who died (KILL / REPLAY), the killer (DEATH)
	int			flags;
	int			weapon;
	int			quick;
	int			mod;
	int			score;
	vec3_t		pos;		// the victim at the kill
	vec3_t		from;		// the player at the kill
} kcKill_t;

typedef struct {
	int			kind;
	kcStore_t	*store;
	kcKill_t	k;
	float		t;			// playhead, game msec
	float		rate;
	int			tStart, tEnd, tKey2;
	int			realStart, realLast, fadeOut;
	vec3_t		keyPos;
	qboolean	camSet;
	vec3_t		camPos, lookPos;
	float		yawBase, yawSpan, dist, height, roll;
	int			side;		// -1 left, 1 right
	int			health, armor;
	qboolean	big;
	char		title[32], sub[64], name[40], tag[48];
} kcShow_t;

static kcSample_t	kc_ringSamples[KC_RING_SAMPLES];
static kcEnt_t		kc_ringEnts[KC_RING_ENTS];
static kcLight_t	kc_ringLights[KC_RING_LIGHTS];
static kcSample_t	kc_clipSamples[KC_CLIP_SAMPLES];
static kcEnt_t		kc_clipEnts[KC_CLIP_ENTS];
static kcLight_t	kc_clipLights[KC_CLIP_LIGHTS];
static kcStore_t	kc_ring, kc_clip;

static polyVert_t	kc_polyVerts[KC_POLYVERTS];
static struct {
	qhandle_t	shader;
	int			first, num;
} kc_polys[KC_POLYS];
static int			kc_numPolys, kc_numPolyVerts;

static byte			kc_used[KC_SAMPLE_ENTS];

static struct {
	qboolean	capturing;		// the main scene is being built
	qboolean	sampling;		// ... and this frame is a sample
	kcSample_t	cur;
	int			curSprites;
	int			nextSample;
	int			lastSample;
	int			mainEnts;
	qboolean	polyOn;
	vec3_t		polyFocus;
	int			frameDone;		// cg.clientFrame KC_Frame ran for
	qboolean	wasInter;
	qboolean	replayWanted;	// wide screens: the side replay at the intermission
	qboolean	gateDone;		// narrow screens: the full size replay has been started
	int			buttons, upmove, cmdNum;
	int			grainTime;
	float		grainS, grainT;
} kc;

static kcShow_t		kc_show;
static kcKill_t		kc_queued;
static kcKill_t		kc_pending[KC_MAX_PENDING];
static kcKill_t		kc_best;
static qboolean		kc_bestValid;
static kcKill_t		kc_lastKill;
static struct {
	int		client, health, armor, weapon, time;
} kc_killer;

static vmCvar_t		kc_cvKillCam;
static vmCvar_t		kc_cvSide;
static vmCvar_t		kc_cvReplays;
static vmCvar_t		kc_cvDeathCam;
static vmCvar_t		kc_cvDebug;

static qhandle_t	kc_grainShader, kc_vignetteShader;

static void KC_VLerp( const vec3_t a, const vec3_t b, float f, vec3_t out ) {
	out[0] = a[0] + ( b[0] - a[0] ) * f;
	out[1] = a[1] + ( b[1] - a[1] ) * f;
	out[2] = a[2] + ( b[2] - a[2] ) * f;
}

/*
===============================================================================

STORE

===============================================================================
*/

static void KC_StoreInit( kcStore_t *s, kcSample_t *samples, int maxSamples, kcEnt_t *ents, int maxEnts,
						 kcLight_t *lights, int maxLights ) {
	s->samples = samples;
	s->maxSamples = maxSamples;
	s->numSamples = 0;
	s->ents = ents;
	s->maxEnts = maxEnts;
	s->entWrite = 0;
	s->lights = lights;
	s->maxLights = maxLights;
	s->lightWrite = 0;
}

static kcSample_t *KC_Sample( kcStore_t *s, int abs ) {
	return &s->samples[abs % s->maxSamples];
}

static kcEnt_t *KC_Ent( kcStore_t *s, kcSample_t *sm, int i ) {
	return &s->ents[( sm->firstEnt + i ) % s->maxEnts];
}

static kcLight_t *KC_Light( kcStore_t *s, kcSample_t *sm, int i ) {
	return &s->lights[( sm->firstLight + i ) % s->maxLights];
}

static qboolean KC_SampleValid( kcStore_t *s, int abs ) {
	kcSample_t	*sm;

	if ( abs < 0 || abs >= s->numSamples || abs < s->numSamples - s->maxSamples ) {
		return qfalse;
	}
	sm = KC_Sample( s, abs );
	if ( s->entWrite - sm->firstEnt > s->maxEnts || s->lightWrite - sm->firstLight > s->maxLights ) {
		return qfalse;		// its entities were overwritten
	}
	return qtrue;
}

// oldest valid absolute sample index, -1 when empty
static int KC_Oldest( kcStore_t *s ) {
	int		i;

	if ( s->numSamples <= 0 || !KC_SampleValid( s, s->numSamples - 1 ) ) {
		return -1;
	}
	for ( i = s->numSamples - 1 ; i > 0 && KC_SampleValid( s, i - 1 ) ; i-- ) {
	}
	return i;
}

static int KC_NewestTime( kcStore_t *s ) {
	if ( s->numSamples <= 0 ) {
		return 0;
	}
	return KC_Sample( s, s->numSamples - 1 )->time;
}

static int KC_OldestTime( kcStore_t *s ) {
	int		o = KC_Oldest( s );

	return o < 0 ? 0 : KC_Sample( s, o )->time;
}

/*
=================
KC_Find

The two samples around time t and how far between them
=================
*/
static qboolean KC_Find( kcStore_t *s, float t, int *a, int *b, float *f ) {
	int		lo, hi, i, ta, tb;

	lo = KC_Oldest( s );
	if ( lo < 0 ) {
		return qfalse;
	}
	hi = s->numSamples - 1;
	*f = 0;
	if ( t <= KC_Sample( s, lo )->time ) {
		*a = *b = lo;
		return qtrue;
	}
	if ( t >= KC_Sample( s, hi )->time ) {
		*a = *b = hi;
		return qtrue;
	}
	for ( i = hi ; i > lo ; i-- ) {
		ta = KC_Sample( s, i - 1 )->time;
		if ( ta <= t ) {
			tb = KC_Sample( s, i )->time;
			*a = i - 1;
			*b = i;
			*f = tb > ta ? ( t - ta ) / (float)( tb - ta ) : 0;
			return qtrue;
		}
	}
	*a = *b = lo;
	return qtrue;
}

static qboolean KC_BitSet( const int *bits, int n ) {
	return ( bits[n >> 5] & ( 1 << ( n & 31 ) ) ) != 0;
}

/*
=================
KC_ClientPos

A client's recorded position at time t (qfalse: not in the recording then)
=================
*/
static qboolean KC_ClientPos( kcStore_t *s, int client, float t, vec3_t out ) {
	int			a, b;
	float		f;
	kcSample_t	*sa, *sb;
	qboolean	va_, vb_;

	if ( client < 0 || client >= MAX_CLIENTS || !KC_Find( s, t, &a, &b, &f ) ) {
		return qfalse;
	}
	sa = KC_Sample( s, a );
	sb = KC_Sample( s, b );
	va_ = KC_BitSet( sa->valid, client );
	vb_ = KC_BitSet( sb->valid, client );
	if ( va_ && vb_ && DistanceSquared( sa->pos[client], sb->pos[client] ) < 250 * 250 ) {
		KC_VLerp( sa->pos[client], sb->pos[client], f, out );
		return qtrue;
	}
	if ( va_ && ( f < 0.5f || !vb_ ) ) {
		VectorCopy( sa->pos[client], out );
		return qtrue;
	}
	if ( vb_ ) {
		VectorCopy( sb->pos[client], out );
		return qtrue;
	}
	return qfalse;
}

/*
===============================================================================

RECORDER (the trap wrappers)

===============================================================================
*/

static qboolean KC_Recording( void ) {
	return kc_cvKillCam.integer || kc_cvReplays.integer || kc_cvDeathCam.integer;
}

static void KC_ResetRecording( void ) {
	KC_StoreInit( &kc_ring, kc_ringSamples, KC_RING_SAMPLES, kc_ringEnts, KC_RING_ENTS, kc_ringLights, KC_RING_LIGHTS );
	kc.nextSample = 0;
	kc.lastSample = 0;
	kc.sampling = qfalse;
	if ( kc_show.store == &kc_ring ) {
		kc_show.kind = KCS_NONE;
	}
	kc_queued.time = 0;
}

void CG_KillCamBeginFrame( void ) {
	kcSample_t	*c = &kc.cur;

	trap_Cvar_Update( &kc_cvKillCam );
	trap_Cvar_Update( &kc_cvSide );
	trap_Cvar_Update( &kc_cvReplays );
	trap_Cvar_Update( &kc_cvDeathCam );
	trap_Cvar_Update( &kc_cvDebug );

	kc.capturing = qfalse;
	kc.sampling = qfalse;
	kc.mainEnts = 0;
	kc_numPolys = 0;
	kc_numPolyVerts = 0;
	if ( !cg.snap ) {
		return;
	}
	kc.capturing = qtrue;
	kc.polyOn = kc_show.kind != KCS_NONE && kc_show.camSet;
	VectorCopy( kc_show.lookPos, kc.polyFocus );

	if ( !KC_Recording() || cg.snap->ps.pm_type == PM_INTERMISSION ) {
		return;
	}
	if ( kc.lastSample && cg.time < kc.lastSample - 500 ) {
		KC_ResetRecording();		// map_restart, demo seek
	}
	if ( cg.time < kc.nextSample ) {
		return;
	}
	kc.nextSample += KC_SAMPLE_MSEC;
	if ( kc.nextSample <= cg.time ) {
		kc.nextSample = cg.time + KC_SAMPLE_MSEC;
	}
	kc.sampling = qtrue;
	c->time = cg.time;
	c->firstEnt = kc_ring.entWrite;
	c->numEnts = 0;
	c->firstLight = kc_ring.lightWrite;
	c->numLights = 0;
	kc.curSprites = 0;
}

static void KC_RecordEnt( const refEntity_t *re ) {
	kcEnt_t		*e;
	float		d;

	if ( re->reType == RT_PORTALSURFACE || ( re->renderfx & ( RF_FIRST_PERSON | RF_DEPTHHACK ) ) ) {
		return;
	}
	if ( kc.cur.numEnts >= KC_SAMPLE_ENTS ) {
		return;
	}
	if ( re->reType == RT_SPRITE ) {
		if ( kc.curSprites >= KC_SAMPLE_SPRITES ) {
			return;
		}
	}
	d = DistanceSquared( re->origin, cg.refdef.vieworg );
	if ( d > KC_RECORD_RANGE * KC_RECORD_RANGE ) {
		if ( re->reType == RT_MODEL || re->reType == RT_SPRITE
			|| DistanceSquared( re->oldorigin, cg.refdef.vieworg ) > KC_RECORD_RANGE * KC_RECORD_RANGE ) {
			return;
		}
	}
	if ( re->reType == RT_SPRITE ) {
		kc.curSprites++;
	}
	e = &kc_ring.ents[kc_ring.entWrite % kc_ring.maxEnts];
	kc_ring.entWrite++;
	kc.cur.numEnts++;

	VectorCopy( re->origin, e->origin );
	VectorCopy( re->oldorigin, e->oldorigin );
	if ( re->renderfx & RF_LIGHTING_ORIGIN ) {
		VectorCopy( re->lightingOrigin, e->lightingOrigin );
	} else {
		VectorCopy( re->origin, e->lightingOrigin );
	}
	VectorCopy( re->axis[0], e->axis[0] );
	VectorCopy( re->axis[1], e->axis[1] );
	VectorCopy( re->axis[2], e->axis[2] );
	e->hModel = re->hModel;
	e->customSkin = re->customSkin;
	e->customShader = re->customShader;
	e->renderfx = re->renderfx & ~( RF_THIRD_PERSON | RF_SHADOW_PLANE );
	e->frame = (short)re->frame;
	e->oldframe = (short)re->oldframe;
	e->backlerp = re->backlerp;
	e->reType = (byte)re->reType;
	e->skinNum = (byte)re->skinNum;
	e->nonNormal = (byte)( re->nonNormalizedAxes ? 1 : 0 );
	e->rgba[0] = re->shaderRGBA[0];
	e->rgba[1] = re->shaderRGBA[1];
	e->rgba[2] = re->shaderRGBA[2];
	e->rgba[3] = re->shaderRGBA[3];
	e->shaderTime = re->shaderTime;
	e->radius = re->radius;
	e->rotation = re->rotation;
	e->st[0] = re->shaderTexCoord[0];
	e->st[1] = re->shaderTexCoord[1];
}

static void KC_RecordLight( const vec3_t org, float intensity, float r, float g, float b, int additive ) {
	kcLight_t	*l;

	if ( !kc.sampling || kc.cur.numLights >= KC_SAMPLE_LIGHTS
		|| DistanceSquared( org, cg.refdef.vieworg ) > KC_RECORD_RANGE * KC_RECORD_RANGE ) {
		return;
	}
	l = &kc_ring.lights[kc_ring.lightWrite % kc_ring.maxLights];
	kc_ring.lightWrite++;
	kc.cur.numLights++;
	VectorCopy( org, l->origin );
	l->intensity = intensity;
	l->r = r;
	l->g = g;
	l->b = b;
	l->additive = additive;
}

void CG_KC_AddRefEntityToScene( const refEntity_t *re ) {
	if ( kc.capturing ) {
		kc.mainEnts++;
		if ( kc.sampling ) {
			KC_RecordEnt( re );
		}
	}
	trap_R_AddRefEntityToScene( re );
}

void CG_KC_AddPolyToScene( qhandle_t hShader, int numVerts, const polyVert_t *verts ) {
	// the PiP re-adds this frame's marks and particles around its focus
	if ( kc.capturing && kc.polyOn && numVerts > 0 && kc_numPolys < KC_POLYS
		&& kc_numPolyVerts + numVerts <= KC_POLYVERTS
		&& DistanceSquared( verts[0].xyz, kc.polyFocus ) < KC_POLY_RANGE * KC_POLY_RANGE ) {
		kc_polys[kc_numPolys].shader = hShader;
		kc_polys[kc_numPolys].first = kc_numPolyVerts;
		kc_polys[kc_numPolys].num = numVerts;
		memcpy( &kc_polyVerts[kc_numPolyVerts], verts, numVerts * sizeof( polyVert_t ) );
		kc_numPolyVerts += numVerts;
		kc_numPolys++;
	}
	trap_R_AddPolyToScene( hShader, numVerts, verts );
}

void CG_KC_AddLightToScene( const vec3_t org, float intensity, float r, float g, float b ) {
	if ( kc.capturing ) {
		KC_RecordLight( org, intensity, r, g, b, 0 );
	}
	trap_R_AddLightToScene( org, intensity, r, g, b );
}

void CG_KC_AddAdditiveLightToScene( const vec3_t org, float intensity, float r, float g, float b ) {
	if ( kc.capturing ) {
		KC_RecordLight( org, intensity, r, g, b, 1 );
	}
	trap_R_AddAdditiveLightToScene( org, intensity, r, g, b );
}

static void KC_CommitSample( void ) {
	kcSample_t	*c = &kc.cur;
	centity_t	*cent;
	int			i, me;

	c->valid[0] = c->valid[1] = 0;
	c->dead[0] = c->dead[1] = 0;
	me = cg.snap->ps.clientNum;
	for ( i = 0 ; i < cgs.maxclients && i < MAX_CLIENTS ; i++ ) {
		if ( i == me ) {
			if ( cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
				continue;
			}
			VectorCopy( cg.predictedPlayerState.origin, c->pos[i] );
			if ( cg.predictedPlayerState.stats[STAT_HEALTH] <= 0 ) {
				c->dead[i >> 5] |= 1 << ( i & 31 );
			}
		} else {
			cent = &cg_entities[i];
			if ( !cent->currentValid || cent->currentState.eType != ET_PLAYER ) {
				continue;
			}
			VectorCopy( cent->lerpOrigin, c->pos[i] );
			if ( cent->currentState.eFlags & EF_DEAD ) {
				c->dead[i >> 5] |= 1 << ( i & 31 );
			}
		}
		c->valid[i >> 5] |= 1 << ( i & 31 );
	}
	kc_ring.samples[kc_ring.numSamples % kc_ring.maxSamples] = *c;
	kc_ring.numSamples++;
	kc.lastSample = c->time;
}

void CG_KC_RenderScene( const refdef_t *fd ) {
	if ( kc.capturing && !( fd->rdflags & RDF_NOWORLDMODEL ) ) {
		if ( kc.sampling ) {
			KC_CommitSample();
		}
		kc.capturing = qfalse;
		kc.sampling = qfalse;
	}
	trap_R_RenderScene( fd );
}

/*
===============================================================================

PLAYBACK: interpolated entities

===============================================================================
*/

static void KC_ToRef( const kcEnt_t *e, refEntity_t *re ) {
	memset( re, 0, sizeof( *re ) );
	re->reType = (refEntityType_t)e->reType;
	re->renderfx = e->renderfx | RF_LIGHTING_ORIGIN;
	re->hModel = e->hModel;
	VectorCopy( e->lightingOrigin, re->lightingOrigin );
	VectorCopy( e->axis[0], re->axis[0] );
	VectorCopy( e->axis[1], re->axis[1] );
	VectorCopy( e->axis[2], re->axis[2] );
	re->nonNormalizedAxes = e->nonNormal ? qtrue : qfalse;
	VectorCopy( e->origin, re->origin );
	re->frame = e->frame;
	VectorCopy( e->oldorigin, re->oldorigin );
	re->oldframe = e->oldframe;
	re->backlerp = e->backlerp;
	re->skinNum = e->skinNum;
	re->customSkin = e->customSkin;
	re->customShader = e->customShader;
	re->shaderRGBA[0] = e->rgba[0];
	re->shaderRGBA[1] = e->rgba[1];
	re->shaderRGBA[2] = e->rgba[2];
	re->shaderRGBA[3] = e->rgba[3];
	re->shaderTexCoord[0] = e->st[0];
	re->shaderTexCoord[1] = e->st[1];
	re->shaderTime = e->shaderTime;
	re->radius = e->radius;
	re->rotation = e->rotation;
}

/*
=================
KC_LerpAnim

Model animation between two samples: across one frame boundary the pose keeps moving
forward (oldframe -> frame of A, then A's frame -> B's frame) instead of popping.
=================
*/
static void KC_LerpAnim( const kcEnt_t *a, const kcEnt_t *b, float f, refEntity_t *re ) {
	float	pA, pB, len, s;

	if ( a->frame == b->frame && a->oldframe == b->oldframe ) {
		re->frame = a->frame;
		re->oldframe = a->oldframe;
		re->backlerp = a->backlerp + ( b->backlerp - a->backlerp ) * f;
		return;
	}
	if ( b->oldframe == a->frame ) {
		pA = 1.0f - a->backlerp;
		pB = 1.0f - b->backlerp;
		len = ( 1.0f - pA ) + pB;
		s = f * len;
		if ( s < 1.0f - pA ) {
			re->frame = a->frame;
			re->oldframe = a->oldframe;
			re->backlerp = 1.0f - ( pA + s );
		} else {
			re->frame = b->frame;
			re->oldframe = a->frame;
			re->backlerp = 1.0f - ( s - ( 1.0f - pA ) );
		}
		if ( re->backlerp < 0 ) {
			re->backlerp = 0;
		} else if ( re->backlerp > 1 ) {
			re->backlerp = 1;
		}
		return;
	}
	if ( f >= 0.5f ) {
		re->frame = b->frame;
		re->oldframe = b->oldframe;
		re->backlerp = b->backlerp;
	}
}

static void KC_Lerp( const kcEnt_t *a, const kcEnt_t *b, float f, refEntity_t *re ) {
	int		i;

	KC_ToRef( a, re );
	KC_VLerp( a->origin, b->origin, f, re->origin );
	KC_VLerp( a->oldorigin, b->oldorigin, f, re->oldorigin );
	KC_VLerp( a->lightingOrigin, b->lightingOrigin, f, re->lightingOrigin );
	for ( i = 0 ; i < 3 ; i++ ) {
		KC_VLerp( a->axis[i], b->axis[i], f, re->axis[i] );
		if ( !a->nonNormal ) {
			VectorNormalize( re->axis[i] );
		}
	}
	if ( a->reType == RT_MODEL ) {
		KC_LerpAnim( a, b, f, re );
	}
	for ( i = 0 ; i < 4 ; i++ ) {
		re->shaderRGBA[i] = (byte)( a->rgba[i] + ( b->rgba[i] - a->rgba[i] ) * f );
	}
	re->radius = a->radius + ( b->radius - a->radius ) * f;
}

static qboolean KC_Visible( const refEntity_t *re, const vec3_t eye ) {
	if ( DistanceSquared( re->origin, eye ) < KC_PIP_RANGE * KC_PIP_RANGE ) {
		return qtrue;
	}
	return re->reType != RT_MODEL && re->reType != RT_SPRITE
		&& DistanceSquared( re->oldorigin, eye ) < KC_PIP_RANGE * KC_PIP_RANGE;
}

/*
=================
KC_EmitScene

Adds the recorded scene at time t to the (empty) render list. Returns the entity count.
=================
*/
static int KC_EmitScene( kcStore_t *s, float t, const vec3_t eye ) {
	int			a, b, i, j, k, n, budget, match, lights;
	float		f, d, best;
	kcSample_t	*sa, *sb;
	kcEnt_t		*ea, *eb;
	kcLight_t	*l;
	refEntity_t	re;

	if ( !KC_Find( s, t, &a, &b, &f ) ) {
		return 0;
	}
	budget = KC_FRAME_ENTS - kc.mainEnts;
	if ( budget > KC_PIP_ENTS ) {
		budget = KC_PIP_ENTS;
	}
	sa = KC_Sample( s, a );
	sb = KC_Sample( s, b );
	n = 0;
	if ( a == b ) {
		for ( i = 0 ; i < sa->numEnts && n < budget ; i++ ) {
			KC_ToRef( KC_Ent( s, sa, i ), &re );
			if ( KC_Visible( &re, eye ) ) {
				trap_R_AddRefEntityToScene( &re );
				n++;
			}
		}
	} else {
		memset( kc_used, 0, sizeof( kc_used ) );
		for ( i = 0 ; i < sa->numEnts && n < budget ; i++ ) {
			ea = KC_Ent( s, sa, i );
			match = -1;
			best = KC_MATCH_RANGE * KC_MATCH_RANGE;
			// same order is the common case: start the search at the same index
			for ( k = 0 ; k < sb->numEnts ; k++ ) {
				j = ( i + k ) % sb->numEnts;
				if ( kc_used[j] ) {
					continue;
				}
				eb = KC_Ent( s, sb, j );
				if ( eb->hModel != ea->hModel || eb->reType != ea->reType || eb->customShader != ea->customShader ) {
					continue;
				}
				d = DistanceSquared( ea->origin, eb->origin );
				if ( d < best ) {
					best = d;
					match = j;
					if ( d < 1.0f ) {
						break;
					}
				}
			}
			if ( match >= 0 ) {
				kc_used[match] = 1;
				KC_Lerp( ea, KC_Ent( s, sb, match ), f, &re );
			} else if ( f < 0.5f ) {
				KC_ToRef( ea, &re );
			} else {
				continue;
			}
			if ( KC_Visible( &re, eye ) ) {
				trap_R_AddRefEntityToScene( &re );
				n++;
			}
		}
		if ( f >= 0.5f ) {
			for ( j = 0 ; j < sb->numEnts && n < budget ; j++ ) {
				if ( kc_used[j] ) {
					continue;
				}
				KC_ToRef( KC_Ent( s, sb, j ), &re );
				if ( KC_Visible( &re, eye ) ) {
					trap_R_AddRefEntityToScene( &re );
					n++;
				}
			}
		}
	}

	// dlights: the nearer sample's
	if ( f >= 0.5f ) {
		sa = sb;
	}
	lights = 0;
	for ( i = 0 ; i < sa->numLights && lights < KC_PIP_LIGHTS ; i++ ) {
		l = KC_Light( s, sa, i );
		if ( DistanceSquared( l->origin, eye ) > 1500 * 1500 ) {
			continue;
		}
		if ( l->additive ) {
			trap_R_AddAdditiveLightToScene( l->origin, l->intensity, l->r, l->g, l->b );
		} else {
			trap_R_AddLightToScene( l->origin, l->intensity, l->r, l->g, l->b );
		}
		lights++;
	}

	// this frame's marks / particles near the focus (blood on the walls)
	for ( i = 0 ; i < kc_numPolys ; i++ ) {
		trap_R_AddPolyToScene( kc_polys[i].shader, kc_polys[i].num, &kc_polyVerts[kc_polys[i].first] );
	}
	return n;
}

/*
=================
KC_CopyClip

The best kill so far: copy its stretch of the ring into the replay clip
=================
*/
static void KC_CopyClip( const kcKill_t *k ) {
	int			lo, hi, s0, i, j, ents, lights, count;
	kcSample_t	*src, *dst;

	lo = KC_Oldest( &kc_ring );
	if ( lo < 0 ) {
		return;
	}
	hi = kc_ring.numSamples - 1;
	while ( hi > lo && KC_Sample( &kc_ring, hi )->time > k->time + KC_REPLAY_POST ) {
		hi--;
	}
	// walk back from the end while the clip has room
	ents = lights = count = 0;
	s0 = hi + 1;
	while ( s0 - 1 >= lo ) {
		src = KC_Sample( &kc_ring, s0 - 1 );
		if ( src->time < k->time - KC_REPLAY_PRE || count + 1 > KC_CLIP_SAMPLES
			|| ents + src->numEnts > KC_CLIP_ENTS || lights + src->numLights > KC_CLIP_LIGHTS ) {
			break;
		}
		ents += src->numEnts;
		lights += src->numLights;
		count++;
		s0--;
	}
	if ( count < 2 ) {
		return;
	}
	KC_StoreInit( &kc_clip, kc_clipSamples, KC_CLIP_SAMPLES, kc_clipEnts, KC_CLIP_ENTS, kc_clipLights, KC_CLIP_LIGHTS );
	for ( i = s0 ; i <= hi ; i++ ) {
		src = KC_Sample( &kc_ring, i );
		dst = &kc_clip.samples[kc_clip.numSamples];
		*dst = *src;
		dst->firstEnt = kc_clip.entWrite;
		dst->firstLight = kc_clip.lightWrite;
		for ( j = 0 ; j < src->numEnts ; j++ ) {
			kc_clip.ents[kc_clip.entWrite++] = *KC_Ent( &kc_ring, src, j );
		}
		for ( j = 0 ; j < src->numLights ; j++ ) {
			kc_clip.lights[kc_clip.lightWrite++] = *KC_Light( &kc_ring, src, j );
		}
		kc_clip.numSamples++;
	}
	kc_best = *k;
	kc_bestValid = qtrue;
	if ( kc_cvDebug.integer ) {
		CG_Printf( "KillCam: best kill now victim %i flags 0x%x score %i (%i samples, %i ents)\n",
			k->victim, k->flags, k->score, kc_clip.numSamples, kc_clip.entWrite );
	}
}

/*
===============================================================================

KILLS: score and labels

===============================================================================
*/

static int KC_Score( int flags, int quick ) {
	int		s = 10;

	if ( flags & KCF_BOSS )		s += 200;
	if ( flags & KCF_FINAL )	s += 120;
	if ( flags & KCF_NEMESIS )	s += 90;
	if ( flags & KCF_PHASE )	s += 70;
	if ( flags & KCF_HEADSHOT )	s += 40;
	if ( flags & KCF_AIRSHOT )	s += 40;
	if ( flags & KCF_ENV )		s += 40;
	if ( flags & KCF_ELITE )	s += 40;
	if ( flags & KCF_GIB )		s += 30;
	if ( flags & KCF_MIDAIR )	s += 30;
	if ( flags & KCF_MELEE )	s += 25;
	if ( flags & KCF_OVERKILL )	s += 15;
	if ( flags & KCF_CHARGED )	s += 10;
	if ( quick >= 2 ) {
		s += quick >= 4 ? 120 : 40 * ( quick - 1 );
	}
	return s;
}

static const char *KC_EnvLabel( int mod ) {
	switch ( mod ) {
	case MOD_LAVA:			return "INTO THE LAVA";
	case MOD_SLIME:			return "SLIMED";
	case MOD_WATER:			return "DROWNED";
	case MOD_CRUSH:			return "CRUSHED";
	case MOD_WALLSPLAT:		return "SPLATTERED";
	default:				return "VOID KILL";
	}
}

/*
=================
KC_Describe

Title (the best label) and up to two more, joined
=================
*/
static void KC_Describe( const kcKill_t *k, char *title, int tsize, char *sub, int ssize ) {
	const char	*labels[16];
	int			n = 0, i, fl = k->flags;

	if ( fl & KCF_BOSS )		labels[n++] = "BOSS SLAIN";
	if ( fl & KCF_FINAL )		labels[n++] = "FINAL FRAG";
	if ( fl & KCF_NEMESIS )		labels[n++] = "NEMESIS SLAIN";
	if ( fl & KCF_PHASE )		labels[n++] = "PHASE BROKEN";
	if ( k->quick >= 3 )		labels[n++] = "MULTI KILL";
	else if ( k->quick == 2 )	labels[n++] = "DOUBLE KILL";
	if ( fl & KCF_HEADSHOT )	labels[n++] = "HEADSHOT";
	if ( fl & KCF_OVERKILL )	labels[n++] = "OVERKILL";
	else if ( fl & KCF_GIB )	labels[n++] = "GIBBED";
	if ( fl & KCF_AIRSHOT )		labels[n++] = "AIRSHOT";
	if ( fl & KCF_MIDAIR )		labels[n++] = "MIDAIR";
	if ( fl & KCF_ENV )			labels[n++] = KC_EnvLabel( k->mod );
	if ( fl & KCF_ELITE )		labels[n++] = "ELITE DOWN";
	if ( fl & KCF_MELEE )		labels[n++] = "HUMILIATION";
	if ( fl & KCF_CHARGED )		labels[n++] = "CHARGED";
	if ( !n ) {
		labels[n++] = "KILL";
	}
	Q_strncpyz( title, labels[0], tsize );
	sub[0] = 0;
	for ( i = 1 ; i < n && i < 3 ; i++ ) {
		if ( i > 1 ) {
			Q_strcat( sub, ssize, "  +  " );
		}
		Q_strcat( sub, ssize, labels[i] );
	}
}

static const char *KC_WeaponName( int w ) {
	gitem_t		*it;
	static char	buf[32];

	if ( w <= WP_NONE || w >= WP_NUM_WEAPONS ) {
		return "";
	}
	it = BG_FindItemForWeapon( (weapon_t)w );
	if ( !it || !it->pickup_name ) {
		return "";
	}
	Q_strncpyz( buf, it->pickup_name, sizeof( buf ) );
	Q_strupr( buf );
	return buf;
}

static void KC_ClientName( int client, char *out, int size ) {
	if ( client < 0 || client >= MAX_CLIENTS || !cgs.clientinfo[client].infoValid ) {
		Q_strncpyz( out, "", size );
		return;
	}
	Q_strncpyz( out, cgs.clientinfo[client].name, size );
	Q_CleanStr( out );
}

/*
===============================================================================

SHOWS

===============================================================================
*/

static qboolean KC_Wide( void ) {
	float	vw = cgs.glconfig.vidWidth, vh = cgs.glconfig.vidHeight;

	return ( vw - vh * 16.0f / 9.0f ) * 0.5f >= vh * 0.30f;
}

static int KC_SideFor( const vec3_t pos ) {
	vec3_t	d;

	if ( kc_cvSide.integer == 1 ) {
		return -1;
	}
	if ( kc_cvSide.integer == 2 ) {
		return 1;
	}
	if ( !KC_Wide() ) {
		return -1;		// 16:9: the left edge, clear of the right side callouts
	}
	VectorSubtract( pos, cg.refdef.vieworg, d );
	return DotProduct( d, cg.refdef.viewaxis[1] ) >= 0 ? -1 : 1;	// the side it happened on
}

static float KC_YawTo( const vec3_t from, const vec3_t to ) {
	float	dx = to[0] - from[0], dy = to[1] - from[1];

	if ( dx * dx + dy * dy < 1 ) {
		return 0;
	}
	return RAD2DEG( atan2( dy, dx ) );
}

static void KC_CamSpot( const vec3_t look, float yaw, float dist, float height, vec3_t out ) {
	out[0] = look[0] + cos( DEG2RAD( yaw ) ) * dist;
	out[1] = look[1] + sin( DEG2RAD( yaw ) ) * dist;
	out[2] = look[2] + height;
}

static float KC_Clearance( const vec3_t look, const vec3_t spot, vec3_t endpos ) {
	trace_t		tr;
	vec3_t		mins = { -8, -8, -8 }, maxs = { 8, 8, 8 };

	CG_Trace( &tr, look, mins, maxs, spot, ENTITYNUM_NONE, MASK_SOLID );
	if ( tr.startsolid || tr.allsolid ) {
		VectorCopy( look, endpos );
		return 0;
	}
	VectorCopy( tr.endpos, endpos );
	return tr.fraction;
}

/*
=================
KC_PickYaw

An orbit start with room for the camera: off to the side of the line of fire first
=================
*/
static float KC_PickYaw( const vec3_t focus, const vec3_t from, float dist, float height, float preferred ) {
	// never straight down the line of fire: the killer's own body would fill the frame
	static const float	offs[8] = { 45, -45, 80, -80, 120, -120, 160, -160 };
	vec3_t	look, spot, end;
	float	line, best, bestYaw, fr;
	int		i;

	VectorCopy( focus, look );
	look[2] += 12;
	line = KC_YawTo( focus, from ) + preferred;
	best = -1;
	bestYaw = line;
	for ( i = 0 ; i < 8 ; i++ ) {
		KC_CamSpot( look, line + offs[i], dist, height, spot );
		fr = KC_Clearance( look, spot, end );
		if ( fr > 0.92f ) {
			return line + offs[i];
		}
		if ( fr > best ) {
			best = fr;
			bestYaw = line + offs[i];
		}
	}
	return bestYaw;
}

static void KC_EndShow( void ) {
	kc_show.kind = KCS_NONE;
	kc_show.camSet = qfalse;
}

static void KC_InitShow( int kind, kcStore_t *store, const kcKill_t *k ) {
	memset( &kc_show, 0, sizeof( kc_show ) );
	kc_show.kind = kind;
	kc_show.store = store;
	kc_show.k = *k;
	kc_show.realStart = kc_show.realLast = trap_Milliseconds();
	VectorCopy( k->pos, kc_show.keyPos );
	KC_ClientPos( store, k->victim, k->time, kc_show.keyPos );
	kc_show.side = KC_SideFor( kc_show.keyPos );
	kc_show.rate = 0.6f;
}

static void KC_StartKill( const kcKill_t *k ) {
	int		oldest;

	KC_InitShow( KCS_KILL, &kc_ring, k );
	oldest = KC_OldestTime( &kc_ring );
	kc_show.tStart = k->time - KC_KILL_PRE;
	if ( kc_show.tStart < oldest ) {
		kc_show.tStart = oldest;
	}
	kc_show.tEnd = k->time + KC_KILL_POST;
	kc_show.t = kc_show.tStart;
	kc_show.dist = ( k->flags & KCF_BOSS ) ? 270 : ( k->flags & KCF_GIB ) ? 200 : 150;
	kc_show.height = ( k->flags & KCF_GIB ) ? 52 : 34;
	kc_show.yawSpan = 55;
	kc_show.roll = ( k->flags & ( KCF_GIB | KCF_HEADSHOT ) ) ? 4 : 0;
	kc_show.yawBase = KC_PickYaw( kc_show.keyPos, k->from, kc_show.dist, kc_show.height, 0 );
	KC_Describe( k, kc_show.title, sizeof( kc_show.title ), kc_show.sub, sizeof( kc_show.sub ) );
	KC_ClientName( k->victim, kc_show.name, sizeof( kc_show.name ) );
	Q_strncpyz( kc_show.tag, "KILL CAM", sizeof( kc_show.tag ) );
	if ( k->weapon > WP_NONE && k->weapon < WP_NUM_WEAPONS ) {
		CG_RegisterWeapon( k->weapon );
	}
	if ( kc_cvDebug.integer ) {
		CG_Printf( "KillCam: kill cam victim %i flags 0x%x score %i \"%s\" side %i\n", k->victim, k->flags,
			k->score, kc_show.title, kc_show.side );
	}
}

static void KC_StartDeath( int killer, int mod ) {
	kcKill_t	k;
	int			oldest;

	memset( &k, 0, sizeof( k ) );
	k.time = cg.time;
	k.victim = killer;
	k.mod = mod;
	k.weapon = cg_entities[killer].currentState.weapon;
	if ( kc_killer.client == killer && cg.time - kc_killer.time < 1000 && kc_killer.weapon > WP_NONE ) {
		k.weapon = kc_killer.weapon;
	}
	VectorCopy( cg_entities[killer].lerpOrigin, k.pos );
	VectorCopy( cg.predictedPlayerState.origin, k.from );

	KC_InitShow( KCS_DEATH, &kc_ring, &k );
	oldest = KC_OldestTime( &kc_ring );
	kc_show.tStart = k.time - KC_DEATH_PRE;
	if ( kc_show.tStart < oldest ) {
		kc_show.tStart = oldest;
	}
	kc_show.tEnd = k.time + KC_DEATH_POST;
	kc_show.t = kc_show.tStart;
	kc_show.dist = ( cgs.clientinfo[killer].infoValid && CG_RL_ClientRole( killer ) == 4 ) ? 240 : 125;
	kc_show.height = 24;
	kc_show.yawSpan = 40;
	// from the player's side of the killer, a little off the line
	kc_show.yawBase = KC_PickYaw( kc_show.keyPos, k.from, kc_show.dist, kc_show.height, 30 );
	kc_show.health = -1;
	if ( kc_killer.client == killer && cg.time - kc_killer.time < 1000 ) {
		kc_show.health = kc_killer.health;
		kc_show.armor = kc_killer.armor;
	}
	Q_strncpyz( kc_show.title, "KILLED BY", sizeof( kc_show.title ) );
	KC_ClientName( killer, kc_show.name, sizeof( kc_show.name ) );
	Q_strncpyz( kc_show.tag, "DEATH CAM", sizeof( kc_show.tag ) );
	if ( k.weapon > WP_NONE && k.weapon < WP_NUM_WEAPONS ) {
		CG_RegisterWeapon( k.weapon );
	}
	kc_queued.time = 0;
	if ( kc_cvDebug.integer ) {
		CG_Printf( "KillCam: death cam killer %i health %i\n", killer, kc_show.health );
	}
}

static void KC_StartReplay( qboolean big ) {
	const kcKill_t	*k = &kc_best;

	KC_InitShow( KCS_REPLAY, &kc_clip, k );
	kc_show.big = big;
	kc_show.tStart = k->time - KC_REPLAY_PRE;
	if ( kc_show.tStart < KC_OldestTime( &kc_clip ) ) {
		kc_show.tStart = KC_OldestTime( &kc_clip );
	}
	kc_show.tEnd = k->time + KC_REPLAY_POST;
	if ( kc_show.tEnd > KC_NewestTime( &kc_clip ) ) {
		kc_show.tEnd = KC_NewestTime( &kc_clip );
	}
	kc_show.t = kc_show.tStart;
	kc_show.dist = ( k->flags & KCF_BOSS ) ? 290 : ( k->flags & KCF_GIB ) ? 215 : 170;
	kc_show.height = ( k->flags & KCF_GIB ) ? 56 : 38;
	kc_show.yawSpan = 85;
	kc_show.roll = ( k->flags & ( KCF_GIB | KCF_HEADSHOT ) ) ? 3 : 0;
	kc_show.yawBase = KC_PickYaw( kc_show.keyPos, k->from, kc_show.dist, kc_show.height, 0 );
	KC_Describe( k, kc_show.title, sizeof( kc_show.title ), kc_show.sub, sizeof( kc_show.sub ) );
	KC_ClientName( k->victim, kc_show.name, sizeof( kc_show.name ) );
	Com_sprintf( kc_show.tag, sizeof( kc_show.tag ), "REPLAY: %s %s", KC_WeaponName( k->weapon ), kc_show.title );
	if ( k->weapon > WP_NONE && k->weapon < WP_NUM_WEAPONS ) {
		CG_RegisterWeapon( k->weapon );
	}
	if ( kc_cvDebug.integer ) {
		CG_Printf( "KillCam: replay victim %i flags 0x%x score %i \"%s\" big %i\n", k->victim, k->flags,
			k->score, kc_show.title, big );
	}
}

/*
=================
KC_OnKill

A kill by the player (rmkc), or a notable moment from another module
=================
*/
static void KC_OnKill( kcKill_t *k ) {
	int		i, slot;
	qboolean	notable;

	// the same kill reported twice (rmkc and a module's notable call, either order): one record
	if ( kc_lastKill.time && kc_lastKill.victim == k->victim && k->time >= kc_lastKill.time
		&& k->time - kc_lastKill.time < 300 ) {
		k->flags |= kc_lastKill.flags;
		if ( kc_lastKill.quick > k->quick ) {
			k->quick = kc_lastKill.quick;
		}
		k->time = kc_lastKill.time;
		for ( i = 0 ; i < KC_MAX_PENDING ; i++ ) {
			if ( kc_pending[i].time == kc_lastKill.time && kc_pending[i].victim == k->victim ) {
				kc_pending[i].time = 0;
			}
		}
		if ( kc_show.kind == KCS_KILL && kc_show.k.time == k->time && kc_show.k.victim == k->victim ) {
			kc_show.k.flags = k->flags;
			kc_show.k.quick = k->quick;
			KC_Describe( &kc_show.k, kc_show.title, sizeof( kc_show.title ), kc_show.sub, sizeof( kc_show.sub ) );
			kc_show.k.score = KC_Score( k->flags, k->quick );
			k->score = kc_show.k.score;
			kc_lastKill = *k;
			for ( i = 0 ; i < KC_MAX_PENDING ; i++ ) {
				if ( !kc_pending[i].time ) {
					kc_pending[i] = *k;
					break;
				}
			}
			return;
		}
	}
	k->score = KC_Score( k->flags, k->quick );
	kc_lastKill = *k;

	// replay candidates: judged once the moments after the kill are recorded
	if ( kc_cvReplays.integer ) {
		slot = 0;
		for ( i = 0 ; i < KC_MAX_PENDING ; i++ ) {
			if ( !kc_pending[i].time ) {
				slot = i;
				break;
			}
			if ( kc_pending[i].time < kc_pending[slot].time ) {
				slot = i;
			}
		}
		kc_pending[slot] = *k;
	}

	if ( kc_cvDebug.integer ) {
		CG_Printf( "KillCam: kill of %i flags 0x%x quick %i (killcam %i, health %i, show %i)\n", k->victim, k->flags,
			k->quick, kc_cvKillCam.integer, cg.predictedPlayerState.stats[STAT_HEALTH], kc_show.kind );
	}
	if ( !kc_cvKillCam.integer || cg.snap->ps.pm_type == PM_INTERMISSION || cg.predictedPlayerState.stats[STAT_HEALTH] <= 0 ) {
		return;
	}
	notable = ( k->flags & KCF_NOTABLE ) || k->quick >= 2 || kc_cvKillCam.integer >= 2;
	if ( !notable ) {
		return;
	}
	if ( kc_show.kind == KCS_KILL && !kc_show.fadeOut && k->time - kc_show.k.time < KC_MERGE_TIME
		&& Distance( k->pos, kc_show.keyPos ) < KC_MERGE_RANGE ) {
		// a multi kill in the same spot: one longer kill cam
		kc_show.k.flags |= k->flags;
		if ( k->quick > kc_show.k.quick ) {
			kc_show.k.quick = k->quick;
		}
		kc_show.k.score = KC_Score( kc_show.k.flags, kc_show.k.quick );
		kc_show.tEnd = k->time + KC_KILL_POST;
		kc_show.tKey2 = k->time;
		KC_Describe( &kc_show.k, kc_show.title, sizeof( kc_show.title ), kc_show.sub, sizeof( kc_show.sub ) );
		return;
	}
	if ( kc_show.kind == KCS_NONE || ( kc_show.kind == KCS_KILL && k->score >= kc_show.k.score + 60 )
		|| kc_show.kind == KCS_DEATH ) {		// (alive again: the death cam is over)
		KC_StartKill( k );		// (a much better kill cuts the shown one: the boss over a phase break)
		return;
	}
	if ( kc_show.kind == KCS_KILL && ( !kc_queued.time || k->score >= kc_queued.score ) ) {
		kc_queued = *k;
	}
}

static void KC_KillFromCommand( void ) {
	kcKill_t	k;
	centity_t	*cent;

	memset( &k, 0, sizeof( k ) );
	k.time = cg.time;
	k.victim = atoi( CG_Argv( 1 ) );
	k.flags = atoi( CG_Argv( 2 ) );
	k.weapon = atoi( CG_Argv( 3 ) );
	k.quick = atoi( CG_Argv( 4 ) );
	k.mod = atoi( CG_Argv( 5 ) );
	if ( k.victim < 0 || k.victim >= MAX_CLIENTS ) {
		return;
	}
	cent = &cg_entities[k.victim];
	VectorCopy( cent->lerpOrigin, k.pos );
	VectorCopy( cg.predictedPlayerState.origin, k.from );
	KC_OnKill( &k );
}

qboolean CG_KillCamServerCommand( const char *cmd ) {
	if ( !strcmp( cmd, "rmkc" ) ) {
		if ( cg.snap ) {
			KC_KillFromCommand();
		}
		return qtrue;
	}
	if ( !strcmp( cmd, "rmkiller" ) ) {
		kc_killer.client = atoi( CG_Argv( 1 ) );
		kc_killer.health = atoi( CG_Argv( 2 ) );
		kc_killer.armor = atoi( CG_Argv( 3 ) );
		kc_killer.weapon = atoi( CG_Argv( 4 ) );
		kc_killer.time = cg.time;
		if ( kc_show.kind == KCS_DEATH && kc_show.k.victim == kc_killer.client ) {
			kc_show.health = kc_killer.health;
			kc_show.armor = kc_killer.armor;
			if ( kc_killer.weapon > WP_NONE && kc_killer.weapon < WP_NUM_WEAPONS ) {
				kc_show.k.weapon = kc_killer.weapon;
				CG_RegisterWeapon( kc_killer.weapon );
			}
		}
		return qtrue;
	}
	return qfalse;
}

void CG_KillCamObituary( int target, int attacker, int mod ) {
	if ( !cg.snap || target != cg.snap->ps.clientNum || !kc_cvDeathCam.integer ) {
		return;
	}
	if ( attacker < 0 || attacker >= MAX_CLIENTS || attacker == target || cg.snap->ps.pm_type == PM_INTERMISSION ) {
		return;
	}
	if ( kc_show.kind == KCS_REPLAY ) {
		return;
	}
	KC_StartDeath( attacker, mod );
}

void CG_KillCamNotable( int clientNum, int kind ) {
	kcKill_t	k;
	int			i, flag;

	if ( !cg.snap || clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}
	flag = kind == KCN_NEMESIS ? KCF_NEMESIS : KCF_PHASE;
	// the kill it belongs to (the nemesis kill command comes with the rmkc)
	if ( kind == KCN_NEMESIS && kc_lastKill.time && kc_lastKill.victim == clientNum && cg.time - kc_lastKill.time < 2000 ) {
		for ( i = 0 ; i < KC_MAX_PENDING ; i++ ) {
			if ( kc_pending[i].time == kc_lastKill.time && kc_pending[i].victim == clientNum ) {
				kc_pending[i].flags |= flag;
				kc_pending[i].score = KC_Score( kc_pending[i].flags, kc_pending[i].quick );
			}
		}
		if ( kc_bestValid && kc_best.time == kc_lastKill.time ) {
			kc_best.flags |= flag;
			kc_best.score = KC_Score( kc_best.flags, kc_best.quick );
		}
		kc_lastKill.flags |= flag;
		if ( kc_show.kind == KCS_KILL && kc_show.k.time == kc_lastKill.time ) {
			kc_show.k.flags |= flag;
			KC_Describe( &kc_show.k, kc_show.title, sizeof( kc_show.title ), kc_show.sub, sizeof( kc_show.sub ) );
			return;
		}
		if ( ( kc_show.kind == KCS_NONE || kc_show.kind == KCS_DEATH ) && kc_cvKillCam.integer
			&& cg.predictedPlayerState.stats[STAT_HEALTH] > 0 ) {
			kc_lastKill.score = KC_Score( kc_lastKill.flags, kc_lastKill.quick );
			KC_StartKill( &kc_lastKill );
		}
		return;
	}
	memset( &k, 0, sizeof( k ) );
	k.time = cg.time;
	k.victim = clientNum;
	k.flags = flag | ( kind == KCN_PHASE ? KCF_BOSS : 0 );
	k.weapon = cg.snap->ps.weapon;
	k.quick = 1;
	VectorCopy( cg_entities[clientNum].lerpOrigin, k.pos );
	VectorCopy( cg.predictedPlayerState.origin, k.from );
	if ( kind == KCN_PHASE ) {
		k.flags &= ~KCF_BOSS;		// not slain: the label is PHASE BROKEN
	}
	KC_OnKill( &k );
}

qboolean CG_KillCamWantsArea( const vec3_t org ) {
	if ( kc_show.kind != KCS_NONE && kc_show.camSet && DistanceSquared( org, kc_show.lookPos ) < 900 * 900 ) {
		return qtrue;
	}
	return kc_lastKill.time && cg.time - kc_lastKill.time < 3000 && cg.time >= kc_lastKill.time
		&& DistanceSquared( org, kc_lastKill.pos ) < 900 * 900;
}

/*
===============================================================================

PER FRAME

===============================================================================
*/

static void KC_ReadSkip( qboolean *pressed ) {
	usercmd_t	cmd;
	int			n = trap_GetCurrentCmdNumber();

	*pressed = qfalse;
	if ( n == kc.cmdNum || !trap_GetUserCmd( n, &cmd ) ) {
		return;
	}
	kc.cmdNum = n;
	if ( ( cmd.buttons & BUTTON_ATTACK ) && !( kc.buttons & BUTTON_ATTACK ) ) {
		*pressed = qtrue;
	}
	if ( cmd.upmove > 0 && kc.upmove <= 0 ) {
		*pressed = qtrue;
	}
	kc.buttons = cmd.buttons;
	kc.upmove = cmd.upmove;
}

static float KC_TargetRate( void ) {
	float	d, d2;

	d = kc_show.t - kc_show.k.time;
	if ( kc_show.tKey2 ) {
		d2 = kc_show.t - kc_show.tKey2;
		if ( fabs( d2 ) < fabs( d ) ) {
			d = d2;
		}
	}
	switch ( kc_show.kind ) {
	case KCS_DEATH:
		return d < -500 ? 0.55f : d < 150 ? 0.32f : 0.6f;
	case KCS_REPLAY:
		return d < -350 ? 0.7f : d < 320 ? 0.24f : 0.45f;
	default:
		return d < -60 ? 0.62f : d < 280 ? 0.26f : 0.5f;
	}
}

/*
=================
KC_Advance

Moves the playhead (real time based: the main view's slow motion doesn't double up)
=================
*/
static void KC_Advance( void ) {
	int			real, dt, newest;
	float		target;
	qboolean	skip;

	real = trap_Milliseconds();
	dt = real - kc_show.realLast;
	kc_show.realLast = real;
	if ( dt < 0 ) {
		dt = 0;
	} else if ( dt > 100 ) {
		dt = 100;
	}
	target = KC_TargetRate();
	kc_show.rate += ( target - kc_show.rate ) * ( dt / ( dt + 110.0f ) );
	kc_show.t += dt * kc_show.rate;
	newest = KC_NewestTime( kc_show.store );
	if ( kc_show.t > newest ) {
		kc_show.t = newest;		// the live ring can't run ahead of the recording
	}

	if ( kc_show.big ) {
		KC_ReadSkip( &skip );
		if ( skip && real - kc_show.realStart > 350 ) {
			KC_EndShow();
			return;
		}
	}
	if ( !kc_show.fadeOut ) {
		if ( kc_show.t >= kc_show.tEnd || real - kc_show.realStart > 9000
			|| ( kc_show.store == &kc_ring && kc_show.t >= newest
				&& ( real - kc_show.realStart > 5000 || cg.snap->ps.pm_type == PM_INTERMISSION ) ) ) {
			kc_show.fadeOut = real;
		}
		if ( kc_show.kind == KCS_DEATH && cg.predictedPlayerState.stats[STAT_HEALTH] > 0
			&& real - kc_show.realStart > 300 ) {
			kc_show.fadeOut = real;		// respawned
		}
	}
	if ( kc_show.fadeOut && real - kc_show.fadeOut > KC_FADE_MSEC ) {
		KC_EndShow();
	}
}

/*
=================
KC_UpdateCamera
=================
*/
static void KC_UpdateCamera( int dt ) {
	vec3_t		focus, look, spot, cam;
	float		p, ease, yaw, dist, k1, k2, since;

	if ( !KC_ClientPos( kc_show.store, kc_show.k.victim, kc_show.t, focus ) ) {
		VectorCopy( kc_show.keyPos, focus );
	}
	if ( kc_show.kind != KCS_DEATH && Distance( focus, kc_show.keyPos ) > ( kc_show.t > kc_show.k.time ? 250 : 400 ) ) {
		VectorCopy( kc_show.keyPos, focus );		// respawned / teleported: stay on the spot
	}
	VectorCopy( focus, look );
	if ( kc_show.kind == KCS_DEATH ) {
		look[2] += 20;
	} else {
		since = CG_Clamp01( ( kc_show.t - kc_show.k.time ) / 600.0f );
		look[2] += 12 - 20 * since;		// follow the body down
	}

	p = CG_Clamp01( ( kc_show.t - kc_show.tStart ) / (float)( kc_show.tEnd - kc_show.tStart + 1 ) );
	ease = p * p * ( 3 - 2 * p );
	yaw = kc_show.yawBase + kc_show.yawSpan * ( ease - 0.5f );
	dist = kc_show.dist * ( 1.08f - 0.18f * ease );
	KC_CamSpot( look, yaw, dist, kc_show.height * ( 1.0f - 0.3f * ease ), spot );
	KC_Clearance( look, spot, cam );

	if ( !kc_show.camSet ) {
		VectorCopy( cam, kc_show.camPos );
		VectorCopy( look, kc_show.lookPos );
		kc_show.camSet = qtrue;
		return;
	}
	k1 = dt / ( dt + 70.0f );
	k2 = dt / ( dt + 55.0f );
	KC_VLerp( kc_show.camPos, cam, k1, kc_show.camPos );
	KC_VLerp( kc_show.lookPos, look, k2, kc_show.lookPos );
}

// cg_killCamDebug >= 2: average fps every 2 s, and whether a panel was up the whole time
static void KC_Bench( qboolean showing ) {
	static int	start, frames, shown;
	int			now;

	if ( kc_cvDebug.integer < 2 ) {
		return;
	}
	now = trap_Milliseconds();
	if ( !start ) {
		start = now;
		frames = shown = 0;
	}
	frames++;
	if ( showing ) {
		shown++;
	}
	if ( now - start >= 2000 ) {
		CG_Printf( "KillCam bench: %.1f fps, panel %i%% of frames\n", frames * 1000.0f / ( now - start ), shown * 100 / frames );
		start = now;
		frames = shown = 0;
	}
}

static void KC_Frame( void ) {
	int		i, dt, real;
	qboolean	inter;

	if ( !cg.snap || kc.frameDone == cg.clientFrame ) {
		return;
	}
	kc.frameDone = cg.clientFrame;

	inter = cg.snap->ps.pm_type == PM_INTERMISSION;
	if ( inter && !kc.wasInter ) {
		kc.wasInter = qtrue;
		// the frag that ended it
		if ( kc_lastKill.time && cg.time - kc_lastKill.time < 2500 && cg.time >= kc_lastKill.time ) {
			for ( i = 0 ; i < KC_MAX_PENDING ; i++ ) {
				if ( kc_pending[i].time == kc_lastKill.time ) {
					kc_pending[i].flags |= KCF_FINAL;
					kc_pending[i].score = KC_Score( kc_pending[i].flags, kc_pending[i].quick );
				}
			}
			if ( kc_bestValid && kc_best.time == kc_lastKill.time && !( kc_best.flags & KCF_FINAL ) ) {
				kc_best.flags |= KCF_FINAL;
				kc_best.score = KC_Score( kc_best.flags, kc_best.quick );
			}
		}
	}

	// replay candidates: copy the best once its aftermath is recorded (at once at the intermission)
	for ( i = 0 ; i < KC_MAX_PENDING ; i++ ) {
		if ( !kc_pending[i].time ) {
			continue;
		}
		if ( cg.time < kc_pending[i].time ) {
			kc_pending[i].time = 0;		// map restart
			continue;
		}
		if ( inter || cg.time - kc_pending[i].time >= KC_REPLAY_POST + 60 ) {
			if ( !kc_bestValid || kc_pending[i].score > kc_best.score ) {
				KC_CopyClip( &kc_pending[i] );
			}
			kc_pending[i].time = 0;
		}
	}

	if ( inter && kc_cvReplays.integer && kc_bestValid && !kc.gateDone ) {
		if ( kc_show.kind != KCS_NONE && kc_show.store == &kc_ring ) {
			KC_EndShow();		// the replay takes over (the final frag is usually the best kill anyway)
		}
		if ( KC_Wide() && !kc.replayWanted ) {
			kc.replayWanted = qtrue;
			kc.gateDone = qtrue;
		}
	}

	// shows
	real = trap_Milliseconds();
	dt = real - kc_show.realLast;
	if ( dt < 0 ) {
		dt = 0;
	} else if ( dt > 100 ) {
		dt = 100;
	}
	if ( ( kc_show.kind == KCS_REPLAY && !kc_cvReplays.integer ) || ( kc_show.kind == KCS_KILL && !kc_cvKillCam.integer )
		|| ( kc_show.kind == KCS_DEATH && !kc_cvDeathCam.integer ) ) {
		KC_EndShow();		// switched off from the menu
	}
	if ( kc_show.kind != KCS_NONE ) {
		if ( kc_show.store == &kc_ring && !KC_Recording() ) {
			KC_EndShow();
		} else {
			KC_Advance();
		}
	}
	if ( kc_show.kind != KCS_NONE ) {
		KC_UpdateCamera( dt );
	}
	if ( kc_show.kind == KCS_NONE && kc_cvDebug.integer >= 3 && kc_bestValid && inter ) {
		KC_StartReplay( kc.gateDone && !KC_Wide() );		// benchmark: loop the replay
		KC_UpdateCamera( 0 );
	}
	KC_Bench( kc_show.kind != KCS_NONE );
	if ( kc_show.kind == KCS_NONE ) {
		if ( kc.replayWanted ) {
			kc.replayWanted = qfalse;
			KC_StartReplay( qfalse );
			KC_UpdateCamera( 0 );
		} else if ( kc_queued.time && !inter ) {
			if ( cg.time - kc_queued.time < KC_QUEUE_AGE && cg.time >= kc_queued.time
				&& cg.predictedPlayerState.stats[STAT_HEALTH] > 0 ) {
				KC_StartKill( &kc_queued );
				KC_UpdateCamera( 0 );
			}
			kc_queued.time = 0;
		}
	}
}

/*
===============================================================================

DRAWING

===============================================================================
*/

static void KC_Fill( float x, float y, float w, float h, const float *color ) {
	trap_R_SetColor( color );
	trap_R_DrawStretchPic( x, y, w, h, 0, 0, 0, 0, cgs.media.whiteShader );
}

static float KC_StrWidth( const char *s, float ch ) {
	return strlen( s ) * ch * 0.72f;
}

static void KC_Str( float x, float y, float ch, const char *s, const float *color, int align, qboolean shadow ) {
	float		cw = ch * 0.72f, fr, fc, sz = 0.0625f, w;
	vec4_t		black;
	const char	*p;
	int			c;

	w = KC_StrWidth( s, ch );
	if ( align == 0 ) {
		x -= w * 0.5f;
	} else if ( align > 0 ) {
		x -= w;
	}
	if ( shadow ) {
		black[0] = black[1] = black[2] = 0;
		black[3] = color[3] * 0.85f;
		trap_R_SetColor( black );
		for ( p = s, c = 0 ; *p ; p++, c++ ) {
			if ( *p == ' ' ) {
				continue;
			}
			fr = ( ( *p & 255 ) >> 4 ) * sz;
			fc = ( *p & 15 ) * sz;
			trap_R_DrawStretchPic( x + c * cw + ch * 0.07f, y + ch * 0.07f, cw, ch, fc, fr, fc + sz, fr + sz,
				cgs.media.charsetShader );
		}
	}
	trap_R_SetColor( color );
	for ( p = s, c = 0 ; *p ; p++, c++ ) {
		if ( *p == ' ' ) {
			continue;
		}
		fr = ( ( *p & 255 ) >> 4 ) * sz;
		fc = ( *p & 15 ) * sz;
		trap_R_DrawStretchPic( x + c * cw, y, cw, ch, fc, fr, fc + sz, fr + sz, cgs.media.charsetShader );
	}
}

static void KC_Accent( int kind, int flags, vec4_t out ) {
	out[3] = 1;
	if ( kind == KCS_DEATH ) {
		out[0] = 1.0f; out[1] = 0.18f; out[2] = 0.12f;
	} else if ( kind == KCS_REPLAY ) {
		out[0] = 1.0f; out[1] = 0.78f; out[2] = 0.25f;
	} else if ( flags & ( KCF_BOSS | KCF_FINAL | KCF_NEMESIS ) ) {
		out[0] = 1.0f; out[1] = 0.3f; out[2] = 0.9f;
	} else if ( flags & ( KCF_HEADSHOT ) ) {
		out[0] = 1.0f; out[1] = 0.85f; out[2] = 0.2f;
	} else if ( flags & ( KCF_GIB | KCF_OVERKILL ) ) {
		out[0] = 1.0f; out[1] = 0.22f; out[2] = 0.15f;
	} else {
		out[0] = 1.0f; out[1] = 0.55f; out[2] = 0.15f;
	}
}

/*
=================
KC_PanelRect

The panel in pixels: in the side gutter past 16:9 on wide screens, a corner box on 16:9,
centred and large for the full size replay
=================
*/
static void KC_PanelRect( float slide, float *x, float *y, float *w, float *h ) {
	float	vw = cgs.glconfig.vidWidth, vh = cgs.glconfig.vidHeight;
	float	gutter = ( vw - vh * 16.0f / 9.0f ) * 0.5f, m;

	if ( kc_show.big ) {
		*w = vw * 0.62f;
		if ( *w > vh * 1.4f ) {
			*w = vh * 1.4f;
		}
		*h = *w * 0.62f;
		*x = ( vw - *w ) * 0.5f;
		*y = vh * 0.15f;
		return;
	}
	if ( KC_Wide() ) {
		m = vh * 0.03f;
		*w = gutter - 2 * m;
		if ( *w > vh * 0.56f ) {
			*w = vh * 0.56f;
		}
		*h = *w * 0.62f;
		*x = kc_show.side < 0 ? ( gutter - *w ) * 0.5f : vw - gutter + ( gutter - *w ) * 0.5f;
		*y = vh * 0.42f - *h * 0.5f;
	} else {
		m = vh * 0.015f;
		*w = vw * 0.23f;
		*h = *w * 0.62f;
		*x = kc_show.side < 0 ? m : vw - *w - m;
		*y = vh * 0.30f;		// under the DESCENT floor / lives line
	}
	// slide in / out through the screen edge
	if ( kc_show.side < 0 ) {
		*x -= ( *x + *w + 4 ) * slide;
	} else {
		*x += ( vw - *x + 4 ) * slide;
	}
}

static void KC_RenderView( int vx, int vy, int vw, int vh ) {
	refdef_t	rd;
	vec3_t		fwd, ang;
	float		fov;
	int			n;

	memset( &rd, 0, sizeof( rd ) );
	rd.x = vx;
	rd.y = vy;
	rd.width = vw;
	rd.height = vh;
	fov = kc_show.kind == KCS_DEATH ? 62 : 70 - 8 * CG_Clamp01( ( kc_show.t - kc_show.tStart ) / (float)( kc_show.tEnd - kc_show.tStart + 1 ) );
	rd.fov_x = fov;
	rd.fov_y = RAD2DEG( 2 * atan2( vh * tan( DEG2RAD( fov * 0.5f ) ), (float)vw ) );
	VectorCopy( kc_show.camPos, rd.vieworg );
	VectorSubtract( kc_show.lookPos, kc_show.camPos, fwd );
	if ( VectorNormalize( fwd ) < 1 ) {
		fwd[0] = 1;
	}
	vectoangles( fwd, ang );
	ang[ROLL] = kc_show.roll * sin( ( kc_show.t - kc_show.tStart ) * 0.002f );
	AnglesToAxis( ang, rd.viewaxis );
	rd.time = (int)kc_show.t;
	rd.rdflags = RDF_PIP;		// opengl2: no shadow maps / SSAO / exposure feedback for this view
	// all areas open: the camera is somewhere else than the player
	memset( rd.areamask, 0, sizeof( rd.areamask ) );

	n = KC_EmitScene( kc_show.store, kc_show.t, kc_show.camPos );
	if ( kc_cvDebug.integer == 4 ) {
		trap_R_ClearScene();		// benchmark: the recording side only, no second view
	} else if ( kc_cvDebug.integer == 5 ) {
		trap_R_ClearScene();		// benchmark: the second view without the entities
		trap_R_RenderScene( &rd );
	} else {
		trap_R_RenderScene( &rd );
	}
	if ( kc_cvDebug.integer >= 2 && cg.clientFrame % 30 == 0 ) {
		CG_Printf( "KillCam: t %i (key %i) main ents %i, pip ents %i, polys %i, key %i %i %i look %i %i %i cam %i %i %i\n",
			(int)kc_show.t, kc_show.k.time, kc.mainEnts, n, kc_numPolys,
			(int)kc_show.keyPos[0], (int)kc_show.keyPos[1], (int)kc_show.keyPos[2],
			(int)kc_show.lookPos[0], (int)kc_show.lookPos[1], (int)kc_show.lookPos[2],
			(int)kc_show.camPos[0], (int)kc_show.camPos[1], (int)kc_show.camPos[2] );
	}
}

static void KC_DrawShow( void ) {
	float		x, y, w, h, bar, slide, a, ch, iw, tx, flash, real;
	int			vx, vy, vw, vh;
	vec4_t		acc, white, dim, black, col;
	const char	*s;
	qhandle_t	icon;

	real = trap_Milliseconds();
	slide = 1.0f - CG_Clamp01( ( real - kc_show.realStart ) / (float)KC_SLIDE_MSEC );
	if ( kc_show.fadeOut ) {
		a = CG_Clamp01( ( real - kc_show.fadeOut ) / (float)KC_FADE_MSEC );
		if ( a > slide ) {
			slide = a;
		}
	}
	slide = slide * slide;
	if ( kc_show.big ) {
		slide = 0;
	}
	KC_PanelRect( slide, &x, &y, &w, &h );
	bar = (float)(int)( h * 0.12f );
	vx = (int)x;
	vy = (int)( y + bar );
	vw = (int)w;
	vh = (int)( h - 2 * bar );
	if ( vw < 16 || vh < 16 || vx + vw <= 0 || vx >= cgs.glconfig.vidWidth ) {
		return;
	}

	KC_RenderView( vx, vy, vw, vh );

	// film look: grain, vignette, the kill flash
	if ( real - kc.grainTime > 45 ) {
		kc.grainTime = real;
		kc.grainS = random();
		kc.grainT = random();
	}
	trap_R_SetColor( NULL );
	trap_R_DrawStretchPic( vx, vy, vw, vh, kc.grainS, kc.grainT, kc.grainS + vw / 512.0f, kc.grainT + vh / 512.0f,
		kc_grainShader );
	white[0] = white[1] = white[2] = 1;
	white[3] = 0.6f;
	trap_R_SetColor( white );
	trap_R_DrawStretchPic( vx, vy, vw, vh, 0, 0, 1, 1, kc_vignetteShader );
	if ( kc_show.kind != KCS_DEATH ) {
		flash = 1.0f - fabs( kc_show.t - kc_show.k.time ) / 70.0f;
		if ( flash > 0 ) {
			white[3] = flash * 0.3f;
			KC_Fill( vx, vy, vw, vh, white );
		}
	}

	// letterbox bars and frame
	KC_Accent( kc_show.kind, kc_show.k.flags, acc );
	black[0] = 0.02f; black[1] = 0.015f; black[2] = 0.02f; black[3] = 0.94f;
	KC_Fill( x, y, w, bar, black );
	KC_Fill( x, y + h - bar, w, bar, black );
	col[0] = acc[0]; col[1] = acc[1]; col[2] = acc[2]; col[3] = 0.85f;
	KC_Fill( x, y, w, 2, col );
	KC_Fill( x, y + h - 2, w, 2, col );
	col[3] = 0.35f;
	KC_Fill( x, y, 1, h, col );
	KC_Fill( x + w - 1, y, 1, h, col );

	// top bar: the tag and who; the speed
	ch = bar * 0.42f;
	dim[0] = dim[1] = dim[2] = 0.78f;
	dim[3] = 0.95f;
	if ( ( (int)real / 500 ) & 1 ) {
		col[0] = 1; col[1] = 0.1f; col[2] = 0.1f; col[3] = 1;
		KC_Fill( x + bar * 0.3f, y + bar * 0.5f - ch * 0.25f, ch * 0.5f, ch * 0.5f, col );
	}
	tx = x + bar * 0.3f + ch * 0.8f;
	KC_Str( tx, y + ( bar - ch ) * 0.5f, ch, kc_show.tag, dim, -1, qfalse );
	tx += KC_StrWidth( kc_show.tag, ch ) + ch;
	if ( kc_show.name[0] && kc_show.kind != KCS_DEATH && tx + KC_StrWidth( kc_show.name, ch ) < x + w - ch * 5 ) {
		KC_Str( tx, y + ( bar - ch ) * 0.5f, ch, kc_show.name, acc, -1, qfalse );
	}
	KC_Str( x + w - bar * 0.3f, y + ( bar - ch ) * 0.5f, ch, va( "%.2fx", kc_show.rate ), dim, 1, qfalse );

	// bottom bar: the label, the details, the weapon
	ch = bar * 0.6f;
	iw = bar * 0.78f;
	icon = 0;
	if ( kc_show.k.weapon > WP_NONE && kc_show.k.weapon < WP_NUM_WEAPONS ) {
		icon = cg_weapons[kc_show.k.weapon].weaponIcon;
	}
	if ( icon ) {
		trap_R_SetColor( NULL );
		trap_R_DrawStretchPic( x + w - iw - bar * 0.25f, y + h - bar + ( bar - iw ) * 0.5f, iw, iw, 0, 0, 1, 1, icon );
	}
	tx = x + bar * 0.3f;
	if ( kc_show.kind == KCS_DEATH ) {
		KC_Str( tx, y + h - bar + ( bar - ch * 0.6f ) * 0.5f, ch * 0.6f, kc_show.title, acc, -1, qtrue );
		tx += KC_StrWidth( kc_show.title, ch * 0.6f ) + ch * 0.5f;
		KC_Str( tx, y + h - bar + ( bar - ch ) * 0.5f, ch, kc_show.name, white, -1, qtrue );
		tx += KC_StrWidth( kc_show.name, ch ) + ch * 0.8f;
		if ( kc_show.health >= 0 ) {
			CG_GetColorForHealth( kc_show.health, kc_show.armor, col );
			col[3] = 1;
			s = va( "%i HP  %i AR", kc_show.health, kc_show.armor );
			if ( tx + KC_StrWidth( s, ch * 0.6f ) < x + w - iw - bar * 0.5f ) {
				KC_Str( tx, y + h - bar + ( bar - ch * 0.6f ) * 0.5f, ch * 0.6f, s, col, -1, qtrue );
			}
		}
	} else {
		KC_Str( tx, y + h - bar + ( bar - ch ) * 0.5f, ch, kc_show.title, acc, -1, qtrue );
		tx += KC_StrWidth( kc_show.title, ch ) + ch * 0.6f;
		if ( kc_show.sub[0] && tx + KC_StrWidth( kc_show.sub, ch * 0.5f ) < x + w - iw - bar * 0.5f ) {
			white[3] = 0.9f;
			KC_Str( tx, y + h - bar + ( bar - ch * 0.5f ) * 0.5f, ch * 0.5f, kc_show.sub, white, -1, qtrue );
		}
	}

	if ( kc_show.big ) {
		const char	*k1 = CG_KeyForCommand( "+moveup" );
		const char	*k2 = CG_KeyForCommand( "+attack" );

		ch = bar * 0.38f;
		s = va( "[%s] / [%s]  SKIP", k1 ? k1 : "JUMP", k2 ? k2 : "FIRE" );
		dim[3] = 0.6f + 0.3f * sin( real * 0.006f );
		KC_Str( x + w, y + h + ch * 0.6f, ch, s, dim, 1, qtrue );
		KC_Str( x, y + h + ch * 0.6f, ch, CG_RL_Active() ? "BEST KILL OF THE FLOOR"
			: "BEST KILL OF THE MATCH", dim, -1, qtrue );
	}
	trap_R_SetColor( NULL );
}

/*
=================
CG_KillCamDraw

After the 2D HUD: the side panel
=================
*/
void CG_KillCamDraw( void ) {
	float	oldShift;

	KC_Frame();
	if ( kc_show.kind == KCS_NONE || kc_show.big || !kc_show.camSet ) {
		return;
	}
	if ( cg_draw2D.integer == 0 || cg.levelShot ) {
		return;
	}
	if ( cg.showScores && !KC_Wide() ) {
		return;		// the scoreboard needs the room
	}
	oldShift = cg.hudShift;
	cg.hudShift = 0;
	KC_DrawShow();
	cg.hudShift = oldShift;
}

/*
=================
CG_KillCamReplayGate

From the intermission screens on 16:9 (no side gutter): the replay plays full size first.
qtrue while it does (the caller draws nothing else).
=================
*/
qboolean CG_KillCamReplayGate( void ) {
	KC_Frame();
	if ( KC_Wide() || !kc_cvReplays.integer ) {
		return qfalse;
	}
	if ( !kc.gateDone && kc_bestValid ) {
		kc.gateDone = qtrue;
		KC_StartReplay( qtrue );
		KC_UpdateCamera( 0 );
	}
	if ( kc_show.kind != KCS_REPLAY || !kc_show.big || !kc_show.camSet ) {
		return qfalse;
	}
	KC_DrawShow();
	return qtrue;
}

/*
=================
CG_KillCamInit
=================
*/
void CG_KillCamInit( void ) {
	memset( &kc, 0, sizeof( kc ) );
	memset( &kc_show, 0, sizeof( kc_show ) );
	memset( &kc_queued, 0, sizeof( kc_queued ) );
	memset( kc_pending, 0, sizeof( kc_pending ) );
	memset( &kc_lastKill, 0, sizeof( kc_lastKill ) );
	memset( &kc_killer, 0, sizeof( kc_killer ) );
	kc_bestValid = qfalse;
	kc.frameDone = -1;
	KC_ResetRecording();
	KC_StoreInit( &kc_clip, kc_clipSamples, KC_CLIP_SAMPLES, kc_clipEnts, KC_CLIP_ENTS, kc_clipLights, KC_CLIP_LIGHTS );

	trap_Cvar_Register( &kc_cvKillCam, "cg_killCam", "1", CVAR_ARCHIVE );
	trap_Cvar_Register( &kc_cvSide, "cg_killCamSide", "0", CVAR_ARCHIVE );
	trap_Cvar_Register( &kc_cvReplays, "cg_replays", "1", CVAR_ARCHIVE );
	trap_Cvar_Register( &kc_cvDeathCam, "cg_deathCam", "1", CVAR_ARCHIVE );
	trap_Cvar_Register( &kc_cvDebug, "cg_killCamDebug", "0", 0 );

	kc_grainShader = trap_R_RegisterShaderNoMip( "rearmed/kcGrain" );
	kc_vignetteShader = trap_R_RegisterShaderNoMip( "rearmed/kcVignette" );
}
