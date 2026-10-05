#include "test.h"

#include "reader/page_cache.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>

/* ---- instrumented values ---------------------------------------------------------------- */

typedef struct {
	int id;
	unsigned char fill[56];
} item_t;

static int freed_log[256];
static int nfreed;
static page_cache_t *probe_cache; /* set to call back into the cache from the free callback */
static int probe_reentered;
static pthread_t freed_on;

static void free_item(void *v, void *user)
{
	item_t *it = v;
	(void)user;
	if (nfreed < 256)
		freed_log[nfreed] = it->id;
	nfreed++;
	freed_on = pthread_self();
	if (probe_cache) { /* would deadlock if the cache still held its lock */
		(void)pc_bytes(probe_cache);
		(void)pc_contains(probe_cache, 1, 0, 0);
		probe_reentered++;
	}
	free(it);
}

static item_t *mk(int id)
{
	item_t *it = calloc(1, sizeof *it);
	it->id = id;
	return it;
}

static int put(page_cache_t *pc, uint64_t book, int page, int variant, size_t cost)
{
	item_t *it = mk(page);
	int r = pc_put(pc, book, page, variant, it, cost, free_item, NULL);
	if (r != PC_OK)
		free(it); /* ownership stayed with us */
	return r;
}

static void reset_log(void)
{
	nfreed = 0;
	probe_cache = NULL;
	probe_reentered = 0;
}

static int get_id(page_cache_t *pc, uint64_t book, int page, int variant)
{
	pc_ref_t *r = pc_get(pc, book, page, variant);
	int id = -1;
	if (r) {
		id = ((item_t *)pc_ref_value(r))->id;
		pc_release(pc, r);
	}
	return id;
}

/* ---- single-threaded behaviour ---------------------------------------------------------- */

static void test_basics(void)
{
	page_cache_t *pc = pc_new(1000);
	pc_ref_t *r;

	reset_log();
	CHECK(pc != NULL);
	CHECK_INT(pc_bytes(pc), 0);
	CHECK_INT(pc_count(pc), 0);
	CHECK(!pc_contains(pc, 7, 3, 0));
	CHECK(pc_get(pc, 7, 3, 0) == NULL);

	CHECK_INT(put(pc, 7, 3, 0, 100), PC_OK);
	CHECK(pc_contains(pc, 7, 3, 0));
	CHECK_INT(pc_bytes(pc), 100);
	CHECK_INT(pc_count(pc), 1);
	/* the key is the whole triple */
	CHECK(!pc_contains(pc, 8, 3, 0));
	CHECK(!pc_contains(pc, 7, 4, 0));
	CHECK(!pc_contains(pc, 7, 3, 1));
	CHECK_INT(put(pc, 8, 3, 0, 10), PC_OK);
	CHECK_INT(put(pc, 7, 4, 0, 10), PC_OK);
	CHECK_INT(put(pc, 7, 3, 1, 10), PC_OK);
	CHECK_INT(pc_count(pc), 4);
	CHECK_INT(pc_bytes(pc), 130);

	r = pc_get(pc, 7, 3, 0);
	CHECK(r != NULL);
	CHECK_INT(((item_t *)pc_ref_value(r))->id, 3);
	pc_release(pc, r);
	CHECK(pc_ref_value(NULL) == NULL);

	/* a duplicate key is refused and ownership stays with the caller */
	CHECK_INT(put(pc, 7, 3, 0, 5), PC_ERR_EXISTS);
	CHECK_INT(pc_bytes(pc), 130);
	CHECK_INT(nfreed, 0);
	/* bad arguments */
	CHECK_INT(pc_put(pc, 1, 1, 1, NULL, 5, free_item, NULL), PC_ERR_ARG);
	CHECK_INT(pc_put(NULL, 1, 1, 1, pc, 5, NULL, NULL), PC_ERR_ARG);
	CHECK(pc_get(NULL, 1, 1, 1) == NULL);
	pc_release(pc, NULL);
	pc_release(NULL, NULL);
	/* a value costing more than the cap can never fit */
	CHECK_INT(put(pc, 9, 9, 9, 1001), PC_ERR_TOO_BIG);
	CHECK_INT(nfreed, 0);
	CHECK_INT(put(pc, 9, 9, 9, 1000), PC_OK); /* exactly the cap fits, evicting the rest */
	CHECK_INT(pc_bytes(pc), 1000);
	CHECK_INT(pc_count(pc), 1);
	CHECK_INT(nfreed, 4);

	pc_free(pc);
	CHECK_INT(nfreed, 5);
	/* a NULL free callback is allowed */
	pc = pc_new(10);
	{
		static int dummy;
		CHECK_INT(pc_put(pc, 1, 1, 1, &dummy, 4, NULL, NULL), PC_OK);
		pc_clear(pc);
		CHECK_INT(pc_count(pc), 0);
	}
	pc_free(pc);
	pc_free(NULL);
}

