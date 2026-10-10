#ifndef NOCT_SHELF_H
#define NOCT_SHELF_H
#include "lib/db.h"
#include <stddef.h>
enum { SHELF_ALL, SHELF_UNREAD, SHELF_READING, SHELF_FINISHED, SHELF_FAVORITES,
       SHELF_COMICS, SHELF_BOOKS, SHELF_FILTERS };
/* Parent folder below ux0:books, or a root filename ending in Vol/Volume/V + number. */
void shelf_series(const char *path, char *out, size_t cap);
void shelf_book_series(const db_book_t *book, char *out, size_t cap);
int shelf_matches(const db_book_t *book, const char *query, int filter);
int shelf_volume_compare(const db_book_t *a, const db_book_t *b);
int shelf_volume_index(const db_book_t *book);
#endif
