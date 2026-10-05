/* input.h - buttons, d-pad key repeat, analog stick, front touch.
 * Screens use the abstract IN_CONFIRM / IN_BACK so the system Cross/Circle
 * enter-button setting is honoured; physical IN_CROSS etc. are for hint tables. */
#ifndef NOCT_INPUT_H
#define NOCT_INPUT_H

#include <stdint.h>

enum {
	IN_UP       = 1u << 0,
	IN_DOWN     = 1u << 1,
	IN_LEFT     = 1u << 2,
	IN_RIGHT    = 1u << 3,
	IN_CROSS    = 1u << 4,
	IN_CIRCLE   = 1u << 5,
	IN_SQUARE   = 1u << 6,
	IN_TRIANGLE = 1u << 7,
	IN_L        = 1u << 8,
	IN_R        = 1u << 9,
	IN_START    = 1u << 10,
	IN_SELECT   = 1u << 11,
	/* Logical, derived from the system enter-button setting. */
	IN_CONFIRM  = 1u << 12,
	IN_BACK     = 1u << 13,
};

#define INPUT_MAX_TOUCH 6

typedef struct {
	int id;
	int x, y; /* screen coordinates, 960x544 */
} touch_pt_t;

typedef struct {
	uint32_t pressed;  /* went down this frame; d-pad bits also fire on key repeat */
	uint32_t held;
	uint32_t released;
	int lx, ly, rx, ry; /* sticks, 0..255, centre 128 */
	int touch_count;
	touch_pt_t touch[INPUT_MAX_TOUCH];
	int touch_began;    /* a finger went down this frame ... */
	int began_x, began_y; /* ... at this point */
	uint64_t now_ms;
} input_t;

void input_init(void);
void input_poll(input_t *in);

/* 1 if the system enter button is Cross (the Western default), 0 if Circle. */
int input_confirm_is_cross(void);

/* Test hook (core/autotest): buttons OR-ed into the real ones on every poll
 * until changed. Uses the physical IN_* bits; IN_CONFIRM/IN_BACK derive from them. */
void input_inject_set(uint32_t held_mask);

#endif
