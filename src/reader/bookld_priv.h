/* bookld_priv.h - internals shared by reader/bookld.c (main-thread API, thread start/stop)
 * and reader/bookld_work.c (the worker: open, layout, relayout, render, resolve), plus
 * test-only hooks. Not for UI code: include reader/bookld.h instead.
 *
 * Locking: b->mu guards every field from mu down; the main thread holds it only for short
 * field reads/writes. Lock order is b->mu then the cache's own lock. The worker alone
 * touches the "worker only" fields, with no lock. */
#ifndef NOCT_BOOKLD_PRIV_H
#define NOCT_BOOKLD_PRIV_H

#include "reader/bookld.h"
#include "reader/page_cache.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#define BOOKLD_ERR_CAP 160
/* Cache cost of one page: the pixels plus generous bookkeeping. The cache cap is
 * BOOKLD_CACHE_PAGES of these, so it never holds more pages than that (pins aside). */
#define BOOKLD_PAGE_COST ((size_t)BOOKLD_PAGE_W * BOOKLD_PAGE_H * 4u + 4096u)

/* One cached page: the public struct (rgba is its own malloc) and the cache pin while the
 * main thread holds it. */
typedef struct {
	bookld_page_t pub;
	pc_ref_t *ref; /* set by bookld_get (main thread) while pinned */
} bpage_t;

struct bookld {
	pthread_t thread;
	page_cache_t *cache;
	char *path;
	char resume[MDOC_POS_MAX];

	/* worker only */
	mdoc_t *doc;
	int prev_keep;        /* keep_page (in gen - 1) of the relayout that made this gen, -1 */
	int prev_count;       /* page count of gen - 1 */
	mdoc_toc_item_t *wtoc; /* BOOKLD_TOC_MAX scratch for mdoc_toc */

	pthread_mutex_t mu;
	pthread_cond_t cv;
	int quit, hold;
	bookld_status_t status;
	char err[BOOKLD_ERR_CAP];
	int gen, count, anchor;
	int fixed;           /* fixed layout (PDF ...): styles are ignored, pages fit the screen */
	int busy;            /* a style change is pending or being laid out */
	int jump_serial, jump_page;
	int find_pending, find_start, find_serial, find_page;
	char find_query[BOOKLD_FIND_MAX];
	bookld_style_t style; /* latest requested style */
	int style_pending, keep_page, keep_gen;
	int resolve_pending;
	char resolve_pos[MDOC_POS_MAX];
	int wish_gen, nwish;
	int wish[BOOKLD_WANT_MAX];
	uint32_t ink, paper; /* colours of the wish list */
	int failed[BOOKLD_WANT_MAX]; /* failed wishes still in the list (not retried) */
	int nfailed;
	int err_page; /* -1 = no error to report */
	char page_err[BOOKLD_ERR_CAP];
	mdoc_toc_item_t *toc; /* BOOKLD_TOC_MAX, copied after each layout */
	int ntoc;
	int renders; /* test counter: renders started */
};

/* Cache key: (gen, ink) as the book id, paper as the variant, so every (gen, page, ink,
 * paper) is exact and a new generation is one pc_clear. */
static inline uint64_t bookld_key_book(int gen, uint32_t ink)
{
	return ((uint64_t)(uint32_t)gen << 32) | ink;
}
static inline int bookld_key_variant(uint32_t paper)
{
	return (int)paper;
}

/* The worker thread (bookld_work.c). */
void *bookld_worker(void *arg);
/* Cache free callback: free() only, safe on any thread. */
void bookld_page_free(void *v, void *user);

/* ---- test hooks (host tests only; harmless in the app) ---- */
/* hold = 1: the worker starts no new job (relayout, resolve, render) until hold = 0. */
void bookld_test_hold(bookld_t *b, int hold);
/* Renders started so far. */
int bookld_test_renders(bookld_t *b);
int bookld_test_cache_count(bookld_t *b);
size_t bookld_test_cache_bytes(bookld_t *b);

#endif
