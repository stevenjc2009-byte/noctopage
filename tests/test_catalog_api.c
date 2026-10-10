#include "test.h"
#include "net/catalog_api.h"
#include <stdlib.h>
static void parse(const char *body,const char *url,int count)
{
 opds_feed *f=NULL; char err[160];
 CHECK_INT(catalog_api_parse(body,strlen(body),url,&f,err,sizeof err),0);
 CHECK(f!=NULL); if(f) { CHECK_INT(f->nentries,count); opds_feed_free(f); }
}
int main(void)
{
 char url[2048];
 CHECK(catalog_url_valid("https://example.com/opds"));
 CHECK(!catalog_url_valid("https://user:secret@example.com/opds"));
 CHECK(!catalog_url_valid("https:///books"));
 CHECK(!catalog_url_valid("file:///private"));
 CHECK(!catalog_url_valid("https://host/a b"));
 CHECK(catalog_same_origin("https://EXAMPLE.com/a","https://example.com/b"));
 CHECK(!catalog_same_origin("https://example.com/a","http://example.com/b"));
 CHECK(!catalog_same_origin("https://example.com/a","https://example.com.evil/b"));
 CHECK(!catalog_same_origin("https://example.com/a","https://example.com:444/b"));
 CHECK_INT(catalog_api_url("https://archive.org/details/comics",url,sizeof url),0);
 CHECK_INT(catalog_api_kind(url),1);
 CHECK(strstr(url,"collection%3Acomics")!=NULL);
 CHECK_INT(catalog_api_url("https://archive.org/details/..",url,sizeof url),0); /* safely encoded query, never a path */
 CHECK_INT(catalog_api_url("https://archive.org/details/a%2fb",url,sizeof url),-1);
 const char *search="https://archive.org/advancedsearch.php?q=subject%3Acomics&rows=24&page=1&output=json";
 CHECK_INT(catalog_api_search(search,"title\" OR everything",url,sizeof url),0);
 CHECK(strstr(url,"%22title%20%20OR%20everything%22")!=NULL);
 const char *docs="{\"response\":{\"numFound\":50,\"docs\":[{\"identifier\":\"a/b\",\"title\":\"<b>Comic</b>\",\"creator\":[\"Writer\"],\"description\":\"<p>Description</p>\"}]}}";
 opds_feed *f=NULL; char err[160];
 CHECK_INT(catalog_api_parse(docs,strlen(docs),search,&f,err,sizeof err),0);
 if(f) {
  CHECK_INT(f->nentries,1); CHECK_STR(f->entries[0].title,"Comic"); CHECK_STR(f->entries[0].author,"Writer");
  CHECK_STR(f->entries[0].nav_href,"https://archive.org/metadata/a%2Fb"); CHECK(f->next!=NULL); CHECK(f->prev==NULL); opds_feed_free(f);
 }
 const char *metadata="{\"metadata\":{\"identifier\":\"comic\",\"title\":\"Book\"},\"files\":[{\"name\":\"cover.jpg\"},{\"name\":\"My Book.pdf\",\"size\":\"120\"},{\"name\":\"private.pdf\",\"private\":true}]}";
 CHECK_INT(catalog_api_parse(metadata,strlen(metadata),"https://archive.org/metadata/comic",&f,err,sizeof err),0);
 if(f) { CHECK_INT(f->entries[0].nacq,1); CHECK_STR(f->entries[0].acq[0].href,"https://archive.org/download/comic/My%20Book.pdf"); CHECK_STR(opds_acq_format(&f->entries[0].acq[0]),"pdf"); opds_feed_free(f); }
 const char *restricted="{\"metadata\":{\"identifier\":\"comic\",\"access-restricted-item\":\"true\"},\"files\":[{\"name\":\"book.pdf\"}]}";
 CHECK_INT(catalog_api_parse(restricted,strlen(restricted),"https://archive.org/metadata/comic",&f,err,sizeof err),0);
 if(f) { CHECK_INT(f->entries[0].nacq,0); opds_feed_free(f); }
 const char *commons="https://commons.wikimedia.org/w/api.php?action=query";
 parse("{\"query\":{\"pages\":{\"1\":{\"title\":\"File:Comic.pdf\",\"imageinfo\":[{\"mime\":\"application/pdf\",\"url\":\"https://upload.wikimedia.org/book.pdf\",\"thumburl\":\"https://upload.wikimedia.org/thumb.png\",\"extmetadata\":{\"ImageDescription\":{\"value\":\"<p>Comic summary</p>\"},\"LicenseShortName\":{\"value\":\"CC0\"}}}]}}}}",commons,1);
 parse("{\"batchcomplete\":\"\"}",commons,0);
 CHECK_INT(catalog_api_parse("{\"error\":\"backend\"}",19,search,&f,err,sizeof err),-1);
 CHECK(f==NULL);
 for(int i=0;i<500;i++) { char b[40]; memset(b,'x',sizeof b); b[i%39]=0; CHECK_INT(catalog_api_parse(b,strlen(b),search,&f,err,sizeof err),-1); CHECK(f==NULL); }
 /* Exercise real captured API responses when present; no network needed. */
 const char *paths[]={"catalog-api-archive.json","catalog-api-metadata.json","catalog-api-commons.json"};
 const char *urls[]={search,"https://archive.org/metadata/comic",commons};
 for(int i=0;i<3;i++) { FILE *file=fopen(paths[i],"rb"); if(!file) continue; fseek(file,0,SEEK_END); long n=ftell(file); rewind(file); char *b=malloc((size_t)n+1); CHECK(b!=NULL); if(b) { CHECK_INT(fread(b,1,(size_t)n,file),n); b[n]=0; CHECK_INT(catalog_api_parse(b,(size_t)n,urls[i],&f,err,sizeof err),0); CHECK(f&&f->nentries>0); opds_feed_free(f); free(b); } fclose(file); }
 return TEST_DONE("catalog_api");
}
