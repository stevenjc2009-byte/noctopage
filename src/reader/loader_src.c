/* loader_src.c - the worker side of reader/loader: open a book (archive, folder, fixed
 * document) and render one page into an lpage_t (one RGBA buffer, tiles pointing into it).
 * Same archive/folder + pagelist + decode pattern as lib/coverimg.c. Worker thread only. */
#include "reader/loader_priv.h"

#include "comic/archive.h"
#include "comic/folder.h"
#include "comic/pagelist.h"
#include "doc/mdoc.h"
#include "img/decode.h"
#include "lib/db.h"
#include "lib/formats.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_err(char *err, size_t cap, const char *msg)
{
	if (cap > 0) snprintf(err, cap, "%s", msg);
}

/* Reading order of n named entries into s->idx; the page count, or -1. */
static int build_pages(lsrc_t *s, int n, const char *(*name)(lsrc_t *s, int i), char *err,
                       size_t cap)
{
	if (n <= 0) {
		set_err(err, cap, "no pages");
		return -1;
	}
	const char **names = malloc(sizeof *names * (size_t)n);
	s->idx = malloc(sizeof *s->idx * (size_t)n);
	if (!names || !s->idx) {
		free(names);
		set_err(err, cap, "out of memory");
		return -1;
	}
	for (int i = 0; i < n; i++) names[i] = name(s, i);
	int np = pagelist_build(names, n, s->idx);
	free(names);
	if (np <= 0) {
		snprintf(err, cap, "no image pages (%d entries)", n);
		return -1;
	}
	return np;
}

static const char *arc_name_s(lsrc_t *s, int i) { return arc_name(s->arc, i); }
static const char *fold_name_s(lsrc_t *s, int i) { return folder_name(s->fold, i); }

int lsrc_open(lsrc_t *s, const char *path, int kind, char *err, size_t cap)
{
	memset(s, 0, sizeof *s);
	s->kind = kind;
	if (!path) {
		set_err(err, cap, "bad arguments");
		return -1;
	}
	if (kind == DB_KIND_ARCHIVE) {
		s->arc = arc_open(path, err, cap);
		if (!s->arc) return -1;
		s->n = build_pages(s, arc_count(s->arc), arc_name_s, err, cap);
	} else if (kind == DB_KIND_FOLDER) {
		s->fold = folder_open(path, formats_is_image, err, cap);
		if (!s->fold) return -1;
		s->n = build_pages(s, folder_count(s->fold), fold_name_s, err, cap);
	} else if (kind == DB_KIND_FIXED) {
		s->doc = mdoc_open(path, err, cap);
		if (!s->doc) return -1;
		if (mdoc_is_reflowable(s->doc)) {
			set_err(err, cap, "reflowable books are not paged");
			return -1;
		}
		s->n = mdoc_page_count(s->doc);
		if (s->n <= 0) set_err(err, cap, "document has no pages");
	} else if (kind == DB_KIND_REFLOW) {
		set_err(err, cap, "reflowable books are not paged");
		return -1;
	} else {
		set_err(err, cap, "unknown kind");
		return -1;
	}
	return s->n > 0 ? s->n : -1;
}

void lsrc_close(lsrc_t *s)
{
	arc_close(s->arc);
	folder_close(s->fold);
	if (s->doc) mdoc_close(s->doc);
	free(s->idx);
	memset(s, 0, sizeof *s);
}

int lsrc_doc_size(lsrc_t *s, int page, int *w, int *h)
{
	float wp, hp;
	if (!s->doc || mdoc_page_size(s->doc, page, &wp, &hp) != 0) return -1;
	*w = (int)lroundf(wp);
	*h = (int)lroundf(hp);
	return 0;
}

void lpage_free(void *v, void *user)
{
	(void)user;
	lpage_t *p = v;
	if (!p) return;
	free(p->buf);
	free(p);
}

size_t lpage_cost(const lpage_t *p)
{
	return (size_t)p->pub.w * (size_t)p->pub.h * 4u + sizeof *p +
	       sizeof(loader_tile_t) * (size_t)p->pub.ntiles;
}

