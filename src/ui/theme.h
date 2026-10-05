/* theme.h - NOBORU "Dark" palette (docs/PLAN.md UI contract). */
#ifndef NOCT_THEME_H
#define NOCT_THEME_H

#include <vita2d.h>

#define THEME_BLACK     RGBA8(0, 0, 0, 255)
#define THEME_DARKGREY  RGBA8(24, 24, 24, 255)   /* also: selected row */
#define THEME_SELECTED  RGBA8(24, 24, 24, 255)
#define THEME_PANEL     RGBA8(72, 72, 72, 255)   /* hint bar, dialogs */
#define THEME_SELECTOR  RGBA8(65, 105, 226, 255) /* pulsing border, accent royal blue */
#define THEME_BADGE     RGBA8(137, 30, 43, 255)
#define THEME_FONT      RGBA8(255, 255, 255, 255)
#define THEME_SUBFONT   RGBA8(128, 128, 128, 255)
#define THEME_COVER     RGBA8(101, 115, 146, 255)

#define THEME_SIDEBAR_W 205

/* Content background: black (OLED) or dark grey (24,24,24) for PCH-1000 / LCD
 * Slim screens. The sidebar is always black. */
unsigned int theme_bg(void);
void theme_set_dark_grey(int enable);
int theme_is_dark_grey(void);

#endif
