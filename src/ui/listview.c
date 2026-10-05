#include "ui/listview.h"

#include "ui/draw.h"
#include "ui/font.h"
#include "ui/text.h"
#include "ui/theme.h"

#include <stdio.h>

#define BOX_W 680
#define BOX_Y 16
#define BOX_H (521 - 2 * BOX_Y)
#define HEAD_H 52
#define ROW_H 36
#define ROWS ((BOX_H - HEAD_H - 8) / ROW_H)
#define INDENT 22

static int g_open, g_n, g_sel, g_top, g_can_delete;
static const lv_item_t *g_items;
static const char *g_title, *g_empty;

static int box_x(void) { return (960 - BOX_W) / 2; }

static void keep_visible(void)
{
	if (g_sel >= g_n) g_sel = g_n - 1;
	if (g_sel < 0) g_sel = 0;
	if (g_sel < g_top) g_top = g_sel;
	if (g_sel >= g_top + ROWS) g_top = g_sel - ROWS + 1;
	if (g_top > g_n - ROWS) g_top = g_n - ROWS;
	if (g_top < 0) g_top = 0;
}

void listview_open(const char *title, const lv_item_t *items, int n, int sel, const char *empty,
                   int can_delete)
{
	g_title = title ? title : "";
	g_empty = empty ? empty : "";
	g_items = items;
	g_n = n > 0 ? n : 0;
	g_sel = sel;
	g_top = sel - ROWS / 2;
	g_can_delete = can_delete;
	keep_visible();
	g_open = 1;
}

void listview_set_items(const lv_item_t *items, int n)
{
	g_items = items;
	g_n = n > 0 ? n : 0;
	keep_visible();
}

int listview_is_open(void) { return g_open; }

lv_action_t listview_update(const input_t *in, int *index)
{
	if (!g_open) return LV_NONE;
	if (in->pressed & IN_BACK) {
		g_open = 0;
		return LV_NONE;
	}
	if (g_n > 0) {
		if (in->pressed & IN_UP) g_sel = g_sel > 0 ? g_sel - 1 : g_n - 1;
		if (in->pressed & IN_DOWN) g_sel = g_sel < g_n - 1 ? g_sel + 1 : 0;
		if (in->pressed & (IN_LEFT | IN_L)) g_sel -= ROWS;
		if (in->pressed & (IN_RIGHT | IN_R)) g_sel += ROWS;
		keep_visible();
		if (in->pressed & IN_CONFIRM) {
			*index = g_sel;
			g_open = 0;
			return LV_PICK;
		}
		if ((in->pressed & IN_TRIANGLE) && g_can_delete) {
			*index = g_sel;
			return LV_DELETE;
		}
	}
	if (in->touch_began) {
		int x = in->began_x - box_x(), y = in->began_y - BOX_Y;
		if (x < 0 || x >= BOX_W || y < 0 || y >= BOX_H) {
			g_open = 0;
			return LV_NONE;
		}
		int row = (y - HEAD_H) / ROW_H;
		if (y >= HEAD_H && row < ROWS && g_top + row < g_n) {
			*index = g_sel = g_top + row;
			g_open = 0;
			return LV_PICK;
		}
	}
	return LV_NONE;
}

void listview_draw(uint64_t now_ms)
{
	if (!g_open) return;
	char line[TEXT_LINE_MAX];
	draw_rect(0, 0, 960, 521, RGBA8(0, 0, 0, 150));
	int x = box_x();
	draw_rrect(x, BOX_Y, BOX_W, BOX_H, 6, THEME_PANEL);
	font_draw(x + 20, BOX_Y + 14, THEME_FONT, FONT_PX_TITLE, g_title);
	if (g_n == 0) {
		int w = font_width(FONT_PX_LABEL, g_empty);
		font_draw(x + (BOX_W - w) / 2, BOX_Y + BOX_H / 2 - 10, THEME_SUBFONT, FONT_PX_LABEL, g_empty);
		return;
	}
	int ty = (ROW_H - font_height(FONT_PX_LABEL)) / 2;
	for (int r = 0; r < ROWS && g_top + r < g_n; r++) {
		const lv_item_t *it = &g_items[g_top + r];
		int ry = BOX_Y + HEAD_H + r * ROW_H;
		if (g_top + r == g_sel) {
			draw_rect(x + 8, ry, BOX_W - 16, ROW_H, THEME_SELECTED);
			draw_selector(x + 8, ry, BOX_W - 16, ROW_H, now_ms);
		}
		int rw = it->right ? font_width(FONT_PX_LABEL, it->right) : 0;
		if (it->right)
			font_draw(x + BOX_W - 24 - rw, ry + ty, THEME_SUBFONT, FONT_PX_LABEL, it->right);
		int depth = it->depth > 6 ? 6 : it->depth < 0 ? 0 : it->depth;
		int lx = x + 24 + depth * INDENT;
		text_ellipsize(FONT_PX_LABEL, it->label ? it->label : "", x + BOX_W - 40 - rw - lx, line);
		font_draw(lx, ry + ty, THEME_FONT, FONT_PX_LABEL, line);
	}
	if (g_n > ROWS) { /* scroll position */
		int track = BOX_H - HEAD_H - 16, th = track * ROWS / g_n;
		int tyb = BOX_Y + HEAD_H + (track - th) * g_top / (g_n - ROWS);
		draw_rect(x + BOX_W - 6, tyb, 3, th, THEME_SUBFONT);
	}
}

int listview_hints(hint_t *out, int max)
{
	hint_t h[] = {
		{GLYPH_DPAD, "Choose"},
		{GLYPH_CONFIRM, "Go"},
		{GLYPH_TRIANGLE, "Delete"},
		{GLYPH_BACK, "Close"},
	};
	int n = 0;
	for (int i = 0; i < 4 && n < max; i++) {
		if (i == 2 && (!g_can_delete || g_n == 0)) continue;
		out[n++] = h[i];
	}
	return n;
}
