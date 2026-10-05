#include "reader/page_cache.h"

#include <pthread.h>
#include <stdlib.h>

#define PC_BUCKETS 256

struct pc_entry {
	uint64_t book;
	int page, variant;
	void *value;
	size_t cost;
	pc_free_fn free_fn;
	void *user;
	int pins;
	int live;                     /* 0 once removed while pinned */
	struct pc_entry *prev, *next; /* LRU list (live) or unlinked list (pinned, removed) */
	struct pc_entry *hnext;       /* hash chain; reused as the "dead" chain while freeing */
};

struct page_cache {
	pthread_mutex_t lock;
	size_t cap, bytes;
	int count;
	struct pc_entry *head, *tail; /* head = most recently used */
	struct pc_entry *unlinked;    /* removed while pinned; freed on last release */
	struct pc_entry *bucket[PC_BUCKETS];
};

static unsigned hash_key(uint64_t book, int page, int variant)
{
	uint64_t h = book * 0x9E3779B97F4A7C15ull;
	h ^= (uint64_t)(unsigned)page * 0xC2B2AE3D27D4EB4Full;
	h ^= (uint64_t)(unsigned)variant * 0x165667B19E3779F9ull;
	h ^= h >> 29;
	return (unsigned)(h % PC_BUCKETS);
}

static struct pc_entry *find_locked(page_cache_t *pc, uint64_t book, int page, int variant)
{
	struct pc_entry *e;
	for (e = pc->bucket[hash_key(book, page, variant)]; e; e = e->hnext)
		if (e->book == book && e->page == page && e->variant == variant)
			return e;
	return NULL;
}

static void list_unlink(struct pc_entry **head, struct pc_entry **tail, struct pc_entry *e)
{
	if (e->prev)
		e->prev->next = e->next;
	else
		*head = e->next;
	if (e->next)
		e->next->prev = e->prev;
	else if (tail)
		*tail = e->prev;
	e->prev = e->next = NULL;
}

static void list_push_head(struct pc_entry **head, struct pc_entry **tail, struct pc_entry *e)
{
	e->prev = NULL;
	e->next = *head;
	if (*head)
		(*head)->prev = e;
	*head = e;
	if (tail && !*tail)
		*tail = e;
}

static void hash_remove(page_cache_t *pc, struct pc_entry *e)
{
	struct pc_entry **pp = &pc->bucket[hash_key(e->book, e->page, e->variant)];
	while (*pp && *pp != e)
		pp = &(*pp)->hnext;
	if (*pp)
		*pp = e->hnext;
	e->hnext = NULL;
}

/* Take a live entry out of the lookup structures. If pinned it parks on the unlinked list
 * (bytes still counted); otherwise it is queued on *dead for freeing outside the lock. */
static void drop_locked(page_cache_t *pc, struct pc_entry *e, struct pc_entry **dead)
{
	hash_remove(pc, e);
	list_unlink(&pc->head, &pc->tail, e);
	pc->count--;
	if (e->pins > 0) {
		e->live = 0;
		list_push_head(&pc->unlinked, NULL, e);
		return;
	}
	pc->bytes -= e->cost;
	e->hnext = *dead;
	*dead = e;
}

static void evict_locked(page_cache_t *pc, struct pc_entry *keep, struct pc_entry **dead)
{
	struct pc_entry *e = pc->tail;
	while (pc->bytes > pc->cap && e) {
		struct pc_entry *prev = e->prev;
		if (e != keep && e->pins == 0)
			drop_locked(pc, e, dead);
		e = prev;
	}
}

/* Runs the callbacks and frees the entries, in the order they were queued (oldest eviction
 * first); must be called with the lock released. */
static void free_dead(struct pc_entry *dead)
{
	struct pc_entry *rev = NULL;
	while (dead) { /* the queue is built newest-first: reverse it */
		struct pc_entry *next = dead->hnext;
		dead->hnext = rev;
		rev = dead;
		dead = next;
	}
	dead = rev;
	while (dead) {
		struct pc_entry *next = dead->hnext;
		if (dead->free_fn)
			dead->free_fn(dead->value, dead->user);
		free(dead);
		dead = next;
	}
}

page_cache_t *pc_new(size_t cap_bytes)
{
	page_cache_t *pc = calloc(1, sizeof *pc);
	if (!pc)
		return NULL;
	if (pthread_mutex_init(&pc->lock, NULL) != 0) {
		free(pc);
		return NULL;
	}
	pc->cap = cap_bytes;
	return pc;
}

