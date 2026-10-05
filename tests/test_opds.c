/* net/opds against real-world-shaped feeds (Gutenberg, Open Library, Komga, Calibre-Web, OPDS 2
 * spec examples) and the acquisition/PSE/OpenSearch helpers. Hostile input, caps and truncation
 * are in test_opds_edge.c. Fixtures live in tests/fixtures/opds (run from tests/). */
#include "opds_testutil.h"

static void test_gutenberg(void)
{
    opds_feed *f = load("gutenberg_root.opds.xml", GUT, OPDS_XML);
    opds_entry *e;
    int i;
    if (!f) return;
    CHECK_INT(f->version, 1);
    CHECK_STR(f->title, "Project Gutenberg");
    CHECK_STR(f->subtitle, "Free eBooks since 1971.");
    CHECK_STR(f->self, "https://www.gutenberg.org/ebooks.opds/");
    CHECK_STR(f->start, "https://www.gutenberg.org/ebooks.opds/");
    CHECK_STR(f->search_href, "https://www.gutenberg.org/catalog/osd-books.xml");
    CHECK_INT(f->search_is_template, 0);
    CHECK_INT(f->nentries, 3);
    CHECK_STR(f->entries[0].title, "Popular");
    CHECK_STR(f->entries[0].nav_href, "https://www.gutenberg.org/ebooks/search.opds/?sort_order=downloads");
    CHECK_STR(f->entries[0].summary, "Our most popular books.");
    CHECK_INT(f->total, -1);
    for (i = 0; i < f->nentries; i++) {
        CHECK(f->entries[i].thumb == NULL && f->entries[i].cover == NULL); /* data: URIs dropped */
        CHECK(f->entries[i].author == NULL); /* the feed-level author does not leak */
        CHECK(f->entries[i].nacq == 0);
    }
    opds_feed_free(f);

    f = load("gutenberg_search.opds.xml", "https://www.gutenberg.org/ebooks/search.opds/?query=dickens", OPDS_XML);
    if (!f) return;
    CHECK_INT(f->nentries, 4);
    CHECK_STR(f->next, "https://www.gutenberg.org/ebooks/search.opds/?query=dickens&start_index=26");
    CHECK_STR(f->entries[2].title, "Great Expectations");
    CHECK_STR(f->entries[2].summary, "Charles Dickens");
    CHECK_STR(f->entries[2].nav_href, "https://www.gutenberg.org/ebooks/1400.opds");
    opds_feed_free(f);

    f = load("gutenberg_book.opds.xml", "https://www.gutenberg.org/ebooks/1342.opds", NULL);
    if (!f) return;
    CHECK_INT(f->nentries, 2);
    e = &f->entries[0];
    CHECK_STR(e->author, "Austen, Jane");
    CHECK_STR(e->id, "urn:gutenberg:1342:2");
    CHECK(e->summary && strncmp(e->summary, "This edition had all images removed.\nTitle: Pride and Prejudice\nNote:", 69) == 0);
    CHECK(e->summary && strlen(e->summary) <= OPDS_SUMMARY_MAX && strlen(e->summary) > 900);
    CHECK(e->summary && strchr(e->summary, '<') == NULL);
    CHECK_STR(e->cover, "https://www.gutenberg.org/cache/epub/1342/pg1342.cover.medium.jpg");
    CHECK_STR(e->thumb, "https://www.gutenberg.org/cache/epub/1342/pg1342.cover.small.jpg");
    CHECK_INT(e->nacq, 2);
    CHECK_STR(e->acq[0].href, "https://www.gutenberg.org/ebooks/1342.epub.noimages");
    CHECK_STR(e->acq[0].type, "application/epub+zip");
    CHECK_STR(e->acq[0].title, "EPUB (older e-readers, no images)");
    CHECK_INT(e->acq[0].length, 561102);
    CHECK_INT(e->acq[0].kind, OPDS_ACQ_GENERIC);
    CHECK_INT(f->entries[1].nacq, 4);
    CHECK_STR(opds_best_acquisition(&f->entries[1], NULL)->href, "https://www.gutenberg.org/ebooks/1342.epub3.images");
    {
        const char *mobi[] = { "mobi", "epub", NULL };
        CHECK_STR(opds_best_acquisition(&f->entries[1], mobi)->href, "https://www.gutenberg.org/ebooks/1342.kf8.images");
    }
    opds_feed_free(f);
}

