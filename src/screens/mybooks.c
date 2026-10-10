/* My Books: folder series, local search, reading-state filters and book actions. */
#include "screens/screens.h"
#include "screens/library_tools.h"
#include "core/log.h"
#include "core/prefs.h"
#include "core/strutil.h"
#include "core/natsort.h"
#include "lib/covers.h"
#include "lib/formats.h"
#include "lib/library.h"
#include "lib/shelf.h"
#include "net/fetch.h"
#include "net/url.h"
#include "net/seriesmeta.h"
#include "ui/draw.h"
#include "ui/font.h"
#include "ui/grid.h"
#include "ui/menu.h"
#include "ui/osk.h"
#include "ui/text.h"
#include "ui/theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BOOKS 10000
#define RELOAD_MS 2000
typedef struct {
	int book, volumes, finished, custom_cover;
 double progress;
	int64_t last_read, added;
	char series[512], title[256], badge[32];
} shelf_row_t;
static db_book_t *g_books;
static shelf_row_t *g_rows;
static int *g_order;
static char (*g_folders)[512];
static unsigned char g_member[MAX_BOOKS];
static int g_capacity;
static int g_nbooks, g_count, g_sort, g_filter, g_group = 1;
static int g_private,g_bulk,g_nselected;
static uint64_t g_selected[MAX_BOOKS];
static int64_t g_collection;
static db_collection_t g_collections[18];
static int g_collection_page;
static char g_collection_title[128];
static int g_ncollections,g_add_bulk;
static const char *g_collection_names[24];
static int selected(uint64_t id) { for(int i=0;i<g_nselected;i++) if(g_selected[i]==id) return 1; return 0; }
static void select_book(uint64_t id,int value) {
 for(int i=0;i<g_nselected;i++) if(g_selected[i]==id) { if(!value) g_selected[i]=g_selected[--g_nselected]; return; }
 if(value && g_nselected<MAX_BOOKS) g_selected[g_nselected++]=id;
}
static int collection_match(const db_book_t *b) { return !g_collection || (g_collection==-1 ? b->finished : db_collection_contains(g_collection,b->id)); }

static const char *pref_section(void) { return g_private ? "private_shelf" : "mybooks"; }
static grid_t g_grid;
static int g_grid_ready, g_menu, g_menu_book = -1;
static char g_query[256], g_series[512], g_heading[256];
static unsigned g_seen_gen = ~0u;
static uint64_t g_now, g_last_reload, g_continue_id;
static const char *const FILTERS[] = {"All", "Unread", "Reading", "Finished", "Favourites", "Comics", "Books / documents"};
static const char *const SORTS[] = {"Title", "Last read", "Added"};
enum { MENU_NONE, MENU_LIBRARY, MENU_BOOK, MENU_FILTER, MENU_SORT, MENU_SERIES, MENU_BULK, MENU_COLLECTIONS, MENU_ADD };
static const char *g_actions[13];
static char g_menu_series[512];
static int g_meta_request,g_meta_explicit;
static uint64_t g_gap_updated;
static char g_gap_series[512],g_gap_text[256];

static char g_meta_series[512],g_meta_attempt[512];
static uint64_t g_meta_last;
static void metadata_start(const char *series,int force);
static void metadata_update(void);
static void metadata_draw(void);
static void metadata_close(void);


static void continue_series(const char *series)
{
	db_book_t b; int n = db_continue_series_section(series, g_private, &b);
	if (n == 1) app_open_book(&b);
	else library_tools_message(n < 0 ? "Could not load series" : "All available volumes in this series are finished");
}


static int order_cmp(const void *aa, const void *bb)
{
	int ia = *(const int *)aa, ib = *(const int *)bb;
	const db_book_t *a = &g_books[ia], *b = &g_books[ib];
	const char *sa = g_folders[ia], *sb = g_folders[ib];
	int c = natcmp(sa[0] ? sa : a->title, sb[0] ? sb : b->title);
	if (!c) c = strcmp(sa, sb); /* keep distinct same-spelling/case folders together */
	return c ? c : shelf_volume_compare(a, b);
}

