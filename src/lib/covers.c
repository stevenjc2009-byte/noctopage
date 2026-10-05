#include "lib/covers.h"

#include "core/fnv.h"
#include "core/log.h"
#include "core/paths.h"
#include "lib/coverimg.h"
#include "ui/texcache.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQ_CAP 32    /* pending requests; the oldest is dropped past this */
#define DONE_CAP 6    /* finished covers waiting for the main thread (6 x 194 KB) */
#define STATE_SLOTS 8192
#define STACK_BYTES (256 * 1024)

enum { ST_NONE = 0, ST_QUEUED = 1, ST_FAILED = 2, ST_DONE = 3 };

typedef struct {
	uint64_t id;
	int kind;
	char path[512];
} req_t;

typedef struct {
	uint64_t id;
	uint8_t *rgba; /* NULL = failed */
} done_t;

/* Book state, main thread only. Open addressing, never deleted (states are overwritten). */
static struct {
	uint64_t id;
	uint8_t st;
	uint8_t used;
} g_state[STATE_SLOTS];

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv_req = PTHREAD_COND_INITIALIZER, g_cv_space = PTHREAD_COND_INITIALIZER;
static req_t g_req[REQ_CAP];
static int g_nreq;
static done_t g_done[DONE_CAP];
static int g_ndone;
static int g_quit, g_started;
static pthread_t g_thread;

static void state_of(uint64_t id, int create, uint8_t **st)
{
	uint32_t h = (uint32_t)(id ^ (id >> 32)) % STATE_SLOTS;
	for (int n = 0; n < STATE_SLOTS; n++, h = (h + 1) % STATE_SLOTS) {
		if (g_state[h].used && g_state[h].id == id) {
			*st = &g_state[h].st;
			return;
		}
		if (!g_state[h].used) {
			if (!create) break;
			g_state[h].used = 1;
			g_state[h].id = id;
			g_state[h].st = ST_NONE;
			*st = &g_state[h].st;
			return;
		}
	}
	*st = NULL;
}

static uint8_t get_state(uint64_t id)
{
	uint8_t *st;
	state_of(id, 0, &st);
	return st ? *st : ST_NONE;
}

static void set_state(uint64_t id, uint8_t v)
{
	uint8_t *st;
	state_of(id, 1, &st);
	if (st) *st = v; /* table full: untracked, at worst re-queued (the queue dedupes) */
}

static void thumb_file(uint64_t id, char *out, size_t cap)
{
	char hex[17];
	fnv64_hex(id, hex);
	snprintf(out, cap, "%s/%s.bin", PATH_THUMBS_DIR, hex);
}

static uint8_t *produce(const req_t *r)
{
	char file[96], err[160] = "";
	uint8_t *px = malloc(COVER_BYTES);
	if (!px) return NULL;
	thumb_file(r->id, file, sizeof file);
	if (coverimg_load(file, px) == 0) return px;
	if (coverimg_make(r->path, r->kind, px, err, sizeof err) != 0) {
		log_printf("covers: %s: %s", r->path, err);
		free(px);
		return NULL;
	}
	if (coverimg_save(file, px) != 0) log_printf("covers: cannot save %s", file);
	return px;
}

static void *worker(void *arg)
{
	(void)arg;
	pthread_mutex_lock(&g_mu);
	for (;;) {
		while (!g_quit && g_nreq == 0) pthread_cond_wait(&g_cv_req, &g_mu);
		if (g_quit) break;
		req_t r = g_req[--g_nreq]; /* newest first */
		pthread_mutex_unlock(&g_mu);
		uint8_t *px = produce(&r);
		pthread_mutex_lock(&g_mu);
		while (!g_quit && g_ndone == DONE_CAP) pthread_cond_wait(&g_cv_space, &g_mu);
		if (g_quit) {
			free(px);
			break;
		}
		g_done[g_ndone].id = r.id;
		g_done[g_ndone].rgba = px;
		g_ndone++;
	}
	pthread_mutex_unlock(&g_mu);
	return NULL;
}

void covers_init(void)
{
	if (g_started) return;
	pthread_attr_t at;
	pthread_attr_init(&at);
	pthread_attr_setstacksize(&at, STACK_BYTES);
	g_quit = 0;
	g_started = pthread_create(&g_thread, &at, worker, NULL) == 0;
	pthread_attr_destroy(&at);
	if (!g_started) log_printf("covers: pthread_create failed");
}

void covers_shutdown(void)
{
	if (!g_started) return;
	pthread_mutex_lock(&g_mu);
	g_quit = 1;
	pthread_cond_broadcast(&g_cv_req);
	pthread_cond_broadcast(&g_cv_space);
	pthread_mutex_unlock(&g_mu);
	pthread_join(g_thread, NULL);
	g_started = 0;
	for (int i = 0; i < g_ndone; i++) free(g_done[i].rgba);
	g_ndone = g_nreq = 0;
}

vita2d_texture *covers_get(uint64_t id, const char *path, int db_kind)
{
	vita2d_texture *t = texcache_get(id);
	if (t) return t;
	uint8_t st = get_state(id);
	if (st == ST_QUEUED || st == ST_FAILED || !g_started || !path || !*path) return NULL;

	pthread_mutex_lock(&g_mu);
	for (int i = 0; i < g_nreq; i++) {
		if (g_req[i].id == id) {
			pthread_mutex_unlock(&g_mu);
			return NULL;
		}
	}
	if (g_nreq == REQ_CAP) { /* drop the oldest; it is re-requested when visible again */
		set_state(g_req[0].id, ST_NONE);
		memmove(&g_req[0], &g_req[1], sizeof g_req[0] * (REQ_CAP - 1));
		g_nreq--;
	}
	req_t *r = &g_req[g_nreq++];
	r->id = id;
	r->kind = db_kind;
	snprintf(r->path, sizeof r->path, "%s", path);
	pthread_cond_signal(&g_cv_req);
	pthread_mutex_unlock(&g_mu);
	set_state(id, ST_QUEUED);
	return NULL;
}

int covers_pump(int max)
{
	done_t got[DONE_CAP];
	int n = 0;
	pthread_mutex_lock(&g_mu);
	while (n < max && n < g_ndone) {
		got[n] = g_done[n];
		n++;
	}
	memmove(&g_done[0], &g_done[n], sizeof g_done[0] * (size_t)(g_ndone - n));
	g_ndone -= n;
	if (n) pthread_cond_signal(&g_cv_space);
	pthread_mutex_unlock(&g_mu);

	for (int i = 0; i < n; i++) {
		if (got[i].rgba && texcache_put_rgba(got[i].id, got[i].rgba, COVER_W, COVER_H))
			set_state(got[i].id, ST_DONE);
		else
			set_state(got[i].id, ST_FAILED); /* render failed or GPU memory ran out: no retry loop */
		free(got[i].rgba);
	}
	return n;
}

void covers_forget(uint64_t id)
{
	char file[96];
	texcache_drop(id);
	set_state(id, ST_NONE);
	thumb_file(id, file, sizeof file);
	remove(file);
}
