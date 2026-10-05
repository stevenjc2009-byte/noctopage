/* History: books opened before, the most recent first, with where the reader stopped.
 * Confirm opens a book where it was left; Square takes it off the list (the saved position
 * and bookmarks stay). Books whose file is gone are listed greyed out and cannot be opened. */
#include "screens/screens.h"

#include "lib/covers.h"
#include "lib/library.h"
#include "ui/draw.h"
#include "ui/font.h"
#include "ui/text.h"
#include "ui/theme.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define HIST_MAX 100
#define X0 (205 + 16)
#define W (755 - 32)
#define HEAD_H 56
#define ROW_H 72
#define ROWS ((521 - HEAD_H) / ROW_H)
#define THUMB_W 40
#define THUMB_H 60

static db_book_t g_books[HIST_MAX];
static int g_n, g_sel, g_top;
static unsigned g_seen_gen;
static uint64_t g_now;

static void reload(void)
{
	uint64_t keep = g_sel < g_n ? g_books[g_sel].id : 0;
	int n = db_history_list(g_books, HIST_MAX);
	g_n = n < 0 ? 0 : n;
	for (int i = 0; i < g_n; i++)
		if (g_books[i].id == keep) g_sel = i;
	if (g_sel >= g_n) g_sel = g_n > 0 ? g_n - 1 : 0;
	if (g_top > g_sel) g_top = g_sel;
	g_seen_gen = library_generation();
}

static void enter(void) { reload(); }
static void leave(void) {}

static void update(const input_t *in)
{
	g_now = in->now_ms;
	if (library_generation() != g_seen_gen) reload();
	if (g_n == 0) return;
	if (in->pressed & IN_DOWN) g_sel = g_sel + 1 < g_n ? g_sel + 1 : g_sel;
	if (in->pressed & IN_UP) g_sel = g_sel > 0 ? g_sel - 1 : 0;
	if (in->pressed & IN_RIGHT) g_sel = g_sel + ROWS < g_n ? g_sel + ROWS : g_n - 1;
	if (in->pressed & IN_LEFT) g_sel = g_sel > ROWS ? g_sel - ROWS : 0;
	if (g_sel < g_top) g_top = g_sel;
	if (g_sel >= g_top + ROWS) g_top = g_sel - ROWS + 1;
	const db_book_t *b = &g_books[g_sel];
	if (in->pressed & IN_SQUARE) {
		db_history_remove(b->id);
		reload();
	} else if ((in->pressed & IN_CONFIRM) && !b->missing) {
		app_open_book(b);
	}
}

/* "Today", "Yesterday", "3 days ago", else the date. */
static void when(int64_t t, char *out, size_t cap)
{
	time_t now = time(NULL), tt = (time_t)t;
	struct tm a, b;
	localtime_r(&now, &a);
	localtime_r(&tt, &b);
	struct tm da = a, db = b; /* both at noon, so a DST change cannot shift the day count */
	da.tm_hour = db.tm_hour = 12;
	da.tm_min = db.tm_min = da.tm_sec = db.tm_sec = 0;
	long days = (long)((mktime(&da) - mktime(&db)) / 86400);
	if (days <= 0)
		snprintf(out, cap, "Today");
	else if (days == 1)
		snprintf(out, cap, "Yesterday");
	else if (days < 7)
		snprintf(out, cap, "%ld days ago", days);
	else
		strftime(out, cap, "%d %b %Y", &b);
}

static void detail(const db_book_t *b, char *out, size_t cap)
{
	char w[32];
	when(b->last_read, w, sizeof w);
	if (b->missing)
		snprintf(out, cap, "File not found  \xC2\xB7  %s", w);
	else if (b->finished)
		snprintf(out, cap, "Finished  \xC2\xB7  %s", w);
	else if (b->page_count > 0 && b->kind != DB_KIND_REFLOW)
		snprintf(out, cap, "Page %d of %d  \xC2\xB7  %s", b->page + 1, b->page_count, w);
	else
		snprintf(out, cap, "%d%%  \xC2\xB7  %s", (int)(b->progress * 100.0 + 0.5), w);
}

static void thumb(const db_book_t *b, int x, int y)
{
	vita2d_texture *t = b->missing ? NULL : covers_get(b->id, b->path, b->kind);
	if (!t) {
		draw_rect(x, y, THUMB_W, THUMB_H, b->missing ? THEME_PANEL : THEME_COVER);
		return;
	}
	float tw = (float)vita2d_texture_get_width(t), th = (float)vita2d_texture_get_height(t);
	float s = THUMB_W / tw < THUMB_H / th ? THUMB_W / tw : THUMB_H / th;
	vita2d_draw_texture_scale(t, (float)x + (THUMB_W - tw * s) / 2, (float)y + (THUMB_H - th * s) / 2,
	                          s, s);
}

static void draw(void)
{
	font_draw(X0, 18, THEME_FONT, FONT_PX_TITLE, "History");
	if (g_n == 0) {
		const char *a = "Nothing read yet";
		const char *b = "Books you open appear here, the most recent first";
		font_draw(582 - font_width(FONT_PX_TITLE, a) / 2, 230, THEME_FONT, FONT_PX_TITLE, a);
		font_draw(582 - font_width(FONT_PX_LABEL, b) / 2, 266, THEME_SUBFONT, FONT_PX_LABEL, b);
		return;
	}
	for (int r = g_top; r < g_n && r < g_top + ROWS; r++) {
		const db_book_t *b = &g_books[r];
		int y = HEAD_H + (r - g_top) * ROW_H;
		if (r == g_sel) draw_rect(X0 - 8, y, W + 16, ROW_H - 4, THEME_SELECTED);
		thumb(b, X0 + 8, y + 4);
		int tx = X0 + 8 + THUMB_W + 14, tw = X0 + W - 16 - tx;
		char line[TEXT_LINE_MAX], sub[TEXT_LINE_MAX], d[96];
		text_ellipsize(FONT_PX_TITLE, b->title, tw, line);
		font_draw(tx, y + 10, b->missing ? THEME_SUBFONT : THEME_FONT, FONT_PX_TITLE, line);
		detail(b, d, sizeof d);
		snprintf(sub, sizeof sub, "%.90s%s%s", b->author, b->author[0] ? "  \xC2\xB7  " : "", d);
		text_ellipsize(FONT_PX_LABEL, sub, tw, line);
		font_draw(tx, y + 38, THEME_SUBFONT, FONT_PX_LABEL, line);
		if (!b->missing && !b->finished && b->progress > 0.0) {
			int bw = 120, fill = (int)(bw * b->progress);
			draw_rect(X0 + W - bw - 8, y + 60, bw, 3, THEME_PANEL);
			draw_rect(X0 + W - bw - 8, y + 60, fill > bw ? bw : fill, 3, THEME_SELECTOR);
		}
		if (r == g_sel) draw_selector(X0 - 8, y, W + 16, ROW_H - 4, g_now);
	}
}

static int hints(hint_t *out, int max)
{
	hint_t h[] = {
		{GLYPH_LR, "Section"},
		{GLYPH_DPAD, "Choose"},
		{GLYPH_SQUARE, "Remove"},
		{GLYPH_CONFIRM, "Open"},
	};
	int n = g_n == 0 ? 1 : (g_sel < g_n && g_books[g_sel].missing ? 3 : 4);
	return screen_copy_hints(h, n, out, max);
}

const screen_t screen_history = {"History", enter, leave, update, draw, hints};
