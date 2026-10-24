/*
===========================================================================
REARMED -- DESCENT v2 (0.5.0) game-module API between the parallel parts.
Included at the end of the g_roguelike.c block of g_local.h. Each owner keeps
its own section; the shared data contract is bg_descent.h.
===========================================================================
*/
#ifndef G_DESCENT_H
#define G_DESCENT_H

/*
-------------------------------------------------------------------------------
g_roguelike.c getters (run state)
-------------------------------------------------------------------------------
*/
int G_RL_CardCount( gentity_t *ent, int card );		// how many of a card a client's kit holds (0 outside a run)
qboolean G_RL_HasCard( gentity_t *ent, int card );		// perks and secrets (passives: G_DS_PassiveLevel)
int G_RL_HumanClient( void );							// -1 outside a run / not in yet
int G_RL_Floor( void );									// the floor being played (1-based)
int G_RL_ClientRole( gentity_t *ent );					// kit role: 1 player, 2 bot, 3 elite, 4 boss (0 outside a run)
const char *G_RL_RunGet( const char *key );
void G_RL_RunSet( const char *key, const char *val );
int G_RL_MetaGetInt( const char *key, int def );
const char *G_RL_MetaGet( const char *key );		// en-tune: a meta value as text ("" none)

/*
-------------------------------------------------------------------------------
ARSENAL (g_ds_arsenal.c): weapon levels 1..8 and evolutions
Everything is a no-op / returns neutral values outside DESCENT (G_RL_Active()).
-------------------------------------------------------------------------------
*/
// growth API (LOOT, META, ACTS call these)
void G_DS_AddWeaponXP( gentity_t *ent, int weapon, int xp );		// gem xp -> the weapon; level-ups, flash + sound
void G_DS_AddWeaponLevel( gentity_t *ent, int weapon, int levels );	// cards, chests, starting kits
int G_DS_WeaponLevel( gentity_t *ent, int weapon );				// 1..8 (0 = not a levelling weapon / outside DESCENT)
qboolean G_DS_Evolved( gentity_t *ent, int evo );
qboolean G_DS_CanEvolve( gentity_t *ent, int evo );				// level 8 + key held + not evolved yet
void G_DS_Evolve( gentity_t *ent, int evo );
void G_DS_SetBotWeaponLevels( gentity_t *ent, int floor, int role );	// role = G_RL_ClientRole value
int G_DS_WeaponForMod( int mod );								// meansOfDeath -> weapon_t (0 none)

// run file + CS_DESCENTGROW (LOOT owns the file I/O and the configstring)
void G_DS_ArsenalLoad( const char *wl, const char *wx, int evoMask );	// G_RL_Init: the human's saved growth
void G_DS_ArsenalSave( char *wl, char *wx, int size, int *evoMask );	// floor end: csv strings + mask
void G_DS_ArsenalGrowInfo( char *buf, int size );						// appends \wl\..\wx\..\ev\<mask>
void G_DS_GrowChanged( void );											// (LOOT, g_ds_loot.c) rebuild CS_DESCENTGROW soon

// hooks (called from the shared game files; each returns at once outside DESCENT)
void G_DS_ArsenalThink( gentity_t *ent );			// g_active.c ClientThink_real, before Pmove: fire rate stat, slows, levels
void G_DS_ArsenalClientBegin( gentity_t *ent );	// a client entered the game (bots: their levels)
void G_DS_ArsenalDisconnect( gentity_t *ent );
qboolean G_DS_FireWeapon( gentity_t *ent, vec3_t muzzle, vec3_t forward, vec3_t right, vec3_t up );	// qtrue = the arsenal fired this shot (primary fire)
float G_DS_GauntletRange( gentity_t *ent );					// CheckGauntletAttack reach (32 stock)
int G_DS_ProxArmTime( gentity_t *mine );					// msec until a stuck prox mine arms
void G_RailShot( gentity_t *ent, int damage, int bounces, int pierce );	// g_weapon.c: one rail slug (muzzle / forward set by FireWeapon)
void G_ElementsIgnite( gentity_t *ent, gentity_t *attacker, int msec );	// g_elements.c: set a client on fire
int G_DS_ArsenalDamage( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, int damage, int mod );	// G_Damage scale
void G_DS_ArsenalHit( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, int take, int mod, vec3_t point, vec3_t dir );	// after the hit
qboolean G_DS_MissileImpact( gentity_t *ent, trace_t *trace );	// qtrue = handled (redirected / split)
void G_DS_MissileExplode( gentity_t *ent, vec3_t origin );		// a missile blew up (impact or fuse)
float G_DS_BFGLaserScale( gentity_t *ent );					// BFG ball zap reach multiplier (targets x2 when > 1)
int G_DS_BotWeaponBonus( gentity_t *ent, int weapon );		// ai: how much a bot prefers a levelled weapon
qboolean G_DS_ArsenalCommand( const char *cmd );			// dev: dslevel / dsxp / dsevolve / dsstate

