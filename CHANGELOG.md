# REARMED changelog

## 0.6.1-beta (2026-10-24)

Playtest fixes and a new vault.
- **Bosses are fairer.** The timed pools are 3-4x smaller (the Butcher had 3049 hp a phase; the calibration came from a sim bot that out-damages a human about 3x), boss damage is 15-20% lower, and **one boss hit never takes more than 55% of your max health** (no more point-blank shotgun / grenade one-shots). Fast bosses are 1.10x speed (was 1.22x); the frenzy and last-minute speed-ups are halved. `g_dsBossScale` still scales health and damage.
- **Boss floors are on small maps** (any act's small map, keeping the act's look; a medium one only when every small map was played this run). Floor-map sim: 73 of 80 boss floors small, none large (was about 1 in 10 small).
- **Boss locator:** a diamond over the boss with the distance, through walls; off screen, a chevron around the crosshair points the way.
- **Vault modes:** each vault plays one of four (from the run seed): THE VAULT (gem rain), SHOOTING GALLERY (shoot floating crystals, they burst into gems), RUSH (gems appear ahead of you; x1 running, x2 fast, x4 blazing), SKY VAULT (columns of floating gems, richer the higher). `g_dsVaultMode 0-3` forces one.
- **Release fixes (clean-install test):** a fresh install no longer opens on the CD KEY screen (`ui_cdkeychecked` is read-only, so the launcher's `+set` never skipped it; ioquake3 needs no key), the launcher writes `qconsole.log` (`logfile 2`) and the zip has `TESTERS.txt` (what to test, which logs to send).

## 0.6.0-beta (2026-10-13)

### Round 4: difficulty tuning, types that remember you, the juggernaut's hit box (en-tune)
- **Bot tactics by difficulty:** a low-skill gate on top of en-brains' skill scale (`BotTacGate`, ai_tactics.c): below skill 4 a bot uses each tactic only in some 12-22 s windows (skill 1 never, 2 a third of the time, 3 two thirds, 4-5 always) and plays stock the rest. Before, the fixed parts of each chance (dodge 25%, heal 20%, hearing 20%, flank 25% ...) applied at every skill, so skill-1 bots retreated to heal about as often as skill-4 bots (1.04 vs 1.26 a bot-minute) and grudge-hunted 0.76 vs 1.14. Hearing radius is now 0.4 + 0.6 x scale (was 0.55 + 0.45). Grudge targeting scales with the gate (skill 1: none). Skill 4-5 unchanged.
- **Measured** (`tools/tune_sweep.sh` + `tools/tune_table.py`): hidden bot-only FFA, 5 min at timescale 2 on q3dm6 / q3dm7 / q3dm13; client 0 is a fixed stand-in "player" (sarge, skill 3, stock AI, tactics off: `bt_split 3`), four opponents at the skill under test, tactics off vs on; `bt_bench 1` logs time-to-kill. Before = en-brains' build, after = the gate. 24-48 matches a cell before, 24 after; +- = bootstrap standard error.

  | Bot skill (DESCENT difficulty, floors 1-6) | Before: stand-in K/D, tactics off / on | After: stand-in K/D, off / on | After: stand-in deaths/min off / on | After: TTK in s off / on |
  |---|---|---|---|---|
  | 1 (I Can Win) | 7.9 +-1.1 / 5.9 +-0.6 (-24%) | 8.5 +-1.5 / 8.0 +-1.9 (-6%) | 0.38 / 0.47 | 3.5 / 1.8 |
  | 2 (Bring It On) | 2.05 +-0.25 / 2.15 +-0.23 (+5%) | 2.43 +-0.30 / 2.52 +-0.36 (+4%) | 0.95 / 0.96 | 1.3 / 1.6 |
  | 3 (Hurt Me Plenty) | 0.98 +-0.07 / 1.03 +-0.09 (+4%) | 0.86 +-0.13 / 0.88 +-0.11 (+2%) | 1.63 / 1.59 | 1.9 / 1.8 |
  | 4 (Hardcore) | 0.48 +-0.07 / 0.49 +-0.09 (+2%) | 0.50 +-0.06 / 0.52 +-0.06 (+4%) | 2.14 / 2.04 | 1.7 / 1.8 |
  | 5 (Nightmare) | 0.23 +-0.04 / 0.30 +-0.04 (+29%) | 0.27 +-0.03 / 0.25 +-0.04 (-8%) | 2.72 / 2.54 | 2.5 / 2.1 |

  The stand-in wins comfortably at skill 1-2 (K/D 8 and 2.4) with or without the tactics; against one stock player in a 5-bot FFA the tactics don't move its K/D beyond the noise at any skill (two identical tactics-off batches differ by ~15%). What the gate fixes is how much of the tactics low-skill bots use (per opponent bot per minute):

  | Skill | dodge+juke before / after | retreat | hear+track | item contest | rj chase + strafe hop | habits | grudge hunt |
  |---|---|---|---|---|---|---|---|
  | 1 | 0.03 / 0 | 1.04 / 0 | 1.52 / 0 | 0.30 / 0 | 0.74 / 0 | 1.13 / 0 | 0.76 / 0 |
  | 2 | 0.07 / 0.04 | 0.96 / 0.34 | 2.82 / 0.62 | 0.49 / 0.15 | 1.21 / 0.23 | 1.72 / 0.58 | 0.70 / 0.22 |
  | 3 | 0.13 / 0.11 | 1.27 / 0.87 | 4.14 / 3.11 | 0.93 / 0.71 | 2.42 / 1.73 | 3.13 / 1.89 | 0.95 / 0.63 |
  | 4 | 0.56 / 0.49 | 1.26 / 1.40 | 8.79 / 7.87 | 1.85 / 1.67 | 5.21 / 4.45 | 6.14 / 5.85 | 1.14 / 1.13 |
  | 5 | 0.36 / 0.41 | 1.62 / 1.38 | 7.63 / 8.17 | 1.95 / 1.99 | 4.86 / 4.39 | 6.28 / 6.50 | 1.35 / 1.21 |

  DESCENT difficulty = bot skill on floors 1-6 (+0.12 a floor after, elites +0.5, bosses +1), so I Can Win reaches skill 3.2 by floor 24 and its gate opens gradually (0 -> 0.72).
- **Types that remember you** (`g_dsTypeAdapt`, console, default 1): deaths by killer archetype per floor (run key `tad_f`, written at floor end), folded into the meta once per floor by the menu (`tad_cur`, `tad_w` x10, `tad_seed`); each new run rolls weight = weight x 0.5 + the last run's deaths, and the top 1-2 types with weight 3+ adapt: sniper glint 0.56 s (cgame laser ramp follows) and idle perch 5 s, rusher full flanking + side charge, medic further back, shield +-70 degrees, kamikaze pairs (once per floor), blinker 3.6 s, summoner 7 s, juggernaut rockets from 600 units; all adapted types x1.5 pick weight and x0.95 health (budget). A right-edge note at the run's start (cg_edge.c slot EDGE_TYPES): "THE SNIPERS REMEMBER YOU / Their glint is shorter. Strafe early." Not in the daily or boss practice. Serverinfo `g_dsTypeAdaptNow` tells cgame. Dev `dstadapt`; tests `tools/tune_adapt.py`, `tools/tune_adapt_ui.sh`.
- **Juggernaut hit box = its drawn size** (1.25x from the feet): after Pmove the linked box is scaled standing, crouched and dead (the body queue copies it), headshots start at the drawn head (`G_DS_TypesHeadLine`, g_combat.c). The movement hull stays the player hull, so doors, jump pads and AAS routes are untouched; the box narrows to the normal width next to walls / players (tall-only) and is plain on movers or someone's head. Dev `dsjugtrace`, `g_dsJuggBox 0`, `g_dsTypesDebug 2` (JuggMove); tests `tools/tune_jugg.py trace|move`.
  Logged traces (`tools/tune_jugg.py trace`, q3dm17, MASK_SHOT from 120 units): standing, the old box hit 1 of 5 points on the drawn body (chest only; head +38, head top +44, both shoulder edges at 17 units out missed), the new box 5 of 5 with head and head top as HEADSHOT; the two points outside the drawn model (24 out, 52 up) miss with both. Corpse: old 1 of 5 (the top and both sides of the lying body missed), new 5 of 5. Movement (`tune_jugg.py move`, every bot a juggernaut, 3 min, 10 act maps: q3dm6/7/8/11/12/13/17, q3tourney2, pro-q3dm6, mptourney2, big box vs plain): mean distance per 5 s 367-576 vs 305-537, "stuck" samples (under 40 units on the ground, includes standing to fight) 1-15% vs 0-20% with no map consistently worse except q3dm7 (12-13% vs 1-7%: four juggernauts block each other in its corridors with the wider boxes); the box went tall-only in 7-19% of frames and plain in 0-1%.
- **Code:** game/ai_tactics.c (gate, hearing, grudge scale, `bt_split 3`, `bt_bench`), ai_tactics.h; game/g_ds_types.c, ai_types.c, bg_ds_types.c/.h, cgame/cg_ds_types.c; small hooks in g_active.c (G_DS_TypesBounds), g_combat.c (headshot line), g_roguelike.c + g_descent.h (`G_RL_MetaGet`), q3_ui/ui_ds_meta.c (DSM_TypeAdaptMerge), cgame/cg_draw.c + cg_local.h. Tools: `tools/bt_sim.py --standin` (+ `BT_PK3` snapshot), `tools/tune_sweep.sh`, `tools/tune_table.py`, `tools/tune_jugg.py`, `tools/tune_adapt.py`, `tools/tune_adapt_ui.sh`.

### Round 4: Daily Challenge (en-daily)
- **DESCENT > DAILY CHALLENGE**: one fixed descent per local date, the same for everyone who plays that day (seed = a hash of YYYYMMDD from the PC clock). Act 1 only: 6 floors, floor 6 is THE BUTCHER as the **timed 1v1 at 5:00**, always, whatever `g_dsBossMode` / `g_dsBossTime` say. Hurt Me Plenty, 2 lives, the day's character (any of the 8 but ORBB, lent even if locked) with its kit plus a free starting ability from the seed. **No permanent upgrades, no skill perks, no skulls**: a pure challenge. Unlimited attempts, the best one counts.
- **Score:** 1,000 a floor won (the boss 3,000), 20 a kill (lost floors too), 150 x the floor's best style rank, speed (won floors: 4 a second under 4:00; the boss: 10 a second left on its clock), no-death (250 a won floor without dying, +1,500 for a flawless clear). One number plus that breakdown on the **DAILY RESULT** screen (TRY AGAIN / DONE); the in-game floor-end banner says THE ATTEMPT ENDS / THE DAILY IS CLEARED and shows no skulls.
- **The daily screen:** today's date and seed, the character, ability and rules, **the six floors as the first floor map shows them** (node types, maps, pick-your-poison modifiers, the boss), and PERSONAL BESTS: today's best and attempts, today's target (beat your best, else the score that enters your top 10), the streak (days in a row with a finished attempt) and the all-time top 10. START / CONTINUE (floor N, lives left) / NEW ATTEMPT (scores the running one as it stands). Mouse, WASD / Tab, Space, gamepad. The hub got a 7th button (buttons 40 high now) with today's best, tries and streak.
- **Seeded, so it repeats:** the floor plan, maps and variants (a blank lifetime profile, so your own map history doesn't steer it), node types and modifiers, the character and ability, card offers (run seed + floor, as before), legendary rolls, enemy types and affixes, champion packs, ambushes and wild events (all run seed + floor), and now the **chest rolls** on a daily floor (a stream of run seed + floor in `G_DS_DailyRand`, independent of what you did before). Not deterministic: bot AI, physics, aim and timing, so kills, drops (gems, vacuums, luck chests) and fights differ; card offers also depend on the cards you already took, and maps after floor 1 on the path you pick (never a map twice).
- **Your descent is never touched:** while the daily is on (cvar `ds_daily`), the UI and the game read and write `rearmed_daily_run.txt` instead of `rearmed_run.txt`; meta reads give a blank profile and meta / history / skills / tally / ghost / style records and mastery are never written (the game logs "Descent: daily, rearmed_meta.txt not written"). Personal bests live in `rearmed_daily.txt`. Both new files are in RESET ALL PROGRESS (backups `rearmed_backup_<n>_dailyrun / _daily.txt`). Nothing goes over the network. The bestiary records nothing on a daily (en-bestiary's call).
- **Code:** `game/bg_ds_daily.h` (contract), `game/g_ds_daily.c`, `q3_ui/ui_ds_daily.c`; small hooks in g_roguelike.c (run file name, RL_Save gate, blank meta, floor records `dly_r<n>` / `dly_n` / `dly_done`, the 6th floor ends the run, no skulls, CS_DESCENT `\dly\`), g_ds_boss.c (always timed 300 s), g_ds_loot.c (chest stream), g_ds_skills.c / g_ds_surprise.c / g_ds_cards.c (no saves, no perks, no ghost), cg_roguelike.c + cg_style.c (banners, no records), ui_roguelike.c (files, MainMenuOpened, commands, no difficulty switch on the daily map), ui_ds_meta.c (hub button, OpenRun, history), ui_ds_maps.c (`UI_DSM_Preview`), ui_ds_econ.c (reset list).
- **Dev / tests:** `set ds_dailyDate YYYYMMDD` fakes the date; `dsdaily [start]`, `dsdailyinfo [date]`, `dsdailypb`, `dsdailyscore`; `tools/daily_test.sh <cfg> [W H]` (home_test/daily, cfgs in `tools/daily_cfgs/`: daily_plan, daily_play, daily_result, daily_boss).

### Round 4: elite affixes and champion packs (en-affix)
- **Eight Diablo-style affixes** on DESCENT elites, ambush mini-bosses, champion packs and the levelled nemesis: VAMPIRIC (heals 30% of damage dealt), EXPLOSIVE (the corpse swells 1.3 s, then blows), SPLITTING (first death: two half-health copies, one life each), REFLECTING (20% back, never lethal), FROST (hits slow you to 70% for 1.3 s), BLAZING (fire-patch trail), MAGNETIC (0.9 s telegraphed yank toward it, break line of sight to snap it), REGENERATING (5%/s out of combat). 1 in act 1, 2 in acts 2-3, 3 in act 4 / the Abyss, seeded by the run.
- **Tells on the enemy, not over it:** red mist, a swelling core, orbiting violet shards, chrome glints and flash, falling ice crystals, embers, teal motes + tether, mint motes; generated sounds per affix (`sound/rearmed/affix/`). A frosty screen-edge tint while you're slowed. No overhead text.
- **Champion packs** (`g_dsChampions`): 14-30% of fight / elite floors, three gold pillars side by side, three champions land together sharing one affix set, one life each, out of the frag race; a right-edge line counts them down. Each kill pays gems + a skull, the last a chest, a gold beam and skulls.
- **Names at the edges:** the elite's affixes under its portrait (loading roster, arrival strip), the ambush landing callout, under the name on TAB.
- **Power:** affixed bots give up 2-11% health and damage (threat weights), copies x0.5 health / x0.6 damage, champions x0.9 / x0.8; explosion damage scales with your power score. Rewards: +6 gems and +1 skull per affix, 2+ affixes roll an extra chest.
- Switches on F10 > BOTS: `g_dsAffixes`, `g_dsChampions`. Dev: `dsaffix`, `dschampions`, `dsaffixstate`. Test: `tools/affix_test.sh` (+ `tools/affix_fixtures/gen.py`). IDs: RM_ENT_AFFIXFX 84, CS_DSAFFIX 744. API for the other parts: `G_DS_AffixGrant` (nemesis affixes), `G_DS_AffixExtra`, `CG_DSA_Names` / `CG_DSA_Color`.

### Round 4: smarter bots, every mode (en-brains)
- **Flank and pincer:** 2+ bots on one target: the closest holds, the others take an AAS route to a spot 320-540 units round your side (pincer 110-160 degrees, a third bot the other side, a fourth behind; behind a raised shield). Chases split the same way.
- **Retreat to heal:** a bot that is losing (health < 40, or < 65 with < 15 armour against an enemy 30+ healthier) goes for the nearest health / armour within ~4.5 s that isn't past you, then comes back (chase) for 6 s once topped up.
- **Dodging:** missile sidestep / splash hop without the dash (reaction 140-420 ms by skill, 25-85% notice chance), rail jukes when a railgun is lined up on the bot and ready to fire. Stays out of the existing dash dodge's way.
- **Rocket / strafe jumps:** skilled bots take botlib's rocket-jump routes more (health 60+ and health + armour 110+) and rocket-jump after an enemy on a ledge 100-300 up; strafe-jump hops on clear straights out of combat.
- **Item timing:** the big items (quad, battlesuit, red armour, mega health) are timed; bots leave to arrive 1.5-4 s early, two at most per item. Bots only go looking for sounds with 50+ health and a real gun.
- **Hunting by sound:** gunfire (350-2000 units by weapon), footsteps (420), jumps, jump pads (1100), landings and pain cries; skill scales the radius, out of PVS muffles it. Idle bots investigate, chasing bots follow the noise.
- **Habits** (per match; DESCENT: run key `bt_hab` keeps weapon + range for the run, hot spots per floor): favourite weapon, engagement range, hot spots / entrances; counter weapons, a counter range, pre-aim.
- **Leader focus** (at most half the bots, 8-14 s windows, target distance x0.5), **grudge** targeting (distance x0.4-0.75 by skill) + hunting (en-personality's `G_EN_GrudgeTarget`), **barrel lures** (the existing prop shot springs them), **panic / crawl** movement for en-feel's states.
- **Weapon thieves** (`g_botWeaponThief`, all modes but Gun Game / Instagib / OITC / Weapon Roulette): the human's dropped gun (DESCENT: a copy) becomes a prize bots race for; the thief switches to it (DESCENT: the human's level, capped at its own + 2), wears DSEF_THIEF (a gold glint at the gun hand) and the human gets a right-edge "STOLEN" callout. Killing it yourself = RECLAIMED: the gun drops back, DESCENT pays `G_DS_Skulls(1 + level / 3)` skulls + a 6 + 3 x level gem spray, other modes 80 style. Log lines ThiefDrop / Thief / Reclaim / ThiefDown.
- Skill scaling: 0.05 + 0.95 t^2 with t = (skill - 1) / 3: skill 2 = 0.16, 3 = 0.47, 4-5 = 1. Archetypes scale each tactic through `BotTypeTactic`. A DESCENT boss only dodges.
- **Switches:** F10 > BOTS `g_botTactics`, `g_botFlank`, `g_botDodge`, `g_botRetreat`, `g_botHabits`, `g_botWeaponThief`; console `g_botHearing`, `g_botItemTiming`, `g_botRocketJump`, `g_botLeader`, `g_botLure`.
- **Code:** game/ai_tactics.c, ai_tactics_move.c, ai_habits.c, ai_tactics.h (+ the ai_types.c stubs en-types took over), game/g_thief.c, cgame/cg_thief.c. Hooks: ai_main.c (frame, input), ai_dmq3.c (think, weapon, retreat / chase / rocket-jump wishes, attack move, enemy choice), ai_dmnet.c (long-term goal, chase goal), g_main.c, g_combat.c, g_items.c, g_svcmds.c, cg_main.c, cg_servercmds.c, cg_draw.c, cg_players.c, ui_rearmed.c; DSEF_THIEF in bg_ds_enemy.h.
- **Dev / tests:** `bt_debug 1-3`, `btstats`, `btreset`, `btthief [give|killby|reclaimfx <bot>]`, `bt_split`, `bt_habitsTarget`, `bt_thiefTarget`; `tools/bt_sim.py` (hidden bot-only dedicated matches, A/B split by client parity, `--skill0` for a strong stand-in human); screenshot cfg `home_test/rearmed/bt_thief.cfg`.
- **Measured** (6 skill-4 bots, q3dm6 / q3dm7, 5 min, timescale 2): half the bots with tactics vs half stock (bt_split 1 and 2 to cancel the client order), 8 matches after tuning: tactics bots 235 kills / 208 deaths (K/D 1.13), stock 189 / 259 (0.73); score 224 vs 175. Rocket-splash deaths 30 vs 61. Before the tuning (16 matches) the tactics half was more passive (K/D 0.89 vs 0.94): retreats and item waits were too eager, grudges too single-minded. All-on vs all-off (4 + 4): the same pace (57 deaths a match) but rocket-splash deaths 52 -> 22 and a tighter scoreboard (winners 11-16 vs 12-21). Per 5-min match: ~50 sound tracks, 15-20 investigations, 10-20 retreats (5-10 healed), 4-14 rail jukes, 5-11 missile sidesteps, 30-150 strafe hops, 1-5 rocket jumps, 0-6 flanks with six equal bots and 12 when bots gang up on a dominant player, 10-60 item contests, 2-4 thefts.

### Round 4: hit feel, every mode (en-feel)
- **Flinch** (`cg_hitFlinch`): bodies kick away from each hit's direction (EV_GORE_HIT), a stagger kicks harder and wobbles.
- **Stagger** (`g_stagger`): rails, direct rockets, charged alt-fires, headshots and 60+ hits take a bot's trigger for 0.4-0.9 s and make its aim drift (DSEF_STAGGER, `g_enemy[].staggerUntil`), 0.9 s lockout after one. Juggernaut: big hits only, half as long, x0.35 knockback.
- **Heads, helmets, limbs** (`g_dismember`): heavy neck kills decapitate (on top of g_gore's headshot decapitation), arms tear off on heavy arm hits (alive or on the kill), blasts take the legs; heavy headshots knock an armoured bot's helmet off. New Blender gibs (arm, leg, helmet) join cg_gore's persistent gib ring; stumps bleed; bodies keep their wounds (entityState.time2 HFB_ bits, copied to corpses). Arm-less skins for the models whose arms are separate surfaces, a shoulder stump on the rest; legless bodies drop the legs model.
- **Crawling** (`g_crawl`): at 15 health or less (30%) or legless: forced crouch, body pitched down, blood trail (DSEF_CRAWL); legless bots bleed out (35 health cap, -2/s down to 8).
- **Morale and panic** (`g_morale`): `g_enemy[].morale` drops on hits, limb loss, crawling, gibbed allies in sight, lost duels, armour breaks and wall splats; under 0.25: DSEF_PANIC for 2.5-4 s, a scream, the gun falls out of the hand for 1.2 s; `g_enemy[].spare[2]` = the client that broke it + 1 (en-brains flees from it).
- **Wall splats** (`g_wallSplat`): switch for the existing splat, plus a stagger, a morale hit and a body thud; big direct hits shove bots x1.25.
- **Armour cracks** (`cg_armorCracks`): `g_enemy[].armorPct` (armour left vs the most this life, 10% steps) is now published; cracks + a pulsing hot rim under 80 / 50 / 25%, sparks on armoured hits, a shatter of plates when it breaks.
- **Death screams** (`cg_deathScreams`): 48 generated screams (6 kinds of death x 4 voices x 2 takes) + panic wails, crawl groans, thuds, shatters, a helmet clang, limb tears; all synthesized (`tools/make_sounds_hitfeel.py`).
- **Code:** game/g_hitfeel.c, cgame/cg_hitfeel.c, game/bg_hitfeel.h (EV_HITFEEL kinds, body bits, scream kinds); hooks in g_combat.c (knockback scale, G_HF_Damage, G_HF_Die), g_active.c (G_HF_ClientThink on pm.cmd), g_physics.c (G_WallSplat), g_svcmds.c (`hftest`), bg_public.h / bg_misc.c (EV_HITFEEL), cg_event.c, cg_players.c, cg_physics.c, cg_gore.c (CG_GoreLimbGib / CG_GoreBloodBurst / CG_GoreTrailMark), ui_rearmed.c (HITS tab). Assets: `tools/make_hitfeel_assets.py`, `tools/blender/make_hitfeel.py`.

### Round 4: enemy archetypes (en-types)
- **Nine bot types on DESCENT fight floors** (`g_enemy[].arch`, DSAR_*), each with its own model, weapon and tells on the body, never text over its head:
  - **SNIPER** (visor, railgun): holds a high perch with a long sightline; every rail shot comes after a **0.7 s red laser + head glint** aimed at its target (fire held, DSEF_TELEGRAPH, a rising whine); it moves to another perch after each shot.
  - **RUSHER** (slash, shotgun, 1.18x speed): charges in and circles close, never retreats.
  - **MEDIC** (lucy, weak machinegun): a **green heal beam** (12 hp/s) on the most hurt ally bot within 650 units, a green back light; stays behind its patient, away from the enemy. Kill it first.
  - **SHIELD** (keel + a generated riot shield model): the raised energy shield blocks **80% of direct hits from its front 120 degrees** (cyan sparks, a clang); splash and hits from the sides / back go through. It lowers the shield (swung to its side) for 1-1.4 s to fire its plasma gun, then raises it for 2-3 s and advances.
  - **KAMIKAZE** (red orbb, gauntlet only, 1.25x speed, half health): runs at you **beeping faster the closer it gets**; within 170 units the fuse lights (frantic beeps, a hot orange glow, 1 s), then an 85-damage blast (radius 250). Killed first it **pops where it stands** (60 damage, radius 200), which can catch other bots.
  - **BLINKER** (mynx, lightning gun): short flanking teleports every 4.5-7 s to the enemy's side or back, magenta flash out and in. **A blink never ends within 400 units of the player** (the spot search asks for 480).
  - **SUMMONER** (ranger/wrack, grenades): walks to a corpse 650+ units from you and stands through a **1.5 s raise ritual** (a pale ring and pillar on the corpse, a chant), then a skeleton (uprising fodder) climbs out of a portal there. At most 2 of its own and 3 on the floor, 9 s apart.
  - **MIMIC** (your own model, inked dark): carries a copy of **your best weapon at your level for it**.
  - **JUGGERNAUT** (tankjr at 1.25x, gunmetal shell): 1.8x health + 50 armour, 0.78x speed, chaingun (rockets at range), ground-shaking footsteps; en-feel only staggers it from big hits.
- **Power budget:** a type changes how a bot fights, not how much (health x damage roughly 0.5-1.2 of a plain bot before its trick; table in `game/bg_ds_types.c`). The floor's tier, kits and the G_DS_PowerScore scaling still apply on top.
- **Floor mix by act** (seeded by the run seed + floor, so the Daily Descent and quitting don't re-roll it): act 1 has 1-2 typed bots among plain ones (sniper, rusher, shield, kamikaze); act 2 about half (+ medic, blinker, juggernaut); act 3 two thirds (+ summoner, mimic) with combos (a medic brings a shield or juggernaut, a summoner kamikazes, a sniper a shield); act 4 / the Abyss all but one. Elites carry a type 30 / 60 / 85 / 100% of the time by act; act 2+ ambush mini-bosses may get a shield or blinks (they keep their own look). Never on boss or vault floors.
- **Names at the edges only:** the arrival strip / versus intro reads "SNIPER - ANGEL" (CS_DSINTRO `t<i>`), the TAB row ends in the type in its colour.
- **Switch:** F10 > BOTS `g_dsTypes`. Dev: `g_dsTypesForce "sniper,medic,..."` (or `dstypes <list>` / `dstypes clear`), `dstype <client> <type>`, `dstypeact <client|type> blink|fuse|raise|tell|shieldup|shielddown`, `dstypestats`, `dstypesim <floor> <bots> [runs] [elite]` (the planner's mix over seeds); `g_dsTypesDebug 1` logs every shield hit, heal tick, blink, fuse, pop, ritual and a DSTypesStats line; `g_dsTypesShots N` (tests) turns your view to a tell and screenshots it.
- **Code:** `game/bg_ds_types.[ch]` (table, RM_ENT_DSTYPE 86), `game/g_ds_types.c`, `game/ai_types.c` (en-brains' hooks), `cgame/cg_ds_types.c`; hooks in g_roguelike.c, g_combat.c (the shield), g_client.c (models), g_preload.c, g_ds_intro.c, g_ds_surprise.c (`G_DS_SurpriseRaise`), cg_players.c, cg_ents.c, cg_event.c, cg_scoreboard.c, cg_ds_intro.c, cg_main.c, ui_rearmed.c. Assets: `tools/make_sounds_types.py`, `tools/make_types_gfx.py`, `tools/blender/make_shield.py` (also `build_all.sh shield`). Test: `tools/types_test.sh` (+ `tools/types_fixtures/gen.py`).

### Round 4: the Bestiary (en-bestiary)
- **BESTIARY tab in the Collection** (10th tab): the 9 archetypes (SNIPER ... JUGGERNAUT), the 8 elite affixes (VAMPIRIC, EXPLOSIVE, SPLITTING, REFLECTING, FROST, BLAZING, MAGNETIC, REGENERATING), the CHAMPION PACK, the 5 ambush mini-bosses and the 4 act bosses: 27 entries. Every entry shows "???" until you **meet** it: a bot carrying it comes into your view on your floor, or hurts you. Then it shows its kind, your lifetime kills, the date you first met it, what it does and **HOW TO BEAT IT** (e.g. SHIELD: "Shoot its back or splash its feet, or hit it the moment the shield drops"). Tiles are coloured by kind: solid squares for archetypes, rings for affixes, a gold double frame for champions, portraits for bosses, with kills in the corner. Bosses you beat before 0.6 count as met and show their old defeat counts. Same keys as the rest of the Collection: mouse, WASD, Tab / Shift-Tab, Q / E, gamepad shoulders and d-pad.
- **NEW IN BESTIARY** callout: the first meeting ever slides a small two-line note in at the **left edge** of the HUD (never the centre), in the entry's colour, up to four stacked, 3.6 s each, with a soft click. Switch `cg_dsBestiaryNote` (F10 > BOTS); the bestiary records either way.
- **Data:** nothing touches rearmed_meta.txt during a floor. At floor end the game writes `bst_fm` (met this floor), `bst_fk` (kills per entry) and `bst_rm` (met this run, so later floors never repeat a callout) into the run file next to `fst`, and the UI merges them once per floor (with the META floor merge) into new meta keys: `bst_seen` / `bst_aseen` / `bst_xseen` (masks), `bst_k` / `bst_ak` / `bst_xk` (kills), `bst_d` / `bst_ad` / `bst_xd` (first-met dates). Reset All Progress empties the meta file, so these go with it. Daily Challenge floors record nothing.
- **Code:** `game/bg_ds_bestiary.[ch]` (shared names, colours, tips; names match en-types' and en-affix's callouts), `game/g_ds_bestiary.c`, `cgame/cg_ds_bestiary.c`, `q3_ui/ui_ds_bestiary.c`; hooks in g_ds_meta.c (init / frame / kill / floor end / dev), g_descent.h, cg_servercmds.c (`dsbst <entry>`), cg_draw.c, cg_local.h, ui_ds_meta.c (tab + merge), ui_ds_meta.h, ui_rearmed.c.
- **Dev:** `dsbst` (state + which bots carry what), `dsbstmeet <entry|name>`, `dsbstkill <entry|name> [n]`; games.log lines `Bestiary: met <name> ... by sight|hurt|kill|dev [- NEW]`. Test cfgs `tools/bestiary_fixtures/bestiary_game.cfg` and `bestiary_hurt.cfg` (run with `CFG=... tools/test_meta.sh W H <fixture dir>`).

### Round 4: enemy personality: taunts, grudges, nemesis levels, veterans (en-personality)
- **Trash talk** (`g_botTaunts` Off / Some / Lots, every mode, F10 > BOTS): bots talk in the chat corner with a short positional voice bark (34 generated barks, male / female by the bot's model: `tools/make_taunt_voices.py`, sound/rearmed/taunt/). Events: they fragged you (barrel and gauntlet kills have their own lines), you died to a barrel / a fall / yourself (the nearest bot), you're on a 5 / 10 / 15 streak, revenge (on you nearly always, between bots now and then), their morale breaks (en-feel's DSEF_PANIC), a veteran returns (DESCENT), the nemesis's own lines. Every line is queued with a 0.6-1.8 s "typing" delay and passes a global gap, a per-bot cooldown and a per-minute cap (Some 20 s / 60 s / 3, Lots 7 s / 25 s / 6; revenge-on-you and nemesis lines may cut in after 3 s, cap + 1). The stock Q3 bot chat (ai_chat.c) is replaced: it also made bots stand still to type.
- **Grudges** (`g_botGrudges` Off / 40 s / 90 s, every mode): a bot killed by anyone holds a grudge (g_enemy[].grudge / grudgeUntil, DSEF_GRUDGE); `G_EN_GrudgeTarget()` for en-brains, whose bots prefer and hunt that target. The revenge kill ends it. DESCENT: your own revenge kill on the bot that last killed you pays 1 skull (x difficulty, 3 a floor), REVENGE at the right edge.
- **Attack shouts** (`g_botShouts`): the rising edge of DSEF_TELEGRAPH (en-types / en-affix wind-ups) plays a shout from the bot: kamikaze "For glory!", summoner "Rise!", pull affix "Get over here!", others "Here it comes!" / "Eat this!"; the sniper stays silent. 6 s per bot, 1.2 s between any two.
- **Nemesis levels** (`g_dsNemesisLevels`, DESCENT, g_ds_nemesis.c): level 1 at the title, +1 every further time it kills you (max 6, run keys nemesis_level / _title / _affix). Damage dealt 1.05 + 0.03 x L (+8% .. +23%), taken 0.95 - 0.02 x L, +10 health / +15 armour per level past 1, weapons +1 level at L2 / +2 at L4, an affix at L3 and L5 via en-affix's `G_DS_AffixGrant` (vampiric / regen / frost / fire trail / reflect), a title from L2 ("Visor the Bonebreaker"). Kill pay 3 + floor/4 + 2 x (L-1) skulls, gems + 10 x (L-1). Right-edge callouts when it arrives and on each level up (held while you're dead / under TAB, so they show when you're back); CS_DESCENTBOSS gets \nl\ \nt\; its lines go through the taunt limiter.
- **Veteran bots** (`g_dsVeterans`, DESCENT, g_ds_rank.c): a run roster in rearmed_run.txt (`vet_roster Name.xp.rank.met.kills.promo,...`; a new run and Reset All Progress clear it). +2 xp per floor on it, +3 per kill of you, +1 per bot kill (3 a floor); ranks I-V at 4 / 9 / 15 / 22 / 30 xp, decided at the floor end. Per rank +4 health, +8 spawn armour, +2% damage (nemesis + rank capped at +30%); weapons +1 level from rank III; +1 kit card at II, +2 at IV. Up to 2 returning veterans are moved to the front of each fight floor's name list (never boss floors). Returning bots recognise you ("We meet again.", promoted: "Moved up since last time." + VETERAN II callout).
- **Where ranks show** (never centre, no overhead text): rank pips on the versus strip / loading roster portraits and notes ("PROMOTED  VETERAN I", "LV3 NEMESIS  THE BONEBREAKER"; CS_DSINTRO \vr\ \vm\ \nl\ \nt\), pips + "NEMESIS LV3" after the name on TAB, a small shoulder light (bronze / silver / gold, a pulsing red one for the nemesis; `cg_prTells` dev switch).
- **Measured** (tools/pers_test.sh, isolated home home_test/pers, timescale 2): q3dm17 FFA, 5 bots, the player idling, 4 game minutes: Some 3 lines (0.75/min, 1 dropped by the limiter), Lots 9 (2.25/min); 27-32 grudges, 4-5 bot revenges, 137-141 GrudgePick and 2-3 GrudgeHunt (en-brains). Nemesis: 5 scripted deaths took Visor from level 1 to 6 ("Visor the Bonebreaker", +FIRE TRAIL at 3, +REFLECTS at 5), its kill paid 13 skulls + 92 gems on floor 2; your revenge on Doom paid 1 skull. Veterans across two floors: floor 2 ended with `vet_roster Anarki.14.2.1.3.1,Hossman.5.1.1.0.1,Doom.8.1.1.1.1,Cadavre.13.2.1.3.1`, floor 3 brought Hossman back ("New stripes. Thanks to you, <player>.", VETERAN I callout) and ended with Hossman rank 2, Anarki rank 3. Kamikaze fuses shouted "For glory!" 13 times over the DESCENT runs.
- **Code:** game/ai_taunt.c, game/g_ds_rank.c, cgame/cg_personality.c, game/bg_ds_personality.h; g_ds_nemesis.c extended. Hooks: g_main.c (G_PR_Init / G_PR_Frame), g_combat.c (G_PR_Death before G_RearmPlayerDie), g_svcmds.c, ai_chat.c (stock chat off), g_roguelike.c (veteran names first, kit cards, max health, spawn, floor end), g_ds_arsenal.c (weapon level bonus), g_ds_intro.c, cg_main.c, cg_servercmds.c (prb / prc), cg_draw.c, cg_players.c, cg_scoreboard.c, cg_ds_intro.c, ui_rearmed.c (5 rows on the BOTS tab). Dev: `prstat`, `prsay`, `prgrudge`, `prkill`, `prkillme`, `prflag`, `dsvet`, `dsnemesis level <n>`.

### Round 4: bot memory across runs, the Nemesis Hall of Fame (en-memory)
- **Lifetime records** (game/g_ds_botmem.c, contract game/bg_ds_botmem.h): `rearmed_bots.txt`, one `b|` line per bot name (its kills on you, yours on it, floors / runs met, last run seed / date / result / score, times nemesis, peak nemesis level, your headshots and revenges on it, its revenges) and one `h|` line per nemesis. Counted live (G_BM_Death from G_RL_PlayerDie), written only at the floor end (G_BM_FloorEnd from RL_Finish, after the run-over decision): no meta writes, nothing mid-floor. Daily (G_RL_Daily) and practice / rematch (G_RL_Practice) floors don't load or write it (md5-checked).
- **Greetings by tier** (stranger / familiar / rival 6 floors or 12 kills / arch-rival 15 floors or 30 kills / old nemesis), quoting the record and the player's in-game name (cleaned of colours, never hard-coded). A scheduler greets the most significant arrival first, 2 a floor (3 with g_botTaunts 2), one stranger at most; strangers 50%. A PRC_RIVAL right-edge callout for the floor's best rival (cg_personality.c). Kill / death lines quoting the score (G_BM_KillLine / G_BM_DeathLine from G_PR_Death). The veterans' generic "We meet again" yields to memory (Vet_Arrive keeps the promotion callout). All through G_PR_SayTo (g_botTaunts).
- **Lifetime grudge** (BM_GRUDGE: 4+ behind you): en-personality's grudge on the player for 60 s at arrival, 30 s every 2 min (G_EN_GrudgeTarget, respects g_botGrudges). **Fear** (BM_FEAR: you lead 8+ and 3x): ai_tactics.c heal-retreat thresholds +15 / +10 HP against the player and x1.3 distance weight on the player as a target (G_BM_Fear); up to 2 panic lines a floor within 700 units in sight. Enemy-side only.
- **Switch** `g_botMemory` 0 off / 1 on / 2 talk only (archived, registered in G_PR_Init; en-screens put it on F10 > BOTS). Records keep growing at 0.
- **Nemesis Hall of Fame** (q3_ui/ui_ds_hall.c): Records > NEMESIS HALL (compact button left of RUN HISTORY; `dshall [rivals | records]`). HALL OF FAME: every nemesis newest first (portrait, name + title, peak level pips, kills on you, run # and floor it rose, fate: slain by you with the weapon and floor, outlived you, dethroned by X, the run ended, still hunting you), details panel with the lifetime record. RIVALS: every remembered bot, most history first, score, tier, fear / grudge. Mouse, W/S/wheel/d-pad/stick, Tab/Q/E/shoulders, Space/A on BACK, Esc/B. Hall hooks in g_ds_nemesis.c: G_BM_NemesisRise (with the one it dethroned), G_BM_NemesisUpdate (level / title / kills), G_BM_NemesisSlain (lasthurt_mod -> weapon); RL_EndRun marks this run's open entry OUTLIVED; an open entry of an older run reads as THE RUN ENDED.
- Reset All Progress: `rearmed_bots.txt` added to dse_files / dse_tags (DSE_NUM_FILES 11, backup tag `bots`).
- cg_personality.c: the "PRCallout:" debug print now needs cg_debugEvents (it showed as a notify line at the top-left during play).
- **Tested** (tools/mem_test.sh, home_test/mem): mem_tiers.cfg scripted one bot through every tier with `dsbm set` / `dsbm say` (stranger "First time? Welcome to the pit." -> familiar "We've met. 3 to 2, me." -> rival "You again, AshWolf. 7 to 6, me." + RIVAL callout -> arch "29 floors. 6 runs. Still standing. 16 to 14, me." -> old nemesis "Your nemesis 2 times, AshWolf. Count them." -> fear "12 times you've dropped me, AshWolf. Not today. Please." + a panic line -> grudge "9 to 3, AshWolf. That changes today." with a 60 s lifetime grudge logged; in a new run a bot from the earlier runs greeted on its own: "I was your nemesis once, AshWolf. I remember how."); quitting mid-floor left the file md5-identical. mem_flow.cfg played 5 real floors over 2 runs: kills / revenges counted, an old nemesis greeted "You put me down with a Railgun. Not again.", the hall ended with 4 entries (slain by Railgun, dethroned by Anarki, outlived the run, still hunting). daily_play.cfg (3 daily floors) and practice_win.cfg left rearmed_bots.txt / meta / history md5-identical, no BotMem lines.

### Round 4: boss rematch (en-rematch)
- **BOSS REMATCH** (the player: "Let me rematch beaten bosses with harder modifiers, tracking my best times"). Get there from DESCENT > PRACTICE / REMATCH (the hub's bottom-row button, renamed from BOSS PRACTICE) > REMATCH, or with the console commands `dsrematch` (the screen) / `dsrematch <boss 1-4> <mask>` (straight into the fight).
  - **Unlocking:** only bosses you have beaten in a real descent can be rematched (meta `cnt_boss<n>` > 0; the daily and practice never count). Locked bosses are shown with how to unlock them, e.g. "Beat it in a descent to unlock: ACT II boss, floor 12".
- **Harder modifiers** (mask bit, heat), all in `game/bg_ds_rematch.h`:
  - IRON (1, +2): the boss has +30% health.
  - FURY (2, +2): the gaps between scripted attacks and their cooldowns are x0.75.
  - HALF CLOCK (4, +3): 2:30 of 5:00.
  - NO HEALTH (8, +2): health items aren't spawned.
  - GLASS (16, +2): your health limit is halved. The boss's power scaling still sees your full health, so GLASS doesn't shrink the boss and cancel IRON.
  - ENRAGED (32, +2): the last-minute enrage (cooldowns x0.75, gaps x0.8, speed x1.06) from the first second.
  - ONE LIFE (64, +3): your first death ends the fight through the time-up path.
  - The screen shows TOTAL HEAT n / 16.
- **Best times:** `rearmed_rematch.txt` keeps the fastest kill per boss and heat, plus the modifiers used, tries and wins. It is separate from the descent's `rearmed_records.txt` (the cgame keeps no records on a practice floor, as before).
  - The rematch screen lists a boss's bests by heat, your best at the heat you've picked, and the last result: NEW BEST - THE BUTCHER IN 0:23.1 - FIRST KILL AT HEAT 0 / +13.7 S OFF YOUR BEST 0:27.8 / FALLEN - ONE LIFE / TIME UP.
  - In the fight, a slim block left of the top boss bar (the clock is on its right) shows REMATCH HEAT n, the modifiers, and BEST at this heat. At the kill it turns into NEW BEST -x.x S or +x.x S OFF BEST. Nothing goes in the centre.
  - The practice end card says FALLEN on a ONE LIFE loss.
  - Reset All Progress empties and backs up the rematch file too (`rearmed_backup_<n>_rematch.txt`).
- **Nothing else is saved:** a rematch is a BOSS PRACTICE floor (g_dsPractice, rearmed_practice.txt with `rematch 1 / rm_mods / rm_prev`), so no run, meta, history, records, skills, tally, ghost or Skulls are written. The game reports `dsrmend <boss> <mask> <result> <kill ms> <bar %> <deaths>` instead of `dspracend`.
- **Tested:** tools/rematch_test.sh (isolated home_test/rematch, cfgs from tools/rematch_cfgs/gen.py), 3840x1080. Every screenshot was looked at.
  - Locked (`none` meta): all four bosses shown as LOCKED, and `dsrematch 1 0` was refused.
  - Two unlocked (`two` meta): Butcher and Overseer selectable, Hollow King and Arch-Demon locked.
  - Heat 0 vs heat 13 (IRON FURY HALF CLOCK NO HEALTH GLASS ENRAGED): boss pool 2257 -> 2933 per phase (x1.30); clock 300 -> 150 s; enraged at the fight start; 6 health items not spawned; GLASS max health 124 -> 62 (HUD 87 at the spawn).
  - FURY alone vs heat 0 (god mode, 57.5 s each): 16 vs 11 telegraphs, mean gap 3.33 vs 4.90 s.
  - ONE LIFE + GLASS: the first death -> "Rematch: ONE LIFE - the player fell" -> loss -> FALLEN.
  - Bests saved and shown per heat; a slower heat 0 kill shows the +13.7 s gap.
  - The seven progress files in the test home and the player's own files were md5-identical after every rematch run. (Opening the DESCENT hub in the entry test banked the copied run's pending result, which is normal hub behaviour and not rematch.)
- **Code:**
  - New: game/bg_ds_rematch.h, game/g_ds_rematch.c, cgame/cg_ds_rematch.c, q3_ui/ui_ds_rematch.c.
  - Hooks in g_ds_boss.c (IRON / HALF CLOCK at init, FURY pace, ENRAGED, ONE LIFE into time-up, CS_DESCENTBOSS rm/rmh/rmp, the power score), g_roguelike.c (`dsrmend`, GLASS in G_RL_MaxHealthBonus) and g_rearmed.c (NO HEALTH in G_RearmItemAllowed).
  - Hooks in cg_ds_acts.c (the block, FALLEN label), cg_roguelike.c (the end card), ui_ds_bossprac.c (REMATCH button, UI_DS_BossPracLaunch, command routing), ui_ds_meta.c (hub label) and ui_ds_econ.c (reset list).
- **Dev:** `dsrminfo` prints the table. `dsrematch <boss> <mask>` (mask 0-127).

### Round 4: timed 1v1 boss fights, boss practice (en-boss)
- **Boss floors are TIMED 1v1** (the player: "kill boss in 5 minutes... die as much as you want... 1x1"). `g_dsBossMode 0` (default):
  - Only you and the act boss: no rival bots, no adds, no ambushes, wild events or ghosts.
  - The boss makes its entrance (pillar, 1100+ units away). The clock starts when it opens fire, 3 s later: `g_dsBossTime`, 300 s.
  - Kill it before 0:00. TIME UP = the floor is lost (a run life; the bar share you took still pays).
  - Unlimited deaths: back in about 2 s, 700+ units from the boss, with a 2.5 s grace and your guns.
  - The boss never ends a relocation within 600 units of you. Measured in the sims: 564 respawns, the nearest 697 units from the boss; 347 relocations, the nearest 610.
  - `g_dsBossMode 1` keeps the round 2b 10-lives many-vs-1 fight (checked: 3 bots + Sim, 10 lives each).
- **One pool, three phases:** emptying a third makes the boss ROAR where it stands (a short invulnerable moment, a gem burst), and its next phase starts. It keeps its 2-of-3 strong sides, its scripted attacks and their telegraphs.
- **The last minute** enrages the boss: a sting, attacks 25% sooner, 6% faster on its feet.
- **No heal on a kill (design decision):** a death already costs about 7 s of the clock (respawn + the walk back), so the clock stays the only pressure.
- **Rewards:**
  - Win: a time bonus of 1 Skull per 15 s left (TIME LEFT +n chip in the floor tally, part of the BOSS line of the run summary).
  - The fastest kill per boss and difficulty is a record (rearmed_records.txt `arena boss_<n>`): SLAIN IN 3:47 - NEW RECORD (WAS 4:02).
- **HUD (top, never the centre):** the clock sits right of the boss bar in big digits.
  - Colours: warm white; amber in the last minute; red in the last 30 s, punching on every second with a draining line under it.
  - Sounds: a tick every second in the last 30 s, heavier with tenths in the last 10; the last-minute sting; a TIME UP buzzer and gong (new `sound/rearmed/acts/clock_tick|clock_tock|lastminute|timeup.wav`, tools/make_sounds_acts.py).
  - The bar shows the whole pool with marks at the phases; no lives skulls, no roster line. The corner block says TIME 4:12.
  - The versus intro and the floor strip say "KILL THE BUTCHER IN 5:00 - DIE AS OFTEN AS YOU MUST".
- **BOSS PRACTICE:** DESCENT hub > BOSS PRACTICE (bottom row), or the console `dsbossprac <1-4>` (`dsbossprac` alone = the picker).
  - A timed fight against act boss 1-4, with the typical kit of a player reaching it (the boss_sim kits) on Hurt Me Plenty.
  - It writes only `rearmed_practice.txt`. The game then writes none of your files (run, meta, history, tally, skills, ghost; the cgame no records): no Skulls, no records.
  - The picker comes back with the result. The file is in Reset All Progress.
  - Tested: all seven progress files in home_test byte-identical (md5) after a practice win and a practice time-up.
- **Balance** (tools/boss_sim.py, now `--mode timed` + `--model <results>`: the Sim alone vs the boss for 300 s, Hurt Me Plenty, timescale 2, 12 runs per batch):
  - Calibration per phase (`actsCalTimed`): Butcher 3.20, Overseer 0.60, Hollow King 0.69, Arch-Demon 0.50 (x ds_bosses[].health, x the power score up to 2.5).
  - The first calibration was far too easy: 48/48 wins, the Butcher dead in 66-220 s.
  - Final batches:
    - THE BUTCHER 3/12 wins (25%): kills at 197-249 s with 13 deaths on average; the losses ended with 9-54% of the bar left; 4.2 deaths a minute.
    - THE HOLLOW KING 4/12 (33%, at 0.69).
    - THE ARCH-DEMON 3/12 (25%, at 0.53; final 0.50, a touch easier).
    - THE OVERSEER 5/12 at 0.56 and 1/12 at 0.64; final 0.60, interpolated, not re-run.
  - A real player plays differently from the bot: `g_dsBossScale` and `g_dsBossTime` are the knobs.
- **Switches** (F10 > BOTS): `g_dsBossMode` (Timed 1v1 / 10 lives, many vs 1), `g_dsBossTime` (3:00-10:00).
- **Dev and tests:**
  - Commands: `dsbosstime <s>`, `dsphase` (timed: empties this phase).
  - Tests: tools/boss_test.sh (timed_lose / timed_win), tools/boss_prac_test.sh (practice from the main menu; hashes the progress files before and after); cfgs in tools/boss_fixtures.
- **Shared changes:**
  - g_roguelike.c: one planned bot on a timed boss floor; G_RL_Practice + rearmed_practice.txt; RL_Save writes nothing in practice; no tally / cards in practice; `dspracend`.
  - g_ds_arrive.c: a lone boss arrives staggered.
  - g_ds_econ.c: the time bonus, CS_DESCENT `tb`.
  - g_ds_cards.c: the TIME LEFT chip; no tally in practice.
  - g_ds_skills.c / g_ds_surprise.c: no writes in practice.
  - cg_ds_cards.c, cg_ds_intro.c, cg_roguelike.c (the practice end screen), cg_style.c (CG_RecordsBossKill, no records in practice).
  - q3_ui: ui_rearmed.c (BOTS tab), ui_roguelike.c (`dsbossprac`, g_dsPractice cleared), ui_ds_meta.c (hub button), ui_ds_econ.c (reset list); new file q3_ui/ui_ds_bossprac.c.

### Round 4: screen check at 7680x2160, decluttering (en-screens)
Checked against real screenshots at the player's 32:9 7680x2160. The fight scenes had typed and affixed bots, champion packs, ambushes, nemesis, thief and bestiary callouts, and hit feel all at once.
- **One stack for the right-edge callouts** (new `cgame/cg_edge.c`, `CG_EdgeSlot( id, height )`). Before, each system picked its own fixed height. NEMESIS sat on the style rank letter, SECRET FOUND on the nemesis-level line, and the evolution line on the mini-boss bar.
  - The callouts now stack under the style rank block (from y 220, 640x480 units) in a fixed order: mini-boss / event status, champion pack, NEMESIS / FINAL BLOW, nemesis levels / revenge / veterans, ambush / ghost / event callouts, STOLEN / RECLAIMED, SECRET FOUND, evolution ready, then en-tune's run-start note.
  - When one appears or ends, the ones under it glide to their new place.
  - While the style acts feed shows ("+60 KILL" lines under the rank letter, the same column), the stack starts under it (`CG_StyleFeedBottom`, cg_style.c) and glides back up when it fades.
  - The stack never reaches the score box.
- **Long callout lines wrap.** An ambush that landed with affixes ("FROST BLAZING - TANKY + DEADLY - ONE LIFE, HUNTS EVERYONE") ran 325 units wide, almost to the middle of the screen. Past 38 characters the line now breaks onto a third line.
- **Affix names** in the callouts are lifted a quarter toward white. The deep VAMPIRIC red was hard to read on the dark box.
- **The crosshair name is one plain colour.** A bot named with colour codes (`^1A^2n^3a...`) drew a rainbow "Anarki" just above the crosshair. This was the stock `cg_drawCrosshairNames`, not the enemy tags: `cg_enemyTags` is 0 by default and draws nothing over enemies.
- **Nothing in the middle of a DESCENT fight after a kill:**
  - The "You fragged X / 1st place with N" centre print is gone. The frag counter, the kill chain and the kill cam already say it.
  - The floating "+1" score number over the body (stock `cg_scorePlum`) is gone too: numbers over enemies are against the HUD rules. Other modes keep both.
- **F10 menu:**
  - The 9 tab labels ran into one word ("MOVEWEAPONSRULES..."). On a wide screen the tab row may now reach 70 units past each side of the 4:3 box, with even gaps.
  - **BOTS is two columns** with headings, **ALL MODES** (tactics, thieves, trash talk, grudges, shouts) and **DESCENT** (types, affixes, champions, nemesis levels, veterans, bot memory, bestiary callout, boss fights, boss clock). The rows used to be squeezed to 15 units for 17 switches.
  - Labels are shorter (the "DESCENT" prefix is now the heading).
  - en-memory's `g_botMemory` (Off / On / Talk only) is on the tab.
  - Every switch still works with the mouse, WASD / Tab / arrows, Space and the gamepad: up and down walk the left column, then the right.
  - HITS was fine; it only needed the tab row fix.
- **Bestiary grid:** archetypes, affixes, the champion pack, mini-bosses and act bosses each start a new row, nine to a row. W / S (and the gamepad's up and down) move to the nearest tile in the next row. Names on small tiles use narrower letters, so "THE ARCH-DEMON" no longer breaks mid-word.
- **Daily Challenge map:** the floor info panel's REWARD line no longer promises skulls ("Pick 1 of 3 cards."). The build panel shows DAILY instead of "SKULLS 0".
- **Test harness:** `tools/screens_test.sh <cfg> ds|menu [map] [W H]` (isolated home `home_test/screens`, 7680x2160 by default, `PK3DIR=` for a private pk3 stage). Cfgs come from `tools/screens_fixtures/gen.py`: fight.cfg (types + affixes + every callout + hit feel), menu.cfg (F10 BOTS / HITS), bestiary.cfg, dailymap.cfg. `tools/screens_fixtures/crops.py <dir>` writes full / centre / left / right review crops.
- **Shared files touched:**
  - cgame: cg_local.h (EDGE_* ids, CG_EdgeSlot), cg_draw.c (crosshair name), cg_event.c (frag centre print), cg_effects.c (score plum), cg_style.c (CG_StyleFeedBottom), cg_ds_acts.c, cg_personality.c, cg_thief.c, cg_ds_surprise.c, cg_ds_affix.c, cg_ds_secrets.c, cg_ds_cards.c (the stack).
  - q3_ui: ui_rearmed.c, ui_ds_meta.c / .h (grid slots), ui_ds_bestiary.c (UI_DS_BestiaryGroup), ui_roguelike.c (daily reward / build panel).
  - cmake/basegame.cmake (cg_edge.c).

### Round 4: review and cleanup (en-review)
- **Bug fixes:**
  - A SPLITTING elite killed by the player now counts as a SPLITTING kill in the bestiary. en-affix cleared the bit at death, before the bestiary counted the kill (`G_DS_AffixSpentNow`).
  - A side, back or splash hit on a shield bearer in the same frame as a blocked one (shotgun pellets, a rocket and its splash) no longer counts as blocked for the flinch and limbs. `g_enemy[].spare[0]` is now reset on every hit.
  - Floor-map boss text follows `g_dsBossMode` / `g_dsBossTime`: "Timed 1v1: kill it in 5:00. Unlimited deaths." or "With two rivals, 10 lives each: slay it first." The daily always shows 5:00. A timed boss shows ADDS 0 (it was "Three falls and the floor is lost." and ADDS 1).
  - Daily / practice floors log "Skills: not saved (daily|practice)" instead of "Skills: saved ...".
  - Collection unlock pop-ups play `sound/rearmed/mastery_up.wav`. `voc_newhighscore.wav` is Team Arena only, so it gave a missing-sound warning.
  - `dsbossprac` clears `ds_daily`, so a practice can't inherit the daily flag.
  - g_weapon.c: `CalcMuzzlePointOrigin` is declared in g_local.h, which removes the build warning.
- **va() (two rotating buffers):** no round-4 call has three live results. These had two live results in one call and now use local buffers:
  - g_ds_daily.c (`dly_r<n>`)
  - g_ds_intro.c (`t<n>` / `a<n>`)
  - ui_ds_daily.c (the top-10 shift)
  - ui_ds_hall.c (`DSH_Date` inside a va(), twice)
- **Merged duplicates (same strings, same look):**
  - The STOLEN / NEMESIS / REVENGE / VETERAN / RIVAL / "THE X REMEMBER YOU" cards now share `CG_EdgeCard` + `CG_EdgeText` in cg_edge.c. Before, cg_thief.c, cg_personality.c and cg_ds_types.c each had a copy.
  - Name cleaning: `G_EN_CleanName` (game) replaces 3 helpers + 6 inline copies. `CG_CleanUpper` / `CG_ClientNameUpper` (cgame) replace 4 helpers + 4 inline copies.
  - `ds_roman` (bg_descent.c) replaces 4 Roman-numeral tables.
  - `BG_DS_AffixName` (the bestiary table) replaces the affix name tables in g_ds_affix.c and cg_ds_affix.c. The bestiary's archetype names come from `ds_types[]`.
  - `BG_DS_TypeAdaptParse` also returns the ordered list (cgame walked the string a second time).
  - `G_RL_NoSave()` (game, = practice or daily, 10 sites) and `CG_DS_NoRecords()` (cgame, 3 sites) replace the repeated checks.
- **Debug leftovers:**
  - The "ThiefCallout:" console line (it showed on screen at every steal) now needs `cg_debugEvents 1`.
  - The daily / practice map-load notices go to games.log only.
  - None of the round-4 debug cvars defaulted on or was archived.
- **Dev commands in multiplayer:** with two or more humans connected, the round-4 dev commands that change the match or saved progress refuse to run unless cheats (devmap) or `developer 1` are on. The commands are btthief, hftest, prsay / prgrudge / prkill / prkillme / prflag, dsnemesis, dstypes / dstype / dstypeact / dstadapt, dsaffix / dschampions, dsbst / dsbstmeet / dsbstkill, dsbm and dsbosstime. Single player, the tests and the bot-only sims are unchanged.
- **Conflicts:**
  - `BotTacticsCommand` was declared twice; the ai_tactics.h copy is removed.
  - A comment in cg_local.h was on the wrong line.
  - `g_botGrudges 0` now also stops en-memory's lifetime grudges up front. Before, they were set and then cleared the next frame.
- **Left as they are:**
  - Shield knockback is applied before the block test, so a blocked hit still shoves. Fixing it would change gameplay.
  - The juggernaut's neck and arm zones (g_hitfeel.c) use the normal-size thresholds.
  - Game and UI each have their own copy of the rearmed_bots.txt parser.
  - CS_DESCENTBOSS is parsed again in cg_ds_rematch.c.
  - The rematch modifier joiners and time formats are duplicated.

## 0.5.0-beta (unreleased)

### Round 3: ambushes, floor modifiers, wild events, ghosts of past runs (ds-surprise)
- **Mid-floor ambush** (DESCENT fight / elite floors from floor 2, ~30% of them, never two floors in a row):
  - Partway through the floor a mini-boss crashes in. First "AMBUSH: THE HEADSMAN - INCOMING, BEHIND 53m" at the right edge, a war horn, and a pillar of light where it will land: always 600+ units away (by preference 900+ and out of sight). It lands 4 s later.
  - Five mini-bosses (Headsman, Warden, Wraith, Pyre, Hound), each with two of three strong sides like the act bosses, one life and one or two of the bosses' telegraphed attacks (cleaver, hook, slam, barrage, lances, meteors). They hunt everyone, rival bots too.
  - Health scales with your power score. Your kill pays the elite chest + gems, more gems, skulls and a 35% OVERDRIVE drop.
- **Pick-your-poison floor modifiers** on the floor map: a chip under ~38% of fight / elite nodes. Take the risk for the reward: ARMOURED (+50 enemy armor / +25% skulls), BLOODLUST (+20% enemy damage / +1 elite chest), BLACKOUT (dark world, glowing loot / +30% gem xp), SWARM (+2 bots / +40% gems), GLASS (half health for everyone / +25% skulls), FERAL (enemies 15% faster / +1 card). Modifier skulls get their own line (`sk_md`, CS_DESCENT `md`, the run summary, the tally's MODIFIER chip).
- **Wild events** (~20% of eligible floors, announced at the side):
  - GOLD METEOR: a telegraphed golden meteor; its crater holds gems and a chest, and bots race you for the chest.
  - BOT UPRISING: 30 s of weak skeleton fodder through purple portals: chain fuel, no frags.
  - BLOOD ECLIPSE: 25 s of double gems.
  - BOUNTY: the leading bot carries a chest, and it passes to whoever kills it.
- **Ghost of a past run:** every floor start writes your kit at that depth to `rearmed_ghost.txt`; a new run turns the last run's snapshots into the past. About a third of the eligible floors with a snapshot (and no ambush) send "GHOST OF <YOU>": your past cards, passives, exact weapon levels and evolutions. It's a pale see-through shell that hunts you. Beating it: YOU SURPASSED YOURSELF, skulls, gems and a chest.
- **Fairness:** nothing lands within 600 units of you without a warning. A spot you walk up to is re-picked far away. Extras are out of the frag race and never become the nemesis. Nothing starts during ds-arrivals' arrival sequence.
- **Switches** (F10 > DESCENT): `g_dsAmbush`, `g_dsModifiers`, `g_dsWildEvents`, `g_dsGhosts`.
- **Dev:** `dsambush`, `dsevent`, `dsghost`, `dsmod`, `dssurprise [sim|kill|look]`, `g_dsSurShots`, UI `dsmods` / `dsmodnode`, `ui_dsModAll`.
- **Tests:** `tools/surprise_test.sh`, `tools/surprise_ui_test.sh`, cfgs from `tools/surprise_fixtures/gen.py`. Assets: `tools/make_surprise_gfx.py`, `tools/make_sounds_surprise.py`, voices in `tools/make_voices.py` (`surprise/`).
- **Shared changes:**
  - `G_RL_AddExtraBot` (g_roguelike.c: bots joining mid-floor with planned kits), `G_DS_BankChest` (loot), `G_DS_SetWeaponLevels` (arsenal).
  - Hooks in the damage / health / speed / spawn / death / offer / floor-end paths and in the bot AI (hunt goal, priority, input).
  - Floors launch with `sv_maxclients` 16.
  - New ids: CS_DSSURPRISE 740, RM_ENT_SURPRISE 80, server command `dsuann`, and the arrival pillar style DSXS_ARRIVAL for ds-arrivals.

### Round 3: map variety, never the same map twice (ds-maps)
- **No repeats in a descent:** every map you launch goes into the run (`ds_played`); the floor map never offers a played map and two branches of a floor never share one. The maps of the floors you can see are stored in the run (`ds_fm`) so they don't change under you; a new node avoids every map above or below it on any path through it, borrowing another act's map before it would risk a repeat. A shown map changes only when a path you took would make it a repeat or when you lost on it. When every map was played (deep Abyss) the least recently played one comes back, tagged REVISITED.
- **Variety across descents:** lifetime play counts and the last descent per map (`dsm_plays` / `dsm_last` in rearmed_meta.txt, seeded once from the DESCENT floors in games.log). Weights favour maps played least and not lately (strongest on act 1) and the floor's size (bots vs small / medium / large maps); bosses avoid small maps. NEW tag for maps never played in DESCENT.
- **Bigger act pools by theme and size:** 14 / 15 / 14 / 14 maps (12 / 11 / 11 / 12 without Team Arena), the Abyss 47. New: q3dm0, q3tourney1–6, q3ctf1–4 and q3tourney6_ctf as FFA arenas (flags removed; team spawns become deathmatch spawns on maps with fewer than 8), pro-q3tourney2 / 4, Team Arena mptourney1–4 and mpteam1, 2, 4, 6, 7 (copied from your own install, `tools/extract_ta_maps.py`; release: setup.ps1 via ta_manifest.txt). Rejected: mpteam3 / mpteam5 (too slow a frag pace), mpteam8 (botlib AAS_LinkEntity overflows), mpterra1–3 (huge), mpq3ctf* / mpq3tourney6 (duplicate layouts).
- **Floor variants** (`g_dsVariants`, `cg_dsVariantLight`, F10 > DESCENT): lighting tints per act (NIGHT, BLOOD MOON, DUSK, FOG, SMOG, COLD LIGHT, TOXIC, INFERNO, FLOODED LIGHT) and item layouts (ARMS SWAP, ARMOR SHIFT, SCRAMBLED), shown on the floor map ("q3dm7  NIGHT  ARMS SWAP"). Run key `node_variant`, CS_DESCENT `mv`. No map is edited.
- **Bots** follow moved items: botlib `BotUpdateEntityItems` re-types the level item at the spot an item now stands on.
- **Measured** (`dsmapsim`, 20 descents x 24 floors, 15% of floors lost and retried): 0 repeats, 0 played maps offered, 0 duplicate branches; 9 shown maps changed before play (1.6%) + 77 replaced after a loss; act 1 spread over its 14 maps 8–13 plays each (with the player's real counts: q3dm7 4, the least played up to 13). 4 x 90 floors: 0 repeats, REVISITED only after the 47 maps ran out. All 23 new maps in the pools load and play with 4 bots on dedicated servers (26 tested) (`tools/dsmaps_maptest.py`).
- **Code and tools:** `game/bg_ds_maps.[ch]`, `game/g_ds_maps.c`, `cgame/cg_ds_maps.c`, `q3_ui/ui_ds_maps.[ch]` (rlLayer_t and the RL_*/RLN_* defines moved there), hooks in ui_roguelike.c, g_main.c, g_spawn.c, g_roguelike.c, cg_draw.c, botlib/be_ai_goal.c; `tools/map_audit.py`, `tools/extract_ta_maps.py`, `tools/dsmaps_test.sh`, `tools/dsmaps_maptest.py`, `tools/dsmaps_cfgs/`. `tools/pack.py` skips repacking zz-rearmed-ta.pk3 when assets_ta is unchanged (it now holds ~200 MB of Team Arena maps).

### Round 3: secret stashes (ds-secrets)
- **1-3 hidden strongboxes per fight / elite / event floor** (DESCENT). No marker: a faint hum only nearby and a glint at the lock now and then. Walking up opens one: the lid creaks open, the loot pops out, a loot beam (ds-frenzy) marks it, and **SECRET FOUND 2/3** plus what it was shows at the right edge. Switch `g_dsSecrets` (F10 > DESCENT).
- **Spots found automatically, no map edited:** at map load the bot navigation data (AAS) and column traces are scanned a few ms per frame, then cached in `rearmed_secrets_<map>.txt` (keyed by the map checksum). The scan keeps out-of-the-way floor (corners, behind pillars, low ceilings, dead ends, seen from few places, pushed against a wall) and places stock movement can't reach. Those are tested by running the real player movement (Pmove) with a jump, double jump, dash, wall run and their combos, plus a zip line fired at nearby walls and ceilings; the grapple uses a geometric line test. Spots in liquids and lava, on movers and near teleporters, jump pads, items and spawns are rejected. 37 stock / tourney / CTF maps give 3-15 spots each; 10 of them have spots that need a move.
- **Gated by your moves:** a stash that needs the double jump / dash / wall run / zip line / grapple only appears when you own one of the moves that reach it (logged per floor: `Secrets: floor N spot K needs ... -> locked`). Seeded by the run and the floor; better hidden spots are likelier.
- **Loot:** gem cache, gold cache (red gems + a few skulls), strongbox (an elite chest), OVERDRIVE (ds-frenzy) and LEGENDARY (the next won floor's offer holds a legendary, ds-cards); luck raises the rare ones.
- **Meta:** new floor stat `stashes` (DSST_STASHES, lifetime `cnt_stashes`, SECRET STASHES FOUND on Records). Finding 5 unlocks the new secret card **DOWSING ROD** (RLS_DOWSING): stashes hum twice as far, and a faint bearing dot with a soft tick at the top edge points to the nearest one.
- **Bots ignore stashes:** only the human player opens them.
- **Contract:** `RLS_DOWSING` (card 86), `DSST_STASHES`, `RM_ENT_STASH` 77, `CS_DSSECRETS` 739, `dsStash_t` / `ds_stashNames` in bg_descent.h.
- **Code and tools:** `game/g_ds_secrets.c`, `cgame/cg_ds_secrets.c`, hooks in g_roguelike.c (init), g_main.c (frame), g_svcmds.c, cg_ents.c, cg_main.c, cg_draw.c, ui_ds_meta.c (unlock, Records line), ui_rearmed.c (switch). Assets: `tools/blender/make_stash.py` (strongbox body + hinged lid), `tools/make_sounds_secrets.py` (hum in 4 loudness bands, lid, fanfare, tick). Tests: `tools/secrets_scan.py`, `tools/secrets_test.sh`, `tools/secrets_cfgs/`. Dev: `dssecrets`, `dsstash`, `g_dsSecretsDebug`.

### Round 3: kill-chain frenzy, lucky crits, power spikes, loot beams (ds-frenzy)
- **Kill-chain frenzy** (DESCENT): quick kills chain up. The window is about 4.2 s, stretched by headshots, gibs, the style rank and elites, and kept within 2.6-7 s.
  - Tiers BLOODLUST 3 / RAMPAGE 6 / UNSTOPPABLE 10 / MAYHEM 15 raise gem xp (x1.3 / 1.65 / 2.1 / 2.6), gem pull radius (up to x1.75) and pull speed (flight time down to x0.55), and add 1-4% crit chance.
  - Each tier plays a rising musical sting, and gem ticks get a louder layer.
  - A glow at the screen edges grows with the tier; the centre stays clean.
  - A compact counter with a timer bar sits under the corner block.
  - When the chain breaks, an "x12 CHAIN +N XP" cash-out counts up at the side; the cash-out bonus xp goes to the gun in hand.
  - Every 15 kills in one chain start BERSERK.
- **Lucky crits** (DESCENT): 4% a hit, raised by luck (+0.15% per luck %, so RABBIT'S FOOT 5 = 10%), the chain tier and BERSERK; capped at 25%. Bots crit 2% (elites and bosses 3%).
  - A crit hits x1.5, a headshot crit x2.
  - You see and hear a gold glint, sparks and light at the impact, a crunchy crack-and-ping, and a gold hitmarker with a small star on the crosshair.
  - A crit can never turn a non-lethal hit on a target at 90%+ health into a kill.
- **Power spikes** (DESCENT, you only): BERSERK 6 s (+40% damage, 30% shorter refire, 15% lifesteal) and OVERDRIVE 7 s (+30% speed, a dash that's always ready, the zip line refreshes in the air).
  - The OVERDRIVE pickup drops from elites (15%), from each boss life taken (20%) and from lucky kills (0.6%), all x luck.
  - Each spike has a slow-mo kick at the start, an edge colour wash, a hum loop, a countdown ring around the crosshair, a blink near the end and a power-down.
  - Spikes are temporary only: the bots' power matching never sees them, and the speed, refire and dash return to normal (logged).
- **Loot beams** (DESCENT): coloured light pillars, white / blue / purple / gold, on chests (by quality), the vacuum, OVERDRIVE pickups, red gems and other workers' legendary drops.
  - A beam is seen faintly through walls (a depth-hacked line and glow) and plays a rarity shimmer the first time it comes into view.
  - Up to 24 beams at once: 24 stacked in view cost about 0.25 ms a frame at 1920x1080 (800 to 666 fps).
- **F10 menu:** a new **DESCENT** tab with the boss lives, boss strength and nemesis switches (moved from RULES) and the new `g_dsFrenzy`, `g_dsCrits`, `g_dsSpikes`, `g_dsBeams`.
- **API** for the other round 3 parts in `g_descent.h` (FRENZY): beams, spikes, spike extension, best chain. Network: `EV_DS_FRENZY`, `HIT_CRIT`, server commands `dsfz` / `dsfzend` / `dsfzspk`, entity markers `RM_ENT_BEAM` 74 / `RM_ENT_SPIKE` 75.
- **Code and tools:** `game/g_ds_frenzy.c`, `cgame/cg_ds_frenzy.c`. Assets come from `tools/make_frenzy_assets.py`. Tests: `tools/frenzy_test.sh` + `tools/frenzy_cfgs/`. Dev commands: `dsfrenzy`, `dscrit`, `dsspike`, `dsbeam`, `dsfzstate`.

- **Promo clips:** `promo/clips/` (8 looping H.264 clips + posters + `manifest.txt`, 8.7 MB) for the landing page, recorded with the engine's `video` command from scripted demos; tools `tools/clip_rec.sh`, `clip_play.sh`, `clip_concat.sh`, `clip_encode.sh`, `clip_sheet.sh`, cfgs in `tools/clip_cfgs/`.

### Round 3: card rarity reveal, legendary cards, synergy ding, floor-end tally (ds-cards)
- **Floor-end tally** after every won floor (before the chests and the cards): kills, headshots, gibs, best chain, gems, damage, style and skulls roll up like a slot machine with ticks and a stamp each, the skulls with where they came from (floor, kills, style, node, boss, modifier, bonus %, difficulty), and a red NEW RECORD stamp for each single-floor personal best (`rearmed_tally.txt`). ~4.7 s, fire / jump skips. The server waits for it in the floor-end phase (client `dsctallydone`, 11 s at most).
- **Card rarity reveal:** the cards deal face-down with a rarity-coloured back and frame (grey / blue / purple / gold) and flip one by one; rare shimmer, epic sting, a legendary charges up and opens with a roar + choir hit, a gold flash, a slow pulse, edge flares and a light sweep. Fire / jump / a pick key flips them all at once. Pick and reroll controls unchanged.
- **12 legendary cards** (new `RLC_LEGEND_FIRST` range after the secrets, kind `RLK_LEGEND` 7, rarity 3): CHAIN LIGHTNING SOUL, ECHO, VAMPIRE LORD, TIME THIEF, BLOOD ENGINE, MIRROR, PHOENIX, MAGNETAR, DOUBLE AGENT, GRAVEDIGGER, BLACK MASS, DEADEYE (effects: README). The player's only, at most one per offer, 3% + floor/2 (+8 elite, +25 boss, x luck, max 50%) and decided by the floor so rerolls can't fish; a secret stash can owe one. Collection LEGENDS tab, the run's build list shows them first, a small gold status row above the corner HUD, and they count toward the enemies' power matching.
- **Synergy ding:** a pick that completes a combo or an evolution key snaps the two icons together with a chime and a burst of light at the side. In play, "<EVOLUTION> READY: OPEN A CHEST" shows small at the right edge when an evolution becomes ready.
- Switch `cg_dsReveal` (F10 > DESCENT > Card reveal). Dev: `dsoffer legendary [n]`, `dstally`, `dslegend <n>`, `dslgtest ...`, `dslgstate`. Tests `tools/cards_test.sh` + `tools/cards_cfgs/`, sounds `tools/make_sounds_cards.py`. New event `EV_DS_CARDS`, server commands `dsctally` / `dsclg`; `CG_DS_CardTagEx` (cg_ds_loot.c) also returns what a card snaps with; bots drop the player as an enemy when they're on the same side (`BotFindEnemy`).

### Enemies join one by one (ds-arrivals)
- **Staggered entry on DESCENT floors** (`g_dsStagger`, F10 > DESCENT > Enemies join one by one, default on): a floor starts with you and the first opponent (two when 5+ bots are planned). The rest join one at a time, about 6.5 s apart on floor 1 down to about 4.3 s from floor 21 (±0.7 s, always 4–7 s). Fight floors bring the regular bots first, then your nemesis, then the elite last.
- **Each arrival gets its own moment:**
  - A warm pillar of light (ds-surprise's landing beam, new style `DSXS_ARRIVAL`) marks a spawn spot at least 600 units from you (preferably 900+ and out of sight). The bot lands there 1.5 s later, with the stock spawn flash and sound and a thump.
  - The announcer reads that one name alone, with "Elite!" / "Your nemesis!" / "Rival!" first (a new "Rival!" line in `tools/make_voices.py`).
  - The portrait slides into the top strip.
- **The strip:** after the versus intro, the top-edge row shrinks to a slim strip (YOU VS + one slot per opponent; empty slots show a dim "?"). It fills as the bots arrive (the newest one shows its threat note) and collapses about 2.5 s after the last one is in. The loading screen still previews everyone ("JOINING ONE BY ONE").
- **Boss floors:** the two rivals first, then the boss. Its arrival has a 2.8 s warning, a roar and its name, lands 1100+ units from you, and the strip grows back to the full versus row for the boss's big introduction. The boss bar comes once the boss is in. Its scripted attacks wait 5 s after it lands, and it holds fire for 3 s as before.
- **Fair frag race:** a late joiner starts with the lowest score among the floor's bots already in. It is never ahead of anyone and never hopelessly behind, and that score is capped at the target −3. The frag target is unchanged. Bots are still only added once the AAS is up, and every planned bot's model is preloaded on the loading screen, so late joiners neither hitch nor wear a stand-in model.
- **Shared announcer:** `CG_DSI_AnnouncerFreeAt` / `CG_DSI_AnnouncerHold` give one voice queue for the intro, the arrivals and ds-surprise's callouts. Ambushes, wild events and ghosts wait until everyone is in plus 3 s (`G_DS_ArrivalsLeft`, `G_DS_ArrivalLastAt`).
- **Code:** `game/g_ds_arrive.c` (new), `cgame/cg_ds_intro.c`; configstring `CS_DSARRIVE` 742. Tests: `tools/arrive_test.sh <normal|elite|boss|big> W H` (fixtures `tools/arrive_fixtures/`). The default `arr_shot.cfg` takes a screenshot about every 2 s. Use `CFG=arr_nowait.cfg` for timings: a long cfg `wait` also holds back the game's `addbot`. `arr_head.cfg` tests the head start. Measured with `arr_nowait.cfg`: every late bot joined in the planned frame, the longest real gap between game frames around a join was 64–66 ms (same as the floor start; a frame is 50 ms), 0 stand-in models, and each name was read once. Logs: `DSArrive:` (game log: plan, warnings, joins with distance and head start; console: announcements, strip open/closed) and `DSVoice: read <NAME>` (one line per name read).

### Enemies announced like in single player (ds-intro)
- **DESCENT loading screen** shows the floor, the act, the goal and the floor's opponents: head portraits from your own install, names, a one-line threat note (weapon level + abilities, ELITE + evolved guns, NEMESIS + kills, RIVAL), and on boss floors the boss big with its title and its two strong sides. The server publishes the floor plan in a new configstring (`CS_DSINTRO` 737, `game/g_ds_intro.c`) during `G_InitGame`, so it's there before the bots join.
- **Versus intro at the floor start** along the top edge (`cgame/cg_ds_intro.c`, `cg_dsIntro`, F10 > HUD > Enemy intro): portraits slide in from the screen edges, the announcer reads the matchup (your install's bot name lines; new generated "The Butcher!" / "The Overseer!" / "The Hollow King!" / "The Arch-Demon!" / "Elite!" / "Your nemesis!" / "Rivals!" lines; the stock "Fight!"), 3.3–4 s, fire skips. It takes the floor strip's place; the act card and the boss bar wait for it (the boss bar then shows at once). Skirmish gets the same strip once the bots are in.
- Dev: `dsintro`, `dsintro skirmish`, `dsintro loading [ms]`; tests `tools/dsintro_test.sh <normal|elite|boss|skirmish> W H` (fixtures in `tools/dsintro_fixtures`).

### Props look like they belong in Quake 3 (ds-art)
- Player: "barrels and yellow barrels seem modern and too high fidelity". Every physics prop and its debris is rebuilt in id's 1999 style. The props are low poly with chunky bevels and 256 px painterly textures (about 3 px per unit, painted at 2x and box-filtered down). They use the warm rust/brown/ochre palette of the stock maps, carry crunchy texel-level grit and have their light painted in (dark creases, highlights on the top edges). There are no printed labels or clean gradients.
  - **Barrel:** a bulged iron cask of riveted plates and four uneven hand-fitted hoops (the middle two tilted). It is dented, rust-streaked and sooty. It has a hand-painted bone-white skull and a red fire rune, plus a glowing crack between the lower hoops that weeps dark oil (additive glow stage, slow pulse).
  - **Gas tank** (still the "yellow barrel"): an old pressure vessel in chipped, patched ochre paint over riveted iron, with iron straps. It has a domed shoulder and a bronze valve gone green, with a spoked hand wheel and an outlet that still points +X for the rupture jet. An oily rag is knotted round the neck. A murky yellow-green sight glass glows faintly. Hand-stencilled marks: the alchemical "air" triangle and three upward chevrons. It is slimmer and taller than the barrel, so the two read apart by both outline and colour.
  - **Crate:** rough dark planks with nails and stains, chunky edge battens, diagonal braces, forged iron corner caps and a burnt-in brand.
  - **Debris:** the textures match (128 px): dark plank splinters, dark forged-iron shrapnel with a few ochre paint flakes, warm grey-brown gothic stone chips.
  - **Shaders:** a light env-map sheen only on oil, brass and glass (an env stage under a base texture whose alpha is 1 - shine). Same model paths, 1 frame, no tags, one surface "body" each, so no code change.
- **Source:** `tools/blender/make_props.py` (new; barrel/tank/crate), `make_models.py` (calls it), `make_2d.py` (debris textures; the old printed-label layers are gone), `make_shader.py`. Rebuild with `bash tools/blender/build_all.sh barrel tank crate debris`.
- **Test tools:** `tools/art_shot.sh` runs screenshots in an isolated install (`dist_art` / `home_art`). `tools/art_pk3.py` packs a `zzz-art.pk3` overlay. `tools/art_cfg.py` writes a prop line-up cfg (wide shot, close-ups, shooting the crate, a barrel and a tank).

### Fixes (ds-fixes, from the 2026-10-02 playtest)
- **Every enemy looked like you until you pressed TAB.** Bots that joined after the map loaded got a stand-in model: with `cg_deferPlayers 1` (the default) the cgame dressed a newcomer in the first loaded client's model, which is yours, and only loaded the real one when the scoreboard came up. Stock `addbot` asks the client to load right away only when the bot joins more than 1 s into the map; DESCENT adds each floor's bots 0.8 s in, so every floor started like this. Now the server lists the coming bots' models in a configstring (`CS_PRELOAD`, from the DESCENT floor plan, the skirmish menu's bots via `g_preloadBots`, and SP arena bots), the cgame loads them during the loading screen, and the joining bot copies them without a hitch. A bot in your own local game is never deferred. Remote players joining a net game keep the stock deferred loading.
- **The event timer ran out but the event went on.** DESCENT event floors started a normal 35 s event and pushed its end back every frame on the server, but the HUD only knew the first end time, so the timer bar emptied and blinked while the event kept running for the whole floor. Event floors now start a floor-long event (`G_RearmStartFloorMutator`, `mutend -1` in `CS_REARMED`): the plate says THIS FLOOR with no countdown, and the event ends with the floor. Timed events still end when their bar does. Dev: `mutator <n> floor`.
- **Gas tanks vanished when shot.** A prop that had rested since it spawned kept a stale "left the ground" time (0), and a shot tank is knocked off the ground without resetting it, so its first physics step counted it as falling for minutes and removed it as "fell out of the world", silently and with no blast. The same could hit a crate or barrel knocked by another prop, a prop whose support blew up, or one shoved off a spawning player. Every prop now starts that clock when it leaves the ground, floating ones don't count as falling, and a lit prop that gets stuck or does fall out explodes instead of disappearing. Dev: `g_propTest "hit <type> <damage>"`.

### Kill cam, death cam and best-kill replays
- **Side kill cam:** your notable kills (gibs, overkills, headshots, airshots, midair, double and multi kills, void / lava kills, the final frag, elites, bosses, broken boss phases, your nemesis) replay in a framed side panel. The victim dies in slow motion (0.6x, 0.26x at the kill) for about 3 s while a camera orbits the body and the gibs, with the label, the weapon icon, the victim's name, letterbox bars, grain and a vignette. The main view keeps running at full speed. One panel at a time: a better kill queues (or cuts in when it's much better), and kills close together join the one showing. On 32:9 it sits in the gutter outside the 16:9 centre, on 16:9 in a small box at the left edge.
- **Death cam:** killed by a player? The panel replays the last 1.5 s around your killer in slow motion, with their name, health and armor left, and their weapon. It closes when you respawn.
- **Best kill replay:** the match's (DESCENT: the floor's) highest-scoring kill replays at the intermission as "REPLAY: <weapon> <label>". Wide screens play it in the side panel next to the scoreboard / chests / cards. On 16:9 it plays full size after the chest jackpot and before the reward cards (in other modes, before the scoreboard); jump or fire skips it.
- **Switches (F10 > CHAOS):** Side kill cam `cg_killCam` (Off / Notable kills / Every kill), Kill cam side `cg_killCamSide` (Auto / Left / Right), Death cam `cg_deathCam`, Best kill replay `cg_replays`.
- **How:** cgame records the main view's entities, lights and client positions at 25 Hz (about 6 s ring; the best kill is copied out to its own clip) and replays them, interpolated (animation frames blended across frame boundaries), into a second 3D view. The server tells the killer what made a kill notable (`rmkc`) and the victim who killed them with how much health (`rmkiller`), in `game/g_killcam.c`.
- **Engine:** new refdef flag `RDF_PIP`. For such a view opengl2 skips the shadow maps, SSAO, bloom, blur and depth prepass, and uses the main view's auto exposure without feeding it. Cost: about 1-2 ms a frame while a panel shows, nothing otherwise. opengl1 ignores the flag. Also fixed: with `r_depthPrepass 0` and `r_ssao 1`, the opengl2 3D view went black (a stale SSAO buffer); SSAO is now only applied when the prepass ran.
- Dev: `cg_killCamDebug 1/2/3`, server `kchurt <victim> <attacker> <damage> [mod]`. Panel textures and shaders come from `tools/make_killcam_gfx.py`.

### Corpses and blood stay until the map ends
- **Bodies stay** in every mode (DESCENT too) until the map changes: up to 64 at once (`cg_gorePersistMax` 8–64), each one still shootable, gibbable and kickable, and you walk through them. When the cap is full, a body nobody can see goes first. If they are all in view, the oldest and farthest one sinks in a second and a spare slot takes the new body. Gibbed players leave their chunks and no invisible body (stock Quake 3 left one that still stopped shots).
- **Blood stays:** pools, splats, wall spray, drips, footprints and the stock blood marks never fade. Pools seep wider twice in the first minute, and blood dries darker and browner over a few minutes. Bodies whose death you didn't see get their pool when you find them.
- The blood pool holds 8192 polys at the default cap (was 4096), in two rings: one for the blood where someone died, one for hit spray. Gunfire can't wipe the pools under the bodies. Repeat droplet splats on the same spot are skipped, and blood stacked more than about 5 layers deep drops its oldest buried layers, which cut the fill-rate cost of one 60-body pile from 3.5 ms to 0.6 ms at 3840x1080.
- **Gib chunks** (up to 256) and **bullet holes / burn marks** (up to 1024) stay too, oldest first, and a chunk in view is never the one removed.
- **Switch:** F10 > CHAOS > Corpses and blood: Stay / Fade (`cg_gorePersist`, default Stay; replaces `cg_bloodPools`), and Corpses kept (`cg_gorePersistMax`). Fade is the stock behaviour.
- **Cost:** bodies, gib chunks and blood outside the view are skipped on the client, kept bodies cast no blob shadow, and the renderer no longer adds decals to the sun-shadow passes. After a 10-minute 6-bot match: 3.65 ms a frame against 3.32 ms with Fade (1920x540). Looking at a 64-body pile: 3.86 ms against 3.61 ms. Snapshots peaked at 77 entities; far old bodies are left out of snapshots if a snapshot ever gets close to the 256-entity limit. `rearmed_gfx.cfg` raises `r_maxpolys` / `r_maxpolyverts` to 16384 / 131072.
- Dev: `gorestats`, `gorefill <deaths> [radius]`, `gorelook [n]` (server), `cg_goreStats 1` (client fps / pool log), `g_corpseSnapHigh` (snapshot guard threshold).

### DESCENT: skill trees (Skyrim style)
- **Ten skills that train by doing**, in every mode against bots: MELEE, BALLISTICS, SCATTER, EXPLOSIVES, ENERGY, PRECISION (railgun + headshots), TRAVERSAL (wall runs, jumps, slides, zip line, grapple, fast air time), EVASION (dashes, blinks, near misses), SURVIVAL (damage lived through, healing), CARNAGE (gibs, style). Levels 1-100, fast early and slow late; permanent and shared by every character. DESCENT scales skill xp by the difficulty's reward multiplier (0.5x-1.10x), other modes 1.0x.
- **Character level:** skill levels raise it (7 skill levels for level 2, then one more each time), each level is a perk point. A first descent that ends at the first boss gives about 1-3 points. "+1 PERK POINT" and the skills trained show on the run summary, never mid-fight.
- **70 perks, 104 ranks** in ten constellations, every one with a minimum skill level per rank and a branch it hangs from. Every effect is a single-digit percent or a mechanic (free shots, gems from headshot kills, a zip line back on a kill, a dodge, cheat death once per floor...). They work in DESCENT only.
- **SKILLS screen** from the hub: star-map trees with taken / available / locked stars, skill level and xp bar per tree, perk points, a details panel with the exact numbers, Space / click / A to take a rank, a per-tree respec for Skulls (25 + 5 per rank). Ten trees in one row on 32:9.
- **HUD:** the held weapon's skill ("EXPLOSIVES 23") and a thin xp bar above the weapon level pips, a short flash and chime on a level-up there. `cg_skillHud`, `g_skills` (F10).
- Files: `rearmed_skills.txt` (pending xp, written by the game at floor / match end), merged once into `rearmed_meta.txt` by the menus.

### DESCENT v2: gems, chests and passives
- **XP gems:** your kills drop blue (1), green (5) and red (25) gems, more and bigger from elites. They pop out, glow, and fly into you when you come close, with a tick that climbs in pitch as you chain them. Each gem levels the weapon that made the kill; a thin bar under the ammo number shows the progress.
- **Vacuum:** a magnet pickup (elites 1 in 4, rarely any kill) pulls every gem on the map to you at once, in streams from across the map.
- **Treasure chests:** elites drop one, bosses a big one, lucky kills a rare one. They're banked (corner block) and opened at the end of a won floor on a slot-machine screen with 1, 3 or 5 rewards: an evolution when one is ready, then weapon levels, then passive levels. Fire or jump skips. A lost floor keeps them.
- **13 passive items** with 5 levels each, 6 slots per run, replace the old stat cards. Every level is a single-digit step.
- **Card offers** now hold passives, +1 weapon levels for weapons you've used, and unlocked secret cards. Cards that advance an evolution or combo with what you hold get a gold ribbon (EVOLVES ROCKET LAUNCHER, KEY FOR HELLFIRE, COMBO: BLINK + MIRAGE).
- **Enemies keep up with you:** bots carry about as many passive levels as you, and their damage follows your overall power score (bots 0.95x, elites 1.1x, clamped).
- Secret cards PENTAGRAM, BLOOD CROWN, GOLDEN SKULL and STOPWATCH work.

### DESCENT v2: weapon levels and evolutions
- **Weapon levels 1–8** for every gun except the grapple, from gem xp, +1 weapon cards and chests. Each level is a small step (damage +6–8%, fire 5% faster, radius or speed +6–8%) or a new trick: piercing bullets, extra pellets and nails, ricochets, slows, knockback, healing hits. Level 8 adds a special per weapon: explosive rounds, burning pellets, bomblets, a bonus rocket, an arcing beam, exploding kills, growing plasma, a burning crater, pinning nails, fire mines, no chaingun spin-down, gibbing gauntlet kills.
- **Fire rate is predicted:** weapon levels, HAIR TRIGGER and the fire-rate upgrade travel to the client with the player state, so faster weapons fire without prediction jitter (tested: machinegun 100 ms -> 70 ms between shots at level 8 with HAIR TRIGGER 5, no prediction misses).
- **13 evolutions** (12 plus the secret ARMAGEDDON) that change how the gun behaves: twin piercing tracers, burning shotgun streaks, cluster bomblets, seeking rocket salvos, forking lightning, path-detonating slugs, splitting star orbs, a black hole, ricocheting nails, chain-reacting fire mines, a never-empty chaingun. Each has its own colour, trails, impacts and a layered sound, and the gun glows in that colour.
- **HUD:** level pips above the ammo number, a coloured frame round the ammo icon of an evolved gun, a short chime and LEVEL tag beside the pips on a level-up (EVOLVED: HELLFIRE for an evolution), levels and evolved glows in the weapon wheel. Nothing mid-screen.
- **Bots keep pace:** their weapon levels follow your average weapon level (-1 early in an act, elites and bosses +1) and catch up as you grow during a floor; elites from Act 3 carry three evolved guns, bosses all of them. Bots lean towards their best-levelled gun.
- New sounds (level-up, evolution, evolved fire and impact layers) are synthesized by `tools/make_sounds_ds.py`.
- Dev: `dslevel`, `dsxp`, `dsevolve`, `dsstate`, `ds_debug 1`.

### DESCENT v2: characters, unlocks, collection, run summary
- **9 characters**, picked after the difficulty: RANGER, SARGE, VISOR, ANARKI, KLESK, XAERO, DOOM, HUNTER and a secret one. Each plays as its own model for the run and starts with its own weapon (and level), passive, abilities or perk, some with a weakness. Your own model setting is never touched.
- **Unlocks from lifetime counters:** kills, headshots, gauntlet kills, gibs, zip rides, chests, evolutions, elites, bosses, acts cleared, wins, best floor. Your kills and floors from before 0.5 count. Unlocks pop up on the run summary, never mid-fight.
- **Secret cards have hidden conditions** (the Collection hints at them): gib 10 enemies on one floor, three weapons at level 8 in one descent, 25 chests, win a floor in under a minute, evolve two weapons in one descent. The secret character has its own hint.
- **Collection** (hub button): weapons with the highest level you reached, evolutions, passives, perks, abilities, characters, bosses with times defeated, secrets.
- **Upgrade tree:** 20 upgrades in 4 tiers (FLESH, MARROW, IRON, SOUL); each tier opens after enough ranks in the one before. The 9 old upgrades keep their ranks and prices. New ones: gem pull, gem xp, luck, fire rate, splash radius, damage taken, extra chest rewards, REVIVAL, ARSENAL (weapons start at level 2), FIRST PICK, more vacuum drops. No new upgrade gives more than 9% per rank.
- **Run summary and VICTORY screen:** a damage breakdown per weapon (level, evolved, damage and share, kills, time held, DPS), passives, evolutions, run stats, what beat you, records, unlocks. After the Act 4 boss: INTO THE ABYSS or END THE DESCENT.
- **Run history:** the last 30 descents, under Records (`rearmed_history.txt`).
- **Resume mid-floor:** quitting or crashing during a floor no longer loses it: Continue restarts that floor from its start, and it costs no life.
- **Old saves:** your 0.4 meta file keeps every value (Skulls, upgrade ranks, records); a 0.4 descent still in progress can be ended from the hub and banked as usual.
- **Fixed:** the menus could silently drop save keys past 64 per file; the limit is now 160 in both the menus and the game.

### DESCENT v2: acts, bosses, the vault and the Abyss
- **24 floors in 4 acts** (THE CRYPTS, THE FOUNDRY, THE VOID, THE PIT), each with its own map pool, colour grade, edge haze, ambient particles (dust, embers, void motes, ash) and music from your own Quake 3 install. A new act opens with a short card at the top right and a low sting. `cg_dsBiome 0/1/2`, `g_dsActMusic 0/1`.
- **A boss ends every act:** THE BUTCHER (meat hook, cleaver charge, ground slam, blood frenzy), THE OVERSEER (rocket barrage, drones with an energy shield, mortar rain), THE HOLLOW KING (rail lances, blink, gravity well, decoys), THE ARCH-DEMON (meteor storm, fire nova, fire floor, enraged). Bosses are drawn and hit bigger, have three phases with a roar between them, and telegraph every big attack 0.8–1.2 s ahead with a sound, a mark in the world and the attack's name under the boss bar. A boss with nobody in sight walks to the nearest fighter. (Round 2: you, two rival bots and the boss, everyone with 10 lives.)
- **Boss bar** at the top centre: name, title, the pool with phase notches, your lives, the coming attack. Win = the boss dead in its last phase (big chest, red gems); lose = out of lives on that floor (see round 2).
- Boss health grows with difficulty and with your power score (capped at 2.5x); Abyss bosses gain 6% health and 1% attack damage per floor.
- **Beating the Act 4 boss wins the descent** (VICTORY screen); **the Abyss** goes on from floor 25 with a random boss every 6th floor.
- **The vault:** a rare ??? node: 45 s with no enemies, raining gems and a free chest, no card pick.
- Floor pacing retuned for the 24 floors: 2→5 bots, 8→16 frags (about 3–4 minutes a floor), skill +0.12 a floor after floor 6, threat tier 0/1/2 across the acts.
- New synthesized sounds (`tools/make_sounds_acts.py`): attack warning, meat hook, rail charge, roar, blink, gravity well, shield, fire nova, act sting, vault chime.
- Dev: `dsact`, `dsboss`, `dsvault`, `dslayers` (UI); `dsattack`, `dsphase`, `dsbossdmg`, `dskillboss`, `dsshow`, `dsbossinfo`, `dsvaultend` (server); `ui_rlDevmap 1`.
- Fixed: `descent new <n>` from the console printed "unknown cmd" (the command name was overwritten by its own argument).

### DESCENT round 2: boss fights and your nemesis
- **Boss floors are many against one:** you + two rival bots + the act boss, and **everyone has 10 lives** (`g_dsBossLives`, F10 > RULES; the boss always 10). The rivals mostly go for the boss but fight everyone; the boss fights everyone and its big attacks lean toward whoever hurt it last. Each boss life is a small pool; when it runs out the boss really dies and comes back far from you (phase 2 at 6 lives left, phase 3 at 3, with a roar). Out of lives = eliminated (a rival or the boss bursts and leaves; you = the floor is lost). Win = the boss's last life taken while you live, by anyone; your own final blow pays bonus Skulls and a second chest, and the boss Skulls follow the share of its lives you took. Rivals hit the boss for 55% damage, so its lives are mostly yours to take. Respawns 2.5 s safe from the boss, away from it; no clock.
- **The boss bar** shows the current boss life, its lives as skulls in its colour (the one just taken flashes), its phase and two strong sides (FAST/SLOW, ARMOURED/FRAIL, BRUTAL/WEAK), and one slim roster line under it: YOU 7   SARGE 5   KLESK OUT   BUTCHER 6. Eliminations get a small line at the right edge. The corner block says LIVES 7/10.
- **Every boss has 2 of 3 strong sides:** THE BUTCHER fast + brutal but frail, THE OVERSEER armoured + brutal but slow, THE HOLLOW KING fast + armoured but hits weakly, THE ARCH-DEMON changes its pair every phase, Abyss bosses roll one. Speed drives its run speed, animation, attack rate and telegraphs (never under 0.8 s); health its pool; damage everything it deals. Fast bosses leave a glowing streak, armoured ones are bigger with a steel sheen, brutal ones smoulder and land heavier blows.
- `g_dsBossScale` (F10 > RULES: 0.7 to 1.3) scales every boss's health and damage.
- **Balanced by simulation:** `tools/boss_sim.py` plays boss floors on hidden dedicated servers with a skill 5 bot standing in for you (your kit for that floor, a modelled read of the telegraphs) against the boss and the two rivals. Target: you win 20–35% of boss floors at Hurt Me Plenty and take most of the boss's lives yourself. Last measured: Butcher 8–15% (lowered after), Overseer 20–33%, Hollow King 20%, Arch-Demon 35% (raised a little after); you take about 80% of the boss lives against the first two bosses and about half against the last two.
- The meat hook and cleaver lock their aim 0.4–0.45 s before they fly (it used to be 0.6–0.7 s after the warning whatever the telegraph length), so a quick boss can't catch you on the lock.
- **A boss never teleports next to you any more:** ON THE HUNT is gone (a boss with nobody in sight walks to the nearest fighter on its own paths), the Hollow King's blink is a SHADOW STEP sidestep along a visible line that never ends within 600 units of you, and a boss rescued from the void or coming back from a lost life appears at a spawn point far from you and out of your sight. Every relocation is logged with its distance to you (`DSBoss: relocate`).
- **Nemesis:** the bot that killed you most (3 times or more) becomes your nemesis: a callout at the right edge, then it follows you to every later fight floor under its name, 8% stronger, with a dark red rim, taunting in the chat line now and then. Killing it pays Skulls and a gem burst (NEMESIS SLAIN). Saved with the run. `g_dsNemesis 0` turns it off.
- Fixed: a floor started from a running game could lose all its bots ("AAS not initialized") when `bot_pause` had been left on: the bot library now finishes loading the map while paused, and a floor adds its bots only once the map's navigation is ready.

### DESCENT round 2: difficulty, rewards, reset; frag target
- **Change the difficulty mid-descent:** a DIFFICULTY button on the floor map (current level + reward multiplier) opens a five-level picker. From the next floor the bots' skill, the bosses and the rewards follow it; lives stay as set at the start. Reward multiplier: I Can Win x0.5, Bring It On x0.75, Hurt Me Plenty x1.0, Hardcore x1.05, Nightmare x1.10 on Skulls (floors, chests, nemesis), gem xp and skill xp. Shown on the picker, the floor-end Skulls line and the run summary.
- **Records count the easiest difficulty you played a floor on** (deepest floor, wins, history; a mixed descent shows BRING IT ON+ in the history and RECORDS AS on the summary).
- **Failed runs pay a little:** a lost floor pays 1 Skull per 3 kills (up to 4), and a lost boss floor 2 + the share of the boss's health you removed (Act 1 bar worth 22: 30% = 9 Skulls). Kills on won floors pay 1 per 3 (was 1 per 4, up to 4 instead of 3); a beaten boss adds its whole bar and a kill bonus (Act 1: 61 Skulls for the boss floor). A run that dies at the first boss on Hurt Me Plenty now banks about 60-80 Skulls (`dsecon sim`: 77), about one first upgrade rank; every boss attempt adds 9-15.
- **The lost-floor screen** shows what the fight paid: kills, BOSS BAR 30%: +9, the multiplier, "+9 SKULLS FOR THE FIGHT".
- **RESET ALL PROGRESS** (Records, main menu only): two separate confirm screens, then the meta, run, history and skills files are backed up as `rearmed_backup_<n>_*.txt` and emptied. **ABANDON DESCENT** (hub): ends the descent as a loss, Skulls and records banked.
- **Perks in DESCENT are single-digit** (Thick Skin +8 health, Heavy Caliber +6%, Quick Hands 6%, Overcharger 9%, Bloodthirst 5%, Headhunter +9%, Sky Hunter +8%, Glass Cannon +9% / -9, Last Stand +9% / -9%, Kinetic up to 8%, Thorns 8%, Ironclad 25 armor, Adrenaline 5, Ground Zero +9%); their DESCENT cards show these numbers. Normal mode keeps the old ones.
- **Frag target x1 / x2 / x3** (F10 > RULES, `g_fragMult`): every normal match, skirmish and single player arena needs that many times its frags; the HUD, scoreboard, warnings and loading screen show the target.
- Dev: `dsdiff`, `dsdiffmenu`, `dsreset`, `dsabandon` (UI); `dsecon [sim|perks]`, `dsscore` (server); tests `tools/econ_test.sh` + `tools/econ_cfgs/`.

### DESCENT: the upgrade economy, tightened
- **A smaller, sharper tree:** 14 upgrades in four tiers that each do one thing: I FLESH (health, speed, damage), II MARROW (rerolls, a free starting ability, a 5-card first offer per act, 4-card offers), III IRON (more chest rewards, weapons at level 2, a banked chest at every act's start: new QUARTERMASTER), IV SOUL (new LAST BREATH +1 boss-floor life, REVIVAL, SPARE SOUL). Tiers open at 5 / 3 / 3 ranks.
- **Retired** the nine upgrades that copied passives or skill perks (BONE PLATE, GRAVE ROBBER, LODESTONE, HUNGER, RABBIT'S FOOT, HAIR TRIGGER, BLAST CORE, IRON HIDE) or did nothing you could feel (EVENT HORIZON). Owned ranks are refunded at the price paid, once, when the hub opens; your kept ranks stay.
- **Prices follow a model** (docs/DESCENT_V2.md, `tools/econ_model.py`, in game `dsecon sim`): the whole tree is 14,800 Skulls (was 7,855 for 20 upgrades), about 40-60 descents; a new profile's first descent buys its first rank, early on a rank about every run, then every buy is a choice between a stat rank and saving for a rule-changer.
- **Income cleaned up:** the boss floor no longer pays a separate kill bonus on top of its x3, the lives you took and the final blow; the boss bar is worth more (24 + 6 per act) and pays the share of the boss's lives you took, so every attempt counts. Per descent on Hurt Me Plenty: 18 (dies on floor 3), 83 (dies at the first boss), 130 (act 2), 210 (boss 2), 275 (act 3), 370 (boss 3), 445 (act 4), 600 (win) (before: 18 / 78 / 141 / 215 / 302 / 390 / 491 / 670).
- **Legible:** upgrade rows show what you have and what the next rank gives; a line shows how much of the tree you own; the run summary breaks the banked Skulls down (floors, kills, bosses, loot and final blows, bonus and difficulty).

## Unreleased (0.4.x)

### Bots: pits and barrels
- **Bots fall into pits far less.** In 5-bot test matches on q3dm17, void deaths per match went from 38 to about 10, and on q3dm18 from 42 to about 9. Stock Quake 3 bots lose about 24 a match on these maps.
- Bots work out where a jump, air jump, wall kick, dash, slide-jump or ground pound will come down before they do it, and skip it if it would end in a pit.
- In the air over a pit, whatever threw them there, bots steer back to the last floor they stood on. If steering won't make it, they dash, blink, air jump or grapple back, using only the moves they own (DESCENT cards count).
- Bots won't run or slide off a ledge into a pit. They jump the gap when a jump makes it; otherwise they stop and take another route for a while.
- Bots no longer dash, air jump, wall run, ground pound or grapple during a jump pad's flight.
- A grapple used to cross a gap holds on until it arrives, instead of letting go over the pit. An offensive grapple needs floor beyond the enemy.
- In a fight next to a pit, bots move away from the edge, so a rocket doesn't knock them in.
- **Bots shoot barrels and gas tanks next to you.** Before, they lined the shot up and then gave up almost every time. They now keep turning onto the barrel until they fire. Shotguns, rockets and BFG shots count when they land close enough. Machinegun, chaingun, lightning, plasma and nailgun fire is held until the barrel blows.
- Bots lead a moving target, prefer the railgun, and shoot more when the blast would finish you off.
- `g_debugAlt 1` writes `PropShot:` and pit recovery lines to games.log. Every pit or lava death now writes a `Fall:` line saying what the bot was doing.

### Cleaner screen
- **DESCENT floor start:** the big centre panel with every bot's kit is gone. A slim strip at the top shows the floor, node, frags to win, enemies, lives, one row of what the enemies can do, and the elite or boss. It fades out after about 3.5 s. Every bot's full kit is next to the scoreboard (hold the scoreboard key).
- **DESCENT corner block:** shows only the floor, lives, frags and Skulls. The lives no longer overlap the ELITE label. Your perks show as small coloured pips: a new perk's name shows for 4 s, and the whole list is on the scoreboard. This applies in every mode.
- **Nothing over enemies' heads by default.** Elites and bosses keep their orange or purple aura. The new `cg_enemyTags` setting (F10 > HUD, Enemy tags) chooses between Off, Minimal (a small ELITE / BOSS label) and Full (the old label, name and kit, plus award badges over heads). The crosshair name is smaller and shows ELITE / BOSS in front of it.
- **Less in the middle of the screen:**
  - The style rank moved out to the right edge of the 16:9 area.
  - Medals now show above the style rank.
  - Streak and bounty banners are smaller and shorter.
  - The event name shows once, on the event plate, and its hint goes away after 6 s.
  - Tips moved down from under the crosshair.
  - "You fragged" is smaller, AIRSHOT! is smaller, and speed lines are fainter.
  - Dark plates fit their text instead of spanning the whole screen.
- **Fixed:** every announcement used to show as the plain white banner at the top, because of a bug that lost the announcement's type. Medals, streaks, bounty and juggernaut callouts now get their own colour and place again.

### Zip line
- **Fixed: the zip line hardly ever worked for players** (bots were fine). Z reached the game and the DESCENT card unlocked it, but:
  - The cable stopped on the first thing it touched and only held on walls, so aiming at an enemy, a body or a barrel was a miss. It now flies past them and holds on the wall behind.
  - Riding checked the line from your feet, not your eyes, so a low lip you could see over cut the cable the moment it attached. It now checks from your eyes.
  - A low ledge or railing in the way knocked you off. You now step over anything up to eye height. Getting stuck just short of the anchor counts as arriving, and a teleporter drops the cable.
- **You can see when it will work:** small cyan brackets `[ ]` around the crosshair mean a press would hold right now (`cg_zipHud`, F10 > HUD).
- **A miss tells you why:** a dry click and a short reason under the crosshair: NO WALL IN RANGE, TOO CLOSE, LAND TO RELOAD, GET THE CARD FIRST (DESCENT) or ZIP LINE IS OFF.
- The DESCENT ZIP LINE card names the key you actually bound ("press [Z]"), and so do the DASH and GRAPPLE cards. A one-time tip explains the brackets the first time they show.
- `g_zipLine` is now in F10 > MOVEMENT.

### Sound and slow motion
- **Fixed the sound going haywire for a whole round:** during Quad Madness, Jetpack Party (a whole DESCENT event floor) and for the juggernaut, the powerup wear-off sound played 10–20 times a second. These powerups now have no time limit while they last, so they don't blink on the HUD and don't drop as 1-second pickups when you die.
- Gore, debris, prop knocks and corpse-bomb beeps share a sound budget (8 per 0.2 s, the same sound at most every 60 ms), so a pile of gibs or a barrel chain can't drown out weapons and announcers.
- **Slow motion is stronger and smoother:** it eases in and out every frame instead of stepping, and it now also comes on for your headshot finishers and gibs. Big moments go down to 0.3–0.4 speed for about a second; the match-winning frag goes deepest and longest.
- Slow motion can't stack or get stuck: one at a time, at least 2.5 s apart (6 s between your own), and the engine returns to normal speed if the game ever leaves it on. It also works at very high frame rates now.

## 0.4.0-beta (2026-09-30)

### DESCENT
- **A new roguelike mode** in the main menu: an endless run of frag races against bots.
  - You start with no abilities and earn them, one card per won floor: abilities, alt-fire per weapon, stats or perks.
  - Bots get the same number of cards from the same pool. Elite bots carry auras, and every 5th floor is a boss.
- **Floor map:** choose a Fight, Elite, Event or Boss node each floor, Slay the Spire style. Lives depend on difficulty.
- **Skulls and upgrades:** Skulls are banked when a descent ends and buy permanent upgrades. Records track your deepest floor per difficulty.

### Zip line
- Z fires a cable at a surface. You ride it straight there at high speed, aiming and shooting; jump to let go with your momentum.
- It works in every mode (`g_zipLine`). Bots use it to reach items, chase, escape and travel.

### Bots
- Bots check further ahead before dashing or double jumping, so they fall off narrow maps like q3dm17 far less: falls without being knocked went from 66 to 23 in a 5-bot test match.
- Knocking someone into a pit, lava or a crusher now counts as your kill ("was knocked into the void by", +60 VOID KILL style).

### Fixes
- **Borderless fullscreen** (F10 > VIDEO > Display mode, now the default). The game no longer drops to the desktop when another window or a notification takes the focus (`r_minimizeOnFocusLoss 0`).
- Fewer barrels: 4–8 props per map, at most a third of them explosive, and Barrel Bonanza drops 12–18.

## 0.3.0-beta (2026-09-30)

Also new since 0.2.0: the style meter (D to SSS with an announcer), perks, records and weapon mastery, the hold-R weapon wheel with last-weapon swap, and bot personalities with trash talk.

### Movement
- **Slide:** crouch while running. It's a short burst (capped, so chains don't snowball) that speeds up downhill; jump out of it to keep the speed.
- **Ground pound:** crouch in the air. You hang for a moment, then plunge. The shockwave launches enemies, and landing on a head is a STOMP. No fall damage.
- Wall jumps off a wall run now really leave the wall; wall-run pull is framerate independent; steering away ends the run.
- **Camera:** landing dip, slide and wall-run lean, speed FOV, FOV punches on dash, slide, blink and slam (`cg_moveCamera`, `cg_speedFov`).

### Perks, style and events
- **14 new perks:** Adrenaline, Glass Cannon, Blink, Ground Zero, Slipstream, Ricochet, Chain Reaction, Last Stand, Featherweight, Magnet Hook, Butcher, Kinetic, Mirage and Thorns.
  - The first offer comes after 2 kills, then every 3, up to 8 perks.
  - Offers mix categories and skip perks whose feature is switched off.
  - The cards animate in, show your bound keys and have a timer.
- **9 new random events:** Blood Moon, Meteor Shower, Overcharge, Bullet Time, Ice Rink, Barrel Bonanza, Weapon Roulette, Thunderstorm and Chain Reaction.
  - Every event gets a 3-second warning with a voice line, a timer bar and a screen tint.
  - Events now run every 60 s for 35 s.
- **Style:** the rank letter pops, the meter animates, and there are new sources (slam, barrel, wall splat, splattered, chain reaction, meteor, thorns, hooked, slide, blink, ricochet, decapitated, double/multi kill). Gain is 1.5× during events.
- **Slow motion** on the match-winning kill, double kills and SSS kills (single player only, `g_slowmo`).
- **Weapon wheel:** open and close animation, ammo bars, hover sounds, right-stick selection on gamepads.
- **First-time tips** for each mechanic (`cg_tips`).
- New kill messages for slam, props, wall splats, meteors, thorns and corpse explosions.

### Physics
- **Props:** a few per map (4–8, at most a third explosive): explosive barrels (chain reactions, burning fuses), gas tanks that rocket around before exploding, and breakable crates that drop health or ammo.
  - They take knockback, get kicked when you run into them, and hurt whoever they hit.
  - Destroyed props respawn somewhere new.
- **Corpse physics:** bodies fly from blasts, flip in the air and slam into walls (wall splat). Living players knocked into walls take damage.
- **Debris:** rock chips and shrapnel from impacts. Explosions push gibs around, and running through gibs kicks them.

### Gore and effects
- Headshot kills decapitate and the neck pumps blood.
- Overkills explode into lots of chunks, and charged rails cut bodies in half.
- Burn and lightning kills char the body.
- Every hit sprays blood onto the wall behind the victim and it runs down.
- Gibs stay until the map ends, and blood leaves footprints.
- Blood on the screen, a flash on big kills, speed lines, and a heartbeat vignette at low health.
- New explosions: layered fireball, a smoke column that lingers, a light flash, scorch marks. Also better impacts and element effects, and glowing charged shots.
- `cg_gore 0-3` scales everything.

### Bots
- Bots slide, ground pound, blink and triple jump.
- They shoot barrels next to you, combo elements and use Magnet Hook, Mirage and Adrenaline.
- They pick perks by personality.
- They step out of meteor and lightning markers, fire and ground-pound landings.
- Kill rate is unchanged: 53.7 kills with the new tricks on against 50.5 with them off, 10 matches each.

### New art
- Our own Blender-made models:
  - barrel, crate and gas tank
  - rock, metal and wood debris
  - six meat gibs and a severed head
  - a meteor
- Blood splatter, drip and screen-blood textures.
- Procedural sounds and announcer lines for the new systems.

### Settings
- A new **CHAOS** tab in the REARMED menu holds props, debris, gore, screen effects, event effects and slow motion.
- The new moves, bot tricks and camera options are in their tabs.

### Engine
- `rm_slowmo` scales the local clock for slow motion, and only works in a local game with one human.

## 0.2.0-beta (2026-09-28)

### Weapons
- **Charged alt-fire:** tap alt-fire for the quick version, or hold it to charge a stronger one (a meter under the crosshair shows the charge). Every weapon has a charged mode: rocket punch, marksman shot, gale, napalm, boulder, thunderstrike, wall-piercing lance, big whirlpool, BFG overload, pinning spike and minefield. The chaingun alt spins up the barrel.
- **Shotgun alt:** now an air blast that throws players back and turns their projectiles around.
- **Elements:**
  - Fire: grenades leave burning ground.
  - Water: plasma soaks.
  - Electricity: the lightning gun shocks, with 1.5x damage and arcs between soaked players.
  - Wind: shotgun alt.
  - Earth: rocket blasts throw grounded players up.
  - Water puts out fire.
  - New kill messages for burning and lightning.
- **Random weapons:** each weapon or ammo spawn has a 25% chance of being a random weapon (`g_weaponRandom`, 0–100 slider), re-rolled on every respawn.

### Bots
- Bots dash away from incoming projectiles and from enemies shooting at them, double jump over splash damage, hook toward far enemies, away from danger and along their route, run on walls and kick off them.
- Bots use every alt-fire, both tapped and charged, per weapon and situation, and air-blast incoming rockets back.
- Fixed: bot dash and double-jump presses were often lost, because a single-frame press could be replaced before the server read it.

### Feedback
- Blood pools: every death leaves blood on the floor and walls that stays until the map ends (`cg_bloodPools`).
- Damage numbers removed.
- Burning, soaked and shocked players have their own visuals and screen tint.

## 0.1.0-beta (2026-09-28)

First public build.

### Settings
- New REARMED menu (main menu, or F10 / the `rearmed` console command): every addition has its own switch, in five tabs (Movement, Weapons, Rules, HUD & Feel, Video).
- Server rules reach client prediction through the player state, so switching them never causes prediction errors.

### Movement
- Wall run: hold forward along a wall at speed. It keeps your speed, lets you fall slowly and gives your double jump back; jump to kick off. The camera leans away from the wall.
- Dash has 2 charges, shown under the crosshair while recharging.
- Jump assist: coyote time after walking off a ledge, and a jump pressed just before landing waits for the ground instead of wasting the double jump (bunny hops keep working).
- Wall jumps and wall runs work on clip-brushed walls.
- Fixed: tapping jump during a grapple pull gave unlimited double jumps.

### Weapons
- Sticky grenades stick to players, with a 1 second fuse and a warning for the victim.
- Gauntlet: an uppercut within 0.7 s of a lunge launches much higher (LUNGE COMBO).
- Overcharged rail: each target is hit once per shot, damage falls to 70% and then 40% per ricochet, and the beam trails embers.
- The prox launcher's alt-fire also detonates mines stuck to players.
- The BFG ball zaps the nearest enemies.
- The shotgun slug now has recoil too.
- Fixed: the plasma singularity pulled teammates.
- Fixed: nailgun headshots almost never registered.

### Rules
- New mode: One in the Chamber (one slug, a kill reloads one).
- New mutator: Jetpack Party. Mutators are now picked by weight and never repeat back to back.
- Bounties: a 5-kill streak puts a price on your head. Ending it pays a point, health and armor.
- Medals: First Blood, Revenge, Triple/Quad/Rampage Kill, Shutdown, Comeback, Mid-Air, From the Grave, Stuck, Lunge Combo.
- Fixed: the Gun Game level reset on every death.
- Fixed: Gun Game could end the match during warmup, or at a low fraglimit.
- Fixed: falling to your death cost Gun Game and Juggernaut score.
- Fixed: Rocket Frenzy took away rockets you had picked up, and left a free launcher behind.
- The juggernaut gets no killstreak rewards, and there is no juggernaut in duels.
- Bots hunt the juggernaut and bounty targets, never retreat in the fixed-weapon modes, and don't jetpack into pits.

### Feedback and HUD
- Damage direction arrows around the crosshair.
- Stacking damage numbers (`cg_damageNumbers 2`).
- Gold hitmarkers and numbers on headshots.
- Armor-break sound.
- Two variants of each hit sound.
- Low-health pulse at the screen edges.
- HUD corners can sit in the 4:3 box, the 16:9 area, or at the screen edges (`cg_hudWide 0/1/2`).
- Fixed: screen shake on the death camera.
- Fixed: the "Low" view-kick setting turned kick off.
- Particle collision is capped per frame.

### Video
- New post-processing pass: contrast-adaptive sharpening, an ultrawide-safe vignette, film grain and chromatic aberration, each with its own switch.
- Bloom settings are clamped.

### Controls
- The chaingun moves from C to V, so C is crouch again.
- F10 opens the settings.
