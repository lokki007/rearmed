#!/bin/bash
# en-personality tests (round 4) in an isolated test home: pers_test.sh <cfg> <ffa|ds> [map] [W H]
# ffa: a plain free-for-all (devmap), ds: a DESCENT floor from $RUN (default tools/pers_fixtures/run_base.txt;
# the run file after the floor is copied to $OUT/rearmed_run_after.txt). HOME_NAME (default pers) picks
# home_test/<name>; never touches home/rearmed. Every archived switch is passed explicitly ($EXTRA adds more).
# Outputs: home_test/<name>/out/<cfg>/ (games.log, qconsole.log, screenshots, run files).
source "$(dirname "$0")/env.sh"
[ -z "$Q3NOLOCK" ] && q3lock
CFG=$1; MODE=${2:-ffa}; MAP=${3:-q3dm17}; W=${4:-1600}; H=${5:-450}
EH="$Q3MOD/home_test/${HOME_NAME:-pers}"; ER="$EH/rearmed"; OUT="$EH/out/${OUTNAME:-${CFG%.cfg}}"
FX="$Q3MOD/tools/pers_fixtures"
mkdir -p "$ER" "$OUT"
rm -f "$ER"/zz-rearmed-*.pk3
cp "$Q3MOD"/${PK3DIR:-build_pk3}/zz-rearmed-*.pk3 "$ER/"
cp "$Q3MOD"/home_test/rearmed/autoexec.cfg "$ER/" 2>/dev/null
cp "$FX"/*.cfg "$ER/"
rm -f "$ER"/screenshots/*.jpg "$ER/ioq3.pid" "$ER/games.log" "$ER/qconsole.log" "$OUT"/*
ARGS="+set g_gametype 0 +set g_rearmMode 0"
if [ "$MODE" = ds ]; then
  cp "${RUN:-$FX/run_base.txt}" "$ER/rearmed_run.txt"
  cp "$ER/rearmed_run.txt" "$OUT/rearmed_run_before.txt"
  ARGS="+set g_gametype 0 +set g_rearmMode 5 +set g_dsStagger 0 +set g_dsAmbush 0 +set g_dsWildEvents 0 +set g_dsGhosts 0 +set g_dsModifiers 0 +set cg_dsIntro 1 +set g_dsNemesis 1"
else
  rm -f "$ER/rearmed_run.txt"
fi
# the switches go through a cfg (ioq3 truncates its command line at 1024 chars)
cat > "$ER/pers_set.cfg" <<EOC
set g_log games.log
set g_logsync 1
set logfile 2
set bot_minplayers 0
set sv_maxclients 12
set fraglimit 0
set timelimit 0
set g_forcerespawn 2
set bt_debug 1
set g_botTaunts ${TAUNTS:-1}
set g_botGrudges ${GRUDGES:-1}
set g_botShouts 1
set g_dsNemesisLevels 1
set g_dsVeterans 1
set g_mutators 0
set bot_nochat 0
set cg_killCam 0
set cg_deathCam 0
set cg_replays 0
EOC
set -- $ARGS
while [ $# -ge 3 ]; do echo "set $2 $3" >> "$ER/pers_set.cfg"; shift 3; done
Q3DIST="$Q3MOD/dist_test" Q3HOME="$EH" timeout ${SHOT_TIMEOUT:-240} "$Q3MOD/tools/run.sh" +set r_fullscreen 0 +set r_mode -1 +set r_customwidth $W +set r_customheight $H +exec pers_set.cfg $EXTRA +devmap $MAP +exec $CFG > "$OUT/run.log" 2>&1
cp "$ER"/screenshots/*.jpg "$OUT/" 2>/dev/null
cp "$ER/games.log" "$ER/qconsole.log" "$OUT/" 2>/dev/null
[ "$MODE" = ds ] && cp "$ER/rearmed_run.txt" "$OUT/rearmed_run_after.txt"
ls "$OUT"
