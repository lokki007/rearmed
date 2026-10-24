#!/bin/bash
# clip_frames.sh <avi|mp4> <every_n> [scale_w] -> scratch frames dir (prints list)
FF=/c/Users/Ian/AppData/Local/Microsoft/WinGet/Packages/Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe/ffmpeg-8.1.1-full_build/bin
S=/c/Users/Ian/AppData/Local/Temp/claude/G---Games/e0694bae-3bd8-468a-a897-5661bdc12904/scratchpad/f
mkdir -p $S; rm -f $S/*.jpg
B=$(basename "$1"); B=${B%.*}
"$FF/ffmpeg" -v error -i "$1" -vf "select='not(mod(n\,$2))',scale=${3:-1280}:-2" -fps_mode passthrough -frame_pts 1 -q:v 4 "$S/${B}_%03d.jpg"
ls $S
