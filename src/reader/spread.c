#include "reader/spread.h"

static int is_wide(const int *w, const int *h, int i)
{
	return w[i] > h[i];
}

int spread_build(const int *w, const int *h, int n, int offset, spread_t *out, int max)
{
	int i = 0, ns = 0;

	if (n <= 0 || !w || !h)
		return 0;
	while (i < n) {
		int count = 1;
		if (!(offset && i == 0) && !is_wide(w, h, i) && i + 1 < n && !is_wide(w, h, i + 1))
			count = 2;
		if (out && ns < max) {
			out[ns].first = i;
			out[ns].count = count;
		}
		ns++;
		i += count;
	}
	return ns;
}

int spread_of_page(const spread_t *s, int ns, int page)
{
	int lo = 0, hi = ns - 1;

	if (!s || page < 0)
		return -1;
	while (lo <= hi) {
		int mid = lo + (hi - lo) / 2;
		if (page < s[mid].first)
			hi = mid - 1;
		else if (page >= s[mid].first + s[mid].count)
			lo = mid + 1;
		else
			return mid;
	}
	return -1;
}

int spread_pages(const spread_t *s, int ns, int i, int rtl, int out[2])
{
	if (!s || i < 0 || i >= ns)
		return 0;
	if (s[i].count == 1) {
		out[0] = s[i].first;
		return 1;
	}
	out[0] = rtl ? s[i].first + 1 : s[i].first;
	out[1] = rtl ? s[i].first : s[i].first + 1;
	return 2;
}
