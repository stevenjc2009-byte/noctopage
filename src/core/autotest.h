/* autotest.h - scripted input for the Vita3K test harness.
 *
 * If ux0:data/noctopage/autotest.txt exists at boot it is read, renamed to
 * autotest.done, and its commands run one at a time from the main loop.
 * Commands are separated by newlines or ';' ('#' starts a comment line):
 *   wait MS            pause the script
 *   press NAME         pressed + released for one frame each
 *   hold NAME MS       hold a button for MS milliseconds
 *   shot NAME          log "AUTOTEST shot NAME", then pause 1500 ms (still rendering)
 *   log TEXT           log "AUTOTEST log TEXT"
 *   exit               log "exit", then sceKernelExitProcess(0) (no vita2d_fini)
 * NAME: cross circle square triangle up down left right l r start select.
 * Injected buttons go through input_inject_set(), i.e. before the abstract
 * confirm/back mapping, exactly like a real button press. */
#ifndef NOCT_AUTOTEST_H
#define NOCT_AUTOTEST_H

#include <stdint.h>

void autotest_init(void);
/* Call once per frame BEFORE input_poll(), with the current time in ms. */
void autotest_tick(uint64_t now_ms);
int autotest_active(void);

#endif
