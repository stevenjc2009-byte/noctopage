#!/usr/bin/env bash
# Host test runner. Run from Windows as:
#   MSYS_NO_PATHCONV=1 wsl -e bash /mnt/c/Users/steve/Documents/noctopage/tools/run_tests.sh
# (a script file, never `bash -lc`: that eats exit codes).
#
# 1. builds + runs every tests/test_*.c on the host gcc (tests/Makefile `run`);
# 2. compiles every pure module in src/core, src/lib/pure, ... listed in PURE_SRCS with
#    arm-vita-eabi-gcc -Wall -Wextra -Werror (compile only), unless ARM=0.
# Exits non-zero if either step fails.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 2

rc=0
ARM_FAIL_MARK="$(mktemp)"
trap 'rm -f "$ARM_FAIL_MARK"' EXIT

echo "== host tests =="
make -C tests run || rc=1

if [ "${ARM:-1}" != "0" ]; then
    ARM_CC="${ARM_CC:-$HOME/vitasdk/bin/arm-vita-eabi-gcc}"
    echo "== arm-vita-eabi compile check of pure modules =="
    if [ ! -x "$ARM_CC" ]; then
        echo "SKIP: $ARM_CC not found"
    else
        # Shared list here; each work package adds tests/arm_<pkg>.list with lines
        # "src/path.c [extra flags]" (# comments allowed).
        lists="$(ls tests/arm_*.list 2>/dev/null)"
        { printf '%s\n' src/core/strutil.c src/core/natsort.c src/core/fnv.c src/core/settings.c
          [ -n "$lists" ] && cat $lists; } | sed 's/#.*//' | while read -r f extra; do
            [ -z "$f" ] && continue
            if "$ARM_CC" -std=gnu11 -Wall -Wextra -Werror -O2 -Isrc -Ithird_party/mupdf/vita/include \
                   -I"$HOME/vitasdk/arm-vita-eabi/include/freetype2" $extra -c "$f" -o /dev/null; then
                echo "arm ok: $f"
            else
                echo "arm FAIL: $f"
                echo FAIL >> "$ARM_FAIL_MARK"
            fi
        done
        [ -s "$ARM_FAIL_MARK" ] && rc=1
    fi
fi

if [ $rc -eq 0 ]; then echo "ALL OK"; else echo "FAILED"; fi
exit $rc
