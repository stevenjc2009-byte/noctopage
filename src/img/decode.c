#include "img/decode.h"

#include "img/scale.h"

#include <mupdf/fitz.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <turbojpeg.h>
#include <webp/decode.h>

typedef enum { K_JPEG, K_PNG, K_WEBP, K_OTHER } kind_t;

static int fail(char *err, size_t cap, const char *msg)
{
    if (cap)
        snprintf(err, cap, "%s", msg);
    return -1;
}

static int fail2(char *err, size_t cap, const char *what, const char *why)
{
    if (cap)
        snprintf(err, cap, "%s: %s", what, why);
    return -1;
}

/* MuPDF prints warnings and errors to stderr by default; this module reports through err only. */
static void quiet(void *user, const char *msg)
{
    (void)user;
    (void)msg;
}

static kind_t detect(const uint8_t *d, size_t n)
{
    if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF)
        return K_JPEG;
    if (n >= 8 && memcmp(d, "\x89PNG\r\n\x1a\n", 8) == 0)
        return K_PNG;
    if (n >= 12 && memcmp(d, "RIFF", 4) == 0 && memcmp(d + 8, "WEBP", 4) == 0)
        return K_WEBP;
    return K_OTHER;
}

static int size_ok(int64_t w, int64_t h)
{
    return w > 0 && h > 0 && w <= 65535 * 4 && h <= 65535 * 4 &&
           w * h * 4 <= (int64_t)IMG_MAX_RGBA_BYTES;
}

void img_free(img_t *img)
{
    if (!img)
        return;
    free(img->rgba);
    memset(img, 0, sizeof *img);
}

/* Largest size inside max_w x max_h (0 = unlimited) with the source aspect; never upscales. */
static void fit(int sw, int sh, int mw, int mh, int *dw, int *dh)
{
    int cw = mw > 0 && mw < sw, ch = mh > 0 && mh < sh;

    *dw = sw;
    *dh = sh;
    if (cw && (!ch || (int64_t)mw * sh <= (int64_t)mh * sw)) {
        *dw = mw;
        *dh = (int)(((int64_t)sh * mw + sw / 2) / sw);
    } else if (ch) {
        *dh = mh;
        *dw = (int)(((int64_t)sw * mh + sh / 2) / sh);
    }
    if (*dw < 1)
        *dw = 1;
    if (*dh < 1)
        *dh = 1;
}

/* Take ownership of buf (bw x bh); store it in out at exactly dw x dh, box-scaling if needed. */
static int finish(img_t *out, uint8_t *buf, int bw, int bh, int dw, int dh, int sw, int sh,
                  char *err, size_t cap)
{
    if (bw != dw || bh != dh) {
        uint8_t *s = malloc((size_t)dw * (size_t)dh * 4);

        if (!s || img_scale(buf, bw, bh, s, dw, dh) != 0) {
            free(s);
            free(buf);
            return fail(err, cap, "out of memory while scaling");
        }
        free(buf);
        buf = s;
    }
    out->rgba = buf;
    out->w = dw;
    out->h = dh;
    out->src_w = sw;
    out->src_h = sh;
    return 0;
}

/* ---- MuPDF: GIF/BMP/TIFF/CMYK-JPEG/anything else. out == NULL: only report the size. ---- */
static int mupdf_run(const uint8_t *data, size_t len, int mw, int mh, img_t *out, int *pw,
                     int *ph, char *err, size_t cap)
{
    fz_context *ctx = fz_new_context(NULL, NULL, 16u << 20);
    fz_buffer *buf = NULL;
    fz_image *im = NULL;
    fz_pixmap *pix = NULL;
    char msg[160] = "";
    int w = 0, h = 0, dw, dh, ok = 0;

    if (!ctx)
        return fail(err, cap, "out of memory (mupdf context)");
    fz_set_warning_callback(ctx, quiet, NULL);
    fz_set_error_callback(ctx, quiet, NULL);
    fz_var(buf);
    fz_var(im);
    fz_var(pix);
    fz_try(ctx)
    {
        buf = fz_new_buffer_from_shared_data(ctx, data, len);
        im = fz_new_image_from_buffer(ctx, buf);
        w = im->w;
        h = im->h;
        if (out) {
            fz_pixmap *raw, *rgb;

            if (!size_ok(w, h))
                fz_throw(ctx, FZ_ERROR_LIMIT, "image too large (%d x %d)", w, h);
            raw = fz_get_unscaled_pixmap_from_image(ctx, im);
            pix = raw;
            rgb = fz_convert_pixmap(ctx, raw, fz_device_rgb(ctx), NULL, NULL,
                                    fz_default_color_params, 1);
            pix = rgb;
            fz_drop_pixmap(ctx, raw);
        }
    }
    fz_always(ctx)
    {
        fz_drop_image(ctx, im);
        fz_drop_buffer(ctx, buf);
    }
    fz_catch(ctx)
    {
        snprintf(msg, sizeof msg, "%s", fz_caught_message(ctx));
    }
    if (!msg[0] && pw)
        *pw = w, *ph = h;
    if (!msg[0] && out) {
        int n = fz_pixmap_components(ctx, pix), alpha = fz_pixmap_alpha(ctx, pix);
        int stride = fz_pixmap_stride(ctx, pix);
        const uint8_t *s = fz_pixmap_samples(ctx, pix);
        uint8_t *rgba = n - alpha == 3 ? malloc((size_t)w * (size_t)h * 4) : NULL;
        int x, y;

        for (y = 0; rgba && y < h; y++)
            for (x = 0; x < w; x++) {
                const uint8_t *p = s + (size_t)y * stride + (size_t)x * n;
                uint8_t *q = rgba + ((size_t)y * w + x) * 4;
                unsigned a = alpha ? p[3] : 255;

                /* MuPDF pixmaps are premultiplied: undo it for straight RGBA */
                q[0] = a && a < 255 ? (uint8_t)((p[0] * 255u + a / 2) / a) : p[0];
                q[1] = a && a < 255 ? (uint8_t)((p[1] * 255u + a / 2) / a) : p[1];
                q[2] = a && a < 255 ? (uint8_t)((p[2] * 255u + a / 2) / a) : p[2];
                q[3] = (uint8_t)a;
            }
        if (rgba) {
            fit(w, h, mw, mh, &dw, &dh);
            ok = finish(out, rgba, w, h, dw, dh, w, h, err, cap) == 0;
        } else {
            snprintf(msg, sizeof msg, "unsupported pixel layout");
        }
    } else if (!msg[0]) {
        ok = 1;
    }
    fz_drop_pixmap(ctx, pix);
    fz_drop_context(ctx);
    if (!ok && msg[0])
        fail(err, cap, msg);
    return ok ? 0 : -1;
}

