#include "test.h"

#include "net/url.h"

#include <string.h>

#define BASE "http://a/b/c/d;p?q"

static void res(const char *base, const char *ref, const char *want)
{
    char out[URL_MAX];
    int rc = url_resolve(out, sizeof out, base, ref);
    g_checks++;
    if (rc != 0 || strcmp(out, want) != 0) {
        g_failed++;
        printf("FAIL url_resolve(\"%s\", \"%s\") = %d \"%s\", wanted \"%s\"\n", base ? base : "(null)",
               ref, rc, out, want);
    }
}

static void test_rfc3986_normal(void)
{
    /* RFC 3986 section 5.4.1 */
    res(BASE, "g:h", "g:h");
    res(BASE, "g", "http://a/b/c/g");
    res(BASE, "./g", "http://a/b/c/g");
    res(BASE, "g/", "http://a/b/c/g/");
    res(BASE, "/g", "http://a/g");
    res(BASE, "//g", "http://g");
    res(BASE, "?y", "http://a/b/c/d;p?y");
    res(BASE, "g?y", "http://a/b/c/g?y");
    res(BASE, "#s", "http://a/b/c/d;p?q#s");
    res(BASE, "g#s", "http://a/b/c/g#s");
    res(BASE, "g?y#s", "http://a/b/c/g?y#s");
    res(BASE, ";x", "http://a/b/c/;x");
    res(BASE, "g;x", "http://a/b/c/g;x");
    res(BASE, "g;x?y#s", "http://a/b/c/g;x?y#s");
    res(BASE, "", "http://a/b/c/d;p?q");
    res(BASE, ".", "http://a/b/c/");
    res(BASE, "./", "http://a/b/c/");
    res(BASE, "..", "http://a/b/");
    res(BASE, "../", "http://a/b/");
    res(BASE, "../g", "http://a/b/g");
    res(BASE, "../..", "http://a/");
    res(BASE, "../../", "http://a/");
    res(BASE, "../../g", "http://a/g");
}

static void test_rfc3986_abnormal(void)
{
    /* RFC 3986 section 5.4.2 */
    res(BASE, "../../../g", "http://a/g");
    res(BASE, "../../../../g", "http://a/g");
    res(BASE, "/./g", "http://a/g");
    res(BASE, "/../g", "http://a/g");
    res(BASE, "g.", "http://a/b/c/g.");
    res(BASE, ".g", "http://a/b/c/.g");
    res(BASE, "g..", "http://a/b/c/g..");
    res(BASE, "..g", "http://a/b/c/..g");
    res(BASE, "./../g", "http://a/b/g");
    res(BASE, "./g/.", "http://a/b/c/g/");
    res(BASE, "g/./h", "http://a/b/c/g/h");
    res(BASE, "g/../h", "http://a/b/c/h");
    res(BASE, "g;x=1/./y", "http://a/b/c/g;x=1/y");
    res(BASE, "g;x=1/../y", "http://a/b/c/y");
    res(BASE, "g?y/./x", "http://a/b/c/g?y/./x");
    res(BASE, "g?y/../x", "http://a/b/c/g?y/../x");
    res(BASE, "g#s/./x", "http://a/b/c/g#s/./x");
    res(BASE, "g#s/../x", "http://a/b/c/g#s/../x");
    res(BASE, "http:g", "http:g"); /* strict parser */
}

static void test_real_world(void)
{
    /* What OPDS feeds actually contain. */
    res("https://www.gutenberg.org/ebooks/search.opds/?query=dickens", "/ebooks/98.opds",
        "https://www.gutenberg.org/ebooks/98.opds");
    res("https://www.gutenberg.org/ebooks.opds/", "/ebooks/search.opds/?query=a&start_index=26",
        "https://www.gutenberg.org/ebooks/search.opds/?query=a&start_index=26");
    res("http://nas.local:25600/opds/v1.2/catalog", "/api/v1/books/xyz/pages/{pageNumber}?zero_based=true",
        "http://nas.local:25600/api/v1/books/xyz/pages/{pageNumber}?zero_based=true");
    res("http://nas.local:25600/opds/v1.2/catalog", "series?page=2", "http://nas.local:25600/opds/v1.2/series?page=2");
    res("http://host", "x", "http://host/x"); /* empty base path with an authority */
    res("http://host", "?q=1", "http://host?q=1");
    res("https://openlibrary.org/opds/", "search{?query,language}", "https://openlibrary.org/opds/search{?query,language}");
    res("https://a/b", "data:image/png;base64,AAAA==", "data:image/png;base64,AAAA==");
    res("https://a/b", "//cdn.example/x.jpg", "https://cdn.example/x.jpg");
    res(NULL, "http://a/b/../c", "http://a/c");
    res("", "relative/only", "relative/only");
    res(NULL, "./a/./b", "a/b"); /* RFC 3986 5.2.4 steps A and B on a relative path with no base */
    res(NULL, "../../g", "g");
    res("http://u:p@h:8080/a/b", "c", "http://u:p@h:8080/a/c");
}

