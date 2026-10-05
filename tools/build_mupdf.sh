#!/usr/bin/env bash
# tools/build_mupdf.sh — fetch, verify and build MuPDF 1.26.10 as static libraries
# for the host (gcc, in-tree thirdparty) and the PS Vita (VitaSDK, SDK freetype /
# harfbuzz / zlib / libjpeg). Runs in WSL:
#
#   MSYS_NO_PATHCONV=1 wsl -e bash /mnt/c/.../noctopage/tools/build_mupdf.sh [--clean] [host] [vita]
#
# No target given = both. Re-running skips the download/extract when the verified
# tree is present and skips a target whose libs are already installed; --clean
# wipes the selected targets' build + install dirs and rebuilds them.
# See third_party/mupdf/README.md for the flags and the reasons behind them.
set -euo pipefail

VERSION=1.26.10
URL="https://mupdf.com/downloads/archive/mupdf-${VERSION}-source.tar.gz"
SHA256=1653f35bd8fbd970f05523efdc7f86e41e9728e2564a3295296e03cf59a51437
TARBALL_NAME="mupdf-${VERSION}-source.tar.gz"

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
M="$ROOT/third_party/mupdf"
SRC="$M/src"
MARKER="$SRC/.noctopage-verified-$SHA256"
CACHE="${MUPDF_CACHE:-$HOME/noct-mupdf-work}"
JOBS=$(nproc)

export VITASDK="${VITASDK:-$HOME/vitasdk}"
VSYS="$VITASDK/arm-vita-eabi"

# Feature switches shared by both targets. CONFIG_DEFS is what ends up in CFLAGS
# for these switches; it is also baked into the installed config.h so app code
# sees exactly the configuration the library was compiled with.
FEATURE_VARS=(build=release mujs=no brotli=no extract=no barcode=no tofu_cjk_lang=yes
              HAVE_GLUT=no HAVE_X11=no HAVE_LIBCRYPTO=no HAVE_OBJCOPY=yes)
CONFIG_DEFS=(FZ_ENABLE_JS=0 FZ_ENABLE_BROTLI=0 FZ_ENABLE_DOCX_OUTPUT=0 FZ_ENABLE_BARCODE=0 TOFU_CJK_LANG)

die() { echo "build_mupdf: ERROR: $*" >&2; exit 1; }
log() { echo "build_mupdf: $*"; }

CLEAN=0
TARGETS=()
for a in "$@"; do
    case "$a" in
        --clean) CLEAN=1 ;;
        host|vita) TARGETS+=("$a") ;;
        -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
        *) die "unknown argument '$a' (expected --clean, host, vita)" ;;
    esac
done
[ ${#TARGETS[@]} -eq 0 ] && TARGETS=(host vita)

fetch() {
    if [ -f "$MARKER" ]; then
        log "source tree present and verified ($SRC)"
        return
    fi
    mkdir -p "$CACHE"
    local tb="$CACHE/$TARBALL_NAME"
    if [ ! -f "$tb" ] || ! echo "$SHA256  $tb" | sha256sum -c --status -; then
        log "downloading $URL"
        rm -f "$tb.part"
        curl -fL --retry 3 -o "$tb.part" "$URL" || die "download failed: $URL"
        mv "$tb.part" "$tb"
    fi
    echo "$SHA256  $tb" | sha256sum -c - || die "sha256 mismatch for $tb (expected $SHA256)"
    log "extracting into $SRC"
    rm -rf "$SRC"
    mkdir -p "$SRC"
    tar xzf "$tb" -C "$SRC" --strip-components=1
    touch "$MARKER"
}

# Copy the public headers and prepend the build configuration to config.h.
install_headers() {
    local dst="$1/include"
    rm -rf "$dst"
    mkdir -p "$dst"
    cp -r "$SRC/include/mupdf" "$dst/"
    local cfg="$dst/mupdf/fitz/config.h" tmp="$dst/mupdf/fitz/config.h.tmp"
    {
        echo "/* Noctopage: configuration MuPDF $VERSION was built with (tools/build_mupdf.sh)."
        echo "   Do not pass conflicting FZ_ENABLE_* / TOFU_* defines to app compiles. */"
        local d
        for d in "${CONFIG_DEFS[@]}"; do
            local name="${d%%=*}" val=""
            [ "$d" != "$name" ] && val="${d#*=}"
            echo "#ifndef $name"
            echo "#define $name $val"
            echo "#endif"
        done
        echo
        cat "$cfg"
    } > "$tmp"
    mv "$tmp" "$cfg"
}

xcflags_defs() {
    local d out=""
    for d in "${CONFIG_DEFS[@]}"; do out="$out -D$d"; done
    echo "$out"
}

build_target() {
    local t="$1"
    local out="$M/build/$t" inst="$M/$t"
    if [ "$CLEAN" = 1 ]; then
        log "[$t] --clean: removing $out and $inst"
        rm -rf "$out" "$inst"
    fi
    if [ -f "$inst/lib/libmupdf.a" ] && [ -f "$inst/lib/libmupdf-third.a" ] && [ -f "$inst/include/mupdf/fitz.h" ]; then
        log "[$t] already built ($inst/lib); pass --clean to rebuild"
        return
    fi
    local defs; defs=$(xcflags_defs)
    log "[$t] building with $JOBS jobs into $out"
    if [ "$t" = host ]; then
        # Host: everything in-tree. Feature set mirrors the Vita build.
        make -C "$SRC" -j"$JOBS" libs OUT="$out" "${FEATURE_VARS[@]}" \
            XCFLAGS="$defs"
    else
        [ -x "$VITASDK/bin/arm-vita-eabi-gcc" ] || die "VitaSDK not found at $VITASDK"
        export PATH="$VITASDK/bin:$PATH"
        # Hide the host's .pc files so nothing host-side leaks into the cross build;
        # the SDK libraries are named explicitly below.
        PKG_CONFIG_LIBDIR=/nonexistent PKG_CONFIG_PATH= \
        make -C "$SRC" -j"$JOBS" libs OUT="$out" OS=vita "${FEATURE_VARS[@]}" \
            CC=arm-vita-eabi-gcc CXX=arm-vita-eabi-g++ AR=arm-vita-eabi-ar \
            LD=arm-vita-eabi-ld RANLIB=arm-vita-eabi-ranlib \
            HAVE_PTHREAD=no \
            USE_SYSTEM_FREETYPE=yes USE_SYSTEM_HARFBUZZ=yes \
            USE_SYSTEM_ZLIB=yes USE_SYSTEM_LIBJPEG=yes \
            SYS_FREETYPE_CFLAGS="-I$VSYS/include/freetype2" SYS_FREETYPE_LIBS=-lfreetype \
            SYS_HARFBUZZ_CFLAGS="-I$VSYS/include/harfbuzz" SYS_HARFBUZZ_LIBS=-lharfbuzz \
            SYS_ZLIB_CFLAGS= SYS_ZLIB_LIBS=-lz \
            SYS_LIBJPEG_CFLAGS= SYS_LIBJPEG_LIBS=-ljpeg \
            XCFLAGS="-mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -include $M/vita_compat.h $defs"
    fi
    mkdir -p "$inst/lib"
    cp "$out/libmupdf.a" "$out/libmupdf-third.a" "$inst/lib/"
    install_headers "$inst"
    log "[$t] installed:"
    ls -l "$inst/lib"
}

fetch
for t in "${TARGETS[@]}"; do
    build_target "$t"
done
log "done"
