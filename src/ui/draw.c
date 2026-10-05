#include "ui/draw.h"

#include "ui/font.h"
#include "ui/input.h"
#include "ui/theme.h"

#include <math.h>
#include <vita2d.h>

void draw_rect(int x, int y, int w, int h, unsigned int color)
{
	vita2d_draw_rectangle((float)x, (float)y, (float)w, (float)h, color);
}

/* Rounded rectangle: centre + side strips + four corner discs. */
void draw_rrect(int x, int y, int w, int h, int r, unsigned int color)
{
	if (r * 2 > h) r = h / 2;
	if (r * 2 > w) r = w / 2;
	if (r <= 0) {
		draw_rect(x, y, w, h, color);
		return;
	}
	draw_rect(x + r, y, w - 2 * r, h, color);
	draw_rect(x, y + r, r, h - 2 * r, color);
	draw_rect(x + w - r, y + r, r, h - 2 * r, color);
	vita2d_draw_fill_circle((float)(x + r), (float)(y + r), (float)r, color);
	vita2d_draw_fill_circle((float)(x + w - r), (float)(y + r), (float)r, color);
	vita2d_draw_fill_circle((float)(x + r), (float)(y + h - r), (float)r, color);
	vita2d_draw_fill_circle((float)(x + w - r), (float)(y + h - r), (float)r, color);
}

static unsigned int lerp_color(unsigned int a, unsigned int b, int num, int den)
{
	unsigned int out = 0;
	for (int shift = 0; shift < 32; shift += 8) {
		int ca = (int)((a >> shift) & 0xFF);
		int cb = (int)((b >> shift) & 0xFF);
		out |= (unsigned int)(ca + (cb - ca) * num / den) << shift;
	}
	return out;
}

void draw_gradient_v(int x, int y, int w, int h, unsigned int top, unsigned int bottom)
{
	const int step = 3;
	for (int row = 0; row < h; row += step) {
		int hh = (row + step > h) ? h - row : step;
		draw_rect(x, y + row, w, hh, lerp_color(top, bottom, row, h > 1 ? h - 1 : 1));
	}
}

void draw_border(int x, int y, int w, int h, int t, unsigned int color)
{
	draw_rect(x, y, w, t, color);
	draw_rect(x, y + h - t, w, t, color);
	draw_rect(x, y + t, t, h - 2 * t, color);
	draw_rect(x + w - t, y + t, t, h - 2 * t, color);
}

void draw_selector(int x, int y, int w, int h, uint64_t now_ms)
{
	draw_border(x, y, w, h, 3, THEME_SELECTOR);
	int a = (int)(100.0f * fabsf(sinf((float)now_ms / 500.0f)));
	draw_border(x, y, w, h, 3, RGBA8(255, 255, 255, a));
}

/* ---- glyphs ------------------------------------------------------------------ */

#define GLYPH_RING   RGBA8(190, 190, 190, 255)
#define GLYPH_DISC   RGBA8(28, 28, 28, 255)
#define GLYPH_PILL   RGBA8(200, 200, 200, 255)
#define GLYPH_INK    RGBA8(28, 28, 28, 255)
#define COL_CROSS    RGBA8(124, 178, 232, 255)
#define COL_CIRCLE   RGBA8(255, 102, 102, 255)
#define COL_SQUARE   RGBA8(255, 105, 248, 255)
#define COL_TRIANGLE RGBA8(64, 226, 160, 255)

static void thick_line(int x0, int y0, int x1, int y1, unsigned int c)
{
	vita2d_draw_line((float)x0, (float)y0, (float)x1, (float)y1, c);
	vita2d_draw_line((float)x0 + 1, (float)y0, (float)x1 + 1, (float)y1, c);
	vita2d_draw_line((float)x0, (float)y0 + 1, (float)x1, (float)y1 + 1, c);
}

static void disc(int x, int y)
{
	float c = GLYPH_SIZE / 2.0f;
	vita2d_draw_fill_circle((float)x + c, (float)y + c, c, GLYPH_RING);
	vita2d_draw_fill_circle((float)x + c, (float)y + c, c - 1.0f, GLYPH_DISC);
}

static int pill(int x, int y, int w, const char *text, int px)
{
	draw_rrect(x, y + 2, w, GLYPH_SIZE - 4, (GLYPH_SIZE - 4) / 2, GLYPH_PILL);
	int tw = font_width(px, text);
	font_draw(x + (w - tw) / 2, y + 2 + (GLYPH_SIZE - 4 - font_height(px)) / 2, GLYPH_INK, px, text);
	return w;
}

static glyph_t resolve(glyph_t g)
{
	if (g == GLYPH_CONFIRM)
		return input_confirm_is_cross() ? GLYPH_CROSS : GLYPH_CIRCLE;
	if (g == GLYPH_BACK)
		return input_confirm_is_cross() ? GLYPH_CIRCLE : GLYPH_CROSS;
	return g;
}

int draw_glyph_width(glyph_t g)
{
	switch (resolve(g)) {
	case GLYPH_L:
	case GLYPH_R:      return 24;
	case GLYPH_LR:     return 50;
	case GLYPH_START:  return font_width(10, "START") + 12;
	case GLYPH_SELECT: return font_width(10, "SELECT") + 12;
	default:           return GLYPH_SIZE;
	}
}

int draw_glyph(glyph_t g, int x, int y)
{
	g = resolve(g);
	int cx = x + GLYPH_SIZE / 2, cy = y + GLYPH_SIZE / 2;
	switch (g) {
	case GLYPH_CROSS:
		disc(x, y);
		thick_line(cx - 4, cy - 4, cx + 4, cy + 4, COL_CROSS);
		thick_line(cx - 4, cy + 4, cx + 4, cy - 4, COL_CROSS);
		break;
	case GLYPH_CIRCLE:
		disc(x, y);
		vita2d_draw_fill_circle((float)cx, (float)cy, 5.5f, COL_CIRCLE);
		vita2d_draw_fill_circle((float)cx, (float)cy, 3.5f, GLYPH_DISC);
		break;
	case GLYPH_SQUARE:
		disc(x, y);
		draw_rect(cx - 4, cy - 4, 9, 9, COL_SQUARE);
		draw_rect(cx - 2, cy - 2, 5, 5, GLYPH_DISC);
		break;
	case GLYPH_TRIANGLE:
		disc(x, y);
		thick_line(cx, cy - 5, cx - 5, cy + 4, COL_TRIANGLE);
		thick_line(cx, cy - 5, cx + 5, cy + 4, COL_TRIANGLE);
		thick_line(cx - 5, cy + 4, cx + 5, cy + 4, COL_TRIANGLE);
		break;
	case GLYPH_L:     return pill(x, y, 24, "L", 12);
	case GLYPH_R:     return pill(x, y, 24, "R", 12);
	case GLYPH_LR:
		pill(x, y, 24, "L", 12);
		pill(x + 26, y, 24, "R", 12);
		return 50;
	case GLYPH_START:  return pill(x, y, draw_glyph_width(g), "START", 10);
	case GLYPH_SELECT: return pill(x, y, draw_glyph_width(g), "SELECT", 10);
	case GLYPH_DPAD:
		draw_rrect(x + 1, cy - 3, GLYPH_SIZE - 2, 6, 2, GLYPH_PILL);
		draw_rrect(cx - 3, y + 1, 6, GLYPH_SIZE - 2, 2, GLYPH_PILL);
		draw_rect(cx - 1, cy - 1, 3, 3, GLYPH_INK);
		break;
	default:
		break;
	}
	return GLYPH_SIZE;
}
