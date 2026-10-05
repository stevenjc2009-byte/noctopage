/* reader/bookld: relayout keeping the reader's place (checked against mdoc on this thread),
 * the latest style winning, the old generation readable while busy, wishes for an old
 * generation ignored, the cache cap and its drop on a new generation, a page pinned across
 * a generation change (ASan watches it), and close at every moment. */
#include "test.h"

#include "doc/mdoc.h"
#include "reader/bookld_priv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define FIX "fixtures/"
#define TMP "build-host-bookld-tmp2"
#define LONG TMP "/long.txt"
#define TIMEOUT 30.0
#define BLACK 0x000000u
#define WHITE 0xFFFFFFu

static const bookld_style_t SMALL = { 18, 24, 1.2f, 0 };
static const bookld_style_t BIG = { 30, 24, 1.2f, 0 };
static const bookld_style_t HUGE = { 40, 60, 1.5f, 1 };

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

static void nap(void) { usleep(2000); }

static void wait_gen(bookld_t *b, int gen, bookld_info_t *in)
{
	double end = now() + TIMEOUT;
	do {
		bookld_info(b, in);
		if (in->status == BOOKLD_FAILED) return;
		if (in->status == BOOKLD_READY && in->gen >= gen && !in->busy) return;
		nap();
	} while (now() < end);
}

static bookld_t *open_ready(const char *path, const bookld_style_t *st, bookld_info_t *in)
{
	bookld_t *b = bookld_open(path, st, NULL);
	CHECK(b != NULL);
	if (!b) return NULL;
	wait_gen(b, 1, in);
	CHECK_INT(in->status, BOOKLD_READY);
	CHECK_INT(in->gen, 1);
	CHECK(in->count >= 8);
	return b;
}

static const bookld_page_t *fetch(bookld_t *b, int gen, int page)
{
	bookld_want(b, gen, &page, 1, BLACK, WHITE);
	double end = now() + TIMEOUT;
	const bookld_page_t *p;
	while (!(p = bookld_get(b, gen, page, BLACK, WHITE)) && now() < end) {
		if (bookld_take_error(b, NULL, 0) >= 0) return NULL;
		nap();
	}
	if (!p) printf("timeout waiting for page %d of gen %d\n", page, gen);
	return p;
}

/* Position string of page (gen) via a render. */
static void pos_of(bookld_t *b, int gen, int page, char out[MDOC_POS_MAX])
{
	const bookld_page_t *p = fetch(b, gen, page);
	out[0] = 0;
	CHECK(p != NULL);
	if (!p) return;
	snprintf(out, MDOC_POS_MAX, "%s", p->pos);
	bookld_release(b, p);
}

/* What mdoc itself says, on this thread: the page holding pos laid out with st; *count. */
static int mdoc_page_of(const bookld_style_t *st, const char *pos, int *count)
{
	char err[160];
	mdoc_t *d = mdoc_open(LONG, err, sizeof err);
	if (!d) return -2;
	mdoc_layout_t lay = { BOOKLD_PAGE_W, BOOKLD_PAGE_H, (float)st->font_px,
	                      (float)st->margin_px, st->line_spacing, st->font_family };
	int r = mdoc_set_layout(d, &lay) == 0 ? mdoc_page_for_position(d, pos) : -3;
	if (count) *count = mdoc_page_count(d);
	mdoc_close(d);
	return r;
}

static void write_long_txt(void)
{
	mkdir(TMP, 0777);
	FILE *f = fopen(LONG, "wb");
	CHECK(f != NULL);
	if (!f) return;
	for (int i = 0; i < 400; i++)
		fprintf(f, "Paragraph %03d. The quick brown fox jumps over the lazy dog, again and "
		           "again, so that every page holds different words. End of %03d.\n\n", i, i);
	CHECK(fclose(f) == 0);
}

