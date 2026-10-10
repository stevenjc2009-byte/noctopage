#include "test.h"

#include "lib/db.h"
#include "sqlite3.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TMPDIR "build-host-db"
#define DBMAX  64
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

static int titles(int sort, const char *want[], int n)
{
	db_book_t out[DBMAX];
	int got = db_books_list(sort, out, DBMAX), ok = got == n;
	for (int i = 0; i < n && i < got; i++)
		if (strcmp(out[i].title, want[i]) != 0) {
			printf("  sort %d pos %d: \"%s\", wanted \"%s\"\n", sort, i, out[i].title, want[i]);
			ok = 0;
		}
	if (!ok)
		printf("  sort %d: got %d rows, wanted %d\n", sort, got, n);
	return ok;
}

static int user_version(const char *path)
{
	sqlite3 *d = NULL;
	sqlite3_stmt *st = NULL;
	int v = -1;
	if (sqlite3_open(path, &d) == SQLITE_OK &&
	    sqlite3_prepare_v2(d, "PRAGMA user_version", -1, &st, NULL) == SQLITE_OK &&
	    sqlite3_step(st) == SQLITE_ROW)
		v = sqlite3_column_int(st, 0);
	sqlite3_finalize(st);
	sqlite3_close(d);
	return v;
}

static void test_open_migrate(void)
{
	db_book_t b;
	const char *p;
	sqlite3 *d = NULL;

	CHECK(db_books_list(DB_SORT_TITLE, &b, 1) < 0); /* not open */
	CHECK(strlen(db_last_error()) > 0);
	p = fresh_path("migrate");
	CHECK_INT(user_version(p), 0); /* the file does not exist yet: an empty database */
	CHECK_INT(db_open(p), 0);
	CHECK_INT(db_open(p), -1); /* already open */
	CHECK_INT(add(1, "ux0:a.cbz", "A"), 1);
	db_close();
	CHECK_INT(user_version(p), 3);
	/* An existing file at user_version 0 migrates too. */
	p = fresh_path("migrate0");
	CHECK_INT(sqlite3_open(p, &d), SQLITE_OK);
	sqlite3_exec(d, "CREATE TABLE unrelated(x)", NULL, NULL, NULL);
	sqlite3_close(d);
	CHECK_INT(user_version(p), 0);
	CHECK_INT(db_open(p), 0);
	CHECK_INT(add(1, "ux0:a.cbz", "A"), 1);
	db_close();
	CHECK_INT(user_version(p), 3);
	/* A newer database is refused, and the failed open leaves nothing open. */
	CHECK_INT(sqlite3_open(p, &d), SQLITE_OK);
	sqlite3_exec(d, "PRAGMA user_version=99", NULL, NULL, NULL);
	sqlite3_close(d);
	CHECK(db_open(p) < 0);
	CHECK(strstr(db_last_error(), "newer") != NULL);
	CHECK(db_books_list(DB_SORT_TITLE, &b, 1) < 0);
	CHECK(db_open(TMPDIR "/no_such_dir/x.db") < 0);
	CHECK_INT(db_open(fresh_path("migrate")), 0);
	db_close();
}

static void test_scan_missing(void)
{
	db_book_t b, out[DBMAX];

	open_fresh("scan");
	CHECK_INT(db_scan_begin(), 0);
	CHECK_INT(db_scan_lookup("ux0:a.cbz", 101, 5), 0); /* unknown */
	CHECK_INT(add(1, "ux0:a.cbz", "A"), 1);
	CHECK_INT(add(2, "ux0:b.cbz", "B"), 1);
	CHECK_INT(add(3, "ux0:c.cbz", "C"), 1);
	CHECK_INT(db_scan_end(), 0);
	CHECK_INT(db_books_list(DB_SORT_TITLE, out, DBMAX), 3);

	/* Second scan: a is unchanged (lookup hits), size/mtime changes miss, b is not seen. */
	CHECK_INT(db_scan_begin(), 0);
	CHECK_INT(db_scan_lookup("ux0:a.cbz", 101, 5), 1);
	CHECK_INT(db_scan_lookup("ux0:c.cbz", 103, 6), 0); /* mtime differs */
	CHECK_INT(db_scan_lookup("ux0:c.cbz", 999, 5), 0); /* size differs */
	CHECK_INT(db_scan_lookup("ux0:c.cbz", 103, 5), 1);
	CHECK_INT(db_scan_end(), 0);
	CHECK_INT(db_books_list(DB_SORT_TITLE, out, DBMAX), 2);
	CHECK_INT(db_book_get(2, &b), 1); /* never deleted */
	CHECK_INT(b.missing, 1);
	CHECK_STR(b.path, "ux0:b.cbz");

	/* It comes back: found by lookup in a later scan. */
	CHECK_INT(db_scan_begin(), 0);
	CHECK_INT(db_scan_lookup("ux0:b.cbz", 102, 5), 1);
	CHECK_INT(db_scan_lookup("ux0:a.cbz", 101, 5), 1);
	CHECK_INT(db_scan_end(), 0);
	CHECK_INT(db_books_list(DB_SORT_TITLE, out, DBMAX), 2); /* a and b; c was not seen */
	CHECK_INT(db_book_get(2, &b), 1);
	CHECK_INT(b.missing, 0);
	CHECK_INT(db_book_get(3, &b), 1);
	CHECK_INT(b.missing, 1);
	/* ... and by upsert. */
	CHECK_INT(db_scan_begin(), 0);
	CHECK_INT(add(3, "ux0:c.cbz", "C"), 0);
	CHECK_INT(db_scan_end(), 0);
	CHECK_INT(db_book_get(3, &b), 1);
	CHECK_INT(b.missing, 0);
	CHECK_INT(db_book_get(1, &b), 1);
	CHECK_INT(b.missing, 1);
	db_close();
}

