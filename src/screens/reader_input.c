/* reader_input.c - buttons, sticks and touch for the comic reader (docs/PLAN.md Controls:
 * R next, L previous, mirrored in right-to-left (the d-pad and sticks only pan, they
 * never turn pages); Up/Down scroll; Triangle overlay; Square reading mode; Select
 * dim; edge taps, pinch and double-tap zoom). */
#include "screens/reader_priv.h"

#include "core/log.h"
#include "core/prefs.h"
#include "reader/stickzoom.h"

#include <stdlib.h>

#define KEY_SCROLL 16.0f    /* px per frame while Up/Down is held */
#define STICK_DEAD 28
#define STICK_DIV 6.0f      /* px per frame per stick unit past the dead zone */
#define SWIPE_TURN 90.0f    /* horizontal swipe that turns the page */
#define DOUBLE_TAP_ZOOM 2.5f

static int known(void) { return R.count > 0 && R.known[R.page]; }
static float pw(void)
{
	float w, h;
	reader_view_size(&w, &h);
	return w;
}

static float ph(void)
{
	float w, h;
	reader_view_size(&w, &h);
	return h;
}

/* Bottom of a tall page reached (or the page fits): another Down goes to the next page. */
static int at_end(int down)
{
	if (!known()) return 1;
	fit_view_t v = R.view;
	float before = v.pan_y;
	fit_pan_by(&v, pw(), ph(), 0, down ? -1.0f : 1.0f);
	return v.pan_y == before;
}

static void scroll_keys(const input_t *in)
{
	uint32_t fresh = in->pressed & ~R.prev_held;
	int up = (in->held & IN_UP) != 0, down = (in->held & IN_DOWN) != 0;
	int left = (in->held & IN_LEFT) != 0, right = (in->held & IN_RIGHT) != 0;
	if (R.mode == RM_VERTICAL) {
		if (up || down) reader_strip_pan(0, down ? -KEY_SCROLL : KEY_SCROLL);
		if (left || right) reader_strip_pan(left ? KEY_SCROLL : -KEY_SCROLL, 0);
		return;
	}
	if (known() && (left || right))
		fit_pan_by(&R.view, pw(), ph(), left ? KEY_SCROLL : -KEY_SCROLL, 0);
	if ((fresh & IN_DOWN) && at_end(1)) {
		reader_step(1);
		return;
	}
	if ((fresh & IN_UP) && at_end(0)) {
		reader_step(-1);
		return;
	}
	if (known() && (up || down))
		fit_pan_by(&R.view, pw(), ph(), 0, down ? -KEY_SCROLL : KEY_SCROLL);
}

static float stick(int v)
{
	int d = v - 128;
	if (abs(d) < STICK_DEAD) return 0;
	return (float)(d > 0 ? d - STICK_DEAD : d + STICK_DEAD) / STICK_DIV;
}

void reader_handle_buttons(const input_t *in)
{
	if (in->pressed & IN_TRIANGLE) R.overlay = !R.overlay;
	if (in->pressed & IN_SELECT) {
		R.dim = (R.dim + 1) % DIM_LEVELS;
		prefs_set_int("reader", "dim", R.dim);
	}
	if (in->pressed & IN_SQUARE) {
		R.mode = (read_mode_t)((R.mode + 1) % RM_COUNT);
		reader_setting_save("mode", R.mode);
		R.sp_dirty = 1;
		log_printf("reader: mode %s", reader_mode_name());
		reader_goto(R.page, 1);
	}
	if ((in->pressed & IN_CONFIRM) && R.overlay) {
		reader_menu_open();
		return;
	}

	/* Page turns come from the shoulder buttons only: the d-pad and the sticks pan. */
	uint32_t next = reader_rtl() ? IN_L : IN_R;
	uint32_t prev = reader_rtl() ? IN_R : IN_L;
	if (R.mode == RM_VERTICAL) {
		float page = SCREEN_H - OVERLAY_BAR_H;
		if (in->pressed & IN_R) reader_strip_pan(0, -page);
		if (in->pressed & IN_L) reader_strip_pan(0, page);
	} else if (in->pressed & next) {
		reader_step(1);
	} else if (in->pressed & prev) {
		reader_step(-1);
	}
	scroll_keys(in);

	unsigned dt = R.stick_at && in->now_ms >= R.stick_at
        ? (unsigned)(in->now_ms - R.stick_at > 50 ? 50 : in->now_ms - R.stick_at) : 16;
    R.stick_at = in->now_ms;
    float factor = stickzoom_factor(in->ry, dt);
    if (factor != 1.0f) {
        if (R.mode == RM_VERTICAL)
            reader_strip_zoom_at(R.strip_zoom * factor, SCREEN_W / 2.0f, SCREEN_H / 2.0f);
        else if (known())
            fit_set_zoom_at(&R.view, pw(), ph(), R.view.zoom * factor,
                            SCREEN_W / 2.0f, SCREEN_H / 2.0f);
    }
    float sx = stick(in->lx), sy = stick(in->ly);
	if (sx != 0 || sy != 0) {
		if (R.mode == RM_VERTICAL)
			reader_strip_pan(-sx, -sy);
		else if (known())
			fit_pan_by(&R.view, pw(), ph(), -sx, -sy);
	}
	R.prev_held = in->held;
}

