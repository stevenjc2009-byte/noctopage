/* xml_util - entity decoding, UTF-8 encoding and namespace lookups for net/xml (split from xml.c). */
#include "net/xml.h"

#include <string.h>

#define XML_NS_URI "http://www.w3.org/XML/1998/namespace"

size_t xml_utf8_encode(unsigned long cp, char out[4])
{
    if (cp == 0 || cp > 0x10FFFFUL || (cp >= 0xD800UL && cp <= 0xDFFFUL))
        return 0;
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

size_t xml_entity(const char *s, size_t avail, char out[4], size_t *consumed)
{
    size_t n, i;

    if (avail < 3 || s[0] != '&')
        return 0;
    for (n = 1; n < avail && n < 12 && s[n] != ';'; n++) {
    }
    if (n >= avail || s[n] != ';' || n < 2)
        return 0;
    if (s[1] == '#') {
        unsigned long cp = 0;
        int hex = (s[2] == 'x');
        size_t first = hex ? 3 : 2;
        char tmp[4];
        size_t k;

        if (first >= n)
            return 0;
        for (i = first; i < n; i++) {
            char c = s[i];
            unsigned d;
            if (c >= '0' && c <= '9')
                d = (unsigned)(c - '0');
            else if (hex && c >= 'a' && c <= 'f')
                d = (unsigned)(c - 'a') + 10;
            else if (hex && c >= 'A' && c <= 'F')
                d = (unsigned)(c - 'A') + 10;
            else
                return 0;
            cp = cp * (hex ? 16 : 10) + d;
            if (cp > 0x10FFFFUL)
                cp = 0x110000UL; /* stays invalid and cannot overflow */
        }
        k = xml_utf8_encode(cp, tmp);
        if (k == 0)
            k = xml_utf8_encode(0xFFFD, tmp);
        memcpy(out, tmp, k);
        *consumed = n + 1;
        return k;
    }
    if (n == 3 && memcmp(s + 1, "lt", 2) == 0)
        out[0] = '<';
    else if (n == 3 && memcmp(s + 1, "gt", 2) == 0)
        out[0] = '>';
    else if (n == 4 && memcmp(s + 1, "amp", 3) == 0)
        out[0] = '&';
    else if (n == 5 && memcmp(s + 1, "quot", 4) == 0)
        out[0] = '"';
    else if (n == 5 && memcmp(s + 1, "apos", 4) == 0)
        out[0] = '\'';
    else
        return 0;
    *consumed = n + 1;
    return 1;
}

const char *xml_resolve(const xml_parser *p, const char *qname, int is_attr, const char **local)
{
    const char *colon = strchr(qname, ':');
    size_t plen;
    int i;

    if (colon == NULL) {
        if (local)
            *local = qname;
        if (is_attr)
            return NULL;
        plen = 0;
    } else {
        if (local)
            *local = colon + 1;
        plen = (size_t)(colon - qname);
        if (plen == 3 && memcmp(qname, "xml", 3) == 0)
            return XML_NS_URI;
    }
    for (i = p->nns - 1; i >= 0; i--) {
        if (strlen(p->ns[i].prefix) == plen && memcmp(p->ns[i].prefix, qname, plen) == 0)
            return p->ns[i].uri[0] ? p->ns[i].uri : NULL;
    }
    return NULL;
}

const char *xml_attr_value(const xml_parser *p, const char *qname)
{
    int i;
    for (i = 0; i < p->nattrs; i++)
        if (strcmp(p->attrs[i].name, qname) == 0)
            return p->attrs[i].value;
    return NULL;
}
