#include "ui/sidebar.h"

#include "ui/draw.h"
#include "ui/font.h"
#include "ui/theme.h"

#include <vita2d.h>

static const char *const LABELS[SEC_COUNT] = {
	"Your shelf", "Private", "Discover", "Recent", "Transfers", "Settings"
};

/* Reading sections above, utilities below. Touch uses the same geometry. */
static const int ROW_CY[SEC_COUNT] = {126, 182, 238, 294, 380, 438};
#define ROW_H 44

static float g_anim[SEC_COUNT];

const char *sidebar_label(section_t s)
{
	return ((unsigned)s < SEC_COUNT) ? LABELS[s] : "";
}

int sidebar_hit(int x, int y)
{
	if (x < 0 || x >= THEME_SIDEBAR_W)
		return -1;
	for (int i = 0; i < SEC_COUNT; i++)
		if (y >= ROW_CY[i] - ROW_H / 2 && y < ROW_CY[i] + ROW_H / 2)
			return i;
	return -1;
}

static void ring(float cx, float cy, float r, unsigned int c, unsigned int bg)
{
	vita2d_draw_fill_circle(cx, cy, r, c);
	vita2d_draw_fill_circle(cx, cy, r - 2.0f, bg);
}

/* 20x20 icons drawn from primitives, top-left (x, y). `bg` is what is behind the icon. */
static void icon(section_t s, int x, int y, unsigned int c, unsigned int bg)
{
	float cx = (float)x + 10.0f, cy = (float)y + 10.0f;
	switch (s) {
	case SEC_MYBOOKS: /* open book */
		draw_rect(x + 1, y + 3, 8, 14, c);
		draw_rect(x + 11, y + 3, 8, 14, c);
		draw_rect(x + 9, y + 5, 2, 12, bg);
		break;
	case SEC_PRIVATE: /* lock */
		ring(cx, cy - 4, 7.0f, c, bg);
		draw_rrect(x + 1, y + 8, 18, 12, 3, c);
		draw_rect(x + 9, y + 12, 2, 4, bg);
		break;
	case SEC_CATALOG: /* globe */
		ring(cx, cy, 10.0f, c, bg);
		draw_rect(x + 9, y + 1, 2, 18, c);
		draw_rect(x + 1, y + 9, 18, 2, c);
		break;
	case SEC_HISTORY: /* clock */
		ring(cx, cy, 10.0f, c, bg);
		draw_rect(x + 9, y + 4, 2, 7, c);
		draw_rect(x + 9, y + 9, 6, 2, c);
		break;
	case SEC_DOWNLOADS: /* arrow into a tray */
		draw_rect(x + 8, y + 1, 4, 9, c);
		draw_rect(x + 4, y + 8, 12, 2, c);
		draw_rect(x + 6, y + 10, 8, 2, c);
		draw_rect(x + 8, y + 12, 4, 2, c);
		draw_rect(x + 1, y + 16, 18, 3, c);
		break;
	case SEC_SETTINGS: /* three sliders */
		for (int i = 0; i < 3; i++) {
			draw_rect(x + 1, y + 3 + i * 6, 18, 2, c);
			draw_rect(x + 3 + ((i * 7 + 4) % 12), y + 1 + i * 6, 4, 6, c);
		}
		break;
	default:
		break;
	}
}

static void draw_moon(int cx, int cy)
{
	/* Thin crescent: white disc with a black disc biting out of it. */
	vita2d_draw_fill_circle((float)cx, (float)cy, 13.0f, THEME_FONT);
	vita2d_draw_fill_circle((float)cx + 6.0f, (float)cy - 3.0f, 11.5f, THEME_BLACK);
}

void sidebar_draw(section_t current, uint64_t now_ms)
{
	(void)now_ms;
	draw_rect(0, 0, THEME_SIDEBAR_W, 544, THEME_BLACK);
	draw_rect(THEME_SIDEBAR_W - 1, 0, 1, 521, THEME_PANEL);

	draw_moon(30, 40);
	font_draw(52, 28, THEME_FONT, FONT_PX_TITLE, "NoctoPage");
	font_draw(20, 66, THEME_SUBFONT, 12, "A LITTLE NIGHT READING");
	draw_rect(20, 96, 160, 1, THEME_PANEL);
	draw_rect(20, 332, 160, 1, THEME_PANEL);
	font_draw(20, 490, THEME_SUBFONT, 11, "READ AT YOUR PACE");

	for (int i = 0; i < SEC_COUNT; i++) {
		int sel = (i == (int)current);
		g_anim[i] += sel ? 0.1f : -0.1f;
		if (g_anim[i] > 1.0f) g_anim[i] = 1.0f;
		if (g_anim[i] < 0.0f) g_anim[i] = 0.0f;

		int top = ROW_CY[i] - ROW_H / 2;
		unsigned int bg = THEME_BLACK;
		if (sel) {
			bg = THEME_SELECTED;
			draw_rrect(10, top, THEME_SIDEBAR_W - 24, ROW_H, 7, bg);
			draw_rect(10, top + 10, 3, ROW_H - 20, THEME_SELECTOR);
		}
		int dx = (int)(g_anim[i] * 5.0f);
		icon((section_t)i, 14 + dx, ROW_CY[i] - 10, sel ? THEME_SELECTOR : THEME_SUBFONT, bg);
		font_draw(52 + dx, ROW_CY[i] - font_height(FONT_PX_LABEL) / 2, sel ? THEME_FONT : THEME_SUBFONT,
		          FONT_PX_LABEL, LABELS[i]);
	}
}
