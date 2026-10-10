#ifndef NOCT_CATALOG_DOWNLOADS_H
#define NOCT_CATALOG_DOWNLOADS_H
#include "net/opds.h"
/* Free, supported links only; comic sources prefer a page-preserving format. */
int catalog_downloads(const opds_entry *e,int comic,const opds_acq **out,int cap);
#endif
