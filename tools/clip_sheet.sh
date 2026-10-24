#!/bin/bash
# clip_sheet.sh <video> <every_n> <cols> <rows> <tile_w> [start_frame] -> scratch/f/sheet_<name>.jpg (frame numbers burned in)
FF=/c/Users/Ian/AppData/Local/Microsoft/WinGet/Packages/Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe/ffmpeg-8.1.1-full_build/bin
S=/c/Users/Ian/AppData/Local/Temp/claude/G---Games/e0694bae-3bd8-468a-a897-5661bdc12904/scratchpad
mkdir -p $S/f; B=$(basename "$1"); B=${B%.*}; IN=$(cd "$(dirname "$1")"; pwd)/$(basename "$1")
cd $S
"$FF/ffmpeg" -v error -y -i "$IN" -vf "select='gte(n\,${6:-0})*not(mod(n-${6:-0}\,$2))',scale=$5:-2,drawtext=fontfile=arial.ttf:text='%{n}':x=4:y=4:fontsize=22:fontcolor=yellow:box=1:boxcolor=black,tile=$3x$4" -fps_mode passthrough -frames:v 1 -update 1 "f/sheet_$B.jpg"
echo "$S/f/sheet_$B.jpg"
