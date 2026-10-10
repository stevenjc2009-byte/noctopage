/* bookld_work.c - reader/bookld worker thread: it alone touches the mdoc_t. Opens the book,
 * lays it out (first layout, then every style change, keeping the reader's place), resolves
 * positions and renders wished pages into the cache.
 *
 * Jobs, in priority order: quit, a pending style, a pending resolve, the first wish that is
 * not cached and has not failed. MuPDF cannot be interrupted, so bookld_close waits for the
 * job in progress (an open, a layout or one page) before the worker sees quit. */
#include "reader/bookld_priv.h"

#include "core/affinity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int clampi(int v, int lo, int hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

static void layout_of(const bookld_style_t *st, mdoc_layout_t *out)
{
	out->page_w = BOOKLD_PAGE_W;
	out->page_h = BOOKLD_PAGE_H;
	out->font_px = (float)st->font_px;
	out->margin_px = (float)st->margin_px;
	out->line_spacing = st->line_spacing;
	out->font_family = st->font_family;
}

/* The page holding pos in the current layout, clamped to [0, count); 0 when pos is empty
 * or unparsable. */
static int page_for(bookld_t *b, const char *pos, int count)
{
	if (!pos || !pos[0]) return 0;
	int p = mdoc_page_for_position(b->doc, pos);
	return p < 0 ? 0 : clampi(p, 0, count - 1);
}

/* Table of contents of the current layout into the worker's scratch: items, >= 0. */
static int load_toc(bookld_t *b)
{
	int n = mdoc_toc(b->doc, b->wtoc, BOOKLD_TOC_MAX);
	return n < 0 ? 0 : n;
}

/* A finished layout becomes the new generation: called with mu held. Old pages are dropped
 * (pinned ones are freed on their release), the wish list and failures belonged to the old
 * page numbers. */
static void publish_locked(bookld_t *b, int count, int anchor, int ntoc)
{
	b->gen++;
	b->count = count;
	b->anchor = anchor;
	memcpy(b->toc, b->wtoc, sizeof *b->toc * (size_t)ntoc);
	b->ntoc = ntoc;
	b->nwish = 0;
	b->nfailed = 0;
	b->err_page = -1;
	pc_clear(b->cache);
}

static void fail(bookld_t *b, const char *msg)
{
	pthread_mutex_lock(&b->mu);
	snprintf(b->err, sizeof b->err, "%s", msg && msg[0] ? msg : "cannot open");
	b->status = BOOKLD_FAILED;
	b->busy = 0;
	pthread_mutex_unlock(&b->mu);
}

/* Open and lay out with the latest style; READY gen 1 or FAILED. Returns 1 when READY. */
static int open_book(bookld_t *b)
{
	char err[BOOKLD_ERR_CAP] = "";
	b->doc = mdoc_open(b->path, err, sizeof err);
	if (!b->doc) {
		fail(b, err);
		return 0;
	}
	pthread_mutex_lock(&b->mu);
	bookld_style_t st = b->style; /* a set_style while OPENING lands here ... */
	b->style_pending = 0;
	int quit = b->quit;
	pthread_mutex_unlock(&b->mu);
	if (quit) return 0;
	int fixed = !mdoc_is_reflowable(b->doc);
	if (!fixed) {
		mdoc_layout_t lay;
		layout_of(&st, &lay);
		if (mdoc_set_layout(b->doc, &lay) != 0) {
			fail(b, "cannot lay out the book");
			return 0;
		}
	}
	int n = mdoc_page_count(b->doc);
	if (n <= 0) {
		fail(b, "the book has no pages");
		return 0;
	}
	int anchor = page_for(b, b->resume, n), ntoc = load_toc(b);
	pthread_mutex_lock(&b->mu);
	b->fixed = fixed;
	b->status = BOOKLD_READY;
	publish_locked(b, n, anchor, ntoc);
	if (fixed) b->style_pending = 0;
	b->busy = b->style_pending; /* ... one during the layout runs next, as a relayout */
	pthread_mutex_unlock(&b->mu);
	b->prev_count = n;
	return 1;
}

/* Re-lay out with the latest style, keeping the text at the start of keep_page. A keep_page
 * given against the previous generation (set_style called while the relayout that made the
 * current one ran) is mapped across: the same page as that relayout kept -> its anchor,
 * otherwise the anchor moved by the same distance scaled by the page-count ratio. Mapping
 * is approximate only in that rare case. On a layout failure the old generation stays. */
