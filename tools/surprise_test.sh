#!/bin/bash
# ds-surprise test: surprise_test.sh <cfg> <seed> W H [map] [extra engine args]
# Plays floor 2 of tools/surprise_fixtures/run_floor2.txt (seed replaced) on <map> (default q3dm6) in
# home_test, with the fixture ghost track (tools/surprise_fixtures/ghost_past.txt: a past run's
# floors 2-3). The test home's run / ghost files are backed up and put back. Screenshots and
# games.log -> $OUT (default home_test/surprise_<cfg>_<W>).
source "$(dirname "$0")/env.sh"
q3lock
CFG=$1; SEED=$2; W=$3; H=$4; MAP=${5:-q3dm6}; shift 4; [ $# -gt 0 ] && shift
FX="$Q3MOD/tools/surprise_fixtures"; HR="$Q3MOD/home_test/rearmed"
OUT=${OUT:-$Q3MOD/home_test/surprise_${CFG%.cfg}_${W}}
sed -e "s/%ARCH%/${ARCH:-0}/" "$FX/$CFG" > "$HR/$CFG"
GLOG=$(grep -a '^seta g_log ' "$HR/q3config.cfg" 2>/dev/null | cut -d'"' -f2); GLOG=${GLOG:-games.log}
SZ=$(stat -c %s "$HR/$GLOG" 2>/dev/null || echo 0)
for f in rearmed_run.txt rearmed_ghost.txt; do [ -f "$HR/$f" ] && cp "$HR/$f" "$HR/$f.surbak"; done
sed -e "s/^seed SEED/seed $SEED/" -e "s/^node_mod NODEMOD/node_mod ${NODEMOD:-}/" "$FX/run_floor2.txt" > "$HR/rearmed_run.txt"
cp "$FX/${GHOST:-ghost_past.txt}" "$HR/rearmed_ghost.txt"
Q3NOLOCK=1 SHOT_CFG=$CFG SHOT_OUT="$OUT" SHOT_LOG="$OUT.log" SHOT_TIMEOUT=${SHOT_TIMEOUT:-170} bash "$Q3MOD/tools/shot.sh" $MAP $W $H \
  +set g_gametype 0 +set g_rearmMode 5 +set bot_minplayers 0 +set sv_maxclients 16 +set g_dsAmbush 1 +set g_dsModifiers 1 \
  +set g_dsWildEvents 1 +set g_dsGhosts 1 +set g_dsNemesis 0 +set cg_dsIntro 0 "$@"
cp "$HR/rearmed_ghost.txt" "$OUT/rearmed_ghost_after.txt" 2>/dev/null
cp "$HR/rearmed_run.txt" "$OUT/rearmed_run_after.txt" 2>/dev/null
for f in rearmed_run.txt rearmed_ghost.txt; do rm -f "$HR/$f"; [ -f "$HR/$f.surbak" ] && mv "$HR/$f.surbak" "$HR/$f"; done
tail -c +$((SZ+1)) "$HR/$GLOG" > "$OUT/new.log" 2>/dev/null
grep -a -E "DSSurprise|DSGhost|DSEvent|Descent:|DSEcon|Loot: chest|extra bot|Kill:" "$OUT/new.log" 2>/dev/null | cut -c1-300