static void test_move_collision(void)
{
	db_book_t b, out[DBMAX];
	db_book_t nopath;
	char v[16];

	g_now = 2000;
	open_fresh("move");
	db_scan_begin();
	add(1, "ux0:old/a.cbz", "A");
	add(2, "ux0:old/b.cbz", "B");
	db_scan_end();
	db_set_progress(1, 7, 30, "", 0.25);
	db_set_title(1, "Edited A", "Writer");
	db_book_setting_set(1, "fit", "width");
	/* a.cbz is renamed and moved: same id, new path. Progress, set title, settings are kept. */
	db_scan_begin();
	CHECK_INT(db_scan_lookup("ux0:new/a2.cbz", 101, 5), 0);
	CHECK_INT(add(1, "ux0:new/a2.cbz", "a2"), 0);
	CHECK_INT(db_scan_lookup("ux0:old/b.cbz", 102, 5), 1);
	db_scan_end();
	CHECK_INT(db_book_get(1, &b), 1);
	CHECK_STR(b.path, "ux0:new/a2.cbz");
	CHECK_STR(b.title, "Edited A");
	CHECK_STR(b.author, "Writer");
	CHECK_INT(b.page, 7);
	CHECK_INT(b.page_count, 30);
	CHECK_INT(b.last_read, 2000);
	CHECK_INT(b.added, 2000);
	CHECK_INT(b.missing, 0);
	CHECK_INT(db_books_list(DB_SORT_TITLE, out, DBMAX), 2);
	CHECK_INT(db_book_setting_get(1, "fit", v, sizeof v), 1);

	/* A different book appears at a path that belongs to book 2: book 2 loses the path. */
	g_now = 3000;
	db_scan_begin();
	CHECK_INT(db_scan_lookup("ux0:new/a2.cbz", 101, 5), 1);
	CHECK_INT(add(9, "ux0:old/b.cbz", "Impostor"), 1);
	db_scan_end();
	CHECK_INT(db_book_get(2, &b), 1);
	CHECK_INT(b.missing, 1);
	CHECK_STR(b.path, "");
	CHECK_STR(b.title, "B");
	CHECK_INT(db_book_get(9, &b), 1);
	CHECK_STR(b.path, "ux0:old/b.cbz");
	CHECK_INT(b.missing, 0);
	CHECK_INT(b.added, 3000);
	CHECK_INT(db_books_list(DB_SORT_TITLE, out, DBMAX), 2); /* A, Impostor */
	/* Same scan: a book that lost its path stays missing even though it was seen earlier in it. */
	db_scan_begin();
	add(2, "ux0:old/b.cbz", "B");
	add(4, "ux0:old/b.cbz", "Fourth");
	db_scan_end();
	CHECK_INT(db_book_get(2, &b), 1);
	CHECK_INT(b.missing, 1);
	CHECK_STR(b.path, "");
	CHECK_INT(db_book_get(4, &b), 1);
	CHECK_INT(b.missing, 0);
	/* Book 2 later turns up again elsewhere: path restored, still the same row. */
	db_scan_begin();
	add(2, "ux0:back/b.cbz", "B");
	db_scan_end();
	CHECK_INT(db_book_get(2, &b), 1);
	CHECK_STR(b.path, "ux0:back/b.cbz");
	CHECK_INT(b.missing, 0);
	nopath = mk(5, "", "No path", 1, 1);
	CHECK_INT(db_book_upsert(&nopath), -1);
	db_close();
}

