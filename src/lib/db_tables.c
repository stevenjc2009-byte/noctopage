/* db_tables â€” schema, read-only book queries, per-book settings, bookmarks, servers, downloads.
 * Everything here is single-statement SQL through db_priv_ql(); db.c holds the connection and
 * the multi-statement scan/upsert logic. */
#include "lib/db.h"

#include "lib/db_priv.h"
#include "lib/shelf.h"
#include "core/natsort.h"

#include <stdint.h>
#include <string.h>

#define I(x)     DBI(x)
#define ql       db_priv_ql
#define col_str  db_priv_col_str

/* migrations[n] upgrades user_version n to n + 1. */
const char *const db_migrations[DB_SCHEMA_VERSION] = {
	"CREATE TABLE books(id INTEGER PRIMARY KEY, path TEXT UNIQUE, title TEXT NOT NULL DEFAULT '',"
	" author TEXT NOT NULL DEFAULT '', kind INTEGER NOT NULL DEFAULT 0, size INTEGER NOT NULL DEFAULT 0,"
	" mtime INTEGER NOT NULL DEFAULT 0, added INTEGER NOT NULL DEFAULT 0,"
	" last_read INTEGER NOT NULL DEFAULT 0, page INTEGER NOT NULL DEFAULT 0,"
	" page_count INTEGER NOT NULL DEFAULT 0, location TEXT NOT NULL DEFAULT '',"
	" progress REAL NOT NULL DEFAULT 0, finished INTEGER NOT NULL DEFAULT 0,"
	" missing INTEGER NOT NULL DEFAULT 0, seen INTEGER NOT NULL DEFAULT 0);"
	"CREATE TABLE book_settings(book_id INTEGER NOT NULL, key TEXT NOT NULL, value TEXT NOT NULL,"
	" PRIMARY KEY(book_id, key));"
	"CREATE TABLE bookmarks(id INTEGER PRIMARY KEY, book_id INTEGER NOT NULL, location TEXT NOT NULL,"
	" page INTEGER NOT NULL DEFAULT 0, label TEXT NOT NULL DEFAULT '', created INTEGER NOT NULL);"
	"CREATE INDEX bookmarks_book ON bookmarks(book_id);"
	"CREATE TABLE servers(id INTEGER PRIMARY KEY, name TEXT NOT NULL, url TEXT NOT NULL,"
	" username TEXT NOT NULL DEFAULT '', password TEXT NOT NULL DEFAULT '', kind TEXT NOT NULL DEFAULT '');"
	"CREATE TABLE downloads(id INTEGER PRIMARY KEY, url TEXT NOT NULL, dest_path TEXT NOT NULL,"
	" title TEXT NOT NULL DEFAULT '', state INTEGER NOT NULL DEFAULT 0, bytes INTEGER NOT NULL DEFAULT 0,"
	" total INTEGER NOT NULL DEFAULT 0, error TEXT NOT NULL DEFAULT '', created INTEGER NOT NULL);",
	"ALTER TABLE books ADD COLUMN favorite INTEGER NOT NULL DEFAULT 0;"
	"ALTER TABLE books ADD COLUMN reading_ms INTEGER NOT NULL DEFAULT 0;"
	"ALTER TABLE books ADD COLUMN pages_read INTEGER NOT NULL DEFAULT 0;",
 "ALTER TABLE books ADD COLUMN series_key TEXT NOT NULL DEFAULT '';"
 "UPDATE books SET series_key=series_key(path,ifnull((SELECT value FROM book_settings WHERE book_id=books.id AND key='series'),''));"
 "CREATE INDEX books_series ON books(series_key,missing);"
 "CREATE TRIGGER series_path AFTER INSERT ON books BEGIN UPDATE books SET series_key=series_key(new.path,ifnull((SELECT value FROM book_settings WHERE book_id=new.id AND key='series'),'')) WHERE id=new.id; END;"
 "CREATE TRIGGER series_path_update AFTER UPDATE OF path ON books BEGIN UPDATE books SET series_key=series_key(new.path,ifnull((SELECT value FROM book_settings WHERE book_id=new.id AND key='series'),'')) WHERE id=new.id; END;"
 "CREATE TRIGGER series_setting AFTER INSERT ON book_settings WHEN new.key='series' BEGIN UPDATE books SET series_key=series_key(path,new.value) WHERE id=new.book_id; END;"
 "CREATE TRIGGER series_setting_update AFTER UPDATE OF value ON book_settings WHEN new.key='series' BEGIN UPDATE books SET series_key=series_key(path,new.value) WHERE id=new.book_id; END;"
 "CREATE TRIGGER series_setting_delete AFTER DELETE ON book_settings WHEN old.key='series' BEGIN UPDATE books SET series_key=series_key(path,'') WHERE id=old.book_id; END;"
 "CREATE TABLE series_settings(series TEXT NOT NULL,key TEXT NOT NULL,value TEXT NOT NULL,PRIMARY KEY(series,key));"
 "CREATE TABLE collections(id INTEGER PRIMARY KEY,name TEXT NOT NULL UNIQUE);"
 "CREATE TABLE collection_books(collection_id INTEGER NOT NULL,book_id INTEGER NOT NULL,PRIMARY KEY(collection_id,book_id));"
 "INSERT INTO collections(name) VALUES('Reading next');"
 "CREATE TABLE move_undo(batch INTEGER NOT NULL,book_id INTEGER NOT NULL,previous INTEGER NOT NULL,PRIMARY KEY(batch,book_id));",
};


