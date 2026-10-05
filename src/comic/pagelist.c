#include "comic/pagelist.h"

#include "core/natsort.h"
#include "lib/formats.h"

#include <stddef.h>
#include <string.h>

/* ASCII case-insensitive compare of the first len bytes of p against the lower-case word w. */
static int component_is(const char *p, size_t len, const char *w)
{
	size_t i;

	if (strlen(w) != len)
		return 0;
	for (i = 0; i < len; i++) {
		char ch = p[i];
		if (ch >= 'A' && ch <= 'Z')
			ch = (char)(ch + ('a' - 'A'));
		if (ch != w[i])
			return 0;
	}
	return 1;
}

/* 1 when every path component is acceptable for a reading page. */
static int path_is_clean(const char *p)
{
	for (;;) {
		const char *end = strchr(p, '/');
		size_t len = end ? (size_t)(end - p) : strlen(p);

		if (len > 0 && p[0] == '.' && len != 1)
			return 0; /* hidden, "..", AppleDouble "._x"; a lone "." is just "./" */
		if (component_is(p, len, "__macosx"))
			return 0;
		if (!end)
			return 1;
		p = end + 1;
	}
}

/* Total order: natural path order, input index breaks ties between identical names. */
static int less(const char *const *names, int a, int b)
{
	int c = natcmp(names[a], names[b]);
	return c < 0 || (c == 0 && a < b);
}

static void sift_down(const char *const *names, int *v, int root, int n)
{
	for (;;) {
		int child = 2 * root + 1, big = root, t;

		if (child < n && less(names, v[big], v[child]))
			big = child;
		if (child + 1 < n && less(names, v[big], v[child + 1]))
			big = child + 1;
		if (big == root)
			return;
		t = v[root];
		v[root] = v[big];
		v[big] = t;
		root = big;
	}
}

int pagelist_build(const char *const *names, int n, int *out_idx)
{
	int i, count = 0;

	if (!names || !out_idx || n <= 0)
		return 0;
	for (i = 0; i < n; i++)
		if (formats_is_image(names[i]) && path_is_clean(names[i])) /* NULL is not an image */
			out_idx[count++] = i;

	/* In-place heapsort: no allocation, no recursion, and no qsort context needed. */
	for (i = count / 2 - 1; i >= 0; i--)
		sift_down(names, out_idx, i, count);
	for (i = count - 1; i > 0; i--) {
		int t = out_idx[0];
		out_idx[0] = out_idx[i];
		out_idx[i] = t;
		sift_down(names, out_idx, 0, i);
	}
	return count;
}
