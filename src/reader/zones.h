/* zones — tap zones and the touch gesture state machine of the reader (pure C, no psp2).
 *
 * Zones (screen w x h, normally 960x544):
 *   paged:    left third / centre third / right third by x. LTR: left = PREV, right = NEXT.
 *             RTL mirrors it: left = NEXT, right = PREV. Centre = MENU (toggle overlay).
 *   vertical: top third = PREV (previous screen), bottom third = NEXT, centre = MENU.
 *             Reading direction does not matter in vertical mode.
 * Thirds are exact integer comparisons: x is in the left third iff 3*x < w, in the right
 * third iff 3*x >= 2*w (960 wide: left x 0..319, centre 320..639, right 640..959).
 */
#ifndef NOCTO_ZONES_H
#define NOCTO_ZONES_H

#include <stdint.h>

#define ZONE_SCREEN_W 960
#define ZONE_SCREEN_H 544

typedef enum { ZONE_PREV, ZONE_NEXT, ZONE_MENU } zone_action_t;

zone_action_t zone_for_tap(int w, int h, int x, int y, int rtl, int vertical);

/* ---- gesture state machine ---------------------------------------------------------- */

#define TAP_DOUBLE_MS     300 /* second tap within this after the first lifted (inclusive) */
#define TAP_DOUBLE_DIST   40  /* ... and within this many px of it (inclusive) */
#define TAP_DRAG_DIST     20  /* moving MORE than this from the touch-down point = drag */

typedef enum {
	GESTURE_TAP,        /* x,y = where the finger went down */
	GESTURE_DOUBLE_TAP, /* x,y = where the second tap went down */
	GESTURE_DRAG,       /* dx,dy = movement since the previous drag event (the first one
	                       carries the movement since touch-down); x,y = current position */
	GESTURE_PINCH       /* scale = current finger distance / distance at pinch start;
	                       x,y = focal point (midpoint of the two fingers) */
} gesture_type_t;

typedef struct {
	gesture_type_t type;
	int x, y;
	int dx, dy;
	float scale;
} gesture_t;

#define TAPSM_MAX_EVENTS 3

typedef enum { TS_IDLE, TS_DOWN, TS_DRAG, TS_PINCH, TS_PINCH_WAIT } tapsm_state_t;

typedef struct {
	tapsm_state_t state;
	int defer_taps;           /* see tapsm_init */
	int tap_valid;            /* a previous tap is remembered for double-tap matching */
	int pending;              /* that tap has not been reported yet (defer mode) */
	int last_x, last_y;       /* previous tap (touch-down point) */
	uint32_t last_t;          /* previous tap: time the finger lifted */
	int down_x, down_y;       /* current touch-down point */
	int cand;                 /* current touch started close enough to the last tap */
	int px, py;               /* last reported drag position */
	float pinch_d0;           /* finger distance at pinch start (>= 1) */
} tapsm_t;

/* defer_taps = 0: a TAP is reported the frame the finger lifts; a quick second tap then
 *   reports DOUBLE_TAP (instead of a second TAP), so the first tap's action has already run.
 * defer_taps != 0: a TAP is held back until TAP_DOUBLE_MS has passed with no second tap (or
 *   until the next touch proves it cannot be one), costing 300 ms of tap latency; a double
 *   tap then reports only DOUBLE_TAP. */
void tapsm_init(tapsm_t *sm, int defer_taps);

/* Feed one frame: touch_count (0, 1 or 2; more is treated as 2), the first two touch points
 * (x1,y1 ignored unless touch_count >= 2) and a millisecond clock (uint32 wrap is fine).
 * Writes up to TAPSM_MAX_EVENTS events to out in order and returns how many.
 *
 * Tap = one finger down and up having moved at most TAP_DRAG_DIST px (Euclidean, from the
 * touch-down point); no hold-time limit. Double tap = a tap whose touch-down is within
 * TAP_DOUBLE_MS of the previous tap's lift and within TAP_DOUBLE_DIST px of its touch-down
 * point; it is reported on lift, and a third quick tap starts over. A touch that moves further
 * than TAP_DRAG_DIST becomes a drag and never a tap. A second finger turns a touch into a
 * pinch; when one finger lifts the rest of the touch is ignored until all fingers are up. */
int tapsm_update(tapsm_t *sm, int touch_count, int x0, int y0, int x1, int y1, uint32_t now_ms,
                 gesture_t out[TAPSM_MAX_EVENTS]);

#endif
