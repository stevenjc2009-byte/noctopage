/* comic/archive against the real fixtures: listing vs manifest.json, reading and decoding every
 * entry, out-of-order reads, truncated / corrupt / non-archive inputs, two threads.
 * (Generated archives, ordering at scale and the size cap live in test_archive_gen.c.) */
#include "test.h"

#include "cjson/cJSON.h"
#include "comic/archive.h"
#include "img/decode.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

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

static void spit(const char *path, const uint8_t *d, size_t n)
{
    FILE *f = fopen(path, "wb");

    if (f) {
        fwrite(d, 1, n, f);
        fclose(f);
    }
}

static int is_image_name(const char *n)
{
    const char *dot = strrchr(n, '.');

    return dot && (strcasecmp(dot, ".png") == 0 || strcasecmp(dot, ".jpg") == 0 ||
                   strcasecmp(dot, ".jpeg") == 0);
}

static int has_name(archive_t *a, const char *name)
{
    int i;

    for (i = 0; i < arc_count(a); i++)
        if (strcmp(arc_name(a, i), name) == 0)
            return 1;
    return 0;
}

typedef struct {
    uint8_t *d;
    size_t n;
} blob_t;

/* Read every entry in order into blobs; returns the number read OK. */
static int read_all(archive_t *a, blob_t *out)
{
    int i, ok = 0;

    for (i = 0; i < arc_count(a); i++)
        if (arc_read(a, i, &out[i].d, &out[i].n) == ARC_OK)
            ok++;
    return ok;
}

static void check_entry(archive_t *a, const blob_t *ref, int i, int *bad)
{
    uint8_t *d = NULL;
    size_t n = 0;

    if (arc_read(a, i, &d, &n) != ARC_OK || n != ref[i].n || memcmp(d, ref[i].d, n) != 0)
        (*bad)++;
    free(d);
}

static void test_fixture(const char *file, const cJSON *m)
{
    char path[300], err[160];
    const cJSON *order = cJSON_GetObjectItemCaseSensitive(m, "archive_order");
    const cJSON *nat = cJSON_GetObjectItemCaseSensitive(m, "natural_order");
    const cJSON *junk = cJSON_GetObjectItemCaseSensitive(m, "must_ignore");
    const cJSON *e;
    archive_t *a;
    blob_t *ref;
    int n, i, decoded = 0, bad = 0, k;
    static const int perm[] = {5, 2, 7, 0, 2, 2, 6, 1, 3, 4};

    snprintf(path, sizeof path, "fixtures/%s", file);
    a = arc_open(path, err, sizeof err);
    CHECK(a != NULL);
    if (!a) {
        printf("  %s: %s\n", file, err);
        return;
    }
    n = arc_count(a);
    if (order) { /* tar/7z/cbz written in a known order */
        CHECK_INT(n, cJSON_GetArraySize(order));
        for (i = 0, e = order->child; e && i < n; e = e->next, i++)
            CHECK_STR(arc_name(a, i), e->valuestring);
    } else {
        CHECK_INT(n, cJSON_GetArraySize(nat) + (junk ? cJSON_GetArraySize(junk) : 0));
        for (e = nat->child; e; e = e->next)
            CHECK(has_name(a, e->valuestring));
        for (e = junk ? junk->child : NULL; e; e = e->next)
            CHECK(has_name(a, e->valuestring));
    }
    CHECK_INT(cJSON_GetObjectItemCaseSensitive(m, "pages")->valueint, cJSON_GetArraySize(nat));

    ref = calloc((size_t)n + 1, sizeof *ref);
    CHECK_INT(read_all(a, ref), n);
    for (i = 0; i < n; i++) {
        CHECK_INT(ref[i].n, arc_size(a, i));
        if (is_image_name(arc_name(a, i)) && !strstr(arc_name(a, i), "__MACOSX")) {
            img_t im;

            if (img_decode(ref[i].d, ref[i].n, 0, 0, &im, err, sizeof err) == 0) {
                decoded++;
                CHECK(im.w > 0 && im.h > 0);
            } else {
                printf("  %s/%s: decode: %s\n", file, arc_name(a, i), err);
            }
            img_free(&im);
        }
    }
    CHECK(decoded > 0);

    /* Out-of-order and repeated reads give the same bytes as the sequential pass. */
    for (k = 0; k < (int)(sizeof perm / sizeof perm[0]); k++)
        check_entry(a, ref, perm[k] % n, &bad);
    for (i = n - 1; i >= 0; i--)
        check_entry(a, ref, i, &bad);
    CHECK_INT(bad, 0);

    {
        uint8_t *d = (uint8_t *)1;
        size_t l = 9;

        CHECK_INT(arc_read(a, n, &d, &l), ARC_E_INDEX);
        CHECK_INT(arc_read(a, -1, &d, &l), ARC_E_INDEX);
        CHECK(d == (uint8_t *)1 && l == 9); /* untouched on failure */
    }
    CHECK(arc_name(a, n) == NULL && arc_name(a, -1) == NULL);
    CHECK_INT(arc_size(a, n), 0);

    for (i = 0; i < n; i++)
        free(ref[i].d);
    free(ref);
    arc_close(a);
}

