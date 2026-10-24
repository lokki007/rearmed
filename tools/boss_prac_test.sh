#!/bin/bash
# en-boss: BOSS PRACTICE from the main menu (no map before it): boss_prac_test.sh <cfg> W H
# Hashes home_test's progress files before and after (they must not change), screenshots -> $OUT
source "$(dirname "$0")/env.sh"
q3lock
CFG=$1; W=$2; H=$3; shift 3
HR="$Q3MOD/home_test/rearmed"; OUT=${OUT:-$Q3MOD/home_test/boss_prac_${CFG%.cfg}_${W}}
cp "$Q3MOD/tools/boss_fixtures/$CFG" "$HR/$CFG"
FILES="rearmed_run.txt rearmed_meta.txt rearmed_records.txt rearmed_history.txt rearmed_tally.txt rearmed_skills.txt rearmed_ghost.txt"
( cd "$HR" && md5sum $FILES > "$OUT.before.md5"; ls -l --time-style=+%T $FILES > "$OUT.before.ls" ) 2>/dev/null || mkdir -p "$(dirname "$OUT")"
SS="$HR/screenshots"; rm -f "$SS"/*.jpg "$HR/ioq3.pid"
Q3DIST="$Q3MOD/dist_test" Q3HOME="$Q3MOD/home_test" timeout ${SHOT_TIMEOUT:-150} "$Q3MOD/tools/run.sh" +set r_fullscreen 0 +set r_mode -1 \
  +set r_customwidth $W +set r_customheight $H +set g_dsBossTime 300 +set g_dsBossMode 0 +set g_dsStagger 1 +set timescale 1 "$@" \
  +exec $CFG > "$OUT.log" 2>&1
mkdir -p "$OUT"; rm -f "$OUT"/*.jpg; cp "$SS"/*.jpg "$OUT"/ 2>/dev/null; cp "$HR/games.log" "$OUT/games.log" 2>/dev/null
echo "== progress files (must all be OK):"; ( cd "$HR" && md5sum -c "$OUT.before.md5" )
echo "== practice file:"; head -3 "$HR/rearmed_practice.txt"
grep -a -E "DESCENT: boss practice|Failed to allocate" "$OUT.log" | sort | uniq -c | head
ls "$OUT"/*.jpg
