/* reader_layout.c - the comic reader's per-book settings, double-page spreads
 * (reader/spread, Komga's pairing rule) and white-border cropping (reader/crop).
 *
 * Settings: reading mode, fit, spreads and crop are saved per book (book_settings) when
 * changed in the reader; a book without its own value uses the default from Settings.
 * Spreads: a page's shape is only known once it has been decoded (fixed documents know all
 * of theirs up front), so pages not seen yet count as portrait, and the pairing is rebuilt
 * whenever a real size arrives. Spreads apply to the paged modes when not rotated.
 * Crop: measured on each page's first upload while cropping is on, kept as fractions so
 * the sharper zoomed copy uses the same box. */
#include "screens/reader_priv.h"

#include "core/log.h"
#include "core/prefs.h"

#include <stdio.h>
#include <stdlib.h>

static int *g_w, *g_h; /* scratch for spread_build */

int reader_setting(const char *key, int def, int lo, int hi)
{
	char v[16];
	long x = db_effective_setting(R.book.id, key, v, sizeof v) == 1 ? atol(v)
	                                                                 : prefs_get_int("reader", key, def);
	return x >= lo && x <= hi ? (int)x : def;
}

void reader_setting_save(const char *key, int value)
{
	char v[16];
	snprintf(v, sizeof v, "%d", value);
	if (db_book_setting_set(R.book.id, key, v) < 0)
		log_printf("reader: setting %s: %s", key, db_last_error());
}

int reader_layout_alloc(int n)
{
	R.sp = calloc((size_t)n, sizeof *R.sp);
	R.cropbox = calloc((size_t)n, sizeof *R.cropbox);
	R.crop_known = calloc((size_t)n, 1);
	g_w = calloc((size_t)n, sizeof *g_w);
	g_h = calloc((size_t)n, sizeof *g_h);
	R.nsp = 0;
	R.sp_dirty = 1;
	return R.sp && R.cropbox && R.crop_known && g_w && g_h ? 0 : -1;
}

void reader_layout_free(void)
{
	free(R.sp);
	free(R.cropbox);
	free(R.crop_known);
	free(g_w);
	free(g_h);
	R.sp = NULL;
	R.cropbox = NULL;
	R.crop_known = NULL;
	g_w = g_h = NULL;
	R.nsp = 0;
}

/* ---- spreads -------------------------------------------------------------------------- */

int reader_spreads_on(void)
{
	return R.spreads && R.mode != RM_VERTICAL && R.rot == 0 && R.sp && R.count > 0;
}

void reader_spreads_rebuild(void)
{
	R.sp_dirty = 0;
	if (!reader_spreads_on()) return;
	for (int i = 0; i < R.count; i++) {
		g_w[i] = R.known[i] ? R.pw[i] : 2; /* not decoded yet: portrait */
		g_h[i] = R.known[i] ? R.ph[i] : 3;
	}
	R.nsp = spread_build(g_w, g_h, R.count, R.spreads == 2, R.sp, R.count);
}

static int spread_index(int page)
{
	if (!reader_spreads_on() || R.nsp <= 0) return -1;
	return spread_of_page(R.sp, R.nsp, page);
}

int reader_spread_first(int page)
{
	int s = spread_index(page);
	return s >= 0 ? R.sp[s].first : page;
}

int reader_spread_step(int page, int dir)
{
	int s = spread_index(page);
	if (s < 0) {
		int to = page + dir;
		return to >= 0 && to < R.count ? to : -1;
	}
	s += dir;
	return s >= 0 && s < R.nsp ? R.sp[s].first : -1;
}

int reader_shown(int out[2])
{
	int s = spread_index(R.page);
	if (s >= 0) return spread_pages(R.sp, R.nsp, s, reader_rtl(), out);
	out[0] = R.page;
	return 1;
}

void reader_view_size(float *w, float *h)
{
	int p[2], n = reader_shown(p);
	*w = (float)reader_page_w(p[0]);
	*h = (float)reader_page_h(p[0]);
	if (n < 2) return;
	/* Both pages at the taller one's height, side by side. */
	float w1 = (float)reader_page_w(p[1]), h1 = (float)reader_page_h(p[1]);
	float hh = *h > h1 ? *h : h1;
	*w = *w * hh / *h + w1 * hh / h1;
	*h = hh;
}

/* ---- crop ----------------------------------------------------------------------------- */

crop_t reader_crop_src(int page)
{
	crop_t whole = {0.0f, 0.0f, 1.0f, 1.0f};
	if (!R.crop || R.mode == RM_VERTICAL || !R.crop_known || page < 0 || page >= R.count ||
	    !R.crop_known[page])
		return whole;
	return R.cropbox[page];
}

static const uint8_t *page_row(const void *ud, int y)
{
	const loader_page_t *p = ud;
	for (int i = p->ntiles - 1; i > 0; i--)
		if (y >= p->tiles[i].y) return p->tiles[i].rgba + (size_t)(y - p->tiles[i].y) * (size_t)p->w * 4;
	return p->tiles[0].rgba + (size_t)y * (size_t)p->w * 4;
}

void reader_crop_note(const loader_page_t *p)
{
	if (!R.crop || !R.crop_known || !p || p->page < 0 || p->page >= R.count || R.crop_known[p->page])
		return;
	if (p->w <= 0 || p->h <= 0 || p->ntiles <= 0 || !p->tiles)
		return; /* corrupt/empty decode: page_row would read out of bounds */
	crop_detect(page_row, p, p->w, p->h, &R.cropbox[p->page]);
	R.crop_known[p->page] = 1;
}
