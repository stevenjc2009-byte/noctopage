#include "lib/coverimg.h"

#include "comic/archive.h"
#include "comic/folder.h"
#include "comic/pagelist.h"
#include "doc/mdoc.h"
#include "img/decode.h"
#include "img/scale.h"
#include "lib/db.h"
#include "lib/formats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRY_PAGES 3 /* a broken first image falls through to the next ones */

static void set_err(char *err, size_t cap, const char *msg)
{
	if (cap > 0) snprintf(err, cap, "%s", msg);
}

/* Decode compressed image bytes and cover-crop them into out. */
static int from_bytes(const uint8_t *data, size_t len, uint8_t *out, char *err, size_t cap)
{
	int w, h;
	if (img_probe(data, len, &w, &h) != 0 || w <= 0 || h <= 0) {
		set_err(err, cap, "not an image");
		return -1;
	}
	/* Smallest decode that still covers the thumbnail on both axes. */
	double sx = (double)COVER_W / w, sy = (double)COVER_H / h, s = sx > sy ? sx : sy;
	int mw = (int)(w * s + 0.999), mh = (int)(h * s + 0.999);
	img_t img;
	if (img_decode(data, len, mw, mh, &img, err, cap) != 0) return -1;
	int r = img_cover_crop(img.rgba, img.w, img.h, out, COVER_W, COVER_H);
	img_free(&img);
	if (r != 0) set_err(err, cap, "scale failed");
	return r == 0 ? 0 : -1;
}

/* Source of numbered entries: an archive or a folder behind one shape. */
typedef struct {
	void *h;
	const char *(*name)(void *h, int i);
	int (*read)(void *h, int i, uint8_t **out, size_t *len);
} pages_t;

static const char *arc_name_v(void *h, int i) { return arc_name(h, i); }
static int arc_read_v(void *h, int i, uint8_t **o, size_t *l) { return arc_read(h, i, o, l); }
static const char *fold_name_v(void *h, int i) { return folder_name(h, i); }
static int fold_read_v(void *h, int i, uint8_t **o, size_t *l) { return folder_read(h, i, o, l); }

/* First decodable page of n entries in reading order. */
static int from_pages(const pages_t *p, int n, uint8_t *out, char *err, size_t cap)
{
	if (n <= 0) {
		set_err(err, cap, "no pages");
		return -1;
	}
	const char **names = malloc(sizeof *names * (size_t)n);
	int *idx = malloc(sizeof *idx * (size_t)n);
	int r = -1;
	if (!names || !idx) {
		set_err(err, cap, "out of memory");
		goto done;
	}
	for (int i = 0; i < n; i++) names[i] = p->name(p->h, i);
	int np = pagelist_build(names, n, idx);
	if (np == 0 && cap) { /* name the first entry: shows a name-encoding problem at a glance */
		char hex[40] = "";
		const char *f = names[0] ? names[0] : "";
		for (int j = 0; f[j] && j < 12; j++)
			snprintf(hex + 3 * j, sizeof hex - 3 * (size_t)j, "%02x ", (unsigned char)f[j]);
		snprintf(err, cap, "no image pages (%d entries, first: %s)", n, hex);
	}
	for (int k = 0; k < np && k < TRY_PAGES && r != 0; k++) {
		uint8_t *data;
		size_t len;
		if (p->read(p->h, idx[k], &data, &len) != 0) {
			set_err(err, cap, "page read failed");
			continue;
		}
		r = from_bytes(data, len, out, err, cap);
		free(data);
	}
done:
	free(names);
	free(idx);
	return r;
}

static int from_document(const char *path, uint8_t *out, char *err, size_t cap)
{
	mdoc_image_t img = { 0, 0, NULL };
	/* Contain in a box wider and taller than the thumbnail, then cover-crop. */
	if (mdoc_render_cover(path, COVER_W * 3, COVER_H * 2, &img) != 0 || !img.rgba) {
		set_err(err, cap, "document cover render failed");
		return -1;
	}
	int r = img_cover_crop(img.rgba, img.w, img.h, out, COVER_W, COVER_H);
	free(img.rgba);
	if (r != 0) set_err(err, cap, "scale failed");
	return r == 0 ? 0 : -1;
}

int coverimg_make(const char *path, int db_kind, uint8_t *out, char *err, size_t errcap)
{
	if (!path || !out) {
		set_err(err, errcap, "bad arguments");
		return -1;
	}
	if (db_kind == DB_KIND_ARCHIVE) {
		archive_t *a = arc_open(path, err, errcap);
		if (!a) return -1;
		pages_t p = { a, arc_name_v, arc_read_v };
		int r = from_pages(&p, arc_count(a), out, err, errcap);
		arc_close(a);
		return r;
	}
	if (db_kind == DB_KIND_FOLDER) {
		folder_t *f = folder_open(path, formats_is_image, err, errcap);
		if (!f) return -1;
		pages_t p = { f, fold_name_v, fold_read_v };
		int r = from_pages(&p, folder_count(f), out, err, errcap);
		folder_close(f);
		return r;
	}
	if (db_kind == DB_KIND_REFLOW || db_kind == DB_KIND_FIXED)
		return from_document(path, out, err, errcap);
	set_err(err, errcap, "unknown kind");
	return -1;
}

int coverimg_save(const char *file, const uint8_t *rgba)
{
	uint8_t hdr[8] = { 'N', 'T', 'H', '1', COVER_W & 0xFF, COVER_W >> 8, COVER_H & 0xFF, COVER_H >> 8 };
	FILE *f = fopen(file, "wb");
	if (!f) return -1;
	int ok = fwrite(hdr, 1, sizeof hdr, f) == sizeof hdr && fwrite(rgba, 1, COVER_BYTES, f) == COVER_BYTES;
	if (fclose(f) != 0) ok = 0;
	if (!ok) remove(file); /* never leave a short file that load would reject forever */
	return ok ? 0 : -1;
}

int coverimg_load(const char *file, uint8_t *rgba)
{
	FILE *f = fopen(file, "rb");
	if (!f) return -1;
	uint8_t hdr[8];
	int ok = fread(hdr, 1, sizeof hdr, f) == sizeof hdr && memcmp(hdr, "NTH1", 4) == 0 &&
	         (hdr[4] | hdr[5] << 8) == COVER_W && (hdr[6] | hdr[7] << 8) == COVER_H &&
	         fread(rgba, 1, COVER_BYTES, f) == COVER_BYTES && fgetc(f) == EOF;
	fclose(f);
	return ok ? 0 : -1;
}
