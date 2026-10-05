/* opds_atom - OPDS 1.x (Atom) feed parsing on top of net/xml. */
#include <stdlib.h>
#include <string.h>

#include "opds_priv.h"
#include "xml.h"

#define NS_ATOM "http://www.w3.org/2005/Atom"
#define NS_OPDS "http://opds-spec.org/2010/catalog"
#define NS_OS   "http://a9.com/-/spec/opensearch/1.1/"
#define NS_PSE  "http://vaemendis.net/opds-pse/ns"

typedef enum { C_NONE, C_TITLE, C_ID, C_UPDATED, C_SUBTITLE, C_SUMMARY, C_CONTENT, C_AUTHOR, C_TOTAL } cap_kind;
typedef enum { M_TEXT, M_HTML, M_XHTML } cap_mode;

typedef struct {
    opds_feed *f;
    xml_parser *p;
    opds_entry *cur; /* NULL while outside an entry, or inside one that was dropped */
    int in_entry, in_author, have_summary, root_ok;
    cap_kind cap;
    cap_mode mode;
    int cap_depth;
    opds_tb tb;
    char tbuf[OPDS_SUMMARY_MAX + 1];
    char authors[1024];
    size_t authors_len;
} atom_state;

static int is_ns(const char *ns, const char *want)
{
    return ns && strcmp(ns, want) == 0;
}

/* Local name of the current element, and whether it is in the given namespace (Atom: also when
 * the feed declares none at all). */
static const char *local_of(const atom_state *s, const char *want_ns)
{
    const char *local = NULL, *ns = xml_resolve(s->p, s->p->name, 0, &local);
    if (!local) return NULL;
    if (ns ? !is_ns(ns, want_ns) : strcmp(want_ns, NS_ATOM) != 0) return NULL;
    return local;
}

/* Attribute by namespace + local name; falls back to the literal "pse:" / "opds:" prefix. */
static const char *attr_ns(const xml_parser *p, const char *ns, const char *local, const char *literal)
{
    int i;
    for (i = 0; i < p->nattrs; i++) {
        const char *l = NULL, *ans = xml_resolve(p, p->attrs[i].name, 1, &l);
        if (is_ns(ans, ns) && l && strcmp(l, local) == 0) return p->attrs[i].value;
    }
    return xml_attr_value(p, literal);
}

static void begin_capture(atom_state *s, cap_kind kind, cap_mode mode)
{
    opds_tb_init(&s->tb, s->tbuf, sizeof s->tbuf);
    s->cap = kind;
    s->mode = mode;
    s->cap_depth = s->p->depth;
}

static cap_mode content_mode(const xml_parser *p)
{
    const char *t = xml_attr_value(p, "type");
    /* Servers put HTML into untyped and "text" summaries all the time, so only xhtml is special. */
    if (t && (strcmp(t, "xhtml") == 0 || strcmp(t, "application/xhtml+xml") == 0)) return M_XHTML;
    return M_HTML;
}

static void on_link(atom_state *s)
{
    const xml_parser *p = s->p;
    const char *rel = xml_attr_value(p, "rel"), *href = xml_attr_value(p, "href");
    const char *type = xml_attr_value(p, "type"), *title = xml_attr_value(p, "title");
    const char *abs, *len;
    opds_acq_kind kind = OPDS_ACQ_GENERIC;
    opds_rel r;

    if (!href) return;
    r = opds_classify_rel(rel, &kind);
    if (r == REL_NONE && s->cur && type && strstr(type, "profile=opds-catalog") &&
        (!rel || strcmp(rel, "alternate") == 0))
        r = REL_NAV;
    if (r == REL_NONE) return;
    abs = opds_abs(s->f, href);
    if (!abs) return;
    if (!s->in_entry) {
        if (r == REL_SEARCH) {
            opds_feed_search(s->f, abs, type);
        } else if (r == REL_FACET) {
            const char *grp = attr_ns(p, NS_OPDS, "facetGroup", "opds:facetGroup");
            const char *act = attr_ns(p, NS_OPDS, "activeFacet", "opds:activeFacet");
            opds_add_facet(s->f, opds_dups(s->f, title), abs, opds_dups(s->f, grp),
                           act && strcmp(act, "true") == 0);
        } else {
            opds_feed_link(s->f, r, abs);
        }
    } else if (s->cur) {
        len = xml_attr_value(p, "length");
        if (r == REL_PSE) {
            const char *cnt = attr_ns(p, NS_PSE, "count", "pse:count");
            opds_entry_pse(s->cur, abs, cnt ? atoi(cnt) : 0);
        } else {
            opds_entry_link(s->f, s->cur, r, kind, abs, type, title,
                            len ? strtoll(len, NULL, 10) : -1);
        }
    }
}

static void append_author(atom_state *s, const char *name, size_t n)
{
    size_t need = n + (s->authors_len ? 2 : 0);
    if (n == 0 || s->authors_len + need >= sizeof s->authors) return;
    if (s->authors_len) {
        memcpy(s->authors + s->authors_len, "; ", 2);
        s->authors_len += 2;
    }
    memcpy(s->authors + s->authors_len, name, n);
    s->authors_len += n;
}

