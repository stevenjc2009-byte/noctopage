#include "test.h"

#include "core/fnv.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Deterministic fake file content: byte i = (i*7 + 3) & 255. Reference values below were
 * computed with an independent Python implementation of the same book-id rule. */
static unsigned char *make_file(size_t n)
{
    unsigned char *p = (unsigned char *)malloc(n ? n : 1);
    for (size_t i = 0; i < n; i++)
        p[i] = (unsigned char)((i * 7 + 3) & 255);
    return p;
}

static uint64_t bookid_of(size_t size)
{
    unsigned char *f = make_file(size);
    size_t plen = bookid_part_len(size);
    uint64_t off = bookid_tail_offset(size);
    uint64_t id = bookid_from_parts(size, f, plen, f + off, plen);
    free(f);
    return id;
}

static void test_vectors(void)
{
    CHECK_U64(fnv64("", 0), 0xcbf29ce484222325ULL);
    CHECK_U64(fnv64("a", 1), 0xaf63dc4c8601ec8cULL);
    CHECK_U64(fnv64("foobar", 6), 0x85944171f73967e8ULL);
}

static void test_incremental(void)
{
    fnv64_t c;
    fnv64_init(&c);
    CHECK_U64(fnv64_final(&c), 0xcbf29ce484222325ULL);
    fnv64_update(&c, "foo", 3);
    fnv64_update(&c, "", 0);
    fnv64_update(&c, "bar", 3);
    CHECK_U64(fnv64_final(&c), 0x85944171f73967e8ULL);
    /* final() does not disturb the state. */
    CHECK_U64(fnv64_final(&c), 0x85944171f73967e8ULL);

    /* Byte at a time equals one shot on binary data (high bytes, NULs). */
    {
        unsigned char buf[300];
        fnv64_t d;
        for (int i = 0; i < 300; i++)
            buf[i] = (unsigned char)(i * 13);
        fnv64_init(&d);
        for (int i = 0; i < 300; i++)
            fnv64_update(&d, buf + i, 1);
        CHECK_U64(fnv64_final(&d), fnv64(buf, sizeof buf));
    }
}

static void test_bookid(void)
{
    CHECK_INT(bookid_part_len(0), 0);
    CHECK_INT(bookid_part_len(100), 100);
    CHECK_INT(bookid_part_len(65536), 65536);
    CHECK_INT(bookid_part_len(200000), 65536);
    CHECK_INT(bookid_part_len((uint64_t)5 << 30), 65536); /* > 4 GiB */
    CHECK_U64(bookid_tail_offset(100), 0);
    CHECK_U64(bookid_tail_offset(65537), 1);
    CHECK_U64(bookid_tail_offset(200000), 200000 - 65536);

    CHECK_U64(bookid_of(0), 0xa8c7f832281a39c5ULL);
    CHECK_U64(bookid_of(1), 0x513819c89daf7872ULL);
    CHECK_U64(bookid_of(100), 0x0d2d79d8297915a9ULL);
    CHECK_U64(bookid_of(65536), 0xc1c2813daa18738cULL);
    CHECK_U64(bookid_of(65537), 0x92f2dbb6ff78b5ddULL);
    CHECK_U64(bookid_of(200000), 0x4ae27aa2987946adULL);

    /* The size is part of the id: equal head/tail bytes but different size differ. */
    CHECK(bookid_of(65536) != bookid_of(65537));
    /* The size is hashed as 8 little-endian bytes: spot-check against a manual build. */
    {
        unsigned char le[8] = {0x00, 0x01, 0, 0, 0, 0, 0, 0}; /* 256 */
        fnv64_t c;
        fnv64_init(&c);
        fnv64_update(&c, le, 8);
        CHECK_U64(bookid_from_parts(256, NULL, 0, NULL, 0), fnv64_final(&c));
    }
}

static void test_hex(void)
{
    char out[17];
    fnv64_hex(0xaf63dc4c8601ec8cULL, out);
    CHECK_STR(out, "af63dc4c8601ec8c");
    fnv64_hex(0x0d2d79d8297915a9ULL, out);
    CHECK_STR(out, "0d2d79d8297915a9");
    fnv64_hex(0, out);
    CHECK_STR(out, "0000000000000000");
}

int main(void)
{
    test_vectors();
    test_incremental();
    test_bookid();
    test_hex();
    return TEST_DONE("fnv");
}