static int row_cmp(const void *aa, const void *bb)
{
	const shelf_row_t *a = aa, *b = bb;
	int64_t av = g_sort == DB_SORT_LAST_READ ? a->last_read : a->added;
	int64_t bv = g_sort == DB_SORT_LAST_READ ? b->last_read : b->added;
	if (g_sort && av != bv) return av > bv ? -1 : 1;
	return natcmp(a->title, b->title);
}

static void build_rows(void)
{
	g_count = 0;
 for(int i=0;i<g_nbooks;i++) g_member[i]=collection_match(&g_books[i]);
 /* Aggregate every owned volume before applying the visible row filter. */
 for(int k=0;k<g_nbooks;) {
  int i=g_order[k], end=k+1; const char *series=g_folders[i];
  int group=g_group && !g_series[0] && series[0];
  if(group) while(end<g_nbooks && !strcmp(g_folders[g_order[end]],series)) end++;
  if(g_series[0] && strcmp(series,g_series)) { k=end; continue; }
  shelf_row_t row={0}; row.book=i; int matches=0;
  for(int j=k;j<end;j++) {
   db_book_t *b=&g_books[g_order[j]];
   matches |= shelf_matches(b,g_query,g_filter) && g_member[g_order[j]];
   row.volumes++; row.finished+=b->finished!=0;
   double progress=b->finished ? 1:b->progress;
   row.progress+=progress<0 ? 0:progress>1 ? 1:progress;
   if(b->last_read>row.last_read) row.last_read=b->last_read;
   if(b->added>row.added) row.added=b->added;
  }
  row.progress/=row.volumes;
  if(matches) {
   db_book_t *b=&g_books[i];
   if(group) {
    str_copy(row.series,sizeof row.series,series); str_copy(row.title,sizeof row.title,path_basename(series));
    snprintf(row.badge,sizeof row.badge,"%d/%d read",row.finished,row.volumes);
   } else {
    str_copy(row.title,sizeof row.title,b->title);
    if(b->favorite) str_copy(row.badge,sizeof row.badge,"FAVOURITE");
    else if(b->last_read && !b->finished) str_copy(row.badge,sizeof row.badge,"READING");
    else if(b->kind==DB_KIND_FOLDER) str_copy(row.badge,sizeof row.badge,"DIR");
    else formats_badge_r(b->path,row.badge,sizeof row.badge);
   }
   if(group) { char cover[512]; row.custom_cover=db_series_setting_get(series,"cover_path",cover,sizeof cover)==1 && cover[0]; }
   if(row.finished==row.volumes) str_copy(row.badge,sizeof row.badge,"Complete");
   g_rows[g_count++]=row;
  }
  k=end;
 }
	if (g_group && !g_series[0]) qsort(g_rows, (size_t)g_count, sizeof *g_rows, row_cmp);
	grid_set_count(&g_grid, g_count);
	snprintf(g_heading, sizeof g_heading, "%.100s | %s%s%.80s", g_series[0] ? path_basename(g_series) : (g_group ? "Series & books" : "All books"),
	         FILTERS[g_filter], g_query[0] ? " | " : "", g_query);
}

