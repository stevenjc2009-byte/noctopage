/* scale — area-average (box) resampling of tight RGBA8888 buffers. Pure C, thread-safe
 * (no globals; every call allocates and frees its own scratch).
 *
 * One algorithm covers both directions on each axis independently:
 *   - downscale: every destination pixel is the exact area-weighted mean of the source
 *     pixels it covers (box filter), in 16.16 fixed point, no per-pixel division;
 *   - upscale:   the same overlap weights degenerate to nearest-neighbour, blended
 *     only where one destination pixel straddles two source pixels (so an integer
 *     upscale is exact nearest-neighbour; there is no bilinear smoothing).
 * Weights per destination pixel sum to exactly 65536, so a solid colour stays exact.
 * Channels are averaged independently (alpha is not premultiplied). */
#ifndef NOCTO_SCALE_H
#define NOCTO_SCALE_H

#include <stdint.h>

/* Scale src (sw x sh, tight RGBA8888) to exactly dw x dh. dst must hold dw*dh*4 bytes and
 * must not overlap src. Returns 0 on success, -1 for non-positive sizes / NULL pointers,
 * -2 if the scratch allocation failed. Equal sizes are a plain copy. */
int img_scale(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh);

/* Scale to fill dw x dh and centre-crop the overflow: the source window with the
 * destination's aspect ratio is centred on the image, then scaled with img_scale's
 * filter. For 180x270 thumbnails. Same return codes as img_scale. */
int img_cover_crop(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh);

#endif
