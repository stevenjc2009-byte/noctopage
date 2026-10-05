#include "scan_fake.h"

/* ---- tests ------------------------------------------------------------------------------ */

static void test_basic_files(void)
{
	const got_t *g;
	fake_reset();
	T("ux0:root.pdf|ux0:books/a.cbz|ux0:books/b.EPUB|ux0:books/notes.doc|ux0:books/c.Pdf|"
	  "ux0:books/single.jpg");
	CHECK_INT(run("ux0:", 8), SCAN_OK);
	CHECK_INT(g_nitems, 4);
	HAS("ux0:root.pdf", FMT_FIXED);
	HAS("ux0:books/a.cbz", ARCH);
	HAS("ux0:books/b.EPUB", FMT_REFLOW);
	HAS("ux0:books/c.Pdf", FMT_FIXED);
	HAS_NOT("ux0:books/notes.doc");
	HAS_NOT("ux0:books/single.jpg");
	CHECK_INT(g_dirs, 2);
	CHECK_INT(g_handles, 0);
	g = find("ux0:books/a.cbz");
	CHECK(g != NULL);
	if (g) {
		CHECK_INT(g->size, 100);
		CHECK_INT(g->mtime, 5);
		CHECK_INT(g->image_count, 0);
	}
}

static void test_skip_list(void)
{
	static const char *const skips[] = { "app", "patch", "addcont", "appmeta", "bgdl", "cache",
		"license", "savedata", "sqlite", "temp", "user", "vd0", "vs0", "tai" };
	char p[128];
	size_t i;

	fake_reset();
	for (i = 0; i < sizeof skips / sizeof *skips; i++) {
		snprintf(p, sizeof p, "ux0:%s/hidden_book.cbz", skips[i]);
		T1(p);
	}
	T("ux0:pspemu/PSP/GAME/g.cbz|ux0:pspemu/PSP/SAVEDATA/s.cbz|ux0:pspemu/other/o.cbz|"
	  "ux0:data/noctopage/thumbs/t.pdf|ux0:data/noctopage/other/y.pdf|"
	  "ux0:data/app/deep.cbz|ux0:books/app/inner.cbz|ux0:APP2/n.cbz");
	CHECK_INT(run("ux0:", 8), SCAN_OK);
	for (i = 0; i < sizeof skips / sizeof *skips; i++) {
		snprintf(p, sizeof p, "ux0:%s/hidden_book.cbz", skips[i]);
		HAS_NOT(p);
		snprintf(p, sizeof p, "ux0:%s", skips[i]);
		CHECK(!was_opened(p)); /* skipped means never listed */
	}
	HAS_NOT("ux0:pspemu/PSP/GAME/g.cbz");
	CHECK(!was_opened("ux0:pspemu/PSP/GAME"));
	HAS_NOT("ux0:data/noctopage/thumbs/t.pdf");
	CHECK(!was_opened("ux0:data/noctopage/thumbs"));
	/* only the exact device-relative path is skipped */
	HAS("ux0:pspemu/PSP/SAVEDATA/s.cbz", ARCH);
	HAS("ux0:pspemu/other/o.cbz", ARCH);
	HAS("ux0:data/noctopage/other/y.pdf", FMT_FIXED);
	HAS("ux0:data/app/deep.cbz", ARCH);
	HAS("ux0:books/app/inner.cbz", ARCH);
	HAS("ux0:APP2/n.cbz", ARCH);
	CHECK_INT(g_nitems, 6);
}

static void test_skip_list_other_device_and_case(void)
{
	fake_reset();
	T("uma0:APP/x.cbz|uma0:Savedata/y.cbz|uma0:Pspemu/psp/game/z.cbz|uma0:keep/k.cbz");
	CHECK_INT(run("uma0:", 8), SCAN_OK); /* ASCII case-insensitive: the Vita FS ignores case */
	HAS_NOT("uma0:APP/x.cbz");
	HAS_NOT("uma0:Savedata/y.cbz");
	HAS_NOT("uma0:Pspemu/psp/game/z.cbz");
	HAS("uma0:keep/k.cbz", ARCH);
	CHECK_INT(g_nitems, 1);
}

static void test_root_below_device_root(void)
{
	fake_reset();
	T("ux0:books/app/a.cbz|ux0:books/cache/c.cbz"); /* device-relative "books/app": not skipped */
	CHECK_INT(run("ux0:books", 8), SCAN_OK);
	HAS("ux0:books/app/a.cbz", ARCH);
	HAS("ux0:books/cache/c.cbz", ARCH);
	fake_reset();
	T("ux0:app/x.cbz");
	CHECK_INT(run("ux0:app", 8), SCAN_OK); /* a skip-list name as the root is still scanned */
	HAS("ux0:app/x.cbz", ARCH);
}

