#include "lib/db.h"

#include "lib/db_priv.h"

#include "core/natsort.h"
#include "core/strutil.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static sqlite3 *g_db;
static sqlite3_stmt *g_lookup; /* cached: scan_lookup runs once per file */
static int64_t g_gen;
static int64_t (*g_clock)(void);
static char g_err[256];

#define LOCK()   pthread_mutex_lock(&g_mu)
#define UNLOCK() pthread_mutex_unlock(&g_mu)
#define I(x)     DBI(x)
#define ql       db_priv_ql

/* ---- helpers (caller holds the lock unless the name says otherwise) ----------------------- */

static int fail_msg(const char *msg)
{
	str_copy(g_err, sizeof g_err, msg);
	return -1;
}

static int fail_db(void)
{
	return fail_msg(g_db ? sqlite3_errmsg(g_db) : "database not open");
}

static int64_t now_locked(void)
{
	return g_clock ? g_clock() : (int64_t)time(NULL);
}

void db_priv_col_str(sqlite3_stmt *st, int i, char *dst, size_t cap)
{
	const char *s = (const char *)sqlite3_column_text(st, i);
	str_copy(dst, cap, s ? s : "");
}
#define col_str db_priv_col_str

static int vq(const char *sql, db_rowfn cb, void *ud, const char *fmt, va_list ap)
{
	sqlite3_stmt *st;
	int rc, rows = 0;

	if (!g_db)
		return fail_msg("database not open");
	if (sqlite3_prepare_v2(g_db, sql, -1, &st, NULL) != SQLITE_OK)
		return fail_db();
	for (int i = 1; fmt[i - 1]; i++) {
		if (fmt[i - 1] == 'I') {
			sqlite3_bind_int64(st, i, va_arg(ap, int64_t));
		} else if (fmt[i - 1] == 'D') {
			sqlite3_bind_double(st, i, va_arg(ap, double));
		} else {
			const char *s = va_arg(ap, const char *);
			if (s)
				sqlite3_bind_text(st, i, s, -1, SQLITE_TRANSIENT);
			else
				sqlite3_bind_null(st, i);
		}
	}
	while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
		rows++;
		if (cb && cb(st, ud))
			break;
	}
	if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
		fail_db();
		sqlite3_finalize(st);
		return -1;
	}
	sqlite3_finalize(st);
	return cb ? rows : sqlite3_changes(g_db);
}

/* Unlocked: for code that already holds the lock (multi-statement operations). */
static int q_(const char *sql, db_rowfn cb, void *ud, const char *fmt, ...)
{
	va_list ap;
	int r;
	va_start(ap, fmt);
	r = vq(sql, cb, ud, fmt, ap);
	va_end(ap);
	return r;
}

int db_priv_ql(const char *sql, db_rowfn cb, void *ud, const char *fmt, ...)
{
	va_list ap;
	int r;
	LOCK();
	va_start(ap, fmt);
	r = vq(sql, cb, ud, fmt, ap);
	va_end(ap);
	UNLOCK();
	return r;
}

int db_priv_insert(const char *sql, int64_t *id, const char *fmt, ...)
{
	va_list ap;
	int r;
	LOCK();
	va_start(ap, fmt);
	r = vq(sql, NULL, NULL, fmt, ap);
	va_end(ap);
	if (r >= 0) {
		if (id)
			*id = sqlite3_last_insert_rowid(g_db);
		r = 0;
	}
	UNLOCK();
	return r;
}

int64_t db_priv_now(void)
{
	int64_t t;
	LOCK();
	t = now_locked();
	UNLOCK();
	return t;
}

static int row_stop(sqlite3_stmt *st, void *ud)
{
	(void)st;
	(void)ud;
	return 1;
}

static int row_int(sqlite3_stmt *st, void *ud)
{
	*(int64_t *)ud = sqlite3_column_int64(st, 0);
	return 1;
}

/* ---- collation, open/close, migration ---------------------------------------------------- */

static int nat_collate(void *ud, int la, const void *a, int lb, const void *b)
{
	char sa[512], sb[512]; /* natcmp wants C strings; titles are <= 255 bytes */
	(void)ud;
	if (la > (int)sizeof sa - 1)
		la = (int)sizeof sa - 1;
	if (lb > (int)sizeof sb - 1)
		lb = (int)sizeof sb - 1;
	memcpy(sa, a, (size_t)la);
	sa[la] = '\0';
	memcpy(sb, b, (size_t)lb);
	sb[lb] = '\0';
	return natcmp(sa, sb);
}

static int migrate(void)
{
	int64_t ver = 0;
	if (q_("PRAGMA user_version", row_int, &ver, "") < 0)
		return -1;
	if (ver > DB_SCHEMA_VERSION)
		return fail_msg("database is newer than this version of the app");
	for (; ver < DB_SCHEMA_VERSION; ver++) {
		char sql[40];
		char *err = NULL;
		if (q_("BEGIN", NULL, NULL, "") < 0)
			return -1;
		snprintf(sql, sizeof sql, "PRAGMA user_version=%d", (int)ver + 1);
		if (sqlite3_exec(g_db, db_migrations[ver], NULL, NULL, &err) != SQLITE_OK ||
		    sqlite3_exec(g_db, sql, NULL, NULL, &err) != SQLITE_OK) {
			fail_msg(err ? err : "migration failed");
			sqlite3_free(err);
			q_("ROLLBACK", NULL, NULL, "");
			return -1;
		}
		if (q_("COMMIT", NULL, NULL, "") < 0)
			return -1;
	}
	return 0;
}

