/* Portable .npb ZIP: typed metadata SQLite and custom cover thumbnails only.
 * Entries are consumed by name, never extracted to an archive-supplied path. */
#include "lib/backup.h"
#include "lib/db.h"
#include "lib/db_vfs.h"
#include "lib/coverimg.h"
#include "core/affinity.h"
#include "core/paths.h"
#include "core/fnv.h"
#include "core/strutil.h"
#include <archive.h>
#include <archive_entry.h>
#include <sqlite3.h>
#include <pthread.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#define WORK PATH_DATA_ROOT "/backup-metadata.tmp"
#define MAX_DB (64ULL*1024*1024)
#define MAX_COVERS 10000
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_thread;
static int g_started;
static backup_state_t g_state;
typedef struct { int restore; char path[512]; } job_t;
static void status(const char *s)
{
	pthread_mutex_lock(&g_mu); snprintf(g_state.message,sizeof g_state.message,"%s",s); pthread_mutex_unlock(&g_mu);
}
static int db_open_file(const char *path, int flags, sqlite3 **out)
{
	if (sqlite3_open_v2(path,out,flags,db_vita_vfs()) == SQLITE_OK) {
		sqlite3_limit(*out,SQLITE_LIMIT_LENGTH,1024*1024);
		sqlite3_limit(*out,SQLITE_LIMIT_SQL_LENGTH,65536);
		sqlite3_exec(*out,"PRAGMA trusted_schema=OFF; PRAGMA cache_size=-2048",NULL,NULL,NULL);
		sqlite3_db_config(*out,SQLITE_DBCONFIG_DEFENSIVE,1,NULL);
		return 0;
	}
	if (*out) sqlite3_close(*out);
	*out = NULL; return -1;
}
static int add_file(struct archive *a, const char *name, const char *path)
{
	struct stat st;
	if (stat(path,&st) || st.st_size < 0) return -1;
	FILE *f = fopen(path,"rb"); if (!f) return -1;
	struct archive_entry *e = archive_entry_new();
	if (!e) { fclose(f); return -1; }
	archive_entry_set_pathname(e,name); archive_entry_set_filetype(e,AE_IFREG);
	archive_entry_set_perm(e,0600); archive_entry_set_size(e,st.st_size);
	int ok = archive_write_header(a,e) == ARCHIVE_OK;
	char buf[32768]; size_t n; uint64_t bytes = 0;
	while (ok && (n = fread(buf,1,sizeof buf,f))) {
		ok = archive_write_data(a,buf,n) == (la_ssize_t)n; bytes += n;
	}
	if (ferror(f) || bytes != (uint64_t)st.st_size) ok = 0;
	fclose(f); archive_entry_free(e); return ok ? 0 : -1;
}
static void remove_work(void)
{
	unlink(WORK); unlink(WORK "-journal"); unlink(WORK "-wal"); unlink(WORK "-shm");
}

static int export_backup(const char *path)
{
 char part[544]; snprintf(part,sizeof part,"%s.part",path); struct stat st;
 if(!stat(path,&st)) { status("A backup with this name already exists"); return -1; }
 remove_work(); unlink(part); status("Saving reading state...");
 int ok=db_reading_snapshot(WORK)==0,books=0;
 sqlite3 *meta=NULL; sqlite3_stmt *q=NULL;
 if(ok && db_open_file(WORK,SQLITE_OPEN_READONLY,&meta)==0 && sqlite3_prepare_v2(meta,"SELECT count(*) FROM reading_state",-1,&q,NULL)==SQLITE_OK && sqlite3_step(q)==SQLITE_ROW) books=sqlite3_column_int(q,0);
 else ok=0;
 sqlite3_finalize(q); if(meta) sqlite3_close(meta);
 struct archive *a=archive_write_new();
 if(!a) ok=0;
 if(ok && (archive_write_set_format_zip(a)!=ARCHIVE_OK || archive_write_zip_set_compression_deflate(a)!=ARCHIVE_OK || archive_write_open_filename(a,part)!=ARCHIVE_OK || add_file(a,"metadata.db",WORK)<0)) ok=0;
 if(a) { if(archive_write_close(a)!=ARCHIVE_OK) ok=0; archive_write_free(a); }
 if(ok && rename(part,path)) ok=0;
 remove_work(); if(!ok) { unlink(part); status("Could not save reading-state backup"); }
 pthread_mutex_lock(&g_mu); g_state.books=books; g_state.covers=0; pthread_mutex_unlock(&g_mu);
 return ok ? 0:-1;
}
static int extract_file(struct archive *a, const char *path, uint64_t limit, uint64_t declared)
{
	if (declared > limit) return -1;
	FILE *f=fopen(path,"wb"); if (!f) return -1;
	char buf[32768]; la_ssize_t n; uint64_t size=0; int ok=1;
	while ((n=archive_read_data(a,buf,sizeof buf)) > 0) {
		size+=(uint64_t)n;
		if (size>limit || fwrite(buf,1,(size_t)n,f)!=(size_t)n) { ok=0; break; }
	}
	if (n<0 || size!=declared) ok=0;
	if (fclose(f)) ok=0;
	if (!ok) unlink(path);
	return ok ? 0 : -1;
}

