#ifndef NOCT_DB_VFS_H
#define NOCT_DB_VFS_H
/* Register a unix-none VFS whose absolute-path handling understands Vita mounts.
 * Call before sqlite3_open_v2. Returns its name, or NULL on registration failure. */
const char *db_vita_vfs(void);
#endif