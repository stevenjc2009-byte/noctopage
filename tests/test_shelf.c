#include "test.h"
#include "lib/shelf.h"
#include <string.h>

int main(void)
{
	char series[512];
	shelf_series("ux0:books/Tokyo Ghoul/Volume 02.cbz", series, sizeof series);
	CHECK_STR(series, "Tokyo Ghoul");
	shelf_series("ux0:/books/Tokyo Ghoul/Volume 10.cbz", series, sizeof series);
	CHECK_STR(series, "Tokyo Ghoul");
	shelf_series("ux0:books/One Piece/Arc 1/Volume 2", series, sizeof series);
	CHECK_STR(series, "One Piece/Arc 1");
	shelf_series("ux0:books/standalone.cbz", series, sizeof series); CHECK_STR(series, "");
	shelf_series("ux0:bookshelf/Tokyo/2.cbz", series, sizeof series); CHECK_STR(series, "");
	shelf_series("uma0:books/Tokyo/2.cbz", series, sizeof series); CHECK_STR(series, "");
	db_book_t b = {0}, a = {0}; b.kind = DB_KIND_ARCHIVE;
	strcpy(b.title,"Tokyo Ghoul Volume 2"); strcpy(b.author,"Sui Ishida");
	strcpy(b.path,"ux0:books/Tokyo Ghoul/Volume 2.cbz");
	CHECK(shelf_matches(&b,"tokyo",SHELF_ALL));
	CHECK(shelf_matches(&b,"ISHIDA",SHELF_ALL));
	CHECK(shelf_matches(&b,"Volume 2.cbz",SHELF_ALL));
	CHECK(!shelf_matches(&b,"one piece",SHELF_ALL));
	CHECK(!shelf_matches(&b,"Tokya",SHELF_ALL));
	CHECK(shelf_matches(&b,"",SHELF_UNREAD));
	CHECK(!shelf_matches(&b,"",SHELF_READING));
	CHECK(shelf_matches(&b,"",SHELF_COMICS));
	CHECK(!shelf_matches(&b,"",SHELF_BOOKS));
	b.last_read=5; CHECK(!shelf_matches(&b,"",SHELF_UNREAD)); CHECK(shelf_matches(&b,"",SHELF_READING));
	b.finished=1; CHECK(shelf_matches(&b,"",SHELF_FINISHED)); CHECK(!shelf_matches(&b,"",SHELF_READING));
	b.favorite=1; CHECK(shelf_matches(&b,"",SHELF_FAVORITES));
	b.missing=1; CHECK(!shelf_matches(&b,"",SHELF_ALL));
	strcpy(a.path,"ux0:books/Tokyo Ghoul/Volume 10.cbz");
	CHECK(shelf_volume_compare(&b,&a)<0);
	strcpy(b.title,"Z custom title"); strcpy(a.title,"A custom title");
	CHECK(shelf_volume_compare(&b,&a)<0); /* title edits cannot change volume order */
	strcpy(b.title,"日本語の漫画"); b.missing=0;
	CHECK(shelf_matches(&b,"日本語",SHELF_ALL));
	/* Loose volumes group with their series folder without guessing bare numbers. */
	shelf_series("ux0:books/Tokyo Ghoul Volume 01.cbz", series, sizeof series); CHECK_STR(series,"Tokyo Ghoul");
	shelf_series("ux0:books/Tokyo Ghoul Vol. 10.pdf", series, sizeof series); CHECK_STR(series,"Tokyo Ghoul");
	shelf_series("ux0:books/One Piece v02.epub", series, sizeof series); CHECK_STR(series,"One Piece");
	shelf_series("ux0:books/One Piece [V02].cbz", series, sizeof series); CHECK_STR(series,"One Piece");
	shelf_series("ux0:books/One Piece VOL-03.cbz", series, sizeof series); CHECK_STR(series,"One Piece");
	shelf_series("ux0:books/1984.epub", series, sizeof series); CHECK_STR(series,"");
	shelf_series("ux0:books/Title 2.pdf", series, sizeof series); CHECK_STR(series,"");
	shelf_series("ux0:books/The Volume of Water.epub", series, sizeof series); CHECK_STR(series,"");
	shelf_series("ux0:books/Book v", series, sizeof series); CHECK_STR(series,"");
	shelf_series("ux0:books/Book vo", series, sizeof series); CHECK_STR(series,"");
	shelf_series("ux0:books/Book volu", series, sizeof series); CHECK_STR(series,"");
	shelf_series("ux0:books/Book volum", series, sizeof series); CHECK_STR(series,"");
	shelf_series("ux0:books/Book volume", series, sizeof series); CHECK_STR(series,"");
	shelf_series("ux0:books/", series, sizeof series); CHECK_STR(series,"");
	shelf_series("ux0:books/Book v2nd.epub", series, sizeof series); CHECK_STR(series,"");
	char small[4]; shelf_series("ux0:books/Tokyo Ghoul Volume 1.cbz",small,sizeof small); CHECK_STR(small,"");
	strcpy(b.series,"Manual series"); shelf_book_series(&b,series,sizeof series); CHECK_STR(series,"Manual series");
	b.series[0]=0; shelf_book_series(&b,series,sizeof series); CHECK_STR(series,"Tokyo Ghoul");
	strcpy(b.series,"Assigned series");
	CHECK(shelf_matches(&b,"assigned",SHELF_ALL));
	CHECK(!shelf_matches(&b,"unrelated",SHELF_ALL));
	strcpy(b.path,"ux0:books/Series/Vol 10.cbz");
	strcpy(a.path,"ux0:books/Series/Volume 2.pdf");
	CHECK(shelf_volume_compare(&a,&b)<0);
	strcpy(a.path,"ux0:books/Series/v0002.epub"); CHECK(shelf_volume_compare(&a,&b)<0);
	strcpy(a.path,"ux0:books/Series/Extras.cbz"); CHECK(shelf_volume_compare(&b,&a)<0);
	strcpy(b.path,"ux0:books/Series/Vol 999999999999999999999999.cbz");
	strcpy(a.path,"ux0:books/Series/Volume 1000000000000000000000000.cbz");
	CHECK(shelf_volume_compare(&b,&a)<0);
	/* Antisymmetry and transitivity across mixed markers and unnumbered extras. */
	const char *names[]={"Vol 10.cbz","Volume 2.cbz","Extras.cbz","V01.cbz","Vol 0.cbz"};
	db_book_t books[5]={{0}};
	for (int i=0;i<5;i++) { books[i].id=i+1; snprintf(books[i].path,sizeof books[i].path,"ux0:books/Series/%s",names[i]); }
	for (int i=0;i<5;i++) for (int j=0;j<5;j++) {
		int ij=shelf_volume_compare(&books[i],&books[j]),ji=shelf_volume_compare(&books[j],&books[i]);
		CHECK((ij>0)-(ij<0)==-((ji>0)-(ji<0)));
		for (int k=0;k<5;k++) if (ij<0 && shelf_volume_compare(&books[j],&books[k])<0)
			CHECK(shelf_volume_compare(&books[i],&books[k])<0);
	}

 b.kind=DB_KIND_FIXED; CHECK(shelf_matches(&b,"",SHELF_COMICS));
 strcpy(b.title,"ÉCOLE Straße Привет"); CHECK(shelf_matches(&b,"école",SHELF_ALL)); CHECK(shelf_matches(&b,"STRASSE",SHELF_ALL)); CHECK(shelf_matches(&b,"ПРИВЕТ",SHELF_ALL));
 strcpy(b.path,"ux0:books/Series/Volume 007.cbz"); CHECK_INT(shelf_volume_index(&b),7);
 strcpy(b.path,"ux0:books/Series/Volume 99999999999.cbz"); CHECK_INT(shelf_volume_index(&b),0);
 return TEST_DONE("shelf");
}
