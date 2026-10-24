#!/bin/bash
# en-screens: full-resolution HUD / menu screenshots in an isolated test home (home_test/screens).
#   screens_test.sh <cfg> ds   [map] [W H]   a DESCENT fight floor (run file below), devmap (cheats)
#   screens_test.sh <cfg> menu [-]   [W H]   no map: the cfg runs at the main menu (F10 tabs, hub screens)
# Defaults: 7680 x 2160 (the player's screen). Cfgs: tools/screens_fixtures (gen.py).
# Env: FLOOR (default 14 = act 3), BOTS (default 6), FORCE (g_dsTypesForce mix), SEED, TAG, EXTRA (engine args),
#      PK3DIR (take the zz-rearmed-*.pk3 from there instead of build_pk3), META (a meta file to copy in).
# Outputs: home_test/screens/out/<cfg>[_TAG]/ (screenshots, games.log, qconsole.log).
source "$(dirname "$0")/env.sh"
[ -z "$Q3NOLOCK" ] && q3lock
CFG=$1; MODE=${2:-ds}; MAP=${3:-q3dm6}; W=${4:-7680}; H=${5:-2160}
FLOOR=${FLOOR:-14}; BOTS=${BOTS:-6}; SEED=${SEED:-4242}; ACT=$(( (FLOOR - 1) / 6 + 1 ))
EH="$Q3MOD/home_test/screens"; ER="$EH/rearmed"; OUT="$EH/out/${CFG%.cfg}${TAG:+_$TAG}"
mkdir -p "$ER" "$OUT"
rm -f "$ER"/zz-rearmed-*.pk3
cp "${PK3DIR:-$Q3MOD/build_pk3}"/zz-rearmed-*.pk3 "$ER/"
cp "$Q3MOD"/home_test/rearmed/autoexec.cfg "$ER/" 2>/dev/null
python "$Q3MOD/tools/screens_fixtures/gen.py" > /dev/null
cp "$Q3MOD"/tools/screens_fixtures/*.cfg "$ER/"
rm -f "$ER"/screenshots/*.jpg "$ER/ioq3.pid" "$ER/games.log" "$ER/qconsole.log" "$OUT"/*
# archived switches from earlier runs must not leak in
rm -f "$ER/q3config.cfg"
[ -n "$META" ] && cp "$META" "$ER/rearmed_meta.txt"
cat > "$ER/rearmed_run.txt" <<EOF
version 2
active 1
seed $SEED
difficulty 3
diff_start 3
diff_min 3
diff_max 3
lives 3
floor $((FLOOR - 1))
skulls 120
cards 0,3,7,11,14
kills 30
time 0
result none
offer
map 0.0.0.0.0.0
rerolls 1
character 0
startcards 0
pv 1,0,0,2,0,0,0,0,0,0,0,0,0
wl 0,3,4,0,0,2,0,0,0,0,0,0,0,0
act $ACT
infloor 1
node_type fight
node_map $MAP
node_bots $BOTS
node_frags 60
node_mutator 0
node_skill 3.00
node_tier $(( ACT - 1 ))
node_floor $FLOOR
node_layer 1
node_index 0
EOF
# the switches go in a cfg (ioq3 cuts its command line at 1024 characters); the player's own HUD settings
{
  echo "set g_log games.log"; echo "set g_logsync 1"; echo "set logfile 2"; echo "set ui_cdkeychecked 1"
  echo "set g_gametype 0"; echo "set g_rearmMode 5"; echo "set bot_minplayers 0"; echo "set sv_maxclients 16"
  echo "set timescale 1"; echo "set g_dsTypes 1"; echo "set g_dsTypesForce \"$FORCE\""; echo "set g_dsTypesShots 0"
  echo "set g_dsAmbush 0"; echo "set g_dsWildEvents 0"; echo "set g_dsGhosts 0"; echo "set g_dsStagger 0"
  echo "set cg_dsIntro 0"; echo "set g_dsAffixes 1"; echo "set g_dsChampions 0"; echo "set g_dsModifiers 0"
  echo "set g_dsNemesis 1"; echo "set cg_enemyTags 0"; echo "set cg_hudWide 1"; echo "set cg_drawCrosshairNames 1"
  echo "set cg_drawFPS 0"; echo "set r_customPixelAspect 1"; echo "set com_maxfps 100"
} > "$ER/screens_setup.cfg"
if [ "$MODE" = menu ]; then
  LAUNCH="+exec $CFG"
else
  LAUNCH="+devmap $MAP +exec $CFG"
fi
Q3DIST="$Q3MOD/dist_test" Q3HOME="$EH" timeout ${SHOT_TIMEOUT:-240} "$Q3MOD/tools/run.sh" +set r_fullscreen 0 +set r_mode -1 \
  +set r_customwidth $W +set r_customheight $H +set com_hunkMegs 256 +exec screens_setup.cfg $EXTRA $LAUNCH > "$OUT/run.log" 2>&1
cp "$ER"/screenshots/*.jpg "$OUT/" 2>/dev/null
cp "$ER/games.log" "$OUT/" 2>/dev/null
cp "$ER/qconsole.log" "$OUT/" 2>/dev/null
ls "$OUT"
