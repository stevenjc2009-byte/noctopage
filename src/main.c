/* Noctopage - PS Vita manga + e-book reader. Boot and main loop. */
#include "app.h"
#include "core/autotest.h"
#include "core/log.h"
#include "core/paths.h"
#include "core/prefs.h"
#include "doc/mdoc.h"
#include "lib/covers.h"
#include "lib/db.h"
#include "lib/library.h"
#include "net/dlq.h"
#include "net/fetch.h"
#include "net/http.h"
#include "ui/font.h"
#include "ui/input.h"
#include "ui/osk.h"
#include "ui/theme.h"

#include <archive.h>
#include <locale.h>
#include <psp2/io/stat.h>
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
	/* libarchive converts entry names to the locale's charset; in newlib's default "C"
	 * locale every non-ASCII name (e.g. Japanese UTF-8 zip entries) comes back NULL. */
	if (!setlocale(LC_CTYPE, "C.UTF-8")) log_printf("setlocale C.UTF-8 failed");
#ifdef NOCT_HAVE_MUPDF
	log_printf("Noctopage boot, libarchive %s, mupdf %s", archive_version_string(), FZ_VERSION);
#else
	log_printf("Noctopage boot, libarchive %s, mupdf not linked", archive_version_string());
#endif

	vita2d_init();
	vita2d_set_vblank_wait(1);
	font_init();
	input_init();
	prefs_load();
	theme_set_dark_grey((int)prefs_get_int("ui", "dark_grey", 0));
	if (mdoc_global_init() != 0)
		log_printf("mdoc: init failed");
	if (db_open(PATH_DB) < 0)
		log_printf("db: open %s: %s", PATH_DB, db_last_error());
	else
		library_scan_start();
	covers_init();
	if (osk_init() < 0) log_printf("osk: init failed");
	if (http_global_init("app0:assets/cacert.pem") < 0) log_printf("http: network unavailable");
	if (fetch_start() < 0) log_printf("fetch: start failed");
	sceIoMkdir(PATH_BOOKS_DIR, 0777); /* downloads go here; fails harmlessly if it exists */
	if (dlq_start(PATH_BOOKS_DIR) < 0) log_printf("dlq: start failed");
	app_init();
	autotest_init();
	log_printf("ready: confirm button is %s", input_confirm_is_cross() ? "Cross" : "Circle");

	input_t in;
	while (!app_quit_requested()) {
		autotest_tick(sceKernelGetProcessTimeWide() / 1000);
		input_poll(&in);
		app_update(&in);
		covers_pump(2);
		if (dlq_pump() > 0) library_scan_start(); /* a download finished: list it */

		vita2d_set_clear_color(theme_bg());
		vita2d_start_drawing();
		vita2d_clear_screen();
		app_draw();
		vita2d_end_drawing();
		vita2d_swap_buffers();
	}

	/* No vita2d_fini / font frees: both hang under Vita3K, and the process is
	 * ending anyway. */
	app_shutdown();
	dlq_stop();
	fetch_stop();
	http_global_cleanup();
	osk_shutdown();
	log_printf("quit");
	sceKernelExitProcess(0);
	return 0;
}
