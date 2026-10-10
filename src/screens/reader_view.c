/* reader_view.c - the comic reader's navigation: page steps, the vertical strip layout,
 * what to decode next, and saving the reading position. */
#include "screens/reader_priv.h"

#include "core/log.h"

#include <stdio.h>
#include <stdlib.h>

#define SAVE_IDLE_MS 1500 /* write the position once the page has been still this long */
#define EST_W 1000        /* size assumed for a strip page that was never decoded */
#define EST_H 1414

int reader_rtl(void) { return R.mode == RM_RTL; }

/* The crop box turned with the page (paged modes). */
static crop_t shown_crop(int page)
{
	crop_t c = reader_crop_src(page);
	switch (R.mode == RM_VERTICAL ? 0 : R.rot & 3) {
	case 1: return (crop_t){1.0f - c.b, c.l, 1.0f - c.t, c.r};
	case 2: return (crop_t){1.0f - c.r, 1.0f - c.b, 1.0f - c.l, 1.0f - c.t};
	case 3: return (crop_t){c.t, 1.0f - c.r, c.b, 1.0f - c.l};
	default: return c;
	}
}

int reader_page_w(int page)
{
	int w = R.mode != RM_VERTICAL && (R.rot & 1) ? R.ph[page] : R.pw[page];
	crop_t c = shown_crop(page);
	int cw = (int)((float)w * (c.r - c.l) + 0.5f);
	return cw > 0 ? cw : 1;
}

int reader_page_h(int page)
{
	int h = R.mode != RM_VERTICAL && (R.rot & 1) ? R.pw[page] : R.ph[page];
	crop_t c = shown_crop(page);
	int ch = (int)((float)h * (c.b - c.t) + 0.5f);
	return ch > 0 ? ch : 1;
}

int reader_box_w(void)
{
	float z = R.mode == RM_VERTICAL ? R.strip_zoom : R.view.zoom;
	return z > HIRES_ZOOM ? BOX_W_HIRES : BOX_W;
}

void reader_goto(int page, int pan)
{
	if (R.count <= 0) return;
	if (page < 0) page = 0;
	if (page >= R.count) page = R.count - 1;
	page = reader_spread_first(page);
	if (page != R.page) R.changed_at = R.now;
	R.page = page;
	if (R.mode == RM_VERTICAL) {
		reader_strip_scroll_to(page);
	} else {
		R.view.zoom = 1.0f;
		R.view.pan_x = R.view.pan_y = 0;
		R.swipe_dx = 0; /* a stored-up swipe must not fire on the new page */
		R.pending_pan = pan;
	}
	reader_wish();
}

void reader_step(int dir)
{
	int to = reader_spread_step(R.page, dir);
	if (to < 0) {
		if (dir > 0) reader_menu_open();
		return;
	}
	reader_goto(to, dir > 0 ? 1 : -1);
}

/* ---- vertical strip ----------------------------------------------------------------- */

void reader_note_size(int page, int w, int h)
{
	if (!R.pw || page < 0 || page >= R.count || w <= 0 || h <= 0) return;
	if (R.known[page] && R.pw[page] == w && R.ph[page] == h) return;
	R.pw[page] = w;
	R.ph[page] = h;
	R.known[page] = 1;
	R.sp_dirty = 1;
	/* Unknown pages borrow the latest real shape: a series is usually uniform. */
	for (int i = 0; i < R.count; i++)
		if (!R.known[i]) {
			R.pw[i] = w;
			R.ph[i] = h;
		}
	R.strip_dirty = 1;
}

void reader_strip_relayout(void)
{
	if (!R.items || R.count <= 0) return;
	/* Keep the page under the screen centre at the same relative spot. */
	int anchor = R.page;
	float frac = 0.5f;
	if (R.strip_total > 0 && R.items[anchor].height > 0) {
		float centre = R.scroll + SCREEN_H / 2.0f;
		frac = (centre - R.items[anchor].top) / R.items[anchor].height;
		if (frac < 0) frac = 0;
		if (frac > 1) frac = 1;
	}
	R.strip_total = strip_layout(R.pw, R.ph, R.count, SCREEN_W, R.strip_zoom, STRIP_GAP, R.items);
	R.scroll = R.items[anchor].top + frac * R.items[anchor].height - SCREEN_H / 2.0f;
	R.scroll = strip_clamp_scroll(R.strip_total, SCREEN_H, R.scroll);
	R.strip_dirty = 0;
}

void reader_strip_scroll_to(int page)
{
	if (!R.items) return;
	if (R.strip_dirty) reader_strip_relayout();
	R.scroll = strip_clamp_scroll(R.strip_total, SCREEN_H, R.items[page].top);
}

static float strip_max_x(void)
{
	float over = (SCREEN_W * R.strip_zoom - SCREEN_W) / 2;
	return over > 0 ? over : 0;
}

