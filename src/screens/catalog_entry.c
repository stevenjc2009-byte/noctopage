/* catalog_entry.c - the book panel (cover, description, Download) and adding or editing a
 * server with the on-screen keyboard. Entries with OPDS-PSE page streaming also offer
 * Read now, which opens the reader on the page URLs without downloading the book. */
#include "screens/catalog_priv.h"
#include "screens/screens.h"

#include "app.h"
#include "core/log.h"
#include "net/dlq.h"
#include "net/catalog_downloads.h"
#include "ui/menu.h"
#include "ui/osk.h"

#include <stdio.h>
#include <string.h>

static const opds_acq *g_cand[MENU_MAX];
static char g_label[MENU_MAX][64];
static const char *g_ptr[MENU_MAX];
static int g_ncand;
static int g_stream; /* this entry offers PSE page streaming (menu row 0) */

static const opds_entry *entry(void)
{
	const cat_level_t *l = cat_top();
	if (!l || !l->feed || C.detail < 0 || C.detail >= l->feed->nentries) return NULL;
	return &l->feed->entries[C.detail];
}

/* Downloadable links, the preferred format first (opds_best_acquisition). */
static void candidates(const opds_entry *e)
{
 const cat_level_t *level=cat_top();
 int comic=level&&catalog_api_kind(level->url)>0;
 g_ncand=catalog_downloads(e,comic,g_cand,MENU_MAX-1);
}

void cat_detail_open(int i)
{
	const opds_entry *e;
	C.detail = i;
	candidates(entry());
	e = entry();
	g_stream = e && e->pse_href && e->pse_href[0] && e->pse_count > 0;
}

static void download(const opds_acq *a)
{
	const opds_entry *e = entry();
	const char *fmt = opds_acq_format(a);
	int64_t id;
	if (dlq_add(a->href, cat_auth_url(a->href), e->title, strcmp(fmt, "other") ? fmt : "", &id) < 0) {
		cat_toast("Could not start the download");
		return;
	}
	char msg[160];
	snprintf(msg, sizeof msg, "Downloading \xE2\x80\x9C%s\xE2\x80\x9D: see Downloads", e->title);
	cat_toast(msg);
	log_printf("catalog: download %s (%s)", a->href, fmt);
}

static void stream_now(void)
{
	const opds_entry *e = entry();
	const http_auth_t *a = e?cat_auth_url(e->pse_href):NULL;
	if (!e || !e->pse_href || e->pse_count <= 0) return;
	app_open_stream(e->title, e->pse_href, e->pse_count, a && a->user ? a->user : "",
	                a && a->pass ? a->pass : "");
	log_printf("catalog: stream %s (%d pages)", e->pse_href, e->pse_count);
}

static void open_formats(void)
{
	int o = 0, i;
	if (g_stream) {
		const opds_entry *e = entry();
		snprintf(g_label[0], sizeof g_label[0], "Read now (%d pages)",
		         e ? e->pse_count : 0);
		g_ptr[0] = g_label[0];
		o = 1;
	}
	for (i = 0; i < g_ncand && o + i < MENU_MAX; i++) {
		char fmt[16];
		snprintf(fmt, sizeof fmt, "%s", opds_acq_format(g_cand[i]));
		for (char *p = fmt; *p; p++)
			if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
		if (g_cand[i]->length > 0)
			snprintf(g_label[o + i], sizeof g_label[0], "Download %s (%.1f MB)", fmt,
			         (double)g_cand[i]->length / (1024.0 * 1024.0));
		else
			snprintf(g_label[o + i], sizeof g_label[0], "Download %s", fmt);
		g_ptr[o + i] = g_label[o + i];
	}
	menu_open(g_stream ? "Read or download" : "Download", g_ptr, o + i);
}

