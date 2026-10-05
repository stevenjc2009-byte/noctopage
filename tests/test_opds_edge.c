/* net/opds edge cases: odd Atom shapes, dropped hrefs, format detection, caps, hostile input,
 * truncation at every byte offset and deterministic corruption (run under ASan/UBSan). */
#include "opds_testutil.h"

#define FEED_HEAD "<feed xmlns=\"http://www.w3.org/2005/Atom\"><title>T</title>"

static void test_edge_cases(void)
{
    opds_feed *f;
    /* no namespace at all, prefixed Atom, CDATA html, entities, xhtml with <br/> */
    f = parse_str("<feed><title>Bare</title><entry><title>A &amp; B &#233;</title><summary><![CDATA[<p>one</p><p>two &amp; <b>three</b></p>]]></summary></entry></feed>", "http://x/");
    CHECK(f != NULL);
    if (f) {
        CHECK_STR(f->title, "Bare");
        CHECK_STR(f->entries[0].title, "A & B \xc3\xa9");
        CHECK_STR(f->entries[0].summary, "one\ntwo & three");
        opds_feed_free(f);
    }
    f = parse_str("<a:feed xmlns:a=\"http://www.w3.org/2005/Atom\"><a:title>Pfx</a:title><a:entry><a:title>E</a:title>"
                  "<a:content type=\"xhtml\"><x:div xmlns:x=\"http://www.w3.org/1999/xhtml\">l1<x:br/>l2<x:p>l3</x:p>l4</x:div></a:content></a:entry></a:feed>", "http://x/");
    CHECK(f != NULL);
    if (f) {
        CHECK_STR(f->title, "Pfx");
        CHECK_STR(f->entries[0].summary, "l1\nl2\nl3\nl4");
        opds_feed_free(f);
    }
    /* summary beats content in either order */
    f = parse_str(FEED_HEAD "<entry><content>c</content><summary>s</summary></entry><entry><summary>s</summary><content>c</content></entry></feed>", "http://x/");
    CHECK(f && f->nentries == 2 && strcmp(f->entries[0].summary, "s") == 0 && strcmp(f->entries[1].summary, "s") == 0);
    opds_feed_free(f);
    /* hrefs: relative with no base, data:, javascript:, mailto: are dropped; absolute survive */
    f = parse_str(FEED_HEAD "<link rel=\"next\" href=\"p2\"/><link rel=\"start\" href=\"http://a/s\"/><entry><title>E</title>"
                  "<link rel=\"subsection\" href=\"data:image/png;base64,AAAA\"/>"
                  "<link rel=\"http://opds-spec.org/image\" href=\"javascript:alert(1)\"/>"
                  "<link rel=\"http://opds-spec.org/acquisition\" href=\"mailto:a@b\"/>"
                  "<link rel=\"http://opds-spec.org/acquisition\" href=\"HTTP://h/a.epub\" type=\"application/epub+zip\"/></entry></feed>", NULL);
    CHECK(f != NULL);
    if (f) {
        CHECK(f->next == NULL);
        CHECK_STR(f->start, "http://a/s");
        CHECK(f->entries[0].nav_href == NULL && f->entries[0].cover == NULL);
        CHECK_INT(f->entries[0].nacq, 1);
        CHECK_INT(f->entries[0].acq[0].length, -1);
        opds_feed_free(f);
    }
    /* link without rel but an OPDS catalog type is a navigation entry; a related feed is not */
    f = parse_str(FEED_HEAD "<entry><link href=\"/n\" type=\"application/atom+xml;profile=opds-catalog\"/></entry>"
                  "<entry><link rel=\"related\" href=\"/r\" type=\"application/atom+xml;profile=opds-catalog\"/></entry></feed>", "http://x/");
    CHECK(f && f->entries[0].nav_href && f->entries[1].nav_href == NULL);
    opds_feed_free(f);
    /* opds_acq_format */
    {
        opds_acq a = { "http://h/dl", "", "", -1, OPDS_ACQ_GENERIC };
        a.href = "http://h/x/book.CBZ?x=1"; CHECK_STR(opds_acq_format(&a), "cbz");
        a.type = "application/octet-stream"; a.href = "http://h/b.epub#f"; CHECK_STR(opds_acq_format(&a), "epub");
        a.type = "application/zip"; a.href = "http://h/a.b/file"; CHECK_STR(opds_acq_format(&a), "other");
        a.href = "http://h/a.zip"; CHECK_STR(opds_acq_format(&a), "zip");
        a.type = "image/jpeg"; a.href = "http://h/a.epub"; CHECK_STR(opds_acq_format(&a), "other");
        a.type = "Application/PDF; charset=binary"; CHECK_STR(opds_acq_format(&a), "pdf");
        a.type = "text/html"; CHECK_STR(opds_acq_format(&a), "html");
    }
    CHECK(opds_best_acquisition(NULL, NULL) == NULL);
    /* no preferred format available: the first downloadable one */
    f = parse_str(FEED_HEAD "<entry><link rel=\"http://opds-spec.org/acquisition/buy\" href=\"http://h/buy.epub\" type=\"application/epub+zip\"/>"
                  "<link rel=\"http://opds-spec.org/acquisition\" href=\"http://h/b.mobi\" type=\"application/x-mobipocket-ebook\"/></entry></feed>", NULL);
    CHECK(f && opds_best_acquisition(&f->entries[0], NULL) && strcmp(opds_best_acquisition(&f->entries[0], NULL)->href, "http://h/b.mobi") == 0);
    opds_feed_free(f);
    /* borrow is recorded with its kind and is never chosen as a download */
    f = parse_str(FEED_HEAD "<entry><link rel=\"http://opds-spec.org/acquisition/borrow\" href=\"http://h/loan.epub\" type=\"application/epub+zip\"/></entry></feed>", NULL);
    CHECK(f && f->entries[0].nacq == 1 && f->entries[0].acq[0].kind == OPDS_ACQ_BORROW);
    CHECK(f && opds_best_acquisition(&f->entries[0], NULL) == NULL);
    opds_feed_free(f);
    /* per-entry state does not leak into the next entry: author and summary */
    f = parse_str(FEED_HEAD "<entry><author><name>Ann</name></author><summary>first</summary></entry>"
                  "<entry><content>second</content></entry><entry><title>third</title></entry></feed>", "http://x/");
    CHECK(f && f->nentries == 3);
    CHECK(f && f->entries[1].author == NULL && f->entries[2].author == NULL);
    CHECK(f && f->entries[1].summary && strcmp(f->entries[1].summary, "second") == 0);
    CHECK(f && f->entries[2].summary == NULL);
    opds_feed_free(f);
    /* search link choice: a template beats an OSD link in either order; the first template wins */
    f = parse_str(FEED_HEAD "<link rel=\"search\" href=\"/o.xml\" type=\"application/opensearchdescription+xml\"/>"
                  "<link rel=\"search\" href=\"/s1?q={searchTerms}\" type=\"application/atom+xml\"/>"
                  "<link rel=\"search\" href=\"/s2?q={searchTerms}\" type=\"application/atom+xml\"/></feed>", "http://x/");
    CHECK(f && f->search_is_template == 1 && strcmp(f->search_href, "http://x/s1?q={searchTerms}") == 0);
    opds_feed_free(f);
    f = parse_str(FEED_HEAD "<link rel=\"search\" href=\"/s1?q={searchTerms}\" type=\"application/atom+xml\"/>"
                  "<link rel=\"search\" href=\"/o.xml\" type=\"application/opensearchdescription+xml\"/></feed>", "http://x/");
    CHECK(f && f->search_is_template == 1 && strcmp(f->search_href, "http://x/s1?q={searchTerms}") == 0);
    opds_feed_free(f);
    /* OpenSearch: an opds-catalog Url beats an earlier atom+xml one, which beats html */
    {
        const char *osd = "<OpenSearchDescription xmlns=\"http://a9.com/-/spec/opensearch/1.1/\">"
                          "<Url type=\"text/html\" template=\"/h?q={searchTerms}\"/>"
                          "<Url type=\"application/atom+xml\" template=\"/a?q={searchTerms}\"/>"
                          "<Url type=\"application/atom+xml;profile=opds-catalog\" template=\"/c?q={searchTerms}\"/></OpenSearchDescription>";
        char tmpl[200];
        CHECK_INT(opds_parse_opensearch(osd, strlen(osd), "http://x/", tmpl, sizeof tmpl, NULL, 0), 0);
        CHECK_STR(tmpl, "http://x/c?q={searchTerms}");
    }
}

