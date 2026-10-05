/* osk - the system on-screen keyboard (sceImeDialog) for typing a server address, a user
 * name, a password or a catalog search. Blocking: it runs its own draw loop until the user
 * confirms or cancels, because the dialog is composited by vita2d_common_dialog_update()
 * and only appears while something calls that every frame. Worker threads keep running.
 * Adapted from vita-radio src/ime.c. MAIN THREAD ONLY. */
#ifndef NOCT_OSK_H
#define NOCT_OSK_H

#include <stddef.h>

#define OSK_MAX_TEXT 255

typedef enum { OSK_TEXT, OSK_URL, OSK_PASSWORD } osk_kind_t;

/* Once at boot, after vita2d_init and input_init. Returns 0 or -1. */
int osk_init(void);
/* title and initial are UTF-8; out receives UTF-8 (at most OSK_MAX_TEXT characters).
 * Returns 1 when confirmed, 0 when cancelled (out untouched), -1 on error. */
int osk_prompt(const char *title, const char *initial, osk_kind_t kind, char *out, size_t cap);
/* Before exit: ends SceAppUtil, whose service thread otherwise keeps the process alive
 * after sceKernelExitProcess (seen on hardware by vita-radio). */
void osk_shutdown(void);

#endif
