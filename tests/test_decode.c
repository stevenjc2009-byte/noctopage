/* img/decode on the real fixtures (tall PNG/JPEG, WebP): exact output sizes, the no-upscale fit
 * rule, scaled decode against decode-then-box-scale, and truncated / corrupt / garbage input
 * (these run under ASAN + UBSAN, so a crash or overrun is a failure). Hand-made GIF, BMP, CMYK
 * JPEG, PNG variants and the 128 MiB limit are in test_decode_gen.c. */
#include "test.h"

#include "img/decode.h"
#include "img/scale.h"

#include <png.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FX "fixtures/"

static uint8_t *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    uint8_t *b;
    long sz;

    *n = 0;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = malloc((size_t)sz + 1);
    if (b && fread(b, 1, (size_t)sz, f) == (size_t)sz)
        *n = (size_t)sz;
    fclose(f);
    return b;
}

static int dec(const uint8_t *d, size_t n, int mw, int mh, img_t *im, char *err)
{
    return img_decode(d, n, mw, mh, im, err, 160);
}

static void expect_size(const uint8_t *d, size_t n, int mw, int mh, int w, int h, int sw, int sh)
{
    img_t im;
    char err[160];
    int rc = dec(d, n, mw, mh, &im, err);

    CHECK_INT(rc, 0);
    if (rc) {
        printf("  max %dx%d: %s\n", mw, mh, err);
        return;
    }
    CHECK_INT(im.w, w);
    CHECK_INT(im.h, h);
    CHECK_INT(im.src_w, sw);
    CHECK_INT(im.src_h, sh);
    CHECK(im.rgba != NULL);
    img_free(&im);
    CHECK(im.rgba == NULL && im.w == 0);
}

static const uint8_t *px(const img_t *im, int x, int y)
{
    return im->rgba + ((size_t)y * im->w + x) * 4;
}

/* Mean absolute difference of two equal-sized RGBA images (alpha ignored); *far gets the share of
 * pixels where any channel differs by more than 48 (edges that came out blurred or shifted). */
static double mean_diff(const img_t *a, const img_t *b, double *far)
{
    double sum = 0;
    size_t i, nfar = 0, n = (size_t)a->w * a->h;

    for (i = 0; i < n; i++) {
        int c, worst = 0;

        for (c = 0; c < 3; c++) {
            int dv = abs(a->rgba[i * 4 + c] - b->rgba[i * 4 + c]);

            sum += dv;
            worst = dv > worst ? dv : worst;
        }
        nfar += worst > 48;
    }
    *far = (double)nfar / (double)n;
    return sum / (double)(n * 3);
}

/* A scaled decode must agree with "decode whole, then box-scale". */
static void check_against_full(const uint8_t *d, size_t n, int mw, int mh, double tol,
                               double far_tol)
{
    img_t full, part, ref;
    double md, far;
    char err[160];

    memset(&ref, 0, sizeof ref);
    if (dec(d, n, 0, 0, &full, err) != 0 || dec(d, n, mw, mh, &part, err) != 0) {
        CHECK(!"decode for comparison");
        return;
    }
    ref.w = part.w;
    ref.h = part.h;
    ref.rgba = malloc((size_t)part.w * part.h * 4);
    CHECK_INT(img_scale(full.rgba, full.w, full.h, ref.rgba, part.w, part.h), 0);
    md = mean_diff(&part, &ref, &far);
    CHECK(md <= tol);
    CHECK(far <= far_tol);
    img_free(&full);
    img_free(&part);
    img_free(&ref);
}

