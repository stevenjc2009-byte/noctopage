/* NoctoPage shelf cards: covers above a separate title panel, roomy gutters,
 * clipped kinetic scrolling, a static teal selector and a slim scrollbar. */
#ifndef NOCT_GRID_H
#define NOCT_GRID_H

#include "ui/input.h"

#include <stdint.h>
#include <vita2d.h>

#define GRID_COLS   4
#define GRID_W      164
#define GRID_H      302
#define GRID_GAP    18
#define GRID_TOP    8   /* first row y inside the scroll area */
#define GRID_VIEW_Y 58   /* scroll area top (screen y) */
#define GRID_VIEW_H 425 /* ends above library status */

typedef struct {
	const char *title;     /* UTF-8; two lines, ellipsised */
	const char *badge;     /* NULL = none; inset cover badge */
	vita2d_texture *cover; /* NULL = placeholder fill; any size, cover-fill cropped */
	float progress;        /* < 0 = no bar; 0..1 = thin bar along the bottom */
	int actions;           /* show a tappable three-dot volume menu */
	int finished;
 int selected;          /* draws a check mark */
} grid_item_t;

/* Fills *out for item index (0..count-1). Strings must stay valid until grid_draw returns. */
typedef void (*grid_item_fn)(void *user, int index, grid_item_t *out);

typedef struct {
	int count, sel;
	int actions; /* enable three-dot touch targets */
	float scroll, vel;      /* scroll offset in px; kinetic velocity */
	int touching, dragging; /* finger down; moved past the drag threshold */
	int touch_y0, touch_last_y;
	int touch_x0;
	float scroll0;
	int follow;             /* ease the view to the selection */
} grid_t;

enum { GRID_NONE = 0, GRID_OPEN = 1, GRID_MOVED = 2, GRID_MENU = 3 };

void grid_init(grid_t *g);
/* Sets the item count, keeping the selection in range. */
void grid_set_count(grid_t *g, int count);
/* D-pad moves the selection; a tap opens; drag scrolls. Returns GRID_OPEN when the
 * selected item should open (Confirm or tap), GRID_MOVED when the selection changed. */
int grid_update(grid_t *g, const input_t *in);
void grid_draw(grid_t *g, grid_item_fn fn, void *user, uint64_t now_ms);
/* Range of items currently on screen (for loading covers in priority order). */
void grid_visible(const grid_t *g, int *first, int *last);

#endif
