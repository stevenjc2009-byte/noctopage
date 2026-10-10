#ifndef NOCT_PRIVACY_H
#define NOCT_PRIVACY_H
#include <stdint.h>
int privacy_unlock(uint64_t now);
int privacy_is_unlocked(void);
int privacy_tick(uint64_t now,int active);
void privacy_lock(void);
void privacy_change(uint64_t now);
#endif
