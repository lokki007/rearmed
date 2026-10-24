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
#include "../qcommon/q_shared.h"
#include "../renderercommon/tr_types.h"
#include "../game/bg_public.h"
#include "cg_public.h"


// The entire cgame module is unloaded and reloaded on each level change,
// so there is NO persistant data between levels on the client side.
// If you absolutely need something stored, it can either be kept
// by the server in the server stored userinfos, or stashed in a cvar.

#define	POWERUP_BLINKS		5

#define	POWERUP_BLINK_TIME	1000
#define	FADE_TIME			200
#define	PULSE_TIME			200
#define	DAMAGE_DEFLECT_TIME	100
#define	DAMAGE_RETURN_TIME	400
#define DAMAGE_TIME			500
#define	LAND_DEFLECT_TIME	150
#define	LAND_RETURN_TIME	300
#define	STEP_TIME			200
#define	DUCK_TIME			100
#define	PAIN_TWITCH_TIME	200
#define	WEAPON_SELECT_TIME	1400
#define	ITEM_SCALEUP_TIME	1000
#define	ZOOM_TIME			150
#define	ITEM_BLOB_TIME		200
#define	MUZZLE_FLASH_TIME	20
#define	SINK_TIME			1000		// time for fragments to sink into ground before going away
#define	ATTACKER_HEAD_TIME	10000
#define	REWARD_TIME			3000

#define	PULSE_SCALE			1.5			// amount to scale up the icons when activating

#define	MAX_STEP_CHANGE		32

#define	MAX_VERTS_ON_POLY	10
#define	MAX_MARK_POLYS		1024	// rearmed: 256 stock; cg_gorePersist keeps bullet holes (cap from cg_gorePersistMax)

#define STAT_MINUS			10	// num frame for '-' stats digit

#define	ICON_SIZE			48
#undef	CHAR_WIDTH				// ignore define from libc
#define	CHAR_WIDTH			32
#define	CHAR_HEIGHT			48
#define	TEXT_ICON_SPACE		4

#define	TEAMCHAT_WIDTH		80
#define TEAMCHAT_HEIGHT		8

// very large characters
#define	GIANT_WIDTH			32
#define	GIANT_HEIGHT		48

#define	NUM_CROSSHAIRS		10

#define TEAM_OVERLAY_MAXNAME_WIDTH	12
#define TEAM_OVERLAY_MAXLOCATION_WIDTH	16

#define	DEFAULT_MODEL			"sarge"
#ifdef MISSIONPACK
#define	DEFAULT_TEAM_MODEL		"james"
#define	DEFAULT_TEAM_HEAD		"*james"
#else
#define	DEFAULT_TEAM_MODEL		"sarge"
#define	DEFAULT_TEAM_HEAD		"sarge"
#endif

#define DEFAULT_REDTEAM_NAME		"Stroggs"
#define DEFAULT_BLUETEAM_NAME		"Pagans"

typedef enum {
	FOOTSTEP_NORMAL,
	FOOTSTEP_BOOT,
	FOOTSTEP_FLESH,
	FOOTSTEP_MECH,
	FOOTSTEP_ENERGY,
	FOOTSTEP_METAL,
	FOOTSTEP_SPLASH,

	FOOTSTEP_TOTAL
} footstep_t;

typedef enum {
	IMPACTSOUND_DEFAULT,
	IMPACTSOUND_METAL,
	IMPACTSOUND_FLESH
} impactSound_t;

//=================================================

// player entities need to track more information
// than any other type of entity.

// note that not every player entity is a client entity,
// because corpses after respawn are outside the normal
// client numbering range

// when changing animation, set animationTime to frameTime + lerping time
// The current lerp will finish out, then it will lerp to the new animation
typedef struct {
	int			oldFrame;
	int			oldFrameTime;		// time when ->oldFrame was exactly on

	int			frame;
	int			frameTime;			// time when ->frame will be exactly on

	float		backlerp;

	float		yawAngle;
	qboolean	yawing;
	float		pitchAngle;
	qboolean	pitching;

	int			animationNumber;	// may include ANIM_TOGGLEBIT
	animation_t	*animation;
	int			animationTime;		// time when the first frame of the animation will be exact
} lerpFrame_t;


typedef struct {
	lerpFrame_t		legs, torso, flag;
	int				painTime;
	int				painDirection;	// flip from 0 to 1
	int				lightningFiring;

	int				railFireTime;

	// machinegun spinning
	float			barrelAngle;
	int				barrelTime;
	qboolean		barrelSpinning;
} playerEntity_t;

//=================================================



// centity_t have a direct corespondence with gentity_t in the game, but
// only the entityState_t is directly communicated to the cgame
typedef struct centity_s {
	entityState_t	currentState;	// from cg.frame
	entityState_t	nextState;		// from cg.nextFrame, if available
	qboolean		interpolate;	// true if next is valid to interpolate to
	qboolean		currentValid;	// true if cg.frame holds this entity

	int				muzzleFlashTime;	// move to playerEntity?
	int				rmFxTime;			// next element particle (burning / wet / shocked, fire patches)
	int				dsFxTime;			// descent arsenal: next evolved-missile trail particle / fire layer sound
	int				previousEvent;
	int				teleportFlag;

	int				trailTime;		// so missile trails can handle dropped initial packets
	int				dustTrailTime;
	int				miscTime;

	int				snapShotTime;	// last time this entity was found in a snapshot

	playerEntity_t	pe;

	int				errorTime;		// decay the error from this time
	vec3_t			errorOrigin;
	vec3_t			errorAngles;
	
	qboolean		extrapolated;	// false if origin / angles is an interpolation
	vec3_t			rawOrigin;
	vec3_t			rawAngles;

	vec3_t			beamEnd;

	// exact interpolated position of entity on this frame
	vec3_t			lerpOrigin;
	vec3_t			lerpAngles;
} centity_t;


//======================================================================

// local entities are created as a result of events or predicted actions,
// and live independently from all server transmitted entities

typedef struct markPoly_s {
	struct markPoly_s	*prevMark, *nextMark;
	int			time;
	qhandle_t	markShader;
	qboolean	alphaFade;		// fade alpha instead of rgb
	float		color[4];
	poly_t		poly;
	polyVert_t	verts[MAX_VERTS_ON_POLY];
} markPoly_t;


typedef enum {
	LE_MARK,
	LE_EXPLOSION,
	LE_SPRITE_EXPLOSION,
	LE_FRAGMENT,
	LE_MOVE_SCALE_FADE,
	LE_FALL_SCALE_FADE,
	LE_FADE_RGB,
	LE_SCALE_FADE,
	LE_SCOREPLUM,
	LE_FX_SPARK,
	LE_FX_FLASH,
	LE_FX_RING,
	LE_DAMAGE_NUMBER,
#ifdef MISSIONPACK
	LE_KAMIKAZE,
	LE_INVULIMPACT,
	LE_INVULJUICED,
	LE_SHOWREFENTITY
#endif
} leType_t;

typedef enum {
	LEF_PUFF_DONT_SCALE  = 0x0001,			// do not scale size over time
	LEF_TUMBLE			 = 0x0002,			// tumble over time, used for ejecting shells
	LEF_SOUND1			 = 0x0004,			// sound 1 for kamikaze
	LEF_SOUND2			 = 0x0008,			// sound 2 for kamikaze
	LEF_FX_EMBER		 = 0x0010			// LE_FX_SPARK drawn as a round glow
} leFlag_t;

typedef enum {
	LEMT_NONE,
	LEMT_BURN,
	LEMT_BLOOD
} leMarkType_t;			// fragment local entities can leave marks on walls

typedef enum {
	LEBS_NONE,
	LEBS_BLOOD,
	LEBS_BRASS
} leBounceSoundType_t;	// fragment local entities can make sounds on impacts

typedef struct localEntity_s {
	struct localEntity_s	*prev, *next;
	leType_t		leType;
	int				leFlags;

	int				startTime;
	int				endTime;
	int				fadeInTime;

	float			lifeRate;			// 1.0 / (endTime - startTime)
	float			endRadius;			// LE_FX_FLASH / LE_FX_RING final size

	trajectory_t	pos;
	trajectory_t	angles;

	float			bounceFactor;		// 0.0 = no bounce, 1.0 = perfect

	float			color[4];

	float			radius;

	float			light;
	vec3_t			lightColor;

	leMarkType_t		leMarkType;		// mark to leave on fragment impact
	leBounceSoundType_t	leBounceSoundType;

	refEntity_t		refEntity;		
} localEntity_t;

//======================================================================


typedef struct {
	int				client;
	int				score;
	int				ping;
	int				time;
	int				scoreFlags;
	int				powerUps;
	int				accuracy;
	int				impressiveCount;
	int				excellentCount;
	int				guantletCount;
	int				defendCount;
	int				assistCount;
	int				captures;
	qboolean	perfect;
	int				team;
} score_t;

// each client has an associated clientInfo_t
// that contains media references necessary to present the
// client model and other color coded effects
// this is regenerated each time a client's configstring changes,
// usually as a result of a userinfo (name, model, etc) change
#define	MAX_CUSTOM_SOUNDS	32

typedef struct {
	qboolean		infoValid;

	char			name[MAX_QPATH];
	team_t			team;

	int				botSkill;		// 0 = not bot, 1-5 = bot

	vec3_t			color1;
	vec3_t			color2;
	
	byte c1RGBA[4];
	byte c2RGBA[4];

	int				score;			// updated by score servercmds
	int				location;		// location index for team mode
	int				health;			// you only get this info about your teammates
	int				armor;
	int				curWeapon;

	int				handicap;
	int				wins, losses;	// in tourney mode

	int				teamTask;		// task in teamplay (offence/defence)
	qboolean		teamLeader;		// true when this is a team leader

	int				powerups;		// so can display quad/flag status

	int				medkitUsageTime;
	int				invulnerabilityStartTime;
	int				invulnerabilityStopTime;

	int				breathPuffTime;

	// when clientinfo is changed, the loading of models/skins/sounds
	// can be deferred until you are dead, to prevent hitches in
	// gameplay
	char			modelName[MAX_QPATH];
	char			skinName[MAX_QPATH];
	char			headModelName[MAX_QPATH];
	char			headSkinName[MAX_QPATH];
	char			redTeam[MAX_TEAMNAME];
	char			blueTeam[MAX_TEAMNAME];
	qboolean		deferred;

	qboolean		newAnims;		// true if using the new mission pack animations
	qboolean		fixedlegs;		// true if legs yaw is always the same as torso yaw
	qboolean		fixedtorso;		// true if torso never changes yaw

	vec3_t			headOffset;		// move head in icon views
	footstep_t		footsteps;
	gender_t		gender;			// from model

	qhandle_t		legsModel;
	qhandle_t		legsSkin;

	qhandle_t		torsoModel;
	qhandle_t		torsoSkin;

	qhandle_t		headModel;
	qhandle_t		headSkin;

	qhandle_t		modelIcon;

	animation_t		animations[MAX_TOTALANIMATIONS];

	sfxHandle_t		sounds[MAX_CUSTOM_SOUNDS];
} clientInfo_t;


// each WP_* weapon enum has an associated weaponInfo_t
// that contains media references necessary to present the
// weapon and its effects
typedef struct weaponInfo_s {
	qboolean		registered;
	gitem_t			*item;

	qhandle_t		handsModel;			// the hands don't actually draw, they just position the weapon
	qhandle_t		weaponModel;
	qhandle_t		barrelModel;
	qhandle_t		flashModel;

	vec3_t			weaponMidpoint;		// so it will rotate centered instead of by tag

	float			flashDlight;
	vec3_t			flashDlightColor;
	sfxHandle_t		flashSound[4];		// fast firing weapons randomly choose

	qhandle_t		weaponIcon;
	qhandle_t		ammoIcon;

	qhandle_t		ammoModel;

	qhandle_t		missileModel;
	sfxHandle_t		missileSound;
	void			(*missileTrailFunc)( centity_t *, const struct weaponInfo_s *wi );
	float			missileDlight;
	vec3_t			missileDlightColor;
	int				missileRenderfx;

	void			(*ejectBrassFunc)( centity_t * );

	float			trailRadius;
	float			wiTrailTime;

	sfxHandle_t		readySound;
	sfxHandle_t		firingSound;
} weaponInfo_t;


