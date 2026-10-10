/* loader.c - reader/loader: one worker thread per book, the wish list, failures, the page
 * cache and the main-thread API. Opening and rendering live in loader_src.c.
 *
 * Locking: l->mu guards everything below it in struct loader; the main thread only ever
 * holds it for a few field reads/writes. Lock order is l->mu then the cache's own lock
 * (pc_contains while picking); free callbacks only free(). */
#include "reader/loader_priv.h"

#include "core/affinity.h"
#include "lib/db.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WORKER_STACK (256u * 1024u)
#define ERR_CAP 160
#define PAGE_OVERHEAD 4096u /* lpage_t + tiles, generously */

typedef struct {
	int page, max_w, max_h; /* normalised */
} lkey_t;

struct loader {
	pthread_t thread;
	page_cache_t *cache;
	size_t budget; /* pixel bytes one page may use: the cache cap less bookkeeping */
	char *path;
	int kind;
	int stream; /* OPDS-PSE stream: open from the params below, not from path */
	char tmpl[LSRC_TMPL_MAX];
	char user[LSRC_CRED_MAX], pass[LSRC_CRED_MAX];
	int stream_count;
	volatile int fetch_cancel; /* set by loader_want/close: aborts a slow page fetch */
	lsrc_t src; /* worker only */

	pthread_mutex_t mu;
	pthread_cond_t cv;
	int quit, hold, busy;
	loader_status_t status;
	char err[ERR_CAP];
	int npages;
	int *nat_w, *nat_h; /* natural sizes, 0 = unknown; npages entries once READY */
	int *renders;       /* test counter: renders started per page */
	mdoc_toc_item_t *toc; /* outline captured at open (documents), ntoc items */
	int ntoc;
	lkey_t wish[LOADER_WANT_MAX];
	int nwish;
	lkey_t failed[LOADER_WANT_MAX]; /* failed requests still in the wish list */
	int nfailed;
	int err_page; /* -1 = no error to report */
	char page_err[ERR_CAP];
};

static int same(const lkey_t *a, const lkey_t *b)
{
	return a->page == b->page && a->max_w == b->max_w && a->max_h == b->max_h;
}

static int in_list(const lkey_t *list, int n, const lkey_t *k)
{
	for (int i = 0; i < n; i++)
		if (same(&list[i], k)) return 1;
	return 0;
}

/* Next wish to render, or -1. Called with mu held. */
static int pick_locked(loader_t *l)
{
	for (int i = 0; i < l->nwish; i++) {
		const lkey_t *k = &l->wish[i];
		if (k->page >= l->npages || in_list(l->failed, l->nfailed, k)) continue;
		if (pc_contains(l->cache, 0, k->page, loader_variant(k->max_w, k->max_h))) continue;
		return i;
	}
	return -1;
}

static void record_failure_locked(loader_t *l, const lkey_t *k, const char *msg)
{
	l->err_page = k->page;
	snprintf(l->page_err, sizeof l->page_err, "%s", msg);
	/* Only a request still wished is remembered; one that already left may be retried. */
	if (in_list(l->wish, l->nwish, k) && !in_list(l->failed, l->nfailed, k) &&
	    l->nfailed < LOADER_WANT_MAX)
		l->failed[l->nfailed++] = *k;
}

/* Open the book; READY (with the size tables) or FAILED. Returns 1 when READY. */
static int open_book(loader_t *l)
{
	char err[ERR_CAP] = "";
	int n;
	if (l->stream)
		n = lsrc_open_stream(&l->src, l->tmpl, l->stream_count, l->user, l->pass,
		                     &l->fetch_cancel, err, sizeof err);
	else
		n = lsrc_open(&l->src, l->path, l->kind, err, sizeof err);
	int *nw = NULL, *nh = NULL, *rc = NULL;
	if (n > 0) {
		nw = calloc((size_t)n, sizeof *nw);
		nh = calloc((size_t)n, sizeof *nh);
		rc = calloc((size_t)n, sizeof *rc);
		if (!nw || !nh || !rc) {
			snprintf(err, sizeof err, "out of memory");
			n = -1;
		}
	}
	if (n > 0 && l->src.doc)
		for (int i = 0; i < n; i++)
			if (lsrc_doc_size(&l->src, i, &nw[i], &nh[i]) != 0) nw[i] = nh[i] = 0;
	mdoc_toc_item_t *toc = NULL;
	int ntoc = 0;
	if (n > 0 && l->src.doc && (toc = malloc(sizeof *toc * LOADER_TOC_MAX)) != NULL) {
		ntoc = mdoc_toc(l->src.doc, toc, LOADER_TOC_MAX); /* a missing outline is not fatal */
		if (ntoc <= 0) {
			free(toc);
			toc = NULL;
			ntoc = 0;
		}
	}
	pthread_mutex_lock(&l->mu);
	if (n > 0) {
		l->toc = toc;
		l->ntoc = ntoc;
		l->nat_w = nw;
		l->nat_h = nh;
		l->renders = rc;
		l->npages = n;
		l->status = LOADER_READY;
	} else {
		free(nw);
		free(nh);
		free(rc);
		snprintf(l->err, sizeof l->err, "%s", err[0] ? err : "cannot open");
		l->status = LOADER_FAILED;
	}
	pthread_mutex_unlock(&l->mu);
	return n > 0;
}