#define BOOK_COLS "id,ifnull(path,''),title,author,kind,size,mtime,added,last_read,page,page_count," \
				  "location,progress,finished,missing,favorite,reading_ms,pages_read," \
				  "EXISTS(SELECT 1 FROM book_settings s WHERE s.book_id=books.id AND s.key='private' AND s.value='1')," \
				  "ifnull((SELECT value FROM book_settings s WHERE s.book_id=books.id AND s.key='series'),'')"
#define PUBLIC_BOOK " NOT EXISTS(SELECT 1 FROM book_settings s WHERE s.book_id=books.id AND s.key='private' AND s.value='1')"
#define SECTION_BOOK " EXISTS(SELECT 1 FROM book_settings s WHERE s.book_id=books.id AND s.key='private' AND s.value='1')=?2"

static void book_from_row(sqlite3_stmt *st, db_book_t *b)
{
	b->id = (uint64_t)sqlite3_column_int64(st, 0);
	col_str(st, 1, b->path, sizeof b->path);
	col_str(st, 2, b->title, sizeof b->title);
	col_str(st, 3, b->author, sizeof b->author);
	b->kind = sqlite3_column_int(st, 4);
	b->size = (uint64_t)sqlite3_column_int64(st, 5);
	b->mtime = sqlite3_column_int64(st, 6);
	b->added = sqlite3_column_int64(st, 7);
	b->last_read = sqlite3_column_int64(st, 8);
	b->page = sqlite3_column_int(st, 9);
	b->page_count = sqlite3_column_int(st, 10);
	col_str(st, 11, b->location, sizeof b->location);
	b->progress = sqlite3_column_double(st, 12);
	b->finished = sqlite3_column_int(st, 13);
	b->missing = sqlite3_column_int(st, 14);
	b->favorite = sqlite3_column_int(st, 15);
	b->reading_ms = (uint64_t)sqlite3_column_int64(st, 16);
	b->pages_read = sqlite3_column_int64(st, 17);
	b->private_book = sqlite3_column_int(st, 18);
	col_str(st, 19, b->series, sizeof b->series);
}

typedef struct {
	db_book_t *out;
	int max, n;
} book_list_t;

static int row_book(sqlite3_stmt *st, void *ud)
{
	book_list_t *l = (book_list_t *)ud;
	book_from_row(st, &l->out[l->n++]);
	return l->n >= l->max;
}

#define books_query(sql, out, max, fmt, ...) 	((max) <= 0 ? 0 : db_priv_ql(sql, row_book, &(book_list_t){ (out), (max), 0 }, fmt, ##__VA_ARGS__))


static int row_count(sqlite3_stmt *st, void *ud)
{
	sqlite3_int64 n = sqlite3_column_int64(st, 0);
	*(int *)ud = n > INT32_MAX ? INT32_MAX : (int)n;
	return 1;
}

