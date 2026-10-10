/* covers.h - cover thumbnails for the grids. A worker thread loads
 * ux0:data/noctopage/thumbs/<id>.bin, or renders it (lib/coverimg) and saves it; the main
 * thread uploads finished pixels into ui/texcache in covers_pump().
 * Requests are served newest first, so the items on screen win after a fast scroll. */
#ifndef NOCT_COVERS_H
#define NOCT_COVERS_H

#include <stdint.h>
#include <vita2d.h>

void covers_init(void); /* starts the worker; mdoc_global_init must have run */
void covers_shutdown(void);
/* Invalidate in-memory covers, cancelling old writes. Main thread; no file deletions. */
void covers_reset(void);
/* Pause background covers while a reader owns CPU/GPU memory. Main thread. */
void covers_pause(int paused);
/* Promote the selected shelf tile ahead of other queued covers. */
void covers_prioritize(uint64_t id);

/* The cover texture for a book, or NULL while it is pending / failed. A miss queues a
 * request (once; a failed book is not retried until covers_forget). MAIN THREAD ONLY. */
vita2d_texture *covers_get(uint64_t id, const char *path, int db_kind);

/* Upload up to max finished covers into the texture cache. Call once per frame.
 * Returns how many were uploaded. MAIN THREAD ONLY. */
int covers_pump(int max);

/* Drop a book's texture, thumbnail file and failed mark (e.g. after it changed). */
void covers_forget(uint64_t id);

uint64_t covers_series_id(const char *series);
vita2d_texture *covers_get_series(const char *series);
#endif
