/* book.c - the book reader's lifecycle, input and position saving. Pages are laid out and
 * rendered by reader/bookld on its worker; drawing is in book_draw.c, the menu in
 * book_menu.c. */
#include "screens/book.h"
#include "screens/book_priv.h"

#include "core/log.h"
#include "lib/reading_clock.h"
#include "core/prefs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAVE_IDLE_MS 1500
#define SWIPE_TURN 90.0f

book_t B;
static int g_open;
static reading_clock_t g_clock;
static void reading_flush(void)
{
	if (!g_clock.milliseconds && !g_clock.pages) return;
	if (db_reading_add(B.book.id, g_clock.milliseconds, g_clock.pages) >= 0) {
		g_clock.milliseconds = 0;
		g_clock.pages = 0;
	}
}


static const uint32_t INK[BOOK_THEMES] = {0x000000, 0x5B4636, 0xC8C8C8};
static const uint32_t PAPER[BOOK_THEMES] = {0xFFFFFF, 0xF4ECD8, 0x000000};
static const char *const THEME_NAMES[BOOK_THEMES] = {"White", "Sepia", "Black"};

uint32_t book_ink(void) { return INK[B.theme]; }
uint32_t book_paper(void) { return PAPER[B.theme]; }
const char *book_theme_name(void) { return THEME_NAMES[B.theme]; }
int book_is_open(void) { return g_open; }

static long book_setting(const char *key,long def)
{
 char v[32]; return db_effective_setting(B.book.id,key,v,sizeof v)==1 ? atol(v):prefs_get_int("book",key,def);
}
static void book_setting_save(const char *key,long value)
{
 char v[32]; snprintf(v,sizeof v,"%ld",value); db_book_setting_set(B.book.id,key,v);
}
static void load_style(void)
{
	B.style.font_px = (int)book_setting("font_px", 24);
	B.style.margin_px = (int)book_setting("margin_px", 32);
	B.style.line_spacing = (float)book_setting("spacing10", 14) / 10.0f;
	B.style.font_family = (int)book_setting("family", 0) ? 1 : 0;
	bookld_style_clamp(&B.style);
	long theme = book_setting("theme", 0);
	B.theme = theme >= 0 && theme < BOOK_THEMES ? (int)theme : 0;
	long dim = prefs_get_int("reader", "dim", 0);
	B.dim = dim >= 0 && dim < BOOK_DIM_LEVELS ? (int)dim : 0;
}

void book_open(const db_book_t *b)
{
	if (g_open) book_close();
	memset(&B, 0, sizeof B);
	memset(&g_clock, 0, sizeof g_clock);
	B.book = *b;
	load_style();
	tapsm_init(&B.tap, 1);
	B.failed_page = -1;
	snprintf(B.saved_pos, sizeof B.saved_pos, "%.*s", (int)sizeof B.saved_pos - 1, b->location);
	B.bl = bookld_open(b->path, &B.style, b->location);
	g_open = 1;
	log_printf("book: open %s at \"%s\"", b->path, b->location);
}

static void save(int force)
{
	if (!B.pos[0] || strcmp(B.pos, B.saved_pos) == 0) return;
	if (!force && B.now - B.changed_at < SAVE_IDLE_MS) return;
	double progress = B.count > 1 ? (double)B.page / (double)(B.count - 1) : 1.0;
	if (db_set_progress(B.book.id, B.page, B.count, B.pos, progress) < 0)
		log_printf("book: save: %s", db_last_error());
	else snprintf(B.saved_pos, sizeof B.saved_pos, "%s", B.pos);
}

void book_close(void)
{
	if (!g_open) return;
	reading_flush();
	save(1);
	book_menu_close();
	book_tex_clear();
	if (B.bl) bookld_close(B.bl);
	B.bl = NULL;
	g_open = 0;
	log_printf("book: closed at page %d \"%s\"", B.page, B.saved_pos);
}

void book_goto(int page)
{
	if (B.count <= 0) return;
	if (page < 0) page = 0;
	if (page >= B.count) page = B.count - 1;
	if (page != B.page) {
		B.changed_at = B.now;
		B.pos[0] = '\0'; /* known again once the page is rendered */
	}
	B.page = page;
}

void book_restyle(void)
{
	bookld_style_clamp(&B.style);
	book_setting_save("font_px", B.style.font_px);
	book_setting_save("margin_px", B.style.margin_px);
	book_setting_save("spacing10", (long)(B.style.line_spacing * 10.0f + 0.5f));
	book_setting_save("family", B.style.font_family);
	bookld_set_style(B.bl, &B.style, B.page);
	log_printf("book: style %d px, margin %d, spacing %.1f, family %d", B.style.font_px,
	           B.style.margin_px, (double)B.style.line_spacing, B.style.font_family);
}

/* A new layout generation (first open or after a restyle): its anchor is our place. */
static void follow_layout(void)
{
	bookld_info(B.bl, &B.info);
	if (B.info.gen != B.gen && B.info.gen > 0) {
		B.gen = B.info.gen;
		B.count = B.info.count;
		B.page = -1;
		book_tex_clear();
		book_goto(B.info.anchor);
		log_printf("book: layout %d, %d pages, at %d", B.gen, B.count, B.page);
	}
	if (B.info.jump_serial != B.jump_serial) {
		B.jump_serial = B.info.jump_serial;
		if (B.info.jump_page >= 0) book_goto(B.info.jump_page);
	}
	if (B.info.find_serial != B.find_serial) {
		B.find_serial = B.info.find_serial;
		if (B.info.find_page >= 0)
			book_goto(B.info.find_page);
		else
			log_printf("book: text not found");
	}
}