void pc_free(page_cache_t *pc)
{
	struct pc_entry *dead = NULL, *e;
	if (!pc)
		return;
	while ((e = pc->head) != NULL) {
		hash_remove(pc, e);
		list_unlink(&pc->head, &pc->tail, e);
		e->hnext = dead;
		dead = e;
	}
	while ((e = pc->unlinked) != NULL) {
		list_unlink(&pc->unlinked, NULL, e);
		e->hnext = dead;
		dead = e;
	}
	free_dead(dead);
	pthread_mutex_destroy(&pc->lock);
	free(pc);
}

int pc_put(page_cache_t *pc, uint64_t book_id, int page, int variant, void *value,
           size_t cost, pc_free_fn free_fn, void *user)
{
	struct pc_entry *e, *dead = NULL;
	unsigned h;
	if (!pc || !value)
		return PC_ERR_ARG;
	if (cost > pc->cap) /* cap is immutable after pc_new */
		return PC_ERR_TOO_BIG;
	e = calloc(1, sizeof *e);
	if (!e)
		return PC_ERR_NOMEM;
	e->book = book_id;
	e->page = page;
	e->variant = variant;
	e->value = value;
	e->cost = cost;
	e->free_fn = free_fn;
	e->user = user;
	e->live = 1;

	pthread_mutex_lock(&pc->lock);
	if (find_locked(pc, book_id, page, variant)) {
		pthread_mutex_unlock(&pc->lock);
		free(e);
		return PC_ERR_EXISTS;
	}
	h = hash_key(book_id, page, variant);
	e->hnext = pc->bucket[h];
	pc->bucket[h] = e;
	list_push_head(&pc->head, &pc->tail, e);
	pc->count++;
	pc->bytes += cost;
	evict_locked(pc, e, &dead);
	pthread_mutex_unlock(&pc->lock);
	free_dead(dead);
	return PC_OK;
}

pc_ref_t *pc_get(page_cache_t *pc, uint64_t book_id, int page, int variant)
{
	struct pc_entry *e;
	if (!pc)
		return NULL;
	pthread_mutex_lock(&pc->lock);
	e = find_locked(pc, book_id, page, variant);
	if (e) {
		e->pins++;
		if (pc->head != e) {
			list_unlink(&pc->head, &pc->tail, e);
			list_push_head(&pc->head, &pc->tail, e);
		}
	}
	pthread_mutex_unlock(&pc->lock);
	return e;
}

void *pc_ref_value(const pc_ref_t *ref)
{
	return ref ? ref->value : NULL;
}

void pc_release(page_cache_t *pc, pc_ref_t *ref)
{
	struct pc_entry *dead = NULL, *e = ref;
	if (!pc || !e)
		return;
	pthread_mutex_lock(&pc->lock);
	if (e->pins > 0)
		e->pins--;
	if (e->pins == 0 && !e->live) {
		list_unlink(&pc->unlinked, NULL, e);
		pc->bytes -= e->cost;
		e->hnext = dead;
		dead = e;
	}
	evict_locked(pc, NULL, &dead);
	pthread_mutex_unlock(&pc->lock);
	free_dead(dead);
}

int pc_contains(page_cache_t *pc, uint64_t book_id, int page, int variant)
{
	int found;
	if (!pc)
		return 0;
	pthread_mutex_lock(&pc->lock);
	found = find_locked(pc, book_id, page, variant) != NULL;
	pthread_mutex_unlock(&pc->lock);
	return found;
}

int pc_remove_book(page_cache_t *pc, uint64_t book_id)
{
	struct pc_entry *dead = NULL, *e, *next;
	int n = 0;
	if (!pc)
		return 0;
	pthread_mutex_lock(&pc->lock);
	for (e = pc->head; e; e = next) {
		next = e->next;
		if (e->book == book_id) {
			drop_locked(pc, e, &dead);
			n++;
		}
	}
	pthread_mutex_unlock(&pc->lock);
	free_dead(dead);
	return n;
}

void pc_clear(page_cache_t *pc)
{
	struct pc_entry *dead = NULL, *e;
	if (!pc)
		return;
	pthread_mutex_lock(&pc->lock);
	while ((e = pc->head) != NULL)
		drop_locked(pc, e, &dead);
	pthread_mutex_unlock(&pc->lock);
	free_dead(dead);
}

size_t pc_bytes(page_cache_t *pc)
{
	size_t b;
	if (!pc)
		return 0;
	pthread_mutex_lock(&pc->lock);
	b = pc->bytes;
	pthread_mutex_unlock(&pc->lock);
	return b;
}

int pc_count(page_cache_t *pc)
{
	int n;
	if (!pc)
		return 0;
	pthread_mutex_lock(&pc->lock);
	n = pc->count;
	pthread_mutex_unlock(&pc->lock);
	return n;
}
