#!/bin/bash
# en-memory tests (round 4 bot memory + nemesis hall) in an isolated test home: mem_test.sh <cfg> [W H]
# home_test/${HOME_NAME:-mem}; FRESH=1 empties its progress files first (run/meta/history/skills/bots...);
# BOTS=<file> installs a rearmed_bots.txt fixture. CFGDIR (default tools/mem_fixtures) holds the cfg (other
# workers' cfgs work too: daily, practice). MD5=1 hashes rearmed_bots.txt (+ the main progress files) before
# and after. Outputs: home_test/<name>/out/<cfg>/ (games.log, qconsole.log, screenshots, rearmed_bots.txt).
# Never touches home/rearmed. Every archived switch is set explicitly in mem_set.cfg.
source "$(dirname "$0")/env.sh"
[ -z "$Q3NOLOCK" ] && q3lock
CFG=$1; W=${2:-1920}; H=${3:-540}
EH="$Q3MOD/home_test/${HOME_NAME:-mem}"; ER="$EH/rearmed"; OUT="$EH/out/${OUTNAME:-${CFG%.cfg}}"
mkdir -p "$ER" "$OUT"
rm -f "$ER"/zz-rearmed-*.pk3
cp "$Q3MOD"/${PK3DIR:-build_pk3}/zz-rearmed-*.pk3 "$ER/"
cp "$Q3MOD"/home_test/rearmed/autoexec.cfg "$ER/" 2>/dev/null
cp "${CFGDIR:-$Q3MOD/tools/mem_fixtures}"/*.cfg "$ER/"
PROG="rearmed_run.txt rearmed_meta.txt rearmed_history.txt rearmed_skills.txt rearmed_ghost.txt rearmed_tally.txt rearmed_practice.txt rearmed_daily_run.txt rearmed_daily.txt rearmed_bots.txt"
[ "$FRESH" = 1 ] && ( cd "$ER" && rm -f $PROG )
[ -n "$BOTS" ] && cp "$BOTS" "$ER/rearmed_bots.txt"
[ -n "$RUN" ] && cp "$RUN" "$ER/rearmed_run.txt"
[ -n "$META" ] && cp "$META" "$ER/rearmed_meta.txt"
rm -f "$ER"/screenshots/*.jpg "$ER/ioq3.pid" "$ER/games.log" "$ER/qconsole.log" "$OUT"/*
if [ "$MD5" = 1 ]; then ( cd "$ER" && md5sum rearmed_bots.txt rearmed_meta.txt rearmed_history.txt 2>/dev/null > "$OUT/before.md5" ); fi
cat > "$ER/mem_set.cfg" <<EOC
set g_log games.log
set g_logsync 1
set logfile 2
set ui_rlDevmap 1
set g_gametype 0
set g_rearmMode 0
set bot_minplayers 0
set fraglimit 0
set timelimit 0
set g_forcerespawn 1
set g_botTaunts ${TAUNTS:-2}
set g_botGrudges 1
set g_botShouts 1
set g_botMemory ${MEMORY:-1}
set g_dsNemesis 1
set g_dsNemesisLevels 1
set g_dsVeterans 1
set g_dsStagger 0
set g_dsAmbush 0
set g_dsWildEvents 0
set g_dsGhosts 0
set g_dsModifiers 0
set g_dsSecrets 0
set g_mutators 0
set g_fragMult 1
set cg_dsIntro 1
set cg_killCam 0
set cg_deathCam 0
set cg_replays 0
set g_dsBossMode 0
set g_dsBossTime 300
EOC
Q3DIST="$Q3MOD/dist_test" Q3HOME="$EH" timeout ${SHOT_TIMEOUT:-300} "$Q3MOD/tools/run.sh" +set r_fullscreen 0 +set r_mode -1 \
  +set r_customwidth $W +set r_customheight $H +exec mem_set.cfg $EXTRA +exec $CFG > "$OUT/run.log" 2>&1
echo "exit $?"
cp "$ER"/screenshots/*.jpg "$OUT/" 2>/dev/null
cp "$ER/games.log" "$ER/qconsole.log" "$OUT/" 2>/dev/null
cp "$ER/rearmed_bots.txt" "$OUT/" 2>/dev/null
if [ "$MD5" = 1 ]; then echo "== md5 after (must all be OK):"; ( cd "$ER" && md5sum -c "$OUT/before.md5" ); fi
ls "$OUT"
