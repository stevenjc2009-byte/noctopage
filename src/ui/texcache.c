#include "ui/texcache.h"

#include <string.h>

typedef struct {
	uint64_t key;
	vita2d_texture *tex;
	uint32_t used; /* tick of last use; 0 = empty slot */
} slot_t;

static slot_t g_slots[TEXCACHE_CAP];
static uint32_t g_tick;

/* Retired textures: freed after RETIRE_FRAMES presented frames instead of stalling
 * on vita2d_wait_rendering_done during scroll evictions. */
#define RETIRE_FRAMES 2
static struct {
	vita2d_texture *tex;
	int age;
} g_retire[TEXCACHE_CAP];

/* The GPU may still be drawing the previous frame with this texture. */
static void free_tex(vita2d_texture *t)
{
	if (!t) return;
	vita2d_wait_rendering_done();
	vita2d_free_texture(t);
}

static void retire_tex(vita2d_texture *t)
{
	int i;

	if (!t) return;
	for (i = 0; i < TEXCACHE_CAP; i++)
		if (!g_retire[i].tex) {
			g_retire[i].tex = t;
			g_retire[i].age = 0;
			return;
		}
	free_tex(t); /* retire list full (should not happen): stall rather than leak */
}

void texcache_new_frame(void)
{
	int i;

	for (i = 0; i < TEXCACHE_CAP; i++)
		if (g_retire[i].tex && ++g_retire[i].age >= RETIRE_FRAMES) {
			vita2d_free_texture(g_retire[i].tex);
			g_retire[i].tex = NULL;
			g_retire[i].age = 0;
		}
}

static slot_t *find(uint64_t key)
{
	for (int i = 0; i < TEXCACHE_CAP; i++)
		if (g_slots[i].used && g_slots[i].key == key)
			return &g_slots[i];
	return NULL;
}

vita2d_texture *texcache_get(uint64_t key)
{
	slot_t *s = find(key);
	if (!s) return NULL;
	s->used = ++g_tick;
	return s->tex;
}

vita2d_texture *texcache_put_rgba(uint64_t key, const uint8_t *rgba, int w, int h)
{
	if (!rgba || w <= 0 || h <= 0) return NULL;
	vita2d_texture *t = vita2d_create_empty_texture((unsigned int)w, (unsigned int)h);
	if (!t) return NULL;
	uint8_t *dst = vita2d_texture_get_datap(t);
	unsigned int stride = vita2d_texture_get_stride(t); /* bytes, not pixels */
	if (stride == (unsigned)w * 4) {
		memcpy(dst, rgba, (size_t)w * (size_t)h * 4);
	} else {
		for (int y = 0; y < h; y++)
			memcpy(dst + (size_t)y * stride, rgba + (size_t)y * (size_t)w * 4, (size_t)w * 4);
	}

	slot_t *s = find(key);
	if (!s) {
		s = &g_slots[0];
		for (int i = 0; i < TEXCACHE_CAP; i++) {
			if (!g_slots[i].used) { s = &g_slots[i]; break; }
			if (g_slots[i].used < s->used) s = &g_slots[i];
		}
	}
	retire_tex(s->tex);
	s->key = key;
	s->tex = t;
	s->used = ++g_tick;
	return t;
}

void texcache_drop(uint64_t key)
{
	slot_t *s = find(key);
	if (!s) return;
	retire_tex(s->tex);
	memset(s, 0, sizeof *s);
}

void texcache_clear(void)
{
	for (int i = 0; i < TEXCACHE_CAP; i++) {
		free_tex(g_slots[i].tex);
		memset(&g_slots[i], 0, sizeof g_slots[i]);
	}
}
