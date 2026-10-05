/* opds_json - OPDS 2.0 (JSON, Readium web publication manifest style) feed parsing via cJSON. */
#include <string.h>

#include "cJSON.h"
#include "opds_priv.h"

static const cJSON *jget(const cJSON *o, const char *key)
{
    return cJSON_IsObject(o) ? cJSON_GetObjectItemCaseSensitive(o, key) : NULL;
}

static const char *jstr(const cJSON *o, const char *key)
{
    const cJSON *v = jget(o, key);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : NULL;
}

/* Role of a link object; rel may be a string or an array of strings. */
static opds_rel link_rel(const cJSON *link, opds_acq_kind *kind)
{
    const cJSON *rel = jget(link, "rel"), *it;
    opds_rel r = REL_NONE;
    *kind = OPDS_ACQ_GENERIC;
    if (cJSON_IsString(rel)) return opds_classify_rel(rel->valuestring, kind);
    if (cJSON_IsArray(rel))
        cJSON_ArrayForEach(it, rel)
            if (cJSON_IsString(it) && (r = opds_classify_rel(it->valuestring, kind)) != REL_NONE) break;
    return r;
}

typedef struct {
    char buf[1024];
    size_t len;
} joined;

static void join_add(joined *j, const char *name)
{
    size_t n = name ? strlen(name) : 0, need = n + (j->len ? 2 : 0);
    if (n == 0 || j->len + need >= sizeof j->buf) return;
    if (j->len) {
        memcpy(j->buf + j->len, "; ", 2);
        j->len += 2;
    }
    memcpy(j->buf + j->len, name, n + 1);
    j->len += n;
}

/* author: "Name" | {"name": "Name"} | an array of those */
static void add_person(joined *j, const cJSON *a)
{
    const cJSON *it;
    if (cJSON_IsString(a)) join_add(j, a->valuestring);
    else if (cJSON_IsObject(a)) join_add(j, jstr(a, "name"));
    else if (cJSON_IsArray(a))
        cJSON_ArrayForEach(it, a) if (!cJSON_IsArray(it)) add_person(j, it);
}

static double jnum(const cJSON *o, const char *key)
{
    const cJSON *v = jget(o, key);
    return cJSON_IsNumber(v) ? v->valuedouble : 0;
}

static void entry_images(opds_feed *f, opds_entry *e, const cJSON *images)
{
    const cJSON *im;
    const char *big = NULL, *small = NULL;
    double big_area = -1, small_area = 0;
    cJSON_ArrayForEach(im, images)
    {
        const char *abs = opds_abs(f, jstr(im, "href"));
        double area = jnum(im, "width") * jnum(im, "height");
        if (!abs) continue;
        if (!big || area > big_area) {
            big = abs;
            big_area = area;
        }
        if (area > 0 && (!small || area < small_area)) {
            small = abs;
            small_area = area;
        }
    }
    opds_entry_link(f, e, REL_IMAGE, OPDS_ACQ_GENERIC, big, NULL, NULL, -1);
    if (small && small != big && small_area < big_area)
        opds_entry_link(f, e, REL_THUMB, OPDS_ACQ_GENERIC, small, NULL, NULL, -1);
}

static void entry_links(opds_feed *f, opds_entry *e, const cJSON *links)
{
    const cJSON *l;
    cJSON_ArrayForEach(l, links)
    {
        opds_acq_kind kind;
        opds_rel r = link_rel(l, &kind);
        const char *abs;
        if (r == REL_NONE) continue;
        abs = opds_abs(f, jstr(l, "href"));
        if (r == REL_PSE) {
            const cJSON *props = jget(l, "properties");
            opds_entry_pse(e, abs, (int)jnum(props, "numberOfItems") + (int)jnum(props, "pse:count"));
        } else {
            opds_entry_link(f, e, r, kind, abs, jstr(l, "type"), jstr(l, "title"), -1);
        }
    }
}