/*
-------------------------------------------------------------------------------
META (g_roguelike.c): characters, upgrades, per-floor stats, damage breakdown
Every call is a no-op / neutral outside DESCENT.
-------------------------------------------------------------------------------
*/
void G_DS_Stat( int stat, int n );					// dsStat_t: add n to this floor's count for the human player
void G_DS_BossDefeated( int boss );				// ACTS: the act boss (dsBoss_t) truly died (last phase)
int G_DS_Character( void );						// dsCharacter_t of the run (CHR_RANGER for old runs, -1 outside)
int G_DS_UpRank( int up );						// dsUpgrade_t rank owned (copied into the run at run start)
float G_DS_UpValue( int up );					// rank * ds_upgrades[up].perRank
void G_DS_MetaDamage( gentity_t *targ, gentity_t *attacker, int dealt, int mod, qboolean headshot );	// G_Damage: breakdown + stats
void G_DS_MetaKill( gentity_t *self, gentity_t *attacker, int mod, qboolean gibbed );	// player_die: kills by weapon, stats
const char *G_DS_ForcedModel( gentity_t *ent );	// ClientUserinfoChanged: the character model for the human, else NULL
// (g_ds_meta.c, called from g_roguelike.c)
void G_DS_MetaInit( void );						// end of G_RL_Init
void G_DS_MetaSpawn( gentity_t *ent );			// G_RL_SpawnLoadout: the character's weapon
qboolean G_DS_MetaRevive( int livesLeft );		// RL_Finish, lost: qtrue = REVIVAL keeps the life
void G_DS_MetaFrame( void );					// G_RL_RunFrame: time held per weapon
void G_DS_MetaFloorEnd( qboolean won );			// RL_Finish, before the run file is saved
int G_DS_KitCards( int numCards );				// bot kit size: the player's cards minus the free character kit
int G_DS_CharHealth( void );					// the character's max health change
float G_DS_CharSpeed( void );					// the character's move speed change (fraction)
qboolean G_DS_MetaCommand( const char *cmd );	// dev: dsstat / dsmeta
// round 4 BESTIARY (en-bestiary, g_ds_bestiary.c; all called from g_ds_meta.c): who the player met, kills per entry
void G_DS_BestiaryInit( void );					// G_DS_MetaInit
void G_DS_BestiaryFrame( void );				// G_DS_MetaFrame, first thing
void G_DS_BestiaryKill( gentity_t *self, gentity_t *attacker );	// G_DS_MetaKill, first thing
void G_DS_BestiaryFloorEnd( void );				// G_DS_MetaFloorEnd: run keys bst_fm / bst_fk / bst_rm
qboolean G_DS_BestiaryCommand( const char *cmd );	// dev: dsbst / dsbstmeet / dsbstkill

/*
-------------------------------------------------------------------------------
ACTS (g_ds_boss.c): acts, biome music, the vault floor, bosses (phases, attacks)
Everything is a no-op / neutral outside a boss or vault floor of a DESCENT.
-------------------------------------------------------------------------------
*/
// node types the game knows (g_roguelike.c RLN_*): 0 fight, 1 elite, 2 boss, 3 event, 4 vault
#define RLN_VAULT_GAME		4

void G_DS_ActsInit( int nodeFloor, int nodeType, int difficulty, int seed );	// G_RL_Init
void G_DS_ActsStart( void );					// G_RL_InitBots (entities spawned): act music
qboolean G_DS_IsVaultFloor( void );
qboolean G_DS_IsBossFloor( void );
int G_DS_BossClient( void );					// the boss's client number, -1 none / not in yet
int G_DS_BossType( void );						// dsBoss_t of this floor, -1 none
int G_DS_Act( void );							// 1..5 (0 outside a run)
const char *G_DS_BossBotName( void );			// the bot file to add ("Sorlag")
const char *G_DS_BossNetName( void );			// its name in game ("Butcher")
void G_DS_BossSpawn( gentity_t *ent );			// G_RL_SpawnLoadout: the boss (re)spawned
void G_DS_ActsFrame( void );					// G_RL_RunFrame
int G_DS_FloorResult( void );					// G_RL_CheckExitRules (boss / vault floors): 0 on, 1 won, 2 lost
void G_DS_ActsPlayerDie( gentity_t *self, gentity_t *attacker );	// G_RL_PlayerDie
int G_DS_BossDamage( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, int take, int mod );	// G_Damage: what really comes off
void G_DS_BossBounds( gentity_t *ent );		// ClientThink_real after Pmove: the boss's bigger hit box
float G_DS_SpeedScale( gentity_t *ent );		// G_RL_GateClient: frenzy / casting
int G_DS_BossHitCap( gentity_t *targ, gentity_t *attacker, int damage );	// G_Damage: no boss one-shots
void G_DS_BotInput( int client, usercmd_t *ucmd );	// ai_main.c: the boss stands still while it casts
qboolean G_DS_BotAllies( int a, int b );		// boss floors: the boss and its adds are one side
qboolean G_DS_BotPriority( int bot, int client );	// boss floors: the rivals go for the boss (~65% of the time)
qboolean G_DS_BossHuntSpot( int client, vec3_t out );	// ai: the boss's walk-to goal when nobody is in sight
qboolean G_DS_ActsCommand( const char *cmd );	// dev: dsattack / dsphase / dsbossinfo / dskillboss
// round 2 (ds-boss): 1v1 boss floors with g_dsBossLives, archetype traits, the sim player, the nemesis
float G_DS_BossProgress( void );				// 0..1 of the whole boss bar removed this floor (0 off a boss floor)
// round 4 (en-boss): g_dsBossMode 0 = timed 1v1 boss floors (only the player and the boss)
qboolean G_DS_BossTimed( void );				// a timed 1v1 boss floor
qboolean G_RL_Practice( void );				// g_roguelike.c: a boss practice floor (g_dsPractice): nothing is saved
// round 4 (en-daily, g_ds_daily.c): the DAILY CHALLENGE (bg_ds_daily.h): own run file, no meta / skills / tally / ghost
qboolean G_RL_NoSave( void );					// G_RL_Practice() || G_RL_Daily(): nothing of the player's is written
qboolean G_RL_Daily( void );					// a floor of the daily challenge (cvar ds_daily at G_RL_Init)
void G_DS_DailyInit( void );					// G_RL_Init, first
int G_DS_DailyRand( void );						// 0..0x7fff: the daily's seeded chest stream (rand() off the daily)
float G_DS_DailyRandom( void );					// 0..1, same stream
qboolean G_DS_DailyFloorEnd( qboolean won, int styleRank, int kills, int msec, int floorNum, qboolean boss );	// RL_Finish: qtrue = the daily is complete
int G_DS_BossTimeLeft( void );					// msec on the boss clock (-1 off a timed boss floor)
int G_DS_BossTimeBonus( void );					// skulls for the time left at the kill (before the multiplier)
float G_DS_ActsDamageScale( gentity_t *targ, gentity_t *attacker );	// G_RL_DamageScale: boss damage side, nemesis
void G_DS_ActsPlayerSpawn( gentity_t *ent );	// G_RL_SpawnLoadout, last: boss-floor respawn spot, guns, grace
void G_DS_ActsDirty( void );					// rebuild CS_DESCENTBOSS soon
int G_DS_SimClient( void );						// g_dsBossSim: the bot playing the human's part, -1 none
qboolean G_RL_IsPlayer( gentity_t *ent );		// (g_roguelike.c) the human, or the g_dsBossSim bot
// the nemesis (g_ds_nemesis.c): run keys nemesis_*
void G_DS_NemesisInit( void );					// G_DS_ActsInit
void G_DS_NemesisFrame( void );					// G_DS_ActsFrame
void G_DS_NemesisDeath( gentity_t *self, gentity_t *attacker );	// G_RL_PlayerDie
const char *G_DS_NemesisPlanName( int nodeType );	// G_RL_InitBots: the nemesis's bot name for this floor, NULL none
float G_DS_NemesisDamageScale( gentity_t *targ, gentity_t *attacker );	// its single-digit edge
void G_DS_NemesisInfo( char *buf, int size );	// CS_DESCENTBOSS on fight floors: \nem\<client>
qboolean G_DS_NemesisCommand( const char *cmd );	// dev: dsnemesis <name> [kills] | clear
int G_DS_NemesisClient( void );					// -1 none on this floor

