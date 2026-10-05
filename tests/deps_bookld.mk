# bookld (reader/bookld worker for reflowable books): links mdoc (MuPDF) and the page cache.
# test_bookld.c gets ../src/reader/bookld.c from the PURE_DIRS rule; test_bookld_more.c
# has no src/*/bookld_more.c, so it lists bookld.c itself.
BOOKLD_SRCS = ../src/reader/bookld_work.c ../src/reader/page_cache.c \
              ../src/doc/mdoc.c ../src/doc/mdoc_render.c ../src/doc/mdoc_pos.c ../src/doc/mdoc_toc.c
BOOKLD_LIBS = $(MUPDF_HOST_LIBS) -lpthread -lm

DEPS_bookld        = $(BOOKLD_SRCS)
CFLAGS_bookld      = $(MUPDF_HOST_CFLAGS)
LIBS_bookld        = $(BOOKLD_LIBS)
DEPS_bookld_more   = ../src/reader/bookld.c $(BOOKLD_SRCS)
CFLAGS_bookld_more = $(MUPDF_HOST_CFLAGS)
LIBS_bookld_more   = $(BOOKLD_LIBS)
