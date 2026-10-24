#!/bin/bash
# clip_encode.sh <in.avi> <start_frame> <frames> <name> <W> <H> [xfade_frames=9] [target_kb=1000] [prefilter]
# -> promo/clips/<name>.mp4 (H.264 yuv420p, faststart, no audio, loop crossfade) + <name>.jpg (its first frame)
# The last xfade frames dissolve into the first ones, so the clip loops without a jump.
FF=/c/Users/Ian/AppData/Local/Microsoft/WinGet/Packages/Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe/ffmpeg-8.1.1-full_build/bin
IN=$1; S=$2; N=$3; NAME=$4; W=$5; H=$6; X=${7:-9}; TGT=${8:-1000}; PRE=${9:-null}
OUT=/g/_Games/q3mod/promo/clips; mkdir -p $OUT
E=$((S+N)); A=$((S+X))
if [ "$X" -gt 0 ]; then
  OFF=$(python -c "print(($N-2*$X)/30.0)")
  FG="[0:v]$PRE,scale=$W:$H:flags=lanczos,setsar=1,split[a][b];[a]trim=start_frame=$A:end_frame=$E,setpts=PTS-STARTPTS,fps=30[m];[b]trim=start_frame=$S:end_frame=$A,setpts=PTS-STARTPTS,fps=30[h];[m][h]xfade=transition=fade:duration=$(python -c "print($X/30.0)"):offset=$OFF,format=yuv420p[v]"
else
  FG="[0:v]$PRE,scale=$W:$H:flags=lanczos,setsar=1,trim=start_frame=$S:end_frame=$E,setpts=PTS-STARTPTS,fps=30,format=yuv420p[v]"
fi
for CRF in ${CRFS:-24 26 28 30 32 34}; do
  "$FF/ffmpeg" -v error -y -i "$IN" -filter_complex "$FG" -map "[v]" -an -c:v libx264 -preset slow -crf $CRF -profile:v high -pix_fmt yuv420p -movflags +faststart "$OUT/$NAME.mp4" || exit 1
  KB=$(( $(stat -c %s "$OUT/$NAME.mp4") / 1024 ))
  echo "crf $CRF -> $KB KB"
  [ $KB -le $TGT ] && break
done
for Q in 3 4 5 6 8; do
  "$FF/ffmpeg" -v error -y -i "$OUT/$NAME.mp4" -frames:v 1 -q:v $Q "$OUT/$NAME.jpg"
  [ $(stat -c %s "$OUT/$NAME.jpg") -le 122000 ] && break
done
"$FF/ffprobe" -v error -show_entries format=duration:stream=width,height,nb_frames -of compact "$OUT/$NAME.mp4"
ls -la "$OUT/$NAME.mp4" "$OUT/$NAME.jpg"