/*
-------------------------------------------------------------------------------
LOOT (g_ds_loot.c): gems, the vacuum, chests + the jackpot, passives, the v2 card pool,
CS_DESCENTGROW and the run keys pv / chests / gems / wused.
Everything is a no-op / neutral outside DESCENT.
-------------------------------------------------------------------------------
*/
void G_DS_SpawnGems( vec3_t origin, int xp, int weaponHint );	// a spray worth ~xp (weaponHint <= 0: the held weapon)
void G_DS_SpawnGemFloat( vec3_t origin, int xp );			// one gem that hangs in place (the vault's RUSH / SKY)
gentity_t *G_DS_SpawnChest( vec3_t origin, int quality );		// 0 normal, 1 elite, 2 boss
void G_DS_Vacuum( gentity_t *player );							// every gem on the map flies to the player
int G_DS_PassiveLevel( gentity_t *ent, int passive );			// 0..5
int G_DS_LuckPct( void );										// the player's luck bonus in percent
float G_DS_PowerScore( gentity_t *ent );						// 1.0 = a fresh run; weapons, passives, perks, health
																// (enemies are matched to the player's: bots 0.95x, elites 1.1x)
int G_RL_AddSkulls( int n );									// (g_roguelike.c) run skulls += n, returns the total

// hooks (shared files)
void G_DS_LootReset( void );						// G_RL_Init, first
void G_DS_LootInit( void );							// G_RL_Init, after the run is read
void G_DS_LootFrame( void );						// G_RL_RunFrame
void G_DS_LootSaveRun( void );						// RL_WriteRunCommon
void G_DS_LootWriteConfigstring( void );
void G_DS_LootPlayerDie( gentity_t *self, gentity_t *attacker, int mod );	// G_RearmPlayerDie
float G_DS_LootDamageScale( gentity_t *targ, gentity_t *attacker );		// G_RL_DamageScale
int G_DS_LootLethal( gentity_t *targ, gentity_t *attacker, int take );		// G_Damage: PENTAGRAM
float G_DS_LootSpeedBonus( gentity_t *ent );		// G_RL_GateClient: QUICKSILVER
int G_DS_LootHealthBonus( gentity_t *ent );			// G_RL_MaxHealthBonus: HOLLOW HEART
int G_DS_LootAmmoPickup( gentity_t *ent, int quantity );	// G_RL_AmmoPickup: AMMO BELT
int G_DS_MaxAmmo( gentity_t *ent, int base );		// Add_Ammo: AMMO BELT
int G_DS_LootSkullPct( void );						// RL_Finish: GRAVE COIN
void G_DS_LootFloorEnd( qboolean won );				// RL_Finish, before the offer: opens the chests
int G_DS_LootChestsOpened( void );
int G_DS_LootChestMsec( void );
qboolean G_DS_LootChestDone( void );
qboolean G_DS_LootCardAvailable( int role, const int *counts, int card );	// RL_CardAvailable, v2 kinds + stats
int G_DS_LootCardWeight( int card, int pickNum );	// RL_CardWeight, v2 kinds
void G_DS_LootFillKit( int *counts );				// the player's passive card counts = levels
int G_DS_LootBotPassiveTarget( int role );			// RL_BuildBotKit: passive levels a bot should hold
void G_DS_LootPickCard( gentity_t *ent, int card );	// RL_Pick
qboolean G_DS_LootClientCommand( gentity_t *ent, const char *cmd );	// dschestdone
qboolean G_DS_LootConsoleCommand( const char *cmd );	// dev: dsgems dsscatter dschest dsvacuum dspassive dsused dsloot

/*
-------------------------------------------------------------------------------
ECONOMY (g_ds_econ.c, ds-economy): difficulty reward multiplier, floor skulls,
records difficulty, the standard-mode frag target multiplier (g_fragMult).
-------------------------------------------------------------------------------
*/
float G_DS_RewardMult( void );						// the current run difficulty's multiplier (skulls, gem xp, skill xp); 1.0 outside DESCENT
int G_DS_Skulls( int n );							// n skulls x G_DS_RewardMult, rounded, >= 1 when n > 0 (pass to G_RL_AddSkulls)
int G_DS_Difficulty( void );						// the current run difficulty 1..5 (3 outside DESCENT)
int G_DS_RecordDifficulty( void );					// the difficulty records go to: the lowest a floor was played on
int G_DS_FloorSkulls( qboolean won, int levelKills, int styleRank, int nodeType, int nodeFloor, int bonusPct, int *parts );	// RL_Finish
void G_DS_EconConfigstring( char *buf, int size );	// G_RL_UpdateConfigstring: appends \rm\<mult x100>\bd\<boss %>\bs\<boss skulls>
qboolean G_DS_EconCommand( const char *cmd );		// dev: dsecon [sim]
int G_FragLimit( void );
void G_DS_EconOtherSkulls( int n );				// G_RL_AddSkulls: tally for the run summary (sk_ot)
int G_DS_PlayerBossLives( int base );				// ACTS: the player's boss-floor lives (+ LAST BREATH)							// the frag target of a match: fraglimit x g_fragMult outside DESCENT

