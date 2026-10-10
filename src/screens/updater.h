#ifndef NOCT_UPDATER_H
#define NOCT_UPDATER_H
#include "ui/input.h"
#include "ui/hintbar.h"
void updater_init(void);
void updater_tick(void);
void updater_open(void);
void updater_configure(void);
const char *updater_repository(void);
void updater_update(const input_t *in);
void updater_draw(void);
int updater_hints(hint_t *out,int max);
int updater_busy(void);
int updater_install_requested(void);
void updater_stop(void);
/* Called after all other workers and the database have closed. */
void updater_install(void);
#endif