static void reload(void)
{
	char keep_series[512]="";
 if(g_count && g_grid.sel<g_count) str_copy(keep_series,sizeof keep_series,g_rows[g_grid.sel].series);
 uint64_t keep = g_count && g_grid.sel < g_count ? g_books[g_rows[g_grid.sel].book].id : 0;
	int needed = db_books_count_section(g_private);
	if (needed < 0) { library_tools_message("Could not load library"); return; }
	if (needed > MAX_BOOKS) { library_tools_message("Showing the first 10,000 volumes; narrow your library"); needed = MAX_BOOKS; }
	if (needed > g_capacity || !g_books) {
		int cap = (needed + 127) / 128 * 128;
		if (cap < 128) cap = 128;
		if (cap > MAX_BOOKS) cap = MAX_BOOKS;
		db_book_t *books = calloc((size_t)cap, sizeof *books);
		shelf_row_t *rows = calloc((size_t)cap, sizeof *rows);
		int *order = malloc((size_t)cap * sizeof *order);
		char (*folders)[512] = malloc((size_t)cap * sizeof *folders);
		if (!books || !rows || !order || !folders) {
			free(books); free(rows); free(order); free(folders);
			library_tools_message("Not enough memory to show library"); return;
		}
		free(g_books); free(g_rows); free(g_order); free(g_folders);
		g_books = books; g_rows = rows; g_order = order; g_folders = folders;
		g_capacity = cap;
	}
	int n = db_books_list_section(g_sort, g_private, g_books, g_capacity);
	if (n < 0) { log_printf("mybooks: list: %s", db_last_error()); n = 0; }
	g_nbooks = n;
	for (int i = 0; i < n; i++) {
		g_order[i] = i;
		shelf_book_series(&g_books[i], g_folders[i], sizeof g_folders[i]);
	}
	qsort(g_order, (size_t)n, sizeof *g_order, order_cmp);
	db_book_t c; g_continue_id = db_continue_reading_section(g_private, &c) == 1 ? c.id : 0;
	build_rows();
	for (int i = 0; i < g_count; i++) if ((keep_series[0] && !strcmp(g_rows[i].series,keep_series)) || (!keep_series[0] && g_books[g_rows[i].book].id == keep)) g_grid.sel = i;
	g_seen_gen = library_generation(); g_last_reload = g_now;
}

static void item(void *user, int i, grid_item_t *out)
{
	(void)user; shelf_row_t *r = &g_rows[i]; db_book_t *b = &g_books[r->book];
	out->actions = 1;
 if(g_bulk) { int n=0; for(int j=0;j<g_nbooks;j++) if((r->series[0] ? !strcmp(g_folders[j],r->series) : j==r->book) && selected(g_books[j].id)) n++; out->selected=n>0; }
	out->title = r->title; out->badge = r->badge;
	out->cover = r->custom_cover ? covers_get_series(r->series) : covers_get(b->id, b->path, b->kind);
	out->finished = r->series[0] ? r->finished == r->volumes : b->finished;
	out->progress = (float)r->progress;
}

static void enter_shelf(void)
{
	g_sort = (int)prefs_get_int(pref_section(), "sort", DB_SORT_TITLE);
	if (g_sort < 0 || g_sort > 2) g_sort = 0;
	g_filter = (int)prefs_get_int(pref_section(), "filter", SHELF_ALL);
	if (g_filter < 0 || g_filter >= SHELF_FILTERS) g_filter = 0;
	g_group = prefs_get_int(pref_section(), "series", 1) != 0;
	if (!g_grid_ready) { grid_init(&g_grid); g_grid_ready = 1; }
	g_grid.actions = 1;
	reload();
}

static void enter(void) { if (g_private) { g_collection=0; g_series[0] = g_query[0] = 0; grid_init(&g_grid); } g_private = 0; enter_shelf(); }
static void enter_private(void) { if (!g_private) { g_collection=0; g_series[0] = g_query[0] = 0; grid_init(&g_grid); } g_private = 1; enter_shelf(); }

static void leave(void) { g_bulk=0; g_nselected=0; metadata_close(); g_menu = MENU_NONE; library_tools_close(); }

