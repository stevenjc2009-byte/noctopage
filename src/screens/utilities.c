#include "screens/utilities.h"
#include "screens/screens.h"
#include "lib/backup.h"
#include "lib/covers.h"
#include "lib/library.h"
#include "net/wifi_import.h"
#include "core/prefs.h"
#include "core/strutil.h"
#include "ui/menu.h"
#include "ui/listview.h"
#include "ui/font.h"
#include "ui/draw.h"
#include "ui/text.h"
#include "ui/theme.h"
#include <psp2/io/dirent.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

enum { U_NONE, U_WIFI, U_MENU, U_PICK, U_CONFIRM, U_JOB, U_DONE };
static int g_mode, g_restore, g_settled;
static char g_names[128][256], g_selected[512];
static lv_item_t g_items[128];
static backup_state_t g_backup;
static int g_files;
static void restore_list(void)
{
	g_files=0;
	SceUID d=sceIoDopen(PATH_BACKUPS);
	if (d>=0) {
		SceIoDirent entry; memset(&entry,0,sizeof entry);
		while (g_files<128 && sceIoDread(d,&entry)>0) {
			const char *dot=strrchr(entry.d_name,'.');
			if (dot && !strcasecmp(dot,".npb") && SCE_S_ISREG(entry.d_stat.st_mode)) {
				str_copy(g_names[g_files],sizeof g_names[g_files],entry.d_name);
				g_items[g_files]=(lv_item_t){g_names[g_files],NULL,0}; g_files++;
			}
			memset(&entry,0,sizeof entry);
		}
		sceIoDclose(d);
	}
	g_mode=U_PICK;
	listview_open("Restore backup",g_items,g_files,0,"No backups. Upload a .npb file using Wi-Fi or VitaShell.",0);
}
void utilities_wifi(void)
{
	utilities_close(); g_mode=U_WIFI; wifi_import_start();
}
void utilities_backup(void)
{
	utilities_close();
	static const char *const choices[]={"Create reading-state backup","Restore a saved backup"};
	g_mode=U_MENU; menu_open("Reading-state backup & restore",choices,2);
}
static uint64_t g_sleep_last;

/* Sleep timer (Settings, minutes of no input): suspends the Vita. Skipped while a
 * backup or Wi-Fi uploads hold the device awake. Reading position is safe: both
 * readers persist it within seconds, and suspend keeps RAM anyway. */
void utilities_sleep_tick(uint64_t now_ms, int active)
{
	long minutes = prefs_get_int("ui", "sleep_min", 0);

	if (!g_sleep_last) g_sleep_last = now_ms;
	if (minutes <= 0 || utilities_keep_awake()) { g_sleep_last = now_ms; return; }
	if (active) { g_sleep_last = now_ms; return; }
	if (now_ms - g_sleep_last >= (uint64_t)minutes * 60000u) {
		g_sleep_last = now_ms; /* re-arms if the request is refused */
		scePowerRequestSuspend();
	}
}

