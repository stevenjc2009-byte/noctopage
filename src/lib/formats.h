/* formats — file-kind tables for the library scan and the openers (pure C, no platform includes).
 * The extension lists follow docs/PLAN.md "Formats". */
#ifndef NOCTO_FORMATS_H
#define NOCTO_FORMATS_H

#include <stddef.h>

typedef enum {
	FMT_NONE = 0,
	FMT_COMIC_ARCHIVE, /* cbz zip cbr rar cb7 7z cbt tar */
	FMT_COMIC_FOLDER,  /* a directory of images; never returned by formats_kind_for_path() */
	FMT_REFLOW,        /* epub mobi fb2 txt html htm xhtml */
	FMT_FIXED          /* pdf xps oxps svg */
} fmt_kind_t;

/* Kind of a file by the extension of its final path component, ASCII case-insensitive.
 * FMT_NONE for NULL, no extension, or an unknown extension. Never returns FMT_COMIC_FOLDER. */
fmt_kind_t formats_kind_for_path(const char *path);

/* 1 when name (or a path) ends in jpg jpeg png gif bmp webp tif tiff, ASCII case-insensitive. */
int formats_is_image(const char *name);

/* Short label for a kind: "comic", "folder", "book", "document", "none". Static strings. */
const char *formats_kind_name(fmt_kind_t k);

/* Grid-badge text: the upper-case extension of the final path component ("CBZ", "EPUB"), cut to
 * FORMATS_BADGE_MAX - 1 bytes; "DIR" when there is no extension. NULL path -> "DIR". */
#define FORMATS_BADGE_MAX 8
/* Reentrant form: writes into out (capacity cap) and returns out. cap == 0 returns "". */
const char *formats_badge_r(const char *path, char *out, size_t cap);
/* Convenience form: returns a pointer into ONE static buffer shared by all callers. Not thread
 * safe; the next call overwrites it. Use it from the main thread only, or use formats_badge_r. */
const char *formats_badge(const char *path);

#endif
