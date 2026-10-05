#include "screens/screens.h"

#include "ui/font.h"
#include "ui/theme.h"

void screen_placeholder_draw(const char *title)
{
	font_draw(225, 22, THEME_FONT, FONT_PX_TITLE, title);

	const char *msg = "Nothing here yet";
	int w = font_width(FONT_PX_LABEL, msg);
	/* Centre of the content area (x 205..960) is 582, as in NOBORU's empty state. */
	font_draw(582 - w / 2, 260, THEME_SUBFONT, FONT_PX_LABEL, msg);
}

int screen_copy_hints(const hint_t *table, int n, hint_t *out, int max)
{
	if (n > max) n = max;
	for (int i = 0; i < n; i++)
		out[i] = table[i];
	return n;
}
