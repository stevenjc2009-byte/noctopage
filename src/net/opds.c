/* opds - public API: opds_parse, OpenSearch description parsing and the acquisition/PSE helpers.
 * The feed container lives in opds_build.c; formats in opds_atom.c (OPDS 1) and opds_json.c (OPDS 2). */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "opds_priv.h"
#include "url.h"
#include "xml.h"

/* ---- public API ---- */

int opds_parse(const char *body, size_t len, const char *content_type, const char *base_url,
               opds_feed **out, char *err, size_t errcap)
{
    opds_feed *f;
    int json, rc;
    size_t i = 0;
    char *copy = NULL;

    if (out) *out = NULL;
    if (!body || !out) {
        opds_seterr(err, errcap, "no input");
        return -1;
    }
    if (len > OPDS_MAX_BODY) {
        opds_seterr(err, errcap, "feed too large");
        return -1;
    }
    while (i < len && (body[i] == ' ' || body[i] == '\t' || body[i] == '\r' || body[i] == '\n')) i++;
    if (i >= len) {
        opds_seterr(err, errcap, "empty feed");
        return -1;
    }
    if (content_type && opds_contains_ci(content_type, "json")) json = 1;
    else json = body[i] == '{';
    if (!json && body[i] != '<') {
        opds_seterr(err, errcap, "not an OPDS feed");
        return -1;
    }
    f = opds_feed_new(json ? 2 : 1, base_url);
    if (!f) {
        opds_seterr(err, errcap, "out of memory");
        return -1;
    }
    if (json) {
        rc = opds_parse_json(f, body, len, err, errcap);
    } else {
        copy = (char *)malloc(len + 1);
        if (!copy) {
            opds_feed_free(f);
            opds_seterr(err, errcap, "out of memory");
            return -1;
        }
        memcpy(copy, body, len);
        copy[len] = '\0';
        rc = opds_parse_atom(f, copy, len, err, errcap);
        free(copy);
    }
    if (rc == 0 && opds_feed_oom(f)) {
        opds_seterr(err, errcap, "out of memory");
        rc = -1;
    }
    if (rc != 0) {
        opds_feed_free(f);
        return -1;
    }
    *out = f;
    return 0;
}

int opds_parse_opensearch(const char *body, size_t len, const char *base_url, char *out, size_t cap,
                          char *err, size_t errcap)
{
    xml_parser *p;
    char *copy;
    int best = 0, rc = -1;
    char tmpl[URL_MAX];

    if (cap > 0) out[0] = '\0';
    if (!body || len == 0 || len > OPDS_MAX_BODY) {
        opds_seterr(err, errcap, "bad OpenSearch document");
        return -1;
    }
    copy = (char *)malloc(len + 1);
    p = (xml_parser *)malloc(sizeof *p);
    if (!copy || !p) {
        free(copy);
        free(p);
        opds_seterr(err, errcap, "out of memory");
        return -1;
    }
    memcpy(copy, body, len);
    copy[len] = '\0';
    xml_init(p, copy, len);
    for (;;) {
        xml_event ev = xml_next(p);
        const char *local = NULL, *type, *t;
        int score;
        if (ev == XML_ERROR) {
            opds_seterr(err, errcap, p->error);
            goto done;
        }
        if (ev == XML_EOF) break;
        if (ev != XML_START) continue;
        xml_resolve(p, p->name, 0, &local);
        if (strcmp(local, "Url") != 0) continue;
        type = xml_attr_value(p, "type");
        t = xml_attr_value(p, "template");
        if (!t || !type) continue;
        if (opds_contains_ci(type, "opds-catalog")) score = 2;
        else if (opds_contains_ci(type, "atom+xml")) score = 1;
        else score = 0;
        if (score > best) {
            if (url_resolve(tmpl, sizeof tmpl, base_url, t) != 0) continue;
            best = score;
        }
    }
    if (best == 0) {
        opds_seterr(err, errcap, "no usable search Url");
    } else if (strlen(tmpl) >= cap) {
        opds_seterr(err, errcap, "search template too long");
    } else {
        memcpy(out, tmpl, strlen(tmpl) + 1);
        rc = 0;
    }
done:
    free(copy);
    free(p);
    return rc;
}

/* ---- acquisition helpers ---- */

static const char *ext_format(const char *href)
{
    static const char *const EXT[] = { "epub", "cbz", "cbr", "cb7", "pdf", "mobi", "fb2", "txt", "zip" };
    size_t n = strcspn(href, "?#"), i;
    const char *dot = NULL, *p;
    for (p = href; p < href + n; p++) {
        if (*p == '/') dot = NULL;
        else if (*p == '.') dot = p + 1;
    }
    if (!dot) return "other";
    for (i = 0; i < sizeof EXT / sizeof EXT[0]; i++) {
        size_t el = strlen(EXT[i]), k;
        if ((size_t)(href + n - dot) != el) continue;
        for (k = 0; k < el; k++)
            if (tolower((unsigned char)dot[k]) != EXT[i][k]) break;
        if (k == el) return EXT[i];
    }
    return "other";
}

const char *opds_acq_format(const opds_acq *a)
{
    static const struct { const char *type, *fmt; } TYPES[] = {
        { "application/epub+zip", "epub" }, { "application/epub", "epub" },
        { "application/x-cbz", "cbz" }, { "application/vnd.comicbook+zip", "cbz" },
        { "application/x-cbr", "cbr" }, { "application/vnd.comicbook-rar", "cbr" },
        { "application/x-cb7", "cb7" }, { "application/pdf", "pdf" },
        { "application/x-mobipocket-ebook", "mobi" }, { "application/fb2+zip", "fb2" },
        { "application/x-fictionbook+xml", "fb2" }, { "application/x-fb2", "fb2" },
        { "text/plain", "txt" }, { "text/html", "html" }, { "application/xhtml+xml", "html" },
    };
    char t[64];
    size_t n = a->type ? strcspn(a->type, "; ") : 0, i;
    if (n >= sizeof t) n = sizeof t - 1;
    for (i = 0; i < n; i++) t[i] = (char)tolower((unsigned char)a->type[i]);
    t[n] = '\0';
    for (i = 0; i < sizeof TYPES / sizeof TYPES[0]; i++)
        if (strcmp(t, TYPES[i].type) == 0) return TYPES[i].fmt;
    if (n == 0 || strcmp(t, "application/zip") == 0 || strcmp(t, "application/x-zip-compressed") == 0 ||
        strcmp(t, "application/octet-stream") == 0)
        return ext_format(a->href);
    return "other";
}

static int downloadable(const opds_acq *a)
{
    return a->kind == OPDS_ACQ_GENERIC || a->kind == OPDS_ACQ_OPEN_ACCESS;
}

const opds_acq *opds_best_acquisition(const opds_entry *e, const char *const *prefs)
{
    static const char *const DEFAULT[] = { "epub", "cbz", "pdf", NULL };
    int i, k;
    if (!e) return NULL;
    if (!prefs) prefs = DEFAULT;
    for (k = 0; prefs[k]; k++)
        for (i = 0; i < e->nacq; i++)
            if (downloadable(&e->acq[i]) && strcmp(opds_acq_format(&e->acq[i]), prefs[k]) == 0)
                return &e->acq[i];
    for (i = 0; i < e->nacq; i++)
        if (downloadable(&e->acq[i])) return &e->acq[i];
    return NULL;
}

int opds_pse_url(const opds_entry *e, int page, int max_width, char *dst, size_t cap)
{
    if (!e || !e->pse_href || page < 0) return -1;
    return url_expand_page(dst, cap, e->pse_href, page, max_width);
}
