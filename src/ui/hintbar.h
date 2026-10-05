/* hintbar.h - bottom control-hint bar (23 px at y 521..544). */
#ifndef NOCT_HINTBAR_H
#define NOCT_HINTBAR_H

#include "ui/draw.h"

#define HINTBAR_Y      521
#define HINTBAR_H      23
#define HINTBAR_MAX    10

typedef struct {
	glyph_t glyph;
	const char *label; /* UTF-8, must outlive the call (use string literals) */
} hint_t;

/* Sets the hints shown. If the set differs from the current one the bar slides
 * in again (y += (target - y) / 4 per frame, NOBORU-style ease). */
void hintbar_set(const hint_t *hints, int n);
/* Draws the bar and advances its slide animation by one frame. */
void hintbar_draw(void);

#endif
