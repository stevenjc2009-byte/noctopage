/* settings: tiny INI store, [section] + key=value, atomic save. Pure C (stdio + malloc).
 *
 * Keys before any [section] header live in the section "" (pass "" or NULL). Section and key
 * names are case-sensitive. Values are trimmed of surrounding whitespace. Comment lines
 * (';' or '#') and blank lines are not kept: on save the file is rewritten as sections in
 * first-seen order, entries in first-seen order. Every key read from disk, including ones this
 * build does not know about, is kept and written back unchanged. */
#ifndef NOCTO_SETTINGS_H
#define NOCTO_SETTINGS_H

#include <stddef.h>

typedef struct settings settings_t;

settings_t *settings_new(void);
void settings_free(settings_t *s);

/* Load (merging over current contents). Returns 0 on success, 1 when the file does not exist
 * (store stays as is, which is fine on a first run), -1 on a read error or a file > 1 MiB. */
int settings_load(settings_t *s, const char *path);

/* Atomic save: writes "<path>.tmp", then renames over path. If the rename fails because the
 * target exists (some platforms refuse to overwrite), the target is removed and the rename is
 * retried. Returns 0 or -1. The old file is left intact on any failure before the rename. */
int settings_save(const settings_t *s, const char *path);

/* Getters return `def` when the key is absent (or unparsable, for int/bool). The returned
 * string pointer stays valid until the next settings_set*, settings_remove or settings_free. */
const char *settings_get(const settings_t *s, const char *section, const char *key, const char *def);
long settings_get_int(const settings_t *s, const char *section, const char *key, long def);
/* Accepts 1/0, true/false, yes/no, on/off (case-insensitive). */
int settings_get_bool(const settings_t *s, const char *section, const char *key, int def);

/* Setters return 0, or -1 on out-of-memory or an invalid name (empty key; '=', '[' or a
 * newline in the key; ']' or a newline in the section; a newline in the value). */
int settings_set(settings_t *s, const char *section, const char *key, const char *value);
int settings_set_int(settings_t *s, const char *section, const char *key, long value);
int settings_set_bool(settings_t *s, const char *section, const char *key, int value);

/* Returns 1 if the key was present and removed, else 0. */
int settings_remove(settings_t *s, const char *section, const char *key);

/* Number of stored entries (for tests). */
size_t settings_count(const settings_t *s);

#endif
