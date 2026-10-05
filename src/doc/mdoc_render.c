/* mdoc rendering: page size, display-list cache, clipped/rotated render to RGBA, page themes.
 * See mdoc.h for the contract. */
#include "doc/mdoc_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MDOC_MAX_SIDE 65535
#define MDOC_MAX_BYTES (256u << 20)

/* Records where raster images land (output pixels), so the theme can leave them alone. */
typedef struct {
    fz_device super;
    fz_irect *r;
    int n, cap;
    fz_irect area;
} img_rec_t;

static void rec_fill_image(fz_context *ctx, fz_device *dev_, fz_image *img, fz_matrix ctm,
                           float alpha, fz_color_params cp)
{
    img_rec_t *dev = (img_rec_t *)dev_;
    fz_rect r = fz_transform_rect(fz_unit_rect, ctm);
    fz_irect ir;

    (void)img;
    (void)alpha;
    (void)cp;
    /* Inward rounding: anti-aliased edge pixels (part paper) follow the theme. */
    ir.x0 = (int)ceilf(r.x0 - 0.001f);
    ir.y0 = (int)ceilf(r.y0 - 0.001f);
    ir.x1 = (int)floorf(r.x1 + 0.001f);
    ir.y1 = (int)floorf(r.y1 + 0.001f);
    ir = fz_intersect_irect(ir, dev->area);
    if (fz_is_empty_irect(ir))
        return;
    if (dev->n == dev->cap) {
        int cap = dev->cap ? dev->cap * 2 : 8;
        dev->r = fz_realloc_array(ctx, dev->r, cap, fz_irect);
        dev->cap = cap;
    }
    dev->r[dev->n++] = ir;
}

static void rec_drop(fz_context *ctx, fz_device *dev_)
{
    img_rec_t *dev = (img_rec_t *)dev_;
    fz_free(ctx, dev->r);
}

/* Loads `page` into the one-entry display-list cache. Call inside fz_try. */
static void ensure_list(mdoc_t *d, int page)
{
    fz_context *ctx = d->ctx;
    fz_page *pg = NULL;
    fz_display_list *list = NULL;
    fz_rect b;

    if (d->dl && d->dl_page == page)
        return;
    fz_drop_display_list(ctx, d->dl);
    d->dl = NULL;
    d->dl_page = -1;
    fz_var(pg);
    fz_var(list);
    fz_var(b);
    mdoc__lock(d);
    fz_try(ctx)
    {
        pg = fz_load_page(ctx, d->doc, page);
        b = fz_bound_page(ctx, pg);
        list = fz_new_display_list_from_page(ctx, pg);
    }
    fz_always(ctx)
    {
        fz_drop_page(ctx, pg);
        mdoc__unlock(d);
    }
    fz_catch(ctx)
        fz_rethrow(ctx);
    d->dl = list;
    d->dl_bounds = b;
    d->dl_page = page;
}

static int check_page(mdoc_t *d, int page)
{
    int n = mdoc__count(d);
    return n >= 0 && page >= 0 && page < n ? 0 : -1;
}

int mdoc_page_size(mdoc_t *d, int page, float *w_pt, float *h_pt)
{
    fz_page *pg = NULL;
    fz_rect b = fz_empty_rect;
    int rc = 0;

    if (!d || !d->doc || check_page(d, page) != 0)
        return -1;
    if (d->dl && d->dl_page == page) {
        b = d->dl_bounds;
    } else {
        fz_var(pg);
        fz_var(rc);
        fz_var(b);
        mdoc__lock(d);
        fz_try(d->ctx)
        {
            pg = fz_load_page(d->ctx, d->doc, page);
            b = fz_bound_page(d->ctx, pg);
        }
        fz_always(d->ctx)
        {
            fz_drop_page(d->ctx, pg);
            mdoc__unlock(d);
        }
        fz_catch(d->ctx)
            rc = -1;
    }
    if (rc != 0 || fz_is_empty_rect(b))
        return -1;
    if (w_pt)
        *w_pt = b.x1 - b.x0;
    if (h_pt)
        *h_pt = b.y1 - b.y0;
    return 0;
}

/* Per-channel black..white -> ink..paper, the same mapping as fz_tint_pixmap. */
static void make_lut(uint8_t lut[3][256], uint32_t ink, uint32_t paper)
{
    int c, v;

    for (c = 0; c < 3; c++) {
        int sh = 16 - 8 * c;
        int b = (int)((ink >> sh) & 255), w = (int)((paper >> sh) & 255);
        for (v = 0; v < 256; v++) {
            int t = v * (w - b);
            t = t >= 0 ? (t + 127) / 255 : -((-t + 127) / 255);
            lut[c][v] = (uint8_t)(b + t);
        }
    }
}

