/* comic/archive on archives generated at run time with libarchive's writer: many entries,
 * solid 7z, UTF-8 names in every format, read order (sequential must be one pass), the 64 MiB
 * entry cap. No RAR here: nothing on this machine can write RAR, so RAR is only covered by the
 * junk-header checks in test_archive.c. */
#include "test.h"

#include "comic/archive.h"

#include <archive.h>
#include <archive_entry.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { ZIP, SEVENZ, TAR };
static const char *FMT[] = {"zip", "7z", "tar"};

#define JP3 "\xe3\x83\x9a\xe3\x83\xbc\xe3\x82\xb8" "03.bin" /* ページ03.bin */
static const size_t SIZES[] = {100, 0, 70000, 300000, 5, 1, 262144, 262145, 90000, 12, 1000000, 33};
#define NENT ((int)(sizeof SIZES / sizeof SIZES[0]))

static void name_of(int i, char *out, size_t cap)
{
    if (i == 2)
        snprintf(out, cap, "%s", "d/" JP3);
    else
        snprintf(out, cap, "d/f%02d.bin", i);
}

/* Position k of the listing -> index into SIZES. 7z stores empty files after all the others. */
static int gen_index(int fmt, int k)
{
    int i, seen = 0;

    if (fmt != SEVENZ)
        return k;
    for (i = 0; i < NENT; i++)
        if (SIZES[i] && seen++ == k)
            return i;
    for (i = 0; i < NENT; i++)
        if (!SIZES[i] && seen++ == k)
            return i;
    return -1;
}

/* Deterministic, half noise / half runs, so zip and 7z both really compress and expand. */
static void gen(int i, size_t n, uint8_t *p)
{
    uint32_t s = 12345u + (uint32_t)i * 7919u;
    size_t k;

    for (k = 0; k < n; k++) {
        s = s * 1664525u + 1013904223u;
        p[k] = (k / 4096) % 2 ? (uint8_t)(i + 1) : (uint8_t)(s >> 16);
    }
}

static int add(struct archive *w, const char *name, size_t n, int fill_index)
{
    struct archive_entry *e = archive_entry_new();
    uint8_t *buf = malloc(n ? n : 1);
    int rc;

    archive_entry_set_pathname_utf8(e, name);
    archive_entry_set_size(e, (la_int64_t)n);
    archive_entry_set_filetype(e, AE_IFREG);
    archive_entry_set_perm(e, 0644);
    rc = archive_write_header(w, e);
    gen(fill_index, n, buf);
    if (n && archive_write_data(w, buf, n) != (la_ssize_t)n)
        rc = -1;
    archive_entry_free(e);
    free(buf);
    return rc;
}

static struct archive *writer(const char *path, int fmt)
{
    struct archive *w = archive_write_new();

    if (fmt == ZIP) {
        archive_write_set_format_zip(w);
        archive_write_set_options(w, "zip:compression=deflate");
    } else if (fmt == SEVENZ) {
        archive_write_set_format_7zip(w);
    } else {
        archive_write_set_format_ustar(w); /* no pax header: names are raw UTF-8 bytes */
    }
    if (archive_write_open_filename(w, path) != ARCHIVE_OK) {
        archive_write_free(w);
        return NULL;
    }
    return w;
}

/* A directory entry first (must not be listed), then the NENT files. */
static int make(const char *path, int fmt)
{
    struct archive *w = writer(path, fmt);
    struct archive_entry *d;
    int i, rc = 0;
    /* The writer needs a UTF-8 locale for the Japanese name; the readers under test run in the
     * default "C" locale, like the Vita does. */
    locale_t u = newlocale(LC_CTYPE_MASK, "C.UTF-8", (locale_t)0), old = uselocale(u);

    if (!w) {
        uselocale(old);
        freelocale(u);
        return -1;
    }
    d = archive_entry_new();
    archive_entry_set_pathname_utf8(d, "d/");
    archive_entry_set_filetype(d, AE_IFDIR);
    archive_entry_set_perm(d, 0755);
    rc |= archive_write_header(w, d);
    archive_entry_free(d);
    for (i = 0; i < NENT; i++) {
        char nm[64];

        name_of(i, nm, sizeof nm);
        rc |= add(w, nm, SIZES[i], i);
    }
    rc |= archive_write_close(w);
    archive_write_free(w);
    uselocale(old);
    freelocale(u);
    return rc;
}

static int g_fmt;

/* Entry at listing position k holds the data generated for gen_index(g_fmt, k). */
static int read_matches(archive_t *a, int k)
{
    int i = gen_index(g_fmt, k);
    uint8_t *d = NULL, *want = malloc(SIZES[i] + 1);
    size_t n = 0;
    int ok;

    gen(i, SIZES[i], want);
    ok = arc_read(a, k, &d, &n) == ARC_OK && n == SIZES[i] && d && d[n] == 0 &&
         memcmp(d, want, n) == 0;
    free(d);
    free(want);
    return ok;
}

