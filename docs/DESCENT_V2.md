# DESCENT v2 (0.5.0-beta) — design and work split

Inspiration: Vampire Survivors (weapon levels, evolutions, passives, chests, gem vacuum,
characters, collection, secrets), Slay the Spire (acts, branching map), Hades (bosses).

## What the player asked for (2026-10-01 interview)

- Growth: weapon levels 1-8, weapon evolutions (max weapon + matching perk/passive),
  treasure chests with a slot-machine opening, stacking passive items (5 levels), synergy
  combos shown on cards.
- Fights: keep frag races vs bots; unique bosses with their own attacks.
- Meta: unlockable characters with starting kits, collection screen, secret cards and hidden
  unlocks, acts/biomes as you go deeper, a bigger permanent upgrade tree.
- Spectacle: a screen full of gems vacuumed by a magnet; an end-of-run damage breakdown.
- Length: a run takes 1-2 hours, save and resume.
- NOT chosen (do not build): mid-fight level-up card picks, horde/survival/wave floors,
  merchants/gold, banish/skip/seal, arcana, curse, daily seed (asked for in round 4: the DAILY
  CHALLENGE, en-daily), damage numbers.

The player: solo vs bots, 32:9 7680x2160 screen, hates centre-screen clutter (keep the
play view clean, big moments only at floor end / short and off-centre), loves gore, charged
alt-fires, headshots, style rank, records. Left-handed, rebinds keys: never hard-code keys,
show the bound key; avoid number row past 5, arrows, Enter for new actions.

## Run structure (owner: ACTS)

- 4 acts x 6 floors = 24 floors. Floor 6/12/18/24 = the act boss. Beating the act 4 boss
  WINS the run (victory screen, unlocks), then the player may continue into THE ABYSS
  (act 5, endless, scaling, a random boss every 6th floor) or bank and end.
- ~3-4 min per floor -> 1.5-2 h per full run. Save at every floor boundary (exists);
  quitting mid-floor resumes that floor from its start without losing a life.
- Each act = biome: its own map pool, colour grade + haze tint, ambient particles, music
  (`ds_acts[]` in bg_descent.c). Act intro card when a new act begins (floor 1/7/13/19),
  short and off-centre-top.
- Node types stay (fight, elite, event) plus a rare secret VAULT node ("???") on the map:
  a short bonus floor where gems rain and a free chest waits (no frag race; leave via a
  timer).

## Growth (owners: ARSENAL, LOOT)

- **Weapon levels 1-8** (ARSENAL): every weapon starts at level 1 the first time you hold
  it in a run. Per-level effects: `ds_weaponLevelDesc[][]` (tune freely, keep the
  table in sync). Levels come from gem xp (LOOT feeds xp to the killing weapon; thresholds
  `ds_wlevelXP`), from +1 weapon cards and chest rewards. A level-up = a short sound + a
  small flash on the weapon icon/ammo area (not mid-screen).
