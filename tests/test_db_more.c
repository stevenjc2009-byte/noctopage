/* db part 2: persistence across reopen, per-book settings, bookmarks, servers, downloads, concurrent writers. */
#include "test.h"

#include "lib/db.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TMPDIR "build-host-db"
#define NIHON  "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E" /* Japanese for "Japanese" */

static int64_t g_now = 1000;
static int64_t fake_clock(void)
{
	return g_now;
}

static const char *fresh_path(const char *name)
{
	static char p[256];
	char j[300];
	mkdir(TMPDIR, 0777);
	snprintf(p, sizeof p, TMPDIR "/%s.db", name);
	unlink(p);
	snprintf(j, sizeof j, "%s-journal", p);
	unlink(j);
	return p;
}

static void open_fresh(const char *name)
{
	CHECK_INT(db_open(fresh_path(name)), 0);
}

static db_book_t mk(uint64_t id, const char *path, const char *title, uint64_t size, int64_t mtime)
{
	db_book_t b;
	memset(&b, 0, sizeof b);
	b.id = id;
	snprintf(b.path, sizeof b.path, "%s", path);
	snprintf(b.title, sizeof b.title, "%s", title);
	b.kind = DB_KIND_ARCHIVE;
	b.size = size;
	b.mtime = mtime;
	return b;
}

static int add(uint64_t id, const char *path, const char *title)
{
	db_book_t b = mk(id, path, title, 100 + id, 5);
	return db_book_upsert(&b);
}

static void test_persistence(void)
{
	db_book_t b;
	db_bookmark_t bm = { 0 }, bms[4];
	db_server_t sv = { 0 }, svs[4];
	db_download_t dl = { 0 }, dls[4];
	char val[32];
	int64_t id;
	const char *p = fresh_path("persist");
	const char *evil = "Quote's \"x\"; DROP TABLE books;--";

	g_now = 5000;
	CHECK_INT(db_open(p), 0);
	b = mk(0xFFFFFFFFFFFFFFF0ULL, "ux0:data/" NIHON ".cbz", evil, 5000000000ULL, 77);
	snprintf(b.author, sizeof b.author, "Au\xC3\xBCthor");
	CHECK_INT(db_book_upsert(&b), 1);
	CHECK_INT(db_set_progress(b.id, 4, 20, "epubcfi(/6/4)", 0.2), 1);
	CHECK_INT(db_book_setting_set(b.id, "mode", "rtl"), 0);
	snprintf(bm.location, sizeof bm.location, "loc1");
	bm.book_id = b.id;
	bm.page = 3;
	snprintf(bm.label, sizeof bm.label, "my mark");
	CHECK_INT(db_bookmark_add(&bm, &id), 0);
	snprintf(sv.name, sizeof sv.name, "Komga");
	snprintf(sv.url, sizeof sv.url, "http://h:25600/opds/v1.2/catalog");
	snprintf(sv.username, sizeof sv.username, "u");
	snprintf(sv.password, sizeof sv.password, "p w");
	snprintf(sv.kind, sizeof sv.kind, "opds");
	CHECK_INT(db_server_add(&sv, &id), 0);
	snprintf(dl.url, sizeof dl.url, "https://x/y.epub");
	snprintf(dl.dest_path, sizeof dl.dest_path, "ux0:books/y.epub");
	snprintf(dl.title, sizeof dl.title, "Y");
	CHECK_INT(db_download_add(&dl, &id), 0);
	CHECK_INT(db_download_update(id, DB_DL_ACTIVE, 10, 100, "oops"), 1);
	db_close();

	CHECK_INT(db_open(p), 0);
	CHECK_INT(db_book_get(0xFFFFFFFFFFFFFFF0ULL, &b), 1);
	CHECK_U64(b.id, 0xFFFFFFFFFFFFFFF0ULL);
	CHECK_STR(b.title, evil);
	CHECK_STR(b.author, "Au\xC3\xBCthor");
	CHECK_STR(b.path, "ux0:data/" NIHON ".cbz");
	CHECK_U64(b.size, 5000000000ULL);
	CHECK_INT(b.mtime, 77);
	CHECK_INT(b.kind, DB_KIND_ARCHIVE);
	CHECK_INT(b.page, 4);
	CHECK_INT(b.page_count, 20);
	CHECK_STR(b.location, "epubcfi(/6/4)");
	CHECK(b.progress > 0.19 && b.progress < 0.21);
	CHECK_INT(b.last_read, 5000);
	CHECK_INT(b.added, 5000);
	CHECK_INT(b.missing, 0);
	CHECK_INT(db_book_setting_get(b.id, "mode", val, sizeof val), 1);
	CHECK_STR(val, "rtl");
	CHECK_INT(db_bookmark_list(b.id, bms, 4), 1);
	CHECK_STR(bms[0].label, "my mark");
	CHECK_INT(bms[0].page, 3);
	CHECK_INT(bms[0].created, 5000);
	CHECK_INT(db_server_list(svs, 4), 1);
	CHECK_STR(svs[0].password, "p w");
	CHECK_STR(svs[0].kind, "opds");
	CHECK_INT(db_download_list(dls, 4), 1);
	CHECK_INT(dls[0].state, DB_DL_ACTIVE);
	CHECK_INT(dls[0].bytes, 10);
	CHECK_INT(dls[0].total, 100);
	CHECK_STR(dls[0].error, "oops");
	CHECK_STR(dls[0].dest_path, "ux0:books/y.epub");
	CHECK_INT(db_book_get(12345, &b), 0);
	db_close();
}

