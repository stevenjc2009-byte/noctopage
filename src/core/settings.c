#include "core/settings.h"

#include "core/strutil.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SETTINGS_MAX_FILE (1024 * 1024)

typedef struct {
    char *section;
    char *key;
    char *value;
} entry_t;

struct settings {
    entry_t *e;
    size_t n, cap;
};

static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}

settings_t *settings_new(void)
{
    return (settings_t *)calloc(1, sizeof(settings_t));
}

static void entry_free(entry_t *e)
{
    free(e->section);
    free(e->key);
    free(e->value);
}

void settings_free(settings_t *s)
{
    if (!s)
        return;
    for (size_t i = 0; i < s->n; i++)
        entry_free(&s->e[i]);
    free(s->e);
    free(s);
}

static int find(const settings_t *s, const char *section, const char *key)
{
    if (!section)
        section = "";
    for (size_t i = 0; i < s->n; i++)
        if (strcmp(s->e[i].key, key) == 0 && strcmp(s->e[i].section, section) == 0)
            return (int)i;
    return -1;
}

static int name_ok(const char *section, const char *key, const char *value)
{
    if (key[0] == '\0' || key[0] == ';' || key[0] == '#' || strpbrk(key, "=[\r\n"))
        return 0;
    if (strpbrk(section, "]\r\n"))
        return 0;
    if (strpbrk(value, "\r\n"))
        return 0;
    return 1;
}

int settings_set(settings_t *s, const char *section, const char *key, const char *value)
{
    int idx;
    char *v;

    if (!section)
        section = "";
    if (!value)
        value = "";
    if (!name_ok(section, key, value))
        return -1;

    v = dup_str(value);
    if (!v)
        return -1;
    idx = find(s, section, key);
    if (idx >= 0) {
        free(s->e[idx].value);
        s->e[idx].value = v;
        return 0;
    }
    if (s->n == s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 16;
        entry_t *ne = (entry_t *)realloc(s->e, nc * sizeof(entry_t));
        if (!ne) {
            free(v);
            return -1;
        }
        s->e = ne;
        s->cap = nc;
    }
    s->e[s->n].section = dup_str(section);
    s->e[s->n].key = dup_str(key);
    s->e[s->n].value = v;
    if (!s->e[s->n].section || !s->e[s->n].key) {
        entry_free(&s->e[s->n]);
        return -1;
    }
    s->n++;
    return 0;
}

const char *settings_get(const settings_t *s, const char *section, const char *key, const char *def)
{
    int i = find(s, section, key);
    return i >= 0 ? s->e[i].value : def;
}

long settings_get_int(const settings_t *s, const char *section, const char *key, long def)
{
    const char *v = settings_get(s, section, key, NULL);
    char *end;
    long r;

    if (!v || *v == '\0')
        return def;
    errno = 0;
    r = strtol(v, &end, 10);
    if (*end != '\0' || errno != 0)
        return def;
    return r;
}

int settings_get_bool(const settings_t *s, const char *section, const char *key, int def)
{
    const char *v = settings_get(s, section, key, NULL);
    if (!v)
        return def;
    if (!str_casecmp(v, "1") || !str_casecmp(v, "true") || !str_casecmp(v, "yes") || !str_casecmp(v, "on"))
        return 1;
    if (!str_casecmp(v, "0") || !str_casecmp(v, "false") || !str_casecmp(v, "no") || !str_casecmp(v, "off"))
        return 0;
    return def;
}

int settings_set_int(settings_t *s, const char *section, const char *key, long value)
{
    char buf[32];
    snprintf(buf, sizeof buf, "%ld", value);
    return settings_set(s, section, key, buf);
}

int settings_set_bool(settings_t *s, const char *section, const char *key, int value)
{
    return settings_set(s, section, key, value ? "1" : "0");
}

int settings_remove(settings_t *s, const char *section, const char *key)
{
    int i = find(s, section, key);
    if (i < 0)
        return 0;
    entry_free(&s->e[i]);
    memmove(&s->e[i], &s->e[i + 1], (s->n - (size_t)i - 1) * sizeof(entry_t));
    s->n--;
    return 1;
}

size_t settings_count(const settings_t *s)
{
    return s->n;
}

/* Parse one line (NUL-terminated, EOL removed) and update `section` on a header line. */
static int parse_line(settings_t *s, char *line, char *section, size_t seccap)
{
    char *p = str_trim(line);
    char *eq, *key, *val;

    if (*p == '\0' || *p == ';' || *p == '#')
        return 0;
    if (*p == '[') {
        char *close = strchr(p, ']');
        if (!close)
            return 0;
        *close = '\0';
        str_copy(section, seccap, str_trim(p + 1));
        return 0;
    }
    eq = strchr(p, '=');
    if (!eq)
        return 0;
    *eq = '\0';
    key = str_trim(p);
    val = str_trim(eq + 1);
    if (*key == '\0')
        return 0;
    return settings_set(s, section, key, val);
}

int settings_load(settings_t *s, const char *path)
{
    FILE *f = fopen(path, "rb");
    char *buf, *line, *nl;
    long sz;
    size_t got;
    char section[256] = "";
    int rc = 0;

    if (!f)
        return errno == ENOENT ? 1 : -1;
    if (fseek(f, 0, SEEK_END) != 0 || (sz = ftell(f)) < 0 || sz > SETTINGS_MAX_FILE ||
        fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    got = fread(buf, 1, (size_t)sz, f);
    if (got != (size_t)sz && ferror(f))
        rc = -1;
    fclose(f);
    buf[got] = '\0';

    /* Skip a UTF-8 BOM if an editor added one. */
    line = buf;
    if (got >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB &&
        (unsigned char)line[2] == 0xBF)
        line += 3;
    while (rc == 0 && *line) {
        nl = strchr(line, '\n');
        if (nl)
            *nl = '\0';
        if (parse_line(s, line, section, sizeof section) != 0)
            rc = -1;
        line = nl ? nl + 1 : line + strlen(line);
    }
    free(buf);
    return rc;
}

static int write_all(const settings_t *s, FILE *f)
{
    /* Each section is written once, at the position of its first entry. */
    for (size_t i = 0; i < s->n; i++) {
        int seen = 0;
        for (size_t j = 0; j < i; j++)
            if (strcmp(s->e[j].section, s->e[i].section) == 0) {
                seen = 1;
                break;
            }
        if (seen)
            continue;
        if (s->e[i].section[0] != '\0' && fprintf(f, "[%s]\n", s->e[i].section) < 0)
            return -1;
        for (size_t j = i; j < s->n; j++)
            if (strcmp(s->e[j].section, s->e[i].section) == 0 &&
                fprintf(f, "%s=%s\n", s->e[j].key, s->e[j].value) < 0)
                return -1;
        if (fputc('\n', f) == EOF)
            return -1;
    }
    return 0;
}

int settings_save(const settings_t *s, const char *path)
{
    char tmp[1024];
    FILE *f;
    int ok;

    if (str_copy(tmp, sizeof tmp, path) >= sizeof tmp || str_cat(tmp, sizeof tmp, ".tmp") >= sizeof tmp)
        return -1;
    f = fopen(tmp, "wb");
    if (!f)
        return -1;
    ok = write_all(s, f) == 0;
    if (fflush(f) != 0)
        ok = 0;
    if (fclose(f) != 0)
        ok = 0;
    if (!ok) {
        remove(tmp);
        return -1;
    }
    if (rename(tmp, path) != 0) {
        remove(path);
        if (rename(tmp, path) != 0) {
            remove(tmp);
            return -1;
        }
    }
    return 0;
}
