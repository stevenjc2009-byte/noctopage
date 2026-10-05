#include "lib/scan.h"

#include "core/strutil.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
	const scan_fs_t *fs;
	int max_depth;
	scan_cb cb;
	void *user;
	volatile int *cancel;
	int dirs;
	int status; /* SCAN_OK while running; first stop/cancel/error code once decided */
} ctx_t;

/* Per-level scratch, heap-allocated so deep trees do not eat the worker's small stack. */
typedef struct {
	scan_dirent_t ent;
	char child[SCAN_PATH_MAX];
} frame_t;

typedef struct {
	char *buf; /* packed NUL-terminated names */
	size_t len, cap;
} names_t;

static const char *const k_skip[] = {
	"app", "patch", "addcont", "appmeta", "bgdl", "cache", "license", "savedata", "sqlite",
	"temp", "user", "vd0", "vs0", "tai", "pspemu/PSP/GAME", "data/noctopage/thumbs",
};

/* Path below the device root: "ux0:data/x" -> "data/x"; no colon -> the path itself. */
static const char *device_relative(const char *path)
{
	const char *colon = strchr(path, ':');
	const char *r = colon ? colon + 1 : path;
	while (*r == '/')
		r++;
	return r;
}

static int is_skipped(const char *path)
{
	const char *rel = device_relative(path);
	size_t i;

	for (i = 0; i < sizeof k_skip / sizeof k_skip[0]; i++)
		if (str_casecmp(rel, k_skip[i]) == 0)
			return 1;
	return 0;
}

/* Plain text and web pages inside an app's data folder ("ux0:data/...") are logs, configs and
 * help pages, not books; real books there (cbz, pdf, epub, ...) still count. */
static int is_app_data_text(const char *path)
{
	static const char *const text_ext[] = { "txt", "html", "htm", "xhtml" };
	const char *rel = device_relative(path);
	const char *ext = path_ext(path);
	size_t i;

	if (!ext || !((rel[0] | 32) == 'd' && (rel[1] | 32) == 'a' && (rel[2] | 32) == 't' &&
	              (rel[3] | 32) == 'a' && rel[4] == '/'))
		return 0;
	for (i = 0; i < sizeof text_ext / sizeof text_ext[0]; i++)
		if (str_casecmp(ext, text_ext[i]) == 0)
			return 1;
	return 0;
}

static int names_add(names_t *n, const char *name)
{
	size_t need = strlen(name) + 1;

	if (n->len + need > n->cap) {
		size_t cap = n->cap ? n->cap * 2 : 512;
		char *p;
		while (cap < n->len + need)
			cap *= 2;
		p = realloc(n->buf, cap);
		if (!p)
			return -1;
		n->buf = p;
		n->cap = cap;
	}
	memcpy(n->buf + n->len, name, need);
	n->len += need;
	return 0;
}

static int cancelled(ctx_t *c)
{
	if (c->status == SCAN_OK && c->cancel && *c->cancel)
		c->status = SCAN_CANCELLED;
	return c->status != SCAN_OK;
}

static void report(ctx_t *c, const char *path, fmt_kind_t kind, uint64_t size, int64_t mtime,
                   int image_count)
{
	scan_item_t it;

	it.path = path;
	it.kind = kind;
	it.size = size;
	it.mtime = mtime;
	it.image_count = image_count;
	if (c->cb(c->user, &it) != 0 && c->status == SCAN_OK)
		c->status = SCAN_STOPPED;
}

/* Lists one directory, reports its files, recurses into its subdirectories, then decides about
 * the folder comic. Returns 1 when images exist in this directory or below, else 0. */
static int scan_dir(ctx_t *c, const char *path, int depth)
{
	frame_t *f;
	void *dir;
	names_t subs = { NULL, 0, 0 };
	int img_n = 0, sub_images = 0, read_err = 0;
	uint64_t img_size = 0;
	int64_t img_mtime = 0;
	size_t off;

	if (cancelled(c))
		return 0;
	f = malloc(sizeof *f);
	if (!f) {
		c->status = SCAN_ERR_NOMEM;
		return 0;
	}
	dir = c->fs->open_dir(c->fs->fsuser, path);
	if (!dir) {
		free(f);
		return 0;
	}
	c->dirs++;

	while (!cancelled(c)) {
		const char *name = f->ent.name;
		int r = c->fs->read_dir(c->fs->fsuser, dir, &f->ent);

		if (r == 0)
			break;
		if (r < 0) {
			read_err = 1;
			break;
		}
		f->ent.name[sizeof f->ent.name - 1] = '\0';
		if (!name[0] || name[0] == '.') /* hidden files and directories, "." and ".." */
			continue;
		if (f->ent.is_dir) {
			if (depth + 1 > c->max_depth)
				continue;
			if (path_join(f->child, sizeof f->child, path, name) != 0 || is_skipped(f->child))
				continue;
			if (names_add(&subs, name) != 0)
				c->status = SCAN_ERR_NOMEM;
		} else if (formats_is_image(name)) {
			img_n++;
			img_size += f->ent.size;
			if (img_n == 1 || f->ent.mtime > img_mtime)
				img_mtime = f->ent.mtime;
		} else {
			fmt_kind_t kind = formats_kind_for_path(name);
			if (kind != FMT_NONE && path_join(f->child, sizeof f->child, path, name) == 0 &&
			    !is_app_data_text(f->child))
				report(c, f->child, kind, f->ent.size, f->ent.mtime, 0);
		}
	}
	c->fs->close_dir(c->fs->fsuser, dir);

	for (off = 0; off < subs.len && c->status == SCAN_OK;) {
		const char *name = subs.buf + off;
		off += strlen(name) + 1;
		/* Cannot fail: the same join succeeded when the name was collected. */
		(void)path_join(f->child, sizeof f->child, path, name);
		if (scan_dir(c, f->child, depth + 1))
			sub_images = 1;
	}

	if (c->status == SCAN_OK && !read_err && img_n >= 2 && !sub_images)
		report(c, path, FMT_COMIC_FOLDER, img_size, img_mtime, img_n);

	free(subs.buf);
	free(f);
	return img_n > 0 || sub_images;
}

int scan_tree(const scan_fs_t *fs, const char *root, int max_depth, scan_cb cb, void *user,
              volatile int *cancel, int *dirs_visited)
{
	ctx_t c;

	if (dirs_visited)
		*dirs_visited = 0;
	if (!fs || !fs->open_dir || !fs->read_dir || !fs->close_dir || !root || !cb || max_depth < 0)
		return SCAN_ERR_ARGS;
	if (max_depth > SCAN_MAX_DEPTH)
		max_depth = SCAN_MAX_DEPTH;
	memset(&c, 0, sizeof c);
	c.fs = fs;
	c.max_depth = max_depth;
	c.cb = cb;
	c.user = user;
	c.cancel = cancel;

	scan_dir(&c, root, 0);
	if (dirs_visited)
		*dirs_visited = c.dirs;
	if (c.status == SCAN_OK && c.dirs == 0)
		return SCAN_ERR_ROOT;
	return c.status;
}
