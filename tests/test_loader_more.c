/* reader/loader, the threaded side: the wish list is replaced (a dropped wish never renders,
 * cached ones are skipped), eviction under a small cache cap spares pinned pages, a page too
 * big for the cache and a corrupt page fail without retries until they leave the list and
 * return, close is prompt while busy / opening / holding wishes, and a few seconds of the
 * main thread churning wishes and pins against the worker (ASan + UBSan; TSan in the
 * package's own script). Uses the test hooks in reader/loader_priv.h. */
#include "test.h"

#include "doc/mdoc.h"
#include "lib/db.h"
#include "reader/loader_priv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define FIX "fixtures/"
#define TMP "build-host-loader-tmp2"
#define TIMEOUT 20.0
#define PAGE_COST (240u * 360u * 4u) /* one natural_order page at natural size, pixels only */

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

static loader_t *open_ready(const char *path, int kind, size_t cap)
{
	loader_t *l = loader_open_cap(path, kind, cap);
	CHECK(l != NULL);
	if (!l) return NULL;
	double end = now() + TIMEOUT;
	while (loader_status(l, NULL, 0) == LOADER_OPENING && now() < end) nap();
	CHECK_INT(loader_status(l, NULL, 0), LOADER_READY);
	return l;
}

/* Poll for an already wished page; NULL on timeout. */
static const loader_page_t *poll_get(loader_t *l, int page, int mw, int mh)
{
	double end = now() + TIMEOUT;
	const loader_page_t *p;
	while (!(p = loader_get(l, page, mw, mh)) && now() < end) nap();
	if (!p) printf("timeout waiting for page %d\n", page);
	return p;
}

static const loader_page_t *fetch(loader_t *l, int page)
{
	loader_req_t r = { page, 0, 0 };
	loader_want(l, &r, 1);
	return poll_get(l, page, 0, 0);
}

static int poll_error(loader_t *l, char *err, size_t cap)
{
	double end = now() + TIMEOUT;
	int e;
	while ((e = loader_take_error(l, err, cap)) < 0 && now() < end) nap();
	return e;
}

static void settle(void) { usleep(150 * 1000); } /* give a wrong worker time to misbehave */

static int colour_ok(const loader_page_t *p, int page)
{
	const uint8_t *q = p->tiles[0].rgba + ((size_t)(p->h / 9) * p->w + p->w / 8) * 4;
	return abs(q[0] - COL[page][0]) <= 4 && abs(q[1] - COL[page][1]) <= 4 &&
	       abs(q[2] - COL[page][2]) <= 4;
}

static void test_want_replaces(void)
{
	loader_t *l = open_ready(FIX "natural_order.cbz", DB_KIND_ARCHIVE, LOADER_CACHE_BYTES);
	if (!l) return;
	loader_test_hold(l, 1);
	loader_req_t a[3] = { { 0, 0, 0 }, { 1, 0, 0 }, { 2, 0, 0 } }, b = { 3, 0, 0 };
	loader_want(l, a, 3);
	loader_want(l, &b, 1); /* 0..2 dropped before the worker could start them */
	settle();
	CHECK_INT(loader_test_renders(l, 0), 0); /* held: nothing started */
	loader_test_hold(l, 0);
	const loader_page_t *p = poll_get(l, 3, 0, 0);
	CHECK(p != NULL);
	if (p) loader_release(l, p);
	settle();
	CHECK_INT(loader_test_renders(l, 0), 0);
	CHECK_INT(loader_test_renders(l, 1), 0);
	CHECK_INT(loader_test_renders(l, 2), 0);
	CHECK_INT(loader_test_renders(l, 3), 1);
	/* Cached wishes are skipped; out-of-range and duplicate ones are ignored. */
	loader_req_t c[4] = { { 3, 0, 0 }, { 99, 0, 0 }, { -1, 0, 0 }, { 3, 0, 0 } };
	loader_want(l, c, 4);
	settle();
	CHECK_INT(loader_test_renders(l, 3), 1);
	CHECK_INT(loader_take_error(l, NULL, 0), -1);
	/* n = 0 clears: nothing more starts. */
	loader_test_hold(l, 1);
	loader_want(l, a, 3);
	loader_want(l, NULL, 0);
	loader_test_hold(l, 0);
	settle();
	CHECK_INT(loader_test_renders(l, 0) + loader_test_renders(l, 1), 0);
	loader_close(l);
}

