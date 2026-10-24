# REARMED reference

Every feature, setting and console variable in detail. New here? Start with the [README](../README.md).

An arena mod for Quake III Arena, built on ioquake3. To play, run **`Play Rearmed.bat`**. Press **F10** in game (or pick REARMED in the main menu) to open the settings: every feature below has its own switch.

The game data is read from your Steam install (`H:\SteamLibrary\steamapps\common\Quake 3 Arena`), which the mod never modifies. The mod's configs, saves and screenshots go in `home\rearmed\`.

## Controls (new)

| Action | Default key | Command |
|---|---|---|
| Alt-fire | Mouse 2 | `+button12` |
| Offhand grapple (hold) | E | `+button13` |
| Dash (2 charges) | Q / Shift | `+button14` |
| Nailgun / Prox launcher / Chaingun | F / G / V | `weapon 11/12/13` |
| REARMED settings | F10 | `rearmed` |
| Weapon wheel (hold, pick with the mouse, release; tap = last weapon) | R | `+wheel` |
| Double jump | jump again in the air | |
| Wall jump | jump next to a wall in the air | |
| Wall run | keep jump held while running fast along a wall (not into it); tap jump to kick off | |
| Slide | crouch (C) while running; jump out of it to keep the speed | |
| Ground pound | crouch (C) in the air, 96+ units above the floor | |
| Zip line | Z: aim at a wall or ledge and press it to ride a cable there; jump to let go | `+button15` |
| Pick a perk / card | 1 / 2 / 3 when the cards are up | `weapon 1..3` |

On a gamepad, the right stick steers the weapon wheel while it's open.

Jump assist gives a short coyote time after walking off a ledge. A jump pressed just before landing waits for the ground instead of using up the double jump.

You can rebind all of these in Setup > Controls. The defaults are applied once, on the first launch.

### Zip line

Aim at a wall, a ledge or a floor 96 to 2000 units away and press **Z** (Setup > Controls > Move > zip line). A cable shoots out and pulls you straight there at up to 1000 ups. You can look around and shoot while riding. Jump (or Z again) lets go with your momentum; arriving drops you off with a small hop.

- **Small cyan brackets `[ ]` around the crosshair** mean a press would hold right now. No brackets: nothing to hold on to, you already zipped this jump, or you don't have the zip line (`cg_zipHud 0` hides them).
- The cable flies past players, bodies and barrels and holds on the wall behind them, so you can zip at an enemy.
- It doesn't hold on the sky, closer than 96 units or beyond 2000. If a press doesn't hold you hear a dry click and a short reason shows under the crosshair: NO WALL IN RANGE, TOO CLOSE, LAND TO RELOAD, GET THE CARD FIRST (DESCENT) or ZIP LINE IS OFF.
- One zip per jump: landing, swimming or a grapple pull reloads it.
- A low lip, railing or player in the way is stepped over; a wall or a gap too small for you knocks you off.
- In DESCENT you need the ZIP LINE card; the card names the key you bound. Elsewhere it's on with `g_zipLine 1` (F10 > MOVEMENT).

## Alt-fire

**Tap** alt-fire (right mouse) for the quick version. **Hold** it to charge (up to 1.2 s; a bar under the crosshair fills, and a blip sounds when it's full), then release for the charged version. Turn charging off with `g_chargedAlt 0`; then alt-fire fires on press, as before.

| Weapon | Tap | Hold (charged) |
|---|---|---|
| Gauntlet | lunge | rocket punch: a much longer lunge; a hit right after it does up to double damage and launches higher |
| Machinegun | 3-round burst | marksman shot: one perfectly accurate round, 20–60 damage |
| Shotgun (wind) | air blast: a cone of wind that throws players back and turns projectiles around (they become yours) | gale: longer range and much stronger push |
| Grenade launcher (fire) | sticky grenade (sticks to players, 1 s fuse) | napalm: bursts into a ring of fire |
| Rocket launcher (earth) | detonate your rockets in flight | boulder: a slow, heavy rocket; its quake launches everyone standing nearby |
| Lightning gun (electricity) | chain arc to 2 nearby enemies | thunderstrike: lightning from the sky where you aim, damages and shocks everyone around |
| Railgun | overcharged ricochet (2 slugs) | lance: 100–200 damage and goes through a wall up to 80 units thick |
| Plasma gun (water) | small whirlpool that drags and soaks | big whirlpool (radius, pull and duration grow with the charge) |
| BFG | triple spread | overload: one huge ball with twice the laser reach and targets |
| Nailgun | ricochet stake | pinning spike: pins the victim in place for up to 1.4 s |
| Prox launcher | detonate your mines | minefield: 3 mines in a fan |
| Chaingun | one accurate shot | spins the barrel up without firing; hold both triggers to fire fully spun |

## Elements

With `g_elements 1` (the default), five weapons carry an element, and the elements react to each other:

| Element | Weapon | Effect |
|---|---|---|
| Fire | grenade launcher | grenades leave burning ground; burning players take damage over time (`was incinerated`) |
| Water | plasma gun | soaks players: a little slower, can't burn, and conducts electricity. Standing in water soaks you too |
| Electricity | lightning gun | shocks (a brief stagger). On soaked players it does 1.5x damage and arcs to other soaked players nearby |
| Wind | shotgun alt | pushes players, reflects projectiles, blows out fires and burning players |
| Earth | rocket launcher | explosions throw grounded players into the air: juggle setups |

Water puts out fire (with a hiss of steam), and fire dries you off.

Headshots (machinegun, shotgun, railgun, nailgun, chaingun) do 1.5x damage and play a sound.

## Weapon feel

| Weapon | Behavior |
|---|---|
| Rocket launcher | airborne enemies take 30% more knockback from explosives, so you can juggle them |
| Shotgun | recoil pushes you back; aim at the floor to shotgun jump |
| Chaingun | spins up from ~9 to ~35 shots/sec over about 1.2s of holding the trigger |
| Nailgun | nails ricochet twice off walls (the alt-fire stake ricochets 4 times) |
| Railgun | overcharged alt-fire shots ricochet twice (each target hit once; 70% then 40% damage after each bounce) |
| Plasma alt | the orb is a singularity that drags nearby enemies in, then detonates |
| BFG | the ball zaps up to 3 nearby enemies with lasers as it flies |
| Gauntlet | hits uppercut the victim into the air; hitting within 0.7s of a lunge launches much higher (LUNGE COMBO) |

Feedback:
- Hitmarkers with a tick sound that deepens as damage goes up. On a kill the marker turns red and a kill-confirm sound plays.
- A direct projectile hit on an airborne player is an **AIRSHOT!**: 1.5× knockback plus a sound sting.
- View kick and gun recoil on every shot.
- Gibs burst into blood.

- Damage direction arrows around the crosshair, a low-health pulse, gold headshot markers, an armor-break sound.
- Deaths leave pools and splatters of blood that stay until the map ends (`cg_gorePersist`, see Physics and gore).

Toggles: `cg_hitMarkers`, `cg_damageIndicator`, `cg_weaponKick 0..1`, `cg_lowHealthFX`, `cg_dashHud`, `cg_zipHud`, `cg_rearmAnnouncer`.

Testing helpers (server console, cheats on): `teleport <client> x y z [yaw] [pitch]`, `where`, and `bot_pause 1`.

## Game modes

Pick a mode under **Mode** in the Start Server / Skirmish options, or set `g_rearmMode` and restart the map.

| Mode | Rules |
|---|---|
| Normal | regular deathmatch |
| Gun Game | Every frag moves you up an 11-weapon ladder: MG → SG → CG → NG → PG → LG → GL → RL → RG → BFG → Gauntlet. A gauntlet kill wins, and getting gauntleted knocks you down a level. There are no weapon pickups and ammo is infinite. (FFA only.) |
| Juggernaut | The first frag makes you the juggernaut: 300 health, 150 armor, permanent quad and a red beacon. Only kills by or on the juggernaut score, and whoever kills the juggernaut becomes it. (FFA only.) |
| Instagib | Railguns only, and every hit kills. Overcharged alt-fire shots ricochet. |
| One in the Chamber | One rail slug that kills in one hit. A kill reloads one slug (max 5); otherwise you have the gauntlet. (FFA only.) |

**Frag target** (F10 > RULES, `g_fragMult` 1/2/3, default x1): multiplies the frags needed to win every normal match: skirmish, the single player arenas (their own limit, e.g. 10 on Arena Gate becomes 20 at x2), team deathmatch and online. The scoreboard, the frag box in the HUD, the "frags left" warnings and the loading screen show the multiplied target; the single player end of match and awards work as before. DESCENT keeps its own frag races.

## DESCENT (roguelike mode)

Pick **DESCENT** in the main menu (or type `descent`). It's a run of short frag races against bots down 24 floors in four acts, each ending in a boss, then the endless Abyss. Separate from the normal campaign.

- **You start with nothing:** no double jump, wall jump, wall run, dash, slide, ground pound, grapple, zip line or alt-fire. Coyote time and jump buffering stay on.
- **Win a floor, pick 1 of 3 cards** (keys 1–3, mouse wheel and fire; Space rerolls if you have rerolls):
  - abilities (green)
  - alt-fire for one weapon (orange), plus charged alt-fire once you own an alt
  - passive items (teal): new, or +1 level of one you hold
  - +1 level for a weapon you have used this run (red-orange, shows what the next level adds)
  - any of the 26 perks (purple)
  - secret cards (gold), once you've unlocked them
  - A card that moves an evolution along or combos with what you hold gets a glowing gold ribbon: EVOLVES ROCKET LAUNCHER, KEY FOR HELLFIRE, COMBO: BLINK + MIRAGE. Passive cards show the level step (SPITE 2 -> 3).
- **The bots grow with you:** every bot gets as many cards as you hold, from the same pool.
- **Loading screen:** the floor, the act, the goal (frags to win, or ELIMINATE THE BUTCHER on a boss floor) and this floor's opponents, the way the single player arenas present them: each one's head portrait (from your own Quake 3 install), name and a one-line threat note: weapon level and abilities (LV4  9 ABIL), ELITE  LV6  3 EVO (evolved guns), NEMESIS  4 KILLS, RIVAL  LV3. A boss floor shows the boss big with its title and its two strong sides (FAST + BRUTAL, then the weak one), the two rivals smaller.
- **Floor start: the versus intro** (`cg_dsIntro`, F10 > HUD > Enemy intro, default on). For about 3.3–4 s a strip along the top edge (never the middle of the screen): YOU slides in from the left, VS, the opponents slide in from the right one after another, and the announcer reads the matchup: the bots' names in your Quake 3 announcer's own voice (Sarge.wav ...), "Elite!" / "Your nemesis!" before those, then "Fight!". Boss floors: a roar and "The Butcher!", the big boss block, then "Rivals!" and the two rivals. The portrait being read swells and lights up. **Fire skips it** ("Fight!" at once). It replaces the old floor strip; the act card and the boss bar come right after it. With `cg_dsIntro 0` you get the slim strip below instead. Skirmish (free for all, tournament, single player arenas) gets the same top strip once the bots are in at the match start (names and "Fight!" in free for all; the arenas and tournaments have their own announcer). Dev: `dsintro` (replay), `dsintro skirmish`, `dsintro loading [ms]` (shows the loading screen again for a screenshot).
- **Enemies join one by one** (`g_dsStagger`, F10 > DESCENT, default on; 0 = everyone at once as before): a floor starts with you and the first opponent (two when 5+ bots are planned). The rest arrive one at a time, about 6.5 s apart near the top and about 4.3 s deep down (4–7 s). Each arrival gets its own moment: a warm pillar of light on a spawn point at least 600 units from you (usually 900+ and out of sight), the announcer reading that one name ("Elite!" / "Your nemesis!" / "Rival!" first), the portrait sliding into a slim strip along the top edge, and 1.5 s later the bot lands there with a spawn flash. The strip shows a dim "?" for everyone still to come and collapses once all are in. The regular bots come first, then your nemesis, the elite last. Boss floors: the two rivals, then the boss with a roar, its name and the full versus row for a moment (it lands 1100+ units away; the boss bar comes after). A late joiner starts with the lowest score of the bots already in, so the frag race stays real; the target doesn't change.
- **Floor start with `cg_dsIntro 0`:** a slim strip at the top of the screen for about 3.5 s shows the floor, the node, frags to win, enemies and lives, one row of what the enemies can do (their abilities, alt-fire and perk counts), and the elite or boss. Hold the scoreboard key to see every bot's full kit (next to the scoreboard on wide screens, under it on 16:9). The run block in the top-left corner shows only the floor, lives, frags and Skulls; your perks show as coloured pips, with the full list on the scoreboard.
  - Elite floors have one bot with 2 extra cards and an orange aura.
  - The last floor of every act is that act's boss (see Acts and bosses).
- **The floor map:** choose your next fight each floor:
  - Fight
  - Elite (from floor 3, double Skulls)
  - Event (a forced random event, 1.5× Skulls) that lasts the whole floor: its plate says THIS FLOOR instead of a timer
  - Boss (the act's boss, 3× Skulls)
  - ??? (rare: something glitters down there)
  Bots (2 to 5), skill and frags-to-win (8 to 16, about 3–4 minutes a floor) rise as you go deeper. A thin line and the act's name mark where a new act begins; the boss's name sits on its row.

### Acts and bosses

- **Four acts of six floors**, each its own biome with its own map pool (installed maps with a bot .aas file only; an act with fewer than 3 of its maps uses every map), a colour grade, a tinted haze at the screen edges, ambient particles and music from your own Quake 3 install. Pools by theme and size (S small, M medium, L large; see Map variety below):
  - ACT I, THE CRYPTS (14): q3dm0–7, q3dm9, q3tourney2, pro-q3tourney2, rm_camp, mptourney1, mptourney3. Dust. Boss: THE BUTCHER
  - ACT II, THE FOUNDRY (15): q3dm6, q3dm10–12, pro-q3dm6, q3tourney1, q3tourney4, pro-q3tourney4, q3tourney5, q3ctf1, rm_camp, mptourney2, mpteam1, mpteam2, mpteam4. Embers. Boss: THE OVERSEER
  - ACT III, THE VOID (14): q3dm10, q3dm11, q3dm16–19, q3tourney6, q3tourney6_ctf, q3ctf4, rm_yard, pro-q3dm13, mptourney4, mpteam6, mpteam7. Drifting void motes. Boss: THE HOLLOW KING
  - ACT IV, THE PIT (14): q3dm7, q3dm8, q3dm12–15, q3tourney3, q3ctf1–3, pro-q3dm13, rm_lost, mpteam4, mpteam7. Ash. Boss: THE ARCH-DEMON
  - The Abyss: all 47. Without Team Arena the pools are 12 / 11 / 11 / 12 and the Abyss 38.
  A new act opens with a short card at the top right (ACT II, THE FOUNDRY and a line of flavour, about 3.5 s) and a low sting. `cg_dsBiome 0` turns the grade, haze and particles off (`2` makes them stronger); `g_dsActMusic 0` keeps every map's own music.
- **Bosses** are big (drawn and hit larger) and fight like a bot plus their own attacks. Every big attack is announced at least 0.8 s ahead (0.8–1.3 s; fast bosses warn a little quicker, slow ones longer): a warning sound, the attack's name under the boss bar, and a mark in the world (a line, a lane, a circle on the floor, a glow where something will appear). Nothing hits instantly. The meat hook's and the cleaver's aim locks 0.4–0.45 s before they fly, so stepping aside after the lock works.
- **Boss floors are on small maps** (tourney size) so the boss is never far: any act's small map can host a boss (it keeps the act's look); a medium map only when every small one was played this run, never a large one. The boss locator (below) shows where it is anyway.
- **Boss fights are TIMED 1v1** (round 4, the default, `g_dsBossMode 0`): just you and the act boss, no rival bots, no adds, no ambushes, wild events or ghosts. **Kill it before the clock runs out** (`g_dsBossTime`, 5:00 by default; F10 > BOTS: DESCENT boss clock 3:00-10:00). **Die as often as you like:** about 2 s after a death you're back at a spawn point 700+ units from the boss and out of its sight where the map allows, facing it, with the guns you had, and for 2.5 s nothing of the boss's can hurt you. The clock is the only way to lose: at 0:00 it's TIME UP and the floor is lost (a run life; the boss Skulls for the share of the bar you took are still paid).
  - The clock starts once the boss has made its entrance (a pillar of light 1100+ units away, then 3 s before it opens fire). It sits right of the boss bar at the top, big: warm white, **amber in the last minute**, **red in the last 30 s** with a tick every second (a heavier one with tenths of a second in the last 10) and a draining red line under it. **The last minute enrages the boss**: a sting, attacks 25% sooner and 6% faster on its feet (ENRAGED under the bar).
  - One health pool in **three phases** (the bar has marks at the thirds); each phase is a third of the pool (the ARCH-DEMON's armoured phase holds more). Emptying a third doesn't kill it: it ROARS where it stands (a short invulnerable moment, a knock-back, a gem burst) and the next phase starts.
  - **The boss doesn't heal when it kills you:** a death costs time (the respawn, the walk back, the grace), never progress, so the clock stays the only pressure.
  - **Win:** a big chest, red gems, the FINAL BLOW Skulls (5 + floor / 3) and a second chest, the boss bar's Skulls in full, plus a **time bonus**: a Skull per 15 s left on the clock (the floor tally shows it as TIME LEFT +n). SLAIN IN 4:12 under the bar, and your **fastest kill per boss and difficulty** is a record (NEW RECORD (WAS 4:40); `records` in the console lists them as boss_0-3).
  - The corner block says TIME 4:12; no lives skulls, no roster line.
- **BOSS PRACTICE** (DESCENT > PRACTICE / REMATCH, the bottom row of the hub, or the console: `dsbossprac 1` for the Butcher, 2 the Overseer, 3 the Hollow King, 4 the Arch-Demon, `dsbossprac` alone for the picker): straight into a timed boss fight with a typical kit for a player who reaches that boss (the cards, passives, weapon levels and upgrades the balance sims use), Hurt Me Plenty, your `g_dsBossTime` on the clock. **Nothing is saved:** the fight is described in `rearmed_practice.txt` and the game writes none of your files (run, meta, history, tally, skills, ghost, records): no Skulls, no records. Afterwards the picker comes back with the result (THE BUTCHER SLAIN - 0:41 LEFT ON THE CLOCK / TIME UP - it had 23% left); AGAIN on the same boss, or LEAVE.
- **BOSS REMATCH** (DESCENT > PRACTICE / REMATCH > REMATCH, or the console `dsrematch` for the screen, `dsrematch <1-4> <mask>` straight in): fight again a boss **you have beaten in a real descent** (the meta's kill count; daily challenges and practice don't count). Locked bosses are listed with how to unlock them ("Beat it in a descent to unlock: ACT II boss, floor 12"). Pick a boss, then stack **harder modifiers**, each worth heat (TOTAL HEAT n / 16):
  - **IRON** +2: the boss has +30% health. **FURY** +2: its big attacks come 25% sooner (gaps and cooldowns x0.75). **HALF CLOCK** +3: half the clock (2:30 of 5:00). **NO HEALTH** +2: no health pickups on the map. **GLASS** +2: your max health is halved. **ENRAGED** +2: the last-minute enrage from the first second. **ONE LIFE** +3: die once and the rematch is lost.
  - Mask bits for `dsrematch`: IRON 1, FURY 2, HALF CLOCK 4, NO HEALTH 8, GLASS 16, ENRAGED 32, ONE LIFE 64 (all = 127).
  - **Best times:** your fastest kill per boss at each heat is kept in `rearmed_rematch.txt` (apart from your descent records in `rearmed_records.txt`). The screen lists them by heat with the modifiers used, your best at the heat you picked, tries and wins. In the fight a slim block left of the boss bar shows REMATCH HEAT n, the modifiers and your BEST at this heat; at the kill it turns into NEW BEST -3.2 S (or +4.1 S OFF BEST). The end screen gives the full result (NEW BEST - THE BUTCHER IN 1:23.4 AT HEAT 9 (-3.2 S) / TIME UP / FALLEN - ONE LIFE).
  - Like practice, a rematch saves nothing else: no Skulls, records, run, meta, history, skills or tally.
- **The 10-lives option** (`g_dsBossMode 1`, F10 > BOTS: DESCENT boss fights "10 lives, many vs 1"): the round 2 version below. **A boss floor is many against one:** you, **two rival bots** (normal kits for the floor, matched to your power like any floor) and the act boss. The rivals go for the boss most of the time (about 65%) but they fight each other and you too; the boss fights everyone, and its big attacks lean a little toward whoever hurt it last. **Everyone has 10 lives** (`g_dsBossLives`, F10 > DESCENT, any difficulty; the boss always 10). A fighter out of lives is eliminated: a rival or the boss bursts and leaves (a small ELIMINATED line at the right edge); you out of lives = the floor is lost (a run life, like any lost floor). After a death you're back in about 2 s at a spawn point away from the boss and out of its sight where the map allows (700–1400 units), facing it, with the guns you had (at least half their ammo); for 2.5 s nothing of the boss's can hurt you and it doesn't shoot at you. Rivals respawn the same way. When the boss first joins it holds its fire for 3 s.
- **The boss's lives:** each of its 10 lives is a smaller pool (the bar shows the current one); when a life runs out the boss really dies (anyone can take it) and comes back after a moment at a spawn point far from you, never within 600 units and out of your sight where possible, briefly invulnerable. Phase 2 starts at 6 lives left and phase 3 at 3: it comes back ROARING (throws everyone near back a little, spills gems). Its health never resets when you die. **The boss never teleports next to you:** a boss with nobody in sight walks to the nearest fighter on its own paths, and the Hollow King's blink is a short sidestep along a visible path that never ends within 600 units of you.
- **Two strong sides out of three** (speed, health, damage), shown under the bar's right end (FAST / SLOW, ARMOURED / FRAIL, BRUTAL / WEAK):
  - THE BUTCHER: fast + brutal, frail (speed x1.22, health x0.62, damage x1.30).
  - THE OVERSEER: armoured + brutal, slow (x0.84, x1.40, x1.25).
  - THE HOLLOW KING: fast + armoured, weak hits (x1.20, x1.35, x0.65).
  - THE ARCH-DEMON changes its pair every phase: fast + brutal, then armoured + brutal, then fast + armoured. Abyss bosses roll one pair (an Abyss ARCH-DEMON still rotates).
  - Speed scales the boss's run speed, its limb animation, how often it attacks and its telegraphs (never under 0.8 s). Health scales its pool, damage everything it deals (guns and attacks). A fast boss leaves a glowing streak when it runs; an armoured one is 8% bigger with a cold steel sheen; a brutal one smoulders with embers and its blows land with bigger shakes and fire.
  - `g_dsBossScale` (F10 > DESCENT: Weaker 0.7 to Brutal 1.3, default 1.0) multiplies every boss's health and damage, from the next boss floor.
  - THE BUTCHER: MEAT HOOK (a red line locks on, step off it or get yanked in), CLEAVER CHARGE (a lane on the floor, then a fast charge), GROUND SLAM (it leaps; a circle where it lands, jump to dodge the shockwave). Phase 3: BLOOD FRENZY (faster, heals from what it deals).
  - THE OVERSEER: ROCKET BARRAGE (circles round you, arcing rockets land a second later), DRONES (two scrap drones that shoot plasma; from phase 2 they hold up an ENERGY SHIELD that takes most damage until you break them). Phase 3: MORTAR RAIN.
  - THE HOLLOW KING: RAIL LANCES (three lines that fire after a second; five in phase 3), SHADOW STEP (BLINK: a dim line and two glows, then it sidesteps 250–420 units, never ending within 600 units of you; a lance follows quickly), GRAVITY WELL (from phase 2: a vortex near its target, by preference at an edge; run against the pull). Phase 3: DECOYS that throw lances of their own.
  - THE ARCH-DEMON: METEOR STORM (circles, then falling rocks), FIRE NOVA (rings of fire roll outwards; jump them). From phase 2: FIRE FLOOR (circles that turn into burning ground). Phase 3: everything, enraged.
  - A new phase begins with a short invulnerable ROAR when the boss comes back (it throws everyone near back a little and spills a few gems).
  - **Boss locator:** a diamond in the boss's colour over its head with the distance (24m), seen through walls (faint while it's close and in plain sight). When the boss is off screen, a chevron on a ring around the crosshair points the way, with the distance.
  - **Boss bar:** top centre once the floor strip is gone: the boss's name, title and phase, the current life's pool (cyan while shielded), a row of small skulls in its colour for its lives (dark = taken; the one just taken flashes) with the count, its two strong sides under the right end, and the coming attack's name under the middle. Under that, one slim roster line: YOU 7   SARGE 5   KLESK OUT   BUTCHER 6 (lives left each; your nemesis in dark red). The corner block says LIVES 7/10.
  - **Win (10-lives mode):** the boss's last life is taken while you still have lives, whoever lands the blow (a big chest and red gems drop; you have 4 s to grab them). Land that **final blow yourself** for bonus Skulls (5 + floor / 3, times the difficulty's reward multiplier) and a second chest (FINAL BLOW callout). The boss Skulls at the floor's end follow the share of its 10 lives **you** took (won or lost; a lost boss floor still pays for every life you took). Rivals still alive don't matter. **Lose:** your lives run out. (The timed mode above has a clock instead.)
  - Boss health grows with difficulty and with your power (weapon levels, passives, cards; up to 2.5×), so a strong build still feels strong.
  - **Timed balance (round 4, measured):** `tools/boss_sim.py` (default `--mode timed`) runs the Sim alone against the boss for 300 s; `--model results_<tag>.txt` turns a batch into a win-rate curve for a bigger or smaller pool (P(win) at pool x k, from each run's real removal rate, deaths included). Target: the first boss really hard but killable (a sim win rate around 20-30%, about ten tries for a player learning it), losses ending with a little of the bar left. See CHANGELOG 0.6.0-beta for the numbers. Calibration per phase (x ds_bosses[].health): Butcher 1.10, Overseer 0.50, Hollow King 0.58, Arch-Demon 0.42 (`actsCalTimed`, g_ds_boss.c; cut 2026-10-19 after playtests, the sim out-damages a human), damage 0.80 / 0.70 / 0.85 / 0.55, one boss hit takes at most 55% of your max health; the bosses still grow with your power score (up to 2.5x).
  - **Balance of the 10-lives mode (measured, not guessed):** `tools/boss_sim.py` runs boss floors on hidden dedicated servers (`dist_sim/`, homes in `home_sim/`) where a skill 5 bot named Sim plays your part (against the boss and the two rivals) with a typical kit for the floor before the boss (floor 5: double jump, dash, rocket alt-fire, SPITE 2 / IRON HIDE 1 / HOLLOW HEART 1, weapons at level 1–3; later bosses a bigger kit) and a modelled human read of the telegraphs (`g_dsSimDodge` 0.75: it reads 3 in 4 attacks and steps off lines, out of circles, jumps slams and fire rings after `g_dsSimReact` 350–500 ms; drones and decoys break 4–5.5 s after they appear, as if you shot them; it goes for the boss 80% of the time). Each boss has a measured calibration (life pool and damage) on top of its sides. Measured on 2026-10-02 (10 lives each, Hurt Me Plenty, timescale 2, maps of each act, several seeds), last batches near the final values: THE BUTCHER 3/20 and 1/12 (15% / 8%; 16 of 95 floors over its last five settings, so its pool was lowered once more), THE OVERSEER 4/12 and 4/20 (33% / 20%), THE HOLLOW KING 4/20 (20%), THE ARCH-DEMON 6/17 (35%; raised a little). The player took most of the boss's lives: about 80% against the Butcher and the Overseer, about 50–55% against the Hollow King and the Arch-Demon, whose rivals last longer, and landed the final blow in nearly every win. Rivals hit the boss for 55% damage (the Arch-Demon 40%). Calibration (one life's pool x 0.30 x the boss health, damage): Butcher 1.10 / 1.00, Overseer 0.32 / 0.80, Hollow King 0.52 / 1.00, Arch-Demon 0.29 / 0.65. No boss relocation ended within 600 units of the player in 323 simulated floors (4479 relocations: respawns, void rescues, shadow steps; the nearest was 602 units). A real player learns the patterns and plays differently from a bot, so treat these as a starting point: `g_dsBossScale` is the knob.
  - `g_dsBossSim 1` (tests only, not archived): the bot named Sim plays the human's part; the server quits after the result line (`DSSim: result ...` in games.log).
