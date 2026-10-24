#!/bin/bash
# en-rematch: BOSS REMATCH tests in an isolated home (home_test/rematch): rematch_test.sh <cfg> <none|two|all> [W H]
# Seeds COPIES of the player's run/meta/records/history/skills/tally/ghost once (read only), then writes the test meta:
#   none = no cnt_boss (every boss locked), two = only cnt_boss0/1 (Butcher + Overseer unlocked), all = the copy as is.
# Hashes the progress files before and after (they must not change) and the player's real files too.
# RESET=1 deletes rearmed_rematch.txt first. Output: home_test/rematch/out/<cfg>/ (screenshots, logs, md5 checks).
source "$(dirname "$0")/env.sh"
[ -z "$Q3NOLOCK" ] && q3lock
CFG=$1; META=${2:-two}; W=${3:-3840}; H=${4:-1080}
TH="$Q3MOD/home_test/rematch"; TR="$TH/rearmed"; OUT="$TH/out/${CFG%.cfg}"; UR="$Q3MOD/home/rearmed"
mkdir -p "$TR" "$OUT"
python "$Q3MOD/tools/rematch_cfgs/gen.py"
cp "$Q3MOD"/build_pk3/*.pk3 "$TR/"
cp "$Q3MOD"/home_test/rearmed/autoexec.cfg "$TR/" 2>/dev/null
cp "$Q3MOD"/home_test/rearmed/rearmed_gfx.cfg "$TR/" 2>/dev/null
cp "$Q3MOD"/tools/rematch_cfgs/*.cfg "$TR/"
FILES="rearmed_run.txt rearmed_meta.txt rearmed_records.txt rearmed_history.txt rearmed_skills.txt rearmed_tally.txt rearmed_ghost.txt"
for f in $FILES; do [ -f "$TR/$f" ] || { [ -f "$UR/$f" ] && cp "$UR/$f" "$TR/$f"; }; done
[ -f "$TR/meta_full.txt" ] || cp "$UR/rearmed_meta.txt" "$TR/meta_full.txt"
case "$META" in
  none) grep -v '^cnt_boss[0-9]' "$TR/meta_full.txt" > "$TR/rearmed_meta.txt" ;;
  two)  grep -v '^cnt_boss[23] ' "$TR/meta_full.txt" > "$TR/rearmed_meta.txt" ;;
  *)    cp "$TR/meta_full.txt" "$TR/rearmed_meta.txt" ;;
esac
[ "$RESET" = 1 ] && rm -f "$TR/rearmed_rematch.txt"
EXIST=""; for f in $FILES; do [ -f "$TR/$f" ] && EXIST="$EXIST $f"; done
( cd "$TR" && md5sum $EXIST > "$OUT/test_progress.md5" )
( cd "$UR" && md5sum $EXIST > "$OUT/player_progress.md5" )
rm -f "$TR"/screenshots/*.jpg "$TR/ioq3.pid" "$TR/games.log" "$TR/qconsole.log" "$OUT"/*.jpg "$OUT"/*.log
Q3DIST="$Q3MOD/dist_test" Q3HOME="$TH" timeout ${SHOT_TIMEOUT:-300} "$Q3MOD/tools/run.sh" +set r_fullscreen 0 +set r_mode -1 \
  +set r_customwidth $W +set r_customheight $H +set g_log games.log +set g_logsync 1 +set logfile 2 $EXTRA +exec $CFG > "$OUT/run.log" 2>&1
cp "$TR"/screenshots/*.jpg "$OUT/" 2>/dev/null
cp "$TR/games.log" "$TR/qconsole.log" "$TR/rearmed_rematch.txt" "$TR/rearmed_practice.txt" "$OUT/" 2>/dev/null
echo "== test-home progress files (must all be OK):"; ( cd "$TR" && md5sum -c "$OUT/test_progress.md5" )
echo "== the player's own files (must all be OK):"; ( cd "$UR" && md5sum -c "$OUT/player_progress.md5" )
echo "== rearmed_rematch.txt:"; cat "$TR/rearmed_rematch.txt" 2>/dev/null
ls "$OUT"
