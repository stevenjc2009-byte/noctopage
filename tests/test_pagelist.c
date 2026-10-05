#include "test.h"

#include "comic/pagelist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* Builds the list and compares the resulting names with want (NULL-terminated). */
static void expect_order(int line, const char *const *names, int n, const char *const *want)
{
	int idx[64], wn = 0, got, i, ok = 1;

	while (want[wn])
		wn++;
	got = pagelist_build(names, n, idx);
	g_checks++;
	if (got != wn)
		ok = 0;
	for (i = 0; ok && i < wn; i++)
		if (strcmp(names[idx[i]], want[i]) != 0)
			ok = 0;
	if (!ok) {
		g_failed++;
		printf("FAIL %s:%d: got %d pages:", __FILE__, line, got);
		for (i = 0; i < got && i < 64; i++)
			printf(" [%s]", names[idx[i]]);
		printf("\n  wanted %d pages:", wn);
		for (i = 0; i < wn; i++)
			printf(" [%s]", want[i]);
		printf("\n");
	}
}

#define EXPECT(names, ...)                                                                   \
	do {                                                                                     \
		static const char *const want_[] = { __VA_ARGS__, NULL };                            \
		expect_order(__LINE__, (names), COUNT(names), want_);                                \
	} while (0)

static void test_fixture_natural_order(void)
{
	/* tests/fixtures/natural_order.cbz archive order -> manifest natural order */
	static const char *const n[] = { "p1.png", "p10.png", "p2.png", "p3.png", "p9.png" };
	EXPECT(n, "p1.png", "p2.png", "p3.png", "p9.png", "p10.png");
}

static void test_fixture_mixed_case_junk(void)
{
	/* tests/fixtures/manifest.json mixed_case_junk.cbz: the junk must vanish. */
	static const char *const n[] = { "page1.png", "__MACOSX/._page1.png", "Thumbs.db", "Page10.png",
	                                 "notes/readme.txt", "page2.PNG", "Cover.JPG" };
	EXPECT(n, "Cover.JPG", "page1.png", "page2.PNG", "Page10.png");
}

static void test_indices_point_at_input(void)
{
	static const char *const n[] = { "b2.png", "junk.txt", "b10.png", "b1.png" };
	int idx[4];
	CHECK_INT(pagelist_build(n, 4, idx), 3);
	CHECK_INT(idx[0], 3);
	CHECK_INT(idx[1], 0);
	CHECK_INT(idx[2], 2);
}

static void test_subdirs_and_full_path_order(void)
{
	static const char *const n[] = { "ch10/p1.jpg", "ch2/p10.jpg", "ch2/p2.jpg", "ch1/p1.jpg",
	                                 "ch2/p1.jpg", "ch1/p20.jpg", "ch1/p3.jpg" };
	EXPECT(n, "ch1/p1.jpg", "ch1/p3.jpg", "ch1/p20.jpg", "ch2/p1.jpg", "ch2/p2.jpg",
	       "ch2/p10.jpg", "ch10/p1.jpg");
}

static void test_case_insensitive_order(void)
{
	static const char *const n[] = { "B.jpg", "a.jpg", "C.JPG", "b2.jpg", "A3.jpg" };
	EXPECT(n, "a.jpg", "A3.jpg", "B.jpg", "b2.jpg", "C.JPG");
}

static void test_junk_filters(void)
{
	static const char *const n[] = {
		"__MACOSX/a/001.jpg",   /* under __MACOSX */
		"x/__MACOSX/002.jpg",   /* nested __MACOSX */
		"__macosx/003.jpg",     /* case-insensitive */
		".hidden/004.jpg",      /* hidden directory */
		"a/.005.jpg",           /* hidden file */
		"a/._006.jpg",          /* AppleDouble */
		"../007.jpg",           /* traversal */
		"a/../008.jpg",
		"Thumbs.db", "thumbs.db", "a/Thumbs.db",
		"notes.txt", "pages/", "pages", "cover.cbz", "",
		"__MACOSXX/keep.jpg",   /* only the exact component is dropped */
		"_MACOSX/keep2.jpg",
		"ok.jpg",
	};
	EXPECT(n, "__MACOSXX/keep.jpg", "_MACOSX/keep2.jpg", "ok.jpg");
}

static void test_dot_and_empty_components(void)
{
	static const char *const n[] = { "./p2.png", "./p1.png", "./sub/p3.png", "a//p4.png",
	                                 "/abs/p5.png" };
	EXPECT(n, "./p1.png", "./p2.png", "./sub/p3.png", "/abs/p5.png", "a//p4.png");
}