static void test_sorts_and_reading(void)
{
	const char *t_title[] = { "Alpha", "apple pie", "Banana", "Vol 1", "vol 2", "Vol 10", "Zed",
	                          NIHON " 2", NIHON " 10" };
	/* Shuffled insertion order; ids are 100 + position here. */
	const char *shuffled[] = { "Vol 10", NIHON " 10", "Banana", "vol 2", "Zed", "Alpha", NIHON " 2", "Vol 1",
	                           "apple pie" };
	const int64_t added[] = { 50, 40, 30, 80, 70, 10, 60, 20, 90 }; /* per shuffled[] */
	db_book_t b, out[DBMAX];

	open_fresh("sorts");
	db_scan_begin();
	for (int i = 0; i < 9; i++) {
		char path[64];
		snprintf(path, sizeof path, "ux0:s/%d.cbz", i);
		b = mk(100 + (uint64_t)i, path, shuffled[i], 1, 1);
		b.added = added[i];
		CHECK_INT(db_book_upsert(&b), 1);
	}
	db_scan_end();
	CHECK(titles(DB_SORT_TITLE, t_title, 9)); /* natural, case-insensitive; Vol 2 < Vol 10; Japanese last */
	{
		const char *w[] = { "apple pie", "vol 2", "Zed", NIHON " 2", "Vol 10", NIHON " 10", "Banana", "Vol 1",
		                    "Alpha" }; /* added, newest first */
		CHECK(titles(DB_SORT_ADDED, w, 9));
	}
	/* Last read: read books newest first, never-read ones after them by title.
	 * Ids: Vol 1 = 107, Zed = 104, Vol 10 = 100. */
	g_now = 100;
	db_set_progress(107, 1, 10, "", 0.1);
	g_now = 300;
	db_set_progress(104, 1, 10, "", 0.1);
	g_now = 200;
	db_set_progress(100, 1, 10, "", 0.1);
	{
		const char *w[] = { "Zed", "Vol 10", "Vol 1", "Alpha", "apple pie", "Banana", "vol 2", NIHON " 2",
		                    NIHON " 10" };
		CHECK(titles(DB_SORT_LAST_READ, w, 9));
	}
	CHECK_INT(db_books_list(DB_SORT_TITLE, out, 3), 3); /* max respected */
	CHECK_INT(db_books_list(DB_SORT_TITLE, out, 0), 0);

	/* Continue reading: newest unfinished, not missing. */
	CHECK_INT(db_continue_reading(&b), 1);
	CHECK_STR(b.title, "Zed");
	g_now = 400;
	CHECK_INT(db_set_progress(104, 9, 10, "", 0.5), 1); /* last page: finished, though progress is 0.5 */
	CHECK_INT(db_book_get(104, &b), 1);
	CHECK_INT(b.finished, 1);
	CHECK_INT(db_continue_reading(&b), 1);
	CHECK_STR(b.title, "Vol 10");
	g_now = 250; /* Vol 10 stays older than Zed (400) and newer than Vol 1 (100) */
	CHECK_INT(db_set_progress(100, 8, 10, "", 0.998), 1); /* not the last page, below 0.999 */
	CHECK_INT(db_book_get(100, &b), 1);
	CHECK_INT(b.finished, 0);
	CHECK_INT(db_set_progress(100, 8, 10, "", 0.999), 1);
	CHECK_INT(db_book_get(100, &b), 1);
	CHECK_INT(b.finished, 1);
	CHECK_INT(db_set_progress(100, 0, 0, "x", 0.2), 1); /* unknown page count: only progress counts */
	CHECK_INT(db_book_get(100, &b), 1);
	CHECK_INT(b.finished, 0);
	CHECK_INT(db_set_finished(100, 1), 1);
	CHECK_INT(db_book_get(100, &b), 1);
	CHECK_INT(b.finished, 1);
	CHECK_INT(db_set_finished(100, 0), 1);
	CHECK_INT(db_set_finished(424242, 1), 0);
	CHECK_INT(db_set_progress(424242, 1, 2, "", 0.5), 0);
	db_set_finished(100, 1);
	CHECK_INT(db_continue_reading(&b), 1);
	CHECK_STR(b.title, "Vol 1");
	db_set_finished(107, 1);
	CHECK_INT(db_continue_reading(&b), 0);
	db_set_finished(107, 0);
	db_scan_begin(); /* nothing is seen: everything becomes missing */
	db_scan_end();
	CHECK_INT(db_continue_reading(&b), 0);
	CHECK_INT(db_books_list(DB_SORT_TITLE, out, DBMAX), 0);

	/* History: read books newest first, missing ones flagged but listed; removing keeps the book. */
	CHECK_INT(db_history_list(out, DBMAX), 3);
	CHECK_STR(out[0].title, "Zed");
	CHECK_STR(out[1].title, "Vol 10");
	CHECK_STR(out[2].title, "Vol 1");
	CHECK_INT(out[0].missing, 1);
	CHECK_INT(db_history_remove(104), 1);
	CHECK_INT(db_history_remove(999), 0);
	CHECK_INT(db_history_list(out, DBMAX), 2);
	CHECK_INT(db_book_get(104, &b), 1);
	CHECK_INT(b.last_read, 0);
	CHECK_INT(b.finished, 1); /* removing from history changes nothing else */
	db_close();
}

int main(void)
{
	db_set_clock(fake_clock);
	test_open_migrate();
	test_scan_missing();
	test_move_collision();
	test_sorts_and_reading();
	return TEST_DONE("db");
}
