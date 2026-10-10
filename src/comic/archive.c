#include "comic/archive.h"
#include "comic/archive_priv.h"

#include <archive.h>
#include <archive_entry.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_SIZE (256 * 1024)
#define ERRCAP 160



/* libarchive converts entry names to the *current locale's* charset, and in the default "C"
 * locale (also newlib's on the Vita) it drops every non-ASCII name ("cannot be converted").
 * A per-handle UTF-8 locale, swapped in with uselocale() (thread-local, no global state)
 * around every libarchive call, makes names come out as UTF-8 for zip, 7z, rar and tar.
 * If the locale is unavailable we run without it. */
static locale_t loc_enter(const archive_t *a)
{
    return a->loc ? uselocale(a->loc) : (locale_t)0;
}

static void loc_leave(const archive_t *a, locale_t old)
{
    if (a->loc)
        uselocale(old);
}

static void set_err(char *dst, size_t cap, const char *prefix, struct archive *a)
{
    const char *m = a ? archive_error_string(a) : NULL;

    if (cap == 0)
        return;
    snprintf(dst, cap, "%s%s%s", prefix, m ? ": " : "", m ? m : "");
}

static struct archive *reader_new(const char *path, char *err, size_t errcap)
{
    struct archive *a = archive_read_new();

    if (!a) {
        if (errcap)
            snprintf(err, errcap, "out of memory");
        return NULL;
    }
    archive_read_support_format_zip(a);
    archive_read_support_format_rar(a);
    archive_read_support_format_rar5(a);
    archive_read_support_format_7zip(a);
    archive_read_support_format_tar(a);
    archive_read_support_filter_gzip(a);
    archive_read_support_filter_bzip2(a);
    archive_read_support_filter_xz(a);
    archive_read_set_options(a, "hdrcharset=UTF-8"); /* best effort: names without the flag */
    if (archive_read_open_filename(a, path, BLOCK_SIZE) != ARCHIVE_OK) {
        set_err(err, errcap, "cannot open archive", a);
        archive_read_free(a);
        return NULL;
    }
    return a;
}

static int is_file(struct archive_entry *e)
{
    return archive_entry_filetype(e) == AE_IFREG;
}

static const char *entry_name(struct archive_entry *e)
{
    const char *n = archive_entry_pathname_utf8(e);

    return n ? n : archive_entry_pathname(e);
}

int list_push(archive_t *a, const char *name, uint64_t size)
{
    char *dup;

    if (a->count == a->cap) {
        int ncap = a->cap ? a->cap * 2 : 64;
        char **nn = realloc(a->names, (size_t)ncap * sizeof *nn);
        uint64_t *ns, *no, *nc;
        uint16_t *nm;
        char *nd;

        if (!nn)
            return -1;
        ns = realloc(a->sizes, (size_t)ncap * sizeof *ns);
        if (!ns)
            return -1;
        no = realloc(a->loff, (size_t)ncap * sizeof *no);
        if (!no)
            return -1;
        nm = realloc(a->meth, (size_t)ncap * sizeof *nm);
        if (!nm)
            return -1;
        nc = realloc(a->csz, (size_t)ncap * sizeof *nc);
        if (!nc)
            return -1;
        nd = realloc(a->direct, (size_t)ncap * sizeof *nd);
        if (!nd)
            return -1;
        /* Publish everything only once all grew: a half-grown listing used to
         * survive the failure path. */
        a->names = nn;
        a->sizes = ns;
        a->loff = no;
        a->meth = nm;
        a->csz = nc;
        a->direct = nd;
        a->cap = ncap;
    }
    dup = strdup(name ? name : "");
    if (!dup)
        return -1;
    a->names[a->count] = dup;
    a->sizes[a->count] = size;
    a->loff[a->count] = 0;
    a->meth[a->count] = 0;
    a->csz[a->count] = 0;
    a->direct[a->count] = 0;
    a->count++;
    return 0;
}

