#include "test.h"

#include "reader/zones.h"

#define W ZONE_SCREEN_W
#define H ZONE_SCREEN_H

static void test_zone_table(void)
{
	int x, y, prev = 0, next = 0, menu = 0;

	/* paged LTR: left third PREV, right third NEXT, centre MENU; exact boundaries */
	CHECK_INT(zone_for_tap(W, H, 0, 0, 0, 0), ZONE_PREV);
	CHECK_INT(zone_for_tap(W, H, 319, 543, 0, 0), ZONE_PREV);
	CHECK_INT(zone_for_tap(W, H, 320, 0, 0, 0), ZONE_MENU);
	CHECK_INT(zone_for_tap(W, H, 639, 300, 0, 0), ZONE_MENU);
	CHECK_INT(zone_for_tap(W, H, 640, 0, 0, 0), ZONE_NEXT);
	CHECK_INT(zone_for_tap(W, H, 959, 543, 0, 0), ZONE_NEXT);
	/* RTL mirrors left/right; the centre stays the menu */
	CHECK_INT(zone_for_tap(W, H, 0, 0, 1, 0), ZONE_NEXT);
	CHECK_INT(zone_for_tap(W, H, 319, 0, 1, 0), ZONE_NEXT);
	CHECK_INT(zone_for_tap(W, H, 320, 0, 1, 0), ZONE_MENU);
	CHECK_INT(zone_for_tap(W, H, 639, 0, 1, 0), ZONE_MENU);
	CHECK_INT(zone_for_tap(W, H, 640, 0, 1, 0), ZONE_PREV);
	CHECK_INT(zone_for_tap(W, H, 959, 0, 1, 0), ZONE_PREV);
	/* vertical strip: top PREV, bottom NEXT, regardless of x and direction */
	CHECK_INT(zone_for_tap(W, H, 0, 0, 0, 1), ZONE_PREV);
	CHECK_INT(zone_for_tap(W, H, 959, 181, 0, 1), ZONE_PREV);
	CHECK_INT(zone_for_tap(W, H, 480, 182, 0, 1), ZONE_MENU);
	CHECK_INT(zone_for_tap(W, H, 480, 362, 0, 1), ZONE_MENU);
	CHECK_INT(zone_for_tap(W, H, 0, 363, 0, 1), ZONE_NEXT);
	CHECK_INT(zone_for_tap(W, H, 959, 543, 0, 1), ZONE_NEXT);
	CHECK_INT(zone_for_tap(W, H, 0, 0, 1, 1), ZONE_PREV);
	CHECK_INT(zone_for_tap(W, H, 0, 543, 1, 1), ZONE_NEXT);
	/* other screen size */
	CHECK_INT(zone_for_tap(100, 100, 33, 0, 0, 0), ZONE_PREV);
	CHECK_INT(zone_for_tap(100, 100, 34, 0, 0, 0), ZONE_MENU);
	CHECK_INT(zone_for_tap(100, 100, 66, 0, 0, 0), ZONE_MENU);
	CHECK_INT(zone_for_tap(100, 100, 67, 0, 0, 0), ZONE_NEXT);

	/* whole-screen sweep: each third is exactly one third wide */
	for (x = 0; x < W; x++)
		for (y = 0; y < H; y += 17) {
			zone_action_t z = zone_for_tap(W, H, x, y, 0, 0);
			prev += z == ZONE_PREV;
			menu += z == ZONE_MENU;
			next += z == ZONE_NEXT;
		}
	CHECK_INT(prev, menu);
	CHECK_INT(next, menu);
	CHECK_INT(prev + menu + next, W * ((H + 16) / 17));
}

/* ---- gestures --------------------------------------------------------------------------- */

static tapsm_t sm;
static gesture_t log_ev[64];
static int nlog;

static void start(int defer)
{
	tapsm_init(&sm, defer);
	nlog = 0;
}

static int step(int cnt, int x0, int y0, int x1, int y1, uint32_t t)
{
	gesture_t ev[TAPSM_MAX_EVENTS];
	int n = tapsm_update(&sm, cnt, x0, y0, x1, y1, t, ev), i;
	for (i = 0; i < n && nlog < 64; i++)
		log_ev[nlog++] = ev[i];
	return n;
}

