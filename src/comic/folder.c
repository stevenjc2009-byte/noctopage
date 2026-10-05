#include "comic/folder.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    char *name;
    uint64_t size;
} fentry_t;

struct folder_t {
    char *dir;
    int count, cap;
    fentry_t *e;
};

static void set_err(char *err, size_t cap, const char *msg)
{
    if (cap)
        snprintf(err, cap, "%s", msg);
}

/* dir + "/" + name; no separator added after a trailing '/' (or "ux0:"-style device colon). */
static char *join(const char *dir, const char *name)
{
    size_t dl = strlen(dir), nl = strlen(name);
    int sep = dl > 0 && dir[dl - 1] != '/' && dir[dl - 1] != ':';
    char *p = malloc(dl + (size_t)sep + nl + 1);

    if (!p)
        return NULL;
    memcpy(p, dir, dl);
    if (sep)
        p[dl] = '/';
    memcpy(p + dl + sep, name, nl + 1);
    return p;
}

static int by_name(const void *a, const void *b)
{
    return strcmp(((const fentry_t *)a)->name, ((const fentry_t *)b)->name);
}

static int push(folder_t *f, const char *name, uint64_t size)
{
    char *dup;

    if (f->count == f->cap) {
        int ncap = f->cap ? f->cap * 2 : 64;
        fentry_t *n = realloc(f->e, (size_t)ncap * sizeof *n);

        if (!n)
            return -1;
        f->e = n;
        f->cap = ncap;
    }
    dup = strdup(name);
    if (!dup)
        return -1;
    f->e[f->count].name = dup;
    f->e[f->count].size = size;
    f->count++;
    return 0;
}

folder_t *folder_open(const char *dir, int (*is_image)(const char *name), char *err,
                      size_t errcap)
{
    folder_t *f;
    DIR *d;
    struct dirent *de;

    if (errcap)
        err[0] = '\0';
    if (!dir || !*dir) {
        set_err(err, errcap, "no directory");
        return NULL;
    }
    d = opendir(dir);
    if (!d) {
        set_err(err, errcap, "cannot open directory");
        return NULL;
    }
    f = calloc(1, sizeof *f);
    if (f)
        f->dir = strdup(dir);
    if (!f || !f->dir) {
        closedir(d);
        folder_close(f);
        set_err(err, errcap, "out of memory");
        return NULL;
    }
    while ((de = readdir(d)) != NULL) {
        struct stat st = {0};
        char *full;
        int ok;

        if (de->d_name[0] == '.' && (de->d_name[1] == '\0' ||
                                     (de->d_name[1] == '.' && de->d_name[2] == '\0')))
            continue;
        if (is_image && !is_image(de->d_name))
            continue;
        full = join(dir, de->d_name);
        ok = full && stat(full, &st) == 0 && S_ISREG(st.st_mode);
        free(full);
        if (ok && push(f, de->d_name, (uint64_t)st.st_size) != 0) {
            closedir(d);
            folder_close(f);
            set_err(err, errcap, "out of memory");
            return NULL;
        }
    }
    closedir(d);
    if (f->count > 1)
        qsort(f->e, (size_t)f->count, sizeof *f->e, by_name);
    return f;
}

void folder_close(folder_t *f)
{
    int i;

    if (!f)
        return;
    for (i = 0; i < f->count; i++)
        free(f->e[i].name);
    free(f->e);
    free(f->dir);
    free(f);
}

int folder_count(const folder_t *f)
{
    return f ? f->count : 0;
}

const char *folder_name(const folder_t *f, int i)
{
    return (f && i >= 0 && i < f->count) ? f->e[i].name : NULL;
}

uint64_t folder_size(const folder_t *f, int i)
{
    return (f && i >= 0 && i < f->count) ? f->e[i].size : 0;
}

int folder_read(folder_t *f, int i, uint8_t **out, size_t *len)
{
    char *full;
    FILE *fp;
    uint8_t *buf;
    size_t want, got;

    if (!f || !out || !len || i < 0 || i >= f->count)
        return FOLDER_E_INDEX;
    if (f->e[i].size > FOLDER_MAX_FILE)
        return FOLDER_E_TOOBIG;
    full = join(f->dir, f->e[i].name);
    if (!full)
        return FOLDER_E_NOMEM;
    fp = fopen(full, "rb");
    free(full);
    if (!fp)
        return FOLDER_E_READ;
    want = (size_t)f->e[i].size;
    buf = malloc(want + 1);
    if (!buf) {
        fclose(fp);
        return FOLDER_E_NOMEM;
    }
    got = want ? fread(buf, 1, want, fp) : 0; /* one big read: small reads are slow on a Vita card */
    fclose(fp);
    if (got != want) {
        free(buf);
        return FOLDER_E_READ;
    }
    buf[got] = '\0';
    *out = buf;
    *len = got;
    return FOLDER_OK;
}