- **Evolutions** (ARSENAL effects, LOOT grants): weapon level 8 + `ds_evolutions[].keyCard`
  held -> the next opened chest evolves it (chest priority #1). Evolved weapons look and
  sound different (new projectile colours/trails/effects). ARMAGEDDON is the secret union
  (needs UNION secret card + HELLFIRE + CLUSTER BOMB).
- **Passive items** (LOOT): 13 passives, 5 levels each, at most 6 different ones per run.
  Offered as cards (RLC_PASSIVE_FIRST+p) and chest rewards. Old stat cards (SWIFT ..
  BANDOLIER) are no longer offered.
- **Cards between floors**: still pick 1 of 3 after a win. Pool = abilities, alt-fires,
  passives, +1 weapon levels (only weapons you have used this run), perks, secret cards
  (if unlocked). **Synergy**: a card that completes or advances an evolution, or combos with
  what you hold, gets a glowing "EVOLVES ROCKET LAUNCHER" / "COMBO: BLINK + MIRAGE" tag.
- **Gems** (LOOT): kills drop xp gems (blue 1, green 5, red 25; more and bigger from elites
  and bosses; merge when there are too many). Base pull radius ~110u, LODESTONE grows it.
  A rare VACUUM pickup (elites 25%, any kill ~1.5% x luck) pulls every gem on the map: the
  spectacle moment. Gem xp goes to the weapon that made the kill (stored per gem).
- **Chests** (LOOT): elites drop one, bosses drop a big one, rare luck drop. Picked-up chests
  are banked (small HUD counter) and opened at the END of a won floor, before the card
  pick: slot-machine jackpot screen (1 / 3 / 5 rewards, luck-scaled), rewards = evolution if
  eligible, then weapon levels for weapons you own, then passive levels. Lost floor: chests
  stay banked.

## Bots (owners: ARSENAL weapon levels, LOOT passives)

Bots grow on the same track: weapon levels ~ 1 + floor/3 (capped 8, elites +1, bosses 8),
passives as many levels as the player has (roughly), evolutions from act 3 for elites and
always for bosses. Keep it readable: no new overhead text (cg_enemyTags stays the switch).

## Bosses (owner: ACTS)

One per act (`ds_bosses[]`): bot client with role boss, scaled model, big health pool in 3
phases (each "death" before the last starts the next phase with a short invulnerable roar),
its own attacks run by the game with clear telegraphs (RM_ENT_BOSSFX), top health bar
(CS_DESCENTBOSS). Boss floor: win = boss dead, loss = the boss frags you 3 times. A few
normal bots as adds. Reuse existing systems where possible (meteors, strike markers, fire
patches, slam shockwave, rockets, grapple pull).

## Meta (owner: META)

- **Characters** (`ds_characters[]`): pick at run start (New Descent -> difficulty ->
  character). Locked ones show their unlock condition. Starting kit applied at run start /
  every spawn.
- **Achievements / unlocks**: lifetime counters in rearmed_meta.txt drive character and secret
  unlocks (kills, headshots, gauntlet kills, zips, chests opened, evolutions, acts cleared,
  floor reached, ...). Unlock popup at the run summary, never mid-fight.
- **Secrets**: secret cards (RLS_*) unlocked by hidden conditions; ORBB character; the union
  evolution; the vault node. Collection shows "???" until found.
- **Collection screen**: weapons (max level reached, evolution found), evolutions, passives,
  perks, abilities, characters, bosses (kills), secrets.
- **Upgrade tree**: expand to ~20 permanent upgrades in tiers (a tier opens after N ranks
  bought in the previous). Keep the existing keys and ranks (the player owns some).
- **Run summary**: end-of-run damage breakdown per weapon (icon, level, evolved, damage,
  kills, time held, DPS), passives held, totals, best floor time, cause of death; run history
  (rearmed_history.txt, last 30 runs) viewable from Records.

## Shared contract

- `code/game/bg_descent.h` (+ tables in `bg_descent.c`, built into all three modules): acts,
  weapon levels, passives, evolutions, secrets, characters, bosses, CS_DESCENTGROW (26),
  CS_DESCENTBOSS (31), RM_ENT_GEM/CHEST/VACUUM/BOSSFX markers.
- Card ids: `RLC_PASSIVE_FIRST`, `RLC_WEAPON_FIRST`, `RLC_SECRET_FIRST` ranges (bg_public.h),
  names/descs/kind/rarity/short extended in bg_misc.c.
- rearmed_run.txt: `version 2` (older runs are discarded). New keys: `act`, `character`,
  `wl` (weapon levels, 14 csv), `wx` (weapon xp, 14 csv), `pv` (passive levels, 13 csv),
  `evo` (mask), `chests` (csv of qualities), `dmg` (damage per weapon, 14 csv), `wkills`
  (14 csv), `wtime` (msec held, 14 csv), `gems`, `vault` + per-run stat counters `st_*`
  (META defines). Game writes growth at floor end; UI writes at run start.
- rearmed_meta.txt: existing keys kept; new `chars` (mask), `secrets` (mask), `seen_*`
  masks, `cnt_*` lifetime counters, new `up_*` upgrade ranks.

## Ownership (parallel workers; small targeted edits in shared files, tell the owner)

| Worker | Owns | New files |
|---|---|---|
| ARSENAL | weapon level effects + evolutions (game firing/damage, cgame visuals/sounds), weapon level HUD pips, bot weapon levels | game/g_ds_arsenal.c, cgame/cg_ds_arsenal.c |
| LOOT | gems, vacuum, chests (entities, HUD counter, end-of-floor jackpot screen), passives (effects), card pool/offer/synergy tags, gem xp -> weapon (calls ARSENAL), bot passives, growth CS + run keys | game/g_ds_loot.c, cgame/cg_ds_loot.c |
| ACTS | 24-floor act structure, map pools, act intro, biome visuals + music, vault node, bosses (AI attacks, phases, telegraphs, health bar), win/Abyss flow | game/g_ds_boss.c, cgame/cg_ds_acts.c |
| META | characters (select + kits), achievements/unlocks/secrets, collection screen, upgrade tree, run summary + damage breakdown + history, resume mid-floor, hub layout | q3_ui/ui_ds_meta.c |

Shared hot files: g_roguelike.c, cg_roguelike.c, ui_roguelike.c, g_combat.c, g_weapon.c,
g_missile.c, bg_descent.*. Use the Edit tool for small edits, never rewrite a shared file,
re-read before editing (others change it), build only through tools/build.sh (lock).
New .c files must be added to ioq3/cmake/basegame.cmake.

## Round 2 (2026-10-01, player feedback)

Player asks, verbatim intent:
1. **Boss fights** (revised 2026-10-02 after playing; the first 1v1 version gave "NO chance"):
   **many vs 1**: the player + 2 rival bots (normal floor kits, power-matched) + the act boss.
   The rivals go for the boss most of the time (~65%) but also fight each other and the player;
   the boss fights everyone and aims its scripted attacks a bit more at whoever hurt it lately.
   **Everyone has 10 lives** (`g_dsBossLives`; the boss always 10). Each boss life is a small
   pool; phase 2 at 6 boss lives left, phase 3 at 3 (a roar when it comes back); its two strong
   sides stay. Out of lives = eliminated (bots/boss leave with a burst; the player = floor lost).
   Win = the boss eliminated while the player has lives, whoever lands the last blow; the last
   blow yourself pays skulls + a second chest, and the boss skulls follow the share of its lives
   YOU took. Respawns: away from the boss with a 2.5 s grace; the boss respawns far from the
   player. **The boss never teleports next to the player** (no ON THE HUNT landing; the Hollow
   King's blink is a sidestep that ends 600+ units from the player; a boss with nobody in sight
   walks to the nearest fighter). HUD: boss lives as skulls + the current life's bar, a slim
   roster line (you / rivals / boss with lives left), elimination callouts at the right edge.
2. **Corpses and blood stay until the end of the match** (all modes), with sane caps.
3. **Standard modes: a frag target multiplier x1 / x2 / x3** (doubles/triples the frags needed
   to clear an arena).
4. **DESCENT difficulty can be switched during a run** (on the floor map, between floors).
   Reward multiplier by difficulty: I Can Win 0.5x, Bring It On 0.75x, Hurt Me Plenty 1.0x,
   Hardcore 1.05x, Nightmare 1.10x (skulls, skill xp, gem xp). Lives stay as set at run start.
5. **Bosses: 2 of 3 strong sides** (speed, health, damage). Fast+strong = low health; tanky =
   slow; etc. The first boss must feel REALLY hard but killable: the player expects ~10 attempts.
   - BUTCHER: fast + damage, low health. OVERSEER: tanky + damage, slow. HOLLOW KING: fast +
     tanky, low damage. ARCH-DEMON: changes pair each phase (fast+strong, tanky+strong,
     fast+tanky). Abyss bosses: a random pair.
6. **Failed runs still pay a little**: losing to the first boss should give enough to buy a
   little permanent progress (an upgrade rank or a perk point), so every loop feels rewarding.
7. **Reset all progress** option (double confirm; back the old files up as rearmed_backup_*.txt).
8. **Skill trees, Skyrim style** (decided by me, the player didn't pick in the form):
   permanent, shared by all characters; skills train by doing (in every mode vs bots, effects
   only in DESCENT); skill levels raise a character level, each character level = 1 perk point,
   perks need a minimum skill level in their tree; constellation look; perks are single-digit or
   mechanical; respec costs skulls. Trees: MELEE, BALLISTICS (machinegun/chaingun/nailgun),
   SCATTER (shotgun), EXPLOSIVES (rockets/grenades/prox/BFG), ENERGY (lightning/plasma),
   PRECISION (railgun + headshots), TRAVERSAL (wall run/jump, double jump, slide, zip, grapple),
   EVASION (dash, blink, dodging), SURVIVAL (damage survived, healing), CARNAGE (gibs, style).
   The HUD shows the current weapon's skill (small, by the ammo).
9. **Existing perks in DESCENT** follow the single-digit rule too (normal modes unchanged).
10. **More gameplay juice**: side-of-screen kill cam (picture-in-picture of notable deaths:
    gibs, headshots, multi-kills, boss phases), replays of the best kill at floor end,
    a nemesis (the bot that killed you most gets stronger and taunts; revenge pays).

Round 2 workers: ds-boss (1-5 boss parts + nemesis), ds-skills (8, weapon skill HUD),
ds-economy (3, 4, 6, 7, 9), ds-gore (2), ds-killcam (10 kill cam + replays).

## Economy (round 2 items 3, 4, 6, 7, 9; owner ds-economy)

Code: game/g_ds_econ.c, q3_ui/ui_ds_econ.c, numbers in bg_descent.h (`DS_SK_*`, `DSP_*`,
`ds_rewardMult`). Dev: game `dsecon` / `dsecon sim`, UI `dsdiff <1-5>`, `dsdiffmenu`,
`dsreset [2|confirm]`, `dsabandon [confirm]`.

**Difficulty switch (4).** Floor map, bottom right: DIFFICULTY button -> 5-level picker.
From the next floor on the bots' skill (node_skill, written at launch), the boss strength
(G_DS_ActsInit reads the run difficulty) and the reward multiplier follow it. Lives stay as
set at run start. Run keys: `difficulty` (current), `diff_start`, `diff_min` / `diff_max`
(lowest / highest difficulty a floor was actually LAUNCHED on). **Records** (best_floor_<d>,
won_<d>, cnt_hardwins, history) go to `diff_min`: a mixed run counts as the easiest
difficulty played (switching without playing a floor costs nothing). History writes
"2-4" for a mixed run and shows "BRING IT ON+".

Reward multiplier (`ds_rewardMult`, `BG_DS_RewardMult(d)`, game `G_DS_RewardMult()` = the
current run difficulty, 1.0 outside DESCENT): I Can Win 0.5, Bring It On 0.75, Hurt Me
Plenty 1.0, Hardcore 1.05, Nightmare 1.10. Applied to floor skulls, chest skulls, the
nemesis bonus (`G_DS_Skulls(n)`), gem xp (Loot_CollectGem) and skill xp (ds-skills).

**Skulls: income** (economy pass 2, 2026-10-02; `Econ_FloorSkulls` in g_ds_econ.c, numbers
`DS_SK_*` in bg_descent.h). Every source, before GRAVE COIN % and the difficulty multiplier:
- won floor: 3 + floor/3, 1 per 3 kills (max 4), style 0-3 (C 1, A 2, SS 3), node bonus as
  the map promises (elite x2, event x1.5, boss x3 of those three).
- boss floor (won or lost): the share of the boss's 10 lives YOU took x the bar (24 + 6 x act:
  act 1 = 30, 5 of 10 lives = 15); a lost boss floor + 2 for trying. Final blow (g_ds_boss.c):
  5 + floor/3. Pass 2 removed the separate boss kill bonus (10 + 5 x act): the floor's x3, the
  lives taken and the final blow already pay for the kill (it was a 4th layer on one event).
- lost floor: 1 per 3 kills (max 4), always less than the same floor won (a won floor needs
  8+ frags: 3 + 2 at least).
- chests with nothing left to level: 5 + floor/2; nemesis revenge: 3 + floor/4 (both x mult).
- GRAVE COIN passive +8% per level (an in-run trade: a passive slot for skulls). GRAVE ROBBER
  (+10% per rank, permanent) is retired: two stacking skull multipliers snowball and a
  permanent one is a dull buy.
- Run summary: "SKULLS FROM floors / kills / bosses / loot+blows / bonus & difficulty"
  (run keys sk_fl, sk_ki, sk_bo, sk_ot, sk_bn; they add up to the banked total).

**The model** (`dsecon sim` in game = tools/econ_model.py; per descent banked; every act's floors
1-5 fight, fight, elite, event, fight; won floors frags+1 kills and style B; 5 of 10 boss lives
+ the final blow on won boss floors, 3/4/4 lives on lost tries; 4 min a floor, 9 a boss floor;
lives 3/3/2/1/1, no SPARE SOUL, no GRAVE COIN):

| outcome | ICW 0.5 | BIO 0.75 | HMP 1.0 | HC 1.05 | NM 1.10 | HMP min | HMP /h |
|---|---|---|---|---|---|---|---|
| dies on floor 3 | 11 | 15 | 18 | 17 | 19 | 16 | 67 |
| dies at boss 1 (took 3-4 of 10) | 51 | 74 | 83 | 71 | 75 | 38 | 131 |
| beats boss 1, dies on floor 9 | 69 | 101 | 130 | 135 | 143 | 45 | 173 |
| dies at boss 2 | 117 | 175 | 211 | 204 | 214 | 67 | 188 |
| dies on floor 15 (act 3) | 143 | 214 | 276 | 288 | 302 | 74 | 223 |
| dies at boss 3 | 203 | 301 | 373 | 370 | 388 | 96 | 233 |
| dies on floor 21 (act 4) | 230 | 342 | 446 | 467 | 490 | 103 | 259 |
| dies at boss 4 | 296 | 438 | 555 | 560 | 587 | 125 | 266 |
| wins (banks after boss 4) | 308 | 456 | 602 | 634 | 664 | 116 | 311 |

Before pass 2 (same model): 18 / 78 / 141 / 215 / 302 / 390 / 491 / 591 / 670 on HMP (the boss
kill bonus made a beaten boss pay ~15-30 more; lost boss tries paid less: bar 18 + 4 x act).

Exploit checks: skulls per hour rise with depth (67/h on the first floors, 311/h for a win), so
farming early floors and abandoning (or dying) never beats pushing on, and a death never costs
the run's skulls. A lost floor pays at most 4 + its boss share. Easy difficulty: an I Can Win
win pays 308 in ~2 h (159/h): below Hurt Me Plenty as soon as you beat its first boss (173/h+),
above it only for a player stuck at the HMP Butcher (131/h); the 0.5x is the player's own
number, so I kept it, and records go to the easiest difficulty played anyway.

**Skulls: spending.** The permanent tree (14 upgrades, 30 ranks, 14,800 skulls) and skill
respecs (ds-skills: 25 + 5 per rank given back, per tree). Nothing else spends skulls.

| tier (opens at) | upgrade | ranks | prices | total |
|---|---|---|---|---|
| I FLESH, the body | IRON HEART +5 max health | 5 | 60 110 170 240 320 | 900 |
| | FLEET FOOT +2% speed | 5 | 60 110 170 240 320 | 900 |
| | WHETSTONE +3% damage | 5 | 70 130 200 280 370 | 1050 |
| II MARROW, the cards (5 ranks in I) | SECOND THOUGHTS +1 reroll | 3 | 250 450 700 | 1400 |
| | BIRTHRIGHT free starting ability | 1 | 400 | 400 |
| | FIRST PICK 5 cards in an act's first offer | 1 | 600 | 600 |
| | WIDER CHOICE 4-card offers | 1 | 900 | 900 |
| III IRON, the spoils (3 ranks in II) | TREASURE HUNTER +5% extra chest reward | 3 | 350 600 900 | 1850 |
| | ARSENAL weapons start at level 2 | 1 | 1000 | 1000 |
| | QUARTERMASTER a banked chest at every act's start | 1 | 1200 | 1200 |
| IV SOUL, the grave (3 ranks in III) | LAST BREATH +1 boss-floor life | 2 | 700 1200 | 1900 |
| | REVIVAL one forgiven loss | 1 | 1200 | 1200 |
| | SPARE SOUL +1 life | 1 | 1500 | 1500 |

Retired (they duplicated passives or skill perks; ranks refunded at the price paid, once, on
load: `UI_DS_EconMigrate`, meta `econ_v 2`, `econ_refund`): BONE PLATE (= ARMORER perk), GRAVE
ROBBER (= GRAVE COIN), LODESTONE / HUNGER / RABBIT'S FOOT / HAIR TRIGGER / BLAST CORE / IRON HIDE
(= the passives of the same name; HAIR TRIGGER also = RAPID CYCLE etc., IRON HIDE = IRON SKIN),
EVENT HORIZON (+6% of a 1.5% drop chance: not felt). Their enum slots stay (`DS_UP_RETIRED`).
Kept upgrades got dearer; owned ranks stay owned (nobody paid more than the new price, so no
refund). The player's own meta (0.5 save: health 4, damage 3, speed 1) keeps all 8 ranks,
nothing retired was owned.

Before pass 2: 20 upgrades, 54 ranks, 7,855 skulls, tiers I-II mostly copies of the passives.

**Pacing** (HMP, the model above): a new profile's first descent (dies at boss 1, 83) buys a
first rank (60-70); runs earning 80-170 buy a rank about every run early (60-130) and every
1-2 runs later in tier I (170-370); tiers II-IV are 250-1500 per step: every buy is a choice
between a stat rank now and saving for a tier rule-changer. Whole tree: a player who improves
(5 runs dying at boss 1, 7 in act 2, 10 in act 3, 13 in act 4, then wins) finishes at run ~44
(~60 h); a player who plateaus in acts 2-3 has ~10,000 after 50 runs and finishes around run
65-70 (the tree itself makes plateaus shorter). Before: 7,855 was done by run ~30 / ~41.
Perk points (skills) and character unlocks progress alongside without skulls.

**Reset all progress (7).** Records screen, RESET ALL PROGRESS (main menu only): two
separate confirm screens, then meta / run / history / rearmed_skills.txt are copied to
`rearmed_backup_<n>_<meta|run|history|skills>.txt` (first n with none of them) and emptied.
Nothing is emptied when a copy fails. **ABANDON DESCENT** (hub, main menu only): the run
ends as a LOST run, its skulls and records banked (cause "Abandoned the descent on floor N").

**Frag target (3).** `g_fragMult` 1-3 (archived, F10 RULES "Frag target: x1/x2/x3"):
`G_FragLimit()` = fraglimit x g_fragMult outside DESCENT, used by CheckExitRules and the
style match point; skirmish, SP arenas (fraglimit from the arena info) and team modes.
CS_REARMED `fl` carries the target; cgame uses it for cgs.fraglimit (scoreboard, HUD,
"N frags left", loading screen). SP awards don't depend on the limit.

**Perks in DESCENT (9)** (`DSP_*`, normal modes unchanged): THICK SKIN +8 max health,
HEAVY CALIBER +6%, QUICK HANDS fire 6% faster, OVERCHARGER charges 9% faster, BLOODTHIRST 5%
lifesteal, HEADHUNTER +9% headshot damage, SKY HUNTER +8%, GLASS CANNON +9% / -9 max health,
LAST STAND +9% dealt / -9% taken, KINETIC up to +8%, THORNS 8%, IRONCLAD armor up to 25,
ADRENALINE heals 5, GROUND ZERO +9% shockwave. Mechanical perks unchanged. DESCENT cards and
the Collection show these numbers (bg_rlCardDescs); normal perk offers keep bg_perkDescs.

## Round 3 (2026-10-05): more fun, more dopamine

Player picks (verbatim intent):
- Mid-fight: kill-chain FRENZY (chains build more gems, faster, louder), LOOT BEAMS (coloured light
  pillars on rare drops), LUCKY CRITS (glint + special sound), POWER SPIKES (short berserk /
  overdrive bursts).
- Between floors: CARD RARITY REVEAL (cards flip, legendaries shine and roar), LEGENDARY
  build-defining cards (rare, game-changing), SYNERGY DING (a loud moment when a combo / evolution
  key completes), FLOOR-END TALLY that counts up like a slot machine.
- Surprises: MID-FLOOR AMBUSH mini-bosses, PICK-YOUR-POISON floor modifiers for more loot,
  SECRET stashes / hidden pickups on maps, RARE WILD EVENTS (a meteor of gold, a bot uprising),
  the GHOST of a past run as a bot.
- Cap rule: the single-digit rule bends ONLY for short temporary bursts (power spikes, crits,
  frenzy peaks, a few seconds). Permanent upgrades and legendary cards stay single-digit or
  mechanical: legendaries are game-changing by what they DO, not by big numbers.
- Not chosen: gib piñatas, treasure runner, level-up shockwave, shrines, devil's deals, set
  bonuses, bigger chests, pity timer, gold rush floors, cursed merchant.
- Still applies: keep the centre of the screen clean during play (big moments at floor end, or
  short and at the edges); everything has a switch; enemies scale with the player's power.

Round 3 workers: ds-frenzy (frenzy, crits, power spikes, loot beams), ds-cards (rarity reveal,
legendaries, synergy ding, floor-end tally), ds-surprise (ambushes, floor modifiers, wild events,
ghost of a past run), ds-secrets (secret stashes on maps).

### Round 3 extra: map variety (ds-maps, 2026-10-06)
Player: "never the same level twice, better variety, I keep playing the beginning ones". No map
repeats within a descent (run keys `ds_played`, `ds_fm`; REVISITED only when every map was played),
lifetime play counts in meta (`dsm_plays`, `dsm_last`, `dsm_v`) weight picks toward fresh maps
and the floor's size, NEW tags, act pools of 14-15 maps (incl. tourney, CTF-as-FFA, pro, Team Arena
maps copied from the player's install), floor variants (`node_variant` = light | layout << 4, CS_DESCENT
`mv`; `g_dsVariants`, `cg_dsVariantLight`). Contract: game/bg_ds_maps.h; UI q3_ui/ui_ds_maps.c (rlLayer_t
lives in ui_ds_maps.h now), game g_ds_maps.c, cgame cg_ds_maps.c. Sim: UI `dsmapsim`.

