#include "lib/db.h"
#include <string.h>
#include <ctype.h>
#include "lib/db_priv.h"

int db_set_private(uint64_t id, int private_book)
{
	return db_bulk_private(&id, 1, private_book);
}

int db_set_series(uint64_t id, const char *series)
{
	if (!series) return -1;
	while (isspace((unsigned char)*series)) series++;
	size_t n = strlen(series);
	while (n && isspace((unsigned char)series[n - 1])) n--;
	if (n > 255) return -1;
	char clean[256]; memcpy(clean, series, n); clean[n] = 0;
	return db_book_setting_set(id, "series", clean);
}

int db_set_favorite(uint64_t id, int favorite)
{
	return db_priv_ql("UPDATE books SET favorite=?2 WHERE id=?1", NULL, NULL, "II",
	                  DBI(id), DBI(favorite != 0));
}

int db_mark_unread(uint64_t id)
{
	return db_priv_ql("UPDATE books SET last_read=0,page=0,location='',progress=0,finished=0 WHERE id=?1",
	                  NULL, NULL, "I", DBI(id));
}

int db_reading_add(uint64_t id, uint64_t milliseconds, int pages)
{
	if (milliseconds > 3600000 || pages < 0 || pages > 10000) return -1;
	return db_priv_ql("UPDATE books SET reading_ms=reading_ms+?2, pages_read=pages_read+?3 WHERE id=?1",
	                  NULL, NULL, "III", DBI(id), DBI(milliseconds), DBI(pages));
}

static int stats_row(sqlite3_stmt *st, void *ud)
{
	db_reading_stats_t *s = ud;
	s->milliseconds = (uint64_t)sqlite3_column_int64(st, 0);
	s->pages = sqlite3_column_int64(st, 1);
	s->finished = sqlite3_column_int(st, 2);
	s->reading = sqlite3_column_int(st, 3);
	s->total = sqlite3_column_int(st, 4);
	return 1;
}

int db_reading_stats(db_reading_stats_t *out)
{
	return db_priv_ql("SELECT ifnull(sum(reading_ms),0),ifnull(sum(pages_read),0),"
	                  "ifnull(sum(missing=0 AND finished<>0),0),"
	                  "ifnull(sum(missing=0 AND last_read>0 AND finished=0),0),"
	                  "ifnull(sum(missing=0),0) FROM books", stats_row, out, "");
}