void cat_detail_update(const input_t *in)
{
	if (menu_is_open()) {
		int k = menu_update(in);
		if (k == 0 && g_stream) stream_now();
		else if (k >= (g_stream ? 1 : 0) && k - (g_stream ? 1 : 0) < g_ncand)
			download(g_cand[k - (g_stream ? 1 : 0)]);
		return;
	}
	if (!entry() || (in->pressed & IN_BACK)) {
		C.detail = -1;
		return;
	}
	if (!(in->pressed & IN_CONFIRM) && !(in->touch_began && in->began_x>=CAT_X0 && in->began_x<CAT_X0+180 && in->began_y>=354 && in->began_y<398)) return;
 if(!g_stream && !g_ncand) { cat_toast("No supported free download is available"); return; }
	if (g_stream && g_ncand == 0) stream_now();
	else if (!g_stream && g_ncand == 1) download(g_cand[0]);
	else open_formats();
}

int cat_detail_hints(hint_t *out, int max)
{
	hint_t h[] = {{GLYPH_BACK, "Back"}, {GLYPH_CONFIRM, g_stream ? "Read / download" : "Download"}};
	return screen_copy_hints(h, g_ncand > 0 || g_stream ? 2 : 1, out, max);
}

/* ---- servers ------------------------------------------------------------------------ */

/* The keyboard, one field after another. Returns 0 when the user cancelled any step. */
static int ask_server(db_server_t *s)
{
	char buf[OSK_MAX_TEXT + 1];
	for (;;) {
		if (osk_prompt("OPDS URL or Archive collection link", s->url[0] ? s->url : "https://", OSK_URL, buf,
		               sizeof buf) != 1)
			return 0;
		if (strlen(buf)<sizeof s->url && catalog_url_valid(buf))
			break;
		cat_toast("The address must start with http:// or https://");
	}
	snprintf(s->url, sizeof s->url, "%s", buf);
	if (!s->name[0]) { /* default name: the host */
		const char *h = strstr(s->url, "://") + 3;
		size_t n = strcspn(h, "/:?#");
		snprintf(s->name, sizeof s->name, "%.*s", (int)(n < sizeof s->name ? n : sizeof s->name - 1), h);
	}
	if (osk_prompt("Name", s->name, OSK_TEXT, buf, sizeof buf) != 1) return 0;
	if (buf[0]) snprintf(s->name, sizeof s->name, "%.63s", buf);
	if (osk_prompt("User name (leave empty if none)", s->username, OSK_TEXT, buf, sizeof buf) != 1)
		return 0;
	snprintf(s->username, sizeof s->username, "%.63s", buf);
	if (!s->username[0]) {
		s->password[0] = '\0';
		return 1;
	}
	if (osk_prompt("Password", "", OSK_PASSWORD, buf, sizeof buf) != 1) return 0;
	if (buf[0]) snprintf(s->password, sizeof s->password, "%.63s", buf); /* empty keeps the old one */
	return 1;
}

void cat_server_add(void)
{
 if(C.nsrc>=CAT_SOURCES_MAX) { cat_toast("Remove a custom source before adding another"); return; }
	db_server_t s;
	memset(&s, 0, sizeof s);
	snprintf(s.kind, sizeof s.kind, "opds");
	if (!ask_server(&s)) return;
	int64_t id;
	if (db_server_add(&s, &id) < 0) {
		cat_toast(db_last_error());
		return;
	}
	log_printf("catalog: server added %s", s.url);
	cat_toast("Server added");
}

void cat_server_edit(int i)
{
	db_server_t s;
	memset(&s, 0, sizeof s);
	s.id = C.src[i].server_id;
	snprintf(s.name, sizeof s.name, "%s", C.src[i].name);
	snprintf(s.url, sizeof s.url, "%s", C.src[i].url);
	snprintf(s.username, sizeof s.username, "%s", C.src[i].user);
	snprintf(s.password, sizeof s.password, "%s", C.src[i].pass);
	snprintf(s.kind, sizeof s.kind, "opds");
	if (!ask_server(&s)) return;
	if (db_server_update(&s) < 0)
		cat_toast(db_last_error());
	else
		cat_toast("Server saved");
}
