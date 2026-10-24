#!/bin/bash
# ds-art: prop screenshots in an isolated install (dist_art + home_art), never touches home_test.
# art_shot.sh <map> <cfg> <outdir> [W H]
source "$(dirname "$0")/env.sh"
MAP=$1; CFG=$2; OUT=$3; W=${4:-1600}; H=${5:-900}
SS="$Q3MOD/home_art/rearmed/screenshots"; rm -f "$SS"/*.jpg "$Q3MOD/home_art/rearmed/ioq3.pid"
Q3DIST="$Q3MOD/dist_art" Q3HOME="$Q3MOD/home_art" timeout ${SHOT_TIMEOUT:-150} "$Q3MOD/tools/run.sh" +set r_fullscreen 0 +set r_mode -1 \
  +set r_customwidth $W +set r_customheight $H +set cg_horPlus 0 +set g_props 1 +set g_propCount 0 +set bot_minplayers 0 \
  +devmap $MAP +exec $CFG > "$Q3MOD/home_art/run.log" 2>&1
mkdir -p "$OUT"; cp "$SS"/*.jpg "$OUT"/ 2>/dev/null; ls "$OUT"/*.jpg
