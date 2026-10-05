/* Catalog: OPDS catalogs to browse and download from. The source list holds the built-in
 * public-domain catalogs and the servers the user added (Komga, Kavita, Calibre-Web...).
 * Feed browsing is in catalog_feed.c, drawing in catalog_draw.c, the book panel and the
 * add-server flow in catalog_entry.c. */
#include "screens/catalog_priv.h"
#include "screens/screens.h"

#include "core/log.h"
#include "ui/menu.h"

#include <stdio.h>
#include <string.h>

#define TOAST_MS 2500

catalog_t C;

/* Built-in catalogs: legal, public-domain or open-access books only. The Internet
 * Archive's own OPDS server (bookserver.archive.org) no longer answers; Open Library is
 * the Archive's catalog and lists its open-access books. */
static const struct {
	const char *name, *url;
} BUILTIN[] = {
	{"Project Gutenberg", "https://www.gutenberg.org/ebooks.opds/"},
	{"Open Library (Internet Archive)", "https://openlibrary.org/opds/"},
};
#define NBUILTIN ((int)(sizeof BUILTIN / sizeof BUILTIN[0]))

enum { SM_EDIT, SM_REMOVE };
static const char *const SERVER_MENU[] = {"Edit server", "Remove server"};
static int g_menu_for = -1;

const http_auth_t *cat_auth(void)
{
	static http_auth_t a;
	if (C.source < 0 || C.source >= C.nsrc) return NULL;
	a.user = C.src[C.source].user;
	a.pass = C.src[C.source].pass;
	return a.user[0] ? &a : NULL;
}

void cat_toast(const char *msg)
{
	snprintf(C.toast, sizeof C.toast, "%s", msg);
	C.toast_until = C.now + TOAST_MS;
}

static void load_sources(void)
{
	C.nsrc = 0;
	for (int i = 0; i < NBUILTIN; i++) {
		cat_source_t *s = &C.src[C.nsrc++];
		memset(s, 0, sizeof *s);
		snprintf(s->name, sizeof s->name, "%s", BUILTIN[i].name);
		snprintf(s->url, sizeof s->url, "%s", BUILTIN[i].url);
	}
	static db_server_t sv[CAT_SOURCES_MAX - NBUILTIN];
	int n = db_server_list(sv, CAT_SOURCES_MAX - NBUILTIN);
	if (n < 0) {
		log_printf("catalog: servers: %s", db_last_error());
		n = 0;
	}
	for (int i = 0; i < n; i++) {
		cat_source_t *s = &C.src[C.nsrc++];
		snprintf(s->name, sizeof s->name, "%s", sv[i].name);
		snprintf(s->url, sizeof s->url, "%s", sv[i].url);
		snprintf(s->user, sizeof s->user, "%s", sv[i].username);
		snprintf(s->pass, sizeof s->pass, "%s", sv[i].password);
		s->server_id = sv[i].id;
	}
	if (C.src_sel > C.nsrc) C.src_sel = C.nsrc;
}

int cat_builtin_count(void) { return NBUILTIN; }

static void enter(void)
{
	static int ready;
	if (!ready) {
		C.source = -1;
		C.detail = -1;
		ready = 1;
	}
	if (C.depth == 0) load_sources();
}

static void leave(void) { g_menu_for = -1; }

static void open_source(int i)
{
	C.source = i;
	cat_push(C.src[i].url, C.src[i].name);
	log_printf("catalog: open %s", C.src[i].url);
}

static void server_menu_pick(int pick)
{
	int i = g_menu_for;
	if (menu_is_open()) return;
	g_menu_for = -1;
	if (pick < 0 || i < NBUILTIN || i >= C.nsrc) return;
	if (pick == SM_EDIT) {
		cat_server_edit(i);
	} else if (db_server_remove(C.src[i].server_id) < 0) {
		cat_toast(db_last_error());
	} else {
		cat_toast("Server removed");
	}
	load_sources();
}