int db_books_count(void)
{
	int n = 0;
	return ql("SELECT count(*) FROM books WHERE missing=0", row_count, &n, "") < 0 ? -1 : n;
}

int db_books_count_section(int private_section)
{
	int n = 0;
	return ql("SELECT count(*) FROM books WHERE missing=0 AND "
	          "EXISTS(SELECT 1 FROM book_settings s WHERE s.book_id=books.id AND s.key='private' AND s.value='1')=?1",
	          row_count, &n, "I", I(private_section != 0)) < 0 ? -1 : n;
}

int db_books_list_section(int sort, int private_section, db_book_t *out, int max)
{
	static const char *const sql[] = {
		"SELECT " BOOK_COLS " FROM books WHERE missing=0 AND " SECTION_BOOK " ORDER BY title COLLATE NAT,id LIMIT ?1",
		"SELECT " BOOK_COLS " FROM books WHERE missing=0 AND " SECTION_BOOK " ORDER BY last_read DESC,title COLLATE NAT,id LIMIT ?1",
		"SELECT " BOOK_COLS " FROM books WHERE missing=0 AND " SECTION_BOOK " ORDER BY added DESC,title COLLATE NAT,id LIMIT ?1",
	};
	if (sort < 0 || sort > DB_SORT_ADDED) sort = DB_SORT_TITLE;
	return books_query(sql[sort], out, max, "II", I(max), I(private_section != 0));
}

int db_books_list(int sort, db_book_t *out, int max)
{
	/* Last read: never-read rows (last_read 0) fall last under DESC, then title. */
	static const char *const sql[] = {
		"SELECT " BOOK_COLS " FROM books WHERE missing=0 ORDER BY title COLLATE NAT, id LIMIT ?1",
		"SELECT " BOOK_COLS " FROM books WHERE missing=0 ORDER BY last_read DESC,"
		" title COLLATE NAT, id LIMIT ?1",
		"SELECT " BOOK_COLS " FROM books WHERE missing=0 ORDER BY added DESC, title COLLATE NAT, id LIMIT ?1",
	};
	if (sort < 0 || sort > DB_SORT_ADDED)
		sort = DB_SORT_TITLE;
	return books_query(sql[sort], out, max, "I", I(max));
}

int db_history_list(db_book_t *out, int max)
{
	return books_query("SELECT " BOOK_COLS " FROM books WHERE last_read>0 AND " PUBLIC_BOOK " ORDER BY last_read DESC, id LIMIT ?1",
	                   out, max, "I", I(max));
}

int db_book_get(uint64_t id, db_book_t *out)
{
	return books_query("SELECT " BOOK_COLS " FROM books WHERE id=?1", out, 1, "I", I(id));
}

int db_continue_reading_section(int private_section, db_book_t *out)
{
	return books_query("SELECT " BOOK_COLS " FROM books WHERE last_read>0 AND finished=0 AND missing=0"
	                   " AND " SECTION_BOOK " ORDER BY last_read DESC, id LIMIT ?1", out, 1, "II", I(1), I(private_section != 0));
}

int db_continue_reading(db_book_t *out)
{
	return db_continue_reading_section(0, out);
}

int db_history_remove(uint64_t id)
{
	return ql("UPDATE books SET last_read=0 WHERE id=?1", NULL, NULL, "I", I(id));
}

int db_set_progress(uint64_t id, int page, int page_count, const char *location, double progress)
{
	return ql("UPDATE books SET page=?2, page_count=?3, location=?4, progress=?5, last_read=?6,"
	          " finished=CASE WHEN ?5>=0.999 OR (?3>0 AND ?2=?3-1) THEN 1 ELSE 0 END WHERE id=?1",
	          NULL, NULL, "IIITDI", I(id), I(page), I(page_count), location ? location : "", progress,
	          db_priv_now());
}

int db_set_finished(uint64_t id, int finished)
{
	return ql("UPDATE books SET finished=?2 WHERE id=?1", NULL, NULL, "II", I(id), I(finished != 0));
}

int db_set_title(uint64_t id, const char *title, const char *author)
{
	return ql("UPDATE books SET title=?2, author=?3 WHERE id=?1", NULL, NULL, "ITT", I(id),
	          title ? title : "", author ? author : "");
}

