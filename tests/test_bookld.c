/* reader/bookld on the fixtures: EPUB / FB2 / TXT open to READY gen 1, page 0 rendered at
 * 960x544 and not blank, ink/paper honoured both ways, each page carrying its position,
 * resume on open, bookld_resolve, the TOC copy, a missing file, a PDF (fixed layout: pages
 * fitted to the screen, styles ignored). Relayout, the cache, pins and close: test_bookld_more.c. */
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
#define TMP "build-host-bookld-tmp1"
#define LONG TMP "/long.txt"
#define TIMEOUT 30.0
#define BLACK 0x000000u
#define WHITE 0xFFFFFFu

static const bookld_style_t STYLE = { 18, 24, 1.2f, 0 };

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

static void nap(void) { usleep(2000); }

/* Info once READY with gen >= gen and nothing pending (or whatever it is at the timeout). */
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

static bookld_t *open_ready(const char *path, const char *resume, bookld_info_t *in)
{
	bookld_t *b = bookld_open(path, &STYLE, resume);
	CHECK(b != NULL);
	if (!b) return NULL;
	wait_gen(b, 1, in);
	CHECK_INT(in->status, BOOKLD_READY);
	if (in->status != BOOKLD_READY) printf("  %s: %s\n", path, in->err);
	CHECK_INT(in->gen, 1);
	CHECK(in->count > 0);
	return b;
}

/* Wish one page and poll for it; NULL on timeout or a reported failure. */
static const bookld_page_t *fetch(bookld_t *b, int gen, int page, uint32_t ink, uint32_t paper)
{
	bookld_want(b, gen, &page, 1, ink, paper);
	double end = now() + TIMEOUT;
	const bookld_page_t *p;
	char err[160];
	while (!(p = bookld_get(b, gen, page, ink, paper)) && now() < end) {
		if (bookld_take_error(b, err, sizeof err) >= 0) {
			printf("page %d failed: %s\n", page, err);
			return NULL;
		}
		nap();
	}
	if (!p) printf("timeout waiting for page %d\n", page);
	return p;
}

/* Distinct RGB values (stops counting at 8). */
static int colours(const bookld_page_t *p)
{
	uint32_t seen[8];
	int n = 0;
	for (size_t i = 0; i < (size_t)p->w * p->h && n < 8; i++) {
		const uint8_t *q = p->rgba + i * 4;
		uint32_t c = (uint32_t)q[0] << 16 | (uint32_t)q[1] << 8 | q[2];
		int k = 0;
		while (k < n && seen[k] != c) k++;
		if (k == n) seen[n++] = c;
	}
	return n;
}

