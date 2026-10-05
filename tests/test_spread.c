#include "test.h"

#include "reader/spread.h"

#include <string.h>

#define MAXP 16

/* Shape string: P = portrait 800x1200, W = wide 1200x800, S = square 1000x1000. */
static int build_str(const char *shape, int offset, spread_t *out)
{
	int w[MAXP], h[MAXP], n = (int)strlen(shape), i;
	for (i = 0; i < n; i++) {
		w[i] = shape[i] == 'P' ? 800 : (shape[i] == 'W' ? 1200 : 1000);
		h[i] = shape[i] == 'P' ? 1200 : (shape[i] == 'W' ? 800 : 1000);
	}
	return spread_build(w, h, n, offset, out, MAXP);
}

/* Render spreads as "0,1|2|3,4" in page-index order for easy comparison. */
static void render(const spread_t *s, int ns, char *buf)
{
	int i, p = 0;
	for (i = 0; i < ns; i++) {
		if (i)
			buf[p++] = '|';
		buf[p++] = (char)('a' + s[i].first);
		if (s[i].count == 2)
			buf[p++] = (char)('a' + s[i].first + 1);
	}
	buf[p] = 0;
}

static void expect(const char *shape, int offset, const char *want)
{
	spread_t s[MAXP];
	char got[64];
	int ns = build_str(shape, offset, s);
	render(s, ns, got);
	g_checks++;
	if (strcmp(got, want) != 0) {
		g_failed++;
		printf("FAIL %s offset=%d: got \"%s\" wanted \"%s\"\n", shape, offset, got, want);
	}
}

static void test_tables(void)
{
	expect("", 0, "");
	expect("P", 0, "a");
	expect("P", 1, "a");
	expect("PP", 0, "ab");
	expect("PP", 1, "a|b");
	expect("PPPPPP", 0, "ab|cd|ef");
	expect("PPPPP", 0, "ab|cd|e");
	expect("PPPPPP", 1, "a|bc|de|f");
	expect("PPPPP", 1, "a|bc|de");
	/* Wide pages are always alone. */
	expect("W", 0, "a");
	expect("WP", 0, "a|b");
	expect("PW", 0, "a|b");
	expect("WW", 0, "a|b");
	expect("WPP", 0, "a|bc");
	expect("WPP", 1, "a|bc");
	/* Wide page in the middle: pairing re-syncs on the page right after it. */
	expect("PPWPPPP", 0, "ab|c|de|fg");
	expect("PWPPPPP", 0, "a|b|cd|ef|g");
	expect("PWPPPPP", 1, "a|b|cd|ef|g");
	expect("PPPWPPP", 0, "ab|c|d|ef|g");
	expect("PPPWPPP", 1, "a|bc|d|ef|g");
	expect("PPWWPP", 0, "ab|c|d|ef");
	/* With the offset, a lone portrait page next to a wide page stays alone. */
	expect("PPWPPP", 1, "a|b|c|de|f");
	/* A wide cover is alone either way. */
	expect("WPPP", 1, "a|bc|d");
	/* Square pages are portrait-like (w > h is false), so they pair. */
	expect("SS", 0, "ab");
	expect("SPS", 0, "ab|c");
	expect("PSW", 0, "ab|c");
	/* Exactly-one-pixel-wider is wide. */
	{
		int w[2] = { 1001, 800 }, h[2] = { 1000, 1200 };
		spread_t s[2];
		CHECK_INT(spread_build(w, h, 2, 0, s, 2), 2);
		w[0] = 1000;
		CHECK_INT(spread_build(w, h, 2, 0, s, 2), 1);
	}
}

