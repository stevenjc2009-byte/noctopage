/* reader/loader on the fixtures: opening every source kind (and the refusals), pages in
 * natural order (pixel colours below, read from the fixture files), box fitting without
 * upscaling for images and with upscaling for documents, the 800x9000 page split into 3 tiles
 * that match a straight decode row for row, natural sizes unknown before / known after.
 * The wish list, cache, failures, close and threads: test_loader_more.c. */
#include "test.h"

#include "doc/mdoc.h"
#include "img/decode.h"
#include "lib/db.h"
#include "reader/loader_priv.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>

#define FIX "fixtures/"
#define TMP "build-host-loader-tmp1"
#define TIMEOUT 20.0

/* (30, 40) colour of each page in natural order: p1 p2 p3 p9 p10 of natural_order.*. */
static const int COL[5][3] = {
	{ 60, 160, 90 }, { 60, 90, 200 }, { 210, 170, 50 }, { 90, 140, 60 }, { 200, 60, 60 }
};

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

static void nap(void) { usleep(2000); }

/* Status once it leaves OPENING (or OPENING after the timeout). */
static loader_status_t wait_open(loader_t *l, char *err, size_t cap)
{
	double end = now() + TIMEOUT;
	loader_status_t s;
	while ((s = loader_status(l, err, cap)) == LOADER_OPENING && now() < end) nap();
	return s;
}

/* Wish one page and poll for it; NULL on timeout (or a reported failure). */
static const loader_page_t *fetch(loader_t *l, int page, int mw, int mh)
{
	loader_req_t r = { page, mw, mh };
	loader_want(l, &r, 1);
	double end = now() + TIMEOUT;
	const loader_page_t *p;
	char err[160];
	while (!(p = loader_get(l, page, mw, mh)) && now() < end) {
		int e = loader_take_error(l, err, sizeof err);
		if (e >= 0) {
			printf("page %d failed: %s\n", e, err);
			return NULL;
		}
		nap();
	}
	if (!p) printf("timeout waiting for page %d (%dx%d)\n", page, mw, mh);
	return p;
}

static int near_px(const loader_page_t *p, int x, int y, const int *c, int tol)
{
	const loader_tile_t *t = &p->tiles[y / LOADER_TILE_MAX];
	const uint8_t *q = t->rgba + ((size_t)(y - t->y) * p->w + x) * 4;
	return abs(q[0] - c[0]) <= tol && abs(q[1] - c[1]) <= tol && abs(q[2] - c[2]) <= tol &&
	       q[3] == 255;
}

static loader_t *open_ready(const char *path, int kind, int pages)
{
	char err[160] = "";
	loader_t *l = loader_open(path, kind);
	CHECK(l != NULL);
	if (!l) return NULL;
	CHECK_INT(wait_open(l, err, sizeof err), LOADER_READY);
	if (err[0]) printf("  %s: %s\n", path, err);
	CHECK_INT(loader_page_count(l), pages);
	return l;
}

static void test_comic_order(const char *path, int kind, int pages, int tol)
{
	loader_t *l = open_ready(path, kind, pages);
	if (!l) return;
	int w, h;
	CHECK_INT(loader_page_size(l, pages - 1, &w, &h), -1); /* not rendered yet */
	for (int i = 0; i < pages; i++) {
		const loader_page_t *p = fetch(l, i, 0, 0);
		CHECK(p != NULL);
		if (!p) continue;
		CHECK_INT(p->page, i);
		CHECK_INT(p->w, 240);
		CHECK_INT(p->h, 360);
		CHECK_INT(p->ntiles, 1);
		/* folder_comic is page1 page2 page10: green blue yellow (= COL 0 1 2). */
		CHECK(near_px(p, 30, 40, COL[i], tol));
		loader_release(l, p);
	}
	CHECK_INT(loader_page_size(l, pages - 1, &w, &h), 0);
	CHECK_INT(w, 240);
	CHECK_INT(h, 360);
	loader_close(l);
}

