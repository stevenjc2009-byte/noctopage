#include "ui/menu.h"

#include "ui/draw.h"
#include "ui/font.h"
#include "ui/theme.h"
#include "ui/text.h"

#define MENU_W 360
#define ROW_H 40
#define HEAD_H 48
#define VISIBLE_ROWS 10
#define FOOT_H 24

static int g_open, g_n, g_sel, g_top;
static int g_touching, g_dragged, g_touch_y, g_touch_last, g_touch_x;
static const char *g_title;
static const char *g_labels[MENU_MAX];

static int box_x(void) { return (960 - MENU_W) / 2; }
static int visible(void) { return g_n > VISIBLE_ROWS ? VISIBLE_ROWS : g_n; }
static int box_h(void) { return HEAD_H + visible() * ROW_H + (g_n > VISIBLE_ROWS ? FOOT_H : 8); }
static int box_y(void) { return (521 - box_h()) / 2; }

void menu_open(const char *title, const char *const *labels, int n)
{
	if (n > MENU_MAX) n = MENU_MAX;
	g_title = title ? title : "";
	for (int i = 0; i < n; i++) g_labels[i] = labels[i];
	g_n = n;
	g_sel = g_top = 0;
	g_touching = g_dragged = 0;
	g_open = n > 0;
}

static void follow(void)
{
	if (g_sel < g_top) g_top = g_sel;
	if (g_sel >= g_top + visible()) g_top = g_sel - visible() + 1;
}

void menu_select(int sel)
{
	if (sel >= 0 && sel < g_n) { g_sel = sel; follow(); }
}

int menu_is_open(void) { return g_open; }

int menu_update(const input_t *in)
{
	if (!g_open) return -1;
	if (in->pressed & IN_UP) g_sel = (g_sel + g_n - 1) % g_n;
	if (in->pressed & IN_DOWN) g_sel = (g_sel + 1) % g_n;
	if (in->pressed & IN_BACK) {
		g_open = 0;
		return -1;
	}
	if (in->pressed & IN_CONFIRM) {
		g_open = 0;
		return g_sel;
	}
	follow();
	if (in->touch_began) {
		int x = in->began_x - box_x();
		if (x < 0 || x >= MENU_W || in->began_y < box_y() || in->began_y >= box_y() + box_h()) {
			g_open = 0;
			return -1;
		}
		g_touching = 1; g_dragged = 0;
		g_touch_y = g_touch_last = in->began_y;
		g_touch_x = in->began_x;
		/* A tap on the footer pages forward without opening an item. */
		if (g_n > VISIBLE_ROWS && in->began_y >= box_y() + HEAD_H + visible() * ROW_H) {
			g_top = g_top < g_n - visible() ? g_top + 1 : 0;
			g_sel = g_top; g_touching = 0;
		}
	}
	if (g_touching && in->touch_count > 0) {
		int y = in->touch[0].y;
		if (y - g_touch_y > 12 || g_touch_y - y > 12 ||
		    in->touch[0].x - g_touch_x > 12 || g_touch_x - in->touch[0].x > 12) g_dragged = 1;
		if (g_dragged) {
			int delta = (g_touch_last - y) / ROW_H;
			if (delta) {
				g_top += delta;
				if (g_top < 0) g_top = 0;
				if (g_top > g_n - visible()) g_top = g_n - visible();
				g_sel = g_top; g_touch_last = y;
			}
		}
	}
	if (g_touching && in->touch_count == 0) {
		g_touching = 0;
		int y = g_touch_y - box_y() - HEAD_H;
		if (!g_dragged && y >= 0 && y / ROW_H < visible()) {
			g_open = 0;
			return g_top + y / ROW_H;
		}
	}
	return -1;
}

void menu_draw(uint64_t now_ms)
{
	if (!g_open) return;
	draw_rect(0, 0, 960, 521, RGBA8(0, 0, 0, 150));
	int x = box_x(), y = box_y();
	draw_rrect(x, y, MENU_W, box_h(), 6, THEME_PANEL);
	char title[TEXT_LINE_MAX];
	text_ellipsize(FONT_PX_TITLE, g_title, MENU_W - 40, title);
	font_draw(x + 20, y + 14, THEME_FONT, FONT_PX_TITLE, title);
	for (int i = g_top; i < g_top + visible(); i++) {
		int ry = y + HEAD_H + (i - g_top) * ROW_H;
		if (i == g_sel) {
			draw_rect(x + 8, ry, MENU_W - 16, ROW_H, THEME_SELECTED);
			draw_selector(x + 8, ry, MENU_W - 16, ROW_H, now_ms);
		}
		font_draw(x + 24, ry + (ROW_H - font_height(FONT_PX_LABEL)) / 2, THEME_FONT, FONT_PX_LABEL,
		          g_labels[i]);
	}
	if (g_n > VISIBLE_ROWS)
		font_draw(x + 24, y + HEAD_H + visible() * ROW_H + 3, THEME_SUBFONT, FONT_PX_LABEL, "Swipe or tap here for more");
}

int menu_hints(hint_t *out, int max)
{
	static const hint_t H[] = {
		{GLYPH_DPAD, "Choose"},
		{GLYPH_CONFIRM, "Select"},
		{GLYPH_BACK, "Close"},
	};
	int n = (int)(sizeof H / sizeof H[0]);
	if (n > max) n = max;
	for (int i = 0; i < n; i++) out[i] = H[i];
	return n;
}
