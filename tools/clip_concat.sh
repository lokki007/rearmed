#!/bin/bash
# clip_concat.sh <out.mkv> <W> <H> <avi>:<start>:<end> ... -> hard-cut montage, lossless (x264 qp 0), 30 fps
FF=/c/Users/Ian/AppData/Local/Microsoft/WinGet/Packages/Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe/ffmpeg-8.1.1-full_build/bin
OUT=$1; W=$2; H=$3; shift 3
IN=""; FG=""; CAT=""; i=0
for seg in "$@"; do
  f=${seg%%:*}; r=${seg#*:}; s=${r%%:*}; e=${r#*:}
  IN="$IN -i $f"
  FG="$FG[$i:v]trim=start_frame=$s:end_frame=$e,setpts=PTS-STARTPTS,fps=30,scale=$W:$H:flags=lanczos,setsar=1[v$i];"
  CAT="$CAT[v$i]"; i=$((i+1))
done
"$FF/ffmpeg" -v error -y $IN -filter_complex "${FG}${CAT}concat=n=$i:v=1:a=0[v]" -map "[v]" -an -c:v libx264 -qp 0 -preset ultrafast -pix_fmt yuv444p "$OUT"
"$FF/ffprobe" -v error -count_frames -show_entries stream=nb_read_frames -of compact "$OUT"
