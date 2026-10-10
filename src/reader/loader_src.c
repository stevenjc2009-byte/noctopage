/* loader_src.c - the worker side of reader/loader: open a book (archive, folder, fixed
 * document) and render one page into an lpage_t (one RGBA buffer, tiles pointing into it).
 * Same archive/folder + pagelist + decode pattern as lib/coverimg.c. Worker thread only. */
#include "reader/loader_priv.h"

#include "comic/archive.h"
#include "comic/folder.h"
#include "comic/pagelist.h"
#include "core/fnv.h"
#include "core/paths.h"
#include "doc/mdoc.h"
#include "img/decode.h"
#include "lib/db.h"
#include "lib/formats.h"
#include "net/http.h"
#include "net/url.h"

#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef __vita__
#include <psp2/io/stat.h> /* sceIoMkdir/sceIoRmdir */
#else
#include <unistd.h> /* rmdir */
#endif

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
	} else if (kind == DB_KIND_STREAM) {
		set_err(err, cap, "streams open with lsrc_open_stream");
		return -1;
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

/* ---- streamed pages (OPDS-PSE) -------------------------------------------------------
 * Pages live at per-page URLs expanded from the catalog template and are cached as
 * files under PATH_STREAM_DIR/<fnv(template)>/, so paging back never re-downloads.
 * http_download resumes partial files; the loader's cancel flag aborts a slow page
 * when the wish list moves on (the .part resumes next time). */

/* Ask the server for reader-sized pages: less bytes, still sharp on the Vita screen. */
#define STREAM_MAX_WIDTH 960
/* Biggest single page body accepted, and the whole stream cache across books. */
#define STREAM_PAGE_MAX (32u * 1024u * 1024u)
#define STREAM_CACHE_MAX (512u * 1024u * 1024u)

static void stream_dir(const char *tmpl, char *out, size_t cap)
{
	char hex[17];

	fnv64_hex(fnv64(tmpl, strlen(tmpl)), hex);
	snprintf(out, cap, "%s/%s", PATH_STREAM_DIR, hex);
}

/* mkdir -p for one cache directory (paths_mkdir_p lives in Vita-only paths.c). */
static int stream_mkdir_p(const char *path)
{
	char buf[256];
	size_t len = strlen(path);

	if (len == 0 || len >= sizeof buf) return -1;
	memcpy(buf, path, len + 1);
	for (char *p = buf + 1; *p; p++) {
		int made;
		if (*p != '/') continue;
		*p = '\0';
#ifdef __vita__
		made = sceIoMkdir(buf, 0777);
		if (made < 0 && (unsigned)made != 0x80010011u) return -1; /* not EEXIST */
#else
		made = mkdir(buf, 0777);
		if (made < 0 && errno != EEXIST) return -1;
#endif
		*p = '/';
	}
#ifdef __vita__
	int r = sceIoMkdir(buf, 0777);
	if (r < 0 && (unsigned)r != 0x80010011u) return -1;
#else
	if (mkdir(buf, 0777) < 0 && errno != EEXIST) return -1;
#endif
	return 0;
}

static void stream_rmdir(const char *dir)
{
#ifdef __vita__
	sceIoRmdir(dir);
#else
	rmdir(dir);
#endif
}

static void stream_file(const char *dir, int page, const char *ext, char *out, size_t cap)
{
	snprintf(out, cap, "%s/p%05d.%s", dir, page, ext);
}

/* Delete a flat cache directory (page bodies and partials only). */
static void stream_wipe_dir(const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *e;

	if (!d) return;
	while ((e = readdir(d)) != NULL) {
		char p[600];
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
		remove(p);
	}
	closedir(d);
	stream_rmdir(dir);
}

/* Newest-first mtime ordering helper for the prune below. */
typedef struct {
	char dir[300];
	int64_t mtime;
	uint64_t bytes;
} stream_use_t;

static int use_cmp(const void *aa, const void *bb)
{
	const stream_use_t *a = aa, *b = bb;
	return a->mtime < b->mtime ? -1 : a->mtime > b->mtime ? 1 : 0;
}

/* Drop the least recently written streamed books until the cache fits its cap. */
static void stream_prune(void)
{
	DIR *d = opendir(PATH_STREAM_DIR);
	struct dirent *e;
	stream_use_t *u = NULL;
	int n = 0, cap = 0, i;
	uint64_t total = 0;

	if (!d) return;
	while ((e = readdir(d)) != NULL) {
		char sub[300];
		DIR *sd;
		struct dirent *se;
		int64_t mt = 0;
		uint64_t bytes = 0;
		struct stat st;

		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		snprintf(sub, sizeof sub, "%s/%s", PATH_STREAM_DIR, e->d_name);
		sd = opendir(sub);
		if (!sd) continue;
		while ((se = readdir(sd)) != NULL) {
			char p[600];
			if (!strcmp(se->d_name, ".") || !strcmp(se->d_name, "..")) continue;
			snprintf(p, sizeof p, "%s/%s", sub, se->d_name);
			if (stat(p, &st) == 0 && S_ISREG(st.st_mode)) {
				bytes += (uint64_t)st.st_size;
				if ((int64_t)st.st_mtime > mt) mt = (int64_t)st.st_mtime;
			}
		}
		closedir(sd);
		if (n == cap) {
			int ncap = cap ? cap * 2 : 16;
			stream_use_t *nu = realloc(u, (size_t)ncap * sizeof *nu);
			if (!nu) break;
			u = nu;
			cap = ncap;
		}
		snprintf(u[n].dir, sizeof u[n].dir, "%s", sub);
		u[n].mtime = mt;
		u[n].bytes = bytes;
		n++;
		total += bytes;
	}
	closedir(d);
	if (n > 1) qsort(u, (size_t)n, sizeof *u, use_cmp);
	for (i = 0; i < n && total > STREAM_CACHE_MAX; i++) {
		stream_wipe_dir(u[i].dir);
		total -= total >= u[i].bytes ? u[i].bytes : total;
	}
	free(u);
}

/* Abort the fetch when the loader moved on, or when the page is absurdly large. */
static int stream_progress(void *ud, int64_t now, int64_t total)
{
	lsrc_t *s = ud;

	(void)total;
	if (now > (int64_t)STREAM_PAGE_MAX) return 1;
	return s->cancel && *s->cancel;
}

static int read_file(const char *path, uint8_t **out, size_t *len, char *err, size_t cap)
{
	FILE *f = fopen(path, "rb");
	long n;

	if (!f) {
		if (cap > 0) snprintf(err, cap, "cannot read cached page");
		return -1;
	}
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		if (cap > 0) snprintf(err, cap, "cannot read cached page");
		return -1;
	}
	n = ftell(f);
	if (n <= 0 || (uint64_t)n > STREAM_PAGE_MAX) {
		fclose(f);
		if (cap > 0) snprintf(err, cap, "cached page has a bad size");
		return -1;
	}
	size_t m = (size_t)n;
	uint8_t *buf = malloc(m + 1);
	if (!buf || fseek(f, 0, SEEK_SET) != 0 || fread(buf, 1, m, f) != m) {
		free(buf);
		fclose(f);
		if (cap > 0) snprintf(err, cap, "cannot read cached page");
		return -1;
	}
	buf[m] = '\0';
	*out = buf;
	*len = m;
	fclose(f);
	return 0;
}

