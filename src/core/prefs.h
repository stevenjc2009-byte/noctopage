/* prefs.h - the app's settings.ini (PATH_SETTINGS), one process-wide store over
 * core/settings. Every set saves the file straight away. MAIN THREAD ONLY. */
#ifndef NOCT_PREFS_H
#define NOCT_PREFS_H

/* Load at boot. A missing file is a first run, not an error. */
void prefs_load(void);

long prefs_get_int(const char *section, const char *key, long def);
void prefs_set_int(const char *section, const char *key, long value);
const char *prefs_get(const char *section, const char *key, const char *def);
void prefs_set(const char *section, const char *key, const char *value);

int prefs_set_checked(const char *section,const char *key,const char *value);
#endif
