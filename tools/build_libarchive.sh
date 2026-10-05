#!/usr/bin/env bash
# tools/build_libarchive.sh — fetch, verify and build libarchive 3.8.9 for the PS Vita WITH
# liblzma (7z / CB7 / xz need it), zlib, bzip2 and zstd. Runs in WSL:
#
#   MSYS_NO_PATHCONV=1 wsl -e bash /mnt/c/.../noctopage/tools/build_libarchive.sh [--clean]
#
# Why this exists: the VitaSDK's own libarchive.a was built without liblzma, so on the Vita
# a .cb7 fails with "LZMA codec is unsupported". This builds the same source (same version,
# same SDK patch, same cmake flags as ~/vitapkgs/libarchive/VITABUILD) with LZMA switched on,
# into third_party/libarchive/vita/{include,lib}. ~/vitasdk is only read, never written.
# Re-running skips download/extract (marker file) and the build when libarchive.a is
# installed; --clean wipes build + install dirs first. Details: third_party/libarchive/README.md
set -euo pipefail

VERSION=3.8.9
URL="https://github.com/libarchive/libarchive/archive/v${VERSION}.tar.gz"
SHA256=744346f6bca195c8f894f847bb32a16e9bcae6002624a58fadc81e80f595b3cb
PATCH_SHA256=8d1de755127cee27b91f0cc2b820e6a84d4dec6d6ef413a73d059bdf1fc2105e
TARBALL_NAME="libarchive-${VERSION}.tar.gz"

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
L="$ROOT/third_party/libarchive"
SRC="$L/src"
BUILD="$L/build/vita"
OUT="$L/vita"
PATCH="$L/vita.patch"
MARKER="$SRC/.noctopage-verified-$SHA256"
CACHE="${LIBARCHIVE_CACHE:-$HOME/noct-libarchive-work}"
JOBS=$(nproc)

export VITASDK="${VITASDK:-$HOME/vitasdk}"
export PATH="$VITASDK/bin:$HOME/.local/bin:$PATH"
VSYS="$VITASDK/arm-vita-eabi"

die() { echo "build_libarchive: ERROR: $*" >&2; exit 1; }
log() { echo "build_libarchive: $*"; }

CLEAN=0
for a in "$@"; do
    case "$a" in
        --clean) CLEAN=1 ;;
        -h|--help) sed -n '2,14p' "$0"; exit 0 ;;
        *) die "unknown argument '$a' (expected --clean)" ;;
    esac
done

for t in cmake curl sha256sum tar patch arm-vita-eabi-gcc arm-vita-eabi-nm; do
    command -v "$t" >/dev/null || die "'$t' not found (cmake: apt install cmake, or pip install --user cmake)"
done
for f in "$VSYS/lib/liblzma.a" "$VSYS/lib/libz.a" "$VSYS/lib/libbz2.a" "$VSYS/lib/libzstd.a" \
         "$VSYS/include/lzma.h" "$VSYS/include/zstd.h" "$VSYS/include/bzlib.h" \
         "$VITASDK/share/vita.toolchain.cmake"; do
    [ -f "$f" ] || die "missing $f (VitaSDK incomplete)"
done
echo "$PATCH_SHA256  $PATCH" | sha256sum -c --status - || die "$PATCH does not match its pinned sha256"

if [ "$CLEAN" = 1 ]; then
    log "--clean: removing $BUILD and $OUT"
    rm -rf "$BUILD" "$OUT"
fi

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
    log "extracting into $SRC and applying vita.patch"
    rm -rf "$SRC"
    mkdir -p "$SRC"
    tar xzf "$tb" -C "$SRC" --strip-components=1
    (cd "$SRC" && patch -p1 < "$PATCH")
    touch "$MARKER"
}

build() {
    if [ -f "$OUT/lib/libarchive.a" ]; then
        log "already built: $OUT/lib/libarchive.a (use --clean to rebuild)"
        return
    fi
    rm -rf "$BUILD"
    mkdir -p "$BUILD"
    log "configuring"
    # Same flags as the SDK recipe, plus the codecs switched ON explicitly. Everything else
    # that could pull a host library in is OFF so nothing is picked up from the host.
    cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
        -DCMAKE_INSTALL_PREFIX="$OUT" \
        -DBUILD_SHARED_LIBS=OFF -DENABLE_TEST=OFF -DENABLE_INSTALL=ON \
        -DENABLE_ACL=OFF -DENABLE_XATTR=OFF -DENABLE_CAT=OFF -DENABLE_CPIO=OFF -DENABLE_TAR=OFF \
        -DENABLE_UNZIP=OFF -DENABLE_LIBXML2=OFF -DENABLE_EXPAT=OFF -DENABLE_ICONV=OFF \
        -DENABLE_OPENSSL=OFF -DENABLE_MBEDTLS=OFF -DENABLE_NETTLE=OFF -DENABLE_LIBB2=OFF \
        -DENABLE_LZ4=OFF -DENABLE_LZO=OFF -DENABLE_PCREPOSIX=OFF -DENABLE_PCRE2POSIX=OFF \
        -DENABLE_ZLIB=ON -DENABLE_BZip2=ON -DENABLE_LZMA=ON -DENABLE_ZSTD=ON \
        -DLIBLZMA_INCLUDE_DIR="$VSYS/include" -DLIBLZMA_LIBRARY="$VSYS/lib/liblzma.a" \
        -DZSTD_INCLUDE_DIR="$VSYS/include" -DZSTD_LIBRARY="$VSYS/lib/libzstd.a" \
        -DCMAKE_C_FLAGS="-std=gnu11 -Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=incompatible-pointer-types" \
        2>&1 | tee "$BUILD/configure.log"
    grep -q "HAVE_LIBLZMA\|LZMA" "$BUILD/configure.log" || die "configure never mentioned LZMA"
    grep -E "^#define HAVE_LZMA_H 1" "$BUILD/config.h" >/dev/null || die "config.h: HAVE_LZMA_H not set; liblzma was not found"
    grep -E "^#define HAVE_LIBLZMA 1" "$BUILD/config.h" >/dev/null || die "config.h: HAVE_LIBLZMA not set"
    log "building (-j$JOBS)"
    cmake --build "$BUILD" -j"$JOBS" --target archive_static
    log "installing into $OUT"
    cmake --install "$BUILD" --component Unspecified >/dev/null 2>&1 || cmake --install "$BUILD"
}

verify() {
    local lib="$OUT/lib/libarchive.a" tmp n
    [ -f "$lib" ] || die "$lib was not produced"
    [ -f "$OUT/include/archive.h" ] || die "$OUT/include/archive.h missing"
    tmp=$(mktemp -d)
    (cd "$tmp" && arm-vita-eabi-ar x "$lib" archive_read_support_format_7zip.c.obj 2>/dev/null \
        || arm-vita-eabi-ar x "$lib" archive_read_support_format_7zip.o)
    n=$(arm-vita-eabi-nm "$tmp"/archive_read_support_format_7zip.* | grep -c ' U lzma_' || true)
    log "archive_read_support_format_7zip: $n undefined lzma_* symbols"
    arm-vita-eabi-nm "$tmp"/archive_read_support_format_7zip.* | grep ' U lzma_' | head -5
    rm -rf "$tmp"
    [ "$n" -gt 0 ] || die "7zip reader does not reference liblzma: LZMA is still missing"
    log "OK: $lib has LZMA support"
}

fetch
build
verify
