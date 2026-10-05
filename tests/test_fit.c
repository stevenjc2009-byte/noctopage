#include "test.h"

#include "reader/fit.h"

#include <math.h>

#define SW 960
#define SH 544

#define CHECK_NEAR(got, want)                                                          \
	do {                                                                               \
		double g__ = (got), w__ = (want);                                              \
		g_checks++;                                                                    \
		if (fabs(g__ - w__) > 1e-3) {                                                  \
			g_failed++;                                                                \
			printf("FAIL %s:%d: %s == %.4f, wanted %.4f\n", __FILE__, __LINE__, #got,  \
			       g__, w__);                                                          \
		}                                                                              \
	} while (0)

static void check_rect(const fit_view_t *v, float pw, float ph, float x, float y, float w,
                       float h)
{
	fit_rect_t r;
	fit_place(v, pw, ph, &r);
	CHECK_NEAR(r.x, x);
	CHECK_NEAR(r.y, y);
	CHECK_NEAR(r.w, w);
	CHECK_NEAR(r.h, h);
}

static fit_view_t view(fit_mode_t mode, float zoom)
{
	fit_view_t v;
	fit_view_init(&v, SW, SH);
	v.mode = mode;
	v.zoom = zoom;
	return v;
}

static void test_base_scales(void)
{
	/* portrait 480x1088: ratio 2.27 */
	CHECK_NEAR(fit_base_scale(FIT_WIDTH, 480, 1088, SW, SH), 2.0);
	CHECK_NEAR(fit_base_scale(FIT_HEIGHT, 480, 1088, SW, SH), 0.5);
	CHECK_NEAR(fit_base_scale(FIT_SMART, 480, 1088, SW, SH), 2.0); /* tall: width */
	CHECK_NEAR(fit_base_scale(FIT_ORIGINAL, 480, 1088, SW, SH), 1.0);
	/* portrait 544x1088: exactly 2.0 still fits whole; 543x1088 is "much taller" */
	CHECK_NEAR(fit_base_scale(FIT_SMART, 544, 1088, SW, SH), 0.5);
	CHECK_NEAR(fit_base_scale(FIT_SMART, 1000, 2000, SW, SH), 0.272);
	CHECK_NEAR(fit_base_scale(FIT_SMART, 1000, 2001, SW, SH), 0.96);
	CHECK_NEAR(fit_base_scale(FIT_SMART, 543, 1088, SW, SH), 960.0 / 543.0);
	/* normal portrait 800x1200 */
	CHECK_NEAR(fit_base_scale(FIT_WIDTH, 800, 1200, SW, SH), 1.2);
	CHECK_NEAR(fit_base_scale(FIT_HEIGHT, 800, 1200, SW, SH), 544.0 / 1200.0);
	CHECK_NEAR(fit_base_scale(FIT_SMART, 800, 1200, SW, SH), 544.0 / 1200.0);
	/* landscape 1920x1000: width-limited whole fit */
	CHECK_NEAR(fit_base_scale(FIT_WIDTH, 1920, 1000, SW, SH), 0.5);
	CHECK_NEAR(fit_base_scale(FIT_HEIGHT, 1920, 1000, SW, SH), 0.544);
	CHECK_NEAR(fit_base_scale(FIT_SMART, 1920, 1000, SW, SH), 0.5);
	/* landscape 1920x1360: height-limited whole fit */
	CHECK_NEAR(fit_base_scale(FIT_SMART, 1920, 1360, SW, SH), 0.4);
	/* extreme tall 800x9000 */
	CHECK_NEAR(fit_base_scale(FIT_WIDTH, 800, 9000, SW, SH), 1.2);
	CHECK_NEAR(fit_base_scale(FIT_HEIGHT, 800, 9000, SW, SH), 544.0 / 9000.0);
	CHECK_NEAR(fit_base_scale(FIT_SMART, 800, 9000, SW, SH), 1.2);
	CHECK_NEAR(fit_base_scale(FIT_ORIGINAL, 800, 9000, SW, SH), 1.0);
	/* degenerate page */
	CHECK_NEAR(fit_base_scale(FIT_WIDTH, 0, 100, SW, SH), 1.0);
	/* configurable screen */
	CHECK_NEAR(fit_base_scale(FIT_WIDTH, 480, 1088, 480, 272), 1.0);
	CHECK_NEAR(fit_base_scale(FIT_HEIGHT, 480, 1088, 480, 272), 0.25);
}

