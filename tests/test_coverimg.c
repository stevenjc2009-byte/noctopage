/* lib/coverimg on the fixtures: the cover is the FIRST page in natural order (p1, not the
 * archive's first entry p10), folder comics too; documents render; thumbnail files round-trip
 * and short / wrong files are refused. Fixture page colours: tools/make_fixtures.py PALETTE. */
#include "test.h"

#include "doc/mdoc.h"
#include "lib/coverimg.h"
#include "lib/db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FIX "fixtures/"

static uint8_t g_px[COVER_BYTES];

/* Pixel (x, y) is within tol of (r, g, b). (30, 40) is page background on every fixture page. */
static int near(const uint8_t *px, int x, int y, int r, int g, int b, int tol)
{
	const uint8_t *p = px + ((size_t)y * COVER_W + x) * 4;
	return abs(p[0] - r) <= tol && abs(p[1] - g) <= tol && abs(p[2] - b) <= tol && p[3] == 255;
}

static int distinct_colours(const uint8_t *px)
{
	int n = 0;
	uint32_t seen[8];
	for (size_t i = 0; i < (size_t)COVER_W * COVER_H && n < 8; i += 97) {
		uint32_t c;
		memcpy(&c, px + i * 4, 4);
		int dup = 0;
		for (int k = 0; k < n; k++) dup |= seen[k] == c;
		if (!dup) seen[n++] = c;
	}
	return n;
}

static void test_comics(void)
{
	char err[160] = "";
	CHECK_INT(coverimg_make(FIX "natural_order.cbz", DB_KIND_ARCHIVE, g_px, err, sizeof err), 0);
	CHECK(near(g_px, 30, 40, 60, 160, 90, 2));   /* p1 green, not p10 red */
	CHECK(!near(g_px, 30, 40, 200, 60, 60, 40));
	CHECK_INT(coverimg_make(FIX "natural_order.cb7", DB_KIND_ARCHIVE, g_px, err, sizeof err), 0);
	CHECK(near(g_px, 30, 40, 60, 160, 90, 2));
	CHECK_INT(coverimg_make(FIX "natural_order.cbt", DB_KIND_ARCHIVE, g_px, err, sizeof err), 0);
	CHECK(near(g_px, 30, 40, 60, 160, 90, 2));
	/* Cover.JPG sorts first and __MACOSX junk is skipped. */
	CHECK_INT(coverimg_make(FIX "mixed_case_junk.cbz", DB_KIND_ARCHIVE, g_px, err, sizeof err), 0);
	CHECK_INT(coverimg_make(FIX "folder_comic", DB_KIND_FOLDER, g_px, err, sizeof err), 0);
	CHECK(near(g_px, 30, 40, 60, 160, 90, 12)); /* page1.jpg green (JPEG), not page10 */
	CHECK_INT(coverimg_make(FIX "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\xe3\x81\xae\xe6\xbc\xab"
	                        "\xe7\x94\xbb \xe7\xac\xac" "1\xe5\xb7\xbb.cbz",
	                        DB_KIND_ARCHIVE, g_px, err, sizeof err), 0);
}

static void copy_file(const char *from, const char *to)
{
	FILE *a = fopen(from, "rb"), *b = fopen(to, "wb");
	int c;
	if (a && b)
		while ((c = fgetc(a)) != EOF) fputc(c, b);
	if (a) fclose(a);
	if (b) fclose(b);
}

/* Every fixture stores page 1 first, so this folder stores it LAST: strcmp lists "a10" before
 * "a2"; natural order makes a2 the cover. */
static void test_natural_beats_listing_order(void)
{
	const char *dir = "build-host-coverimg-order";
	char err[160] = "";
	mkdir(dir, 0777);
	copy_file(FIX "folder_comic/page10.jpg", "build-host-coverimg-order/a10.jpg");
	copy_file(FIX "folder_comic/page2.png", "build-host-coverimg-order/a2.png");
	CHECK_INT(coverimg_make(dir, DB_KIND_FOLDER, g_px, err, sizeof err), 0);
	CHECK(near(g_px, 30, 40, 60, 90, 200, 2)); /* a2 blue, not a10 yellow */
	remove("build-host-coverimg-order/a10.jpg");
	remove("build-host-coverimg-order/a2.png");
	rmdir(dir);
}

