#include "test.h"

#include "net/xml.h"

#include <stdlib.h>
#include <string.h>

/* Run the parser over a copy of `xml` held in an exactly-sized heap block (so ASan sees any
 * overrun) and render the event stream: <name a=v> ... text ... </name>, then "EOF" or
 * "ERR:message". */
static void trace_n(const char *xml, size_t len, char *out, size_t cap)
{
    char *buf = malloc(len + 1);
    xml_parser *p = malloc(sizeof *p);
    size_t n = 0;
    int i;

    memcpy(buf, xml, len);
    xml_init(p, buf, len);
    out[0] = '\0';
    for (;;) {
        xml_event ev = xml_next(p);
        char piece[512];
        if (ev == XML_START) {
            n = (size_t)snprintf(piece, sizeof piece, "<%s", p->name);
            for (i = 0; i < p->nattrs; i++)
                n += (size_t)snprintf(piece + n, sizeof piece - n, " %s=%s", p->attrs[i].name,
                                      p->attrs[i].value);
            snprintf(piece + n, sizeof piece - n, ">");
        } else if (ev == XML_END) {
            snprintf(piece, sizeof piece, "</%s>", p->name);
        } else if (ev == XML_TEXT) {
            snprintf(piece, sizeof piece, "[%s]", p->text);
            if (strlen(p->text) != p->text_len)
                snprintf(piece, sizeof piece, "[BADLEN]");
        } else if (ev == XML_EOF) {
            snprintf(piece, sizeof piece, "EOF");
        } else {
            snprintf(piece, sizeof piece, "ERR:%s", p->error ? p->error : "(null)");
        }
        if (strlen(out) + strlen(piece) < cap)
            strcat(out, piece);
        if (ev == XML_EOF || ev == XML_ERROR)
            break;
    }
    free(p);
    free(buf);
}

static void trace(const char *xml, char *out, size_t cap)
{
    trace_n(xml, strlen(xml), out, cap);
}

#define T(xml, want)                                                                           \
    do {                                                                                       \
        char o__[4096];                                                                        \
        trace((xml), o__, sizeof o__);                                                         \
        CHECK_STR(o__, (want));                                                                \
    } while (0)

static int is_err(const char *xml)
{
    char o[4096];
    trace(xml, o, sizeof o);
    return strstr(o, "ERR:") != NULL;
}

static void test_basics(void)
{
    T("<a/>", "<a></a>EOF");
    T("<a></a>", "<a></a>EOF");
    T("<a>hi</a>", "<a>[hi]</a>EOF");
    T("<a x=\"1\" y='two'>t</a>", "<a x=1 y=two>[t]</a>EOF");
    T("<a  x = \"1\"\n\ty='2' />", "<a x=1 y=2></a>EOF");
    T("<a><b/><c>x</c></a>", "<a><b></b><c>[x]</c></a>EOF");
    T("<a>one<b/>two</a>", "<a>[one]<b></b>[two]</a>EOF");
    T("  \n<a>\n  <b>x</b>\n</a>\n", "<a>[\n  ]<b>[x]</b>[\n]</a>EOF");
    T("<a>\xC3\xA9\xE2\x82\xAC</a>", "<a>[\xC3\xA9\xE2\x82\xAC]</a>EOF");
    T("<a\n>t</a\n>", "<a>[t]</a>EOF");
    T("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<a>t</a>\n", "<a>[t]</a>EOF");
    T("\xEF\xBB\xBF<a>t</a>", "<a>[t]</a>EOF");
}

static void test_entities(void)
{
    T("<a>&lt;&gt;&amp;&quot;&apos;</a>", "<a>[<>&\"']</a>EOF");
    T("<a>&#65;&#x42;&#x63;&#233;&#x20AC;&#x1F600;</a>", "<a>[ABc\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80]</a>EOF");
    /* invalid references become U+FFFD, they never produce NUL or surrogates */
    T("<a>&#0;&#xD800;&#x110000;&#9999999;</a>", "<a>[\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD]</a>EOF");
    /* unknown / malformed references stay as typed */
    T("<a>&nbsp;&foo;&amp &#;&#x;&#xZZ; & &</a>", "<a>[&nbsp;&foo;&amp &#;&#x;&#xZZ; & &]</a>EOF");
    T("<a>&amp;amp;</a>", "<a>[&amp;]</a>EOF");
    T("<a>&", "<a>[&]ERR:unexpected end of input");
    T("<a x=\"1\"y=\"2\"/>", "<a x=1 y=2></a>EOF"); /* missing space between attributes is tolerated */
    T("<a t=\"a&amp;b&lt;c&#34;d&#x27;\">x</a>", "<a t=a&b<c\"d'>[x]</a>EOF");
    T("<a t='&quot;q&quot;'/>", "<a t=\"q\"></a>EOF");
    T("<a t=\"&unknown;\"/>", "<a t=&unknown;></a>EOF");
    T("<a>&#x1F600</a>", "<a>[&#x1F600]</a>EOF");
}

