/* page_cache — byte-capped, thread-safe LRU cache of decoded pages (pure C, pthreads).
 *
 * Key: (uint64 book_id, int page, int variant). Values are opaque pointers; each carries a
 * byte cost and a free callback supplied at insert time.
 *
 * Typical use: a worker thread pc_put()s decoded CPU RGBA buffers, the main thread pc_get()s
 * (pins) the page it is drawing and pc_release()s it when done.
 *
 * Rules:
 *  - pc_get pins the entry (refcount). A pinned entry is never evicted.
 *  - Eviction frees least-recently-used UNPINNED entries until bytes <= cap. If everything
 *    else is pinned the cache may exceed its cap temporarily; the excess is evicted as soon as
 *    pins are released (pc_release re-checks the cap).
 *  - The free callback runs WITHOUT the cache lock held, on whichever thread caused the
 *    eviction (pc_put, pc_release, pc_remove_book, pc_clear or pc_free). The caller must make
 *    the callback safe to run on any thread: the reader stores CPU buffers only (free()),
 *    never vita2d textures — those are created and freed by the main thread elsewhere.
 *  - pc_remove_book / pc_clear on a pinned entry unlinks it (pc_get misses at once) but its
 *    memory is freed on the last pc_release; until then it still counts in pc_bytes.
 *  - pc_put on an existing key fails (PC_ERR_EXISTS); remove the old entry first.
 *    On any pc_put failure ownership of the value stays with the caller (callback not run).
 */
#ifndef NOCTO_PAGE_CACHE_H
#define NOCTO_PAGE_CACHE_H

#include <stddef.h>
#include <stdint.h>

#define PC_OK            0
#define PC_ERR_EXISTS   (-1)  /* key already cached */
#define PC_ERR_TOO_BIG  (-2)  /* cost alone exceeds the cap */
#define PC_ERR_NOMEM    (-3)
#define PC_ERR_ARG      (-4)  /* NULL cache or NULL value */

typedef struct page_cache page_cache_t;
typedef struct pc_entry pc_ref_t; /* pinned handle returned by pc_get */
typedef void (*pc_free_fn)(void *value, void *user);

/* New cache holding at most cap_bytes (soft, see pinning). NULL on out of memory. */
page_cache_t *pc_new(size_t cap_bytes);

/* Free every entry (calling the callbacks, pinned or not) and the cache. The caller must
 * guarantee no other thread is using it. */
void pc_free(page_cache_t *pc);

/* Insert value. free_fn may be NULL (nothing to free). Returns PC_OK or a PC_ERR_*. */
int pc_put(page_cache_t *pc, uint64_t book_id, int page, int variant, void *value,
           size_t cost, pc_free_fn free_fn, void *user);

/* Look up and pin; marks the entry most recently used. NULL if absent. Every non-NULL
 * result must be passed to pc_release exactly once. */
pc_ref_t *pc_get(page_cache_t *pc, uint64_t book_id, int page, int variant);

/* The cached value; valid until the matching pc_release. */
void *pc_ref_value(const pc_ref_t *ref);

/* Unpin. May evict (and so run free callbacks) if the cache is over its cap. */
void pc_release(page_cache_t *pc, pc_ref_t *ref);

/* 1 if cached. Does not pin and does not touch the LRU order. */
int pc_contains(page_cache_t *pc, uint64_t book_id, int page, int variant);

/* Drop every entry of one book; returns how many were dropped. */
int pc_remove_book(page_cache_t *pc, uint64_t book_id);

/* Drop everything. */
void pc_clear(page_cache_t *pc);

/* Total bytes held (including unlinked entries still pinned) and live entry count. */
size_t pc_bytes(page_cache_t *pc);
int pc_count(page_cache_t *pc);

#endif
