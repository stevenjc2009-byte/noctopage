/* crop.c - see crop.h. */
#include "reader/crop.h"

#include <stddef.h>

static int ink(const uint8_t *p) { return p[0] < CROP_WHITE || p[1] < CROP_WHITE || p[2] < CROP_WHITE; }

static int limit(int samples) { return samples / 100 > 1 ? samples / 100 : 1; }

static int row_is_border(const uint8_t *px, int w)
{
	int n = 0, max = limit(w);
	for (int x = 0; x < w; x++)
		if (ink(px + (size_t)x * 4) && ++n > max) return 0;
	return 1;
}

static int col_is_border(const uint8_t *(*row)(const void *, int), const void *ud, int x, int y0,
                         int y1)
{
	int n = 0, max = limit((y1 - y0 + 1) / 2);
	for (int y = y0; y < y1; y += 2)
		if (ink(row(ud, y) + (size_t)x * 4) && ++n > max) return 0;
	return 1;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

int crop_detect(const uint8_t *(*row)(const void *ud, int y), const void *ud, int w, int h,
                crop_t *out)
{
	*out = (crop_t){0.0f, 0.0f, 1.0f, 1.0f};
	if (w <= 0 || h <= 0) return 0;
	int top = 0, bottom = h; /* content rows top .. bottom-1 */
	while (top < h && row_is_border(row(ud, top), w)) top++;
	if (top == h) return 0; /* blank page */
	while (bottom > top + 1 && row_is_border(row(ud, bottom - 1), w)) bottom--;
	int left = 0, right = w;
	while (left < w - 1 && col_is_border(row, ud, left, top, bottom)) left++;
	while (right > left + 1 && col_is_border(row, ud, right - 1, top, bottom)) right--;
	if (top == 0 && left == 0 && bottom == h && right == w) return 0; /* no border */

	crop_t c;
	c.l = (float)left / (float)w - (left ? CROP_PAD : 0.0f);
	c.t = (float)top / (float)h - (top ? CROP_PAD : 0.0f);
	c.r = (float)right / (float)w + (right < w ? CROP_PAD : 0.0f);
	c.b = (float)bottom / (float)h + (bottom < h ? CROP_PAD : 0.0f);
	c.l = clampf(c.l, 0.0f, 1.0f);
	c.t = clampf(c.t, 0.0f, 1.0f);
	c.r = clampf(c.r, 0.0f, 1.0f);
	c.b = clampf(c.b, 0.0f, 1.0f);
	if (c.r - c.l < CROP_MIN_KEEP || c.b - c.t < CROP_MIN_KEEP) return 0;
	*out = c;
	return 1;
}
