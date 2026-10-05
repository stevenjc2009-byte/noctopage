#include "test.h"

#include "reader/fit.h"

#include <math.h>

#define SW 960
#define SH 544

#define CHECK_NEAR(got, want)                                                          \
	do {                                                                               \
		double g__ = (got), w__ = (want);                                              \
		g_checks++;                                                                    \
		if (fabs(g__ - w__) > 1e-3) {                                                  \
			g_failed++;                                                                \
			printf("FAIL %s:%d: %s == %.4f, wanted %.4f\n", __FILE__, __LINE__, #got,  \
			       g__, w__);                                                          \
		}                                                                              \
	} while (0)

/* ---- vertical strip --------------------------------------------------------------------- */

static const int SW_[4] = { 480, 960, 1920, 640 };
static const int SH_[4] = { 1000, 1500, 2000, 800 };

static void test_strip_layout(void)
{
	strip_item_t it[4];
	float total = strip_layout(SW_, SH_, 4, SW, 1.0f, 0, it);
	int first, last;

	CHECK_NEAR(total, 5700);
	CHECK_NEAR(it[0].top, 0);
	CHECK_NEAR(it[0].height, 2000); /* scale 2 */
	CHECK_NEAR(it[1].top, 2000);
	CHECK_NEAR(it[1].height, 1500); /* scale 1 */
	CHECK_NEAR(it[2].top, 3500);
	CHECK_NEAR(it[2].height, 1000); /* scale 0.5 */
	CHECK_NEAR(it[3].top, 4500);
	CHECK_NEAR(it[3].height, 1200); /* scale 1.5 */

	CHECK_INT(strip_visible(it, 4, 0, SH, &first, &last), 1);
	CHECK_INT(first, 0);
	CHECK_INT(last, 0);
	CHECK_INT(strip_visible(it, 4, 1900, SH, &first, &last), 2);
	CHECK_INT(first, 0);
	CHECK_INT(last, 1);
	/* touching an edge is not intersecting: viewport [1456,2000) ends where page 1 begins */
	CHECK_INT(strip_visible(it, 4, 1456, SH, &first, &last), 1);
	CHECK_INT(first, 0);
	CHECK_INT(last, 0);
	/* viewport starting exactly at page 0's bottom edge */
	CHECK_INT(strip_visible(it, 4, 2000, SH, &first, &last), 1);
	CHECK_INT(first, 1);
	CHECK_INT(last, 1);
	CHECK_INT(strip_visible(it, 4, 3000, SH, &first, &last), 2);
	CHECK_INT(first, 1);
	CHECK_INT(last, 2);
	CHECK_INT(strip_visible(it, 4, 5156, SH, &first, &last), 1);
	CHECK_INT(first, 3);
	CHECK_INT(last, 3);
	CHECK_INT(strip_visible(it, 4, 0, 6000, &first, &last), 4);
	CHECK_INT(first, 0);
	CHECK_INT(last, 3);
	CHECK_INT(strip_visible(it, 4, 6000, SH, &first, &last), 0);
	CHECK_INT(first, -1);
	CHECK_INT(last, -1);
	CHECK_INT(strip_visible(it, 4, -544, SH, &first, &last), 0); /* wholly above the strip */

	/* gap between pages, none after the last */
	total = strip_layout(SW_, SH_, 4, SW, 1.0f, 10, it);
	CHECK_NEAR(total, 5730);
	CHECK_NEAR(it[1].top, 2010);
	CHECK_NEAR(it[2].top, 3520);
	CHECK_NEAR(it[3].top, 4530);
	CHECK_INT(strip_visible(it, 4, 2000, SH, &first, &last), 1); /* page 0 ends at 2000 */
	CHECK_INT(first, 1);
	CHECK_INT(last, 1);
	CHECK_INT(strip_visible(it, 4, 1990, SH, &first, &last), 2);
	CHECK_INT(first, 0);
	CHECK_INT(last, 1);
	/* zoom 2: everything doubles */
	total = strip_layout(SW_, SH_, 4, SW, 2.0f, 0, it);
	CHECK_NEAR(total, 11400);
	CHECK_NEAR(it[3].top, 9000);
	/* zoom is clamped like the page zoom */
	total = strip_layout(SW_, SH_, 4, SW, 100.0f, 0, it);
	CHECK_NEAR(total, 5700 * 8);
	total = strip_layout(SW_, SH_, 4, SW, 0.1f, 0, it);
	CHECK_NEAR(total, 5700);

	/* scroll clamping */
	CHECK_NEAR(strip_clamp_scroll(5700, SH, -10), 0);
	CHECK_NEAR(strip_clamp_scroll(5700, SH, 100), 100);
	CHECK_NEAR(strip_clamp_scroll(5700, SH, 5156), 5156);
	CHECK_NEAR(strip_clamp_scroll(5700, SH, 99999), 5156);
	CHECK_NEAR(strip_clamp_scroll(300, SH, 50), 0); /* shorter than the screen */
}

static void test_strip_page_at(void)
{
	strip_item_t it[4];
	strip_layout(SW_, SH_, 4, SW, 1.0f, 10, it);
	CHECK_INT(strip_page_at(it, 4, -5), 0);
	CHECK_INT(strip_page_at(it, 4, 0), 0);
	CHECK_INT(strip_page_at(it, 4, 2005), 0); /* in the gap: belongs to the page above */
	CHECK_INT(strip_page_at(it, 4, 2010), 1);
	CHECK_INT(strip_page_at(it, 4, 3519.5f), 1);
	CHECK_INT(strip_page_at(it, 4, 3520), 2);
	CHECK_INT(strip_page_at(it, 4, 99999), 3);
	CHECK_INT(strip_page_at(it, 0, 10), -1);
}

/* Compare strip_visible with a brute-force scan over a long, irregular strip. */
static void test_strip_vs_bruteforce(void)
{
	enum { N = 3000 };
	static int w[N], h[N];
	static strip_item_t it[N];
	unsigned seed = 12345;
	int i, s, bad = 0, checked = 0;
	float total;

	for (i = 0; i < N; i++) {
		seed = seed * 1103515245u + 12345u;
		w[i] = 300 + (int)((seed >> 16) % 1500);
		seed = seed * 1103515245u + 12345u;
		h[i] = 300 + (int)((seed >> 16) % 2500);
	}
	w[17] = 0; /* zero-height pages are skipped */
	h[400] = 0;
	total = strip_layout(w, h, N, SW, 1.0f, 4, it);
	CHECK_NEAR(it[17].height, 0);
	for (s = -100; s < (int)total + 600; s += 211) {
		int first, last, bf_first = -1, bf_last = -1, bf_count = 0, n;
		for (i = 0; i < N; i++)
			if (it[i].height > 0 && it[i].top < s + SH && it[i].top + it[i].height > s) {
				if (bf_first < 0)
					bf_first = i;
				bf_last = i;
				bf_count++;
			}
		n = strip_visible(it, N, (float)s, SH, &first, &last);
		if (n != bf_count || first != bf_first || last != bf_last)
			bad++;
		checked++;
	}
	CHECK(checked > 100);
	CHECK_INT(bad, 0);
}

int main(void)
{
	test_strip_layout();
	test_strip_page_at();
	test_strip_vs_bruteforce();
	return TEST_DONE("fit_strip");
}
