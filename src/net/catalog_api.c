#include "net/catalog_api.h"
#include "net/opds_priv.h"
#include "net/url.h"
#include "cJSON.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *str(const cJSON *o, const char *key)
{
 const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);
 if(cJSON_IsArray(v)) v=cJSON_GetArrayItem(v,0);
 return cJSON_IsString(v)&&v->valuestring?v->valuestring:"";
}
static int origin(const char *s, char *out, size_t cap)
{
 if(!s) return -1;
 int n=!strncasecmp(s,"https://",8)?8:!strncasecmp(s,"http://",7)?7:0;
 if(!n) return -1;
 const char *end=s+n+strcspn(s+n,"/?#");
 if(end==s+n || (size_t)(end-s)>=cap) return -1;
 for(const char *p=s;p<end;p++) {
  if(*p=='@'||(unsigned char)*p<=32||*p=='\\') return -1;
  out[p-s]=(char)tolower((unsigned char)*p);
 }
 out[end-s]=0; return 0;
}
int catalog_url_valid(const char *url)
{
 char o[512]; if(origin(url,o,sizeof o)<0 || strlen(url)>=URL_MAX) return 0;
 for(const char *p=url;*p;p++) if((unsigned char)*p<=32||*p=='\\') return 0;
 return 1;
}
int catalog_same_origin(const char *a,const char *b)
{
 char x[512],y[512]; return origin(a,x,sizeof x)==0&&origin(b,y,sizeof y)==0&&!strcmp(x,y);
}
int catalog_api_kind(const char *u)
{
 if(!u) return 0;
 if(!strncmp(u,"https://archive.org/advancedsearch.php?",strlen("https://archive.org/advancedsearch.php?"))) return 1;
 if(!strncmp(u,"https://archive.org/metadata/",strlen("https://archive.org/metadata/"))) return 2;
 if(!strncmp(u,"https://commons.wikimedia.org/w/api.php?",strlen("https://commons.wikimedia.org/w/api.php?"))) return 3;
 return 0;
}
static int archive_query(const char *q,int page,char *out,size_t cap)
{
 char enc[1500]; if(url_encode_component(enc,sizeof enc,q)<0) return -1;
 int n=snprintf(out,cap,"https://archive.org/advancedsearch.php?q=%s&fl[]=identifier&fl[]=title&fl[]=creator&fl[]=description&rows=24&page=%d&output=json",enc,page);
 return n<0||(size_t)n>=cap?-1:0;
}
int catalog_api_url(const char *in,char *out,size_t cap)
{
 const char *p="https://archive.org/details/";
 if(!strncmp(in,p,strlen(p))) {
  const char *id=in+strlen(p); size_t n=strcspn(id,"/?#");
  if(!n||n>100) return -1;
  for(size_t i=0;i<n;i++) if(!isalnum((unsigned char)id[i])&&id[i]!='_'&&id[i]!='-'&&id[i]!='.') return -1;
  char q[256]; snprintf(q,sizeof q,"collection:%.*s AND mediatype:texts AND NOT access-restricted-item:true",(int)n,id);
  return archive_query(q,1,out,cap);
 }
 int n=snprintf(out,cap,"%s",in); return catalog_url_valid(in)&&n>=0&&(size_t)n<cap?0:-1;
}
static int query_value(const char *url,const char *key,char *out,size_t cap)
{
 const char *p=strchr(url,'?'); if(!p) return -1; p++;
 size_t k=strlen(key); for(;;) {
  if(!strncmp(p,key,k)&&p[k]=='=') {
   p+=k+1; size_t n=0;
   while(*p&&*p!='&') {
    unsigned char c=(unsigned char)*p++;
    if(c=='%') {
     if(!p[0]||!p[1]||!isxdigit((unsigned char)p[0])||!isxdigit((unsigned char)p[1])) return -1;
     char hex[3]={p[0],p[1],0}; c=(unsigned char)strtol(hex,NULL,16); p+=2;
    } else if(c=='+') c=' ';
    if(!c||n+1>=cap) return -1;
    out[n++]=(char)c;
   }
   out[n]=0; return 0;
  }
  p=strchr(p,'&'); if(!p) return -1; p++;
 }
}
int catalog_api_search(const char *base,const char *terms,char *out,size_t cap)
{
 int kind=catalog_api_kind(base); char q[1024];
 if(kind==1) {
  char original[700],safe[256];
  if(query_value(base,"q",original,sizeof original)<0) return -1;
  /* Treat search text as a literal, never as an Archive query operator. */
  size_t n=0; for(const char *p=terms;*p&&n+1<sizeof safe;p++)
   safe[n++]=(*p=='"'||*p=='\\'||(unsigned char)*p<32)?' ':*p;
  safe[n]=0; snprintf(q,sizeof q,"(%s) AND \"%s\"",original,safe);
  return archive_query(q,1,out,cap);
 }
 if(kind==3) {
  char enc[1200]; snprintf(q,sizeof q,"filetype:pdf %s",terms);
  if(url_encode_component(enc,sizeof enc,q)<0) return -1;
  int n=snprintf(out,cap,"https://commons.wikimedia.org/w/api.php?action=query&generator=search&gsrsearch=%s&gsrnamespace=6&gsrlimit=24&prop=imageinfo&iiprop=url%%7Cmime%%7Cextmetadata&iiurlwidth=180&format=json",enc);
  return n<0||(size_t)n>=cap?-1:0;
 }
 return -1;
}
static const char *plain(opds_feed *f,const char *s)
{
 char text[2048]; opds_strip_html(s,text,sizeof text); return opds_dups(f,text);
}
static int flag(const cJSON *o,const char *k)
{
 const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,k);
 return cJSON_IsTrue(v)||(cJSON_IsNumber(v)&&v->valuedouble!=0)||(cJSON_IsNumber(v)&&v->valuedouble!=0)||(cJSON_IsString(v)&&(!strcmp(v->valuestring,"true")||!strcmp(v->valuestring,"1")));
}
static void link(opds_feed *f,opds_entry *e,opds_rel role,const char *url,const char *mime,long long size)
{ opds_entry_link(f,e,role,OPDS_ACQ_OPEN_ACCESS,opds_abs(f,url),mime,NULL,size); }
static int archive_parse(opds_feed *f,const cJSON *root,int kind,const char *url)
{
 char address[URL_MAX];
 if(kind==1) {
  const cJSON *r=cJSON_GetObjectItemCaseSensitive(root,"response");
  const cJSON *docs=cJSON_GetObjectItemCaseSensitive(r,"docs");
  if(!cJSON_IsArray(docs)) return -1;
  const cJSON *total=cJSON_GetObjectItemCaseSensitive(r,"numFound");
  if(cJSON_IsNumber(total)&&total->valuedouble>=0&&total->valuedouble<LLONG_MAX) f->total=(long long)total->valuedouble;
  const cJSON *doc; cJSON_ArrayForEach(doc,docs) {
   const char *id=str(doc,"identifier"); char enc[512];
   if(!id[0]||url_encode_component(enc,sizeof enc,id)<0) continue;
   opds_entry *e=opds_add_entry(f); if(!e) break;
   e->id=opds_dups(f,id); e->title=plain(f,str(doc,"title")); e->author=plain(f,str(doc,"creator")); e->summary=plain(f,str(doc,"description"));
   snprintf(address,sizeof address,"https://archive.org/services/img/%s",enc); link(f,e,REL_IMAGE,address,"image/jpeg",0);
   snprintf(address,sizeof address,"https://archive.org/metadata/%s",enc); link(f,e,REL_NAV,address,"application/json",0);
  }
  char q[1024],p[32]; int page=1;
  if(query_value(url,"page",p,sizeof p)==0) { long v=strtol(p,NULL,10); if(v>0&&v<100000) page=(int)v; }
  if(query_value(url,"q",q,sizeof q)==0) {
   if(page>1&&archive_query(q,page-1,address,sizeof address)==0) f->prev=opds_dups(f,address);
   if((long long)page*24<f->total&&archive_query(q,page+1,address,sizeof address)==0) f->next=opds_dups(f,address);
  }
  return 0;
 }
 const cJSON *m=cJSON_GetObjectItemCaseSensitive(root,"metadata");
 if(!cJSON_IsObject(m)) return -1;
 const char *id=str(m,"identifier"); char enc[512]; if(!id[0]||url_encode_component(enc,sizeof enc,id)<0) return -1;
 opds_entry *e=opds_add_entry(f); if(!e) return -1;
 e->title=plain(f,str(m,"title")); e->author=plain(f,str(m,"creator")); e->summary=plain(f,str(m,"description"));
 snprintf(address,sizeof address,"https://archive.org/services/img/%s",enc); link(f,e,REL_IMAGE,address,"image/jpeg",0);
 if(flag(root,"is_dark")||flag(root,"nodownload")||flag(m,"access-restricted-item")) {
  e->summary=opds_dups(f,"This item is restricted by its provider and cannot be downloaded here."); return 0;
 }
 const cJSON *file,*files=cJSON_GetObjectItemCaseSensitive(root,"files");
 cJSON_ArrayForEach(file,files) {
  if(flag(file,"private")) continue;
  const char *name=str(file,"name"),*ext=strrchr(name,'.'),*mime=NULL;
  if(ext&&!strcasecmp(ext,".pdf")) mime="application/pdf";
  else if(ext&&!strcasecmp(ext,".cbz")) mime="application/vnd.comicbook+zip";
  else if(ext&&!strcasecmp(ext,".cbr")) mime="application/vnd.comicbook-rar";
  else if(ext&&!strcasecmp(ext,".epub")) mime="application/epub+zip";
  if(!mime) continue;
  char fileenc[1200]; if(url_encode_component(fileenc,sizeof fileenc,name)<0) continue;
  snprintf(address,sizeof address,"https://archive.org/download/%s/%s",enc,fileenc);
  long long size=strtoll(str(file,"size"),NULL,10); link(f,e,REL_ACQ,address,mime,size>0?size:0);
 }
 return 0;
}
static int commons_parse(opds_feed *f,const cJSON *root,const char *url)
{
 const cJSON *query=cJSON_GetObjectItemCaseSensitive(root,"query"),*pages=cJSON_GetObjectItemCaseSensitive(query,"pages");
 if(!cJSON_IsObject(pages)) {
  /* MediaWiki omits query when a valid search has no results. */
  return cJSON_GetObjectItemCaseSensitive(root,"error")?-1:0;
 }
 const cJSON *page; cJSON_ArrayForEach(page,pages) {
  const cJSON *ii=cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(page,"imageinfo"),0);
  if(strcmp(str(ii,"mime"),"application/pdf")) continue;
  const char *href=str(ii,"url"); if(!catalog_url_valid(href)) continue;
  opds_entry *e=opds_add_entry(f); if(!e) break;
  const char *title=str(page,"title"); e->title=plain(f,!strncmp(title,"File:",5)?title+5:title);
  const cJSON *meta=cJSON_GetObjectItemCaseSensitive(ii,"extmetadata");
  e->author=plain(f,str(cJSON_GetObjectItemCaseSensitive(meta,"Artist"),"value"));
  char summary[4096]; snprintf(summary,sizeof summary,"%s\nLicence: %s",str(cJSON_GetObjectItemCaseSensitive(meta,"ImageDescription"),"value"),str(cJSON_GetObjectItemCaseSensitive(meta,"LicenseShortName"),"value")); e->summary=plain(f,summary);
  link(f,e,REL_ACQ,href,"application/pdf",0);
  link(f,e,REL_IMAGE,str(ii,"thumburl"),"image/jpeg",0);
 }
 const cJSON *cont=cJSON_GetObjectItemCaseSensitive(root,"continue"),*offset=cJSON_GetObjectItemCaseSensitive(cont,"gsroffset");
 if(cJSON_IsNumber(offset)&&offset->valuedouble>=0&&offset->valuedouble<1000000) {
  char base[URL_MAX],next[URL_MAX]; snprintf(base,sizeof base,"%s",url);
  char *old=strstr(base,"&gsroffset="); if(old) *old=0;
  int n=snprintf(next,sizeof next,"%s&gsroffset=%d",base,offset->valueint);
  if(n>0&&(size_t)n<sizeof next) f->next=opds_dups(f,next);
 }
 return 0;
}
int catalog_api_parse(const char *body,size_t len,const char *url,opds_feed **out,char *err,size_t cap)
{
 *out=NULL; int kind=catalog_api_kind(url); cJSON *root=cJSON_ParseWithLength(body,len);
 if(!root) { snprintf(err,cap,"The service returned invalid JSON"); return -1; }
 opds_feed *f=opds_feed_new(2,url); int rc=-1;
 if(f) {
  f->title=opds_dups(f,kind==3?"Wikimedia Commons comics":"Internet Archive comics");
  rc=kind==3?commons_parse(f,root,url):archive_parse(f,root,kind,url);
  if(opds_feed_oom(f)) rc=-1;
 }
 cJSON_Delete(root);
 if(rc<0) { opds_feed_free(f); snprintf(err,cap,"The service returned an unsupported catalogue response"); return -1; }
 *out=f; return 0;
}
