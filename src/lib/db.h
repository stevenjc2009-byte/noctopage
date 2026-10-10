/* db â€” the library database (SQLite). One shared connection for the whole process, opened in
 * serialized mode with the no-locking VFS ("unix-none": one process owns the file, newlib has no
 * fcntl locks). Every function takes one module mutex, so any thread may call any function
 * concurrently; multi-statement operations (upsert, scan end) run inside a SAVEPOINT.
 *
 * Return convention: < 0 = error (db_last_error() has the text); otherwise
 *   - get/lookup/continue: 1 found, 0 not found
 *   - list: number of rows written (never more than max)
 *   - update/remove/set_*: 1 if a row was changed, 0 if no such row
 *   - add: 0 and the new row id in *out_id
 * 64-bit book ids are stored as the same bits in SQLite's signed INTEGER.
 *
 * Schema (PRAGMA user_version = 1; see migrations[] in db.c):
 *   books(id PK, path UNIQUE nullable, title, author, kind, size, mtime, added, last_read,
 *         page, page_count, location, progress, finished, missing, seen)
 *   book_settings(book_id, key, value, PK(book_id,key))
 *   bookmarks(id PK, book_id, location, page, label, created)
 *   servers(id PK, name, url, username, password, kind)
 *   downloads(id PK, url, dest_path, title, state, bytes, total, error, created)
 * A book that lost its path to another book has path NULL (read back as "") and missing = 1.
 * Collation "NAT" (natural, ASCII case-insensitive, core/natsort) is registered on the connection.
 *
 * Scanning: db_scan_begin(); for each file: if db_scan_lookup(path,size,mtime) == 1 skip it,
 * else hash it and db_book_upsert(); finally db_scan_end(). Rows not seen in the scan are marked
 * missing (never deleted; progress, settings and bookmarks stay). Two files with identical
 * content share one id and therefore one row: the row follows whichever path was upserted last. */
#ifndef NOCTO_DB_H
#define NOCTO_DB_H

#include <stddef.h>
#include <stdint.h>

enum { DB_KIND_ARCHIVE = 1, DB_KIND_FOLDER = 2, DB_KIND_REFLOW = 3, DB_KIND_FIXED = 4,
       DB_KIND_STREAM = 5 }; /* OPDS-PSE pages over HTTP, never stored in the library */
enum { DB_SORT_TITLE = 0, DB_SORT_LAST_READ = 1, DB_SORT_ADDED = 2 };
enum { DB_DL_QUEUED = 0, DB_DL_ACTIVE = 1, DB_DL_DONE = 2, DB_DL_FAILED = 3, DB_DL_CANCELLED = 4 };

typedef struct {
	uint64_t id;
	char path[512];
	char title[256];
	char author[128];
	int kind;
	uint64_t size;
	int64_t mtime;
	int64_t added;     /* unix s; upsert fills it with "now" when 0 */
	int64_t last_read; /* unix s, 0 = never */
	int page;
	int page_count;
	char location[256]; /* opaque position string for reflow books */
	double progress;    /* 0..1 */
	int finished;
	int missing;
	int favorite;
	int private_book; /* hidden from public shelf, history and continue */
	char series[256]; /* optional user-assigned series; empty = detect from path */
	uint64_t reading_ms;
	int64_t pages_read;
} db_book_t;

typedef struct {
	int64_t id;
	uint64_t book_id;
	char location[256];
	int page;
	char label[128];
	int64_t created;
} db_bookmark_t;

typedef struct {
	int64_t id;
	char name[64];
	char url[256];
	char username[64];
	char password[64];
	char kind[16]; /* e.g. "opds", "komga" */
} db_server_t;

typedef struct {
	int64_t id;
	char url[2048];
	char dest_path[512];
	char title[256];
	int state; /* DB_DL_* */
	int64_t bytes;
	int64_t total;
	char error[128];
	int64_t created;
} db_download_t;

/* Open (creating and migrating as needed) / close. db_open on an open database fails. A database
 * with a user_version newer than this code knows is refused. */
int db_open(const char *path);
void db_close(void);
/* Text of the last error. The pointer is to a shared buffer (another thread's failure may
 * overwrite it); db_last_error_copy() takes a consistent snapshot. */
const char *db_last_error(void);
void db_last_error_copy(char *out, size_t cap);
/* Clock hook for tests (unix seconds); NULL restores time(NULL). */
void db_set_clock(int64_t (*fn)(void));

