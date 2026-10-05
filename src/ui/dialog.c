#include "ui/dialog.h"

#include "ui/draw.h"
#include "ui/font.h"
#include "ui/theme.h"

#define DLG_W 420
#define DLG_H 150

static int g_open;
static const char *g_title = "";
static const char *g_message = "";

void dialog_open_confirm(const char *title, const char *message)
{
	g_title = title ? title : "";
	g_message = message ? message : "";
	g_open = 1;
}

int dialog_is_open(void)
{
	return g_open;
}

dialog_result_t dialog_update(const input_t *in)
{
	if (!g_open)
		return DIALOG_PENDING;
	if (in->pressed & IN_CONFIRM) {
		g_open = 0;
		return DIALOG_YES;
	}
	if (in->pressed & IN_BACK) {
		g_open = 0;
		return DIALOG_NO;
	}
	return DIALOG_PENDING;
}

void dialog_draw(void)
{
	if (!g_open)
		return;
	draw_rect(0, 0, 960, 544, RGBA8(0, 0, 0, 150));

	int x = (960 - DLG_W) / 2, y = (544 - DLG_H) / 2;
	draw_rrect(x, y, DLG_W, DLG_H, 6, THEME_PANEL);
	font_draw(x + 24, y + 20, THEME_FONT, FONT_PX_TITLE, g_title);
	font_draw(x + 24, y + 56, THEME_SUBFONT, FONT_PX_LABEL, g_message);

	int bx = x + 24, by = y + DLG_H - 40;
	bx += draw_glyph(GLYPH_CONFIRM, bx, by) + 6;
	font_draw(bx, by - 1, THEME_FONT, FONT_PX_LABEL, "Yes");
	bx += font_width(FONT_PX_LABEL, "Yes") + 30;
	bx += draw_glyph(GLYPH_BACK, bx, by) + 6;
	font_draw(bx, by - 1, THEME_FONT, FONT_PX_LABEL, "No");
}

int dialog_hints(hint_t *out, int max)
{
	static const hint_t H[] = {
		{GLYPH_CONFIRM, "Yes"},
		{GLYPH_BACK, "No"},
	};
	int n = (int)(sizeof H / sizeof H[0]);
	if (n > max) n = max;
	for (int i = 0; i < n; i++)
		out[i] = H[i];
	return n;
}
