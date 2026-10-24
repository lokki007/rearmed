#!/bin/bash
# promo clips, pass 1 (demo): clip_rec.sh <cfg> <map> [extra engine args]
# Runs tools/clip_cfgs/<cfg>.cfg in the isolated dist_clips + home_clips install (never home/ or home_test/)
# in a small window; the cfg records a demo (record <name> ... stoprecord) -> home_clips/rearmed/demos/.
# Pass 2 (clip_play.sh) renders the demo with the video command at the final size.
source "$(dirname "$0")/env.sh"
q3lock
CFG=$1; MAP=$2; shift 2
HR="$Q3MOD/home_clips/rearmed"
cp "$Q3MOD"/tools/clip_cfgs/*.cfg "$HR/"
rm -f "$HR/ioq3.pid"
Q3DIST="$Q3MOD/dist_clips" Q3HOME="$Q3MOD/home_clips" timeout ${CLIP_TIMEOUT:-180} "$Q3MOD/tools/run.sh" \
  +set r_fullscreen 0 +set r_mode -1 +set r_customwidth ${RW:-960} +set r_customheight ${RH:-540} +set in_mouse 0 \
  +set com_maxfps 125 +set sv_fps 40 +set snaps 40 +set name Player "$@" $( [ "$MAP" != "-" ] && echo "+devmap $MAP" ) +exec $CFG.cfg > "$Q3MOD/home_clips/$CFG.log" 2>&1
echo "exit $?"
grep -a -v GL_EXT "$Q3MOD/home_clips/$CFG.log" | grep -a -i -E "^(error|warning)|ERROR:|recording to|kchurt|where|DS|KillCam" | cut -c1-200 | head -40
ls -la "$HR/demos/" 2>/dev/null