// each IT_* item has an associated itemInfo_t
// that constains media references necessary to present the
// item and its effects
typedef struct {
	qboolean		registered;
	qhandle_t		models[MAX_ITEM_MODELS];
	qhandle_t		icon;
} itemInfo_t;


typedef struct {
	int				itemNum;
} powerupInfo_t;


#define MAX_SKULLTRAIL		10

typedef struct {
	vec3_t positions[MAX_SKULLTRAIL];
	int numpositions;
} skulltrail_t;


#define MAX_REWARDSTACK		10
#define MAX_SOUNDBUFFER		20

//======================================================================

// all cg.stepTime, cg.duckTime, cg.landTime, etc are set to cg.time when the action
// occurs, and they will have visible effects for #define STEP_TIME or whatever msec after

#define MAX_PREDICTED_EVENTS	16
 
typedef struct {
	int			clientFrame;		// incremented each frame

	int			clientNum;
	
	qboolean	demoPlayback;
	qboolean	levelShot;			// taking a level menu screenshot
	int			deferredPlayerLoading;
	qboolean	loading;			// don't defer players at initial startup
	qboolean	intermissionStarted;	// don't play voice rewards, because game will end shortly

	// there are only one or two snapshot_t that are relevant at a time
	int			latestSnapshotNum;	// the number of snapshots the client system has received
	int			latestSnapshotTime;	// the time from latestSnapshotNum, so we don't need to read the snapshot yet

	snapshot_t	*snap;				// cg.snap->serverTime <= cg.time
	snapshot_t	*nextSnap;			// cg.nextSnap->serverTime > cg.time, or NULL
	snapshot_t	activeSnapshots[2];

	float		frameInterpolation;	// (float)( cg.time - cg.frame->serverTime ) / (cg.nextFrame->serverTime - cg.frame->serverTime)

	qboolean	thisFrameTeleport;
	qboolean	nextFrameTeleport;

	int			frametime;		// cg.time - cg.oldTime

	int			time;			// this is the time value that the client
								// is rendering at.
	int			oldTime;		// time at last frame, used for missile trails and prediction checking

	int			physicsTime;	// either cg.snap->time or cg.nextSnap->time

	int			timelimitWarnings;	// 5 min, 1 min, overtime
	int			fraglimitWarnings;

	qboolean	mapRestart;			// set on a map restart to set back the weapon

	qboolean	renderingThirdPerson;		// during deaths, chasecams, etc

	// prediction state
	qboolean	hyperspace;				// true if prediction has hit a trigger_teleport
	playerState_t	predictedPlayerState;
	centity_t		predictedPlayerEntity;
	qboolean	validPPS;				// clear until the first call to CG_PredictPlayerState
	int			predictedErrorTime;
	vec3_t		predictedError;

	int			eventSequence;
	int			predictableEvents[MAX_PREDICTED_EVENTS];

	float		stepChange;				// for stair up smoothing
	int			stepTime;

	float		duckChange;				// for duck viewheight smoothing
	int			duckTime;

	float		landChange;				// for landing hard
	int			landTime;

	// input state sent to server
	int			weaponSelect;

	// auto rotating items
	vec3_t		autoAngles;
	vec3_t		autoAxis[3];
	vec3_t		autoAnglesFast;
	vec3_t		autoAxisFast[3];

	// view rendering
	refdef_t	refdef;
	vec3_t		refdefViewAngles;		// will be converted to refdef.viewaxis

	// zoom key
	qboolean	zoomed;
	int			zoomTime;
	float		zoomSensitivity;

	// information screen text during loading
	char		infoScreenText[MAX_STRING_CHARS];

	// scoreboard
	int			scoresRequestTime;
	int			numScores;
	int			selectedScore;
	int			teamScores[2];
	score_t		scores[MAX_CLIENTS];
	qboolean	showScores;
	qboolean	scoreBoardShowing;
	int			scoreFadeTime;
	char		killerName[MAX_NAME_LENGTH];
	char			spectatorList[MAX_STRING_CHARS];		// list of names
	int				spectatorLen;												// length of list
	float			spectatorWidth;											// width in device units
	int				spectatorTime;											// next time to offset
	int				spectatorPaintX;										// current paint x
	int				spectatorPaintX2;										// current paint x
	int				spectatorOffset;										// current offset from start
	int				spectatorPaintLen; 									// current offset from start

#ifdef MISSIONPACK
	// skull trails
	skulltrail_t	skulltrails[MAX_CLIENTS];
#endif

	// centerprinting
	int			centerPrintTime;
	int			centerPrintCharWidth;
	int			centerPrintY;
	char		centerPrint[1024];
	int			centerPrintLines;

	// low ammo warning state
	int			lowAmmoWarning;		// 1 = low, 2 = empty

	// crosshair client ID
	int			crosshairClientNum;
	int			crosshairClientTime;

	// powerup active flashing
	int			powerupActive;
	int			powerupTime;

	// attacking player
	int			attackerTime;
	int			voiceTime;

	// reward medals
	int			rewardStack;
	int			rewardTime;
	int			headshotTime;		// last time we landed a headshot
	int			shakeTime;			// screen shake start
	float		shakeAmplitude;		// screen shake strength in degrees
	qboolean	impactAlt;			// the impact being drawn is from an alt-fire projectile
	int			hitMarkerTime;		// hit confirm feedback
	int			hitMarkerDamage;
	qboolean	hitMarkerKill;
	int			airshotTime;
	char		rmBanner[128];		// rearmed announcement
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
	int			rmPerkOffer[3];		// perks on offer ([0] = -1 for none)
	int			rmPerks;			// PERK_ bits we hold
	qboolean	rmRecordChecked;	// the end-of-match record check ran
	int			rmDashFullTime;		// when the dash charges were last full
	int			kickTime;			// weapon kick of the last shot
	float		kickPitch;
	float		kickYaw;
	int			rewardCount[MAX_REWARDSTACK];
	qhandle_t	rewardShader[MAX_REWARDSTACK];
	qhandle_t	rewardSound[MAX_REWARDSTACK];

	// sound buffer mainly for announcer sounds
	int			soundBufferIn;
	int			soundBufferOut;
	int			soundTime;
	qhandle_t	soundBuffer[MAX_SOUNDBUFFER];

#ifdef MISSIONPACK
	// for voice chat buffer
	int			voiceChatTime;
	int			voiceChatBufferIn;
	int			voiceChatBufferOut;
#endif

	// warmup countdown
	int			warmup;
	int			warmupCount;

	//==========================

	int			itemPickup;
	int			itemPickupTime;
	int			itemPickupBlendTime;	// the pulse around the crosshair is timed separately

	int			weaponSelectTime;
	int			weaponAnimation;
	int			weaponAnimationTime;

	// blend blobs
	float		damageTime;
	float		damageX, damageY, damageValue;

	// status bar head
	float		headYaw;
	float		headEndPitch;
	float		headEndYaw;
	int			headEndTime;
	float		headStartPitch;
	float		headStartYaw;
	int			headStartTime;

	// view movement
	float		v_dmg_time;
	float		v_dmg_pitch;
	float		v_dmg_roll;

	// temp working variables for player view
	float		bobfracsin;
	int			bobcycle;
	float		xyspeed;
	int     nextOrbitTime;

	//qboolean cameraMode;		// if rendering from a loaded camera

	// weapon wheel (cg_wheel.c)
	qboolean	wheelOpen;
	int			wheelOpenTime;		// cg.time when opened
	int			wheelOpenMsec;		// key event time from "+wheel <key> <time>"
	float		wheelX, wheelY;		// pointer, clamped to the wheel radius
	int			wheelCurWeapon;		// weapon held last frame
	int			wheelLastWeapon;	// the one held before it (tap swaps back)
	qboolean	wheelInit;			// stale capture from a previous cgame cleared

	// development tool
	refEntity_t		testModelEntity;
	char			testModelName[MAX_QPATH];
	qboolean		testGun;

} cg_t;