static void collections_menu(int adding)
{
 g_ncollections=db_collections_page(g_collection_page*18,g_collections,18); if(g_ncollections<0) g_ncollections=0;
 for(int i=0;i<g_ncollections;i++) g_collection_names[i]=g_collections[i].name;
 g_collection_names[g_ncollections]="Create collection";
 g_collection_names[g_ncollections+1]="Previous collections"; g_collection_names[g_ncollections+2]="More collections";
 if(!adding) { g_collection_names[g_ncollections+3]="Completed"; g_collection_names[g_ncollections+4]="All collections / library"; }
 g_menu=adding ? MENU_ADD:MENU_COLLECTIONS;
 menu_open(adding ? "Add to collection":"Collections",g_collection_names,g_ncollections+(adding ? 3:5));
}
static void bulk_menu(void)
{
 static const char *const a[]={"Move selection to Private","Move selection to Your shelf","Mark selection complete","Mark selection unread","Add selection to collection","Select all visible","Clear selection","Exit bulk selection"};
 g_menu=MENU_BULK; menu_open("Bulk selection",a,8);
}
static void toggle_row(int index,int value)
{
 shelf_row_t *r=&g_rows[index];
 for(int i=0;i<g_nbooks;i++) if(r->series[0] ? !strcmp(g_folders[i],r->series):i==r->book) select_book(g_books[i].id,value);
}
static void library_menu(void)
{
	static const char *const a[] = {"Search library", "Clear search", "Filter", "Sort",
	                               "Toggle series / all books", "Reading statistics", "Rescan library", "Bulk selection", "Collections", "Undo recent move"};
	g_menu = MENU_LIBRARY; menu_open("Library", a, 10);
}

static void book_menu(void)
{
	if (g_grid.sel >= g_count) { library_menu(); return; }
	if (g_rows[g_grid.sel].series[0]) {
		static const char *const a[] = {"Continue series", "Browse volumes", "Choose series cover image", "Find series cover online", "Add series to collection", "Move series to Private", "Move series to Your shelf", "Expected volume count", "Check volume count online", "Use original series cover"};
		str_copy(g_menu_series, sizeof g_menu_series, g_rows[g_grid.sel].series);
		g_menu_book = g_rows[g_grid.sel].book;
		g_menu = MENU_SERIES; menu_open(path_basename(g_menu_series), a, 10); return;
	}
	g_menu_book = g_rows[g_grid.sel].book; db_book_t *b = &g_books[g_menu_book];
	g_actions[0] = b->finished ? "Mark as unread" : "Mark as finished";
	g_actions[1] = b->favorite ? "Remove from favourites" : "Add to favourites";
	g_actions[2] = "Edit title"; g_actions[3] = "Choose custom cover image";
	g_actions[4] = "Find cover online"; g_actions[5] = "Use original cover";
	g_actions[6] = "Refresh cover"; g_actions[7] = "Reading statistics";
	g_actions[8] = "Delete file";
	g_actions[9] = b->private_book ? "Move to Your shelf" : "Move to Private";
	g_actions[10] = "Set series";
 g_actions[11]="Add to collection"; g_actions[12]="Remove from this collection";
	g_menu = MENU_BOOK; menu_open(b->title, g_actions, g_collection>0 ? 13:12);
}