/* ---- JPEG ---- */
static int decode_jpeg(const uint8_t *d, size_t n, int mw, int mh, img_t *out, char *err,
                       size_t cap)
{
    tjhandle t = tjInitDecompress();
    tjscalingfactor *sf;
    int w, h, cs, ss, nsf, i, dw, dh, bw, bh, best = -1, rc;
    int64_t bestpix = 0;
    uint8_t *buf;

    if (!t)
        return fail(err, cap, "jpeg: out of memory");
    if (tjDecompressHeader3(t, d, (unsigned long)n, &w, &h, &ss, &cs) != 0) {
        tjDestroy(t);
        return fail(err, cap, "jpeg: bad header");
    }
    if (cs == TJCS_CMYK || cs == TJCS_YCCK) { /* turbojpeg cannot convert these to RGB */
        tjDestroy(t);
        return mupdf_run(d, n, mw, mh, out, NULL, NULL, err, cap);
    }
    fit(w, h, mw, mh, &dw, &dh);
    sf = tjGetScalingFactors(&nsf);
    for (i = 0; sf && i < nsf; i++) { /* smallest decode that still covers the target */
        int sw = (w * sf[i].num + sf[i].denom - 1) / sf[i].denom;
        int sh = (h * sf[i].num + sf[i].denom - 1) / sf[i].denom;

        if (sw >= dw && sh >= dh && (best < 0 || (int64_t)sw * sh < bestpix)) {
            best = i;
            bestpix = (int64_t)sw * sh;
            bw = sw;
            bh = sh;
        }
    }
    if (best < 0) {
        bw = w;
        bh = h;
    }
    if (!size_ok(bw, bh)) {
        tjDestroy(t);
        return fail(err, cap, "jpeg: image too large");
    }
    buf = malloc((size_t)bw * (size_t)bh * 4);
    if (!buf) {
        tjDestroy(t);
        return fail(err, cap, "jpeg: out of memory");
    }
    rc = tjDecompress2(t, d, (unsigned long)n, buf, bw, 0, bh, TJPF_RGBA, TJFLAG_FASTDCT);
    if (rc != 0) {
        fail2(err, cap, "jpeg", tjGetErrorStr());
        tjDestroy(t);
        free(buf);
        return -1;
    }
    tjDestroy(t);
    return finish(out, buf, bw, bh, dw, dh, w, h, err, cap);
}

/* ---- PNG ---- */
static int png_size(const uint8_t *d, size_t n, int *w, int *h)
{
    uint32_t a, b;

    if (n < 24 || memcmp(d + 12, "IHDR", 4) != 0)
        return -1;
    a = (uint32_t)d[16] << 24 | d[17] << 16 | d[18] << 8 | d[19];
    b = (uint32_t)d[20] << 24 | d[21] << 16 | d[22] << 8 | d[23];
    if (a == 0 || b == 0 || a > 0x7fffffffu || b > 0x7fffffffu)
        return -1;
    *w = (int)a;
    *h = (int)b;
    return 0;
}

