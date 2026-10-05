#include "test.h"

#include "lib/formats.h"

#include <string.h>

static void kind_is(const char *path, fmt_kind_t want)
{
	fmt_kind_t got = formats_kind_for_path(path);
	g_checks++;
	if (got != want) {
		g_failed++;
		printf("FAIL %s:%d: kind(\"%s\") == %d, wanted %d\n", __FILE__, __LINE__, path, (int)got,
		       (int)want);
	}
}

static void test_every_extension(void)
{
	static const char *const archive[] = { "cbz", "zip", "cbr", "rar", "cb7", "7z", "cbt", "tar" };
	static const char *const reflow[] = { "epub", "mobi", "fb2", "txt", "html", "htm", "xhtml" };
	static const char *const fixed[] = { "pdf", "xps", "oxps", "svg" };
	char p[64];
	size_t i;

	for (i = 0; i < sizeof archive / sizeof *archive; i++) {
		snprintf(p, sizeof p, "ux0:books/a.%s", archive[i]);
		kind_is(p, FMT_COMIC_ARCHIVE);
	}
	for (i = 0; i < sizeof reflow / sizeof *reflow; i++) {
		snprintf(p, sizeof p, "ux0:books/a.%s", reflow[i]);
		kind_is(p, FMT_REFLOW);
	}
	for (i = 0; i < sizeof fixed / sizeof *fixed; i++) {
		snprintf(p, sizeof p, "ux0:books/a.%s", fixed[i]);
		kind_is(p, FMT_FIXED);
	}
}

static void test_case_and_paths(void)
{
	kind_is("a.CBZ", FMT_COMIC_ARCHIVE);
	kind_is("a.CbZ", FMT_COMIC_ARCHIVE);
	kind_is("ux0:Books/Vol 1.EPUB", FMT_REFLOW);
	kind_is("ux0:Books/x.PdF", FMT_FIXED);
	kind_is("a.b.c.tar", FMT_COMIC_ARCHIVE);
	kind_is("ux0:本/進撃の巨人 第1巻.CBZ", FMT_COMIC_ARCHIVE);
	kind_is("ux0:本/吾輩は猫である.epub", FMT_REFLOW);
	/* Only the final component counts. */
	kind_is("ux0:books/x.cbz/readme", FMT_NONE);
	kind_is("ux0:books/x.cbz/readme.doc", FMT_NONE);
	kind_is("a.cbz.bak", FMT_NONE);
	kind_is("a.tar.gz", FMT_NONE);
}

static void test_not_a_kind(void)
{
	kind_is(NULL, FMT_NONE);
	kind_is("", FMT_NONE);
	kind_is("cbz", FMT_NONE);          /* no dot: not an extension */
	kind_is(".cbz", FMT_NONE);         /* hidden file, leading dot alone */
	kind_is("ux0:books/", FMT_NONE);
	kind_is("a.", FMT_NONE);
	kind_is("a.jpg", FMT_NONE);        /* images are not books */
	kind_is("a.docx", FMT_NONE);
	kind_is("a.cbzz", FMT_NONE);
	kind_is("a.c", FMT_NONE);
	kind_is("a.7", FMT_NONE);
}

static void test_never_folder(void)
{
	static const char *const paths[] = { "a.cbz", "a.epub", "a.pdf", "dir", "", "a.jpg", "x/y.tar" };
	size_t i;
	for (i = 0; i < sizeof paths / sizeof *paths; i++)
		CHECK(formats_kind_for_path(paths[i]) != FMT_COMIC_FOLDER);
	CHECK(formats_kind_for_path(NULL) != FMT_COMIC_FOLDER);
}

