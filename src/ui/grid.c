#include "ui/grid.h"

#include "core/fnv.h"
#include "ui/draw.h"
#include "ui/font.h"
#include "ui/text.h"
#include "ui/theme.h"

#include <math.h>
#include <string.h>

#define STEP_X      (GRID_W + GRID_GAP)
#define STEP_Y      (GRID_H + GRID_GAP)
#define COL0_X      211 /* NOBORU: 580 + (col - 2) * 186 + 3 */
#define DRAG_PX     10
#define GRADIENT_H  120
#define TITLE_PAD   8
#define BADGE_H     24
#define BAR_X       955
#define BAR_W       5

/* Wrapped titles, so text is not re-measured every frame. */
#define WRAP_SLOTS 48
static struct {
	uint64_t key;
	int n;
	char lines[2][TEXT_LINE_MAX];
} g_wrap[WRAP_SLOTS];

static int wrap_title(const char *title, char (**lines)[TEXT_LINE_MAX])
{
	uint64_t key = fnv64(title, strlen(title)) | 1;
	int slot = (int)(key % WRAP_SLOTS);
	if (g_wrap[slot].key != key) {
		g_wrap[slot].key = key;
		g_wrap[slot].n = text_wrap(FONT_PX_LABEL, title, GRID_W - 2 * TITLE_PAD, 2, g_wrap[slot].lines);
	}
	*lines = g_wrap[slot].lines;
	return g_wrap[slot].n;
}

static int rows_of(int count) { return (count + GRID_COLS - 1) / GRID_COLS; }

