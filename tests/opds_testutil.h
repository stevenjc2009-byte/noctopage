/* Shared helpers for test_opds.c and test_opds_edge.c: fixture loading and parse wrappers. */
#ifndef NOCTO_OPDS_TESTUTIL_H
#define NOCTO_OPDS_TESTUTIL_H

#include "test.h"

#include "net/opds.h"
#include "net/url.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GUT "https://www.gutenberg.org/ebooks.opds/"
#define KOM "https://komga.example.org/opds/v1.2/series/0A1B2C3D?page=0"
#define OPDS_XML "application/atom+xml;profile=opds-catalog"

#define PR(s, ct) parse_rc((s), strlen(s), (ct))

static inline char *slurp(const char *name, size_t *n)
{
    char path[256], *b;
    FILE *fp;
    snprintf(path, sizeof path, "fixtures/opds/%s", name);
    fp = fopen(path, "rb");
    if (!fp) {
        printf("FAIL: cannot open %s\n", path);
        g_failed++;
        *n = 0;
        return NULL;
    }
    b = (char *)malloc(1 << 20);
    *n = fread(b, 1, (1 << 20) - 1, fp);
    b[*n] = '\0';
    fclose(fp);
    return b;
}

static inline opds_feed *load(const char *name, const char *base, const char *ctype)
{
    size_t n;
    char *b = slurp(name, &n), err[128] = "";
    opds_feed *f = NULL;
    int rc = b ? opds_parse(b, n, ctype, base, &f, err, sizeof err) : -1;
    free(b);
    g_checks++;
    if (rc != 0) {
        g_failed++;
        printf("FAIL: parse %s: %s\n", name, err);
        return NULL;
    }
    return f;
}

static inline opds_feed *parse_str(const char *s, const char *base)
{
    opds_feed *f = NULL;
    opds_parse(s, strlen(s), NULL, base, &f, NULL, 0);
    return f;
}

static inline int parse_rc(const char *s, size_t len, const char *ctype)
{
    opds_feed *f = NULL;
    char err[64];
    int rc = opds_parse(s, len, ctype, "http://x/", &f, err, sizeof err);
    CHECK((rc == 0) == (f != NULL));
    CHECK(rc == 0 || err[0] != '\0');
    opds_feed_free(f);
    return rc;
}


#endif