static void test_refusals(void)
{
	char err[160] = "";
	loader_t *l = loader_open(FIX "minimal.epub", DB_KIND_REFLOW);
	CHECK_INT(wait_open(l, err, sizeof err), LOADER_FAILED);
	CHECK_STR(err, "reflowable books are not paged");
	CHECK_INT(loader_page_count(l), 0);
	loader_close(l);
	/* Filed as fixed but MuPDF says reflowable: same refusal. */
	l = loader_open(FIX "minimal.epub", DB_KIND_FIXED);
	CHECK_INT(wait_open(l, err, sizeof err), LOADER_FAILED);
	CHECK_STR(err, "reflowable books are not paged");
	loader_close(l);
	err[0] = 0;
	l = loader_open(FIX "no_such_book.cbz", DB_KIND_ARCHIVE);
	CHECK_INT(wait_open(l, err, sizeof err), LOADER_FAILED);
	CHECK(err[0] != 0);
	loader_req_t r = { 0, 0, 0 };
	loader_want(l, &r, 1); /* harmless on a failed loader */
	CHECK(loader_get(l, 0, 0, 0) == NULL);
	loader_close(l);
	l = loader_open(FIX "no_such_dir", DB_KIND_FOLDER);
	CHECK_INT(wait_open(l, err, sizeof err), LOADER_FAILED);
	loader_close(l);
	l = loader_open(FIX "two_pages.pdf", 99);
	CHECK_INT(wait_open(l, err, sizeof err), LOADER_FAILED);
	CHECK_STR(err, "unknown kind");
	loader_close(l);
	loader_close(NULL);
}

static void test_fitting(void)
{
	loader_t *l = open_ready(FIX "natural_order.cbz", DB_KIND_ARCHIVE, 5);
	if (!l) return;
	const loader_page_t *p = fetch(l, 0, 120, 0);
	CHECK(p && p->w == 120 && p->h == 180 && p->src_w == 240 && p->src_h == 360);
	if (p) loader_release(l, p);
	p = fetch(l, 0, 100, 100); /* height binds */
	CHECK(p && p->h == 100 && p->w >= 66 && p->w <= 67);
	if (p) CHECK(near_px(p, 10, 10, COL[0], 4));
	if (p) loader_release(l, p);
	p = fetch(l, 1, 1000, 2000); /* never upscaled */
	CHECK(p && p->w == 240 && p->h == 360);
	if (p) loader_release(l, p);
	/* Exact match only: a cached size does not answer another size. */
	CHECK(loader_get(l, 1, 1000, 2001) == NULL);
	CHECK(loader_get(l, 1, 0, 0) == NULL);
	p = loader_get(l, 1, 1000, 2000);
	CHECK(p != NULL);
	if (p) loader_release(l, p);
	loader_close(l);
}

static void test_pdf(void)
{
	loader_t *l = open_ready(FIX "two_pages.pdf", DB_KIND_FIXED, 2);
	if (!l) return;
	int w = 0, h = 0;
	CHECK_INT(loader_page_size(l, 1, &w, &h), 0); /* documents know sizes up front */
	CHECK(w > 0 && h > 0);
	CHECK_INT(loader_page_size(l, 2, &w, &h), -1);
	const loader_page_t *p = fetch(l, 0, 0, 0); /* scale 1: points */
	CHECK(p && p->w == w && p->h == h && p->src_w == w && p->src_h == h);
	if (p) CHECK(near_px(p, 0, 0, (const int[3]){ 255, 255, 255 }, 0)); /* white paper */
	if (p) loader_release(l, p);
	p = fetch(l, 1, 2 * w, 0); /* upscaled to the box */
	CHECK(p && p->w == 2 * w && abs(p->h - 2 * h) <= 1);
	if (p) loader_release(l, p);
	p = fetch(l, 1, 2 * w, h); /* height binds: scale 1 */
	CHECK(p && p->h == h && abs(p->w - w) <= 1);
	if (p) loader_release(l, p);
	/* 4096 wide would need more than the whole cache: shrunk until it fits. */
	p = fetch(l, 0, 9000, 0);
	CHECK(p && p->w > 3000 && p->w < LOADER_TILE_MAX &&
	      (size_t)p->w * p->h * 4 <= LOADER_CACHE_BYTES);
	if (p) loader_release(l, p);
	loader_close(l);

	/* With room in the cache: width capped at the texture limit, two tiles. */
	l = loader_open_cap(FIX "two_pages.pdf", DB_KIND_FIXED, 256u << 20);
	p = fetch(l, 0, 9000, 0);
	CHECK(p && p->w == LOADER_TILE_MAX && p->h == (int)lround((double)h * 4096 / w) &&
	      p->ntiles == 2);
	if (p && p->ntiles == 2) {
		CHECK_INT(p->tiles[1].y, 4096);
		CHECK_INT(p->tiles[1].h, p->h - 4096);
		CHECK(p->tiles[1].rgba == p->tiles[0].rgba + (size_t)4096 * p->w * 4);
	}
	if (p) loader_release(l, p);
	loader_close(l);
}

