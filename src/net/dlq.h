/* dlq - the download queue: one worker thread downloads books, one at a time, into
 * <books_dir>/<name>.part, renamed to <name> when complete. The queue lives in the database
 * table downloads (lib/db, state DB_DL_*), but only the MAIN THREAD touches the database:
 * the worker publishes progress in memory and dlq_pump() writes it through.
 *
 * Credentials are kept in memory only: after a restart a queued download from a server that
 * needs a login fails with "HTTP 401" and is retried from the catalog. */
#ifndef NOCT_DLQ_H
#define NOCT_DLQ_H

#include "net/http.h"

#include <stdint.h>

#define DLQ_MAX 64

typedef struct {
	int64_t id;
	char title[256];
	char dest_path[512]; /* final path ("" until the name is known) */
	int state;           /* DB_DL_* */
	int64_t bytes, total; /* total -1 when unknown */
	char error[128];
} dlq_item_t;

/* Main thread, after db_open. Loads the table: ACTIVE rows become QUEUED again and resume
 * from their .part file. Returns 0 or -1. */
int dlq_start(const char *books_dir);
/* Cancel the transfer (its .part file stays, so it resumes next time) and join. */
void dlq_stop(void);

/* Queue url. The file is named when it is queued, from title (the server's own file name is
 * not used): "<title>.<ext>" (ext from the format token, e.g. "epub", else the URL), made safe for FAT/exFAT
 * and unique in books_dir ("Name (2).epub"). Returns 0 and *out_id, or -1. */
int dlq_add(const char *url, const http_auth_t *auth, const char *title, const char *ext,
            int64_t *out_id);
/* Cancel a queued or active download (its .part file is removed). */
int dlq_cancel(int64_t id);
/* Queue a FAILED or CANCELLED download again. */
int dlq_retry(int64_t id);
/* Forget a finished, failed or cancelled download (the downloaded book stays). */
int dlq_remove(int64_t id);

/* Snapshot of the queue, oldest first. Returns the count. */
int dlq_list(dlq_item_t *out, int max);
/* Main thread, every frame: writes progress and state changes to the database (progress at
 * most once a second per download). Returns how many downloads completed since the last
 * call, so the caller can rescan the library. */
int dlq_pump(void);
/* 1 while something is queued or downloading. */
int dlq_busy(void);

#endif
