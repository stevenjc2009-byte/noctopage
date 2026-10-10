/* Host test for src/doc/mdoc*.c (MuPDF wrapper). Run from tests/: fixtures are relative. */
#include "test.h"

#include "doc/mdoc_internal.h"
#include "mdoc_testdata.h"

#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FIX "fixtures/"
#define GEN_DIR "build-host-mdoc/"

static const mdoc_layout_t LAY_A = {300, 400, 14.0f, 12.0f, 1.2f, 0};
static const mdoc_layout_t LAY_B = {300, 400, 20.0f, 12.0f, 1.2f, 0}; /* size-only change */
static const mdoc_layout_t LAY_C = {360, 300, 17.0f, 30.0f, 1.6f, 1}; /* CSS change: reopen */

/* EPUB_FILES + 3 chapters x 40 paragraphs, each starting with a unique marker cXpYY. No fz_try:
 * an uncaught MuPDF error exits the test process with a failure status. */
static void make_epub(const char *path)
{
    static char page[32768];
    fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    fz_zip_writer *z = fz_new_zip_writer(ctx, path);
    int i, p;
    for (i = 0; i < 7; i++) {
        char name[16] = "c0.xhtml";
        size_t n = (size_t)snprintf(page, sizeof page, "%s", i < 4 ? EPUB_FILES[i][1] : "");
        fz_buffer *b;
        if (i >= 4) {
            name[1] = (char)('0' + i - 3);
            n = (size_t)snprintf(page, sizeof page, "<html xmlns=\"http://www.w3.org/1999/xhtml\">"
                                 "<body><h1>Chapter %d</h1>", i - 3);
            for (p = 0; p < 40; p++)
                n += (size_t)snprintf(page + n, sizeof page - n, "<p id=\"p%d\">c%dp%02d lorem "
                                      "ipsum dolor sit amet, consectetur adipiscing elit, sed do "
                                      "eiusmod tempor incididunt ut labore et dolore aliqua.</p>",
                                      p, i - 3, p);
            n += (size_t)snprintf(page + n, sizeof page - n, "</body></html>");
        }
        b = fz_new_buffer_from_copied_data(ctx, (const unsigned char *)page, n);
        fz_write_zip_entry(ctx, z, i < 4 ? EPUB_FILES[i][0] : name, b, 0);
        fz_drop_buffer(ctx, b);
    }
    fz_close_zip_writer(ctx, z);
    fz_drop_zip_writer(ctx, z);
    fz_drop_context(ctx);
}

static int write_file(const char *path, const void *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    int ok = f && fwrite(data, 1, n, f) == n;
    return (f && fclose(f) == 0) && ok;
}

/* Opens a fixture; without it the rest of the test is meaningless, so stop here. */
static mdoc_t *must_open(const char *path)
{
    char err[256] = "";
    mdoc_t *d = mdoc_open(path, err, sizeof err);
    if (!d) {
        printf("FAIL cannot open %s: %s\n", path, err);
        exit(1);
    }
    return d;
}

static void strip(const char *s, char *out, size_t cap)
{
    size_t n = 0;
    for (; *s && n + 1 < cap; s++)
        if ((unsigned char)*s > 32 && *s != '-')
            out[n++] = *s;
    out[n] = '\0';
}

static int darker(const mdoc_image_t *im, int lim) /* pixels with any channel < lim */
{
    int i, n = 0;
    for (i = 0; i < im->w * im->h; i++)
        n += im->rgba[i * 4] < lim || im->rgba[i * 4 + 1] < lim || im->rgba[i * 4 + 2] < lim;
    return n;
}

static const uint8_t *px(const mdoc_image_t *im, int x, int y)
{
    return im->rgba + ((size_t)y * im->w + x) * 4;
}

/* True if `sig` (a page's first visible characters in the old layout) starts on page q. */
static int starts_on(mdoc_t *d, int q, const char *sig)
{
    static char t[65536], s1[65536], s2[131072];
    const char *hit;
    mdoc__page_text(d, q, t, sizeof t);
    strip(t, s1, sizeof s1);
    t[0] = '\0';
    if (q + 1 < mdoc_page_count(d))
        mdoc__page_text(d, q + 1, t, sizeof t);
    snprintf(s2, sizeof s2, "%s", s1);
    strip(t, s2 + strlen(s2), sizeof s2 - strlen(s2));
    hit = strstr(s2, sig);
    return hit && (size_t)(hit - s2) < strlen(s1);
}

static int pages_with(mdoc_t *d, int font_family, float spacing, float margin)
{
    mdoc_layout_t l = {300, 400, 14.0f, margin, spacing, font_family};
    return mdoc_set_layout(d, &l) == 0 ? mdoc_page_count(d) : -1;
}

