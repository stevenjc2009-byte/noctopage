/* Shared fake filesystem and helpers for test_scan.c and test_scan_more.c (one translation unit
 * each; every function is static inline so an unused one is not a warning). */
#ifndef NOCTO_TEST_SCAN_FAKE_H
#define NOCTO_TEST_SCAN_FAKE_H

#include "test.h"

#include "core/strutil.h"
#include "lib/scan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- fake filesystem: a node list; a directory lists the nodes whose parent is its path ---- */

#define MAX_NODES 1024
#define PATHLEN 1400

typedef struct {
	char parent[PATHLEN], name[256];
	int is_dir;
	uint64_t size;
	int64_t mtime;
} node_t;
typedef struct {
	char path[PATHLEN];
	int pos;
} handle_t;
typedef struct {
	char path[PATHLEN];
	fmt_kind_t kind;
	uint64_t size;
	int64_t mtime;
	int image_count;
} got_t;

static node_t g_nodes[MAX_NODES];
static int g_nnodes;
static char g_fail_open[PATHLEN]; /* opening this directory fails */
static char g_fail_read[PATHLEN]; /* listing this directory fails after g_fail_read_after entries */
static int g_fail_read_after;
static char g_opened[MAX_NODES][PATHLEN];
static int g_nopened, g_handles, g_reads, g_cancel_at_read;
static volatile int g_cancel;
static got_t g_items[MAX_NODES];
static int g_nitems, g_stop_after, g_cancel_after_items, g_dirs;

static inline void fake_reset(void)
{
	g_nnodes = g_fail_read_after = g_nopened = g_handles = g_reads = g_cancel_at_read = 0;
	g_nitems = g_stop_after = g_cancel_after_items = 0;
	g_fail_open[0] = g_fail_read[0] = '\0';
	g_cancel = 0;
}

static inline void add_node(const char *parent, const char *name, int is_dir, uint64_t size, int64_t mt)
{
	node_t *n = &g_nodes[g_nnodes++];
	if (g_nnodes > MAX_NODES)
		exit(2);
	snprintf(n->parent, sizeof n->parent, "%s", parent);
	snprintf(n->name, sizeof n->name, "%s", name);
	n->is_dir = is_dir;
	n->size = size;
	n->mtime = mt;
}

static inline int has_dir(const char *path)
{
	char joined[PATHLEN];
	int i;
	for (i = 0; i < g_nnodes; i++)
		if (g_nodes[i].is_dir &&
		    path_join(joined, sizeof joined, g_nodes[i].parent, g_nodes[i].name) == 0 &&
		    strcmp(joined, path) == 0)
			return 1;
	return 0;
}

/* Registers "ux0:a/b/c" as a directory together with its missing ancestors; roots end in ':'. */
static inline void ensure_dir(const char *path)
{
	char parent[PATHLEN];
	if (path[strlen(path) - 1] == ':' || has_dir(path))
		return;
	path_dirname(parent, sizeof parent, path);
	ensure_dir(parent);
	add_node(parent, path_basename(path), 1, 0, 0);
}

/* One entry: "path" is a file, "path/" a directory; optional "#size,mtime" (default 100,5). */
static inline void T1(const char *spec)
{
	char path[PATHLEN], parent[PATHLEN];
	unsigned long long size = 100;
	long long mt = 5;
	size_t n;
	const char *hash = strchr(spec, '#');

	snprintf(path, sizeof path, "%.*s", hash ? (int)(hash - spec) : (int)strlen(spec), spec);
	if (hash)
		sscanf(hash + 1, "%llu,%lld", &size, &mt);
	n = strlen(path);
	if (path[n - 1] == '/') {
		path[n - 1] = '\0';
		ensure_dir(path);
		return;
	}
	path_dirname(parent, sizeof parent, path);
	ensure_dir(parent);
	add_node(parent, path_basename(path), 0, size, mt);
}

