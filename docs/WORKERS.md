# Rules for work-package agents (autonomous run 2026-10-05)

Read `docs/PLAN.md` first: it is the contract (formats, layout, UI values, phases).

## Hard rules

- Write ONLY the files your brief lists as yours. Read anything. If a file you do
  not own needs a change, say so in your report; do not edit it.
- No git commands that write (no add/commit/stash/checkout/reset). No Agent tool.
- Never delete with `rm`/`del` outside your own build dir; never touch `~/vitasdk`.
- Files you create: LF line endings (Python on Windows writes CRLF; use
  `newline="\n"` or fix with `sed -i 's/\r$//'`). C files ≤ 400 lines.
- Scratch files go in the scratchpad dir your brief names, prefixed with your
  package tag.
- C: gnu11, `-Wall -Wextra -Werror`, tabs, the style of `src/core/*.c`.
- "Pure" modules (no `psp2/`, `vita2d.h`) are host-tested. Pure modules do not log;
  they return error codes / messages to the caller.
- Threads: pthreads (newlib has them on the Vita, glibc on the host). Workers decode
  to CPU RGBA buffers; only the main thread creates/frees vita2d textures.

## Building and testing (WSL, from Windows bash)

Always a script FILE run as `MSYS_NO_PATHCONV=1 wsl -e bash /mnt/c/.../script.sh`
(never `bash -lc`: it eats exit codes; WSL /tmp is wiped between calls, use `$HOME`).

- Host tests: `tests/test_<name>.c` is linked with `src/*/<name>.c` automatically.
  Extra sources / flags go in YOUR OWN `tests/deps_<pkg>.mk`:
  ```
  DEPS_<name>   = ../src/core/strutil.c $(BUILD)/sqlite3.o
  CFLAGS_<name> = $(HOSTDEPS_CFLAGS) $(MUPDF_HOST_CFLAGS)
  LIBS_<name>   = $(MUPDF_HOST_LIBS) $(HOSTDEPS_LIBS) -larchive -lturbojpeg -lpng16 -lwebp -lm
  ```
  `HOSTDEPS` = `$HOME/noct-hostdeps` (libarchive, turbojpeg, libpng16, libwebp headers +
  .so, unpacked without sudo). MuPDF host libs: `third_party/mupdf/host/`.
- Build and run only YOUR tests, in YOUR build dir (other agents run at the same time):
  `make -C tests BUILD=build-host-<pkg> build-host-<pkg>/test_<name> && tests/build-host-<pkg>/test_<name>`
  Run from `tests/` or pass fixture paths relative to where you run.
- ARM compile check: add YOUR `tests/arm_<pkg>.list` (lines `src/path.c [extra flags]`);
  check with `$HOME/vitasdk/bin/arm-vita-eabi-gcc -std=gnu11 -Wall -Wextra -Werror -O2
  -Isrc -Ithird_party/mupdf/vita/include -I$HOME/vitasdk/arm-vita-eabi/include/freetype2 -c FILE -o /dev/null`.
- Do NOT run the full `make` VPK build or `tools/run_tests.sh` (shared dirs) unless
  your brief says so. Do NOT start Vita3K unless your brief says so (one at a time).
- Fixtures: `tests/fixtures/` (see `manifest.json`). Make extra fixtures in your test
  at run time (in your build dir), not by editing `tools/make_fixtures.py`.

## Proof

A test that cannot fail proves nothing: for each new rule, break the code once,
watch the test go red, restore. Report the real numbers ("N checks, 0 failed").

## Report (your final message, ≤ 40 lines)

Files written; public API summary (one line per function); test results verbatim
(summary lines); sabotage done; anything you could not do or that someone else must
change.