static void test_open_meta(void)
{
    static const struct { const char *path, *title, *author, *p1; int reflow; } T[5] = {
        {FIX "minimal.epub", "Fixture Book", "Noctopage Tests", NULL, 1},
        {FIX "minimal.fb2", "Fixture FB2", NULL, "Second Section", 1}, /* p1: page 1 start */
        {GEN_DIR "cover.fb2", "", NULL, "T", 1},
        {FIX "minimal.txt", "", "", NULL, 1}, /* no metadata: the caller uses the file name */
        {FIX "two_pages.pdf", "", "", NULL, 0}};
    mdoc_toc_item_t it[8];
    char buf[128];
    int i;
    for (i = 0; i < 5; i++) {
        mdoc_t *d = must_open(T[i].path);
        mdoc_image_t im = {0, 0, NULL}; /* page 0 shows text: FB2 once gave a blank page 0 */
        CHECK(mdoc_is_reflowable(d) == T[i].reflow && mdoc_page_count(d) >= 1 &&
              !mdoc_render(d, 0, 1.0f, 0, NULL, 0, 0xFFFFFF, &im) && darker(&im, 96) > 50);
        free(im.rgba);
        if (T[i].p1) /* FB2: page 0 is the cover or the first section; every section breaks */
            CHECK(mdoc_page_count(d) == 2 && mdoc__page_text(d, 1, buf, sizeof buf) > 0 &&
                  !strncmp(buf, T[i].p1, strlen(T[i].p1)));
        CHECK(mdoc_title(d, buf, sizeof buf) == (int)strlen(buf) && !strcmp(buf, T[i].title));
        if (T[i].author)
            CHECK(mdoc_author(d, buf, sizeof buf) >= 0 && strcmp(buf, T[i].author) == 0);
        if (!T[i].reflow) /* fixed layout: set_layout is a no-op; this PDF has no outline */
            CHECK(!mdoc_set_layout(d, &LAY_A) && mdoc_page_count(d) == 2 && !mdoc_toc(d, it, 8));
        mdoc_close(d);
    }
}

static void test_pages_and_positions(void)
{
    mdoc_t *d = must_open(GEN_DIR "gen.epub");
    char pos[16][MDOC_POS_MAX], sig[16][32], t[65536], s[65536];
    int pa = pages_with(d, 0, 1.2f, 12.0f), pb = -1, i, p, q[16], moved = 0, n = 0; /* LAY_A */
    CHECK(mdoc_set_layout(d, &LAY_B) == 0 && (pb = mdoc_page_count(d)) > pa); /* bigger font */
    printf("gen.epub pages: font14=%d font20=%d\n", pa, pb);
    /* The CSS really applies: more line spacing / bigger margins give more pages. */
    CHECK(pages_with(d, 0, 2.0f, 12.0f) > pages_with(d, 0, 1.0f, 12.0f));
    CHECK(pages_with(d, 1, 1.2f, 60.0f) > pages_with(d, 1, 1.2f, 0.0f));
    /* Positions under layout A for pages spread over all chapters. */
    mdoc_set_layout(d, &LAY_A);
    for (p = 1; p < pa && n < 16; p += pa / 12 + 1, n++) {
        mdoc__page_text(d, p, t, sizeof t);
        strip(t, s, sizeof s);
        snprintf(sig[n], sizeof sig[n], "%.20s", s);
        CHECK_INT(mdoc_position(d, p, pos[n]), 0);
        q[n] = p;
    }
    CHECK(n >= 8);
    CHECK_INT(mdoc_set_layout(d, &LAY_B), 0); /* layout only */
    for (i = 0; i < n; i++) {
        int r = mdoc_page_for_position(d, pos[i]);
        CHECK(r >= 0 && starts_on(d, r, sig[i]));
        moved += r != q[i];
    }
    CHECK(moved > 0); /* page indices really changed: an identity mapping cannot pass */
    CHECK(mdoc_set_layout(d, &LAY_C) == 0 && mdoc_page_count(d) != pa); /* CSS: reopen */
    for (i = 0; i < n; i++)
        CHECK(starts_on(d, mdoc_page_for_position(d, pos[i]), sig[i]));
    mdoc_close(d);
    d = must_open(GEN_DIR "gen.epub"); /* app restart: a fresh open restores them too */
    CHECK_INT(mdoc_set_layout(d, &LAY_B), 0);
    for (i = 0; i < n; i++)
        CHECK(starts_on(d, mdoc_page_for_position(d, pos[i]), sig[i]));
    CHECK_INT(mdoc_page_for_position(d, "p:3"), -1); /* fixed-layout string on reflow */
    CHECK_INT(mdoc_page_for_position(d, "r:1:x"), -1);
    CHECK_INT(mdoc_page_for_position(d, "r:0:0:0"), 0);
    CHECK(mdoc_position(d, 0, pos[0]) == 0 && strcmp(pos[0], "r:0:0:0") == 0);
    CHECK_INT(mdoc_position(d, 99999, pos[0]), -1);
    mdoc_close(d);
    d = must_open(FIX "two_pages.pdf");
    CHECK(mdoc_position(d, 1, pos[0]) == 0 && mdoc_page_for_position(d, pos[0]) == 1);
    mdoc_close(d);
}

