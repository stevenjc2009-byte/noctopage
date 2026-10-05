/* decode — compressed image bytes to tight RGBA8888, for worker threads. Uses libturbojpeg
 * (JPEG, scaled decode), libpng (PNG), libwebp (WebP, scaled decode) and MuPDF
 * fz_new_image_from_buffer (GIF, BMP, TIFF, anything else; first frame only, CMYK JPEG).
 * No psp2 / vita2d code: builds and tests on the host.
 *
 * Format is detected by magic bytes, never by file name. No globals: calls are
 * thread-safe, each MuPDF fallback call builds its own fz_context (16 MiB store). Decoded
 * RGBA larger than IMG_MAX_RGBA_BYTES (at the size actually decoded) is refused. */
#ifndef NOCTO_DECODE_H
#define NOCTO_DECODE_H

#include <stddef.h>
#include <stdint.h>

#define IMG_MAX_RGBA_BYTES (128u * 1024u * 1024u)

typedef struct {
    uint8_t *rgba; /* tight RGBA8888, w*h*4 bytes, malloc'd; NULL when empty */
    int w, h;      /* decoded size */
    int src_w, src_h; /* size of the image in the file */
} img_t;

/* Free out->rgba and zero *img. NULL is fine. */
void img_free(img_t *img);

/* Header-only size query. Returns 0 and fills *w and *h, or -1 if the data is not a
 * recognisable image. Never decodes pixels (MuPDF is used only for formats other than
 * JPEG/PNG/WebP/GIF/BMP, which are read straight from their headers). */
int img_probe(const uint8_t *data, size_t len, int *w, int *h);

/* Decode into *out. The result fits within max_w x max_h preserving aspect (0 = no limit
 * on that axis) and is never upscaled; out->src_w/src_h hold the original size. JPEG is
 * decoded at the smallest DCT scale still covering the target, then box-scaled to the
 * exact fit; PNG is decoded whole then box-scaled; WebP is scaled by the decoder.
 * Returns 0, or -1 with a message in err (when errcap > 0) and *out zeroed. */
int img_decode(const uint8_t *data, size_t len, int max_w, int max_h, img_t *out, char *err,
               size_t errcap);

#endif
