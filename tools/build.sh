#!/bin/bash
# Build engine + game code. Always deploys to the test install (dist_test/ + home_test/rearmed/);
# deploys to the player install (dist/ + home/rearmed/) only when the game isn't running, or with DEPLOY=1.
set -e
source "$(dirname "$0")/env.sh"
[ -z "$Q3NOLOCK" ] && q3lock
rm -f "$Q3MOD"/ioq3/build/qvm.dir/*/*.asm
cmake --build "$Q3MOD/ioq3/build" 2>&1 | grep -E "error|warning:|FAILED" || true
cmake --build "$Q3MOD/ioq3/build" >/dev/null   # re-run to surface failure status
R="$Q3MOD/ioq3/build/Release"
STAGE="$Q3MOD/build_pk3"
mkdir -p "$STAGE" "$Q3MOD/dist_test" "$Q3MOD/home_test/rearmed"
PK3_OUT="$STAGE" python "$Q3MOD/tools/pack.py"

deploy() {  # deploy <engine dir> <mod dir>
  cp "$R"/ioquake3.exe "$R"/ioq3ded.exe "$R"/renderer_opengl*.dll "$R"/SDL2.dll "$1/"
  cp "$STAGE"/*.pk3 "$2/"
}
deploy "$Q3MOD/dist_test" "$Q3MOD/home_test/rearmed"
# isolated test home: the preset cfgs, never the player's q3config.cfg
cp "$Q3MOD"/home/rearmed/autoexec.cfg "$Q3MOD"/home/rearmed/rearmed_*.cfg "$Q3MOD/home_test/rearmed/"

if [ "$DEPLOY" = 1 ] || ! tasklist 2>/dev/null | grep -qi "^ioquake3.exe"; then
  mkdir -p "$Q3MOD/dist" "$Q3MOD/home/rearmed"
  deploy "$Q3MOD/dist" "$Q3MOD/home/rearmed"
  echo "deployed to dist/ and home/rearmed/"
else
  echo "game is running: deployed to the test install only (DEPLOY=1 to force)"
fi
