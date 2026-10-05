/* opds_text - plain-text extraction for catalog summaries (HTML strip, whitespace collapse). */
#include <string.h>
#include <ctype.h>

#include "opds_priv.h"
#include "xml.h"

void opds_tb_init(opds_tb *t, char *out, size_t cap)
{
    t->out = out;
    t->cap = cap;
    t->len = 0;
    t->sep = 0;
    t->full = cap == 0;
    if (cap > 0) out[0] = '\0';
}

static void emit(opds_tb *t, const char *s, size_t n)
{
    size_t need = n + ((t->len > 0 && t->sep) ? 1 : 0);
    if (t->full) return;
    if (t->len + need > t->cap - 1) {
        t->full = 1;
        return;
    }
    if (t->len > 0 && t->sep) t->out[t->len++] = t->sep == 2 ? '\n' : ' ';
    t->sep = 0;
    memcpy(t->out + t->len, s, n);
    t->len += n;
}

/* Length of the valid UTF-8 sequence at s (n bytes left), or 0 when invalid. */
static size_t utf8_len(const unsigned char *s, size_t n)
{
    unsigned char c = s[0];
    size_t l, k;
    if (c >= 0xC2 && c <= 0xDF) l = 2;
    else if (c >= 0xE0 && c <= 0xEF) l = 3;
    else if (c >= 0xF0 && c <= 0xF4) l = 4;
    else return 0;
    if (l > n) return 0;
    for (k = 1; k < l; k++)
        if ((s[k] & 0xC0) != 0x80) return 0;
    if (c == 0xE0 && s[1] < 0xA0) return 0;
    if (c == 0xED && s[1] >= 0xA0) return 0;
    if (c == 0xF0 && s[1] < 0x90) return 0;
    if (c == 0xF4 && s[1] >= 0x90) return 0;
    return l;
}

void opds_tb_text(opds_tb *t, const char *str, size_t n)
{
    const unsigned char *s = (const unsigned char *)str;
    size_t i = 0;
    while (i < n && !t->full) {
        if (s[i] < 0x80) {
            if (s[i] <= ' ' || s[i] == 0x7F) {
                if (t->sep < 1) t->sep = 1;
            } else {
                emit(t, str + i, 1);
            }
            i++;
        } else {
            size_t l = utf8_len(s + i, n - i);
            if (l == 0) {
                i++;
            } else {
                if (l == 2 && s[i] == 0xC2 && s[i + 1] == 0xA0) { /* NBSP */
                    if (t->sep < 1) t->sep = 1;
                } else {
                    emit(t, str + i, l);
                }
                i += l;
            }
        }
    }
}

void opds_tb_block(opds_tb *t)
{
    t->sep = 2;
}

size_t opds_tb_finish(opds_tb *t)
{
    if (t->cap > 0) t->out[t->len] = '\0';
    return t->len;
}

static const struct { const char *name; unsigned long cp; } NAMED[] = {
    { "nbsp", 0xA0 },   { "mdash", 0x2014 }, { "ndash", 0x2013 }, { "hellip", 0x2026 },
    { "lsquo", 0x2018 }, { "rsquo", 0x2019 }, { "ldquo", 0x201C }, { "rdquo", 0x201D },
    { "copy", 0xA9 },   { "reg", 0xAE },     { "trade", 0x2122 }, { "laquo", 0xAB },
    { "raquo", 0xBB },  { "middot", 0xB7 },  { "bull", 0x2022 },  { "eacute", 0xE9 },
    { "egrave", 0xE8 }, { "agrave", 0xE0 },  { "uuml", 0xFC },    { "ouml", 0xF6 },
    { "auml", 0xE4 },   { "szlig", 0xDF },   { "ccedil", 0xE7 },  { "ntilde", 0xF1 },
};

/* s at '&': decode one reference into out (4 bytes). Returns bytes written, 0 if not a reference. */
static size_t decode_entity(const char *s, char out[4], size_t *consumed)
{
    size_t avail = 0, n, i;
    char name[10];
    while (avail < 16 && s[avail]) avail++;
    n = xml_entity(s, avail, out, consumed);
    if (n) return n;
    for (i = 1; i < avail && i < sizeof name && isalnum((unsigned char)s[i]); i++) name[i - 1] = s[i];
    if (i >= avail || s[i] != ';') return 0;
    name[i - 1] = '\0';
    for (n = 0; n < sizeof NAMED / sizeof NAMED[0]; n++) {
        if (strcmp(name, NAMED[n].name) == 0) {
            *consumed = i + 1;
            return xml_utf8_encode(NAMED[n].cp, out);
        }
    }
    return 0;
}

int opds_is_block_tag(const char *name, size_t n)
{
    static const char *const BLOCK[] = { "p", "div", "br", "li", "tr", "ul", "ol", "blockquote",
                                         "h1", "h2", "h3", "h4", "h5", "h6", "hr", "table" };
    size_t i;
    char low[12];
    if (n == 0 || n >= sizeof low) return 0;
    for (i = 0; i < n; i++) low[i] = (char)tolower((unsigned char)name[i]);
    low[n] = '\0';
    for (i = 0; i < sizeof BLOCK / sizeof BLOCK[0]; i++)
        if (strcmp(low, BLOCK[i]) == 0) return 1;
    return 0;
}

static int tag_start(const char *h)
{
    return h[0] == '<' && (isalpha((unsigned char)h[1]) || h[1] == '/' || h[1] == '!' || h[1] == '?');
}

/* html at '<' that starts a tag/comment: returns the index just after it, or 0 when unterminated. */
static size_t skip_tag(const char *h)
{
    size_t i;
    char quote = 0;
    if (strncmp(h, "<!--", 4) == 0) {
        const char *e = strstr(h + 4, "-->");
        return e ? (size_t)(e + 3 - h) : 0;
    }
    for (i = 1; h[i]; i++) {
        if (quote) {
            if (h[i] == quote) quote = 0;
        } else if (h[i] == '"' || h[i] == '\'') {
            quote = h[i];
        } else if (h[i] == '>') {
            return i + 1;
        }
    }
    return 0;
}

void opds_tb_html(opds_tb *t, const char *h)
{
    size_t i = 0;
    while (h[i] && !t->full) {
        if (tag_start(h + i)) {
            size_t e = skip_tag(h + i), k = i + 1;
            if (e == 0) break; /* unterminated tag: drop the rest */
            if (h[k] == '/') k++;
            {
                size_t s = k;
                while (isalnum((unsigned char)h[k])) k++;
                if (opds_is_block_tag(h + s, k - s)) opds_tb_block(t);
            }
            i += e;
        } else if (h[i] == '&') {
            char out[4];
            size_t used = 0, n = decode_entity(h + i, out, &used);
            if (n) {
                opds_tb_text(t, out, n);
                i += used;
            } else {
                opds_tb_text(t, "&", 1);
                i++;
            }
        } else {
            size_t s = i++; /* plain run; a '<' that opens no tag stays literal */
            while (h[i] && h[i] != '&' && !tag_start(h + i)) i++;
            opds_tb_text(t, h + s, i - s);
        }
    }
}

size_t opds_strip_html(const char *html, char *out, size_t cap)
{
    opds_tb t;
    opds_tb_init(&t, out, cap);
    opds_tb_html(&t, html ? html : "");
    return opds_tb_finish(&t);
}
