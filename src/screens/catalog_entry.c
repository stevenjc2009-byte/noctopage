/* catalog_entry.c - the book panel (cover, description, Download) and adding or editing a
 * server with the on-screen keyboard. */
#include "screens/catalog_priv.h"
#include "screens/screens.h"

#include "core/log.h"
#include "net/dlq.h"
#include "ui/menu.h"
#include "ui/osk.h"

#include <stdio.h>
#include <string.h>

static const opds_acq *g_cand[MENU_MAX];
static char g_label[MENU_MAX][64];
static const char *g_ptr[MENU_MAX];
static int g_ncand;

static const opds_entry *entry(void)
{
	const cat_level_t *l = cat_top();
	if (!l || !l->feed || C.detail < 0 || C.detail >= l->feed->nentries) return NULL;
	return &l->feed->entries[C.detail];
}

/* Downloadable links, the preferred format first (opds_best_acquisition). */
static void candidates(const opds_entry *e)
{
	g_ncand = 0;
	const opds_acq *best = opds_best_acquisition(e, NULL);
	if (!best) return;
	g_cand[g_ncand++] = best;
	for (int i = 0; i < e->nacq && g_ncand < MENU_MAX; i++) {
		const opds_acq *a = &e->acq[i];
		if (a == best || (a->kind != OPDS_ACQ_GENERIC && a->kind != OPDS_ACQ_OPEN_ACCESS)) continue;
		g_cand[g_ncand++] = a;
	}
}

void cat_detail_open(int i)
{
	C.detail = i;
	candidates(entry());
}

static void download(const opds_acq *a)
{
	const opds_entry *e = entry();
	const char *fmt = opds_acq_format(a);
	int64_t id;
	if (dlq_add(a->href, cat_auth(), e->title, strcmp(fmt, "other") ? fmt : "", &id) < 0) {
		cat_toast("Could not start the download");
		return;
	}
	char msg[160];
	snprintf(msg, sizeof msg, "Downloading \xE2\x80\x9C%s\xE2\x80\x9D: see Downloads", e->title);
	cat_toast(msg);
	log_printf("catalog: download %s (%s)", a->href, fmt);
}

static void open_formats(void)
{
	for (int i = 0; i < g_ncand; i++) {
		char fmt[16];
		snprintf(fmt, sizeof fmt, "%s", opds_acq_format(g_cand[i]));
		for (char *p = fmt; *p; p++)
			if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
		if (g_cand[i]->length > 0)
			snprintf(g_label[i], sizeof g_label[0], "Download %s (%.1f MB)", fmt,
			         (double)g_cand[i]->length / (1024.0 * 1024.0));
		else
			snprintf(g_label[i], sizeof g_label[0], "Download %s", fmt);
		g_ptr[i] = g_label[i];
	}
	menu_open("Download", g_ptr, g_ncand);
}

void cat_detail_update(const input_t *in)
{
	if (menu_is_open()) {
		int k = menu_update(in);
		if (k >= 0 && k < g_ncand) download(g_cand[k]);
		return;
	}
	if (!entry() || (in->pressed & IN_BACK)) {
		C.detail = -1;
		return;
	}
	if ((in->pressed & IN_CONFIRM) && g_ncand == 1) download(g_cand[0]);
	if ((in->pressed & IN_CONFIRM) && g_ncand > 1) open_formats();
}

int cat_detail_hints(hint_t *out, int max)
{
	hint_t h[] = {{GLYPH_BACK, "Back"}, {GLYPH_CONFIRM, "Download"}};
	return screen_copy_hints(h, g_ncand > 0 ? 2 : 1, out, max);
}

/* ---- servers ------------------------------------------------------------------------ */

/* The keyboard, one field after another. Returns 0 when the user cancelled any step. */
static int ask_server(db_server_t *s)
{
	char buf[OSK_MAX_TEXT + 1];
	for (;;) {
		if (osk_prompt("OPDS catalog address", s->url[0] ? s->url : "http://", OSK_URL, buf,
		               sizeof buf) != 1)
			return 0;
		if ((strncmp(buf, "http://", 7) == 0 && buf[7]) || (strncmp(buf, "https://", 8) == 0 && buf[8]))
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
