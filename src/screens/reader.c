/* reader.c - the comic reader's lifecycle and frame loop. Navigation is in reader_view.c,
 * drawing in reader_draw.c, input in reader_input.c, GPU pages in reader_tex.c. */
#include "screens/reader.h"
#include "screens/reader_priv.h"

#include "core/log.h"
#include "lib/reading_clock.h"
#include "core/prefs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

reader_t R;
static int g_open;
static reading_clock_t g_clock;
static void reading_flush(void)
{
	if (R.stream) return; /* streams have no library row to charge time to */
	if (!g_clock.milliseconds && !g_clock.pages) return;
	if (db_reading_add(R.book.id, g_clock.milliseconds, g_clock.pages) >= 0) {
		g_clock.milliseconds = 0;
		g_clock.pages = 0;
	}
}

static int g_last_wish_page = -1, g_last_wish_box, g_last_wish_scroll;

int reader_is_open(void) { return g_open; }

void reader_open(const db_book_t *b)
{
	if (g_open) reader_close();
	memset(&R, 0, sizeof R);
	memset(&g_clock, 0, sizeof g_clock);
	R.book = *b;
	R.mode = (read_mode_t)reader_setting("mode", RM_LTR, 0, RM_COUNT - 1);
	R.fit = (fit_mode_t)reader_setting("fit", FIT_SMART, FIT_WIDTH, FIT_ORIGINAL);
	R.spreads = reader_setting("spreads", 0, 0, 2);
	R.crop = reader_setting("crop", 0, 0, 1);
	R.dim = (int)prefs_get_int("reader", "dim", 0);
	if (R.dim < 0 || R.dim >= DIM_LEVELS) R.dim = 0;
	fit_view_init(&R.view, SCREEN_W, SCREEN_H);
	R.view.mode = R.fit;
	R.strip_zoom = 1.0f;
	R.saved_page = b->page;
	R.failed_page = -1;
	char rot[8];
	if (db_effective_setting(b->id, "rotate", rot, sizeof rot) == 1) R.rot = atoi(rot) & 3;
	tapsm_init(&R.tap, 1);
	g_last_wish_page = -1;
	R.ld = loader_open(b->path, b->kind);
	if (!R.ld) snprintf(R.err, sizeof R.err, "out of memory");
	g_open = 1;
	log_printf("reader: open %s (kind %d, page %d)", b->path, b->kind, b->page);
}

void reader_open_stream(const char *title, const char *tmpl, int count, const char *user,
                        const char *pass)
{
	if (g_open) reader_close();
	memset(&R, 0, sizeof R);
	memset(&g_clock, 0, sizeof g_clock);
	snprintf(R.book.title, sizeof R.book.title, "%s", title ? title : "");
	R.book.kind = DB_KIND_STREAM;
	R.stream = 1;
	R.mode = (read_mode_t)reader_setting("mode", RM_LTR, 0, RM_COUNT - 1);
	R.fit = (fit_mode_t)reader_setting("fit", FIT_SMART, FIT_WIDTH, FIT_ORIGINAL);
	R.spreads = reader_setting("spreads", 0, 0, 2);
	R.crop = reader_setting("crop", 0, 0, 1);
	R.dim = (int)prefs_get_int("reader", "dim", 0);
	if (R.dim < 0 || R.dim >= DIM_LEVELS) R.dim = 0;
	fit_view_init(&R.view, SCREEN_W, SCREEN_H);
	R.view.mode = R.fit;
	R.strip_zoom = 1.0f;
	R.failed_page = -1;
	tapsm_init(&R.tap, 1);
	g_last_wish_page = -1;
	R.ld = loader_open_stream(tmpl, count, user, pass);
	if (!R.ld) snprintf(R.err, sizeof R.err, "out of memory");
	g_open = 1;
	log_printf("reader: stream %s (%d pages)", tmpl ? tmpl : "", count);
}

void reader_close(void)
{
	if (!g_open) return;
	reading_flush();
	reader_save(1);
	if (R.ld) loader_close(R.ld);
	R.ld = NULL;
	rtex_clear();
	reader_strip_free();
	reader_layout_free();
	g_open = 0;
	log_printf("reader: closed at page %d", R.page);
}

/* The loader finished opening: size the per-page arrays and go to the saved page. */
static void on_ready(void)
{
	int n = loader_page_count(R.ld);
	if (n <= 0 || reader_strip_alloc(n) < 0 || reader_layout_alloc(n) < 0) {
		reader_strip_free();
		reader_layout_free();
		snprintf(R.err, sizeof R.err, n <= 0 ? "no pages" : "out of memory");
		return;
	}
	R.count = n;
	for (int i = 0; i < n; i++) { /* fixed documents know their sizes up front */
		int w, h;
		if (loader_page_size(R.ld, i, &w, &h) == 0 && w > 0 && h > 0) {
			R.pw[i] = w;
			R.ph[i] = h;
			R.known[i] = 1;
		}
	}
	R.page = -1;
	reader_spreads_rebuild();
	int start = R.book.page >= 0 && R.book.page < n ? R.book.page : 0;
	reader_goto(start, 1);
	R.saved_page = R.book.page;
	reader_menu_reset();
	log_printf("reader: %d pages, start at %d", n, start);
}

