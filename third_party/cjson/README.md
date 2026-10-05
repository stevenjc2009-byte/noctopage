# cJSON (vendored)

| | |
|---|---|
| What | cJSON, a small ANSI C JSON parser/printer. Used for OPDS 2.0 feeds (`src/net/opds`). |
| Version | v1.7.18 (`cJSON_Version()` returns `"1.7.18"`; checked by `tests/test_cjson.c`) |
| Source | https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.18.tar.gz |
| Tarball sha256 (measured) | `3aa806844a03442c00769b83e99970be70fbef03735ff898f4811dd03b9f5ee5` |
| `cJSON.c` sha256 | `75c51de8fa40ac9d7a99319c6330719bd692eb81c0a869265f3d4c682533f9b9` |
| `cJSON.h` sha256 | `0578cc29132912edbc88f83207a8fc76e5db3db0605497e909a9384ef3cc474b` |
| Licence | MIT (`LICENSE` in this directory, copied unchanged) |
| AGPL-3.0 compatibility | MIT is permissive and GPL-compatible, so it can be linked into an AGPL-3.0 app. Keep the copyright notice in `LICENSE`. |

Files copied unmodified: `cJSON.c`, `cJSON.h`, `LICENSE`. `cJSON_Utils.*` (JSON Pointer/Patch) is
not vendored because nothing uses it.

Build notes: plain C, no special flags. Verified to compile with zero warnings under
`-std=gnu11 -Wall -Wextra` on both host gcc 15.2 and arm-vita-eabi-gcc 15.2. The host test Makefile
builds it with `-w` anyway so a vendor update can never break the build.
Memory: parsing a document allocates one small node per value. Always `cJSON_Delete()` the root,
and free strings from `cJSON_Print*` with `cJSON_free()`.
