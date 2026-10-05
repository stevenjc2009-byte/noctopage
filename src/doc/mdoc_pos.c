/* mdoc reading positions. MuPDF's own bookmarks (fz_make_bookmark) cannot be used for
 * reflowable documents: for EPUB/HTML/FB2/TXT they are raw pointers to layout nodes
 * (fz_html_flow *), meaningless after a reopen or an app restart. So a reflowable position is
 * "r:<chapter>:<chars>:<skip>": the number of visible characters in that chapter before the
 * page's first character (whitespace and hyphens are not counted, so the number does not
 * depend on line breaking, fonts or ligatures), plus how many character-less pages (e.g. a
 * full-page image) sit at that same offset before the page. Fixed layout: "p:<page>". */
#include "doc/mdoc_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int visible(int c)
{
    if (c <= 32 || c == 0x7F || c == 0xA0 || c == 0xAD || c == '-' || c == 0x2010 ||
        c == 0x2011 || c == 0x202F || c == 0x3000 || c == 0xFEFF)
        return 0;
    return !(c >= 0x2000 && c <= 0x200B);
}

typedef struct {
    char *out;
    size_t cap, len;
    int count;
} text_acc_t;

static void put(text_acc_t *a, int c)
{
    char buf[8];
    int n = fz_runetochar(buf, c);

    if (!a->out || a->len + (size_t)n >= a->cap)
        return;
    memcpy(a->out + a->len, buf, (size_t)n);
    a->len += (size_t)n;
    a->out[a->len] = '\0';
}

static void walk_blocks(fz_stext_block *b, text_acc_t *a)
{
    for (; b; b = b->next) {
        if (b->type == FZ_STEXT_BLOCK_TEXT) {
            fz_stext_line *ln;
            for (ln = b->u.t.first_line; ln; ln = ln->next) {
                fz_stext_char *ch;
                for (ch = ln->first_char; ch; ch = ch->next) {
                    if (visible(ch->c))
                        a->count++;
                    put(a, ch->c);
                }
                put(a, '\n');
            }
        } else if (b->type == FZ_STEXT_BLOCK_STRUCT && b->u.s.down) {
            walk_blocks(b->u.s.down->first_block, a);
        }
    }
}

int mdoc__page_text(mdoc_t *d, int page, char *out, size_t cap)
{
    fz_page *pg = NULL;
    fz_stext_page *st = NULL;
    text_acc_t a;
    int rc = -1;

    a.out = out;
    a.cap = cap;
    a.len = 0;
    a.count = 0;
    if (out && cap > 0)
        out[0] = '\0';
    if (!d || !d->doc)
        return -1;
    fz_var(pg);
    fz_var(st);
    fz_var(rc);
    mdoc__lock(d);
    fz_try(d->ctx)
    {
        pg = fz_load_page(d->ctx, d->doc, page);
        st = fz_new_stext_page_from_page(d->ctx, pg, NULL);
        walk_blocks(st->first_block, &a);
        rc = a.count;
    }
    fz_always(d->ctx)
    {
        fz_drop_stext_page(d->ctx, st);
        fz_drop_page(d->ctx, pg);
        mdoc__unlock(d);
    }
    fz_catch(d->ctx)
        rc = -1;
    return rc;
}

/* Visible characters on a page, cached until the next layout change. */
static int page_chars(mdoc_t *d, int page)
{
    int n = mdoc__count(d), i;

    if (n < 0 || page < 0 || page >= n)
        return -1;
    if (!d->chars) {
        d->chars = malloc(sizeof(int) * (size_t)(n > 0 ? n : 1));
        if (!d->chars)
            return -1;
        for (i = 0; i < n; i++)
            d->chars[i] = -1;
    }
    if (d->chars[page] < 0)
        d->chars[page] = mdoc__page_text(d, page, NULL, 0);
    return d->chars[page];
}

/* Chapter of `page` and its first page. */
static int chapter_of(mdoc_t *d, int page, int *chapter, int *first)
{
    int rc = 0;

    fz_var(rc);
    mdoc__lock(d);
    fz_try(d->ctx)
    {
        fz_location loc = fz_location_from_page_number(d->ctx, d->doc, page);
        *chapter = loc.chapter;
        *first = page - loc.page;
    }
    fz_always(d->ctx)
        mdoc__unlock(d);
    fz_catch(d->ctx)
        rc = -1;
    return rc;
}

int mdoc_position(mdoc_t *d, int page, char out[MDOC_POS_MAX])
{
    int n, chapter, first, q, off = 0, skip = 0;

    if (!out)
        return -1;
    out[0] = '\0';
    n = mdoc__count(d);
    if (n < 0 || page < 0 || page >= n)
        return -1;
    if (!d->reflow) {
        snprintf(out, MDOC_POS_MAX, "p:%d", page);
        return 0;
    }
    if (chapter_of(d, page, &chapter, &first) != 0 || chapter < 0 || first < 0)
        return -1;
    for (q = first; q < page; q++) {
        int c = page_chars(d, q);
        if (c < 0)
            return -1;
        off += c;
        skip = c == 0 ? skip + 1 : 0;
    }
    snprintf(out, MDOC_POS_MAX, "r:%d:%d:%d", chapter, off, skip);
    return 0;
}

/* First page and page count of `chapter` (clamped to the last chapter). */
static int chapter_pages(mdoc_t *d, int chapter, int *first, int *cnt)
{
    int rc = 0;

    fz_var(rc);
    fz_var(chapter);
    mdoc__lock(d);
    fz_try(d->ctx)
    {
        int nch = fz_count_chapters(d->ctx, d->doc);
        if (chapter >= nch)
            chapter = nch - 1;
        if (chapter < 0)
            fz_throw(d->ctx, FZ_ERROR_FORMAT, "no chapters");
        *cnt = fz_count_chapter_pages(d->ctx, d->doc, chapter);
        *first = fz_page_number_from_location(d->ctx, d->doc, fz_make_location(chapter, 0));
    }
    fz_always(d->ctx)
        mdoc__unlock(d);
    fz_catch(d->ctx)
        rc = -1;
    return rc;
}

/* Page in `chapter` whose text contains character `off` (see the file comment). */
static int find_in_chapter(mdoc_t *d, int chapter, int off, int skip)
{
    int first = 0, cnt = 0, q, start = 0, k = 0;

    if (chapter_pages(d, chapter, &first, &cnt) != 0 || cnt < 1 || first < 0)
        return -1;
    for (q = first; q < first + cnt; q++) {
        int c = page_chars(d, q);
        if (c < 0)
            return -1;
        if (start == off) {
            if (k == skip || c > 0)
                return q;
            k++;
        } else if (start < off && off < start + c) {
            return q;
        }
        start += c;
    }
    return first + cnt - 1;
}

int mdoc_page_for_position(mdoc_t *d, const char *pos)
{
    int n = mdoc__count(d), a = 0, b = 0, c = 0, used = 0;

    if (n < 1 || !pos)
        return -1;
    if (sscanf(pos, "p:%d%n", &a, &used) == 1 && pos[used] == '\0') {
        if (d->reflow)
            return -1;
        return a < 0 ? 0 : a >= n ? n - 1 : a;
    }
    if (sscanf(pos, "r:%d:%d:%d%n", &a, &b, &c, &used) == 3 && pos[used] == '\0') {
        if (!d->reflow || a < 0 || b < 0 || c < 0)
            return -1;
        return find_in_chapter(d, a, b, c);
    }
    return -1;
}
