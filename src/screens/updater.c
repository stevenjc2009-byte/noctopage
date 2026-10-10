#include "screens/updater.h"
#include "screens/screens.h"
#include "app.h"
#include "core/paths.h"
#include "core/prefs.h"
#include "core/log.h"
#include "net/fetch.h"
#include "net/release.h"
#include "lib/update_package.h"
#include "ui/osk.h"
#include "ui/draw.h"
#include "ui/font.h"
#include "ui/text.h"
#include "ui/theme.h"
#include <pthread.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdatomic.h>
#include <psp2/appmgr.h>
#include <psp2/io/devctl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/promoterutil.h>
#include <psp2/sysmodule.h>

#define ROOT PATH_DATA_ROOT "/update"
#define PACKAGE ROOT "/package"
#define VPK ROOT "/update.vpk"
#define RESULT ROOT "/result.txt"
static int g_open,g_fetch,g_ready,g_thread_started,g_done,g_ok,g_install;
static char g_repo[201],g_message[160];
static release_t g_release;
static pthread_t g_thread;
static pthread_mutex_t g_mu=PTHREAD_MUTEX_INITIALIZER;
static _Atomic int g_cancel;
static int g_worker_ok;
static char g_worker_message[160];
static int64_t g_bytes;

/* Only called for our fixed update staging paths; never the library or app directory. */
static int remove_tree(const char *path,int depth)
{
 if(strncmp(path,ROOT "/",strlen(ROOT "/"))||depth>32) return -1;
 struct stat st; if(stat(path,&st)<0) return 0;
 if(!S_ISDIR(st.st_mode)) return remove(path);
 DIR *dir=opendir(path); if(!dir) return -1;
 struct dirent *e; int rc=0;
 while((e=readdir(dir))) {
  if(!strcmp(e->d_name,".")||!strcmp(e->d_name,"..")) continue;
  char child[768]; int n=snprintf(child,sizeof child,"%s/%s",path,e->d_name);
  if(n<0||(size_t)n>=sizeof child||remove_tree(child,depth+1)<0) rc=-1;
 }
 closedir(dir); return rc<0?-1:rmdir(path);
}
static void message(const char *s) { snprintf(g_message,sizeof g_message,"%s",s); }
const char *updater_repository(void)
{
 return release_repository(prefs_get("updates","repository",""));
}
void updater_configure(void)
{
 if(g_thread_started) return;
 char input[256],repo[201];
 if(osk_prompt("GitHub repository URL or owner/repository",updater_repository(),OSK_URL,input,sizeof input)!=1) return;
 if(release_repo(input,repo,sizeof repo)<0) { app_message("Enter a GitHub URL or owner/repository"); return; }
 if(prefs_set_checked("updates","repository",repo)<0) { app_message("Could not save update source"); return; }
 if(g_fetch>0) fetch_forget(g_fetch);
 g_fetch=0; g_ready=g_ok=0; snprintf(g_repo,sizeof g_repo,"%s",repo); message("Repository saved. Choose Check for updates.");
}
static void check(void)
{
 if(g_fetch>0||g_thread_started) return;
 if(release_repo(updater_repository(),g_repo,sizeof g_repo)<0) { message("Set the GitHub update repository in Settings first."); return; }
 char url[400]; snprintf(url,sizeof url,"https://api.github.com/repos/%s/releases/latest",g_repo);
 g_ready=g_ok=0; g_fetch=fetch_get(url,NULL);
 if(g_fetch<=0) { g_fetch=0; message("The network is not available. Try again with Wi-Fi."); }
 else message("Checking the latest GitHub release…");
}
void updater_init(void)
{
 FILE *f=fopen(RESULT,"rb"); if(f) { char result[160]={0}; fread(result,1,sizeof result-1,f); fclose(f); remove(RESULT); if(result[0]) app_message(result); }
 if(prefs_get_int("updates","check_startup",1)) check();
}
void updater_open(void) { g_open=1; if(!g_ready) check(); }
int updater_busy(void) { return g_open; }
int updater_install_requested(void) { return g_install; }
void updater_tick(void)
{
 if(!g_fetch) return;
 fetch_result_t r; fetch_state_t st=fetch_take(g_fetch,&r); if(st==FETCH_PENDING) return;
 g_fetch=0;
 if(st!=FETCH_DONE) {
  if(r.doc.status==404) message("Repository or published release not found on GitHub.");
  else if(r.doc.status==403||r.doc.status==429) message("GitHub rate limit reached. Try again later.");
  else message(r.err[0]?r.err:"Could not check GitHub releases.");
 } else {
  char err[160]; int rc=release_parse(r.doc.data,r.doc.len,g_repo,NOCT_VERSION,&g_release,err,sizeof err);
  if(rc<0) message(err);
  else if(rc==0) message("NoctoPage is up to date (" NOCT_VERSION ").");
  else { char msg[160]; snprintf(msg,sizeof msg,"NoctoPage %s is available (%.1f MB).",g_release.version,(double)g_release.size/(1024*1024)); message(msg); g_ready=1; if(!g_open) app_message("NoctoPage update available — open Settings"); }
 }
 fetch_result_free(&r);
}
static int progress(void *ud,int64_t now,int64_t total)
{
 (void)ud; (void)total; pthread_mutex_lock(&g_mu); g_bytes=now; pthread_mutex_unlock(&g_mu);
 return g_cancel||now>g_release.size||now>UPDATE_MAX_BYTES;
}
static void *download_worker(void *ud)
{
 (void)ud; char err[160]={0}; int ok=0;
 if(paths_mkdir_p(ROOT)<0||remove_tree(PACKAGE,0)<0||remove_tree(VPK,0)<0) snprintf(err,sizeof err,"Could not prepare update storage.");
 else if(http_download(g_release.url,NULL,VPK,0,progress,NULL,NULL,0,err,sizeof err)==0) {
  if(!g_cancel&&update_package(VPK,PACKAGE,g_release.sha256,g_release.size,g_release.version,err,sizeof err)==0) ok=1;
 }
 if(g_cancel) { ok=0; snprintf(err,sizeof err,"Update cancelled."); }
 if(!ok) { remove_tree(PACKAGE,0); remove(VPK); }
 pthread_mutex_lock(&g_mu); g_worker_ok=ok; g_done=1;
 snprintf(g_worker_message,sizeof g_worker_message,"%s",ok?"Update verified. Confirm to install and restart.":err[0]?err:"Update download failed.");
 pthread_mutex_unlock(&g_mu); return NULL;
}
static void download_start(void)
{
 /* Leave room for the compressed VPK, its extracted app and the promoter's copy. */
 SceIoDevInfo space={0};
 int status=sceIoDevctl("ux0:",0x3001,NULL,0,&space,sizeof space);
 if(status<0||space.free_size<g_release.size*4+32LL*1024*1024) { message("Not enough free storage, or storage could not be checked."); return; }
 g_done=g_ok=0; g_bytes=0; g_cancel=0; message("Downloading and verifying update…");
 pthread_attr_t attr; pthread_attr_init(&attr); pthread_attr_setstacksize(&attr,256*1024);
 int rc=pthread_create(&g_thread,&attr,download_worker,NULL); pthread_attr_destroy(&attr);
 if(rc) message("Could not start the update worker."); else g_thread_started=1;
}
void updater_update(const input_t *in)
{
 updater_tick();
 if(g_thread_started) {
  pthread_mutex_lock(&g_mu); int done=g_done; pthread_mutex_unlock(&g_mu);
  if(!done) { if(in->pressed&IN_BACK) g_cancel=1; return; }
  pthread_join(g_thread,NULL); g_thread_started=0; g_ok=g_worker_ok; message(g_worker_message);
 }
 if(in->pressed&IN_BACK) { g_open=0; return; }
 if(in->pressed&IN_TRIANGLE) { if(!g_ok) check(); return; }
 if((in->pressed&IN_CONFIRM) || (in->touch_began && in->began_x>=221 && in->began_x<=550 && in->began_y>=302 && in->began_y<=344)) {
  if(g_ok) g_install=1;
  else if(g_ready) download_start();
  else if(!g_fetch) check();
 }
}
void updater_draw(void)
{
 font_draw(221,18,THEME_FONT,FONT_PX_TITLE,"GitHub auto update");
 char lines[5][TEXT_LINE_MAX],msg[160]; int64_t bytes;
 pthread_mutex_lock(&g_mu); snprintf(msg,sizeof msg,"%s",g_message); bytes=g_bytes; pthread_mutex_unlock(&g_mu);
 font_draw(221,74,THEME_SUBFONT,FONT_PX_LABEL,"Installed: " NOCT_VERSION);
 text_ellipsize(FONT_PX_LABEL,g_repo[0]?g_repo:"No repository configured",720,lines[0]); font_draw(221,106,THEME_SUBFONT,FONT_PX_LABEL,lines[0]);
 int n=text_wrap(FONT_PX_TITLE,msg,680,5,lines); for(int i=0;i<n;i++) font_draw(221,166+i*30,THEME_FONT,FONT_PX_TITLE,lines[i]);
 if(!g_thread_started && !g_fetch) {
  draw_rrect(221,302,330,42,5,THEME_PANEL);
  font_draw(235,312,THEME_SELECTOR,FONT_PX_TITLE,g_ok?"Install & restart":g_ready?"Download update":"Check again");
 }
 if(g_thread_started&&g_release.size>0) {
  float fraction=(float)bytes/(float)g_release.size; if(fraction>1) fraction=1;
  draw_rect(221,348,700,8,THEME_PANEL); draw_rect(221,348,700*fraction,8,THEME_SELECTOR);
  char s[80]; snprintf(s,sizeof s,"%.1f / %.1f MB",(double)bytes/1048576,(double)g_release.size/1048576); font_draw(221,372,THEME_SUBFONT,FONT_PX_LABEL,s);
 }
 font_draw(221,440,THEME_SUBFONT,FONT_PX_LABEL,"Updates replace the app; your comics and reading state remain on the memory card.");
}
int updater_hints(hint_t *out,int max)
{
 hint_t h[]={{GLYPH_BACK,g_thread_started?"Cancel download":"Back"},{GLYPH_CONFIRM,g_ok?"Install & restart":g_ready?"Download update":"Check again"},{GLYPH_TRIANGLE,"Refresh"}};
 return screen_copy_hints(h,g_thread_started||g_fetch?1:g_ok?2:3,out,max);
}
void updater_stop(void)
{
 if(g_fetch) fetch_forget(g_fetch);
 g_fetch=0;
 if(g_thread_started) { g_cancel=1; pthread_join(g_thread,NULL); g_thread_started=0; }
}
void updater_install(void)
{
 if(!g_install||!g_ok) return;
 /* PAF load arguments follow VitaShell's documented package installer pattern. */
 uint32_t args[]={0x180000,(uint32_t)-1,(uint32_t)-1,1,(uint32_t)-1,(uint32_t)-1};
 int paf_result=-1; SceSysmoduleOpt opt={.flags=sizeof opt,.result=&paf_result,.unused={-1,-1}};
 int rc=sceSysmoduleLoadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF,sizeof args,args,&opt);
 int paf=rc>=0&&paf_result>=0,promoter=0,init=0;
 if(paf) { rc=sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL); promoter=rc>=0; }
 if(promoter) { rc=scePromoterUtilityInit(); init=rc>=0; }
 if(init) {
  rc=scePromoterUtilityPromotePkgWithRif(PACKAGE,1);
  if(rc>=0) { int result=-1; int call=scePromoterUtilityGetResult(&result); rc=call<0?call:result; }
  scePromoterUtilityExit();
 }
 if(promoter) sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
 if(paf) { SceSysmoduleOpt unload={.flags=sizeof unload}; sceSysmoduleUnloadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF,0,NULL,&unload); }
 char result[160]; if(rc<0||!init) snprintf(result,sizeof result,"Update install failed (%08X). Open Settings to retry.",(unsigned)rc);
 else { snprintf(result,sizeof result,"NoctoPage %s installed successfully.",g_release.version); remove(VPK); }
 FILE *f=fopen(RESULT,"wb"); if(f) { fputs(result,f); fclose(f); }
 log_printf("%s",result);
 /* Reload from the app mount after promotion; report any launch error persistently. */
 int launch=sceAppMgrLoadExec("app0:eboot.bin",NULL,NULL);
 log_printf("update: restart returned %08X; reopen NoctoPage",(unsigned)launch);
}
