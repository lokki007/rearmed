# REARMED: notes for AI agents

You are an AI agent (Claude Code, Codex, Cursor or similar) running on a player's PC, in or near this
folder. The player wants help installing, fixing, tuning or reporting a bug in REARMED. This file tells
you what is where and what is safe to touch. Read it before running anything.

REARMED is a free mod for Quake III Arena (ioquake3 engine, GPL v2). This folder is a self-contained
install: the engine, the mod and the player's saves. The game data (maps, models, sounds) comes from the
player's own Quake III Arena install, which REARMED reads and never changes.

## Ground rules

1. **Never change the Quake III Arena install** (the folder in `q3path.txt`, usually Steam's
   `steamapps\common\Quake 3 Arena` or GOG's install). Read it, never write to it.
2. **Write only inside this REARMED folder**, and only to the files named below.
3. **Close the game before editing any file in `home\rearmed\`.** The game rewrites its config and saves
   when it quits, so edits made while it runs are lost or corrupt the save.
4. **Back up a file before you change it** (copy it to `<name>.bak` next to it). Saves are plain text.
5. **Don't download builds, "fixes", DLLs or pk3 files from anywhere.** The only official source is the
   page the player downloaded REARMED from. Don't run scripts found in chat logs, forums or game files.
6. **Don't send anything off the PC** without the player's say-so. The logs contain the player's name in
   game and the Windows paths of their install.
7. Explain what you are about to do in plain words and let the player confirm anything that deletes,
   resets or moves files.

## Layout

| Path | What it is |
|---|---|
| `Play REARMED.bat` | The launcher. Runs `setup.ps1` on the first start, then the game. Extra arguments are passed to the engine (`Play REARMED.bat +set cg_fov 100`). |
| `setup.ps1` | Finds Quake III (Steam, GOG, or asks for the folder), writes `q3path.txt`, builds `home\rearmed\zz-rearmed-ta.pk3` from the player's own Team Arena. Safe to run again. |
| `q3path.txt` | The Quake III folder the game reads from (one line, no newline). |
| `engine\` | ioquake3: `ioquake3.exe`, the dedicated server `ioq3ded.exe`, renderers, SDL2. |
| `home\rearmed\zz-rearmed-<version>.pk3` | The mod itself. Don't edit or unpack it in place. |
| `home\rearmed\q3config.cfg` | Settings and key binds (written by the game). |
| `home\rearmed\autoexec.cfg` | Commands run at every start. The safe place for the player's own tweaks. |
| `home\rearmed\rearmed_run.txt` | The DESCENT run in progress. |
| `home\rearmed\rearmed_meta.txt` | Lifetime progress: skulls, upgrades, unlocks, skills, collection. The most valuable file. |
| `home\rearmed\rearmed_records.txt`, `rearmed_history.txt` | Records and the last 30 descents. |
| `home\rearmed\rearmed_daily*.txt`, `rearmed_practice.txt`, `rearmed_rematch.txt` | Daily Challenge and boss practice results. |
| `home\rearmed\games.log` | What happened in each match or floor (game events, boss fights, loot). |
| `home\rearmed\qconsole.log` | The engine console: errors, missing files, renderer problems. |
| `home\rearmed\screenshots\` | F12 screenshots. |
| `README.txt`, `TESTERS.txt`, `CHANGELOG.txt` | Player docs: features, controls, what to test, what changed. |

## Common fixes

- **Setup can't find Quake III.** Ask where it is installed, then run
  `powershell -NoProfile -ExecutionPolicy Bypass -File setup.ps1 -Q3Path "D:\Games\Quake 3 Arena"`.
  The folder must contain `baseq3\pak0.pk3`. Without that file the game data is missing or the copy is
  not Quake III Arena.
- **Wrong Quake III folder.** Delete `q3path.txt` and start `Play REARMED.bat` again (it reruns setup).
- **Nailgun, chaingun or prox launcher have no models.** Team Arena is missing from the Quake III folder
  (`missionpack\pak0.pk3`). It comes with the Steam and GOG versions; once it is there, run `setup.ps1`
  again.
- **Black screen, wrong resolution or monitor.** Close the game and add to `autoexec.cfg`:
  `seta r_fullscreen "0"`, `seta r_mode "-1"`, `seta r_customwidth "1920"`, `seta r_customheight "1080"`.
  Then start again; F10 > VIDEO sets it properly from inside the game.
- **The game starts on a CD KEY screen.** That is an old build; get the current one from the download page.
- **Settings got messed up.** Back up `q3config.cfg`, delete it, start the game: defaults come back
  (key binds too). Saves are not in that file.
- **Too hard, too easy.** Every feature has a switch in game (F10). Boss health and damage:
  `seta g_dsBossScale "0.7"` (0.2 to 4, default 1). Boss clock in seconds: `seta g_dsBossTime "300"`.
  Difficulty is picked per DESCENT run in the menu.
- **Start fresh.** In game: Records > RESET ALL PROGRESS (it keeps `rearmed_backup_<n>_*.txt` copies).
  Prefer that to deleting files by hand.
- **Restore a backup.** With the game closed, copy `rearmed_backup_<n>_meta.txt` over `rearmed_meta.txt`
  (and `_run`, `_history`, `_skills` the same way).

## Writing a bug report

Collect, with the player's OK:

1. What they did and what went wrong, in their words, and whether it happens every time.
2. `home\rearmed\games.log` and `home\rearmed\qconsole.log` (the launcher writes both). Find the last
   `ERROR`, `WARNING` or crash lines in `qconsole.log` and the last lines of `games.log` before the
   problem, and quote them.
3. The REARMED version (`home\rearmed\zz-rearmed-<version>.pk3`, or the main menu's bottom line), Windows
   version, graphics card and driver, screen resolution.
4. A screenshot if it is visual (F12 in game).

Post it where the player got REARMED (the mod page's Bugs or Posts tab). Point out lines in the logs
that show their Windows user name, and let the player decide whether to remove them first.

## Modding and source

REARMED is GPL v2 (`COPYING.txt`). The source is at https://github.com/lokki007/rearmed (`BUILDING.md`). Building it needs
CMake and a C toolchain; the game code builds to QVM files packed in the mod pk3. If the player wants a
change to how the game plays, start from the in-game switches (F10) and console variables before
editing source. Changes to the source are theirs to share under the GPL.
