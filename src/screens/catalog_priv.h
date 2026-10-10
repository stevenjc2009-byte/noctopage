/* catalog_priv.h - state shared by the Catalog screen's files (screens/catalog*.c): the
 * list of catalog sources (built-in and added servers), a stack of open OPDS feeds and the
 * detail panel of one book. Feeds and covers are fetched by net/fetch, never on the main
 * loop; downloads go to net/dlq. */
#ifndef NOCT_CATALOG_PRIV_H
#define NOCT_CATALOG_PRIV_H

#include "lib/db.h"
#include "net/fetch.h"
#include "net/opds.h"
#include "net/catalog_api.h"
#include "ui/grid.h"
#include "ui/hintbar.h"
#include "ui/input.h"

#include <stdint.h>

#define CAT_SOURCES_MAX 24
#define CAT_DEPTH 12
#define CAT_X0 (205 + 16)  /* content area left edge */
#define CAT_W (755 - 32)
#define CAT_HEAD_H 56      /* feed title band */
#define CAT_ROW_H 72
#define CAT_ROWS ((521 - CAT_HEAD_H) / CAT_ROW_H)

typedef struct {
	char name[64];
	char url[256];
	char user[64], pass[64];
	int64_t server_id; /* 0 = built in */
} cat_source_t;

/* One open feed. Rows: [previous page] + entries + [next page]. */
typedef struct {
	char url[HTTP_URL_MAX];
	char title[128];
	int fetch_id; /* > 0 while loading */
	opds_feed *feed;
	char err[160];
	int sel, top;
 grid_t grid;
 char filter[256];
 int order[OPDS_MAX_ENTRIES], norder;
} cat_level_t;

typedef enum { ROW_ENTRY, ROW_PREV, ROW_NEXT } cat_row_kind_t;

typedef struct {
	cat_source_t src[CAT_SOURCES_MAX];
	int nsrc, src_sel, src_top; /* rows: sources + "Add server" */
	int source;                 /* index of the open source, -1 at the source list */
	cat_level_t lv[CAT_DEPTH];
	int depth;                  /* open feeds; 0 = the source list is shown */
	int detail;                 /* entry index shown in the detail panel, -1 = none */
	int search_fetch;           /* OpenSearch description being fetched, 0 = none */
	char search_terms[256];
	char toast[160];
	uint64_t toast_until, now;
} catalog_t;

extern catalog_t C;

/* catalog.c */
const http_auth_t *cat_auth(void);
const http_auth_t *cat_auth_url(const char *url);
int cat_builtin_count(void);
void cat_toast(const char *msg);

/* catalog_feed.c */
cat_level_t *cat_top(void);
void cat_push(const char *url, const char *title);
void cat_replace(const char *url); /* previous / next page, in place */
void cat_pop(void);
void cat_reload(void);
void cat_close_all(void);
void cat_poll(void);
void cat_search_start(void);
void cat_filter(cat_level_t *l);
int cat_rows(const cat_level_t *l);
/* Kind of row r, and its entry index for ROW_ENTRY. */
cat_row_kind_t cat_row(const cat_level_t *l, int r, int *entry);

/* catalog_draw.c */
void cat_draw_sources(void);
void cat_draw_feed(void);
void cat_draw_detail(void);
void cat_draw_toast(void);
/* Cover texture for an image URL at a size class (0 = row thumbnail, 1 = detail panel),
 * fetched on demand. NULL while loading or when it failed. */
void *cat_cover(const char *url, int big);
void cat_covers_pump(void);
void cat_covers_clear(void);

/* catalog_entry.c */
void cat_detail_open(int entry);
void cat_detail_update(const input_t *in);
int cat_detail_hints(hint_t *out, int max);
void cat_server_add(void);
void cat_server_edit(int i);

#endif
