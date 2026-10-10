#include "screens/library_tools.h"
#include "screens/screens.h"
#include "core/fnv.h"
#include "core/paths.h"
#include "core/strutil.h"
#include "lib/covers.h"
#include "lib/coverimg.h"
#include "img/scale.h"
#include "net/coversearch.h"
#include "net/fetch.h"
#include "ui/dialog.h"
#include "ui/draw.h"
#include "ui/font.h"
#include "ui/listview.h"
#include "ui/osk.h"
#include "ui/text.h"
#include "ui/theme.h"
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { T_NONE, T_SEARCH, T_RESULTS, T_IMAGE, T_PREVIEW, T_STATS, T_DELETE };
static int g_mode, g_request, g_count, g_selected, g_local;
static db_book_t g_book;
static char g_cover_series[512];
static cover_candidate_t g_candidates[COVERSEARCH_MAX];
static lv_item_t g_items[COVERSEARCH_MAX];
static char g_stat_labels[6][64], g_stat_values[6][64];
static char g_message[160];
static uint64_t g_now, g_message_until;
static vita2d_texture *g_preview;
static uint8_t *g_pixels;

void library_tools_message(const char *text)
{
	str_copy(g_message, sizeof g_message, text);
	g_message_until = g_now + 5000;
}

static void free_preview(void)
{
	if (g_preview) { vita2d_wait_rendering_done(); vita2d_free_texture(g_preview); }
	g_preview = NULL;
	free(g_pixels);
	g_pixels = NULL;
}

void library_tools_close(void)
{
	if (g_request > 0) fetch_forget(g_request);
	g_request = 0;
	if (listview_is_open()) {
		input_t back = {0}; int i;
		back.pressed = IN_BACK;
		listview_update(&back, &i);
	}
	free_preview();
	g_mode = T_NONE;
 g_cover_series[0]=0;
}

int library_tools_busy(void) { return g_mode != T_NONE; }

void library_edit_title(const db_book_t *b)
{
	char title[256];
	if (osk_prompt("Book title", b->title, OSK_TEXT, title, sizeof title) != 1) return;
	if (!title[0]) { library_tools_message("The title cannot be empty"); return; }
	if (db_set_title(b->id, title, b->author) < 0) library_tools_message("Could not save title");
	else library_tools_message("Title saved");
}

void library_cover_search(const db_book_t *b)
{
	char title[256], url[HTTP_URL_MAX];
	if (osk_prompt("Find cover: title and volume", b->title, OSK_TEXT, title, sizeof title) != 1) return;
	if (coversearch_url(title, url, sizeof url) < 0) return;
	library_tools_close();
	g_book = *b;
	g_local = 0;
	g_request = fetch_get(url, NULL);
	if (g_request < 0) library_tools_message("Network search unavailable");
	else g_mode = T_SEARCH;
}

void library_cover_local(const db_book_t *b)
{
	char path[256];
	if (osk_prompt("Cover image path (JPG/PNG, max 4 MB)", "ux0:books/cover.jpg", OSK_TEXT,
	               path, sizeof path) != 1 || !path[0]) return;
	library_tools_close();
	g_book = *b;
	g_local = 1;
	g_request = fetch_file_image(path, COVER_W * 3, COVER_H * 3);
	if (g_request < 0) library_tools_message("Could not load custom cover");
	else g_mode = T_IMAGE;
}

static int make_preview(const img_t *img)
{
	g_pixels = malloc(COVER_BYTES);
	if (!g_pixels || img_cover_crop(img->rgba, img->w, img->h, g_pixels, COVER_W, COVER_H) < 0) return -1;
	g_preview = vita2d_create_empty_texture(COVER_W, COVER_H);
	if (!g_preview) return -1;
	uint8_t *dst = vita2d_texture_get_datap(g_preview);
	unsigned stride = vita2d_texture_get_stride(g_preview);
	for (int y = 0; y < COVER_H; y++) memcpy(dst + (size_t)y * stride, g_pixels + (size_t)y * COVER_W * 4, COVER_W * 4);
	return 0;
}

