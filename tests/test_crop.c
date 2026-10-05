#include "test.h"

#include "reader/crop.h"

#include <stdlib.h>
#include <string.h>

#define W 200
#define H 300

static uint8_t g_px[H][W][4];

static const uint8_t *row(const void *ud, int y)
{
	(void)ud;
	return &g_px[y][0][0];
}

static void fill(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b)
{
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++) {
			g_px[y][x][0] = r;
			g_px[y][x][1] = g;
			g_px[y][x][2] = b;
			g_px[y][x][3] = 255;
		}
}

static void paper(void) { fill(0, 0, W, H, 255, 255, 255); }

static int near(float got, float want) { return got > want - 0.002f && got < want + 0.002f; }

static void check_box(const crop_t *c, float l, float t, float r, float b)
{
	CHECK(near(c->l, l));
	CHECK(near(c->t, t));
	CHECK(near(c->r, r));
	CHECK(near(c->b, b));
	if (!near(c->l, l) || !near(c->t, t) || !near(c->r, r) || !near(c->b, b))
		printf("  box %.4f %.4f %.4f %.4f, wanted %.4f %.4f %.4f %.4f\n", c->l, c->t, c->r, c->b, l,
		       t, r, b);
}

int main(void)
{
	crop_t c;

	/* Art at x 40..159, y 60..239 on white: cropped to it, plus 1% padding. */
	paper();
	fill(40, 60, 160, 240, 0, 0, 0);
	CHECK_INT(crop_detect(row, NULL, W, H, &c), 1);
	check_box(&c, 0.2f - CROP_PAD, 0.2f - CROP_PAD, 0.8f + CROP_PAD, 0.8f + CROP_PAD);

	/* A 2x2 speck in the corner does not stop the crop. */
	fill(0, 0, 2, 2, 0, 0, 0);
	CHECK_INT(crop_detect(row, NULL, W, H, &c), 1);
	check_box(&c, 0.2f - CROP_PAD, 0.2f - CROP_PAD, 0.8f + CROP_PAD, 0.8f + CROP_PAD);

	/* ...but a 3 px wide line on a 200 px row (1.5%) does. */
	paper();
	fill(40, 60, 160, 240, 0, 0, 0);
	fill(100, 10, 103, 11, 0, 0, 0);
	CHECK_INT(crop_detect(row, NULL, W, H, &c), 1);
	check_box(&c, 0.2f - CROP_PAD, 10.0f / H - CROP_PAD, 0.8f + CROP_PAD, 0.8f + CROP_PAD);

	/* Off-white paper (230) is still paper; a pure red pixel is art. */
	fill(0, 0, W, H, 230, 232, 240);
	fill(40, 60, 160, 240, 255, 0, 0);
	CHECK_INT(crop_detect(row, NULL, W, H, &c), 1);
	check_box(&c, 0.2f - CROP_PAD, 0.2f - CROP_PAD, 0.8f + CROP_PAD, 0.8f + CROP_PAD);

	/* Art touching the left and top edges: those sides stay at 0. */
	paper();
	fill(0, 0, 150, 200, 10, 10, 10);
	CHECK_INT(crop_detect(row, NULL, W, H, &c), 1);
	check_box(&c, 0.0f, 0.0f, 0.75f + CROP_PAD, 200.0f / H + CROP_PAD);

	/* A blank page, a page with no border, and a lone small panel are shown whole. */
	paper();
	CHECK_INT(crop_detect(row, NULL, W, H, &c), 0);
	check_box(&c, 0, 0, 1, 1);
	fill(0, 0, W, H, 0, 0, 0);
	CHECK_INT(crop_detect(row, NULL, W, H, &c), 0);
	check_box(&c, 0, 0, 1, 1);
	paper();
	fill(90, 130, 110, 170, 0, 0, 0);
	CHECK_INT(crop_detect(row, NULL, W, H, &c), 0);
	check_box(&c, 0, 0, 1, 1);

	/* Just above the keep limit on both axes is still cropped (width 0.42, height 0.42). */
	paper();
	fill(58, 87, 142, 213, 0, 0, 0);
	CHECK_INT(crop_detect(row, NULL, W, H, &c), 1);
	CHECK(c.r - c.l >= CROP_MIN_KEEP && c.b - c.t >= CROP_MIN_KEEP);

	/* Degenerate sizes. */
	CHECK_INT(crop_detect(row, NULL, 0, H, &c), 0);
	CHECK_INT(crop_detect(row, NULL, W, 0, &c), 0);
	check_box(&c, 0, 0, 1, 1);

	return TEST_DONE("crop");
}
