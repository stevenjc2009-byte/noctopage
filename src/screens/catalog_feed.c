/* catalog_feed.c - the stack of open OPDS feeds: open, page, reload, close, search. Each
 * feed is fetched by net/fetch and parsed (net/opds) on the main thread when it arrives. */
#include "screens/catalog_priv.h"

#include "core/log.h"
#include "net/url.h"
#include "core/casefold.h"
#include "ui/osk.h"

#include <stdio.h>
#include <string.h>

cat_level_t *cat_top(void) { return C.depth > 0 ? &C.lv[C.depth - 1] : NULL; }

static void level_free(cat_level_t *l)
{
	if (l->fetch_id > 0) fetch_forget(l->fetch_id);
	opds_feed_free(l->feed);
	l->feed = NULL;
	l->fetch_id = 0;
}

static void level_load(cat_level_t *l, const char *url)
{
	if(C.search_fetch>0) fetch_forget(C.search_fetch);
 C.search_fetch=0;
 char copy[HTTP_URL_MAX]; /* url may live in the feed freed below */
	snprintf(copy, sizeof copy, "%s", url);
	level_free(l);
	snprintf(l->url, sizeof l->url, "%s", copy);
	l->err[0] = '\0';
	l->sel = l->top = 0;
 grid_init(&l->grid);
	l->fetch_id = fetch_get(l->url, cat_auth_url(l->url));
	if (l->fetch_id < 0) {
		l->fetch_id = 0;
		snprintf(l->err, sizeof l->err, "The network is not available");
	}
}

void cat_push(const char *url, const char *title)
{
	if (!url || !url[0]) return;
	if (C.depth == CAT_DEPTH) {
		cat_toast("Too many catalog pages open: go back first");
		return;
	}
	cat_level_t *l = &C.lv[C.depth++];
	memset(l, 0, sizeof *l);
	snprintf(l->title, sizeof l->title, "%s", title ? title : "");
	level_load(l, url);
}

void cat_replace(const char *url)
{
	if (url && url[0] && C.depth > 0) level_load(cat_top(), url);
}

void cat_reload(void)
{
	cat_level_t *l = cat_top();
	if (l) { cat_covers_clear(); level_load(l, l->url); }
}

void cat_pop(void)
{
	if (C.search_fetch>0) fetch_forget(C.search_fetch);
 C.search_fetch=0; C.detail=-1;
 if (C.depth == 0) return;
	level_free(&C.lv[--C.depth]);
	if (C.depth == 0) C.source = -1;
}

void cat_close_all(void)
{
	while (C.depth > 0) cat_pop();
	if (C.search_fetch > 0) fetch_forget(C.search_fetch);
	C.search_fetch = 0;
	C.detail = -1;
}

static void search_with(const char *tmpl)
{
	char url[URL_MAX], title[128];
	if (url_expand_search(url, sizeof url, tmpl, C.search_terms) < 0) {
		cat_toast("The search address is too long");
		return;
	}
	snprintf(title, sizeof title, "Search: %.100s", C.search_terms);
	log_printf("catalog: search %s", url);
	cat_push(url, title);
}

void cat_search_start(void)
{
	cat_level_t *l = cat_top();
	if (!l || !l->feed) {
		cat_toast("This catalog has no search");
		return;
	}
	char terms[sizeof C.search_terms];
	if (osk_prompt("Search", C.search_terms, OSK_TEXT, terms, sizeof terms) != 1)
		return;
	snprintf(C.search_terms, sizeof C.search_terms, "%s", terms);
	char api[URL_MAX];
 if(catalog_api_kind(l->url)==1 || catalog_api_kind(l->url)==3) {
  /* Use the source's base query, so successive searches replace the previous terms. */
  char base[URL_MAX];
  if(!terms[0]) { char base[URL_MAX]; if(C.source>=0 && catalog_api_url(C.src[C.source].url,base,sizeof base)==0) cat_push(base,C.src[C.source].name); return; }
  if(C.source<0 || catalog_api_url(C.src[C.source].url,base,sizeof base)<0 || catalog_api_search(base,terms,api,sizeof api)<0) { cat_toast("Search address is too long"); return; }
  cat_push(api,"Search results"); return;
 }
 if(!l->feed->search_href) { snprintf(l->filter,sizeof l->filter,"%s",terms); cat_filter(l); return; }
 if (l->feed->search_is_template) {
		search_with(l->feed->search_href);
		return;
	}
	/* An OpenSearch description: fetch it, then expand its template (cat_poll). */
	if (C.search_fetch > 0) fetch_forget(C.search_fetch);
	C.search_fetch = fetch_get(l->feed->search_href, cat_auth_url(l->feed->search_href));
	if (C.search_fetch < 0) { C.search_fetch = 0; cat_toast("The network is not available"); }
}