static void test_render(void)
{
    mdoc_t *d = must_open(FIX "two_pages.pdf");
    mdoc_image_t full, cl, rot, im;
    mdoc_rect_t r = {37, 53, 200, 120}, edge = {700, 1000, 400, 400};
    float w = 0, h = 0, sc = 0.37f;
    int y, x, same = 1, ok = 1, good = 0, bad = 0;
    CHECK(mdoc_page_size(d, 0, &w, &h) == 0 && w == 612.0f && h == 792.0f);
    CHECK_INT(mdoc_render(d, 0, sc, 0, NULL, 0, 0xFFFFFF, &im), 0);
    CHECK(im.w == lroundf(w * sc) && im.h == lroundf(h * sc));
    free(im.rgba);
    CHECK_INT(mdoc_render(d, 0, 1.5f, 0, NULL, 0, 0xFFFFFF, &full), 0);
    CHECK(full.w == 918 && full.h == 1188 && darker(&full, 255) > 100);
    CHECK_INT(mdoc_render(d, 0, 1.5f, 0, &r, 0, 0xFFFFFF, &cl), 0);
    CHECK(cl.w == r.w && cl.h == r.h && darker(&cl, 255) > 100);
    for (y = 0; y < cl.h; y++)
        same &= memcmp(px(&cl, 0, y), px(&full, r.x, r.y + y), (size_t)cl.w * 4) == 0;
    CHECK(same);
    free(cl.rgba);
    CHECK_INT(mdoc_render(d, 0, 1.5f, 0, &edge, 0, 0xFFFFFF, &cl), 0); /* clipped to page */
    CHECK(cl.w == 918 - 700 && cl.h == 1188 - 1000);
    free(cl.rgba);
    /* Rotate 90 clockwise: output(x', y') = input(y', H-1-x'). Compare with the wrong way. */
    CHECK_INT(mdoc_render(d, 0, 1.5f, 90, NULL, 0, 0xFFFFFF, &rot), 0);
    CHECK(rot.w == full.h && rot.h == full.w);
    for (y = 0; y < rot.h && rot.w == full.h; y += 3)
        for (x = 0; x < rot.w; x += 3) {
            good += abs(px(&rot, x, y)[0] - px(&full, y, full.h - 1 - x)[0]);
            bad += abs(px(&rot, x, y)[0] - px(&full, full.w - 1 - y, x)[0]);
        }
    CHECK(good * 4 < bad);
    free(rot.rgba);
    /* Theme black: paper pixels are exactly the paper colour, ink pixels the ink colour. */
    CHECK_INT(mdoc_render(d, 0, 1.5f, 0, NULL, 0xFFFFFF, 0x000000, &im), 0);
    for (y = 0; y < full.w * full.h * 4; y += 4) { /* channel sums: 765 = white, 0 = black */
        int a = full.rgba[y] + full.rgba[y + 1] + full.rgba[y + 2];
        int b = im.rgba[y] + im.rgba[y + 1] + im.rgba[y + 2];
        ok &= (a != 765 || b == 0) && (a != 0 || b == 765);
    }
    CHECK(ok && darker(&im, 255) > full.w * full.h / 2);
    free(im.rgba);
    free(full.rgba);
    CHECK(mdoc_render(d, 2, 1.0f, 0, NULL, 0, 0xFFFFFF, &im) == -1 &&
          mdoc_render(d, 0, 1.0f, 45, NULL, 0, 0xFFFFFF, &im) == -1 &&
          mdoc_render(d, 0, 0.0f, 0, NULL, 0, 0xFFFFFF, &im) == -1);
    r.x = 5000;
    CHECK(mdoc_render(d, 0, 1.0f, 0, &r, 0, 0xFFFFFF, &im) == -1 && !im.rgba && !im.w);
    mdoc_close(d);
    /* Raster images keep their colours under a theme; vector art follows it. */
    CHECK(write_file(GEN_DIR "image.pdf", IMAGE_PDF, sizeof IMAGE_PDF - 1));
    d = must_open(GEN_DIR "image.pdf");
    CHECK_INT(mdoc_render(d, 0, 1.0f, 0, NULL, 0xFFFFFF, 0x000000, &im), 0);
    CHECK(im.rgba && !memcmp(px(&im, 100, 100), "\377\0\0", 3)); /* raster: unchanged red */
    CHECK(im.rgba && !memcmp(px(&im, 250, 100), "\0\377\377", 3) && !px(&im, 10, 10)[0]);
    free(im.rgba);
    mdoc_close(d);
}