static void test_limits(void)
{
    char out[16];
    char big[URL_MAX + 8];

    CHECK_INT(url_resolve(out, sizeof out, "http://a/b/c", "http://example.com/long/long/long"), -1);
    CHECK_INT(url_resolve(out, 0, "http://a/", "x"), -1);
    CHECK_INT(url_resolve(out, sizeof out, "http://a/", "xyz"), 0);
    CHECK_STR(out, "http://a/xyz");
    /* exactly cap - 1 chars fits, one more does not */
    CHECK_INT(url_resolve(out, 13, "http://a/", "xyz"), 0);
    CHECK_INT(url_resolve(out, 12, "http://a/", "xyz"), -1);
    memset(big, 'a', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    {
        char dst[URL_MAX * 2];
        CHECK_INT(url_resolve(dst, sizeof dst, "http://a/", big), -1);
    }
    CHECK(url_has_scheme("http://x"));
    CHECK(url_has_scheme("urn:x"));
    CHECK(!url_has_scheme("/x:y"));
    CHECK(!url_has_scheme("a/b:c"));
    CHECK(!url_has_scheme("1http:x"));
    CHECK(!url_has_scheme(""));
}

static void test_encode(void)
{
    char out[64];

    CHECK_INT(url_encode_component(out, sizeof out, "dickens"), 7);
    CHECK_STR(out, "dickens");
    CHECK_INT(url_encode_component(out, sizeof out, "a b&c=d/é"), 22);
    CHECK_STR(out, "a%20b%26c%3Dd%2F%C3%A9");
    CHECK_INT(url_encode_component(out, sizeof out, "-._~AZaz09"), 10);
    CHECK_STR(out, "-._~AZaz09");
    CHECK_INT(url_encode_component(out, sizeof out, ""), 0);
    CHECK_STR(out, "");
    CHECK_INT(url_encode_component(out, 3, "abc"), -1); /* needs 4 with the NUL */
    CHECK_INT(url_encode_component(out, 4, "abc"), 3);
    CHECK_INT(url_encode_component(out, 7, "ab c"), 6);
    CHECK_STR(out, "ab%20c");
    CHECK_INT(url_encode_component(out, 6, "ab c"), -1);
    CHECK_INT(url_encode_component(out, 0, "a"), -1);
}

static void test_expand(void)
{
    char out[256];
    url_var v[3];

    CHECK_INT(url_expand_search(out, sizeof out, "http://m.gutenberg.org/ebooks/search.opds/?query={searchTerms}", "war & peace"), 0);
    CHECK_STR(out, "http://m.gutenberg.org/ebooks/search.opds/?query=war%20%26%20peace");

    /* OpenSearch optional parameters expand to nothing. */
    CHECK_INT(url_expand_search(out, sizeof out, "http://x/s?q={searchTerms}&pw={startPage?}&c={count?}", "a"), 0);
    CHECK_STR(out, "http://x/s?q=a&pw=&c=");
    CHECK_INT(url_expand_search(out, sizeof out, "http://x/s?q={atom:author}{searchTerms}", "z"), 0);
    CHECK_STR(out, "http://x/s?q=z");

    /* RFC 6570 form-style expansion as used by OPDS 2 (Open Library). */
    v[0].name = "query";
    v[0].value = "dune";
    v[1].name = "mode";
    v[1].value = "open_access";
    v[2].name = "language";
    v[2].value = NULL;
    CHECK_INT(url_expand(out, sizeof out, "https://openlibrary.org/opds/search{?query,language,mode,media_type}", v, 3), 0);
    CHECK_STR(out, "https://openlibrary.org/opds/search?query=dune&mode=open_access");
    CHECK_INT(url_expand(out, sizeof out, "https://x/s?a=1{&query}", v, 3), 0);
    CHECK_STR(out, "https://x/s?a=1&query=dune");
    CHECK_INT(url_expand(out, sizeof out, "https://x/s{?language}", v, 3), 0);
    CHECK_STR(out, "https://x/s");

    CHECK_INT(url_expand_page(out, sizeof out, "/api/v1/books/b1/pages/{pageNumber}?zero_based=true", 7, 0), 0);
    CHECK_STR(out, "/api/v1/books/b1/pages/7?zero_based=true");
    CHECK_INT(url_expand_page(out, sizeof out, "/p?n={pageNumber}&w={maxWidth}", 0, 480), 0);
    CHECK_STR(out, "/p?n=0&w=480");
    CHECK_INT(url_expand_page(out, sizeof out, "/p?n={pageNumber}&w={maxWidth}", 12, 0), 0);
    CHECK_STR(out, "/p?n=12&w=");

    /* Unterminated brace is copied literally; overflow reports -1. */
    CHECK_INT(url_expand_search(out, sizeof out, "http://x/{oops", "a"), 0);
    CHECK_STR(out, "http://x/{oops");
    CHECK_INT(url_expand_search(out, 8, "http://x/{searchTerms}", "a"), -1);
    CHECK_INT(url_expand_search(out, 12, "http://x/{searchTerms}", "abcdefg"), -1);
    CHECK_INT(url_expand_search(out, 14, "http://x/{searchTerms}", "ab"), 0);
    CHECK_STR(out, "http://x/ab");
    CHECK_INT(url_expand_search(out, 0, "x", "a"), -1);
}

int main(void)
{
    test_rfc3986_normal();
    test_rfc3986_abnormal();
    test_real_world();
    test_limits();
    test_encode();
    test_expand();
    return TEST_DONE("url");
}