static int decode_png(const uint8_t *d, size_t n, int mw, int mh, img_t *out, char *err,
                      size_t cap)
{
    png_image im;
    uint8_t *buf;
    int dw, dh, w, h, ok;

    memset(&im, 0, sizeof im);
    im.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&im, d, n)) {
        fail2(err, cap, "png", im.message);
        png_image_free(&im);
        return -1;
    }
    im.format = PNG_FORMAT_RGBA;
    if (!size_ok(im.width, im.height)) {
        png_image_free(&im);
        return fail(err, cap, "png: image too large");
    }
    buf = malloc((size_t)im.width * (size_t)im.height * 4);
    if (!buf) {
        png_image_free(&im);
        return fail(err, cap, "png: out of memory");
    }
    ok = png_image_finish_read(&im, NULL, buf, 0, NULL);
    if (!ok) {
        fail2(err, cap, "png", im.message);
        png_image_free(&im);
        free(buf);
        return -1;
    }
    w = (int)im.width;
    h = (int)im.height;
    png_image_free(&im);
    fit(w, h, mw, mh, &dw, &dh);
    return finish(out, buf, w, h, dw, dh, w, h, err, cap);
}

/* ---- WebP ---- */
static int decode_webp(const uint8_t *d, size_t n, int mw, int mh, img_t *out, char *err,
                       size_t cap)
{
    WebPDecoderConfig cfg;
    int dw, dh;
    uint8_t *buf;

    if (!WebPInitDecoderConfig(&cfg) || WebPGetFeatures(d, n, &cfg.input) != VP8_STATUS_OK)
        return fail(err, cap, "webp: bad header");
    if (cfg.input.has_animation)
        return fail(err, cap, "webp: animated images are not supported");
    fit(cfg.input.width, cfg.input.height, mw, mh, &dw, &dh);
    if (!size_ok(dw, dh))
        return fail(err, cap, "webp: image too large");
    buf = malloc((size_t)dw * (size_t)dh * 4);
    if (!buf)
        return fail(err, cap, "webp: out of memory");
    if (dw != cfg.input.width || dh != cfg.input.height) {
        cfg.options.use_scaling = 1;
        cfg.options.scaled_width = dw;
        cfg.options.scaled_height = dh;
    }
    cfg.output.colorspace = MODE_RGBA;
    cfg.output.is_external_memory = 1;
    cfg.output.u.RGBA.rgba = buf;
    cfg.output.u.RGBA.stride = dw * 4;
    cfg.output.u.RGBA.size = (size_t)dw * (size_t)dh * 4;
    if (WebPDecode(d, n, &cfg) != VP8_STATUS_OK) {
        free(buf);
        return fail(err, cap, "webp: corrupt or truncated data");
    }
    return finish(out, buf, dw, dh, dw, dh, cfg.input.width, cfg.input.height, err, cap);
}

/* ---- public ---- */
int img_probe(const uint8_t *d, size_t n, int *w, int *h)
{
    int pw = 0, ph = 0, cs, ss;

    if (!d || !w || !h)
        return -1;
    switch (detect(d, n)) {
    case K_JPEG: {
        tjhandle t = tjInitDecompress();
        int rc = t ? tjDecompressHeader3(t, d, (unsigned long)n, &pw, &ph, &ss, &cs) : -1;

        if (t)
            tjDestroy(t);
        if (rc != 0)
            return -1;
        break;
    }
    case K_PNG:
        if (png_size(d, n, &pw, &ph) != 0)
            return -1;
        break;
    case K_WEBP:
        if (!WebPGetInfo(d, n, &pw, &ph))
            return -1;
        break;
    default:
        if (n >= 10 && memcmp(d, "GIF8", 4) == 0) {
            pw = d[6] | d[7] << 8;
            ph = d[8] | d[9] << 8;
        } else if (n >= 26 && d[0] == 'B' && d[1] == 'M' && d[14] >= 40) {
            int32_t bh = (int32_t)((uint32_t)d[22] | d[23] << 8 | (uint32_t)d[24] << 16 |
                                   (uint32_t)d[25] << 24);
            pw = d[18] | d[19] << 8 | d[20] << 16 | (int)((uint32_t)d[21] << 24);
            ph = bh < 0 ? -bh : bh;
        } else if (mupdf_run(d, n, 0, 0, NULL, &pw, &ph, NULL, 0) != 0) {
            return -1;
        }
    }
    if (pw <= 0 || ph <= 0)
        return -1;
    *w = pw;
    *h = ph;
    return 0;
}

int img_decode(const uint8_t *d, size_t n, int mw, int mh, img_t *out, char *err, size_t cap)
{
    int rc;

    if (cap)
        err[0] = '\0';
    if (!out)
        return fail(err, cap, "no output");
    memset(out, 0, sizeof *out);
    if (!d || n == 0)
        return fail(err, cap, "empty image data");
    switch (detect(d, n)) {
    case K_JPEG: rc = decode_jpeg(d, n, mw, mh, out, err, cap); break;
    case K_PNG:  rc = decode_png(d, n, mw, mh, out, err, cap); break;
    case K_WEBP: rc = decode_webp(d, n, mw, mh, out, err, cap); break;
    default:     rc = mupdf_run(d, n, mw, mh, out, NULL, NULL, err, cap); break;
    }
    if (rc != 0)
        img_free(out);
    return rc;
}
