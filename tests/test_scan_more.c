#include "scan_fake.h"

static void test_folder_comic_basic(void)
{
	const got_t *g;
	fake_reset();
	T("ux0:books/one/1.jpg#1000,10|ux0:books/two/1.PNG#100,30|ux0:books/two/2.png#200,70|"
	  "ux0:books/two/3.webp#300,20|ux0:books/two/cover.txt#5,999|ux0:books/none/readme.doc");
	CHECK_INT(run("ux0:", 8), SCAN_OK);
	HAS_NOT("ux0:books/one");
	HAS_NOT("ux0:books/none");
	HAS("ux0:books/two", FOLDER);
	HAS("ux0:books/two/cover.txt", FMT_REFLOW); /* a txt is a book, never an image */
	CHECK_INT(g_nitems, 2);
	g = find("ux0:books/two");
	if (g) {
		CHECK_INT(g->size, 600);
		CHECK_INT(g->mtime, 70);
		CHECK_INT(g->image_count, 3);
	}
	fake_reset(); /* exactly 2 images is enough, 1 is not */
	T("ux0:x/a.gif|ux0:x/b.bmp|ux0:y/a.tif");
	CHECK_INT(run("ux0:", 8), SCAN_OK);
	HAS("ux0:x", FOLDER);
	HAS_NOT("ux0:y");
	CHECK_INT(find("ux0:x") ? find("ux0:x")->image_count : -1, 2);
	fake_reset(); /* the root itself can be a folder comic */
	T("ux0:a.jpg|ux0:b.jpg");
	CHECK_INT(run("ux0:", 8), SCAN_OK);
	HAS("ux0:", FOLDER);
}

static void test_folder_comic_nested(void)
{
	fake_reset(); /* series of chapter folders: the chapters are comics, the series is not */
	T("ux0:series/cover.jpg|ux0:series/back.jpg|ux0:series/ch1/1.jpg|ux0:series/ch1/2.jpg|"
	  "ux0:series/ch2/1.jpg|ux0:series/ch2/2.jpg|ux0:series/book.epub");
	CHECK_INT(run("ux0:", 8), SCAN_OK);
	HAS_NOT("ux0:series");
	HAS("ux0:series/ch1", FOLDER);
	HAS("ux0:series/ch2", FOLDER);
	HAS("ux0:series/book.epub", FMT_REFLOW);
	CHECK_INT(g_nitems, 3);

	fake_reset(); /* a subfolder with one image still blocks the parent, and is no comic itself */
	T("ux0:p/1.jpg|ux0:p/2.jpg|ux0:p/3.jpg|ux0:p/extras/bonus.png");
	CHECK_INT(run("ux0:", 8), SCAN_OK);
	CHECK_INT(g_nitems, 0);

	fake_reset(); /* images three levels down still count ("recursively") */
	T("ux0:p/1.jpg|ux0:p/2.jpg|ux0:p/a/b/c/deep.jpg");
	CHECK_INT(run("ux0:", 8), SCAN_OK);
	CHECK_INT(g_nitems, 0);

	fake_reset(); /* subfolders without images do not block the parent */
	T("ux0:p/1.jpg|ux0:p/2.jpg|ux0:p/docs/notes.doc|ux0:p/empty/");
	CHECK_INT(run("ux0:", 8), SCAN_OK);
	HAS("ux0:p", FOLDER);
}

static void test_folder_comic_ignored_subtrees(void)
{
	/* image-holding subfolders that are hidden, unopenable, or beyond the depth limit were
	 * never visited, so they must not suppress the parent. */
	fake_reset();
	T("ux0:a/1.jpg|ux0:a/2.jpg|ux0:a/.cache/t.jpg|ux0:b/1.jpg|ux0:b/2.jpg|ux0:b/bad/t.jpg|"
	  "ux0:c/1.jpg|ux0:c/2.jpg|ux0:c/x/y/t.jpg");
	snprintf(g_fail_open, sizeof g_fail_open, "ux0:b/bad");
	CHECK_INT(run("ux0:", 2), SCAN_OK);
	HAS("ux0:a", FOLDER);
	HAS("ux0:b", FOLDER);
	HAS("ux0:c", FOLDER); /* y is at depth 3 > 2 */
	CHECK_INT(run("ux0:", 3), SCAN_OK);
	HAS_NOT("ux0:c");     /* now y is visited and holds an image */
	HAS("ux0:a", FOLDER);
}

