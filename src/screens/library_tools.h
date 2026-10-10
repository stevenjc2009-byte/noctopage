#ifndef NOCT_LIBRARY_TOOLS_H
#define NOCT_LIBRARY_TOOLS_H
#include "lib/db.h"
#include "ui/hintbar.h"
#include "ui/input.h"
void library_edit_title(const db_book_t *book);
void library_cover_search(const db_book_t *book);
void library_cover_local(const db_book_t *book);
void library_stats_open(const db_book_t *book); /* NULL = library totals */
void library_delete_book(const db_book_t *book); /* confirm dialog, then delete file + row */
int library_tools_busy(void);
int library_tools_update(const input_t *in); /* 1 after a cover was saved */
void library_tools_draw(uint64_t now);
int library_tools_hints(hint_t *out, int max);
void library_tools_close(void);
void library_tools_message(const char *text);
void library_series_cover(const db_book_t *book,const char *series,int online);
#endif
