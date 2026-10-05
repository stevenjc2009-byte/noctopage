#include "net/xml.h"

#include <string.h>

#define NO_SAVE    ((size_t)-1)

static int is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

void xml_init(xml_parser *p, char *buf, size_t len)
{
    memset(p, 0, sizeof *p);
    p->buf = buf;
    p->len = len;
    p->saved_pos = NO_SAVE;
    buf[len] = '\0';
    if (len >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB &&
        (unsigned char)buf[2] == 0xBF)
        p->pos = 3;
    if (memchr(buf, 0, len) != NULL) {
        p->failed = 1;
        p->error = "NUL byte in input";
    }
}

static xml_event fail(xml_parser *p, const char *msg)
{
    p->failed = 1;
    p->error = msg;
    return XML_ERROR;
}

static int starts(const xml_parser *p, size_t at, const char *lit)
{
    size_t n = strlen(lit);
    return at + n <= p->len && memcmp(p->buf + at, lit, n) == 0;
}

/* Index of the first occurrence of lit at or after `from`, or NO_SAVE. */
static size_t find(const xml_parser *p, size_t from, const char *lit)
{
    size_t n = strlen(lit), i;
    for (i = from; i + n <= p->len; i++)
        if (p->buf[i] == lit[0] && memcmp(p->buf + i, lit, n) == 0)
            return i;
    return NO_SAVE;
}

/* Skip a declaration starting at '<!' (DOCTYPE): to the matching '>' honouring [..] and quotes. */
static int skip_doctype(xml_parser *p)
{
    size_t i = p->pos + 2;
    int brackets = 0;
    char quote = 0;

    for (; i < p->len; i++) {
        char c = p->buf[i];
        if (quote) {
            if (c == quote)
                quote = 0;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '<' && starts(p, i, "<!--")) {
            size_t e = find(p, i + 4, "-->");
            if (e == NO_SAVE)
                return -1;
            i = e + 2;
        } else if (c == '[') {
            brackets++;
        } else if (c == ']') {
            if (brackets > 0)
                brackets--;
        } else if (c == '>' && brackets == 0) {
            p->pos = i + 1;
            return 0;
        }
    }
    return -1;
}

/* Skip a comment or PI at p->pos (caller checked the opener); -1 when unterminated. */
static int skip_comment_or_pi(xml_parser *p, size_t r, size_t *next)
{
    int comment = starts(p, r, "<!--");
    size_t e = find(p, r + (comment ? 4 : 2), comment ? "-->" : "?>");
    if (e == NO_SAVE)
        return -1;
    *next = e + (comment ? 3 : 2);
    return 0;
}

/* Decode a text run in place starting at p->pos, merging CDATA and dropping comments and PIs.
 * Stops at the next real tag or the end of input. */
static xml_event read_text(xml_parser *p)
{
    char *b = p->buf;
    size_t r = p->pos, w = p->pos, start = p->pos;

    while (r < p->len) {
        char c = b[r];
        if (c == '&') {
            char out[4];
            size_t used = 0, k = xml_entity(b + r, p->len - r, out, &used);
            if (k) {
                memcpy(b + w, out, k);
                w += k;
                r += used;
                continue;
            }
        } else if (c == '<') {
            if (starts(p, r, "<![CDATA[")) {
                size_t e = find(p, r + 9, "]]>");
                if (e == NO_SAVE)
                    return fail(p, "unterminated CDATA section");
                memmove(b + w, b + r + 9, e - (r + 9));
                w += e - (r + 9);
                r = e + 3;
                continue;
            } else if (starts(p, r, "<!--") || starts(p, r, "<?")) {
                if (skip_comment_or_pi(p, r, &r) != 0)
                    return fail(p, "unterminated comment or processing instruction");
                continue;
            }
            break;
        }
        b[w++] = c;
        r++;
    }
    p->pos = r;
    /* The NUL may land on the '<' that opens the next tag; restore it on the next call. */
    p->saved_pos = w;
    p->saved_char = b[w];
    b[w] = '\0';
    p->text = b + start;
    p->text_len = w - start;
    return XML_TEXT;
}