/* Several entries separated by '|'. */
static inline void T(const char *specs)
{
	char one[PATHLEN];
	const char *p = specs;
	while (*p) {
		const char *bar = strchr(p, '|');
		size_t len = bar ? (size_t)(bar - p) : strlen(p);
		snprintf(one, sizeof one, "%.*s", (int)len, p);
		T1(one);
		p += len + (bar ? 1 : 0);
	}
}

static inline void *fk_open(void *u, const char *path)
{
	handle_t *h;
	(void)u;
	if (g_nopened < MAX_NODES)
		snprintf(g_opened[g_nopened++], PATHLEN, "%s", path);
	if (strcmp(g_fail_open, path) == 0)
		return NULL;
	if (path[strlen(path) - 1] != ':' && !has_dir(path))
		return NULL;
	h = calloc(1, sizeof *h);
	snprintf(h->path, sizeof h->path, "%s", path);
	g_handles++;
	return h;
}

static inline int fk_read(void *u, void *dir, scan_dirent_t *out)
{
	handle_t *h = dir;
	int i, seen = 0;
	(void)u;
	g_reads++;
	if (g_cancel_at_read && g_reads == g_cancel_at_read)
		g_cancel = 1;
	if (g_fail_read[0] && strcmp(g_fail_read, h->path) == 0 && h->pos >= g_fail_read_after)
		return -1;
	for (i = 0; i < g_nnodes; i++) {
		if (strcmp(g_nodes[i].parent, h->path) != 0 || seen++ < h->pos)
			continue;
		memset(out, 0, sizeof *out);
		snprintf(out->name, sizeof out->name, "%s", g_nodes[i].name);
		out->is_dir = g_nodes[i].is_dir;
		out->size = g_nodes[i].size;
		out->mtime = g_nodes[i].mtime;
		h->pos++;
		return 1;
	}
	return 0;
}

static inline void fk_close(void *u, void *dir)
{
	(void)u;
	g_handles--;
	free(dir);
}

static const scan_fs_t FS = { fk_open, fk_read, fk_close, NULL };

static inline int collect(void *user, const scan_item_t *it)
{
	(void)user;
	if (g_nitems < MAX_NODES) {
		got_t *g = &g_items[g_nitems];
		snprintf(g->path, sizeof g->path, "%s", it->path);
		g->kind = it->kind;
		g->size = it->size;
		g->mtime = it->mtime;
		g->image_count = it->image_count;
	}
	g_nitems++;
	if (g_cancel_after_items && g_nitems == g_cancel_after_items)
		g_cancel = 1;
	return g_stop_after && g_nitems >= g_stop_after;
}

static inline int run(const char *root, int depth)
{
	g_nitems = g_nopened = g_reads = 0;
	return scan_tree(&FS, root, depth, collect, NULL, &g_cancel, &g_dirs);
}

static inline const got_t *find(const char *path)
{
	int i;
	for (i = 0; i < g_nitems && i < MAX_NODES; i++)
		if (strcmp(g_items[i].path, path) == 0)
			return &g_items[i];
	return NULL;
}

static inline int was_opened(const char *path)
{
	int i;
	for (i = 0; i < g_nopened; i++)
		if (strcmp(g_opened[i], path) == 0)
			return 1;
	return 0;
}

static inline void expect_item(int line, const char *path, int want, fmt_kind_t kind)
{
	const got_t *g = find(path);
	g_checks++;
	if (want && (!g || g->kind != kind)) {
		g_failed++;
		printf("FAIL %s:%d: \"%s\" %s (kind wanted %d)\n", __FILE__, line, path,
		       g ? "reported with the wrong kind" : "was NOT reported", (int)kind);
	} else if (!want && g) {
		g_failed++;
		printf("FAIL %s:%d: \"%s\" was reported but must not be\n", __FILE__, line, path);
	}
}
#define HAS(path, kind) expect_item(__LINE__, (path), 1, (kind))
#define HAS_NOT(path) expect_item(__LINE__, (path), 0, FMT_NONE)
#define ARCH FMT_COMIC_ARCHIVE
#define FOLDER FMT_COMIC_FOLDER

#endif
