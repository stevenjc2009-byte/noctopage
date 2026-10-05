/* coverimg - book cover thumbnails (pure: no psp2/vita2d, builds and tests on the host).
 *
 * A thumbnail is COVER_W x COVER_H tight RGBA8888, cover-fill cropped. On disk it is
 * "NTH1", uint16 LE width, uint16 LE height, then the pixels (PLAN "Data on the device"). */
#ifndef NOCT_COVERIMG_H
#define NOCT_COVERIMG_H

#include <stddef.h>
#include <stdint.h>

#define COVER_W 180
#define COVER_H 270
#define COVER_BYTES ((size_t)COVER_W * COVER_H * 4)
#define COVER_FILE_BYTES (8 + COVER_BYTES)

/* Render the cover of a book into out (COVER_BYTES). db_kind is DB_KIND_*:
 *   archive / folder: the first page in natural reading order (comic/pagelist);
 *   reflow / fixed:   page 0 via mdoc_render_cover (mdoc_global_init must have run).
 * Returns 0, or -1 with a message in err (when errcap > 0). Thread-safe. */
int coverimg_make(const char *path, int db_kind, uint8_t *out, char *err, size_t errcap);

/* Write / read a thumbnail file. read returns 0 only for a complete file of the right size
 * and header; anything else (missing, short, wrong magic or size) is -1. */
int coverimg_save(const char *file, const uint8_t *rgba);
int coverimg_load(const char *file, uint8_t *rgba);

#endif