/*
-------------------------------------------------------------------------------
SKILLS (g_ds_skills.c, ds-skills): skill xp in every mode vs bots, perk effects in
DESCENT only (every hook returns at once / neutral otherwise). Contract: bg_ds_skills.h.
-------------------------------------------------------------------------------
*/
void G_DS_SkillsInit( void );						// G_RearmInit, after G_RL_Init
void G_DS_SkillsFrame( void );						// G_RunFrame
void G_DS_SkillsSave( void );						// LogExit, G_ShutdownGame: pending xp -> rearmed_skills.txt
void G_DS_SkillEvent( gentity_t *ent, int event, int parm );	// ClientEvents
int G_DS_SkillDamage( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, int damage, int mod, int dflags, int hitFlags );	// G_Damage scale, -1 = no hit
int G_DS_SkillHit( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, int take, int asave, int mod, int hitFlags );	// G_Damage: xp, CHEAT DEATH
qboolean G_DS_SkillForceGib( gentity_t *self, gentity_t *attacker, int mod );	// player_die
void G_DS_SkillKill( gentity_t *self, gentity_t *attacker, int mod, qboolean gibbed );	// player_die
void G_DS_SkillGate( gentity_t *ent );				// G_RL_GateClient
int G_DS_SkillFirePct( gentity_t *ent, int w );		// G_DS_ArsenalThink: refire percent faster
int G_DS_SkillFireFlags( gentity_t *ent, int w );	// G_DS_ArsenalThink: DSF_ bits
float G_DS_SkillAreaBonus( gentity_t *ent, int w );	// Ars_AreaScale
float G_DS_SkillSpeedBonus( gentity_t *ent, int w );	// Ars_SpeedScale
float G_DS_SkillGauntletScale( gentity_t *ent );	// G_DS_GauntletRange
void G_DS_SkillOnFire( gentity_t *ent );			// FireWeapon
int G_DS_SkillMaxHealth( gentity_t *ent );			// G_RL_MaxHealthBonus
void G_DS_SkillSpawn( gentity_t *ent );				// G_RL_SpawnLoadout
int G_DS_SkillStyle( gentity_t *ent, int points );	// G_StyleAdd
qboolean G_DS_SkillsCommand( const char *cmd );		// dev: dsskills / dsskillxp / dsperk / dsskillsave

/*
-------------------------------------------------------------------------------
INTRO (ds-intro, g_ds_intro.c): the floor's opponents in CS_DSINTRO for the loading screen
and the versus intro (cgame/cg_ds_intro.c)
-------------------------------------------------------------------------------
*/
void G_DS_IntroPublish( void );						// end of G_RL_InitBots ("" outside a run)
int G_RL_PlanNum( void );							// (g_roguelike.c) this floor's planned bots
const char *G_RL_PlanName( int i, int *role );		// its name and kit role (2 bot, 3 elite, 4 boss)
int G_RL_PlanCards( int i, int kind );				// different cards of an RLK_ kind it holds (-1 all)
int G_DS_BotLevelPreview( int floor, int role );	// (g_ds_arsenal.c) the weapon level its bots will get

/*
ARRIVALS (ds-arrivals, g_ds_arrive.c, g_dsStagger): the floor's bots join one at a time
*/
void G_DS_ArrivePlan( void );						// G_DS_IntroPublish: the order, the start count, CS_DSARRIVE
qboolean G_DS_ArriveAtStart( int planIndex );		// G_RL_RunFrame: added with the first ones (qtrue when not staggered)
void G_DS_ArriveBegin( void );						// G_RL_RunFrame: the start bots were added
void G_DS_ArriveFrame( void );						// G_RL_RunFrame: warnings, late joiners
void G_DS_ArriveSpawn( gentity_t *ent );			// G_RL_SpawnLoadout, last: the arrival spot, the head start
int G_DS_ArrivalsLeft( void );						// planned bots not in yet (0: everyone, or not staggered)
int G_DS_ArrivalLastAt( void );						// level time of the last arrival (0 none)
void G_RL_AddPlanBot( int planIndex );				// (g_roguelike.c) addbot for a planned bot

/*
-------------------------------------------------------------------------------
FRENZY (round 3, ds-frenzy, g_ds_frenzy.c): kill-chain frenzy, lucky crits, power spikes,
loot beams. Switches g_dsFrenzy / g_dsCrits / g_dsSpikes / g_dsBeams. Every call is a no-op /
neutral outside DESCENT or with its switch off. Constants (DS_BEAM_*, DS_SPIKE_*): bg_descent.h.
-------------------------------------------------------------------------------
*/
// hooks (shared files)
void G_DS_FrenzyInit( void );						// G_RL_Init (every map): reset, cvars
void G_DS_FrenzyFrame( void );						// G_RL_RunFrame: chain timer, spikes, beams
void G_DS_FrenzyFloorEnd( void );					// RL_Finish: cash the chain out, end a spike (once per floor)
void G_DS_FrenzyKill( gentity_t *self, gentity_t *attacker, int mod );	// player_die, before G_RearmPlayerDie (the gem drop)
int G_DS_FrenzyDamage( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, int damage, int mod, int *hitFlags, vec3_t point );	// G_Damage: crits, berserk
void G_DS_FrenzyGate( gentity_t *ent );				// G_RL_GateClient, last: overdrive speed / dash / zip
int G_DS_FrenzyFirePct( gentity_t *ent, int pct );	// G_DS_ArsenalThink: berserk refire percent
float G_DS_FrenzyGemMult( void );					// LOOT: gem xp multiplier of the current chain tier (1.0 off)
void G_DS_FrenzyGemXP( int extra );				// LOOT: extra gem xp the multiplier added (cash-out tally)
float G_DS_FrenzyPullScale( void );					// LOOT: gem pull radius multiplier
float G_DS_FrenzyFlightScale( void );				// LOOT: gem flight time multiplier (< 1 = faster)
qboolean G_DS_FrenzyCommand( const char *cmd );		// dev: dsfrenzy / dscrit / dsspike / dsbeam / dsfzstate
// API for the other workers
int G_DS_FrenzyBestChain( void );					// the human's best kill chain this floor (valid until the next map)
int G_DS_FrenzyChainXP( void );						// extra xp frenzy paid this floor (gem bonus + cash-outs)
int G_DS_FrenzyTier( void );						// the current chain tier 0..DS_FRENZY_TIERS
void G_DS_LootBeam( gentity_t *ent, int rarity );	// a coloured light pillar on an entity (follows it, ends with it)
gentity_t *G_DS_BeamAt( vec3_t origin, int rarity, int msec );	// a standalone beam (msec 0: until G_DS_LootBeamOff)
void G_DS_LootBeamOff( gentity_t *ent );			// the beam on ent (or ent itself if it is a beam) goes away
void G_DS_GrantSpike( gentity_t *player, int kind );	// DS_SPIKE_BERSERK / DS_SPIKE_OVERDRIVE now (the human only)
gentity_t *G_DS_SpawnPowerSpike( vec3_t origin, int kind );	// a spike pickup with its beam (the human takes it)
int G_DS_FrenzySpikeExtend( gentity_t *ent, int msec );	// push a running spike's end back (capped), returns msec added
int G_DS_SpikeActive( gentity_t *ent );				// DS_SPIKE_ kind running on ent, 0 none