int utilities_busy(void) { return g_mode!=U_NONE; }
int utilities_working(void) { return g_mode==U_JOB; }
int utilities_keep_awake(void) { return g_mode==U_JOB || g_mode==U_WIFI; }
static void start_job(int restore, const char *path)
{
	/* Freeze scanner metadata and cancel stale cover writes before restoring. */
	library_scan_stop(); covers_pause(1); covers_reset();
	g_restore=restore; g_settled=0;
	g_mode=U_JOB;
	memset(&g_backup,0,sizeof g_backup);
	str_copy(g_backup.path,sizeof g_backup.path,path);
	str_copy(g_backup.message,sizeof g_backup.message,"Preparing library backup...");
	if (backup_start(restore,path)<0) {
		backup_state(&g_backup);
		g_backup.running=0; g_backup.done=1; g_backup.result=-1;
		if (!g_backup.message[0]) str_copy(g_backup.message,sizeof g_backup.message,"Could not start backup. Check free storage.");
		covers_pause(0); library_scan_start(); g_settled=1; g_mode=U_DONE;
	}
}
static void settle(void)
{
	if (g_settled) return;
	backup_join();
	if (g_restore) covers_reset();
	covers_pause(0); library_scan_start(); g_settled=1;
}
void utilities_update(const input_t *in)
{
	if (g_mode==U_NONE) return;
	if (g_mode==U_WIFI) { if (in->pressed&IN_BACK) utilities_close(); return; }
	if (g_mode==U_JOB) {
		backup_state(&g_backup);
		if (!g_backup.running) { settle(); g_mode=U_DONE; }
		return;
	}
	if (g_mode==U_DONE) { if (in->pressed&(IN_BACK|IN_CONFIRM)) g_mode=U_NONE; return; }
	if (g_mode==U_PICK) {
		int index=0;
		lv_action_t a=listview_update(in,&index);
		if (a==LV_PICK && index>=0 && index<g_files) {
			snprintf(g_selected,sizeof g_selected,"%s/%s",PATH_BACKUPS,g_names[index]);
			static const char *const choices[]={"Restore saved reading progress","Cancel"};
			g_mode=U_CONFIRM; menu_open("Replace progress for matching volumes?",choices,2); menu_select(1);
		} else if (!listview_is_open()) g_mode=U_NONE;
		return;
	}
	int mode=g_mode, k=menu_update(in);
	if (menu_is_open()) return;
	g_mode=U_NONE;
	if (k<0) return;
	if (mode==U_MENU && k==0) {
		char path[512]; snprintf(path,sizeof path,PATH_BACKUPS "/library-%lld-%llu.npb",(long long)time(NULL),
		                        (unsigned long long)sceKernelGetProcessTimeWide());
		start_job(0,path);
	} else if (mode==U_MENU && k==1) restore_list();
	else if (mode==U_CONFIRM && k==0) start_job(1,g_selected);
}
static void line(int y, unsigned color, int size, const char *s)
{
	char text[TEXT_LINE_MAX]; text_ellipsize(size,s,820,text);
	font_draw(70,y,color,size,text);
}
void utilities_draw(uint64_t now)
{
	if (!g_mode) return;
	if (g_mode==U_MENU || g_mode==U_CONFIRM) {
		menu_draw(now);
		if (g_mode==U_CONFIRM) { draw_rect(50,462,860,38,THEME_PANEL); line(472,THEME_SUBFONT,FONT_PX_LABEL,g_selected); }
		return;
	}
	if (g_mode==U_PICK) { listview_draw(now); return; }
	draw_rect(0,0,960,521,theme_bg()); draw_rect(40,40,880,430,THEME_PANEL);
	if (g_mode==U_WIFI) {
		wifi_import_state_t s; wifi_import_state(&s);
		line(70,THEME_FONT,FONT_PX_TITLE,"Wi-Fi book uploads");
		line(112,THEME_SUBFONT,FONT_PX_LABEL,"Open this address in a phone or PC browser on the same Wi-Fi:");
		line(153,THEME_SELECTOR,32,s.url[0] ? s.url : "Wi-Fi unavailable");
		char code[96]; snprintf(code,sizeof code,"Pairing code: %s",s.code[0] ? s.code : "------");
		line(207,THEME_FONT,FONT_PX_TITLE,code);
		line(254,THEME_SUBFONT,FONT_PX_LABEL,"Choose a series folder in the browser, or leave it blank for ux0:books.");
		line(291,THEME_FONT,FONT_PX_LABEL,s.file);
		char progress[96]; snprintf(progress,sizeof progress,"%.1f / %.1f MB  |  %d files received",s.bytes/1048576.0,s.total/1048576.0,s.imported);
		line(323,THEME_SUBFONT,FONT_PX_LABEL,progress);
		draw_rect(70,358,820,8,theme_bg());
		if (s.total) draw_rect(70,358,(int)(820.0*s.bytes/s.total),8,THEME_SELECTOR);
		line(391,THEME_FONT,FONT_PX_LABEL,s.message);
		line(434,THEME_SUBFONT,FONT_PX_LABEL,"Back stops the server. Keep this screen open while uploading.");
	} else {
		line(70,THEME_FONT,FONT_PX_TITLE,g_restore ? "Restore library" : "Back up library");
		line(130,g_mode==U_DONE && g_backup.result<0 ? THEME_SUBFONT : THEME_SELECTOR,FONT_PX_TITLE,g_backup.message);
		char counts[96]; snprintf(counts,sizeof counts,"%d reading records  |  %d comic files",g_backup.books,g_backup.covers);
		line(188,THEME_FONT,FONT_PX_LABEL,counts);
		line(233,THEME_SUBFONT,FONT_PX_LABEL,"Includes saved pages, reading progress and reading statistics only.");
		line(272,THEME_SUBFONT,FONT_PX_LABEL,"No comic files, covers or automatic backups.");
		line(322,THEME_FONT,FONT_PX_LABEL,g_backup.path);
		line(378,THEME_SUBFONT,FONT_PX_LABEL,g_mode==U_JOB ? "Keep the app open until this job finishes." : "Backups are in ux0:data/noctopage/backups.");
		line(426,THEME_SUBFONT,FONT_PX_LABEL,g_restore ? "Restore updates matching volumes already in your library." : "Copy the .npb file to your PC using VitaShell.");
	}
}
int utilities_hints(hint_t *out, int max)
{
	if (g_mode==U_MENU || g_mode==U_CONFIRM) return menu_hints(out,max);
	if (g_mode==U_PICK) return listview_hints(out,max);
	if (g_mode==U_JOB) return 0;
	hint_t h[]={{GLYPH_BACK,g_mode==U_WIFI ? "Stop uploads" : "Close"}};
	return screen_copy_hints(h,1,out,max);
}
void utilities_close(void)
{
	wifi_import_stop();
	if (g_mode==U_JOB) { backup_join(); backup_state(&g_backup); settle(); }
	g_mode=U_NONE;
}
