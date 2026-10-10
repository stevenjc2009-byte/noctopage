/* app.h - screen registry, current section, quit dialog. */
#ifndef NOCT_APP_H
#define NOCT_APP_H

#include "lib/db.h"
#include "ui/hintbar.h"
#include "ui/input.h"

typedef struct {
	const char *title;
	void (*enter)(void);
	void (*leave)(void);
	void (*update)(const input_t *in);
	void (*draw)(void);
	int (*hints)(hint_t *out, int max);
} screen_t;

void app_init(void);
void app_update(const input_t *in);
void app_draw(void);
/* Open a book in the reader (the reader takes a copy). */
void app_open_book(const db_book_t *b);
/* Open an OPDS-PSE stream in the reader (not stored in the library). */
void app_open_stream(const char *title, const char *tmpl, int count, const char *user,
                     const char *pass);
/* Queue a next-volume switch after the current reader frame ends. */
void app_request_next(uint64_t id);
/* Close the reader (saving the position) before the app exits. */
void app_shutdown(void);
/* Non-zero once the user confirmed the quit dialog. */
int app_quit_requested(void);

void app_private_moved(int is_private);
void app_message(const char *message);
#endif
