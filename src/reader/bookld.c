/* bookld.c - reader/bookld: the main-thread API, starting and stopping the worker, the wish
 * list and the page cache. Opening, laying out and rendering live in bookld_work.c.
 *
 * Every function here holds b->mu only for a few field reads/writes (bookld_get also pins
 * under it: lock order b->mu then the cache's lock). */
#include "reader/bookld_priv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WORKER_STACK (256u * 1024u)

static int clampi(int v, int lo, int hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

void bookld_style_clamp(bookld_style_t *st)
{
	if (!st) return;
	st->font_px = clampi(st->font_px, 12, 48);
	st->margin_px = clampi(st->margin_px, 0, 120);
	if (!(st->line_spacing >= 1.0f)) st->line_spacing = 1.0f; /* NaN lands here too */
	if (st->line_spacing > 2.0f) st->line_spacing = 2.0f;
	st->font_family = clampi(st->font_family, 0, 1);
}

bookld_t *bookld_open(const char *path, const bookld_style_t *st, const char *resume_pos)
{
	bookld_t *b = calloc(1, sizeof *b);
	if (!b) return NULL;
	b->status = BOOKLD_OPENING;
	b->err_page = -1;
	b->jump_page = -1;
	b->find_page = -1;
	b->prev_keep = -1;
	if (st) {
		b->style = *st;
	} else {
		b->style.font_px = 22;
		b->style.margin_px = 24;
		b->style.line_spacing = 1.3f;
	}
	bookld_style_clamp(&b->style);
	snprintf(b->resume, sizeof b->resume, "%s", resume_pos ? resume_pos : "");
	b->path = strdup(path ? path : "");
	b->toc = calloc(BOOKLD_TOC_MAX, sizeof *b->toc);
	b->wtoc = calloc(BOOKLD_TOC_MAX, sizeof *b->wtoc);
	b->cache = pc_new(BOOKLD_CACHE_PAGES * BOOKLD_PAGE_COST);
	if (!b->path || !b->toc || !b->wtoc || !b->cache) goto fail;
	if (pthread_mutex_init(&b->mu, NULL) != 0) goto fail;
	if (pthread_cond_init(&b->cv, NULL) != 0) goto fail_mu;
	pthread_attr_t at;
	if (pthread_attr_init(&at) != 0) goto fail_cv;
	pthread_attr_setstacksize(&at, WORKER_STACK);
	int r = pthread_create(&b->thread, &at, bookld_worker, b);
	pthread_attr_destroy(&at);
	if (r != 0) goto fail_cv;
	return b;
fail_cv:
	pthread_cond_destroy(&b->cv);
fail_mu:
	pthread_mutex_destroy(&b->mu);
fail:
	pc_free(b->cache);
	free(b->wtoc);
	free(b->toc);
	free(b->path);
	free(b);
	return NULL;
}

void bookld_close(bookld_t *b)
{
	if (!b) return;
	pthread_mutex_lock(&b->mu);
	b->quit = 1;
	pthread_cond_broadcast(&b->cv);
	pthread_mutex_unlock(&b->mu);
	pthread_join(b->thread, NULL); /* the worker closes the document itself */
	pc_free(b->cache);
	pthread_cond_destroy(&b->cv);
	pthread_mutex_destroy(&b->mu);
	free(b->wtoc);
	free(b->toc);
	free(b->path);
	free(b);
}

void bookld_info(bookld_t *b, bookld_info_t *out)
{
	if (!out) return;
	memset(out, 0, sizeof *out);
	if (!b) {
		out->status = BOOKLD_FAILED;
		out->jump_page = -1;
		out->find_page = -1;
		return;
	}
	pthread_mutex_lock(&b->mu);
	out->status = b->status;
	out->gen = b->gen;
	out->count = b->count;
	out->anchor = b->anchor;
	out->busy = b->busy;
	out->jump_serial = b->jump_serial;
	out->jump_page = b->jump_page;
	out->find_serial = b->find_serial;
	out->find_page = b->find_page;
	if (b->status == BOOKLD_FAILED) snprintf(out->err, sizeof out->err, "%s", b->err);
	pthread_mutex_unlock(&b->mu);
}

void bookld_set_style(bookld_t *b, const bookld_style_t *st, int keep_page)
{
	if (!b || !st) return;
	bookld_style_t s = *st;
	bookld_style_clamp(&s);
	pthread_mutex_lock(&b->mu);
	/* Fixed layout has no style: nothing to do, the generation stays. While OPENING the
	 * request is kept: the first layout uses the latest style (see bookld_work.c). */
	if (b->status == BOOKLD_FAILED || (b->status == BOOKLD_READY && b->fixed)) {
		pthread_mutex_unlock(&b->mu);
		return;
	}
	b->style = s;
	b->style_pending = 1;
	b->keep_page = keep_page;
	b->keep_gen = b->gen;
	b->busy = 1;
	pthread_cond_signal(&b->cv);
	pthread_mutex_unlock(&b->mu);
}

static int in_list(const int *list, int n, int v)
{
	for (int i = 0; i < n; i++)
		if (list[i] == v) return 1;
	return 0;
}

void bookld_want(bookld_t *b, int gen, const int *pages, int n, uint32_t ink, uint32_t paper)
{
	int list[BOOKLD_WANT_MAX];
	int m = 0;
	if (!b) return;
	if (n > BOOKLD_WANT_MAX) n = BOOKLD_WANT_MAX;
	pthread_mutex_lock(&b->mu);
	if (b->status != BOOKLD_READY || gen != b->gen) { /* an old (or unknown) generation */
		pthread_mutex_unlock(&b->mu);
		return;
	}
	for (int i = 0; i < n && pages; i++)
		if (pages[i] >= 0 && pages[i] < b->count && !in_list(list, m, pages[i]))
			list[m++] = pages[i];
	if (ink != b->ink || paper != b->paper || gen != b->wish_gen) {
		b->nfailed = 0; /* other colours or layout: every page may be tried again */
	} else { /* failures that left the list may be retried when they come back */
		int keep = 0;
		for (int i = 0; i < b->nfailed; i++)
			if (in_list(list, m, b->failed[i])) b->failed[keep++] = b->failed[i];
		b->nfailed = keep;
	}
	memcpy(b->wish, list, sizeof list[0] * (size_t)m);
	b->nwish = m;
	b->wish_gen = gen;
	b->ink = ink;
	b->paper = paper;
	pthread_cond_signal(&b->cv);
	pthread_mutex_unlock(&b->mu);
}

const bookld_page_t *bookld_get(bookld_t *b, int gen, int page, uint32_t ink, uint32_t paper)
{
	if (!b || page < 0) return NULL;
	pc_ref_t *ref = NULL;
	pthread_mutex_lock(&b->mu);
	if (b->status == BOOKLD_READY && gen == b->gen)
		ref = pc_get(b->cache, bookld_key_book(gen, ink), page, bookld_key_variant(paper));
	pthread_mutex_unlock(&b->mu);
	if (!ref) return NULL;
	bpage_t *p = pc_ref_value(ref);
	if (p->pub.gen != gen || p->pub.page != page || p->pub.ink != ink || p->pub.paper != paper) {
		pc_release(b->cache, ref); /* cannot happen: exact key */
		return NULL;
	}
	p->ref = ref;
	return &p->pub;
}

void bookld_release(bookld_t *b, const bookld_page_t *pub)
{
	if (!b || !pub) return;
	bpage_t *p = (bpage_t *)((char *)pub - offsetof(bpage_t, pub));
	pc_release(b->cache, p->ref); /* frees it when a new generation already dropped it */
}

void bookld_resolve(bookld_t *b, const char *pos)
{
	if (!b) return;
	pthread_mutex_lock(&b->mu);
	snprintf(b->resolve_pos, sizeof b->resolve_pos, "%s", pos ? pos : "");
	b->resolve_pending = 1; /* a second call before the worker gets to it replaces this one */
	pthread_cond_signal(&b->cv);
	pthread_mutex_unlock(&b->mu);
}

void bookld_find(bookld_t *b, const char *query, int start)
{
	if (!b) return;
	pthread_mutex_lock(&b->mu);
	snprintf(b->find_query, sizeof b->find_query, "%s", query ? query : "");
	b->find_start = start;
	b->find_pending = 1; /* a second call before the worker gets to it replaces this one */
	pthread_cond_signal(&b->cv);
	pthread_mutex_unlock(&b->mu);
}

int bookld_toc(bookld_t *b, mdoc_toc_item_t *out, int max)
{
	if (!b || !out || max <= 0) return 0;
	pthread_mutex_lock(&b->mu);
	int n = b->status == BOOKLD_READY ? b->ntoc : 0;
	if (n > max) n = max;
	memcpy(out, b->toc, sizeof *out * (size_t)n);
	pthread_mutex_unlock(&b->mu);
	return n;
}

int bookld_take_error(bookld_t *b, char *err, size_t cap)
{
	if (!b) return -1;
	pthread_mutex_lock(&b->mu);
	int page = b->err_page;
	if (page >= 0 && err && cap > 0) snprintf(err, cap, "%s", b->page_err);
	b->err_page = -1;
	pthread_mutex_unlock(&b->mu);
	return page;
}

/* ---- test hooks ---- */

void bookld_test_hold(bookld_t *b, int hold)
{
	pthread_mutex_lock(&b->mu);
	b->hold = hold;
	pthread_cond_signal(&b->cv);
	pthread_mutex_unlock(&b->mu);
}

int bookld_test_renders(bookld_t *b)
{
	pthread_mutex_lock(&b->mu);
	int n = b->renders;
	pthread_mutex_unlock(&b->mu);
	return n;
}

int bookld_test_cache_count(bookld_t *b) { return pc_count(b->cache); }
size_t bookld_test_cache_bytes(bookld_t *b) { return pc_bytes(b->cache); }
