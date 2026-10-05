#include "core/fnv.h"

void fnv64_init(fnv64_t *c)
{
    c->h = FNV64_OFFSET;
}

void fnv64_update(fnv64_t *c, const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;
    uint64_t h = c->h;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= FNV64_PRIME;
    }
    c->h = h;
}

uint64_t fnv64_final(const fnv64_t *c)
{
    return c->h;
}

uint64_t fnv64(const void *data, size_t len)
{
    fnv64_t c;
    fnv64_init(&c);
    fnv64_update(&c, data, len);
    return fnv64_final(&c);
}

size_t bookid_part_len(uint64_t size)
{
    return size < BOOKID_CHUNK ? (size_t)size : (size_t)BOOKID_CHUNK;
}

uint64_t bookid_tail_offset(uint64_t size)
{
    return size - bookid_part_len(size);
}

uint64_t bookid_from_parts(uint64_t size, const void *head, size_t headlen,
                           const void *tail, size_t taillen)
{
    unsigned char le[8];
    fnv64_t c;

    for (int i = 0; i < 8; i++)
        le[i] = (unsigned char)(size >> (8 * i));
    fnv64_init(&c);
    fnv64_update(&c, le, sizeof le);
    fnv64_update(&c, head, headlen);
    fnv64_update(&c, tail, taillen);
    return fnv64_final(&c);
}

void fnv64_hex(uint64_t v, char out[17])
{
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++)
        out[i] = digits[(v >> (60 - 4 * i)) & 0xf];
    out[16] = '\0';
}
