#include "core/prefs.h"

#include "core/log.h"
#include "core/paths.h"
#include "core/settings.h"

static settings_t *g_s;

static settings_t *store(void)
{
	if (!g_s) g_s = settings_new();
	return g_s;
}

void prefs_load(void)
{
	int r = settings_load(store(), PATH_SETTINGS);
	if (r < 0) log_printf("prefs: cannot read %s", PATH_SETTINGS);
}

static void save(void)
{
	if (settings_save(store(), PATH_SETTINGS) != 0) log_printf("prefs: cannot save %s", PATH_SETTINGS);
}

long prefs_get_int(const char *section, const char *key, long def)
{
	return settings_get_int(store(), section, key, def);
}

void prefs_set_int(const char *section, const char *key, long value)
{
	if (settings_set_int(store(), section, key, value) == 0) save();
}

const char *prefs_get(const char *section, const char *key, const char *def)
{
	return settings_get(store(), section, key, def);
}

void prefs_set(const char *section, const char *key, const char *value)
{
	if (settings_set(store(), section, key, value) == 0) save();
}
