# MuPDF 1.26.10 (vendored build)

| | |
|---|---|
| Version | 1.26.10 |
| Source | https://mupdf.com/downloads/archive/mupdf-1.26.10-source.tar.gz (307 → `casper.mupdf.com`, same path) |
| sha256 (measured 2026-10-05, 64,530,548 bytes) | `1653f35bd8fbd970f05523efdc7f86e41e9728e2564a3295296e03cf59a51437` |
| Licence | AGPL-3.0-or-later, © Artifex Software, Inc. (`src/COPYING`). Noctopage must ship under AGPL-compatible terms with its source. Bundled thirdparty code keeps its own licences (`src/thirdparty/*/`), fonts: OFL (URW base-14, Charis SIL, Noto), Apache-2.0 (Droid Sans Fallback). |

## Layout

```
third_party/mupdf/
  src/              extracted tarball (git-ignored; created by tools/build_mupdf.sh)
  build/{host,vita} object dirs (git-ignored)
  host/lib/         libmupdf.a libmupdf-third.a   (gcc, everything in-tree)
  host/include/     public headers, config.h with the build config baked in
  vita/lib/         libmupdf.a libmupdf-third.a   (arm-vita-eabi)
  vita/include/     public headers, config.h with the build config baked in
  vita_compat.h     force-included into the Vita build only (timegm shim)
```

Build: `MSYS_NO_PATHCONV=1 wsl -e bash /mnt/c/Users/steve/Documents/noctopage/tools/build_mupdf.sh [--clean] [host] [vita]`.
Re-runs skip download/extract (marker `src/.noctopage-verified-<sha256>`) and skip any
target whose libs are installed; `--clean` wipes that target's `build/` + install dir.
The tarball is cached in `$HOME/noct-mupdf-work` (override: `MUPDF_CACHE`).

Smoke test: `tools/mupdf_smoke/run_smoke.sh` (host run, Vita link + `vita-elf-create`,
duplicate-symbol check with a control arm on the SDK libs).

## Build flags

Both targets (make variables): `build=release` (`-O2 -DNDEBUG`, plus MuPDF's
`-ffunction-sections -fdata-sections`) `mujs=no brotli=no extract=no barcode=no
tofu_cjk_lang=yes HAVE_GLUT=no HAVE_X11=no HAVE_LIBCRYPTO=no HAVE_OBJCOPY=yes`,
target `libs` only, and
`XCFLAGS=-DFZ_ENABLE_JS=0 -DFZ_ENABLE_BROTLI=0 -DFZ_ENABLE_DOCX_OUTPUT=0 -DFZ_ENABLE_BARCODE=0 -DTOFU_CJK_LANG`.

Those five defines are also prepended to the installed `include/mupdf/fitz/config.h`
(both targets), so app code needs **no** `-D` flags — only `-I`. Do not pass
conflicting `FZ_ENABLE_*` / `TOFU_*` defines to app compiles (`FZ_ENABLE_ICC`
changes headers; it is left at the default 1).

Host: `make -C src libs OUT=build/host <vars above>` — all thirdparty in-tree
(freetype, harfbuzz, zlib, libjpeg, jbig2dec, openjpeg, lcms2mt, gumbo).

Vita: same plus
```
OS=vita CC=arm-vita-eabi-gcc CXX=arm-vita-eabi-g++ AR=arm-vita-eabi-ar
LD=arm-vita-eabi-ld RANLIB=arm-vita-eabi-ranlib HAVE_PTHREAD=no
USE_SYSTEM_FREETYPE=yes USE_SYSTEM_HARFBUZZ=yes USE_SYSTEM_ZLIB=yes USE_SYSTEM_LIBJPEG=yes
SYS_FREETYPE_CFLAGS=-I$VITASDK/arm-vita-eabi/include/freetype2
SYS_HARFBUZZ_CFLAGS=-I$VITASDK/arm-vita-eabi/include/harfbuzz
XCFLAGS+= -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -include third_party/mupdf/vita_compat.h
PKG_CONFIG_LIBDIR=/nonexistent PKG_CONFIG_PATH=   (keeps host .pc files out of the cross build)
```
freetype, harfbuzz, zlib and libjpeg(-turbo) come from the SDK (the app links them for
vita2d) — `libmupdf-third.a` holds only jbig2dec, openjpeg, lcms2mt, gumbo. Fonts are
embedded with `arm-vita-eabi-ld -r -b binary` (`HAVE_OBJCOPY=yes`), so no hexdump
generation step is needed; the ICC/CMap/CSS generated headers ship in the tarball.

`vita_compat.h`: VitaSDK newlib has no `timegm()` (used by `pdf_parse_date`); the
header supplies a `static inline` replacement (checked against glibc `timegm` on
211,200 dates, 0 mismatches).

## Disabled, and why

| What | Why |
|---|---|
| MuJS (`FZ_ENABLE_JS=0`) | PDF form JavaScript; a reader never runs it. |
| Brotli (`FZ_ENABLE_BROTLI=0`) | Only the rare PDF `BrotliDecode` filter; saves encoder+decoder. |
| extract (`FZ_ENABLE_DOCX_OUTPUT=0`) | DOCX/ODT *output* only. |
| barcode, tesseract/leptonica | Not built (MuPDF defaults); not needed. |
| libarchive (`HAVE_LIBARCHIVE`) | Off: the app reads CBR/CB7 itself through libarchive. |
| Source Han Serif (`TOFU_CJK_LANG`) | 24.8 MB. CJK falls back to Droid Sans Fallback Full (5.07 MB, covers Japanese). |

Kept: PDF, XPS, SVG, CBZ, images, EPUB, MOBI, FB2, HTML/XHTML, TXT, Office; ICC (lcms2),
JPX, JBIG2; base-14 (URW), Charis SIL, all Noto (non-CJK) fonts. Setting
`tofu_noto`/`-DTOFU_NOTO` would save ~7.9 MiB more of linked font data.

## Measured (2026-10-05)

| File | Bytes |
|---|---|
| host/lib/libmupdf.a | 30,518,468 |
| host/lib/libmupdf-third.a | 7,844,600 |
| vita/lib/libmupdf.a | 27,849,564 |
| vita/lib/libmupdf-third.a | 1,471,686 |
| smoke ELF (Vita, static, unstripped) | 21,531,488 (text 5,573,048 / data 14,245,608 / bss 311,780) |
| smoke .velf | 22,721,986 |

Font data linked into the Vita ELF: droid 4.84 MiB, noto 7.88 MiB, urw 0.58 MiB,
sil 0.18 MiB — 13.49 MiB, 176 fonts.

Vita link line (from `tools/mupdf_smoke/run_smoke.sh`):
```
arm-vita-eabi-gcc ... -I third_party/mupdf/vita/include ... -Wl,-q
  -L third_party/mupdf/vita/lib -lmupdf -lmupdf-third
  -lharfbuzz -lfreetype -lpng16 -lbz2 -ljpeg -lz -lpthread -lm -lc -lSceLibKernel_stub
```
`-lpthread` is required (lcms2mt and SDK harfbuzz use pthread mutexes).
Host: `gcc ... -I third_party/mupdf/host/include ... -L third_party/mupdf/host/lib -lmupdf -lmupdf-third -lm`.
