/* mdoc: global context, open/close, metadata, reflow layout, page count, contents, cover.
 * Rendering lives in mdoc_render.c, reading positions in mdoc_pos.c. See mdoc.h. */
#include "doc/mdoc_internal.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MDOC_STORE_BYTES (32u << 20)

static fz_context *g_ctx;
static pthread_mutex_t g_locks[FZ_LOCK_MAX];
static pthread_mutex_t g_css_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_init_lock = PTHREAD_MUTEX_INITIALIZER;

static void lock_cb(void *user, int lock)
{
    (void)user;
    pthread_mutex_lock(&g_locks[lock]);
}

static void unlock_cb(void *user, int lock)
{
    (void)user;
    pthread_mutex_unlock(&g_locks[lock]);
}

/* Pure module: MuPDF's stderr warnings and error reports are dropped. */
static void quiet_cb(void *user, const char *message)
{
    (void)user;
    (void)message;
}

int mdoc_global_init(void)
{
    fz_locks_context locks;
    int i, rc = 0;

    pthread_mutex_lock(&g_init_lock);
    if (g_ctx)
        goto out;
    for (i = 0; i < FZ_LOCK_MAX; i++)
        pthread_mutex_init(&g_locks[i], NULL);
    locks.user = NULL;
    locks.lock = lock_cb;
    locks.unlock = unlock_cb;
    g_ctx = fz_new_context(NULL, &locks, MDOC_STORE_BYTES);
    if (!g_ctx) {
        rc = -1;
        goto out;
    }
    fz_set_warning_callback(g_ctx, quiet_cb, NULL);
    fz_set_error_callback(g_ctx, quiet_cb, NULL);
    fz_var(rc);
    fz_try(g_ctx)
        fz_register_document_handlers(g_ctx);
    fz_catch(g_ctx)
        rc = -1;
    if (rc != 0) {
        fz_drop_context(g_ctx);
        g_ctx = NULL;
    }
out:
    pthread_mutex_unlock(&g_init_lock);
    return rc;
}

void mdoc_global_shutdown(void)
{
    pthread_mutex_lock(&g_init_lock);
    if (g_ctx) {
        fz_drop_context(g_ctx);
        g_ctx = NULL;
    }
    pthread_mutex_unlock(&g_init_lock);
}

void mdoc__set_err(char *err, size_t cap, const char *msg)
{
    if (err && cap > 0)
        snprintf(err, cap, "%s", msg ? msg : "unknown error");
}

/* Installs `css` as the (shared) user stylesheet and holds the CSS lock. Set to NULL first:
 * fz_set_user_css frees the old string before copying, so a failed copy would otherwise leave
 * a dangling pointer behind. */
static void css_enter(fz_context *ctx, const char *css)
{
    const char *cur;

    pthread_mutex_lock(&g_css_lock);
    cur = fz_user_css(ctx);
    if (cur && strcmp(cur, css) == 0)
        return;
    fz_set_user_css(ctx, NULL);
    fz_try(ctx)
        fz_set_user_css(ctx, css);
    fz_catch(ctx)
    {
        /* Out of memory: parsing proceeds with no user CSS rather than failing. */
    }
}

void mdoc__lock(mdoc_t *d)
{
    if (d->reflow)
        css_enter(d->ctx, d->css);
}

void mdoc__unlock(mdoc_t *d)
{
    if (d->reflow)
        pthread_mutex_unlock(&g_css_lock);
}