/* Upload what is about to be drawn, current page first. */
static void fetch(void)
{
	int box = reader_box_w();
	if (R.mode == RM_VERTICAL) {
		int first, last;
		if (!R.items || strip_visible(R.items, R.count, R.scroll, SCREEN_H, &first, &last) == 0)
			return;
		for (int p = first; p <= last; p++)
			if (!rtex_fetch(p, box, BOX_H_MAX) && box != BOX_W) rtex_fetch(p, BOX_W, BOX_H_MAX);
		return;
	}
	int p[2], n = reader_shown(p), lo = R.page, hi = R.page;
	for (int i = 0; i < n; i++) {
		if (!rtex_fetch(p[i], box, BOX_H_MAX) && box != BOX_W) rtex_fetch(p[i], BOX_W, BOX_H_MAX);
		if (p[i] < lo) lo = p[i];
		if (p[i] > hi) hi = p[i];
	}
	rtex_fetch(hi + 1, BOX_W, BOX_H_MAX);
	rtex_fetch(lo - 1, BOX_W, BOX_H_MAX);
}

static void track(void)
{
	if (R.sp_dirty) {
		reader_spreads_rebuild();
		int first = reader_spread_first(R.page);
		if (first != R.page && R.mode != RM_VERTICAL) R.page = first; /* pairing moved */
	}
	if (R.mode == RM_VERTICAL && R.items) {
		if (R.strip_dirty) reader_strip_relayout();
		int p = strip_page_at(R.items, R.count, R.scroll + SCREEN_H / 2.0f);
		if (p >= 0 && p != R.page) {
			R.page = p;
			R.changed_at = R.now;
		}
	}
	if (R.pending_pan && R.mode != RM_VERTICAL && R.known[R.page]) {
		float w, h;
		reader_view_size(&w, &h);
		if (R.pending_pan > 0)
			fit_pan_start(&R.view, w, h, reader_rtl());
		else
			fit_pan_end(&R.view, w, h, reader_rtl());
		R.pending_pan = 0;
	}
	int box = reader_box_w(), scroll = (int)R.scroll / 64;
	if (R.page != g_last_wish_page || box != g_last_wish_box || scroll != g_last_wish_scroll) {
		g_last_wish_page = R.page;
		g_last_wish_box = box;
		g_last_wish_scroll = scroll;
		reader_wish();
	}
}

void reader_update(const input_t *in)
{
	if (!g_open) return;
	R.now = in->now_ms;
	reading_clock_tick(&g_clock, in->now_ms, R.page, (R.count > 0 && !R.err[0] && R.failed_page != R.page) && !reader_menu_is_open(),
	                   in->pressed || in->touch_count || in->lx < 108 || in->lx > 148 ||
	                   in->ly < 108 || in->ly > 148 || in->ry < 100 || in->ry > 156);
	if (g_clock.milliseconds >= 30000 || g_clock.pages >= 10) reading_flush();
	rtex_new_frame();
	char err[160];
	loader_status_t st = R.ld ? loader_status(R.ld, err, sizeof err) : LOADER_FAILED;
	if (st == LOADER_READY && R.count == 0 && !R.err[0]) on_ready();

	input_t quiet;
	if (R.count > 0 && reader_menu_is_open()) {
		reader_menu_update(in);
		memset(&quiet, 0, sizeof quiet); /* the page sees no input while a menu is up */
		quiet.lx = quiet.ly = quiet.rx = quiet.ry = 128;
		quiet.now_ms = in->now_ms;
		in = &quiet;
	} else if (in->pressed & IN_BACK) {
		reader_close();
		return;
	}
	if (R.count <= 0) return;

	int failed = R.ld ? loader_take_error(R.ld, err, sizeof err) : -1;
	if (failed >= 0) {
		log_printf("reader: page %d: %s", failed, err);
		R.failed_page = failed;
	}
	reader_handle_buttons(in);
	reader_handle_touch(in);
	track();
	fetch();
	reader_save(0);
}

void reader_draw(void)
{
	if (!g_open) return;
	reader_draw_pages();
	reader_draw_overlay();
	reader_menu_draw();
}

int reader_hints(hint_t *out, int max)
{
	if (!g_open) return 0;
	if (reader_menu_is_open()) return reader_menu_hints(out, max);
	if (!R.overlay) return 0;
	static const hint_t paged[] = {
		{GLYPH_BACK, "Close"},      {GLYPH_LR, "Turn page"}, {GLYPH_SQUARE, "Mode"},
		{GLYPH_CONFIRM, "Menu"},    {GLYPH_SELECT, "Dim"},   {GLYPH_TRIANGLE, "Hide"},
	};
	static const hint_t strip[] = {
		{GLYPH_BACK, "Close"},     {GLYPH_DPAD, "Scroll"},  {GLYPH_SQUARE, "Mode"},
		{GLYPH_CONFIRM, "Menu"},   {GLYPH_SELECT, "Dim"},   {GLYPH_TRIANGLE, "Hide"},
	};
	const hint_t *h = R.mode == RM_VERTICAL ? strip : paged;
	int n = 6;
	if (n > max) n = max;
	memcpy(out, h, sizeof *h * (size_t)n);
	return n;
}