/*
-------------------------------------------------------------------------------
CARDS (round 3, ds-cards, g_ds_cards.c): legendary cards (offer, effects), the floor-end
tally (server command dsctally, the RLP_END gate), legendary HUD state (dsclg). Every hook is
a no-op / neutral outside DESCENT or for anyone but the player. Constants (LG_*, LGX_*,
RLK_LEGEND, RLRAR_LEGEND): bg_descent.h / bg_public.h.
-------------------------------------------------------------------------------
*/
void G_DS_CardsInit( void );						// G_RL_Init (every map), after the run is read
void G_DS_CardsFrame( void );						// G_RL_RunFrame: echoes, orbs, graves, the agent, HUD state
void G_DS_CardsOffer( int *offer, int numOffer, int nodeType, int floor, int luckPct );	// RL_MakePlayerOffer: maybe one legendary
void G_DS_CardsFloorEnd( qboolean won, int levelKills, int styleRank, int reward );	// RL_Finish, after G_DS_MetaFloorEnd: the tally, records
qboolean G_DS_CardsTallyReady( void );				// G_RL_CheckExitRules RLP_END (won): the client finished the tally
void G_DS_CardsPicked( int card );					// RL_Pick: a legendary taken (log, owed offer cleared)
int G_DS_CardsDamage( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, int damage, int dflags, int mod, int *hitFlags );	// G_Damage: -1 = no hit (MIRROR, the agent)
int G_DS_CardsHit( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, int take, int mod );	// G_Damage, last: PHOENIX, VAMPIRE LORD
void G_DS_CardsKill( gentity_t *self, gentity_t *attacker, int mod, qboolean gibbed );	// player_die
void G_DS_CardsFired( gentity_t *ent );				// g_active.c after FireWeapon: ECHO
void G_DS_CardsGem( gentity_t *player );			// g_ds_loot.c a gem collected: MAGNETAR
qboolean G_DS_CardsAllies( int a, int b );			// ai BotSameTeam: DOUBLE AGENT and the player
float G_DS_CardsPower( gentity_t *ent );			// G_DS_PowerScore: legendaries held (enemies scale)
qboolean G_DS_CardsClientCommand( gentity_t *ent, const char *cmd );	// dsctallydone
qboolean G_DS_CardsCommand( const char *cmd );		// dev: dsoffer / dstally / dslegend / dslgstate
// API for the other workers
void G_DS_CardsGrantLegendary( void );				// ds-secrets: the next won floor's offer holds a legendary (run key lg_owed)

/*
-------------------------------------------------------------------------------
SURPRISES (round 3, ds-surprise, g_ds_surprise.c): mid-floor ambush mini-bosses, the floor
modifier (run key node_mod), wild events, the ghost of a past run (rearmed_ghost.txt).
Switches g_dsAmbush / g_dsModifiers / g_dsWildEvents / g_dsGhosts. Every hook is a no-op /
neutral outside a DESCENT fight floor. Contract: bg_ds_surprise.h.
-------------------------------------------------------------------------------
*/
void G_DS_SurpriseInit( void );						// G_RL_Init, last: the floor's plan, the ghost snapshot
void G_DS_SurprisePreload( void );					// G_RL_InitBots, before G_PreloadPublish: planned bots' models
void G_DS_SurpriseFrame( void );					// G_RL_RunFrame
void G_DS_SurpriseSpawn( gentity_t *ent );			// G_RL_SpawnLoadout, last: placement, armour, kits
void G_DS_SurpriseDeath( gentity_t *self, gentity_t *attacker );	// G_RL_PlayerDie
int G_DS_SurpriseHealthBonus( gentity_t *ent, int bonus );	// G_RL_MaxHealthBonus: + this (GLASS, mini-boss, fodder)
float G_DS_SurpriseDamageScale( gentity_t *targ, gentity_t *attacker );	// G_RL_DamageScale
float G_DS_SurpriseSpeedScale( gentity_t *ent );	// G_RL_GateClient
qboolean G_DS_SurpriseExtra( int client );			// an ambusher / ghost / fodder bot: out of the frag race
qboolean G_DS_SurpriseNoScore( gentity_t *self );	// player_die: killing it is no frag
int G_DS_SurpriseOfferBonus( void );				// RL_MakePlayerOffer: FERAL +1 card
void G_DS_SurpriseFloorEnd( qboolean won );			// RL_Finish, before G_DS_LootFloorEnd: BLOODLUST chest, run keys
int G_DS_SurpriseModSkulls( int floorSkulls );		// G_DS_FloorSkulls (won): the modifier's skulls
float G_DS_SurpriseGemXpMult( void );				// LOOT gem xp: BLACKOUT
int G_DS_SurpriseGemAmount( int xp );				// LOOT G_DS_SpawnGems: SWARM
void G_DS_SurpriseBotInput( int client, usercmd_t *ucmd );	// ai_main.c: casting mini-boss, dead extras
qboolean G_DS_SurpriseHuntSpot( int client, vec3_t out );	// ai BotRearmHuntGoal
qboolean G_DS_SurprisePriority( int bot, int client );	// ai BotRearmPriorityTarget: the ghost wants the player
qboolean G_DS_SurpriseAggressive( int client );		// ai BotRearmAggressive
qboolean G_DS_SurpriseCommand( const char *cmd );	// dev: dsambush / dsevent / dsghost / dsmod / dssurprise
int G_DS_SurpriseMod( void );						// the floor modifier (DSMOD_), 0 none
// (g_roguelike.c) a bot that joins mid-floor with a kit planned here (cards NULL: a regular kit of numCards)
void G_RL_AddExtraBot( const char *botFile, const char *netName, float skill, int role, const int *cards, int numCards, const int *passives );
// (g_ds_loot.c) a chest straight into the bank (opened at the floor end)
void G_DS_BankChest( int quality );
// (g_ds_arsenal.c) a bot's exact weapon levels (DS_NUM_WEAPONS, 0 = keep) and evolutions
void G_DS_SetWeaponLevels( gentity_t *ent, const int *levels, int evoMask );