/* Decode an attribute value in place; p->pos is just past the opening quote. */
static int read_attr_value(xml_parser *p, char quote, char **value)
{
    char *b = p->buf;
    size_t r = p->pos, w = p->pos, start = p->pos;

    while (r < p->len && b[r] != quote) {
        if (b[r] == '&') {
            char out[4];
            size_t used = 0, k = xml_entity(b + r, p->len - r, out, &used);
            if (k) {
                memcpy(b + w, out, k);
                w += k;
                r += used;
                continue;
            }
        }
        b[w++] = b[r++];
    }
    if (r >= p->len)
        return -1;
    b[w] = '\0';
    p->pos = r + 1;
    *value = b + start;
    return 0;
}

static int add_ns(xml_parser *p, char *prefix, char *uri)
{
    if (p->nns >= XML_MAX_NS)
        return -1;
    p->ns[p->nns].prefix = prefix;
    p->ns[p->nns].uri = uri;
    p->ns[p->nns].depth = p->depth;
    p->nns++;
    return 0;
}

static size_t name_len(const char *s, size_t avail)
{
    size_t n = 0;
    while (n < avail && !is_ws(s[n]) && s[n] != '=' && s[n] != '>' && s[n] != '/')
        n++;
    return n;
}

/* One attribute at b[*r]; advances *r. Returns NULL on success or an error message. */
static const char *read_attr(xml_parser *p, size_t *r)
{
    char *b = p->buf;
    char *an, *av, delim, quote;
    size_t m = name_len(b + *r, p->len - *r);

    if (m == 0)
        return "bad attribute name";
    if (*r + m >= p->len)
        return "unexpected end of input in a tag";
    an = b + *r;
    delim = b[*r + m];
    b[*r + m] = '\0';
    *r += m + 1;
    if (is_ws(delim)) {
        while (*r < p->len && is_ws(b[*r]))
            (*r)++;
        if (*r >= p->len || b[*r] != '=')
            return "attribute without a value";
        (*r)++;
    } else if (delim != '=') {
        return "attribute without a value";
    }
    while (*r < p->len && is_ws(b[*r]))
        (*r)++;
    if (*r >= p->len || (b[*r] != '"' && b[*r] != '\''))
        return "attribute value is not quoted";
    quote = b[*r];
    p->pos = *r + 1;
    if (read_attr_value(p, quote, &av) != 0)
        return "unterminated attribute value";
    *r = p->pos;
    if (strcmp(an, "xmlns") == 0) {
        if (add_ns(p, "", av) != 0)
            return "too many namespace declarations";
    } else if (strncmp(an, "xmlns:", 6) == 0) {
        if (add_ns(p, an + 6, av) != 0)
            return "too many namespace declarations";
    }
    if (p->nattrs < XML_MAX_ATTRS) {
        p->attrs[p->nattrs].name = an;
        p->attrs[p->nattrs].value = av;
        p->nattrs++;
    }
    return NULL;
}

