@echo off
rem REARMED launcher. Game data comes from the Steam install (read only).
setlocal
set Q3MOD=%~dp0
set Q3BASE=H:\SteamLibrary\steamapps\common\Quake 3 Arena
set EXTRA=
if not exist "%Q3MOD%home\rearmed\.binds_applied" (
  set EXTRA=+exec rearmed_binds.cfg
  echo applied> "%Q3MOD%home\rearmed\.binds_applied"
  echo applied> "%Q3MOD%home\rearmed\.binds_fix1"
  echo applied> "%Q3MOD%home\rearmed\.binds_fix2"
)
rem one-time fix for the first binds: C is crouch again, chaingun on V, F10 opens the settings
if not exist "%Q3MOD%home\rearmed\.binds_fix1" (
  set EXTRA=+exec rearmed_binds_fix1.cfg
  echo applied> "%Q3MOD%home\rearmed\.binds_fix1"
)
rem one-time: R opens the weapon wheel (hold + mouse to pick, tap for the last weapon)
if not exist "%Q3MOD%home\rearmed\.binds_fix2" (
  set EXTRA=%EXTRA% +exec rearmed_binds_fix2.cfg
  echo applied> "%Q3MOD%home\rearmed\.binds_fix2"
)
rem one-time: Z fires the zip line
if not exist "%Q3MOD%home\rearmed\.binds_fix3" (
  set EXTRA=%EXTRA% +exec rearmed_binds_fix3.cfg
  echo applied> "%Q3MOD%home\rearmed\.binds_fix3"
)
rem one-time: borderless fullscreen (F10 > VIDEO > Display mode to change it)
if not exist "%Q3MOD%home\rearmed\.display_fix4" (
  set EXTRA=%EXTRA% +exec rearmed_display_fix4.cfg
  echo applied> "%Q3MOD%home\rearmed\.display_fix4"
)
cd /d "%Q3MOD%dist"
start "" ioquake3.exe +set fs_basepath "%Q3BASE%" +set fs_homepath "%Q3MOD%home" +set fs_game rearmed +set cl_renderer opengl2 +set ui_cdkeychecked 1 +set com_introplayed 1 +set com_hunkMegs 256 %EXTRA% %*
