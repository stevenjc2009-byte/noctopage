/* natsort — natural ("p2" < "p10") string ordering for file names. Pure C. */
#ifndef NOCTO_NATSORT_H
#define NOCTO_NATSORT_H

#include <stddef.h>

/* Natural compare. ASCII case-insensitive; runs of ASCII digits compare by numeric value
 * (any length, no overflow; leading zeros ignored); everything else compares as unsigned
 * bytes, so UTF-8 sorts in code-point order. If the strings are equivalent under those
 * rules (e.g. "A1" vs "a01") the result falls back to strcmp, so the order is total and
 * natcmp() returns 0 only for identical strings. */
int natcmp(const char *a, const char *b);

/* qsort() comparator for an array of `const char *` (or `char *`). */
int natcmp_ptr(const void *a, const void *b);

/* Sort an array of string pointers in natural order, in place. */
void natsort_strings(const char **arr, size_t n);

#endif
