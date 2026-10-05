# SQLite on the Vita (VitaSDK newlib). Include from the main Makefile:
#     include third_party/sqlite/vita_flags.mk
#     SQLITE_OBJ = $(SQLITE_VITA_SRCS:.c=.o)
# and compile those two files with $(SQLITE_VITA_CFLAGS) in addition to the usual -O2 etc.
# Rationale for every flag is in third_party/sqlite/README.md.

SQLITE_VITA_DIR := $(dir $(lastword $(MAKEFILE_LIST)))

SQLITE_VITA_SRCS := $(SQLITE_VITA_DIR)sqlite3.c $(SQLITE_VITA_DIR)vita_stubs.c

# Link with these (sqlite's mutexes live in libpthread, not libc).
SQLITE_VITA_LIBS := -lpthread -lm

# -I vita_shim: newlib has no <sys/ioctl.h>; the shim directory holds an empty one.
SQLITE_VITA_CFLAGS := \
	-I$(SQLITE_VITA_DIR)vita_shim \
	-DSQLITE_THREADSAFE=2 \
	-DSQLITE_MAX_WORKER_THREADS=0 \
	-DSQLITE_OMIT_LOAD_EXTENSION \
	-DSQLITE_OMIT_WAL \
	-DSQLITE_MAX_MMAP_SIZE=0 \
	-DSQLITE_OMIT_SHARED_CACHE \
	-DSQLITE_DEFAULT_UNIX_VFS=\"unix-dotfile\" \
	-DSQLITE_TEMP_STORE=3 \
	-DSQLITE_DEFAULT_MEMSTATUS=0 \
	-DSQLITE_OMIT_DEPRECATED \
	-Wno-unused-function -Wno-unused-parameter
