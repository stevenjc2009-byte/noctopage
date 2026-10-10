#include "lib/library.h"

#include "comic/folder.h"
#include "comic/pagelist.h"
#include "core/affinity.h"
#include "core/log.h"
#include "core/paths.h"
#include "core/strutil.h"
#include "lib/bookid.h"
#include "lib/db.h"
#include "lib/formats.h"
#include "lib/scan_vita.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCAN_DEPTH 8         /* PLAN "Storage scan" */
#define BUMP_EVERY 100       /* new books between list refreshes during a scan */
#define STACK_BYTES (256 * 1024)

static pthread_t g_thread;
static int g_joinable;
static volatile int g_running, g_cancel, g_found;
static volatile unsigned g_gen;
static int g_new_since_bump;

static void bump(void) { __atomic_add_fetch(&g_gen, 1, __ATOMIC_RELEASE); }

static int db_kind_of(fmt_kind_t k)
{
	switch (k) {
	case FMT_COMIC_ARCHIVE: return DB_KIND_ARCHIVE;
	case FMT_COMIC_FOLDER: return DB_KIND_FOLDER;
	case FMT_REFLOW: return DB_KIND_REFLOW;
	case FMT_FIXED: return DB_KIND_FIXED;
	default: return 0;
	}
}

/* Title from the file name: final component, extension dropped (folders keep theirs). */
static void title_of(const char *path, int is_folder, char *out, size_t cap)
{
	const char *base = path_basename(path);
	size_t n = strlen(base);
	const char *dot = is_folder ? NULL : strrchr(base, '.');
	if (dot && dot > base) n = (size_t)(dot - base);
	if (n >= cap) n = cap - 1;
	memcpy(out, base, n);
	out[n] = '\0';
}

/* Folder comic id: image names in reading order with their sizes. */
static int folder_id(const char *path, uint64_t *id)
{
	folder_t *f = folder_open(path, formats_is_image, NULL, 0);
	if (!f) return -1;
	int n = folder_count(f), r = -1;
	const char **names = malloc(sizeof *names * (size_t)(n > 0 ? n : 1));
	const char **sorted = malloc(sizeof *sorted * (size_t)(n > 0 ? n : 1));
	uint64_t *sizes = malloc(sizeof *sizes * (size_t)(n > 0 ? n : 1));
	int *idx = malloc(sizeof *idx * (size_t)(n > 0 ? n : 1));
	if (names && sorted && sizes && idx) {
		for (int i = 0; i < n; i++) names[i] = folder_name(f, i);
		int np = pagelist_build(names, n, idx);
		for (int k = 0; k < np; k++) {
			sorted[k] = names[idx[k]];
			sizes[k] = folder_size(f, idx[k]);
		}
		*id = bookid_folder(sorted, sizes, np);
		r = 0;
	}
	free(names);
	free(sorted);
	free(sizes);
	free(idx);
	folder_close(f);
	return r;
}

static int on_item(void *user, const scan_item_t *it)
{
	(void)user;
	int kind = db_kind_of(it->kind);
	if (!kind) return 0;
	g_found++;
	if (db_scan_lookup(it->path, it->size, it->mtime) == 1) return 0;

	db_book_t *b = calloc(1, sizeof *b);
	if (!b) return 0;
	int ok = kind == DB_KIND_FOLDER ? folder_id(it->path, &b->id) == 0
	                                : bookid_file(it->path, &b->id, NULL) == 0;
	if (ok) {
		snprintf(b->path, sizeof b->path, "%s", it->path);
		title_of(it->path, kind == DB_KIND_FOLDER, b->title, sizeof b->title);
		b->kind = kind;
		b->size = it->size;
		b->mtime = it->mtime;
		if (db_book_upsert(b) < 0)
			log_printf("library: upsert %s: %s", it->path, db_last_error());
		else if (++g_new_since_bump >= BUMP_EVERY) {
			g_new_since_bump = 0;
			bump();
		}
	} else {
		log_printf("library: cannot hash %s", it->path);
	}
	free(b);
	return 0;
}

static void *scan_main(void *arg)
{
	(void)arg;
	affinity_use_all_cores();
	int complete = db_scan_begin() >= 0;
	if (complete) {
		int dirs = 0;
		int r = scan_tree(scan_vita_fs(), PATH_BOOKS_DIR, SCAN_DEPTH,
		                  on_item, NULL, &g_cancel, &dirs);
		log_printf("library: %s r=%d dirs=%d found=%d", PATH_BOOKS_DIR, r, dirs, g_found);
		/* A failed or cancelled scan must not mark existing books missing. */
		if (r != SCAN_OK) complete = 0;
	}
	/* Only a complete walk may mark unseen books missing. */
	if (complete && db_scan_end() < 0)
		log_printf("library: scan end: %s", db_last_error());
	log_printf("library: scan %s, %d books", complete ? "done" : "incomplete", g_found);
	bump();
	__atomic_store_n(&g_running, 0, __ATOMIC_RELEASE);
	return NULL;
}

void library_scan_start(void)
{
	if (g_running) return;
	if (g_joinable) {
		pthread_join(g_thread, NULL);
		g_joinable = 0;
	}
	g_cancel = 0;
	g_found = 0;
	g_new_since_bump = 0;
	g_running = 1;
	pthread_attr_t at;
	pthread_attr_init(&at);
	pthread_attr_setstacksize(&at, STACK_BYTES);
	if (pthread_create(&g_thread, &at, scan_main, NULL) == 0) {
		g_joinable = 1;
	} else {
		g_running = 0;
		log_printf("library: pthread_create failed");
	}
	pthread_attr_destroy(&at);
}

void library_scan_stop(void)
{
	g_cancel = 1;
	if (g_joinable) {
		pthread_join(g_thread, NULL);
		g_joinable = 0;
	}
}

int library_scan_running(void) { return __atomic_load_n(&g_running, __ATOMIC_ACQUIRE); }
int library_scan_found(void) { return g_found; }
unsigned library_generation(void) { return __atomic_load_n(&g_gen, __ATOMIC_ACQUIRE); }
