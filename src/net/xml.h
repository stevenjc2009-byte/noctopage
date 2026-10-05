/* xml - tiny non-validating XML pull parser (pure C, no allocation, no recursion).
 * Sources: xml.c (parser) and xml_util.c (entities, UTF-8, namespace lookups); link both.
 *
 * The parser works IN PLACE on a caller-owned, writable buffer: names, attribute values and
 * text are decoded (entities, CDATA) and NUL-terminated inside that buffer, and the events point
 * into it. The buffer must hold len + 1 bytes (room for one terminating NUL) and must stay alive
 * and untouched while events are in use. Decoded text is never longer than its source, so this
 * is always possible.
 *
 *   xml_parser p;
 *   xml_init(&p, buf, len);
 *   for (;;) {
 *       xml_event ev = xml_next(&p);
 *       if (ev == XML_START) ... p.name, p.attrs[0..p.nattrs) ...
 *       else if (ev == XML_TEXT) ... p.text ...
 *       else if (ev == XML_END) ... p.name ...
 *       else break;                       // XML_EOF or XML_ERROR (p.error)
 *   }
 *
 * Events: XML_START (an element opened; a self-closing element yields START then END), XML_TEXT
 * (character data of the enclosing element, CDATA merged in, entities decoded, never empty),
 * XML_END, XML_EOF (root closed), XML_ERROR (malformed or truncated input; sticky). Comments,
 * processing instructions, the XML declaration and DOCTYPE are skipped. Custom entities from a
 * DOCTYPE are not expanded (unknown "&name;" stays as typed). The input is taken as UTF-8.
 * Element and attribute names keep their prefix ("dcterms:language"); xml_resolve() maps a
 * prefix to its namespace URI using the xmlns declarations in scope.
 * Limits: depth XML_MAX_DEPTH, XML_MAX_NS declarations in scope (error beyond); attributes past
 * XML_MAX_ATTRS are parsed but not stored. A NUL byte in the input is an error. */
#ifndef NOCTO_XML_H
#define NOCTO_XML_H

#include <stddef.h>

#define XML_MAX_DEPTH 64
#define XML_MAX_ATTRS 48
#define XML_MAX_NS    32

typedef enum { XML_START, XML_END, XML_TEXT, XML_EOF, XML_ERROR } xml_event;

typedef struct {
    char *name;  /* qualified name as written */
    char *value; /* decoded */
} xml_attr;

typedef struct {
    char *prefix; /* "" = default namespace */
    char *uri;
    int depth;
} xml_ns;

typedef struct {
    char *buf;
    size_t len, pos;
    int depth;        /* START/END: depth of that element (root = 1); TEXT: depth of its parent */
    int pending_end;  /* synthesized END for a self-closing element */
    int pop_pending;  /* the element returned by the last END is popped on the next call */
    int seen_root, failed;
    size_t saved_pos; /* byte overwritten by the NUL that terminates the last TEXT */
    char saved_char;
    /* current event */
    char *name;
    xml_attr attrs[XML_MAX_ATTRS];
    int nattrs;
    char *text;
    size_t text_len;
    const char *error; /* set with XML_ERROR; static string */
    char *stack[XML_MAX_DEPTH];
    xml_ns ns[XML_MAX_NS];
    int nns;
} xml_parser;

/* buf must be writable with room for len + 1 bytes. */
void xml_init(xml_parser *p, char *buf, size_t len);

xml_event xml_next(xml_parser *p);

/* Namespace URI of a qualified name in the current scope, or NULL (no namespace / undeclared
 * prefix). An unprefixed ELEMENT name takes the default namespace; an unprefixed ATTRIBUTE
 * name has none. *local (optional) receives the part after the colon. */
const char *xml_resolve(const xml_parser *p, const char *qname, int is_attr, const char **local);

/* Value of the attribute with this qualified name on the current START, or NULL. */
const char *xml_attr_value(const xml_parser *p, const char *qname);

/* Encode a code point as UTF-8 into out[4]; returns the length, or 0 for an invalid code point
 * (0, surrogates, above U+10FFFF). */
size_t xml_utf8_encode(unsigned long cp, char out[4]);

/* If s (NUL-terminated, at '&') is a character or predefined entity reference
 * (&amp; &lt; &gt; &quot; &apos; &#N; &#xH;) write its UTF-8 to out[4] and return the number of
 * bytes written (1..4), storing the source length in *consumed. Returns 0 when s is not a
 * recognised reference. An invalid numeric reference yields U+FFFD. */
size_t xml_entity(const char *s, size_t avail, char out[4], size_t *consumed);

#endif