static void poll_level(cat_level_t *l)
{
	fetch_result_t r;
	fetch_state_t st = fetch_take(l->fetch_id, &r);
	if (st == FETCH_PENDING) return;
	l->fetch_id = 0;
	if (st == FETCH_DONE) {
		const char *base = r.doc.final_url[0] ? r.doc.final_url : l->url;
		int kind=catalog_api_kind(l->url);
  int parsed=kind?catalog_api_parse(r.doc.data,r.doc.len,l->url,&l->feed,l->err,sizeof l->err):opds_parse(r.doc.data,r.doc.len,r.doc.content_type[0]?r.doc.content_type:NULL,base,&l->feed,l->err,sizeof l->err);
  if(parsed<0) {
			l->feed = NULL;
			log_printf("catalog: %s: %s", l->url, l->err);
		} else {
			if (!l->title[0] && l->feed->title[0])
				snprintf(l->title, sizeof l->title, "%s", l->feed->title);
   cat_filter(l);
   if(kind==2 && l==cat_top() && l->feed->nentries==1) cat_detail_open(0);
			log_printf("catalog: %s: %d entries%s", l->url, l->feed->nentries,
			           l->feed->next ? ", more pages" : "");
		}
	} else {
		snprintf(l->err, sizeof l->err, "%s", r.err[0] ? r.err : "Could not load the catalog");
		log_printf("catalog: %s: %s", l->url, l->err);
	}
	fetch_result_free(&r);
}

static void poll_search(void)
{
	fetch_result_t r;
	fetch_state_t st = fetch_take(C.search_fetch, &r);
	if (st == FETCH_PENDING) return;
	C.search_fetch = 0;
	char tmpl[URL_MAX], err[160];
	if (st != FETCH_DONE) {
		cat_toast(r.err[0] ? r.err : "Search is not available");
	} else if (opds_parse_opensearch(r.doc.data, r.doc.len, r.doc.final_url, tmpl, sizeof tmpl,
	                                 err, sizeof err) < 0) {
		cat_toast(err);
	} else {
		search_with(tmpl);
	}
	fetch_result_free(&r);
}

void cat_poll(void)
{
	for (int i = 0; i < C.depth; i++)
		if (C.lv[i].fetch_id > 0) poll_level(&C.lv[i]);
	if (C.search_fetch > 0) poll_search();
}

void cat_filter(cat_level_t *l)
{
 l->norder=0;
 if(l->feed) for(int i=0;i<l->feed->nentries;i++) {
  const opds_entry *e=&l->feed->entries[i];
  if(!l->filter[0]||unicode_contains_ci(e->title,l->filter)||(e->author&&unicode_contains_ci(e->author,l->filter))) l->order[l->norder++]=i;
 }
 grid_init(&l->grid); grid_set_count(&l->grid,cat_rows(l));
}

int cat_rows(const cat_level_t *l)
{
	if (!l || !l->feed) return 0;
	return l->norder + (l->feed->prev ? 1 : 0) + (l->feed->next ? 1 : 0);
}

cat_row_kind_t cat_row(const cat_level_t *l, int r, int *entry)
{
	if(!l || !l->feed || r<0 || r>=cat_rows(l)) { *entry=-1; return ROW_NEXT; }
 int first = l->feed->prev ? 1 : 0;
	*entry = -1;
	if (r < first) return ROW_PREV;
	if (r - first >= l->norder) return ROW_NEXT;
	*entry = l->order[r - first];
	return ROW_ENTRY;
}
