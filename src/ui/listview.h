/* listview.h - modal scrolling list over the screen (reader contents and bookmarks).
 * D-pad chooses (Left/Right a screenful), confirm picks, Triangle deletes when allowed,
 * back or a tap outside closes. One list at a time. MAIN THREAD ONLY. */
#ifndef NOCT_LISTVIEW_H
#define NOCT_LISTVIEW_H

#include "ui/hintbar.h"
#include "ui/input.h"

#include <stdint.h>

typedef struct {
	const char *label; /* UTF-8 */
	const char *right; /* small grey text on the right (e.g. a page number), may be NULL */
	int depth;         /* indent level */
} lv_item_t;

typedef enum { LV_NONE, LV_PICK, LV_DELETE } lv_action_t;

/* items is NOT copied: keep it (and its strings) alive while the list is open. empty is
 * shown when n == 0. can_delete enables Triangle = LV_DELETE (the list stays open). */
void listview_open(const char *title, const lv_item_t *items, int n, int sel, const char *empty,
                   int can_delete);
/* After a delete, the caller hands over the shortened array (sel is kept in range). */
void listview_set_items(const lv_item_t *items, int n);
int listview_is_open(void);
/* LV_PICK / LV_DELETE with *index set, else LV_NONE (also when it was just closed). */
lv_action_t listview_update(const input_t *in, int *index);
void listview_draw(uint64_t now_ms);
int listview_hints(hint_t *out, int max);

#endif