static float max_scroll(const grid_t *g)
{
	float content = (float)(GRID_TOP + rows_of(g->count) * STEP_Y);
	return content > GRID_VIEW_H ? content - GRID_VIEW_H : 0.0f;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

void grid_init(grid_t *g)
{
	memset(g, 0, sizeof *g);
	g->follow = 1;
}

void grid_set_count(grid_t *g, int count)
{
	g->count = count < 0 ? 0 : count;
	if (g->sel >= g->count) g->sel = g->count > 0 ? g->count - 1 : 0;
	g->scroll = clampf(g->scroll, 0.0f, max_scroll(g));
}

/* Item under a screen point, or -1. */
static int hit(const grid_t *g, int x, int y)
{
	int cx = x - COL0_X, cy = y - GRID_VIEW_Y - GRID_TOP + (int)g->scroll;
	if (cx < 0 || cy < 0 || cx % STEP_X >= GRID_W || cy % STEP_Y >= GRID_H) return -1;
	int col = cx / STEP_X, row = cy / STEP_Y;
	if (col >= GRID_COLS) return -1;
	int i = row * GRID_COLS + col;
	return i < g->count ? i : -1;
}

static int update_touch(grid_t *g, const input_t *in)
{
	int down = in->touch_count > 0;
	int y = down ? in->touch[0].y : 0;
	if (in->touch_began && in->began_x > THEME_SIDEBAR_W && in->began_y < GRID_VIEW_Y + GRID_VIEW_H) {
		g->touching = 1;
		g->dragging = 0;
		g->touch_x0 = in->began_x;
		g->touch_y0 = g->touch_last_y = in->began_y;
		g->scroll0 = g->scroll;
		g->vel = 0.0f;
	}
	if (!g->touching) return GRID_NONE;
	if (down) {
		if (!g->dragging && (y - g->touch_y0 > DRAG_PX || g->touch_y0 - y > DRAG_PX)) {
			g->dragging = 1;
			g->follow = 0;
		}
		if (g->dragging) {
			g->vel = (float)(g->touch_last_y - y);
			g->scroll = clampf(g->scroll0 - (float)(y - g->touch_y0), 0.0f, max_scroll(g));
		}
		g->touch_last_y = y;
		return GRID_NONE;
	}
	g->touching = 0;
	if (g->dragging) return GRID_NONE;
	int i = hit(g, g->touch_x0, g->touch_y0);
	if (i < 0) return GRID_NONE;
	g->sel = i;
	g->follow = 1;
	return GRID_OPEN;
}

int grid_update(grid_t *g, const input_t *in)
{
	int ret = update_touch(g, in);
	if (!g->touching && fabsf(g->vel) >= 1.0f) { /* kinetic scroll after release */
		g->scroll = clampf(g->scroll + g->vel, 0.0f, max_scroll(g));
		g->vel /= 1.12f;
	} else if (!g->touching) {
		g->vel = 0.0f;
	}
	if (g->count == 0) return GRID_NONE;

	int old = g->sel, col = g->sel % GRID_COLS;
	if ((in->pressed & IN_LEFT) && col > 0) g->sel--;
	if ((in->pressed & IN_RIGHT) && col < GRID_COLS - 1 && g->sel + 1 < g->count) g->sel++;
	if ((in->pressed & IN_UP) && g->sel >= GRID_COLS) g->sel -= GRID_COLS;
	if (in->pressed & IN_DOWN) {
		if (g->sel + GRID_COLS < g->count) g->sel += GRID_COLS;
		else if (g->sel / GRID_COLS < rows_of(g->count) - 1) g->sel = g->count - 1;
	}
	if (g->sel != old) {
		g->follow = 1;
		g->vel = 0.0f;
		if (ret == GRID_NONE) ret = GRID_MOVED;
	}
	if (in->pressed & IN_CONFIRM) ret = GRID_OPEN;

	if (g->follow && !g->touching) { /* ease the selected row to the middle (NOBORU /8) */
		float target = (float)((g->sel / GRID_COLS) * STEP_Y + GRID_H / 2 - 232);
		target = clampf(target, 0.0f, max_scroll(g));
		g->scroll += (target - g->scroll) / 8.0f;
		if (fabsf(target - g->scroll) < 0.5f) g->scroll = target;
	}
	return ret;
}

void grid_visible(const grid_t *g, int *first, int *last)
{
	int r0 = (int)(g->scroll - GRID_TOP) / STEP_Y;
	int r1 = (int)(g->scroll + GRID_VIEW_H) / STEP_Y;
	if (r0 < 0) r0 = 0;
	*first = r0 * GRID_COLS;
	*last = (r1 + 1) * GRID_COLS - 1;
	if (*last >= g->count) *last = g->count - 1;
}

static void draw_cover(vita2d_texture *t, int x, int y)
{
	draw_rect(x, y, GRID_W, GRID_H, THEME_BLACK);
	if (!t) {
		draw_rect(x, y, GRID_W, GRID_H, THEME_COVER);
		return;
	}
	float tw = (float)vita2d_texture_get_width(t), th = (float)vita2d_texture_get_height(t);
	float s = fmaxf(GRID_W / tw, GRID_H / th);
	float sw = GRID_W / s, sh = GRID_H / s;
	vita2d_draw_texture_part_scale(t, (float)x, (float)y, (tw - sw) / 2.0f, (th - sh) / 2.0f, sw, sh, s, s);
}

static void draw_check(int x, int y)
{
	vita2d_draw_fill_circle((float)x, (float)y, 11.0f, THEME_SELECTOR);
	for (int d = 0; d < 2; d++) {
		vita2d_draw_line((float)(x - 5), (float)(y + d), (float)(x - 1), (float)(y + 4 + d), THEME_FONT);
		vita2d_draw_line((float)(x - 1), (float)(y + 4 + d), (float)(x + 6), (float)(y - 4 + d), THEME_FONT);
	}
}

static void draw_item(const grid_item_t *it, int x, int y)
{
	draw_cover(it->cover, x, y);
	draw_gradient_v(x, y + GRID_H - GRADIENT_H, GRID_W, GRADIENT_H, RGBA8(0, 0, 0, 0), RGBA8(0, 0, 0, 230));
	if (it->title && *it->title) {
		char (*lines)[TEXT_LINE_MAX];
		int n = wrap_title(it->title, &lines);
		for (int i = 0; i < n; i++) {
			int ly = y + GRID_H - (n == 2 && i == 0 ? 47 : 27);
			font_draw(x + TITLE_PAD, ly, THEME_FONT, FONT_PX_LABEL, lines[i]);
		}
	}
	if (it->badge && *it->badge) {
		int w = font_width(FONT_PX_LABEL, it->badge) + 11;
		draw_rect(x, y, w, BADGE_H, THEME_BADGE);
		font_draw(x + 5, y + (BADGE_H - font_height(FONT_PX_LABEL)) / 2 + 1, THEME_FONT, FONT_PX_LABEL, it->badge);
	}
	if (it->progress >= 0.0f) {
		float p = clampf(it->progress, 0.0f, 1.0f);
		draw_rect(x, y + GRID_H - 4, GRID_W, 4, RGBA8(40, 40, 40, 255));
		draw_rect(x, y + GRID_H - 4, (int)(GRID_W * p + 0.5f), 4, THEME_SELECTOR);
	}
	if (it->finished)
		draw_check(x + GRID_W - 16, y + 16);
}

void grid_draw(grid_t *g, grid_item_fn fn, void *user, uint64_t now_ms)
{
	if (g->count == 0) return;
	int first, last;
	grid_visible(g, &first, &last);
	for (int i = first; i <= last; i++) {
		grid_item_t it = {0};
		it.progress = -1.0f;
		fn(user, i, &it);
		int x = COL0_X + (i % GRID_COLS) * STEP_X;
		int y = GRID_VIEW_Y + GRID_TOP + (i / GRID_COLS) * STEP_Y - (int)g->scroll;
		draw_item(&it, x, y);
		if (i == g->sel)
			draw_selector(x, y, GRID_W, GRID_H, now_ms);
	}
	float ms = max_scroll(g);
	if (ms > 0.0f) {
		float content = ms + GRID_VIEW_H;
		int th = (int)(GRID_VIEW_H * GRID_VIEW_H / content);
		if (th < 16) th = 16;
		int ty = GRID_VIEW_Y + (int)((GRID_VIEW_H - th) * (g->scroll / ms));
		draw_rect(BAR_X, GRID_VIEW_Y, BAR_W, GRID_VIEW_H, theme_bg());
		draw_rect(BAR_X, ty, BAR_W, th, THEME_FONT);
	}
}
