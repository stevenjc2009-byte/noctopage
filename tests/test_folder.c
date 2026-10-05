#include "test.h"

#include "comic/folder.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FIXTURE "fixtures/folder_comic"

static int is_img(const char *n)
{
    size_t l = strlen(n);

    return (l > 4 && strcmp(n + l - 4, ".jpg") == 0) || (l > 4 && strcmp(n + l - 4, ".png") == 0);
}

static long file_size(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

static void test_fixture_folder(void)
{
    char err[128], path[256];
    folder_t *f = folder_open(FIXTURE, is_img, err, sizeof err);
    static const char *want[] = {"page1.jpg", "page10.jpg", "page2.png"}; /* strcmp order */
    int i;

    CHECK(f != NULL);
    if (!f)
        return;
    CHECK_INT(folder_count(f), 3);
    for (i = 0; i < 3; i++) {
        uint8_t *d = NULL;
        size_t len = 0;

        CHECK_STR(folder_name(f, i), want[i]);
        snprintf(path, sizeof path, "%s/%s", FIXTURE, want[i]);
        CHECK_INT(folder_size(f, i), file_size(path));
        CHECK_INT(folder_read(f, i, &d, &len), FOLDER_OK);
        CHECK_INT(len, file_size(path));
        if (d) {
            /* magic follows the extension, and a NUL terminates the data */
            if (i == 2)
                CHECK(len > 8 && memcmp(d, "\x89PNG", 4) == 0);
            else
                CHECK(len > 3 && d[0] == 0xFF && d[1] == 0xD8);
            CHECK_INT(d[len], 0);
        }
        free(d);
    }
    CHECK(folder_name(f, 3) == NULL);
    CHECK(folder_name(f, -1) == NULL);
    CHECK_INT(folder_size(f, 3), 0);
    {
        uint8_t *d = (uint8_t *)1;
        size_t len = 77;

        CHECK_INT(folder_read(f, 3, &d, &len), FOLDER_E_INDEX);
        CHECK(d == (uint8_t *)1 && len == 77); /* untouched on failure */
    }
    folder_close(f);

    /* Trailing slash and no predicate. */
    f = folder_open(FIXTURE "/", NULL, err, sizeof err);
    CHECK(f != NULL);
    CHECK_INT(folder_count(f), 3);
    folder_close(f);
}

static void put(const char *dir, const char *name, const char *data, size_t n)
{
    char p[256];
    FILE *fp;

    snprintf(p, sizeof p, "%s/%s", dir, name);
    fp = fopen(p, "wb");
    if (fp) {
        fwrite(data, 1, n, fp);
        fclose(fp);
    }
}

static void test_temp_folder(void)
{
    char dir[] = "/tmp/noct_folder_XXXXXX", p[256], err[128];
    folder_t *f;
    uint8_t *d = NULL;
    size_t len = 0;
    FILE *fp;

    if (!mkdtemp(dir)) {
        CHECK(!"mkdtemp");
        return;
    }
    put(dir, "b.txt", "text", 4);
    put(dir, "a.jpg", "AAAA", 4);
    put(dir, "empty.png", "", 0);
    snprintf(p, sizeof p, "%s/sub.png", dir); /* a directory named like an image */
    mkdir(p, 0700);

    f = folder_open(dir, is_img, err, sizeof err);
    CHECK(f != NULL);
    CHECK_INT(folder_count(f), 2); /* a.jpg, empty.png; not b.txt, not the sub.png dir */
    CHECK_STR(folder_name(f, 0), "a.jpg");
    CHECK_STR(folder_name(f, 1), "empty.png");
    CHECK_INT(folder_read(f, 0, &d, &len), FOLDER_OK);
    CHECK(len == 4 && memcmp(d, "AAAA", 4) == 0);
    free(d);
    d = NULL;
    CHECK_INT(folder_read(f, 1, &d, &len), FOLDER_OK); /* zero-length file */
    CHECK_INT(len, 0);
    free(d);
    folder_close(f);

    f = folder_open(dir, NULL, err, sizeof err); /* NULL predicate: every regular file */
    CHECK_INT(folder_count(f), 3);
    folder_close(f);

    /* file removed after listing: read fails cleanly */
    f = folder_open(dir, is_img, err, sizeof err);
    snprintf(p, sizeof p, "%s/a.jpg", dir);
    unlink(p);
    CHECK_INT(folder_read(f, 0, &d, &len), FOLDER_E_READ);
    folder_close(f);

    /* a file above the 64 MiB cap is refused (sparse file, no real disk use) */
    snprintf(p, sizeof p, "%s/huge.jpg", dir);
    fp = fopen(p, "wb");
    if (fp) {
        CHECK_INT(ftruncate(fileno(fp), (off_t)FOLDER_MAX_FILE + 1), 0);
        fclose(fp);
    }
    f = folder_open(dir, is_img, err, sizeof err);
    CHECK_INT(folder_count(f), 2); /* empty.png, huge.jpg */
    CHECK_STR(folder_name(f, 1), "huge.jpg");
    d = NULL;
    CHECK_INT(folder_read(f, 1, &d, &len), FOLDER_E_TOOBIG);
    CHECK(d == NULL);
    folder_close(f);

    snprintf(p, sizeof p, "%s/huge.jpg", dir);
    unlink(p);
    snprintf(p, sizeof p, "%s/empty.png", dir);
    unlink(p);
    snprintf(p, sizeof p, "%s/b.txt", dir);
    unlink(p);
    snprintf(p, sizeof p, "%s/sub.png", dir);
    rmdir(p);

    f = folder_open(dir, is_img, err, sizeof err); /* now empty */
    CHECK(f != NULL);
    CHECK_INT(folder_count(f), 0);
    folder_close(f);
    rmdir(dir);
}

static void test_errors(void)
{
    char err[128] = "";

    CHECK(folder_open("fixtures/no_such_dir", is_img, err, sizeof err) == NULL);
    CHECK(err[0] != '\0');
    CHECK(folder_open("fixtures/manifest.json", is_img, err, sizeof err) == NULL); /* a file */
    CHECK(folder_open(NULL, is_img, err, sizeof err) == NULL);
    CHECK(folder_open("", is_img, NULL, 0) == NULL);
    folder_close(NULL);
    CHECK_INT(folder_count(NULL), 0);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    test_fixture_folder();
    test_temp_folder();
    test_errors();
    return TEST_DONE("folder");
}