static void test_keep_place(void)
{
	bookld_info_t in;
	bookld_t *b = open_ready(LONG, &SMALL, &in);
	if (!b) return;
	int n1 = in.count, k = n1 * 2 / 3, n2 = 0;
	char pk[MDOC_POS_MAX], pa[MDOC_POS_MAX];
	pos_of(b, 1, k, pk);
	pos_of(b, 1, 1, pa);
	CHECK_INT(mdoc_page_of(&SMALL, pk, NULL), k);
	CHECK_INT(bookld_test_cache_count(b), 2);

	bookld_set_style(b, &BIG, k);
	bookld_info(b, &in);
	CHECK_INT(in.busy, 1); /* at once, before the worker even starts */
	wait_gen(b, 2, &in);
	CHECK_INT(in.gen, 2);
	CHECK_INT(in.busy, 0);
	CHECK(in.count >= n1);
	int want = mdoc_page_of(&BIG, pk, &n2);
	CHECK_INT(in.count, n2);
	CHECK(want > 0);
	CHECK_INT(in.anchor, want); /* the page now holding the text kept */
	CHECK_INT(bookld_test_cache_count(b), 0);
	CHECK(bookld_get(b, 1, k, BLACK, WHITE) == NULL);
	CHECK(bookld_get(b, 1, 1, BLACK, WHITE) == NULL);

	/* Pages of the new generation carry new-layout positions. */
	pos_of(b, 2, in.anchor, pa);
	CHECK_INT(mdoc_page_of(&BIG, pa, NULL), in.anchor);

	/* Wishes for generation 1 are ignored now: not rendered, and the current list stays. */
	int r0 = bookld_test_renders(b), pages[2] = { 0, 1 };
	int cur = in.anchor + 1 < in.count ? in.anchor + 1 : in.anchor - 1;
	bookld_test_hold(b, 1);
	bookld_want(b, 2, &cur, 1, BLACK, WHITE);
	bookld_want(b, 1, pages, 2, BLACK, WHITE);
	bookld_test_hold(b, 0);
	double end = now() + TIMEOUT;
	const bookld_page_t *p;
	while (!(p = bookld_get(b, 2, cur, BLACK, WHITE)) && now() < end) nap();
	CHECK(p != NULL);
	if (p) bookld_release(b, p);
	usleep(150000);
	CHECK_INT(bookld_test_renders(b), r0 + 1);
	CHECK_INT(bookld_test_cache_count(b), 2);

	/* Back to the small style keeping the anchor: the same text as page k came back. */
	bookld_set_style(b, &SMALL, in.anchor);
	wait_gen(b, 3, &in);
	CHECK_INT(in.gen, 3);
	CHECK_INT(in.count, n1);
	CHECK_INT(in.anchor, mdoc_page_of(&SMALL, pa, NULL));
	CHECK(in.anchor >= k - 1 && in.anchor <= k);
	bookld_close(b);
}

/* Two calls before the worker starts: one relayout, the latest style and keep_page. The old
 * generation stays readable meanwhile. */
static void test_latest_wins(void)
{
	bookld_info_t in;
	bookld_t *b = open_ready(LONG, &SMALL, &in);
	if (!b) return;
	int k = in.count / 2, n3 = 0;
	char pk[MDOC_POS_MAX];
	pos_of(b, 1, k, pk);
	bookld_test_hold(b, 1);
	bookld_set_style(b, &BIG, 3);
	bookld_set_style(b, &HUGE, k);
	usleep(50000);
	bookld_info(b, &in);
	CHECK(in.gen == 1 && in.busy == 1);
	const bookld_page_t *p = bookld_get(b, 1, k, BLACK, WHITE);
	CHECK(p != NULL);
	if (p) bookld_release(b, p);
	bookld_test_hold(b, 0);
	wait_gen(b, 2, &in);
	usleep(100000);
	bookld_info(b, &in);
	CHECK_INT(in.gen, 2);
	int want = mdoc_page_of(&HUGE, pk, &n3);
	CHECK_INT(in.count, n3);
	CHECK_INT(in.anchor, want);

	/* A second style while a relayout runs: it settles one generation later, in range. */
	bookld_set_style(b, &SMALL, in.anchor);
	usleep(1000);
	bookld_set_style(b, &BIG, in.anchor);
	wait_gen(b, 3, &in);
	CHECK(in.gen == 3 || in.gen == 4);
	CHECK(in.anchor >= 0 && in.anchor < in.count);
	bookld_close(b);
}

