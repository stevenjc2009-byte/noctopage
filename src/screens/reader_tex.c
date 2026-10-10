/* reader_tex.c - GPU copies of the reader's decoded pages. A few pages stay uploaded (the
 * current one and its neighbours); the least recently used is freed past RTEX_SLOTS.
 * MAIN THREAD ONLY: vita2d textures are created and freed here, and only from the update
 * step (rtex_fetch), never while a frame is being drawn: a texture freed mid-scene may
 * already be referenced by queued draw commands. Drawing uses rtex_peek. */
#include "screens/reader_priv.h"

#include "core/log.h"

#include <string.h>
#include <vita2d.h>

#define RTEX_SLOTS 6
/* Upload budget per frame instead of a fixed count: one 5 MB page, or several small
 * ones, so fast paging catches up without stalling the frame on copies. */
#define UPLOAD_BYTES_PER_FRAME (6u * 1024u * 1024u)
/* Retired textures wait this many frames before freeing, so eviction never stalls on
 * vita2d_wait_rendering_done in the middle of paging. */
#define RETIRE_FRAMES 2
#define RETIRE_N (RTEX_SLOTS + 2)

typedef struct {
	rtex_t t;
	int used;
	uint32_t tick;
} slot_t;

static slot_t g_slot[RTEX_SLOTS];
static uint32_t g_tick;
static size_t g_upload_left = UPLOAD_BYTES_PER_FRAME;
static int g_uploaded;
static struct {
	void *tex;
	int age;
} g_retire[RETIRE_N];

static void free_now(void *t)
{
	if (!t) return;
	vita2d_wait_rendering_done(); /* the GPU may still be drawing the last frame with it */
	vita2d_free_texture(t);
}

static void retire(void *t)
{
	int i;

	if (!t) return;
	for (i = 0; i < RETIRE_N; i++)
		if (!g_retire[i].tex) {
			g_retire[i].tex = t;
			g_retire[i].age = 0;
			return;
		}
	free_now(t); /* retire list full (should not happen): stall rather than leak */
}

static void free_slot(slot_t *s)
{
	int i;

	if (!s->used) return;
	for (i = 0; i < s->t.ntiles; i++)
		retire(s->t.tex[i]);
	memset(s, 0, sizeof *s);
}

void rtex_clear(void)
{
	int i, k;

	/* Book switch: free at once (with the stall) rather than retiring, so the old
	 * book's textures do not sit in GPU memory next to the new book's. */
	for (i = 0; i < RTEX_SLOTS; i++) {
		if (!g_slot[i].used) continue;
		for (k = 0; k < g_slot[i].t.ntiles; k++)
			free_now(g_slot[i].t.tex[k]);
		memset(&g_slot[i], 0, sizeof g_slot[i]);
	}
	for (i = 0; i < RETIRE_N; i++) {
		free_now(g_retire[i].tex);
		g_retire[i].tex = NULL;
		g_retire[i].age = 0;
	}
}

void rtex_new_frame(void)
{
	int i;

	g_upload_left = UPLOAD_BYTES_PER_FRAME;
	g_uploaded = 0;
	for (i = 0; i < RETIRE_N; i++)
		if (g_retire[i].tex && ++g_retire[i].age >= RETIRE_FRAMES) {
			vita2d_free_texture(g_retire[i].tex); /* aged out: no wait needed */
			g_retire[i].tex = NULL;
			g_retire[i].age = 0;
		}
}

static slot_t *find(int page, int box_w, int box_h)
{
	for (int i = 0; i < RTEX_SLOTS; i++) {
		rtex_t *t = &g_slot[i].t;
		if (g_slot[i].used && t->page == page && t->box_w == box_w && t->box_h == box_h)
			return &g_slot[i];
	}
	return NULL;
}

static slot_t *victim(void)
{
	slot_t *v = &g_slot[0];
	for (int i = 0; i < RTEX_SLOTS; i++) {
		if (!g_slot[i].used) return &g_slot[i];
		if (g_slot[i].tick < v->tick) v = &g_slot[i];
	}
	return v;
}

