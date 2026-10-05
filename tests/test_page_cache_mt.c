#include "test.h"

#include "reader/page_cache.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>

/* ---- 4-thread hammer -------------------------------------------------------------------- */

#define HAMMER_THREADS 4
#define HAMMER_ITERS 60000
#define HAMMER_CAP (24 * 64)

typedef struct {
	uint64_t book;
	int page;
	unsigned char fill[48];
} hval_t;

static atomic_int h_alloc, h_free, h_bad;

static void h_free_cb(void *v, void *user)
{
	hval_t *h = v;
	(void)user;
	if (h->fill[0] != (unsigned char)(h->book * 31 + h->page) ||
	    h->fill[47] != (unsigned char)(h->book * 31 + h->page))
		atomic_fetch_add(&h_bad, 1);
	free(h);
	atomic_fetch_add(&h_free, 1);
}

static hval_t *h_make(uint64_t book, int page)
{
	hval_t *h = malloc(sizeof *h);
	h->book = book;
	h->page = page;
	memset(h->fill, (unsigned char)(book * 31 + page), sizeof h->fill);
	atomic_fetch_add(&h_alloc, 1);
	return h;
}

static void *hammer(void *arg)
{
	page_cache_t *pc = arg;
	unsigned seed = (unsigned)(uintptr_t)pthread_self() ^ 0x9E3779B9u;
	pc_ref_t *held[3] = { NULL, NULL, NULL };
	int i, nheld = 0;

	for (i = 0; i < HAMMER_ITERS; i++) {
		unsigned r;
		uint64_t book;
		int page, op;
		seed = seed * 1664525u + 1013904223u;
		r = seed >> 8;
		book = r % 3;
		page = (int)((r >> 4) % 20);
		op = (int)((r >> 12) % 100);
		if (op < 40) {
			hval_t *h = h_make(book, page);
			if (pc_put(pc, book, page, 0, h, 64, h_free_cb, NULL) != PC_OK)
				h_free_cb(h, NULL);
		} else if (op < 85) {
			pc_ref_t *ref = pc_get(pc, book, page, 0);
			if (ref) {
				hval_t *h = pc_ref_value(ref);
				if (h->book != book || h->page != page ||
				    h->fill[10] != (unsigned char)(book * 31 + page))
					atomic_fetch_add(&h_bad, 1);
				if (nheld < 3 && (r & 1))
					held[nheld++] = ref; /* keep it pinned across later operations */
				else
					pc_release(pc, ref);
			}
		} else if (op < 90) {
			(void)pc_contains(pc, book, page, 0);
		} else if (op < 92) {
			(void)pc_remove_book(pc, book);
		} else if (op < 93) {
			pc_clear(pc);
		} else {
			(void)pc_bytes(pc);
			(void)pc_count(pc);
		}
		if (nheld == 3 || (nheld > 0 && (r % 7) == 0)) {
			hval_t *h = pc_ref_value(held[--nheld]);
			if (h->fill[1] != (unsigned char)(h->book * 31 + h->page))
				atomic_fetch_add(&h_bad, 1);
			pc_release(pc, held[nheld]);
		}
	}
	while (nheld > 0)
		pc_release(pc, held[--nheld]);
	return NULL;
}

static void test_hammer(void)
{
	page_cache_t *pc = pc_new(HAMMER_CAP);
	pthread_t th[HAMMER_THREADS];
	int i;

	for (i = 0; i < HAMMER_THREADS; i++)
		pthread_create(&th[i], NULL, hammer, pc);
	for (i = 0; i < HAMMER_THREADS; i++)
		pthread_join(th[i], NULL);
	CHECK_INT(atomic_load(&h_bad), 0);
	/* all pins are gone: the cap holds again, and the books agree with the entries */
	CHECK((size_t)pc_bytes(pc) <= HAMMER_CAP);
	CHECK_INT(pc_bytes(pc), (size_t)pc_count(pc) * 64);
	pc_free(pc);
	/* every value ever made was freed exactly once (ASan catches a double free / leak) */
	CHECK_INT(atomic_load(&h_free), atomic_load(&h_alloc));
	CHECK(atomic_load(&h_alloc) > HAMMER_THREADS * HAMMER_ITERS / 4);
	CHECK_INT(atomic_load(&h_bad), 0);
}

int main(void)
{
	test_hammer();
	return TEST_DONE("page_cache_mt");
}