int db_open(const char *path)
{
	int64_t g = 0;
	int r = -1;

	LOCK();
	if (g_db) {
		fail_msg("database already open");
	} else if (sqlite3_open_v2(path, &g_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
	                           "unix-none") != SQLITE_OK) {
		fail_db();
		sqlite3_close(g_db);
		g_db = NULL;
	} else {
		sqlite3_busy_timeout(g_db, 2000);
		if (sqlite3_create_collation(g_db, "NAT", SQLITE_UTF8, NULL, nat_collate) != SQLITE_OK) {
			fail_db();
		} else if (migrate() == 0 && q_("SELECT ifnull(max(seen),0) FROM books", row_int, &g, "") >= 0) {
			g_gen = g;
			r = 0;
		}
		if (r < 0) {
			sqlite3_close(g_db);
			g_db = NULL;
		}
	}
	UNLOCK();
	return r;
}

void db_close(void)
{
	LOCK();
	if (g_db) {
		sqlite3_finalize(g_lookup);
		g_lookup = NULL;
		sqlite3_close(g_db);
		g_db = NULL;
	}
	UNLOCK();
}

const char *db_last_error(void)
{
	return g_err;
}

void db_last_error_copy(char *out, size_t cap)
{
	LOCK();
	str_copy(out, cap, g_err);
	UNLOCK();
}

void db_set_clock(int64_t (*fn)(void))
{
	LOCK();
	g_clock = fn;
	UNLOCK();
}

/* ---- books ------------------------------------------------------------------------------- */

int db_scan_begin(void)
{
	int r = -1;
	LOCK();
	if (g_db) {
		g_gen++;
		r = 0;
	} else {
		fail_msg("database not open");
	}
	UNLOCK();
	return r;
}

int db_scan_lookup(const char *path, uint64_t size, int64_t mtime)
{
	int r = -1;
	LOCK();
	if (!g_db) {
		fail_msg("database not open");
	} else if (!g_lookup && sqlite3_prepare_v2(g_db, "UPDATE books SET seen=?1, missing=0"
	                                                 " WHERE path=?2 AND size=?3 AND mtime=?4",
	                                           -1, &g_lookup, NULL) != SQLITE_OK) {
		fail_db();
	} else {
		sqlite3_reset(g_lookup);
		sqlite3_bind_int64(g_lookup, 1, g_gen);
		sqlite3_bind_text(g_lookup, 2, path, -1, SQLITE_TRANSIENT);
		sqlite3_bind_int64(g_lookup, 3, I(size));
		sqlite3_bind_int64(g_lookup, 4, mtime);
		if (sqlite3_step(g_lookup) == SQLITE_DONE)
			r = sqlite3_changes(g_db) > 0;
		else
			fail_db();
		sqlite3_reset(g_lookup);
	}
	UNLOCK();
	return r;
}

static int upsert_locked(const db_book_t *b)
{
	int exists;

	if (q_("SAVEPOINT noct", NULL, NULL, "") < 0)
		return -1;
	/* A different book already at this path loses it (kept as a missing row). */
	if (q_("UPDATE books SET path=NULL, missing=1, seen=0 WHERE path=?1 AND id<>?2", NULL, NULL, "TI",
	       b->path, I(b->id)) < 0 ||
	    (exists = q_("SELECT 1 FROM books WHERE id=?1", row_stop, NULL, "I", I(b->id))) < 0)
		goto fail;
	if (exists) { /* known id: a new path = moved/renamed; progress, added and set titles are kept */
		if (q_("UPDATE books SET path=?2, kind=?3, size=?4, mtime=?5,"
		       " title=CASE WHEN title='' THEN ?6 ELSE title END,"
		       " author=CASE WHEN author='' THEN ?7 ELSE author END,"
		       " page_count=CASE WHEN ?9>0 THEN ?9 ELSE page_count END, seen=?8, missing=0 WHERE id=?1",
		       NULL, NULL, "ITIIITTII", I(b->id), b->path, I(b->kind), I(b->size), b->mtime, b->title,
		       b->author, g_gen, I(b->page_count)) < 0)
			goto fail;
	} else if (q_("INSERT INTO books(id,path,title,author,kind,size,mtime,added,page_count,seen)"
	              " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10)", NULL, NULL, "ITTTIIIIII", I(b->id), b->path,
	              b->title, b->author, I(b->kind), I(b->size), b->mtime, b->added ? b->added : now_locked(),
	              I(b->page_count), g_gen) < 0) {
		goto fail;
	}
	return q_("RELEASE noct", NULL, NULL, "") < 0 ? -1 : !exists;
fail:
	{
		char keep[sizeof g_err];
		str_copy(keep, sizeof keep, g_err);
		q_("ROLLBACK TO noct", NULL, NULL, "");
		q_("RELEASE noct", NULL, NULL, "");
		str_copy(g_err, sizeof g_err, keep);
	}
	return -1;
}

int db_book_upsert(const db_book_t *b)
{
	int r;
	LOCK();
	r = b->path[0] ? upsert_locked(b) : fail_msg("book has no path");
	UNLOCK();
	return r;
}

int db_scan_end(void)
{
	return ql("UPDATE books SET missing=CASE WHEN seen=?1 THEN 0 ELSE 1 END",
	          NULL, NULL, "I", g_gen) < 0 ? -1 : 0;
}