/* Wrap a w x h RGBA buffer (ownership taken) into tiles of at most LOADER_TILE_MAX rows. */
static lpage_t *wrap(uint8_t *buf, int w, int h, int src_w, int src_h, int page, char *err,
                     size_t cap)
{
	if (!buf || w <= 0 || h <= 0 || w > LOADER_TILE_MAX) {
		free(buf);
		set_err(err, cap, "bad render size");
		return NULL;
	}
	int nt = (h + LOADER_TILE_MAX - 1) / LOADER_TILE_MAX;
	lpage_t *p = calloc(1, sizeof *p + sizeof(loader_tile_t) * (size_t)nt);
	if (!p) {
		free(buf);
		set_err(err, cap, "out of memory");
		return NULL;
	}
	p->buf = buf;
	p->pub.page = page;
	p->pub.w = w;
	p->pub.h = h;
	p->pub.src_w = src_w;
	p->pub.src_h = src_h;
	p->pub.ntiles = nt;
	p->pub.tiles = p->tiles;
	for (int i = 0; i < nt; i++) {
		int y = i * LOADER_TILE_MAX;
		p->tiles[i].y = y;
		p->tiles[i].h = h - y < LOADER_TILE_MAX ? h - y : LOADER_TILE_MAX;
		p->tiles[i].rgba = buf + (size_t)y * (size_t)w * 4u;
	}
	return p;
}

/* Factor (<= 1) that brings a w x h RGBA render within budget bytes, with a 2% margin for
 * the renderer's rounding. */
static double budget_factor(double w, double h, size_t budget)
{
	double bytes = w * h * 4.0, room = (double)budget * 0.98;
	return bytes > room ? sqrt(room / bytes) : 1.0;
}

static lpage_t *render_image(lsrc_t *s, int page, int max_w, int max_h, size_t budget,
                             char *err, size_t cap)
{
	uint8_t *data = NULL;
	size_t len = 0;
	int e = s->arc ? arc_read(s->arc, s->idx[page], &data, &len)
	               : folder_read(s->fold, s->idx[page], &data, &len);
	if (e != 0) {
		if (s->arc && arc_error(s->arc)[0])
			snprintf(err, cap, "page read failed: %s", arc_error(s->arc));
		else
			set_err(err, cap, "page read failed");
		return NULL;
	}
	int mw = max_w <= 0 || max_w > LOADER_TILE_MAX ? LOADER_TILE_MAX : max_w, mh = max_h;
	int w, h;
	if (img_probe(data, len, &w, &h) == 0 && w > 0 && h > 0) {
		double sc = fmin(1.0, (double)mw / w); /* the fit img_decode will make */
		if (mh > 0) sc = fmin(sc, (double)mh / h);
		double f = budget_factor(w * sc, h * sc, budget);
		if (f < 1.0) {
			mw = (int)fmax(1.0, floor(w * sc * f));
			mh = (int)fmax(1.0, floor(h * sc * f));
		}
	}
	img_t img;
	int r = img_decode(data, len, mw, mh, &img, err, cap);
	free(data);
	if (r != 0) return NULL;
	return wrap(img.rgba, img.w, img.h, img.src_w, img.src_h, page, err, cap);
}

static lpage_t *render_doc(lsrc_t *s, int page, int max_w, int max_h, size_t budget,
                           char *err, size_t cap)
{
	float wp, hp;
	if (mdoc_page_size(s->doc, page, &wp, &hp) != 0 || wp <= 0 || hp <= 0) {
		set_err(err, cap, "page size unknown");
		return NULL;
	}
	double sc = 1.0;
	if (max_w > 0 && max_h > 0)
		sc = fmin(max_w / wp, max_h / hp);
	else if (max_w > 0)
		sc = max_w / wp;
	else if (max_h > 0)
		sc = max_h / hp;
	if (wp * sc > LOADER_TILE_MAX) sc = LOADER_TILE_MAX / wp;
	sc *= budget_factor(round(wp * sc), round(hp * sc), budget);
	double ow = round(wp * sc), oh = round(hp * sc);
	if (ow < 1 || oh < 1 || ow * oh * 4.0 > IMG_MAX_RGBA_BYTES) {
		set_err(err, cap, "page too large to render");
		return NULL;
	}
	mdoc_image_t img = { 0, 0, NULL };
	if (mdoc_render(s->doc, page, (float)sc, 0, NULL, 0x000000, 0xFFFFFF, &img) != 0 ||
	    !img.rgba) {
		free(img.rgba);
		set_err(err, cap, "page render failed");
		return NULL;
	}
	return wrap(img.rgba, img.w, img.h, (int)lroundf(wp), (int)lroundf(hp), page, err, cap);
}

lpage_t *lsrc_render(lsrc_t *s, int page, int max_w, int max_h, size_t budget, char *err,
                     size_t cap)
{
	if (page < 0 || page >= s->n) {
		set_err(err, cap, "page out of range");
		return NULL;
	}
	lpage_t *p = s->doc ? render_doc(s, page, max_w, max_h, budget, err, cap)
	                    : render_image(s, page, max_w, max_h, budget, err, cap);
	if (p) {
		p->max_w = max_w;
		p->max_h = max_h;
	}
	return p;
}