static void test_format(int fmt, const char *dir)
{
    char path[200], err[160], nm[64];
    archive_t *a;
    int i, bad = 0;
    uint32_t s = 99;
    static const int orders[][7] = {{5, 2, 7, 0, -1}, {11, 0, 11, 3, 3, 10, 1}, {9, 8, 7, 6, -1}};
    size_t k, m;

    snprintf(path, sizeof path, "%s/t.%s", dir, FMT[fmt]);
    CHECK_INT(make(path, fmt), 0);
    a = arc_open(path, err, sizeof err);
    CHECK(a != NULL);
    if (!a) {
        printf("  %s: %s\n", FMT[fmt], err);
        return;
    }
    CHECK_INT(arc_count(a), NENT); /* the directory entry is not listed */
    g_fmt = fmt;
    for (i = 0; i < NENT && i < arc_count(a); i++) {
        name_of(gen_index(fmt, i), nm, sizeof nm);
        CHECK_STR(arc_name(a, i), nm);
        CHECK_INT(arc_size(a, i), SIZES[gen_index(fmt, i)]);
    }
    for (i = 0; i < NENT; i++)
        bad += !read_matches(a, i);
    CHECK_INT(bad, 0);

    /* Random access: fixed orders (including a repeat of the same index) ... */
    for (k = 0; k < sizeof orders / sizeof orders[0]; k++)
        for (m = 0; m < 7 && orders[k][m] >= 0; m++)
            bad += !read_matches(a, orders[k][m]);
    /* ... and 200 pseudo-random hops. */
    for (i = 0; i < 200; i++) {
        s = s * 1664525u + 1013904223u;
        bad += !read_matches(a, (int)((s >> 16) % NENT));
    }
    CHECK_INT(bad, 0);
    arc_close(a);

    /* Sequential reads are ONE pass: unlink the file after entry 0 is read; the open reader
     * must carry on through the rest. Re-reading an earlier (or the same) index must reopen
     * the path, which is gone now, so it fails cleanly. */
    a = arc_open(path, err, sizeof err);
    CHECK(a != NULL);
    CHECK(read_matches(a, 0));
    unlink(path);
    for (i = 1; i < NENT; i++)
        bad += !read_matches(a, i);
    CHECK_INT(bad, 0);
    {
        uint8_t *d = NULL;
        size_t n = 0;

        CHECK_INT(arc_read(a, NENT - 1, &d, &n), ARC_E_READ); /* same index again: reopen */
        CHECK(d == NULL);
        CHECK(arc_error(a)[0] != '\0');
        CHECK_INT(arc_read(a, 0, &d, &n), ARC_E_READ);        /* earlier index: reopen */
    }
    arc_close(a);
}

static void test_size_cap(const char *dir)
{
    char path[200], err[160];
    struct archive *w;
    archive_t *a;
    uint8_t *d = NULL;
    size_t n = 0;

    snprintf(path, sizeof path, "%s/big.zip", dir);
    w = writer(path, ZIP);
    CHECK(w != NULL);
    if (!w)
        return;
    CHECK_INT(add(w, "small1.bin", 10, 1), 0);
    CHECK_INT(add(w, "exactly_cap.bin", ARC_MAX_ENTRY, 255), 0);
    CHECK_INT(add(w, "over_cap.bin", (size_t)ARC_MAX_ENTRY + 1, 255), 0);
    CHECK_INT(add(w, "small2.bin", 20, 2), 0);
    CHECK_INT(archive_write_close(w), 0);
    archive_write_free(w);

    a = arc_open(path, err, sizeof err);
    CHECK(a != NULL);
    if (!a)
        return;
    CHECK_INT(arc_count(a), 4);
    CHECK_INT(arc_read(a, 0, &d, &n), ARC_OK);
    free(d);
    d = NULL;
    CHECK_INT(arc_read(a, 1, &d, &n), ARC_OK); /* exactly 64 MiB is allowed */
    CHECK_INT(n, ARC_MAX_ENTRY);
    free(d);
    d = NULL;
    CHECK_INT(arc_read(a, 2, &d, &n), ARC_E_TOOBIG); /* one byte more is refused, not a crash */
    CHECK(d == NULL);
    CHECK(strstr(arc_error(a), "too large") != NULL);
    CHECK_INT(arc_read(a, 3, &d, &n), ARC_OK); /* and the handle is still usable */
    CHECK_INT(n, 20);
    free(d);
    arc_close(a);
    unlink(path);
}

int main(void)
{
    char dir[] = "/tmp/noct_arcgen_XXXXXX";
    int f;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (!mkdtemp(dir)) {
        CHECK(!"mkdtemp");
        return TEST_DONE("archive_gen");
    }
    for (f = ZIP; f <= TAR; f++)
        test_format(f, dir);
    test_size_cap(dir);
    rmdir(dir);
    return TEST_DONE("archive_gen");
}