static void relayout(bookld_t *b)
{
	pthread_mutex_lock(&b->mu);
	bookld_style_t st = b->style;
	int keep = b->keep_page, kgen = b->keep_gen, gen = b->gen;
	int count = b->count, anchor = b->anchor;
	b->style_pending = 0;
	pthread_mutex_unlock(&b->mu);

	int page;
	if (kgen == gen)
		page = keep;
	else if (kgen <= 0 || b->prev_keep < 0 || b->prev_count <= 0)
		page = anchor;
	else
		page = anchor + (int)((long long)(keep - b->prev_keep) * count / b->prev_count);
	page = clampi(page, 0, count - 1);
	char pos[MDOC_POS_MAX] = "";
	if (mdoc_position(b->doc, page, pos) != 0) pos[0] = 0;

	mdoc_layout_t lay;
	layout_of(&st, &lay);
	int n = mdoc_set_layout(b->doc, &lay) == 0 ? mdoc_page_count(b->doc) : -1;
	int anchor2 = n > 0 ? page_for(b, pos, n) : 0, ntoc = n > 0 ? load_toc(b) : 0;
	pthread_mutex_lock(&b->mu);
	if (n > 0) {
		publish_locked(b, n, anchor2, ntoc);
		b->prev_keep = page;
		b->prev_count = count;
	}
	b->busy = b->style_pending;
	pthread_mutex_unlock(&b->mu);
}

static void resolve(bookld_t *b)
{
	char pos[MDOC_POS_MAX];
	pthread_mutex_lock(&b->mu);
	memcpy(pos, b->resolve_pos, sizeof pos);
	int count = b->count;
	b->resolve_pending = 0;
	pthread_mutex_unlock(&b->mu);
	int p = pos[0] ? mdoc_page_for_position(b->doc, pos) : -1;
	if (p >= count) p = count - 1;
	pthread_mutex_lock(&b->mu);
	b->jump_page = p < 0 ? -1 : p;
	b->jump_serial++;
	pthread_mutex_unlock(&b->mu);
}

/* Text search of the current generation, from the requested start page. The answer is
 * only published when no relayout slipped in meanwhile (page numbers are per-gen). */
static void find(bookld_t *b)
{
	char query[BOOKLD_FIND_MAX];
	pthread_mutex_lock(&b->mu);
	memcpy(query, b->find_query, sizeof query);
	int start = b->find_start, count = b->count, gen = b->gen;
	b->find_pending = 0;
	pthread_mutex_unlock(&b->mu);
	int p = -1;
	if (query[0] && count > 0) {
		if (start < 0) start = 0;
		if (start < count) p = mdoc_find_page(b->doc, start, query);
	}
	pthread_mutex_lock(&b->mu);
	if (gen == b->gen) {
		b->find_page = p;
		b->find_serial++;
	}
	pthread_mutex_unlock(&b->mu);
}

/* img as exactly BOOKLD_PAGE_W x BOOKLD_PAGE_H: itself when it already is, else a new
 * paper-filled buffer with img centred on it (cropped when larger). Frees img.rgba either
 * way unless returned; NULL on out of memory. */
static uint8_t *to_screen(mdoc_image_t *img, uint32_t paper)
{
	const int W = BOOKLD_PAGE_W, H = BOOKLD_PAGE_H;
	if (img->w == W && img->h == H) return img->rgba;
	uint8_t *px = malloc((size_t)W * H * 4);
	if (px) {
		const uint8_t c[4] = { (uint8_t)(paper >> 16), (uint8_t)(paper >> 8), (uint8_t)paper, 255 };
		for (size_t i = 0; i < (size_t)W * H; i++) memcpy(px + i * 4, c, 4);
		int dx = (W - img->w) / 2, dy = (H - img->h) / 2;
		int sx = dx < 0 ? -dx : 0, sy = dy < 0 ? -dy : 0;
		int cw = img->w - sx < W ? img->w - sx : W, ch = img->h - sy < H ? img->h - sy : H;
		if (dx < 0) dx = 0;
		if (dy < 0) dy = 0;
		for (int y = 0; y < ch && cw > 0; y++)
			memcpy(px + ((size_t)(dy + y) * W + dx) * 4,
			       img->rgba + ((size_t)(sy + y) * img->w + sx) * 4, (size_t)cw * 4);
	}
	free(img->rgba);
	img->rgba = NULL;
	return px;
}

/* Render one page at screen size: reflowable at scale 1 (the layout is the screen), fixed
 * layout scaled to fit the screen and centred on paper. NULL with a message in err. */