/* ---- per-book settings ------------------------------------------------------------------- */

typedef struct {
	char *out;
	size_t cap;
} text_dst_t;

static int row_text(sqlite3_stmt *st, void *ud)
{
	text_dst_t *d = (text_dst_t *)ud;
	if (d->cap)
		col_str(st, 0, d->out, d->cap);
	return 1;
}

int db_book_setting_get(uint64_t id, const char *key, char *out, size_t cap)
{
	text_dst_t d = { out, cap };
	if (cap)
		out[0] = '\0';
	return ql("SELECT value FROM book_settings WHERE book_id=?1 AND key=?2", row_text, &d, "IT", I(id), key);
}

int db_book_setting_set(uint64_t id, const char *key, const char *value)
{
	int r = ql("INSERT INTO book_settings(book_id,key,value) VALUES(?1,?2,?3)"
	           " ON CONFLICT(book_id,key) DO UPDATE SET value=excluded.value", NULL, NULL, "ITT", I(id), key,
	           value ? value : "");
	return r < 0 ? r : 0;
}

/* ---- bookmarks --------------------------------------------------------------------------- */

typedef struct {
	db_bookmark_t *out;
	int max, n;
} bm_list_t;

static int row_bookmark(sqlite3_stmt *st, void *ud)
{
	bm_list_t *l = (bm_list_t *)ud;
	db_bookmark_t *b = &l->out[l->n++];
	b->id = sqlite3_column_int64(st, 0);
	b->book_id = (uint64_t)sqlite3_column_int64(st, 1);
	col_str(st, 2, b->location, sizeof b->location);
	b->page = sqlite3_column_int(st, 3);
	col_str(st, 4, b->label, sizeof b->label);
	b->created = sqlite3_column_int64(st, 5);
	return l->n >= l->max;
}

int db_bookmark_add(const db_bookmark_t *b, int64_t *out_id)
{
	return db_priv_insert("INSERT INTO bookmarks(book_id,location,page,label,created) VALUES(?1,?2,?3,?4,?5)",
	                      out_id, "ITITI", I(b->book_id), b->location, I(b->page), b->label,
	                      b->created ? b->created : db_priv_now());
}

int db_bookmark_list(uint64_t book_id, db_bookmark_t *out, int max)
{
	bm_list_t l = { out, max, 0 };
	return max <= 0 ? 0 : ql("SELECT id,book_id,location,page,label,created FROM bookmarks WHERE book_id=?1"
	                         " ORDER BY page, id LIMIT ?2", row_bookmark, &l, "II", I(book_id), I(max));
}

int db_bookmark_remove(int64_t id)
{
	return ql("DELETE FROM bookmarks WHERE id=?1", NULL, NULL, "I", id);
}


/* ---- OPDS servers ------------------------------------------------------------------------ */

typedef struct {
	db_server_t *out;
	int max, n;
} srv_list_t;

static int row_server(sqlite3_stmt *st, void *ud)
{
	srv_list_t *l = (srv_list_t *)ud;
	db_server_t *s = &l->out[l->n++];
	s->id = sqlite3_column_int64(st, 0);
	col_str(st, 1, s->name, sizeof s->name);
	col_str(st, 2, s->url, sizeof s->url);
	col_str(st, 3, s->username, sizeof s->username);
	col_str(st, 4, s->password, sizeof s->password);
	col_str(st, 5, s->kind, sizeof s->kind);
	return l->n >= l->max;
}

int db_server_add(const db_server_t *s, int64_t *out_id)
{
	return db_priv_insert("INSERT INTO servers(name,url,username,password,kind) VALUES(?1,?2,?3,?4,?5)",
	                      out_id, "TTTTT", s->name, s->url, s->username, s->password, s->kind);
}

int db_server_update(const db_server_t *s)
{
	return ql("UPDATE servers SET name=?2, url=?3, username=?4, password=?5, kind=?6 WHERE id=?1", NULL, NULL,
	          "ITTTTT", s->id, s->name, s->url, s->username, s->password, s->kind);
}

int db_server_list(db_server_t *out, int max)
{
	srv_list_t l = { out, max, 0 };
	return max <= 0 ? 0 : ql("SELECT id,name,url,username,password,kind FROM servers ORDER BY id LIMIT ?1",
	                         row_server, &l, "I", I(max));
}

