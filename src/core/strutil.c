#include "core/strutil.h"

#include <string.h>

static int lower_ascii(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static int is_space_ascii(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

size_t str_copy(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (cap > 0) {
        size_t k = n < cap - 1 ? n : cap - 1;
        memcpy(dst, src, k);
        dst[k] = '\0';
    }
    return n;
}

size_t str_cat(char *dst, size_t cap, const char *src)
{
    size_t dlen = 0;
    while (dlen < cap && dst[dlen] != '\0')
        dlen++;
    if (dlen == cap)
        return cap + strlen(src);
    return dlen + str_copy(dst + dlen, cap - dlen, src);
}

void str_lower(char *s)
{
    for (; *s; s++)
        *s = (char)lower_ascii((unsigned char)*s);
}

int str_casecmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int ca = lower_ascii((unsigned char)*a);
        int cb = lower_ascii((unsigned char)*b);
        if (ca != cb)
            return ca - cb;
        if (ca == 0)
            return 0;
    }
}

int str_ends_with_ci(const char *s, const char *suffix)
{
    size_t sl = strlen(s), xl = strlen(suffix);
    if (xl > sl)
        return 0;
    return str_casecmp(s + (sl - xl), suffix) == 0;
}

char *str_trim(char *s)
{
    size_t n;
    while (*s && is_space_ascii(*s))
        s++;
    n = strlen(s);
    while (n > 0 && is_space_ascii(s[n - 1]))
        s[--n] = '\0';
    return s;
}

int path_join(char *dst, size_t cap, const char *a, const char *b)
{
    size_t alen = strlen(a);
    size_t total;
    int need_sep = alen > 0 && a[alen - 1] != '/' && a[alen - 1] != ':';

    if (alen > 0) {
        while (*b == '/')
            b++;
    }
    total = str_copy(dst, cap, a);
    if (need_sep && b[0] != '\0')
        total = str_cat(dst, cap, "/");
    total = str_cat(dst, cap, b);
    return total < cap ? 0 : -1;
}

const char *path_basename(const char *path)
{
    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == ':')
            base = p + 1;
    return base;
}

size_t path_dirname_len(const char *path)
{
    const char *base = path_basename(path);
    size_t len = (size_t)(base - path);

    /* Drop the separator before the basename unless that leaves nothing meaningful:
     * keep "ux0:" (colon) and a root "/" intact. */
    while (len > 1 && path[len - 1] == '/')
        len--;
    return len;
}

int path_dirname(char *dst, size_t cap, const char *path)
{
    size_t len = path_dirname_len(path);
    if (cap == 0)
        return -1;
    if (len >= cap) {
        memcpy(dst, path, cap - 1);
        dst[cap - 1] = '\0';
        return -1;
    }
    memcpy(dst, path, len);
    dst[len] = '\0';
    return 0;
}

const char *path_ext(const char *path)
{
    const char *base = path_basename(path);
    const char *dot = strrchr(base, '.');
    if (dot == NULL || dot == base)
        return base + strlen(base);
    return dot + 1;
}

int str_page_number(const char *s,int max,int *out)
{
 while(*s==' ' || *s=='\t') s++;
 if(*s<'0' || *s>'9' || max<1) return 0;
 int n=0; while(*s>='0' && *s<='9') { int d=*s++-'0'; if(n>(max-d)/10 || d>max) return 0; n=n*10+d; }
 while(*s==' ' || *s=='\t') s++;
 if(*s || n<1 || n>max) return 0;
 *out=n; return 1;
}
