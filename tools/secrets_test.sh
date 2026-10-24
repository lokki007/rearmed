#!/bin/bash
# ds-secrets: a DESCENT fight floor in an isolated test home (home_test/secrets), from a fixture run.
#   secrets_test.sh <map> <cards csv or -> <cfg in tools/secrets_cfgs> [seed] [W H]
# Writes rearmed_run.txt (floor 3, fight node, the given cards), runs +devmap <map> with g_rearmMode 5,
# execs the cfg, copies screenshots + games.log to home_test/secrets/out/<tag>/ (tag = TAG env or the cfg).
# Never touches home/rearmed.
source "$(dirname "$0")/env.sh"
[ -z "$Q3NOLOCK" ] && q3lock
MAP=$1; CARDS=${2:--}; CFG=$3; SEED=${4:-4242}; W=${5:-1600}; H=${6:-900}
[ "$CARDS" = "-" ] && CARDS=""
TAG=${TAG:-${CFG%.cfg}}
SH="$Q3MOD/home_test/secrets"; SR="$SH/rearmed"; OUT="$SH/out/$TAG"
mkdir -p "$SR" "$OUT"
cp "$Q3MOD"/build_pk3/*.pk3 "$SR/"
cp "$Q3MOD"/home_test/rearmed/autoexec.cfg "$SR/" 2>/dev/null
cp "$Q3MOD"/tools/secrets_cfgs/*.cfg "$SR/"
rm -f "$SR"/screenshots/*.jpg "$SR/ioq3.pid" "$SR/games.log" "$OUT"/*
cat > "$SR/rearmed_run.txt" <<EOF
version 2
active 1
seed $SEED
difficulty 3
diff_start 3
diff_min 3
diff_max 3
lives 3
floor 2
skulls 0
cards $CARDS
kills 0
time 0
result none
offer
map 0.0
rerolls 0
character 0
startcards 0
act 1
infloor 1
node_type fight
node_map $MAP
node_bots 2
node_frags 40
node_mutator 0
node_skill 1.00
node_tier 0
node_floor 3
node_layer 2
node_index 0
EOF
[ -n "$META" ] && printf '%b' "$META" > "$SR/rearmed_meta.txt"
Q3DIST="$Q3MOD/dist_test" Q3HOME="$SH" timeout ${SHOT_TIMEOUT:-180} "$Q3MOD/tools/run.sh" +set r_fullscreen 0 +set r_mode -1 \
  +set r_customwidth $W +set r_customheight $H +set g_log games.log +set g_logsync 1 +set logfile 2 \
  +set g_rearmMode 5 +set g_gametype 0 +set g_dsSecrets 1 +set g_dsSecretsDebug ${SECDBG:-0} +set g_mutators 0 \
  +set bot_pause 0 +set cg_drawFPS 1 $EXTRA +devmap $MAP +exec $CFG > "$OUT/run.log" 2>&1
cp "$SR"/screenshots/*.jpg "$OUT/" 2>/dev/null
cp "$SR/games.log" "$OUT/" 2>/dev/null
ls "$OUT"