## Round 4 (2026-10-13): enemies

Player picks (everything offered was picked):
- **Brains (ALL modes, en-brains):** flank and pincer you; retreat to heal when hurt; dodge your
  rockets and rails; rocket-jump and strafe-jump; fight over powerups and armour (item timing);
  hunt you by sound; learn your habits during a run / match; gang up on whoever leads; lure you
  into traps and explosive barrels. Bot skill still matters: lower skills use less of this.
- **Types (DESCENT, en-types):** Sniper, Rusher, Medic, Shield bearer, Kamikaze, Blinker,
  Summoner, Mimic, Juggernaut (`dsArchetype_t`). A distinct look per type (silhouette, skin,
  weapon, scale), telegraphed big attacks. Power-budgeted: a type changes HOW a bot fights.
- **Affixes (DESCENT, en-affix):** Vampiric, Explodes on death, Splits in two, Reflects some
  damage, Frost slows you, Fire trail, Pulls you in, Regenerates; champion packs of 3 (`DSAF_`).
- **Hit feel (ALL modes, en-feel):** flinch and stagger; heads / helmets / limbs blown off;
  crawl when nearly dead; panic and flee (en-feel breaks morale, en-brains moves it); knockback
  into walls (wall splats); armour that cracks visibly; unique death screams.
