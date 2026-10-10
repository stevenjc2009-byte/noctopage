#include "lib/update_package.h"
#include "net/release.h"
#include <archive.h>
#include <archive_entry.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef __vita__
#include <mbedtls/sha256.h>
#else
#include <openssl/evp.h>
#endif
static unsigned u16(const unsigned char *s) { return s[0]|(unsigned)s[1]<<8; }
static unsigned u32(const unsigned char *s) { return u16(s)|u16(s+2)<<16; }
int update_path_safe(const char *s)
{
 if(!s||!*s||*s=='/'||strlen(s)>240) return 0;
 const char *p=s;
 while(*p) {
  size_t n=strcspn(p,"/"); if(!n||(n==1&&p[0]=='.')||(n==2&&p[0]=='.'&&p[1]=='.')) return 0;
  for(size_t i=0;i<n;i++) if(p[i]=='\\'||p[i]==':'||(unsigned char)p[i]<32) return 0;
  p+=n; if(*p) p++;
 }
 return 1;
}
int update_sfo(const unsigned char *s,size_t n,char version[16])
{
 if(n<20||memcmp(s,"\0PSF",4)) return -1;
 unsigned keys=u32(s+8),data=u32(s+12),count=u32(s+16); int title=0,ver=0;
 if(count>128||20+(size_t)count*16>n||keys>=n||data>=n) return -1;
 for(unsigned i=0;i<count;i++) {
  const unsigned char *e=s+20+i*16; size_t k=(size_t)keys+u16(e),v=(size_t)data+u32(e+12); unsigned len=u32(e+4),max=u32(e+8);
  if(k>=n||!memchr(s+k,0,n-k)||v>=n||len<1||len>max||max>n-v||!memchr(s+v,0,len)) return -1;
  if(!strcmp((const char *)s+k,"TITLE_ID")) { if(title++||strcmp((const char *)s+v,"NOCT00001")) return -1; }
  if(!strcmp((const char *)s+k,"APP_VER")) { if(ver++||release_version((const char *)s+v)<0||strlen((const char *)s+v)!=5) return -1; snprintf(version,16,"%s",(const char *)s+v); }
 }
 return title==1&&ver==1?0:-1;
}
static int hashfile(const char *path,char hex[65],int64_t *size)
{
 FILE *f=fopen(path,"rb"); if(!f) return -1;
 unsigned char b[32768],digest[32]; size_t n; int rc=0; *size=0;
#ifdef __vita__
 mbedtls_sha256_context ctx; mbedtls_sha256_init(&ctx); rc=mbedtls_sha256_starts(&ctx,0);
#else
 EVP_MD_CTX *ctx=EVP_MD_CTX_new(); if(!ctx) { fclose(f); return -1; } rc=EVP_DigestInit_ex(ctx,EVP_sha256(),NULL)==1?0:-1;
#endif
 while(rc==0&&(n=fread(b,1,sizeof b,f))>0) {
  *size+=(int64_t)n; if(*size>UPDATE_MAX_BYTES) { rc=-1; break; }
#ifdef __vita__
  rc=mbedtls_sha256_update(&ctx,b,n);
#else
  rc=EVP_DigestUpdate(ctx,b,n)==1?0:-1;
#endif
 }
 if(ferror(f)) rc=-1;
#ifdef __vita__
 if(!rc) rc=mbedtls_sha256_finish(&ctx,digest);
 mbedtls_sha256_free(&ctx);
#else
 if(!rc) rc=EVP_DigestFinal_ex(ctx,digest,NULL)==1?0:-1;
 EVP_MD_CTX_free(ctx);
#endif
 fclose(f); if(rc) return -1;
 const char *h="0123456789abcdef"; for(int i=0;i<32;i++) { hex[2*i]=h[digest[i]>>4]; hex[2*i+1]=h[digest[i]&15]; } hex[64]=0; return 0;
}
static struct archive *openzip(const char *path)
{
 struct archive *a=archive_read_new(); if(!a) return NULL;
 archive_read_support_format_zip(a); archive_read_support_filter_none(a);
 if(archive_read_open_filename(a,path,32768)!=ARCHIVE_OK) { archive_read_free(a); return NULL; } return a;
}
static int parents(char *path)
{
 /* Vita device prefix is retained; callers supply the trusted extraction root. */
 char *p=strchr(path,':'); p=p?p+1:path;
 for(;*p;p++) if(*p=='/'&&p!=path) { *p=0; struct stat st; int rc=stat(path,&st); if(rc==0&&!S_ISDIR(st.st_mode)) { *p='/'; return -1; } if(rc<0&&mkdir(path,0777)<0) { *p='/'; return -1; } *p='/'; }
 return 0;
}
int update_package(const char *vpk,const char *dest,const char *sha,int64_t expected,const char *version,char *err,size_t cap)
{
 char digest[65]; int64_t size; const char *why="Update checksum or size does not match GitHub"; int rc=-1;
 if(hashfile(vpk,digest,&size)<0||size!=expected||strcmp(digest,sha)) goto fail;
 struct archive *a=openzip(vpk); if(!a) { why="Update is not a valid VPK ZIP"; goto fail; }
 char (*names)[241]=calloc(2048,241); if(!names) { archive_read_free(a); why="Not enough memory to validate update"; goto fail; }
 struct archive_entry *e; int count=0,eboot=0,sfo=0,head=0,status; int64_t total=0; why="Unsafe or damaged update archive";
 while((status=archive_read_next_header(a,&e))==ARCHIVE_OK) {
  const char *name=archive_entry_pathname(e); mode_t type=archive_entry_filetype(e); int64_t bytes=archive_entry_size(e);
  if(!update_path_safe(name)||count>=2048||archive_entry_symlink(e)||archive_entry_hardlink(e)||(type!=AE_IFREG&&type!=AE_IFDIR)||bytes<0||bytes>64LL*1024*1024||total+bytes>256LL*1024*1024) goto validation;
  for(int i=0;i<count;i++) if(!strcasecmp(names[i],name)) goto validation;
  snprintf(names[count++],241,"%s",name); total+=bytes;
  if(!strcmp(name,"eboot.bin")&&type==AE_IFREG&&bytes>0) eboot=1;
  if(!strcmp(name,"sce_sys/package/head.bin")&&type==AE_IFREG&&bytes>0) head=1;
  if(!strcmp(name,"sce_sys/param.sfo")) {
   unsigned char buf[8192]; char found[16];
   if(type!=AE_IFREG||bytes<=0||bytes>(int64_t)sizeof buf||archive_read_data(a,buf,(size_t)bytes)!=bytes||update_sfo(buf,(size_t)bytes,found)<0||strcmp(found,version)) { why="Update app ID or version does not match NoctoPage"; goto validation; } sfo=1;
  } else if(archive_read_data_skip(a)!=ARCHIVE_OK) goto validation;
 }
 if(status!=ARCHIVE_EOF||!eboot||!sfo||!head) { why="Update is incomplete (needs eboot, param.sfo and package/head.bin)"; goto validation; }
 archive_read_free(a); a=openzip(vpk); if(!a) goto validation;
 why="Could not extract update; check available storage";
 /* The root must be fresh. This prevents stale files entering the promoted package. */
 if(mkdir(dest,0777)<0) goto validation;
 while((status=archive_read_next_header(a,&e))==ARCHIVE_OK) {
  char path[768]; const char *name=archive_entry_pathname(e); int n=snprintf(path,sizeof path,"%s/%s",dest,name);
  if(n<0||(size_t)n>=sizeof path||parents(path)<0) goto validation;
  if(archive_entry_filetype(e)==AE_IFDIR) { struct stat st; if(stat(path,&st)<0&&mkdir(path,0777)<0) goto validation; continue; }
  int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600); if(fd<0) goto validation;
  FILE *f=fdopen(fd,"wb"); if(!f) { close(fd); goto validation; }
  unsigned char b[32768]; la_ssize_t got; int bad=0; int64_t written=0;
  while((got=archive_read_data(a,b,sizeof b))>0) { if(fwrite(b,1,(size_t)got,f)!=(size_t)got) { bad=1; break; } written+=got; }
  if(got<0||written!=archive_entry_size(e)||fflush(f)) bad=1;
  if(fclose(f)) bad=1;
  if(bad) goto validation;
 }
 if(status==ARCHIVE_EOF) rc=0;
validation:
 archive_read_free(a); free(names);
 if(rc==0) return 0;
fail:
 snprintf(err,cap,"%s",why); return -1;
}