// all of the model, shader, and sound references that are
// loaded at gamestate time are stored in cgMedia_t
// Other media that can be tied to clients, weapons, or items are
// stored in the clientInfo_t, itemInfo_t, weaponInfo_t, and powerupInfo_t
typedef struct {
	qhandle_t	charsetShader;
	qhandle_t	charsetProp;
	qhandle_t	charsetPropGlow;
	qhandle_t	charsetPropB;
	qhandle_t	whiteShader;

#ifdef MISSIONPACK
	qhandle_t	redCubeModel;
	qhandle_t	blueCubeModel;
	qhandle_t	redCubeIcon;
	qhandle_t	blueCubeIcon;
#endif
	qhandle_t	redFlagModel;
	qhandle_t	blueFlagModel;
	qhandle_t	neutralFlagModel;
	qhandle_t	redFlagShader[3];
	qhandle_t	blueFlagShader[3];
	qhandle_t	flagShader[4];

	qhandle_t	flagPoleModel;
	qhandle_t	flagFlapModel;

	qhandle_t	redFlagFlapSkin;
	qhandle_t	blueFlagFlapSkin;
	qhandle_t	neutralFlagFlapSkin;

	qhandle_t	redFlagBaseModel;
	qhandle_t	blueFlagBaseModel;
	qhandle_t	neutralFlagBaseModel;

#ifdef MISSIONPACK
	qhandle_t	overloadBaseModel;
	qhandle_t	overloadTargetModel;
	qhandle_t	overloadLightsModel;
	qhandle_t	overloadEnergyModel;

	qhandle_t	harvesterModel;
	qhandle_t	harvesterRedSkin;
	qhandle_t	harvesterBlueSkin;
	qhandle_t	harvesterNeutralModel;
#endif

	qhandle_t	armorModel;
	qhandle_t	armorIcon;

	qhandle_t	teamStatusBar;

	qhandle_t	deferShader;

	// gib explosions
	qhandle_t	gibAbdomen;
	qhandle_t	gibArm;
	qhandle_t	gibChest;
	qhandle_t	gibFist;
	qhandle_t	gibFoot;
	qhandle_t	gibForearm;
	qhandle_t	gibIntestine;
	qhandle_t	gibLeg;
	qhandle_t	gibSkull;
	qhandle_t	gibBrain;

	qhandle_t	smoke2;

	qhandle_t	machinegunBrassModel;
	qhandle_t	shotgunBrassModel;

	qhandle_t	railRingsShader;
	qhandle_t	railCoreShader;

	qhandle_t	lightningShader;

	qhandle_t	friendShader;

	qhandle_t	balloonShader;
	qhandle_t	connectionShader;

	qhandle_t	selectShader;
	qhandle_t	viewBloodShader;
	qhandle_t	tracerShader;
	qhandle_t	crosshairShader[NUM_CROSSHAIRS];
	qhandle_t	lagometerShader;
	qhandle_t	backTileShader;
	qhandle_t	noammoShader;

	qhandle_t	smokePuffShader;
	qhandle_t	smokePuffRageProShader;
	qhandle_t	shotgunSmokePuffShader;
	qhandle_t	plasmaBallShader;
	qhandle_t	waterBubbleShader;
	qhandle_t	bloodTrailShader;
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	qhandle_t	nailPuffShader;
	qhandle_t	blueProxMine;
#endif

	qhandle_t	numberShaders[11];

	qhandle_t	shadowMarkShader;

	qhandle_t	botSkillShaders[5];

	// wall mark shaders
	qhandle_t	wakeMarkShader;
	qhandle_t	bloodMarkShader;
	qhandle_t	bulletMarkShader;
	qhandle_t	burnMarkShader;
	qhandle_t	holeMarkShader;
	qhandle_t	energyMarkShader;

	// powerup shaders
	qhandle_t	quadShader;
	qhandle_t	redQuadShader;
	qhandle_t	quadWeaponShader;
	qhandle_t	invisShader;
	qhandle_t	regenShader;
	qhandle_t	battleSuitShader;
	qhandle_t	battleWeaponShader;
	qhandle_t	hastePuffShader;
#ifdef MISSIONPACK
	qhandle_t	redKamikazeShader;
	qhandle_t	blueKamikazeShader;
#endif

	// weapon effect models
	qhandle_t	bulletFlashModel;
	qhandle_t	ringFlashModel;
	qhandle_t	dishFlashModel;
	qhandle_t	lightningExplosionModel;

	// weapon effect shaders
	qhandle_t	railExplosionShader;
	qhandle_t	plasmaExplosionShader;
	qhandle_t	bulletExplosionShader;
	qhandle_t	rocketExplosionShader;
	qhandle_t	grenadeExplosionShader;
	qhandle_t	bfgExplosionShader;
	qhandle_t	bloodExplosionShader;

	// special effects models
	qhandle_t	teleportEffectModel;
	qhandle_t	teleportEffectShader;
#ifdef MISSIONPACK
	qhandle_t	kamikazeEffectModel;
	qhandle_t	kamikazeShockWave;
	qhandle_t	kamikazeHeadModel;
	qhandle_t	kamikazeHeadTrail;
	qhandle_t	guardPowerupModel;
	qhandle_t	scoutPowerupModel;
	qhandle_t	doublerPowerupModel;
	qhandle_t	ammoRegenPowerupModel;
	qhandle_t	invulnerabilityImpactModel;
	qhandle_t	invulnerabilityJuicedModel;
	qhandle_t	medkitUsageModel;
	qhandle_t	dustPuffShader;
	qhandle_t	heartShader;
	qhandle_t	invulnerabilityPowerupModel;
#endif

	// scoreboard headers
	qhandle_t	scoreboardName;
	qhandle_t	scoreboardPing;
	qhandle_t	scoreboardScore;
	qhandle_t	scoreboardTime;

	// medals shown during gameplay
	qhandle_t	medalImpressive;
	qhandle_t	medalExcellent;
	qhandle_t	medalGauntlet;
	qhandle_t	medalDefend;
	qhandle_t	medalAssist;
	qhandle_t	medalCapture;

	// sounds
	sfxHandle_t	quadSound;
	sfxHandle_t	tracerSound;
	sfxHandle_t	selectSound;
	sfxHandle_t	useNothingSound;
	sfxHandle_t	wearOffSound;
	sfxHandle_t	footsteps[FOOTSTEP_TOTAL][4];
	sfxHandle_t	sfx_lghit1;
	sfxHandle_t	sfx_lghit2;
	sfxHandle_t	sfx_lghit3;
	sfxHandle_t	sfx_ric1;
	sfxHandle_t	sfx_ric2;
	sfxHandle_t	sfx_ric3;
	//sfxHandle_t	sfx_railg;
	sfxHandle_t	sfx_rockexp;
	sfxHandle_t	sfx_plasmaexp;
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
	sfxHandle_t	sfx_proxexp;
	sfxHandle_t	sfx_nghit;
	sfxHandle_t	sfx_nghitflesh;
	sfxHandle_t	sfx_nghitmetal;
	sfxHandle_t	sfx_chghit;
	sfxHandle_t	sfx_chghitflesh;
	sfxHandle_t	sfx_chghitmetal;
#endif
#ifdef MISSIONPACK
	sfxHandle_t kamikazeExplodeSound;
	sfxHandle_t kamikazeImplodeSound;
	sfxHandle_t kamikazeFarSound;
	sfxHandle_t useInvulnerabilitySound;
	sfxHandle_t invulnerabilityImpactSound1;
	sfxHandle_t invulnerabilityImpactSound2;
	sfxHandle_t invulnerabilityImpactSound3;
	sfxHandle_t invulnerabilityJuicedSound;
	sfxHandle_t obeliskHitSound1;
	sfxHandle_t obeliskHitSound2;
	sfxHandle_t obeliskHitSound3;
	sfxHandle_t	obeliskRespawnSound;
	sfxHandle_t	winnerSound;
	sfxHandle_t	loserSound;
#endif
	sfxHandle_t	gibSound;
	sfxHandle_t	gibBounce1Sound;
	sfxHandle_t	gibBounce2Sound;
	sfxHandle_t	gibBounce3Sound;
	sfxHandle_t	teleInSound;
	sfxHandle_t	teleOutSound;
	sfxHandle_t	noAmmoSound;
	sfxHandle_t	respawnSound;
	sfxHandle_t talkSound;
	sfxHandle_t landSound;
	sfxHandle_t fallSound;
	sfxHandle_t jumpPadSound;

	sfxHandle_t oneMinuteSound;
	sfxHandle_t fiveMinuteSound;
	sfxHandle_t suddenDeathSound;

	sfxHandle_t threeFragSound;
	sfxHandle_t twoFragSound;
	sfxHandle_t oneFragSound;

	sfxHandle_t hitSound;
	sfxHandle_t hitSoundHighArmor;
	sfxHandle_t hitSoundLowArmor;
	sfxHandle_t hitTeamSound;
	sfxHandle_t impressiveSound;
	sfxHandle_t excellentSound;
	sfxHandle_t deniedSound;
	sfxHandle_t humiliationSound;
	sfxHandle_t assistSound;
	sfxHandle_t defendSound;
	sfxHandle_t firstImpressiveSound;
	sfxHandle_t firstExcellentSound;
	sfxHandle_t firstHumiliationSound;

	sfxHandle_t takenLeadSound;
	sfxHandle_t tiedLeadSound;
	sfxHandle_t lostLeadSound;

	sfxHandle_t voteNow;
	sfxHandle_t votePassed;
	sfxHandle_t voteFailed;

	sfxHandle_t watrInSound;
	sfxHandle_t watrOutSound;
	sfxHandle_t watrUnSound;

	sfxHandle_t flightSound;
	sfxHandle_t medkitSound;

#ifdef MISSIONPACK
	sfxHandle_t weaponHoverSound;
#endif

	// teamplay sounds
	sfxHandle_t captureAwardSound;
	sfxHandle_t redScoredSound;
	sfxHandle_t blueScoredSound;
	sfxHandle_t redLeadsSound;
	sfxHandle_t blueLeadsSound;
	sfxHandle_t teamsTiedSound;

	sfxHandle_t	captureYourTeamSound;
	sfxHandle_t	captureOpponentSound;
	sfxHandle_t	returnYourTeamSound;
	sfxHandle_t	returnOpponentSound;
	sfxHandle_t	takenYourTeamSound;
	sfxHandle_t	takenOpponentSound;

	sfxHandle_t redFlagReturnedSound;
	sfxHandle_t blueFlagReturnedSound;
#ifdef MISSIONPACK
	sfxHandle_t neutralFlagReturnedSound;
#endif
	sfxHandle_t	enemyTookYourFlagSound;
	sfxHandle_t yourTeamTookEnemyFlagSound;
	sfxHandle_t	youHaveFlagSound;
#ifdef MISSIONPACK
	sfxHandle_t	enemyTookTheFlagSound;
	sfxHandle_t yourTeamTookTheFlagSound;
	sfxHandle_t yourBaseIsUnderAttackSound;
#endif
	sfxHandle_t holyShitSound;

	// tournament sounds
	sfxHandle_t	count3Sound;
	sfxHandle_t	count2Sound;
	sfxHandle_t	count1Sound;
	sfxHandle_t	countFightSound;
	sfxHandle_t	countPrepareSound;

#ifdef MISSIONPACK
	// new stuff
	qhandle_t patrolShader;
	qhandle_t assaultShader;
	qhandle_t campShader;
	qhandle_t followShader;
	qhandle_t defendShader;
	qhandle_t teamLeaderShader;
	qhandle_t retrieveShader;
	qhandle_t escortShader;
	qhandle_t flagShaders[3];
	sfxHandle_t	countPrepareTeamSound;

	sfxHandle_t ammoregenSound;
	sfxHandle_t doublerSound;
	sfxHandle_t guardSound;
	sfxHandle_t scoutSound;

	qhandle_t cursor;
	qhandle_t selectCursor;
	qhandle_t sizeCursor;
#endif

	sfxHandle_t	regenSound;
	sfxHandle_t	protectSound;
	sfxHandle_t	n_healthSound;
	sfxHandle_t	hgrenb1aSound;
	sfxHandle_t	hgrenb2aSound;
	sfxHandle_t	wstbimplSound;
	sfxHandle_t	wstbimpmSound;
	sfxHandle_t	wstbimpdSound;
	sfxHandle_t	wstbactvSound;
	sfxHandle_t	headshotSound;
	sfxHandle_t	doubleJumpSound;
	sfxHandle_t	wallJumpSound;
	sfxHandle_t	dashSound;
	sfxHandle_t	hookFireSound;
	qhandle_t	fxSparkShader;
	qhandle_t	fxGlowShader;
	qhandle_t	fxRingShader;
	qhandle_t	hitMarkerShader;
	sfxHandle_t	hitSounds[3];
	sfxHandle_t	killConfirmSound;
	sfxHandle_t	airshotSound;
	sfxHandle_t	singularitySound;
	sfxHandle_t	bfgZapSound;
	qhandle_t	vampireShell;
	qhandle_t	rapidShell;
	qhandle_t	vampireItem;
	qhandle_t	rapidItem;
	qhandle_t	jetpackItem;
	qhandle_t	flameShader;
	sfxHandle_t	annStreak;
	sfxHandle_t	annMutator;
	sfxHandle_t	annJuggernaut;
	sfxHandle_t	annGunGame;
	sfxHandle_t	annInfo;
	sfxHandle_t	annMedal;
	sfxHandle_t	annBounty;
	sfxHandle_t	annWarn;
	sfxHandle_t	armorBreakSound;
	sfxHandle_t	wallRunSound;
	sfxHandle_t	hitSounds2[3];
	qhandle_t	edgeShader;
	qhandle_t	wetShell;
	sfxHandle_t	fireLoopSound;
	sfxHandle_t	thunderSound;
	sfxHandle_t	windSound;
	sfxHandle_t	steamSound;
	sfxHandle_t	zapSound;
	sfxHandle_t	chargeReadySound;
	qhandle_t	dmgDirShaders[16];

} cgMedia_t;