static void test_eviction(void)
{
	/* Room for two pages (plus bookkeeping), not three. */
	size_t cap = 2 * PAGE_COST + 4096;
	loader_t *l = open_ready(FIX "natural_order.cbz", DB_KIND_ARCHIVE, cap);
	if (!l) return;
	const loader_page_t *p0 = fetch(l, 0); /* pinned throughout */
	CHECK(p0 != NULL);
	for (int i = 1; i < 5; i++) {
		const loader_page_t *p = fetch(l, i);
		CHECK(p != NULL);
		if (p) CHECK(colour_ok(p, i));
		if (p) loader_release(l, p);
		CHECK(loader_test_cache_bytes(l) <= cap);
	}
	CHECK_INT(loader_test_cache_count(l), 2); /* pinned page 0 + newest page 4 */
	if (p0) CHECK(colour_ok(p0, 0));          /* still alive (ASan would flag a free) */
	const loader_page_t *again = loader_get(l, 0, 0, 0);
	CHECK(again == p0);
	if (again) loader_release(l, again);
	CHECK(loader_get(l, 1, 0, 0) == NULL); /* evicted */
	if (p0) loader_release(l, p0);
	CHECK(loader_test_cache_bytes(l) <= cap);
	loader_close(l);

	/* A page alone bigger than the cap comes back shrunk to fit it, under its own key. */
	l = open_ready(FIX "natural_order.cbz", DB_KIND_ARCHIVE, PAGE_COST / 2);
	if (!l) return;
	const loader_page_t *p = fetch(l, 2);
	CHECK(p && p->w < 240 && p->w >= 160 && abs(p->w * 3 - p->h * 2) <= 3);
	if (p) CHECK(colour_ok(p, 2));
	if (p) loader_release(l, p);
	CHECK(loader_test_cache_bytes(l) <= PAGE_COST / 2);
	CHECK_INT(loader_take_error(l, NULL, 0), -1);
	loader_close(l);
}

