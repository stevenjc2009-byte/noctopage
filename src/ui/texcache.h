/* texcache.h - main-thread cache of GPU textures keyed by a 64-bit id (covers,
 * thumbnails). Least recently used textures are freed past the capacity.
 * MAIN THREAD ONLY: vita2d textures are created and freed here and nowhere else. */
#ifndef NOCT_TEXCACHE_H
#define NOCT_TEXCACHE_H

#include <stdint.h>
#include <vita2d.h>

#define TEXCACHE_CAP 64 /* 64 x 180x270 RGBA = 12.4 MB of GPU memory */

/* The texture for key, or NULL. Marks it recently used. */
vita2d_texture *texcache_get(uint64_t key);

/* Uploads tight RGBA8888 pixels as the texture for key (replacing any old one).
 * Returns the texture, or NULL if GPU memory ran out. */
vita2d_texture *texcache_put_rgba(uint64_t key, const uint8_t *rgba, int w, int h);

void texcache_drop(uint64_t key);
void texcache_clear(void);

#endif
