#include "lib/db_vfs.h"

#include <sqlite3.h>
#include <string.h>

static sqlite3_vfs g_vfs;
static sqlite3_vfs *g_base;

static int full_path(sqlite3_vfs *vfs, const char *path, int cap, char *out)
{
	const char *colon = strchr(path, ':');
	const char *slash = strchr(path, '/');
	(void)vfs;
	/* A mount prefix is absolute on Vita, even without a leading slash.
	 * Unix fullpathname would prepend getcwd and '/' to it, corrupting the path. */
	if (colon && colon != path && (!slash || colon < slash)) {
		size_t n = strlen(path);
		if (cap <= 0 || n >= (size_t)cap) return SQLITE_CANTOPEN;
		memcpy(out, path, n + 1);
		return SQLITE_OK;
	}
	return g_base->xFullPathname(g_base, path, cap, out);
}

const char *db_vita_vfs(void)
{
	if (!g_base) {
		sqlite3_vfs *base = sqlite3_vfs_find("unix-none");
		if (!base) return NULL;
		g_vfs = *base;
		g_vfs.pNext = NULL;
		g_vfs.zName = "noct-vita";
		g_vfs.xFullPathname = full_path;
		if (sqlite3_vfs_register(&g_vfs, 0) != SQLITE_OK) return NULL;
		g_base = base;
	}
	return g_vfs.zName;
}