// The client game static (cgs) structure hold everything
// loaded or calculated from the gamestate.  It will NOT
// be cleared when a tournement restart is done, allowing
// all clients to begin playing instantly
typedef struct {
	gameState_t		gameState;			// gamestate from server
	glconfig_t		glconfig;			// rendering configuration
	float			screenXScale;		// derived from glconfig
	float			screenYScale;
	float			screenXBias;
	float			screenXStretch;	// full-width x scale, for full screen fills
	int				rmMode;			// rearmed game mode (CS_REARMED)
	int				rmMutator;
	int				rmMutatorEnd;
	int				rmJuggernaut;
	int				rmSwitches;		// RS_ bits
	qboolean		rmBounty[MAX_CLIENTS];
	int				rmMutatorStart;		// server time the running event started
	int				rmMutatorNext;		// event announced as incoming (0 = none)
	int				rmMutatorNextTime;	// server time it starts

	int				serverCommandSequence;	// reliable command stream counter
	int				processedSnapshotNum;// the number of snapshots cgame has requested

	qboolean		localServer;		// detected on startup by checking sv_running

	// parsed from serverinfo
	gametype_t		gametype;
	int				dmflags;
	int				teamflags;
	int				fraglimit;
	int				capturelimit;
	int				timelimit;
	int				maxclients;
	char			mapname[MAX_QPATH];
	char			redTeam[MAX_QPATH];
	char			blueTeam[MAX_QPATH];

	int				voteTime;
	int				voteYes;
	int				voteNo;
	qboolean		voteModified;			// beep whenever changed
	char			voteString[MAX_STRING_TOKENS];

	int				teamVoteTime[2];
	int				teamVoteYes[2];
	int				teamVoteNo[2];
	qboolean		teamVoteModified[2];	// beep whenever changed
	char			teamVoteString[2][MAX_STRING_TOKENS];

	int				levelStartTime;

	int				scores1, scores2;		// from configstrings
	int				redflag, blueflag;		// flag status from configstrings
	int				flagStatus;

	qboolean  newHud;

	//
	// locally derived information from gamestate
	//
	qhandle_t		gameModels[MAX_MODELS];
	sfxHandle_t		gameSounds[MAX_SOUNDS];

	int				numInlineModels;
	qhandle_t		inlineDrawModel[MAX_MODELS];
	vec3_t			inlineModelMidpoints[MAX_MODELS];

	clientInfo_t	clientinfo[MAX_CLIENTS];

	// teamchat width is *3 because of embedded color codes
	char			teamChatMsgs[TEAMCHAT_HEIGHT][TEAMCHAT_WIDTH*3+1];
	int				teamChatMsgTimes[TEAMCHAT_HEIGHT];
	int				teamChatPos;
	int				teamLastChatPos;

	int cursorX;
	int cursorY;
	qboolean eventHandling;
	qboolean mouseCaptured;
	qboolean sizingHud;
	void *capturedItem;
	qhandle_t activeCursor;

	// orders
	int currentOrder;
	qboolean orderPending;
	int orderTime;
	int currentVoiceClient;
	int acceptOrderTime;
	int acceptTask;
	int acceptLeader;
	char acceptVoice[MAX_NAME_LENGTH];

	// media
	cgMedia_t		media;

} cgs_t;

//==============================================================================

extern	cgs_t			cgs;
extern	cg_t			cg;
extern	centity_t		cg_entities[MAX_GENTITIES];
extern	weaponInfo_t	cg_weapons[MAX_WEAPONS];
extern	itemInfo_t		cg_items[MAX_ITEMS];
extern	markPoly_t		cg_markPolys[MAX_MARK_POLYS];

extern	vmCvar_t		cg_centertime;
extern	vmCvar_t		cg_runpitch;
extern	vmCvar_t		cg_runroll;
extern	vmCvar_t		cg_bobup;
extern	vmCvar_t		cg_bobpitch;
extern	vmCvar_t		cg_bobroll;
extern	vmCvar_t		cg_swingSpeed;
extern	vmCvar_t		cg_shadows;
extern	vmCvar_t		cg_gibs;
extern	vmCvar_t		cg_drawTimer;
extern	vmCvar_t		cg_drawFPS;
extern	vmCvar_t		cg_drawSnapshot;
extern	vmCvar_t		cg_draw3dIcons;
extern	vmCvar_t		cg_drawIcons;
extern	vmCvar_t		cg_drawAmmoWarning;
extern	vmCvar_t		cg_drawCrosshair;
extern	vmCvar_t		cg_drawCrosshairNames;
extern	vmCvar_t		cg_drawRewards;
extern	vmCvar_t		cg_drawTeamOverlay;
extern	vmCvar_t		cg_teamOverlayUserinfo;
extern	vmCvar_t		cg_crosshairX;
extern	vmCvar_t		cg_crosshairY;
extern	vmCvar_t		cg_crosshairSize;
extern	vmCvar_t		cg_crosshairHealth;
extern	vmCvar_t		cg_drawStatus;
extern	vmCvar_t		cg_draw2D;
extern	vmCvar_t		cg_animSpeed;
extern	vmCvar_t		cg_debugAnim;
extern	vmCvar_t		cg_debugPosition;
extern	vmCvar_t		cg_debugEvents;
extern	vmCvar_t		cg_railTrailTime;
extern	vmCvar_t		cg_errorDecay;
extern	vmCvar_t		cg_nopredict;
extern	vmCvar_t		cg_noPlayerAnims;
extern	vmCvar_t		cg_showmiss;
extern	vmCvar_t		cg_footsteps;
extern	vmCvar_t		cg_addMarks;
extern	vmCvar_t		cg_brassTime;
extern	vmCvar_t		cg_gun_frame;
extern	vmCvar_t		cg_gun_x;
extern	vmCvar_t		cg_gun_y;
extern	vmCvar_t		cg_gun_z;
extern	vmCvar_t		cg_drawGun;
extern	vmCvar_t		cg_viewsize;
extern	vmCvar_t		cg_tracerChance;
extern	vmCvar_t		cg_tracerWidth;
extern	vmCvar_t		cg_tracerLength;
extern	vmCvar_t		cg_autoswitch;
extern	vmCvar_t		cg_ignore;
extern	vmCvar_t		cg_simpleItems;
extern	vmCvar_t		cg_fov;
extern	vmCvar_t		cg_horPlus;
extern	vmCvar_t		cg_fx;
extern	vmCvar_t		cg_screenShake;
extern	vmCvar_t		cg_hitMarkers;
extern	vmCvar_t		cg_weaponKick;
extern	vmCvar_t		cg_speedFov;
extern	vmCvar_t		cg_moveCamera;
extern	vmCvar_t		cg_damageIndicator;
extern	vmCvar_t		cg_dashHud;
extern	vmCvar_t		cg_zipHud;
extern	vmCvar_t		cg_rearmAnnouncer;
extern	vmCvar_t		cg_hudWide;
extern	vmCvar_t		cg_lowHealthFX;
extern	vmCvar_t		cg_style;
extern	vmCvar_t		cg_gore;
extern	vmCvar_t		cg_screenFX;
extern	vmCvar_t		cg_bloodScreen;
extern	vmCvar_t		cg_killFlash;
extern	vmCvar_t		cg_speedLines;
extern	vmCvar_t		cg_zoomFov;
extern	vmCvar_t		cg_thirdPersonRange;
extern	vmCvar_t		cg_thirdPersonAngle;
extern	vmCvar_t		cg_thirdPerson;
extern	vmCvar_t		cg_lagometer;
extern	vmCvar_t		cg_drawAttacker;
extern	vmCvar_t		cg_synchronousClients;
extern	vmCvar_t		cg_teamChatTime;
extern	vmCvar_t		cg_teamChatHeight;
extern	vmCvar_t		cg_stats;
extern	vmCvar_t 		cg_forceModel;
extern	vmCvar_t 		cg_buildScript;
extern	vmCvar_t		cg_paused;
extern	vmCvar_t		cg_blood;
extern	vmCvar_t		cg_predictItems;
extern	vmCvar_t		cg_deferPlayers;
extern	vmCvar_t		cg_drawFriend;
extern	vmCvar_t		cg_teamChatsOnly;
#ifdef MISSIONPACK
extern	vmCvar_t		cg_noVoiceChats;
extern	vmCvar_t		cg_noVoiceText;
#endif
extern  vmCvar_t		cg_scorePlum;
extern	vmCvar_t		cg_smoothClients;
extern	vmCvar_t		pmove_fixed;
extern	vmCvar_t		pmove_msec;
//extern	vmCvar_t		cg_pmove_fixed;
extern	vmCvar_t		cg_cameraOrbit;
extern	vmCvar_t		cg_cameraOrbitDelay;
extern	vmCvar_t		cg_timescaleFadeEnd;
extern	vmCvar_t		cg_timescaleFadeSpeed;
extern	vmCvar_t		cg_timescale;
extern	vmCvar_t		cg_cameraMode;
#ifdef MISSIONPACK
extern  vmCvar_t		cg_smallFont;
extern  vmCvar_t		cg_bigFont;
extern	vmCvar_t		cg_noTaunt;
#endif
extern	vmCvar_t		cg_noProjectileTrail;
extern	vmCvar_t		cg_oldRail;
extern	vmCvar_t		cg_oldRocket;
extern	vmCvar_t		cg_oldPlasma;
extern	vmCvar_t		cg_trueLightning;
#ifdef MISSIONPACK
extern	vmCvar_t		cg_redTeamName;
extern	vmCvar_t		cg_blueTeamName;
extern	vmCvar_t		cg_currentSelectedPlayer;
extern	vmCvar_t		cg_currentSelectedPlayerName;
extern	vmCvar_t		cg_singlePlayer;
extern	vmCvar_t		cg_enableDust;
extern	vmCvar_t		cg_enableBreath;
extern	vmCvar_t		cg_singlePlayerActive;
extern  vmCvar_t		cg_recordSPDemo;
extern  vmCvar_t		cg_recordSPDemoName;
extern	vmCvar_t		cg_obeliskRespawnDelay;
#endif

//
// cg_main.c
//
const char *CG_ConfigString( int index );
const char *CG_Argv( int arg );

void QDECL CG_Printf( const char *msg, ... ) Q_PRINTF_FUNC(1, 2);
void QDECL CG_DevPrintf( const char *msg, ... ) Q_PRINTF_FUNC(1, 2);	// test / debug log lines: cg_devLog or developer only
void QDECL CG_Error( const char *msg, ... ) Q_NO_RETURN Q_PRINTF_FUNC(1, 2);

void CG_StartMusic( void );

void CG_UpdateCvars( void );

int CG_CrosshairPlayer( void );
int CG_LastAttacker( void );
void CG_LoadMenus(const char *menuFile);
void CG_KeyEvent(int key, qboolean down);
void CG_MouseEvent(int x, int y);
void CG_EventHandling(int type);
void CG_RankRunFrame( void );
void CG_SetScoreSelection(void *menu);
score_t *CG_GetSelectedScore( void );
void CG_BuildSpectatorString( void );


//
// cg_view.c
//
void CG_TestModel_f (void);
void CG_TestGun_f (void);
void CG_TestModelNextFrame_f (void);
void CG_TestModelPrevFrame_f (void);
void CG_TestModelNextSkin_f (void);
void CG_TestModelPrevSkin_f (void);
void CG_ZoomDown_f( void );
void CG_ZoomUp_f( void );
void CG_AddBufferedSound( sfxHandle_t sfx);

void CG_DrawActiveFrame( int serverTime, stereoFrame_t stereoView, qboolean demoPlayback );


//
// cg_drawtools.c
//
void CG_AdjustFrom640( float *x, float *y, float *w, float *h );
void CG_FillRect( float x, float y, float width, float height, const float *color );
void CG_DrawPic( float x, float y, float width, float height, qhandle_t hShader );
void CG_DrawString( float x, float y, const char *string, 
				   float charWidth, float charHeight, const float *modulate );


void CG_DrawStringExt( int x, int y, const char *string, const float *setColor, 
		qboolean forceColor, qboolean shadow, int charWidth, int charHeight, int maxChars );
void CG_DrawBigString( int x, int y, const char *s, float alpha );
void CG_DrawBigStringColor( int x, int y, const char *s, vec4_t color );
void CG_DrawSmallString( int x, int y, const char *s, float alpha );
void CG_DrawSmallStringColor( int x, int y, const char *s, vec4_t color );

int CG_DrawStrlen( const char *str );

float	*CG_FadeColor( int startMsec, int totalMsec );
float *CG_TeamColor( int team );
void CG_TileClear( void );
void CG_ColorForHealth( vec4_t hcolor );
void CG_GetColorForHealth( int health, int armor, vec4_t hcolor );

void UI_DrawProportionalString( int x, int y, const char* str, int style, vec4_t color );
void CG_DrawRect( float x, float y, float width, float height, float size, const float *color );
void CG_DrawSides(float x, float y, float w, float h, float size);
void CG_DrawTopBottom(float x, float y, float w, float h, float size);


//
// cg_draw.c, cg_newDraw.c
//
extern	int sortedTeamPlayers[TEAM_MAXOVERLAY];
extern	int	numSortedTeamPlayers;
extern	int drawTeamOverlayModificationCount;
extern  char systemChat[256];
extern  char teamChat1[256];
extern  char teamChat2[256];