void mdoc_layout_default(mdoc_layout_t *out)
{
    out->page_w = 360;
    out->page_h = 540;
    out->font_px = 16.0f;
    out->margin_px = 16.0f;
    out->line_spacing = 1.4f;
    out->font_family = 0;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static void clamp_layout(mdoc_layout_t *s)
{
    float small;

    if (s->page_w < 64)
        s->page_w = 64;
    if (s->page_h < 64)
        s->page_h = 64;
    if (s->page_w > 8192)
        s->page_w = 8192;
    if (s->page_h > 8192)
        s->page_h = 8192;
    small = (float)(s->page_w < s->page_h ? s->page_w : s->page_h);
    s->font_px = clampf(s->font_px, 4.0f, 200.0f);
    s->margin_px = clampf(s->margin_px, 0.0f, small / 4.0f);
    s->line_spacing = clampf(s->line_spacing, 1.0f, 2.0f);
    s->font_family = s->font_family == 1 ? 1 : 0;
}

/* !important outranks the book's own rules (MuPDF adds 1000 to the specificity). The body
 * margin is zeroed so that margin_px is the whole margin. */
static void build_css(const mdoc_layout_t *s, char *out, size_t cap)
{
    snprintf(out, cap,
             "@page{margin:%.2fpx !important}"
             "body{margin:0 !important;padding:0 !important}"
             /* FB2: section>title and coverpage have page-break-before:always, which MuPDF
              * skips only at y == 0 of a page. FictionBook{margin:1em} and the 1em top margins
              * of title>p and image (collapsed up to the root) put the first one at 1em, so
              * page 0 came out blank; zeroing them makes page 0 the cover or first section.
              * Later sections still break (they start below earlier text). */
             "FictionBook{margin:0 !important;padding:0 !important}"
             "section>title>p{margin-top:0 !important}coverpage>image{margin:0 !important}"
             "*{line-height:%.2f !important}%s",
             s->margin_px, s->line_spacing,
             s->font_family == 1 ? "*{font-family:sans-serif !important}"
                                   "code,pre,kbd,samp,tt{font-family:monospace !important}"
                                 : "");
}

/* Opens path with `css` installed and lays it out. Returns the document or NULL (err set). */
static fz_document *open_doc(fz_context *ctx, const char *path, const char *css,
                             const mdoc_layout_t *s, int *reflow, char *err, size_t errcap)
{
    fz_document *doc = NULL;

    fz_var(doc);
    css_enter(ctx, css);
    fz_try(ctx)
    {
        doc = fz_open_document(ctx, path);
        if (fz_needs_password(ctx, doc) && !fz_authenticate_password(ctx, doc, ""))
            fz_throw(ctx, FZ_ERROR_ARGUMENT, "document is password protected");
        *reflow = fz_is_document_reflowable(ctx, doc);
        if (*reflow)
            fz_layout_document(ctx, doc, (float)s->page_w, (float)s->page_h, s->font_px);
        else if (fz_count_pages(ctx, doc) < 1)
            fz_throw(ctx, FZ_ERROR_FORMAT, "document has no pages");
    }
    fz_always(ctx)
        pthread_mutex_unlock(&g_css_lock);
    fz_catch(ctx)
    {
        mdoc__set_err(err, errcap, fz_caught_message(ctx));
        fz_drop_document(ctx, doc);
        doc = NULL;
    }
    return doc;
}

mdoc_t *mdoc_open(const char *path, char *err, size_t errcap)
{
    mdoc_t *d;

    if (!path) {
        mdoc__set_err(err, errcap, "no path");
        return NULL;
    }
    if (!g_ctx) {
        mdoc__set_err(err, errcap, "mdoc_global_init was not called");
        return NULL;
    }
    d = calloc(1, sizeof *d);
    if (!d || !(d->path = strdup(path))) {
        free(d);
        mdoc__set_err(err, errcap, "out of memory");
        return NULL;
    }
    d->npages = -1;
    d->dl_page = -1;
    mdoc_layout_default(&d->layout);
    build_css(&d->layout, d->css, sizeof d->css);
    d->ctx = fz_clone_context(g_ctx);
    if (!d->ctx) {
        mdoc__set_err(err, errcap, "cannot clone MuPDF context");
        free(d->path);
        free(d);
        return NULL;
    }
    d->doc = open_doc(d->ctx, path, d->css, &d->layout, &d->reflow, err, errcap);
    if (!d->doc) {
        fz_drop_context(d->ctx);
        free(d->path);
        free(d);
        return NULL;
    }
    return d;
}

void mdoc__drop_caches(mdoc_t *d)
{
    d->npages = -1;
    free(d->chars);
    d->chars = NULL;
    fz_drop_display_list(d->ctx, d->dl);
    d->dl = NULL;
    d->dl_page = -1;
}

void mdoc_close(mdoc_t *d)
{
    if (!d)
        return;
    mdoc__drop_caches(d);
    fz_drop_document(d->ctx, d->doc);
    fz_drop_context(d->ctx);
    free(d->path);
    free(d);
}

int mdoc_is_reflowable(const mdoc_t *d)
{
    return d && d->doc ? d->reflow : 0;
}

static int meta(mdoc_t *d, const char *key, char *out, size_t cap)
{
    int n = -1;

    if (!out || cap == 0)
        return 0;
    out[0] = '\0';
    if (!d || !d->doc)
        return 0;
    fz_var(n);
    mdoc__lock(d);
    fz_try(d->ctx)
        n = fz_lookup_metadata(d->ctx, d->doc, key, out, cap);
    fz_always(d->ctx)
        mdoc__unlock(d);
    fz_catch(d->ctx)
        n = -1;
    if (n <= 0) {
        out[0] = '\0';
        return 0;
    }
    out[cap - 1] = '\0';
    return (int)strlen(out);
}

int mdoc_title(mdoc_t *d, char *out, size_t cap)
{
    return meta(d, FZ_META_INFO_TITLE, out, cap);
}

int mdoc_author(mdoc_t *d, char *out, size_t cap)
{
    return meta(d, FZ_META_INFO_AUTHOR, out, cap);
}

int mdoc_set_layout(mdoc_t *d, const mdoc_layout_t *s)
{
    mdoc_layout_t n;
    char css[sizeof d->css];
    int rc = 0, reflow = 1;

    if (!d || !s || !d->doc)
        return -1;
    if (!d->reflow)
        return 0;
    n = *s;
    clamp_layout(&n);
    build_css(&n, css, sizeof css);
    mdoc__drop_caches(d);
    if (strcmp(css, d->css) != 0) {
        /* MuPDF applies user CSS only while parsing (and EPUB keeps parsed chapters in the
         * store), so new CSS needs a fresh document. Open it before dropping the old one. */
        fz_document *nd = open_doc(d->ctx, d->path, css, &n, &reflow, NULL, 0);
        if (!nd)
            return -1;
        fz_drop_document(d->ctx, d->doc);
        d->doc = nd;
        d->reflow = reflow;
        memcpy(d->css, css, sizeof d->css);
        d->layout = n;
        return 0;
    }
    fz_var(rc);
    mdoc__lock(d);
    fz_try(d->ctx)
        fz_layout_document(d->ctx, d->doc, (float)n.page_w, (float)n.page_h, n.font_px);
    fz_always(d->ctx)
        mdoc__unlock(d);
    fz_catch(d->ctx)
        rc = -1;
    if (rc == 0)
        d->layout = n;
    return rc;
}

int mdoc__count(mdoc_t *d)
{
    int n = -1;

    if (!d || !d->doc)
        return -1;
    if (d->npages >= 0)
        return d->npages;
    fz_var(n);
    mdoc__lock(d);
    fz_try(d->ctx)
        n = fz_count_pages(d->ctx, d->doc);
    fz_always(d->ctx)
        mdoc__unlock(d);
    fz_catch(d->ctx)
        n = -1;
    if (n >= 0)
        d->npages = n;
    return n;
}

int mdoc_page_count(mdoc_t *d)
{
    return mdoc__count(d);
}
