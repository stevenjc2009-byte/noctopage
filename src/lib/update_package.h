#ifndef NOCT_UPDATE_PACKAGE_H
#define NOCT_UPDATE_PACKAGE_H
#include <stddef.h>
#include <stdint.h>
int update_path_safe(const char *s);
int update_sfo(const unsigned char *s,size_t n,char version[16]);
/* Validate digest and complete ZIP before extracting into a fresh app-owned directory.
 * Does not install anything. Never follows archive symlinks or overwrites existing files. */
int update_package(const char *vpk,const char *dest,const char *sha256,int64_t size,
                   const char *version,char *err,size_t cap);
#endif
