/* book_menu.c - the book reader's menu (Confirm while the overlay is shown): font size,
 * margins, line spacing, font, page theme, contents and bookmarks. A setting opens a second
 * menu listing its choices; contents and bookmarks open a list. */
#include "screens/book_priv.h"

#include "core/log.h"
#include "core/prefs.h"
#include "ui/listview.h"
#include "ui/menu.h"

#include <stdio.h>
#include <string.h>

#define BM_MAX 200

enum { M_FONT, M_MARGIN, M_SPACING, M_FAMILY, M_THEME, M_CONTENTS, M_ADD_BM, M_BOOKMARKS,
       M_COUNT };

static const int FONT_PX[] = {16, 18, 20, 22, 24, 28, 32, 36, 40};
static const int MARGINS[] = {0, 16, 32, 48, 64, 96};
static const int SPACING10[] = {10, 12, 14, 16, 18, 20};
#define COUNT(a) ((int)(sizeof a / sizeof a[0]))

static char g_label[M_COUNT][64];
static const char *g_ptr[MENU_MAX];
static char g_choice[MENU_MAX][32];
static int g_sub = -1; /* the setting whose choices are shown, -1 = main menu */

static enum { L_NONE, L_TOC, L_BM } g_list;
static mdoc_toc_item_t g_toc[BOOKLD_TOC_MAX];
static db_bookmark_t g_bm[BM_MAX];
static lv_item_t g_items[BOOKLD_TOC_MAX];
static char g_right[BOOKLD_TOC_MAX][16];
static int g_ntoc, g_nbm;

int book_menu_is_open(void) { return menu_is_open() || listview_is_open(); }

void book_menu_close(void)
{
	while (menu_is_open() || listview_is_open()) { /* a Back press closes either */
		input_t back;
		memset(&back, 0, sizeof back);
		back.pressed = IN_BACK;
		int i;
		if (listview_is_open())
			listview_update(&back, &i);
		else
			menu_update(&back);
	}
	g_sub = -1;
	g_list = L_NONE;
}

void book_menu_open(void)
{
	snprintf(g_label[M_FONT], 64, "Font size: %d px", B.style.font_px);
	snprintf(g_label[M_MARGIN], 64, "Margins: %d px", B.style.margin_px);
	snprintf(g_label[M_SPACING], 64, "Line spacing: %.1f", (double)B.style.line_spacing);
	snprintf(g_label[M_FAMILY], 64, "Font: %s", B.style.font_family ? "Sans-serif" : "Book's own");
	snprintf(g_label[M_THEME], 64, "Page: %s", book_theme_name());
	snprintf(g_label[M_CONTENTS], 64, "Contents");
	snprintf(g_label[M_ADD_BM], 64, "Add bookmark");
	snprintf(g_label[M_BOOKMARKS], 64, "Bookmarks");
	for (int i = 0; i < M_COUNT; i++) g_ptr[i] = g_label[i];
	g_sub = -1;
	menu_open(B.book.title, g_ptr, M_COUNT);
}

static void open_choices(int setting)
{
	int n = 0, cur = 0;
	if (setting == M_FONT)
		for (; n < COUNT(FONT_PX); n++) {
			snprintf(g_choice[n], 32, "%d px", FONT_PX[n]);
			if (FONT_PX[n] == B.style.font_px) cur = n;
		}
	else if (setting == M_MARGIN)
		for (; n < COUNT(MARGINS); n++) {
			snprintf(g_choice[n], 32, "%d px", MARGINS[n]);
			if (MARGINS[n] == B.style.margin_px) cur = n;
		}
	else if (setting == M_SPACING)
		for (; n < COUNT(SPACING10); n++) {
			snprintf(g_choice[n], 32, "%d.%d", SPACING10[n] / 10, SPACING10[n] % 10);
			if (SPACING10[n] == (int)(B.style.line_spacing * 10.0f + 0.5f)) cur = n;
		}
	else if (setting == M_FAMILY) {
		snprintf(g_choice[n++], 32, "Book's own fonts");
		snprintf(g_choice[n++], 32, "Sans-serif");
		cur = B.style.font_family;
	}
	for (int i = 0; i < n; i++) g_ptr[i] = g_choice[i];
	g_sub = setting;
	menu_open(g_label[setting], g_ptr, n);
	menu_select(cur);
}

