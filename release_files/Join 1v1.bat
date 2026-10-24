@echo off
rem REARMED 1v1: joins a friend's duel server. Run it and type the host's address.
setlocal
set R=%~dp0
if not exist "%R%q3path.txt" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%R%setup.ps1"
  if errorlevel 1 ( pause & exit /b 1 )
)
set /p Q3BASE=<"%R%q3path.txt"
set ADDR=%1
if "%ADDR%"=="" set /p ADDR=Host address (IP or Tailscale name, e.g. 100.64.1.2):
set EXTRA=
if not exist "%R%home\rearmed\.firstrun" (
  set EXTRA=+exec rearmed_firstrun.cfg
  if not exist "%R%home\rearmed" mkdir "%R%home\rearmed"
  echo done> "%R%home\rearmed\.firstrun"
)
cd /d "%R%engine"
start "" ioquake3.exe +set fs_basepath "%Q3BASE%" +set fs_homepath "%R%home" +set fs_game rearmed +set cl_renderer opengl2 +set ui_cdkeychecked 1 +set com_introplayed 1 +set com_hunkMegs 256 %EXTRA% +connect %ADDR%:27960
