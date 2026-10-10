#include "test.h"
#include "lib/db_vfs.h"
#include <sqlite3.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
	const char *name = db_vita_vfs();
	CHECK_STR(name, "noct-vita");
	CHECK_STR(db_vita_vfs(), name);
	sqlite3_vfs *v = sqlite3_vfs_find(name);
	CHECK(v != NULL);
	if (!v) return TEST_DONE("db_vfs");
	char path[512], original[512];
	const char *paths[] = {"ux0:data/noctopage/library.db", "ux0:/data/noctopage/library.db",
	                      "uma0:books/test.db", "app0:readme", "ux0:data/library.db-journal"};
	for (unsigned i = 0; i < sizeof paths / sizeof paths[0]; i++) {
		CHECK_INT(v->xFullPathname(v, paths[i], sizeof path, path), SQLITE_OK);
		CHECK_STR(path, paths[i]);
	}
	CHECK_INT(v->xFullPathname(v, paths[0], 4, path), SQLITE_CANTOPEN);
	CHECK_INT(v->xFullPathname(v, paths[0], 0, path), SQLITE_CANTOPEN);
	CHECK_INT(v->xFullPathname(v, paths[0], (int)strlen(paths[0]), path), SQLITE_CANTOPEN);
	sqlite3_vfs *base = sqlite3_vfs_find("unix-none");
	CHECK_INT(base->xFullPathname(base, paths[0], sizeof original, original), SQLITE_OK);
	CHECK(strcmp(original, paths[0]) != 0); /* reproduce the original path corruption */
	CHECK_INT(base->xFullPathname(base, "relative.db", sizeof original, original), SQLITE_OK);
	CHECK_INT(v->xFullPathname(v, "relative.db", sizeof path, path), SQLITE_OK);
	CHECK_STR(path, original);
	CHECK_INT(v->xFullPathname(v, "/tmp/nocto.db", sizeof path, path), SQLITE_OK);
	CHECK_STR(path, "/tmp/nocto.db");

	/* A POSIX directory with a Vita mount name lets the real Unix I/O test persist,
	 * close and reopen through this adapter, including rollback journal writes. */
	mkdir("build-host-db", 0777);
	mkdir("build-host-db/ux0:data", 0777);
	CHECK_INT(chdir("build-host-db"), 0);
	unlink("ux0:data/vfs-test.db");
	sqlite3 *db = NULL;
	int rc = sqlite3_open_v2("ux0:data/vfs-test.db", &db,
	                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, name);
	CHECK_INT(rc, SQLITE_OK);
	if (rc == SQLITE_OK) {
		CHECK_STR(sqlite3_db_filename(db, "main"), "ux0:data/vfs-test.db");
		CHECK_INT(sqlite3_exec(db, "CREATE TABLE books(path TEXT); INSERT INTO books VALUES('ux0:books/test.cbz')",
		                      NULL, NULL, NULL), SQLITE_OK);
	}
	CHECK_INT(sqlite3_close(db), SQLITE_OK);
	db = NULL;
	rc = sqlite3_open_v2("ux0:data/vfs-test.db", &db, SQLITE_OPEN_READWRITE, name);
	CHECK_INT(rc, SQLITE_OK);
	if (rc == SQLITE_OK) {
		sqlite3_stmt *st = NULL;
		CHECK_INT(sqlite3_prepare_v2(db, "SELECT path FROM books", -1, &st, NULL), SQLITE_OK);
		if (st) {
			CHECK_INT(sqlite3_step(st), SQLITE_ROW);
			CHECK_STR((const char *)sqlite3_column_text(st, 0), "ux0:books/test.cbz");
			sqlite3_finalize(st);
		}
	}
	CHECK_INT(sqlite3_close(db), SQLITE_OK);
	unlink("ux0:data/vfs-test.db");
	return TEST_DONE("db_vfs");
}