static vita2d_texture *upload_tile(const uint8_t *rgba, int w, int h)
{
	vita2d_texture *t = vita2d_create_empty_texture((unsigned)w, (unsigned)h);
	if (!t) return NULL;
	uint8_t *dst = vita2d_texture_get_datap(t);
	unsigned stride = vita2d_texture_get_stride(t);
	if (stride == (unsigned)w * 4) {
		memcpy(dst, rgba, (size_t)w * (size_t)h * 4);
	} else {
		for (int y = 0; y < h; y++)
			memcpy(dst + (size_t)y * stride, rgba + (size_t)y * (size_t)w * 4, (size_t)w * 4);
	}
	vita2d_texture_set_filters(t, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
	return t;
}

/* Upload p into s. On GPU memory exhaustion frees the oldest other page and tries again. */
static int fill(slot_t *s, const loader_page_t *p, int box_w, int box_h)
{
	s->used = 1;
	s->t.page = p->page;
	s->t.box_w = box_w;
	s->t.box_h = box_h;
	s->t.w = p->w;
	s->t.h = p->h;
	s->t.ntiles = 0;
	if (p->ntiles > RTEX_TILES_MAX) {
		log_printf("reader: page %d has %d tiles, max %d", p->page, p->ntiles, RTEX_TILES_MAX);
		return -1;
	}
	for (int i = 0; i < p->ntiles; i++) {
		vita2d_texture *t = upload_tile(p->tiles[i].rgba, p->w, p->tiles[i].h);
		for (int tries = 0; !t && tries < RTEX_SLOTS; tries++) {
			slot_t *old = NULL;
			for (int k = 0; k < RTEX_SLOTS; k++)
				if (g_slot[k].used && &g_slot[k] != s && (!old || g_slot[k].tick < old->tick))
					old = &g_slot[k];
			if (!old) break;
			free_slot(old);
			t = upload_tile(p->tiles[i].rgba, p->w, p->tiles[i].h);
		}
		if (!t) {
			log_printf("reader: out of GPU memory for page %d", p->page);
			return -1;
		}
		s->t.tex[i] = t;
		s->t.tile_y[i] = p->tiles[i].y;
		s->t.tile_h[i] = p->tiles[i].h;
		s->t.ntiles = i + 1;
	}
	return 0;
}

const rtex_t *rtex_peek(int page, int box_w, int box_h)
{
	slot_t *s = find(page, box_w, box_h);
	if (!s) return NULL;
	s->tick = ++g_tick;
	return &s->t;
}

const rtex_t *rtex_fetch(int page, int box_w, int box_h)
{
	slot_t *s = find(page, box_w, box_h);
	if (s) {
		s->tick = ++g_tick;
		return &s->t;
	}
	if (!R.ld) return NULL;
	const loader_page_t *p = loader_get(R.ld, page, box_w, box_h);
	if (!p) return NULL;
	/* Second and later uploads share the frame budget; the first always goes through
	 * so a single huge page cannot starve every frame. */
	size_t cost = (size_t)p->w * (size_t)p->h * 4;
	if (g_uploaded && cost > g_upload_left) {
		loader_release(R.ld, p);
		return NULL;
	}
	s = victim();
	free_slot(s);
	int ok = fill(s, p, box_w, box_h) == 0;
	reader_note_size(page, p->src_w, p->src_h);
	reader_crop_note(p);
	loader_release(R.ld, p);
	g_uploaded = 1;
	g_upload_left = cost > g_upload_left ? 0 : g_upload_left - cost;
	if (!ok) {
		free_slot(s);
		return NULL;
	}
	s->tick = ++g_tick;
	return &s->t;
}

void rtex_draw(const rtex_t *t, float x, float y, float dst_w)
{
	float s = dst_w / (float)t->w;
	for (int i = 0; i < t->ntiles; i++) {
		float ty = y + (float)t->tile_y[i] * s;
		float th = (float)t->tile_h[i] * s;
		if (ty + th < 0 || ty > SCREEN_H) continue;
		vita2d_draw_texture_scale(t->tex[i], x, ty, s, s);
	}
}

void rtex_draw_crop(const rtex_t *t, float x, float y, float w, float h, int rot, crop_t c)
{
	rot &= 3;
	float cl = c.l * (float)t->w, cr = c.r * (float)t->w;
	float ct = c.t * (float)t->h, cb = c.b * (float)t->h;
	if (cr - cl < 1.0f || cb - ct < 1.0f) return;
	float s = ((rot & 1) ? h : w) / (cr - cl); /* screen px per texture px */
	float mx = (cl + cr) / 2, my = (ct + cb) / 2; /* kept part's centre, texture px */
	float sx = x + w / 2, sy = y + h / 2;         /* ...lands on the rectangle's centre */
	float rad = (float)rot * 1.5707963f;
	for (int i = 0; i < t->ntiles; i++) {
		float a = (float)t->tile_y[i], b = a + (float)t->tile_h[i];
		if (a < ct) a = ct;
		if (b > cb) b = cb;
		if (b - a < 0.5f) continue;
		/* This tile part's centre relative to the kept centre, scaled, then turned clockwise. */
		float dx = ((cl + cr) / 2 - mx) * s, dy = ((a + b) / 2 - my) * s, ox = dx, oy = dy;
		for (int k = 0; k < rot; k++) {
			float nx = -oy;
			oy = ox;
			ox = nx;
		}
		vita2d_draw_texture_part_scale_rotate(t->tex[i], sx + ox, sy + oy, cl,
		                                      a - (float)t->tile_y[i], cr - cl, b - a, s, s, rad);
	}
}
