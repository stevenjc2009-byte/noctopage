/* loader.h - background page decoding for the readers.
 *
 * One loader per open book. Its worker thread opens the book (lists an archive, opens a
 * document), then renders the pages the reader asks for into CPU RGBA, split into
 * horizontal tiles of at most LOADER_TILE_MAX px per side so each tile fits one GPU texture.
 * Finished pages sit in a byte-capped cache (reader/page_cache) until evicted.
 * No psp2 / vita2d code: builds and tests on the host. The main thread uploads tiles.
 *
 * Sources (by db kind, see lib/db.h):
 *   DB_KIND_ARCHIVE, DB_KIND_FOLDER: image pages in comic/pagelist order, decoded with
 *     img/decode (never upscaled: a page smaller than the box comes back at its own size).
 *   DB_KIND_FIXED: document pages via doc/mdoc, rendered at the scale that fits the box
 *     (upscaling allowed), black ink on white paper, no rotation.
 *   DB_KIND_REFLOW: refused in phase 2 (LOADER_FAILED, "reflowable books are not paged").
 *
 * Threads: every function is MAIN THREAD ONLY unless stated; the worker never calls back. */
#ifndef NOCT_LOADER_H
#define NOCT_LOADER_H

#include "doc/mdoc.h"

#include <stddef.h>
#include <stdint.h>

#define LOADER_TILE_MAX 4096           /* vita2d texture limit, both axes */
#define LOADER_CACHE_BYTES (64u << 20) /* decoded pages kept, pinned ones may exceed it */
#define LOADER_WANT_MAX 8              /* requests per loader_want call */
#define LOADER_TOC_MAX 256             /* outline items kept per book */

typedef enum { LOADER_OPENING, LOADER_READY, LOADER_FAILED } loader_status_t;

typedef struct {
	int y, h;      /* rows of the page this tile covers */
	uint8_t *rgba; /* tight RGBA8888, page.w * h * 4 bytes */
} loader_tile_t;

typedef struct {
	int page;
	int w, h;           /* rendered size, w <= LOADER_TILE_MAX */
	int src_w, src_h;   /* natural size: image pixels, or document points */
	int ntiles;         /* ceil(h / LOADER_TILE_MAX), >= 1 */
	loader_tile_t *tiles;
} loader_page_t;

/* A page wanted at a size: the result fits within max_w x max_h keeping its aspect
 * (0 = no limit on that axis; both 0 = natural size). */
typedef struct {
	int page;
	int max_w, max_h;
} loader_req_t;

typedef struct loader loader_t;

/* Start loading a book. Never blocks on the file: opening happens on the worker.
 * Returns NULL only when out of memory or the thread cannot start. */
loader_t *loader_open(const char *path, int db_kind);
/* Stops the worker (an in-flight decode finishes first), frees everything. NULL is fine.
 * Pages still held from loader_get must have been released. */
void loader_close(loader_t *l);

/* LOADER_FAILED fills err (if cap > 0) with the reason. */
loader_status_t loader_status(loader_t *l, char *err, size_t cap);
/* Page count once READY, else 0. */
int loader_page_count(loader_t *l);
/* Natural size of a page if known (it was rendered, or the source knows it without
 * decoding, as documents do). Returns 0 and fills w/h, or -1 when not known yet. */
int loader_page_size(loader_t *l, int page, int *w, int *h);

/* The document outline, copied on the worker right after a successful open (fixed documents
 * via mdoc_toc, at most LOADER_TOC_MAX items; comics and folders have none). Copies up to max
 * items into out and returns how many; 0 when there is none or the loader is not READY.
 * Never waits on rendering. */
int loader_toc(loader_t *l, mdoc_toc_item_t *out, int max);

/* Replace the wish list: the worker renders these in order (index 0 first), skipping any
 * already cached. Earlier wishes not in the new list are dropped unless already started.
 * n is clamped to LOADER_WANT_MAX; n = 0 clears the list. Out-of-range pages are ignored. */
void loader_want(loader_t *l, const loader_req_t *reqs, int n);

/* A finished page matching exactly (page, max_w, max_h), pinned so it cannot be evicted,
 * or NULL when not ready. Release it with loader_release as soon as it is uploaded. */
const loader_page_t *loader_get(loader_t *l, int page, int max_w, int max_h);
void loader_release(loader_t *l, const loader_page_t *p);

/* Set when a wished page failed to render: page index and reason, cleared by reading.
 * Returns the page, or -1 when nothing failed since the last call. Failed (page, size)
 * pairs are not retried until they leave the wish list and come back. */
int loader_take_error(loader_t *l, char *err, size_t cap);

#endif
