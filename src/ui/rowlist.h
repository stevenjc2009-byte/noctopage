/* rowlist.h - shared geometry and behaviour for the sidebar row-list screens
 * (History, Downloads, Settings): same panel, same paging, same empty state. */
#ifndef NOCT_ROWLIST_H
#define NOCT_ROWLIST_H

#include "ui/input.h"

#include <stdint.h>

#define ROWLIST_X0 (205 + 16)
#define ROWLIST_W (755 - 32)
#define ROWLIST_HEAD_H 56
#define ROWLIST_LIST_H 521 /* rows must end above this y */
#define ROWLIST_ROWS(row_h) ((ROWLIST_LIST_H - ROWLIST_HEAD_H) / (row_h))
#define ROWLIST_CENTRE_X 582 /* empty-state text centre */
#define ROWLIST_EMPTY_Y 230
#define ROWLIST_EMPTY_SUB_Y 266

typedef struct {
	int sel, top;
} rowlist_t;

/* D-pad paging with clamping (no wrap): Up/Down one row, Left/Right one page. */
void rowlist_move(rowlist_t *r, int count, int rows, const input_t *in);
/* Keep sel/top valid after the row count changed. */
void rowlist_keep(rowlist_t *r, int count, int rows);
/* Y of a row's top edge. */
int rowlist_y(int row, int top, int row_h);
/* Centred "nothing here" two-liner. */
void rowlist_empty(const char *a, const char *b);
/* Selected-row highlight + pulsing selector around (x0, y, w, row_h - 4). */
void rowlist_selected(int x0, int w, int y, int row_h, uint64_t now);

#endif
