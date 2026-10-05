/* Downloads: the download queue (net/dlq) with progress. Finished books appear in My Books
 * after the library rescan that each completed download starts. */
#include "screens/screens.h"

#include "net/dlq.h"
#include "ui/draw.h"
#include "ui/font.h"
#include "ui/menu.h"
#include "ui/text.h"
#include "ui/theme.h"

#include <stdio.h>
#include <string.h>

#define X0 (205 + 16)
#define W (755 - 32)
#define HEAD_H 56
#define ROW_H 72
#define ROWS ((521 - HEAD_H) / ROW_H)

static dlq_item_t g_items[DLQ_MAX];
static int g_n, g_sel, g_top;
static uint64_t g_now;

enum { A_CANCEL, A_RETRY, A_REMOVE };
static int g_actions[3], g_nactions;
static const char *g_labels[3];

static void refresh(void)
{
	int64_t keep = g_sel < g_n ? g_items[g_sel].id : 0;
	g_n = dlq_list(g_items, DLQ_MAX);
	for (int i = 0; i < g_n; i++)
		if (g_items[i].id == keep) g_sel = i;
	if (g_sel >= g_n) g_sel = g_n > 0 ? g_n - 1 : 0;
}

static void enter(void) { refresh(); }
static void leave(void) {}

static int active(const dlq_item_t *d) { return d->state == DB_DL_QUEUED || d->state == DB_DL_ACTIVE; }

static void open_menu(void)
{
	const dlq_item_t *d = &g_items[g_sel];
	static const char *const NAMES[] = {"Cancel download", "Try again", "Remove from list"};
	g_nactions = 0;
	if (active(d)) g_actions[g_nactions++] = A_CANCEL;
	if (d->state == DB_DL_FAILED || d->state == DB_DL_CANCELLED) g_actions[g_nactions++] = A_RETRY;
	if (!active(d)) g_actions[g_nactions++] = A_REMOVE;
	for (int i = 0; i < g_nactions; i++) g_labels[i] = NAMES[g_actions[i]];
	menu_open(d->title, g_labels, g_nactions);
}

static void act(int a, int64_t id)
{
	if (a == A_CANCEL) dlq_cancel(id);
	if (a == A_RETRY) dlq_retry(id);
	if (a == A_REMOVE) dlq_remove(id);
	refresh();
}

static void update(const input_t *in)
{
	g_now = in->now_ms;
	refresh();
	if (menu_is_open()) {
		int k = menu_update(in);
		if (k >= 0 && k < g_nactions && g_sel < g_n) act(g_actions[k], g_items[g_sel].id);
		return;
	}
	if (g_n == 0) return;
	if (in->pressed & IN_DOWN) g_sel = g_sel + 1 < g_n ? g_sel + 1 : g_sel;
	if (in->pressed & IN_UP) g_sel = g_sel > 0 ? g_sel - 1 : 0;
	if (g_sel < g_top) g_top = g_sel;
	if (g_sel >= g_top + ROWS) g_top = g_sel - ROWS + 1;
	const dlq_item_t *d = &g_items[g_sel];
	if (in->pressed & IN_TRIANGLE) open_menu();
	else if ((in->pressed & IN_CONFIRM) && (d->state == DB_DL_FAILED || d->state == DB_DL_CANCELLED))
		act(A_RETRY, d->id);
}

static void size_text(int64_t n, char *out, size_t cap)
{
	if (n >= 1024 * 1024)
		snprintf(out, cap, "%.1f MB", (double)n / (1024.0 * 1024.0));
	else
		snprintf(out, cap, "%lld KB", (long long)(n / 1024));
}

static void status(const dlq_item_t *d, char *out, size_t cap)
{
	char a[24], b[24];
	size_text(d->bytes, a, sizeof a);
	size_text(d->total, b, sizeof b);
	switch (d->state) {
	case DB_DL_QUEUED: snprintf(out, cap, "Waiting"); break;
	case DB_DL_ACTIVE:
		if (d->total > 0)
			snprintf(out, cap, "%s of %s", a, b);
		else
			snprintf(out, cap, "%s", a);
		break;
	case DB_DL_DONE: snprintf(out, cap, "Done  \xC2\xB7  %s  \xC2\xB7  in My Books", a); break;
	case DB_DL_FAILED: snprintf(out, cap, "Failed: %s", d->error); break;
	default: snprintf(out, cap, "Cancelled"); break;
	}
}

static void draw(void)
{
	font_draw(X0, 18, THEME_FONT, FONT_PX_TITLE, "Downloads");
	if (g_n == 0) {
		const char *a = "No downloads";
		const char *b = "Books you download from the Catalog appear here";
		font_draw(582 - font_width(FONT_PX_TITLE, a) / 2, 230, THEME_FONT, FONT_PX_TITLE, a);
		font_draw(582 - font_width(FONT_PX_LABEL, b) / 2, 266, THEME_SUBFONT, FONT_PX_LABEL, b);
	}
	for (int r = g_top; r < g_n && r < g_top + ROWS; r++) {
		const dlq_item_t *d = &g_items[r];
		int y = HEAD_H + (r - g_top) * ROW_H;
		if (r == g_sel) draw_rect(X0 - 8, y, W + 16, ROW_H - 4, THEME_SELECTED);
		char line[TEXT_LINE_MAX], st[192];
		text_ellipsize(FONT_PX_TITLE, d->title, W - 16, line);
		font_draw(X0 + 8, y + 10, THEME_FONT, FONT_PX_TITLE, line);
		status(d, st, sizeof st);
		text_ellipsize(FONT_PX_LABEL, st, W - 16, line);
		font_draw(X0 + 8, y + 38, d->state == DB_DL_FAILED ? THEME_BADGE : THEME_SUBFONT, FONT_PX_LABEL,
		          line);
		if (d->state == DB_DL_ACTIVE && d->total > 0) {
			int bw = 200, fill = (int)((double)bw * (double)d->bytes / (double)d->total);
			if (fill > bw) fill = bw;
			draw_rect(X0 + W - bw - 8, y + 46, bw, 4, THEME_PANEL);
			draw_rect(X0 + W - bw - 8, y + 46, fill, 4, THEME_SELECTOR);
		}
		if (r == g_sel) draw_selector(X0 - 8, y, W + 16, ROW_H - 4, g_now);
	}
	menu_draw(g_now);
}

static int hints(hint_t *out, int max)
{
	if (menu_is_open()) return menu_hints(out, max);
	hint_t h[] = {
		{GLYPH_LR, "Section"},
		{GLYPH_DPAD, "Choose"},
		{GLYPH_TRIANGLE, "Menu"},
		{GLYPH_CONFIRM, "Try again"},
	};
	int n = g_n == 0 ? 1 : 3;
	if (g_n > 0 && g_sel < g_n &&
	    (g_items[g_sel].state == DB_DL_FAILED || g_items[g_sel].state == DB_DL_CANCELLED))
		n = 4;
	return screen_copy_hints(h, n, out, max);
}

const screen_t screen_downloads = {"Downloads", enter, leave, update, draw, hints};
