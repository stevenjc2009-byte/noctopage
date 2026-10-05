/* img/decode on images made at run time: GIF and BMP (MuPDF fallback), a CMYK JPEG (turbojpeg
 * cannot, MuPDF does), PNG variants (straight alpha, gray, 16-bit, palette) and the 128 MiB
 * limit, using headers that claim a huge size and carry no pixels. */
#include "test.h"

#include "img/decode.h"

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

static int same_px(const uint8_t *p, int r, int g, int b, int a, int tol)
{
    return abs(p[0] - r) <= tol && abs(p[1] - g) <= tol && abs(p[2] - b) <= tol &&
           abs(p[3] - a) <= tol;
}

/* ---- hand-made images ---- */
static const uint8_t GIF1[] = {0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00, 0x01, 0x00, 0x80, 0x00,
                               0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00,
                               0x01, 0x00, 0x01, 0x00, 0x00, 0x02, 0x02, 0x44, 0x01, 0x00, 0x3B};

static size_t make_bmp(uint8_t *o)
{
    /* 3x2, 24-bit, rows bottom-up, padded to 12 bytes: top = red green blue, bottom = white black grey */
    static const uint8_t bottom[9] = {255, 255, 255, 0, 0, 0, 32, 64, 128}; /* B G R per pixel */
    static const uint8_t top[9] = {0, 0, 255, 0, 255, 0, 255, 0, 0};
    uint32_t total = 54 + 24;

    memset(o, 0, total);
    o[0] = 'B';
    o[1] = 'M';
    o[2] = (uint8_t)total;
    o[10] = 54;
    o[14] = 40;
    o[18] = 3;
    o[22] = 2;
    o[26] = 1;
    o[28] = 24;
    memcpy(o + 54, bottom, 9);
    memcpy(o + 54 + 12, top, 9);
    return total;
}

static void test_gif_bmp(void)
{
    uint8_t bmp[80];
    size_t bn = make_bmp(bmp);
    img_t im;
    char err[160];
    int w, h;

    CHECK_INT(img_probe(GIF1, sizeof GIF1, &w, &h), 0);
    CHECK(w == 1 && h == 1);
    CHECK_INT(dec(GIF1, sizeof GIF1, 0, 0, &im, err), 0);
    CHECK(im.rgba && im.w == 1 && im.h == 1 && same_px(im.rgba, 255, 255, 255, 255, 0));
    img_free(&im);

    CHECK_INT(img_probe(bmp, bn, &w, &h), 0);
    CHECK(w == 3 && h == 2);
    if (dec(bmp, bn, 0, 0, &im, err) != 0) {
        CHECK(!"bmp decode");
        printf("  bmp: %s\n", err);
    } else {
        CHECK(im.w == 3 && im.h == 2);
        CHECK(same_px(px(&im, 0, 0), 255, 0, 0, 255, 0)); /* top row, left to right */
        CHECK(same_px(px(&im, 1, 0), 0, 255, 0, 255, 0));
        CHECK(same_px(px(&im, 2, 0), 0, 0, 255, 255, 0));
        CHECK(same_px(px(&im, 0, 1), 255, 255, 255, 255, 0));
        CHECK(same_px(px(&im, 1, 1), 0, 0, 0, 255, 0));
        CHECK(same_px(px(&im, 2, 1), 128, 64, 32, 255, 0));
        img_free(&im);
    }
    expect_size(bmp, bn, 2, 0, 2, 1, 3, 2);
    expect_size(bmp, bn, 9, 9, 3, 2, 3, 2); /* no upscale on the fallback path either */
    CHECK_INT(img_decode(bmp, bn - 20, 0, 0, &im, err, sizeof err) <= 0, 1); /* truncated: no crash */
    img_free(&im);
}

static const uint8_t CMYK_JPG[] = {
    0xff, 0xd8, 0xff, 0xee, 0x00, 0x0e, 0x41, 0x64, 0x6f, 0x62, 0x65, 0x00, 0x64, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xff, 0xdb, 0x00, 0x43, 0x00, 0x10, 0x0b, 0x0c, 0x0e, 0x0c, 0x0a, 0x10, 0x0e, 0x0d,
    0x0e, 0x12, 0x11, 0x10, 0x13, 0x18, 0x28, 0x1a, 0x18, 0x16, 0x16, 0x18, 0x31, 0x23, 0x25, 0x1d,
    0x28, 0x3a, 0x33, 0x3d, 0x3c, 0x39, 0x33, 0x38, 0x37, 0x40, 0x48, 0x5c, 0x4e, 0x40, 0x44, 0x57,
    0x45, 0x37, 0x38, 0x50, 0x6d, 0x51, 0x57, 0x5f, 0x62, 0x67, 0x68, 0x67, 0x3e, 0x4d, 0x71, 0x79,
    0x70, 0x64, 0x78, 0x5c, 0x65, 0x67, 0x63, 0xff, 0xc0, 0x00, 0x14, 0x08, 0x00, 0x08, 0x00, 0x08,
    0x04, 0x43, 0x11, 0x00, 0x4d, 0x11, 0x00, 0x59, 0x11, 0x00, 0x4b, 0x11, 0x00, 0xff, 0xc4, 0x00,
    0x1f, 0x00, 0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0xff, 0xc4,
    0x00, 0xb5, 0x10, 0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04, 0x04, 0x00,
    0x00, 0x01, 0x7d, 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13,
    0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15,
    0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25,
    0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46,
    0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66,
    0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86,
    0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4,
    0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2,
    0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9,
    0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5,
    0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xff, 0xda, 0x00, 0x0e, 0x04, 0x43, 0x00, 0x4d, 0x00, 0x59, 0x00,
    0x4b, 0x00, 0x00, 0x3f, 0x00, 0xf4, 0x0a, 0xf4, 0x0a, 0xf4, 0x0a, 0xf4, 0x0a, 0xff, 0xd9,
};

