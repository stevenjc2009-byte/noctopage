/* zipfast.c - the ZIP fast path for comic/archive: central-directory listing (open no
 * longer decompresses the whole book) and direct reads from stored local-header
 * offsets (paging backwards no longer re-scans from entry 0). See archive_priv.h. */
#include "comic/archive.h"
#include "comic/archive_priv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define ZIP_LOCAL_SIG 0x04034b50u
#define ZIP_CHUNK (16u * 1024u) /* small: worker stacks are 256 KiB */

/* ---- fast ZIP listing -------------------------------------------------------------
 * Opening a comic used to walk the whole archive with libarchive, decompressing every
 * entry just to learn the names (10+ s on big books). Almost every comic is a ZIP
 * (cbz/zip), whose central directory lists every name + size up front, so read that
 * directly. Returns 1 when the listing was filled, 0 when libarchive should do it
 * (not a zip, zip64, multi-disk, or anything unexpected). Page reads still go through
 * libarchive unchanged; a corrupt central directory simply falls back to the walk. */
#define ZIP_EOCD_SIG 0x06054b50u
#define ZIP_CENT_SIG 0x02014b50u

static uint16_t rd16le(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }

static uint32_t rd32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void drop_listing(archive_t *a)
{
    for (int i = 0; i < a->count; i++)
        free(a->names[i]);
    a->count = 0;
}

/* Direct-readable: stored or deflated, known sizes, no encryption, and no bare
 * data-descriptor entry (central sizes would be zero and wrong). */
static int zip_direct(uint16_t flags, uint16_t method, uint32_t us, uint32_t cs)
{
    if (flags & 1)
        return 0;
    if (method != 0 && method != 8)
        return 0;
    if (cs == 0xFFFFFFFFu || us == 0xFFFFFFFFu)
        return 0;
    if (method == 0)
        return us > 0 || !(flags & 8);
    return us > 0 && cs > 0;
}

int zip_list(archive_t *a)
{
    FILE *f = fopen(a->path, "rb");
    long size, eo = -1, off;
    size_t tail_len;
    uint8_t *tail = NULL, *cd = NULL;
    const uint8_t *e;
    uint16_t total;
    uint32_t cd_size, cd_off;
    size_t pos;
    int i, ok = 0;

    if (!f)
        return 0;
    if (fseek(f, 0, SEEK_END) != 0)
        goto out;
    size = ftell(f);
    if (size < 22)
        goto out;
    /* The end-of-central-directory record ends the file, preceded by at most a 64 KiB comment. */
    tail_len = (size_t)size < 65535u + 22u ? (size_t)size : 65535u + 22u;
    tail = malloc(tail_len);
    if (!tail)
        goto out;
    if (fseek(f, size - (long)tail_len, SEEK_SET) != 0 || fread(tail, 1, tail_len, f) != tail_len)
        goto out;
    for (off = (long)tail_len - 22; off >= 0; off--) {
        if (rd32le(tail + (size_t)off) == ZIP_EOCD_SIG &&
            (size_t)off + 22u + rd16le(tail + (size_t)off + 20) == tail_len) {
            eo = off;
            break;
        }
    }
    if (eo < 0)
        goto out;
    e = tail + (size_t)eo;
    if (rd16le(e + 4) != 0 || rd16le(e + 6) != 0)
        goto out; /* multi-disk: let libarchive handle it */
    if (rd16le(e + 8) != rd16le(e + 10))
        goto out;
    total = rd16le(e + 10);
    if (total == 0xFFFFu)
        goto out; /* zip64 */
    if (total == 0) {
        ok = 1; /* empty zip: nothing to list */
        goto out;
    }
    cd_size = rd32le(e + 12);
    cd_off = rd32le(e + 16);
    if (cd_off > (uint32_t)size || cd_size > (uint32_t)size - cd_off)
        goto out;
    if (cd_size < (uint32_t)total * 46u)
        goto out;
    cd = malloc(cd_size);
    if (!cd)
        goto out;
    if (fseek(f, (long)cd_off, SEEK_SET) != 0 || fread(cd, 1, cd_size, f) != cd_size)
        goto out;
    /* Validate the whole directory before pushing anything, so a failure leaves the
     * listing untouched for the libarchive fallback. */
    pos = 0;
    for (i = 0; i < total; i++) {
        uint16_t nl, el, cl;
        if (pos + 46 > cd_size || rd32le(cd + pos) != ZIP_CENT_SIG)
            goto out;
        nl = rd16le(cd + pos + 28);
        el = rd16le(cd + pos + 30);
        cl = rd16le(cd + pos + 32);
        if (nl == 0 || pos + 46 + (size_t)nl + el + cl > cd_size)
            goto out;
        if (memchr(cd + pos + 46, 0, nl))
            goto out;
        pos += 46 + (size_t)nl + el + cl;
    }
    pos = 0;
    for (i = 0; i < total; i++) {
        uint16_t fl = rd16le(cd + pos + 8);
        uint16_t me = rd16le(cd + pos + 10);
        uint32_t cs = rd32le(cd + pos + 20);
        uint16_t nl = rd16le(cd + pos + 28);
        uint16_t el = rd16le(cd + pos + 30);
        uint16_t cl = rd16le(cd + pos + 32);
        uint32_t us = rd32le(cd + pos + 24);
        uint32_t lo = rd32le(cd + pos + 42);
        char *nm = malloc((size_t)nl + 1);
        if (!nm)
            goto out;
        memcpy(nm, cd + pos + 46, nl);
        nm[nl] = '\0';
        if (nm[nl - 1] != '/') { /* skip directory entries, like the libarchive walk */
            uint64_t sz = us == 0xFFFFFFFFu ? 0 : us; /* zip64 extra: size unknown, read grows */
            if (list_push(a, nm, sz) != 0) {
                free(nm);
                goto out;
            }
            int k = a->count - 1;
            a->loff[k] = lo;
            a->meth[k] = me;
            a->csz[k] = cs == 0xFFFFFFFFu ? 0 : cs;
            a->direct[k] = (char)zip_direct(fl, me, us, cs);
        }
        free(nm);
        pos += 46 + (size_t)nl + el + cl;
    }
    ok = 1;
out:
    if (!ok)
        drop_listing(a);
    free(cd);
    free(tail);
    if (f)
        fclose(f);
    return ok;
}

