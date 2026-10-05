#include "net/dlq.h"

#include "core/strutil.h"
#include "lib/db.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define STACK_BYTES (256 * 1024)

/* net/dlq_name.c (no header) */
int dlq_name_make(const char *dir, const char *title, const char *ext, const char *url,
                  int (*taken)(const char *path, void *ud), void *ud, char *out, size_t cap);

typedef struct {
	int used;
	dlq_item_t it;    /* what dlq_list shows; worker and main thread, under g_mu */
	char url[512];
	char *user, *pass; /* memory only, never written to the database */
	int dirty;        /* state or error changed since the last database write */
	int64_t db_bytes; /* bytes at the last database write */
	time_t db_time;
	volatile int cancel; /* polled by the running transfer */
	int cancel_req;      /* dlq_cancel: end as CANCELLED and remove the .part */
} slot_t;

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static slot_t g_slots[DLQ_MAX];
static char g_dir[512];
static int g_quit, g_started, g_completed;
static pthread_t g_thread;

static int64_t file_size(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0 ? (int64_t)st.st_size : -1;
}

static int part_path(char *out, size_t cap, const char *dest)
{
	snprintf(out, cap, "%s.part", dest);
	return strlen(dest) + 5 < cap ? 0 : -1;
}

static int64_t part_size(const char *dest)
{
	char part[520];
	int64_t n = part_path(part, sizeof part, dest) == 0 ? file_size(part) : -1;
	return n > 0 ? n : 0;
}

static void remove_part(const char *dest)
{
	char part[520];
	if (dest[0] && part_path(part, sizeof part, dest) == 0) remove(part);
}

static slot_t *find(int64_t id)
{
	for (int i = 0; i < DLQ_MAX; i++)
		if (g_slots[i].used && g_slots[i].it.id == id) return &g_slots[i];
	return NULL;
}

static void set_state(slot_t *s, int state, const char *err)
{
	s->it.state = state;
	str_copy(s->it.error, sizeof s->it.error, err ? err : "");
	s->dirty = 1;
}

static void slot_free(slot_t *s)
{
	free(s->user);
	free(s->pass);
	memset(s, 0, sizeof *s);
}

static slot_t *oldest_queued(void)
{
	slot_t *best = NULL;
	for (int i = 0; i < DLQ_MAX; i++)
		if (g_slots[i].used && g_slots[i].it.state == DB_DL_QUEUED && (!best || g_slots[i].it.id < best->it.id))
			best = &g_slots[i];
	return best;
}

static int on_progress(void *ud, int64_t now, int64_t total)
{
	slot_t *s = (slot_t *)ud;
	pthread_mutex_lock(&g_mu);
	s->it.bytes = now;
	s->it.total = total;
	pthread_mutex_unlock(&g_mu);
	return s->cancel;
}

/* Called with g_mu held; drops it during the transfer. The slot cannot be freed meanwhile:
 * dlq_remove refuses an ACTIVE download and dlq_stop joins this thread first. */
static void run_one(slot_t *s)
{
	char url[512], dest[512], part[520], err[128] = "";
	http_auth_t a = { s->user, s->pass };
	str_copy(url, sizeof url, s->url);
	str_copy(dest, sizeof dest, s->it.dest_path);
	set_state(s, DB_DL_ACTIVE, NULL);
	s->cancel = 0;
	s->cancel_req = 0;
	int64_t resume = part_size(dest);
	s->it.bytes = resume;
	pthread_mutex_unlock(&g_mu);

	int rc = part_path(part, sizeof part, dest);
	if (rc != 0) str_copy(err, sizeof err, "Path too long");
	else rc = http_download(url, &a, part, resume, on_progress, s, NULL, 0, err, sizeof err);
	if (rc == 0 && file_size(dest) >= 0) {
		rc = -1;
		str_copy(err, sizeof err, "File already exists");
	} else if (rc == 0 && rename(part, dest) != 0) {
		rc = -1;
		str_copy(err, sizeof err, "Could not rename the file");
	}

	pthread_mutex_lock(&g_mu);
	if (rc == 0) {
		s->it.bytes = s->it.total = file_size(dest);
		set_state(s, DB_DL_DONE, NULL);
		g_completed++;
	} else if (s->cancel_req) {
		remove(part);
		s->it.bytes = 0;
		set_state(s, DB_DL_CANCELLED, NULL);
	} else if (!g_quit) {
		set_state(s, DB_DL_FAILED, err);
	} /* else stopping: stays ACTIVE, and dlq_start resumes it from the .part */
}

