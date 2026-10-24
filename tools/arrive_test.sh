#!/bin/bash
# ds-arrivals test: arrive_test.sh <normal|elite|boss|big> W H [extra engine args]
# Copies tools/arrive_fixtures/run_<kind>.txt into home_test (backs the old run file up and puts it back),
# runs CFG (default arr_shot.cfg: screenshots every ~2 s), output in $OUT (default home_test/arrive_<kind>_<W>)
source "$(dirname "$0")/env.sh"
q3lock
KIND=$1; W=$2; H=$3; shift 3
FX="$Q3MOD/tools/arrive_fixtures"; HR="$Q3MOD/home_test/rearmed"
OUT=${OUT:-$Q3MOD/home_test/arrive_${KIND}_${W}}
CFG=${CFG:-arr_shot.cfg}; cp "$FX/$CFG" "$HR/$CFG"
[ -f "$HR/rearmed_run.txt" ] && cp "$HR/rearmed_run.txt" "$HR/rearmed_run.arrbak"
cp "$FX/run_$KIND.txt" "$HR/rearmed_run.txt"
Q3NOLOCK=1 SHOT_CFG=$CFG SHOT_OUT="$OUT" SHOT_LOG="$OUT.log" SHOT_TIMEOUT=150 bash "$Q3MOD/tools/shot.sh" q3dm6 $W $H \
  +set g_gametype 0 +set g_rearmMode 5 +set bot_minplayers 0 +set g_dsStagger 1 +set cg_dsIntro 1 \
  +set g_dsAmbush 0 +set g_dsWildEvents 0 +set g_dsGhosts 0 "$@"
[ -f "$HR/rearmed_run.arrbak" ] && mv "$HR/rearmed_run.arrbak" "$HR/rearmed_run.txt"
grep -a -E "DSArrive|DSVoice|DSIntro: (start|over|skipped)|Wrote screenshots|DSModels: [0-9]+ stand" "$OUT.log" | cut -c1-300
grep -a -E "DSArrive|DSBoss: (floor|spawned)|Descent: floor" "$OUT/games.log" 2>/dev/null | cut -c1-300
