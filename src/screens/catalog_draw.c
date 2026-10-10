/* catalog_draw.c - drawing for the Catalog screen (source list, feed rows, book panel,
 * toast) and the cover cache: covers are fetched and decoded by net/fetch, uploaded into
 * ui/texcache here in the update step (cat_covers_pump), and only looked up when drawing. */
#include "screens/catalog_priv.h"

#include "core/fnv.h"
#include "net/catalog_downloads.h"
#include "ui/draw.h"
#include "ui/font.h"
#include "ui/texcache.h"
#include "ui/text.h"
#include "ui/theme.h"

#include <stdio.h>
#include <string.h>

#define COVERS 96
#define COVERS_IN_FLIGHT 12
#define COVER_STALE_MS 1500 /* a pending cover nobody drew for this long is dropped */
#define THUMB_W 164
#define THUMB_H 230

typedef struct {
	uint64_t key;
	int fetch_id; /* > 0 pending, 0 free, -1 failed (not retried) */
	uint64_t wanted;
} cover_t;

static cover_t g_cov[COVERS];

static uint64_t cover_key(const char *url, int big)
{
	return ((fnv64(url, strlen(url)) ^ (C.source>=0?(uint64_t)C.src[C.source].server_id*0x9e3779b97f4a7c15ULL ^ fnv64(C.src[C.source].user,strlen(C.src[C.source].user)) ^ fnv64(C.src[C.source].pass,strlen(C.src[C.source].pass)):0)) | 1ULL << 63) ^ (uint64_t)big;
}

/* No GPU work, so safe in the draw step: a miss queues a fetch, and the update step
 * (cat_covers_pump) uploads the result. */
void *cat_cover(const char *url, int big)
{
	if (!url || !url[0]) return NULL;
	uint64_t key = cover_key(url, big);
	vita2d_texture *t = texcache_get(key);
	if (t) return t;
	int free_i = -1, pending = 0;
	for (int i = 0; i < COVERS; i++) {
		if (g_cov[i].fetch_id != 0 && g_cov[i].key == key) {
			g_cov[i].wanted = C.now;
			return NULL;
		}
		if (g_cov[i].fetch_id > 0) pending++;
		if (g_cov[i].fetch_id == 0 && free_i < 0) free_i = i;
	}
	if (pending >= COVERS_IN_FLIGHT) return NULL;
	if (free_i < 0) { /* forget the oldest failure */
		for (int i = 0; i < COVERS; i++)
			if (g_cov[i].fetch_id < 0 && (free_i < 0 || g_cov[i].wanted < g_cov[free_i].wanted))
				free_i = i;
		if (free_i < 0) return NULL;
	}
	int id = fetch_image(url, cat_auth_url(url), big ? 180 : THUMB_W, big ? 270 : THUMB_H);
	if (id <= 0) return NULL;
	g_cov[free_i] = (cover_t){key, id, C.now};
	return NULL;
}

void cat_covers_pump(void)
{
	for (int i = 0; i < COVERS; i++) {
		cover_t *c = &g_cov[i];
		if (c->fetch_id <= 0) continue;
		if (C.now - c->wanted > COVER_STALE_MS) {
			fetch_forget(c->fetch_id);
			c->fetch_id = 0;
			continue;
		}
		fetch_result_t r;
		fetch_state_t st = fetch_take(c->fetch_id, &r);
		if (st == FETCH_PENDING) continue;
		if (st == FETCH_DONE && r.img.rgba &&
		    texcache_put_rgba(c->key, r.img.rgba, r.img.w, r.img.h))
			c->fetch_id = 0;
		else
			c->fetch_id = -1;
		fetch_result_free(&r);
	}
}

void cat_covers_clear(void)
{
	for (int i = 0; i < COVERS; i++) {
		if (g_cov[i].fetch_id > 0) fetch_forget(g_cov[i].fetch_id);
		g_cov[i].fetch_id = 0;
	}
}

