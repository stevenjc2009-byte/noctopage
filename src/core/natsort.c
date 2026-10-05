#include "core/natsort.h"

#include <stdlib.h>
#include <string.h>

static int is_digit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

static int lower_byte(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

/* Compare the digit runs at *pa and *pb by value; advance both past their runs. */
static int cmp_digit_runs(const char **pa, const char **pb)
{
    const char *a = *pa, *b = *pb;
    size_t la, lb;
    int r;

    while (*a == '0' && is_digit((unsigned char)a[1]))
        a++;
    while (*b == '0' && is_digit((unsigned char)b[1]))
        b++;
    la = lb = 0;
    while (is_digit((unsigned char)a[la]))
        la++;
    while (is_digit((unsigned char)b[lb]))
        lb++;

    if (la != lb)
        r = la < lb ? -1 : 1;
    else
        r = memcmp(a, b, la);
    *pa = a + la;
    *pb = b + lb;
    return r < 0 ? -1 : (r > 0 ? 1 : 0);
}

int natcmp(const char *a, const char *b)
{
    const char *pa = a, *pb = b;

    while (*pa && *pb) {
        unsigned char ca = (unsigned char)*pa, cb = (unsigned char)*pb;
        if (is_digit(ca) && is_digit(cb)) {
            int r = cmp_digit_runs(&pa, &pb);
            if (r != 0)
                return r;
        } else {
            int la = lower_byte(ca), lb = lower_byte(cb);
            if (la != lb)
                return la < lb ? -1 : 1;
            pa++;
            pb++;
        }
    }
    if (*pa || *pb)
        return *pa ? 1 : -1;

    {
        int r = strcmp(a, b);
        return r < 0 ? -1 : (r > 0 ? 1 : 0);
    }
}

int natcmp_ptr(const void *a, const void *b)
{
    return natcmp(*(const char *const *)a, *(const char *const *)b);
}

void natsort_strings(const char **arr, size_t n)
{
    if (n > 1)
        qsort((void *)arr, n, sizeof(arr[0]), natcmp_ptr);
}
