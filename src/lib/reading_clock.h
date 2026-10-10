#ifndef NOCT_READING_CLOCK_H
#define NOCT_READING_CLOCK_H
#include <stdint.h>
typedef struct {
	uint64_t last, active, milliseconds;
	int page, pages, started, ready;
} reading_clock_t;
/* Returns active milliseconds this frame. Stops after 2 minutes without input,
 * excludes loading/menu time and long suspend gaps. Page changes count once. */
uint64_t reading_clock_tick(reading_clock_t *c, uint64_t now, int page, int ready, int input);
#endif
