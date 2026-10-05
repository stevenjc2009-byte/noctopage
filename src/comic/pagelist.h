/* pagelist — reading order of a comic archive or image folder (pure C, no allocation). */
#ifndef NOCTO_PAGELIST_H
#define NOCTO_PAGELIST_H

/* names: n entry names (UTF-8, '/' separated; NULL entries are skipped).
 * out_idx: room for n ints. Fills it with the indices of the reading pages, in natural order of
 * the full path (natcmp: case-insensitive, digit runs by value; exact duplicates keep their
 * input order), and returns how many there are.
 * An entry is a page when formats_is_image() accepts it and no path component is
 * "__MACOSX" (case-insensitive), starts with '.' (hidden, AppleDouble "._x", "..").
 * A "." component or an empty one ("a//b", leading '/') is ignored, so tar-style "./p1.png"
 * is kept. Non-images (Thumbs.db, readme.txt, directory entries "pages/") are dropped. */
int pagelist_build(const char *const *names, int n, int *out_idx);

#endif