static void test_komga(void)
{
    opds_feed *f = load("komga_series.opds.xml", KOM, "application/xml");
    opds_entry *e;
    char url[URL_MAX];
    if (!f) return;
    CHECK_INT(f->total, 2);
    CHECK_STR(f->prev, "https://komga.example.org/opds/v1.2/series/0A1B2C3D?page=0");
    CHECK_STR(f->up, "https://komga.example.org/opds/v1.2/series");
    CHECK_STR(f->search_href, "https://komga.example.org/opds/v1.2/search");
    CHECK_INT(f->nfacets, 2);
    CHECK_STR(f->facets[0].group, "Sort");
    CHECK_STR(f->facets[0].title, "Oldest first");
    CHECK_INT(f->facets[0].active, 1);
    CHECK_INT(f->facets[1].active, 0);
    CHECK_INT(f->nentries, 3);
    e = &f->entries[0];
    CHECK_STR(e->title, "One Piece 001 & Prologue");
    CHECK_STR(e->author, "Eiichiro Oda; Shueisha \xe2\x80\x93 Team");
    CHECK_STR(e->summary, "Luffy sets sail.\nSecond line with & entity.");
    CHECK_INT(e->pse_count, 54);
    CHECK_STR(e->pse_href, "https://komga.example.org/api/v1/books/BOOK0001/pages/{pageNumber}?zero_based=true");
    CHECK_INT(opds_pse_url(e, 0, 0, url, sizeof url), 0);
    CHECK_STR(url, "https://komga.example.org/api/v1/books/BOOK0001/pages/0?zero_based=true");
    CHECK_INT(opds_pse_url(e, 53, 0, url, 8), -1);
    CHECK_STR(opds_acq_format(&e->acq[0]), "cbz"); /* application/zip, decided by the .cbz href */
    CHECK_STR(opds_best_acquisition(e, NULL)->href,
              "https://komga.example.org/api/v1/books/BOOK0001/file/One%20Piece%20001.cbz");
    e = &f->entries[1];
    CHECK_INT(e->pse_count, 198);
    CHECK_INT(opds_pse_url(e, 7, 480, url, sizeof url), 0);
    CHECK_STR(url, "https://komga.example.org/api/opds/KEY123/image?libraryId=1&seriesId=9&chapterId=22&pageNumber=7&maxWidth=480");
    CHECK_INT(e->nacq, 3);
    CHECK_STR(opds_best_acquisition(e, NULL)->href, "https://komga.example.org/api/v1/books/BOOK0002/file/One%20Piece%20002.epub");
    {
        const char *pdf[] = { "pdf", NULL }, *cbz[] = { "cbz", "epub", NULL };
        CHECK_STR(opds_best_acquisition(e, pdf)->type, "application/pdf");
        CHECK_STR(opds_best_acquisition(e, cbz)->type, "application/vnd.comicbook+zip");
    }
    CHECK_INT(opds_pse_url(&f->entries[2], 0, 0, url, sizeof url), -1);
    CHECK_STR(f->entries[2].nav_href, "https://komga.example.org/opds/v1.2/series/0A1B2C3E");
    CHECK(f->entries[2].cover == NULL);
    CHECK_STR(f->entries[2].thumb, "https://komga.example.org/api/v1/series/0A1B2C3E/thumbnail");
    opds_feed_free(f);

    f = load("komga_catalog.opds.xml", "https://komga.example.org/opds/v1.2/catalog", NULL);
    if (!f) return;
    CHECK_INT(f->nentries, 2);
    CHECK_STR(f->entries[1].nav_href, "https://komga.example.org/opds/v1.2/series");
    opds_feed_free(f);
}