/* Library scan. */
int db_scan_begin(void);
int db_scan_lookup(const char *path, uint64_t size, int64_t mtime);
int db_book_upsert(const db_book_t *b); /* returns 1 new row, 0 existing row updated */
int db_scan_end(void);

typedef struct {
	uint64_t milliseconds;
	int64_t pages;
	int finished, reading, total;
} db_reading_stats_t;
int db_set_favorite(uint64_t id, int favorite);
int db_set_private(uint64_t id, int private_book);
int db_set_series(uint64_t id, const char *series);
int db_books_count_section(int private_section);
int db_books_list_section(int sort, int private_section, db_book_t *out, int max);
int db_continue_reading_section(int private_section, db_book_t *out);
int db_continue_series_section(const char *series, int private_section, db_book_t *out);
int db_mark_unread(uint64_t id);
int db_reading_add(uint64_t id, uint64_t milliseconds, int pages);
int db_reading_stats(db_reading_stats_t *out);
/* Natural filename order within the same series folder; root books have no series. */
int db_continue_series(const char *series, db_book_t *out);
int db_next_volume(uint64_t id, db_book_t *out);

/* Queries. */
int db_books_count(void); /* visible library rows, or -1 */
int db_books_list(int sort, db_book_t *out, int max); /* excludes missing */
int db_book_get(uint64_t id, db_book_t *out);
int db_continue_reading(db_book_t *out);
int db_history_list(db_book_t *out, int max);         /* last_read > 0, newest first, incl. missing */
int db_history_remove(uint64_t id);                   /* last_read = 0 */

/* Updates. db_set_progress sets last_read = now and finished = (progress >= 0.999 or
 * (page_count > 0 and page == page_count - 1)); it recomputes finished on every call. */
int db_set_progress(uint64_t id, int page, int page_count, const char *location, double progress);
int db_set_finished(uint64_t id, int finished);
int db_set_title(uint64_t id, const char *title, const char *author);

/* Per-book settings: get returns 1 (value copied, truncated to cap) or 0 (not set). */
int db_book_setting_get(uint64_t id, const char *key, char *out, size_t cap);
int db_book_setting_set(uint64_t id, const char *key, const char *value);

/* Delete a book row with its settings and bookmarks (the file itself is removed by
 * the caller first). Returns < 0 on DB failure. */
int db_book_remove(uint64_t id);

/* Bookmarks (list: by page, then creation order). created = 0 in b means "now". */
int db_bookmark_add(const db_bookmark_t *b, int64_t *out_id);
int db_bookmark_list(uint64_t book_id, db_bookmark_t *out, int max);
int db_bookmark_remove(int64_t id);

/* OPDS servers (list in id order). */
int db_server_add(const db_server_t *s, int64_t *out_id);
int db_server_update(const db_server_t *s); /* by s->id */
int db_server_list(db_server_t *out, int max);
int db_server_remove(int64_t id);

/* Downloads (list in id order). created = 0 in d means "now". */
int db_download_add(const db_download_t *d, int64_t *out_id);
int db_download_update(int64_t id, int state, int64_t bytes, int64_t total, const char *error);
int db_download_list(db_download_t *out, int max);
int db_download_remove(int64_t id);

/* Portable backup metadata: excludes servers and download credentials. */
int db_library_snapshot(const char *path);
int db_library_restore(const char *path);

typedef struct { int64_t id; char name[128]; } db_collection_t;
int db_series_volume_numbers(const char *series,unsigned char *owned,int cap);
int db_bulk_private(const uint64_t *ids,int n,int target);
int db_undo_move(void);
int db_reading_snapshot(const char *path);
int db_reading_restore(const char *path);
int db_series_setting_get(const char *series,const char *key,char *out,size_t cap);
int db_series_setting_set(const char *series,const char *key,const char *value);
int db_effective_setting(uint64_t id,const char *key,char *out,size_t cap);
int db_apply_series_settings(uint64_t id);
int db_collections_page(int offset,db_collection_t *out,int max);
int db_collections(db_collection_t *out,int max);
int db_collection_create(const char *name,int64_t *id);
int db_collection_add(int64_t collection,uint64_t book);
int db_collection_contains(int64_t collection,uint64_t book);
int db_collection_remove(int64_t collection,uint64_t book);
#endif