static void pick(int k)
{
	int mode = g_menu; g_menu = MENU_NONE;
	if (k < 0) return;
	if (mode == MENU_SERIES) {
		if (k == 0) continue_series(g_menu_series);
		if (k == 1) { str_copy(g_series, sizeof g_series, g_menu_series); build_rows(); g_grid.sel = 0; g_grid.scroll = 0; }
		if (k == 2) library_series_cover(&g_books[g_menu_book],g_menu_series,0);
		if (k == 3) library_series_cover(&g_books[g_menu_book],g_menu_series,1);
  if(k==4) { g_nselected=0; for(int i=0;i<g_nbooks;i++) if(!strcmp(g_folders[i],g_menu_series)) select_book(g_books[i].id,1); g_add_bulk=1; collections_menu(1); }
  if(k==5 || k==6) { uint64_t ids[MAX_BOOKS]; int n=0; for(int i=0;i<g_nbooks;i++) if(!strcmp(g_folders[i],g_menu_series)) ids[n++]=g_books[i].id; if(db_bulk_private(ids,n,k==5)<0) library_tools_message("Could not move series"); reload(); }
  if(k==7) { char count[16],old[16]; db_series_setting_get(g_menu_series,"expected_volumes",old,sizeof old); if(osk_prompt("Expected volume count (0 disables warnings)",old,OSK_TEXT,count,sizeof count)==1) { int n; if(!strcmp(count,"0") || str_page_number(count,1000,&n)) db_series_setting_set(g_menu_series,"expected_volumes",count); else library_tools_message("Enter 0-1000"); } }
  if(k==8) metadata_start(g_menu_series,1);
  if(k==9) { db_series_setting_set(g_menu_series,"cover_path",""); covers_forget(covers_series_id(g_menu_series)); reload(); }

	} else if (mode == MENU_LIBRARY) {
		if (k == 0) { char q[256]; if (osk_prompt("Search titles, authors and folders", g_query, OSK_TEXT, q, sizeof q) == 1) { str_copy(g_query, sizeof g_query, q); build_rows(); } }
		if (k == 1) { g_query[0] = 0; build_rows(); }
		if (k == 2) { g_menu = MENU_FILTER; menu_open("Filter", FILTERS, SHELF_FILTERS); menu_select(g_filter); }
		if (k == 3) { g_menu = MENU_SORT; menu_open("Sort", SORTS, 3); menu_select(g_sort); }
		if (k == 4) { g_group = !g_group; g_series[0] = 0; prefs_set_int(pref_section(), "series", g_group); build_rows(); }
		if (k == 5) library_stats_open(NULL);
		if (k == 6) library_scan_start();
  if(k==7) { g_bulk=1; g_nselected=0; library_tools_message("Confirm: select volumes | Triangle: bulk actions"); }
  if(k==8) collections_menu(0);
  if(k==9) { int r=db_undo_move(); library_tools_message(r>0 ? "Recent move undone":r<0 ? "Could not undo move":"No moves to undo"); reload(); }

	} else if(mode==MENU_COLLECTIONS || mode==MENU_ADD) {
  int64_t id=0;
  if(k==g_ncollections+1 || k==g_ncollections+2) {
   if(k==g_ncollections+1 && g_collection_page>0) g_collection_page--;
   if(k==g_ncollections+2 && g_ncollections==18) g_collection_page++;
   collections_menu(mode==MENU_ADD); return;
  }
  if(k==g_ncollections) { char name[128]; if(osk_prompt("New collection name","",OSK_TEXT,name,sizeof name)==1) { char *clean=str_trim(name); if(db_collection_create(clean,&id)<0) library_tools_message("Could not create collection; use a unique name"); else if(mode==MENU_COLLECTIONS) str_copy(g_collection_title,sizeof g_collection_title,clean); } }
  else if(k<g_ncollections) { id=g_collections[k].id; if(mode==MENU_COLLECTIONS) str_copy(g_collection_title,sizeof g_collection_title,g_collections[k].name); }
  else if(k==g_ncollections+3) { id=-1; str_copy(g_collection_title,sizeof g_collection_title,"Completed"); }
  if(mode==MENU_COLLECTIONS) { if(k!=g_ncollections || id) { g_collection=id; build_rows(); g_grid.sel=0; g_grid.scroll=0; } }
  else if(id>0) {
   int ok=1; if(g_add_bulk) { for(int i=0;i<g_nselected;i++) if(db_collection_add(id,g_selected[i])<0) ok=0; }
   else if(g_menu_book>=0 && db_collection_add(id,g_books[g_menu_book].id)<0) ok=0;
   library_tools_message(ok ? "Added to collection":"Could not add every selected volume");
  }
 } else if(mode==MENU_BULK) {
  if(k==5) { for(int i=0;i<g_count;i++) toggle_row(i,1); }
  else if(k==6) g_nselected=0;
  else if(k==7) { g_bulk=0; g_nselected=0; }
  else if(!g_nselected) library_tools_message("Select volumes with Confirm first");
  else if(k==4) { g_add_bulk=1; collections_menu(1); }
  else {
   int ok=1;
   if(k<2) ok=db_bulk_private(g_selected,g_nselected,k==0)==0;
   else for(int i=0;i<g_nselected;i++) if((k==2 ? db_set_finished(g_selected[i],1):db_mark_unread(g_selected[i]))<0) ok=0;
   library_tools_message(ok ? "Selection updated":"Could not update every selected volume"); g_nselected=0; reload();
  }
 } else if (mode == MENU_FILTER) {
		g_filter = k; prefs_set_int(pref_section(), "filter", k); build_rows();
	} else if (mode == MENU_SORT) {
		g_sort = k; prefs_set_int(pref_section(), "sort", k); reload();
	} else if (mode == MENU_BOOK && g_menu_book >= 0 && g_menu_book < g_nbooks) {
		db_book_t *b = &g_books[g_menu_book];
		if (k == 0) {
			int rc = b->finished ? db_mark_unread(b->id) : db_set_finished(b->id, 1);
			if (rc >= 0) rc = db_book_get(b->id, b) == 1 ? 0 : -1;
			if (rc != 0) log_printf("mybooks: finished: %s", db_last_error());
		}
		if (k == 1) {
			int rc = db_set_favorite(b->id, !b->favorite);
			if (rc >= 0) rc = db_book_get(b->id, b) == 1 ? 0 : -1;
			if (rc != 0) log_printf("mybooks: favorite: %s", db_last_error());
		}
		if (k == 2) library_edit_title(b);
		if (k == 3) library_cover_local(b);
		if (k == 4) library_cover_search(b);
		if (k == 5) { db_book_setting_set(b->id, "cover_path", ""); db_book_setting_set(b->id, "cover_source", ""); covers_forget(b->id); }
		if (k == 6) covers_forget(b->id);
		if (k == 7) library_stats_open(b);
		if (k == 8) library_delete_book(b);
		if (k == 9 && db_set_private(b->id, !b->private_book) < 0)
			library_tools_message("Could not move volume");
		if(k==11) { g_add_bulk=0; collections_menu(1); }
 if(k==12 && g_collection>0) db_collection_remove(g_collection,b->id);
		if (k == 10) {
			char name[256], current[512];
			shelf_book_series(b, current, sizeof current);
			if (osk_prompt("Series name (empty restores automatic grouping)", current, OSK_TEXT, name, sizeof name) == 1) {
				if (db_set_series(b->id, name) < 0) library_tools_message("Could not save series");
				else g_series[0] = 0;
			}
		}
		reload();
	}
}

