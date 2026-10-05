#include "core/log.h"

#include "core/paths.h"

#include <psp2/kernel/processmgr.h>
#include <stdarg.h>
#include <stdio.h>

void log_reset(void)
{
	paths_mkdir_p(PATH_DATA_ROOT);
	FILE *f = fopen(PATH_LOG, "w");
	if (f)
		fclose(f);
}

void log_printf(const char *fmt, ...)
{
	static int dirs_made;
	if (!dirs_made) {
		paths_mkdir_p(PATH_DATA_ROOT);
		dirs_made = 1;
	}

	FILE *f = fopen(PATH_LOG, "a");
	if (!f)
		return;
	double secs = (double)sceKernelGetProcessTimeWide() / 1000000.0;
	fprintf(f, "[%8.3f] ", secs);
	va_list ap;
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	fputc('\n', f);
	fclose(f);
}