static void test_toc_cover(void)
{
    mdoc_toc_item_t it[8];
    mdoc_image_t im;
    char t[8192];
    mdoc_t *d = must_open(GEN_DIR "gen.epub");
    int k, ch3 = -1;
    for (k = 0; k < 2; k++) { /* resolved again for each layout */
        CHECK(mdoc_set_layout(d, k ? &LAY_B : &LAY_A) == 0 && mdoc_toc(d, it, 8) == 4);
        CHECK(!strcmp(it[0].title, "Chapter 1") && it[0].depth == 0 && it[0].page == 0);
        CHECK(!strcmp(it[2].title, "Part 2b") && it[2].depth == 1 && it[2].page > it[1].page);
        CHECK(it[3].page > 0 && it[3].page != ch3);
        ch3 = it[3].page;
        CHECK(mdoc__page_text(d, ch3, t, sizeof t) > 0 && strncmp(t, "Chapter 3", 9) == 0);
        CHECK(mdoc__page_text(d, it[2].page, t, sizeof t) > 0 && strstr(t, "c2p20") != NULL);
    }
    CHECK_INT(mdoc_toc(d, it, 2), 2);
    mdoc_close(d);
    CHECK(mdoc_render_cover(FIX "minimal.epub", 180, 270, &im) == 0 && im.w == 180 &&
          im.h == 270 && darker(&im, 255) > 50);
    free(im.rgba);
    CHECK(mdoc_render_cover(FIX "two_pages.pdf", 180, 270, &im) == 0 && im.w <= 180 &&
          im.h <= 270 && (im.w == 180 || im.h == 270) && darker(&im, 255) > 10);
    free(im.rgba);
}

/* 4 threads, one document each, reflowable ones flipping between two CSS layouts; every
 * render's hash must equal the single-threaded reference. */
static const char *thr_path[4] = {FIX "minimal.epub", FIX "minimal.fb2", FIX "minimal.txt",
                                  FIX "two_pages.pdf"};
static unsigned thr_ref[4][2];
static int thr_bad[4];

static unsigned render_hash(mdoc_t *d, int k)
{
    mdoc_image_t im;
    unsigned h = 2166136261u;
    size_t i;
    if (mdoc_set_layout(d, k ? &LAY_C : &LAY_A) != 0 ||
        mdoc_render(d, k % mdoc_page_count(d), 1.0f, 0, NULL, 0x202020, 0xF4ECD8, &im) != 0)
        return 0;
    for (i = 0; i < (size_t)im.w * im.h * 4; i++)
        h = (h ^ im.rgba[i]) * 16777619u;
    free(im.rgba);
    return h;
}

static void *thr_main(void *arg)
{
    int i = (int)(intptr_t)arg, k;
    mdoc_t *d = mdoc_open(thr_path[i], NULL, 0);
    for (k = 0; d && k < 20; k++)
        thr_bad[i] += render_hash(d, k & 1) != thr_ref[i][k & 1];
    thr_bad[i] += d ? 0 : 99;
    mdoc_close(d);
    return NULL;
}

static void test_threads(void)
{
    pthread_t th[4];
    int i;
    for (i = 0; i < 4; i++) {
        mdoc_t *d = must_open(thr_path[i]);
        thr_ref[i][0] = render_hash(d, 0);
        thr_ref[i][1] = render_hash(d, 1);
        CHECK(thr_ref[i][0] != 0 && thr_ref[i][1] != 0);
        mdoc_close(d);
    }
    CHECK(thr_ref[0][0] != thr_ref[0][1] && thr_ref[1][0] != thr_ref[1][1]);
    for (i = 0; i < 4; i++)
        pthread_create(&th[i], NULL, thr_main, (void *)(intptr_t)i);
    for (i = 0; i < 4; i++) {
        pthread_join(th[i], NULL);
        CHECK_INT(thr_bad[i], 0);
    }
}