static void tap(int x, int y, uint32_t t_down, uint32_t t_up)
{
	step(1, x, y, 0, 0, t_down);
	step(0, 0, 0, 0, 0, t_up);
}

static void expect_ev(int i, gesture_type_t type, int x, int y)
{
	CHECK(i < nlog);
	if (i < nlog) {
		CHECK_INT(log_ev[i].type, type);
		CHECK_INT(log_ev[i].x, x);
		CHECK_INT(log_ev[i].y, y);
	}
}

static void test_tap_and_drag(void)
{
	start(0);
	CHECK_INT(step(1, 100, 200, 0, 0, 0), 0); /* press: nothing yet */
	CHECK_INT(step(1, 103, 202, 0, 0, 16), 0); /* wobble */
	CHECK_INT(nlog, 0);
	CHECK_INT(step(0, 0, 0, 0, 0, 50), 1);
	expect_ev(0, GESTURE_TAP, 100, 200); /* reported at the touch-down point */
	CHECK_INT(nlog, 1);

	/* exactly 20 px away (12,16) is still a tap; 20.8 px is a drag */
	start(0);
	step(1, 100, 100, 0, 0, 0);
	step(1, 112, 116, 0, 0, 10);
	step(0, 0, 0, 0, 0, 20);
	CHECK_INT(nlog, 1);
	expect_ev(0, GESTURE_TAP, 100, 100);

	start(0);
	step(1, 100, 100, 0, 0, 0);
	CHECK_INT(step(1, 112, 117, 0, 0, 10), 1);
	expect_ev(0, GESTURE_DRAG, 112, 117);
	CHECK_INT(log_ev[0].dx, 12); /* first drag carries the movement since touch-down */
	CHECK_INT(log_ev[0].dy, 17);
	CHECK_INT(step(1, 117, 120, 0, 0, 20), 1);
	CHECK_INT(log_ev[1].dx, 5);
	CHECK_INT(log_ev[1].dy, 3);
	CHECK_INT(step(1, 117, 120, 0, 0, 30), 0); /* no movement, no event */
	CHECK_INT(step(1, 100, 100, 0, 0, 40), 1); /* back past the start is still a drag */
	CHECK_INT(log_ev[2].dx, -17);
	CHECK_INT(log_ev[2].dy, -20);
	CHECK_INT(step(0, 0, 0, 0, 0, 50), 0); /* lifting after a drag is not a tap */
	CHECK_INT(nlog, 3);

	/* 20.02 px (20,1) is already a drag: the limit is exactly 20 px, not "about" 20 */
	start(0);
	step(1, 100, 100, 0, 0, 0);
	step(1, 120, 101, 0, 0, 10);
	CHECK_INT(nlog, 1);
	CHECK_INT(log_ev[0].type, GESTURE_DRAG);
	start(0);
	step(1, 100, 100, 0, 0, 0);
	step(1, 120, 100, 0, 0, 10);
	step(0, 0, 0, 0, 0, 20);
	CHECK_INT(log_ev[0].type, GESTURE_TAP);

	/* long-ish drag in one axis crossing the threshold slowly */
	start(0);
	step(1, 500, 300, 0, 0, 0);
	step(1, 500, 310, 0, 0, 10);
	step(1, 500, 320, 0, 0, 20);
	CHECK_INT(nlog, 0); /* exactly 20 px up to here */
	step(1, 500, 321, 0, 0, 30);
	CHECK_INT(nlog, 1);
	CHECK_INT(log_ev[0].dy, 21);
}

