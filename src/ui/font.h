/* font.h - text drawing and measurement (UTF-8). */
#ifndef NOCT_FONT_H
#define NOCT_FONT_H

/* Sizes used by the UI. */
#define FONT_PX_LABEL 16
#define FONT_PX_TITLE 20

/* Loads the UI fonts (MuPDF's embedded Latin + CJK faces). Call after vita2d_init().
 * Never freed: the process exits via sceKernelExitProcess. */
void font_init(void);

/* Draws UTF-8 text with its TOP-LEFT at (x, y), nominal size px (pixels). */
void font_draw(int x, int y, unsigned int color, int px, const char *utf8);

/* Width of the text in pixels at size px. */
int font_width(int px, const char *utf8);

/* Height of one text line box at size px. */
int font_height(int px);

#endif
