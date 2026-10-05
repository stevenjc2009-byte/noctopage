/* reader_draw.c - drawing for the comic reader: pages on black, placeholders while a page
 * decodes, the dim layer and the overlay (title bar, page counter, page slider). */
#include "screens/reader_priv.h"

#include "ui/draw.h"
#include "ui/font.h"
#include "ui/theme.h"

#include <stdio.h>
#include <string.h>
#include <vita2d.h>

#define SLIDER_X0 70
#define SLIDER_X1 890
#define SLIDER_Y (SCREEN_H - OVERLAY_BAR_H + 26)
#define BAR_COLOUR RGBA8(0, 0, 0, 224)

static const char *const MODE_NAMES[RM_COUNT] = {"Left to right", "Right to left", "Vertical"};
static const char *const FIT_NAMES[] = {"Fit width", "Fit height", "Fit smart", "Original"};

const char *reader_mode_name(void) { return MODE_NAMES[R.mode]; }
const char *reader_fit_name(void) { return FIT_NAMES[R.fit]; }

/* The sharpest uploaded copy of a page. */
static const rtex_t *best(int page)
{
	const rtex_t *t = NULL;
	if (reader_box_w() != BOX_W) t = rtex_peek(page, reader_box_w(), BOX_H_MAX);
	return t ? t : rtex_peek(page, BOX_W, BOX_H_MAX);
}

static void centred(int y, unsigned colour, int px, const char *s)
{
	font_draw((SCREEN_W - font_width(px, s)) / 2, y, colour, px, s);
}

static void placeholder(int page, float y, float h)
{
	char msg[64];
	if (page == R.failed_page)
		snprintf(msg, sizeof msg, "Page %d could not be read", page + 1);
	else
		snprintf(msg, sizeof msg, "Page %d", page + 1);
	int ty = (int)(y + h / 2) - font_height(FONT_PX_TITLE) / 2;
	if (ty < 8) ty = 8;
	if (ty > SCREEN_H - 40) ty = SCREEN_H - 40;
	centred(ty, THEME_SUBFONT, FONT_PX_TITLE, msg);
}

/* One page, or two side by side at the same height (a spread); a half still decoding
 * shows its page number in its place. */
static void draw_paged(void)
{
	if (!R.known[R.page]) {
		placeholder(R.page, 0, SCREEN_H);
		return;
	}
	int p[2], n = reader_shown(p);
	float vw, vh;
	reader_view_size(&vw, &vh);
	fit_rect_t r;
	fit_place(&R.view, vw, vh, &r);
	float x = r.x;
	for (int i = 0; i < n; i++) {
		float w = n == 1 ? r.w : (float)reader_page_w(p[i]) * r.h / (float)reader_page_h(p[i]);
		const rtex_t *t = best(p[i]);
		if (t && R.known[p[i]]) {
			rtex_draw_crop(t, x, r.y, w, r.h, R.rot, reader_crop_src(p[i]));
		} else {
			char msg[32];
			snprintf(msg, sizeof msg, "Page %d", p[i] + 1);
			font_draw((int)(x + (w - (float)font_width(FONT_PX_TITLE, msg)) / 2),
			          SCREEN_H / 2 - font_height(FONT_PX_TITLE) / 2, THEME_SUBFONT, FONT_PX_TITLE, msg);
		}
		x += w;
	}
}

static void draw_strip(void)
{
	int first, last;
	if (!R.items || strip_visible(R.items, R.count, R.scroll, SCREEN_H, &first, &last) == 0)
		return;
	float w = SCREEN_W * R.strip_zoom;
	float x = (SCREEN_W - w) / 2 + R.strip_x;
	for (int p = first; p <= last; p++) {
		float y = R.items[p].top - R.scroll;
		const rtex_t *t = best(p);
		if (t)
			rtex_draw(t, x, y, w);
		else
			placeholder(p, y < 0 ? 0 : y, R.items[p].height);
	}
}

void reader_draw_pages(void)
{
	char err[160];
	vita2d_set_clear_color(THEME_BLACK);
	draw_rect(0, 0, SCREEN_W, SCREEN_H, THEME_BLACK);
	loader_status_t st = R.ld ? loader_status(R.ld, err, sizeof err) : LOADER_FAILED;
	if (st == LOADER_OPENING) {
		centred(SCREEN_H / 2 - 12, THEME_SUBFONT, FONT_PX_TITLE, "Opening\xE2\x80\xA6");
	} else if (st == LOADER_FAILED || R.count <= 0) {
		centred(SCREEN_H / 2 - 30, THEME_FONT, FONT_PX_TITLE, "This book could not be opened");
		centred(SCREEN_H / 2 + 4, THEME_SUBFONT, FONT_PX_LABEL, R.err[0] ? R.err : err);
	} else if (R.mode == RM_VERTICAL) {
		draw_strip();
	} else {
		draw_paged();
	}
	if (R.dim > 0) draw_rect(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, R.dim * 64));
}

