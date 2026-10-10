DEPS_shelf = ../src/core/casefold.c  ../src/core/strutil.c ../src/core/natsort.c
DEPS_features = ../src/core/casefold.c  ../src/lib/db.c ../src/lib/db_tables.c ../src/lib/db_extras.c ../src/lib/shelf.c ../src/core/strutil.c ../src/core/natsort.c $(BUILD)/sqlite3.o
CFLAGS_features = -I../third_party/sqlite
LIBS_features = -lpthread -ldl -lm
DEPS_coversearch = ../src/net/url.c ../src/core/strutil.c $(BUILD)/cJSON.o
CFLAGS_coversearch = -I../third_party/cjson
LIBS_coversearch = -lm

DEPS_stickzoom = ../src/reader/fit.c
LIBS_stickzoom = -lm

DEPS_shelf_progress = ../src/lib/shelf.c ../src/core/casefold.c ../src/core/strutil.c ../src/core/natsort.c
CFLAGS_shelf_progress = -Istubs -ffunction-sections -fdata-sections -Wno-unused-function
LIBS_shelf_progress = -Wl,--gc-sections -lm
$(BUILD)/test_shelf_progress: ../src/screens/mybooks.c
LIBS_pin = -lcrypto
DEPS_seriesmeta = ../src/core/strutil.c $(BUILD)/cJSON.o
CFLAGS_seriesmeta = -I../third_party/cjson
LIBS_seriesmeta = -lm