static void *worker(void *arg)
{
	loader_t *l = arg;
	affinity_use_all_cores();
	if (!open_book(l)) {
		lsrc_close(&l->src);
		return NULL;
	}
	pthread_mutex_lock(&l->mu);
	while (!l->quit) {
		int i = l->hold ? -1 : pick_locked(l);
		if (i < 0) {
			pthread_cond_wait(&l->cv, &l->mu);
			continue;
		}
		lkey_t k = l->wish[i];
		l->busy = 1;
		l->fetch_cancel = 0; /* this render's fetch may run to completion */
		l->renders[k.page]++;
		pthread_mutex_unlock(&l->mu);

		char err[ERR_CAP] = "";
		lpage_t *p = lsrc_render(&l->src, k.page, k.max_w, k.max_h, l->budget, err, sizeof err);
		int rc = PC_OK, sw = 0, sh = 0;
		pthread_mutex_lock(&l->mu);
		if (l->quit || !in_list(l->wish, l->nwish, &k)) {
			lpage_free(p, NULL);
			l->busy = 0;
			continue; /* loop continues with mu held */
		}
		pthread_mutex_unlock(&l->mu);
		if (p) {
			sw = p->pub.src_w; /* p may be evicted (freed) as soon as it is in the cache */
			sh = p->pub.src_h;
			rc = pc_put(l->cache, 0, k.page, loader_variant(k.max_w, k.max_h), p,
			            lpage_cost(p), lpage_free, NULL);
			if (rc != PC_OK) {
				snprintf(err, sizeof err, "%s",
				         rc == PC_ERR_TOO_BIG ? "page too large for the cache" : "cache insert failed");
				lpage_free(p, NULL);
			}
		}

		pthread_mutex_lock(&l->mu);
		l->busy = 0;
		if (p && rc == PC_OK) {
			l->nat_w[k.page] = sw;
			l->nat_h[k.page] = sh;
		} else {
			record_failure_locked(l, &k, err[0] ? err : "render failed");
		}
	}
	pthread_mutex_unlock(&l->mu);
	lsrc_close(&l->src);
	return NULL;
}

loader_t *loader_open_cap(const char *path, int db_kind, size_t cache_bytes)
{
	loader_t *l = calloc(1, sizeof *l);
	if (!l) return NULL;
	l->kind = db_kind;
	l->err_page = -1;
	l->status = LOADER_OPENING;
	l->path = strdup(path ? path : "");
	l->cache = pc_new(cache_bytes);
	l->budget = cache_bytes > 2 * PAGE_OVERHEAD ? cache_bytes - PAGE_OVERHEAD : PAGE_OVERHEAD;
	if (!l->path || !l->cache) goto fail;
	if (pthread_mutex_init(&l->mu, NULL) != 0) goto fail;
	if (pthread_cond_init(&l->cv, NULL) != 0) goto fail_mu;
	pthread_attr_t at;
	if (pthread_attr_init(&at) != 0) goto fail_cv;
	pthread_attr_setstacksize(&at, WORKER_STACK);
	int r = pthread_create(&l->thread, &at, worker, l);
	pthread_attr_destroy(&at);
	if (r != 0) goto fail_cv;
	return l;
fail_cv:
	pthread_cond_destroy(&l->cv);
fail_mu:
	pthread_mutex_destroy(&l->mu);
fail:
	pc_free(l->cache);
	free(l->path);
	free(l);
	return NULL;
}

loader_t *loader_open(const char *path, int db_kind)
{
	return loader_open_cap(path, db_kind, LOADER_CACHE_BYTES);
}

loader_t *loader_open_stream(const char *tmpl, int count, const char *user, const char *pass)
{
	loader_t *l;

	if (!tmpl || !tmpl[0] || strlen(tmpl) >= LSRC_TMPL_MAX || count <= 0) return NULL;
	l = loader_open_cap("", DB_KIND_STREAM, LOADER_CACHE_BYTES);
	if (!l) return NULL;
	l->stream = 1;
	snprintf(l->tmpl, sizeof l->tmpl, "%s", tmpl);
	snprintf(l->user, sizeof l->user, "%s", user ? user : "");
	snprintf(l->pass, sizeof l->pass, "%s", pass ? pass : "");
	l->stream_count = count;
	return l;
}

void loader_close(loader_t *l)
{
	if (!l) return;
	pthread_mutex_lock(&l->mu);
	l->fetch_cancel = 1; /* do not wait out a slow streamed page */
	l->quit = 1;
	pthread_cond_broadcast(&l->cv);
	pthread_mutex_unlock(&l->mu);
	pthread_join(l->thread, NULL);
	pc_free(l->cache);
	pthread_cond_destroy(&l->cv);
	pthread_mutex_destroy(&l->mu);
	free(l->nat_w);
	free(l->nat_h);
	free(l->renders);
	free(l->toc);
	free(l->path);
	free(l);
}

