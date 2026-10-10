/* reader_priv.h - state shared by the comic reader's files (screens/reader*.c). */
#ifndef NOCT_READER_PRIV_H
#define NOCT_READER_PRIV_H

#include "lib/db.h"
#include "reader/crop.h"
#include "reader/fit.h"
#include "reader/loader.h"
#include "reader/spread.h"
#include "reader/zones.h"
#include "ui/hintbar.h"
#include "ui/input.h"

#include <stdint.h>

#define SCREEN_W 960
#define SCREEN_H 544
#define OVERLAY_BAR_H 80
#define STRIP_GAP 0

/* Decode boxes. Normal pages are decoded 960 wide (taller pages keep their aspect up to
 * the height cap); zoomed-in views ask for a sharper 2048-wide copy. */
#define BOX_W 960
#define BOX_W_HIRES 2048
#define BOX_H_MAX 16384
#define HIRES_ZOOM 1.25f

typedef enum { RM_LTR, RM_RTL, RM_VERTICAL, RM_COUNT } read_mode_t;

#define DIM_LEVELS 4 /* 0, 25, 50, 75 % */

typedef struct {
	db_book_t book;
	loader_t *ld;
	int count;        /* pages, 0 until the loader is READY */
	int page;         /* current page (paged) or page at the screen centre (strip) */
	read_mode_t mode;
	fit_mode_t fit;
	int dim;          /* 0..DIM_LEVELS-1 */
	int overlay;
	int stream;       /* OPDS-PSE stream: no library row, so no progress or bookmarks */
	int rot;          /* clockwise quarter turns, paged modes only (per book) */
	int spreads;      /* 0 off, 1 double pages, 2 double pages with the cover alone (per book) */
	int crop;         /* crop white borders, paged modes only (per book) */
	uint64_t now, stick_at;

	/* paged */
	fit_view_t view;
	int pending_pan;  /* 1 = pan to the reading start, -1 = to the end, once the size is known */
	float pinch_zoom; /* zoom when the pinch began */

	/* strip */
	int *pw, *ph;       /* page sizes, estimated until known */
	uint8_t *known;     /* 1 = pw/ph are real */
	strip_item_t *items;
	float strip_total, scroll, strip_zoom, strip_x;
	int strip_dirty;    /* sizes changed: lay out again keeping the anchor page */

	tapsm_t tap;
	int pinching;       /* a pinch is in progress (pinch_zoom / strip zoom recorded) */
	float swipe_dx;     /* horizontal drag of the current touch on a page that fits */
	uint32_t prev_held; /* buttons held last frame, to tell a new press from key repeat */
	int seeking;        /* finger on the overlay slider */
	int seek_page;

	/* layout (reader_layout.c) */
	spread_t *sp;       /* spreads of all pages; unknown sizes count as portrait */
	int nsp;
	int sp_dirty;       /* rebuild before the next use (sizes, mode or setting changed) */
	crop_t *cropbox;    /* per page, source orientation */
	uint8_t *crop_known;

	int saved_page;     /* last page written to the database */
	uint64_t changed_at;
	char err[160];      /* book could not be opened */
	int failed_page;    /* page that could not be decoded, -1 = none */
} reader_t;

extern reader_t R;

/* reader_tex.c: GPU copies of decoded pages (MAIN THREAD ONLY). */
typedef struct {
	int page, box_w, box_h;
	int w, h;            /* rendered size */
	int ntiles;
	int tile_y[8], tile_h[8];
	void *tex[8];        /* vita2d_texture * */
} rtex_t;
#define RTEX_TILES_MAX 8
void rtex_clear(void);
/* UPDATE STEP ONLY: the uploaded page, uploading it from the loader if it is ready and the
 * per-frame upload budget allows (may free other pages). NULL when not available yet. */
const rtex_t *rtex_fetch(int page, int box_w, int box_h);
/* DRAW STEP: the page if already uploaded, else NULL. Never uploads or frees. */
const rtex_t *rtex_peek(int page, int box_w, int box_h);
void rtex_new_frame(void);
/* Draw a page with its top-left at (x, y), scaled so its width is dst_w. */
void rtex_draw(const rtex_t *t, float x, float y, float dst_w);

/* reader_view.c: navigation and layout. */
int reader_rtl(void);
int reader_box_w(void);           /* BOX_W or BOX_W_HIRES for the current zoom */
void reader_goto(int page, int pan); /* pan: 1 start, -1 end, 0 keep */
void reader_step(int dir);        /* +1 next, -1 previous in reading order */
void reader_strip_relayout(void);
int reader_strip_alloc(int n);
void reader_strip_free(void);
void reader_strip_scroll_to(int page);
void reader_strip_zoom_at(float zoom, float fx, float fy);
void reader_strip_pan(float dx, float dy);
void reader_note_size(int page, int w, int h);
void reader_wish(void);           /* tell the loader what to decode next */
void reader_save(int force);

/* reader_draw.c */
void reader_draw_pages(void);
void reader_draw_overlay(void);
const char *reader_mode_name(void);
const char *reader_fit_name(void);
int reader_overlay_hit_slider(int x, int y);
int reader_slider_page(int x);

/* reader_menu.c: the overlay menu, contents and bookmarks (modal over the page). */
void reader_menu_open(void);
int reader_menu_is_open(void);
void reader_menu_update(const input_t *in);
void reader_menu_draw(void);
int reader_menu_hints(hint_t *out, int max);
void reader_menu_reset(void);

/* Page size as shown: swapped when rotated a quarter turn (paged modes). */
int reader_page_w(int page);
int reader_page_h(int page);

/* reader_layout.c: per-book settings, double-page spreads, cropped sizes. */
/* The book's own value of a comic setting, else the default from Settings (prefs "reader"). */
int reader_setting(const char *key, int def, int lo, int hi);
void reader_setting_save(const char *key, int value);
int reader_layout_alloc(int n);
void reader_layout_free(void);
int reader_spreads_on(void);          /* spreads apply now: setting on, paged, not rotated */
void reader_spreads_rebuild(void);    /* UPDATE STEP; clears sp_dirty */
int reader_spread_first(int page);    /* first page of the spread holding page */
int reader_spread_step(int page, int dir); /* first page of the next/previous spread, -1 = none */
int reader_shown(int out[2]);         /* pages on screen with R.page, left to right */
void reader_view_size(float *w, float *h); /* size of what is shown, as fit_* sees it */
/* Crop box of a page in source orientation ({0,0,1,1} when off or not measured yet). */
crop_t reader_crop_src(int page);
/* Measure a freshly decoded page's border if cropping is on (rtex upload, main thread). */
void reader_crop_note(const loader_page_t *p);
/* Draw a page cropped to c (source orientation) and rotated rot quarter turns, so the kept
 * part fills the on-screen rectangle (x, y, w, h). */
void rtex_draw_crop(const rtex_t *t, float x, float y, float w, float h, int rot, crop_t c);

/* reader_input.c */
void reader_handle_buttons(const input_t *in);
void reader_handle_touch(const input_t *in);

#endif