static void test_placement(void)
{
	fit_view_t v;

	/* every mode, 480x1088 portrait, zoom 1, centred */
	v = view(FIT_WIDTH, 1);
	check_rect(&v, 480, 1088, 0, (SH - 2176) / 2.0f, 960, 2176);
	v = view(FIT_HEIGHT, 1);
	check_rect(&v, 480, 1088, 360, 0, 240, 544);
	v = view(FIT_SMART, 1);
	check_rect(&v, 480, 1088, 0, (SH - 2176) / 2.0f, 960, 2176);
	v = view(FIT_ORIGINAL, 1);
	check_rect(&v, 480, 1088, 240, -272, 480, 1088);
	/* 544x1088: whole page */
	v = view(FIT_SMART, 1);
	check_rect(&v, 544, 1088, 344, 0, 272, 544);
	/* landscape 1920x1000: whole fit, letterboxed top/bottom */
	check_rect(&v, 1920, 1000, 0, 22, 960, 500);
	v = view(FIT_ORIGINAL, 1);
	check_rect(&v, 1920, 1000, -480, -228, 1920, 1000);
	/* extreme tall 800x9000, original size: centred, 4228 px above the screen top */
	check_rect(&v, 800, 9000, 80, -4228, 800, 9000);
	v = view(FIT_SMART, 1);
	check_rect(&v, 800, 9000, 0, (SH - 10800) / 2.0f, 960, 10800);
	v = view(FIT_HEIGHT, 1);
	{
		fit_rect_t r;
		fit_place(&v, 800, 9000, &r);
		CHECK_NEAR(r.h, 544);
		CHECK_NEAR(r.w, 800 * 544.0 / 9000.0);
		CHECK_NEAR(r.x, (SW - r.w) / 2);
	}
	/* zoom multiplies the base scale: 480x1088 height-fit at 2x is 480x1088, centred in x */
	v = view(FIT_HEIGHT, 2);
	check_rect(&v, 480, 1088, 240, -272, 480, 1088);
	/* pixel values of fit_place with a pan that is inside the limits */
	v = view(FIT_HEIGHT, 4); /* 960x2176 */
	v.pan_y = 100;
	check_rect(&v, 480, 1088, 0, (SH - 2176) / 2.0f + 100, 960, 2176);
}

static void test_zoom_clamp(void)
{
	CHECK_NEAR(fit_clamp_zoom(0.1f), 1.0);
	CHECK_NEAR(fit_clamp_zoom(0.75f), 1.0);
	CHECK_NEAR(fit_clamp_zoom(0.999f), 1.0);
	CHECK_NEAR(fit_clamp_zoom(1.0f), 1.0);
	CHECK_NEAR(fit_clamp_zoom(3.5f), 3.5);
	CHECK_NEAR(fit_clamp_zoom(8.0f), 8.0);
	CHECK_NEAR(fit_clamp_zoom(8.01f), 8.0);
	CHECK_NEAR(fit_clamp_zoom(100.0f), 8.0);
	{
		fit_view_t v = view(FIT_WIDTH, 1);
		fit_set_zoom_at(&v, 480, 1088, 0.25f, 100, 100);
		CHECK_NEAR(v.zoom, 1.0);
		fit_set_zoom_at(&v, 480, 1088, 50.0f, 100, 100);
		CHECK_NEAR(v.zoom, 8.0);
		CHECK_NEAR(fit_scale(&v, 480, 1088), 16.0);
	}
}

static void test_pan_clamps(void)
{
	fit_view_t v = view(FIT_HEIGHT, 8); /* 480x1088 -> scale 4 -> 1920x4352 */

	/* four edges: huge pans land exactly flush with the screen edges */
	v.pan_x = 99999;
	v.pan_y = 0;
	check_rect(&v, 480, 1088, 0, (SH - 4352) / 2.0f, 1920, 4352); /* left edge at 0 */
	v.pan_x = -99999;
	check_rect(&v, 480, 1088, SW - 1920, (SH - 4352) / 2.0f, 1920, 4352); /* right edge at SW */
	v.pan_x = 0;
	v.pan_y = 99999;
	check_rect(&v, 480, 1088, (SW - 1920) / 2.0f, 0, 1920, 4352); /* top edge at 0 */
	v.pan_y = -99999;
	check_rect(&v, 480, 1088, (SW - 1920) / 2.0f, SH - 4352, 1920, 4352); /* bottom at SH */
	/* fit_clamp_pan writes the clamped values back */
	v.pan_x = 99999;
	v.pan_y = -99999;
	fit_clamp_pan(&v, 480, 1088);
	CHECK_NEAR(v.pan_x, 480);
	CHECK_NEAR(v.pan_y, -1904);
	/* inside the limits nothing changes */
	v.pan_x = 12;
	v.pan_y = -34;
	fit_clamp_pan(&v, 480, 1088);
	CHECK_NEAR(v.pan_x, 12);
	CHECK_NEAR(v.pan_y, -34);

	/* a page smaller than the screen on an axis stays centred on that axis */
	v = view(FIT_HEIGHT, 1);
	v.pan_x = 77;
	v.pan_y = -50;
	check_rect(&v, 480, 1088, 360, 0, 240, 544);
	fit_clamp_pan(&v, 480, 1088);
	CHECK_NEAR(v.pan_x, 0);
	CHECK_NEAR(v.pan_y, 0);
	/* exactly screen-sized on an axis: no slack there */
	v = view(FIT_WIDTH, 1);
	v.pan_x = 5;
	fit_clamp_pan(&v, 480, 1088);
	CHECK_NEAR(v.pan_x, 0);

	/* fit_pan_by accumulates then clamps (limits: x 480, y 1904) */
	v = view(FIT_HEIGHT, 8);
	fit_pan_by(&v, 480, 1088, 100, -200);
	fit_pan_by(&v, 480, 1088, 100, -200);
	CHECK_NEAR(v.pan_x, 200);
	CHECK_NEAR(v.pan_y, -400);
	fit_pan_by(&v, 480, 1088, 1000, 5000);
	CHECK_NEAR(v.pan_x, 480);
	CHECK_NEAR(v.pan_y, 1904);
	fit_pan_by(&v, 480, 1088, -1000, -5000);
	CHECK_NEAR(v.pan_x, -480);
	CHECK_NEAR(v.pan_y, -1904);
}