static void test_calibre_and_search(void)
{
    opds_feed *f = load("calibreweb.opds.xml", "http://cw.local:8083/opds/new", NULL);
    char url[URL_MAX], tmpl[URL_MAX], err[64], *b;
    size_t n;
    if (f) {
        opds_entry *e = &f->entries[0];
        CHECK_INT(f->search_is_template, 1); /* the template link beats the OpenSearch description */
        CHECK_STR(f->search_href, "http://cw.local:8083/opds/search/{searchTerms}");
        CHECK_INT(url_expand_search(url, sizeof url, f->search_href, "war & peace"), 0);
        CHECK_STR(url, "http://cw.local:8083/opds/search/war%20%26%20peace");
        CHECK_STR(f->next, "http://cw.local:8083/opds/new?offset=60");
        CHECK_STR(e->summary, "A scientist builds a machine and travels to the year 802,701.\nTags: Science Fiction, Classics");
        CHECK_STR(e->cover, "http://cw.local:8083/opds/cover/12?full=1");
        CHECK_STR(e->thumb, "http://cw.local:8083/opds/cover/12?thumb=1");
        CHECK_INT(e->acq[2].length, 900100);
        CHECK_STR(f->entries[1].nav_href, "http://cw.local:8083/opds/shelf/1");
        opds_feed_free(f);
    }
    b = slurp("gutenberg_osd.xml", &n);
    CHECK_INT(opds_parse_opensearch(b, n, "https://www.gutenberg.org/catalog/osd-books.xml", tmpl, sizeof tmpl, err, sizeof err), 0);
    CHECK_STR(tmpl, "http://m.gutenberg.org/ebooks/search.opds/?query={searchTerms}"); /* not text/html, not suggestions */
    CHECK_INT(opds_parse_opensearch(b, n, NULL, tmpl, 10, err, sizeof err), -1);
    free(b);
    b = slurp("komga_search.osd.xml", &n);
    CHECK_INT(opds_parse_opensearch(b, n, "https://komga.example.org/opds/v1.2/search", tmpl, sizeof tmpl, err, sizeof err), 0);
    CHECK_STR(tmpl, "https://komga.example.org/opds/v1.2/search?search={searchTerms}&page={startPage?}");
    CHECK_INT(url_expand_search(url, sizeof url, tmpl, "one piece"), 0);
    CHECK_STR(url, "https://komga.example.org/opds/v1.2/search?search=one%20piece&page=");
    free(b);
    {
        const char *html_only = "<OpenSearchDescription xmlns=\"http://a9.com/-/spec/opensearch/1.1/\"><Url type=\"text/html\" template=\"/s?q={searchTerms}\"/></OpenSearchDescription>";
        CHECK_INT(opds_parse_opensearch(html_only, strlen(html_only), "http://x/", tmpl, sizeof tmpl, err, sizeof err), -1);
        CHECK(err[0] != '\0');
        CHECK_INT(opds_parse_opensearch("<a><b>", 6, "http://x/", tmpl, sizeof tmpl, err, sizeof err), -1);
        CHECK_INT(opds_parse_opensearch("", 0, "http://x/", tmpl, sizeof tmpl, NULL, 0), -1);
    }
}

