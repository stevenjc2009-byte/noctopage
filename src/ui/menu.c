#include "ui/menu.h"

#include "ui/draw.h"
#include "ui/font.h"
#include "ui/theme.h"

#define MENU_W 360
#define ROW_H 40
#define HEAD_H 48

static int g_open, g_n, g_sel;
static const char *g_title;
static const char *g_labels[MENU_MAX];

static int box_x(void) { return (960 - MENU_W) / 2; }
static int box_h(void) { return HEAD_H + g_n * ROW_H + 8; }
static int box_y(void) { return (521 - box_h()) / 2; }

void menu_open(const char *title, const char *const *labels, int n)
{
	if (n > MENU_MAX) n = MENU_MAX;
	g_title = title ? title : "";
	for (int i = 0; i < n; i++) g_labels[i] = labels[i];
	g_n = n;
	g_sel = 0;
	g_open = n > 0;
}

void menu_select(int sel)
{
	if (sel >= 0 && sel < g_n) g_sel = sel;
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
	if (in->touch_began) {
		int x = in->began_x - box_x(), y = in->began_y - box_y() - HEAD_H;
		if (x < 0 || x >= MENU_W || in->began_y < box_y() || in->began_y >= box_y() + box_h()) {
			g_open = 0; /* tap outside closes */
			return -1;
		}
		if (y >= 0 && y / ROW_H < g_n) {
			g_open = 0;
			return y / ROW_H;
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
	font_draw(x + 20, y + 14, THEME_FONT, FONT_PX_TITLE, g_title);
	for (int i = 0; i < g_n; i++) {
		int ry = y + HEAD_H + i * ROW_H;
		if (i == g_sel) {
			draw_rect(x + 8, ry, MENU_W - 16, ROW_H, THEME_SELECTED);
			draw_selector(x + 8, ry, MENU_W - 16, ROW_H, now_ms);
		}
		font_draw(x + 24, ry + (ROW_H - font_height(FONT_PX_LABEL)) / 2, THEME_FONT, FONT_PX_LABEL,
		          g_labels[i]);
	}
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
