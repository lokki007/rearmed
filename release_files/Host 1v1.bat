@echo off
rem REARMED 1v1: starts a duel server in the background and joins it.
rem The other player connects with "Join 1v1.bat" and your address (see README.txt, PLAYING 1v1).
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
start "REARMED 1v1 server" /min ioq3ded.exe +set fs_basepath "%Q3BASE%" +set fs_homepath "%R%home" +set fs_game rearmed +set dedicated 2 +set net_port 27960 +exec server_duel.cfg
timeout /t 4 /nobreak >nul
start "" ioquake3.exe +set fs_basepath "%Q3BASE%" +set fs_homepath "%R%home" +set fs_game rearmed +set cl_renderer opengl2 +set ui_cdkeychecked 1 +set com_introplayed 1 +set com_hunkMegs 256 %EXTRA% +set net_port 27961 +connect localhost:27960
