/* paths.h - on-device data locations (see docs/PLAN.md "Data on the device"). */
#ifndef NOCT_PATHS_H
#define NOCT_PATHS_H

#define PATH_DATA_ROOT   "ux0:data/noctopage"
#define PATH_DB          "ux0:data/noctopage/library.db"
#define PATH_THUMBS_DIR  "ux0:data/noctopage/thumbs"
#define PATH_SETTINGS    "ux0:data/noctopage/settings.ini"
#define PATH_BOOKS_DIR   "ux0:books"
#define PATH_LOG         "ux0:data/noctopage/log.txt"
#define PATH_AUTOTEST    "ux0:data/noctopage/autotest.txt"
#define PATH_AUTOTEST_DONE "ux0:data/noctopage/autotest.done"

/* mkdir -p for a Vita path ("ux0:a/b/c"). Returns 0 on success (or if it exists), <0 on error. */
int paths_mkdir_p(const char *path);

/* Creates the app data root, the thumbs dir and ux0:books. Safe to call repeatedly. */
void paths_init(void);

#endif
