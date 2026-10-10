# Noctopage - plain make build for the PS Vita (VitaSDK, no cmake).
#   make              -> build/Noctopage.vpk
#   make BUILD=build2 -> separate object/output dir
#   make sizes        -> section sizes of the linked ELF
#   make clean        -> removes $(BUILD) only
# Host tests live in tests/Makefile and are NOT part of this build.

VITASDK ?= $(HOME)/vitasdk
PREFIX  := $(VITASDK)/bin/arm-vita-eabi
CC      := $(PREFIX)-gcc
SIZE    := $(PREFIX)-size

TITLE     := Noctopage
TITLE_ID  := NOCT00001
APP_VER   := 00.04
TARGET    := noctopage
VPK_NAME  := Noctopage.vpk
BUILD     ?= build

# The optional third-party rules below must not become the default goal.
.DEFAULT_GOAL := all

# Every src/**/*.c, objects mirror the source tree under $(BUILD).
SRCS := $(shell find src -name '*.c' | sort)
OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

CFLAGS := -std=gnu11 -Wall -Wextra -Werror -O2 -Isrc -MMD -MP
CFLAGS += -I$(VITASDK)/arm-vita-eabi/include
CFLAGS += -DNOCT_VERSION=\"$(APP_VER)\"

# ---- TLS: vendored curl + mbedTLS, exactly as vita-radio does ------------------
TLS_DIR  := third_party/tls-mbedtls
CFLAGS   += -DCURL_STATICLIB -I$(TLS_DIR)/include
TLS_LIBS := -L$(TLS_DIR)/lib -lcurl -lmbedtls -lmbedx509 -lmbedcrypto

# ---- MuPDF (built by tools/build_mupdf.sh), required -------------------------
# It must come BEFORE freetype, harfbuzz, jpeg, z (it depends on them); harfbuzz is
# C++ so -lstdc++ follows. The UI font (src/ui/font.c) uses MuPDF's embedded fonts.
MUPDF_LIB := third_party/mupdf/vita/lib/libmupdf.a
ifeq ($(wildcard $(MUPDF_LIB)),)
$(error $(MUPDF_LIB) missing: run tools/build_mupdf.sh vita)
endif
CFLAGS      += -Ithird_party/mupdf/vita/include -DNOCT_HAVE_MUPDF
CFLAGS      += -I$(VITASDK)/arm-vita-eabi/include/freetype2
MUPDF_LIBS  := -Lthird_party/mupdf/vita/lib -lmupdf -lmupdf-third
MUPDF_DEPS  := -lharfbuzz -lfreetype -lbz2 -lstdc++ -ljpeg -lz

# ---- libarchive WITH liblzma (built by tools/build_libarchive.sh) --------------------
# The VitaSDK's own libarchive.a has no LZMA, so .cb7/.7z fail on the Vita ("LZMA codec is
# unsupported"). The vendored build goes first: its -I is prepended, so it precedes the SDK
# include dir given at the top, and its -L precedes the SDK lib dir.
LIBARCHIVE_LIB := third_party/libarchive/vita/lib/libarchive.a
ifeq ($(wildcard $(LIBARCHIVE_LIB)),)
$(warning $(LIBARCHIVE_LIB) missing: run tools/build_libarchive.sh; falling back to the SDK libarchive, which has NO LZMA (CB7 fails))
else
CFLAGS          := -Ithird_party/libarchive/vita/include $(CFLAGS)
LIBARCHIVE_LIBS := -Lthird_party/libarchive/vita/lib
endif

# ---- Optional third-party sources compiled straight into the app ---------------
# Compiled with relaxed warnings (-w): we do not own that code, -Werror stays on ours.
EXTRA_OBJS :=
TP_CFLAGS  := -std=gnu11 -O2 -w -MMD -MP -I$(VITASDK)/arm-vita-eabi/include

# SQLite amalgamation. Flag choices for newlib on the Vita (decided, see below):
#  THREADSAFE=1       worker threads (scan, covers) each open their own connection or
#                     share one; serialised mutex mode is the safe default. newlib has
#                     pthreads (-lpthread is linked).
#  OMIT_LOAD_EXTENSION  no dlopen on the Vita.
#  OMIT_WAL           WAL needs shared memory / fcntl locks the Vita does not provide.
#  TEMP_STORE=3       temp tables in RAM; the Vita has no usable /tmp.
#  OMIT_SHARED_CACHE  not needed, smaller.
#  DEFAULT_LOCKING_MODE=1  exclusive locking: one process owns the DB, so skip
#                     fcntl() file locks that newlib stubs out (ENOSYS).
#  MAX_MMAP_SIZE=0    no mmap (newlib has none).
# third_party/sqlite/vita_shim/ supplies the one header newlib lacks
# (<sys/ioctl.h>, included by the unix VFS); with it sqlite3.c compiles clean.
# TODO(db.c): if open still fails with SQLITE_IOERR on hardware, open the database
# with sqlite3_open_v2(..., "unix-none") (the built-in no-locking VFS) - unverified on
# a real Vita, nothing links SQLite yet. Do NOT use -DSQLITE_OS_OTHER (needs a custom
# VFS we do not have).
SQLITE_SRC := third_party/sqlite/sqlite3.c
ifneq ($(wildcard $(SQLITE_SRC)),)
CFLAGS     += -Ithird_party/sqlite
SQLITE_FLAGS := -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_OMIT_WAL \
	-DSQLITE_TEMP_STORE=3 -DSQLITE_OMIT_SHARED_CACHE -DSQLITE_DEFAULT_LOCKING_MODE=1 	-DSQLITE_MAX_MMAP_SIZE=0 -Ithird_party/sqlite/vita_shim