static xml_event read_start_tag(xml_parser *p)
{
    char *b = p->buf;
    size_t r = p->pos + 1, n;
    int empty = 0, closed = 0;
    unsigned char c0;
    char delim;

    if (p->depth >= XML_MAX_DEPTH)
        return fail(p, "elements nested too deeply");
    if (p->depth == 0 && p->seen_root)
        return fail(p, "content after the root element");
    n = name_len(b + r, p->len - r);
    c0 = (unsigned char)b[r];
    if (n == 0 || !((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z') || c0 == '_' ||
                    c0 == ':' || c0 >= 0x80))
        return fail(p, "bad element name");
    if (r + n >= p->len)
        return fail(p, "unexpected end of input in a tag");
    p->name = b + r;
    p->nattrs = 0;
    p->depth++;
    p->seen_root = 1;
    delim = b[r + n];
    b[r + n] = '\0';
    r += n + 1;
    if (delim == '>') {
        closed = 1;
    } else if (delim == '/') {
        if (r < p->len && b[r] == '>') {
            r++;
            closed = empty = 1;
        } else {
            return fail(p, "bad tag end");
        }
    } else if (delim == '=') {
        return fail(p, "bad element name");
    }
    while (!closed) {
        const char *err;
        while (r < p->len && is_ws(b[r]))
            r++;
        if (r >= p->len)
            return fail(p, "unexpected end of input in a tag");
        if (b[r] == '>') {
            r++;
            break;
        }
        if (b[r] == '/') {
            if (r + 1 < p->len && b[r + 1] == '>') {
                r += 2;
                empty = 1;
                break;
            }
            return fail(p, "bad tag end");
        }
        err = read_attr(p, &r);
        if (err)
            return fail(p, err);
    }
    p->pos = r;
    p->stack[p->depth - 1] = p->name;
    p->pending_end = empty;
    return XML_START;
}

static xml_event read_end_tag(xml_parser *p)
{
    char *b = p->buf;
    size_t r = p->pos + 2, n = name_len(b + r, p->len - r);
    char *name = b + r, delim;

    if (p->depth == 0)
        return fail(p, "unexpected end tag");
    if (r + n >= p->len)
        return fail(p, "unexpected end of input in a tag");
    delim = b[r + n];
    b[r + n] = '\0';
    r += n + 1;
    if (is_ws(delim)) {
        while (r < p->len && is_ws(b[r]))
            r++;
        if (r >= p->len)
            return fail(p, "unexpected end of input in a tag");
        if (b[r] != '>')
            return fail(p, "bad end tag");
        r++;
    } else if (delim != '>') {
        return fail(p, "bad end tag");
    }
    if (strcmp(name, p->stack[p->depth - 1]) != 0)
        return fail(p, "mismatched end tag");
    p->pos = r;
    p->name = p->stack[p->depth - 1];
    p->pop_pending = 1;
    return XML_END;
}

static void restore_saved(xml_parser *p)
{
    if (p->saved_pos != NO_SAVE) {
        p->buf[p->saved_pos] = p->saved_char;
        p->saved_pos = NO_SAVE;
    }
}

xml_event xml_next(xml_parser *p)
{
    if (p->failed)
        return XML_ERROR;
    restore_saved(p);
    if (p->pop_pending) {
        while (p->nns > 0 && p->ns[p->nns - 1].depth == p->depth)
            p->nns--;
        p->depth--;
        p->pop_pending = 0;
    }
    if (p->pending_end) {
        p->pending_end = 0;
        p->name = p->stack[p->depth - 1];
        p->pop_pending = 1;
        return XML_END;
    }
    for (;;) {
        restore_saved(p);
        if (p->pos >= p->len) {
            if (p->depth > 0)
                return fail(p, "unexpected end of input");
            if (!p->seen_root)
                return fail(p, "no root element");
            return XML_EOF;
        }
        if (p->buf[p->pos] != '<' || starts(p, p->pos, "<![CDATA[")) {
            if (read_text(p) == XML_ERROR)
                return XML_ERROR;
            if (p->depth == 0) {
                size_t i;
                for (i = 0; i < p->text_len; i++)
                    if (!is_ws(p->text[i]))
                        return fail(p, "text outside the root element");
                continue;
            }
            if (p->text_len == 0)
                continue;
            return XML_TEXT;
        }
        if (starts(p, p->pos, "<!--") || starts(p, p->pos, "<?")) {
            if (skip_comment_or_pi(p, p->pos, &p->pos) != 0)
                return fail(p, "unterminated comment or processing instruction");
        } else if (starts(p, p->pos, "<!")) {
            if (skip_doctype(p) != 0)
                return fail(p, "unterminated declaration");
        } else if (starts(p, p->pos, "</")) {
            return read_end_tag(p);
        } else {
            return read_start_tag(p);
        }
    }
}
