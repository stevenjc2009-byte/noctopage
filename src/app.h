/* app.h - screen registry, current section, quit dialog. */
#ifndef NOCT_APP_H
#define NOCT_APP_H

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
/* Non-zero once the user confirmed the quit dialog. */
int app_quit_requested(void);

#endif