static const char *strip(const char *in)
{
    static char out[OPDS_SUMMARY_MAX + 1];
    size_t n = opds_strip_html(in, out, sizeof out);
    CHECK_INT(n, strlen(out));
    return out;
}

static void test_strip_html(void)
{
    char out[8];
    CHECK_STR(strip("<p>Hello <b>world</b></p>"), "Hello world");
    CHECK_STR(strip("a<br>b<br/>c<BR />d"), "a\nb\nc\nd");
    CHECK_STR(strip("<p>one</p><p>two</p><ul><li>x</li><li>y</li></ul>"), "one\ntwo\nx\ny");
    CHECK_STR(strip("x &amp; y &lt;z&gt; &quot;q&quot; &apos;"), "x & y <z> \"q\" '");
    CHECK_STR(strip("&#169; &#xE9; &mdash; &nbsp;|&#160;|"), "\xc2\xa9 \xc3\xa9 \xe2\x80\x94 | |");
    CHECK_STR(strip("  lots \n\t of   space \r\n "), "lots of space");
    CHECK_STR(strip("a<!-- hidden <b> -->b"), "ab");
    CHECK_STR(strip("5 < 6 and 7 > 3"), "5 < 6 and 7 > 3");
    CHECK_STR(strip("bad &unknown; & &#99999999999; <b"), "bad &unknown; & &#99999999999;"); /* too long: literal; open tag dropped */
    CHECK_STR(strip("&#9999999;"), "\xef\xbf\xbd"); /* U+FFFD for an invalid code point */
    CHECK_STR(strip("<a title=\"x>y\" href='>'>link</a>"), "link");
    CHECK_STR(strip("a\xff" "b\xc3"), "ab");
    CHECK_STR(strip(""), "");
    CHECK_STR(strip("<p></p><br>"), "");
    CHECK_INT(opds_strip_html(NULL, out, sizeof out), 0);
    CHECK_STR(out, "");
    CHECK_INT(opds_strip_html("abc", out, 0), 0); /* cap 0: nothing written */
    CHECK_INT(opds_strip_html("abc", out, 1), 0);
    CHECK_STR(out, "");
    CHECK_INT(opds_strip_html("a\xc3\xa9", out, 3), 1); /* the two-byte char does not fit: dropped whole */
    CHECK_STR(out, "a");
    CHECK_INT(opds_strip_html("a\xc3\xa9", out, 4), 3);
    CHECK_STR(out, "a\xc3\xa9");
    CHECK_INT(opds_strip_html("one two three", out, sizeof out), 7);
    CHECK_STR(out, "one two");
}