static void test_hidden_files_and_comics(void)
{
	const got_t *g;
	fake_reset(); /* AppleDouble "._" files never count toward the 2-image threshold */
	T("ux0:one/1.jpg|ux0:one/._1.jpg|ux0:one/.hidden.png#7,99|"
	  "ux0:two/1.jpg#10,1|ux0:two/2.jpg#20,2|ux0:two/._1.jpg#1000,500|ux0:two/._2.jpg|"
	  "ux0:three/1.jpg|ux0:three/2.jpg|ux0:three/sub/._a.jpg");
	CHECK_INT(run("ux0:", 8), SCAN_OK);
	HAS_NOT("ux0:one"); /* 1 real image + 2 hidden ones */
	HAS("ux0:two", FOLDER);
	g = find("ux0:two");
	if (g) {
		CHECK_INT(g->image_count, 2);
		CHECK_INT(g->size, 30);
		CHECK_INT(g->mtime, 2);
	}
	HAS("ux0:three", FOLDER); /* a subfolder holding only hidden images does not block it */
	CHECK_INT(g_nitems, 2);
}

static void test_error_dirs(void)
{
	fake_reset();
	T("ux0:bad/no.cbz|ux0:good/yes.cbz");
	snprintf(g_fail_open, sizeof g_fail_open, "ux0:bad");
	CHECK_INT(run("ux0:", 8), SCAN_OK);
	HAS("ux0:good/yes.cbz", ARCH);
	HAS_NOT("ux0:bad/no.cbz");
	CHECK_INT(g_dirs, 2);
	CHECK_INT(g_handles, 0);

	fake_reset(); /* root that cannot be opened */
	T("ux0:x/");
	snprintf(g_fail_open, sizeof g_fail_open, "ux0:");
	CHECK_INT(run("ux0:", 8), SCAN_ERR_ROOT);
	CHECK_INT(g_dirs, 0);
	CHECK_INT(g_nitems, 0);
	fake_reset();
	CHECK_INT(run("nodevice:foo", 8), SCAN_ERR_ROOT);

	/* A listing that fails part-way: earlier files stay reported, no folder comic is guessed
	 * from a partial listing, siblings are still scanned. */
	fake_reset();
	T("ux0:broken/1.jpg|ux0:broken/2.jpg|ux0:broken/3.jpg|ux0:broken/late.cbz|ux0:fine/f.cbz");
	snprintf(g_fail_read, sizeof g_fail_read, "ux0:broken");
	g_fail_read_after = 3;
	CHECK_INT(run("ux0:", 8), SCAN_OK);
	HAS_NOT("ux0:broken");
	HAS_NOT("ux0:broken/late.cbz");
	HAS("ux0:fine/f.cbz", ARCH);
	CHECK_INT(g_handles, 0);
	g_fail_read[0] = '\0'; /* the same tree without the failure does report the comic */
	CHECK_INT(run("ux0:", 8), SCAN_OK);
	HAS("ux0:broken", FOLDER);
	HAS("ux0:broken/late.cbz", ARCH);
}

static void test_cancel_and_stop(void)
{
	char p[64];
	int i;

	fake_reset();
	for (i = 0; i < 10; i++) {
		snprintf(p, sizeof p, "ux0:d%d/a.cbz", i);
		T1(p);
		snprintf(p, sizeof p, "ux0:d%d/b.cbz", i);
		T1(p);
	}
	CHECK_INT(run("ux0:", 8), SCAN_OK);
	CHECK_INT(g_nitems, 20);
	g_cancel = 1; /* already set: nothing is opened */
	CHECK_INT(run("ux0:", 8), SCAN_CANCELLED);
	CHECK_INT(g_nitems, 0);
	CHECK_INT(g_dirs, 0);
	CHECK_INT(g_nopened, 0);
	g_cancel = 0;
	g_cancel_after_items = 3; /* set from the callback: stops promptly, handles closed */
	CHECK_INT(run("ux0:", 8), SCAN_CANCELLED);
	CHECK_INT(g_nitems, 3);
	CHECK_INT(g_handles, 0);

	fake_reset(); /* cancel during a listing: the half-read directory is not reported */
	T("ux0:comic/1.jpg|ux0:comic/2.jpg|ux0:comic/3.jpg");
	g_cancel_at_read = 4; /* reads: root 1-2, then comic 3 (1.jpg) and 4 (2.jpg) */
	CHECK_INT(run("ux0:", 8), SCAN_CANCELLED);
	HAS_NOT("ux0:comic");
	CHECK_INT(g_handles, 0);
	CHECK_INT(g_reads, 4); /* it did not keep reading */

	fake_reset(); /* callback stop */
	T("ux0:d/a.cbz|ux0:d/b.cbz|ux0:d/c.cbz|ux0:e/z.cbz");
	g_stop_after = 2;
	CHECK_INT(run("ux0:", 8), SCAN_STOPPED);
	CHECK_INT(g_nitems, 2);
	CHECK_INT(g_handles, 0);
	CHECK(!was_opened("ux0:e"));
}