- **Personality (en-personality):** taunts and trash talk; grudges against whoever killed them
  (all modes); a nemesis that levels up each time it kills you (DESCENT); bots rank up through a
  run (DESCENT, a persistent roster of named veterans); telegraphs (with en-types).
- **Bosses (en-boss), revised again by the player:** TIMED 1v1. Just you and the boss, no rival
  bots, no adds. Kill it within 5:00 (`g_dsBossTime`); die as often as you like, the timer is the
  only loss condition. The 10-lives many-vs-1 version stays as an option (`g_dsBossMode 1`).
- Still applies: keep the centre of the screen clean (tells live ON the enemy: shells, glints,
  sounds, particles; names / types at the edges, the arrival callout, TAB); every feature has a
  switch; the single-digit rule binds the player's permanent upgrades, not enemy numbers, but
  enemy power still follows G_DS_PowerScore.

Contract (mine): game/bg_ds_enemy.h (`dsArchetype_t` DSAR_*, `DSAF_*` affixes, `DSEF_*` flags,
CS_DSENEMY 743), game/g_enemy.c (`g_enemy[]` gEnemy_t in g_descent.h, `G_EN_*`, published once a
frame), cgame/cg_enemy.c (`CG_EN( client )`). `gEnemy_t.spare[]`: claim a slot here before use.

