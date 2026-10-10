/* reader.h - the comic and fixed-layout reader (CBZ/CB7/CBR/folders, PDF/XPS/DjVu).
 * Modal: while open it owns the screen, the input and the hint bar. */
#ifndef NOCT_READER_H
#define NOCT_READER_H

#include "lib/db.h"
#include "ui/hintbar.h"
#include "ui/input.h"

/* Open b (copied). Resumes at b->page. */
void reader_open(const db_book_t *b);
/* Open an OPDS-PSE stream: title shown, position not saved (no library row). */
void reader_open_stream(const char *title, const char *tmpl, int count, const char *user,
                        const char *pass);
int reader_is_open(void);
/* One frame of input. Circle closes the reader (position saved). */
void reader_update(const input_t *in);
void reader_draw(void);
/* Hints for the hint bar: none unless the overlay is shown. */
int reader_hints(hint_t *out, int max);
/* Save the position and free everything (also used when the app quits). */
void reader_close(void);

#endif
