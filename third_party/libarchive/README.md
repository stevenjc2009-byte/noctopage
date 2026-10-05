# libarchive 3.8.9 for the Vita, with liblzma (vendored build)

| | |
|---|---|
| Version | 3.8.9 (the version the VitaSDK ships, so headers and behaviour match) |
| Source | https://github.com/libarchive/libarchive/archive/v3.8.9.tar.gz |
| sha256 | `744346f6bca195c8f894f847bb32a16e9bcae6002624a58fadc81e80f595b3cb` (taken from `~/vitapkgs/libarchive/VITABUILD`; the script refuses a tarball that does not match) |
| Patch | `vita.patch` = `~/vitapkgs/libarchive/libarchive.patch` unchanged, sha256 `8d1de755127cee27b91f0cc2b820e6a84d4dec6d6ef413a73d059bdf1fc2105e` |
| Licence | New BSD (2-clause and others, see `src/COPYING` once fetched) |

## Why

The VitaSDK's `libarchive.a` was built without liblzma (`libarchive.pc` has
`Libs.private: -lz -lbz2 -lzstd`). On the Vita a `.cb7` / `.7z` then fails with
"corrupt or truncated archive: LZMA codec is unsupported". Host tests pass because the host
libarchive has LZMA. This build is the SDK recipe with `-DENABLE_LZMA=ON`. `~/vitasdk` is
read, never modified.

## Layout

```
third_party/libarchive/
  vita.patch        SDK patch (no man install, __vita__ stubs for the program filter, timezone)
  src/              extracted + patched tarball (git-ignored; made by the script)
  build/vita/       cmake build dir (git-ignored)
  vita/include/     archive.h archive_entry.h
  vita/lib/         libarchive.a (+ pkgconfig)
```

Build: `MSYS_NO_PATHCONV=1 wsl -e bash /mnt/c/Users/steve/Documents/noctopage/tools/build_libarchive.sh [--clean]`
The tarball is cached in `$HOME/noct-libarchive-work` (override `LIBARCHIVE_CACHE`); re-runs skip
download/extract and skip the build when `vita/lib/libarchive.a` exists. Needs `cmake` in WSL.
The script ends by checking that `archive_read_support_format_7zip` references `lzma_*`
(`arm-vita-eabi-nm`), and dies if not.

## Configure flags (cmake, `vita.toolchain.cmake`)

`-DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DENABLE_TEST=OFF`
`-DENABLE_ACL=OFF -DENABLE_XATTR=OFF -DENABLE_CAT=OFF -DENABLE_CPIO=OFF -DENABLE_TAR=OFF -DENABLE_UNZIP=OFF`
`-DENABLE_LIBXML2=OFF -DENABLE_EXPAT=OFF -DENABLE_ICONV=OFF -DENABLE_OPENSSL=OFF -DENABLE_MBEDTLS=OFF`
`-DENABLE_NETTLE=OFF -DENABLE_LIBB2=OFF -DENABLE_LZ4=OFF -DENABLE_LZO=OFF -DENABLE_PCREPOSIX=OFF -DENABLE_PCRE2POSIX=OFF`
**`-DENABLE_ZLIB=ON -DENABLE_BZip2=ON -DENABLE_LZMA=ON -DENABLE_ZSTD=ON`**
(`liblzma`/`libzstd` found at `~/vitasdk/arm-vita-eabi/{include,lib}`), `CMAKE_C_FLAGS` as the SDK recipe.

## Link

Top-level `Makefile` puts `-Ithird_party/libarchive/vita/include` first and
`-Lthird_party/libarchive/vita/lib` before `-larchive -llzma -lbz2 -lzstd`. If the lib is
missing the Makefile warns and falls back to the SDK libarchive (no LZMA).