static int save_cover(void)
{
	char dir[96], file[160], temp[168], hex[17];
	fnv64_hex(g_cover_series[0] ? covers_series_id(g_cover_series) : g_book.id, hex);
	snprintf(dir, sizeof dir, "%s/covers", PATH_DATA_ROOT);
	if (paths_mkdir_p(dir) < 0) return -1;
	snprintf(file, sizeof file, "%s/%s-%llu.bin", dir, hex, (unsigned long long)g_now);
	snprintf(temp, sizeof temp, "%s.part", file);
	if (coverimg_save(temp, g_pixels) < 0 || rename(temp, file)) { remove(temp); return -1; }
	char old[512];
	int had_old = (g_cover_series[0] ? db_series_setting_get(g_cover_series,"cover_path",old,sizeof old) : db_book_setting_get(g_book.id, "cover_path", old, sizeof old));
	if ((g_cover_series[0] ? db_series_setting_set(g_cover_series,"cover_path",file) : db_book_setting_set(g_book.id, "cover_path", file)) < 0) { remove(file); return -1; }
	const char *source=g_local ? "Custom image":"Open Library: https://openlibrary.org";
 if(g_cover_series[0]) db_series_setting_set(g_cover_series,"cover_source",source); else db_book_setting_set(g_book.id,"cover_source",source);
	/* Only remove a previous file created inside the app's cover directory. */
	if (had_old == 1 && strncmp(old, dir, strlen(dir)) == 0 && old[strlen(dir)] == '/') remove(old);
	covers_forget(g_cover_series[0] ? covers_series_id(g_cover_series):g_book.id);
	return 0;
}

/* Recursive delete for a book file or folder comic. Refuses paths outside the books
 * directory so a corrupt record can never eat system files. */
static int remove_tree(const char *path)
{
	SceIoStat st;
	SceUID fd;
	SceIoDirent e;

	if (sceIoGetstat(path, &st) < 0) return -1;
	if (!SCE_S_ISDIR(st.st_mode)) return sceIoRemove(path) < 0 ? -1 : 0;
	fd = sceIoDopen(path);
	if (fd < 0) return -1;
	while (sceIoDread(fd, &e) > 0) {
		char child[1024];
		if (!strcmp(e.d_name, ".") || !strcmp(e.d_name, "..")) continue;
		snprintf(child, sizeof child, "%s/%s", path, e.d_name);
		if (remove_tree(child) < 0) { sceIoDclose(fd); return -1; }
	}
	sceIoDclose(fd);
	return sceIoRmdir(path) < 0 ? -1 : 0;
}

void library_delete_book(const db_book_t *b)
{
	char msg[160];

	library_tools_close();
	g_book = *b;
	snprintf(msg, sizeof msg, "Delete \"%.120s\" from the device?", b->title);
	dialog_open_confirm("Delete book", msg);
	g_mode = T_DELETE;
}

static int delete_confirmed(void)
{
	size_t root = strlen(PATH_BOOKS_DIR);

	if (strncmp(g_book.path, PATH_BOOKS_DIR, root) != 0 ||
	    (g_book.path[root] != '/' && g_book.path[root] != '\0')) {
		library_tools_message("Refusing to delete outside ux0:books");
		return 0;
	}
	if (remove_tree(g_book.path) < 0) {
		library_tools_message("Could not delete the file");
		return 0;
	}
	if (db_book_remove(g_book.id) < 0)
		library_tools_message("File deleted; shelf entry lingers until rescan");
	else
		library_tools_message("Book deleted");
	covers_forget(g_book.id);
	return 1; /* the shelf must reload */
}

void library_stats_open(const db_book_t *b)
{
	db_reading_stats_t stats = {0};
	if (!b && db_reading_stats(&stats) < 0) { library_tools_message("Statistics unavailable"); return; }
	uint64_t ms = b ? b->reading_ms : stats.milliseconds;
	const char *labels[] = {"Active reading time", "Pages viewed", "Finished volumes", "Currently reading", "Books in library", "Time tracking"};
	for (int i = 0; i < 6; i++) { str_copy(g_stat_labels[i], sizeof g_stat_labels[i], labels[i]); g_items[i] = (lv_item_t){g_stat_labels[i], g_stat_values[i], 0}; }
	snprintf(g_stat_values[0], 64, "%llu h %llu min", (unsigned long long)(ms / 3600000), (unsigned long long)(ms / 60000 % 60));
	snprintf(g_stat_values[1], 64, "%lld", (long long)(b ? b->pages_read : stats.pages));
	snprintf(g_stat_values[2], 64, "%d", b ? b->finished : stats.finished);
	snprintf(g_stat_values[3], 64, "%d", b ? (b->last_read > 0 && !b->finished) : stats.reading);
	snprintf(g_stat_values[4], 64, "%d", b ? 1 : stats.total);
	str_copy(g_stat_values[5], 64, "Pauses after 2 min idle");
	g_mode = T_STATS;
	listview_open(b ? b->title : "Reading statistics", g_items, 6, 0, "", 0);
}

static void poll_request(void)
{
	fetch_result_t r;
	fetch_state_t state = fetch_take(g_request, &r);
	if (state == FETCH_PENDING) return;
	g_request = 0;
	if (state != FETCH_DONE) {
		library_tools_message(r.err[0] ? r.err : "Cover request failed");
		fetch_result_free(&r); g_mode = T_NONE; return;
	}
	if (g_mode == T_SEARCH) {
		g_count = coversearch_parse(r.doc.data, r.doc.len, g_candidates, COVERSEARCH_MAX);
		if (g_count <= 0) { library_tools_message("No covers found. Try the series and volume number"); g_mode = T_NONE; }
		else {
			for (int i = 0; i < g_count; i++) g_items[i] = (lv_item_t){g_candidates[i].title, g_candidates[i].author, 0};
			g_mode = T_RESULTS;
			listview_open("Open Library: choose an edition", g_items, g_count, 0, "", 0);
		}
	} else {
		if (make_preview(&r.img) < 0) { free_preview(); library_tools_message("Could not preview cover"); g_mode = T_NONE; }
		else g_mode = T_PREVIEW;
	}
	fetch_result_free(&r);
}

