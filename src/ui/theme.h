/* NoctoPage night-shelf palette: teal, ink panels and warm paper text. */
#ifndef NOCT_THEME_H
#define NOCT_THEME_H

#include <vita2d.h>

#define THEME_BLACK     RGBA8(0, 0, 0, 255)
#define THEME_DARKGREY  RGBA8(17, 24, 32, 255)   /* also: selected row */
#define THEME_SELECTED  RGBA8(17, 24, 32, 255)
#define THEME_PANEL     RGBA8(28, 39, 48, 255)   /* hint bar, dialogs */
#define THEME_SELECTOR  RGBA8(78, 202, 181, 255) /* teal selection and progress */
#define THEME_BADGE     RGBA8(29, 70, 67, 255)
#define THEME_FONT      RGBA8(240, 235, 224, 255)
#define THEME_SUBFONT   RGBA8(155, 171, 176, 255)
#define THEME_COVER     RGBA8(38, 55, 64, 255)

#define THEME_SIDEBAR_W 205

/* Content background: black (OLED) or ink grey (17,24,32) for PCH-1000 / LCD
 * Slim screens. The sidebar is always black. */
unsigned int theme_bg(void);
void theme_set_dark_grey(int enable);
int theme_is_dark_grey(void);

#endif
