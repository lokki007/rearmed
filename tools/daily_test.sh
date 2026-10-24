#!/bin/bash
# en-daily tests in an isolated test home (home_test/daily): daily_test.sh <cfg> [W H]
# Copies the built pk3s + autoexec + tools/daily_cfgs/*.cfg into home_test/daily/rearmed, seeds a copy
# of the player's MAIN run/meta (read only) once, runs the cfg, and copies screenshots,
# qconsole.log and games.log to home_test/daily/out/<cfg>/. RESET=1 clears the daily files first.
# Never touches home/rearmed.
source "$(dirname "$0")/env.sh"
[ -z "$Q3NOLOCK" ] && q3lock
CFG=$1; W=${2:-3840}; H=${3:-1080}
DH="$Q3MOD/home_test/daily"; DR="$DH/rearmed"; OUT="$DH/out/${CFG%.cfg}"
mkdir -p "$DR" "$OUT"
cp "$Q3MOD"/build_pk3/*.pk3 "$DR/"
cp "$Q3MOD"/home_test/rearmed/autoexec.cfg "$DR/" 2>/dev/null
cp "$Q3MOD"/tools/daily_cfgs/*.cfg "$DR/"
[ -f "$DR/rearmed_run.txt" ] || cp "$Q3MOD/home/rearmed/rearmed_run.txt" "$DR/rearmed_run.txt"	# (a read-only copy of the player's descent)
[ -f "$DR/rearmed_meta.txt" ] || cp "$Q3MOD/home/rearmed/rearmed_meta.txt" "$DR/rearmed_meta.txt"
[ "$RESET" = 1 ] && rm -f "$DR/rearmed_daily.txt" "$DR/rearmed_daily_run.txt"
rm -f "$DR"/screenshots/*.jpg "$DR/ioq3.pid" "$DR/games.log" "$DR/qconsole.log" "$OUT"/*
Q3DIST="$Q3MOD/dist_test" Q3HOME="$DH" timeout ${SHOT_TIMEOUT:-240} "$Q3MOD/tools/run.sh" +set r_fullscreen 0 +set r_mode -1 \
  +set r_customwidth $W +set r_customheight $H +set g_log games.log +set g_logsync 1 +set logfile 2 $EXTRA +exec $CFG > "$OUT/run.log" 2>&1
cp "$DR"/screenshots/*.jpg "$OUT/" 2>/dev/null
cp "$DR/games.log" "$DR/qconsole.log" "$OUT/" 2>/dev/null
cp "$DR/rearmed_daily.txt" "$DR/rearmed_daily_run.txt" "$OUT/" 2>/dev/null
ls "$OUT"