/* Read a central-directory entry straight from its local header: no re-scan, so paging
 * backwards is as cheap as paging forwards. Falls back (ARC_E_READ) on anything the
 * walker tolerates better; out and len are untouched then. */
int zipfast_read(archive_t *a, int i, uint8_t **out, size_t *len)
{
    uint8_t lh[30];
    FILE *f = fopen(a->path, "rb");
    uint64_t data, usize = a->sizes[i], left;
    uint16_t nl, el;

    if (!f)
        return ARC_E_READ;
    if (fseek(f, (long)a->loff[i], SEEK_SET) != 0 || fread(lh, 1, sizeof lh, f) != sizeof lh ||
        rd32le(lh) != ZIP_LOCAL_SIG) {
        fclose(f);
        return ARC_E_READ;
    }
    nl = rd16le(lh + 26);
    el = rd16le(lh + 28);
    data = a->loff[i] + 30 + nl + el;
    if (data > (uint64_t)0x7fffffffu || fseek(f, (long)data, SEEK_SET) != 0) {
        fclose(f);
        return ARC_E_READ;
    }
    if (a->meth[i] == 0) {
        size_t n = (size_t)usize; /* read_entry refused anything above ARC_MAX_ENTRY */
        uint8_t *buf = malloc(n + 1);

        if (!buf) {
            fclose(f);
            return ARC_E_NOMEM;
        }
        if (n && fread(buf, 1, n, f) != n) {
            fclose(f);
            free(buf);
            return ARC_E_READ;
        }
        buf[n] = '\0';
        *out = buf;
        *len = n;
        fclose(f);
        return ARC_OK;
    }
    left = a->csz[i];
    {
        size_t n = (size_t)usize;
        uint8_t *buf = malloc(n + 1), in[ZIP_CHUNK];
        z_stream zs;
        int failed = 1;

        memset(&zs, 0, sizeof zs);
        if (!buf || inflateInit2(&zs, -15) != Z_OK) {
            int nomem = buf == NULL;
            free(buf);
            fclose(f);
            return nomem ? ARC_E_NOMEM : ARC_E_READ;
        }
        for (;;) {
            int zr;

            if ((size_t)zs.total_out == n) { failed = 0; break; } /* all output made */
            if (zs.avail_in == 0 && left > 0) {
                size_t want = left < ZIP_CHUNK ? (size_t)left : ZIP_CHUNK;
                size_t got = fread(in, 1, want, f);

                if (got == 0) break; /* truncated: the walker gets a turn below */
                zs.next_in = in;
                zs.avail_in = (uInt)got;
                left -= got;
            }
            zs.next_out = buf + zs.total_out;
            zs.avail_out = (uInt)(n - (size_t)zs.total_out);
            zr = inflate(&zs, Z_NO_FLUSH);
            if (zr == Z_STREAM_END) { failed = (size_t)zs.total_out != n; break; }
            if (zr != Z_OK && zr != Z_BUF_ERROR) break;
            if (left == 0 && zs.avail_in == 0) break; /* stuck: no input, no output */
        }
        inflateEnd(&zs);
        if (failed) {
            fclose(f);
            free(buf);
            return ARC_E_READ;
        }
        buf[n] = '\0';
        *out = buf;
        *len = n;
        fclose(f);
        return ARC_OK;
    }
}
