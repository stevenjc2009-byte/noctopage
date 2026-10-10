/* db_priv — internal helpers shared by db.c and db_tables.c. Not for other modules. */
#ifndef NOCTO_DB_PRIV_H
#define NOCTO_DB_PRIV_H

#include "sqlite3.h"

#include <stddef.h>
#include <stdint.h>

#define DBI(x) ((int64_t)(x))

#define DB_SCHEMA_VERSION 3
/* migrations[n] upgrades user_version n to n + 1 (defined in db_tables.c). */
extern const char *const db_migrations[DB_SCHEMA_VERSION];

typedef int (*db_rowfn)(sqlite3_stmt *, void *); /* return non-zero to stop */

/* Run one statement under the module lock. fmt has one letter per ?N parameter: I int64_t,
 * D double, T const char* (NULL binds NULL). With a row callback returns the rows visited, else
 * the rows changed; < 0 on error. */
int db_priv_ql(const char *sql, db_rowfn cb, void *ud, const char *fmt, ...);
/* Same for an INSERT: 0 on success with the new rowid in *id. */
int db_priv_insert(const char *sql, int64_t *id, const char *fmt, ...);
/* Current time through the clock hook (takes the lock; do not call with it held). */
int64_t db_priv_now(void);
/* Copy column i of the current row as text into dst (cap bytes, always NUL-terminated). */
void db_priv_col_str(sqlite3_stmt *st, int i, char *dst, size_t cap);

#endif
