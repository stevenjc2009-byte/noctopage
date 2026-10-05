#include "ui/hintbar.h"

#include "ui/font.h"
#include "ui/theme.h"

#include <string.h>

static hint_t g_hints[HINTBAR_MAX];
static int g_count;
static float g_y = HINTBAR_H; /* slide offset: 0 = shown, 23 = hidden below the screen */

void hintbar_set(const hint_t *hints, int n)
{
	if (n > HINTBAR_MAX) n = HINTBAR_MAX;
	if (n < 0) n = 0;

	int same = (n == g_count);
	for (int i = 0; same && i < n; i++)
		same = hints[i].glyph == g_hints[i].glyph &&
		       strcmp(hints[i].label ? hints[i].label : "",
		              g_hints[i].label ? g_hints[i].label : "") == 0;
	if (same)
		return;

	for (int i = 0; i < n; i++)
		g_hints[i] = hints[i];
	g_count = n;
	g_y = HINTBAR_H; /* restart the slide-in */
}

void hintbar_draw(void)
{
	g_y += (0.0f - g_y) / 4.0f;
	if (g_y < 0.05f)
		g_y = 0.0f;
	int oy = (int)(g_y + 0.5f);

	/* Two faint shadow strips above the panel, then the panel itself. */
	draw_rect(0, HINTBAR_Y + oy, 960, 2, RGBA8(0, 0, 0, 32));
	draw_rect(0, HINTBAR_Y + oy + 1, 960, 2, RGBA8(0, 0, 0, 32));
	draw_rect(0, HINTBAR_Y + 3 + oy, 960, HINTBAR_H - 3, THEME_PANEL);

	int x = 20;
	for (int i = 0; i < g_count; i++) {
		x += draw_glyph(g_hints[i].glyph, x, HINTBAR_Y + 4 + oy) + 5;
		if (g_hints[i].label) {
			font_draw(x, HINTBAR_Y + 2 + oy, THEME_FONT, FONT_PX_LABEL, g_hints[i].label);
			x += font_width(FONT_PX_LABEL, g_hints[i].label);
		}
		x += 10;
	}
}
