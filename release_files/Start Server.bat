@echo off
rem REARMED dedicated server. Edit server.cfg for the mode, mutators, bots and maps.
rem Players connect with /connect <your ip>:27960. Open UDP 27960 on your router for internet games.
setlocal
set R=%~dp0
if not exist "%R%q3path.txt" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%R%setup.ps1"
  if errorlevel 1 ( pause & exit /b 1 )
)
set /p Q3BASE=<"%R%q3path.txt"
cd /d "%R%engine"
ioq3ded.exe +set fs_basepath "%Q3BASE%" +set fs_homepath "%R%home" +set fs_game rearmed +set dedicated 2 +set net_port 27960 +exec server.cfg %*