static int write_file(const char *path, const void *d, size_t n)
{
	FILE *f = fopen(path, "wb");
	if (!f) return -1;
	int ok = fwrite(d, 1, n, f) == n;
	return fclose(f) == 0 && ok ? 0 : -1;
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

static void test_corrupt_page(void)
{
	static const char junk[] = "\x89PNG\r\n\x1a\n this is not really an image";
	mkdir(TMP, 0777);
	mkdir(TMP "/bad", 0777);
	CHECK_INT(copy_file(FIX "folder_comic/page1.jpg", TMP "/bad/p1.jpg"), 0);
	CHECK_INT(write_file(TMP "/bad/p2.png", junk, sizeof junk), 0);
	loader_t *l = open_ready(TMP "/bad", DB_KIND_FOLDER, LOADER_CACHE_BYTES);
	if (!l) return;
	char err[160] = "";
	loader_req_t both[2] = { { 1, 0, 0 }, { 0, 0, 0 } }, good = { 0, 0, 0 };
	loader_want(l, both, 2);
	const loader_page_t *p = poll_get(l, 0, 0, 0);
	CHECK(p != NULL);
	if (p) loader_release(l, p);
	CHECK_INT(loader_take_error(l, err, sizeof err), 1);
	CHECK(err[0] != 0);
	CHECK_INT(loader_take_error(l, err, sizeof err), -1); /* cleared by reading */
	CHECK_INT(loader_test_renders(l, 1), 1);
	loader_want(l, both, 2); /* still wished: not retried */
	settle();
	CHECK_INT(loader_test_renders(l, 1), 1);
	CHECK_INT(loader_take_error(l, NULL, 0), -1);
	loader_want(l, &good, 1); /* leaves the list ... */
	loader_want(l, both, 2);  /* ... and comes back: retried once */
	CHECK_INT(poll_error(l, err, sizeof err), 1);
	settle();
	CHECK_INT(loader_test_renders(l, 1), 2);
	loader_close(l);
}

static void test_close(void)
{
	mkdir(TMP, 0777);
	mkdir(TMP "/tall", 0777);
	CHECK_INT(copy_file(FIX "tall_800x9000.png", TMP "/tall/a.png"), 0);
	loader_t *l = open_ready(TMP "/tall", DB_KIND_FOLDER, LOADER_CACHE_BYTES);
	if (!l) return;
	loader_req_t r = { 0, 0, 0 };
	loader_want(l, &r, 1);
	double end = now() + TIMEOUT;
	while (!loader_test_busy(l) && !loader_test_cache_count(l) && now() < end) usleep(200);
	CHECK(loader_test_busy(l) || loader_test_cache_count(l));
	double t0 = now();
	loader_close(l); /* the in-flight decode finishes, then the worker exits */
	double dt = now() - t0;
	CHECK(dt < 3.0);
	printf("close while busy: %.0f ms\n", dt * 1000);

	t0 = now();
	l = loader_open(FIX "natural_order.cb7", DB_KIND_ARCHIVE); /* still opening */
	loader_close(l);
	l = loader_open(FIX "two_pages.pdf", DB_KIND_FIXED);
	loader_want(l, &r, 1); /* wished before READY */
	loader_close(l);
	l = open_ready(FIX "natural_order.cbz", DB_KIND_ARCHIVE, LOADER_CACHE_BYTES);
	loader_test_hold(l, 1);
	loader_want(l, &r, 1);
	loader_close(l); /* held wishes do not block close */
	CHECK(now() - t0 < 5.0);
}

/* Wished before READY: rendered once the count is known; out-of-range ones dropped. */
static void test_early_wish(void)
{
	loader_t *l = loader_open(FIX "two_pages.pdf", DB_KIND_FIXED);
	CHECK(l != NULL);
	if (!l) return;
	loader_req_t r[2] = { { 7, 0, 0 }, { 1, 300, 0 } };
	loader_want(l, r, 2);
	const loader_page_t *p = poll_get(l, 1, 300, 0);
	CHECK(p && p->w == 300);
	if (p) loader_release(l, p);
	CHECK_INT(loader_take_error(l, NULL, 0), -1);
	loader_close(l);
}

/* A two-page PDF with a three-item outline (one nested), xref offsets computed. */
static int write_outline_pdf(const char *path)
{
	static const char *obj[] = {
		"<< /Type /Catalog /Pages 2 0 R /Outlines 5 0 R >>",
		"<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>",
		"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] >>",
		"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] >>",
		"<< /Type /Outlines /First 6 0 R /Last 7 0 R /Count 3 >>",
		"<< /Title (Chapter One) /Parent 5 0 R /Next 7 0 R /Dest [3 0 R /Fit] >>",
		"<< /Title (Chapter Two) /Parent 5 0 R /Prev 6 0 R /Dest [4 0 R /Fit] "
		"/First 8 0 R /Last 8 0 R /Count 1 >>",
		"<< /Title (Section) /Parent 7 0 R /Dest [4 0 R /Fit] >>",
	};
	enum { N = sizeof obj / sizeof obj[0] };
	long off[N];
	FILE *f = fopen(path, "wb");
	if (!f) return -1;
	fputs("%PDF-1.4\n", f);
	for (int i = 0; i < N; i++) {
		off[i] = ftell(f);
		fprintf(f, "%d 0 obj\n%s\nendobj\n", i + 1, obj[i]);
	}
	long xref = ftell(f);
	fprintf(f, "xref\n0 %d\n0000000000 65535 f \n", N + 1);
	for (int i = 0; i < N; i++) fprintf(f, "%010ld 00000 n \n", off[i]);
	fprintf(f, "trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n", N + 1, xref);
	return fclose(f) == 0 ? 0 : -1;
}

static void test_toc(void)
{
	mdoc_toc_item_t toc[LOADER_TOC_MAX];
	loader_t *l = loader_open(FIX "two_pages.pdf", DB_KIND_FIXED);
	CHECK_INT(loader_toc(l, toc, LOADER_TOC_MAX), 0); /* OPENING (or no outline) */
	loader_close(l);
	l = open_ready(FIX "two_pages.pdf", DB_KIND_FIXED, LOADER_CACHE_BYTES);
	CHECK_INT(loader_toc(l, toc, LOADER_TOC_MAX), 0); /* no outline */
	loader_close(l);
	l = open_ready(FIX "natural_order.cbz", DB_KIND_ARCHIVE, LOADER_CACHE_BYTES);
	CHECK_INT(loader_toc(l, toc, LOADER_TOC_MAX), 0); /* comics have none */
	loader_close(l);

	mkdir(TMP, 0777);
	CHECK_INT(write_outline_pdf(TMP "/outline.pdf"), 0);
	l = open_ready(TMP "/outline.pdf", DB_KIND_FIXED, LOADER_CACHE_BYTES);
	if (!l) return;
	CHECK_INT(loader_page_count(l), 2);
	memset(toc, 0, sizeof toc);
	int n = loader_toc(l, toc, LOADER_TOC_MAX);
	CHECK_INT(n, 3);
	if (n == 3) {
		CHECK_STR(toc[0].title, "Chapter One");
		CHECK_INT(toc[0].depth, 0);
		CHECK_INT(toc[0].page, 0);
		CHECK_STR(toc[1].title, "Chapter Two");
		CHECK_INT(toc[1].page, 1);
		CHECK_STR(toc[2].title, "Section");
		CHECK_INT(toc[2].depth, 1);
		CHECK_INT(toc[2].page, 1);
	}
	CHECK_INT(loader_toc(l, toc, 1), 1); /* max respected */
	CHECK_INT(loader_toc(l, toc, 0), 0);
	CHECK_INT(loader_toc(l, NULL, 5), 0);
	loader_close(l);
	CHECK_INT(loader_toc(NULL, toc, 5), 0);
}

static void stress(const char *path, int kind, int pages, double secs, size_t cap)
{
	static const int SZ[4][2] = { { 0, 0 }, { 120, 0 }, { 100, 100 }, { 240, 360 } };
	loader_t *l = open_ready(path, kind, cap);
	if (!l) return;
	unsigned seed = 12345;
	const loader_page_t *held[3] = { NULL, NULL, NULL };
	int gets = 0, bad = 0, iters = 0;
	double end = now() + secs;
	while (now() < end) {
		loader_req_t r[LOADER_WANT_MAX];
		int n = 1 + rand_r(&seed) % LOADER_WANT_MAX;
		for (int i = 0; i < n; i++) {
			const int *s = SZ[rand_r(&seed) % 4];
			r[i] = (loader_req_t){ rand_r(&seed) % pages, s[0], s[1] };
		}
		loader_want(l, r, n);
		for (int i = 0; i < n; i++) {
			const loader_page_t *p = loader_get(l, r[i].page, r[i].max_w, r[i].max_h);
			if (!p) continue;
			gets++;
			if (p->page != r[i].page || (r[i].max_w && p->w > r[i].max_w) ||
			    (r[i].max_h && p->h > r[i].max_h) || p->ntiles != 1)
				bad++;
			if (kind != DB_KIND_FIXED && !colour_ok(p, p->page)) bad++;
			int slot = rand_r(&seed) % 4;
			if (slot < 3) {
				if (held[slot]) loader_release(l, held[slot]);
				held[slot] = p;
			} else {
				loader_release(l, p);
			}
		}
		if (loader_take_error(l, NULL, 0) >= 0) bad++;
		iters++;
		usleep(rand_r(&seed) % 3000);
	}
	for (int i = 0; i < 3; i++)
		if (held[i]) loader_release(l, held[i]);
	printf("stress %s: %d iterations, %d gets, %d bad\n", path, iters, gets, bad);
	CHECK(gets > 20);
	CHECK_INT(bad, 0);
	CHECK(loader_test_cache_bytes(l) <= cap);
	loader_close(l);
}

int main(void)
{
	if (mdoc_global_init() != 0) {
		printf("mdoc_global_init failed\n");
		return 1;
	}
	test_want_replaces();
	test_eviction();
	test_corrupt_page();
	test_close();
	test_early_wish();
	test_toc();
	stress(FIX "natural_order.cbz", DB_KIND_ARCHIVE, 5, 3.0, 4 * PAGE_COST);
	stress(FIX "two_pages.pdf", DB_KIND_FIXED, 2, 1.5, 6u << 20);
	mdoc_global_shutdown();
	return TEST_DONE("loader_more");
}
