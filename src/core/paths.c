#include "core/paths.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <string.h>

int paths_mkdir_p(const char *path)
{
	char buf[512];
	size_t len = strlen(path);
	if (len == 0 || len >= sizeof buf)
		return -1;
	memcpy(buf, path, len + 1);

	/* Skip the "ux0:" device prefix so we never try to mkdir the device itself. */
	char *p = strchr(buf, ':');
	p = p ? p + 1 : buf;
	while (*p == '/')
		p++;

	for (; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		sceIoMkdir(buf, 0777); /* already-exists is not an error here */
		*p = '/';
	}
	int r = sceIoMkdir(buf, 0777);
	/* SCE_ERROR_ERRNO_EEXIST = 0x80010011 */
	if (r < 0 && (unsigned)r != 0x80010011u)
		return r;
	return 0;
}

void paths_init(void)
{
	paths_mkdir_p(PATH_DATA_ROOT);
	paths_mkdir_p(PATH_THUMBS_DIR);
	paths_mkdir_p(PATH_BOOKS_DIR);
}