static void test_json_odd_shapes(void)
{
    const char *js =
        "{\"metadata\":{\"title\":123,\"numberOfItems\":-5},\"links\":{\"a\":1},\"publications\":["
        "{\"metadata\":{\"title\":\"X\",\"author\":[[\"nested\"],{\"name\":5},null,\"Ok\"]},\"links\":["
        "{\"rel\":[\"x\",\"http://opds-spec.org/acquisition/open-access\"],\"href\":\"f.epub\",\"type\":\"application/epub+zip\"},"
        "{\"rel\":5,\"href\":\"y\"},\"str\",{\"href\":\"nohref\"}],\"images\":["
        "{\"href\":\"a.jpg\",\"width\":10,\"height\":10},{\"href\":\"b.jpg\",\"width\":100,\"height\":100},"
        "{\"href\":\"c.jpg\",\"width\":50,\"height\":50}]},\"junk\",null,5],"
        "\"navigation\":[{\"href\":\"data:x\"},{\"title\":\"NoHref\"}],"
        "\"groups\":[{\"metadata\":{\"title\":\"G\"},\"publications\":[{\"metadata\":{\"title\":\"Gp\"},\"links\":["
        "{\"rel\":\"http://vaemendis.net/opds-pse/stream\",\"href\":\"p/{pageNumber}\",\"templated\":true,"
        "\"properties\":{\"numberOfItems\":12}}]}]},5]}";
    opds_feed *f = parse_str(js, "http://x/");
    CHECK(f != NULL);
    if (!f) return;
    CHECK_STR(f->title, "");
    CHECK_INT(f->total, -1);
    CHECK_INT(f->nentries, 2);
    CHECK_STR(f->entries[0].author, "Ok");
    CHECK_INT(f->entries[0].nacq, 1);
    CHECK_INT(f->entries[0].acq[0].kind, OPDS_ACQ_OPEN_ACCESS);
    CHECK_STR(f->entries[0].cover, "http://x/b.jpg");
    CHECK_STR(f->entries[0].thumb, "http://x/a.jpg");
    CHECK_STR(f->entries[1].group, "G");
    CHECK_INT(f->entries[1].pse_count, 12);
    CHECK_STR(f->entries[1].pse_href, "http://x/p/{pageNumber}");
    opds_feed_free(f);
}