static void test_start_end(void)
{
	fit_view_t v = view(FIT_HEIGHT, 4); /* 1920x1088 -> scale 0.5*4 = 2 -> 3840x2176 */
	fit_rect_t r;

	fit_pan_start(&v, 1920, 1088, 0);
	fit_place(&v, 1920, 1088, &r);
	CHECK_NEAR(r.x, 0);
	CHECK_NEAR(r.y, 0);
	fit_pan_start(&v, 1920, 1088, 1);
	fit_place(&v, 1920, 1088, &r);
	CHECK_NEAR(r.x, SW - 3840); /* right edge flush with the screen: RTL starts on the right */
	CHECK_NEAR(r.y, 0);
	fit_pan_end(&v, 1920, 1088, 0);
	fit_place(&v, 1920, 1088, &r);
	CHECK_NEAR(r.x, SW - 3840);
	CHECK_NEAR(r.y, SH - 2176);
	fit_pan_end(&v, 1920, 1088, 1);
	fit_place(&v, 1920, 1088, &r);
	CHECK_NEAR(r.x, 0);
	CHECK_NEAR(r.y, SH - 2176);
	/* page smaller than the screen: both stay centred */
	v = view(FIT_HEIGHT, 1);
	fit_pan_start(&v, 480, 1088, 0);
	CHECK_NEAR(v.pan_x, 0);
	CHECK_NEAR(v.pan_y, 0);
	fit_pan_end(&v, 480, 1088, 1);
	CHECK_NEAR(v.pan_x, 0);
	CHECK_NEAR(v.pan_y, 0);
	/* tall page at fit-width: starts at the top, ends at the bottom */
	v = view(FIT_WIDTH, 1); /* 800x9000 -> 960x10800 */
	fit_pan_start(&v, 800, 9000, 0);
	fit_place(&v, 800, 9000, &r);
	CHECK_NEAR(r.y, 0);
	fit_pan_end(&v, 800, 9000, 0);
	fit_place(&v, 800, 9000, &r);
	CHECK_NEAR(r.y, SH - 10800);
}

/* After a zoom at (fx,fy): the page point under it is unchanged, OR the pan clamp forced the
 * placement (page edge flush with the screen edge / page centred because it is smaller). */
static void zoom_probe(fit_mode_t mode, float pw, float ph, float zoom0, float zoom1, float fx,
                       float fy, int *held, int *clamped)
{
	fit_view_t v = view(mode, 1);
	float ax, ay, bx, by;
	fit_rect_t r;

	fit_set_zoom_at(&v, pw, ph, zoom0, SW / 2.0f, SH / 2.0f);
	fit_pan_by(&v, pw, ph, 37, -23);
	fit_screen_to_page(&v, pw, ph, fx, fy, &ax, &ay);
	fit_set_zoom_at(&v, pw, ph, zoom1, fx, fy);
	fit_screen_to_page(&v, pw, ph, fx, fy, &bx, &by);
	fit_place(&v, pw, ph, &r);
	{ /* the stored pan is already clamped (fit_place would hide an unclamped one) */
		fit_view_t c = v;
		fit_clamp_pan(&c, pw, ph);
		CHECK(c.pan_x == v.pan_x && c.pan_y == v.pan_y);
	}
	if (fabsf(ax - bx) < 0.01f && fabsf(ay - by) < 0.01f) {
		(*held)++;
		return;
	}
	(*clamped)++;
	/* the zoom changed the point, so the clamp must have been what moved it: on each axis
	 * where it differs, the page is flush with a screen edge or smaller than the screen */
	if (fabsf(ax - bx) >= 0.01f) {
		int flush = fabsf(r.x) < 0.01f || fabsf(r.x + r.w - SW) < 0.01f;
		int centred = r.w <= SW + 0.01f && fabsf(r.x - (SW - r.w) / 2) < 0.01f;
		CHECK(flush || centred);
	}
	if (fabsf(ay - by) >= 0.01f) {
		int flush = fabsf(r.y) < 0.01f || fabsf(r.y + r.h - SH) < 0.01f;
		int centred = r.h <= SH + 0.01f && fabsf(r.y - (SH - r.h) / 2) < 0.01f;
		CHECK(flush || centred);
	}
}