static void test_double_tap_timing(void)
{
	uint32_t gap;

	/* gap = lift of tap 1 -> touch-down of tap 2; <= 300 ms is a double tap */
	for (gap = 299; gap <= 301; gap++) {
		start(0);
		tap(400, 300, 0, 50);
		tap(405, 300, 50 + gap, 50 + gap + 40);
		if (gap <= 300) {
			CHECK_INT(nlog, 2);
			expect_ev(0, GESTURE_TAP, 400, 300);
			expect_ev(1, GESTURE_DOUBLE_TAP, 405, 300);
		} else {
			CHECK_INT(nlog, 2);
			expect_ev(0, GESTURE_TAP, 400, 300);
			expect_ev(1, GESTURE_TAP, 405, 300);
		}
	}

	/* distance: <= 40 px (Euclid) between the two touch-down points */
	start(0);
	tap(400, 300, 0, 50);
	tap(440, 300, 100, 140);
	expect_ev(1, GESTURE_DOUBLE_TAP, 440, 300);
	start(0);
	tap(400, 300, 0, 50);
	tap(441, 300, 100, 140);
	expect_ev(1, GESTURE_TAP, 441, 300);
	start(0);
	tap(400, 300, 0, 50);
	tap(424, 332, 100, 140); /* 24,32 -> exactly 40 */
	expect_ev(1, GESTURE_DOUBLE_TAP, 424, 332);
	start(0);
	tap(400, 300, 0, 50);
	tap(424, 333, 100, 140); /* 40.8 */
	expect_ev(1, GESTURE_TAP, 424, 333);

	/* a third quick tap starts over; the fourth doubles with the third */
	start(0);
	tap(400, 300, 0, 50);
	tap(400, 300, 100, 150);
	tap(400, 300, 200, 250);
	tap(400, 300, 300, 350);
	CHECK_INT(nlog, 4);
	expect_ev(0, GESTURE_TAP, 400, 300);
	expect_ev(1, GESTURE_DOUBLE_TAP, 400, 300);
	expect_ev(2, GESTURE_TAP, 400, 300);
	expect_ev(3, GESTURE_DOUBLE_TAP, 400, 300);

	/* a drag between two taps breaks the pair */
	start(0);
	tap(400, 300, 0, 50);
	step(1, 400, 300, 0, 0, 100);
	step(1, 450, 300, 0, 0, 110);
	step(0, 0, 0, 0, 0, 120);
	tap(400, 300, 150, 200);
	CHECK_INT(nlog, 3);
	expect_ev(2, GESTURE_TAP, 400, 300);

	/* 32-bit clock wrap: lift at 0xFFFFFF00, next press 272 ms later after the wrap */
	start(0);
	tap(400, 300, 0xFFFFFE00u, 0xFFFFFF00u);
	tap(400, 300, 0x10u, 0x40u);
	expect_ev(1, GESTURE_DOUBLE_TAP, 400, 300);
}

static void test_defer_mode(void)
{
	uint32_t t;

	/* single tap: reported only once 300 ms have passed since the lift */
	start(1);
	tap(300, 200, 0, 50);
	CHECK_INT(step(0, 0, 0, 0, 0, 350), 0); /* 300 ms: window still open */
	CHECK_INT(nlog, 0);
	CHECK_INT(step(0, 0, 0, 0, 0, 351), 1);
	expect_ev(0, GESTURE_TAP, 300, 200);
	for (t = 400; t < 2000; t += 100)
		step(0, 0, 0, 0, 0, t);
	CHECK_INT(nlog, 1); /* reported once */

	/* double tap: only DOUBLE_TAP, never the single */
	start(1);
	tap(300, 200, 0, 50);
	tap(310, 205, 200, 240);
	CHECK_INT(nlog, 1);
	expect_ev(0, GESTURE_DOUBLE_TAP, 310, 205);
	for (t = 300; t < 2000; t += 100)
		step(0, 0, 0, 0, 0, t);
	CHECK_INT(nlog, 1);

	/* second press just past the window: first tap is released first, then the second tap
	 * waits for its own window */
	start(1);
	tap(300, 200, 0, 50);
	step(1, 300, 200, 0, 0, 351);
	CHECK_INT(nlog, 1);
	expect_ev(0, GESTURE_TAP, 300, 200);
	step(0, 0, 0, 0, 0, 400);
	step(0, 0, 0, 0, 0, 701);
	CHECK_INT(nlog, 2);
	expect_ev(1, GESTURE_TAP, 300, 200);

	/* second press in time but too far away: first tap flushed at the press */
	start(1);
	tap(300, 200, 0, 50);
	step(1, 500, 200, 0, 0, 100);
	CHECK_INT(nlog, 1);
	expect_ev(0, GESTURE_TAP, 300, 200);

	/* second touch in time that turns into a drag: TAP then DRAG, in that order */
	start(1);
	tap(300, 200, 0, 50);
	step(1, 300, 200, 0, 0, 100);
	CHECK_INT(nlog, 0);
	CHECK_INT(step(1, 300, 260, 0, 0, 110), 2);
	expect_ev(0, GESTURE_TAP, 300, 200);
	expect_ev(1, GESTURE_DRAG, 300, 260);
}