/* Page bytes: the cache file when present, else one resumable download of the page URL. */
static int stream_fetch(lsrc_t *s, int page, uint8_t **data, size_t *len, char *err,
                        size_t cap)
{
	char dir[128], file[160], part[168], url[HTTP_URL_MAX];
	struct stat st;

	stream_dir(s->tmpl, dir, sizeof dir);
	stream_file(dir, page, "bin", file, sizeof file);
	stream_file(dir, page, "part", part, sizeof part);
	if (stat(file, &st) == 0 && st.st_size > 0)
		return read_file(file, data, len, err, cap);
	if (url_expand_page(url, sizeof url, s->tmpl, page, STREAM_MAX_WIDTH) != 0) {
		if (cap > 0) snprintf(err, cap, "bad page address");
		return -1;
	}
	if (stream_mkdir_p(dir) < 0) {
		if (cap > 0) snprintf(err, cap, "cannot cache streamed pages");
		return -1;
	}
	http_auth_t a = { s->user[0] ? s->user : NULL, s->pass[0] ? s->pass : NULL };
	int64_t resume = 0;
	if (stat(part, &st) == 0 && st.st_size > 0) resume = (int64_t)st.st_size;
	if (http_download(url, &a, part, resume, stream_progress, s, NULL, 0, err, cap) != 0)
		return -1;
	if (rename(part, file) != 0) {
		remove(part);
		if (cap > 0) snprintf(err, cap, "cannot cache streamed page");
		return -1;
	}
	return read_file(file, data, len, err, cap);
}

int lsrc_open_stream(lsrc_t *s, const char *tmpl, int count, const char *user,
                     const char *pass, volatile int *cancel, char *err, size_t cap)
{
	memset(s, 0, sizeof *s);
	s->kind = DB_KIND_STREAM;
	if (!tmpl || !tmpl[0] || strlen(tmpl) >= sizeof s->tmpl || count <= 0) {
		if (cap > 0) snprintf(err, cap, "bad stream");
		return -1;
	}
	snprintf(s->tmpl, sizeof s->tmpl, "%s", tmpl);
	snprintf(s->user, sizeof s->user, "%s", user ? user : "");
	snprintf(s->pass, sizeof s->pass, "%s", pass ? pass : "");
	s->cancel = cancel;
	s->n = count;
	stream_prune();
	return count;
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

/* Decode page bytes into a tile-wrapped page, shrinking to the box and budget. */
static lpage_t *render_bytes(uint8_t *data, size_t len, int page, int max_w, int max_h,
                             size_t budget, char *err, size_t cap)
{
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
	return render_bytes(data, len, page, max_w, max_h, budget, err, cap);
}

static lpage_t *render_stream(lsrc_t *s, int page, int max_w, int max_h, size_t budget,
                              char *err, size_t cap)
{
	uint8_t *data = NULL;
	size_t len = 0;
	if (stream_fetch(s, page, &data, &len, err, cap) != 0) return NULL;
	return render_bytes(data, len, page, max_w, max_h, budget, err, cap);
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
	                    : s->kind == DB_KIND_STREAM
	                        ? render_stream(s, page, max_w, max_h, budget, err, cap)
	                        : render_image(s, page, max_w, max_h, budget, err, cap);
	if (p) {
		p->max_w = max_w;
		p->max_h = max_h;
	}
	return p;
}
