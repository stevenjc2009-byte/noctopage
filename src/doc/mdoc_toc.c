/* mdoc table of contents (resolved in the current layout) and cover thumbnails. */
#include "doc/mdoc_internal.h"

#include <stdio.h>

static int toc_walk(mdoc_t *d, fz_outline *o, int depth, mdoc_toc_item_t *out, int n, int max)
{
    for (; o && n < max; o = o->next) {
        fz_location loc = o->page;
        mdoc_toc_item_t *it = &out[n++];

        snprintf(it->title, sizeof it->title, "%s", o->title ? o->title : "");
        it->depth = depth;
        if (o->uri && !fz_is_external_link(d->ctx, o->uri))
            loc = fz_resolve_link(d->ctx, d->doc, o->uri, NULL, NULL);
        it->page = loc.chapter < 0 ? -1 : fz_page_number_from_location(d->ctx, d->doc, loc);
        if (o->down)
            n = toc_walk(d, o->down, depth + 1, out, n, max);
    }
    return n;
}

int mdoc_toc(mdoc_t *d, mdoc_toc_item_t *out, int max)
{
    fz_outline *ol = NULL;
    int n = -1;

    if (!d || !d->doc || !out || max < 0)
        return -1;
    fz_var(ol);
    fz_var(n);
    mdoc__lock(d);
    fz_try(d->ctx)
    {
        ol = fz_load_outline(d->ctx, d->doc);
        n = toc_walk(d, ol, 0, out, 0, max);
    }
    fz_always(d->ctx)
    {
        fz_drop_outline(d->ctx, ol);
        mdoc__unlock(d);
    }
    fz_catch(d->ctx)
        n = -1;
    return n;
}

int mdoc_render_cover(const char *path, int max_w, int max_h, mdoc_image_t *out)
{
    mdoc_t *d;
    float w = 0, h = 0, sx, sy;
    int rc = -1;

    if (!out || max_w < 1 || max_h < 1)
        return -1;
    out->w = out->h = 0;
    out->rgba = NULL;
    d = mdoc_open(path, NULL, 0);
    if (!d)
        return -1;
    if (mdoc_page_size(d, 0, &w, &h) == 0 && w > 0 && h > 0) {
        sx = (float)max_w / w;
        sy = (float)max_h / h;
        rc = mdoc_render(d, 0, sx < sy ? sx : sy, 0, NULL, 0x000000, 0xFFFFFF, out);
    }
    mdoc_close(d);
    return rc;
}