int db_server_remove(int64_t id)
{
	return ql("DELETE FROM servers WHERE id=?1", NULL, NULL, "I", id);
}

/* ---- downloads --------------------------------------------------------------------------- */

typedef struct {
	db_download_t *out;
	int max, n;
} dl_list_t;

static int row_download(sqlite3_stmt *st, void *ud)
{
	dl_list_t *l = (dl_list_t *)ud;
	db_download_t *d = &l->out[l->n++];
	d->id = sqlite3_column_int64(st, 0);
	col_str(st, 1, d->url, sizeof d->url);
	col_str(st, 2, d->dest_path, sizeof d->dest_path);
	col_str(st, 3, d->title, sizeof d->title);
	d->state = sqlite3_column_int(st, 4);
	d->bytes = sqlite3_column_int64(st, 5);
	d->total = sqlite3_column_int64(st, 6);
	col_str(st, 7, d->error, sizeof d->error);
	d->created = sqlite3_column_int64(st, 8);
	return l->n >= l->max;
}

int db_download_add(const db_download_t *d, int64_t *out_id)
{
	return db_priv_insert("INSERT INTO downloads(url,dest_path,title,state,bytes,total,error,created)"
	                      " VALUES(?1,?2,?3,?4,?5,?6,?7,?8)", out_id, "TTTIIITI", d->url, d->dest_path, d->title,
	                      I(d->state), d->bytes, d->total, d->error, d->created ? d->created : db_priv_now());
}

int db_download_update(int64_t id, int state, int64_t bytes, int64_t total, const char *error)
{
	return ql("UPDATE downloads SET state=?2, bytes=?3, total=?4, error=?5 WHERE id=?1", NULL, NULL, "IIIIT",
	          id, I(state), bytes, total, error ? error : "");
}

int db_download_list(db_download_t *out, int max)
{
	dl_list_t l = { out, max, 0 };
	return max <= 0 ? 0 : ql("SELECT id,url,dest_path,title,state,bytes,total,error,created FROM downloads"
	                         " ORDER BY id LIMIT ?1", row_download, &l, "I", I(max));
}

int db_download_remove(int64_t id)
{
	return ql("DELETE FROM downloads WHERE id=?1", NULL, NULL, "I", id);
}

typedef struct {
	db_book_t current, next;
	char series[512];
	int found;
} next_ctx_t;

static int row_next(sqlite3_stmt *st, void *ud)
{
	next_ctx_t *c = ud;
	db_book_t b;
	char series[512];
	book_from_row(st, &b);
	shelf_book_series(&b, series, sizeof series);
	if (b.private_book != c->current.private_book || strcmp(series, c->series) || shelf_volume_compare(&b, &c->current) <= 0) return 0;
	if (!c->found || shelf_volume_compare(&b, &c->next) < 0) {
		c->next = b;
		c->found = 1;
	}
	return 0;
}

int db_next_volume(uint64_t id, db_book_t *out)
{
	next_ctx_t c = {0};
	if (db_book_get(id, &c.current) != 1 || c.current.missing) return 0;
	shelf_book_series(&c.current, c.series, sizeof c.series);
	if (!c.series[0]) return 0;
	if (ql("SELECT " BOOK_COLS " FROM books WHERE missing=0 AND series_key=?3 AND " SECTION_BOOK " LIMIT ?1", row_next, &c, "IIT", I(-1), I(c.current.private_book), c.series) < 0) return -1;
	if (c.found) *out = c.next;
	return c.found;
}


typedef struct { const char *series; db_book_t best; int found; } series_ctx_t;
static int row_series_continue(sqlite3_stmt *st, void *ud)
{
	series_ctx_t *c = ud;
	db_book_t b; char series[512];
	book_from_row(st, &b);
	shelf_book_series(&b, series, sizeof series);
	if (strcmp(series, c->series)) return 0;
	/* Resume the most recently started volume, otherwise begin the first unread one. */
	if (!c->found || b.last_read > c->best.last_read ||
	    (b.last_read == c->best.last_read && shelf_volume_compare(&b, &c->best) < 0)) {
		c->best = b; c->found = 1;
	}
	return 0;
}
int db_continue_series_section(const char *series, int private_section, db_book_t *out)
{
	if (!series || !*series || !out) return 0;
	series_ctx_t c = {.series = series};
	if (ql("SELECT " BOOK_COLS " FROM books WHERE missing=0 AND finished=0 AND series_key=?3 AND " SECTION_BOOK " LIMIT ?1",
	       row_series_continue, &c, "IIT", I(-1), I(private_section != 0), series) < 0) return -1;
	if (c.found) *out = c.best;
	return c.found;
}

