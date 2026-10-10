#ifndef NOCT_BACKUP_H
#define NOCT_BACKUP_H
#include <stdint.h>
#define PATH_BACKUPS "ux0:data/noctopage/backups"
typedef struct { int running, done, result, books, covers; char message[192], path[512]; } backup_state_t;
/* One job at a time. Restore first creates an automatic before-restore backup. */
int backup_start(int restore, const char *path);
void backup_state(backup_state_t *out);
void backup_join(void);
#endif
