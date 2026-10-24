#!/bin/bash
# timedemo benchmark: bench.sh W H [extra engine args] -> prints the timedemo result line
source "$(dirname "$0")/env.sh"
W=${1:-1920}; H=${2:-540}; shift 2
rm -f "$Q3MOD/home_test/rearmed/ioq3.pid"
Q3DIST="$Q3MOD/dist_test" Q3HOME="$Q3MOD/home_test" timeout 300 "$Q3MOD/tools/run.sh" +set r_fullscreen 0 +set r_mode -1 +set r_customwidth $W +set r_customheight $H \
  +set com_maxfps 0 +set timedemo 1 +set nextdemo quit "$@" +demo four > "$Q3MOD/home/bench.log" 2>&1
grep -E "frames.*seconds.*fps" "$Q3MOD/home/bench.log" | tail -1