| Worker | Owns | New files |
|---|---|---|
| en-brains | bot tactics + habits, all modes; moves panicked / grudging bots; calls type behaviour hooks | game/ai_tactics.c (+ more ai_*.c of its own) |
| en-types | archetypes: spawn mix, kits, looks, telegraphs, type behaviour (via hooks en-brains calls) | game/g_ds_types.c, cgame/cg_ds_types.c, game/ai_types.c |
| en-affix | affixes, champion packs, their tells | game/g_ds_affix.c, cgame/cg_ds_affix.c |
| en-feel | flinch/stagger, dismemberment, crawl, morale, wall splats, armour cracks, death screams | game/g_hitfeel.c, cgame/cg_hitfeel.c |
| en-personality | taunts, grudges, nemesis levels, veteran ranks | game/g_ds_rank.c, game/ai_taunt.c (+ g_ds_nemesis.c) |
| en-boss | timed 1v1 boss floors, rebalance, timer HUD | g_ds_boss.c, cg_ds_acts.c (existing) |
| en-daily | DAILY CHALLENGE: a fixed act-1 descent per date, score, personal bests (rearmed_daily_run.txt, rearmed_daily.txt) | game/bg_ds_daily.h, game/g_ds_daily.c, q3_ui/ui_ds_daily.c |

