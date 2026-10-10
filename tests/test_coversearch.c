#include "test.h"
#include "net/coversearch.h"
#include <string.h>

int main(void)
{
	char url[2048]; cover_candidate_t out[12];
	CHECK_INT(coversearch_url("Tokyo Ghoul 2 & More",url,sizeof url),0);
	CHECK(strstr(url,"Tokyo%20Ghoul%202%20%26%20More") != NULL);
	CHECK(strstr(url,"limit=12") != NULL);
	CHECK_INT(coversearch_url("",url,sizeof url),-1);
	CHECK_INT(coversearch_url("Tokyo",url,8),-1);
	CHECK_INT(coversearch_image_url(12345,url,sizeof url),0);
	CHECK_STR(url,"https://covers.openlibrary.org/b/id/12345-M.jpg?default=false");
	CHECK_INT(coversearch_image_url(0,url,sizeof url),-1);
	const char *json="{\"docs\":[{\"title\":\"No cover\"},{\"title\":\"Tokyo Ghoul 2\",\"author_name\":[\"Sui Ishida\"],\"cover_i\":123,\"key\":\"/works/OL1W\"},{\"title\":\"Bad\",\"cover_i\":-1},{\"title\":\"Other edition\",\"cover_i\":456}]}";
	CHECK_INT(coversearch_parse(json,strlen(json),out,12),2);
	CHECK_STR(out[0].title,"Tokyo Ghoul 2"); CHECK_STR(out[0].author,"Sui Ishida");
	CHECK_U64(out[0].cover_id,123); CHECK_STR(out[0].key,"/works/OL1W");
	CHECK_STR(out[1].author,"");
	CHECK_INT(coversearch_parse(json,strlen(json),out,1),1);
	CHECK_INT(coversearch_parse("{bad",4,out,12),-1);
	CHECK_INT(coversearch_parse("{}",2,out,12),-1);
	CHECK_INT(coversearch_parse("{\"docs\":[]}",11,out,12),0);
	return TEST_DONE("coversearch");
}