loader_status_t loader_status(loader_t *l, char *err, size_t cap)
{
	if (!l) return LOADER_FAILED;
	pthread_mutex_lock(&l->mu);
	loader_status_t s = l->status;
	if (s == LOADER_FAILED && cap > 0) snprintf(err, cap, "%s", l->err);
	pthread_mutex_unlock(&l->mu);
	return s;
}

int loader_page_count(loader_t *l)
{
	if (!l) return 0;
	pthread_mutex_lock(&l->mu);
	int n = l->status == LOADER_READY ? l->npages : 0;
	pthread_mutex_unlock(&l->mu);
	return n;
}

int loader_page_size(loader_t *l, int page, int *w, int *h)
{
	int r = -1;
	if (!l) return -1;
	pthread_mutex_lock(&l->mu);
	if (l->status == LOADER_READY && page >= 0 && page < l->npages && l->nat_w[page] > 0) {
		if (w) *w = l->nat_w[page];
		if (h) *h = l->nat_h[page];
		r = 0;
	}
	pthread_mutex_unlock(&l->mu);
	return r;
}

int loader_toc(loader_t *l, mdoc_toc_item_t *out, int max)
{
	int n = 0;
	if (!l || !out || max <= 0) return 0;
	pthread_mutex_lock(&l->mu);
	if (l->status == LOADER_READY) {
		n = l->ntoc < max ? l->ntoc : max;
		if (n > 0) memcpy(out, l->toc, sizeof *out * (size_t)n);
	}
	pthread_mutex_unlock(&l->mu);
	return n;
}

void loader_want(loader_t *l, const loader_req_t *reqs, int n)
{
	lkey_t list[LOADER_WANT_MAX];
	int m = 0;
	if (!l) return;
	if (n > LOADER_WANT_MAX) n = LOADER_WANT_MAX;
	for (int i = 0; i < n && reqs; i++) {
		if (reqs[i].page < 0) continue;
		lkey_t k = { reqs[i].page, loader_norm(reqs[i].max_w), loader_norm(reqs[i].max_h) };
		if (!in_list(list, m, &k)) list[m++] = k;
	}
	pthread_mutex_lock(&l->mu);
	if (l->status == LOADER_READY) { /* before READY the count is unknown: pick filters */
		int keep = 0;
		for (int i = 0; i < m; i++)
			if (list[i].page < l->npages) list[keep++] = list[i];
		m = keep;
	}
	memcpy(l->wish, list, sizeof list[0] * (size_t)m);
	l->nwish = m;
	l->fetch_cancel = 1; /* a stale streamed page stops fetching (and resumes later) */
	/* Failures that left the list may be retried when they come back. */
	int keep = 0;
	for (int i = 0; i < l->nfailed; i++)
		if (in_list(l->wish, l->nwish, &l->failed[i])) l->failed[keep++] = l->failed[i];
	l->nfailed = keep;
	pthread_cond_signal(&l->cv);
	pthread_mutex_unlock(&l->mu);
}

const loader_page_t *loader_get(loader_t *l, int page, int max_w, int max_h)
{
	if (!l || page < 0) return NULL;
	int mw = loader_norm(max_w), mh = loader_norm(max_h);
	pc_ref_t *ref = pc_get(l->cache, 0, page, loader_variant(mw, mh));
	if (!ref) return NULL;
	lpage_t *p = pc_ref_value(ref);
	if (p->pub.page != page || p->max_w != mw || p->max_h != mh) { /* cannot happen: exact key */
		pc_release(l->cache, ref);
		return NULL;
	}
	p->ref = ref;
	return &p->pub;
}

void loader_release(loader_t *l, const loader_page_t *pub)
{
	if (!l || !pub) return;
	lpage_t *p = (lpage_t *)((char *)pub - offsetof(lpage_t, pub));
	pc_release(l->cache, p->ref);
}

int loader_take_error(loader_t *l, char *err, size_t cap)
{
	if (!l) return -1;
	pthread_mutex_lock(&l->mu);
	int page = l->err_page;
	if (page >= 0 && cap > 0) snprintf(err, cap, "%s", l->page_err);
	l->err_page = -1;
	pthread_mutex_unlock(&l->mu);
	return page;
}

/* ---- test hooks ---- */

void loader_test_hold(loader_t *l, int hold)
{
	pthread_mutex_lock(&l->mu);
	l->hold = hold;
	pthread_cond_signal(&l->cv);
	pthread_mutex_unlock(&l->mu);
}

int loader_test_renders(loader_t *l, int page)
{
	pthread_mutex_lock(&l->mu);
	int n = l->renders && page >= 0 && page < l->npages ? l->renders[page] : 0;
	pthread_mutex_unlock(&l->mu);
	return n;
}

int loader_test_busy(loader_t *l)
{
	pthread_mutex_lock(&l->mu);
	int b = l->busy;
	pthread_mutex_unlock(&l->mu);
	return b;
}

size_t loader_test_cache_bytes(loader_t *l) { return pc_bytes(l->cache); }
int loader_test_cache_count(loader_t *l) { return pc_count(l->cache); }
