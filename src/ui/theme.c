#include "ui/theme.h"

static int g_dark_grey;

unsigned int theme_bg(void)
{
	return g_dark_grey ? THEME_DARKGREY : THEME_BLACK;
}

void theme_set_dark_grey(int enable)
{
	g_dark_grey = enable != 0;
}

int theme_is_dark_grey(void)
{
	return g_dark_grey;
}
