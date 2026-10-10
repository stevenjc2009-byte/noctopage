/* book_priv.h - state shared by the book reader's files (screens/book*.c): reflowable
 * books (EPUB, MOBI, FB2, TXT, HTML) laid out to the screen by reader/bookld. */
#ifndef NOCT_BOOK_PRIV_H
#define NOCT_BOOK_PRIV_H

#include "lib/db.h"
#include "reader/bookld.h"
#include "reader/zones.h"
#include "ui/hintbar.h"
#include "ui/input.h"

#include <stdint.h>

#define BOOK_THEMES 3 /* white, sepia, black */
#define BOOK_DIM_LEVELS 4

typedef struct {
	db_book_t book;
	bookld_t *bl;
	bookld_info_t info; /* refreshed every frame */
	int gen, count, page;
	bookld_style_t style;
	int theme, dim, overlay;
	uint64_t now, changed_at;
	char pos[MDOC_POS_MAX]; /* position of the page on screen, once rendered */
	char saved_pos[MDOC_POS_MAX];
	int jump_serial;
	int find_serial;
	tapsm_t tap;
	float swipe_dx;
	int seeking, seek_page;
	int failed_page;
} book_t;

extern book_t B;

uint32_t book_ink(void);
uint32_t book_paper(void);
void book_goto(int page);
void book_restyle(void); /* clamp, save to prefs, re-lay out keeping the page */
const char *book_theme_name(void);

/* book_draw.c */
void book_draw_page(void);
void book_draw_overlay(void);
int book_slider_page(int x);
int book_slider_hit(int x, int y);
/* Texture of (gen, page, colours) if uploaded; book_tex_fetch uploads (update step only). */
void *book_tex_fetch(int page);
void *book_tex_peek(int page);
void book_tex_clear(void);

/* book_menu.c */
void book_menu_open(void);
int book_menu_is_open(void);
void book_menu_update(const input_t *in);
void book_menu_draw(void);
int book_menu_hints(hint_t *out, int max);
void book_menu_close(void);

#endif
