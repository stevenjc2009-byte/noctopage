#include "lib/scan_vita.h"

#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/rtc.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

const char *const SCAN_VITA_DEVICES[] = { "ux0:", "uma0:", "ur0:", "imc0:", "grw0:", "xmc0:", NULL };

typedef struct {
	SceUID fd;
	SceIoDirent ent; /* ~400 bytes: kept off the worker stack */
} vdir_t;

static void *v_open(void *fsuser, const char *path)
{
	(void)fsuser;
	SceUID fd = sceIoDopen(path);
	if (fd < 0) return NULL;
	vdir_t *d = malloc(sizeof *d);
	if (!d) {
		sceIoDclose(fd);
		return NULL;
	}
	d->fd = fd;
	return d;
}

static int v_read(void *fsuser, void *dir, scan_dirent_t *out)
{
	(void)fsuser;
	vdir_t *d = dir;
	memset(&d->ent, 0, sizeof d->ent);
	int r = sceIoDread(d->fd, &d->ent);
	if (r <= 0) return r < 0 ? -1 : 0;
	strncpy(out->name, d->ent.d_name, sizeof out->name - 1);
	out->name[sizeof out->name - 1] = '\0';
	out->is_dir = SCE_S_ISDIR(d->ent.d_stat.st_mode);
	out->size = d->ent.d_stat.st_size < 0 ? 0 : (uint64_t)d->ent.d_stat.st_size;
	time_t t = 0;
	if (sceRtcGetTime_t(&d->ent.d_stat.st_mtime, &t) < 0) t = 0;
	out->mtime = (int64_t)t;
	return 1;
}

static void v_close(void *fsuser, void *dir)
{
	(void)fsuser;
	vdir_t *d = dir;
	sceIoDclose(d->fd);
	free(d);
}

static const scan_fs_t g_fs = { v_open, v_read, v_close, NULL };

const scan_fs_t *scan_vita_fs(void) { return &g_fs; }

int scan_vita_device_present(const char *dev)
{
	/* Dopen, not Getstat: Getstat on a bare device root fails on some firmware. */
	SceUID fd = sceIoDopen(dev);
	if (fd < 0) return 0;
	sceIoDclose(fd);
	return 1;
}
