/* Noctopage - PS Vita manga + e-book reader. Boot and main loop. */
#include "app.h"
#include "core/autotest.h"
#include "core/log.h"
#include "core/paths.h"
#include "ui/font.h"
#include "ui/input.h"
#include "ui/theme.h"

#include <archive.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#ifdef NOCT_HAVE_MUPDF
#include <mupdf/fitz/version.h>
#endif

/* newlib heap: decoded pages, MuPDF and the page cache live here (extended memory
 * mode, ATTRIBUTE2=12). */
int _newlib_heap_size_user = 192 * 1024 * 1024;

int main(void)
{
	paths_init();
	log_reset();
#ifdef NOCT_HAVE_MUPDF
	log_printf("Noctopage boot, libarchive %s, mupdf %s", archive_version_string(), FZ_VERSION);
#else
	log_printf("Noctopage boot, libarchive %s, mupdf not linked", archive_version_string());
#endif

	vita2d_init();
	vita2d_set_vblank_wait(1);
	font_init();
	input_init();
	app_init();
	autotest_init();
	log_printf("ready: confirm button is %s", input_confirm_is_cross() ? "Cross" : "Circle");

	input_t in;
	while (!app_quit_requested()) {
		autotest_tick(sceKernelGetProcessTimeWide() / 1000);
		input_poll(&in);
		app_update(&in);

		vita2d_set_clear_color(theme_bg());
		vita2d_start_drawing();
		vita2d_clear_screen();
		app_draw();
		vita2d_end_drawing();
		vita2d_swap_buffers();
	}

	/* No vita2d_fini / font frees: both hang under Vita3K, and the process is
	 * ending anyway. */
	log_printf("quit");
	sceKernelExitProcess(0);
	return 0;
}
