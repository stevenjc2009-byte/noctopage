/* mdoc internals shared by src/doc/mdoc*.c (and the host test). Includes fitz.h: never include
 * this from UI code, use mdoc.h. */
#ifndef NOCTO_MDOC_INTERNAL_H
#define NOCTO_MDOC_INTERNAL_H

#include "doc/mdoc.h"

#include <mupdf/fitz.h>

struct mdoc {
    fz_context *ctx;          /* this document's clone of the base context */
    fz_document *doc;         /* NULL after a failed reopen: every call then fails */
    char *path;
    int reflow;
    mdoc_layout_t layout;
    char css[512];            /* user CSS this document was parsed with */
    int npages;               /* cached page count, -1 = unknown */
    int *chars;               /* per page: visible characters, -1 = unknown (reflow only) */
    int dl_page;              /* one-entry display-list cache, -1 = empty */
    fz_display_list *dl;
    fz_rect dl_bounds;
};

/* MuPDF keeps the user stylesheet in a style context SHARED by every cloned context, and reads
 * it whenever HTML is parsed: at open for FB2/TXT/HTML/MOBI, lazily per chapter for EPUB (any
 * page load, page count or bounds query). So for reflowable documents every such call runs
 * under one global lock, with that document's CSS installed first. Fixed-layout documents
 * never take it. lock/unlock are no-ops for fixed-layout documents. */
void mdoc__lock(mdoc_t *d);
void mdoc__unlock(mdoc_t *d);

/* Forgets the page count, character counts and display list (after a layout change). */
void mdoc__drop_caches(mdoc_t *d);

/* Cached page count; -1 on error (message is lost: callers only need failure). */
int mdoc__count(mdoc_t *d);

/* Writes the page's text (UTF-8, lines separated by '\n') into out. Used by the position code
 * and by the host test to prove a restored position shows the same text. Returns the number
 * of visible (counted) characters on the page, or -1. out may be NULL. */
int mdoc__page_text(mdoc_t *d, int page, char *out, size_t cap);

/* Copies a message into a caller buffer (NUL-terminated when cap > 0). */
void mdoc__set_err(char *err, size_t cap, const char *msg);

#endif