gEnemy_t spare slots claimed: spare[0] en-types (level.time when THIS hit was a frontal shield block; reset at the start of every hit on a shield bearer, read by G_HF_Damage right after);
spare[2] en-feel (client + 1 that broke its morale, 0 = none: PANIC flees from it), spare[3] en-feel (limbs
lost: 1 left arm, 2 right arm, 4 legs). en-feel event: EV_HITFEEL (end of entity_event_t, eventParm HFX_*).

Daily (en-daily): cvar ds_daily = a daily is being played (UI: UI_DS_DailyOn, game: G_RL_Daily). While it is on the
run file is rearmed_daily_run.txt, meta reads are a blank profile, nothing else is written (meta, history, skills, tally,
ghost, style records); the boss floor is always timed 300 s. A plan that should repeat for everyone on a date must
come from the run seed + floor (never rand()); G_DS_DailyRand() is the seeded chest stream. ID_RL_DAILY 23.
IDs claimed in round 4: en-affix RM_ENT_AFFIXFX 84 (game/bg_ds_affix.h), CS_DSAFFIX 744; en-types RM_ENT_DSTYPE 86 (game/bg_ds_types.h).
en-memory (bot memory + NEMESIS HALL): file rearmed_bots.txt (game-owned, written only at floor ends, never on daily /
practice; contract game/bg_ds_botmem.h), API G_BM_* in g_descent.h (game/g_ds_botmem.c), PRC_RIVAL 5 (bg_ds_personality.h),
ID_RL_HALL 26 (ui_ds_meta.h), q3_ui/ui_ds_hall.c (UI_DS_HallMenu, `dshall`), cvar g_botMemory 0/1/2, dev `dsbm`.
en-bestiary: server command `dsbst <entry>`, run keys bst_fm / bst_fk / bst_rm, meta keys bst_seen / bst_aseen / bst_xseen,
bst_k / bst_ak / bst_xk, bst_d / bst_ad / bst_xd; the shared enemy name / tip table is game/bg_ds_bestiary.[ch] (all three modules).
en-brains: flag DSEF_THIEF 0x0200 (bg_ds_enemy.h, a bot carrying the human's stolen weapon), server cmd `thc` (cgame/cg_thief.c);
bot hooks in game/ai_tactics.h (BotType* implemented by en-types in ai_types.c; G_EN_GrudgeTarget read for grudges).
F10 menu (2026-10-13): MAX_MENUITEMS 192; two new tabs, BOTS (RMTAB_BOTS: en-brains <=6, en-personality 5,
en-types 1-2, en-affix 2, en-boss 2) and HITS (RMTAB_HITS: en-feel). Tab centres come from the label widths
(Rearmed_TabX, order rm_tabOrder). DESCENT and RULES tabs are full.
en-personality: no gEnemy_t spare slots; g_enemy[].rank / grudge / grudgeUntil, DSEF_NEMESIS / DSEF_GRUDGE. API in g_descent.h
(round 4 PERSONALITY block): G_EN_GrudgeTarget, G_PR_Say / G_PR_SayTo (rate-limited bot lines, prio 0-2) / G_PR_Bark,
G_DS_RankFor, G_DS_NemesisLevel / Title / Is. Server cmds `prb` (bark) / `prc` (edge callout), contract game/bg_ds_personality.h
(prBark_t, PRC_*). Run keys vet_roster, nemesis_level / nemesis_title / nemesis_affix; CS_DSINTRO keys vr, vm, nl, nt;
CS_DESCENTBOSS keys nl, nt. The stock ai_chat.c bot chat is off (G_PR_StockChatOff).
en-boss (timed 1v1 boss floors, 2026-10-13): `g_dsBossMode` 0 timed (default) / 1 the round 2b 10-lives many-vs-1;
`g_dsBossTime` seconds (300, clamped 60-1800); both on F10 > BOTS. Timed: G_RL_InitBots plans only the boss
(G_DS_BossTimed()), ds-arrivals gives a lone boss its entrance (numStart 0: pillar, 1100+ units, DSA_GAP_BOSS),
ds-surprise already skips boss floors (no ambush / event / ghost / modifier). One pool in `phases` segments
(segment = ds_bosses[].health x difficulty x actsCalTimed x power x health side x g_dsBossScale; an empty segment =
roar + next phase, no death; bossLivesLeft is 1 so the last segment is the real death). The clock starts 3 s after
the boss's first spawn (DS_FIGHT_DELAY, when it opens fire); the last 60 s enrage it (cooldowns x0.75, attack gaps
x0.8, speed x1.06); at 0:00 timeUp -> G_DS_FloorResult 2 (2.5 s later). Player deaths unlimited; respawn 700-1400
units from the boss with a 2.5 s grace (existing); the 600-unit relocation rule is unchanged. DECISION: the boss does
NOT heal when it kills you (a death already costs ~7 s of the clock: 2.3 s respawn + the walk back), so the clock is
the only pressure. Time bonus: a skull per 15 s left (G_DS_BossTimeBonus, paid by g_ds_econ.c, CS_DESCENT `tb`, the
tally chip TIME LEFT). G_DS_BossProgress (failed-loop pay) = the share of the bar removed. CS_DESCENTBOSS timed keys:
tm 1, tl limit s, ts / te clock start / end (level times; te 0 = not started), kt kill msec, lm last minute, tu time
up, tb bonus skulls, pr practice; blm 0 and no ro in timed mode. Records: cgame CG_RecordsBossKill -> rearmed_records.txt
`arena boss_<type> <records difficulty> <msec>` (not in practice / the daily). API (g_descent.h): G_DS_BossTimed,
G_DS_BossTimeLeft (msec left; frozen at the kill; -1 off a timed floor), G_DS_BossTimeBonus, G_RL_Practice.
BOSS PRACTICE (q3_ui/ui_ds_bossprac.c): `dsbossprac [1-4]` / hub BOSS PRACTICE (ID_RL_PRACTICE 22) writes only
rearmed_practice.txt (run key `practice`) and sets g_dsPractice; the game then loads that file and writes nothing
(RL_Save, tally, skills, ghost all return; cgame records off via CG_DS_Practice), skips tally / chests / cards and
runs `dspracend <result> <msec left> <bar %>` for the picker. g_dsPractice is cleared at the main menu and on every
real floor launch. Dev: `dsbosstime <s>`, `dsphase` (timed: empties this phase). Sim: tools/boss_sim.py --mode timed
+ --model; tests tools/boss_test.sh, tools/boss_prac_test.sh, cfgs in tools/boss_fixtures.
en-rematch (BOSS REMATCH, 2026-10-13): a rematch = a BOSS PRACTICE floor whose rearmed_practice.txt also says `rematch 1`,
`rm_mods <DSRM_ mask>`, `rm_prev <best ms at this heat>` (UI_DS_BossPracLaunch(boss, extra) in ui_ds_bossprac.c). Contract
game/bg_ds_rematch.h (DSRM_ bits IRON 1 FURY 2 HALFCLOCK 4 NOHEALTH 8 GLASS 16 ENRAGED 32 ONELIFE 64, heat table, max 16).
Unlock = meta cnt_boss<n> > 0. Game API (g_descent.h): G_DS_Rematch, G_DS_RmHealthScale, G_DS_RmTimeLimit, G_DS_RmPace,
G_DS_RmEnraged, G_DS_RmFallen (ONE LIFE goes through the time-up block), G_DS_RmItemAllowed (G_RearmItemAllowed),
G_DS_RmHealthBonus (G_RL_MaxHealthBonus), G_DS_RmPowerScore (Acts_PowerScale: GLASS doesn't shrink the boss),
G_DS_RematchInfo (CS_DESCENTBOSS rm / rmh / rmp), G_DS_RematchEnd (`dsrmend <boss> <mask> <result> <kill ms> <bar %> <deaths>`
instead of dspracend). UI q3_ui/ui_ds_rematch.c owns rearmed_rematch.txt (last / best <b> <heat> <ms> <mask> / stats; in
Reset All Progress). cgame/cg_ds_rematch.c: the block left of the boss bar, CG_DS_RematchOn / CG_DS_RematchFallen.
Tests: tools/rematch_test.sh <cfg> <none|two|all> (home_test/rematch; cfgs tools/rematch_cfgs/gen.py: rm_locked, rm_fights,
rm_onelife, rm_pace, rm_entry). Dev: dsrematch [<boss> <mask>], dsrminfo.
en-tune (2026-10-13): low-skill tactic gate `BotTacGate` (ai_tactics.c; skill 1 stock .. 4+ always), `bt_split 3` / `bt_bench 1` stand-in sims (tools/tune_sweep.sh). TYPE ADAPTATION `g_dsTypeAdapt`: run key `tad_f` (game, floor end), meta keys `tad_w` (x10) / `tad_cur` / `tad_seed` (q3_ui DSM_TypeAdaptMerge only), serverinfo `g_dsTypeAdaptNow` "<floor>:<arch>,..", `G_DS_TypeAdapted(arch)`, `G_RL_MetaGet(key)`; right-edge slot EDGE_TYPES. Juggernaut hit box `G_DS_TypesBounds` (g_active.c after G_DS_BossBounds) + headshot line `G_DS_TypesHeadLine` (g_combat.c).
en-review (round 4 cleanup): shared helpers to use instead of new copies: G_EN_CleanName (game, g_enemy.c), CG_CleanUpper /
CG_ClientNameUpper / CG_EdgeText / CG_EdgeCard (cgame, cg_edge.c: the right-edge title + body card), ds_roman[] (bg_descent.c),
BG_DS_AffixName (bg_ds_bestiary.c; the bestiary's archetype names come from ds_types[]), G_RL_NoSave() (practice or daily) /
CG_DS_NoRecords(). G_DS_AffixSpentNow(client): affix bits a death spent this frame (SPLIT). Round-4 dev commands that change the
match or progress need devmap / developer 1 once two humans are connected (g_svcmds.c svDevCmds). va() has two buffers: copy a
va() result (or a helper that returns one) before a second va() in the same call.