static void test_fixtures(void)
{
    size_t n;
    uint8_t *txt = slurp("fixtures/manifest.json", &n);
    cJSON *root;
    const cJSON *m;
    int seen = 0;

    CHECK(txt != NULL);
    if (!txt)
        return;
    txt[n] = '\0';
    root = cJSON_Parse((const char *)txt);
    CHECK(root != NULL);
    for (m = root ? root->child : NULL; m; m = m->next) {
        const char *kind = cJSON_GetObjectItemCaseSensitive(m, "kind")->valuestring;

        if (strcmp(kind, "cbz") && strcmp(kind, "cbt") && strcmp(kind, "cb7"))
            continue;
        test_fixture(m->string, m);
        seen++;
    }
    CHECK_INT(seen, 5); /* natural_order x3, mixed_case_junk, the Japanese cbz */
    cJSON_Delete(root);
    free(txt);
}

static void test_japanese_names(void)
{
    char err[160];
    archive_t *a = arc_open("fixtures/\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\xe3\x81\xae\xe6\xbc\xab"
                            "\xe7\x94\xbb \xe7\xac\xac" "1\xe5\xb7\xbb.cbz",
                            err, sizeof err);

    CHECK(a != NULL);
    if (!a)
        return;
    CHECK(has_name(a, "\xe3\x83\x9a\xe3\x83\xbc\xe3\x82\xb8" "01.png")); /* ページ01.png */
    CHECK(has_name(a, "\xe3\x83\x9a\xe3\x83\xbc\xe3\x82\xb8" "10.png"));
    arc_close(a);
}

/* Truncating or damaging a file must never crash; if it still opens, whatever reads OK must be
 * the original bytes. */
static void sweep_damage(const char *file, size_t step)
{
    char path[200], tmpdir[] = "/tmp/noct_arc_XXXXXX", cut[260], err[160];
    size_t n, len, opened = 0, failed = 0;
    uint8_t *orig, *copy;
    archive_t *ref;
    blob_t *rb;
    int i, rn;

    snprintf(path, sizeof path, "fixtures/%s", file);
    orig = slurp(path, &n);
    ref = arc_open(path, err, sizeof err);
    CHECK(orig && ref);
    if (!orig || !ref || !mkdtemp(tmpdir)) {
        free(orig);
        arc_close(ref);
        return;
    }
    rn = arc_count(ref);
    rb = calloc((size_t)rn, sizeof *rb);
    read_all(ref, rb);
    snprintf(cut, sizeof cut, "%s/x.bin", tmpdir);
    copy = malloc(n);
    for (len = 1; len < n + n / 2; len += step) {
        archive_t *a;
        size_t keep = len < n ? len : n;

        memcpy(copy, orig, n);
        if (len >= n) /* second half of the sweep: flip one byte instead of cutting */
            copy[(len - n) % n] ^= 0x5A;
        spit(cut, copy, keep);
        a = arc_open(cut, err, sizeof err);
        if (!a) {
            failed++;
            CHECK(err[0] != '\0');
            continue;
        }
        opened++;
        for (i = 0; i < arc_count(a); i++) {
            uint8_t *d = NULL;
            size_t dn = 0;

            if (arc_read(a, i, &d, &dn) == ARC_OK && len < n) { /* pure truncation: must be exact */
                int j = -1, k;
                for (k = 0; k < rn; k++)
                    if (strcmp(arc_name(a, i), arc_name(ref, k)) == 0)
                        j = k;
                CHECK(j >= 0 && dn == rb[j].n && memcmp(d, rb[j].d, dn) == 0);
            }
            free(d);
        }
        arc_close(a);
    }
    printf("  %s damage sweep: %zu opened, %zu rejected\n", file, opened, failed);
    CHECK(failed > 0);
    /* The half-way cut specifically must be rejected or read back exactly. */
    spit(cut, orig, n / 2 + 37);
    {
        archive_t *a = arc_open(cut, err, sizeof err);
        CHECK(a == NULL || arc_count(a) < rn);
        arc_close(a);
    }
    unlink(cut);
    rmdir(tmpdir);
    for (i = 0; i < rn; i++)
        free(rb[i].d);
    free(rb);
    free(copy);
    free(orig);
    arc_close(ref);
}

