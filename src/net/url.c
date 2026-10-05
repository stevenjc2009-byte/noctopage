#include "net/url.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *scheme, *auth, *path, *query, *frag; /* NULL = component absent */
    size_t scheme_len, auth_len, path_len, query_len, frag_len;
} url_parts;

static int is_scheme_char(char c, int first)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
        return 1;
    return !first && ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.');
}

int url_has_scheme(const char *s)
{
    size_t i;
    if (!is_scheme_char(s[0], 1))
        return 0;
    for (i = 1; is_scheme_char(s[i], 0); i++) {
    }
    return s[i] == ':';
}

static void split(const char *s, url_parts *u)
{
    size_t n;
    memset(u, 0, sizeof *u);
    if (url_has_scheme(s)) {
        n = strcspn(s, ":");
        u->scheme = s;
        u->scheme_len = n;
        s += n + 1;
    }
    if (s[0] == '/' && s[1] == '/') {
        n = strcspn(s + 2, "/?#");
        u->auth = s + 2;
        u->auth_len = n;
        s += 2 + n;
    }
    n = strcspn(s, "?#");
    u->path = s;
    u->path_len = n;
    s += n;
    if (*s == '?') {
        n = strcspn(s + 1, "#");
        u->query = s + 1;
        u->query_len = n;
        s += 1 + n;
    }
    if (*s == '#') {
        u->frag = s + 1;
        u->frag_len = strlen(s + 1);
    }
}

/* Drop the last output segment and its preceding '/'. */
static size_t pop_segment(const char *out, size_t o)
{
    while (o > 0 && out[o - 1] != '/')
        o--;
    return o > 0 ? o - 1 : 0;
}

/* RFC 3986 5.2.4. in/inlen is the path, out receives the result (capacity >= inlen + 1). */
static size_t remove_dots(const char *in, size_t inlen, char *out)
{
    size_t o = 0, i = 0;

    while (i < inlen) {
        const char *s = in + i;
        size_t left = inlen - i;
        if (left >= 3 && memcmp(s, "../", 3) == 0) {
            i += 3;
        } else if (left >= 2 && memcmp(s, "./", 2) == 0) {
            i += 2;
        } else if (left >= 3 && memcmp(s, "/./", 3) == 0) {
            i += 2; /* "/./x" -> "/x" */
        } else if (left == 2 && memcmp(s, "/.", 2) == 0) {
            out[o++] = '/';
            i += 2;
        } else if (left >= 4 && memcmp(s, "/../", 4) == 0) {
            o = pop_segment(out, o);
            i += 3; /* "/../x" -> "/x" */
        } else if (left == 3 && memcmp(s, "/..", 3) == 0) {
            o = pop_segment(out, o);
            out[o++] = '/';
            i += 3;
        } else if ((left == 1 && s[0] == '.') || (left == 2 && memcmp(s, "..", 2) == 0)) {
            i = inlen;
        } else {
            size_t seg = 1; /* a leading '/' belongs to the segment */
            while (seg < left && s[seg] != '/')
                seg++;
            memcpy(out + o, s, seg);
            o += seg;
            i += seg;
        }
    }
    out[o] = '\0';
    return o;
}

static int append(char *dst, size_t cap, size_t *n, const char *s, size_t len)
{
    if (*n + len >= cap)
        return -1;
    memcpy(dst + *n, s, len);
    *n += len;
    dst[*n] = '\0';
    return 0;
}

/* RFC 3986 5.2.3: base path up to its last '/' plus the reference path, into merged. */
static int merge_paths(const url_parts *b, const url_parts *r, char *merged, size_t cap, size_t *mlen)
{
    size_t keep = b->path_len, n = 0;

    if (b->auth != NULL && b->path_len == 0) {
        merged[n++] = '/';
    } else {
        while (keep > 0 && b->path[keep - 1] != '/')
            keep--;
        if (keep + r->path_len >= cap)
            return -1;
        memcpy(merged, b->path, keep);
        n = keep;
    }
    if (n + r->path_len >= cap)
        return -1;
    memcpy(merged + n, r->path, r->path_len);
    *mlen = n + r->path_len;
    return 0;
}

