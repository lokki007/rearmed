#!/bin/bash
# en-types test (enemy archetypes) in an isolated test home: types_test.sh <cfg> [map] [W H]
# Writes a DESCENT run file for one fight floor and launches it with devmap (cheats for god / bot_pause).
# Env: FLOOR (node floor, default 2; act = (FLOOR-1)/6+1), BOTS (default 4), FORCE (g_dsTypesForce mix,
# e.g. "sniper,medic,shield"; empty = the natural floor mix), SHOTS (g_dsTypesShots: screenshots per tell
# kind, default 1), SEED, TS (timescale, default 1), EXTRA (more engine args).
# Cfgs: tools/types_fixtures (gen.py). Outputs: home_test/types/out/<cfg>[_TAG]/ (screenshots, games.log).
source "$(dirname "$0")/env.sh"
[ -z "$Q3NOLOCK" ] && q3lock
CFG=$1; MAP=${2:-q3dm6}; W=${3:-1600}; H=${4:-700}
FLOOR=${FLOOR:-2}; BOTS=${BOTS:-4}; SEED=${SEED:-4242}; ACT=$(( (FLOOR - 1) / 6 + 1 ))
EH="$Q3MOD/home_test/types"; ER="$EH/rearmed"; OUT="$EH/out/${CFG%.cfg}${TAG:+_$TAG}"
mkdir -p "$ER" "$OUT"
rm -f "$ER"/zz-rearmed-*.pk3
cp "$Q3MOD"/build_pk3/zz-rearmed-*.pk3 "$ER/"
cp "$Q3MOD"/home_test/rearmed/autoexec.cfg "$ER/" 2>/dev/null
python "$Q3MOD/tools/types_fixtures/gen.py" > /dev/null
cp "$Q3MOD"/tools/types_fixtures/*.cfg "$ER/"
rm -f "$ER"/screenshots/*.jpg "$ER/ioq3.pid" "$ER/games.log" "$ER/qconsole.log" "$OUT"/*
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
# the switches go in a cfg (ioq3 cuts its command line at 1024 characters)
{
  echo "set g_log games.log"; echo "set g_logsync 1"; echo "set logfile 2"; echo "set ui_cdkeychecked 1"
  echo "set g_gametype 0"; echo "set g_rearmMode 5"; echo "set bot_minplayers 0"; echo "set sv_maxclients 16"
  echo "set timescale ${TS:-1}"; echo "set g_dsTypes 1"; echo "set g_dsTypesForce \"$FORCE\""; echo "set g_dsTypesDebug 1"
  echo "set g_dsTypesShots ${SHOTS:-1}"; echo "set g_dsAmbush 0"; echo "set g_dsWildEvents 0"; echo "set g_dsGhosts 0"
  echo "set g_dsNemesis 0"; echo "set g_dsStagger 0"; echo "set cg_dsIntro 0"; echo "set g_dsAffixes 0"
  echo "set g_dsChampions 0"; echo "set g_dsModifiers 0"
} > "$ER/types_setup.cfg"
Q3DIST="$Q3MOD/dist_test" Q3HOME="$EH" timeout ${SHOT_TIMEOUT:-200} "$Q3MOD/tools/run.sh" +set r_fullscreen 0 +set r_mode -1 \
  +set r_customwidth $W +set r_customheight $H +set com_hunkMegs 256 +exec types_setup.cfg $EXTRA +devmap $MAP +exec $CFG > "$OUT/run.log" 2>&1
cp "$ER"/screenshots/*.jpg "$OUT/" 2>/dev/null
cp "$ER/games.log" "$OUT/" 2>/dev/null
cp "$ER/qconsole.log" "$OUT/" 2>/dev/null
ls "$OUT"
grep -a -E "DSTypes" "$OUT/games.log" 2>/dev/null | grep -v "shield .* hit\|medic .* heals" | cut -c1-260 | head -80