/* ---- touch ---------------------------------------------------------------------------- */

static void on_tap(int x, int y)
{
	if (R.overlay) {
		if (x >= 902 && y >= 8 && y < 56) { reader_menu_open(); return; }
		if (y >= OVERLAY_BAR_H && y < SCREEN_H - OVERLAY_BAR_H) R.overlay = 0;
		return;
	}
	zone_action_t z = zone_for_tap(SCREEN_W, SCREEN_H, x, y, reader_rtl(),
	                               R.mode == RM_VERTICAL);
	if (z == ZONE_MENU) {
		R.overlay = 1;
	} else if (R.mode == RM_VERTICAL) {
		float page = SCREEN_H - OVERLAY_BAR_H;
		reader_strip_pan(0, z == ZONE_NEXT ? -page : page);
	} else {
		reader_step(z == ZONE_NEXT ? 1 : -1);
	}
}

static void on_double_tap(int x, int y)
{
	if (R.mode == RM_VERTICAL) {
		reader_strip_zoom_at(R.strip_zoom > 1.01f ? 1.0f : 2.0f, (float)x, (float)y);
	} else if (known()) {
		float z = R.view.zoom > 1.01f ? 1.0f : DOUBLE_TAP_ZOOM;
		fit_set_zoom_at(&R.view, pw(), ph(), z, (float)x, (float)y);
	}
}

static void on_drag(int dx, int dy)
{
	if (R.mode == RM_VERTICAL) {
		reader_strip_pan((float)dx, (float)dy);
		return;
	}
	if (!known()) return;
	fit_rect_t r;
	fit_place(&R.view, pw(), ph(), &r);
	if (r.w <= SCREEN_W + 0.5f) R.swipe_dx += (float)dx; /* no room to pan sideways: a swipe */
	fit_pan_by(&R.view, pw(), ph(), (float)dx, (float)dy);
}

static void on_pinch(float scale, int x, int y)
{
	if (!R.pinching) {
		R.pinching = 1;
		R.pinch_zoom = R.mode == RM_VERTICAL ? R.strip_zoom : R.view.zoom;
	}
	if (R.mode == RM_VERTICAL)
		reader_strip_zoom_at(R.pinch_zoom * scale, (float)x, (float)y);
	else if (known())
		fit_set_zoom_at(&R.view, pw(), ph(), R.pinch_zoom * scale, (float)x, (float)y);
}

static void seek(const input_t *in)
{
	if (in->touch_count > 0) {
		R.seek_page = reader_slider_page(in->touch[0].x);
		return;
	}
	R.seeking = 0;
	tapsm_init(&R.tap, 1);
	reader_goto(R.seek_page, 1);
}

void reader_handle_touch(const input_t *in)
{
	if (R.seeking) {
		seek(in);
		return;
	}
	if (in->touch_began && R.count > 0 && reader_overlay_hit_slider(in->began_x, in->began_y)) {
		R.seeking = 1;
		R.seek_page = reader_slider_page(in->began_x);
		return;
	}
	if (in->touch_began) R.swipe_dx = 0;

	gesture_t ev[TAPSM_MAX_EVENTS];
	int x1 = in->touch_count > 1 ? in->touch[1].x : 0, y1 = in->touch_count > 1 ? in->touch[1].y : 0;
	int n = tapsm_update(&R.tap, in->touch_count, in->touch_count ? in->touch[0].x : 0,
	                     in->touch_count ? in->touch[0].y : 0, x1, y1, (uint32_t)in->now_ms, ev);
	for (int i = 0; i < n && R.count > 0; i++) {
		switch (ev[i].type) {
		case GESTURE_TAP: on_tap(ev[i].x, ev[i].y); break;
		case GESTURE_DOUBLE_TAP: on_double_tap(ev[i].x, ev[i].y); break;
		case GESTURE_DRAG: on_drag(ev[i].dx, ev[i].dy); break;
		case GESTURE_PINCH: on_pinch(ev[i].scale, ev[i].x, ev[i].y); break;
		}
	}
	if (in->touch_count == 0) {
		int was_pinch = R.pinching;
		R.pinching = 0;
		/* A pinch release must never turn the page: only a pure drag swipes. */
		if (!was_pinch && (R.swipe_dx <= -SWIPE_TURN || R.swipe_dx >= SWIPE_TURN)) {
			int forward = R.swipe_dx < 0; /* finger moved left: the next page comes in */
			reader_step(forward != reader_rtl() ? 1 : -1);
		}
		R.swipe_dx = 0;
	}
}
