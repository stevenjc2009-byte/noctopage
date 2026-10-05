/* dlq_name - file names for downloads: the server's suggestion (Content-Disposition, else the
 * URL's last segment) and the queue's own "<title>.<ext>" names, made safe for FAT/exFAT and
 * unique. Pure C. No header: http.c and dlq.c declare what they use; tests/test_http.c and
 * tests/test_dlq.c test it. */
#include "core/strutil.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#define NAME_MAX_BYTES 256
#define TITLE_MAX_BYTES 120 /* leaves room for " (999).epub.part" within exFAT's 255 */

int dlq_name_disposition(const char *value, char *out, size_t cap);
int dlq_name_from_url(const char *url, char *out, size_t cap);
void dlq_name_safe(char *s);
void dlq_name_ext(const char *ext, const char *url, char *out, size_t cap);
int dlq_name_make(const char *dir, const char *title, const char *ext, const char *url,
                  int (*taken)(const char *path, void *ud), void *ud, char *out, size_t cap);

/* n bytes of s, cut back to a UTF-8 character boundary. */
static size_t utf8_cut(const char *s, size_t n)
{
	while (n > 0 && ((unsigned char)s[n] & 0xc0) == 0x80) n--;
	return n;
}

static int hexval(int c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/* Percent-decode n bytes of s into out (cap > 0); a decoded NUL becomes '_'. */
static void pct_decode(const char *s, size_t n, char *out, size_t cap)
{
	size_t o = 0;
	for (size_t i = 0; i < n && o + 1 < cap; i++) {
		int hi, lo;
		if (s[i] == '%' && i + 2 < n && (hi = hexval((unsigned char)s[i + 1])) >= 0 &&
		    (lo = hexval((unsigned char)s[i + 2])) >= 0) {
			int b = hi * 16 + lo;
			out[o++] = b ? (char)b : '_';
			i += 2;
		} else {
			out[o++] = s[i];
		}
	}
	out[o] = '\0';
}

void dlq_name_safe(char *s)
{
	size_t n, i = 0;
	for (char *p = s; *p; p++) {
		unsigned char c = (unsigned char)*p;
		if (c < 0x20 || c == 0x7f || strchr("<>:\"/\\|?*", c)) *p = '_';
	}
	while (s[i] == ' ' || s[i] == '.') i++; /* a leading dot hides the file */
	memmove(s, s + i, strlen(s + i) + 1);
	n = strlen(s);
	while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '.')) s[--n] = '\0'; /* FAT drops these */
}

/* Copy src (its last path component when strip_dirs) into out, made safe; cut on a UTF-8
 * boundary. Returns 1 when a name is left. */
static int finish_name(const char *src, int strip_dirs, char *out, size_t cap)
{
	const char *a = strrchr(src, '/'), *b = strrchr(src, '\\');
	if (b && (!a || b > a)) a = b;
	if (strip_dirs && a) src = a + 1;
	size_t n = strlen(src);
	if (n >= cap) n = utf8_cut(src, cap - 1);
	memcpy(out, src, n);
	out[n] = '\0';
	dlq_name_safe(out);
	return out[0] != '\0';
}

int dlq_name_disposition(const char *v, char *out, size_t cap)
{
	char plain[NAME_MAX_BYTES] = "", ext[NAME_MAX_BYTES] = "", val[NAME_MAX_BYTES];
	const char *p = v ? strchr(v, ';') : NULL;
	if (cap == 0) return 0;
	out[0] = '\0';
	while (p) {
		size_t nl, o = 0;
		p++;
		while (*p == ' ' || *p == '\t') p++;
		const char *name = p;
		nl = strcspn(p, "=;");
		if (p[nl] != '=') {
			p = strchr(p, ';');
			continue;
		}
		while (nl > 0 && name[nl - 1] == ' ') nl--;
		p += strcspn(p, "=") + 1;
		while (*p == ' ') p++;
		if (*p == '"') {
			for (p++; *p && *p != '"'; p++) {
				if (*p == '\\' && p[1]) p++;
				if (o + 1 < sizeof val) val[o++] = *p;
			}
			val[o] = '\0';
		} else {
			o = strcspn(p, ";");
			while (o > 0 && p[o - 1] == ' ') o--;
			if (o >= sizeof val) o = sizeof val - 1;
			memcpy(val, p, o);
			val[o] = '\0';
		}
		if (nl == 9 && strncasecmp(name, "filename*", 9) == 0) {
			/* RFC 5987: charset'language'pct-encoded; only UTF-8 is taken */
			const char *q1 = strchr(val, '\''), *q2 = q1 ? strchr(q1 + 1, '\'') : NULL;
			if (q2 && q1 - val == 5 && strncasecmp(val, "utf-8", 5) == 0)
				pct_decode(q2 + 1, strlen(q2 + 1), ext, sizeof ext);
		} else if (nl == 8 && strncasecmp(name, "filename", 8) == 0) {
			str_copy(plain, sizeof plain, val);
		}
		p = strchr(p, ';');
	}
	return finish_name(ext[0] ? ext : plain, 1, out, cap);
}

int dlq_name_from_url(const char *url, char *out, size_t cap)
{
	char seg[NAME_MAX_BYTES];
	const char *p = strstr(url, "://");
	if (cap == 0) return 0;
	out[0] = '\0';
	p = p ? p + 3 : url;
	p += strcspn(p, "/?#"); /* skip the host */
	size_t n = strcspn(p, "?#");
	const char *s = p + n;
	while (s > p && s[-1] != '/') s--;
	pct_decode(s, (size_t)(p + n - s), seg, sizeof seg);
	return finish_name(seg, 0, out, cap); /* a %2F is part of the segment */
}

static int ext_ok(const char *e)
{
	size_t n = strlen(e);
	if (n == 0 || n > 8) return 0;
	for (; *e; e++)
		if (!((*e >= 'a' && *e <= 'z') || (*e >= 'A' && *e <= 'Z') || (*e >= '0' && *e <= '9'))) return 0;
	return 1;
}

void dlq_name_ext(const char *ext, const char *url, char *out, size_t cap)
{
	char seg[NAME_MAX_BYTES];
	if (ext && *ext == '.') ext++;
	if (ext && ext_ok(ext)) str_copy(out, cap, ext);
	else if (url && dlq_name_from_url(url, seg, sizeof seg) && ext_ok(path_ext(seg)))
		str_copy(out, cap, path_ext(seg));
	else str_copy(out, cap, "bin");
	str_lower(out);
}

int dlq_name_make(const char *dir, const char *title, const char *ext, const char *url,
                  int (*taken)(const char *path, void *ud), void *ud, char *out, size_t cap)
{
	char base[TITLE_MAX_BYTES + 1], e[16], name[TITLE_MAX_BYTES + 32], path[512];
	size_t n = title ? strlen(title) : 0;
	if (n > TITLE_MAX_BYTES) n = utf8_cut(title, TITLE_MAX_BYTES);
	memcpy(base, title ? title : "", n);
	base[n] = '\0';
	dlq_name_safe(base);
	if (!base[0]) str_copy(base, sizeof base, "Download");
	dlq_name_ext(ext, url, e, sizeof e);
	for (int i = 1; i < 1000; i++) {
		if (i == 1) snprintf(name, sizeof name, "%s.%s", base, e);
		else snprintf(name, sizeof name, "%s (%d).%s", base, i, e);
		if (path_join(path, sizeof path, dir, name) != 0) return -1;
		if (!taken(path, ud)) return str_copy(out, cap, path) < cap ? 0 : -1;
	}
	return -1;
}
