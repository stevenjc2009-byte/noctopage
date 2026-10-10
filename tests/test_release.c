#include "test.h"
#include "net/release.h"
static int parse(const char *tag,const char *url,const char *digest,const char *size,const char *extra,release_t *r)
{
 char body[4096],err[160]; snprintf(body,sizeof body,"{\"tag_name\":\"%s\",\"assets\":[{\"name\":\"Noctopage.vpk\",\"size\":%s,\"browser_download_url\":\"%s\",\"digest\":\"%s\"}]%s}",tag,size,url,digest,extra);
 return release_parse(body,strlen(body),"owner/NoctoPage","00.04",r,err,sizeof err);
}
int main(void)
{
 char repo[201]; release_t r;
 CHECK_STR(release_repository(NULL),"stevenjc2009-byte/noctopage");
 CHECK_STR(release_repository(""),"stevenjc2009-byte/noctopage");
 CHECK_STR(release_repository("someone/custom"),"someone/custom");
 CHECK_INT(release_repo("https://github.com/owner/NoctoPage.git/",repo,sizeof repo),0); CHECK_STR(repo,"owner/NoctoPage");
 CHECK_INT(release_repo("owner/repo",repo,sizeof repo),0);
 const char *bad[]={"","owner","/repo","owner/","o/r/x","https://evil.com/o/r","o/r?x=y","o/..","user name/repo"};
 for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++) CHECK_INT(release_repo(bad[i],repo,sizeof repo),-1);
 CHECK_INT(release_version("00.04"),4); CHECK_INT(release_version("v01.00"),100); CHECK_INT(release_version("1.2.3"),-1); CHECK_INT(release_version("00.05-beta"),-1);
 const char *url="https://github.com/owner/NoctoPage/releases/download/v00.05/Noctopage.vpk";
 const char *hash="sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
 CHECK_INT(parse("v00.05",url,hash,"100","",&r),1); CHECK_STR(r.version,"00.05"); CHECK_INT(r.size,100); CHECK_INT(strlen(r.sha256),64);
 CHECK_INT(parse("v00.05","https://github.com/OWNER/noctopage/releases/download/v00.05/Noctopage.vpk",hash,"100","",&r),1);
 CHECK_INT(parse("v00.04",url,hash,"100","",&r),0);
 CHECK_INT(parse("v00.03",url,hash,"100","",&r),0);
 CHECK_INT(parse("v00.05",url,hash,"0","",&r),-1);
 CHECK_INT(parse("v00.05",url,hash,"1.5","",&r),-1);
 CHECK_INT(parse("v00.05",url,hash,"999999999","",&r),-1);
 CHECK_INT(parse("v00.05",url,"","100","",&r),-1);
 CHECK_INT(parse("v00.05",url,hash,"100",",\"prerelease\":true",&r),-1);
 CHECK_INT(parse("v00.05","https://evil.com/app.vpk",hash,"100","",&r),-1);
 CHECK_INT(parse("v00.05","https://github.com/owner/other/releases/download/v00.05/Noctopage.vpk",hash,"100","",&r),-1);
 char err[160]; const char *ambiguous="{\"tag_name\":\"v00.05\",\"assets\":[{\"name\":\"Noctopage-a.vpk\"},{\"name\":\"Noctopage-b.vpk\"}]}";
 CHECK_INT(release_parse(ambiguous,strlen(ambiguous),"owner/NoctoPage","00.04",&r,err,sizeof err),-1);
 for(int n=0;n<128;n++) { char b[129]; memset(b,'x',128); b[n]=0; CHECK_INT(release_parse(b,(size_t)n,"owner/NoctoPage","00.04",&r,err,sizeof err),-1); }
 return TEST_DONE("release");
}