static void choose(int setting, int k)
{
	if (setting == M_FONT) B.style.font_px = FONT_PX[k];
	if (setting == M_MARGIN) B.style.margin_px = MARGINS[k];
	if (setting == M_SPACING) B.style.line_spacing = (float)SPACING10[k] / 10.0f;
	if (setting == M_FAMILY) B.style.font_family = k;
	book_restyle();
}

static void load_bookmarks(void)
{
	g_nbm = db_bookmark_list(B.book.id, g_bm, BM_MAX);
	if (g_nbm < 0) {
		log_printf("book: bookmarks: %s", db_last_error());
		g_nbm = 0;
	}
	for (int i = 0; i < g_nbm; i++) {
		g_items[i].label = g_bm[i].label;
		g_items[i].right = NULL;
		g_items[i].depth = 0;
	}
}

static void add_bookmark(void)
{
	if (!B.pos[0]) return; /* the page is not rendered yet: no position to keep */
	db_bookmark_t b;
	memset(&b, 0, sizeof b);
	b.book_id = B.book.id;
	b.page = B.page;
	snprintf(b.location, sizeof b.location, "%s", B.pos);
	int pct = B.count > 1 ? B.page * 100 / (B.count - 1) : 100;
	snprintf(b.label, sizeof b.label, "Page %d (%d%%)", B.page + 1, pct);
	int64_t id;
	if (db_bookmark_add(&b, &id) < 0) log_printf("book: bookmark: %s", db_last_error());
}

static void open_contents(void)
{
	int sel = 0;
	g_ntoc = bookld_toc(B.bl, g_toc, BOOKLD_TOC_MAX);
	for (int i = 0; i < g_ntoc; i++) {
		g_items[i].label = g_toc[i].title;
		g_items[i].depth = g_toc[i].depth;
		g_items[i].right = NULL;
		if (g_toc[i].page >= 0) {
			snprintf(g_right[i], sizeof g_right[0], "%d", g_toc[i].page + 1);
			g_items[i].right = g_right[i];
			if (g_toc[i].page <= B.page) sel = i;
		}
	}
	g_list = L_TOC;
	listview_open("Contents", g_items, g_ntoc, sel, "This book has no contents list", 0);
}

static void pick(int id)
{
	switch (id) {
	case M_FONT:
	case M_MARGIN:
	case M_SPACING:
	case M_FAMILY: open_choices(id); break;
	case M_THEME:
		B.theme = (B.theme + 1) % BOOK_THEMES;
		prefs_set_int("book", "theme", B.theme);
		break;
	case M_CONTENTS: open_contents(); break;
	case M_ADD_BM: add_bookmark(); break;
	case M_BOOKMARKS:
		load_bookmarks();
		g_list = L_BM;
		listview_open("Bookmarks", g_items, g_nbm, 0, "No bookmarks yet", 1);
		break;
	}
}

void book_menu_update(const input_t *in)
{
	if (listview_is_open()) {
		int i = -1;
		lv_action_t a = listview_update(in, &i);
		if (a == LV_PICK && g_list == L_TOC && g_toc[i].page >= 0) {
			book_goto(g_toc[i].page);
		} else if (a == LV_PICK && g_list == L_BM) {
			bookld_resolve(B.bl, g_bm[i].location); /* the layout may have changed since */
		} else if (a == LV_DELETE && g_list == L_BM) {
			db_bookmark_remove(g_bm[i].id);
			load_bookmarks();
			listview_set_items(g_items, g_nbm);
		}
		return;
	}
	int k = menu_update(in);
	if (k < 0) {
		if (!menu_is_open()) g_sub = -1;
		return;
	}
	if (g_sub >= 0) {
		choose(g_sub, k);
		g_sub = -1;
	} else {
		pick(k);
	}
}

void book_menu_draw(void)
{
	menu_draw(B.now);
	listview_draw(B.now);
}

int book_menu_hints(hint_t *out, int max)
{
	return listview_is_open() ? listview_hints(out, max) : menu_hints(out, max);
}
