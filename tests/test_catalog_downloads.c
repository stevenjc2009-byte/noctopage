#include "test.h"
#include "net/catalog_downloads.h"
int main(void)
{
 const opds_acq *out[24];
 CHECK_INT(catalog_downloads(NULL,1,out,24),0);
 opds_acq links[30]; for(int i=0;i<30;i++) links[i]=(opds_acq){.kind=OPDS_ACQ_OPEN_ACCESS,.href="https://example.com/book.pdf",.type="application/pdf"};
 opds_entry e={.acq=links,.nacq=30}; CHECK_INT(catalog_downloads(&e,1,out,23),23); CHECK(out[0]==&links[0]);
 links[0].type="application/epub+zip"; CHECK_INT(catalog_downloads(&e,1,out,23),23); CHECK(out[0]==&links[1]);
 CHECK_INT(catalog_downloads(&e,0,out,23),23); CHECK(out[0]==&links[0]);
 links[0].type="application/octet-stream"; links[0].href="https://example.com/app.exe"; e.nacq=1;
 CHECK_INT(catalog_downloads(&e,1,out,23),0);
 links[0].type="application/pdf"; links[0].kind=OPDS_ACQ_BORROW; CHECK_INT(catalog_downloads(&e,1,out,23),0);
 links[0].kind=OPDS_ACQ_OPEN_ACCESS; CHECK_INT(catalog_downloads(&e,1,out,0),0);
 links[0].kind=OPDS_ACQ_OPEN_ACCESS; links[0].type="application/zip"; links[0].href="https://example.com/book.zip"; CHECK_INT(catalog_downloads(&e,0,out,23),1);
 links[0].type="application/x-mobipocket-ebook"; CHECK_INT(catalog_downloads(&e,0,out,23),1);
 return TEST_DONE("catalog_downloads");
}
