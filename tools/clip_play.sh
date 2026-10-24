#!/bin/bash
# promo clips, pass 2: clip_play.sh W H demo1 [demo2 ...] -> home_clips/rearmed/videos/<demo>.avi
# One engine run plays the demos one after another (nextdemo chain). Paced to 30 real fps (PFPS) so the
# kill cam (it runs on real time) plays at its real speed in the video. EXTRA = more engine args,
# VIEWCFG = an extra cfg exec'd before the demo, POSTCMD = commands run after `video` (e.g. wait + dsintro).
source "$(dirname "$0")/env.sh"
q3lock
W=$1; H=$2; shift 2
HR="$Q3MOD/home_clips/rearmed"
cp "$Q3MOD"/tools/clip_cfgs/*.cfg "$HR/"
i=0; n=$#
for D in "$@"; do
  NEXT="quit"; [ $((i+1)) -lt $n ] && NEXT="exec clip_play$((i+1)).cfg"
  printf 'exec clip_common.cfg\nexec clip_view.cfg\n%s\nset nextdemo "%s"\ndemo %s\nvideo %s\n%s\n' "${VIEWCFG:+exec $VIEWCFG}" "$NEXT" "$D" "$D" "$POSTCMD" >"$HR/clip_play$i.cfg"
  rm -f "$HR/videos/$D.avi"
  i=$((i+1))
done
rm -f "$HR/ioq3.pid"
Q3DIST="$Q3MOD/dist_clips" Q3HOME="$Q3MOD/home_clips" timeout ${CLIP_TIMEOUT:-600} "$Q3MOD/tools/run.sh" \
  +set r_fullscreen 0 +set r_mode -1 +set r_customwidth $W +set r_customheight $H +set in_mouse 0 \
  +set com_maxfps ${PFPS:-30} +set r_swapInterval 0 +set cl_aviFrameRate 30 +set cl_aviMotionJpeg 1 $EXTRA +exec clip_play0.cfg > "$Q3MOD/home_clips/play_$1.log" 2>&1
echo "exit $?"
grep -a -v GL_EXT "$Q3MOD/home_clips/play_$1.log" | grep -a -i -E "^(error|warning)|ERROR:|Wrote" | cut -c1-200 | head -20