static void test_cmyk_jpeg(void)
{
    img_t im;
    char err[160];
    int w, h;

    CHECK_INT(img_probe(CMYK_JPG, sizeof CMYK_JPG, &w, &h), 0);
    CHECK(w == 8 && h == 8);
    CHECK_INT(dec(CMYK_JPG, sizeof CMYK_JPG, 0, 0, &im, err), 0); /* turbojpeg cannot: MuPDF does */
    CHECK(im.rgba && im.w == 8 && im.h == 8 && im.rgba[3] == 255);
    img_free(&im);
    expect_size(CMYK_JPG, sizeof CMYK_JPG, 4, 0, 4, 4, 8, 8);
}

/* ---- PNG variants, written with libpng's simplified writer ---- */
static uint8_t *enc_png(png_uint_32 fmt, int w, int h, const void *buf, const void *cmap,
                        int entries, int to8, size_t *len)
{
    png_image im;
    png_alloc_size_t sz = 0;
    uint8_t *mem;

    memset(&im, 0, sizeof im);
    im.version = PNG_IMAGE_VERSION;
    im.width = (png_uint_32)w;
    im.height = (png_uint_32)h;
    im.format = fmt;
    im.colormap_entries = (png_uint_32)entries;
    if (!png_image_write_to_memory(&im, NULL, &sz, to8, buf, 0, cmap))
        return NULL;
    mem = malloc(sz);
    if (!mem || !png_image_write_to_memory(&im, mem, &sz, to8, buf, 0, cmap)) {
        free(mem);
        return NULL;
    }
    *len = sz;
    return mem;
}

static void test_png_variants(void)
{
    static const uint8_t rgba[8] = {200, 100, 50, 128, 1, 2, 3, 255};
    static const uint8_t gray[6] = {0, 128, 255, 255, 128, 0};
    static const uint16_t g16[2] = {0, 65535};
    static const uint8_t cmap[9] = {255, 0, 0, 0, 255, 0, 0, 0, 255}, idx[4] = {0, 1, 2, 1};
    uint8_t *p;
    size_t n;
    img_t im;
    char err[160];

    p = enc_png(PNG_FORMAT_RGBA, 2, 1, rgba, NULL, 0, 0, &n);
    CHECK(p != NULL);
    if (p && dec(p, n, 0, 0, &im, err) == 0) {
        CHECK(same_px(px(&im, 0, 0), 200, 100, 50, 128, 0)); /* straight, not premultiplied */
        CHECK(same_px(px(&im, 1, 0), 1, 2, 3, 255, 0));
        img_free(&im);
    } else {
        CHECK(!"rgba png");
    }
    free(p);

    p = enc_png(PNG_FORMAT_GRAY, 3, 2, gray, NULL, 0, 0, &n);
    CHECK(p != NULL);
    if (p && dec(p, n, 0, 0, &im, err) == 0) {
        CHECK(same_px(px(&im, 0, 0), 0, 0, 0, 255, 0));
        CHECK(same_px(px(&im, 1, 0), 128, 128, 128, 255, 0));
        CHECK(same_px(px(&im, 2, 1), 0, 0, 0, 255, 0));
        img_free(&im);
    } else {
        CHECK(!"gray png");
    }
    free(p);

    p = enc_png(PNG_FORMAT_LINEAR_Y, 2, 1, g16, NULL, 0, 0, &n); /* a 16-bit PNG */
    CHECK(p != NULL);
    if (p && dec(p, n, 0, 0, &im, err) == 0) {
        CHECK(same_px(px(&im, 0, 0), 0, 0, 0, 255, 0));
        CHECK(same_px(px(&im, 1, 0), 255, 255, 255, 255, 0));
        img_free(&im);
    } else {
        CHECK(!"16-bit png");
    }
    free(p);

    p = enc_png(PNG_FORMAT_RGB_COLORMAP, 2, 2, idx, cmap, 3, 0, &n); /* palette */
    CHECK(p != NULL);
    if (p && dec(p, n, 0, 0, &im, err) == 0) {
        CHECK(same_px(px(&im, 0, 0), 255, 0, 0, 255, 0));
        CHECK(same_px(px(&im, 1, 0), 0, 255, 0, 255, 0));
        CHECK(same_px(px(&im, 0, 1), 0, 0, 255, 255, 0));
        img_free(&im);
    } else {
        CHECK(!"palette png");
    }
    free(p);
}

