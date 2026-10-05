# mdoc (src/doc/mdoc*.c): MuPDF wrapper. mdoc.c is linked automatically.
DEPS_mdoc   = ../src/doc/mdoc_render.c ../src/doc/mdoc_pos.c ../src/doc/mdoc_toc.c
CFLAGS_mdoc = $(MUPDF_HOST_CFLAGS)
LIBS_mdoc   = $(MUPDF_HOST_LIBS) -lpthread -lm
