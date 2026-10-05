#include "test.h"

#include "img/scale.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *solid(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    uint8_t *p = malloc((size_t)w * h * 4);
    int i;

    for (i = 0; i < w * h; i++) {
        p[i * 4] = r;
        p[i * 4 + 1] = g;
        p[i * 4 + 2] = b;
        p[i * 4 + 3] = a;
    }
    return p;
}

static int all_pixels(const uint8_t *p, int n, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    int i;

    for (i = 0; i < n; i++)
        if (p[i * 4] != r || p[i * 4 + 1] != g || p[i * 4 + 2] != b || p[i * 4 + 3] != a)
            return 0;
    return 1;
}

static void test_solid_stays_exact(void)
{
    static const int dims[][4] = {{7, 5, 3, 2}, {7, 5, 1, 1}, {3, 3, 10, 7}, {1, 1, 9, 9},
                                  {800, 900, 80, 90}, {101, 37, 100, 36}, {13, 13, 13, 12},
                                  /* heavy decimation: per-tap rounding must not leak into the sum */
                                  {1000, 1, 1, 1}, {1, 1000, 1, 1}, {800, 900, 1, 1}, {2999, 3, 3, 3}};
    size_t k;

    for (k = 0; k < sizeof dims / sizeof dims[0]; k++) {
        int sw = dims[k][0], sh = dims[k][1], dw = dims[k][2], dh = dims[k][3];
        uint8_t *s = solid(sw, sh, 201, 17, 255, 128), *d = malloc((size_t)dw * dh * 4);

        CHECK_INT(img_scale(s, sw, sh, d, dw, dh), 0);
        CHECK(all_pixels(d, dw * dh, 201, 17, 255, 128));
        free(s);
        free(d);
    }
}

static void test_known_averages(void)
{
    /* 2x2 -> 1x1: channel-wise mean, rounded to nearest. */
    const uint8_t two[16] = {10, 0, 0, 255, 20, 0, 0, 255, 30, 0, 0, 255, 40, 255, 0, 255};
    uint8_t d[4];
    /* 3x1 -> 2x1: dst0 = (0*1 + 90*.5)/1.5 = 30, dst1 = (90*.5 + 180*1)/1.5 = 150. */
    const uint8_t three[12] = {0, 0, 0, 255, 90, 90, 90, 255, 180, 180, 180, 255};
    uint8_t e[8];
    /* 2x1 -> 4x1: nearest, exact. */
    const uint8_t up[8] = {10, 10, 10, 255, 200, 200, 200, 255};
    uint8_t f[16];
    /* 1x4 -> 1x2: the vertical pass averages too. */
    const uint8_t col[16] = {0, 0, 0, 255, 100, 0, 0, 255, 100, 0, 0, 255, 200, 0, 0, 255};
    uint8_t g[8];

    CHECK_INT(img_scale(two, 2, 2, d, 1, 1), 0);
    CHECK_INT(d[0], 25);
    CHECK_INT(d[1], 64); /* (0+0+0+255)/4 = 63.75 */
    CHECK_INT(d[2], 0);
    CHECK_INT(d[3], 255);

    CHECK_INT(img_scale(three, 3, 1, e, 2, 1), 0);
    CHECK_INT(e[0], 30);
    CHECK_INT(e[4], 150);

    CHECK_INT(img_scale(up, 2, 1, f, 4, 1), 0);
    CHECK_INT(f[0], 10);
    CHECK_INT(f[4], 10);
    CHECK_INT(f[8], 200);
    CHECK_INT(f[12], 200);

    CHECK_INT(img_scale(col, 1, 4, g, 1, 2), 0);
    CHECK_INT(g[0], 50);
    CHECK_INT(g[4], 150);
}

static uint32_t lcg(uint32_t *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s >> 8;
}

/* Float reference: exact area average over the same source/destination intervals. */
static double axis_overlap(int sn, int dn, int i, int j)
{
    double lo = (double)i * sn / dn, hi = (double)(i + 1) * sn / dn;
    double a = j > lo ? j : lo, b = (j + 1) < hi ? (j + 1) : hi;

    return b > a ? b - a : 0;
}

