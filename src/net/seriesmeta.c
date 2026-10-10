
#include "net/seriesmeta.h"
#include "core/strutil.h"
#include <cJSON.h>
#include <string.h>
#include <math.h>
static int title_matches(const cJSON *obj,const char *title)
{
 const char *keys[]={"title","title_english","title_japanese"};
 for(int i=0;i<3;i++) { cJSON *v=cJSON_GetObjectItemCaseSensitive(obj,keys[i]); if(cJSON_IsString(v)&&!str_casecmp(v->valuestring,title)) return 1; }
 cJSON *v; cJSON_ArrayForEach(v,cJSON_GetObjectItemCaseSensitive(obj,"title_synonyms")) if(cJSON_IsString(v)&&!str_casecmp(v->valuestring,title)) return 1;
 cJSON_ArrayForEach(v,cJSON_GetObjectItemCaseSensitive(obj,"titles")) { cJSON *s=cJSON_GetObjectItemCaseSensitive(v,"title"); if(cJSON_IsString(s)&&!str_casecmp(s->valuestring,title)) return 1; }
 return 0;
}
int seriesmeta_count(const char *json,size_t len,const char *title,char *source,size_t cap)
{
 if(cap) source[0]=0;
 cJSON *root=cJSON_ParseWithLength(json,len),*v; if(!root) return 0;
 int count=0,matches=0;
 cJSON_ArrayForEach(v,cJSON_GetObjectItemCaseSensitive(root,"data")) {
  cJSON *type=cJSON_GetObjectItemCaseSensitive(v,"type"),*vol=cJSON_GetObjectItemCaseSensitive(v,"volumes"),*status=cJSON_GetObjectItemCaseSensitive(v,"status"),*url=cJSON_GetObjectItemCaseSensitive(v,"url");
  if(!cJSON_IsString(type)||strcmp(type->valuestring,"Manga")||!title_matches(v,title)) continue;
  matches++;
  if(!cJSON_IsString(status)||strcmp(status->valuestring,"Finished")||!cJSON_IsNumber(vol)||!isfinite(vol->valuedouble)||vol->valuedouble<1||vol->valuedouble>1000||floor(vol->valuedouble)!=vol->valuedouble) continue;
  count=vol->valueint; if(cJSON_IsString(url)) str_copy(source,cap,url->valuestring);
 }
 cJSON_Delete(root); if(matches!=1) { if(cap) source[0]=0; return 0; } return count;
}
