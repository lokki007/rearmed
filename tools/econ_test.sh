#!/bin/bash
# ds-economy tests in an isolated test home (home_test/econ): econ_test.sh <cfg> [devmap <map> | menu] [W H]
# copies the freshly built pk3s + autoexec into home_test/econ/rearmed, runs the cfg, copies
# screenshots + logs to home_test/econ/out/<cfg>/. Never touches home/rearmed.
source "$(dirname "$0")/env.sh"
[ -z "$Q3NOLOCK" ] && q3lock
CFG=$1; MODE=${2:-menu}; MAP=${3:-q3dm17}; W=${4:-1920}; H=${5:-540}
EH="$Q3MOD/home_test/econ"; ER="$EH/rearmed"; OUT="$EH/out/${CFG%.cfg}"
mkdir -p "$ER" "$OUT"
cp "$Q3MOD"/build_pk3/*.pk3 "$ER/"
cp "$Q3MOD"/home_test/rearmed/autoexec.cfg "$ER/" 2>/dev/null
cp "$Q3MOD"/tools/econ_cfgs/*.cfg "$ER/"
rm -f "$ER"/screenshots/*.jpg "$ER/ioq3.pid" "$ER/games.log" "$OUT"/*
ARGS=""
[ "$MODE" = devmap ] && ARGS="+devmap $MAP"
[ "$MODE" = spdevmap ] && ARGS="+set ui_singlePlayerActive 1 +spdevmap $MAP"
Q3DIST="$Q3MOD/dist_test" Q3HOME="$EH" timeout ${SHOT_TIMEOUT:-180} "$Q3MOD/tools/run.sh" +set r_fullscreen 0 +set r_mode -1 \
  +set r_customwidth $W +set r_customheight $H +set g_log games.log +set g_logsync 1 +set logfile 2 $EXTRA $ARGS +exec $CFG > "$OUT/run.log" 2>&1
cp "$ER"/screenshots/*.jpg "$OUT/" 2>/dev/null
cp "$ER/games.log" "$OUT/" 2>/dev/null
cp "$ER/qconsole.log" "$OUT/" 2>/dev/null
ls "$OUT"