/*
-------------------------------------------------------------------------------
SECRETS (round 3, ds-secrets, g_ds_secrets.c): secret stashes at hidden spots found from
the AAS data (cache rearmed_secrets_<map>.txt), CS_DSSECRETS, DSST_STASHES. g_dsSecrets.
-------------------------------------------------------------------------------
*/
void G_DS_SecretsInit( void );						// G_RL_Init, first (every map, every mode)
void G_DS_SecretsFrame( void );						// G_RunFrame (every mode: the dev commands scan outside a run)
qboolean G_DS_SecretsCommand( const char *cmd );	// dev: dssecrets list|show|hide|tp|scan|load|pick, dsstash

/*
-------------------------------------------------------------------------------
MAPS (round 3, ds-maps, g_ds_maps.c): CTF / Team Arena maps as FFA arenas, the floor's layout
variant (node_variant, g_dsVariants), CS_DESCENT "mv". Contract: bg_ds_maps.h.
-------------------------------------------------------------------------------
*/
void G_DSM_SpawnVars( gentity_t *ent );			// g_spawn.c: every map entity, fields parsed
void G_DSM_PostSpawn( void );						// g_main.c: right after G_SpawnEntitiesFromString
void G_DSM_Configstring( char *buf, int size );	// G_RL_UpdateConfigstring: appends \mv\<variant>

// round 4 ENEMIES shared state (g_enemy.c, contract bg_ds_enemy.h): each worker writes its own fields
typedef struct {
	int		arch;			// dsArchetype_t (en-types), published
	int		affix;			// DSAF_ mask (en-affix), published
	int		rank;			// 0..DS_MAX_RANK (en-personality), published
	int		flags;			// DSEF_ (see bg_ds_enemy.h for who sets what), published
	int		armorPct;		// 0..100 armour left vs its spawn armour (en-feel cracks), published
	int		grudge;			// en-personality: client it hunts (-1 / 0 none: use DSEF_GRUDGE)
	int		grudgeUntil;	// level.time
	float	morale;			// en-feel: 1 fine .. 0 broken (DSEF_PANIC under the threshold)
	int		staggerUntil;	// en-feel: level.time
	int		telegraphUntil;	// en-types / en-affix: level.time a wound-up attack lands
	int		spare[8];		// for the workers: claim one in docs/DESCENT_V2.md Round 4 before use
} gEnemy_t;
extern gEnemy_t	g_enemy[MAX_CLIENTS];
void G_EN_Init( void );								// G_InitGame, after G_DSM_PostSpawn
void G_EN_Clear( int clientNum );					// ClientDisconnect
gEnemy_t *G_EN( int clientNum );					// NULL out of range
void G_EN_SetFlag( int clientNum, int flag, qboolean on );
qboolean G_EN_HasFlag( int clientNum, int flag );
void G_EN_MarkDirty( void );						// after writing arch / affix / rank / armorPct
void G_EN_CleanName( const char *in, char *out, int size );	// Q_strncpyz + Q_CleanStr (names in lines / logs)
void G_EN_Publish( void );							// G_RunFrame, after G_RL_RunFrame

// round 4 HIT FEEL (en-feel, g_hitfeel.c, bg_hitfeel.h): stagger, dismember, crawl, morale, splats, screams
float G_HF_KnockbackScale( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, int mod, int dflags );	// G_Damage
void G_HF_Damage( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, vec3_t dir, vec3_t point,
				 int take, int asave, int dflags, int mod, int hitFlags );	// G_Damage, before the health drops
void G_HF_Die( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int mod, int damage );	// player_die, after G_GoreOnDeath
qboolean G_HF_WallSplat( gentity_t *victim, gentity_t *attacker, float impact );	// g_physics.c G_WallSplat: qfalse = none
void G_HF_ClientThink( gentity_t *ent, usercmd_t *cmd );	// ClientThink_real, on pm.cmd before Pmove
qboolean G_HF_Command( const char *cmd );				// dev: hftest

