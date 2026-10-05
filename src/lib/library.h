/* library.h - the background library scan: walks every storage device (lib/scan_vita),
 * hashes new or changed books (lib/bookid) and upserts them into the database (lib/db).
 * Unchanged files (same path, size, mtime) are skipped without reading them. */
#ifndef NOCT_LIBRARY_H
#define NOCT_LIBRARY_H

/* Start a scan on a worker thread. No-op while one is running. db_open must have succeeded. */
void library_scan_start(void);
/* Ask a running scan to stop and wait for its thread. */
void library_scan_stop(void);

int library_scan_running(void);
/* Books seen by the running (or last) scan, unchanged ones included. */
int library_scan_found(void);
/* Bumped whenever the book list in the database changed (new books mid-scan, scan end).
 * Screens reload their list when it differs from the value they last saw. */
unsigned library_generation(void);

#endif