static void test_zoom_about_focal(void)
{
	/* exact numbers: 2000x3000 at original size, zoom 2 about (300,200) */
	fit_view_t v = view(FIT_ORIGINAL, 1);
	float px, py;
	int held = 0, clamped = 0;
	static const float zooms[] = { 1.0f, 1.5f, 2.0f, 3.0f, 5.0f, 8.0f };
	static const fit_mode_t modes[] = { FIT_WIDTH, FIT_HEIGHT, FIT_SMART, FIT_ORIGINAL };
	static const float pages[][2] = { { 800, 1200 }, { 1920, 1000 }, { 800, 9000 }, { 480, 1088 },
	                                  { 2000, 3000 } };
	unsigned m, p, a, b, fx, fy;

	fit_set_zoom_at(&v, 2000, 3000, 2.0f, 300, 200);
	CHECK_NEAR(v.pan_x, 180);
	CHECK_NEAR(v.pan_y, 72);
	check_rect(&v, 2000, 3000, -1340, -2656, 4000, 6000);
	fit_screen_to_page(&v, 2000, 3000, 300, 200, &px, &py);
	CHECK_NEAR(px, 820); /* (300 - (960-2000)/2) / 1 */
	CHECK_NEAR(py, 1428);
	/* zooming again about another point keeps that one fixed */
	fit_screen_to_page(&v, 2000, 3000, 700, 400, &px, &py);
	fit_set_zoom_at(&v, 2000, 3000, 3.0f, 700, 400);
	{
		float qx, qy;
		fit_screen_to_page(&v, 2000, 3000, 700, 400, &qx, &qy);
		CHECK_NEAR(qx, px);
		CHECK_NEAR(qy, py);
	}
	/* zooming back to 1 about the centre of a smaller-than-screen page recentres it */
	v = view(FIT_HEIGHT, 1);
	fit_set_zoom_at(&v, 480, 1088, 1.0f, 100, 100);
	CHECK_NEAR(v.pan_x, 0);
	CHECK_NEAR(v.pan_y, 0);

	/* sweep: every mode, page shape, zoom pair and a grid of focal points */
	for (m = 0; m < 4; m++)
		for (p = 0; p < 5; p++)
			for (a = 0; a < 6; a++)
				for (b = 0; b < 6; b++)
					for (fx = 0; fx < 5; fx++)
						for (fy = 0; fy < 5; fy++)
							zoom_probe(modes[m], pages[p][0], pages[p][1], zooms[a], zooms[b],
							           fx * 240.0f, fy * 136.0f, &held, &clamped);
	CHECK(held > 1000);    /* the invariance branch is really exercised ... */
	CHECK(clamped > 100);  /* ... and so is the clamp branch */
}

static void test_screen_to_page(void)
{
	fit_view_t v = view(FIT_WIDTH, 1);
	float px, py;

	/* 800x1200 at fit-width (1.2): shown 960x1440 centred, top at (544-1440)/2 = -448 */
	fit_screen_to_page(&v, 800, 1200, 0, 0, &px, &py);
	CHECK_NEAR(px, 0);
	CHECK_NEAR(py, 448 / 1.2);
	fit_screen_to_page(&v, 800, 1200, SW, SH, &px, &py);
	CHECK_NEAR(px, 800);
	CHECK_NEAR(py, (448 + 544) / 1.2);
	/* custom screen size */
	fit_view_init(&v, 480, 272);
	v.mode = FIT_WIDTH;
	fit_screen_to_page(&v, 480, 1088, 0, 0, &px, &py);
	CHECK_NEAR(px, 0);
	CHECK_NEAR(py, (1088 - 272) / 2.0);
}

int main(void)
{
	test_base_scales();
	test_placement();
	test_zoom_clamp();
	test_pan_clamps();
	test_start_end();
	test_zoom_about_focal();
	test_screen_to_page();
	return TEST_DONE("fit");
}
