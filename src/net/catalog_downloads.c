#include "net/catalog_downloads.h"
#include <string.h>
static int allowed(const opds_acq *a)
{
 if(a->kind!=OPDS_ACQ_GENERIC&&a->kind!=OPDS_ACQ_OPEN_ACCESS) return 0;
 const char *format=opds_acq_format(a);
 const char *const supported[]={"cbz","cbr","cb7","pdf","epub","zip","mobi","fb2","txt","html",NULL};
 for(int i=0;supported[i];i++) if(!strcmp(format,supported[i])) return 1;
 return 0;
}
int catalog_downloads(const opds_entry *e,int comic,const opds_acq **out,int cap)
{
 if(!e||!out||cap<=0) return 0;
 const char *const prefs[]={"cbz","cbr","cb7","pdf","epub",NULL};
 const opds_acq *best=opds_best_acquisition(e,comic?prefs:NULL);
 int n=0; if(best&&allowed(best)) out[n++]=best;
 for(int i=0;i<e->nacq&&n<cap;i++) if(&e->acq[i]!=best&&allowed(&e->acq[i])) out[n++]=&e->acq[i];
 return n;
}
