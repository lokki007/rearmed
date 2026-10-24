#!/bin/bash
# Regenerate every REARMED v4 model/texture/shader from scratch (deterministic):
#   bash tools/blender/build_all.sh            -> assets/models/rearmed, assets/gfx/rearmed/gore, assets/scripts/rearmed_v4.shader
#   bash tools/blender/build_all.sh barrel     -> only some models (barrel crate tank gibs head meteor debris loot)
# Then: python tools/md3.py --all (validate), bash tools/build.sh (pack into the pk3).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
BLENDER="${BLENDER:-/g/_Games/_toolchain/blender-5.2.2-windows-x64/blender.exe}"
python "$HERE/make_2d.py"
"$BLENDER" --background --factory-startup --python "$HERE/make_models.py" -- "$@" 2>&1 | grep -v "^$\|HIPEW\|Info: Baking\|  bake " || true
# DESCENT v2 loot (gems, vacuum magnet, chests + assets/scripts/rearmed_loot.shader): all, or "loot"
if [ $# = 0 ] || [[ " $* " == *" loot "* ]]; then
  "$BLENDER" --background --factory-startup --python "$HERE/make_loot.py" 2>&1 | grep -i "wrote\|tris\|error" || true
fi
# DESCENT round 3 secret stash (strongbox + lid + assets/scripts/rearmed_secrets.shader): all, or "stash"
if [ $# = 0 ] || [[ " $* " == *" stash "* ]]; then
  "$BLENDER" --background --factory-startup --python "$HERE/make_stash.py" 2>&1 | grep -i "wrote\|tris\|error" || true
fi
# round 4 enemy types: the shield bearer's riot shield (+ assets/scripts/rearmed_types_models.shader): all, or "shield"
if [ $# = 0 ] || [[ " $* " == *" shield "* ]]; then
  "$BLENDER" --background --factory-startup --python "$HERE/make_shield.py" 2>&1 | grep -i "wrote\|tris\|error" || true
fi
python "$HERE/make_shader.py"
python "$HERE/../md3.py" --all
if [ -n "$PREVIEW" ]; then  # PREVIEW=out.png renders a contact sheet of every model
  "$BLENDER" --background --factory-startup --python "$HERE/preview.py" -- "$PREVIEW" 300 2>&1 | grep -i "wrote\|error:" || true
fi