static void test_caps(void)
{
    size_t cap = 400000, n = 0;
    char *b = (char *)malloc(cap);
    int i;
    opds_feed *f;

    n += (size_t)snprintf(b + n, cap - n, "<feed xmlns=\"http://www.w3.org/2005/Atom\"><title>Big</title>");
    for (i = 0; i < 100; i++)
        n += (size_t)snprintf(b + n, cap - n, "<link rel=\"http://opds-spec.org/facet\" title=\"f%d\" href=\"/f%d\"/>", i, i);
    for (i = 0; i < 600; i++) {
        int k;
        n += (size_t)snprintf(b + n, cap - n, "<entry><title>E%d</title>", i);
        if (i == 7) {
            n += (size_t)snprintf(b + n, cap - n, "<summary>");
            for (k = 0; k < 800; k++) n += (size_t)snprintf(b + n, cap - n, "w\xc3\xa9rd ");
            n += (size_t)snprintf(b + n, cap - n, "</summary>");
            for (k = 0; k < 50; k++)
                n += (size_t)snprintf(b + n, cap - n, "<link rel=\"http://opds-spec.org/acquisition\" href=\"/a%d.epub\"/>", k);
        }
        n += (size_t)snprintf(b + n, cap - n, "</entry>");
    }
    n += (size_t)snprintf(b + n, cap - n, "</feed>");
    CHECK_INT(opds_parse(b, n, NULL, "http://x/", &f, NULL, 0), 0);
    CHECK_INT(f->nentries, 500);
    CHECK_INT(f->truncated, 1);
    CHECK_STR(f->entries[499].title, "E499");
    CHECK_INT(f->nfacets, 64);
    CHECK_INT(f->entries[7].nacq, 32);
    /* 170 words of "w\xc3\xa9rd" = 1019 bytes, then " w\xc3\xa9r" fills the 1024 and the "d" no longer fits */
    CHECK_INT(strlen(f->entries[7].summary), 1024);
    CHECK(strcmp(f->entries[7].summary + 1019, " w\xc3\xa9r") == 0);
    opds_feed_free(f);
    free(b);

    b = (char *)malloc(cap);
    n = (size_t)snprintf(b, cap, "{\"metadata\":{\"title\":\"J\"},\"publications\":[");
    for (i = 0; i < 600; i++) n += (size_t)snprintf(b + n, cap - n, "%s{\"metadata\":{\"title\":\"P%d\"}}", i ? "," : "", i);
    n += (size_t)snprintf(b + n, cap - n, "]}");
    CHECK_INT(opds_parse(b, n, NULL, "http://x/", &f, NULL, 0), 0);
    CHECK_INT(f->nentries, 500);
    CHECK_INT(f->truncated, 1);
    opds_feed_free(f);
    free(b);
}

