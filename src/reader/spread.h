/* spread — double-page spread layout, Komga style (pure C).
 *
 * Pairing rule (greedy, in reading order):
 *   - A page is "wide" when w > h (a square page is NOT wide). A wide page is always alone.
 *   - offset != 0: page 0 (the cover) is alone, whatever its shape; pairing starts at page 1.
 *   - From the current page i, if i is portrait and i+1 exists and is portrait, they form a
 *     pair and pairing continues at i+2. Otherwise i is alone (its neighbour is wide or it is
 *     the last page) and pairing continues at i+1.
 *   So a wide page re-syncs the pairing: portrait pages after it pair up again starting at
 *   the page right after it, and an odd portrait page before a wide page stays alone.
 *
 * Spreads cover every page exactly once, in order: spread k holds pages first .. first+count-1.
 * Reading direction only affects where the two pages of a pair are drawn.
 */
#ifndef NOCTO_SPREAD_H
#define NOCTO_SPREAD_H

typedef struct {
	int first; /* first page in reading order */
	int count; /* 1 or 2 */
} spread_t;

/* Build the spreads of n pages (w[i] x h[i]). Writes at most max entries to out (out may be
 * NULL to just count) and returns the total number of spreads, which can exceed max. */
int spread_build(const int *w, const int *h, int n, int offset, spread_t *out, int max);

/* Index of the spread holding page, or -1 if page is outside 0..n-1 of the built spreads.
 * Binary search; ns is the count returned by spread_build (must be <= the array size). */
int spread_of_page(const spread_t *s, int ns, int page);

/* Pages of spread i in left-to-right screen order into out[0..count-1]; returns count (1 or
 * 2), or 0 when i is out of range. LTR: out = {first, first+1}. RTL: the first page of a pair
 * is on the right, so out = {first+1, first}. A lone page is out[0]. */
int spread_pages(const spread_t *s, int ns, int i, int rtl, int out[2]);

#endif