static void test_lru_order(void)
{
	page_cache_t *pc = pc_new(100);
	pc_ref_t *r;

	reset_log();
	put(pc, 1, 0, 0, 30);
	put(pc, 1, 1, 0, 30);
	put(pc, 1, 2, 0, 30); /* MRU -> LRU: 2 1 0 */
	CHECK_INT(nfreed, 0);
	r = pc_get(pc, 1, 0, 0); /* touching page 0 makes it the newest: 0 2 1 */
	pc_release(pc, r);
	CHECK(pc_contains(pc, 1, 1, 0)); /* pc_contains must not reorder */
	put(pc, 1, 3, 0, 30);            /* 3 0 2 1 -> over cap, 1 is the LRU */
	CHECK_INT(nfreed, 1);
	CHECK_INT(freed_log[0], 1);
	CHECK(!pc_contains(pc, 1, 1, 0));
	CHECK(pc_contains(pc, 1, 0, 0) && pc_contains(pc, 1, 2, 0) && pc_contains(pc, 1, 3, 0));
	CHECK_INT(pc_bytes(pc), 90);
	put(pc, 1, 4, 0, 30); /* 4 3 0 2 -> evicts 2 */
	CHECK_INT(nfreed, 2);
	CHECK_INT(freed_log[1], 2);
	/* a big insert evicts several, oldest first: 4 3 0 -> need room for 70: frees 0, 3 */
	put(pc, 1, 5, 0, 70);
	CHECK_INT(nfreed, 4);
	CHECK_INT(freed_log[2], 0);
	CHECK_INT(freed_log[3], 3);
	CHECK(pc_contains(pc, 1, 4, 0) && pc_contains(pc, 1, 5, 0));
	CHECK_INT(pc_bytes(pc), 100);
	CHECK_INT(get_id(pc, 1, 4, 0), 4);
	CHECK_INT(get_id(pc, 1, 9, 0), -1);
	/* the entry just inserted is never evicted by its own insert */
	put(pc, 1, 6, 0, 100);
	CHECK(pc_contains(pc, 1, 6, 0));
	CHECK_INT(pc_count(pc), 1);
	pc_free(pc);
}

static void test_pinning(void)
{
	page_cache_t *pc = pc_new(100);
	pc_ref_t *a, *b;

	reset_log();
	put(pc, 1, 0, 0, 30);
	put(pc, 1, 1, 0, 30);
	put(pc, 1, 2, 0, 30);
	a = pc_get(pc, 1, 0, 0);
	(void)get_id(pc, 1, 1, 0);
	(void)get_id(pc, 1, 2, 0); /* MRU -> LRU: 2 1 0, and 0 (the LRU) is pinned */
	put(pc, 1, 3, 0, 30);
	CHECK_INT(nfreed, 1); /* page 1 went instead of the pinned page 0 */
	CHECK_INT(freed_log[0], 1);
	CHECK(pc_contains(pc, 1, 0, 0));
	/* two pins on the same entry need two releases */
	b = pc_get(pc, 1, 0, 0);
	pc_release(pc, a);
	put(pc, 1, 4, 0, 30);
	put(pc, 1, 5, 0, 30);
	CHECK(pc_contains(pc, 1, 0, 0)); /* still pinned once */
	pc_release(pc, b);
	reset_log();
	pc_clear(pc);
	CHECK_INT(nfreed, 3); /* 0, 4, 5 */

	/* everything else pinned: the cap may be exceeded; the excess goes at the release */
	put(pc, 2, 0, 0, 40);
	put(pc, 2, 1, 0, 40);
	a = pc_get(pc, 2, 0, 0);
	b = pc_get(pc, 2, 1, 0);
	put(pc, 2, 2, 0, 40); /* 120 > 100: the new entry is kept, the others are pinned */
	CHECK_INT(nfreed, 3);
	CHECK_INT(pc_bytes(pc), 120);
	CHECK_INT(pc_count(pc), 3);
	pc_release(pc, a); /* over the cap: the now-unpinned LRU entry goes at once */
	CHECK(!pc_contains(pc, 2, 0, 0));
	CHECK(pc_contains(pc, 2, 1, 0) && pc_contains(pc, 2, 2, 0));
	CHECK_INT(pc_bytes(pc), 80);
	CHECK_INT(nfreed, 4);
	pc_release(pc, b);
	CHECK_INT(pc_bytes(pc), 80); /* under the cap: nothing more to evict */
	CHECK_INT(nfreed, 4);
	pc_free(pc);
}

