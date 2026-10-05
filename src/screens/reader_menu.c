/* reader_menu.c - the comic reader's overlay menu (Confirm while the overlay is shown):
 * reading mode, fit, rotation, double pages, crop, contents, bookmarks. Mode, fit, double
 * pages and crop are saved for this book only (Settings holds the defaults). Contents and bookmarks open a list. */
#include "screens/reader_priv.h"

#include "core/log.h"
#include "ui/listview.h"
#include "ui/menu.h"

#include <stdio.h>
#include <string.h>

#define BM_MAX 200
#define TOC_MAX 256

enum { M_MODE, M_FIT, M_ROTATE, M_SPREADS, M_CROP, M_CONTENTS, M_ADD_BM, M_BOOKMARKS, M_COUNT };

static int g_ids[M_COUNT], g_nids;
static char g_label[M_COUNT][64];
static const char *g_ptr[M_COUNT];

static enum { L_NONE, L_TOC, L_BM } g_list;
static db_bookmark_t g_bm[BM_MAX];
static mdoc_toc_item_t g_toc[TOC_MAX];
static lv_item_t g_items[TOC_MAX > BM_MAX ? TOC_MAX : BM_MAX];
static char g_right[TOC_MAX > BM_MAX ? TOC_MAX : BM_MAX][16];
static int g_nbm, g_ntoc;

void reader_menu_reset(void)
{
	g_list = L_NONE;
	g_ntoc = R.ld ? loader_toc(R.ld, g_toc, TOC_MAX) : 0;
	if (g_ntoc < 0) g_ntoc = 0;
}

static void add(int id, const char *fmt, const char *val)
{
	snprintf(g_label[g_nids], sizeof g_label[0], fmt, val);
	g_ptr[g_nids] = g_label[g_nids];
	g_ids[g_nids++] = id;
}

void reader_menu_open(void)
{
	static const char *const SPREADS[] = {"Off", "On", "On, cover alone"};
	static const char *const ROT[] = {"0\xC2\xB0", "90\xC2\xB0", "180\xC2\xB0", "270\xC2\xB0"};
	g_nids = 0;
	add(M_MODE, "Reading mode: %s", reader_mode_name());
	if (R.mode != RM_VERTICAL) {
		add(M_FIT, "Fit: %s", reader_fit_name());
		add(M_ROTATE, "Rotate: %s", ROT[R.rot & 3]);
		if (R.rot == 0) add(M_SPREADS, "Double pages: %s", SPREADS[R.spreads]);
		add(M_CROP, "Crop white borders: %s", R.crop ? "On" : "Off");
	}
	if (g_ntoc > 0) add(M_CONTENTS, "%s", "Contents");
	add(M_ADD_BM, "%s", "Add bookmark");
	add(M_BOOKMARKS, "%s", "Bookmarks");
	menu_open(R.book.title, g_ptr, g_nids);
}

int reader_menu_is_open(void) { return menu_is_open() || listview_is_open(); }

static void load_bookmarks(void)
{
	g_nbm = db_bookmark_list(R.book.id, g_bm, BM_MAX);
	if (g_nbm < 0) {
		log_printf("reader: bookmarks: %s", db_last_error());
		g_nbm = 0;
	}
	for (int i = 0; i < g_nbm; i++) {
		g_items[i].label = g_bm[i].label;
		g_items[i].depth = 0;
		g_items[i].right = NULL;
	}
}

static void open_bookmarks(void)
{
	load_bookmarks();
	int sel = 0;
	for (int i = 0; i < g_nbm; i++)
		if (g_bm[i].page <= R.page) sel = i;
	g_list = L_BM;
	listview_open("Bookmarks", g_items, g_nbm, sel, "No bookmarks yet", 1);
}

static void open_contents(void)
{
	int sel = 0;
	for (int i = 0; i < g_ntoc; i++) {
		g_items[i].label = g_toc[i].title;
		g_items[i].depth = g_toc[i].depth;
		if (g_toc[i].page >= 0) {
			snprintf(g_right[i], sizeof g_right[0], "%d", g_toc[i].page + 1);
			g_items[i].right = g_right[i];
			if (g_toc[i].page <= R.page) sel = i;
		} else {
			g_items[i].right = NULL;
		}
	}
	g_list = L_TOC;
	listview_open("Contents", g_items, g_ntoc, sel, "", 0);
}

static void add_bookmark(void)
{
	db_bookmark_t b;
	memset(&b, 0, sizeof b);
	b.book_id = R.book.id;
	b.page = R.page;
	snprintf(b.location, sizeof b.location, "p:%d", R.page);
	snprintf(b.label, sizeof b.label, "Page %d", R.page + 1);
	int64_t id;
	if (db_bookmark_add(&b, &id) < 0)
		log_printf("reader: bookmark: %s", db_last_error());
	else
		log_printf("reader: bookmark page %d", R.page);
}

static void pick(int id)
{
	switch (id) {
	case M_MODE:
		R.mode = (read_mode_t)((R.mode + 1) % RM_COUNT);
		reader_setting_save("mode", R.mode);
		R.sp_dirty = 1;
		reader_goto(R.page, 1);
		break;
	case M_FIT:
		R.fit = (fit_mode_t)((R.fit + 1) % 4);
		R.view.mode = R.fit;
		reader_setting_save("fit", R.fit);
		reader_goto(R.page, 1);
		break;
	case M_ROTATE: {
		char v[8];
		R.rot = (R.rot + 1) & 3;
		snprintf(v, sizeof v, "%d", R.rot);
		db_book_setting_set(R.book.id, "rotate", v);
		R.sp_dirty = 1;
		reader_goto(R.page, 1);
		break;
	}
	case M_SPREADS:
		R.spreads = (R.spreads + 1) % 3;
		reader_setting_save("spreads", R.spreads);
		reader_spreads_rebuild();
		reader_goto(R.page, 1);
		break;
	case M_CROP:
		R.crop = !R.crop;
		reader_setting_save("crop", R.crop);
		rtex_clear(); /* pages uploaded before were not measured */
		reader_goto(R.page, 1);
		break;
	case M_CONTENTS: open_contents(); return;
	case M_ADD_BM: add_bookmark(); return;
	case M_BOOKMARKS: open_bookmarks(); return;
	}
	log_printf("reader: %s, %s, rotate %d, spreads %d, crop %d", reader_mode_name(), reader_fit_name(),
	           R.rot * 90, R.spreads, R.crop);
}

void reader_menu_update(const input_t *in)
{
	if (listview_is_open()) {
		int i = -1;
		lv_action_t a = listview_update(in, &i);
		if (a == LV_PICK && g_list == L_TOC && g_toc[i].page >= 0) {
			reader_goto(g_toc[i].page, 1);
		} else if (a == LV_PICK && g_list == L_BM) {
			reader_goto(g_bm[i].page, 1);
		} else if (a == LV_DELETE && g_list == L_BM) {
			db_bookmark_remove(g_bm[i].id);
			load_bookmarks();
			listview_set_items(g_items, g_nbm);
		}
		return;
	}
	int k = menu_update(in);
	if (k >= 0 && k < g_nids) pick(g_ids[k]);
}

void reader_menu_draw(void)
{
	menu_draw(R.now);
	listview_draw(R.now);
}

int reader_menu_hints(hint_t *out, int max)
{
	return listview_is_open() ? listview_hints(out, max) : menu_hints(out, max);
}
