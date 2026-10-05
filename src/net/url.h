/* url - RFC 3986 reference resolution, percent-encoding and URL templates (pure C). */
#ifndef NOCTO_URL_H
#define NOCTO_URL_H

#include <stddef.h>

/* Longest URL (including the NUL) url_resolve() will build; longer results fail. */
#define URL_MAX 2048

/* 1 when s starts with a URI scheme ("http:", "data:", "urn:"...). */
int url_has_scheme(const char *s);

/* Resolve ref against base (RFC 3986 section 5.2, strict). base may be NULL or "" (then ref is
 * copied; dot segments of an absolute ref are still removed). Bytes are never re-encoded, so
 * template braces survive. Returns 0, or -1 when the result does not fit in cap or URL_MAX. */
int url_resolve(char *dst, size_t cap, const char *base, const char *ref);

/* Percent-encode src as a query component: unreserved characters (A-Z a-z 0-9 - . _ ~) are
 * kept, every other byte becomes %XX (space -> %20). Returns the length written (NUL-terminated),
 * or -1 when it does not fit. */
int url_encode_component(char *dst, size_t cap, const char *src);

typedef struct {
    const char *name;
    const char *value; /* raw (unencoded); NULL = not set */
} url_var;

/* Expand a URL template. Supported expressions:
 *   {name}      the percent-encoded value, or nothing when the variable is unset
 *   {name?}     the same (OpenSearch "optional" form, e.g. {startPage?})
 *   {?a,b,c}    "?a=v&b=v&c=v" for the variables that are set ("&" instead of "?" for {&a,b})
 * Unknown variables expand to nothing. Text outside braces is copied unchanged.
 * Returns 0, or -1 when the result does not fit in cap. */
int url_expand(char *dst, size_t cap, const char *tmpl, const url_var *vars, size_t nvars);

/* {searchTerms} / {query} <- terms. */
int url_expand_search(char *dst, size_t cap, const char *tmpl, const char *terms);

/* {pageNumber} <- page, {maxWidth} <- max_width (left unset when max_width <= 0). */
int url_expand_page(char *dst, size_t cap, const char *tmpl, int page, int max_width);

#endif