static void test_japanese_and_case(void)
{
	fake_reset();
	T("ux0:本/進撃の巨人 第1巻.cbz|ux0:本/吾輩は猫である.EPUB|ux0:本/シリーズ/01.JPG|"
	  "ux0:本/シリーズ/02.Jpeg|ux0:漫画/第1話/P1.PNG|ux0:漫画/第1話/P2.png|"
	  "ux0:漫画/第2話/p1.WebP|ux0:漫画/第2話/p2.WEBP|ux0:UPPER.CBR|ux0:Mixed.CbZ");
	CHECK_INT(run("ux0:", 8), SCAN_OK);
	HAS("ux0:本/進撃の巨人 第1巻.cbz", ARCH);
	HAS("ux0:本/吾輩は猫である.EPUB", FMT_REFLOW);
	HAS("ux0:本/シリーズ", FOLDER);
	HAS("ux0:漫画/第1話", FOLDER);
	HAS("ux0:漫画/第2話", FOLDER);
	HAS_NOT("ux0:漫画");
	HAS("ux0:UPPER.CBR", ARCH);
	HAS("ux0:Mixed.CbZ", ARCH);
	CHECK_INT(g_nitems, 7);
}

static void test_many_dirs_and_long_paths(void)
{
	char name[300], p[PATHLEN], file[PATHLEN + 8];
	int i, n;

	fake_reset(); /* 300 sibling directories: the packed name list has to grow */
	for (i = 0; i < 300; i++) {
		snprintf(p, sizeof p, "ux0:directory_with_a_longish_name_%d/b.cbz", i);
		T1(p);
	}
	CHECK_INT(run("ux0:", 8), SCAN_OK);
	CHECK_INT(g_nitems, 300);
	CHECK_INT(g_dirs, 301);
	HAS("ux0:directory_with_a_longish_name_299/b.cbz", ARCH);

	fake_reset(); /* paths longer than SCAN_PATH_MAX are skipped, not overflowed */
	memset(name, 'n', 250);
	name[250] = '\0';
	n = snprintf(p, sizeof p, "ux0:");
	for (i = 0; i < 5; i++) {
		n += snprintf(p + n, sizeof p - (size_t)n, "%s/", name);
		snprintf(file, sizeof file, "%sf.cbz", p);
		T1(file);
	}
	CHECK_INT(run("ux0:", 8), SCAN_OK);
	CHECK_INT(g_nitems, 4); /* levels 1-4 fit in 1024 bytes, level 5 (1255 bytes) does not */
	CHECK_INT(g_dirs, 5);
	CHECK_INT(g_handles, 0);

	fake_reset(); /* an empty entry name is ignored */
	add_node("ux0:", "", 0, 1, 1);
	T1("ux0:ok.cbz");
	CHECK_INT(run("ux0:", 8), SCAN_OK);
	CHECK_INT(g_nitems, 1);
}

static void test_app_data_text(void)
{
	fake_reset(); /* other apps' logs and help pages under data/ are not books */
	T("ux0:data/vitamind/chat.txt|ux0:data/noctopage/log.txt|ux0:DATA/x/help.HTML|"
	  "ux0:data/x/a.htm|ux0:data/x/b.xhtml|ux0:data/x/book.epub|ux0:data/x/c.cbz|"
	  "ux0:books/novel.txt|ux0:books/data/page.html|ux0:database/notes.txt|ux0:data.txt");
	CHECK_INT(run("ux0:", 8), SCAN_OK);
	HAS_NOT("ux0:data/vitamind/chat.txt");
	HAS_NOT("ux0:data/noctopage/log.txt");
	HAS_NOT("ux0:DATA/x/help.HTML");
	HAS_NOT("ux0:data/x/a.htm");
	HAS_NOT("ux0:data/x/b.xhtml");
	HAS("ux0:data/x/book.epub", FMT_REFLOW); /* real books in data/ still count */
	HAS("ux0:data/x/c.cbz", ARCH);
	HAS("ux0:books/novel.txt", FMT_REFLOW);
	HAS("ux0:books/data/page.html", FMT_REFLOW); /* only the device's own data/ */
	HAS("ux0:database/notes.txt", FMT_REFLOW);
	HAS("ux0:data.txt", FMT_REFLOW);
	CHECK_INT(g_nitems, 6);
}

int main(void)
{
	test_folder_comic_basic();
	test_folder_comic_nested();
	test_folder_comic_ignored_subtrees();
	test_hidden_files_and_comics();
	test_error_dirs();
	test_cancel_and_stop();
	test_japanese_and_case();
	test_many_dirs_and_long_paths();
	test_app_data_text();
	return TEST_DONE("scan_more");
}
