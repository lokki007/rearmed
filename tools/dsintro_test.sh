#!/bin/bash
# ds-intro test: dsintro_test.sh <normal|elite|boss|skirmish> W H [extra engine args]
# Copies tools/dsintro_fixtures/run_<kind>.txt into home_test (backs the old run file up and puts it back),
# runs tools/dsintro_fixtures/dsi_shot.cfg, screenshots -> scratch dir $OUT (default home_test/dsintro_<kind>_<W>)
source "$(dirname "$0")/env.sh"
q3lock
KIND=$1; W=$2; H=$3; shift 3
FX="$Q3MOD/tools/dsintro_fixtures"; HR="$Q3MOD/home_test/rearmed"
OUT=${OUT:-$Q3MOD/home_test/dsintro_${KIND}_${W}}
CFG=${CFG:-dsi_shot.cfg}; [ "$KIND" = skirmish ] && CFG=dsi_skirmish.cfg; cp "$FX/$CFG" "$HR/$CFG"
[ -f "$HR/rearmed_run.txt" ] && cp "$HR/rearmed_run.txt" "$HR/rearmed_run.dsibak"
if [ "$KIND" = skirmish ]; then MODE=0; else MODE=5; cp "$FX/run_$KIND.txt" "$HR/rearmed_run.txt"; fi
Q3NOLOCK=1 SHOT_CFG=$CFG SHOT_OUT="$OUT" SHOT_LOG="$OUT.log" SHOT_TIMEOUT=120 bash "$Q3MOD/tools/shot.sh" q3dm6 $W $H \
  +set g_gametype 0 +set g_rearmMode $MODE +set bot_minplayers 0 "$@"
[ -f "$HR/rearmed_run.dsibak" ] && mv "$HR/rearmed_run.dsibak" "$HR/rearmed_run.txt"
grep -a -E "DSIntro|Descent:|DSBoss: floor" "$OUT.log" "$OUT/games.log" 2>/dev/null | cut -c1-400
