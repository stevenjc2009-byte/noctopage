/* bookld.h - background engine for reflowable books (EPUB, MOBI, FB2, TXT, HTML) in the
 * book reader. One worker thread owns the mdoc_t: it opens the book, lays it out to the
 * reader's style, re-lays it out when the style changes (keeping the reader's place),
 * renders wished pages to RGBA in the current theme, and resolves saved positions.
 *
 * Page indices only mean something within one layout GENERATION: every finished
 * (re)layout increments the generation, and the main thread must re-read the page count and
 * jump to the anchor page (bookld_info) when it sees a new one.
 *
 * MAIN THREAD calls everything here; nothing blocks on rendering or layout (short mutex
 * holds only). Pure module: no logging, no vita2d. */
#ifndef NOCT_BOOKLD_H
#define NOCT_BOOKLD_H

#include "doc/mdoc.h"

#include <stddef.h>
#include <stdint.h>

#define BOOKLD_PAGE_W 960 /* every page is rendered at the full screen size */
#define BOOKLD_PAGE_H 544
#define BOOKLD_CACHE_PAGES 6
#define BOOKLD_WANT_MAX 4
#define BOOKLD_TOC_MAX 256

typedef struct bookld bookld_t;

typedef struct {
	int font_px;        /* 12..48 */
	int margin_px;      /* 0..120, all four sides */
	float line_spacing; /* 1.0..2.0 */
	int font_family;    /* 0 = the book's fonts, 1 = sans-serif */
} bookld_style_t;

typedef enum { BOOKLD_OPENING, BOOKLD_READY, BOOKLD_FAILED } bookld_status_t;

typedef struct {
	bookld_status_t status;
	int gen;         /* layout generation, 0 until the first layout is ready */
	int count;       /* pages in this generation */
	int anchor;      /* page to show for this generation: the resumed position after open,
	                    the kept position after a relayout */
	int busy;        /* a relayout is running (the old generation stays readable) */
	int jump_serial; /* increments when a bookld_resolve finished ... */
	int jump_page;   /* ... with this page in the then-current generation (-1 = unparsable) */
	char err[160];   /* BOOKLD_FAILED: why */
} bookld_info_t;

typedef struct {
	int gen, page;
	uint32_t ink, paper;
	int w, h;                 /* BOOKLD_PAGE_W x BOOKLD_PAGE_H */
	uint8_t *rgba;            /* tight RGBA8888 */
	char pos[MDOC_POS_MAX];   /* mdoc_position of this page: save it as the reading place */
} bookld_page_t;

/* Clamp a style into the ranges above. */
void bookld_style_clamp(bookld_style_t *st);

/* Start opening path in the background, laid out with st. resume_pos (may be NULL or "")
 * is an mdoc position string: the first generation's anchor is the page holding it (0 when
 * NULL, empty or unparsable). Returns NULL only on out of memory. */
bookld_t *bookld_open(const char *path, const bookld_style_t *st, const char *resume_pos);
/* Stops the worker (waiting for a render or layout in progress) and frees everything.
 * Pages still held from bookld_get must have been released. */
void bookld_close(bookld_t *b);

void bookld_info(bookld_t *b, bookld_info_t *out);

/* Re-lay out with st in the background, keeping the text at the start of keep_page (a page
 * of the current generation). When it finishes: gen + 1, new count, anchor = the page now
 * holding that text, all cached pages dropped. A second call while one is running replaces
 * the pending style (the latest wins; keep_page of the latest call is used). */
void bookld_set_style(bookld_t *b, const bookld_style_t *st, int keep_page);

/* Replace the wish list: render these pages of generation gen in order (index 0 first) in
 * the given colours, skipping any already cached. Wishes for an old generation are ignored.
 * n is clamped to BOOKLD_WANT_MAX; out-of-range pages are ignored. */
void bookld_want(bookld_t *b, int gen, const int *pages, int n, uint32_t ink, uint32_t paper);

/* A finished page matching (gen, page, ink, paper) exactly, pinned so it cannot be
 * evicted, else NULL. Release it with bookld_release once uploaded. */
const bookld_page_t *bookld_get(bookld_t *b, int gen, int page, uint32_t ink, uint32_t paper);
void bookld_release(bookld_t *b, const bookld_page_t *p);

/* Resolve an mdoc position string (a bookmark) in the background; the answer arrives in
 * bookld_info as jump_serial + 1 and jump_page. */
void bookld_resolve(bookld_t *b, const char *pos);

/* Copy of the table of contents for the current generation (pages resolved for it).
 * Returns items written (<= max), 0 when the book has none or no layout is ready. */
int bookld_toc(bookld_t *b, mdoc_toc_item_t *out, int max);

/* Set when a wished page failed to render: page index and reason, cleared by reading.
 * Returns the page or -1. */
int bookld_take_error(bookld_t *b, char *err, size_t cap);

#endif
