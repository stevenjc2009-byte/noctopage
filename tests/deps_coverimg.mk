# coverimg (main thread's Phase 1 work): links the comic, image and document engines.
DEPS_coverimg   = ../src/comic/archive.c ../src/comic/zipfast.c ../src/comic/folder.c ../src/comic/pagelist.c \
                  ../src/lib/formats.c ../src/core/strutil.c ../src/core/natsort.c \
                  ../src/img/decode.c ../src/img/scale.c \
                  ../src/doc/mdoc.c ../src/doc/mdoc_render.c ../src/doc/mdoc_pos.c ../src/doc/mdoc_toc.c $(BUILD)/cJSON.o
CFLAGS_coverimg = $(HOSTDEPS_CFLAGS) $(MUPDF_HOST_CFLAGS)
LIBS_coverimg   = $(MUPDF_HOST_LIBS) $(HOSTDEPS_LIBS) -larchive -lturbojpeg -lpng16 -lwebp -lz -lpthread -lm
