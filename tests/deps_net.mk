# net work package (src/net: http, http_vita, fetch, dlq, dlq_name). Paths relative to tests/.
# Host libcurl: WSL has the runtime library but no -dev package, so the headers are the
# vendored Vita curl's (8.22, same stable ABI) and the .so is linked by its full path.
NET_CURL_CFLAGS = -I../third_party/tls-mbedtls/include
NET_CURL_LIBS   = /usr/lib/x86_64-linux-gnu/libcurl.so.4 -lpthread

DEPS_http   = ../src/net/dlq_name.c ../src/core/strutil.c
CFLAGS_http = $(NET_CURL_CFLAGS)
LIBS_http   = $(NET_CURL_LIBS)

DEPS_fetch   = ../src/net/http.c ../src/net/dlq_name.c ../src/core/strutil.c ../src/img/decode.c ../src/img/scale.c
CFLAGS_fetch = $(NET_CURL_CFLAGS) $(HOSTDEPS_CFLAGS) $(MUPDF_HOST_CFLAGS)
LIBS_fetch   = $(NET_CURL_LIBS) $(MUPDF_HOST_LIBS) $(HOSTDEPS_LIBS) -lturbojpeg -lpng16 -lwebp -lz -lm

DEPS_dlq   = ../src/net/dlq_name.c ../src/net/http.c ../src/lib/db.c ../src/lib/db_tables.c ../src/core/casefold.c ../src/lib/shelf.c \
             ../src/core/strutil.c ../src/core/natsort.c $(BUILD)/sqlite3.o
CFLAGS_dlq = $(NET_CURL_CFLAGS) -I../third_party/sqlite
LIBS_dlq   = $(NET_CURL_LIBS) -ldl -lm
