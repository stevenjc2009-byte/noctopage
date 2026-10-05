/* libc_stubs.c - symbols the Vita's newlib declares but does not implement.
 *
 * SQLite's unix VFS (third_party/sqlite/sqlite3.c, built with HAVE_FCHOWN and
 * HAVE_READLINK forced on by its own amalgamation header) references fchown()
 * and readlink() in its syscall table, so the link fails without them.
 * They are weak: if a future SDK provides real ones, those win.
 * The Vita has neither file ownership nor symlinks, so "no-op success" and
 * "not a symlink" are the truthful answers. */
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

__attribute__((weak)) int fchown(int fd, uid_t owner, gid_t group)
{
	(void)fd; (void)owner; (void)group;
	return 0;
}

__attribute__((weak)) ssize_t readlink(const char *restrict path, char *restrict buf, size_t len)
{
	(void)path; (void)buf; (void)len;
	errno = EINVAL; /* "not a symbolic link" */
	return -1;
}
