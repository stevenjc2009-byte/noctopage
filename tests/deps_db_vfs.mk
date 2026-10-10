DEPS_db_vfs = $(BUILD)/sqlite3.o
CFLAGS_db_vfs = -I../third_party/sqlite
LIBS_db_vfs = -lpthread -ldl -lm
