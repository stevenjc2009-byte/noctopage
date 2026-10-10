/* mdoc: the one wrapper all document reading goes through (MuPDF underneath).
 *
 * Reflowable: EPUB, MOBI, FB2, TXT, HTML/XHTML. Fixed layout: PDF, XPS, OXPS, SVG (and
 * anything else MuPDF opens). No MuPDF types appear here, so UI code never includes fitz.h.
 *
 * Threads: call mdoc_global_init() once before anything else. Each mdoc_t owns its own MuPDF
 * context (cloned from the global one), so one document may be used by ONE thread at a time,
 * and different documents may be used on different threads at the same time.
 *
 * Every function catches MuPDF errors; nothing throws or aborts. Functions returning int give
 * -1 on error unless stated otherwise. Pure module: no logging.
 */
#ifndef NOCTO_MDOC_H
#define NOCTO_MDOC_H

#include <stddef.h>
#include <stdint.h>

#define MDOC_POS_MAX 64

typedef struct mdoc mdoc_t;

/* Reflow settings. Units are output pixels at render scale 1.0. */
typedef struct {
    int page_w, page_h;   /* page box, margins included */
    float font_px;        /* base font size (the layout "em") */
    float margin_px;      /* page margin on all four sides */
    float line_spacing;   /* 1.0 .. 2.0, clamped */
    int font_family;      /* 0 = the book's own fonts (MuPDF default serif), 1 = force sans-serif */
} mdoc_layout_t;

typedef struct {
    int x, y, w, h;
} mdoc_rect_t;

/* Tight RGBA8888 (stride = w * 4), allocated with malloc: the caller calls free(rgba). */
typedef struct {
    int w, h;
    uint8_t *rgba;
} mdoc_image_t;

typedef struct {
    char title[128];
    int depth;            /* 0 = top level */
    int page;             /* page index in the CURRENT layout, -1 if unresolved */
} mdoc_toc_item_t;

/* Base context (real pthread locks, 32 MiB store) + document handlers. Returns 0 or -1.
 * Calling it again while initialised is a no-op returning 0. */
int mdoc_global_init(void);
/* Drops the base context. Every mdoc_t must be closed first. */
void mdoc_global_shutdown(void);

/* The default layout used by mdoc_open for reflowable documents (360x540, 16 px, ...). */
void mdoc_layout_default(mdoc_layout_t *out);

/* Opens any supported file. On failure returns NULL and writes a message to err (if errcap
 * > 0). Fixed-layout documents with zero pages are rejected. */
mdoc_t *mdoc_open(const char *path, char *err, size_t errcap);
void mdoc_close(mdoc_t *doc);

int mdoc_is_reflowable(const mdoc_t *doc);

/* Metadata (info:Title / info:Author). Copies into out (always NUL-terminated when cap > 0)
 * and returns the length, 0 when absent. Falling back to the file name is the caller's job. */
int mdoc_title(mdoc_t *doc, char *out, size_t cap);
int mdoc_author(mdoc_t *doc, char *out, size_t cap);

/* Applies reflow settings. Font family, margin and line spacing are user CSS, which MuPDF only
 * reads when a document is parsed, so a CSS change reopens the file transparently; a size-only
 * change just re-lays out. No-op (returns 0) for fixed-layout documents. Page indices change:
 * to keep the reader's place, do
 *     mdoc_position(doc, cur, pos); mdoc_set_layout(doc, &s);
 *     cur = mdoc_page_for_position(doc, pos);
 * On failure (e.g. the file vanished before a reopen) the document keeps its previous layout
 * and stays usable. */
int mdoc_set_layout(mdoc_t *doc, const mdoc_layout_t *s);

int mdoc_page_count(mdoc_t *doc);

/* Reading positions: opaque NUL-terminated strings (< MDOC_POS_MAX bytes) that survive
 * re-layout, reopening and app restarts. Fixed layout: the page number. Reflowable: chapter +
 * count of visible characters before the page's first character, so the restored page is the
 * one holding the same text. mdoc_position returns 0 or -1; mdoc_page_for_position returns
 * the page index in the current layout or -1 for an unparsable string. */
int mdoc_position(mdoc_t *doc, int page, char out[MDOC_POS_MAX]);
int mdoc_page_for_position(mdoc_t *doc, const char *pos);

/* Page size in points (unrotated). For reflowable documents this is the layout page size. */
int mdoc_page_size(mdoc_t *doc, int page, float *w_pt, float *h_pt);

/* Renders a page at `scale`, rotated clockwise by `rotate` (0, 90, 180, 270). The full output
 * is round(w*scale) x round(h*scale) pixels (swapped for 90/270). `clip` (output pixels, NULL
 * = everything) selects a sub-rectangle; it is intersected with the page and the result has
 * the intersection's size, pixel-identical to that crop of a full render.
 *
 * Themes: every pixel is mapped per channel from black..white to ink..paper (0xRRGGBB), so
 * black text becomes ink and white paper becomes paper; anti-aliasing and grey vectors land
 * in between. Raster images in the page (photos, illustrations, scanned pages) are NOT mapped:
 * their bounding rectangles keep the image's own colours, so photos are never inverted on a
 * black theme. Consequence: a scanned PDF (each page one image) looks the same in every theme.
 * Images drawn as stencil masks (one-colour) are ink and are mapped. Returns 0 or -1. */
int mdoc_render(mdoc_t *doc, int page, float scale, int rotate, const mdoc_rect_t *clip,
                uint32_t ink_rgb, uint32_t paper_rgb, mdoc_image_t *out);

/* Thumbnail helper: opens `path`, lays reflowable books out at 360x540 (so page 0 is the
 * cover page when the book has one), renders page 0 to fit inside max_w x max_h keeping the
 * aspect ratio, closes. Thread-safe like any other mdoc call. Returns 0 or -1. */
int mdoc_render_cover(const char *path, int max_w, int max_h, mdoc_image_t *out);

/* Flattened outline (depth-first), pages resolved for the current layout. Returns the number
 * of items written (<= max), 0 when the document has no outline, -1 on error. */
int mdoc_toc(mdoc_t *doc, mdoc_toc_item_t *out, int max);

/* First page at or after start whose extracted text contains query (ASCII
 * case-insensitive), or -1 when nothing matches. Empty query and errors give -1.
 * Scanned pages (no text layer) never match. */
int mdoc_find_page(mdoc_t *d, int start, const char *query);

#endif
