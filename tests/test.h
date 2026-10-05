/* Minimal test framework: one header, one executable per test file.
 *
 *   #include "test.h"
 *   int main(void) {
 *       CHECK(1 + 1 == 2);
 *       CHECK_INT(add(2, 2), 4);
 *       CHECK_STR(name, "abc");
 *       return TEST_DONE("strutil");     // prints "strutil: N checks, M failed"
 *   }
 *
 * Every CHECK* counts one assertion; failures print file:line and the values. TEST_DONE returns
 * 0 when nothing failed and 1 otherwise, so it can be the return value of main(). A test
 * binary that ran zero checks also fails: it means the test was wired but proves nothing. */
#ifndef NOCTO_TEST_H
#define NOCTO_TEST_H

#include <stdio.h>
#include <string.h>

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        g_checks++;                                                                    \
        if (!(cond)) {                                                                 \
            g_failed++;                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                     \
        }                                                                              \
    } while (0)

#define CHECK_INT(got, want)                                                           \
    do {                                                                               \
        long long g__ = (long long)(got), w__ = (long long)(want);                     \
        g_checks++;                                                                    \
        if (g__ != w__) {                                                              \
            g_failed++;                                                                \
            printf("FAIL %s:%d: %s == %lld, wanted %lld\n", __FILE__, __LINE__, #got,  \
                   g__, w__);                                                          \
        }                                                                              \
    } while (0)

#define CHECK_U64(got, want)                                                           \
    do {                                                                               \
        unsigned long long g__ = (unsigned long long)(got);                            \
        unsigned long long w__ = (unsigned long long)(want);                           \
        g_checks++;                                                                    \
        if (g__ != w__) {                                                              \
            g_failed++;                                                                \
            printf("FAIL %s:%d: %s == 0x%016llx, wanted 0x%016llx\n", __FILE__,        \
                   __LINE__, #got, g__, w__);                                          \
        }                                                                              \
    } while (0)

#define CHECK_STR(got, want)                                                           \
    do {                                                                               \
        const char *g__ = (got), *w__ = (want);                                        \
        g_checks++;                                                                    \
        if (g__ == NULL || w__ == NULL ? g__ != w__ : strcmp(g__, w__) != 0) {         \
            g_failed++;                                                                \
            printf("FAIL %s:%d: %s == \"%s\", wanted \"%s\"\n", __FILE__, __LINE__,    \
                   #got, g__ ? g__ : "(null)", w__ ? w__ : "(null)");                  \
        }                                                                              \
    } while (0)

static inline int test_done(const char *name)
{
    if (g_checks == 0) {
        g_failed++;
        printf("FAIL %s: zero checks ran\n", name);
    }
    printf("%s: %d checks, %d failed\n", name, g_checks, g_failed);
    return g_failed ? 1 : 0;
}
#define TEST_DONE(name) test_done(name)

#endif
