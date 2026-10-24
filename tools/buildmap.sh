#!/bin/bash
# Compile a .map into maps_build/maps/<name>.bsp + .aas
# usage: buildmap.sh <file.map> <name> [fast]
set -e
source "$(dirname "$0")/env.sh"
SRC="$1"; NAME="$2"; MODE="${3:-final}"
OUT="$Q3MOD/maps_build/maps"
WORK="$Q3MOD/maps/work"
mkdir -p "$OUT" "$WORK"
rm -rf "$WORK/$NAME" "$Q3MOD/maps/scripts/q3map2_$NAME.shader"
cp "$SRC" "$WORK/$NAME.map"
M="$(cygpath -w "$WORK/$NAME.map")"
B="$(cygpath -w "$WORK/$NAME.bsp")"
Q="$NR/q3map2.exe"
BASE=(-game quake3 -fs_basepath "$(cygpath -w "$Q3_BASE")" -fs_homepath "$(cygpath -w "$Q3MOD/home")" -fs_game rearmed)
LOG="$WORK/$NAME.log"
: > "$LOG"

echo "[$NAME] bsp"
"$Q" "${BASE[@]}" -meta -patchmeta "$M" >> "$LOG" 2>&1
echo "[$NAME] vis"
"$Q" "${BASE[@]}" -vis -saveprt "$M" >> "$LOG" 2>&1
echo "[$NAME] light ($MODE)"
if [ "$MODE" = fast ]; then
  "$Q" "${BASE[@]}" -light -fast -deluxe -patchshadows "$M" >> "$LOG" 2>&1
else
  "$Q" "${BASE[@]}" -light -fast -bounce 2 -samples 3 -filter -deluxe -patchshadows "$M" >> "$LOG" 2>&1
fi
echo "[$NAME] aas"
"$NR/mbspc.exe" -forcesidesvisible -optimize -bsp2aas "$B" >> "$LOG" 2>&1
cp "$WORK/$NAME.bsp" "$WORK/$NAME.aas" "$OUT/"
# light styles: q3map2 emits custom shaders + external lightmaps that must ship with the bsp
rm -rf "$OUT/$NAME" "$Q3MOD/maps_build/scripts/q3map2_$NAME.shader"
[ -d "$WORK/$NAME" ] && cp -r "$WORK/$NAME" "$OUT/$NAME"
if [ -f "$Q3MOD/maps/scripts/q3map2_$NAME.shader" ]; then
  mkdir -p "$Q3MOD/maps_build/scripts"
  cp "$Q3MOD/maps/scripts/q3map2_$NAME.shader" "$Q3MOD/maps_build/scripts/"
fi
grep -iE "leaked|error" "$LOG" | grep -v "^$" | head -5 || true
ls -la "$OUT/$NAME.bsp" "$OUT/$NAME.aas"
