/* archive — read comic archives (cbz/zip, cbr/rar incl. RAR5, cb7/7z, cbt/tar) through
 * libarchive. Regular-file entries only, indexed in archive order (listing pass at open).
 *
 * Threads: no globals. One handle may be used by one thread at a time; separate handles
 * may be used from separate threads concurrently. */
#ifndef NOCTO_ARCHIVE_H
#define NOCTO_ARCHIVE_H

#include <stddef.h>
#include <stdint.h>

#define ARC_MAX_ENTRY (64u * 1024u * 1024u) /* arc_read refuses larger entries */

#define ARC_OK 0
#define ARC_E_INDEX (-1)  /* index out of range */
#define ARC_E_TOOBIG (-2) /* entry larger than ARC_MAX_ENTRY */
#define ARC_E_READ (-3)   /* corrupt / truncated / unreadable data */
#define ARC_E_NOMEM (-4)

typedef struct archive_t archive_t;

/* Open and list `path`. Returns NULL on failure (not an archive, truncated, unreadable,
 * out of memory) with a message in err (if errcap > 0). The file is read in 256 KiB blocks. */
archive_t *arc_open(const char *path, char *err, size_t errcap);
void arc_close(archive_t *a); /* NULL is fine */

int arc_count(const archive_t *a);
/* UTF-8 path of entry i inside the archive; NULL if i is out of range. Valid until arc_close. */
const char *arc_name(const archive_t *a, int i);
/* Size in bytes recorded in the archive header (0 when i is out of range or unrecorded). */
uint64_t arc_size(const archive_t *a, int i);

/* Read entry i fully into a malloc'd buffer (caller frees; one extra NUL byte follows the
 * data and is not counted in *len). Returns ARC_OK or an ARC_E_* code; *out and *len are
 * untouched on failure. Sequential reads (i increasing) cost one pass over the file;
 * reading an index at or before the last one read reopens the file and skips forward. */
int arc_read(archive_t *a, int i, uint8_t **out, size_t *len);

/* Message for the last failed arc_read on this handle ("" if none). */
const char *arc_error(const archive_t *a);

#endif
