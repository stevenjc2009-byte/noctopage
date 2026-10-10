#include "net/release.h"
#include "cJSON.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
const char *release_repository(const char *configured)
{
 return configured && configured[0] ? configured : NOCT_UPDATE_REPOSITORY;
}
int release_repo(const char *input,char *out,size_t cap)
{
 if(!input||!out||!cap) return -1;
 const char *s=input;
 if(!strncmp(s,"https://github.com/",19)) s+=19;
 size_t n=strlen(s); while(n&&s[n-1]=='/') n--;
 if(n>4&&!strncmp(s+n-4,".git",4)) n-=4;
 if(!n||n>=cap||n>200) return -1;
 int slash=0; size_t split=0;
 for(size_t i=0;i<n;i++) {
  unsigned char c=(unsigned char)s[i];
  if(c=='/') { slash++; split=i; }
  else if(!isalnum(c)&&c!='_'&&c!='-'&&c!='.') return -1;
 }
 if(slash!=1||!split||split+1==n||s[0]=='.'||s[split+1]=='.') return -1;
 memcpy(out,s,n); out[n]=0; return 0;
}
int release_version(const char *s)
{
 if(!s) return -1;
 if(*s=='v'||*s=='V') s++;
 if(strlen(s)!=5||s[2]!='.') return -1;
 for(int i=0;i<5;i++) if(i!=2&&(s[i]<'0'||s[i]>'9')) return -1;
 return (s[0]-'0')*1000+(s[1]-'0')*100+(s[3]-'0')*10+s[4]-'0';
}
static const char *str(const cJSON *o,const char *k)
{ const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,k); return cJSON_IsString(v)?v->valuestring:""; }
int release_parse(const char *body,size_t len,const char *repo,const char *current,release_t *out,char *err,size_t cap)
{
 memset(out,0,sizeof *out); cJSON *root=cJSON_ParseWithLength(body,len); int rc=-1;
 const char *why="Invalid GitHub release response";
 if(!root||!cJSON_IsObject(root)) goto done;
 const char *tag=str(root,"tag_name"); int ver=release_version(tag),cur=release_version(current);
 if(ver<0||cur<0||cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root,"draft"))||cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root,"prerelease"))) { why="Release needs a stable tag such as v00.05"; goto done; }
 if(ver<=cur) { rc=0; goto done; }
 const cJSON *assets=cJSON_GetObjectItemCaseSensitive(root,"assets"),*a,*chosen=NULL;
 int count=0,exact=0;
 cJSON_ArrayForEach(a,assets) {
  const char *name=str(a,"name"); size_t n=strlen(name);
  if(n<4||strcasecmp(name+n-4,".vpk")||strncasecmp(name,"noctopage",9)) continue;
  count++; if(!strcasecmp(name,"Noctopage.vpk")) { chosen=a; exact++; } else if(!chosen) chosen=a;
 }
 if(!chosen||exact>1||(count>1&&!exact)) { why="Release has no unambiguous Noctopage VPK"; goto done; }
 const char *url=str(chosen,"browser_download_url"); char prefix[300];
 snprintf(prefix,sizeof prefix,"https://github.com/%s/releases/download/",repo);
 if(strncasecmp(url,prefix,strlen(prefix))||strlen(url)>=sizeof out->url) { why="VPK must be a release asset from the configured repository"; goto done; }
 const cJSON *size=cJSON_GetObjectItemCaseSensitive(chosen,"size");
 if(!cJSON_IsNumber(size)||size->valuedouble<1||size->valuedouble>UPDATE_MAX_BYTES||(double)(int64_t)size->valuedouble!=size->valuedouble) { why="Invalid or oversized update package"; goto done; }
 const char *digest=str(chosen,"digest");
 if(strncmp(digest,"sha256:",7)||strlen(digest)!=71) { why="Release asset has no SHA-256 digest; re-upload it to GitHub"; goto done; }
 for(int i=0;i<64;i++) { if(!isxdigit((unsigned char)digest[7+i])) { why="Invalid SHA-256 digest"; goto done; } out->sha256[i]=(char)tolower((unsigned char)digest[7+i]); }
 snprintf(out->version,sizeof out->version,"%s",(*tag=='v'||*tag=='V')?tag+1:tag);
 snprintf(out->url,sizeof out->url,"%s",url); out->size=(int64_t)size->valuedouble; rc=1;
done:
 cJSON_Delete(root); if(rc<0) snprintf(err,cap,"%s",why); return rc;
}