static void update_sources(const input_t *in)
{
	int rows = C.nsrc + 1; /* + "Add server" */
	if (in->pressed & IN_DOWN) C.src_sel = (C.src_sel + 1) % rows;
	if (in->pressed & IN_UP) C.src_sel = (C.src_sel + rows - 1) % rows;
	if (C.src_sel < C.src_top) C.src_top = C.src_sel;
	if (C.src_sel >= C.src_top + CAT_ROWS) C.src_top = C.src_sel - CAT_ROWS + 1;
	if (in->pressed & IN_CONFIRM) {
		if (C.src_sel == C.nsrc) {
			cat_server_add();
			load_sources();
		} else {
			open_source(C.src_sel);
		}
	} else if ((in->pressed & IN_TRIANGLE) && C.src_sel >= NBUILTIN && C.src_sel < C.nsrc) {
		g_menu_for = C.src_sel;
		menu_open(C.src[C.src_sel].name, SERVER_MENU, 2);
	}
}

static void update_feed(const input_t *in)
{
	cat_level_t *l = cat_top();
	if (in->pressed & IN_BACK) {
		cat_pop();
		if (C.depth == 0) load_sources();
		return;
	}
	if (in->pressed & IN_SQUARE) {
		cat_search_start();
		return;
	}
	if (in->pressed & IN_TRIANGLE) {
		cat_reload();
		return;
	}
	int rows = cat_rows(l);
	if (rows == 0) return;
	if (in->pressed & IN_DOWN) l->sel = l->sel + 1 < rows ? l->sel + 1 : l->sel;
	if (in->pressed & IN_UP) l->sel = l->sel > 0 ? l->sel - 1 : 0;
	if (in->pressed & IN_RIGHT) l->sel = l->sel + CAT_ROWS < rows ? l->sel + CAT_ROWS : rows - 1;
	if (in->pressed & IN_LEFT) l->sel = l->sel > CAT_ROWS ? l->sel - CAT_ROWS : 0;
	if (l->sel < l->top) l->top = l->sel;
	if (l->sel >= l->top + CAT_ROWS) l->top = l->sel - CAT_ROWS + 1;
	if (!(in->pressed & IN_CONFIRM)) return;
	int e = -1;
	cat_row_kind_t k = cat_row(l, l->sel, &e);
	if (k == ROW_PREV) {
		cat_replace(l->feed->prev);
	} else if (k == ROW_NEXT) {
		cat_replace(l->feed->next);
	} else if (l->feed->entries[e].nav_href && l->feed->entries[e].nacq == 0) {
		cat_push(l->feed->entries[e].nav_href, l->feed->entries[e].title);
	} else {
		cat_detail_open(e);
	}
}

static void update(const input_t *in)
{
	C.now = in->now_ms;
	cat_poll();
	cat_covers_pump();
	if (menu_is_open() && g_menu_for >= 0) {
		server_menu_pick(menu_update(in));
	} else if (C.detail >= 0) {
		cat_detail_update(in);
	} else if (C.depth == 0) {
		update_sources(in);
	} else {
		update_feed(in);
	}
}

static void draw(void)
{
	if (C.depth == 0)
		cat_draw_sources();
	else if (C.detail >= 0)
		cat_draw_detail();
	else
		cat_draw_feed();
	cat_draw_toast();
	menu_draw(C.now);
}

static int hints(hint_t *out, int max)
{
	if (menu_is_open()) return menu_hints(out, max);
	if (C.detail >= 0) return cat_detail_hints(out, max);
	if (C.depth == 0) {
		hint_t h[] = {
			{GLYPH_LR, "Section"},
			{GLYPH_DPAD, "Choose"},
			{GLYPH_CONFIRM, C.src_sel == C.nsrc ? "Add server" : "Open"},
			{GLYPH_TRIANGLE, "Edit"},
		};
		int n = C.src_sel >= NBUILTIN && C.src_sel < C.nsrc ? 4 : 3;
		return screen_copy_hints(h, n, out, max);
	}
	const cat_level_t *l = cat_top();
	hint_t h[] = {
		{GLYPH_LR, "Section"}, {GLYPH_DPAD, "Choose"},    {GLYPH_CONFIRM, "Open"},
		{GLYPH_BACK, "Back"},  {GLYPH_TRIANGLE, "Reload"}, {GLYPH_SQUARE, "Search"},
	};
	int n = l->feed && l->feed->search_href ? 6 : 5;
	return screen_copy_hints(h, n, out, max);
}

const screen_t screen_catalog = {"Catalog", enter, leave, update, draw, hints};