static void *worker(void *arg)
{
	(void)arg;
	pthread_mutex_lock(&g_mu);
	for (;;) {
		slot_t *s = NULL;
		while (!g_quit && !(s = oldest_queued())) pthread_cond_wait(&g_cv, &g_mu);
		if (g_quit) break;
		run_one(s);
	}
	pthread_mutex_unlock(&g_mu);
	return NULL;
}

/* Database writes the main thread owes: every state change, progress once a second. */
static int flush(int force_progress)
{
	struct {
		int64_t id, bytes, total;
		int state;
		char error[128];
	} w[DLQ_MAX];
	int n = 0;
	time_t now = time(NULL);
	pthread_mutex_lock(&g_mu);
	for (int i = 0; i < DLQ_MAX; i++) {
		slot_t *s = &g_slots[i];
		if (!s->used) continue;
		if (!s->dirty && (s->it.bytes == s->db_bytes || (!force_progress && now - s->db_time < 1))) continue;
		w[n].id = s->it.id;
		w[n].bytes = s->it.bytes;
		w[n].total = s->it.total;
		w[n].state = s->it.state;
		str_copy(w[n].error, sizeof w[n].error, s->it.error);
		n++;
		s->dirty = 0;
		s->db_bytes = s->it.bytes;
		s->db_time = now;
	}
	int done = g_completed;
	g_completed = 0;
	pthread_mutex_unlock(&g_mu);
	for (int i = 0; i < n; i++) db_download_update(w[i].id, w[i].state, w[i].bytes, w[i].total, w[i].error);
	return done;
}

int dlq_pump(void)
{
	return g_started ? flush(0) : 0;
}

int dlq_start(const char *books_dir)
{
	if (g_started || !books_dir || str_copy(g_dir, sizeof g_dir, books_dir) >= sizeof g_dir) return -1;
	db_download_t *rows = malloc(sizeof *rows * DLQ_MAX);
	if (!rows) return -1;
	int n = db_download_list(rows, DLQ_MAX);
	if (n < 0) {
		free(rows);
		return -1;
	}
	pthread_mutex_lock(&g_mu);
	memset(g_slots, 0, sizeof g_slots);
	for (int i = 0; i < n; i++) {
		slot_t *s = &g_slots[i];
		db_download_t *d = &rows[i];
		s->used = 1;
		s->it.id = d->id;
		str_copy(s->it.title, sizeof s->it.title, d->title);
		str_copy(s->it.dest_path, sizeof s->it.dest_path, d->dest_path);
		str_copy(s->url, sizeof s->url, d->url);
		s->it.state = d->state;
		s->it.bytes = s->db_bytes = d->bytes;
		s->it.total = d->total;
		str_copy(s->it.error, sizeof s->it.error, d->error);
		if (d->state == DB_DL_ACTIVE) set_state(s, DB_DL_QUEUED, NULL);
		if (s->it.state == DB_DL_QUEUED) s->it.bytes = part_size(s->it.dest_path);
	}
	g_quit = 0;
	g_completed = 0;
	pthread_mutex_unlock(&g_mu);
	free(rows);

	pthread_attr_t at;
	pthread_attr_init(&at);
	pthread_attr_setstacksize(&at, STACK_BYTES);
	g_started = pthread_create(&g_thread, &at, worker, NULL) == 0;
	pthread_attr_destroy(&at);
	return g_started ? 0 : -1;
}

void dlq_stop(void)
{
	if (!g_started) return;
	pthread_mutex_lock(&g_mu);
	g_quit = 1;
	for (int i = 0; i < DLQ_MAX; i++)
		if (g_slots[i].used && g_slots[i].it.state == DB_DL_ACTIVE) g_slots[i].cancel = 1;
	pthread_cond_broadcast(&g_cv);
	pthread_mutex_unlock(&g_mu);
	pthread_join(g_thread, NULL);
	flush(1);
	g_started = 0;
	for (int i = 0; i < DLQ_MAX; i++) slot_free(&g_slots[i]);
}

/* A name is taken by a file, a .part file or another download's destination. g_mu held. */
static int name_taken(const char *path, void *ud)
{
	char part[520];
	(void)ud;
	if (file_size(path) >= 0) return 1;
	if (part_path(part, sizeof part, path) == 0 && file_size(part) >= 0) return 1;
	for (int i = 0; i < DLQ_MAX; i++)
		if (g_slots[i].used && str_casecmp(g_slots[i].it.dest_path, path) == 0) return 1;
	return 0;
}

