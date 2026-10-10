#include "app.h"
#include "screens/library_tools.h"
#include "screens/utilities.h"
#include "screens/privacy.h"
#include "screens/updater.h"

#include "core/log.h"
#include "lib/covers.h"
#include "screens/book.h"
#include "screens/reader.h"
#include "screens/screens.h"
#include "ui/dialog.h"
#include "ui/menu.h"
#include "ui/listview.h"
#include "ui/sidebar.h"
#include "ui/theme.h"
#include "ui/draw.h"
#include "ui/font.h"
#include "core/strutil.h"

static const screen_t *const SCREENS[SEC_COUNT] = {
	&screen_mybooks, &screen_private, &screen_catalog, &screen_history, &screen_downloads, &screen_settings,
};

static section_t g_section = SEC_MYBOOKS;
static int g_quit, g_next_pending;
static db_book_t g_next;
static int g_private_viewer;
static uint64_t g_now_ms, g_message_until;
static char g_message[128];
void app_message(const char *message) { str_copy(g_message,sizeof g_message,message); g_message_until=g_now_ms+5000; }

static void switch_section(section_t s)
{
	if (s == g_section)
		return;
	SCREENS[g_section]->leave();
 if(g_section==SEC_PRIVATE) privacy_lock();
 if(s==SEC_PRIVATE && !privacy_unlock(g_now_ms)) s=SEC_MYBOOKS;
	g_section = s;
	SCREENS[g_section]->enter();
	log_printf("section: %s", SCREENS[g_section]->title);
}

void app_init(void)
{
	g_section = SEC_MYBOOKS;
	SCREENS[g_section]->enter();
}

void app_open_book(const db_book_t *b)
{
	if(b->private_book && !privacy_unlock(g_now_ms)) return;
 g_private_viewer=b->private_book;
	log_printf("open: %s (kind %d)", b->path, b->kind);
	SCREENS[g_section]->leave();
	covers_pause(1);
	if (b->kind == DB_KIND_REFLOW)
		book_open(b);
	else
		reader_open(b);
}

void app_open_stream(const char *title, const char *tmpl, int count, const char *user,
                     const char *pass)
{
	log_printf("open: stream %s (%d pages)", tmpl ? tmpl : "", count);
	SCREENS[g_section]->leave();
	covers_pause(1);
	reader_open_stream(title, tmpl, count, user, pass);
}

void app_request_next(uint64_t id)
{
	g_next_pending = db_next_volume(id, &g_next) == 1;
}

/* A book or comic fills the screen and owns the input until it closes. */
static int viewer_open(void) { return reader_is_open() || book_is_open(); }

static int viewer_hints(hint_t *out, int max)
{
	return book_is_open() ? book_hints(out, max) : reader_hints(out, max);
}

void app_shutdown(void)
{
	utilities_close();
	reader_close();
	book_close();
}

int app_quit_requested(void)
{
	return g_quit || updater_install_requested();
}

void app_update(const input_t *in)
{
	g_now_ms = in->now_ms;
 updater_tick();
 if(privacy_tick(g_now_ms,in->pressed || in->touch_count)) {
  if(g_private_viewer) { reader_close(); book_close(); covers_pause(0); g_private_viewer=0; }
  if(g_section==SEC_PRIVATE) switch_section(SEC_MYBOOKS);
 }
 if(g_private_viewer && !viewer_open()) { g_private_viewer=0; if(g_section!=SEC_PRIVATE) privacy_lock(); }

	const screen_t *scr = SCREENS[g_section];
	hint_t hints[HINTBAR_MAX];
	int n;

	if (dialog_is_open()) {
		if (dialog_update(in) == DIALOG_YES) {
			log_printf("quit confirmed");
			g_quit = 1;
		}
		n = dialog_hints(hints, HINTBAR_MAX);
		hintbar_set(hints, n);
		return;
	}

	if (viewer_open() && !(in->pressed & IN_START)) {
		if (book_is_open())
			book_update(in);
		else
			reader_update(in);
		if (g_next_pending) {
		g_next_pending = 0;
		reader_close();
		book_close();
		app_open_book(&g_next);
		}
		if (!viewer_open()) { covers_pause(0); SCREENS[g_section]->enter(); } /* shows the new progress */
		n = viewer_hints(hints, HINTBAR_MAX);
		hintbar_set(hints, n);
		return;
	}

	if (in->pressed & IN_START) {
		dialog_open_confirm("Quit Noctopage?", "Your place in every book is saved.");
		n = dialog_hints(hints, HINTBAR_MAX);
		hintbar_set(hints, n);
		return;
	}

	/* An open menu owns L/R and touch until it closes. */
	if (menu_is_open() || listview_is_open() || library_tools_busy() || utilities_busy() || updater_busy()) {
	} else if (in->pressed & IN_L)
		switch_section((section_t)((g_section + SEC_COUNT - 1) % SEC_COUNT));
	else if (in->pressed & IN_R)
		switch_section((section_t)((g_section + 1) % SEC_COUNT));
	else if (in->touch_began) {
		int hit = sidebar_hit(in->began_x, in->began_y);
		if (hit >= 0)
			switch_section((section_t)hit);
	}

	scr = SCREENS[g_section];
	scr->update(in);
	n = scr->hints(hints, HINTBAR_MAX);
	hintbar_set(hints, n);
}

void app_draw(void)
{
	if (book_is_open()) {
		book_draw();
	} else if (reader_is_open()) {
		reader_draw();
	} else {
		sidebar_draw(g_section, g_now_ms);
		SCREENS[g_section]->draw();
	}
	if(g_now_ms<g_message_until) { draw_rrect(160,450,640,42,6,THEME_PANEL); font_draw(180,460,THEME_FONT,FONT_PX_LABEL,g_message); }
	dialog_draw(); /* dims content + sidebar, but not the hint bar below */
	/* A reader page fills the screen: the bar shows only with the reader's overlay. */
	hint_t h[HINTBAR_MAX];
	if (!viewer_open() || dialog_is_open() || viewer_hints(h, HINTBAR_MAX) > 0)
		hintbar_draw();
}

void app_private_moved(int is_private)
{
 g_private_viewer=is_private;
 if(is_private && !privacy_unlock(g_now_ms)) { reader_close(); book_close(); covers_pause(0); }
}
