#!/bin/bash
# Public source mirror: publish_source.sh [message]
# Exports HEAD of this repo into the mirror checkout ($PUB, default G:\_Games\rearmed-public), leaves out
# what is not ours to publish or is maintainer-only (the remix .map sources decompiled from id maps,
# internal worker notes), commits as the GitHub no-reply identity and pushes to github.com/lokki007/rearmed.
set -e
SRC=/g/_Games/q3mod
PUB=${PUB:-/g/_Games/rearmed-public}
MSG=${1:-"Sync with $(git -C "$SRC" rev-parse --short HEAD)"}
EXCLUDE=(maps/src maps/scripts docs/WORKER_RULES.md)

[ -d "$PUB/.git" ] || { echo "no mirror at $PUB (git clone https://github.com/lokki007/rearmed $PUB)"; exit 1; }
find "$PUB" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +
git -C "$SRC" archive HEAD | tar -x -C "$PUB"
for p in "${EXCLUDE[@]}"; do rm -rf "$PUB/$p"; done
cp "$PUB/ioq3/COPYING.txt" "$PUB/LICENSE"		# GitHub reads the license from the root
cd "$PUB"
git add -A
if git diff --cached --quiet; then echo "mirror already up to date"; exit 0; fi
git -c user.name=lokki007 -c user.email=17731656+lokki007@users.noreply.github.com commit -q -m "$MSG"
git push -q origin HEAD
git log --oneline -1