static void test_not_archives(void)
{
    char tmpdir[] = "/tmp/noct_arc_XXXXXX", p[260], err[160];
    static const uint8_t rar4[] = {'R', 'a', 'r', '!', 0x1A, 0x07, 0x00, 0xCF, 0x90, 0x73, 0x00,
                                   0x00, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t rar5[] = {'R', 'a', 'r', '!', 0x1A, 0x07, 0x01, 0x00, 0x33, 0x92, 0xB5,
                                   0xE5, 0x0A, 0x01, 0x05, 0x06, 0x00, 0x05, 0x01, 0x01};
    static const uint8_t sevenz[] = {'7', 'z', 0xBC, 0xAF, 0x27, 0x1C, 0x00, 0x04, 1, 2, 3, 4,
                                     5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
    static const uint8_t pk[] = {'P', 'K', 3, 4, 20, 0, 0, 0, 8, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    size_t n;
    uint8_t *txt = slurp("fixtures/minimal.txt", &n);

    CHECK(mkdtemp(tmpdir) != NULL);
    snprintf(p, sizeof p, "%s/f.bin", tmpdir);

    CHECK(arc_open("fixtures/does_not_exist.cbz", err, sizeof err) == NULL);
    CHECK(err[0] != '\0');
    CHECK(arc_open("fixtures", err, sizeof err) == NULL); /* a directory */
    CHECK(arc_open(NULL, err, sizeof err) == NULL);
    CHECK(arc_open("", NULL, 0) == NULL);
    CHECK(arc_open("fixtures/minimal.txt", err, sizeof err) == NULL);
    CHECK(err[0] != '\0');
    CHECK(arc_open("fixtures/two_pages.pdf", err, sizeof err) == NULL);

    spit(p, (const uint8_t *)"", 0);
    CHECK(arc_open(p, err, sizeof err) == NULL); /* empty file */
    spit(p, rar4, sizeof rar4);
    CHECK(arc_open(p, err, sizeof err) == NULL); /* RAR 4 signature + junk */
    spit(p, rar5, sizeof rar5);
    {
        /* a bare RAR 5 main header is a valid, empty archive; anything else must be rejected */
        archive_t *r5 = arc_open(p, err, sizeof err);

        CHECK(r5 == NULL || arc_count(r5) == 0);
        arc_close(r5);
    }
    spit(p, sevenz, sizeof sevenz);
    CHECK(arc_open(p, err, sizeof err) == NULL);
    spit(p, pk, sizeof pk);
    CHECK(arc_open(p, err, sizeof err) == NULL);
    spit(p, txt, n);
    CHECK(arc_open(p, err, sizeof err) == NULL);

    arc_close(NULL);
    CHECK_INT(arc_count(NULL), 0);
    CHECK(arc_name(NULL, 0) == NULL);
    CHECK_STR(arc_error(NULL), "");
    unlink(p);
    rmdir(tmpdir);
    free(txt);
}

typedef struct {
    const char *path;
    blob_t *ref;
    int bad;
} job_t;

static void *worker(void *arg)
{
    job_t *j = arg;
    char err[160];
    archive_t *a = arc_open(j->path, err, sizeof err);
    int round, i;

    if (!a) {
        j->bad = 1000;
        return NULL;
    }
    for (round = 0; round < 15; round++)
        for (i = 0; i < arc_count(a); i++)
            check_entry(a, j->ref, i, &j->bad);
    arc_close(a);
    return NULL;
}

static void test_two_threads(void)
{
    const char *path = "fixtures/natural_order.cb7";
    char err[160];
    archive_t *a = arc_open(path, err, sizeof err);
    blob_t ref[8] = {{0}};
    job_t j[2] = {{path, ref, 0}, {path, ref, 0}};
    pthread_t t[2];
    int i;

    CHECK(a != NULL);
    if (!a)
        return;
    CHECK_INT(read_all(a, ref), arc_count(a));
    for (i = 0; i < 2; i++)
        pthread_create(&t[i], NULL, worker, &j[i]);
    for (i = 0; i < 2; i++)
        pthread_join(t[i], NULL);
    CHECK_INT(j[0].bad, 0);
    CHECK_INT(j[1].bad, 0);
    for (i = 0; i < arc_count(a); i++)
        free(ref[i].d);
    arc_close(a);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    test_fixtures();
    test_japanese_names();
    sweep_damage("natural_order.cbz", 97);
    sweep_damage("natural_order.cb7", 61);
    sweep_damage("natural_order.cbt", 389);
    sweep_damage("mixed_case_junk.cbz", 113);
    test_not_archives();
    test_two_threads();
    return TEST_DONE("archive");
}
