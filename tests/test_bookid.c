#include "test.h"

#include "core/fnv.h"
#include "lib/bookid.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define TMPDIR "build-host-db"

static unsigned char *make_bytes(size_t n)
{
	unsigned char *p = (unsigned char *)malloc(n ? n : 1);
	for (size_t i = 0; i < n; i++)
		p[i] = (unsigned char)((i * 31 + (i >> 8) * 7 + 5) & 255);
	return p;
}

/* Write buf to TMPDIR/name, optionally xor-ing byte `flip` (flip < 0: none). Returns the path. */
static const char *write_file(const char *name, const unsigned char *buf, size_t n, long flip)
{
	static char path[256];
	FILE *f;
	mkdir(TMPDIR, 0777);
	snprintf(path, sizeof path, TMPDIR "/%s", name);
	f = fopen(path, "wb");
	if (!f) {
		printf("cannot write %s\n", path);
		exit(2);
	}
	for (size_t i = 0; i < n; i++) {
		unsigned char c = buf[i];
		if ((long)i == flip)
			c ^= 0x55;
		fputc(c, f);
	}
	fclose(f);
	return path;
}

static uint64_t id_of(const char *path, uint64_t *size)
{
	uint64_t id = 0;
	CHECK_INT(bookid_file(path, &id, size), 0);
	return id;
}

/* Independent reference: hash size(8 LE) || head || tail with plain fnv64() over one buffer. */
static uint64_t reference_id(const unsigned char *f, size_t n)
{
	size_t plen = n < 65536 ? n : 65536;
	unsigned char *b = (unsigned char *)malloc(8 + 2 * plen + 1);
	uint64_t id;
	for (int i = 0; i < 8; i++)
		b[i] = (unsigned char)((uint64_t)n >> (8 * i));
	memcpy(b + 8, f, plen);
	memcpy(b + 8 + plen, f + n - plen, plen);
	id = fnv64(b, 8 + 2 * plen);
	free(b);
	return id;
}

static void test_fixtures(void)
{
	uint64_t s1 = 0, s2 = 0, s3 = 0;
	uint64_t a = id_of("fixtures/natural_order.cbz", &s1);
	uint64_t other = id_of("fixtures/mixed_case_junk.cbz", &s2);
	FILE *in = fopen("fixtures/natural_order.cbz", "rb");
	unsigned char *buf = (unsigned char *)malloc((size_t)s1);
	uint64_t copy;

	CHECK(s1 > 0);
	CHECK(a != other);
	CHECK(in != NULL);
	if (in) {
		CHECK_INT((long long)fread(buf, 1, (size_t)s1, in), (long long)s1);
		fclose(in);
		/* A byte-identical copy under another name has the same id; the size is reported. */
		copy = id_of(write_file("copy_of_natural_order.cbz", buf, (size_t)s1, -1), &s3);
		CHECK_U64(copy, a);
		CHECK_INT(s3, s1);
		CHECK_U64(a, reference_id(buf, (size_t)s1));
	}
	free(buf);
	CHECK_INT(bookid_file(TMPDIR "/does_not_exist.bin", NULL, NULL), -1);
	{
		uint64_t id;
		CHECK_INT(bookid_file("fixtures/natural_order.cbz", &id, NULL), 0);
		CHECK_INT(bookid_file("fixtures/natural_order.cbz", NULL, NULL), 0);
	}
}

static void test_big_file(void)
{
	const size_t n = 300 * 1024;
	unsigned char *f = make_bytes(n);
	uint64_t size, base = id_of(write_file("big.bin", f, n, -1), &size);
	const long mid_lo = 65536, tail_start = (long)n - 65536;

	CHECK_INT(size, (long long)n);
	CHECK_U64(base, reference_id(f, n));
	/* Inside the head and tail ranges, including both boundary bytes: the id changes. */
	CHECK(id_of(write_file("b_head0.bin", f, n, 0), NULL) != base);
	CHECK(id_of(write_file("b_head_last.bin", f, n, mid_lo - 1), NULL) != base);
	CHECK(id_of(write_file("b_tail_first.bin", f, n, tail_start), NULL) != base);
	CHECK(id_of(write_file("b_tail_last.bin", f, n, (long)n - 1), NULL) != base);
	/* Strictly between the ranges: the id does NOT change (by design: only head, tail, size). */
	CHECK_U64(id_of(write_file("b_mid_first.bin", f, n, mid_lo), NULL), base);
	CHECK_U64(id_of(write_file("b_mid.bin", f, n, 120000), NULL), base);
	CHECK_U64(id_of(write_file("b_mid_last.bin", f, n, tail_start - 1), NULL), base);
	/* The size is part of the id: one extra byte in the middle keeps head and tail identical. */
	{
		unsigned char *g = (unsigned char *)malloc(n + 1);
		memcpy(g, f, 150000);
		g[150000] = 0x42;
		memcpy(g + 150001, f + 150000, n - 150000);
		CHECK_U64(reference_id(g, n + 1) != base, 1);
		CHECK(id_of(write_file("b_plus1.bin", g, n + 1, -1), &size) != base);
		CHECK_INT(size, (long long)n + 1);
		free(g);
	}
	free(f);
}

