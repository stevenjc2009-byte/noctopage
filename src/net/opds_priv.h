/* opds_priv - helpers shared by opds.c, opds_atom.c, opds_json.c and opds_text.c. Not public. */
#ifndef NOCTO_OPDS_PRIV_H
#define NOCTO_OPDS_PRIV_H

#include "opds.h"

/* ---- link roles, shared by both formats ---- */
typedef enum {
    REL_NONE, REL_SELF, REL_NEXT, REL_PREV, REL_UP, REL_START, REL_FIRST, REL_LAST,
    REL_SEARCH, REL_FACET, REL_IMAGE, REL_THUMB, REL_NAV, REL_ACQ, REL_PSE
} opds_rel;

/* rel is one IRI, or several separated by spaces (first recognised one wins). *kind is set for
 * REL_ACQ. */
opds_rel opds_classify_rel(const char *rel, opds_acq_kind *kind);

/* ---- feed building (everything is owned by the feed's arena) ---- */
opds_feed *opds_feed_new(int version, const char *base_url);
int opds_feed_oom(const opds_feed *f); /* an allocation failed while building: discard the feed */
const char *opds_dup(opds_feed *f, const char *s, size_t n);
const char *opds_dups(opds_feed *f, const char *s); /* NULL in, NULL out */
/* Resolve href against the feed URL. NULL when it is empty, too long, or not http(s) afterwards
 * (data: URIs, mailto:, relative with no usable base). */
const char *opds_abs(opds_feed *f, const char *href);
/* New entry (title ""), or NULL once OPDS_MAX_ENTRIES is reached (sets f->truncated). */
opds_entry *opds_add_entry(opds_feed *f);
void opds_feed_link(opds_feed *f, opds_rel r, const char *abs); /* SELF..LAST, first wins (SELF too) */
void opds_feed_search(opds_feed *f, const char *abs, const char *type);
void opds_add_facet(opds_feed *f, const char *title, const char *abs, const char *group, int active);
/* REL_IMAGE / REL_THUMB / REL_NAV / REL_ACQ on an entry; abs is already absolute. */
void opds_entry_link(opds_feed *f, opds_entry *e, opds_rel r, opds_acq_kind kind, const char *abs,
                     const char *type, const char *title, long long length);
void opds_entry_pse(opds_entry *e, const char *abs, int count);

/* ---- parsers (opds_atom.c / opds_json.c); buf is the caller's copy, writable, len + 1 bytes ---- */
int opds_parse_atom(opds_feed *f, char *buf, size_t len, char *err, size_t errcap);
int opds_parse_json(opds_feed *f, const char *body, size_t len, char *err, size_t errcap);
void opds_seterr(char *err, size_t errcap, const char *msg);

/* ---- plain-text builder (opds_text.c) ----
 * Collapses whitespace (ASCII, NBSP) to one space, drops invalid UTF-8, never splits a character
 * at the end, writes no leading or trailing separator. */
typedef struct {
    char *out;
    size_t cap, len;
    int sep;  /* pending separator: 0 none, 1 space, 2 newline */
    int full; /* output limit reached */
} opds_tb;

void opds_tb_init(opds_tb *t, char *out, size_t cap);
void opds_tb_text(opds_tb *t, const char *s, size_t n); /* literal text */
void opds_tb_block(opds_tb *t);                         /* paragraph/line break */
void opds_tb_html(opds_tb *t, const char *html);        /* tags stripped, entities decoded */
size_t opds_tb_finish(opds_tb *t);                      /* NUL-terminate, returns length */
int opds_is_block_tag(const char *name, size_t n);       /* p, div, br, li, h1.. (any case) */
int opds_contains_ci(const char *s, const char *needle); /* ASCII case-insensitive; needle lower case */

#endif