static void test_settings_and_lists(void)
{
	char v[8];
	db_bookmark_t bm = { 0 }, bms[8];
	db_server_t sv = { 0 }, svs[8];
	db_download_t dl = { 0 }, dls[8];
	int64_t i1, i2, i3;

	open_fresh("misc");
	add(1, "ux0:a", "A");
	add(2, "ux0:b", "B");
	CHECK_INT(db_book_setting_get(1, "k", v, sizeof v), 0);
	CHECK_STR(v, "");
	CHECK_INT(db_book_setting_set(1, "k", "first"), 0);
	CHECK_INT(db_book_setting_set(1, "k", "second-longer-than-v"), 0);
	CHECK_INT(db_book_setting_set(1, "j", "other"), 0);
	CHECK_INT(db_book_setting_set(2, "k", "book2"), 0);
	CHECK_INT(db_book_setting_get(1, "k", v, sizeof v), 1);
	CHECK_STR(v, "second-"); /* the second set replaced the first; truncated to cap */
	CHECK_INT(db_book_setting_get(1, "j", v, sizeof v), 1);
	CHECK_STR(v, "other");
	CHECK_INT(db_book_setting_get(2, "k", v, sizeof v), 1);
	CHECK_STR(v, "book2");
	CHECK_INT(db_book_setting_get(3, "k", v, sizeof v), 0);

	g_now = 10;
	bm.book_id = 1;
	snprintf(bm.location, sizeof bm.location, "l9");
	bm.page = 9;
	CHECK_INT(db_bookmark_add(&bm, &i1), 0);
	bm.page = 2;
	snprintf(bm.label, sizeof bm.label, "two");
	CHECK_INT(db_bookmark_add(&bm, &i2), 0);
	bm.book_id = 2;
	CHECK_INT(db_bookmark_add(&bm, &i3), 0);
	CHECK(i1 != i2 && i2 != i3);
	CHECK_INT(db_bookmark_list(1, bms, 8), 2);
	CHECK_INT(bms[0].page, 2); /* by page */
	CHECK_STR(bms[0].label, "two");
	CHECK_INT(bms[1].page, 9);
	CHECK_INT(db_bookmark_list(1, bms, 1), 1);
	CHECK_INT(db_bookmark_remove(i2), 1);
	CHECK_INT(db_bookmark_remove(i2), 0);
	CHECK_INT(db_bookmark_list(1, bms, 8), 1);
	CHECK_INT(db_bookmark_list(2, bms, 8), 1);

	snprintf(sv.name, sizeof sv.name, "One");
	snprintf(sv.url, sizeof sv.url, "http://one");
	CHECK_INT(db_server_add(&sv, &i1), 0);
	snprintf(sv.name, sizeof sv.name, "Two");
	CHECK_INT(db_server_add(&sv, &i2), 0);
	sv.id = i1;
	snprintf(sv.name, sizeof sv.name, "One!");
	snprintf(sv.kind, sizeof sv.kind, "komga");
	CHECK_INT(db_server_update(&sv), 1);
	sv.id = 777;
	CHECK_INT(db_server_update(&sv), 0);
	CHECK_INT(db_server_list(svs, 8), 2);
	CHECK_STR(svs[0].name, "One!");
	CHECK_STR(svs[0].kind, "komga");
	CHECK_STR(svs[1].name, "Two");
	CHECK_INT(db_server_remove(i1), 1);
	CHECK_INT(db_server_list(svs, 8), 1);
	CHECK_STR(svs[0].name, "Two");

	snprintf(dl.url, sizeof dl.url, "http://d1");
	snprintf(dl.dest_path, sizeof dl.dest_path, "ux0:books/1");
	CHECK_INT(db_download_add(&dl, &i1), 0);
	snprintf(dl.url, sizeof dl.url, "http://d2");
	CHECK_INT(db_download_add(&dl, &i2), 0);
	CHECK_INT(db_download_update(i2, DB_DL_DONE, 50, 50, NULL), 1);
	CHECK_INT(db_download_update(555, DB_DL_DONE, 0, 0, NULL), 0);
	CHECK_INT(db_download_list(dls, 8), 2);
	CHECK_INT(dls[0].state, DB_DL_QUEUED);
	CHECK_INT(dls[0].created, 10);
	CHECK_INT(dls[1].state, DB_DL_DONE);
	CHECK_STR(dls[1].error, "");
	CHECK_INT(db_download_remove(i1), 1);
	CHECK_INT(db_download_list(dls, 8), 1);
	db_close();
}

