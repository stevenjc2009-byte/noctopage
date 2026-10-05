#!/bin/bash
# Builds the Vita VPK inside WSL. Run from Windows as:
#   MSYS_NO_PATHCONV=1 wsl -e bash /mnt/c/Users/steve/Documents/noctopage/tools/build_vita.sh [make args]
# (script file, never `bash -lc`: that eats exit codes.)
set -e
export VITASDK="${VITASDK:-$HOME/vitasdk}"
export PATH="$VITASDK/bin:$PATH"
cd "$(dirname "$0")/.."
make -j"$(nproc)" "$@"
ls -l build/Noctopage.vpk build/eboot.bin
