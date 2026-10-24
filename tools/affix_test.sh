#!/bin/bash
# en-affix test: affix_test.sh <cfg> <seed> W H [map] [extra engine args]
# Plays DESCENT floor 8 (act 2, an elite floor) of tools/affix_fixtures/run_elite8.txt (seed replaced)
# on <map> (default q3dm6) in home_test. The test home's run file is backed up and put back.
# Screenshots, console log and the new games.log lines -> $OUT (default home_test/affix_<cfg>_<W>).
source "$(dirname "$0")/env.sh"
q3lock
CFG=$1; SEED=$2; W=$3; H=$4; MAP=${5:-q3dm6}; shift 4; [ $# -gt 0 ] && shift
FX="$Q3MOD/tools/affix_fixtures"; HR="$Q3MOD/home_test/rearmed"
OUT=${OUT:-$Q3MOD/home_test/affix_${CFG%.cfg}_${W}}
python "$FX/gen.py" >/dev/null
cp "$FX/$CFG" "$HR/$CFG"
GLOG=$(grep -a '^seta g_log ' "$HR/q3config.cfg" 2>/dev/null | cut -d'"' -f2); GLOG=${GLOG:-games.log}
SZ=$(stat -c %s "$HR/$GLOG" 2>/dev/null || echo 0)
[ -f "$HR/rearmed_run.txt" ] && cp "$HR/rearmed_run.txt" "$HR/rearmed_run.txt.affbak"
sed -e "s/^seed SEED/seed $SEED/" "$FX/${RUN:-run_elite8.txt}" > "$HR/rearmed_run.txt"
Q3NOLOCK=1 SHOT_CFG=$CFG SHOT_OUT="$OUT" SHOT_LOG="$OUT.log" SHOT_TIMEOUT=${SHOT_TIMEOUT:-200} bash "$Q3MOD/tools/shot.sh" $MAP $W $H \
  +set g_gametype 0 +set g_rearmMode 5 +set bot_minplayers 0 +set sv_maxclients 16 +set g_dsAmbush 0 +set g_dsModifiers 0 \
  +set g_dsWildEvents 0 +set g_dsGhosts 0 +set g_dsNemesis 0 +set cg_dsIntro 0 +set g_dsStagger 0 \
  +set g_dsAffixes 1 +set g_dsChampions 1 +set timescale 1 "$@"
rm -f "$HR/rearmed_run.txt"; [ -f "$HR/rearmed_run.txt.affbak" ] && mv "$HR/rearmed_run.txt.affbak" "$HR/rearmed_run.txt"
tail -c +$((SZ+1)) "$HR/$GLOG" > "$OUT/new.log" 2>/dev/null
grep -a -E "DSAffix|Descent: extra bot" "$OUT/new.log" 2>/dev/null | cut -c1-260
