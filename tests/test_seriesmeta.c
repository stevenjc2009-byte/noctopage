
#include "test.h"
#include "net/seriesmeta.h"
int main(void)
{
 char source[128]; const char *valid="{\"data\":[{\"title\":\"Tokyo Ghoul\",\"type\":\"Manga\",\"status\":\"Finished\",\"volumes\":14,\"url\":\"https://myanimelist.net/manga/33327/Tokyo_Ghoul\"}]}";
 CHECK_INT(seriesmeta_count(valid,strlen(valid),"tokyo ghoul",source,sizeof source),14); CHECK(strstr(source,"33327")); CHECK_INT(seriesmeta_count(valid,strlen(valid),"Tokyo Ghoul:re",source,sizeof source),0);
 const char *unknown="{\"data\":[{\"title\":\"Series\",\"type\":\"Manga\",\"status\":\"Publishing\",\"volumes\":null}]}";
 CHECK_INT(seriesmeta_count(unknown,strlen(unknown),"Series",source,sizeof source),0);
 const char *ambiguous="{\"data\":[{\"title\":\"Series\",\"type\":\"Manga\",\"status\":\"Finished\",\"volumes\":10},{\"title\":\"Series\",\"type\":\"Manga\",\"status\":\"Finished\",\"volumes\":12}]}";
 CHECK_INT(seriesmeta_count(ambiguous,strlen(ambiguous),"Series",source,sizeof source),0);
 const char *alias="{\"data\":[{\"title\":\"Romanised\",\"title_synonyms\":[\"English title\"],\"type\":\"Manga\",\"status\":\"Finished\",\"volumes\":9}]}";
 CHECK_INT(seriesmeta_count(alias,strlen(alias),"English title",source,sizeof source),9); CHECK_INT(seriesmeta_count("{",1,"Series",source,sizeof source),0);
 return TEST_DONE("seriesmeta");
}