static void step(int dir)
{
	if (dir > 0 && B.page == B.count - 1) { book_menu_open(); return; }
	book_goto(B.page + dir);
}

static void buttons(const input_t *in)
{
	if (in->pressed & IN_TRIANGLE) B.overlay = !B.overlay;
	if (in->pressed & IN_SELECT) {
		B.dim = (B.dim + 1) % BOOK_DIM_LEVELS;
		prefs_set_int("reader", "dim", B.dim);
	}
	if (in->pressed & IN_SQUARE) {
		B.theme = (B.theme + 1) % BOOK_THEMES;
		book_setting_save("theme", B.theme);
	}
	if ((in->pressed & IN_CONFIRM) && B.overlay) book_menu_open();
	if (in->pressed & (IN_RIGHT | IN_R | IN_DOWN)) step(1);
	if (in->pressed & (IN_LEFT | IN_L | IN_UP)) step(-1);
}

static void touch(const input_t *in)
{
	if (B.seeking) {
		if (in->touch_count > 0) {
			B.seek_page = book_slider_page(in->touch[0].x);
			return;
		}
		B.seeking = 0;
		tapsm_init(&B.tap, 1);
		book_goto(B.seek_page);
		return;
	}
	if (in->touch_began && book_slider_hit(in->began_x, in->began_y)) {
		B.seeking = 1;
		B.seek_page = book_slider_page(in->began_x);
		return;
	}
	if (in->touch_began) B.swipe_dx = 0;
	gesture_t ev[TAPSM_MAX_EVENTS];
	int n = tapsm_update(&B.tap, in->touch_count, in->touch_count ? in->touch[0].x : 0,
	                     in->touch_count ? in->touch[0].y : 0, 0, 0, (uint32_t)in->now_ms, ev);
	for (int i = 0; i < n; i++) {
		if (ev[i].type == GESTURE_DRAG) B.swipe_dx += (float)ev[i].dx;
		if (ev[i].type != GESTURE_TAP) continue;
		if (B.overlay) {
			if (ev[i].x >= 902 && ev[i].y >= 8 && ev[i].y < 56) { book_menu_open(); B.swipe_dx = 0; return; }
			if (ev[i].y >= 80 && ev[i].y < 544 - 80) B.overlay = 0;
			continue;
		}
		zone_action_t z = zone_for_tap(960, 544, ev[i].x, ev[i].y, 0, 0);
		if (z == ZONE_MENU)
			B.overlay = 1;
		else
			step(z == ZONE_NEXT ? 1 : -1);
	}
	if (in->touch_count == 0) {
		if (B.swipe_dx <= -SWIPE_TURN) step(1);
		if (B.swipe_dx >= SWIPE_TURN) step(-1);
		B.swipe_dx = 0;
	}
}

/* Ask for the page on screen, then the next one and the previous one. */
static void wish(void)
{
	int pages[3] = {B.page, B.page + 1, B.page - 1};
	bookld_want(B.bl, B.gen, pages, 3, book_ink(), book_paper());
}

void book_update(const input_t *in)
{
	if (!g_open) return;
	B.now = in->now_ms;
	reading_clock_tick(&g_clock, in->now_ms, B.page, (B.count > 0 && B.failed_page != B.page) && !book_menu_is_open(),
	                   in->pressed || in->touch_count || in->lx < 108 || in->lx > 148 ||
	                   in->ly < 108 || in->ly > 148);
	if (g_clock.milliseconds >= 30000 || g_clock.pages >= 10) reading_flush();
	if (B.bl) follow_layout();
	if (book_menu_is_open()) {
		book_menu_update(in);
	} else if (in->pressed & IN_BACK) {
		book_close();
		return;
	} else if (B.count > 0) {
		buttons(in);
		touch(in);
	}
	if (B.count <= 0) return;
	char err[160];
	int failed = bookld_take_error(B.bl, err, sizeof err);
	if (failed >= 0) {
		log_printf("book: page %d: %s", failed, err);
		B.failed_page = failed;
	}
	wish();
	book_tex_fetch(B.page);
	book_tex_fetch(B.page + 1);
	book_tex_fetch(B.page - 1);
	save(0);
}

void book_draw(void)
{
	if (!g_open) return;
	book_draw_page();
	book_draw_overlay();
	book_menu_draw();
}

int book_hints(hint_t *out, int max)
{
	if (!g_open) return 0;
	if (book_menu_is_open()) return book_menu_hints(out, max);
	if (!B.overlay) return 0;
	static const hint_t H[] = {
		{GLYPH_BACK, "Close"},   {GLYPH_LR, "Turn page"}, {GLYPH_CONFIRM, "Menu"},
		{GLYPH_SQUARE, "Theme"}, {GLYPH_SELECT, "Dim"},   {GLYPH_TRIANGLE, "Hide"},
	};
	int n = 6 < max ? 6 : max;
	memcpy(out, H, sizeof *H * (size_t)n);
	return n;
}
