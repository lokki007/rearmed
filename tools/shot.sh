#!/bin/bash
# Automated screenshot: shot.sh <map> [W H] [extra engine args...]  -> prints screenshot path
source "$(dirname "$0")/env.sh"
[ -z "$Q3NOLOCK" ] && q3lock
MAP=${1:-q3dm17}; W=${2:-1920}; H=${3:-540}; shift 3 2>/dev/null
# tests +set archived switches; drop them so every run starts from the defaults
sed -i -E '/^seta (g_(doubleJump|wallJump|wallRun|airDash|jumpAssist|altFire|chargedAlt|elements|offhandHook|shotgunKick|chaingunSpin|ricochet|stickyGrenades|singularity|bfgLasers|uppercut|headshots|airshots|killstreaks|bounty|medals|rearmPowerups|mutators|weaponRandom|rearmMode|rearmItems|style|perks|slide|slam|props|corpsePhysics|slowmo|mutatorInterval|mutatorDuration)|bot_rearm|bot_personality|bot_rearmTricks|bot_useProps|bot_avoidDanger|g_propCount|g_propRespawn|cg_draw2D|cg_fov|cg_thirdPerson|cg_drawGun|r_postProcess|r_ssao|r_sunShadows) /d' "$Q3MOD/home_test/rearmed/q3config.cfg" 2>/dev/null
SS="$Q3MOD/home_test/rearmed/screenshots"; rm -f "$SS"/*.jpg "$Q3MOD/home_test/rearmed/ioq3.pid"  # stale pid file = blocking safe-mode dialog
Q3DIST="$Q3MOD/dist_test" Q3HOME="$Q3MOD/home_test" timeout ${SHOT_TIMEOUT:-150} "$Q3MOD/tools/run.sh" +set r_fullscreen 0 +set r_mode -1 +set r_customwidth $W +set r_customheight $H \
  +set cg_drawFPS 1 "$@" +devmap $MAP +exec ${SHOT_CFG:-shot.cfg} > "${SHOT_LOG:-$Q3MOD/home/run.log}" 2>&1
grep -i -E "^(error|warning)|ERROR:" "${SHOT_LOG:-$Q3MOD/home/run.log}" | head
if [ -n "$SHOT_OUT" ]; then  # copy out before the lock is released (parallel workers)
  mkdir -p "$SHOT_OUT"; rm -f "$SHOT_OUT"/*.jpg; cp "$SS"/*.jpg "$SHOT_OUT"/ 2>/dev/null; ls "$SHOT_OUT"/*.jpg
  cp "$Q3MOD/home_test/rearmed/games.log" "$SHOT_OUT/games.log" 2>/dev/null
else
  ls "$SS"/*.jpg
fi
