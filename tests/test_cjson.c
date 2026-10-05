#include "test.h"

#include "cjson/cJSON.h"

/* An OPDS 2.0 feed in the shape the catalog screen will parse. */
static const char *OPDS2 =
    "{"
    "\"metadata\": {\"title\": \"Standard Ebooks\", \"numberOfItems\": 2, \"itemsPerPage\": 50},"
    "\"links\": ["
    "  {\"rel\": \"self\", \"href\": \"/opds/new\", \"type\": \"application/opds+json\"},"
    "  {\"rel\": \"next\", \"href\": \"/opds/new?page=2\", \"type\": \"application/opds+json\"}"
    "],"
    "\"publications\": ["
    "  {\"metadata\": {\"@type\": \"http://schema.org/Book\", \"title\": \"Moby \\u2013 Dick\","
    "     \"author\": [{\"name\": \"Herman Melville\"}], \"language\": \"en\"},"
    "   \"links\": [{\"rel\": \"http://opds-spec.org/acquisition/open-access\","
    "               \"href\": \"/ebooks/moby.epub\", \"type\": \"application/epub+zip\"}],"
    "   \"images\": [{\"href\": \"/covers/moby.jpg\", \"type\": \"image/jpeg\", \"width\": 400}]},"
    "  {\"metadata\": {\"title\": \"\\u6f2b\\u753b\", \"author\": {\"name\": \"Anon\"}},"
    "   \"links\": []}"
    "]}";

static void test_parse_opds2(void)
{
    cJSON *root = cJSON_Parse(OPDS2);
    cJSON *meta, *links, *pubs, *pub0, *pub1, *item;

    CHECK(root != NULL);
    if (!root)
        return;

    meta = cJSON_GetObjectItemCaseSensitive(root, "metadata");
    CHECK(cJSON_IsObject(meta));
    CHECK_STR(cJSON_GetObjectItemCaseSensitive(meta, "title")->valuestring, "Standard Ebooks");
    CHECK_INT(cJSON_GetObjectItemCaseSensitive(meta, "numberOfItems")->valueint, 2);

    links = cJSON_GetObjectItemCaseSensitive(root, "links");
    CHECK_INT(cJSON_GetArraySize(links), 2);
    item = cJSON_GetArrayItem(links, 1);
    CHECK_STR(cJSON_GetObjectItemCaseSensitive(item, "rel")->valuestring, "next");
    CHECK_STR(cJSON_GetObjectItemCaseSensitive(item, "href")->valuestring, "/opds/new?page=2");

    pubs = cJSON_GetObjectItemCaseSensitive(root, "publications");
    CHECK(cJSON_IsArray(pubs));
    CHECK_INT(cJSON_GetArraySize(pubs), 2);

    pub0 = cJSON_GetArrayItem(pubs, 0);
    meta = cJSON_GetObjectItemCaseSensitive(pub0, "metadata");
    /* – decodes to UTF-8 (E2 80 93). */
    CHECK_STR(cJSON_GetObjectItemCaseSensitive(meta, "title")->valuestring, "Moby \xE2\x80\x93 Dick");
    item = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(meta, "author"), 0);
    CHECK_STR(cJSON_GetObjectItemCaseSensitive(item, "name")->valuestring, "Herman Melville");
    item = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(pub0, "links"), 0);
    CHECK_STR(cJSON_GetObjectItemCaseSensitive(item, "type")->valuestring, "application/epub+zip");
    item = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(pub0, "images"), 0);
    CHECK_STR(cJSON_GetObjectItemCaseSensitive(item, "href")->valuestring, "/covers/moby.jpg");
    CHECK_INT(cJSON_GetObjectItemCaseSensitive(item, "width")->valueint, 400);

    pub1 = cJSON_GetArrayItem(pubs, 1);
    meta = cJSON_GetObjectItemCaseSensitive(pub1, "metadata");
    CHECK_STR(cJSON_GetObjectItemCaseSensitive(meta, "title")->valuestring, "\xE6\xBC\xAB\xE7\x94\xBB");
    /* OPDS allows author as an object or an array; callers must handle both. */
    CHECK(cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(meta, "author")));
    CHECK_INT(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(pub1, "links")), 0);
    CHECK(cJSON_GetObjectItemCaseSensitive(pub1, "images") == NULL);

    cJSON_Delete(root);
}

static void test_bad_input(void)
{
    cJSON *bounded;

    CHECK(cJSON_Parse("{\"a\": ") == NULL);
    CHECK(cJSON_Parse("") == NULL);
    CHECK(cJSON_Parse("<feed xmlns=\"http://www.w3.org/2005/Atom\"/>") == NULL); /* OPDS 1 is not JSON */
    bounded = cJSON_ParseWithLength("{\"a\":1}garbage", 7); /* length-bounded */
    CHECK(bounded != NULL);
    cJSON_Delete(bounded);
}

static void test_print_roundtrip(void)
{
    cJSON *o = cJSON_CreateObject();
    char *s;
    cJSON *back;

    cJSON_AddStringToObject(o, "title", "A \"quoted\" title");
    cJSON_AddNumberToObject(o, "page", 12);
    cJSON_AddBoolToObject(o, "rtl", 1);
    s = cJSON_PrintUnformatted(o);
    CHECK_STR(s, "{\"title\":\"A \\\"quoted\\\" title\",\"page\":12,\"rtl\":true}");
    back = cJSON_Parse(s);
    CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(back, "rtl")));
    CHECK_STR(cJSON_GetObjectItemCaseSensitive(back, "title")->valuestring, "A \"quoted\" title");
    cJSON_free(s);
    cJSON_Delete(back);
    cJSON_Delete(o);
}

int main(void)
{
    CHECK_STR(cJSON_Version(), "1.7.18");
    test_parse_opds2();
    test_bad_input();
    test_print_roundtrip();
    return TEST_DONE("cjson");
}
