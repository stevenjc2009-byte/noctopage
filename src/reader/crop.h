/* crop - finds the blank paper border around a decoded page, for "crop white borders"
 * (pure C, no allocation).
 *
 * A pixel is paper when every colour channel is >= CROP_WHITE. Scanning in from each edge, a
 * row (or column) is part of the border while at most 1% of its samples (and at least 1) are
 * not paper, so scanner specks and stray dots do not stop the crop. Rows are read whole;
 * columns sample every second row. The found box is widened by CROP_PAD of the page on each
 * side so the art never touches the screen edge.
 *
 * The crop is refused (box = whole page, return 0) when the page is blank, when nothing would
 * be removed, or when the box would keep less than CROP_MIN_KEEP of the width or the height:
 * a page that is mostly white (a lone small panel, a title page) is shown whole. */
#ifndef NOCTO_CROP_H
#define NOCTO_CROP_H

#include <stdint.h>

#define CROP_WHITE 225
#define CROP_PAD 0.01f
#define CROP_MIN_KEEP 0.4f

typedef struct {
	float l, t, r, b; /* fractions of the page width / height, 0 <= l < r <= 1, 0 <= t < b <= 1 */
} crop_t;

/* row(ud, y) returns row y of the page: w tightly packed RGBA8888 pixels.
 * Returns 1 and the box when there is a border to crop, else 0 and {0, 0, 1, 1}. */
int crop_detect(const uint8_t *(*row)(const void *ud, int y), const void *ud, int w, int h,
                crop_t *out);

#endif
