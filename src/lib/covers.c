#include "lib/covers.h"

#include "core/affinity.h"
#include "core/fnv.h"
#include "core/log.h"
#include "core/paths.h"
#include "lib/coverimg.h"
#include "lib/db.h"
#include "ui/texcache.h"

#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQ_CAP 64    /* pending requests; the oldest is dropped past this */
#define DONE_CAP 12   /* finished covers waiting for the main thread (12 x 194 KB) */
#define STATE_SLOTS 8192
#define STACK_BYTES (256 * 1024)

enum { ST_NONE = 0, ST_QUEUED = 1, ST_FAILED = 2, ST_DONE = 3 };

typedef struct {
	uint64_t id;
	int kind;
	unsigned revision;
	char path[512];
} req_t;

typedef struct {
	uint64_t id;
	unsigned revision;
	uint8_t *rgba; /* NULL = failed */
} done_t;

/* Book state, main thread only. Open addressing, never deleted (states are overwritten). */
typedef struct {
	uint64_t id;
	uint8_t st;
	uint8_t used;
	uint8_t fails; /* transient-failure retries used (ST_FAILED is terminal past these) */
	unsigned revision;
} state_t;
#define COVER_RETRIES 3
static state_t g_state[STATE_SLOTS];

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv_req = PTHREAD_COND_INITIALIZER, g_cv_space = PTHREAD_COND_INITIALIZER;
static req_t g_req[REQ_CAP];
static int g_nreq;
static done_t g_done[DONE_CAP];
static int g_ndone;
static int g_quit, g_started, g_paused, g_active_cancelled;
static uint64_t g_active_id;
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

static unsigned revision(uint64_t id, int advance)
{
	uint8_t *st;
	state_of(id, 1, &st);
	if (!st) return 0;
	state_t *slot = (state_t *)((char *)st - offsetof(state_t, st));
	if (advance) slot->revision++;
	return slot->revision;
}

/* Count one transient failure; returns 1 when retries are exhausted (main thread only). */
static int note_failure(uint64_t id)
{
	uint8_t *st;
	state_of(id, 1, &st);
	if (!st) return 1;
	state_t *slot = (state_t *)((char *)st - offsetof(state_t, st));
	if (slot->fails >= COVER_RETRIES) return 1;
	slot->fails++;
	return 0;
}