void CG_AddLagometerFrameInfo( void );
void CG_AddLagometerSnapshotInfo( snapshot_t *snap );
void CG_CenterPrint( const char *str, int y, int charWidth );
void CG_DrawHead( float x, float y, float w, float h, int clientNum, vec3_t headAngles );
void CG_DrawActive( stereoFrame_t stereoView );
void CG_DrawFlagModel( float x, float y, float w, float h, int team, qboolean force2D );
void CG_DrawTeamBackground( int x, int y, int w, int h, float alpha, int team );
void CG_OwnerDraw(float x, float y, float w, float h, float text_x, float text_y, int ownerDraw, int ownerDrawFlags, int align, float special, float scale, vec4_t color, qhandle_t shader, int textStyle);
void CG_Text_Paint(float x, float y, float scale, vec4_t color, const char *text, float adjust, int limit, int style);
int CG_Text_Width(const char *text, float scale, int limit);
int CG_Text_Height(const char *text, float scale, int limit);
void CG_SelectPrevPlayer( void );
void CG_SelectNextPlayer( void );
float CG_GetValue(int ownerDraw);
qboolean CG_OwnerDrawVisible(int flags);
void CG_RunMenuScript(char **args);
void CG_ShowResponseHead( void );
void CG_SetPrintString(int type, const char *p);
void CG_InitTeamChat( void );
void CG_GetTeamColor(vec4_t *color);
const char *CG_GetGameStatusText( void );
const char *CG_GetKillerText( void );
void CG_Draw3DModel(float x, float y, float w, float h, qhandle_t model, qhandle_t skin, vec3_t origin, vec3_t angles);
void CG_Text_PaintChar(float x, float y, float width, float height, float scale, float s, float t, float s2, float t2, qhandle_t hShader);
void CG_CheckOrderPending( void );
const char *CG_GameTypeString( void );
qboolean CG_YourTeamHasFlag( void );
qboolean CG_OtherTeamHasFlag( void );
qhandle_t CG_StatusHandle(int task);



//
// cg_player.c
//
void CG_Player( centity_t *cent );
void CG_ResetPlayerEntity( centity_t *cent );
void CG_AddRefEntityWithPowerups( refEntity_t *ent, entityState_t *state, int team );
void CG_NewClientInfo( int clientNum );
sfxHandle_t	CG_CustomSound( int clientNum, const char *soundName );

//
// cg_predict.c
//
void CG_BuildSolidList( void );
int	CG_PointContents( const vec3_t point, int passEntityNum );
void CG_Trace( trace_t *result, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, 
					 int skipNumber, int mask );
void CG_PredictPlayerState( void );
void CG_LoadDeferredPlayers( void );
void CG_LoadClientInfoNow( clientInfo_t *ci );

//
// cg_preload.c (ds-fixes): bot models announced in CS_PRELOAD, loaded during the loading screen
//
void CG_PreloadRoster( void );
const clientInfo_t *CG_PreloadFind( const clientInfo_t *ci );
qboolean CG_PreloadLoadNow( const char *configstring );
void CG_PreloadReport_f( void );


//
// cg_events.c
//
void CG_CheckEvents( centity_t *cent );
const char	*CG_PlaceString( int rank );
void CG_EntityEvent( centity_t *cent, vec3_t position );
void CG_PainEvent( centity_t *cent, int health );


//
// cg_ents.c
//
void CG_SetEntitySoundPosition( centity_t *cent );
void CG_AddPacketEntities( void );
void CG_Beam( centity_t *cent );
void CG_AdjustPositionForMover(const vec3_t in, int moverNum, int fromTime, int toTime, vec3_t out, vec3_t angles_in, vec3_t angles_out);

void CG_PositionEntityOnTag( refEntity_t *entity, const refEntity_t *parent, 
							qhandle_t parentModel, char *tagName );
void CG_PositionRotatedEntityOnTag( refEntity_t *entity, const refEntity_t *parent, 
							qhandle_t parentModel, char *tagName );



//
// cg_weapons.c
//
void CG_NextWeapon_f( void );
void CG_PrevWeapon_f( void );
void CG_Weapon_f( void );

void CG_RegisterWeapon( int weaponNum );
void CG_RegisterItemVisuals( int itemNum );

void CG_FireWeapon( centity_t *cent );
void CG_MissileHitWall( int weapon, int clientNum, vec3_t origin, vec3_t dir, impactSound_t soundType );
void CG_MissileHitPlayer( int weapon, vec3_t origin, vec3_t dir, int entityNum );
void CG_ShotgunFire( entityState_t *es );
void CG_Bullet( vec3_t origin, int sourceEntityNum, vec3_t normal, qboolean flesh, int fleshEntityNum );

void CG_RailTrail( clientInfo_t *ci, vec3_t start, vec3_t end );
void CG_GrappleTrail( centity_t *ent, const weaponInfo_t *wi );
void CG_AddViewWeapon (playerState_t *ps);
void CG_AddPlayerWeapon( refEntity_t *parent, playerState_t *ps, centity_t *cent, int team );
void CG_DrawWeaponSelect( void );

void CG_OutOfAmmoChange( void );	// should this be in pmove?

//
// cg_marks.c
//
void	CG_InitMarkPolys( void );
void	CG_AddMarks( void );
void	CG_ImpactMark( qhandle_t markShader, 
				    const vec3_t origin, const vec3_t dir, 
					float orientation, 
				    float r, float g, float b, float a, 
					qboolean alphaFade, 
					float radius, qboolean temporary );

//
// cg_localents.c
//
void	CG_InitLocalEntities( void );
localEntity_t	*CG_AllocLocalEntity( void );
void	CG_AddLocalEntities( void );

//
// cg_effects.c
//
localEntity_t *CG_SmokePuff( const vec3_t p, 
				   const vec3_t vel, 
				   float radius,
				   float r, float g, float b, float a,
				   float duration,
				   int startTime,
				   int fadeInTime,
				   int leFlags,
				   qhandle_t hShader );
void CG_BubbleTrail( vec3_t start, vec3_t end, float spacing );
void CG_SpawnEffect( vec3_t org );
void CG_MoveTechPuff( centity_t *cent, int type );
void CG_ArcBeam( vec3_t start, vec3_t end );

//
// cg_fx.c
//
qboolean CG_FxSound( const vec3_t origin, int entityNum, int channel, sfxHandle_t sfx );
void CG_AddFxSpark( localEntity_t *le );
void CG_AddFxFlash( localEntity_t *le );
void CG_AddFxRing( localEntity_t *le );
void CG_ImpactFX( int weapon, const vec3_t origin, const vec3_t dir, qboolean alt );
void CG_MissileGlow( centity_t *cent );
void CG_ApplyScreenShake( void );

// cg_moves.c
void CG_MovesRegister( void );
void CG_MoveEvent( centity_t *cent, int event, int parm );
void CG_MovesFrame( void );
qboolean CG_MoveCamera( vec3_t origin, vec3_t angles );
float CG_MoveFov( float fov );

// cg_zipline.c
void CG_ZipRegister( void );
void CG_ZipEvent( centity_t *cent, int event, int parm );
void CG_ZipFrame( void );
float CG_ZipFov( float fov );
void CG_ZipDrawHud( float cx, float cy );
qboolean CG_ZipAnchorReady( void );
void CG_HitConfirm( centity_t *cent );
void CG_BFGLaser( vec3_t start, vec3_t end );
void CG_RicochetFX( vec3_t origin );
void CG_GibFX( vec3_t origin );
void CG_WeaponKick( int weapon );
void CG_FxShake( const vec3_t org, float amplitude, float radius );
void CG_FxPunch( float amplitude );
void CG_FxSparks( const vec3_t org, const vec3_t normal, const vec3_t color, int count, float speed, int life, float focus, int embers );
void CG_FxExplosion( const vec3_t origin, const vec3_t dir, float scale, const vec3_t color, qboolean smoke );
void CG_FxChargeGlow( refEntity_t *gun, qhandle_t weaponModel, playerState_t *ps );

//
// cg_gorekeep.c: corpses and blood stay until the map ends (cg_gorePersist, replaces cg_bloodPools)
//
void CG_GoreKeepInit( void );
void CG_GoreKeepFrame( void );
int CG_GorePersist( void );
int CG_GoreKeepDebug( void );
int CG_GoreKeepCap( void );
qboolean CG_GoreInView( const vec3_t p, float radius );
qboolean CG_GoreKeepCullCorpse( centity_t *cent );
void CG_BloodUnseenCorpse( const vec3_t origin );		// cg_blood.c
extern int	cg_goreStatPermMade, cg_goreStatPerm, cg_goreStatPermDrawn, cg_goreStatMarks, cg_goreStatGibs, cg_goreStatGibsDrawn;

//
// cg_gore.c
//
int CG_GoreLevel( void );
void CG_GoreAddEntities( void );
void CG_GoreDrawScreen( void );
void CG_GoreHit( entityState_t *es, const vec3_t position );
qboolean CG_GoreDeath( centity_t *cent, int killer, qboolean gib );
qboolean CG_GoreHead( centity_t *cent, refEntity_t *torso, refEntity_t *head );
qboolean CG_GoreSkipTorso( centity_t *cent );
void CG_GoreBodyShell( refEntity_t *ent, entityState_t *state );
void CG_GoreScreenBlood( float amount );
void CG_GoreKillFlash( float amount );
void CG_GoreWallSpatter( const vec3_t org, const vec3_t normal, const vec3_t dir, float radius, int drips );
void CG_GoreWallSplat( const vec3_t org, const vec3_t normal, int speed );
void CG_GoreCorpseBurst( const vec3_t org );
void CG_GoreFire( const vec3_t org, const vec3_t vel, float size, float endSize, int life, const vec3_t color, float drag, float lift );
void CG_GoreSmoke( const vec3_t org, const vec3_t vel, float size, float endSize, int life, float shade, float alpha, float lift, int delay );
void CG_GoreParticleDrop( const vec3_t org, const vec3_t vel, float size, int life, const float *color );
void CG_GoreLight( const vec3_t org, float radius, float r, float g, float b, int life );
qboolean CG_GoreLimbGib( qhandle_t model, const vec3_t org, const vec3_t vel, float scale, float bleed, qboolean head );	// en-feel
void CG_GoreBloodBurst( const vec3_t org, const vec3_t dir, int drops, float speed, int mists, float mistSize );	// en-feel
void CG_GoreTrailMark( const vec3_t org, const vec3_t normal, float radius );	// en-feel: a crawler's blood trail

// round 4 HIT FEEL (en-feel, cg_hitfeel.c, bg_hitfeel.h): flinch, limbs, crawl pose, armour cracks, death screams
void CG_HF_Register( void );										// CG_GoreClear (map load)
void CG_HF_Hit( entityState_t *es, const vec3_t position );		// EV_GORE_HIT
void CG_HF_Pain( centity_t *cent );								// EV_PAIN
void CG_HF_Event( entityState_t *es, const vec3_t position );		// EV_HITFEEL
qboolean CG_HF_DeathScreams( void );								// EV_DEATHx: skip the stock *death sound
void CG_HF_WallSplat( const vec3_t origin, int speed );			// EV_WALLSPLAT: the thud
void CG_HF_PlayerAngles( centity_t *cent, vec3_t legs, vec3_t torso, vec3_t head );	// CG_PlayerAngles
qboolean CG_HF_Legs( centity_t *cent, refEntity_t *legs );		// CG_Player: qtrue = don't draw the legs
void CG_HF_Torso( centity_t *cent, refEntity_t *torso, clientInfo_t *ci );	// CG_Player: arm-less skin, stumps
qboolean CG_HF_HideWeapon( centity_t *cent );						// CG_Player: panic weapon drop
void CG_HF_BodyShell( refEntity_t *ent, entityState_t *state );	// CG_AddRefEntityWithPowerups: armour cracks

