/* http - blocking HTTP(S) over libcurl + mbedTLS (vendored, third_party/tls-mbedtls; the
 * host build uses the system libcurl). Called from worker threads only (net/fetch, net/dlq),
 * never from the main loop. Builds and tests on the host; the Vita network bring-up
 * (sceNet, sceNetCtl, sceSsl, sceHttp modules) is in http_vita.c. */
#ifndef NOCT_HTTP_H
#define NOCT_HTTP_H

#include <stddef.h>
#include <stdint.h>

#define HTTP_USER_AGENT "Noctopage/1.0 (PS Vita)"
#define HTTP_URL_MAX 2048

typedef struct {
	const char *user; /* HTTP Basic credentials; NULL or "" = none */
	const char *pass;
} http_auth_t;

typedef struct {
	char *data; /* NUL-terminated (one extra byte), free with http_doc_free */
	size_t len;
	long status;               /* HTTP status, 0 when no response arrived */
	char content_type[128];    /* "" when the server sent none */
	char final_url[HTTP_URL_MAX]; /* after redirects: resolve relative links against it */
} http_doc_t;

/* Once at boot (main thread). ca_file is the PEM bundle (app0:assets/cacert.pem on the
 * Vita); NULL uses curl's default (host tests). On the Vita it first brings the network
 * modules up. Returns 0, or -1 (network unavailable: every later call fails cleanly). */
int http_global_init(const char *ca_file);
void http_global_cleanup(void);

/* GET url into memory, following redirects (max 10), 20 s connect / 60 s stall timeouts.
 * Bodies larger than max_bytes are refused. *cancel (may be NULL) is polled during the
 * transfer; nonzero aborts it. Returns 0 on a 2xx status, else -1 with a short readable
 * reason in err ("HTTP 404", "Could not resolve host", "Too large", "Cancelled") and *out
 * zeroed except status. */
int http_get(const char *url, const http_auth_t *auth, size_t max_bytes, const volatile int *cancel,
             http_doc_t *out, char *err, size_t errcap);
void http_doc_free(http_doc_t *d);

/* Progress callback: bytes written so far (resume offset included) and the total (-1 when
 * unknown). Return nonzero to cancel. Called on the transfer's thread. */
typedef int (*http_progress_fn)(void *ud, int64_t now, int64_t total);

/* GET url into the file part_path. When resume_from > 0 and part_path holds that many
 * bytes, a Range request continues it (a server that ignores Range restarts the file).
 * filename (may be NULL) receives the server's suggested file name from
 * Content-Disposition, else the last path segment of the final URL, percent-decoded and
 * with characters FAT/exFAT refuse replaced by '_'. Returns 0 on success, else -1 with err
 * as for http_get ("Cancelled" when the callback stopped it). */
int http_download(const char *url, const http_auth_t *auth, const char *part_path,
                  int64_t resume_from, http_progress_fn progress, void *ud, char *filename,
                  size_t filename_cap, char *err, size_t errcap);

#endif
