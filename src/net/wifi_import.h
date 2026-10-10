#ifndef NOCT_WIFI_IMPORT_H
#define NOCT_WIFI_IMPORT_H
#include <stdint.h>
typedef struct {
	int running, imported;
	uint64_t bytes, total;
	char url[64], code[7], file[256], message[160];
} wifi_import_state_t;
int wifi_import_start(void);
void wifi_import_stop(void);
void wifi_import_state(wifi_import_state_t *out);
int wifi_import_pump(void); /* successful book uploads since the previous call */
#endif
