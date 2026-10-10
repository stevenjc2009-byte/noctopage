
#include "test.h"
#define NOCT_SHELF_TEST
/* Exercise the actual screen aggregation and card values, with only services stubbed. */
#include "../src/screens/mybooks.c"
void grid_set_count(grid_t *g,int n) { g->count=n; }
int db_collection_contains(int64_t c,uint64_t id) { (void)c; return id==1; }
int db_series_setting_get(const char *s,const char *k,char *out,size_t cap) { (void)s; (void)k; if(cap) out[0]=0; return 0; }
vita2d_texture *covers_get(uint64_t id,const char *path,int kind) { (void)id;(void)path;(void)kind; return NULL; }
vita2d_texture *covers_get_series(const char *s) { (void)s; return NULL; }
const char *formats_badge_r(const char *path,char *out,size_t cap) { (void)path; str_copy(out,cap,"CBZ"); return out; }
int main(void)
{
 db_book_t books[10]={0}; shelf_row_t rows[10]={0}; int order[10]; char folders[10][512];
 g_books=books; g_rows=rows; g_order=order; g_folders=folders; g_nbooks=10; g_group=1;
 for(int i=0;i<10;i++) { order[i]=i; strcpy(folders[i],"Tokyo Ghoul"); books[i].id=i+1; books[i].kind=DB_KIND_ARCHIVE; snprintf(books[i].title,sizeof books[i].title,"Volume %d",i+1); snprintf(books[i].path,sizeof books[i].path,"ux0:books/Tokyo Ghoul/Volume %d.cbz",i+1); }
 books[0].finished=1; books[0].last_read=123;
 build_rows(); CHECK_INT(g_count,1); CHECK_INT(rows[0].volumes,10); CHECK_INT(rows[0].finished,1); CHECK(rows[0].progress>0.099 && rows[0].progress<0.101);
 grid_item_t card={0}; item(NULL,0,&card); CHECK(!card.finished); CHECK(card.progress>0.099f && card.progress<0.101f);
 g_filter=SHELF_FINISHED; build_rows(); CHECK_INT(g_count,1); CHECK_INT(rows[0].volumes,10); CHECK(rows[0].progress<0.101); item(NULL,0,&card); CHECK(!card.finished); CHECK_STR(card.badge,"1/10 read");
 books[1].progress=0.5; books[1].last_read=124; g_filter=SHELF_ALL; build_rows(); CHECK(rows[0].progress>0.149 && rows[0].progress<0.151);
 g_collection=7; build_rows(); CHECK_INT(g_count,1); CHECK_INT(rows[0].volumes,10); CHECK(rows[0].progress<0.151);
 g_collection=0; strcpy(g_series,"Tokyo Ghoul"); build_rows(); CHECK_INT(g_count,10); item(NULL,0,&card); CHECK(card.finished); CHECK(card.progress==1.0f); CHECK_STR(card.badge,"Complete"); item(NULL,1,&card); CHECK(!card.finished); CHECK(card.progress==0.5f);
 g_series[0]=0; for(int i=0;i<10;i++) books[i].finished=1; build_rows(); item(NULL,0,&card); CHECK(card.finished); CHECK(card.progress==1.0f); CHECK_STR(card.badge,"Complete");
 return TEST_DONE("shelf_progress");
}