static void test_errors(void)
{
    char err[16];
    opds_feed *f = (opds_feed *)1;
    char *big;
    int i;
    CHECK_INT(opds_parse(NULL, 0, NULL, "http://x/", &f, err, sizeof err), -1);
    CHECK(f == NULL);
    CHECK_INT(PR("", NULL), -1);
    CHECK_INT(PR("  \r\n\t ", NULL), -1);
    CHECK_INT(PR("hello", NULL), -1);
    CHECK_INT(PR("<html><body>404</body></html>", "text/html"), -1);
    CHECK_INT(PR("<feed/>", NULL), 0);
    CHECK_INT(PR("{}", NULL), -1);
    CHECK_INT(PR("[]", "application/json"), -1);
    CHECK_INT(PR("{\"metadata\":", NULL), -1);
    CHECK_INT(PR("{\"metadata\":{}}", NULL), 0);
    CHECK_INT(PR("{\"metadata\":{}}", "application/atom+xml"), 0); /* sniffed: a JSON body is JSON */
    CHECK_INT(PR("<feed/>", "application/opds+json"), -1);
    CHECK_INT(PR("<feed><entry></feed>", NULL), -1);
    CHECK_INT(PR("<feed/><feed/>", NULL), -1);
    CHECK_INT(opds_parse("hello", 5, NULL, "http://x/", &f, err, 1), -1); /* tiny error buffer */
    CHECK(err[0] == '\0');
    big = (char *)calloc(OPDS_MAX_BODY + 1, 1);
    memset(big, ' ', OPDS_MAX_BODY + 1);
    memcpy(big, "<feed/>", 7); /* a valid feed padded past the limit: only the size check can reject it */
    CHECK_INT(opds_parse(big, OPDS_MAX_BODY + 1, NULL, "http://x/", &f, err, sizeof err), -1);
    free(big);
    big = (char *)malloc(20000);
    memset(big, '[', 20000);
    memcpy(big, "{\"a\":", 5);
    CHECK_INT(parse_rc(big, 20000, NULL), -1); /* absurd nesting: an error, not a stack overflow */
    for (i = 0; i < 20000; i += 7) memcpy(big + i, "<a><b/>", i + 7 <= 20000 ? 7 : 20000 - i);
    CHECK_INT(parse_rc(big, 20000, NULL), -1);
    free(big);
    opds_feed_free(NULL);
}

/* Every strict prefix of the feed is an error and none may touch memory outside its exact-size copy. */
static void truncation(const char *name, const char *base, char last)
{
    size_t n, i, full;
    char *src = slurp(name, &n), *copy;
    if (!src) return;
    full = (size_t)(strrchr(src, last) - src) + 1; /* just past the final '>' or '}' */
    for (i = 0; i <= n; i++) {
        opds_feed *f = NULL;
        int rc;
        copy = (char *)malloc(i ? i : 1);
        memcpy(copy, src, i);
        rc = opds_parse(copy, i, NULL, base, &f, NULL, 0);
        g_checks++;
        if ((i < full) != (rc != 0)) {
            g_failed++;
            printf("FAIL: %s prefix %zu of %zu: rc=%d\n", name, i, n, rc);
        }
        opds_feed_free(f);
        free(copy);
    }
    free(src);
}

/* Deterministic byte corruption: must never crash or leak whatever the result. */
static void mutate(const char *name, const char *base)
{
    size_t n, i;
    char *src = slurp(name, &n);
    unsigned long s = 12345;
    int round;
    if (!src) return;
    for (round = 0; round < 1500; round++) {
        char *copy = (char *)malloc(n);
        opds_feed *f = NULL;
        memcpy(copy, src, n);
        for (i = 0; i < 1 + (size_t)(round % 4); i++) {
            s = s * 1103515245UL + 12345UL;
            copy[(s >> 8) % n] = (char)(s >> 20);
        }
        if (opds_parse(copy, n, NULL, base, &f, NULL, 0) == 0) CHECK(f && f->title != NULL);
        else CHECK(f == NULL);
        opds_feed_free(f);
        free(copy);
    }
    free(src);
}

static void test_robustness(void)
{
    truncation("komga_series.opds.xml", KOM, '>');
    truncation("calibreweb.opds.xml", "http://cw.local/opds/new", '>');
    truncation("opds2_spec_feed.json", "https://example.com/", '}');
    mutate("komga_series.opds.xml", KOM);
    mutate("gutenberg_book.opds.xml", "https://www.gutenberg.org/ebooks/1342.opds");
    mutate("opds2_spec_feed.json", "https://example.com/");
}

int main(void)
{
    test_strip_html();
    test_edge_cases();
    test_json_odd_shapes();
    test_caps();
    test_errors();
    test_robustness();
    return TEST_DONE("opds_edge");
}
