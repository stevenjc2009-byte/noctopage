/* screens.h - the five sidebar screens (placeholders in phase 0). */
#ifndef NOCT_SCREENS_H
#define NOCT_SCREENS_H

#include "app.h"

extern const screen_t screen_mybooks;
extern const screen_t screen_private;
extern const screen_t screen_catalog;
extern const screen_t screen_history;
extern const screen_t screen_downloads;
extern const screen_t screen_settings;

/* Shared placeholder body: the screen title top-left of the content area and a
 * grey "Nothing here yet" line centred in it. */
void screen_placeholder_draw(const char *title);
/* Copies a static hint table into `out` (at most `max`), returns the count. */
int screen_copy_hints(const hint_t *table, int n, hint_t *out, int max);

#endif
