/* strutil — small allocation-free string and path helpers (pure C, no platform includes). */
#ifndef NOCTO_STRUTIL_H
#define NOCTO_STRUTIL_H

#include <stddef.h>

/* Copy src into dst (capacity cap, including the NUL). Always NUL-terminates when cap > 0.
 * Returns strlen(src); a return value >= cap means the copy was truncated. */
size_t str_copy(char *dst, size_t cap, const char *src);

/* Append src to the NUL-terminated string in dst (capacity cap). Same return convention as
 * strlcat: the length the full result would have had; >= cap means truncated.
 * If dst is not NUL-terminated within cap, dst is left alone and cap + strlen(src) is returned. */
size_t str_cat(char *dst, size_t cap, const char *src);

/* ASCII-only lowercase in place (bytes >= 0x80 are untouched, so UTF-8 survives). */
void str_lower(char *s);

/* ASCII case-insensitive compare. <0, 0, >0 like strcmp. */
int str_casecmp(const char *a, const char *b);

/* 1 when s ends with suffix, ASCII case-insensitive. An empty suffix always matches. */
int str_ends_with_ci(const char *s, const char *suffix);

/* Trim ASCII whitespace from both ends, in place. Returns a pointer into s (the first
 * non-space char); the trailing whitespace is overwritten with NULs. */
char *str_trim(char *s);

/* Join a and b into dst with exactly one separator. No '/' is added when a is empty or ends
 * in '/' or ':' (so "ux0:" + "data" -> "ux0:data"); leading '/' on b are dropped when a is
 * non-empty. Returns 0 on success, -1 when the result did not fit (dst is then truncated but
 * NUL-terminated). dst must not alias a or b. */
int path_join(char *dst, size_t cap, const char *a, const char *b);

/* Pointer to the final path component (after the last '/' or ':'). Trailing slashes are not
 * special: "a/b/" has an empty basename. Never NULL. */
const char *path_basename(const char *path);

/* Length of the directory part of path, without allocation: path_dirname_len(p) bytes of p
 * are the directory. "ux0:data/a.cbz" -> "ux0:data", "ux0:a.cbz" -> "ux0:", "/a" -> "/",
 * "a" -> "" (0). Trailing slashes after the directory are removed, except for a root "/". */
size_t path_dirname_len(const char *path);

/* Copy of the directory part into dst (see path_dirname_len). Returns 0 or -1 on truncation. */
int path_dirname(char *dst, size_t cap, const char *path);

/* Extension of the final path component, without the dot, pointing into path; "" when there
 * is none. A leading dot alone (".hidden") is not an extension. "a.tar.gz" -> "gz". */
const char *path_ext(const char *path);

#endif