/* ---- the 128 MiB limit, with PNG files that are only a header (no pixel data) ---- */
static uint32_t crc_of(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    size_t i;
    int k;

    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static size_t chunk(uint8_t *o, const char *type, const uint8_t *body, uint32_t blen)
{
    uint32_t crc;

    o[0] = (uint8_t)(blen >> 24);
    o[1] = (uint8_t)(blen >> 16);
    o[2] = (uint8_t)(blen >> 8);
    o[3] = (uint8_t)blen;
    memcpy(o + 4, type, 4);
    if (blen)
        memcpy(o + 8, body, blen);
    crc = crc_of(o + 4, 4 + blen);
    o[8 + blen] = (uint8_t)(crc >> 24);
    o[9 + blen] = (uint8_t)(crc >> 16);
    o[10 + blen] = (uint8_t)(crc >> 8);
    o[11 + blen] = (uint8_t)crc;
    return 12 + blen;
}

static size_t header_only_png(uint8_t *o, uint32_t w, uint32_t h)
{
    uint8_t ihdr[13] = {0, 0, 0, 0, 0, 0, 0, 0, 8, 6, 0, 0, 0}; /* 8-bit RGBA */
    static const uint8_t idat[4] = {0x78, 0x01, 0x00, 0x00};
    size_t n = 8;
    int i;

    for (i = 0; i < 4; i++) {
        ihdr[i] = (uint8_t)(w >> (24 - 8 * i));
        ihdr[4 + i] = (uint8_t)(h >> (24 - 8 * i));
    }
    memcpy(o, "\x89PNG\r\n\x1a\n", 8);
    n += chunk(o + n, "IHDR", ihdr, 13);
    n += chunk(o + n, "IDAT", idat, 4);
    n += chunk(o + n, "IEND", NULL, 0);
    return n;
}

static void test_size_limit(void)
{
    uint8_t buf[128];
    size_t n;
    img_t im;
    char err[160];
    int w, h;

    n = header_only_png(buf, 20000, 20000); /* 1.6 GB of RGBA */
    CHECK_INT(img_probe(buf, n, &w, &h), 0);
    CHECK(w == 20000 && h == 20000);
    CHECK_INT(dec(buf, n, 0, 0, &im, err), -1);
    CHECK(strstr(err, "too large") != NULL);
    CHECK(im.rgba == NULL);

    n = header_only_png(buf, 8192, 4097); /* one row over 128 MiB */
    CHECK_INT(dec(buf, n, 0, 0, &im, err), -1);
    CHECK(strstr(err, "too large") != NULL);
    CHECK_INT(dec(buf, n, 100, 100, &im, err), -1); /* PNG is decoded whole, so a limit does not help */
    CHECK(strstr(err, "too large") != NULL);

    n = header_only_png(buf, 8192, 4096); /* exactly 128 MiB: allowed, fails later on the bad data */
    CHECK_INT(dec(buf, n, 0, 0, &im, err), -1);
    CHECK(strstr(err, "too large") == NULL);
    CHECK(im.rgba == NULL);
}

/* A real JPEG whose SOF claims 20000 x 20000: refused at full size, a scaled decode is allowed. */
static void test_jpeg_size_limit(void)
{
    size_t n, i;
    uint8_t *d = slurp(FX "tall_800x9000.jpg", &n);
    img_t im;
    char err[160];

    CHECK(d != NULL);
    for (i = 2; d && i + 9 < n; i++)
        if (d[i] == 0xFF && (d[i + 1] == 0xC0 || d[i + 1] == 0xC2)) {
            d[i + 5] = 20000 >> 8, d[i + 6] = 20000 & 255; /* height */
            d[i + 7] = 20000 >> 8, d[i + 8] = 20000 & 255; /* width */
            break;
        }
    if (d) {
        CHECK_INT(dec(d, n, 0, 0, &im, err), -1);
        CHECK(strstr(err, "too large") != NULL);
        CHECK(im.rgba == NULL);
        CHECK_INT(dec(d, n, 40, 40, &im, err) <= 0, 1); /* data is short: error or image, no crash */
        img_free(&im);
    }
    free(d);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    test_gif_bmp();
    test_cmyk_jpeg();
    test_png_variants();
    test_size_limit();
    test_jpeg_size_limit();
    return TEST_DONE("decode_gen");
}
