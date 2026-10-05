/* Link-time stand-ins for two POSIX calls that sqlite3.c's unix VFS references but the VitaSDK
 * newlib does not provide (checked with arm-vita-eabi-nm over every lib in vitasdk/arm-vita-eabi/lib).
 * Vita files have no owners or symlinks, so these are honest no-ops. */
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

int fchown(int fd, uid_t owner, gid_t group)
{
    (void)fd;
    (void)owner;
    (void)group;
    return 0; /* nothing to change: SQLite only calls this to copy ownership of a new journal */
}

ssize_t readlink(const char *path, char *buf, size_t bufsiz)
{
    (void)path;
    (void)buf;
    (void)bufsiz;
    errno = EINVAL; /* "not a symbolic link" */
    return -1;
}