/* Fill the listing; on failure returns -1 with the message in err. */
static int list_archive(archive_t *a, char *err, size_t errcap)
{
    struct archive_entry *e;
    int r;

    a->rd = reader_new(a->path, err, errcap);
    if (!a->rd) {
        if (errcap && !err[0])
            snprintf(err, errcap, "cannot open archive");
        return -1;
    }
    while ((r = archive_read_next_header(a->rd, &e)) == ARCHIVE_OK || r == ARCHIVE_WARN) {
        uint64_t size;

        if (!is_file(e))
            continue;
        size = archive_entry_size_is_set(e) ? (uint64_t)archive_entry_size(e) : 0;
        if (list_push(a, entry_name(e), size) != 0) {
            if (errcap)
                snprintf(err, errcap, "out of memory");
            return -1;
        }
        if (archive_read_data_skip(a->rd) < ARCHIVE_WARN) {
            r = ARCHIVE_FATAL;
            break;
        }
    }
    if (r != ARCHIVE_EOF) {
        set_err(err, errcap, "corrupt or truncated archive", a->rd);
        return -1;
    }
    /* Listing used up the reader; the first arc_read reopens. */
    archive_read_free(a->rd);
    a->rd = NULL;
    return 0;
}


archive_t *arc_open(const char *path, char *err, size_t errcap)
{
    archive_t *a;
    locale_t old;
    int rc;

    if (errcap)
        err[0] = '\0';
    if (!path || !*path) {
        if (errcap)
            snprintf(err, errcap, "no path");
        return NULL;
    }
    a = calloc(1, sizeof *a);
    if (!a || !(a->path = strdup(path))) {
        if (errcap)
            snprintf(err, errcap, "out of memory");
        arc_close(a);
        return NULL;
    }
    a->cur = -1;
    a->loc = newlocale(LC_CTYPE_MASK, "C.UTF-8", (locale_t)0);
    old = loc_enter(a);
    rc = zip_list(a) ? 0 : list_archive(a, err, errcap);
    loc_leave(a, old);
    if (rc != 0) {
        arc_close(a);
        return NULL;
    }
    return a;
}

void arc_close(archive_t *a)
{
    int i;

    if (!a)
        return;
    if (a->rd)
        archive_read_free(a->rd);
    for (i = 0; i < a->count; i++)
        free(a->names[i]);
    free(a->names);
    free(a->sizes);
    free(a->loff);
    free(a->meth);
    free(a->csz);
    free(a->direct);
    free(a->path);
    if (a->loc)
        freelocale(a->loc);
    free(a);
}

int arc_count(const archive_t *a)
{
    return a ? a->count : 0;
}

const char *arc_name(const archive_t *a, int i)
{
    return (a && i >= 0 && i < a->count) ? a->names[i] : NULL;
}

uint64_t arc_size(const archive_t *a, int i)
{
    return (a && i >= 0 && i < a->count) ? a->sizes[i] : 0;
}

const char *arc_error(const archive_t *a)
{
    return a ? a->err : "";
}

static void drop_reader(archive_t *a)
{
    if (a->rd)
        archive_read_free(a->rd);
    a->rd = NULL;
}

/* Position the reader on the header of entry i (data unread). */
static int seek_to(archive_t *a, int i)
{
    struct archive_entry *e;

    if (a->rd && (i < a->cur || (i == a->cur && a->consumed)))
        drop_reader(a);
    if (!a->rd) {
        a->rd = reader_new(a->path, a->err, sizeof a->err);
        a->cur = -1;
        a->consumed = 0;
        if (!a->rd)
            return ARC_E_READ;
    }
    while (a->cur < i) {
        int r;

        if (a->cur >= 0 && !a->consumed && archive_read_data_skip(a->rd) < ARCHIVE_WARN)
            return ARC_E_READ;
        do {
            r = archive_read_next_header(a->rd, &e);
        } while ((r == ARCHIVE_OK || r == ARCHIVE_WARN) && !is_file(e));
        if (r != ARCHIVE_OK && r != ARCHIVE_WARN)
            return ARC_E_READ;
        a->cur++;
        a->consumed = 0;
    }
    return ARC_OK;
}

