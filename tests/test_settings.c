#define _GNU_SOURCE
#include "test.h"

#include "core/settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char dir[256];

static void path_in(char *out, size_t cap, const char *name)
{
    snprintf(out, cap, "%s/%s", dir, name);
}

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    fputs(text, f);
    fclose(f);
}

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *b = (char *)calloc(1, 8192);
    if (f) {
        size_t n = fread(b, 1, 8191, f);
        b[n] = '\0';
        fclose(f);
    }
    return b;
}

static void test_defaults_and_types(void)
{
    settings_t *s = settings_new();
    CHECK_STR(settings_get(s, "reader", "mode", "paged"), "paged");
    CHECK_INT(settings_get_int(s, "reader", "dim", 7), 7);
    CHECK_INT(settings_get_bool(s, "reader", "rtl", 1), 1);
    CHECK_INT(settings_get_bool(s, "reader", "rtl", 0), 0);

    CHECK_INT(settings_set(s, "reader", "mode", "strip"), 0);
    CHECK_STR(settings_get(s, "reader", "mode", "paged"), "strip");
    CHECK_INT(settings_set_int(s, "reader", "dim", -42), 0);
    CHECK_INT(settings_get_int(s, "reader", "dim", 7), -42);
    CHECK_INT(settings_set_bool(s, "reader", "rtl", 1), 0);
    CHECK_INT(settings_get_bool(s, "reader", "rtl", 0), 1);
    CHECK_STR(settings_get(s, "reader", "rtl", ""), "1");

    /* Same key in another section is a different key; overwrite does not add entries. */
    CHECK_INT(settings_set(s, "book", "mode", "other"), 0);
    CHECK_STR(settings_get(s, "reader", "mode", ""), "strip");
    CHECK_INT(settings_count(s), 4);
    CHECK_INT(settings_set(s, "reader", "mode", "paged"), 0);
    CHECK_INT(settings_count(s), 4);

    /* NULL section means the global section "". */
    CHECK_INT(settings_set(s, NULL, "version", "3"), 0);
    CHECK_STR(settings_get(s, "", "version", "x"), "3");
    CHECK_INT(settings_get_int(s, NULL, "version", 0), 3);

    CHECK_INT(settings_remove(s, "reader", "mode"), 1);
    CHECK_INT(settings_remove(s, "reader", "mode"), 0);
    CHECK_STR(settings_get(s, "reader", "mode", "gone"), "gone");
    CHECK_STR(settings_get(s, "book", "mode", ""), "other");
    settings_free(s);
}

static void test_bad_values(void)
{
    settings_t *s = settings_new();
    settings_set(s, "a", "num", "12abc");
    settings_set(s, "a", "empty", "");
    settings_set(s, "a", "flag", "maybe");
    settings_set(s, "a", "big", "99999999999999999999999");
    CHECK_INT(settings_get_int(s, "a", "num", 5), 5);
    CHECK_INT(settings_get_int(s, "a", "empty", 5), 5);
    CHECK_INT(settings_get_int(s, "a", "big", 5), 5);
    CHECK_INT(settings_get_bool(s, "a", "flag", 1), 1);
    CHECK_INT(settings_get_bool(s, "a", "flag", 0), 0);

    CHECK_INT(settings_set(s, "a", "", "v"), -1);
    CHECK_INT(settings_set(s, "a", "k=v", "v"), -1);
    CHECK_INT(settings_set(s, "a", "k", "line1\nline2"), -1);
    CHECK_INT(settings_set(s, "a]b", "k", "v"), -1);
    CHECK_INT(settings_set(s, "a", "[k", "v"), -1);
    CHECK_STR(settings_get(s, "a", "k", "none"), "none");
    settings_free(s);
}

static void test_bool_spellings(void)
{
    static const char *yes[] = {"1", "true", "TRUE", "Yes", "on"};
    static const char *no[] = {"0", "false", "False", "NO", "Off"};
    settings_t *s = settings_new();
    for (size_t i = 0; i < 5; i++) {
        settings_set(s, "b", "k", yes[i]);
        CHECK_INT(settings_get_bool(s, "b", "k", 0), 1);
        settings_set(s, "b", "k", no[i]);
        CHECK_INT(settings_get_bool(s, "b", "k", 1), 0);
    }
    settings_free(s);
}

static void test_load_parse(void)
{
    char p[300];
    settings_t *s = settings_new();

    path_in(p, sizeof p, "parse.ini");
    write_file(p,
               "\xEF\xBB\xBF; comment\r\n"
               "top = 1\r\n"
               "\r\n"
               "[reader]\r\n"
               "  mode = strip  \r\n"
               "# another comment\r\n"
               "title=a = b\r\n"
               "[ spaced section ]\n"
               "k=v\n"
               "garbage line without equals\n"
               "[reader]\n"
               "dim=30\n"
               "nolinefeed=at-eof");
    CHECK_INT(settings_load(s, p), 0);
    CHECK_STR(settings_get(s, "", "top", ""), "1");
    CHECK_STR(settings_get(s, "reader", "mode", ""), "strip");
    CHECK_STR(settings_get(s, "reader", "title", ""), "a = b");
    CHECK_STR(settings_get(s, "spaced section", "k", ""), "v");
    CHECK_INT(settings_get_int(s, "reader", "dim", 0), 30); /* re-opened section merges */
    CHECK_STR(settings_get(s, "reader", "nolinefeed", ""), "at-eof");
    CHECK_INT(settings_count(s), 6);

    path_in(p, sizeof p, "does-not-exist.ini");
    CHECK_INT(settings_load(s, p), 1); /* missing file: not an error */
    CHECK_INT(settings_count(s), 6);
    settings_free(s);
}

