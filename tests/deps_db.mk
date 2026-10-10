DEPS_db        = ../src/lib/db_tables.c ../src/core/strutil.c ../src/core/natsort.c $(BUILD)/sqlite3.o ../src/core/casefold.c ../src/lib/shelf.c ../src/lib/db_extras.c
CFLAGS_db      = -I../third_party/sqlite
LIBS_db        = -lpthread -ldl -lm
DEPS_bookid    = ../src/core/fnv.c
DEPS_db_more   = ../src/lib/db.c ../src/lib/db_tables.c ../src/core/strutil.c ../src/core/natsort.c $(BUILD)/sqlite3.o ../src/core/casefold.c ../src/lib/shelf.c ../src/lib/db_extras.c
CFLAGS_db_more = -I../third_party/sqlite
LIBS_db_more   = -lpthread -ldl -lm
