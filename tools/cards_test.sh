#!/bin/bash
# ds-cards tests in an isolated test home: cards_test.sh <cfg> [devmap <map> | menu] [W H]
# HOME_NAME (default cards) picks home_test/<name>; PK3DIR (default build_pk3) the pk3s to test
# (PK3DIR=home/rearmed runs the player's installed build as a baseline: the pk3s are COPIED,
# nothing in home/rearmed is written). Outputs land in home_test/<name>/out/<cfg>/.
source "$(dirname "$0")/env.sh"
[ -z "$Q3NOLOCK" ] && q3lock
CFG=$1; MODE=${2:-menu}; MAP=${3:-q3dm6}; W=${4:-1600}; H=${5:-450}
EH="$Q3MOD/home_test/${HOME_NAME:-cards}"; ER="$EH/rearmed"; OUT="$EH/out/${CFG%.cfg}"
PK3="$Q3MOD/${PK3DIR:-build_pk3}"
mkdir -p "$ER" "$OUT"
rm -f "$ER"/zz-rearmed-*.pk3
cp "$PK3"/zz-rearmed-*.pk3 "$ER/"
cp "$Q3MOD"/home_test/rearmed/autoexec.cfg "$ER/" 2>/dev/null
cp "$Q3MOD"/tools/cards_cfgs/*.cfg "$ER/"
rm -f "$ER"/screenshots/*.jpg "$ER/ioq3.pid" "$ER/games.log" "$ER/qconsole.log" "$OUT"/*
ARGS=""
[ "$MODE" = devmap ] && ARGS="+devmap $MAP"
Q3DIST="$Q3MOD/dist_test" Q3HOME="$EH" timeout ${SHOT_TIMEOUT:-180} "$Q3MOD/tools/run.sh" +set r_fullscreen 0 +set r_mode -1 \
  +set r_customwidth $W +set r_customheight $H +set g_log games.log +set g_logsync 1 +set logfile 2 $EXTRA $ARGS +exec $CFG > "$OUT/run.log" 2>&1
cp "$ER"/screenshots/*.jpg "$OUT/" 2>/dev/null
cp "$ER/games.log" "$OUT/" 2>/dev/null
cp "$ER/qconsole.log" "$OUT/" 2>/dev/null
ls "$OUT"
