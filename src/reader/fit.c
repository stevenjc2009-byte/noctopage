#include "reader/fit.h"

void fit_view_init(fit_view_t *v, int screen_w, int screen_h)
{
	v->screen_w = screen_w;
	v->screen_h = screen_h;
	v->mode = FIT_SMART;
	v->zoom = 1.0f;
	v->pan_x = v->pan_y = 0.0f;
}

float fit_base_scale(fit_mode_t mode, float pw, float ph, int screen_w, int screen_h)
{
	float sw, sh;

	if (pw <= 0.0f || ph <= 0.0f)
		return 1.0f;
	sw = (float)screen_w / pw;
	sh = (float)screen_h / ph;
	switch (mode) {
	case FIT_WIDTH:
		return sw;
	case FIT_HEIGHT:
		return sh;
	case FIT_SMART:
		if (ph > FIT_SMART_RATIO * pw)
			return sw;
		return sw < sh ? sw : sh;
	case FIT_ORIGINAL:
	default:
		return 1.0f;
	}
}

float fit_clamp_zoom(float zoom)
{
	if (zoom < FIT_ZOOM_MIN)
		return FIT_ZOOM_MIN;
	if (zoom > FIT_ZOOM_MAX)
		return FIT_ZOOM_MAX;
	return zoom;
}

float fit_scale(const fit_view_t *v, float pw, float ph)
{
	return fit_base_scale(v->mode, pw, ph, v->screen_w, v->screen_h) * v->zoom;
}

/* How far the page may move from centre on one axis: 0 when it fits. */
static float pan_limit(float shown, int screen)
{
	return shown > (float)screen ? (shown - (float)screen) * 0.5f : 0.0f;
}

static float clampf(float x, float lim)
{
	return x > lim ? lim : (x < -lim ? -lim : x);
}

void fit_clamp_pan(fit_view_t *v, float pw, float ph)
{
	float s = fit_scale(v, pw, ph);
	v->pan_x = clampf(v->pan_x, pan_limit(pw * s, v->screen_w));
	v->pan_y = clampf(v->pan_y, pan_limit(ph * s, v->screen_h));
}

void fit_place(const fit_view_t *v, float pw, float ph, fit_rect_t *out)
{
	fit_view_t c = *v;
	float s;

	fit_clamp_pan(&c, pw, ph);
	s = fit_scale(&c, pw, ph);
	out->w = pw * s;
	out->h = ph * s;
	out->x = ((float)c.screen_w - out->w) * 0.5f + c.pan_x;
	out->y = ((float)c.screen_h - out->h) * 0.5f + c.pan_y;
}

void fit_pan_by(fit_view_t *v, float pw, float ph, float dx, float dy)
{
	v->pan_x += dx;
	v->pan_y += dy;
	fit_clamp_pan(v, pw, ph);
}

void fit_screen_to_page(const fit_view_t *v, float pw, float ph, float sx, float sy, float *px,
                        float *py)
{
	fit_rect_t r;
	float s = fit_scale(v, pw, ph);

	fit_place(v, pw, ph, &r);
	*px = (sx - r.x) / s;
	*py = (sy - r.y) / s;
}

void fit_set_zoom_at(fit_view_t *v, float pw, float ph, float zoom, float fx, float fy)
{
	float u, w, base = fit_base_scale(v->mode, pw, ph, v->screen_w, v->screen_h);
	float nw, nh;

	fit_screen_to_page(v, pw, ph, fx, fy, &u, &w);
	v->zoom = fit_clamp_zoom(zoom);
	nw = pw * base * v->zoom;
	nh = ph * base * v->zoom;
	/* new top-left = focal - page point * new scale; pan = top-left - centred top-left */
	v->pan_x = (fx - u * base * v->zoom) - ((float)v->screen_w - nw) * 0.5f;
	v->pan_y = (fy - w * base * v->zoom) - ((float)v->screen_h - nh) * 0.5f;
	fit_clamp_pan(v, pw, ph);
}

static void pan_to(fit_view_t *v, float pw, float ph, int top, int lead_side_positive)
{
	float s = fit_scale(v, pw, ph);
	float lx = pan_limit(pw * s, v->screen_w);
	float ly = pan_limit(ph * s, v->screen_h);

	v->pan_x = lead_side_positive ? lx : -lx;
	v->pan_y = top ? ly : -ly;
}

void fit_pan_start(fit_view_t *v, float pw, float ph, int rtl)
{
	pan_to(v, pw, ph, 1, !rtl);
}

void fit_pan_end(fit_view_t *v, float pw, float ph, int rtl)
{
	pan_to(v, pw, ph, 0, rtl);
}

/* ---- vertical strip ---------------------------------------------------------------------- */

float strip_scale(int page_w, int screen_w, float zoom)
{
	if (page_w <= 0)
		return 1.0f;
	return fit_clamp_zoom(zoom) * (float)screen_w / (float)page_w;
}

float strip_layout(const int *w, const int *h, int n, int screen_w, float zoom, int gap,
                   strip_item_t *out)
{
	float y = 0.0f;
	int i;

	for (i = 0; i < n; i++) {
		out[i].top = y;
		out[i].height = (w[i] > 0 && h[i] > 0) ? (float)h[i] * strip_scale(w[i], screen_w, zoom)
		                                       : 0.0f;
		y += out[i].height;
		if (i + 1 < n)
			y += (float)gap;
	}
	return y;
}

float strip_clamp_scroll(float total_height, int screen_h, float scroll)
{
	float max = total_height - (float)screen_h;
	if (max < 0.0f)
		max = 0.0f;
	return scroll < 0.0f ? 0.0f : (scroll > max ? max : scroll);
}

/* First page whose bottom edge is below y (n if none). Bottoms never decrease. */
static int first_bottom_after(const strip_item_t *it, int n, float y)
{
	int lo = 0, hi = n;
	while (lo < hi) {
		int mid = lo + (hi - lo) / 2;
		if (it[mid].top + it[mid].height > y)
			hi = mid;
		else
			lo = mid + 1;
	}
	return lo;
}

int strip_visible(const strip_item_t *it, int n, float scroll, int screen_h, int *first,
                  int *last)
{
	float end = scroll + (float)screen_h;
	int i, count = 0;

	*first = *last = -1;
	for (i = first_bottom_after(it, n, scroll); i < n && it[i].top < end; i++) {
		if (it[i].height <= 0.0f)
			continue;
		if (*first < 0)
			*first = i;
		*last = i;
		count++;
	}
	return count;
}

int strip_page_at(const strip_item_t *it, int n, float y)
{
	int lo = 0, hi = n - 1, ans = 0;

	if (n <= 0)
		return -1;
	while (lo <= hi) { /* last page whose top <= y */
		int mid = lo + (hi - lo) / 2;
		if (it[mid].top <= y) {
			ans = mid;
			lo = mid + 1;
		} else {
			hi = mid - 1;
		}
	}
	return ans;
}
