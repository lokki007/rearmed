@echo off
rem REARMED - an arena mod for Quake III Arena. Needs your own copy of the game (Steam or GOG).
setlocal
set R=%~dp0
if not exist "%R%q3path.txt" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%R%setup.ps1"
  if errorlevel 1 ( pause & exit /b 1 )
)
set /p Q3BASE=<"%R%q3path.txt"
set EXTRA=
if not exist "%R%home\rearmed\.firstrun" (
  set EXTRA=+exec rearmed_firstrun.cfg
  if not exist "%R%home\rearmed" mkdir "%R%home\rearmed"
  echo done> "%R%home\rearmed\.firstrun"
)
cd /d "%R%engine"
start "" ioquake3.exe +set fs_basepath "%Q3BASE%" +set fs_homepath "%R%home" +set fs_game rearmed +set cl_renderer opengl2 +set ui_cdkeychecked 1 +set com_introplayed 1 +set com_hunkMegs 256 +set logfile 2 %EXTRA% %*
