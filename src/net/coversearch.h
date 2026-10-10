#ifndef NOCT_COVERSEARCH_H
#define NOCT_COVERSEARCH_H
#include <stddef.h>
#include <stdint.h>
#define COVERSEARCH_MAX 12
typedef struct { char title[256], author[128], key[64]; int64_t cover_id; } cover_candidate_t;
int coversearch_url(const char *title, char *out, size_t cap);
int coversearch_parse(const char *json, size_t len, cover_candidate_t *out, int max);
int coversearch_image_url(int64_t id, char *out, size_t cap);
#endif
