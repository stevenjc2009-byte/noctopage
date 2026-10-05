#include "img/scale.h"

#include <stdlib.h>
#include <string.h>

/* Per-axis resampling table: destination pixel i reads source pixels
 * [start[i], start[i] + cnt[i]) with 16.16 weights w[off[i] ...] summing to 65536. */
typedef struct {
    int *start;
    int *cnt;
    int *off;
    uint32_t *w;
} axis_t;

static void axis_free(axis_t *a)
{
    free(a->start);
    free(a->cnt);
    free(a->off);
    free(a->w);
    memset(a, 0, sizeof *a);
}

/* Source pixel j spans [j*dn, (j+1)*dn) and destination pixel i spans [i*sn, (i+1)*sn) on
 * a common integer axis of length sn*dn, so the overlaps are exact integers summing to sn. */
static int axis_build(axis_t *a, int sn, int dn)
{
    int i, taps = sn / dn + 2;
    int total = 0;

    memset(a, 0, sizeof *a);
    a->start = malloc((size_t)dn * sizeof(int));
    a->cnt = malloc((size_t)dn * sizeof(int));
    a->off = malloc((size_t)dn * sizeof(int));
    a->w = malloc(((size_t)dn * (size_t)taps) * sizeof(uint32_t));
    if (!a->start || !a->cnt || !a->off || !a->w) {
        axis_free(a);
        return -1;
    }
    for (i = 0; i < dn; i++) {
        int64_t lo = (int64_t)i * sn, hi = lo + sn;
        int j0 = (int)(lo / dn), j1 = (int)((hi - 1) / dn);
        uint32_t sum = 0;
        int j, big = 0;

        a->start[i] = j0;
        a->cnt[i] = j1 - j0 + 1;
        a->off[i] = total;
        for (j = j0; j <= j1; j++) {
            int64_t s0 = (int64_t)j * dn, s1 = s0 + dn;
            int64_t o = (hi < s1 ? hi : s1) - (lo > s0 ? lo : s0);
            uint32_t wq = (uint32_t)((((uint64_t)o << 16) + (uint64_t)sn / 2) / (uint64_t)sn);
            a->w[total + (j - j0)] = wq;
            sum += wq;
            if (wq > a->w[total + big])
                big = j - j0;
        }
        /* Rounding residue goes to the heaviest tap so the weights sum to exactly 65536. */
        a->w[total + big] += 65536u - sum;
        total += a->cnt[i];
    }
    return 0;
}

/* Horizontal pass of one source row into hrow (dw*4 values, 8.8 fixed point). */
static void hpass(const uint8_t *row, const axis_t *ax, int dw, uint16_t *hrow)
{
    int x, k;

    for (x = 0; x < dw; x++) {
        const uint8_t *p = row + (size_t)ax->start[x] * 4;
        const uint32_t *w = ax->w + ax->off[x];
        uint32_t r = 0, g = 0, b = 0, a = 0;

        for (k = 0; k < ax->cnt[x]; k++, p += 4) {
            r += w[k] * p[0];
            g += w[k] * p[1];
            b += w[k] * p[2];
            a += w[k] * p[3];
        }
        hrow[x * 4 + 0] = (uint16_t)((r + 128) >> 8);
        hrow[x * 4 + 1] = (uint16_t)((g + 128) >> 8);
        hrow[x * 4 + 2] = (uint16_t)((b + 128) >> 8);
        hrow[x * 4 + 3] = (uint16_t)((a + 128) >> 8);
    }
}

static int scale_rect(const uint8_t *src, size_t stride, int sw, int sh, uint8_t *dst, int dw,
                      int dh)
{
    axis_t ax, ay;
    uint16_t *hrow;
    uint32_t *acc;
    int y, last = -1, rc = -2;

    if (axis_build(&ax, sw, dw) != 0)
        return -2;
    if (axis_build(&ay, sh, dh) != 0) {
        axis_free(&ax);
        return -2;
    }
    hrow = malloc((size_t)dw * 4 * sizeof(uint16_t));
    acc = malloc((size_t)dw * 4 * sizeof(uint32_t));
    if (!hrow || !acc)
        goto done;

    for (y = 0; y < dh; y++) {
        const uint32_t *w = ay.w + ay.off[y];
        size_t n = (size_t)dw * 4, i;
        int k;

        memset(acc, 0, n * sizeof(uint32_t));
        for (k = 0; k < ay.cnt[y]; k++) {
            int sy = ay.start[y] + k;
            if (sy != last) { /* a boundary row shared by two dest rows is filtered once */
                hpass(src + (size_t)sy * stride, &ax, dw, hrow);
                last = sy;
            }
            for (i = 0; i < n; i++)
                acc[i] += w[k] * hrow[i];
        }
        /* weights sum to 65536 and hrow <= 65280, so acc + 2^23 stays below 2^32 */
        for (i = 0; i < n; i++)
            dst[(size_t)y * dw * 4 + i] = (uint8_t)((acc[i] + (1u << 23)) >> 24);
    }
    rc = 0;
done:
    free(hrow);
    free(acc);
    axis_free(&ax);
    axis_free(&ay);
    return rc;
}

int img_scale(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh)
{
    if (!src || !dst || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
        return -1;
    if (sw == dw && sh == dh) {
        memcpy(dst, src, (size_t)sw * (size_t)sh * 4);
        return 0;
    }
    return scale_rect(src, (size_t)sw * 4, sw, sh, dst, dw, dh);
}

int img_cover_crop(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh)
{
    int cw = sw, ch = sh, x0 = 0, y0 = 0;

    if (!src || !dst || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
        return -1;
    if ((int64_t)sw * dh > (int64_t)sh * dw) { /* source wider than destination: crop sides */
        cw = (int)(((int64_t)sh * dw + dh / 2) / dh);
        if (cw < 1)
            cw = 1;
        x0 = (sw - cw) / 2;
    } else { /* source taller (or equal): crop top and bottom */
        ch = (int)(((int64_t)sw * dh + dw / 2) / dw);
        if (ch < 1)
            ch = 1;
        y0 = (sh - ch) / 2;
    }
    return scale_rect(src + ((size_t)y0 * sw + (size_t)x0) * 4, (size_t)sw * 4, cw, ch, dst, dw,
                      dh);
}
