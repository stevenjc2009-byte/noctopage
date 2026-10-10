#ifndef NOCT_CATALOG_API_H
#define NOCT_CATALOG_API_H
#include "net/opds.h"
/* Adapt documented Archive and Commons JSON APIs to the existing OPDS browser. */
int catalog_api_kind(const char *url); /* 1 Archive search, 2 metadata, 3 Commons */
int catalog_api_url(const char *input, char *out, size_t cap);
int catalog_api_search(const char *base, const char *terms, char *out, size_t cap);
int catalog_api_parse(const char *body, size_t len, const char *url,
                      opds_feed **out, char *err, size_t cap);
int catalog_same_origin(const char *a, const char *b);
int catalog_url_valid(const char *url);
#endif