static void test_opds2(void)
{
    opds_feed *f = load("opds2_spec_feed.json", "https://example.com/opds/new.json?page=1", "application/opds+json");
    opds_entry *e;
    if (f) {
        CHECK_INT(f->version, 2);
        CHECK_STR(f->title, "Example listing publications");
        CHECK_INT(f->total, 5);
        CHECK_STR(f->next, "https://example.com/opds/new.json?page=2");
        CHECK_STR(f->prev, "https://example.com/opds/new.json?page=0");
        CHECK_STR(f->start, "https://example.com/opds/root.json");
        CHECK_INT(f->search_is_template, 1);
        CHECK_STR(f->search_href, "https://example.com/opds/search{?query}");
        CHECK_INT(f->nfacets, 2);
        CHECK_INT(f->facets[0].active, 1);
        CHECK_STR(f->facets[1].href, "https://example.com/opds/new.json?sort=title");
        CHECK_INT(f->nentries, 2);
        e = &f->entries[0];
        CHECK_STR(e->title, "Moby-Dick");
        CHECK_STR(e->author, "Herman Melville");
        CHECK_STR(e->id, "urn:isbn:978-3-16-148410-0");
        CHECK_STR(e->updated, "2015-09-29T17:00:00Z");
        CHECK_STR(e->summary, "Call me Ishmael. Some years ago \xe2\x80\x94 never mind how long & precisely \xe2\x80\x93 having little money.\nSecond paragraph.");
        CHECK_STR(e->cover, "https://example.com/img/moby-large.jpg");
        CHECK_STR(e->thumb, "https://example.com/img/moby-small.jpg");
        CHECK_INT(e->nacq, 3);
        CHECK_INT(e->acq[0].kind, OPDS_ACQ_OPEN_ACCESS);
        CHECK_INT(e->acq[2].kind, OPDS_ACQ_SAMPLE);
        CHECK_STR(e->acq[0].href, "https://example.com/files/moby-dick.epub");
        CHECK_STR(opds_best_acquisition(e, NULL)->href, "https://example.com/files/moby-dick.epub"); /* not the sample */
        e = &f->entries[1];
        CHECK_STR(e->author, "Jane Doe; John Roe; Plain String");
        CHECK_INT(e->nacq, 1);
        CHECK_INT(e->acq[0].kind, OPDS_ACQ_BUY);
        CHECK(opds_best_acquisition(e, NULL) == NULL); /* nothing downloadable */
        CHECK_STR(e->cover, "https://img.example.com/c.png");
        CHECK(e->thumb == NULL);
        opds_feed_free(f);
    }
    f = load("opds2_spec_navigation.json", "https://example.com/opds/root.json", NULL);
    if (f) {
        CHECK_INT(f->nentries, 4);
        CHECK_STR(f->entries[0].nav_href, "https://example.com/opds/new.json");
        CHECK(f->entries[0].group == NULL);
        CHECK_STR(f->entries[2].group, "Staff picks");
        CHECK_STR(opds_best_acquisition(&f->entries[2], NULL)->href, "https://example.com/files/one.cbz");
        CHECK_STR(f->entries[3].group, "Genres");
        CHECK_STR(f->entries[3].title, "Science Fiction");
        opds_feed_free(f);
    }
    f = load("openlibrary_search.json", "https://openlibrary.org/opds/search?query=open_access", "application/opds+json");
    if (f) {
        CHECK_INT(f->total, 99);
        CHECK_STR(f->first, "https://openlibrary.org/opds/search?query=open_access&limit=2&mode=open_access&page=1");
        CHECK_STR(f->search_href, "https://openlibrary.org/opds/search{?query,language,mode,media_type}");
        CHECK_INT(f->nfacets, 8);
        CHECK_STR(f->entries[0].cover, "https://covers.openlibrary.org/b/id/9785893-L.jpg");
        CHECK_INT(f->entries[0].nacq, 0);
        CHECK_STR(f->entries[0].author, "Aaron Swartz");
        opds_feed_free(f);
    }
    f = load("openlibrary_root.json", "https://openlibrary.org/opds", NULL);
    if (f) {
        CHECK_INT(f->nentries, 7);
        CHECK_STR(f->entries[3].group, "Trending Books");
        CHECK_INT(f->entries[3].nacq, 1);
        CHECK_STR(opds_acq_format(&f->entries[3].acq[0]), "epub");
        opds_feed_free(f);
    }
}

int main(void)
{
    test_gutenberg();
    test_komga();
    test_calibre_and_search();
    test_opds2();
    return TEST_DONE("opds");
}
