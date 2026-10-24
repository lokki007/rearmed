#!/bin/bash
# DESCENT META UI test: screenshots of the hub, character select, upgrade tree, collection,
# records, history and the run summary from fixture files, in ONE short game window.
#   test_meta.sh [W H] [fixture dir]    -> screenshots + the resulting files in $OUT (default scratch/meta_shots)
# Works only on home_test: backs up home_test's rearmed_{run,meta,history}.txt, installs the fixtures
# (tools/meta_fixtures/ by default: run.txt, meta.txt, history.txt, any may be missing), runs, copies the
# results out, restores the backups. Never touches home/rearmed.
source "$(dirname "$0")/env.sh"
q3lock
W=${1:-1920}; H=${2:-1080}
FIX=${3:-$Q3MOD/tools/meta_fixtures}
OUT=${OUT:-$Q3MOD/home_test/meta_shots_${W}x${H}}
HT="$Q3MOD/home_test/rearmed"
BK=$(mktemp -d)
for f in run meta history; do
  [ -f "$HT/rearmed_$f.txt" ] && cp "$HT/rearmed_$f.txt" "$BK/"
  rm -f "$HT/rearmed_$f.txt"
  [ -f "$FIX/$f.txt" ] && cp "$FIX/$f.txt" "$HT/rearmed_$f.txt"
done
cp "${CFG:-$Q3MOD/tools/meta_fixtures/meta_shots.cfg}" "$HT/meta_shots.cfg"
mkdir -p "$OUT"; rm -f "$OUT"/*
Q3NOLOCK=1 SHOT_CFG=meta_shots.cfg SHOT_OUT="$OUT" SHOT_LOG="$OUT/run.log" SHOT_TIMEOUT=${SHOT_TIMEOUT:-120} \
  bash "$Q3MOD/tools/shot.sh" q3dm17 "$W" "$H" +set g_rearmMode 0 +set cg_draw2D 1
for f in run meta history; do
  [ -f "$HT/rearmed_$f.txt" ] && cp "$HT/rearmed_$f.txt" "$OUT/after_$f.txt"
  rm -f "$HT/rearmed_$f.txt"
  [ -f "$BK/rearmed_$f.txt" ] && cp "$BK/rearmed_$f.txt" "$HT/"
done
rm -rf "$BK"
grep -E "DESCENT|dsmeta|history" "$OUT/run.log" | head -40
echo "results in $OUT"