static void test_cdata_comments(void)
{
    T("<a><![CDATA[<b>&amp;</b>]]></a>", "<a>[<b>&amp;</b>]</a>EOF");
    T("<a>x<![CDATA[y]]>z</a>", "<a>[xyz]</a>EOF");
    T("<a>&lt;<![CDATA[&lt;]]>&lt;</a>", "<a>[<&lt;<]</a>EOF");
    T("<a><![CDATA[]]></a>", "<a></a>EOF");
    T("<a><![CDATA[a]]b]]></a>", "<a>[a]]b]</a>EOF");
    T("<a>x<!-- c -->y<!-- <b> --></a>", "<a>[xy]</a>EOF");
    T("<!-- lead --><a><!--x-->t</a><!-- trail -->", "<a>[t]</a>EOF");
    T("<a><?pi data?>t</a>", "<a>[t]</a>EOF");
    T("<a>t<?pi a>b ?>u</a>", "<a>[tu]</a>EOF");
    T("<!DOCTYPE a [<!ENTITY e \"x>y\"> <!-- ] -->]><a>&e;</a>", "<a>[&e;]</a>EOF");
    T("<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0//EN\" \"http://www.w3.org/x.dtd\"><a/>", "<a></a>EOF");
}

static void test_names_and_ns(void)
{
    char buf[] = "<feed xmlns=\"http://www.w3.org/2005/Atom\" xmlns:dc=\"http://purl.org/dc/\">"
                 "<dc:title xml:lang=\"en\" dc:x=\"1\" plain=\"2\">t</dc:title>"
                 "<inner xmlns=\"urn:other\" xmlns:dc=\"urn:shadow\"><dc:y/><z/></inner>"
                 "<after/></feed>";
    xml_parser p;
    const char *local = NULL;

    xml_init(&p, buf, strlen(buf));
    CHECK_INT(xml_next(&p), XML_START);
    CHECK_STR(p.name, "feed");
    CHECK_STR(xml_resolve(&p, p.name, 0, &local), "http://www.w3.org/2005/Atom");
    CHECK_STR(local, "feed");
    CHECK_INT(xml_next(&p), XML_START);
    CHECK_STR(p.name, "dc:title");
    CHECK_STR(xml_resolve(&p, p.name, 0, &local), "http://purl.org/dc/");
    CHECK_STR(local, "title");
    CHECK(xml_resolve(&p, "plain", 1, &local) == NULL); /* attributes ignore the default ns */
    CHECK_STR(local, "plain");
    CHECK_STR(xml_resolve(&p, "dc:x", 1, NULL), "http://purl.org/dc/");
    CHECK_STR(xml_resolve(&p, "xml:lang", 1, NULL), "http://www.w3.org/XML/1998/namespace");
    CHECK_STR(xml_attr_value(&p, "xml:lang"), "en");
    CHECK(xml_attr_value(&p, "missing") == NULL);
    CHECK(xml_resolve(&p, "nope:x", 0, NULL) == NULL);
    CHECK_INT(xml_next(&p), XML_TEXT);
    CHECK_INT(xml_next(&p), XML_END);
    CHECK_STR(p.name, "dc:title");
    CHECK_STR(xml_resolve(&p, "dc:title", 0, NULL), "http://purl.org/dc/");
    CHECK_INT(xml_next(&p), XML_START);
    CHECK_STR(p.name, "inner");
    CHECK_STR(xml_resolve(&p, "inner", 0, NULL), "urn:other");
    CHECK_STR(xml_resolve(&p, "dc:q", 0, NULL), "urn:shadow");
    CHECK_INT(xml_next(&p), XML_START); /* dc:y */
    CHECK_INT(xml_next(&p), XML_END);
    CHECK_STR(xml_resolve(&p, "dc:y", 0, NULL), "urn:shadow");
    CHECK_INT(xml_next(&p), XML_START); /* z */
    CHECK_STR(xml_resolve(&p, "z", 0, NULL), "urn:other");
    CHECK_INT(xml_next(&p), XML_END);
    CHECK_INT(xml_next(&p), XML_END); /* inner */
    CHECK_INT(xml_next(&p), XML_START);
    CHECK_STR(p.name, "after");
    /* the declarations of <inner> went out of scope */
    CHECK_STR(xml_resolve(&p, "after", 0, NULL), "http://www.w3.org/2005/Atom");
    CHECK_STR(xml_resolve(&p, "dc:q", 0, NULL), "http://purl.org/dc/");
    CHECK_INT(xml_next(&p), XML_END);
    CHECK_INT(xml_next(&p), XML_END); /* feed */
    CHECK_INT(xml_next(&p), XML_EOF);
    CHECK_INT(xml_next(&p), XML_EOF);

    {
        char b2[] = "<a xmlns=\"\"><b/></a>";
        xml_init(&p, b2, strlen(b2));
        CHECK_INT(xml_next(&p), XML_START);
        CHECK(xml_resolve(&p, "a", 0, NULL) == NULL); /* xmlns="" means no namespace */
    }
}