/* ---- overlay ---------------------------------------------------------------------- */

static int slider_x(int page)
{
	float f = R.count > 1 ? (float)page / (float)(R.count - 1) : 0.0f;
	if (reader_rtl()) f = 1.0f - f;
	return SLIDER_X0 + (int)(f * (SLIDER_X1 - SLIDER_X0));
}

int reader_slider_page(int x)
{
	float f = (float)(x - SLIDER_X0) / (float)(SLIDER_X1 - SLIDER_X0);
	if (f < 0) f = 0;
	if (f > 1) f = 1;
	if (reader_rtl()) f = 1.0f - f;
	return (int)(f * (float)(R.count - 1) + 0.5f);
}

int reader_overlay_hit_slider(int x, int y)
{
	(void)x;
	return R.overlay && R.count > 1 && y >= SCREEN_H - OVERLAY_BAR_H && y < SCREEN_H - 23;
}

void reader_draw_overlay(void)
{
	if (!R.overlay) return;
	char buf[96];
	draw_rect(0, 0, SCREEN_W, OVERLAY_BAR_H, BAR_COLOUR);
	draw_rect(0, SCREEN_H - OVERLAY_BAR_H, SCREEN_W, OVERLAY_BAR_H, BAR_COLOUR);

	int p[2], n = R.seeking ? 1 : reader_shown(p);
	if (R.seeking) p[0] = R.seek_page;
	if (n == 2)
		snprintf(buf, sizeof buf, "%d–%d / %d", (p[0] < p[1] ? p[0] : p[1]) + 1,
		         (p[0] < p[1] ? p[1] : p[0]) + 1, R.count);
	else
		snprintf(buf, sizeof buf, "%d / %d", p[0] + 1, R.count);
	int cw = font_width(FONT_PX_TITLE, buf);
	font_draw(SCREEN_W - 24 - cw, 14, THEME_FONT, FONT_PX_TITLE, buf);
	/* Title, cut with an ellipsis so it never runs into the counter. */
	int room = SCREEN_W - 24 - cw - 24 - 24;
	char title[sizeof R.book.title + 4];
	snprintf(title, sizeof title, "%s", R.book.title);
	for (int n = (int)strlen(title); n > 0 && font_width(FONT_PX_TITLE, title) > room; n--) {
		while (n > 0 && ((unsigned char)title[n] & 0xC0) == 0x80) n--; /* whole UTF-8 chars */
		snprintf(title + n, sizeof title - (size_t)n, "\xE2\x80\xA6");
	}
	font_draw(24, 14, THEME_FONT, FONT_PX_TITLE, title);
	if (R.mode == RM_VERTICAL)
		snprintf(buf, sizeof buf, "%s  \xC2\xB7  Dim %d%%", reader_mode_name(), R.dim * 25);
	else
		snprintf(buf, sizeof buf, "%s  \xC2\xB7  %s  \xC2\xB7  Dim %d%%", reader_mode_name(),
		         reader_fit_name(), R.dim * 25);
	font_draw(24, 46, THEME_SUBFONT, FONT_PX_LABEL, buf);

	if (R.count > 1) {
		int kx = slider_x(R.seeking ? R.seek_page : R.page);
		draw_rect(SLIDER_X0, SLIDER_Y - 2, SLIDER_X1 - SLIDER_X0, 4, THEME_PANEL);
		if (reader_rtl())
			draw_rect(kx, SLIDER_Y - 2, SLIDER_X1 - kx, 4, THEME_SELECTOR);
		else
			draw_rect(SLIDER_X0, SLIDER_Y - 2, kx - SLIDER_X0, 4, THEME_SELECTOR);
		draw_rrect(kx - 8, SLIDER_Y - 8, 16, 16, 8, THEME_FONT);
		snprintf(buf, sizeof buf, "%d", reader_rtl() ? R.count : 1);
		font_draw(SLIDER_X0 - 12 - font_width(FONT_PX_LABEL, buf), SLIDER_Y - 10, THEME_SUBFONT,
		          FONT_PX_LABEL, buf);
		snprintf(buf, sizeof buf, "%d", reader_rtl() ? 1 : R.count);
		font_draw(SLIDER_X1 + 12, SLIDER_Y - 10, THEME_SUBFONT, FONT_PX_LABEL, buf);
	}
}
