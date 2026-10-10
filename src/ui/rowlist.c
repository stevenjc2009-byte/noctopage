/* rowlist.c - see rowlist.h. */
#include "ui/rowlist.h"

#include "ui/draw.h"
#include "ui/font.h"
#include "ui/theme.h"

void rowlist_move(rowlist_t *r, int count, int rows, const input_t *in)
{
	if (count <= 0) {
		r->sel = r->top = 0;
		return;
	}
	if (in->pressed & IN_DOWN) r->sel = r->sel + 1 < count ? r->sel + 1 : count - 1;
	if (in->pressed & IN_UP) r->sel = r->sel > 0 ? r->sel - 1 : 0;
	if (in->pressed & IN_RIGHT) r->sel = r->sel + rows < count ? r->sel + rows : count - 1;
	if (in->pressed & IN_LEFT) r->sel = r->sel > rows ? r->sel - rows : 0;
	rowlist_keep(r, count, rows);
}

void rowlist_keep(rowlist_t *r, int count, int rows)
{
	if (r->sel >= count) r->sel = count > 0 ? count - 1 : 0;
	if (r->sel < 0) r->sel = 0;
	if (r->top > r->sel) r->top = r->sel;
	if (r->top < 0) r->top = 0;
	if (r->sel >= r->top + rows) r->top = r->sel - rows + 1;
}

int rowlist_y(int row, int top, int row_h)
{
	return ROWLIST_HEAD_H + (row - top) * row_h;
}

void rowlist_empty(const char *a, const char *b)
{
	font_draw(ROWLIST_CENTRE_X - font_width(FONT_PX_TITLE, a) / 2, ROWLIST_EMPTY_Y, THEME_FONT,
	          FONT_PX_TITLE, a);
	font_draw(ROWLIST_CENTRE_X - font_width(FONT_PX_LABEL, b) / 2, ROWLIST_EMPTY_SUB_Y,
	          THEME_SUBFONT, FONT_PX_LABEL, b);
}

void rowlist_selected(int x0, int w, int y, int row_h, uint64_t now)
{
	draw_rect(x0, y, w, row_h - 4, THEME_SELECTED);
	draw_selector(x0, y, w, row_h - 4, now);
}
