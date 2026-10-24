#!/bin/bash
# en-boss test: boss_test.sh <cfg in tools/boss_fixtures> W H [extra engine args]
# A timed 1v1 boss floor (Butcher, floor 6 kit) in home_test: copies boss_fixtures/run_boss1.txt over
# home_test's run file (the old one is backed up and put back), runs the cfg, screenshots -> $OUT
# (default home_test/boss_<cfg>_<W>). Practice tests: RUN=none keeps home_test's run file as it is.
source "$(dirname "$0")/env.sh"
q3lock
CFG=$1; W=$2; H=$3; shift 3
FX="$Q3MOD/tools/boss_fixtures"; HR="$Q3MOD/home_test/rearmed"
OUT=${OUT:-$Q3MOD/home_test/boss_${CFG%.cfg}_${W}}
MAP=${MAP:-q3dm6}
cp "$FX/$CFG" "$HR/$CFG"
if [ "${RUN:-boss1}" != none ]; then
  [ -f "$HR/rearmed_run.txt" ] && cp "$HR/rearmed_run.txt" "$HR/rearmed_run.bossbak"
  cp "$FX/run_${RUN:-boss1}.txt" "$HR/rearmed_run.txt"
fi
Q3NOLOCK=1 SHOT_CFG=$CFG SHOT_OUT="$OUT" SHOT_LOG="$OUT.log" SHOT_TIMEOUT=${SHOT_TIMEOUT:-150} bash "$Q3MOD/tools/shot.sh" $MAP $W $H \
  +set g_gametype 0 +set g_rearmMode 5 +set bot_minplayers 0 +set g_dsBossMode 0 +set g_dsBossTime 300 +set g_dsStagger 1 \
  +set g_dsPractice 0 +set g_dsBossScale 1 +set timescale 1 "$@"
[ -f "$HR/rearmed_run.bossbak" ] && mv "$HR/rearmed_run.bossbak" "$HR/rearmed_run.txt"
grep -a -E "DSBoss|DSArrive: (plan|warn)|Descent: (floor|boss|practice)|DSEcon|ClientBegin" "$OUT/games.log" 2>/dev/null | cut -c1-300