static void finish_capture(atom_state *s)
{
    size_t n = opds_tb_finish(&s->tb);
    const char *str = n ? opds_dup(s->f, s->tbuf, n) : NULL;
    opds_feed *f = s->f;
    cap_kind k = s->cap;

    s->cap = C_NONE;
    if (k == C_AUTHOR) {
        append_author(s, s->tbuf, n);
    } else if (k == C_TOTAL) {
        long long v = strtoll(s->tbuf, NULL, 10);
        if (n && v >= 0) f->total = v;
    } else if (!str) {
        return;
    } else if (s->cur) {
        if (k == C_TITLE) s->cur->title = str;
        else if (k == C_ID) s->cur->id = str;
        else if (k == C_UPDATED) s->cur->updated = str;
        else if (k == C_SUMMARY) s->cur->summary = str, s->have_summary = 1;
        else if (k == C_CONTENT && !s->have_summary) s->cur->summary = str;
    } else if (!s->in_entry) {
        if (k == C_TITLE) f->title = str;
        else if (k == C_ID) f->id = str;
        else if (k == C_UPDATED) f->updated = str;
        else if (k == C_SUBTITLE) f->subtitle = str;
    }
}

static int on_start(atom_state *s)
{
    const xml_parser *p = s->p;
    const char *l;

    if (s->cap != C_NONE) {
        const char *local = strrchr(p->name, ':');
        local = local ? local + 1 : p->name;
        if (s->mode == M_XHTML && opds_is_block_tag(local, strlen(local))) opds_tb_block(&s->tb);
        return 0;
    }
    if (p->depth == 1) {
        l = local_of(s, NS_ATOM);
        if (!l || strcmp(l, "feed") != 0) return -1;
        s->root_ok = 1;
    } else if (p->depth == 2) {
        if ((l = local_of(s, NS_ATOM)) != NULL) {
            if (strcmp(l, "title") == 0) begin_capture(s, C_TITLE, M_TEXT);
            else if (strcmp(l, "id") == 0) begin_capture(s, C_ID, M_TEXT);
            else if (strcmp(l, "updated") == 0) begin_capture(s, C_UPDATED, M_TEXT);
            else if (strcmp(l, "subtitle") == 0) begin_capture(s, C_SUBTITLE, M_TEXT);
            else if (strcmp(l, "link") == 0) on_link(s);
            else if (strcmp(l, "entry") == 0) {
                s->in_entry = 1;
                s->cur = opds_add_entry(s->f);
                s->authors_len = 0;
                s->have_summary = 0;
            }
        } else if ((l = local_of(s, NS_OS)) != NULL && strcmp(l, "totalResults") == 0) {
            begin_capture(s, C_TOTAL, M_TEXT);
        }
    } else if (p->depth == 3 && s->cur) {
        if ((l = local_of(s, NS_ATOM)) == NULL) return 0;
        if (strcmp(l, "title") == 0) begin_capture(s, C_TITLE, M_TEXT);
        else if (strcmp(l, "id") == 0) begin_capture(s, C_ID, M_TEXT);
        else if (strcmp(l, "updated") == 0) begin_capture(s, C_UPDATED, M_TEXT);
        else if (strcmp(l, "summary") == 0) begin_capture(s, C_SUMMARY, content_mode(p));
        else if (strcmp(l, "content") == 0) begin_capture(s, C_CONTENT, content_mode(p));
        else if (strcmp(l, "link") == 0) on_link(s);
        else if (strcmp(l, "author") == 0) s->in_author = 1;
    } else if (p->depth == 4 && s->cur && s->in_author) {
        if ((l = local_of(s, NS_ATOM)) != NULL && strcmp(l, "name") == 0) begin_capture(s, C_AUTHOR, M_TEXT);
    }
    return 0;
}

static void on_end(atom_state *s)
{
    const xml_parser *p = s->p;
    if (s->cap != C_NONE) {
        if (p->depth == s->cap_depth) {
            finish_capture(s);
        } else if (s->mode == M_XHTML) {
            const char *local = strrchr(p->name, ':');
            local = local ? local + 1 : p->name;
            if (opds_is_block_tag(local, strlen(local))) opds_tb_block(&s->tb);
        }
    } else if (p->depth == 3 && s->in_author) {
        s->in_author = 0;
    } else if (p->depth == 2 && s->in_entry) {
        if (s->cur && s->authors_len) s->cur->author = opds_dup(s->f, s->authors, s->authors_len);
        s->in_entry = 0;
        s->cur = NULL;
    }
}

int opds_parse_atom(opds_feed *f, char *buf, size_t len, char *err, size_t errcap)
{
    atom_state *s = (atom_state *)calloc(1, sizeof *s);
    xml_parser *p = (xml_parser *)malloc(sizeof *p);
    int rc = -1;

    if (!s || !p) {
        opds_seterr(err, errcap, "out of memory");
        goto done;
    }
    s->f = f;
    s->p = p;
    xml_init(p, buf, len);
    for (;;) {
        xml_event ev = xml_next(p);
        if (ev == XML_ERROR) {
            opds_seterr(err, errcap, p->error);
            goto done;
        }
        if (ev == XML_EOF) break;
        if (ev == XML_START) {
            if (on_start(s) != 0) {
                opds_seterr(err, errcap, "not an Atom feed");
                goto done;
            }
        } else if (ev == XML_END) {
            on_end(s);
        } else if (ev == XML_TEXT && s->cap != C_NONE) {
            if (s->mode == M_HTML) opds_tb_html(&s->tb, p->text);
            else opds_tb_text(&s->tb, p->text, p->text_len);
        }
    }
    if (!s->root_ok) {
        opds_seterr(err, errcap, "not an Atom feed");
        goto done;
    }
    rc = 0;
done:
    free(s);
    free(p);
    return rc;
}