/* Texture t scaled to fit w x h, centred in the box; a placeholder when NULL. */
static void cover_box(vita2d_texture *t, int x, int y, int w, int h)
{
	if (!t) {
		draw_rect(x, y, w, h, THEME_COVER);
		return;
	}
	float tw = (float)vita2d_texture_get_width(t), th = (float)vita2d_texture_get_height(t);
	if(tw<=0 || th<=0) return;
 float s = (float)w / tw < (float)h / th ? (float)w / tw : (float)h / th;
	vita2d_draw_texture_scale(t, (float)x + ((float)w - tw * s) / 2,
	                          (float)y + ((float)h - th * s) / 2, s, s);
}

static void line(int x, int y, int w, int px, unsigned colour, const char *s)
{
	char out[TEXT_LINE_MAX];
	text_ellipsize(px, s ? s : "", w, out);
	font_draw(x, y, colour, px, out);
}

static void header(const char *title, const char *right)
{
	int rw = right ? font_width(FONT_PX_LABEL, right) : 0;
	line(CAT_X0, 18, CAT_W - rw - 24, FONT_PX_TITLE, THEME_FONT, title);
	if (right) font_draw(CAT_X0 + CAT_W - rw, 22, THEME_SUBFONT, FONT_PX_LABEL, right);
}

static void centred(int y, int px, unsigned colour, const char *s)
{
	font_draw(CAT_X0 + (CAT_W - font_width(px, s)) / 2, y, colour, px, s);
}

void cat_draw_sources(void)
{
	header("Browse sources", NULL);
	int rows = C.nsrc + 1;
	for (int r = C.src_top; r < rows && r < C.src_top + CAT_ROWS; r++) {
		int y = CAT_HEAD_H + (r - C.src_top) * CAT_ROW_H;
		if (r == C.src_sel) draw_rect(CAT_X0 - 8, y, CAT_W + 16, CAT_ROW_H - 4, THEME_SELECTED);
		if (r == C.nsrc) {
			line(CAT_X0 + 8, y + 12, CAT_W - 16, FONT_PX_TITLE, THEME_FONT,
			     "+  Add custom catalogue\xE2\x80\xA6");
			line(CAT_X0 + 8, y + 40, CAT_W - 16, FONT_PX_LABEL, THEME_SUBFONT,
			     "OPDS (Komga, Kavita, Calibre) or an Archive collection link");
		} else {
			const cat_source_t *s = &C.src[r];
			line(CAT_X0 + 8, y + 12, CAT_W - 16, FONT_PX_TITLE, THEME_FONT, s->name);
			line(CAT_X0 + 8, y + 40, CAT_W - 16, FONT_PX_LABEL, THEME_SUBFONT, s->url);
		}
		if (r == C.src_sel) draw_selector(CAT_X0 - 8, y, CAT_W + 16, CAT_ROW_H - 4, C.now);
	}
}

/* "EPUB, PDF" for the downloadable links, "Not free to download" when there are none. */
static void formats(const opds_entry *e, char *out, size_t cap)
{
	out[0] = '\0';
	int any = 0;
	for (int i = 0; i < e->nacq; i++) {
		if (e->acq[i].kind != OPDS_ACQ_GENERIC && e->acq[i].kind != OPDS_ACQ_OPEN_ACCESS) continue;
		char tok[16];
		snprintf(tok, sizeof tok, "%s", opds_acq_format(&e->acq[i]));
		for (char *p = tok; *p; p++)
			if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
		if (!strcmp(tok,"OTHER") || strstr(out, tok)) continue;
		size_t n = strlen(out);
		snprintf(out + n, cap - n, "%s%s", any ? ", " : "", tok);
		any = 1;
	}
	if (!any && e->nacq > 0) snprintf(out, cap, "Not free to download");
}

static void card(void *user,int r,grid_item_t *out)
{
 const cat_level_t *l=user; int index;
 cat_row_kind_t k=cat_row(l,r,&index);
 out->progress=-1;
 if(k!=ROW_ENTRY) { out->title=k==ROW_PREV?"Previous page":"Next page"; out->badge="PAGE"; return; }
 const opds_entry *e=&l->feed->entries[index];
 out->title=e->title; out->cover=cat_cover(e->thumb?e->thumb:e->cover,0);
 const opds_acq *one;
 out->badge=catalog_downloads(e,1,&one,1)?"DOWNLOAD":e->pse_href?"READ":e->nav_href?"OPEN":NULL;
}

