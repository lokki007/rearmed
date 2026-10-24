#!/bin/bash
# ds-maps tests (home_test only):
#   dsmaps_test.sh sim          the floor-map simulation (dsmapsim, tools/dsmaps_cfgs/dsm_sim.cfg):
#                               20 runs x 24 floors (fresh profile and the player's real play counts,
#                               from a COPY of home/rearmed/games.log), 4 runs x 90 floors (the Abyss)
#                               -> home_test/dsmaps/rearmed_mapsim_*.txt
#   dsmaps_test.sh ui <cfg> W H the menu with a fixture run (screenshots of the floor map)
source "$(dirname "$0")/env.sh"
q3lock
HR="$Q3MOD/home_test/rearmed"; OUT="$Q3MOD/home_test/dsmaps"; mkdir -p "$OUT"
MODE=$1; shift
for f in rearmed_run.txt rearmed_meta.txt; do [ -f "$HR/$f" ] && cp "$HR/$f" "$HR/$f.dsmbak"; done
restore() { for f in rearmed_run.txt rearmed_meta.txt; do rm -f "$HR/$f"; [ -f "$HR/$f.dsmbak" ] && mv "$HR/$f.dsmbak" "$HR/$f"; done; rm -f "$HR/dsm_player_games.log" "$HR/ioq3.pid"; }
if [ "$MODE" = sim ]; then
  cp "$Q3MOD/home/rearmed/games.log" "$HR/dsm_player_games.log"   # read only copy of the player's log
  rm -f "$HR"/rearmed_mapsim_*.txt; cp "$Q3MOD/tools/dsmaps_cfgs/dsm_sim.cfg" "$HR/"
  Q3DIST="$Q3MOD/dist_test" Q3HOME="$Q3MOD/home_test" timeout ${TIMEOUT:-240} "$Q3MOD/tools/run.sh" +set r_fullscreen 0 +set r_mode -1 \
    +set r_customwidth 640 +set r_customheight 360 +exec dsm_sim.cfg > "$OUT/sim.log" 2>&1

  mv "$HR"/rearmed_mapsim_*.txt "$OUT/" 2>/dev/null
  grep -a -E "^(floors launched|shown map|act 1 pool|DESCENT maps)" "$OUT"/rearmed_mapsim_*.txt
elif [ "$MODE" = ui ]; then
  CFG=$1; W=$2; H=$3
  cp "$Q3MOD/tools/dsmaps_cfgs/$CFG" "$HR/$CFG"
  cp "$Q3MOD/tools/dsmaps_cfgs/${RUNFX:-run_fixture.txt}" "$HR/rearmed_run.txt"
  [ -n "$METAFX" ] && cp "$Q3MOD/tools/dsmaps_cfgs/$METAFX" "$HR/rearmed_meta.txt"
  rm -f "$HR"/screenshots/*.jpg
  Q3DIST="$Q3MOD/dist_test" Q3HOME="$Q3MOD/home_test" timeout ${TIMEOUT:-120} "$Q3MOD/tools/run.sh" +set r_fullscreen 0 +set r_mode -1 \
    +set r_customwidth $W +set r_customheight $H +set g_dsVariants 1 +exec $CFG > "$OUT/ui.log" 2>&1
  mkdir -p "$OUT/${CFG%.cfg}"; rm -f "$OUT/${CFG%.cfg}"/*.jpg; cp "$HR"/screenshots/*.jpg "$OUT/${CFG%.cfg}/" 2>/dev/null
  cp "$HR/rearmed_run.txt" "$OUT/${CFG%.cfg}/run_after.txt"; cp "$HR/rearmed_meta.txt" "$OUT/${CFG%.cfg}/meta_after.txt" 2>/dev/null
  ls "$OUT/${CFG%.cfg}"
elif [ "$MODE" = floor ]; then
  # a DESCENT floor straight from a run fixture: dsmaps_test.sh floor <run fixture> <cfg> <map> W H
  RFX=$1; CFG=$2; MAP=$3; W=$4; H=$5
  cp "$Q3MOD/tools/dsmaps_cfgs/$CFG" "$HR/$CFG"
  cp "$Q3MOD/tools/dsmaps_cfgs/$RFX" "$HR/rearmed_run.txt"
  sed -i "s/^node_map .*/node_map $MAP/" "$HR/rearmed_run.txt"
  rm -f "$HR"/screenshots/*.jpg
  Q3DIST="$Q3MOD/dist_test" Q3HOME="$Q3MOD/home_test" timeout ${TIMEOUT:-150} "$Q3MOD/tools/run.sh" +set r_fullscreen 0 +set r_mode -1     +set r_customwidth $W +set r_customheight $H +set g_gametype 0 +set g_rearmMode 5 +set g_dsVariants 1 +set cg_dsVariantLight 1     +set bot_minplayers 0 +set sv_maxclients 12 +set g_dsNemesis 0 +set cg_dsIntro 0 +set g_mutators 0 +devmap $MAP +exec $CFG > "$OUT/floor.log" 2>&1
  D="$OUT/${CFG%.cfg}_$MAP"; mkdir -p "$D"; rm -f "$D"/*.jpg; cp "$HR"/screenshots/*.jpg "$D/" 2>/dev/null; ls "$D"
fi
restore