static void reset_failures(uint64_t id)
{
	uint8_t *st;
	state_of(id, 0, &st);
	if (st) ((state_t *)((char *)st - offsetof(state_t, st)))->fails = 0;
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

static uint8_t *produce(const req_t *r, int *cached)
{
	char file[96], err[160] = "";
	uint8_t *px = malloc(COVER_BYTES);
	if (!px) return NULL;
	thumb_file(r->id, file, sizeof file);
	if (coverimg_load(file, px) == 0) { *cached = 1; return px; }
	char custom[512];
	int made = -1;
	if ((r->kind==-1 ? db_series_setting_get(r->path,"cover_path",custom,sizeof custom) : db_book_setting_get(r->id, "cover_path", custom, sizeof custom)) == 1 && custom[0]) {
		made = coverimg_file(custom, px, err, sizeof err);
		if (made < 0) log_printf("covers: custom %s: %s", custom, err);
	}
	if (made < 0 && r->kind!=-1) made = coverimg_make(r->path, r->kind, px, err, sizeof err);
	if (made != 0) {
		log_printf("covers: %s: %s", r->path, err);
		free(px);
		return NULL;
	}
	/* The worker writes generated pixels before publishing the completion. */
	return px;
}

static void *worker(void *arg)
{
	(void)arg;
	affinity_use_all_cores();
	pthread_mutex_lock(&g_mu);
	for (;;) {
		while (!g_quit && (g_paused || g_nreq == 0)) pthread_cond_wait(&g_cv_req, &g_mu);
		if (g_quit) break;
		req_t r = g_req[--g_nreq]; /* newest first */
		g_active_id = r.id; g_active_cancelled = 0;
		/* Wait for room BEFORE decoding: the old order blocked holding a finished
		 * 194 KB buffer that covers_pump never drains while paused. */
		while (!g_quit && g_ndone == DONE_CAP) pthread_cond_wait(&g_cv_space, &g_mu);
		if (g_quit) {
			g_active_id = 0;
			break;
		}
		pthread_mutex_unlock(&g_mu);
		int cached = 0;
		uint8_t *px = produce(&r, &cached);
		char file[96], temp[128] = "";
		if (px && !cached) {
			thumb_file(r.id, file, sizeof file);
			snprintf(temp, sizeof temp, "%s.%u.part", file, r.revision);
			if (coverimg_save(temp, px)) { remove(temp); temp[0] = 0; }
		}
		pthread_mutex_lock(&g_mu);
		if (temp[0]) {
			if (g_quit || g_active_cancelled) remove(temp);
			else if (rename(temp, file)) {
				/* Some Vita filesystems reject replacing an existing file. */
				remove(file);
				if (rename(temp, file)) remove(temp);
			}
		}
		g_active_id = 0;
		if (g_quit) {
			free(px);
			break;
		}
		g_done[g_ndone].id = r.id;
		g_done[g_ndone].revision = r.revision;
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
	g_quit = g_paused = 0;
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
	if (strlen(path) >= sizeof((req_t *)0)->path) return NULL; /* must not truncate below */

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
	r->revision = revision(id, 0);
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
	if (g_paused) { pthread_mutex_unlock(&g_mu); return 0; }
	while (n < max && n < g_ndone) {
		got[n] = g_done[n];
		n++;
	}
	memmove(&g_done[0], &g_done[n], sizeof g_done[0] * (size_t)(g_ndone - n));
	g_ndone -= n;
	if (n) pthread_cond_signal(&g_cv_space);
	pthread_mutex_unlock(&g_mu);

	for (int i = 0; i < n; i++) {
		if (got[i].revision != revision(got[i].id, 0)) { free(got[i].rgba); continue; }
		if (got[i].rgba && texcache_put_rgba(got[i].id, got[i].rgba, COVER_W, COVER_H))
			set_state(got[i].id, ST_DONE);
		else if (note_failure(got[i].id))
			set_state(got[i].id, ST_FAILED); /* still failing after retries: stop asking */
		else
			set_state(got[i].id, ST_NONE); /* transient (e.g. GPU OOM): try again */
		free(got[i].rgba);
	}
	return n;
}

void covers_forget(uint64_t id)
{
	char file[96];
	pthread_mutex_lock(&g_mu);
	revision(id, 1);
	if (g_active_id == id) g_active_cancelled = 1;
	for (int i = 0; i < g_nreq;) {
		if (g_req[i].id == id) {
			memmove(&g_req[i], &g_req[i + 1], sizeof g_req[0] * (size_t)(g_nreq - i - 1));
			g_nreq--;
		} else i++;
	}
	thumb_file(id, file, sizeof file);
	remove(file);
	pthread_mutex_unlock(&g_mu);
	texcache_drop(id);
	set_state(id, ST_NONE);
	reset_failures(id); /* explicit refresh earns fresh retries */
}

void covers_pause(int paused)
{
	pthread_mutex_lock(&g_mu);
	g_paused = paused != 0;
	pthread_cond_signal(&g_cv_req);
	pthread_mutex_unlock(&g_mu);
}

void covers_prioritize(uint64_t id)
{
	pthread_mutex_lock(&g_mu);
	for (int i = 0; i + 1 < g_nreq; i++) if (g_req[i].id == id) {
		req_t r = g_req[i];
		memmove(&g_req[i], &g_req[i + 1], sizeof r * (size_t)(g_nreq - i - 1));
		g_req[g_nreq - 1] = r;
		break;
	}
	pthread_mutex_unlock(&g_mu);
}


void covers_reset(void)
{
	pthread_mutex_lock(&g_mu);
	g_active_cancelled = 1; g_nreq = 0;
	for (int i=0;i<g_ndone;i++) free(g_done[i].rgba);
	g_ndone = 0;
	for (int i=0;i<STATE_SLOTS;i++) if (g_state[i].used) {
		g_state[i].revision++; g_state[i].st = ST_NONE; g_state[i].fails = 0;
	}
	pthread_cond_broadcast(&g_cv_space);
	pthread_mutex_unlock(&g_mu);
	texcache_clear();
}

uint64_t covers_series_id(const char *series) { return fnv64(series,strlen(series)) ^ UINT64_C(0xaffed39020b7ac18); }
vita2d_texture *covers_get_series(const char *series) { return covers_get(covers_series_id(series),series,-1); }