static void test_remove_and_clear(void)
{
	page_cache_t *pc = pc_new(10000);
	pc_ref_t *r;
	int i;

	reset_log();
	for (i = 0; i < 5; i++) {
		put(pc, 100, i, 0, 10);
		put(pc, 200, i, 1, 10);
	}
	CHECK_INT(pc_remove_book(pc, 100), 5);
	CHECK_INT(nfreed, 5);
	CHECK_INT(pc_count(pc), 5);
	CHECK_INT(pc_bytes(pc), 50);
	CHECK(!pc_contains(pc, 100, 2, 0) && pc_contains(pc, 200, 2, 1));
	CHECK_INT(pc_remove_book(pc, 100), 0);
	CHECK_INT(pc_remove_book(NULL, 100), 0);

	/* removing a pinned entry hides it at once but frees it only on release */
	r = pc_get(pc, 200, 3, 1);
	CHECK_INT(pc_remove_book(pc, 200), 5);
	CHECK_INT(nfreed, 9);
	CHECK(pc_get(pc, 200, 3, 1) == NULL);
	CHECK(!pc_contains(pc, 200, 3, 1));
	CHECK_INT(pc_count(pc), 0);
	CHECK_INT(pc_bytes(pc), 10); /* still held by the pin */
	CHECK_INT(((item_t *)pc_ref_value(r))->id, 3); /* and still readable */
	/* the same key can be re-inserted while the old one is pinned */
	CHECK_INT(put(pc, 200, 3, 1, 10), PC_OK);
	pc_release(pc, r);
	CHECK_INT(nfreed, 10);
	CHECK_INT(pc_bytes(pc), 10);
	CHECK(pc_contains(pc, 200, 3, 1));

	/* pc_clear */
	put(pc, 1, 1, 1, 10);
	r = pc_get(pc, 1, 1, 1);
	pc_clear(pc);
	CHECK_INT(pc_count(pc), 0);
	CHECK_INT(pc_bytes(pc), 10);
	pc_release(pc, r);
	CHECK_INT(pc_bytes(pc), 0);
	CHECK_INT(nfreed, 12);

	/* pc_free releases pinned entries too */
	put(pc, 1, 1, 1, 10);
	put(pc, 1, 2, 1, 10);
	r = pc_get(pc, 1, 1, 1);
	(void)pc_remove_book(pc, 1); /* page 1 now unlinked-but-pinned, page 2 freed */
	put(pc, 3, 3, 3, 10);
	(void)r;
	pc_free(pc);
	CHECK_INT(nfreed, 12 + 2 + 1);
}

static void test_callback_context(void)
{
	page_cache_t *pc = pc_new(60);

	reset_log();
	probe_cache = pc;
	put(pc, 1, 0, 0, 30);
	put(pc, 1, 1, 0, 30);
	put(pc, 1, 2, 0, 30); /* evicts one: the callback re-enters the cache without deadlock */
	CHECK_INT(nfreed, 1);
	CHECK_INT(probe_reentered, 1);
	pc_clear(pc);
	CHECK_INT(probe_reentered, 3);
	put(pc, 1, 0, 0, 30);
	pc_remove_book(pc, 1);
	CHECK_INT(probe_reentered, 4);
	probe_cache = NULL;
	pc_free(pc);
}

static void *evict_from_thread(void *arg)
{
	page_cache_t *pc = arg;
	put(pc, 1, 2, 0, 30); /* this thread's insert evicts page 0 */
	return NULL;
}

static void test_callback_thread(void)
{
	page_cache_t *pc = pc_new(60);
	pthread_t t;

	reset_log();
	put(pc, 1, 0, 0, 30);
	put(pc, 1, 1, 0, 30);
	pthread_create(&t, NULL, evict_from_thread, pc);
	pthread_join(t, NULL);
	CHECK_INT(nfreed, 1);
	CHECK(pthread_equal(freed_on, t)); /* freed on the thread that caused the eviction */
	CHECK(!pthread_equal(freed_on, pthread_self()));
	pc_free(pc);
}

int main(void)
{
	test_basics();
	test_lru_order();
	test_pinning();
	test_remove_and_clear();
	test_callback_context();
	test_callback_thread();
	return TEST_DONE("page_cache");
}