static int max_err_vs_reference(int sw, int sh, int dw, int dh, uint32_t seed)
{
    uint8_t *s = malloc((size_t)sw * sh * 4), *d = malloc((size_t)dw * dh * 4);
    int x, y, c, worst = 0, i, j;

    for (i = 0; i < sw * sh * 4; i++)
        s[i] = (uint8_t)lcg(&seed);
    if (img_scale(s, sw, sh, d, dw, dh) != 0)
        worst = 999;
    for (y = 0; y < dh && worst < 999; y++)
        for (x = 0; x < dw; x++)
            for (c = 0; c < 4; c++) {
                double acc = 0, wsum = 0, ref;
                int e;

                for (j = 0; j < sh; j++) {
                    double wy = axis_overlap(sh, dh, y, j);
                    if (wy == 0)
                        continue;
                    for (i = 0; i < sw; i++) {
                        double w = wy * axis_overlap(sw, dw, x, i);
                        acc += w * s[((size_t)j * sw + i) * 4 + c];
                        wsum += w;
                    }
                }
                ref = acc / wsum;
                e = (int)ceil(fabs(ref - d[((size_t)y * dw + x) * 4 + c]) - 1e-9);
                if (e > worst)
                    worst = e;
            }
    free(s);
    free(d);
    return worst;
}

static void test_matches_float_reference(void)
{
    /* within 1 level of the exact area average, down, up and mixed */
    CHECK(max_err_vs_reference(37, 23, 11, 9, 1) <= 1);
    CHECK(max_err_vs_reference(11, 9, 37, 23, 2) <= 1);
    CHECK(max_err_vs_reference(64, 80, 17, 100, 3) <= 1);
    CHECK(max_err_vs_reference(200, 5, 3, 5, 4) <= 1);
    CHECK(max_err_vs_reference(5, 200, 5, 3, 5) <= 1);
}

static void test_identity_and_errors(void)
{
    uint8_t *s = solid(4, 3, 1, 2, 3, 4), d[48];

    memset(d, 0, sizeof d);
    CHECK_INT(img_scale(s, 4, 3, d, 4, 3), 0);
    CHECK(memcmp(s, d, 48) == 0);
    CHECK_INT(img_scale(NULL, 4, 3, d, 4, 3), -1);
    CHECK_INT(img_scale(s, 0, 3, d, 4, 3), -1);
    CHECK_INT(img_scale(s, 4, 3, d, 4, -1), -1);
    CHECK_INT(img_cover_crop(s, 4, 3, NULL, 4, 3), -1);
    free(s);
}

static void paint(uint8_t *p, int w, int h, int x, int y, const uint8_t rgb[3])
{
    (void)h;
    p[((size_t)y * w + x) * 4] = rgb[0];
    p[((size_t)y * w + x) * 4 + 1] = rgb[1];
    p[((size_t)y * w + x) * 4 + 2] = rgb[2];
    p[((size_t)y * w + x) * 4 + 3] = 255;
}

static void test_cover_crop_geometry(void)
{
    static const uint8_t R[3] = {255, 0, 0}, G[3] = {0, 255, 0}, B[3] = {0, 0, 255};
    uint8_t *s, *d = malloc(180 * 270 * 4);
    int x, y;

    /* Wide 1000x500: window is 333 wide at x0 = 333 -> columns 333..665 are the green band. */
    s = malloc(1000 * 500 * 4);
    for (y = 0; y < 500; y++)
        for (x = 0; x < 1000; x++)
            paint(s, 1000, 500, x, y, x < 333 ? R : x < 666 ? G : B);
    CHECK_INT(img_cover_crop(s, 1000, 500, d, 180, 270), 0);
    CHECK(all_pixels(d, 180 * 270, 0, 255, 0, 255));
    free(s);

    /* Tall 500x1000: window is 750 tall at y0 = 125 -> rows 125..874 are green. */
    s = malloc(500 * 1000 * 4);
    for (y = 0; y < 1000; y++)
        for (x = 0; x < 500; x++)
            paint(s, 500, 1000, x, y, y < 125 ? R : y < 875 ? G : B);
    CHECK_INT(img_cover_crop(s, 500, 1000, d, 180, 270), 0);
    CHECK(all_pixels(d, 180 * 270, 0, 255, 0, 255));
    free(s);

    /* Same aspect as the destination: no crop, plain scale (left red half stays left). */
    s = malloc(360 * 540 * 4);
    for (y = 0; y < 540; y++)
        for (x = 0; x < 360; x++)
            paint(s, 360, 540, x, y, x < 180 ? R : B);
    CHECK_INT(img_cover_crop(s, 360, 540, d, 180, 270), 0);
    CHECK_INT(d[0], 255);
    CHECK_INT(d[(179) * 4 + 2], 255);
    free(s);

    /* Source smaller than the destination: still fills, crops, upscales. */
    s = solid(30, 10, 9, 8, 7, 255);
    CHECK_INT(img_cover_crop(s, 30, 10, d, 180, 270), 0);
    CHECK(all_pixels(d, 180 * 270, 9, 8, 7, 255));
    free(s);
    free(d);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    test_solid_stays_exact();
    test_known_averages();
    test_matches_float_reference();
    test_identity_and_errors();
    test_cover_crop_geometry();
    return TEST_DONE("scale");
}
