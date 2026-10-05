/* book.h - the reader for reflowable books (EPUB, MOBI, FB2, TXT, HTML): font size,
 * margins, line spacing, white/sepia/black pages, contents, bookmarks. Modal like reader.h. */
#ifndef NOCT_BOOK_H
#define NOCT_BOOK_H

#include "lib/db.h"
#include "ui/hintbar.h"
#include "ui/input.h"

void book_open(const db_book_t *b);
int book_is_open(void);
void book_update(const input_t *in);
void book_draw(void);
int book_hints(hint_t *out, int max);
void book_close(void);

#endif