void reader_strip_pan(float dx, float dy)
{
	if (R.strip_dirty) reader_strip_relayout();
	float mx = strip_max_x();
	R.strip_x += dx;
	if (R.strip_x > mx) R.strip_x = mx;
	if (R.strip_x < -mx) R.strip_x = -mx;
	R.scroll = strip_clamp_scroll(R.strip_total, SCREEN_H, R.scroll - dy);
}

/* Zoom the strip keeping the strip point under (fx, fy) under it. Pages scale linearly
 * with the zoom (no gaps), so the scroll scales by the same ratio. */
void reader_strip_zoom_at(float zoom, float fx, float fy)
{
	if (zoom < 1.0f) zoom = 1.0f;
	if (zoom > 3.0f) zoom = 3.0f;
	if (R.strip_dirty) reader_strip_relayout();
	float ratio = zoom / R.strip_zoom;
	float px = fx - SCREEN_W / 2.0f - R.strip_x; /* finger relative to the strip centre */
	R.strip_zoom = zoom;
	R.strip_total = strip_layout(R.pw, R.ph, R.count, SCREEN_W, zoom, STRIP_GAP, R.items);
	R.scroll = strip_clamp_scroll(R.strip_total, SCREEN_H, (R.scroll + fy) * ratio - fy);
	R.strip_x = fx - SCREEN_W / 2.0f - px * ratio;
	reader_strip_pan(0, 0);
}

int reader_strip_alloc(int n)
{
	R.pw = calloc((size_t)n, sizeof *R.pw);
	R.ph = calloc((size_t)n, sizeof *R.ph);
	R.known = calloc((size_t)n, 1);
	R.items = calloc((size_t)n, sizeof *R.items);
	if (!R.pw || !R.ph || !R.known || !R.items) return -1;
	for (int i = 0; i < n; i++) {
		R.pw[i] = EST_W;
		R.ph[i] = EST_H;
	}
	R.strip_dirty = 1;
	return 0;
}

void reader_strip_free(void)
{
	free(R.pw);
	free(R.ph);
	free(R.known);
	free(R.items);
	R.pw = R.ph = NULL;
	R.known = NULL;
	R.items = NULL;
}

/* ---- decoding and saving -------------------------------------------------------------- */

static int add(loader_req_t *w, int n, int page, int box_w)
{
	if (page < 0 || page >= R.count || n >= LOADER_WANT_MAX) return n;
	for (int i = 0; i < n; i++)
		if (w[i].page == page && w[i].max_w == box_w) return n;
	w[n].page = page;
	w[n].max_w = box_w;
	w[n].max_h = BOX_H_MAX;
	return n + 1;
}

void reader_wish(void)
{
	if (!R.ld || R.count <= 0) return;
	loader_req_t w[LOADER_WANT_MAX];
	int n = 0, box = reader_box_w();
	if (R.mode == RM_VERTICAL) {
		int first = R.page, last = R.page;
		if (R.items) strip_visible(R.items, R.count, R.scroll, SCREEN_H, &first, &last);
		if (first < 0) first = last = R.page;
		for (int p = first; p <= last; p++) n = add(w, n, p, BOX_W);
		if (box != BOX_W)
			for (int p = first; p <= last; p++) n = add(w, n, p, box);
		n = add(w, n, last + 1, BOX_W);
		n = add(w, n, last + 2, BOX_W);
		n = add(w, n, first - 1, BOX_W);
	} else {
		int p[2], shown = reader_shown(p);
		for (int i = 0; i < shown; i++) n = add(w, n, p[i], BOX_W);
		if (box != BOX_W)
			for (int i = 0; i < shown; i++) n = add(w, n, p[i], box);
		n = add(w, n, R.page + 1, BOX_W);
		n = add(w, n, R.page + 2, BOX_W);
		if (reader_spreads_on()) {
			n = add(w, n, R.page + 3, BOX_W);
		}
		n = add(w, n, R.page - 1, BOX_W);
		if (reader_spreads_on()) n = add(w, n, R.page - 2, BOX_W);
	}
	loader_want(R.ld, w, n);
}

void reader_save(int force)
{
	if (R.stream) return; /* streams have no library row to save to */
	if (R.count <= 0 || R.page == R.saved_page) return;
	if (!force && R.now - R.changed_at < SAVE_IDLE_MS) return;
	char loc[32];
	snprintf(loc, sizeof loc, "p:%d", R.page);
	double progress = R.count > 1 ? (double)R.page / (double)(R.count - 1) : 1.0;
	if (db_set_progress(R.book.id, R.page, R.count, loc, progress) < 0)
		log_printf("reader: save: %s", db_last_error());
	else R.saved_page = R.page;
}
