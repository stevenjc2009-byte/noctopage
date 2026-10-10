#ifndef NOCT_UTILITIES_H
#define NOCT_UTILITIES_H
#include "ui/input.h"
#include "ui/hintbar.h"
void utilities_wifi(void);
void utilities_backup(void);
int utilities_busy(void);
int utilities_working(void);
int utilities_keep_awake(void);
/* Sleep timer tick (main loop, every frame): suspends after the Settings minutes. */
void utilities_sleep_tick(uint64_t now_ms, int active);
void utilities_update(const input_t *in);
void utilities_draw(uint64_t now);
int utilities_hints(hint_t *out, int max);
void utilities_close(void);
#endif
