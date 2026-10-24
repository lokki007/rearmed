#!/bin/bash
# ds-surprise floor map test: surprise_ui_test.sh <cfg> <seed> W H
# Starts at the main menu with tools/surprise_fixtures/run_floor2.txt (floor 1 cleared, seed replaced) in
# home_test, runs the cfg (open the map, take a modifier, launch, win). Run / ghost files backed up and put back.
source "$(dirname "$0")/env.sh"
q3lock
CFG=$1; SEED=$2; W=$3; H=$4
FX="$Q3MOD/tools/surprise_fixtures"; HR="$Q3MOD/home_test/rearmed"
OUT=${OUT:-$Q3MOD/home_test/surprise_${CFG%.cfg}_${W}}
GLOG=$(grep -a '^seta g_log ' "$HR/q3config.cfg" 2>/dev/null | cut -d'"' -f2); GLOG=${GLOG:-games.log}
SZ=$(stat -c %s "$HR/$GLOG" 2>/dev/null || echo 0)
cp "$FX/$CFG" "$HR/$CFG"
for f in rearmed_run.txt rearmed_ghost.txt; do [ -f "$HR/$f" ] && cp "$HR/$f" "$HR/$f.surbak"; done
sed -e "s/^seed SEED/seed $SEED/" -e "s/^node_mod NODEMOD/node_mod/" -e "s/^infloor 1/infloor 0/" -e "s/^floor 1$/floor 2/" -e "s/^map .*/map 0.0/" "$FX/run_floor2.txt" > "$HR/rearmed_run.txt"
cp "$FX/ghost_past.txt" "$HR/rearmed_ghost.txt"
SS="$HR/screenshots"; rm -f "$SS"/*.jpg "$HR/ioq3.pid"
Q3DIST="$Q3MOD/dist_test" Q3HOME="$Q3MOD/home_test" timeout ${SHOT_TIMEOUT:-150} "$Q3MOD/tools/run.sh" +set r_fullscreen 0 +set r_mode -1 \
  +set r_customwidth $W +set r_customheight $H +set sv_maxclients 16 +set g_dsModifiers 1 +set g_dsAmbush 0 +set g_dsWildEvents 0 \
  +set g_dsGhosts 0 +set g_dsNemesis 0 +set cg_dsIntro 0 +set bot_minplayers 0 +exec $CFG > "$OUT.log" 2>&1
mkdir -p "$OUT"; rm -f "$OUT"/*.jpg; cp "$SS"/*.jpg "$OUT"/ 2>/dev/null; ls "$OUT"/*.jpg
cp "$HR/rearmed_run.txt" "$OUT/rearmed_run_after.txt" 2>/dev/null
for f in rearmed_run.txt rearmed_ghost.txt; do rm -f "$HR/$f"; [ -f "$HR/$f.surbak" ] && mv "$HR/$f.surbak" "$HR/$f"; done
tail -c +$((SZ+1)) "$HR/$GLOG" > "$OUT/new.log" 2>/dev/null
grep -a -E "DESCENT|dsmod|modifier|floor [ 0-9]+:" "$OUT.log" | cut -c1-200
grep -a -E "DSSurprise|DSEcon|Descent:|Loot: chest" "$OUT/new.log" | cut -c1-300
