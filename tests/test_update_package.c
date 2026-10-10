#include "test.h"
#include "lib/update_package.h"
#include <archive.h>
#include <archive_entry.h>
#include <openssl/evp.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
static void put32(unsigned char *p,unsigned v) { for(int i=0;i<4;i++) p[i]=(unsigned char)(v>>(i*8)); }
static void sfo(unsigned char b[96],const char *title)
{
 memset(b,0,96); memcpy(b,"\0PSF",4); put32(b+4,0x101); put32(b+8,52); put32(b+12,72); put32(b+16,2);
 put32(b+24,10); put32(b+28,16); put32(b+32,0);
 b[36]=9; put32(b+40,6); put32(b+44,8); put32(b+48,16);
 memcpy(b+52,"TITLE_ID\0APP_VER\0",17); memcpy(b+72,title,10); memcpy(b+88,"00.05",6);
}
static void entry(struct archive *a,const char *name,const void *data,size_t n,int type)
{
 struct archive_entry *e=archive_entry_new(); archive_entry_set_pathname(e,name); archive_entry_set_size(e,n); archive_entry_set_filetype(e,type); archive_entry_set_perm(e,0644);
 if(type==AE_IFLNK) archive_entry_set_symlink(e,"../../outside");
 CHECK_INT(archive_write_header(a,e),ARCHIVE_OK); if(n) CHECK_INT(archive_write_data(a,data,n),n); archive_entry_free(e);
}
static void zip(const char *path,int bad)
{
 unsigned char b[96]; sfo(b,bad==1?"EVIL00001":"NOCT00001");
 struct archive *a=archive_write_new(); archive_write_set_format_zip(a); CHECK_INT(archive_write_open_filename(a,path),ARCHIVE_OK);
 entry(a,"eboot.bin","elf",3,AE_IFREG); entry(a,"sce_sys/param.sfo",b,sizeof b,AE_IFREG);
 if(bad!=5) entry(a,"sce_sys/package/head.bin","head",4,AE_IFREG);
 if(bad==2) entry(a,"../outside","bad",3,AE_IFREG);
 if(bad==3) entry(a,"eboot.bin","duplicate",9,AE_IFREG);
 if(bad==4) entry(a,"link",NULL,0,AE_IFLNK);
 CHECK_INT(archive_write_close(a),ARCHIVE_OK); archive_write_free(a);
}
static int apply(const char *path,const char *dest,int corrupt)
{
 FILE *f=fopen(path,"rb"); CHECK(f!=NULL); if(!f) return -1;
 EVP_MD_CTX *ctx=EVP_MD_CTX_new(); EVP_DigestInit_ex(ctx,EVP_sha256(),NULL); unsigned char buf[4096],hash[32]; size_t n; int64_t size=0;
 while((n=fread(buf,1,sizeof buf,f))) { EVP_DigestUpdate(ctx,buf,n); size+=n; } fclose(f); EVP_DigestFinal_ex(ctx,hash,NULL); EVP_MD_CTX_free(ctx);
 char hex[65],err[160]; for(int i=0;i<32;i++) snprintf(hex+2*i,3,"%02x",hash[i]); if(corrupt) hex[0]=hex[0]=='0'?'1':'0';
 return update_package(path,dest,hex,size,"00.05",err,sizeof err);
}
int main(void)
{
 const char *bad[]={"","/absolute","../escape","a/../escape","a/./b","ux0:app/x","a\\b","a//b"};
 for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++) CHECK(!update_path_safe(bad[i]));
 CHECK(update_path_safe("sce_sys/package/head.bin")); CHECK(update_path_safe("assets/"));
 unsigned char b[96]; char v[16]; sfo(b,"NOCT00001"); CHECK_INT(update_sfo(b,sizeof b,v),0); CHECK_STR(v,"00.05");
 for(size_t n=0;n<94;n++) CHECK_INT(update_sfo(b,n,v),-1);
 for(int n=0;n<100;n++) { unsigned char x[96]; memcpy(x,b,96); put32(x+8,(unsigned)(100+n)); CHECK_INT(update_sfo(x,96,v),-1); }
 char root[]="/tmp/noct-update-XXXXXX"; CHECK(mkdtemp(root)!=NULL); char path[256],dest[256]; snprintf(path,sizeof path,"%s/update.vpk",root); snprintf(dest,sizeof dest,"%s/package",root);
 zip(path,0); CHECK_INT(apply(path,dest,1),-1); struct stat st; CHECK_INT(stat(dest,&st),-1);
 CHECK_INT(apply(path,dest,0),0); CHECK_INT(apply(path,dest,0),-1); /* fresh root only */
 char file[512]; snprintf(file,sizeof file,"%s/eboot.bin",dest); CHECK_INT(stat(file,&st),0); CHECK_INT(st.st_size,3); unlink(file);
 snprintf(file,sizeof file,"%s/sce_sys/param.sfo",dest); unlink(file); snprintf(file,sizeof file,"%s/sce_sys/package/head.bin",dest); unlink(file); snprintf(file,sizeof file,"%s/sce_sys/package",dest); rmdir(file); snprintf(file,sizeof file,"%s/sce_sys",dest); rmdir(file); rmdir(dest);
 for(int i=1;i<=5;i++) { zip(path,i); CHECK_INT(apply(path,dest,0),-1); CHECK_INT(stat(dest,&st),-1); }
 unlink(path); rmdir(root);
 return TEST_DONE("update_package");
}