static void update(const input_t *in)
{
	g_now = in->now_ms;
 metadata_update();
 if(g_series[0]) metadata_start(g_series,0);
	if (library_tools_busy()) { if (library_tools_update(in)) reload(); return; }
	library_tools_update(in);
	if (menu_is_open()) { int k = menu_update(in); if (!menu_is_open()) pick(k); return; }
	unsigned gen = library_generation();
	if (gen != g_seen_gen && (!library_scan_running() || g_now - g_last_reload >= RELOAD_MS)) {
		reload();
	}
 if((in->pressed & IN_BACK) && g_bulk) { g_bulk=0; g_nselected=0; return; }
	if ((in->pressed & IN_BACK) && g_series[0]) { g_series[0] = 0; build_rows(); g_grid.sel = 0; g_grid.scroll = 0; return; }
	if (in->pressed & IN_SQUARE) { library_menu(); return; }
	if (in->pressed & IN_TRIANGLE) { if(g_bulk) bulk_menu(); else book_menu(); return; }
	if (in->pressed & IN_SELECT) {
  if(g_bulk) return;
		const char *series = g_series[0] ? g_series :
		    (g_grid.sel < g_count ? g_rows[g_grid.sel].series : "");
		if (series[0]) { continue_series(series); return; }
		db_book_t b; if (db_book_get(g_continue_id, &b) == 1 && !b.missing) app_open_book(&b);
		return;
	}
	int action = grid_update(&g_grid, in);
	if (action == GRID_MENU) { if(g_bulk) bulk_menu(); else book_menu(); return; }
	if (action == GRID_OPEN && g_grid.sel < g_count) {
		shelf_row_t *r = &g_rows[g_grid.sel];
 if(g_bulk) { toggle_row(g_grid.sel,!selected(g_books[r->book].id)); return; }
		if (r->series[0]) { str_copy(g_series, sizeof g_series, r->series); build_rows(); g_grid.sel = 0; g_grid.scroll = 0; }
		else app_open_book(&g_books[r->book]);
	}
}