//
// cg_physics.c (rearmed)
//
void CG_PhysicsRegister( void );
void CG_PhysicsFrame( void );
void CG_Prop( centity_t *cent );
qboolean CG_PhysFragment( localEntity_t *le );
void CG_PhysicsBlast( const vec3_t origin, float radius, float force );
void CG_PhysicsExplosion( int weapon, const vec3_t origin, const vec3_t dir, int event );
void CG_PropBreakFX( entityState_t *es, const vec3_t origin );
void CG_PropHitFX( centity_t *cent, int strength );
void CG_WallSplatFX( entityState_t *es, const vec3_t origin );
void CG_CorpseTumble( centity_t *cent, vec3_t legsAngles );

//
// cg_roguelike.c (DESCENT)
//
void CG_RL_Register( void );
void CG_RL_ParseConfigstrings( void );
qboolean CG_RL_ServerCommand( const char *cmd );
void CG_RL_Draw2D( void );
qboolean CG_RL_DrawIntermission( void );
qboolean CG_RL_Pick( int num );
qboolean CG_RL_Cycle( int dir );
qboolean CG_RL_Active( void );
int CG_RL_ClientRole( int clientNum );
void CG_RL_RoleColor( int role, vec4_t out );
void CG_RL_BodyShell( refEntity_t *ent, entityState_t *state );
void CG_RL_PlayerFX( centity_t *cent );
int CG_RL_CardCount( int card );				// how many of a card the player's run holds

//
// cg_ds_loot.c (DESCENT v2 LOOT: growth configstring, gems, chests, jackpot, card tags)
//
void CG_DS_LootRegister( void );
// cg_ds_arsenal.c (DESCENT v2 ARSENAL: weapon levels, evolutions)
void CG_DS_ArsenalRegister( void );
qboolean CG_DS_ArsenalServerCommand( const char *cmd );		// dslvl / dsevo
void CG_DS_ArsenalEvent( centity_t *cent, vec3_t position );	// EV_DS_FX
qboolean CG_DS_Missile( centity_t *cent );					// evolved / level-8 missiles (qtrue = drawn)
void CG_DS_MissileImpact( entityState_t *es, vec3_t origin, vec3_t dir );
void CG_DS_WeaponFX( refEntity_t *gun, qhandle_t weaponModel, playerState_t *ps, centity_t *cent );
void CG_DS_LightningBeam( centity_t *cent, vec3_t start, vec3_t end );
void CG_DS_FireWeapon( centity_t *cent );
void CG_DS_ShotgunPellet( vec3_t start, vec3_t end, int skipNum, int code );
int CG_DS_ArsenalLevel( int w );							// 0 outside a run / not levelling
qboolean CG_DS_ArsenalEvoColor( int w, vec4_t out );		// the evolved colour of a weapon the player holds
void CG_DS_DrawWeaponLevel( float x, float y, float iconX, float iconSize );
// cg_ds_skills.c (SKILLS)
void CG_DS_SkillsRegister( void );
qboolean CG_DS_SkillsServerCommand( const char *cmd );	// dsskl
void CG_DS_DrawSkill( float x, float y );			// status bar, above the weapon level pips
void CG_DS_ParseGrow( void );					// CS_DESCENTGROW changed
int CG_DS_WeaponLevel( int w );					// the player's weapon level 1..8 (0 not levelling / not a run)
int CG_DS_WeaponXP( int w );					// xp toward the next level (resets on a level up)
int CG_DS_PassiveLevel( int p );
int CG_DS_EvolvedMask( void );					// bit per dsEvolution_t
int CG_DS_ChestsBanked( void );
qboolean CG_DS_LootEntity( centity_t *cent );	// CG_General: gems, chests, the vacuum
void CG_DS_VaultCrystal( centity_t *cent );			// RM_ENT_VAULTGEMS: the vault's SHOOTING GALLERY target
// ds-secrets (cg_ds_secrets.c): secret stashes
void CG_DS_SecretsRegister( void );
qboolean CG_DS_SecretsEntity( centity_t *cent );	// CG_General: RM_ENT_STASH
void CG_DS_SecretsDraw2D( void );				// CG_Draw2D: SECRET FOUND fanfare, DOWSING ROD compass
void CG_DS_DrawHud( void );						// the held weapon's xp bar
float CG_DS_DrawCornerChests( float x, float y, float w );
const char *CG_DS_CardTag( int card );			// synergy: "EVOLVES ROCKET LAUNCHER", "COMBO: ..."
const char *CG_DS_CardFooter( int card );		// "SPITE 2 -> 3", "LV 4 -> 5"
const char *CG_DS_CardDesc( int card );			// weapon cards: the next level's effect
qhandle_t CG_DS_WeaponIcon( int w );
qboolean CG_DS_DrawJackpot( int phase );		// the chest screen (RLP_CHEST)
// FRENZY (round 3, ds-frenzy, cg_ds_frenzy.c): chain counter, crits, power spikes, loot beams
void CG_DS_FrenzyRegister( void );
qboolean CG_DS_FrenzyServerCommand( const char *cmd );	// dsfz / dsfzend / dsfzspk
void CG_DS_FrenzyEvent( centity_t *cent, vec3_t position );	// EV_DS_FRENZY
qboolean CG_DS_FrenzyEntity( centity_t *cent );	// CG_General: loot beams, spike pickups
void CG_DS_FrenzyDraw2D( void );				// CG_RL_Draw2D: edges, ring, chain counter, tally
void CG_DS_FrenzyHitConfirm( int flags );		// CG_HitConfirm: HIT_CRIT
qboolean CG_DS_FrenzyCritMarker( void );		// CG_DrawHitFeedback: the marker turns gold
void CG_DS_FrenzyTick( int note );				// LOOT gem tick: the hot layer while a chain runs
// CARDS (round 3, ds-cards, cg_ds_cards.c): card rarity reveal, legendaries, synergy ding, floor-end tally
const char *CG_DS_CardTagEx( int card, int *partner, int *weapon );	// (cg_ds_loot.c) the tag + what it snaps with
void CG_DSC_Register( void );						// CG_RL_Register: sounds, cvar cg_dsReveal
qboolean CG_DSC_ServerCommand( const char *cmd );	// dsctally / dsclg
void CG_DSC_Event( centity_t *cent, vec3_t position );	// EV_DS_CARDS
void CG_DSC_AddScene( void );						// scene: MAGNETAR orbs
void CG_DSC_BodyShell( refEntity_t *ent, entityState_t *state );	// CG_RL_BodyShell: the DOUBLE AGENT's shell
void CG_DSC_Draw2D( void );							// CG_RL_Draw2D: legendary HUD row, evolution ready, dstally preview
qboolean CG_DSC_RevealOn( void );					// cg_dsReveal 1
int CG_DSC_RevealLeft( int start, int n, const int *offer );	// msec until the reveal is done (<= 0: done since -n)
void CG_DSC_RevealSkip( int start );				// fire / Space / a pick key during the reveal: flip them all now
qboolean CG_DSC_CardAnim( int i, int start, int n, const int *offer, float *dy, float *alpha, float *flip, float *scale, float *flash );
qboolean CG_DSC_CardBack( int card, float x, float y, float w, float h, float sx, float alpha );	// CG_RL_DrawCard: the rarity back
void CG_DSC_CardFace( int slot, int card, float x, float y, float w, float h, float alpha );	// rarity frame, shimmer, sweep
void CG_DSC_DrawOfferFX( int start, int n, const int *offer );	// over the cards: screen-edge flare, legendary light
void CG_DSC_OnPick( int card, float cx, float cy );	// the pick (synergy ding)
void CG_DSC_DrawDing( void );						// the synergy ding (intermission)
qboolean CG_DSC_DrawTally( int floorPlayed );		// RLP_END of a won floor: qtrue while the tally owns the screen

//
// cg_ds_acts.c (DESCENT v2 ACTS: biomes, act intro, vault, bosses)
//
void CG_DS_ActsRegister( void );
void CG_DS_ActsParse( void );						// CS_DESCENT / CS_DESCENTBOSS changed
int CG_DS_BossClient( void );						// -1 none
qboolean CG_DS_Practice( void );			// en-boss: a boss practice floor (no records)
void CG_DS_RematchDraw( float x, float y, float h, float al );	// en-rematch: the modifier line left of the boss bar
qboolean CG_DS_RematchOn( void );			// en-rematch: this practice floor is a rematch
qboolean CG_DS_RematchFallen( void );		// en-rematch: a ONE LIFE rematch lost by dying
qboolean CG_DS_NoRecords( void );			// CG_DS_Practice() || CG_RL_Daily() (cg_roguelike.c)
qboolean CG_RL_Daily( void );				// en-daily (cg_roguelike.c): a daily challenge floor (no records, no mastery)
int CG_RecordsBossKill( int boss, int skill, int msec, int *isNew );	// en-boss (cg_style.c): the fastest timed boss kill
float CG_DS_AnimSpeed( centity_t *cent );			// ds-boss: a fast boss animates quicker, a slow one heavier
qboolean CG_DS_BodyShell( refEntity_t *ent, entityState_t *state );	// ds-boss: nemesis rim / armoured boss (qtrue = done)
qboolean CG_DS_ActsServerCommand( const char *cmd );	// ds-boss: dsnem (nemesis callouts)
const char *CG_DS_FloorGoal( int lives );			// floor strip line on boss / vault floors (NULL: frag race)
const char *CG_DS_HudLine( void );					// corner HUD line on boss / vault floors (NULL: frags)
// ds-intro (cg_ds_intro.c): the opponents on the loading screen, the top-edge versus intro
void CG_DSI_Register( void );						// CG_Init
qboolean CG_DSI_DrawLoading( void );				// CG_DrawInformation: qtrue = a DESCENT floor drawn
void CG_DSI_Draw2D( void );							// CG_Draw2D, before the descent HUD
qboolean CG_DSI_Busy( void );						// the intro runs (act card / boss bar wait)
qboolean CG_DSI_Covers( void );						// the intro ran this floor (no floor strip)
void CG_DSI_Command_f( void );						// dev: dsintro [skirmish | loading]
qboolean CG_DSI_ShowLoading( void );				// dev: CG_DrawActive draws the loading screen again
int CG_DSI_AnnouncerFreeAt( void );					// ds-arrivals: cg.time the announcer is free (shared voice queue)
void CG_DSI_AnnouncerHold( int untilTime );			// ds-arrivals: another voice line speaks until then
int CG_DSI_BossBarTime( int t );					// ds-arrivals: cg_ds_acts boss bar clock (-1 while the boss is still to come)
void CG_DS_DrawBiome( void );						// under the HUD: act grade + haze
void CG_DS_AddBiome( void );						// scene: ambient particles
void CG_DSM_Draw( void );						// ds-maps (cg_ds_maps.c): under the HUD, the floor's lighting variant
int CG_DSM_Variant( void );						// ds-maps: the floor variant (CS_DESCENT mv), 0 = as built
void CG_DS_BossScale( centity_t *cent, refEntity_t *legs );	// CG_Player: the boss drawn big
void CG_DS_BossFx( centity_t *cent );				// RM_ENT_BOSSFX telegraphs
void CG_DS_BossProp( centity_t *cent );				// RM_ENT_BOSSPROP drones / decoys
void CG_DS_ActsDraw2D( void );						// act intro, vault timer, boss bar
// ds-surprise (cg_ds_surprise.c): ambush pillars, the golden meteor, side callouts, BLACKOUT, the ghost's look
void CG_DS_SurpriseRegister( void );				// CG_Init
qboolean CG_DS_SurpriseEntity( centity_t *cent );	// CG_General: RM_ENT_SURPRISE
qboolean CG_DS_SurpriseServerCommand( const char *cmd );	// dsuann
void CG_DS_SurpriseDraw2D( void );					// CG_Draw2D: right-edge callouts, mini-boss / event line
void CG_DS_SurpriseDrawWorldFX( void );				// CG_Draw2D, under the HUD: BLACKOUT, BLOOD ECLIPSE
// round 4 en-bestiary (cg_ds_bestiary.c): "NEW IN BESTIARY" callouts at the left edge
qboolean CG_DS_BestiaryServerCommand( const char *cmd );	// dsbst <entry>
void CG_DS_BestiaryDraw2D( void );					// CG_Draw2D, next to the ds-surprise callouts
qboolean CG_DS_GhostRefEntity( refEntity_t *ent, entityState_t *state );	// CG_AddRefEntityWithPowerups (qtrue = drawn)
int CG_DS_SurpriseMod( void );						// the floor modifier (DSMOD_)