/* Read the current entry's data into a fresh buffer, growing it (capped at ARC_MAX_ENTRY bytes of
 * data) if the recorded size was missing or too small. The buffer starts one byte larger than
 * the recorded size so that an entry of exactly that size is seen to end instead of looking full. */
static int read_data(archive_t *a, uint64_t hint, uint8_t **out, size_t *len)
{
    /* Clamp the 64-bit header size first: on 32-bit size_t a lying entry used to
     * truncate into a tiny initial buffer (or wrap the doubling below). */
    uint64_t h = hint > ARC_MAX_ENTRY ? ARC_MAX_ENTRY : hint;
    size_t cap = h ? (size_t)h + 1 : 4096, got = 0;
    uint8_t *buf = malloc(cap + 1);

    if (!buf)
        return ARC_E_NOMEM;
    for (;;) {
        la_ssize_t n;

        if (got == cap) {
            uint8_t *nb;

            if (cap > ARC_MAX_ENTRY) { /* full with more than the cap */
                free(buf);
                snprintf(a->err, sizeof a->err, "entry too large");
                return ARC_E_TOOBIG;
            }
            /* Halve-checked doubling: cap * 2 must not wrap 32-bit size_t first. */
            cap = cap > (size_t)ARC_MAX_ENTRY / 2 ? (size_t)ARC_MAX_ENTRY + 1 : cap * 2;
            nb = realloc(buf, cap + 1);
            if (!nb) {
                free(buf);
                return ARC_E_NOMEM;
            }
            buf = nb;
        }
        n = archive_read_data(a->rd, buf + got, cap - got);
        if (n == 0)
            break;
        if (n < 0) {
            if (n == ARCHIVE_RETRY)
                continue;
            set_err(a->err, sizeof a->err, "read error", a->rd);
            free(buf);
            return ARC_E_READ;
        }
        got += (size_t)n;
    }
    buf[got] = '\0';
    *out = buf;
    *len = got;
    return ARC_OK;
}

static int read_entry(archive_t *a, int i, uint8_t **out, size_t *len)
{
    int rc;

    if (!a || !out || !len || i < 0 || i >= a->count)
        return ARC_E_INDEX;
    a->err[0] = '\0';
    if (a->sizes[i] > ARC_MAX_ENTRY) {
        snprintf(a->err, sizeof a->err, "entry too large (%llu bytes)",
                 (unsigned long long)a->sizes[i]);
        return ARC_E_TOOBIG;
    }
    /* The open libarchive reader streams forward for free (and survives the file
     * being unlinked mid-read); only jump directly when it would have to reopen
     * and re-scan from the start. */
    int streaming = (a->rd && i >= a->cur && (i != a->cur || !a->consumed)) ||
                    (!a->rd && i == a->cur + 1);
    if (a->direct[i] && !streaming) {
        rc = zipfast_read(a, i, out, len);
        if (rc == ARC_OK || rc == ARC_E_NOMEM)
            return rc;
        /* A direct miss (odd corner the walker tolerates): fall through. */
        a->err[0] = '\0';
    }
    rc = seek_to(a, i);
    if (rc == ARC_OK) {
        rc = read_data(a, a->sizes[i], out, len);
        a->consumed = 1;
    } else if (!a->err[0]) {
        set_err(a->err, sizeof a->err, "cannot reach entry", a->rd);
    }
    if (rc != ARC_OK)
        drop_reader(a); /* position unknown after a failure: next read reopens */
    return rc;
}

int arc_read(archive_t *a, int i, uint8_t **out, size_t *len)
{
    locale_t old;
    int rc;

    if (!a)
        return ARC_E_INDEX;
    old = loc_enter(a);
    rc = read_entry(a, i, out, len);
    loc_leave(a, old);
    return rc;
}