/*
-------------------------------------------------------------------------------
round 4 AFFIXES (en-affix, g_ds_affix.c): elite affixes, champion packs of 3, split copies.
g_dsAffixes / g_dsChampions. DESCENT only, never on boss floors. Contract bg_ds_affix.h.
-------------------------------------------------------------------------------
*/
void G_DS_AffixInit( void );						// G_InitGame, after G_EN_Init
void G_DS_AffixPreload( void );					// G_RL_InitBots, before G_PreloadPublish: the champion pack's models
int G_DS_AffixPlanMask( int planIndex );			// ds-intro: the affixes a planned bot will carry (0 none)
void G_DS_AffixGrant( int client, int mask );		// en-personality: OR affixes into a live bot (nemesis levels)
void G_DS_AffixSpawn( gentity_t *ent );			// G_RL_SpawnLoadout: assign / place / one-life checks
int G_DS_AffixSpentNow( int client );				// en-bestiary: affix bits this death spent this frame (SPLIT)
void G_DS_AffixFrame( void );						// G_RunFrame, before G_EN_Publish
void G_DS_AffixDeath( gentity_t *self, gentity_t *attacker );	// G_RL_PlayerDie, first
void G_DS_AffixHit( gentity_t *targ, gentity_t *attacker, int take, int mod );	// G_Damage, right before health comes off
float G_DS_AffixDamageScale( gentity_t *targ, gentity_t *attacker );	// G_RL_DamageScale
int G_DS_AffixHealthBonus( gentity_t *ent, int bonus );	// G_RL_MaxHealthBonus (added, can be negative)
float G_DS_AffixSpeedScale( gentity_t *ent );		// G_RL speed line: FROST slow
qboolean G_DS_AffixExtra( int client );			// a split copy / champion: one life, out of the frag race
qboolean G_DS_AffixNoSelfBurn( gentity_t *patchOwner, gentity_t *other );	// g_elements.c: a BLAZING bot walks its own trail
void G_DS_AffixBotInput( int client, usercmd_t *ucmd );	// ai_main.c: dead extras stay down
qboolean G_DS_AffixHuntSpot( int client, vec3_t out );	// ai_rearmed.c: champions / copies go for the player
qboolean G_DS_AffixCommand( const char *cmd );		// dev: dsaffix / dschampions / dsaffixstate

/*
-------------------------------------------------------------------------------
round 4 PERSONALITY (en-personality): taunts, grudges, telegraph shouts (game/ai_taunt.c, all
modes; g_botTaunts / g_botGrudges / g_botShouts), veterans (game/g_ds_rank.c, DESCENT,
g_dsVeterans), nemesis levels (g_ds_nemesis.c, g_dsNemesisLevels). Contract bg_ds_personality.h.
-------------------------------------------------------------------------------
*/
void G_PR_Init( void );								// G_InitGame, after G_EN_Init
void G_PR_Frame( void );							// G_RunFrame, before G_EN_Publish
void G_PR_Death( gentity_t *self, gentity_t *attacker, int mod );	// player_die, before G_RearmPlayerDie
int G_EN_GrudgeTarget( int botClient );				// en-brains: the live client this bot hunts, -1 none
void G_PR_Say( int client, int prio, const char *line, int bark );	// a rate-limited chat line (+ bark, -1 none)
void G_PR_SayTo( int client, int prio, const char *line, int bark, int target );	// "%s" = target's name
void G_PR_Bark( int client, int bark );				// a voice bark only (prBark_t)
qboolean G_PR_StockChatOff( void );					// ai_chat.c: the stock bot chat is replaced
qboolean G_PR_Command( const char *cmd );			// dev: prstat / prsay / prgrudge / dsvet
void G_DS_RankInit( void );							// G_PR_Init
void G_DS_RankFrame( void );						// G_PR_Frame: ranks onto g_enemy[].rank
int G_DS_RankFor( gentity_t *ent );					// a bot's veteran rank (0 outside DESCENT / off)
int G_DS_RankOfName( const char *name );
void G_DS_RankOrderNames( const char **names, int *idx, int num, int floor );	// G_RL_InitBots: returning veterans first
int G_DS_RankKitBonus( const char *name );			// G_RL_InitBots: extra kit cards
int G_DS_RankHealthBonus( gentity_t *ent );			// G_RL_MaxHealthBonus (rank + nemesis level)
int G_DS_RankWeaponBonus( gentity_t *ent );			// g_ds_arsenal.c: bot weapon levels (rank + nemesis level)
float G_DS_RankDamageScale( gentity_t *targ, gentity_t *attacker );	// via G_DS_NemesisDamageScale
void G_DS_RankSpawn( gentity_t *ent );				// end of G_RL_SpawnLoadout: armour, arrival lines
void G_DS_RankKill( gentity_t *self, gentity_t *attacker );	// G_PR_Death: xp
void G_DS_RankFloorEnd( qboolean won );				// RL_Finish, before the run file is saved
void G_DS_RankIntro( char *s, int size );			// G_DS_IntroPublish: \vr\ \vm\ \nl\ \nt\ keys
qboolean G_DS_RankCommand( const char *cmd );		// dev: dsvet
int G_DS_NemesisLevel( void );						// 1..6, 0 no nemesis
const char *G_DS_NemesisTitle( void );				// "the Relentless" or ""
qboolean G_DS_NemesisIs( gentity_t *ent );			// by name (works before the client is found)
int G_DS_NemesisHealthBonus( gentity_t *ent );
int G_DS_NemesisWeaponBonus( gentity_t *ent );
void G_DS_NemesisSpawn( gentity_t *ent );			// G_DS_RankSpawn: level armour, affixes, DSEF_NEMESIS

/*
-------------------------------------------------------------------------------
BOT MEMORY + NEMESIS HALL OF FAME (round 4, en-memory, game/g_ds_botmem.c, g_botMemory): lifetime
records per bot name in rearmed_bots.txt (contract bg_ds_botmem.h), written only at the floor end;
never on daily / practice floors.
-------------------------------------------------------------------------------
*/
void G_BM_RegisterCvars( void );					// G_PR_Init (all modes): g_botMemory
void G_BM_Init( void );								// G_DS_NemesisInit: load the records
void G_BM_Frame( void );							// G_PR_Frame: greetings, lifetime grudges, panic lines
void G_BM_Death( gentity_t *self, gentity_t *attacker );	// G_RL_PlayerDie (play phase): k / d / headshots / revenges
const char *G_BM_KillLine( int bot );				// G_PR_Death: a bot killed the player, a record line or NULL
const char *G_BM_DeathLine( int bot );				// G_PR_Death: the player killed this bot, a record line or NULL
qboolean G_BM_OwnsArrival( gentity_t *ent );		// Vet_Arrive: memory greets this bot instead
float G_BM_Fear( int client );						// ai_tactics.c: 1 = it fears the player
void G_BM_NemesisRise( const char *name, int kills, const char *oldName );	// g_ds_nemesis.c
void G_BM_NemesisUpdate( const char *name, int lvl, const char *title, int kills );
void G_BM_NemesisSlain( const char *name, int lvl, int kills, int mod );
void G_BM_FloorEnd( qboolean won, qboolean runOver );	// RL_Finish, before the run file is saved
qboolean G_BM_Command( const char *cmd );			// dev: dsbm