static void add_publication(opds_feed *f, const cJSON *pub, const char *group)
{
    opds_entry *e = cJSON_IsObject(pub) ? opds_add_entry(f) : NULL;
    const cJSON *md = jget(pub, "metadata");
    const char *s;
    joined j;
    if (!e) return;
    j.len = 0;
    j.buf[0] = '\0';
    s = jstr(md, "title");
    if (s && *s) e->title = opds_dups(f, s);
    e->id = opds_dups(f, jstr(md, "identifier"));
    s = jstr(md, "modified");
    e->updated = opds_dups(f, s ? s : jstr(md, "published"));
    e->group = group;
    add_person(&j, jget(md, "author"));
    if (j.len) e->author = opds_dup(f, j.buf, j.len);
    s = jstr(md, "description");
    if (s) {
        char text[OPDS_SUMMARY_MAX + 1];
        if (opds_strip_html(s, text, sizeof text) > 0) e->summary = opds_dups(f, text);
    }
    entry_links(f, e, jget(pub, "links"));
    entry_images(f, e, jget(pub, "images"));
    if (!e->title) e->title = "";
}

static void add_navigation(opds_feed *f, const cJSON *nav, const char *group)
{
    const cJSON *n;
    cJSON_ArrayForEach(n, nav)
    {
        const char *abs = opds_abs(f, jstr(n, "href"));
        const char *title = jstr(n, "title");
        opds_entry *e;
        if (!abs || !(e = opds_add_entry(f))) continue;
        e->title = title ? opds_dups(f, title) : "";
        e->id = abs;
        e->nav_href = abs;
        e->group = group;
    }
}

static void add_publications(opds_feed *f, const cJSON *pubs, const char *group)
{
    const cJSON *p;
    cJSON_ArrayForEach(p, pubs) add_publication(f, p, group);
}

static void feed_links(opds_feed *f, const cJSON *links)
{
    const cJSON *l;
    cJSON_ArrayForEach(l, links)
    {
        opds_acq_kind kind;
        opds_rel r = link_rel(l, &kind);
        const char *abs = r == REL_NONE ? NULL : opds_abs(f, jstr(l, "href"));
        if (r == REL_SEARCH) opds_feed_search(f, abs, jstr(l, "type"));
        else opds_feed_link(f, r, abs);
    }
}

static void add_facets(opds_feed *f, const cJSON *facets)
{
    const cJSON *fc, *l;
    cJSON_ArrayForEach(fc, facets)
    {
        const char *group = opds_dups(f, jstr(jget(fc, "metadata"), "title"));
        cJSON_ArrayForEach(l, jget(fc, "links"))
        {
            opds_acq_kind kind;
            const char *title = jstr(l, "title");
            opds_add_facet(f, title ? opds_dups(f, title) : NULL, opds_abs(f, jstr(l, "href")), group,
                           link_rel(l, &kind) == REL_SELF);
        }
    }
}

int opds_parse_json(opds_feed *f, const char *body, size_t len, char *err, size_t errcap)
{
    cJSON *root = cJSON_ParseWithLength(body, len);
    const cJSON *md, *g, *v;
    const char *s;

    if (!root) {
        opds_seterr(err, errcap, "malformed JSON");
        return -1;
    }
    if (!cJSON_IsObject(root) || (!jget(root, "metadata") && !jget(root, "publications") &&
                                  !jget(root, "navigation") && !jget(root, "groups"))) {
        cJSON_Delete(root);
        opds_seterr(err, errcap, "not an OPDS 2 feed");
        return -1;
    }
    md = jget(root, "metadata");
    s = jstr(md, "title");
    if (s) f->title = opds_dups(f, s);
    f->subtitle = opds_dups(f, jstr(md, "subtitle"));
    f->id = opds_dups(f, jstr(md, "identifier"));
    f->updated = opds_dups(f, jstr(md, "modified"));
    v = jget(md, "numberOfItems");
    if (cJSON_IsNumber(v) && v->valuedouble >= 0) f->total = (long long)v->valuedouble;
    feed_links(f, jget(root, "links"));
    add_navigation(f, jget(root, "navigation"), NULL);
    add_publications(f, jget(root, "publications"), NULL);
    cJSON_ArrayForEach(g, jget(root, "groups"))
    {
        const char *title = jstr(jget(g, "metadata"), "title");
        const char *group = title ? opds_dups(f, title) : NULL;
        add_navigation(f, jget(g, "navigation"), group);
        add_publications(f, jget(g, "publications"), group);
    }
    add_facets(f, jget(root, "facets"));
    cJSON_Delete(root);
    if (!f->title) f->title = "";
    return 0;
}