static void test_errors(void)
{
    CHECK(is_err(""));
    CHECK(is_err("   "));
    CHECK(is_err("just text"));
    CHECK(is_err("<a>"));
    CHECK(is_err("<a></b>"));
    CHECK(is_err("<a><b></a></b>"));
    CHECK(is_err("</a>"));
    CHECK(is_err("<a/><b/>"));
    CHECK(is_err("<a/>text"));
    CHECK(is_err("text<a/>"));
    CHECK(is_err("<a x=1/>"));
    CHECK(is_err("<a x/>"));
    CHECK(is_err("<a x=\"1/>"));
    CHECK(is_err("<>"));
    CHECK(is_err("<1a/>"));
    CHECK(is_err("<a/ >"));
    CHECK(is_err("<a><!-- unterminated</a>"));
    CHECK(is_err("<a><![CDATA[ unterminated</a>"));
    CHECK(is_err("<a><? unterminated</a>"));
    CHECK(is_err("<!DOCTYPE a [ <a/>"));
    T("<a><!BOGUS></a>", "<a></a>EOF"); /* any other <! declaration is skipped, not validated */
    CHECK(is_err("<a"));
    CHECK(is_err("<a x"));
    CHECK(is_err("<a x="));
    CHECK(is_err("<a></a"));
    CHECK(is_err("<a></a "));
    CHECK(is_err("<a></a x>"));
}

static void test_nul_byte(void)
{
    char o[256];
    trace_n("<a>t\0z</a>", 10, o, sizeof o);
    CHECK_STR(o, "ERR:NUL byte in input");
}