/* Every shape string of length 1..10, both offsets: structural invariants. */
static void test_exhaustive(void)
{
	int n, mask, offset, bad = 0, runs = 0;

	for (n = 1; n <= 10; n++)
		for (mask = 0; mask < (1 << n); mask++)
			for (offset = 0; offset <= 1; offset++) {
				char shape[MAXP + 1];
				spread_t s[MAXP];
				int ns, i, next = 0;
				for (i = 0; i < n; i++)
					shape[i] = (mask >> i) & 1 ? 'W' : 'P';
				shape[n] = 0;
				ns = build_str(shape, offset, s);
				runs++;
				for (i = 0; i < ns; i++) {
					if (s[i].first != next || s[i].count < 1 || s[i].count > 2)
						bad++;
					if (s[i].count == 2) {
						/* never a wide page, never the offset cover */
						if (shape[s[i].first] == 'W' || shape[s[i].first + 1] == 'W')
							bad++;
						if (offset && s[i].first == 0)
							bad++;
					}
					/* maximal pairing: a lone portrait page is followed by a wide page,
					 * the end, or (cover) nothing else */
					if (s[i].count == 1 && shape[s[i].first] == 'P' &&
					    !(offset && s[i].first == 0) && s[i].first + 1 < n &&
					    shape[s[i].first + 1] != 'W')
						bad++;
					next += s[i].count;
					if (spread_of_page(s, ns, s[i].first) != i ||
					    spread_of_page(s, ns, s[i].first + s[i].count - 1) != i)
						bad++;
				}
				if (next != n)
					bad++;
			}
	CHECK_INT(runs, 2 * ((1 << 11) - 2));
	CHECK_INT(bad, 0);
}

static void test_lookup_and_rtl(void)
{
	spread_t s[MAXP];
	int ns = build_str("PPWPP", 1, s); /* a | b | c | de */
	int out[2];

	CHECK_INT(ns, 4);
	CHECK_INT(spread_of_page(s, ns, 0), 0);
	CHECK_INT(spread_of_page(s, ns, 1), 1);
	CHECK_INT(spread_of_page(s, ns, 2), 2);
	CHECK_INT(spread_of_page(s, ns, 3), 3);
	CHECK_INT(spread_of_page(s, ns, 4), 3);
	CHECK_INT(spread_of_page(s, ns, 5), -1);
	CHECK_INT(spread_of_page(s, ns, -1), -1);
	CHECK_INT(spread_of_page(s, 0, 0), -1);

	/* single pages: same in both directions */
	CHECK_INT(spread_pages(s, ns, 0, 0, out), 1);
	CHECK_INT(out[0], 0);
	CHECK_INT(spread_pages(s, ns, 0, 1, out), 1);
	CHECK_INT(out[0], 0);
	/* pair de: LTR d on the left, RTL d (first page) on the right */
	CHECK_INT(spread_pages(s, ns, 3, 0, out), 2);
	CHECK_INT(out[0], 3);
	CHECK_INT(out[1], 4);
	CHECK_INT(spread_pages(s, ns, 3, 1, out), 2);
	CHECK_INT(out[0], 4);
	CHECK_INT(out[1], 3);
	CHECK_INT(spread_pages(s, ns, 4, 0, out), 0);
	CHECK_INT(spread_pages(s, ns, -1, 0, out), 0);
}

static void test_build_limits(void)
{
	int w[6] = { 800, 800, 800, 800, 800, 800 }, h[6] = { 1200, 1200, 1200, 1200, 1200, 1200 };
	spread_t s[2] = { { -1, -1 }, { -1, -1 } };

	CHECK_INT(spread_build(w, h, 6, 0, NULL, 0), 3); /* count only */
	CHECK_INT(spread_build(w, h, 6, 0, s, 2), 3);    /* more than max: total returned */
	CHECK_INT(s[1].first, 2);
	CHECK_INT(spread_build(w, h, 0, 0, s, 2), 0);
	CHECK_INT(spread_build(NULL, h, 6, 0, s, 2), 0);
}

int main(void)
{
	test_tables();
	test_exhaustive();
	test_lookup_and_rtl();
	test_build_limits();
	return TEST_DONE("spread");
}
