/* scan — recursive library walker over an injected filesystem-ops table (pure C).
 *
 * Rules (docs/PLAN.md "Storage scan"):
 *  - root is depth 0; directories deeper than max_depth are not listed.
 *  - a directory or file whose name starts with '.' is skipped (so AppleDouble "._page.jpg" never
 *    counts as an image); so is any directory whose path
 *    relative to the device root (the text after the first ':') equals, ASCII case-insensitively,
 *    one of: app patch addcont appmeta bgdl cache license savedata sqlite temp user vd0 vs0 tai
 *    pspemu/PSP/GAME data/noctopage/thumbs. The root itself is never skipped.
 *  - every file whose extension has a kind (formats_kind_for_path) is reported, except .txt
 *    .html .htm .xhtml files under the device's own data/ folder (other apps' logs and pages).
 *  - folder comic: a directory with >= 2 image files and NO visited subdirectory that holds
 *    images (recursively) is reported once as FMT_COMIC_FOLDER (size = sum of image sizes,
 *    mtime = newest image, image_count). A directory whose subtree has images below it reports
 *    none of its loose images. Book files in such a directory are still reported as files.
 *    A directory whose listing failed part-way is never reported as a folder comic.
 *  - a directory that cannot be opened is skipped; the walk continues.
 *
 * Reporting order: files of a directory as listed, then its subdirectories (depth first), then
 * the directory itself when it is a folder comic. item->path is valid only during the callback.
 *
 * Stack: a call needs under 2 KiB of stack for any max_depth (about 100 bytes per directory
 * level). The per-level path buffer (1 KiB), dirent (about 280 bytes) and the list of
 * subdirectory names live on the heap, so a 64 KiB Vita worker thread stack is plenty. */
#ifndef NOCTO_SCAN_H
#define NOCTO_SCAN_H

#include <stdint.h>

#include "lib/formats.h"

#define SCAN_PATH_MAX 1024 /* longest path the walker builds; longer entries are skipped */
#define SCAN_MAX_DEPTH 32  /* max_depth is clamped to this */

typedef struct {
	char name[256];
	int is_dir;
	uint64_t size;
	int64_t mtime;
} scan_dirent_t;

typedef struct {
	/* Returns an opaque handle, or NULL when the directory cannot be opened. */
	void *(*open_dir)(void *fsuser, const char *path);
	/* 1 = *out filled, 0 = end of directory, < 0 = error (listing ends early). */
	int (*read_dir)(void *fsuser, void *dir, scan_dirent_t *out);
	void (*close_dir)(void *fsuser, void *dir);
	void *fsuser;
} scan_fs_t;

typedef struct {
	const char *path;
	fmt_kind_t kind;
	uint64_t size;
	int64_t mtime;
	int image_count; /* FMT_COMIC_FOLDER only, else 0 */
} scan_item_t;

/* Return non-zero to stop the whole scan. */
typedef int (*scan_cb)(void *user, const scan_item_t *item);

#define SCAN_OK 0
#define SCAN_STOPPED 1     /* the callback returned non-zero */
#define SCAN_CANCELLED 2   /* *cancel became non-zero */
#define SCAN_ERR_ARGS (-1) /* NULL argument or negative max_depth */
#define SCAN_ERR_NOMEM (-2)
#define SCAN_ERR_ROOT (-3) /* the root directory could not be opened */

/* Walk root (e.g. "ux0:", "ux0:books"). cancel may be NULL; it is polled for every directory
 * entry. dirs_visited (may be NULL) receives the number of directories successfully opened,
 * the root included. Returns one of the SCAN_* codes. */
int scan_tree(const scan_fs_t *fs, const char *root, int max_depth, scan_cb cb, void *user,
              volatile int *cancel, int *dirs_visited);

#endif