- **Beating THE ARCH-DEMON on floor 24 wins the descent:** the VICTORY screen (INTO THE ABYSS or END THE DESCENT and bank).
- **The Abyss:** floor 25 on, endless. Every map (none twice until all were played), a random boss every 6th floor, frags and bot skill at their highest; boss health +6% and its attacks +1% per Abyss floor.
- **The vault (???):** about one act in four hides a vault on a middle floor. No enemies, 45 seconds, a free chest waits in front of you, and the vault plays one of four modes (the run seed picks it; the card at the start names it and a hint sits under the timer), each worth about 400-600 xp:
  - **THE VAULT:** gems rain all around you.
  - **SHOOTING GALLERY:** up to 5 crystals float around the room. Shoot one (any weapon) and it bursts into gems (8 xp, a golden one every 7th: 30). A new one appears half a second later, and one left alone drifts off after 9 s. The count of crystals broken sits under the timer.
  - **RUSH:** the faster you move, the more gems appear ahead of you on your line of travel. Running gives x1, fast (dash, slide, strafe jumps: 450+ ups) x2, blazing (650+) x4, and a speed bar under the timer shows the tier. Standing still gives nothing.
  - **SKY VAULT:** every 1.4 s a column of 5 gems hangs in the air, worth 1, 2, 3, 5 and 8 from the bottom up (every 5th column's top gem: 25). Double jump, wall jump, the zip line or a rocket jump to reach the high ones.
  When the timer runs out the floor counts as won (the chest opens on the chest screen) but there is no card pick. `g_dsVaultMode 0-3` forces a mode (tests).
- Dev commands: `dsact <1-5>` (jump the run to an act's first floor, map shown), `dsboss <1-5> [map]` (play that act's boss now; 5 = the first Abyss boss), `dsvault [map]`, `dslayers [n]` (print the act pools and the first n floors) in the UI; `dsattack <name or number>`, `dsphase` (take boss lives up to the next phase; timed: empty this phase), `dsbosstime <seconds>` (timed: set the clock), `dsbosslife` (you take one boss life), `dsbossdmg <n>`, `dskillboss`, `dsshow [dist]` (boss in front of you, for screenshots), `dsbossinfo`, `dsvaultend` on the server, plus `dsnemesis [<bot name> [kills] | clear | killme <client>]`. `rearmed <0-6>` opens the F10 menu on a tab (2 = RULES, 5 = DESCENT). `set ui_rlDevmap 1` launches floors with devmap (cheats on for tests). Test configs: `home_test/rearmed/ds_*.cfg` (`ds_resp.cfg` boss-floor respawn, `ds_nem.cfg` nemesis + boss bar, `ds_aas.cfg` bots on later floors).
- **Your NEMESIS:** the bot that killed you most on this descent becomes your nemesis once it has killed you 3 times (a bot with more kills of you takes the title over). A short callout at the right edge (NEMESIS: SARGE, KILLED YOU 3 TIMES) and a line from it in the chat area. From then on it comes back under its name on every later floor (on a boss floor as one of the two rivals), hits 8% harder and takes 7% less, wears a dim dark red rim and a dark red glow at its feet (no text over its head), and taunts now and then in the chat line (short lines, at most one every 15 s). Kill it for revenge: Skulls (3 + floor / 4, times the difficulty's reward multiplier), a gem burst and a NEMESIS SLAIN callout; then it's an ordinary bot again until someone earns the title. Saved in the run file (`nemesis_name`, `nemesis_kills`, `nemesis_slain`, `nemesis_cand`). `g_dsNemesis 0` (F10 > DESCENT) turns it off.
- **Lives:** 3 on I Can Win and Bring It On, 2 on Hurt Me Plenty, 1 on Hardcore and Nightmare. Losing a floor costs a life; at zero the descent ends.
- **Change the difficulty during a descent:** the DIFFICULTY button at the bottom right of the floor map (it shows the current one and its reward multiplier) opens a picker with all five. From the next floor on the bots' skill, the bosses' health and attacks and your rewards follow it; your lives stay as they were at the start. Rewards: I Can Win x0.5, Bring It On x0.75, Hurt Me Plenty x1.0, Hardcore x1.05, Nightmare x1.10, for Skulls, gem xp and skill xp. **Records** of a descent count the easiest difficulty you actually played a floor on (switching and back without playing costs nothing); the run history marks a mixed descent with a + (BRING IT ON+).
- **Skulls** are banked when the descent ends (dying or abandoning never costs the ones you earned). Spend them in Upgrades (see below) and on skill respecs. A won floor pays the floor (3 + floor/3), 1 per 3 kills (up to 4), style (up to 3) and the node bonus (elite x2, event x1.5, boss x3). On a boss floor you also get the share of the boss's 10 lives that YOU took x its bar (24 + 6 per act: Act 1 is worth 30, so 5 of 10 lives = 15), won or lost, plus 5 + floor/3 for the final blow. **A lost floor still pays** 1 per 3 kills (a lost boss floor + 2 for trying), always less than winning it. Chests with nothing left to level and nemesis revenge pay a few too. GRAVE COIN adds its %, then the difficulty multiplier. The run summary shows where the Skulls came from (floors, kills, bosses, loot and final blows, bonus and difficulty).
- **What a descent pays** (Hurt Me Plenty, typical play; `dsecon sim`, the full table per difficulty is in docs/DESCENT_V2.md): dies on floor 3: 18 � dies at the first boss: ~83 � beats it, dies in act 2: ~130 � dies at boss 2: ~210 � act 3: ~275 � boss 3: ~370 � act 4: ~445 � wins: ~600. Deeper floors pay more per hour (from ~70/h on the first floors to ~310/h for a win), so pushing on always beats farming the start.
- **Abandon:** ABANDON DESCENT (hub, bottom right, from the main menu) ends the descent as a loss: its Skulls and records are banked.
- **Records:** deepest floor per difficulty, kills, descents, lifetime counters, single-floor bests and the run history.
- **Reset all progress:** Records > RESET ALL PROGRESS (main menu only) asks twice on two separate screens, then copies `rearmed_meta.txt`, `rearmed_run.txt`, `rearmed_history.txt` and `rearmed_skills.txt` to `rearmed_backup_<n>_meta.txt` (`_run`, `_history`, `_skills`) and empties them (the ghost, tally, boss practice `rearmed_practice.txt` and boss rematch `rearmed_rematch.txt` files too). To undo, copy the backups back over the files while the game is closed.
- Dev commands (economy): `dsdiff <1-5>`, `dsdiffmenu`, `dsreset [2|confirm]`, `dsabandon [confirm]` (UI); `dsecon [sim|perks]`, `dsscore <client> <n>` (server). Test configs: `tools/econ_cfgs/`, runner `tools/econ_test.sh` (isolated `home_test/econ`).

### Map variety: never the same map twice (round 3, ds-maps)

- **No map twice in one descent**, across the acts and the Abyss. The floor map never offers a map you already played this run, and two branches of one floor never show the same map. A map shown on the floor map stays put; it only changes if a path you took would make it a repeat (about 1.6% of floors in the simulation) or if you lost on it (a retried floor gets a new map). Only when every installed map was played this run (deep in the Abyss, past ~47 floors) does a floor bring one back: the least recently played, tagged **REVISITED** on the floor map (AGAIN beside the node).
- **Different maps every descent.** The game keeps how often you played each map in DESCENT and in which descent (`dsm_plays`, `dsm_last` in rearmed_meta.txt; the first time it counts the DESCENT floors in your games.log). The pick leans hard toward maps you played least and not lately, hardest on the first act, and follows the floor's size: small tourney maps for 2–3 bots, big CTF / Team Arena arenas for 4–5. Maps you never played in DESCENT say **NEW** on the floor map (the node you can pick: NEW beside it; floors further down: a small green mark).
- **More maps:** every q3dm (0–19), q3tourney1–6, the four q3ctf maps and q3tourney6_ctf as free-for-all arenas (their flags go; a map with fewer than 8 deathmatch spawns gets one at every team spawn), the four pro maps, the rm_ remixes, and with Team Arena: mptourney1–4 and mpteam1, 2, 4, 6, 7 (copied from your own `missionpack/pak0.pk3` into `zz-rearmed-ta.pk3` by `tools/extract_ta_maps.py`, or by setup.ps1 in a release). Left out: mpteam3 and mpteam5 (too big for a frag race of 6: half the kill pace of q3ctf), mpteam8 (bot library errors), mpterra1–3 (huge terrain), mpq3ctf1–4 / mpq3tourney6 (the same layouts as q3ctf / q3tourney6_ctf). Every map in the pools was loaded on a dedicated server with 4 bots (`tools/dsmaps_maptest.py`): all of them played, 11–40 kills in 75 s (classic maps 18–40, the big CTF / Team Arena ones 11–36).
- **Floor variants:** each node rolls a twist, shown after the map name in the floor map's panel (`q3dm7  NIGHT  ARMS SWAP`):
  - lighting (about half the floors; per act): NIGHT, BLOOD MOON, DUSK, FOG, SMOG, COLD LIGHT, TOXIC, INFERNO, FLOODED LIGHT. A tint and haze over the world under the HUD, on top of the act's own look. `cg_dsVariantLight` (F10 > DESCENT: Off / On / Strong). A dark variant steps aside when you take the BLACKOUT modifier.
  - layout (45% of fight / elite / event floors, never bosses or vaults): ARMS SWAP (the weapon pickups trade places), ARMOR SHIFT (armor and mega health trade places with health), SCRAMBLED (both). Nothing is added or removed; the bots know the new layout.
  - `g_dsVariants 0` (F10 > DESCENT: Floor variants) turns them off. No map file is changed.
- Dev and tests: `dsmaps` (pools, lifetime counts, this run's played list and the stored floor-map window), `dsmapsim [runs] [floors] [loss%] [real|log:<file>]` (UI: the simulation, writes `rearmed_mapsim_*.txt`), `set ui_dsmDebug 1` (every pick with its tier and weight). `tools/dsmaps_test.sh sim|ui|floor`, `tools/dsmaps_maptest.py` (every map with bots, hidden dedicated servers), `tools/map_audit.py` (spawns, items, AAS, size of every installed map).

### Loot: gems, chests, passives

- **Gems:** your kills drop xp gems: blue (1), green (5) and red (25). Elites drop more and bigger ones. Gems pop out, sit and glow, and fly into you once you're within about 110 units, with a rising tick as you chain them. Each gem feeds the weapon that made the kill and fills the thin xp bar under your ammo number. Only you collect gems; the bots' weapon levels follow yours (see Arsenal). Past 150 gems on the map, new ones merge into old ones.
- **Vacuum:** a spinning magnet that drops from elites (1 in 4) and rarely from any kill. Touch it and every gem on the map streams to you.
- **Chests:** elites drop one, bosses a big one, and lucky kills a rare one. Touching a chest banks it (it shows in the corner block). Banked chests open at the end of a won floor, before the cards: a slot machine spins 1, 3 or 5 reels (luck makes 3 and 5 likelier). Rewards come in this order: an evolution if one is ready (weapon level 8 + its key card), then weapon levels for weapons you've used, then passive levels. Fire or jump skips. A lost floor keeps your banked chests. Chests still lying on the map when you win are banked for you.
- **Passive items:** 13 of them, 5 levels each, at most 6 different ones per run. Each level is a small step: SPITE +5% damage, IRON HIDE -4% damage taken, HOLLOW HEART +6 max health, BLACK BLOOD 0.3 health a second, QUICKSILVER +3% speed, HAIR TRIGGER fire 4% faster, BLAST CORE +6% blast radius, RIFLING +6% projectile speed, LODESTONE +25% gem pull radius, RABBIT'S FOOT +8% luck, HUNGER +8% gem xp, GRAVE COIN +8% Skulls, AMMO BELT +8% ammo. They replace the old stat cards.
- **The bots keep up:** bots carry about as many passive levels as you do (elites and bosses a few more), and their damage dealt and taken follows your overall power (bots about 0.95x yours, elites 1.1x), within limits so a strong build still wins.
- Dev commands (server console, test runs only): `dsgems [xp] [bursts]`, `dsscatter [n]` (gems on every spawn point), `dschest <0-2> [bank|here]`, `dsvacuum [now|here]`, `dspassive <passive> <level>`, `dsfrag [client]` (you frag a bot: the kill drop), `dsused <weapon>`, `dspower`, `dsloot`.

### Arsenal: weapon levels and evolutions

- **Weapon levels 1–8:** every weapon (not the grapple) starts at level 1 the first time you hold it in a descent (level 2 with the ARSENAL upgrade). Gem xp, +1 weapon cards and chest rewards raise it. Eight small pips above the ammo number show the level of the gun in hand; a level-up gives a short chime and a flash on the pips with a LEVEL 5 tag beside them, never mid-screen. The weapon wheel shows each gun's level at the corner of its icon.
- **What the levels add** (every step is single-digit; the specials are new behaviour, not big multipliers):

| Weapon | Levels 2–7 | Level 8 |
|---|---|---|
| Gauntlet | +8% damage, swing 6% faster, hard knockback, +8% damage, every hit heals 3, swing 6% faster | kills gib and splash gore that hurts nearby enemies |
| Machinegun | +6% damage, fire 5% faster, bullets pierce one enemy, +6% damage, 1 in 12 shots free, fire 5% faster | every 5th bullet explodes |
| Shotgun | +1 pellet, +6% damage, tighter spread, +1 pellet, fire 5% faster, +6% damage | pellets set enemies on fire |
| Grenade launcher | +6% damage, +6% radius, fire 5% faster, livelier bounce, +6% damage, +6% radius | every grenade splits into 3 bomblets |
| Rocket launcher | +6% damage, +8% speed, +6% radius, fire 5% faster, +6% damage, +8% speed | every 4th shot adds a half-strength rocket |
| Lightning gun | +6% damage, hits slow the target, +6% damage, fire 5% faster, 1 in 12 cells free, +6% damage | the beam arcs to a second enemy |
| Railgun | +6% damage, recharge 5% faster, slugs ricochet off a wall, +6% damage, recharge 5% faster, +6% damage | kills explode |
| Plasma gun | +6% damage, +8% bolt speed, fire 5% faster, +8% splash, +6% damage, fire 5% faster | bolts grow (up to +35%) as they fly |
| BFG10K | +6% damage, +8% radius, recharge 6% faster, +6% damage, zaps reach further and 2 more enemies, +8% radius | the blast leaves a burning crater |
| Nailgun | +6% damage, +1 nail, nails ricochet once more, +6% damage, fire 5% faster, +1 nail | nails pin (slow) enemies |
| Prox launcher | +6% damage, +8% radius, mines arm twice as fast, fire 5% faster, +6% damage, +8% radius | mines leave fire |
| Chaingun | +6% damage, spins up 9% faster, +6% damage, bullets pierce one enemy, 1 in 12 shots free, fire 5% faster | no spin-down |

- **Evolutions:** a level 8 weapon plus its key card (a passive at any level, or a perk) evolves at the next chest you open. An evolved gun wears a coloured glow in your hand, gets a coloured frame round its ammo icon and a glow in the wheel, and sounds different:
  - HEARTRIPPER (gauntlet + BLACK BLOOD): twice the reach, red claw slashes, every hit heals 8, kills burst
  - BULLET HELL (machinegun + HAIR TRIGGER): twin gold tracers, no spread, rounds pierce three bodies
  - DRAGON'S BREATH (shotgun + BLAST CORE): burning pellet streaks that ignite enemies and leave fire on the floor
  - CLUSTER BOMB (grenade launcher + RABBIT'S FOOT): every grenade bursts into a carpet of 6 glowing bomblets
  - HELLFIRE (rocket launcher + SPITE): a salvo of three seeking rockets with crimson fire tails
  - STORM CALLER (lightning gun + CONDUCTOR): violet forks jump from your target to every enemy near it
  - JUDGEMENT (railgun + HEADHUNTER): slugs punch through two walls and the path detonates in a line of golden blasts
  - STAR FURNACE (plasma gun + RIFLING): white-hot stars that burst into a spray of small orbs
  - DOOMSDAY (BFG + LODESTONE): a slow black hole that drags enemies in, then collapses
  - IRON MAIDEN (nailgun + IRON HIDE): nails ricochet from enemy to enemy (3 jumps)
  - MINEFIELD (prox launcher + PYROMANIAC): mines arm fast, set the ground on fire and set each other off in chains
  - RIPSAW (chaingun + AMMO BELT): no spin-up, never runs dry, hot tracers, shredded bodies
  - ARMAGEDDON (secret: HELLFIRE + CLUSTER BOMB + the UNION card): seeking rockets that rain bomblets on their way and burst on impact
- **Bots keep pace:** bot weapon levels follow your own average weapon level (one lower on the first half of an act, elites and bosses one higher) and catch up during a floor as you grow. Elites from Act 3 carry three evolved guns, bosses all of them. In a fight a bot reaches for a clearly better-levelled gun.
- Dev commands (server console, test runs only): `dslevel <weapon> <level> [client]`, `dsxp <weapon> <xp> [client]`, `dsevolve <0-12> [client]`, `dsstate` (every client's levels to games.log). `set ds_debug 1` logs every arsenal shot (`DSFire`, with time and fire-rate stat) and damage scaling (`DSDamage`).

### Characters, unlocks, collection, upgrades

- **The hub:** Continue, New Descent, Upgrades, Skills, Collection, Records. Mouse, WASD, Tab / Shift-Tab, Space and a gamepad all work; nothing needs the arrow keys or Enter.
- **Characters:** New Descent asks for the difficulty, then the character. Each plays as its own model for the run (your own model setting is never changed) and starts with a kit:
  - RANGER: machinegun lv2, no weakness (always open)
  - SARGE: shotgun lv2, IRON HIDE 1 (clear Act 1)
  - VISOR: railgun, HEADHUNTER (land 300 headshots)
  - ANARKI: double jump and dash, QUICKSILVER 2, -20 max health (reach floor 10)
  - KLESK: gauntlet lv3, BLACK BLOOD 2 (200 gauntlet kills)
  - XAERO: plasma gun, BLAST CORE 1 (evolve a weapon)
  - DOOM: rocket launcher, SPITE 1, 4% slower (1000 kills)
  - HUNTER: grapple and zip line, LODESTONE 2 (100 zip line rides)
  - ??? : a secret. The hint is in the Collection.
  Locked characters show their condition and how far you are. The free kit doesn't count toward the bots' card count.
- **Unlocks** come from lifetime counters (kills, headshots, gauntlet kills, gibs, zip rides, chests, evolutions, elites, bosses, acts, wins, best floor). The kills and floors you played before 0.5 count too. New unlocks pop up on the run summary, never during a fight.
- **Secret cards** (then offered in card picks): PENTAGRAM, BLOOD CROWN, GOLDEN SKULL, STOPWATCH, UNION. Their conditions are hidden; the Collection gives a vague hint until you find each one.
- **Collection:** weapons (highest level reached and every level's effect), evolutions (??? until found), passives, perks, abilities and alt-fires (dim until you've taken them), characters, bosses (times defeated) and secrets. Tab / Shift-Tab or the shoulder buttons switch lists; WASD or the mouse picks.
- **Bestiary** (the Collection's BESTIARY tab, 0.6): every enemy archetype, elite affix, the champion pack, the ambush mini-bosses and the act bosses. Each stays "???" until you meet it (it comes into view on your floor, or hurts you); then it shows your lifetime kills, when you first met it, what it does and how to beat it. The first meeting ever shows a small NEW IN BESTIARY note at the left edge (`cg_dsBestiaryNote`, F10 > BOTS). Kept in rearmed_meta.txt (`bst_*` keys, merged at floor end); daily floors don't count. Dev: `dsbst`, `dsbstmeet <name>`, `dsbstkill <name> [n]`.
- **Upgrades:** 14 permanent upgrades, 30 ranks, 14,800 Skulls for the whole tree (roughly 40-60 descents). Four tiers, each about one thing; a tier opens once you've bought enough ranks in the one before it (5, 3, 3):
  - I FLESH, the body: IRON HEART +5 max health (60-320 a rank), FLEET FOOT +2% speed (60-320), WHETSTONE +3% damage (70-370), 5 ranks each
  - II MARROW, the cards: SECOND THOUGHTS +1 reroll (250 / 450 / 700), BIRTHRIGHT a free starting ability (400), FIRST PICK 5 cards in each act's first offer (600), WIDER CHOICE 4-card offers (900)
  - III IRON, the spoils: TREASURE HUNTER +5% chance of one more chest reward (350 / 600 / 900), ARSENAL every weapon starts at level 2 (1000), QUARTERMASTER every act's first floor starts with a chest in the bank (1200)
  - IV SOUL, the grave: LAST BREATH +1 life on every boss floor (700 / 1200), REVIVAL the loss that would end a descent costs no life, once (1200), SPARE SOUL +1 life (1500)
  Each row shows what you have and what the next rank gives, and its price; the line under the tree shows how much of it you own. No rank gives 10% or more.
  - Retired in the economy pass (they did what passives and skill perks already do): BONE PLATE, GRAVE ROBBER, LODESTONE, HUNGER, RABBIT'S FOOT, HAIR TRIGGER, BLAST CORE, IRON HIDE, EVENT HORIZON. Ranks you owned in them are refunded at the price you paid, once, the first time the hub opens (the Upgrades screen shows how many Skulls came back). Ranks you own in the kept upgrades stay yours.
- **Run summary:** when a descent ends: floors, time, kills, headshots, gibs, chests, Skulls banked, new records, what beat you, your passives and evolutions, the unlocks you earned, and a **damage breakdown per weapon** (level, evolved, damage and its share, kills, time held, damage per second). Beating the Act 4 boss opens the **VICTORY** screen with the same breakdown: go INTO THE ABYSS or END THE DESCENT and bank.
- **Run history:** your last 30 descents (date, character, difficulty, result, floor, kills, time, Skulls, how it ended), under Records.
- **Save and resume:** the run is saved at every floor boundary. Quit (or crash) in the middle of a floor and Continue restarts that same floor from its start, without costing a life.
- Saves live in `home/rearmed/`: `rearmed_run.txt` (the current descent), `rearmed_meta.txt` (Skulls, upgrades, records, lifetime counters, unlocks) and `rearmed_history.txt` (the last 30 descents).
- Dev commands: `descent collection|characters <difficulty>|history|summary`, `dsui [set <key> <value> | char <n> | unlock <id> | process]` (UI), `dsmeta`, `dsstat <stat> <n>` (game). Test script: `tools/test_meta.sh [W H] [fixture dir]` (home_test only).

### Skills (Skyrim-style skill trees)

- **Ten skills train by doing**, permanently and for every character: MELEE (gauntlet), BALLISTICS (machinegun, chaingun, nailgun), SCATTER (shotgun), EXPLOSIVES (rockets, grenades, prox mines, BFG), ENERGY (lightning, plasma), PRECISION (railgun, and headshots with any weapon), TRAVERSAL (wall runs, wall and air jumps, slides, zip line, grapple, fast air time), EVASION (dashes, blinks, enemy missiles that miss you closely), SURVIVAL (damage you take and live through for 3 s, healing), CARNAGE (gibs, style points). They train in **every mode against bots** (DESCENT, skirmish, single player arenas); with no bot in the game nothing trains.
- **Skill levels 1-100**, fast early, slow late: level 1 to 2 takes 120 xp, level 6 about 800, level 25 about 9.9k, level 50 41.5k, level 100 207k (xp from level L to L+1 = 100 + 20L + 0.3L^2). Weapon skills get xp from the damage you deal and your kills, weighted so that a skill you use levels at about the same pace as the movement and survival skills. DESCENT multiplies skill xp by the difficulty's reward multiplier (I Can Win 0.5x ... Nightmare 1.10x); outside DESCENT it is 1.0x.
- **Character level and perk points:** every few skill levels raise your character level, and each character level is one perk point. Level 2 needs 7 skill levels gained, then 8 more, 9 more and so on (3 at 15, 4 at 24, 5 at 34); all ten skills at 100 make level 39 (38 points for 104 perk ranks, so you choose). A first descent that dies at the first boss earns about 1-3 points. New points show on the run summary ("+1 PERK POINT"), never in a fight.
- **SKILLS screen** (hub): ten constellations, one per skill, each with its level and xp bar. A lit star is a perk you hold, a pulsing ring one you can take now, a dim ring one that waits for a skill level (the number under it) or a perk point, a grey ring one behind a perk you haven't taken. The panel below gives the perk's exact numbers now and at the next rank and what it needs. WASD (or the stick / mouse) moves between stars, Q / E / Tab / shoulders change the tree, Space / click / A takes a rank, R / Y respecs the tree you're on (25 Skulls + 5 per rank given back). On a 32:9 screen the ten trees sit in one row.
- **Perks** (DESCENT only; no rank gives 10% or more; ranks need the skill level in brackets; a perk needs one of the perks it hangs from):
  - MELEE: BRAWLER +5% gauntlet damage per rank (1/25/50), LONG ARM +8% reach (15), BLOODLETTER gauntlet hits heal 2 per rank (20/45), SWIFT FISTS 5% faster per rank (30/60), JUGGERNAUT -6% damage taken holding the gauntlet (40), RECKONING gauntlet kills refill a dash charge (55), MEAT GRINDER gauntlet kills always gib and drop a green gem (80)
  - BALLISTICS: STEADY HANDS +4% per rank (1/25/50), SCAVENGER kills give back 10 rounds (15), RAPID CYCLE fire 3% faster per rank (20/45), SPIN DOCTOR chaingun spins up 9% faster (30), SHREDDER +1% per hit in a row on one target, up to +8% (40), ARMOR PIERCER +4% to armored enemies per rank (50/70), TRIGGER DISCIPLINE 8% of shots cost no ammo (75)
  - SCATTER: BUCKSHOT +4% per rank (1/25/50), POINT BLANK +5% within 250 units per rank (15/40), QUICK PUMP 4% faster per rank (20/45), FLAYER shotgun kills within 250 units gib (30), BRACE -5% damage taken holding the shotgun (40), SECOND BARREL 8% of blasts cost no shell (55), LAST RITES shotgun kills heal 5 (75)
  - EXPLOSIVES: DEMOLITION +4% splash damage per rank (1/25/50), SAFETY FUSE -6% self damage per rank (15/35), PAYLOAD rockets, grenades and mines 5% faster (20), BLAST RADIUS +4% splash radius per rank (30/55), AIR RAID +7% to airborne enemies (40), IMPACT +8% on direct hits (50), CHAIN FUSE explosive kills give back a round (65)
  - ENERGY: CAPACITOR +4% per rank (1/25/50), OVERCLOCK plasma 3% faster per rank (15/40), GROUNDED -6% from lightning and plasma (20), SUPERHEATED +6% plasma splash radius (30), STATIC CHARGE +6% lightning after 1 s on one target (45), RECYCLER 8% of energy shots free (60), SIPHON energy kills give 5 armor (75)
  - PRECISION: DEADEYE +4% railgun per rank (1/25/50), STEADY railgun recharges 4% faster per rank (15/40), SKULLCRACKER +5% headshot damage per rank (20/45), LONG SHOT +6% hitscan beyond 1200 units (30), EXECUTIONER headshot kills drop a green gem (40), SNIPER'S NEST +6% railgun standing still (55), MARKED railgun hits mark the enemy: +5% damage to it for 3 s (75)
  - TRAVERSAL: MOMENTUM +3% speed per rank on a wall run (1/30), SPRINGHEEL air and wall jumps 5% higher (15), SKYWALKER wall runs 8% longer per rank (20/45), SECOND WIND a kill gives back the zip line and air jump (30), AIRBORNE +3% damage in the air per rank (40/60), SOFT LANDING no fall damage (50), SLINGSHOT +5% speed for 2 s after a zip or grapple (70)
  - EVASION: GHOST dash recharges 5% faster per rank (1/30), SIDESTEP -4% damage for 1 s after a dash per rank (15/40), SLIPPERY -6% splash damage taken (20), AFTERIMAGE near misses recharge the dash by 0.3 s (30), COUNTERSTEP +6% damage for 1.5 s after a dash (45), SHADOW 5% chance to dodge a direct hit (60), REFLEX a kill within 1 s of a dash refills a dash charge (75)
  - SURVIVAL: TOUGHNESS +3 max health per rank (1/25/50), IRON SKIN -3% damage taken per rank (15/40), MENDING below 50 health regenerate 1 every 2 s per rank (20/45), ARMORER spawn with 10 armor (30), GRIT -8% damage taken below 25 health (40), UNBREAKABLE every hit 1 smaller (55), CHEAT DEATH once per floor a killing blow leaves 1 health (75)
  - CARNAGE: RED RAIN gibs heal 2 per rank (1/30), SHOWOFF +4% style points per rank (15/40), GORE HOUND gib kills drop a blue gem (20), OVERKILL +5% to enemies below 30 health (30), FRENZY fire 4% faster at style rank S or above (45), CARNIVAL gib kills hold the style meter 3 s (55), MASSACRE two kills within 2 s heal 5 and refill a dash charge (75)
- **HUD:** the held weapon's skill and a thin xp bar ("EXPLOSIVES 23") sit small above the weapon level pips by the ammo; a level-up flashes there for under 2 s with a soft chime (a movement or survival level-up borrows the row for that moment). Nothing mid-screen. `cg_skillHud 0` hides it (F10 > HUD).
- **Switch:** `g_skills 0` (F10 > RULES) stops training and turns the perks off.
- **Files:** the game writes the xp of every floor / match (and of a map change or quit) into `rearmed_skills.txt` as a pending batch; the menus add each batch to `rearmed_meta.txt` (`sk_xp`, `sk_perks`, `sk_mseq`, `sk_respecs`) exactly once. The game counts a batch the menus haven't merged yet, so levels stay right across normal matches. RESET ALL PROGRESS clears both.
- Dev commands: `dsskills`, `dsskillxp <skill 0-9> <xp>`, `dsperk <perk 0-69> <rank>`, `dsperkall <rank>`, `dsskillsave` (server, this map only); `descent skills [skill]`, `dsskilltake <perk>`, `dsskillrespec <skill>`, `dsskillmerge` (UI). `g_skillsDebug 1` logs level-ups and every perk effect (`SkillPerk:`) to games.log, `2` also every xp gain (`SkillXP:`); `g_skillsTrack <client>` trains a bot instead of you and gives it the perks (tests). Test script: `tools/test_skills.sh [W H]` (home_test only), configs `home_test/rearmed/sk_*.cfg`. Sounds: `tools/make_sounds_skills.py`.

### Frenzy, lucky crits, power spikes, loot beams (round 3)

All four are DESCENT-only and each has a switch on the F10 > DESCENT tab: `g_dsFrenzy`, `g_dsCrits`, `g_dsSpikes`, `g_dsBeams` (all on by default). The single-digit rule bends only for these short bursts. Nothing here is permanent, and the bots' power matching (G_DS_PowerScore) never counts them.

- **Kill-chain frenzy:** each kill you make within the chain window adds to a chain. The window is about 4.2 s, and it shrinks a little at every tier so the peak becomes a sprint. Things that stretch the window:
  - a headshot kill +1 s, a gib +0.7 s, each style rank (D..SSS) +0.11 s, an elite +1.5 s, a boss life +2.5 s
  - any headshot hit, even one that doesn't kill, +0.25 s
  - the window stays between 2.6 and 7 s

  The tiers raise the xp of every gem spray and pull the gems in from further and faster:

| Tier | Chain | Gem xp | Pull radius | Flight time | Crit chance |
|---|---|---|---|---|---|
| BLOODLUST | 3 | x1.3 | x1.15 | x0.88 | +1% |
| RAMPAGE | 6 | x1.65 | x1.3 | x0.76 | +2% |
| UNSTOPPABLE | 10 | x2.1 | x1.5 | x0.64 | +3% |
| MAYHEM | 15 | x2.6 | x1.75 | x0.55 | +4% |

  - Every 15 kills in one chain start BERSERK.
  - Each tier plays a rising musical sting, and the gem pickup ticks get a louder, brighter layer. A glow at the screen edges grows with the tier, pulses faster, and flickers in the last second of the window. The centre stays clean.
  - **Counter:** a compact "x12 RAMPAGE / GEMS x1.65" with a draining timer bar sits under the corner block (top left, cg_hudWide).
  - **Cash-out:** when the chain breaks (time runs out, you die, or the floor ends), "x12 CHAIN +N XP" counts up at the side with a coin cascade. N is the gem bonus plus a cash-out bonus of 3 xp per kill, plus 6 xp per kill past the 9th, x the difficulty multiplier. The cash-out bonus goes to the gun in your hand.
- **Lucky crits:** every weapon hit can crit.
  - **Chance:** 4% base, +0.15% per luck percent (RABBIT'S FOOT 5 = 10%), +1% per frenzy tier, +8% while BERSERK, capped at 25%. Bots crit 2% (elites and bosses 3%).
  - **Damage:** x1.5 on that hit, x2 on a headshot.
  - **What you see and hear:** a gold glint at the impact (a bigger white star for headshot crits), sparks and a flash of light, a crunchy crack-and-ping sound (always audible for your own crits and for crits on you), and a gold hitmarker with a small gold star on the crosshair.
  - **Fair:** a crit never turns a hit that wouldn't kill into a kill on a target at 90%+ health. The hit leaves 1 health at most.
- **Power spikes:** short bursts for you only, never for bots. Each starts with a short slow-mo kick, a colour wash at the screen edges, a musical hum loop, and a countdown ring of dots around the crosshair. The name and seconds left show under the chain counter. In the last 1.5 s the wash blinks, and a power-down plays when it ends. Taking the same spike again restarts it; it never stacks past one burst.
  - **BERSERK** (6 s, from a 15-kill chain): +40% damage, 30% shorter refire, 15% of damage dealt heals you, +8% crit chance.
  - **OVERDRIVE** (7 s): +30% speed, the dash is always ready (even without the dash card), the zip line refreshes in the air.
  - **The OVERDRIVE pickup** is a spinning cyan star with a gold beam. It drops from elites (15%), from each boss life you take (20%), from any kill rarely (0.6%), and from other round 3 sources (ambush mini-bosses, stashes). Luck raises all of these. It lasts 30 s and blinks in its last 5.
- **Loot beams:** coloured light pillars on rare drops, white (common), blue (rare), purple (epic), gold (legendary):
  - plain chests blue, elite chests purple, boss chests gold
  - the vacuum blue
  - OVERDRIVE pickups gold
  - red gems white
  - legendary drops from cards and stashes

  Each beam grows in with a flare and has a ring of light on the floor, rising motes and a soft light. Behind a wall you still see a faint line and a glow, the same size on screen at any distance, so you can find it. Each beam plays a short shimmer, in the sound of its rarity, the first time it comes into view. At most 24 beams exist at once, and at most 6 white ones; the weakest, oldest beam makes way.
- **API for other parts** (g_descent.h, FRENZY section): `G_DS_LootBeam( ent, rarity )`, `G_DS_BeamAt( origin, rarity, msec )`, `G_DS_LootBeamOff( ent )`, `G_DS_GrantSpike( player, kind )`, `G_DS_SpawnPowerSpike( origin, kind )`, `G_DS_FrenzySpikeExtend( ent, msec )`, `G_DS_FrenzyBestChain()`, `G_DS_FrenzyChainXP()`.
- **Dev commands** (server, test runs only):
  - `dsfrenzy <n> | end` sets the chain or cashes it out.
  - `dscrit` makes your next hit crit. `dscrit hits <n>` lands n real hits on the nearest bot and logs the rate. `dscrit clamp` tests the fairness clamp. `dscrit sim <n>` rolls the chance n times. `dscrit stats` prints the counters. `dscrit fx [1]` shows only the glint and the marker.
  - `dsspike berserk|overdrive [drop|here] | off`
  - `dsbeam <rarity> [msec]`. `dsbeam chests` puts chests and pickups on the spawn points. `dsbeam hidden` puts beams behind walls and turns you to the nearest.
  - `dsfzstate`
  - Everything logs `Frenzy:` lines to games.log.
  - Tests: `tools/frenzy_test.sh <cfg> menu <map> W H` with cfgs in `tools/frenzy_cfgs/` (`fz_chain`, `fz_beams`, `fz_walls`, `fz_fps`).
  - Assets: `python tools/make_frenzy_assets.py` (textures, shaders, sounds).

### Card reveal, legendary cards, synergy ding, floor-end tally (round 3)

One switch, F10 > DESCENT > **Card reveal** `cg_dsReveal` (1 = the full show, 0 = instant: the cards are just there and the tally is skipped). Code: `game/g_ds_cards.c`, `cgame/cg_ds_cards.c`.

- **Floor-end tally:** after a won floor, before the chests and the cards, a slot-machine plate counts up KILLS, HEADSHOTS, GIBS, BEST CHAIN (the frenzy chain), GEMS, DAMAGE, STYLE (the letter spins) and SKULLS. Each line rolls with ticks and lands with a stamp. Under SKULLS, gold chips show where they came from: FLOOR, KILLS, STYLE, the node (ELITE / EVENT / BOSS), BOSS (the bar), MODIFIER (a floor modifier, when one paid), the bonus % and the difficulty multiplier. A red **NEW RECORD** stamp slams next to every single-floor personal best (kept in `rearmed_tally.txt` in the mod home, written by the game). About 4.7 s; fire or jump skips to the end, a second press goes on. The server waits for it (at most 11 s) in the floor-end phase; then the chest jackpot, then (16:9 screens) the best-kill replay, then the cards.
- **Card rarity reveal:** the offer deals face-down and the cards flip one by one (0.2 s each). The back's colour and frame tell the rarity before it turns: grey common, blue rare, purple epic, gold legendary. Rare and epic cards shimmer and chime / sting as they open. A **legendary** charges first (its back glows and shakes, the screen dims for 0.7 s), turns slowly, opens with a roar and a choir hit, a gold flash, a slow pulse on the card, flares at both screen edges and "LEGENDARY!" under it; its face keeps a gold light sweep. Fire, jump or a pick key during the reveal flips everything at once (one sound for the best rarity). The controls after the reveal are unchanged: 1-3 / 4 / 5, the wheel, fire, jump rerolls, the gamepad.
- **Synergy ding:** a pick that completes a combo or an evolution key ("COMBO: BLINK + MIRAGE", "EVOLVES ROCKET LAUNCHER", "KEY FOR HELLFIRE", "FUSES ARMAGEDDON") snaps the two icons together (the held card or the weapon + the picked card) right of the taken card, with a snap, a chime, a burst of light and a flare at the screen's right edge, and "COMBO!" / "EVOLUTION KEY!" / "EVOLUTION READY!". In play, when an evolution becomes ready (level 8 + its key held), "HELLFIRE READY: OPEN A CHEST" shows small at the right edge for 4 s with a chime.
- **Legendary cards:** 12 build-defining cards, the player's only (bots never get them), at most one per offer. Chance per offer: 3% + floor / 2, +8 on elite offers, +25 on boss offers, x luck (RABBIT'S FOOT), at most 50%. The floor decides it, not the deal: a reroll keeps the legendary. A secret stash can owe one (the next offer holds it). The Collection has a LEGENDS tab. Each legendary held counts toward the power score the enemies are matched to. Numbers stay single digit or mechanical:

| Card | Tag | What it does |
|---|---|---|
| CHAIN LIGHTNING SOUL | CLS | Every kill arcs lightning (40 damage) into the nearest enemy in sight within 700 units; an arc that kills arcs on (4 links at most). |
| ECHO | ECH | Every 5th shot echoes: a free copy of the shot fires 0.15 s later along the same aim (not the grapple). |
| VAMPIRE LORD | VMP | The damage past an enemy's last health point (overkill) becomes armor, up to your max health. |
| TIME THIEF | TTH | Kills add 1.5 s to a running power spike (frenzy; capped by it) and to running powerups (quad, haste, ...). |
| BLOOD ENGINE | BEN | Gibbing an enemy refunds a dash charge and a few shots of the held weapon (9 bullets / cells / lightning, 3 nails, 2 shells / grenades / rockets / mines, 1 slug / BFG). |
| MIRROR | MIR | The first hit you take each life does nothing to you and hits the attacker for the same damage. |
| PHOENIX | PHX | Once a floor, a killing blow instead leaves you at half your max health (or what you had), with a fire nova (60 damage at the centre, 320 units, sets enemies on fire for 2 s) and 1.2 s of grace. |
| MAGNETAR | MGT | 3 gems orbit you (64 units out) and burn enemies they touch (8 damage, at most every 0.4 s per enemy); each gem you pick up joins the orbit for 6 s (6 orbs at most). |
| DOUBLE AGENT | DAG | The next enemy you hit (not an act boss) fights for you for 8 s: it hunts the others, can't hurt you, you can't hurt it, its kills score for nobody. Once every 30 s. A green shell marks it. |
| GRAVEDIGGER | GRV | Corpses you walk over give up a gem (once each); every 5th grave a big one (25 xp) under a gold beam. |
| BLACK MASS | BMS | Every 9th kill pulls every gem on the map to you (the vacuum). |
| DEADEYE | DEY | Every 5th hit you land (not splash) is a headshot (x1.5, the headshot effects). |

  Each has its own sound and effect (arcs, a cyan echo ring, red mist flowing into you, a mint ring, blood motes, a mirror shatter, the phoenix fire nova, purple orbs, a green turncoat beam, grave dust, a collapsing purple ring, a gold head glint). A small gold row above the corner HUD lists the legendaries held: MIRROR / PHOENIX dim once spent, DOUBLE AGENT counts its seconds or its cooldown, MAGNETAR its orbs, BLACK MASS its count to 9.
- **Dev commands** (server, test runs only): `dsoffer legendary [0-11]` (the next offer holds one; `rlwin` deals it), `dstally` (show the last tally, or a sample, over the game), `dslegend <0-11>` (give the player a legendary this floor), `dslgtest pull <client> [dist] | hit <client> <dmg> | hurt <client> <dmg> | bm <n> | agentkill`, `dslgstate`. Everything logs `Legend:` lines to games.log.
- **API** (g_descent.h, CARDS section): `G_DS_CardsGrantLegendary()` (ds-secrets' stash: the next won floor's offer holds a legendary, run key `lg_owed`).
- **Tests:** `tools/cards_test.sh <cfg> menu q3dm6 W H` with cfgs in `tools/cards_cfgs/` (`cards_reveal`: tally + reveal + ding; `cards_l1`..`cards_l4`: the legendary effects), `python tools/cards_sheet.py <dir> <out.jpg>` makes a contact sheet. Sounds: `python tools/make_sounds_cards.py`.

### Secret stashes (round 3)

Every fight, elite and event floor hides **1-3 old strongboxes** (iron-banded oak, a brass lock plate with a scratched skull). There is no marker: listen for a faint hum (you hear it only within a few steps) and watch for a glint at the lock now and then. Walk up to one and it creaks open. The loot pops out, a loot beam marks the spot for a few seconds, and **SECRET FOUND 2/3** slides in at the right edge with the name of what you found. Boss and vault floors have none. Switch: `g_dsSecrets` (F10 > DESCENT, "Secret stashes").
- **Where:** no map file is touched. When a map loads, the game reads the bot navigation data (AAS) and scans the level (a few ms a frame, done within seconds, then cached in `rearmed_secrets_<map>.txt` in the mod home; a different map version rescans). It looks for:
  - out-of-the-way floor: dark corners and spots behind pillars (seen from few places), under low ceilings and stairs, dead ends, far from items and spawns, always pushed back against a wall or into a corner;
  - places stock movement can't reach (no bot route there: tops of structures, walls and ledges, botclipped spots). Each one is tested by running the player's real movement code (Pmove) from nearby floor: a jump, double jump, dash, wall run and their combos, and the zip line fired at walls and ceilings around the spot; the grapple is judged by a clear line and a drop onto the spot. If a plain jump gets there, the spot counts as walkable.
  - Spots in liquids, lava and slime, in hurt or no-drop volumes, on movers, near teleporters, jump pads, items and spawns are rejected.
- **Abilities gate them:** a stash that needs a move (double jump, dash, wall run, zip line, grapple) only appears once you own one of the moves that reach it. The choice is seeded by the run and the floor, and better-hidden spots are likelier.
- **What's inside** (luck raises the rare ones): GEM CACHE (52%), GOLD CACHE (22%, red gems and a few skulls), STRONGBOX (14%, an elite chest), OVERDRIVE (~7%, the power spike, ds-frenzy) and LEGENDARY (~5%, your next won floor's card offer holds a legendary, ds-cards).
- **Counts:** the floor stat `stashes` (DSST_STASHES), lifetime **SECRET STASHES FOUND** on the Records screen. Finding 5 unlocks the secret card **DOWSING ROD**: stashes hum twice as far, and a faint bearing dot on a thin arc at the top edge points to the nearest one, with a soft tick that quickens as you get close. It's information, not power, so the single-digit rule doesn't apply.
- **Bots ignore stashes.** Only you open them, so bots never take one from you. The box is solid; it stands against walls, out of the way.
- **Spots found** (37 maps scanned, `tools/secrets_scan.py`): 3-15 per map. Spots that need a move turned up on q3dm11, q3dm12 (3), q3dm13, pro-q3dm13, q3dm15 (zip only), rm_lost, q3tourney5 (2), q3tourney6 (double jump + dash), q3ctf2 (2, zip) and q3ctf3. Every other map has only walkable spots, because the stock id maps are well connected for bots.
- **Dev commands** (server console): `dssecrets list | show [plain] | hide | load | scan | pick [n] | put <spot> [type] | tp <spot> [launch|run|on] | tpstash <n> [on]`, `dsstash <type 0-4> [here]`. `g_dsSecretsDebug 1` logs the scan details and bots walking past stashes. With 2 it scans in any mode; 3 quits when done (batch), and 4 also ignores the cache.
- **Tests:** `python tools/secrets_scan.py <maps> [--force]` (hidden dedicated servers, homes in `home_sec/`), `tools/secrets_test.sh <map> <cards csv|-> <cfg> [seed]` (a DESCENT fight floor from a fixture run in `home_test/secrets`, cfgs in `tools/secrets_cfgs/`). Assets: `blender --background --factory-startup --python tools/blender/make_stash.py` (models, textures, `scripts/rearmed_secrets.shader`), `python tools/make_sounds_secrets.py`.

### Surprises: ambushes, floor modifiers, wild events, ghosts (round 3)

Four DESCENT-only surprises, each with a switch on F10 > DESCENT: **Mid-floor ambushes** `g_dsAmbush`, **Floor modifiers** `g_dsModifiers`, **Wild events** `g_dsWildEvents` and **Ghosts of past runs** `g_dsGhosts`. All are on by default. Announcements slide in at the **right edge** (under the style rank), never in the middle of the screen. Code: `game/g_ds_surprise.c`, `cgame/cg_ds_surprise.c`, `q3_ui/ui_ds_surprise.c`, tables in `game/bg_ds_surprise.c`.

What an ambush, a wild event or a ghost brings is rolled from the run seed and the floor, so quitting and coming back doesn't re-roll it. They only come on **fight and elite floors from floor 2 on**: never on event, boss or vault floors. Nothing starts while the floor's own bots are still arriving (ds-arrivals).

**Nothing ever appears within 600 units of you without a warning.** An ambusher, a ghost or a fodder bot lands on a spawn spot at least 600 units away (by preference 900+ and out of sight) with a pillar of light and a sound first. If you walk up to the pillar, it lands somewhere else that is far away. Tests: 27 arrivals, the closest 839 units away.

- **Mid-floor ambush** (about 30% of eligible floors, never two floors in a row, never the same mini-boss twice running):
  - **When:** partway through the floor, once 45-60 s have passed and either the leader has 35% of the frags or 80-100 s have passed.
  - **The warning:** "AMBUSH: THE HEADSMAN / INCOMING - BEHIND 53m" at the right edge, a war horn, and a beam of light in the mini-boss's colour where it will land. It lands 4 s later with a thump.
  - **The mini-boss:** one life, the elite aura, two guns, and two of three strong sides like the act bosses. It hunts everyone, rival bots too, and never runs for health. A slim name and health line stays at the right edge while it lives.
  - **Its attacks:** borrowed from the bosses and drawn with the same telegraphs, aimed at whoever is nearest in sight (you a little more).

| Mini-boss | Sides | Attacks |
|---|---|---|
| THE HEADSMAN | fast, deadly (x0.75 health) | cleaver charge, meat hook |
| THE WARDEN | tanky, deadly (slow) | rocket barrage, ground slam |
| THE WRAITH | fast, tanky (x0.75 damage) | rail lances, meat hook |
| THE PYRE | tanky, deadly (slow) | meteor strikes, ground slam |
| THE HOUND | fast, tanky (x0.8 damage) | ground slam, cleaver charge |

  - **Strength:** health = 100 + the elite's 60 (+10 per tier) + (140 + 8 x floor) x its health side x the square root of your power score (G_DS_PowerScore, 0.8-4). That is 277 (Headsman) and 378 (Warden) on floor 2 at power 1.18. It also has the elite armour (50), and the usual power matching applies to it.
  - **Kill it yourself:** the elite chest and its gems (LOOT), 30 + 5 x floor more gems, 4 + floor/3 skulls (x the difficulty), a 35% chance of an OVERDRIVE pickup (ds-frenzy) and a purple beam. If a bot gets the kill, you get nothing.
- **Pick-your-poison floor modifiers:** about 38% of fight and elite nodes on the floor map (from floor 2, not the first floor of an act) carry a red chip under the node: "[ ] ARMOURED". Click it, or Tab to it and press Space, to take the risk. The info panel shows the risk and the reward. Floors further down show a small red mark where a modifier waits. Enemy-side numbers can go past single digits. The reward is loot, never a stat boost:

| Modifier | Risk | Reward |
|---|---|---|
| ARMOURED | enemies spawn with +50 armor | +25% skulls (won floor) |
| BLOODLUST | enemies deal +20% damage to you | +1 elite chest (won floor) |
| BLACKOUT | the world goes dark; gems and chests glow through it | +30% gem xp |
| SWARM | 2 more bots | +40% gems |
| GLASS | everyone has half max health, you too | +25% skulls (won floor) |
| FERAL | enemies move 15% faster | +1 card to choose from |

  The modifier's skulls are a separate line of the floor reward: the run key `sk_md`, CS_DESCENT `\md\`, "floor modifiers" in the run summary and a MODIFIER chip in the floor-end tally.
- **Wild events** (about 20% of eligible floors, so roughly 1 floor in 6-7 overall, spread evenly over the four). Each is announced at the right edge with a gong and a voice line. A name and timer line stays there while it runs.
  - **GOLD METEOR:** a golden rock falls for 6 s onto a spot 800-2400 units away, with gold rings on the ground and a beam. The impact deals 60 damage within 260 units. The glowing crater holds 70 + 8 x floor xp of gems and an elite chest under a gold beam. About half the bots race for it, and a bot that reaches the chest first takes it ("SARGE SNATCHED THE GOLD"); the gems stay.
  - **BOT UPRISING:** for 30 s, up to 6 weak skeleton bots (Thrall, Husk, Ghoul ...) pour in through purple portals at spawn spots 800+ away. They have 50 max health and deal 0.55x damage, and a dead one is replaced 1-3 s later. They fight everyone and drop the usual gems, so they feed the chain. Killing them is no frag, and their kills don't count in the frag race. When the event ends the rest crumble away.
  - **BLOOD ECLIPSE:** for 25 s every kill drops a second gem spray, and the screen edges turn red.
  - **BOUNTY:** the leading bot carries a chest for 40 s, marked by a purple beam that follows it. Kill it for an elite chest, 20 + 4 x floor gems and 2 + floor/4 skulls. If another bot kills it, the bounty passes to the killer.
- **Ghost of a past run:** at every floor start the game writes your kit at that depth to `rearmed_ghost.txt` in the mod home: character, cards, weapon levels, passive levels and evolutions. A new run moves the last run's snapshots to `past_<floor>`; each depth keeps the latest run that reached it.
  - **When:** on an eligible floor with a past snapshot and no ambush, about a third of the time, 30-55 s in.
  - **What comes:** "GHOST OF <YOUR NAME>" with your past kit and your character's bot. It has the past weapon levels exactly, with no catching up, and carries the guns you had levelled. It looks like a pale, see-through blue-white shell with a cold light and hunts you above anyone else.
  - **Kill it yourself:** **YOU SURPASSED YOURSELF**, 5 + floor/3 skulls, 40 + 6 x floor gems, an elite chest under a gold beam and a short slow-mo. If someone else kills it, "YOUR GHOST FADED".
- **Extras** (ambusher, ghost, fodder) join mid-floor as extra bots with kits planned in `G_RL_AddExtraBot` and their models preloaded with the floor. They are out of the frag race both ways, never become your nemesis and leave when they die. Floors launch with `sv_maxclients` 16 to make room.
- **Dev commands:**
  - Server: `dsambush [0-4 | headsman ...]`, `dsevent goldmeteor | uprising | eclipse | bounty | end`, `dsghost [floor]` (uses this run's snapshot if there is no past one), `dsmod <name>`, `dssurprise` (state).
  - `dssurprise kill ambush|ghost|bounty|fodder` (you kill it), `dssurprise look [gold|ghost|...]` (puts you 650+ units away, facing it).
  - `dssurprise sim`: the plan rolls over 2000 runs x 22 floors.
  - `g_dsSurShots 1` (tests): the game takes its own screenshots at the key moments.
  - UI: `dsmods` (the offers on the next floors), `dsmodnode <reach index> [on|off]`. `ui_dsModAll 1` puts a modifier on every eligible node.
  - Logs: `DSSurprise:`, `DSEvent:` and `DSGhost:` lines.
- **Tests:** `tools/surprise_test.sh <cfg> <seed> W H [map]` (floor 2 from `tools/surprise_fixtures/run_floor2.txt` with a fixture ghost track; env `ARCH`, `NODEMOD`, `GHOST`), `tools/surprise_ui_test.sh ui_map.cfg <seed> W H` (the floor map: take a modifier, launch, win). The cfgs are written by `tools/surprise_fixtures/gen.py`: `amb_dev`, `amb_nat` (seed 1009 rolls a natural ambush), `events`, `ghost`, `mod`, `ui_map`. Assets: `python tools/make_surprise_gfx.py`, `python tools/make_sounds_surprise.py`, `python tools/make_voices.py surprise/`.

### Daily Challenge (round 4)

**DESCENT > DAILY CHALLENGE**: a fixed descent for each day (the PC's date), with a personal best table to beat. Offline and solo: nothing goes over the network.

- **The format:** act 1 only, **6 floors**: five frag-race floors you pick on the floor map, then THE BUTCHER as a **timed 1v1, 5:00 on the clock** (always, whatever your boss settings are). Hurt Me Plenty, 2 lives. The day picks the character (lent even if you haven't unlocked it) and a free starting ability. **No permanent upgrades, no skill perks, no skulls**: everyone gets the same challenge that day. Play it as often as you like; the best attempt counts.
- **The screen:** today's date and seed, your character and ability, the six floors as the floor map will show them (fight / elite / event nodes, their maps, the pick-your-poison modifiers, the boss) and your PERSONAL BESTS: today's best and attempts, today's target, your streak (days in a row with a finished attempt) and your all-time top 10. START (or CONTINUE, or NEW ATTEMPT, which scores the running one as it stands), BACK. Mouse, WASD / Tab, Space or a gamepad.
- **The score**, shown with its breakdown when an attempt ends (out of lives, or the boss dies): 1,000 a floor won (the boss 3,000), 20 a kill, 150 x your best style rank on each won floor, speed (4 a second under 4:00 on a won floor; 10 a second left on the boss clock), no-death bonus (250 a won floor without dying, 1,500 more for a flawless clear).
- **What repeats:** the floors, maps and their variants, node types and modifiers, the character and ability, the card offers (for the same picks), the legendary rolls, the chest rolls, the enemy types and affixes, champion packs, ambushes and wild events. **What can't:** the bots' decisions, physics and your aim, so the fights, kills and gem / vacuum / luck drops differ every time; the maps after floor 1 follow the path you take.
- **Your real descent is untouched:** the daily lives in `rearmed_daily_run.txt`, its table in `rearmed_daily.txt`. It never writes rearmed_run.txt, rearmed_meta.txt, the history, skills, tally, ghost or style records, and pays no skulls or unlocks. RESET ALL PROGRESS clears the two daily files too (with backups). If you quit mid-floor, CONTINUE restarts that floor; an attempt from an earlier day is scored for its own date when you next open the screen.
- **Dev / tests:** `set ds_dailyDate 20261013` fakes today's date; `dsdaily` (the screen), `dsdaily start`, `dsdailyinfo [date]` (prints the plan), `dsdailypb`, `dsdailyscore`. `tools/daily_test.sh <cfg> [W H]` runs a cfg from `tools/daily_cfgs/` in `home_test/daily`. Code: `game/bg_ds_daily.h`, `game/g_ds_daily.c`, `q3_ui/ui_ds_daily.c`.

### Elite affixes and champion packs (round 4)

Diablo-style affixes on DESCENT's tougher enemies. Two switches on **F10 > BOTS**: **DESCENT elite affixes** `g_dsAffixes` and **DESCENT champion packs** `g_dsChampions` (both on). Never on boss or vault floors. Code: `game/g_ds_affix.c`, `cgame/cg_ds_affix.c`, ids in `game/bg_ds_affix.h`.

**Who gets them:** the floor's elite (decided with the roster, so the loading screen and the arrival strip name them under its portrait: "FROST VAMPIRIC"), ambush mini-bosses, champion packs, and your nemesis as it levels up (en-personality). **How many:** 1 in act 1, 2 in acts 2-3, 3 in act 4 and the Abyss. The roll comes from the run seed and the floor (the daily challenge gives everyone the same affixes).

| Affix | What it does | The tell on the bot |
|---|---|---|
| VAMPIRIC | heals 30% of the damage it deals | red mist and blood droplets, a red streak from whoever it bites |
| EXPLOSIVE | its corpse swells for 1.3 s (rising hiss), then blows up (55 + 2.5 per floor, x your power, radius 210) | a pulsing red-orange core light, spitting sparks |
| SPLITTING | its first death tears it into two copies (half health, 60% damage, one life each, no further splits) | three violet shards orbiting it |
| REFLECTING | 20% of the damage it takes goes back to the attacker, never below 1 health | white glints; a chrome flash on the body and a ping when it reflects |
| FROST | its hits slow you to 70% for 1.3 s | ice crystals drifting down, a cold light; your screen edges frost over while slowed |
| BLAZING | drops short fire patches as it runs (its own fire doesn't burn it) | amber embers at its feet |
| MAGNETIC | every 7-10 s, in sight 260-1100 units away: a 0.9 s wind-up (a teal tether to you, a ring under it, a rising whine), then it yanks you toward it (one wind-up at a time on you, 2.5 s apart at least). Break line of sight to snap the tether. | teal motes spiralling in |
| REGENERATING | 5% health a second after 4 s out of combat | pale mint motes and a chime while it heals |

**Champion packs:** on 14% (act 1, from floor 2) to 30% (the Abyss) of fight / elite floors, 50-80 s in and after the floor's own bots have arrived, three gold pillars rise side by side far from you ("CHAMPION PACK / FROST BLAZING / INCOMING - BEHIND 40m" at the right edge). 3 s later three champions land together. They share one affix set (never SPLITTING), wear a faint gold sheen, have one life each, are out of the frag race and hunt you. The line then counts them down ("CHAMPIONS 2/3").

**Power and loot:** affixed bots trade a little health and damage for their affixes (x0.89-0.98 by affix count), so the total threat still follows your power score; copies and champions are weaker one by one. Affixed kills pay more: elites and mini-bosses +6 gems per affix and a skull per affix, with 2+ affixes a 35-70% roll for an extra chest; each champion 10+ gems and a skull, the last one of a pack a chest, a gold beam and 2-4 skulls.

**Where the names show:** under the elite's portrait in the arrival strip / loading roster, in the ambush landing callout, under the bot's name on TAB (in colour, "CHAMPION" in gold), and the champion line at the right edge. Nothing over their heads.

- **Dev (server console, DESCENT floors):** `dsaffix next <affixes> [n]` (the next n bots to join get them), `dsaffix give|set <client|all> <affixes>`, `dsaffix kill|hurt <client> [dmg]` (as if you did it), `dsaffix look <client> [dist]`, `dsaffix lookpack`, `dsaffix frost`, `dschampions [affixes]` (a pack now), `dsaffixstate`. Affixes: `vampiric explode split reflect frost fire pull regen all 0x<mask>`. Logs: `DSAffix:` lines (vampire heals, reflected shares, frost slows, splits, pull distances, rewards). `cg_dsAffixTells 0` hides the particle tells (performance / screenshots; 2 = without the vampire light).
- **Tests:** `tools/affix_test.sh <cfg> <seed> W H [map] [+set ...]` (floor 8, an act 2 elite floor, from `tools/affix_fixtures/run_elite8.txt`). The cfgs come from `tools/affix_fixtures/gen.py`: `tells` (fights with affixes, one tell per bot, explode + split, the frost edge, TAB, a champion pack), `pics` (frozen bots, one affix at a time), `mech` (an affixed field and a pack against you). Assets: `python tools/make_affix_gfx.py`, `python tools/make_sounds_affix.py`.

### Enemy types (round 4)

On DESCENT fight floors some bots come as a **type**: a different model, a different gun and a different way to fight. You read a type from the bot itself (its silhouette, colours, lights and sounds), never from text over its head. The name only shows at the edges: the arrival strip says "SNIPER - ANGEL", and the TAB row ends in the type in its colour. Switch: F10 > BOTS **DESCENT enemy types** `g_dsTypes` (on by default).

| Type | Looks like | What it does | How to beat it |
|---|---|---|---|
| SNIPER | visor, railgun | holds a high perch with a long sightline. **Every shot comes after a 0.7 s red laser and a glint on its head**, aimed at its target, with a rising whine. It moves to a new perch after each shot | break line of sight when the laser lands on you, then push its perch while it relocates |
| RUSHER | slash, shotgun | fast (1.18x), charges in and circles close, never retreats | keep moving back and to the side, splash its feet |
| MEDIC | lucy, green back light | a **green beam heals the most hurt ally** within 650 units (12 hp/s), it stays behind its patient; its own gun is weak | kill it first |
| SHIELD | keel with a cyan riot shield | the raised shield **stops 80% of direct hits from the front** (sparks and a clang); every 2-3 s it lowers the shield to its side for about a second to fire | shoot its back or sides, splash its feet, or hit it while the shield is down |
| KAMIKAZE | small red orbb | runs at you **beeping faster the closer it gets**; within ~170 units the fuse lights (frantic beeps, a hot orange glow) and 1 s later it blows (85 damage, radius 250) | back off when the beeping turns frantic, or kill it early: it pops where it stands (a smaller blast that hurts other bots too) |
| BLINKER | mynx, lightning gun | short flanking teleports to your side or back with a magenta flash. **A blink never ends within 400 units of you** | keep your back to a wall; it is fragile |
| SUMMONER | ranger (wrack), grenades | walks to a corpse at least 650 units from you and stands still through a **1.5 s raise ritual** (a pale ring and pillar), then a skeleton climbs out. At most 2 of its own and 3 on the floor | interrupt the ritual: it can't move or shoot while it chants |
| MIMIC | **your own model**, inked dark | carries a copy of **your best weapon at your level** | it has your gun, not your moves |
| JUGGERNAUT | tankjr at 1.25x size, gunmetal | 1.8x health + 50 armour, slow (0.78x), chaingun (rockets at range), the ground shakes under its steps; only big hits stagger it | keep your distance and kite it |

**Fair by design.** A type changes how a bot fights, not how much: health x damage is roughly that of a plain bot, and its trick pays for the rest. The floor's tier, the bot's kit and the scaling with your power score still apply on top.

**Which types show up** depends on the act. Act 1 has one or two typed bots among plain ones (sniper, rusher, shield, kamikaze). Act 2 makes about half of them typed and adds medics, blinkers and juggernauts. Act 3 makes two thirds typed, adds summoners and mimics, and pairs them up: a medic brings a shield or a juggernaut, a summoner brings kamikazes, a sniper brings a shield. Act 4 and the Abyss type everyone but one bot. Elites carry a type 30% of the time in act 1, then 60%, 85% and always. Ambush mini-bosses may get a shield or blinks from act 2 (they keep their own look). Boss and vault floors have no types. The mix comes from the run seed and the floor, so quitting and coming back (and the Daily Descent) gives the same one.

**The juggernaut is hit where it is drawn.** It is drawn at 1.25x, and since 0.6.0 its hit box is too: 37.5 units wide and 70 tall instead of 30 x 56 (crouched and dead boxes scale the same way, so the corpse gibs where you see it), and headshots start at its drawn head (31 units above its origin instead of 20). Only the hit box grows: it still walks, jumps and fits through doors with the normal player hull, so its routes don't change. Next to a wall or another player the box narrows to the normal width for that frame (it keeps the taller head), and on a lift or someone's head it is the normal box.

**Types that remember you** (`g_dsTypeAdapt`, console, default 1). The types that killed you most in your last runs adapt a little in your next one. At the start of the run a short line at the right edge says so, for example "THE SNIPERS REMEMBER YOU: Their glint is shorter. Strafe early." At most two types adapt, and a type needs about three deaths in a run to qualify. It fades when you stop dying to it: its weight halves with every run, plus the deaths of the run just played.

| Type | What changes when it remembers you |
|---|---|
| SNIPER | the glint before a shot is 0.56 s instead of 0.7 s, and it leaves an idle perch after 5 s instead of 8 s |
| RUSHER | it uses the full flanking tactic (0.4 before) and charges at a point 220 units off your side, then straight in |
| MEDIC | it stays 260 units behind its patient instead of 170 |
| SHIELD | the shield covers +-70 degrees instead of +-60 |
| KAMIKAZE | once per floor, a planned kamikaze brings a second one |
| BLINKER | blinks 3.6-6.1 s apart instead of 4.5-7 s |
| SUMMONER | raises every 7 s instead of 9 s |
| MIMIC | nothing extra beyond the pick weight below |
| JUGGERNAUT | switches to rockets beyond 600 units instead of 850 |

An adapted type is also picked 1.5x as often, and it pays for its trick with 5% of its health (power budget: the floor's G_DS_PowerScore scaling still applies on top). Daily runs and boss practice neither count your deaths nor adapt. The counts go into the run file at each floor's end (`tad_f`), and the menu folds them into `rearmed_meta.txt` once per floor (`tad_cur` = this run, `tad_w` = the weights x10, `tad_seed`). RESET ALL PROGRESS clears them. Dev: `dstadapt` prints the weights, this floor's deaths and what is adapted; `dstadapt <type> [n]` counts n deaths to it (tests); games.log `DSTypeAdapt:` lines. Tests: `python tools/tune_adapt.py` (game side, hidden server), `bash tools/tune_adapt_ui.sh` (the menu's merge, a small window).

Juggernaut hit box dev: `dsjugtrace <client>` fires MASK_SHOT traces at the drawn chest, head, head top, both shoulder edges and two points outside it (a corpse: its lying box) and logs HIT / miss / HEADSHOT; `set g_dsJuggBox 0` gives back the old box for comparison. `g_dsTypesDebug 2` logs `JuggMove:` every 5 s (position, distance moved, frames with the big / tall-only / plain box). Tests: `python tools/tune_jugg.py trace|move` (hidden servers, every bot a juggernaut).

Dev (server console, devmap): `dstypes` lists the bots and their types; `dstypes sniper,medic,none,...` retypes the bots now and keeps the mix for later floors (`g_dsTypesForce`; `dstypes clear` forgets it); `dstype <client> <type>`; `dstypeact <client|type> blink|fuse|raise|tell|shieldup|shielddown`; `dstypestats`; `dstypesim <floor> <bots> [runs] [elite]` prints the natural mix over seeds. `g_dsTypesDebug 1` logs every shield hit (FRONT / SIDE / BACK / SPLASH and the damage before and after), heal tick, blink (with the distance to you), fuse, pop and ritual. Test: `FORCE=... BOTS=9 FLOOR=14 bash tools/types_test.sh look.cfg|tells.cfg|behave.cfg|kami.cfg|sim.cfg|intro.cfg [map] [W H]` (isolated home `home_test/types`). Code: `game/g_ds_types.c`, `game/ai_types.c`, `cgame/cg_ds_types.c`, table `game/bg_ds_types.c`. Assets: `tools/make_sounds_types.py`, `tools/make_types_gfx.py`, `tools/blender/make_shield.py`.

### Enemy personality: taunts, grudges, nemesis levels, veterans (round 4)
All of it stays off the screen centre: lines go to the chat corner, callouts to the right edge, ranks to the arrival strip and TAB.
- **Trash talk** (`g_botTaunts` Off / Some / Lots, every mode). Bots talk after fragging you, when you die to a barrel / a fall / your own rocket, when you're on a 5 / 10 / 15 streak, when they get revenge, when their nerve breaks, and when a veteran comes back (DESCENT). Each line comes with a short voice bark from the bot's body (male / female voice by model). Hard limits: Some = 20 s between any two lines, 60 s per bot, 3 a minute at most; Lots = 7 s / 25 s / 6. Revenge and nemesis lines may cut in after 3 s. The stock Q3 bot chat is replaced (it also made bots stand still to type).
- **Grudges** (`g_botGrudges` Off / 40 s / 90 s, every mode). A bot you kill holds a grudge and goes after you (bot brains prefer and hunt that target); a bot killed by another bot hunts that one. A grudge ends with the revenge kill ("Payback.") or when it runs out. DESCENT: when YOU kill the bot that last killed you, +1 skull (x difficulty, 3 a floor at most), REVENGE at the right edge.
- **Attack shouts** (`g_botShouts`). Right before a big wound-up attack the bot shouts: the kamikaze "For glory!", the summoner "Rise!", a pull affix "Get over here!", others "Here it comes!" / "Eat this!". The sniper stays silent (its glint is the tell).
- **The nemesis levels up** (`g_dsNemesisLevels`, DESCENT). It is level 1 when it takes the title and gains a level every time it kills you again (max 6, kept for the run): damage dealt +8% at level 1 up to +23% at 6, damage taken -7% up to -17%, +10 health and +15 armour per level, weapons +1 level at 2 and +2 at 4, an affix at 3 and a second at 5 (vampiric / regenerates / frost / fire trail / reflects), a title from level 2 ("VISOR THE RELENTLESS"). It announces itself at the right edge when it arrives and on each level up. Killing it pays 3 + floor/4 + 2 per level past 1 skulls and more gems.
- **Veteran bots** (`g_dsVeterans`, DESCENT). Every bot you meet joins the run's roster: +2 xp per floor it was on, +3 for each time it fragged you, +1 per bot kill (3 a floor). Ranks I-V at 4 / 9 / 15 / 22 / 30 xp: +4 health, +8 armour and +2% damage per rank, weapons +1 level from rank III, an extra kit card at II and another at IV. Up to two veterans come back on each fight floor ("We meet again." / "Moved up since last time."). Rank shows as pips on the arrival strip and the loading roster ("VETERAN II", "PROMOTED"), as pips after the name on TAB, and as a small shoulder light (bronze I-II, silver III-IV, gold V; the nemesis has a pulsing red one on the other shoulder).
- Saved in the run file (`vet_roster`, `nemesis_level` / `_title` / `_affix`): a new run starts a new roster, Reset All Progress clears it.
- Dev: `prstat`, `prsay <client> <kill|env|streak|revenge|vow|flee>`, `prgrudge <bot> <target> [sec]`, `prkill <victim> [attacker]`, `prflag <client> <DSEF bit> <0|1>`, `dsvet [name rank | clear]`, `dsnemesis level <n>`, `cg_prTells 0` (no shoulder lights) / `-1` (none of the personality HUD). Test harness `tools/pers_test.sh` (cfgs from `tools/pers_fixtures/gen.py`), voices `tools/make_taunt_voices.py`.

### Bot memory and the Nemesis Hall (round 4)
Named bots remember you across DESCENT runs (`g_botMemory`, F10 > BOTS "Bot memory": Off / On / Talk only, default On; the console works too).
- **The record.** Every named bot keeps a lifetime record with you in `rearmed_bots.txt`: its kills on you and yours on it, floors and runs met, the last meeting (date, floor won or lost, that floor's score), how often it was your nemesis and its peak level, your headshot and revenge kills on it, its revenge kills on you. It is written once at the end of every floor (never mid-floor: quit a floor and it counts nothing). Daily challenge floors and boss practice / rematches neither read nor write it. The record keeps growing with `g_botMemory 0`, so turning it back on picks up where you were.
- **Greetings that escalate.** When a bot that knows you arrives, it says so in the chat corner, quoting the real numbers and your in-game name: a stranger ("First time? Welcome to the pit."), familiar ("You again, AshWolf." / "We've met. 3 to 2, me."), rival from 6 floors or 12 kills between you ("You again, AshWolf. 7 to 6, me."), arch-rival from 15 floors or 30 kills ("Our 30th dance, AshWolf.", "Round 30, AshWolf."), and an old nemesis ("Your nemesis 2 times, AshWolf. Count them." / "You put me down with a Railgun. Not again."). One or two a floor (three with Taunts on Lots), the most significant bot first; the best rival also gets a short callout at the right edge (RIVAL / ARCH-RIVAL / OLD NEMESIS, YOU 9 - 12, 23 FLOORS). When such a bot kills you it may quote the new score ("13 to 9 now, AshWolf."), and a rival you kill may grumble ("10 now. I'm keeping score."). Everything goes through the Taunts limits: `g_botTaunts 0` silences it all.
- **Lifetime grudges.** A bot that keeps losing to you (4 or more behind) hunts you for its first minute on a floor and again for 30 s every 2 minutes (the grudge en-brains' bots act on; Grudges off turns this off too).
- **Fear.** A bot you dominate (8+ kills and three times its own) backs off to heal a little sooner, would rather fight someone else, and now and then panics when you come close ("Not AshWolf! Anyone but AshWolf!"). Only enemy behaviour changes: nothing on your side gets a boost. "Talk only" (`g_botMemory 2`) keeps the lines and leaves the grudge and fear behaviour out.
- **The Nemesis Hall** (Records > NEMESIS HALL, or `dshall` in the console): every nemesis of your runs, newest first, with its portrait, name and title, peak level (pips), kills on you, the run and floor where it rose, and how it ended: SLAIN BY YOU (weapon, floor), OUTLIVED YOU (your run died with it still the nemesis), DETHRONED BY another bot, SURVIVED - THE RUN ENDED (won, banked or abandoned), or STILL HUNTING YOU in the run in progress. The panel on the right adds the bot's lifetime record with you. The RIVALS tab lists every bot that remembers you, most history first, with its tier, your score against it and whether it fears you or holds a grudge. Tab / Q / E / shoulder buttons switch the list, W / S, the wheel, the d-pad or the mouse pick, Esc / B / right mouse go back.
- Reset All Progress backs up and empties `rearmed_bots.txt` with the rest.
- Dev: `dsbm` (list the records and the hall), `dsbm set <bot or client> <its kills> <your kills> <floors> <runs> [times nemesis]`, `dsbm say <bot or client>` (its greeting now), `dsbm clear`; `dshall [rivals | records]` (UI). Log lines `BotMem:` in games.log. Test harness `tools/mem_test.sh <cfg> [W H]` (isolated home `home_test/mem`, cfgs from `tools/mem_fixtures/gen.py`: `mem_tiers.cfg`, `mem_flow.cfg`, `mem_hall.cfg`; `MD5=1 CFGDIR=tools/daily_cfgs ... daily_play.cfg` checks that a daily leaves the file alone).

## Style, perks and records

- **Style meter** (`g_style`, HUD `cg_style`): kills, headshots, airshots and juggles, charged alt-fire kills, element combos, reflected rockets and gibs build a rank from D (Decent) up to SSS (Slaughterhouse). An announcer calls out each new rank. Getting hit and standing idle drain it, and dying resets it. From S up, every kill gibs.
- **Style sources** also include slam dunks, barrel kills, wall splats, chain reactions, meteors, thorns, hooks, slide kills, blink kills, ricochets, decapitations, and double and multi kills. Style gain is 1.5× during a random event.
- **Slow motion** (`g_slowmo`, single player only): the match-winning kill, your double and multi kills, headshot finishers, gibs and SSS kills slow time to 0.3–0.45 speed for about a second, easing in and out (at most one every 6 s; the match winner always gets it).
- **Perks** (`g_perks`, Normal mode): the first offer comes after 2 kills, then one every 3. Press 1, 2 or 3 to pick, or the first is chosen for you after 15 s. Perks stay for the whole match, up to 8, and each offer mixes categories (offense, defense, movement, chaos). Bots pick by personality.

| Perk | Effect |
|---|---|
| Thick Skin | +25 max health |
| Heavy Caliber | +15% damage |
| Quick Hands | fire 20% faster |
| Third Wind | a third dash charge |
| Overcharger | alt-fire charges 40% faster |
| Bloodthirst | 15% of damage dealt heals you |
| Headhunter | headshots do double damage |
| Sky Hunter | +30% damage to airborne enemies |
| Pyromaniac | your dashes leave fire |
| Conductor | your lightning arcs as if everyone were soaked |
| Ironclad | armor regenerates up to 50 |
| Scavenger | kills refill your weapon |
| Adrenaline | kills refill your dashes and heal 10 |
| Glass Cannon | +35% damage, max health 65 |
| Blink | your dash is a 220-unit teleport |
| Ground Zero | a 1.5× bigger ground-pound shockwave |
| Slipstream | slides last twice as long and don't slow down |
| Ricochet | rockets, grenades and plasma bounce off the first wall |
| Chain Reaction | your victims explode a second after dying |
| Last Stand | under 30 health: +50% damage dealt, -25% taken |
| Featherweight | a third jump, no fall damage |
| Magnet Hook | your grapple yanks enemies to you |
| Butcher | gib kills drop a health bubble |
| Kinetic | +1% damage per 10 ups over 320 (up to +50%) |
| Mirage | dashing makes you invisible for 1.5 s |
| Thorns | 30% of the damage you take hits back |

In DESCENT the perks follow the single-digit rule (their cards show these numbers): Thick Skin +8 max health, Heavy Caliber +6%, Quick Hands fire 6% faster, Overcharger 9% faster charge, Bloodthirst 5%, Headhunter +9% headshot damage, Sky Hunter +8%, Ironclad armor up to 25, Adrenaline heals 5, Glass Cannon +9% damage / -9 max health, Ground Zero +9% shockwave, Last Stand +9% dealt / -9% taken, Kinetic up to +8%, Thorns 8%. The mechanical perks (Third Wind, Blink, Ricochet, Chain Reaction, ...) work as in Normal.
- **Records** (`records` in the console, saved to `home/rearmed/rearmed_records.txt`):
  - Your fastest win on each arena, per difficulty, shown at the end of the match.
  - Weapon mastery: Bronze, Silver, Gold, Platinum and Diamond at 25, 100, 250, 500 and 1000 kills.

## Random weapons

`g_weaponRandom` (0–100, default 25; a slider in the REARMED menu) is the chance that each weapon or ammo spawn is a random weapon instead of the map's own. It's rolled again every time the item respawns, so the arsenal keeps changing during a match.

## Mutators

With **Mutators** on (`g_mutators 1`, the default), a random 35-second event hits the whole match every 60 seconds (`g_mutatorInterval`, `g_mutatorDuration`). A siren, a voice and a 3-second countdown announce each one. While it runs, its name, a timer bar and a screen tint stay up (`cg_eventFX`).

| Mutator | Effect |
|---|---|
| Low Gravity | 35% gravity |
| Vampire | the damage you deal heals you |
| Rocket Frenzy | everyone gets a rocket launcher with infinite rockets |
| Speed Rush | everyone moves 40% faster |
| Big Heads | heads are huge, and so is the headshot zone |
| Knockback Chaos | 3× knockback |
| Quad Madness | everyone has quad damage |
| Jetpack Party | everyone has a jetpack |
| Blood Moon | every kill gibs, 25% lifesteal, red sky and blood rain |
| Meteor Shower | burning rocks fall on open ground; a glowing ring marks each impact |
| Overcharge | alt-fire charges three times as fast |
| Bullet Time | every kill slows time (single player) |
| Ice Rink | almost no ground friction |
| Barrel Bonanza | 12–18 explosive barrels and gas tanks drop from the ceiling |
| Weapon Roulette | everyone gets a new random weapon every 8 seconds |
| Thunderstorm | lightning strikes near players under open sky, with a warning marker first |
| Chain Reaction | every corpse explodes |

Meteor Shower and Thunderstorm only come up on maps with open sky.

Mutators are picked by weight, so the big ones come up less often, and the same one never runs twice in a row.

A DESCENT event floor runs its event until the floor ends, with THIS FLOOR on the plate instead of a timer bar. Dev: `mutator <n> floor` starts one for the rest of the map; `mutator <n> now` starts a timed one.

## Physics and gore

- **Props** (`g_props`, `g_propCount`, `g_propRespawn`): 4–8 per map near items and spawns (mostly crates, at most a third explosive).
  - Explosive barrels chain-react and burn when holed.
  - Gas tanks (the yellow cylinders) rupture when shot: a hissing gas jet spins them off like a rocket, and they blow about 1.5-2 s later.
  - No prop ever just vanishes: a lit one that gets stuck or flies off the map explodes where it is. Dev: `g_propTest "hit <barrel|crate|tank|all> <damage>"` shoots every resting prop of that type.
  - Crates break and sometimes drop health or ammo.
  - The props are made in id's 1999 style (riveted iron cask, ochre pressure vessel with a sight glass, plank crate). Source: `tools/blender/make_props.py`. Rebuild: `bash tools/blender/build_all.sh barrel tank crate debris`.
  - All of them take knockback, so you can rocket a barrel into someone, kick it by running into it, or slide into it. Destroyed props come back 30 s later somewhere else.
- **Corpse physics** (`g_corpsePhysics`): bodies fly from blasts and flip in the air. Knock someone into a wall hard enough for a **wall splat**.
- **Debris** (`cg_physicsFX`): rock chips and shrapnel off walls. Blasts push gibs around, and players kick them as they run through.
- **Gore** (`cg_gore` 0–3, server `g_gore`):
  - Headshot kills decapitate; the neck pumps blood.
  - Overkills explode into lots of chunks, and charged rails cut people in half.
  - Burn and lightning kills leave charred, smoking bodies.
  - Every hit sprays blood onto the wall behind the victim and it runs down. Walking through blood leaves footprints.
- **Corpses and blood stay until the map ends** (`cg_gorePersist`, default 1; F10 > CHAOS > Corpses and blood: Stay / Fade). This works in every mode, DESCENT included, and everything is cleared when the map changes.
  - Corpses: up to `cg_gorePersistMax` bodies (8–64, default 64; F10 > CHAOS > Corpses kept). They can still be shot, gibbed and kicked, and you walk through them. When the cap is reached, a body nobody can see goes first. If every body is in view, the oldest and farthest one sinks quickly. Gibbed players leave only their chunks.
  - Blood: pools, splats, wall spray, drips and footprints never fade. Pools seep wider twice in the first minute after a death, and all blood slowly dries darker and browner over a few minutes. A body whose death happened out of your sight gets its pool when you first come across it.
  - The blood pool holds `cg_gorePersistMax` × 128 polys (8192 at 64), split into a keep ring (40%: pools and splatter where someone died) and a spray ring (hit spray, droplets, footprints), so heavy gunfire can't recycle the pools under the bodies. Each ring recycles its oldest blood first. Where blood piles up more than about 5 layers deep, the oldest buried layers are dropped: it looks the same and keeps the fill-rate cost bounded. Up to 2 droplet splats land on the same 16-unit spot per minute.
  - Gib chunks: up to `cg_gorePersistMax` × 4 (256), oldest first, and a chunk you are looking at is never the one removed. Bullet holes and burn marks stay too (up to `cg_gorePersistMax` × 16).
  - The renderer needs `r_maxpolys 16384` and `r_maxpolyverts 131072` (set by `rearmed_gfx.cfg`; the game raises them too, which takes effect after `vid_restart`).
  - `cg_gorePersist 0` brings back the stock behaviour: bodies sink after 5 s and blood fades. It replaces the old `cg_bloodPools`.
  - Cost, measured with `cg_goreStats` (uncapped fps, 1920x540 window, other workers' test servers running, so ±5%): after a 10-minute 6-bot match on q3dm6 the spawn view ran at 3.65 ms a frame with Stay against 3.32 ms with Fade. Worst case, looking straight at a `gorefill` pile of 64 bodies, 256 gib chunks and a full blood pool: 3.86 ms against 3.61 ms with Fade at 1920x540, and +0.85 ms at 3840x1080 (before the layer cap this pile cost 3.5 ms at 3840x1080).
  - Snapshots: the largest estimate seen was 77 entities (64 bodies in one room), far below the 256 limit. Above 200, the farthest bodies older than 15 s are left out of snapshots until there is room again (`g_corpseSnapHigh`).
  - Dev (server, cheats): `gorestats` (corpses, recycles, entity use, snapshot estimate), `gorefill <deaths> [radius]` (bots die around you), `gorelook [n]` (look at the n-th biggest pile). Client: `cg_goreStats 1` prints fps, snapshot entities and pool use every 2 s; `cg_goreKeepDebug` 1/2/4/8 switches parts off for profiling.
- **Kill cam, death cam and replays** (F10 > CHAOS). Every panel sits off to the side, so the centre of the screen stays clear. On a 32:9 screen it fills the gutter outside the 16:9 centre; on 16:9 it is a smaller box at the left edge.
  - **Side kill cam** (`cg_killCam`: 0 off, 1 notable kills (the default), 2 every kill). Notable kills are gibs and overkills, headshots, airshots and midair kills, double and multi kills, void / lava / crusher kills, the final frag, elites, bosses, broken boss phases and your nemesis. When you make one, a framed panel slides in and replays the victim dying in slow motion (about 0.6x, then 0.26x at the moment of the kill) for about 3 s. A camera orbits the body and the gibs. The panel shows the label (HEADSHOT, OVERKILL, BOSS SLAIN...), the next two labels, the victim's name and the weapon icon, with letterbox bars, grain, vignette and a flash at the kill. The main view keeps running at full speed. Only one panel shows at a time: a better kill waits its turn (or cuts in if it's much better), and kills in the same spot join the one already showing.
  - **Side** (`cg_killCamSide`): Auto (on wide screens, the side of the screen the kill happened on; left on 16:9), Left or Right.
  - **Death cam** (`cg_deathCam`): when a player kills you, the panel replays the last 1.5 s around your killer in slow motion. It shows their name, the health and armor they had left and their weapon, and it closes when you respawn.
  - **Best kill replay** (`cg_replays`): your highest-scoring kill of the match (DESCENT: of the floor) replays at the intermission, labelled "REPLAY: <weapon> <label>". On wide screens it plays in the side panel next to the scoreboard, the chests and the cards. On 16:9 it plays full size first: after the chest jackpot and before the reward cards in DESCENT, before the scoreboard in the other modes. Jump or fire skips it.
  - How it works: cgame records every entity, light and client position of the main view 25 times a second (about 6 s are kept) and plays the recording back, interpolated, into a second 3D view. opengl2 renders that view without shadow maps, SSAO, bloom or a depth prepass (renderer flag `RDF_PIP`), and with the main view's exposure. Cost: about 1-2 ms per frame while a panel is up (tested at 3840x1080: ~110 fps without a panel, ~95 with one); recording costs nothing measurable. Dev: `cg_killCamDebug 1` (log lines), `2` (fps every 2 s), `3` (loop the replay at the intermission); server `kchurt <victim> <attacker> <damage> [mod]`.
- **Hit feel** (round 4, every mode, F10 > HITS). Enemies react on their bodies; the centre of the screen stays clean.
  - **Flinch** (`cg_hitFlinch`): every hit kicks the torso and head away from the shot's direction.
  - **Stagger** (`g_stagger`): rails, direct rockets, charged alt-fires, headshots and any 60+ hit stagger a bot for 0.4-0.9 s: it can't fire, its aim drifts and its body wobbles. Not again for 0.9 s after one ends (no stun lock). The Juggernaut only staggers from the biggest hits, half as long, and takes x0.35 knockback.
  - **Dismemberment** (`g_dismember`, needs `g_gore`): headshot kills and heavy kills to the neck / upper chest (rail, direct rocket, BFG, charged shots, 80+) pop the head; heavy hits on an arm tear it off (alive: 50% on a 50+ heavy hit; on the killing blow 70%); big blasts take the legs (alive 35% on 70+, otherwise 60% of blast kills lose a limb). A heavy headshot an armoured bot survives (30+ damage or a heavy weapon) knocks its helmet off. Limbs, heads and helmets are gibs that bounce and stay with the rest of the gore; stumps pump blood for a few seconds. Bodies keep their wounds. Models with separate arm surfaces (biker, doom, grunt, lucy, major, ranger, Team Arena's fritzkrieg) really lose the arm (generated arm-less skins); every other model shows a torn shoulder stump. Bosses, kamikazes and the Juggernaut keep their limbs while alive.
  - **Crawling wounded** (`g_crawl`): a bot hit down to 15 health or less drops to a crawl 30% of the time, a legless one always does: low, slow (forced crouch), pitched forward, leaving a blood trail; it stands up again only with its legs and 50+ health. A legless bot bleeds out: capped at 35 health, draining 2 a second down to 8. en-brains crawls it towards cover.
  - **Morale and panic** (`g_morale`): every hit costs morale (damage / 600, big hits +0.08), losing a limb -0.35, dropping to a crawl -0.3, a nearby ally gibbed / decapitated / dismembered in sight -0.25, losing a duel (taking 70+ while dealing under 30%) -0.2, armour shattering -0.1, a wall splat -0.2; it comes back after 2.5 s unhurt. Under 0.25 the bot panics for 2.5-4 s: a scream, the gun falls out of its hand for 1.2 s (no fire), then it runs from whoever broke it (en-brains); afterwards morale sits at 0.6 and it can't panic again for 7 s. The Juggernaut loses half as much; bosses and kamikazes never panic.
  - **Wall splats** (`g_wallSplat`, needs `g_corpsePhysics`): the existing splat (damage, a big blood splat that runs down the wall) now also staggers and shakes the bot and plays a body thud; direct rockets, rails and charged shots shove bots x1.25 harder, so splats happen more.
  - **Armour cracks** (`cg_armorCracks`): below 80% of the most armour it had this life, the body gets dark cracks with a hot pulsing rim, worse at 50% and 25%; hits on armour spark; the last of it shatters in a burst of plates, sparks and a flash.
  - **Death screams** (`cg_deathScreams`): generated screams (formant synthesis, no recorded voices) by how they died: a choked gurgle for headshots, a torn-off shriek for gibs, a long wavering scream for burning / lightning, a receding fall scream, a blown-out yell for blasts, and a plain one; four voices (low, higher, female, creature) picked from the model's gender and name, two takes each. Drowning keeps the stock gurgle.
  - Measured (6 skill-4 bots, q3dm6, 4 min at timescale 2, 43 kills): 60 staggers, 28 wall splats, 10 decapitations, 11 crawls, 9 panics, 10 armour breaks, 4 limbs lost.
  - Dev (server): `hftest <client> stagger|crawl|panic|larm|rarm|legs|helmet|armor [n]|decap`, `hftest stats`; every event logs a `HitFeel:` line in games.log. Assets: `python tools/make_sounds_hitfeel.py`, `python tools/make_hitfeel_assets.py` (crack textures, shader, arm-less skins), `blender --background --python tools/blender/make_hitfeel.py` (arm, leg, helmet gibs). Test cfgs: `tools/hitfeel_cfgs/` (copy to home_test/rearmed, run with `SHOT_CFG=hf_shot.cfg tools/shot.sh q3dm6`).
- **Screen effects** (`cg_screenFX` master switch): blood on your screen (`cg_bloodScreen`), a flash and thump on big kills (`cg_killFlash`), speed lines (`cg_speedLines`), and a heartbeat vignette at low health (`cg_lowHealthFX`).
- **Camera** (`cg_moveCamera`, `cg_speedFov`): landing dip, slide and wall-run lean, speed FOV and FOV punches on dash, slide, blink and slam.
- **Tips** (`cg_tips`): a one-line hint the first time each mechanic comes up. Set `cg_tipsSeen 0` to see them again.

## Killstreaks and powerups

| Streak | Title | Reward |
|---|---|---|
| 3 | RAMPAGE | 10s Haste |
| 5 | UNSTOPPABLE | 15s Vampire |
| 8 | GODLIKE | 15s Rapid Fire |
| 12 | LEGENDARY | 10s Quad |

Ending someone's streak is announced to everyone.

**Bounty** (`g_bounty`): a 5-kill streak puts a bounty on you. Everyone sees a gold marker over your head, and bots hunt you. Whoever ends the streak gets an extra point, full health and 50 armor.

**Medals** (`g_medals`): First Blood, Revenge, Triple/Quad/Rampage Kill, Shutdown, Comeback, Mid-Air, From the Grave, Stuck and Lunge Combo.

There are three new powerups (`g_rearmPowerups`):
- **Vampire:** the damage you deal heals you (red shell).
- **Rapid Fire:** double fire rate (orange shell).
- **Jetpack:** hold jump to fly.

With `g_rearmItems 1` they replace every second Regen, Invisibility and Haste spawn. rm_yard also has a jetpack on the lower deck.

## Bots

Each bot has a personality that shapes its weapons, movement and trash talk: Rusher, Sniper, Trickster, Brawler, Pyro, Stormcaller or Balanced. For example, Xaero and Anarki are Tricksters, Visor is a Sniper and Sarge is a Brawler. `bot_personality 0-6` forces one personality on every bot.

Bots now use the new moves:
- dodge-dashes and double jumps in fights
- flying the jetpack
- grappling toward enemies far above them
- alt-fire chosen by range and situation (slug at range, singularity, overcharged rail, LG chain arc on groups, stakes, lunges)
- remote-detonating their rockets and prox mines near you
- aiming for the head with hitscan weapons when skilled

They also slide, slide-jump, ground pound onto you, blink and triple jump with those perks, and shoot barrels and tanks you stand next to (`bot_useProps`). They combo elements (plasma soak, then lightning), yank you with Magnet Hook, and step out of meteor and lightning markers, fire and ground-pound landings (`bot_avoidDanger`). `bot_rearmTricks 0` turns the new tricks off.

In testing they killed as often as the stock bots. Set `bot_rearm 0` to go back to stock bot behavior.

### Smarter bots (round 4, every mode)

Bots play as a pack and think about the fight (F10 > BOTS). Bot skill scales all of it: skill 1 bots play like stock bots, skill 2 bots use the tactics a third of the time (and weakly), skill 3 two thirds of the time, skill 4-5 always and fully. In DESCENT the difficulty is the bot skill (I Can Win 1, Bring It On 2, Hurt Me Plenty 3, Hardcore 4, Nightmare 5 on floors 1-6, a little more each floor after), so the easy settings stay easy. Measured and tuned by en-tune: see the CHANGELOG (0.6.0) for the tables, rerun with `bash tools/tune_sweep.sh <tag>` and `python tools/tune_table.py <tag>`.
- **Flank and pincer:** when two or more bots go for the same target, the closest one holds and the others circle round over a real route to your side or your back (to the back of a raised shield). A chase splits the same way instead of everyone filing down one corridor.
- **Retreat to heal:** a bot that is losing (very low, or low and facing someone much healthier) breaks off toward the nearest health or armour that isn't past you, then comes back topped up.
- **Dodging:** they sidestep or hop your rockets, plasma and grenades after a human-ish reaction time (and sometimes miss the cue), and juke when your railgun is lined up on them. This sits on top of the dash dodges they already had.
- **Rocket and strafe jumps:** skilled bots rocket jump to items and ledges more often and rocket-jump after you onto a ledge 100-300 units up; on long straight runs they strafe-jump for speed.
- **Item timing:** they keep the clock on quad, battlesuit, red armour and mega health and turn up a few seconds before the respawn (two bots at most per item).
- **Hunting by sound:** gunfire, footsteps, jump pads, landings and pain cries carry (louder weapons further, less through walls). A bot with nobody in sight goes to look where it heard you, and a chasing bot follows your noise instead of knowing where you are.
- **Habits:** over a match (DESCENT: the whole run) they learn your favourite weapon, the range you fight at and the routes you take. Then they pick weapons that beat yours, keep out of your comfort range and pre-aim the doorway you usually come through.
- **The leader:** some bots, some of the time, go after whoever leads on frags (never more than half of them).
- **Grudges:** a bot that holds a grudge (`g_botGrudges`, en-personality) prefers its killer as a target and hunts it by sound.
- **Barrel traps:** a bot being chased backs off behind an explosive barrel and sets it off when you walk past it.
- **Morale:** panicking bots run away from whoever scared them and crawling ones drag themselves toward health (en-feel's hit feel states).
- **Weapon thieves:** when you die, nearby bots race for your dropped gun. The one that grabs it switches to it and uses it (DESCENT: at your weapon level, capped at its own level + 2; DESCENT drops a copy, your kit stays). The thief carries a faint gold glint at its gun hand and "STOLEN: <NAME> TOOK YOUR RAILGUN" slides in at the right edge. Kill it yourself for RECLAIMED: the gun drops back, plus 1-3 skulls and a gem spray by the weapon's level in DESCENT, or 80 style points elsewhere.

Switches: F10 > BOTS has `g_botTactics` (master), `g_botFlank`, `g_botDodge`, `g_botRetreat`, `g_botHabits`, `g_botWeaponThief`. Console-only: `g_botHearing`, `g_botItemTiming`, `g_botRocketJump` (rocket and strafe jumps), `g_botLeader`, `g_botLure` (all default 1). A DESCENT boss only uses the dodging.
Dev: `bt_debug 1` logs BtFlank / BtHeal / BtContest / BtHear / BtLure / BtRJump / BtPrize / GrudgePick / GrudgeHunt lines and a `BtStats:` count line every 60 s and at the match end (2 = every event, 3 = flank roles too); `btstats` / `btreset` (server), `btthief`; `bt_split 1|2` lets only even / odd client numbers use the tactics (A/B tests); `bt_habitsTarget` / `bt_thiefTarget <client>` make a bot stand in for the human; `bt_split 3` = client 0 plays stock (a stand-in player), `bt_bench 1` logs `BtBench: in|out <ms>` time-to-kill for client 0. Difficulty sweep: `bash tools/tune_sweep.sh <tag> "1 2 3 4 5" 24` (`python tools/bt_sim.py --standin`, `BT_PK3=<dir>` pins a pk3 snapshot) then `python tools/tune_table.py <tag>`. Bot-only test matches: `python tools/bt_sim.py --runs 4 --par 2 --split 1 --tag x` (hidden dedicated servers in dist_bt / home_bt).
Archetype hooks for en-types: `game/ai_tactics.h` (`BotTypeThink`, `BotTypeGoal`, `BotTypeChooseWeapon`, `BotTypeTactic`, `BotTypeRange`, `BotTypeInput`, implemented in `game/ai_types.c`).

Bots wear their own models from the moment they spawn. The server lists the models of the bots it is about to add (a DESCENT floor's bots, a skirmish's bots, single-player arena bots) and the game loads them during the loading screen. A bot added by hand later loads at once (a short hitch) instead of wearing a stand-in model until the scoreboard. Dev: `dsmodels` (client) prints who wears which model, `dslineup [dist]` (server) lines the bots up in front of you.

## Maps

`rm_yard`, `rm_camp` and `rm_lost` are remixes of q3dm17, q3dm6 and q3dm13. They have Team Arena weapons placed in the layout and new relighting (bounce light, deluxe maps). rm_yard also adds a stepping-stone route to the railgun that you can only cross with dash and double jump.

Open the console and type `devmap rm_yard`, or pick one in Skirmish. On stock maps, `g_rearmItems 1` swaps every second GL, plasma gun and shotgun spawn for a TA weapon (`0` turns this off, `2` swaps all of them).

## Settings

- Display mode (F10 > VIDEO): Borderless (`r_fullscreen 2`, the default: fills the screen without exclusive fullscreen, so alt-tab and pop-ups don't throw you out), Fullscreen (`1`) or Window (`0`). `r_minimizeOnFocusLoss 0` also keeps an exclusive fullscreen game from minimizing when another window grabs the focus.
- `autoexec.cfg`: resolution (7680x2160) and FOV. `cg_fov` is the 4:3 horizontal FOV; `cg_horPlus 1` widens it to fit 32:9.
- `rearmed_gfx.cfg`: graphics preset (HDR, bloom, SSAO, sun shadows, cubemaps, generated normal maps, 4x MSAA).
  - If the frame rate is low, try `r_ssao 0`, `r_ext_framebuffer_multisample 2` or `r_sunShadows 0`. Setting `r_dlightMode 1` looks nicer but costs a lot.
- Bloom: `r_bloom`, `r_bloomIntensity`, `r_bloomThreshold`, `r_bloomPasses`.
- Effects: `cg_fx 0/1` (sparks and shockwaves), `cg_screenShake 0..1`.
- Post effects: `r_rmSharpen 0..1`, `r_rmVignette`, `r_rmGrain`, `r_rmChroma`.
- Widescreen HUD: `cg_hudWide 0` keeps the HUD in the 4:3 box, `1` moves the corner items to the 16:9 area, `2` to the screen edges. The style rank and medals follow the right side, and the DESCENT block and perks follow the left.
- Enemy tags (F10 > HUD, `cg_enemyTags`): `0` (the default) draws nothing over enemies; elites and bosses are marked only by their orange or purple aura. `1` adds a small ELITE / BOSS label. `2` brings back the full tags (label, name and kit) and the award badges over players' heads. Aiming at someone always shows their name small above the crosshair, in one plain colour (`cg_drawCrosshairNames 0` hides it), with ELITE / BOSS in front of it in a descent. In a descent there is no "You fragged X" centre print and no floating "+1" over the body (the frag counter, kill chain and kill cam show it).
- Right-edge callouts (nemesis, revenge / veterans, STOLEN / RECLAIMED, ambushes and events, champion packs, SECRET FOUND, evolution ready) share one stack under the style rank (`cgame/cg_edge.c`, `CG_EdgeSlot`), so they never overlap. Screen checks at 7680x2160: `tools/screens_test.sh fight.cfg ds` / `menu.cfg menu` / `bestiary.cfg menu` / `dailymap.cfg menu` (cfgs from `tools/screens_fixtures/gen.py`, crops with `crops.py`).
- The middle of the screen is kept clear:
  - Medals (FIRST BLOOD, TRIPLE KILL and so on) appear above the style rank on the right.
  - Streak and bounty banners are smaller and shorter, at the top.
  - A new event shows its name only on the event plate, and its hint line goes away after 6 s.
  - Tips appear low in the view instead of under the crosshair.
  - "You fragged" is smaller and higher.
  - AIRSHOT! is smaller, and speed lines are fainter.
  - Hitmarkers, HEADSHOT, damage arrows, kill flash and gore are unchanged.
- Server switches (all default on): `g_doubleJump`, `g_wallJump`, `g_wallRun`, `g_airDash`, `g_jumpAssist`, `g_offhandHook`, `g_altFire`, `g_shotgunKick`, `g_chaingunSpin`, `g_ricochet`, `g_stickyGrenades`, `g_singularity`, `g_bfgLasers`, `g_uppercut`, `g_headshots`, `g_airshots`, `g_killstreaks`, `g_bounty`, `g_medals`, `g_rearmPowerups`, `g_zipLine`. Also `g_rearmItems 0/1/2`.

## Building

```bash
source tools/env.sh && tools/build.sh      # engine + QVMs + pk3s (portable GCC/CMake in G:\_Games\_toolchain)
python tools/remix.py rm_yard              # rebuild a map (maps/remixes.py defines the remixes)
tools/shot.sh rm_yard 1920 540             # automated screenshot test, runs home/rearmed/shot.cfg
tools/bench.sh 1920 540                    # timedemo benchmark
python tools/package.py                    # shareable release zips in release/ (no id data)
```

`build.sh` always deploys to the test install (`dist_test/`, `home_test/`). It only deploys to your play install when the game isn't running, unless you set `DEPLOY=1`.

Dev console commands (server): `findwall` lists long walls for wall-run tests. `where` prints each player's position, score, gun game level, streak and bounty. `g_debugMove 2` logs why a wall run didn't start, and every zip line attach, miss (with its reason), step-over, arrival and knock-off. In a descent, `rlgive <card>` gives you a card for this floor and `rlwin [card card card]` wins the floor (optionally dealing that offer).

## Sharing

`python tools/package.py` builds `release/REARMED-<version>-win64.zip` and a `-src.zip` with the GPL source. The release contains no id Software data:
- On first start, `setup.ps1` finds the player's Quake III install and copies the Team Arena weapon files out of their own `missionpack/pak0.pk3`. It uses `ta_manifest.txt`, which lists the files by name only.
- The remixed maps are left out, because they're built from id's maps.
- The packager fails if the mod pk3 would contain any path that's also in the id paks, except the GPL-built game code.

See `CHANGELOG.md`.

## Promo clips

`promo/clips/` holds the landing-page clips (8 MP4s + poster JPEGs, `manifest.txt` with sizes and captions). To re-record one: `bash tools/clip_rec.sh <cfg> <map|-> [engine args]` runs `tools/clip_cfgs/<cfg>.cfg` in its own test install (`dist_clips/` + `home_clips/`, never `home/` or `home_test/`) in a small window and records a demo; `bash tools/clip_play.sh W H <demo>...` plays the demos back with the `video` command (30 fps, paced to real time so the kill cam runs at its real speed) into `home_clips/rearmed/videos/`; `tools/clip_concat.sh` cuts montages, `tools/clip_encode.sh` encodes the H.264 clip and poster, `tools/clip_sheet.sh` makes contact sheets for checking frames. DESCENT clips copy a run fixture from `tools/dsintro_fixtures/` to `home_clips/rearmed/rearmed_run.txt` first. The AVIs are large (about 1.4 GB for all of them): delete them after encoding.

## Known issues

- The dedicated server (`ioq3ded.exe`) can crash on Windows when it has no console window (upstream ioquake3 bug). Normal play isn't affected.

- The source changes are in `ioq3/` (uncommitted, on top of upstream ioquake3). `tools/patches/` has the scripts that applied them.
- `zz-rearmed-ta.pk3` and `zz-rearmed-maps.pk3` contain content extracted from your own Quake 3 / Team Arena install. They're for personal use only; don't redistribute them.
- The announcer voice lines from `tools/make_voices.py` (style ranks, records, events, the DESCENT intro's boss / elite / nemesis / rivals lines in `sound/rearmed/dsintro`) are Windows' built-in speech voice (Microsoft David), pitched and processed. Microsoft's terms for that voice output don't clearly allow redistribution: treat them as personal use and re-record them or swap the voice before sharing a release. The bots' names in the intro aren't shipped; they play from your own install's `sound/player/announce/*.wav`.
