#!/bin/bash
# DESCENT SKILLS UI test: screenshots of the hub, the SKILLS screen (merge, spend, respec) and the
# run summary block from fixture files, in ONE short game window.
#   test_skills.sh [W H] [fixture dir]    -> screenshots + the resulting files in $OUT
# Works only on home_test: backs up home_test's rearmed_{run,meta,history,skills}.txt, installs the
# fixtures (tools/skills_fixtures/: run.txt, meta.txt, skills.txt), runs, copies the results out,
# restores the backups. Never touches home/rearmed.
source "$(dirname "$0")/env.sh"
q3lock
W=${1:-1920}; H=${2:-1080}
FIX=${3:-$Q3MOD/tools/skills_fixtures}
OUT=${OUT:-$Q3MOD/home_test/skills_shots_${W}x${H}}
HT="$Q3MOD/home_test/rearmed"
BK=$(mktemp -d)
for f in run meta history skills; do
  [ -f "$HT/rearmed_$f.txt" ] && cp "$HT/rearmed_$f.txt" "$BK/"
  rm -f "$HT/rearmed_$f.txt"
  [ -f "$FIX/$f.txt" ] && cp "$FIX/$f.txt" "$HT/rearmed_$f.txt"
done
cp "${CFG:-$FIX/skills_shots.cfg}" "$HT/skills_shots.cfg"
mkdir -p "$OUT"; rm -f "$OUT"/*
Q3NOLOCK=1 SHOT_CFG=skills_shots.cfg SHOT_OUT="$OUT" SHOT_LOG="$OUT/run.log" SHOT_TIMEOUT=${SHOT_TIMEOUT:-120} \
  bash "$Q3MOD/tools/shot.sh" q3dm17 "$W" "$H" +set g_rearmMode 0 +set cg_draw2D 1
for f in run meta history skills; do
  [ -f "$HT/rearmed_$f.txt" ] && cp "$HT/rearmed_$f.txt" "$OUT/after_$f.txt"
  rm -f "$HT/rearmed_$f.txt"
  [ -f "$BK/rearmed_$f.txt" ] && cp "$BK/rearmed_$f.txt" "$HT/"
done
rm -rf "$BK"
grep -E "SKILLS|dsskill" "$OUT/run.log" | head -40
echo "results in $OUT"
