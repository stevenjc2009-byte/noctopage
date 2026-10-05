#include "reader/zones.h"

#include <math.h>

zone_action_t zone_for_tap(int w, int h, int x, int y, int rtl, int vertical)
{
	if (vertical) {
		if (3 * y < h)
			return ZONE_PREV;
		if (3 * y >= 2 * h)
			return ZONE_NEXT;
		return ZONE_MENU;
	}
	if (3 * x < w)
		return rtl ? ZONE_NEXT : ZONE_PREV;
	if (3 * x >= 2 * w)
		return rtl ? ZONE_PREV : ZONE_NEXT;
	return ZONE_MENU;
}

void tapsm_init(tapsm_t *sm, int defer_taps)
{
	*sm = (tapsm_t){ .state = TS_IDLE, .defer_taps = defer_taps };
}

static long dist2(int ax, int ay, int bx, int by)
{
	long dx = ax - bx, dy = ay - by;
	return dx * dx + dy * dy;
}

static int emit(gesture_t *out, int n, gesture_type_t type, int x, int y, int dx, int dy,
                float scale)
{
	out[n].type = type;
	out[n].x = x;
	out[n].y = y;
	out[n].dx = dx;
	out[n].dy = dy;
	out[n].scale = scale;
	return n + 1;
}

/* Report the held-back tap (if any) and forget the remembered one. */
static int flush_tap(tapsm_t *sm, gesture_t *out, int n)
{
	if (sm->pending)
		n = emit(out, n, GESTURE_TAP, sm->last_x, sm->last_y, 0, 0, 1.0f);
	sm->pending = 0;
	sm->tap_valid = 0;
	return n;
}

static void start_pinch(tapsm_t *sm, int x0, int y0, int x1, int y1)
{
	float d = sqrtf((float)dist2(x0, y0, x1, y1));
	sm->state = TS_PINCH;
	sm->pinch_d0 = d < 1.0f ? 1.0f : d;
}

int tapsm_update(tapsm_t *sm, int touch_count, int x0, int y0, int x1, int y1, uint32_t now_ms,
                 gesture_t out[TAPSM_MAX_EVENTS])
{
	int n = 0;

	if (touch_count < 0)
		touch_count = 0;
	if (touch_count > 2)
		touch_count = 2;

	/* A held-back tap whose double-tap window has closed. */
	if (sm->state == TS_IDLE && touch_count == 0 && sm->pending &&
	    (uint32_t)(now_ms - sm->last_t) > TAP_DOUBLE_MS)
		n = flush_tap(sm, out, n);

	switch (sm->state) {
	case TS_IDLE:
		if (touch_count == 0)
			break;
		if (touch_count == 2) {
			n = flush_tap(sm, out, n);
			start_pinch(sm, x0, y0, x1, y1);
			break;
		}
		sm->down_x = x0;
		sm->down_y = y0;
		sm->cand = sm->tap_valid && (uint32_t)(now_ms - sm->last_t) <= TAP_DOUBLE_MS &&
		           dist2(x0, y0, sm->last_x, sm->last_y) <=
		               (long)TAP_DOUBLE_DIST * TAP_DOUBLE_DIST;
		if (!sm->cand)
			n = flush_tap(sm, out, n);
		sm->state = TS_DOWN;
		break;

	case TS_DOWN:
		if (touch_count == 0) {
			sm->state = TS_IDLE;
			if (sm->cand) {
				n = emit(out, n, GESTURE_DOUBLE_TAP, sm->down_x, sm->down_y, 0, 0, 1.0f);
				sm->tap_valid = 0;
				sm->pending = 0;
			} else {
				sm->tap_valid = 1;
				sm->last_x = sm->down_x;
				sm->last_y = sm->down_y;
				sm->last_t = now_ms;
				if (sm->defer_taps)
					sm->pending = 1;
				else
					n = emit(out, n, GESTURE_TAP, sm->down_x, sm->down_y, 0, 0, 1.0f);
			}
		} else if (touch_count == 2) {
			n = flush_tap(sm, out, n);
			start_pinch(sm, x0, y0, x1, y1);
		} else if (dist2(x0, y0, sm->down_x, sm->down_y) >
		           (long)TAP_DRAG_DIST * TAP_DRAG_DIST) {
			n = flush_tap(sm, out, n);
			n = emit(out, n, GESTURE_DRAG, x0, y0, x0 - sm->down_x, y0 - sm->down_y, 1.0f);
			sm->px = x0;
			sm->py = y0;
			sm->state = TS_DRAG;
		}
		break;

	case TS_DRAG:
		if (touch_count == 0) {
			sm->state = TS_IDLE;
		} else if (touch_count == 2) {
			start_pinch(sm, x0, y0, x1, y1);
		} else if (x0 != sm->px || y0 != sm->py) {
			n = emit(out, n, GESTURE_DRAG, x0, y0, x0 - sm->px, y0 - sm->py, 1.0f);
			sm->px = x0;
			sm->py = y0;
		}
		break;

	case TS_PINCH:
		if (touch_count == 2) {
			float d = sqrtf((float)dist2(x0, y0, x1, y1));
			n = emit(out, n, GESTURE_PINCH, (x0 + x1) / 2, (y0 + y1) / 2, 0, 0,
			         d / sm->pinch_d0);
		} else {
			sm->state = touch_count == 0 ? TS_IDLE : TS_PINCH_WAIT;
		}
		break;

	case TS_PINCH_WAIT:
		if (touch_count == 0)
			sm->state = TS_IDLE;
		else if (touch_count == 2)
			start_pinch(sm, x0, y0, x1, y1);
		break;
	}
	return n;
}