static void test_errors(void)
{
    static char buf[1024];
    char err[256] = "", e2[64] = "", e3[64] = "";
    FILE *f = fopen(FIX "two_pages.pdf", "rb");
    size_t n = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
    const char *xref = strstr(buf, "\nxref");
    mdoc_image_t im = {0, 0, NULL};
    mdoc_t *d;
    if (f)
        fclose(f);
    CHECK(n > 400 && write_file(GEN_DIR "trunc.pdf", buf, 400)); /* cut mid-object */
    d = mdoc_open(GEN_DIR "trunc.pdf", err, sizeof err);
    CHECK(d == NULL && err[0] != '\0');
    /* Cut just before the xref: every object survives and MuPDF repairs the file. */
    CHECK(xref && write_file(GEN_DIR "trunc2.pdf", buf, (size_t)(xref - buf)));
    d = mdoc_open(GEN_DIR "trunc2.pdf", err, sizeof err);
    CHECK(d && mdoc_page_count(d) == 2 && mdoc_render(d, 1, 0.5f, 0, NULL, 0, 0xFFFFFF, &im) == 0);
    free(im.rgba);
    mdoc_close(d);
    CHECK(write_file(GEN_DIR "garbage.epub", "PK\3\4 this is not a zip at all", 30));
    CHECK(mdoc_open(GEN_DIR "garbage.epub", e2, sizeof e2) == NULL && e2[0] != '\0');
    CHECK(mdoc_open(GEN_DIR "missing.pdf", e3, sizeof e3) == NULL && e3[0] != '\0');
    CHECK_INT(mdoc_render_cover(GEN_DIR "garbage.epub", 10, 10, &im), -1);
    CHECK(mdoc_page_count(NULL) == -1 && mdoc_set_layout(NULL, &LAY_A) == -1);
}

static void test_find(void)
{
    mdoc_t *d = must_open(GEN_DIR "gen.epub");
    char t[65536];
    int n, p;

    CHECK_INT(mdoc_set_layout(d, &LAY_A), 0);
    n = mdoc_page_count(d);
    CHECK(n > 2);
    p = mdoc_find_page(d, 0, "c3p39"); /* unique last-paragraph marker */
    CHECK(p >= 0 && p < n);
    if (p >= 0) {
        CHECK(mdoc__page_text(d, p, t, sizeof t) > 0 && strstr(t, "c3p39") != NULL);
        CHECK_INT(mdoc_find_page(d, p, "c3p39"), p);
        CHECK_INT(mdoc_find_page(d, 0, "C3P39"), p); /* case-insensitive */
    }
    CHECK_INT(mdoc_find_page(d, n - 1, "c1p00"), -1); /* before the only match */
    CHECK_INT(mdoc_find_page(d, 0, "no such words anywhere"), -1);
    CHECK_INT(mdoc_find_page(d, 0, ""), -1);
    CHECK_INT(mdoc_find_page(NULL, 0, "x"), -1);
    CHECK_INT(mdoc_find_page(d, -1, "x"), -1);
    CHECK_INT(mdoc_find_page(d, n, "x"), -1);
    mdoc_close(d);
}

int main(void)
{
    const mdoc_layout_t big = {960, 544, 20.0f, 24.0f, 1.5f, 0};
    char err[128] = "";
    int i;
    CHECK(mdoc_open(FIX "two_pages.pdf", err, sizeof err) == NULL && err[0] != '\0');
    CHECK(mdoc_global_init() == 0 && mdoc_global_init() == 0);
    make_epub(GEN_DIR "gen.epub");
    CHECK(write_file(GEN_DIR "cover.fb2", COVER_FB2, sizeof COVER_FB2 - 1));
    test_open_meta();
    test_pages_and_positions();
    test_find();
    test_render();
    test_toc_cover();
    test_threads();
    test_errors();
    for (i = 0; i < 5; i++) { /* timing: open + CSS layout + count + render page 0 */
        struct timespec t0, t1;
        mdoc_image_t im = {0, 0, NULL};
        clock_gettime(CLOCK_MONOTONIC, &t0);
        mdoc_t *d = mdoc_open(GEN_DIR "gen.epub", NULL, 0);
        CHECK(d && mdoc_set_layout(d, &big) == 0 && mdoc_page_count(d) > 0 &&
              mdoc_render(d, 0, 1.0f, 0, NULL, 0, 0xFFFFFF, &im) == 0);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        printf("epub open+layout+count+render 960x544 run %d: %.1f ms\n", i + 1,
               (double)(t1.tv_sec - t0.tv_sec) * 1e3 + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6);
        free(im.rgba);
        mdoc_close(d);
    }
    mdoc_global_shutdown();
    return TEST_DONE("mdoc");
}