static int copy_file(const char *from, const char *to)
{
	FILE *a = fopen(from, "rb"), *b = fopen(to, "wb");
	int c, ok = a && b;
	if (ok)
		while ((c = fgetc(a)) != EOF) fputc(c, b);
	if (a) fclose(a);
	if (b && fclose(b) != 0) ok = 0;
	return ok ? 0 : -1;
}

static uint8_t *slurp(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *d = malloc((size_t)n);
	if (d && fread(d, 1, (size_t)n, f) != (size_t)n) {
		free(d);
		d = NULL;
	}
	fclose(f);
	*len = (size_t)n;
	return d;
}

/* The tall page at natural size: 3 tiles of 4096, 4096, 808 rows, equal to a straight decode. */
static void test_tall(const char *fixture, int page, loader_t *l)
{
	size_t len;
	uint8_t *data = slurp(fixture, &len);
	img_t ref;
	char err[160];
	CHECK(data && img_decode(data, len, 0, 0, &ref, err, sizeof err) == 0);
	free(data);
	const loader_page_t *p = fetch(l, page, 0, 0);
	CHECK(p != NULL);
	if (p && ref.rgba) {
		CHECK_INT(p->w, 800);
		CHECK_INT(p->h, 9000);
		CHECK_INT(p->ntiles, 3);
		static const int ty[3] = { 0, 4096, 8192 }, th[3] = { 4096, 4096, 808 };
		size_t row = (size_t)p->w * 4;
		for (int i = 0; i < 3 && i < p->ntiles; i++) {
			CHECK_INT(p->tiles[i].y, ty[i]);
			CHECK_INT(p->tiles[i].h, th[i]);
			CHECK(memcmp(p->tiles[i].rgba, ref.rgba + ty[i] * row, th[i] * row) == 0);
		}
		/* The seams are colour bands: the rows either side differ from white. */
		CHECK(near_px(p, 400, 4095, (const int[3]){ 150, 70, 190 }, 12));
		CHECK(near_px(p, 400, 4096, (const int[3]){ 150, 70, 190 }, 12));
		CHECK(near_px(p, 400, 8191, (const int[3]){ 180, 60, 140 }, 12));
		CHECK(near_px(p, 400, 8192, (const int[3]){ 180, 60, 140 }, 12));
	}
	if (p) loader_release(l, p);
	img_free(&ref);
	/* Fitting a tall page into a short box gives one tile. */
	p = fetch(l, page, 0, 900);
	CHECK(p && p->h == 900 && p->w == 80 && p->ntiles == 1 && p->tiles[0].y == 0);
	if (p) loader_release(l, p);
}

static void put_chunk(FILE *f, const char *type, const uint8_t *d, uint32_t n)
{
	uint8_t be[4] = { n >> 24, n >> 16 & 255, n >> 8 & 255, n & 255 };
	fwrite(be, 1, 4, f);
	fwrite(type, 1, 4, f);
	if (n) fwrite(d, 1, n, f);
	uLong c = crc32(crc32(0, (const Bytef *)type, 4), d, n);
	uint8_t cb[4] = { c >> 24, c >> 16 & 255, c >> 8 & 255, c & 255 };
	fwrite(cb, 1, 4, f);
}