static void test_japanese(void)
{
	static const char *const n[] = { "第10話/01.jpg", "第1話/10.jpg", "第1話/02.png", "第1話/01.PNG",
	                                 "第2話/01.webp", "表紙.jpg", "__MACOSX/第1話/01.jpg" };
	/* Code-point order puts the digit directories (ASCII) before CJK names; within a chapter
	 * the digit runs are numeric. */
	EXPECT(n, "第1話/01.PNG", "第1話/02.png", "第1話/10.jpg", "第2話/01.webp", "第10話/01.jpg",
	       "表紙.jpg");
}

static void test_image_types(void)
{
	static const char *const n[] = { "1.jpg", "2.jpeg", "3.png", "4.gif", "5.bmp", "6.webp",
	                                 "7.tif", "8.tiff", "9.svg", "10.pdf", "11.avif" };
	EXPECT(n, "1.jpg", "2.jpeg", "3.png", "4.gif", "5.bmp", "6.webp", "7.tif", "8.tiff");
}

static void test_duplicates_keep_input_order(void)
{
	static const char *const n[] = { "p1.png", "p1.png", "p1.png", "p0.png", "p1.png" };
	int idx[5];
	CHECK_INT(pagelist_build(n, 5, idx), 5);
	CHECK_INT(idx[0], 3);
	CHECK_INT(idx[1], 0);
	CHECK_INT(idx[2], 1);
	CHECK_INT(idx[3], 2);
	CHECK_INT(idx[4], 4);
}

static void test_edges(void)
{
	static const char *const n[] = { "a.png", NULL, "b.png" };
	static const char *const junk[] = { "a.txt", "Thumbs.db", "__MACOSX/x.png" };
	int idx[3];

	CHECK_INT(pagelist_build(n, 3, idx), 2);
	CHECK_INT(idx[0], 0);
	CHECK_INT(idx[1], 2);
	CHECK_INT(pagelist_build(junk, 3, idx), 0);
	CHECK_INT(pagelist_build(n, 0, idx), 0);
	CHECK_INT(pagelist_build(n, -1, idx), 0);
	CHECK_INT(pagelist_build(NULL, 3, idx), 0);
	CHECK_INT(pagelist_build(n, 3, NULL), 0);
	{
		static const char *const one[] = { "only.jpg" };
		CHECK_INT(pagelist_build(one, 1, idx), 1);
		CHECK_INT(idx[0], 0);
	}
}

/* 2000 pages in a scrambled order, with junk mixed in: the heapsort must give numeric order. */
static void test_large_scrambled(void)
{
	enum { PAGES = 2000, JUNK = 100, N = PAGES + JUNK };
	static char store[N][32];
	static const char *names[N];
	static int idx[N];
	int i, got, ok = 1;

	for (i = 0; i < PAGES; i++) {
		int page = (int)(((long)i * 977 + 13) % PAGES) + 1; /* 977 is coprime with 2000 */
		snprintf(store[i], sizeof store[i], "scan/p%d.%s", page, (i % 3) ? "jpg" : "PNG");
		names[i] = store[i];
	}
	for (i = PAGES; i < N; i++) {
		snprintf(store[i], sizeof store[i], "__MACOSX/p%d.jpg", i);
		names[i] = store[i];
	}
	got = pagelist_build(names, N, idx);
	CHECK_INT(got, PAGES);
	for (i = 0; i < got && ok; i++) {
		int page = atoi(names[idx[i]] + 6);
		if (page != i + 1) {
			ok = 0;
			printf("FAIL %s:%d: position %d holds page %d\n", __FILE__, __LINE__, i, page);
		}
	}
	CHECK(ok);
	/* Already-reversed input too. */
	for (i = 0; i < PAGES; i++) {
		snprintf(store[i], sizeof store[i], "p%d.jpg", PAGES - i);
		names[i] = store[i];
	}
	got = pagelist_build(names, PAGES, idx);
	CHECK_INT(got, PAGES);
	ok = 1;
	for (i = 0; i < got; i++)
		if (atoi(names[idx[i]] + 1) != i + 1)
			ok = 0;
	CHECK(ok);
}

int main(void)
{
	test_fixture_natural_order();
	test_fixture_mixed_case_junk();
	test_indices_point_at_input();
	test_subdirs_and_full_path_order();
	test_case_insensitive_order();
	test_junk_filters();
	test_dot_and_empty_components();
	test_japanese();
	test_image_types();
	test_duplicates_keep_input_order();
	test_edges();
	test_large_scrambled();
	return TEST_DONE("pagelist");
}
