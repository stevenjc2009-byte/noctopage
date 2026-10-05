/* opds_build - feed container, arena, link classification and the builders the two format parsers
 * share (see opds_priv.h). The public API is in opds.c. */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "opds_priv.h"
#include "url.h"

#define CHUNK_SIZE (16 * 1024)
#define MAX_ACQ    32

typedef struct opds_chunk {
    struct opds_chunk *next;
    size_t used, cap;
    long long align_; /* keeps the data that follows 8-byte aligned */
} opds_chunk;

struct opds_arena {
    opds_chunk *head;
    int oom;
};

static void *arena_alloc(struct opds_arena *a, size_t n)
{
    opds_chunk *c = a->head;
    n = (n + 7u) & ~(size_t)7u;
    if (!c || c->cap - c->used < n) {
        size_t cap = n > CHUNK_SIZE ? n : CHUNK_SIZE;
        c = (opds_chunk *)malloc(sizeof *c + cap);
        if (!c) {
            a->oom = 1;
            return NULL;
        }
        c->used = 0;
        c->cap = cap;
        /* an oversized chunk goes behind the head so the head keeps its free space */
        if (n > CHUNK_SIZE && a->head) {
            c->next = a->head->next;
            a->head->next = c;
        } else {
            c->next = a->head;
            a->head = c;
        }
    }
    {
        void *p = (char *)(c + 1) + c->used;
        c->used += n;
        return p;
    }
}

static void *feed_alloc(opds_feed *f, size_t n)
{
    return arena_alloc(f->arena_, n);
}

const char *opds_dup(opds_feed *f, const char *s, size_t n)
{
    char *d = (char *)feed_alloc(f, n + 1);
    if (!d) return NULL;
    memcpy(d, s, n);
    d[n] = '\0';
    return d;
}

const char *opds_dups(opds_feed *f, const char *s)
{
    return s ? opds_dup(f, s, strlen(s)) : NULL;
}

opds_feed *opds_feed_new(int version, const char *base_url)
{
    opds_feed *f = (opds_feed *)calloc(1, sizeof *f);
    if (!f) return NULL;
    f->arena_ = (struct opds_arena *)calloc(1, sizeof *f->arena_);
    if (!f->arena_) {
        free(f);
        return NULL;
    }
    f->version = version;
    f->total = -1;
    f->title = "";
    f->base_ = opds_dups(f, base_url);
    return f;
}

void opds_feed_free(opds_feed *f)
{
    opds_chunk *c;
    if (!f) return;
    c = f->arena_->head;
    while (c) {
        opds_chunk *next = c->next;
        free(c);
        c = next;
    }
    free(f->arena_);
    free(f);
}

static int has_prefix_ci(const char *s, const char *prefix)
{
    for (; *prefix; s++, prefix++)
        if (tolower((unsigned char)*s) != *prefix) return 0;
    return 1;
}

/* ASCII case-insensitive substring test; needle must be lower case. */
int opds_contains_ci(const char *s, const char *needle)
{
    for (; *s; s++)
        if (has_prefix_ci(s, needle)) return 1;
    return 0;
}

const char *opds_abs(opds_feed *f, const char *href)
{
    char tmp[URL_MAX];
    if (!href || !*href) return NULL;
    if (url_resolve(tmp, sizeof tmp, f->base_, href) != 0) return NULL;
    if (!has_prefix_ci(tmp, "http://") && !has_prefix_ci(tmp, "https://")) return NULL;
    return opds_dups(f, tmp);
}

void opds_seterr(char *err, size_t errcap, const char *msg)
{
    size_t n;
    if (!err || errcap == 0) return;
    n = strlen(msg);
    if (n >= errcap) n = errcap - 1;
    memcpy(err, msg, n);
    err[n] = '\0';
}

/* ---- link roles ---- */

#define ACQ_NS "http://opds-spec.org/acquisition"
#define SORT_NS "http://opds-spec.org/sort/"

static int is_tok(const char *s, size_t n, const char *lit)
{
    return strlen(lit) == n && strncmp(s, lit, n) == 0;
}

static opds_rel classify_one(const char *s, size_t n, opds_acq_kind *kind)
{
    static const struct { const char *rel; opds_rel role; } EXACT[] = {
        { "self", REL_SELF }, { "next", REL_NEXT }, { "previous", REL_PREV }, { "prev", REL_PREV },
        { "up", REL_UP }, { "start", REL_START }, { "first", REL_FIRST }, { "last", REL_LAST },
        { "search", REL_SEARCH }, { "subsection", REL_NAV },
        { "http://opds-spec.org/facet", REL_FACET },
        { "http://opds-spec.org/image", REL_IMAGE }, { "cover", REL_IMAGE },
        { "x-stanza-cover-image", REL_IMAGE },
        { "http://opds-spec.org/image/thumbnail", REL_THUMB },
        { "http://opds-spec.org/thumbnail", REL_THUMB }, { "thumbnail", REL_THUMB },
        { "x-stanza-cover-image-thumbnail", REL_THUMB },
        { "http://opds-spec.org/featured", REL_NAV }, { "http://opds-spec.org/recommended", REL_NAV },
        { "http://vaemendis.net/opds-pse/stream", REL_PSE },
    };
    static const struct { const char *suffix; opds_acq_kind kind; } ACQ[] = {
        { "/open-access", OPDS_ACQ_OPEN_ACCESS }, { "/borrow", OPDS_ACQ_BORROW },
        { "/buy", OPDS_ACQ_BUY }, { "/sample", OPDS_ACQ_SAMPLE }, { "/preview", OPDS_ACQ_SAMPLE },
        { "/subscribe", OPDS_ACQ_SUBSCRIBE },
    };
    size_t i, an = strlen(ACQ_NS), sn = strlen(SORT_NS);
    for (i = 0; i < sizeof EXACT / sizeof EXACT[0]; i++)
        if (is_tok(s, n, EXACT[i].rel)) return EXACT[i].role;
    if (n >= an && strncmp(s, ACQ_NS, an) == 0) {
        *kind = OPDS_ACQ_GENERIC;
        for (i = 0; i < sizeof ACQ / sizeof ACQ[0]; i++)
            if (is_tok(s + an, n - an, ACQ[i].suffix)) *kind = ACQ[i].kind;
        return REL_ACQ;
    }
    if (n > sn && strncmp(s, SORT_NS, sn) == 0) return REL_NAV;
    return REL_NONE;
}

