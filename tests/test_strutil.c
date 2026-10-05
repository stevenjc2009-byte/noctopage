#include "test.h"

#include "core/strutil.h"

#include <string.h>

static void test_copy_cat(void)
{
    char b[8];
    char guard[16];

    CHECK_INT(str_copy(b, sizeof b, "abc"), 3);
    CHECK_STR(b, "abc");
    CHECK_INT(str_cat(b, sizeof b, "de"), 5);
    CHECK_STR(b, "abcde");

    /* Truncation: returns the full length, result stays NUL-terminated. */
    CHECK_INT(str_cat(b, sizeof b, "fghij"), 10);
    CHECK_STR(b, "abcdefg");
    CHECK_INT(str_copy(b, sizeof b, "0123456789"), 10);
    CHECK_STR(b, "0123456");

    /* cap 1 and cap 0 are safe. */
    memset(guard, 'x', sizeof guard);
    CHECK_INT(str_copy(guard, 1, "abc"), 3);
    CHECK_INT(guard[0], 0);
    CHECK_INT(guard[1], 'x');
    CHECK_INT(str_copy(guard + 4, 0, "abc"), 3);
    CHECK_INT(guard[4], 'x');

    /* Unterminated destination is left alone. */
    memset(guard, 'y', 4);
    CHECK_INT(str_cat(guard, 4, "zz"), 6);
    CHECK_INT(guard[3], 'y');
}

static void test_case(void)
{
    char s[] = "Hello.CBZ \xE3\x82\xAB";
    str_lower(s);
    CHECK_STR(s, "hello.cbz \xE3\x82\xAB"); /* UTF-8 bytes untouched */

    CHECK(str_casecmp("ABC", "abc") == 0);
    CHECK(str_casecmp("abc", "abd") < 0);
    CHECK(str_casecmp("abd", "ABC") > 0);
    CHECK(str_casecmp("ab", "abc") < 0);

    CHECK(str_ends_with_ci("Book.CBZ", ".cbz"));
    CHECK(str_ends_with_ci("book.cbz", ".CBZ"));
    CHECK(!str_ends_with_ci("book.cbr", ".cbz"));
    CHECK(!str_ends_with_ci("z", ".cbz"));
    CHECK(str_ends_with_ci("anything", ""));
    CHECK(str_ends_with_ci("", ""));
}

static void test_trim(void)
{
    char a[] = "  \t hi there \r\n";
    char b[] = "   ";
    char c[] = "x";
    char d[] = "";
    CHECK_STR(str_trim(a), "hi there");
    CHECK_STR(str_trim(b), "");
    CHECK_STR(str_trim(c), "x");
    CHECK_STR(str_trim(d), "");
}

static void test_join(void)
{
    char b[32];
    char t[8];

    CHECK_INT(path_join(b, sizeof b, "ux0:data", "noctopage"), 0);
    CHECK_STR(b, "ux0:data/noctopage");
    CHECK_INT(path_join(b, sizeof b, "ux0:", "books"), 0);
    CHECK_STR(b, "ux0:books"); /* no slash after a device colon */
    CHECK_INT(path_join(b, sizeof b, "ux0:books/", "a.cbz"), 0);
    CHECK_STR(b, "ux0:books/a.cbz");
    CHECK_INT(path_join(b, sizeof b, "ux0:books", "/a.cbz"), 0);
    CHECK_STR(b, "ux0:books/a.cbz");
    CHECK_INT(path_join(b, sizeof b, "", "a"), 0);
    CHECK_STR(b, "a");
    CHECK_INT(path_join(b, sizeof b, "/", "a"), 0);
    CHECK_STR(b, "/a");
    CHECK_INT(path_join(b, sizeof b, "dir", ""), 0);
    CHECK_STR(b, "dir");

    CHECK_INT(path_join(t, sizeof t, "ux0:data", "toolong"), -1);
    CHECK_STR(t, "ux0:dat");
}

static void test_basename_dirname_ext(void)
{
    char d[32];
    char tiny[4];

    CHECK_STR(path_basename("ux0:data/a.cbz"), "a.cbz");
    CHECK_STR(path_basename("ux0:a.cbz"), "a.cbz");
    CHECK_STR(path_basename("a.cbz"), "a.cbz");
    CHECK_STR(path_basename("dir/"), "");
    CHECK_STR(path_basename(""), "");

    CHECK_INT(path_dirname(d, sizeof d, "ux0:data/a.cbz"), 0);
    CHECK_STR(d, "ux0:data");
    CHECK_INT(path_dirname(d, sizeof d, "ux0:a.cbz"), 0);
    CHECK_STR(d, "ux0:");
    CHECK_INT(path_dirname(d, sizeof d, "/a"), 0);
    CHECK_STR(d, "/");
    CHECK_INT(path_dirname(d, sizeof d, "a"), 0);
    CHECK_STR(d, "");
    CHECK_INT(path_dirname(d, sizeof d, "a/b/c.txt"), 0);
    CHECK_STR(d, "a/b");
    CHECK_INT(path_dirname(d, sizeof d, "a//c.txt"), 0);
    CHECK_STR(d, "a");
    CHECK_INT(path_dirname_len("ux0:data/a.cbz"), 8);
    CHECK_INT(path_dirname(tiny, sizeof tiny, "ux0:data/a.cbz"), -1);
    CHECK_STR(tiny, "ux0");

    CHECK_STR(path_ext("ux0:data/a.CBZ"), "CBZ");
    CHECK_STR(path_ext("a.tar.gz"), "gz");
    CHECK_STR(path_ext("noext"), "");
    CHECK_STR(path_ext(".hidden"), "");
    CHECK_STR(path_ext("dir.d/file"), ""); /* dot in a directory name is not an extension */
    CHECK_STR(path_ext("trailing."), "");
    CHECK_STR(path_ext("\xE6\xBC\xAB\xE7\x94\xBB.cbz"), "cbz");
}

int main(void)
{
    test_copy_cat();
    test_case();
    test_trim();
    test_join();
    test_basename_dirname_ext();
    return TEST_DONE("strutil");
}