int db_continue_series(const char *series, db_book_t *out)
{
	return db_continue_series_section(series, 0, out);
}

int db_series_setting_get(const char *series,const char *key,char *out,size_t cap)
{
 text_dst_t d={out,cap}; if(cap) out[0]=0;
 return ql("SELECT value FROM series_settings WHERE series=?1 AND key=?2",row_text,&d,"TT",series,key);
}
int db_series_setting_set(const char *series,const char *key,const char *value)
{
 return ql("INSERT INTO series_settings VALUES(?1,?2,?3) ON CONFLICT(series,key) DO UPDATE SET value=excluded.value",NULL,NULL,"TTT",series,key,value)<0 ? -1:0;
}
int db_effective_setting(uint64_t id,const char *key,char *out,size_t cap)
{
 int r=db_book_setting_get(id,key,out,cap); if(r!=0) return r;
 db_book_t b; char series[512]; if(db_book_get(id,&b)!=1) return 0;
 shelf_book_series(&b,series,sizeof series); return db_series_setting_get(series,key,out,cap);
}
static int collection_row(sqlite3_stmt *st,void *ud)
{
 db_collection_t **p=ud; (*p)->id=sqlite3_column_int64(st,0); col_str(st,1,(*p)->name,sizeof (*p)->name); (*p)++; return 0;
}
int db_collections(db_collection_t *out,int max)
{
 return max>0 ? ql("SELECT id,name FROM collections ORDER BY name COLLATE NAT LIMIT ?1",collection_row,&out,"I",I(max)):0;
}
int db_collection_create(const char *name,int64_t *id)
{
 if(!name || !*name || strlen(name)>127) return -1;
 return db_priv_insert("INSERT INTO collections(name) VALUES(?1)",id,"T",name);
}
int db_collection_add(int64_t collection,uint64_t book)
{
 return ql("INSERT OR IGNORE INTO collection_books SELECT ?1,id FROM books WHERE id=?2 AND EXISTS(SELECT 1 FROM collections WHERE id=?1)",NULL,NULL,"II",collection,I(book));
}
int db_collection_contains(int64_t collection,uint64_t book)
{
 int n=0; return ql("SELECT count(*) FROM collection_books WHERE collection_id=?1 AND book_id=?2",row_count,&n,"II",collection,I(book))<0 ? 0:n;
}
int db_collection_remove(int64_t collection,uint64_t book)
{
 return ql("DELETE FROM collection_books WHERE collection_id=?1 AND book_id=?2",NULL,NULL,"II",collection,I(book));
}

typedef struct { unsigned char *owned; int cap,unknown; } volume_numbers_t;
static int volume_number_row(sqlite3_stmt *st,void *ud)
{
 volume_numbers_t *v=ud; db_book_t book={0}; col_str(st,0,book.path,sizeof book.path);
 int n=shelf_volume_index(&book); if(n>0 && n<v->cap) v->owned[n]=1; else v->unknown++;
 return 0;
}
int db_series_volume_numbers(const char *series,unsigned char *owned,int cap)
{
 if(!owned || cap<2 || cap>1001) return -1;
 memset(owned,0,(size_t)cap); volume_numbers_t v={owned,cap,0};
 if(ql("SELECT path FROM books WHERE missing=0 AND series_key=?1",volume_number_row,&v,"T",series)<0) return -1;
 return v.unknown;
}

int db_collections_page(int offset,db_collection_t *out,int max)
{
 if(offset<0 || max<1) return 0;
 return ql("SELECT id,name FROM collections ORDER BY name COLLATE NAT LIMIT ?1 OFFSET ?2",collection_row,&out,"II",I(max),I(offset));
}
