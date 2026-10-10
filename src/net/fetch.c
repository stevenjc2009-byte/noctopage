#include "net/fetch.h"

#include "core/affinity.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STACK_BYTES (256 * 1024)

enum { SL_FREE = 0, SL_QUEUED, SL_RUNNING, SL_DONE, SL_FAILED };

typedef struct {
	int id, st;
	int forgotten; /* running when forgotten: the worker frees it when the transfer ends */
	unsigned seq;  /* queue order; the highest runs first */
	int img, max_w, max_h;
	char *url, *user, *pass;
	volatile int cancel;
	fetch_result_t res;
} slot_t;

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static slot_t g_slots[FETCH_SLOTS];
static int g_next_id = 1, g_quit, g_started;
static unsigned g_seq;
static pthread_t g_thread;

static char *dup_or_null(const char *s)
{
	if (!s || !*s) return NULL;
	size_t n = strlen(s) + 1;
	char *d = malloc(n);
	if (d) memcpy(d, s, n);
	return d;
}

void fetch_result_free(fetch_result_t *r)
{
	if (!r) return;
	http_doc_free(&r->doc);
	img_free(&r->img);
	memset(r, 0, sizeof *r);
}

/* Caller holds g_mu (or the worker is stopped). */
static void slot_clear(slot_t *s)
{
	free(s->url);
	free(s->user);
	free(s->pass);
	fetch_result_free(&s->res);
	memset(s, 0, sizeof *s);
}

static slot_t *find(int id)
{
	if (id <= 0) return NULL;
	for (int i = 0; i < FETCH_SLOTS; i++)
		if (g_slots[i].st != SL_FREE && g_slots[i].id == id && !g_slots[i].forgotten) return &g_slots[i];
	return NULL;
}

static slot_t *newest_queued(void)
{
	slot_t *best = NULL;
	for (int i = 0; i < FETCH_SLOTS; i++)
		if (g_slots[i].st == SL_QUEUED && (!best || g_slots[i].seq > best->seq)) best = &g_slots[i];
	return best;
}

/* Worker, without the lock: the slot's request fields do not change while it runs. */
static int local_body(const char *path, http_doc_t *out, char *err)
{
	FILE *f = fopen(path, "rb");
	if (!f) { strcpy(err, "Cannot open cover image"); return -1; }
	if (fseek(f, 0, SEEK_END) || ftell(f) <= 0 || ftell(f) > FETCH_MAX_BODY) {
		fclose(f); strcpy(err, "Cover image must be under 4 MB"); return -1;
	}
	size_t n = (size_t)ftell(f);
	char *data = malloc(n + 1);
	if (!data || fseek(f, 0, SEEK_SET) || fread(data, 1, n, f) != n) {
		free(data); fclose(f); strcpy(err, "Cannot read cover image"); return -1;
	}
	fclose(f); data[n] = 0; out->data = data; out->len = n;
	return 0;
}

static int run(slot_t *s, fetch_result_t *r)
{
	http_auth_t a = { s->user, s->pass };
	memset(r, 0, sizeof *r);
	if (s->img == 2) {
		if (local_body(s->url, &r->doc, r->err) < 0) return -1;
	} else if (http_get(s->url, &a, FETCH_MAX_BODY, &s->cancel, &r->doc, r->err, sizeof r->err) != 0) return -1;
	if (!s->img) return 0;
	int rc = s->cancel ? -1 : img_decode((const uint8_t *)r->doc.data, r->doc.len, s->max_w, s->max_h,
	                                      &r->img, r->err, sizeof r->err);
	if (s->cancel) strcpy(r->err, "Cancelled");
	free(r->doc.data);
	r->doc.data = NULL;
	r->doc.len = 0;
	return rc;
}

static void *worker(void *arg)
{
	(void)arg;
	affinity_use_all_cores();
	pthread_mutex_lock(&g_mu);
	for (;;) {
		slot_t *s = NULL;
		while (!g_quit && !(s = newest_queued())) pthread_cond_wait(&g_cv, &g_mu);
		if (g_quit) break;
		s->st = SL_RUNNING;
		s->cancel = 0;
		pthread_mutex_unlock(&g_mu);
		fetch_result_t r;
		int rc = run(s, &r);
		pthread_mutex_lock(&g_mu);
		if (s->forgotten) {
			fetch_result_free(&r);
			slot_clear(s);
		} else {
			s->res = r;
			s->st = rc == 0 ? SL_DONE : SL_FAILED;
		}
	}
	pthread_mutex_unlock(&g_mu);
	return NULL;
}

