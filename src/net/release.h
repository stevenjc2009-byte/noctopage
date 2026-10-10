#ifndef NOCT_RELEASE_H
#define NOCT_RELEASE_H
#include <stddef.h>
#include <stdint.h>
#ifndef NOCT_VERSION
#define NOCT_VERSION "00.04" /* Host-test fallback; Vita build uses APP_VER. */
#endif
#define UPDATE_MAX_BYTES (128LL*1024*1024)
#define NOCT_UPDATE_REPOSITORY "stevenjc2009-byte/noctopage"
/* A saved custom source wins; absent/empty settings use the official repository. */
const char *release_repository(const char *configured);
typedef struct { char version[16], url[2048], sha256[65]; int64_t size; } release_t;
int release_repo(const char *input,char *out,size_t cap);
int release_version(const char *s); /* MM.mm or vMM.mm, numeric ordering, -1 invalid */
/* 1 newer compatible VPK, 0 current, -1 invalid/ambiguous release. */
int release_parse(const char *body,size_t len,const char *repo,const char *current,
                  release_t *out,char *err,size_t cap);
#endif