static void draw(void)
{
	if (!g_count) {
		const char *msg = library_scan_running() ? "Looking for books..." : (g_nbooks ? "No matching books" : (g_private ? "No private volumes" : "No books yet"));
		font_draw(582 - font_width(FONT_PX_TITLE, msg) / 2, 230, THEME_FONT, FONT_PX_TITLE, msg);
		font_draw(325, 266, THEME_SUBFONT, FONT_PX_LABEL, g_nbooks ? "Square: change search or filter" : (g_private ? "Volume menu: Move to Private" : "Put books in ux0:books or its subfolders"));
	} else {
		grid_draw(&g_grid, item, NULL, g_now);
		if (g_grid.sel < g_count) covers_prioritize(g_books[g_rows[g_grid.sel].book].id);
	}
	char heading[TEXT_LINE_MAX], count[48];
	text_ellipsize(FONT_PX_TITLE, g_series[0] ? path_basename(g_series) : (g_private ? "Private" : "Your shelf"), 535, heading);
	font_draw(222, 19, THEME_FONT, FONT_PX_TITLE, heading);
	snprintf(count, sizeof count, "%d %s", g_count, g_series[0] || !g_group ? "volumes" : "shelves / books");
	font_draw(942 - font_width(FONT_PX_LABEL, count), 22, THEME_SUBFONT, FONT_PX_LABEL, count);
	draw_rect(222, 49, 720, 1, THEME_PANEL);
	/* A small footer leaves the cover grid's touch and scroll geometry intact. */
	draw_rect(205, 487, 755, 34, theme_bg());
	char line[TEXT_LINE_MAX]; text_ellipsize(FONT_PX_LABEL, g_heading, 730, line);
	if(g_bulk) snprintf(line,sizeof line,"%d selected | Confirm: select | Triangle: bulk actions",g_nselected);
 else if(g_collection) { const char *name=g_collection_title; snprintf(line,sizeof line,"Collection: %.120s | Square: Collections to return",name); }
 font_draw(218, 496, THEME_SUBFONT, FONT_PX_LABEL, line);
 metadata_draw();
	if (library_scan_running()) {
		char msg[64]; snprintf(msg, sizeof msg, "Scanning: %d found", library_scan_found());
		draw_rect(745, 463, 210, 25, THEME_PANEL); font_draw(752, 467, THEME_FONT, FONT_PX_LABEL, msg);
	}
	menu_draw(g_now); library_tools_draw(g_now);
}

static int hints(hint_t *out, int max)
{
	if (library_tools_busy()) return library_tools_hints(out, max);
	if (menu_is_open()) return menu_hints(out, max);
 if(g_bulk) {
  hint_t h[]={{GLYPH_LR,"Section"},{GLYPH_DPAD,"Choose"},{GLYPH_CONFIRM,"Select"},{GLYPH_TRIANGLE,"Bulk actions"},{GLYPH_SQUARE,"Library"},{GLYPH_BACK,"Exit selection"}};
  return screen_copy_hints(h,6,out,max);
 }
	hint_t h[] = {{GLYPH_LR, "Section"}, {GLYPH_DPAD, "Choose"}, {GLYPH_CONFIRM, "Open"},
	              {GLYPH_TRIANGLE, "Volume menu"}, {GLYPH_SQUARE, "Library"}, {GLYPH_SELECT, "Continue"}, {GLYPH_BACK, "Series"}};
	int n = (g_continue_id || g_series[0] || (g_grid.sel < g_count && g_rows[g_grid.sel].series[0])) ? 6 : 5;
	if (g_series[0]) { h[n] = h[6]; n++; }
	return screen_copy_hints(h, n, out, max);
}

#ifndef NOCT_SHELF_TEST
const screen_t screen_mybooks = {"My Books", enter, leave, update, draw, hints};

const screen_t screen_private = {"Private", enter_private, leave, update, draw, hints};
#endif

