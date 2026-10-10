# engines work package: archive, folder, decode, scale. Paths relative to tests/.
# test_archive also decodes every page of the fixtures, so it links the image engine too.
ENGINES_LIBS = $(MUPDF_HOST_LIBS) $(HOSTDEPS_LIBS) -larchive -lturbojpeg -lpng16 -lwebp -lz -lpthread -lm

DEPS_archive   = ../src/img/decode.c ../src/img/scale.c ../src/comic/zipfast.c $(BUILD)/cJSON.o
CFLAGS_archive = $(HOSTDEPS_CFLAGS) $(MUPDF_HOST_CFLAGS)
LIBS_archive   = $(ENGINES_LIBS)

CFLAGS_folder  = $(HOSTDEPS_CFLAGS)

DEPS_decode    = ../src/img/scale.c
CFLAGS_decode  = $(HOSTDEPS_CFLAGS) $(MUPDF_HOST_CFLAGS)
LIBS_decode    = $(ENGINES_LIBS)

LIBS_scale     = -lm

DEPS_archive_gen   = ../src/comic/archive.c ../src/comic/zipfast.c
CFLAGS_archive_gen = $(HOSTDEPS_CFLAGS)
LIBS_archive_gen   = $(HOSTDEPS_LIBS) -larchive -lz -lpthread

DEPS_decode_gen   = ../src/img/decode.c ../src/img/scale.c
CFLAGS_decode_gen = $(HOSTDEPS_CFLAGS) $(MUPDF_HOST_CFLAGS)
LIBS_decode_gen   = $(MUPDF_HOST_LIBS) $(HOSTDEPS_LIBS) -lturbojpeg -lpng16 -lwebp -lz -lpthread -lm
