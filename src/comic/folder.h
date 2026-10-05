/* folder — a directory of image files, with the same index/name/size/read shape as
 * comic/archive. Pure POSIX (opendir/readdir/stat/fopen), no psp2 code.
 *
 * Entries are the regular files directly inside the directory (no recursion) that the
 * caller's predicate accepts, sorted by plain strcmp so the order is deterministic;
 * natural page order is comic/pagelist's job.
 *
 * Threads: no globals. One handle per thread at a time. */
#ifndef NOCTO_FOLDER_H
#define NOCTO_FOLDER_H

#include <stddef.h>
#include <stdint.h>

#define FOLDER_MAX_FILE (64u * 1024u * 1024u) /* folder_read refuses larger files */

#define FOLDER_OK 0
#define FOLDER_E_INDEX (-1)
#define FOLDER_E_TOOBIG (-2)
#define FOLDER_E_READ (-3)
#define FOLDER_E_NOMEM (-4)

typedef struct folder_t folder_t;

/* List `dir`. is_image(name) gets the bare file name; NULL accepts every regular file.
 * Returns NULL (message in err when errcap > 0) if the directory cannot be read. A
 * directory with no accepted files opens fine with count 0. */
folder_t *folder_open(const char *dir, int (*is_image)(const char *name), char *err,
                      size_t errcap);
void folder_close(folder_t *f); /* NULL is fine */

int folder_count(const folder_t *f);
/* Bare file name of entry i, or NULL when out of range. Valid until folder_close. */
const char *folder_name(const folder_t *f, int i);
/* File size from stat at open time (0 when out of range). */
uint64_t folder_size(const folder_t *f, int i);

/* Read file i fully into a malloc'd buffer (caller frees; one NUL byte follows the data
 * and is not counted in *len). Returns FOLDER_OK or a FOLDER_E_* code; *out and *len are
 * untouched on failure. */
int folder_read(folder_t *f, int i, uint8_t **out, size_t *len);

#endif