static void test_images(void)
{
	static const char *const yes[] = { "a.jpg", "a.jpeg", "a.png", "a.gif", "a.bmp", "a.webp",
	                                   "a.tif", "a.tiff", "A.JPG", "a.JpEg", "p/Q.PNG",
	                                   "ux0:漫画/第一話.WebP", "dir/sub/001.TIFF" };
	static const char *const no[] = { "a.cbz", "a.txt", "Thumbs.db", "a", "", ".jpg", "a.jpg.bak",
	                                  "a.jpgx", "a.svg", "a.pdf", "a.", "dir.png/readme" };
	size_t i;

	for (i = 0; i < sizeof yes / sizeof *yes; i++) {
		g_checks++;
		if (!formats_is_image(yes[i])) {
			g_failed++;
			printf("FAIL %s:%d: \"%s\" should be an image\n", __FILE__, __LINE__, yes[i]);
		}
	}
	for (i = 0; i < sizeof no / sizeof *no; i++) {
		g_checks++;
		if (formats_is_image(no[i])) {
			g_failed++;
			printf("FAIL %s:%d: \"%s\" should not be an image\n", __FILE__, __LINE__, no[i]);
		}
	}
	CHECK_INT(formats_is_image(NULL), 0);
}

static void test_kind_names(void)
{
	CHECK_STR(formats_kind_name(FMT_COMIC_ARCHIVE), "comic");
	CHECK_STR(formats_kind_name(FMT_COMIC_FOLDER), "folder");
	CHECK_STR(formats_kind_name(FMT_REFLOW), "book");
	CHECK_STR(formats_kind_name(FMT_FIXED), "document");
	CHECK_STR(formats_kind_name(FMT_NONE), "none");
	CHECK_STR(formats_kind_name((fmt_kind_t)99), "none");
}

static void test_badge(void)
{
	char buf[FORMATS_BADGE_MAX];

	CHECK_STR(formats_badge("ux0:books/a.cbz"), "CBZ");
	CHECK_STR(formats_badge("a.EpUb"), "EPUB");
	CHECK_STR(formats_badge("x/y.pdf"), "PDF");
	CHECK_STR(formats_badge("a.7z"), "7Z");
	CHECK_STR(formats_badge("ux0:本/巻.oxps"), "OXPS");
	CHECK_STR(formats_badge("ux0:books/Naruto"), "DIR");
	CHECK_STR(formats_badge("ux0:"), "DIR");
	CHECK_STR(formats_badge(""), "DIR");
	CHECK_STR(formats_badge(NULL), "DIR");
	CHECK_STR(formats_badge(".hidden"), "DIR");
	CHECK_STR(formats_badge("a.verylongextension"), "VERYLON");
	CHECK_STR(formats_badge("a.a-b_c"), "A-B_C");
	/* Non-ASCII bytes are left alone; a cut never splits a UTF-8 sequence. */
	CHECK_STR(formats_badge("a.é"), "é");
	CHECK_STR(formats_badge("a.abcdeé"), "ABCDEé");     /* 5 + 2 bytes = 7: fits */
	CHECK_STR(formats_badge("a.abcdefé"), "ABCDEF");    /* é would straddle byte 7 */

	/* Reentrant form, small buffers. */
	CHECK_STR(formats_badge_r("a.cbz", buf, sizeof buf), "CBZ");
	CHECK(formats_badge_r("a.cbz", buf, sizeof buf) == buf);
	CHECK_STR(formats_badge_r("a.cbz", buf, 3), "CB");
	CHECK_STR(formats_badge_r("a.cbz", buf, 1), "");
	CHECK_STR(formats_badge_r("dir", buf, 2), "D");
	CHECK_STR(formats_badge_r("a.cbz", buf, 0), "");
	{
		char big[32];
		CHECK_STR(formats_badge_r("a.verylongextension", big, sizeof big), "VERYLON");
	}

	/* The convenience form shares one buffer: the second call overwrites the first. */
	{
		const char *a = formats_badge("a.cbz");
		const char *b = formats_badge("b.pdf");
		CHECK(a == b);
		CHECK_STR(a, "PDF");
	}
}

int main(void)
{
	test_every_extension();
	test_case_and_paths();
	test_not_a_kind();
	test_never_folder();
	test_images();
	test_kind_names();
	test_badge();
	return TEST_DONE("formats");
}