/* A w x h solid RGB PNG written with zlib: wider than any fixture. */
static int write_png(const char *path, int w, int h, const uint8_t rgb[3])
{
	size_t raw_n = (size_t)h * (1 + (size_t)w * 3);
	uLongf z_n = compressBound(raw_n);
	uint8_t *raw = malloc(raw_n), *z = malloc(z_n);
	FILE *f = fopen(path, "wb");
	int ok = raw && z && f;
	if (ok) {
		for (size_t i = 0; i < raw_n; i++) {
			size_t x = i % (1 + (size_t)w * 3);
			raw[i] = x == 0 ? 0 : rgb[(x - 1) % 3];
		}
		ok = compress(z, &z_n, raw, raw_n) == Z_OK;
	}
	if (ok) {
		uint8_t ihdr[13] = { w >> 24, w >> 16 & 255, w >> 8 & 255, w & 255,
		                     h >> 24, h >> 16 & 255, h >> 8 & 255, h & 255, 8, 2, 0, 0, 0 };
		static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
		fwrite(sig, 1, 8, f);
		put_chunk(f, "IHDR", ihdr, 13);
		put_chunk(f, "IDAT", z, (uint32_t)z_n);
		put_chunk(f, "IEND", NULL, 0);
	}
	if (f && fclose(f) != 0) ok = 0;
	free(raw);
	free(z);
	return ok ? 0 : -1;
}

/* Image pages wider than a texture come back LOADER_TILE_MAX wide, whatever the box says. */
static void test_wide(void)
{
	static const uint8_t rgb[3] = { 20, 120, 220 };
	mkdir(TMP, 0777);
	mkdir(TMP "/wide", 0777);
	CHECK_INT(write_png(TMP "/wide/a.png", 5000, 20, rgb), 0);
	loader_t *l = open_ready(TMP "/wide", DB_KIND_FOLDER, 1);
	if (!l) return;
	const loader_page_t *p = fetch(l, 0, 0, 0);
	CHECK(p && p->w == LOADER_TILE_MAX && p->h == 16 && p->src_w == 5000 && p->src_h == 20);
	if (p) CHECK(near_px(p, 4095, 15, (const int[3]){ 20, 120, 220 }, 1));
	if (p) loader_release(l, p);
	p = fetch(l, 0, 6000, 0);
	CHECK(p && p->w == LOADER_TILE_MAX);
	if (p) loader_release(l, p);
	loader_close(l);
}

static void test_tall_pages(void)
{
	mkdir(TMP, 0777);
	mkdir(TMP "/tall", 0777);
	CHECK_INT(copy_file(FIX "tall_800x9000.png", TMP "/tall/a.png"), 0);
	CHECK_INT(copy_file(FIX "tall_800x9000.jpg", TMP "/tall/b.jpg"), 0);
	loader_t *l = open_ready(TMP "/tall", DB_KIND_FOLDER, 2);
	if (!l) return;
	test_tall(FIX "tall_800x9000.png", 0, l);
	test_tall(FIX "tall_800x9000.jpg", 1, l);
	loader_close(l);
}

int main(void)
{
	if (mdoc_global_init() != 0) {
		printf("mdoc_global_init failed\n");
		return 1;
	}
	test_comic_order(FIX "natural_order.cbz", DB_KIND_ARCHIVE, 5, 2);
	test_comic_order(FIX "natural_order.cbt", DB_KIND_ARCHIVE, 5, 2);
	test_comic_order(FIX "natural_order.cb7", DB_KIND_ARCHIVE, 5, 2);
	test_comic_order(FIX "folder_comic", DB_KIND_FOLDER, 3, 12);
	test_refusals();
	test_fitting();
	test_pdf();
	test_tall_pages();
	test_wide();
	mdoc_global_shutdown();
	return TEST_DONE("loader");
}
