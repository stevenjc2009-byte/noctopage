# Host test harness. Builds every tests/test_<name>.c into build-host/test_<name> with host gcc
# (run from WSL: see tools/run_tests.sh) and `make run` executes them all.
#
# Convention: test_<name>.c is linked with ../src/<dir>/<name>.c for each dir in PURE_DIRS, so
# test_strutil.c gets src/core/strutil.c. Extra dependencies go in tests/deps.mk:
#     DEPS_<name> = ../src/core/strutil.c ../src/core/natsort.c   # more sources / objects
#     LIBS_<name> = -lm                                           # more link flags
# Only pure code (no psp2/vita2d/mupdf) may be built here.

CC      ?= gcc
BUILD   ?= build-host
WARN    := -std=gnu11 -Wall -Wextra -Werror
CFLAGS  := $(WARN) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
           -I. -I../src -I../third_party
# Vendored third-party code is compiled without -Werror and without sanitizers.
TPFLAGS := -std=gnu11 -O1 -w -I../third_party

# Directories holding pure modules. src/screens, src/ui and src/*.c are Vita-only: never built here.
PURE_DIRS := ../src/core ../src/lib ../src/img ../src/comic ../src/doc ../src/reader ../src/net

TESTS := $(sort $(patsubst test_%.c,%,$(wildcard test_*.c)))
BINS  := $(addprefix $(BUILD)/test_,$(TESTS))

# Host libraries the Vita build gets from the SDK: headers + .so unpacked without sudo into
# HOSTDEPS (libarchive, turbojpeg, libpng, libwebp; see tests/README-hostdeps.md). A test that
# needs them adds $(HOSTDEPS_CFLAGS) via CFLAGS_<name> and $(HOSTDEPS_LIBS) -l... via LIBS_<name>.
HOSTDEPS        ?= $(HOME)/noct-hostdeps
HOSTDEPS_CFLAGS := -I$(HOSTDEPS)/usr/include
HOSTDEPS_LIBS   := -L$(HOSTDEPS)/usr/lib/x86_64-linux-gnu -Wl,-rpath,$(HOSTDEPS)/usr/lib/x86_64-linux-gnu
# MuPDF host build (tools/build_mupdf.sh host).
MUPDF_HOST_CFLAGS := -I../third_party/mupdf/host/include
MUPDF_HOST_LIBS   := ../third_party/mupdf/host/lib/libmupdf.a ../third_party/mupdf/host/lib/libmupdf-third.a -lm

DEPS_cjson  = $(BUILD)/cJSON.o
DEPS_sqlite = $(BUILD)/sqlite3.o
LIBS_sqlite = -lpthread -ldl -lm
# deps.mk is shared; each work package owns its own deps_<pkg>.mk.
-include deps.mk $(sort $(wildcard deps_*.mk))

all: $(BINS)

$(BUILD)/cJSON.o: ../third_party/cjson/cJSON.c ../third_party/cjson/cJSON.h
	mkdir -p $(BUILD)
	$(CC) $(TPFLAGS) -c $< -o $@

$(BUILD)/sqlite3.o: ../third_party/sqlite/sqlite3.c ../third_party/sqlite/sqlite3.h
	mkdir -p $(BUILD)
	$(CC) $(TPFLAGS) -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DEFAULT_MEMSTATUS=0 -c $< -o $@

define TEST_RULE
$$(BUILD)/test_$(1): test_$(1).c test.h $$(foreach d,$$(PURE_DIRS),$$(wildcard $$(d)/$(1).c)) $$(wildcard ../src/*/*.h) $$(DEPS_$(1))
	mkdir -p $$(BUILD)
	$$(CC) $$(CFLAGS) $$(CFLAGS_$(1)) -o $$@ test_$(1).c $$(foreach d,$$(PURE_DIRS),$$(wildcard $$(d)/$(1).c)) $$(DEPS_$(1)) $$(LIBS_$(1))
endef
$(foreach t,$(TESTS),$(eval $(call TEST_RULE,$(t))))

# Runs every test binary, prints each one's summary line and a grand total, and exits non-zero
# when any binary failed, crashed, or printed no "N checks, M failed" summary.
run: $(BINS)
	@total=0; bad=0; rc=0; \
	for t in $(BINS); do \
	  out=`$$t 2>&1`; r=$$?; echo "$$out"; \
	  line=`echo "$$out" | tail -n 1`; \
	  n=`echo "$$line" | sed -n 's/^.*: \([0-9][0-9]*\) checks, \([0-9][0-9]*\) failed.*$$/\1/p'`; \
	  m=`echo "$$line" | sed -n 's/^.*: \([0-9][0-9]*\) checks, \([0-9][0-9]*\) failed.*$$/\2/p'`; \
	  if [ -z "$$n" ]; then echo "FAIL: $$t printed no summary line (exit $$r)"; rc=1; n=0; m=0; fi; \
	  if [ $$r -ne 0 ]; then rc=1; fi; \
	  total=$$((total + n)); bad=$$((bad + m)); \
	done; \
	echo "$$total checks, $$bad failed"; \
	[ $$rc -eq 0 ] && [ $$bad -eq 0 ]

clean:
	rm -rf $(BUILD)

.PHONY: all run clean
