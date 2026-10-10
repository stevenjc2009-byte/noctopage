#ifndef NOCT_SERIESMETA_H
#define NOCT_SERIESMETA_H
#include <stddef.h>
/* Accept only an unambiguous exact title/alias of a completed manga. */
int seriesmeta_count(const char *json,size_t len,const char *title,char *source,size_t cap);
#endif
