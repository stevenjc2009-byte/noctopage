/* archive_priv.h - internals shared by comic/archive.c (libarchive walk, reads, API)
 * and comic/zipfast.c (central-directory listing, direct reads). Not for other users:
 * include comic/archive.h instead. */
#ifndef NOCT_ARCHIVE_PRIV_H
#define NOCT_ARCHIVE_PRIV_H

#include <locale.h>
#include <stddef.h>
#include <stdint.h>

#define ERRCAP 160

struct archive; /* libarchive's reader, completed by <archive.h> where used */

struct archive_t {
    char *path;
    struct archive *rd; /* positioned reader, or NULL */
    int cur;            /* index of the entry whose header rd last returned, -1 = none */
    int consumed;       /* 1 once cur's data was read or skipped */
    int count, cap;
    char **names;
    uint64_t *sizes;
    /* ZIP fast path: per-entry local-header offset, method and compressed size from the
     * central directory, plus whether the entry is read directly (stored/deflated, known
     * sizes, no encryption). Direct reads never touch rd, so sequential libarchive state
     * stays valid across them. Zeros for non-ZIP listings. */
    uint64_t *loff;
    uint16_t *meth;
    uint64_t *csz;
    char *direct;
    locale_t loc;       /* UTF-8 LC_CTYPE, made current (this thread only) around libarchive calls */
    char err[ERRCAP];
};

/* Append (name, size) to the listing, growing every parallel array; zeros the new
 * entry's ZIP extras (owned by zipfast.c's listing pass). Returns 0 or -1. */
int list_push(archive_t *a, const char *name, uint64_t size);

/* Fill the listing from the ZIP central directory: 1 filled, 0 = use libarchive. */
int zip_list(archive_t *a);

/* Read a direct-flagged entry straight from its local header (ARC_OK / ARC_E_READ /
 * ARC_E_NOMEM; out and len untouched unless ARC_OK). */
int zipfast_read(archive_t *a, int i, uint8_t **out, size_t *len);

#endif