EXTRA_OBJS += $(BUILD)/third_party/sqlite3.o
$(BUILD)/third_party/sqlite3.o: $(SQLITE_SRC) Makefile
	@mkdir -p $(dir $@)
	$(CC) $(TP_CFLAGS) $(SQLITE_FLAGS) -c $< -o $@
endif

# cJSON (single file). The include path is added whenever the dir exists.
CJSON_SRC := third_party/cjson/cJSON.c
ifneq ($(wildcard $(CJSON_SRC)),)
CFLAGS     += -Ithird_party/cjson
EXTRA_OBJS += $(BUILD)/third_party/cJSON.o
$(BUILD)/third_party/cJSON.o: $(CJSON_SRC) Makefile
	@mkdir -p $(dir $@)
	$(CC) $(TP_CFLAGS) -Ithird_party/cjson -c $< -o $@
endif

DEPS += $(EXTRA_OBJS:.o=.d)

# ---- Link ------------------------------------------------------------------------
# Static link order: each library before the libraries it depends on.
#   app -> curl/mbedTLS -> mupdf -> archive(+its codecs) -> image codecs
#       -> vita2d(+freetype, png) -> system stubs -> libc
LIBS := $(TLS_LIBS) \
	$(MUPDF_LIBS) \
	$(LIBARCHIVE_LIBS) -larchive -llzma -lbz2 -lzstd \
	-lwebp -lpng16 -lturbojpeg \
	$(MUPDF_DEPS) \
	-lvita2d -lfreetype -lbz2 -lpng16 -lz \
	-lSceDisplay_stub -lSceGxm_stub -lSceSysmodule_stub -lSceCtrl_stub \
	-lSceTouch_stub -lScePgf_stub -lScePvf_stub -lSceCommonDialog_stub \
	-lSceAppUtil_stub -lSceIme_stub -lSceNet_stub -lSceNetCtl_stub \
	-lScePower_stub -lSceAppMgr_stub -lSceRtc_stub -lSceIofilemgr_stub \
	-lSceLibKernel_stub -lSceKernelThreadMgr_stub -lSceProcessmgr_stub \
	-lSceSysmem_stub -lSceKernelModulemgr_stub -lScePromoterUtil_stub \
	-lpthread -lm -lc -lgcc

VPK_FILES := \
	sce_sys/icon0.png \
	sce_sys/livearea/contents/bg.png \
	sce_sys/livearea/contents/startup.png \
	sce_sys/livearea/contents/template.xml \
	assets/cacert.pem \
 sce_sys/package/head.bin \
 docs/updater-attribution.txt \
 docs/GPL-3.0-VitaShell.txt

.PHONY: all clean sizes
all: $(BUILD)/$(VPK_NAME)

$(BUILD)/$(VPK_NAME): $(BUILD)/eboot.bin $(BUILD)/param.sfo $(VPK_FILES)
	$(VITASDK)/bin/vita-pack-vpk -s $(BUILD)/param.sfo -b $(BUILD)/eboot.bin \
		$(foreach f,$(VPK_FILES),-a $(f)=$(f)) $@

# Unsafe homebrew: vita-make-fself WITHOUT -s (decided - needs extended permissions).
$(BUILD)/eboot.bin: $(BUILD)/$(TARGET).velf
	$(VITASDK)/bin/vita-make-fself $< $@

# Makefile is a prerequisite so a TITLE/TITLE_ID edit regenerates the SFO.
# ATTRIBUTE2=12 = extended memory mode.
$(BUILD)/param.sfo: Makefile | $(BUILD)
	$(VITASDK)/bin/vita-mksfoex -s TITLE_ID=$(TITLE_ID) -s APP_VER=$(APP_VER) -d ATTRIBUTE2=12 "$(TITLE)" $@

$(BUILD)/$(TARGET).velf: $(BUILD)/$(TARGET).elf
	$(VITASDK)/bin/vita-elf-create $< $@

$(BUILD)/$(TARGET).elf: $(OBJS) $(EXTRA_OBJS)
	$(CC) -Wl,-q -o $@ $(OBJS) $(EXTRA_OBJS) $(LIBS)

$(BUILD)/%.o: src/%.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $@

sizes: $(BUILD)/$(TARGET).elf
	$(SIZE) $<

clean:
	rm -rf $(BUILD)

-include $(DEPS)
