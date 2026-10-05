LIBS_page_cache = -lpthread
LIBS_zones      = -lm
# split-out test files (the Makefile links test_<name>.c with src/*/<name>.c only for a real module)
DEPS_fit_strip     = ../src/reader/fit.c
DEPS_page_cache_mt = ../src/reader/page_cache.c
LIBS_page_cache_mt = -lpthread
