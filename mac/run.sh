#!/bin/sh
# Starts Pip's brain (screen watcher + AI) and the on-screen overlay.
cd "$(dirname "$0")"
[ -d overlay/Pip.app ] || sh overlay/build.sh
open -g overlay/Pip.app
exec python3 brain.py "$@"