int fetch_start(void)
{
	if (g_started) return 0;
	pthread_attr_t at;
	pthread_attr_init(&at);
	pthread_attr_setstacksize(&at, STACK_BYTES);
	g_quit = 0;
	g_started = pthread_create(&g_thread, &at, worker, NULL) == 0;
	pthread_attr_destroy(&at);
	return g_started ? 0 : -1;
}

void fetch_stop(void)
{
	if (!g_started) return;
	pthread_mutex_lock(&g_mu);
	g_quit = 1;
	for (int i = 0; i < FETCH_SLOTS; i++)
		if (g_slots[i].st == SL_RUNNING) g_slots[i].cancel = 1;
	pthread_cond_broadcast(&g_cv);
	pthread_mutex_unlock(&g_mu);
	pthread_join(g_thread, NULL);
	g_started = 0;
	for (int i = 0; i < FETCH_SLOTS; i++) slot_clear(&g_slots[i]);
}

static int queue(const char *url, const http_auth_t *auth, int img, int max_w, int max_h)
{
	if (!url || !*url) return -1;
	pthread_mutex_lock(&g_mu);
	slot_t *s = NULL;
	for (int i = 0; i < FETCH_SLOTS && g_started && !g_quit; i++) {
		if (g_slots[i].st == SL_FREE) {
			s = &g_slots[i];
			break;
		}
	}
	if (!s) {
		pthread_mutex_unlock(&g_mu);
		return -1;
	}
	s->url = dup_or_null(url);
	s->user = auth ? dup_or_null(auth->user) : NULL;
	s->pass = auth ? dup_or_null(auth->pass) : NULL;
	if (!s->url || (auth && auth->user && *auth->user && !s->user) ||
	    (auth && auth->pass && *auth->pass && !s->pass)) {
		slot_clear(s);
		pthread_mutex_unlock(&g_mu);
		return -1;
	}
	s->id = g_next_id++;
	if (g_next_id <= 0) g_next_id = 1;
	s->seq = ++g_seq;
	s->img = img;
	s->max_w = max_w;
	s->max_h = max_h;
	s->st = SL_QUEUED;
	int id = s->id;
	pthread_cond_signal(&g_cv);
	pthread_mutex_unlock(&g_mu);
	return id;
}

int fetch_get(const char *url, const http_auth_t *auth)
{
	return queue(url, auth, 0, 0, 0);
}

int fetch_image(const char *url, const http_auth_t *auth, int max_w, int max_h)
{
	return queue(url, auth, 1, max_w, max_h);
}

int fetch_file_image(const char *path, int max_w, int max_h)
{
	return queue(path, NULL, 2, max_w, max_h);
}

static fetch_state_t state_of(const slot_t *s)
{
	if (!s) return FETCH_UNKNOWN;
	if (s->st == SL_DONE) return FETCH_DONE;
	if (s->st == SL_FAILED) return FETCH_FAILED;
	return FETCH_PENDING;
}

fetch_state_t fetch_state(int id)
{
	pthread_mutex_lock(&g_mu);
	fetch_state_t st = state_of(find(id));
	pthread_mutex_unlock(&g_mu);
	return st;
}

fetch_state_t fetch_take(int id, fetch_result_t *out)
{
	memset(out, 0, sizeof *out);
	pthread_mutex_lock(&g_mu);
	slot_t *s = find(id);
	fetch_state_t st = state_of(s);
	if (st == FETCH_DONE || st == FETCH_FAILED) {
		*out = s->res;
		memset(&s->res, 0, sizeof s->res);
		slot_clear(s);
	}
	pthread_mutex_unlock(&g_mu);
	return st;
}

void fetch_forget(int id)
{
	pthread_mutex_lock(&g_mu);
	slot_t *s = find(id);
	if (s && s->st == SL_RUNNING) {
		s->forgotten = 1;
		s->cancel = 1;
	} else if (s) {
		slot_clear(s);
	}
	pthread_mutex_unlock(&g_mu);
}
