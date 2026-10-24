#!/bin/bash
# en-tune: the UI half of the type adaptation (q3_ui DSM_TypeAdaptMerge) in a short small window, isolated home
# home_tune/ui. Two merges of the same run (floor 1 then floor 2 of seed 777) on a meta from an older run (seed 1):
# the first rolls tad_w (x0.5 + the old run's tad_cur) and starts tad_cur over, the second only adds.
source "$(dirname "$0")/env.sh"
[ -z "$Q3NOLOCK" ] && q3lock
H="$Q3MOD/home_tune/ui"; M="$H/rearmed"
rm -rf "$H"; mkdir -p "$M"
cp "$Q3MOD"/build_pk3/*.pk3 "$M/"
cat > "$M/rearmed_meta.txt" <<EOF
tad_w 0,80,0,0,0,10,0,0,0,0
tad_cur 0,1,0,0,0,4,0,0,0,0
tad_seed 1
EOF
run() {  # run <fseq> <tad_f>
cat > "$M/rearmed_run.txt" <<EOF
version 2
active 1
seed 777
difficulty 3
floor $1
fseq $1
tad_f $2
EOF
printf 'wait 100\ndsui process\nwait 30\nquit\n' > "$M/ta_ui.cfg"
Q3DIST="$Q3MOD/dist_test" Q3HOME="$H" timeout 90 "$Q3MOD/tools/run.sh" +set r_fullscreen 0 +set r_mode -1 \
  +set r_customwidth 640 +set r_customheight 360 +set s_volume 0 +set g_rearmMode 0 +exec ta_ui.cfg > "$H/run_$1.log" 2>&1
echo "after merge of floor $1 (tad_f $2):"; grep "^tad_" "$M/rearmed_meta.txt"
}
echo "before:"; cat "$M/rearmed_meta.txt"
run 1 0,2,0,0,0,1,0,0,0,0
run 2 0,1,0,0,0,0,0,0,0,3
