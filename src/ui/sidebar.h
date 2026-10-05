/* sidebar.h - 205 px left sidebar: app name, five sections. */
#ifndef NOCT_SIDEBAR_H
#define NOCT_SIDEBAR_H

#include <stdint.h>

typedef enum {
	SEC_MYBOOKS,
	SEC_CATALOG,
	SEC_HISTORY,
	SEC_DOWNLOADS,
	SEC_SETTINGS,
	SEC_COUNT
} section_t;

const char *sidebar_label(section_t s);
/* Draws the sidebar with `current` highlighted. Keeps its own slide animation
 * (selected row's icon and label ease 5 px right, 0.1 per frame). */
void sidebar_draw(section_t current, uint64_t now_ms);
/* Section under a touch point, or -1. */
int sidebar_hit(int x, int y);

#endif
