#include "lib/reading_clock.h"

uint64_t reading_clock_tick(reading_clock_t *c, uint64_t now, int page, int ready, int input)
{
	if (!c->started) {
		c->last = c->active = now;
		c->page = -1;
		c->started = 1;
	}
	uint64_t dt = now >= c->last ? now - c->last : 0;
	if (input) c->active = now;
	if (dt > 5000 || !c->ready || !ready || now < c->active || now - c->active >= 120000) dt = 0;
	if (ready && page >= 0 && c->page != page) {
		c->page = page;
		c->pages++;
	}
	c->last = now;
	c->ready = ready;
	c->milliseconds += dt;
	return dt;
}
