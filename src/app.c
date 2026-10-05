#include "app.h"

#include "core/log.h"
#include "screens/book.h"
#include "screens/reader.h"
#include "screens/screens.h"
#include "ui/dialog.h"
#include "ui/menu.h"
#include "ui/sidebar.h"
#include "ui/theme.h"

static const screen_t *const SCREENS[SEC_COUNT] = {
	&screen_mybooks, &screen_catalog, &screen_history, &screen_downloads, &screen_settings,
};

static section_t g_section = SEC_MYBOOKS;
static int g_quit;
static uint64_t g_now_ms;

static void switch_section(section_t s)
{
	if (s == g_section)
		return;
	SCREENS[g_section]->leave();
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
	log_printf("open: %s (kind %d)", b->path, b->kind);
	SCREENS[g_section]->leave();
	if (b->kind == DB_KIND_REFLOW)
		book_open(b);
	else
		reader_open(b);
}

/* A book or comic fills the screen and owns the input until it closes. */
static int viewer_open(void) { return reader_is_open() || book_is_open(); }

static int viewer_hints(hint_t *out, int max)
{
	return book_is_open() ? book_hints(out, max) : reader_hints(out, max);
}

void app_shutdown(void)
{
	reader_close();
	book_close();
}

int app_quit_requested(void)
{
	return g_quit;
}

void app_update(const input_t *in)
{
	g_now_ms = in->now_ms;
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
		if (!viewer_open()) SCREENS[g_section]->enter(); /* shows the new progress */
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
	if (menu_is_open()) {
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
		SCREENS[g_section]->draw();
		sidebar_draw(g_section, g_now_ms);
	}
	dialog_draw(); /* dims content + sidebar, but not the hint bar below */
	/* A reader page fills the screen: the bar shows only with the reader's overlay. */
	hint_t h[HINTBAR_MAX];
	if (!viewer_open() || dialog_is_open() || viewer_hints(h, HINTBAR_MAX) > 0)
		hintbar_draw();
}