static void test_roundtrip_and_unknown_keys(void)
{
    char p[300], tmp[310];
    settings_t *a = settings_new(), *b = settings_new();
    char *text;

    path_in(p, sizeof p, "rt.ini");
    snprintf(tmp, sizeof tmp, "%s.tmp", p);
    write_file(p, "future_flag=1\n[reader]\nmode=paged\nfuture_key=keep me\n[net]\nproxy=none\n");

    CHECK_INT(settings_load(a, p), 0);
    /* An older build only touches keys it knows. */
    CHECK_INT(settings_set(a, "reader", "mode", "strip"), 0);
    CHECK_INT(settings_set_int(a, "reader", "dim", 55), 0);
    CHECK_INT(settings_set(a, "ui", "theme", "oled"), 0);
    CHECK_INT(settings_save(a, p), 0);
    CHECK_INT(access(tmp, F_OK), -1); /* temp file renamed away, not left behind */

    CHECK_INT(settings_load(b, p), 0);
    CHECK_STR(settings_get(b, "", "future_flag", ""), "1");
    CHECK_STR(settings_get(b, "reader", "future_key", ""), "keep me");
    CHECK_STR(settings_get(b, "net", "proxy", ""), "none");
    CHECK_STR(settings_get(b, "reader", "mode", ""), "strip");
    CHECK_INT(settings_get_int(b, "reader", "dim", 0), 55);
    CHECK_STR(settings_get(b, "ui", "theme", ""), "oled");
    CHECK_INT(settings_count(b), settings_count(a));

    text = slurp(p);
    CHECK(strstr(text, "[reader]\nmode=strip\nfuture_key=keep me\ndim=55\n") != NULL);
    CHECK(strncmp(text, "future_flag=1\n", 14) == 0);
    free(text);
    settings_free(a);
    settings_free(b);
}

static void test_save_overwrites_and_fails_cleanly(void)
{
    char p[300], bad[320];
    settings_t *s = settings_new();
    char *text;

    path_in(p, sizeof p, "ow.ini");
    write_file(p, "old=1\n");
    settings_set(s, "x", "new", "2");
    CHECK_INT(settings_save(s, p), 0);
    text = slurp(p);
    CHECK_STR(text, "[x]\nnew=2\n\n");
    free(text);

    /* Unwritable directory: reports failure, existing file untouched. */
    snprintf(bad, sizeof bad, "%s/no-such-dir/s.ini", dir);
    CHECK_INT(settings_save(s, bad), -1);
    text = slurp(p);
    CHECK_STR(text, "[x]\nnew=2\n\n");
    free(text);

    /* A save/load cycle with a long value. */
    {
        char longv[2000];
        settings_t *t = settings_new();
        memset(longv, 'v', sizeof longv - 1);
        longv[sizeof longv - 1] = '\0';
        settings_set(s, "x", "long", longv);
        CHECK_INT(settings_save(s, p), 0);
        CHECK_INT(settings_load(t, p), 0);
        CHECK_STR(settings_get(t, "x", "long", ""), longv);
        settings_free(t);
    }
    settings_free(s);
}

static void test_many_entries(void)
{
    char p[300], k[32];
    settings_t *s = settings_new(), *t = settings_new();
    path_in(p, sizeof p, "many.ini");
    for (int i = 0; i < 500; i++) {
        snprintf(k, sizeof k, "key%d", i);
        settings_set_int(s, i % 2 ? "odd" : "even", k, i);
    }
    CHECK_INT(settings_save(s, p), 0);
    CHECK_INT(settings_load(t, p), 0);
    CHECK_INT(settings_count(t), 500);
    CHECK_INT(settings_get_int(t, "odd", "key499", -1), 499);
    CHECK_INT(settings_get_int(t, "even", "key0", -1), 0);
    settings_free(s);
    settings_free(t);
}

int main(void)
{
    char tmpl[] = "/tmp/nocto-settings-XXXXXX";
    char cmd[300];

    if (!mkdtemp(tmpl)) {
        printf("FAIL: mkdtemp\n");
        return 1;
    }
    snprintf(dir, sizeof dir, "%s", tmpl);

    test_defaults_and_types();
    test_bad_values();
    test_bool_spellings();
    test_load_parse();
    test_roundtrip_and_unknown_keys();
    test_save_overwrites_and_fails_cleanly();
    test_many_entries();

    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    if (system(cmd) != 0)
        printf("note: could not remove %s\n", dir);
    return TEST_DONE("settings");
}