static void test_tall(const char *name, int is_png)
{
    char path[64];
    size_t n;
    uint8_t *d;
    img_t a;
    char err[160];
    int w, h;

    snprintf(path, sizeof path, FX "%s", name);
    d = slurp(path, &n);
    CHECK(d != NULL);
    if (!d)
        return;
    CHECK_INT(img_probe(d, n, &w, &h), 0);
    CHECK_INT(w, 800);
    CHECK_INT(h, 9000);
    expect_size(d, n, 0, 0, 800, 9000, 800, 9000);
    expect_size(d, n, 960, 0, 800, 9000, 800, 9000); /* wider limit: never upscaled */
    expect_size(d, n, 2000, 20000, 800, 9000, 800, 9000);
    expect_size(d, n, 0, 544, 48, 544, 800, 9000);   /* 800 * 544 / 9000 = 48.4 */
    expect_size(d, n, 960, 544, 48, 544, 800, 9000);
    expect_size(d, n, 100, 100, 9, 100, 800, 9000);
    expect_size(d, n, 400, 0, 400, 4500, 800, 9000);
    expect_size(d, n, 1, 1, 1, 1, 800, 9000);
    /* PNG now pre-shrinks by exact halving before the quality scaler (like the JPEG
     * DCT downscale), so rounding-level differences vs one-shot box scale are
     * expected: 0.5 mean, still with zero shifted edges. */
    check_against_full(d, n, 0, 544, is_png ? 0.5 : 2.5, is_png ? 0.0 : 1.0);
    check_against_full(d, n, 100, 0, is_png ? 0.5 : 2.5, is_png ? 0.0 : 1.0);
    check_against_full(d, n, 400, 0, is_png ? 0.5 : 2.5, is_png ? 0.0 : 1.0);
    check_against_full(d, n, 0, 3000, is_png ? 0.5 : 2.5, is_png ? 0.0 : 1.0);
    check_against_full(d, n, 301, 0, is_png ? 0.5 : 2.5, is_png ? 0.0 : 1.0);

    /* Full-size decode is sharp: the 6 px white outline ends at x = 5. (Independent of the scaled
     * path, so a DCT scale chosen too small, which blurs this edge, cannot hide.) */
    if (dec(d, n, 0, 0, &a, err) == 0) {
        const uint8_t *band = px(&a, 60, 4500), *e;
        int x, c;

        for (x = 1; x <= 4; x++)
            for (c = 0; c < 3; c++)
                CHECK(px(&a, x, 4500)[c] >= 235);
        for (x = 6; x <= 7; x++) {
            e = px(&a, x, 4500);
            for (c = 0; c < 3; c++)
                CHECK(abs(e[c] - band[c]) <= 40);
        }
        img_free(&a);
    } else {
        CHECK(!"full decode");
    }
    /* flat band: same colour whatever the format (jpeg within its loss) */
    CHECK_INT(dec(d, n, 0, 544, &a, err), 0);
    CHECK_INT(a.w, 48);
    CHECK(a.rgba && a.w == 48 && a.h == 544);
    if (a.rgba) {
        const uint8_t *p = px(&a, 10, 20), *q = px(&a, 10, 20 + 61); /* band 0, band 1 */

        CHECK(p[3] == 255);
        CHECK(abs(p[0] - q[0]) + abs(p[1] - q[1]) + abs(p[2] - q[2]) > 30); /* two bands differ */
    }
    img_free(&a);
    free(d);
}

static void test_webp(void)
{
    size_t n;
    uint8_t *d = slurp(FX "page.webp", &n);
    int w, h;

    CHECK(d != NULL);
    if (!d)
        return;
    CHECK_INT(img_probe(d, n, &w, &h), 0);
    CHECK_INT(w, 400);
    CHECK_INT(h, 600);
    expect_size(d, n, 0, 0, 400, 600, 400, 600);
    expect_size(d, n, 800, 800, 400, 600, 400, 600);
    expect_size(d, n, 200, 0, 200, 300, 400, 600);
    expect_size(d, n, 0, 150, 100, 150, 400, 600);
    expect_size(d, n, 77, 77, 51, 77, 400, 600);
    check_against_full(d, n, 200, 0, 1.5, 1.0);
    check_against_full(d, n, 0, 150, 1.5, 1.0);
    free(d);
}