#define NTHREADS 4
#define PER      200
#define SHARED   50
static int g_thread_errors;
static pthread_mutex_t g_errmu = PTHREAD_MUTEX_INITIALIZER;

static void *worker(void *arg)
{
	int t = (int)(intptr_t)arg, errs = 0;
	db_book_t b, out[16];
	char path[64], v[16];
	db_bookmark_t bm = { 0 };

	for (int i = 0; i < PER + SHARED; i++) {
		uint64_t id = i < PER ? 10000 + (uint64_t)t * 1000 + (uint64_t)i : 90000 + (uint64_t)(i - PER);
		snprintf(path, sizeof path, "ux0:t%d/%d.cbz", i < PER ? t : 9, i);
		b = mk(id, path, "T", 10, 10);
		if (db_book_upsert(&b) < 0)
			errs++;
		if (db_set_progress(id, 1, 10, "", 0.1) < 0)
			errs++;
		if (db_book_setting_set(id, "k", "v") < 0 || db_book_setting_get(id, "k", v, sizeof v) != 1)
			errs++;
		if (i < PER) {
			bm.book_id = id;
			if (db_bookmark_add(&bm, NULL) < 0)
				errs++;
			/* Our own row is found by exact (path,size,mtime), and not with a wrong mtime. */
			if (db_scan_lookup(path, 10, 10) != 1 || db_scan_lookup(path, 10, 11) != 0)
				errs++;
		}
		if (db_books_list(DB_SORT_LAST_READ, out, 16) < 0 || db_continue_reading(&b) < 0)
			errs++;
	}
	pthread_mutex_lock(&g_errmu);
	g_thread_errors += errs;
	pthread_mutex_unlock(&g_errmu);
	return NULL;
}

static void test_concurrent(void)
{
	pthread_t th[NTHREADS];
	db_book_t *all = (db_book_t *)malloc(sizeof(db_book_t) * 2000);
	db_bookmark_t bms[4];

	open_fresh("conc");
	db_scan_begin();
	for (int i = 0; i < NTHREADS; i++)
		pthread_create(&th[i], NULL, worker, (void *)(intptr_t)i);
	for (int i = 0; i < NTHREADS; i++)
		pthread_join(th[i], NULL);
	CHECK_INT(g_thread_errors, 0);
	/* The shared ids were upserted by all 4 threads: one row each. */
	CHECK_INT(db_books_list(DB_SORT_TITLE, all, 2000), NTHREADS * PER + SHARED);
	CHECK_INT(db_bookmark_list(10000, bms, 4), 1);
	db_close();
	free(all);
}

int main(void)
{
	db_set_clock(fake_clock);
	test_persistence();
	test_settings_and_lists();
	test_concurrent();
	return TEST_DONE("db_more");
}
