#!/usr/bin/env bash
# tools/mupdf_smoke/run_smoke.sh — build + run the MuPDF smoke test on the host,
# cross-link it for the Vita (vita-elf-create must accept it), and prove
# libmupdf-third.a (Vita) defines no jpeg_/FT_/hb_/inflate symbols.
# Run in WSL after tools/build_mupdf.sh:
#   MSYS_NO_PATHCONV=1 wsl -e bash /mnt/c/.../noctopage/tools/mupdf_smoke/run_smoke.sh
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
M="$ROOT/third_party/mupdf"
OUT="$ROOT/third_party/mupdf/build/smoke"
export VITASDK="${VITASDK:-$HOME/vitasdk}"
export PATH="$VITASDK/bin:$PATH"
mkdir -p "$OUT"

echo "== host: compile + run"
gcc -std=gnu11 -O2 -Wall -Wextra -Werror -I"$M/host/include" \
    -o "$OUT/smoke_host" "$HERE/smoke.c" \
    -L"$M/host/lib" -lmupdf -lmupdf-third -lm
"$OUT/smoke_host"

echo "== vita: compile + link + vita-elf-create"
arm-vita-eabi-gcc -std=gnu11 -O2 -Wall -Wextra -Werror \
    -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -I"$M/vita/include" \
    -Wl,-q -o "$OUT/smoke_vita.elf" "$HERE/smoke.c" \
    -L"$M/vita/lib" -lmupdf -lmupdf-third \
    -lharfbuzz -lfreetype -lpng16 -lbz2 -ljpeg -lz -lpthread -lm -lc \
    -lSceLibKernel_stub
vita-elf-create "$OUT/smoke_vita.elf" "$OUT/smoke_vita.velf"
ls -l "$OUT/smoke_vita.elf" "$OUT/smoke_vita.velf"
arm-vita-eabi-size "$OUT/smoke_vita.elf"

echo "== vita: symbols DEFINED in libmupdf-third.a / libmupdf.a matching ^(jpeg_|FT_|hb_|inflate|deflate)"
# Global (linkable) definitions only: file-local statics cannot clash.
defs() {
    arm-vita-eabi-nm -g --defined-only "$1" 2>/dev/null \
        | awk 'NF==3 {print $3}' | grep -E '^(jpeg_|jinit_|FT_|hb_|inflate|deflate)' || true
}
count_defs() { defs "$1" | grep -c . || true; }
# Control arm: the same check must go red on the SDK's own libraries.
for lib in libjpeg.a libfreetype.a libharfbuzz.a libz.a; do
    n=$(count_defs "$VITASDK/arm-vita-eabi/lib/$lib")
    echo "control SDK $lib: $n"
    [ "$n" -gt 0 ] || { echo "CONTROL FAILED: check cannot see symbols in $lib"; exit 1; }
done
bad=0
for lib in libmupdf-third.a libmupdf.a; do
    n=$(count_defs "$M/vita/lib/$lib")
    echo "$lib: $n"
    [ "$n" = 0 ] || { bad=1; defs "$M/vita/lib/$lib" | sort -u | head -20; }
done
[ "$bad" = 0 ] || { echo "DUPLICATE-SYMBOL CHECK FAILED"; exit 1; }
echo "SMOKE ALL OK"
