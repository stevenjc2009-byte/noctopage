#include "test.h"

#include "sqlite/sqlite3.h"

#include <stdlib.h>
#include <string.h>

static int exec(sqlite3 *db, const char *sql)
{
    char *err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    sqlite3_free(err); /* callers check the return code; constraint errors are expected here */
    return rc;
}

static void test_memory_db(void)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *st = NULL;

    CHECK_INT(sqlite3_open(":memory:", &db), SQLITE_OK);
    CHECK_INT(exec(db, "CREATE TABLE books(id INTEGER PRIMARY KEY, bookid TEXT UNIQUE, "
                       "title TEXT NOT NULL, page INTEGER DEFAULT 0, size INTEGER)"),
              SQLITE_OK);

    /* Insert with bound parameters, including a 64-bit value and UTF-8 text. */
    CHECK_INT(sqlite3_prepare_v2(db, "INSERT INTO books(bookid,title,size) VALUES(?,?,?)", -1, &st, NULL),
              SQLITE_OK);
    sqlite3_bind_text(st, 1, "af63dc4c8601ec8c", -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, "\xE6\xBC\xAB\xE7\x94\xBB Vol. 1", -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 3, 5000000000LL);
    CHECK_INT(sqlite3_step(st), SQLITE_DONE);
    sqlite3_reset(st);
    sqlite3_bind_text(st, 1, "cbf29ce484222325", -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, "Plain Book", -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 3, 123);
    CHECK_INT(sqlite3_step(st), SQLITE_DONE);
    sqlite3_finalize(st);
    CHECK_INT(sqlite3_changes(db), 1);

    /* Duplicate bookid hits the UNIQUE constraint. */
    CHECK_INT(exec(db, "INSERT INTO books(bookid,title) VALUES('cbf29ce484222325','dup')"), SQLITE_CONSTRAINT);
    CHECK_INT(exec(db, "INSERT INTO books(bookid,title) VALUES('x',NULL)"), SQLITE_CONSTRAINT);

    CHECK_INT(sqlite3_prepare_v2(db, "SELECT title, page, size FROM books ORDER BY size DESC", -1, &st, NULL),
              SQLITE_OK);
    CHECK_INT(sqlite3_step(st), SQLITE_ROW);
    CHECK_STR((const char *)sqlite3_column_text(st, 0), "\xE6\xBC\xAB\xE7\x94\xBB Vol. 1");
    CHECK_INT(sqlite3_column_int(st, 1), 0);
    CHECK_INT(sqlite3_column_int64(st, 2), 5000000000LL);
    CHECK_INT(sqlite3_step(st), SQLITE_ROW);
    CHECK_STR((const char *)sqlite3_column_text(st, 0), "Plain Book");
    CHECK_INT(sqlite3_step(st), SQLITE_DONE);
    sqlite3_finalize(st);

    /* UPDATE + aggregate. */
    CHECK_INT(exec(db, "UPDATE books SET page=41 WHERE bookid='cbf29ce484222325'"), SQLITE_OK);
    CHECK_INT(sqlite3_prepare_v2(db, "SELECT count(*), sum(page) FROM books", -1, &st, NULL), SQLITE_OK);
    CHECK_INT(sqlite3_step(st), SQLITE_ROW);
    CHECK_INT(sqlite3_column_int(st, 0), 2);
    CHECK_INT(sqlite3_column_int(st, 1), 41);
    sqlite3_finalize(st);

    CHECK_INT(sqlite3_close(db), SQLITE_OK);
}

static void test_build_features(void)
{
    CHECK(strncmp(sqlite3_libversion(), "3.", 2) == 0);
    CHECK_INT(sqlite3_threadsafe() != 0, 1);              /* host build uses THREADSAFE=1 */
    CHECK_STR(sqlite3_compileoption_used("THREADSAFE=1") ? "yes" : "no", "yes");
}

static void test_transaction_rollback(void)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *st = NULL;

    CHECK_INT(sqlite3_open(":memory:", &db), SQLITE_OK);
    CHECK_INT(exec(db, "CREATE TABLE t(v INTEGER)"), SQLITE_OK);
    CHECK_INT(exec(db, "BEGIN; INSERT INTO t VALUES(1); INSERT INTO t VALUES(2); ROLLBACK;"), SQLITE_OK);
    CHECK_INT(exec(db, "BEGIN; INSERT INTO t VALUES(3); COMMIT;"), SQLITE_OK);
    CHECK_INT(sqlite3_prepare_v2(db, "SELECT count(*), min(v) FROM t", -1, &st, NULL), SQLITE_OK);
    CHECK_INT(sqlite3_step(st), SQLITE_ROW);
    CHECK_INT(sqlite3_column_int(st, 0), 1);
    CHECK_INT(sqlite3_column_int(st, 1), 3);
    sqlite3_finalize(st);
    sqlite3_close(db);
}

int main(void)
{
    test_memory_db();
    test_build_features();
    test_transaction_rollback();
    return TEST_DONE("sqlite");
}