static void test_documents(void)
{
	char err[160] = "";
	CHECK_INT(mdoc_global_init(), 0);
	memset(g_px, 0, sizeof g_px);
	CHECK_INT(coverimg_make(FIX "two_pages.pdf", DB_KIND_FIXED, g_px, err, sizeof err), 0);
	CHECK(distinct_colours(g_px) >= 2);
	memset(g_px, 0, sizeof g_px);
	CHECK_INT(coverimg_make(FIX "minimal.epub", DB_KIND_REFLOW, g_px, err, sizeof err), 0);
	CHECK(distinct_colours(g_px) >= 2);
	memset(g_px, 0, sizeof g_px);
	CHECK_INT(coverimg_make(FIX "minimal.txt", DB_KIND_REFLOW, g_px, err, sizeof err), 0);
	CHECK(distinct_colours(g_px) >= 2);
	memset(g_px, 0, sizeof g_px); /* fb2 covers once came out blank white on the Vita */
	CHECK_INT(coverimg_make(FIX "minimal.fb2", DB_KIND_REFLOW, g_px, err, sizeof err), 0);
	CHECK(distinct_colours(g_px) >= 2);
}

static void test_failures(void)
{
	char err[160] = "";
	CHECK_INT(coverimg_make(FIX "nope.cbz", DB_KIND_ARCHIVE, g_px, err, sizeof err), -1);
	CHECK(err[0] != '\0');
	CHECK_INT(coverimg_make(FIX "minimal.txt", DB_KIND_ARCHIVE, g_px, err, sizeof err), -1);
	CHECK_INT(coverimg_make(FIX "natural_order.cbz", 99, g_px, err, sizeof err), -1);
	CHECK_INT(coverimg_make(NULL, DB_KIND_ARCHIVE, g_px, err, sizeof err), -1);
	CHECK_INT(coverimg_make(FIX "opds", DB_KIND_FOLDER, g_px, err, sizeof err), -1); /* no images */
}

static void test_files(void)
{
	const char *f = "build-host-coverimg-thumb.bin";
	static uint8_t back[COVER_BYTES];
	for (size_t i = 0; i < COVER_BYTES; i++) g_px[i] = (uint8_t)(i * 31 + 7);
	CHECK_INT(coverimg_save(f, g_px), 0);
	CHECK_INT(coverimg_load(f, back), 0);
	CHECK(memcmp(g_px, back, COVER_BYTES) == 0);
    char err[160] = "";
    CHECK_INT(coverimg_file(f, back, err, sizeof err), 0);
    CHECK(memcmp(g_px, back, COVER_BYTES) == 0);

	FILE *fp = fopen(f, "ab"); /* one byte too many */
	fputc(0, fp);
	fclose(fp);
	CHECK_INT(coverimg_load(f, back), -1);

	fp = fopen(f, "wb"); /* right size, wrong magic */
	for (size_t i = 0; i < COVER_FILE_BYTES; i++) fputc('X', fp);
	fclose(fp);
	CHECK_INT(coverimg_load(f, back), -1);

	fp = fopen(f, "wb"); /* header only */
	fwrite("NTH1\xb4\x00\x0e\x01", 1, 8, fp);
	fclose(fp);
	CHECK_INT(coverimg_load(f, back), -1);

	CHECK_INT(coverimg_file(FIX "folder_comic/page2.png", back, err, sizeof err), 0);
    CHECK(near(back, 30, 40, 60, 90, 200, 2));
    CHECK_INT(coverimg_file(FIX "minimal.txt", back, err, sizeof err), -1);
    remove(f);
	CHECK_INT(coverimg_load(f, back), -1);
	CHECK_INT(coverimg_save("no-such-dir/x.bin", g_px), -1);
}

int main(void)
{
	test_comics();
	test_natural_beats_listing_order();
	test_documents();
	test_failures();
	test_files();
	return TEST_DONE("coverimg");
}
