# SQLite (vendored amalgamation)

| | |
|---|---|
| What | SQLite amalgamation (`sqlite3.c`, `sqlite3.h`) for `library.db` (`ux0:data/noctopage/library.db`) |
| Version | 3.53.4 (`SQLITE_VERSION "3.53.4"`, release 2026) |
| Source | https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip (official site, listed on https://www.sqlite.org/download.html) |
| Zip SHA3-256 | `628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e` (measured; equals the hash printed on the download page, which is SHA3-256) |
| Zip SHA-256 (measured) | `1e71ddf93849c6a6ecf58b827c0692073d2dd7ee40196158068f7b29f422e87d` |
| `sqlite3.c` SHA-256 | `b1dd5d74ec7f29055a6684fa06fb3c2f6821c87dd38f9a458dfd2e8a1db28189` |
| `sqlite3.h` SHA-256 | `919e7f2e8ed1d8f56ac17b412b8971c76aa5d1a879752cc6058f75e7d5910e1d` |
| Licence | Public domain (the SQLite "blessing"; no copyright to preserve) |
| AGPL-3.0 compatibility | Public-domain code can be combined with any licence, including AGPL-3.0. Nothing to attribute or disclose. |

`sqlite3.c` and `sqlite3.h` are unmodified. `shell.c` and `sqlite3ext.h` from the zip are not vendored.
Files added by us (not from upstream): `vita_flags.mk`, `vita_stubs.c`, `vita_shim/sys/ioctl.h`.

## Using it from the Makefile

```make
include third_party/sqlite/vita_flags.mk
# SQLITE_VITA_SRCS   = sqlite3.c + vita_stubs.c
# SQLITE_VITA_CFLAGS = every -D / -I flag below; SQLITE_VITA_LIBS = -lpthread -lm
```

Compile those two files with `$(SQLITE_VITA_CFLAGS)` plus the normal `-O2`. `sqlite3.c` is a single
9.5 MB translation unit: expect about 3.5 minutes of `arm-vita-eabi-gcc -O2` under WSL on this
machine, so build it to an object once and let make cache it. It is warning-clean under
`-Wall -Wextra` with the flags above (without `-Wno-unused-function` / `-Wno-unused-parameter`
it prints a few notes about code that the WAL/mmap omissions leave unused).

## What the Vita build needs, and why (measured, not guessed)

VitaSDK newlib is not Linux, so the unix VFS had to be coaxed. Method: compile with
arm-vita-eabi-gcc 15.2, read each error, then `arm-vita-eabi-nm -u` the object and look up every
undefined symbol in all of `vitasdk/arm-vita-eabi/lib/*.a`.

| Problem found | Fix | Flag / file |
|---|---|---|
| `fatal error: sys/ioctl.h: No such file` (newlib for Vita has no such header; sqlite only includes it) | Empty stand-in header, added with `-I` | `vita_shim/sys/ioctl.h`, `-Ithird_party/sqlite/vita_shim` |
| `<sys/mman.h>` is absent too; it is only pulled in for WAL or mmap | Drop both features | `-DSQLITE_OMIT_WAL -DSQLITE_MAX_MMAP_SIZE=0` |
| Undefined at link: `fchown`, `readlink` (declared in the headers, not in any lib) | No-op / "not a symlink" stubs. Vita files have no owners or links | `vita_stubs.c` |
| `pthread_create/join` referenced for sort worker threads | We do not want background sort threads on a Vita | `-DSQLITE_MAX_WORKER_THREADS=0` |
| `pthread_mutex_*` (SQLite's global mutexes) | Live in `libpthread.a`, not `libc.a` | link with `-lpthread` (`SQLITE_VITA_LIBS`) |
| Everything else (`fsync`, `fcntl`, `ftruncate`, `fchmod`, `getpid`, `geteuid`, `lstat`, `utimes`, `nanosleep`) | Present in newlib `libc.a` | nothing |

`__aeabi_*` symbols in the undefined list come from libgcc and resolve at link time.

Other flags and why:

* `-DSQLITE_THREADSAFE=2` (multi-thread): the app uses 2 to 3 threads, each with its own connection and
  never sharing one. Mode 2 skips the per-connection mutex. Use `1` instead if a connection is ever
  shared between threads.
* `-DSQLITE_OMIT_LOAD_EXTENSION` (no `dlopen` on Vita), `-DSQLITE_OMIT_SHARED_CACHE`,
  `-DSQLITE_OMIT_DEPRECATED`, `-DSQLITE_DEFAULT_MEMSTATUS=0` (no global allocator mutex/stat counters; saves time).
* `-DSQLITE_TEMP_STORE=3`: temp tables and sort spills stay in RAM and never touch the memory card.
* `-DSQLITE_DEFAULT_UNIX_VFS=\"unix-dotfile\"`: see the next section.

## Locking: read this before opening the database on device

The default unix VFS locks with `fcntl(F_SETLK)`. Newlib for the Vita probably cannot honour that
(not verifiable on the host), so the default VFS is switched to `unix-dotfile`, which locks by
creating a `library.db.lock` directory with `mkdir`. That needs no fcntl locking. Consequences:

1. **Open with a busy timeout**: `sqlite3_busy_timeout(db, 5000)` on every connection. Dotfile locking has
   no shared read lock, so two threads that touch the DB at the same time will see `SQLITE_BUSY`
   instead of waiting unless a timeout is set.
2. **A crash or power loss leaves a stale `library.db.lock` directory.** At startup, before the first
   `sqlite3_open`, the app should `sceIoRmdir("ux0:data/noctopage/library.db.lock")` (and
   `library.db-journal` is handled by SQLite's own hot-journal rollback). This is safe because only one
   Noctopage instance runs at a time.
3. **No WAL** (it needs shared memory + mmap). Use the default `journal_mode=DELETE` (or `TRUNCATE`).
   Writers block readers for the length of a transaction, so keep write transactions short and
   batch scan results in one transaction.
4. Keep one database file only; do not `ATTACH` other files on the same connection from a second thread.

## Results (this machine, WSL Ubuntu, arm-vita-eabi-gcc 15.2.0)

* `sqlite3.c` with the flags above, `-O2 -Wall -Wextra`: exit 0, zero warnings, 4 min 41 s, object
  `text 604969 / data 7036 / bss 677` bytes (about 0.6 MB of code).
* `vita_stubs.c` with `-Wall -Wextra -Werror`: clean.
* Link smoke test (no run): a 4-line `main` calling `sqlite3_open_v2` + `sqlite3_close`, linked with
  `sqlite3.o vita_stubs.o -lpthread -lm`, exit 0, ELF `text 631088 / data 9212 / bss 285668`.
  Without `vita_stubs.c` the link fails on `fchown` and `readlink`; without `-lpthread` it fails on
  the `pthread_mutex_*` family. Both were observed, not assumed.
* Host (x86-64 gcc, `THREADSAFE=1`): `tests/test_sqlite.c` passes (open `:memory:`, create, insert with
  bound params incl. 64-bit and UTF-8, select, constraint errors, rollback).

Not verified: execution on a Vita or in Vita3K. The locking behaviour above is a reasoned expectation,
not a measurement. The first device/emulator run should open the DB, create a table, insert, restart
the app, read back, and also kill the app mid-transaction to confirm the stale `.lock` cleanup works.
