/* Settings: app-wide defaults. Comic options here are the defaults for books that have no
 * setting of their own (the reader's menu saves per book); book text options are shared by
 * every book. Left/Right or confirm change the value on the selected row; every change is
 * saved at once (core/prefs). */
#include "screens/screens.h"

#include "core/prefs.h"
#include "lib/library.h"
#include "ui/draw.h"
#include "ui/font.h"
#include "ui/text.h"
#include "ui/theme.h"

#include <stdio.h>
#include <string.h>

#define X0 (205 + 16)
#define W (755 - 32)
#define HEAD_H 56
#define ROW_H 52
#define ROWS ((521 - HEAD_H) / ROW_H)

typedef struct {
	const char *label;
	const char *section, *key; /* NULL key = an action or a plain line */
	int def;
	int n;                      /* number of choices */
	const char *const *names;   /* choice names; NULL = values[] shown as "%d" */
	const int *values;          /* stored values; NULL = 0..n-1 */
} row_t;

static const char *const BG[] = {"OLED black", "Dark grey (LCD and PCH-1000 screens)"};
static const char *const MODE[] = {"Left to right", "Right to left (manga)", "Vertical strip"};
static const char *const FIT[] = {"Fit width", "Fit height", "Fit smart", "Original"};
static const char *const SPREADS[] = {"Off", "On", "On, cover alone"};
static const char *const ONOFF[] = {"Off", "On"};
static const char *const PAGE[] = {"White", "Sepia", "Black"};
static const int FONT_PX[] = {16, 18, 20, 22, 24, 28, 32, 36, 40};

enum { R_BG, R_MODE, R_FIT, R_SPREADS, R_CROP, R_FONT, R_PAGE, R_RESCAN, R_ABOUT, R_COUNT };

static const row_t ROW[R_COUNT] = {
	[R_BG] = {"Background", "ui", "dark_grey", 0, 2, BG, NULL},
	[R_MODE] = {"Comics: reading direction", "reader", "mode", 0, 3, MODE, NULL},
	[R_FIT] = {"Comics: page fit", "reader", "fit", 2, 4, FIT, NULL},
	[R_SPREADS] = {"Comics: double-page spreads", "reader", "spreads", 0, 3, SPREADS, NULL},
	[R_CROP] = {"Comics: crop white borders", "reader", "crop", 0, 2, ONOFF, NULL},
	[R_FONT] = {"Books: text size", "book", "font_px", 24, 9, NULL, FONT_PX},
	[R_PAGE] = {"Books: page colour", "book", "theme", 0, 3, PAGE, NULL},
	[R_RESCAN] = {"Look for new books now", NULL, NULL, 0, 0, NULL, NULL},
	[R_ABOUT] = {"Noctopage 1.00", NULL, NULL, 0, 0, NULL, NULL},
};

static int g_sel, g_top;
static uint64_t g_now, g_rescan_at;

static void enter(void) {}
static void leave(void) {}

/* Index of the stored value among the row's choices (the default's when unknown). */
static int choice(const row_t *r)
{
	long v = prefs_get_int(r->section, r->key, r->def);
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < r->n; i++)
			if ((r->values ? r->values[i] : i) == v) return i;
		v = r->def;
	}
	return 0;
}

static void step(int row, int dir)
{
	const row_t *r = &ROW[row];
	if (!r->key) return;
	int i = (choice(r) + dir + r->n) % r->n;
	int v = r->values ? r->values[i] : i;
	prefs_set_int(r->section, r->key, v);
	if (row == R_BG) theme_set_dark_grey(v);
}

static void update(const input_t *in)
{
	g_now = in->now_ms;
	if (in->pressed & IN_DOWN) g_sel = (g_sel + 1) % R_COUNT;
	if (in->pressed & IN_UP) g_sel = (g_sel + R_COUNT - 1) % R_COUNT;
	if (g_sel < g_top) g_top = g_sel;
	if (g_sel >= g_top + ROWS) g_top = g_sel - ROWS + 1;
	if (in->pressed & (IN_RIGHT | IN_CONFIRM)) step(g_sel, 1);
	if (in->pressed & IN_LEFT) step(g_sel, -1);
	if (g_sel == R_RESCAN && (in->pressed & IN_CONFIRM)) {
		library_scan_start();
		g_rescan_at = g_now;
	}
}

static void value_text(int row, char *out, size_t cap)
{
	const row_t *r = &ROW[row];
	out[0] = '\0';
	if (r->key) {
		int i = choice(r);
		if (r->names)
			snprintf(out, cap, "%s", r->names[i]);
		else
			snprintf(out, cap, "%d", r->values[i]);
	} else if (row == R_RESCAN) {
		if (library_scan_running())
			snprintf(out, cap, "Looking\xE2\x80\xA6");
		else if (g_rescan_at)
			snprintf(out, cap, "Done");
	} else if (row == R_ABOUT) {
		snprintf(out, cap, "Free software, AGPL-3.0");
	}
}

static void draw(void)
{
	font_draw(X0, 18, THEME_FONT, FONT_PX_TITLE, "Settings");
	for (int r = g_top; r < R_COUNT && r < g_top + ROWS; r++) {
		int y = HEAD_H + (r - g_top) * ROW_H;
		if (r == g_sel) draw_rect(X0 - 8, y, W + 16, ROW_H - 4, THEME_SELECTED);
		char v[96], line[TEXT_LINE_MAX];
		value_text(r, v, sizeof v);
		int vw = font_width(FONT_PX_LABEL, v);
		int ty = y + (ROW_H - 4 - font_height(FONT_PX_TITLE)) / 2;
		text_ellipsize(FONT_PX_TITLE, ROW[r].label, W - vw - 40, line);
		font_draw(X0 + 8, ty, r == R_ABOUT ? THEME_SUBFONT : THEME_FONT, FONT_PX_TITLE, line);
		int vy = y + (ROW_H - 4 - font_height(FONT_PX_LABEL)) / 2;
		font_draw(X0 + W - 8 - vw, vy, ROW[r].key ? THEME_SELECTOR : THEME_SUBFONT, FONT_PX_LABEL, v);
		if (r == g_sel) draw_selector(X0 - 8, y, W + 16, ROW_H - 4, g_now);
	}
}

static int hints(hint_t *out, int max)
{
	if (ROW[g_sel].key) {
		hint_t h[] = {{GLYPH_LR, "Section"}, {GLYPH_DPAD, "Choose / change"}};
		return screen_copy_hints(h, 2, out, max);
	}
	hint_t h[] = {{GLYPH_LR, "Section"}, {GLYPH_DPAD, "Choose"}, {GLYPH_CONFIRM, "Start"}};
	return screen_copy_hints(h, g_sel == R_RESCAN ? 3 : 2, out, max);
}

const screen_t screen_settings = {"Settings", enter, leave, update, draw, hints};