static void test_limits(void)
{
    char big[8192], o[8192];
    size_t n = 0;
    int i;

    /* depth: 64 nested elements parse, 65 do not (no recursion either way) */
    for (i = 0; i < 64; i++)
        n += (size_t)sprintf(big + n, "<e>");
    for (i = 0; i < 64; i++)
        n += (size_t)sprintf(big + n, "</e>");
    trace(big, o, sizeof o);
    CHECK(strstr(o, "EOF") != NULL);
    n = 0;
    for (i = 0; i < 65; i++)
        n += (size_t)sprintf(big + n, "<e>");
    trace(big, o, sizeof o);
    CHECK(strstr(o, "ERR:elements nested too deeply") != NULL);
    n = 0;
    for (i = 0; i < 4000; i++)
        n += (size_t)sprintf(big + n, "<");
    trace(big, o, sizeof o);
    CHECK(strstr(o, "ERR:") != NULL);

    /* 32 namespace declarations are fine, 33 are an error */
    n = (size_t)sprintf(big, "<a");
    for (i = 0; i < 32; i++)
        n += (size_t)sprintf(big + n, " xmlns:p%d=\"u%d\"", i, i);
    strcpy(big + n, "/>");
    trace(big, o, sizeof o);
    CHECK(strstr(o, "EOF") != NULL);
    n = (size_t)sprintf(big, "<a");
    for (i = 0; i < 33; i++)
        n += (size_t)sprintf(big + n, " xmlns:p%d=\"u%d\"", i, i);
    strcpy(big + n, "/>");
    trace(big, o, sizeof o);
    CHECK(strstr(o, "ERR:too many namespace declarations") != NULL);

    /* attributes beyond XML_MAX_ATTRS are parsed but not stored */
    n = (size_t)sprintf(big, "<a");
    for (i = 0; i < XML_MAX_ATTRS + 10; i++)
        n += (size_t)sprintf(big + n, " k%d=\"v\"", i);
    strcpy(big + n, ">x</a>");
    {
        xml_parser *p = malloc(sizeof *p);
        xml_init(p, big, strlen(big));
        CHECK_INT(xml_next(p), XML_START);
        CHECK_INT(p->nattrs, XML_MAX_ATTRS);
        CHECK_INT(xml_next(p), XML_TEXT);
        CHECK_STR(p->text, "x");
        free(p);
    }

    /* a long text run with entities decodes to the right length */
    {
        char *src = malloc(400000), *buf;
        xml_parser *p = malloc(sizeof *p);
        size_t len = 0, want = 0;
        len += (size_t)sprintf(src, "<a>");
        for (i = 0; i < 20000; i++) {
            len += (size_t)sprintf(src + len, "&#x20AC;&amp;x");
            want += 3 + 1 + 1;
        }
        len += (size_t)sprintf(src + len, "</a>");
        buf = malloc(len + 1);
        memcpy(buf, src, len);
        xml_init(p, buf, len);
        CHECK_INT(xml_next(p), XML_START);
        CHECK_INT(xml_next(p), XML_TEXT);
        CHECK_INT(p->text_len, want);
        CHECK_INT(strlen(p->text), want);
        CHECK_INT(xml_next(p), XML_END);
        CHECK_INT(xml_next(p), XML_EOF);
        free(p);
        free(buf);
        free(src);
    }
}

static const char *SMALL_FEED =
    "<?xml version=\"1.0\"?>\n<!DOCTYPE feed>\n"
    "<feed xmlns=\"http://www.w3.org/2005/Atom\" xmlns:dc=\"urn:dc\"><!-- c -->\n"
    "<title t=\"a&amp;b\">T &lt;1&gt; <![CDATA[<raw>]]> &#x20AC;</title>\n"
    "<entry><id>1</id><link rel=\"x\" href='/y?a=1&amp;b=2'/><dc:l>en</dc:l></entry>\n"
    "<?pi x?></feed>";

static void test_truncation(void)
{
    size_t full = strlen(SMALL_FEED), k, errors = 0;
    char o[4096];

    trace(SMALL_FEED, o, sizeof o);
    CHECK(strstr(o, "EOF") != NULL && strstr(o, "ERR") == NULL);
    /* Every proper prefix is an error (never EOF, never a crash under ASan/UBSan). */
    for (k = 0; k < full; k++) {
        trace_n(SMALL_FEED, k, o, sizeof o);
        if (strstr(o, "ERR:") != NULL)
            errors++;
        else
            printf("FAIL prefix %zu parsed without error: %s\n", k, o);
        g_checks++;
        if (strstr(o, "ERR:") == NULL)
            g_failed++;
    }
    CHECK_INT(errors, full);
}

static void test_garbage(void)
{
    /* Deterministic noise over a feed skeleton must never crash or hang. */
    char o[8192];
    unsigned seed = 12345;
    int round, ok = 0;
    size_t len = strlen(SMALL_FEED);

    for (round = 0; round < 2000; round++) {
        char mutated[512];
        size_t i;
        memcpy(mutated, SMALL_FEED, len + 1);
        for (i = 0; i < 4; i++) {
            seed = seed * 1103515245u + 12345u;
            mutated[(seed >> 8) % len] = (char)(seed >> 3);
            if (mutated[(seed >> 8) % len] == '\0')
                mutated[(seed >> 8) % len] = '<';
        }
        trace_n(mutated, len, o, sizeof o);
        if (strstr(o, "EOF") != NULL || strstr(o, "ERR:") != NULL)
            ok++;
    }
    CHECK_INT(ok, 2000);
}

int main(void)
{
    test_basics();
    test_entities();
    test_cdata_comments();
    test_names_and_ns();
    test_errors();
    test_nul_byte();
    test_limits();
    test_truncation();
    test_garbage();
    return TEST_DONE("xml");
}
