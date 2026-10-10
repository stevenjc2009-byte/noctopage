/* menu.h - modal popup list (Triangle menus). D-pad / touch choose, confirm picks,
 * back closes. One menu at a time. MAIN THREAD ONLY. */
#ifndef NOCT_MENU_H
#define NOCT_MENU_H

#include "ui/hintbar.h"
#include "ui/input.h"

#include <stdint.h>

#define MENU_MAX 24

/* labels are copied pointers: keep them alive while the menu is open (literals). */
void menu_open(const char *title, const char *const *labels, int n);
/* Move the highlight (e.g. to the current value); ignored when out of range. */
void menu_select(int sel);
int menu_is_open(void);
/* -1 while open or cancelled (then the menu is closed), else the picked index. */
int menu_update(const input_t *in);
void menu_draw(uint64_t now_ms);
int menu_hints(hint_t *out, int max);

#endif