/* Per mille of pixels with every channel below 64. */
static int dark_permille(const bookld_page_t *p)
{
	size_t n = (size_t)p->w * p->h, dark = 0;
	for (size_t i = 0; i < n; i++) {
		const uint8_t *q = p->rgba + i * 4;
		dark += q[0] < 64 && q[1] < 64 && q[2] < 64;
	}
	return (int)(dark * 1000 / n);
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

static void test_open_render(const char *path)
{
	bookld_info_t in;
	bookld_t *b = open_ready(path, NULL, &in);
	if (!b) return;
	CHECK_INT(in.anchor, 0);
	CHECK_INT(in.busy, 0);
	const bookld_page_t *p = fetch(b, 1, 0, BLACK, WHITE);
	CHECK(p != NULL);
	if (p) {
		CHECK_INT(p->w, BOOKLD_PAGE_W);
		CHECK_INT(p->h, BOOKLD_PAGE_H);
		CHECK_INT(p->gen, 1);
		CHECK_INT(p->page, 0);
		CHECK(p->ink == BLACK && p->paper == WHITE);
		CHECK(colours(p) >= 2);          /* not blank */
		CHECK(dark_permille(p) < 300);   /* black text on white paper: mostly light */
		CHECK(p->pos[0] != 0);
		bookld_release(b, p);
	}
	/* The black theme: the same page is mostly dark, and a separate cache entry. */
	p = fetch(b, 1, 0, WHITE, BLACK);
	CHECK(p != NULL);
	if (p) {
		CHECK(colours(p) >= 2);
		CHECK(dark_permille(p) > 700);
		CHECK(p->ink == WHITE && p->paper == BLACK);
		bookld_release(b, p);
	}
	CHECK_INT(bookld_test_cache_count(b), 2);
	CHECK_INT(bookld_take_error(b, NULL, 0), -1);
	bookld_close(b);
}

static void test_missing(void)
{
	bookld_info_t in;
	bookld_t *b = bookld_open(FIX "no_such_book.epub", &STYLE, NULL);
	CHECK(b != NULL);
	if (!b) return;
	wait_gen(b, 1, &in);
	CHECK_INT(in.status, BOOKLD_FAILED);
	CHECK(in.err[0] != 0);
	CHECK_INT(in.gen, 0);
	CHECK_INT(in.count, 0);
	int page = 0;
	bookld_want(b, 0, &page, 1, BLACK, WHITE); /* all harmless */
	CHECK(bookld_get(b, 0, 0, BLACK, WHITE) == NULL);
	mdoc_toc_item_t t[2];
	CHECK_INT(bookld_toc(b, t, 2), 0);
	bookld_set_style(b, &STYLE, 0);
	bookld_info(b, &in);
	CHECK_INT(in.busy, 0);
	bookld_close(b);
}

/* Positions: the pos of page k reopens on k, resolves to k; junk resumes on 0. */
static void test_resume_resolve(void)
{
	bookld_info_t in;
	bookld_t *b = open_ready(LONG, NULL, &in);
	if (!b) return;
	CHECK(in.count >= 8);
	int k = in.count / 2;
	char pos[MDOC_POS_MAX] = "", pos0[MDOC_POS_MAX] = "";
	const bookld_page_t *p = fetch(b, 1, k, BLACK, WHITE);
	CHECK(p != NULL);
	if (p) {
		snprintf(pos, sizeof pos, "%s", p->pos);
		bookld_release(b, p);
	}
	p = fetch(b, 1, 0, BLACK, WHITE);
	if (p) {
		snprintf(pos0, sizeof pos0, "%s", p->pos);
		bookld_release(b, p);
	}
	CHECK(pos[0] && strcmp(pos, pos0) != 0);

	CHECK_INT(in.jump_serial, 0);
	bookld_resolve(b, pos);
	double end = now() + TIMEOUT;
	while ((bookld_info(b, &in), in.jump_serial) < 1 && now() < end) nap();
	CHECK_INT(in.jump_serial, 1);
	CHECK_INT(in.jump_page, k);
	bookld_resolve(b, "r:1:x");
	while ((bookld_info(b, &in), in.jump_serial) < 2 && now() < end) nap();
	CHECK_INT(in.jump_serial, 2);
	CHECK_INT(in.jump_page, -1);
	bookld_resolve(b, pos0);
	while ((bookld_info(b, &in), in.jump_serial) < 3 && now() < end) nap();
	CHECK_INT(in.jump_page, 0);
	bookld_close(b);

	b = open_ready(LONG, pos, &in);
	if (b) CHECK_INT(in.anchor, k);
	bookld_close(b);
	b = open_ready(LONG, "r:1:x", &in);
	if (b) CHECK_INT(in.anchor, 0);
	bookld_close(b);
	b = open_ready(LONG, "", &in);
	if (b) CHECK_INT(in.anchor, 0);
	bookld_close(b);
}

/* The TOC copy equals mdoc_toc of the same layout, read on this thread. min: items the
 * fixture must give (minimal.epub has only an EPUB 3 nav, which MuPDF does not read: 0). */
static void test_toc(const char *path, int min)
{
	bookld_info_t in;
	bookld_t *b = open_ready(path, NULL, &in);
	if (!b) return;
	mdoc_toc_item_t got[BOOKLD_TOC_MAX], want[BOOKLD_TOC_MAX];
	int n = bookld_toc(b, got, BOOKLD_TOC_MAX);
	char err[160] = "";
	mdoc_t *d = mdoc_open(path, err, sizeof err);
	CHECK(d != NULL);
	if (d) {
		mdoc_layout_t lay = { BOOKLD_PAGE_W, BOOKLD_PAGE_H, (float)STYLE.font_px,
		                      (float)STYLE.margin_px, STYLE.line_spacing, STYLE.font_family };
		CHECK_INT(mdoc_set_layout(d, &lay), 0);
		CHECK_INT(mdoc_page_count(d), in.count);
		int m = mdoc_toc(d, want, BOOKLD_TOC_MAX);
		CHECK(m >= min);
		CHECK_INT(n, m);
		for (int i = 0; i < n && i < m; i++) {
			CHECK_STR(got[i].title, want[i].title);
			CHECK_INT(got[i].page, want[i].page);
			CHECK_INT(got[i].depth, want[i].depth);
		}
		mdoc_close(d);
	}
	CHECK_INT(bookld_toc(b, got, 1), n > 0 ? 1 : 0);
	bookld_close(b);
}

/* Fixed layout: pages fitted into 960x544 on paper, set_style a no-op (gen stays 1). */
static void test_pdf(void)
{
	bookld_info_t in;
	bookld_t *b = open_ready(FIX "two_pages.pdf", NULL, &in);
	if (!b) return;
	CHECK_INT(in.count, 2);
	const bookld_page_t *p = fetch(b, 1, 1, BLACK, WHITE);
	CHECK(p != NULL);
	if (p) {
		CHECK(p->w == BOOKLD_PAGE_W && p->h == BOOKLD_PAGE_H);
		CHECK(colours(p) >= 2);
		CHECK(p->pos[0] != 0);
		bookld_release(b, p);
	}
	bookld_style_t big = STYLE;
	big.font_px = 40;
	bookld_set_style(b, &big, 1);
	bookld_info(b, &in);
	CHECK_INT(in.busy, 0);
	usleep(100000);
	bookld_info(b, &in);
	CHECK_INT(in.gen, 1);
	CHECK_INT(bookld_test_cache_count(b), 1);
	bookld_close(b);
}

static void test_clamp(void)
{
	bookld_style_t s = { 2, 500, 9.0f, 7 };
	bookld_style_clamp(&s);
	CHECK(s.font_px == 12 && s.margin_px == 120 && s.line_spacing == 2.0f && s.font_family == 1);
	s = (bookld_style_t){ 99, -4, 0.2f, -1 };
	bookld_style_clamp(&s);
	CHECK(s.font_px == 48 && s.margin_px == 0 && s.line_spacing == 1.0f && s.font_family == 0);
}

int main(void)
{
	if (mdoc_global_init() != 0) {
		printf("mdoc_global_init failed\n");
		return 1;
	}
	write_long_txt();
	test_clamp();
	test_open_render(FIX "minimal.epub");
	test_open_render(FIX "minimal.fb2");
	test_open_render(FIX "minimal.txt");
	test_missing();
	test_resume_resolve();
	test_toc(FIX "minimal.epub", 0);
	test_toc(FIX "minimal.fb2", 2);
	test_pdf();
	mdoc_global_shutdown();
	return TEST_DONE("bookld");
}