int library_tools_update(const input_t *in)
{
	g_now = in->now_ms;
	if (g_mode == T_SEARCH || g_mode == T_IMAGE) {
		if (in->pressed & IN_BACK) library_tools_close();
		else poll_request();
	} else if (g_mode == T_RESULTS || g_mode == T_STATS) {
		int i = -1;
		lv_action_t a = listview_update(in, &i);
		if (!listview_is_open() && a != LV_PICK) g_mode = T_NONE;
		else if (a == LV_PICK && g_mode == T_STATS) g_mode = T_NONE;
		else if (a == LV_PICK && i >= 0 && i < g_count) {
			char url[256]; g_selected = i;
			if (coversearch_image_url(g_candidates[i].cover_id, url, sizeof url) < 0) { g_mode = T_NONE; return 0; }
			g_request = fetch_image(url, NULL, COVER_W, COVER_H);
			g_mode = g_request > 0 ? T_IMAGE : T_NONE;
		}
	} else if (g_mode == T_DELETE) {
		dialog_result_t r = dialog_update(in);
		if (r == DIALOG_YES) {
			int changed = delete_confirmed();
			g_mode = T_NONE;
			return changed;
		}
		if (r == DIALOG_NO) g_mode = T_NONE;
	} else if (g_mode == T_PREVIEW) {
		if (in->pressed & IN_CONFIRM) {
			int saved = save_cover() == 0;
			library_tools_message(saved ? "Custom cover saved" : "Could not save cover");
			library_tools_close(); return saved;
		}
		if (in->pressed & IN_BACK) {
			free_preview();
			if (g_local) g_mode = T_NONE;
			else { g_mode = T_RESULTS; listview_open("Open Library: choose an edition", g_items, g_count, g_selected, "", 0); }
		}
	}
	return 0;
}

void library_tools_draw(uint64_t now)
{
	listview_draw(now);
	if (g_mode == T_DELETE) dialog_draw();
	if (g_mode == T_SEARCH || g_mode == T_IMAGE) {
		draw_rrect(290, 205, 560, 95, 8, THEME_PANEL);
		font_draw(315, 225, THEME_FONT, FONT_PX_TITLE, g_mode == T_SEARCH ? "Searching Open Library..." : "Loading cover...");
		font_draw(315, 261, THEME_SUBFONT, FONT_PX_LABEL, "Back to cancel");
	}
	if (g_mode == T_PREVIEW && g_preview) {
		draw_rect(245, 35, 665, 450, theme_bg());
		vita2d_draw_texture(g_preview, 270, 95);
		char line[TEXT_LINE_MAX];
		text_ellipsize(FONT_PX_TITLE, g_local ? g_book.title : g_candidates[g_selected].title, 410, line);
		font_draw(470, 125, THEME_FONT, FONT_PX_TITLE, line);
		font_draw(470, 170, THEME_SUBFONT, FONT_PX_LABEL, g_local ? "Custom cover preview" : "Cover source: Open Library");
		font_draw(470, 205, THEME_SUBFONT, FONT_PX_LABEL, "Confirm to use this cover");
		font_draw(470, 238, THEME_SUBFONT, FONT_PX_LABEL, "Back to choose another");
	}
	if (g_message[0] && now < g_message_until) {
		draw_rrect(240, 475, 680, 34, 5, THEME_PANEL);
		char line[TEXT_LINE_MAX]; text_ellipsize(FONT_PX_LABEL, g_message, 650, line);
		font_draw(255, 483, THEME_FONT, FONT_PX_LABEL, line);
	}
}

int library_tools_hints(hint_t *out, int max)
{
	if (g_mode == T_DELETE) return dialog_hints(out, max);
	if (listview_is_open()) return listview_hints(out, max);
	hint_t h[] = {{GLYPH_BACK, "Cancel"}, {GLYPH_CONFIRM, "Use cover"}};
	return screen_copy_hints(h, g_mode == T_PREVIEW ? 2 : 1, out, max);
}

void library_series_cover(const db_book_t *book,const char *series,int online)
{
 db_book_t b=*book; str_copy(b.title,sizeof b.title,path_basename(series));
 if(online) library_cover_search(&b); else library_cover_local(&b);
 if(library_tools_busy()) str_copy(g_cover_series,sizeof g_cover_series,series);
}
