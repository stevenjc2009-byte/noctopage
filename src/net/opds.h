/* opds - OPDS 1.2 (Atom) and OPDS 2.0 (JSON) catalog parsing (pure C, no network, no logging).
 *
 * opds_parse() turns a feed body into an opds_feed. Every string in the feed and its entries is
 * owned by one allocation arena and stays valid until opds_feed_free(). All hrefs are absolute
 * (resolved against the feed URL); links whose target cannot be resolved or that are data: URIs
 * (Project Gutenberg inlines its navigation icons that way) are dropped. Strings are never NULL
 * where a title is expected ("" instead); everything else is NULL when the feed did not say.
 *
 * Source files: opds.c (API, arena, link classification), opds_atom.c (OPDS 1 via net/xml),
 * opds_json.c (OPDS 2 via cJSON), opds_text.c (whitespace/HTML to plain text). opds.c, opds_atom.c
 * and opds_json.c need -I third_party/cjson for "cJSON.h" (opds_json.c only). */
#ifndef NOCTO_OPDS_H
#define NOCTO_OPDS_H

#include <stddef.h>

#define OPDS_MAX_ENTRIES 500       /* entries kept per page; the rest set feed->truncated */
#define OPDS_MAX_FACETS  64
#define OPDS_SUMMARY_MAX 1024      /* bytes, UTF-8 safe */
#define OPDS_MAX_BODY    (4 * 1024 * 1024)

typedef enum {
    OPDS_ACQ_GENERIC,     /* http://opds-spec.org/acquisition */
    OPDS_ACQ_OPEN_ACCESS, /* .../acquisition/open-access */
    OPDS_ACQ_BORROW,
    OPDS_ACQ_BUY,
    OPDS_ACQ_SAMPLE,
    OPDS_ACQ_SUBSCRIBE
} opds_acq_kind;

typedef struct {
    const char *href;  /* absolute */
    const char *type;  /* MIME type as given, may be "" */
    const char *title; /* may be "" */
    long long length;  /* bytes, -1 when unknown */
    opds_acq_kind kind;
} opds_acq;

typedef struct {
    const char *id, *title;
    const char *author;  /* names joined with "; ", NULL when none */
    const char *summary; /* plain text (tags stripped, entities decoded), <= OPDS_SUMMARY_MAX, NULL when none */
    const char *updated; /* as written */
    const char *group;   /* OPDS 2 group title this entry was listed under, else NULL */
    const char *cover;   /* rel .../image */
    const char *thumb;   /* rel .../image/thumbnail */
    const char *nav_href; /* subsection feed to open; non-NULL = a navigation entry */
    opds_acq *acq;       /* acquisition links in document order */
    int nacq;
    const char *pse_href; /* OPDS-PSE page template with {pageNumber} (and maybe {maxWidth}), absolute */
    int pse_count;        /* pse:count; 0 when the entry has no page streaming */
    int acq_cap_;         /* private */
} opds_entry;

typedef struct {
    const char *title, *href, *group;
    int active;
} opds_facet;

typedef struct opds_feed {
    int version; /* 1 = Atom, 2 = JSON */
    const char *title, *id, *subtitle, *updated;
    const char *self, *next, *prev, *up, *start, *first, *last;
    /* Search: when search_is_template the href is a URL template for url_expand_search() (Atom
     * "{searchTerms}" or OPDS 2 "{?query}"); otherwise it is an OpenSearch description document to
     * fetch and give to opds_parse_opensearch(). NULL = the catalog has no search. */
    const char *search_href;
    int search_is_template;
    long long total; /* opensearch:totalResults / metadata.numberOfItems, -1 when unknown */
    opds_entry *entries;
    int nentries;
    int truncated; /* more than OPDS_MAX_ENTRIES entries were dropped */
    opds_facet *facets;
    int nfacets;
    struct opds_arena *arena_;
    int entries_cap_, facets_cap_; /* private */
    const char *base_;             /* private: URL the feed was fetched from */
} opds_feed;

/* Parse a feed. content_type may be NULL; it decides between OPDS 1 and 2 (anything containing
 * "json" = 2, containing "xml" = 1), else the first non-blank byte does ('{' = 2, '<' = 1).
 * base_url is the URL the body was fetched from (after redirects). Returns 0 and *out (free with
 * opds_feed_free), or -1 with a short message in err (may be NULL). Bodies above OPDS_MAX_BODY,
 * malformed or truncated input are errors, never crashes. */
int opds_parse(const char *body, size_t len, const char *content_type, const char *base_url,
               opds_feed **out, char *err, size_t errcap);

void opds_feed_free(opds_feed *f); /* NULL is fine */

/* Read an OpenSearch description document and write the best Atom search template into out
 * (absolute when base_url is given; "{searchTerms}" and optional "{startPage?}" stay in place).
 * Preference: an OPDS catalog Url, then any Atom Url; text/html and suggestion Urls are never
 * used. Returns 0, or -1 (err) when the document has no usable Url. */
int opds_parse_opensearch(const char *body, size_t len, const char *base_url, char *out, size_t cap,
                          char *err, size_t errcap);

/* Lower-case format token of an acquisition link: "epub", "cbz", "cbr", "cb7", "pdf", "mobi",
 * "fb2", "txt", "html", "zip", or "other". Decided by the MIME type, falling back to the href's
 * file extension when the type is generic (application/zip, octet-stream) or empty. */
const char *opds_acq_format(const opds_acq *a);

/* The acquisition link to download. prefs is a NULL-terminated array of format tokens in order of
 * preference (NULL = {"epub", "cbz", "pdf", NULL}). The first preference with a link wins (the
 * first such link in document order); when none matches, the first downloadable link of any
 * format. Only OPDS_ACQ_GENERIC and OPDS_ACQ_OPEN_ACCESS links are candidates (not buy, borrow,
 * sample, subscribe). NULL when there is none. */
const opds_acq *opds_best_acquisition(const opds_entry *e, const char *const *prefs);

/* URL of page `page` (as the server numbers it: see pse:count, usually 0-based) from the entry's
 * PSE template. Pass max_width > 0 when the template has {maxWidth}. 0 or -1 (no PSE / too long). */
int opds_pse_url(const opds_entry *e, int page, int max_width, char *dst, size_t cap);

/* Strip HTML tags, decode entities, collapse whitespace (block tags and <br> become one newline),
 * trim; writes at most cap - 1 bytes (UTF-8 safe) + NUL, returns the length written. */
size_t opds_strip_html(const char *html, char *out, size_t cap);

#endif
