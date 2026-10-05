#include "screens/screens.h"

static void enter(void) {}
static void leave(void) {}
static void update(const input_t *in) { (void)in; }
static void draw(void) { screen_placeholder_draw("My Books"); }

static int hints(hint_t *out, int max)
{
	static const hint_t H[] = {
		{GLYPH_LR, "Section"},
		{GLYPH_DPAD, "Choose"},
		{GLYPH_CONFIRM, "Open"},
		{GLYPH_TRIANGLE, "Menu"},
		{GLYPH_SQUARE, "Sort"},
	};
	return screen_copy_hints(H, (int)(sizeof H / sizeof H[0]), out, max);
}

const screen_t screen_mybooks = {"My Books", enter, leave, update, draw, hints};
