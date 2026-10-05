/* My Books: every book found on the memory cards, as a NOBORU cover grid. */
#include "screens/screens.h"

#include "core/log.h"
#include "core/prefs.h"
#include "lib/covers.h"
#include "lib/db.h"
#include "lib/formats.h"
#include "lib/library.h"
#include "ui/draw.h"
#include "ui/font.h"
#include "ui/grid.h"
#include "ui/menu.h"
#include "ui/theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BOOKS 10000
#define RELOAD_MS 2000 /* while a scan runs, reload the list at most this often */

static db_book_t *g_books;
static char (*g_badge)[FORMATS_BADGE_MAX];
static int g_count;
static grid_t g_grid;
static int g_grid_ready;
static int g_sort;
static unsigned g_seen_gen = ~0u;
static uint64_t g_last_reload, g_now;
static uint64_t g_continue_id; /* most recently read book, 0 = none */
static int g_menu_for = -1;    /* book index the open menu belongs to */

static const char *const SORT_NAMES[] = {"Title", "Last read", "Added"};
static const char *const SORT_HINTS[] = {"Sort: Title", "Sort: Last read", "Sort: Added"};

enum { M_FINISH, M_COVER, M_RESCAN };
static const char *const MENU_UNREAD[] = {"Mark as finished", "Refresh cover", "Rescan library"};
static const char *const MENU_READ[] = {"Mark as unread", "Refresh cover", "Rescan library"};

static void reload(void)
{
	if (!g_books) {
		g_books = malloc(sizeof *g_books * MAX_BOOKS);
		g_badge = malloc(sizeof *g_badge * MAX_BOOKS);
		if (!g_books || !g_badge) {
			log_printf("mybooks: out of memory for the list");
			free(g_books);
			free(g_badge);
			g_books = NULL;
			g_badge = NULL;
			return;
		}
	}
	uint64_t keep = g_count > 0 && g_grid.sel < g_count ? g_books[g_grid.sel].id : 0;
	int n = db_books_list(g_sort, g_books, MAX_BOOKS);
	if (n < 0) {
		log_printf("mybooks: list: %s", db_last_error());
		n = 0;
	}
	g_count = n;
	int sel = -1;
	for (int i = 0; i < n; i++) {
		if (g_books[i].kind == DB_KIND_FOLDER)
			strcpy(g_badge[i], "DIR");
		else
			formats_badge_r(g_books[i].path, g_badge[i], FORMATS_BADGE_MAX);
		if (keep && g_books[i].id == keep) sel = i;
	}
	db_book_t *c = malloc(sizeof *c);
	g_continue_id = c && db_continue_reading(c) == 1 ? c->id : 0;
	free(c);
	grid_set_count(&g_grid, n);
	if (sel >= 0) g_grid.sel = sel;
	g_seen_gen = library_generation();
	g_last_reload = g_now;
}

static void item(void *user, int i, grid_item_t *out)
{
	(void)user;
	const db_book_t *b = &g_books[i];
	out->title = b->title;
	out->badge = b->id == g_continue_id ? "READING" : g_badge[i];
	out->cover = covers_get(b->id, b->path, b->kind);
	out->finished = b->finished;
	out->progress = b->last_read > 0 && !b->finished ? (float)b->progress : -1.0f;
}

static void enter(void)
{
	g_sort = (int)prefs_get_int("mybooks", "sort", DB_SORT_TITLE);
	if (g_sort < 0 || g_sort > 2) g_sort = DB_SORT_TITLE;
	if (!g_grid_ready) {
		grid_init(&g_grid);
		g_grid_ready = 1;
	}
	reload();
}

static void leave(void) { g_menu_for = -1; }

static void open_menu(void)
{
	if (g_grid.sel >= g_count) return;
	g_menu_for = g_grid.sel;
	const db_book_t *b = &g_books[g_menu_for];
	menu_open(b->title, b->finished ? MENU_READ : MENU_UNREAD, 3);
}

static void menu_pick(int pick)
{
	int i = g_menu_for;
	if (menu_is_open()) return;
	g_menu_for = -1;
	if (pick < 0 || i < 0 || i >= g_count) return;
	db_book_t *b = &g_books[i];
	if (pick == M_FINISH) {
		db_set_finished(b->id, !b->finished);
		reload();
	} else if (pick == M_COVER) {
		covers_forget(b->id);
	} else if (pick == M_RESCAN) {
		library_scan_start();
	}
}

static void update(const input_t *in)
{
	g_now = in->now_ms;
	if (menu_is_open()) {
		menu_pick(menu_update(in));
		return;
	}
	unsigned gen = library_generation();
	if (gen != g_seen_gen && (!library_scan_running() || g_now - g_last_reload >= RELOAD_MS))
		reload();

	if (in->pressed & IN_SQUARE) {
		g_sort = (g_sort + 1) % 3;
		prefs_set_int("mybooks", "sort", g_sort);
		log_printf("mybooks: sort %s", SORT_NAMES[g_sort]);
		reload();
		g_grid.sel = 0;
		g_grid.follow = 1;
	}
	if (in->pressed & IN_TRIANGLE) {
		open_menu();
		return;
	}
	if ((in->pressed & IN_SELECT) && g_continue_id) {
		for (int i = 0; i < g_count; i++)
			if (g_books[i].id == g_continue_id) app_open_book(&g_books[i]);
		return;
	}
	if (grid_update(&g_grid, in) == GRID_OPEN && g_grid.sel < g_count)
		app_open_book(&g_books[g_grid.sel]);
}

static void draw_status(void)
{
	if (!library_scan_running()) return;
	char msg[64];
	snprintf(msg, sizeof msg, "Scanning\xE2\x80\xA6 %d found", library_scan_found());
	int w = font_width(FONT_PX_LABEL, msg) + 20;
	draw_rrect(950 - w, 487, w, 28, 4, THEME_PANEL);
	font_draw(960 - w, 487 + (28 - font_height(FONT_PX_LABEL)) / 2, THEME_FONT, FONT_PX_LABEL, msg);
}

static void draw(void)
{
	if (g_count == 0) {
		const char *a = library_scan_running() ? "Looking for books\xE2\x80\xA6" : "No books yet";
		const char *b = "Put comics and books anywhere on the memory card, e.g. ux0:books";
		font_draw(582 - font_width(FONT_PX_TITLE, a) / 2, 230, THEME_FONT, FONT_PX_TITLE, a);
		font_draw(582 - font_width(FONT_PX_LABEL, b) / 2, 266, THEME_SUBFONT, FONT_PX_LABEL, b);
	} else {
		grid_draw(&g_grid, item, NULL, g_now);
	}
	draw_status();
	menu_draw(g_now);
}

static int hints(hint_t *out, int max)
{
	if (menu_is_open()) return menu_hints(out, max);
	hint_t h[] = {
		{GLYPH_LR, "Section"},
		{GLYPH_DPAD, "Choose"},
		{GLYPH_CONFIRM, "Open"},
		{GLYPH_TRIANGLE, "Menu"},
		{GLYPH_SQUARE, SORT_HINTS[g_sort]},
		{GLYPH_SELECT, "Continue"},
	};
	int n = (int)(sizeof h / sizeof h[0]);
	if (!g_continue_id) n--;
	if (g_count == 0) {
		h[1] = h[4];
		n = 2;
	}
	return screen_copy_hints(h, n, out, max);
}

const screen_t screen_mybooks = {"My Books", enter, leave, update, draw, hints};
