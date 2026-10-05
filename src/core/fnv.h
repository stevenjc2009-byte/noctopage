/* fnv — 64-bit FNV-1a, incremental, plus the Noctopage book-id helper. Pure C. */
#ifndef NOCTO_FNV_H
#define NOCTO_FNV_H

#include <stddef.h>
#include <stdint.h>

#define FNV64_OFFSET 0xcbf29ce484222325ULL
#define FNV64_PRIME  0x00000100000001b3ULL

typedef struct {
    uint64_t h;
} fnv64_t;

void fnv64_init(fnv64_t *c);
void fnv64_update(fnv64_t *c, const void *data, size_t len);
uint64_t fnv64_final(const fnv64_t *c);

/* One-shot convenience. */
uint64_t fnv64(const void *data, size_t len);

/* Book id = FNV-1a( size as 8 bytes little-endian || first 64 KiB || last 64 KiB ).
 * For files smaller than 64 KiB the head and tail are both the whole file (the bytes are
 * hashed twice) so the rule has no special case. Use the three helpers below to know what
 * to read, then hand the buffers to bookid_from_parts(). */
#define BOOKID_CHUNK 65536u

/* Number of bytes to read for the head and for the tail: min(size, BOOKID_CHUNK). */
size_t bookid_part_len(uint64_t size);
/* File offset where the tail starts: size - bookid_part_len(size). */
uint64_t bookid_tail_offset(uint64_t size);

uint64_t bookid_from_parts(uint64_t size, const void *head, size_t headlen,
                           const void *tail, size_t taillen);

/* 16 lowercase hex digits + NUL into out (>= 17 bytes). */
void fnv64_hex(uint64_t v, char out[17]);

#endif
