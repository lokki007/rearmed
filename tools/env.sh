# source this: portable toolchain for the Q3 mod
export TC=/g/_Games/_toolchain
export PATH="$TC/mingw64/bin:$TC/cmake-4.4.3-windows-x86_64/bin:$TC/ninja:$PATH"
export Q3_BASE="/h/SteamLibrary/steamapps/common/Quake 3 Arena"
export Q3MOD=/g/_Games/q3mod
export NR="/g/_Games/_toolchain/netradiant"
# q3lock: serialize builds and test runs between parallel workers (released on exit)
q3lock() {
  local lock="$Q3MOD/.q3lock" waited=0
  until mkdir "$lock" 2>/dev/null; do
    # a lock older than 20 minutes is stale (killed worker)
    if [ -n "$(find "$lock" -maxdepth 0 -mmin +20 2>/dev/null)" ]; then rmdir "$lock" 2>/dev/null; fi
    [ $waited = 0 ] && echo "waiting for the build/test lock..." >&2
    waited=1; sleep 2
  done
  echo "$$ $(basename "$0")" > "$lock/owner"
  trap 'rm -rf "$Q3MOD/.q3lock"' EXIT
}