static void metadata_close(void)
{
 if(g_meta_request>0) fetch_forget(g_meta_request);
 g_meta_request=0; g_meta_attempt[0]=0;
}
static void metadata_start(const char *series,int force)
{
 char count[16];
 if(!series[0] || g_meta_request>0 || (g_meta_last && g_now-g_meta_last<3500)) return;
 /* Publisher-confirmed starter record; never replace an edition override. */
 if(!force && !str_casecmp(path_basename(series),"Tokyo Ghoul") &&
    db_series_setting_get(series,"expected_volumes",count,sizeof count)==0) {
  db_series_setting_set(series,"expected_volumes","14");
  db_series_setting_set(series,"volume_source","VIZ: https://www.viz.com/manga-books/manga/tokyo-ghoul/all");
  return;
 }
 if(!force && (!strcmp(series,g_meta_attempt) || db_series_setting_get(series,"expected_volumes",count,sizeof count)==1)) return;
 char encoded[768],url[1024];
 if(url_encode_component(encoded,sizeof encoded,path_basename(series))<0) return;
 snprintf(url,sizeof url,"https://api.jikan.moe/v4/manga?q=%s&limit=10",encoded);
 g_meta_request=fetch_get(url,NULL); g_meta_last=g_now; g_meta_explicit=force;
 str_copy(g_meta_attempt,sizeof g_meta_attempt,series);
 str_copy(g_meta_series,sizeof g_meta_series,series);
 if(force) library_tools_message(g_meta_request>0 ? "Checking manga catalogue...":"Network lookup unavailable");
}
static void metadata_update(void)
{
 if(g_meta_request<=0 || fetch_state(g_meta_request)==FETCH_PENDING) return;
 fetch_result_t r; fetch_state_t state=fetch_take(g_meta_request,&r); g_meta_request=0;
 if(state==FETCH_DONE && r.doc.status==200 && r.doc.data) {
  char source[512],count[16]; int n=seriesmeta_count(r.doc.data,r.doc.len,path_basename(g_meta_series),source,sizeof source);
  if(n) { snprintf(count,sizeof count,"%d",n); db_series_setting_set(g_meta_series,"expected_volumes",count); db_series_setting_set(g_meta_series,"volume_source",source); }
  else if(g_meta_explicit) library_tools_message("Volume total unconfirmed. Set Expected volume count if needed");
 }
 fetch_result_free(&r);
}
static void metadata_refresh_gap(void)
{
 g_gap_text[0]=0; str_copy(g_gap_series,sizeof g_gap_series,g_series); g_gap_updated=g_now;

 if(!g_series[0]) return;
 char value[16]; int total;
 if(db_series_setting_get(g_series,"expected_volumes",value,sizeof value)!=1 || !str_page_number(value,1000,&total)) return;
 unsigned char owned[1001]={0};
 int unknown=db_series_volume_numbers(g_series,owned,sizeof owned);
 if(unknown<0) return;
 if(unknown) { str_copy(g_gap_text,sizeof g_gap_text,"Some volume numbers are unconfirmed; use Vol N filenames to check gaps"); return; }

 char text[256]="You appear to be missing volumes: "; int missing=0,shown=0;
 for(int i=1;i<=total;i++) if(!owned[i]) { missing++; if(shown<12) { char num[16]; snprintf(num,sizeof num,"%s%d",shown ? ", ":"",i); str_cat(text,sizeof text,num); shown++; } }
 if(!missing) return;
 if(missing>shown) { char more[32]; snprintf(more,sizeof more," (+%d more)",missing-shown); str_cat(text,sizeof text,more); }
 str_copy(g_gap_text,sizeof g_gap_text,text);
}
static void metadata_draw(void)
{
 if(!g_series[0]) return;
 if(strcmp(g_gap_series,g_series) || g_now-g_gap_updated>=1000) metadata_refresh_gap();
 if(!g_gap_text[0]) return;
 draw_rect(205,464,755,23,theme_bg()); char line[TEXT_LINE_MAX]; text_ellipsize(FONT_PX_LABEL,g_gap_text,730,line); font_draw(218,467,THEME_SUBFONT,FONT_PX_LABEL,line);
}