//
// cg_rearmed.c
//
void CG_RearmRegister( void );
void CG_RearmParseConfigstring( void );
qboolean CG_RearmServerCommand( const char *cmd );
void CG_DrawRearmed( void );
void CG_RearmDrawOverlays( void );

//
// cg_mutators.c (random events, slow motion, tips)
//
extern	vmCvar_t		cg_eventFX;
extern	vmCvar_t		cg_tips;
extern	vmCvar_t		cg_enemyTags;		// 0 clean, 1 minimal (ELITE / BOSS label), 2 full (name, kit, award sprites)
extern	vmCvar_t		cg_tipsSeen;
void CG_MutatorsRegister( void );
void CG_StrikeMarker( centity_t *cent );
void CG_MeteorMissile( centity_t *cent );
void CG_MeteorImpact( vec3_t origin, vec3_t dir );
void CG_CorpseExplode( entityState_t *es );
void CG_SlowMoEvent( int strength );
void CG_DrawMutatorFX( void );
void CG_DrawEventHud( float y );
void CG_DrawTip( void );
void CG_MutatorsFrame( void );
char *CG_RearmObituary( int mod, qboolean self, qboolean byPlayer, char **message2 );
void CG_Tip( int tip );
void CG_TipsFrame( void );
const char *CG_KeyForCommand( const char *binding );
#define TIP_PERK		0
#define TIP_CHARGE		1
#define TIP_SLIDE		2
#define TIP_SLAM		3
#define TIP_EVENT		4
#define TIP_WHEEL		5
#define TIP_DASH		6
#define TIP_HOOK		7
#define TIP_WHEELUSE	8
#define TIP_ZIP			9
#define TIP_NUM			10

//
// cg_style.c
//
void CG_StyleRegister( void );
void CG_DrawStyle( void );
void CG_DrawPerks( void );
void CG_DrawPerkBuild( qboolean expanded );
qboolean CG_PerkPanelPick( int num );
void CG_RecordsObituary( int target, int attacker, int mod );
void CG_RecordsFrame( void );
void CG_DrawRecordsIntermission( void );
void CG_Records_f( void );
qboolean CG_StyleServerCommand( const char *cmd );
void CG_RearmDamageDir( float yaw );
void CG_HudAnchor( int side );
// en-screens (cg_edge.c): the shared right-edge callout stack, top to bottom in this order
typedef enum {
	EDGE_STATUS,		// cg_ds_surprise.c: mini-boss health / wild event timer (standing lines)
	EDGE_CHAMP,			// cg_ds_affix.c: champion pack
	EDGE_NEMESIS,		// cg_ds_acts.c: NEMESIS / NEMESIS SLAIN / FINAL BLOW / ELIMINATED
	EDGE_PERSONALITY,	// cg_personality.c: nemesis levels, revenge, veterans
	EDGE_SURPRISE,		// cg_ds_surprise.c: ambush / ghost / event callouts
	EDGE_THIEF,			// cg_thief.c: STOLEN / RECLAIMED
	EDGE_SECRET,		// cg_ds_secrets.c: SECRET FOUND
	EDGE_EVO,			// cg_ds_cards.c: an evolution is ready
	EDGE_TYPES,			// cg_ds_types.c (en-tune): the run-start adaptation note
	EDGE_NUM
} edgeSlotId_t;
float CG_EdgeSlot( int id, float h );		// y for a right-edge callout of height h (x 620, CG_HudAnchor( 1 ))
void CG_EdgeText( float x, float y, const char *s, const float *col, int cw, int ch );	// right-aligned at x
void CG_EdgeCard( float x, float y, float h, float al, const char *l1, const char *l2,
	const vec3_t acc, const vec3_t bg, float lift, const vec3_t body );		// the shared title + body card
void CG_CleanUpper( const char *in, char *out, int size );		// no colour codes, upper case
void CG_ClientNameUpper( int client, char *out, int size );		// the same for a client ("" out of range)
float CG_StyleFeedBottom( void );			// cg_style.c: bottom of the style acts feed (0 = none); the stack starts under it
void CG_OverchargeTrail( vec3_t start, vec3_t end );
void CG_ElementsPlayerFX( centity_t *cent );
void CG_FirePatch( centity_t *cent );
void CG_WindBlastFX( entityState_t *es );
void CG_ThunderFX( vec3_t ground, vec3_t sky );
void CG_SteamFX( vec3_t origin );
float CG_Clamp01( float v );
extern localEntity_t	cg_activeLocalEntities;
void CG_RearmPowerupShells( refEntity_t *ent, entityState_t *state );
void CG_RearmItemShader( gitem_t *item, refEntity_t *ent );
void CG_RearmHead( refEntity_t *head );
void CG_RearmPlayerFX( centity_t *cent, refEntity_t *head );
#ifdef MISSIONPACK
void CG_KamikazeEffect( vec3_t org );
void CG_ObeliskExplode( vec3_t org, int entityNum );
void CG_ObeliskPain( vec3_t org );
void CG_InvulnerabilityImpact( vec3_t org, vec3_t angles );
void CG_InvulnerabilityJuiced( vec3_t org );
void CG_LightningBoltBeam( vec3_t start, vec3_t end );
#endif
void CG_ScorePlum( int client, vec3_t org, int score );

void CG_GibPlayer( vec3_t playerOrigin );
void CG_BigExplode( vec3_t playerOrigin );

void CG_Bleed( vec3_t origin, int entityNum );

localEntity_t *CG_MakeExplosion( vec3_t origin, vec3_t dir,
								qhandle_t hModel, qhandle_t shader, int msec,
								qboolean isSprite );

//
// cg_snapshot.c
//
void CG_ProcessSnapshots( void );

//
// cg_info.c
//
void CG_LoadingString( const char *s );
void CG_LoadingItem( int itemNum );
void CG_LoadingClient( int clientNum );
void CG_DrawInformation( void );

//
// cg_scoreboard.c
//
qboolean CG_DrawOldScoreboard( void );
void CG_DrawTourneyScoreboard( void );

//
// cg_consolecmds.c
//
qboolean CG_ConsoleCommand( void );
void CG_InitConsoleCommands( void );

//
// cg_servercmds.c
//
void CG_ExecuteNewServerCommands( int latestSequence );
void CG_ParseServerinfo( void );
void CG_SetConfigValues( void );
void CG_ShaderStateChanged(void);
#ifdef MISSIONPACK
void CG_LoadVoiceChats( void );
void CG_VoiceChatLocal( int mode, qboolean voiceOnly, int clientNum, int color, const char *cmd );
void CG_PlayBufferedVoiceChats( void );
#endif

//
// cg_playerstate.c
//
void CG_Respawn( void );
void CG_TransitionPlayerState( playerState_t *ps, playerState_t *ops );
void CG_CheckChangedPredictableEvents( playerState_t *ps );

//
// cg_wheel.c
//
extern	vmCvar_t		cg_wheel;
extern	vmCvar_t		cg_wheelSensitivity;
void CG_WheelDown_f( void );
void CG_WheelUp_f( void );
void CG_WheelTrackWeapon( void );
void CG_DrawWeaponWheel( void );


//===============================================

//
// system traps
// These functions are how the cgame communicates with the main game system
//

// print message on the local console
void		trap_Print( const char *fmt );

// abort the game
void		trap_Error(const char *fmt) Q_NO_RETURN;

// milliseconds should only be used for performance tuning, never
// for anything game related.  Get time from the CG_DrawActiveFrame parameter
int			trap_Milliseconds( void );

// console variable interaction
void		trap_Cvar_Register( vmCvar_t *vmCvar, const char *varName, const char *defaultValue, int flags );
void		trap_Cvar_Update( vmCvar_t *vmCvar );
void		trap_Cvar_Set( const char *var_name, const char *value );
void		trap_Cvar_VariableStringBuffer( const char *var_name, char *buffer, int bufsize );

// ServerCommand and ConsoleCommand parameter access
int			trap_Argc( void );
void		trap_Argv( int n, char *buffer, int bufferLength );
void		trap_Args( char *buffer, int bufferLength );

// filesystem access
// returns length of file
int			trap_FS_FOpenFile( const char *qpath, fileHandle_t *f, fsMode_t mode );
void		trap_FS_Read( void *buffer, int len, fileHandle_t f );
void		trap_FS_Write( const void *buffer, int len, fileHandle_t f );
void		trap_FS_FCloseFile( fileHandle_t f );
int			trap_FS_Seek( fileHandle_t f, long offset, int origin ); // fsOrigin_t

// add commands to the local console as if they were typed in
// for map changing, etc.  The command is not executed immediately,
// but will be executed in order the next time console commands
// are processed
void		trap_SendConsoleCommand( const char *text );

// register a command name so the console can perform command completion.
// FIXME: replace this with a normal console command "defineCommand"?
void		trap_AddCommand( const char *cmdName );
void		trap_RemoveCommand( const char *cmdName );

// send a string to the server over the network
void		trap_SendClientCommand( const char *s );

// force a screen update, only used during gamestate load
void		trap_UpdateScreen( void );

// model collision
void		trap_CM_LoadMap( const char *mapname );
int			trap_CM_NumInlineModels( void );
clipHandle_t trap_CM_InlineModel( int index );		// 0 = world, 1+ = bmodels
clipHandle_t trap_CM_TempBoxModel( const vec3_t mins, const vec3_t maxs );
int			trap_CM_PointContents( const vec3_t p, clipHandle_t model );
int			trap_CM_TransformedPointContents( const vec3_t p, clipHandle_t model, const vec3_t origin, const vec3_t angles );
void		trap_CM_BoxTrace( trace_t *results, const vec3_t start, const vec3_t end,
					  const vec3_t mins, const vec3_t maxs,
					  clipHandle_t model, int brushmask );
void		trap_CM_CapsuleTrace( trace_t *results, const vec3_t start, const vec3_t end,
					  const vec3_t mins, const vec3_t maxs,
					  clipHandle_t model, int brushmask );
void		trap_CM_TransformedBoxTrace( trace_t *results, const vec3_t start, const vec3_t end,
					  const vec3_t mins, const vec3_t maxs,
					  clipHandle_t model, int brushmask,
					  const vec3_t origin, const vec3_t angles );
void		trap_CM_TransformedCapsuleTrace( trace_t *results, const vec3_t start, const vec3_t end,
					  const vec3_t mins, const vec3_t maxs,
					  clipHandle_t model, int brushmask,
					  const vec3_t origin, const vec3_t angles );

// Returns the projection of a polygon onto the solid brushes in the world
int			trap_CM_MarkFragments( int numPoints, const vec3_t *points, 
			const vec3_t projection,
			int maxPoints, vec3_t pointBuffer,
			int maxFragments, markFragment_t *fragmentBuffer );

// normal sounds will have their volume dynamically changed as their entity
// moves and the listener moves
void		trap_S_StartSound( vec3_t origin, int entityNum, int entchannel, sfxHandle_t sfx );
void		trap_S_StopLoopingSound(int entnum);

// a local sound is always played full volume
void		trap_S_StartLocalSound( sfxHandle_t sfx, int channelNum );
void		trap_S_ClearLoopingSounds( qboolean killall );
void		trap_S_AddLoopingSound( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx );
void		trap_S_AddRealLoopingSound( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx );
void		trap_S_UpdateEntityPosition( int entityNum, const vec3_t origin );

