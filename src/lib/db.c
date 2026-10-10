#include "lib/db.h"

#include "lib/db_priv.h"
#include "lib/db_vfs.h"
#include "lib/shelf.h"

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

static void sql_series(sqlite3_context *ctx, int n, sqlite3_value **v)
{
 (void)n; char name[512]; const char *path=(const char *)sqlite3_value_text(v[0]);
 const char *manual=(const char *)sqlite3_value_text(v[1]);
 if (manual && *manual) str_copy(name,sizeof name,manual); else shelf_series(path ? path : "",name,sizeof name);
 sqlite3_result_text(ctx,name,-1,SQLITE_TRANSIENT);
}
int db_open(const char *path)
{
	int64_t g = 0;
	int r = -1;
	const char *vfs = "unix-none";
#ifdef __vita__
	vfs = db_vita_vfs();
#endif

	LOCK();
	if (!vfs) {
		fail_msg("Vita database VFS unavailable");
	} else if (g_db) {
		fail_msg("database already open");
	} else if (sqlite3_open_v2(path, &g_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
	                           vfs) != SQLITE_OK) {
		fail_db();
		sqlite3_close(g_db);
		g_db = NULL;
	} else {
		sqlite3_busy_timeout(g_db, 2000);
		if (sqlite3_create_function(g_db,"series_key",2,SQLITE_UTF8|SQLITE_DETERMINISTIC|SQLITE_INNOCUOUS,NULL,sql_series,NULL,NULL)!=SQLITE_OK || sqlite3_create_collation(g_db, "NAT", SQLITE_UTF8, NULL, nat_collate) != SQLITE_OK) {
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


/* Use fixed SQL and typed SQLite rows, preserving 64-bit content IDs. Never export
 * the servers/downloads tables: even deleted credential pages would be a leak. */
static int backup_exec(const char *sql)
{
	char *err = NULL;
	int rc = sqlite3_exec(g_db, sql, NULL, NULL, &err);
	if (rc != SQLITE_OK) fail_msg(err ? err : "backup database error");
	sqlite3_free(err);
	return rc == SQLITE_OK ? 0 : -1;
}

int db_library_snapshot(const char *path)
{
	int r = -1, attached = 0, transaction = 0;
	LOCK();
	if (q_("ATTACH DATABASE ?1 AS portable", NULL, NULL, "T", path) < 0) goto done;
	attached = 1;
	if (backup_exec("BEGIN") < 0) goto done;
	transaction = 1;
	if (backup_exec(
	    "CREATE TABLE portable.backup_info(version INTEGER);"
	    "INSERT INTO portable.backup_info VALUES(1);"
	    "CREATE TABLE portable.books AS SELECT * FROM main.books;"
	    "CREATE TABLE portable.book_settings AS SELECT * FROM main.book_settings;"
	    "CREATE TABLE portable.bookmarks AS SELECT * FROM main.bookmarks;") < 0) goto done;
	if (backup_exec("COMMIT") < 0) goto done;
	transaction = 0; r = 0;
done:
	{
		char keep[sizeof g_err]; str_copy(keep, sizeof keep, g_err);
		if (transaction) backup_exec("ROLLBACK");
		if (attached && backup_exec("DETACH DATABASE portable") < 0) r = -1;
		if (r < 0) str_copy(g_err, sizeof g_err, keep);
	}
	UNLOCK(); return r;
}

int db_library_restore(const char *path)
{
	int r = -1, attached = 0, transaction = 0;
	int64_t n = 0;
	LOCK();
	if (q_("ATTACH DATABASE ?1 AS portable", NULL, NULL, "T", path) < 0) goto done;
	attached = 1;
	if (q_("SELECT count(*) FROM portable.sqlite_master WHERE type='table'"
	       " AND name IN ('books','book_settings','bookmarks','backup_info')", row_int, &n, "") != 1 || n != 4 ||
	    q_("SELECT count(*) FROM portable.backup_info", row_int, &n, "") != 1 || n != 1 ||
	    q_("SELECT version FROM portable.backup_info", row_int, &n, "") != 1 || n != 1) {
		fail_msg("Not a supported NoctoPage backup"); goto done;
	}
	if (q_("SELECT count(*) FROM portable.books", row_int, &n, "") != 1 || n > 10000 ||
	    q_("SELECT count(*) FROM portable.books GROUP BY id HAVING count(*)>1", row_int, &n, "") != 0 ||
	    q_("SELECT count(*) FROM portable.book_settings", row_int, &n, "") != 1 || n > 100000 ||
	    q_("SELECT count(*) FROM portable.bookmarks", row_int, &n, "") != 1 || n > 100000) {
		fail_msg("Backup has duplicate IDs or too many records"); goto done;
	}
	if (backup_exec("BEGIN IMMEDIATE") < 0) goto done;
	transaction = 1;
	/* Existing file identity and scan state stay local. Missing books retain their
	 * content ID so copying the same book back later reconnects its saved progress. */
	if (backup_exec(
	    "INSERT INTO main.books(id,path,title,author,kind,size,mtime,added,last_read,page,page_count,"
	    "location,progress,finished,missing,seen,favorite,reading_ms,pages_read)"
	    " SELECT id,NULL,substr(title,1,255),substr(author,1,127),kind,size,mtime,added,last_read,"
	    "max(0,page),max(0,page_count),substr(location,1,255),min(1.0,max(0.0,progress)),"
	    "finished<>0,1,0,favorite<>0,max(0,reading_ms),max(0,pages_read) FROM portable.books WHERE 1"
	    " ON CONFLICT(id) DO UPDATE SET title=excluded.title,author=excluded.author,"
	    "last_read=excluded.last_read,page=excluded.page,page_count=excluded.page_count,"
	    "location=excluded.location,progress=excluded.progress,finished=excluded.finished,"
	    "favorite=excluded.favorite,reading_ms=excluded.reading_ms,pages_read=excluded.pages_read;"
	    "DELETE FROM main.book_settings WHERE book_id IN (SELECT id FROM portable.books)"
	    " AND key IN ('mode','fit','rotate','spreads','crop','cover_path','cover_source','private','series');"
	    "INSERT INTO main.book_settings(book_id,key,value)"
	    " SELECT s.book_id,s.key,substr(s.value,1,511) FROM portable.book_settings s"
	    " INNER JOIN portable.books b ON b.id=s.book_id"
	    " WHERE s.key IN ('mode','fit','rotate','spreads','crop','cover_path','cover_source','private','series')"
	    " AND (s.key<>'cover_path' OR s.value='' OR s.value GLOB 'ux0:data/noctopage/covers/restore-*.bin')"
	    " ON CONFLICT(book_id,key) DO UPDATE SET value=excluded.value;"
	    "DELETE FROM main.bookmarks WHERE book_id IN (SELECT id FROM portable.books);"
	    "INSERT INTO main.bookmarks(book_id,location,page,label,created)"
	    " SELECT s.book_id,substr(s.location,1,255),max(0,s.page),substr(s.label,1,127),s.created"
	    " FROM portable.bookmarks s INNER JOIN portable.books b ON b.id=s.book_id;") < 0) goto done;
	if (backup_exec("COMMIT") < 0) goto done;
	transaction = 0; r = 0;
done:
	{
		char keep[sizeof g_err]; str_copy(keep, sizeof keep, g_err);
		if (transaction) backup_exec("ROLLBACK");
		/* A committed restore remains successful even if detach cleanup fails. */
		if (attached) backup_exec("DETACH DATABASE portable");
		if (r < 0) str_copy(g_err, sizeof g_err, keep);
	}
	UNLOCK(); return r;
}

int db_book_remove(uint64_t id)
{
	int r = -1;
	LOCK();
	if (q_("SAVEPOINT remove_book", NULL, NULL, "") < 0) goto done;
	if (q_("DELETE FROM collection_books WHERE book_id=?1",NULL,NULL,"I",I(id))<0 ||
 q_("DELETE FROM move_undo WHERE book_id=?1",NULL,NULL,"I",I(id))<0 || q_("DELETE FROM bookmarks WHERE book_id=?1", NULL, NULL, "I", I(id)) < 0 ||
	    q_("DELETE FROM book_settings WHERE book_id=?1", NULL, NULL, "I", I(id)) < 0 ||
	    (r = q_("DELETE FROM books WHERE id=?1", NULL, NULL, "I", I(id))) < 0 ||
	    q_("RELEASE remove_book", NULL, NULL, "") < 0) {
		char keep[sizeof g_err]; str_copy(keep, sizeof keep, g_err);
		q_("ROLLBACK TO remove_book", NULL, NULL, "");
		q_("RELEASE remove_book", NULL, NULL, "");
		str_copy(g_err, sizeof g_err, keep); r = -1;
	}
done:
	UNLOCK(); return r;
}

/* Batches are one transaction: a failed volume cannot leave half a selection moved. */
int db_bulk_private(const uint64_t *ids, int n, int target)
{
 int r=-1; int64_t batch=0; LOCK();
 if (!ids || n<1 || n>10000 || backup_exec("SAVEPOINT move_batch")<0) goto done;
 if (q_("SELECT ifnull(max(batch),0)+1 FROM move_undo",row_int,&batch,"")<0) goto rollback;
 for (int i=0;i<n;i++) {
  if(q_("INSERT INTO move_undo SELECT ?1,id,EXISTS(SELECT 1 FROM book_settings WHERE book_id=books.id AND key='private' AND value='1') FROM books WHERE id=?2 AND EXISTS(SELECT 1 FROM book_settings WHERE book_id=books.id AND key='private' AND value='1')<>?3",NULL,NULL,"III",batch,I(ids[i]),I(target!=0))<0 ||
     q_("INSERT INTO book_settings SELECT id,'private',?2 FROM books WHERE id=?1 ON CONFLICT(book_id,key) DO UPDATE SET value=excluded.value",NULL,NULL,"IT",I(ids[i]),target ? "1":"0")<0) goto rollback;
 }
 if(q_("DELETE FROM move_undo WHERE batch<=?1-32",NULL,NULL,"I",batch)<0 || backup_exec("RELEASE move_batch")<0) goto rollback;
 r=0; goto done;
rollback: { char keep[256]; str_copy(keep,sizeof keep,g_err); backup_exec("ROLLBACK TO move_batch; RELEASE move_batch"); str_copy(g_err,sizeof g_err,keep); }
done: UNLOCK(); return r;
}
int db_undo_move(void)
{
 int r=-1; int64_t n=0; LOCK();
 if(q_("SELECT count(*) FROM move_undo",row_int,&n,"")<0) goto done;
 if(!n) { r=0; goto done; }
 if(backup_exec("SAVEPOINT undo_move")<0) goto done;
 if(backup_exec("INSERT INTO book_settings SELECT book_id,'private',CAST(previous AS TEXT) FROM move_undo WHERE batch=(SELECT max(batch) FROM move_undo) AND book_id IN (SELECT id FROM books) ON CONFLICT(book_id,key) DO UPDATE SET value=excluded.value; DELETE FROM move_undo WHERE batch=(SELECT max(batch) FROM move_undo); RELEASE undo_move;")<0) {
  char keep[256]; str_copy(keep,sizeof keep,g_err); backup_exec("ROLLBACK TO undo_move; RELEASE undo_move"); str_copy(g_err,sizeof g_err,keep);
 } else r=1;
done: UNLOCK(); return r;
}
int db_reading_snapshot(const char *path)
{
 int r=-1,attached=0; LOCK();
 if(q_("ATTACH DATABASE ?1 AS reading_backup",NULL,NULL,"T",path)<0) goto done;
 attached=1;
 if(backup_exec("BEGIN; CREATE TABLE reading_backup.backup_info(version INTEGER); INSERT INTO reading_backup.backup_info VALUES(2); CREATE TABLE reading_backup.reading_state AS SELECT id,page,page_count,location,progress,finished,last_read,reading_ms,pages_read FROM books WHERE last_read>0 OR finished<>0 OR reading_ms>0; COMMIT;")==0) r=0;
 else backup_exec("ROLLBACK");
done: if(attached) backup_exec("DETACH DATABASE reading_backup"); UNLOCK(); return r;
}
int db_reading_restore(const char *path)
{
 int r=-1,attached=0; int64_t n=0; LOCK();
 if(q_("ATTACH DATABASE ?1 AS reading_backup",NULL,NULL,"T",path)<0) goto done;
 attached=1;
 if(q_("SELECT count(*) FROM reading_backup.sqlite_master WHERE type NOT IN ('table','index') OR (type='table' AND (name NOT IN ('backup_info','reading_state') OR upper(substr(sql,1,12))<>'CREATE TABLE'))",row_int,&n,"")<0 || n) goto done;
 if(q_("SELECT count(*) FROM reading_backup.backup_info",row_int,&n,"")!=1 || n!=1) goto done;
 if(q_("SELECT version FROM reading_backup.backup_info",row_int,&n,"")!=1 || n!=2) goto done;
 if(q_("SELECT count(*) FROM reading_backup.reading_state",row_int,&n,"")<0 || n>10000) goto done;
 if(q_("SELECT count(*)-count(DISTINCT id) FROM reading_backup.reading_state",row_int,&n,"")<0 || n) goto done;
 if(q_("SELECT count(*) FROM reading_backup.reading_state WHERE page<0 OR page_count<0 OR progress<0 OR progress>1 OR length(location)>255 OR reading_ms<0 OR pages_read<0",row_int,&n,"")<0 || n) goto done;
 if(backup_exec("BEGIN; UPDATE books SET (page,page_count,location,progress,finished,last_read,reading_ms,pages_read)=(SELECT page,page_count,location,progress,finished,last_read,reading_ms,pages_read FROM reading_backup.reading_state WHERE id=books.id) WHERE id IN (SELECT id FROM reading_backup.reading_state); COMMIT;")==0) r=0;
 else backup_exec("ROLLBACK");
done: if(attached) backup_exec("DETACH DATABASE reading_backup"); if(r<0) fail_msg("Invalid reading-state backup; progress preserved"); UNLOCK(); return r;
}

int db_apply_series_settings(uint64_t id)
{
 int r=-1; db_book_t book; char series[512];
 if(db_book_get(id,&book)!=1) return -1;
 shelf_book_series(&book,series,sizeof series); if(!series[0]) return -1;
 LOCK(); if(backup_exec("SAVEPOINT series_preferences")<0) goto done;
 if(q_("INSERT INTO series_settings SELECT ?2,key,value FROM book_settings WHERE book_id=?1 AND key IN ('mode','fit','spreads','crop','rotate','font_px','margin_px','spacing10','family','theme') ON CONFLICT(series,key) DO UPDATE SET value=excluded.value",NULL,NULL,"IT",I(id),series)<0 ||
    q_("UPDATE book_settings SET value=(SELECT value FROM series_settings WHERE series=?1 AND key=book_settings.key) WHERE book_id IN (SELECT id FROM books WHERE series_key=?1) AND key IN ('mode','fit','spreads','crop','rotate','font_px','margin_px','spacing10','family','theme') AND EXISTS(SELECT 1 FROM series_settings WHERE series=?1 AND key=book_settings.key)",NULL,NULL,"T",series)<0 || backup_exec("RELEASE series_preferences")<0) {
  char keep[256]; str_copy(keep,sizeof keep,g_err); backup_exec("ROLLBACK TO series_preferences; RELEASE series_preferences"); str_copy(g_err,sizeof g_err,keep);
 } else r=0;
done: UNLOCK(); return r;
}