/*
-------------------------------------------------------------------------------
ARCHETYPES (round 4, en-types, g_ds_types.c + ai_types.c): DESCENT fight floors only, g_dsTypes.
Table + constants: bg_ds_types.h. The type itself: g_enemy[client].arch (DSAR_*).
-------------------------------------------------------------------------------
*/
void G_DS_TypesInit( void );						// G_RL_Init, after G_DS_SurpriseInit
void G_DS_TypesPlan( void );						// G_RL_InitBots, before the preload list goes out
void G_DS_TypesSpawn( gentity_t *ent );				// G_RL_SpawnLoadout, after the other spawn hooks
void G_DS_TypesFrame( void );						// G_RL_RunFrame
void G_DS_TypesDeath( gentity_t *self, gentity_t *attacker );	// G_RL_PlayerDie: kamikaze pop
void G_DS_TypesDisconnect( int client );			// G_RL_ClientDisconnect
void G_DS_TypesFloorEnd( void );					// RL_Finish: the evidence counters to the log
int G_DS_TypesHealthBonus( gentity_t *ent, int bonus );	// G_RL_MaxHealthBonus
float G_DS_TypesDamageScale( gentity_t *targ, gentity_t *attacker );	// G_RL_DamageScale
float G_DS_TypesSpeedScale( gentity_t *ent );		// G_RL_GateClient
int G_DS_TypesDamage( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, int damage, int dflags, int mod, vec3_t point );	// G_Damage: the shield's front
const char *G_DS_TypeModel( gentity_t *ent );		// ClientUserinfoChanged: a typed bot's model, NULL own
void G_DS_TypesBounds( gentity_t *ent );			// en-tune: ClientThink_real after Pmove: the juggernaut's hit box at its drawn size
float G_DS_TypesHeadLine( gentity_t *targ );		// en-tune: g_combat.c headshot line (z above the origin)
qboolean G_DS_TypeAdapted( int arch );			// en-tune: this run's type adaptation (g_dsTypeAdapt)
int G_DS_TypesPlanArch( int planIndex );			// ds-intro: the type of a planned bot
qboolean G_DS_TypesCommand( const char *cmd );		// dev: dstypes / dstype / dstypeact / dstypestats
qboolean G_DS_TypesOn( void );
qboolean G_DS_TypesRaised( int client );			// a skeleton a summoner raised (fodder, arch 0)
// ai_types.c -> g_ds_types.c
qboolean G_DS_TypesOtherTelegraph( int client );	// another worker's wind-up runs on this bot
qboolean G_DS_TypesBlink( int client, int enemy, qboolean force );
int G_DS_TypesRaiseCorpse( int client );			// a corpse it may raise now, -1 none
qboolean G_DS_TypesRitual( int client, int corpse );
qboolean G_DS_TypesInRitual( int client );
int G_DS_TypesPatient( int client );				// the medic's patient, -1 none
int G_DS_TypesFuse( int client );					// the kamikaze's blast time, 0 none
void G_DS_TypesSnipe( int client, int target, float dist );
void G_DS_TypesTelegraph( int client, int msec );	// DSEF_TELEGRAPH + telegraphUntil (0 = off)
// (g_ds_surprise.c) a summoned skeleton: fodder netName lands at spot after warnMsec
qboolean G_DS_SurpriseRaise( const char *netName, const vec3_t spot, float yaw, int warnMsec );

/*
-------------------------------------------------------------------------------
round 4 BRAINS (en-brains, all modes): bot tactics game/ai_tactics*.c + ai_habits.c (bot API in
game/ai_tactics.h), weapon thieves game/g_thief.c (g_botWeaponThief).
-------------------------------------------------------------------------------
*/
qboolean BotTacticsCommand( const char *cmd );		// dev: btstats / btreset
void G_TH_Init( void );								// G_InitGame, after G_EN_Init
void G_TH_Frame( void );							// G_RunFrame, before G_EN_Publish
void G_TH_PlayerDie( gentity_t *self, gentity_t *attacker, int mod );	// player_die (before the body / drops go)
qboolean G_TH_Pickup( gentity_t *item, gentity_t *other );	// Touch_Item, before the pickup: qtrue = handled
qboolean G_TH_Command( const char *cmd );			// dev: btthief
int G_TH_Thief( int botClient );					// the weapon a bot stole (WP_), 0 none
int G_TH_PrizeOwner( gentity_t *item );			// ai: the human a dropped prize weapon came from, -1 = not a prize

// round 4 en-rematch: BOSS REMATCH (game/g_ds_rematch.c, contract game/bg_ds_rematch.h): a practice floor + modifiers
qboolean G_DS_Rematch( void );					// this practice floor is a rematch
float G_DS_RmHealthScale( void );				// IRON: x the boss pool
int G_DS_RmTimeLimit( int limit );				// HALF CLOCK: the timed limit (msec)
float G_DS_RmPace( void );						// FURY: x scripted attack gaps and cooldowns
qboolean G_DS_RmEnraged( void );				// ENRAGED: the last-minute enrage from the start
qboolean G_DS_RmFallen( int playerDeaths );		// ONE LIFE: the player died: time-up path
qboolean G_DS_RmItemAllowed( gitem_t *item );	// NO HEALTH: no health items
int G_DS_RmHealthBonus( gentity_t *ent, int bonus );	// GLASS: the human's health limit halved (G_RL_MaxHealthBonus)
float G_DS_RmPowerScore( gentity_t *ent );		// Acts_PowerScale: GLASS doesn't shrink the boss
void G_DS_RematchInfo( char *s, int size );		// CS_DESCENTBOSS rm / rmh / rmp
qboolean G_DS_RematchEnd( int result );			// RLP_DONE: "dsrmend" to the UI (qtrue = it was a rematch)

#endif // G_DESCENT_H
