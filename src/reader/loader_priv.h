/* loader_priv.h - internals shared by reader/loader.c (threads, wish list, cache) and
 * reader/loader_src.c (opening a book, rendering one page), plus test-only hooks.
 * Not for UI code: include reader/loader.h instead. */
#ifndef NOCT_LOADER_PRIV_H
#define NOCT_LOADER_PRIV_H

#include "reader/loader.h"
#include "reader/page_cache.h"

#include <stddef.h>
#include <stdint.h>

typedef struct archive_t archive_t;
typedef struct folder_t folder_t;

/* An open book, used by the worker thread only. */
typedef struct {
	int kind;       /* DB_KIND_* */
	archive_t *arc; /* DB_KIND_ARCHIVE */
	folder_t *fold; /* DB_KIND_FOLDER */
	int *idx;       /* comics: page -> entry index (pagelist order) */
	mdoc_t *doc;    /* DB_KIND_FIXED */
	int n;          /* page count */
} lsrc_t;

/* One cached page: the public struct, the single RGBA buffer every tile points into, the
 * exact request it answers, and the cache pin while the main thread holds it. */
typedef struct {
	loader_page_t pub;
	uint8_t *buf;
	int max_w, max_h; /* normalised request (see loader_norm) */
	pc_ref_t *ref;    /* set by loader_get (main thread) while pinned */
	loader_tile_t tiles[];
} lpage_t;

/* Request sizes are normalised before keying: negative -> 0, above 0xFFFF -> 0xFFFF, so the
 * cache variant (max_w << 16 | max_h) is exact (no two requests share a key). */
static inline int loader_norm(int v)
{
	return v < 0 ? 0 : v > 0xFFFF ? 0xFFFF : v;
}
static inline int loader_variant(int max_w, int max_h)
{
	return (int)(((uint32_t)loader_norm(max_w) << 16) | (uint32_t)loader_norm(max_h));
}

/* Open the book: returns the page count (> 0) or -1 with a message in err. */
int lsrc_open(lsrc_t *s, const char *path, int kind, char *err, size_t cap);
void lsrc_close(lsrc_t *s);
/* Documents only: page size in points, rounded. 0 or -1. */
int lsrc_doc_size(lsrc_t *s, int page, int *w, int *h);
/* Render page at the normalised box, shrunk further (aspect kept) when its pixels would
 * exceed budget bytes, so a page always fits the cache. NULL with a message in err. */
lpage_t *lsrc_render(lsrc_t *s, int page, int max_w, int max_h, size_t budget, char *err,
                     size_t cap);
/* Cache free callback: free() only, safe on any thread. */
void lpage_free(void *v, void *user);
size_t lpage_cost(const lpage_t *p);

/* ---- test hooks (host tests only; harmless in the app) ---- */
/* loader_open with a cache cap other than LOADER_CACHE_BYTES. */
loader_t *loader_open_cap(const char *path, int db_kind, size_t cache_bytes);
/* hold = 1: the worker starts no new render until hold = 0 (an in-flight one finishes). */
void loader_test_hold(loader_t *l, int hold);
/* Renders started for page (any size), counted when the worker takes the wish. */
int loader_test_renders(loader_t *l, int page);
/* 1 while the worker is rendering. */
int loader_test_busy(loader_t *l);
size_t loader_test_cache_bytes(loader_t *l);
int loader_test_cache_count(loader_t *l);

#endif