opds_rel opds_classify_rel(const char *rel, opds_acq_kind *kind)
{
    opds_acq_kind dummy;
    if (!rel) return REL_NONE;
    if (!kind) kind = &dummy;
    *kind = OPDS_ACQ_GENERIC;
    while (*rel) {
        size_t n;
        opds_rel r;
        while (*rel == ' ') rel++;
        n = strcspn(rel, " ");
        r = classify_one(rel, n, kind);
        if (r != REL_NONE) return r;
        rel += n;
    }
    return REL_NONE;
}

/* ---- building ---- */

static void *grow(opds_feed *f, void *old, int count, int *cap, int max, size_t elem, int first)
{
    int ncap = *cap ? *cap * 2 : first;
    void *p;
    if (ncap > max) ncap = max;
    p = feed_alloc(f, (size_t)ncap * elem);
    if (!p) return NULL;
    if (old) memcpy(p, old, (size_t)count * elem);
    *cap = ncap;
    return p;
}

opds_entry *opds_add_entry(opds_feed *f)
{
    opds_entry *e;
    if (f->nentries >= OPDS_MAX_ENTRIES) {
        f->truncated = 1;
        return NULL;
    }
    if (f->nentries >= f->entries_cap_) {
        void *p = grow(f, f->entries, f->nentries, &f->entries_cap_, OPDS_MAX_ENTRIES,
                       sizeof *f->entries, 16);
        if (!p) return NULL;
        f->entries = (opds_entry *)p;
    }
    e = &f->entries[f->nentries++];
    memset(e, 0, sizeof *e);
    e->title = "";
    return e;
}

void opds_feed_link(opds_feed *f, opds_rel r, const char *abs)
{
    const char **slot = NULL;
    if (!abs) return;
    switch (r) {
    case REL_SELF: slot = &f->self; break;
    case REL_NEXT: slot = &f->next; break;
    case REL_PREV: slot = &f->prev; break;
    case REL_UP: slot = &f->up; break;
    case REL_START: slot = &f->start; break;
    case REL_FIRST: slot = &f->first; break;
    case REL_LAST: slot = &f->last; break;
    default: return;
    }
    if (!*slot) *slot = abs;
}

void opds_feed_search(opds_feed *f, const char *abs, const char *type)
{
    int osd = type && opds_contains_ci(type, "opensearchdescription");
    if (!abs) return;
    if (osd) {
        if (!f->search_href) {
            f->search_href = abs;
            f->search_is_template = 0;
        }
    } else if (strchr(abs, '{')) {
        if (!f->search_href || !f->search_is_template) {
            f->search_href = abs;
            f->search_is_template = 1;
        }
    }
}

void opds_add_facet(opds_feed *f, const char *title, const char *abs, const char *group, int active)
{
    opds_facet *fc;
    if (!abs || f->nfacets >= OPDS_MAX_FACETS) return;
    if (f->nfacets >= f->facets_cap_) {
        void *p = grow(f, f->facets, f->nfacets, &f->facets_cap_, OPDS_MAX_FACETS, sizeof *f->facets, 8);
        if (!p) return;
        f->facets = (opds_facet *)p;
    }
    fc = &f->facets[f->nfacets++];
    fc->title = title ? title : "";
    fc->href = abs;
    fc->group = group;
    fc->active = active;
}

void opds_entry_link(opds_feed *f, opds_entry *e, opds_rel r, opds_acq_kind kind, const char *abs,
                     const char *type, const char *title, long long length)
{
    opds_acq *a;
    if (!abs) return;
    switch (r) {
    case REL_IMAGE:
        if (!e->cover) e->cover = abs;
        break;
    case REL_THUMB:
        if (!e->thumb) e->thumb = abs;
        break;
    case REL_NAV:
        if (!e->nav_href) e->nav_href = abs;
        break;
    case REL_ACQ:
        if (e->nacq >= MAX_ACQ) break;
        if (e->nacq >= e->acq_cap_) {
            void *p = grow(f, e->acq, e->nacq, &e->acq_cap_, MAX_ACQ, sizeof *e->acq, 4);
            if (!p) break;
            e->acq = (opds_acq *)p;
        }
        a = &e->acq[e->nacq++];
        a->href = abs;
        a->type = type ? opds_dups(f, type) : "";
        a->title = title ? opds_dups(f, title) : "";
        if (!a->type) a->type = "";
        if (!a->title) a->title = "";
        a->length = length;
        a->kind = kind;
        break;
    default:
        break;
    }
}

void opds_entry_pse(opds_entry *e, const char *abs, int count)
{
    if (!abs || count <= 0 || e->pse_href) return;
    e->pse_href = abs;
    e->pse_count = count;
}


int opds_feed_oom(const opds_feed *f)
{
    return f->arena_->oom;
}