/* ---- broken input: must come back as an error (or a sane image), never crash ---- */
static void try_broken(const uint8_t *d, size_t n, const char *what)
{
    img_t im;
    char err[160];
    int rc = dec(d, n, 0, 544, &im, err);

    if (rc == 0) {
        CHECK(im.rgba && im.w > 0 && im.h > 0 && im.h <= 544); /* partial image is fine, if sane */
    } else {
        CHECK(im.rgba == NULL && im.w == 0);
        if (!err[0])
            printf("  %s (%zu bytes): error without a message\n", what, n);
        CHECK(err[0] != '\0');
    }
    img_free(&im);
}

static void test_truncated(const char *name)
{
    char path[64];
    size_t n, k;
    uint8_t *d;
    static const size_t head[] = {0, 1, 2, 3, 4, 8, 11, 12, 16, 20, 24, 30, 33, 41, 100};
    int w, h;

    snprintf(path, sizeof path, FX "%s", name);
    d = slurp(path, &n);
    CHECK(d != NULL);
    if (!d)
        return;
    for (k = 0; k < sizeof head / sizeof head[0]; k++)
        if (head[k] < n) {
            if (head[k]) /* n == 0 is "empty data", tested elsewhere */
                try_broken(d, head[k], name);
            (void)img_probe(d, head[k], &w, &h); /* header-only query on a cut file */
        }
    for (k = 1; k < 16; k++)
        try_broken(d, n * k / 16, name);
    try_broken(d, n - 1, name);
    d[n / 2] ^= 0xFF; /* a flipped byte mid-stream */
    d[n / 3] ^= 0x5A;
    try_broken(d, n, name);
    free(d);
}

static void test_garbage(void)
{
    static const char text[] = "this is not an image at all, just words";
    uint8_t noise[300];
    uint32_t s = 7;
    img_t im;
    char err[160];
    size_t i;
    int w, h;

    for (i = 0; i < sizeof noise; i++)
        noise[i] = (uint8_t)((s = s * 1664525u + 1013904223u) >> 24);
    CHECK_INT(dec((const uint8_t *)text, sizeof text, 0, 0, &im, err), -1);
    CHECK(err[0] && im.rgba == NULL);
    CHECK_INT(dec(noise, sizeof noise, 0, 0, &im, err), -1);
    CHECK(err[0] && im.rgba == NULL);
    CHECK_INT(dec(noise, 1, 0, 0, &im, err), -1);
    CHECK_INT(dec(noise, 0, 0, 0, &im, err), -1);
    CHECK_INT(dec(NULL, 10, 0, 0, &im, err), -1);
    CHECK_INT(img_decode(noise, sizeof noise, 0, 0, NULL, err, sizeof err), -1);
    CHECK_INT(img_decode(noise, sizeof noise, 0, 0, &im, NULL, 0), -1); /* no message buffer */
    CHECK_INT(img_probe((const uint8_t *)text, sizeof text, &w, &h), -1);
    CHECK_INT(img_probe(noise, sizeof noise, &w, &h), -1);
    CHECK_INT(img_probe(NULL, 5, &w, &h), -1);
    CHECK_INT(img_probe(noise, 0, &w, &h), -1);
    img_free(NULL);
    {
        uint8_t magic[12] = {0xFF, 0xD8, 0xFF, 0xE0, 1, 2, 3, 4, 5, 6, 7, 8}; /* magic only */
        CHECK_INT(dec(magic, sizeof magic, 0, 0, &im, err), -1);
        memcpy(magic, "\x89PNG\r\n\x1a\n", 8);
        CHECK_INT(dec(magic, sizeof magic, 0, 0, &im, err), -1);
        memcpy(magic, "RIFF\x04\0\0\0WEBP", 12);
        CHECK_INT(dec(magic, sizeof magic, 0, 0, &im, err), -1);
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    test_tall("tall_800x9000.png", 1);
    test_tall("tall_800x9000.jpg", 0);
    test_webp();
    test_truncated("tall_800x9000.png");
    test_truncated("tall_800x9000.jpg");
    test_truncated("page.webp");
    test_garbage();
    return TEST_DONE("decode");
}