static void test_small_files(void)
{
	unsigned char *f = make_bytes(1000);
	uint64_t size, base = id_of(write_file("small.bin", f, 1000, -1), &size);
	unsigned char zeros[8] = { 0 };

	CHECK_INT(size, 1000);
	CHECK_U64(base, reference_id(f, 1000));
	/* Whole file is both head and tail: any byte matters. */
	CHECK(id_of(write_file("s_first.bin", f, 1000, 0), NULL) != base);
	CHECK(id_of(write_file("s_mid.bin", f, 1000, 500), NULL) != base);
	CHECK(id_of(write_file("s_last.bin", f, 1000, 999), NULL) != base);
	/* Exactly 64 KiB and 64 KiB + 1: head and tail ranges overlap/touch. */
	{
		unsigned char *g = make_bytes(65537);
		uint64_t a = id_of(write_file("e64.bin", g, 65536, -1), NULL);
		uint64_t b = id_of(write_file("e64p1.bin", g, 65537, -1), NULL);
		CHECK_U64(a, reference_id(g, 65536));
		CHECK_U64(b, reference_id(g, 65537));
		CHECK(a != b);
		CHECK(id_of(write_file("e64p1_mid.bin", g, 65537, 1), NULL) != b);
		free(g);
	}
	/* Empty file: id of the 8 zero size bytes alone. */
	CHECK_U64(id_of(write_file("empty.bin", f, 0, -1), &size), fnv64(zeros, 8));
	CHECK_INT(size, 0);
	free(f);
}

static void test_folder(void)
{
	const char *n1[] = { "p1.jpg", "p2.jpg", "p10.jpg" };
	const char *n2[] = { "p2.jpg", "p1.jpg", "p10.jpg" };
	const char *n3[] = { "p1.jpg", "p2.jpg", "p11.jpg" };
	const uint64_t s1[] = { 100, 200, 0xAB000000000001FFULL }; /* uses all 8 size bytes */
	const uint64_t s2[] = { 100, 201, 300 };
	const uint64_t s_sw[] = { 200, 100, 300 };
	uint64_t id = bookid_folder(n1, s1, 3);
	unsigned char buf[256], *p = buf;
	const char *ab[] = { "ab", "c" }, *a_bc[] = { "a", "bc" };
	const uint64_t z2[] = { 0, 0 };

	CHECK_U64(bookid_folder(n1, s1, 3), id);
	CHECK(bookid_folder(n2, s_sw, 3) != id);  /* order matters */
	CHECK(bookid_folder(n3, s1, 3) != id);    /* name matters */
	CHECK(bookid_folder(n1, s2, 3) != id);    /* size matters */
	CHECK(bookid_folder(n1, s1, 2) != id);    /* count matters */
	CHECK(bookid_folder(ab, z2, 2) != bookid_folder(a_bc, z2, 2)); /* NUL-separated */
	/* Reference: "folder\0" then name\0 + size LE for each entry, one buffer, plain fnv64(). */
	memcpy(p, "folder", 7);
	p += 7;
	for (int i = 0; i < 3; i++) {
		size_t l = strlen(n1[i]) + 1;
		memcpy(p, n1[i], l);
		p += l;
		for (int k = 0; k < 8; k++)
			*p++ = (unsigned char)(s1[i] >> (8 * k));
	}
	CHECK_U64(id, fnv64(buf, (size_t)(p - buf)));
	CHECK_U64(bookid_folder(NULL, NULL, 0), fnv64("folder", 7));
}

int main(void)
{
	mkdir(TMPDIR, 0777);
	test_fixtures();
	test_big_file();
	test_small_files();
	test_folder();
	return TEST_DONE("bookid");
}