static int restore_backup(const char *path)
{
 remove_work(); status("Reading selected reading-state backup...");
 struct archive *a=archive_read_new(); if(!a) return -1;
 archive_read_support_format_zip(a); archive_read_support_filter_none(a);
 int ok=archive_read_open_filename(a,path,32768)==ARCHIVE_OK,metadata=0,rc=ARCHIVE_EOF;
 struct archive_entry *entry;
 while(ok && (rc=archive_read_next_header(a,&entry))==ARCHIVE_OK) {
  const char *name=archive_entry_pathname(entry); la_int64_t size=archive_entry_size(entry);
  if(!name || strcmp(name,"metadata.db") || metadata++ || archive_entry_filetype(entry)!=AE_IFREG || size<0 || archive_entry_is_encrypted(entry)>0 || extract_file(a,WORK,MAX_DB,(uint64_t)size)<0) { ok=0; break; }
 }
 if(rc!=ARCHIVE_EOF || metadata!=1) ok=0;
 archive_read_close(a); archive_read_free(a);
 if(ok && db_reading_restore(WORK)<0) ok=0;
 remove_work(); if(!ok) status("Invalid reading-state backup; progress preserved");
 return ok ? 0:-1;
}
static void *worker(void *ud)
{
	job_t *j=ud;
	affinity_use_all_cores();
	int r=j->restore ? restore_backup(j->path) : export_backup(j->path);
	pthread_mutex_lock(&g_mu);
	g_state.result=r; g_state.running=0; g_state.done=1;
	if (!r) snprintf(g_state.message,sizeof g_state.message,j->restore ? "Reading state restored. Your shelf will refresh." : "Backup saved. Copy the .npb file somewhere safe.");
	pthread_mutex_unlock(&g_mu); free(j); return NULL;
}
int backup_start(int restore, const char *path)
{
	backup_state_t current; backup_state(&current); if (current.running) return -1;
	backup_join();
	pthread_mutex_lock(&g_mu); memset(&g_state,0,sizeof g_state); g_state.result=-1;
	str_copy(g_state.path,sizeof g_state.path,path); pthread_mutex_unlock(&g_mu);
	if (paths_mkdir_p(PATH_BACKUPS)<0) { status("Could not create backup folder."); return -1; }
	job_t *j=calloc(1,sizeof *j);
	if (!j) { status("Not enough memory to start backup."); return -1; }
	j->restore=restore; str_copy(j->path,sizeof j->path,path);
	pthread_mutex_lock(&g_mu); memset(&g_state,0,sizeof g_state); g_state.running=1;
	str_copy(g_state.path,sizeof g_state.path,path); str_copy(g_state.message,sizeof g_state.message,"Preparing reading-state backup...");
	pthread_mutex_unlock(&g_mu);
	pthread_attr_t attr; pthread_attr_init(&attr); pthread_attr_setstacksize(&attr,512*1024);
	int rc=pthread_create(&g_thread,&attr,worker,j); pthread_attr_destroy(&attr);
	if (rc) { free(j); pthread_mutex_lock(&g_mu); g_state.running=0; g_state.done=1; g_state.result=-1; str_copy(g_state.message,sizeof g_state.message,"Could not start backup worker."); pthread_mutex_unlock(&g_mu); return -1; }
	g_started=1; return 0;
}
void backup_state(backup_state_t *out)
{
	pthread_mutex_lock(&g_mu); *out=g_state; pthread_mutex_unlock(&g_mu);
}
void backup_join(void)
{
	if (g_started) { pthread_join(g_thread,NULL); g_started=0; }
}