void cat_draw_feed(void)
{
	const cat_level_t *l = cat_top();
	char right[48];
	right[0] = '\0';
	if (l->fetch_id > 0)
		snprintf(right, sizeof right, "Loading\xE2\x80\xA6");
	else if (l->feed && l->feed->total > 0)
		snprintf(right, sizeof right, "%lld books", l->feed->total);
	line(CAT_X0,18,450,FONT_PX_TITLE,THEME_FONT,l->title);
 if(right[0]) line(CAT_X0,492,450,FONT_PX_LABEL,THEME_SUBFONT,right);
	if (!l->feed) {
		if (l->fetch_id > 0) return;
		centred(220, FONT_PX_TITLE, THEME_FONT, "This catalog could not be loaded");
		centred(256, FONT_PX_LABEL, THEME_SUBFONT, l->err);
		return;
	}
 draw_rrect(694,14,250,36,5,THEME_PANEL);
 line(706,22,226,FONT_PX_LABEL,THEME_SELECTOR,l->filter[0]?l->filter:"Search comics…");
 if(cat_rows(l)==0) centred(240,FONT_PX_TITLE,THEME_SUBFONT,"No matching comics");
 grid_draw((grid_t *)&l->grid,card,(void *)l,C.now);
 if(l->feed->search_href==NULL && catalog_api_kind(l->url)==0)
  line(CAT_X0,492,CAT_W,FONT_PX_LABEL,THEME_SUBFONT,"Search filters this page; clear the search text to show all.");

}

void cat_draw_detail(void)
{
	const cat_level_t *l = cat_top();
	if (!l->feed || C.detail >= l->feed->nentries) return;
	const opds_entry *e = &l->feed->entries[C.detail];
	header(l->title, NULL);
	vita2d_texture *t = cat_cover(e->cover ? e->cover : e->thumb, 1);
	if (!t) t = cat_cover(e->thumb ? e->thumb : e->cover, 0); /* the row's copy meanwhile */
	cover_box(t, CAT_X0, CAT_HEAD_H + 8, 180, 270);
 const opds_acq *available;
 int can_download=catalog_downloads(e,1,&available,1);
 draw_rrect(CAT_X0,354,180,44,5,THEME_PANEL);
 line(CAT_X0+12,366,156,FONT_PX_LABEL,THEME_SELECTOR,can_download?"Download":e->pse_href?"Read now":"No free download");
	int x = CAT_X0 + 200, w = CAT_W - 200, y = CAT_HEAD_H + 8;
	static char lines[12][TEXT_LINE_MAX];
	int n = text_wrap(FONT_PX_TITLE, e->title, w, 3, lines);
	for (int i = 0; i < n; i++, y += 28) font_draw(x, y, THEME_FONT, FONT_PX_TITLE, lines[i]);
	if (e->author) {
		line(x, y + 2, w, FONT_PX_LABEL, THEME_SUBFONT, e->author);
		y += 26;
	}
	char f[64];
	formats(e, f, sizeof f);
	line(x, y + 2, w, FONT_PX_LABEL, THEME_SELECTOR, f);
	y += 36;
	n = text_wrap(FONT_PX_LABEL, e->summary ? e->summary : "", w, 12, lines);
	for (int i = 0; i < n && y < 500; i++, y += 22)
		font_draw(x, y, THEME_FONT, FONT_PX_LABEL, lines[i]);
}

void cat_draw_toast(void)
{
	if (!C.toast[0] || C.now >= C.toast_until) return;
	char msg[TEXT_LINE_MAX];
	text_ellipsize(FONT_PX_LABEL, C.toast, CAT_W - 40, msg);
	int w = font_width(FONT_PX_LABEL, msg) + 20;
	draw_rrect(950 - w, 487, w, 28, 4, THEME_PANEL);
	font_draw(960 - w, 487 + (28 - font_height(FONT_PX_LABEL)) / 2, THEME_FONT, FONT_PX_LABEL,
	          msg);
}