static bpage_t *render_page(bookld_t *b, int gen, int page, uint32_t ink, uint32_t paper,
                            char *err, size_t cap)
{
	float scale = 1.0f, w, h;
	int fixed;
	pthread_mutex_lock(&b->mu);
	fixed = b->fixed;
	pthread_mutex_unlock(&b->mu);
	if (fixed && mdoc_page_size(b->doc, page, &w, &h) == 0 && w > 0 && h > 0) {
		float sw = BOOKLD_PAGE_W / w, sh = BOOKLD_PAGE_H / h;
		scale = sw < sh ? sw : sh;
	}
	mdoc_image_t img = { 0, 0, NULL };
	if (mdoc_render(b->doc, page, scale, 0, NULL, ink, paper, &img) != 0 || !img.rgba ||
	    img.w <= 0 || img.h <= 0) {
		free(img.rgba);
		snprintf(err, cap, "render failed");
		return NULL;
	}
	uint8_t *px = to_screen(&img, paper);
	bpage_t *p = px ? calloc(1, sizeof *p) : NULL;
	if (!p) {
		free(px);
		snprintf(err, cap, "out of memory");
		return NULL;
	}
	p->pub.gen = gen;
	p->pub.page = page;
	p->pub.ink = ink;
	p->pub.paper = paper;
	p->pub.w = BOOKLD_PAGE_W;
	p->pub.h = BOOKLD_PAGE_H;
	p->pub.rgba = px;
	if (mdoc_position(b->doc, page, p->pub.pos) != 0) p->pub.pos[0] = 0;
	return p;
}

void bookld_page_free(void *v, void *user)
{
	(void)user;
	bpage_t *p = v;
	free(p->pub.rgba);
	free(p);
}

static int in_list(const int *list, int n, int v)
{
	for (int i = 0; i < n; i++)
		if (list[i] == v) return 1;
	return 0;
}

/* Next wish to render, or -1. Called with mu held. */
static int pick_locked(bookld_t *b)
{
	if (b->status != BOOKLD_READY || b->wish_gen != b->gen) return -1;
	for (int i = 0; i < b->nwish; i++) {
		int page = b->wish[i];
		if (page >= b->count || in_list(b->failed, b->nfailed, page)) continue;
		if (pc_contains(b->cache, bookld_key_book(b->gen, b->ink), page,
		                bookld_key_variant(b->paper)))
			continue;
		return i;
	}
	return -1;
}

/* Remembered only while the same request is still wished; one that left may be retried. */
static void record_failure_locked(bookld_t *b, int gen, int page, uint32_t ink, uint32_t paper,
                                  const char *msg)
{
	b->err_page = page;
	snprintf(b->page_err, sizeof b->page_err, "%s", msg);
	if (b->wish_gen == gen && b->ink == ink && b->paper == paper &&
	    in_list(b->wish, b->nwish, page) && !in_list(b->failed, b->nfailed, page) &&
	    b->nfailed < BOOKLD_WANT_MAX)
		b->failed[b->nfailed++] = page;
}

/* Render wish i and cache it. Called with mu held; returns with it held. */
static void render_wish_locked(bookld_t *b, int i)
{
	int gen = b->gen, page = b->wish[i];
	uint32_t ink = b->ink, paper = b->paper;
	b->renders++;
	pthread_mutex_unlock(&b->mu);

	char err[BOOKLD_ERR_CAP] = "";
	bpage_t *p = render_page(b, gen, page, ink, paper, err, sizeof err);
	int rc = PC_OK;
	if (p) { /* only this thread inserts, and the generation cannot change meanwhile */
		rc = pc_put(b->cache, bookld_key_book(gen, ink), page, bookld_key_variant(paper), p,
		            BOOKLD_PAGE_COST, bookld_page_free, NULL);
		if (rc != PC_OK) {
			snprintf(err, sizeof err, "cache insert failed");
			bookld_page_free(p, NULL);
		}
	}
	pthread_mutex_lock(&b->mu);
	if (!p || rc != PC_OK) record_failure_locked(b, gen, page, ink, paper, err);
}

void *bookld_worker(void *arg)
{
	bookld_t *b = arg;
	affinity_use_all_cores();
	if (open_book(b)) {
		pthread_mutex_lock(&b->mu);
		while (!b->quit) {
			int i = -1;
			if (b->hold) {
			} else if (b->style_pending) {
				pthread_mutex_unlock(&b->mu);
				relayout(b);
				pthread_mutex_lock(&b->mu);
				continue;
			} else if (b->resolve_pending) {
				pthread_mutex_unlock(&b->mu);
				resolve(b);
				pthread_mutex_lock(&b->mu);
				continue;
			} else if (b->find_pending) {
				pthread_mutex_unlock(&b->mu);
				find(b);
				pthread_mutex_lock(&b->mu);
				continue;
			} else {
				i = pick_locked(b);
			}
			if (i < 0)
				pthread_cond_wait(&b->cv, &b->mu);
			else
				render_wish_locked(b, i);
		}
		pthread_mutex_unlock(&b->mu);
	}
	if (b->doc) mdoc_close(b->doc);
	b->doc = NULL;
	return NULL;
}