int url_resolve(char *dst, size_t cap, const char *base, const char *ref)
{
    url_parts r, b, t;
    char merged[URL_MAX], path[URL_MAX];
    size_t n = 0, plen;
    int has_base = base != NULL && base[0] != '\0';

    if (cap == 0)
        return -1;
    dst[0] = '\0';
    if (strlen(ref) >= URL_MAX || (has_base && strlen(base) >= URL_MAX))
        return -1;
    split(ref, &r);
    if (r.scheme != NULL || !has_base) {
        t = r;
    } else {
        split(base, &b);
        memset(&t, 0, sizeof t);
        t.scheme = b.scheme;
        t.scheme_len = b.scheme_len;
        if (r.auth != NULL) {
            t.auth = r.auth;
            t.auth_len = r.auth_len;
            t.path = r.path;
            t.path_len = r.path_len;
            t.query = r.query;
            t.query_len = r.query_len;
        } else {
            t.auth = b.auth;
            t.auth_len = b.auth_len;
            if (r.path_len == 0) {
                t.path = b.path;
                t.path_len = b.path_len;
                t.query = r.query != NULL ? r.query : b.query;
                t.query_len = r.query != NULL ? r.query_len : b.query_len;
            } else {
                t.query = r.query;
                t.query_len = r.query_len;
                if (r.path[0] == '/') {
                    t.path = r.path;
                    t.path_len = r.path_len;
                } else {
                    if (merge_paths(&b, &r, merged, sizeof merged, &t.path_len) != 0)
                        return -1;
                    t.path = merged;
                }
            }
        }
        t.frag = r.frag;
        t.frag_len = r.frag_len;
    }
    plen = remove_dots(t.path, t.path_len, path);
    if (t.scheme != NULL &&
        (append(dst, cap, &n, t.scheme, t.scheme_len) || append(dst, cap, &n, ":", 1)))
        return -1;
    if (t.auth != NULL && (append(dst, cap, &n, "//", 2) || append(dst, cap, &n, t.auth, t.auth_len)))
        return -1;
    if (append(dst, cap, &n, path, plen))
        return -1;
    if (t.query != NULL && (append(dst, cap, &n, "?", 1) || append(dst, cap, &n, t.query, t.query_len)))
        return -1;
    if (t.frag != NULL && (append(dst, cap, &n, "#", 1) || append(dst, cap, &n, t.frag, t.frag_len)))
        return -1;
    return 0;
}

int url_encode_component(char *dst, size_t cap, const char *src)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;

    if (cap == 0)
        return -1;
    for (; *src; src++) {
        unsigned char c = (unsigned char)*src;
        int keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                   c == '-' || c == '.' || c == '_' || c == '~';
        if (n + (keep ? 1 : 3) >= cap)
            return -1;
        if (keep) {
            dst[n++] = (char)c;
        } else {
            dst[n++] = '%';
            dst[n++] = hex[c >> 4];
            dst[n++] = hex[c & 15];
        }
    }
    dst[n] = '\0';
    return (int)n;
}

static const char *lookup(const url_var *vars, size_t nvars, const char *name, size_t len)
{
    size_t i;
    for (i = 0; i < nvars; i++)
        if (strlen(vars[i].name) == len && memcmp(vars[i].name, name, len) == 0)
            return vars[i].value;
    return NULL;
}

static int put_value(char *dst, size_t cap, size_t *n, const char *v)
{
    int k = url_encode_component(dst + *n, cap - *n, v);
    if (k < 0)
        return -1;
    *n += (size_t)k;
    return 0;
}

/* One {...} expression whose body is [e, close). */
static int expand_expr(char *dst, size_t cap, size_t *n, const char *e, const char *close,
                       const url_var *vars, size_t nvars)
{
    char op = 0;
    int first = 1;

    if (*e == '?' || *e == '&')
        op = *e++;
    while (e < close) {
        size_t len = strcspn(e, ",}"), nl;
        const char *v;
        if (e + len > close)
            len = (size_t)(close - e);
        nl = len;
        if (!op && nl > 0 && e[nl - 1] == '?')
            nl--;
        v = lookup(vars, nvars, e, nl);
        if (v != NULL) {
            if (op) {
                if (append(dst, cap, n, first && op == '?' ? "?" : "&", 1) ||
                    append(dst, cap, n, e, nl) || append(dst, cap, n, "=", 1) ||
                    put_value(dst, cap, n, v))
                    return -1;
                first = 0;
            } else if (put_value(dst, cap, n, v) != 0) {
                return -1;
            }
        }
        e += len;
        if (e < close && *e == ',')
            e++;
    }
    return 0;
}

int url_expand(char *dst, size_t cap, const char *tmpl, const url_var *vars, size_t nvars)
{
    size_t n = 0;

    if (cap == 0)
        return -1;
    dst[0] = '\0';
    while (*tmpl) {
        const char *open = strchr(tmpl, '{'), *close = open ? strchr(open, '}') : NULL;

        if (close == NULL)
            return append(dst, cap, &n, tmpl, strlen(tmpl));
        if (append(dst, cap, &n, tmpl, (size_t)(open - tmpl)) != 0 ||
            expand_expr(dst, cap, &n, open + 1, close, vars, nvars) != 0)
            return -1;
        tmpl = close + 1;
    }
    return 0;
}

int url_expand_search(char *dst, size_t cap, const char *tmpl, const char *terms)
{
    url_var v[2];
    v[0].name = "searchTerms";
    v[0].value = terms;
    v[1].name = "query";
    v[1].value = terms;
    return url_expand(dst, cap, tmpl, v, 2);
}

int url_expand_page(char *dst, size_t cap, const char *tmpl, int page, int max_width)
{
    char p[16], w[16];
    url_var v[2];
    snprintf(p, sizeof p, "%d", page);
    snprintf(w, sizeof w, "%d", max_width);
    v[0].name = "pageNumber";
    v[0].value = p;
    v[1].name = "maxWidth";
    v[1].value = max_width > 0 ? w : NULL;
    return url_expand(dst, cap, tmpl, v, 2);
}
