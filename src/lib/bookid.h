/* bookid — stable 64-bit book ids (pure, stdio only). Rule: core/fnv.h bookid_from_parts().
 * The id follows the book across moves and renames; it changes when the size, the first 64 KiB
 * or the last 64 KiB change. A change strictly between those ranges (files > 128 KiB) does NOT
 * change the id. Files of 64 KiB or less hash the whole file twice (see fnv.h). */
#ifndef NOCTO_BOOKID_H
#define NOCTO_BOOKID_H

#include <stdint.h>

/* Hash a file (fopen "rb": works with "ux0:..." paths on the Vita). Reads at most 128 KiB.
 * Returns 0 and fills *out_id / *out_size (either may be NULL); -1 when the file cannot be
 * opened or sized, -2 on a read error. Offsets are off_t: on the Vita (newlib, 32-bit off_t) files
 * of 2 GiB or more cannot be sized and return -1. */
int bookid_file(const char *path, uint64_t *out_id, uint64_t *out_size);

/* Id of a folder comic: FNV-1a over "folder\0", then for every entry its name, a NUL, and its
 * size as 8 bytes little-endian, in the order given (the caller passes natural order). names and
 * sizes have n entries; n == 0 is valid (names/sizes may then be NULL). */
uint64_t bookid_folder(const char *const *names, const uint64_t *sizes, int n);

#endif