// respatialize recalculates the volumes of sound as they should be heard by the
// given entityNum and position
void		trap_S_Respatialize( int entityNum, const vec3_t origin, vec3_t axis[3], int inwater );
sfxHandle_t	trap_S_RegisterSound( const char *sample, qboolean compressed );		// returns buzz if not found
void		trap_S_StartBackgroundTrack( const char *intro, const char *loop );	// empty name stops music
void	trap_S_StopBackgroundTrack( void );


void		trap_R_LoadWorldMap( const char *mapname );

// all media should be registered during level startup to prevent
// hitches during gameplay
qhandle_t	trap_R_RegisterModel( const char *name );			// returns rgb axis if not found
qhandle_t	trap_R_RegisterSkin( const char *name );			// returns all white if not found
qhandle_t	trap_R_RegisterShader( const char *name );			// returns all white if not found
qhandle_t	trap_R_RegisterShaderNoMip( const char *name );			// returns all white if not found

// a scene is built up by calls to R_ClearScene and the various R_Add functions.
// Nothing is drawn until R_RenderScene is called.
void		trap_R_ClearScene( void );
void		trap_R_AddRefEntityToScene( const refEntity_t *re );

// polys are intended for simple wall marks, not really for doing
// significant construction
void		trap_R_AddPolyToScene( qhandle_t hShader , int numVerts, const polyVert_t *verts );
void		trap_R_AddPolysToScene( qhandle_t hShader , int numVerts, const polyVert_t *verts, int numPolys );
void		trap_R_AddLightToScene( const vec3_t org, float intensity, float r, float g, float b );
void		trap_R_AddAdditiveLightToScene( const vec3_t org, float intensity, float r, float g, float b );
int			trap_R_LightForPoint( vec3_t point, vec3_t ambientLight, vec3_t directedLight, vec3_t lightDir );
void		trap_R_RenderScene( const refdef_t *fd );
void		trap_R_SetColor( const float *rgba );	// NULL = 1,1,1,1
void		trap_R_DrawStretchPic( float x, float y, float w, float h, 
			float s1, float t1, float s2, float t2, qhandle_t hShader );
void		trap_R_ModelBounds( clipHandle_t model, vec3_t mins, vec3_t maxs );
int			trap_R_LerpTag( orientation_t *tag, clipHandle_t mod, int startFrame, int endFrame, 
					   float frac, const char *tagName );
void		trap_R_RemapShader( const char *oldShader, const char *newShader, const char *timeOffset );
qboolean	trap_R_inPVS( const vec3_t p1, const vec3_t p2 );

// The glconfig_t will not change during the life of a cgame.
// If it needs to change, the entire cgame will be restarted, because
// all the qhandle_t are then invalid.
void		trap_GetGlconfig( glconfig_t *glconfig );

// the gamestate should be grabbed at startup, and whenever a
// configstring changes
void		trap_GetGameState( gameState_t *gamestate );

// cgame will poll each frame to see if a newer snapshot has arrived
// that it is interested in.  The time is returned separately so that
// snapshot latency can be calculated.
void		trap_GetCurrentSnapshotNumber( int *snapshotNumber, int *serverTime );

// a snapshot get can fail if the snapshot (or the entties it holds) is so
// old that it has fallen out of the client system queue
qboolean	trap_GetSnapshot( int snapshotNumber, snapshot_t *snapshot );

// retrieve a text command from the server stream
// the current snapshot will hold the number of the most recent command
// qfalse can be returned if the client system handled the command
// argc() / argv() can be used to examine the parameters of the command
qboolean	trap_GetServerCommand( int serverCommandNumber );

// returns the most recent command number that can be passed to GetUserCmd
// this will always be at least one higher than the number in the current
// snapshot, and it may be quite a few higher if it is a fast computer on
// a lagged connection
int			trap_GetCurrentCmdNumber( void );	

qboolean	trap_GetUserCmd( int cmdNumber, usercmd_t *ucmd );

// used for the weapon select and zoom
void		trap_SetUserCmdValue( int stateValue, float sensitivityScale );

// aids for VM testing
void		testPrintInt( char *string, int i );
void		testPrintFloat( char *string, float f );

int			trap_MemoryRemaining( void );
void		trap_R_RegisterFont(const char *fontName, int pointSize, fontInfo_t *font);
qboolean	trap_Key_IsDown( int keynum );
int			trap_Key_GetCatcher( void );
void		trap_Key_SetCatcher( int catcher );
int			trap_Key_GetKey( const char *binding );


typedef enum {
  SYSTEM_PRINT,
  CHAT_PRINT,
  TEAMCHAT_PRINT
} q3print_t;


int trap_CIN_PlayCinematic( const char *arg0, int xpos, int ypos, int width, int height, int bits);
e_status trap_CIN_StopCinematic(int handle);
e_status trap_CIN_RunCinematic (int handle);
void trap_CIN_DrawCinematic (int handle);
void trap_CIN_SetExtents (int handle, int x, int y, int w, int h);

int			trap_RealTime(qtime_t *qtime);
void		trap_SnapVector( float *v );

qboolean	trap_loadCamera(const char *name);
void		trap_startCamera(int time);
qboolean	trap_getCameraInfo(int time, vec3_t *origin, vec3_t *angles);

qboolean	trap_GetEntityToken( char *buffer, int bufferSize );

void	CG_ClearParticles (void);
void	CG_AddParticles (void);
void	CG_ParticleSnow (qhandle_t pshader, vec3_t origin, vec3_t origin2, int turb, float range, int snum);
void	CG_ParticleSmoke (qhandle_t pshader, centity_t *cent);
void	CG_AddParticleShrapnel (localEntity_t *le);
void	CG_ParticleSnowFlurry (qhandle_t pshader, centity_t *cent);
void	CG_ParticleBulletDebris (vec3_t	org, vec3_t vel, int duration);
void	CG_ParticleSparks (vec3_t org, vec3_t vel, int duration, float x, float y, float speed);
void	CG_ParticleDust (centity_t *cent, vec3_t origin, vec3_t dir);
void	CG_ParticleMisc (qhandle_t pshader, vec3_t origin, int size, int duration, float alpha);
void	CG_ParticleExplosion (char *animStr, vec3_t origin, vec3_t vel, int duration, int sizeStart, int sizeEnd);
extern qboolean		initparticles;
int CG_NewParticleArea ( int num );



//
// cg_killcam.c (rearmed 0.5): side kill cam, death cam and the best-kill replay.
// The recorder sees every refEntity / light / poly of the main scene: the trap_R_* calls below
// are routed through it (cg_killcam.c and cg_syscalls.c define CG_KC_RAW_TRAPS to get the real ones).
//
#define KCN_NEMESIS		1		// CG_KillCamNotable kinds: the player killed their nemesis (ds-boss)
#define KCN_PHASE		2		// the player broke a boss phase
void CG_KillCamInit( void );						// CG_Init: cvars, shaders, recorder reset
void CG_KillCamBeginFrame( void );					// CG_DrawActiveFrame, after trap_R_ClearScene
void CG_KillCamDraw( void );						// CG_DrawActive, after CG_Draw2D: the side panel
qboolean CG_KillCamServerCommand( const char *cmd );	// rmkc, rmkiller
void CG_KillCamObituary( int target, int attacker, int mod );
void CG_KillCamNotable( int clientNum, int kind );	// nemesis slain, boss phase broken (cg_ds_acts.c)
qboolean CG_KillCamReplayGate( void );				// intermission: qtrue while a full size replay plays (16:9 screens)
qboolean CG_KillCamWantsArea( const vec3_t org );		// qtrue: near a kill cam focus, don't frustum cull (cg_gorekeep.c)
void CG_KC_AddRefEntityToScene( const refEntity_t *re );
void CG_KC_AddPolyToScene( qhandle_t hShader, int numVerts, const polyVert_t *verts );
void CG_KC_AddLightToScene( const vec3_t org, float intensity, float r, float g, float b );
void CG_KC_AddAdditiveLightToScene( const vec3_t org, float intensity, float r, float g, float b );
void CG_KC_RenderScene( const refdef_t *fd );
#ifndef CG_KC_RAW_TRAPS
#define trap_R_AddRefEntityToScene		CG_KC_AddRefEntityToScene
#define trap_R_AddPolyToScene			CG_KC_AddPolyToScene
#define trap_R_AddLightToScene			CG_KC_AddLightToScene
#define trap_R_AddAdditiveLightToScene	CG_KC_AddAdditiveLightToScene
#define trap_R_RenderScene				CG_KC_RenderScene
#endif

// round 4 ENEMIES (cg_enemy.c): the CS_DSENEMY mirror, contract bg_ds_enemy.h
typedef struct {
	int		arch, affix, rank, flags, armorPct;
} cgEnemy_t;
void CG_EN_Frame( void );							// CG_DrawActiveFrame, after CG_ProcessSnapshots
const cgEnemy_t *CG_EN( int clientNum );			// never NULL
int CG_EN_ChangedTime( void );
// round 4 PERSONALITY (cg_personality.c, en-personality): barks, edge callouts, rank tells
void CG_PR_Register( void );						// CG_Init
qboolean CG_PR_ServerCommand( const char *cmd );	// prb / prc
void CG_PR_Draw2D( void );							// CG_Draw2D: right-edge callouts
void CG_PR_ScoreTag( float x, float y, int client, float h, float fade );	// cg_scoreboard.c: rank pips, nemesis tag
void CG_PR_PlayerTell( centity_t *cent, refEntity_t *torso, refEntity_t *head );	// CG_Player: shoulder lights
// round 4 en-brains (cg_thief.c): weapon thieves -- edge callouts (thc), the thief's gun-hand glint
void CG_TH_Register( void );						// CG_Init
qboolean CG_TH_ServerCommand( const char *cmd );	// thc
void CG_TH_Draw2D( void );							// CG_Draw2D: right-edge callout
void CG_TH_PlayerTell( centity_t *cent, refEntity_t *torso );	// CG_Player: gold glint at a thief's gun hand

// round 4 en-affix (cg_ds_affix.c): affix tells on the bots, the champion pack line, frost edges, TAB tags
void CG_DSA_Register( void );						// CG_Init
qboolean CG_DSA_Entity( centity_t *cent );			// CG_General: RM_ENT_AFFIXFX
void CG_DSA_PlayerFX( centity_t *cent );			// CG_Player: the tells around an affixed bot
void CG_DSA_BodyShell( refEntity_t *ent, entityState_t *state );	// CG_AddRefEntityWithPowerups: champion gold, reflect flash
void CG_DSA_Draw2D( void );							// CG_Draw2D: frost edges, the champion pack line
void CG_DSA_ScoreTag( int client, float x, float y, qboolean large, float fade );	// cg_scoreboard.c: affix names under the name
const char *CG_DSA_Names( int mask );				// "FROST VAMPIRIC"
void CG_DSA_Color( int mask, vec4_t out );			// the first affix's colour

// round 4 en-types (cg_ds_types.c): archetype looks, telegraphs, sounds, names at the edges
void CG_DS_TypesRegister( void );					// CG_Init
void CG_DS_TypesPlayer( centity_t *cent, refEntity_t *legs );	// CG_Player after CG_DS_BossScale: juggernaut size, glints, beeps
void CG_DS_TypesTorso( centity_t *cent, refEntity_t *torso );	// CG_Player after the torso: the riot shield
void CG_DS_TypesShell( refEntity_t *ent, entityState_t *state );	// CG_AddRefEntityWithPowerups: mimic / juggernaut / fuse shells
void CG_DS_TypesFootstep( centity_t *cent );		// cg_event.c EV_FOOTSTEP*: juggernaut thuds
qboolean CG_DS_TypesEntity( centity_t *cent );		// CG_General: RM_ENT_DSTYPE
void CG_DS_TypesDraw2D( void );		// en-tune: the run-start type adaptation note (right-edge stack)
const char *CG_DS_TypeName( int client );			// "SNIPER" ("" plain)
const char *CG_DS_TypeTag( int client );			// "  ^1SNIPER" for the TAB row ("" plain)
void CG_DS_TypeColor( int arch, vec4_t out );
