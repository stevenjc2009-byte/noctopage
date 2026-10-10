/* affinity.h - let worker threads use every CPU core, including CPU3 when it is
 * unlocked (e.g. by an extra-core mod). Stock consoles only have cores 0-2, so the
 * call is best effort: failure just keeps the Vita default mask. Host builds no-op. */
#ifndef NOCT_AFFINITY_H
#define NOCT_AFFINITY_H

#ifdef __vita__
void affinity_use_all_cores(void);
#else
static inline void affinity_use_all_cores(void) {}
#endif

#endif
