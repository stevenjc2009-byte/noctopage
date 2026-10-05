# loader (reader/loader worker + page cache): links the comic, image and document engines.
# test_loader.c gets ../src/reader/loader.c from the PURE_DIRS rule; test_loader_more.c
# has no src/*/loader_more.c, so it lists loader.c itself.
LOADER_SRCS = ../src/reader/loader_src.c ../src/reader/page_cache.c \
              ../src/comic/archive.c ../src/comic/folder.c ../src/comic/pagelist.c \
              ../src/lib/formats.c ../src/core/strutil.c ../src/core/natsort.c \
              ../src/img/decode.c ../src/img/scale.c \
              ../src/doc/mdoc.c ../src/doc/mdoc_render.c ../src/doc/mdoc_pos.c ../src/doc/mdoc_toc.c $(BUILD)/cJSON.o
LOADER_LIBS = $(MUPDF_HOST_LIBS) $(HOSTDEPS_LIBS) -larchive -lturbojpeg -lpng16 -lwebp -lz -lpthread -lm

DEPS_loader        = $(LOADER_SRCS)
CFLAGS_loader      = $(HOSTDEPS_CFLAGS) $(MUPDF_HOST_CFLAGS)
LIBS_loader        = $(LOADER_LIBS)
DEPS_loader_more   = ../src/reader/loader.c $(LOADER_SRCS)
CFLAGS_loader_more = $(HOSTDEPS_CFLAGS) $(MUPDF_HOST_CFLAGS)
LIBS_loader_more   = $(LOADER_LIBS)
