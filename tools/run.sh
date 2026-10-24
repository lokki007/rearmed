#!/bin/bash
# Launch the mod. Extra args are passed to the engine, e.g. ./run.sh +devmap q3dm17
source "$(dirname "$0")/env.sh"
cd "${Q3DIST:-$Q3MOD/dist}"
exec ./ioquake3.exe +set fs_basepath "$(cygpath -w "$Q3_BASE")" +set fs_homepath "$(cygpath -w "${Q3HOME:-$Q3MOD/home}")" \
  +set fs_game rearmed +set cl_renderer opengl2 +set ui_cdkeychecked 1 +set com_introplayed 1 +set cg_devLog 1 "$@"