int dlq_add(const char *url, const http_auth_t *auth, const char *title, const char *ext, int64_t *out_id)
{
	db_download_t d;
	slot_t *s = NULL;
	if (!g_started || !url || !*url || strlen(url) >= sizeof d.url) return -1;
	memset(&d, 0, sizeof d);
	pthread_mutex_lock(&g_mu);
	for (int i = 0; i < DLQ_MAX && !s; i++)
		if (!g_slots[i].used) s = &g_slots[i];
	if (!s || dlq_name_make(g_dir, title, ext, url, name_taken, NULL, d.dest_path, sizeof d.dest_path) != 0) {
		pthread_mutex_unlock(&g_mu);
		return -1;
	}
	str_copy(d.url, sizeof d.url, url);
	str_copy(d.title, sizeof d.title, title ? title : "");
	d.state = DB_DL_QUEUED;
	d.total = -1;
	int64_t id;
	if (db_download_add(&d, &id) < 0) {
		pthread_mutex_unlock(&g_mu);
		return -1;
	}
	s->used = 1;
	s->it.id = id;
	str_copy(s->it.title, sizeof s->it.title, d.title);
	str_copy(s->it.dest_path, sizeof s->it.dest_path, d.dest_path);
	str_copy(s->url, sizeof s->url, url);
	s->it.state = DB_DL_QUEUED;
	s->it.total = -1;
	if (auth && auth->user && *auth->user) {
		s->user = strdup(auth->user);
		s->pass = strdup(auth->pass ? auth->pass : "");
	}
	pthread_cond_signal(&g_cv);
	pthread_mutex_unlock(&g_mu);
	if (out_id) *out_id = id;
	return 0;
}

int dlq_cancel(int64_t id)
{
	int rc = 0;
	pthread_mutex_lock(&g_mu);
	slot_t *s = find(id);
	if (s && s->it.state == DB_DL_QUEUED) {
		remove_part(s->it.dest_path);
		s->it.bytes = 0;
		set_state(s, DB_DL_CANCELLED, NULL);
	} else if (s && s->it.state == DB_DL_ACTIVE) {
		s->cancel_req = 1;
		s->cancel = 1;
	} else {
		rc = -1;
	}
	pthread_mutex_unlock(&g_mu);
	return rc;
}

int dlq_retry(int64_t id)
{
	int rc = -1;
	pthread_mutex_lock(&g_mu);
	slot_t *s = find(id);
	if (s && (s->it.state == DB_DL_FAILED || s->it.state == DB_DL_CANCELLED)) {
		s->it.bytes = part_size(s->it.dest_path);
		set_state(s, DB_DL_QUEUED, NULL);
		pthread_cond_signal(&g_cv);
		rc = 0;
	}
	pthread_mutex_unlock(&g_mu);
	return rc;
}

int dlq_remove(int64_t id)
{
	pthread_mutex_lock(&g_mu);
	slot_t *s = find(id);
	if (!s || s->it.state == DB_DL_QUEUED || s->it.state == DB_DL_ACTIVE) {
		pthread_mutex_unlock(&g_mu);
		return -1;
	}
	if (s->it.state != DB_DL_DONE) remove_part(s->it.dest_path);
	slot_free(s);
	pthread_mutex_unlock(&g_mu);
	return db_download_remove(id) < 0 ? -1 : 0;
}

int dlq_list(dlq_item_t *out, int max)
{
	const slot_t *o[DLQ_MAX];
	int n = 0;
	pthread_mutex_lock(&g_mu);
	for (int i = 0; i < DLQ_MAX; i++) {
		if (!g_slots[i].used) continue;
		int j = n++; /* insertion sort by id = oldest first */
		for (; j > 0 && o[j - 1]->it.id > g_slots[i].it.id; j--) o[j] = o[j - 1];
		o[j] = &g_slots[i];
	}
	if (n > max) n = max > 0 ? max : 0;
	for (int i = 0; i < n; i++) out[i] = o[i]->it;
	pthread_mutex_unlock(&g_mu);
	return n;
}

int dlq_busy(void)
{
	int busy = 0;
	pthread_mutex_lock(&g_mu);
	for (int i = 0; i < DLQ_MAX && !busy; i++)
		busy = g_slots[i].used && (g_slots[i].it.state == DB_DL_QUEUED || g_slots[i].it.state == DB_DL_ACTIVE);
	pthread_mutex_unlock(&g_mu);
	return busy;
}