static void test_depth_limit(void)
{
	fake_reset();
	T("ux0:d0.cbz|ux0:a/d1.cbz|ux0:a/b/d2.cbz|ux0:a/b/c/d3.cbz|ux0:a/b/c/d/d4.cbz");
	CHECK_INT(run("ux0:", 2), SCAN_OK);
	HAS("ux0:d0.cbz", ARCH);
	HAS("ux0:a/d1.cbz", ARCH);
	HAS("ux0:a/b/d2.cbz", ARCH);
	HAS_NOT("ux0:a/b/c/d3.cbz");
	CHECK(!was_opened("ux0:a/b/c")); /* a directory beyond max_depth is not even listed */
	CHECK_INT(g_dirs, 3);
	CHECK_INT(run("ux0:", 0), SCAN_OK);
	CHECK_INT(g_nitems, 1);
	CHECK_INT(g_dirs, 1);
	CHECK_INT(run("ux0:", 3), SCAN_OK);
	CHECK_INT(g_nitems, 4);
	CHECK_INT(run("ux0:", 8), SCAN_OK);
	CHECK_INT(g_nitems, 5);
	CHECK_INT(g_dirs, 5);
}

static void test_depth_clamp_and_bad_args(void)
{
	char p[PATHLEN], file[PATHLEN + 8];
	int i, n;

	fake_reset();
	n = snprintf(p, sizeof p, "ux0:");
	for (i = 1; i <= 40; i++) {
		n += snprintf(p + n, sizeof p - (size_t)n, "d%d/", i);
		snprintf(file, sizeof file, "%sf.cbz", p);
		T1(file);
	}
	CHECK_INT(run("ux0:", 1000), SCAN_OK); /* clamped to SCAN_MAX_DEPTH */
	CHECK_INT(g_nitems, SCAN_MAX_DEPTH);   /* one file in each of levels 1..32 */
	CHECK_INT(g_dirs, SCAN_MAX_DEPTH + 1);
	CHECK_INT(run("ux0:", -1), SCAN_ERR_ARGS);
	CHECK_INT(scan_tree(NULL, "ux0:", 8, collect, NULL, NULL, &g_dirs), SCAN_ERR_ARGS);
	CHECK_INT(scan_tree(&FS, NULL, 8, collect, NULL, NULL, &g_dirs), SCAN_ERR_ARGS);
	CHECK_INT(scan_tree(&FS, "ux0:", 8, NULL, NULL, NULL, &g_dirs), SCAN_ERR_ARGS);
	CHECK_INT(scan_tree(&FS, "ux0:", 8, collect, NULL, NULL, NULL), SCAN_OK); /* NULL out ok */
}

static void test_hidden_dirs(void)
{
	fake_reset();
	T("ux0:.hidden/a.cbz|ux0:books/.thumbs/x.pdf|ux0:books/.thumbs/deeper/y.pdf|"
	  "ux0:books/visible/v.cbz");
	add_node("ux0:books", "..", 1, 0, 0);
	add_node("ux0:books", ".", 1, 0, 0);
	CHECK_INT(run("ux0:", 8), SCAN_OK);
	CHECK_INT(g_nitems, 1);
	HAS("ux0:books/visible/v.cbz", ARCH);
	CHECK(!was_opened("ux0:.hidden"));
	CHECK(!was_opened("ux0:books/.thumbs"));
	CHECK(!was_opened("ux0:books/.."));
	CHECK_INT(g_dirs, 3);
}

static void test_hidden_files(void)
{
	fake_reset(); /* hidden files are never reported, whatever their extension */
	T("ux0:books/.secret.cbz|ux0:books/._volume1.cbz|ux0:books/.hidden.pdf|ux0:books/real.cbz|"
	  "ux0:.nomedia.epub");
	CHECK_INT(run("ux0:", 8), SCAN_OK);
	CHECK_INT(g_nitems, 1);
	HAS("ux0:books/real.cbz", ARCH);
	HAS_NOT("ux0:books/.secret.cbz");
	HAS_NOT("ux0:books/._volume1.cbz");
	HAS_NOT("ux0:.nomedia.epub");
}

int main(void)
{
	test_basic_files();
	test_skip_list();
	test_skip_list_other_device_and_case();
	test_root_below_device_root();
	test_depth_limit();
	test_depth_clamp_and_bad_args();
	test_hidden_dirs();
	test_hidden_files();
	return TEST_DONE("scan");
}
