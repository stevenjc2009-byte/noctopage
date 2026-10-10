DEPS_catalog_api = ../src/net/opds.c $(OPDS_SRCS)
CFLAGS_catalog_api = -I../third_party/cjson
DEPS_release = $(BUILD)/cJSON.o
CFLAGS_release = -I../third_party/cjson
DEPS_update_package = ../src/net/release.c $(BUILD)/cJSON.o
CFLAGS_update_package = $(HOSTDEPS_CFLAGS) -I../third_party/cjson
LIBS_update_package = $(HOSTDEPS_LIBS) -larchive -lcrypto

DEPS_catalog_downloads = ../src/net/opds.c $(OPDS_SRCS)
CFLAGS_catalog_downloads = -I../third_party/cjson
