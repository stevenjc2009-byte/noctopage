/* draw.h - primitives and code-drawn button glyphs. */
#ifndef NOCT_DRAW_H
#define NOCT_DRAW_H

#include <stdint.h>

typedef enum {
	GLYPH_CROSS,
	GLYPH_CIRCLE,
	GLYPH_SQUARE,
	GLYPH_TRIANGLE,
	GLYPH_L,
	GLYPH_R,
	GLYPH_LR,     /* L and R side by side ("L\R" in NOBORU) */
	GLYPH_START,
	GLYPH_SELECT,
	GLYPH_DPAD,
	GLYPH_CONFIRM, /* Cross or Circle, whichever the system enter button is */
	GLYPH_BACK,    /* the other one */
} glyph_t;

#define GLYPH_SIZE 18 /* height of every glyph */

void draw_rect(int x, int y, int w, int h, unsigned int color);
void draw_rrect(int x, int y, int w, int h, int r, unsigned int color);
/* Vertical gradient, colours interpolated per channel including alpha. */
void draw_gradient_v(int x, int y, int w, int h, unsigned int top, unsigned int bottom);
void draw_border(int x, int y, int w, int h, int thickness, unsigned int color);
/* Pulsing selector: 3 px royal-blue border plus a white overlay border whose
 * alpha follows 100*|sin(t/500)|. Drawn inside the given rectangle. */
void draw_selector(int x, int y, int w, int h, uint64_t now_ms);

/* Draws the glyph with its top-left at (x, y); returns its advance width. */
int draw_glyph(glyph_t g, int x, int y);
int draw_glyph_width(glyph_t g);

#endif