/* RGB pixmap -> tight RGBA, theme applied outside the recorded image rectangles. */
static void convert(const fz_pixmap *pix, const img_rec_t *rec, uint32_t ink, uint32_t paper,
                    uint8_t *dst, uint8_t *mask)
{
    uint8_t lut[3][256];
    int x, y, i, w = pix->w, h = pix->h;
    int tint = !(ink == 0x000000 && paper == 0xFFFFFF);

    make_lut(lut, ink, paper);
    if (mask)
        for (i = 0; i < rec->n; i++)
            for (y = rec->r[i].y0; y < rec->r[i].y1; y++)
                memset(mask + (size_t)(y - pix->y) * w + (rec->r[i].x0 - pix->x), 1,
                       (size_t)(rec->r[i].x1 - rec->r[i].x0));
    for (y = 0; y < h; y++) {
        const uint8_t *s = pix->samples + (size_t)y * pix->stride;
        uint8_t *o = dst + (size_t)y * w * 4;
        const uint8_t *m = mask ? mask + (size_t)y * w : NULL;
        for (x = 0; x < w; x++, s += pix->n, o += 4) {
            if (tint && !(m && m[x])) {
                o[0] = lut[0][s[0]];
                o[1] = lut[1][s[1]];
                o[2] = lut[2][s[2]];
            } else {
                o[0] = s[0];
                o[1] = s[1];
                o[2] = s[2];
            }
            o[3] = 255;
        }
    }
}

int mdoc_render(mdoc_t *d, int page, float scale, int rotate, const mdoc_rect_t *clip,
                uint32_t ink_rgb, uint32_t paper_rgb, mdoc_image_t *out)
{
    fz_context *ctx;
    fz_pixmap *pix = NULL;
    fz_device *dev = NULL;
    img_rec_t *rec = NULL;
    uint8_t *rgba = NULL, *mask = NULL;
    int rc = -1;

    if (!out)
        return -1;
    out->w = out->h = 0;
    out->rgba = NULL;
    if (!d || !d->doc || !(scale > 0.0f) || !isfinite(scale) ||
        (rotate != 0 && rotate != 90 && rotate != 180 && rotate != 270) || check_page(d, page) != 0)
        return -1;
    ctx = d->ctx;
    ink_rgb &= 0xFFFFFF;
    paper_rgb &= 0xFFFFFF;
    fz_var(pix);
    fz_var(dev);
    fz_var(rec);
    fz_var(rgba);
    fz_var(mask);
    fz_var(rc);
    fz_var(ink_rgb);
    fz_var(paper_rgb);
    fz_try(ctx)
    {
        fz_matrix ctm;
        fz_rect tb, scissor;
        fz_irect area;
        float fw, fh;
        int W, H;

        ensure_list(d, page);
        ctm = fz_pre_rotate(fz_scale(scale, scale), (float)rotate);
        tb = fz_transform_rect(d->dl_bounds, ctm);
        fw = roundf(tb.x1 - tb.x0);
        fh = roundf(tb.y1 - tb.y0);
        if (!(fw >= 1.0f && fh >= 1.0f && fw <= MDOC_MAX_SIDE && fh <= MDOC_MAX_SIDE))
            fz_throw(ctx, FZ_ERROR_ARGUMENT, "output size out of range");
        W = (int)fw;
        H = (int)fh;
        ctm = fz_concat(ctm, fz_translate(-tb.x0, -tb.y0));
        area = fz_make_irect(0, 0, W, H);
        if (clip)
            area = fz_intersect_irect(area, fz_make_irect(clip->x, clip->y,
                                                          clip->x + clip->w, clip->y + clip->h));
        if (fz_is_empty_irect(area))
            fz_throw(ctx, FZ_ERROR_ARGUMENT, "clip is outside the page");
        if ((size_t)(area.x1 - area.x0) * (size_t)(area.y1 - area.y0) * 4 > MDOC_MAX_BYTES)
            fz_throw(ctx, FZ_ERROR_LIMIT, "render area too large");
        scissor = fz_rect_from_irect(area);

        pix = fz_new_pixmap_with_bbox(ctx, fz_device_rgb(ctx), area, NULL, 0);
        fz_clear_pixmap_with_value(ctx, pix, 255);
        dev = fz_new_draw_device(ctx, fz_identity, pix);
        fz_run_display_list(ctx, d->dl, dev, ctm, scissor, NULL);
        fz_close_device(ctx, dev);
        fz_drop_device(ctx, dev);
        dev = NULL;

        if (!(ink_rgb == 0x000000 && paper_rgb == 0xFFFFFF)) {
            rec = fz_new_derived_device(ctx, img_rec_t);
            rec->super.fill_image = rec_fill_image;
            rec->super.drop_device = rec_drop;
            rec->area = area;
            fz_run_display_list(ctx, d->dl, &rec->super, ctm, scissor, NULL);
            fz_close_device(ctx, &rec->super);
            if (rec->n > 0 && !(mask = calloc((size_t)pix->w * pix->h, 1)))
                fz_throw(ctx, FZ_ERROR_SYSTEM, "out of memory");
        }
        rgba = malloc((size_t)pix->w * pix->h * 4);
        if (!rgba)
            fz_throw(ctx, FZ_ERROR_SYSTEM, "out of memory");
        convert(pix, rec, ink_rgb, paper_rgb, rgba, mask);
        out->w = pix->w;
        out->h = pix->h;
        out->rgba = rgba;
        rgba = NULL;
        rc = 0;
    }
    fz_always(ctx)
    {
        fz_drop_device(ctx, dev);
        fz_drop_device(ctx, rec ? &rec->super : NULL);
        fz_drop_pixmap(ctx, pix);
        free(mask);
        free(rgba);
    }
    fz_catch(ctx)
        rc = -1;
    return rc;
}