static void test_cache_cap(void)
{
	bookld_info_t in;
	bookld_t *b = open_ready(LONG, &SMALL, &in);
	if (!b) return;
	for (int i = 0; i < 10 && i < in.count; i++) {
		const bookld_page_t *p = fetch(b, 1, i);
		CHECK(p != NULL);
		if (p) CHECK(p->page == i && p->gen == 1);
		if (p) bookld_release(b, p);
		CHECK(bookld_test_cache_count(b) <= BOOKLD_CACHE_PAGES);
	}
	CHECK_INT(bookld_test_cache_count(b), BOOKLD_CACHE_PAGES);
	CHECK(bookld_get(b, 1, 0, BLACK, WHITE) == NULL); /* evicted, least recently used */
	CHECK(bookld_test_cache_bytes(b) <= BOOKLD_CACHE_PAGES * BOOKLD_PAGE_COST);

	/* Four wishes in one call all arrive; more than BOOKLD_WANT_MAX are clamped. */
	int four[6] = { 0, 1, 2, 3, 4, 5 };
	int r0 = bookld_test_renders(b);
	bookld_want(b, 1, four, 6, BLACK, WHITE);
	double end = now() + TIMEOUT;
	int got = 0;
	while (got < 4 && now() < end) {
		got = 0;
		for (int i = 0; i < 4; i++) {
			const bookld_page_t *p = bookld_get(b, 1, i, BLACK, WHITE);
			if (p) got++;
			if (p) bookld_release(b, p);
		}
		nap();
	}
	CHECK_INT(got, 4);
	usleep(100000);
	CHECK_INT(bookld_test_renders(b), r0 + 4); /* 0..3 (all evicted earlier), not 4 or 5 */
	bookld_close(b);
}

/* A page held across a generation change stays valid and is freed on release. */
static void test_pin_across_gen(void)
{
	bookld_info_t in;
	bookld_t *b = open_ready(LONG, &SMALL, &in);
	if (!b) return;
	const bookld_page_t *p = fetch(b, 1, 2);
	CHECK(p != NULL);
	bookld_set_style(b, &BIG, 2);
	wait_gen(b, 2, &in);
	CHECK_INT(in.gen, 2);
	CHECK_INT(bookld_test_cache_count(b), 0);
	if (p) {
		CHECK(p->gen == 1 && p->page == 2);
		size_t light = 0;
		for (size_t i = 0; i < (size_t)p->w * p->h; i++) light += p->rgba[i * 4] > 128;
		CHECK(light > 0); /* readable: ASan reports a use after free here otherwise */
		CHECK(bookld_test_cache_bytes(b) >= (size_t)BOOKLD_PAGE_W * BOOKLD_PAGE_H * 4);
		bookld_release(b, p);
	}
	CHECK_INT(bookld_test_cache_bytes(b), 0);
	bookld_close(b);
}

/* Close while opening, laying out, relaying out and rendering: no hang, no leak (ASan). */
static void test_close_any_time(void)
{
	double t0 = now();
	int four[4] = { 0, 1, 2, 3 };
	for (int i = 0; i < 20; i++) {
		bookld_t *b = bookld_open(i % 2 ? FIX "minimal.epub" : LONG, &SMALL, NULL);
		CHECK(b != NULL);
		if (i % 4 == 1) usleep((useconds_t)(i * 500));
		bookld_close(b);
	}
	for (int i = 0; i < 6; i++) {
		bookld_info_t in;
		bookld_t *b = open_ready(LONG, &SMALL, &in);
		if (!b) continue;
		if (i % 2) bookld_set_style(b, &HUGE, 3); /* close mid-relayout */
		bookld_want(b, 1, four, 4, BLACK, WHITE); /* close mid-render */
		usleep((useconds_t)(i * 3000));
		bookld_resolve(b, "r:0:0:0");
		bookld_close(b);
	}
	bookld_t *b = bookld_open(LONG, &SMALL, NULL);
	bookld_test_hold(b, 1);
	bookld_close(b); /* held: close still returns */
	CHECK(now() - t0 < 60.0);
	printf("close tests: %.0f ms\n", (now() - t0) * 1000);
}

/* set_style while OPENING: the first layout uses it (gen 1, no relayout). */
static void test_style_while_opening(void)
{
	bookld_info_t in;
	bookld_t *b = bookld_open(LONG, &SMALL, NULL);
	CHECK(b != NULL);
	if (!b) return;
	bookld_set_style(b, &BIG, 0);
	wait_gen(b, 1, &in);
	usleep(100000);
	bookld_info(b, &in);
	int n2 = 0;
	mdoc_page_of(&BIG, "r:0:0:0", &n2);
	CHECK(in.status == BOOKLD_READY && in.busy == 0);
	CHECK(in.gen == 1 || in.gen == 2); /* 2 when the call landed during the first layout */
	CHECK_INT(in.count, n2);
	bookld_close(b);
}

int main(void)
{
	if (mdoc_global_init() != 0) {
		printf("mdoc_global_init failed\n");
		return 1;
	}
	write_long_txt();
	test_keep_place();
	test_latest_wins();
	test_cache_cap();
	test_pin_across_gen();
	test_close_any_time();
	test_style_while_opening();
	mdoc_global_shutdown();
	return TEST_DONE("bookld_more");
}
