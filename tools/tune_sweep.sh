#!/bin/bash
# en-tune: tactics-by-difficulty sweep with tools/bt_sim.py --standin.
# Client 0 = a fixed stock "player" (sarge, skill 3, tactics off); 4 opponents at skill S, tactics on / off.
# Usage: tools/tune_sweep.sh <tagprefix> [skills] [runs]   e.g. tools/tune_sweep.sh pre "1 2 3 4 5" 12
# Then: python tools/tune_table.py <tagprefix>
cd "$(dirname "$0")/.."
PFX=${1:-pre}; SKILLS=${2:-"1 2 3 4 5"}; RUNS=${3:-12}; PAR=${PAR:-6}; MAPS=${MAPS:-q3dm6,q3dm7,q3dm13}
for S in $SKILLS; do
  for T in 0 1; do
    python tools/bt_sim.py --standin --skill0 3 --skill $S --tactics $T --bots 5 --minutes 5 --runs $RUNS \
      --par $PAR --maps $MAPS --tag ${PFX}_s${S}_t${T} --slotbase $(( ${SLOT0:-0} + T * PAR )) > home_bt/${PFX}_s${S}_t${T}.out 2>&1 &
    sleep 20
  done
  wait
done
echo SWEEP DONE $PFX