static void test_pinch(void)
{
	start(0);
	CHECK_INT(step(1, 400, 272, 0, 0, 0), 0);
	CHECK_INT(step(2, 400, 272, 500, 272, 10), 0); /* second finger: pinch begins, d0 = 100 */
	CHECK_INT(step(2, 300, 272, 600, 272, 20), 1);
	CHECK_INT(log_ev[0].type, GESTURE_PINCH);
	CHECK(log_ev[0].scale == 3.0f); /* 300 / 100 */
	CHECK_INT(log_ev[0].x, 450);
	CHECK_INT(log_ev[0].y, 272);
	CHECK_INT(step(2, 450, 272, 550, 272, 30), 1);
	CHECK(log_ev[1].scale == 1.0f);
	CHECK_INT(step(2, 475, 100, 525, 100, 40), 1); /* 50 apart, focal moved up */
	CHECK(log_ev[2].scale == 0.5f);
	CHECK_INT(log_ev[2].x, 500);
	CHECK_INT(log_ev[2].y, 100);
	/* diagonal distance: 3-4-5 triangle scaled by 20 -> 100, same as start */
	CHECK_INT(step(2, 500, 100, 560, 180, 50), 1);
	CHECK(log_ev[3].scale == 1.0f);
	/* one finger lifts: the leftover finger neither drags nor taps */
	CHECK_INT(step(1, 500, 100, 0, 0, 60), 0);
	CHECK_INT(step(1, 700, 400, 0, 0, 70), 0);
	CHECK_INT(step(0, 0, 0, 0, 0, 80), 0);
	CHECK_INT(nlog, 4);
	/* and the machine is usable again */
	tap(100, 100, 100, 130);
	CHECK_INT(nlog, 5);
	expect_ev(4, GESTURE_TAP, 100, 100);

	/* two fingers down in the same frame; pinch start distance 200 */
	start(0);
	step(2, 100, 100, 300, 100, 0);
	step(2, 100, 100, 500, 100, 10);
	CHECK(log_ev[0].scale == 2.0f);
	step(0, 0, 0, 0, 0, 20);
	CHECK_INT(nlog, 1);

	/* a finger down then a second one cancels the tap */
	start(0);
	step(1, 100, 100, 0, 0, 0);
	step(2, 100, 100, 200, 100, 10);
	step(0, 0, 0, 0, 0, 20);
	CHECK_INT(nlog, 0);

	/* drag then second finger: pinch from then on; scale is relative to that moment */
	start(0);
	step(1, 100, 100, 0, 0, 0);
	step(1, 150, 100, 0, 0, 10);
	step(2, 150, 100, 250, 100, 20);
	step(2, 150, 100, 350, 100, 30);
	CHECK_INT(nlog, 2);
	CHECK_INT(log_ev[0].type, GESTURE_DRAG);
	CHECK_INT(log_ev[1].type, GESTURE_PINCH);
	CHECK(log_ev[1].scale == 2.0f);

	/* three touches count as two; zero distance does not divide by zero */
	start(0);
	step(2, 100, 100, 100, 100, 0);
	step(3, 100, 100, 150, 100, 10);
	CHECK(log_ev[0].scale == 50.0f); /* d0 clamped to 1 px */
}

static void test_pending_tap_is_flushed_by_pinch(void)
{
	start(1);
	tap(300, 200, 0, 50);
	step(2, 100, 100, 200, 100, 100);
	CHECK_INT(nlog, 1);
	expect_ev(0, GESTURE_TAP, 300, 200);
}

int main(void)
{
	test_zone_table();
	test_tap_and_drag();
	test_double_tap_timing();
	test_defer_mode();
	test_pinch();
	test_pending_tap_is_flushed_by_pinch();
	return TEST_DONE("zones");
}
