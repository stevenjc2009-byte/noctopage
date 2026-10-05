/* scan_vita.h - sceIo filesystem ops for scan_tree, and the storage devices to probe. */
#ifndef NOCT_SCAN_VITA_H
#define NOCT_SCAN_VITA_H

#include "lib/scan.h"

/* The fs-ops table backed by sceIoDopen/Dread/Dclose. */
const scan_fs_t *scan_vita_fs(void);

/* Devices from docs/PLAN.md "Storage scan", in probe order. NULL-terminated. */
extern const char *const SCAN_VITA_DEVICES[];

/* 1 when the device root ("ux0:") can be listed. */
int scan_vita_device_present(const char *dev);

#endif
